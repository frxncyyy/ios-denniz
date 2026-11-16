/*
 * Host tests for the Nord 2 arch9 GPU MMU builder.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_mmu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BYTES (2u * 1024u * 1024u)
#define MAX_TABLES 256

struct arena {
        uint8_t *base;
        size_t used;
        uint64_t gpu_base;
        unsigned flushes;
};

static void *arena_alloc(struct arena *a, size_t size)
{
        size = (size + 4095u) & ~4095u;
        if (a->used + size > ARENA_BYTES)
                return NULL;
        void *p = a->base + a->used;
        a->used += size;
        memset(p, 0, size);
        return p;
}

static bool alloc_table(void *ctx, uint64_t *pa, void **cpu)
{
        struct arena *a = ctx;
        void *p = arena_alloc(a, NORD2_GPU_PAGE_SIZE);
        if (!p)
                return false;
        *cpu = p;
        *pa = a->gpu_base + (uint64_t)((uint8_t *)p - a->base);
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
        (void)pa;
        (void)size;
        ((struct arena *)ctx)->flushes++;
}

static bool alloc_host(void *ctx, size_t size, void **cpu)
{
        struct arena *a = ctx;
        void *p = calloc(1, size);
        (void)a;
        *cpu = p;
        return p != NULL;
}

static void free_host(void *ctx, void *cpu, size_t size)
{
        (void)ctx;
        (void)size;
        free(cpu);
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

static void test_map_walk(void)
{
        struct arena a = {0};
        a.base = calloc(1, ARENA_BYTES);
        a.gpu_base = 0x10000000;
        CHECK(a.base != NULL, "arena");
        const struct nord2_gpu_mmu_ops ops = {
                .alloc_table = alloc_table,
                .free_table = free_table,
                .flush_table = flush_table,
                .alloc_host = alloc_host,
                .free_host = free_host,
        };
        struct nord2_gpu_mmu mmu;
        CHECK(nord2_gpu_mmu_init(&mmu, &ops, &a, MAX_TABLES), "init");
        CHECK(nord2_gpu_mmu_map(&mmu, 0x200000, 0x80000000, 3 * 4096,
                                NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false),
              "map");
        /* Independent hardware-format oracle: r32p1 entry_set_pte uses
         * type 3 for NEXT TABLE, not type 1 (a large block ATE). Do not
         * use nord2_gpu_mmu_walk here: it could share a builder bug. */
        const uint64_t *table = mmu.root_cpu;
        for (unsigned level = 0; level < 3; level++) {
                unsigned index = (0x200000ull >> (39 - level * 9)) & 511;
                uint64_t pte = table[index];
                CHECK((pte & 3) == 3, "r32p1 next-table type must be 3");
                CHECK((pte & (1ull << 10)) != 0, "table access bit");
                uint64_t physical = pte & 0xfffffff000ull;
                CHECK(physical >= a.gpu_base &&
                      physical - a.gpu_base < a.used, "table backing");
                table = (const uint64_t *)(a.base + physical - a.gpu_base);
        }
        CHECK((table[0] & 0xfffffff003ull) == 0x80000003ull,
              "hardware walk reaches expected leaf");
        uint64_t pa = 0, entry = 0;
        CHECK(nord2_gpu_mmu_walk(&mmu, 0x200000, &pa, &entry), "walk0");
        CHECK(pa == 0x80000000, "pa0");
        CHECK((entry & 3) == 3, "leaf type");
        CHECK((entry & (0x7ull << 2)) == (2ull << 2), "memattr");
        CHECK((entry & (1ull << 54)) != 0, "NX");
        CHECK(nord2_gpu_mmu_walk(&mmu, 0x201000, &pa, NULL), "walk1");
        CHECK(pa == 0x80001000, "pa1");
        CHECK(mmu.mapped_pages == 3, "page count");
        CHECK(!nord2_gpu_mmu_walk(&mmu, 0x202000 + 0x1000, &pa, NULL),
              "unmapped walk");
        CHECK(nord2_gpu_mmu_range_free(&mmu, 0x203000, 4096), "range free");

        /* A XNU 16 KiB physical page is four independent 4 KiB GPU leaves. */
        CHECK(nord2_gpu_mmu_map(&mmu, 0x300000, 0x90000000, 4 * 4096,
                                NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false),
              "16K split");
        for (unsigned i = 0; i < 4; i++) {
                uint64_t p = 0;
                CHECK(nord2_gpu_mmu_walk(&mmu, 0x300000 + i * 4096, &p, NULL) &&
                              p == 0x90000000 + i * 4096,
                      "16K leaf");
        }
        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x300000, 4 * 4096), "16K unmap");

        /* Idempotent identical mapping. */
        CHECK(nord2_gpu_mmu_map(&mmu, 0x200000, 0x80000000, 4096,
                                NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false),
              "idempotent map");
        CHECK(mmu.mapped_pages == 3, "idempotent count");

        /* Conflicting mapping must not modify any earlier page. */
        CHECK(!nord2_gpu_mmu_map(&mmu, 0x200000, 0x90000000, 2 * 4096,
                                 NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false),
              "conflict refused");
        CHECK(nord2_gpu_mmu_walk(&mmu, 0x200000, &pa, NULL) &&
                      pa == 0x80000000,
              "conflict preserved");

        /* Misalignment and overflow are refused. */
        CHECK(!nord2_gpu_mmu_map(&mmu, 0x200001, 0x80000000, 4096,
                                 NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false),
              "unaligned va");
        CHECK(!nord2_gpu_mmu_map(&mmu, 0x200000, 0x80000001, 4096,
                                 NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false),
              "unaligned pa");
        CHECK(!nord2_gpu_mmu_map(&mmu, 0x200000, 0x80000000, 0,
                                 NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false),
              "zero size");
        CHECK(!nord2_gpu_mmu_map(&mmu, (1ull << 40) - 4096, 0x80000000,
                                 8192, NORD2_GPU_MEMATTR_WRITE_ALLOC, true,
                                 false),
              "va overflow");

        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x200000, 2 * 4096), "unmap");
        CHECK(!nord2_gpu_mmu_walk(&mmu, 0x200000, &pa, NULL), "unmapped");
        CHECK(nord2_gpu_mmu_walk(&mmu, 0x202000, &pa, NULL), "still mapped");
        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x202000, 4096), "unmap last");
        CHECK(nord2_gpu_mmu_range_free(&mmu, 0x200000, 3 * 4096), "range free 2");

        nord2_gpu_mmu_fini(&mmu);
        free(a.base);
}

