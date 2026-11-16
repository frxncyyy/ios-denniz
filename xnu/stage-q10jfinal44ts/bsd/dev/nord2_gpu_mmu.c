/*
 * Nord 2 arch9 GPU MMU page-table builder.
 *
 * Entry formats follow mmu/mali_kbase_mmu_mode_aarch64.c (r32p1): level-3
 * ATE type 3, next-level PTE type 3 with ENTRY_ACCESS_BIT, invalid type 2.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_mmu.h"

#include "nord2_gpu_compat.h"

#define ATE_VALID_L3 3ULL
#define ATE_BLOCK_L02 1ULL
#define PTE_NEXT_TABLE 3ULL
#define ATE_INVALID 2ULL
#define ATE_ACCESS_BIT (1ULL << 10)
#define ATE_ACCESS_RW (1ULL << 6)
#define ATE_ACCESS_RO (3ULL << 6)
#define ATE_NX (1ULL << 54)
#define ATE_TYPE_MASK 3ULL
#define PA_MASK (((1ULL << NORD2_GPU_PA_BITS) - 1ULL) & ~(NORD2_GPU_PAGE_SIZE - 1ULL))
#define LAST_LEVEL (NORD2_GPU_MMU_LEVELS - 1u)

static bool is_aligned(uint64_t value)
{
        return (value & (NORD2_GPU_PAGE_SIZE - 1u)) == 0;
}

static unsigned index_at(unsigned level, uint64_t va)
{
        unsigned shift = NORD2_GPU_PAGE_SHIFT + (LAST_LEVEL - level) * 9u;
        return (unsigned)((va >> shift) & (NORD2_GPU_MMU_ENTRIES - 1u));
}

static uint64_t level_span(unsigned level)
{
        return 1ull << (NORD2_GPU_PAGE_SHIFT + (LAST_LEVEL - level) * 9u);
}

static uint64_t entry_pa(uint64_t entry)
{
        return entry & PA_MASK;
}

static bool entry_valid(uint64_t entry, unsigned level)
{
        uint64_t type = entry & ATE_TYPE_MASK;
        if (level == LAST_LEVEL)
                return type == ATE_VALID_L3;
        return type == PTE_NEXT_TABLE;
}

static struct nord2_gpu_mmu_table *find_table(const struct nord2_gpu_mmu *mmu,
                                              uint64_t pa)
{
        for (unsigned i = 0; i < mmu->nr_tables; i++) {
                if (mmu->tables[i].pa == pa && !mmu->tables[i].detached)
                        return &mmu->tables[i];
        }
        return NULL;
}

static struct nord2_gpu_mmu_table *add_table(struct nord2_gpu_mmu *mmu,
                                             uint64_t pa, void *cpu)
{
        if (mmu->nr_tables >= mmu->max_tables)
                return NULL;
        struct nord2_gpu_mmu_table *t = &mmu->tables[mmu->nr_tables++];
        t->pa = pa;
        t->cpu = cpu;
        t->dirty = false;
        t->detached = false;
        return t;
}

static void flush_dirty(struct nord2_gpu_mmu *mmu)
{
        for (unsigned i = 0; i < mmu->nr_tables; i++) {
                struct nord2_gpu_mmu_table *t = &mmu->tables[i];
                if (!t->dirty)
                        continue;
                mmu->ops.flush_table(mmu->ctx, t->pa, NORD2_GPU_PAGE_SIZE);
                t->dirty = false;
        }
}

bool nord2_gpu_mmu_init(struct nord2_gpu_mmu *mmu,
                        const struct nord2_gpu_mmu_ops *ops, void *ctx,
                        unsigned max_tables)
{
        if (!mmu || !ops || !ops->alloc_table || !ops->free_table ||
            !ops->flush_table || !ops->alloc_host || !ops->free_host ||
            max_tables < 4)
                return false;
        memset(mmu, 0, sizeof(*mmu));
        mmu->ops = *ops;
        mmu->ctx = ctx;
        mmu->max_tables = max_tables;
        if (!ops->alloc_host(ctx, max_tables * sizeof(*mmu->tables),
                             (void **)&mmu->tables))
                return false;
        if (!ops->alloc_table(ctx, &mmu->root_pa, &mmu->root_cpu)) {
                ops->free_host(ctx, mmu->tables,
                               max_tables * sizeof(*mmu->tables));
                mmu->tables = NULL;
                return false;
        }
        if (!is_aligned(mmu->root_pa) || !mmu->root_cpu ||
            mmu->root_pa >= (1ull << NORD2_GPU_PA_BITS)) {
                ops->free_table(ctx, mmu->root_pa, mmu->root_cpu);
                ops->free_host(ctx, mmu->tables,
                               max_tables * sizeof(*mmu->tables));
                mmu->tables = NULL;
                return false;
        }
        mmu->valid = true;
        struct nord2_gpu_mmu_table *root =
                add_table(mmu, mmu->root_pa, mmu->root_cpu);
        root->dirty = true;
        flush_dirty(mmu);
        return true;
}

void nord2_gpu_mmu_fini(struct nord2_gpu_mmu *mmu)
{
        if (!mmu || !mmu->valid)
                return;
        for (unsigned i = mmu->nr_tables; i > 0; i--)
                mmu->ops.free_table(mmu->ctx, mmu->tables[i - 1].pa,
                                    mmu->tables[i - 1].cpu);
        if (mmu->tables)
                mmu->ops.free_host(mmu->ctx, mmu->tables,
                                   mmu->max_tables * sizeof(*mmu->tables));
        memset(mmu, 0, sizeof(*mmu));
}

/* Walk to the level-3 table for va; NULL if any level is absent. */
static struct nord2_gpu_mmu_table *walk_l3(const struct nord2_gpu_mmu *mmu,
                                           uint64_t va, bool *corrupt)
{
        uint64_t pa = mmu->root_pa;
        struct nord2_gpu_mmu_table *t = find_table(mmu, pa);
        for (unsigned level = 0; t && level < LAST_LEVEL; level++) {
                uint64_t entry = ((const uint64_t *)t->cpu)[index_at(level, va)];
                if (!entry_valid(entry, level)) {
                        if ((entry & ATE_TYPE_MASK) == ATE_BLOCK_L02 && corrupt)
                                *corrupt = true;
                        return NULL;
                }
                t = find_table(mmu, entry_pa(entry));
                if (!t && corrupt)
                        *corrupt = true;
        }
        return t;
}

