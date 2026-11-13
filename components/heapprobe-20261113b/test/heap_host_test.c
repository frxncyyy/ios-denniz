/* 0419 host test: the heap region table against LK's mblock_info free list, the live FDT's reserved-memory and our fixed claims. */
#include <assert.h>
#include <stdio.h>
#include "../b/iokit/Kernel/arm/sweet_fb_heap.h"

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

int main(void)
{
	uint64_t total = 0, prev_end = 0;
	for (unsigned r = 0; r < SWEET_FB_HEAP_NREG; r++) {
		const struct sweet_fb_heap_region *g = &sweet_fb_heap_regions[r];
		uint64_t e = g->base + g->size;
		assert(g->size && (g->base % SWEET_FB_HEAP_GRANULE) == 0 && (g->size % SWEET_FB_HEAP_GRANULE) == 0);
		assert(e <= 0x100000000ull && g->base >= prev_end);
		prev_end = e;
		int in = 0;
		for (unsigned i = 0; i < sizeof(mfree) / sizeof(mfree[0]); i++) { in |= g->base >= mfree[i].b && e <= mfree[i].e; }
		assert(in);
		for (unsigned i = 0; i < sizeof(busy) / sizeof(busy[0]); i++) {
			if (g->base < busy[i].e && busy[i].b < e) { printf("region %s overlaps %s\n", g->name, busy[i].n); return 1; }
		}
		bool iova = g->base < 0xb0000000ull && 0x90000000ull < e;
		assert(iova == !!(g->flags & SWEET_FB_HEAP_F_NOPHYS));
		assert(sweet_fb_heap_region_of(g->base, g->size) == (int)r && sweet_fb_heap_region_of(g->base, g->size + 1) == -1);
		assert(sweet_fb_heap_region_of(e - 4096, 4096) == (int)r && sweet_fb_heap_region_of(e - 4096, 8192) != (int)r);
		assert(!sweet_fb_heap_contains(g->base - 4096, 4096) || r > 0);
		total += g->size;
		printf("%-6s %#011llx..%#011llx %8.2f MiB %s%s\n", g->name, (unsigned long long)g->base, (unsigned long long)e, g->size / 1048576.0,
		    g->cls ? "CHECK" : "SAFE", (g->flags & SWEET_FB_HEAP_F_NOPHYS) ? " iommu-only" : "");
	}
	uint32_t n = sweet_fb_heap_total_gran(), g = 0;
	for (unsigned r = 0; r < SWEET_FB_HEAP_NREG; r++) {
		assert(sweet_fb_heap_gran_pa(g) == sweet_fb_heap_regions[r].base);
		g += sweet_fb_heap_ngran(r);
		assert(sweet_fb_heap_gran_pa(g - 1) == sweet_fb_heap_regions[r].base + sweet_fb_heap_regions[r].size - SWEET_FB_HEAP_GRANULE);
	}
	assert(g == n && sweet_fb_heap_gran_pa(n) == ~0ull);
	assert(!sweet_fb_heap_contains(0xc0000000ull, 4096) && !sweet_fb_heap_contains(0x70000000ull, 4096) && !sweet_fb_heap_contains(0x90000000ull, 4096));
	for (uint32_t i = 0; i < n; i++) {     /* canaries differ between granules and between the two spots */
		uint64_t pa = sweet_fb_heap_gran_pa(i);
		assert(sweet_fb_heap_canary(pa, 0) != sweet_fb_heap_canary(pa + SWEET_FB_HEAP_GRANULE, 0));
		assert(sweet_fb_heap_canary(pa, 0) != sweet_fb_heap_canary(pa, 8));
	}
	assert(sweet_fb_heap_verdict(true, 0, 0, 0, 0) == SWEET_FB_HEAP_V_FREE && sweet_fb_heap_verdict(true, 1, 0, 0, 0) == SWEET_FB_HEAP_V_ACTIVE);
	assert(sweet_fb_heap_verdict(true, 0, 1, 0, 0) == SWEET_FB_HEAP_V_BLOCKED && sweet_fb_heap_verdict(false, 0, 0, 0, 0) == SWEET_FB_HEAP_V_UNMAPPED);
	static const int bm[][3] = { {0,0,0},{1,1,0},{2,2,0},{3,-1,0},{4,0,1},{5,1,1},{6,2,1},{7,-1,0},{8,-1,0},{-1,-1,0} };
	for (unsigned i = 0; i < sizeof(bm) / sizeof(bm[0]); i++) {
		assert(sweet_fb_heap_probe_bootarg_mode(bm[i][0]) == bm[i][1] && sweet_fb_heap_probe_bootarg_mpu(bm[i][0]) == bm[i][2]);
	}
	printf("total %u granules = %.2f MiB (+ display pool 256 MiB)\nheap_host_test: OK\n", n, total / 1048576.0);
	return 0;
}
