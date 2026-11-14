/* SPDX-License-Identifier: MIT */
/*
 * t_dec04.c -- host tests for DEC04: the rev-3 whole-VDEC register window and guest-store
 * write-combining in gpu/hwcodec/vcu/n2vcu.c.
 *
 * Oracle (TASK.md):
 *   - the same register record, in order and with the same values, with and without combining,
 *     on the DEC02 run; the number of back-end batches (one RWVC_REGS ioctl each on the phone)
 *     drops.
 *
 * How the oracle is built:
 *   - the back end is a recording n2vc_hw in this file: every rwvc_reg_op it receives is stored
 *     with the call index (so "batches" are counted, not estimated), the register file executes
 *     the ops, and power/IRQ events are stored with the op count at that moment (ordering).
 *   - the DEC02 run is replayed exactly: dec02_au1.inc holds AU 1 of DEC02's measured 320x240
 *     12-AU session (facts/vdec_trace/PAGES.md; 1574 register ops, 1032 reads, 542 writes) in
 *     the recorded order, absolute VDEC offsets and measured write values.  It is regenerated
 *     with tools/extract_dec02_trace.sh, which runs DEC02's own stage.py + n2tr recorder.
 *   - with combining OFF an aligned store is a read-modify-write pair (READ then WRITE);
 *     with it ON the store is queued and travels in one RWVC_REGS batch with the read that
 *     follows it.  The oracle therefore compares the *write* subsequence (op, window, offset,
 *     value, mask), the final register image of every window, and the values the guest itself
 *     read back -- not the internal RMW reads, which combining elides.
 *
 * Everything is a host test.  Nothing here touches the phone.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2vcu.h"
#include "mtk_vcu_abi.h"

/* The patch uses the real ABI name, so this test needs redwood_vcodec.h rev 3.  run_tests.sh
 * stages the proposed rev-3 line into a /tmp copy when the tree header is still rev 2; if that
 * did not happen, fail at compile time with a clear message rather than testing a wrong index. */
typedef char dec04_needs_header_rev3[
	(RWVC_WIN_VDEC_ALL == 12 && RWVC_WIN_COUNT == 13) ? 1 : -1];

/* ------------------------------------------------------------------ scoreboard */

#define DEC04_AU1_LARB_STORES 4u   /* opus-decint: larb5/larb4 SLP_CON request + release in the measured AU */

static unsigned g_checks, g_fails;

static void ck(const char *what, int cond)
{
	g_checks++;
	if (!cond) {
		g_fails++;
		printf("FAIL %s\n", what);
	}
}

static void ck_u64(const char *what, uint64_t got, uint64_t want)
{
	g_checks++;
	if (got != want) {
		g_fails++;
		printf("FAIL %s: got 0x%llx want 0x%llx\n", what,
		       (unsigned long long)got, (unsigned long long)want);
	}
}

static void ck_i64(const char *what, int64_t got, int64_t want)
{
	g_checks++;
	if (got != want) {
		g_fails++;
		printf("FAIL %s: got %lld want %lld\n", what, (long long)got, (long long)want);
	}
}

/* ------------------------------------------------------------------ interpreter stand-ins
 *
 * The test links V02's real guest memory manager but not the instruction core.  Two core entry
 * points are needed by n2vcu.c; they are the same trivial definitions test_v40 uses.
 */
void n2a32_cpu_init(struct n2a32_cpu *c, struct n2a32_mem *m)
{
	if (!c)
		return;
	memset(c, 0, sizeof(*c));
	c->mem = m;
	c->stop = N2A32_STOP_NONE;
}

void n2a32_trace_mmio(struct n2a32_mem *m, uint32_t addr, unsigned bytes, uint32_t value, int write)
{
	(void)m;
	(void)addr;
	(void)bytes;
	(void)value;
	(void)write;
}

/* ------------------------------------------------------------------ guest helpers */

#define GB 0x40000000u
#define CMDS_G (GB + 0x2000u)   /* struct gce_cmds (0xe808 bytes) */
#define QOBJ_G (GB + 0x11000u)  /* struct gce_cmdq_obj */
#define MAP_G (GB + 0x11800u)   /* struct map_obj for SET_MMAP_TYPE */

static void put32(struct n2a32_cpu *c, uint32_t a, uint32_t v) { n2a32_st32(c, a, v); }
static uint32_t get32(struct n2a32_cpu *c, uint32_t a) { return n2a32_ld32(c, a); }
static void put64(struct n2a32_cpu *c, uint32_t a, uint64_t v) { n2a32_st64(c, a, v); }

static void cmd_set(struct n2a32_cpu *c, unsigned i, unsigned cmd, uint64_t addr, uint64_t data,
		    uint32_t mask)
{
	n2a32_st8(c, CMDS_G + i, (uint8_t)cmd);
	put64(c, CMDS_G + 0x800u + 8u * i, addr);
	put64(c, CMDS_G + 0x4800u + 8u * i, data);
	put32(c, CMDS_G + 0x8800u + 4u * i, mask);
}

static void cmd_count(struct n2a32_cpu *c, unsigned n) { put32(c, CMDS_G + 0xe800u, n); }

/* ------------------------------------------------------------------ recording back end */

#define REC_MAX_OPS 65536u

struct rop {
	uint8_t op, win;
	uint32_t off, value, mask;
	unsigned call;
};

struct rev {
	int kind;           /* 1 power, 2 wait_irq */
	unsigned a;         /* block / irq */
	int on;             /* power only */
	unsigned nops;      /* recorded ops at the moment of the event */
};

struct rec_dma {
	int used;
	uint32_t handle, pa, bytes;
	uint8_t *host;
};

struct rec {
	struct n2vc_hw hw;
	uint32_t *wmem[RWVC_WIN_COUNT];
	uint32_t wbytes[RWVC_WIN_COUNT];
	struct rop *ops;
	unsigned nops, maxops;
	unsigned calls;     /* hw->regs() invocations: one RWVC_REGS ioctl each */
	struct rev ev[256];
	unsigned nev;
	int powered[RWVC_BLK_COUNT];
	struct rec_dma dma[64];
	uint32_t next_handle, next_pa;
	uint32_t irq_status[RWVC_IRQ_COUNT];
	unsigned waits[RWVC_IRQ_COUNT];
};

static const uint32_t rec_win_bytes[] = {
	0x2000, 0x2000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x4000,
	0x1000, 0x0400, 0x1000, 0x1000, 0x30000,
};

static void rec_event(struct rec *r, int kind, unsigned a, int on)
{
	if (r->nev < sizeof(r->ev) / sizeof(r->ev[0])) {
		struct rev *e = &r->ev[r->nev++];

		e->kind = kind;
		e->a = a;
		e->on = on;
		e->nops = r->nops;
	}
}

