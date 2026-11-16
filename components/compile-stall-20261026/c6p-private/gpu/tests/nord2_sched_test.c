#include "nord2_gpu_desc.h"
/*
 * Host ASan/UBSan tests for the Nord 2 job scheduler and quarantine policy.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_mem.h"
#include "nord2_gpu_mmu.h"
#include "nord2_gpu_sched.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BYTES (1u * 1024u * 1024u)
#define MAX_TABLES 64

struct fake {
        uint8_t *arena;
        size_t used;
        uint64_t gpu_base;
        uint64_t now_ms;
        unsigned doorbells;
        unsigned stops;
        unsigned quarantines;
        unsigned freed;
};

struct fake_page {
        struct nord2_gpu_page page;
        struct fake *fake;
};

static void *arena_alloc(struct fake *f, size_t size)
{
        size = (size + 4095u) & ~4095u;
        if (f->used + size > ARENA_BYTES)
                return NULL;
        void *p = f->arena + f->used;
        f->used += size;
        memset(p, 0, size);
        return p;
}

static bool alloc_table(void *ctx, uint64_t *pa, void **cpu)
{
        struct fake *f = ctx;
        void *p = arena_alloc(f, 4096);
        if (!p)
                return false;
        *cpu = p;
        *pa = f->gpu_base + (uint64_t)((uint8_t *)p - f->arena);
        return true;
}

static void free_table(void *ctx, uint64_t pa, void *cpu)
{
        (void)ctx;
        (void)pa;
        (void)cpu;
}
static void flush_table(void *ctx, uint64_t pa, size_t size)
{
        (void)ctx;
        (void)pa;
        (void)size;
}
static bool alloc_host(void *ctx, size_t size, void **cpu)
{
        (void)ctx;
        *cpu = calloc(1, size);
        return *cpu != NULL;
}
static void free_host(void *ctx, void *cpu, size_t size)
{
        (void)ctx;
        (void)size;
        free(cpu);
}

static bool alloc_page(void *ctx, struct nord2_gpu_page *page)
{
        struct fake *f = ctx;
        struct fake_page *p = arena_alloc(f, 4096);
        if (!p)
                return false;
        p->fake = f;
        p->page.pa = f->gpu_base + 0x100000 +
                     (uint64_t)((uint8_t *)p - f->arena);
        p->page.cpu = p;
        page->pa = p->page.pa;
        page->cpu = p->page.cpu;
        return true;
}
static void free_page(void *ctx, const struct nord2_gpu_page *page)
{
        (void)page;
        ((struct fake *)ctx)->freed++;
}
static void sync_page(void *ctx, uint64_t pa, void *cpu, size_t size, bool to_device)
{
        (void)ctx;
        (void)pa;
        (void)size;
        (void)to_device;
}

static bool doorbell(void *ctx, unsigned slot, uint64_t chain_pa,
                     uint64_t job_id)
{
        (void)slot;
        (void)chain_pa;
        (void)job_id;
        ((struct fake *)ctx)->doorbells++;
        return true;
}
static void stop_slot(void *ctx, unsigned slot, bool hard)
{
        (void)slot;
        (void)hard;
        ((struct fake *)ctx)->stops++;
}
static uint64_t now_ms(void *ctx)
{
        return ((struct fake *)ctx)->now_ms;
}
static void quarantine_job(void *ctx, const struct nord2_gpu_job *job,
                           uint32_t reason)
{
        (void)job;
        (void)reason;
        ((struct fake *)ctx)->quarantines++;
}

static int failures;
#define CHECK(cond, msg)                                                        \
        do {                                                                    \
                if (!(cond)) {                                                  \
                        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, \
                                msg);                                           \
                        failures++;                                             \
                }                                                               \
        } while (0)

struct env {
        struct fake fake;
        struct nord2_gpu_mmu mmu;
        struct nord2_gpu_mem mem;
        struct nord2_gpu_sched sched;
};

static bool env_init(struct env *e)
{
        memset(e, 0, sizeof(*e));
        e->fake.arena = calloc(1, ARENA_BYTES);
        e->fake.gpu_base = 0x40000000;
        const struct nord2_gpu_mmu_ops mmu_ops = {
                .alloc_table = alloc_table,
                .free_table = free_table,
                .flush_table = flush_table,
                .alloc_host = alloc_host,
                .free_host = free_host,
        };
        if (!nord2_gpu_mmu_init(&e->mmu, &mmu_ops, &e->fake, MAX_TABLES))
                return false;
        const struct nord2_gpu_mem_ops mem_ops = {
                .alloc_page = alloc_page,
                .free_page = free_page,
                .sync = sync_page,
        };
        if (!nord2_gpu_mem_init(&e->mem, &mem_ops, &e->fake, &e->mmu,
                                0x10000000, 0x10000000 + 0x10000000))
                return false;
        const struct nord2_gpu_sched_ops sched_ops = {
                .doorbell = doorbell,
                .stop_slot = stop_slot,
                .now_ms = now_ms,
                .quarantine_job = quarantine_job,
        };
        return nord2_gpu_sched_init(&e->sched, &sched_ops, &e->fake, &e->mem);
}

static void env_fini(struct env *e)
{
        free(e->fake.arena);
}

static bool make_bo(struct env *e, struct nord2_gpu_bo *bo, struct nord2_gpu_page *pages)
{
        for (unsigned i = 0; i < 2; i++) {
                if (!alloc_page(&e->fake, &pages[i]))
                        return false;
        }
        return nord2_gpu_bo_create(&e->mem, bo, pages, 2,
                                   NORD2_GPU_BO_GPU_READ |
                                           NORD2_GPU_BO_GPU_WRITE,
                                   1, NULL, false);
}

static struct nord2_gpu_job job_for(struct nord2_gpu_bo *bo, unsigned slot)
{
        struct nord2_gpu_job job = {0};
        job.chain_pa = bo->va;
        job.chain_size = 128;
        job.type = NORD2_JOB_COMPUTE;
        job.slot = slot;
        job.owner = 1;
        job.bos[0] = bo;
        job.nr_bos = 1;
        return job;
}

static void test_queue_and_complete_order(void)
{
        struct env e;
        CHECK(env_init(&e), "env init");
        struct nord2_gpu_bo bo_a, bo_b, bo_c;
        struct nord2_gpu_page pa[2], pb[2], pc[2];
        CHECK(make_bo(&e, &bo_a, pa), "bo a");
        CHECK(make_bo(&e, &bo_b, pb), "bo b");
        CHECK(make_bo(&e, &bo_c, pc), "bo c");
        uint64_t ida, idb, idc;
        struct nord2_gpu_job ja = job_for(&bo_a, NORD2_GPU_SLOT_COMPUTE);
        struct nord2_gpu_job jb = job_for(&bo_b, NORD2_GPU_SLOT_COMPUTE);
        struct nord2_gpu_job jc = job_for(&bo_c, NORD2_GPU_SLOT_COMPUTE);
        CHECK(nord2_gpu_sched_submit(&e.sched, &ja, &ida), "submit a");
        CHECK(nord2_gpu_sched_submit(&e.sched, &jb, &idb), "submit b");
        CHECK(nord2_gpu_sched_submit(&e.sched, &jc, &idc), "submit c");
        CHECK(e.sched.running[NORD2_GPU_SLOT_COMPUTE] == ida, "a running");
        CHECK(nord2_gpu_sched_get(&e.sched, idb)->state ==
                      NORD2_GPU_JOB_QUEUED,
              "b queued");
        CHECK(e.fake.doorbells == 1, "one doorbell");

        /* Out-of-order completion on the wrong slot is refused. */
        CHECK(!nord2_gpu_sched_complete(&e.sched, NORD2_GPU_SLOT_FRAGMENT, 0,
                                        0, 0, 0),
              "wrong slot completion");
        CHECK(nord2_gpu_sched_complete(&e.sched, NORD2_GPU_SLOT_COMPUTE, 0, 0,
                                       0, 0),
              "complete a");
        CHECK(nord2_gpu_sched_get(&e.sched, ida)->state == NORD2_GPU_JOB_DONE,
              "a done");
        CHECK(e.sched.running[NORD2_GPU_SLOT_COMPUTE] == idb, "b running");
        CHECK(e.fake.doorbells == 2, "two doorbells");

        /* A fault blocks the slot; the queued job must not start. */
        CHECK(nord2_gpu_sched_complete(&e.sched, NORD2_GPU_SLOT_COMPUTE,
                                       0x42, 0x42, 0xdead, 3),
              "fault b");
        CHECK(nord2_gpu_sched_get(&e.sched, idb)->state ==
                      NORD2_GPU_JOB_FAULTED,
              "b faulted");
        CHECK(e.sched.blocked[NORD2_GPU_SLOT_COMPUTE], "slot blocked");
        CHECK(e.sched.running[NORD2_GPU_SLOT_COMPUTE] == 0, "slot idle");
        CHECK(nord2_gpu_sched_get(&e.sched, idc)->state ==
                      NORD2_GPU_JOB_QUEUED,
              "c still queued");
        CHECK(e.fake.quarantines == 1, "fault quarantined");

        /* Reset cancels the queued job and releases its reference. */
        unsigned changed = nord2_gpu_sched_reset(&e.sched);
        CHECK(changed >= 1, "reset changed");
        CHECK(nord2_gpu_sched_get(&e.sched, idc)->state ==
                      NORD2_GPU_JOB_CANCELLED,
              "c cancelled");
        CHECK(!e.sched.blocked[NORD2_GPU_SLOT_COMPUTE], "unblocked");

        nord2_gpu_sched_close(&e.sched);
        nord2_gpu_mmu_fini(&e.mmu);
        env_fini(&e);
}

