/*
 * Nord 2 GPU buffer ownership and quarantine implementation.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_mem.h"

#include "nord2_gpu_compat.h"

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
        return (value + alignment - 1) & ~(alignment - 1);
}

bool nord2_gpu_mem_init(struct nord2_gpu_mem *mem,
                        const struct nord2_gpu_mem_ops *ops, void *ctx,
                        struct nord2_gpu_mmu *mmu, uint64_t va_base,
                        uint64_t va_limit)
{
        if (!mem || !ops || !ops->alloc_page || !ops->free_page ||
            !ops->sync || !mmu || !mmu->valid)
                return false;
        if ((va_base & (NORD2_GPU_PAGE_SIZE - 1u)) ||
            (va_limit & (NORD2_GPU_PAGE_SIZE - 1u)) || va_limit <= va_base)
                return false;
        memset(mem, 0, sizeof(*mem));
        mem->ops = *ops;
        mem->ctx = ctx;
        mem->mmu = mmu;
        mem->va_base = va_base;
        mem->va_limit = va_limit;
        mem->next_va = va_base;
        return true;
}

bool nord2_gpu_bo_create(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo,
                         struct nord2_gpu_page *pages, unsigned nr_pages,
                         uint32_t flags, uint64_t owner, void *owner_cookie,
                         bool owns_pages)
{
        if (!mem || !bo || !pages || !nr_pages ||
            nr_pages > NORD2_GPU_MAX_BO_PAGES)
                return false;
        size_t size = (size_t)nr_pages * NORD2_GPU_PAGE_SIZE;
        uint64_t va = align_up(mem->next_va, NORD2_GPU_PAGE_SIZE);
        if (va < mem->next_va || va > mem->va_limit ||
            size > mem->va_limit - va)
                return false;
        for (unsigned i = 0; i < nr_pages; i++) {
                if (!pages[i].pa ||
                    (pages[i].pa & (NORD2_GPU_PAGE_SIZE - 1u)))
                        return false;
        }
        bool executable = (flags & NORD2_GPU_BO_EXEC) != 0;
        bool writable = (flags & NORD2_GPU_BO_GPU_WRITE) != 0;
        enum nord2_gpu_memattr attr = (flags & NORD2_GPU_BO_IOSURFACE)
                                              ? NORD2_GPU_MEMATTR_NON_CACHEABLE
                                              : NORD2_GPU_MEMATTR_WRITE_ALLOC;
        for (unsigned i = 0; i < nr_pages; i++) {
                if (!nord2_gpu_mmu_map(mem->mmu, va + i * NORD2_GPU_PAGE_SIZE,
                                       pages[i].pa, NORD2_GPU_PAGE_SIZE, attr,
                                       writable, executable)) {
                        /* Roll back what this call mapped. */
                        for (unsigned j = 0; j < i; j++)
                                (void)nord2_gpu_mmu_unmap(
                                        mem->mmu,
                                        va + j * NORD2_GPU_PAGE_SIZE,
                                        NORD2_GPU_PAGE_SIZE);
                        if (owns_pages) {
                                for (unsigned j = 0; j < nr_pages; j++)
                                        mem->ops.free_page(mem->ctx,
                                                            &pages[j]);
                        }
                        return false;
                }
        }
        memset(bo, 0, sizeof(*bo));
        bo->va = va;
        bo->size = size;
        bo->nr_pages = nr_pages;
        bo->pages = pages;
        bo->refs = 1;
        bo->flags = flags;
        bo->state = NORD2_GPU_BO_PINNED;
        bo->owner = owner;
        bo->owner_cookie = owner_cookie;
        bo->owns_pages = owns_pages;
        mem->next_va = va + size;
        return true;
}

void nord2_gpu_bo_retain(struct nord2_gpu_bo *bo)
{
        if (bo)
                bo->refs++;
}

bool nord2_gpu_bo_sync(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo,
                       bool to_device)
{
        if (!mem || !bo || !bo->pages || !bo->nr_pages)
                return false;
        for (unsigned i = 0; i < bo->nr_pages; i++)
                mem->ops.sync(mem->ctx, bo->pages[i].pa, bo->pages[i].cpu,
                               NORD2_GPU_PAGE_SIZE, to_device);
        return true;
}

static void free_bo_storage(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo)
{
        if (bo->state == NORD2_GPU_BO_MAPPED ||
            bo->state == NORD2_GPU_BO_PINNED ||
            bo->state == NORD2_GPU_BO_QUARANTINED) {
                for (unsigned i = 0; i < bo->nr_pages; i++)
                        (void)nord2_gpu_mmu_unmap(
                                mem->mmu,
                                bo->va + i * NORD2_GPU_PAGE_SIZE,
                                NORD2_GPU_PAGE_SIZE);
        }
        if (bo->owns_pages && bo->pages) {
                for (unsigned i = 0; i < bo->nr_pages; i++)
                        mem->ops.free_page(mem->ctx, &bo->pages[i]);
        } else if (mem->ops.free_owner && bo->owner_cookie) {
                mem->ops.free_owner(mem->ctx, bo->owner_cookie);
        }
        bo->owner_cookie = NULL;
        bo->state = NORD2_GPU_BO_FREE;
        bo->refs = 0;
}

bool nord2_gpu_bo_release(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo)
{
        if (!mem || !bo)
                return false;
        if (bo->state == NORD2_GPU_BO_QUARANTINED)
                return false;
        if (bo->refs == 0)
                return false;
        if (--bo->refs)
                return true;
        free_bo_storage(mem, bo);
        if (mem->ops.free_bo)
                mem->ops.free_bo(mem->ctx, bo);
        return true;
}

bool nord2_gpu_quarantine(struct nord2_gpu_mem *mem, struct nord2_gpu_bo *bo,
                          uint64_t job_id, uint32_t reason)
{
        if (!mem || !bo || bo->state == NORD2_GPU_BO_QUARANTINED ||
            mem->nr_quarantine >= NORD2_GPU_MAX_JOBS)
                return false;
        bo->state = NORD2_GPU_BO_QUARANTINED;
        bo->job_id = job_id;
        mem->quarantine[mem->nr_quarantine++] =
                (struct nord2_gpu_quarantine_entry){
                        .bo = bo,
                        .job_id = job_id,
                        .reason = reason,
                };
        return true;
}

unsigned nord2_gpu_quarantine_drain(struct nord2_gpu_mem *mem)
{
        if (!mem)
                return 0;
        unsigned drained = mem->nr_quarantine;
        for (unsigned i = 0; i < mem->nr_quarantine; i++) {
                struct nord2_gpu_bo *bo = mem->quarantine[i].bo;
                if (bo) {
                        free_bo_storage(mem, bo);
                        if (mem->ops.free_bo)
                                mem->ops.free_bo(mem->ctx, bo);
                }
        }
        memset(mem->quarantine, 0, sizeof(mem->quarantine));
        mem->nr_quarantine = 0;
        return drained;
}
