/*
 * 0417 (memfix-20261113): purge of idle VOLATILE IOSurfaces under memory pressure, one budget for surfaces + GPU private memory,
 * a per-process surface budget.  SPDX-License-Identifier: MIT
 *
 * Why (autocycle/runs/lag1, q10jgn, fresh boot + Camera + Photos, 5 min): wired 208,837 pages (3.2 GiB of 16 KiB) with avail ~8-11k
 * pages and 8-23k pageins / 2 s (file-backed pages thrashing = the display lags).  Every SweetSurface is a wired IOBufferMemoryDescriptor
 * (~1.8 GiB, budget 2048 MiB) and GPU private memory is 1388 MiB (budget 1536 MiB): 3584 MiB of budgets in a 3776 MiB window, so RAM
 * runs out before either refuses.  IOSurfaceSetPurgeable only recorded the state (Photos: 521 volatile surfaces, 312 MiB).
 *
 * Purge: a surface whose holders marked it VOLATILE and nobody uses (use counts 0, no display hold, no GPU job, no GPU import, GPU not
 * writing it, not on an IOMMU-programmed layer, not swapped / created for idle_s) gets, at the same user addresses (the 0406 in-place
 * kIOMapOverwrite remap), a view of one shared wired "sink" buffer instead of its own pages; its purgeable state becomes Empty and its
 * own wired buffer is freed after a grace period (sweet_reclaim_grace of 0406).  A later SetPurgeable(NonVolatile) allocates wired memory
 * again (remap back) and answers Empty, so the client redraws.
 *
 * Why a shared physical sink and not "pageable memory": getPhysicalSegment() on an unwired virtual descriptor panics ("IOGMD: not wired
 * for the IODMACommand", IOMemoryDescriptor.cpp) and the GPU import (Nord2GPU::importSurface), the display IOMMU map (n2io_map_locked) and
 * the framebuffer's H0 log all call it on s->memory without prepare().  A withPhysicalAddress view of the sink is always walkable,
 * costs no RAM per surface, and anything that still writes a purged surface (an app ignoring Empty, the GPU) only writes the sink.
 *
 * Pure: no IOKit; shared by SweetSurface.cpp and memfix-20261113/test/purge_host_test.c.
 */
#ifndef SWEET_SURFACE_PURGE_H
#define SWEET_SURFACE_PURGE_H
/* (uint*_t / bool come from the includer: the kernel's headers, or stdint.h + stdbool.h in the host test) */

/* kern.n2surf_purge: 0 off, 1 decide and log only, 2 purge (default) */
enum { SWEET_PURGE_OFF = 0, SWEET_PURGE_REPORT = 1, SWEET_PURGE_ON = 2 };

#define SWEET_PURGE_VOLATILE    1u      /* kIOSurfacePurgeableVolatile */

enum {
	SWEET_PURGE_OK = 0,
	SWEET_PURGE_NO_BUFFER,          /* not a wired buffer surface (pool, panel, already purged) */
	SWEET_PURGE_NO_KERNEL,          /* made by the kernel */
	SWEET_PURGE_NO_MODE,            /* kern.n2surfuse < 2: holds not counted, idleness cannot be proven */
	SWEET_PURGE_NO_SIZE,            /* bigger than the sink */
	SWEET_PURGE_NO_NONVOLATILE,     /* contents still wanted */
	SWEET_PURGE_NO_YOUNG,           /* created less than idle_s ago */
	SWEET_PURGE_NO_ACTIVE,          /* named in a swap less than idle_s ago */
	SWEET_PURGE_NO_USE,             /* IOSurfaceIsInUse (a use-count word != 0) */
	SWEET_PURGE_NO_DISPLAY,         /* a swap holds it */
	SWEET_PURGE_NO_GPU_JOB,         /* a GPU job lists it */
	SWEET_PURGE_NO_GPU_IMPORT,      /* a GPU import maps its pages */
	SWEET_PURGE_NO_GPU_BUSY,        /* a GPU job writing it has not finished */
	SWEET_PURGE_NO_IOMMU,           /* its IOVA is programmed in an overlay slot now */
	SWEET_PURGE_NREASONS
};

struct sweet_purge_cand {
	uint32_t size;                  /* allocSize */
	uint32_t born_at, shown_at;     /* uptime s; shown_at 0 = never in a swap */
	uint32_t purgeable;
	uint32_t w0, w1, kd, kg, imports;
	int32_t  creator;
	bool     buffer, gpu_busy, iommu_prog;
};

