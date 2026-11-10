/*
 * n2avc_me.c -- integer-sample motion search for one 16x16 macroblock.
 *
 * Written from ITU-T H.264 clause 8.4 (motion vectors, mvd coding) and the
 * rate-distortion model stated in the task.  No code from x264, FFmpeg,
 * OpenH264 or JM was used or consulted.
 *
 * Algorithm (task text, verbatim in the comments where it is a requirement):
 *   - start candidates: (0,0), the 16x16 prediction rounded to whole samples,
 *     the left / above / above-right neighbours' vectors;
 *   - then a small-diamond descent (the four axial neighbours), repeated while
 *     the cost improves, at most N2AVC_ME_MAX_STEPS steps;
 *   - cost = SAD + lambda * (bits(se(mvd_x)) + bits(se(mvd_y))), with
 *     bits(se(v)) = 2*floor(log2(k+1)) + 1, k = 2|v| - (v > 0 ? 1 : 0);
 *   - lambda = max(1, round(sqrt(0.85 * 2^((qp - 12)/3)))), the 52-entry
 *     table below (computed once from that formula, not at run time);
 *   - a vector is legal only when |v| <= search_range and the 16x16 luma block
 *     plus the 8x8 chroma blocks (including their +1 sample for the bilinear
 *     interpolation) stay inside the reference extended by N2AVC_PAD - 8 luma
 *     samples.
 * When N2AVC_ME_FULL is defined, or a non-reference n2vk backend is installed,
 * the n2vk_me16 answer for the slice is added as a start candidate; the
 * diamond descent then refines it for the rate-distortion cost (n2vk_me16
 * minimises SAD only).  n2vk_me16 is referenced weakly, so a build without the
 * kernel objects still links and uses the diamond search alone.
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2avc_inter_priv.h"

#include <stdlib.h>
#include <string.h>

#include "n2vk.h"

#if defined(__APPLE__)
extern int n2vk_me16(const n2vk_me16_args *, int16_t *, int16_t *, uint32_t *)
	__attribute__((weak_import));
extern const char *n2vk_backend_name(void) __attribute__((weak_import));
#else
extern int n2vk_me16(const n2vk_me16_args *, int16_t *, int16_t *, uint32_t *)
	__attribute__((weak));
extern const char *n2vk_backend_name(void) __attribute__((weak));
#endif

/* lambda[qp] = max(1, round(sqrt(0.85 * 2^((qp - 12)/3)))).  The values are
 * literals so that neither the encoder nor the tests need a libm call (the
 * iOS plug-in therefore imports no new symbol). */
static const int32_t lambda_table[52] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 10,
	12, 13, 15, 17, 19, 21, 23, 26, 30, 33, 37, 42, 47, 53, 59, 66, 74, 83
};

int
n2avc_me_lambda(int qp)
{
	if (qp < 0)
		qp = 0;
	if (qp > 51)
		qp = 51;
	return (int)lambda_table[qp];
}

int
n2avc_se_bits(int32_t v)
{
	/* k = 2|v| - (v > 0 ? 1 : 0); code length 2*floor(log2(k + 1)) + 1.
	 * int64 keeps the expression defined for INT32_MIN, although the search
	 * only produces small values. */
	int64_t k = 2 * (v < 0 ? -(int64_t)v : (int64_t)v) - (v > 0 ? 1 : 0);
	int64_t m = k + 1;
	int n = 0;

	while (m > 1) {
		m >>= 1;
		n++;
	}
	return 2 * n + 1;
}

/* Quarter samples to the nearest whole sample (half away from zero). */
static int
qpel_to_int(int q)
{
	if (q >= 0)
		return (q + 2) >> 2;
	return -((2 - q) >> 2);
}

/* Both the luma block and the two chroma blocks, including the +1 sample of
 * clause 8.4.2.2.2, must fit in the reference border. */
