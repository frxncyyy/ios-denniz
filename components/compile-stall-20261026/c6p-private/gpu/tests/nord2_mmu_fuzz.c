/*
 * Randomized model test for the arch9 GPU MMU.  Every operation is checked
 * against a tiny software model; a map conflict must leave the prior mapping
 * intact and an unmap must be atomic.
 */
#include "nord2_gpu_mmu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGES 32u
#define MAX_TABLES 256u
#define ARENA_BYTES (2u * 1024u * 1024u)
#define VA_BASE 0x200000ull

struct arena {
        uint8_t *base;
        size_t used;
        uint64_t gpu_base;
};
static struct arena a;

static void *aa(size_t size)
{
        size = (size + 4095u) & ~4095u;
        if (a.used + size > ARENA_BYTES)
                return NULL;
        void *p = a.base + a.used;
        a.used += size;
        memset(p, 0, size);
        return p;
}
static bool alloc_table(void *ctx, uint64_t *pa, void **cpu)
{
        (void)ctx;
        void *p = aa(4096);
        if (!p)
                return false;
        *cpu = p;
        *pa = a.gpu_base + (uint64_t)((uint8_t *)p - a.base);
        return true;
}
static void free_table(void *ctx, uint64_t pa, void *cpu)
{
        (void)ctx; (void)pa; (void)cpu;
}
static void flush(void *ctx, uint64_t pa, size_t size)
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

static uint32_t rnd_state = 0x12345678u;
static uint32_t rnd(void)
{
        rnd_state ^= rnd_state << 13;
        rnd_state ^= rnd_state >> 17;
        rnd_state ^= rnd_state << 5;
        return rnd_state;
}

int main(void)
{
        a.base = calloc(1, ARENA_BYTES);
        a.gpu_base = 0x60000000;
        const struct nord2_gpu_mmu_ops ops = {
                alloc_table, free_table, flush, alloc_host, free_host,
        };
        struct nord2_gpu_mmu mmu;
        if (!nord2_gpu_mmu_init(&mmu, &ops, &a, MAX_TABLES))
                return 1;
        uint64_t model[PAGES];
        memset(model, 0, sizeof(model));
        unsigned failures = 0;
        for (unsigned step = 0; step < 5000; step++) {
                unsigned first = rnd() % PAGES;
                unsigned count = 1 + (rnd() % 4);
                if (count > PAGES - first)
                        count = PAGES - first;
                uint64_t va = VA_BASE + (uint64_t)first * 4096;
                bool do_map = (rnd() & 1) != 0;
                uint64_t pa = 0x70000000ull + (rnd() % 4096) * 4096ull;
                if (do_map) {
                        bool expect = true;
                        for (unsigned i = 0; i < count; i++) {
                                if (model[first + i] &&
                                    model[first + i] != pa + i * 4096)
                                        expect = false;
                        }
                        bool got = nord2_gpu_mmu_map(
                                &mmu, va, pa, count * 4096,
                                NORD2_GPU_MEMATTR_WRITE_ALLOC, true, false);
                        if (got != expect) {
                                fprintf(stderr,
                                        "step %u map expectation mismatch\n",
                                        step);
                                failures++;
                                break;
                        }
                        if (got) {
                                for (unsigned i = 0; i < count; i++)
                                        model[first + i] = pa + i * 4096;
                        }
                } else {
                        bool expect = true;
                        for (unsigned i = 0; i < count; i++)
                                if (!model[first + i])
                                        expect = false;
                        bool got = nord2_gpu_mmu_unmap(&mmu, va,
                                                       count * 4096);
                        if (got != expect) {
                                fprintf(stderr,
                                        "step %u unmap expectation mismatch\n",
                                        step);
                                failures++;
                                break;
                        }
                        if (got) {
                                for (unsigned i = 0; i < count; i++)
                                        model[first + i] = 0;
                        }
                }
                for (unsigned i = 0; i < PAGES; i++) {
                        uint64_t got_pa = 0;
                        bool present = nord2_gpu_mmu_walk(
                                &mmu, VA_BASE + (uint64_t)i * 4096, &got_pa,
                                NULL);
                        if (present != (model[i] != 0) ||
                            (present && got_pa != model[i])) {
                                fprintf(stderr,
                                        "step %u page %u mismatch got %d %llx "
                                        "want %llx\n",
                                        step, i, present,
                                        (unsigned long long)got_pa,
                                        (unsigned long long)model[i]);
                                failures++;
                                break;
                        }
                }
                if (failures)
                        break;
        }
        nord2_gpu_mmu_fini(&mmu);
        free(a.base);
        if (failures)
                return 1;
        printf("all randomized MMU model checks passed\n");
        return 0;
}
