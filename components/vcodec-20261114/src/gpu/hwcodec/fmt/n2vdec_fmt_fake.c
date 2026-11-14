/* SPDX-License-Identifier: MIT */
/*
 * n2vdec_fmt_fake.c -- opus-fmt: see n2vdec_fmt_fake.h.  Register meanings and their evidence: facts/FORMATS.md section 4.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n2vdec_fmt_fake.h"
#include "redwood_vcodec.h"
#include "n2a32.h"

int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes);   /* V41 */
int n2vc_dma_lookup_pa(uint32_t pa, uint32_t *handle, uint32_t *guest_addr, uint32_t *bytes) __attribute__((weak));
#define NALLOC 512

#define VDEC_BYTES 0x30000u
enum {
	O_LARB5 = 0x0d000, O_LAT_MISC = 0x10000, O_LAT_VLD = 0x11000, O_VLD = 0x20000, O_MC = 0x21000, O_MISC = 0x25000,
	O_BS2 = 0x26800, O_VP8 = 0x27800, O_VP9 = 0x28400, O_AV1 = 0x28800, O_LARB4 = 0x2e000,
	O_LAT_VP9 = 0x15400, O_LAT_AV1 = 0x15800,
};
/* rev 2 window index -> base offset; 12 = RWVC_WIN_VDEC_ALL (same table as fakevdec_hevc) */
static const int32_t WIN_BASE[13] = { -1, -1, 0x2f000, 0x00000, 0x20000, 0x21000, 0x23000, 0x25000, 0x10000, 0x11000,
	0x04000, 0x0f000, 0x00000 };

/* One bitstream reader: vfifo ring + latched read pointer (16-byte aligned like the 128-bit fetch; the vendor skips
 * rptr & 15 bytes itself, VP8_InitBarrelShifter) + raw bit position (VP8/VP9/AV1/MPEG have no emulation prevention). */
struct unit { uint32_t vstart, vend, rptr, fetch_ok; uint64_t bit; };

struct n2vdec_fmt_fake {
	struct n2vc_hw inner;
	struct n2a32_mem *mem;
	pthread_mutex_t lock;
	uint32_t reg[VDEC_BYTES / 4];        /* last value written through this wrapper */
	struct unit core, lat, bs2;
	struct { uint32_t handle, pa, bytes; } alloc[NALLOC];
	FILE *trace; char last[96]; unsigned last_rep;
	struct n2vdec_fmt_fake_stats st;
};

static void tr(struct n2vdec_fmt_fake *m, const char *fmt, ...)
{
	char b[96]; va_list ap;
	if (!m->trace) return;
	va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
	if (!strcmp(b, m->last)) { m->last_rep++; return; }
	if (m->last_rep > 1) fprintf(m->trace, "  (x%u)\n", m->last_rep);
	fprintf(m->trace, "%s\n", b);
	snprintf(m->last, sizeof m->last, "%s", b); m->last_rep = 1;
}

static int read_byte(struct n2vdec_fmt_fake *m, uint32_t pa, uint8_t *out)
{
	uint32_t h = 0, g = 0, n = 0, p0 = 0;
	*out = 0;
	for (unsigned i = 0; i < NALLOC; i++) {
		if (!m->alloc[i].bytes || pa - m->alloc[i].pa >= m->alloc[i].bytes) continue;
		if (m->mem && n2vc_dma_lookup_handle(m->alloc[i].handle, &p0, &g, &n) == 0 && g) {
			*out = *(const uint8_t *)n2a32_ptr(m->mem, g + (pa - m->alloc[i].pa));
			return 0;
		}
		return m->inner.dma_rw(m->inner.ctx, m->alloc[i].handle, pa - m->alloc[i].pa, out, 1, 0);
	}
	if (n2vc_dma_lookup_pa && n2vc_dma_lookup_pa(pa, &h, &g, &n) == 0) {      /* client buffer (V41 registry) */
		if (m->mem && g) { *out = *(const uint8_t *)n2a32_ptr(m->mem, g); return 0; }
		if (h && n2vc_dma_lookup_handle(h, &p0, &g, &n) == 0 && pa >= p0) return m->inner.dma_rw(m->inner.ctx, h, pa - p0, out, 1, 0);
	}
	return -EFAULT;
}
static uint8_t ubyte(struct n2vdec_fmt_fake *m, const struct unit *u, uint64_t idx)
{
	uint32_t size = u->vend > u->vstart ? u->vend - u->vstart : 0, a; uint8_t b;
	uint64_t o = (uint64_t)(u->rptr - u->vstart) + idx;
	a = size ? u->vstart + (uint32_t)(o % size) : u->rptr + (uint32_t)idx;
	(void)read_byte(m, a, &b);
	return b;
}
static uint32_t peek32(struct n2vdec_fmt_fake *m, const struct unit *u)
{
	uint64_t by = u->bit >> 3, w = 0; unsigned sh = (unsigned)(u->bit & 7);
	for (int i = 0; i < 5; i++) w = (w << 8) | ubyte(m, u, by + (uint64_t)i);
	return (uint32_t)(w >> (8 - sh));
}

