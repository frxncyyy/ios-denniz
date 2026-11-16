/*
 * 0440 (gfxheap-20261113): the low-bank graphics heap, KERNEL part.  Included ONCE by SweetSurface.cpp after the 0394 pool code and
 * before sweet_fb_iommu_hw.h (n2io_ram accepts heap pages).  Table + allocator: sweet_gfx_heap.h (pure, host-tested).
 *
 * What goes there (nothing else):
 *   - IOSurfaces of the pool's eligibility AND admission policy (full-screen 'BGRA', 1x1 elements, no planes, pitch % 64, offset 0,
 *     kern.n2fbhw_pool_policy) that did not go to the display pool (default: the pool is off since 0416, so all of them);
 *   - GPU-only buffers (Nord2GPU ALLOC flags == 2) of >= 64 KiB (0441, sweet_heap_gpu_take / _give).
 * Heap memory is NOT XNU memory: it is not counted in sweet_surface_wired, the 0417 per-process / combined budgets or the 0418 caps,
 * and Nord2GPU (0441) leaves heap buffers out of its global private total.  Own cap: kern.n2heap_cap_mib.
 * Mapping: each region ONCE, write-combined, with ml_io_map_unmappable (kernel_map; never io_submap: the 8 MiB io_submap is nearly full
 * and its KMA_NOFAIL exhaustion was the invisible panic of 0410).  User mappings: withPhysicalAddress descriptors, kIOMapWriteCombineCache
 * (as pool surfaces).  Canary in the head of every free granule: written at setup (a region with a bad readback is switched off for the
 * boot), checked at every allocation and by a 30 s scrub of the free granules (a foreign write switches that region off for new
 * allocations; what it already holds stays).  An M4U fault whose PA lies in a region switches that region off as well.
 * Display: a heap surface is never a pool surface (sweet_surface_pool_pa_obj() = 0), so it is only ever shown through the display IOMMU
 * (IOVA window 0x90000000..0xB0000000 -> PTEs with the heap PA); never as a physical OVL address (mb13b has IOVA-window numbers).
 * 0406 eviction and 0417 purge only touch pool / IOBufferMemoryDescriptor surfaces, so heap surfaces are neither evicted nor purged:
 * their range goes back to the heap in SweetSurface::free (once nothing holds the descriptor), as pool ranges do.
 * Locks: sweet_heap_spin (spin) = the bitmap + counters; sweet_heap_setup_lock (IOLock) = region setup (thread call only).
 * Sysctls (no boot-arg): kern.n2heap 0 off / 1 on (default: the SAFE regions), kern.n2heap_check 1 = also the CHECK regions,
 * kern.n2heap_gpu 0 = no GPU buffers, kern.n2heap_mask (admin region mask), kern.n2heap_cap_mib, read-only stats.
 * Log: "NORD2 HEAP:" (setup per region, allocation/free samples, foreign writes, one stats line with used/free per region every 60 s
 * when something changed and every 10 min regardless).
 * 0650 (ram8-20261114): regions dph/top/lb4/lb0 (sweet_gfx_heap.h), cap 2048 MiB.  They are set up ONE per tick, each after a
 * "NORD2 HEAP: 0650 region ... setup begins" line and a 1 s pause (the USB live log / persistent console holds the line if the first
 * touch hangs).  dph only after sweet_pool_lend_hi() limited the display pool to its first 16 MiB.  At start the loader's live
 * addresses (/chosen nord2-stock-entry = chain-back stub, nord2-stock-dtb = the DTB the Wi-Fi driver maps 1 MiB of) are checked
 * against every region: an overlap switches that region off for the boot.  "NORD2 RAM8:" = the physical memory plan, once.
 */
#include "sweet_gfx_heap.h"
extern "C" {
extern unsigned long gPhysBase, gPhysSize;     /* = sweet_fb_iommu_hw.h */
}

#define SWEET_HEAP_WIMG_WCOMB          0x6u    /* VM_WIMG_WCOMB (= SWEET_POOL_WIMG_WCOMB) */
#define SWEET_HEAP_GPU_MIN              0x10000ull
#define SWEET_HEAP_TICK_S               30u