static bool map_preflight(const struct nord2_gpu_mmu *mmu, uint64_t va,
                          uint64_t pa, const uint64_t *pas, size_t size,
                          uint64_t flags)
{
        const struct nord2_gpu_mmu_table *t = NULL;
        uint64_t t_base = ~0ull;
        for (uint64_t off = 0; off < size; off += NORD2_GPU_PAGE_SIZE) {
                uint64_t cur = va + off;
                uint64_t base = cur & ~(level_span(LAST_LEVEL - 1) - 1);
                if (base != t_base) {          /* one walk per level-3 table, not per page */
                        bool corrupt = false;
                        t = walk_l3(mmu, cur, &corrupt);
                        if (corrupt)
                                return false;
                        t_base = base;
                }
                if (!t)
                        continue;
                uint64_t entry = ((const uint64_t *)t->cpu)
                        [index_at(LAST_LEVEL, cur)];
                uint64_t want = pas ? pas[off / NORD2_GPU_PAGE_SIZE] : pa + off;
                if (entry_valid(entry, LAST_LEVEL) &&
                    entry != (want | flags | ATE_VALID_L3))
                        return false;
        }
        return true;
}

bool nord2_gpu_mmu_map(struct nord2_gpu_mmu *mmu, uint64_t va, uint64_t pa,
                       size_t size, enum nord2_gpu_memattr memattr,
                       bool writable, bool executable)
{
        return nord2_gpu_mmu_map_shared(mmu, va, pa, size, memattr,
                                        NORD2_GPU_SHARE_NONE, writable, executable);
}

/* Map `size` bytes at va: from the contiguous range at pa, or (pas != NULL) page by page from the list.  *done_out gets the number of
 * pages that are mapped when it fails part-way (they are the first ones). */
