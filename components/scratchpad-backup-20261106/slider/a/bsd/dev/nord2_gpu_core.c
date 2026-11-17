/*
 * Nord 2 GPU core.  See nord2_gpu_core.h for the lifetime rules.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_core.h"

#include "nord2_gpu_compat.h"

#define JOB_HEADER_BYTES 128u
#define SPAN_512G (1ull << 39)
#define MAX_BO_PAGES 65536u     /* 256 MiB in 4 KiB pages */
_Static_assert(N2VA_MAX_EXTENTS > N2C_MAX_BOS, "a free gap can sit behind every buffer");
#define BO_GUARD 0x1000ull      /* unmapped page after every buffer */
#define JS_STATUS_BUS_FAULT 0x48u

static void note(struct n2c_core *c, const char *msg)
{
        if (c->ops.log)
                c->ops.log(c->ctx, msg);
}

static uint64_t now(struct n2c_core *c)
{
        return c->ops.now_ms(c->ctx);
}

uint64_t n2c_now_us(struct n2c_core *c)
{
        return c->ops.now_us ? c->ops.now_us(c->ctx) : c->ops.now_ms(c->ctx) * 1000ull;
}

static void timing_note(struct n2c_core *c, const struct n2c_job *j, bool done)
{
        struct n2c_timing *t = &c->timing;
        if (!t->jobs && !t->faulted)
                t->window_start_us = j->t_submit_us;
        if (!done) {
                t->faulted++;
                return;
        }
        uint64_t q = j->t_start_us >= j->t_submit_us ? j->t_start_us - j->t_submit_us : 0;
        uint64_t r = j->t_end_us >= j->t_start_us ? j->t_end_us - j->t_start_us : 0;
        uint64_t all = j->t_end_us >= j->t_submit_us ? j->t_end_us - j->t_submit_us : 0;
#define N2C_ACC(sum, max, v) do { t->sum += (v); if ((v) > t->max) t->max = (uint32_t)((v) > 0xffffffffull ? 0xffffffffull : (v)); } while (0)
        N2C_ACC(sum_queue_us, max_queue_us, q);
        N2C_ACC(sum_run_us, max_run_us, r);
        N2C_ACC(sum_total_us, max_total_us, all);
        N2C_ACC(sum_clean_us, max_clean_us, (uint64_t)j->clean_us);
        N2C_ACC(sum_inval_us, max_inval_us, (uint64_t)j->inval_us);
#undef N2C_ACC
        t->sum_clean_pages += j->clean_pages;
        t->sum_inval_pages += j->inval_pages;
        if (j->full_maint)
                t->full_jobs++;
        t->jobs++;
}

bool n2c_timing_take(struct n2c_core *c, struct n2c_timing *out,
                     uint64_t min_window_us)
{
        if (!c || !out || (!c->timing.jobs && !c->timing.faulted))
                return false;
        uint64_t t = n2c_now_us(c);
        if (t - c->timing.window_start_us < min_window_us)
                return false;
        *out = c->timing;
        memset(&c->timing, 0, sizeof(c->timing));
        out->window_start_us = t - out->window_start_us; /* window length */
        return true;
}

enum n2c_error n2c_job_get_times(struct n2c_core *c, unsigned idx, uint64_t id,
                             struct n2c_job_times *out)
{
        if (!c || !out || idx >= N2C_MAX_CTX || !id)
                return N2C_E_ARGS;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                const struct n2c_job *j = &c->jobs[i];
                if (j->state != N2C_JOB_FREE && j->id == id && j->ctx == idx) {
                        out->submit_us = j->t_submit_us;
                        out->start_us = j->t_start_us;
                        out->end_us = j->t_end_us;
                        out->clean_us = j->clean_us;
                        out->inval_us = j->inval_us;
                        return N2C_OK;
                }
        }
        return N2C_E_NO_JOB;
}

static struct n2c_ctx *ctx_at(struct n2c_core *c, unsigned idx)
{
        if (idx >= N2C_MAX_CTX)
                return NULL;
        struct n2c_ctx *x = &c->ctxs[idx];
        return x->state == N2C_CTX_FREE ? NULL : x;
}

static struct n2c_bo *bo_at(struct n2c_ctx *x, uint32_t handle)
{
        unsigned slot = handle & N2C_SLOT_MASK;
        struct n2c_bo *b = x->bos[slot];
        return (b && b->handle == handle && b->handle_live) ? b : NULL;
}

static bool terminal(uint8_t s)
{
        return s == N2C_JOB_DONE || s == N2C_JOB_FAULTED ||
               s == N2C_JOB_CANCELLED || s == N2C_JOB_RESET;
}

/* ------------------------------------------------------------------ */
/* Buffer teardown                                                     */

static void free_bo_memory(struct n2c_core *c, struct n2c_ctx *x,
                           struct n2c_bo *b)
{
        if (b->retained) {
                b->retained = false;
                if (x->retained)
                        x->retained--;
        }
        for (unsigned i = 0; i < c->nr_quarantine;) {
                if (c->quarantine[i] == b) {
                        c->quarantine[i] = c->quarantine[--c->nr_quarantine];
                        continue;
                }
                i++;
        }
        if (b->state == N2C_BO_QUARANTINED)
                c->stats.drained++;
        if (b->size)
                (void)n2va_free(&x->va, b->va, b->size + BO_GUARD);
        if (b->cookie && c->ops.free_owner)
                c->ops.free_owner(c->ctx, b->cookie);
        x->wired = x->wired >= b->charged ? x->wired - b->charged : 0;
        if (b->flags & N2C_BO_IMPORTED)
                x->imported = x->imported >= b->charged ? x->imported - b->charged : 0;
        c->stats.bos_live--;
        c->stats.bytes_live -= b->size;
        if (b->pages)
                c->ops.free_host(c->ctx, b->pages,
                                 b->nr_pages * sizeof(*b->pages));
        unsigned slot = b->handle & N2C_SLOT_MASK;
        if (x->bos[slot] == b)
                x->bos[slot] = NULL;
        c->ops.free_host(c->ctx, b, sizeof(*b));
}

static void quarantine(struct n2c_core *c, struct n2c_bo *b)
{
        if (b->state == N2C_BO_QUARANTINED)
                return;
        b->state = N2C_BO_QUARANTINED;
        c->stats.quarantined++;
        c->reset_pending = true;
        if (c->nr_quarantine < N2C_QUARANTINE)
                c->quarantine[c->nr_quarantine++] = b;
        /* ctxs[].bos[] is the authoritative list; reset also drains entries
         * beyond this bounded diagnostic list. */
}

/* Unmap only the installed prefix. Keep the full VA reservation until all
 * page-table removals and the buffer itself are safe to reuse. */
static void teardown(struct n2c_core *c, struct n2c_ctx *x, struct n2c_bo *b)
{
        if (c->state == N2C_CORE_BROKEN) {
                quarantine(c, b);
                return;
        }
        if (b->mapped_size) {
                if (!nord2_gpu_mmu_unmap(&x->mmu, b->va, b->mapped_size)) {
                        quarantine(c, b);
                        return;
                }
                b->mapped_size = 0;
        }
        do {
                if (!b->nr_tables)
                        b->nr_tables = nord2_gpu_mmu_detach_empty(&x->mmu,
                                b->va, b->size, b->tables, 8);
                /* Always invalidate the enclosing L0 span: an allocation
                 * failure may have published an empty intermediate table. */
                uint64_t lo = b->va & ~(SPAN_512G - 1);
                uint64_t hi = (b->va + b->size + SPAN_512G - 1) &
                              ~(SPAN_512G - 1);
                if (c->gpu_powered && x->as_live &&
                    n2hw_as_flush(c->hw, x->as, lo, hi - lo, true) != N2HW_OK) {
                        c->stats.flush_failures++;
                        note(c, "Nord2 GPU core: release flush not acknowledged; quarantined");
                        quarantine(c, b);
                        return;
                }
                nord2_gpu_mmu_free_detached(&x->mmu, b->tables, b->nr_tables);
                b->nr_tables = nord2_gpu_mmu_detach_empty(&x->mmu, b->va,
                                                        b->size, b->tables, 8);
        } while (b->nr_tables);
        free_bo_memory(c, x, b);
}

