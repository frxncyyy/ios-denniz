/* SPDX-License-Identifier: MIT */
/* Host test of gpu/hwcodec/kern/bsd/dev/nord2_vcodec.inc against a model of the MT6893 registers and SPM it touches.
 *
 * The model fails the test (violations > 0) on:
 *   - any access to a block's register window, gate controller or LARB while its domain is off, isolated, in reset or bus
 *     protected;
 *   - any write to a LARB port or a bus-protection bit that does not belong to the block being powered;
 *   - any gate/LARB/top-mux/SPM-power write outside the block the test is asking the driver to power;
 *   - any wait that does not end (the model's time advances only through delay() and the fake sleep).
 * Expected values are literals from the Linux sources (see kern/VCODEC_HW.md for the path:line of each).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "redwood_vcodec.h"

typedef uintptr_t vm_offset_t;
#define N2VC_HOST 1
#define N2VC_HOST_SLOTS 1        /* compile the cdev slot table for the close-all test */

static int fails, checks, violations;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static void CHECKMSG(int c, const char *fmt, ...) { checks++; if (!c) { fails++; va_list a; va_start(a, fmt); printf("FAIL "); vprintf(fmt, a); printf("\n"); va_end(a); } }

/* ---- MT6893 model ---------------------------------------------------------------------------------------------- */
enum {
	W_SPM, W_INFRA, W_TOP, W_VCG, W_VCG1, W_L7, W_L8, W_VSGC, W_VGC, W_L4, W_L5, W_VENC, W_VENC1,
	W_VBASE, W_VVLD, W_VMC, W_VMV, W_VMISC, W_VLMISC, W_VLVLD, W_VRACING, W_POOL, W_VALL, W_N
};
static const uint32_t wpa[W_N] = {
	0x10006000u, 0x10001000u, 0x10000000u, 0x17000000u, 0x17800000u, 0x17010000u, 0x17810000u,
	0x1600f000u, 0x1602f000u, 0x1602e000u, 0x1600d000u, 0x17020000u, 0x17820000u,
	0x16000000u, 0x16020000u, 0x16021000u, 0x16023000u, 0x16025000u, 0x16010000u, 0x16011000u, 0x16004000u,
	RW_CLAIM_VCODEC_PA, 0x16000000u
};
static const uint32_t wsz[W_N] = {
	0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u,
	0x3000u, 0x3000u, 0x1000u, 0x1000u, 0x1000u, 0x1000u, 0x4000u, 0x1000u, 0x0400u, 0x1000u,
	RW_CLAIM_VCODEC_BYTES, 0x30000u
};
/* block that owns a window: RWVC_BLK_* or -1 for the SoC top-level blocks */
static const int wblk[W_N] = {
	-1, -1, -1, RWVC_BLK_VENC, RWVC_BLK_VENC_C1, RWVC_BLK_VENC, RWVC_BLK_VENC_C1,
	RWVC_BLK_VDEC_SOC, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_SOC,
	RWVC_BLK_VENC, RWVC_BLK_VENC_C1,
	RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE,
	RWVC_BLK_VDEC_SOC, RWVC_BLK_VDEC_SOC, RWVC_BLK_VDEC_CORE, -1, -2
};

/* RWVC_WIN_VDEC_ALL page -> power block, from facts/VDEC_PROTOCOL.md section 4 and mt6893.dts:6281-6292 (not from the
 * driver): 0x00-0x0c CORE (base/racing/vendor low pages), 0x0d SOC (larb5), 0x0e CORE (larb6), 0x0f-0x1f SOC
 * (SOC_GCON + LAT), 0x20-0x2f CORE (VLD..VDEC_SYS + larb4). */
static unsigned wall_blk(uint32_t off)
{
	uint32_t p = off >> 12;
	if (p <= 0x0cu || p == 0x0eu || p >= 0x20u) { return RWVC_BLK_VDEC_CORE; }
	return RWVC_BLK_VDEC_SOC;
}

static uint8_t *wm[W_N];
static uint64_t now_us;
static uint32_t expect_mask;
static int expect_arm;

struct mdom { uint16_t con; uint8_t sta; uint32_t s1mm, s2mm, s1mm2, s2mm2; bool pwr_dead, sram_stuck; };
static struct mdom md[RWVC_BLK_COUNT] = {
	{ 0x344, 17, 1u << 26, 1u << 27, 1u << 0, 1u << 1, false, false },
	{ 0x348, 18, (1u << 28) | (1u << 30), (1u << 29) | (1u << 31), 0, 0, false, false },
	{ 0x33c, 15, 1u << 24, 1u << 25, 0, 0, false, false },
	{ 0x340, 16, 0, 0, 1u << 6, 1u << 7, false, false },
};
static struct {
	bool dis_off, mux_dead, prot_stuck, prot_set_stuck;
	int larb_stuck;                 /* -1 none, else that port's NON_SEC_CON write is dropped */
	bool irq_armed[RWVC_IRQ_COUNT];
	uint64_t irq_at[RWVC_IRQ_COUNT];
	uint32_t irq_bits[RWVC_IRQ_COUNT];
	bool poll_armed;
	int poll_w; uint32_t poll_off, poll_value;
	uint64_t poll_at;
} MDL;

static void viol(const char *what, unsigned a, unsigned b)
{
	violations++;
	printf("VIOLATION %s (%#x, %#x)\n", what, a, b);
}

static uint32_t mr(int w, uint32_t off) { uint32_t v; memcpy(&v, wm[w] + off, 4); return v; }
static void setr(int w, uint32_t off, uint32_t v) { memcpy(wm[w] + off, &v, 4); }

static uint32_t ostdl_of(int w, unsigned p)
{
	if (w == W_L7) { static const uint8_t t[27] = { 1,4,1,1,1,1,1,1,1,4,4,1,4,1,0xa,6,1,0xa,6,1,1,1,1,5,3,3,4 }; return t[p]; }
	if (w == W_L8) { static const uint8_t t[27] = { 1,4,1,1,1,1,1,1,1,4,4,1,4,1,0xa,6,1,0xa,6,1,1,1,1,5,3,3,4 }; return t[p]; }
	if (w == W_L4) { static const uint8_t t[11] = { 0x28,0x19,0xb,1,1,1,1,1,1,4,1 }; return t[p]; }
	if (w == W_L5) { static const uint8_t t[8] = { 1,1,4,1,1,1,1,0x16 }; return t[p]; }
	return 0;
}

static bool dom_ok(unsigned blk)
{
	const struct mdom *d = &md[blk];
	if (!(mr(W_SPM, 0x16c) & (1u << d->sta)) || !(mr(W_SPM, 0x170) & (1u << d->sta))) { return false; }
	uint32_t con = mr(W_SPM, d->con);
	if ((con & 0x1fu) != 0x0du || (con & 0x1100u)) { return false; }
	if (mr(W_INFRA, 0x2d0) & (d->s1mm | d->s2mm)) { return false; }
	if (mr(W_INFRA, 0xdc8) & (d->s1mm2 | d->s2mm2)) { return false; }
	return true;
}

static void spm_update(void)
{
	for (unsigned b = 0; b < RWVC_BLK_COUNT; b++) {
		const struct mdom *d = &md[b];
		uint32_t con = mr(W_SPM, d->con);
		bool on = (con & 0xcu) == 0xcu && !d->pwr_dead;
		uint32_t sta = 1u << d->sta;
		setr(W_SPM, 0x16c, (mr(W_SPM, 0x16c) & ~sta) | (on ? sta : 0));
		setr(W_SPM, 0x170, (mr(W_SPM, 0x170) & ~sta) | (on ? sta : 0));
		uint32_t ack = d->sram_stuck ? (1u << 12) : ((con & (1u << 8)) ? (1u << 12) : 0);
		setr(W_SPM, d->con, (con & ~(1u << 12)) | ack);
	}
}

static void irq_update(void)
{
	for (unsigned i = 0; i < RWVC_IRQ_COUNT; i++) {
		if (!MDL.irq_armed[i] || now_us < MDL.irq_at[i]) { continue; }
		MDL.irq_armed[i] = false;
		if (i == RWVC_IRQ_VENC || i == RWVC_IRQ_VENC_C1) {
			int w = i == RWVC_IRQ_VENC ? W_VENC : W_VENC1;
			setr(w, 0x5c, mr(w, 0x5c) | MDL.irq_bits[i]);
		} else {
			int w = i == RWVC_IRQ_VDEC ? W_VMISC : W_VLMISC;
			setr(w, 0xa4, mr(w, 0xa4) | MDL.irq_bits[i]);
		}
	}
	if (MDL.poll_armed && now_us >= MDL.poll_at) { MDL.poll_armed = false; setr(MDL.poll_w, MDL.poll_off, MDL.poll_value); }
}

static void model_us(uint32_t us) { now_us += us; irq_update(); }

/* expected-block rule for registers that program a specific block */
static void expect_check(uint32_t blk, const char *what, uint32_t v)
{
	if (expect_arm && !(expect_mask & (1u << blk))) { viol(what, blk, v); }
}

static uint32_t prot_owned(uint32_t v, int mm2)
{
	uint32_t own = 0;
	for (unsigned b = 0; b < RWVC_BLK_COUNT; b++) {
		if (!(expect_mask & (1u << b))) { continue; }
		own |= mm2 ? (md[b].s1mm2 | md[b].s2mm2) : (md[b].s1mm | md[b].s2mm);
	}
	return v & ~own;
}

