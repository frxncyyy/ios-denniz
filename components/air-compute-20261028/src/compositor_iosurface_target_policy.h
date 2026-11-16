#ifndef TEGU_COMPOSITOR_IOSURFACE_TARGET_POLICY_H
#define TEGU_COMPOSITOR_IOSURFACE_TARGET_POLICY_H
#include <stdint.h>
#include "fullscreen_job_contract.h"
#include "imported_target_policy.h"
#define TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION 1u

/* A non-planar IOSurface reports planeCount==0; one explicit plane is also
 * accepted only when its complete geometry aliases the same BGRA allocation. */
struct tegu_compositor_iosurface_layout {
    uint32_t width, height, row_bytes, offset, pixel_format, allocation_size;
    uint32_t plane_count, plane_width, plane_height, plane_row_bytes;
    uint32_t plane_offset, plane_size;
    uint16_t bytes_per_element, plane_bytes_per_element;
    uint8_t element_width, element_height, plane_element_width, plane_element_height;
};
typedef int (*tegu_compositor_iosurface_page_fn)(void *ctx, uint64_t offset,
    uint64_t *physical);

static inline int tegu_compositor_iosurface_layout_validate(
    const struct tegu_compositor_iosurface_layout *s, uint64_t backing_length,
    struct tegu_fs_surface_layout *out)
{
    if (!s || !out || s->pixel_format != TEGU_IOSURFACE_PIXELFORMAT_BGRA ||
        s->plane_count > 1 || s->bytes_per_element != 4 ||
        s->element_width != 1 || s->element_height != 1 ||
        s->allocation_size != backing_length ||
        s->width < 16 || s->height < 16 ||
        s->width > TEGU_FS_MAX_DIMENSION || s->height > TEGU_FS_MAX_DIMENSION ||
        s->row_bytes < (uint64_t)s->width * 4 || s->row_bytes > 65536 ||
        (s->row_bytes & 63) ||
        (s->offset & 4095) || (s->allocation_size & 4095) ||
        s->allocation_size > TEGU_FS_MAX_BYTES + 4095u ||
        s->offset > backing_length ||
        (uint64_t)s->row_bytes * s->height > backing_length - s->offset)
        return 0;
    if (s->plane_count == 1 &&
        (s->plane_width != s->width || s->plane_height != s->height ||
         s->plane_row_bytes != s->row_bytes || s->plane_offset != s->offset ||
         s->plane_size < (uint64_t)s->row_bytes * s->height ||
         s->plane_bytes_per_element != 4 || s->plane_element_width != 1 ||
         s->plane_element_height != 1 || s->plane_offset > backing_length ||
         s->plane_size > backing_length - s->plane_offset)) return 0;
    return tegu_fs_surface_layout(s->width, s->height, s->row_bytes,
        s->offset, 0, out) == 0;
}

/* Shared importer preflight: prove every backing page covering target bytes
 * translates through the caller's trusted IOBuffer page resolver. */
static inline int tegu_compositor_iosurface_pages_validate(
    const struct tegu_compositor_iosurface_layout *s, uint64_t backing_length,
    struct tegu_fs_surface_layout *out, tegu_compositor_iosurface_page_fn resolve,
    void *ctx)
{
    if (!resolve || !tegu_compositor_iosurface_layout_validate(s, backing_length, out))
        return 0;
    uint64_t end = (uint64_t)s->offset + out->target_bytes;
    for (uint64_t offset = s->offset; offset < end; offset += 4096) {
        uint64_t physical = 0;
        if (!resolve(ctx, offset, &physical)) return 0;
    }
    return 1;
}
#endif
