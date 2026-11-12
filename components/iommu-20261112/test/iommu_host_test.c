/* 0401 host test: v7s builder/walk vs an independent reference (Linux io-pgtable-arm-v7s.c iopte_to_paddr / prot_to_pte semantics,
 * CONFIG_MTK_IOMMU_V2, quirks NS|NO_PERMS|MTK_4GB), IOVA allocator vs a first-fit model, cache victim, gate decisions, planner window.
 * cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../b/iokit/Kernel/arm iommu_host_test.c   (also as C++: -x c++) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_iommu.h"
#include "sweet_fb_hw.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint64_t rs = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

/* ---- reference: written from the Linux macros, not from sweet_fb_iommu.h */
#define R_PA32 (1u << 9)
#define R_PA33 (1u << 4)
static uint64_t ref_paddr(uint32_t pte, int lvl)
{
	uint64_t mask = (pte & 3u) == 1u && lvl == 1 ? 0xfffffc00ull : (lvl == 1 ? 0xfff00000ull : 0xfffff000ull);
	uint64_t pa = pte & mask;
	if (pte & R_PA32) pa |= 1ull << 32;
	if (pte & R_PA33) pa |= 1ull << 33;
	return pa;
}
/* the walk as the M4U does it, tables addressed by PA through a host "memory" lookup */
static const uint8_t *g_blk; static uint64_t g_blk_pa;
static uint32_t mem32(uint64_t pa) { uint32_t v; memcpy(&v, g_blk + (pa - g_blk_pa), 4); return v; }
static uint64_t ref_walk(uint64_t l1pa, uint64_t iova)
{
	uint32_t e1 = mem32(l1pa + ((iova >> 20) & 0x3fffu) * 4u);
	if ((e1 & 3u) == 0) return ~0ull;
	if ((e1 & 3u) == 2u) return ref_paddr(e1, 1) | (iova & 0xfffffu);
	if ((e1 & 3u) == 1u) {
		uint64_t l2 = e1 & 0xfffffc00u;   /* + PA32/33 bits for an L2 above 4 GiB: never ours */
		uint32_t e2 = mem32(l2 + ((iova >> 12) & 0xffu) * 4u);
		if (!(e2 & 2u)) return ~0ull;
		return ref_paddr(e2, 2) | (iova & 0xfffu);
	}
	return ~0ull;
}
/* Linux arm_v7s_prot_to_pte(IOMMU_READ|IOMMU_WRITE, lvl, NO_PERMS|NS|MTK_4GB) | paddr_to_iopte */
static uint32_t ref_pte(uint64_t pa, int lvl)
{
	uint32_t attr = (1u << 7) | (1u << 2);      /* NG | TEX(1) (V2: no S) */
	uint32_t pte = attr << (16 - lvl * 6);
	pte |= 2u;
	if (lvl == 1) pte |= 1u << 19;
	if (pa & (1ull << 32)) pte |= R_PA32;
	if (pa & (1ull << 33)) pte |= R_PA33;
	pte |= (uint32_t)(pa & (lvl == 1 ? 0xfff00000u : 0xfffff000u));
	return pte;
}

