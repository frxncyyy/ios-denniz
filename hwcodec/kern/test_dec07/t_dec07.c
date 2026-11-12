/* SPDX-License-Identifier: MIT */
/*
 * t_dec07.c -- host test of the RWVC_WIN_VDEC_ALL addition to kern/bsd/dev/nord2_vcodec.inc (package DEC07).
 *
 * The file under test is the staged copy the run script builds:
 *   /tmp/n2hw-DEC07/stage/kern/nord2_vcodec.inc  (current tree file + patches/nord2_vcodec-vdec-all.patch)
 *   include/redwood_vcodec.h                     (current tree file + patches/redwood_vcodec-rev3.patch)
 * It is compiled here against a small model of the MT6893 registers and SPM that this test owns (ni_rd/ni_wr/
 * n2vc_host_* below).  The model is deliberately not K02's: it checks the same invariant independently -- a register
 * access to a block window while that block is not powered, out of reset and unprotected is a violation -- and it fails
 * the run if the driver writes another block's gates/LARB/SPM control during a power sequence, or a kernel-owned larb
 * page through the new window.
 *
 * The ALL window is modelled as its own 0x30000 array; K02's ten old windows have their own arrays.  The two views do
 * not alias in the model, so the tests use one view at a time (real hardware aliases them; power gating, the property
 * under test, is the same for both).
 *
 * Expected values are literals from the specifications, not from the driver:
 *   - the 48-page block table: facts/VDEC_PROTOCOL.md section 4, mt6893.dts:6281-6292, K02's per-window table in
 *     kern/VCODEC_HW.md section 0 (VDEC_BASE and VDEC_RACING are core/VDE2; SOC_GCON and LAT_* are VDE; larb5/larb4 are
 *     kernel-owned) and the DEC07 task text;
 *   - the register addresses of the SPM, INFRA, topckgen, gate and LARB blocks: kern/VCODEC_HW.md sections 1-4.
 * K02's own suite is a separate binary (run_tests.sh runs it first on the same patched .inc).
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

static int fails, checks, violations;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static void CHECKMSG(int c, const char *fmt, ...)
{
	checks++;
	if (!c) {
		fails++;
		va_list a;
		va_start(a, fmt);
		printf("FAIL ");
		vprintf(fmt, a);
		printf("\n");
		va_end(a);
	}
}

/* ---- literals from the specification --------------------------------------------------------------------------- */
#define CORE RWVC_BLK_VDEC_CORE
#define SOC  RWVC_BLK_VDEC_SOC
#define LARB5_PAGE 0x0du
#define LARB4_PAGE 0x2eu

/* facts/VDEC_PROTOCOL.md section 4 + mt6893.dts:6281-6292 + K02's per-window table (kern/VCODEC_HW.md section 0). */
static const uint8_t PAGE_BLK[48] = {
	/* 0x00 VDEC_BASE, 0x01-0x03 vendor core sub-blocks, 0x04 VDEC_RACING, 0x05-0x0c unused */
	CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE,
	/* 0x0d larb5 (kernel) */
	SOC,
	/* 0x0e larb6 (zero ports) */
	CORE,
	/* 0x0f SOC_GCON, 0x10 LAT_MISC, 0x11 LAT_VLD, 0x12-0x1f vendor LAT sub-blocks */
	SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC,
	/* 0x20 VLD, 0x21 MC, 0x22 AVC_VLD, 0x23 MV, 0x24 PP, 0x25-0x28 MISC, 0x29, 0x2a-0x2d unused,
	 * 0x2e larb4 (kernel), 0x2f VDEC_SYS */
	CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE,
};

/* ---- the model's register blocks -------------------------------------------------------------------------------- */
enum { W_SPM, W_INFRA, W_TOP, W_VG, W_VG2, W_L5, W_L4, W_ALL, W_BASE, W_LATM, W_LVLD, W_N };
static struct region {
	uint32_t pa, bytes;
	uint8_t *mem;
	int blk;                        /* RWVC_BLK_* that must be powered, -1 for SoC-level blocks / per-page */
	const char *name;
} R[W_N];

static uint64_t now_us;
static uint64_t reg_accesses;           /* every ni_rd/ni_wr that reached a region */
static uint32_t expect_mask;            /* blocks whose power sequence the test asked for */
static int expect_arm;