static bool map_impl(struct nord2_gpu_mmu *mmu, uint64_t va, uint64_t pa,
                     const uint64_t *pas, size_t size,
                     enum nord2_gpu_memattr memattr,
                     enum nord2_gpu_shareability shareability,
                     bool writable, bool executable, uint64_t *done_out)
{
        if (done_out)
                *done_out = 0;
        if (!mmu || !mmu->valid || !is_aligned(va) || (!pas && !is_aligned(pa)) ||
            (unsigned)memattr > NORD2_GPU_MEMATTR_NON_CACHEABLE ||
            (shareability != NORD2_GPU_SHARE_NONE &&
             shareability != NORD2_GPU_SHARE_INNER) ||
            (memattr == NORD2_GPU_MEMATTR_NON_CACHEABLE &&
             shareability != NORD2_GPU_SHARE_NONE) ||
            !size || (size & (NORD2_GPU_PAGE_SIZE - 1u)) ||
            va >= (1ULL << NORD2_GPU_VA_BITS) ||
            size > (1ULL << NORD2_GPU_VA_BITS) - va ||
            (!pas && (pa >= (1ULL << NORD2_GPU_PA_BITS) ||
                      size > (1ULL << NORD2_GPU_PA_BITS) - pa)))
                return false;
        if (pas) {
                for (uint64_t i = 0; i < size / NORD2_GPU_PAGE_SIZE; i++)
                        if (!is_aligned(pas[i]) || pas[i] >= (1ULL << NORD2_GPU_PA_BITS))
                                return false;
        }
        uint64_t flags = ((uint64_t)memattr << 2) |
                         ((uint64_t)shareability << 8);
        flags |= writable ? ATE_ACCESS_RW : ATE_ACCESS_RO;
        flags |= ATE_ACCESS_BIT;
        if (!executable)
                flags |= ATE_NX;
        if (!map_preflight(mmu, va, pa, pas, size, flags))
                return false;

        bool ok = true;
        uint64_t done_pages = 0;
        struct nord2_gpu_mmu_table *l3 = NULL;
        uint64_t l3_base = ~0ull;
        for (uint64_t off = 0; off < size && ok; off += NORD2_GPU_PAGE_SIZE) {
                uint64_t cur_va = va + off;
                if (!l3 || (cur_va & ~(level_span(LAST_LEVEL - 1) - 1)) !=
                                   l3_base) {
                        struct nord2_gpu_mmu_table *t =
                                find_table(mmu, mmu->root_pa);
                        for (unsigned level = 0; t && level < LAST_LEVEL;
                             level++) {
                                uint64_t *table = (uint64_t *)t->cpu;
                                unsigned idx = index_at(level, cur_va);
                                if (!entry_valid(table[idx], level)) {
                                        uint64_t next_pa = 0;
                                        void *next_cpu = NULL;
                                        if (!mmu->ops.alloc_table(
                                                    mmu->ctx, &next_pa,
                                                    &next_cpu)) {
                                                t = NULL;
                                                break;
                                        }
                                        struct nord2_gpu_mmu_table *n = NULL;
                                        if (is_aligned(next_pa) && next_cpu &&
                                            next_pa < (1ull << NORD2_GPU_PA_BITS))
                                                n = add_table(mmu, next_pa,
                                                              next_cpu);
                                        if (!n) {
                                                mmu->ops.free_table(mmu->ctx,
                                                                    next_pa,
                                                                    next_cpu);
                                                t = NULL;
                                                break;
                                        }
                                        /* A zeroed table reads as type 0,
                                         * which the GPU treats as invalid.
                                         * The registry array is fixed-size,
                                         * so `t` stays valid across add. */
                                        n->dirty = true;
                                        table[idx] = next_pa | ATE_ACCESS_BIT |
                                                     PTE_NEXT_TABLE;
                                        t->dirty = true;
                                        t = n;
                                        continue;
                                }
                                t = find_table(mmu, entry_pa(table[idx]));
                        }
                        l3 = t;
                        l3_base = cur_va & ~(level_span(LAST_LEVEL - 1) - 1);
                        if (!l3) {
                                ok = false;
                                break;
                        }
                }
                uint64_t *table = (uint64_t *)l3->cpu;
                unsigned idx = index_at(LAST_LEVEL, cur_va);
                uint64_t want_pa = pas ? pas[off / NORD2_GPU_PAGE_SIZE] : pa + off;
                if (entry_valid(table[idx], LAST_LEVEL)) {
                        if (entry_pa(table[idx]) != want_pa)
                                ok = false; /* excluded by preflight */
                        else
                                done_pages++;
                        continue;           /* identical: idempotent */
                }
                table[idx] = want_pa | flags | ATE_VALID_L3;
                l3->dirty = true;
                mmu->mapped_pages++;
                mmu->mapped_bytes += NORD2_GPU_PAGE_SIZE;
                done_pages++;
        }
        flush_dirty(mmu);
        if (done_out)
                *done_out = ok ? size / NORD2_GPU_PAGE_SIZE : done_pages;
        return ok;
}

