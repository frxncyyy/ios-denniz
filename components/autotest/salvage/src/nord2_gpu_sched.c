/*
 * Nord 2 arch9 Job Manager scheduler implementation.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_sched.h"

#include "nord2_gpu_compat.h"

#define DEFAULT_JOB_TIMEOUT_MS 5000u

static struct nord2_gpu_job *find_by_id(struct nord2_gpu_sched *sched,
                                        uint64_t id)
{
        if (!id)
                return NULL;
        for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                if (sched->jobs[i].id == id)
                        return &sched->jobs[i];
        }
        return NULL;
}

static void retain_bos(const struct nord2_gpu_job *job)
{
        for (unsigned i = 0; i < job->nr_bos; i++)
                nord2_gpu_bo_retain(job->bos[i]);
}

static void release_bos(struct nord2_gpu_sched *sched,
                        const struct nord2_gpu_job *job)
{
        if (!sched->mem)
                return;
        for (unsigned i = 0; i < job->nr_bos; i++)
                (void)nord2_gpu_bo_release(sched->mem, job->bos[i]);
}

static bool start_job(struct nord2_gpu_sched *sched, struct nord2_gpu_job *job)
{
        if (!sched->ops.doorbell)
                return false;
        job->state = NORD2_GPU_JOB_RUNNING;
        job->submitted_ms = sched->ops.now_ms ? sched->ops.now_ms(sched->ctx)
                                               : 0;
        job->deadline_ms = job->submitted_ms + DEFAULT_JOB_TIMEOUT_MS;
        sched->running[job->slot] = job->id;
        if (!sched->ops.doorbell(sched->ctx, job->slot, job->chain_pa,
                                  job->id)) {
                job->state = NORD2_GPU_JOB_FAULTED;
                job->status = 0xffffffffu;
                sched->running[job->slot] = 0;
                sched->blocked[job->slot] = true;
                sched->faults++;
                if (sched->ops.quarantine_job)
                        sched->ops.quarantine_job(sched->ctx, job,
                                                   0x100 /* submission */);
                return false;
        }
        return true;
}

static void start_next_for_slot(struct nord2_gpu_sched *sched, unsigned slot)
{
        if (sched->closing || sched->blocked[slot])
                return;
        /* A reaped record can be reused below an older queued record.
         * Preserve submission order independently of the array index. */
        struct nord2_gpu_job *oldest = NULL;
        for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                struct nord2_gpu_job *job = &sched->jobs[i];
                if (job->state == NORD2_GPU_JOB_QUEUED && job->slot == slot &&
                    (!oldest || job->id < oldest->id))
                        oldest = job;
        }
        if (oldest)
                (void)start_job(sched, oldest);
}

bool nord2_gpu_sched_init(struct nord2_gpu_sched *sched,
                          const struct nord2_gpu_sched_ops *ops, void *ctx,
                          struct nord2_gpu_mem *mem)
{
        if (!sched || !ops || !ops->doorbell || !ops->stop_slot ||
            !ops->now_ms || !mem)
                return false;
        memset(sched, 0, sizeof(*sched));
        sched->ops = *ops;
        sched->ctx = ctx;
        sched->mem = mem;
        sched->next_id = 1;
        return true;
}

bool nord2_gpu_sched_submit(struct nord2_gpu_sched *sched,
                            const struct nord2_gpu_job *job, uint64_t *id_out)
{
        if (!sched || !job || !job->chain_pa || !job->chain_size ||
            job->slot >= NORD2_GPU_SLOT_COUNT ||
            job->nr_bos > NORD2_GPU_MAX_JOB_BOS || sched->closing ||
            sched->next_id == 0)
                return false;
        unsigned index = NORD2_GPU_MAX_JOBS;
        for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                if (sched->jobs[i].state == NORD2_GPU_JOB_FREE) {
                        index = i;
                        break;
                }
        }
        if (index == NORD2_GPU_MAX_JOBS)
                return false;
        struct nord2_gpu_job *slot = &sched->jobs[index];
        memset(slot, 0, sizeof(*slot));
        *slot = *job;
        slot->id = sched->next_id++;
        slot->state = NORD2_GPU_JOB_QUEUED;
        retain_bos(slot);
        if (id_out)
                *id_out = slot->id;
        if (sched->running[job->slot] == 0 && !sched->blocked[job->slot])
                (void)start_job(sched, slot);
        return true;
}

bool nord2_gpu_sched_complete(struct nord2_gpu_sched *sched, unsigned slot,
                              uint32_t status, uint32_t fault_status,
                              uint64_t fault_address,
                              uint32_t first_incomplete_task)
{
        if (!sched || slot >= NORD2_GPU_SLOT_COUNT)
                return false;
        struct nord2_gpu_job *job = find_by_id(sched, sched->running[slot]);
        if (!job || job->state != NORD2_GPU_JOB_RUNNING)
                return false;
        job->status = status;
        job->fault_status = fault_status;
        job->fault_address = fault_address;
        job->first_incomplete_task = first_incomplete_task;
        if (status == 0) {
                job->state = NORD2_GPU_JOB_DONE;
                sched->completed++;
                release_bos(sched, job);
        } else {
                job->state = NORD2_GPU_JOB_FAULTED;
                sched->faults++;
                sched->blocked[slot] = true;
                if (sched->ops.quarantine_job)
                        sched->ops.quarantine_job(sched->ctx, job,
                                                   fault_status ? fault_status
                                                                : status);
        }
        sched->running[slot] = 0;
        start_next_for_slot(sched, slot);
        return true;
}