static void model_write(int w, uint32_t off, uint32_t v)
{
	if (wblk[w] >= 0 && !dom_ok((unsigned)wblk[w])) { viol("access to a block that is not powered", (unsigned)w, off); }
	if (wblk[w] == -2 && !dom_ok(wall_blk(off))) { viol("write of a VDEC_ALL page whose block is off", off >> 12, off); }
	switch (w) {
	case W_SPM:
		if (off == 0x16c || off == 0x170) { setr(w, off, v); return; }
		if (off == 0 || off == 0x350) { setr(w, off, v); if (off == 0x350) { spm_update(); } return; }
		for (unsigned b = 0; b < RWVC_BLK_COUNT; b++) {
			if (off == md[b].con) { expect_check(b, "write to another block's PWR_CON", off); setr(w, off, v); spm_update(); return; }
		}
		setr(w, off, v);
		return;
	case W_INFRA:
		if (off == 0x2d4 || off == 0x2d8 || off == 0xdcc || off == 0xdd0) {
			if (expect_arm) {
				int mm2 = (off == 0xdcc || off == 0xdd0);
				if (prot_owned(v, mm2)) { viol("bus-protection bit of another block", off, v); }
			}
			uint32_t cur = mr(w, off == 0x2d4 ? 0x2d0 : off == 0x2d8 ? 0x2d0 : off == 0xdcc ? 0xdc8 : 0xdc8);
			uint32_t nv;
			bool set = (off == 0x2d4 || off == 0xdcc);
			if (set) { nv = cur | v; }
			else { nv = MDL.prot_stuck ? cur : (cur & ~v); }
			setr(w, set ? (off == 0x2d4 ? 0x2d0 : 0xdc8) : (off == 0x2d8 ? 0x2d0 : 0xdc8), nv);
			if (!(set && MDL.prot_set_stuck)) {
				setr(W_INFRA, 0x2ec, mr(W_INFRA, 0x2d0));
				setr(W_INFRA, 0xdd8, mr(W_INFRA, 0xdc8));
			}
			return;
		}
		setr(w, off, v);
		return;
	case W_VCG:
	case W_VCG1: {
		unsigned blk = w == W_VCG ? RWVC_BLK_VENC : RWVC_BLK_VENC_C1;
		if (off == 0x4 || off == 0x8) { expect_check(blk, "venc gate write", off); }
		if (off == 0x4) { setr(w, 0x0, mr(w, 0x0) | v); }
		else if (off == 0x8) { setr(w, 0x0, mr(w, 0x0) & ~v); }
		else { setr(w, off, v); }
		return;
	}
	case W_VSGC:
	case W_VGC: {
		unsigned blk = w == W_VSGC ? RWVC_BLK_VDEC_SOC : RWVC_BLK_VDEC_CORE;
		if (off == 0x0 || off == 0x200 || off == 0x8 || off == 0x4 || off == 0x204 || off == 0xc) { expect_check(blk, "vdec gate write", off); }
		if (off == 0x0 || off == 0x200 || off == 0x8) { setr(w, off, mr(w, off) | v); }
		else if (off == 0x4) { setr(w, 0x0, mr(w, 0x0) & ~v); }
		else if (off == 0x204) { setr(w, 0x200, mr(w, 0x200) & ~v); }
		else if (off == 0xc) { setr(w, 0x8, mr(w, 0x8) & ~v); }
		else { setr(w, off, v); }
		return;
	}
	case W_L7: case W_L8: case W_L4: case W_L5: {
		unsigned blk = w == W_L7 ? RWVC_BLK_VENC : w == W_L8 ? RWVC_BLK_VENC_C1 : w == W_L4 ? RWVC_BLK_VDEC_CORE : RWVC_BLK_VDEC_SOC;
		unsigned nports = w == W_L4 ? 11 : w == W_L5 ? 8 : 27;
		uint32_t mask = (w == W_L4) ? 0x7ffu : (w == W_L5) ? 0xffu : ((1u << 7) - 1u) | (0x3ffu << 13);
		expect_check(blk, "larb write", off);
		if (off >= 0x200 && off < 0x200 + 4 * nports) {
			unsigned p = (off - 0x200) / 4;
			if (!(mask & (1u << p))) { viol("OSTDL write to another block's larb port", w, p); }
		}
		if (off >= 0x380 && off < 0x380 + 4 * nports) {
			unsigned p = (off - 0x380) / 4;
			if (!(mask & (1u << p))) { viol("NON_SEC_CON write to another block's larb port", w, p); }
			if ((int)p == MDL.larb_stuck) { return; }
		}
		setr(w, off, v);
		return;
	}
	case W_VENC:
	case W_VENC1:
		if (off == 0x60) { setr(w, 0x5c, mr(w, 0x5c) & ~v); setr(w, off, v); return; }
		setr(w, off, v);
		return;
	case W_VMISC:
	case W_VLMISC:
		if (off == 0xa4) {
			uint32_t cur = mr(w, off);
			if (v & 0x10u) { setr(w, off, (cur & ~0x10000u) | (v & 0x1u)); return; }
			setr(w, off, (cur & 0x10000u) | (v & ~0x10000u));
			return;
		}
		setr(w, off, v);
		return;
	case W_TOP:
		if (off == 0xe4 || off == 0xe8) { expect_check(RWVC_BLK_VENC, "venc mux write", off); }
		if (off == 0xf4 || off == 0xf8) { expect_check(RWVC_BLK_VDEC_SOC, "vdec mux write (SOC)", off); }
		if (MDL.mux_dead && (off == 0xe0 || off == 0xe4 || off == 0xe8)) { setr(W_TOP, 0xe0, 0x80000000u); return; }
		if (MDL.mux_dead && (off == 0xf0 || off == 0xf4 || off == 0xf8)) { setr(W_TOP, 0xf0, 0x80000080u); return; }
		if (off == 0xe4 || off == 0xf4) { setr(w, off - 4, mr(w, off - 4) | v); setr(w, off, v); return; }   /* mux SET */
		if (off == 0xe8 || off == 0xf8) { setr(w, off - 8, mr(w, off - 8) & ~v); setr(w, off, v); return; }   /* mux CLR */
		setr(w, off, v);
		return;
	default:
		setr(w, off, v);
		return;
	}
}

static int va_decode(vm_offset_t va, uint32_t off, uint32_t bytes, int *w, uint32_t *woff)
{
	if (va >= 0x1000000000ull && va < 0x1000000000ull + RW_CLAIM_VCODEC_BYTES) {
		*w = W_POOL; *woff = (uint32_t)(va - 0x1000000000ull) + off;
		if (*woff + bytes > wsz[W_POOL]) { viol("pool access out of range", *woff, bytes); return 0; }
		return 1;
	}
	if (va >= 0x80000000ull && va < 0x80000000ull + (uint64_t)W_N * 0x10000ull) {
		unsigned i = (unsigned)((va - 0x80000000ull) / 0x10000ull);
		*woff = (uint32_t)(va - 0x80000000ull - (uint64_t)i * 0x10000ull) + off;
		*w = (int)i;
		if (*woff + bytes > wsz[i]) { viol("window access out of range", i, *woff); return 0; }
		return 1;
	}
	viol("access to an unmapped address", (unsigned)va, off);
	return 0;
}

/* the kernel's MMIO helpers, modelled */
static uint32_t ni_rd(vm_offset_t base, unsigned off)
{
	int w; uint32_t o;
	if (!va_decode(base, off, 4, &w, &o)) { return 0; }
	if (wblk[w] >= 0 && !dom_ok((unsigned)wblk[w])) { viol("read of a block that is not powered", (unsigned)w, o); }
	if (wblk[w] == -2 && !dom_ok(wall_blk(o))) { viol("read of a VDEC_ALL page whose block is off", o >> 12, o); }
	return mr(w, o);
}
static void ni_wr(vm_offset_t base, unsigned off, uint32_t v)
{
	int w; uint32_t o;
	if (!va_decode(base, off, 4, &w, &o)) { return; }
	model_write(w, o, v);
}
static void ni_mask(vm_offset_t base, unsigned off, uint32_t mask, uint32_t v)
{
	ni_wr(base, off, (ni_rd(base, off) & ~mask) | (v & mask));
}

/* shims the driver expects */
void *n2vc_host_map(uint32_t pa, uint32_t bytes)
{
	for (int i = 0; i < W_N; i++) {
		if (wpa[i] == pa && wsz[i] >= bytes) {
			if (i == W_POOL) { return wm[i]; }          /* the driver memsets/copies the pool through this pointer */
			return (void *)(uintptr_t)(0x80000000ull + (uint64_t)i * 0x10000ull);
		}
	}
	return NULL;
}
void n2vc_host_us(uint32_t us) { model_us(us); }
/* deterministic fake clock for mach_absolute_time(); the driver accounts waits with it */
uint64_t n2vc_host_now_us(void) { return now_us; }

/* "during a sleep": advance the fake clock and run at most one armed callback (test_k02 powers a block off or closes
 * the file under a sleeping waiter this way).  The callback is cleared before it runs so the driver's own power
 * sequences (which use delay(), not sleep) cannot recurse into it. */
static void (*n2vc_test_sleep_cb)(void);
void n2vc_host_sleep(uint32_t us)
{
	model_us(us);
	if (n2vc_test_sleep_cb) {
		void (*cb)(void) = n2vc_test_sleep_cb;
		n2vc_test_sleep_cb = NULL;
		cb();
	}
}
int n2vc_host_copyin(uint64_t uaddr, void *kaddr, uint32_t bytes)
{
	if (uaddr < 0x1000ull) { return EFAULT; }
	memcpy(kaddr, (const void *)(uintptr_t)uaddr, bytes);
	return 0;
}
int n2vc_host_copyout(const void *kaddr, uint64_t uaddr, uint32_t bytes)
{
	if (uaddr < 0x1000ull) { return EFAULT; }
	memcpy((void *)(uintptr_t)uaddr, kaddr, bytes);
	return 0;
}

#include "../bsd/dev/nord2_vcodec.inc"

/* ---- test helpers ---------------------------------------------------------------------------------------------- */
struct n2vc_open *F, *G;

static int call(u_long cmd, void *arg, uint32_t expect)
{
	expect_mask = expect; expect_arm = 1;
	int e = n2vc_ioctl(F, cmd, (caddr_t)arg, 0, NULL);
	expect_arm = 0;
	return e;
}
static int callf(struct n2vc_open *f, u_long cmd, void *arg, uint32_t expect)
{
	expect_mask = expect; expect_arm = 1;
	int e = n2vc_ioctl(f, cmd, (caddr_t)arg, 0, NULL);
	expect_arm = 0;
	return e;
}
static int power(unsigned blk, int on, uint32_t expect)
{
	struct rwvc_power p = { blk, on ? 1u : 0u };
	return call(RWVC_POWER, &p, expect);
}
static int powerf(struct n2vc_open *f, unsigned blk, int on, uint32_t expect)
{
	struct rwvc_power p = { blk, on ? 1u : 0u };
	return callf(f, RWVC_POWER, &p, expect);
}
static struct rwvc_info info(void)
{
	struct rwvc_info i;
	memset(&i, 0xa5, sizeof(i));
	int e = call(RWVC_INFO, &i, 0);
	CHECKMSG(e == 0, "INFO returns %d", e);
	return i;
}
static int dma_alloc(struct n2vc_open *f, uint32_t bytes, uint32_t flags, uint32_t *h, uint32_t *pa)
{
	struct rwvc_dma_alloc a = { bytes, flags, 0, 0 };
	int e = callf(f, RWVC_DMA_ALLOC, &a, 0);
	if (!e) { *h = a.out_handle; *pa = a.out_pa; }
	return e;
}
static int dma_rw(struct n2vc_open *f, uint32_t h, uint32_t off, void *buf, uint32_t bytes, int wr)
{
	struct rwvc_dma_rw r = { h, off, (uint64_t)(uintptr_t)buf, bytes, (uint32_t)wr };
	return callf(f, RWVC_DMA_RW, &r, 0);
}
static int dma_fill(struct n2vc_open *f, uint32_t h, uint32_t off, uint32_t bytes, uint32_t val)
{
	struct rwvc_dma_fill fl = { h, off, bytes, val };
	return callf(f, RWVC_DMA_FILL, &fl, 0);
}
static int dma_free(struct n2vc_open *f, uint32_t h)
{
	struct rwvc_dma_free fr = { h, 0 };
	return callf(f, RWVC_DMA_FREE, &fr, 0);
}
static int dma_info(struct n2vc_open *f, uint32_t h, uint32_t *bytes, uint32_t *pa, uint32_t *reserved)
{
	struct rwvc_dma_info di = { h, 0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu };
	int e = callf(f, RWVC_DMA_INFO, &di, 0);
	if (bytes) { *bytes = di.bytes; }
	if (pa) { *pa = di.out_pa; }
	if (reserved) { *reserved = di.reserved; }
	return e;
}
static int wait_irq(unsigned irq, uint32_t ms, struct rwvc_wait_irq *w, uint32_t expect)
{
	memset(w, 0, sizeof(*w));
	w->irq = irq; w->timeout_ms = ms;
	return call(RWVC_WAIT_IRQ, w, expect);
}

