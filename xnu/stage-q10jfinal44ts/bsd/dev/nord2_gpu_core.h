/*
 * Nord 2 GPU core: contexts, address spaces, buffer lifetime and jobs.
 *
 * Model (vendor kbase r32p1 JM): each client context owns one hardware
 * address space; the kernel does not parse job chains, it relies on AS
 * isolation exactly as kbase does (JS_HEAD is a context VA, AS number in
 * JS_CONFIG).  AS0 belongs to the kernel's own diagnostic context.
 *
 * Lifetime rules enforced here:
 *  - a buffer referenced by a queued/running job is never unmapped;
 *  - unmapped pages, VA and page tables are freed only after an
 *    acknowledged AS FLUSH_MEM over the range (kbase_mmu_flush_invalidate
 *    sync), or while the GPU is unpowered (AS UPDATE at power-up invalidates);
 *  - if a flush or a job stop is not acknowledged, the memory moves to a
 *    quarantine that is drained only by a verified GPU reset;
 *  - if that reset fails the core is BROKEN: nothing is ever freed again and
 *    no job is accepted (memory stays pinned until reboot).
 *
 * The core is single-threaded; the IOKit layer serialises every call with
 * one lock and never sleeps while holding it (waits poll and drop it).
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_CORE_H
#define NORD2_GPU_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nord2_gpu_hw.h"
#include "nord2_gpu_mmu.h"
#include "nord2_gpu_va.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0500: contexts and hardware address spaces are decoupled.  The GPU has N2C_HW_AS address spaces (AS0 = the TTRX_3485 workaround / kernel
 * context, AS1..AS7 for clients).  With dynamic AS management (default; n2c_set_as_dynamic) a client context owns its page tables for its
 * whole life but holds a hardware AS only while it needs one: n2c_poll binds one when the context's next job is about to start and, if none
 * is free, takes the least recently used one from a context with no job on the hardware (panfrost_mmu_as_get/put): that AS is drained
 * (FLUSH_MEM over the whole range) and unmapped first, the new owner's TRANSTAB/MEMATTR/TRANSCFG are written with UPDATE and its TLB entries
 * are flushed.  Static mode (the kill switch, boot-arg n2gctx=0 or n2gdv +32) is the old model: context i = AS i, at most 7 clients. */
#define N2C_HW_AS 8u            /* hardware address spaces (AS_PRESENT is checked as well) */
#define N2C_MAX_CTX 64u         /* 0500: software contexts (index 0 = kernel); static mode uses 0..N2C_HW_AS-1 only */
#define N2C_AS_NONE 0xffu       /* x->as of a context that holds no hardware AS */
/* Handles per context.  A handle is (serial << N2C_SLOT_BITS) | slot, 32 bits in the ABI (u32 handles[]), so the slot field takes
 * N2C_SLOT_BITS of the serial: 0105 raised the context limit from 256 to 1024 slots (backboardd held all 256 and dropped commands) and
 * the core-wide serial, which never repeats so a stale handle cannot alias, shrank from 24 to 22 bits (4194303 buffers per boot). */
#define N2C_SLOT_BITS 13u   /* 0186: 8192 slots (was 1024: backboardd/SpringBoard grew to 1024 LIVE buffers in ~3 minutes, ALLOC then failed with N2C_E_NO_HANDLE = EIO = the 10 s freeze); serial 19 bits = 524287 buffers per boot */
#define N2C_MAX_BOS (1u << N2C_SLOT_BITS)
#define N2C_SLOT_MASK (N2C_MAX_BOS - 1u)
#define N2C_HANDLE_SERIAL_MAX (0xffffffffu >> N2C_SLOT_BITS)
#define N2C_MAX_JOBS 128u       /* records across the device */
#define N2C_CTX_JOBS 96u        /* in-flight (not yet reaped) jobs per context */
/* 0661: 128 buffers per job (was 32: backboardd's lists did not fit, so every compositor job took whole-context maintenance and was never
 * precise).  Masks are N2C_MASK_WORDS x 32 bits; n2c_submit keeps write_mask / touch_mask for bits 0..31 and *_hi[] for 32..127. */
#define N2C_JOB_BOS 128u
#define N2C_MASK_WORDS (N2C_JOB_BOS / 32u)
static inline bool n2c_mask_bit(const uint32_t *m, unsigned i) { return (m[i >> 5] >> (i & 31u)) & 1u; }
#define N2C_MAX_TABLES 1024u    /* page tables per context (2 GiB of L3) */
#define N2C_QUARANTINE 64u
#define N2C_VA_BASE 0x0000001000000000ull
#define N2C_VA_LIMIT 0x0000008000000000ull
#define N2C_JOB_TIMEOUT_MS 2000u
#define N2C_STOP_TIMEOUT_MS 100u

