/* Owned multi-L3 backing and per-binding readonly resource aliases.
 * The BSD submitter maps/unmaps it under the shared submission mutex. */
#include "TeguGPUFullscreenAllocation.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/OSAtomic.h>
#include <string.h>

#define TEGU_FS_PA_MIN UINT64_C(0x882000000)
#define TEGU_FS_PA_LIMIT UINT64_C(0x8c0000000)
#define TEGU_FS_RESERVED_RANGES_MAX 64u

struct tegu_fullscreen_backing {
    struct tegu_fullscreen_backing *quarantine_next;
    TeguGPUBuffer *scratch, *target, *pt_backing;
    TeguGPUBuffer **heap_buffers;
    size_t heap_buffer_count, heap_buffer_capacity;
    IOMemoryDescriptor *surface_target_backing;
    OSObject *surface_target_lifetime;
    tegu_fs_surface_page_fn surface_page_resolver;
    OSObject **resource_buffers; /* buffers or imported-surface lifetimes */
    size_t resource_buffer_count, resource_buffer_capacity;
    struct fs_page *scratch_pages, *target_pages, *resource_pages, *heap_pages;
    struct tegu_fs_resource_binding *resource_bindings;
    struct fs_pt_page *l3_pages;
    uint64_t *validation_hash;
    uint8_t *validation_roles;
    struct fs_mapping mapping;
    struct fs_request request;
    struct tegu_fs_reserved_range *reserved_ranges;
    struct tegu_fs_reserved_context reserved;
    struct tegu_full_render_job job;
    struct tegu_fs_surface_layout layout;
    uint64_t owner;
    size_t scratch_page_count, target_page_count, resource_page_count, heap_page_count, l3_page_count;
    size_t resource_binding_count;
    int quarantined;
};

static volatile SInt64 tegu_fs_next_owner = 1;
static struct tegu_fullscreen_backing * volatile tegu_fs_quarantine_head;

int tegu_fs_page_is_reserved(void *ctx, uint64_t page_pa)
{
    const struct tegu_fs_reserved_context *r =
        (const struct tegu_fs_reserved_context *)ctx;
    if (!r || !r->ranges || !r->count || (page_pa & (FS_PAGE_SIZE - 1u)))
        return 1; /* missing exclusion inventory fails closed */
    for (size_t i = 0; i < r->count; ++i) {
        const struct tegu_fs_reserved_range *range = &r->ranges[i];
        if (range->begin >= range->end || (range->begin & (FS_PAGE_SIZE - 1u)) ||
            (range->end & (FS_PAGE_SIZE - 1u))) return 1;
        if (page_pa >= range->begin && page_pa < range->end) return 1;
    }
    return 0;
}

static void tegu_fs_free_array(void *p, size_t bytes)
{
    if (p) IOFree(p, bytes);
}
static void tegu_fs_backing_destroy(struct tegu_fullscreen_backing *b)
{
    if (!b) return;
    for (size_t i = 0; i < b->resource_buffer_count; ++i)
        if (b->resource_buffers[i]) b->resource_buffers[i]->release();
    for (size_t i = 0; i < b->heap_buffer_count; ++i)
        if (b->heap_buffers[i]) b->heap_buffers[i]->release();
    if (b->scratch) b->scratch->release();
    if (b->target) b->target->release();
    if (b->surface_target_backing) b->surface_target_backing->release();
    if (b->surface_target_lifetime) b->surface_target_lifetime->release();
    if (b->pt_backing) b->pt_backing->release();
    tegu_fs_free_array(b->resource_buffers,
        b->resource_buffer_capacity * sizeof(*b->resource_buffers));
    tegu_fs_free_array(b->heap_buffers,
        b->heap_buffer_capacity * sizeof(*b->heap_buffers));
    tegu_fs_free_array(b->resource_bindings,
        b->resource_binding_count * sizeof(*b->resource_bindings));
    tegu_fs_free_array(b->scratch_pages,
        b->scratch_page_count * sizeof(*b->scratch_pages));
    tegu_fs_free_array(b->target_pages,
        b->target_page_count * sizeof(*b->target_pages));
    tegu_fs_free_array(b->resource_pages,
        b->resource_page_count * sizeof(*b->resource_pages));
    tegu_fs_free_array(b->heap_pages,
        b->heap_page_count * sizeof(*b->heap_pages));
    tegu_fs_free_array(b->l3_pages,
        b->l3_page_count * sizeof(*b->l3_pages));
    tegu_fs_free_array(b->validation_hash,
        FS_HASH_CAPACITY * sizeof(*b->validation_hash));
    tegu_fs_free_array(b->validation_roles,
        FS_HASH_CAPACITY * sizeof(*b->validation_roles));
    tegu_fs_free_array(b->reserved_ranges,
        b->reserved.count * sizeof(*b->reserved_ranges));
    IOFree(b, sizeof(*b));
}
static void tegu_fs_backing_quarantine(struct tegu_fullscreen_backing *b)
{
    if (!b || b->quarantined) return;
    b->quarantined = 1;
    struct tegu_fullscreen_backing *old;
    do {
        old = tegu_fs_quarantine_head;
        b->quarantine_next = old;
        OSMemoryBarrier();
    } while (!OSCompareAndSwapPtr(old, b,
        (void * volatile *)&tegu_fs_quarantine_head));
    IOLog("TEGU GPU: fullscreen backing quarantined until proven reset\n");
}