static inline uint32_t
sweet_purge_verdict(const struct sweet_purge_cand *c, uint32_t now, uint32_t idle_s, bool use_mode_all, uint64_t sink_bytes)
{
	if (!c->buffer || c->size == 0) return SWEET_PURGE_NO_BUFFER;
	if (c->creator <= 0) return SWEET_PURGE_NO_KERNEL;
	if (!use_mode_all) return SWEET_PURGE_NO_MODE;
	if ((uint64_t)c->size > sink_bytes) return SWEET_PURGE_NO_SIZE;
	if (c->purgeable != SWEET_PURGE_VOLATILE) return SWEET_PURGE_NO_NONVOLATILE;
	if (idle_s == 0) idle_s = 1;
	if (now - c->born_at < idle_s) return SWEET_PURGE_NO_YOUNG;
	if (c->shown_at != 0 && now - c->shown_at < idle_s) return SWEET_PURGE_NO_ACTIVE;
	if (c->w0 != 0 || c->w1 != 0) return SWEET_PURGE_NO_USE;
	if (c->kd != 0) return SWEET_PURGE_NO_DISPLAY;
	if (c->kg != 0) return SWEET_PURGE_NO_GPU_JOB;
	if (c->imports != 0) return SWEET_PURGE_NO_GPU_IMPORT;
	if (c->gpu_busy) return SWEET_PURGE_NO_GPU_BUSY;
	if (c->iommu_prog) return SWEET_PURGE_NO_IOMMU;
	return SWEET_PURGE_OK;
}

static inline uint32_t
sweet_purge_last_use(const struct sweet_purge_cand *c)
{
	return c->shown_at != 0 ? c->shown_at : c->born_at;
}

/*
 * Bytes a purge pass should free now (0 = no pressure).  Pressure = surface wired bytes over thr (back to thr - hyst), available pages
 * under avail_min (back to avail_min + hyst), surfaces + GPU private + need over the combined budget, plus need itself when any of these
 * holds (a creation that triggered the pass must fit afterwards).  Overflow-safe; limits of 0 = that rule off.
 */
static inline uint64_t
sweet_purge_goal(uint64_t surf_wired, uint64_t gpu_private, uint32_t avail_pages, uint32_t page_bytes, uint64_t thr, uint32_t avail_min,
    uint64_t combined, uint64_t hyst, uint64_t need)
{
	uint64_t goal = 0, g;
	if (thr != 0 && surf_wired + need > thr) {
		g = surf_wired + need - (thr > hyst ? thr - hyst : 0);
		if (g > goal) goal = g;
	}
	if (avail_min != 0 && avail_pages < avail_min) {
		g = (uint64_t)(avail_min - avail_pages) * page_bytes + hyst;
		if (g > goal) goal = g;
	}
	if (combined != 0 && surf_wired + gpu_private + need > combined) {
		g = surf_wired + gpu_private + need - combined + hyst;
		if (g > goal) goal = g;
	}
	if (goal != 0 && need > goal) goal = need;
	return goal;
}

/* kern.n2mem_budget: one budget for surface wired bytes + GPU private bytes.  0 = off. */
static inline bool
sweet_mem_combined_ok(uint64_t surf_wired, uint64_t gpu_private, uint64_t request, uint64_t limit)
{
	if (limit == 0) return true;
	return surf_wired <= limit && gpu_private <= limit - surf_wired && request <= limit - surf_wired - gpu_private;
}

/*
 * The GPU side of the combined budget (called from nord2_gpu_global_private_ok, under the GPU locks: cannot purge inline).  It may go
 * ahead when the request fits once the idle volatile surfaces seen by the last scan are purged (the caller kicks the purge thread);
 * idle_bytes is that estimate.  Returns 0 refuse, 1 fits now, 2 fits after the purge (kick it).
 */
static inline int
sweet_mem_gpu_decide(uint64_t surf_wired, uint64_t gpu_private, uint64_t request, uint64_t limit, uint64_t idle_bytes)
{
	if (sweet_mem_combined_ok(surf_wired, gpu_private, request, limit)) return 1;
	uint64_t after = surf_wired > idle_bytes ? surf_wired - idle_bytes : 0;
	return sweet_mem_combined_ok(after, gpu_private, request, limit) ? 2 : 0;
}

/* Per-process surface budget (bytes made by one pid).  0 = off. */
static inline bool
sweet_mem_proc_ok(uint64_t proc_bytes, uint64_t request, uint64_t limit)
{
	return limit == 0 || (proc_bytes <= limit && request <= limit - proc_bytes);
}

/* Per-process byte table (pid -> wired surface bytes it created).  Caller serialises.  A full table leaves a pid untracked. */
#define SWEET_MEM_PROCS 64
struct sweet_mem_proc { int32_t pid; uint64_t bytes; };

/* Index of pid's row (make: a new or emptied row for it), -1 = none. */
static inline int
sweet_mem_proc_find(const struct sweet_mem_proc *t, int32_t pid, bool make)
{
	int empty = -1;
	if (pid <= 0) return -1;
	for (int i = 0; i < SWEET_MEM_PROCS; i++) {
		if (t[i].pid == pid) return i;
		if (empty < 0 && (t[i].pid == 0 || t[i].bytes == 0)) empty = i;
	}
	return make ? empty : -1;
}