struct mdom { uint16_t con; uint8_t sta; uint32_t s1mm, s2mm, s1mm2, s2mm2; };
static const struct mdom md[RWVC_BLK_COUNT] = {
	{ 0x344, 17, 1u << 26, 1u << 27, 1u << 0, 1u << 1 },                    /* VEN */
	{ 0x348, 18, (1u << 28) | (1u << 30), (1u << 29) | (1u << 31), 0, 0 }, /* VEN_C1 */
	{ 0x33c, 15, 1u << 24, 1u << 25, 0, 0 },                               /* VDEC_SOC (VDE) */
	{ 0x340, 16, 0, 0, 1u << 6, 1u << 7 },                                 /* VDEC_CORE (VDE2) */
};

static uint32_t mr(int r, uint32_t off) { uint32_t v; memcpy(&v, R[r].mem + off, 4); return v; }
static void setr(int r, uint32_t off, uint32_t v) { memcpy(R[r].mem + off, &v, 4); }

static void viol(const char *what, unsigned a, unsigned b)
{
	violations++;
	printf("VIOLATION %s (%#x, %#x)\n", what, a, b);
}

static bool dom_ok(unsigned blk)
{
	if (blk >= RWVC_BLK_COUNT) { return false; }
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
		bool on = (con & 0xcu) == 0xcu;
		uint32_t sta = 1u << d->sta;
		setr(W_SPM, 0x16c, (mr(W_SPM, 0x16c) & ~sta) | (on ? sta : 0));
		setr(W_SPM, 0x170, (mr(W_SPM, 0x170) & ~sta) | (on ? sta : 0));
		setr(W_SPM, d->con, (con & ~(1u << 12)) | ((con & (1u << 8)) ? (1u << 12) : 0));
	}
}

static void expect_check(uint32_t blk, const char *what, uint32_t v)
{
	if (expect_arm && !(expect_mask & (1u << blk))) { viol(what, blk, v); }
}

static void model_write(struct region *r, uint32_t off, uint32_t v)
{
	switch (r - R) {
	case W_SPM: {
		if (off == 0x16c || off == 0x170 || off == 0x000) { setr(W_SPM, off, v); return; }
		if (off == 0x350) { setr(W_SPM, off, v); spm_update(); return; }
		for (unsigned b = 0; b < RWVC_BLK_COUNT; b++) {
			if (off == md[b].con) { expect_check(b, "write to another block's PWR_CON", off); setr(W_SPM, off, v); spm_update(); return; }
		}
		setr(W_SPM, off, v);
		return;
	}
	case W_INFRA: {
		if (off == 0x2d4 || off == 0x2d8 || off == 0xdcc || off == 0xdd0) {
			bool mm2 = (off == 0xdcc || off == 0xdd0);
			bool set = (off == 0x2d4 || off == 0xdcc);
			uint32_t base = mm2 ? 0xdc8u : 0x2d0u;
			uint32_t cur = mr(W_INFRA, base);
			setr(W_INFRA, base, set ? (cur | v) : (cur & ~v));
			setr(W_INFRA, mm2 ? 0xdd8u : 0x2ecu, mr(W_INFRA, base));
			return;
		}
		setr(W_INFRA, off, v);
		return;
	}
	case W_TOP:
		if (off == 0xe4 || off == 0xf4) { setr(W_TOP, off - 4, mr(W_TOP, off - 4) | v); setr(W_TOP, off, v); return; }
		if (off == 0xe8 || off == 0xf8) { setr(W_TOP, off - 8, mr(W_TOP, off - 8) & ~v); setr(W_TOP, off, v); return; }
		setr(W_TOP, off, v);
		return;
	case W_VG:
	case W_VG2: {
		unsigned blk = (r - R) == W_VG ? RWVC_BLK_VDEC_SOC : RWVC_BLK_VDEC_CORE;
		expect_check(blk, "vdec gate write", off);
		if (off == 0x0 || off == 0x200 || off == 0x8) { setr(r - R, off, mr(r - R, off) | v); }
		else if (off == 0x4) { setr(r - R, 0x0, mr(r - R, 0x0) & ~v); }
		else if (off == 0x204) { setr(r - R, 0x200, mr(r - R, 0x200) & ~v); }
		else if (off == 0xc) { setr(r - R, 0x8, mr(r - R, 0x8) & ~v); }
		else { setr(r - R, off, v); }
		return;
	}
	case W_L5:
	case W_L4: {
		unsigned blk = (r - R) == W_L5 ? RWVC_BLK_VDEC_SOC : RWVC_BLK_VDEC_CORE;
		unsigned nports = (r - R) == W_L5 ? 8u : 11u;
		uint32_t mask = (r - R) == W_L5 ? 0xffu : 0x7ffu;
		expect_check(blk, "larb write", off);
		if (off >= 0x200 && off < 0x200 + 4u * nports) {
			unsigned p = (off - 0x200) / 4u;
			if (!(mask & (1u << p))) { viol("larb OSTDL write to a port of another user", off, p); }
		}
		if (off >= 0x380 && off < 0x380 + 4u * nports) {
			unsigned p = (off - 0x380) / 4u;
			if (!(mask & (1u << p))) { viol("larb NON_SEC_CON write to a port of another user", off, p); }
		}
		setr(r - R, off, v);
		return;
	}
	default:
		setr(r - R, off, v);
		return;
	}
}

