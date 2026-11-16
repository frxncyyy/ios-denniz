/*
 * Randomized bitmap-model test for the bounded GPU VA allocator.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_va.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { failures++; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

#define PAGES 4096u
#define BASE 0x1000000000ull

int main(void)
{
        static struct n2va va;
        static unsigned char owner[PAGES];
        struct { uint64_t start, size; bool live; } a[512];
        memset(a, 0, sizeof(a));
        CHECK(!n2va_init(&va, BASE + 1, BASE + PAGES * 4096ull));
        CHECK(n2va_init(&va, BASE, BASE + PAGES * 4096ull));
        uint64_t x;
        CHECK(!n2va_alloc(&va, 0, 4096, &x));
        CHECK(!n2va_alloc(&va, 4096, 3 * 4096, &x));
        CHECK(!n2va_alloc(&va, 100, 4096, &x));
        CHECK(n2va_alloc(&va, 4096, 0x10000, &x) && x == BASE);
        CHECK(n2va_free(&va, x, 4096) && va.nfree == 1 && va.allocated == 0);
        CHECK(!n2va_free(&va, x, 4096)); /* double free */
        srand(5);
        unsigned allocs = 0, refusals = 0;
        for (unsigned it = 0; it < 200000; it++) {
                unsigned k = (unsigned)rand() % 512;
                if (!a[k].live) {
                        uint64_t pages = 1 + (uint64_t)(rand() % 64);
                        uint64_t align = 4096ull << (rand() % 5);
                        uint64_t start;
                        if (n2va_alloc(&va, pages * 4096, align, &start)) {
                                CHECK((start & (align - 1)) == 0);
                                CHECK(start >= BASE && start + pages * 4096 <= BASE + PAGES * 4096ull);
                                for (uint64_t p = 0; p < pages; p++) {
                                        unsigned idx = (unsigned)((start - BASE) / 4096 + p);
                                        CHECK(owner[idx] == 0); /* never double-assigned */
                                        owner[idx] = (unsigned char)(k + 1 > 255 ? 255 : k + 1);
                                }
                                a[k].start = start;
                                a[k].size = pages * 4096;
                                a[k].live = true;
                                allocs++;
                        } else {
                                refusals++;
                        }
                } else {
                        if ((rand() % 50) == 0) {
                                /* corrupted frees are refused */
                                CHECK(!n2va_free(&va, a[k].start + 4096 * 70000ull, a[k].size));
                        }
                        CHECK(n2va_free(&va, a[k].start, a[k].size));
                        for (uint64_t p = 0; p < a[k].size / 4096; p++)
                                owner[(a[k].start - BASE) / 4096 + p] = 0;
                        a[k].live = false;
                }
                /* free list sorted, disjoint, non-adjacent (coalesced) */
                for (unsigned i = 1; i < va.nfree; i++)
                        CHECK(va.free[i - 1].start + va.free[i - 1].size < va.free[i].start);
        }
        for (unsigned k = 0; k < 512; k++)
                if (a[k].live)
                        CHECK(n2va_free(&va, a[k].start, a[k].size));
        CHECK(va.nfree == 1 && va.allocated == 0 &&
              n2va_largest_free(&va) == PAGES * 4096ull);
        if (failures) {
                fprintf(stderr, "nord2_va_test: %u failures\n", failures);
                return 1;
        }
        printf("nord2_va_test: %u allocations, %u refusals, never double-assigned, "
               "fully coalesced\n", allocs, refusals);
        return 0;
}