/* venc_sel: CLK_CFG_13 bits 27:24 parent, bit 31 gate (clk-mt6885.c:1958-1963); the other fields of the register
 * (aud_2/adsp/dpmaif muxes, bits 0-23) must never change. */
static unsigned venc_parent(void) { return (mr(W_TOP, 0xe0) >> 24) & 0xfu; }
static int venc_gated(void) { return (mr(W_TOP, 0xe0) & (1u << 31)) != 0; }
static unsigned venc_want(void) { return N2VC_VENC_SLOW ? N2VC_VENC_PARENT_SLOW : N2VC_VENC_PARENT_FAST; }

static int ven_get_expect(uint32_t expect) { expect_mask = expect; expect_arm = 1; int e = n2vc_ven_get(); expect_arm = 0; return e; }
static void ven_put_expect(uint32_t expect) { expect_mask = expect; expect_arm = 1; n2vc_ven_put(); expect_arm = 0; }

static void model_init(void)
{
	if (!wm[0]) {
		for (int i = 0; i < W_N; i++) {
			wm[i] = calloc(1, wsz[i]);
			if (!wm[i]) { printf("out of memory for window %d\n", i); exit(2); }
		}
	}
	for (int i = 0; i < W_N; i++) { memset(wm[i], 0, wsz[i]); }
	memset(&MDL, 0, sizeof(MDL));
	MDL.larb_stuck = -1;
	now_us = 0;
	expect_arm = 0; expect_mask = 0;
	setr(W_SPM, 0x16c, (1u << 19) | (1u << 20));       /* MDP + DIS on at boot (runs/q10hw1/00-step0.log) */
	setr(W_SPM, 0x170, (1u << 19) | (1u << 20));
	setr(W_SPM, 0x344, 0x1112);
	setr(W_SPM, 0x348, 0x1112);
	setr(W_SPM, 0x33c, 0x1112);
	setr(W_SPM, 0x340, 0x1112);
	setr(W_INFRA, 0x2d0, (1u << 24) | (1u << 25) | (1u << 26) | (1u << 27) | (1u << 28) | (1u << 29) | (1u << 30) | (1u << 31));
	setr(W_INFRA, 0x2ec, mr(W_INFRA, 0x2d0));
	setr(W_INFRA, 0xdc8, (1u << 0) | (1u << 1) | (1u << 6) | (1u << 7));
	setr(W_INFRA, 0xdd8, mr(W_INFRA, 0xdc8));
	setr(W_TOP, 0xe0, 0x0c030601u);            /* the phone's own venc_sel: gate clear, parent 12 */
	setr(W_TOP, 0xf0, 0);                      /* both vdec muxes gated, parent 0 */
	for (int w = W_L7; w <= W_L5; w++) {
		unsigned n = w == W_L4 ? 11 : w == W_L5 ? 8 : 27;
		for (unsigned p = 0; p < n; p++) { setr(w, 0x380 + 4 * p, 0x5501u); }
	}
	/* the driver's state survives a model reset only through the file table; tests close all files first */
	memset(&n2vc.on, 0, sizeof(n2vc.on));
	memset(&n2vc.refs, 0, sizeof(n2vc.refs));
	memset(&n2vc.hidden, 0, sizeof(n2vc.hidden));
	memset(&n2vc.dep_ref, 0, sizeof(n2vc.dep_ref));
	memset(n2vc.pool_bits, 0, sizeof(n2vc.pool_bits));
	memset(n2vc.dma, 0, sizeof(n2vc.dma));
	memset(n2vc.files, 0, sizeof(n2vc.files));
	memset(n2vc_slot, 0, sizeof(n2vc_slot));
	memset(n2vc.mm, 0, sizeof(n2vc.mm));
	n2vc.nmm = 0;
}


/* one file per test section: close it (and everything it holds) with the expected blocks */
static void close_expect(struct n2vc_open *f, uint32_t expect)
{
	expect_mask = expect; expect_arm = 1;
	n2vc_open_destroy(f);
	expect_arm = 0;
}

/* ---- tests ----------------------------------------------------------------------------------------------------- */
static void test_info_and_args(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	struct rwvc_info i = info();
	CHECK(i.abi_version == RWVC_ABI_VERSION);
	CHECK(i.powered_mask == 0);
	CHECK(i.pool_pa == RW_CLAIM_VCODEC_PA);
	CHECK(i.pool_bytes == RW_CLAIM_VCODEC_BYTES);
	CHECK(i.pool_free == RW_CLAIM_VCODEC_BYTES);
	CHECK(i.irq_mode == 0);
	CHECK(i.server_present == 1);
	CHECK(i.reserved[0] == 0 && i.reserved[7] == 0);

	/* INFO on the client minor answers too, server_present stays about minor 0 */
	G = n2vc_open_create(1);
	CHECK(G != NULL);
	expect_mask = 0; expect_arm = 1;
	struct rwvc_info gi;
	memset(&gi, 0, sizeof(gi));
	CHECK(n2vc_ioctl(G, RWVC_INFO, (caddr_t)&gi, 0, NULL) == 0);
	expect_arm = 0;
	CHECK(gi.abi_version == RWVC_ABI_VERSION && gi.server_present == 1);

	struct rwvc_power p = { RWVC_BLK_COUNT, 1 };
	CHECK(call(RWVC_POWER, &p, 0) == EINVAL);
	p.block = 0; p.on = 0;
	CHECK(call(RWVC_POWER, &p, 0) == EINVAL);
	p.on = 2;                                       /* any non-zero is "on" */
	CHECK(call(RWVC_POWER, &p, 1u << 0) == 0);
	CHECK(power(0, 0, 1u << 0) == 0);
	CHECK(power(0, 0, 0) == EINVAL);                /* no reference left */
	CHECK(n2vc_powered_mask() == 0);
	CHECK(n2vc_minor_opens(0) == 1 && n2vc_minor_opens(1) == 1);
	close_expect(F, 0);
	close_expect(G, 0);
	CHECK(n2vc_minor_opens(0) == 0);
	/* a destroyed file is refused */
	CHECK(n2vc_ioctl(F, RWVC_INFO, (caddr_t)&gi, 0, NULL) == ENXIO);
}

static void test_ven_full(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	setr(W_L7, 0x380 + 4 * 5, 0x15501u);              /* MMSRAM routing bit 16 (Linux smi_sysram_enable) */
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(mr(W_L7, 0x380 + 4 * 5) == 0);               /* bits 0, 8-15 and 16-19 all cleared on a VENC larb */
	CHECK(dom_ok(RWVC_BLK_VENC));
	CHECK(n2vc_powered_mask() == (1u << 0));
	/* RWVC_POWER (vcodecd) selects N2VC_VENC_SLOW's parent even over the phone's running 624 MHz setting: root's
	 * 273 MHz decision for the first phone encode. 4 = mainpll_d4_d2, 12 = univpll_d4 (clk-mt6885.c:1221-1237). */
	CHECK(venc_parent() == venc_want() && !venc_gated());
	CHECK((mr(W_TOP, 0xe0) & 0x00ffffffu) == 0x00030601u);      /* the other CLK_CFG_13 muxes are untouched */
	CHECK((mr(W_VCG, 0x0) & 0x10000011u) == 0x10000011u && !(mr(W_VCG, 0x0) & 0x100u));   /* larb, venc, GALS + pg.c's bit 2 */
	CHECK(mr(W_L7, 0x24) == 0x300256u && mr(W_L7, 0x40) == 1u && mr(W_L7, 0x524) == 0x300256u);
	for (unsigned p = 0; p < 27; p++) {
		if ((N2VC_PORTS_VENC & (1u << p)) != 0) {
			CHECKMSG(mr(W_L7, 0x380 + 4 * p) == 0, "port %u non-sec %#x", p, mr(W_L7, 0x380 + 4 * p));
			CHECKMSG(mr(W_L7, 0x200 + 4 * p) == ostdl_of(W_L7, p), "port %u ostdl %#x", p, mr(W_L7, 0x200 + 4 * p));
		} else {
			CHECKMSG(mr(W_L7, 0x380 + 4 * p) == 0x5501u, "JPEG port %u was touched (%#x)", p, mr(W_L7, 0x380 + 4 * p));
			CHECKMSG(mr(W_L7, 0x200 + 4 * p) == 0, "JPEG port %u OSTDL written", p);
		}
	}
	CHECK(mr(W_L8, 0x24) == 0 && mr(W_L4, 0x24) == 0 && mr(W_L5, 0x24) == 0);
	/* second reference: no register access at all (expect mask empty) */
	CHECK(power(0, 1, 0) == 0);
	CHECK(power(0, 0, 0) == 0);
	CHECK(dom_ok(RWVC_BLK_VENC));
	CHECK(power(0, 0, 1u << 0) == 0);
	CHECK(!dom_ok(RWVC_BLK_VENC));
	CHECK((mr(W_VCG, 0x0) & 0x10000011u) == 0 && !(mr(W_VCG, 0x0) & 0x100u));
	CHECK(mr(W_SPM, 0x344) == 0x1112u);
	CHECK((mr(W_INFRA, 0x2d0) & ((1u << 26) | (1u << 27))) == ((1u << 26) | (1u << 27)));
	CHECK((mr(W_INFRA, 0xdc8) & 3u) == 3u);
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);
	/* power on again from a fresh file: the domain is off and protected, the full sequence runs */
	F = n2vc_open_create(0);
	CHECK(power(0, 1, 1u << 0) == 0 && dom_ok(RWVC_BLK_VENC));
	close_expect(F, 1u << 0);
	CHECK(!dom_ok(RWVC_BLK_VENC));
	/* the autotest's 3-argument form (macro, venc_parent 0): never re-mux a running venc_sel */
	model_init();
	F = n2vc_open_create(0);
	expect_mask = 1u << 0; expect_arm = 1;
	CHECK(n2vc_acquire_locked(RWVC_BLK_VENC, F, false) == 0);
	expect_arm = 0;
	CHECK(venc_parent() == N2VC_VENC_PARENT_FAST && !venc_gated());    /* 0x0c030601 kept as the phone boots */
	close_expect(F, 1u << 0);
	CHECK(!dom_ok(RWVC_BLK_VENC));
	/* and with the mux gated: the vcodec path picks the knob's parent */
	model_init();
	F = n2vc_open_create(0);
	setr(W_TOP, 0xe0, 0x80000000u);                    /* gate set, parent 0 */
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(venc_parent() == venc_want() && !venc_gated());
	close_expect(F, 1u << 0);
	/* the JPEG driver's shared reference (n2vc_ven_get) never rewrites a running parent, and picks 624 MHz when gated */
	model_init();
	F = n2vc_open_create(0);
	CHECK(ven_get_expect(1u << 0) == 0);
	CHECK(mr(W_TOP, 0xe0) == 0x0c030601u);
	ven_put_expect(1u << 0);
	model_init();
	F = n2vc_open_create(0);
	setr(W_TOP, 0xe0, 0x80000000u);
	CHECK(ven_get_expect(1u << 0) == 0);
	CHECK(mr(W_TOP, 0xe0) == (N2VC_VENC_PARENT_FAST << 24));
	ven_put_expect(1u << 0);
	/* a vcodec job leaves the knob's parent; the next JPEG job restores its own 624 MHz parent while VEN is off */
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(venc_parent() == venc_want() && !venc_gated());
	CHECK(power(0, 0, 1u << 0) == 0);
	CHECK(ven_get_expect(1u << 0) == 0);
	CHECK(venc_parent() == N2VC_VENC_PARENT_FAST && !venc_gated());
	CHECK((mr(W_TOP, 0xe0) & 0x00ffffffu) == 0x00030601u);
	ven_put_expect(1u << 0);
	close_expect(F, 0);
	/* VEN held by the JPEG driver: a vcodec power does not touch the running mux (first encode then runs at 624 MHz) */
	model_init();
	F = n2vc_open_create(0);
	CHECK(ven_get_expect(1u << 0) == 0);
	CHECK(power(0, 1, 0) == 0);                        /* VEN already on: no register access */
	CHECK(mr(W_TOP, 0xe0) == 0x0c030601u);
	CHECK(power(0, 0, 0) == 0);
	ven_put_expect(1u << 0);
	CHECK(power(0, 1, 1u << 0) == 0);                  /* now VEN is off: the knob applies */
	CHECK(venc_parent() == venc_want() && !venc_gated());
	close_expect(F, 1u << 0);
	/* vdec muxes: both are programmed when gated */
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VDEC_CORE, 1, (1u << 2) | (1u << 3)) == 0);
	CHECK(mr(W_TOP, 0xf0) == ((14u << 0) | (14u << 8)));
	close_expect(F, (1u << 2) | (1u << 3));
}

