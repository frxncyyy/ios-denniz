/* SPDX-License-Identifier: MIT */
/*
 * n2_mm21.h -- MediaTek MM21 (8-bit 4:2:0 block mode, V4L2 fourcc 'MM21', called V4L2_PIX_FMT_MT21 in the vendor kernel)
 * to linear NV12 conversion for the Nord2 (MT6893) hardware decoder output.  Owner: opus-mm21.  Layout: facts/MM21_LAYOUT.md.
 *
 * Layout (one picture = two planes, each its own buffer or offset):
 *   Y plane : tiles of 16 bytes x 32 rows (512 B, row-major inside the tile); tiles left to right form a tile row of
 *             y_stride * 32 bytes; tile rows top to bottom.  byte(x, y) = (y/32)*y_stride*32 + (x/16)*512 + (y%32)*16 + x%16
 *   UV plane: interleaved Cb,Cr bytes (as NV12), tiles of 16 bytes (8 pairs) x 16 rows (256 B), same rule with tile height 16:
 *             byte(xb, yc) = (yc/16)*uv_stride*16 + (xb/16)*256 + (yc%16)*16 + xb%16      (xb = byte column, yc = chroma row)
 *   Strides are multiples of 16 (the vendor uses vsi.pic.buf_w for both planes), the aligned height buf_h is a multiple of 32.
 *
 * All functions are pure CPU code: no allocation, no printing, no global state, thread-safe; disjoint row ranges of one
 * frame may be converted on several threads.  Return 0 or a negative errno (-EINVAL for a bad geometry).
 * With __ARM_NEON the inner row copy uses NEON (16-byte tile rows, four tiles per iteration); otherwise portable C.
 */
#ifndef N2_MM21_H
#define N2_MM21_H
#include <stddef.h>
#include <stdint.h>

#define N2_MM21_TILE_W    16u   /* bytes */
#define N2_MM21_Y_TILE_H  32u   /* rows  */
#define N2_MM21_UV_TILE_H 16u   /* rows  */

struct n2_mm21_frame {          /* source (decoder output) */
	const uint8_t *y, *uv;      /* plane origins (start of tile row 0) */
	uint32_t y_stride;          /* bytes per (virtual linear) luma row = vsi.pic.buf_w; multiple of 16 */
	uint32_t uv_stride;         /* bytes per chroma row; = y_stride for the vendor decoder; multiple of 16 */
	uint32_t buf_h;             /* aligned luma height (vsi.pic.buf_h, e.g. 1088); multiple of 32; 0 = do not check */
};

struct n2_nv12_out {            /* destination, e.g. a locked CVPixelBuffer (420v / 420f) */
	uint8_t *y, *uv;            /* pixel (0,0) of the output rectangle in each plane */
	size_t y_pitch, uv_pitch;   /* CVPixelBufferGetBytesPerRowOfPlane(pb, 0 / 1) */
};

/* Convert the rectangle (x, y, w, h) of the MM21 picture (x, y even: the crop offset; w, h >= 1, odd allowed: the last
 * chroma column pair / row is then the one covering the odd luma column / row) into NV12 at dst.  Writes exactly
 * w bytes x h rows of luma and 2*ceil(w/2) bytes x ceil(h/2) rows of chroma; never reads outside the tiles that cover
 * the rectangle.  -EINVAL: NULL pointer, stride not a multiple of 16, odd x/y, empty rectangle, rectangle outside
 * the stride / buf_h, destination pitch too small. */
int n2_mm21_to_nv12(const struct n2_mm21_frame *src, const struct n2_nv12_out *dst,
                    uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* One plane: copy bytes [x, x+w) of rows [y, y+h) of a tiled plane (tile width 16 bytes, tile height tile_h = 32 or 16,
 * origin `plane`, row pitch `stride`) to a linear plane.  No alignment rule on x/y here (bytes / rows). */
int n2_mm21_detile_plane(const uint8_t *plane, uint32_t stride, uint32_t tile_h,
                         uint8_t *dst, size_t dst_pitch, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Staged conversion for sources the CPU cannot address (RWVC DMA buffers on the phone: user access only by copy,
 * RWVC_DMA_RW).  For every luma tile row (and its chroma tile row) that meets the rectangle, fetch() is asked for the
 * contiguous byte range of that tile row covering the rectangle's tile columns; the bytes are detiled from `staging`
 * into dst while still in cache.  plane: 0 = Y, 1 = UV; offset/bytes are relative to that plane's origin.  fetch returns
 * 0 or a negative errno, which is returned unchanged.  staging must hold n2_mm21_staging_bytes(src_geometry, x, w)
 * bytes (1080p: 61440).  src->y / src->uv are ignored (may be NULL).  Same rectangle rules as n2_mm21_to_nv12. */
typedef int (*n2_mm21_fetch_fn)(void *ctx, int plane, uint32_t offset, uint32_t bytes, uint8_t *staging);
size_t n2_mm21_staging_bytes(const struct n2_mm21_frame *src, uint32_t x, uint32_t w);
int n2_mm21_to_nv12_staged(const struct n2_mm21_frame *src, n2_mm21_fetch_fn fetch, void *ctx,
                           uint8_t *staging, size_t staging_bytes, const struct n2_nv12_out *dst,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Plane sizes of a MM21 buffer of the given stride / aligned height: y = stride * buf_h, uv = y / 2 (fb_sz[0], fb_sz[1]). */
static inline size_t n2_mm21_y_bytes(uint32_t stride, uint32_t buf_h) { return (size_t)stride * buf_h; }
static inline size_t n2_mm21_uv_bytes(uint32_t stride, uint32_t buf_h) { return (size_t)stride * buf_h / 2u; }

/* Inverse (linear NV12 -> MM21), for tests and for a fake decoder that has to produce MM21: fills the whole
 * stride x buf_h tile area; source pixels outside w x h are taken by edge replication. */
int n2_nv12_to_mm21(const uint8_t *src_y, size_t src_y_pitch, const uint8_t *src_uv, size_t src_uv_pitch,
                    uint32_t w, uint32_t h, uint8_t *dst_y, uint8_t *dst_uv, uint32_t stride, uint32_t buf_h);

/* opus-decint: n2_mm21_to_nv12() split into `threads` horizontal bands (multiples of 32 luma rows), bands 1.. on their
 * own pthreads, band 0 on the caller; byte-identical result.  threads 0/1 = single pass.  Phone (vc27): one NEON pass
 * is 8.8 ms per 3840x2160 picture.  Returns the first band's error. */
int n2_mm21_to_nv12_mt(const struct n2_mm21_frame *src, const struct n2_nv12_out *dst,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h, unsigned threads);

/* Which inner loop this build uses: "neon" or "c". */
const char *n2_mm21_impl(void);
#endif
