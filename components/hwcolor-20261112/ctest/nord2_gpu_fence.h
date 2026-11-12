/* 0395: per-surface GPU write fences for the display (Nord2GPU -> SweetFramebuffer), and the display-pool range the GPU import accepts.
 * SPDX-License-Identifier: MIT
 *
 * Sequence numbers are the GPU core's job ids: one counter for the whole device (all contexts), starting at 1 and strictly increasing.
 * A job "writes" a surface when its submit listed an import of that IOSurface with the handle's write_mask bit set (the same rule as
 * nord2_gpu_surface_busy).  A job "completed" when it left QUEUED/RUNNING/STOPPING (DONE, FAULTED, CANCELLED or RESET: a faulted job's
 * pixels are not trustworthy, but the GPU no longer writes them).
 *
 *   nord2_gpu_surface_write_seq(id)    seq of the newest job submitted that writes the surface, 0 if none is known.
 *   nord2_gpu_surface_write_pending(id) some unfinished job writes the surface.
 *   nord2_gpu_completed_seq()          every job with seq <= this value has completed (low-water mark; monotonic).
 *   nord2_gpu_surface_writes_done(id, s) no unfinished job with seq <= s writes the surface: the exact fence for a seq snapshot
 *                                      taken at SwapEnd (a newer job on the same surface does not hold an older swap back).
 *                                      completed_seq() >= s implies it, but one long job of another client can delay that one.
 *   nord2_gpu_surface_wait_idle(id, t) polls write_pending every <= 100 us for at most t us (clamped to 100 ms); true = idle.
 *                                      Sleeps (IODelay), never holds a lock while waiting; do not call with a spinlock held.
 * All functions take the GPU driver's IOLock briefly; they return 0/false/true(idle) while the GPU driver is not ready.
 * They do not include 0112 reservations (promised, not yet submitted frames): nord2_gpu_surface_busy() still reports those.
 * Jobs that reach buffers they did not list (legacy compute ABI, full handle lists) are not seen, as in nord2_gpu_surface_busy(). */
#ifndef NORD2_GPU_FENCE_H
#define NORD2_GPU_FENCE_H
#ifndef KERNEL
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
uint64_t nord2_gpu_surface_write_seq(uint32_t surface_id);
bool nord2_gpu_surface_write_pending(uint32_t surface_id);
uint64_t nord2_gpu_completed_seq(void);
bool nord2_gpu_surface_writes_done(uint32_t surface_id, uint64_t upto_seq);
bool nord2_gpu_surface_wait_idle(uint32_t surface_id, uint32_t timeout_us);
#ifdef __cplusplus
}
#endif

#ifdef NORD2_GPU_FENCE_IMPL
/* ---- pure logic, shared by Nord2GPU.cpp and the host test (no allocation, fixed table) ---- */

/* Display pool (Track A, sweet_fb_hw_pool.h): normal DRAM outside XNU's RAM, physically contiguous. */
#if defined(KERNEL) && defined(__has_include)
#if __has_include("sweet_fb_hw_pool.h")
#include "sweet_fb_hw_pool.h"
#endif
#endif
#ifdef SWEET_FB_HW_POOL_BASE
#define N2GF_POOL_BASE ((uint64_t)(SWEET_FB_HW_POOL_BASE))
#define N2GF_POOL_SIZE ((uint64_t)(SWEET_FB_HW_POOL_SIZE))
#else
#define N2GF_POOL_BASE 0xC0000000ull
#define N2GF_POOL_SIZE 0x10000000ull
#endif
/* One 4 KiB GPU page entirely inside the pool. */
static inline bool n2gf_pool_page(uint64_t pa)
{
        return !(pa & 4095u) && N2GF_POOL_SIZE >= 4096 && pa >= N2GF_POOL_BASE && pa - N2GF_POOL_BASE <= N2GF_POOL_SIZE - 4096;
}

#define N2GF_SLOTS 64u
#define N2GF_WAIT_STEP_US 100u
#define N2GF_WAIT_MAX_US 100000u

struct n2gf_slot { uint64_t seq; uint32_t sid; uint32_t writes; };   /* seq 0 = free slot */
struct n2gf_stats {
        uint64_t records, hits, fills, evict_done, evict_pending, miss_scans;
        uint64_t q_seq, q_pend, q_pend_true, q_done, q_done_false;
        uint64_t waits, wait_ok, wait_timeout, wait_sum_us; uint32_t wait_max_us;
        uint64_t pool_imports, pool_pages, pool_refused;
};
struct n2gf_table { struct n2gf_slot s[N2GF_SLOTS]; uint64_t completed; struct n2gf_stats st; };

/* View of the core's job records: `unfinished` gives the id of record i if it is queued/running/stopping;
 * `writes` says whether record i writes surface sid. */
