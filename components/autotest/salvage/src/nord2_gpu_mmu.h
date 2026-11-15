/*
 * Nord 2 Mali-G77 arch9 GPU MMU (AArch64-style 4-level table, 4 KiB pages).
 *
 * The page-table format is taken from the vendor r32p1 aarch64 MMU mode:
 *   - bottom-level (level 3) valid ATE: bits[1:0] = 3
 *   - levels 0..2 next-table PTE: bits[1:0] = 3
 *   - levels 0..2 block ATE: bits[1:0] = 1 (not emitted by this driver)
 *   - invalid entry: 2
 *   - access bit 10, attribute bits 4:2, access permission bits 7:6,
 *     shareability bits 9:8, NX bit 54
 *
 * XNU manages 16 KiB pages.  Each physical 16 KiB page is therefore mapped as
 * four independent 4 KiB GPU pages; no code here assumes host page size.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_MMU_H
#define NORD2_GPU_MMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORD2_GPU_PAGE_SHIFT 12u
#define NORD2_GPU_PAGE_SIZE  (1u << NORD2_GPU_PAGE_SHIFT)
#define NORD2_GPU_VA_BITS    40u
#define NORD2_GPU_PA_BITS    40u
#define NORD2_GPU_MMU_LEVELS 4u
#define NORD2_GPU_MMU_ENTRIES 512u

/* Memory attributes (vendor AS_MEMATTR indices in bits 4:2). */
enum nord2_gpu_memattr {
        NORD2_GPU_MEMATTR_IMPL_DEF = 0,
        NORD2_GPU_MEMATTR_FORCE_CACHE = 1,
        NORD2_GPU_MEMATTR_WRITE_ALLOC = 2,
        NORD2_GPU_MEMATTR_OUTER_IMPL_DEF = 3,
        NORD2_GPU_MEMATTR_OUTER_WA = 4,
        NORD2_GPU_MEMATTR_NON_CACHEABLE = 5,
};

/* GPU-internal coherency is independent of CPU/GPU snooping. */
enum nord2_gpu_shareability {
        NORD2_GPU_SHARE_NONE = 0,
        NORD2_GPU_SHARE_INNER = 3,
};

struct nord2_gpu_mmu_ops {
        /* Allocate one zeroed 4 KiB table.  Returns both the physical address
         * visible to the GPU and a CPU mapping used to fill entries. */
        bool (*alloc_table)(void *ctx, uint64_t *pa, void **cpu);
        void (*free_table)(void *ctx, uint64_t pa, void *cpu);
        /* Make new/updated entries visible to the GPU (cache clean). */
        void (*flush_table)(void *ctx, uint64_t pa, size_t size);
        /* Host memory for the bounded table registry. */
        bool (*alloc_host)(void *ctx, size_t size, void **cpu);
        void (*free_host)(void *ctx, void *cpu, size_t size);
        void (*log)(void *ctx, const char *msg);
};

struct nord2_gpu_mmu {
        struct nord2_gpu_mmu_ops ops;
        void *ctx;
        uint64_t root_pa;
        void *root_cpu;
        uint64_t mapped_pages;
        uint64_t mapped_bytes;
        /* Bounded list of allocated tables for deterministic teardown. */
        struct nord2_gpu_mmu_table *tables;
        unsigned nr_tables;
        unsigned max_tables;
        bool valid;
};

struct nord2_gpu_mmu_table {
        uint64_t pa;
        void *cpu;
        bool dirty;     /* modified since the last flush_table */
        bool detached;  /* unlinked, awaiting nord2_gpu_mmu_free_detached */
};

/* max_tables bounds table allocation (each covers 1 GiB at level 2 or 2 MiB
 * at level 3); a 4 GiB aperture needs at most 1+4+2048 tables if fully sparse.
 * Callers should choose a bound appropriate to their address-space policy. */
bool nord2_gpu_mmu_init(struct nord2_gpu_mmu *mmu,
                        const struct nord2_gpu_mmu_ops *ops, void *ctx,
                        unsigned max_tables);
void nord2_gpu_mmu_fini(struct nord2_gpu_mmu *mmu);