enum n2c_bo_flags {
        N2C_BO_GPU_READ = 1u << 0,
        N2C_BO_GPU_WRITE = 1u << 1,
        N2C_BO_GPU_EXEC = 1u << 2,
        N2C_BO_CPU_UNCACHED = 1u << 3,  /* CPU mapping WC/NC: no CPU sync */
        N2C_BO_IMPORTED = 1u << 4,      /* pages owned by e.g. IOSurface */
        N2C_BO_GPU_ONLY = 1u << 5,      /* promise: never mapped for the CPU (the kernel refuses the mapping) */
};

enum n2c_bo_state {
        N2C_BO_FREE = 0,
        N2C_BO_MAPPED,
        N2C_BO_QUARANTINED,
};

enum n2c_ctx_state {
        N2C_CTX_FREE = 0,
        N2C_CTX_OPEN,
        N2C_CTX_FAULTED,    /* MMU fault: AS disabled, submissions refused */
        N2C_CTX_ZOMBIE,     /* close could not be acknowledged; pinned */
};

enum n2c_job_kind {
        N2C_JOB_COMPUTE = 1,   /* chain[0] on the compute slot */
        N2C_JOB_RENDER = 2,    /* chain[0] vertex/tiler, then chain[1] FS */
        N2C_JOB_FRAGMENT = 3,  /* chain[1] only (clear/resolve passes) */
};

enum n2c_job_state {
        N2C_JOB_FREE = 0,
        N2C_JOB_QUEUED,
        N2C_JOB_RUNNING,
        N2C_JOB_STOPPING,   /* stop issued, acknowledgement pending */
        N2C_JOB_DONE,
        N2C_JOB_FAULTED,    /* hardware reported failure (acknowledged) */
        N2C_JOB_CANCELLED,  /* never reached the hardware */
        N2C_JOB_RESET,      /* terminated by a verified GPU reset */
};

enum n2c_core_state {
        N2C_CORE_OFF = 0,
        N2C_CORE_READY,
        N2C_CORE_BROKEN,
};

enum n2c_error {
        N2C_OK = 0,
        N2C_E_ARGS,
        N2C_E_STATE,
        N2C_E_NO_CTX,
        N2C_E_NO_HANDLE,
        N2C_E_NOMEM,
        N2C_E_QUOTA,
        N2C_E_VA,
        N2C_E_MAP,
        N2C_E_BUSY,        /* buffer still referenced by a job */
        N2C_E_RANGE,       /* chain head outside the listed buffers */
        N2C_E_NO_JOB,
        N2C_E_TIMEOUT,
        N2C_E_HW,
        N2C_E_BROKEN,
};

struct n2c_ops {
        bool (*alloc_table)(void *ctx, uint64_t *pa, void **cpu);
        void (*free_table)(void *ctx, uint64_t pa, void *cpu);
        void (*flush_table)(void *ctx, uint64_t pa, size_t size);
        bool (*alloc_host)(void *ctx, size_t size, void **cpu);
        void (*free_host)(void *ctx, void *cpu, size_t size);
        /* CPU cache maintenance for one 4 KiB page. */
        void (*sync)(void *ctx, uint64_t pa, void *cpu, size_t size,
                     bool to_device);
        /* Release whatever keeps the pages alive (descriptor, surface) and
         * the bytes charged to the context quota. */
        void (*free_owner)(void *ctx, void *cookie);
        uint64_t (*now_ms)(void *ctx);
        void (*log)(void *ctx, const char *msg);
        /* Optional microsecond clock for job timing; now_ms()*1000 if NULL. */
        uint64_t (*now_us)(void *ctx);
        /* 0112: identity of the display surface behind an imported buffer's owner cookie (what the framebuffer compares); NULL = not a surface.
         * Optional: the cookie itself if NULL.  Resolved at reservation time only; the result is never dereferenced by the core. */
        const void *(*resv_token)(void *ctx, void *cookie);
};

struct n2c_page {
        uint64_t pa;
        void *cpu;
};

struct n2c_bo {
        uint32_t handle;
        uint8_t ctx;
        uint8_t state;
        uint16_t pad;
        uint32_t flags;
        uint32_t refs;            /* handle reference + one per job */
        bool handle_live;
        uint64_t va, size, mapped_size;
        uint64_t charged;
        struct n2c_page *pages;   /* owned by the core (alloc_host) */
        unsigned nr_pages;
        void *cookie;
        uint64_t tables[8];       /* detached tables awaiting reset */
        unsigned nr_tables;
        /* CPU cache tracking (track_level >= 1).  A buffer the CPU can never touch needs
         * one clean (the kernel's zero fill) and no maintenance after that. */
        bool cpu_dirty;           /* CPU lines may be dirty: clean before the GPU reads */
        uint32_t dirty_gen;       /* 0661: bumped whenever cpu_dirty is set (a clean done outside the lock is kept only if nothing set it meanwhile) */
        bool cpu_visible;         /* the CPU can read/write it: user mapping handed out, or an imported surface */
        bool explicit_track;      /* visible, but CPU access is bracketed by acquire/dirty calls (level 2) */
        bool gpu_dirty;           /* tracked: GPU wrote it since the last acquire */
        bool gpu_written;         /* the GPU has written it at least once */
        bool retained;            /* 0187: released, still mapped for older jobs (counted in ctx->retained) */
        uint64_t rel_id;          /* 0185: c->next_job_id when the client released the handle (0 = still live): only jobs older than this can still address it */
};