static int rec_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct rec *r = (struct rec *)ctx;
	unsigned i, call;

	call = ++r->calls;
	for (i = 0; i < count; i++) {
		struct rwvc_reg_op *o = &ops[i];
		struct rop *e;
		uint32_t v;

		if (o->window >= RWVC_WIN_COUNT || o->offset > r->wbytes[o->window] ||
		    r->wbytes[o->window] - o->offset < 4u) {
			*done = i;
			return -EINVAL;
		}
		if (r->nops >= r->maxops) {
			*done = i;
			return -ENOSPC;
		}
		e = &r->ops[r->nops++];
		e->op = o->op;
		e->win = o->window;
		e->off = o->offset;
		e->value = o->value;
		e->mask = o->mask;
		e->call = call;

		switch (o->op) {
		case RWVC_OP_READ:
			v = r->wmem[o->window][o->offset / 4u];
			o->value = v;
			e->value = v;
			break;
		case RWVC_OP_WRITE:
			r->wmem[o->window][o->offset / 4u] = o->value;
			/* The larb5 sleep-protect handshake the vendor uses (opus-dec1's frame trace):
			 * writing bit 0 makes the hardware raise the ack in bit 16, which the library
			 * then polls.  A fake register that did not do this would time out the poll. */
			if (o->window == RWVC_WIN_VDEC_ALL &&
			    (o->offset == 0x0d00cu || o->offset == 0x2e00cu) &&
			    (o->value & 1u))
				r->wmem[o->window][o->offset / 4u] |= 0x00010000u;
			break;
		case RWVC_OP_WRITE_MASK:
			v = r->wmem[o->window][o->offset / 4u];
			v = (v & ~o->mask) | (o->value & o->mask);
			/* opus-decint: the vcu sends larb SLP_CON stores as bit-0 masked writes; same ack model */
			if (o->window == RWVC_WIN_VDEC_ALL &&
			    (o->offset == 0x0d00cu || o->offset == 0x2e00cu))
				v = (v & ~0x00010000u) | ((v & 1u) << 16);
			r->wmem[o->window][o->offset / 4u] = v;
			break;
		case RWVC_OP_POLL: {
			uint32_t n;

			for (n = 0; n < 10000; n++) {
				v = r->wmem[o->window][o->offset / 4u];
				if ((v & o->mask) == (o->value & o->mask))
					break;
				{
					struct timespec ts = {0, 100000};

					nanosleep(&ts, NULL);
				}
			}
			if (n == 10000) {
				*done = i + 1;
				return -ETIMEDOUT;
			}
			break;
		}
		case RWVC_OP_DELAY_US: {
			unsigned us = o->value > 1000u ? 1000u : o->value;
			struct timespec ts;

			ts.tv_sec = 0;
			ts.tv_nsec = (long)us * 1000L;
			nanosleep(&ts, NULL);
			break;
		}
		default:
			*done = i + 1;
			return -EINVAL;
		}
	}
	*done = count;
	return 0;
}

static int rec_power(void *ctx, unsigned block, int on)
{
	struct rec *r = (struct rec *)ctx;

	if (block >= RWVC_BLK_COUNT)
		return -EINVAL;
	r->powered[block] = on;
	rec_event(r, 1, block, on);
	return 0;
}

static int rec_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct rec *r = (struct rec *)ctx;

	(void)timeout_ms;
	if (irq >= RWVC_IRQ_COUNT)
		return -EINVAL;
	r->waits[irq]++;
	rec_event(r, 2, irq, 0);
	if (status)
		*status = r->irq_status[irq];
	return 0;
}

static int rec_irq_clear(void *ctx, unsigned irq)
{
	struct rec *r = (struct rec *)ctx;

	if (irq >= RWVC_IRQ_COUNT)
		return -EINVAL;
	r->irq_status[irq] = 0;
	return 0;
}

static int rec_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct rec *r = (struct rec *)ctx;
	unsigned i;

	(void)flags;
	if (!bytes)
		return -EINVAL;
	for (i = 0; i < sizeof(r->dma) / sizeof(r->dma[0]); i++) {
		if (!r->dma[i].used) {
			struct rec_dma *d = &r->dma[i];
			uint32_t len = (bytes + 0xfffu) & ~0xfffu;

			d->host = (uint8_t *)calloc(1, len);
			if (!d->host)
				return -ENOMEM;
			d->used = 1;
			d->bytes = len;
			d->handle = ++r->next_handle;
			d->pa = 0x20000000u + (r->next_pa << 12);
			r->next_pa += len >> 12;
			*handle = d->handle;
			*pa = d->pa;
			return 0;
		}
	}
	return -ENOMEM;
}

static int rec_dma_free(void *ctx, uint32_t handle)
{
	struct rec *r = (struct rec *)ctx;
	unsigned i;

	for (i = 0; i < sizeof(r->dma) / sizeof(r->dma[0]); i++) {
		if (r->dma[i].used && r->dma[i].handle == handle) {
			free(r->dma[i].host);
			memset(&r->dma[i], 0, sizeof(r->dma[i]));
			return 0;
		}
	}
	return -EINVAL;
}

static int rec_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes,
		      int write)
{
	struct rec *r = (struct rec *)ctx;
	unsigned i;

	for (i = 0; i < sizeof(r->dma) / sizeof(r->dma[0]); i++) {
		struct rec_dma *d = &r->dma[i];

		if (!d->used || d->handle != handle)
			continue;
		if (offset > d->bytes || bytes > d->bytes - offset)
			return -EINVAL;
		if (write)
			memcpy(d->host + offset, host, bytes);
		else
			memcpy(host, d->host + offset, bytes);
		return 0;
	}
	return -EINVAL;
}

static void rec_close(void *ctx) { (void)ctx; }

static int rec_init(struct rec *r)
{
	unsigned i;

	memset(r, 0, sizeof(*r));
	r->ops = (struct rop *)calloc(REC_MAX_OPS, sizeof(*r->ops));
	if (!r->ops)
		return -1;
	r->maxops = REC_MAX_OPS;
	for (i = 0; i < RWVC_WIN_COUNT; i++) {
		uint32_t bytes = i < sizeof(rec_win_bytes) / sizeof(rec_win_bytes[0])
			? rec_win_bytes[i] : 0x10000u;

		r->wbytes[i] = bytes;
		r->wmem[i] = (uint32_t *)calloc(1, bytes);
		if (!r->wmem[i])
			return -1;
	}
	r->hw.ctx = r;
	r->hw.power = rec_power;
	r->hw.regs = rec_regs;
	r->hw.wait_irq = rec_wait_irq;
	r->hw.irq_clear = rec_irq_clear;
	r->hw.dma_alloc = rec_dma_alloc;
	r->hw.dma_free = rec_dma_free;
	r->hw.dma_rw = rec_dma_rw;
	r->hw.close = rec_close;
	return 0;
}

