/*
 * 0650 host test (ram8-20261114): the 13-region graphics-heap table (sweet_gfx_heap.h of piece-0650/b) against the full 8 GiB map
 * (LK mblock_info free list, every reserved-memory node with a reg in live.dts, the loader's live addresses, every fixed claim of our
 * code, XNU's window, the DMA reach of the masters that touch heap pages), the allocator against a reference model (400k random ops),
 * and the display-pool lending (bitmap tail reserved -> the pool never hands out or classifies a granule of the lent part).
 * Build: see run.sh (ASan + UBSan, -Werror).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef HEAP_H
#define HEAP_H "../piece-0650/b/iokit/Kernel/arm/sweet_gfx_heap.h"
#endif
#include HEAP_H
#include "sweet_fb_iommu.h"
#include "sweet_fb_hw_pool_alloc.h"

struct rg { uint64_t b, e; const char *n; };
/* live.dts /memory mblock_info (decoded: 20 LK free blocks, 7055.3 MiB) */
static const struct rg mfree[] = {
	{0x40000000,0x40080000,""},{0x47d00000,0x48080000,""},{0x48180000,0x48200000,""},{0x48600000,0x48c00000,""},{0x48e00000,0x48f00000,""},
	{0x55100000,0x5ff00000,""},{0x5ff80000,0x74000000,""},{0x74e30000,0x75e00000,""},{0x7e841000,0x7e900000,""},{0x7fe00000,0x7feb0000,""},
	{0x7ffb0000,0x7ffbf000,""},{0x80000000,0x8a000000,""},{0x8d560000,0x8e000000,""},{0x8f800000,0x9fa00000,""},{0x9ff00000,0xbd6c0000,""},
	{0xbfe40000,0xbff70000,""},{0xc0000000,0xd0000000,""},{0xd0000000,0xdfe70000,""},{0x100000000ull,0x13ffff000ull,""},{0x140000000ull,0x23ffff000ull,""},
};
/* LK-reserved but Linux RAM (no reserved-memory node: Linux reuses it after boot): the boot buffers the 0650 regions lb0/lb4 use */
static const struct rg lk_boot[] = {
	{0x40080000,0x47d00000,"LK: kernel Image + payload + DTB area"},{0x48f00000,0x55100000,"LK: boot buffers / ramdisk 0x51100000"},
};
/* every reserved-memory node with a reg (live.dts) */
static const struct rg vendor[] = {
	{0xdfe70000,0xe0000000,"DPM"},{0x74000000,0x74e30000,"adsp"},{0x8a000000,0x8d560000,"ccci"},{0x8e000000,0x8f800000,"ap_md_nc_smem"},
	{0xe0000000,0x100000000ull,"md_mem_usage"},{0xbff70000,0xbff80000,"ccci_tag"},{0x9fa00000,0x9ff00000,"SCP-reserved"},{0x75e00000,0x75e10000,"SPM"},
	{0xbd6c0000,0xbfe00000,"vpu_binary"},{0x76e10000,0x78c00000,"framebuffer"},{0x48170000,0x48180000,"minirdump"},{0x48090000,0x48170000,"pstore"},
	{0x48080000,0x48090000,"aee"},{0x5ff00000,0x5ff80000,"PICACHU"},{0x7e900000,0x7ea00000,"MCUPM"},{0x78c00000,0x7e400000,"tee"},
	{0x7feb0000,0x7ffb0000,"SSPM"},{0x7e400000,0x7e800000,"tee-secmem"},{0xbfe00000,0xbfe40000,"atf-log"},{0xbff80000,0xc0000000,"atf-ramdump"},
	{0x48c00000,0x48e00000,"atf"},{0x7ffbf000,0x7ffff000,"log_store"},{0x7e800000,0x7e841000,"gz-log"},{0x7ea00000,0x7fe00000,"gz"},
	{0x7ffff000,0x80000000,"emi_mbist"},{0x13ffff000ull,0x140000000ull,"dramc-rk0"},{0x23ffff000ull,0x240000000ull,"dramc-rk1"},
};
/* our fixed claims + what the loader leaves for later use */
static const struct rg ours[] = {
	{0x40000000,0x40400000,"loader 0x40080000 + chain-back stub 0x40280000 (/chosen nord2-stock-entry)"},
	{0x47c00000,0x47e00000,"stock DTB 0x47c80000 + the 1 MiB the Wi-Fi driver maps (/chosen nord2-stock-dtb)"},
	{0x48000000,0x48200000,"pmsg/ramoops/GT trace block (identity NC 0x48000000 block) margin"},
	{0x70000000,0x70200000,"N2D share"},{0x89000000,0x89320000,"SCP DRAM"},
	{0x8fb00000,0x8ff50000,"Wi-Fi CONN"},{0x90000000,0x90f20000,"Wi-Fi DMA pool"},{0xa0000000,0xb8000000,"RWVC (vcodec)"},
	{0xc0000000,0xc1000000,"display pool kept part (IOMMU tables)"},
	{0xd0000000,0xd8000000,"cameras"},{0xd8000000,0xda800000,"JPEG"},{0xda800000,0xdfc00000,"jpegdec + guard"},{0xdfc00000,0xdfe70000,"GNSS"},
	{0x100000000ull,0x23e000000ull,"XNU window (5088 MiB)"},
};

