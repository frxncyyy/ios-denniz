/*
 * Nord 2 arch9 Job Manager scheduler core.
 *
 * This module owns the job state machine, not the hardware registers.  It is
 * intentionally independent of XNU so that close-during-async-work, timeout,
 * out-of-order completion and quarantine behaviour can be tested under
 * ASan/UBSan on the host.  The kernel adapter supplies doorbell/stop/time and
 * a quarantine callback that pins buffers until a verified GPU reset.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_SCHED_H
#define NORD2_GPU_SCHED_H

#include <stdbool.h>
#include <stdint.h>

#include "nord2_gpu_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

enum nord2_gpu_slot {
        NORD2_GPU_SLOT_VERTEX_TILER = 0,
        NORD2_GPU_SLOT_FRAGMENT = 1,
        NORD2_GPU_SLOT_COMPUTE = 2,
        NORD2_GPU_SLOT_COUNT = 3,
};

enum nord2_gpu_job_state {
        NORD2_GPU_JOB_FREE = 0,
        NORD2_GPU_JOB_QUEUED,
        NORD2_GPU_JOB_RUNNING,
        NORD2_GPU_JOB_DONE,
        NORD2_GPU_JOB_FAULTED,
        NORD2_GPU_JOB_TIMED_OUT,
        NORD2_GPU_JOB_QUARANTINED,
        NORD2_GPU_JOB_CANCELLED,
};

struct nord2_gpu_job {
        uint64_t id;
        uint64_t chain_pa;
        uint32_t chain_size;
        uint32_t type;
        uint8_t slot;
        uint8_t state;
        uint64_t owner;
        uint64_t submitted_ms;
        uint64_t deadline_ms;
        uint32_t status;
        uint32_t fault_status;
        uint64_t fault_address;
        uint32_t first_incomplete_task;
        struct nord2_gpu_bo *bos[NORD2_GPU_MAX_JOB_BOS];
        unsigned nr_bos;
};

struct nord2_gpu_sched_ops {
        /* Program JS_HEAD_NEXT/JS_CONFIG_NEXT and start the slot.  Must not
         * block. */
        bool (*doorbell)(void *ctx, unsigned slot, uint64_t chain_pa,
                         uint64_t job_id);
        /* Issue SOFT_STOP/HARD_STOP for the slot.  Must not block. */
        void (*stop_slot)(void *ctx, unsigned slot, bool hard);
        uint64_t (*now_ms)(void *ctx);
        /* Called without the scheduler lock held (the adapter owns locking).
         * The callback must quarantine every BO of the job. */
        void (*quarantine_job)(void *ctx, const struct nord2_gpu_job *job,
                               uint32_t reason);
        void (*log)(void *ctx, const char *msg);
};

struct nord2_gpu_sched {
        struct nord2_gpu_sched_ops ops;
        void *ctx;
        struct nord2_gpu_mem *mem;
        struct nord2_gpu_job jobs[NORD2_GPU_MAX_JOBS];
        uint64_t running[NORD2_GPU_SLOT_COUNT];
        bool blocked[NORD2_GPU_SLOT_COUNT];
        uint64_t next_id;
        uint64_t completed;
        uint64_t faults;
        uint64_t timeouts;
        uint64_t cancelled;
        bool closing;
};

bool nord2_gpu_sched_init(struct nord2_gpu_sched *sched,
                          const struct nord2_gpu_sched_ops *ops, void *ctx,
                          struct nord2_gpu_mem *mem);

/* Submit a job.  The scheduler retains every BO before returning.  The chain
 * buffer itself must already be pinned and valid until completion. */
bool nord2_gpu_sched_submit(struct nord2_gpu_sched *sched,
                            const struct nord2_gpu_job *job, uint64_t *id_out);

/* Hardware completion.  status == 0 means success; nonzero is the raw
 * JS_STATUS.  Unknown/stuck completion paths call nord2_gpu_sched_timeout. */
bool nord2_gpu_sched_complete(struct nord2_gpu_sched *sched, unsigned slot,
                              uint32_t status, uint32_t fault_status,
                              uint64_t fault_address,
                              uint32_t first_incomplete_task);

/* Advance deadlines; returns the number of jobs that timed out. */
unsigned nord2_gpu_sched_tick(struct nord2_gpu_sched *sched);

/* Process close: stop all slots, cancel queued jobs and quarantine every
 * running/unknown job.  Safe to call twice. */
unsigned nord2_gpu_sched_close(struct nord2_gpu_sched *sched);

/* After a verified GPU reset, queued jobs are failed and quarantined jobs may
 * be released by the memory layer.  Running jobs are moved to quarantine. */
unsigned nord2_gpu_sched_reset(struct nord2_gpu_sched *sched);

const struct nord2_gpu_job *nord2_gpu_sched_get(const struct nord2_gpu_sched *sched,
                                                uint64_t id);

/* Consume a successfully completed result after the caller has copied it.
 * Only DONE may be reclaimed; running/faulted/quarantined jobs remain pinned.
 * The adapter must serialize this with submit/complete/get. */
bool nord2_gpu_sched_reap(struct nord2_gpu_sched *sched, uint64_t id);

/* Human-readable decoding of the vendor r32p1 JS_STATUS values.  Unknown
 * values return "unknown"; the raw word is never hidden. */
const char *nord2_gpu_fault_string(uint32_t status);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_SCHED_H */