/* Raw JM chains may address any buffer in their own AS, including one
 * omitted from the submit list. Retain all released buffers until that
 * context is idle; AS isolation is still enforced between clients. */
/* 0185: a released buffer used to stay mapped until the context had NO job in flight.  backboardd renders continuously, so its buffers piled up (1024 handle
 * slots, hundreds of MiB wired) until the slots ran out: ALLOC then failed with N2C_E_NO_HANDLE (EIO in the shim, which retries for 10 s = the 10 s UI freeze),
 * and the freeze left the table fuller, so the next one came sooner.  A raw chain can address any buffer of its own AS, but only a job SUBMITTED BEFORE the release
 * can still be using it: the buffer is reclaimed as soon as every older job of the context has dropped its references. */
static uint64_t ctx_oldest_pending(const struct n2c_core *c, const struct n2c_ctx *x)
{
        uint64_t oldest = UINT64_MAX;
        unsigned idx = (unsigned)(x - c->ctxs);
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                const struct n2c_job *j = &c->jobs[i];
                if (j->state != N2C_JOB_FREE && j->ctx == idx && !j->released && j->id < oldest)
                        oldest = j->id;
        }
        return oldest;
}

static void reap_idle_buffers(struct n2c_core *c, struct n2c_ctx *x)
{
        if (!x->retained)
                return;                 /* 0187: nothing released is waiting: no scan of the handle table after every job */
        uint64_t oldest = x->jobs ? ctx_oldest_pending(c, x) : UINT64_MAX;
        for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                struct n2c_bo *b = x->bos[i];
                if (b && !b->refs && b->state == N2C_BO_MAPPED && (!x->jobs || (b->rel_id && oldest >= b->rel_id)))
                        teardown(c, x, b);
        }
}

static void bo_put(struct n2c_core *c, struct n2c_ctx *x, struct n2c_bo *b)
{
        if (!b->refs)
                return;
        if (--b->refs == 0 && b->state == N2C_BO_MAPPED &&
            (!x->jobs || (b->rel_id && ctx_oldest_pending(c, x) >= b->rel_id)))
                teardown(c, x, b);
}

/* ------------------------------------------------------------------ */
/* Init, start, stop                                                  */

bool n2c_init(struct n2c_core *c, const struct n2c_ops *ops, void *ctx,
              struct n2hw *hw)
{
        if (!c || !ops || !hw || !ops->alloc_table || !ops->free_table ||
            !ops->flush_table || !ops->alloc_host || !ops->free_host ||
            !ops->sync || !ops->now_ms)
                return false;
        memset(c, 0, sizeof(*c));
        c->ops = *ops;
        c->ctx = ctx;
        c->hw = hw;
        c->next_job_id = 1;
        c->next_handle = 1;
        c->state = N2C_CORE_OFF;
        return true;
}

bool n2c_set_wa_payload(struct n2c_core *c, const uint8_t *bytes, size_t size)
{
        if (!c || c->state != N2C_CORE_OFF || c->gpu_powered ||
            c->wa_cpu || !bytes || size != 384) return false;
        c->wa_payload = bytes; c->wa_size = size;
        return true;
}

/* Only used before publishing AS0 or after acknowledged AS disable. */
static void wa_free(struct n2c_core *c)
{
        if (c->wa_as_live) return;
        if (c->wa_mmu.valid) nord2_gpu_mmu_fini(&c->wa_mmu);
        if (c->wa_cpu) c->ops.free_table(c->ctx, c->wa_pa, c->wa_cpu);
        c->wa_cpu = NULL; c->wa_pa = 0;
}

static enum n2c_error wa_start(struct n2c_core *c)
{
        if (!c->hw->has_issue_ttrx_3485) return N2C_OK;
        if (c->hw->wa_complete) return N2C_OK;
        if (!c->wa_payload || c->wa_size != 384 || c->ctxs[0].state != N2C_CTX_FREE)
                return N2C_E_STATE;
        if (!c->wa_cpu) {
                const struct nord2_gpu_mmu_ops ops = {
                        c->ops.alloc_table, c->ops.free_table, c->ops.flush_table,
                        c->ops.alloc_host, c->ops.free_host, c->ops.log,
                };
                if (!nord2_gpu_mmu_init(&c->wa_mmu, &ops, c->ctx, 4))
                        return N2C_E_NOMEM;
                if (!c->ops.alloc_table(c->ctx, &c->wa_pa, &c->wa_cpu)) {
                        wa_free(c); return N2C_E_NOMEM;
                }
                memset(c->wa_cpu, 0, NORD2_GPU_PAGE_SIZE);
                memcpy(c->wa_cpu, c->wa_payload, c->wa_size);
                c->ops.sync(c->ctx, c->wa_pa, c->wa_cpu, NORD2_GPU_PAGE_SIZE, true);
                /* Android map_flags=6: CPU write + GPU read, NX.
                 * Descriptor writeback disabled by the special JS config. */
                if (!nord2_gpu_mmu_map(&c->wa_mmu, 0x100000000ull, c->wa_pa,
                                       NORD2_GPU_PAGE_SIZE, NORD2_GPU_MEMATTR_IMPL_DEF,
                                       false, false)) {
                        wa_free(c); return N2C_E_MAP;
                }
        }
        c->wa_as_live = true; /* timeout may follow a committed UPDATE */
        if (n2hw_as_configure(c->hw, 0, c->wa_mmu.root_pa) != N2HW_OK ||
            n2hw_dummy_job_wa(c->hw) != N2HW_OK)
                return N2C_E_HW; /* keep all memory pinned; caller marks BROKEN */
        return N2C_OK;
}

enum n2c_error n2c_start(struct n2c_core *c)
{
        if (!c || c->state == N2C_CORE_BROKEN)
                return N2C_E_BROKEN;
        if (!c->hw->identified || !c->hw->slots_valid || !c->hw->l2_on)
                return N2C_E_STATE;
        c->gpu_powered = true;
        enum n2c_error we = wa_start(c);
        if (we != N2C_OK || n2hw_power_on_cores(c->hw, c->hw->id.shader_present) != N2HW_OK) {
                c->state = N2C_CORE_BROKEN;
                c->reset_pending = true;
                note(c, "Nord2 GPU core: startup WA/cores failed; memory pinned");
                return we != N2C_OK ? we : N2C_E_HW;
        }
        for (unsigned i = 0; i < N2C_MAX_CTX; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                if (x->state != N2C_CTX_OPEN)
                        continue;
                x->as_live = true; /* UPDATE may commit even on timeout */
                if (n2hw_as_configure(c->hw, x->as, x->mmu.root_pa) != N2HW_OK) {
                        c->state = N2C_CORE_BROKEN;
                        c->reset_pending = true;
                        return N2C_E_HW;
                }
        }
        c->state = N2C_CORE_READY;
        return N2C_OK;
}