static int           sweet_heap_on = 1;         /* kern.n2heap */
static int           sweet_heap_check = 1;      /* kern.n2heap_check (0651: default 1 = the CHECK regions too; 0440/0650: 0) */
static int           sweet_heap_gpu = 1;        /* kern.n2heap_gpu */
static uint32_t      sweet_heap_mask = SWEET_GFX_HEAP_ALL;      /* kern.n2heap_mask */
static uint32_t      sweet_heap_cap_mib = 2048;  /* kern.n2heap_cap_mib (0650: all 13 regions = 1581 MiB; 0440: 1024 for 996) */
static uint32_t      sweet_heap_ready;          /* regions mapped + canary ok (bit r) */
static uint32_t      sweet_heap_off;            /* regions off for this boot (setup canary, foreign write, M4U fault) */
static IOSimpleLock *sweet_heap_spin;
static IOLock       *sweet_heap_setup_lock;
static struct sweet_gfx_heap sweet_heap;        /* under sweet_heap_spin */
static volatile uint8_t *sweet_heap_kva[SWEET_GFX_HEAP_NREG];
static uint32_t      sweet_heap_used_kib, sweet_heap_live, sweet_heap_full, sweet_heap_foreign, sweet_heap_leaked;
static uint32_t      sweet_heap_surf_live, sweet_heap_gpu_live, sweet_heap_logged, sweet_heap_told_full;
static uint32_t      sweet_heap_log_at, sweet_heap_log_sig;
static thread_call_t sweet_heap_tc;
static bool          sweet_heap_plan_logged;    /* 0650 */
#define SWEET_HEAP_FIRST_0650           9u      /* 0650: regions 9.. are new (sweet_gfx_heap.h) */
_Static_assert(SWEET_GFX_HEAP_NREG == 13u && SWEET_HEAP_FIRST_0650 < SWEET_GFX_HEAP_NREG, "0650 region table");
SYSCTL_INT(_kern, OID_AUTO, n2heap, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_heap_on, 0, "0440: low-bank graphics heap: 0 off, 1 on (SAFE regions)");
SYSCTL_INT(_kern, OID_AUTO, n2heap_check, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_heap_check, 0, "0440: 1 = also the CHECK regions mb11b mb12 mb13b");
SYSCTL_INT(_kern, OID_AUTO, n2heap_gpu, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_heap_gpu, 0, "0440: 1 = GPU-only buffers >= 64 KiB from the heap");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_mask, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_heap_mask, 0, "0440: region mask (bit = table index)");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_cap_mib, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_heap_cap_mib, 0, "0440: heap MiB in use at most");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_ready, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_heap_ready, 0, "0440: regions mapped and checked");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_off, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_heap_off, 0, "0440: regions switched off for this boot");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_used_kib, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_heap_used_kib, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_live, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_heap_live, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_full, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_heap_full, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, n2heap_foreign, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_heap_foreign, 0, "");

static inline void
sweet_heap_wc_drain(void)
{
	__builtin_arm_dsb(0xf);                 /* WC: drain the write buffers before a master (GPU/OVL) or the next check reads */
}

static uint32_t
sweet_heap_ready_mask(void)
{
	return __atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE) & ~__atomic_load_n(&sweet_heap_off, __ATOMIC_RELAXED);
}

/* Kernel VA of [pa, pa + len) when it lies in a ready region (an off region keeps its mapping: its allocations stay valid). */
static volatile uint8_t *
sweet_heap_va(uint64_t pa, uint64_t len)
{
	int r = sweet_gfx_heap_region_of(pa, len);
	if (r < 0 || !((__atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE) >> r) & 1u)) {
		return NULL;
	}
	return sweet_heap_kva[r] + (pa - sweet_gfx_heap_regions[r].base);
}

/* For n2io_ram (0401) and the GPU import (0441): a 4 KiB page of a mapped region. */
static bool
sweet_heap_page_ok(uint64_t pa)
{
	return !(pa & 0xfffu) && sweet_heap_va(pa, 0x1000u) != NULL;
}

static void
sweet_heap_publish_locked(void)
{
	sweet_heap_used_kib = sweet_heap.used_total * (uint32_t)(SWEET_GFX_HEAP_GRANULE >> 10);
	sweet_heap_live = sweet_heap.live_total;
	sweet_heap_full = (uint32_t)sweet_heap.full;
}

static void
sweet_heap_region_off(unsigned r, const char *why, uint64_t pa)
{
	uint32_t was = __atomic_fetch_or(&sweet_heap_off, 1u << r, __ATOMIC_RELAXED);
	if (!((was >> r) & 1u)) {
		IOLog("NORD2 HEAP: region %s OFF for new allocations (%s at pa %#llx); %u allocations stay where they are\n",
		    sweet_gfx_heap_regions[r].name, why, pa, sweet_heap.live[r]);
	}
}