int
n2avc_me_valid(const n2avc_slice *s, int mbx, int mby, int dx, int dy)
{
	const int range = s->search_range < 0 ? 0 : s->search_range;
	const int bw = s->mb_w * 16;
	const int bh = s->mb_h * 16;
	const int margin = N2AVC_PAD - 8;        /* 24 luma samples */
	const int padc = N2AVC_PAD / 2;          /* 16 chroma samples */
	const int bx = mbx * 16;
	const int by = mby * 16;
	int x_int, y_int;

	if (dx < -range || dx > range || dy < -range || dy > range)
		return 0;
	if (bx + dx < -margin || bx + dx + 16 > bw + margin)
		return 0;
	if (by + dy < -margin || by + dy + 16 > bh + margin)
		return 0;

	/* Chroma vector is mvx/8 in chroma samples; for an integer luma search
	 * this is dx/2 with the floor of clause 8.4.2.2.1 written as an
	 * arithmetic shift.  The bilinear filter reads xInt..xInt+8. */
	x_int = mbx * 8 + (dx >> 1);
	y_int = mby * 8 + (dy >> 1);
	if (x_int < -padc || x_int + 8 > s->mb_w * 8 + padc - 1)
		return 0;
	if (y_int < -padc || y_int + 8 > s->mb_h * 8 + padc - 1)
		return 0;
	return 1;
}

static int
sad16(const uint8_t *cur, int cur_stride, const uint8_t *ref, int ref_stride)
{
	int sum = 0;
	int y, x;

	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++) {
			int d = (int)cur[x] - (int)ref[x];
			sum += d < 0 ? -d : d;
		}
		cur += cur_stride;
		ref += ref_stride;
	}
	return sum;
}

static const uint8_t *
cur_block(const n2avc_slice *s, int mbx, int mby)
{
	return s->src->y.p + (mby * 16) * s->src->y.stride + mbx * 16;
}

static const uint8_t *
ref_block(const n2avc_slice *s, int mbx, int mby, int dx, int dy)
{
	return s->ref->y.p + (mby * 16 + dy) * s->ref->y.stride + mbx * 16 + dx;
}

/* Clamp a candidate into the legal rectangle.  Returns 0 when even the
 * clamped candidate is invalid (cannot happen for a block inside the picture,
 * but the search never trusts that). */
static int
clamp_candidate(const n2avc_slice *s, int mbx, int mby, int *dx, int *dy)
{
	const int range = s->search_range < 0 ? 0 : s->search_range;
	const int margin = N2AVC_PAD - 8;
	const int bx = mbx * 16;
	const int by = mby * 16;
	const int lo_x = -margin - bx > -range ? -margin - bx : -range;
	const int hi_x = s->mb_w * 16 + margin - 16 - bx < range
	                 ? s->mb_w * 16 + margin - 16 - bx : range;
	const int lo_y = -margin - by > -range ? -margin - by : -range;
	const int hi_y = s->mb_h * 16 + margin - 16 - by < range
	                 ? s->mb_h * 16 + margin - 16 - by : range;

	if (lo_x > hi_x || lo_y > hi_y)
		return 0;
	if (*dx < lo_x)
		*dx = lo_x;
	if (*dx > hi_x)
		*dx = hi_x;
	if (*dy < lo_y)
		*dy = lo_y;
	if (*dy > hi_y)
		*dy = hi_y;
	return n2avc_me_valid(s, mbx, mby, *dx, *dy);
}

static int32_t
mvd_bits_for(int mvx, int mvy, int pmvx, int pmvy)
{
	return n2avc_se_bits(mvx - pmvx) + n2avc_se_bits(mvy - pmvy);
}

/* SAD and rate-distortion cost of one candidate. */
static void
eval_candidate(const n2avc_slice *s, int mbx, int mby, int dx, int dy,
               int pmvx, int pmvy, int lambda, int *sad_out, int *cost_out,
               int32_t *bits_out)
{
	const uint8_t *cur = cur_block(s, mbx, mby);
	const uint8_t *ref = ref_block(s, mbx, mby, dx, dy);
	int sad = sad16(cur, s->src->y.stride, ref, s->ref->y.stride);
	int32_t bits = mvd_bits_for(dx * 4, dy * 4, pmvx, pmvy);

	*sad_out = sad;
	*cost_out = sad + lambda * (int)bits;
	*bits_out = bits;
}

