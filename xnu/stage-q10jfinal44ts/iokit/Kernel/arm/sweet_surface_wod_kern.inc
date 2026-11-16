/*
 * 0480 (memwod-20261113): wire-on-demand IOSurfaces, KERNEL part.  Included ONCE by SweetSurface.cpp (before the display IOMMU code).
 * State machine, rules and invariants: sweet_surface_wod.h.  SPDX-License-Identifier: MIT
 *
 * kern.n2surf_wod        0 (default) = every surface is a wired buffer as before; 1 = new eligible surfaces are pageable + purgeable
 *                        and wired on demand.  Read at creation only: surfaces keep the kind they were made with.
 *                        Boot-arg n2wod=1 turns it on from the first surface of the boot (backboardd's and SpringBoard's included).
 * kern.n2surf_wod_idle   s without a holder before the unwire, and before a volatile surface becomes a volatile VM object (5)
 * kern.n2surf_wod_min    smallest surface made pageable (bytes, 65536)
 * Log: "NORD2 SURF WOD:" (first 8 wires / unwires, every failure, one line every 10 s while pageable surfaces exist).
 *
 * Locks: sweet_wod_lock is a LEAF mutex (nothing is called with it held; IOLockSleep on the surface for a transition in progress).  It
 * is taken under n2io_lock (IOMMU entry holds) and under the GPU locks (free_owner), never the other way round.  sweet_pool_evict_lock
 * is NOT used: the 0417 purge pass holds it while it takes n2io_lock, the opposite order.  Purgeable changes of one surface are
 * serialised by the BUSY state instead.
 * Accounting: sweet_surface_wired counts a pageable surface only while it is wired; sweet_surface_buffers and the per-process bytes
 * (0417 runaway guard) count it always.
 */
static int           sweet_wod_mode = 0;
static uint32_t      sweet_wod_idle_s = 5, sweet_wod_min = 65536;
static IOLock       *sweet_wod_lock;
static thread_call_t sweet_wod_tc;
static uint32_t      sweet_wod_live, sweet_wod_wired_n, sweet_wod_wires, sweet_wod_unwires, sweet_wod_fail, sweet_wod_vol, sweet_wod_emptied;
static uint32_t      sweet_wod_unmatched, sweet_wod_told, sweet_wod_log_at, sweet_wod_slow_ms;
static uint64_t      sweet_wod_bytes, sweet_wod_wired_bytes;
static uint32_t      n2io_wod_swept, n2io_wod_refused;         /* display IOMMU side (sweet_fb_iommu_hw.h) */
SYSCTL_INT(_kern, OID_AUTO, n2surf_wod, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_wod_mode, 0, "0480: new IOSurfaces pageable, wired on demand: 0 off, 1 on");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_wod_idle, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_wod_idle_s, 0, "0480: s without a holder before the unwire");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_wod_min, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_wod_min, 0, "0480: smallest pageable surface (bytes)");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_wod_live, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_wod_live, 0, "0480: pageable surfaces alive");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_wod_wired, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_wod_wired_n, 0, "0480: ... wired now");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_wod_fail, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_wod_fail, 0, "0480: prepare() failures");
SYSCTL_UINT(_kern, OID_AUTO, n2surf_wod_unmatched, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_wod_unmatched, 0, "0480: holds given back that were never taken (must stay 0)");

static uint32_t sweet_surface_uptime(void);
static bool sweet_surface_publish(SweetSurface *s);
static void n2io_wod_sweep(uint32_t now, uint32_t idle_s);     /* sweet_fb_iommu_hw.h */
struct sweet_fb_iommu_ent;
static void n2io_wod_unmapped(struct sweet_fb_iommu_ent *e);     /* sweet_fb_iommu_wod.inc, called by n2io_unmap_ent */
static void sweet_wod_tick(thread_call_param_t, thread_call_param_t);

static inline IOBufferMemoryDescriptor *
sweet_wod_md(SweetSurface *s)
{
	return (IOBufferMemoryDescriptor *)s->memory;       /* wod.on: always the buffer it was created with (0406 / 0417 leave it alone) */
}