bool nord2_gpu_mmu_map_shared(struct nord2_gpu_mmu *mmu, uint64_t va, uint64_t pa,
                              size_t size, enum nord2_gpu_memattr memattr,
                              enum nord2_gpu_shareability shareability,
                              bool writable, bool executable)
{
        return map_impl(mmu, va, pa, NULL, size, memattr, shareability,
                        writable, executable, NULL);
}

bool nord2_gpu_mmu_map_pages(struct nord2_gpu_mmu *mmu, uint64_t va,
                             const uint64_t *pas, size_t count,
                             enum nord2_gpu_memattr memattr,
                             enum nord2_gpu_shareability shareability,
                             bool writable, bool executable, uint64_t *mapped)
{
        if (!pas || !count)
                return false;
        return map_impl(mmu, va, 0, pas, count * NORD2_GPU_PAGE_SIZE, memattr,
                        shareability, writable, executable, mapped);
}

bool nord2_gpu_mmu_walk(const struct nord2_gpu_mmu *mmu, uint64_t va,
                        uint64_t *pa, uint64_t *entry_out)
{
        if (!mmu || !mmu->valid || va >= (1ULL << NORD2_GPU_VA_BITS))
                return false;
        const struct nord2_gpu_mmu_table *t = walk_l3(mmu, va, NULL);
        if (!t)
                return false;
        uint64_t entry = ((const uint64_t *)t->cpu)[index_at(LAST_LEVEL, va)];
        if (!entry_valid(entry, LAST_LEVEL))
                return false;
        if (pa)
                *pa = entry_pa(entry);
        if (entry_out)
                *entry_out = entry;
        return true;
}

bool nord2_gpu_mmu_unmap(struct nord2_gpu_mmu *mmu, uint64_t va, size_t size)
{
        if (!mmu || !mmu->valid || !is_aligned(va) || !size ||
            (size & (NORD2_GPU_PAGE_SIZE - 1u)) ||
            va >= (1ULL << NORD2_GPU_VA_BITS) ||
            size > (1ULL << NORD2_GPU_VA_BITS) - va)
                return false;
        /* Atomic: refuse unless every page is mapped.  One walk per level-3 table, not per page. */
        struct nord2_gpu_mmu_table *t = NULL;
        uint64_t t_base = ~0ull;
        for (uint64_t off = 0; off < size; off += NORD2_GPU_PAGE_SIZE) {
                uint64_t cur = va + off;
                uint64_t base = cur & ~(level_span(LAST_LEVEL - 1) - 1);
                if (base != t_base) {
                        t = walk_l3(mmu, cur, NULL);
                        t_base = base;
                }
                if (!t || !entry_valid(((const uint64_t *)t->cpu)[index_at(LAST_LEVEL, cur)], LAST_LEVEL))
                        return false;
        }
        unsigned count = 0;
        t = NULL;
        t_base = ~0ull;
        for (uint64_t off = 0; off < size; off += NORD2_GPU_PAGE_SIZE) {
                uint64_t cur = va + off;
                uint64_t base = cur & ~(level_span(LAST_LEVEL - 1) - 1);
                if (base != t_base) {
                        t = walk_l3(mmu, cur, NULL);
                        t_base = base;
                }
                uint64_t *table = (uint64_t *)t->cpu;
                table[index_at(LAST_LEVEL, cur)] = ATE_INVALID;
                t->dirty = true;
                count++;
        }
        flush_dirty(mmu);
        mmu->mapped_pages = count > mmu->mapped_pages ? 0
                                                      : mmu->mapped_pages - count;
        mmu->mapped_bytes = mmu->mapped_pages * NORD2_GPU_PAGE_SIZE;
        return true;
}

bool nord2_gpu_mmu_range_free(const struct nord2_gpu_mmu *mmu, uint64_t va,
                              size_t size)
{
        if (!mmu || !mmu->valid || !size ||
            (size & (NORD2_GPU_PAGE_SIZE - 1u)) ||
            va >= (1ULL << NORD2_GPU_VA_BITS) ||
            size > (1ULL << NORD2_GPU_VA_BITS) - va)
                return false;
        for (uint64_t off = 0; off < size; off += NORD2_GPU_PAGE_SIZE) {
                if (nord2_gpu_mmu_walk(mmu, va + off, NULL, NULL))
                        return false;
        }
        return true;
}

static bool table_empty(const struct nord2_gpu_mmu_table *t)
{
        const uint64_t *e = (const uint64_t *)t->cpu;
        for (unsigned i = 0; i < NORD2_GPU_MMU_ENTRIES; i++) {
                uint64_t type = e[i] & ATE_TYPE_MASK;
                if (type == ATE_VALID_L3 || type == ATE_BLOCK_L02)
                        return false;
        }
        return true;
}