static int region_of(vm_offset_t base, uint32_t off, uint32_t bytes)
{
	for (int i = 0; i < W_N; i++) {
		uintptr_t a = (uintptr_t)R[i].mem;
		if (base >= a && base + off + bytes <= a + R[i].bytes) { return i; }
	}
	return -1;
}

static unsigned win_page_blk(int region, uint32_t off)
{
	if (region == W_ALL) { return PAGE_BLK[(off >> 12) & 63u]; }
	return (unsigned)R[region].blk;
}

static uint32_t ni_rd(vm_offset_t base, unsigned off)
{
	int i = region_of(base, off, 4);
	if (i < 0) { viol("read outside every modelled region", (unsigned)base, off); return 0; }
	reg_accesses++;
	unsigned blk = win_page_blk(i, off);
	if (blk < RWVC_BLK_COUNT && !dom_ok(blk)) { viol("read of a block that is not powered", (unsigned)i, off); }
	return mr(i, off);
}

static void ni_wr(vm_offset_t base, unsigned off, uint32_t v)
{
	int i = region_of(base, off, 4);
	if (i < 0) { viol("write outside every modelled region", (unsigned)base, off); return; }
	reg_accesses++;
	unsigned blk = win_page_blk(i, off);
	if (blk < RWVC_BLK_COUNT && !dom_ok(blk)) { viol("write of a block that is not powered", (unsigned)i, off); }
	if (i == W_ALL) {
		uint32_t page = off >> 12;
		if (page == LARB5_PAGE || page == LARB4_PAGE) {
			bool ok = (page == LARB5_PAGE || page == LARB4_PAGE) && (off & 0xfffu) == 0x00cu && (v & ~1u) == 0;
			if (!ok) { viol("write through the ALL window to a kernel-owned larb page", page, off); }
		}
	}
	model_write(&R[i], off, v);
}

static void ni_mask(vm_offset_t base, unsigned off, uint32_t mask, uint32_t v)
{
	ni_wr(base, off, (ni_rd(base, off) & ~mask) | (v & mask));
}

/* ---- shims the driver expects ---------------------------------------------------------------------------------- */
void *n2vc_host_map(uint32_t pa, uint32_t bytes)
{
	int best = -1;
	for (int i = 0; i < W_N; i++) {
		if (R[i].pa != pa || R[i].bytes < bytes) { continue; }
		if (best < 0 || R[i].bytes < R[best].bytes) { best = i; }
	}
	return best < 0 ? NULL : R[best].mem;
}
void n2vc_host_us(uint32_t us) { now_us += us; }
uint64_t n2vc_host_now_us(void) { return now_us; }