enum n2c_error n2c_stop(struct n2c_core *c)
{
        if (!c)
                return N2C_E_ARGS;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                uint8_t s = c->jobs[i].state;
                if (s == N2C_JOB_QUEUED || s == N2C_JOB_RUNNING ||
                    s == N2C_JOB_STOPPING)
                        return N2C_E_BUSY;
        }
        if (c->state == N2C_CORE_BROKEN)
                return N2C_E_BROKEN;
        if (c->reset_pending)
                return N2C_E_BUSY;
        /* Power has NOT been removed yet. Obtain a hardware acknowledgement
         * for every live AS before allowing the board to power down. */
        for (unsigned i = 0; i < N2C_MAX_CTX; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                if (x->as_live && n2hw_as_disable(c->hw, x->as) != N2HW_OK) {
                        c->reset_pending = true;
                        return N2C_E_HW;
                }
                x->as_live = false;
        }
        if (c->wa_as_live && n2hw_as_disable(c->hw, 0) != N2HW_OK) {
                c->reset_pending = true;
                return N2C_E_HW;
        }
        c->wa_as_live = false;
        wa_free(c);
        c->gpu_powered = false;
        if (c->state != N2C_CORE_BROKEN)
                c->state = N2C_CORE_OFF;
        return N2C_OK;
}

/* ------------------------------------------------------------------ */
/* Contexts                                                            */

enum n2c_error n2c_ctx_open(struct n2c_core *c, uint64_t owner,
                            uint64_t wired_limit, bool kernel,
                            unsigned *ctx_out)
{
        if (!c || !ctx_out || !wired_limit)
                return N2C_E_ARGS;
        if (c->state == N2C_CORE_BROKEN)
                return N2C_E_BROKEN;
        /* AS0 belongs to the workaround for the complete powered lifetime. */
        if (kernel && (c->hw->has_issue_ttrx_3485 || c->wa_payload))
                return N2C_E_STATE;
        unsigned first = kernel ? 0 : 1, last = kernel ? 1 : N2C_MAX_CTX;
        for (unsigned i = first; i < last; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                if (x->state != N2C_CTX_FREE)
                        continue;
                if (c->hw->identified && !(c->hw->id.as_present & (1u << i)))
                        continue;
                uint32_t gen = x->generation;
                memset(x, 0, sizeof(*x));
                x->generation = gen + 1;
                const struct nord2_gpu_mmu_ops mops = {
                        c->ops.alloc_table, c->ops.free_table,
                        c->ops.flush_table, c->ops.alloc_host,
                        c->ops.free_host, c->ops.log,
                };
                if (!nord2_gpu_mmu_init(&x->mmu, &mops, c->ctx,
                                        N2C_MAX_TABLES))
                        return N2C_E_NOMEM;
                if (!n2va_init(&x->va, N2C_VA_BASE, N2C_VA_LIMIT)) {
                        nord2_gpu_mmu_fini(&x->mmu);
                        return N2C_E_VA;
                }
                x->as = (uint8_t)i;
                x->owner = owner;
                x->wired_limit = wired_limit;
                x->state = N2C_CTX_OPEN;
                if (c->state == N2C_CORE_READY && c->gpu_powered) {
                        x->as_live = true;
                        if (n2hw_as_configure(c->hw, x->as, x->mmu.root_pa) !=
                            N2HW_OK) {
                                if (n2hw_as_disable(c->hw, x->as) == N2HW_OK) {
                                        x->as_live = false;
                                        nord2_gpu_mmu_fini(&x->mmu);
                                        x->state = N2C_CTX_FREE;
                                } else {
                                        x->state = N2C_CTX_ZOMBIE;
                                        c->reset_pending = true;
                                }
                                return N2C_E_HW;
                        }
                }
                *ctx_out = i;
                return N2C_OK;
        }
        return N2C_E_NO_CTX;
}

static void job_release(struct n2c_core *c, struct n2c_job *j)
{
        if (j->released)
                return;
        struct n2c_ctx *x = &c->ctxs[j->ctx];
        for (unsigned i = 0; i < j->nr_bos; i++)
                bo_put(c, x, j->bos[i]);
        j->released = true;
        if (x->jobs)
                x->jobs--;
        reap_idle_buffers(c, x);
}

/* CPU cache maintenance of one buffer, page by page.  Uncached CPU mappings
 * need none.  to_device: clean before the GPU reads; otherwise invalidate
 * after the GPU wrote. */
static void bo_cache_sync(struct n2c_core *c, struct n2c_bo *b, bool to_device)
{
        if (!b || (b->flags & N2C_BO_CPU_UNCACHED))
                return;
        for (unsigned p = 0; p < b->nr_pages; p++)
                c->ops.sync(c->ctx, b->pages[p].pa, b->pages[p].cpu,
                            NORD2_GPU_PAGE_SIZE, to_device);
        if (to_device)
                c->stats.cache_clean_pages += b->nr_pages;
        else
                c->stats.cache_inval_pages += b->nr_pages;
}

static void job_finish(struct n2c_core *c, struct n2c_job *j, uint8_t state)
{
        j->state = state;
        if (state == N2C_JOB_DONE) {
                /* GPU writes reached memory at the END_FLUSH_CLEAN; drop
                 * stale CPU lines before the client reads.  A GPU write can
                 * only land in a buffer mapped GPU_WRITE.  A job that listed
                 * its buffers only reaches those; any other job (legacy
                 * ABI, a full list, the rollback valve) invalidates every
                 * GPU-writable buffer of the context. */
                j->t_end_us = n2c_now_us(c);
                uint64_t before = c->stats.cache_inval_pages;
                if (j->full_maint) {
                        for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                                struct n2c_bo *b = c->ctxs[j->ctx].bos[i];
                                if (!b || !(b->flags & N2C_BO_GPU_WRITE))
                                        continue;
                                if (j->full_hard || c->track_level == N2C_TRACK_LEGACY || (b->cpu_visible && !b->explicit_track)) {
                                        bo_cache_sync(c, b, false);
                                        b->gpu_written = true;
                                        b->gpu_dirty = false;
                                } else if (!b->cpu_visible) {
                                        b->gpu_written = true;      /* no CPU alias: no CPU line can be stale */
                                        if (!(b->flags & N2C_BO_CPU_UNCACHED))
                                                c->stats.cache_skip_inval_pages += b->nr_pages;
                                } else {
                                        b->gpu_written = true;
                                        b->gpu_dirty = true;        /* invalidated by the CPU's acquire */
                                        c->stats.cache_deferred_pages += b->nr_pages;
                                }
                        }
                } else {
                        for (unsigned i = 0; i < j->nr_bos; i++) {
                                struct n2c_bo *b = j->bos[i];
                                if (!(b->flags & N2C_BO_GPU_WRITE))
                                        continue;
                                if (c->wmask_inval && j->write_mask && !((j->write_mask >> i) & 1u)) {
                                        /* The job declared what it writes and this buffer is not in it: no CPU line of it can be stale because of this job. */
                                        c->stats.cache_skip_wmask_pages += b->nr_pages;
                                        continue;
                                }
                                b->gpu_written = true;
                                if (c->track_level == N2C_TRACK_LEGACY || (b->cpu_visible && !b->explicit_track)) {
                                        bo_cache_sync(c, b, false);
                                } else if (!b->cpu_visible) {
                                        /* No CPU alias exists, so no CPU line can be stale. */
                                        if (!(b->flags & N2C_BO_CPU_UNCACHED))
                                                c->stats.cache_skip_inval_pages += b->nr_pages;
                                } else {
                                        b->gpu_dirty = true;       /* invalidated by the CPU's acquire */
                                        c->stats.cache_deferred_pages += b->nr_pages;
                                }
                        }
                }
                j->inval_pages = (uint32_t)(c->stats.cache_inval_pages - before);
                j->inval_us = (uint32_t)(n2c_now_us(c) - j->t_end_us);
                timing_note(c, j, true);
                c->stats.completed++;
        } else if (state == N2C_JOB_FAULTED) {
                timing_note(c, j, false);
                c->stats.faulted++;
        } else if (state == N2C_JOB_CANCELLED) {
                c->stats.cancelled++;
        }
        job_release(c, j);
}

