/*
 * Built-in IOSurface for SWEET.
 *
 * Apple's IOSurface kext is closed. IOSurface.framework (iOS 16.7) finds
 * IOServiceNameMatching("IOCoreSurfaceRoot"), opens it with type 0 and
 * talks to the user client below:
 *
 *   sel 13  limits: struct out 0x18, u16 at +0x16 must be 1 (ABI version),
 *           +0 offset alignment mask, +4 bytes-per-row alignment mask
 *   sel 0   create: scalar [parent id], struct in = serialized property
 *           dictionary (binary or XML), struct out = lock result (0xa68)
 *   sel 4   lookup by id: scalar [id], struct out = lock result
 *   sel 34  lookup by mach port: scalar [port name], struct out = lock result
 *   sel 35  create mach port: scalars [id, options], scalar out [port name]
 *   traps   0/1 use count +/- (id, category), 2 lock / 3 unlock
 *           (id, options, &seed), 4/5 release (id)
 *           0398: the use count is real (sweet_surface_use.h): state page
 *           +0x18/+0x1c = holders of category 0/1 across all processes and
 *           the kernel (display swaps, GPU jobs), read by IOSurfaceIsInUse
 *
 * The lock result tells the library where the surface lives in the
 * caller's address space: +0 pixels, +8 shared state page (registry id,
 * seed at +0xc), +0x10 timestamps, then id, sizes and geometry (layout in
 * sweet_surface_fill_result). Every surface is wired kernel memory mapped
 * into each task that creates or looks it up; mappings are counted per
 * client and dropped on release or close.
 *
 * Values, notifications, accelerators and shared events are accepted and
 * ignored (logged once per selector).
 */
#include "SweetSurface.h"
#include "sweet_surface_abi.h"

#if defined(SWEET)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/system.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSUnserialize.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <pexpert/pexpert.h>
#include "sweet_fb_hw_pool_alloc.h"     /* 0394 */
#include "sweet_gfx_heap.h"              /* 0650: SWEET_GFX_HEAP_POOL_KEEP for the pool lending (table + allocator, pure) */
#include "sweet_surface_use.h"          /* 0398 */
#include "sweet_fb_pool_reclaim.h"     /* 0406 */
#include "sweet_surface_purge.h"         /* 0417 */
#include <IOKit/IOSubMemoryDescriptor.h>  /* 0417: purged surfaces = views of the sink */
#include <kern/thread_call.h>           /* 0417: the 2 s pressure scan */
extern "C" {
#include <arm/caches_internal.h>
vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
}

enum {
	kSweetSurfaceCreate             = 0,
	kSweetSurfaceLookup             = 4,
	kSweetSurfaceLimits             = 13,
	kSweetSurfaceLookupFromPort     = 34,
	kSweetSurfaceCreatePort         = 35,
};

enum {
	kSweetSurfaceTrapIncrementUse   = 0,
	kSweetSurfaceTrapDecrementUse   = 1,
	kSweetSurfaceTrapLock           = 2,
	kSweetSurfaceTrapUnlock         = 3,
	kSweetSurfaceTrapRelease        = 4,
	kSweetSurfaceTrapReleaseFailed  = 5,
};

#define SWEET_SURFACE_RESULT_SIZE       0xa68
#define SWEET_SURFACE_MAX_ID            4096
#define SWEET_SURFACE_MAX_DIM           16384
#define SWEET_SURFACE_MAX_ALLOC         (256u << 20)
#define SWEET_SURFACE_MAX_PROPS         (64u << 10)
#define SWEET_SURFACE_ROW_ALIGN         64
#define SWEET_SURFACE_TIMESTAMPS        0x800   /* offset in the state page */
#define SWEET_SURFACE_STATE_SEED        0xc     /* u32 in the state page */
#define kIOSurfaceLockReadOnly          0x1

static IOLock       *sweet_surface_lock;
static SweetSurface *sweet_surface_table[SWEET_SURFACE_MAX_ID];
static uint32_t      sweet_surface_users[SWEET_SURFACE_MAX_ID];
static uint32_t      sweet_surface_next_id = 1;
static uint32_t      sweet_surface_creates_logged;
static uint64_t      sweet_surface_wired;       /* bytes of buffer surfaces */
/*
 * 0299: every surface here is wired kernel memory (nothing is purgeable or paged as on Apple's IOSurface), under one budget.
 * 1536 MiB was reached by the Photos application and the camera together (run user2: 1438 creations refused with
 * kIOReturnNoMemory, of every size). 2048 MiB of the 3776 MiB the kernel is given; each refusal now says what is held, by whom.
 */
/* 0418: 55 % of the RAM window (2048 MiB = 54 % of 3776 before; 2798 MiB at 5088), set in IOCoreSurfaceRoot::start (sweet_purge_start) */
static uint32_t      sweet_surf_cap_mib = 2048;
#define SWEET_SURFACE_WIRED_MAX         ((uint64_t)__atomic_load_n(&sweet_surf_cap_mib, __ATOMIC_RELAXED) << 20)
static int32_t       sweet_surface_owner[SWEET_SURFACE_MAX_ID];     /* pid that put the surface in the table */
static uint32_t      sweet_surface_buffers;     /* buffer surfaces alive (in the table or only held by a port / the kernel) */
static uint64_t      sweet_surface_refused, sweet_surface_report_ns;

/*
 * 0398 (usecount-20261112): the IOSurface use count.  Before, traps 0/1 answered success and did nothing, so IOSurfaceIsInUse was
 * always false: CVPixelBufferPool (CVPixelBufferBacking::okToReuse), CABackingStore (buffer_is_unused) and the render server's
 * holds (CA::Render::Surface, CAImageQueue) never protected a buffer that was still shown or read, and pools handed out surfaces
 * that textures, layers and the display still used (vanishing icons, black Photos thumbnails).  Protocol and holders:
 * sweet_surface_use.h.  kern.n2surfuse (boot-arg n2su) 0/1/2, default 2; kern.n2surfuse_report=1 logs a report with the
 * surfaces in use at the next trap.  Logs "NORD2 SURF USE:" (boot+30 s, every 60 s while traps come, on request).
 */
static int           sweet_use_mode = SWEET_USE_MODE_ALL;
static int           sweet_use_report_req;
static bool          sweet_use_args_done;
static struct sweet_use_stats sweet_use_st;
static uint32_t      sweet_use_report_at, sweet_use_reporting, sweet_use_reports;
static uint32_t      sweet_use_told_dup, sweet_use_told_unmatched, sweet_use_told_found, sweet_use_told_drop, sweet_use_told_free;
SYSCTL_INT(_kern, OID_AUTO, n2surfuse, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_use_mode, 0, "0398: IOSurface use count: 0 off, 1 user traps, 2 + display/GPU holds");
SYSCTL_INT(_kern, OID_AUTO, n2surfuse_report, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_use_report_req, 0, "0398: 1 = log the use-count report at the next trap");
SYSCTL_QUAD(_kern, OID_AUTO, n2surfuse_live, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_use_st.live, "0398: use-count holds now");
SYSCTL_QUAD(_kern, OID_AUTO, n2surfuse_inc, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_use_st.user_inc, "0398: user increments");
SYSCTL_QUAD(_kern, OID_AUTO, n2surfuse_dec, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_use_st.user_dec, "0398: user decrements");

static void
sweet_use_args(void)
{
	if (sweet_use_args_done) {
		return;
	}
	int v = 0;
	if (PE_parse_boot_argn("n2su", &v, sizeof(v)) && v >= SWEET_USE_MODE_OFF && v <= SWEET_USE_MODE_ALL) {
		sweet_use_mode = v;
	}
	sweet_use_args_done = true;
}

static int
sweet_use_mode_now(void)
{
	return __atomic_load_n(&sweet_use_mode, __ATOMIC_RELAXED);
}

static uint32_t      sweet_surface_seq[SWEET_SURFACE_MAX_ID];       /* 0300: creation number of the surface in the slot */
static uint32_t      sweet_surface_born[SWEET_SURFACE_MAX_ID];      /* 0300: second of uptime it was put there */
static uint32_t      sweet_surface_created;     /* buffer surfaces ever made (released = created - alive) */
static void sweet_surface_report(const char *why, uint32_t asked);      /* after the client class */

/*
 * 0394 (hwcomp-20261112, TRACK A): the display surface pool, PA 0xC0000000..0xD0000000 (sweet_fb_hw_pool.h).
 * Memory map (gpu/hwcodec/DISPLAY_HW_COMPOSE.md, section "Display pool"): XNU's RAM is 0x1_42000000 + 3776 MiB (loader
 * NORD2_XNU_PHYS_BASE / loader3776 + ADT dram-size 0xec000000), so XNU never manages the pool and nothing has to be taken out
 * of its RAM map.  Neighbours: atf-ramdump ends at 0xC0000000 (no-map), the camera buffers start at 0xD0000000 (nord2_camsv.inc).
 * The live DTS names exactly this range as Linux's SSMR CMA pool (reusable, not no-map; LK lists it as free RAM): nobody asks the
 * TEE for it under XNU.  The pool init writes a canary into every 64 KiB granule and reads it back; any mismatch turns the pool
 * off for the boot (NORD2 FB POOL: ... disabled).
 * Every CPU mapping of the pool has the same attribute, chosen once (boot-arg n2fbpoolc): 0 = write-combined (default), 1 =
 * write-back cacheable.  Never kIOMapDefaultCache: for a PA outside XNU's RAM that resolves to VM_WIMG_IO (Device memory:
 * IODefaultCacheBits -> pmap_cache_attributes), on which unaligned accesses and DC ZVA fault.
 * One kernel mapping of the whole pool (ml_io_map_unmappable: kernel_map; ml_io_map_wcomb would use the 8 MiB io_submap with
 * KMA_NOFAIL and panic); user mappings are withPhysicalAddress descriptors mapped like the panel buffers.
 */
#define SWEET_POOL_WIMG_COPYBACK        0x2u    /* osfmk/arm/memory_types.h VM_WIMG_COPYBACK (== VM_WIMG_DEFAULT) */
#define SWEET_POOL_WIMG_WCOMB           0x6u    /* VM_WIMG_WCOMB */
_Static_assert(SWEET_FB_HW_POOL_BASE >= 0xC0000000ull, "pool overlaps atf-ramdump (ends at 0xC0000000)");
_Static_assert(SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE <= 0xD0000000ull, "pool overlaps the camera buffers at 0xD0000000");
_Static_assert(SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE <= 0x100000000ull, "OVL Ln_ADDR is 32 bits");
_Static_assert((SWEET_FB_HW_POOL_BASE % SWEET_FB_HW_POOL_GRANULE) == 0 && (SWEET_FB_HW_POOL_GRANULE % 16384) == 0, "granule");

static int           sweet_pool_enabled = 0;    /* kern.n2fbhw_pool (boot-arg n2fbhw_pool).  0416: default 0: every surface is shown through the IOMMU (no pool-only mode) */
static uint32_t      sweet_pool_copyback;       /* boot-arg n2fbpoolc: fixed at the first pool use */
static uint32_t      sweet_pool_state;          /* 0 not set up, 1 ready, 2 off for this boot */
static bool          sweet_pool_args_done;
static IOSimpleLock *sweet_pool_spin;           /* the bitmap */
static struct sweet_fb_pool sweet_pool;
static volatile uint8_t *sweet_pool_kva;        /* the whole pool */
static uint32_t      sweet_pool_live, sweet_pool_used_kib, sweet_pool_full, sweet_pool_foreign, sweet_pool_leaked;
static uint32_t      sweet_pool_why[SWEET_FB_POOL_NREASONS];    /* creations by eligibility answer (0 = went to the pool) */
static uint32_t      sweet_pool_logged_alloc, sweet_pool_logged_free;
static uint32_t      sweet_pool_ngran_lim = SWEET_FB_POOL_NGRAN;        /* 0650: granules the pool may touch (the rest lent to the graphics heap) */
static bool          sweet_pool_hi_lent;                                /* 0650: under sweet_surface_lock */
SYSCTL_INT(_kern, OID_AUTO, n2fbhw_pool, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_pool_enabled, 0, "0394: big BGRA IOSurfaces from the display pool");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_live, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_live, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_used_kib, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_used_kib, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_full, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_full, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_foreign, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_foreign, 0, "");
/* 0398: admission by creator, the live-surface registry and the leak report (sweet_surface_use.h, "Display pool") */
static int           sweet_pool_policy = SWEET_POOL_POLICY_NO_SPRINGBOARD;
static uint32_t      sweet_pool_age = 30;       /* s: older and not named in a swap for this long = leak candidate */
static uint32_t      sweet_pool_denied, sweet_pool_told_denied, sweet_pool_report_full, sweet_pool_report_at;
static struct sweet_pool_reg sweet_pool_reg;    /* under sweet_surface_lock */
SYSCTL_INT(_kern, OID_AUTO, n2fbhw_pool_policy, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_pool_policy, 0, "0398: pool admission 0 any, 1 not SpringBoard, 2 backboardd only");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_age, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_pool_age, 0, "0398: leak-candidate age (s)");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_denied, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_denied, 0, "0398: eligible creations kept out by the policy");
/*
 * 0406 (poolreclaim-20261114): eviction of idle VOLATILE pool surfaces when the pool is full (sweet_fb_pool_reclaim.h).  Mode 1 (default)
 * only decides and logs "NORD2 FB POOL RECLAIM:" (what mode 2 would evict, why the others stay); mode 2 evicts.  Lock order:
 * sweet_pool_evict_lock, then sweet_clients_lock, a client's fLock, sweet_surface_lock.  The IOSurfaceSetPurgeable selector takes
 * sweet_pool_evict_lock too, so a surface cannot turn non-volatile between the decision and the end of the remap.
 */
static int           sweet_pool_reclaim_mode = SWEET_POOL_RECLAIM_REPORT;
static uint32_t      sweet_pool_reclaim_grace_s = 2;    /* s between the swap and the old range's return (and after any later swap) */
static uint32_t      sweet_pool_evicted, sweet_pool_reclaim_given, sweet_pool_remap_failed, sweet_pool_reclaim_passes;
static uint32_t      sweet_pool_reclaim_at, sweet_pool_reclaim_told;
static uint32_t      sweet_pool_reclaim_npend;          /* pending old ranges (read without the lock as a hint) */
static IOLock       *sweet_pool_evict_lock;
SYSCTL_INT(_kern, OID_AUTO, n2fbhw_pool_reclaim, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_pool_reclaim_mode, 0, "0406: full pool: 0 off, 1 log what would be evicted, 2 evict idle volatile surfaces");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_reclaim_grace, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_pool_reclaim_grace_s, 0, "0406: s before an evicted range goes back");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_evicted, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_evicted, 0, "0406: surfaces moved out of the pool");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_reclaim_given, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_reclaim_given, 0, "0406: evicted ranges given back to the pool");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhw_pool_remap_failed, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_pool_remap_failed, 0, "0406: client mappings left on the old range");
static void sweet_pool_reclaim(uint64_t bytes);
static void sweet_pool_reclaim_drain(void);
/*
 * 0417 (memfix-20261113): purge of idle VOLATILE surfaces under pressure, one budget for surfaces + GPU private memory, a per-process
 * surface budget, and "NORD2 MEM SURF:" every 10 s (sweet_surface_purge.h).  A purged surface keeps its id, state page and user addresses;
 * its pages are a view (IOSubMemoryDescriptor) of one shared wired sink and its own wired buffer is freed after the 0406 grace.
 * Accounting: a purged surface leaves sweet_surface_wired / sweet_surface_buffers / its creator's bytes at the purge (its old buffer is
 * counted in "pend" until the grace returns it); SetPurgeable(NonVolatile) charges it again.  Locks: sweet_pool_evict_lock serialises
 * purges, unpurges, SetPurgeable and the 0406 evictions; under it sweet_clients_lock, a client's fLock, sweet_surface_lock, n2io_lock,
 * the GPU lock (nord2_gpu_surface_busy) as in 0406.  sweet_mem_spin (spin) guards the per-process table only.
 */
static int           sweet_purge_mode = SWEET_PURGE_ON;         /* kern.n2surf_purge (boot-arg n2sp) */
static uint32_t      sweet_purge_thr_mib = 1228;                /* surface wired bytes above this = pressure */
static uint32_t      sweet_purge_avail_min = 40000;             /* memorystatus_available_pages below this = pressure (16 KiB pages) */
static uint32_t      sweet_purge_idle_s = 3;                    /* not created / swapped for this long */
static uint32_t      sweet_mem_budget_mib = 2240;               /* kern.n2mem_budget_mib (boot-arg n2mb): surfaces + GPU private, 0 off */
static uint32_t      sweet_surf_proc_mib = 896;                 /* kern.n2surf_proc_mib: wired surface bytes one process may make, 0 off */
/* 0418: every value above is now derived from the RAM window at start (sweet_mem_policy_for); n2mem_budget_mib is only a purge mark;
 * refusals need available pages under kern.n2mem_floor (or a hard cap: surfaces 55 %, GPU 40 %, one process 60 % of RAM) */
static uint32_t      sweet_mem_floor = 18000;                   /* kern.n2mem_floor: available pages under which allocations are refused */
static uint32_t      sweet_gpu_cap_mib = 1536;                  /* kern.n2mem_gpu_cap_mib: GPU private cap (max with Nord2GPU's own limit) */
static uint32_t      sweet_purge_n, sweet_unpurge_n, sweet_purge_failed, sweet_unpurge_failed, sweet_purge_passes, sweet_purge_remap_bad;
static uint32_t      sweet_purged_live, sweet_purge_given, sweet_purge_npend;
static uint64_t      sweet_purged_bytes, sweet_purge_pend_bytes;
static uint32_t      sweet_mem_refused_combined, sweet_mem_refused_proc, sweet_mem_refused_gpu, sweet_mem_gpu_kicks, sweet_mem_exempted;
static uint64_t      sweet_mem_gpu_private;                     /* the GPU's private total at its last allocation check */
static uint64_t      sweet_mem_idle_bytes;                      /* idle volatile bytes found by the last scan (purge estimate) */
static IOSimpleLock *sweet_mem_spin;
static struct sweet_mem_proc sweet_mem_procs[SWEET_MEM_PROCS]; /* under sweet_mem_spin */
SYSCTL_INT(_kern, OID_AUTO, n2surf_purge, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_purge_mode, 0, "0417: idle volatile surfaces under pressure: 0 off, 1 log only, 2 purge");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_purge_mib, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_purge_thr_mib, 0, "0417: surface wired MiB above which idle volatile surfaces go");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_purge_avail, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_purge_avail_min, 0, "0417: available pages below which idle volatile surfaces go");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_purge_idle, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_purge_idle_s, 0, "0417: s without a swap before a volatile surface may go");
SYSCTL_UINT(_kern, OID_AUTO, n2mem_budget_mib, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_mem_budget_mib, 0, "0417: one budget for surfaces + GPU private (MiB, 0 off)");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_proc_mib, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_surf_proc_mib, 0, "0417: surface MiB one process may make (0 off)");
SYSCTL_UINT(_kern, OID_AUTO, n2mem_floor, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_mem_floor, 0, "0418: available pages under which allocations are refused");
SYSCTL_UINT(_kern, OID_AUTO, n2mem_gpu_cap_mib, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_gpu_cap_mib, 0, "0418: GPU private cap MiB");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_cap_mib, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_surf_cap_mib, 0, "0418: surface wired cap MiB");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_purged, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_purge_n, 0, "0417: surfaces purged");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_unpurged, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_unpurge_n, 0, "0417: purged surfaces made non-volatile again");
static bool sweet_mem_admit(uint32_t size, int32_t pid);
static bool sweet_purge_unpurge_locked(SweetSurface *s);
static void sweet_mem_proc_charge(int32_t pid, int64_t delta);
static uintptr_t sweet_purge_sink_kva(void);
static void sweet_purge_start(void);
extern "C" bool sweet_mem_gpu_private_ok3(uint64_t total_private, uint64_t rounded, uint64_t limit);     /* called by nord2_gpu_transport.h */

static void
sweet_pool_args(void)
{
	if (sweet_pool_args_done) {
		return;
	}
	uint32_t v = 0;
	/* n2fp = short alias (the command line has 4-11 chars left), so backboardd's boot-time surfaces can be pooled too */
	if (PE_parse_boot_argn("n2fbhw_pool", &v, sizeof(v)) || PE_parse_boot_argn("n2fp", &v, sizeof(v))) {
		sweet_pool_enabled = v ? 1 : 0;
	}
	v = 0;
	if (PE_parse_boot_argn("n2fbpoolc", &v, sizeof(v))) {
		sweet_pool_copyback = v ? 1 : 0;
	}
	sweet_pool_args_done = true;
}

