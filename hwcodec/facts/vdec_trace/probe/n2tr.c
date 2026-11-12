/* SPDX-License-Identifier: MIT */
/*
 * n2tr.c -- DEC02 recording wrapper around a struct n2vc_hw.  See n2tr.h for the trace format
 * and the VDEC_ALL (window 12) model.  All state is guarded by one mutex: V40 runs GCE lists on
 * the daemon thread while the AP side powers blocks and waits for interrupts on its own thread.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n2tr.h"

#define VDEC_ALL_BASE  0x16000000u
#define VDEC_ALL_BYTES 0x30000u
#define VDEC_ALL_WORDS (VDEC_ALL_BYTES / 4u)
#define VDEC_ALL_WIN   12u

struct n2tr {
	struct n2vc_hw inner;
	FILE *f;
	pthread_mutex_t mu;
	unsigned refs[RWVC_BLK_COUNT];
	uint32_t *vdec_all;              /* VDEC_ALL register array, 0x30000 bytes */
	uint64_t events;
	uint64_t larb_acks;
	struct n2tr_stats st;
	/* Guest-thread tags: host threads in order of first appearance.  _Thread_local is not usable
	 * here because the tag must be stable across the calls of one host thread; a tiny table keyed
	 * by pthread_self() is enough (the vendor creates a handful of threads). */
	struct { pthread_t id; uint64_t seq; } thr[16];
	unsigned nthr;
};

