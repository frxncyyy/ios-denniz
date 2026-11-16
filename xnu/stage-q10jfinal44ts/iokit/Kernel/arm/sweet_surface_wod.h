/*
 * 0480 (memwod-20261113): WIRE-ON-DEMAND IOSurfaces.  SPDX-License-Identifier: MIT
 *
 * Until now every buffer surface is a wired IOBufferMemoryDescriptor for its whole life (~1.8 GiB in autocycle/runs/lag1).  With
 * kern.n2surf_wod = 1 a new surface that needs no kernel write at creation is PAGEABLE + PURGEABLE memory with no kernel mapping
 * (IOBufferMemoryDescriptor::inTaskWithOptions(NULL, kIOMemoryPageable | kIOMemoryPurgeable)); clients fault its pages in like any
 * anonymous memory, the VM may compress them, and it is wired (prepare()) only while somebody that needs PHYSICAL addresses holds it:
 *   - a GPU import (Nord2GPU: the pages are in the GPU page tables for the life of the import buffer, which covers every job),
 *   - a swap (SweetFramebuffer sweet_fb_retain_surface .. sweet_fb_release_surface, taken on the SwapEnd caller's thread),
 *   - a display IOMMU cache entry (sweet_fb_iommu_hw.h: from the page-table write to the unmap + TLB invalidate).
 * getPhysicalSegment() on an unwired descriptor panics ("IOGMD: not wired for the IODMACommand"), so the rule every holder follows is:
 * hold first, look up after, give the hold back only when the hardware can no longer reach the pages.  The last hold only starts an
 * idle timer; the unwire (complete()) is done by a 2 s thread call after idle_s without holders, so a surface shown or drawn every
 * frame is never unwired.  IOSurfaceSetPurgeable(Volatile) becomes a volatile VM object only while unwired and idle (the VM then
 * discards it under pressure without compressing); any hold or SetPurgeable(NonVolatile) makes it non-volatile FIRST, and a purged
 * object answers Empty at the next SetPurgeable.
 *
 * This file is the state machine only (no IOKit): shared by SweetSurface.cpp and memwod-20261113/wod-0480/test/wod_host_test.c, which
 * explores every interleaving of holders, the tick, SetPurgeable and VM purges against the invariants below.  All functions are called
 * with the one WOD lock held (a leaf mutex); the descriptor operations (prepare / complete / setPurgeable) are done WITHOUT the lock by
 * the thread that moved the state to BUSY, everybody else waits (holders, SetPurgeable) or skips (the tick, the never-blocking try).
 *
 * Invariants (checked by the host test after every step):
 *   I1  holds > 0  =>  state == WIRED (a holder may look up physical addresses at any time)
 *   I2  the descriptor is prepared exactly while state == WIRED, or BUSY on the way in or out (never prepared twice)
 *   I3  the VM object is volatile only while not prepared; prepare() is never called on a volatile object
 *   I4  complete() only with holds == 0, and never while a GPU write is pending
 *   I5  a client that was answered for SetPurgeable(NonVolatile) owns a non-volatile object until it asks for Volatile again
 */
#ifndef SWEET_SURFACE_WOD_H
#define SWEET_SURFACE_WOD_H
/* (uint*_t / bool come from the includer: the kernel's headers, or stdint.h + stdbool.h in the host test) */

enum { SWEET_WOD_UNWIRED = 0, SWEET_WOD_WIRED = 1, SWEET_WOD_BUSY = 2 };

/* What the caller has to do after a *_begin (lock dropped), then report with the matching *_done. */
enum {
	SWEET_WOD_A_NONE = 0,           /* nothing: the call is complete */
	SWEET_WOD_A_WAIT,               /* another thread owns a transition: sleep on the surface, call again */
	SWEET_WOD_A_WIRE,               /* [setPurgeable(NonVolatile) if md_volatile] then prepare() */
	SWEET_WOD_A_UNWIRE,             /* complete() */
	SWEET_WOD_A_VOLATILE,           /* setPurgeable(Volatile) */
	SWEET_WOD_A_NONVOLATILE         /* setPurgeable(NonVolatile, &old) */
};

/* IOSurfacePurgeable requests / answers (IOSurface.framework) */
#define SWEET_WOD_P_NONVOLATILE 0u
#define SWEET_WOD_P_VOLATILE    1u
#define SWEET_WOD_P_EMPTY       2u
#define SWEET_WOD_P_KEEP        3u

struct sweet_wod {
	uint8_t  on;                    /* 1 = pageable surface managed here; fixed at creation, read without the lock */
	uint8_t  state;                 /* SWEET_WOD_* */
	uint8_t  md_volatile;           /* the VM object is volatile now */
	uint8_t  want_volatile;         /* the clients' last SetPurgeable was Volatile / Empty */
	uint8_t  empty;                 /* the VM purged it: the next SetPurgeable answers Empty */
	uint32_t holds;                 /* GPU imports + swaps + display IOMMU entries */
	uint32_t idle_at;               /* uptime s of the creation / the last hold given back / the last unwire */
};

/* kern.n2surf_wod at creation.  Surfaces the kernel must write at creation (the 4:2:0 chroma fill, a snapshot of client memory) and
 * tiny ones stay wired buffers: no kernel write path to pageable pixels exists and none is added. */
static inline bool
sweet_wod_eligible(int mode, uint32_t alloc_size, bool chroma_init, bool client_memory, uint32_t min_bytes)
{
	return mode == 1 && !chroma_init && !client_memory && alloc_size >= min_bytes;
}