static void
sweet_pool_flush(vm_offset_t va, size_t len)
{
	if (sweet_pool_copyback) {
		FlushPoC_DcacheRegion(va, len);         /* no dirty CPU line may later land over GPU/OVL data */
	} else {
		__builtin_arm_dsb(0xf);                 /* WC: drain the write buffers */
	}
}

/* 0650: granules [lim, NGRAN) marked in use in the pool bitmap: never handed out, never given back (not counted in used/live).  Spin held. */
static void
sweet_pool_reserve_tail_locked(uint32_t lim)
{
	for (uint32_t g = lim; g < SWEET_FB_POOL_NGRAN; g++) {
		sweet_pool.bits[g >> 6] |= 1ull << (g & 63);
	}
}

/* Map the pool and check it once (thread context; creation paths only). */
static bool
sweet_pool_ready(void)
{
	if (sweet_pool_state != 0) {
		return sweet_pool_state == 1;
	}
	IOLockLock(sweet_surface_lock);
	if (sweet_pool_state == 0) {
		sweet_pool_args();
		sweet_pool_spin = IOSimpleLockAlloc();
		vm_offset_t va = sweet_pool_spin != NULL ? ml_io_map_unmappable((vm_offset_t)SWEET_FB_HW_POOL_BASE,
		    (vm_size_t)SWEET_FB_HW_POOL_SIZE, sweet_pool_copyback ? SWEET_POOL_WIMG_COPYBACK : SWEET_POOL_WIMG_WCOMB) : 0;
		uint32_t bad = 0;
		if (va != 0) {
			for (uint32_t g = 0; g < sweet_pool_ngran_lim; g++) {     /* 0650: never the part lent to the heap */
				sweet_fb_pool_canary_put((volatile uint64_t *)(va + (uint64_t)g * SWEET_FB_HW_POOL_GRANULE), g);
				sweet_pool_flush(va + (uint64_t)g * SWEET_FB_HW_POOL_GRANULE, 64);
			}
			for (uint32_t g = 0; g < sweet_pool_ngran_lim; g++) {
				if (!sweet_fb_pool_canary_ok((volatile uint64_t *)(va + (uint64_t)g * SWEET_FB_HW_POOL_GRANULE), g)) {
					bad++;
				}
			}
		}
		sweet_pool_kva = (volatile uint8_t *)va;
		if (sweet_pool_spin != NULL && sweet_pool_ngran_lim < SWEET_FB_POOL_NGRAN) {
			IOSimpleLockLock(sweet_pool_spin);
			sweet_pool_reserve_tail_locked(sweet_pool_ngran_lim);  /* 0650: lent before the pool's first use */
			IOSimpleLockUnlock(sweet_pool_spin);
		}
		__builtin_arm_dmb(0xb);
		sweet_pool_state = (va != 0 && bad == 0) ? 1 : 2;
		IOLog("NORD2 FB POOL: pa %#llx size %llu MiB granule %llu KiB, %s mapping at %#lx, canary readback %u bad of %u -> %s\n",
		    SWEET_FB_HW_POOL_BASE, ((uint64_t)sweet_pool_ngran_lim * SWEET_FB_HW_POOL_GRANULE) >> 20, SWEET_FB_HW_POOL_GRANULE >> 10,
		    sweet_pool_copyback ? "write-back" : "write-combined", (unsigned long)va, bad, sweet_pool_ngran_lim,
		    sweet_pool_state == 1 ? "ready" : "disabled for this boot");
	}
	IOLockUnlock(sweet_surface_lock);
	return sweet_pool_state == 1;
}

/*
 * 0650: lend the pool's upper part [POOL_BASE + SWEET_GFX_HEAP_POOL_KEEP, POOL_END) to the graphics heap (region dph) for the rest of the boot.
 * Refused while the pool is enabled (n2fbhw_pool / n2fp boot-arg or sysctl), write-back (n2fbpoolc: a cacheable alias of WC heap pages),
 * disabled by its own canary, or already holding memory up there.  Afterwards the pool only ever touches its first 16 MiB (canary loops,
 * bitmap tail reserved), so a later kern.n2fbhw_pool=1 still works inside 16 MiB.  Thread context (the heap tick).
 */
static bool
sweet_pool_lend_hi(uint64_t base, uint64_t size, const char **why)
{
	const uint32_t lim = (uint32_t)(SWEET_GFX_HEAP_POOL_KEEP / SWEET_FB_HW_POOL_GRANULE);
	bool ok = false;
	if (base < SWEET_FB_HW_POOL_BASE + SWEET_GFX_HEAP_POOL_KEEP || size == 0 || base + size > SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE ||
	    sweet_surface_lock == NULL) {
		*why = "region outside the display pool's upper part";
		return false;
	}
	IOLockLock(sweet_surface_lock);
	sweet_pool_args();
	if (sweet_pool_hi_lent) {
		ok = true;
	} else if (sweet_pool_enabled) {
		*why = "the display pool is enabled (n2fbhw_pool / n2fp)";
	} else if (sweet_pool_copyback) {
		*why = "the display pool is write-back (n2fbpoolc)";
	} else if (sweet_pool_state == 2) {
		*why = "the display pool failed its own canary check this boot";
	} else if (sweet_pool_state == 1 && sweet_pool_spin != NULL) {
		bool busy = false;
		IOSimpleLockLock(sweet_pool_spin);
		for (uint32_t g = lim; g < SWEET_FB_POOL_NGRAN && !busy; g++) {
			busy = sweet_fb_pool_test(&sweet_pool, g);
		}
		if (!busy) {
			sweet_pool_reserve_tail_locked(lim);
		}
		IOSimpleLockUnlock(sweet_pool_spin);
		ok = !busy;
		if (busy) {
			*why = "the display pool already holds memory above its first 16 MiB";
		}
	} else if (sweet_pool_state == 0) {
		ok = true;                      /* sweet_pool_ready() reserves the tail when it sets the pool up */
	} else {
		*why = "display pool state unknown";
	}
	if (ok && !sweet_pool_hi_lent) {
		sweet_pool_ngran_lim = lim;
		sweet_pool_hi_lent = true;
		IOLog("NORD2 FB POOL: 0650: pa %#llx..%#llx lent to the graphics heap; the pool keeps %#llx + %llu MiB for this boot (pool state %u)\n",
		    SWEET_FB_HW_POOL_BASE + SWEET_GFX_HEAP_POOL_KEEP, SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE, SWEET_FB_HW_POOL_BASE,
		    SWEET_GFX_HEAP_POOL_KEEP >> 20, sweet_pool_state);
	}
	IOLockUnlock(sweet_surface_lock);
	return ok;
}

/* PA of a pool surface's pixels, 0 for every other surface. */
static uint64_t
sweet_pool_surface_pa(const SweetSurface *cs)
{
	SweetSurface *s = const_cast<SweetSurface *>(cs);
	if (s == NULL || s->memory == NULL || sweet_pool_state != 1 || OSDynamicCast(IOBufferMemoryDescriptor, s->memory) != NULL) {
		return 0;
	}
	IOByteCount len = 0;
	uint64_t pa = s->memory->getPhysicalSegment(0, &len, kIOMemoryMapperNone);
	uint64_t bytes = s->memory->getLength();
	/* 0650: a surface in the lent upper part is a graphics-heap surface (region dph), never a pool surface */
	return sweet_fb_hw_pool_contains(pa, bytes) && pa - SWEET_FB_HW_POOL_BASE + bytes <= (uint64_t)sweet_pool_ngran_lim * SWEET_FB_HW_POOL_GRANULE ? pa : 0;
}

static void
sweet_pool_publish_stats(void)
{
	sweet_pool_live = sweet_pool.live;
	sweet_pool_used_kib = (uint32_t)(sweet_pool.used * (SWEET_FB_HW_POOL_GRANULE >> 10));
	sweet_pool_full = (uint32_t)sweet_pool.full;
}

/* Contiguous, zeroed pool memory of bytes; 0 when the pool is off or full (the caller falls back to normal memory). */
static uint64_t
sweet_pool_take(uint64_t bytes)
{
	if (!sweet_pool_ready()) {
		return 0;
	}
	IOSimpleLockLock(sweet_pool_spin);
	uint64_t off = sweet_fb_pool_alloc(&sweet_pool, bytes);
	uint32_t largest = off == SWEET_FB_POOL_FAIL ? sweet_fb_pool_largest_free(&sweet_pool) : 0;
	sweet_pool_publish_stats();
	IOSimpleLockUnlock(sweet_pool_spin);
	if (off == SWEET_FB_POOL_FAIL) {
		if (sweet_pool_full <= 8 || (sweet_pool_full % 100) == 0) {
			IOLog("NORD2 FB POOL: full for %llu bytes (live %u, used %u MiB, largest free %llu MiB, full %u) -> normal memory\n",
			    bytes, sweet_pool_live, sweet_pool_used_kib >> 10, ((uint64_t)largest * SWEET_FB_HW_POOL_GRANULE) >> 20, sweet_pool_full);
		}
		return 0;
	}
	uint32_t g0 = (uint32_t)(off / SWEET_FB_HW_POOL_GRANULE), n = sweet_fb_pool_granules(bytes), foreign = 0;
	for (uint32_t g = g0; g < g0 + n; g++) {
		if (!sweet_fb_pool_canary_ok((volatile uint64_t *)(sweet_pool_kva + (uint64_t)g * SWEET_FB_HW_POOL_GRANULE), g)) {
			if (foreign++ < 2) {
				IOLog("NORD2 FB POOL: foreign write in free granule at pa %#llx (seen at alloc)\n",
				    SWEET_FB_HW_POOL_BASE + (uint64_t)g * SWEET_FB_HW_POOL_GRANULE);
			}
		}
	}
	if (foreign != 0) {
		OSAddAtomic((SInt32)foreign, (volatile SInt32 *)&sweet_pool_foreign);
	}
	bzero((void *)(sweet_pool_kva + off), (size_t)bytes);
	sweet_pool_flush((vm_offset_t)(sweet_pool_kva + off), (size_t)bytes);
	return SWEET_FB_HW_POOL_BASE + off;
}

/* Give a pool range back (SweetSurface::free, after every mapping of it is gone). */
static void
sweet_pool_give(uint64_t pa, uint64_t bytes)
{
	uint64_t off = pa - SWEET_FB_HW_POOL_BASE;
	uint32_t g0 = (uint32_t)(off / SWEET_FB_HW_POOL_GRANULE), n = sweet_fb_pool_granules(bytes);
	for (uint32_t g = g0; g < g0 + n && g < SWEET_FB_POOL_NGRAN; g++) {
		sweet_fb_pool_canary_put((volatile uint64_t *)(sweet_pool_kva + (uint64_t)g * SWEET_FB_HW_POOL_GRANULE), g);
		sweet_pool_flush((vm_offset_t)(sweet_pool_kva + (uint64_t)g * SWEET_FB_HW_POOL_GRANULE), 64);
	}
	IOSimpleLockLock(sweet_pool_spin);
	bool ok = sweet_fb_pool_free(&sweet_pool, off, bytes);
	sweet_pool_publish_stats();
	IOSimpleLockUnlock(sweet_pool_spin);
	uint32_t k = sweet_pool_logged_free++;
	if (!ok || k < 32 || (k % 64) == 0) {
		IOLog("NORD2 FB POOL: free pa %#llx %llu bytes%s (live %u, used %u MiB)\n", pa, bytes, ok ? "" : " REFUSED (not allocated)",
		    sweet_pool_live, sweet_pool_used_kib >> 10);
	}
}

/*
 * Put s (geometry set, allocSize final) into the pool: memory + mapCache; false = pool off or full.
 * 0406: from a client's create (user = true, no lock held) evicted ranges whose grace is over come back first, and a full pool runs
 * the reclaim decision (mode 2: evictions whose ranges come back after the grace, i.e. for the next creations).  Kernel-made pool
 * surfaces (H2 test patterns, possibly under framebuffer locks) never reclaim.
 */
static bool
sweet_pool_attach(SweetSurface *s, bool user)
{
	if (user && __atomic_load_n(&sweet_pool_reclaim_npend, __ATOMIC_RELAXED) != 0) {
		sweet_pool_reclaim_drain();
	}
	uint64_t pa = sweet_pool_take(s->allocSize);
	if (pa == 0) {
		if (user) {
			sweet_pool_reclaim(s->allocSize);
		}
		return false;
	}
	s->memory = IOMemoryDescriptor::withPhysicalAddress(pa, s->allocSize, kIODirectionInOut);
	if (s->memory == NULL) {
		sweet_pool_give(pa, s->allocSize);
		return false;
	}
	s->mapCache = sweet_pool_copyback ? kIOMapCopybackCache : kIOMapWriteCombineCache;
	IOLockLock(sweet_surface_lock);         /* 0398: the leak report finds it here until SweetSurface::free */
	(void)sweet_pool_reg_add(&sweet_pool_reg, s);
	IOLockUnlock(sweet_surface_lock);
	return true;
}

static void
sweet_pool_log_alloc(SweetSurface *s, const char *who)
{
	uint32_t k = sweet_pool_logged_alloc++;
	if (k < 32 || (k % 64) == 0) {
		IOLog("NORD2 FB POOL: %s id %u %ux%u bpr %u -> pa %#llx size %u (live %u, used %u MiB of %llu)\n", who, s->id, s->width,
		    s->height, s->bytesPerRow, sweet_pool_surface_pa(s), s->allocSize, sweet_pool_live, sweet_pool_used_kib >> 10,
		    SWEET_FB_HW_POOL_SIZE >> 20);
	}
}

#include "sweet_surface_wod_kern.inc"   /* 0480: wire-on-demand surfaces (kern.n2surf_wod, default off) */

static uint32_t
sweet_surface_uptime(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return (uint32_t)(ns / 1000000000ull);
}

#include "sweet_gfx_heap_kern.inc"    /* 0440: low-bank graphics heap (before the IOMMU: n2io_ram accepts its pages) */
#include "sweet_fb_iommu_hw.h"        /* 0401 IOMMU: display IOMMU (uses the pool code above for its page tables) */

OSDefineMetaClassAndStructors(SweetSurface, OSObject);

void
SweetSurface::free()
{
	sweet_fb_iommu_surface_gone(this);      /* 0401 IOMMU: unmap before the pages can go */
	sweet_wod_free(this);                   /* 0480: unwire (a pageable surface is in sweet_surface_wired only while wired) */
	if (OSDynamicCast(IOBufferMemoryDescriptor, memory) != NULL) {
		OSAddAtomic64(-(SInt64)allocSize, (volatile SInt64 *)&sweet_surface_wired);
		OSDecrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
		sweet_mem_proc_charge(creator, -(int64_t)allocSize);   /* 0417 */
	} else if (purged) {    /* 0417: a view of the sink: its buffer already left the accounting at the purge */
		OSDecrementAtomic((volatile SInt32 *)&sweet_purged_live);
		OSAddAtomic64(-(SInt64)allocSize, (volatile SInt64 *)&sweet_purged_bytes);
	}
	/* 0394: a pool surface gives its range back once nothing else holds the descriptor (a holder could still map it). */
	uint64_t poolPa = sweet_pool_surface_pa(this), poolLen = poolPa != 0 ? memory->getLength() : 0;
	bool poolHeld = poolPa != 0 && memory->getRetainCount() > 1;
	/* 0440: a heap surface likewise (never a pool surface: the two ranges are disjoint) */
	uint64_t heapPa = poolPa == 0 ? sweet_heap_surface_pa(this) : 0, heapLen = heapPa != 0 ? memory->getLength() : 0;
	bool heapHeld = heapPa != 0 && memory->getRetainCount() > 1;
	/* 0398: nobody maps or holds this surface any more, so its use counts must be 0 (else the accounting lost a give-back) */
	if (state != NULL) {
		volatile uint32_t *w = (volatile uint32_t *)((uintptr_t)state->getBytesNoCopy() + SWEET_USE_STATE_WORDS);
		if (!sweet_use_free_check(w, useKern, &sweet_use_st) && sweet_use_told_free++ < 16) {
			IOLog("NORD2 SURF USE: LEAK at free: surface %u %ux%u fmt 0x%x freed with counts %u/%u, display %u, gpu %u (free-nonzero %llu)\n",
			    id, width, height, pixelFormat, w[0], w[1], useKern[0], useKern[1], sweet_use_st.free_nonzero);
		}
	}
	if (poolPa != 0 && sweet_surface_lock != NULL) {
		IOLockLock(sweet_surface_lock);         /* 0398: out of the registry before the fields go */
		(void)sweet_pool_reg_remove(&sweet_pool_reg, this);
		IOLockUnlock(sweet_surface_lock);
	}
	OSSafeReleaseNULL(kernelMap);
	OSSafeReleaseNULL(memory);
	OSSafeReleaseNULL(state);
	if (poolPa != 0 && !poolHeld) {
		sweet_pool_give(poolPa, poolLen);
	} else if (poolHeld) {
		OSIncrementAtomic((volatile SInt32 *)&sweet_pool_leaked);
		IOLog("NORD2 FB POOL: pa %#llx %llu bytes kept out of the pool: its descriptor is still held (leaked %u)\n",
		    poolPa, poolLen, sweet_pool_leaked);
	}
	if (heapPa != 0 && !heapHeld) {        /* 0440 */
		sweet_heap_give(heapPa, heapLen, false);
	} else if (heapHeld) {
		OSIncrementAtomic((volatile SInt32 *)&sweet_heap_leaked);
		IOLog("NORD2 HEAP: surface pa %#llx %llu KiB kept out of the heap: its descriptor is still held (quarantined %u)\n",
		    heapPa, heapLen >> 10, sweet_heap_leaked);
	}
	OSObject::free();
}

uintptr_t
SweetSurface::kernelAddress()
{
	IOBufferMemoryDescriptor *buffer = OSDynamicCast(IOBufferMemoryDescriptor, memory);
	if (buffer != NULL) {
		return (uintptr_t)buffer->getBytesNoCopy();
	}
	if (purged) {           /* 0417: the sink's permanent kernel mapping (never a kernelMap that an unpurge would have to drop) */
		return sweet_purge_sink_kva();
	}
	uint64_t poolPa = sweet_pool_surface_pa(this);         /* 0394: the pool's own kernel mapping (same attribute as the users') */
	if (poolPa != 0) {
		return (uintptr_t)(sweet_pool_kva + (poolPa - SWEET_FB_HW_POOL_BASE));
	}
	uint64_t heapPa = sweet_heap_surface_pa(this);         /* 0440: the heap region's own kernel mapping (write-combined, as the users') */
	if (heapPa != 0) {
		return (uintptr_t)sweet_heap_va(heapPa, memory->getLength());
	}
	IOLockLock(sweet_surface_lock);
	if (kernelMap == NULL && memory != NULL) {
		kernelMap = memory->map(kIOMapWriteCombineCache);
	}
	IOLockUnlock(sweet_surface_lock);
	return kernelMap != NULL ? (uintptr_t)kernelMap->getVirtualAddress() : 0;
}

static uint32_t *
sweet_surface_seed(SweetSurface *s)
{
	return (uint32_t *)((uint8_t *)s->state->getBytesNoCopy() + SWEET_SURFACE_STATE_SEED);
}

/* 0398: the two global use counts in the shared state page (IOSurfaceIsInUse reads them in every process). */
static volatile uint32_t *
sweet_use_words(SweetSurface *s)
{
	return (volatile uint32_t *)((uintptr_t)s->state->getBytesNoCopy() + SWEET_USE_STATE_WORDS);
}

extern "C" bool
sweet_surface_use_kernel(void *surface, uint32_t kind, int delta)
{
	SweetSurface *s = (SweetSurface *)surface;
	if (s == NULL || s->state == NULL || kind >= SWEET_USE_KINDS || delta == 0) {
		return false;
	}
	sweet_use_args();
	if (kind == SWEET_USE_DISPLAY && delta > 0) {   /* pool leak report: when a swap last named it (whatever the mode) */
		s->shownAt = sweet_surface_uptime();
		s->shownCount++;
	}
	return sweet_use_kern(s->useKern, sweet_use_words(s), (int)kind, delta > 0 ? 1 : -1, sweet_use_mode_now(), &sweet_use_st);
}

/*
 * Give the surface an id and a table slot; the table holds one retain and a
 * user count (creators, client mappings, kernel holders). Lock held.
 */