static void rec_free(struct rec *r)
{
	unsigned i;

	for (i = 0; i < RWVC_WIN_COUNT; i++)
		free(r->wmem[i]);
	for (i = 0; i < sizeof(r->dma) / sizeof(r->dma[0]); i++)
		free(r->dma[i].host);
	free(r->ops);
	memset(r, 0, sizeof(*r));
}

/* The n2vc_dma_* registry is V41's ap/n2vc_dma.c.  These tests never allocate a DMA buffer
 * through the model, so a failing stand-in is the honest minimum (the declarations must exist to
 * link n2vcu.c). */
int n2vc_dma_guest_map(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle,
		       uint32_t bytes, uint32_t *guest_addr)
{
	(void)h; (void)m; (void)handle; (void)bytes; (void)guest_addr;
	return -1;
}

int n2vc_dma_guest_unmap(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr)
{
	(void)h; (void)m; (void)guest_addr;
	return -1;
}

int n2vc_dma_sync(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr,
		  uint32_t offset, uint32_t bytes, int to_device)
{
	(void)h; (void)m; (void)guest_addr; (void)offset; (void)bytes; (void)to_device;
	return -1;
}

int n2vc_dma_lookup_pa(uint32_t pa, uint32_t *handle, uint32_t *guest_addr, uint32_t *bytes)
{
	(void)pa; (void)handle; (void)guest_addr; (void)bytes;
	return -1;
}

/* ------------------------------------------------------------------ environment */

struct env {
	struct n2a32_mem *mem;
	struct n2a32_cpu cpu;
	struct rec rec;
	struct n2vcu *vcu;
	const struct n2vfs_dev *dev;
	int opened;
	uint32_t vdec;   /* guest base of the 0x16000000+0x40000 mapping */
	uint32_t venc;   /* guest base of the 0x17020000+0x10000 mapping */
};

static uint32_t dev_mmap(struct env *e, uint32_t len, uint64_t offset)
{
	int err = -999;
	uint32_t g = e->dev->mmap(e->dev->ctx, &e->cpu, len, 3, offset, &err);

	return err == 0 ? g : 0;
}

static int dev_ioctl(struct env *e, uint32_t request, uint32_t arg)
{
	return e->dev->ioctl(e->dev->ctx, &e->cpu, request, arg);
}

static struct n2vcu_stats stats_of(struct env *e)
{
	struct n2vcu_stats s;

	n2vcu_stats(e->vcu, &s);
	return s;
}

static int env_init(struct env *e, int combine)
{
	memset(e, 0, sizeof(*e));
	e->mem = n2a32_mem_create();
	if (!e->mem)
		return -1;
	if (n2a32_mem_map(e->mem, GB, 0x20000, 1) != 0)
		return -1;
	n2a32_cpu_init(&e->cpu, e->mem);
	if (rec_init(&e->rec) != 0)
		return -1;
	e->vcu = n2vcu_create(e->mem, &e->rec.hw);
	if (!e->vcu)
		return -1;
	e->dev = n2vcu_dev(e->vcu);
	if (e->dev->open(e->dev->ctx, 2) != 0)
		return -1;
	e->opened = 1;
	e->vdec = dev_mmap(e, 0x40000, 0x16000000u);
	e->venc = dev_mmap(e, 0x10000, 0x17020000u);
	if (!e->vdec || !e->venc)
		return -1;
	if (combine)
		n2vcu_set_write_combining(e->vcu, 1);
	return 0;
}

static void env_fini(struct env *e)
{
	if (e->dev && e->opened)
		e->dev->close(e->dev->ctx);
	e->opened = 0;
	if (e->vcu)
		n2vcu_destroy(e->vcu);
	e->vcu = NULL;
	if (e->mem)
		n2a32_mem_destroy(e->mem);
	e->mem = NULL;
	rec_free(&e->rec);
}

static int gce_flush(struct env *e, uint64_t handle, uint32_t codec, uint32_t core)
{
	put64(&e->cpu, QOBJ_G, CMDS_G);
	put64(&e->cpu, QOBJ_G + 8u, handle);
	put32(&e->cpu, QOBJ_G + 0x10u, 0);
	put32(&e->cpu, QOBJ_G + 0x14u, codec);
	put32(&e->cpu, QOBJ_G + 0x18u, core);
	put32(&e->cpu, QOBJ_G + 0x1cu, 0);
	return dev_ioctl(e, MTK_VCU_GCE_SET_CMD_FLUSH, QOBJ_G);
}

static unsigned rec_writes(const struct rec *r)
{
	unsigned i, n = 0;

	for (i = 0; i < r->nops; i++)
		if (r->ops[i].op == RWVC_OP_WRITE || r->ops[i].op == RWVC_OP_WRITE_MASK)
			n++;
	return n;
}

static uint32_t rec_word(const struct rec *r, unsigned win, uint32_t off)
{
	return r->wmem[win][off / 4u];
}

/* ------------------------------------------------------------------ window tests */

