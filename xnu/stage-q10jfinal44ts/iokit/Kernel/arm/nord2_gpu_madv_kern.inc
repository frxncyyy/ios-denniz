/*
 * 0481 (memwod-20261113): MADVISE for GPU buffers, KERNEL part.  Included ONCE by Nord2GPU.cpp after the Nord2GPU class.
 * What, why, ABI: nord2_gpu_madv.h.  SPDX-License-Identifier: MIT
 *
 * kern.n2gpu_madv          0 off (default) / 1 on (boot-arg n2madv=1)        kern.n2gpu_madv_thr   pressure mark in pages (0 = 20 % of RAM)
 * kern.n2gpu_madv_now      write 1 = one purge pass of everything marked, whatever the pressure (test hook)
 * Log: "NORD2 GPU MADV:" (first marks, every pass that purged, a line every 10 s while buffers are marked).
 * Locks: marks and the pass run under Nord2GPU::lock (the pass in a thread call); the pages are dropped in free_owner (owner_drain's
 * thread call, or inline for a buffer the core never adopted) with no GPU lock needed: the buffer is out of the GPU by then.
 */
static int           gMadvMode = 0;
static uint32_t      gMadvThr, gMadvNow;
static thread_call_t gMadvCall;
static uint32_t      gMadvMarked, gMadvCalls, gMadvGone, gMadvPurged, gMadvPasses, gMadvDropFail, gMadvLogAt, gMadvTold;
static uint64_t      gMadvMarkedBytes, gMadvPurgedBytes, gMadvDroppedBytes;
SYSCTL_INT(_kern, OID_AUTO, n2gpu_madv, CTLFLAG_RW | CTLFLAG_LOCKED, &gMadvMode, 0, "0481: GPU buffer MADVISE + purge under pressure: 0 off, 1 on");
SYSCTL_UINT(_kern, OID_AUTO, n2gpu_madv_thr, CTLFLAG_RW | CTLFLAG_LOCKED, &gMadvThr, 0, "0481: free + file-backed pages under which DONTNEED buffers go (0 = 20 % of RAM)");
SYSCTL_UINT(_kern, OID_AUTO, n2gpu_madv_now, CTLFLAG_RW | CTLFLAG_LOCKED, &gMadvNow, 0, "0481: 1 = purge every DONTNEED buffer at the next tick");
SYSCTL_UINT(_kern, OID_AUTO, n2gpu_madv_marked, CTLFLAG_RD | CTLFLAG_LOCKED, &gMadvMarked, 0, "0481: buffers marked DONTNEED now");
SYSCTL_UINT(_kern, OID_AUTO, n2gpu_madv_purged, CTLFLAG_RD | CTLFLAG_LOCKED, &gMadvPurged, 0, "0481: buffers purged");
extern "C" {
extern unsigned int vm_page_free_count, vm_page_pageable_external_count;
}

/* mapBuffer: pageable buffers are also purgeable while the mode is on (their pages can then be dropped under a live CPU mapping). */
static inline IOOptionBits n2madv_md_options(void) {
        static bool argsDone;
        if (!argsDone) { uint32_t v = 0; argsDone = true; if (PE_parse_boot_argn("n2madv", &v, sizeof(v)) && v <= 1) gMadvMode = (int)v; }   /* boot-arg, once */
        return __atomic_load_n(&gMadvMode, __ATOMIC_RELAXED) == 1 ? (IOOptionBits)kIOMemoryPurgeable : 0;
}