static bool
sweet_surface_insert_locked(SweetSurface *s)
{
	for (uint32_t n = 0; n < SWEET_SURFACE_MAX_ID - 1; n++) {
		uint32_t id = sweet_surface_next_id;
		sweet_surface_next_id = id + 1 < SWEET_SURFACE_MAX_ID ? id + 1 : 1;
		if (sweet_surface_table[id] == NULL) {
			s->id = id;
			s->retain();
			sweet_surface_table[id] = s;
			sweet_surface_users[id] = 1;
			sweet_surface_owner[id] = proc_selfpid();
			sweet_surface_seq[id] = sweet_surface_created;
			sweet_surface_born[id] = sweet_surface_uptime();
			*(uint64_t *)s->state->getBytesNoCopy() = id;   /* registry id */
			return true;
		}
	}
	return false;
}

/* Add a user to a surface that may have left the table (port lookups). */
static bool
sweet_surface_get(SweetSurface *s)
{
	bool ok = true;
	IOLockLock(sweet_surface_lock);
	if (sweet_surface_table[s->id] == s) {
		sweet_surface_users[s->id]++;
	} else if (sweet_surface_table[s->id] == NULL) {
		s->retain();
		sweet_surface_table[s->id] = s;
		sweet_surface_users[s->id] = 1;
	} else {
		ok = false;
	}
	IOLockUnlock(sweet_surface_lock);
	return ok;
}

/* Drop a user; the last one removes the surface from the table. */
static void
sweet_surface_put(SweetSurface *s)
{
	bool drop = false;
	IOLockLock(sweet_surface_lock);
	if (sweet_surface_table[s->id] == s && --sweet_surface_users[s->id] == 0) {
		sweet_surface_table[s->id] = NULL;
		drop = true;
	}
	IOLockUnlock(sweet_surface_lock);
	if (drop) {
		s->release();
	}
}

SweetSurface *
sweet_surface_copy(uint32_t id)
{
	SweetSurface *s = NULL;
	if (id == 0 || id >= SWEET_SURFACE_MAX_ID || sweet_surface_lock == NULL) {
		return NULL;
	}
	IOLockLock(sweet_surface_lock);
	s = sweet_surface_table[id];
	if (s != NULL) {
		s->retain();
	}
	IOLockUnlock(sweet_surface_lock);
	return s;
}

/* The table lock; the framebuffer may publish a surface before the root starts. */
static void
sweet_surface_init_lock(void)
{
	if (sweet_surface_lock == NULL) {
		IOLock *lock = IOLockAlloc();
		if (!OSCompareAndSwapPtr(NULL, lock, &sweet_surface_lock)) {
			IOLockFree(lock);
		}
	}
	if (sweet_mem_spin == NULL) {   /* 0417: the per-process table */
		IOSimpleLock *spin = IOSimpleLockAlloc();
		if (!OSCompareAndSwapPtr(NULL, spin, &sweet_mem_spin)) {
			IOSimpleLockFree(spin);
		}
	}
}

static SweetSurface *
sweet_surface_alloc(void)
{
	sweet_surface_init_lock();
	SweetSurface *s = OSTypeAlloc(SweetSurface);
	if (s == NULL || !s->init()) {
		OSSafeReleaseNULL(s);
		return NULL;
	}
	s->state = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size, page_size);
	if (s->state == NULL) {
		s->release();
		return NULL;
	}
	bzero(s->state->getBytesNoCopy(), page_size);
	s->bornAt = sweet_surface_uptime();     /* 0398 */
	return s;
}

static bool
sweet_surface_publish(SweetSurface *s)
{
	bool ok;
	IOLockLock(sweet_surface_lock);
	ok = sweet_surface_insert_locked(s);
	IOLockUnlock(sweet_surface_lock);
	return ok;
}

SweetSurface *
sweet_surface_create_physical(uint64_t phys, uint32_t width, uint32_t height,
    uint32_t bytesPerRow, uint32_t pixelFormat)
{
	SweetSurface *s = sweet_surface_alloc();
	if (s == NULL) {
		return NULL;
	}
	s->width = width;
	s->height = height;
	s->bytesPerRow = bytesPerRow;
	s->bytesPerElement = 4;
	s->elementWidth = s->elementHeight = 1;
	s->pixelFormat = pixelFormat;
	s->allocSize = (uint32_t)round_page(bytesPerRow * height);
	s->memory = IOMemoryDescriptor::withPhysicalAddress(phys, s->allocSize, kIODirectionInOut);
	s->mapCache = kIOMapWriteCombineCache;
	if (s->memory == NULL || !sweet_surface_publish(s)) {
		s->release();
		return NULL;
	}
	return s;
}

/* 0394: kernel-made pool surface (sweet_fb_hw_pool.h). */
SweetSurface *
sweet_surface_create_pool(uint32_t width, uint32_t height)
{
	uint64_t bpr = ((uint64_t)width * 4 + SWEET_SURFACE_ROW_ALIGN - 1) & ~(uint64_t)(SWEET_SURFACE_ROW_ALIGN - 1);
	if (width == 0 || height == 0 || width > SWEET_SURFACE_MAX_DIM || height > SWEET_SURFACE_MAX_DIM ||
	    round_page(bpr * height) > SWEET_FB_HW_POOL_SIZE) {
		return NULL;
	}
	SweetSurface *s = sweet_surface_alloc();
	if (s == NULL) {
		return NULL;
	}
	s->width = width;
	s->height = height;
	s->bytesPerRow = (uint32_t)bpr;
	s->bytesPerElement = 4;
	s->elementWidth = s->elementHeight = 1;
	s->pixelFormat = SWEET_FB_POOL_BGRA;
	s->allocSize = (uint32_t)round_page(bpr * height);
	if (!sweet_pool_attach(s, false) || !sweet_surface_publish(s)) {
		s->release();           /* gives the range back if it got one */
		return NULL;
	}
	sweet_pool_log_alloc(s, "kernel");
	return s;
}

/* 0394: what the GPU import (TRACK B) and the OVL planner (TRACK C) need (sweet_fb_hw_pool.h). */
extern "C" uint64_t
sweet_surface_pool_pa_obj(const void *surface)
{
	return sweet_pool_surface_pa((const SweetSurface *)surface);
}

extern "C" uint32_t
sweet_surface_pool_stride_obj(const void *surface)
{
	return sweet_pool_surface_pa((const SweetSurface *)surface) != 0 ? ((const SweetSurface *)surface)->bytesPerRow : 0;
}

extern "C" uint64_t
sweet_surface_pool_size_obj(const void *surface)
{
	return sweet_pool_surface_pa((const SweetSurface *)surface) != 0 ? ((const SweetSurface *)surface)->memory->getLength() : 0;
}

extern "C" uint32_t
sweet_surface_pool_cache_obj(const void *surface)
{
	if (sweet_pool_surface_pa((const SweetSurface *)surface) == 0) {
		return SWEET_FB_HW_POOL_CACHE_NONE;
	}
	return sweet_pool_copyback ? SWEET_FB_HW_POOL_CACHE_COPYBACK : SWEET_FB_HW_POOL_CACHE_WC;
}

#define SWEET_POOL_BY_ID(type, name) \
	extern "C" type \
	sweet_surface_pool_ ## name(uint32_t surface_id) \
	{ \
	        SweetSurface *s = sweet_surface_copy(surface_id); \
	        type v = s != NULL ? sweet_surface_pool_ ## name ## _obj(s) : 0; \
	        OSSafeReleaseNULL(s); \
	        return v; \
	}
SWEET_POOL_BY_ID(uint64_t, pa)
SWEET_POOL_BY_ID(uint32_t, stride)
SWEET_POOL_BY_ID(uint64_t, size)
SWEET_POOL_BY_ID(uint32_t, cache)

mach_port_name_t
sweet_surface_port_in_task(SweetSurface *s, task_t task)
{
	mach_port_name_t name = 0;
	IOUserClient::copyPortNameForObjectInTask(task, s, &name);
	return name;
}

static uint32_t
sweet_dict_u32(OSDictionary *dict, const char *key, uint32_t def)
{
	OSNumber *n = OSDynamicCast(OSNumber, dict->getObject(key));
	return n != NULL ? n->unsigned32BitValue() : def;
}

static uint32_t
sweet_div_up(uint32_t a, uint32_t b)
{
	return b > 1 ? (a + b - 1) / b : a;
}

static uint32_t
sweet_row_bytes(uint32_t width, uint32_t ew, uint32_t bpe)
{
	uint64_t row = (uint64_t)sweet_div_up(width, ew) * bpe;
	return (uint32_t)((row + SWEET_SURFACE_ROW_ALIGN - 1) & ~(uint64_t)(SWEET_SURFACE_ROW_ALIGN - 1));
}

/* Geometry from the creation properties (IOSurface.h keys). */
static IOReturn
sweet_surface_layout(SweetSurface *s, OSDictionary *props)
{
	uint64_t end;

	s->width = sweet_dict_u32(props, "IOSurfaceWidth", 0);
	s->height = sweet_dict_u32(props, "IOSurfaceHeight", 0);
	s->bytesPerElement = (uint16_t)sweet_dict_u32(props, "IOSurfaceBytesPerElement", 4);
	s->elementWidth = (uint8_t)sweet_dict_u32(props, "IOSurfaceElementWidth", 1);
	s->elementHeight = (uint8_t)sweet_dict_u32(props, "IOSurfaceElementHeight", 1);
	s->pixelFormat = sweet_dict_u32(props, "IOSurfacePixelFormat", 0);
	s->cacheMode = sweet_dict_u32(props, "IOSurfaceCacheMode", 0);
	s->offset = sweet_dict_u32(props, "IOSurfaceOffset", 0);
	if (s->width > SWEET_SURFACE_MAX_DIM || s->height > SWEET_SURFACE_MAX_DIM ||
	    s->bytesPerElement > 64 || s->elementWidth == 0 || s->elementHeight == 0) {
		return kIOReturnBadArgument;
	}
	s->bytesPerRow = sweet_dict_u32(props, "IOSurfaceBytesPerRow",
	    sweet_row_bytes(s->width, s->elementWidth, s->bytesPerElement));
	if (s->bytesPerRow < (uint64_t)sweet_div_up(s->width, s->elementWidth) * s->bytesPerElement) {
		return kIOReturnBadArgument;
	}
	end = (uint64_t)s->offset + (uint64_t)s->bytesPerRow * sweet_div_up(s->height, s->elementHeight);

	OSArray *planes = OSDynamicCast(OSArray, props->getObject("IOSurfacePlaneInfo"));
	s->planeCount = 0;
	if (planes != NULL) {
		uint64_t next = s->offset;
		if (planes->getCount() > SWEET_SURFACE_MAX_PLANES) {
			return kIOReturnBadArgument;
		}
		for (unsigned int i = 0; i < planes->getCount(); i++) {
			OSDictionary *pd = OSDynamicCast(OSDictionary, planes->getObject(i));
			struct sweet_surface_plane *p = &s->planes[i];
			if (pd == NULL) {
				return kIOReturnBadArgument;
			}
			p->width = sweet_dict_u32(pd, "IOSurfacePlaneWidth", s->width);
			p->height = sweet_dict_u32(pd, "IOSurfacePlaneHeight", s->height);
			p->bytesPerElement = (uint16_t)sweet_dict_u32(pd, "IOSurfacePlaneBytesPerElement", 1);
			p->elementWidth = (uint8_t)sweet_dict_u32(pd, "IOSurfacePlaneElementWidth", 1);
			p->elementHeight = (uint8_t)sweet_dict_u32(pd, "IOSurfacePlaneElementHeight", 1);
			if (p->width > SWEET_SURFACE_MAX_DIM || p->height > SWEET_SURFACE_MAX_DIM ||
			    p->bytesPerElement > 64 || p->elementWidth == 0 || p->elementHeight == 0) {
				return kIOReturnBadArgument;
			}
			p->bytesPerRow = sweet_dict_u32(pd, "IOSurfacePlaneBytesPerRow",
			    sweet_row_bytes(p->width, p->elementWidth, p->bytesPerElement));
			p->offset = sweet_dict_u32(pd, "IOSurfacePlaneOffset", (uint32_t)next);
			p->size = sweet_dict_u32(pd, "IOSurfacePlaneSize",
			    p->bytesPerRow * sweet_div_up(p->height, p->elementHeight));
			next = round_page((uint64_t)p->offset + p->size);
			if ((uint64_t)p->offset + p->size > end) {
				end = (uint64_t)p->offset + p->size;
			}
		}
		s->planeCount = planes->getCount();
	}

	uint64_t alloc = sweet_dict_u32(props, "IOSurfaceAllocSize", 0);
	if (alloc < end) {
		alloc = end;
	}
	alloc = round_page(alloc);
	if (alloc == 0 || alloc > SWEET_SURFACE_MAX_ALLOC) {
		return kIOReturnNoMemory;
	}
	s->allocSize = (uint32_t)alloc;
	return kIOReturnSuccess;
}

/* Per-client mapping of one surface. */
class SweetSurfaceRef : public OSObject {
	OSDeclareDefaultStructors(SweetSurfaceRef);

public:
	SweetSurface *surface;
	IOMemoryMap  *data;
	IOMemoryMap  *state;
	uint32_t      count;
	uint8_t       useHeld;      /* 0398: bit c = this client holds use-count category c of the surface (traps 0/1) */
	int32_t       pid;          /* 0398: the client's process (logs) */

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(SweetSurfaceRef, OSObject);

void
SweetSurfaceRef::free()
{
	OSSafeReleaseNULL(data);
	OSSafeReleaseNULL(state);
	if (surface != NULL && useHeld != 0 && surface->state != NULL) {
		/* 0398: released (trap 4/5, close, process death) while in use: the process can no longer give its holds back */
		uint8_t had = useHeld;
		unsigned n = sweet_use_user_drop(&useHeld, sweet_use_words(surface), &sweet_use_st);
		if (n != 0 && sweet_use_told_drop++ < 12) {
			IOLog("NORD2 SURF USE: pid %d let surface %u go with use count held (categories %#x): %u hold(s) dropped (dropped %llu)\n",
			    pid, surface->id, had, n, sweet_use_st.dropped);
		}
	}
	if (surface != NULL) {
		sweet_surface_put(surface);
		surface->release();
		surface = NULL;
	}
	OSObject::free();
}

class IOCoreSurfaceRoot : public IOService {
	OSDeclareDefaultStructors(IOCoreSurfaceRoot);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(IOCoreSurfaceRoot, IOService);

static void sweet_m2_install(void);     /* 0672: sweet_m2scaler.inc (AppleM2ScalerCSCDriver over the MDP) */

bool
IOCoreSurfaceRoot::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	sweet_surface_init_lock();
	sweet_pool_args();      /* 0394: kern.n2fbhw_pool's boot default; the pool itself is mapped at its first use */
	if (sweet_pool_evict_lock == NULL) {
		sweet_pool_evict_lock = IOLockAlloc();  /* 0406 */
	}
	sweet_purge_start();    /* 0417: sink, 2 s pressure scan, NORD2 MEM SURF line */
	sweet_heap_start();     /* 0440: graphics heap (regions mapped by its thread call) */
	sweet_use_args();       /* 0398 */
	IOLog("NORD2 SURF USE: IOSurface use count mode %d (kern.n2surfuse / n2su: 0 off, 1 user traps, 2 + display/GPU holds), counts at state page +0x%x\n",
	    sweet_use_mode, SWEET_USE_STATE_WORDS);
	IOLog("NORD2 FB POOL: kern.n2fbhw_pool %d, cache %s (n2fbhw_pool / n2fbpoolc)\n", sweet_pool_enabled,
	    sweet_pool_copyback ? "write-back" : "write-combined");
	setProperty(kIOUserClientClassKey, "IOSurfaceRootUserClient");
	registerService();
	IOLog("SWEET SURF: IOCoreSurfaceRoot registered\n");
	sweet_m2_install();     /* 0672: MDP hooks; the service itself is registered after the MDP self-tests pass */
	return true;
}

class IOSurfaceRootUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOSurfaceRootUserClient);

	task_t   fTask;
	IOLock  *fLock;
	OSArray *fRefs;
	int32_t  fPid;          /* 0300 */
	friend void sweet_surface_report(const char *why, uint32_t asked);
	friend void sweet_pool_evict_remap(SweetSurface *s, IOMemoryDescriptor *old, IOMemoryDescriptor *buf, uint32_t *ok, uint32_t *bad);
	friend void sweet_purge_remap(SweetSurface *s, IOMemoryDescriptor *old, IOMemoryDescriptor *buf, uint32_t *ok, uint32_t *bad);

	SweetSurfaceRef *findRef(uint32_t id, unsigned int *index);
	IOReturn addRef(SweetSurface *s, SweetSurfaceRef **refOut, bool *existing);
	IOReturn returnSurface(SweetSurface *s, IOExternalMethodArguments *args);
	IOReturn create(IOExternalMethodArguments *args);

public:
	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	IOExternalTrap *getTargetAndTrapForIndex(IOService **targetP, UInt32 index) APPLE_KEXT_OVERRIDE;

	IOReturn trapUseCount(void *id, void *category, bool inc);
	IOReturn trapIncrementUse(void *id, void *category, void *, void *, void *, void *);
	IOReturn trapDecrementUse(void *id, void *category, void *, void *, void *, void *);
	IOReturn trapLock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapUnlock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapRelease(void *id, void *, void *, void *, void *, void *);
};

OSDefineMetaClassAndStructors(IOSurfaceRootUserClient, IOUserClient);

/*
 * 0300: who HOLDS the surfaces. A surface stays in the table (and wired) as long as one client has it in its fRefs: its creator or
 * anybody that looked it up; the table itself keeps nothing alive (sweet_surface_put). Run user3 showed all 719 buffer surfaces in
 * the table and a dead creator still "owning" 16 of them, i.e. somebody else holds those. So every open client is listed here
 * and a refusal says, besides the creators (0299): the holders, what of it is volatile (IOSurfaceSetPurgeable: the holder said the
 * contents may be thrown away; nothing is thrown away here, the pages are wired and mapped), which sizes, how old.
 * Lock order: sweet_clients_lock, then a client's fLock, then sweet_surface_lock (as fRefs' removal already takes the last two).
 */
#define SWEET_CLIENTS_MAX 512
static IOSurfaceRootUserClient *sweet_clients[SWEET_CLIENTS_MAX];
static IOLock *sweet_clients_lock;

/*
 * 0406 (poolreclaim-20261114): pool eviction (sweet_fb_pool_reclaim.h).  An evicted surface keeps its id, its state page and every
 * client address: each client mapping of the old pool range is replaced in place by a mapping of new zeroed normal memory
 * (createMappingInTask(kIOMapOverwrite) at the same address = vm_map_enter overwrite of the old entry; the new IOMemoryMap then owns the
 * range, the old one is released without unmapping it), the surface's purgeable state becomes Empty and its old descriptor (with the
 * pool range) waits in sweet_reclaim_pend until sweet_reclaim_grace() says nothing can reach it any more.
 */
#define SWEET_RECLAIM_PEND_MAX  16u
struct sweet_reclaim_pend_ent { SweetSurface *s; IOMemoryDescriptor *old; uint64_t pa, len; uint32_t armed_at, shown_at; };
static struct sweet_reclaim_pend_ent sweet_reclaim_pend[SWEET_RECLAIM_PEND_MAX];        /* under sweet_pool_evict_lock */

/* Every client mapping of s that still shows old: remapped onto buf at the same address.  Holds sweet_clients_lock and each fLock. */
void
sweet_pool_evict_remap(SweetSurface *s, IOMemoryDescriptor *old, IOMemoryDescriptor *buf, uint32_t *ok, uint32_t *bad)
{
	if (sweet_clients_lock == NULL) {
		return;
	}
	IOLockLock(sweet_clients_lock);
	for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
		IOSurfaceRootUserClient *c = sweet_clients[i];
		if (c == NULL || c->fLock == NULL || c->fRefs == NULL) {
			continue;
		}
		IOLockLock(c->fLock);
		for (unsigned int k = 0; k < c->fRefs->getCount(); k++) {
			SweetSurfaceRef *ref = (SweetSurfaceRef *)c->fRefs->getObject(k);
			IOMemoryMap *om = ref != NULL && ref->surface == s ? ref->data : NULL;
			if (om == NULL || om->getMemoryDescriptor() != old) {
				continue;
			}
			IOMemoryMap *nm = buf->createMappingInTask(om->getAddressTask(), om->getAddress(), kIOMapOverwrite | kIOMapDefaultCache,
			    0, om->getLength());
			if (nm == NULL || nm->getAddress() != om->getAddress()) {
				OSSafeReleaseNULL(nm);  /* (an overwrite map never deallocates on release) */
				(*bad)++;
				continue;
			}
			nm->fOptions &= ~(IOOptionBits)kIOMapOverwrite;  /* the new map owns the range: its release unmaps it */
			om->fOptions |= kIOMapOverwrite;                  /* the old one must not unmap what is now nm's */
			ref->data = nm;
			om->release();
			(*ok)++;
		}
		IOLockUnlock(c->fLock);
	}
	IOLockUnlock(sweet_clients_lock);
}