static void (*n2vc_test_sleep_cb)(void);
void n2vc_host_sleep(uint32_t us)
{
	now_us += us;
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

/* the file under test (staged copy, see the header comment) */
#include "nord2_vcodec_inc.h"

/* ---- test helpers ---------------------------------------------------------------------------------------------- */
static struct n2vc_open *F;
static int cb_err;

static int ioctl_call(u_long cmd, void *arg)
{
	return n2vc_ioctl(F, cmd, (caddr_t)arg, 0, NULL);
}

static int power_ex(unsigned blk, int on, uint32_t mask)
{
	struct rwvc_power p = { blk, (uint32_t)(on ? 1 : 0) };
	expect_mask = mask;
	expect_arm = 1;
	int e = ioctl_call(RWVC_POWER, &p);
	expect_arm = 0;
	return e;
}

static int rd(unsigned win, uint32_t off, uint32_t *v)
{
	struct rwvc_reg_op o = { RWVC_OP_READ, (uint8_t)win, 0, off, 0, 0 };
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)&o, 1, 0, 0, 0 };
	expect_arm = 0;
	int e = ioctl_call(RWVC_REGS, &b);
	if (v) { *v = o.value; }
	return e;
}

static int rd_done(unsigned win, uint32_t off, uint32_t *done)
{
	struct rwvc_reg_op o = { RWVC_OP_READ, (uint8_t)win, 0, off, 0, 0 };
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)&o, 1, 0, 0, 0 };
	expect_arm = 0;
	int e = ioctl_call(RWVC_REGS, &b);
	*done = b.out_done;
	return e;
}

static int wr(unsigned win, uint32_t off, uint32_t v)
{
	struct rwvc_reg_op o = { RWVC_OP_WRITE, (uint8_t)win, 0, off, v, 0 };
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)&o, 1, 0, 0, 0 };
	expect_arm = 0;
	return ioctl_call(RWVC_REGS, &b);
}

static void model_init(void)
{
	for (int i = 0; i < W_N; i++) { memset(R[i].mem, 0, R[i].bytes); }
	now_us = 0;
	reg_accesses = 0;
	expect_mask = 0;
	expect_arm = 0;
	violations = 0;
	cb_err = 0;
	n2vc_test_sleep_cb = NULL;
	/* the phone's power state (runs/q10hw1/00-step0.log: PWR_STATUS 0x10580e03): DIS bit 20 and MDP bit 19 on */
	setr(W_SPM, 0x16c, (1u << 20) | (1u << 19));
	setr(W_SPM, 0x170, (1u << 20) | (1u << 19));
	setr(W_SPM, 0x000, 0x0b160001u);
	/* CLK_CFG_1: disp_sel and mdp_sel ungated; CLK_CFG_14: vdec_sel/vdec_lat_sel on parent 14, ungated -> the driver's
	 * n2vc_mux() leaves a running mux alone and writes no top-clock register (kern/VCODEC_HW.md section 2). */
	setr(W_TOP, 0x020, 0);
	setr(W_TOP, 0x0f0, 0x00000e0eu);
	memset(&n2vc, 0, sizeof n2vc);          /* clear every bit of driver state (refs, on[], mappings, files) */
}

static void open_file(void)
{
	F = n2vc_open_create(0);
	CHECK(F != NULL);
}

static void close_file(void)
{
	n2vc_open_destroy(F);
	F = NULL;
}

/* ---- tests ----------------------------------------------------------------------------------------------------- */
static void test_window_decl(void)
{
	model_init();
	CHECK(RWVC_WIN_VDEC_ALL == 12);
	CHECK(RWVC_WIN_COUNT == 13);
	CHECK(n2vc_wins[RWVC_WIN_VDEC_ALL].pa == 0x16000000u);
	CHECK(n2vc_wins[RWVC_WIN_VDEC_ALL].bytes == 0x30000u);
	CHECK(n2vc_wins[RWVC_WIN_VDEC_ALL].blk == N2VC_BLK_PER_PAGE);
	/* the ten old windows keep their own block (K02's table, unchanged by this patch) */
	CHECK(n2vc_wins[RWVC_WIN_VDEC_BASE].blk == RWVC_BLK_VDEC_CORE);
	CHECK(n2vc_wins[RWVC_WIN_VDEC_RACING].blk == RWVC_BLK_VDEC_CORE);
	CHECK(n2vc_wins[RWVC_WIN_VDEC_LAT_MISC].blk == RWVC_BLK_VDEC_SOC);
	CHECK(n2vc_wins[RWVC_WIN_VDEC_SOC_GCON].blk == RWVC_BLK_VDEC_SOC);
	CHECK(N2VC_VDEC_PAGES == 48u);
	/* the two windows over the same physical base map separately (the old one first in the cache) */
	CHECK(n2vc_pa(0x16000000u, 0x1000u, 1) == (vm_offset_t)(uintptr_t)R[W_BASE].mem);
	CHECK(n2vc_pa(0x16000000u, 0x30000u, 1) == (vm_offset_t)(uintptr_t)R[W_ALL].mem);
}