/* Cache tracking levels: 0 = every listed buffer is cleaned before and invalidated after every job (0087);
 * 1 = buffers the CPU cannot reach are maintained once, user-visible ones as at level 0;
 * 2 = level 1 plus explicitly tracked visible buffers (IOSurface lock/unlock): clean only after a CPU write,
 *     invalidate only on CPU acquire. */
#define N2C_TRACK_LEGACY 0u
#define N2C_TRACK_PRIVATE 1u
#define N2C_TRACK_EXPLICIT 2u

struct n2c_ctx {
        uint8_t state;
        uint8_t as;               /* hardware AS; N2C_AS_NONE while unbound (dynamic mode) */
        bool as_live;             /* AS programmed with this context */
        uint64_t owner;
        uint32_t generation;
        uint64_t as_stamp;        /* 0500: LRU stamp, set at every bind and job start */
        struct nord2_gpu_mmu mmu;
        /* 0500: allocated at open (ops.alloc_host), NULL while the context is FREE: 64 contexts would otherwise take ~12 MiB inside n2c_core. */
        struct n2va *va;
        struct n2c_bo **bos;      /* N2C_MAX_BOS entries */
        uint64_t wired, wired_limit;
        uint64_t imported;        /* subset of wired charged to N2C_BO_IMPORTED pages */
        uint32_t jobs;            /* jobs not yet terminal */
        uint32_t retained;        /* 0187: released buffers still held for older jobs */
        uint32_t fault_status;
        uint64_t fault_address;
        /* 0660 telemetry: GPU time of the context's jobs since the last n2c_prof_take, queue wait sum / max, jobs */
        uint64_t prof_run_us, prof_q_us;
        uint32_t prof_q_max_us, prof_jobs;
        uint8_t prio;             /* 0662: N2C_PRIO_* (set at open by the IOKit layer from the process name) */
        /* 0750: MMU-fault recovery.  A FAULTED dynamic client gets its submissions back (state OPEN, AS handed back, rebound at its next
         * job) once nothing of it is queued or on the hardware, at least one whole poll has passed without a further report on its AS
         * and N2C_REVIVE_QUIET_MS elapsed; at most N2C_REVIVE_MAX revivals per N2C_REVIVE_WINDOW_MS, after that it waits for the window. */
        uint64_t fault_ms;        /* time of the last fault charged to (or trailing behind) this context */
        uint32_t fault_poll;      /* c->poll_seq at that report */
        uint32_t faults;          /* faults charged to this context */
        uint8_t revives;          /* revivals in the current window */
        bool revive_held;         /* the window's budget is spent (counted once in as_stats.revive_refused) */
        uint64_t revive_win_ms;   /* start of the window (0 = none yet) */
};
#define N2C_REVIVE_QUIET_MS 20u
#define N2C_REVIVE_MAX 3u
#define N2C_REVIVE_WINDOW_MS 30000u
/* 0750: what the core saw for each MMU fault (ring, read by the IOKit layer with n2c_fault_get) */
enum { N2C_FC_CTX = 1, N2C_FC_TRAIL, N2C_FC_ORPHAN };
enum { N2C_FW_NONE = 0, N2C_FW_INSIDE, N2C_FW_GUARD, N2C_FW_FREED };
#define N2C_FAULT_RING 8u
#define N2C_FREED_RING 64u
struct n2c_fault_rec {
        uint64_t seq;             /* 1, 2, ... (0 = empty) */
        uint64_t t_ms, address, head;
        uint32_t status;          /* AS_FAULTSTATUS */
        uint8_t as, ctx, cls;     /* ctx: N2C_AS_NONE when nobody holds the AS */
        uint8_t kind, phase, slot;        /* the context's job on the hardware (kind 0 = none) */
        uint8_t where;            /* N2C_FW_*: the address against the context's buffers */
        bool bus;                 /* bus fault bit */
        uint32_t bo_handle;       /* buffer containing the address, or the nearest one below it (0 = none) */
        uint64_t bo_va, bo_size;
        uint64_t freed_ago_ms;    /* N2C_FW_FREED: how long ago that range was unmapped */
        uint32_t ctx_bos, ctx_jobs;
};
struct n2c_freed { uint64_t va, size, t_ms; uint32_t handle; uint8_t ctx; };
#define N2C_PRIO_LOW 0u
#define N2C_PRIO_NORMAL 1u
#define N2C_PRIO_HIGH 2u

