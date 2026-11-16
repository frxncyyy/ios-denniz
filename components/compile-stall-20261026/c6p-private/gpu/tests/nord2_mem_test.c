/*
 * Host ASan/UBSan tests for Nord 2 buffer ownership, mapping and quarantine.
 */
#include "nord2_gpu_mem.h"
#include "nord2_gpu_mmu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BYTES (1u * 1024u * 1024u)
#define MAX_TABLES 32

struct fake {
        uint8_t *arena;
        size_t used;
        uint64_t gpu_base;
        unsigned owners_freed;
        unsigned pages_freed;
        unsigned syncs;
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
        (void)ctx; (void)pa; (void)cpu;
}
static void flush_table(void *ctx, uint64_t pa, size_t size)
{
        (void)ctx; (void)pa; (void)size;
}
static bool alloc_host(void *ctx, size_t size, void **cpu)
{
        (void)ctx;
        *cpu = calloc(1, size);
        return *cpu != NULL;
}
static void free_host(void *ctx, void *cpu, size_t size)
{
        (void)ctx; (void)size;
        free(cpu);
}
static bool alloc_page(void *ctx, struct nord2_gpu_page *page)
{
        struct fake *f = ctx;
        void *p = arena_alloc(f, 4096);
        if (!p)
                return false;
        page->pa = f->gpu_base + 0x100000 +
                   (uint64_t)((uint8_t *)p - f->arena);
        page->cpu = p;
        return true;
}
static void free_page(void *ctx, const struct nord2_gpu_page *page)
{
        (void)page;
        ((struct fake *)ctx)->pages_freed++;
}
static void sync_page(void *ctx, uint64_t pa, void *cpu, size_t size,
                      bool to_device)
{
        (void)pa; (void)cpu; (void)size; (void)to_device;
        ((struct fake *)ctx)->syncs++;
}
static void free_owner(void *ctx, void *owner)
{
        (void)owner;
        ((struct fake *)ctx)->owners_freed++;
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

int main(void)
{
        struct fake f = {0};
        f.arena = calloc(1, ARENA_BYTES);
        f.gpu_base = 0x50000000;
        const struct nord2_gpu_mmu_ops mo = {
                .alloc_table = alloc_table, .free_table = free_table,
                .flush_table = flush_table, .alloc_host = alloc_host,
                .free_host = free_host,
        };
        const struct nord2_gpu_mem_ops me = {
                .alloc_page = alloc_page, .free_page = free_page,
                .sync = sync_page, .free_owner = free_owner,
        };
        struct nord2_gpu_mmu mmu;
        struct nord2_gpu_mem mem;
        CHECK(nord2_gpu_mmu_init(&mmu, &mo, &f, MAX_TABLES), "mmu init");
        CHECK(nord2_gpu_mem_init(&mem, &me, &f, &mmu, 0x20000000,
                                 0x20000000 + 0x10000), "mem init");

        struct nord2_gpu_page pages[2];
        CHECK(alloc_page(&f, &pages[0]) && alloc_page(&f, &pages[1]), "pages");
        struct nord2_gpu_bo bo;
        CHECK(nord2_gpu_bo_create(&mem, &bo, pages, 2,
                                  NORD2_GPU_BO_GPU_READ |
                                          NORD2_GPU_BO_GPU_WRITE,
                                  7, (void *)0x1234, false), "create");
        CHECK(bo.refs == 1 && bo.va == 0x20000000, "initial");
        CHECK(nord2_gpu_bo_sync(&mem, &bo, true), "sync");
        CHECK(f.syncs == 2, "sync pages");

        nord2_gpu_bo_retain(&bo);
        CHECK(bo.refs == 2, "retain");
        CHECK(nord2_gpu_bo_release(&mem, &bo) && bo.refs == 1, "release one");
        CHECK(f.owners_freed == 0, "owner kept");
        CHECK(nord2_gpu_bo_release(&mem, &bo), "release last");
        CHECK(f.owners_freed == 1, "owner freed");
        CHECK(bo.state == NORD2_GPU_BO_FREE, "free state");
        CHECK(!nord2_gpu_bo_release(&mem, &bo), "double release refused");

        /* Quarantine keeps the mapping/owner until drain. */
        CHECK(alloc_page(&f, &pages[0]), "page 3");
        CHECK(nord2_gpu_bo_create(&mem, &bo, pages, 1,
                                  NORD2_GPU_BO_GPU_WRITE, 8, (void *)0x99,
                                  false), "create 2");
        CHECK(nord2_gpu_quarantine(&mem, &bo, 42, 0x200), "quarantine");
        CHECK(bo.state == NORD2_GPU_BO_QUARANTINED, "state");
        CHECK(!nord2_gpu_bo_release(&mem, &bo), "quarantined release refused");
        CHECK(mem.nr_quarantine == 1, "quarantine count");
        uint64_t pa;
        CHECK(nord2_gpu_mmu_walk(&mmu, bo.va, &pa, NULL), "still mapped");
        CHECK(nord2_gpu_quarantine_drain(&mem) == 1, "drain");
        CHECK(!nord2_gpu_mmu_walk(&mmu, bo.va, &pa, NULL), "unmapped");
        CHECK(f.owners_freed == 2, "quarantine owner freed");

        /* Invalid page alignment and VA exhaustion. */
        struct nord2_gpu_page bad[1] = {{.pa = 0x1001, .cpu = 0}};
        CHECK(!nord2_gpu_bo_create(&mem, &bo, bad, 1, 0, 9, NULL, false),
              "bad page");
        CHECK(!nord2_gpu_bo_create(&mem, &bo, pages, 0, 0, 9, NULL, false),
              "zero pages");
        struct nord2_gpu_page many[17];
        for (unsigned i = 0; i < 17; i++)
                CHECK(alloc_page(&f, &many[i]), "page many");
        CHECK(!nord2_gpu_bo_create(&mem, &bo, many, 17, 0, 9, NULL, false),
              "VA exhaustion");

        nord2_gpu_mmu_fini(&mmu);
        free(f.arena);
        if (failures) {
                fprintf(stderr, "%d failures\n", failures);
                return 1;
        }
        printf("all buffer/quarantine tests passed\n");
        return 0;
}
