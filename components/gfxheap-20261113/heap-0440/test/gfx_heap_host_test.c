/* 0440 host test: the graphics-heap region table + allocator. Region table against LK's mblock_info free list, the live FDT's reserved-memory and our fixed claims. */
#include <assert.h>
#include <stdio.h>
#include "../b/iokit/Kernel/arm/sweet_gfx_heap.h"

struct rg { uint64_t b, e; const char *n; };
/* live.dts mblock_info (decoded little-endian: 20 blocks) */
static const struct rg mfree[] = {
	{0x40000000,0x40080000,""},{0x47d00000,0x48080000,""},{0x48180000,0x48200000,""},{0x48600000,0x48c00000,""},{0x48e00000,0x48f00000,""},
	{0x55100000,0x5ff00000,""},{0x5ff80000,0x74000000,""},{0x74e30000,0x75e00000,""},{0x7e841000,0x7e900000,""},{0x7fe00000,0x7feb0000,""},
	{0x7ffb0000,0x7ffbf000,""},{0x80000000,0x8a000000,""},{0x8d560000,0x8e000000,""},{0x8f800000,0x9fa00000,""},{0x9ff00000,0xbd6c0000,""},
	{0xbfe40000,0xbff70000,""},{0xc0000000,0xd0000000,""},{0xd0000000,0xdfe70000,""},{0x100000000,0x13ffff000,""},{0x140000000,0x23ffff000,""},
};
/* reserved-memory with reg (live.dts) + our fixed claims */
static const struct rg busy[] = {
	{0xdfe70000,0xe0000000,"DPM"},{0x74000000,0x74e30000,"adsp"},{0x8a000000,0x8d560000,"ccci"},{0x8e000000,0x8f800000,"ap_md_nc_smem"},
	{0xe0000000,0x100000000,"md_mem_usage"},{0xbff70000,0xbff80000,"ccci_tag"},{0x9fa00000,0x9ff00000,"SCP-reserved"},{0x75e00000,0x75e10000,"SPM"},
	{0xbd6c0000,0xbfe00000,"vpu_binary"},{0x76e10000,0x78c00000,"framebuffer"},{0x48170000,0x48180000,"minirdump"},{0x48090000,0x48170000,"pstore"},
	{0x48080000,0x48090000,"aee"},{0x5ff00000,0x5ff80000,"PICACHU"},{0x7e900000,0x7ea00000,"MCUPM"},{0x78c00000,0x7e400000,"tee"},
	{0x7feb0000,0x7ffb0000,"SSPM"},{0x7e400000,0x7e800000,"tee-secmem"},{0xbfe00000,0xbfe40000,"atf-log"},{0xbff80000,0xc0000000,"atf-ramdump"},
	{0x48c00000,0x48e00000,"atf"},{0x7ffbf000,0x7ffff000,"log_store"},{0x7e800000,0x7e841000,"gz-log"},{0x7ea00000,0x7fe00000,"gz"},
	{0x7ffff000,0x80000000,"emi_mbist"},
	{0x40000000,0x55100000,"loader/stock kernel/ramdisk/payload/LK dtb"},{0x70000000,0x70200000,"N2D share"},{0x89000000,0x89320000,"SCP DRAM"},
	{0x8fb00000,0x8ff50000,"Wi-Fi CONN"},{0x90000000,0x90f20000,"Wi-Fi DMA pool"},{0xa0000000,0xb8000000,"RWVC"},{0xc0000000,0xd0000000,"display pool"},
	{0xd0000000,0xdfe70000,"camera/hwcodec/JPEG"},{0x100000000,0x240000000ull,"XNU windows (3776 / 5088)"},
};

#include <stdlib.h>
#include <string.h>
#include "sweet_fb_iommu.h"
#include "sweet_fb_hw_pool.h"

/* reference model: per granule owner id (0 free) */
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