static int unit_of(uint32_t off, struct n2vdec_fmt_fake *m, struct unit **u, uint32_t *r)
{
	if (off >= O_BS2 && off < O_BS2 + 0x400) { *u = &m->bs2; *r = off - O_BS2; return 1; }
	if (off >= O_VLD && off < O_VLD + 0x400) { *u = &m->core; *r = off - O_VLD; return 1; }
	if (off >= O_LAT_VLD && off < O_LAT_VLD + 0x400) { *u = &m->lat; *r = off - O_LAT_VLD; return 1; }
	return 0;
}

/* Writes: track the reader state; return nothing (the inner back end already stored the value). */
static void on_write(struct n2vdec_fmt_fake *m, uint32_t off, uint32_t v)
{
	struct unit *u; uint32_t r;
	m->reg[off >> 2] = v;
	if (unit_of(off, m, &u, &r)) {
		switch (r) {
		case 0xb4: u->vstart = v << 6; break;
		case 0xb8: u->vend = v << 6; break;
		case 0xb0: u->rptr = v & ~0xfu; u->bit = 0; break;
		case 0x108: if (v & 0x100u) u->bit = 0; break;
		case 0x8c: if (v & (1u << 20)) u->fetch_ok = 1; if (v & (1u << 23)) u->bit = 0; break;
		}
	}
	if (off == O_LARB4 + 0xc || off == O_LARB5 + 0xc)
		m->reg[off >> 2] = (v & 1u) ? (v | 0x10000u) : (v & ~0x10000u);
	if (off == O_MISC + 0x100 || off == O_LAT_MISC + 0x100)
		m->reg[(off + 4) >> 2] = (v & 1u) ? 0x11u : 0u;
}