static void tegu_fs_partial_release(struct tegu_fullscreen_backing *b)
{
    if (!b) return;
    /* Used only before fs_mapping_init succeeds or while its state is STAGED. */
    tegu_fs_backing_destroy(b);
}

static struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_impl(
    TeguGPUBuffer *scratch, TeguGPUBuffer *target,
    IOMemoryDescriptor *surface_target_backing,
    OSObject *surface_target_lifetime, tegu_fs_surface_page_fn surface_page_resolver,
    size_t scratch_page_count, size_t heap_page_count, int b3_exact,
    uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved)
{
    if (!scratch || ((!target) == (!surface_target_backing)) || !layout || !l2_entries || !reserved ||
        (surface_target_backing && (!surface_target_lifetime || !surface_page_resolver)) ||
        tegu_fs_quarantine_head ||
        !reserved->ranges || !reserved->count ||
        reserved->count > TEGU_FS_RESERVED_RANGES_MAX ||
        (scratch_page_count != FS_SCRATCH_PAGES &&
         scratch_page_count != FS_V7_SCRATCH_PAGES) ||
        (heap_page_count != 0 && heap_page_count != FS_HEAP_PAGES) ||
        (scratch_page_count == FS_SCRATCH_PAGES &&
         (scratch_exec_mask & ~FS_SCRATCH_EXEC_ALLOWED)) ||
        (scratch_page_count == FS_V7_SCRATCH_PAGES &&
         (scratch_exec_mask & ~FS_V7_SCRATCH_EXEC_ALLOWED)) ||
        (target_offset & (FS_PAGE_SIZE - 1u)) ||
        layout->target_bytes == 0 || layout->target_bytes > TEGU_FS_MAX_BYTES ||
        layout->target_pages == 0 || layout->target_pages > FS_MAX_TARGET_PAGES ||
        layout->target_bytes > UINT32_MAX ||
        resource_span_count > FS_MAX_RESOURCE_PAGES ||
        l2_page_pa != TEGU_FS_QUEUE_L2_PA ||
        (resource_span_count && !resources) ||
        scratch->logicalLength() < scratch_page_count * FS_PAGE_SIZE ||
        target_offset > (target ? target->logicalLength() : surface_target_backing->getLength()) ||
        layout->target_bytes > (target ? target->logicalLength() : surface_target_backing->getLength()) - target_offset)
        return NULL;

    uint64_t expected_pages =
        (layout->target_bytes + FS_PAGE_SIZE - 1u) / FS_PAGE_SIZE;
    if (expected_pages != layout->target_pages) return NULL;
    uint64_t resource_pages = 0;
    for (size_t i = 0; i < resource_span_count; ++i) {
        const struct tegu_fs_resource_span *s = &resources[i];
        if (!s->buffer && (!s->surface_backing || !s->surface_lifetime || !s->surface_page)) return NULL;
        uint64_t logical = s->buffer ? s->buffer->logicalLength() : s->surface_backing->getLength();
        uint64_t whole = s->buffer ? s->buffer->getLength() : s->surface_backing->getLength();
        if (!s->length || s->offset > logical || s->length > logical - s->offset) return NULL;
        uint64_t page_offset = s->offset & (FS_PAGE_SIZE - 1u);
        uint64_t covered = page_offset + (uint64_t)s->length;
        uint64_t n = (covered + FS_PAGE_SIZE - 1u) / FS_PAGE_SIZE;
        uint64_t first_page = s->offset - page_offset;
        if (first_page > whole || n > (whole - first_page) / FS_PAGE_SIZE) return NULL;
        if (n > FS_MAX_RESOURCE_PAGES - resource_pages) return NULL;
        resource_pages += n;
    }
    struct tegu_fs_surface_layout checked_layout;
    if (resource_pages > UINT32_MAX || (b3_exact?
        tegu_fs_surface_layout_b3_with_heap(layout->width,
        layout->height, layout->row_stride, target_offset,
        (uint32_t)resource_pages, (uint32_t)scratch_page_count,
        (uint32_t)heap_page_count, &checked_layout):
        tegu_fs_surface_layout_with_heap(layout->width,
        layout->height, layout->row_stride, target_offset,
        (uint32_t)resource_pages, (uint32_t)scratch_page_count,
        (uint32_t)heap_page_count, &checked_layout)) ||
        checked_layout.width != layout->width ||
        checked_layout.height != layout->height ||
        checked_layout.row_stride != layout->row_stride ||
        checked_layout.target_bytes != layout->target_bytes ||
        checked_layout.target_pages != layout->target_pages ||
        checked_layout.total_pages != layout->total_pages ||
        checked_layout.l3_pages != layout->l3_pages ||
        checked_layout.heap_pages != layout->heap_pages) return NULL;
    uint64_t leaves = scratch_page_count + expected_pages + resource_pages + heap_page_count;
    uint64_t l3_count = (leaves + FS_L3_ENTRIES - 1u) / FS_L3_ENTRIES;
    if (!leaves || leaves > FS_MAX_SCRATCH_PAGES + FS_MAX_TARGET_PAGES +
        FS_MAX_RESOURCE_PAGES + FS_HEAP_PAGES || l3_count > FS_MAX_L3_PAGES ||
        FS_L2_FIRST + l3_count > 512u || target_offset > (target ? target->getLength() : surface_target_backing->getLength()) ||
        expected_pages > ((target ? target->getLength() : surface_target_backing->getLength()) - target_offset) / FS_PAGE_SIZE)
        return NULL;

    struct tegu_fullscreen_backing *b =
        (struct tegu_fullscreen_backing *)IOMallocZero(sizeof(*b));
    if (!b) return NULL;
    b->layout = *layout;
    b->scratch_page_count = scratch_page_count;
    b->target_page_count = (size_t)expected_pages;
    b->resource_page_count = (size_t)resource_pages;
    b->heap_page_count = heap_page_count;
    b->heap_buffer_capacity = heap_page_count / 4u;
    b->l3_page_count = (size_t)l3_count;
    b->resource_binding_count = resource_span_count;
    b->resource_buffer_capacity = resource_span_count;
    b->reserved_ranges = (struct tegu_fs_reserved_range *)IOMallocZero(
        reserved->count * sizeof(*b->reserved_ranges));
    if (b->reserved_ranges) {
        memcpy(b->reserved_ranges, reserved->ranges,
            reserved->count * sizeof(*b->reserved_ranges));
        b->reserved.ranges = b->reserved_ranges;
        b->reserved.count = reserved->count;
    }
    b->scratch_pages = (struct fs_page *)IOMallocZero(
        scratch_page_count * sizeof(*b->scratch_pages));
    b->target_pages = (struct fs_page *)IOMallocZero(
        b->target_page_count * sizeof(*b->target_pages));
    b->resource_pages = resource_pages ? (struct fs_page *)IOMallocZero(
        b->resource_page_count * sizeof(*b->resource_pages)) : NULL;
    b->heap_pages = heap_page_count ? (struct fs_page *)IOMallocZero(
        b->heap_page_count * sizeof(*b->heap_pages)) : NULL;
    b->heap_buffers = b->heap_buffer_capacity ? (TeguGPUBuffer **)IOMallocZero(
        b->heap_buffer_capacity * sizeof(*b->heap_buffers)) : NULL;
    b->l3_pages = (struct fs_pt_page *)IOMallocZero(
        b->l3_page_count * sizeof(*b->l3_pages));
    b->validation_hash = (uint64_t *)IOMallocZero(
        FS_HASH_CAPACITY * sizeof(*b->validation_hash));
    b->validation_roles = (uint8_t *)IOMallocZero(
        FS_HASH_CAPACITY * sizeof(*b->validation_roles));
    b->resource_buffers = resource_span_count ? (OSObject **)IOMallocZero(
        resource_span_count * sizeof(*b->resource_buffers)) : NULL;
    b->resource_bindings = resource_span_count ?
        (struct tegu_fs_resource_binding *)IOMallocZero(
        resource_span_count * sizeof(*b->resource_bindings)) : NULL;
    uint64_t pt_bytes = b->l3_page_count * FS_PAGE_SIZE;
    if (page_size == 0 || (page_size & (page_size - 1u)) ||
        pt_bytes > UINT64_MAX - (uint64_t)page_size + 1u) {
        tegu_fs_partial_release(b);
        return NULL;
    }
    uint64_t pt_allocation_bytes = (pt_bytes + (uint64_t)page_size - 1u) &
        ~((uint64_t)page_size - 1u);
    b->pt_backing = TeguGPUBuffer::allocate(pt_allocation_bytes);
    if (!b->scratch_pages || !b->target_pages ||
        (resource_pages && !b->resource_pages) ||
        (heap_page_count && (!b->heap_pages || !b->heap_buffers)) || !b->l3_pages ||
        !b->validation_hash || !b->validation_roles || !b->reserved_ranges ||
        (resource_span_count && (!b->resource_buffers || !b->resource_bindings)) ||
        !b->pt_backing) {
        /* Initialize resource count only after retained references are recorded. */
        b->resource_buffer_count = 0;
        tegu_fs_partial_release(b);
        return NULL;
    }
    SInt64 owner = OSAddAtomic64(1, &tegu_fs_next_owner);
    if (owner <= 0) { tegu_fs_partial_release(b); return NULL; }
    b->owner = (uint64_t)owner;
    b->scratch = scratch; scratch->retain();
    if (target) { b->target = target; target->retain(); }
    else {
        b->surface_target_backing = surface_target_backing; surface_target_backing->retain();
        b->surface_target_lifetime = surface_target_lifetime; surface_target_lifetime->retain();
        b->surface_page_resolver = surface_page_resolver;
    }
    for (size_t i = 0; i < resource_span_count; ++i) {
        b->resource_buffers[i] = resources[i].buffer ? (OSObject *)resources[i].buffer : resources[i].surface_lifetime;
        b->resource_buffers[i]->retain();
        b->resource_buffer_count++;
    }
    /* Each child is one XNU VM page (four AS1 4 KiB leaves). Retain the
     * allocator's original reference in this backing until unmap/quarantine. */
    bool valid = true;
    for (size_t i = 0; valid && i < b->heap_buffer_capacity; ++i) {
        TeguGPUBuffer *child=TeguGPUBuffer::allocate(4u*FS_PAGE_SIZE);
        if (!child || child->logicalLength()<4u*FS_PAGE_SIZE ||
            child->getLength()<4u*FS_PAGE_SIZE || !child->getBytesNoCopy()) {
            if(child)child->release();valid=false;break;
        }
        b->heap_buffers[i]=child;b->heap_buffer_count++;
        bzero(child->getBytesNoCopy(),4u*FS_PAGE_SIZE);
        uint64_t first_pa=0;
        if(!child->gpuPage(0,&first_pa)){valid=false;break;}
        for(unsigned p=0;p<4;p++) {
            uint64_t pa=0;
            if(!child->gpuPage((uint64_t)p*FS_PAGE_SIZE,&pa)||
               pa!=first_pa+(uint64_t)p*FS_PAGE_SIZE) {valid=false;break;}
            b->heap_pages[i*4u+p]=(struct fs_page){pa,b->owner};
        }
    }

    if(!valid){tegu_fs_partial_release(b);return NULL;}

    for (size_t i = 0; i < scratch_page_count; ++i) {
        uint64_t pa = 0;
        if (!scratch->gpuPage((uint64_t)i * FS_PAGE_SIZE, &pa)) { valid = false; break; }
        b->scratch_pages[i] = (struct fs_page){pa, b->owner};
    }
    for (size_t i = 0; valid && i < b->target_page_count; ++i) {
        uint64_t pa = 0;
        int pageOK = target ? target->gpuPage(target_offset + (uint64_t)i * FS_PAGE_SIZE, &pa) :
            surface_page_resolver(surface_target_backing, target_offset + (uint64_t)i * FS_PAGE_SIZE, &pa);
        if (!pageOK) {
            valid = false; break;
        }
        b->target_pages[i] = (struct fs_page){pa, b->owner};
    }
    size_t resource_index = 0;
    for (size_t i = 0; valid && i < resource_span_count; ++i) {
        const struct tegu_fs_resource_span *s = &resources[i];
        uint64_t first_page = s->offset & ~((uint64_t)FS_PAGE_SIZE - 1u);
        uint64_t covered = (s->offset - first_page) + (uint64_t)s->length;
        uint64_t mapped_bytes = (covered + FS_PAGE_SIZE - 1u) &
            ~((uint64_t)FS_PAGE_SIZE - 1u);
        size_t first_index = resource_index;
        for (uint64_t offset = 0; offset < mapped_bytes; offset += FS_PAGE_SIZE) {
            uint64_t pa = 0;
            if (s->buffer ? !s->buffer->gpuPage(first_page + offset, &pa) :
                !s->surface_page(s->surface_backing, first_page + offset, &pa)) { valid = false; break; }
            b->resource_pages[resource_index++] = (struct fs_page){pa, b->owner};
        }
        b->resource_bindings[i] = (struct tegu_fs_resource_binding) {
            .buffer = s->buffer,
            .buffer_offset = s->offset,
            .length = s->length,
            .first_page = (uint32_t)first_index,
            .page_count = (uint32_t)(mapped_bytes / FS_PAGE_SIZE),
            .gpu_va = FS_APERTURE_VA +
                (uint64_t)(scratch_page_count + expected_pages + first_index) * FS_PAGE_SIZE +
                (s->offset - first_page)
        };
    }
    unsigned char *pt_cpu = (unsigned char *)b->pt_backing->getBytesNoCopy();
    if (!pt_cpu || ((uintptr_t)pt_cpu & (FS_PAGE_SIZE - 1u))) valid = false;
    for (size_t i = 0; valid && i < b->l3_page_count; ++i) {
        uint64_t pa = 0;
        if (!b->pt_backing->gpuPage(i * FS_PAGE_SIZE, &pa)) { valid = false; break; }
        b->l3_pages[i].page = (struct fs_page){pa, b->owner};
        b->l3_pages[i].entries = (volatile uint64_t *)(pt_cpu + i * FS_PAGE_SIZE);
    }
    if (!valid) { tegu_fs_partial_release(b); return NULL; }

    if (!fs_mapping_init(&b->mapping)) { tegu_fs_partial_release(b); return NULL; }
    uint64_t ram_end = (uint64_t)gPhysBase + (uint64_t)gPhysSize;
    if (ram_end < (uint64_t)gPhysBase) {
        tegu_fs_partial_release(b);
        return NULL;
    }
    uint64_t pa_min = TEGU_FS_PA_MIN > (uint64_t)gPhysBase ?
        TEGU_FS_PA_MIN : (uint64_t)gPhysBase;
    uint64_t pa_limit = TEGU_FS_PA_LIMIT < ram_end ? TEGU_FS_PA_LIMIT : ram_end;
    if (pa_min >= pa_limit) {
        tegu_fs_partial_release(b);
        return NULL;
    }
    b->request = (struct fs_request) {
        .owner = b->owner,
        .scratch_exec_mask = scratch_exec_mask,
        .pa_min = pa_min,
        .pa_limit = pa_limit,
        .target_bytes = (uint32_t)layout->target_bytes,
        .scratch = b->scratch_pages, .scratch_count = scratch_page_count,
        .target = b->target_pages, .target_count = b->target_page_count,
        .resources = b->resource_pages, .resource_count = b->resource_page_count,
        .heap = b->heap_pages, .heap_count = b->heap_page_count,
        .l3 = b->l3_pages, .l3_count = b->l3_page_count,
        .l2_entries = l2_entries, .l2_page_pa = l2_page_pa,
        .l2_pa_min = TEGU_FS_QUEUE_L2_PA,
        .l2_pa_limit = TEGU_FS_QUEUE_L2_PA + FS_PAGE_SIZE,
        .validation_hash = b->validation_hash,
        .validation_hash_count = FS_HASH_CAPACITY,
        .validation_roles = b->validation_roles,
        .validation_roles_count = FS_HASH_CAPACITY,
        .is_reserved_pa = tegu_fs_page_is_reserved,
        .reserved_ctx = &b->reserved
    };
    b->job = (struct tegu_full_render_job) {
        .mapping = &b->mapping,
        .request = &b->request,
        .commands = NULL, .command_count = 0,
        .owner = b->owner,
        .target_gpu_va = FS_APERTURE_VA + (uint64_t)scratch_page_count * FS_PAGE_SIZE,
        .heap_gpu_va = heap_page_count ? FS_APERTURE_VA +
            (uint64_t)(scratch_page_count + expected_pages + resource_pages) * FS_PAGE_SIZE : 0,
        .heap_bytes = heap_page_count ? 0x200000u : 0,
        .timeout_ticks = 0,
        .width = layout->width, .height = layout->height,
        .row_stride = layout->row_stride,
        .target_page_start = (uint32_t)scratch_page_count,
        .target_page_count = (uint32_t)expected_pages,
        .page_count = (uint32_t)leaves,
        .kind = TEGU_FS_TRIANGLE
    };
    /* The request stays in this retained object; fs_map shallow-copies its
     * pointers and the owned vectors remain immutable through exact unmap. */
    return b;
}

