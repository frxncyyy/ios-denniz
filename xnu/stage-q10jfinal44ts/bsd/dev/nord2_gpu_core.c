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

static void free_ctx_memory(struct n2c_core *c, struct n2c_ctx *x);

/* 0630: register side of two chains per slot.  nord2_gpu_hw.o is a CI archive member that this port never rebuilds, so these live here and
 * use the same n2hw ops (the host model test builds the core with N2C_HW_RB_EXTERN and supplies its own). */
#ifndef N2C_HW_RB_EXTERN
static uint32_t rb_rd(struct n2hw *hw, uint32_t off) { return hw->ops.read(hw->ctx, off); }
static void rb_wr(struct n2hw *hw, uint32_t off, uint32_t v) { hw->ops.write(hw->ctx, off, v); }
static uint64_t rb_rd64(struct n2hw *hw, uint32_t lo) { return (uint64_t)rb_rd(hw, lo) | ((uint64_t)rb_rd(hw, lo + 4) << 32); }

/* JS_COMMAND HARD/SOFT_STOP_0/1: stops the CURRENT chain only if its JOB_CHAIN_FLAG equals flag (kbasep_job_slot_soft_or_hard_stop_do_action
 * with JOBCHAIN_DISAMBIGUATION, a tTRx feature in the vendor r32p1 mali_base_hwconfig_features.h), so a chain that moved up from NEXT is
 * never hit by a stop meant for the one before it. */
static void n2hw_js_stop_chain(struct n2hw *hw, unsigned slot, bool hard, bool flag)
{
        if (!hw || slot >= 16)
                return;
        uint32_t cmd = flag ? (hard ? N2HW_JS_CMD_HARD_STOP_1 : N2HW_JS_CMD_SOFT_STOP_1)
                            : (hard ? N2HW_JS_CMD_HARD_STOP_0 : N2HW_JS_CMD_SOFT_STOP_0);
        rb_wr(hw, N2HW_JS_REG(slot, N2HW_JS_COMMAND), cmd);
}

/* Take back a chain in the NEXT registers that has not started (kbase_backend_soft_hard_stop_slot / kbase_gpu_irq_evict): NOP into
 * JS_COMMAND_NEXT, then a JS_HEAD_NEXT that still holds the chain = it never started (returned, then cleared); 0 = it started. */
static bool n2hw_js_evict_next(struct n2hw *hw, unsigned slot, uint64_t *head)
{
        if (head)
                *head = 0;
        if (!hw || slot >= 16)
                return false;
        rb_wr(hw, N2HW_JS_REG(slot, N2HW_JS_COMMAND_NEXT), 0u);
        uint64_t h = rb_rd64(hw, N2HW_JS_REG(slot, N2HW_JS_HEAD_NEXT_LO));
        if (!h)
                return false;
        rb_wr(hw, N2HW_JS_REG(slot, N2HW_JS_HEAD_NEXT_LO), 0u);
        rb_wr(hw, N2HW_JS_REG(slot, N2HW_JS_HEAD_NEXT_HI), 0u);
        /* 0668: the chain taken back was the last one written: give its JOB_CHAIN_FLAG back, so the next chain again carries the flag
         * opposite to the current one (else a flag-targeted stop of a current chain that just completed would hit the next chain) */
        hw->job_chain_flag[slot] = !hw->job_chain_flag[slot];
        if (head)
                *head = h;
        return true;
}

/* kbase_job_done's register sequence per slot: status/head; on a failure the NEXT chain is evicted BEFORE the bits are cleared (the failed
 * chain halts the slot until then, so it cannot have started); clear; JOB_IRQ_JS_STATE; a failure raised after RAWSTAT was read counts as
 * still active (the next round reports it). */
static unsigned n2hw_job_irq_rb(struct n2hw *hw, const uint64_t *next_head, struct n2hw_job_event_rb *events, unsigned max_events)
{
        if (!hw || !events || !max_events)
                return 0;
        uint32_t raw = rb_rd(hw, N2HW_JOB_IRQ_RAWSTAT);
        unsigned n = 0;
        for (unsigned slot = 0; slot < 16 && n < max_events; slot++) {
                uint32_t done = 1u << slot, failed = 1u << (slot + 16);
                if (!(raw & (done | failed)))
                        continue;
                struct n2hw_job_event_rb *ev = &events[n++];
                ev->slot = slot;
                ev->done = (raw & done) != 0;
                ev->failed = (raw & failed) != 0;
                ev->status = ev->failed ? rb_rd(hw, N2HW_JS_REG(slot, N2HW_JS_STATUS)) : 0;
                if (ev->failed && ev->status == 0)
                        ev->status = 0x04;
                ev->head = rb_rd64(hw, N2HW_JS_REG(slot, N2HW_JS_HEAD_LO));
                ev->evicted = false;
                ev->evicted_head = 0;
                if (ev->failed && next_head && next_head[slot])
                        ev->evicted = n2hw_js_evict_next(hw, slot, &ev->evicted_head);
                rb_wr(hw, N2HW_JOB_IRQ_CLEAR, raw & (done | failed));
                uint32_t active = rb_rd(hw, N2HW_JOB_IRQ_JS_STATE);
                ev->cur_active = (active >> slot) & 1u;
                ev->next_busy = (active >> (slot + 16)) & 1u;
                if (!ev->cur_active && !ev->failed && ((rb_rd(hw, N2HW_JOB_IRQ_RAWSTAT) >> (slot + 16)) & 1u))
                        ev->cur_active = true;
        }
        return n;
}
#endif


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
        if (b->size) {
                (void)n2va_free(x->va, b->va, b->size + BO_GUARD);
                struct n2c_freed *fr = &c->freed[c->freed_next++ % N2C_FREED_RING];      /* 0750: for the fault report */
                fr->va = b->va; fr->size = b->size; fr->t_ms = now(c); fr->handle = b->handle; fr->ctx = (uint8_t)(x - c->ctxs);
        }
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
        c->lat_release_late = true;     /* 0400 */
        c->as_dyn = true;               /* 0500 */
        memset(c->as_owner, N2C_AS_NONE, sizeof(c->as_owner));
        return true;
}

bool n2c_set_as_dynamic(struct n2c_core *c, bool on)
{
        if (!c)
                return false;
        for (unsigned i = 0; i < N2C_MAX_CTX; i++)
                if (c->ctxs[i].state != N2C_CTX_FREE)
                        return false;
        c->as_dyn = on;
        memset(c->as_owner, N2C_AS_NONE, sizeof(c->as_owner));
        return true;
}

bool n2c_set_jsnext(struct n2c_core *c, bool on)
{
        if (!c)
                return false;
        bool idle = c->next_job_id == 1;        /* no job was ever submitted */
        if (idle) {
                c->jsnext = on;
                c->jsnext_rb = on;
                return true;
        }
        if (on && !c->jsnext_rb)
                return false;                   /* completions are read the one-chain way: NEXT stays unused until reboot */
        c->jsnext = on;
        return true;
}

/* 0500: a client context of the dynamic mode (the kernel context 0 keeps AS0 as before). */
static bool as_dynamic_ctx(const struct n2c_core *c, unsigned idx)
{
        return c->as_dyn && idx != 0;
}

/* The context no longer holds its AS (the AS was disabled with an acknowledgement, or a reset unprogrammed every AS). */
static void as_unbind(struct n2c_core *c, struct n2c_ctx *x)
{
        x->as_live = false;
        unsigned idx = (unsigned)(x - c->ctxs);
        if (!as_dynamic_ctx(c, idx))
                return;
        if (x->as < N2C_HW_AS && c->as_owner[x->as] == idx) {
                c->as_owner[x->as] = N2C_AS_NONE;
                if (c->as_stats.bound)
                        c->as_stats.bound--;
        }
        x->as = N2C_AS_NONE;
}

