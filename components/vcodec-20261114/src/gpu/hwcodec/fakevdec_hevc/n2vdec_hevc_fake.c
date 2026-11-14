/* SPDX-License-Identifier: MIT */
/*
 * n2vdec_hevc_fake.c -- opus-hevcdec: VDEC register model for the vendor HEVC decode path on the host runner.
 * See n2vdec_hevc_fake.h for the scope and facts/HEVC_DEC_PROTOCOL.md for every register rule (evidence there).
 * No vendor code: only register offsets/bit positions observed in the vendor's accesses.
 */
#define _POSIX_C_SOURCE 200809L
#include "n2vdec_hevc_fake.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n2a32.h"

int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes);   /* V41 */
int n2vc_dma_lookup_pa(uint32_t pa, uint32_t *handle, uint32_t *guest_addr, uint32_t *bytes) __attribute__((weak)); /* V41 */

#define VDEC_BYTES 0x30000u
#define NALLOC 512

/* ---------------------------------------------------------------- sub-block offsets (VDEC_REGISTERS.md §1) */
enum {
	O_LARB5 = 0x0d000, O_LAT_MISC = 0x10000, O_LAT_VLD = 0x11000, O_LAT_HEVC = 0x15000,
	O_VLD = 0x20000, O_MISC = 0x25000, O_HEVC = 0x28000, O_LARB4 = 0x2e000,
};
enum { U_CORE = 0, U_LAT = 1 };

/* rev 2 window index -> base offset; 12 = proposed RWVC_WIN_VDEC_ALL */
static const int32_t WIN_BASE[13] = { -1, -1, 0x2f000, 0x00000, 0x20000, 0x21000, 0x23000, 0x25000, 0x10000, 0x11000,
	0x04000, 0x0f000, 0x00000 };

/* ---------------------------------------------------------------- bit reader (one per unit) */
struct unit {
	uint32_t vstart, vend;          /* vfifo ring [start, end) in PA (VLD 0xb4/0xb8 << 6) */
	uint32_t rptr_latch;            /* VLD 0xb0 */
	uint64_t bit;                   /* position in the de-emulated view, counted from rptr_latch (raw bytes) */
	uint32_t fetch_ok;              /* VLD 0xe8 bit 0 */
	uint8_t *cb, *ce;               /* de-emulated bytes from the latch on; ce[i] = 1: an EPB was dropped before cb[i] */
	uint64_t clen, ccap, craw;      /* cached bytes, capacity, raw bytes consumed */
	int cz;                         /* zero run at craw */
};

struct n2vdec_hevc_fake {
	struct n2vc_hw inner;
	struct n2a32_mem *mem;
	pthread_mutex_t lock;
	pthread_cond_t cv;
	uint32_t reg[VDEC_BYTES / 4];
	struct unit u[2];
	unsigned irq_pending[RWVC_IRQ_COUNT];
	struct { uint32_t handle, pa, bytes; } alloc[NALLOC];
	uint32_t hit_pa, hit_bytes; const uint8_t *hit_host;
	FILE *trace;
	char last[96]; unsigned last_rep;
	struct n2vdec_hevc_fake_stats st;
};

static void tr_flush(struct n2vdec_hevc_fake *m)
{
	if (m->trace && m->last_rep > 1)
		fprintf(m->trace, "  (x%u)\n", m->last_rep);
	m->last_rep = 0; m->last[0] = 0;
}
static void tr(struct n2vdec_hevc_fake *m, const char *fmt, ...)
{
	char b[96]; va_list ap;
	if (!m->trace) return;
	va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
	if (!strcmp(b, m->last)) { m->last_rep++; return; }
	tr_flush(m);
	fprintf(m->trace, "%s\n", b);
	snprintf(m->last, sizeof m->last, "%s", b); m->last_rep = 1;
}