/* Map region r once and check it.  Thread call only, sweet_heap_setup_lock held. */
static bool sweet_pool_lend_hi(uint64_t base, uint64_t size, const char **why);     /* SweetSurface.cpp, 0650 */

static void
sweet_heap_setup_region(unsigned r)
{
	const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
	uint32_t n = sweet_gfx_heap_ngran(r), bad = 0;
	if (r >= SWEET_HEAP_FIRST_0650) {       /* 0650: never touched before this boot's log says so */
		IOLog("NORD2 HEAP: 0650 region %s pa %#llx..%#llx (%llu MiB) setup begins%s\n", g->name, g->base, g->base + g->size, g->size >> 20,
		    (g->flags & SWEET_GFX_HEAP_F_POOLHI) ? " (display pool upper part)" : "");
		IOSleep(1000);
	}
	if (g->flags & SWEET_GFX_HEAP_F_POOLHI) {
		const char *why = "?";
		if (!sweet_pool_lend_hi(g->base, g->size, &why)) {
			__atomic_fetch_or(&sweet_heap_off, 1u << r, __ATOMIC_RELAXED);
			IOLog("NORD2 HEAP: region %s NOT used for this boot: %s\n", g->name, why);
			return;
		}
	}
	vm_offset_t va = ml_io_map_unmappable((vm_offset_t)g->base, (vm_size_t)g->size, SWEET_HEAP_WIMG_WCOMB);
	if (va != 0) {
		for (uint32_t k = 0; k < n; k++) {
			uint64_t pa = g->base + (uint64_t)k * SWEET_GFX_HEAP_GRANULE;
			sweet_gfx_heap_canary_put((volatile uint64_t *)(uintptr_t)(va + (uint64_t)k * SWEET_GFX_HEAP_GRANULE), pa);
		}
		sweet_heap_wc_drain();
		for (uint32_t k = 0; k < n; k++) {
			uint64_t pa = g->base + (uint64_t)k * SWEET_GFX_HEAP_GRANULE;
			if (!sweet_gfx_heap_canary_ok((volatile uint64_t *)(uintptr_t)(va + (uint64_t)k * SWEET_GFX_HEAP_GRANULE), pa)) {
				bad++;
			}
		}
	}
	sweet_heap_kva[r] = (volatile uint8_t *)va;
	if (va == 0 || bad != 0) {
		__atomic_fetch_or(&sweet_heap_off, 1u << r, __ATOMIC_RELAXED);
	}
	__atomic_fetch_or(&sweet_heap_ready, va != 0 ? 1u << r : 0u, __ATOMIC_RELEASE);
	IOLog("NORD2 HEAP: region %s pa %#llx..%#llx (%llu KiB, %s%s) -> kva %#lx write-combined, canary readback %u bad of %u -> %s\n", g->name, g->base,
	    g->base + g->size, g->size >> 10, g->cls == SWEET_GFX_HEAP_SAFE ? "SAFE" : "CHECK", (g->flags & SWEET_GFX_HEAP_F_NOPHYS) ? ", IOMMU only" : "",
	    (unsigned long)va, bad, n, va == 0 ? "NOT MAPPED, off" : (bad ? "OFF for this boot" : "ready"));
}