/* Try to detach the table at `level+1` covering va from its parent chain.
 * Returns the number of tables appended to out. */
static unsigned detach_at(struct nord2_gpu_mmu *mmu, uint64_t va,
                          uint64_t *out, unsigned max_out)
{
        /* Record the path root..L3 for va. */
        struct nord2_gpu_mmu_table *path[NORD2_GPU_MMU_LEVELS] = {0};
        path[0] = find_table(mmu, mmu->root_pa);
        unsigned depth = 0;
        if (!path[0])
                return 0;
        for (unsigned level = 0; level < LAST_LEVEL; level++) {
                uint64_t entry = ((const uint64_t *)path[level]->cpu)
                        [index_at(level, va)];
                if (!entry_valid(entry, level))
                        break;
                path[level + 1] = find_table(mmu, entry_pa(entry));
                if (!path[level + 1])
                        return 0; /* invalid registry: do not free ancestors */
                depth = level + 1;
        }
        unsigned n = 0;
        /* Bottom-up: unlink a child only when it is empty; the root stays. */
        for (unsigned level = depth; level > 0 && n < max_out; level--) {
                struct nord2_gpu_mmu_table *child = path[level];
                if (!child || !table_empty(child))
                        break;
                struct nord2_gpu_mmu_table *parent = path[level - 1];
                ((uint64_t *)parent->cpu)[index_at(level - 1, va)] =
                        ATE_INVALID;
                parent->dirty = true;
                child->detached = true;
                child->dirty = false;
                out[n++] = child->pa;
        }
        return n;
}

unsigned nord2_gpu_mmu_detach_empty(struct nord2_gpu_mmu *mmu, uint64_t va,
                                    size_t size, uint64_t *out,
                                    unsigned max_out)
{
        if (!mmu || !mmu->valid || !out || !max_out || !size ||
            !is_aligned(va) || (size & (NORD2_GPU_PAGE_SIZE - 1u)) ||
            va >= (1ULL << NORD2_GPU_VA_BITS) ||
            size > (1ULL << NORD2_GPU_VA_BITS) - va)
                return 0;
        unsigned n = 0;
        const uint64_t l3_span = level_span(LAST_LEVEL - 1); /* 2 MiB */
        uint64_t first = va & ~(l3_span - 1);
        for (uint64_t cur = first; cur < va + size && n < max_out;
             cur += l3_span)
                n += detach_at(mmu, cur, out + n, max_out - n);
        flush_dirty(mmu);
        return n;
}

unsigned nord2_gpu_mmu_free_detached(struct nord2_gpu_mmu *mmu,
                                     const uint64_t *pas, unsigned count)
{
        if (!mmu || !mmu->valid || !pas)
                return 0;
        unsigned freed = 0;
        for (unsigned k = 0; k < count; k++) {
                for (unsigned i = 0; i < mmu->nr_tables; i++) {
                        struct nord2_gpu_mmu_table *t = &mmu->tables[i];
                        if (t->pa != pas[k] || !t->detached)
                                continue;
                        mmu->ops.free_table(mmu->ctx, t->pa, t->cpu);
                        mmu->tables[i] = mmu->tables[--mmu->nr_tables];
                        memset(&mmu->tables[mmu->nr_tables], 0,
                               sizeof(mmu->tables[0]));
                        freed++;
                        break;
                }
        }
        return freed;
}

unsigned nord2_gpu_mmu_live_tables(const struct nord2_gpu_mmu *mmu)
{
        unsigned n = 0;
        if (!mmu || !mmu->valid)
                return 0;
        for (unsigned i = 0; i < mmu->nr_tables; i++)
                n += !mmu->tables[i].detached;
        return n;
}

const char *nord2_gpu_mmu_exception_string(uint32_t faultstatus)
{
        switch ((faultstatus >> 3) & 0x7) {
        case 0: return "translation fault";
        case 1: return "permission fault";
        case 2: return "translation table bus fault";
        case 3: return "access flag fault";
        case 4: return "address size fault";
        case 5: return "memory attributes fault";
        default: return "unknown exception";
        }
}

const char *nord2_gpu_mmu_access_string(uint32_t faultstatus)
{
        switch ((faultstatus >> 8) & 0x3) {
        case 0: return "atomic";
        case 1: return "execute";
        case 2: return "read";
        case 3: return "write";
        default: return "unknown access";
        }
}
