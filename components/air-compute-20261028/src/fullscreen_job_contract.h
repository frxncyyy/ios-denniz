#ifndef TEGU_FULLSCREEN_JOB_CONTRACT_H
#define TEGU_FULLSCREEN_JOB_CONTRACT_H
#include "fullscreen_map.h"
#include <stddef.h>
#include <stdint.h>

/* Private kernel-side extension. Selector 15 keeps its version-1 target-layout
 * request and LOAD-pass version 2 remains an additive pass contract. Neither
 * user ABI is reinterpreted by the multi-L3 mapping extension. */
#define TEGU_FS_MAX_DIMENSION 4096u
#define TEGU_FS_MAX_BYTES (16u * 1024u * 1024u)
#define TEGU_FS_MAX_RESOURCES FS_MAX_RESOURCE_PAGES
#define TEGU_FS_MAX_LEAVES (FS_MAX_SCRATCH_PAGES + FS_MAX_TARGET_PAGES + FS_MAX_RESOURCE_PAGES + FS_HEAP_PAGES)

enum tegu_fs_draw_kind { TEGU_FS_CLEAR = 0, TEGU_FS_TRIANGLE = 1 };
struct tegu_fs_surface_layout {
    uint32_t width, height, row_stride;
    uint64_t target_bytes;
    uint32_t target_pages, total_pages, l3_pages, heap_pages;
};

/* The Metal allocation contract covers the visible bytes of the final row,
 * while the GPU layout maps complete row strides.  A padded final row is
 * valid when the retained allocation covers the complete mapped layout. */
static inline int tegu_fs_surface_visible_span_fits(
    const struct tegu_fs_surface_layout *layout, uint64_t visible_bytes,
    uint64_t allocation_bytes)
{
    return layout && visible_bytes &&
        visible_bytes <= layout->target_bytes &&
        layout->target_bytes <= allocation_bytes;
}

/* Checked arithmetic, also used by host tests. The large-map path requires a
 * page-aligned buffer offset; the legacy 1 MiB path retains its 64-byte rule. */
static inline int tegu_fs_surface_layout_with_scratch_class(uint32_t width, uint32_t height,
    uint32_t row_stride, uint64_t buffer_offset, uint32_t resource_pages,
    uint32_t scratch_pages,uint32_t b3_exact,struct tegu_fs_surface_layout *out)
{
    if (!out || width < 16 || height < 16 || width > TEGU_FS_MAX_DIMENSION ||
        height > TEGU_FS_MAX_DIMENSION ||
        ((row_stride & 63u) && !(b3_exact&&width==776u&&height==24u&&row_stride==3104u)) ||
        (uint64_t)row_stride < (uint64_t)width * 4u ||
        (buffer_offset & (FS_PAGE_SIZE - 1u)) ||
        resource_pages > TEGU_FS_MAX_RESOURCES ||
        (scratch_pages != FS_SCRATCH_PAGES &&
         scratch_pages != FS_V7_SCRATCH_PAGES)) return -1;
    uint64_t bytes = (uint64_t)row_stride * height;
    if (!bytes || bytes > TEGU_FS_MAX_BYTES) return -1;
    uint64_t target_pages = (bytes + FS_PAGE_SIZE - 1u) / FS_PAGE_SIZE;
    uint64_t total = scratch_pages + target_pages + resource_pages;
    uint64_t l3 = (total + FS_L3_ENTRIES - 1u) / FS_L3_ENTRIES;
    if (target_pages > FS_MAX_TARGET_PAGES || total > TEGU_FS_MAX_LEAVES ||
        l3 > FS_MAX_L3_PAGES || FS_L2_FIRST + l3 > 512u) return -1;
    *out = (struct tegu_fs_surface_layout) {
        .width=width, .height=height, .row_stride=row_stride,
        .target_bytes=bytes, .target_pages=(uint32_t)target_pages,
        .total_pages=(uint32_t)total, .l3_pages=(uint32_t)l3, .heap_pages=0
    };
    return 0;
}

static inline int tegu_fs_surface_layout_with_scratch(uint32_t width,uint32_t height,
 uint32_t row_stride,uint64_t buffer_offset,uint32_t resource_pages,
 uint32_t scratch_pages,struct tegu_fs_surface_layout *out)
{
 return tegu_fs_surface_layout_with_scratch_class(width,height,row_stride,buffer_offset,
  resource_pages,scratch_pages,0,out);
}