/* Contiguous, zeroed heap memory: PA (0 = heap off/full; the caller falls back to normal memory).  Thread context, no spin lock held. */
static uint64_t
sweet_heap_take(uint64_t bytes, bool gpu)
{
	if (sweet_heap_spin == NULL || bytes == 0) {
		return 0;
	}
	uint32_t mask = sweet_gfx_heap_allowed(__atomic_load_n(&sweet_heap_on, __ATOMIC_RELAXED), __atomic_load_n(&sweet_heap_check, __ATOMIC_RELAXED),
	    __atomic_load_n(&sweet_heap_mask, __ATOMIC_RELAXED), 0) & sweet_heap_ready_mask();
	uint32_t cap = (uint32_t)(((uint64_t)__atomic_load_n(&sweet_heap_cap_mib, __ATOMIC_RELAXED) << 20) / SWEET_GFX_HEAP_GRANULE);
	uint32_t n = sweet_gfx_heap_granules(bytes);
	for (unsigned tries = 0; mask != 0 && tries < SWEET_GFX_HEAP_NREG; tries++) {
		unsigned r = 0;
		IOSimpleLockLock(sweet_heap_spin);
		uint32_t g0 = sweet_gfx_heap_alloc(&sweet_heap, bytes, mask & sweet_heap_ready_mask(), cap, &r);
		sweet_heap_publish_locked();
		IOSimpleLockUnlock(sweet_heap_spin);
		if (g0 == SWEET_GFX_HEAP_FAIL) {
			uint32_t k = sweet_heap_told_full++;
			if (k < 8 || (k % 100) == 0) {
				IOLog("NORD2 HEAP: full for %llu KiB %s (used %u MiB, live %u, cap %u MiB, full %u) -> normal memory\n", bytes >> 10,
				    gpu ? "GPU buffer" : "surface", sweet_heap_used_kib >> 10, sweet_heap_live, sweet_heap_cap_mib, sweet_heap_full);
			}
			return 0;
		}
		uint64_t pa = sweet_gfx_heap_gran_pa(g0);
		volatile uint8_t *va = sweet_heap_va(pa, (uint64_t)n * SWEET_GFX_HEAP_GRANULE);
		uint32_t foreign = 0;
		uint64_t fpa = 0;
		for (uint32_t k = 0; va != NULL && k < n; k++) {
			if (!sweet_gfx_heap_canary_ok((volatile uint64_t *)(uintptr_t)(va + (uint64_t)k * SWEET_GFX_HEAP_GRANULE), pa + (uint64_t)k * SWEET_GFX_HEAP_GRANULE)) {
				if (foreign++ == 0) {
					fpa = pa + (uint64_t)k * SWEET_GFX_HEAP_GRANULE;
				}
			}
		}
		if (va == NULL || foreign != 0) {
			/* someone else writes there (or the region vanished): the range stays allocated forever (quarantine), the region goes off */
			OSAddAtomic((SInt32)foreign, (volatile SInt32 *)&sweet_heap_foreign);
			OSIncrementAtomic((volatile SInt32 *)&sweet_heap_leaked);
			IOLog("NORD2 HEAP: FOREIGN write in %u of %u free granules of %s (first pa %#llx, seen at alloc): range quarantined\n", foreign, n,
			    sweet_gfx_heap_regions[r].name, fpa);
			sweet_heap_region_off(r, "foreign write", fpa);
			mask &= ~(1u << r);
			continue;
		}
		bzero((void *)va, (size_t)bytes);       /* Normal non-cacheable memory: DC ZVA is allowed (never Device) */
		sweet_heap_wc_drain();
		OSIncrementAtomic((volatile SInt32 *)(gpu ? &sweet_heap_gpu_live : &sweet_heap_surf_live));
		return pa;
	}
	return 0;
}

/* Give a range back (after every mapping and every device use of it is gone). */
static void
sweet_heap_give(uint64_t pa, uint64_t bytes, bool gpu)
{
	uint32_t n = sweet_gfx_heap_granules(bytes);
	volatile uint8_t *va = sweet_heap_va(pa, (uint64_t)n * SWEET_GFX_HEAP_GRANULE);
	bool ok = false;
	bool mine = false;
	if (va != NULL && sweet_heap_spin != NULL) {
		IOSimpleLockLock(sweet_heap_spin);
		mine = sweet_gfx_heap_is_allocated(&sweet_heap, pa, bytes);
		IOSimpleLockUnlock(sweet_heap_spin);
	}
	if (mine) {
		for (uint32_t k = 0; k < n; k++) {      /* canary first, bit second: a free granule always holds its canary (scrub relies on it) */
			sweet_gfx_heap_canary_put((volatile uint64_t *)(uintptr_t)(va + (uint64_t)k * SWEET_GFX_HEAP_GRANULE), pa + (uint64_t)k * SWEET_GFX_HEAP_GRANULE);
		}
		sweet_heap_wc_drain();
		IOSimpleLockLock(sweet_heap_spin);
		ok = sweet_gfx_heap_free(&sweet_heap, pa, bytes);
		sweet_heap_publish_locked();
		IOSimpleLockUnlock(sweet_heap_spin);
	}
	if (ok) {
		OSDecrementAtomic((volatile SInt32 *)(gpu ? &sweet_heap_gpu_live : &sweet_heap_surf_live));
	}
	uint32_t k = sweet_heap_logged++;
	if (!ok || k < 16 || (k % 256) == 0) {
		IOLog("NORD2 HEAP: free %s pa %#llx %llu KiB%s (used %u MiB, live %u)\n", gpu ? "GPU buffer" : "surface", pa, bytes >> 10,
		    ok ? "" : " REFUSED (not a heap allocation)", sweet_heap_used_kib >> 10, sweet_heap_live);
	}
}

