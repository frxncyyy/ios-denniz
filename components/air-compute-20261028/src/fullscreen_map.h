#ifndef NATIVE_FULLSCREEN_MAP_H
#define NATIVE_FULLSCREEN_MAP_H
#include <stddef.h>
#include <stdint.h>

#define FS_PAGE_SIZE 4096u
#define FS_APERTURE_VA UINT64_C(0x06000000)
#define FS_OLD_APERTURE_VA UINT64_C(0x05100000)
#define FS_SCRATCH_PAGES 64u
#define FS_MAX_SCRATCH_PAGES 128u
#define FS_V7_SCRATCH_PAGES 112u
#define FS_MAX_TARGET_PAGES 4096u
#define FS_MAX_RESOURCE_PAGES 4096u /* 16 MiB: one full-screen sampled texture plus buffers */
#define FS_HEAP_PAGES 512u /* fixed qualified 2 MiB initial tiler heap */
#define FS_L3_ENTRIES 512u
#define FS_MAX_L3_PAGES 18u /* ceil(TEGU_FS_MAX_LEAVES / 512) */
#define FS_L2_FIRST ((unsigned)((FS_APERTURE_VA >> 21) & 511u))
#define FS_HASH_CAPACITY 16384u
#define FS_SCRATCH_EXEC_CLEAR UINT64_C(0xC00C) /* established clear shader pages 2,3,14,15 */
#define FS_SCRATCH_EXEC_LOAD UINT64_C(0x700C) /* VS 2,3; FS 12,13; preload 14 */
#define FS_SCRATCH_EXEC_ALLOWED UINT64_C(0xF00C)
#define FS_V7_SCRATCH_EXEC_ALLOWED UINT64_C(0x007f00000000400c)

/* Leaf attribute patterns are the same patterns used by the qualified 4 KiB
 * AS1 map: RW/NX data, RO/NX resources, RO/X shader pages. */
#define FS_PTE_RW_NX UINT64_C(0x40000000000457)
#define FS_PTE_RO_NX UINT64_C(0x400000000004D7)
#define FS_PTE_RO_X  UINT64_C(0x4D7)
#define FS_PTE_L3 UINT64_C(0x403)

enum fs_disposition {
    FS_RELEASE_STAGED = 0, /* no hardware-visible mapping; caller may free staged pages */
    FS_KEEP_LIVE = 1,      /* map remains live; caller retains everything */
    FS_RELEASED = 2,       /* unmap+flush complete; caller may release all backing */
    FS_QUARANTINE = 3      /* MMU state uncertain; never free any referenced backing */
};
struct fs_page { uint64_t pa, owner; };
struct fs_pt_page { struct fs_page page; volatile uint64_t *entries; };
struct fs_request {
    uint64_t owner;
    uint64_t scratch_exec_mask;
    uint64_t pa_min, pa_limit; /* owned leaf/table allocator bounds */
    uint32_t target_bytes;
    const struct fs_page *scratch; size_t scratch_count;
    const struct fs_page *target; size_t target_count;
    const struct fs_page *resources; size_t resource_count;
    const struct fs_page *heap; size_t heap_count;
    struct fs_pt_page *l3; size_t l3_count;
    volatile uint64_t *l2_entries; /* kernel mapping of existing AS1 L2 */
    uint64_t l2_page_pa; /* physical page for alias exclusion; trusted kernel metadata */
    uint64_t l2_pa_min, l2_pa_limit; /* separate trusted existing-table bounds */
    uint64_t *validation_hash; size_t validation_hash_count; /* caller-owned workspace */
    uint8_t *validation_roles; size_t validation_roles_count;
    int (*is_reserved_pa)(void *, uint64_t); /* required queue/FS/firmware/live metadata exclusion */
    void *reserved_ctx;
};
struct fs_ops {
    void *ctx;
    /* Must serialize this AS1 mapper, wait for the GPU idle condition, program
     * LOCKADDR for the supplied self-aligned region, issue LOCK and wait. */
    int (*lock_begin)(void *, uint64_t lock_base, unsigned size_log2);
    /* One complete 64-bit descriptor update with the platform's ordered
     * low/high stores and barriers. Test callbacks fail before changing memory. */
    int (*write_entry)(void *, volatile uint64_t *, uint64_t);
    /* Publish each complete private L3 page to PoC before any L2 link makes
     * it visible to the noncoherent GPU page-table walker. */
    int (*sync_table)(void *, volatile uint64_t *, size_t bytes);
    /* Issue FLUSH_PT/unlock and wait for completion. Failure means uncertain MMU state. */
    int (*flush_unlock)(void *);
};
#define FS_MAPPING_MAGIC UINT64_C(0x54454755464d4150)
struct fs_mapping {
    uint64_t magic;
    struct fs_request req;
    struct fs_ops ops;
    uint64_t lock_base;
    unsigned lock_size_log2, leaf_count;
    unsigned linked_l3;
    enum fs_disposition disposition;
    int active;
};

/* Map/unmap must be serialized with every other AS1 operation by the caller.
 * Page descriptors and table CPU mappings must originate from trusted kernel
 * allocators. Request must provide allocator-qualified PA bounds and a non-optional
 * reserved-page callback. Duplicate PA aliases are allowed only between RO/NX
 * resource leaves; scratch, target, L2, and L3 pages remain mutually unique.
 * Request arrays must remain immutable through unmap. Never populate from userspace. */
#ifdef __cplusplus
extern "C" {
#endif
int fs_mapping_init(struct fs_mapping *); /* storage must be zero-initialized; refuses active/quarantined reuse */
enum fs_disposition fs_map(struct fs_mapping *, const struct fs_request *, const struct fs_ops *);
enum fs_disposition fs_unmap(struct fs_mapping *);
uint64_t fs_leaf_value(const struct fs_request *, unsigned page_index);
#ifdef __cplusplus
}
#endif
#endif