static void test_timeout_and_close(void)
{
        struct env e;
        CHECK(env_init(&e), "env init");
        struct nord2_gpu_bo bo;
        struct nord2_gpu_page pages[2];
        CHECK(make_bo(&e, &bo, pages), "bo");
        struct nord2_gpu_job j = job_for(&bo, NORD2_GPU_SLOT_VERTEX_TILER);
        uint64_t id;
        CHECK(nord2_gpu_sched_submit(&e.sched, &j, &id), "submit");
        e.fake.now_ms = 1;
        CHECK(nord2_gpu_sched_tick(&e.sched) == 0, "no early timeout");
        e.fake.now_ms = 100000;
        CHECK(nord2_gpu_sched_tick(&e.sched) == 1, "timeout");
        CHECK(nord2_gpu_sched_get(&e.sched, id)->state ==
                      NORD2_GPU_JOB_TIMED_OUT,
              "timed out");
        CHECK(e.fake.stops == 1, "hard stop");
        CHECK(e.fake.quarantines == 1, "timeout quarantine");

        /* A second tick does not double-fire; close is idempotent. */
        CHECK(nord2_gpu_sched_tick(&e.sched) == 0, "no double timeout");
        CHECK(nord2_gpu_sched_close(&e.sched) >= 0, "close");
        CHECK(nord2_gpu_sched_close(&e.sched) == 0, "close idempotent");
        nord2_gpu_mmu_fini(&e.mmu);
        env_fini(&e);
}

