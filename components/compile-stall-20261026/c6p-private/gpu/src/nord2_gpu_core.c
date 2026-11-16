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

static struct n2c_ctx *ctx_at(struct n2c_core *c, unsigned idx)
{
        if (idx >= N2C_MAX_CTX)
                return NULL;
        struct n2c_ctx *x = &c->ctxs[idx];
        return x->state == N2C_CTX_FREE ? NULL : x;
}

static struct n2c_bo *bo_at(struct n2c_ctx *x, uint32_t handle)
{
        unsigned slot = handle & 0xffu;
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
        c->stats.bos_live--;
        c->stats.bytes_live -= b->size;
        if (b->pages)
                c->ops.free_host(c->ctx, b->pages,
                                 b->nr_pages * sizeof(*b->pages));
        unsigned slot = b->handle & 0xffu;
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
static void reap_idle_buffers(struct n2c_core *c, struct n2c_ctx *x)
{
        if (x->jobs)
                return;
        for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                struct n2c_bo *b = x->bos[i];
                if (b && !b->refs && b->state == N2C_BO_MAPPED)
                        teardown(c, x, b);
        }
}

static void bo_put(struct n2c_core *c, struct n2c_ctx *x, struct n2c_bo *b)
{
        if (!b->refs)
                return;
        if (--b->refs == 0 && !x->jobs && b->state == N2C_BO_MAPPED)
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

static void job_finish(struct n2c_core *c, struct n2c_job *j, uint8_t state)
{
        j->state = state;
        if (state == N2C_JOB_DONE) {
                /* GPU writes reached memory at the END_FLUSH_CLEAN; drop
                 * stale CPU lines before the client reads. */
                for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                        struct n2c_bo *b = c->ctxs[j->ctx].bos[i];
                        if (!b || !(b->flags & N2C_BO_GPU_WRITE) ||
                            (b->flags & N2C_BO_CPU_UNCACHED))
                                continue;
                        for (unsigned p = 0; p < b->nr_pages; p++)
                                c->ops.sync(c->ctx, b->pages[p].pa,
                                            b->pages[p].cpu,
                                            NORD2_GPU_PAGE_SIZE, false);
                }
                c->stats.completed++;
        } else if (state == N2C_JOB_FAULTED) {
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

enum n2c_error n2c_ctx_close(struct n2c_core *c, unsigned idx)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x)
                return N2C_E_NO_CTX;
        if (x->state == N2C_CTX_ZOMBIE)
                return N2C_E_BROKEN;
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
            !va_out || (flags & ~0x1fu) ||
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
        if (c->next_handle > 0xffffffu)
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
        b->handle = (c->next_handle++ << 8) | slot;
        bool exec = (flags & N2C_BO_GPU_EXEC) != 0;
        bool write = (flags & N2C_BO_GPU_WRITE) != 0;
        unsigned mapped = 0;
        for (; mapped < nr_pages; mapped++) {
                /* Android r32p1 JIT/tiler allocations request COHERENT_LOCAL:
                 * default cached attribute + SHARE_INNER. CPU coherency NONE
                 * does not disable GPU-internal sharing between shader/tiler
                 * units. Apply the same policy to all ordinary user BOs; CPU
                 * cache maintenance and permissions remain necessary. */
                if (!nord2_gpu_mmu_map_shared(&x->mmu, va + (uint64_t)mapped * 4096u,
                                       pages[mapped].pa, 4096,
                                       NORD2_GPU_MEMATTR_IMPL_DEF,
                                       NORD2_GPU_SHARE_INNER, write,
                                       exec))
                        break;
        }
        x->bos[slot] = b;
        c->stats.bos_live++;
        c->stats.bytes_live += size;
        x->wired += charged;
        b->refs = 1;
        b->handle_live = true;
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
        bool deferred = b->refs > 1 || x->jobs;
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
                for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                        struct n2c_job *j = &c->jobs[i];
                        if (j->state != N2C_JOB_QUEUED)
                                continue;
                        bool blocked = false;
                        for (unsigned k = 0; k < N2C_MAX_JOBS; k++) {
                                const struct n2c_job *o = &c->jobs[k];
                                if (o != j && o->ctx == j->ctx && o->id < j->id &&
                                    (o->state == N2C_JOB_QUEUED ||
                                     o->state == N2C_JOB_RUNNING ||
                                     o->state == N2C_JOB_STOPPING))
                                        blocked = true;
                        }
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
                 * before its successor starts. A raw chain may read buffers
                 * omitted from the hint list; cover the entire context. */
                if (pick->phase == 0 || pick->kind == N2C_JOB_FRAGMENT) {
                        for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                                struct n2c_bo *b = c->ctxs[pick->ctx].bos[i];
                                if (!b || b->state != N2C_BO_MAPPED ||
                                    (b->flags & N2C_BO_CPU_UNCACHED))
                                        continue;
                                for (unsigned p = 0; p < b->nr_pages; p++)
                                        c->ops.sync(c->ctx, b->pages[p].pa,
                                                    b->pages[p].cpu,
                                                    NORD2_GPU_PAGE_SIZE, true);
                        }
                }
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
