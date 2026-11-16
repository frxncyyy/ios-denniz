/*
 * Bounded GPU virtual-address allocator for one address space.
 *
 * First-fit over a sorted, coalescing free list with a fixed capacity.  A
 * range returns to the free list only when the caller frees it, which the
 * core does after an acknowledged AS flush; there is no implicit reuse.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_VA_H
#define NORD2_GPU_VA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2VA_MAX_EXTENTS 256u
#define N2VA_GRANULE 0x1000ull

struct n2va_extent {
        uint64_t start;
        uint64_t size;
};

struct n2va {
        uint64_t base, limit;
        unsigned nfree;
        uint64_t allocated;
        struct n2va_extent free[N2VA_MAX_EXTENTS];
};

bool n2va_init(struct n2va *va, uint64_t base, uint64_t limit);
/* align: power of two >= 4 KiB.  Callers that want a guard gap include it
 * in `size` and pass the same size back to n2va_free. */
bool n2va_alloc(struct n2va *va, uint64_t size, uint64_t align,
                uint64_t *out);
/* Exact range previously returned; refuses overlap with free space. */
bool n2va_free(struct n2va *va, uint64_t start, uint64_t size);
uint64_t n2va_largest_free(const struct n2va *va);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_VA_H */