static void test_sparse_and_multilevel(void)
{
        struct arena a = {0};
        a.base = calloc(1, ARENA_BYTES);
        a.gpu_base = 0x20000000;
        const struct nord2_gpu_mmu_ops ops = {
                .alloc_table = alloc_table,
                .free_table = free_table,
                .flush_table = flush_table,
                .alloc_host = alloc_host,
                .free_host = free_host,
        };
        struct nord2_gpu_mmu mmu;
        CHECK(nord2_gpu_mmu_init(&mmu, &ops, &a, MAX_TABLES), "init");
        /* Two mappings one 1 GiB apart force multiple level-0/1 tables. */
        CHECK(nord2_gpu_mmu_map(&mmu, 0x1000, 0x1000, 4096,
                                NORD2_GPU_MEMATTR_IMPL_DEF, true, true),
              "map low");
        CHECK(nord2_gpu_mmu_map(&mmu, 0x40000000ull, 0x40000000ull, 4096,
                                NORD2_GPU_MEMATTR_IMPL_DEF, true, true),
              "map high");
        uint64_t pa;
        CHECK(nord2_gpu_mmu_walk(&mmu, 0x1000, &pa, NULL) && pa == 0x1000,
              "low walk");
        CHECK(nord2_gpu_mmu_walk(&mmu, 0x40000000ull, &pa, NULL) &&
                      pa == 0x40000000ull,
              "high walk");
        CHECK(mmu.nr_tables >= 6, "multilevel tables");
        nord2_gpu_mmu_fini(&mmu);
        free(a.base);
}

static void test_table_limit(void)
{
        struct arena a = {0};
        a.base = calloc(1, ARENA_BYTES);
        a.gpu_base = 0x30000000;
        const struct nord2_gpu_mmu_ops ops = {
                .alloc_table = alloc_table,
                .free_table = free_table,
                .flush_table = flush_table,
                .alloc_host = alloc_host,
                .free_host = free_host,
        };
        struct nord2_gpu_mmu mmu;
        CHECK(nord2_gpu_mmu_init(&mmu, &ops, &a, 5), "init limit");
        /* 4 level-3 tables fit exactly after root + one mid-level table?  The
         * bound must stop allocation without corrupting the existing maps. */
        bool any = false;
        for (unsigned i = 0; i < 8; i++) {
                if (!nord2_gpu_mmu_map(&mmu, 0x1000000ull + i * 0x40000000ull,
                                       0x50000000ull + i * 0x40000000ull,
                                       4096, NORD2_GPU_MEMATTR_IMPL_DEF, true,
                                       true))
                        any = true;
        }
        CHECK(any, "limit exercised");
        nord2_gpu_mmu_fini(&mmu);
        free(a.base);
}