static void test_tables(void)
{
	size_t sz = SWEET_FB_IOMMU_BLOCK_BYTES;
	uint8_t *blk = (uint8_t *)aligned_alloc(65536, (sz + 65535) & ~(size_t)65535);
	memset(blk, 0xa5, sz);
	struct sweet_fb_iommu_pt pt;
	CHECK(!sweet_fb_iommu_pt_init(&pt, blk, 0xC0108000ull), "unaligned pa accepted");
	CHECK(!sweet_fb_iommu_pt_init(&pt, blk, 0xFFFF0000ull), "block crossing 4 GiB accepted");
	g_blk = blk; g_blk_pa = 0xC0100000ull;
	CHECK(sweet_fb_iommu_pt_init(&pt, blk, g_blk_pa), "init");
	CHECK(sz <= 10u * 65536u && SWEET_FB_IOMMU_OFF_SCRATCH % 4096 == 0, "layout %zu", sz);
	/* PTE encodings vs Linux */
	CHECK(sweet_fb_iommu_pte_page(0x142345000ull) == ref_pte(0x142345000ull, 2), "page pte %08x vs %08x", sweet_fb_iommu_pte_page(0x142345000ull), ref_pte(0x142345000ull, 2));
	CHECK(sweet_fb_iommu_pte_page(0x142345000ull) == 0x42345a42u, "page pte literal %08x", sweet_fb_iommu_pte_page(0x142345000ull));
	CHECK(sweet_fb_iommu_pte_page(0x22dfff000ull) == ref_pte(0x22dfff000ull, 2), "bit33 pte");
	CHECK(sweet_fb_iommu_pte_section(0xC0000000u) == ref_pte(0xC0000000ull, 1), "section %08x vs %08x", sweet_fb_iommu_pte_section(0xC0000000u), ref_pte(0xC0000000ull, 1));
	/* identity + invalid regions (tables only reachable through ref_walk by PA) */
	static const uint32_t ids[] = { 0x76e00000u, 0x76e10000u, 0x77806123u, 0x781fc000u, 0x78bfffffu, 0xC0000000u, 0xCfffffffu, 0xD0000000u, 0xDFE70000u, 0xDfffffffu };
	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		CHECK(sweet_fb_iommu_walk(&pt, ids[i]) == ids[i], "identity %08x", ids[i]);
		CHECK(ref_walk(pt.l1_pa, ids[i]) == ids[i], "ref identity %08x", ids[i]);
	}
	static const uint64_t bad[] = { 0, 0x76d00000u, 0x78c00000u, 0x7fffffffu, 0x80000000u, 0x8fffffffu, 0x9fffffffu, 0xB0000000u, 0xBfffffffu, 0xE0000000u, 0xffffffffu,
		                        0x100000000ull, 0x1C0000000ull, 0x380000000ull };
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(sweet_fb_iommu_walk(&pt, bad[i]) == SWEET_FB_IOMMU_BAD_PA, "not faulting %llx", (unsigned long long)bad[i]);
		CHECK(ref_walk(pt.l1_pa, bad[i]) == ~0ull, "ref not faulting %llx", (unsigned long long)bad[i]);
	}
	/* L1 pointers of the window -> our L2 tables, < 4 GiB, NS_TABLE */
	for (uint32_t i = SWEET_FB_IOMMU_WIN_BASE >> 20; i < (SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE) >> 20; i++) {
		uint32_t e = pt.l1[i];
		CHECK((e & 3u) == 1u && (e & 8u) && (e & 0xfffffc00u) == pt.l2_pa + (i - (SWEET_FB_IOMMU_WIN_BASE >> 20)) * 1024u, "l1[%x]=%08x", i, e);
	}
	for (uint32_t i = 4096; i < SWEET_FB_IOMMU_L1_ENTRIES; i++) { CHECK(pt.l1[i] == 0, "l1 above 4 GiB [%x]", i); }
	/* rejects */
	CHECK(!sweet_fb_iommu_map_page(&pt, 0x8ffff000u, 0x142000000ull), "below window");
	CHECK(!sweet_fb_iommu_map_page(&pt, 0xB0000000u, 0x142000000ull), "above window");
	CHECK(!sweet_fb_iommu_map_page(&pt, SWEET_FB_IOMMU_WIN_BASE + 0x800u, 0x142000000ull), "unaligned iova");
	CHECK(!sweet_fb_iommu_map_page(&pt, SWEET_FB_IOMMU_WIN_BASE, 0x142000800ull), "unaligned pa");
	CHECK(!sweet_fb_iommu_map_page(&pt, SWEET_FB_IOMMU_WIN_BASE, 0x400000000ull), "pa >= 2^34");
	CHECK(pt.mapped == 0, "mapped after rejects");
	/* random mapping vs model */
	enum { NP = SWEET_FB_IOMMU_WIN_SIZE >> 12 };
	uint64_t *model = (uint64_t *)calloc(NP, sizeof(uint64_t));
	uint32_t live = 0;
	for (int step = 0; step < 400000; step++) {
		uint32_t pg = (uint32_t)(rnd() % NP), iova = SWEET_FB_IOMMU_WIN_BASE + (pg << 12);
		int op = (int)(rnd() % 4);
		if (op < 2) {
			uint64_t pa = (0x142000000ull + (rnd() % 0xEC000ull) * 4096ull);
			if ((rnd() & 15) == 0) pa = 0xC0000000ull + (rnd() % 0x10000ull) * 4096ull;   /* pool page */
			bool ok = sweet_fb_iommu_map_page(&pt, iova, pa);
			CHECK(ok == (model[pg] == 0), "map ok %d model %llx", ok, (unsigned long long)model[pg]);
			if (ok) { model[pg] = pa; live++; }
		} else if (op == 2) {
			uint32_t n = 1 + (uint32_t)(rnd() % 64);
			sweet_fb_iommu_unmap(&pt, iova, n);
			for (uint32_t k = 0; k < n && pg + k < NP; k++) { if (model[pg + k]) { model[pg + k] = 0; live--; } }
		} else {
			uint64_t off = rnd() & 0xfff, a = (uint64_t)iova + off;
			uint64_t w = sweet_fb_iommu_walk(&pt, a), r = ref_walk(pt.l1_pa, a), m = model[pg] ? model[pg] + off : ~0ull;
			CHECK(w == m && r == m, "walk %llx: %llx ref %llx model %llx", (unsigned long long)a, (unsigned long long)w, (unsigned long long)r, (unsigned long long)m);
		}
		CHECK(pt.mapped == live, "mapped %u live %u", pt.mapped, live);
		if (fails > 20) break;
	}
	for (uint32_t pg = 0; pg < NP; pg += 7) {
		uint64_t a = SWEET_FB_IOMMU_WIN_BASE + ((uint64_t)pg << 12) + 0x7c4;
		uint64_t m = model[pg] ? model[pg] + 0x7c4 : ~0ull;
		CHECK(sweet_fb_iommu_walk(&pt, a) == m && ref_walk(pt.l1_pa, a) == m, "final walk %llx", (unsigned long long)a);
		if (model[pg]) { uint32_t e; memcpy(&e, g_blk + (pt.l2_pa - g_blk_pa) + pg * 4u, 4); CHECK(e == ref_pte(model[pg], 2), "pte %08x vs ref %08x", e, ref_pte(model[pg], 2)); }
	}
	/* a corrupted L1 pointer must not be followed */
	uint32_t save = pt.l1[SWEET_FB_IOMMU_WIN_BASE >> 20]; pt.l1[SWEET_FB_IOMMU_WIN_BASE >> 20] = 0x00001001u;
	CHECK(sweet_fb_iommu_walk(&pt, SWEET_FB_IOMMU_WIN_BASE) == SWEET_FB_IOMMU_BAD_PA, "foreign l2 followed");
	pt.l1[SWEET_FB_IOMMU_WIN_BASE >> 20] = save;
	free(model); free(blk);
}