/* Is a non-reference n2vk backend installed (or was N2AVC_ME_FULL forced)? */
static int
full_mode(void)
{
#ifdef N2AVC_ME_FULL
	return 1;
#else
	const char *name;

	if (n2vk_backend_name == NULL)
		return 0;
	name = n2vk_backend_name();
	return name != NULL && strcmp(name, "reference") != 0;
#endif
}

/* Fill p->full_* with n2vk_me16's answer for this slice, if available.  The
 * kernel gets pad = N2AVC_PAD - 8 so that its candidate rectangle is exactly
 * the one n2avc_me_valid() accepts.  Any failure leaves have_full = 0. */
void
n2avc_me_full_setup(n2avc_slice *s, n2avc_inter_priv *p)
{
	n2vk_me16_args a;
	size_t n;
	int rc;

	p->have_full = 0;
	p->first_row = s->first_row;
	p->rows = s->end_row - s->first_row;
	p->mb_w = s->mb_w;
	if (!full_mode() || p->rows <= 0 || s->src == NULL || s->ref == NULL)
		return;
	n = (size_t)p->mb_w * (size_t)p->rows;
	p->full_mvx = (int16_t *)calloc(n, sizeof(*p->full_mvx));
	p->full_mvy = (int16_t *)calloc(n, sizeof(*p->full_mvy));
	p->full_sad = (uint32_t *)calloc(n, sizeof(*p->full_sad));
	if (p->full_mvx == NULL || p->full_mvy == NULL || p->full_sad == NULL)
		goto fail;

	memset(&a, 0, sizeof(a));
	a.size = (uint32_t)sizeof(a);
	a.cur = s->src->y.p + (size_t)(s->first_row * 16) * (size_t)s->src->y.stride;
	a.cur_stride = (uint32_t)s->src->y.stride;
	a.ref = s->ref->y.p + (size_t)(s->first_row * 16) * (size_t)s->ref->y.stride;
	a.ref_stride = (uint32_t)s->ref->y.stride;
	a.mb_w = (uint32_t)p->mb_w;
	a.mb_h = (uint32_t)p->rows;
	a.pad = N2AVC_PAD - 8;
	a.range = (uint32_t)(s->search_range < 0 ? 0 : (s->search_range > 32 ? 32 : s->search_range));
	a.centre_x = NULL;
	a.centre_y = NULL;
	if (n2vk_me16 == NULL)
		goto fail;
	rc = n2vk_me16(&a, p->full_mvx, p->full_mvy, p->full_sad);
	if (rc != N2VK_OK)
		goto fail;
	p->have_full = 1;
	return;

fail:
	n2avc_me_full_free(p);
}

void
n2avc_me_full_free(n2avc_inter_priv *p)
{
	free(p->full_mvx);
	free(p->full_mvy);
	free(p->full_sad);
	p->full_mvx = NULL;
	p->full_mvy = NULL;
	p->full_sad = NULL;
	p->have_full = 0;
}

int
n2avc_me_full_result(const n2avc_inter_priv *p, int mbx, int mby,
                     int *dx, int *dy, int *sad)
{
	int row = mby - p->first_row;
	size_t i;

	if (!p->have_full || row < 0 || row >= p->rows || mbx < 0 || mbx >= p->mb_w)
		return 0;
	i = (size_t)row * (size_t)p->mb_w + (size_t)mbx;
	*dx = p->full_mvx[i];
	*dy = p->full_mvy[i];
	*sad = (int)p->full_sad[i];
	return 1;
}

/* One neighbour's vector as a whole-sample start candidate; unavailable,
 * intra and the picture border give (0,0). */
static void
neighbour_candidate(const n2avc_slice *s, int x, int y, int *dx, int *dy)
{
	*dx = 0;
	*dy = 0;
	if (!n2avc_avail(s, x, y))
		return;
	{
		const n2avc_mbinfo *m = &s->mb[y * s->mb_w + x];
		if (m->ref >= 0) {
			*dx = qpel_to_int(m->mvx);
			*dy = qpel_to_int(m->mvy);
		}
	}
}

/* A candidate is better when its cost is lower; ties go to the lower SAD,
 * then to the smaller absolute mvd, then to the one nearer the top-left (a
 * deterministic total order, so the encoder is reproducible). */
