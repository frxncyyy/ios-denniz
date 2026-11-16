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

#define N2C_MAX_CTX 8u          /* AS0 kernel + AS1..AS7 clients */
/* Handles per context.  A handle is (serial << N2C_SLOT_BITS) | slot, 32 bits in the ABI (u32 handles[]), so the slot field takes
 * N2C_SLOT_BITS of the serial: 0105 raised the context limit from 256 to 1024 slots (backboardd held all 256 and dropped commands) and
 * the core-wide serial, which never repeats so a stale handle cannot alias, shrank from 24 to 22 bits (4194303 buffers per boot). */
#define N2C_SLOT_BITS 10u
#define N2C_MAX_BOS (1u << N2C_SLOT_BITS)
#define N2C_SLOT_MASK (N2C_MAX_BOS - 1u)
#define N2C_HANDLE_SERIAL_MAX (0xffffffffu >> N2C_SLOT_BITS)
#define N2C_MAX_JOBS 128u       /* records across the device */
#define N2C_CTX_JOBS 96u        /* in-flight (not yet reaped) jobs per context */
#define N2C_JOB_BOS 32u
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
        bool cpu_visible;         /* the CPU can read/write it: user mapping handed out, or an imported surface */
        bool explicit_track;      /* visible, but CPU access is bracketed by acquire/dirty calls (level 2) */
        bool gpu_dirty;           /* tracked: GPU wrote it since the last acquire */
        bool gpu_written;         /* the GPU has written it at least once */
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
        uint8_t as;
        bool as_live;             /* AS programmed with this context */
        uint64_t owner;
        uint32_t generation;
        struct nord2_gpu_mmu mmu;
        struct n2va va;
        struct n2c_bo *bos[N2C_MAX_BOS];
        uint64_t wired, wired_limit;
        uint64_t imported;        /* subset of wired charged to N2C_BO_IMPORTED pages */
        uint32_t jobs;            /* jobs not yet terminal */
        uint32_t fault_status;
        uint64_t fault_address;
};

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
        uint32_t write_mask;
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
};

struct n2c_submit {
        uint32_t kind;
        uint32_t nr_handles;
        uint64_t heads[2];
        uint32_t handles[N2C_JOB_BOS];
        uint32_t write_mask;      /* bit i: GPU may write handles[i] */
        uint32_t flags;           /* N2C_SUBMIT_* */
};

/* The handle list is only a hint: the chain may reach buffers that are not
 * listed (legacy compute ABI following pointers inside FAU/resource tables).
 * CPU cache maintenance then covers every buffer of the context instead of
 * the listed ones.  A list that fills N2C_JOB_BOS is treated the same way,
 * because a producer that ran out of slots cannot list everything. */
#define N2C_SUBMIT_FULL_MAINT (1u << 0)

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
        uint64_t running[16];     /* job id per hardware slot */
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

};

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
enum n2c_error n2c_job_state(struct n2c_core *c, unsigned ctx, uint64_t id,
                             bool reap, uint32_t *state, uint32_t *status);
/* Verified GPU reset and recovery (all contexts' AS reprogrammed). */
enum n2c_error n2c_reset(struct n2c_core *c);

const char *n2c_error_string(enum n2c_error e);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_CORE_H */