static void t_windows(void)
{
	struct env e;
	struct n2vcu_stats s0;
	struct {
		uint32_t off;
		unsigned win;
		uint32_t woff;
	} tab[] = {
		{ 0x00000u, RWVC_WIN_VDEC_BASE, 0x00000u },
		{ 0x00400u, RWVC_WIN_VDEC_BASE, 0x00400u },
		{ 0x04000u, RWVC_WIN_VDEC_RACING, 0x00000u },
		{ 0x04400u, RWVC_WIN_VDEC_RACING, 0x00400u },
		{ 0x0f000u, RWVC_WIN_VDEC_SOC_GCON, 0x00000u },
		{ 0x0f800u, RWVC_WIN_VDEC_SOC_GCON, 0x00800u },
		{ 0x10000u, RWVC_WIN_VDEC_LAT_MISC, 0x00000u },
		{ 0x10800u, RWVC_WIN_VDEC_LAT_MISC, 0x00800u },
		{ 0x11000u, RWVC_WIN_VDEC_LAT_VLD, 0x00000u },
		{ 0x113fcu, RWVC_WIN_VDEC_LAT_VLD, 0x003fcu },
		{ 0x20000u, RWVC_WIN_VDEC_VLD, 0x00000u },
		{ 0x20800u, RWVC_WIN_VDEC_VLD, 0x00800u },
		{ 0x21000u, RWVC_WIN_VDEC_MC, 0x00000u },
		{ 0x23000u, RWVC_WIN_VDEC_MV, 0x00000u },
		{ 0x25000u, RWVC_WIN_VDEC_MISC, 0x00000u },
		{ 0x28800u, RWVC_WIN_VDEC_MISC, 0x03800u },
		{ 0x2f000u, RWVC_WIN_VDEC_SYS, 0x00000u },
		{ 0x2f800u, RWVC_WIN_VDEC_SYS, 0x00800u },
		/* the rev-3 gap pages: only these reach RWVC_WIN_VDEC_ALL */
		{ 0x01000u, RWVC_WIN_VDEC_ALL, 0x01000u },
		{ 0x02000u, RWVC_WIN_VDEC_ALL, 0x02000u },
		{ 0x03800u, RWVC_WIN_VDEC_ALL, 0x03800u },
		{ 0x0d000u, RWVC_WIN_VDEC_ALL, 0x0d000u },   /* larb5 */
		{ 0x0e000u, RWVC_WIN_VDEC_ALL, 0x0e000u },
		{ 0x11400u, RWVC_WIN_VDEC_ALL, 0x11400u },   /* tail of the LAT_VLD page */
		{ 0x11800u, RWVC_WIN_VDEC_ALL, 0x11800u },
		{ 0x12000u, RWVC_WIN_VDEC_ALL, 0x12000u },
		{ 0x13000u, RWVC_WIN_VDEC_ALL, 0x13000u },
		{ 0x14800u, RWVC_WIN_VDEC_ALL, 0x14800u },
		{ 0x17ffcu, RWVC_WIN_VDEC_ALL, 0x17ffcu },
		{ 0x18800u, RWVC_WIN_VDEC_ALL, 0x18800u },
		{ 0x22000u, RWVC_WIN_VDEC_ALL, 0x22000u },   /* AVC_VLD */
		{ 0x24000u, RWVC_WIN_VDEC_ALL, 0x24000u },   /* PP */
		{ 0x29000u, RWVC_WIN_VDEC_ALL, 0x29000u },
		{ 0x2e000u, RWVC_WIN_VDEC_ALL, 0x2e000u },   /* larb4 */
		{ 0x2ff04u, RWVC_WIN_VDEC_SYS, 0x00f04u },   /* tail of the old VDEC_SYS window */
	};
	unsigned i;

	ck("env_init windows", env_init(&e, 0) == 0);
	s0 = stats_of(&e);
	for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
		unsigned before = e.rec.nops;
		uint32_t v = 0xa5000000u | i;

		put32(&e.cpu, e.vdec + tab[i].off, v);
		/* combining is off here: an aligned VDEC store is one write (opus-decint: no RMW read) */
		if (e.rec.nops != before + 1u) {
			printf("FAIL window 0x%x produced %u ops\n", tab[i].off, e.rec.nops - before);
			g_fails++;
		}
		g_checks++;
		if (e.rec.ops[e.rec.nops - 1u].win != tab[i].win ||
		    e.rec.ops[e.rec.nops - 1u].off != tab[i].woff ||
		    e.rec.ops[e.rec.nops - 1u].value != v) {
			printf("FAIL window 0x%x -> win %u off 0x%x (want %u/0x%x)\n", tab[i].off,
			       e.rec.ops[e.rec.nops - 1u].win, e.rec.ops[e.rec.nops - 1u].off,
			       tab[i].win, tab[i].woff);
			g_fails++;
		}
	}
	ck_u64("no bad MMIO on the DEC02 page list", stats_of(&e).mmio_bad, s0.mmio_bad);

	/* outside the last window: still rejected and counted, with the physical address */
	{
		unsigned before = stats_of(&e).mmio_bad;

		put32(&e.cpu, e.vdec + 0x30000u, 1);
		ck_u64("0x16030000 is outside VDEC_ALL", stats_of(&e).mmio_bad, before + 1u);
		ck_u64("last bad MMIO address", stats_of(&e).last_bad_mmio_addr, 0x16030000u);
	}
	put32(&e.cpu, e.vdec + 0x3fffcu, 2);
	ck_u64("0x1603fffc is outside VDEC_ALL", stats_of(&e).mmio_bad, s0.mmio_bad + 2u);

	/* VENC/VENC_C1 keep their own windows and indices */
	put32(&e.cpu, e.venc + 0x10u, 0x55u);
	ck_u64("VENC window unchanged", e.rec.ops[e.rec.nops - 1u].win, RWVC_WIN_VENC);
	ck_u64("VENC offset", e.rec.ops[e.rec.nops - 1u].off, 0x10u);

	env_fini(&e);
}

static void t_gce_gap_pages(void)
{
	struct env e;
	struct n2vcu_stats s0;

	ck("env_init gce gaps", env_init(&e, 0) == 0);
	s0 = stats_of(&e);

	cmd_set(&e.cpu, 0, CMD_WRITE, 0x16022000u, 0x1234u, 0xffffffffu);   /* AVC_VLD */
	cmd_set(&e.cpu, 1, CMD_WRITE, 0x16024000u, 0x5678u, 0xffffffffu);   /* PP */
	cmd_set(&e.cpu, 2, CMD_WRITE, 0x1600d000u, 0x9abcu, 0xffffffffu);   /* larb5 */
	cmd_set(&e.cpu, 3, CMD_WRITE, 0x1602e000u, 0xdef0u, 0xffffffffu);   /* larb4 */
	cmd_count(&e.cpu, 4);
	ck_i64("GCE write to gap pages", gce_flush(&e, 0x100u, VCU_VDEC, 0), 0);
	ck_u64("no GCE command skipped", stats_of(&e).gce_skipped, s0.gce_skipped);
	ck_u64("four writes landed", rec_writes(&e.rec), 4);
	ck_u64("AVC_VLD through VDEC_ALL", e.rec.ops[0].win, RWVC_WIN_VDEC_ALL);
	ck_u64("AVC_VLD offset", e.rec.ops[0].off, 0x22000u);
	ck_u64("larb5 through VDEC_ALL", e.rec.ops[2].win, RWVC_WIN_VDEC_ALL);
	ck_u64("larb5 offset", e.rec.ops[2].off, 0x0d000u);
	ck_u64("larb4 offset", e.rec.ops[3].off, 0x2e000u);

	/* poll and read in the gap pages are no longer skipped either */
	cmd_set(&e.cpu, 0, CMD_WRITE, 0x16022004u, 0x55aa55aau, 0xffffffffu);
	cmd_set(&e.cpu, 1, CMD_POLL_REG, 0x16022004u, 0x55aa55aau, 0xffffffffu);
	cmd_set(&e.cpu, 2, CMD_READ, 0x16029000u, 0, 0);
	cmd_count(&e.cpu, 3);
	ck_i64("GCE poll/read in gap pages", gce_flush(&e, 0x101u, VCU_VDEC, 0), 0);
	ck_u64("still no GCE skip", stats_of(&e).gce_skipped, s0.gce_skipped);

	env_fini(&e);
}

/* ------------------------------------------------------------------ combining basics */