struct n2c_job {
        uint64_t id;
        uint8_t state;
        uint8_t kind;
        uint8_t phase;            /* 0 vertex/compute, 1 fragment */
        uint8_t ctx;
        uint64_t heads[2];
        unsigned slot;            /* slot of the current phase */
        struct n2c_bo *bos[N2C_JOB_BOS];
        unsigned nr_bos;
        uint32_t write_mask[N2C_MASK_WORDS];      /* 0661: 128 bits */
        bool full_maint;          /* cache maintenance covers the whole context */
        bool full_hard;           /* ... unconditionally (rollback valve or explicit flag), not just because the list was full */
        /* Timing (us, n2c ops.now_us), set by submit / try_start / job_finish. */
        uint64_t t_submit_us, t_start_us, t_end_us;
        uint32_t clean_us, inval_us, clean_pages, inval_pages;
        bool t_started;
        uint64_t started_ms, stop_ms;
        uint32_t status;          /* JS_STATUS of the failing phase */
        uint64_t head_at_fault;
        bool released;            /* references dropped */
        bool release_due;         /* 0400: terminal in this n2c_poll, references dropped after try_start */
        bool in_next;             /* 0630: RUNNING but written to the slot's NEXT registers behind running[slot] (not started yet as far as we know) */
        bool precise;             /* 0632: N2C_SUBMIT_PRECISE and a complete list (not full_maint) */
        uint32_t touch_mask[N2C_MASK_WORDS];      /* 0632 (0661: 128 bits) */
        bool prep;                /* 0661: QUEUED but not startable yet: its CPU clean runs in the submitting thread outside the lock */
        bool precleaned;          /* 0661: that clean is done; try_start does not clean again */
        bool chain_flag;          /* 0630: JOB_CHAIN_FLAG it was started with (flag-targeted stops) */
};

struct n2c_submit {
        uint32_t kind;
        uint32_t nr_handles;
        uint64_t heads[2];
        uint32_t handles[N2C_JOB_BOS];
        uint32_t write_mask;      /* bit i: GPU may write handles[i] */
        uint32_t flags;           /* N2C_SUBMIT_* */
        uint32_t touch_mask;      /* 0632, with N2C_SUBMIT_PRECISE: bit i = this chain accesses handles[i] (the others are listed for lifetime only) */
        uint32_t write_mask_hi[N2C_MASK_WORDS - 1u];      /* 0661: bits 32..127 */
        uint32_t touch_mask_hi[N2C_MASK_WORDS - 1u];
};

/* The handle list is only a hint: the chain may reach buffers that are not
 * listed (legacy compute ABI following pointers inside FAU/resource tables).
 * CPU cache maintenance then covers every buffer of the context instead of
 * the listed ones.  A list that fills N2C_JOB_BOS is treated the same way,
 * because a producer that ran out of slots cannot list everything. */
#define N2C_SUBMIT_FULL_MAINT (1u << 0)
/* 0632: the producer (gpupipe1 userland) declares per chain what it touches (touch_mask) and writes (write_mask, a subset), from Mesa's
 * per-stage access flags, and gives every render pass its own tiler heap of a rotation marked as written.  Such a job may start while older
 * FRAGMENT jobs of its context still run or wait, when no buffer one of them writes is touched by the other (kbase-style overlap of pass
 * k+1's vertex/tiler with pass k's fragment). */
#define N2C_SUBMIT_PRECISE (1u << 1)
/* 0661: the producer had more buffers than it could list: whole-context maintenance under the tracking rule (what a full list meant before),
 * never precise.  The IOKit layer sets it for v2/v3 requests that carry the old maximum of 32 handles. */
#define N2C_SUBMIT_TRUNCATED (1u << 2)

/* Job timing aggregated over a window; read and reset with n2c_timing_take().
 * queue = submit -> first phase start (includes the pre-job clean),
 * run = first phase start -> completion noticed by the poller (the GPU's own
 * execution plus up to one polling interval), total = submit -> completion. */
struct n2c_timing {
        uint32_t jobs, faulted, full_jobs;
        uint64_t sum_queue_us, sum_run_us, sum_total_us, sum_clean_us, sum_inval_us;
        uint64_t sum_clean_pages, sum_inval_pages;
        uint32_t max_queue_us, max_run_us, max_total_us, max_clean_us, max_inval_us;
        uint64_t window_start_us;
};

/* Per-job times of a not yet reaped job (for wait-return lag accounting). */
struct n2c_job_times {
        uint64_t submit_us, start_us, end_us;
        uint32_t clean_us, inval_us;
};

