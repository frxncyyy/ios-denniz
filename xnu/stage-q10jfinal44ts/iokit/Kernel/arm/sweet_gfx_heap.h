/*
 * 0440 (gfxheap-20261113): the low-bank GRAPHICS HEAP -- region table and allocator, pure (no kernel calls; the host test
 * gfxheap-20261113/heap-0440/test/gfx_heap_host_test.c runs exactly this code).  Kernel part: sweet_gfx_heap_kern.inc (SweetSurface.cpp).
 *
 * Why: XNU's own window is 5088 MiB at 0x1_00000000; the phone has 8 GiB.  The free holes of the low bank (LK mblock_info free blocks,
 * outside every reserved-memory node and every fixed claim of our code, region table = 0419's) hold wired graphics memory instead of
 * XNU pages: displayed full-screen BGRA IOSurfaces and GPU-only buffers.  XNU keeps every page of its window for pageable memory.
 * Probe result (0419, image q10jgx, mode 2 with canary writes, 10 min of normal use): all 9 regions FREE, canary_bad 0, restore_bad 0,
 * late 0.  The probe only proves CPU access; GPU / display (M4U) / EMI MPU per-master permissions are NOT proven by it.
 *
 * Classes: SAFE = inside an LK free block, no claim of unknown extent nearby (on by default with kern.n2heap = 1); CHECK = next to a
 * claim whose real extent is not known (SCP firmware, ccci, Wi-Fi): used only with kern.n2heap_check = 1.
 * F_NOPHYS (mb13b): its PAs have the same numbers as the display IOMMU's IOVA window 0x90000000..0xB0000000.  Heap surfaces are never
 * pool surfaces (sweet_surface_pool_pa_obj() = 0), so the overlay planner only ever sees them through an IOVA mapping (PTE -> PA);
 * a heap PA is never programmed into an OVL address register.  The window has no identity sections, so no IOVA == PA aliasing exists.
 *
 * Allocator: one bitmap of 64 KiB granules over all regions (global numbering = regions concatenated in table order), first fit that
 * restarts at every region start, so no allocation ever spans two regions (each allocation is ONE physically contiguous range).
 *
 * 0650 (ram8-20261114, "all 8 GiB"): four more regions at the END of the table (first fit = the proven 0440 regions fill first):
 *   dph  0xC1000000..0xD0000000  the display pool's upper 240 MiB.  The pool (0394) is off by default since 0416 and then holds only the
 *        580 KiB display-IOMMU table block (first fit at 0xC0000000).  Flag F_POOLHI: the region is set up only if the pool is NOT
 *        enabled at boot (n2fbhw_pool/n2fp), not write-back (n2fbpoolc) and not disabled by its own canary; the pool is then limited to
 *        its first 16 MiB for the rest of the boot (SweetSurface.cpp sweet_pool_lend_hi), so pool and heap never share a granule.
 *   top  0x23E000000..0x23FFF0000 the 32 MiB above XNU's 5088 MiB window minus the last 64 KiB (mblock-2-dramc-rk1 page 0x23FFFF000).
 *        Above 4 GiB: reached by the GPU (40-bit PA) and the display IOMMU (34-bit PA) only -- heap surfaces never go to a physical OVL.
 *   lb4  0x49000000..0x55100000  LK's boot buffers (kernel decompress buffer, ramdisk at 0x51100000): Linux RAM, dead after the
 *        SWEETXNU loader copied the payload; 1 MiB margin above atf-reserved (ends 0x48E00000) and LK's free block 0x48E00000..0x48F00000.
 *   lb0  0x40400000..0x47C00000  the loader's own boot area: Image/stub at 0x40080000/0x40280000 (kept: chain-back target), SWEETXNU
 *        payload 0x43744000 (copied into XNU's window before __start), stock DTB 0x47C80000 (kept: 0x47C00000..0x47E00000, the Wi-Fi
 *        driver maps 1 MiB from it at run time).
 * 0651 (stage b): the CHECK regions mb11b/mb12/mb13b ON by default (kern.n2heap_check = 1), each with a guard band to the neighbour whose real
 * extent is not known (SCP DRAM / ccci / ap_md_nc_smem / SCP-reserved).  0419 probed exactly these ranges FREE (canary, 10 min of use);
 * the 30 s scrub still switches a region off at the first foreign write.
 * 0653 (gpusched-20261114): checked against the live DT (dtb/nord2/live.dtb): mb11b, mb12 and mb13b lie inside LK's mblock_info free list
 * (0x80000000..0x8A000000, 0x8D560000..0x8E000000, 0x8F800000..0x9FA00000 = RAM LK hands to Linux as System RAM) and outside every
 * reserved-memory node with a reg; the Linux-only dynamic reservations (scp/sspm/adsp/mcupm share, consys, wifi, gps, ssmr) are not
 * allocated under iOS except by our own drivers (SCP DRAM 0x89000000, CONN 0x8FB00000, Wi-Fi 0x90000000, all outside).  Every edge to the
 * modem (ccci, ap_md_nc_smem) and to SCP-reserved now keeps >= 0.6 MiB: mb11b ends 1 MiB below ccci (was flush against it).
 * Everything else of the 8 GiB is firmware (TEE, ATF, GenieZone, SSPM/MCUPM/SPM/SCP/ADSP/DPM/PICACHU, vpu, pstore), the running modem
 * (ccci, ap_md_nc_smem, md_mem_usage 0xE0000000..0x100000000), the LK framebuffer, or a device pool of ours (Wi-Fi, vcodec, cameras,
 * JPEG, GNSS, DVFS/SCP shares): ram8-20261114/tools/memmap.py prints the full table and checks this one against it.
 */
