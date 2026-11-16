#ifndef TEGU_COMPOSITOR_PUBLIC_IOSURFACE_LAYOUT_H
#define TEGU_COMPOSITOR_PUBLIC_IOSURFACE_LAYOUT_H
#include <stddef.h>
#include <stdint.h>

#define TEGU_PUBLIC_SURFACE_WIDTH 16u
#define TEGU_PUBLIC_SURFACE_HEIGHT 16u
#define TEGU_PUBLIC_SURFACE_ROW_BYTES 128u
#define TEGU_PUBLIC_SURFACE_OFFSET 0u
#define TEGU_PUBLIC_SURFACE_REQUEST_ALLOC_BYTES 16384u
#define TEGU_PUBLIC_SURFACE_MAX_ALLOC_BYTES 16384u
#define TEGU_PUBLIC_SURFACE_PIXEL_BYTES (16u * 16u * 4u)

struct tegu_public_surface_layout {
    uint32_t width, height, row_bytes, offset, allocation_bytes;
};

static inline int tegu_public_surface_layout_valid(const struct tegu_public_surface_layout *l) {
    if (!l || l->width != TEGU_PUBLIC_SURFACE_WIDTH ||
        l->height != TEGU_PUBLIC_SURFACE_HEIGHT ||
        l->row_bytes != TEGU_PUBLIC_SURFACE_ROW_BYTES ||
        l->offset != TEGU_PUBLIC_SURFACE_OFFSET ||
        l->allocation_bytes != TEGU_PUBLIC_SURFACE_REQUEST_ALLOC_BYTES ||
        (l->allocation_bytes & 4095u)) return 0;
    return (uint64_t)l->offset + (uint64_t)l->row_bytes*l->height <= l->allocation_bytes;
}

/* Verify visible BGRA bytes plus all bytes outside the target rectangle. */
static inline int tegu_public_surface_verify(const unsigned char *base,
    const struct tegu_public_surface_layout *l, const unsigned char *expected,
    unsigned char canary, uint32_t *pixel_mismatches, uint32_t *guard_mismatches) {
    if (!base || !expected || !tegu_public_surface_layout_valid(l) ||
        !pixel_mismatches || !guard_mismatches) return 0;
    uint32_t pixels=0, guards=0;
    for (uint32_t i=0;i<l->offset;i++) guards += base[i] != canary;
    for (uint32_t y=0;y<l->height;y++) {
        const unsigned char *row=base+l->offset+(size_t)y*l->row_bytes;
        for (uint32_t x=0;x<l->width*4u;x++)
            pixels += row[x] != expected[(size_t)y*l->width*4u+x];
        for (uint32_t x=l->width*4u;x<l->row_bytes;x++) guards += row[x] != canary;
    }
    uint32_t end=l->offset+l->row_bytes*l->height;
    for (uint32_t i=end;i<l->allocation_bytes;i++) guards += base[i] != canary;
    *pixel_mismatches=pixels;*guard_mismatches=guards;
    return 1;
}
#endif
