/* SPDX-License-Identifier: MIT */
/*
 * n2vc_hw_fake.c -- T02: the host fake of include/n2vc_hw.h.
 *
 * One fake instance models the four MT6893 power blocks and the thirteen
 * register windows of include/redwood_vcodec.h (ABI rev 3 adds VDEC_ALL),
 * records every operation as a text line, and executes rules from a small text
 * file so that interpreted vendor code can make progress without a phone.
 * Everything is bounded and every error is returned, never printed or asserted.
 *
 * The window -> power block mapping is the one K02 documented in
 * gpu/hwcodec/kern/VCODEC_HW.md section 0 (from mt6893.dts and
 * mtk_vcodec_dec_pm.c).  Power implications VENC_C1 -> VEN and
 * VDEC_CORE -> VDEC_SOC are the hidden references of RWVC_POWER in that file
 * (section 7).  RWVC_WIN_VDEC_ALL is decided per 4 KiB page: 0x00-0x0c and
 * 0x0e and 0x20-0x2f need VDEC_CORE, 0x0d and 0x0f-0x1f need VDEC_SOC, and the
 * two kernel-owned larb pages (0x0d000 larb5, 0x2e000 larb4) accept a write
 * only at offset 0x00c bit 0, the vendor's sleep-protect word.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>

#include "n2vc_hw_fake.h"

#define N2VC_FAKE_WIN_WORDS (0x4000u / 4u)
#define N2VC_FAKE_MAX_RULES 256u
#define N2VC_FAKE_MAX_DMA   256u
#define N2VC_FAKE_MAX_DUE   64u
#define N2VC_FAKE_CHAIN_MAX 32u
#define N2VC_FAKE_POLL_MAX  1000000u
#define N2VC_FAKE_LINE      512u
#define N2VC_FAKE_BLK_PAGE  0xffu              /* win_desc.block: decided per 4 KiB page (VDEC_ALL) */
#define N2VC_FAKE_ALL_BYTES 0x30000u           /* RWVC_WIN_VDEC_ALL */

struct fake_win {
	uint32_t value[N2VC_FAKE_WIN_WORDS];
};

struct fake_dma {
	int used;
	uint32_t handle;
	uint32_t pa;
	uint32_t bytes;
	uint32_t flags;
	uint8_t *host;
};

enum fake_rule_kind { FAKE_SET = 1, FAKE_IRQ = 2, FAKE_READ = 3, FAKE_DEFAULT = 4 };

struct fake_rule {
	int kind;
	unsigned win;
	uint32_t off;
	uint32_t value;
	uint32_t mask;
	unsigned awin;
	uint32_t aoff;
	uint32_t aval;
	unsigned irq;
	uint32_t delay_ms;
	int once;
	int used;
	int status_only;
};

struct n2vc_hw_fake {
	pthread_mutex_t lock;
	FILE *rec;
	char err[192];

	unsigned user_refs[RWVC_BLK_COUNT];
	unsigned hidden_refs[RWVC_BLK_COUNT];
	struct fake_win win[RWVC_WIN_COUNT];
	uint8_t *vdec_all;

	uint32_t irq_status[RWVC_IRQ_COUNT];
	unsigned irq_pending[RWVC_IRQ_COUNT];
	uint64_t irq_due[RWVC_IRQ_COUNT][N2VC_FAKE_MAX_DUE];
	unsigned irq_ndue[RWVC_IRQ_COUNT];

	struct fake_rule rules[N2VC_FAKE_MAX_RULES];
	unsigned nrules;

	struct fake_dma dma[N2VC_FAKE_MAX_DMA];
	uint32_t next_handle;
	int core_off_failed;      /* opus-decint: VDEC_CORE parent reference kept after a replayed failed power-off */

	uint32_t poll_timeout_us;
	struct n2vc_hw_fake_stats stats;
};

struct win_desc {
	const char *name;
	uint32_t bytes;
	unsigned block;
};

/* K02, VCODEC_HW.md section 0. */
static const struct win_desc WINS[RWVC_WIN_COUNT] = {
	{ "VENC",         0x2000, RWVC_BLK_VENC },
	{ "VENC_C1",      0x2000, RWVC_BLK_VENC_C1 },
	{ "VDEC_SYS",     0x1000, RWVC_BLK_VDEC_CORE },
	{ "VDEC_BASE",    0x1000, RWVC_BLK_VDEC_CORE },
	{ "VDEC_VLD",     0x1000, RWVC_BLK_VDEC_CORE },
	{ "VDEC_MC",      0x1000, RWVC_BLK_VDEC_CORE },
	{ "VDEC_MV",      0x1000, RWVC_BLK_VDEC_CORE },
	{ "VDEC_MISC",    0x4000, RWVC_BLK_VDEC_CORE },
	{ "VDEC_LAT_MISC", 0x1000, RWVC_BLK_VDEC_SOC },
	{ "VDEC_LAT_VLD", 0x0400, RWVC_BLK_VDEC_SOC },
	{ "VDEC_RACING",  0x1000, RWVC_BLK_VDEC_CORE },
	{ "VDEC_SOC_GCON", 0x1000, RWVC_BLK_VDEC_SOC },
	{ "VDEC_ALL",     N2VC_FAKE_ALL_BYTES, N2VC_FAKE_BLK_PAGE },
};