static bool ctx_has_active(struct n2c_core *c, unsigned idx)
{
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                struct n2c_job *j = &c->jobs[i];
                if (j->ctx == idx && (j->state == N2C_JOB_RUNNING ||
                                      j->state == N2C_JOB_STOPPING))
                        return true;
        }
        return false;
}

static void stop_job(struct n2c_core *c, struct n2c_job *j)
{
        if (j->state != N2C_JOB_RUNNING)
                return;
        n2hw_js_stop(c->hw, j->slot, true);
        j->state = N2C_JOB_STOPPING;
        j->stop_ms = now(c);
}

static void free_ctx_memory(struct n2c_core *c, struct n2c_ctx *x)
{
        for (unsigned s = 0; s < N2C_MAX_BOS; s++) {
                struct n2c_bo *b = x->bos[s];
                if (b)
                        free_bo_memory(c, x, b);
        }
        nord2_gpu_mmu_fini(&x->mmu);
}

/* ------------------------------------------------------------------ */
/* Surface reservations (0112)                                         */

static bool ctx_jobs_pending_upto(struct n2c_core *c, unsigned idx, uint64_t seq)
{
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                const struct n2c_job *j = &c->jobs[i];
                if (j->state != N2C_JOB_FREE && j->ctx == idx && j->id <= seq && !terminal(j->state))
                        return true;
        }
        return false;
}

/* Drop what no longer counts: unretired reservations older than the expiry, retired ones whose jobs are all terminal. */
static void resv_sweep(struct n2c_core *c)
{
        uint64_t t = now(c);
        for (unsigned i = 0; i < N2C_RESV_SLOTS; i++) {
                struct n2c_resv *r = &c->resv[i];
                if (!r->id)
                        continue;
                if (!r->seq) {
                        if (t >= r->t_ms && t - r->t_ms >= N2C_RESV_EXPIRE_MS) {
                                c->stats.resv_expired++;
                                memset(r, 0, sizeof(*r));
                        }
                } else if (!ctx_jobs_pending_upto(c, r->ctx, r->seq)) {
                        memset(r, 0, sizeof(*r));
                }
        }
}

static void resv_drop_ctx(struct n2c_core *c, unsigned idx)
{
        for (unsigned i = 0; i < N2C_RESV_SLOTS; i++) {
                struct n2c_resv *r = &c->resv[i];
                if (r->id && r->ctx == idx) {
                        c->stats.resv_cancels++;
                        memset(r, 0, sizeof(*r));
                }
        }
}

void n2c_set_resv_enabled(struct n2c_core *c, bool on)
{
        if (c)
                c->resv_off = !on;
}

enum n2c_error n2c_resv_create(struct n2c_core *c, unsigned idx, const uint32_t *handles, unsigned n, uint32_t *id)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        if (c->resv_off)
                return N2C_E_STATE;
        if (!id || n > N2C_RESV_MAX || (n && !handles))
                return N2C_E_ARGS;
        *id = 0;
        if (!n)
                return N2C_OK;                  /* the availability probe */
        resv_sweep(c);
        unsigned live = 0, free_slot = N2C_RESV_SLOTS;
        for (unsigned i = 0; i < N2C_RESV_SLOTS; i++) {
                if (!c->resv[i].id) {
                        if (free_slot == N2C_RESV_SLOTS)
                                free_slot = i;
                } else if (c->resv[i].ctx == idx) {
                        live++;
                }
        }
        const void *tok[N2C_RESV_MAX];
        for (unsigned i = 0; i < n; i++) {
                struct n2c_bo *b = bo_at(x, handles[i]);
                if (!b || b->state != N2C_BO_MAPPED || !(b->flags & N2C_BO_IMPORTED))
                        return N2C_E_ARGS;
                tok[i] = c->ops.resv_token ? c->ops.resv_token(c->ctx, b->cookie) : b->cookie;
                if (!tok[i])
                        return N2C_E_ARGS;
        }
        if (live >= N2C_RESV_PER_CTX || free_slot == N2C_RESV_SLOTS)
                return N2C_E_BUSY;
        struct n2c_resv *r = &c->resv[free_slot];
        memset(r, 0, sizeof(*r));
        uint32_t nid = ++c->resv_next[idx];
        if (!nid)
                nid = ++c->resv_next[idx];     /* never 0 */
        r->id = nid;
        r->ctx = (uint8_t)idx;
        r->n = (uint8_t)n;
        r->t_ms = now(c);
        for (unsigned i = 0; i < n; i++)
                r->tok[i] = tok[i];
        c->stats.resv_reserves++;
        *id = nid;
        return N2C_OK;
}

enum n2c_error n2c_resv_retire(struct n2c_core *c, unsigned idx, uint32_t id, uint64_t seq)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        if (c->resv_off)
                return N2C_E_STATE;
        if (!id)
                return N2C_OK;
        resv_sweep(c);
        struct n2c_resv *r = NULL;
        for (unsigned i = 0; i < N2C_RESV_SLOTS; i++)
                if (c->resv[i].id == id && c->resv[i].ctx == idx) {
                        r = &c->resv[i];
                        break;
                }
        if (!r)
                return N2C_E_NO_JOB;
        if (!seq) {
                c->stats.resv_cancels++;
                memset(r, 0, sizeof(*r));
                return N2C_OK;
        }
        if (seq >= c->next_job_id)
                return N2C_E_ARGS;              /* must name an issued job: nothing would ever release it early */
        if (r->seq)
                return N2C_OK;                  /* retiring twice keeps the first seq */
        r->seq = seq;
        c->stats.resv_retires++;
        resv_sweep(c);
        return N2C_OK;
}

bool n2c_resv_busy(struct n2c_core *c, const void *token)
{
        if (!c || !token)
                return false;
        resv_sweep(c);
        for (unsigned i = 0; i < N2C_RESV_SLOTS; i++) {
                const struct n2c_resv *r = &c->resv[i];
                if (!r->id)
                        continue;
                for (unsigned k = 0; k < r->n; k++)
                        if (r->tok[k] == token)
                                return true;
        }
        return false;
}

enum n2c_error n2c_ctx_close(struct n2c_core *c, unsigned idx)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        if (x->state == N2C_CTX_ZOMBIE)
                return N2C_E_BROKEN;
        resv_drop_ctx(c, idx);   /* a client that is gone (or closing) reserves nothing */
        /* 1. Jobs: cancel queued, stop running. */
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                struct n2c_job *j = &c->jobs[i];
                if (j->state == N2C_JOB_FREE || j->ctx != idx)
                        continue;
                if (j->state == N2C_JOB_QUEUED)
                        job_finish(c, j, N2C_JOB_CANCELLED);
                else if (j->state == N2C_JOB_RUNNING)
                        stop_job(c, j);
        }
        n2c_poll(c);
        if (ctx_has_active(c, idx)) {
                /* Caller retries after a short sleep; past the stop bound
                 * n2c_poll escalates to a reset by itself. */
                if (c->state != N2C_CORE_BROKEN)
                        return N2C_E_BUSY;
                x->state = N2C_CTX_ZOMBIE;
                return N2C_E_BROKEN;
        }
        /* 2. AS: drain and disable while still powered. */
        if (c->gpu_powered && x->as_live) {
                if (n2hw_as_disable(c->hw, x->as) != N2HW_OK) {
                        c->reset_pending = true;
                        if (n2c_reset(c) != N2C_OK) {
                                x->state = N2C_CTX_ZOMBIE;
                                return N2C_E_BROKEN;
                        }
                        /* The reset reprogrammed the AS; drain again. */
                        if (n2hw_as_disable(c->hw, x->as) != N2HW_OK) {
                                x->state = N2C_CTX_ZOMBIE;
                                return N2C_E_BROKEN;
                        }
                }
                x->as_live = false;
        }
        if (c->state == N2C_CORE_BROKEN && c->gpu_powered) {
                x->state = N2C_CTX_ZOMBIE;
                return N2C_E_BROKEN;
        }
        /* 3. Records and memory. */
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                struct n2c_job *j = &c->jobs[i];
                if (j->state != N2C_JOB_FREE && j->ctx == idx)
                        memset(j, 0, sizeof(*j));
        }
        free_ctx_memory(c, x);
        uint32_t gen = x->generation;
        memset(x, 0, sizeof(*x));
        x->generation = gen;
        return N2C_OK;
}

