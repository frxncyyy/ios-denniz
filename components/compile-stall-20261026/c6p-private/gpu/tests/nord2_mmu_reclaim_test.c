/*
 * Page-table reclamation: detach/free two-phase protocol, leak bounds and
 * an independent raw walker over the produced table memory.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_mmu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { failures++; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

#define POOL 2048u
static uint8_t (*pool)[4096];
static bool used[POOL];
static unsigned live, frees, flushes;
#define POOL_PA 0x80000000ull

static bool alloc_table(void *ctx, uint64_t *pa, void **cpu)
{
        (void)ctx;
        for (unsigned i = 0; i < POOL; i++) {
                if (used[i])
                        continue;
                used[i] = true;
                memset(pool[i], 0, 4096);
                *pa = POOL_PA + (uint64_t)i * 4096;
                *cpu = pool[i];
                live++;
                return true;
        }
        return false;
}
static void free_table(void *ctx, uint64_t pa, void *cpu)
{
        (void)ctx;
        unsigned i = (unsigned)((pa - POOL_PA) / 4096);
        if (i >= POOL || !used[i] || cpu != pool[i]) {
                failures++;
                fprintf(stderr, "FAIL bad free %llx\n", (unsigned long long)pa);
                return;
        }
        memset(pool[i], 0xa5, 4096); /* poison: a stale walk would see junk */
        used[i] = false;
        live--;
        frees++;
}
static void flush(void *ctx, uint64_t pa, size_t size) { (void)ctx; (void)pa; (void)size; flushes++; }
static bool alloc_host(void *ctx, size_t size, void **cpu) { (void)ctx; *cpu = calloc(1, size); return *cpu; }
static void free_host(void *ctx, void *cpu, size_t size) { (void)ctx; (void)size; free(cpu); }

/* Independent walker straight over pool memory using the vendor format. */
static bool raw_walk(uint64_t root, uint64_t va, uint64_t *pa)
{
        uint64_t table = root;
        for (unsigned level = 0; level < 4; level++) {
                unsigned shift = 12 + (3 - level) * 9;
                unsigned idx = (unsigned)((va >> shift) & 511);
                unsigned slot = (unsigned)((table - POOL_PA) / 4096);
                if (slot >= POOL || !used[slot])
                        return false;
                uint64_t e;
                memcpy(&e, &pool[slot][idx * 8], 8);
                if ((e & 3) != 3)
                        return false;
                if (level == 3) {
                        *pa = e & 0xfffffff000ull;
                        return true;
                }
                if (!(e & (1ull << 10)))
                        return false;
                table = e & 0xfffffff000ull;
        }
        return false;
}

#define SLOTS 96u
static uint64_t slot_va(unsigned s)
{
        /* Spread over several 2 MiB, 1 GiB and 512 GiB regions. */
        /* 2 L0 entries x 6 GiB x 8 2-MiB spans = 96 distinct slots, all
         * inside the 40-bit GPU VA space. */
        return 0x1000000000ull + (uint64_t)(s % 2) * 0x8000000000ull +
               (uint64_t)((s / 2) % 6) * 0x40000000ull +
               (uint64_t)(s / 12) * 0x200000ull + 0x3000ull * (s % 3);
}

static unsigned expected_tables(const bool *mapped, const unsigned *pages)
{
        /* Distinct L1 (512 GiB), L2 (1 GiB), L3 (2 MiB) spans + root. */
        uint64_t spans[3][SLOTS * 4];
        unsigned n[3] = {0, 0, 0};
        const uint64_t size[3] = {1ull << 39, 1ull << 30, 1ull << 21};
        for (unsigned s = 0; s < SLOTS; s++) {
                if (!mapped[s])
                        continue;
                for (unsigned p = 0; p < pages[s]; p++) {
                        uint64_t va = slot_va(s) + (uint64_t)p * 4096;
                        for (unsigned l = 0; l < 3; l++) {
                                uint64_t key = va / size[l];
                                bool seen = false;
                                for (unsigned i = 0; i < n[l]; i++)
                                        seen |= spans[l][i] == key;
                                if (!seen)
                                        spans[l][n[l]++] = key;
                        }
                }
        }
        return 1 + n[0] + n[1] + n[2];
}

