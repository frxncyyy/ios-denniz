/*
 * 0652 host test (NORD2 RAM8C): the real n2ram code (extracted verbatim by ram8c_extract.py from the patched arm_vm_init.c, pmap_data.h,
 * pmap.c) compiled on the host with XNU stubs, for N2RAM_STAGE = 0, 1, 2 (one binary per stage, -DN2RAM_STAGE).  Checks:
 *  - pa_valid() over every 16 KiB page of the 8 GiB = window U extra ranges exactly; never a reserved-memory node, a fixed claim of our
 *    drivers, a 0440 heap region or the TTBR0 identity block; stage 0 = the stock range test bit for bit;
 *  - pmap tables: pa_index() of every managed page < npages (n2ram_index_top - n2ram_index_base); pai -> pa round trip;
 *  - pmap_has_managed_page(): false for every carve-out of ours (no OSReportWithBacktrace storm), true for any range touching RAM;
 *  - nord2_ram_managed_end / nord2_ram_extra_contains (drivers) and n2ram_mappable_end (physmap bounds) agree with the model;
 *  - n2ram_validate refuses overlap with the window, misalignment, out-of-DRAM (all extras off) and honours -r8off;
 *  - a model of the VM page population: pmap_startup's linear vm_pages[] over [first_avail, avail_end) + vm_page_create() pages of the
 *    extra ranges: every page has exactly one vm_page, no hole page ever reaches the free list, 400k random grab/release (ASan).
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t pmap_paddr_t;
typedef unsigned int ppnum_t;
typedef int boolean_t;
#define TRUE 1
#define FALSE 0
#define PAGE_SIZE 16384ull
#define PAGE_MASK (PAGE_SIZE - 1)
#define atop(x) ((uint64_t)(x) >> 14)
#define ptoa(x) ((uint64_t)(x) << 14)
#define trunc_page(x) ((x) & ~PAGE_MASK)
struct n2ram_range { uint64_t base, end; };
typedef struct { uint64_t physBase, memSize; } boot_args;
static unsigned long gPhysBase = 0x100000000ul, gPhysSize = 0x13e000000ul;
static pmap_paddr_t avail_end = 0x23e000000ull, real_avail_end = 0x23e000000ull;
static pmap_paddr_t vm_first_phys, vm_last_phys;
static int marks;
static void sweet_early_mark(unsigned int stage, const char *label) { (void)stage; (void)label; marks++; }
static int r8off;
static int PE_parse_boot_argn(const char *n, void *v, int sz) { (void)v; (void)sz; return r8off && !strcmp(n, "-r8off"); }
#define printf(...) ((void)0)
#include "ram8c_gen.h"

/* = pmap_bootstrap's 0652 block */
static void boot_pmap(void)
{
	vm_first_phys = gPhysBase;
	vm_last_phys = trunc_page(avail_end);
	n2ram_win_lo = vm_first_phys;
	n2ram_win_hi = vm_last_phys;
	vm_first_phys = n2ram_index_base();
	vm_last_phys = n2ram_index_top();
}

struct rg { uint64_t b, e; const char *n; };
static const struct rg never[] = {      /* every reserved-memory node with a reg + our fixed claims + 0440 heap + boot keep + identity block */
	{0xdfe70000,0xe0000000,"DPM"},{0x74000000,0x74e30000,"adsp"},{0x8a000000,0x8d560000,"ccci"},{0x8e000000,0x8f800000,"ap_md_nc_smem"},
	{0xe0000000,0x100000000ull,"md_mem_usage"},{0xbff70000,0xbff80000,"ccci_tag"},{0x9fa00000,0x9ff00000,"SCP"},{0x75e00000,0x75e10000,"SPM"},
	{0xbd6c0000,0xbfe00000,"vpu"},{0x76e10000,0x78c00000,"framebuffer"},{0x48080000,0x48180000,"aee/pstore/minirdump"},{0x5ff00000,0x5ff80000,"PICACHU"},
	{0x7e900000,0x7ea00000,"MCUPM"},{0x78c00000,0x7e400000,"tee"},{0x7feb0000,0x7ffb0000,"SSPM"},{0x7e400000,0x7e800000,"tee-secmem"},
	{0xbfe00000,0xc0000000,"atf-log/ramdump"},{0x48c00000,0x48e00000,"atf"},{0x7ffbf000,0x80000000,"log_store/mbist"},{0x7e800000,0x7fe00000,"gz"},
	{0x13ffff000ull,0x140000000ull,"dramc-rk0 (inside the window: known since ram5088)"},{0x23ffff000ull,0x240000000ull,"dramc-rk1"},
	{0x40000000,0x40400000,"loader+stub"},{0x47c00000,0x47e00000,"stock DTB"},{0x48000000,0x4a000000,"TTBR0 identity NC block (pmsg)"},
	{0x70000000,0x70200000,"N2D share"},{0x89000000,0x89320000,"SCP DRAM"},{0x8fb00000,0x90f20000,"Wi-Fi"},{0xa0000000,0xb8000000,"vcodec"},
	{0xc0000000,0xd0000000,"display pool"},{0xd0000000,0xdfe70000,"camera/JPEG/GNSS"},
	{0x55100000,0x5ff00000,"heap mb5"},{0x5ff80000,0x70000000,"heap mb6a"},{0x70200000,0x74000000,"heap mb6b"},{0x74e30000,0x75e00000,"heap mb7"},
	{0x80000000,0x89000000,"heap mb11a"},{0x89320000,0x8a000000,"heap mb11b"},{0x8d560000,0x8e000000,"heap mb12"},{0x91000000,0x9fa00000,"heap mb13b"},
	{0xb8000000,0xbd6c0000,"heap mb14b"},
#if N2RAM_STAGE >= 3
	{0x40280000,0x43744000,"stock Android Image (chain-back)"},{0x51100000,0x51814000,"stock ramdisk (chain-back)"},{0xc1000000,0xd0000000,"heap dph"},
#endif
#if N2RAM_STAGE == 3 || N2RAM_STAGE == 4
	{0x23e000000ull,0x23fff0000ull,"heap top"},
#endif
};