/* ---------------------------------------------------------------- bus: bytes at a PA, read like coherent DMA */
static int read_byte(struct n2vdec_hevc_fake *m, uint32_t pa, uint8_t *out)
{
	if (m->hit_bytes && pa - m->hit_pa < m->hit_bytes) {                /* last buffer hit (one ring per job) */
		*out = m->hit_host[pa - m->hit_pa];
		return 0;
	}
	for (unsigned i = 0; i < NALLOC; i++) {
		if (!m->alloc[i].bytes || pa < m->alloc[i].pa || pa - m->alloc[i].pa >= m->alloc[i].bytes) continue;
		uint32_t p0 = 0, g = 0, n = 0, off = pa - m->alloc[i].pa;
		if (m->mem && n2vc_dma_lookup_handle(m->alloc[i].handle, &p0, &g, &n) == 0 && g) {
			m->hit_pa = m->alloc[i].pa; m->hit_bytes = m->alloc[i].bytes;
			m->hit_host = n2a32_ptr(m->mem, g);
			*out = m->hit_host[off];
			return 0;
		}
		return m->inner.dma_rw(m->inner.ctx, m->alloc[i].handle, off, out, 1, 0);
	}
	{
		/* opus-decint: a buffer this wrapper did not allocate -- the client's own bitstream buffer, which the vendor's
		 * HEVC LAT reads in place (bs_dma = the client's pa).  V41's registry knows it (vcodecd registers client
		 * handles); read its guest mirror (refreshed from the device before every START) like coherent DMA. */
		uint32_t h = 0, g = 0, n = 0;
		if (n2vc_dma_lookup_pa && m->mem && n2vc_dma_lookup_pa(pa, &h, &g, &n) == 0 && g) {
			*out = *(const uint8_t *)n2a32_ptr(m->mem, g);
			return 0;
		}
		if (n2vc_dma_lookup_pa && n2vc_dma_lookup_pa(pa, &h, &g, &n) == 0 && h) {
			uint32_t p0 = 0, g0 = 0, n0 = 0;
			if (n2vc_dma_lookup_handle(h, &p0, &g0, &n0) == 0 && pa >= p0)
				return m->inner.dma_rw(m->inner.ctx, h, pa - p0, out, 1, 0);
		}
	}
	*out = 0;
	return -EFAULT;
}

static uint32_t ring_pa(const struct unit *u, uint64_t byteoff)
{
	uint32_t size = u->vend > u->vstart ? u->vend - u->vstart : 0;
	uint64_t a = (uint64_t)u->rptr_latch - u->vstart + byteoff;
	return size ? u->vstart + (uint32_t)(a % size) : u->rptr_latch + (uint32_t)byteoff;
}

static void cache_reset(struct unit *u) { u->clen = 0; u->craw = 0; u->cz = 0; }

/* Byte idx of the de-emulated stream after the latch (00 00 03 -> 00 00), cached append-only. */
static uint8_t de_byte(struct n2vdec_hevc_fake *m, struct unit *u, uint64_t idx)
{
	while (u->clen <= idx) {
		uint8_t b = 0; int dropped = 0;
		if (u->craw > (64u << 20)) return 0;                     /* runaway guard: past any vfifo */
		if (u->clen == u->ccap) {
			uint64_t n = u->ccap ? u->ccap * 2 : 65536;
			uint8_t *nb = realloc(u->cb, n), *ne;
			if (!nb) return 0;
			u->cb = nb;
			ne = realloc(u->ce, n);
			if (!ne) return 0;
			u->ce = ne; u->ccap = n;
		}
		for (;;) {
			(void)read_byte(m, ring_pa(u, u->craw), &b);
			u->craw++;
			if (u->cz >= 2 && b == 3) { u->cz = 0; dropped = 1; continue; }
			break;
		}
		u->cz = b == 0 ? u->cz + 1 : 0;
		u->cb[u->clen] = b; u->ce[u->clen] = (uint8_t)dropped; u->clen++;
	}
	return u->cb[idx];
}