static void test_close_with_queued_work(void)
{
        struct env e;
        CHECK(env_init(&e), "env init");
        struct nord2_gpu_bo bo_a, bo_b;
        struct nord2_gpu_page pa[2], pb[2];
        CHECK(make_bo(&e, &bo_a, pa) && make_bo(&e, &bo_b, pb), "bos");
        struct nord2_gpu_job ja = job_for(&bo_a, NORD2_GPU_SLOT_FRAGMENT);
        struct nord2_gpu_job jb = job_for(&bo_b, NORD2_GPU_SLOT_FRAGMENT);
        uint64_t ida, idb;
        CHECK(nord2_gpu_sched_submit(&e.sched, &ja, &ida), "submit a");
        CHECK(nord2_gpu_sched_submit(&e.sched, &jb, &idb), "submit b");
        CHECK(nord2_gpu_sched_close(&e.sched) == 2, "close both");
        CHECK(nord2_gpu_sched_get(&e.sched, ida)->state ==
                      NORD2_GPU_JOB_CANCELLED,
              "a cancelled");
        CHECK(nord2_gpu_sched_get(&e.sched, idb)->state ==
                      NORD2_GPU_JOB_CANCELLED,
              "b cancelled");
        CHECK(e.fake.quarantines == 1, "running quarantined");
        CHECK(!nord2_gpu_sched_submit(&e.sched, &ja, NULL), "closed refuses");
        nord2_gpu_mmu_fini(&e.mmu);
        env_fini(&e);
}