/* ------------------------------------------------------------------ */
/* Buffers                                                             */

enum n2c_error n2c_bo_map(struct n2c_core *c, unsigned idx,
                          const struct n2c_page *pages, unsigned nr_pages,
                          uint32_t flags, void *cookie, uint64_t charged,
                          uint32_t *handle_out, uint64_t *va_out,
                          bool *adopted_out)
{
        if (!adopted_out)
                return N2C_E_ARGS;
        *adopted_out = false;
        if (handle_out) *handle_out = 0;
        if (va_out) *va_out = 0;
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        if (x->state != N2C_CTX_OPEN || c->state == N2C_CORE_BROKEN ||
            c->reset_pending)
                return N2C_E_STATE;
        if (!pages || !nr_pages || nr_pages > MAX_BO_PAGES || !handle_out ||
            !va_out || (flags & ~0x3fu) || ((flags & N2C_BO_GPU_ONLY) && (flags & (N2C_BO_IMPORTED | N2C_BO_CPU_UNCACHED))) ||
            !(flags & (N2C_BO_GPU_READ | N2C_BO_GPU_WRITE)))
                return N2C_E_ARGS;
        for (unsigned i = 0; i < nr_pages; i++) {
                if (!pages[i].pa || (pages[i].pa & 0xfffu) ||
                    pages[i].pa >= (1ull << NORD2_GPU_PA_BITS) ||
                    (!(flags & N2C_BO_CPU_UNCACHED) && !pages[i].cpu))
                        return N2C_E_ARGS;
        }
        if (charged < (uint64_t)nr_pages * NORD2_GPU_PAGE_SIZE)
                return N2C_E_ARGS;
        if (c->next_handle > N2C_HANDLE_SERIAL_MAX)
                return N2C_E_NO_HANDLE; /* never alias a stale handle */
        if (charged > x->wired_limit || x->wired > x->wired_limit - charged)
                return N2C_E_QUOTA;
        unsigned slot = N2C_MAX_BOS;
        for (unsigned s = 0; s < N2C_MAX_BOS; s++) {
                if (!x->bos[s]) {
                        slot = s;
                        break;
                }
        }
        if (slot == N2C_MAX_BOS)
                return N2C_E_NO_HANDLE;
        uint64_t size = (uint64_t)nr_pages * NORD2_GPU_PAGE_SIZE;
        uint64_t va = 0;
        uint64_t align = size >= (2ull << 20) ? (2ull << 20) : 0x10000ull;
        if (!n2va_alloc(&x->va, size + BO_GUARD, align, &va))
                return N2C_E_VA;
        struct n2c_bo *b = NULL;
        if (!c->ops.alloc_host(c->ctx, sizeof(*b), (void **)&b) || !b) {
                (void)n2va_free(&x->va, va, size + BO_GUARD);
                return N2C_E_NOMEM;
        }
        memset(b, 0, sizeof(*b));
        if (!c->ops.alloc_host(c->ctx, nr_pages * sizeof(*b->pages),
                               (void **)&b->pages) || !b->pages) {
                c->ops.free_host(c->ctx, b, sizeof(*b));
                (void)n2va_free(&x->va, va, size + BO_GUARD);
                return N2C_E_NOMEM;
        }
        memcpy(b->pages, pages, nr_pages * sizeof(*b->pages));
        b->nr_pages = nr_pages;
        b->va = va;
        b->size = size;
        b->flags = flags;
        b->ctx = (uint8_t)idx;
        b->charged = charged;
        b->cookie = cookie;
        *adopted_out = true;
        b->handle = (c->next_handle++ << N2C_SLOT_BITS) | slot;
        bool exec = (flags & N2C_BO_GPU_EXEC) != 0;
        bool write = (flags & N2C_BO_GPU_WRITE) != 0;
        unsigned mapped = 0;
        /* Android r32p1 JIT/tiler allocations request COHERENT_LOCAL:
         * default cached attribute + SHARE_INNER. CPU coherency NONE
         * does not disable GPU-internal sharing between shader/tiler
         * units. Apply the same policy to all ordinary user BOs; CPU
         * cache maintenance and permissions remain necessary.
         * The pages go down in one call (one walk per level-3 table and one flush): a page per call scanned the table
         * registry several times for every 4 KiB. */
        {
                uint64_t *pas = NULL;
                if (nr_pages > 1 &&
                    c->ops.alloc_host(c->ctx, nr_pages * sizeof(*pas), (void **)&pas) && pas) {
                        uint64_t done = 0;
                        for (unsigned i = 0; i < nr_pages; i++)
                                pas[i] = pages[i].pa;
                        (void)nord2_gpu_mmu_map_pages(&x->mmu, va, pas, nr_pages,
                                                      NORD2_GPU_MEMATTR_IMPL_DEF,
                                                      NORD2_GPU_SHARE_INNER, write, exec, &done);
                        mapped = (unsigned)done;
                        c->ops.free_host(c->ctx, pas, nr_pages * sizeof(*pas));
                } else {
                        if (pas)
                                c->ops.free_host(c->ctx, pas, nr_pages * sizeof(*pas));
                        for (; mapped < nr_pages; mapped++) {
                                if (!nord2_gpu_mmu_map_shared(&x->mmu, va + (uint64_t)mapped * 4096u,
                                                       pages[mapped].pa, 4096,
                                                       NORD2_GPU_MEMATTR_IMPL_DEF,
                                                       NORD2_GPU_SHARE_INNER, write,
                                                       exec))
                                        break;
                        }
                }
        }
        x->bos[slot] = b;
        c->stats.bos_live++;
        c->stats.bytes_live += size;
        x->wired += charged;
        if (flags & N2C_BO_IMPORTED)
                x->imported += charged;
        b->refs = 1;
        b->handle_live = true;
        b->cpu_dirty = true;                        /* the kernel zero fill / initial CPU writes */
        b->cpu_visible = (flags & N2C_BO_IMPORTED) != 0;
        b->state = N2C_BO_MAPPED;
        b->mapped_size = (uint64_t)mapped * NORD2_GPU_PAGE_SIZE;
        if (mapped != nr_pages) {
                b->handle_live = false;
                b->refs = 0;
                teardown(c, x, b);
                return N2C_E_MAP;
        }
        if (c->gpu_powered && x->as_live &&
            n2hw_as_flush(c->hw, x->as, va, size, false) != N2HW_OK) {
                c->stats.flush_failures++;
                b->handle_live = false;
                b->refs = 0;
                /* Retain both mapping and pages until a verified reset. */
                quarantine(c, b);
                return N2C_E_HW;
        }
        *handle_out = b->handle;
        *va_out = va;
        return N2C_OK;
}

enum n2c_error n2c_bo_release(struct n2c_core *c, unsigned idx,
                              uint32_t handle)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        struct n2c_bo *b = bo_at(x, handle);
        if (!b)
                return N2C_E_NO_HANDLE;
        b->handle_live = false;
        if (!b->rel_id)
                b->rel_id = c->next_job_id;
        bool deferred = b->refs > 1 || (x->jobs && ctx_oldest_pending(c, x) < b->rel_id);
        if (deferred && !b->retained) {
                b->retained = true;
                x->retained++;
        }
        bo_put(c, x, b);
        return deferred ? N2C_E_BUSY : N2C_OK;
}