static void test_map_pages(void)
{
        struct arena a = {0};
        a.base = calloc(1, ARENA_BYTES);
        a.gpu_base = 0x10000000;
        CHECK(a.base != NULL, "arena");
        const struct nord2_gpu_mmu_ops ops = { .alloc_table = alloc_table, .free_table = free_table, .flush_table = flush_table,
                                               .alloc_host = alloc_host, .free_host = free_host };
        struct nord2_gpu_mmu mmu;
        CHECK(nord2_gpu_mmu_init(&mmu, &ops, &a, MAX_TABLES), "init");
        /* 600 pages that are not contiguous (a level-3 table holds 512) */
        uint64_t pas[600], done = 99;
        for (unsigned i = 0; i < 600; i++)
                pas[i] = 0x80000000ull + (uint64_t)((i * 7919u) % 4000u) * 4096u;
        unsigned flushes0 = a.flushes;
        CHECK(nord2_gpu_mmu_map_pages(&mmu, 0x400000, pas, 600, NORD2_GPU_MEMATTR_IMPL_DEF, NORD2_GPU_SHARE_INNER, true, false, &done) && done == 600,
              "map_pages");
        CHECK(a.flushes - flushes0 <= 6, "one flush of the dirty tables, not one per page");
        for (unsigned i = 0; i < 600; i += 37) {
                uint64_t pa = 0, entry = 0;
                CHECK(nord2_gpu_mmu_walk(&mmu, 0x400000 + (uint64_t)i * 4096u, &pa, &entry) && pa == pas[i], "page-list walk");
        }
        /* the same pages again are idempotent, another page in one slot is refused and nothing changes */
        CHECK(nord2_gpu_mmu_map_pages(&mmu, 0x400000, pas, 600, NORD2_GPU_MEMATTR_IMPL_DEF, NORD2_GPU_SHARE_INNER, true, false, &done) && done == 600, "idempotent");
        uint64_t other[2] = { pas[0], 0x90000000ull };
        CHECK(!nord2_gpu_mmu_map_pages(&mmu, 0x400000, other, 2, NORD2_GPU_MEMATTR_IMPL_DEF, NORD2_GPU_SHARE_INNER, true, false, &done) && done == 0, "conflict refused");
        uint64_t unaligned[1] = { 0x80000123ull };
        CHECK(!nord2_gpu_mmu_map_pages(&mmu, 0x800000, unaligned, 1, NORD2_GPU_MEMATTR_IMPL_DEF, NORD2_GPU_SHARE_INNER, true, false, &done) && done == 0, "unaligned refused");
        CHECK(!nord2_gpu_mmu_map_pages(&mmu, 0x800000, pas, 0, NORD2_GPU_MEMATTR_IMPL_DEF, NORD2_GPU_SHARE_INNER, true, false, &done), "empty refused");
        /* unmap across two level-3 tables, atomically */
        CHECK(!nord2_gpu_mmu_unmap(&mmu, 0x400000, 601 * 4096u), "unmap beyond the mapping refused");
        CHECK(nord2_gpu_mmu_walk(&mmu, 0x400000, NULL, NULL), "still mapped after the refusal");
        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x400000, 600 * 4096u), "unmap");
        CHECK(!nord2_gpu_mmu_walk(&mmu, 0x400000, NULL, NULL) && !nord2_gpu_mmu_walk(&mmu, 0x400000 + 599 * 4096u, NULL, NULL), "unmapped");
        nord2_gpu_mmu_fini(&mmu);
        free(a.base);
}

int main(void)
{
        CHECK(!strcmp(nord2_gpu_mmu_exception_string(0x0), "translation fault"),
              "fault exception");
        CHECK(!strcmp(nord2_gpu_mmu_access_string(0x300), "write"),
              "fault access");
        CHECK(NORD2_MMU_AS_REG(0, NORD2_MMU_AS_TRANSTAB_LO) == 0x2400,
              "AS0 transtab offset");
        CHECK(NORD2_MMU_AS_REG(1, NORD2_MMU_AS_COMMAND) == 0x2458,
              "AS1 command offset");
        test_map_walk();
        test_map_pages();
        test_sparse_and_multilevel();
        test_table_limit();
        if (failures) {
                fprintf(stderr, "%d failures\n", failures);
                return 1;
        }
        printf("all MMU tests passed\n");
        return 0;
}