static uint32_t peek32(struct n2vdec_hevc_fake *m, struct unit *u)
{
	uint64_t byte = u->bit >> 3; unsigned sh = (unsigned)(u->bit & 7);
	uint64_t w = 0;
	for (int i = 0; i < 5; i++) w = (w << 8) | de_byte(m, u, byte + (uint64_t)i);
	return (uint32_t)(w >> (8 - sh));
}
static uint32_t getbits(struct n2vdec_hevc_fake *m, struct unit *u, unsigned n)
{
	uint32_t v = n ? peek32(m, u) >> (32 - n) : 0;
	u->bit += n;
	return v;
}
static uint32_t ue(struct n2vdec_hevc_fake *m, struct unit *u)
{
	unsigned lz = 0;
	while (lz < 32 && getbits(m, u, 1) == 0) lz++;
	if (lz >= 32) return 0xffffffffu;
	return (lz ? getbits(m, u, lz) : 0) + (1u << lz) - 1u;
}
static int32_t se(struct n2vdec_hevc_fake *m, struct unit *u)
{
	uint32_t k = ue(m, u);
	return (k & 1) ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
}

/* Start-code search: the window is placed on the next 00 00 01 at or after the current byte. */
static void hevc_search(struct n2vdec_hevc_fake *m, struct unit *u)
{
	uint64_t b = (u->bit + 7) >> 3;
	if (u->vend <= u->vstart) return;
	uint64_t lim = u->vend > u->vstart ? u->vend - u->vstart : (4u << 20);
	for (uint64_t i = 0; i < lim; i++, b++)
		if (de_byte(m, u, b) == 0 && de_byte(m, u, b + 1) == 0 && de_byte(m, u, b + 2) == 1 && !u->ce[b + 1] && !u->ce[b + 2]) {
			u->bit = b << 3; return;
		}
}

/* ---------------------------------------------------------------- register rules */
static void raise_irq(struct n2vdec_hevc_fake *m, unsigned irq);

/* "Decode done" for one unit: no pixels; the reader moves past the slice data to the next start code (as the block
 * consumed the slice), picture-complete (HEVC_VLD(158) bit 31) is set and the done interrupt is raised. */
static void decode_done(struct n2vdec_hevc_fake *m, int k)
{
	struct unit *u = &m->u[k];
	uint32_t base = k == U_CORE ? O_HEVC : O_LAT_HEVC;
	if (u->vend > u->vstart) {          /* only a unit that reads the bitstream (in LAT+core mode the core reads the UBE) */
		u->bit = (((u->bit + 7) >> 3) + 3) << 3;
		hevc_search(m, u);
	}
	m->reg[(base + 0x9e * 4) >> 2] |= 0x80000000u;
	/* last LCU position (HEVC_VLD(75): x | y << 16, read by vdec_hal_h265_get_mbx_mby) = what the vendor programmed in
	 * VLD_TOP(26) (pic width/height in CTBs - 1): the whole picture was decoded */
	m->reg[(base + 0x4b * 4) >> 2] = m->reg[((k == U_CORE ? O_VLD : O_LAT_VLD) + 0x800 + 0x68) >> 2];
	if (k == U_CORE) m->st.core_triggers++; else m->st.lat_triggers++;
	raise_irq(m, k == U_CORE ? RWVC_IRQ_VDEC : RWVC_IRQ_VDEC_LAT);
}

static uint32_t *R(struct n2vdec_hevc_fake *m, uint32_t off) { return &m->reg[off >> 2]; }

static void raise_irq(struct n2vdec_hevc_fake *m, unsigned irq)
{
	m->irq_pending[irq]++;
	if (irq == RWVC_IRQ_VDEC) { *R(m, O_MISC + 0xa4) |= 0x10000u; m->st.core_irqs++; }
	else { *R(m, O_LAT_MISC + 0xa4) |= 0x10000u; m->st.lat_irqs++; }
	pthread_cond_broadcast(&m->cv);
}

static int vld_unit(uint32_t off, uint32_t *reg)
{
	if (off >= O_VLD && off < O_VLD + 0x400) { *reg = (off - O_VLD) >> 2; return U_CORE; }
	if (off >= O_LAT_VLD && off < O_LAT_VLD + 0x400) { *reg = (off - O_LAT_VLD) >> 2; return U_LAT; }
	return -1;
}
static int hevc_unit(uint32_t off, uint32_t *reg)
{
	if (off >= O_HEVC && off < O_HEVC + 0x400) { *reg = (off - O_HEVC) >> 2; return U_CORE; }
	if (off >= O_LAT_HEVC && off < O_LAT_HEVC + 0x400) { *reg = (off - O_LAT_HEVC) >> 2; return U_LAT; }
	return -1;
}