static void test_page_table(void)
{
	model_init();
	for (uint32_t p = 0; p < 48; p++) {
		CHECKMSG(n2vc_vdec_page_blk(p) == PAGE_BLK[p], "page 0x%02x: driver says block %u, the specification says %u",
		    p, n2vc_vdec_page_blk(p), PAGE_BLK[p]);
	}
}

/* Every page must be refused with EACCES while its block is off, and the driver must not have touched hardware. */
static void test_off_all_pages(void)
{
	model_init();
	open_file();
	CHECK(n2vc_powered_mask() == 0);
	for (uint32_t p = 0; p < 48; p++) {
		uint32_t off = p * 4096u, v = 0, done = 99;
		uint64_t before = reg_accesses;
		CHECK(rd(RWVC_WIN_VDEC_ALL, off, &v) == EACCES);
		CHECK(rd_done(RWVC_WIN_VDEC_ALL, off, &done) == EACCES && done == 0);
		CHECK(wr(RWVC_WIN_VDEC_ALL, off, 0xdec07000u) == EACCES);
		CHECKMSG(reg_accesses == before, "page 0x%02x: %llu register accesses while every block is off", p,
		    (unsigned long long)(reg_accesses - before));
	}
	close_file();
}

/* Only VDE (VDEC_SOC) on: its 18 pages work, the 30 core pages stay EACCES. */
static void test_soc_only(void)
{
	model_init();
	open_file();
	CHECK(power_ex(RWVC_BLK_VDEC_SOC, 1, 1u << RWVC_BLK_VDEC_SOC) == 0);
	CHECK(n2vc_powered_mask() == (1u << RWVC_BLK_VDEC_SOC));
	unsigned soc_pages = 0, core_pages = 0;
	for (uint32_t p = 0; p < 48; p++) {
		uint32_t off = p * 4096u;
		if (PAGE_BLK[p] == SOC) {
			soc_pages++;
			CHECKMSG(rd(RWVC_WIN_VDEC_ALL, off, NULL) == 0, "SOC page 0x%02x not readable with VDEC_SOC on", p);
			int e = wr(RWVC_WIN_VDEC_ALL, off, 0x50c00000u + p);
			if (p == LARB5_PAGE) { CHECK(e == EACCES); } else { CHECKMSG(e == 0, "SOC page 0x%02x not writable", p); }
		} else {
			core_pages++;
			CHECKMSG(rd(RWVC_WIN_VDEC_ALL, off, NULL) == EACCES, "core page 0x%02x readable with only VDEC_SOC on", p);
			CHECKMSG(wr(RWVC_WIN_VDEC_ALL, off, 0xdead0000u) == EACCES, "core page 0x%02x writable with only VDEC_SOC on", p);
		}
	}
	CHECK(soc_pages == 18 && core_pages == 30);
	/* a value written through the window is the value the register holds, and it reads back */
	CHECK(wr(RWVC_WIN_VDEC_ALL, 0x10 * 4096u + 8, 0x11223344u) == 0);
	CHECK(mr(W_ALL, 0x10 * 4096u + 8) == 0x11223344u);
	uint32_t v = 0;
	CHECK(rd(RWVC_WIN_VDEC_ALL, 0x10 * 4096u + 8, &v) == 0 && v == 0x11223344u);
	close_file();
	CHECK(n2vc_powered_mask() == 0);
}