/* A holder that may block.  A_NONE = held and wired; A_WAIT; A_WIRE = this thread wires, then sweet_wod_hold_done. */
static inline int
sweet_wod_hold_begin(struct sweet_wod *w)
{
	if (w->state == SWEET_WOD_BUSY) { return SWEET_WOD_A_WAIT; }
	if (w->state == SWEET_WOD_WIRED) { w->holds++; return SWEET_WOD_A_NONE; }
	w->state = SWEET_WOD_BUSY;
	return SWEET_WOD_A_WIRE;
}

/* made_nonvolatile: the owner cleared the object's volatility before prepare() (only when md_volatile was set); was_empty: it had
 * been purged.  wired false = prepare() failed: no hold was taken. */
static inline bool
sweet_wod_hold_done(struct sweet_wod *w, bool wired, bool made_nonvolatile, bool was_empty)
{
	if (made_nonvolatile) { w->md_volatile = 0; }
	if (was_empty) { w->empty = 1; }
	w->state = wired ? SWEET_WOD_WIRED : SWEET_WOD_UNWIRED;
	if (wired) { w->holds++; }
	return wired;
}

/* A holder that must not block (the present thread).  True only when the surface is wired now. */
static inline bool
sweet_wod_hold_try(struct sweet_wod *w)
{
	if (w->state != SWEET_WOD_WIRED) { return false; }
	w->holds++;
	return true;
}

/* false = no hold to give back (an accounting bug: counted and logged by the caller, never wrapped). */
static inline bool
sweet_wod_unhold(struct sweet_wod *w, uint32_t now)
{
	if (w->holds == 0) { return false; }
	if (--w->holds == 0) { w->idle_at = now; }
	return true;
}

/* The 2 s tick.  in_use: an IOSurface use-count word is not 0; gpu_busy: a GPU job that writes the surface has not finished. */
static inline int
sweet_wod_tick_begin(struct sweet_wod *w, uint32_t now, uint32_t idle_s, bool in_use, bool gpu_busy)
{
	if (w->state == SWEET_WOD_BUSY || w->holds != 0 || now - w->idle_at < idle_s) { return SWEET_WOD_A_NONE; }
	if (w->state == SWEET_WOD_WIRED) {
		if (gpu_busy) { return SWEET_WOD_A_NONE; }
		w->state = SWEET_WOD_BUSY;
		return SWEET_WOD_A_UNWIRE;
	}
	if (w->want_volatile && !w->md_volatile && !in_use) {
		w->state = SWEET_WOD_BUSY;
		return SWEET_WOD_A_VOLATILE;
	}
	return SWEET_WOD_A_NONE;
}

static inline void
sweet_wod_tick_done(struct sweet_wod *w, int action, bool ok, uint32_t now)
{
	if (action == SWEET_WOD_A_UNWIRE) {
		w->state = SWEET_WOD_UNWIRED;
		w->idle_at = now;               /* the volatile step waits idle_s more */
	} else if (action == SWEET_WOD_A_VOLATILE) {
		w->state = SWEET_WOD_UNWIRED;
		if (ok) { w->md_volatile = 1; }
	}
}

/* IOSurfaceSetPurgeable.  A_NONE = recorded; A_WAIT; A_NONVOLATILE = this thread clears the volatility, then purgeable_done. */
static inline int
sweet_wod_purgeable_begin(struct sweet_wod *w, uint32_t req)
{
	if (w->state == SWEET_WOD_BUSY) { return SWEET_WOD_A_WAIT; }
	if (req == SWEET_WOD_P_VOLATILE || req == SWEET_WOD_P_EMPTY) {
		w->want_volatile = 1;           /* applied by the tick once unwired and idle */
		if (req == SWEET_WOD_P_EMPTY) { w->empty = 1; }
	} else if (req == SWEET_WOD_P_NONVOLATILE) {
		w->want_volatile = 0;
		if (w->md_volatile) {           /* only ever set while UNWIRED */
			w->state = SWEET_WOD_BUSY;
			return SWEET_WOD_A_NONVOLATILE;
		}
	}
	return SWEET_WOD_A_NONE;
}

static inline void
sweet_wod_purgeable_done(struct sweet_wod *w, bool was_empty)
{
	w->md_volatile = 0;
	if (was_empty) { w->empty = 1; }
	w->state = SWEET_WOD_UNWIRED;
}

/* The previous state for the client: Empty once the contents are gone, until it has been told with a NonVolatile request. */
static inline uint32_t
sweet_wod_purgeable_answer(struct sweet_wod *w, uint32_t req, uint32_t recorded_old)
{
	if (!w->empty) { return recorded_old; }
	if (req == SWEET_WOD_P_NONVOLATILE) { w->empty = 0; }
	return SWEET_WOD_P_EMPTY;
}

/* Display IOMMU cache entry of a wire-on-demand surface: may it be unmapped now?  (same safety rule as sweet_fb_iommu_victim: not
 * looked up in the generation being planned, not programmed in an overlay slot) + not looked up for idle_s. */
static inline bool
sweet_wod_iommu_idle(uint64_t stamp, uint64_t gen, bool programmed, uint32_t seen_at, uint32_t now, uint32_t idle_s)
{
	return stamp < gen && !programmed && now - seen_at >= idle_s;
}
#endif /* SWEET_SURFACE_WOD_H */