static void test_c1_and_vdec(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC_C1, 1, (1u << 0) | (1u << 1)) == 0);
	CHECK(dom_ok(RWVC_BLK_VENC) && dom_ok(RWVC_BLK_VENC_C1));   /* VEN is the parent and comes up too */
	CHECK((mr(W_VCG, 0x0) & 0x10000011u) == 0x10000011u && !(mr(W_VCG, 0x0) & 0x100u));
	CHECK((mr(W_VCG1, 0x0) & 0x10000011u) == 0x10000011u && !(mr(W_VCG1, 0x0) & 0x100u));
	CHECK(mr(W_L8, 0x24) == 0x300256u);
	for (unsigned p = 0; p < 27; p++) {
		if ((N2VC_PORTS_VENC & (1u << p)) != 0) { CHECK(mr(W_L8, 0x380 + 4 * p) == 0); }
		else { CHECKMSG(mr(W_L8, 0x380 + 4 * p) == 0x5501u, "larb8 JPEG port %u touched", p); }
	}
	/* VEN was only held by the hidden reference: dropping C1 drops VEN too */
	CHECK(power(RWVC_BLK_VENC_C1, 0, (1u << 0) | (1u << 1)) == 0);
	CHECK(!dom_ok(RWVC_BLK_VENC_C1) && !dom_ok(RWVC_BLK_VENC));
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);

	model_init();
	F = n2vc_open_create(0);
	setr(W_L4, 0x380, 0x15501u);                       /* VDEC larbs are not VENC's: bits 16-19 stay as they are */
	CHECK(power(RWVC_BLK_VDEC_CORE, 1, (1u << 2) | (1u << 3)) == 0);
	CHECK(mr(W_L4, 0x380) == 0x10000u);
	CHECK(dom_ok(RWVC_BLK_VDEC_CORE) && dom_ok(RWVC_BLK_VDEC_SOC));
	CHECK((mr(W_VGC, 0x0) & 1u) == 1u && (mr(W_VGC, 0x200) & 1u) == 1u && (mr(W_VGC, 0x8) & 1u) == 1u);
	CHECK((mr(W_VSGC, 0x0) & 1u) == 1u && (mr(W_VSGC, 0x200) & 1u) == 1u && (mr(W_VSGC, 0x8) & 1u) == 1u);
	CHECK(mr(W_L4, 0x24) == 0x300256u && mr(W_L4, 0x40) == 1u);
	CHECK(mr(W_L5, 0x24) == 0x300256u && mr(W_L5, 0x40) == 1u);
	for (unsigned p = 0; p < 11; p++) {
		uint32_t want = p == 0 ? 0x10000u : 0;      /* port 0 was preset with the VENC-only MMSRAM bit */
		CHECKMSG(mr(W_L4, 0x380 + 4 * p) == want, "larb4 port %u non-sec %#x", p, mr(W_L4, 0x380 + 4 * p));
		CHECKMSG(mr(W_L4, 0x200 + 4 * p) == ostdl_of(W_L4, p), "larb4 port %u ostdl", p);
	}
	for (unsigned p = 0; p < 8; p++) {
		CHECKMSG(mr(W_L5, 0x380 + 4 * p) == 0, "larb5 port %u non-sec %#x", p, mr(W_L5, 0x380 + 4 * p));
		CHECKMSG(mr(W_L5, 0x200 + 4 * p) == ostdl_of(W_L5, p), "larb5 port %u ostdl", p);
	}
	/* a second VDEC_CORE reference must not take a second hidden reference on SOC */
	CHECK(power(RWVC_BLK_VDEC_CORE, 1, 0) == 0);
	CHECK(power(RWVC_BLK_VDEC_CORE, 0, 0) == 0);
	CHECK(dom_ok(RWVC_BLK_VDEC_CORE) && dom_ok(RWVC_BLK_VDEC_SOC));
	CHECK(power(RWVC_BLK_VDEC_CORE, 0, (1u << 2) | (1u << 3)) == 0);
	CHECK(!dom_ok(RWVC_BLK_VDEC_CORE) && !dom_ok(RWVC_BLK_VDEC_SOC));
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);

	/* VDEC_SOC alone: only its own domain and gates */
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VDEC_SOC, 1, 1u << 2) == 0);
	CHECK(dom_ok(RWVC_BLK_VDEC_SOC) && !dom_ok(RWVC_BLK_VDEC_CORE));
	CHECK((mr(W_VSGC, 0x0) & 1u) == 1u && (mr(W_VSGC, 0x200) & 1u) == 1u);
	CHECK(mr(W_VGC, 0x0) == 0);
	CHECK(mr(W_L5, 0x380) == 0 && mr(W_L4, 0x380) == 0x5501u);
	close_expect(F, 1u << 2);
}

static void test_faults(void)
{
	int e;
	/* display domain off */
	model_init();
	F = n2vc_open_create(0);
	setr(W_SPM, 0x16c, 0); setr(W_SPM, 0x170, 0);
	e = power(0, 1, 0);
	CHECK(e == EACCES);
	CHECK(mr(W_TOP, 0xe0) == 0x0c030601u && mr(W_SPM, 0x344) == 0x1112u && (mr(W_VCG, 0) & 0x10000011u) == 0);
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);

	/* power request never acknowledged */
	model_init();
	F = n2vc_open_create(0);
	md[0].pwr_dead = true;
	e = power(0, 1, 1u << 0);
	CHECK(e == ETIMEDOUT);
	CHECK(mr(W_SPM, 0x344) == 0x1112u);              /* PWR_ON/2ND withdrawn */
	CHECK((mr(W_VCG, 0) & 0x10000011u) == 0 && !dom_ok(0));
	CHECK(n2vc_powered_mask() == 0);
	md[0].pwr_dead = false;
	close_expect(F, 0);

	/* SRAM power-down ack stuck */
	model_init();
	F = n2vc_open_create(0);
	md[0].sram_stuck = true;
	e = power(0, 1, 1u << 0);
	CHECK(e == ETIMEDOUT);
	CHECK((mr(W_INFRA, 0x2d0) & ((1u << 26) | (1u << 27))) == ((1u << 26) | (1u << 27)));
	CHECK((mr(W_VCG, 0) & 0x10000011u) == 0);
	md[0].sram_stuck = false;
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(dom_ok(0));
	close_expect(F, 1u << 0);

	/* protection release stuck: power-on fails EIO, and recovers when the ack works again */
	model_init();
	F = n2vc_open_create(0);
	MDL.prot_stuck = true;
	e = power(0, 1, 1u << 0);
	CHECK(e == EIO);
	CHECK(!dom_ok(0) && n2vc_powered_mask() == 0);
	MDL.prot_stuck = false;
	CHECK(power(0, 1, 1u << 0) == 0 && dom_ok(0));
	/* protection ack stuck on power-off: ETIMEDOUT, domain left powered, block marked unusable */
	MDL.prot_set_stuck = true;
	e = power(0, 0, 1u << 0);
	CHECK(e == ETIMEDOUT);
	CHECK(mr(W_SPM, 0x344) & (1u << 2));             /* still powered */
	CHECK(n2vc_powered_mask() == 0);
	MDL.prot_set_stuck = false;
	CHECK(power(0, 1, 1u << 0) == 0 && dom_ok(0));
	close_expect(F, 1u << 0);
	CHECK(!dom_ok(0));

	/* mux that refuses to take the parent */
	model_init();
	F = n2vc_open_create(0);
	setr(W_TOP, 0xe0, 0x80000000u);
	MDL.mux_dead = true;
	e = power(0, 1, 1u << 0);
	CHECK(e == EIO);
	CHECK(mr(W_SPM, 0x344) == 0x1112u && (mr(W_VCG, 0) & 0x10000011u) == 0);
	close_expect(F, 0);

	/* LARB write that does not stick */
	model_init();
	F = n2vc_open_create(0);
	MDL.larb_stuck = 3;
	e = power(0, 1, 1u << 0);
	CHECK(e == EIO);
	CHECK(!dom_ok(0));
	CHECK(mr(W_SPM, 0x344) == 0x1112u);              /* the failed sequence was unwound */
	CHECK((mr(W_VCG, 0) & 0x10000011u) == 0);
	MDL.larb_stuck = -1;
	close_expect(F, 0);

	/* MDP domain off: VENC_C1 (larb8) and VDEC_CORE (larb4) hang off the MDP SMI common, so they are refused;
	 * VENC (larb7) and VDEC_SOC (larb5) are on the display common and still work */
	model_init();
	F = n2vc_open_create(0);
	setr(W_SPM, 0x16c, 1u << 20); setr(W_SPM, 0x170, 1u << 20);
	e = power(RWVC_BLK_VENC_C1, 1, 1u << 0);           /* its hidden VEN parent comes up and is unwound */
	CHECK(e == EACCES);
	CHECK(n2vc_powered_mask() == 0);
	e = power(RWVC_BLK_VDEC_CORE, 1, 1u << 2);
	CHECK(e == EACCES);
	CHECK(n2vc_powered_mask() == 0);
	CHECK(power(0, 1, 1u << 0) == 0 && dom_ok(RWVC_BLK_VENC));
	CHECK(power(0, 0, 1u << 0) == 0);
	CHECK(power(RWVC_BLK_VDEC_SOC, 1, 1u << 2) == 0 && dom_ok(RWVC_BLK_VDEC_SOC));
	CHECK(power(RWVC_BLK_VDEC_SOC, 0, 1u << 2) == 0);
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);

	/* gated SMI clock roots (CLK_CFG_1): disp_sel bit 7 refuses larb7/larb5 blocks, mdp_sel bit 15 the MDP ones */
	model_init();
	F = n2vc_open_create(0);
	setr(W_TOP, 0x020, 1u << 7);
	e = power(0, 1, 0);
	CHECK(e == EACCES);
	e = power(RWVC_BLK_VDEC_SOC, 1, 0);
	CHECK(e == EACCES);
	setr(W_TOP, 0x020, 1u << 15);
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(power(0, 0, 1u << 0) == 0);
	e = power(RWVC_BLK_VENC_C1, 1, 1u << 0);
	CHECK(e == EACCES);
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);
}