/* VDEC_CORE on (it holds a hidden VDEC_SOC reference): every page works except writes to the two larb pages. */
static void test_core_powered(void)
{
	model_init();
	open_file();
	uint32_t both = (1u << RWVC_BLK_VDEC_SOC) | (1u << RWVC_BLK_VDEC_CORE);
	CHECK(power_ex(RWVC_BLK_VDEC_CORE, 1, both) == 0);
	CHECK(n2vc_powered_mask() == both);
	for (uint32_t p = 0; p < 48; p++) {
		uint32_t off = p * 4096u;
		CHECKMSG(rd(RWVC_WIN_VDEC_ALL, off, NULL) == 0, "page 0x%02x not readable with VDEC_CORE on", p);
		int e = wr(RWVC_WIN_VDEC_ALL, off, 0x600d0000u + p);
		if (p == LARB4_PAGE || p == LARB5_PAGE) { CHECK(e == EACCES); }
		else { CHECKMSG(e == 0, "page 0x%02x not writable", p); }
	}
	/* the kernel's own LARB programming happened in the power sequence */
	CHECK(mr(W_L5, 0x24) == 0x00300256u);
	CHECK(mr(W_L4, 0x24) == 0x00300256u);
	/* the sleep-protect bit is the one larb word a client may change, on both larbs */
	CHECK(wr(RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, 0x1u) == 0);
	CHECK(mr(W_ALL, LARB5_PAGE * 4096u + 0x00c) == 1u);
	CHECK(wr(RWVC_WIN_VDEC_ALL, LARB4_PAGE * 4096u + 0x00c, 0x1u) == 0);
	CHECK(mr(W_ALL, LARB4_PAGE * 4096u + 0x00c) == 1u);
	CHECK(wr(RWVC_WIN_VDEC_ALL, LARB4_PAGE * 4096u + 0x00c, 0x2u) == EACCES);   /* bit 1 outside the mask */
	CHECK(wr(RWVC_WIN_VDEC_ALL, LARB4_PAGE * 4096u + 0x010u, 0x1u) == EACCES);  /* another larb4 word */
	CHECK(power_ex(RWVC_BLK_VDEC_CORE, 0, both) == 0);
	CHECK(n2vc_powered_mask() == 0);
	for (uint32_t p = 0; p < 48; p++) {
		CHECKMSG(rd(RWVC_WIN_VDEC_ALL, p * 4096u, NULL) == EACCES, "page 0x%02x readable after power-off", p);
	}
	close_file();
}