static void t_default_off_then_on(void)
{
	struct env e;
	struct n2vcu_stats s0, s1, s2, s3;

	ck("env_init default", env_init(&e, 0) == 0);
	s0 = stats_of(&e);
	ck_u64("combining off: no queued writes", s0.wc_writes, 0);
	put32(&e.cpu, e.vdec + 0x2f000u, 0x11223344u);
	s1 = stats_of(&e);
	/* opus-decint: an aligned VDEC store is one write, never read first (read-sensitive decoder registers) */
	ck_u64("off: an aligned VDEC store is one write", s1.hw_regs_calls, s0.hw_regs_calls + 1u);
	ck_u64("off: mmio_writes counts the store", s1.mmio_writes, s0.mmio_writes + 1u);
	ck_u64("off: store writes the word", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 0), 0x11223344u);
	ck("off: last op is the write", e.rec.nops >= 1u && e.rec.ops[e.rec.nops - 1u].op == RWVC_OP_WRITE);
	ck("off: no RMW read before it", e.rec.nops < 2u || e.rec.ops[e.rec.nops - 2u].op != RWVC_OP_READ ||
	   e.rec.ops[e.rec.nops - 2u].off != 0u);

	n2vcu_set_write_combining(e.vcu, 1);
	s1 = stats_of(&e);
	put32(&e.cpu, e.vdec + 0x2f004u, 0x55667788u);
	s2 = stats_of(&e);
	ck_u64("on: store queues without a batch", s2.hw_regs_calls, s1.hw_regs_calls);
	ck_u64("on: queued store counted", s2.wc_writes, s1.wc_writes + 1u);
	ck_u64("on: store still counted as a guest write", s2.mmio_writes, s1.mmio_writes + 1u);
	ck_u64("on: register not written yet", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 4), 0);
	ck_u64("on: guest read flushes and sees it", get32(&e.cpu, e.vdec + 0x2f004u), 0x55667788u);
	s3 = stats_of(&e);
	ck_u64("on: one batch carries the store and the read", s3.hw_regs_calls, s2.hw_regs_calls + 1u);
	ck_u64("on: one flush", s3.wc_flushes, s2.wc_flushes + 1u);
	ck_u64("on: batch held one store", s3.wc_batch_ops, s2.wc_batch_ops + 1u);
	ck_u64("on: final value", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 4), 0x55667788u);
	/* the batch order is the guest order: the store, then the read that observed it */
	ck("on: batched read follows the store", e.rec.ops[e.rec.nops - 2u].op == RWVC_OP_WRITE &&
	   e.rec.ops[e.rec.nops - 1u].op == RWVC_OP_READ);
	ck("on: store and read share one batch", e.rec.ops[e.rec.nops - 2u].call ==
	   e.rec.ops[e.rec.nops - 1u].call);

	/* turning it off flushes what is queued, and the next store is immediate again */
	put32(&e.cpu, e.vdec + 0x2f008u, 0x99aabbccu);
	ck_u64("off switch: still queued", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 8), 0);
	n2vcu_set_write_combining(e.vcu, 0);
	ck_u64("off switch: queue flushed", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 8), 0x99aabbccu);
	s3 = stats_of(&e);
	put32(&e.cpu, e.vdec + 0x2f00cu, 0x0badf00du);
	ck_u64("off again: immediate write", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 0xc), 0x0badf00du);
	ck_u64("off again: one call", stats_of(&e).hw_regs_calls, s3.hw_regs_calls + 1u);

	env_fini(&e);
}

/* A queued store and the read that follows may target different windows; the kernel executes the
 * batch in order, so the store still lands before the read. */
static void t_mixed_window_read(void)
{
	struct env e;
	struct n2vcu_stats s0, s1;
	uint32_t v;

	ck("env_init mixed windows", env_init(&e, 1) == 0);
	s0 = stats_of(&e);
	put32(&e.cpu, e.vdec + 0x2f000u, 0xdeadbeefu);
	ck_u64("mixed: store queued", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 0), 0);
	v = get32(&e.cpu, e.venc + 0x10u);
	(void)v;   /* the recorder's VENC image is zero; the order below is what matters */
	s1 = stats_of(&e);
	ck_u64("mixed: one call for the store and the read", s1.hw_regs_calls,
	       s0.hw_regs_calls + 1u);
	ck_u64("mixed: batch holds two ops", s1.wc_batch_ops, s0.wc_batch_ops + 1u);
	ck("mixed: first op is the VDEC store", e.rec.ops[e.rec.nops - 2u].op == RWVC_OP_WRITE &&
	   e.rec.ops[e.rec.nops - 2u].win == RWVC_WIN_VDEC_SYS);
	ck("mixed: second op is the VENC read", e.rec.ops[e.rec.nops - 1u].op == RWVC_OP_READ &&
	   e.rec.ops[e.rec.nops - 1u].win == RWVC_WIN_VENC);
	ck("mixed: same batch", e.rec.ops[e.rec.nops - 2u].call == e.rec.ops[e.rec.nops - 1u].call);
	ck_u64("mixed: store landed first", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 0), 0xdeadbeefu);
	ck_u64("mixed: no errors", s1.wc_errors, s0.wc_errors);
	env_fini(&e);
}

static void t_batch_limit(void)
{
	struct env e;
	struct n2vcu_stats s;
	unsigned i;

	ck("env_init batch", env_init(&e, 1) == 0);
	for (i = 0; i < 300u; i++)
		put32(&e.cpu, e.vdec + 0x1000u + 4u * i, 0xc0000000u | i);
	s = stats_of(&e);
	ck_u64("256-op cap: forced once", s.wc_forced, 1);
	ck_u64("256-op cap: one batch so far", s.wc_flushes, 1);
	ck_u64("256-op cap: batch is 256", s.wc_batch_ops, 256);
	ck_u64("256-op cap: max batch", s.wc_max_batch, 256);
	ck_u64("256-op cap: 256 writes landed", rec_writes(&e.rec), 256);
	ck_u64("44 stores still queued", get32(&e.cpu, e.vdec + 0x1000u), 0xc0000000u);
	s = stats_of(&e);
	ck_u64("read flushes the rest", s.wc_flushes, 2);
	ck_u64("all 300 writes batched", s.wc_batch_ops, 300);
	ck_u64("max batch stays 256", s.wc_max_batch, 256);
	ck_u64("300 writes landed", rec_writes(&e.rec), 300);
	ck_u64("first word", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x1000u), 0xc0000000u);
	ck_u64("last word", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x1000u + 4u * 299u),
	       0xc0000000u | 299u);
	ck_u64("no errors", s.wc_errors, 0);
	env_fini(&e);
}