#ifndef SWEET_GFX_HEAP_H
#define SWEET_GFX_HEAP_H

#include <stdint.h>
#include <stdbool.h>

#define SWEET_GFX_HEAP_GRANULE  0x10000ull      /* 64 KiB, = SWEET_FB_HW_POOL_GRANULE = SWEET_FB_IOMMU_GRANULE */
#define SWEET_GFX_HEAP_NREG     13u
#define SWEET_GFX_HEAP_SAFE     0u
#define SWEET_GFX_HEAP_CHECK    1u
#define SWEET_GFX_HEAP_F_NOPHYS 1u              /* never a physical OVL address (IOVA window numbers) */
#define SWEET_GFX_HEAP_F_POOLHI 2u              /* 0650: the display pool's upper part: only while the pool is limited to SWEET_GFX_HEAP_POOL_KEEP */
#define SWEET_GFX_HEAP_POOL_KEEP 0x1000000ull   /* 0650: bytes the display pool keeps (from its base) when it lends its upper part */
#define SWEET_GFX_HEAP_NGRAN    25232u          /* sum of the regions' granules (host test + check in sweet_heap_start); 0651: 25248, 0650: 25304, 0440: 15945 */
#define SWEET_GFX_HEAP_WORDS    ((SWEET_GFX_HEAP_NGRAN + 63u) / 64u)
#define SWEET_GFX_HEAP_FAIL     0xffffffffu
#define SWEET_GFX_HEAP_ALL      ((1u << SWEET_GFX_HEAP_NREG) - 1u)

struct sweet_gfx_heap_region {
	uint64_t    base, size;
	uint32_t    cls, flags;
	const char *name;
};