/* ---- allocator vs first-fit model */
static void test_iova(void)
{
	static struct sweet_fb_iova a;
	static uint8_t own[SWEET_FB_IOMMU_NGRAN];      /* 0 free, else allocation tag */
	struct { uint32_t iova; uint64_t bytes; } live[512];
	unsigned nl = 0;
	sweet_fb_iova_init(&a);
	memset(own, 0, sizeof(own));
	CHECK(sweet_fb_iova_alloc(&a, 0) == SWEET_FB_IOVA_FAIL, "0 bytes");
	CHECK(sweet_fb_iova_alloc(&a, (uint64_t)SWEET_FB_IOMMU_WIN_SIZE + 1) == SWEET_FB_IOVA_FAIL, "too big");
	/* full-screen surfaces: 11,069,184 B = 169 granules + guard */
	uint32_t n = 0, x;
	while ((x = sweet_fb_iova_alloc(&a, 11069184ull)) != SWEET_FB_IOVA_FAIL) { CHECK(x == SWEET_FB_IOMMU_WIN_BASE + n * 170u * 65536u, "fs %u at %x", n, x); n++; }
	CHECK(n == SWEET_FB_IOMMU_NGRAN / 170u, "full-screen count %u", n);
	printf("window holds %u full-screen (1125x2436 pitch 4544) surfaces\n", n);
	for (uint32_t i = 0; i < n; i++) CHECK(sweet_fb_iova_free(&a, SWEET_FB_IOMMU_WIN_BASE + i * 170u * 65536u, 11069184ull), "free fs %u", i);
	CHECK(a.live == 0 && a.granules == 0, "empty after frees");
	uint32_t badc = a.bad_free;
	for (int step = 0; step < 200000; step++) {
		if ((rnd() % 3) != 0 && nl < 512) {
			uint64_t bytes = (rnd() & 1) ? 1 + rnd() % (12u << 20) : 1 + rnd() % 300000;
			uint32_t g = sweet_fb_iova_ngran(bytes), run = 0, want = SWEET_FB_IOVA_FAIL;
			for (uint32_t k = 0; k < SWEET_FB_IOMMU_NGRAN; k++) { run = own[k] ? 0 : run + 1; if (run == g) { want = SWEET_FB_IOMMU_WIN_BASE + (k + 1 - g) * 65536u; break; } }
			uint32_t got = sweet_fb_iova_alloc(&a, bytes);
			CHECK(got == want, "alloc %llu: %x want %x", (unsigned long long)bytes, got, want);
			if (got != SWEET_FB_IOVA_FAIL) {
				uint32_t g0 = (got - SWEET_FB_IOMMU_WIN_BASE) / 65536u;
				for (uint32_t k = g0; k < g0 + g; k++) { CHECK(!own[k], "overlap"); own[k] = 1; }
				live[nl].iova = got; live[nl].bytes = bytes; nl++;
			}
		} else if (nl > 0) {
			unsigned i = (unsigned)(rnd() % nl);
			int kind = (int)(rnd() % 8);
			if (kind == 0) { CHECK(!sweet_fb_iova_free(&a, live[i].iova + 65536u, live[i].bytes), "inner free accepted"); }
			else if (kind == 1) { CHECK(!sweet_fb_iova_free(&a, live[i].iova, live[i].bytes + 65536u * 2), "longer free accepted"); }
			else if (kind == 2 && sweet_fb_iova_ngran(live[i].bytes) > 2) { CHECK(!sweet_fb_iova_free(&a, live[i].iova, 1), "shorter free accepted"); }
			else {
				CHECK(sweet_fb_iova_free(&a, live[i].iova, live[i].bytes), "free");
				uint32_t g0 = (live[i].iova - SWEET_FB_IOMMU_WIN_BASE) / 65536u, g = sweet_fb_iova_ngran(live[i].bytes);
				for (uint32_t k = g0; k < g0 + g; k++) own[k] = 0;
				CHECK(!sweet_fb_iova_free(&a, live[i].iova, live[i].bytes), "double free accepted");
				live[i] = live[--nl];
			}
		}
		if (fails > 20) break;
	}
	CHECK(a.live == nl, "live %u vs %u", a.live, nl);
	CHECK(a.bad_free > badc, "bad frees not counted");
	CHECK(!sweet_fb_iova_free(&a, 0x70000000u, 4096), "outside");
	CHECK(!sweet_fb_iova_free(&a, SWEET_FB_IOMMU_WIN_BASE + 100, 4096), "unaligned");
}

