/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_pixels.c -- MM21 (16x32 luma / 16x16 chroma tiles) and linear NV12/I420 to an NV12 CVPixelBuffer.
 *
 * See n2vtd_pixels.h for the evidence on the tile geometry.  Every read and write is bounds-checked against the
 * geometry the caller states; a mismatch returns -1 instead of touching memory the daemon did not describe.
 */
#include "n2vtd_pixels.h"

#include <string.h>

#define MM21_Y_TILE_W 16u
#define MM21_Y_TILE_H 32u
#define MM21_C_TILE_W 16u   /* bytes = 8 CbCr pairs */
#define MM21_C_TILE_H 16u   /* rows */

size_t n2vtd_frame_bytes(uint32_t format, uint32_t buf_w, uint32_t buf_h)
{
	if (buf_w == 0 || buf_h == 0 || (buf_w & 1u) || (buf_h & 1u))
		return 0;
	switch (format) {
	case N2VTD_FMT_NV12:
	case N2VTD_FMT_MM21:
		/* 4:2:0 with an interleaved chroma plane; the tight size is buf_w bytes per chroma row */
		return (size_t)buf_w * buf_h + (size_t)buf_w * (buf_h / 2u);
	case N2VTD_FMT_I420:
		return (size_t)buf_w * buf_h + (size_t)(buf_w / 2u) * (buf_h / 2u) * 2u;
	default:
		return 0;
	}
}

/* Byte offset of luma pixel (x, y) in an MM21 luma plane of pitch `stride`. */
static size_t mm21_y_off(size_t stride, uint32_t x, uint32_t y)
{
	size_t tiles_per_row = stride / MM21_Y_TILE_W;
	size_t tile = (size_t)(y / MM21_Y_TILE_H) * tiles_per_row + (x / MM21_Y_TILE_W);

	return tile * (MM21_Y_TILE_W * MM21_Y_TILE_H) + (size_t)(y % MM21_Y_TILE_H) * MM21_Y_TILE_W +
	       (x % MM21_Y_TILE_W);
}

/* Byte offset of the Cb sample of chroma pixel (cx, cy) in the interleaved MM21 chroma plane of pitch `stride`.
 * The chroma tile is 16 BYTES wide (8 CbCr pairs) and 16 rows high, 256 bytes: this is the libyuv-verified MM21
 * layout (facts/MM21_LAYOUT.md section 2, byte(xb, yc) = (yc/16)*stride*16 + (xb/16)*256 + (yc%16)*16 + xb%16 with
 * xb = 2*cx), not the 32-byte tile an earlier version of this file assumed. */
static size_t mm21_c_off(size_t stride, uint32_t cx, uint32_t cy)
{
	uint32_t xb = cx * 2u;

	return (size_t)(cy / MM21_C_TILE_H) * stride * MM21_C_TILE_H + (size_t)(xb / MM21_C_TILE_W) * 256u +
	       (size_t)(cy % MM21_C_TILE_H) * MM21_C_TILE_W + (size_t)(xb % MM21_C_TILE_W);
}

static int geometry_ok(uint32_t buf_w, uint32_t buf_h, uint32_t crop_x, uint32_t crop_y, uint32_t crop_w,
                       uint32_t crop_h)
{
	if (buf_w == 0 || buf_h == 0 || crop_w == 0 || crop_h == 0)
		return 0;
	if (crop_x >= buf_w || crop_y >= buf_h)
		return 0;
	if (crop_w > buf_w - crop_x || crop_h > buf_h - crop_y)
		return 0;
	return 1;
}