static void t_subword_order(void)
{
	struct env e;

	ck("env_init subword", env_init(&e, 1) == 0);
	put32(&e.cpu, e.vdec + 0x2f008u, 0xaaaaaaaau);
	n2a32_st8(&e.cpu, e.vdec + 0x2f009u, 0xbb);
	ck_u64("byte store merges", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 8), 0xaaaabbaau);
	ck_u64("three ops: flush W, RMW R, RMW W", e.rec.nops, 3);
	ck_u64("byte: first op is the queued word", e.rec.ops[0].value, 0xaaaaaaaau);
	ck("byte: then the RMW read", e.rec.ops[1].op == RWVC_OP_READ);
	ck_u64("byte: then the merged write", e.rec.ops[2].value, 0xaaaabbaau);

	/* straddling 32-bit store: flushes, reads two words, writes two words */
	put32(&e.cpu, e.vdec + 0x2f008u, 0xaaaaaaaau);
	put32(&e.cpu, e.vdec + 0x2f00cu, 0xbbbbbbbbu);
	n2a32_st32(&e.cpu, e.vdec + 0x2f00bu, 0xdeadbeefu);
	ck_u64("straddle: low word", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 8), 0xefaaaaaau);
	ck_u64("straddle: high word", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 0xc), 0xbbdeadbeu);
	ck_u64("straddle: guest reads back", get32(&e.cpu, e.vdec + 0x2f008u), 0xefaaaaaau);
	ck_u64("straddle: guest reads high", get32(&e.cpu, e.vdec + 0x2f00cu), 0xbbdeadbeu);

	/* unaligned sub-word and 16-bit stores take the same flush-then-RMW path */
	put32(&e.cpu, e.vdec + 0x2f010u, 0x11111111u);
	n2a32_st16(&e.cpu, e.vdec + 0x2f012u, 0x2233u);
	ck_u64("halfword store merges", rec_word(&e.rec, RWVC_WIN_VDEC_SYS, 0x10u), 0x22331111u);
	ck_u64("halfword read back", n2a32_ld16(&e.cpu, e.vdec + 0x2f012u), 0x2233u);

	env_fini(&e);
}

/* ------------------------------------------------------------------ flush points */

static void t_flush_points(void)
{
	struct env e;
	struct n2vcu_stats s0, s1;

	ck("env_init flush points", env_init(&e, 1) == 0);
	/* ioctl entry */
	put32(&e.cpu, e.vdec + 0x12000u, 0x01020304u);
	ck_u64("queued before ioctl", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x12000u), 0);
	put32(&e.cpu, MAP_G, 0);
	ck_i64("SET_MMAP_TYPE", dev_ioctl(&e, MTK_VCU_SET_MMAP_TYPE, MAP_G), 0);
	ck_u64("ioctl entry flushed", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x12000u), 0x01020304u);

	/* GCE flush ioctl (empty list) */
	put32(&e.cpu, e.vdec + 0x13000u, 0x05060708u);
	cmd_count(&e.cpu, 0);
	ck_i64("empty GCE list", gce_flush(&e, 0x200u, VCU_VDEC, 0), 0);
	ck_u64("GCE entry flushed", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x13000u), 0x05060708u);

	/* a GCE poll of a register whose new value is still queued: the entry flush must put the
	 * store first, otherwise the poll (and the list) times out on the old value */
	put32(&e.cpu, e.vdec + 0x22004u, 0x0f1e2d3cu);
	cmd_set(&e.cpu, 0, CMD_POLL_REG, 0x16022004u, 0x0f1e2d3cu, 0xffffffffu);
	cmd_count(&e.cpu, 1);
	ck_i64("GCE poll of a queued value", gce_flush(&e, 0x203u, VCU_VDEC, 0), 0);
	ck_u64("poll: store landed", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x22004u), 0x0f1e2d3cu);

	/* wait_irq: the store must be in before the wait event is recorded */
	put32(&e.cpu, e.vdec + 0x14800u, 0x0a0b0c0du);
	cmd_set(&e.cpu, 0, CMD_WAIT_EVENT, 0, VDEC_EVENT_1, 0);
	cmd_count(&e.cpu, 1);
	ck_i64("GCE wait list", gce_flush(&e, 0x201u, VCU_VDEC, 0), 0);
	ck_u64("wait: store landed", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x14800u), 0x0a0b0c0du);
	ck_u64("wait: event recorded", e.rec.ev[e.rec.nev - 1u].kind, 2);
	ck_u64("wait: after the store", e.rec.ev[e.rec.nev - 1u].nops >= 1u, 1);
	ck_u64("wait: VDEC irq", e.rec.ev[e.rec.nev - 1u].a, RWVC_IRQ_VDEC);

	/* power change: guest store to VENC, then a VENC GCE list powers on */
	put32(&e.cpu, e.venc + 0x10u, 0x0e0f1011u);
	cmd_set(&e.cpu, 0, CMD_WRITE, 0x17020020u, 1u, 0xffffffffu);
	cmd_count(&e.cpu, 1);
	s0 = stats_of(&e);
	ck_i64("VENC GCE list", gce_flush(&e, 0x202u, VCU_VENC, 0), 0);
	s1 = stats_of(&e);
	ck_u64("power: GCE list ran", s1.hw_regs_calls > s0.hw_regs_calls, 1);
	ck_u64("power: VENC off again", e.rec.powered[RWVC_BLK_VENC], 0);
	ck_u64("power: store landed", rec_word(&e.rec, RWVC_WIN_VENC, 0x10u), 0x0e0f1011u);
	/* the last three events are power-on, wait? no: power on and power off */
	ck_u64("power: on event", e.rec.ev[e.rec.nev - 2u].kind, 1);
	ck_u64("power: on block", e.rec.ev[e.rec.nev - 2u].a, RWVC_BLK_VENC);
	ck_u64("power: on is true", e.rec.ev[e.rec.nev - 2u].on, 1);
	ck_u64("power: off event", e.rec.ev[e.rec.nev - 1u].on, 0);
	ck_u64("power: both after the queued store", e.rec.ev[e.rec.nev - 2u].nops >= 1u, 1);

	/* a close flushes the queue */
	put32(&e.cpu, e.vdec + 0x15000u, 0x12131415u);
	e.dev->close(e.dev->ctx);
	e.opened = 0;
	ck_u64("close flushed", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x15000u), 0x12131415u);

	env_fini(&e);
}

static void t_destroy_flush(void)
{
	struct env e;
	struct n2vcu_stats s;

	ck("env_init destroy", env_init(&e, 1) == 0);
	put32(&e.cpu, e.vdec + 0x16000u, 0x16171819u);
	s = stats_of(&e);
	ck_u64("destroy: queued", s.wc_writes, 1);
	n2vcu_destroy(e.vcu);
	e.vcu = NULL;
	e.opened = 0;
	ck_u64("destroy flushed the queue", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x16000u), 0x16171819u);
	n2a32_mem_destroy(e.mem);
	e.mem = NULL;
	rec_free(&e.rec);
}

/* ------------------------------------------------------------------ two guest threads */

struct th_arg {
	struct env *e;
	struct n2a32_cpu cpu;
	uint32_t off;
	uint32_t seed;
	unsigned n;
};