struct n2c_stats {
        uint64_t submitted, completed, faulted, cancelled, timeouts;
        uint64_t resets, reset_failures, flush_failures;
        uint64_t mmu_faults, quarantined, drained;
        uint64_t bos_live, bytes_live, tables_live;
        /* CPU cache maintenance, in 4 KiB pages (clean before a job phase,
         * invalidate after completion) and jobs that needed whole-context
         * maintenance. */
        uint64_t cache_clean_pages, cache_inval_pages, cache_full_jobs;
        /* tracking: pages NOT cleaned / NOT invalidated per job, invalidations deferred to a CPU acquire,
         * and pages invalidated by acquire or by a buffer becoming user-visible */
        uint64_t cache_skip_clean_pages, cache_skip_inval_pages, cache_deferred_pages, cache_acquire_pages;
        uint64_t cache_skip_wmask_pages;   /* listed GPU_WRITE buffers the job did not declare in write_mask (n2c_set_wmask_inval) */
        /* 0112 surface reservations: reserved, retired with a completion seq, cancelled (seq 0 or client gone), expired unretired */
        uint64_t resv_reserves, resv_retires, resv_cancels, resv_expired;
};

/* 0112: a reservation tells the framebuffer that jobs NOT YET SUBMITTED will write these surfaces.  Fail-safe by construction: at most 4 per context,
 * an unretired one stops counting after N2C_RESV_EXPIRE_MS, a retired one ends when every job of the context up to its seq is terminal (any terminal
 * state), and closing the context drops them. */
#define N2C_RESV_MAX 8u
#define N2C_RESV_PER_CTX 4u
#define N2C_RESV_SLOTS (N2C_MAX_CTX * N2C_RESV_PER_CTX)
#define N2C_RESV_EXPIRE_MS 200u
struct n2c_resv {
        uint32_t id;              /* 0 = free; per-context monotonic otherwise */
        uint8_t ctx, n;
        uint64_t seq;             /* 0 = not retired yet */
        uint64_t t_ms;            /* reservation time */
        const void *tok[N2C_RESV_MAX];
};

/* 0500: address-space management counters (cumulative). */
struct n2c_as_stats {
        uint64_t binds;           /* AS programmed for a context (dynamic mode) */
        uint64_t evictions;       /* AS taken from an idle context */
        uint64_t bind_waits;      /* a job could not get an AS in this poll (every AS held by a context with work on the hardware) */
        uint64_t bind_failures;   /* disable/configure/flush not acknowledged: reset requested */
        uint64_t orphan_faults;   /* MMU fault on an AS no context holds */
        uint64_t trail_faults;    /* 0750: further report on the AS a FAULTED context still holds (disabled): its own stalled access, not an orphan */
        uint64_t revives;         /* 0750: FAULTED contexts given back their submissions */
        uint64_t revive_refused;  /* 0750: revivals held back because the window's budget was spent */
        uint64_t faulted_refused; /* 0750: submits refused because the context was FAULTED */
        uint64_t ctx_refused;     /* n2c_ctx_open found no free context */
        uint32_t ctx_open, ctx_peak, bound;
};

/* 0400 latency accounting, read and reset with n2c_lat_take() (all us, n2c ops.now_us):
 *  irq   = job interrupt top half (n2c_lat_irq) -> the poll that consumed the completion;
 *  hand  = the job's predecessor in its context finished (or, for a job submitted later, its submission) -> its first phase started;
 *  phase = vertex/tiler phase finished -> fragment phase of the same job started;
 *  depth = unfinished jobs of the context at submit (including the new one);
 *  busy  = time with at least one job slot owned by a job (start -> completion noticed);
 *  rel   = time spent dropping completed jobs' references (buffer teardown) per poll, now after try_start;
 *  same  = starts on the slot the context's previous phase just left (what a JS_*_NEXT queue could hide). */
struct n2c_lat {
        uint32_t irq_n, irq_max, hand_n, hand_max, phase_n, phase_max, depth_max, rel_n, rel_max;
        uint64_t irq_sum, hand_sum, phase_sum, depth_sum, rel_sum, busy_us, submits;
        uint32_t kind[4], same_slot, deferred;
        uint64_t window_start_us;
        /* 0630 (JS NEXT pipelining): chains written behind a running one: same context and slot (hardware keeps the order) / another context;
         * moved up at a completion; taken back (failure of the chain ahead, MMU kill, close); flag-targeted stops; completions that found
         * fewer chains done than expected (kbase 'spurious'); handoffs at zero cost (the successor was already in NEXT). */
        uint32_t nx_same, nx_cross, nx_promoted, nx_evicted, nx_stops, nx_spurious, nx_two;
        uint32_t ov_starts, ov_blocked, precise_jobs;
        uint32_t pr_evicted, pr_aged, pr_reserved;     /* 0662: lower-class NEXT chains taken back for a high one; jobs promoted by age; submits refused to keep the reserve */   /* 0632: jobs started past older fragment jobs; precise jobs held by a conflict; precise submits */
};