static const char *const BLOCK_NAMES[RWVC_BLK_COUNT] = {
	"VENC", "VENC_C1", "VDEC_SOC", "VDEC_CORE"
};

static struct n2vc_hw_fake *fake_of(const struct n2vc_hw *hw)
{
	return hw ? (struct n2vc_hw_fake *)hw->ctx : NULL;
}

static uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void sleep_us(unsigned us)
{
	struct timespec ts;
	ts.tv_sec = us / 1000000u;
	ts.tv_nsec = (long)(us % 1000000u) * 1000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

static void recf(struct n2vc_hw_fake *f, const char *fmt, ...)
{
	va_list ap;

	if (!f->rec)
		return;
	va_start(ap, fmt);
	vfprintf(f->rec, fmt, ap);
	va_end(ap);
	fputc('\n', f->rec);
	fflush(f->rec);
}

static int block_on(const struct n2vc_hw_fake *f, unsigned block)
{
	if (block >= RWVC_BLK_COUNT)
		return 0;
	return f->user_refs[block] + f->hidden_refs[block] != 0;
}

static int check_reg(unsigned win, uint32_t off)
{
	if (win >= RWVC_WIN_COUNT)
		return -EINVAL;
	if ((off & 3u) != 0)
		return -EINVAL;
	if (off > WINS[win].bytes || WINS[win].bytes - off < 4u)
		return -EINVAL;
	return 0;
}

static uint32_t read_word(const struct n2vc_hw_fake *f, unsigned win, uint32_t off)
{
	if (win == RWVC_WIN_VDEC_ALL) {
		uint32_t v;
		memcpy(&v, f->vdec_all + off, 4);
		return v;
	}
	return f->win[win].value[off / 4u];
}

static void write_word(struct n2vc_hw_fake *f, unsigned win, uint32_t off, uint32_t v)
{
	if (win == RWVC_WIN_VDEC_ALL) {
		memcpy(f->vdec_all + off, &v, 4);
		return;
	}
	f->win[win].value[off / 4u] = v;
}

/* RWVC_WIN_VDEC_ALL's page -> block map (ABI rev 3; facts/VDEC_PROTOCOL.md section 4 and K02's per-window table). */
static unsigned vdec_all_page_block(uint32_t page)
{
	if (page < 0x0du)
		return RWVC_BLK_VDEC_CORE;
	if (page == 0x0du)
		return RWVC_BLK_VDEC_SOC;      /* larb5 */
	if (page == 0x0eu)
		return RWVC_BLK_VDEC_CORE;     /* larb6 */
	if (page < 0x20u)
		return RWVC_BLK_VDEC_SOC;      /* SOC_GCON + LAT */
	return RWVC_BLK_VDEC_CORE;         /* VLD .. VDEC_SYS + larb4 */
}

/* the block a window word belongs to; RWVC_BLK_COUNT for an unknown window */
static unsigned win_block(unsigned win, uint32_t off)
{
	if (win >= RWVC_WIN_COUNT)
		return RWVC_BLK_COUNT;
	if (win == RWVC_WIN_VDEC_ALL)
		return vdec_all_page_block(off >> 12);
	return WINS[win].block;
}

/* The kernel owns the larb configuration.  Same allow-list as the kernel (n2vc_vdec_allow[]): larb5 0x1600d00c and
 * larb4 0x1602e00c bit 0 (SMI_LARB_SLP_CON, the vendor's sleep-protect handshake; opus-dec1/opus-dec2 host traces).
 * A write to a listed word may only change the bits in its mask; every other larb word refuses a write. */
static const struct { uint8_t page; uint32_t off, wmask; } LARB_ALLOW[] = {
	{ 0x0du, 0x00cu, 0x00000001u },
	{ 0x2eu, 0x00cu, 0x00000001u },
};

static int larb_write_refused(unsigned win, uint32_t off, uint32_t bits)
{
	uint32_t page;
	unsigned i;

	if (win != RWVC_WIN_VDEC_ALL)
		return 0;
	page = off >> 12;
	if (page != 0x0du && page != 0x2eu)
		return 0;
	for (i = 0; i < sizeof LARB_ALLOW / sizeof LARB_ALLOW[0]; i++)
		if (LARB_ALLOW[i].page == page && LARB_ALLOW[i].off == (off & 0xfffu))
			return (bits & ~LARB_ALLOW[i].wmask) != 0;
	return 1;
}

static int find_read_rule(struct n2vc_hw_fake *f, unsigned win, uint32_t off, uint32_t *v)
{
	unsigned i;

	for (i = 0; i < f->nrules; i++) {
		struct fake_rule *r = &f->rules[i];
		if (r->kind != FAKE_READ || r->win != win || r->off != off)
			continue;
		if (r->once && r->used)
			continue;
		if (r->once)
			r->used = 1;
		*v = r->value;
		return 1;
	}
	return 0;
}

static int irq_schedule(struct n2vc_hw_fake *f, unsigned irq, uint32_t delay_ms)
{
	if (irq >= RWVC_IRQ_COUNT)
		return -EINVAL;
	if (delay_ms == 0) {
		f->irq_pending[irq]++;
		f->stats.irq_fired++;
		recf(f, "SCRIPT irq %u now", irq);
		return 0;
	}
	if (f->irq_ndue[irq] >= N2VC_FAKE_MAX_DUE)
		return -ENOSPC;
	f->irq_due[irq][f->irq_ndue[irq]++] = now_us() + (uint64_t)delay_ms * 1000u;
	recf(f, "SCRIPT irq %u in %u ms", irq, delay_ms);
	return 0;
}

static void irq_collect(struct n2vc_hw_fake *f)
{
	uint64_t now = now_us();
	unsigned irq;

	for (irq = 0; irq < RWVC_IRQ_COUNT; irq++) {
		unsigned i, keep = 0;
		for (i = 0; i < f->irq_ndue[irq]; i++) {
			if (f->irq_due[irq][i] <= now) {
				f->irq_pending[irq]++;
				f->stats.irq_fired++;
			} else {
				f->irq_due[irq][keep++] = f->irq_due[irq][i];
			}
		}
		f->irq_ndue[irq] = keep;
	}
}

static int apply_rules(struct n2vc_hw_fake *f, unsigned win, uint32_t off, uint32_t trigger, unsigned depth)
{
	unsigned i;

	if (depth > N2VC_FAKE_CHAIN_MAX) {
		f->stats.violations++;
		snprintf(f->err, sizeof f->err, "rule chain longer than %u", N2VC_FAKE_CHAIN_MAX);
		recf(f, "SCRIPT error %s", f->err);
		return -ELOOP;
	}
	for (i = 0; i < f->nrules; i++) {
		struct fake_rule *r = &f->rules[i];
		int rc;

		if (r->win != win || r->off != off)
			continue;
		if ((trigger & r->mask) != (r->value & r->mask))
			continue;
		if (r->kind == FAKE_READ || r->kind == FAKE_DEFAULT)
			continue;
		if (r->status_only)
			continue;
		if (r->once && r->used)
			continue;
		if (r->once)
			r->used = 1;
		if (r->kind == FAKE_IRQ) {
			rc = irq_schedule(f, r->irq, r->delay_ms);
			if (rc)
				return rc;
			continue;
		}
		if (r->kind != FAKE_SET)
			continue;
		if (check_reg(r->awin, r->aoff) != 0) {
			f->stats.violations++;
			snprintf(f->err, sizeof f->err, "set target window/offset invalid");
			recf(f, "SCRIPT error %s", f->err);
			return -EINVAL;
		}
		if (larb_write_refused(r->awin, r->aoff, r->aval)) {
			f->stats.violations++;
			snprintf(f->err, sizeof f->err, "set target %s 0x%04x is a read-only larb word",
				 WINS[r->awin].name, r->aoff);
			recf(f, "SCRIPT error %s", f->err);
			return -EACCES;
		}
		if (!block_on(f, win_block(r->awin, r->aoff))) {
			f->stats.violations++;
			snprintf(f->err, sizeof f->err, "set target %s is not powered",
				 WINS[r->awin].name);
			recf(f, "SCRIPT error %s", f->err);
			return -EACCES;
		}
		write_word(f, r->awin, r->aoff, r->aval);
		f->stats.script_actions++;
		recf(f, "SCRIPT set %s 0x%04x = 0x%08x", WINS[r->awin].name, r->aoff, r->aval);
		rc = apply_rules(f, r->awin, r->aoff, r->aval, depth + 1u);
		if (rc)
			return rc;
	}
	return 0;
}

static int fake_power(void *ctx, unsigned block, int on)
{
	struct n2vc_hw_fake *f = ctx;
	unsigned parent = RWVC_BLK_COUNT;
	int rc = 0;

	if (!f || block >= RWVC_BLK_COUNT)
		return -EINVAL;
	if (block == RWVC_BLK_VENC_C1)
		parent = RWVC_BLK_VENC;
	else if (block == RWVC_BLK_VDEC_CORE)
		parent = RWVC_BLK_VDEC_SOC;

	pthread_mutex_lock(&f->lock);
	if (on) {
		f->user_refs[block]++;
		if (block == RWVC_BLK_VDEC_CORE && f->core_off_failed) {
			if (f->user_refs[block] == 1)
				f->core_off_failed = 0;     /* the parent reference kept by the failed off serves again */
		} else if (parent != RWVC_BLK_COUNT) {
			f->hidden_refs[parent]++;
		}
	} else if (f->user_refs[block] == 0) {
		rc = -EINVAL;
	} else {
		f->user_refs[block]--;
		/* opus-decint: N2VC_FAKE_VDEC_CORE_OFF_FAIL=1 replays phone run vc19 (q10je): the last VDEC_CORE power-off
		 * times out on the bus-protection ack -> ETIMEDOUT, the reference is gone, the parent's is kept. */
		if (block == RWVC_BLK_VDEC_CORE && f->user_refs[block] == 0 && getenv("N2VC_FAKE_VDEC_CORE_OFF_FAIL")) {
			f->core_off_failed = 1;
			rc = -ETIMEDOUT;
		} else if (parent != RWVC_BLK_COUNT) {
			f->hidden_refs[parent]--;
		}
	}
	recf(f, "POWER %s %s -> %d", BLOCK_NAMES[block], on ? "on" : "off", rc);
	pthread_mutex_unlock(&f->lock);
	return rc;
}

static int fake_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct n2vc_hw_fake *f = ctx;
	unsigned i, d = 0;
	int rc = 0;

	if (!f)
		return -EINVAL;
	if (count > RWVC_REG_MAX_OPS || (count != 0 && ops == NULL)) {
		if (done)
			*done = 0;
		return -EINVAL;
	}
	pthread_mutex_lock(&f->lock);
	for (i = 0; i < count; i++) {
		struct rwvc_reg_op *o = &ops[i];
		uint32_t v;
		char opc = '?';

		if (o->op == RWVC_OP_DELAY_US) {
			if (o->value > 1000u) {
				rc = -EINVAL;
				break;
			}
			f->stats.delays++;
			recf(f, "DELAY %u us", o->value);
			pthread_mutex_unlock(&f->lock);
			sleep_us(o->value);
			pthread_mutex_lock(&f->lock);
			d++;
			continue;
		}
		rc = check_reg(o->window, o->offset);
		if (rc != 0)
			break;
		opc = o->op == RWVC_OP_READ ? 'R' :
		      (o->op == RWVC_OP_WRITE || o->op == RWVC_OP_WRITE_MASK) ? 'W' : 'P';
		if (!block_on(f, win_block(o->window, o->offset))) {
			f->stats.violations++;
			recf(f, "%c %s 0x%04x EACCES", opc, WINS[o->window].name, o->offset);
			rc = -EACCES;
			break;
		}
		if ((o->op == RWVC_OP_WRITE || o->op == RWVC_OP_WRITE_MASK) &&
		    larb_write_refused(o->window, o->offset,
				       o->op == RWVC_OP_WRITE_MASK ? (o->value & o->mask) : o->value)) {
			f->stats.violations++;
			recf(f, "%c %s 0x%04x EACCES (kernel-owned larb word)",
			     opc, WINS[o->window].name, o->offset);
			rc = -EACCES;
			break;
		}
		switch (o->op) {
		case RWVC_OP_READ:
			if (find_read_rule(f, o->window, o->offset, &v))
				recf(f, "R %s 0x%04x 0x%08x script", WINS[o->window].name, o->offset, v);
			else {
				v = read_word(f, o->window, o->offset);
				recf(f, "R %s 0x%04x 0x%08x", WINS[o->window].name, o->offset, v);
			}
			o->value = v;
			f->stats.reads++;
			break;
		case RWVC_OP_WRITE:
			write_word(f, o->window, o->offset, o->value);
			f->stats.writes++;
			recf(f, "W %s 0x%04x 0x%08x", WINS[o->window].name, o->offset, o->value);
			rc = apply_rules(f, o->window, o->offset, o->value, 0);
			break;
		case RWVC_OP_WRITE_MASK:
			v = read_word(f, o->window, o->offset);
			v = (v & ~o->mask) | (o->value & o->mask);
			write_word(f, o->window, o->offset, v);
			f->stats.masked_writes++;
			recf(f, "W %s 0x%04x 0x%08x mask 0x%08x", WINS[o->window].name, o->offset,
			     o->value, o->mask);
			rc = apply_rules(f, o->window, o->offset, o->value & o->mask, 0);
			break;
		case RWVC_OP_POLL: {
			uint64_t deadline = now_us() + f->poll_timeout_us;
			unsigned reads = 0;
			int ok = 0;

			for (;;) {
				if (find_read_rule(f, o->window, o->offset, &v))
					;
				else
					v = read_word(f, o->window, o->offset);
				reads++;
				if ((v & o->mask) == (o->value & o->mask)) {
					ok = 1;
					break;
				}
				if (now_us() >= deadline)
					break;
				pthread_mutex_unlock(&f->lock);
				sleep_us(10);
				pthread_mutex_lock(&f->lock);
			}
			f->stats.polls++;
			if (ok) {
				recf(f, "POLL %s 0x%04x 0x%08x mask 0x%08x -> ok reads=%u",
				     WINS[o->window].name, o->offset, o->value, o->mask, reads);
			} else {
				recf(f, "POLL %s 0x%04x 0x%08x mask 0x%08x -> timeout reads=%u",
				     WINS[o->window].name, o->offset, o->value, o->mask, reads);
				rc = -ETIMEDOUT;
			}
			break;
		}
		default:
			rc = -EINVAL;
			break;
		}
		/* opus-decint: SMI_LARB_SLP_CON hardware behaviour (larb5 0x0d00c, larb4 0x2e00c): bit 16
		 * (SLP_PROT_RDY, read-only) follows the bit-0 request, so the vendor's 50000-read poll ends
		 * at once, as on the silicon.  A script "set" cannot do this (the allow-list refuses it). */
		if (rc == 0 && o->window == RWVC_WIN_VDEC_ALL && (o->offset == 0x0d00cu || o->offset == 0x2e00cu) &&
		    (o->op == RWVC_OP_WRITE || o->op == RWVC_OP_WRITE_MASK)) {
			v = read_word(f, o->window, o->offset);
			v = (v & ~0x00010000u) | ((v & 1u) << 16);
			write_word(f, o->window, o->offset, v);
		}
		if (rc != 0)
			break;
		d++;
	}
	if (done)
		*done = d;
	pthread_mutex_unlock(&f->lock);
	return rc;
}

