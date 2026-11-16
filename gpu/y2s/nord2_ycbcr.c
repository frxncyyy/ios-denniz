/*
 * Two-plane 4:2:0 YCbCr ('420f' / '420v': a plane of Y, a plane of Cb,Cr pairs at half size each way) to BGRA8, on the CPU.
 *
 * Why the driver needs it.  QuartzCore's Metal compositor, on a device that answers YES to -supportsFamily:MTLGPUFamilyApple1 (this one does),
 * makes ONE texture of the private format 500 (MTLPixelFormatYCBCR8_420_2P) over such a surface and draws it with its ordinary image shader:
 * CA::OGL::four_cc_to_mtl_format gives 500 for '420f'/'420v' when bit 2 of its MetalLimits is set (new_metal_context sets it from
 * supportsFamily:1001), and CA::OGL::MetalContext::update_surface then takes neither its per-plane branch (R8 + RG8 textures, image function
 * "biplanar", the shader's own ycbcr matrices) nor records the matrix.  So sampling that texture has to give RGB already, as Apple's GPUs do;
 * the Mali samples the two planes raw (Y, Cb, Cr in r, g, b: white comes out pink and black teal, run q10hd).  The driver therefore serves a
 * format-500 surface as a BGRA texture of its own, converted here whenever the surface changes (redwoodmetal.m, the y2s37 patch).
 *
 * The matrix is the one CA::Render::iosurface_ycbcr_matrix would use: IOSurfaceGetYCbCrMatrix's code point 1 is BT.709, 9 is BT.2020, anything
 * else and no answer are BT.601; the range is the FourCC's ('420f' full, '420v' video).  Chroma is taken per 2x2 block, not interpolated.
 *
 * Plain C, built -O3 so that clang vectorises the row loop; the same source is tested on the host (nord2_ycbcr_test.c).
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include <stddef.h>
#include <stdint.h>

#define NORD2_YCBCR_601         0
#define NORD2_YCBCR_709         1
#define NORD2_YCBCR_2020        2

#define Q 13                    /* fixed point: the largest term is 255 * 2.2 * 8192, far inside 32 bits */

struct coefficients { int32_t y_offset, y, rv, gu, gv, bu; };

static struct coefficients
coefficients_for(int matrix, int full_range)
{
	static const double kr_kb[3][2] = { { 0.299, 0.114 }, { 0.2126, 0.0722 }, { 0.2627, 0.0593 } };
	const double kr = kr_kb[matrix == NORD2_YCBCR_709 ? 1 : matrix == NORD2_YCBCR_2020 ? 2 : 0][0];
	const double kb = kr_kb[matrix == NORD2_YCBCR_709 ? 1 : matrix == NORD2_YCBCR_2020 ? 2 : 0][1];
	const double kg = 1.0 - kr - kb;
	const double ys = full_range ? 1.0 : 255.0 / 219.0, cs = full_range ? 1.0 : 255.0 / 224.0, one = (double)(1 << Q);
	struct coefficients k;

	k.y_offset = full_range ? 0 : 16;
	k.y = (int32_t)(ys * one + 0.5);
	k.rv = (int32_t)(2.0 * (1.0 - kr) * cs * one + 0.5);
	k.bu = (int32_t)(2.0 * (1.0 - kb) * cs * one + 0.5);
	k.gu = (int32_t)(2.0 * kb * (1.0 - kb) / kg * cs * one + 0.5);
	k.gv = (int32_t)(2.0 * kr * (1.0 - kr) / kg * cs * one + 0.5);
	return k;
}

static inline uint8_t
clamp8(int32_t v)
{
	return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* one row: `pairs` pairs of pixels sharing a chroma sample each, then one more pixel when the width is odd */
static void
row(const uint8_t *restrict luma, const uint8_t *restrict chroma, uint8_t *restrict bgra, unsigned width, const struct coefficients k)
{
	const unsigned pairs = width / 2;
	const int32_t half = 1 << (Q - 1);

	for (unsigned i = 0; i < pairs; i++) {
		const int32_t u = (int32_t)chroma[2 * i] - 128, v = (int32_t)chroma[2 * i + 1] - 128;
		const int32_t r = k.rv * v + half, g = -k.gu * u - k.gv * v + half, b = k.bu * u + half;
		int32_t y0 = (int32_t)luma[2 * i] - k.y_offset, y1 = (int32_t)luma[2 * i + 1] - k.y_offset;

		y0 = (y0 < 0 ? 0 : y0) * k.y;
		y1 = (y1 < 0 ? 0 : y1) * k.y;
		bgra[8 * i + 0] = clamp8((y0 + b) >> Q);
		bgra[8 * i + 1] = clamp8((y0 + g) >> Q);
		bgra[8 * i + 2] = clamp8((y0 + r) >> Q);
		bgra[8 * i + 3] = 255;
		bgra[8 * i + 4] = clamp8((y1 + b) >> Q);
		bgra[8 * i + 5] = clamp8((y1 + g) >> Q);
		bgra[8 * i + 6] = clamp8((y1 + r) >> Q);
		bgra[8 * i + 7] = 255;
	}
	if (width & 1) {
		const int32_t u = (int32_t)chroma[2 * pairs] - 128, v = (int32_t)chroma[2 * pairs + 1] - 128;
		int32_t y0 = (int32_t)luma[2 * pairs] - k.y_offset;

		y0 = (y0 < 0 ? 0 : y0) * k.y;
		bgra[8 * pairs + 0] = clamp8((y0 + k.bu * u + half) >> Q);
		bgra[8 * pairs + 1] = clamp8((y0 - k.gu * u - k.gv * v + half) >> Q);
		bgra[8 * pairs + 2] = clamp8((y0 + k.rv * v + half) >> Q);
		bgra[8 * pairs + 3] = 255;
	}
}

/*
 * `luma`: height rows of width bytes, luma_row apart.  `chroma`: (height + 1) / 2 rows of (width + 1) / 2 Cb,Cr pairs, chroma_row apart.
 * `bgra`: height rows of width * 4 bytes, bgra_row apart (B, G, R, 255: MTLPixelFormatBGRA8Unorm).  The caller has checked the planes hold that much.
 */
__attribute__((visibility("hidden"))) void
nord2_ycbcr420_to_bgra(const uint8_t *luma, size_t luma_row, const uint8_t *chroma, size_t chroma_row, uint8_t *bgra, size_t bgra_row,
		       unsigned width, unsigned height, int matrix, int full_range)
{
	const struct coefficients k = coefficients_for(matrix, full_range);

	for (unsigned y = 0; y < height; y++) {
		row(luma + (size_t)y * luma_row, chroma + (size_t)(y / 2) * chroma_row, bgra + (size_t)y * bgra_row, width, k);
	}
}