/* The decision inputs of a registry surface.  sweet_surface_lock held (gpu_busy filled by the caller, outside the lock). */
static void
sweet_reclaim_fill(SweetSurface *t, struct sweet_reclaim_cand *c)
{
	uint64_t pa = sweet_pool_surface_pa(t);
	volatile uint32_t *w = sweet_use_words(t);
	bzero(c, sizeof(*c));
	c->pool = pa != 0 && t->kernelMap == NULL;
	c->g0 = pa != 0 ? (uint32_t)((pa - SWEET_FB_HW_POOL_BASE) / SWEET_FB_HW_POOL_GRANULE) : 0;
	c->ngran = pa != 0 ? sweet_fb_pool_granules(t->memory->getLength()) : 0;
	c->born_at = t->bornAt;
	c->shown_at = t->shownAt;
	c->purgeable = t->purgeable;
	c->w0 = w[0];
	c->w1 = w[1];
	c->kd = t->useKern[0];
	c->kg = t->useKern[1];
	c->imports = t->gpuImports;
	c->creator = t->creator;
}

static uint32_t
sweet_reclaim_verdict_now(const struct sweet_reclaim_cand *c, uint32_t now)
{
	return sweet_reclaim_verdict(c, now, __atomic_load_n(&sweet_pool_age, __ATOMIC_RELAXED), sweet_use_mode_now() == SWEET_USE_MODE_ALL,
	           n2io_state == 1);
}

/* Evicted ranges whose grace is over go back to the pool.  sweet_pool_evict_lock held. */
static void
sweet_pool_reclaim_drain_locked(uint32_t now)
{
	for (uint32_t i = 0; i < SWEET_RECLAIM_PEND_MAX; i++) {
		struct sweet_reclaim_pend_ent *e = &sweet_reclaim_pend[i];
		if (e->s == NULL) {
			continue;
		}
		int v = sweet_reclaim_grace(now, e->armed_at, __atomic_load_n(&sweet_pool_reclaim_grace_s, __ATOMIC_RELAXED), e->shown_at,
		    e->s->shownAt, e->s->useKern[0], e->s->useKern[1], e->s->gpuImports, (uint32_t)e->old->getRetainCount());
		if (v == SWEET_RECLAIM_REARM) {
			e->armed_at = now;
			e->shown_at = e->s->shownAt;
		} else if (v == SWEET_RECLAIM_GIVE) {
			OSSafeReleaseNULL(e->old);
			sweet_pool_give(e->pa, e->len);
			OSIncrementAtomic((volatile SInt32 *)&sweet_pool_reclaim_given);
			IOLog("NORD2 FB POOL RECLAIM: surface %u: old range pa %#llx (%llu MiB) back in the pool (given %u)\n", e->s->id, e->pa,
			    e->len >> 20, sweet_pool_reclaim_given);
			OSSafeReleaseNULL(e->s);
			__atomic_sub_fetch(&sweet_pool_reclaim_npend, 1, __ATOMIC_RELAXED);
		}
	}
}

static void
sweet_pool_reclaim_drain(void)
{
	if (sweet_pool_evict_lock == NULL) {
		return;
	}
	IOLockLock(sweet_pool_evict_lock);
	sweet_pool_reclaim_drain_locked(sweet_surface_uptime());
	IOLockUnlock(sweet_pool_evict_lock);
}

/* Move s out of the pool (verdict re-checked under sweet_surface_lock).  sweet_pool_evict_lock held. */
static bool
sweet_pool_evict_one(SweetSurface *s, uint32_t now)
{
	uint32_t slot = SWEET_RECLAIM_PEND_MAX;
	for (uint32_t i = 0; i < SWEET_RECLAIM_PEND_MAX && slot == SWEET_RECLAIM_PEND_MAX; i++) {
		slot = sweet_reclaim_pend[i].s == NULL ? i : slot;
	}
	if (slot == SWEET_RECLAIM_PEND_MAX || s->state == NULL) {
		return false;
	}
	uint32_t size = s->allocSize;
	if ((uint64_t)OSAddAtomic64(size, (volatile SInt64 *)&sweet_surface_wired) + size > SWEET_SURFACE_WIRED_MAX) {
		OSAddAtomic64(-(SInt64)size, (volatile SInt64 *)&sweet_surface_wired);
		return false;
	}
	IOBufferMemoryDescriptor *buf = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, size, page_size);
	if (buf == NULL || buf->getLength() < size) {
		OSSafeReleaseNULL(buf);
		OSAddAtomic64(-(SInt64)size, (volatile SInt64 *)&sweet_surface_wired);
		return false;
	}
	bzero(buf->getBytesNoCopy(), size);
	bool busy = nord2_gpu_surface_busy(s);
	struct sweet_reclaim_cand c;
	IOLockLock(sweet_surface_lock);
	sweet_reclaim_fill(s, &c);
	c.gpu_busy = busy;
	uint32_t v = sweet_reclaim_verdict_now(&c, now);
	IOMemoryDescriptor *old = s->memory;
	uint64_t pa = sweet_pool_surface_pa(s), len = old != NULL ? old->getLength() : 0;
	if (v != SWEET_RECLAIM_OK || pa == 0 || len < size) {
		IOLockUnlock(sweet_surface_lock);
		buf->release();
		OSAddAtomic64(-(SInt64)size, (volatile SInt64 *)&sweet_surface_wired);
		return false;
	}
	s->memory = buf;                        /* the reference moves from buf to s; old's moves to the pending record */
	s->mapCache = kIOMapDefaultCache;       /* as every normal-memory surface */
	s->purgeable = SWEET_PURGEABLE_EMPTY;   /* the next IOSurfaceSetPurgeable answers Empty: the client redraws */
	(void)sweet_pool_reg_remove(&sweet_pool_reg, s);
	IOLockUnlock(sweet_surface_lock);
	OSIncrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
	s->retain();
	struct sweet_reclaim_pend_ent e = { s, old, pa, len, now, s->shownAt };
	sweet_reclaim_pend[slot] = e;
	__atomic_add_fetch(&sweet_pool_reclaim_npend, 1, __ATOMIC_RELAXED);
	uint32_t ok = 0, bad = 0;
	sweet_pool_evict_remap(s, old, buf, &ok, &bad);
	OSIncrementAtomic((volatile SInt32 *)&sweet_pool_evicted);
	if (bad != 0) {
		OSAddAtomic((SInt32)bad, (volatile SInt32 *)&sweet_pool_remap_failed);
	}
	char name[32] = "kernel";
	if (s->creator > 0) {
		proc_name(s->creator, name, sizeof(name));
	}
	IOLog("NORD2 FB POOL RECLAIM: evicted surface %u by %s[%d] (volatile, idle, last swap %s%u s ago): pa %#llx %u MiB -> normal memory, "
	    "%u client mapping(s) remapped%s%u, range back after %u s grace (evicted %u)\n", s->id, name, s->creator, s->shownAt ? "" : "never; made ",
	    now - (s->shownAt ? s->shownAt : s->bornAt), pa, size >> 20, ok, bad ? ", LEFT ON THE OLD RANGE (kept out of the pool): " : ", failed ",
	    bad, sweet_pool_reclaim_grace_s, sweet_pool_evicted);
	return true;
}

/* Pool full for bytes (client create, no lock held): decide (mode 1: log only), evict (mode 2).  At most one pass a second. */
static void
sweet_pool_reclaim(uint64_t bytes)
{
	static SweetSurface *cs[SWEET_POOL_REG_MAX];
	static struct sweet_reclaim_cand cand[SWEET_POOL_REG_MAX];
	static uint32_t verdict[SWEET_POOL_REG_MAX], victims[8];
	static struct sweet_fb_pool sim;
	static const char *why_name[SWEET_RECLAIM_NREASONS] = { "evictable", "not pool", "kernel", "use mode < 2", "iommu", "young", "active",
		                                                "non-volatile", "in use", "display", "gpu job", "gpu import", "gpu busy", "pending" };
	int mode = __atomic_load_n(&sweet_pool_reclaim_mode, __ATOMIC_RELAXED);
	uint32_t now = sweet_surface_uptime();
	if (mode <= SWEET_POOL_RECLAIM_OFF || sweet_pool_evict_lock == NULL || sweet_pool_state != 1 || sweet_surface_lock == NULL ||
	    now == __atomic_load_n(&sweet_pool_reclaim_at, __ATOMIC_RELAXED)) {
		return;
	}
	IOLockLock(sweet_pool_evict_lock);
	__atomic_store_n(&sweet_pool_reclaim_at, now, __ATOMIC_RELAXED);
	sweet_pool_reclaim_passes++;
	sweet_pool_reclaim_drain_locked(now);
	uint32_t n = 0, counts[SWEET_RECLAIM_NREASONS] = { 0 }, mib[SWEET_RECLAIM_NREASONS] = { 0 };
	IOLockLock(sweet_surface_lock);
	for (uint32_t i = 0; i < SWEET_POOL_REG_MAX && n < SWEET_POOL_REG_MAX; i++) {
		SweetSurface *t = (SweetSurface *)sweet_pool_reg.s[i];
		if (t == NULL || t->state == NULL || t->memory == NULL) {
			continue;
		}
		t->retain();
		cs[n] = t;
		sweet_reclaim_fill(t, &cand[n]);
		n++;
	}
	IOLockUnlock(sweet_surface_lock);
	for (uint32_t i = 0; i < n; i++) {
		cand[i].gpu_busy = nord2_gpu_surface_busy(cs[i]);
		verdict[i] = sweet_reclaim_verdict_now(&cand[i], now);
		counts[verdict[i]]++;
		mib[verdict[i]] += cs[i]->allocSize >> 20;
	}
	IOSimpleLockLock(sweet_pool_spin);
	sim = sweet_pool;
	IOSimpleLockUnlock(sweet_pool_spin);
	uint32_t pend_mib = 0;
	for (uint32_t i = 0; i < SWEET_RECLAIM_PEND_MAX; i++) {
		if (sweet_reclaim_pend[i].s != NULL) {
			sweet_reclaim_clear(&sim, (uint32_t)((sweet_reclaim_pend[i].pa - SWEET_FB_HW_POOL_BASE) / SWEET_FB_HW_POOL_GRANULE),
			    sweet_fb_pool_granules(sweet_reclaim_pend[i].len));
			pend_mib += (uint32_t)(sweet_reclaim_pend[i].len >> 20);
		}
	}
	bool fits_now = false;
	uint32_t k = sweet_reclaim_pick(&sim, cand, verdict, n, bytes, victims, 8, &fits_now);
	uint32_t told = sweet_pool_reclaim_told++;
	if (told < 16 || (told % 32) == 0) {
		char line[400];
		int off = 0;
		for (uint32_t r = 0; r < SWEET_RECLAIM_NREASONS && off < (int)sizeof(line) - 40; r++) {
			if (counts[r] != 0) {
				off += snprintf(line + off, sizeof(line) - (size_t)off, "%s%s %u (%u MiB)", off ? ", " : "", why_name[r], counts[r], mib[r]);
			}
		}
		IOLog("NORD2 FB POOL RECLAIM: pool full for %llu bytes (mode %d, pass %u): %u pool surfaces: %s; pending %u MiB; %s\n", bytes, mode,
		    sweet_pool_reclaim_passes, n, off ? line : "none", pend_mib,
		    fits_now ? "fits once the pending ranges are back" : k ? (mode >= SWEET_POOL_RECLAIM_EVICT ? "evicting" : "would evict (mode 1)") :
		    "no eviction makes it fit");
	}
	for (uint32_t i = 0; i < k; i++) {
		if (mode >= SWEET_POOL_RECLAIM_EVICT) {
			(void)sweet_pool_evict_one(cs[victims[i]], now);
		} else if (told < 16) {
			IOLog("NORD2 FB POOL RECLAIM:   would evict surface %u (%u MiB, last swap %s%u s ago)\n", cs[victims[i]]->id,
			    cs[victims[i]]->allocSize >> 20, cand[victims[i]].shown_at ? "" : "never; made ",
			    now - sweet_reclaim_last_use(&cand[victims[i]]));
		}
	}
	for (uint32_t i = 0; i < n; i++) {
		cs[i]->release();
	}
	IOLockUnlock(sweet_pool_evict_lock);
}

/*
 * ---------------------------------------------------------------- 0417 (memfix-20261113): purge, budgets, NORD2 MEM SURF
 */
extern "C" {
extern unsigned int memorystatus_available_pages, vm_page_wire_count;
}
#define SWEET_PURGE_SINK_MAX    (16u << 20)     /* surfaces up to this size can be purged (Photos' volatile ones: ~0.6 MiB average) */
#define SWEET_PURGE_PEND_MAX    512u
#define SWEET_PURGE_SCAN_MAX    SWEET_SURFACE_MAX_ID
#define SWEET_PURGE_PICK_MAX    256u            /* surfaces purged per pass at most */
#define SWEET_PURGE_HYST        (64ull << 20)
struct sweet_purge_pend_ent { SweetSurface *s; IOBufferMemoryDescriptor *old; uint32_t len, armed_at, shown_at; };
static struct sweet_purge_pend_ent sweet_purge_pend[SWEET_PURGE_PEND_MAX];     /* under sweet_pool_evict_lock */
static IOBufferMemoryDescriptor *sweet_purge_sink;                              /* set once in IOCoreSurfaceRoot::start */
static uint64_t      sweet_purge_sink_len;
static thread_call_t sweet_purge_tc, sweet_purge_kick_tc;
static thread_call_t sweet_purge_memgov_tc;                    /* 0420: kicked by NORD2 MEMGOV (kern_exec.c) */
static uint32_t      sweet_purge_memgov_pending;
extern "C" void sweet_surface_purge_kick_memgov(void);     /* 0420: called by kern_exec.c */
static uint32_t      sweet_purge_kick_pending;
static uint64_t      sweet_mem_create_pass_ns;
static uint32_t      sweet_mem_log_at, sweet_mem_told_refuse, sweet_mem_told_gpu, sweet_purge_told;
/* last scan (periodic line) */
static uint32_t      sweet_purge_st_vol_n, sweet_purge_st_idle_n, sweet_purge_st_bufs;
static uint64_t      sweet_purge_st_vol_b, sweet_purge_st_idle_b;
static uint32_t      sweet_purge_st_why[SWEET_PURGE_NREASONS];
SYSCTL_UINT(_kern, OID_AUTO, n2surf_purge_failed, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_purge_failed, 0, "0417: purges refused at the re-check or remap");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_unpurge_failed, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_unpurge_failed, 0, "0417: NonVolatile requests left on the sink (no memory)");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_purge_remap_bad, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_purge_remap_bad, 0, "0417: client mappings left on the old pages");

static uintptr_t
sweet_purge_sink_kva(void)
{
	return sweet_purge_sink != NULL ? (uintptr_t)sweet_purge_sink->getBytesNoCopy() : 0;
}

static uint64_t
sweet_mem_now_ns(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}

static void
sweet_mem_proc_charge(int32_t pid, int64_t delta)
{
	if (sweet_mem_spin == NULL || pid <= 0 || delta == 0) {
		return;
	}
	IOSimpleLockLock(sweet_mem_spin);
	sweet_mem_proc_add(sweet_mem_procs, pid, delta);
	IOSimpleLockUnlock(sweet_mem_spin);
}

static uint64_t
sweet_mem_proc_bytes(int32_t pid)
{
	uint64_t b = 0;
	if (sweet_mem_spin == NULL || pid <= 0) {
		return 0;
	}
	IOSimpleLockLock(sweet_mem_spin);
	int i = sweet_mem_proc_find(sweet_mem_procs, pid, false);
	b = i >= 0 ? sweet_mem_procs[i].bytes : 0;
	IOSimpleLockUnlock(sweet_mem_spin);
	return b;
}

/* The render server and the home screen never get a budget refusal (a failed CA surface there can take the UI down); they still purge. */
static bool
sweet_mem_exempt_self(void)
{
	char name[32];
	proc_selfname(name, sizeof(name));
	return strncmp(name, "backboardd", sizeof(name)) == 0 || strncmp(name, "SpringBoard", sizeof(name)) == 0;
}

/* Is s's IOVA programmed in an overlay slot now (purging it would pull the pages from under the scan)?  No lock held. */
static bool
sweet_purge_iommu_programmed(const SweetSurface *s)
{
	bool prog = false;
	if (__atomic_load_n(&n2io_state, __ATOMIC_RELAXED) != 1 || n2io_lock == NULL) {
		return false;
	}
	IOLockLock(n2io_lock);
	for (unsigned i = 0; i < SWEET_FB_IOMMU_CACHE; i++) {
		if (n2io_ent[i].obj == s && sweet_fb_iommu_ent_programmed(&n2io_ent[i], &n2io_prog)) {
			prog = true;
		}
	}
	IOLockUnlock(n2io_lock);
	return prog;
}

/* As sweet_pool_evict_remap (0406), with kIOMapUnique: a mapping of the shared sink is never reused for another surface. */
void
sweet_purge_remap(SweetSurface *s, IOMemoryDescriptor *old, IOMemoryDescriptor *buf, uint32_t *ok, uint32_t *bad)
{
	if (sweet_clients_lock == NULL) {
		return;
	}
	IOLockLock(sweet_clients_lock);
	for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
		IOSurfaceRootUserClient *c = sweet_clients[i];
		if (c == NULL || c->fLock == NULL || c->fRefs == NULL) {
			continue;
		}
		IOLockLock(c->fLock);
		for (unsigned int k = 0; k < c->fRefs->getCount(); k++) {
			SweetSurfaceRef *ref = (SweetSurfaceRef *)c->fRefs->getObject(k);
			IOMemoryMap *om = ref != NULL && ref->surface == s ? ref->data : NULL;
			if (om == NULL || om->getMemoryDescriptor() != old) {
				continue;
			}
			IOMemoryMap *nm = buf->createMappingInTask(om->getAddressTask(), om->getAddress(),
			    kIOMapOverwrite | kIOMapUnique | kIOMapDefaultCache, 0, om->getLength());
			if (nm == NULL || nm->getAddress() != om->getAddress()) {
				OSSafeReleaseNULL(nm);
				(*bad)++;
				continue;
			}
			nm->fOptions &= ~(IOOptionBits)kIOMapOverwrite;  /* the new map owns the range */
			om->fOptions |= kIOMapOverwrite;                  /* the old one must not unmap it */
			ref->data = nm;
			om->release();
			(*ok)++;
		}
		IOLockUnlock(c->fLock);
	}
	IOLockUnlock(sweet_clients_lock);
}

/* Old buffers whose grace is over are freed.  sweet_pool_evict_lock held. */
static void
sweet_purge_drain_locked(uint32_t now)
{
	if (__atomic_load_n(&sweet_purge_npend, __ATOMIC_RELAXED) == 0) {
		return;
	}
	for (uint32_t i = 0; i < SWEET_PURGE_PEND_MAX; i++) {
		struct sweet_purge_pend_ent *e = &sweet_purge_pend[i];
		if (e->s == NULL) {
			continue;
		}
		int v = sweet_reclaim_grace(now, e->armed_at, __atomic_load_n(&sweet_pool_reclaim_grace_s, __ATOMIC_RELAXED), e->shown_at,
		    e->s->shownAt, e->s->useKern[0], e->s->useKern[1], e->s->gpuImports, (uint32_t)e->old->getRetainCount());
		if (v == SWEET_RECLAIM_REARM) {
			e->armed_at = now;
			e->shown_at = e->s->shownAt;
		} else if (v == SWEET_RECLAIM_GIVE) {
			OSSafeReleaseNULL(e->old);
			OSAddAtomic64(-(SInt64)e->len, (volatile SInt64 *)&sweet_purge_pend_bytes);
			OSIncrementAtomic((volatile SInt32 *)&sweet_purge_given);
			OSSafeReleaseNULL(e->s);
			__atomic_sub_fetch(&sweet_purge_npend, 1, __ATOMIC_RELAXED);
		}
	}
}

