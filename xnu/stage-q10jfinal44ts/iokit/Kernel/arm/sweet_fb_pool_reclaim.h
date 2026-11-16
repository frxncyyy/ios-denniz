/*
 * 0406 (poolreclaim-20261114): give display-pool ranges of idle, VOLATILE pool surfaces back while their holders keep them.
 * SPDX-License-Identifier: MIT
 *
 * Why the pool stays full (night18, q10jgb mode 3, 23 surfaces / 247 of 256 MiB for an hour, not one "free pa"): a pool range goes
 * back only in SweetSurface::free(), at the last retain.  Every leak candidate still has a CLIENT MAPPING (SweetSurfaceRef in some
 * process's fRefs: "mappings 1-2"; "descriptor retains" = the surface's own reference + one IOMemoryMap per mapping).  The holders are
 * backboardd (its own never-shown 1125x2436 surfaces, and lookups of app drawables: SubwaySurf ids 161..163 are still mapped 40 min
 * after SubwaySurf died) and MobileSlideShow.  These are not kernel leaks: those processes can still read or write the pixels at the
 * address the lock result gave them, so the range cannot be freed or handed to another surface, and the kernel may not copy it.
 *
 * What IOSurface allows: a surface marked kIOSurfacePurgeableVolatile (IOSurfaceSetPurgeable, sweet_surface_abi.h) has contents the
 * holders said may be thrown away; the next SetPurgeable answers kIOSurfacePurgeableEmpty and the client redraws.  So 0406 EVICTS
 * only such surfaces: same user addresses, new zeroed normal memory behind them (kIOMapOverwrite remap of every client mapping), state
 * Empty, and the old pool range goes back after a grace period once nothing can still reach it.  No pixel is copied.
 *
 * Pure: no IOKit; the kernel (SweetSurface.cpp) and the host test (poolreclaim-20261114/test/reclaim_host_test.c) share it.
 */
#ifndef SWEET_FB_POOL_RECLAIM_H
#define SWEET_FB_POOL_RECLAIM_H
#include "sweet_fb_hw_pool_alloc.h"

/* kern.n2fbhw_pool_reclaim: 0 off, 1 decide and log only (default: same behaviour as q10jgb), 2 evict volatile idle surfaces */
enum { SWEET_POOL_RECLAIM_OFF = 0, SWEET_POOL_RECLAIM_REPORT = 1, SWEET_POOL_RECLAIM_EVICT = 2 };

#define SWEET_PURGEABLE_NONVOLATILE     0u      /* kIOSurfacePurgeableNonVolatile */
#define SWEET_PURGEABLE_VOLATILE        1u      /* kIOSurfacePurgeableVolatile */
#define SWEET_PURGEABLE_EMPTY           2u      /* kIOSurfacePurgeableEmpty: what the next SetPurgeable answers after an eviction */

/* Why a pool surface may (0) or may not be evicted now; first matching reason wins (index into the per-reason counters). */
enum {
	SWEET_RECLAIM_OK = 0,
	SWEET_RECLAIM_NO_POOL,          /* not (or no longer) a pool surface */
	SWEET_RECLAIM_NO_KERNEL,        /* made by the kernel (H2 test patterns, panel): never */
	SWEET_RECLAIM_NO_MODE,          /* kern.n2surfuse < 2: display / GPU holds are not counted, idleness cannot be proven */
	SWEET_RECLAIM_NO_IOMMU,         /* display IOMMU active (0401): it may hold a translation of the old pages */
	SWEET_RECLAIM_NO_YOUNG,         /* younger than the age limit */
	SWEET_RECLAIM_NO_ACTIVE,        /* named in a swap within the age limit */
	SWEET_RECLAIM_NO_NONVOLATILE,   /* contents still wanted (purgeable state != volatile) */
	SWEET_RECLAIM_NO_USE,           /* IOSurfaceIsInUse: a use-count word != 0 (user holds, or the display / GPU in category 0) */
	SWEET_RECLAIM_NO_DISPLAY,       /* a swap holds it (scanout until FRAME_DONE) */
	SWEET_RECLAIM_NO_GPU_JOB,       /* a GPU job lists it */
	SWEET_RECLAIM_NO_GPU_IMPORT,    /* a GPU import buffer maps its pages (GPU MMU) */
	SWEET_RECLAIM_NO_GPU_BUSY,      /* nord2_gpu_surface_busy: a job that writes it has not finished */
	SWEET_RECLAIM_NO_PENDING,       /* already evicted, old range waiting for its grace */
	SWEET_RECLAIM_NREASONS
};

