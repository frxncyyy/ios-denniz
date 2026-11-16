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
#define N2C_MAX_BOS 256u        /* handles per context */
#define N2C_MAX_JOBS 32u        /* records across the device */
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
};

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
};

struct n2c_stats {
        uint64_t submitted, completed, faulted, cancelled, timeouts;
        uint64_t resets, reset_failures, flush_failures;
        uint64_t mmu_faults, quarantined, drained;
        uint64_t bos_live, bytes_live, tables_live;
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
enum n2c_error n2c_job_state(struct n2c_core *c, unsigned ctx, uint64_t id,
                             bool reap, uint32_t *state, uint32_t *status);
/* Verified GPU reset and recovery (all contexts' AS reprogrammed). */
enum n2c_error n2c_reset(struct n2c_core *c);

const char *n2c_error_string(enum n2c_error e);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_CORE_H */