/* Additive fixed 2 MiB initial-heap layout. Heap pages are either zero
 * (preserve the legacy layout) or the qualified full 512-page interval. */
static inline int tegu_fs_surface_layout_with_heap(uint32_t width, uint32_t height,
    uint32_t row_stride, uint64_t buffer_offset, uint32_t resource_pages,
    uint32_t scratch_pages, uint32_t heap_pages, struct tegu_fs_surface_layout *out)
{
    if (heap_pages != 0 && heap_pages != FS_HEAP_PAGES) return -1;
    struct tegu_fs_surface_layout value;
    if (tegu_fs_surface_layout_with_scratch(width,height,row_stride,buffer_offset,
            resource_pages,scratch_pages,&value)) return -1;
    uint64_t total=(uint64_t)value.total_pages+heap_pages;
    uint64_t l3=(total+FS_L3_ENTRIES-1u)/FS_L3_ENTRIES;
    if(total>TEGU_FS_MAX_LEAVES||l3>FS_MAX_L3_PAGES||FS_L2_FIRST+l3>512u)return -1;
    value.total_pages=(uint32_t)total;value.l3_pages=(uint32_t)l3;value.heap_pages=heap_pages;
    *out=value;return 0;
}

static inline int tegu_fs_surface_layout_b3_with_heap(uint32_t width,uint32_t height,
 uint32_t row_stride,uint64_t buffer_offset,uint32_t resource_pages,
 uint32_t scratch_pages,uint32_t heap_pages,struct tegu_fs_surface_layout *out)
{
 if(heap_pages!=FS_HEAP_PAGES)return -1;
 struct tegu_fs_surface_layout value;
 if(tegu_fs_surface_layout_with_scratch_class(width,height,row_stride,buffer_offset,
      resource_pages,scratch_pages,1,&value))return -1;
 uint64_t total=(uint64_t)value.total_pages+heap_pages;
 uint64_t l3=(total+FS_L3_ENTRIES-1u)/FS_L3_ENTRIES;
 if(total>TEGU_FS_MAX_LEAVES||l3>FS_MAX_L3_PAGES||FS_L2_FIRST+l3>512u)return -1;
 value.total_pages=(uint32_t)total;value.l3_pages=(uint32_t)l3;value.heap_pages=heap_pages;
 *out=value;return 0;
}

/* Compatibility entry point: the established v6/legacy layout remains 64 pages. */
static inline int tegu_fs_surface_layout(uint32_t width, uint32_t height,
    uint32_t row_stride, uint64_t buffer_offset, uint32_t resource_pages,
    struct tegu_fs_surface_layout *out)
{
    return tegu_fs_surface_layout_with_scratch(width,height,row_stride,
        buffer_offset,resource_pages,FS_SCRATCH_PAGES,out);
}

/* No embedded page arrays: all arrays/hash/PT backing live in a retained
 * backing object. `mapping` shallow-copies fs_request, so its request arrays
 * must remain immutable until `fs_unmap` returns FS_RELEASED. */
/* 0204: the most caller commands one fullscreen job submits (was 128).
 * tg_fullscreen_append adds at most 34 codecheck words and 7 wrapper words,
 * which keeps one append under tegu_gpu_ring_append's 496-word bound. */
#define TEGU_FS_MAX_JOB_COMMANDS 400u
struct tegu_full_render_job {
    struct fs_mapping *mapping;
    const struct fs_request *request;
    const uint64_t *commands;
    size_t command_count;
    uint64_t owner, target_gpu_va, heap_gpu_va;
    uint32_t heap_bytes;
    uint64_t timeout_ticks; /* trusted bounded wait budget; zero keeps legacy waits */
    uint32_t width, height, row_stride;
    uint32_t target_page_start, target_page_count, page_count;
    enum tegu_fs_draw_kind kind;
};
/* Trusted queue metadata for the IOKit allocator. This never crosses the
 * userspace ABI. The full reservation, including firmware and all queue pages,
 * must be supplied as excluded backing when constructing the resource map. */
struct tegu_fs_queue_context {
    volatile uint64_t *l2_entries;
    uint64_t l2_page_pa, protected_begin, protected_end;
};
#ifdef __cplusplus
extern "C" {
#endif
int tegu_gpu_fullscreen_context(struct tegu_fs_queue_context *);
int tegu_gpu_render_fullscreen(const struct tegu_full_render_job *,uint32_t,uint32_t *);
uint64_t tegu_gpu_fullscreen_timeout_ticks(void);
#ifdef __cplusplus
}
#endif
#endif