static void test_cache_gate(void)
{
	struct sweet_fb_iommu_ent e[4];
	memset(e, 0, sizeof(e));
	int o1, o2, o3;
	e[0].obj = &o1; e[0].iova = 0x80000000u; e[0].bytes = 0x100000u; e[0].stamp = 5;
	e[1].obj = &o2; e[1].iova = 0x80200000u; e[1].bytes = 0x100000u; e[1].stamp = 3;
	e[2].obj = &o3; e[2].iova = 0x80400000u; e[2].bytes = 0x100000u; e[2].stamp = 9;
	struct sweet_fb_iommu_prog p = { { { 0x76e10000u, 0xffffffffu }, { 0x76e10000u, 0xffffffffu } } };
	CHECK(sweet_fb_iommu_victim(e, 4, &p, 9) == 1, "lru victim");
	p.addr[1][1] = 0x80200000u + 0x8000u;      /* e[1] programmed (pipe 1 layer 1, with crop offset) */
	CHECK(sweet_fb_iommu_victim(e, 4, &p, 9) == 0, "programmed skipped");
	CHECK(sweet_fb_iommu_victim(e, 4, &p, 5) == -1, "current generation protected");
	p.addr[0][0] = 0x80000000u + 0xfffffu;
	CHECK(sweet_fb_iommu_victim(e, 4, &p, 100) == 2, "only unprogrammed");
	CHECK(sweet_fb_iommu_ent_programmed(&e[0], &p) && !sweet_fb_iommu_ent_programmed(&e[2], &p), "programmed()");
	p.addr[0][0] = 0x80100000u;     /* one past e[0] */
	CHECK(!sweet_fb_iommu_ent_programmed(&e[0], &p), "end exclusive");
	/* gate: has_iova, ports_on, armed, other */
	CHECK(sweet_fb_iommu_gate_decide(true, false, true, false) == SWEET_FB_IOMMU_G_ENABLE, "enable");
	CHECK(sweet_fb_iommu_gate_decide(true, true, true, false) == SWEET_FB_IOMMU_G_KEEP, "keep on");
	CHECK(sweet_fb_iommu_gate_decide(true, true, false, false) == SWEET_FB_IOMMU_G_REFUSE, "refuse unarmed");
	CHECK(sweet_fb_iommu_gate_decide(true, false, false, true) == SWEET_FB_IOMMU_G_REFUSE, "refuse unarmed 2");
	CHECK(sweet_fb_iommu_gate_decide(false, true, false, false) == SWEET_FB_IOMMU_G_DISABLE, "disable");
	CHECK(sweet_fb_iommu_gate_decide(false, true, false, true) == SWEET_FB_IOMMU_G_KEEP, "other pipe still reads iova");
	CHECK(sweet_fb_iommu_gate_decide(false, true, true, false) == SWEET_FB_IOMMU_G_KEEP, "armed keeps on");
	CHECK(sweet_fb_iommu_gate_decide(false, false, false, false) == SWEET_FB_IOMMU_G_KEEP, "off stays off");
}

