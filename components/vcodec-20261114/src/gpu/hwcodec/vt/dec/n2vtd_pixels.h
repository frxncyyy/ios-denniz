/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_pixels.h -- copy a decoded picture from the daemon's output buffer into a CVPixelBuffer.
 *
 * The MT6893 VDEC writes MM21: a two-plane 8-bit 4:2:0 layout in which the luma plane is tiled 16x32 and the
 * interleaved chroma plane 16x16.  The plane sizes the vendor reports for 1920x1080 (Y 2088960 = 1920 x 1088,
 * chroma 1044480 = 1920 x 544) are exactly the tiled equivalents of the linear planes, which pins the tile geometry.
 * The tile order itself (tile rows in raster order, pixels row-major inside a tile) is the MediaTek MM21 convention and
 * is marked [I] in STATUS.md until a phone run compares the result with ffmpeg.
 *
 * Also handles a linear NV12 output (if the daemon ever accepts the layout) and separate-plane I420, and applies the
 * clean-aperture origin (crop) while copying.
 */
#ifndef N2VTD_PIXELS_H
#define N2VTD_PIXELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	N2VTD_FMT_NV12 = 0x3231564eu,   /* 'NV12': linear, Y then interleaved CbCr */
	N2VTD_FMT_MM21 = 0x31324d4du,   /* 'MM21': tiled, Y 16x32, CbCr 16x16 */
	N2VTD_FMT_I420 = 0x30323449u,   /* 'I420': linear, three planes (source only) */
};

/* One source plane: data may be NULL for a layout that does not use it. */
struct n2vtd_src_plane {
	const uint8_t *data;
	size_t stride;                  /* bytes between the starts of two rows (MM21: the plane pitch) */
};

/* Copy `crop_w x crop_h` pixels starting at (crop_x, crop_y) of the source into the destination NV12 planes.
 * The source geometry is buf_w x buf_h (the coded, aligned picture the daemon reports); the destination strides come
 * from CVPixelBufferGetBytesPerRowOfPlane().  Returns 0, or -1 on a geometry the copy cannot express (odd crop origin
 * with subsampled chroma, a crop outside the source, a source/destination overlap). */
int n2vtd_copy_to_nv12(uint32_t format, const struct n2vtd_src_plane *y, const struct n2vtd_src_plane *c,
                       uint32_t buf_w, uint32_t buf_h, uint32_t crop_x, uint32_t crop_y, uint32_t crop_w,
                       uint32_t crop_h, uint8_t *dst_y, size_t dst_y_stride, uint8_t *dst_c, size_t dst_c_stride);

/* The same for a three-plane I420 source (U and V separate). */
int n2vtd_copy_i420_to_nv12(const struct n2vtd_src_plane *yp, const struct n2vtd_src_plane *u,
                            const struct n2vtd_src_plane *v, uint32_t buf_w, uint32_t buf_h, uint32_t crop_x,
                            uint32_t crop_y, uint32_t crop_w, uint32_t crop_h, uint8_t *dst_y, size_t dst_y_stride,
                            uint8_t *dst_c, size_t dst_c_stride);

/* Bytes the daemon needs for one output buffer of a layout (0 for an unknown layout). */
size_t n2vtd_frame_bytes(uint32_t format, uint32_t buf_w, uint32_t buf_h);

#ifdef __cplusplus
}
#endif
#endif /* N2VTD_PIXELS_H */