/* PA of a heap surface's pixels, 0 for every other surface (pool surfaces included). */
static uint64_t
sweet_heap_surface_pa(const SweetSurface *cs)
{
	SweetSurface *s = const_cast<SweetSurface *>(cs);
	if (s == NULL || s->memory == NULL || s->purged || __atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE) == 0 ||
	    OSDynamicCast(IOBufferMemoryDescriptor, s->memory) != NULL || OSDynamicCast(IOSubMemoryDescriptor, s->memory) != NULL) {
		return 0;
	}
	IOByteCount len = 0;
	uint64_t pa = s->memory->getPhysicalSegment(0, &len, kIOMemoryMapperNone);
	return sweet_heap_va(pa, s->memory->getLength()) != NULL ? pa : 0;
}

/* Put s (geometry set, allocSize final) into the heap: memory + mapCache; false = heap off/full (s untouched). */
static bool
sweet_heap_attach(SweetSurface *s)
{
	uint64_t pa = sweet_heap_take(s->allocSize, false);
	if (pa == 0) {
		return false;
	}
	s->memory = IOMemoryDescriptor::withPhysicalAddress(pa, s->allocSize, kIODirectionInOut);
	if (s->memory == NULL) {
		sweet_heap_give(pa, s->allocSize, false);
		return false;
	}
	s->mapCache = kIOMapWriteCombineCache;
	return true;
}

static void
sweet_heap_log_alloc(SweetSurface *s, const char *pname)
{
	uint32_t k = sweet_heap_logged++;
	if (k < 16 || (k % 256) == 0) {
		uint64_t pa = sweet_heap_surface_pa(s);
		int r = sweet_gfx_heap_region_of(pa, s->allocSize);
		IOLog("NORD2 HEAP: surface id %u by %s %ux%u bpr %u -> pa %#llx (%s) %u KiB (used %u MiB, live %u)\n", s->id, pname, s->width, s->height,
		    s->bytesPerRow, pa, r >= 0 ? sweet_gfx_heap_regions[r].name : "?", s->allocSize >> 10, sweet_heap_used_kib >> 10, sweet_heap_live);
	}
}

/* 0401 IOMMU hook: an M4U fault reported a PA inside a mapped region: that region takes no new allocations. */
static void
sweet_heap_m4u_fault_pa(uint64_t pa)
{
	int r = sweet_gfx_heap_region_of(pa & ~0xfffull, 0x1000u);
	if (r >= 0 && ((__atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE) >> r) & 1u)) {
		sweet_heap_region_off((unsigned)r, "display M4U fault", pa);
	}
}

/* Free granules whose canary changed (thread call).  A candidate is confirmed under the spin lock: a granule whose bit is clear there
 * holds its canary unless something outside XNU wrote it (allocation sets the bit before zeroing; free writes the canary before clearing). */
static uint32_t
sweet_heap_scrub(void)
{
	uint32_t found = 0, g = 0;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; g += sweet_gfx_heap_ngran(r), r++) {
		if (!((sweet_heap_ready_mask() >> r) & 1u)) {
			continue;
		}
		const struct sweet_gfx_heap_region *rg = &sweet_gfx_heap_regions[r];
		for (uint32_t k = 0; k < sweet_gfx_heap_ngran(r); k++) {
			uint64_t pa = rg->base + (uint64_t)k * SWEET_GFX_HEAP_GRANULE;
			volatile uint64_t *head = (volatile uint64_t *)(uintptr_t)(sweet_heap_kva[r] + (uint64_t)k * SWEET_GFX_HEAP_GRANULE);
			if (sweet_gfx_heap_test(&sweet_heap, g + k) || sweet_gfx_heap_canary_ok(head, pa)) {
				continue;                       /* racy pre-filter */
			}
			IOSimpleLockLock(sweet_heap_spin);
			bool bad = !sweet_gfx_heap_test(&sweet_heap, g + k) && !sweet_gfx_heap_canary_ok(head, pa);
			if (bad) {
				sweet_gfx_heap_set(&sweet_heap, g + k, 1, true);        /* quarantined: never handed out (not counted as used) */
			}
			IOSimpleLockUnlock(sweet_heap_spin);
			if (bad) {
				found++;
				OSIncrementAtomic((volatile SInt32 *)&sweet_heap_foreign);
				if (found <= 2) {
					IOLog("NORD2 HEAP: FOREIGN write in free granule pa %#llx of %s (seen by the scrub): %016llx %016llx\n", pa, rg->name, head[0], head[1]);
				}
				sweet_heap_region_off(r, "foreign write (scrub)", pa);
			}
		}
	}
	return found;
}

