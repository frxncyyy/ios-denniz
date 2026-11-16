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

/* Free extents.  Every live buffer can leave one free gap behind it (small buffers are 64 KiB aligned and the gap is not reusable for
 * another 64 KiB-aligned buffer), so the list must hold more extents than a context has handles: 256 extents made the 257th small
 * buffer fail with N2C_E_VA long before any other limit (0105; nord2_gpu_core.c asserts N2VA_MAX_EXTENTS > N2C_MAX_BOS). */
#define N2VA_MAX_EXTENTS 8200u   /* 0186/0187: must stay above N2C_MAX_BOS (8192) */
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