/* free_owner, after complete(): a buffer the purge released gives its pages back now; a marked one that goes normally is uncounted. */
static void n2madv_owner_gone(BufferOwner *o) {
        if (!(o->madv & N2MADV_F_DONTNEED)) return;
        uint64_t len = o->md ? o->md->getLength() : 0;
        __atomic_fetch_sub(&gMadvMarked, 1u, __ATOMIC_RELAXED); __atomic_fetch_sub(&gMadvMarkedBytes, len, __ATOMIC_RELAXED);
        if (!(o->madv & N2MADV_F_PURGED) || !o->md) return;
        IOOptionBits was = 0;
        if (o->md->setPurgeable(kIOMemoryPurgeableEmpty, &was) == kIOReturnSuccess) {
                (void)o->md->setPurgeable(kIOMemoryPurgeableNonVolatile, NULL);    /* a stray CPU access zero-fills, it does not fault */
                __atomic_fetch_add(&gMadvDroppedBytes, len, __ATOMIC_RELAXED);
        } else {
                __atomic_fetch_add(&gMadvDropFail, 1u, __ATOMIC_RELAXED);            /* the pages stay until the client unmaps */
        }
}

/* GPU lock held.  Releases marked, idle buffers until `goal` bytes are on their way out. */
static uint64_t n2madv_pass(Nord2GPU *g, uint64_t goal) {
        uint64_t freed = 0; uint32_t n = 0;
        for (unsigned c = 1; c < N2C_MAX_CTX && freed < goal; c++) {
                struct n2c_ctx *x = &g->core->ctxs[c];
                if (x->state == N2C_CTX_FREE) continue;
                for (unsigned k = 0; k < N2C_MAX_BOS && freed < goal; k++) {
                        struct n2c_bo *b = x->bos[k]; BufferOwner *o = b ? (BufferOwner *)b->cookie : NULL;
                        if (!o || !n2madv_purge_ok(o->madv, b->handle_live, b->refs, (b->flags & N2C_BO_IMPORTED) != 0, b->state == N2C_BO_MAPPED)) continue;
                        uint64_t size = b->size;
                        o->madv |= N2MADV_F_PURGED;
                        (void)n2c_bo_release(g->core, c, b->handle);   /* b may be gone now (or waits for the context's older jobs) */
                        freed += size; n++;
                }
        }
        gMadvPasses++;
        if (n) { gMadvPurged += n; gMadvPurgedBytes += freed;
                IOLog("NORD2 GPU MADV: purged %u buffers %llu KiB (goal %llu KiB; marked now %u; purged %u, %llu MiB since boot)\n", n, freed >> 10,
                      goal == ~0ull ? 0ull : goal >> 10, gMadvMarked, gMadvPurged, gMadvPurgedBytes >> 20); }
        return freed;
}

static void n2madv_tick(thread_call_param_t, thread_call_param_t) {
        Nord2GPU *g = gSurfaceGpu;
        uint64_t thr = __atomic_load_n(&gMadvThr, __ATOMIC_RELAXED);
        if (!thr) thr = n2madv_default_thr(gPhysSize, (uint32_t)page_size);
        uint64_t avail = (uint64_t)vm_page_free_count + vm_page_pageable_external_count;
        uint64_t goal = n2madv_goal(avail, thr, (uint32_t)page_size, 32ull << 20);
        if (__atomic_exchange_n(&gMadvNow, 0u, __ATOMIC_ACQ_REL)) goal = ~0ull;
        if (g && goal && __atomic_load_n(&gMadvMarked, __ATOMIC_RELAXED)) {
                g->retain();
                IOLockLock(g->lock);
                if (g->ready && !g->stopping && g->core) (void)n2madv_pass(g, goal);
                IOLockUnlock(g->lock);
                g->release();
        }
        uint64_t t = now_ms(NULL) / 1000;
        if (gMadvMarked && (uint32_t)t - gMadvLogAt >= 10) { gMadvLogAt = (uint32_t)t;
                IOLog("NORD2 GPU MADV: marked=%u/%llu MiB calls=%u gone=%u purged=%u/%llu MiB dropped=%llu MiB drop_fail=%u passes=%u avail=%llu thr=%llu mode=%d\n",
                      gMadvMarked, gMadvMarkedBytes >> 20, gMadvCalls, gMadvGone, gMadvPurged, gMadvPurgedBytes >> 20, gMadvDroppedBytes >> 20, gMadvDropFail,
                      gMadvPasses, avail, thr, gMadvMode); }
        uint64_t dl = 0; clock_interval_to_deadline(2, kSecondScale, &dl);
        thread_call_enter_delayed(gMadvCall, dl);
}

