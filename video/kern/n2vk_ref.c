/*
 * n2vk_ref.c -- the processor reference of the three video kernels of include/n2vk.h.
 *
 * This file is the specification in code: the GPU shaders of gpu/air_video.c are checked
 * against it bit for bit on the host.  All arithmetic is integer; the only floating point
 * is the one-time construction of the JPEG DCT table, which the header defines that way
 * (lround(8192 * ...)), exactly as the T.81 transform's constants are written down.
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2vk.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------------------------------------------------------------
 * K1: 16x16 full-search motion estimation.
 * ----------------------------------------------------------------------------------------------------------------------------- */

int
n2vk_ref_me16(const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad)
{
	if (a == NULL || mvx == NULL || mvy == NULL || sad == NULL)
		return N2VK_E_ARGUMENT;
	if (a->size < sizeof(*a))
		return N2VK_E_ARGUMENT;
	if (a->cur == NULL || a->ref == NULL)
		return N2VK_E_ARGUMENT;
	if (a->mb_w < 1 || a->mb_w > 256 || a->mb_h < 1 || a->mb_h > 144)
		return N2VK_E_ARGUMENT;
	if (a->pad > 64 || a->range > 32)
		return N2VK_E_ARGUMENT;
	if (a->cur_stride < 16u * a->mb_w || a->ref_stride < 16u * a->mb_w + 2u * a->pad)
		return N2VK_E_ARGUMENT;
	if ((a->centre_x == NULL) != (a->centre_y == NULL))
		return N2VK_E_ARGUMENT;

	const int32_t pad = (int32_t)a->pad;
	const int32_t range = (int32_t)a->range;
	/* sample (0, 0) of the extended reference, i.e. coordinate (-pad, -pad).  Written once
	 * so the inner loop indexes with non-negative numbers. */
	const uint8_t *ref0 = a->ref - (ptrdiff_t)pad * (ptrdiff_t)a->ref_stride - (ptrdiff_t)pad;

	for (uint32_t my = 0; my < a->mb_h; my++) {
		for (uint32_t mx = 0; mx < a->mb_w; mx++) {
			const uint32_t i = my * a->mb_w + mx;
			/* The rectangle of top-left places for which the block is inside the
			 * reference extended by pad on each side (the header's two inequalities). */
			const int32_t lo_x = -pad - 16 * (int32_t)mx;
			const int32_t hi_x = 16 * ((int32_t)a->mb_w - (int32_t)mx - 1) + pad;
			const int32_t lo_y = -pad - 16 * (int32_t)my;
			const int32_t hi_y = 16 * ((int32_t)a->mb_h - (int32_t)my - 1) + pad;
			int32_t cx = a->centre_x != NULL ? (int32_t)a->centre_x[i] : 0;
			int32_t cy = a->centre_y != NULL ? (int32_t)a->centre_y[i] : 0;

			if (cx < lo_x)
				cx = lo_x;
			if (cx > hi_x)
				cx = hi_x;
			if (cy < lo_y)
				cy = lo_y;
			if (cy > hi_y)
				cy = hi_y;

			const int32_t first_x = cx - range > lo_x ? cx - range : lo_x;
			const int32_t last_x = cx + range < hi_x ? cx + range : hi_x;
			const int32_t first_y = cy - range > lo_y ? cy - range : lo_y;
			const int32_t last_y = cy + range < hi_y ? cy + range : hi_y;

			uint32_t best_cost = UINT32_MAX;
			int32_t best_dx = 0, best_dy = 0, best_rank = INT32_MAX;

			const uint8_t *cur_mb = a->cur + (ptrdiff_t)(16u * my) * (ptrdiff_t)a->cur_stride + 16u * mx;

			for (int32_t dy = first_y; dy <= last_y; dy++) {
				const uint8_t *ref_row = ref0 + (ptrdiff_t)(16 * (int32_t)my + dy + pad) * (ptrdiff_t)a->ref_stride;

				for (int32_t dx = first_x; dx <= last_x; dx++) {
					const uint8_t *ref_mb = ref_row + 16 * (int32_t)mx + dx + pad;
					uint32_t cost = 0;

					for (int32_t y = 0; y < 16; y++) {
						const uint8_t *c = cur_mb + (ptrdiff_t)y * (ptrdiff_t)a->cur_stride;
						const uint8_t *r = ref_mb + (ptrdiff_t)y * (ptrdiff_t)a->ref_stride;

						for (int32_t x = 0; x < 16; x++)
							cost += (uint32_t)(c[x] > r[x] ? c[x] - r[x] : r[x] - c[x]);
					}

					const int32_t rank = (dx > cx ? dx - cx : cx - dx) + (dy > cy ? dy - cy : cy - dy);

					if (cost < best_cost || (cost == best_cost &&
					    (rank < best_rank || (rank == best_rank &&
					     (dy < best_dy || (dy == best_dy && dx < best_dx)))))) {
						best_cost = cost;
						best_dx = dx;
						best_dy = dy;
						best_rank = rank;
					}
				}
			}

			/* The candidate set is never empty: clamp() put the centre inside the
			 * allowed rectangle, which contains at least one place (0 in x for a
			 * one-macroblock picture with pad 0), and the range window includes it. */
			mvx[i] = (int16_t)best_dx;
			mvy[i] = (int16_t)best_dy;
			sad[i] = best_cost;
		}
	}
	return N2VK_OK;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * K2: the half-size picture.
 * ----------------------------------------------------------------------------------------------------------------------------- */

int
n2vk_ref_half(const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t width, uint32_t height)
{
	if (src == NULL || dst == NULL)
		return N2VK_E_ARGUMENT;
	if (width == 0 || height == 0 || width > UINT32_MAX / 2 || height > UINT32_MAX / 2)
		return N2VK_E_ARGUMENT;
	if (src_stride < 2u * width || dst_stride < width)
		return N2VK_E_ARGUMENT;

	for (uint32_t y = 0; y < height; y++) {
		const uint8_t *s0 = src + (ptrdiff_t)(2u * y) * (ptrdiff_t)src_stride;
		const uint8_t *s1 = s0 + src_stride;
		uint8_t *d = dst + (ptrdiff_t)y * (ptrdiff_t)dst_stride;

		for (uint32_t x = 0; x < width; x++) {
			const uint32_t sum = (uint32_t)s0[2u * x] + (uint32_t)s0[2u * x + 1] +
			                     (uint32_t)s1[2u * x] + (uint32_t)s1[2u * x + 1];
			d[x] = (uint8_t)((sum + 2u) >> 2);
		}
	}
	return N2VK_OK;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * K3: JPEG forward DCT + quantisation.
 *
 * The table is n2vk.h's:
 *     c[u][x] = lround(8192 * (u == 0 ? sqrt(1/8) : 0.5 * cos((2x + 1) * u * pi / 16)))
 * and is computed once, with lround, on the first call.  A literal copy of it lives in the
 * test (kern/test/t_kern.c) and is the check that this arithmetic has not drifted.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static int32_t dct_c[8][8];
static atomic_int dct_state; /* 0 unbuilt, 1 building, 2 built */

static void
dct_build(void)
{
	for (int u = 0; u < 8; u++) {
		for (int x = 0; x < 8; x++) {
			const double scale = u == 0 ? sqrt(1.0 / 8.0)
			                            : 0.5 * cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0);

			dct_c[u][x] = (int32_t)lround(8192.0 * scale);
		}
	}
}

static const int32_t (*
dct_table(void))[8]
{
	int state = atomic_load_explicit(&dct_state, memory_order_acquire);

	if (state != 2) {
		int expected = 0;

		if (atomic_compare_exchange_strong_explicit(&dct_state, &expected, 1, memory_order_acq_rel, memory_order_acquire)) {
			dct_build();
			atomic_store_explicit(&dct_state, 2, memory_order_release);
		} else {
			while (atomic_load_explicit(&dct_state, memory_order_acquire) != 2)
				; /* another thread is building it; the build writes only dct_c */
		}
	}
	return dct_c;
}

/* F / Q, rounded half away from zero (n2vk.h's third kernel).  Exposed so that the test
 * can check the four tie cases with a value that no 8-bit block produces exactly. */
int32_t
n2vk_ref_quant(int32_t f, uint16_t q)
{
	const int32_t magnitude = f < 0 ? -f : f;
	const int32_t rounded = (magnitude + (int32_t)(q >> 1)) / (int32_t)q;

	return f < 0 ? -rounded : rounded;
}

int
n2vk_ref_fdct8_quant(const uint8_t *src, uint32_t src_stride, uint32_t width, uint32_t height, const uint16_t qtable[64],
                     int16_t *coef)
{
	if (src == NULL || qtable == NULL || coef == NULL)
		return N2VK_E_ARGUMENT;
	if (width == 0 || height == 0 || (width & 7u) != 0 || (height & 7u) != 0)
		return N2VK_E_ARGUMENT;
	if (src_stride < width)
		return N2VK_E_ARGUMENT;
	for (int i = 0; i < 64; i++)
		if (qtable[i] == 0)
			return N2VK_E_ARGUMENT;

	const int32_t (*c)[8] = dct_table();
	int16_t *out = coef;

	for (uint32_t by = 0; by < height; by += 8) {
		for (uint32_t bx = 0; bx < width; bx += 8, out += 64) {
			int32_t t[8][8];

			/* pass 1 (rows): t[y][u] = (sum_x c[u][x] * f[y][x] + (1 << 9)) >> 10 */
			for (int y = 0; y < 8; y++) {
				const uint8_t *row = src + (ptrdiff_t)(by + (uint32_t)y) * (ptrdiff_t)src_stride + bx;
				int32_t f[8];

				for (int x = 0; x < 8; x++)
					f[x] = (int32_t)row[x] - 128;
				for (int u = 0; u < 8; u++) {
					int32_t sum = 0;

					for (int x = 0; x < 8; x++)
						sum += c[u][x] * f[x];
					t[y][u] = (sum + (1 << 9)) >> 10;
				}
			}
			/* pass 2 (columns): F[v][u] = (sum_y c[v][y] * t[y][u] + (1 << 15)) >> 16,
			 * then F / Q rounded half away from zero. */
			for (int v = 0; v < 8; v++) {
				for (int u = 0; u < 8; u++) {
					int32_t sum = 0;

					for (int y = 0; y < 8; y++)
						sum += c[v][y] * t[y][u];
					const int32_t f = (sum + (1 << 15)) >> 16;

					out[v * 8 + u] = (int16_t)n2vk_ref_quant(f, qtable[v * 8 + u]);
				}
			}
		}
	}
	return N2VK_OK;
}