/* Reads: 1 = overridden (*v set). */
static int on_read(struct n2vdec_fmt_fake *m, uint32_t off, uint32_t *v)
{
	struct unit *u; uint32_t r;
	if (unit_of(off, m, &u, &r)) {
		switch (r) {
		case 0xe8: *v = u->fetch_ok; return 1;       /* fetch ok (bit 0) */
		case 0xf4: *v = 0; return 1;                 /* process flag: bit 15 clear = idle */
		}
		return 0;
	}
	/* VP8 VLD (0x27800): partition 1 reader at +0x000 (fed by the core VLD), partition 2 at +0x200 (fed by BS2);
	 * register 0 = next 32 bits, register n (1..32) = shift n bits then next 32 bits (VP8_VLDGetBits 0x125336) */
	if (off >= O_VP8 && off < O_VP8 + 0x400 && ((off - O_VP8) & 0x1ff) <= 0x80) {
		struct unit *pu = (off - O_VP8) >= 0x200 ? &m->bs2 : &m->core;
		uint32_t n = ((off - O_VP8) & 0x1ff) >> 2;
		pu->bit += n;
		*v = peek32(m, pu);
		return 1;
	}
	/* VP9 / AV1 bit readers (vdec_hal_read_vp9 / vdec_hal_read_av1 index 0 = peek, 1..32 = shift n then peek;
	 * vdec_hal_av1_read_literal_raw 0x146724): core 0x28400 / 0x28800 fed by the core VLD, LAT 0x15400 / 0x15800 by LAT_VLD */
	{
		struct unit *pu = NULL; uint32_t base = 0;
		if (off >= O_VP9 && off <= O_VP9 + 0x80) { pu = &m->core; base = O_VP9; }
		else if (off >= O_AV1 && off <= O_AV1 + 0x80) { pu = &m->core; base = O_AV1; }
		else if (off >= O_LAT_VP9 && off <= O_LAT_VP9 + 0x80) { pu = &m->lat; base = O_LAT_VP9; }
		else if (off >= O_LAT_AV1 && off <= O_LAT_AV1 + 0x80) { pu = &m->lat; base = O_LAT_AV1; }
		if (pu && !((off - base) & 3)) { pu->bit += (off - base) >> 2; *v = peek32(m, pu); return 1; }
	}
	/* VP9 counts-table clear: write 1 to VP9(0x6a) = +0x1a8, poll until 0 (vendor log "vdec_hal_vp9_clear_counts_table
	 * timeout" after 50000 reads on the LAT copy 0x155a8, run f320-vp9 2026-11-12) -> done at once, core and LAT */
	if (off == O_VP9 + 0x1a8 || off == O_LAT_VP9 + 0x1a8) { *v = 0; return 1; }
	/* VP9 status words polled until a bit is SET (polls.py over libvcodecdrv, 2026-11-12):
	 *   +0x110 (idx 0x44) bit 16: bool decoder initialised (vdec_hal_vp9_init_bool 0x12da5c, VP9_initBool)
	 *   +0x0cc (idx 0x33) bits 0..13: per-table probability update done (VP9_update*Probs, vdec_hal_vp9_update_probs)
	 *   +0x1ec (idx 0x7b) bit 0: counts / prob table WDMA done (vdec_hal_vp9_count_tbl_wdma, _prob_tbl_wdma) */
	if (off == O_VP9 + 0x110 || off == O_LAT_VP9 + 0x110) { *v = m->reg[off >> 2] | 0x10000u; return 1; }
	if (off == O_VP9 + 0x0cc || off == O_LAT_VP9 + 0x0cc) { *v = 0x3fffu; return 1; }
	if (off == O_VP9 + 0x1ec || off == O_LAT_VP9 + 0x1ec) { *v = m->reg[off >> 2] | 1u; return 1; }
	/* VP8 picture finished: VP8VLD 0x170 / 0x174 bit 0 (VP8_ReadFinishFlag 0x1243e2) [I: always done here] */
	if (off == O_VP8 + 0x170 || off == O_VP8 + 0x174) { *v = m->reg[off >> 2] | 1u; return 1; }
	/* VP8 hardware probability updates: write 1, poll until 0 (VP8_HwAccCoefProbUpdate 0x120dd8: 0x148,
	 * VP8_HwAccMVProbUpdate 0x120e10: 0x14c) -> done at once (the real block consumes bool-coded bits here) */
	if (off == O_VP8 + 0x148 || off == O_VP8 + 0x14c) { *v = 0; return 1; }
	if (off == O_LARB4 + 0xc || off == O_LARB5 + 0xc || off == O_MISC + 0x104 || off == O_LAT_MISC + 0x104) {
		*v = m->reg[off >> 2]; return 1;
	}
	if ((off >= O_MISC + 0x110 && off <= O_MISC + 0x13c) || (off >= O_LAT_MISC + 0x110 && off <= O_LAT_MISC + 0x13c)) {
		*v = 0; return 1;
	}
	return 0;
}

static int vdec_off(unsigned win, uint32_t off, uint32_t *abs)
{
	if (win > 12 || WIN_BASE[win] < 0) return 0;
	*abs = (uint32_t)WIN_BASE[win] + off;
	return *abs < VDEC_BYTES;
}

static int f_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct n2vdec_fmt_fake *m = ctx;
	unsigned i; int rc = 0;
	for (i = 0; i < count; i++) {
		struct rwvc_reg_op *o = &ops[i], chk = *o; uint32_t a, v; unsigned d1 = 0;
		int mine = o->op != RWVC_OP_DELAY_US && vdec_off(o->window, o->offset, &a);
		if (mine && chk.op == RWVC_OP_POLL) chk.op = RWVC_OP_READ;
		rc = m->inner.regs(m->inner.ctx, &chk, 1, &d1);
		if (rc) break;
		if (!mine) { *o = chk; continue; }
		pthread_mutex_lock(&m->lock);
		m->st.ops++;
		switch (o->op) {
		case RWVC_OP_READ:
			v = chk.value;
			if (on_read(m, a, &v)) m->st.overrides++;
			o->value = v;
			tr(m, "R %05x %08x", a, v);
			break;
		case RWVC_OP_WRITE:
			on_write(m, a, o->value);
			tr(m, "W %05x %08x", a, o->value);
			break;
		case RWVC_OP_WRITE_MASK:
			on_write(m, a, (m->reg[a >> 2] & ~o->mask) | (o->value & o->mask));
			tr(m, "M %05x %08x/%08x", a, o->value, o->mask);
			break;
		case RWVC_OP_POLL:
			v = chk.value;
			if (on_read(m, a, &v)) m->st.overrides++;
			tr(m, "P %05x %08x/%08x -> %08x", a, o->value, o->mask, v);
			if ((v & o->mask) != (o->value & o->mask)) rc = -ETIMEDOUT;
			break;
		}
		pthread_mutex_unlock(&m->lock);
		if (rc) break;
	}
	if (done) *done = i;
	return rc;
}