static const struct sweet_gfx_heap_region sweet_gfx_heap_regions[SWEET_GFX_HEAP_NREG] = {
	{ 0x55100000ull, 0x0ae00000ull, SWEET_GFX_HEAP_SAFE,  0,                       "mb5"   },   /* 174 MiB  (Linux: dram_r0_dummy_read) */
	{ 0x5ff80000ull, 0x10080000ull, SWEET_GFX_HEAP_SAFE,  0,                       "mb6a"  },   /* 256.5 MiB, PICACHU end .. DVFS share */
	{ 0x70200000ull, 0x03e00000ull, SWEET_GFX_HEAP_SAFE,  0,                       "mb6b"  },   /* 62 MiB, DVFS share end .. adsp */
	{ 0x74e30000ull, 0x00fd0000ull, SWEET_GFX_HEAP_SAFE,  0,                       "mb7"   },   /* 15.8 MiB, adsp end .. SPM */
	{ 0x80000000ull, 0x09000000ull, SWEET_GFX_HEAP_SAFE,  0,                       "mb11a" },   /* 144 MiB, .. SCP DRAM */
	{ 0x89400000ull, 0x00b00000ull, SWEET_GFX_HEAP_CHECK, 0,                       "mb11b" },   /* 0653: 11 MiB (SCP DRAM end + 0.9 MiB guard .. ccci - 1 MiB) */
	{ 0x8d600000ull, 0x00900000ull, SWEET_GFX_HEAP_CHECK, 0,                       "mb12"  },   /* 0651: 9 MiB (ccci end + 0.6 MiB .. ap_md_nc_smem - 1 MiB) */
	{ 0x91000000ull, 0x0e900000ull, SWEET_GFX_HEAP_CHECK, SWEET_GFX_HEAP_F_NOPHYS, "mb13b" },   /* 0651: 233 MiB, Wi-Fi pool end (+16 MiB) .. SCP-reserved - 1 MiB */
	{ 0xb8000000ull, 0x056c0000ull, SWEET_GFX_HEAP_SAFE,  0,                       "mb14b" },   /* 86.75 MiB, RWVC end .. vpu_binary */
	{ 0xc1000000ull, 0x0f000000ull, SWEET_GFX_HEAP_SAFE,  SWEET_GFX_HEAP_F_POOLHI, "dph"   },   /* 0650: 240 MiB, display pool + 16 MiB .. cameras */
	{ 0x23e000000ull, 0x01ff0000ull, SWEET_GFX_HEAP_SAFE, 0,                       "top"   },   /* 0650: 31.9 MiB, XNU window end .. dramc-rk1 granule */
	{ 0x49000000ull, 0x0c100000ull, SWEET_GFX_HEAP_SAFE,  0,                       "lb4"   },   /* 0650: 193 MiB, LK boot buffers/ramdisk .. mb5 */
	{ 0x40400000ull, 0x07800000ull, SWEET_GFX_HEAP_SAFE,  0,                       "lb0"   },   /* 0650: 120 MiB, loader+stub (kept) .. stock DTB (kept) */
};

static inline uint32_t
sweet_gfx_heap_ngran(unsigned r)
{
	return (uint32_t)(sweet_gfx_heap_regions[r].size / SWEET_GFX_HEAP_GRANULE);
}

/* First global granule of region r. */
static inline uint32_t
sweet_gfx_heap_first(unsigned r)
{
	uint32_t g = 0;
	for (unsigned i = 0; i < r && i < SWEET_GFX_HEAP_NREG; i++) {
		g += sweet_gfx_heap_ngran(i);
	}
	return g;
}

/* Region holding all of [pa, pa + len), or -1. */
static inline int
sweet_gfx_heap_region_of(uint64_t pa, uint64_t len)
{
	if (len == 0) {
		return -1;
	}
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
		if (pa >= g->base && len <= g->size && pa - g->base <= g->size - len) {
			return (int)r;
		}
	}
	return -1;
}

/* Region mask a creation may use: SAFE regions with on, CHECK regions only with check too; minus the off and admin masks. */
static inline uint32_t
sweet_gfx_heap_allowed(int on, int check, uint32_t admin_mask, uint32_t off_mask)
{
	if (!on) {
		return 0;
	}
	uint32_t m = 0;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		if (sweet_gfx_heap_regions[r].cls == SWEET_GFX_HEAP_SAFE || check) {
			m |= 1u << r;
		}
	}
	return m & admin_mask & ~off_mask & SWEET_GFX_HEAP_ALL;
}