static void test_regs(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(0, 1, 1u << 0) == 0);

	struct rwvc_reg_op ops[8];
	memset(ops, 0, sizeof(ops));
	uint32_t n = 0;
	ops[n].op = RWVC_OP_WRITE; ops[n].window = RWVC_WIN_VENC; ops[n].offset = 0x100; ops[n].value = 0xdeadbeefu; n++;
	ops[n].op = RWVC_OP_READ;  ops[n].window = RWVC_WIN_VENC; ops[n].offset = 0x100; n++;
	ops[n].op = RWVC_OP_WRITE_MASK; ops[n].window = RWVC_WIN_VENC; ops[n].offset = 0x100; ops[n].mask = 0xffff0000u; ops[n].value = 0x12340000u; n++;
	ops[n].op = RWVC_OP_READ;  ops[n].window = RWVC_WIN_VENC; ops[n].offset = 0x100; n++;
	ops[n].op = RWVC_OP_DELAY_US; ops[n].value = 100; n++;
	ops[n].op = RWVC_OP_POLL; ops[n].window = RWVC_WIN_VENC; ops[n].offset = 0x104; ops[n].value = 1; ops[n].mask = 1; n++;
	ops[n].op = RWVC_OP_READ; ops[n].window = RWVC_WIN_VENC; ops[n].offset = 0x104; n++;
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)ops, n, 0, 500u, 0 };
	MDL.poll_armed = true; MDL.poll_w = W_VENC; MDL.poll_off = 0x104; MDL.poll_at = now_us + 300; MDL.poll_value = 0x1;
	CHECK(call(RWVC_REGS, &b, 0) == 0);
	CHECK(b.out_done == n);
	CHECK(ops[1].value == 0xdeadbeefu);
	CHECK(ops[3].value == 0x1234beefu);
	CHECK(ops[6].value == 1u);
	CHECK(mr(W_VENC, 0x100) == 0x1234beefu);

	/* poll timeout, out_done stops at the failing op */
	memset(ops, 0, sizeof(ops));
	ops[0].op = RWVC_OP_WRITE; ops[0].window = RWVC_WIN_VENC; ops[0].offset = 0x108; ops[0].value = 0;
	ops[1].op = RWVC_OP_POLL; ops[1].window = RWVC_WIN_VENC; ops[1].offset = 0x108; ops[1].value = 4; ops[1].mask = 4;
	ops[2].op = RWVC_OP_WRITE; ops[2].window = RWVC_WIN_VENC; ops[2].offset = 0x10c; ops[2].value = 9;
	b.count = 3; b.poll_timeout_us = 200; b.out_done = 0;
	CHECK(call(RWVC_REGS, &b, 0) == ETIMEDOUT);
	CHECK(b.out_done == 1);
	CHECK(mr(W_VENC, 0x10c) == 0);

	/* op validation */
	memset(ops, 0, sizeof(ops));
	b.ops = (uint64_t)(uintptr_t)ops; b.count = 1; b.poll_timeout_us = 100;
	ops[0].op = 9; CHECK(call(RWVC_REGS, &b, 0) == EINVAL);
	ops[0].op = RWVC_OP_READ; ops[0].window = 99; CHECK(call(RWVC_REGS, &b, 0) == EINVAL);
	ops[0].window = RWVC_WIN_VENC; ops[0].offset = 2; CHECK(call(RWVC_REGS, &b, 0) == EINVAL);
	ops[0].offset = 0x2ffe; CHECK(call(RWVC_REGS, &b, 0) == EINVAL);
	ops[0].offset = 0x2ffc; CHECK(call(RWVC_REGS, &b, 0) == 0);      /* vpud's 0x3000 mmap: registers above 0x2000 */
	ops[0].offset = 0x100; ops[0].op = RWVC_OP_DELAY_US; ops[0].value = 1001; CHECK(call(RWVC_REGS, &b, 0) == EINVAL);
	/* EACCES: window of a block that is not powered */
	ops[0].op = RWVC_OP_READ; ops[0].window = RWVC_WIN_VDEC_BASE; ops[0].offset = 0; ops[0].value = 0;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	/* bad count / null ops / bad user pointer */
	b.count = RWVC_REG_MAX_OPS + 1; CHECK(call(RWVC_REGS, &b, 0) == EINVAL);
	b.count = 1; b.ops = 0; CHECK(call(RWVC_REGS, &b, 0) == EINVAL);
	b.ops = 0x10; CHECK(call(RWVC_REGS, &b, 0) == EFAULT);
	b.ops = (uint64_t)(uintptr_t)ops; b.count = 0; CHECK(call(RWVC_REGS, &b, 0) == 0 && b.out_done == 0);

	/* the batch budget: 2048 x 1 ms of delay in ONE batch stops at 2 s with ETIMEDOUT */
	struct rwvc_reg_op *big = calloc(RWVC_REG_MAX_OPS, sizeof(struct rwvc_reg_op));
	CHECK(big != NULL);
	for (unsigned i = 0; i < RWVC_REG_MAX_OPS; i++) { big[i].op = RWVC_OP_DELAY_US; big[i].value = 1000; }
	b.ops = (uint64_t)(uintptr_t)big; b.count = RWVC_REG_MAX_OPS; b.out_done = 0;
	CHECK(call(RWVC_REGS, &b, 0) == ETIMEDOUT);
	CHECK(b.out_done == 2000);
	free(big);
	b.count = 0; b.ops = (uint64_t)(uintptr_t)ops;

	/* reads/writes of every VDEC window while the core is on, and of VENC_C1 while only VENC is on (EACCES) */
	CHECK(power(RWVC_BLK_VDEC_CORE, 1, (1u << 2) | (1u << 3)) == 0);
	for (unsigned win = RWVC_WIN_VDEC_SYS; win <= RWVC_WIN_VDEC_RACING; win++) {
		ops[0].op = RWVC_OP_WRITE; ops[0].window = (uint8_t)win; ops[0].offset = 0x100; ops[0].value = 0x55aa0000u + win;
		ops[1].op = RWVC_OP_READ;  ops[1].window = (uint8_t)win; ops[1].offset = 0x100;
		b.count = 2; b.out_done = 0; b.poll_timeout_us = 100;
		CHECKMSG(call(RWVC_REGS, &b, 0) == 0, "window %u batch", win);
		CHECKMSG(ops[1].value == 0x55aa0000u + win, "window %u read back %#x", win, ops[1].value);
	}
	ops[0].op = RWVC_OP_READ; ops[0].window = RWVC_WIN_VENC_C1; ops[0].offset = 0x100;
	b.count = 1; CHECK(call(RWVC_REGS, &b, 0) == EACCES);     /* VENC core 1 not powered */
	CHECK(power(RWVC_BLK_VDEC_SOC, 1, 0) == 0);
	ops[0].window = RWVC_WIN_VDEC_LAT_VLD; CHECK(call(RWVC_REGS, &b, 0) == 0);
	close_expect(F, (1u << 0) | (1u << 2) | (1u << 3));
	CHECK(n2vc_powered_mask() == 0);
}

static void test_dma(void)
{
	model_init();
	F = n2vc_open_create(0);
	uint32_t h = 0, pa = 0, h2 = 0, pa2 = 0;
	CHECK(dma_alloc(F, 0, 0, &h, &pa) == EINVAL);
	CHECK(dma_alloc(F, 4096, 2, &h, &pa) == EINVAL);
	CHECK(dma_alloc(F, 8192, 0, &h, &pa) == 0);
	CHECK((pa & 0xfffu) == 0 && pa >= RW_CLAIM_VCODEC_PA && pa + 8192 <= RW_CLAIM_VCODEC_PA + RW_CLAIM_VCODEC_BYTES);
	CHECK(h != 0);
	struct rwvc_info i = info();
	CHECK(i.pool_free == RW_CLAIM_VCODEC_BYTES - 8192);

	uint8_t pat[256], back[256];
	for (unsigned k = 0; k < sizeof(pat); k++) { pat[k] = (uint8_t)(k * 7 + 3); }
	memset(back, 0, sizeof(back));
	CHECK(dma_rw(F, h, 100, pat, sizeof(pat), 1) == 0);
	CHECK(dma_rw(F, h, 100, back, sizeof(back), 0) == 0);
	CHECK(memcmp(pat, back, sizeof(pat)) == 0);
	CHECK(dma_rw(F, h, 8000, pat, 512, 1) == EINVAL);          /* beyond the buffer */
	CHECK(dma_rw(F, h, 0, NULL, 16, 1) == EFAULT);
	CHECK(dma_fill(F, h, 0, 8192, 0x5a) == 0);
	memset(back, 0, sizeof(back));
	CHECK(dma_rw(F, h, 4096, back, sizeof(back), 0) == 0);
	for (unsigned k = 0; k < sizeof(back); k++) { CHECKMSG(back[k] == 0x5a, "fill byte %u = %#x", k, back[k]); }
	CHECK(dma_fill(F, h, 8192, 1, 0) == EINVAL);

	/* ownership and sharing */
	G = n2vc_open_create(1);
	CHECK(G != NULL);
	CHECK(dma_rw(G, h, 0, back, 16, 0) == EACCES);              /* not shared, not G's */
	CHECK(dma_free(G, h) == EACCES);                            /* only the owner frees */
	uint32_t hg = 0, pg = 0;
	CHECK(dma_alloc(G, 4096, 0, &hg, &pg) == 0);
	CHECK(dma_rw(G, hg, 0, pat, 16, 1) == 0);
	CHECK(dma_alloc(F, 4096, 1, &h2, &pa2) == 0);              /* shared */
	uint32_t hs = h2;
	CHECK(dma_rw(G, hs, 0, back, 16, 0) == 0);
	CHECK(dma_free(G, hs) == EACCES);
	/* the client minor cannot use the hardware ioctls */
	struct rwvc_power p = { 0, 1 };
	CHECK(callf(G, RWVC_POWER, &p, 0) == EACCES);
	struct rwvc_reg_batch b = { 0, 0, 0, 0, 0 };
	CHECK(callf(G, RWVC_REGS, &b, 0) == EACCES);
	struct rwvc_dma_fill fl = { h, 0, 0, 0 };
	CHECK(callf(G, RWVC_DMA_FILL, &fl, 0) == EACCES);
	CHECK(dma_free(F, hs) == 0);
	CHECK(dma_free(F, h) == 0);
	CHECK(dma_free(G, hg) == 0);
	CHECK(dma_rw(F, h, 0, back, 16, 0) == EINVAL);            /* stale handle */
	uint32_t junk = 0xffffffffu;
	struct rwvc_dma_free fr = { junk, 0 };
	CHECK(callf(F, RWVC_DMA_FREE, &fr, 0) == EINVAL);
	CHECK(info().pool_free == RW_CLAIM_VCODEC_BYTES);
	close_expect(F, 0);
	close_expect(G, 0);

	/* property test: 10000 random alloc/free, no overlap, all memory reused */
	model_init();
	F = n2vc_open_create(0);
	struct { uint32_t h, pa, bytes; } live[256];
	unsigned nlive = 0;
	uint32_t rng = 12345;
	unsigned ops_done = 0;
	for (unsigned it = 0; it < 10000; it++) {
		rng = rng * 1103515245u + 12345u;
		unsigned act = (rng >> 16) % 100;
		if ((nlive < 200 && act < 55) || nlive == 0) {
			uint32_t bytes = 4096u * (1u + ((rng >> 8) % 64u));          /* 4 KiB .. 256 KiB */
			uint32_t hh, pp;
			if (dma_alloc(F, bytes, (rng & 1) ? 1u : 0u, &hh, &pp) != 0) {
				CHECKMSG(1, "alloc refused at %u (fragmentation is allowed)", it);
				if (nlive) { unsigned k = (rng >> 7) % nlive; CHECK(dma_free(F, live[k].h) == 0); live[k] = live[--nlive]; }
				ops_done++;
				continue;
			}
			/* zero-filled by the allocator */
			uint32_t zero = 0, k;
			for (k = 0; k < 4; k++) { memcpy(&zero, wm[W_POOL] + (pp - RW_CLAIM_VCODEC_PA) + k * 4, 4); if (zero) { break; } }
			CHECKMSG(zero == 0, "allocation %u not zero-filled", it);
			for (unsigned j = 0; j < nlive; j++) {
				CHECKMSG(pp + bytes <= live[j].pa || live[j].pa + live[j].bytes <= pp, "overlap at %u", it);
			}
			/* make the pages dirty so the next zero-fill is a real check */
			memset(wm[W_POOL] + (pp - RW_CLAIM_VCODEC_PA), 0xa5, 16);
			live[nlive].h = hh; live[nlive].pa = pp; live[nlive].bytes = bytes; nlive++;
		} else {
			unsigned k = (rng >> 7) % nlive;
			CHECK(dma_free(F, live[k].h) == 0);
			live[k] = live[--nlive];
		}
		ops_done++;
	}
	CHECK(ops_done == 10000);
	for (unsigned j = 0; j < nlive; j++) { CHECK(dma_free(F, live[j].h) == 0); }
	CHECK(info().pool_free == RW_CLAIM_VCODEC_BYTES);
	/* full reuse: one allocation of the whole pool, then a second one fails */
	uint32_t hh, pp;
	CHECK(dma_alloc(F, RW_CLAIM_VCODEC_BYTES, 0, &hh, &pp) == 0);
	CHECK(pp == RW_CLAIM_VCODEC_PA);
	CHECK(info().pool_free == 0);
	uint32_t h3, p3;
	CHECK(dma_alloc(F, 4096, 0, &h3, &p3) == ENOMEM);
	CHECK(dma_free(F, hh) == 0);
	CHECK(info().pool_free == RW_CLAIM_VCODEC_BYTES);
	close_expect(F, 0);

	/* close with outstanding buffers and power references */
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(dma_alloc(F, 1u << 20, 0, &hh, &pp) == 0);
	CHECK(dma_alloc(F, 1u << 20, 0, &h3, &p3) == 0);
	CHECK(info().pool_free == RW_CLAIM_VCODEC_BYTES - 2u * (1u << 20));
	close_expect(F, 1u << 0);
	CHECK(n2vc_powered_mask() == 0);
	F = n2vc_open_create(0);
	CHECK(info().pool_free == RW_CLAIM_VCODEC_BYTES);
	CHECK(power(0, 1, 1u << 0) == 0);
	close_expect(F, 1u << 0);
}