const struct n2c_bo *n2c_bo_lookup(const struct n2c_core *c, unsigned idx,
                                   uint32_t handle)
{
        if (!c || idx >= N2C_MAX_CTX || c->ctxs[idx].state == N2C_CTX_FREE)
                return NULL;
        return bo_at((struct n2c_ctx *)&c->ctxs[idx], handle);
}

enum n2c_error n2c_bo_sync(struct n2c_core *c, unsigned idx, uint32_t handle,
                           bool to_device)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        struct n2c_bo *b = bo_at(x, handle);
        if (!b)
                return N2C_E_NO_HANDLE;
        if (b->state != N2C_BO_MAPPED || c->state == N2C_CORE_BROKEN ||
            c->reset_pending)
                return N2C_E_STATE;
        if (x->jobs)
                return N2C_E_BUSY;
        if (!(b->flags & N2C_BO_CPU_UNCACHED))
                for (unsigned p = 0; p < b->nr_pages; p++)
                        c->ops.sync(c->ctx, b->pages[p].pa, b->pages[p].cpu,
                                    NORD2_GPU_PAGE_SIZE, to_device);
        return N2C_OK;
}

/* ------------------------------------------------------------------ */
/* CPU cache tracking                                                  */

bool n2c_set_wmask_inval(struct n2c_core *c, bool on)
{
        if (!c || c->stats.bos_live)
                return false;
        c->wmask_inval = on;
        return true;
}

bool n2c_set_track_level(struct n2c_core *c, unsigned level)
{
        if (!c || level > N2C_TRACK_EXPLICIT || c->stats.bos_live)
                return false;
        c->track_level = (uint8_t)level;
        return true;
}

static struct n2c_bo *tracked_bo(struct n2c_core *c, unsigned idx, uint32_t handle,
                                 enum n2c_error *e)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x) {
                *e = N2C_E_NO_CTX;
                return NULL;
        }
        struct n2c_bo *b = bo_at(x, handle);
        if (!b || b->state != N2C_BO_MAPPED) {
                *e = N2C_E_NO_HANDLE;
                return NULL;
        }
        *e = N2C_OK;
        return b;
}

enum n2c_error n2c_bo_cpu_visible(struct n2c_core *c, unsigned idx, uint32_t handle)
{
        enum n2c_error e;
        struct n2c_bo *b = tracked_bo(c, idx, handle, &e);
        if (!b)
                return e;
        if (b->flags & N2C_BO_GPU_ONLY)
                return N2C_E_STATE;                 /* promised never to be CPU-visible */
        if (b->cpu_visible)
                return N2C_OK;
        b->cpu_visible = true;
        b->cpu_dirty = true;                        /* the CPU may write at any time from now on */
        if (b->gpu_written && !(b->flags & N2C_BO_CPU_UNCACHED)) {
                /* A speculative fill of the kernel alias could have cached pre-GPU contents while the
                 * buffer was invisible; drop it once before the CPU can look. */
                bo_cache_sync(c, b, false);
        }
        return N2C_OK;
}

enum n2c_error n2c_bo_track(struct n2c_core *c, unsigned idx, uint32_t handle)
{
        enum n2c_error e;
        struct n2c_bo *b = tracked_bo(c, idx, handle, &e);
        if (!b)
                return e;
        if (c->track_level < N2C_TRACK_EXPLICIT || !b->cpu_visible)
                return N2C_E_STATE;
        b->explicit_track = true;
        return N2C_OK;
}

enum n2c_error n2c_bo_cpu_dirty(struct n2c_core *c, unsigned idx, uint32_t handle)
{
        enum n2c_error e;
        struct n2c_bo *b = tracked_bo(c, idx, handle, &e);
        if (!b)
                return e;
        b->cpu_dirty = true;
        return N2C_OK;
}

enum n2c_error n2c_bo_cpu_acquire(struct n2c_core *c, unsigned idx, uint32_t handle,
                                  uint32_t *invalidated_pages)
{
        enum n2c_error e;
        struct n2c_bo *b = tracked_bo(c, idx, handle, &e);
        if (invalidated_pages)
                *invalidated_pages = 0;
        if (!b)
                return e;
        if (b->gpu_dirty && !(b->flags & N2C_BO_CPU_UNCACHED)) {
                uint64_t before = c->stats.cache_inval_pages;
                bo_cache_sync(c, b, false);
                c->stats.cache_acquire_pages += c->stats.cache_inval_pages - before;
                if (invalidated_pages)
                        *invalidated_pages = (uint32_t)(c->stats.cache_inval_pages - before);
        }
        b->gpu_dirty = false;
        return N2C_OK;
}

/* ------------------------------------------------------------------ */
/* Jobs                                                                */

static bool head_inside(const struct n2c_submit *r, struct n2c_bo *const *bos,
                        uint64_t head)
{
        if (!head || (head & 63u))
                return false;
        for (unsigned i = 0; i < r->nr_handles; i++) {
                const struct n2c_bo *b = bos[i];
                if ((b->flags & N2C_BO_GPU_READ) && head >= b->va &&
                    b->size >= JOB_HEADER_BYTES &&
                    head - b->va <= b->size - JOB_HEADER_BYTES)
                        return true;
        }
        return false;
}

enum n2c_error n2c_submit(struct n2c_core *c, unsigned idx,
                          const struct n2c_submit *r, uint64_t *id_out)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        if (c->state == N2C_CORE_BROKEN)
                return N2C_E_BROKEN;
        if (c->state != N2C_CORE_READY || x->state != N2C_CTX_OPEN ||
            !x->as_live || c->reset_pending)
                return N2C_E_STATE;
        if (!r || !id_out || !r->nr_handles || r->nr_handles > N2C_JOB_BOS ||
            (r->flags & ~N2C_SUBMIT_FULL_MAINT) ||
            (r->nr_handles < 32 && (r->write_mask >> r->nr_handles)))
                return N2C_E_ARGS;
        bool need0 = r->kind == N2C_JOB_COMPUTE || r->kind == N2C_JOB_RENDER;
        bool need1 = r->kind == N2C_JOB_RENDER || r->kind == N2C_JOB_FRAGMENT;
        if (!need0 && !need1)
                return N2C_E_ARGS;
        if ((!need0 && r->heads[0]) || (!need1 && r->heads[1]))
                return N2C_E_ARGS;
        struct n2c_bo *bos[N2C_JOB_BOS];
        for (unsigned i = 0; i < r->nr_handles; i++) {
                bos[i] = bo_at(x, r->handles[i]);
                if (!bos[i] || bos[i]->state != N2C_BO_MAPPED)
                        return N2C_E_NO_HANDLE;
                for (unsigned k = 0; k < i; k++)
                        if (bos[k] == bos[i])
                                return N2C_E_ARGS;
                if ((r->write_mask & (1u << i)) &&
                    !(bos[i]->flags & N2C_BO_GPU_WRITE))
                        return N2C_E_ARGS;
        }
        if ((need0 && !head_inside(r, bos, r->heads[0])) ||
            (need1 && !head_inside(r, bos, r->heads[1])))
                return N2C_E_RANGE;
        if (x->jobs >= N2C_CTX_JOBS)
                return N2C_E_BUSY; /* per-context in-flight window */
        struct n2c_job *j = NULL;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                if (c->jobs[i].state == N2C_JOB_FREE) {
                        j = &c->jobs[i];
                        break;
                }
        }
        if (!j)
                return N2C_E_BUSY;
        if (!c->next_job_id)
                return N2C_E_NO_JOB; /* no stale-ID alias after wrap */
        memset(j, 0, sizeof(*j));
        j->id = c->next_job_id++;
        j->kind = (uint8_t)r->kind;
        j->ctx = (uint8_t)idx;
        j->heads[0] = r->heads[0];
        j->heads[1] = r->heads[1];
        j->phase = need0 ? 0 : 1;
        j->nr_bos = r->nr_handles;
        j->write_mask = r->write_mask;
        j->t_submit_us = n2c_now_us(c);
        j->full_hard = c->full_cache_maint || (r->flags & N2C_SUBMIT_FULL_MAINT);
        j->full_maint = j->full_hard || r->nr_handles >= N2C_JOB_BOS;
        for (unsigned i = 0; i < r->nr_handles; i++) {
                struct n2c_bo *b = bos[i];
                j->bos[i] = b;
                b->refs++;

        }
        x->jobs++;
        j->state = N2C_JOB_QUEUED;
        c->stats.submitted++;
        *id_out = j->id;
        n2c_poll(c);
        return N2C_OK;
}