struct sweet_gfx_heap {
	uint64_t bits[SWEET_GFX_HEAP_WORDS];    /* 1 = granule in use (or quarantined) */
	uint32_t used[SWEET_GFX_HEAP_NREG];     /* granules in use per region */
	uint32_t live[SWEET_GFX_HEAP_NREG];     /* allocations per region */
	uint32_t used_total, used_peak, live_total;
	uint64_t allocs, frees, full, bad_free;
};

static inline uint32_t
sweet_gfx_heap_granules(uint64_t bytes)
{
	return (uint32_t)((bytes + SWEET_GFX_HEAP_GRANULE - 1) / SWEET_GFX_HEAP_GRANULE);
}

static inline bool
sweet_gfx_heap_test(const struct sweet_gfx_heap *h, uint32_t g)
{
	return (h->bits[g >> 6] >> (g & 63)) & 1;
}

static inline void
sweet_gfx_heap_set(struct sweet_gfx_heap *h, uint32_t g0, uint32_t n, bool on)
{
	for (uint32_t k = g0; k < g0 + n; k++) {
		if (on) {
			h->bits[k >> 6] |= 1ull << (k & 63);
		} else {
			h->bits[k >> 6] &= ~(1ull << (k & 63));
		}
	}
}

/* Global granule -> PA (~0 past the end). */
static inline uint64_t
sweet_gfx_heap_gran_pa(uint32_t g)
{
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		uint32_t n = sweet_gfx_heap_ngran(r);
		if (g < n) {
			return sweet_gfx_heap_regions[r].base + (uint64_t)g * SWEET_GFX_HEAP_GRANULE;
		}
		g -= n;
	}
	return ~0ull;
}

/* Longest free run of region r (granules, for logs). */
static inline uint32_t
sweet_gfx_heap_largest_free(const struct sweet_gfx_heap *h, unsigned r)
{
	uint32_t g0 = sweet_gfx_heap_first(r), n = sweet_gfx_heap_ngran(r), best = 0, run = 0;
	for (uint32_t g = g0; g < g0 + n; g++) {
		if (sweet_gfx_heap_test(h, g)) {
			run = 0;
		} else if (++run > best) {
			best = run;
		}
	}
	return best;
}

/*
 * First fit of bytes in the regions of mask (table order), never across a region boundary.  Returns the first global granule, or
 * SWEET_GFX_HEAP_FAIL (counted in full).  cap_gran: the global cap (granules in use after this allocation must stay <= cap_gran).
 */
static inline uint32_t
sweet_gfx_heap_alloc(struct sweet_gfx_heap *h, uint64_t bytes, uint32_t mask, uint32_t cap_gran, unsigned *region_out)
{
	uint32_t n = sweet_gfx_heap_granules(bytes);
	if (bytes == 0 || n > SWEET_GFX_HEAP_NGRAN || h->used_total + n > cap_gran) {
		h->full++;
		return SWEET_GFX_HEAP_FAIL;
	}
	uint32_t base = 0;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; base += sweet_gfx_heap_ngran(r), r++) {
		uint32_t rn = sweet_gfx_heap_ngran(r);
		if (!((mask >> r) & 1u) || n > rn - h->used[r]) {
			continue;
		}
		uint32_t start = base, run = 0;
		for (uint32_t g = base; g < base + rn; g++) {
			if ((g & 63) == 0 && g + 64 <= base + rn && h->bits[g >> 6] == ~0ull) {     /* whole word in use */
				run = 0;
				g += 63;
				continue;
			}
			if (sweet_gfx_heap_test(h, g)) {
				run = 0;
				continue;
			}
			if (run++ == 0) {
				start = g;
			}
			if (run == n) {
				sweet_gfx_heap_set(h, start, n, true);
				h->used[r] += n;
				h->live[r]++;
				h->used_total += n;
				h->live_total++;
				if (h->used_total > h->used_peak) {
					h->used_peak = h->used_total;
				}
				h->allocs++;
				if (region_out != NULL) {
					*region_out = r;
				}
				return start;
			}
		}
	}
	h->full++;
	return SWEET_GFX_HEAP_FAIL;
}