/* Move s onto the sink (verdict re-checked under sweet_surface_lock).  sweet_pool_evict_lock held, nothing else. */
static bool
sweet_purge_one(SweetSurface *s, uint32_t now, bool busy, bool prog)
{
	uint32_t slot = SWEET_PURGE_PEND_MAX;
	for (uint32_t i = 0; i < SWEET_PURGE_PEND_MAX && slot == SWEET_PURGE_PEND_MAX; i++) {
		slot = sweet_purge_pend[i].s == NULL ? i : slot;
	}
	if (slot == SWEET_PURGE_PEND_MAX || sweet_purge_sink == NULL || s->state == NULL || s->allocSize > sweet_purge_sink_len) {
		return false;
	}
	uint32_t size = s->allocSize;
	IOSubMemoryDescriptor *view = IOSubMemoryDescriptor::withSubRange(sweet_purge_sink, 0, size, kIODirectionInOut);
	if (view == NULL) {
		return false;
	}
	IOLockLock(sweet_surface_lock);
	volatile uint32_t *w = sweet_use_words(s);
	IOBufferMemoryDescriptor *old = OSDynamicCast(IOBufferMemoryDescriptor, s->memory);
	struct sweet_purge_cand c = { size, s->bornAt, s->shownAt, s->purgeable, w[0], w[1], s->useKern[0], s->useKern[1], s->gpuImports,
		                      s->creator, old != NULL && !s->purged && sweet_pool_surface_pa(s) == 0, busy, prog };
	uint32_t v = sweet_purge_verdict(&c, now, __atomic_load_n(&sweet_purge_idle_s, __ATOMIC_RELAXED), sweet_use_mode_now() == SWEET_USE_MODE_ALL,
	    sweet_purge_sink_len);
	if (v != SWEET_PURGE_OK || old == NULL || old->getLength() < size) {
		IOLockUnlock(sweet_surface_lock);
		view->release();
		OSIncrementAtomic((volatile SInt32 *)&sweet_purge_failed);
		return false;
	}
	s->memory = view;                       /* s's reference moves from old to the view; old's goes to the pending record */
	s->mapCache = kIOMapDefaultCache;
	s->purgeable = SWEET_PURGEABLE_EMPTY;   /* the next IOSurfaceSetPurgeable answers Empty: the client redraws */
	s->purged = 1;
	IOLockUnlock(sweet_surface_lock);
	OSAddAtomic64(-(SInt64)size, (volatile SInt64 *)&sweet_surface_wired);
	OSDecrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
	sweet_mem_proc_charge(s->creator, -(int64_t)size);
	OSIncrementAtomic((volatile SInt32 *)&sweet_purged_live);
	OSAddAtomic64((SInt64)size, (volatile SInt64 *)&sweet_purged_bytes);
	OSAddAtomic64((SInt64)size, (volatile SInt64 *)&sweet_purge_pend_bytes);
	s->retain();
	struct sweet_purge_pend_ent e = { s, old, size, now, s->shownAt };
	sweet_purge_pend[slot] = e;
	__atomic_add_fetch(&sweet_purge_npend, 1, __ATOMIC_RELAXED);
	uint32_t ok = 0, bad = 0;
	sweet_purge_remap(s, old, view, &ok, &bad);
	sweet_fb_iommu_surface_gone(s);         /* a cached IOVA of the old pages goes (nothing programmed: checked) */
	OSIncrementAtomic((volatile SInt32 *)&sweet_purge_n);
	if (bad != 0) {
		OSAddAtomic((SInt32)bad, (volatile SInt32 *)&sweet_purge_remap_bad);
	}
	uint32_t k = sweet_purge_told++;
	if (k < 24 || bad != 0 || (k % 256) == 0) {
		char name[32] = "?";
		proc_name(s->creator, name, sizeof(name));
		IOLog("NORD2 MEM SURF: purged surface %u %ux%u fmt 0x%x %u KiB by %s[%d] (volatile, idle %u s): %u mapping(s) remapped%s%u (purged %u)\n",
		    s->id, s->width, s->height, s->pixelFormat, size >> 10, name, s->creator, now - (s->shownAt ? s->shownAt : s->bornAt), ok,
		    bad ? ", LEFT ON THE OLD PAGES (buffer kept): " : ", failed ", bad, sweet_purge_n);
	}
	return true;
}

/*
 * One scan of the id table: last-scan statistics, and (mode 2, goal > 0) purge of LRU idle volatile surfaces until goal bytes.
 * only_pid != 0: that process's surfaces only (its budget).  sweet_pool_evict_lock held, nothing else.  Returns the bytes purged.
 */
static uint64_t
sweet_purge_pass_locked(uint64_t goal, int32_t only_pid, const char *why)
{
	static SweetSurface *cs[SWEET_PURGE_SCAN_MAX];
	static struct sweet_purge_cand cand[SWEET_PURGE_SCAN_MAX];
	static uint32_t verdict[SWEET_PURGE_SCAN_MAX], pick[SWEET_PURGE_PICK_MAX];
	uint32_t now = sweet_surface_uptime(), n = 0, bufs = 0;
	uint32_t idle_s = __atomic_load_n(&sweet_purge_idle_s, __ATOMIC_RELAXED);
	bool all = sweet_use_mode_now() == SWEET_USE_MODE_ALL;
	sweet_purge_drain_locked(now);
	sweet_purge_passes++;
	IOLockLock(sweet_surface_lock);
	for (uint32_t id = 1; id < SWEET_SURFACE_MAX_ID && n < SWEET_PURGE_SCAN_MAX; id++) {
		SweetSurface *t = sweet_surface_table[id];
		if (t == NULL || t->state == NULL || t->memory == NULL || OSDynamicCast(IOBufferMemoryDescriptor, t->memory) == NULL || t->wod.on) {      /* 0480: pageable = the VM's */
			continue;
		}
		bufs++;
		if (only_pid != 0 && t->creator != only_pid) {
			continue;
		}
		volatile uint32_t *w = sweet_use_words(t);
		struct sweet_purge_cand c = { t->allocSize, t->bornAt, t->shownAt, t->purgeable, w[0], w[1], t->useKern[0], t->useKern[1], t->gpuImports,
			                      t->creator, !t->purged && sweet_pool_surface_pa(t) == 0, false, false };
		t->retain();
		cs[n] = t;
		cand[n++] = c;
	}
	IOLockUnlock(sweet_surface_lock);
	uint32_t why_n[SWEET_PURGE_NREASONS] = { 0 }, vol_n = 0, idle_n = 0;
	uint64_t vol_b = 0, idle_b = 0;
	for (uint32_t i = 0; i < n; i++) {
		verdict[i] = sweet_purge_verdict(&cand[i], now, idle_s, all, sweet_purge_sink_len);
		if (verdict[i] == SWEET_PURGE_OK) {     /* only now the questions that take other locks */
			cand[i].gpu_busy = nord2_gpu_surface_busy(cs[i]);
			cand[i].iommu_prog = !cand[i].gpu_busy && sweet_purge_iommu_programmed(cs[i]);
			verdict[i] = sweet_purge_verdict(&cand[i], now, idle_s, all, sweet_purge_sink_len);
		}
		why_n[verdict[i]]++;
		if (cand[i].purgeable == SWEET_PURGE_VOLATILE) {
			vol_n++;
			vol_b += cand[i].size;
		}
		if (verdict[i] == SWEET_PURGE_OK) {
			idle_n++;
			idle_b += cand[i].size;
		}
	}
	if (only_pid == 0) {
		sweet_purge_st_vol_n = vol_n; sweet_purge_st_vol_b = vol_b; sweet_purge_st_idle_n = idle_n; sweet_purge_st_idle_b = idle_b;
		sweet_purge_st_bufs = bufs;
		memcpy(sweet_purge_st_why, why_n, sizeof(why_n));
		__atomic_store_n(&sweet_mem_idle_bytes, idle_b, __ATOMIC_RELAXED);
	}
	uint64_t done = 0;
	int mode = __atomic_load_n(&sweet_purge_mode, __ATOMIC_RELAXED);
	if (goal != 0 && idle_n != 0 && mode >= SWEET_PURGE_REPORT) {
		uint64_t sel = 0;
		uint32_t k = sweet_purge_pick(cand, verdict, n, goal, pick, SWEET_PURGE_PICK_MAX, &sel);
		for (uint32_t i = 0; i < k && mode >= SWEET_PURGE_ON; i++) {
			if (sweet_purge_one(cs[pick[i]], now, cand[pick[i]].gpu_busy, cand[pick[i]].iommu_prog)) {
				done += cand[pick[i]].size;
			}
		}
		uint32_t t = sweet_purge_told;
		if (t < 64 || (sweet_purge_passes % 32) == 0 || mode < SWEET_PURGE_ON) {
			IOLog("NORD2 MEM SURF: pass %u (%s%s): goal %llu MiB, idle volatile %u (%llu MiB) of %u volatile (%llu MiB): %s %u (%llu MiB) -> %llu MiB\n",
			    sweet_purge_passes, why, only_pid ? ", one process" : "", goal >> 20, idle_n, idle_b >> 20, vol_n, vol_b >> 20,
			    mode >= SWEET_PURGE_ON ? "purging" : "would purge (mode 1)", k, sel >> 20, done >> 20);
		}
	}
	for (uint32_t i = 0; i < n; i++) {
		cs[i]->release();
	}
	return done;
}

/* 0418: the live policy (sysctl-tunable values); caps of the surfaces are applied through SWEET_SURFACE_WIRED_MAX */
static void
sweet_mem_policy_now(struct sweet_mem_policy *p)
{
	p->surf_cap = SWEET_SURFACE_WIRED_MAX;
	p->gpu_cap = (uint64_t)__atomic_load_n(&sweet_gpu_cap_mib, __ATOMIC_RELAXED) << 20;
	p->proc_cap = (uint64_t)__atomic_load_n(&sweet_surf_proc_mib, __ATOMIC_RELAXED) << 20;
	p->purge_thr = (uint64_t)__atomic_load_n(&sweet_purge_thr_mib, __ATOMIC_RELAXED) << 20;
	p->combined_purge = (uint64_t)__atomic_load_n(&sweet_mem_budget_mib, __ATOMIC_RELAXED) << 20;
	p->avail_high = __atomic_load_n(&sweet_purge_avail_min, __ATOMIC_RELAXED);
	p->avail_floor = __atomic_load_n(&sweet_mem_floor, __ATOMIC_RELAXED);
}

static uint64_t
sweet_mem_goal(uint64_t need)
{
	return sweet_purge_goal(__atomic_load_n(&sweet_surface_wired, __ATOMIC_RELAXED), __atomic_load_n(&sweet_mem_gpu_private, __ATOMIC_RELAXED),
	           memorystatus_available_pages, (uint32_t)page_size, (uint64_t)__atomic_load_n(&sweet_purge_thr_mib, __ATOMIC_RELAXED) << 20,
	           __atomic_load_n(&sweet_purge_avail_min, __ATOMIC_RELAXED), (uint64_t)__atomic_load_n(&sweet_mem_budget_mib, __ATOMIC_RELAXED) << 20,
	           SWEET_PURGE_HYST, need);
}

/*
 * NonVolatile asked for a purged surface: own wired memory again (zeroed), every client mapping back onto it.  The caller answers
 * Empty either way.  false = no memory: the surface stays on the sink (its pixels are then shared garbage).  sweet_pool_evict_lock held.
 */
static bool
sweet_purge_unpurge_locked(SweetSurface *s)
{
	if (!s->purged || sweet_purge_sink == NULL) {
		return true;
	}
	uint32_t size = s->allocSize;
	for (int attempt = 0; attempt < 2; attempt++) {
		bool fits = __atomic_load_n(&sweet_surface_wired, __ATOMIC_RELAXED) + size <= SWEET_SURFACE_WIRED_MAX &&
		    memorystatus_available_pages >= __atomic_load_n(&sweet_mem_floor, __ATOMIC_RELAXED);      /* 0418 */
		if (fits || attempt == 1) {
			break;
		}
		(void)sweet_purge_pass_locked(sweet_mem_goal(size), 0, "unpurge");
	}
	IOBufferMemoryDescriptor *buf = NULL;
	if ((uint64_t)OSAddAtomic64(size, (volatile SInt64 *)&sweet_surface_wired) + size <= SWEET_SURFACE_WIRED_MAX) {
		buf = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared, size, page_size);
	}
	if (buf == NULL || buf->getLength() < size) {
		OSSafeReleaseNULL(buf);
		OSAddAtomic64(-(SInt64)size, (volatile SInt64 *)&sweet_surface_wired);
		uint32_t k = OSIncrementAtomic((volatile SInt32 *)&sweet_unpurge_failed);
		if (k < 16 || (k % 64) == 0) {
			IOLog("NORD2 MEM SURF: NonVolatile for purged surface %u (%u KiB): no memory (wired %llu MiB): left on the sink (unpurge failures %u)\n",
			    s->id, size >> 10, sweet_surface_wired >> 20, k + 1);
		}
		return false;
	}
	bzero(buf->getBytesNoCopy(), size);
	IOLockLock(sweet_surface_lock);
	IOMemoryDescriptor *view = s->memory;
	s->memory = buf;
	s->mapCache = kIOMapDefaultCache;
	s->purged = 0;
	IOLockUnlock(sweet_surface_lock);
	OSIncrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
	sweet_mem_proc_charge(s->creator, (int64_t)size);
	OSDecrementAtomic((volatile SInt32 *)&sweet_purged_live);
	OSAddAtomic64(-(SInt64)size, (volatile SInt64 *)&sweet_purged_bytes);
	uint32_t ok = 0, bad = 0;
	sweet_purge_remap(s, sweet_purge_sink, buf, &ok, &bad);   /* the maps of a view belong to the sink (IOSubMemoryDescriptor::makeMapping) */
	sweet_fb_iommu_surface_gone(s);         /* a cached IOVA of the sink goes */
	OSSafeReleaseNULL(view);                /* nothing but maps holds a view; the GPU / IOMMU used the sink's pages, which stay */
	OSIncrementAtomic((volatile SInt32 *)&sweet_unpurge_n);
	if (bad != 0) {
		OSAddAtomic((SInt32)bad, (volatile SInt32 *)&sweet_purge_remap_bad);
	}
	if (sweet_unpurge_n <= 24 || bad != 0 || (sweet_unpurge_n % 256) == 0) {
		IOLog("NORD2 MEM SURF: surface %u NonVolatile again: own %u KiB, %u mapping(s) remapped%s%u, answered Empty (unpurged %u)\n", s->id,
		    size >> 10, ok, bad ? ", LEFT ON THE SINK: " : ", failed ", bad, sweet_unpurge_n);
	}
	return true;
}

/*
 * Creation of a wired buffer surface of size by pid (thread context, no lock held): purge under pressure first, then the per-process and
 * the combined budget.  false = refuse (kIOReturnNoMemory, as the 2048 MiB budget).
 */
static bool
sweet_mem_admit(uint32_t size, int32_t pid)
{
	if (sweet_pool_evict_lock == NULL) {
		return true;
	}
	struct sweet_mem_policy pol;
	sweet_mem_policy_now(&pol);
	uint64_t goal = sweet_mem_goal(size), mine = sweet_mem_proc_bytes(pid);
	bool proc_over = !sweet_mem_proc_ok(mine, size, pol.proc_cap);
	uint64_t now = sweet_mem_now_ns(), last = __atomic_load_n(&sweet_mem_create_pass_ns, __ATOMIC_RELAXED);
	/* 0418: pressure purges (rate-limited to 5/s); a runaway process or memory under the floor purges at once */
	bool urgent = proc_over || memorystatus_available_pages < pol.avail_floor;
	if ((goal != 0 || proc_over) && __atomic_load_n(&sweet_purge_mode, __ATOMIC_RELAXED) >= SWEET_PURGE_ON && (urgent || now - last >= 200000000ull)) {
		__atomic_store_n(&sweet_mem_create_pass_ns, now, __ATOMIC_RELAXED);
		IOLockLock(sweet_pool_evict_lock);
		if (proc_over) {
			(void)sweet_purge_pass_locked(mine + size - pol.proc_cap + SWEET_PURGE_HYST, pid, "process guard");
		}
		if (goal != 0) {
			(void)sweet_purge_pass_locked(goal, 0, "create");
		}
		IOLockUnlock(sweet_pool_evict_lock);
		mine = sweet_mem_proc_bytes(pid);
	}
	uint32_t avail = memorystatus_available_pages;
	if (sweet_mem_surface_rule(&pol, mine, size, avail)) {
		return true;
	}
	proc_over = !sweet_mem_proc_ok(mine, size, pol.proc_cap);
	if (sweet_mem_exempt_self()) {
		OSIncrementAtomic((volatile SInt32 *)&sweet_mem_exempted);
		return true;
	}
	OSIncrementAtomic((volatile SInt32 *)(proc_over ? &sweet_mem_refused_proc : &sweet_mem_refused_combined));
	uint32_t k = sweet_mem_told_refuse++;
	if (k < 16 || (k % 128) == 0) {
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("NORD2 MEM SURF: %s[%d] %u KiB refused: %s (process %llu of %llu MiB, available %u pages, floor %u; surfaces %llu + GPU private %llu MiB) "
		    "(refused: process %u, low memory %u)\n", name, pid, size >> 10, proc_over ? "runaway process guard" : "memory under the floor after purging",
		    mine >> 20, pol.proc_cap >> 20, avail, pol.avail_floor, sweet_surface_wired >> 20, sweet_mem_gpu_private >> 20, sweet_mem_refused_proc,
		    sweet_mem_refused_combined);
	}
	return false;
}

/*
 * GPU side (nord2_gpu_transport.h nord2_gpu_global_private_ok, under the GPU locks: never purges inline).  0418: refused only over the cap
 * (max of Nord2GPU's limit and 40 % of RAM) or with available pages under the floor; pressure only kicks the purge thread.
 */
extern "C" bool
sweet_mem_gpu_private_ok3(uint64_t total_private, uint64_t rounded, uint64_t limit)
{
	__atomic_store_n(&sweet_mem_gpu_private, total_private, __ATOMIC_RELAXED);
	struct sweet_mem_policy pol;
	sweet_mem_policy_now(&pol);
	uint32_t avail = memorystatus_available_pages;
	int d = sweet_mem_gpu_rule(&pol, total_private, rounded, limit, avail, sweet_mem_goal(rounded) != 0);
	if (d != 1 && sweet_purge_kick_tc != NULL && __atomic_exchange_n(&sweet_purge_kick_pending, 1u, __ATOMIC_ACQ_REL) == 0) {
		OSIncrementAtomic((volatile SInt32 *)&sweet_mem_gpu_kicks);
		thread_call_enter(sweet_purge_kick_tc);
	}
	if (d != 0) {
		return true;
	}
	bool over_cap = !(total_private <= (pol.gpu_cap > limit ? pol.gpu_cap : limit) &&
	    rounded <= (pol.gpu_cap > limit ? pol.gpu_cap : limit) - total_private);
	if (!over_cap && sweet_mem_exempt_self()) {
		OSIncrementAtomic((volatile SInt32 *)&sweet_mem_exempted);
		return true;
	}
	uint32_t k = OSIncrementAtomic((volatile SInt32 *)&sweet_mem_refused_gpu);
	if (sweet_mem_told_gpu++ < 16 || (k % 128) == 0) {
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("NORD2 MEM SURF: GPU allocation of %llu KiB by %s[%d] refused: %s (GPU private %llu MiB, cap %llu MiB, available %u pages, floor %u) "
		    "(GPU refusals %u)\n", rounded >> 10, name, proc_selfpid(), over_cap ? "over the GPU cap" : "memory under the floor",
		    total_private >> 20, (pol.gpu_cap > limit ? pol.gpu_cap : limit) >> 20, avail, pol.avail_floor, k + 1);
	}
	return false;
}