static int f_power(void *c, unsigned b, int on) { struct n2vdec_fmt_fake *m = c; return m->inner.power(m->inner.ctx, b, on); }
static int f_wait_irq(void *c, unsigned irq, unsigned t, uint32_t *s) { struct n2vdec_fmt_fake *m = c; return m->inner.wait_irq(m->inner.ctx, irq, t, s); }
static int f_irq_clear(void *c, unsigned irq) { struct n2vdec_fmt_fake *m = c; return m->inner.irq_clear(m->inner.ctx, irq); }
static int f_dma_alloc(void *c, uint32_t b, uint32_t fl, uint32_t *h, uint32_t *pa)
{
	struct n2vdec_fmt_fake *m = c;
	int rc = m->inner.dma_alloc(m->inner.ctx, b, fl, h, pa);
	if (rc == 0) {
		pthread_mutex_lock(&m->lock);
		for (unsigned i = 0; i < NALLOC; i++)
			if (!m->alloc[i].bytes) { m->alloc[i].handle = *h; m->alloc[i].pa = *pa; m->alloc[i].bytes = b; break; }
		pthread_mutex_unlock(&m->lock);
	}
	return rc;
}
static int f_dma_free(void *c, uint32_t h)
{
	struct n2vdec_fmt_fake *m = c;
	pthread_mutex_lock(&m->lock);
	for (unsigned i = 0; i < NALLOC; i++)
		if (m->alloc[i].bytes && m->alloc[i].handle == h) memset(&m->alloc[i], 0, sizeof m->alloc[i]);
	pthread_mutex_unlock(&m->lock);
	return m->inner.dma_free(m->inner.ctx, h);
}
static int f_dma_rw(void *c, uint32_t h, uint32_t o, void *p, uint32_t b, int w) { struct n2vdec_fmt_fake *m = c; return m->inner.dma_rw(m->inner.ctx, h, o, p, b, w); }
static void f_close(void *c)
{
	struct n2vdec_fmt_fake *m = c;
	if (m->trace) { if (m->last_rep > 1) fprintf(m->trace, "  (x%u)\n", m->last_rep); fclose(m->trace); }
	m->inner.close(m->inner.ctx);
	free(m);
}

int n2vdec_fmt_fake_open(struct n2vc_hw *out, const struct n2vc_hw *inner, const char *trace_path, struct n2vdec_fmt_fake **model)
{
	struct n2vdec_fmt_fake *m = calloc(1, sizeof *m);
	if (!m) return -ENOMEM;
	m->inner = *inner;
	pthread_mutex_init(&m->lock, NULL);
	if (trace_path && *trace_path && (m->trace = fopen(trace_path, "w")) != NULL) setvbuf(m->trace, NULL, _IOLBF, 0);
	memset(out, 0, sizeof *out);
	out->ctx = m; out->power = f_power; out->regs = f_regs; out->wait_irq = f_wait_irq; out->irq_clear = f_irq_clear;
	out->dma_alloc = f_dma_alloc; out->dma_free = f_dma_free; out->dma_rw = f_dma_rw; out->close = f_close;
	if (model) *model = m;
	return 0;
}
void n2vdec_fmt_fake_set_mem(struct n2vdec_fmt_fake *m, struct n2a32_mem *mem) { m->mem = mem; }
void n2vdec_fmt_fake_stats(struct n2vdec_fmt_fake *m, struct n2vdec_fmt_fake_stats *out)
{
	pthread_mutex_lock(&m->lock); *out = m->st; pthread_mutex_unlock(&m->lock);
}
