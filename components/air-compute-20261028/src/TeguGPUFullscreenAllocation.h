#ifndef TEGU_GPU_FULLSCREEN_ALLOCATION_H
#define TEGU_GPU_FULLSCREEN_ALLOCATION_H
#ifdef __cplusplus
extern "C" {
#endif
#include "fullscreen_map.h"
#include "fullscreen_job_contract.h"
#ifdef __cplusplus
}
#endif
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <stdint.h>
#include <stddef.h>

struct TeguGPUBuffer;
class OSObject;
typedef int (*tegu_fs_surface_page_fn)(IOMemoryDescriptor *backing,
    uint64_t offset, uint64_t *physical);
struct tegu_fs_resource_span {
    TeguGPUBuffer *buffer;
    uint64_t offset;
    uint32_t length;
    /* Imported IOSurface span when buffer is NULL: pages come from
     * surface_page over surface_backing, kept alive by surface_lifetime. */
    IOMemoryDescriptor *surface_backing;
    OSObject *surface_lifetime;
    tegu_fs_surface_page_fn surface_page;
};
struct tegu_fs_resource_binding {
    TeguGPUBuffer *buffer; /* retained by the enclosing backing */
    uint64_t buffer_offset;
    uint32_t length;
    uint32_t first_page, page_count;
    uint64_t gpu_va; /* alias VA corresponding exactly to buffer_offset */
};
struct tegu_fs_reserved_range { uint64_t begin, end; };
struct tegu_fs_reserved_context {
    const struct tegu_fs_reserved_range *ranges;
    size_t count;
};
struct tegu_fullscreen_backing;

#define TEGU_FS_QUEUE_L2_PA UINT64_C(0x880102000)

/* `ranges` must include the live staged-firmware interval, full queue image,
 * queue root/L1/L2/L3 pages, and every separate firmware/FS page allocation. */
int tegu_fs_page_is_reserved(void *ctx, uint64_t page_pa);
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create(
    TeguGPUBuffer *scratch, TeguGPUBuffer *target, uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved);
/* Additive imported-target path. It retains both the exact IOSurface-owned
 * memory descriptor and the surface lifetime owner through completion or
 * quarantine. Page translation must validate the physical segment policy. */
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_surface(
    TeguGPUBuffer *scratch, IOMemoryDescriptor *target_backing,
    OSObject *target_lifetime, tegu_fs_surface_page_fn resolve_target_page,
    uint64_t target_offset, const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved);
/* v7-only expanded aperture. Caller must allocate a 0x70000 scratch buffer and
 * use the v7 layout's 112-page whitelist; v6 constructors remain 64 pages. */
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_v7(
    TeguGPUBuffer *scratch, TeguGPUBuffer *target, uint64_t target_offset,
    const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved);
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_surface_v7(
    TeguGPUBuffer *scratch, IOMemoryDescriptor *target_backing,
    OSObject *target_lifetime, tegu_fs_surface_page_fn resolve_target_page,
    uint64_t target_offset, const struct tegu_fs_surface_layout *layout,
    const struct tegu_fs_resource_span *resources, size_t resource_span_count,
    uint64_t scratch_exec_mask, volatile uint64_t *l2_entries,
    uint64_t l2_page_pa, const struct tegu_fs_reserved_context *reserved);
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_v7_heap2m(
    TeguGPUBuffer *,TeguGPUBuffer *,uint64_t,const struct tegu_fs_surface_layout *,
    const struct tegu_fs_resource_span *,size_t,uint64_t,volatile uint64_t *,uint64_t,
    const struct tegu_fs_reserved_context *);
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_surface_v7_heap2m(
    TeguGPUBuffer *,IOMemoryDescriptor *,OSObject *,tegu_fs_surface_page_fn,uint64_t,
    const struct tegu_fs_surface_layout *,const struct tegu_fs_resource_span *,size_t,
    uint64_t,volatile uint64_t *,uint64_t,const struct tegu_fs_reserved_context *);
struct tegu_fullscreen_backing *tegu_fullscreen_backing_create_b3_heap2m(
    TeguGPUBuffer *,TeguGPUBuffer *,uint64_t,const struct tegu_fs_surface_layout *,
    const struct tegu_fs_resource_span *,size_t,uint64_t,volatile uint64_t *,uint64_t,
    const struct tegu_fs_reserved_context *);
enum fs_disposition tegu_fullscreen_backing_map(
    struct tegu_fullscreen_backing *backing, const struct fs_ops *ops);
struct tegu_full_render_job *tegu_fullscreen_backing_job(
    struct tegu_fullscreen_backing *backing);
struct fs_mapping *tegu_fullscreen_backing_mapping(
    struct tegu_fullscreen_backing *backing);
size_t tegu_fullscreen_backing_heap_buffer_count(const struct tegu_fullscreen_backing *);
TeguGPUBuffer *tegu_fullscreen_backing_heap_buffer(const struct tegu_fullscreen_backing *,size_t);
size_t tegu_fullscreen_backing_resource_count(
    const struct tegu_fullscreen_backing *backing);
const struct tegu_fs_resource_binding *tegu_fullscreen_backing_resource(
    const struct tegu_fullscreen_backing *backing, size_t index);
int tegu_fullscreen_backing_resolve_resource(
    const struct tegu_fullscreen_backing *backing, size_t index,
    uint64_t buffer_offset, uint64_t byte_length, uint64_t *gpu_va);
/* gpu_known_idle=false permanently roots all owned buffers and map metadata. */
void tegu_fullscreen_backing_finish(struct tegu_fullscreen_backing *backing,
    int gpu_known_idle);
#endif