static void
sweet_mem_log_line(void)
{
	char top[48] = "-";
	int32_t tp = 0;
	uint64_t tb = 0;
	if (sweet_mem_spin != NULL) {
		IOSimpleLockLock(sweet_mem_spin);
		for (unsigned i = 0; i < SWEET_MEM_PROCS; i++) {
			if (sweet_mem_procs[i].pid > 0 && sweet_mem_procs[i].bytes > tb) {
				tb = sweet_mem_procs[i].bytes;
				tp = sweet_mem_procs[i].pid;
			}
		}
		IOSimpleLockUnlock(sweet_mem_spin);
	}
	if (tp > 0) {
		char name[32] = "?";
		proc_name(tp, name, sizeof(name));
		snprintf(top, sizeof(top), "%s[%d] %llu MiB", name, tp, tb >> 20);
	}
	uint64_t gp = __atomic_load_n(&sweet_mem_gpu_private, __ATOMIC_RELAXED), sw = __atomic_load_n(&sweet_surface_wired, __ATOMIC_RELAXED);
	IOLog("NORD2 MEM SURF: wired=%llu MiB bufs=%u (table %u) volatile=%u/%llu MiB idle=%u/%llu MiB iommu=%u MiB purged=%u/%llu MiB pend=%u/%llu MiB "
	    "gpu_private=%llu MiB combined=%llu/%u MiB avail=%u wired_pages=%u purges=%u unpurges=%u fail=%u/%u remap_bad=%u refused=proc %u comb %u gpu %u "
	    "exempt %u kicks %u top=%s mode=%d floor=%u caps=surf %u gpu %u proc %u MiB\n", sw >> 20, sweet_surface_buffers, sweet_purge_st_bufs, sweet_purge_st_vol_n, sweet_purge_st_vol_b >> 20, sweet_purge_st_idle_n,
	    sweet_purge_st_idle_b >> 20, (n2io_pt.mapped * 4096u) >> 20, sweet_purged_live, sweet_purged_bytes >> 20, sweet_purge_npend, sweet_purge_pend_bytes >> 20,
	    gp >> 20, (sw + gp) >> 20, sweet_mem_budget_mib, memorystatus_available_pages, vm_page_wire_count, sweet_purge_n, sweet_unpurge_n, sweet_purge_failed,
	    sweet_unpurge_failed, sweet_purge_remap_bad, sweet_mem_refused_proc, sweet_mem_refused_combined, sweet_mem_refused_gpu, sweet_mem_exempted,
	    sweet_mem_gpu_kicks, top, sweet_purge_mode, sweet_mem_floor, sweet_surf_cap_mib, sweet_gpu_cap_mib, sweet_surf_proc_mib);
	if (sweet_purge_st_vol_n != 0) {
		uint32_t *y = sweet_purge_st_why;
		IOLog("NORD2 MEM SURF: volatile kept because: young %u active %u in-use %u display %u gpu-job %u gpu-import %u gpu-busy %u iommu %u size %u mode %u\n",
		    y[SWEET_PURGE_NO_YOUNG], y[SWEET_PURGE_NO_ACTIVE], y[SWEET_PURGE_NO_USE], y[SWEET_PURGE_NO_DISPLAY], y[SWEET_PURGE_NO_GPU_JOB],
		    y[SWEET_PURGE_NO_GPU_IMPORT], y[SWEET_PURGE_NO_GPU_BUSY], y[SWEET_PURGE_NO_IOMMU], y[SWEET_PURGE_NO_SIZE], y[SWEET_PURGE_NO_MODE]);
	}
}

/* Every 2 s (thread call): drain, scan, purge under pressure; the line every 10 s. */
static void
sweet_purge_tick(thread_call_param_t p0, thread_call_param_t)
{
	bool periodic = p0 == NULL;
	bool memgov = p0 == (thread_call_param_t)2;    /* 0420 */
	if (memgov) {
		__atomic_store_n(&sweet_purge_memgov_pending, 0u, __ATOMIC_RELEASE);
	} else if (!periodic) {
		__atomic_store_n(&sweet_purge_kick_pending, 0u, __ATOMIC_RELEASE);
	}
	if (sweet_pool_evict_lock != NULL && sweet_surface_lock != NULL) {
		uint64_t goal = sweet_mem_goal(0);
		IOLockLock(sweet_pool_evict_lock);
		(void)sweet_purge_pass_locked(goal, 0, periodic ? "pressure" : memgov ? "memgov" : "gpu budget");
		IOLockUnlock(sweet_pool_evict_lock);
	}
	if (!periodic) {
		return;
	}
	uint32_t now = sweet_surface_uptime();
	if (now - sweet_mem_log_at >= 10) {
		sweet_mem_log_at = now;
		sweet_mem_log_line();
	}
	uint64_t dl = 0;
	clock_interval_to_deadline(2, kSecondScale, &dl);
	thread_call_enter_delayed(sweet_purge_tc, dl);
}

/* 0420: NORD2 MEMGOV (kern_exec.c) under its low mark, at most once a second: one purge pass now (kern.n2surf_purge still decides). */
extern "C" void
sweet_surface_purge_kick_memgov(void)
{
	if (sweet_purge_memgov_tc != NULL && __atomic_exchange_n(&sweet_purge_memgov_pending, 1u, __ATOMIC_ACQ_REL) == 0) {
		thread_call_enter(sweet_purge_memgov_tc);
	}
}

static void
sweet_purge_start(void)
{
	uint32_t v = 0;
	{   /* 0418: every limit from the RAM window */
		struct sweet_mem_policy pol;
		sweet_mem_policy_for(max_mem, (uint32_t)page_size, &pol);
		sweet_surf_cap_mib = (uint32_t)(pol.surf_cap >> 20);
		sweet_gpu_cap_mib = (uint32_t)(pol.gpu_cap >> 20);
		sweet_surf_proc_mib = (uint32_t)(pol.proc_cap >> 20);
		sweet_purge_thr_mib = (uint32_t)(pol.purge_thr >> 20);
		sweet_mem_budget_mib = (uint32_t)(pol.combined_purge >> 20);
		sweet_purge_avail_min = pol.avail_high;
		sweet_mem_floor = pol.avail_floor;
		IOLog("NORD2 MEM SURF: 0418 policy for %llu MiB RAM: caps surfaces %u GPU %u process %u MiB; purge when surfaces > %u MiB, surfaces + GPU > %u MiB "
		    "or available < %u pages; refuse only under %u available pages\n", max_mem >> 20, sweet_surf_cap_mib, sweet_gpu_cap_mib, sweet_surf_proc_mib,
		    sweet_purge_thr_mib, sweet_mem_budget_mib, sweet_purge_avail_min, sweet_mem_floor);
	}
	if (PE_parse_boot_argn("n2sp", &v, sizeof(v)) && v <= SWEET_PURGE_ON) {
		sweet_purge_mode = (int)v;
	}
	if (PE_parse_boot_argn("n2mb", &v, sizeof(v))) {
		sweet_mem_budget_mib = v;
	}
	for (uint64_t len = SWEET_PURGE_SINK_MAX; sweet_purge_sink == NULL && len >= (4u << 20); len >>= 1) {
		IOBufferMemoryDescriptor *b = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared,
		    (vm_size_t)len, page_size);
		if (b != NULL && b->getLength() >= len) {
			bzero(b->getBytesNoCopy(), (size_t)len);
			sweet_purge_sink_len = len;
			sweet_purge_sink = b;
		} else {
			OSSafeReleaseNULL(b);
		}
	}
	sweet_purge_tc = thread_call_allocate(sweet_purge_tick, NULL);
	sweet_purge_kick_tc = thread_call_allocate(sweet_purge_tick, (thread_call_param_t)1);
	sweet_purge_memgov_tc = thread_call_allocate(sweet_purge_tick, (thread_call_param_t)2);
	IOLog("NORD2 MEM SURF: purge mode %d (kern.n2surf_purge / n2sp), sink %llu MiB, pressure: surfaces > %u MiB or available < %u pages, idle %u s; "
	    "budgets: surfaces + GPU private %u MiB (kern.n2mem_budget_mib / n2mb), per process %u MiB, surfaces %llu MiB\n", sweet_purge_mode,
	    sweet_purge_sink_len >> 20, sweet_purge_thr_mib, sweet_purge_avail_min, sweet_purge_idle_s, sweet_mem_budget_mib, sweet_surf_proc_mib,
	    SWEET_SURFACE_WIRED_MAX >> 20);
	if (sweet_purge_tc != NULL) {
		uint64_t dl = 0;
		clock_interval_to_deadline(10, kSecondScale, &dl);
		thread_call_enter_delayed(sweet_purge_tc, dl);
	}
}

static void
sweet_clients_init_lock(void)
{
	if (sweet_clients_lock == NULL) {
		IOLock *lock = IOLockAlloc();
		if (!OSCompareAndSwapPtr(NULL, lock, &sweet_clients_lock)) {
			IOLockFree(lock);
		}
	}
}

struct sweet_report_who { int32_t pid; uint32_t count, vol_count, dead_count; uint64_t bytes, vol_bytes, dead_bytes; };
struct sweet_report_size { uint32_t width, height, format, count, oldest; uint64_t bytes; };
#define SWEET_REPORT_WHO 32
#define SWEET_REPORT_SIZES 24

static void
sweet_report_sort(struct sweet_report_who *who, uint32_t used, uint32_t first)
{
	for (uint32_t n = 0; n < first && n < used; n++) {
		uint32_t best = n;
		for (uint32_t k = n + 1; k < used; k++) {
			if (who[k].bytes > who[best].bytes) {
				best = k;
			}
		}
		struct sweet_report_who tmp = who[n]; who[n] = who[best]; who[best] = tmp;
	}
}

static bool
sweet_report_dead(int32_t pid)
{
	proc_t p = pid > 0 ? proc_find(pid) : NULL;
	if (p != NULL) {
		proc_rele(p);
		return false;
	}
	return pid > 0;
}

/* At most every five seconds. Only reads; a client that is closing is kept out by sweet_clients_lock (free()) and its fLock. */
static void
sweet_surface_report(const char *why, uint32_t asked)
{
	static struct sweet_report_who made[SWEET_REPORT_WHO], held[SWEET_REPORT_WHO];     /* under sweet_clients_lock */
	static struct sweet_report_size sizes[3][SWEET_REPORT_SIZES];
	static bool dead[SWEET_REPORT_WHO];
	uint32_t made_n = 0, held_n = 0, in_table = 0, vol_count = 0, now = sweet_surface_uptime();
	uint64_t table_bytes = 0, vol_bytes = 0;
	uint64_t refused = OSAddAtomic64(1, (volatile SInt64 *)&sweet_surface_refused) + 1;
	sweet_clients_init_lock();
	IOLockLock(sweet_clients_lock);
	if (sweet_surface_report_ns != 0 && now - (uint32_t)sweet_surface_report_ns < 5) {
		IOLockUnlock(sweet_clients_lock);
		return;
	}
	sweet_surface_report_ns = now ? now : 1;
	/* creators */
	IOLockLock(sweet_surface_lock);
	for (uint32_t id = 1; id < SWEET_SURFACE_MAX_ID; id++) {
		SweetSurface *t = sweet_surface_table[id];
		if (t == NULL || OSDynamicCast(IOBufferMemoryDescriptor, t->memory) == NULL) {
			continue;
		}
		in_table++;
		table_bytes += t->allocSize;
		if (t->purgeable == 1) {
			vol_count++; vol_bytes += t->allocSize;
		}
		uint32_t k = 0;
		while (k < made_n && made[k].pid != sweet_surface_owner[id]) {
			k++;
		}
		if (k == made_n && made_n < SWEET_REPORT_WHO) {
			bzero(&made[made_n], sizeof(made[0]));
			made[made_n++].pid = sweet_surface_owner[id];
		}
		if (k < made_n) {
			made[k].count++; made[k].bytes += t->allocSize;
			if (t->purgeable == 1) {
				made[k].vol_count++; made[k].vol_bytes += t->allocSize;
			}
		}
	}
	sweet_report_sort(made, made_n, 5);
	/* the sizes of the three biggest creators */
	bzero(sizes, sizeof(sizes));
	for (uint32_t id = 1; id < SWEET_SURFACE_MAX_ID; id++) {
		SweetSurface *t = sweet_surface_table[id];
		if (t == NULL || OSDynamicCast(IOBufferMemoryDescriptor, t->memory) == NULL) {
			continue;
		}
		for (uint32_t n = 0; n < 3 && n < made_n; n++) {
			if (made[n].pid != sweet_surface_owner[id]) {
				continue;
			}
			uint32_t age = now - sweet_surface_born[id];
			for (uint32_t k = 0; k < SWEET_REPORT_SIZES; k++) {
				struct sweet_report_size *z = &sizes[n][k];
				if (z->count == 0 || (z->width == t->width && z->height == t->height && z->format == t->pixelFormat)) {
					z->width = t->width; z->height = t->height; z->format = t->pixelFormat;
					z->count++; z->bytes += t->allocSize;
					if (age > z->oldest) {
						z->oldest = age;
					}
					break;
				}
			}
		}
	}
	IOLockUnlock(sweet_surface_lock);
	for (uint32_t k = 0; k < made_n; k++) {
		dead[k] = k < 5 ? sweet_report_dead(made[k].pid) : false;
	}
	/* holders */
	for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
		IOSurfaceRootUserClient *c = sweet_clients[i];
		if (c == NULL || c->fLock == NULL || c->fRefs == NULL) {
			continue;
		}
		uint32_t k = 0;
		while (k < held_n && held[k].pid != c->fPid) {
			k++;
		}
		if (k == held_n) {
			if (held_n == SWEET_REPORT_WHO) {
				continue;
			}
			bzero(&held[held_n], sizeof(held[0]));
			held[held_n++].pid = c->fPid;
		}
		IOLockLock(c->fLock);
		for (unsigned int r = 0; r < c->fRefs->getCount(); r++) {
			SweetSurface *t = ((SweetSurfaceRef *)c->fRefs->getObject(r))->surface;
			if (t == NULL || t->id >= SWEET_SURFACE_MAX_ID || OSDynamicCast(IOBufferMemoryDescriptor, t->memory) == NULL) {
				continue;
			}
			held[k].count++; held[k].bytes += t->allocSize;
			if (t->purgeable == 1) {
				held[k].vol_count++; held[k].vol_bytes += t->allocSize;
			}
			int32_t owner = sweet_surface_owner[t->id];
			for (uint32_t m = 0; m < 5 && m < made_n; m++) {
				if (made[m].pid == owner && dead[m] && owner != c->fPid) {
					held[k].dead_count++; held[k].dead_bytes += t->allocSize;
				}
			}
		}
		IOLockUnlock(c->fLock);
	}
	sweet_report_sort(held, held_n, 6);
	IOLog("SWEET SURF: refused (%s, %u bytes asked, refusal %llu): %llu of %llu MiB wired in %u buffer surfaces (%u made, %u released); table has %u, %llu MiB; volatile %u, %llu MiB\n",
	    why, asked, refused, sweet_surface_wired >> 20, SWEET_SURFACE_WIRED_MAX >> 20, sweet_surface_buffers,
	    sweet_surface_created, sweet_surface_created - sweet_surface_buffers, in_table, table_bytes >> 20, vol_count, vol_bytes >> 20);
	IOLog("NORD2 FB POOL: (not in the wired budget) live %u, used %u MiB, full %u, foreign %u, leaked %u; creations: eligible %u (full -> normal: see full), eligible-but-off %u, "
	    "format %u, planes %u, small %u, stride %u, offset %u, size %u\n", sweet_pool_live, sweet_pool_used_kib >> 10, sweet_pool_full,
	    sweet_pool_foreign, sweet_pool_leaked, sweet_pool_why[SWEET_FB_POOL_OK], sweet_pool_why[SWEET_FB_POOL_NO_OFF],
	    sweet_pool_why[SWEET_FB_POOL_NO_FORMAT], sweet_pool_why[SWEET_FB_POOL_NO_PLANES], sweet_pool_why[SWEET_FB_POOL_NO_SMALL],
	    sweet_pool_why[SWEET_FB_POOL_NO_STRIDE], sweet_pool_why[SWEET_FB_POOL_NO_OFFSET], sweet_pool_why[SWEET_FB_POOL_NO_SIZE]);   /* 0394 */
	sweet_heap_log_line(true);      /* 0440: not in the wired budget either */
	for (uint32_t n = 0; n < 5 && n < made_n; n++) {
		char name[32] = "?";
		proc_name(made[n].pid, name, sizeof(name));
		IOLog("SWEET SURF:   made by %s[%d]%s: %u, %llu MiB (volatile %u, %llu MiB)\n", name, made[n].pid, dead[n] ? " (gone)" : "",
		    made[n].count, made[n].bytes >> 20, made[n].vol_count, made[n].vol_bytes >> 20);
		if (n >= 3) {
			continue;
		}
		for (uint32_t pass = 0; pass < 5; pass++) {      /* its five sizes with the most bytes */
			struct sweet_report_size *best = NULL;
			for (uint32_t k = 0; k < SWEET_REPORT_SIZES; k++) {
				if (sizes[n][k].count != 0 && (best == NULL || sizes[n][k].bytes > best->bytes)) {
					best = &sizes[n][k];
				}
			}
			if (best == NULL) {
				break;
			}
			IOLog("SWEET SURF:     %ux%u fmt 0x%x: %u, %llu MiB, oldest %u s\n", best->width, best->height, best->format,
			    best->count, best->bytes >> 20, best->oldest);
			best->count = 0;
		}
	}
	for (uint32_t n = 0; n < 6 && n < held_n; n++) {
		char name[32] = "?";
		proc_name(held[n].pid, name, sizeof(name));
		IOLog("SWEET SURF:   held by %s[%d]: %u, %llu MiB (volatile %u, %llu MiB; made by a process that is gone %u, %llu MiB)\n", name, held[n].pid,
		    held[n].count, held[n].bytes >> 20, held[n].vol_count, held[n].vol_bytes >> 20, held[n].dead_count, held[n].dead_bytes >> 20);
	}
	IOLockUnlock(sweet_clients_lock);
}

static uint64_t sweet_surf_logged;      /* selectors / traps already logged (< 64) */

static void
sweet_surf_log_once(const char *what, uint32_t n)
{
	uint32_t bit = (what[0] == 't' ? 48 : 0) + n;
	if (bit >= 64 || (sweet_surf_logged & (1ULL << bit))) {
		return;
	}
	sweet_surf_logged |= 1ULL << bit;
	char name[32];
	proc_selfname(name, sizeof(name));
	IOLog("SWEET SURF: %s %s %u\n", name, what, n);
}

bool
IOSurfaceRootUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fLock = IOLockAlloc();
	fRefs = OSArray::withCapacity(16);
	if (fLock == NULL || fRefs == NULL) {
		return false;
	}
	fPid = proc_selfpid();
	sweet_clients_init_lock();
	IOLockLock(sweet_clients_lock);
	for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
		if (sweet_clients[i] == NULL) {
			sweet_clients[i] = this;
			break;
		}
	}
	IOLockUnlock(sweet_clients_lock);
	return true;
}

void
IOSurfaceRootUserClient::free()
{
	if (sweet_clients_lock != NULL) {
		/* out of the list before anything of it goes away; the report holds this lock while it reads fRefs */
		IOLockLock(sweet_clients_lock);
		for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
			if (sweet_clients[i] == this) {
				sweet_clients[i] = NULL;
			}
		}
		IOLockUnlock(sweet_clients_lock);
	}
	OSSafeReleaseNULL(fRefs);
	if (fLock != NULL) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	IOUserClient::free();
}

IOReturn
IOSurfaceRootUserClient::clientClose(void)
{
	IOLockLock(fLock);
	fRefs->flushCollection();
	IOLockUnlock(fLock);
	terminate();
	return kIOReturnSuccess;
}

/* Lock held. */
SweetSurfaceRef *
IOSurfaceRootUserClient::findRef(uint32_t id, unsigned int *index)
{
	for (unsigned int i = 0; i < fRefs->getCount(); i++) {
		SweetSurfaceRef *ref = (SweetSurfaceRef *)fRefs->getObject(i);
		if (ref->surface->id == id) {
			if (index != NULL) {
				*index = i;
			}
			return ref;
		}
	}
	return NULL;
}

/*
 * Count one more create/lookup of s by this client, mapping it into the
 * task the first time. Takes over the caller's user count on s.
 *
 * A lookup of a surface this client already has maps nothing and counts
 * nothing (*existing): IOSurface.framework then gets base address -1 in the
 * lock result and reuses its cached IOSurfaceClient. Counting it instead
 * made the framework build a second client for the same id; releasing that
 * one (traps 4 + 5) unmapped the memory under the first (backboardd
 * snapshot crash in CGDataProviderCreateWithData).
 */