int main(void)
{
        pool = aligned_alloc(4096, (size_t)POOL * 4096);
        const struct nord2_gpu_mmu_ops ops = { alloc_table, free_table, flush,
                                               alloc_host, free_host, NULL };
        struct nord2_gpu_mmu mmu;
        CHECK(nord2_gpu_mmu_init(&mmu, &ops, NULL, POOL));

        /* Deterministic: one page, unmap, detach cascades to root only. */
        CHECK(nord2_gpu_mmu_map(&mmu, 0x1000000000ull, 0x40000000, 4096,
                                NORD2_GPU_MEMATTR_IMPL_DEF, true, false));
        CHECK(nord2_gpu_mmu_live_tables(&mmu) == 4 && live == 4);
        uint64_t out[64];
        CHECK(nord2_gpu_mmu_detach_empty(&mmu, 0x1000000000ull, 4096, out, 64) == 0);
        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x1000000000ull, 4096));
        unsigned n = nord2_gpu_mmu_detach_empty(&mmu, 0x1000000000ull, 4096, out, 64);
        CHECK(n == 3 && nord2_gpu_mmu_live_tables(&mmu) == 1);
        /* Detached but not yet freed: still allocated (GPU may hold them). */
        CHECK(live == 4);
        uint64_t pa;
        CHECK(!raw_walk(mmu.root_pa, 0x1000000000ull, &pa));
        /* Re-map before free uses fresh tables, never the detached ones. */
        CHECK(nord2_gpu_mmu_map(&mmu, 0x1000000000ull, 0x50000000, 4096,
                                NORD2_GPU_MEMATTR_IMPL_DEF, true, false));
        CHECK(live == 7 && raw_walk(mmu.root_pa, 0x1000000000ull, &pa) &&
              pa == 0x50000000);
        CHECK(nord2_gpu_mmu_free_detached(&mmu, out, n) == 3 && live == 4);
        CHECK(nord2_gpu_mmu_free_detached(&mmu, out, n) == 0); /* double */
        CHECK(raw_walk(mmu.root_pa, 0x1000000000ull, &pa) && pa == 0x50000000);
        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x1000000000ull, 4096));
        n = nord2_gpu_mmu_detach_empty(&mmu, 0x1000000000ull, 4096, out, 64);
        CHECK(nord2_gpu_mmu_free_detached(&mmu, out, n) == 3 && live == 1);

        /* Partially used L3 is kept. */
        CHECK(nord2_gpu_mmu_map(&mmu, 0x2000000000ull, 0x40000000, 8192,
                                NORD2_GPU_MEMATTR_IMPL_DEF, true, false));
        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x2000000000ull, 4096));
        CHECK(nord2_gpu_mmu_detach_empty(&mmu, 0x2000000000ull, 4096, out, 64) == 0);
        CHECK(raw_walk(mmu.root_pa, 0x2000001000ull, &pa) && pa == 0x40001000);
        CHECK(nord2_gpu_mmu_unmap(&mmu, 0x2000001000ull, 4096));
        n = nord2_gpu_mmu_detach_empty(&mmu, 0x2000000000ull, 8192, out, 64);
        CHECK(n == 3);
        /* max_out bounding: a 1-entry buffer still makes progress later. */
        nord2_gpu_mmu_free_detached(&mmu, out, n);
        CHECK(live == 1);

        /* Randomized: mapping slots with a model of the minimal table set. */
        bool mapped[SLOTS] = {0};
        unsigned pages[SLOTS] = {0};
        srand(11);
        for (unsigned iter = 0; iter < 6000; iter++) {
                unsigned s = (unsigned)rand() % SLOTS;
                if (!mapped[s]) {
                        pages[s] = 1 + (unsigned)rand() % 6;
                        CHECK(nord2_gpu_mmu_map(&mmu, slot_va(s),
                                                0x100000000ull + s * 0x10000ull,
                                                pages[s] * 4096ull,
                                                NORD2_GPU_MEMATTR_IMPL_DEF,
                                                (rand() & 1) != 0, false));
                        mapped[s] = true;
                } else {
                        CHECK(nord2_gpu_mmu_unmap(&mmu, slot_va(s), pages[s] * 4096ull));
                        mapped[s] = false;
                        unsigned max = 1 + (unsigned)rand() % 4;
                        n = nord2_gpu_mmu_detach_empty(&mmu, slot_va(s),
                                                       pages[s] * 4096ull, out, max);
                        CHECK(n <= max);
                        for (unsigned i = 0; i < n; i++) {
                                uint64_t probe;
                                /* nothing mapped can route through them */
                                for (unsigned t = 0; t < SLOTS; t++)
                                        if (mapped[t])
                                                CHECK(raw_walk(mmu.root_pa, slot_va(t), &probe));
                        }
                        CHECK(nord2_gpu_mmu_free_detached(&mmu, out, n) == n);
                }
                if ((iter % 97) == 0) {
                        /* Sweep the whole space for leftovers (bounded calls). */
                        for (unsigned t = 0; t < SLOTS; t++) {
                                if (mapped[t])
                                        continue;
                                n = nord2_gpu_mmu_detach_empty(&mmu, slot_va(t), 4096, out, 64);
                                nord2_gpu_mmu_free_detached(&mmu, out, n);
                        }
                        CHECK(nord2_gpu_mmu_live_tables(&mmu) ==
                              expected_tables(mapped, pages));
                        CHECK(live == nord2_gpu_mmu_live_tables(&mmu));
                }
                for (unsigned t = 0; t < SLOTS; t++) {
                        if (!mapped[t] || (iter % 211))
                                continue;
                        for (unsigned p = 0; p < pages[t]; p++)
                                CHECK(raw_walk(mmu.root_pa, slot_va(t) + p * 4096ull, &pa) &&
                                      pa == 0x100000000ull + t * 0x10000ull + p * 4096ull);
                }
        }
        for (unsigned s = 0; s < SLOTS; s++) {
                if (!mapped[s])
                        continue;
                CHECK(nord2_gpu_mmu_unmap(&mmu, slot_va(s), pages[s] * 4096ull));
                n = nord2_gpu_mmu_detach_empty(&mmu, slot_va(s), pages[s] * 4096ull, out, 64);
                nord2_gpu_mmu_free_detached(&mmu, out, n);
        }
        CHECK(nord2_gpu_mmu_live_tables(&mmu) == 1 && live == 1);
        nord2_gpu_mmu_fini(&mmu);
        CHECK(live == 0);
        free(pool);
        if (failures) {
                fprintf(stderr, "nord2_mmu_reclaim_test: %u failures\n", failures);
                return 1;
        }
        printf("nord2_mmu_reclaim_test: detach/free protocol, minimal table set, "
               "raw-walk agreement over 6000 operations (%u frees, %u flushes)\n",
               frees, flushes);
        return 0;
}