/* True iff [pa, pa + bytes) is exactly the kind of range sweet_gfx_heap_alloc hands out and every granule of it is in use (the kernel
 * checks this before writing the free canaries, so a bad give-back never writes into another allocation). */
static inline bool
sweet_gfx_heap_is_allocated(const struct sweet_gfx_heap *h, uint64_t pa, uint64_t bytes)
{
	uint32_t n = sweet_gfx_heap_granules(bytes);
	int r = bytes != 0 ? sweet_gfx_heap_region_of(pa, (uint64_t)n * SWEET_GFX_HEAP_GRANULE) : -1;
	if (r < 0 || ((pa - sweet_gfx_heap_regions[r].base) % SWEET_GFX_HEAP_GRANULE) != 0 || h->live[r] == 0 || h->used[r] < n) {
		return false;
	}
	uint32_t g0 = sweet_gfx_heap_first((unsigned)r) + (uint32_t)((pa - sweet_gfx_heap_regions[r].base) / SWEET_GFX_HEAP_GRANULE);
	for (uint32_t k = g0; k < g0 + n; k++) {
		if (!sweet_gfx_heap_test(h, k)) {
			return false;
		}
	}
	return true;
}

/* Give back what sweet_gfx_heap_alloc(bytes) returned (as a PA).  Misaligned / out of the regions / not allocated: nothing changes. */
static inline bool
sweet_gfx_heap_free(struct sweet_gfx_heap *h, uint64_t pa, uint64_t bytes)
{
	uint32_t n = sweet_gfx_heap_granules(bytes);
	int r = bytes != 0 ? sweet_gfx_heap_region_of(pa, (uint64_t)n * SWEET_GFX_HEAP_GRANULE) : -1;
	if (r < 0 || ((pa - sweet_gfx_heap_regions[r].base) % SWEET_GFX_HEAP_GRANULE) != 0 || h->live[r] == 0 || h->used[r] < n) {
		h->bad_free++;
		return false;
	}
	uint32_t g0 = sweet_gfx_heap_first((unsigned)r) + (uint32_t)((pa - sweet_gfx_heap_regions[r].base) / SWEET_GFX_HEAP_GRANULE);
	for (uint32_t k = g0; k < g0 + n; k++) {
		if (!sweet_gfx_heap_test(h, k)) {
			h->bad_free++;
			return false;
		}
	}
	sweet_gfx_heap_set(h, g0, n, false);
	h->used[r] -= n;
	h->live[r]--;
	h->used_total -= n;
	h->live_total--;
	h->frees++;
	return true;
}

/* Canary in the first 16 bytes of every FREE granule (setup, free; checked at alloc and by the 30 s scrub).  PA-derived: a write that
 * lands on another granule (address aliasing) never matches. */
static inline uint64_t
sweet_gfx_heap_canary(uint64_t pa)
{
	uint64_t x = pa ^ 0x4e32474658480000ull;
	x ^= x >> 31; x *= 0x9e3779b97f4a7c15ull; x ^= x >> 29;
	return x;
}

static inline void
sweet_gfx_heap_canary_put(volatile uint64_t *head, uint64_t pa)
{
	head[0] = sweet_gfx_heap_canary(pa);
	head[1] = ~sweet_gfx_heap_canary(pa);
}

static inline bool
sweet_gfx_heap_canary_ok(const volatile uint64_t *head, uint64_t pa)
{
	return head[0] == sweet_gfx_heap_canary(pa) && head[1] == ~sweet_gfx_heap_canary(pa);
}

/* GPU-only buffer placement: exactly flags 2 (GPU-only, not executable, not write-combined) and at least min_bytes. */
static inline bool
sweet_gfx_heap_gpu_eligible(uint32_t flags, uint64_t bytes, uint64_t min_bytes)
{
	return flags == 2u && bytes >= min_bytes && (bytes % 4096u) == 0;
}

#endif /* SWEET_GFX_HEAP_H */