/* Nord2GPUUserClient::externalMethod, selector NORD2_GPU_SEL_MADVISE (before the transport's validator, which does not know it). */
static IOReturn n2madv_call(Nord2GPU *g, IOLock *clientLock, const bool *closed, const unsigned *context, IOExternalMethodArguments *a) {
        if (a->scalarInputCount != 2 || a->scalarOutputCount < 1 || a->structureInputSize || a->structureInputDescriptor || a->structureOutputDescriptor ||
            a->asyncWakePort != MACH_PORT_NULL || a->scalarInput[0] > UINT32_MAX || a->scalarInput[1] > UINT32_MAX) return kIOReturnBadArgument;
        uint32_t handle = (uint32_t)a->scalarInput[0], advice = (uint32_t)a->scalarInput[1], retained = 1; int mark = 0;
        IOReturn r = kIOReturnNotOpen;
        IOLockLock(clientLock); IOLockLock(g->lock);
        if (!*closed && g->ready && !g->stopping && g->core) {
                const struct n2c_bo *b = *context < N2C_MAX_CTX ? n2c_bo_lookup(g->core, *context, handle) : NULL;
                BufferOwner *o = b ? (BufferOwner *)b->cookie : NULL;
                IOOptionBits st = 0;
                bool imported = b && (b->flags & N2C_BO_IMPORTED), mapped = b && b->state == N2C_BO_MAPPED;
                bool can = o && o->md && o->prepared && !imported && mapped && advice == N2MADV_DONTNEED && gMadvMode == 1 &&
                           o->md->setPurgeable(kIOMemoryPurgeableKeepCurrent, &st) == kIOReturnSuccess;    /* only memory made purgeable (mode 1 at ALLOC) */
                uint8_t none = 0;
                r = n2madv_advise(gMadvMode, b != NULL, n2madv_markable(o && o->md, o && o->prepared, can, imported, mapped), o ? &o->madv : &none,
                                  advice, &retained, &mark) ? kIOReturnSuccess : kIOReturnBadArgument;
                gMadvCalls++; if (b == NULL) gMadvGone++;
                if (mark) { uint64_t len = o->md ? o->md->getLength() : 0;
                        /* (atomic: n2madv_owner_gone counts down without the GPU lock) */
                        if (mark > 0) { __atomic_fetch_add(&gMadvMarked, 1u, __ATOMIC_RELAXED); __atomic_fetch_add(&gMadvMarkedBytes, len, __ATOMIC_RELAXED); }
                        else { __atomic_fetch_sub(&gMadvMarked, 1u, __ATOMIC_RELAXED); __atomic_fetch_sub(&gMadvMarkedBytes, len, __ATOMIC_RELAXED); } }
                if (mark > 0 && !gMadvCall) {
                        gMadvCall = thread_call_allocate(n2madv_tick, NULL);
                        if (gMadvCall) { uint64_t dl = 0; clock_interval_to_deadline(2, kSecondScale, &dl); thread_call_enter_delayed(gMadvCall, dl); }
                }
                if (gMadvTold < 4 && (mark > 0 || b == NULL)) { gMadvTold++;
                        IOLog("NORD2 GPU MADV: ctx=%u handle=%u advice=%u -> retained=%u%s (marked %u)\n", *context, handle, advice, retained, mark > 0 ? " marked" : "", gMadvMarked); }
        }
        IOLockUnlock(g->lock); IOLockUnlock(clientLock);
        if (r == kIOReturnSuccess) { a->scalarOutput[0] = retained; a->scalarOutputCount = 1; }
        return r;
}