/* 0660 telemetry (kept outside n2c_core so C++ sees the same name) */
struct n2c_prof {
        uint64_t clean_pages[6];          /* by reason: N2C_CR_* */
        uint32_t clean_max_pages;         /* biggest single buffer cleaned */
        uint64_t inval_pages;
        uint32_t hist[2][7];              /* run time per slot 0/1: <100 <250 <500 <1000 <4000 <16000 >=16000 us */
        uint64_t preclean_us;             /* time spent cleaning outside the lock */
        uint32_t full_jobs, truncated_jobs, precise_jobs, jobs;
        uint32_t pg_offs, pg_ons, pg_on_max_us, pg_fail;               /* 0666: shader-core power-downs / -ups, slowest power-up, failures */
        uint64_t pg_off_us;                                            /* 0666: time with the shader cores off */
};

struct n2c_core {
        struct n2c_ops ops;
        void *ctx;
        struct n2hw *hw;
        uint8_t state;
        bool gpu_powered;
        bool reset_pending;
        struct n2c_ctx ctxs[N2C_MAX_CTX];
        struct n2c_job jobs[N2C_MAX_JOBS];
        uint64_t next_job_id;
        uint64_t running[16];     /* job id per hardware slot (0630: the chain in the slot's current registers, or the older of two) */
        uint64_t next[16];        /* 0630: job id written to the slot's NEXT registers behind running[slot] (0 = none) */
        bool jsnext;              /* 0630: write a second chain to NEXT while one runs (kill switch: false = one chain per slot, the old path) */
        bool jsnext_rb;           /* 0630: completions are read the two-chain way (set with jsnext before the first job; stays on if jsnext is cleared later) */
        bool pipe;                /* 0632: precise jobs may overtake older non-conflicting fragment jobs of their context (kill switch: false) */
        bool preclean;            /* 0661: CPU cleans of listed buffers run at submit in the submitting thread, outside the lock (kill switch: false) */
        bool prio_on;             /* 0662: compositor-first pick, NEXT take-back, job-record reserve (kill switch: false) */
        bool pg_on;               /* 0666: power the shader cores down after pg_idle_us without work (kbase: 25 x 400 us on MT6893) */
        uint32_t pg_idle_us;
        bool cores_off;
        uint64_t idle_since_us, off_since_us;
        /* 0660 telemetry, read and cleared by n2c_prof_take every 2 s */
        struct n2c_prof prof;
        struct n2c_bo *quarantine[N2C_QUARANTINE];
        unsigned nr_quarantine;
        uint32_t next_handle;
        struct n2c_stats stats;
        struct n2c_timing timing;
        /* Rollback valve (boot-arg nord2_gpu_cache=1): maintain the whole
         * context for every job, as before per-job scoping. */
        bool full_cache_maint;
        uint8_t track_level;      /* N2C_TRACK_*; set before the first buffer exists */
        bool wmask_inval;         /* 0110: a job with a non-zero write_mask invalidates only the buffers it declares; set before the first buffer exists */
        bool resv_off;            /* 0112 kill switch (boot-arg n2resv=0): the selectors answer kIOReturnUnsupported */
        uint32_t resv_next[N2C_MAX_CTX];
        struct n2c_resv resv[N2C_RESV_SLOTS];
        /* Private immutable workaround, separate AS0 MMU, never user mapped. */
        const uint8_t *wa_payload;
        size_t wa_size;
        struct nord2_gpu_mmu wa_mmu;
        uint64_t wa_pa;
        void *wa_cpu;
        bool wa_as_live;

        /* 0400: the completion handler releases buffers AFTER starting the next job (false = old order: release, then start). */
        bool lat_release_late;
        bool lat_in_irq;                  /* inside handle_job_irqs (completions defer their release) */
        bool hold_start;                  /* 0461: no job is started (the GPU DVFS waits for the running ones, at most 50 ms, to change the clock in the gap) */
        volatile uint64_t lat_irq_us;     /* set by the interrupt top half, consumed by the poll */
        uint64_t lat_done_us[N2C_MAX_CTX];  /* last phase completion of each context (0 = none since its last start) */
        uint8_t lat_done_slot[N2C_MAX_CTX];
        uint64_t lat_busy_since;          /* first slot taken while all were free */
        unsigned lat_slots_busy;
        struct n2c_lat lat;
        /* 0500 */
        bool as_dyn;                      /* dynamic AS management (false = static: context i is AS i) */
        uint8_t as_owner[N2C_HW_AS];      /* context holding each AS (dynamic mode), N2C_AS_NONE = free */
        uint64_t as_clock;
        struct n2c_as_stats as_stats;
        /* 0750 */
        bool revive_on;                   /* revive FAULTED dynamic clients (n2c_init: false; the IOKit layer sets kern.n2gfrev, default 1) */
        uint32_t poll_seq;                /* n2c_poll rounds on a READY core */
        uint64_t fault_seq;
        struct n2c_fault_rec fault_ring[N2C_FAULT_RING];
        struct n2c_freed freed[N2C_FREED_RING];
        unsigned freed_next;
};
/* 0750: the oldest fault record with seq > after (false: none).  Telemetry only, call under the core lock. */
bool n2c_fault_get(const struct n2c_core *c, uint64_t after, struct n2c_fault_rec *out);