int main(void)
{
	uint64_t total = 0, prev_end = 0, safe = 0;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
		uint64_t e = g->base + g->size;
		assert(g->size && (g->base % SWEET_GFX_HEAP_GRANULE) == 0 && (g->size % SWEET_GFX_HEAP_GRANULE) == 0);
		assert(e <= 0x100000000ull && g->base >= prev_end);
		prev_end = e;
		int in = 0;
		for (unsigned i = 0; i < sizeof(mfree) / sizeof(mfree[0]); i++) { in |= g->base >= mfree[i].b && e <= mfree[i].e; }
		assert(in);
		for (unsigned i = 0; i < sizeof(busy) / sizeof(busy[0]); i++) {
			if (g->base < busy[i].e && busy[i].b < e) { printf("region %s overlaps %s\n", g->name, busy[i].n); return 1; }
		}
		/* IOMMU: no region in an identity section (an IOVA there would be read as the PA); IOVA-window numbers <=> F_NOPHYS */
		assert(!(g->base < SWEET_FB_IOMMU_ID_FB_END && SWEET_FB_IOMMU_ID_FB_BASE < e));
		assert(!(g->base < SWEET_FB_IOMMU_ID_LOW_END && SWEET_FB_IOMMU_ID_LOW_BASE < e));
		bool iova = g->base < SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE && SWEET_FB_IOMMU_WIN_BASE < e;
		assert(iova == !!(g->flags & SWEET_GFX_HEAP_F_NOPHYS));
		assert(!(iova && g->cls == SWEET_GFX_HEAP_SAFE));       /* the IOVA-numbered region is never on by default */
		assert(!sweet_fb_hw_pool_contains(g->base, 4096) && !sweet_fb_hw_pool_contains(e - 4096, 4096));
		assert(sweet_gfx_heap_region_of(g->base, g->size) == (int)r && sweet_gfx_heap_region_of(g->base, g->size + 1) == -1);
		assert(sweet_gfx_heap_region_of(e - 4096, 4096) == (int)r && sweet_gfx_heap_region_of(e - 4096, 8192) != (int)r);
		total += g->size; safe += g->cls == SWEET_GFX_HEAP_SAFE ? g->size : 0;
		gfirst[r] = sweet_gfx_heap_first(r); gfirst[r + 1] = gfirst[r] + sweet_gfx_heap_ngran(r);
		printf("%-6s %#011llx..%#011llx %8.2f MiB %s%s\n", g->name, (unsigned long long)g->base, (unsigned long long)e, g->size / 1048576.0,
		    g->cls ? "CHECK" : "SAFE", (g->flags & SWEET_GFX_HEAP_F_NOPHYS) ? " iommu-only" : "");
	}
	assert(gfirst[SWEET_GFX_HEAP_NREG] == SWEET_GFX_HEAP_NGRAN);
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		assert(sweet_gfx_heap_gran_pa(gfirst[r]) == sweet_gfx_heap_regions[r].base);
		assert(sweet_gfx_heap_gran_pa(gfirst[r + 1] - 1) == sweet_gfx_heap_regions[r].base + sweet_gfx_heap_regions[r].size - SWEET_GFX_HEAP_GRANULE);
	}
	assert(sweet_gfx_heap_gran_pa(SWEET_GFX_HEAP_NGRAN) == ~0ull);
	for (uint32_t i = 0; i + 1 < SWEET_GFX_HEAP_NGRAN; i++) {
		uint64_t pa = sweet_gfx_heap_gran_pa(i);
		assert(sweet_gfx_heap_canary(pa) != sweet_gfx_heap_canary(sweet_gfx_heap_gran_pa(i + 1)));
	}
	/* allowed masks: default = SAFE only; check adds CHECK; off/admin masks subtract */
	uint32_t safe_mask = sweet_gfx_heap_allowed(1, 0, SWEET_GFX_HEAP_ALL, 0);
	assert(safe_mask == 0x11f && sweet_gfx_heap_allowed(1, 1, SWEET_GFX_HEAP_ALL, 0) == 0x1ff && sweet_gfx_heap_allowed(0, 1, 0x1ff, 0) == 0);
	assert(sweet_gfx_heap_allowed(1, 1, 0x1ff, 0x80) == 0x17f && sweet_gfx_heap_allowed(1, 0, 0x003, 0x001) == 0x002);
	assert(sweet_gfx_heap_gpu_eligible(2, 65536, 65536) && !sweet_gfx_heap_gpu_eligible(2, 61440, 65536) && !sweet_gfx_heap_gpu_eligible(3, 1 << 20, 65536) &&
	    !sweet_gfx_heap_gpu_eligible(6, 1 << 20, 65536) && !sweet_gfx_heap_gpu_eligible(0, 1 << 20, 65536));

	static struct sweet_gfx_heap h;
	unsigned rr = 99;
	/* a full-screen surface (1125x2436, pitch 4544 = 11,069,184 bytes = 169 granules) lands at the start of mb5 */
	uint32_t g0 = sweet_gfx_heap_alloc(&h, 11069184, safe_mask, ~0u, &rr);
	assert(g0 == 0 && rr == 0 && h.used[0] == 169 && h.live_total == 1);
	assert(sweet_gfx_heap_is_allocated(&h, sweet_gfx_heap_gran_pa(g0), 11069184) && !sweet_gfx_heap_is_allocated(&h, sweet_gfx_heap_gran_pa(g0), 12 << 20));
	assert(!sweet_gfx_heap_is_allocated(&h, sweet_gfx_heap_gran_pa(g0) + 4096, 65536) && !sweet_gfx_heap_is_allocated(&h, 0xc0000000ull, 65536));
	assert(sweet_gfx_heap_free(&h, sweet_gfx_heap_gran_pa(g0), 11069184) && h.used_total == 0 && h.live_total == 0);
	assert(!sweet_gfx_heap_is_allocated(&h, sweet_gfx_heap_gran_pa(g0), 11069184));
	assert(!sweet_gfx_heap_free(&h, sweet_gfx_heap_gran_pa(g0), 11069184) && h.bad_free == 1);          /* double free */
	assert(!sweet_gfx_heap_free(&h, 0xc0000000ull, 65536) && !sweet_gfx_heap_free(&h, 0x55100000ull + 4096, 65536)); /* not heap / misaligned */
	/* never across a region boundary: fill mb7 (253 granules) but for 1, ask 2 with only mb7+mb11a allowed -> mb11a */
	uint32_t a = sweet_gfx_heap_alloc(&h, 252ull * 65536, 1u << 3, ~0u, &rr); assert(a == gfirst[3] && rr == 3);
	uint32_t b2 = sweet_gfx_heap_alloc(&h, 2ull * 65536, (1u << 3) | (1u << 4), ~0u, &rr); assert(b2 == gfirst[4] && rr == 4);
	uint32_t c1 = sweet_gfx_heap_alloc(&h, 65536, (1u << 3) | (1u << 4), ~0u, &rr); assert(c1 == gfirst[3] + 252 && rr == 3);
	assert(sweet_gfx_heap_alloc(&h, 65536, 1u << 3, ~0u, &rr) == SWEET_GFX_HEAP_FAIL);                  /* mb7 full */
	/* larger than any allowed region fails; cap limits; mask 0 fails */
	assert(sweet_gfx_heap_alloc(&h, 0x10080000ull + 65536, SWEET_GFX_HEAP_ALL, ~0u, &rr) == SWEET_GFX_HEAP_FAIL);
	assert(sweet_gfx_heap_alloc(&h, 0x10080000ull, 0x2, ~0u, &rr) == gfirst[1]);                        /* exactly a whole region */
	assert(sweet_gfx_heap_free(&h, sweet_gfx_heap_gran_pa(gfirst[1]), 0x10080000ull));
	assert(sweet_gfx_heap_alloc(&h, 65536, 0, ~0u, &rr) == SWEET_GFX_HEAP_FAIL);
	assert(sweet_gfx_heap_alloc(&h, 65536, SWEET_GFX_HEAP_ALL, h.used_total, &rr) == SWEET_GFX_HEAP_FAIL);
	assert(sweet_gfx_heap_alloc(&h, 65536, SWEET_GFX_HEAP_ALL, h.used_total + 1, &rr) != SWEET_GFX_HEAP_FAIL);
	memset(&h, 0, sizeof(h));
	/* random stress against the model; every allocation inside one region, contiguous, CHECK regions only with their bit */
	srand(4402);
	struct { uint64_t pa, bytes; uint32_t id; } live[4096];
	unsigned nlive = 0; uint32_t next_id = 1;
	for (unsigned it = 0; it < 400000; it++) {
		if (nlive < 4096 && (rand() % 100) < 55) {
			uint64_t bytes = (rand() % 4 == 0) ? 11069184 : ((uint64_t)(1 + rand() % 300) * 4096 * (1 + rand() % 16));
			uint32_t mask = (rand() % 3 == 0) ? 0x1ff : safe_mask;
			uint32_t cap = (rand() % 10 == 0) ? (uint32_t)(rand() % SWEET_GFX_HEAP_NGRAN) : ~0u;
			uint32_t used0 = h.used_total;
			uint32_t g = sweet_gfx_heap_alloc(&h, bytes, mask, cap, &rr);
			uint32_t n = sweet_gfx_heap_granules(bytes);
			if (g == SWEET_GFX_HEAP_FAIL) {
				/* only when no allowed region has a free run of n (or the cap says no) */
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
	printf("total %u granules = %.2f MiB (SAFE %.2f MiB)\ngfx_heap_host_test: OK\n", SWEET_GFX_HEAP_NGRAN, total / 1048576.0, safe / 1048576.0);
	return 0;
}