static inline void
sweet_mem_proc_add(struct sweet_mem_proc *t, int32_t pid, int64_t delta)
{
	int i = sweet_mem_proc_find(t, pid, delta > 0);
	if (i < 0) return;
	if (t[i].pid != pid) {
		t[i].pid = pid;
		t[i].bytes = 0;
	}
	if (delta < 0 && (uint64_t)(-delta) > t[i].bytes) t[i].bytes = 0;
	else t[i].bytes = (uint64_t)((int64_t)t[i].bytes + delta);
}

/*
 * LRU victims: indices of candidates with verdict OK, oldest last use first, until their sizes reach goal (or cap).  order[] is scratch
 * of n entries.  Returns the count; *freed = their bytes.  Selection sort over at most n (n <= a few thousand, called at most every 2 s).
 */
static inline uint32_t
sweet_purge_pick(const struct sweet_purge_cand *c, const uint32_t *verdict, uint32_t n, uint64_t goal, uint32_t *out, uint32_t cap,
    uint64_t *freed)
{
	uint32_t k = 0;
	uint64_t got = 0;
	uint32_t last_v = 0, last_i = 0;
	bool have_last = false;
	while (k < cap && got < goal) {
		int best = -1;
		for (uint32_t i = 0; i < n; i++) {
			if (verdict[i] != SWEET_PURGE_OK) continue;
			uint32_t v = sweet_purge_last_use(&c[i]);
			/* strictly after the previous pick in (last_use, index) order: no "taken" array needed */
			if (have_last && (v < last_v || (v == last_v && i <= last_i))) continue;
			if (best < 0 || v < sweet_purge_last_use(&c[best])) best = (int)i;
		}
		if (best < 0) break;
		out[k++] = (uint32_t)best;
		got += c[best].size;
		last_v = sweet_purge_last_use(&c[best]);
		last_i = (uint32_t)best;
		have_last = true;
	}
	*freed = got;
	return k;
}
/*
 * 0418 (memfix-20261113b): limits scale with the RAM window and nothing is refused while memory is available.  r5b (q10jgr, 5088 MiB
 * window): Photos crashed on refusals by 0417's static caps (combined 2240 MiB, per process 896 MiB) with avail = 156,673 pages free.
 * Now: pressure (purge idle volatile surfaces) = available pages under avail_high, surfaces over purge_thr, or surfaces + GPU private
 * over combined_purge; a refusal only when available pages stay under avail_floor (after a purge) or a hard cap is hit
 * (surf_cap 55 % of RAM, gpu_cap 40 %, proc_cap 60 % = runaway guard).  Ratios fitted to 0417's 3776 MiB numbers (2048, 1228, 2240)
 * and the coordinator's 5088 MiB marks (120k / 25k pages).
 */
struct sweet_mem_policy {
	uint64_t surf_cap, gpu_cap, proc_cap, purge_thr, combined_purge;
	uint32_t avail_high, avail_floor;
};

static inline void
sweet_mem_policy_for(uint64_t ram_bytes, uint32_t page_bytes, struct sweet_mem_policy *p)
{
	uint64_t pages = page_bytes ? ram_bytes / page_bytes : 0;
	p->surf_cap = ram_bytes / 100 * 55;
	p->gpu_cap = ram_bytes / 100 * 40;
	p->proc_cap = ram_bytes / 100 * 60;
	p->purge_thr = ram_bytes / 1000 * 325;
	p->combined_purge = ram_bytes / 1000 * 593;
	p->avail_high = (uint32_t)(pages * 37 / 100);
	p->avail_floor = (uint32_t)(pages * 77 / 1000);
	if (p->avail_floor < 8192) p->avail_floor = 8192;
	if (p->avail_high < p->avail_floor) p->avail_high = p->avail_floor;
}

/* GPU private allocation: 0 refuse, 1 go, 2 go and kick a purge.  static_limit = Nord2GPU's own global limit (kept as a floor of the cap). */
static inline int
sweet_mem_gpu_rule(const struct sweet_mem_policy *p, uint64_t total_private, uint64_t request, uint64_t static_limit, uint32_t avail,
    bool pressure)
{
	uint64_t cap = p->gpu_cap > static_limit ? p->gpu_cap : static_limit;
	if (!(total_private <= cap && request <= cap - total_private)) return 0;
	if (avail < p->avail_floor) return 0;
	return pressure ? 2 : 1;
}

/* Surface creation after the purge attempt: true = allowed (the hard surface cap is checked by the caller). */
static inline bool
sweet_mem_surface_rule(const struct sweet_mem_policy *p, uint64_t proc_bytes, uint64_t request, uint32_t avail)
{
	if (!sweet_mem_proc_ok(proc_bytes, request, p->proc_cap)) return false;
	return avail >= p->avail_floor;
}
#endif /* SWEET_SURFACE_PURGE_H */