static void *th_writer(void *p)
{
	struct th_arg *a = (struct th_arg *)p;
	unsigned i;

	for (i = 0; i < a->n; i++)
		put32(&a->cpu, a->e->vdec + a->off + 4u * i, a->seed | i);
	return NULL;
}

static void t_threads(void)
{
	struct env e;
	struct th_arg a, b;
	pthread_t ta, tb;
	unsigned i;

	ck("env_init threads", env_init(&e, 1) == 0);
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	n2a32_cpu_init(&a.cpu, e.mem);
	n2a32_cpu_init(&b.cpu, e.mem);
	a.e = b.e = &e;
	a.off = 0x12000u;
	a.seed = 0x40000000u;
	a.n = 200;
	b.off = 0x13000u;
	b.seed = 0x50000000u;
	b.n = 200;
	ck_i64("thread A", pthread_create(&ta, NULL, th_writer, &a), 0);
	ck_i64("thread B", pthread_create(&tb, NULL, th_writer, &b), 0);
	pthread_join(ta, NULL);
	pthread_join(tb, NULL);
	for (i = 0; i < 200u; i++) {
		uint32_t va = get32(&e.cpu, e.vdec + a.off + 4u * i);
		uint32_t vb = get32(&e.cpu, e.vdec + b.off + 4u * i);

		if (va != (a.seed | i) || vb != (b.seed | i)) {
			printf("FAIL threaded store %u: %08x/%08x\n", i, va, vb);
			g_fails++;
			break;
		}
	}
	g_checks++;
	ck_u64("threads: no errors", stats_of(&e).wc_errors, 0);
	env_fini(&e);
}

/* ------------------------------------------------------------------ DEC02 oracle */

/* DEC02's measured H.264 access unit (facts/vdec_trace/PAGES.md: 18 pages, 19653 register ops
 * over the 12-AU 320x240 session).  dec02_au1.inc carries AU 1 (1574 ops: 1032 reads, 542
 * writes) in the order n2tr recorded it, as absolute VDEC offsets.  The measured property that
 * shapes the expected numbers: every write is immediately preceded by a read of the same
 * register (the vendor's write helper is a read-modify-write), so the whole session contains no
 * run of two stores.  Pure batching therefore has nothing to merge; what saves round trips is
 * that the read which follows a store travels in the same RWVC_REGS batch as that store, and that
 * the store no longer needs the model's own read-modify-write. */
#include "dec02_au1.inc"

#define MAX_GUEST_READS 2048u

struct trace_out {
	uint32_t reads[MAX_GUEST_READS];
	unsigned nreads;
};

static void run_dec02_trace(struct env *e, struct trace_out *out)
{
	unsigned i;

	memset(out, 0, sizeof(*out));
	for (i = 0; i < DEC02_AU1_OPS; i++) {
		uint32_t a = dec02_au1_ops[2u * i];
		uint32_t v = dec02_au1_ops[2u * i + 1u];
		uint32_t guest = e->vdec + (a >> 1) - 0x16000000u;

		if (a & 1u)
			put32(&e->cpu, guest, v);
		else
			out->reads[out->nreads++] = get32(&e->cpu, guest);
	}
}

/* Write-only subsequence, in order. */
static unsigned collect_writes(const struct rec *r, struct rop *out, unsigned max)
{
	unsigned i, n = 0;

	for (i = 0; i < r->nops && n < max; i++) {
		if (r->ops[i].op == RWVC_OP_WRITE || r->ops[i].op == RWVC_OP_WRITE_MASK)
			out[n++] = r->ops[i];
	}
	return n;
}

static void compare_write_records(const struct rec *off, const struct rec *on)
{
	static struct rop a[REC_MAX_OPS], b[REC_MAX_OPS];
	unsigned na = collect_writes(off, a, REC_MAX_OPS);
	unsigned nb = collect_writes(on, b, REC_MAX_OPS);
	unsigned i, bad = 0;

	ck_u64("oracle: same number of register writes", nb, na);
	for (i = 0; i < na && i < nb; i++) {
		if (a[i].op != b[i].op || a[i].win != b[i].win || a[i].off != b[i].off ||
		    a[i].value != b[i].value || a[i].mask != b[i].mask) {
			printf("FAIL oracle write %u: off %u/0x%x=0x%08x on %u/0x%x=0x%08x\n", i,
			       a[i].win, a[i].off, a[i].value, b[i].win, b[i].off, b[i].value);
			bad++;
			if (bad > 8)
				break;
		}
	}
	g_checks++;
	if (bad)
		g_fails++;

	/* and the resulting register image of every window is identical */
	bad = 0;
	for (i = 0; i < RWVC_WIN_COUNT; i++) {
		if (off->wbytes[i] != on->wbytes[i] ||
		    memcmp(off->wmem[i], on->wmem[i], off->wbytes[i]) != 0) {
			printf("FAIL oracle final image window %u differs\n", i);
			bad++;
		}
	}
	g_checks++;
	if (bad)
		g_fails++;
}

/* The vendor's larb sleep-protect handshake: write bit 0 at +0x00c, then read/poll bit 16
 * until the hardware acks.  LAT does it on larb5 0x1600d00c (opus-dec1's frame trace), CORE
 * on larb4 0x1602e00c (opus-dec2: util 0x292e8, smi_larb slot 0x2e000).  It is a plain guest
 * MMIO write into the larb page, i.e. exactly the access a flat "larb read-only" kernel rule
 * would refuse; the model maps both pages through RWVC_WIN_VDEC_ALL. */
static void t_larb_handshake(void)
{
	struct env e;
	struct n2vcu_stats s0, s1;

	ck("env_init larb", env_init(&e, 1) == 0);
	s0 = stats_of(&e);
	put32(&e.cpu, e.vdec + 0x0d00cu, 1u);
	/* opus-decint: larb SLP_CON stores are never queued: one bit-0 WRITE_MASK at once (kernel allow-list) */
	ck_u64("larb5 write not queued (masked write at once)", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x0d00cu) & 1u, 1u);
	ck_u64("larb5 ack read", get32(&e.cpu, e.vdec + 0x0d00cu), 0x00010001u);
	s1 = stats_of(&e);
	ck_u64("larb5 write reached window 12", e.rec.ops[e.rec.nops - 2u].win,
	       RWVC_WIN_VDEC_ALL);
	ck_u64("larb5 write offset", e.rec.ops[e.rec.nops - 2u].off, 0x0d00cu);
	ck_u64("larb5 handshake needs no queue flush", s1.wc_flushes, s0.wc_flushes);
	ck_u64("larb5 store is a masked write of bit 0", e.rec.ops[e.rec.nops - 2u].op == RWVC_OP_WRITE_MASK &&
	       e.rec.ops[e.rec.nops - 2u].mask == 1u, 1u);
	ck_u64("larb5 no bad MMIO", s1.mmio_bad, s0.mmio_bad);

	/* the core path does the same handshake on larb4 (opus-dec2: util 0x292e8, slot 0x2e000) */
	put32(&e.cpu, e.vdec + 0x2e00cu, 1u);
	ck_u64("larb4 write not queued (masked write at once)", rec_word(&e.rec, RWVC_WIN_VDEC_ALL, 0x2e00cu) & 1u, 1u);
	ck_u64("larb4 ack read", get32(&e.cpu, e.vdec + 0x2e00cu), 0x00010001u);
	ck_u64("larb4 write reached window 12", e.rec.ops[e.rec.nops - 2u].win,
	       RWVC_WIN_VDEC_ALL);
	ck_u64("larb4 write offset", e.rec.ops[e.rec.nops - 2u].off, 0x2e00cu);
	ck_u64("larb4 no bad MMIO", stats_of(&e).mmio_bad, s0.mmio_bad);
	env_fini(&e);
}