static void do_write(struct n2vdec_hevc_fake *m, uint32_t off, uint32_t v)
{
	uint32_t reg; int k;
	*R(m, off) = v;
	if ((k = vld_unit(off, &reg)) >= 0) {
		struct unit *u = &m->u[k];
		switch (reg) {
		case 0x2d: u->vstart = v << 6; cache_reset(u); break;
		case 0x2e: u->vend = v << 6; cache_reset(u); break;
		case 0x2c: u->rptr_latch = v & ~0xfu; cache_reset(u); break;   /* 128-bit fetch: the vendor skips rptr & 15 bytes itself */
		case 0x42: if (v & 0x100u) { u->bit = 0; cache_reset(u); } break;
		case 0x23:
			if (v & (1u << 20)) u->fetch_ok = 1;
			if (v & (1u << 23)) { u->bit = 0; cache_reset(u); }
			break;
		}
		return;
	}
	if ((k = hevc_unit(off, &reg)) >= 0 && reg == 0x25) {          /* HEVC_VLD(37): bit 8 search start code, 4 ref reorder, 0 wp table */
		struct unit *u = &m->u[k];
		if (v & 0x100u) hevc_search(m, u);
		*R(m, off) = v & ~0x111u;                                  /* done at once */
		return;
	}
	if (k >= 0 && reg == 0x24 && (v & 1u)) {                      /* vdec_hal_h265_dec_start: HEVC_VLD(36) bit 0 = trigger */
		decode_done(m, k);
		*R(m, off) = v & ~1u;
		return;
	}
	if (off == 0x00064 && (v & 1u)) {                             /* multi-core start: SOC MCORE_TOP(25) bit 0 */
		*R(m, 0x0f020) |= 0x10000u;                               /* SOC_GCON(8) bit 16, read by wait_core_done */
		decode_done(m, U_CORE);
		*R(m, off) = v & ~1u;
		return;
	}
	if (off == O_MISC + 0x100 || off == O_LAT_MISC + 0x100) {      /* HW break request -> MISC(65) 0x11 ready */
		*R(m, off + 4) = (v & 1u) ? (*R(m, off + 4) | 0x11u) : (*R(m, off + 4) & ~0x11u);
		return;
	}
	if (off == O_LARB4 + 0xc || off == O_LARB5 + 0xc) {          /* SMI larb SLP_CON: bit 0 request -> bit 16 ready */
		*R(m, off) = (v & 1u) ? (v | 0x10000u) : (v & ~0x10000u);
		return;
	}
	if (off == O_MISC + 0xa4 || off == O_LAT_MISC + 0xa4) {        /* IRQ ack (mtk_vcodec_intr.c:61-153) */
		if (v & 0x10u) *R(m, off) = v & ~0x10000u;
		return;
	}
}

static uint32_t do_read(struct n2vdec_hevc_fake *m, uint32_t off)
{
	uint32_t reg, v = *R(m, off); int k;
	if ((k = vld_unit(off, &reg)) >= 0) {
		struct unit *u = &m->u[k];
		switch (reg) {
		case 0x3a: return (v & ~1u) | u->fetch_ok;                   /* fetch ok */
		case 0x3d: return 0;                                         /* process flag: bit 15 clear */
		case 0x3b: return v & ~0x0300ffffu;
		case 0x34: return v & ~0x00ff0000u;
		case 0x3f: { uint32_t size = u->vend > u->vstart ? u->vend - u->vstart : 1;
			return u->vstart + (uint32_t)((((u->rptr_latch - u->vstart) + (u->bit >> 5) * 4u) + 0x1cu) % size); }
		}
		return v;
	}
	if ((k = hevc_unit(off, &reg)) >= 0) {
		struct unit *u = &m->u[k];
		if (reg == 0) return peek32(m, u);
		if (reg <= 32) { u->bit += reg; return peek32(m, u); }
		if (reg == 0x26) return ue(m, u);
		if (reg == 0x27) return (uint32_t)se(m, u);
		if (reg == 0x21) return (v & ~0x0f003f00u) | ((uint32_t)(u->bit & 31) << 8);   /* [13:8] bit count, [27:24] EPB flags = 0 */
		return v;
	}
	if (off == 0x02004)                                              /* SOC WRAP_TOP(1)[5:0] = 0x20: idle, reset allowed */
		return (v & ~0x3fu) | 0x20u;                                 /* (vdec_hal_reset_wrap_top_1 polls it <= 202 times) */
	if ((off >= O_MISC + 0x110 && off <= O_MISC + 0x13c) || (off >= O_LAT_MISC + 0x110 && off <= O_LAT_MISC + 0x13c))
		return 0;                                                    /* all DRAM and bits processed */
	return v;
}