static int fake_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct n2vc_hw_fake *f = ctx;
	uint64_t deadline;
	int rc = 0;

	if (!f || irq >= RWVC_IRQ_COUNT || timeout_ms > 5000u)
		return -EINVAL;
	deadline = now_us() + (uint64_t)timeout_ms * 1000u;
	pthread_mutex_lock(&f->lock);
	for (;;) {
		irq_collect(f);
		if (f->irq_pending[irq] != 0) {
			f->irq_pending[irq]--;
			if (status)
				*status = f->irq_status[irq];
			f->stats.irq_waits++;
			recf(f, "IRQ wait %u timeout_ms=%u -> 0 status=0x%08x", irq, timeout_ms,
			     f->irq_status[irq]);
			break;
		}
		if (now_us() >= deadline) {
			if (status)
				*status = f->irq_status[irq];
			f->stats.irq_waits++;
			f->stats.irq_timeouts++;
			recf(f, "IRQ wait %u timeout_ms=%u -> -ETIMEDOUT status=0x%08x", irq,
			     timeout_ms, f->irq_status[irq]);
			rc = -ETIMEDOUT;
			break;
		}
		pthread_mutex_unlock(&f->lock);
		sleep_us(1000);
		pthread_mutex_lock(&f->lock);
	}
	pthread_mutex_unlock(&f->lock);
	return rc;
}

