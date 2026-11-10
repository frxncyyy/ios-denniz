/*
 * n2vk.h -- "video kernels": the whole-picture stages of the codecs that are worth giving to the Mali, each defined in exact
 * integer arithmetic so that the processor's and the GPU's answers are the same bit for bit. LEAD-OWNED, FROZEN.
 *
 *   processor reference : kern/n2vk_ref.c           (WP08, plain C; this is the specification in code)
 *   GPU                 : gpu/air_video.c           (WP08, nir_builder compute/fragment shaders in the style of
 *                                                    gpu/build/y2s46-vt6/src/air_isp.c, reached through rwa_vid_* calls of
 *                                                    libredwoodair.dylib; the lead puts it into a library build)
 * The codecs call n2vk_*(); those go to the backend that n2vk_use() installed (the reference by default).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2VK_H
#define N2VK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2VK_API_VERSION 1
enum { N2VK_OK = 0, N2VK_E_ARGUMENT = -1, N2VK_E_UNSUPPORTED = -2, N2VK_E_GPU = -4, N2VK_E_MEMORY = -5 };

/* K1. Motion search for every 16x16 macroblock of a luma plane (integer samples, full search).
 *   For macroblock (mx, my), centre (cx, cy) = (centre_x[i], centre_y[i]) or (0, 0) when the arrays are NULL, i = my * mb_w + mx.
 *   Candidates: every (dx, dy) with |dx - cx| <= range and |dy - cy| <= range for which the 16x16 block at
 *   (16 * mx + dx, 16 * my + dy) lies inside the reference extended by `pad` samples on each side, i.e.
 *   -pad <= 16 * mx + dx  and  16 * mx + dx + 16 <= 16 * mb_w + pad   (same for y).
 *   cost(dx, dy) = sum over the 256 samples of |cur - ref|.
 *   The answer is the candidate of least cost; among equals the one with the least |dx - cx| + |dy - cy|, then the least dy,
 *   then the least dx. If the centre itself is not a candidate it is first clamped into the allowed rectangle.
 *   Outputs per macroblock: mvx, mvy (integer samples) and sad. */
typedef struct n2vk_me16_args {
	uint32_t size;                  /* sizeof(struct n2vk_me16_args) */
	const uint8_t *cur;             /* 16 * mb_w x 16 * mb_h */
	uint32_t cur_stride;
	const uint8_t *ref;             /* sample (0,0) of the reference; rows and columns -pad .. size + pad - 1 are readable */
	uint32_t ref_stride;
	uint32_t mb_w, mb_h;            /* 1..256 x 1..144 */
	uint32_t pad;                   /* 0..64 */
	uint32_t range;                 /* 0..32 */
	const int16_t *centre_x, *centre_y;     /* mb_w * mb_h entries each, or both NULL */
} n2vk_me16_args;
int n2vk_me16(const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad);

/* K2. Half-size picture: dst(x, y) = (s(2x, 2y) + s(2x+1, 2y) + s(2x, 2y+1) + s(2x+1, 2y+1) + 2) >> 2.
 *   width and height are those of dst; src is 2 * width x 2 * height. For the coarse level of a two-level motion search and
 *   for thumbnails. */
int n2vk_half(const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t width, uint32_t height);

/* K3. JPEG forward DCT + quantisation of a plane of 8-bit samples, block by block (width and height multiples of 8).
 *   Level shift by -128, then F = round-to-nearest(C * f * C^T) computed as two passes of 32-bit integer arithmetic:
 *     c[u][x] = lround(8192 * (u == 0 ? sqrt(1/8) : 0.5 * cos((2x + 1) * u * pi / 16)))         (a constant table, in n2vk_ref.c)
 *     pass 1 (rows):    t[y][u] = (sum_x c[u][x] * f[y][x] + (1 << 9))  >> 10                   (arithmetic shift)
 *     pass 2 (columns): F[v][u] = (sum_y c[v][y] * t[y][u] + (1 << 15)) >> 16
 *   then q = F[v][u] / Q[v][u] rounded half away from zero: q = (|F| + (Q >> 1)) / Q with the sign of F.
 *   Output: 64 int16 per block in natural (row-major v, u) order, blocks in raster order. qtable is in natural order too. */
int n2vk_fdct8_quant(const uint8_t *src, uint32_t src_stride, uint32_t width, uint32_t height, const uint16_t qtable[64],
                     int16_t *coef);

/* The backend. A NULL member means "use the reference for this kernel". n2vk_use(NULL) goes back to the reference. A backend
 * call that fails with N2VK_E_GPU or N2VK_E_UNSUPPORTED is answered by the reference instead (n2vk_* never fail for that), and
 * after three N2VK_E_GPU in a row that kernel stays on the reference until n2vk_use() is called again. */
typedef struct n2vk_backend {
	uint32_t size;
	const char *name;
	int (*me16)(void *ctx, const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad);
	int (*half)(void *ctx, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t w, uint32_t h);
	int (*fdct8_quant)(void *ctx, const uint8_t *src, uint32_t src_stride, uint32_t w, uint32_t h, const uint16_t q[64], int16_t *coef);
	void *ctx;
} n2vk_backend;
void        n2vk_use(const n2vk_backend *backend);
const char *n2vk_backend_name(void);
/* The reference itself, callable directly (tests compare a backend against these). */
int n2vk_ref_me16(const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad);
int n2vk_ref_half(const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t width, uint32_t height);
int n2vk_ref_fdct8_quant(const uint8_t *src, uint32_t src_stride, uint32_t width, uint32_t height, const uint16_t qtable[64], int16_t *coef);

#ifdef __cplusplus
}
#endif
#endif