/* ---------------------------------------------------------------- n2vc_hw entry points */
static int f_power(void *ctx, unsigned block, int on)
{
	struct n2vdec_hevc_fake *m = ctx;
	return m->inner.power(m->inner.ctx, block, on);
}

static int vdec_off(unsigned win, uint32_t off, uint32_t *abs)
{
	if (win > 12 || WIN_BASE[win] < 0) return 0;
	*abs = (uint32_t)WIN_BASE[win] + off;
	return *abs < VDEC_BYTES;
}

static int f_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct n2vdec_hevc_fake *m = ctx;
	unsigned i; int rc = 0;
	for (i = 0; i < count; i++) {
		struct rwvc_reg_op *o = &ops[i]; uint32_t a;
		if (o->op == RWVC_OP_DELAY_US || !vdec_off(o->window, o->offset, &a)) {
			unsigned d1 = 0;
			rc = m->inner.regs(m->inner.ctx, o, 1, &d1);
			if (rc) break;
			continue;
		}
		{
			/* opus-decint: kernel parity.  The op first goes through the inner back end (T02's fake: the kernel's
			 * window/offset checks, per-page VDEC power and the larb allow-list of nord2_vcodec.inc); a refusal
			 * there is returned exactly as the phone's RWVC_REGS would, and only an accepted op reaches the model.
			 * A POLL is validated as a READ (the inner register file does not know the model's status bits). */
			struct rwvc_reg_op chk = *o;
			unsigned d1 = 0;
			if (chk.op == RWVC_OP_POLL)
				chk.op = RWVC_OP_READ;
			rc = m->inner.regs(m->inner.ctx, &chk, 1, &d1);
			if (rc) break;
		}
		pthread_mutex_lock(&m->lock);
		switch (o->op) {
		case RWVC_OP_READ:
			o->value = do_read(m, a); m->st.reads++;
			tr(m, "R %05x %08x", a, o->value);
			break;
		case RWVC_OP_WRITE:
			do_write(m, a, o->value); m->st.writes++;
			tr(m, "W %05x %08x", a, o->value);
			break;
		case RWVC_OP_WRITE_MASK: {
			uint32_t v = (*R(m, a) & ~o->mask) | (o->value & o->mask);
			do_write(m, a, v); m->st.masked++;
			tr(m, "M %05x %08x/%08x", a, o->value, o->mask);
			break; }
		case RWVC_OP_POLL: {
			uint32_t v = do_read(m, a); m->st.polls++;
			tr(m, "P %05x %08x/%08x -> %08x", a, o->value, o->mask, v);
			if ((v & o->mask) != (o->value & o->mask)) rc = -ETIMEDOUT;
			break; }
		default: rc = -EINVAL;
		}
		pthread_mutex_unlock(&m->lock);
		if (rc) break;
	}
	if (done) *done = i;
	return rc;
}

static int f_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct n2vdec_hevc_fake *m = ctx;
	if (irq != RWVC_IRQ_VDEC && irq != RWVC_IRQ_VDEC_LAT)
		return m->inner.wait_irq(m->inner.ctx, irq, timeout_ms, status);
	struct timespec ts; int rc = 0;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000; ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
	pthread_mutex_lock(&m->lock);
	m->st.irq_waits++;
	while (!m->irq_pending[irq] && rc == 0) rc = pthread_cond_timedwait(&m->cv, &m->lock, &ts);
	if (m->irq_pending[irq]) { m->irq_pending[irq]--; rc = 0; } else { rc = -ETIMEDOUT; m->st.irq_timeouts++; }
	if (status) *status = *R(m, irq == RWVC_IRQ_VDEC ? O_MISC + 0xa4 : O_LAT_MISC + 0xa4);
	tr(m, "IRQ wait %u -> %d", irq, rc);
	pthread_mutex_unlock(&m->lock);
	return rc;
}