static int overl(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { return a < d && c < b; }

static uint32_t model[SWEET_GFX_HEAP_NGRAN];
static uint32_t gfirst[SWEET_GFX_HEAP_NREG + 1];
static unsigned region_of_gran(uint32_t g) { for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) if (g < gfirst[r + 1]) return r; return 99; }

static void check_consistent(const struct sweet_gfx_heap *h)
{
	uint32_t tot = 0;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		uint32_t u = 0;
		for (uint32_t g = gfirst[r]; g < gfirst[r + 1]; g++) { assert(sweet_gfx_heap_test(h, g) == (model[g] != 0)); u += model[g] != 0; }
		assert(u == h->used[r]); tot += u;
	}
	assert(tot == h->used_total);
	for (uint32_t g = SWEET_GFX_HEAP_NGRAN; g < SWEET_GFX_HEAP_WORDS * 64; g++) assert(!sweet_gfx_heap_test(h, g));
}

/* = SweetSurface.cpp sweet_pool_surface_pa's 0650 predicate */
static int pool_surface(uint64_t pa, uint64_t len, uint32_t lim)
{
	return sweet_fb_hw_pool_contains(pa, len) && pa - SWEET_FB_HW_POOL_BASE + len <= (uint64_t)lim * SWEET_FB_HW_POOL_GRANULE;
}