struct tegu_fullscreen_backing *tegu_fullscreen_backing_create(
    TeguGPUBuffer *scratch, TeguGPUBuffer *target, uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved)
{
    return tegu_fullscreen_backing_create_impl(scratch,target,NULL,NULL,NULL,
        FS_SCRATCH_PAGES,0,0,target_offset,layout,resources,resource_span_count,scratch_exec_mask,
        l2_entries,l2_page_pa,reserved);
}

struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_surface(
    TeguGPUBuffer *scratch, IOMemoryDescriptor *target_backing,
    OSObject *target_lifetime, tegu_fs_surface_page_fn resolve_target_page,
    uint64_t target_offset, const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved)
{
    return tegu_fullscreen_backing_create_impl(scratch,NULL,target_backing,
        target_lifetime,resolve_target_page,FS_SCRATCH_PAGES,0,0,target_offset,layout,resources,
        resource_span_count,scratch_exec_mask,l2_entries,l2_page_pa,reserved);
}

struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_v7(
    TeguGPUBuffer *scratch, TeguGPUBuffer *target, uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved)
{
    return tegu_fullscreen_backing_create_impl(scratch,target,NULL,NULL,NULL,
        FS_V7_SCRATCH_PAGES,0,0,target_offset,layout,resources,resource_span_count,
        scratch_exec_mask,l2_entries,l2_page_pa,reserved);
}

struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_surface_v7(
    TeguGPUBuffer *scratch, IOMemoryDescriptor *target_backing,
    OSObject *target_lifetime, tegu_fs_surface_page_fn resolve_target_page,
    uint64_t target_offset, const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved)
{
    return tegu_fullscreen_backing_create_impl(scratch,NULL,target_backing,
        target_lifetime,resolve_target_page,FS_V7_SCRATCH_PAGES,0,0,target_offset,
        layout,resources,resource_span_count,scratch_exec_mask,l2_entries,
        l2_page_pa,reserved);
}

struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_v7_heap2m(
    TeguGPUBuffer *scratch,TeguGPUBuffer *target,uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources,size_t resource_span_count,
    uint64_t scratch_exec_mask,volatile uint64_t *l2_entries,
    uint64_t l2_page_pa,const struct tegu_fs_reserved_context *reserved)
{
    return tegu_fullscreen_backing_create_impl(scratch,target,NULL,NULL,NULL,
        FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES,0,target_offset,layout,resources,resource_span_count,
        scratch_exec_mask,l2_entries,l2_page_pa,reserved);
}
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_surface_v7_heap2m(
    TeguGPUBuffer *scratch,IOMemoryDescriptor *target_backing,OSObject *target_lifetime,
    tegu_fs_surface_page_fn resolve_target_page,uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources,size_t resource_span_count,
    uint64_t scratch_exec_mask,volatile uint64_t *l2_entries,uint64_t l2_page_pa,
    const struct tegu_fs_reserved_context *reserved)
{
    return tegu_fullscreen_backing_create_impl(scratch,NULL,target_backing,
        target_lifetime,resolve_target_page,FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES,0,
        target_offset,layout,resources,resource_span_count,scratch_exec_mask,
        l2_entries,l2_page_pa,reserved);
}

struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_b3_heap2m(
    TeguGPUBuffer *scratch,TeguGPUBuffer *target,uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources,size_t resource_span_count,
    uint64_t scratch_exec_mask,volatile uint64_t *l2_entries,
    uint64_t l2_page_pa,const struct tegu_fs_reserved_context *reserved)
{
    return tegu_fullscreen_backing_create_impl(scratch,target,NULL,NULL,NULL,
        FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES,1,target_offset,layout,resources,
        resource_span_count,scratch_exec_mask,l2_entries,l2_page_pa,reserved);
}

enum fs_disposition tegu_fullscreen_backing_map(
    struct tegu_fullscreen_backing *b, const struct fs_ops *ops)
{
    if (!b || !ops) return FS_RELEASE_STAGED;
    if (b->quarantined) return FS_QUARANTINE;
    return fs_map(&b->mapping, &b->request, ops);
}

struct tegu_full_render_job *tegu_fullscreen_backing_job(
    struct tegu_fullscreen_backing *b) { return b ? &b->job : NULL; }
struct fs_mapping *tegu_fullscreen_backing_mapping(
    struct tegu_fullscreen_backing *b) { return b ? &b->mapping : NULL; }
size_t tegu_fullscreen_backing_heap_buffer_count(
    const struct tegu_fullscreen_backing *b)
{return b?b->heap_buffer_count:0;}
TeguGPUBuffer *tegu_fullscreen_backing_heap_buffer(
    const struct tegu_fullscreen_backing *b,size_t index)
{return b&&index<b->heap_buffer_count?b->heap_buffers[index]:NULL;}
size_t tegu_fullscreen_backing_resource_count(
    const struct tegu_fullscreen_backing *b)
{ return b ? b->resource_binding_count : 0; }
const struct tegu_fs_resource_binding *tegu_fullscreen_backing_resource(
    const struct tegu_fullscreen_backing *b, size_t index)
{ return b && index < b->resource_binding_count ? &b->resource_bindings[index] : NULL; }
int tegu_fullscreen_backing_resolve_resource(
    const struct tegu_fullscreen_backing *b, size_t index,
    uint64_t buffer_offset, uint64_t byte_length, uint64_t *gpu_va)
{
    if (!b || !gpu_va || index >= b->resource_binding_count || !byte_length) return 0;
    const struct tegu_fs_resource_binding *binding = &b->resource_bindings[index];
    if (buffer_offset < binding->buffer_offset) return 0;
    uint64_t relative = buffer_offset - binding->buffer_offset;
    if (relative > binding->length || byte_length > binding->length - relative ||
        binding->gpu_va > UINT64_MAX - relative) return 0;
    *gpu_va = binding->gpu_va + relative;
    return 1;
}
void tegu_fullscreen_backing_finish(struct tegu_fullscreen_backing *b,
    int gpu_known_idle)
{
    if (!b || b->quarantined) return;
    if (!gpu_known_idle) {
        tegu_fs_backing_quarantine(b);
        return;
    }
    enum fs_disposition disposition = b->mapping.disposition;
    if (disposition == FS_KEEP_LIVE) disposition = fs_unmap(&b->mapping);
    if (disposition == FS_RELEASED || disposition == FS_RELEASE_STAGED) {
        tegu_fs_backing_destroy(b);
        return;
    }
    /* This includes uncertain GPU completion, failed unmap, and unexpected
     * KEEP_LIVE even if the caller claims it is idle. Nothing is freed. */
    tegu_fs_backing_quarantine(b);
}