/* After a verified reset or before power-down: no client holds an AS. */
static void as_unbind_all(struct n2c_core *c)
{
        for (unsigned i = 0; i < N2C_MAX_CTX; i++)
                if (as_dynamic_ctx(c, i) && c->ctxs[i].state != N2C_CTX_FREE)
                        as_unbind(c, &c->ctxs[i]);
        memset(c->as_owner, N2C_AS_NONE, sizeof(c->as_owner));
        c->as_stats.bound = 0;
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
                if (x->state != N2C_CTX_OPEN || as_dynamic_ctx(c, i))
                        continue;       /* 0500: dynamic clients get an AS when their next job starts */
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
                as_unbind(c, x);        /* 0500: also an AS held (disabled) by a faulted context */
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
        /* 0500: static mode keeps context index = AS number (1..7); dynamic clients take any of N2C_MAX_CTX slots. */
        unsigned first = kernel ? 0 : 1, last = kernel ? 1 : (c->as_dyn ? N2C_MAX_CTX : N2C_HW_AS);
        for (unsigned i = first; i < last; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                if (x->state != N2C_CTX_FREE)
                        continue;
                if (!as_dynamic_ctx(c, i) && c->hw->identified && !(c->hw->id.as_present & (1u << i)))
                        continue;
                uint32_t gen = x->generation;
                memset(x, 0, sizeof(*x));
                x->generation = gen + 1;
                if (!c->ops.alloc_host(c->ctx, N2C_MAX_BOS * sizeof(*x->bos), (void **)&x->bos) || !x->bos) {
                        x->bos = NULL;
                        return N2C_E_NOMEM;
                }
                memset(x->bos, 0, N2C_MAX_BOS * sizeof(*x->bos));
                if (!c->ops.alloc_host(c->ctx, sizeof(*x->va), (void **)&x->va) || !x->va) {
                        c->ops.free_host(c->ctx, x->bos, N2C_MAX_BOS * sizeof(*x->bos));
                        x->bos = NULL;
                        x->va = NULL;
                        return N2C_E_NOMEM;
                }
                const struct nord2_gpu_mmu_ops mops = {
                        c->ops.alloc_table, c->ops.free_table,
                        c->ops.flush_table, c->ops.alloc_host,
                        c->ops.free_host, c->ops.log,
                };
                if (!nord2_gpu_mmu_init(&x->mmu, &mops, c->ctx,
                                        N2C_MAX_TABLES)) {
                        free_ctx_memory(c, x);
                        return N2C_E_NOMEM;
                }
                if (!n2va_init(x->va, N2C_VA_BASE, N2C_VA_LIMIT)) {
                        free_ctx_memory(c, x);
                        return N2C_E_VA;
                }
                x->as = as_dynamic_ctx(c, i) ? N2C_AS_NONE : (uint8_t)i;
                x->owner = owner;
                x->wired_limit = wired_limit;
                x->state = N2C_CTX_OPEN;
                c->as_stats.ctx_open++;
                if (c->as_stats.ctx_open > c->as_stats.ctx_peak)
                        c->as_stats.ctx_peak = c->as_stats.ctx_open;
                if (!as_dynamic_ctx(c, i) && c->state == N2C_CORE_READY && c->gpu_powered) {
                        x->as_live = true;
                        if (n2hw_as_configure(c->hw, x->as, x->mmu.root_pa) !=
                            N2HW_OK) {
                                if (n2hw_as_disable(c->hw, x->as) == N2HW_OK) {
                                        x->as_live = false;
                                        free_ctx_memory(c, x);
                                        x->state = N2C_CTX_FREE;
                                        c->as_stats.ctx_open--;
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
        c->as_stats.ctx_refused++;
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
                                if (j->precise && !n2c_mask_bit(j->touch_mask, i))
                                        continue;       /* 0632: this chain never accessed it */
                                bool any_w = false;
                                for (unsigned w = 0; w < N2C_MASK_WORDS; w++)
                                        any_w |= j->write_mask[w] != 0;
                                if (c->wmask_inval && any_w && !n2c_mask_bit(j->write_mask, i)) {
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
        /* 0660: per-context GPU time and queue wait, run-time histogram per slot, invalidated pages */
        if ((state == N2C_JOB_DONE || state == N2C_JOB_FAULTED) && j->t_started && j->ctx < N2C_MAX_CTX) {
                uint64_t end = j->t_end_us ? j->t_end_us : n2c_now_us(c);
                uint64_t run = end >= j->t_start_us ? end - j->t_start_us : 0, q = j->t_start_us >= j->t_submit_us ? j->t_start_us - j->t_submit_us : 0;
                struct n2c_ctx *px = &c->ctxs[j->ctx];
                px->prof_run_us += run;
                px->prof_q_us += q;
                px->prof_jobs++;
                if (q > px->prof_q_max_us)
                        px->prof_q_max_us = (uint32_t)(q > 0xffffffffull ? 0xffffffffull : q);
                static const uint32_t lim[6] = { 100, 250, 500, 1000, 4000, 16000 };
                unsigned bk = 0;
                while (bk < 6 && run >= lim[bk])
                        bk++;
                c->prof.hist[j->slot == 0 ? 0 : 1][bk]++;
                c->prof.inval_pages += j->inval_pages;
        }
        /* 0400: a completion seen by handle_job_irqs keeps its references until n2c_poll started the next job (job_release_due);
         * every other caller (MMU fault, reset, close, start failure) releases at once as before. */
        if (c->lat_in_irq && c->lat_release_late) {
                j->release_due = true;
                c->lat.deferred++;
                return;
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
        if (!c->jsnext_rb) {
                n2hw_js_stop(c->hw, j->slot, true);
                j->state = N2C_JOB_STOPPING;
                j->stop_ms = now(c);
                return;
        }
        /* 0630.  A chain still in the NEXT registers is taken back if it has not started: it never ran, so it is CANCELLED (callers are
         * the MMU-fault kill and close, which cancel the context's queued jobs as well; a deadline never applies to a chain in NEXT).
         * If it did start, the chain ahead of it is complete and it is the slot's current chain: the flag-targeted stop reaches it alone. */
        if (j->in_next && j->slot < 16 && c->next[j->slot] == j->id) {
                uint64_t head = 0;
                if (n2hw_js_evict_next(c->hw, j->slot, &head)) {
                        c->next[j->slot] = 0;
                        j->in_next = false;
                        c->lat.nx_evicted++;
                        if (head != j->heads[j->phase]) {
                                c->reset_pending = true;        /* not the chain we wrote: only a reset says what runs */
                                note(c, "Nord2 GPU core: NEXT eviction returned an unknown chain; reset");
                        }
                        job_finish(c, j, N2C_JOB_CANCELLED);
                        return;
                }
        }
        /* The current chain (or the NEXT one that started): HARD_STOP_0/1 with its own JOB_CHAIN_FLAG never hits the other chain. */
        n2hw_js_stop_chain(c->hw, j->slot, true, j->chain_flag);
        c->lat.nx_stops++;
        j->state = N2C_JOB_STOPPING;
        j->stop_ms = now(c);
}

static void free_ctx_memory(struct n2c_core *c, struct n2c_ctx *x)
{
        if (x->bos)
                for (unsigned s = 0; s < N2C_MAX_BOS; s++) {
                        struct n2c_bo *b = x->bos[s];
                        if (b)
                                free_bo_memory(c, x, b);
                }
        if (x->mmu.valid)
                nord2_gpu_mmu_fini(&x->mmu);
        /* 0500: the per-context tables go with the context */
        if (x->bos)
                c->ops.free_host(c->ctx, x->bos, N2C_MAX_BOS * sizeof(*x->bos));
        if (x->va)
                c->ops.free_host(c->ctx, x->va, sizeof(*x->va));
        x->bos = NULL;
        x->va = NULL;
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
                        /* The reset reprogrammed the AS (static mode; 0500 dynamic clients come out of a reset unbound); drain again. */
                        if (x->as_live && n2hw_as_disable(c->hw, x->as) != N2HW_OK) {
                                x->state = N2C_CTX_ZOMBIE;
                                return N2C_E_BROKEN;
                        }
                }
                as_unbind(c, x);
                x->as_live = false;
        }
        as_unbind(c, x);                /* 0500: unpowered (the AS UPDATE at power-up invalidates), or an AS held disabled after a fault */
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
        if (c->as_stats.ctx_open)
                c->as_stats.ctx_open--;
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
        if (!n2va_alloc(x->va, size + BO_GUARD, align, &va))
                return N2C_E_VA;
        struct n2c_bo *b = NULL;
        if (!c->ops.alloc_host(c->ctx, sizeof(*b), (void **)&b) || !b) {
                (void)n2va_free(x->va, va, size + BO_GUARD);
                return N2C_E_NOMEM;
        }
        memset(b, 0, sizeof(*b));
        if (!c->ops.alloc_host(c->ctx, nr_pages * sizeof(*b->pages),
                               (void **)&b->pages) || !b->pages) {
                c->ops.free_host(c->ctx, b, sizeof(*b));
                (void)n2va_free(x->va, va, size + BO_GUARD);
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
        b->dirty_gen++;
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
        b->dirty_gen++;
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
        b->dirty_gen++;
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
        if (x->state == N2C_CTX_FAULTED)
                c->as_stats.faulted_refused++;  /* 0750 */
        if (c->state != N2C_CORE_READY || x->state != N2C_CTX_OPEN ||
            (!x->as_live && !as_dynamic_ctx(c, idx)) || c->reset_pending)
                return N2C_E_STATE;     /* 0500: a dynamic client gets its AS in try_start */
        if (!r || !id_out || !r->nr_handles || r->nr_handles > N2C_JOB_BOS ||
            (r->flags & ~(N2C_SUBMIT_FULL_MAINT | N2C_SUBMIT_PRECISE | N2C_SUBMIT_TRUNCATED)))
                return N2C_E_ARGS;
        /* 0661: 128-bit masks (bits 0..31 in write_mask / touch_mask, the rest in *_hi) */
        uint32_t wm[N2C_MASK_WORDS] = { r->write_mask }, tm[N2C_MASK_WORDS] = { r->touch_mask };
        for (unsigned w = 1; w < N2C_MASK_WORDS; w++) {
                wm[w] = r->write_mask_hi[w - 1];
                tm[w] = r->touch_mask_hi[w - 1];
        }
        for (unsigned i = r->nr_handles; i < N2C_JOB_BOS; i++)
                if (n2c_mask_bit(wm, i))
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
                if (n2c_mask_bit(wm, i) &&
                    !(bos[i]->flags & N2C_BO_GPU_WRITE))
                        return N2C_E_ARGS;
        }
        if ((need0 && !head_inside(r, bos, r->heads[0])) ||
            (need1 && !head_inside(r, bos, r->heads[1])))
                return N2C_E_RANGE;
        if (x->jobs >= N2C_CTX_JOBS)
                return N2C_E_BUSY; /* per-context in-flight window */
        if (c->prio_on && x->prio < N2C_PRIO_HIGH) {      /* 0662: keep N2C_RESERVE_HIGH records for the compositor */
                unsigned free_records = 0;
                for (unsigned i = 0; i < N2C_MAX_JOBS; i++)
                        free_records += c->jobs[i].state == N2C_JOB_FREE;
                if (free_records <= N2C_RESERVE_HIGH) {
                        c->lat.pr_reserved++;
                        return N2C_E_BUSY;
                }
        }
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
        for (unsigned w = 0; w < N2C_MASK_WORDS; w++)
                j->write_mask[w] = wm[w];
        j->t_submit_us = n2c_now_us(c);
        j->full_hard = c->full_cache_maint || (r->flags & N2C_SUBMIT_FULL_MAINT);
        j->full_maint = j->full_hard || r->nr_handles >= N2C_JOB_BOS || (r->flags & N2C_SUBMIT_TRUNCATED);
        /* 0632: a declared access set; the chain's own head buffer always counts as touched */
        j->precise = (r->flags & N2C_SUBMIT_PRECISE) && !j->full_maint;
        if (j->precise) {
                for (unsigned i = 0; i < r->nr_handles; i++)
                        if (n2c_mask_bit(tm, i) || n2c_mask_bit(wm, i))
                                j->touch_mask[i >> 5] |= 1u << (i & 31u);
                uint64_t head = r->heads[need0 ? 0 : 1];
                for (unsigned i = 0; i < r->nr_handles; i++)
                        if (head >= bos[i]->va && head - bos[i]->va < bos[i]->size)
                                j->touch_mask[i >> 5] |= 1u << (i & 31u);
                c->lat.precise_jobs++;
                c->prof.precise_jobs++;
        }
        c->prof.jobs++;
        if (j->full_maint)
                c->prof.full_jobs++;
        if (r->flags & N2C_SUBMIT_TRUNCATED)
                c->prof.truncated_jobs++;
        /* 0661: the CPU clean of a listed (not whole-context) job runs in the submitting thread, outside the lock (n2c_prep_*) */
        j->prep = c->preclean && !j->full_maint && (need0 || r->kind == N2C_JOB_FRAGMENT);
        for (unsigned i = 0; i < r->nr_handles; i++) {
                struct n2c_bo *b = bos[i];
                j->bos[i] = b;
                b->refs++;

        }
        x->jobs++;
        j->state = N2C_JOB_QUEUED;
        c->stats.submitted++;
        if (!c->lat.submits)
                c->lat.window_start_us = j->t_submit_us;     /* 0400 */
        c->lat.submits++;
        c->lat.kind[r->kind & 3u]++;
        c->lat.depth_sum += x->jobs;
        if (x->jobs > c->lat.depth_max)
                c->lat.depth_max = x->jobs;
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

/* 0400 latency accounting (see struct n2c_lat). */
#define N2C_LAT_ACC(n, sum, max, v) do { uint64_t v_ = (v); c->lat.n++; c->lat.sum += v_; if (v_ > c->lat.max) c->lat.max = (uint32_t)(v_ > 0xffffffffull ? 0xffffffffull : v_); } while (0)
static void lat_busy(struct n2c_core *c, uint64_t t)
{
        bool any = false;
        for (unsigned s = 0; s < 16; s++)
                any |= c->running[s] != 0;
        if (any && !c->lat_slots_busy) {
                c->lat_slots_busy = 1;
                c->lat_busy_since = t;
        } else if (!any && c->lat_slots_busy) {
                c->lat_slots_busy = 0;
                c->lat.busy_us += t >= c->lat_busy_since ? t - c->lat_busy_since : 0;
        }
}

static void lat_started(struct n2c_core *c, struct n2c_job *j, unsigned slot)
{
        uint64_t t = n2c_now_us(c);
        bool second = j->kind == N2C_JOB_RENDER && j->phase == 1;
        uint64_t base = second ? 0 : j->t_submit_us;
        bool done = c->lat_done_us[j->ctx] != 0;
        if (done && c->lat_done_us[j->ctx] > base)
                base = c->lat_done_us[j->ctx];
        if (done && c->lat_done_slot[j->ctx] == slot)
                c->lat.same_slot++;
        c->lat_done_us[j->ctx] = 0;
        if (second) {
                if (done)
                        N2C_LAT_ACC(phase_n, phase_sum, phase_max, t >= base ? t - base : 0);
        } else {
                N2C_LAT_ACC(hand_n, hand_sum, hand_max, t >= base ? t - base : 0);
        }
        lat_busy(c, t);
}

bool n2c_lat_take(struct n2c_core *c, struct n2c_lat *out, uint64_t min_window_us)
{
        if (!c || !out || !c->lat.submits)
                return false;
        uint64_t t = n2c_now_us(c);
        if (t - c->lat.window_start_us < min_window_us)
                return false;
        if (c->lat_slots_busy) {        /* split a busy period at the window edge */
                c->lat.busy_us += t >= c->lat_busy_since ? t - c->lat_busy_since : 0;
                c->lat_busy_since = t;
        }
        *out = c->lat;
        memset(&c->lat, 0, sizeof(c->lat));
        out->window_start_us = t - out->window_start_us; /* window length */
        return true;
}

static bool ctx_has_active(struct n2c_core *c, unsigned idx);

static bool ctx_has_queued(const struct n2c_core *c, unsigned idx)
{
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++)
                if (c->jobs[i].ctx == idx && c->jobs[i].state == N2C_JOB_QUEUED)
                        return true;
        return false;
}

/* 0500 (panfrost_mmu_as_get): give context idx a hardware AS.  A free one first; else the least recently used AS of a context with nothing
 * on the hardware (no RUNNING/STOPPING job), preferring contexts with nothing queued either.  The victim's AS is drained and unmapped
 * (n2hw_as_disable: LOCK + FLUSH_MEM over the whole range, then UPDATE to UNMAPPED) before it is reprogrammed, so no TLB entry or walk of the
 * victim's tables survives; the new owner's tables are written with UPDATE and its range is flushed again (FLUSH_PT).  Page tables changed
 * while a context held no AS need no flush of their own: nothing caches them (the last flush of the AS that held them covered the whole
 * range, and every job start cleans+invalidates the L2).  false: no AS this time (every AS busy: retried at the next poll), or the hardware
 * did not acknowledge (reset_pending set; the reset unbinds every client). */
static bool as_bind(struct n2c_core *c, unsigned idx)
{
        struct n2c_ctx *x = &c->ctxs[idx];
        if (x->as_live)
                return true;
        if (!c->gpu_powered || c->state != N2C_CORE_READY || c->reset_pending)
                return false;
        unsigned pick = N2C_AS_NONE;
        for (unsigned as = 1; as < N2C_HW_AS && pick == N2C_AS_NONE; as++)
                if ((c->hw->id.as_present & (1u << as)) && c->as_owner[as] == N2C_AS_NONE)
                        pick = as;
        if (pick == N2C_AS_NONE) {
                unsigned best_cls = 2;
                uint64_t best_stamp = UINT64_MAX;
                for (unsigned as = 1; as < N2C_HW_AS; as++) {
                        unsigned v = c->as_owner[as];
                        if (!(c->hw->id.as_present & (1u << as)) || v >= N2C_MAX_CTX || v == idx)
                                continue;
                        if (ctx_has_active(c, v))
                                continue;               /* never evict a context with jobs on the hardware */
                        unsigned cls = ctx_has_queued(c, v) ? 1u : 0u;
                        if (cls < best_cls || (cls == best_cls && c->ctxs[v].as_stamp < best_stamp)) {
                                best_cls = cls;
                                best_stamp = c->ctxs[v].as_stamp;
                                pick = as;
                        }
                }
                if (pick == N2C_AS_NONE) {
                        c->as_stats.bind_waits++;
                        return false;
                }
                struct n2c_ctx *v = &c->ctxs[c->as_owner[pick]];
                if (n2hw_as_disable(c->hw, pick) != N2HW_OK) {
                        c->as_stats.bind_failures++;
                        c->reset_pending = true;        /* the victim still counts as bound until the reset */
                        note(c, "Nord2 GPU core: AS eviction not acknowledged; reset");
                        return false;
                }
                as_unbind(c, v);
                c->as_stats.evictions++;
        }
        x->as = (uint8_t)pick;
        c->as_owner[pick] = (uint8_t)idx;
        c->as_stats.bound++;
        x->as_live = true;      /* UPDATE may commit even on timeout */
        x->as_stamp = ++c->as_clock;
        if (n2hw_as_configure(c->hw, pick, x->mmu.root_pa) != N2HW_OK ||
            n2hw_as_flush(c->hw, pick, 0, 1ull << 44, false) != N2HW_OK) {
                c->as_stats.bind_failures++;
                c->reset_pending = true;
                note(c, "Nord2 GPU core: AS bind not acknowledged; reset");
                return false;
        }
        c->as_stats.binds++;
        return true;
}

static const struct n2c_job *job_by_id_c(const struct n2c_core *c, uint64_t id)
{
        if (!id)
                return NULL;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++)
                if (c->jobs[i].state != N2C_JOB_FREE && c->jobs[i].id == id)
                        return &c->jobs[i];
        return NULL;
}

/* 0632: two jobs of one context conflict when a buffer one of them writes is touched by the other (by buffer identity). */
static bool jobs_conflict(const struct n2c_job *a, const struct n2c_job *b)
{
        for (unsigned i = 0; i < a->nr_bos; i++) {
                if (!n2c_mask_bit(a->touch_mask, i))
                        continue;
                bool aw = n2c_mask_bit(a->write_mask, i);
                for (unsigned k = 0; k < b->nr_bos; k++) {
                        if (b->bos[k] != a->bos[i] || !n2c_mask_bit(b->touch_mask, k))
                                continue;
                        if (aw || n2c_mask_bit(b->write_mask, k))
                                return true;
                }
        }
        return false;
}

/* 0632: j (a precise vertex/tiler/compute job, JS1) may start although older jobs of its context are unfinished, when every one of those is a
 * precise FRAGMENT job (JS0; at most four) that conflicts with nothing j touches.  Pass k's fragment then overlaps pass k+1's vertex/tiler as
 * with kbase; pass k+1's fragment still waits for pass k's (same slot, in order) and for its own vertex/tiler job (older, unfinished). */
#define N2C_OVERTAKE_MAX 4u
static bool may_overtake(struct n2c_core *c, const struct n2c_job *j)
{
        if (!c->pipe || !j->precise || j->kind != N2C_JOB_COMPUTE)
                return false;
        unsigned n = 0;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                const struct n2c_job *o = &c->jobs[i];
                if (o == j || o->ctx != j->ctx || o->id > j->id ||
                    !(o->state == N2C_JOB_QUEUED || o->state == N2C_JOB_RUNNING || o->state == N2C_JOB_STOPPING))
                        continue;
                if (!o->precise || o->kind != N2C_JOB_FRAGMENT || o->state == N2C_JOB_STOPPING || ++n > N2C_OVERTAKE_MAX)
                        return false;
                if (jobs_conflict(o, j) || jobs_conflict(j, o)) {
                        c->lat.ov_blocked++;
                        return false;
                }
        }
        return n > 0;
}

static struct n2c_job *job_by_id(struct n2c_core *c, uint64_t id);

/* 0662: pick class: the context's priority, a job waiting longer than N2C_AGE_HIGH_US counts as high (no starvation) */
static unsigned pick_class(const struct n2c_core *c, const struct n2c_job *j, uint64_t now)
{
        if (!c->prio_on)
                return 0;
        if (now >= j->t_submit_us && now - j->t_submit_us >= N2C_AGE_HIGH_US)
                return N2C_PRIO_HIGH;
        return c->ctxs[j->ctx].prio;
}

/* 0660: why a buffer is cleaned before a listed job (-1 = it is not) */
static int clean_reason(const struct n2c_core *c, const struct n2c_bo *b)
{
        if (c->track_level == N2C_TRACK_LEGACY)
                return N2C_CR_LEGACY;
        if (!b->cpu_dirty)
                return -1;
        return (b->cpu_visible && !b->explicit_track) ? N2C_CR_IMPLICIT : N2C_CR_DIRTY;
}

static void prof_clean(struct n2c_core *c, const struct n2c_bo *b, int reason)
{
        if (reason < 0 || (b->flags & N2C_BO_CPU_UNCACHED))
                return;
        c->prof.clean_pages[reason] += b->nr_pages;
        if (b->nr_pages > c->prof.clean_max_pages)
                c->prof.clean_max_pages = b->nr_pages;
}

static void try_start(struct n2c_core *c)
{
        if (c->state != N2C_CORE_READY || c->reset_pending || c->hold_start)
                return;
        bool nobind[N2C_MAX_CTX] = { false };   /* 0500: contexts that found no AS in this call */
        unsigned evictions = 0;                 /* 0662: NEXT take-backs in this call (bounded) */
        for (;;) {
                /* Oldest queued job whose context has nothing older still
                 * pending (per-context submission order across slots). */
                struct n2c_job *pick = NULL;
                bool pick_next = false, pick_ov = false;
                uint64_t oldest[N2C_MAX_CTX] = { 0 };
                uint64_t second[N2C_MAX_CTX] = { 0 };   /* 0630: second oldest unfinished job of each context */
                for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                        const struct n2c_job *o = &c->jobs[i];
                        if (!(o->state == N2C_JOB_QUEUED || o->state == N2C_JOB_RUNNING ||
                              o->state == N2C_JOB_STOPPING))
                                continue;
                        if (!oldest[o->ctx] || o->id < oldest[o->ctx]) {
                                second[o->ctx] = oldest[o->ctx];
                                oldest[o->ctx] = o->id;
                        } else if (!second[o->ctx] || o->id < second[o->ctx]) {
                                second[o->ctx] = o->id;
                        }
                }
                uint64_t tnow = c->prio_on ? n2c_now_us(c) : 0;
                unsigned pick_cls = 0;
                bool evicted = false;
                for (unsigned i = 0; i < N2C_MAX_JOBS && !evicted; i++) {
                        struct n2c_job *j = &c->jobs[i];
                        if (j->state != N2C_JOB_QUEUED || j->prep)       /* 0661: a job in prep waits for its commit */
                                continue;
                        /* Blocked unless it is the oldest unfinished job of its context (0632: or may overtake older fragment jobs). */
                        bool overtake = false;
                        bool blocked = oldest[j->ctx] != j->id;
                        bool ctx_ok = true;
                        if (blocked && may_overtake(c, j))
                                blocked = false, overtake = true;
                        if (c->ctxs[j->ctx].state != N2C_CTX_OPEN)
                                ctx_ok = false;
                        else if (!c->ctxs[j->ctx].as_live && (!as_dynamic_ctx(c, j->ctx) || nobind[j->ctx]))
                                ctx_ok = false;
                        else if (!c->ctxs[j->ctx].as_live && !c->gpu_powered)
                                ctx_ok = false;
                        if (!ctx_ok)
                                blocked = true;
                        unsigned slot = phase_slot(c, j);
                        bool to_next = false;
                        unsigned cls = pick_class(c, j, tnow);
                        /* 0662: a waiting higher-class job takes back a not-yet-started lower-class chain of another context from NEXT */
                        if (c->prio_on && c->jsnext && !blocked && ctx_ok && evictions < 2 && j->kind != N2C_JOB_RENDER && c->running[slot] && c->next[slot]) {
                                struct n2c_job *q = job_by_id(c, c->next[slot]);
                                const struct n2c_job *f = job_by_id_c(c, c->running[slot]);
                                uint64_t head = 0;
                                /* only when j can take the NEXT registers right after (else q would go back there and be taken again) */
                                if (f && f->state == N2C_JOB_RUNNING && !f->in_next && f->kind != N2C_JOB_RENDER &&
                                    q && q->in_next && q->state == N2C_JOB_RUNNING && q->ctx != j->ctx && pick_class(c, q, tnow) < cls &&
                                    n2hw_js_evict_next(c->hw, slot, &head)) {
                                        c->next[slot] = 0;
                                        q->in_next = false;
                                        q->state = N2C_JOB_QUEUED;
                                        c->lat.pr_evicted++;
                                        c->lat.nx_evicted++;
                                        if (head != q->heads[q->phase]) {
                                                c->reset_pending = true;
                                                note(c, "Nord2 GPU core: NEXT eviction returned an unknown chain; reset");
                                                return;
                                        }
                                        evicted = true;         /* pick again with the slot's NEXT free */
                                        evictions++;
                                        continue;
                                }
                        }
                        if (c->running[slot]) {
                                /* 0630: the slot runs a chain; this one may wait in its NEXT registers when the hardware order is enough:
                                 * (cross) it is the oldest unfinished job of its context and the running chain belongs to another one, or
                                 * (same) the only older unfinished job of its context is that running chain itself (one slot runs its chains
                                 * in order, each ending with a cache clean and the next starting with clean+invalidate, as kbase relies on
                                 * for same-slot dependencies).  Never behind a stopping chain or a render job (two phases on two slots). */
                                to_next = !blocked || (oldest[j->ctx] == c->running[slot] && second[j->ctx] == j->id);
                                if (!c->jsnext || !ctx_ok || c->next[slot] || j->kind == N2C_JOB_RENDER)
                                        to_next = false;
                                if (to_next) {
                                        const struct n2c_job *f = job_by_id_c(c, c->running[slot]);
                                        if (!f || f->state != N2C_JOB_RUNNING || f->in_next || f->kind == N2C_JOB_RENDER)
                                                to_next = false;
                                }
                                blocked = !to_next;
                        }
                        if (!blocked && (!pick || cls > pick_cls || (cls == pick_cls && j->id < pick->id))) {
                                pick = j;
                                pick_cls = cls;
                                pick_next = to_next;
                                pick_ov = overtake;
                        }
                }
                if (evicted)
                        continue;
                if (!pick)
                        return;
                if (c->prio_on && pick_cls == N2C_PRIO_HIGH && c->ctxs[pick->ctx].prio < N2C_PRIO_HIGH)
                        c->lat.pr_aged++;
                if (!c->ctxs[pick->ctx].as_live) {
                        if (!as_bind(c, pick->ctx)) {
                                if (c->reset_pending)
                                        return;
                                nobind[pick->ctx] = true;
                                continue;
                        }
                }
                c->ctxs[pick->ctx].as_stamp = ++c->as_clock;
                if (c->cores_off) {             /* 0666: shader cores were powered down while idle */
                        uint64_t tp = n2c_now_us(c);
                        if (n2hw_power_on_cores(c->hw, c->hw->id.shader_present) != N2HW_OK) {
                                c->prof.pg_fail++;
                                c->reset_pending = true;
                                note(c, "Nord2 GPU core: shader power-up failed; reset");
                                return;
                        }
                        uint64_t t1p = n2c_now_us(c);
                        c->cores_off = false;
                        c->prof.pg_ons++;
                        if (t1p - tp > c->prof.pg_on_max_us)
                                c->prof.pg_on_max_us = (uint32_t)(t1p - tp);
                        if (t1p > c->off_since_us)
                                c->prof.pg_off_us += t1p - c->off_since_us;
                }
                unsigned slot = phase_slot(c, pick);
                /* Serialize CPU maintenance after the older job completed,
                 * before its successor starts.  A job that listed its
                 * buffers cleans those (the producer lists everything the
                 * chain reads); a raw chain that may reach unlisted buffers
                 * (legacy ABI, full list, rollback valve) covers the entire
                 * context. */
                uint64_t t_clean = n2c_now_us(c);
                uint64_t clean_before = c->stats.cache_clean_pages;
                if ((pick->phase == 0 || pick->kind == N2C_JOB_FRAGMENT) && !pick->precleaned) {      /* 0661: precleaned at submit */
                        if (pick->full_maint) {
                                c->stats.cache_full_jobs++;
                                for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                                        struct n2c_bo *b = c->ctxs[pick->ctx].bos[i];
                                        if (!b || b->state != N2C_BO_MAPPED)
                                                continue;
                                        /* A list that was only too long (not the rollback valve) covers the whole context under the same
                                         * tracking rule as a listed buffer: a buffer the CPU never mapped is clean after its first sync. */
                                        if (pick->full_hard || c->track_level == N2C_TRACK_LEGACY || b->cpu_dirty) {
                                                prof_clean(c, b, pick->full_hard ? N2C_CR_FULLHARD : N2C_CR_FULL);
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
                                        if (pick->precise && !n2c_mask_bit(pick->touch_mask, i))
                                                continue;       /* 0632: listed for lifetime only; the chain that touches it cleans it */
                                        if (c->track_level == N2C_TRACK_LEGACY || b->cpu_dirty) {
                                                prof_clean(c, b, clean_reason(c, b));
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
                if (pick_ov)
                        c->lat.ov_starts++;
                pick->slot = slot;
                pick->chain_flag = c->hw->job_chain_flag[slot];   /* n2hw_js_start set JOB_CHAIN_FLAG iff the toggle was clear, then flipped it */
                pick->state = N2C_JOB_RUNNING;
                pick->started_ms = now(c);
                if (pick_next) {
                        /* 0630: waits behind running[slot]; start time, deadline and latency accounting begin when it moves up */
                        pick->in_next = true;
                        c->next[slot] = pick->id;
                        if (oldest[pick->ctx] == c->running[slot])
                                c->lat.nx_same++;
                        else
                                c->lat.nx_cross++;
                        continue;
                }
                if (!pick->t_started) {
                        pick->t_started = true;
                        pick->t_start_us = n2c_now_us(c);
                }
                c->running[slot] = pick->id;
                lat_started(c, pick, slot);
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

/* 0630: one chain of slot s ended (oldest first: running[s]); the NEXT chain, if any, moves up.  failed/status: how it ended. */
static void rb_complete(struct n2c_core *c, unsigned s, bool failed, uint32_t status, uint64_t head, uint64_t t_hw)
{
        struct n2c_job *j = job_by_id(c, c->running[s]);
        c->running[s] = c->next[s];
        c->next[s] = 0;
        struct n2c_job *up = job_by_id(c, c->running[s]);
        if (up) {
                up->in_next = false;
                c->lat.nx_promoted++;
                if (up->state == N2C_JOB_RUNNING) {
                        up->started_ms = now(c);
                        if (!up->t_started) {
                                up->t_started = true;
                                up->t_start_us = t_hw ? t_hw : n2c_now_us(c);
                        }
                }
                if (up->ctx < N2C_MAX_CTX)
                        c->lat_done_us[up->ctx] = 0;
                N2C_LAT_ACC(hand_n, hand_sum, hand_max, 0);     /* the handoff cost nothing: the chain was already in NEXT */
        } else if (c->running[s]) {
                c->running[s] = 0;
                note(c, "Nord2 GPU core: NEXT chain without a job");
        }
        if (!j) {
                note(c, "Nord2 GPU core: job IRQ without a job");
                return;
        }
        if (j->ctx < N2C_MAX_CTX && !(up && up->ctx == j->ctx)) {
                c->lat_done_us[j->ctx] = t_hw ? t_hw : 1;
                c->lat_done_slot[j->ctx] = (uint8_t)s;
        }
        if (!failed && j->state == N2C_JOB_RUNNING) {
                if (j->kind == N2C_JOB_RENDER && j->phase == 0) {
                        j->phase = 1;
                        j->state = N2C_JOB_QUEUED;
                        return;
                }
                job_finish(c, j, N2C_JOB_DONE);
                return;
        }
        j->status = failed ? status : 0x03u;
        j->head_at_fault = head;
        if (failed && status == JS_STATUS_BUS_FAULT && c->hw->has_issue_ttrx_3076)
                c->reset_pending = true; /* corrupt state: reset */
        job_finish(c, j, N2C_JOB_FAULTED);
}

/* 0630: completions of slots that may hold two chains (kbase_job_done).  nr_done = chains we put in the slot - the one still current -
 * the one still waiting in NEXT; every chain but the last reported here passed (a failed chain halts its slot until its bit is cleared). */
static void handle_job_irqs_rb(struct n2c_core *c)
{
        struct n2hw_job_event_rb ev[16];
        uint64_t nh[16];
        for (unsigned s = 0; s < 16; s++) {
                const struct n2c_job *q = c->next[s] ? job_by_id_c(c, c->next[s]) : NULL;
                nh[s] = q ? q->heads[q->phase] : 0;
        }
        unsigned n = n2hw_job_irq_rb(c->hw, nh, ev, 16);
        uint64_t t = n ? n2c_now_us(c) : 0, irq = c->lat_irq_us, t_hw = t;
        c->lat_irq_us = 0;
        if (n && irq && irq <= t) {
                N2C_LAT_ACC(irq_n, irq_sum, irq_max, t - irq);
                t_hw = irq;
        }
        for (unsigned i = 0; i < n; i++) {
                unsigned s = ev[i].slot;
                if (s >= 16)
                        continue;
                if (ev[i].evicted) {
                        struct n2c_job *q = job_by_id(c, c->next[s]);
                        c->next[s] = 0;
                        c->lat.nx_evicted++;
                        if (q) {
                                q->in_next = false;
                                if (q->state == N2C_JOB_RUNNING)
                                        q->state = N2C_JOB_QUEUED;      /* never started: back in the queue, in order */
                                else if (q->state == N2C_JOB_STOPPING)
                                        job_finish(c, q, N2C_JOB_CANCELLED);
                        }
                        if (!q || ev[i].evicted_head != nh[s]) {
                                c->reset_pending = true;
                                note(c, "Nord2 GPU core: NEXT eviction returned an unknown chain; reset");
                        }
                }
                int submitted = (c->running[s] ? 1 : 0) + (c->next[s] ? 1 : 0);
                int nr_done = submitted - (ev[i].cur_active ? 1 : 0) - (ev[i].next_busy && c->next[s] ? 1 : 0);
                if (nr_done <= 0) {
                        c->lat.nx_spurious++;
                        if (!submitted)
                                note(c, "Nord2 GPU core: job IRQ without a job");
                        continue;
                }
                if (nr_done > 1)
                        c->lat.nx_two++;
                while (nr_done > 0) {
                        bool last = nr_done == 1;
                        rb_complete(c, s, last ? ev[i].failed : false, last ? ev[i].status : 0, ev[i].head, t_hw);
                        nr_done--;
                }
        }
        if (n)
                lat_busy(c, t);
}

static void handle_job_irqs(struct n2c_core *c)
{
        if (c->jsnext_rb) {
                handle_job_irqs_rb(c);
                return;
        }
        struct n2hw_job_event ev[16];
        unsigned n = n2hw_job_irq(c->hw, ev, 16);
        /* 0400: hardware completion time = the interrupt top half's stamp when there is one, else now (polling) */
        uint64_t t = n ? n2c_now_us(c) : 0, irq = c->lat_irq_us, t_hw = t;
        c->lat_irq_us = 0;
        if (n && irq && irq <= t) {
                N2C_LAT_ACC(irq_n, irq_sum, irq_max, t - irq);
                t_hw = irq;
        }
        for (unsigned i = 0; i < n; i++) {
                if (ev[i].slot >= 16)
                        continue;
                struct n2c_job *j = job_by_id(c, c->running[ev[i].slot]);
                c->running[ev[i].slot] = 0;
                if (!j) {
                        note(c, "Nord2 GPU core: job IRQ without a job");
                        continue;
                }
                if (j->ctx < N2C_MAX_CTX) {
                        c->lat_done_us[j->ctx] = t_hw ? t_hw : 1;
                        c->lat_done_slot[j->ctx] = (uint8_t)ev[i].slot;
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
        if (n)
                lat_busy(c, t);
}

/* 0750: one record per MMU fault: the address against the context's live buffers (inside / in the guard page behind one) and the
 * ranges unmapped lately, plus its job on the hardware.  Read by the IOKit layer (n2c_fault_get) for the 'NORD2 GPU FAULT:' line. */
static void fault_record(struct n2c_core *c, const struct n2hw_mmu_fault *f, unsigned idx, unsigned cls)
{
        struct n2c_fault_rec *r = &c->fault_ring[c->fault_seq % N2C_FAULT_RING];
        memset(r, 0, sizeof(*r));
        r->seq = ++c->fault_seq;
        r->t_ms = now(c);
        r->address = f->address;
        r->status = f->status;
        r->bus = f->bus_error;
        r->as = (uint8_t)f->as;
        r->cls = (uint8_t)cls;
        r->ctx = idx < N2C_MAX_CTX ? (uint8_t)idx : N2C_AS_NONE;
        if (idx >= N2C_MAX_CTX)
                return;
        struct n2c_ctx *x = &c->ctxs[idx];
        if (x->state == N2C_CTX_FREE || !x->bos)
                return;
        const struct n2c_bo *below = NULL;
        for (unsigned i = 0; i < N2C_MAX_BOS; i++) {
                const struct n2c_bo *b = x->bos[i];
                if (!b || !b->size)
                        continue;
                r->ctx_bos++;
                if (r->where == N2C_FW_INSIDE)
                        continue;       /* found; the rest is only counted */
                if (f->address >= b->va && f->address - b->va < b->size) {
                        below = b;
                        r->where = N2C_FW_INSIDE;
                        continue;
                }
                if (b->va <= f->address && (!below || b->va > below->va))
                        below = b;
        }
        if (below) {
                r->bo_handle = below->handle;
                r->bo_va = below->va;
                r->bo_size = below->size;
                if (r->where == N2C_FW_NONE && f->address - below->va < below->size + BO_GUARD)
                        r->where = N2C_FW_GUARD;
        }
        if (r->where == N2C_FW_NONE) {
                for (unsigned k = 0; k < N2C_FREED_RING; k++) {      /* newest first */
                        const struct n2c_freed *fr = &c->freed[(c->freed_next + N2C_FREED_RING - 1u - k) % N2C_FREED_RING];
                        if (fr->size && fr->ctx == idx && f->address >= fr->va && f->address - fr->va < fr->size + BO_GUARD) {
                                r->where = N2C_FW_FREED;
                                r->bo_handle = fr->handle;
                                r->bo_va = fr->va;
                                r->bo_size = fr->size;
                                r->freed_ago_ms = r->t_ms >= fr->t_ms ? r->t_ms - fr->t_ms : 0;
                                break;
                        }
                }
        }
        for (unsigned k = 0; k < N2C_MAX_JOBS; k++) {
                const struct n2c_job *j = &c->jobs[k];
                if (j->ctx != idx || j->state == N2C_JOB_FREE)
                        continue;
                if (!terminal(j->state))
                        r->ctx_jobs++;
                if ((j->state == N2C_JOB_RUNNING || j->state == N2C_JOB_STOPPING) && !r->kind) {
                        r->kind = j->kind;
                        r->phase = j->phase;
                        r->slot = (uint8_t)j->slot;
                        r->head = j->heads[j->phase & 1u];
                }
        }
}

bool n2c_fault_get(const struct n2c_core *c, uint64_t after, struct n2c_fault_rec *out)
{
        if (!c || !out || c->fault_seq <= after)
                return false;
        uint64_t first = c->fault_seq > N2C_FAULT_RING ? c->fault_seq - N2C_FAULT_RING + 1u : 1u;
        uint64_t want = after + 1u > first ? after + 1u : first;
        *out = c->fault_ring[(want - 1u) % N2C_FAULT_RING];
        return out->seq == want;
}

static void handle_mmu_irqs(struct n2c_core *c)
{
        struct n2hw_mmu_fault f[16];
        unsigned n = n2hw_mmu_irq(c->hw, f, 16);
        for (unsigned i = 0; i < n; i++) {
                c->stats.mmu_faults++;
                /* 0500: the AS number names the context only in static mode (and for AS0); a dynamic AS belongs to as_owner[as]. */
                unsigned idx = f[i].as;
                if (c->as_dyn && f[i].as != 0) {
                        idx = f[i].as < N2C_HW_AS ? c->as_owner[f[i].as] : N2C_AS_NONE;
                        /* 0750: the AS was disabled for this context's fault and it still holds it: a further report of its own stalled
                         * access (the other slot's chain, or the same access again), not an orphan.  Its work is already stopped. */
                        if (idx < N2C_MAX_CTX && c->ctxs[idx].state == N2C_CTX_FAULTED && !c->ctxs[idx].as_live &&
                            c->ctxs[idx].as == f[i].as) {
                                c->as_stats.trail_faults++;
                                fault_record(c, &f[i], idx, N2C_FC_TRAIL);
                                c->ctxs[idx].fault_ms = now(c);
                                c->ctxs[idx].fault_poll = c->poll_seq;
                                continue;
                        }
                        if (idx >= N2C_MAX_CTX || !c->ctxs[idx].as_live || c->ctxs[idx].as != f[i].as) {
                                c->as_stats.orphan_faults++;
                                fault_record(c, &f[i], N2C_AS_NONE, N2C_FC_ORPHAN);
                                note(c, "Nord2 GPU core: MMU fault on an AS no context holds");
                                continue;
                        }
                }
                if (idx >= N2C_MAX_CTX || (!c->as_dyn && idx >= N2C_HW_AS))
                        continue;
                struct n2c_ctx *x = &c->ctxs[idx];
                if (x->state == N2C_CTX_FREE)
                        continue;
                fault_record(c, &f[i], idx, x->state == N2C_CTX_FAULTED ? N2C_FC_TRAIL : N2C_FC_CTX);      /* 0750 (static mode: same AS) */
                x->fault_status = f[i].status;
                x->fault_address = f[i].address;
                x->fault_ms = now(c);   /* 0750 */
                x->fault_poll = c->poll_seq;
                x->faults++;
                if (x->state == N2C_CTX_OPEN)
                        x->state = N2C_CTX_FAULTED;
                /* kbase_mmu_report_fault_and_kill(): stop the context's
                 * work and disable its AS so the stalled access completes
                 * as a fault instead of reaching memory. */
                for (unsigned k = 0; k < N2C_MAX_JOBS; k++) {
                        struct n2c_job *j = &c->jobs[k];
                        if (j->ctx == idx && j->state == N2C_JOB_RUNNING)
                                stop_job(c, j);
                        if (j->ctx == idx && j->state == N2C_JOB_QUEUED && !j->prep)      /* 0661: a job in prep is cancelled by its commit */
                                job_finish(c, j, N2C_JOB_CANCELLED);
                }
                if (x->as_live) {
                        if (n2hw_as_disable(c->hw, x->as) != N2HW_OK)
                                c->reset_pending = true;
                        else
                                x->as_live = false;     /* 0500: the AS stays HELD by the faulted context (as_owner) until its stopped jobs are
                                                         * acknowledged: as_bind never reprograms it under a job still on the hardware, then
                                                         * lends it like any idle AS (close or LRU eviction gives it back) */
                }
        }
}

static void check_deadlines(struct n2c_core *c)
{
        uint64_t t = now(c);
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                struct n2c_job *j = &c->jobs[i];
                if (j->state == N2C_JOB_RUNNING && !j->in_next &&        /* 0630: a chain waiting in NEXT has not started */
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

/* 0750: a client killed by an MMU fault (kbase_mmu_report_fault_and_kill) used to stay FAULTED until it closed: every later submit EIO.
 * For backboardd that is a dead compositor (frames presented from never-rendered buffers = the flashing display) until reboot.  Its
 * faulting work is over once no job of it is queued (prep included) or on the hardware and one whole poll has drained the MMU interrupt
 * without a further report on its AS (trailing reports refresh fault_poll): hand the disabled AS back (as_unbind; the next job rebinds one
 * with UPDATE + FLUSH like any unbound client) and accept submits again.  Bounded: N2C_REVIVE_MAX per N2C_REVIVE_WINDOW_MS. */
static void revive_faulted(struct n2c_core *c)
{
        if (!c->revive_on || !c->as_dyn || c->reset_pending || c->state != N2C_CORE_READY)
                return;
        uint64_t t = now(c);
        for (unsigned i = 1; i < N2C_MAX_CTX; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                if (x->state != N2C_CTX_FAULTED || x->as_live)  /* as_live: the disable failed, the reset owns it */
                        continue;
                if (c->poll_seq - x->fault_poll < 2u || t - x->fault_ms < N2C_REVIVE_QUIET_MS ||
                    ctx_has_active(c, i) || ctx_has_queued(c, i))
                        continue;
                if (!x->revive_win_ms || t - x->revive_win_ms >= N2C_REVIVE_WINDOW_MS) {
                        x->revive_win_ms = t ? t : 1;
                        x->revives = 0;
                        x->revive_held = false;
                }
                if (x->revives >= N2C_REVIVE_MAX) {
                        if (!x->revive_held) {
                                x->revive_held = true;
                                c->as_stats.revive_refused++;
                                note(c, "Nord2 GPU core: faulted context not revived (budget of the 30 s window spent; waits for the window)");
                        }
                        continue;
                }
                as_unbind(c, x);
                x->state = N2C_CTX_OPEN;
                x->revives++;
                c->as_stats.revives++;
                note(c, "Nord2 GPU core: context revived after its MMU fault (0750)");
        }
}

void n2c_poll(struct n2c_core *c)
{
        if (!c || c->state != N2C_CORE_READY)
                return;
        c->poll_seq++;  /* 0750 */
        c->lat_in_irq = true;
        handle_job_irqs(c);
        c->lat_in_irq = false;
        handle_mmu_irqs(c);
        check_deadlines(c);
        if (c->reset_pending)
                (void)n2c_reset(c);
        revive_faulted(c);      /* 0750 */
        try_start(c);
        /* 0666: no job on the hardware or waiting -> after pg_idle_us power the shader cores down (L2 cleaned and kept on, kbase-like) */
        if (c->pg_on && c->state == N2C_CORE_READY && c->gpu_powered && !c->reset_pending && !c->hold_start) {
                bool work = false;
                for (unsigned i = 0; i < N2C_MAX_JOBS && !work; i++) {
                        uint8_t st = c->jobs[i].state;
                        work = st == N2C_JOB_QUEUED || st == N2C_JOB_RUNNING || st == N2C_JOB_STOPPING;
                }
                uint64_t tn = n2c_now_us(c);
                if (work || c->cores_off)
                        c->idle_since_us = 0;
                else if (!c->idle_since_us)
                        c->idle_since_us = tn ? tn : 1;
                else if (tn - c->idle_since_us >= c->pg_idle_us) {
                        if (n2hw_power_off_cores(c->hw) == N2HW_OK) {
                                c->cores_off = true;
                                c->off_since_us = tn;
                                c->prof.pg_offs++;
                        } else {
                                c->prof.pg_fail++;
                                c->pg_on = false;       /* do not try again this boot */
                                note(c, "Nord2 GPU core: shader power-down failed; gating off");
                        }
                        c->idle_since_us = 0;
                }
        }
        /* 0400: the jobs completed above drop their buffer references only now, with the next job already on the GPU: the teardown of
         * released buffers (MMU unmap, AS flush, owner release) used to run between a completion and the start of its successor. */
        bool due = false;
        for (unsigned i = 0; i < N2C_MAX_JOBS && !due; i++)
                due = c->jobs[i].release_due;
        if (!due)
                return;
        uint64_t t0 = n2c_now_us(c);
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                struct n2c_job *j = &c->jobs[i];
                if (!j->release_due)
                        continue;
                j->release_due = false;
                job_release(c, j);
        }
        uint64_t t1 = n2c_now_us(c);
        N2C_LAT_ACC(rel_n, rel_sum, rel_max, t1 >= t0 ? t1 - t0 : 0);
        if (c->state == N2C_CORE_READY && c->reset_pending) {
                (void)n2c_reset(c);     /* a release flush that failed: same poll as before 0400 */
                try_start(c);
        }
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
                if (x->state == N2C_CTX_FREE || !x->bos)
                        continue;       /* 0500: a FREE context has no buffer table */
                for (unsigned s = 0; s < N2C_MAX_BOS; s++) {
                        struct n2c_bo *b = x->bos[s];
                        if (b && b->state == N2C_BO_QUARANTINED)
                                teardown(c, x, b); /* AS no longer live */
                }
                if (x->state == N2C_CTX_ZOMBIE) {
                        free_ctx_memory(c, x);
                        x->state = N2C_CTX_FREE;
                        if (c->as_stats.ctx_open)
                                c->as_stats.ctx_open--;
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
        memset(c->next, 0, sizeof(c->next));    /* 0630: a chain in NEXT is finished below like a running one (RESET) */
        lat_busy(c, n2c_now_us(c));     /* 0400 */
        for (unsigned i = 0; i < N2C_MAX_CTX; i++)
                c->ctxs[i].as_live = false;
        as_unbind_all(c);       /* 0500: dynamic clients rebind at their next job */
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
        c->cores_off = false;           /* 0666: the reset powers them up below */
        if (n2hw_apply_quirks(c->hw) != N2HW_OK ||
            n2hw_power_on_l2(c->hw) != N2HW_OK || wa_start(c) != N2C_OK ||
            n2hw_power_on_cores(c->hw, c->hw->id.shader_present) != N2HW_OK) {
                c->state = N2C_CORE_BROKEN;
                c->stats.reset_failures++;
                return N2C_E_BROKEN;
        }
        for (unsigned i = 0; i < N2C_MAX_CTX; i++) {
                struct n2c_ctx *x = &c->ctxs[i];
                if (x->state != N2C_CTX_OPEN || as_dynamic_ctx(c, i))
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

/* ------------------------------------------------------------------ */
/* 0660 telemetry, 0661 two-phase submit clean, 0662 priorities        */

void n2c_prof_take(struct n2c_core *c, struct n2c_prof *out)
{
        if (!c || !out)
                return;
        *out = c->prof;
        memset(&c->prof, 0, sizeof(c->prof));
}

bool n2c_ctx_set_prio(struct n2c_core *c, unsigned idx, unsigned prio)
{
        struct n2c_ctx *x = c ? ctx_at(c, idx) : NULL;
        if (!x || prio > N2C_PRIO_HIGH)
                return false;
        x->prio = (uint8_t)prio;
        return true;
}

/* Under the lock: the buffers job id will clean (the same rule as try_start's listed path), their dirty generations; false = nothing to do
 * (the job is then committed at once by the caller). */
bool n2c_prep_begin(struct n2c_core *c, unsigned idx, uint64_t id, struct n2c_prep *p)
{
        if (!c || !p)
                return false;
        p->n = 0;
        p->pages = 0;
        p->us = 0;
        struct n2c_job *j = job_by_id(c, id);
        if (!j || j->ctx != idx || !j->prep || j->state != N2C_JOB_QUEUED)
                return false;
        for (unsigned i = 0; i < j->nr_bos && p->n < N2C_PREP_MAX; i++) {
                struct n2c_bo *b = j->bos[i];
                if (b->state != N2C_BO_MAPPED || (b->flags & N2C_BO_CPU_UNCACHED))
                        continue;
                if (j->precise && !n2c_mask_bit(j->touch_mask, i))
                        continue;
                int r = clean_reason(c, b);
                if (r < 0) {
                        c->stats.cache_skip_clean_pages += b->nr_pages;
                        continue;
                }
                p->bo[p->n] = b;
                p->gen[p->n] = b->dirty_gen;
                p->reason[p->n] = (uint8_t)r;
                p->n++;
                p->pages += b->nr_pages;
        }
        return p->n != 0;
}

/* Without the lock: pure CPU cache maintenance of pages the job holds references on (nothing frees them before the commit). */
void n2c_prep_run(struct n2c_core *c, struct n2c_prep *p)
{
        uint64_t t0 = n2c_now_us(c);
        for (unsigned k = 0; k < p->n; k++) {
                struct n2c_bo *b = p->bo[k];
                for (unsigned pg = 0; pg < b->nr_pages; pg++)
                        c->ops.sync(c->ctx, b->pages[pg].pa, b->pages[pg].cpu, NORD2_GPU_PAGE_SIZE, true);
        }
        p->us = n2c_now_us(c) - t0;
}

/* Under the lock again: a buffer marked dirty meanwhile (its generation moved) keeps its mark and the job cleans in try_start as before;
 * otherwise the tracking rule of try_start applies and the job starts without a second clean.  A context that faulted or closed meanwhile:
 * the job is cancelled here. */
void n2c_prep_commit(struct n2c_core *c, unsigned idx, uint64_t id, struct n2c_prep *p)
{
        struct n2c_job *j = c ? job_by_id(c, id) : NULL;
        if (!j || j->ctx != idx || !j->prep)
                return;
        bool all = true;
        for (unsigned k = 0; k < p->n; k++) {
                struct n2c_bo *b = p->bo[k];
                if (b->dirty_gen != p->gen[k]) {
                        all = false;
                        continue;
                }
                prof_clean(c, b, N2C_CR_PRESUBMIT);
                c->stats.cache_clean_pages += b->nr_pages;
                b->cpu_dirty = b->cpu_visible && !b->explicit_track;
        }
        j->clean_pages += (uint32_t)p->pages;
        j->clean_us += (uint32_t)p->us;
        c->prof.preclean_us += p->us;
        j->prep = false;
        j->precleaned = all;
        if (c->ctxs[idx].state != N2C_CTX_OPEN && j->state == N2C_JOB_QUEUED) {
                job_finish(c, j, N2C_JOB_CANCELLED);
                return;
        }
        n2c_poll(c);
}