static void ev(struct n2tr *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* The caller must hold t->mu. */
static unsigned thr_tag(struct n2tr *t, uint64_t *seq)
{
	pthread_t me = pthread_self();
	unsigned i;

	for (i = 0; i < t->nthr; i++) {
		if (pthread_equal(t->thr[i].id, me)) {
			*seq = ++t->thr[i].seq;
			return i;
		}
	}
	if (t->nthr >= 16) {
		*seq = 0;
		return 15;
	}
	i = t->nthr++;
	t->thr[i].id = me;
	t->thr[i].seq = 1;
	*seq = 1;
	return i;
}

static void ev(struct n2tr *t, const char *fmt, ...)
{
	va_list ap;
	uint64_t seq;
	unsigned tag;

	t->events++;
	if (t->f == NULL)
		return;
	tag = thr_tag(t, &seq);
	va_start(ap, fmt);
	vfprintf(t->f, fmt, ap);
	va_end(ap);
	fprintf(t->f, " t%u %llu\n", tag, (unsigned long long)seq);
	if ((t->events & 0x7ffu) == 0)
		fflush(t->f);
}

/* The kernel's RWVC_WIN_VDEC_ALL page map (ABI rev 3, DEC07; kern/bsd/dev/nord2_vcodec.inc
 * n2vc_vdec_page_blk() and tests/hw/n2vc_hw_fake.c vdec_all_page_block()): pages below 0x0d and
 * page 0x0e need VDE2/CORE (CORE also takes a hidden reference on SOC), larb5 at 0x0d and the
 * SOC/GCON/LAT range 0x0f..0x1f need VDE/SOC, and 0x20+ are VDE2/CORE again.  Using the same map
 * as the kernel keeps the recorder's power bookkeeping equal to what a phone run would enforce. */
static unsigned block_of_off(uint32_t off)
{
	uint32_t page = off >> 12;

	if (page < 0x0du || page == 0x0eu)
		return RWVC_BLK_VDEC_CORE;
	if (page < 0x20u)
		return RWVC_BLK_VDEC_SOC;
	return RWVC_BLK_VDEC_CORE;
}

static int powered(const struct n2tr *t, unsigned block)
{
	return block < RWVC_BLK_COUNT && t->refs[block] != 0;
}

static int tr_power(void *ctx, unsigned block, int on)
{
	struct n2tr *t = ctx;
	int rc;

	if (block >= RWVC_BLK_COUNT)
		return -EINVAL;
	pthread_mutex_lock(&t->mu);
	if (on) {
		t->refs[block]++;
		t->st.power_on[block]++;
		if (block == RWVC_BLK_VDEC_CORE)
			t->refs[RWVC_BLK_VDEC_SOC]++;   /* hidden ref, T02 fake power model */
	} else {
		if (t->refs[block])
			t->refs[block]--;
		t->st.power_off[block]++;
		if (block == RWVC_BLK_VDEC_CORE && t->refs[RWVC_BLK_VDEC_SOC])
			t->refs[RWVC_BLK_VDEC_SOC]--;
	}
	rc = t->inner.power ? t->inner.power(t->inner.ctx, block, on) : 0;
	ev(t, "P %u %s %d", block, on ? "on" : "off", rc);
	pthread_mutex_unlock(&t->mu);
	return rc;
}

/* One VDEC_ALL operation.  Returns 0 or -errno; *done = 1 when applied. */
static int vdec_all_op(struct n2tr *t, struct rwvc_reg_op *o, unsigned *done)
{
	uint32_t off = o->offset;
	unsigned blk = block_of_off(off);
	uint32_t *word;

	*done = 0;
	if ((off & 3u) || off + 4u > VDEC_ALL_BYTES) {
		ev(t, "BAD %u 0x%05x %d 0/1", VDEC_ALL_WIN, off, -EINVAL);
		return -EINVAL;
	}
	word = &t->vdec_all[off / 4u];
	if (!powered(t, blk)) {
		t->st.refused++;
		ev(t, "X %u 0x%05x %s %d", VDEC_ALL_WIN, off,
			o->op == RWVC_OP_READ ? "rd" : "wr", -EACCES);
		return -EACCES;
	}
	switch (o->op) {
	case RWVC_OP_READ:
		o->value = *word;
		t->st.reg_reads++;
		t->st.vdec_all_reads++;
		ev(t, "R %u 0x%05x 0x%08x", VDEC_ALL_WIN, off, *word);
		break;
	case RWVC_OP_WRITE:
		*word = o->value;
		/* SMI larb sleep-protect handshake: the vendor writes SLP_CON bit 0 and polls
		 * bit 16 until the larb is asleep/woken (vdec_hal_wait_larb_sleep_protect,
		 * libvcodec_utility.so 0x292e8; larb5 0x1600d00c, larb4 0x1602e00c).  The real
		 * larb acks; without this model the vendor spends 50000 failed reads per LOCK. */
		if (off == 0x0d00cu || off == 0x2e00cu) {
			if (o->value & 1u)
				*word |= 0x10000u;
			else
				*word &= ~0x10000u;
			t->larb_acks++;
		}
		t->st.reg_writes++;
		t->st.vdec_all_writes++;
		ev(t, "W %u 0x%05x 0x%08x", VDEC_ALL_WIN, off, o->value);
		break;
	case RWVC_OP_WRITE_MASK:
		*word = (*word & ~o->mask) | (o->value & o->mask);
		if (off == 0x0d00cu || off == 0x2e00cu) {
			if (o->value & 1u)
				*word |= 0x10000u;
			else
				*word &= ~0x10000u;
			t->larb_acks++;
		}
		t->st.reg_writes_mask++;
		t->st.vdec_all_writes++;
		ev(t, "WM %u 0x%05x 0x%08x 0x%08x", VDEC_ALL_WIN, off, o->value, o->mask);
		break;
	case RWVC_OP_POLL: {
		/* Bounded like T02's fake (which also has no per-op timeout field in this ABI):
		 * one read per millisecond up to 100 ms. */
		uint64_t reads = 0;
		struct timespec ts = { 0, 1000000 };
		uint32_t limit = 100u;

		for (;;) {
			uint32_t v = *word;

			reads++;
			if ((v & o->mask) == (o->value & o->mask)) {
				t->st.polls++;
				ev(t, "PLL %u 0x%05x 0x%08x 0x%08x 0 %llu", VDEC_ALL_WIN, off, o->value,
					o->mask, (unsigned long long)reads);
				break;
			}
			if (reads > limit) {
				ev(t, "PLL %u 0x%05x 0x%08x 0x%08x %d %llu", VDEC_ALL_WIN, off, o->value,
					o->mask, -ETIMEDOUT, (unsigned long long)reads);
				return -ETIMEDOUT;
			}
			nanosleep(&ts, NULL);
		}
		break;
	}
	default:
		return -EINVAL;
	}
	*done = 1;
	return 0;
}

static int tr_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct n2tr *t = ctx;
	unsigned i;
	int rc = 0;

	pthread_mutex_lock(&t->mu);
	for (i = 0; i < count; i++) {
		struct rwvc_reg_op *o = &ops[i];
		unsigned d = 0;
		int r;

		if (o->op == RWVC_OP_DELAY_US) {
			t->st.delays++;
			ev(t, "DLY %u", o->value);
			*done = i + 1;
			continue;
		}
		if (o->window == VDEC_ALL_WIN) {
			r = vdec_all_op(t, o, &d);
		} else if (o->window >= RWVC_WIN_COUNT) {
			r = -EINVAL;
		} else if (t->inner.regs == NULL) {
			r = -ENOSYS;
		} else {
			r = t->inner.regs(t->inner.ctx, o, 1, &d);
		}
		if (r != 0) {
			t->st.forwarding_errors++;
			ev(t, "BAD %u 0x%05x %d %u/%u", o->window, o->offset, r, d, 1u);
			rc = r;
			*done = i;
			break;
		}
		/* Record the frozen windows too; the back end already applied the op. */
		if (o->window != VDEC_ALL_WIN) {
			switch (o->op) {
			case RWVC_OP_READ:
				t->st.reg_reads++;
				ev(t, "R %u 0x%05x 0x%08x", o->window, o->offset, o->value);
				break;
			case RWVC_OP_WRITE:
				t->st.reg_writes++;
				ev(t, "W %u 0x%05x 0x%08x", o->window, o->offset, o->value);
				break;
			case RWVC_OP_WRITE_MASK:
				t->st.reg_writes_mask++;
				ev(t, "WM %u 0x%05x 0x%08x 0x%08x", o->window, o->offset, o->value,
					o->mask);
				break;
			default:
				break;
			}
		}
		*done = i + 1;
	}
	pthread_mutex_unlock(&t->mu);
	return rc;
}

