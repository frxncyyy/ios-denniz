/* SPDX-License-Identifier: MIT */
/*
 * n2vc_hw_log.c -- X01: forwarding logger for the frozen struct n2vc_hw.
 *
 * See n2vc_hw_log.h for the line syntax and the exact guarantees. The
 * forwarding call is made before the log line is written and its return value
 * and out-parameters are never modified. The log mutex protects the FILE, the
 * byte counter and the truncation state, not the hardware call.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2vc_hw_log.h"

#if N2HW_HWLOG_MAX_MB < 0
#error "N2HW_HWLOG_MAX_MB must not be negative"
#endif

#define LOG_LINE   1024u
#define U64(x)     ((unsigned long long)(x))

static const char *const WIN_NAMES[RWVC_WIN_COUNT] = {
	"VENC", "VENC_C1", "VDEC_SYS", "VDEC_BASE", "VDEC_VLD", "VDEC_MC",
	"VDEC_MV", "VDEC_MISC", "VDEC_LAT_MISC", "VDEC_LAT_VLD",
	"VDEC_RACING", "VDEC_SOC_GCON",
};

static const char *const BLK_NAMES[RWVC_BLK_COUNT] = {
	"VENC", "VENC_C1", "VDEC_SOC", "VDEC_CORE",
};

struct log_state {
	pthread_mutex_t lock;
	FILE *fp;
	uint64_t cap_bytes;
	uint64_t bytes;
	uint64_t dropped;
	int truncated;
	int io_error;
	uint32_t crc_table[256];
	uint64_t (*clock_us)(void *arg);
	void *clock_arg;
	struct n2vc_hw inner;
};

static uint64_t (*g_test_clock)(void *arg);
static void *g_test_clock_arg;

void
n2vc_hw_log_test_set_clock(uint64_t (*clock_us)(void *arg), void *arg)
{
	g_test_clock = clock_us;
	g_test_clock_arg = arg;
}

static uint64_t
log_now(const struct log_state *st)
{
	struct timespec ts;

	if (st->clock_us != NULL)
		return st->clock_us(st->clock_arg);
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static const char *
win_name(unsigned win, char *buf, size_t n)
{
	if (win < RWVC_WIN_COUNT)
		return WIN_NAMES[win];
	(void)snprintf(buf, n, "WIN%u", win);
	return buf;
}

static const char *
blk_name(unsigned block, char *buf, size_t n)
{
	if (block < RWVC_BLK_COUNT)
		return BLK_NAMES[block];
	(void)snprintf(buf, n, "BLK%u", block);
	return buf;
}

/* IEEE 802.3 CRC-32: reflected poly 0xedb88320, init/xorout 0xffffffff. */
static void
crc_init(uint32_t *tab)
{
	unsigned i, k;

	for (i = 0; i < 256; i++) {
		uint32_t c = i;
		for (k = 0; k < 8; k++)
			c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
		tab[i] = c;
	}
}

static uint32_t
crc32_ieee(const uint32_t *tab, const void *p, size_t n)
{
	const uint8_t *b = p;
	uint32_t c = 0xffffffffu;
	size_t i;

	for (i = 0; i < n; i++)
		c = tab[(c ^ b[i]) & 0xffu] ^ (c >> 8);
	return ~c;
}

/*
 * Format one log line. Caller holds st->lock. Timestamps are taken here, so
 * the line order and the timestamp order agree. The cap check is conservative
 * by at most 20 bytes (the longest possible uint64 timestamp).
 */
static void
emit(struct log_state *st, const char *fmt, ...)
{
	char body[LOG_LINE];
	char line[LOG_LINE + 32u];
	va_list ap;
	uint64_t ts;
	int n, m;

	if (st->io_error || st->truncated) {
		st->dropped++;
		return;
	}
	va_start(ap, fmt);
	n = vsnprintf(body, sizeof body, fmt, ap);
	va_end(ap);
	if (n < 0) {
		st->io_error = 1;
		st->dropped++;
		return;
	}
	if ((size_t)n >= sizeof body) {
		/* A format bug: keep the line, mark it visibly. */
		body[sizeof body - 1u] = '\0';
		n = (int)sizeof body - 1;
	}
	if (st->bytes + (uint64_t)n + 1u + 21u > st->cap_bytes) {
		st->truncated = 1;
		st->dropped++;
		return;
	}
	ts = log_now(st);
	m = snprintf(line, sizeof line, "%llu %s", U64(ts), body);
	if (m < 0) {
		st->io_error = 1;
		st->dropped++;
		return;
	}
	if ((size_t)m >= sizeof line) {
		line[sizeof line - 1u] = '\0';
		m = (int)sizeof line - 1;
	}
	if (fprintf(st->fp, "%s\n", line) < 0) {
		st->io_error = 1;
		st->dropped++;
		return;
	}
	st->bytes += (uint64_t)m + 1u;
}