struct sweet_reclaim_cand {
	uint32_t born_at, shown_at;     /* uptime seconds; shown_at 0 = never named in a swap */
	uint32_t purgeable;             /* SWEET_PURGEABLE_* */
	uint32_t w0, w1;                /* use-count words (state page +0x18 / +0x1c) */
	uint32_t kd, kg, imports;       /* display holds, GPU job holds, GPU import buffers */
	int32_t  creator;               /* pid, 0 = kernel */
	bool     pool, gpu_busy, pending;
	uint32_t g0, ngran;             /* its granules in the pool bitmap */
};

static inline uint32_t
sweet_reclaim_verdict(const struct sweet_reclaim_cand *c, uint32_t now, uint32_t age_limit, bool use_mode_all, bool iommu_on)
{
	if (!c->pool || c->ngran == 0) return SWEET_RECLAIM_NO_POOL;
	if (c->pending) return SWEET_RECLAIM_NO_PENDING;
	if (c->creator <= 0) return SWEET_RECLAIM_NO_KERNEL;
	if (!use_mode_all) return SWEET_RECLAIM_NO_MODE;
	if (iommu_on) return SWEET_RECLAIM_NO_IOMMU;
	if (age_limit == 0) age_limit = 1;
	if (now - c->born_at < age_limit) return SWEET_RECLAIM_NO_YOUNG;
	if (c->shown_at != 0 && now - c->shown_at < age_limit) return SWEET_RECLAIM_NO_ACTIVE;
	if (c->purgeable != SWEET_PURGEABLE_VOLATILE) return SWEET_RECLAIM_NO_NONVOLATILE;
	if (c->w0 != 0 || c->w1 != 0) return SWEET_RECLAIM_NO_USE;
	if (c->kd != 0) return SWEET_RECLAIM_NO_DISPLAY;
	if (c->kg != 0) return SWEET_RECLAIM_NO_GPU_JOB;
	if (c->imports != 0) return SWEET_RECLAIM_NO_GPU_IMPORT;
	if (c->gpu_busy) return SWEET_RECLAIM_NO_GPU_BUSY;
	return SWEET_RECLAIM_OK;
}

/* Least recently used first: last swap, or the creation for a never-shown surface. */
static inline uint32_t
sweet_reclaim_last_use(const struct sweet_reclaim_cand *c)
{
	return c->shown_at != 0 ? c->shown_at : c->born_at;
}

static inline void
sweet_reclaim_clear(struct sweet_fb_pool *p, uint32_t g0, uint32_t n)
{
	for (uint32_t k = g0; k < g0 + n && k < SWEET_FB_POOL_NGRAN; k++) {
		p->bits[k >> 6] &= ~(1ull << (k & 63));
	}
}

/* Is there a first-fit run of n free granules (what sweet_fb_pool_alloc would find)? */
static inline bool
sweet_reclaim_fits(const struct sweet_fb_pool *p, uint32_t n)
{
	uint32_t run = 0;
	for (uint32_t g = 0; g < SWEET_FB_POOL_NGRAN; g++) {
		run = sweet_fb_pool_test(p, g) ? 0 : run + 1;
		if (run >= n) return true;
	}
	return false;
}