/* Batch semantics: the first failing op ends the batch, earlier results are still copied out. */
static void test_batches(void)
{
	model_init();
	open_file();
	CHECK(power_ex(RWVC_BLK_VDEC_SOC, 1, 1u << RWVC_BLK_VDEC_SOC) == 0);
	setr(W_ALL, 0x10 * 4096u + 4, 0xabcdef01u);
	struct rwvc_reg_op ops[3] = {
		{ RWVC_OP_READ, RWVC_WIN_VDEC_ALL, 0, 0x10 * 4096u + 4, 0, 0 },
		{ RWVC_OP_WRITE, RWVC_WIN_VDEC_ALL, 0, LARB5_PAGE * 4096u + 0x24, 0, 0 },   /* read-only larb */
		{ RWVC_OP_READ, RWVC_WIN_VDEC_ALL, 0, 0x11 * 4096u, 0, 0 },
	};
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)ops, 3, 0, 0, 0 };
	CHECK(ioctl_call(RWVC_REGS, &b) == EACCES);
	CHECK(b.out_done == 1);
	CHECK(ops[0].value == 0xabcdef01u);           /* copied out before the failing op */
	CHECK(ops[1].value == 0);

	/* a core page in the middle of a batch, only SOC on: op 0 runs, op 1 fails, op 2 does not run */
	struct rwvc_reg_op ops2[3] = {
		{ RWVC_OP_READ, RWVC_WIN_VDEC_ALL, 0, 0x11 * 4096u, 0, 0 },
		{ RWVC_OP_WRITE, RWVC_WIN_VDEC_ALL, 0, 0x20 * 4096u, 0x1111u, 0 },
		{ RWVC_OP_READ, RWVC_WIN_VDEC_ALL, 0, 0x11 * 4096u, 0, 0 },
	};
	struct rwvc_reg_batch b2 = { (uint64_t)(uintptr_t)ops2, 3, 0, 0, 0 };
	CHECK(ioctl_call(RWVC_REGS, &b2) == EACCES);
	CHECK(b2.out_done == 1);
	CHECK(mr(W_ALL, 0x20 * 4096u) == 0);

	/* bounds and alignment: 0x2fffc is the last word of the window */
	CHECK(rd(RWVC_WIN_VDEC_ALL, 0x2fffc, NULL) == EACCES);        /* page 0x2f is core: off */
	CHECK(rd(RWVC_WIN_VDEC_ALL, 0x30000, NULL) == EINVAL);
	CHECK(rd(RWVC_WIN_VDEC_ALL, 0x2ffe, NULL) == EINVAL);
	CHECK(rd(RWVC_WIN_COUNT, 0, NULL) == EINVAL);
	CHECK(rd(RWVC_WIN_VDEC_ALL, 0x0d000u, NULL) == 0);            /* larb5 read, SOC on */
	CHECK(rd(RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x24, NULL) == 0);
	/* a masked write to a larb page is a write and is refused too */
	struct rwvc_reg_op m = { RWVC_OP_WRITE_MASK, RWVC_WIN_VDEC_ALL, 0, LARB5_PAGE * 4096u + 0x24, 1, 1 };
	struct rwvc_reg_batch bm = { (uint64_t)(uintptr_t)&m, 1, 0, 0, 0 };
	CHECK(ioctl_call(RWVC_REGS, &bm) == EACCES && bm.out_done == 0);
	/* ... except the two sleep-protect words (larb4 needs its core powered: covered by test_core_powered), whose bit-0
	 * write and masked write are allowed */
	CHECK(wr(RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, 0x1) == 0);
	CHECK(mr(W_ALL, LARB5_PAGE * 4096u + 0x00c) == 1u);
	struct rwvc_reg_op m2 = { RWVC_OP_WRITE_MASK, RWVC_WIN_VDEC_ALL, 0, LARB5_PAGE * 4096u + 0x00c, 1, 1 };
	struct rwvc_reg_batch bm2 = { (uint64_t)(uintptr_t)&m2, 1, 0, 0, 0 };
	CHECK(ioctl_call(RWVC_REGS, &bm2) == 0 && bm2.out_done == 1);
	struct rwvc_reg_op m3 = { RWVC_OP_WRITE_MASK, RWVC_WIN_VDEC_ALL, 0, LARB5_PAGE * 4096u + 0x00c, 2, 3 };
	struct rwvc_reg_batch bm3 = { (uint64_t)(uintptr_t)&m3, 1, 0, 0, 0 };
	CHECK(ioctl_call(RWVC_REGS, &bm3) == EACCES && bm3.out_done == 0);         /* bit 1 is outside the mask */
	CHECK(wr(RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, 0x2) == EACCES);
	CHECK(wr(RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x010, 0x1) == EACCES);   /* another larb5 word */
	close_file();
}

/* The larb allow-list: larb5 and larb4 SMI_LARB_SLP_CON bit 0, bit-masked; everything else refused. */
static void test_allow_list(void)
{
	model_init();
	/* the two documented words: larb5 0x1600d00c and larb4 0x1602e00c, bit 0 only (the sleep-protect handshake) */
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x00c, 0x1) == true);
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x00c, 0x0) == true);
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x00c, 0x2) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x00c, 0x3) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x010, 0x1) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB4_PAGE, 0x00c, 0x1) == true);
	CHECK(n2vc_vdec_larb_write_allowed(LARB4_PAGE, 0x00c, 0x2) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB4_PAGE, 0x010, 0x1) == false);
	CHECK(n2vc_vdec_allow_n >= 2);     /* DEC02 may add a word; the two known entries are checked above */
	/* the words the kernel itself programs must never be opened: larb CON/CMD_THRT/SW_FLAG and the port registers */
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x24, 0) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x40, 0) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x524, 0) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB5_PAGE, 0x200, 0) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB4_PAGE, 0x24, 0) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB4_PAGE, 0x200, 0) == false);
	CHECK(n2vc_vdec_larb_write_allowed(LARB4_PAGE, 0x380, 0) == false);
}

/* A POLL on an ALL page whose block is powered off during the sleep must end with EACCES, not ETIMEDOUT, and must not
 * read the register after the wake-up. */
static void cb_power_off(void)
{
	cb_err = power_ex(RWVC_BLK_VDEC_CORE, 0, (1u << RWVC_BLK_VDEC_SOC) | (1u << RWVC_BLK_VDEC_CORE));
}