bool n2c_init(struct n2c_core *c, const struct n2c_ops *ops, void *ctx,
              struct n2hw *hw);
/* Immutable, hash-verified at build time, remains resident for driver life. */
bool n2c_set_wa_payload(struct n2c_core *c, const uint8_t *bytes, size_t size);
/* The board reached READY: workaround, then live contexts and normal jobs. */
enum n2c_error n2c_start(struct n2c_core *c);
/* The board is about to remove power: refuses while jobs are active. */
enum n2c_error n2c_stop(struct n2c_core *c);

enum n2c_error n2c_ctx_open(struct n2c_core *c, uint64_t owner,
                            uint64_t wired_limit, bool kernel,
                            unsigned *ctx_out);
/* Cancels queued work, stops running work (reset if unacknowledged),
 * disables the AS and frees everything, or leaves a pinned ZOMBIE. */
enum n2c_error n2c_ctx_close(struct n2c_core *c, unsigned ctx);

/* Map 4 KiB pages (copied). adopted_out is mandatory: once true, the core
 * owns the cookie even on error, and may have released it or quarantined it.
 * The caller must release the cookie only when adopted_out remains false.
 * Failed operations never expose a usable handle or GPU VA. */
enum n2c_error n2c_bo_map(struct n2c_core *c, unsigned ctx,
                          const struct n2c_page *pages, unsigned nr_pages,
                          uint32_t flags, void *cookie, uint64_t charged,
                          uint32_t *handle_out, uint64_t *va_out,
                          bool *adopted_out);
/* Drop the handle reference; teardown happens at the last reference. */
enum n2c_error n2c_bo_release(struct n2c_core *c, unsigned ctx,
                              uint32_t handle);
const struct n2c_bo *n2c_bo_lookup(const struct n2c_core *c, unsigned ctx,
                                   uint32_t handle);
enum n2c_error n2c_bo_sync(struct n2c_core *c, unsigned ctx, uint32_t handle,
                           bool to_device);

enum n2c_error n2c_submit(struct n2c_core *c, unsigned ctx,
                          const struct n2c_submit *req, uint64_t *id_out);
/* Process hardware events and deadlines; never sleeps. */
void n2c_poll(struct n2c_core *c);
/* Query a job; a terminal job is consumed (record freed) when reap. */
/* Aggregate view of every job of one context with id <= seq (jobs run in
 * submission order per context, so seq = the last submitted id covers the
 * whole chain).  pending counts QUEUED/RUNNING/STOPPING jobs; done/failed count
 * terminal ones; first_failed_* describe the lowest failing id. */
struct n2c_seq_result {
        uint32_t pending, done, failed;
        uint64_t first_failed_id;
        uint32_t first_failed_state, first_failed_status;
};
/* seq must have been issued (N2C_E_NO_JOB otherwise).  With reap, and only when
 * nothing is pending, every terminal job <= seq of the context is released. */
enum n2c_error n2c_seq_state(struct n2c_core *c, unsigned ctx, uint64_t seq,
                             bool reap, struct n2c_seq_result *res);
bool n2c_set_track_level(struct n2c_core *c, unsigned level);
/* 0500: dynamic (true, n2c_init's default) or static address-space management.  Refused while any context exists. */
bool n2c_set_as_dynamic(struct n2c_core *c, bool on);
/* 0630: JS NEXT pipelining.  Before the first job: on/off for good (off = the pre-0630 code paths bit for bit).  Later: only stops or
 * resumes writing new chains to NEXT (completions keep the two-chain reading). */
bool n2c_set_jsnext(struct n2c_core *c, bool on);
/* 0660: clean reasons */
enum { N2C_CR_FULLHARD = 0, N2C_CR_LEGACY, N2C_CR_IMPLICIT, N2C_CR_DIRTY, N2C_CR_FULL, N2C_CR_PRESUBMIT };
void n2c_prof_take(struct n2c_core *c, struct n2c_prof *out);
/* 0661: two-phase submit.  When n2c_submit returns a job with j->prep set, the caller (holding the lock) calls n2c_prep_begin, drops the lock,
 * runs n2c_prep_run (pure CPU cache cleans of buffers the job holds references on), takes the lock again and calls n2c_prep_commit.  The job is
 * not startable in between; nothing else frees or cancels it (an MMU fault leaves it to the commit). */