/* Map [va, va+size) to [pa, pa+size).  Both must be 4 KiB aligned and within
 * the configured VA/PA width.  The range must not already be mapped to a
 * different physical address or attributes. The legacy entry point preserves
 * non-shareable mappings, including the Android dummy-job workaround. */
bool nord2_gpu_mmu_map(struct nord2_gpu_mmu *mmu, uint64_t va, uint64_t pa,
                       size_t size, enum nord2_gpu_memattr memattr,
                       bool writable, bool executable);
bool nord2_gpu_mmu_map_shared(struct nord2_gpu_mmu *mmu, uint64_t va, uint64_t pa,
                              size_t size, enum nord2_gpu_memattr memattr,
                              enum nord2_gpu_shareability shareability,
                              bool writable, bool executable);
bool nord2_gpu_mmu_unmap(struct nord2_gpu_mmu *mmu, uint64_t va, size_t size);

/* Walk returns the leaf physical address and the raw entry, or false. */
bool nord2_gpu_mmu_walk(const struct nord2_gpu_mmu *mmu, uint64_t va,
                        uint64_t *pa, uint64_t *entry);
bool nord2_gpu_mmu_range_free(const struct nord2_gpu_mmu *mmu, uint64_t va,
                              size_t size);

/* Two-phase page-table reclamation.  After unmapping [va, va+size), unlink
 * every non-root table under that range whose 512 entries are all invalid
 * (cascading upwards) and return them in `out`.  The tables stay allocated:
 * the GPU may still hold them in its TLB/walk caches until the caller has
 * completed an acknowledged AS FLUSH covering the range (or a verified GPU
 * reset).  Only then call nord2_gpu_mmu_free_detached().  Returns the number
 * of tables detached (bounded by max_out; remaining empty tables simply stay
 * linked and are found again by a later call). */
unsigned nord2_gpu_mmu_detach_empty(struct nord2_gpu_mmu *mmu, uint64_t va,
                                    size_t size, uint64_t *out,
                                    unsigned max_out);
/* Free tables previously returned by detach_empty. */
unsigned nord2_gpu_mmu_free_detached(struct nord2_gpu_mmu *mmu,
                                     const uint64_t *pas, unsigned count);
unsigned nord2_gpu_mmu_live_tables(const struct nord2_gpu_mmu *mmu);

/* Decode AS_FAULTSTATUS (r32p1 aarch64 mode). */
const char *nord2_gpu_mmu_exception_string(uint32_t faultstatus);
const char *nord2_gpu_mmu_access_string(uint32_t faultstatus);

/* Register programming helpers (used by the kernel adapter and by tests that
 * fake MMIO).  The caller supplies the AS register base offset. */
enum {
        NORD2_MMU_AS_TRANSTAB_LO = 0x00,
        NORD2_MMU_AS_TRANSTAB_HI = 0x04,
        NORD2_MMU_AS_MEMATTR_LO = 0x08,
        NORD2_MMU_AS_MEMATTR_HI = 0x0c,
        NORD2_MMU_AS_COMMAND = 0x18,
        NORD2_MMU_AS_FAULTSTATUS = 0x1c,
        NORD2_MMU_AS_FAULTADDRESS_LO = 0x20,
        NORD2_MMU_AS_STATUS = 0x28,
        NORD2_MMU_AS_TRANSCFG_LO = 0x30,
        NORD2_MMU_AS_TRANSCFG_HI = 0x34,
};

#define NORD2_MMU_BASE       0x2000u
#define NORD2_MMU_AS0_OFFSET 0x400u
#define NORD2_MMU_AS_STRIDE  0x40u
#define NORD2_MMU_AS_REG(as, reg) \
        (NORD2_MMU_BASE + NORD2_MMU_AS0_OFFSET + \
         (as) * NORD2_MMU_AS_STRIDE + (reg))

#define NORD2_MMU_CMD_UPDATE     0x01u
#define NORD2_MMU_CMD_FLUSH_PT   0x04u
#define NORD2_MMU_CMD_FLUSH_MEM  0x05u
#define NORD2_MMU_TRANSCFG_ADRMODE_AARCH64_4K 6u
#define NORD2_MMU_STATUS_ACTIVE  0x01u

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_MMU_H */