static void test_poll_poweroff(void)
{
	model_init();
	open_file();
	CHECK(power_ex(RWVC_BLK_VDEC_CORE, 1, (1u << RWVC_BLK_VDEC_SOC) | (1u << RWVC_BLK_VDEC_CORE)) == 0);
	struct rwvc_reg_op o = { RWVC_OP_POLL, RWVC_WIN_VDEC_ALL, 0, 0x20 * 4096u + 4, 1, 1 };
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)&o, 1, 0, 5000, 0 };
	n2vc_test_sleep_cb = cb_power_off;
	int e = ioctl_call(RWVC_REGS, &b);
	CHECK(e == EACCES);
	CHECK(b.out_done == 0);
	CHECK(cb_err == 0);
	CHECK(n2vc_powered_mask() == 0);
	CHECK(violations == 0);
	close_file();
}

/* The ten old windows are untouched by this patch: same block, same behaviour. */
static void test_old_windows(void)
{
	model_init();
	open_file();
	CHECK(power_ex(RWVC_BLK_VDEC_SOC, 1, 1u << RWVC_BLK_VDEC_SOC) == 0);
	CHECK(rd(RWVC_WIN_VDEC_LAT_MISC, 0, NULL) == 0);
	CHECK(rd(RWVC_WIN_VDEC_BASE, 0, NULL) == EACCES);       /* core: still refused with only SOC on */
	CHECK(wr(RWVC_WIN_VDEC_LAT_VLD, 0, 0x1234u) == 0);
	CHECK(power_ex(RWVC_BLK_VDEC_CORE, 1, (1u << RWVC_BLK_VDEC_SOC) | (1u << RWVC_BLK_VDEC_CORE)) == 0);
	CHECK(wr(RWVC_WIN_VDEC_BASE, 0x0, 0x99u) == 0);
	CHECK(mr(W_BASE, 0) == 0x99u);
	CHECK(wr(RWVC_WIN_VDEC_BASE, 0x1000, 0x99u) == EINVAL); /* still a 0x1000 window */
	close_file();
}

int main(void)
{
	static const struct region def[W_N] = {
		[W_SPM]   = { 0x10006000u, 0x1000u, NULL, -1, "SPM" },
		[W_INFRA] = { 0x10001000u, 0x1000u, NULL, -1, "INFRA" },
		[W_TOP]   = { 0x10000000u, 0x1000u, NULL, -1, "TOP" },
		[W_VG]    = { 0x1600f000u, 0x1000u, NULL, RWVC_BLK_VDEC_SOC,  "vdec_soc_gcon" },
		[W_VG2]   = { 0x1602f000u, 0x1000u, NULL, RWVC_BLK_VDEC_CORE, "vdec_gcon" },
		[W_L5]    = { 0x1600d000u, 0x1000u, NULL, RWVC_BLK_VDEC_SOC,  "larb5" },
		[W_L4]    = { 0x1602e000u, 0x1000u, NULL, RWVC_BLK_VDEC_CORE, "larb4" },
		[W_ALL]   = { 0x16000000u, 0x30000u, NULL, -1, "VDEC_ALL" },
		[W_BASE]  = { 0x16000000u, 0x1000u, NULL, RWVC_BLK_VDEC_CORE, "VDEC_BASE" },
		[W_LATM]  = { 0x16010000u, 0x1000u, NULL, RWVC_BLK_VDEC_SOC,  "VDEC_LAT_MISC" },
		[W_LVLD]  = { 0x16011000u, 0x0400u, NULL, RWVC_BLK_VDEC_SOC,  "VDEC_LAT_VLD" },
	};
	for (int i = 0; i < W_N; i++) {
		R[i] = def[i];
		R[i].mem = calloc(1, R[i].bytes);
		if (!R[i].mem) { printf("N2TEST DEC07CORE FAIL allocation\n"); return 1; }
	}

	test_window_decl();
	test_page_table();
	test_off_all_pages();
	test_soc_only();
	test_core_powered();
	test_batches();
	test_allow_list();
	test_poll_poweroff();
	test_old_windows();

	for (int i = 0; i < W_N; i++) { free(R[i].mem); }
	if (violations) { printf("%d model violations\n", violations); }
	printf("N2TEST DEC07CORE %s %d/%d (violations %d)\n", fails || violations ? "FAIL" : "PASS", checks - fails, checks, violations);
	return fails || violations ? 1 : 0;
}