static void
sweet_heap_log_line(bool force)
{
	char buf[640];
	int o = 0;
	uint32_t ready = __atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE), off = __atomic_load_n(&sweet_heap_off, __ATOMIC_RELAXED);
	uint32_t sig = sweet_heap_used_kib ^ (sweet_heap_live << 20) ^ (ready << 8) ^ (off << 17) ^ (sweet_heap_foreign << 26) ^ (uint32_t)sweet_heap_on ^
	    ((uint32_t)sweet_heap_check << 1) ^ ((uint32_t)sweet_heap_gpu << 2);
	uint32_t now = sweet_surface_uptime();
	if (!force && sig == sweet_heap_log_sig && now - sweet_heap_log_at < 600) {
		return;
	}
	sweet_heap_log_sig = sig;
	sweet_heap_log_at = now;
	uint64_t total_ready = 0;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG && o < (int)sizeof(buf) - 48; r++) {
		uint32_t used_kib = sweet_heap.used[r] * (uint32_t)(SWEET_GFX_HEAP_GRANULE >> 10), size_kib = (uint32_t)(sweet_gfx_heap_regions[r].size >> 10);
		char st = ((off >> r) & 1u) ? 'O' : (((ready >> r) & 1u) ? 'R' : '-');
		if (((ready & ~off) >> r) & 1u) {
			total_ready += sweet_gfx_heap_regions[r].size;
		}
		o += snprintf(buf + o, sizeof(buf) - (size_t)o, " %s %c %u/%u", sweet_gfx_heap_regions[r].name, st, used_kib >> 10, (size_kib - used_kib) >> 10);
	}
	IOLog("NORD2 HEAP: on %d check %d gpu %d mask %#x cap %u MiB | used %u MiB (peak %llu) of %llu ready, live %u (surfaces %u, GPU %u), full %u, foreign %u, "
	    "quarantined %u | used/free MiB per region (R ready, O off, - unmapped):%s\n", sweet_heap_on, sweet_heap_check, sweet_heap_gpu, sweet_heap_mask,
	    sweet_heap_cap_mib, sweet_heap_used_kib >> 10, ((uint64_t)sweet_heap.used_peak * SWEET_GFX_HEAP_GRANULE) >> 20, total_ready >> 20, sweet_heap_live,
	    sweet_heap_surf_live, sweet_heap_gpu_live, sweet_heap_full, sweet_heap_foreign, sweet_heap_leaked, buf);
}

/*
 * 0650: "NORD2 RAM8:" the physical memory plan (once at start, once when every wanted region is set up): XNU's window (gPhysBase/gPhysSize),
 * the heap regions with their state, and the sum.  The 8 GiB split itself (firmware, modem, device pools) is static: memmap.py.
 */
static void
sweet_heap_plan_log(const char *when)
{
	char buf[640];
	int o = 0;
	uint64_t ready_b = 0, want_b = 0;
	uint32_t ready = __atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE), off = __atomic_load_n(&sweet_heap_off, __ATOMIC_RELAXED);
	uint32_t want = sweet_gfx_heap_allowed(sweet_heap_on, sweet_heap_check, sweet_heap_mask, 0);
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG && o < (int)sizeof(buf) - 64; r++) {
		const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
		char st = ((off >> r) & 1u) ? 'O' : (((ready >> r) & 1u) ? 'R' : (((want >> r) & 1u) ? 'w' : '-'));
		if (((ready & ~off) >> r) & 1u) {
			ready_b += g->size;
		}
		if (((want & ~off) >> r) & 1u) {
			want_b += g->size;
		}
		o += snprintf(buf + o, sizeof(buf) - (size_t)o, " %s %#llx+%lluM %c", g->name, g->base, g->size >> 20, st);
	}
	IOLog("NORD2 RAM8: %s | DRAM 8192 MiB | XNU window %#lx + %lu MiB | heap ready %llu MiB of %llu MiB wanted (R ready, w wanted, O off, - not wanted) |"
	    " XNU + heap ready = %llu MiB |%s\n", when, gPhysBase, gPhysSize >> 20, ready_b >> 20, want_b >> 20, ((uint64_t)gPhysSize + ready_b) >> 20, buf);
}