static void t_dec02_oracle(void)
{
	static struct env eoff, eon;
	static struct trace_out toff, ton;
	struct n2vcu_stats soff, son;
	unsigned i;

	ck("env_init oracle off", env_init(&eoff, 0) == 0);
	ck("env_init oracle on", env_init(&eon, 1) == 0);
	run_dec02_trace(&eoff, &toff);
	run_dec02_trace(&eon, &ton);
	/* The AU ends on a store (the start/stop trigger at 0x16000890); in the real session the next
	 * AU's first read drains it.  Here the documented off-switch flush drains it, so the comparison
	 * sees the complete measured sequence. */
	n2vcu_set_write_combining(eon.vcu, 0);
	soff = stats_of(&eoff);
	son = stats_of(&eon);

	compare_write_records(&eoff.rec, &eon.rec);
	ck_u64("oracle: same guest read count", ton.nreads, toff.nreads);
	for (i = 0; i < toff.nreads && i < ton.nreads; i++) {
		if (toff.reads[i] != ton.reads[i]) {
			printf("FAIL oracle guest read %u: off 0x%08x on 0x%08x\n", i,
			       toff.reads[i], ton.reads[i]);
			g_fails++;
			break;
		}
	}
	g_checks++;

	ck_u64("oracle: the measured AU has 1032 reads", toff.nreads, DEC02_AU1_READS);
	ck_u64("oracle: no bad MMIO (off)", soff.mmio_bad, 0);
	ck_u64("oracle: no bad MMIO (on)", son.mmio_bad, 0);
	ck_u64("oracle: no skipped GCE (off)", soff.gce_skipped, 0);
	ck_u64("oracle: no skipped GCE (on)", son.gce_skipped, 0);
	ck_u64("oracle: combining wrote through the queue", son.wc_writes, DEC02_AU1_WRITES - DEC04_AU1_LARB_STORES);
	ck_u64("oracle: no combining errors", son.wc_errors, 0);
	ck_u64("oracle: all queued writes left in batches", son.wc_batch_ops, son.wc_writes);
	ck_u64("oracle: batch cap respected", son.wc_max_batch <= 256u, 1);

	/* Measured facts of this AU, literals from the trace: 542 stores, 1032 guest reads, and not
	 * one pair of adjacent stores.  Without combining each store is the old read-modify-write
	 * pair, so the model pays 1032 + 2*542 = 2116 back-end calls (one RWVC_REGS ioctl each on
	 * the phone).  With combining every store is carried by the read that follows it: exactly
	 * 1032 calls, one per guest read, plus one write-only batch for the final trigger store;
	 * every batch holds one store + one read. */
	/* opus-decint: aligned VDEC stores are no longer read first, and the AU's 4 larb SLP_CON stores (request +
	 * release on larb5 and larb4) bypass the queue as bit-0 masked writes, one call each. */
	ck_u64("oracle: off costs one call per read plus one per store", soff.hw_regs_calls,
	       DEC02_AU1_READS + DEC02_AU1_WRITES);
	ck_u64("oracle: on costs one call per read, the final store and the 4 larb stores", son.hw_regs_calls,
	       DEC02_AU1_READS + 1u + DEC04_AU1_LARB_STORES);
	ck_u64("oracle: 538 store batches", son.wc_flushes, DEC02_AU1_WRITES - DEC04_AU1_LARB_STORES);
	ck_u64("oracle: no batch holds two stores", son.wc_max_batch, 1u);

	printf("DEC04 IOCTLS per_au off=%llu on=%llu guest_stores=%llu batches=%llu max_stores_per_batch=%llu\n",
	       (unsigned long long)soff.hw_regs_calls, (unsigned long long)son.hw_regs_calls,
	       (unsigned long long)son.wc_writes, (unsigned long long)son.wc_flushes,
	       (unsigned long long)son.wc_max_batch);
	ck("oracle: combining costs fewer register ioctls",
	   son.hw_regs_calls < soff.hw_regs_calls);

	/* Every store in the measured order but the last is followed by a read of the next register
	 * in the same back-end call: the batch is [WRITE, READ], the ordering rule in one ioctl.  The
	 * last store (the start/stop trigger at the end of the AU) has no read after it and left as
	 * the one write-only batch drained by the off-switch flush. */
	{
		unsigned k, bad = 0, pairs = 0;

		for (k = 0; k + 1u < eon.rec.nops; k++) {
			if (eon.rec.ops[k].op != RWVC_OP_WRITE)
				continue;
			pairs++;
			if (eon.rec.ops[k + 1u].op != RWVC_OP_READ ||
			    eon.rec.ops[k + 1u].call != eon.rec.ops[k].call)
				bad++;
		}
		ck_u64("oracle: every store but the final trigger shares a batch with the next read",
		       pairs, DEC02_AU1_WRITES - 1u - DEC04_AU1_LARB_STORES);
		ck_u64("oracle: no store escaped the merged batch", bad, 0);
		ck_u64("oracle: the final trigger store drained alone", son.wc_flushes,
		       DEC02_AU1_WRITES - DEC04_AU1_LARB_STORES);
	}

	env_fini(&eon);
	env_fini(&eoff);
}

/* ------------------------------------------------------------------ main */

int main(void)
{
	printf("DEC04 tests (write-combining + VDEC_ALL), header rev3: win %d count %d\n",
	       RWVC_WIN_VDEC_ALL, RWVC_WIN_COUNT);
	t_windows();
	t_gce_gap_pages();
	t_default_off_then_on();
	t_mixed_window_read();
	t_batch_limit();
	t_subword_order();
	t_flush_points();
	t_destroy_flush();
	t_threads();
	t_larb_handshake();
	t_dec02_oracle();
	printf("SUMMARY %u %u\n", g_checks - g_fails, g_checks);
	return g_fails ? 1 : 0;
}