static void test_irq(void)
{
	model_init();
	F = n2vc_open_create(0);
	struct rwvc_wait_irq w;
	CHECK(wait_irq(RWVC_IRQ_VENC, 10, &w, 0) == EACCES);       /* not powered */
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(wait_irq(RWVC_IRQ_COUNT, 10, &w, 0) == EINVAL);
	CHECK(wait_irq(RWVC_IRQ_VENC, 5001, &w, 0) == EINVAL);
	/* no interrupt: bounded timeout with the last status */
	CHECK(wait_irq(RWVC_IRQ_VENC, 2, &w, 0) == ETIMEDOUT);
	CHECK(w.out_count == 0 && w.out_status == 0);
	/* a frame-done interrupt; status is cleared as the Linux ISR does (ack 0x60) */
	MDL.irq_armed[RWVC_IRQ_VENC] = true; MDL.irq_at[RWVC_IRQ_VENC] = now_us + 500; MDL.irq_bits[RWVC_IRQ_VENC] = 0x4;
	CHECK(wait_irq(RWVC_IRQ_VENC, 100, &w, 0) == 0);
	CHECK(w.out_count == 1 && w.out_status == 0x4);
	CHECK(mr(W_VENC, 0x5c) == 0);                              /* write-1-to-clear */
	CHECK((mr(W_VENC, 0x60) & 0x4) == 0x4);                    /* the ack register saw the bit */
	/* a second wait sees one more */
	MDL.irq_armed[RWVC_IRQ_VENC] = true; MDL.irq_at[RWVC_IRQ_VENC] = now_us + 300; MDL.irq_bits[RWVC_IRQ_VENC] = 0x1;
	CHECK(wait_irq(RWVC_IRQ_VENC, 100, &w, 0) == 0 && w.out_count == 1 && w.out_status == 0x1);
	/* IRQ_CLEAR forgets a pending status and the count */
	MDL.irq_armed[RWVC_IRQ_VENC] = true; MDL.irq_at[RWVC_IRQ_VENC] = now_us + 100; MDL.irq_bits[RWVC_IRQ_VENC] = 0x4;
	n2vc_host_us(200);
	CHECK(mr(W_VENC, 0x5c) == 0x4);
	struct rwvc_irq_clear c = { RWVC_IRQ_VENC, 0 };
	CHECK(call(RWVC_IRQ_CLEAR, &c, 0) == 0);
	CHECK(mr(W_VENC, 0x5c) == 0);
	CHECK(call(RWVC_IRQ_CLEAR, &(struct rwvc_irq_clear){ RWVC_IRQ_COUNT, 0 }, 0) == EINVAL);

	/* VENC core 1 line, same layout at its own window */
	CHECK(power(RWVC_BLK_VENC_C1, 1, (1u << 0) | (1u << 1)) == 0);
	MDL.irq_armed[RWVC_IRQ_VENC_C1] = true; MDL.irq_at[RWVC_IRQ_VENC_C1] = now_us + 200; MDL.irq_bits[RWVC_IRQ_VENC_C1] = 0x4;
	CHECK(wait_irq(RWVC_IRQ_VENC_C1, 100, &w, 0) == 0 && w.out_status == 0x4);
	CHECK(mr(W_VENC1, 0x5c) == 0 && mr(W_VENC, 0x5c) == 0);

	/* VDEC: only DEC_SUCCESS (bit 16) is a completion; other bits are left set and time out */
	CHECK(power(RWVC_BLK_VDEC_CORE, 1, (1u << 2) | (1u << 3)) == 0);
	MDL.irq_armed[RWVC_IRQ_VDEC] = true; MDL.irq_at[RWVC_IRQ_VDEC] = now_us + 100; MDL.irq_bits[RWVC_IRQ_VDEC] = 0x20000;
	CHECK(wait_irq(RWVC_IRQ_VDEC, 2, &w, 0) == ETIMEDOUT && w.out_status == 0x20000);
	CHECK(mr(W_VMISC, 0xa4) == 0x20000);
	MDL.irq_armed[RWVC_IRQ_VDEC] = true; MDL.irq_at[RWVC_IRQ_VDEC] = now_us + 100; MDL.irq_bits[RWVC_IRQ_VDEC] = 0x10000;
	CHECK(wait_irq(RWVC_IRQ_VDEC, 100, &w, 0) == 0 && w.out_status == 0x30000);
	CHECK((mr(W_VMISC, 0xa4) & 0x10000u) == 0);
	/* the LAT line uses its own status register and the SOC block */
	MDL.irq_armed[RWVC_IRQ_VDEC_LAT] = true; MDL.irq_at[RWVC_IRQ_VDEC_LAT] = now_us + 100; MDL.irq_bits[RWVC_IRQ_VDEC_LAT] = 0x10000;
	CHECK(wait_irq(RWVC_IRQ_VDEC_LAT, 100, &w, 0) == 0 && w.out_status == 0x10000);
	CHECK((mr(W_VLMISC, 0xa4) & 0x10000u) == 0 && (mr(W_VMISC, 0xa4) & 0x10000u) == 0);
	close_expect(F, (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3));

	/* a line whose block went off is refused, not polled */
	F = n2vc_open_create(0);
	int e9 = wait_irq(RWVC_IRQ_VENC, 10, &w, 0);
	CHECKMSG(e9 == EACCES, "wait_irq after close = %d, powered %#x", e9, n2vc_powered_mask());
	close_expect(F, 0);
}

static void test_shared_ven_and_relay(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(ven_get_expect(1u << 0) == 0);                       /* K01's call */
	CHECK(dom_ok(RWVC_BLK_VENC));
	CHECK(ven_get_expect(0) == 0);                             /* idempotent: no register access */
	CHECK(power(0, 1, 0) == 0);                                /* no hardware access */
	CHECK(n2vc_powered_mask() == 1);
	/* a file that only holds a reference (no registers touched yet) may use the VENC window while VEN is on */
	struct rwvc_reg_op ro = { RWVC_OP_WRITE, RWVC_WIN_VENC, 0, 0x180, 0x5a5aa5a5u, 0 };
	struct rwvc_reg_batch rb = { (uint64_t)(uintptr_t)&ro, 1, 0, 0, 0 };
	CHECK(call(RWVC_REGS, &rb, 0) == 0 && mr(W_VENC, 0x180) == 0x5a5aa5a5u);
	ven_put_expect(0);
	CHECK(dom_ok(RWVC_BLK_VENC));
	CHECK(power(0, 0, 0) == 0);
	CHECK(dom_ok(RWVC_BLK_VENC));                              /* the other n2vc_ven_get() holds it */
	ven_put_expect(1u << 0);
	CHECK(!dom_ok(RWVC_BLK_VENC));
	ro.op = RWVC_OP_READ;
	CHECK(call(RWVC_REGS, &rb, 0) == EACCES);                  /* the window is gone with the domain */
	CHECK(n2vc_powered_mask() == 0);
	CHECK(ven_get_expect(1u << 0) == 0);
	close_expect(F, 0);                                        /* the external reference survives a file close */
	CHECK(dom_ok(RWVC_BLK_VENC));
	ven_put_expect(1u << 0);
	CHECK(!dom_ok(RWVC_BLK_VENC));

	/* two files: the last reference powers off */
	model_init();
	F = n2vc_open_create(0);
	G = n2vc_open_create(0);
	CHECK(power(0, 1, 1u << 0) == 0);
	CHECK(powerf(G, 0, 1, 0) == 0);
	close_expect(F, 0);
	CHECK(dom_ok(RWVC_BLK_VENC));
	close_expect(G, 1u << 0);
	CHECK(!dom_ok(RWVC_BLK_VENC));

	/* DMA sharing: allowed only across minors (IK's n2vr_dma_allowed rule); two clients never share */
	model_init();
	F = n2vc_open_create(0);                  /* server */
	G = n2vc_open_create(1);                  /* client A */
	struct n2vc_open *C = n2vc_open_create(1);/* client B */
	CHECK(F && G && C);
	uint32_t hh = 0, pp = 0, h2 = 0, p2 = 0;
	uint8_t buf2[16];
	CHECK(dma_alloc(F, 4096, 1, &hh, &pp) == 0);
	CHECK(dma_rw(G, hh, 0, buf2, 16, 0) == 0);       /* shared, other minor: allowed */
	CHECK(dma_rw(C, hh, 0, buf2, 16, 0) == 0);       /* every minor-1 client is on the other minor from the server */
	CHECK(dma_alloc(G, 4096, 1, &h2, &p2) == 0);
	CHECK(dma_free(C, h2) == EACCES);
	CHECK(dma_rw(F, h2, 0, buf2, 16, 0) == 0);       /* shared, other minor: allowed */
	close_expect(F, 0);
	close_expect(G, 0);
	close_expect(C, 0);
}