static int tr_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct n2tr *t = ctx;
	int rc = -ENOSYS;

	pthread_mutex_lock(&t->mu);
	t->st.irq_waits++;
	if (t->inner.wait_irq)
		rc = t->inner.wait_irq(t->inner.ctx, irq, timeout_ms, status);
	ev(t, "IRQ %u %u %d 0x%08x", irq, timeout_ms, rc, status ? *status : 0u);
	pthread_mutex_unlock(&t->mu);
	return rc;
}

static int tr_irq_clear(void *ctx, unsigned irq)
{
	struct n2tr *t = ctx;
	int rc = 0;

	pthread_mutex_lock(&t->mu);
	t->st.irq_clears++;
	if (t->inner.irq_clear)
		rc = t->inner.irq_clear(t->inner.ctx, irq);
	ev(t, "IRQC %u", irq);
	pthread_mutex_unlock(&t->mu);
	return rc;
}

static int tr_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct n2tr *t = ctx;
	int rc = -ENOSYS;

	pthread_mutex_lock(&t->mu);
	if (t->inner.dma_alloc)
		rc = t->inner.dma_alloc(t->inner.ctx, bytes, flags, handle, pa);
	if (rc == 0)
		t->st.dma_allocs++;
	ev(t, "DA %u 0x%08x 0x%08x %d", bytes, handle ? *handle : 0u, pa ? *pa : 0u, rc);
	pthread_mutex_unlock(&t->mu);
	return rc;
}