/* 0650: switch off every region that overlaps what the loader left for later use: the chain-back target (/chosen nord2-stock-entry, 2 MiB
 * from its 1 MiB boundary) and the stock DTB (/chosen nord2-stock-dtb, 1 MiB: wifi_main_phy_xnu.c maps that much).  Missing properties:
 * the low-bank boot regions lb0/lb4 are switched off (their safety rests on those addresses). */
static void
sweet_heap_loader_check(void)
{
	uint64_t keep[2][2] = { { 0, 0 }, { 0, 0 } };
	const char *what[2] = { "nord2-stock-entry", "nord2-stock-dtb" };
	bool have = true;
	IORegistryEntry *chosen = IORegistryEntry::fromPath("IODeviceTree:/chosen");
	for (unsigned i = 0; i < 2; i++) {
		OSData *d = chosen != NULL ? OSDynamicCast(OSData, chosen->getProperty(what[i])) : NULL;
		uint64_t v = 0;
		if (d == NULL || d->getLength() != sizeof(v)) {
			have = false;
			continue;
		}
		memcpy(&v, d->getBytesNoCopy(), sizeof(v));
		keep[i][0] = i == 0 ? (v & ~0xfffffull) : v;
		keep[i][1] = i == 0 ? 0x200000ull : 0x100000ull;
	}
	if (chosen != NULL) {
		chosen->release();
	}
	for (unsigned r = SWEET_HEAP_FIRST_0650; r < SWEET_GFX_HEAP_NREG; r++) {
		const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
		bool low = g->base < 0x55100000ull;     /* lb0 / lb4: inside the loader's boot area */
		for (unsigned i = 0; i < 2; i++) {
			if (keep[i][1] != 0 && keep[i][0] < g->base + g->size && g->base < keep[i][0] + keep[i][1]) {
				__atomic_fetch_or(&sweet_heap_off, 1u << r, __ATOMIC_RELAXED);
				IOLog("NORD2 HEAP: region %s OFF for this boot: overlaps %s %#llx+%#llx\n", g->name, what[i], keep[i][0], keep[i][1]);
			}
		}
		if (low && !have) {
			__atomic_fetch_or(&sweet_heap_off, 1u << r, __ATOMIC_RELAXED);
			IOLog("NORD2 HEAP: region %s OFF for this boot: /chosen nord2-stock-entry/-dtb missing\n", g->name);
		}
	}
	IOLog("NORD2 HEAP: 0650 loader addresses: stub %#llx+%#llx, stock DTB %#llx+%#llx (kept out of the heap)\n", keep[0][0], keep[0][1], keep[1][0], keep[1][1]);
}

/* Every 30 s (thread call): map the regions now allowed, scrub the free granules, the stats line. */
static void
sweet_heap_tick(thread_call_param_t, thread_call_param_t)
{
	IOLockLock(sweet_heap_setup_lock);
	uint32_t want = sweet_gfx_heap_allowed(__atomic_load_n(&sweet_heap_on, __ATOMIC_RELAXED), __atomic_load_n(&sweet_heap_check, __ATOMIC_RELAXED),
	    __atomic_load_n(&sweet_heap_mask, __ATOMIC_RELAXED), 0);
	bool changed = false;
	bool did_0650 = false;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		if (((want & ~__atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE) & ~__atomic_load_n(&sweet_heap_off, __ATOMIC_RELAXED)) >> r) & 1u) {
			if (r >= SWEET_HEAP_FIRST_0650) {
				if (did_0650) {
					continue;       /* 0650: one new region per tick (the next tick follows in 5 s) */
				}
				did_0650 = true;
			}
			sweet_heap_setup_region(r);
			changed = true;
		}
	}
	if (changed && !sweet_heap_plan_logged && ((want & ~__atomic_load_n(&sweet_heap_ready, __ATOMIC_ACQUIRE) &
	    ~__atomic_load_n(&sweet_heap_off, __ATOMIC_RELAXED)) == 0)) {
		sweet_heap_plan_logged = true;
		sweet_heap_plan_log("all wanted regions set up");
	}
	(void)sweet_heap_scrub();
	IOLockUnlock(sweet_heap_setup_lock);
	sweet_heap_log_line(changed);
	uint64_t dl = 0;
	clock_interval_to_deadline(changed ? 5 : SWEET_HEAP_TICK_S, kSecondScale, &dl);
	thread_call_enter_delayed(sweet_heap_tc, dl);
}