static int f_irq_clear(void *ctx, unsigned irq)
{
	struct n2vdec_hevc_fake *m = ctx;
	if (irq == RWVC_IRQ_VDEC || irq == RWVC_IRQ_VDEC_LAT) {
		pthread_mutex_lock(&m->lock); m->irq_pending[irq] = 0; pthread_mutex_unlock(&m->lock);
		return 0;
	}
	return m->inner.irq_clear(m->inner.ctx, irq);
}

static int f_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct n2vdec_hevc_fake *m = ctx;
	int rc = m->inner.dma_alloc(m->inner.ctx, bytes, flags, handle, pa);
	if (rc == 0) {
		pthread_mutex_lock(&m->lock);
		for (unsigned i = 0; i < NALLOC; i++)
			if (!m->alloc[i].bytes) { m->alloc[i].handle = *handle; m->alloc[i].pa = *pa; m->alloc[i].bytes = bytes; break; }
		m->st.dma_bytes += bytes;
		if (m->st.dma_bytes > m->st.dma_peak) m->st.dma_peak = m->st.dma_bytes;
		pthread_mutex_unlock(&m->lock);
	}
	return rc;
}
static int f_dma_free(void *ctx, uint32_t handle)
{
	struct n2vdec_hevc_fake *m = ctx;
	pthread_mutex_lock(&m->lock);
	for (unsigned i = 0; i < NALLOC; i++)
		if (m->alloc[i].bytes && m->alloc[i].handle == handle) {
			m->st.dma_bytes -= m->alloc[i].bytes;
			if (m->hit_bytes && m->hit_pa == m->alloc[i].pa) m->hit_bytes = 0;
			memset(&m->alloc[i], 0, sizeof m->alloc[i]);
		}
	pthread_mutex_unlock(&m->lock);
	return m->inner.dma_free(m->inner.ctx, handle);
}
static int f_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct n2vdec_hevc_fake *m = ctx;
	return m->inner.dma_rw(m->inner.ctx, handle, offset, host, bytes, write);
}
static void f_close(void *ctx)
{
	struct n2vdec_hevc_fake *m = ctx;
	if (m->trace) { tr_flush(m); fclose(m->trace); }
	m->inner.close(m->inner.ctx);
	for (int k = 0; k < 2; k++) { free(m->u[k].cb); free(m->u[k].ce); }
	free(m);
}

int n2vdec_hevc_fake_open(struct n2vc_hw *out, const struct n2vc_hw *inner, const char *trace_path,
			  struct n2vdec_hevc_fake **model)
{
	struct n2vdec_hevc_fake *m = calloc(1, sizeof *m);
	if (!m) return -ENOMEM;
	m->inner = *inner;
	pthread_mutex_init(&m->lock, NULL);
	pthread_cond_init(&m->cv, NULL);
	if (trace_path && *trace_path) m->trace = fopen(trace_path, "w");
	memset(out, 0, sizeof *out);
	out->ctx = m; out->power = f_power; out->regs = f_regs; out->wait_irq = f_wait_irq; out->irq_clear = f_irq_clear;
	out->dma_alloc = f_dma_alloc; out->dma_free = f_dma_free; out->dma_rw = f_dma_rw; out->close = f_close;
	if (model) *model = m;
	return 0;
}

void n2vdec_hevc_fake_set_mem(struct n2vdec_hevc_fake *m, struct n2a32_mem *mem) { m->mem = mem; }
void n2vdec_hevc_fake_stats(struct n2vdec_hevc_fake *m, struct n2vdec_hevc_fake_stats *out)
{
	pthread_mutex_lock(&m->lock); *out = m->st; pthread_mutex_unlock(&m->lock);
}
void n2vdec_hevc_fake_mark(struct n2vdec_hevc_fake *m, const char *text)
{
	pthread_mutex_lock(&m->lock);
	if (m->trace) { tr_flush(m); fprintf(m->trace, "MARK %s\n", text); fflush(m->trace); }
	pthread_mutex_unlock(&m->lock);
}