/* planner: an IOVA layer (full screen 1125x2436, crop 22,18) is planned; anything leaving the window is not */
static void test_planner(void)
{
	CHECK(!sweet_fb_hw_reachable(0x80000000ull, 0x80001000ull), "0x80000000 stays unreachable (0399 test)");
	CHECK(sweet_fb_hw_reachable(SWEET_FB_IOMMU_WIN_BASE, (uint64_t)SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE), "window reachable");
	CHECK(!sweet_fb_hw_reachable(SWEET_FB_IOMMU_WIN_BASE - 4, (uint64_t)SWEET_FB_IOMMU_WIN_BASE + 4), "window start edge");
	CHECK(!sweet_fb_hw_reachable((uint64_t)SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE - 4, (uint64_t)SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE + 4), "window end edge");
	struct sweet_fb_hw_in l;
	memset(&l, 0, sizeof(l));
	l.pa = SWEET_FB_IOMMU_WIN_BASE + 170u * 65536u * 3u; l.len = 11069184u; l.pitch = 4544; l.format_ok = 1;
	l.sx = 22; l.sy = 18; l.dx = 0; l.dy = 0; l.w = 1080; l.h = 2400;
	struct sweet_fb_hw_plan p;
	int r = sweet_fb_hw_plan_make(&p, &l, 1, SWEET_FB_HW_SINGLE, 2, 1080, 2400);
	CHECK(r == SWEET_FB_HW_OK, "iova plan %d", r);
	CHECK(p.slot[0][0].addr == l.pa + 18u * 4544u + 22u * 4u, "pipe0 addr %x", p.slot[0][0].addr);
	CHECK(p.slot[1][0].addr == l.pa + 18u * 4544u + (22u + 540u) * 4u, "pipe1 addr %x", p.slot[1][0].addr);
	l.pa = (uint64_t)SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE - 11069184u + 0x80000u;
	CHECK(sweet_fb_hw_plan_make(&p, &l, 1, SWEET_FB_HW_SINGLE, 2, 1080, 2400) == SWEET_FB_HW_W_RANGE, "leaving the window planned");
	l.pa = 0x142000000ull;
	CHECK(sweet_fb_hw_plan_make(&p, &l, 1, SWEET_FB_HW_SINGLE, 2, 1080, 2400) == SWEET_FB_HW_W_RANGE, "raw DRAM planned");
}

#ifdef __cplusplus
static uint32_t g_a0, g_a1; static unsigned g_pipe, g_calls;
extern "C" bool sweet_fb_iommu_gate(unsigned pipe, uint32_t a0, uint32_t a1) { g_pipe = pipe; g_a0 = a0; g_a1 = a1; g_calls++; return true; }
static void test_gate_list(void)
{
	struct sweet_fb_hw_reg r[SWEET_FB_HW_MAX_REGS];
	struct sweet_fb_hw_base b;
	memset(&b, 0, sizeof(b));
	b.addr[1] = 0x77806000u;
	unsigned n = sweet_fb_hw_regs_base(r, 1, &b, 0x76e10870u);
	CHECK(sweet_fb_iommu_gate_list(1, r, n) && g_pipe == 1 && g_a0 == 0x76e10870u && g_a1 == 0x77806000u, "gate_list base %x %x", g_a0, g_a1);
	struct sweet_fb_hw_reg one = { 0x2cu, 1u, 0xfu };
	CHECK(sweet_fb_iommu_gate_list(0, &one, 1) && g_a0 == 0xffffffffu && g_a1 == 0xffffffffu, "gate_list without ADDR");
}
#endif

int main(void)
{
	test_tables();
	test_iova();
	test_cache_gate();
	test_planner();
#ifdef __cplusplus
	test_gate_list();
#endif
	printf("%s (%s)\n", fails ? "FAILED" : "ALL OK", sizeof(void *) == 8 ? "64-bit" : "?");
	return fails != 0;
}