IOReturn
IOSurfaceRootUserClient::addRef(SweetSurface *s, SweetSurfaceRef **refOut, bool *existing)
{
	*existing = false;
	IOLockLock(fLock);
	SweetSurfaceRef *ref = findRef(s->id, NULL);
	if (ref != NULL && ref->surface == s) {
		IOLockUnlock(fLock);
		sweet_surface_put(s);
		*refOut = ref;
		*existing = true;
		return kIOReturnSuccess;
	}
	IOLockUnlock(fLock);

	ref = OSTypeAlloc(SweetSurfaceRef);
	if (ref == NULL || !ref->init()) {
		OSSafeReleaseNULL(ref);
		sweet_surface_put(s);
		return kIOReturnNoMemory;
	}
	s->retain();
	ref->surface = s;
	ref->count = 1;
	ref->pid = fPid;        /* 0398 */
	/*
	 * 0406: s->memory can be swapped by an eviction (under sweet_surface_lock): map a retained snapshot and map again if it changed
	 * meanwhile.  A mapping of the old range that still slips through only keeps that range out of the pool (the old descriptor's
	 * retain count gates its return), it never aliases another surface.
	 */
	for (uint32_t tries = 0; ref->data == NULL && tries < 3; tries++) {
		IOLockLock(sweet_surface_lock);
		IOMemoryDescriptor *mem = s->memory;
		IOOptionBits cache = s->mapCache;
		if (mem != NULL) {
			mem->retain();
		}
		IOLockUnlock(sweet_surface_lock);
		if (mem == NULL) {
			break;
		}
		/* 0417: a purged surface's view maps the shared sink: kIOMapUnique, never another surface's compatible mapping of it */
		ref->data = mem->createMappingInTask(fTask, 0, kIOMapAnywhere | cache |
		    (OSDynamicCast(IOSubMemoryDescriptor, mem) != NULL ? (IOOptionBits)kIOMapUnique : 0));
		IOLockLock(sweet_surface_lock);
		bool moved = s->memory != mem;
		IOLockUnlock(sweet_surface_lock);
		mem->release();
		if (moved && tries < 2) {
			OSSafeReleaseNULL(ref->data);
		}
	}
	ref->state = s->state->createMappingInTask(fTask, 0, kIOMapAnywhere | kIOMapReadOnly);
	if (ref->data == NULL || ref->state == NULL) {
		ref->release();
		return kIOReturnVMError;
	}
	IOLockLock(fLock);
	SweetSurfaceRef *raced = findRef(s->id, NULL);
	if (raced != NULL && raced->surface == s) {
		/* Another thread mapped it meanwhile: count there, drop ours. */
		raced->count++;
		IOLockUnlock(fLock);
		ref->release();
		*refOut = raced;
		return kIOReturnSuccess;
	}
	fRefs->setObject(ref);
	IOLockUnlock(fLock);
	ref->release();         /* fRefs holds it */
	*refOut = ref;
	return kIOReturnSuccess;
}

static void
put32(uint8_t *p, uint32_t off, uint32_t v)
{
	memcpy(p + off, &v, 4);
}

static void
put64(uint8_t *p, uint32_t off, uint64_t v)
{
	memcpy(p + off, &v, 8);
}

/* IOSurface.framework's lock result (copied to IOSurfaceClient + 0x68). */
static void
sweet_surface_fill_result(SweetSurface *s, SweetSurfaceRef *ref, uint8_t *out)
{
	bzero(out, SWEET_SURFACE_RESULT_SIZE);
	put64(out, 0x00, ref->data->getAddress());
	put64(out, 0x08, ref->state->getAddress());
	put64(out, 0x10, ref->state->getAddress() + SWEET_SURFACE_TIMESTAMPS);
	put32(out, 0x18, s->id);
	put32(out, 0x1c, s->allocSize);
	put32(out, 0x20, s->width);
	put32(out, 0x24, s->height);
	put32(out, 0x28, s->bytesPerRow);
	put32(out, 0x2c, s->offset);
	put32(out, 0x30, s->pixelFormat);
	put32(out, 0x34, s->allocSize);
	put32(out, 0x38, s->planeCount);
	memcpy(out + 0x40, &s->bytesPerElement, 2);
	out[0x42] = s->elementWidth;
	out[0x43] = s->elementHeight;
	put32(out, 0x44, s->cacheMode);
	put32(out, 0x50, s->parentID);
	put32(out, 0x54, 1);
	for (uint32_t i = 0; i < s->planeCount; i++) {
		const struct sweet_surface_plane *pl = &s->planes[i];
		struct sweet_plane_abi abi = { pl->width, pl->height, pl->bytesPerRow, pl->offset, pl->size,
			                       pl->bytesPerElement, pl->elementWidth, pl->elementHeight };
		sweet_plane_record(out + 0x68 + i * SWEET_PLANE_RECORD_BYTES, &abi);   /* bytesPerRow at +0x10 */
	}
}

static IOReturn
sweet_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		if (len > args->structureOutputDescriptor->getLength()) {
			return kIOReturnNoSpace;
		}
		args->structureOutputDescriptor->writeBytes(0, buf, len);
		args->structureOutputDescriptorSize = len;
		return kIOReturnSuccess;
	}
	if (args->structureOutput == NULL || len > args->structureOutputSize) {
		return kIOReturnNoSpace;
	}
	memcpy(args->structureOutput, buf, len);
	args->structureOutputSize = len;
	return kIOReturnSuccess;
}

/* Map s for this client and answer with its lock result. Takes s's user count. */
IOReturn
IOSurfaceRootUserClient::returnSurface(SweetSurface *s, IOExternalMethodArguments *args)
{
	SweetSurfaceRef *ref = NULL;
	bool existing = false;
	IOReturn ret = addRef(s, &ref, &existing);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	uint8_t *out = (uint8_t *)IOMallocZeroData(SWEET_SURFACE_RESULT_SIZE);
	if (out == NULL) {
		ret = kIOReturnNoMemory;
	} else {
		sweet_surface_fill_result(s, ref, out);
		if (existing) {
			put64(out, 0x00, ~0ULL);        /* "already yours": use the cached client */
		}
		ret = sweet_struct_out(args, out, SWEET_SURFACE_RESULT_SIZE);
		IOFreeData(out, SWEET_SURFACE_RESULT_SIZE);
	}
	if (ret != kIOReturnSuccess && !existing) {
		/* The caller never learns the id: undo this create/lookup. */
		trapRelease((void *)(uintptr_t)s->id, NULL, NULL, NULL, NULL, NULL);
	}
	return ret;
}