struct n2gf_jobs {
        void *ctx;
        unsigned count;
        bool (*unfinished)(void *ctx, unsigned i, uint64_t *id);
        bool (*writes)(void *ctx, unsigned i, uint32_t sid);
};

/* Low-water mark: (smallest unfinished id) - 1, or the last id handed out when nothing is unfinished.  next_id is the core's
 * next_job_id (0 only after a wrap, which the core refuses to continue past). */
static inline uint64_t n2gf_low_water(struct n2gf_table *t, const struct n2gf_jobs *j, uint64_t next_id)
{
        uint64_t lo = next_id ? next_id - 1 : ~0ull;
        for (unsigned i = 0; i < j->count; i++) {
                uint64_t id;
                if (j->unfinished(j->ctx, i, &id) && id && id - 1 < lo) lo = id - 1;
        }
        if (lo > t->completed) t->completed = lo;
        return t->completed;
}

/* Any unfinished job with id <= upto writes sid?  *max_id (optional) gets the newest such id. */
static inline bool n2gf_pending(const struct n2gf_jobs *j, uint32_t sid, uint64_t upto, uint64_t *max_id)
{
        bool any = false; uint64_t m = 0;
        for (unsigned i = 0; i < j->count; i++) {
                uint64_t id;
                if (!j->unfinished(j->ctx, i, &id) || id > upto || !j->writes(j->ctx, i, sid)) continue;
                any = true; if (id > m) m = id;
        }
        if (max_id) *max_id = m;
        return any;
}

static inline struct n2gf_slot *n2gf_find(struct n2gf_table *t, uint32_t sid)
{
        for (unsigned i = 0; i < N2GF_SLOTS; i++) if (t->s[i].seq && t->s[i].sid == sid) return &t->s[i];
        return NULL;
}

/* Record that job `seq` writes surface sid.  Replacement when full: the slot with the oldest seq (counted as evict_pending
 * when that job may still run; write_seq then falls back to the job scan, so no pending write is ever lost). */
static inline void n2gf_record(struct n2gf_table *t, const struct n2gf_jobs *j, uint64_t next_id, uint32_t sid, uint64_t seq)
{
        if (!sid || !seq) return;
        t->st.records++;
        struct n2gf_slot *s = n2gf_find(t, sid);
        if (s) { t->st.hits++; if (seq > s->seq) s->seq = seq; s->writes++; return; }
        struct n2gf_slot *v = NULL;
        for (unsigned i = 0; i < N2GF_SLOTS && !v; i++) if (!t->s[i].seq) v = &t->s[i];
        if (v) t->st.fills++;
        else {
                v = &t->s[0];
                for (unsigned i = 1; i < N2GF_SLOTS; i++) if (t->s[i].seq < v->seq) v = &t->s[i];
                if (v->seq <= n2gf_low_water(t, j, next_id)) t->st.evict_done++; else t->st.evict_pending++;
        }
        v->sid = sid; v->seq = seq; v->writes = 1;
}

static inline uint64_t n2gf_write_seq(struct n2gf_table *t, const struct n2gf_jobs *j, uint32_t sid)
{
        t->st.q_seq++;
        if (!sid) return 0;
        struct n2gf_slot *s = n2gf_find(t, sid);
        if (s) return s->seq;
        uint64_t m = 0; t->st.miss_scans++;
        (void)n2gf_pending(j, sid, ~0ull, &m);    /* evicted entry or a surface written before tracking started */
        return m;
}

/* Bounded poll.  pending() is called without any lock held by the caller; sleep_us may block. */
struct n2gf_wait_ops {
        void *ctx;
        uint64_t (*now_us)(void *ctx);
        void (*sleep_us)(void *ctx, uint32_t us);
        bool (*pending)(void *ctx);
};
static inline bool n2gf_wait(const struct n2gf_wait_ops *o, uint32_t timeout_us, uint32_t *elapsed_out)
{
        if (timeout_us > N2GF_WAIT_MAX_US) timeout_us = N2GF_WAIT_MAX_US;
        uint64_t t0 = o->now_us(o->ctx), el = 0; bool idle;
        for (;;) {
                idle = !o->pending(o->ctx);
                el = o->now_us(o->ctx) - t0;
                if (idle || el >= timeout_us) break;
                uint64_t left = timeout_us - el;
                o->sleep_us(o->ctx, (uint32_t)(left < N2GF_WAIT_STEP_US ? left : N2GF_WAIT_STEP_US));
        }
        if (elapsed_out) *elapsed_out = (uint32_t)el;
        return idle;
}
static inline void n2gf_wait_account(struct n2gf_table *t, bool idle, uint32_t el)
{
        t->st.waits++; if (idle) t->st.wait_ok++; else t->st.wait_timeout++;
        t->st.wait_sum_us += el; if (el > t->st.wait_max_us) t->st.wait_max_us = el;
}
#endif /* NORD2_GPU_FENCE_IMPL */
#endif /* NORD2_GPU_FENCE_H */