static int fake_irq_clear(void *ctx, unsigned irq)
{
	struct n2vc_hw_fake *f = ctx;

	if (!f || irq >= RWVC_IRQ_COUNT)
		return -EINVAL;
	pthread_mutex_lock(&f->lock);
	f->irq_pending[irq] = 0;
	f->irq_ndue[irq] = 0;
	recf(f, "IRQ clear %u", irq);
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static struct fake_dma *find_dma(struct n2vc_hw_fake *f, uint32_t handle)
{
	unsigned i;
	for (i = 0; i < N2VC_FAKE_MAX_DMA; i++)
		if (f->dma[i].used && f->dma[i].handle == handle)
			return &f->dma[i];
	return NULL;
}

static uint32_t next_free_handle(struct n2vc_hw_fake *f)
{
	for (;;) {
		uint32_t h = f->next_handle++;
		if (h == 0)
			continue;
		if (!find_dma(f, h))
			return h;
	}
}

static int fake_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct n2vc_hw_fake *f = ctx;
	uint32_t need, candidate, end;
	unsigned i, slot = N2VC_FAKE_MAX_DMA;

	if (!f || !handle || !pa || bytes == 0)
		return -EINVAL;
	/* Kernel parity (nord2_vcodec.inc RWVC_DMA_ALLOC: flags & ~RWVC_DMA_SHARED -> EINVAL).  Phone run vc10 failed
	 * the first OPEN on exactly this: the shim forwarded ION cache flags, which the fake had silently accepted. */
	if (flags & ~RWVC_DMA_SHARED)
		return -EINVAL;
	if (bytes > RW_CLAIM_VCODEC_BYTES)
		return -ENOMEM;
	need = (bytes + 4095u) & ~4095u;
	if (need < bytes || need > RW_CLAIM_VCODEC_BYTES)
		return -ENOMEM;
	pthread_mutex_lock(&f->lock);
	for (i = 0; i < N2VC_FAKE_MAX_DMA; i++)
		if (!f->dma[i].used) {
			slot = i;
			break;
		}
	if (slot == N2VC_FAKE_MAX_DMA) {
		pthread_mutex_unlock(&f->lock);
		return -ENOMEM;
	}
	candidate = RW_CLAIM_VCODEC_PA;
	end = RW_CLAIM_VCODEC_PA + RW_CLAIM_VCODEC_BYTES;
	for (;;) {
		int moved = 0;
		for (i = 0; i < N2VC_FAKE_MAX_DMA; i++) {
			struct fake_dma *d = &f->dma[i];
			uint32_t d_end;
			if (!d->used)
				continue;
			d_end = d->pa + d->bytes;
			if (candidate < d_end && d->pa < candidate + need) {
				candidate = (d_end + 4095u) & ~4095u;
				moved = 1;
			}
		}
		if (!moved)
			break;
	}
	if (candidate > end || end - candidate < need) {
		pthread_mutex_unlock(&f->lock);
		return -ENOMEM;
	}
	f->dma[slot].host = calloc(1, need);
	if (!f->dma[slot].host) {
		pthread_mutex_unlock(&f->lock);
		return -ENOMEM;
	}
	f->dma[slot].used = 1;
	f->dma[slot].handle = next_free_handle(f);
	f->dma[slot].pa = candidate;
	f->dma[slot].bytes = need;
	f->dma[slot].flags = flags;
	*handle = f->dma[slot].handle;
	*pa = f->dma[slot].pa;
	f->stats.dma_allocs++;
	recf(f, "DMA alloc handle=%u pa=0x%08x bytes=%u flags=0x%x", *handle, *pa, need, flags);
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static int fake_dma_free(void *ctx, uint32_t handle)
{
	struct n2vc_hw_fake *f = ctx;
	struct fake_dma *d;

	if (!f)
		return -EINVAL;
	pthread_mutex_lock(&f->lock);
	d = find_dma(f, handle);
	if (!d) {
		pthread_mutex_unlock(&f->lock);
		return -EINVAL;
	}
	free(d->host);
	memset(d, 0, sizeof *d);
	f->stats.dma_frees++;
	recf(f, "DMA free handle=%u", handle);
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static int fake_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct n2vc_hw_fake *f = ctx;
	struct fake_dma *d;

	if (!f || !host)
		return -EINVAL;
	pthread_mutex_lock(&f->lock);
	d = find_dma(f, handle);
	if (!d || offset > d->bytes || d->bytes - offset < bytes) {
		pthread_mutex_unlock(&f->lock);
		return -EINVAL;
	}
	if (write)
		memcpy(d->host + offset, host, bytes);
	else
		memcpy(host, d->host + offset, bytes);
	f->stats.dma_rw++;
	recf(f, "DMA rw handle=%u off=0x%04x bytes=%u %c", handle, offset, bytes, write ? 'W' : 'R');
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static void fake_close(void *ctx)
{
	struct n2vc_hw_fake *f = ctx;
	unsigned i;

	if (!f)
		return;
	for (i = 0; i < N2VC_FAKE_MAX_DMA; i++)
		if (f->dma[i].used)
			free(f->dma[i].host);
	if (f->rec)
		fclose(f->rec);
	free(f->vdec_all);
	pthread_mutex_destroy(&f->lock);
	free(f);
}

/* ---- script parser ---------------------------------------------------- */

static int ieq(const char *a, const char *b)
{
	return strcasecmp(a, b) == 0;
}

static int parse_u32(const char *s, uint32_t *out)
{
	char *end = NULL;
	unsigned long v;

	if (!s || !*s)
		return -EINVAL;
	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno || !end || *end != '\0' || v > 0xfffffffful)
		return -EINVAL;
	*out = (uint32_t)v;
	return 0;
}

static int parse_win(const char *s, unsigned *win)
{
	unsigned i;
	uint32_t v;

	for (i = 0; i < RWVC_WIN_COUNT; i++)
		if (ieq(s, WINS[i].name)) {
			*win = i;
			return 0;
		}
	if (parse_u32(s, &v) == 0 && v < RWVC_WIN_COUNT) {
		*win = v;
		return 0;
	}
	return -EINVAL;
}

static int parse_val_mask(char *s, uint32_t *value, uint32_t *mask)
{
	char *amp = strchr(s, '&');

	*mask = 0xffffffffu;
	if (amp) {
		*amp = '\0';
		if (parse_u32(amp + 1, mask) != 0)
			return -EINVAL;
	}
	return parse_u32(s, value);
}

static int parse_script(FILE *fp, struct fake_rule *rules, unsigned *nrules, char *err, size_t errlen)
{
	char buf[N2VC_FAKE_LINE];
	unsigned line = 0, n = 0;

	while (fgets(buf, sizeof buf, fp)) {
		char *tok[16], *save = NULL, *hash, *p;
		unsigned nt = 0;
		struct fake_rule r;

		line++;
		if (!strchr(buf, '\n') && !feof(fp)) {
			snprintf(err, errlen, "line %u: longer than %u bytes", line, N2VC_FAKE_LINE - 1);
			return -EINVAL;
		}
		hash = strchr(buf, '#');
		if (hash)
			*hash = '\0';
		for (p = buf; *p; p++)
			if (*p == '\r' || *p == '\n') {
				*p = '\0';
				break;
			}
		for (p = strtok_r(buf, " \t", &save); p && nt < 16; p = strtok_r(NULL, " \t", &save))
			tok[nt++] = p;
		if (nt == 0)
			continue;
		if (n >= N2VC_FAKE_MAX_RULES) {
			snprintf(err, errlen, "line %u: more than %u rules", line, N2VC_FAKE_MAX_RULES);
			return -EINVAL;
		}
		memset(&r, 0, sizeof r);
		r.mask = 0xffffffffu;
		if (ieq(tok[0], "after")) {
			if (nt < 10 || !ieq(tok[1], "write") || strcmp(tok[5], "->") != 0 ||
			    !ieq(tok[6], "set")) {
				snprintf(err, errlen, "line %u: after write WIN OFF VALUE&MASK -> set WIN OFF VALUE", line);
				return -EINVAL;
			}
			r.kind = FAKE_SET;
			if (parse_win(tok[2], &r.win) || parse_u32(tok[3], &r.off) ||
			    parse_val_mask(tok[4], &r.value, &r.mask) ||
			    parse_win(tok[7], &r.awin) || parse_u32(tok[8], &r.aoff) ||
			    parse_u32(tok[9], &r.aval)) {
				snprintf(err, errlen, "line %u: bad number or window", line);
				return -EINVAL;
			}
			if (check_reg(r.win, r.off) || check_reg(r.awin, r.aoff)) {
				snprintf(err, errlen, "line %u: offset outside window or not 4-aligned", line);
				return -EINVAL;
			}
		} else if (ieq(tok[0], "irq")) {
			if (nt == 4 && ieq(tok[2], "status")) {
				if (parse_u32(tok[1], &r.irq) || parse_u32(tok[3], &r.value) ||
				    r.irq >= RWVC_IRQ_COUNT) {
					snprintf(err, errlen, "line %u: irq N status VALUE", line);
					return -EINVAL;
				}
				r.kind = FAKE_IRQ;
				r.status_only = 1;
				r.win = RWVC_WIN_COUNT;
			} else {
				if (nt < 9 || !ieq(tok[2], "fires") || !ieq(tok[4], "ms") ||
				    !ieq(tok[5], "after") || !ieq(tok[6], "write")) {
					snprintf(err, errlen, "line %u: irq N fires K ms after write WIN OFF [VALUE[&MASK]]", line);
					return -EINVAL;
				}
				r.kind = FAKE_IRQ;
				if (parse_u32(tok[1], &r.irq) || r.irq >= RWVC_IRQ_COUNT ||
				    parse_u32(tok[3], &r.delay_ms) ||
				    parse_win(tok[7], &r.win) || parse_u32(tok[8], &r.off)) {
					snprintf(err, errlen, "line %u: bad irq rule", line);
					return -EINVAL;
				}
				if (nt >= 10 && parse_val_mask(tok[9], &r.value, &r.mask)) {
					snprintf(err, errlen, "line %u: bad value/mask", line);
					return -EINVAL;
				}
				if (nt < 10)
					r.value = 0, r.mask = 0;   /* any write */
				if (check_reg(r.win, r.off)) {
					snprintf(err, errlen, "line %u: offset outside window or not 4-aligned", line);
					return -EINVAL;
				}
			}
		} else if (ieq(tok[0], "set")) {
			if (nt < 4) {
				snprintf(err, errlen, "line %u: set WIN OFF VALUE", line);
				return -EINVAL;
			}
			r.kind = FAKE_DEFAULT;
			if (parse_win(tok[1], &r.win) || parse_u32(tok[2], &r.off) ||
			    parse_u32(tok[3], &r.value)) {
				snprintf(err, errlen, "line %u: bad set rule", line);
				return -EINVAL;
			}
			if (check_reg(r.win, r.off)) {
				snprintf(err, errlen, "line %u: offset outside window or not 4-aligned", line);
				return -EINVAL;
			}
		} else if (ieq(tok[0], "read")) {
			if (nt < 5 || !ieq(tok[3], "returns")) {
				snprintf(err, errlen, "line %u: read WIN OFF returns VALUE [once]", line);
				return -EINVAL;
			}
			r.kind = FAKE_READ;
			if (parse_win(tok[1], &r.win) || parse_u32(tok[2], &r.off) ||
			    parse_u32(tok[4], &r.value)) {
				snprintf(err, errlen, "line %u: bad read rule", line);
				return -EINVAL;
			}
			if (check_reg(r.win, r.off)) {
				snprintf(err, errlen, "line %u: offset outside window or not 4-aligned", line);
				return -EINVAL;
			}
			if (nt >= 6 && ieq(tok[5], "once"))
				r.once = 1;
			else if (nt >= 6) {
				snprintf(err, errlen, "line %u: expected once", line);
				return -EINVAL;
			}
		} else {
			snprintf(err, errlen, "line %u: unknown rule", line);
			return -EINVAL;
		}
		rules[n++] = r;
	}
	if (ferror(fp)) {
		snprintf(err, errlen, "read error");
		return -EINVAL;
	}
	*nrules = n;
	return 0;
}

int n2vc_hw_fake_script(struct n2vc_hw *hw, const char *path)
{
	struct n2vc_hw_fake *f = fake_of(hw);
	struct fake_rule *rules;
	unsigned nrules = 0;
	char err[192];
	FILE *fp;

	if (!f || !path || !*path)
		return -EINVAL;
	fp = fopen(path, "r");
	if (!fp)
		return -errno;
	rules = calloc(N2VC_FAKE_MAX_RULES, sizeof *rules);
	if (!rules) {
		fclose(fp);
		return -ENOMEM;
	}
	err[0] = '\0';
	if (parse_script(fp, rules, &nrules, err, sizeof err) != 0) {
		fclose(fp);
		pthread_mutex_lock(&f->lock);
		snprintf(f->err, sizeof f->err, "%s", err);
		recf(f, "SCRIPT error %s", f->err);
		pthread_mutex_unlock(&f->lock);
		free(rules);
		return -EINVAL;
	}
	fclose(fp);
	pthread_mutex_lock(&f->lock);
	memcpy(f->rules, rules, nrules * sizeof *rules);
	f->nrules = nrules;
	/* Load-time defaults are initial register values, not register
	 * accesses: they do not require the block to be powered. */
	{
		unsigned i;
		for (i = 0; i < nrules; i++)
			if (f->rules[i].kind == FAKE_DEFAULT) {
				write_word(f, f->rules[i].win, f->rules[i].off, f->rules[i].value);
				recf(f, "SCRIPT default %s 0x%04x = 0x%08x",
				     WINS[f->rules[i].win].name, f->rules[i].off, f->rules[i].value);
			}
	}
	/* status-only rules set the wait status immediately; a new script
	 * starts from the default status 1. */
	{
		unsigned i;
		for (i = 0; i < RWVC_IRQ_COUNT; i++) {
			f->irq_status[i] = 1u;
			f->irq_pending[i] = 0;
			f->irq_ndue[i] = 0;
		}
		for (i = 0; i < nrules; i++)
			if (f->rules[i].kind == FAKE_IRQ && f->rules[i].status_only &&
			    f->rules[i].irq < RWVC_IRQ_COUNT)
				f->irq_status[f->rules[i].irq] = f->rules[i].value;
	}
	f->err[0] = '\0';
	recf(f, "SCRIPT loaded %u rules from %s", nrules, path);
	pthread_mutex_unlock(&f->lock);
	free(rules);
	return 0;
}

unsigned n2vc_hw_fake_violations(const struct n2vc_hw *hw)
{
	const struct n2vc_hw_fake *f = hw ? (const struct n2vc_hw_fake *)hw->ctx : NULL;
	return f ? (unsigned)f->stats.violations : 0;
}

void n2vc_hw_fake_stats(const struct n2vc_hw *hw, struct n2vc_hw_fake_stats *out)
{
	const struct n2vc_hw_fake *f = hw ? (const struct n2vc_hw_fake *)hw->ctx : NULL;

	if (!out)
		return;
	if (!f) {
		memset(out, 0, sizeof *out);
		return;
	}
	*out = f->stats;
}

int n2vc_hw_fake_set_poll_timeout(struct n2vc_hw *hw, uint32_t us)
{
	struct n2vc_hw_fake *f = fake_of(hw);

	if (!f || us > N2VC_FAKE_POLL_MAX)
		return -EINVAL;
	pthread_mutex_lock(&f->lock);
	f->poll_timeout_us = us;
	pthread_mutex_unlock(&f->lock);
	return 0;
}

int n2vc_hw_fake_dma_host(struct n2vc_hw *hw, uint32_t handle, void **host, uint32_t *bytes)
{
	struct n2vc_hw_fake *f = fake_of(hw);
	struct fake_dma *d;

	if (!f || !host || !bytes)
		return -EINVAL;
	pthread_mutex_lock(&f->lock);
	d = find_dma(f, handle);
	if (!d) {
		pthread_mutex_unlock(&f->lock);
		return -EINVAL;
	}
	*host = d->host;
	*bytes = d->bytes;
	pthread_mutex_unlock(&f->lock);
	return 0;
}

const char *n2vc_hw_fake_last_error(const struct n2vc_hw *hw)
{
	const struct n2vc_hw_fake *f = hw ? (const struct n2vc_hw_fake *)hw->ctx : NULL;
	return f ? f->err : "no fake instance";
}

int n2vc_hw_open_fake(struct n2vc_hw *out, const char *record_path)
{
	struct n2vc_hw_fake *f;
	const char *env;
	unsigned i;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof *out);
	f = calloc(1, sizeof *f);
	if (!f)
		return -ENOMEM;
	f->vdec_all = calloc(1, N2VC_FAKE_ALL_BYTES);
	if (!f->vdec_all) {
		free(f);
		return -ENOMEM;
	}
	if (pthread_mutex_init(&f->lock, NULL) != 0) {
		free(f->vdec_all);
		free(f);
		return -ENOMEM;
	}
	f->poll_timeout_us = 100000u;
	f->next_handle = 1;
	{
		/* opus-decint: N2VC_FAKE_HANDLE_BASE moves the fake's DMA handles away from a host client's
		 * "<socket>.buf/<n>" files (vcodecd --dma-dir resolves a handle to a file first).  On the phone
		 * the kernel hands daemon and client handles out of one table, so they never collide. */
		const char *hb = getenv("N2VC_FAKE_HANDLE_BASE");
		if (hb && *hb) {
			unsigned long v = strtoul(hb, NULL, 0);
			if (v > 0 && v < 0x80000000ul)
				f->next_handle = (uint32_t)v;
		}
	}
	for (i = 0; i < RWVC_IRQ_COUNT; i++)
		f->irq_status[i] = 1u;
	if (record_path) {
		f->rec = fopen(record_path, "w");
		if (!f->rec) {
			int e = -errno;
			pthread_mutex_destroy(&f->lock);
			free(f);
			return e;
		}
	}
	out->ctx = f;
	out->power = fake_power;
	out->regs = fake_regs;
	out->wait_irq = fake_wait_irq;
	out->irq_clear = fake_irq_clear;
	out->dma_alloc = fake_dma_alloc;
	out->dma_free = fake_dma_free;
	out->dma_rw = fake_dma_rw;
	out->close = fake_close;
	env = getenv("N2VC_FAKE_SCRIPT");
	if (env && *env) {
		int rc = n2vc_hw_fake_script(out, env);
		if (rc != 0) {
			fake_close(f);
			memset(out, 0, sizeof *out);
			return rc;
		}
	}
	return 0;
}