static int
log_power(void *ctx, unsigned block, int on)
{
	struct log_state *st = ctx;
	char b[24];
	const char *name = blk_name(block, b, sizeof b);
	int rc = st->inner.power(st->inner.ctx, block, on);

	pthread_mutex_lock(&st->lock);
	emit(st, "POWER %s %s -> %d", name, on ? "on" : "off", rc);
	pthread_mutex_unlock(&st->lock);
	return rc;
}

static void
emit_reg_op(struct log_state *st, const struct rwvc_reg_op *o, int rc, int failed)
{
	char wb[24];
	const char *w = win_name(o->window, wb, sizeof wb);
	unsigned off = (unsigned)o->offset;

	switch (o->op) {
	case RWVC_OP_READ:
		if (failed)
			emit(st, "R %s 0x%04x ? -> %d", w, off, rc);
		else
			emit(st, "R %s 0x%04x 0x%08x -> 0", w, off,
			     (unsigned)o->value);
		break;
	case RWVC_OP_WRITE:
		emit(st, "W %s 0x%04x 0x%08x -> %d", w, off,
		     (unsigned)o->value, failed ? rc : 0);
		break;
	case RWVC_OP_WRITE_MASK:
		emit(st, "W %s 0x%04x 0x%08x mask 0x%08x -> %d", w, off,
		     (unsigned)o->value, (unsigned)o->mask, failed ? rc : 0);
		break;
	case RWVC_OP_POLL:
		if (!failed)
			emit(st, "POLL %s 0x%04x 0x%08x mask 0x%08x -> ok", w, off,
			     (unsigned)o->value, (unsigned)o->mask);
		else if (rc == -ETIMEDOUT)
			emit(st, "POLL %s 0x%04x 0x%08x mask 0x%08x -> timeout", w, off,
			     (unsigned)o->value, (unsigned)o->mask);
		else
			emit(st, "POLL %s 0x%04x 0x%08x mask 0x%08x -> %d", w, off,
			     (unsigned)o->value, (unsigned)o->mask, rc);
		break;
	case RWVC_OP_DELAY_US:
		emit(st, "DELAY %u us -> %d", (unsigned)o->value, failed ? rc : 0);
		break;
	default:
		emit(st, "OP %u %s 0x%04x 0x%08x mask 0x%08x -> %d",
		     (unsigned)o->op, w, off, (unsigned)o->value,
		     (unsigned)o->mask, failed ? rc : 0);
		break;
	}
}

static int
log_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct log_state *st = ctx;
	unsigned d = 0, i, n;
	int valid = count <= RWVC_REG_MAX_OPS && (count == 0 || ops != NULL);
	int rc = st->inner.regs(st->inner.ctx, ops, count, done);

	if (done != NULL)
		d = *done;
	else if (rc == 0)
		d = count;   /* the header says all ops completed on success */

	pthread_mutex_lock(&st->lock);
	if (!valid) {
		emit(st, "REGS invalid count=%u ops=%s -> %d", count,
		     ops != NULL ? "set" : "null", rc);
	} else if (done == NULL && rc != 0) {
		/* The caller asked for no out_done, so the failing index is not
		 * observable; log the batch result without pretending to know it. */
		emit(st, "REGS count=%u -> %d (no out_done)", count, rc);
	} else {
		n = d < count ? d : count;
		for (i = 0; i < n; i++)
			emit_reg_op(st, &ops[i], 0, 0);
		if (rc != 0 && d < count)
			emit_reg_op(st, &ops[d], rc, 1);
	}
	pthread_mutex_unlock(&st->lock);
	return rc;
}

static int
log_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct log_state *st = ctx;
	uint32_t got;
	int rc = st->inner.wait_irq(st->inner.ctx, irq, timeout_ms, status);

	got = status != NULL ? *status : 0;

	pthread_mutex_lock(&st->lock);
	if (status == NULL) {
		if (rc == 0)
			emit(st, "IRQ wait %u timeout_ms=%u -> 0 status=none",
			     irq, timeout_ms);
		else if (rc == -ETIMEDOUT)
			emit(st, "IRQ wait %u timeout_ms=%u -> -ETIMEDOUT status=none",
			     irq, timeout_ms);
		else
			emit(st, "IRQ wait %u timeout_ms=%u -> %d status=none",
			     irq, timeout_ms, rc);
	} else if (rc == 0) {
		emit(st, "IRQ wait %u timeout_ms=%u -> 0 status=0x%08x",
		     irq, timeout_ms, (unsigned)got);
	} else if (rc == -ETIMEDOUT) {
		emit(st, "IRQ wait %u timeout_ms=%u -> -ETIMEDOUT status=0x%08x",
		     irq, timeout_ms, (unsigned)got);
	} else {
		emit(st, "IRQ wait %u timeout_ms=%u -> %d status=0x%08x",
		     irq, timeout_ms, rc, (unsigned)got);
	}
	pthread_mutex_unlock(&st->lock);
	return rc;
}