static int
better(int cost, int sad, int32_t bits, int dx, int dy,
       int bcost, int bsad, int32_t bbits, int bdx, int bdy)
{
	if (cost != bcost)
		return cost < bcost;
	if (sad != bsad)
		return sad < bsad;
	if (bits != bbits)
		return bits < bbits;
	if (dy != bdy)
		return dy < bdy;
	return dx < bdx;
}

int
n2avc_me_search(const n2avc_slice *s, int mbx, int mby, int pmvx, int pmvy,
                n2avc_me_result *out)
{
	const int lambda = n2avc_me_lambda(s->qp);
	int start_x[6], start_y[6];
	int nstart = 0;
	int i;
	int best_dx = 0, best_dy = 0;
	int best_sad = 0, best_cost = 0;
	int32_t best_bits = 0;
	int step;

	/* Start candidates, in the order of the task text.  Duplicates are
	 * harmless (the first one wins the tie). */
	start_x[nstart] = 0;
	start_y[nstart] = 0;
	nstart++;
	start_x[nstart] = qpel_to_int(pmvx);
	start_y[nstart] = qpel_to_int(pmvy);
	nstart++;
	neighbour_candidate(s, mbx - 1, mby, &start_x[nstart], &start_y[nstart]);
	nstart++;
	neighbour_candidate(s, mbx, mby - 1, &start_x[nstart], &start_y[nstart]);
	nstart++;
	neighbour_candidate(s, mbx + 1, mby - 1, &start_x[nstart], &start_y[nstart]);
	nstart++;
	{
		int fx, fy, fs;

		if (s->inter_priv != NULL &&
		    n2avc_me_full_result((const n2avc_inter_priv *)s->inter_priv,
		                         mbx, mby, &fx, &fy, &fs)) {
			start_x[nstart] = fx;
			start_y[nstart] = fy;
			nstart++;
		}
	}

	best_cost = 0;
	for (i = 0; i < nstart; i++) {
		int dx = start_x[i];
		int dy = start_y[i];
		int sad, cost;
		int32_t bits;

		if (!clamp_candidate(s, mbx, mby, &dx, &dy))
			continue;
		eval_candidate(s, mbx, mby, dx, dy, pmvx, pmvy, lambda,
		               &sad, &cost, &bits);
		if (i == 0 || better(cost, sad, bits, dx, dy,
		                     best_cost, best_sad, best_bits, best_dx, best_dy)) {
			best_dx = dx;
			best_dy = dy;
			best_sad = sad;
			best_cost = cost;
			best_bits = bits;
		}
	}
	/* (0,0) is always legal for a macroblock inside the picture, so the
	 * loop above always set the best values (i == 0 is that candidate). */

	for (step = 0; step < N2AVC_ME_MAX_STEPS; step++) {
		static const int off[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
		int next_dx = best_dx, next_dy = best_dy;
		int next_sad = best_sad, next_cost = best_cost;
		int32_t next_bits = best_bits;
		int improved = 0;
		int k;

		for (k = 0; k < 4; k++) {
			int dx = best_dx + off[k][0];
			int dy = best_dy + off[k][1];
			int sad, cost;
			int32_t bits;

			if (!clamp_candidate(s, mbx, mby, &dx, &dy))
				continue;
			if (dx == best_dx && dy == best_dy)
				continue;
			eval_candidate(s, mbx, mby, dx, dy, pmvx, pmvy, lambda,
			               &sad, &cost, &bits);
			if (better(cost, sad, bits, dx, dy,
			           next_cost, next_sad, next_bits, next_dx, next_dy)) {
				next_dx = dx;
				next_dy = dy;
				next_sad = sad;
				next_cost = cost;
				next_bits = bits;
				improved = 1;
			}
		}
		if (!improved)
			break;
		best_dx = next_dx;
		best_dy = next_dy;
		best_sad = next_sad;
		best_cost = next_cost;
		best_bits = next_bits;
	}

	out->mvx = best_dx * 4;
	out->mvy = best_dy * 4;
	out->pmvx = pmvx;
	out->pmvy = pmvy;
	out->sad = best_sad;
	out->cost = best_cost;
	out->mvd_bits = best_bits;
	return 0;
}