/*
 * Pick LRU victims (oldest first, then the ones the fit does not need are dropped again) among the candidates with verdict[i] == SWEET_RECLAIM_OK so that an allocation of need_bytes fits.
 * sim = a copy of the pool bitmap, already with the ranges of pending evictions cleared (they come back after their grace); it is
 * modified.  out[] receives candidate indices (at most out_cap).  Returns the number of victims; 0 when it already fits OR when even
 * evicting every allowed candidate would not make it fit (then nothing is evicted for nothing).  *fits_now tells the two apart.
 */
static inline uint32_t
sweet_reclaim_pick(struct sweet_fb_pool *sim, const struct sweet_reclaim_cand *c, const uint32_t *verdict, uint32_t n,
    uint64_t need_bytes, uint32_t *out, uint32_t out_cap, bool *fits_now)
{
	uint32_t need = sweet_fb_pool_granules(need_bytes), k = 0;
	*fits_now = need != 0 && need <= SWEET_FB_POOL_NGRAN && sweet_reclaim_fits(sim, need);
	if (*fits_now || need == 0 || need > SWEET_FB_POOL_NGRAN) return 0;
	uint8_t taken[256] = { 0 };
	if (n > 256) n = 256;
	while (k < out_cap) {
		int best = -1;
		for (uint32_t i = 0; i < n; i++) {
			if (verdict[i] != SWEET_RECLAIM_OK || taken[i]) continue;
			if (best < 0 || sweet_reclaim_last_use(&c[i]) < sweet_reclaim_last_use(&c[best])) best = (int)i;
		}
		if (best < 0) return 0;         /* ran out of victims without a fit: evict none */
		taken[best] = 1;
		out[k++] = (uint32_t)best;
		sweet_reclaim_clear(sim, c[best].g0, c[best].ngran);
		if (sweet_reclaim_fits(sim, need)) {
			/* prune: an earlier (older) victim whose range the fit does not need stays (its granules set again) */
			uint32_t kept = 0;
			for (uint32_t i = 0; i < k; i++) {
				uint32_t g0 = c[out[i]].g0, ng = c[out[i]].ngran;
				for (uint32_t g = g0; g < g0 + ng && g < SWEET_FB_POOL_NGRAN; g++) sim->bits[g >> 6] |= 1ull << (g & 63);
				if (i + 1 < k && sweet_reclaim_fits(sim, need)) continue;       /* not needed: dropped */
				sweet_reclaim_clear(sim, g0, ng);
				out[kept++] = out[i];
			}
			return kept;
		}
	}
	return 0;
}

/*
 * An evicted surface's OLD pool range (kept, with its descriptor, after the swap) may go back to the pool when: the grace has passed
 * since the eviction (or since the last re-arm), no display hold, GPU job hold or GPU import is left on the surface, no swap named it
 * since (a swap that resolved the old address just before the swap would show up here), and the old descriptor is held by nobody but
 * the eviction record (old_retains == 1: no IOMemoryMap of the old range is left in any task, no reader holds it).
 */
enum { SWEET_RECLAIM_WAIT = 0, SWEET_RECLAIM_REARM = 1, SWEET_RECLAIM_GIVE = 2 };

static inline int
sweet_reclaim_grace(uint32_t now, uint32_t armed_at, uint32_t grace, uint32_t shown_at_armed, uint32_t shown_at_now,
    uint32_t kd, uint32_t kg, uint32_t imports, uint32_t old_retains)
{
	if (shown_at_now != shown_at_armed) return SWEET_RECLAIM_REARM;
	if (kd != 0 || kg != 0 || imports != 0 || old_retains != 1) return SWEET_RECLAIM_WAIT;
	if (grace == 0) grace = 1;
	return now - armed_at >= grace ? SWEET_RECLAIM_GIVE : SWEET_RECLAIM_WAIT;
}
#endif /* SWEET_FB_POOL_RECLAIM_H */