static int
log_irq_clear(void *ctx, unsigned irq)
{
	struct log_state *st = ctx;
	int rc = st->inner.irq_clear(st->inner.ctx, irq);

	pthread_mutex_lock(&st->lock);
	emit(st, "IRQ clear %u -> %d", irq, rc);
	pthread_mutex_unlock(&st->lock);
	return rc;
}

static int
log_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle,
	      uint32_t *pa)
{
	struct log_state *st = ctx;
	int rc = st->inner.dma_alloc(st->inner.ctx, bytes, flags, handle, pa);

	pthread_mutex_lock(&st->lock);
	if (rc == 0 && handle != NULL && pa != NULL)
		emit(st, "DMA alloc handle=%u pa=0x%08x bytes=%u flags=0x%x -> 0",
		     (unsigned)*handle, (unsigned)*pa, (unsigned)bytes,
		     (unsigned)flags);
	else
		emit(st, "DMA alloc bytes=%u flags=0x%x -> %d", (unsigned)bytes,
		     (unsigned)flags, rc);
	pthread_mutex_unlock(&st->lock);
	return rc;
}

static int
log_dma_free(void *ctx, uint32_t handle)
{
	struct log_state *st = ctx;
	int rc = st->inner.dma_free(st->inner.ctx, handle);

	pthread_mutex_lock(&st->lock);
	emit(st, "DMA free handle=%u -> %d", (unsigned)handle, rc);
	pthread_mutex_unlock(&st->lock);
	return rc;
}

static int
log_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host,
	   uint32_t bytes, int write)
{
	struct log_state *st = ctx;
	int rc = st->inner.dma_rw(st->inner.ctx, handle, offset, host, bytes,
				  write);
	uint32_t crc = 0;
	int have_crc = rc == 0 && host != NULL;

	if (have_crc)
		crc = crc32_ieee(st->crc_table, host, bytes);

	pthread_mutex_lock(&st->lock);
	if (have_crc)
		emit(st, "DMA rw handle=%u off=0x%04x bytes=%u %c crc32=0x%08x -> 0",
		     (unsigned)handle, (unsigned)offset, (unsigned)bytes,
		     write ? 'W' : 'R', (unsigned)crc);
	else
		emit(st, "DMA rw handle=%u off=0x%04x bytes=%u %c -> %d",
		     (unsigned)handle, (unsigned)offset, (unsigned)bytes,
		     write ? 'W' : 'R', rc);
	pthread_mutex_unlock(&st->lock);
	return rc;
}

static void
log_close(void *ctx)
{
	struct log_state *st = ctx;
	struct n2vc_hw inner;

	if (st == NULL)
		return;
	inner = st->inner;
	pthread_mutex_lock(&st->lock);
	if (st->truncated && !st->io_error && st->fp != NULL) {
		uint64_t ts = log_now(st);
		(void)fprintf(st->fp,
			      "%llu HWLOG truncated limit_bytes=%llu"
			      " bytes=%llu dropped=%llu\n",
			      U64(ts), U64(st->cap_bytes), U64(st->bytes),
			      U64(st->dropped));
	}
	if (st->fp != NULL) {
		(void)fflush(st->fp);
		(void)fsync(fileno(st->fp));
		(void)fclose(st->fp);
		st->fp = NULL;
	}
	pthread_mutex_unlock(&st->lock);
	pthread_mutex_destroy(&st->lock);
	free(st);
	if (inner.close != NULL)
		inner.close(inner.ctx);
}

int
n2vc_hw_wrap_log(struct n2vc_hw *out, const struct n2vc_hw *inner,
		 const char *path)
{
	struct log_state *st;
	struct n2vc_hw copy;
	FILE *fp;
	int prc;

	if (out == NULL || inner == NULL || path == NULL || *path == '\0')
		return -EINVAL;
	if (inner->power == NULL || inner->regs == NULL ||
	    inner->wait_irq == NULL || inner->irq_clear == NULL ||
	    inner->dma_alloc == NULL || inner->dma_free == NULL ||
	    inner->dma_rw == NULL)
		return -EINVAL;

	copy = *inner;
	fp = fopen(path, "w");
	if (fp == NULL)
		return errno != 0 ? -errno : -EIO;

	st = calloc(1, sizeof *st);
	if (st == NULL) {
		(void)fclose(fp);
		return -ENOMEM;
	}
	prc = pthread_mutex_init(&st->lock, NULL);
	if (prc != 0) {
		(void)fclose(fp);
		free(st);
		return -prc;
	}
	st->fp = fp;
	st->cap_bytes = (uint64_t)(N2HW_HWLOG_MAX_MB) * 1024u * 1024u;
	st->inner = copy;
	st->clock_us = g_test_clock;
	st->clock_arg = g_test_clock_arg;
	crc_init(st->crc_table);

	out->ctx = st;
	out->power = log_power;
	out->regs = log_regs;
	out->wait_irq = log_wait_irq;
	out->irq_clear = log_irq_clear;
	out->dma_alloc = log_dma_alloc;
	out->dma_free = log_dma_free;
	out->dma_rw = log_dma_rw;
	out->close = log_close;
	return 0;
}