unsigned nord2_gpu_sched_tick(struct nord2_gpu_sched *sched)
{
        if (!sched || !sched->ops.now_ms)
                return 0;
        uint64_t now = sched->ops.now_ms(sched->ctx);
        unsigned fired = 0;
        for (unsigned slot = 0; slot < NORD2_GPU_SLOT_COUNT; slot++) {
                struct nord2_gpu_job *job =
                        find_by_id(sched, sched->running[slot]);
                if (!job || job->state != NORD2_GPU_JOB_RUNNING)
                        continue;
                if (now < job->deadline_ms)
                        continue;
                /* The slot is stopped but the completion status is unknown:
                 * quarantine and do not start another job until reset. */
                sched->ops.stop_slot(sched->ctx, slot, true);
                job->state = NORD2_GPU_JOB_TIMED_OUT;
                sched->running[slot] = 0;
                sched->blocked[slot] = true;
                sched->timeouts++;
                fired++;
                if (sched->ops.quarantine_job)
                        sched->ops.quarantine_job(sched->ctx, job, 0x200);
        }
        return fired;
}

static unsigned fail_job(struct nord2_gpu_sched *sched,
                         struct nord2_gpu_job *job, uint8_t state)
{
        if (job->state == NORD2_GPU_JOB_QUEUED) {
                release_bos(sched, job);
                job->state = state;
                sched->cancelled++;
                return 1;
        }
        if (job->state == NORD2_GPU_JOB_RUNNING) {
                sched->ops.stop_slot(sched->ctx, job->slot, true);
                job->state = state;
                if (sched->ops.quarantine_job)
                        sched->ops.quarantine_job(sched->ctx, job, 0x300);
                sched->running[job->slot] = 0;
                sched->cancelled++;
                return 1;
        }
        return 0;
}

unsigned nord2_gpu_sched_close(struct nord2_gpu_sched *sched)
{
        if (!sched)
                return 0;
        unsigned changed = 0;
        sched->closing = true;
        for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                changed += fail_job(sched, &sched->jobs[i],
                                    NORD2_GPU_JOB_CANCELLED);
        }
        return changed;
}

unsigned nord2_gpu_sched_reset(struct nord2_gpu_sched *sched)
{
        if (!sched)
                return 0;
        unsigned changed = 0;
        for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                struct nord2_gpu_job *job = &sched->jobs[i];
                if (job->state == NORD2_GPU_JOB_RUNNING ||
                    job->state == NORD2_GPU_JOB_TIMED_OUT ||
                    job->state == NORD2_GPU_JOB_FAULTED) {
                        if (job->state == NORD2_GPU_JOB_RUNNING)
                                sched->ops.stop_slot(sched->ctx, job->slot,
                                                      true);
                        job->state = NORD2_GPU_JOB_QUARANTINED;
                        if (sched->ops.quarantine_job)
                                sched->ops.quarantine_job(sched->ctx, job,
                                                           0x301);
                        changed++;
                } else if (job->state == NORD2_GPU_JOB_QUEUED) {
                        release_bos(sched, job);
                        job->state = NORD2_GPU_JOB_CANCELLED;
                        sched->cancelled++;
                        changed++;
                }
        }
        memset(sched->running, 0, sizeof(sched->running));
        memset(sched->blocked, 0, sizeof(sched->blocked));
        sched->closing = false;
        return changed;
}

const struct nord2_gpu_job *nord2_gpu_sched_get(const struct nord2_gpu_sched *sched,
                                                uint64_t id)
{
        if (!sched || !id)
                return NULL;
        for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                if (sched->jobs[i].id == id)
                        return &sched->jobs[i];
        }
        return NULL;
}

bool nord2_gpu_sched_reap(struct nord2_gpu_sched *sched, uint64_t id)
{
        if (!sched)
                return false;
        struct nord2_gpu_job *job = find_by_id(sched, id);
        if (!job || job->state != NORD2_GPU_JOB_DONE)
                return false;
        /* complete() already released the BOs. Never dereference them here. */
        memset(job, 0, sizeof(*job));
        return true;
}

const char *nord2_gpu_fault_string(uint32_t status)
{
        switch (status) {
        case 0x00: return "no exception";
        case 0x02: return "interrupted";
        case 0x03: return "stopped";
        case 0x04: return "terminated";
        case 0x40: return "configuration fault";
        case 0x41: return "power fault";
        case 0x42: return "read fault";
        case 0x43: return "write fault";
        case 0x44: return "affinity fault";
        case 0x48: return "bus fault";
        case 0x50: return "instruction invalid PC";
        case 0x51: return "instruction invalid encoding";
        case 0x52: return "instruction type mismatch";
        case 0x53: return "instruction operand fault";
        case 0x54: return "instruction TLS fault";
        case 0x55: return "instruction barrier fault";
        case 0x56: return "instruction alignment fault";
        case 0x58: return "data invalid fault";
        case 0x59: return "tile range fault";
        case 0x5a: return "address range fault";
        case 0x60: return "out of memory";
        case 0x7f: return "unknown";
        default: return "unknown";
        }
}