/* The lock and the tick exist from the first pageable surface on. */
static bool
sweet_wod_start(void)
{
	if (sweet_wod_lock == NULL) {
		IOLock *l = IOLockAlloc();
		if (l != NULL && !OSCompareAndSwapPtr(NULL, l, (void * volatile *)&sweet_wod_lock)) { IOLockFree(l); }
	}
	if (sweet_wod_lock != NULL && sweet_wod_tc == NULL) {
		thread_call_t tc = thread_call_allocate(sweet_wod_tick, NULL);
		if (tc != NULL && !OSCompareAndSwapPtr(NULL, tc, (void * volatile *)&sweet_wod_tc)) {
			thread_call_free(tc);
		} else if (tc != NULL) {
			uint64_t dl = 0;
			clock_interval_to_deadline(2, kSecondScale, &dl);
			thread_call_enter_delayed(tc, dl);
			IOLog("NORD2 SURF WOD: wire-on-demand surfaces on (kern.n2surf_wod %d, idle %u s, min %u bytes)\n", sweet_wod_mode, sweet_wod_idle_s, sweet_wod_min);
		}
	}
	return sweet_wod_lock != NULL && sweet_wod_tc != NULL;
}

/* May block (prepare() pages the pixels in): never on the present thread, never under the GPU lock.  false = not wired, no hold. */
extern "C" bool
sweet_surface_wod_hold(void *surface)
{
	SweetSurface *s = (SweetSurface *)surface;
	if (s == NULL || !s->wod.on) { return true; }
	int a;
	IOLockLock(sweet_wod_lock);
	while ((a = sweet_wod_hold_begin(&s->wod)) == SWEET_WOD_A_WAIT) {
		(void)IOLockSleep(sweet_wod_lock, &s->wod, THREAD_UNINT);
	}
	bool unvol = a == SWEET_WOD_A_WIRE && s->wod.md_volatile;
	IOLockUnlock(sweet_wod_lock);
	if (a == SWEET_WOD_A_NONE) { return true; }
	uint64_t t0 = mach_absolute_time(), ns = 0;
	IOOptionBits old = kIOMemoryPurgeableNonVolatile;
	if (unvol) { (void)sweet_wod_md(s)->setPurgeable(kIOMemoryPurgeableNonVolatile, &old); }     /* I3: never wire a volatile object */
	IOReturn r = sweet_wod_md(s)->prepare(kIODirectionInOut);
	absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
	IOLockLock(sweet_wod_lock);
	bool ok = sweet_wod_hold_done(&s->wod, r == kIOReturnSuccess, unvol, unvol && old == kIOMemoryPurgeableEmpty);
	if (ok) {
		sweet_wod_wires++; sweet_wod_wired_n++; sweet_wod_wired_bytes += s->allocSize;
		OSAddAtomic64((SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
	} else {
		sweet_wod_fail++;
	}
	if (unvol && old == kIOMemoryPurgeableEmpty) { sweet_wod_emptied++; }
	if ((uint32_t)(ns / 1000000ull) > sweet_wod_slow_ms) { sweet_wod_slow_ms = (uint32_t)(ns / 1000000ull); }
	IOLockWakeup(sweet_wod_lock, &s->wod, false);
	IOLockUnlock(sweet_wod_lock);
	if (!ok || sweet_wod_told++ < 8) {
		IOLog("NORD2 SURF WOD: wire id %u %u KiB -> %s in %llu us (prepare 0x%x%s; wired %u of %u, %llu MiB)\n", s->id, s->allocSize >> 10,
		    ok ? "wired" : "FAILED", ns / 1000ull, (unsigned)r, unvol ? (old == kIOMemoryPurgeableEmpty ? ", was purged" : ", was volatile") : "",
		    sweet_wod_wired_n, sweet_wod_live, sweet_wod_wired_bytes >> 20);
	}
	return ok;
}

/* Never blocks: a hold only if the surface is wired now (the display IOMMU map on the present thread, the GPU import after its
 * blocking hold).  true for surfaces that are not wire-on-demand. */
extern "C" bool
sweet_surface_wod_hold_try(void *surface)
{
	SweetSurface *s = (SweetSurface *)surface;
	if (s == NULL || !s->wod.on) { return true; }
	IOLockLock(sweet_wod_lock);
	bool ok = sweet_wod_hold_try(&s->wod);
	IOLockUnlock(sweet_wod_lock);
	return ok;
}

extern "C" void
sweet_surface_wod_unhold(void *surface)
{
	SweetSurface *s = (SweetSurface *)surface;
	if (s == NULL || !s->wod.on) { return; }
	IOLockLock(sweet_wod_lock);
	if (!sweet_wod_unhold(&s->wod, sweet_surface_uptime())) { sweet_wod_unmatched++; }
	IOLockUnlock(sweet_wod_lock);
}

/* True when a lookup of physical addresses would panic (diagnostic paths ask before they walk s->memory). */
extern "C" bool
sweet_surface_wod_unwired(const void *surface)
{
	const SweetSurface *s = (const SweetSurface *)surface;
	return s != NULL && s->wod.on && __atomic_load_n(&s->wod.state, __ATOMIC_RELAXED) != SWEET_WOD_WIRED;
}

/*
 * IOSurfaceRootUserClient::create, in front of the wired-buffer path.  true = this function made the surface or refused it (*ret);
 * false = not eligible / no pageable memory: the wired path runs as before.
 */
static bool
sweet_wod_create(SweetSurface *s, OSDictionary *props, int32_t pid, IOReturn *ret)
{
	static bool args_done;
	if (!args_done) {       /* boot-arg n2wod (the sysctl may change it later) */
		uint32_t v = 0;
		args_done = true;
		if (PE_parse_boot_argn("n2wod", &v, sizeof(v)) && v <= 1) { sweet_wod_mode = (int)v; }
	}
	/* 0483 (integ-20261114): never a surface that already has its memory: the display pool (0394) and the graphics heap (0440) attach
	 * theirs before this call, and neither is XNU pages (no prepare/complete, no VM object).  The create path only reaches here when it
	 * is neither (if pooled || heaped ... else if wod), so this is the explicit form of that rule; the holders test wod.on, never set here. */
	if (s->memory != NULL) {
		return false;
	}
	bool chroma = s->planeCount == 2 && (s->pixelFormat == 0x34323066 || s->pixelFormat == 0x34323076 ||
	    s->pixelFormat == 0x78663230 || s->pixelFormat == 0x78343230);
	bool client = props->getObject("IOSurfaceAddress") != NULL || props->getObject("IOSurfaceClientAddress") != NULL;
	if (!sweet_wod_eligible(__atomic_load_n(&sweet_wod_mode, __ATOMIC_RELAXED), s->allocSize, chroma, client,
	    __atomic_load_n(&sweet_wod_min, __ATOMIC_RELAXED)) || !sweet_wod_start()) {
		return false;
	}
	if (!sweet_mem_admit(s->allocSize, pid)) {      /* 0417/0418: runaway-process guard and the memory floor still apply */
		*ret = kIOReturnNoMemory;
		return true;
	}
	/* NULL task = no kernel mapping (IOBufferMemoryDescriptor.h): the kernel's pageable maps (16 x 96 MiB, shared with the GPU's
	 * pageable buffers) are not used.  Fresh pageable memory is zero-fill anonymous VM: no bzero. */
	IOBufferMemoryDescriptor *mem = IOBufferMemoryDescriptor::inTaskWithOptions(NULL,
	    kIODirectionInOut | kIOMemoryPageable | kIOMemoryPurgeable, s->allocSize, page_size);
	if (mem == NULL || mem->getLength() < s->allocSize) {
		OSSafeReleaseNULL(mem);
		return false;
	}
	s->wod.on = 1;
	s->wod.idle_at = sweet_surface_uptime();
	s->memory = mem;
	s->mapCache = kIOMapDefaultCache;
	OSIncrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
	OSIncrementAtomic((volatile SInt32 *)&sweet_surface_created);
	sweet_mem_proc_charge(pid, (int64_t)s->allocSize);
	IOLockLock(sweet_wod_lock);
	sweet_wod_live++; sweet_wod_bytes += s->allocSize;
	IOLockUnlock(sweet_wod_lock);
	if (!sweet_surface_publish(s)) {
		*ret = kIOReturnNoResources;     /* s->release() in the caller: SweetSurface::free undoes the accounting */
	}
	return true;
}

/* SweetSurface::free, first thing after the IOMMU mapping went: nobody holds it any more (holders have a retain). */
static void
sweet_wod_free(SweetSurface *s)
{
	if (!s->wod.on || sweet_wod_lock == NULL) { return; }
	IOLockLock(sweet_wod_lock);
	bool wired = s->wod.state == SWEET_WOD_WIRED;
	uint32_t holds = s->wod.holds;
	s->wod.state = SWEET_WOD_UNWIRED; s->wod.holds = 0;
	sweet_wod_live--; sweet_wod_bytes -= s->allocSize;
	if (wired) { sweet_wod_wired_n--; sweet_wod_wired_bytes -= s->allocSize; }
	IOLockUnlock(sweet_wod_lock);
	if (wired) {
		(void)sweet_wod_md(s)->complete(kIODirectionInOut);
	} else {        /* free() takes allocSize out of sweet_surface_wired for every buffer surface: this one is not in it */
		OSAddAtomic64((SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
	}
	if (holds != 0) {
		IOLog("NORD2 SURF WOD: LEAK at free: surface %u freed with %u holds\n", s->id, holds);
	}
}

/* IOSurfaceSetPurgeable for a pageable surface (after the recorded state changed; no other lock held).  Returns the old state. */
static uint32_t
sweet_wod_purgeable(SweetSurface *s, uint32_t req, uint32_t old)
{
	if (!s->wod.on) { return old; }
	int a;
	IOLockLock(sweet_wod_lock);
	while ((a = sweet_wod_purgeable_begin(&s->wod, req)) == SWEET_WOD_A_WAIT) {
		(void)IOLockSleep(sweet_wod_lock, &s->wod, THREAD_UNINT);
	}
	if (a == SWEET_WOD_A_NONVOLATILE) {
		IOOptionBits was = kIOMemoryPurgeableNonVolatile;
		IOLockUnlock(sweet_wod_lock);
		(void)sweet_wod_md(s)->setPurgeable(kIOMemoryPurgeableNonVolatile, &was);
		IOLockLock(sweet_wod_lock);
		sweet_wod_purgeable_done(&s->wod, was == kIOMemoryPurgeableEmpty);
		if (was == kIOMemoryPurgeableEmpty) { sweet_wod_emptied++; }
		IOLockWakeup(sweet_wod_lock, &s->wod, false);
	}
	old = sweet_wod_purgeable_answer(&s->wod, req, old);
	IOLockUnlock(sweet_wod_lock);
	return old;
}

/* One surface of the tick (retained by the caller, no lock held). */
static void
sweet_wod_tick_one(SweetSurface *s, uint32_t now, uint32_t idle_s)
{
	volatile uint32_t *w = (volatile uint32_t *)((uintptr_t)s->state->getBytesNoCopy() + SWEET_USE_STATE_WORDS);
	bool in_use = (w[0] | w[1]) != 0;
	/* I4: a GPU write needs an import, an import is a hold; asked anyway (takes the GPU lock, so before ours) */
	bool busy = s->wod.state == SWEET_WOD_WIRED && s->wod.holds == 0 && nord2_gpu_surface_busy(s);
	IOLockLock(sweet_wod_lock);
	int a = sweet_wod_tick_begin(&s->wod, now, idle_s, in_use, busy);
	IOLockUnlock(sweet_wod_lock);
	if (a == SWEET_WOD_A_NONE) { return; }
	IOReturn r = a == SWEET_WOD_A_UNWIRE ? sweet_wod_md(s)->complete(kIODirectionInOut) :
	    sweet_wod_md(s)->setPurgeable(kIOMemoryPurgeableVolatile, NULL);
	IOLockLock(sweet_wod_lock);
	sweet_wod_tick_done(&s->wod, a, r == kIOReturnSuccess, now);
	if (a == SWEET_WOD_A_UNWIRE) {
		sweet_wod_unwires++; sweet_wod_wired_n--; sweet_wod_wired_bytes -= s->allocSize;
		OSAddAtomic64(-(SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
	} else if (r == kIOReturnSuccess) {
		sweet_wod_vol++;
	}
	IOLockWakeup(sweet_wod_lock, &s->wod, false);
	IOLockUnlock(sweet_wod_lock);
	if (r != kIOReturnSuccess || (a == SWEET_WOD_A_UNWIRE && sweet_wod_unwires <= 8)) {
		IOLog("NORD2 SURF WOD: %s id %u %u KiB -> 0x%x (wired %u of %u, %llu MiB)\n", a == SWEET_WOD_A_UNWIRE ? "unwire" : "volatile", s->id,
		    s->allocSize >> 10, (unsigned)r, sweet_wod_wired_n, sweet_wod_live, sweet_wod_wired_bytes >> 20);
	}
}

/* Every 2 s: idle display IOMMU entries of pageable surfaces go first (unmap + TLB invalidate, then their hold), then the surfaces
 * without holders are unwired / made volatile.  Thread-call context: may take the GPU lock and n2io_lock, may free surfaces. */
static void
sweet_wod_tick(thread_call_param_t, thread_call_param_t)
{
	static SweetSurface *cs[256];
	uint32_t now = sweet_surface_uptime(), idle_s = __atomic_load_n(&sweet_wod_idle_s, __ATOMIC_RELAXED);
	static uint32_t next_id = 1;
	if (sweet_wod_live != 0 && sweet_surface_lock != NULL) {
		n2io_wod_sweep(now, idle_s);
		uint32_t n = 0, id = next_id;
		IOLockLock(sweet_surface_lock);
		for (uint32_t k = 1; k < SWEET_SURFACE_MAX_ID && n < 256; k++, id = id + 1 < SWEET_SURFACE_MAX_ID ? id + 1 : 1) {
			SweetSurface *t = sweet_surface_table[id];
			if (t != NULL && t->wod.on && t->state != NULL && t->wod.holds == 0 &&
			    (t->wod.state == SWEET_WOD_WIRED || (t->wod.want_volatile && !t->wod.md_volatile))) {
				t->retain();
				cs[n++] = t;
			}
		}
		next_id = id;
		IOLockUnlock(sweet_surface_lock);
		for (uint32_t i = 0; i < n; i++) {
			sweet_wod_tick_one(cs[i], now, idle_s);
			cs[i]->release();
		}
		if (now - sweet_wod_log_at >= 10) {
			sweet_wod_log_at = now;
			IOLog("NORD2 SURF WOD: live=%u/%llu MiB wired=%u/%llu MiB wires=%u unwires=%u volatile=%u purged=%u fail=%u unmatched=%u slowest_wire=%u ms "
			    "iommu_swept=%u iommu_refused=%u mode=%d idle=%u\n", sweet_wod_live, sweet_wod_bytes >> 20, sweet_wod_wired_n, sweet_wod_wired_bytes >> 20,
			    sweet_wod_wires, sweet_wod_unwires, sweet_wod_vol, sweet_wod_emptied, sweet_wod_fail, sweet_wod_unmatched, sweet_wod_slow_ms,
			    n2io_wod_swept, n2io_wod_refused, sweet_wod_mode, idle_s);
		}
	}
	uint64_t dl = 0;
	clock_interval_to_deadline(2, kSecondScale, &dl);
	thread_call_enter_delayed(sweet_wod_tc, dl);
}
