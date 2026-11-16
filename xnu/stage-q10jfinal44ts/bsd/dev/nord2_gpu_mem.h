/*
 * Nord 2 GPU buffer ownership, mapping and quarantine.
 *
 * A buffer object is backed by a caller-provided list of 4 KiB physical
 * pages.  The MMU maps those pages into one contiguous GPU virtual range.
 * Unknown job completion never frees or reuses a pinned page; the buffer is
 * moved to a quarantine list until an explicit reset/recovery proves the GPU
 * can no longer access it.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_MEM_H
#define NORD2_GPU_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nord2_gpu_mmu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NORD2_GPU_MAX_BO_PAGES 4096u /* 16 MiB at 4 KiB */
#define NORD2_GPU_MAX_JOB_BOS 16u
#define NORD2_GPU_MAX_JOBS 16u

struct nord2_gpu_bo;

struct nord2_gpu_page {
        uint64_t pa;
        void *cpu;
};

struct nord2_gpu_mem_ops {
        /* Allocate/free one physically backed 4 KiB page.  The CPU pointer is
         * a non-cacheable or cache-managed kernel mapping of the same page. */
        bool (*alloc_page)(void *ctx, struct nord2_gpu_page *page);
        void (*free_page)(void *ctx, const struct nord2_gpu_page *page);
        /* Clean/invalidate CPU caches for a physical range. */
        void (*sync)(void *ctx, uint64_t pa, void *cpu, size_t size, bool to_device);
        /* Release the opaque owner of a page list provided by the caller
         * (for example an IOBufferMemoryDescriptor). */
        void (*free_owner)(void *ctx, void *owner);
        /* Release the BO object itself after the last reference.  Optional:
         * stack/host-test BOs leave this NULL. */
        void (*free_bo)(void *ctx, struct nord2_gpu_bo *bo);
        void (*log)(void *ctx, const char *msg);
};

enum nord2_gpu_bo_flags {
        NORD2_GPU_BO_CPU_READ = 1u << 0,
        NORD2_GPU_BO_CPU_WRITE = 1u << 1,
        NORD2_GPU_BO_GPU_READ = 1u << 2,
        NORD2_GPU_BO_GPU_WRITE = 1u << 3,
        NORD2_GPU_BO_EXEC = 1u << 4,
        NORD2_GPU_BO_IOSURFACE = 1u << 5,
};

enum nord2_gpu_bo_state {
        NORD2_GPU_BO_FREE = 0,
        NORD2_GPU_BO_MAPPED,
        NORD2_GPU_BO_PINNED,
        NORD2_GPU_BO_QUARANTINED,
};

struct nord2_gpu_bo {
        uint64_t va;
        size_t size;
        unsigned nr_pages;
        struct nord2_gpu_page *pages;
        uint32_t refs;
        uint32_t flags;
        enum nord2_gpu_bo_state state;
        uint64_t owner;
        void *owner_cookie;
        uint64_t job_id; /* last job that referenced it, for diagnostics */
        bool owns_pages;
};

struct nord2_gpu_quarantine_entry {
        struct nord2_gpu_bo *bo;
        uint64_t job_id;
        uint32_t reason;
};

struct nord2_gpu_mem {
        struct nord2_gpu_mem_ops ops;
        void *ctx;
        struct nord2_gpu_mmu *mmu;
        uint64_t va_base;
        uint64_t va_limit;
        uint64_t next_va;
        struct nord2_gpu_quarantine_entry quarantine[NORD2_GPU_MAX_JOBS];
        unsigned nr_quarantine;
};

bool nord2_gpu_mem_init(struct nord2_gpu_mem *mem,
                        const struct nord2_gpu_mem_ops *ops, void *ctx,
                        struct nord2_gpu_mmu *mmu, uint64_t va_base,
                        uint64_t va_limit);

/* Create a mapped buffer from an existing page list.  On success the caller
 * must not free the page array until the BO is released. */
bool nord2_gpu_bo_create(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo,
                         struct nord2_gpu_page *pages, unsigned nr_pages,
                         uint32_t flags, uint64_t owner, void *owner_cookie,
                         bool owns_pages);
bool nord2_gpu_bo_release(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo);
void nord2_gpu_bo_retain(struct nord2_gpu_bo *bo);
bool nord2_gpu_bo_sync(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo,
                       bool to_device);

/* Quarantine the BO: it stays mapped and its pages are not freed. */
bool nord2_gpu_quarantine(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo,
                          uint64_t job_id, uint32_t reason);
/* After a verified GPU reset, release all quarantined buffers. */
unsigned nord2_gpu_quarantine_drain(struct nord2_gpu_mem *mem);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_MEM_H */