/* ---- wave 2: sleeping waits, RWVC_DMA_INFO, chunked DMA_RW ------------------------------------------------------- */
static struct n2vc_open *sleep_victim;
static int hook_result;

static void
hook_power_off_venc(void)
{
	uint32_t sm = expect_mask;
	int sa = expect_arm;
	struct rwvc_power p = { RWVC_BLK_VENC, 0 };
	expect_mask = 1u << RWVC_BLK_VENC; expect_arm = 1;
	hook_result = n2vc_ioctl(F, RWVC_POWER, (caddr_t)&p, 0, NULL);
	expect_mask = sm; expect_arm = sa;
}

static void
hook_close_venc(void)
{
	uint32_t sm = expect_mask;
	int sa = expect_arm;
	expect_mask = 1u << RWVC_BLK_VENC; expect_arm = 1;
	n2vc_open_destroy(sleep_victim);
	expect_mask = sm; expect_arm = sa;
}

/* the destroyed slot is reused by a new open while the waiter sleeps: the waiter must notice the new id */
static void
hook_close_reopen_venc(void)
{
	uint32_t sm = expect_mask;
	int sa = expect_arm;
	expect_mask = 1u << RWVC_BLK_VENC; expect_arm = 1;
	n2vc_open_destroy(sleep_victim);
	sleep_victim = n2vc_open_create(0);
	expect_mask = sm; expect_arm = sa;
}

static void
test_sleep_waits(void)
{
	struct rwvc_wait_irq w;
	uint64_t t0;

	/* WAIT_IRQ: another thread powers the block off during the sleep -> EACCES before any further window read */
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	CHECK(power(0, 1, 1u << 0) == 0);
	hook_result = -1;
	n2vc_test_sleep_cb = hook_power_off_venc;
	t0 = now_us;
	CHECK(wait_irq(RWVC_IRQ_VENC, 1000, &w, 1u << 0) == EACCES);
	CHECKMSG(hook_result == 0, "power-off in the sleep hook = %d", hook_result);
	CHECK(w.out_count == 0);
	CHECK(now_us - t0 >= N2VC_IRQ_POLL_US && now_us - t0 < 1000);
	CHECK(!dom_ok(RWVC_BLK_VENC));
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);

	/* WAIT_IRQ: the file is closed during the sleep -> ENXIO, no access after the block is gone */
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	CHECK(power(0, 1, 1u << 0) == 0);
	sleep_victim = F;
	n2vc_test_sleep_cb = hook_close_venc;
	CHECK(wait_irq(RWVC_IRQ_VENC, 1000, &w, 1u << 0) == ENXIO);
	CHECK(n2vc_powered_mask() == 0);
	CHECK(n2vc_minor_opens(0) == 0);
	F = NULL;

	/* WAIT_IRQ: close + reopen the same slot during the sleep -> the new open id must not continue the old wait */
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	CHECK(power(0, 1, 1u << 0) == 0);
	sleep_victim = F;
	n2vc_test_sleep_cb = hook_close_reopen_venc;
	CHECK(wait_irq(RWVC_IRQ_VENC, 1000, &w, 1u << 0) == ENXIO);
	CHECK(sleep_victim == F);                       /* the slot really was reused */
	CHECK(n2vc_minor_opens(0) == 1);
	F = sleep_victim;
	close_expect(F, 0);
	F = NULL;

	/* POLL: the block is powered off during the sleep -> EACCES, out_done stops before the poll */
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	CHECK(power(0, 1, 1u << 0) == 0);
	struct rwvc_reg_op ops[2];
	memset(ops, 0, sizeof(ops));
	ops[0].op = RWVC_OP_WRITE; ops[0].window = RWVC_WIN_VENC; ops[0].offset = 0x108; ops[0].value = 0;
	ops[1].op = RWVC_OP_POLL;  ops[1].window = RWVC_WIN_VENC; ops[1].offset = 0x108; ops[1].value = 4; ops[1].mask = 4;
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)ops, 2, 0, 500000u, 0 };
	hook_result = -1;
	n2vc_test_sleep_cb = hook_power_off_venc;
	t0 = now_us;
	CHECK(call(RWVC_REGS, &b, 1u << 0) == EACCES);
	CHECKMSG(hook_result == 0, "power-off in the sleep hook = %d", hook_result);
	CHECK(b.out_done == 1);
	CHECK(now_us - t0 == N2VC_IRQ_POLL_US);
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);

	/* POLL: the file is closed during the sleep -> ENXIO */
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	CHECK(power(0, 1, 1u << 0) == 0);
	sleep_victim = F;
	n2vc_test_sleep_cb = hook_close_venc;
	CHECK(call(RWVC_REGS, &b, 1u << 0) == ENXIO);
	CHECK(b.out_done == 1);
	CHECK(n2vc_powered_mask() == 0);
	F = NULL;
	CHECK(n2vc_test_sleep_cb == NULL);

	/* time is accounted, not counted: a 3 ms WAIT_IRQ leaves after >= 3000 fake us */
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(0, 1, 1u << 0) == 0);
	t0 = now_us;
	CHECK(wait_irq(RWVC_IRQ_VENC, 3, &w, 0) == ETIMEDOUT);
	CHECK(now_us - t0 >= 3000 && now_us - t0 <= 3500);
	close_expect(F, 1u << 0);

	/* a POLL that matches after several sleeps and its measured duration */
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(0, 1, 1u << 0) == 0);
	ops[0].op = RWVC_OP_POLL; ops[0].window = RWVC_WIN_VENC; ops[0].offset = 0x104; ops[0].value = 1; ops[0].mask = 1;
	b.count = 1; b.out_done = 0; b.poll_timeout_us = 5000;
	MDL.poll_armed = true; MDL.poll_w = W_VENC; MDL.poll_off = 0x104; MDL.poll_at = now_us + 1200; MDL.poll_value = 1;
	t0 = now_us;
	CHECK(call(RWVC_REGS, &b, 0) == 0);
	CHECK(b.out_done == 1);
	CHECK(ops[0].value == 1u);
	CHECK(now_us - t0 >= 1000 && now_us - t0 <= 2000);
	close_expect(F, 1u << 0);
}

static void
test_dma_info(void)
{
	model_init();
	F = n2vc_open_create(0);
	G = n2vc_open_create(1);
	struct n2vc_open *C = n2vc_open_create(1);
	CHECK(F && G && C);
	uint32_t h = 0, pa = 0, h2 = 0, p2 = 0, hg = 0, pg = 0, hc = 0, pc = 0;
	uint32_t bytes = 0, ipa = 0, res = 0;

	CHECK(dma_alloc(F, 3 * 4096, 0, &h, &pa) == 0);
	CHECK(dma_info(F, h, &bytes, &ipa, &res) == 0);
	CHECK(bytes == 3 * 4096);
	CHECK(ipa == pa);
	CHECK(res == 0xdeadbeefu);                       /* reserved is not a result field */
	CHECK(dma_info(G, h, NULL, NULL, NULL) == EPERM);   /* allocated without RWVC_DMA_SHARED */
	CHECK(dma_info(F, 0, NULL, NULL, NULL) == EINVAL);
	CHECK(dma_info(F, 0xffffffffu, NULL, NULL, NULL) == ENOENT);
	CHECK(dma_info(F, h ^ (1u << 12), NULL, NULL, NULL) == ENOENT);   /* stale generation */

	CHECK(dma_alloc(F, 8192, RWVC_DMA_SHARED, &h2, &p2) == 0);
	CHECK(dma_info(F, h2, &bytes, &ipa, NULL) == 0 && bytes == 8192 && ipa == p2);
	CHECK(dma_info(G, h2, &bytes, &ipa, NULL) == 0 && bytes == 8192 && ipa == p2);
	CHECK(dma_info(C, h2, NULL, NULL, NULL) == 0);      /* every client is on the other minor from the server */

	CHECK(dma_alloc(G, 4096, RWVC_DMA_SHARED, &hg, &pg) == 0);
	CHECK(dma_info(F, hg, &bytes, &ipa, NULL) == 0 && bytes == 4096 && ipa == pg);
	CHECK(dma_info(G, hg, NULL, NULL, NULL) == 0);      /* own handle */
	CHECK(dma_alloc(C, 4096, RWVC_DMA_SHARED, &hc, &pc) == 0);
	CHECK(dma_info(G, hc, NULL, NULL, NULL) == EPERM);  /* same minor: clients never see each other */
	CHECK(dma_info(F, hc, NULL, NULL, NULL) == 0);      /* other minor and shared */

	/* a freed slot reused by a new allocation: the old handle is stale (ENOENT), the new one works */
	CHECK(dma_free(F, h2) == 0);
	uint32_t h3 = 0, p3 = 0;
	CHECK(dma_alloc(F, 4096, 0, &h3, &p3) == 0);
	CHECK(dma_info(F, h3, &bytes, &ipa, NULL) == 0 && bytes == 4096 && ipa == p3);
	CHECK(dma_info(G, h2, NULL, NULL, NULL) == ENOENT);
	CHECK(dma_info(F, h2, NULL, NULL, NULL) == ENOENT);

	CHECK(dma_free(F, h) == 0 && dma_free(F, h3) == 0 && dma_free(G, hg) == 0 && dma_free(C, hc) == 0);
	close_expect(F, 0);
	close_expect(G, 0);
	close_expect(C, 0);
}

static void
test_dma_rw_chunks(void)
{
	model_init();
	F = n2vc_open_create(0);
	uint32_t bytes = 3u << 20, h = 0, pa = 0;
	CHECK(dma_alloc(F, bytes, 0, &h, &pa) == 0);
	uint8_t *src = malloc(bytes), *back = malloc(bytes);
	CHECK(src != NULL && back != NULL);
	if (!src || !back) { free(src); free(back); return; }
	for (uint32_t i = 0; i < bytes; i++) { src[i] = (uint8_t)(i * 131u + 7u); }
	uint32_t off = 100, len = bytes - 200;
	CHECK(dma_rw(F, h, off, src, len, 1) == 0);
	uint8_t *pool = wm[W_POOL] + (pa - RW_CLAIM_VCODEC_PA);
	/* the 1 MiB copy chunk boundaries, checked against the literal source pattern in the model's memory */
	CHECK(pool[off + (1u << 20) - 1] == src[(1u << 20) - 1]);
	CHECK(pool[off + (1u << 20)] == src[1u << 20]);
	CHECK(pool[off + (2u << 20) - 1] == src[(2u << 20) - 1]);
	CHECK(pool[off + (2u << 20)] == src[2u << 20]);
	CHECK(pool[off + len - 1] == src[len - 1]);
	CHECK(pool[off - 1] == 0 && pool[off + len] == 0);   /* outside the written range: the zero fill stays */
	memset(back, 0, bytes);
	CHECK(dma_rw(F, h, off, back, len, 0) == 0);
	CHECK(memcmp(src, back, len) == 0);
	/* the last byte of the buffer is untouched by a length that ends early */
	CHECK(back[len] == 0);
	free(src); free(back);
	CHECK(dma_free(F, h) == 0);
	close_expect(F, 0);
}