/* IOCoreSurfaceRoot::start: locks, the table check, the first tick (regions are mapped there, never on a creation path). */
static void
sweet_heap_start(void)
{
	uint32_t total = 0, safe = 0;
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {
		total += sweet_gfx_heap_ngran(r);
		safe += sweet_gfx_heap_regions[r].cls == SWEET_GFX_HEAP_SAFE ? sweet_gfx_heap_ngran(r) : 0;
	}
	if (total != SWEET_GFX_HEAP_NGRAN) {
		IOLog("NORD2 HEAP: region table has %u granules, bitmap %u: heap disabled\n", total, SWEET_GFX_HEAP_NGRAN);
		return;
	}
	for (unsigned r = 0; r < SWEET_GFX_HEAP_NREG; r++) {     /* never XNU's window, the display pool or the IOMMU identity sections */
		const struct sweet_gfx_heap_region *g = &sweet_gfx_heap_regions[r];
		if (g->base + g->size > (uint64_t)gPhysBase && g->base < (uint64_t)gPhysBase + gPhysSize) {
			IOLog("NORD2 HEAP: region %s overlaps XNU's window %#lx+%#lx: heap disabled\n", g->name, gPhysBase, gPhysSize);
			return;
		}
		if (g->base + g->size > SWEET_FB_HW_POOL_BASE && g->base < SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE &&
		    !((g->flags & SWEET_GFX_HEAP_F_POOLHI) && g->base >= SWEET_FB_HW_POOL_BASE + SWEET_GFX_HEAP_POOL_KEEP &&
		    g->base + g->size <= SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE)) {
			IOLog("NORD2 HEAP: region %s overlaps the display pool: heap disabled\n", g->name);
			return;
		}
	}
	sweet_heap_loader_check();      /* 0650: the chain-back stub and the stock DTB stay untouched */
	sweet_heap_spin = IOSimpleLockAlloc();
	sweet_heap_setup_lock = IOLockAlloc();
	sweet_heap_tc = (sweet_heap_spin != NULL && sweet_heap_setup_lock != NULL) ? thread_call_allocate(sweet_heap_tick, NULL) : NULL;
	IOLog("NORD2 HEAP: 0440 graphics heap: %u regions, %llu MiB (SAFE %llu MiB), kern.n2heap %d, kern.n2heap_check %d, kern.n2heap_gpu %d, cap %u MiB%s\n",
	    SWEET_GFX_HEAP_NREG, ((uint64_t)total * SWEET_GFX_HEAP_GRANULE) >> 20, ((uint64_t)safe * SWEET_GFX_HEAP_GRANULE) >> 20,
	    sweet_heap_on, sweet_heap_check, sweet_heap_gpu, sweet_heap_cap_mib,
	    sweet_heap_tc != NULL ? "" : " -- NO LOCKS/THREAD CALL: heap off");
	sweet_heap_plan_log("start (regions are mapped by the heap tick)");
	if (sweet_heap_tc != NULL) {
		thread_call_enter(sweet_heap_tc);
	}
}

/* ---- 0441: Nord2GPU (GPU-only buffers, imports of heap surfaces) */
extern "C" uint64_t sweet_heap_gpu_take(uint64_t bytes, uintptr_t *kva);
extern "C" void sweet_heap_gpu_give(uint64_t pa, uint64_t bytes);
extern "C" bool sweet_heap_gpu_page_ok(uint64_t pa);
extern "C" uint64_t
sweet_heap_gpu_take(uint64_t bytes, uintptr_t *kva)
{
	if (kva == NULL || !__atomic_load_n(&sweet_heap_gpu, __ATOMIC_RELAXED) || !sweet_gfx_heap_gpu_eligible(2u, bytes, SWEET_HEAP_GPU_MIN)) {
		return 0;
	}
	uint64_t pa = sweet_heap_take(bytes, true);
	*kva = pa != 0 ? (uintptr_t)sweet_heap_va(pa, bytes) : 0;
	if (pa != 0) {
		uint32_t k = sweet_heap_logged++;
		if (k < 16 || (k % 256) == 0) {
			IOLog("NORD2 HEAP: GPU buffer %llu KiB -> pa %#llx (used %u MiB, live %u)\n", bytes >> 10, pa, sweet_heap_used_kib >> 10, sweet_heap_live);
		}
	}
	return pa;
}

extern "C" void
sweet_heap_gpu_give(uint64_t pa, uint64_t bytes)
{
	sweet_heap_give(pa, bytes, true);
}

extern "C" bool
sweet_heap_gpu_page_ok(uint64_t pa)
{
	return sweet_heap_page_ok(pa);
}