static void test_reap_reuse_and_fifo(void)
{
        struct env e;
        CHECK(env_init(&e), "env init");
        struct nord2_gpu_bo bo;
        struct nord2_gpu_page pages[2];
        CHECK(make_bo(&e, &bo, pages), "bo");
        struct nord2_gpu_job job = job_for(&bo, NORD2_GPU_SLOT_COMPUTE);
        /* Exercise IDs beyond 32 bits as well as more than the 16 records. */
        e.sched.next_id = UINT64_C(0x100000000);
        uint64_t previous = 0;
        for (unsigned i = 0; i < 1024; i++) {
                uint64_t id = 0;
                CHECK(nord2_gpu_sched_submit(&e.sched, &job, &id), "submit reuse");
                CHECK(id > previous, "monotonic 64-bit job ID");
                CHECK(e.sched.running[job.slot] == id, "full ID retained");
                CHECK(!nord2_gpu_sched_reap(&e.sched, id), "cannot reap running");
                CHECK(nord2_gpu_sched_complete(&e.sched, job.slot, 0, 0, 0, 0),
                      "complete reuse");
                CHECK(nord2_gpu_sched_reap(&e.sched, id), "reap completed");
                CHECK(!nord2_gpu_sched_get(&e.sched, id), "old ID consumed");
                CHECK(!nord2_gpu_sched_reap(&e.sched, id), "reap only once");
                CHECK(bo.refs == 1, "exactly one owner reference after completion");
                previous = id;
        }
        uint64_t a, b, c, d;
        CHECK(nord2_gpu_sched_submit(&e.sched, &job, &a), "submit a");
        CHECK(nord2_gpu_sched_submit(&e.sched, &job, &b), "submit b");
        CHECK(nord2_gpu_sched_submit(&e.sched, &job, &c), "submit c");
        CHECK(nord2_gpu_sched_complete(&e.sched, job.slot, 0, 0, 0, 0), "complete a");
        CHECK(nord2_gpu_sched_reap(&e.sched, a), "reuse lowest index");
        CHECK(nord2_gpu_sched_submit(&e.sched, &job, &d), "submit d in reused index");
        CHECK(nord2_gpu_sched_complete(&e.sched, job.slot, 0, 0, 0, 0), "complete b");
        CHECK(e.sched.running[job.slot] == c, "older c precedes reused d");
        CHECK(nord2_gpu_sched_complete(&e.sched, job.slot, 0, 0, 0, 0), "complete c");
        CHECK(e.sched.running[job.slot] == d, "d last");
        CHECK(nord2_gpu_sched_complete(&e.sched, job.slot, 0, 0, 0, 0), "complete d");
        CHECK(nord2_gpu_sched_reap(&e.sched, b), "reap b");
        CHECK(nord2_gpu_sched_reap(&e.sched, c), "reap c");
        CHECK(nord2_gpu_sched_reap(&e.sched, d), "reap d");
        CHECK(nord2_gpu_bo_release(&e.mem, &bo), "release owner");
        nord2_gpu_mmu_fini(&e.mmu);
        env_fini(&e);
}

int main(void)
{
        CHECK(!strcmp(nord2_gpu_fault_string(0x42), "read fault"),
              "fault string");
        CHECK(!strcmp(nord2_gpu_fault_string(0x1234), "unknown"),
              "unknown fault");
        test_queue_and_complete_order();
        test_timeout_and_close();
        test_close_with_queued_work();
        test_reap_reuse_and_fifo();
        if (failures) {
                fprintf(stderr, "%d failures\n", failures);
                return 1;
        }
        printf("all scheduler tests passed\n");
        return 0;
}