/* ---- ABI rev 3: RWVC_WIN_VDEC_ALL (DEC07) ----------------------------------------------------------------------- */
static void
hook_power_off_vdec_core(void)
{
	uint32_t sm = expect_mask;
	int sa = expect_arm;
	struct rwvc_power p = { RWVC_BLK_VDEC_CORE, 0 };
	expect_mask = (1u << RWVC_BLK_VDEC_CORE) | (1u << RWVC_BLK_VDEC_SOC);   /* the hidden SOC reference goes too */
	expect_arm = 1;
	hook_result = n2vc_ioctl(F, RWVC_POWER, (caddr_t)&p, 0, NULL);
	expect_mask = sm; expect_arm = sa;
}

static void
test_vdec_all(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(F != NULL);
	struct rwvc_reg_op ops[8];
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)ops, 0, 0, 200u, 0 };
	memset(ops, 0, sizeof(ops));
	for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) { ops[i].window = RWVC_WIN_VDEC_ALL; }

	/* nothing powered: both a core page and the larb5 page are refused */
	ops[0].op = RWVC_OP_READ; ops[0].window = RWVC_WIN_VDEC_ALL; ops[0].offset = 0x20000;
	b.count = 1; b.out_done = 0;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	ops[0].offset = 0x0d00c;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);

	/* SOC alone: SOC pages work, core pages do not, larb5 is readable */
	CHECK(power(RWVC_BLK_VDEC_SOC, 1, 1u << 2) == 0);
	ops[0].op = RWVC_OP_WRITE; ops[0].offset = 0x0f100; ops[0].value = 0x5a5a0001u;
	ops[1].op = RWVC_OP_READ;  ops[1].offset = 0x0f100;
	b.count = 2; b.out_done = 0;
	CHECK(call(RWVC_REGS, &b, 0) == 0 && b.out_done == 2);
	CHECK(ops[1].value == 0x5a5a0001u);
	ops[0].op = RWVC_OP_READ; ops[0].offset = 0x20000; b.count = 1; b.out_done = 0;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	ops[0].offset = 0x0d00c; ops[0].value = 0;
	CHECK(call(RWVC_REGS, &b, 0) == 0 && ops[0].value == 0);
	CHECK(power(RWVC_BLK_VDEC_SOC, 0, 1u << 2) == 0);

	/* CORE (its hidden SOC reference comes up too) opens all 48 pages; only the two SLP_CON words are writable on
	 * the larb pages (SMI_LARB_SLP_CON 0xc, bit 0: DEC04/opus-dec2's sleep-protect handshake) */
	CHECK(power(RWVC_BLK_VDEC_CORE, 1, (1u << 2) | (1u << 3)) == 0);
	memset(ops, 0, sizeof(ops));
	for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) { ops[i].window = RWVC_WIN_VDEC_ALL; }
	ops[0].op = RWVC_OP_WRITE; ops[0].offset = 0x20104; ops[0].value = 0xcafef00du;   /* VLD page */
	ops[1].op = RWVC_OP_READ;  ops[1].offset = 0x20104;
	ops[2].op = RWVC_OP_WRITE; ops[2].offset = 0x0d00c; ops[2].value = 1;             /* larb5 SLP_CON bit 0 */
	ops[3].op = RWVC_OP_WRITE; ops[3].offset = 0x2e00c; ops[3].value = 1;             /* larb4 SLP_CON bit 0 */
	ops[4].op = RWVC_OP_READ;  ops[4].offset = 0x2e00c;
	ops[5].op = RWVC_OP_WRITE; ops[5].offset = 0x0e000; ops[5].value = 0x11;           /* larb6, not kernel-owned */
	ops[6].op = RWVC_OP_WRITE; ops[6].offset = 0x2f100; ops[6].value = 0x22;           /* VDEC_SYS page */
	b.count = 7; b.out_done = 0;
	CHECK(call(RWVC_REGS, &b, 0) == 0 && b.out_done == 7);
	CHECK(ops[1].value == 0xcafef00du && ops[4].value == 1u);
	CHECK(mr(W_VALL, 0x20104) == 0xcafef00du && mr(W_VALL, 0x0d00c) == 1u && mr(W_VALL, 0x2e00c) == 1u);

	/* the allow-list is bit-masked and exact-offset: everything else on the larb pages is EACCES */
	memset(ops, 0, sizeof(ops));
	for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) { ops[i].window = RWVC_WIN_VDEC_ALL; }
	b.count = 1; b.out_done = 0;
	ops[0].op = RWVC_OP_WRITE; ops[0].offset = 0x0d00c; ops[0].value = 2;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES && b.out_done == 0);
	ops[0].op = RWVC_OP_WRITE_MASK; ops[0].value = 2; ops[0].mask = 2;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	ops[0].op = RWVC_OP_WRITE_MASK; ops[0].value = 3; ops[0].mask = 3;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	ops[0].op = RWVC_OP_WRITE; ops[0].offset = 0x0d010; ops[0].value = 1;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	ops[0].op = RWVC_OP_WRITE; ops[0].offset = 0x2e010; ops[0].value = 1;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	ops[0].op = RWVC_OP_WRITE_MASK; ops[0].offset = 0x0d00c; ops[0].value = 1; ops[0].mask = 1;
	CHECK(call(RWVC_REGS, &b, 0) == 0 && mr(W_VALL, 0x0d00c) == 1u);
	ops[0].op = RWVC_OP_WRITE_MASK; ops[0].offset = 0x0d00c; ops[0].value = 0; ops[0].mask = 1;
	CHECK(call(RWVC_REGS, &b, 0) == 0 && mr(W_VALL, 0x0d00c) == 0u);
	/* the ack poll is a read, not a write: allowed */
	ops[0].op = RWVC_OP_POLL; ops[0].offset = 0x0d00c; ops[0].value = 0x10000; ops[0].mask = 0x10000;
	setr(W_VALL, 0x0d00c, 0x10000u);
	CHECK(call(RWVC_REGS, &b, 0) == 0);
	/* range: the last word is 0x2fffc, 0x30000 is outside the 48 pages */
	ops[0].op = RWVC_OP_READ; ops[0].offset = 0x2fffc; ops[0].value = 0xdeadbeefu;
	CHECK(call(RWVC_REGS, &b, 0) == 0 && ops[0].value == 0u);
	ops[0].offset = 0x30000;
	CHECK(call(RWVC_REGS, &b, 0) == EINVAL);

	/* powered off during a POLL on a core page: EACCES and no register access after the wake-up */
	memset(ops, 0, sizeof(ops));
	for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) { ops[i].window = RWVC_WIN_VDEC_ALL; }
	ops[0].op = RWVC_OP_WRITE; ops[0].offset = 0x20108; ops[0].value = 0;
	ops[1].op = RWVC_OP_POLL;  ops[1].offset = 0x20108; ops[1].value = 4; ops[1].mask = 4;
	b.count = 2; b.out_done = 0; b.poll_timeout_us = 500000u;
	hook_result = -1;
	n2vc_test_sleep_cb = hook_power_off_vdec_core;
	uint64_t t0 = now_us;
	CHECK(call(RWVC_REGS, &b, (1u << 2) | (1u << 3)) == EACCES);
	CHECKMSG(hook_result == 0, "core power-off in the sleep hook = %d", hook_result);
	CHECK(b.out_done == 1);
	CHECK(now_us - t0 == N2VC_IRQ_POLL_US);
	CHECK(n2vc_powered_mask() == 0);
	close_expect(F, 0);
}

static void
test_open_slots(void)
{
	model_init();
	struct n2vc_open *a = n2vc_open_create(0);
	struct n2vc_open *b = n2vc_open_create(0);
	CHECK(a != NULL && b != NULL && a != b);
	n2vc_lock();
	CHECK(n2vc_slot_add(a, 0, NULL) == 0);
	CHECK(n2vc_slot_add(b, 0, NULL) == 0);
	n2vc_unlock();
	struct n2vc_open *out[N2VC_MAX_SLOTS];
	unsigned found = 0;
	n2vc_lock();
	unsigned n = n2vc_slot_close_all(0, out, N2VC_MAX_SLOTS, &found);
	n2vc_unlock();
	CHECK(n == 2 && found == 2);
	CHECK(out[0] == a && out[1] == b);
	n2vc_open_destroy(a);
	n2vc_open_destroy(b);

	/* one open in flight: it is not returned, but marked dead for the ioctl exit path; the other is returned */
	a = n2vc_open_create(0);
	b = n2vc_open_create(0);
	CHECK(a != NULL && b != NULL);
	n2vc_lock();
	CHECK(n2vc_slot_add(a, 0, NULL) == 0 && n2vc_slot_add(b, 0, NULL) == 0);
	n2vc_slot[0].busy = 1;
	n = n2vc_slot_close_all(0, out, N2VC_MAX_SLOTS, &found);
	n2vc_unlock();
	CHECK(n == 1 && found == 2 && out[0] == b);
	CHECK(n2vc_slot[0].dead == 1 && n2vc_slot[0].f == a && n2vc_slot[1].f == NULL);
	n2vc_lock();
	n2vc_slot[0].busy = 0; n2vc_slot[0].f = NULL; n2vc_slot[0].dead = 0;
	n2vc_unlock();
	n2vc_open_destroy(a);
	n2vc_open_destroy(b);

	/* all 16 files in slots: one close-all destroys every one (XNU's single d_close) */
	model_init();
	struct n2vc_open *many[N2VC_MAX_SLOTS];
	for (unsigned i = 0; i < N2VC_MAX_SLOTS; i++) {
		many[i] = n2vc_open_create(0);
		CHECK(many[i] != NULL);
		n2vc_lock();
		CHECK(n2vc_slot_add(many[i], 0, NULL) == 0);
		n2vc_unlock();
	}
	n2vc_lock();
	n = n2vc_slot_close_all(0, out, N2VC_MAX_SLOTS, &found);
	n2vc_unlock();
	CHECK(n == N2VC_MAX_SLOTS && found == N2VC_MAX_SLOTS);
	for (unsigned i = 0; i < n; i++) { n2vc_open_destroy(out[i]); }
	CHECK(n2vc_minor_opens(0) == 0);
}

int main(void)
{
	test_info_and_args();
	test_ven_full();
	test_c1_and_vdec();
	test_faults();
	test_regs();
	test_dma();
	test_irq();
	test_shared_ven_and_relay();
	test_sleep_waits();
	test_dma_info();
	test_dma_rw_chunks();
	test_vdec_all();
	test_open_slots();
	if (violations) { printf("%d model violations\n", violations); }
	printf("N2TEST K02 %s %d/%d (violations %d)\n", fails || violations ? "FAIL" : "PASS", checks - fails, checks, violations);
	return fails || violations ? 1 : 0;
}