int main(void)
{
	uint64_t total = 0, safe = 0, new0650 = 0;
	/* 1. table geometry and the 8 GiB map */
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
		uint64_t e = g->base + g->size;
		assert(g->size && (g->base % SWEET_GFX_HEAP_GRANULE) == 0 && (g->size % SWEET_GFX_HEAP_GRANULE) == 0);
		assert(g->base >= 0x40000000ull && e <= 0x240000000ull);                      /* /memory reg = 0x40000000 + 8 GiB */
		for (unsigned q = 0; q < r; q++) assert(!overl(g->base, e, sweet_gfx_heap_regions[q].base, sweet_gfx_heap_regions[q].base + sweet_gfx_heap_regions[q].size));
		int in = 0;
		for (unsigned i = 0; i < sizeof(mfree) / sizeof(mfree[0]); i++) in |= g->base >= mfree[i].b && e <= mfree[i].e;
		for (unsigned i = 0; i < sizeof(lk_boot) / sizeof(lk_boot[0]); i++) in |= r >= 9 && g->base >= lk_boot[i].b && e <= lk_boot[i].e;
		if (!in) { printf("region %s is neither LK-free nor an LK boot buffer\n", g->name); return 1; }
		for (unsigned i = 0; i < sizeof(vendor) / sizeof(vendor[0]); i++)
			if (overl(g->base, e, vendor[i].b, vendor[i].e)) { printf("region %s overlaps reserved-memory %s\n", g->name, vendor[i].n); return 1; }
		for (unsigned i = 0; i < sizeof(ours) / sizeof(ours[0]); i++)
			if (overl(g->base, e, ours[i].b, ours[i].e)) { printf("region %s overlaps %s\n", g->name, ours[i].n); return 1; }
		/* IOMMU: IOVA-window numbers <=> F_NOPHYS (never on by default); an identity section only for the pool's lent part (IOVA == PA there: harmless,
		 * and heap surfaces are only shown through IOVA-window PTEs anyway) */
		bool iova = overl(g->base, e, SWEET_FB_IOMMU_WIN_BASE, (uint64_t)SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE);
		assert(iova == !!(g->flags & SWEET_GFX_HEAP_F_NOPHYS));
		assert(!(iova && g->cls == SWEET_GFX_HEAP_SAFE));
		assert(!overl(g->base, e, SWEET_FB_IOMMU_ID_FB_BASE, SWEET_FB_IOMMU_ID_FB_END));
		bool idlow = overl(g->base, e, SWEET_FB_IOMMU_ID_LOW_BASE, SWEET_FB_IOMMU_ID_LOW_END);
		bool inpool = overl(g->base, e, SWEET_FB_HW_POOL_BASE, SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE);
		assert(inpool == !!(g->flags & SWEET_GFX_HEAP_F_POOLHI) && idlow == inpool);
		if (inpool) assert(g->base >= SWEET_FB_HW_POOL_BASE + SWEET_GFX_HEAP_POOL_KEEP && e <= SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE);
		/* DMA reach: display M4U PTEs carry PA[33:32] (34 bit), Mali 40 bit -> every heap PA below 16 GiB; OVL physical (pool) only below 4 GiB */
		assert(e <= 0x400000000ull);
		assert(sweet_gfx_heap_region_of(g->base, g->size) == (int)r && sweet_gfx_heap_region_of(g->base, g->size + 1) != (int)r);
		assert(sweet_gfx_heap_region_of(e - 4096, 4096) == (int)r && sweet_gfx_heap_region_of(e - 4096, 8192) != (int)r);
		total += g->size; safe += g->cls == SWEET_GFX_HEAP_SAFE ? g->size : 0; new0650 += r >= 9 ? g->size : 0;
		gfirst[r] = sweet_gfx_heap_first(r); gfirst[r + 1] = gfirst[r] + sweet_gfx_heap_ngran(r);
		printf("%-6s %#011llx..%#011llx %8.2f MiB %s%s%s\n", g->name, (unsigned long long)g->base, (unsigned long long)e, g->size / 1048576.0,
		    g->cls ? "CHECK" : "SAFE", (g->flags & SWEET_GFX_HEAP_F_NOPHYS) ? " iommu-only" : "", (g->flags & SWEET_GFX_HEAP_F_POOLHI) ? " pool-lent" : "");
	}
	assert(gfirst[SWEET_GFX_HEAP_NREG] == SWEET_GFX_HEAP_NGRAN);
	assert(SWEET_FB_IOMMU_BLOCK_BYTES <= SWEET_GFX_HEAP_POOL_KEEP);            /* the IOMMU table block (first fit at the pool base) stays in the kept part */
	assert((SWEET_GFX_HEAP_POOL_KEEP % SWEET_FB_HW_POOL_GRANULE) == 0 && SWEET_GFX_HEAP_POOL_KEEP < SWEET_FB_HW_POOL_SIZE);
	/* the loader check of the kernel: the live addresses of this phone (loader log: entry 0x40280000, dtb 0x47c80000) hit no region */
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
		assert(!overl(g->base, g->base + g->size, 0x40280000ull & ~0xfffffull, (0x40280000ull & ~0xfffffull) + 0x200000ull));
		assert(!overl(g->base, g->base + g->size, 0x47c80000ull, 0x47c80000ull + 0x100000ull));
	}
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		assert(sweet_gfx_heap_gran_pa(gfirst[r]) == sweet_gfx_heap_regions[r].base);
		assert(sweet_gfx_heap_gran_pa(gfirst[r + 1] - 1) == sweet_gfx_heap_regions[r].base + sweet_gfx_heap_regions[r].size - SWEET_GFX_HEAP_GRANULE);
	}
	assert(sweet_gfx_heap_gran_pa(SWEET_GFX_HEAP_NGRAN) == ~0ull);
	for (uint32_t i = 0; i + 1 < SWEET_GFX_HEAP_NGRAN; i++)
		assert(sweet_gfx_heap_canary(sweet_gfx_heap_gran_pa(i)) != sweet_gfx_heap_canary(sweet_gfx_heap_gran_pa(i + 1)));
	/* 2. masks: default = SAFE (0440's six + the four 0650 regions), CHECK only with kern.n2heap_check */
	uint32_t safe_mask = sweet_gfx_heap_allowed(1, 0, SWEET_GFX_HEAP_ALL, 0);
	assert(SWEET_GFX_HEAP_ALL == 0x1fff && safe_mask == 0x1f1f && sweet_gfx_heap_allowed(1, 1, SWEET_GFX_HEAP_ALL, 0) == 0x1fff);
	assert(sweet_gfx_heap_allowed(0, 1, 0x1fff, 0) == 0 && sweet_gfx_heap_allowed(1, 0, 0x1fff, 1u << 9) == 0x1d1f);
	/* 3. first fit keeps the 0440 order: the proven regions fill before any 0650 region */
	static struct sweet_gfx_heap h;
	unsigned rr = 99;
	uint32_t got = 0;
	for (;;) {
		uint32_t g = sweet_gfx_heap_alloc(&h, 11069184, safe_mask, ~0u, &rr);
		if (g == SWEET_GFX_HEAP_FAIL) break;
		if (rr >= 9) { for (unsigned q = 0; q < 9; q++) if ((safe_mask >> q) & 1) assert(sweet_gfx_heap_ngran(q) - h.used[q] < 169); }   /* sequential fill: a 0440 region's free tail is < 169 granules */
		got++;
	}
	printf("full-screen surfaces (1125x2436 BGRA, 169 granules): %u fit in the SAFE regions (0440 alone: 67 by the same rule)\n", got);
	memset(&h, 0, sizeof(h));
	/* 4. random stress against the model (all 13 regions; CHECK only with its bit) */
	srand(6502);
	static struct { uint64_t pa, bytes; uint32_t id; } live[8192];
	unsigned nlive = 0; uint32_t next_id = 1;
	for (unsigned it = 0; it < 400000; it++) {
		if (nlive < 8192 && (rand() % 100) < 55) {
			uint64_t bytes = (rand() % 4 == 0) ? 11069184 : ((uint64_t)(1 + rand() % 300) * 4096 * (1 + rand() % 16));
			uint32_t mask = (rand() % 3 == 0) ? SWEET_GFX_HEAP_ALL : safe_mask;
			uint32_t cap = (rand() % 10 == 0) ? (uint32_t)(rand() % SWEET_GFX_HEAP_NGRAN) : ~0u;
			uint32_t used0 = h.used_total;
			uint32_t g = sweet_gfx_heap_alloc(&h, bytes, mask, cap, &rr);
			uint32_t n = sweet_gfx_heap_granules(bytes);
			if (g == SWEET_GFX_HEAP_FAIL) {
				if (used0 + n <= cap) for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
					if (!((mask >> r) & 1)) continue;
					uint32_t run = 0; for (uint32_t q = gfirst[r]; q < gfirst[r + 1]; q++) { run = model[q] ? 0 : run + 1; assert(run < n); }
				}
				continue;
			}
			assert(used0 + n <= cap && ((mask >> rr) & 1) && region_of_gran(g) == rr && region_of_gran(g + n - 1) == rr);
			for (uint32_t q = g; q < g + n; q++) { assert(model[q] == 0); model[q] = next_id; }
			uint64_t pa = sweet_gfx_heap_gran_pa(g);
			assert(sweet_gfx_heap_region_of(pa, (uint64_t)n * 65536) == (int)rr);
			/* no heap allocation is ever classified as a pool surface once the pool lent its upper part */
			assert(!pool_surface(pa, bytes, (uint32_t)(SWEET_GFX_HEAP_POOL_KEEP / SWEET_FB_HW_POOL_GRANULE)));
			live[nlive].pa = pa; live[nlive].bytes = bytes; live[nlive].id = next_id++; nlive++;
		} else if (nlive) {
			unsigned k = (unsigned)rand() % nlive;
			int r = sweet_gfx_heap_region_of(live[k].pa, 65536);
			uint32_t g = gfirst[r] + (uint32_t)((live[k].pa - sweet_gfx_heap_regions[r].base) / 65536), n = sweet_gfx_heap_granules(live[k].bytes);
			assert(sweet_gfx_heap_is_allocated(&h, live[k].pa, live[k].bytes));
			assert(sweet_gfx_heap_free(&h, live[k].pa, live[k].bytes));
			for (uint32_t q = g; q < g + n; q++) { assert(model[q] == live[k].id); model[q] = 0; }
			live[k] = live[--nlive];
		}
		if ((it % 5000) == 0) check_consistent(&h);
	}
	check_consistent(&h);
	printf("stress: %llu allocs %llu frees %llu full, peak %u granules (%u MiB), live %u\n", (unsigned long long)h.allocs, (unsigned long long)h.frees,
	    (unsigned long long)h.full, h.used_peak, h.used_peak / 16, h.live_total);
	/* 5. pool lending: tail reserved (= sweet_pool_reserve_tail_locked) -> the pool allocator never returns an offset >= KEEP */
	static struct sweet_fb_pool p;
	const uint32_t lim = (uint32_t)(SWEET_GFX_HEAP_POOL_KEEP / SWEET_FB_HW_POOL_GRANULE);
	for (uint32_t g = lim; g < SWEET_FB_POOL_NGRAN; g++) p.bits[g >> 6] |= 1ull << (g & 63);
	uint64_t blk = sweet_fb_pool_alloc(&p, SWEET_FB_IOMMU_BLOCK_BYTES);
	assert(blk == 0);                                                           /* IOMMU tables first fit at the pool base */
	static struct { uint64_t off, bytes; } pl[512];
	unsigned np = 0, pool_allocs = 0;
	for (unsigned it = 0; it < 200000; it++) {
		if (np < 512 && rand() % 2) {
			uint64_t bytes = (rand() % 8 == 0) ? 11069184 : (uint64_t)(1 + rand() % 64) * 65536;
			uint64_t off = sweet_fb_pool_alloc(&p, bytes);
			if (off == SWEET_FB_POOL_FAIL) continue;
			assert(off + bytes <= SWEET_GFX_HEAP_POOL_KEEP && pool_surface(SWEET_FB_HW_POOL_BASE + off, bytes, lim));
			pl[np].off = off; pl[np].bytes = bytes; np++; pool_allocs++;
		} else if (np) {
			unsigned k = (unsigned)rand() % np;
			assert(sweet_fb_pool_free(&p, pl[k].off, pl[k].bytes));
			pl[k] = pl[--np];
		}
	}
	for (uint32_t g = lim; g < SWEET_FB_POOL_NGRAN; g++) assert(sweet_fb_pool_test(&p, g));    /* the tail never went back to the pool */
	/* the lent region's PAs are never pool surfaces; the kept part still is */
	assert(!pool_surface(0xc1000000ull, 65536, lim) && !pool_surface(0xcff00000ull, 65536, lim) && !pool_surface(0xc0ff0000ull, 0x20000, lim));
	assert(pool_surface(0xc0000000ull, SWEET_FB_IOMMU_BLOCK_BYTES, lim) && pool_surface(0xc0ff0000ull, 65536, lim));
	assert(pool_surface(0xc1000000ull, 65536, SWEET_FB_POOL_NGRAN));            /* not lent (pool enabled): unchanged 0394 behaviour */
	printf("pool lending: %u pool allocations, all inside the kept %llu MiB; tail bits intact\n", pool_allocs, SWEET_GFX_HEAP_POOL_KEEP >> 20);
	printf("total %u granules = %.2f MiB (SAFE %.2f MiB, new in 0650 %.2f MiB)\nram8_host_test: OK\n", SWEET_GFX_HEAP_NGRAN, total / 1048576.0,
	    safe / 1048576.0, new0650 / 1048576.0);
	return 0;
}