static unsigned phase_slot(const struct n2c_core *c, const struct n2c_job *j)
{
        if (j->phase == 1)
                return c->hw->slot_fragment;
        return j->kind == N2C_JOB_COMPUTE ? c->hw->slot_compute
                                          : c->hw->slot_vertex_tiler;
}

static void try_start(struct n2c_core *c)
{
        if (c->state != N2C_CORE_READY || c->reset_pending)
                return;
        for (;;) {
                /* Oldest queued job whose context has nothing older still
                 * pending (per-context submission order across slots). */
                struct n2c_job *pick = NULL;
                uint64_t oldest[N2C_MAX_CTX] = { 0 };
                for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                        const struct n2c_job *o = &c->jobs[i];
                        if ((o->state == N2C_JOB_QUEUED || o->state == N2C_JOB_RUNNING ||
                             o->state == N2C_JOB_STOPPING) &&
                            (!oldest[o->ctx] || o->id < oldest[o->ctx]))
                                oldest[o->ctx] = o->id;
                }
                for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                        struct n2c_job *j = &c->jobs[i];
                        if (j->state != N2C_JOB_QUEUED)
                                continue;
                        /* Blocked unless it is the oldest unfinished job of its context. */
                        bool blocked = oldest[j->ctx] != j->id;
                        if (c->ctxs[j->ctx].state != N2C_CTX_OPEN ||
                            !c->ctxs[j->ctx].as_live)
                                blocked = true;
                        unsigned slot = phase_slot(c, j);
                        if (c->running[slot])
                                blocked = true;
                        if (!blocked && (!pick || j->id < pick->id))
                                pick = j;
                }
                if (!pick)
                        return;
                unsigned slot = phase_slot(c, pick);
                /* Serialize CPU maintenance after the older job completed,
                 * before its successor starts.  A job that listed its
                 * buffers cleans those (the producer lists everything the
                 * chain reads); a raw chain that may reach unlisted buffers
                 * (legacy ABI, full list, rollback valve) covers the entire
                 * context. */
                uint64_t t_clean = n2c_now_us(c);
                uint64_t clean_before = c->stats.cache_clean_pages;
                if (pick->phase == 0 || pick->kind == N2C_JOB_FRAGMENT) {
                        if (pick->full_maint) {
                                c->stats.cache_full_jobs++;
                                for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                                        struct n2c_bo *b = c->ctxs[pick->ctx].bos[i];
                                        if (!b || b->state != N2C_BO_MAPPED)
                                                continue;
                                        /* A list that was only too long (not the rollback valve) covers the whole context under the same
                                         * tracking rule as a listed buffer: a buffer the CPU never mapped is clean after its first sync. */
                                        if (pick->full_hard || c->track_level == N2C_TRACK_LEGACY || b->cpu_dirty) {
                                                bo_cache_sync(c, b, true);
                                                b->cpu_dirty = b->cpu_visible && !b->explicit_track;
                                        } else if (!(b->flags & N2C_BO_CPU_UNCACHED)) {
                                                c->stats.cache_skip_clean_pages += b->nr_pages;
                                        }
                                }
                        } else {
                                for (unsigned i = 0; i < pick->nr_bos; i++) {
                                        struct n2c_bo *b = pick->bos[i];
                                        if (b->state != N2C_BO_MAPPED)
                                                continue;
                                        if (c->track_level == N2C_TRACK_LEGACY || b->cpu_dirty) {
                                                bo_cache_sync(c, b, true);
                                                b->cpu_dirty = b->cpu_visible && !b->explicit_track;
                                        } else if (!(b->flags & N2C_BO_CPU_UNCACHED)) {
                                                c->stats.cache_skip_clean_pages += b->nr_pages;
                                        }
                                }
                        }
                }
                pick->clean_pages += (uint32_t)(c->stats.cache_clean_pages - clean_before);
                pick->clean_us += (uint32_t)(n2c_now_us(c) - t_clean);
                enum n2hw_error e = n2hw_js_start(c->hw, slot,
                        c->ctxs[pick->ctx].as, pick->heads[pick->phase],
                        c->hw->shaders_on);
                if (e != N2HW_OK) {
                        pick->status = 0xffffffffu;
                        job_finish(c, pick, N2C_JOB_FAULTED);
                        if (e == N2HW_E_SLOT_BUSY)
                                c->reset_pending = true;
                        continue;
                }
                pick->slot = slot;
                if (!pick->t_started) {
                        pick->t_started = true;
                        pick->t_start_us = n2c_now_us(c);
                }
                pick->state = N2C_JOB_RUNNING;
                pick->started_ms = now(c);
                c->running[slot] = pick->id;
        }
}

static struct n2c_job *job_by_id(struct n2c_core *c, uint64_t id)
{
        if (!id)
                return NULL;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++)
                if (c->jobs[i].state != N2C_JOB_FREE && c->jobs[i].id == id)
                        return &c->jobs[i];
        return NULL;
}

static void handle_job_irqs(struct n2c_core *c)
{
        struct n2hw_job_event ev[16];
        unsigned n = n2hw_job_irq(c->hw, ev, 16);
        for (unsigned i = 0; i < n; i++) {
                if (ev[i].slot >= 16)
                        continue;
                struct n2c_job *j = job_by_id(c, c->running[ev[i].slot]);
                c->running[ev[i].slot] = 0;
                if (!j) {
                        note(c, "Nord2 GPU core: job IRQ without a job");
                        continue;
                }
                if (!ev[i].failed && j->state == N2C_JOB_RUNNING) {
                        if (j->kind == N2C_JOB_RENDER && j->phase == 0) {
                                j->phase = 1;
                                j->state = N2C_JOB_QUEUED;
                                continue;
                        }
                        job_finish(c, j, N2C_JOB_DONE);
                        continue;
                }
                /* Failure, or completion of a job we were stopping: the
                 * IRQ is the hardware's acknowledgement that the chain no
                 * longer runs. */
                j->status = ev[i].failed ? ev[i].status : 0x03u;
                j->head_at_fault = ev[i].head;
                if (ev[i].failed && ev[i].status == JS_STATUS_BUS_FAULT &&
                    c->hw->has_issue_ttrx_3076)
                        c->reset_pending = true; /* corrupt state: reset */
                job_finish(c, j, N2C_JOB_FAULTED);
        }
}

static void handle_mmu_irqs(struct n2c_core *c)
{
        struct n2hw_mmu_fault f[16];
        unsigned n = n2hw_mmu_irq(c->hw, f, 16);
        for (unsigned i = 0; i < n; i++) {
                c->stats.mmu_faults++;
                if (f[i].as >= N2C_MAX_CTX)
                        continue;
                struct n2c_ctx *x = &c->ctxs[f[i].as];
                if (x->state == N2C_CTX_FREE)
                        continue;
                x->fault_status = f[i].status;
                x->fault_address = f[i].address;
                if (x->state == N2C_CTX_OPEN)
                        x->state = N2C_CTX_FAULTED;
                /* kbase_mmu_report_fault_and_kill(): stop the context's
                 * work and disable its AS so the stalled access completes
                 * as a fault instead of reaching memory. */
                for (unsigned k = 0; k < N2C_MAX_JOBS; k++) {
                        struct n2c_job *j = &c->jobs[k];
                        if (j->ctx == f[i].as && j->state == N2C_JOB_RUNNING)
                                stop_job(c, j);
                        if (j->ctx == f[i].as && j->state == N2C_JOB_QUEUED)
                                job_finish(c, j, N2C_JOB_CANCELLED);
                }
                if (x->as_live) {
                        if (n2hw_as_disable(c->hw, x->as) != N2HW_OK)
                                c->reset_pending = true;
                        else
                                x->as_live = false;
                }
        }
}