IOReturn
IOSurfaceRootUserClient::create(IOExternalMethodArguments *args)
{
	const char *buf = (const char *)args->structureInput;
	uint32_t len = args->structureInputSize;
	char *copy = NULL;
	IOReturn ret;

	if (args->structureInputDescriptor != NULL) {
		len = (uint32_t)args->structureInputDescriptor->getLength();
		if (len == 0 || len > SWEET_SURFACE_MAX_PROPS) {
			return kIOReturnBadArgument;
		}
		copy = (char *)IOMallocData(len);
		if (copy == NULL) {
			return kIOReturnNoMemory;
		}
		args->structureInputDescriptor->readBytes(0, copy, len);
		buf = copy;
	}
	OSObject *obj = (buf != NULL && len > 0) ? OSUnserializeXML(buf, len) : NULL;
	if (copy != NULL) {
		IOFreeData(copy, len);
	}
	OSDictionary *props = OSDynamicCast(OSDictionary, obj);
	if (props == NULL) {
		OSSafeReleaseNULL(obj);
		IOLog("SWEET SURF: create: bad property dictionary (%u bytes)\n", len);
		return kIOReturnBadArgument;
	}

	SweetSurface *s = sweet_surface_alloc();
	if (s == NULL) {
		props->release();
		return kIOReturnNoMemory;
	}
	s->parentID = args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : 0;
	ret = sweet_surface_layout(s, props);
	bool pooled = false, heaped = false;
	if (ret == kIOReturnSuccess) {
		/* 0394: full-screen BGRA surfaces from the display pool when kern.n2fbhw_pool = 1; anything else, or a full pool, below. */
		uint32_t why = sweet_fb_pool_eligible(1, s->width, s->height, s->bytesPerRow, s->bytesPerElement,
		    s->elementWidth, s->elementHeight, s->pixelFormat, s->planeCount, s->offset, s->allocSize);
		uint32_t elig = why;            /* 0440: the heap takes the same surfaces (and the same admission) when the pool does not */
		if (why == SWEET_FB_POOL_OK && !sweet_pool_enabled) {
			why = SWEET_FB_POOL_NO_OFF;     /* "off" = would have gone to the pool (H0 data with the pool off) */
		}
		OSIncrementAtomic((volatile SInt32 *)&sweet_pool_why[why]);
		bool admit = true;
		if (elig == SWEET_FB_POOL_OK) { /* 0398: who makes it decides too (snapshots are never scanned out) */
			char pname[32];
			proc_selfname(pname, sizeof(pname));
			admit = sweet_pool_admit(__atomic_load_n(&sweet_pool_policy, __ATOMIC_RELAXED), pname);
			if (!admit) {
				OSIncrementAtomic((volatile SInt32 *)&sweet_pool_denied);
				if (sweet_pool_told_denied++ < 8) {
					IOLog("NORD2 FB POOL: %s[%d] %ux%u kept out of the pool by kern.n2fbhw_pool_policy %d (denied %u)\n", pname, fPid,
					    s->width, s->height, sweet_pool_policy, sweet_pool_denied);
				}
			}
		}
		s->creator = fPid;
		pooled = why == SWEET_FB_POOL_OK && admit && sweet_pool_attach(s, true);
		heaped = !pooled && elig == SWEET_FB_POOL_OK && admit && sweet_heap_attach(s);     /* 0440: else normal memory below */
	}
	if (pooled || heaped) {
		OSNumber *client = OSDynamicCast(OSNumber, props->getObject("IOSurfaceAddress"));
		if (client == NULL) { client = OSDynamicCast(OSNumber, props->getObject("IOSurfaceClientAddress")); }
		if (client != NULL) {   /* snapshot of the creator's memory, as below (no planes here) */
			uint64_t address = client->unsigned64BitValue();
			uint64_t used = sweet_client_range_bytes(address, 0, (uint64_t)s->bytesPerRow * s->height, 0, s->allocSize);
			uintptr_t kva = s->kernelAddress();
			int err = used == 0 || kva == 0 ? EINVAL : copyin((user_addr_t)address, (void *)kva, (size_t)used);
			if (err == 0) {
				if (heaped) {
					sweet_heap_wc_drain();      /* 0440: write-combined */
				} else {
					sweet_pool_flush((vm_offset_t)kva, (size_t)used);
				}
			}
			IOLog("%s: create from client memory %#llx (%llu bytes) -> %s\n", heaped ? "NORD2 HEAP" : "NORD2 FB POOL", address, used,
			    err ? "refused" : "snapshot copied");
			ret = err ? kIOReturnBadArgument : kIOReturnSuccess;        /* refused: s->release() below gives the range back */
		}
		if (ret == kIOReturnSuccess && !sweet_surface_publish(s)) {
			ret = kIOReturnNoResources;
		}
		if (ret == kIOReturnSuccess && heaped) {
			char hname[32];
			proc_selfname(hname, sizeof(hname));
			sweet_heap_log_alloc(s, hname);
		} else if (ret == kIOReturnSuccess) {
			sweet_pool_log_alloc(s, "create");
		}
	} else if (ret == kIOReturnSuccess && sweet_wod_create(s, props, fPid, &ret)) {
		/* 0480: pageable + purgeable, wired on demand (or refused by the 0417 guards) */
	} else if (ret == kIOReturnSuccess) {
		IOBufferMemoryDescriptor *mem = NULL;
		bool admitted = sweet_mem_admit(s->allocSize, fPid);      /* 0417: purge under pressure, per-process and combined budgets */
		bool in_budget = (uint64_t)OSAddAtomic64(s->allocSize, (volatile SInt64 *)&sweet_surface_wired) +
		    s->allocSize <= SWEET_SURFACE_WIRED_MAX && admitted;
		if (in_budget) {
			mem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
			    kIODirectionInOut | kIOMemoryKernelUserShared, s->allocSize, page_size);
		}
		if (mem == NULL) {
			OSAddAtomic64(-(SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
			ret = kIOReturnNoMemory;
			sweet_surface_report(in_budget ? "the kernel gave no memory" : (admitted ? "over the budget" : "over the 0417 budgets"), s->allocSize);   /* 0299 */
		} else {
			OSIncrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
			OSIncrementAtomic((volatile SInt32 *)&sweet_surface_created);
			bzero(mem->getBytesNoCopy(), s->allocSize);
			/*
			 * 0300: a two-plane 4:2:0 surface that nobody has written yet must not read as green (Y 0, Cb Cr 0): the chroma
			 * plane starts at mid level, the luma of the video-range formats at their black. '420f' '420v' 8 bit, 'xf20' 'x420'
			 * 10 bit in 16 (level << 6); the packed/compressed kinds have other layouts and stay zero.
			 */
			if (s->planeCount == 2 && (s->pixelFormat == 0x34323066 || s->pixelFormat == 0x34323076 ||
			    s->pixelFormat == 0x78663230 || s->pixelFormat == 0x78343230)) {
				bool wide = (s->pixelFormat >> 24) == 0x78, video = (s->pixelFormat & 0xff) == 0x76 || s->pixelFormat == 0x78343230;
				uint8_t *base = (uint8_t *)mem->getBytesNoCopy();
				for (uint32_t i = 0; i < 2; i++) {
					uint64_t off = s->planes[i].offset, len = s->planes[i].size;
					if (off > s->allocSize || len > s->allocSize - off || (i == 0 && !video)) {
						continue;
					}
					if (!wide) {
						memset(base + off, i == 0 ? 16 : 0x80, (size_t)len);
					} else {
						uint16_t v = i == 0 ? 0x1000 : 0x8000;
						for (uint64_t k = 0; k + 2 <= len; k += 2) {
							memcpy(base + off + k, &v, 2);
						}
					}
				}
			}
			/* IOSurfaceAddress / IOSurfaceClientAddress: memory the creator already filled.  Keep a snapshot (see
			 * sweet_surface_abi.h) instead of zeros; an unusable range fails the creation so the caller falls back. */
			OSNumber *client = OSDynamicCast(OSNumber, props->getObject("IOSurfaceAddress"));
			if (client == NULL) { client = OSDynamicCast(OSNumber, props->getObject("IOSurfaceClientAddress")); }
			if (client != NULL) {
				uint64_t rows = (uint64_t)s->bytesPerRow * sweet_div_up(s->height, s->elementHeight), plane_end = 0;
				for (uint32_t i = 0; i < s->planeCount; i++) {
					if ((uint64_t)s->planes[i].offset + s->planes[i].size > plane_end) {
						plane_end = (uint64_t)s->planes[i].offset + s->planes[i].size;
					}
				}
				uint64_t address = client->unsigned64BitValue();
				uint64_t used = sweet_client_range_bytes(address, s->offset, rows, plane_end, s->allocSize);
				int err = used == 0 ? EINVAL : copyin((user_addr_t)address, mem->getBytesNoCopy(), (size_t)used);
				static uint32_t told;
				if (told++ < 6) {
					char name[32];
					proc_selfname(name, sizeof(name));
					IOLog("SWEET SURF: %s create from client memory %#llx (%llu of %u bytes) %ux%u fmt 0x%x -> %s\n",
					    name, address, used, s->allocSize, s->width, s->height, s->pixelFormat, err ? "refused" : "snapshot copied");
				}
				if (err) {
					OSAddAtomic64(-(SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
					OSDecrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
					mem->release();
					mem = NULL;
					ret = kIOReturnBadArgument;
				}
			}
			if (mem != NULL) {
			s->memory = mem;
			s->mapCache = kIOMapDefaultCache;
			sweet_mem_proc_charge(fPid, (int64_t)s->allocSize);    /* 0417 (free() gives it back by s->creator == fPid) */
			if (!sweet_surface_publish(s)) {
				ret = kIOReturnNoResources;
			}
			}
		}
	}
	if (ret != kIOReturnSuccess || sweet_surface_creates_logged < 64) {
		char name[32];
		proc_selfname(name, sizeof(name));
		sweet_surface_creates_logged++;
		IOLog("SWEET SURF: %s create id %u %ux%u fmt 0x%x bpe %u bpr %u planes %u size %u keys %u -> 0x%x\n",
		    name, s->id, s->width, s->height, s->pixelFormat, s->bytesPerElement,
		    s->bytesPerRow, s->planeCount, s->allocSize, props->getCount(), ret);
	}
	props->release();
	if (ret != kIOReturnSuccess) {
		s->release();
		return ret;
	}
	ret = returnSurface(s, args);   /* takes the creator's user count */
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	uint64_t arg0 = args->scalarInputCount > 0 ? args->scalarInput[0] : 0;

	switch (selector) {
	case kSweetSurfaceLimits: {
		uint8_t limits[0x18] = { 0 };
		put32(limits, 0x0, SWEET_SURFACE_ROW_ALIGN - 1);
		put32(limits, 0x4, SWEET_SURFACE_ROW_ALIGN - 1);
		limits[0x16] = 1;
		sweet_surf_log_once("sel", selector);
		return sweet_struct_out(args, limits, sizeof(limits));
	}
	case kSweetSurfaceCreate:
		return create(args);
	case kSweetSurfaceLookup: {
		SweetSurface *s = sweet_surface_copy((uint32_t)arg0);
		IOReturn ret = kIOReturnNotFound;
		if (s != NULL) {
			ret = sweet_surface_get(s) ? returnSurface(s, args) : kIOReturnNotFound;
			s->release();
		}
		sweet_surf_log_once("sel", selector);
		return ret;
	}
	case kSweetSurfaceLookupFromPort: {
		OSObject *obj = NULL;
		IOReturn ret = IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj);
		SweetSurface *s = OSDynamicCast(SweetSurface, obj);
		if (ret == kIOReturnSuccess) {
			ret = (s != NULL && sweet_surface_get(s)) ? returnSurface(s, args) : kIOReturnBadArgument;
		}
		OSSafeReleaseNULL(obj);
		sweet_surf_log_once("sel", selector);
		return ret;
	}
	case kSweetSurfaceCreatePort: {
		SweetSurface *s = sweet_surface_copy((uint32_t)arg0);
		if (s == NULL || args->scalarOutputCount < 1) {
			OSSafeReleaseNULL(s);
			return kIOReturnNotFound;
		}
		args->scalarOutput[0] = sweet_surface_port_in_task(s, fTask);
		args->scalarOutputCount = 1;
		s->release();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	default:
		break;
	}
	/* Everything that is not implemented above has a deliberate answer (sweet_surface_abi.h). */
	sweet_surf_log_once("sel", selector);
	switch (sweet_selector_policy(selector)) {
	case SWEET_POLICY_UNSUPPORTED:
		/* IOSurfaceWrapClientImage/Memory: failing makes CoreAnimation upload the pixels itself. */
		return kIOReturnUnsupported;
	case SWEET_POLICY_NOT_FOUND:
		return kIOReturnNotFound;
	case SWEET_POLICY_PURGEABLE: {
		SweetSurface *s = args->scalarInputCount >= 2 ? sweet_surface_copy((uint32_t)arg0) : NULL;
		if (s == NULL) {
			return args->scalarInputCount >= 2 ? kIOReturnNotFound : kIOReturnBadArgument;
		}
		if (sweet_pool_evict_lock != NULL) {
			IOLockLock(sweet_pool_evict_lock);     /* 0406: not in the middle of an eviction of s */
		}
		uint32_t req = (uint32_t)args->scalarInput[1];
		IOLockLock(sweet_surface_lock);
		bool was_purged = s->purged != 0;
		uint32_t old = sweet_purgeable_update(&s->purgeable, req);
		IOLockUnlock(sweet_surface_lock);
		if (was_purged) {
			/* 0417: the contents are gone whatever was asked in between: Empty; NonVolatile gets own memory again (evict lock held) */
			old = SWEET_PURGEABLE_EMPTY;
			if (req == SWEET_PURGEABLE_NONVOLATILE && sweet_pool_evict_lock != NULL) {
				(void)sweet_purge_unpurge_locked(s);
			}
		}
		if (sweet_pool_evict_lock != NULL) {
			IOLockUnlock(sweet_pool_evict_lock);
		}
		old = sweet_wod_purgeable(s, req, old);  /* 0480: the VM object follows; Empty if the VM purged it */
		if (args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = old;
			args->scalarOutputCount = 1;
		}
		s->release();
		return kIOReturnSuccess;
	}
	case SWEET_POLICY_UNKNOWN: {
		/* Unidentified selector (seen: 40, 41, 43): tolerated as before; log its shape once so it can be named. */
		static uint64_t shape_logged;
		if (selector < 64 && !(shape_logged & (1ULL << selector))) {
			shape_logged |= 1ULL << selector;
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("SWEET SURF: %s unidentified selector %u: scalars in %u out %u, struct in %u out %u, arg0 %llx -> accepted, zeroed output\n",
			    name, selector, args->scalarInputCount, args->scalarOutputCount, (uint32_t)args->structureInputSize,
			    (uint32_t)args->structureOutputSize, (unsigned long long)arg0);
		}
		break;
	}
	default:                /* SWEET_POLICY_ACCEPT: known, no effect */
		break;
	}
	for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
		args->scalarOutput[i] = 0;
	}
	if (args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	return kIOReturnSuccess;
}

/*
 * 0398: "NORD2 SURF USE:" report.  Counters, then the surfaces in use now (table scan): how many, by holder, and (dump) up to 10 of
 * them with their counts.  Leak rule: user holds return to 0 when the apps that took them are gone (a closed app's holds are dropped
 * at its mappings' release and show up as "dropped"); display holds stay <= the layers of the swaps in flight (<= 16); GPU holds are
 * 0 whenever the GPU is idle; free-nonzero and unmatched stay 0.  Called from trap context only (no lock held).
 */
struct sweet_use_row { uint32_t id, width, height, format, size, age, w0, w1, kd, kg; int32_t owner; };

static void
sweet_use_report(const char *why, bool dump)
{
	static struct sweet_use_row rows[10];     /* under sweet_use_reporting */
	uint32_t in_use = 0, by_user = 0, by_display = 0, by_gpu = 0, only_kernel = 0, nrows = 0, now = sweet_surface_uptime();
	uint64_t bytes = 0;
	IOLockLock(sweet_surface_lock);
	for (uint32_t id = 1; id < SWEET_SURFACE_MAX_ID; id++) {
		SweetSurface *t = sweet_surface_table[id];
		if (t == NULL || t->state == NULL) {
			continue;
		}
		volatile uint32_t *w = sweet_use_words(t);
		uint32_t w0 = sweet_use_load(&w[0]), w1 = sweet_use_load(&w[1]), kd = t->useKern[0], kg = t->useKern[1];
		if (w0 == 0 && w1 == 0) {
			continue;
		}
		in_use++;
		bytes += t->allocSize;
		uint32_t user = w0 + w1 - (kd + kg <= w0 ? kd + kg : w0);
		by_user += user != 0;
		by_display += kd != 0;
		by_gpu += kg != 0;
		only_kernel += user == 0;
		if (dump && nrows < 10) {
			struct sweet_use_row r = { id, t->width, t->height, t->pixelFormat, t->allocSize, now - sweet_surface_born[id], w0, w1, kd, kg,
				                   sweet_surface_owner[id] };
			rows[nrows++] = r;
		}
	}
	IOLockUnlock(sweet_surface_lock);
	struct sweet_use_stats *st = &sweet_use_st;
	IOLog("NORD2 SURF USE: %s t=%us mode %d: incs %llu decs %llu (user %llu/%llu, display %llu/%llu, gpu %llu/%llu), live %lld "
	    "(display %lld, gpu %lld) max-live %lld max-count %u; imbalance: unmatched-dec %llu dup-inc %llu kernel-unmatched %llu/%llu "
	    "not-found %llu; dropped-at-release %llu (%llu mappings), free-nonzero %llu, saturated %llu, refused-off %llu\n",
	    why, now, sweet_use_mode_now(), st->user_inc + st->kern_inc[0] + st->kern_inc[1], st->user_dec + st->kern_dec[0] + st->kern_dec[1],
	    st->user_inc, st->user_dec, st->kern_inc[0], st->kern_dec[0], st->kern_inc[1], st->kern_dec[1], st->live, st->live_kind[0],
	    st->live_kind[1], st->max_live, st->max_word, st->user_dec_unmatched, st->user_inc_dup, st->kern_unmatched[0],
	    st->kern_unmatched[1], st->not_found, st->dropped, st->dropped_maps, st->free_nonzero, st->saturated, st->refused_off);
	IOLog("NORD2 SURF USE: %s in use now: %u surfaces (%llu MiB): held by processes %u, by the display %u, by the GPU %u, only by the kernel %u%s\n",
	    why, in_use, bytes >> 20, by_user, by_display, by_gpu, only_kernel,
	    (st->free_nonzero || st->kern_unmatched[0] || st->kern_unmatched[1] || st->live_kind[0] > 16) ? " -- LEAK SUSPECT" : "");
	for (uint32_t i = 0; i < nrows; i++) {
		char name[32] = "?";
		proc_name(rows[i].owner, name, sizeof(name));
		IOLog("NORD2 SURF USE:   surface %u %ux%u fmt 0x%x %u KiB made by %s[%d] %u s ago: counts %u/%u (display %u, gpu %u)\n", rows[i].id,
		    rows[i].width, rows[i].height, rows[i].format, rows[i].size >> 10, name, rows[i].owner, rows[i].age, rows[i].w0, rows[i].w1,
		    rows[i].kd, rows[i].kg);
	}
}

/*
 * 0398: "NORD2 FB POOL: leak candidates" -- every live pool surface (registry), classified (sweet_pool_classify), with who still holds it:
 * mappings (client connections: creator and lookups), whether it is still in the id table (out of it = only kernel references left:
 * framebuffer swap sets, GPU import buffers, GPU job holds), the descriptor's retain count, use counts.  Expected after closing apps:
 * pool used MiB back to backboardd's own framebuffers (3-4 x 10.7 MiB) plus the drawables of the app on screen.
 */
struct sweet_pool_row { uint32_t id, age, shown_ago, shown, users, retains, w0, w1, kd, kg, imports, purgeable, verdict; int32_t creator; int cls; bool table; };

static void
sweet_pool_leak_report(const char *why, bool dump)
{
	static struct sweet_pool_row rows[16];    /* under sweet_use_reporting */
	static const char *cls_name[SWEET_POOL_CLASSES] = { "young", "active", "stale", "NEVER shown" };
	uint32_t n = 0, cls[SWEET_POOL_CLASSES] = { 0 }, cls_mib[SWEET_POOL_CLASSES] = { 0 }, kernel_only = 0, gpu_held = 0, now = sweet_surface_uptime();
	uint32_t limit = __atomic_load_n(&sweet_pool_age, __ATOMIC_RELAXED);
	IOLockLock(sweet_surface_lock);
	for (uint32_t i = 0; i < SWEET_POOL_REG_MAX; i++) {
		SweetSurface *t = (SweetSurface *)sweet_pool_reg.s[i];
		if (t == NULL || t->state == NULL) {
			continue;
		}
		int c = sweet_pool_classify(now, t->bornAt, t->shownAt, limit ? limit : 1);
		bool table = t->id < SWEET_SURFACE_MAX_ID && sweet_surface_table[t->id] == t;
		cls[c]++;
		cls_mib[c] += t->allocSize >> 20;
		kernel_only += !table;
		gpu_held += t->gpuImports != 0;
		if ((dump || c >= SWEET_POOL_STALE || !table) && n < 16) {
			volatile uint32_t *w = sweet_use_words(t);
			struct sweet_reclaim_cand rc;   /* 0406: the eviction verdict (GPU busy not asked here) */
			sweet_reclaim_fill(t, &rc);
			struct sweet_pool_row r = { t->id, now - t->bornAt, t->shownAt ? now - t->shownAt : 0, t->shownCount,
				                    table ? sweet_surface_users[t->id] : 0, t->memory ? (uint32_t)t->memory->getRetainCount() : 0,
				                    w[0], w[1], t->useKern[0], t->useKern[1], t->gpuImports, t->purgeable,
				                    sweet_reclaim_verdict_now(&rc, now), t->creator, c, table };
			rows[n++] = r;
		}
	}
	uint32_t reg_n = sweet_pool_reg.n, reg_over = sweet_pool_reg.overflow;
	IOLockUnlock(sweet_surface_lock);
	IOLog("NORD2 FB POOL: leak candidates (%s, age limit %u s): live %u (registry %u%s), used %u of %llu MiB, full %u, denied by policy %d: %u; "
	    "young %u (%u MiB), active %u (%u MiB), stale %u (%u MiB), never shown %u (%u MiB); only kernel holders %u, with GPU imports %u\n",
	    why, limit, sweet_pool_live, reg_n, reg_over ? " OVERFLOW" : "", sweet_pool_used_kib >> 10, SWEET_FB_HW_POOL_SIZE >> 20, sweet_pool_full,
	    sweet_pool_policy, sweet_pool_denied, cls[0], cls_mib[0], cls[1], cls_mib[1], cls[2], cls_mib[2], cls[3], cls_mib[3], kernel_only, gpu_held);
	for (uint32_t i = 0; i < n; i++) {
		char name[32] = "kernel";
		if (rows[i].creator > 0) {
			proc_name(rows[i].creator, name, sizeof(name));
		}
		char shown[24];
		if (rows[i].shown) {
			snprintf(shown, sizeof(shown), "%u s ago", rows[i].shown_ago);
		} else {
			snprintf(shown, sizeof(shown), "never");
		}
		IOLog("NORD2 FB POOL:   id %u by %s[%d] %s: age %u s, last swap %s (%u swaps); holders: mappings %u%s, descriptor retains %u, "
		    "use %u/%u, display %u, gpu jobs %u, gpu imports %u; purgeable %u, reclaim verdict %u (0 = evictable)\n", rows[i].id, name,
		    rows[i].creator, cls_name[rows[i].cls], rows[i].age,
		    shown, rows[i].shown, rows[i].users, rows[i].table ? "" : " (OUT OF THE ID TABLE: kernel references only)", rows[i].retains,
		    rows[i].w0, rows[i].w1, rows[i].kd, rows[i].kg, rows[i].imports, rows[i].purgeable, rows[i].verdict);
	}
}

/* At boot+30 s, every 60 s while traps come, and when kern.n2surfuse_report is set (then with the surface list). */
static void
sweet_use_maybe_report(void)
{
	uint32_t now = sweet_surface_uptime(), at = __atomic_load_n(&sweet_use_report_at, __ATOMIC_RELAXED);
	int req = __atomic_load_n(&sweet_use_report_req, __ATOMIC_RELAXED);
	const char *why = NULL;
	if (req != 0) {
		why = "asked";
	} else if (at == 0 && now >= 30) {
		why = "boot+30s";
	} else if (at != 0 && now - at >= 60) {
		why = "periodic";
	}
	/* the pool report also when the pool refused a creation since the last one (at most every 15 s) */
	bool pool_full = sweet_pool_state == 1 && sweet_pool_full != sweet_pool_report_full && now - sweet_pool_report_at >= 15;
	if (__atomic_load_n(&sweet_pool_reclaim_npend, __ATOMIC_RELAXED) != 0) {
		sweet_pool_reclaim_drain();     /* 0406: evicted ranges come back even when nothing is being created (trap context, no lock) */
	}
	if ((why == NULL && !pool_full) || !OSCompareAndSwap(0, 1, &sweet_use_reporting)) {
		return;
	}
	if (why != NULL) {
		__atomic_store_n(&sweet_use_report_req, 0, __ATOMIC_RELAXED);
		__atomic_store_n(&sweet_use_report_at, now ? now : 1, __ATOMIC_RELAXED);
		sweet_use_report(why, true);    /* the in-use list every time (at most 10 lines a minute): leak checks need it */
		sweet_use_reports++;
	}
	if (sweet_pool_state == 1) {
		sweet_pool_report_full = sweet_pool_full;
		sweet_pool_report_at = now;
		sweet_pool_leak_report(why != NULL ? why : "pool full", req != 0);
	}
	__atomic_store_n(&sweet_use_reporting, 0, __ATOMIC_RELEASE);
}

/* Trap 0 / 1 (IOSurface.framework calls them on this process's 0 -> 1 / 1 -> 0 step of a category: sweet_surface_use.h). */
IOReturn
IOSurfaceRootUserClient::trapUseCount(void *id, void *category, bool inc)
{
	uint32_t sid = (uint32_t)(uintptr_t)id, cat = (uint32_t)(uintptr_t)category;
	sweet_use_args();
	int mode = sweet_use_mode_now();
	if (mode == SWEET_USE_MODE_OFF && inc) {
		__atomic_add_fetch(&sweet_use_st.refused_off, 1, __ATOMIC_RELAXED);
		return kIOReturnSuccess;        /* the old no-op */
	}
	if (cat >= SWEET_USE_CATEGORIES) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fLock);
	SweetSurfaceRef *ref = findRef(sid, NULL);
	if (ref == NULL || ref->surface->state == NULL) {
		IOLockUnlock(fLock);
		__atomic_add_fetch(&sweet_use_st.not_found, 1, __ATOMIC_RELAXED);
		if (sweet_use_told_found++ < 8) {
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("NORD2 SURF USE: %s[%d] %s use count of surface %u (category %u) it has no mapping of -> not found\n", name, fPid,
			    inc ? "increments" : "decrements", sid, cat);
		}
		return kIOReturnNotFound;
	}
	uint8_t before = ref->useHeld;
	uint64_t dup = sweet_use_st.user_inc_dup, unm = sweet_use_st.user_dec_unmatched;
	(void)sweet_use_user(&ref->useHeld, sweet_use_words(ref->surface), cat, inc, mode, &sweet_use_st);
	IOLockUnlock(fLock);
	if (sweet_use_st.user_inc_dup != dup && sweet_use_told_dup++ < 8) {
		IOLog("NORD2 SURF USE: pid %d increments surface %u category %u twice (held %#x): kept one hold\n", fPid, sid, cat, before);
	}
	if (sweet_use_st.user_dec_unmatched != unm && mode != SWEET_USE_MODE_OFF && sweet_use_told_unmatched++ < 8) {
		IOLog("NORD2 SURF USE: pid %d decrements surface %u category %u it does not hold (held %#x): ignored\n", fPid, sid, cat, before);
	}
	sweet_use_maybe_report();
	return kIOReturnSuccess;
}

IOReturn
IOSurfaceRootUserClient::trapIncrementUse(void *id, void *category, void *, void *, void *, void *)
{
	return trapUseCount(id, category, true);
}

IOReturn
IOSurfaceRootUserClient::trapDecrementUse(void *id, void *category, void *, void *, void *, void *)
{
	return trapUseCount(id, category, false);
}

static IOReturn
sweet_surface_seed_out(SweetSurface *s, void *seedOut, bool bump)
{
	uint32_t seed = bump ? OSIncrementAtomic((volatile SInt32 *)sweet_surface_seed(s)) + 1 :
	    *sweet_surface_seed(s);
	if (seedOut != NULL && copyout(&seed, (user_addr_t)seedOut, sizeof(seed)) != 0) {
		return kIOReturnBadArgument;
	}
	return kIOReturnSuccess;
}

/* Retained surface this client created or looked up, or NULL. */
static SweetSurface *
sweet_surface_held(IOLock *lock, OSArray *refs, uint32_t id)
{
	SweetSurface *s = NULL;
	IOLockLock(lock);
	for (unsigned int i = 0; i < refs->getCount(); i++) {
		SweetSurfaceRef *ref = (SweetSurfaceRef *)refs->getObject(i);
		if (ref->surface->id == id) {
			s = ref->surface;
			s->retain();
			break;
		}
	}
	IOLockUnlock(lock);
	return s;
}

extern "C" __attribute__((weak)) void nord2_gpu_surface_cpu_acquire(void *) {}
extern "C" __attribute__((weak)) void nord2_gpu_surface_cpu_release(void *) {}
extern "C" __attribute__((weak)) bool nord2_gpu_surface_busy(void *) { return false; }
extern "C" __attribute__((weak)) uint64_t nord2_gpu_resv_expired(void) { return 0; }

IOReturn
IOSurfaceRootUserClient::trapLock(void *id, __unused void *options, void *seedOut,
    void *, void *, void *)
{
	SweetSurface *s = sweet_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = sweet_surface_seed_out(s, seedOut, false);
	nord2_gpu_surface_cpu_acquire(s);       /* GPU-written lines of this surface must not satisfy the CPU's reads */
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapUnlock(void *id, void *options, void *seedOut,
    void *, void *, void *)
{
	SweetSurface *s = sweet_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = sweet_surface_seed_out(s, seedOut,
	    !((uintptr_t)options & kIOSurfaceLockReadOnly));
	if (!((uintptr_t)options & kIOSurfaceLockReadOnly)) {
		nord2_gpu_surface_cpu_release(s);       /* the CPU wrote it: clean before the GPU's next read */
		sweet_fb_iommu_cpu_wrote(s);            /* 0401 IOMMU: and before the next overlay scan through the IOMMU */
	}
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapRelease(void *id, void *, void *, void *, void *, void *)
{
	unsigned int index;
	IOLockLock(fLock);
	SweetSurfaceRef *ref = findRef((uint32_t)(uintptr_t)id, &index);
	if (ref != NULL && --ref->count == 0) {
		fRefs->removeObject(index);     /* unmaps and drops the user count */
	}
	IOLockUnlock(fLock);
	return ref != NULL ? kIOReturnSuccess : kIOReturnNotFound;
}

static IOExternalTrap sweet_surface_traps[] = {
	[kSweetSurfaceTrapIncrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapIncrementUse },     /* 0398: were one no-op */
	[kSweetSurfaceTrapDecrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapDecrementUse },
	[kSweetSurfaceTrapLock]          = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapLock },
	[kSweetSurfaceTrapUnlock]        = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUnlock },
	[kSweetSurfaceTrapRelease]       = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
	[kSweetSurfaceTrapReleaseFailed] = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
};

IOExternalTrap *
IOSurfaceRootUserClient::getTargetAndTrapForIndex(IOService **targetP, UInt32 index)
{
	if (index >= sizeof(sweet_surface_traps) / sizeof(sweet_surface_traps[0])) {
		sweet_surf_log_once("trap", index);
		return NULL;
	}
	sweet_surf_log_once("trap", index);
	*targetP = this;
	return &sweet_surface_traps[index];
}

/*
 * 0640: debug snapshot (SweetFramebuffer.cpp snapHw, autotest thread only, never the display path).  Copies n bytes at byte offset
 * off of the surface's memory into dst, through a kernel mapping the driver ALREADY has (buffer, pool, heap region, purge sink, an
 * existing kernelMap) -- never a new mapping (a new write-combined alias of cacheable user pages would be a mismatched alias) --
 * else through IOMemoryDescriptor::readBytes under a non-blocking wire hold.  A cacheable buffer range is cleaned+invalidated to
 * PoC first, so the bytes are the ones in DRAM, i.e. what the overlay reads.  0 = done; 1 no memory, 2 out of range, 3 not wired now,
 * 4 read failed.  The caller holds a retain on the surface.
 */
extern "C" int sweet_surface_snap_read(void *surface, uint64_t off, void *dst, uint32_t n);
extern "C" int
sweet_surface_snap_read(void *surface, uint64_t off, void *dst, uint32_t n)
{
	SweetSurface *s = (SweetSurface *)surface;
	if (s == NULL || s->memory == NULL || dst == NULL) { return 1; }
	uint64_t len = s->memory->getLength();
	if (off > len || n > len - off) { return 2; }
	if (n == 0) { return 0; }
	if (!sweet_surface_wod_hold_try(s)) { return 3; }
	int r = 0;
	IOBufferMemoryDescriptor *buffer = OSDynamicCast(IOBufferMemoryDescriptor, s->memory);
	uintptr_t va = 0;
	bool cacheable = false;
	if (buffer != NULL) {
		va = (uintptr_t)buffer->getBytesNoCopy(); cacheable = true;
	} else if (s->purged) {
		va = sweet_purge_sink_kva(); cacheable = true;
	} else if (sweet_pool_surface_pa(s) != 0) {
		va = (uintptr_t)(sweet_pool_kva + (sweet_pool_surface_pa(s) - SWEET_FB_HW_POOL_BASE)); cacheable = sweet_pool_copyback != 0;
	} else if (sweet_heap_surface_pa(s) != 0) {
		va = (uintptr_t)sweet_heap_va(sweet_heap_surface_pa(s), len);
	} else {
		IOLockLock(sweet_surface_lock);
		if (s->kernelMap != NULL) { va = (uintptr_t)s->kernelMap->getVirtualAddress(); }
		IOLockUnlock(sweet_surface_lock);
	}
	if (va != 0) {
		if (cacheable) { FlushPoC_DcacheRegion((vm_offset_t)(va + off), (unsigned)n); }
		memcpy(dst, (const void *)(va + off), n);
	} else if (s->memory->readBytes((IOByteCount)off, dst, (IOByteCount)n) != n) {
		r = 4;
	}
	sweet_surface_wod_unhold(s);
	return r;
}
#include "sweet_m2scaler.inc"   /* 0672 */
#endif /* SWEET */