int n2vtd_copy_to_nv12(uint32_t format, const struct n2vtd_src_plane *y, const struct n2vtd_src_plane *c,
                       uint32_t buf_w, uint32_t buf_h, uint32_t crop_x, uint32_t crop_y, uint32_t crop_w,
                       uint32_t crop_h, uint8_t *dst_y, size_t dst_y_stride, uint8_t *dst_c, size_t dst_c_stride)
{
	uint32_t dx, dy, cx, cy;

	if (!y || !c || !dst_y || !dst_c)
		return -1;
	if (!geometry_ok(buf_w, buf_h, crop_x, crop_y, crop_w, crop_h))
		return -1;
	if (format == N2VTD_FMT_MM21) {
		/* Tile geometry: a luma tile row is 16 bytes, a chroma tile row 32; a chroma tile is 16 rows high, so the
		 * plane height must be a multiple of 32 rows (buf_h a multiple of 32) or the last tile would read past the
		 * plane.  Widths whose stride is not a multiple of 32 bytes are refused (see STATUS.md: the exact pitch the
		 * vendor uses for them is unverified). */
		if ((y->stride % MM21_Y_TILE_W) != 0 || (c->stride % MM21_C_TILE_W) != 0 || (buf_h % 32u) != 0)
			return -1;
		if ((crop_x & 1u) || (crop_y & 1u))
			return -1;
		for (dy = 0; dy < crop_h; dy++) {
			uint8_t *drow = dst_y + (size_t)dy * dst_y_stride;

			for (dx = 0; dx < crop_w; dx++)
				drow[dx] = y->data[mm21_y_off(y->stride, crop_x + dx, crop_y + dy)];
		}
		for (cy = 0; cy < (crop_h + 1u) / 2u; cy++) {
			uint8_t *drow = dst_c + (size_t)cy * dst_c_stride;

			for (cx = 0; cx < (crop_w + 1u) / 2u; cx++) {
				size_t off = mm21_c_off(c->stride, (crop_x / 2u) + cx, (crop_y / 2u) + cy);

				drow[cx * 2u] = c->data[off];
				drow[cx * 2u + 1u] = c->data[off + 1u];
			}
		}
		return 0;
	}
	if (format == N2VTD_FMT_NV12) {
		if ((crop_x & 1u) || (crop_y & 1u))
			return -1;
		for (dy = 0; dy < crop_h; dy++)
			memcpy(dst_y + (size_t)dy * dst_y_stride, y->data + (size_t)(crop_y + dy) * y->stride + crop_x, crop_w);
		for (cy = 0; cy < (crop_h + 1u) / 2u; cy++)
			memcpy(dst_c + (size_t)cy * dst_c_stride,
			       c->data + (size_t)((crop_y / 2u) + cy) * c->stride + (crop_x / 2u) * 2u,
			       (crop_w + 1u) / 2u * 2u);
		return 0;
	}
	if (format == N2VTD_FMT_I420) {
		/* Three planes cannot travel in two: use n2vtd_copy_i420_to_nv12(). */
		return -1;
	}
	return -1;
}

int n2vtd_copy_i420_to_nv12(const struct n2vtd_src_plane *yp, const struct n2vtd_src_plane *u,
                            const struct n2vtd_src_plane *v, uint32_t buf_w, uint32_t buf_h, uint32_t crop_x,
                            uint32_t crop_y, uint32_t crop_w, uint32_t crop_h, uint8_t *dst_y, size_t dst_y_stride,
                            uint8_t *dst_c, size_t dst_c_stride)
{
	uint32_t dy, cx, cy;

	if (!yp || !u || !v || !dst_y || !dst_c)
		return -1;
	if (!geometry_ok(buf_w, buf_h, crop_x, crop_y, crop_w, crop_h))
		return -1;
	if ((crop_x & 1u) || (crop_y & 1u))
		return -1;
	for (dy = 0; dy < crop_h; dy++)
		memcpy(dst_y + (size_t)dy * dst_y_stride, yp->data + (size_t)(crop_y + dy) * yp->stride + crop_x, crop_w);
	for (cy = 0; cy < (crop_h + 1u) / 2u; cy++) {
		uint8_t *drow = dst_c + (size_t)cy * dst_c_stride;
		size_t urow = (size_t)((crop_y / 2u) + cy) * u->stride + (crop_x / 2u);
		size_t vrow = (size_t)((crop_y / 2u) + cy) * v->stride + (crop_x / 2u);

		for (cx = 0; cx < (crop_w + 1u) / 2u; cx++) {
			drow[cx * 2u] = u->data[urow + cx];
			drow[cx * 2u + 1u] = v->data[vrow + cx];
		}
	}
	return 0;
}