static void check_deadlines(struct n2c_core *c)
{
        uint64_t t = now(c);
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                struct n2c_job *j = &c->jobs[i];
                if (j->state == N2C_JOB_RUNNING &&
                    t - j->started_ms >= N2C_JOB_TIMEOUT_MS) {
                        c->stats.timeouts++;
                        stop_job(c, j);
                } else if (j->state == N2C_JOB_STOPPING &&
                           t - j->stop_ms >= N2C_STOP_TIMEOUT_MS) {
                        /* Stop not acknowledged: only a reset proves the
                         * GPU no longer touches this job's memory. */
                        c->reset_pending = true;
                }
        }
}

void n2c_poll(struct n2c_core *c)
{
        if (!c || c->state != N2C_CORE_READY)
                return;
        handle_job_irqs(c);
        handle_mmu_irqs(c);
        check_deadlines(c);
        if (c->reset_pending)
                (void)n2c_reset(c);
        try_start(c);
}

enum n2c_error n2c_seq_state(struct n2c_core *c, unsigned idx, uint64_t seq,
                             bool reap, struct n2c_seq_result *res)
{
        if (!c || !res || idx >= N2C_MAX_CTX)
                return N2C_E_ARGS;
        n2c_poll(c);
        memset(res, 0, sizeof(*res));
        /* seq 0 waits for nothing: success with empty counts.  The bridge probes the selector with WAIT_SEQ(0, 0) ("only answers on a kernel that has
         * it") and keeps asynchronous submission OFF unless that succeeds; before 0108 this returned N2C_E_NO_JOB (kIOReturnNotFound), so every
         * job was still submitted and waited for one at a time ("waits=n, seqw=0" in every device window). */
        if (!seq)
                return N2C_OK;
        if (seq >= c->next_job_id || !c->next_job_id)
                return N2C_E_NO_JOB;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                const struct n2c_job *j = &c->jobs[i];
                if (j->state == N2C_JOB_FREE || j->ctx != idx || j->id > seq)
                        continue;
                if (!terminal(j->state)) {
                        res->pending++;
                } else if (j->state == N2C_JOB_DONE) {
                        res->done++;
                } else {
                        res->failed++;
                        if (!res->first_failed_id || j->id < res->first_failed_id) {
                                res->first_failed_id = j->id;
                                res->first_failed_state = j->state;
                                res->first_failed_status = j->status;
                        }
                }
        }
        if (reap && !res->pending)
                for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                        struct n2c_job *j = &c->jobs[i];
                        if (j->state != N2C_JOB_FREE && j->ctx == idx && j->id <= seq &&
                            terminal(j->state))
                                memset(j, 0, sizeof(*j));
                }
        return N2C_OK;
}

enum n2c_error n2c_job_state(struct n2c_core *c, unsigned idx, uint64_t id,
                             bool reap, uint32_t *state, uint32_t *status)
{
        if (!c || idx >= N2C_MAX_CTX)
                return N2C_E_ARGS;
        n2c_poll(c);
        struct n2c_job *j = job_by_id(c, id);
        if (!j || j->ctx != idx)
                return N2C_E_NO_JOB;
        if (state)
                *state = j->state;
        if (status)
                *status = j->status;
        if (reap && terminal(j->state))
                memset(j, 0, sizeof(*j));
        return N2C_OK;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */

static void drain_quarantine(struct n2c_core *c)
{
        for (unsigned i = 0; i < N2C_MAX_CTX; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                for (unsigned s = 0; s < N2C_MAX_BOS; s++) {
                        struct n2c_bo *b = x->bos[s];
                        if (b && b->state == N2C_BO_QUARANTINED)
                                teardown(c, x, b); /* AS no longer live */
                }
                if (x->state == N2C_CTX_ZOMBIE) {
                        free_ctx_memory(c, x);
                        x->state = N2C_CTX_FREE;
                }
        }
}

enum n2c_error n2c_reset(struct n2c_core *c)
{
        if (!c)
                return N2C_E_ARGS;
        if (c->state == N2C_CORE_BROKEN)
                return N2C_E_BROKEN;
        if (!c->gpu_powered)
                return N2C_E_STATE;
        for (unsigned s = 0; s < 16; s++)
                if (c->running[s])
                        n2hw_js_stop(c->hw, s, true);
        if (n2hw_reset(c->hw) != N2HW_OK) {
                c->stats.reset_failures++;
                c->state = N2C_CORE_BROKEN;
                note(c, "Nord2 GPU core: reset failed; memory stays pinned");
                return N2C_E_BROKEN;
        }
        c->stats.resets++;
        c->wa_as_live = false;
        /* The completed reset stopped every bus master: running work is
         * over and every TLB/walk cache is gone. */
        memset(c->running, 0, sizeof(c->running));
        for (unsigned i = 0; i < N2C_MAX_CTX; i++)
                c->ctxs[i].as_live = false;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                struct n2c_job *j = &c->jobs[i];
                if (j->state == N2C_JOB_RUNNING ||
                    j->state == N2C_JOB_STOPPING)
                        job_finish(c, j, N2C_JOB_RESET);
        }
        drain_quarantine(c);
        for (unsigned i = 0; i < N2C_MAX_CTX; i++)
                resv_drop_ctx(c, i);          /* a reset ends everything the frames were waiting for */
        c->reset_pending = false;
        if (n2hw_apply_quirks(c->hw) != N2HW_OK ||
            n2hw_power_on_l2(c->hw) != N2HW_OK || wa_start(c) != N2C_OK ||
            n2hw_power_on_cores(c->hw, c->hw->id.shader_present) != N2HW_OK) {
                c->state = N2C_CORE_BROKEN;
                c->stats.reset_failures++;
                return N2C_E_BROKEN;
        }
        for (unsigned i = 0; i < N2C_MAX_CTX; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                if (x->state != N2C_CTX_OPEN)
                        continue;
                x->as_live = true;
                if (n2hw_as_configure(c->hw, x->as, x->mmu.root_pa) !=
                    N2HW_OK) {
                        c->state = N2C_CORE_BROKEN;
                        c->stats.reset_failures++;
                        return N2C_E_BROKEN;
                }
                x->as_live = true;
        }
        return N2C_OK;
}

const char *n2c_error_string(enum n2c_error e)
{
        switch (e) {
        case N2C_OK: return "ok";
        case N2C_E_ARGS: return "invalid arguments";
        case N2C_E_STATE: return "invalid state";
        case N2C_E_NO_CTX: return "no such context";
        case N2C_E_NO_HANDLE: return "no such buffer";
        case N2C_E_NOMEM: return "out of memory";
        case N2C_E_QUOTA: return "quota exceeded";
        case N2C_E_VA: return "GPU VA exhausted";
        case N2C_E_MAP: return "mapping failed";
        case N2C_E_BUSY: return "busy";
        case N2C_E_RANGE: return "chain outside listed buffers";
        case N2C_E_NO_JOB: return "no such job";
        case N2C_E_TIMEOUT: return "timeout";
        case N2C_E_HW: return "hardware acknowledgement failed";
        case N2C_E_BROKEN: return "GPU broken; memory pinned";
        }
        return "unknown";
}
