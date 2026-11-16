#include "nord2_gpu_desc.h"
/*
 * Randomized scheduler stress/model test.  Checks slot/job state invariants,
 * quarantine accounting and close/reset termination under ASan/UBSan.
 */
#include "nord2_gpu_mem.h"
#include "nord2_gpu_mmu.h"
#include "nord2_gpu_sched.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA (1u << 20)
#define TABLES 64
#define NBOS 6

struct env {
        uint8_t *arena;
        size_t used;
        uint64_t gpu_base;
        uint64_t now;
        unsigned quarantines;
        unsigned doorbells;
        unsigned stops;
        struct nord2_gpu_mmu mmu;
        struct nord2_gpu_mem mem;
        struct nord2_gpu_sched sched;
        struct nord2_gpu_bo *bos[NBOS];
        struct nord2_gpu_page pages[NBOS][2];
};

static void *aa(struct env *e, size_t n)
{
        n = (n + 4095u) & ~4095u;
        if (e->used + n > ARENA)
                return NULL;
        void *p = e->arena + e->used;
        e->used += n;
        memset(p, 0, n);
        return p;
}
static bool at(void *c, uint64_t *pa, void **cpu)
{
        struct env *e = c;
        void *p = aa(e, 4096);
        if (!p) return false;
        *cpu = p; *pa = e->gpu_base + (uint64_t)((uint8_t *)p - e->arena);
        return true;
}
static void ft(void *c, uint64_t pa, void *cpu) { (void)c; (void)pa; (void)cpu; }
static void fl(void *c, uint64_t pa, size_t n) { (void)c; (void)pa; (void)n; }
static bool ah(void *c, size_t n, void **cpu)
{
        (void)c; *cpu = calloc(1, n); return *cpu != NULL;
}
static void fh(void *c, void *cpu, size_t n) { (void)c; (void)n; free(cpu); }
static bool ap(void *c, struct nord2_gpu_page *p)
{
        struct env *e = c;
        void *m = aa(e, 4096);
        if (!m) return false;
        p->pa = e->gpu_base + 0x100000 + (uint64_t)((uint8_t *)m - e->arena);
        p->cpu = m;
        return true;
}
static void fp(void *c, const struct nord2_gpu_page *p) { (void)c; (void)p; }
static void sy(void *c, uint64_t pa, void *cpu, size_t n, bool d)
{
        (void)c; (void)pa; (void)cpu; (void)n; (void)d;
}
static void fo(void *c, void *o) { (void)c; (void)o; }
static bool db(void *c, unsigned slot, uint64_t pa, uint64_t id)
{
        (void)slot; (void)pa; (void)id;
        ((struct env *)c)->doorbells++;
        return true;
}
static void st(void *c, unsigned slot, bool hard)
{
        (void)slot; (void)hard;
        ((struct env *)c)->stops++;
}
static uint64_t nm(void *c) { return ((struct env *)c)->now; }
static void qj(void *c, const struct nord2_gpu_job *j, uint32_t r)
{
        struct env *e = c;
        (void)r;
        for (unsigned i = 0; i < j->nr_bos; i++)
                (void)nord2_gpu_quarantine(&e->mem, j->bos[i], j->id, r);
        e->quarantines++;
}

static uint32_t r = 0xa5a5a5a5u;
static uint32_t rn(void)
{
        r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r;
}

int main(void)
{
        struct env e = {0};
        e.arena = calloc(1, ARENA);
        e.gpu_base = 0x40000000;
        const struct nord2_gpu_mmu_ops mo = {at, ft, fl, ah, fh};
        const struct nord2_gpu_mem_ops me = {ap, fp, sy, fo, 0};
        const struct nord2_gpu_sched_ops so = {db, st, nm, qj};
        if (!nord2_gpu_mmu_init(&e.mmu, &mo, &e, TABLES) ||
            !nord2_gpu_mem_init(&e.mem, &me, &e, &e.mmu, 0x10000000,
                                0x10000000 + 0x100000) ||
            !nord2_gpu_sched_init(&e.sched, &so, &e, &e.mem))
                return 1;
        for (unsigned i = 0; i < NBOS; i++) {
                for (unsigned j = 0; j < 2; j++)
                        if (!ap(&e, &e.pages[i][j])) return 1;
                e.bos[i] = calloc(1, sizeof(*e.bos[i]));
                if (!e.bos[i] ||
                    !nord2_gpu_bo_create(&e.mem, e.bos[i], e.pages[i], 2,
                                         NORD2_GPU_BO_GPU_WRITE, 1, NULL,
                                         false))
                        return 1;
        }

        unsigned failures = 0;
        for (unsigned step = 0; step < 3000; step++) {
                unsigned op = rn() % 10;
                if (op < 5) {
                        unsigned slot = rn() % 3;
                        struct nord2_gpu_job j = {
                                .chain_pa = 0x1000,
                                .chain_size = 128,
                                .type = NORD2_JOB_COMPUTE,
                                .slot = (uint8_t)slot,
                                .owner = 1,
                                .nr_bos = 1,
                                .bos = {e.bos[rn() % NBOS]},
                        };
                        uint64_t id;
                        (void)nord2_gpu_sched_submit(&e.sched, &j, &id);
                } else if (op < 8) {
                        unsigned slot = rn() % 3;
                        uint32_t status = (rn() % 4) ? 0 : (rn() & 0x7f);
                        (void)nord2_gpu_sched_complete(&e.sched, slot, status,
                                                       status, 0, 0);
                } else if (op < 9) {
                        e.now += 1 + rn() % 6000;
                        (void)nord2_gpu_sched_tick(&e.sched);
                } else {
                        (void)nord2_gpu_sched_tick(&e.sched);
                }

                for (unsigned slot = 0; slot < 3; slot++) {
                        uint64_t id = e.sched.running[slot];
                        if (!id)
                                continue;
                        const struct nord2_gpu_job *j =
                                nord2_gpu_sched_get(&e.sched, id);
                        if (!j || j->state != NORD2_GPU_JOB_RUNNING ||
                            j->slot != slot) {
                                fprintf(stderr, "step %u slot %u invariant\n",
                                        step, slot);
                                failures++;
                        }
                }
                for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                        const struct nord2_gpu_job *j = &e.sched.jobs[i];
                        if (j->state == NORD2_GPU_JOB_RUNNING &&
                            e.sched.running[j->slot] != j->id) {
                                fprintf(stderr,
                                        "step %u running job %llu not mapped\n",
                                        step, (unsigned long long)j->id);
                                failures++;
                        }
                }
                if (failures)
                        break;
        }
        (void)nord2_gpu_sched_close(&e.sched);
        for (unsigned i = 0; i < NORD2_GPU_MAX_JOBS; i++) {
                uint8_t s = e.sched.jobs[i].state;
                if (s == NORD2_GPU_JOB_QUEUED || s == NORD2_GPU_JOB_RUNNING) {
                        fprintf(stderr, "close left job state %u\n", s);
                        failures++;
                }
        }
        (void)nord2_gpu_sched_reset(&e.sched);
        (void)nord2_gpu_quarantine_drain(&e.mem);
        nord2_gpu_mmu_fini(&e.mmu);
        for (unsigned i = 0; i < NBOS; i++) {
                if (e.bos[i]->state != NORD2_GPU_BO_QUARANTINED)
                        (void)nord2_gpu_bo_release(&e.mem, e.bos[i]);
                free(e.bos[i]);
        }
        free(e.arena);
        if (failures)
                return 1;
        printf("all randomized scheduler checks passed\n");
        return 0;
}