static int tr_dma_free(void *ctx, uint32_t handle)
{
	struct n2tr *t = ctx;
	int rc = -ENOSYS;

	pthread_mutex_lock(&t->mu);
	if (t->inner.dma_free)
		rc = t->inner.dma_free(t->inner.ctx, handle);
	if (rc == 0)
		t->st.dma_frees++;
	ev(t, "DF 0x%08x %d", handle, rc);
	pthread_mutex_unlock(&t->mu);
	return rc;
}

static int tr_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct n2tr *t = ctx;
	int rc = -ENOSYS;

	pthread_mutex_lock(&t->mu);
	if (t->inner.dma_rw)
		rc = t->inner.dma_rw(t->inner.ctx, handle, offset, host, bytes, write);
	if (rc == 0)
		t->st.dma_rw++;
	ev(t, "DR 0x%08x 0x%06x %u %c %d", handle, offset, bytes, write ? 'W' : 'R', rc);
	pthread_mutex_unlock(&t->mu);
	return rc;
}

static void tr_close(void *ctx)
{
	struct n2tr *t = ctx;

	pthread_mutex_lock(&t->mu);
	if (t->inner.close)
		t->inner.close(t->inner.ctx);
	pthread_mutex_unlock(&t->mu);
}

struct n2tr *n2tr_open(const struct n2vc_hw *inner, const char *path)
{
	struct n2tr *t = calloc(1, sizeof(*t));

	if (t == NULL)
		return NULL;
	if (inner == NULL)
		goto fail;
	t->inner = *inner;
	t->vdec_all = calloc(VDEC_ALL_WORDS, sizeof(uint32_t));
	if (t->vdec_all == NULL)
		goto fail;
	if (path != NULL) {
		t->f = fopen(path, "w");
		if (t->f == NULL)
			goto fail;
		setvbuf(t->f, NULL, _IOFBF, 1u << 20);
		fprintf(t->f, "# N2DEC02 trace v1\n");
	}
	pthread_mutex_init(&t->mu, NULL);
	return t;
fail:
	free(t->vdec_all);
	free(t);
	return NULL;
}

const struct n2vc_hw *n2tr_hw(struct n2tr *t)
{
	static const struct n2vc_hw hw = {
		NULL, tr_power, tr_regs, tr_wait_irq, tr_irq_clear,
		tr_dma_alloc, tr_dma_free, tr_dma_rw, tr_close
	};
	static struct n2vc_hw out;

	if (t == NULL)
		return NULL;
	out = hw;
	out.ctx = t;
	return &out;
}

void n2tr_free(struct n2tr *t)
{
	if (t == NULL)
		return;
	if (t->f) {
		fflush(t->f);
		fclose(t->f);
	}
	pthread_mutex_destroy(&t->mu);
	free(t->vdec_all);
	free(t);
}

void n2tr_mark(struct n2tr *t, const char *phase)
{
	if (t == NULL)
		return;
	pthread_mutex_lock(&t->mu);
	ev(t, "MARK %s", phase ? phase : "");
	pthread_mutex_unlock(&t->mu);
}

void n2tr_stats(const struct n2tr *t, struct n2tr_stats *out)
{
	if (out == NULL)
		return;
	if (t == NULL) {
		memset(out, 0, sizeof(*out));
		return;
	}
	pthread_mutex_lock((pthread_mutex_t *)&t->mu);
	*out = t->st;
	pthread_mutex_unlock((pthread_mutex_t *)&t->mu);
}

int n2tr_powered(const struct n2tr *t, unsigned block)
{
	int on;

	if (t == NULL || block >= RWVC_BLK_COUNT)
		return 0;
	pthread_mutex_lock((pthread_mutex_t *)&t->mu);
	on = t->refs[block] != 0;
	pthread_mutex_unlock((pthread_mutex_t *)&t->mu);
	return on;
}

uint64_t n2tr_events(const struct n2tr *t)
{
	uint64_t n;

	if (t == NULL)
		return 0;
	pthread_mutex_lock((pthread_mutex_t *)&t->mu);
	n = t->events;
	pthread_mutex_unlock((pthread_mutex_t *)&t->mu);
	return n;
}