#define N2C_PREP_MAX N2C_JOB_BOS
struct n2c_prep { unsigned n; struct n2c_bo *bo[N2C_PREP_MAX]; uint32_t gen[N2C_PREP_MAX]; uint8_t reason[N2C_PREP_MAX]; uint64_t pages, us; };
bool n2c_prep_begin(struct n2c_core *c, unsigned idx, uint64_t id, struct n2c_prep *p);
void n2c_prep_run(struct n2c_core *c, struct n2c_prep *p);
void n2c_prep_commit(struct n2c_core *c, unsigned idx, uint64_t id, struct n2c_prep *p);
/* 0662 */
bool n2c_ctx_set_prio(struct n2c_core *c, unsigned idx, unsigned prio);
#define N2C_AGE_HIGH_US 50000u    /* a job waiting longer than this is picked like a compositor job */
#define N2C_RESERVE_HIGH 16u      /* job records kept free for the compositor */
/* 0630: one slot's completion read the kbase_job_done way (nord2_gpu_core.c n2hw_job_irq_rb). */
struct n2hw_job_event_rb {
        unsigned slot;
        bool done, failed;
        uint32_t status;    /* JS_STATUS when failed (0 -> 0x04 TERMINATED), else 0 */
        uint64_t head;      /* JS_HEAD at the time of the IRQ */
        bool cur_active;    /* JOB_IRQ_JS_STATE bit n after the clear (+ a failure raised after RAWSTAT was read: reported next round) */
        bool next_busy;     /* JOB_IRQ_JS_STATE bit 16+n after the clear: a chain still waits in the NEXT registers */
        bool evicted;       /* the NEXT chain was taken back (only on a failure, only if next_head[slot] was given) */
        uint64_t evicted_head;
};
#ifdef N2C_HW_RB_EXTERN
void n2hw_js_stop_chain(struct n2hw *hw, unsigned slot, bool hard, bool flag);
bool n2hw_js_evict_next(struct n2hw *hw, unsigned slot, uint64_t *head);
unsigned n2hw_job_irq_rb(struct n2hw *hw, const uint64_t *next_head, struct n2hw_job_event_rb *events, unsigned max_events);
#endif
/* Opt-in (boot-arg n2wm): trust write_mask as the list of buffers a job writes.  A job with write_mask == 0 keeps the conservative rule
 * (every listed GPU_WRITE buffer).  Refused once buffers exist. */
bool n2c_set_wmask_inval(struct n2c_core *c, bool on);

/* 0112 surface reservations.  RESERVE: handles are this context's imported-surface buffers; n == 0 answers id 0 (availability probe); > 8 or an unknown or
 * non-imported handle: N2C_E_ARGS; more than 4 live reservations in the context: N2C_E_BUSY.  RETIRE: id 0 is a no-op; unknown id: N2C_E_NO_JOB; seq 0 cancels
 * at once; seq must name an issued job (N2C_E_ARGS otherwise).  BUSY: a live reservation names the surface token.  Never blocks. */
enum n2c_error n2c_resv_create(struct n2c_core *c, unsigned idx, const uint32_t *handles, unsigned n, uint32_t *id);
enum n2c_error n2c_resv_retire(struct n2c_core *c, unsigned idx, uint32_t id, uint64_t seq);
bool n2c_resv_busy(struct n2c_core *c, const void *token);
void n2c_set_resv_enabled(struct n2c_core *c, bool on);
/* A user mapping of the buffer was handed out: from now on it is maintained like any CPU-visible buffer. */
enum n2c_error n2c_bo_cpu_visible(struct n2c_core *c, unsigned ctx, uint32_t handle);
/* Bracketed CPU access (level 2, visible buffers only): lock/unlock-style protocol replaces per-job maintenance. */
enum n2c_error n2c_bo_track(struct n2c_core *c, unsigned ctx, uint32_t handle);
enum n2c_error n2c_bo_cpu_dirty(struct n2c_core *c, unsigned ctx, uint32_t handle);     /* CPU wrote (unlock) */
enum n2c_error n2c_bo_cpu_acquire(struct n2c_core *c, unsigned ctx, uint32_t handle,   /* CPU is about to read */
                                  uint32_t *invalidated_pages);
uint64_t n2c_now_us(struct n2c_core *c);
/* Returns false (and leaves *out untouched) while the window holds no finished job or is
 * younger than min_window_us; otherwise copies and resets the window. */
bool n2c_timing_take(struct n2c_core *c, struct n2c_timing *out,
                     uint64_t min_window_us);
enum n2c_error n2c_job_get_times(struct n2c_core *c, unsigned ctx, uint64_t id,
                             struct n2c_job_times *out);
/* 0400: interrupt top half timestamp (any context, no lock: one aligned store). */
static inline void n2c_lat_irq(struct n2c_core *c, uint64_t now_us) { if (c && !c->lat_irq_us) c->lat_irq_us = now_us; }
/* 0400: copy and reset the latency window; false while it is younger than min_window_us or empty. */
bool n2c_lat_take(struct n2c_core *c, struct n2c_lat *out, uint64_t min_window_us);
enum n2c_error n2c_job_state(struct n2c_core *c, unsigned ctx, uint64_t id,
                             bool reap, uint32_t *state, uint32_t *status);
/* Verified GPU reset and recovery (all contexts' AS reprogrammed). */
enum n2c_error n2c_reset(struct n2c_core *c);

const char *n2c_error_string(enum n2c_error e);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_CORE_H */
