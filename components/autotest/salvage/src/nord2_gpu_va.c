/*
 * Bounded GPU VA allocator.  See nord2_gpu_va.h.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_va.h"

#include "nord2_gpu_compat.h"

static bool pow2(uint64_t x)
{
        return x && !(x & (x - 1));
}

bool n2va_init(struct n2va *va, uint64_t base, uint64_t limit)
{
        if (!va || (base & (N2VA_GRANULE - 1)) || (limit & (N2VA_GRANULE - 1)) ||
            limit <= base)
                return false;
        memset(va, 0, sizeof(*va));
        va->base = base;
        va->limit = limit;
        va->free[0].start = base;
        va->free[0].size = limit - base;
        va->nfree = 1;
        return true;
}

static void remove_at(struct n2va *va, unsigned i)
{
        for (unsigned j = i; j + 1 < va->nfree; j++)
                va->free[j] = va->free[j + 1];
        va->nfree--;
}

static bool insert_at(struct n2va *va, unsigned i, uint64_t start,
                      uint64_t size)
{
        if (va->nfree >= N2VA_MAX_EXTENTS)
                return false;
        for (unsigned j = va->nfree; j > i; j--)
                va->free[j] = va->free[j - 1];
        va->free[i].start = start;
        va->free[i].size = size;
        va->nfree++;
        return true;
}

bool n2va_alloc(struct n2va *va, uint64_t size, uint64_t align, uint64_t *out)
{
        if (!va || !out || !size || (size & (N2VA_GRANULE - 1)) ||
            !pow2(align) || align < N2VA_GRANULE)
                return false;
        for (unsigned i = 0; i < va->nfree; i++) {
                struct n2va_extent *e = &va->free[i];
                uint64_t start = (e->start + align - 1) & ~(align - 1);
                if (start < e->start || start - e->start > e->size ||
                    size > e->size - (start - e->start))
                        continue;
                uint64_t head = start - e->start;
                uint64_t tail = e->size - head - size;
                if (head && tail) {
                        /* Splitting needs one more extent slot. */
                        if (va->nfree >= N2VA_MAX_EXTENTS)
                                continue;
                        uint64_t tail_start = start + size;
                        e->size = head;
                        (void)insert_at(va, i + 1, tail_start, tail);
                } else if (head) {
                        e->size = head;
                } else if (tail) {
                        e->start = start + size;
                        e->size = tail;
                } else {
                        remove_at(va, i);
                }
                va->allocated += size;
                *out = start;
                return true;
        }
        return false;
}

bool n2va_free(struct n2va *va, uint64_t start, uint64_t size)
{
        if (!va || !size || (start & (N2VA_GRANULE - 1)) ||
            (size & (N2VA_GRANULE - 1)) || start < va->base ||
            start > va->limit || size > va->limit - start ||
            size > va->allocated)
                return false;
        uint64_t end = start + size;
        unsigned i = 0;
        while (i < va->nfree && va->free[i].start < start)
                i++;
        /* Refuse any overlap with existing free extents (double free). */
        if (i > 0 && va->free[i - 1].start + va->free[i - 1].size > start)
                return false;
        if (i < va->nfree && va->free[i].start < end)
                return false;
        bool merge_prev = i > 0 &&
                va->free[i - 1].start + va->free[i - 1].size == start;
        bool merge_next = i < va->nfree && va->free[i].start == end;
        if (merge_prev && merge_next) {
                va->free[i - 1].size += size + va->free[i].size;
                remove_at(va, i);
        } else if (merge_prev) {
                va->free[i - 1].size += size;
        } else if (merge_next) {
                va->free[i].start = start;
                va->free[i].size += size;
        } else if (!insert_at(va, i, start, size)) {
                /* Capacity exhausted: the range is leaked from the
                 * allocator, never double-assigned. */
                va->allocated -= size;
                return false;
        }
        va->allocated -= size;
        return true;
}

uint64_t n2va_largest_free(const struct n2va *va)
{
        uint64_t best = 0;
        for (unsigned i = 0; va && i < va->nfree; i++)
                if (va->free[i].size > best)
                        best = va->free[i].size;
        return best;
}