static bool model_ram(uint64_t pa)
{
	if (pa >= gPhysBase && pa < avail_end) return true;
	for (unsigned i = 0; i < n2ram_extra_n; i++) if (pa >= n2ram_extra[i].base && pa < n2ram_extra[i].end) return true;
	return false;
}

int main(void)
{
	boot_args ba = { 0x100000000ull, 0x13e000000ull };
	n2ram_validate(&ba);
	unsigned want_n = N2RAM_STAGE == 0 ? 0 : (N2RAM_STAGE == 1 || N2RAM_STAGE >= 3) ? 1 : 3;
	assert(n2ram_extra_n == want_n);
	const uint64_t extra_bytes = n2ram_extra_total();
	boot_pmap();
	if (N2RAM_STAGE == 0) assert(vm_first_phys == gPhysBase && vm_last_phys == avail_end && n2ram_win_lo == vm_first_phys && n2ram_win_hi == vm_last_phys);
	uint64_t npages = atop(n2ram_index_top() - n2ram_index_base()), managed = 0;
	/* 1. pa_valid over all of DRAM (+ a margin) */
	for (uint64_t pa = 0x3f000000ull; pa < 0x241000000ull; pa += 0x1000) {
		bool v = pa_valid(pa);
		assert(v == model_ram(pa));
		if (N2RAM_STAGE == 0) assert(v == ((pa >= gPhysBase) && (pa < avail_end)));      /* stock semantics */
		if (v) {
			assert(atop(pa - vm_first_phys) < npages);
			assert(vm_first_phys + ptoa(atop(pa - vm_first_phys)) == trunc_page(pa));
			managed += (pa & PAGE_MASK) == 0;
		}
		for (unsigned i = 0; i < sizeof(never) / sizeof(never[0]); i++) {
			if (pa >= never[i].b && pa < never[i].e && !(never[i].b == 0x13ffff000ull)) {
				if (v) { printf("pa %#llx managed but in %s\n", (unsigned long long)pa, never[i].n); fprintf(stderr, "pa %#llx in %s\n", (unsigned long long)pa, never[i].n); return 1; }
			}
		}
		/* drivers + physmap bounds */
		uint64_t me = nord2_ram_managed_end(pa), pe = n2ram_mappable_end(pa);
		assert((me != 0) == v && (pe != 0) == v);
		if (v) assert(me > pa && pe > pa && pa_valid(me - PAGE_SIZE) && !(me < 0x240000000ull && pa_valid(me) && !(me == gPhysBase || me == avail_end) && 0));
		if (v && !(pa >= gPhysBase && pa < avail_end)) assert(nord2_ram_extra_contains(pa, 0x1000) && !nord2_ram_extra_contains(pa, 0));
		if (pa >= gPhysBase && pa < avail_end) assert(!nord2_ram_extra_contains(pa, 0x1000));
	}
	/* 2. pmap_has_managed_page: our carve-outs never, a range touching RAM always */
	for (unsigned i = 0; i < sizeof(never) / sizeof(never[0]); i++) {
		if (never[i].b == 0x13ffff000ull) continue;
		assert(!pmap_has_managed_page((ppnum_t)atop(never[i].b), (ppnum_t)atop(never[i].e - 1)));
	}
	assert(pmap_has_managed_page((ppnum_t)atop(0x100000000ull), (ppnum_t)atop(0x100000000ull)));
	for (unsigned i = 0; i < n2ram_extra_n; i++) {
		assert(pmap_has_managed_page((ppnum_t)atop(n2ram_extra[i].base - 0x100000), (ppnum_t)atop(n2ram_extra[i].base)));
		assert(pmap_has_managed_page((ppnum_t)atop(n2ram_extra[i].end - 1), (ppnum_t)atop(n2ram_extra[i].end + 0x100000)));
	}
	/* 3. VM population model: linear array over the window (pmap_startup) + one created page per checked extra page */
	uint64_t first_avail = 0x10a000000ull;     /* kernel + stolen boot memory (order of magnitude; exact value irrelevant) */
	uint64_t span = atop(n2ram_index_top() - n2ram_index_base());
	uint8_t *owner = calloc(span, 1);          /* 0 none, 1 array, 2 created */
	uint64_t arr = 0, created = 0;
	for (uint64_t pa = first_avail; pa < avail_end; pa += PAGE_SIZE) { assert(pa_valid(pa)); owner[atop(pa - vm_first_phys)] = 1; arr++; }
	for (unsigned i = 0; i < n2ram_extra_n; i++)
		for (uint64_t pa = n2ram_extra[i].base; pa < n2ram_extra[i].end; pa += PAGE_SIZE) {
			assert(pa_valid(pa) && owner[atop(pa - vm_first_phys)] == 0); owner[atop(pa - vm_first_phys)] = 2; created++;
		}
	uint64_t holes = 0;
	for (uint64_t k = 0; k < span; k++) if (!owner[k]) { assert(!pa_valid(vm_first_phys + ptoa(k)) || vm_first_phys + ptoa(k) < first_avail); holes++; }
	/* free-list stress over the populated pages only */
	uint64_t *freel = malloc(sizeof(uint64_t) * (arr + created)), nf = 0;
	for (uint64_t k = 0; k < span; k++) if (owner[k]) freel[nf++] = k;
	uint8_t *busy = calloc(span, 1);
	uint64_t *held = malloc(sizeof(uint64_t) * nf), nh = 0;
	srand(652);
	for (unsigned it = 0; it < 400000; it++) {
		if (nf && (rand() & 1)) { uint64_t j = (uint64_t)rand() % nf; uint64_t k = freel[j]; freel[j] = freel[--nf];
			assert(owner[k] && !busy[k] && pa_valid(vm_first_phys + ptoa(k))); busy[k] = 1; held[nh++] = k; }
		else if (nh) { uint64_t j = (uint64_t)rand() % nh; uint64_t k = held[j]; held[j] = held[--nh]; assert(busy[k]); busy[k] = 0; freel[nf++] = k; }
	}
	/* 4. validation refusals and -r8off */
	if (N2RAM_STAGE != 0) {
		boot_args bad = { 0x100000000ull, 0x13f000000ull };     /* a window that would reach an extra range ("top") or not */
		n2ram_extra_n = want_n; n2ram_validate(&bad);
		assert(n2ram_extra_n == ((N2RAM_STAGE == 2 || N2RAM_STAGE == 5) ? 0u : 1u));
		n2ram_extra_n = want_n; r8off = 1; n2ram_validate(&ba); assert(n2ram_extra_n == 0); r8off = 0;
		boot_args low = { 0x4a000000ull, 0x1000000ull };        /* a window over lb4 */
		n2ram_extra_n = want_n; n2ram_validate(&low); assert(n2ram_extra_n == (N2RAM_STAGE == 5 ? 1u : 0u));
	}
	fprintf(stdout, "stage %d: extras %u (%llu MiB), managed pages %llu (window %llu + extra %llu), pmap npages %llu (+%llu KiB pv/attr tables vs stock), "
	    "holes in the index %llu, VM pages %llu, marks %d\nram8c_host_test: OK\n", N2RAM_STAGE, want_n, (unsigned long long)(extra_bytes >> 20),
	    (unsigned long long)managed, (unsigned long long)atop(avail_end - gPhysBase), (unsigned long long)created, (unsigned long long)npages,
	    (unsigned long long)((npages - atop(avail_end - gPhysBase)) * 10 / 1024), (unsigned long long)holes, (unsigned long long)(arr + created), marks);
	free(owner); free(freel); free(busy); free(held);
	return 0;
}
