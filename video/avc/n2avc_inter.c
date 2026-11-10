/*
 * n2avc_inter.c -- P-slice inter prediction and the per-macroblock decision.
 *
 * Written from ITU-T H.264:
 *   clause 8.4.1.1  P_Skip motion vector derivation
 *   clause 8.4.1.3  16x16 luma motion-vector prediction (median, the
 *                   "B and C unavailable" special case)
 *   clause 8.4.2.2.2 chroma motion compensation (bilinear, eighth chroma
 *                   samples, (8-xF)(8-yF)A + ... + 32) >> 6)
 *   clause 7.3.4 / 7.3.5  P_L0_16x16 syntax
 * No code from x264, FFmpeg, OpenH264 or JM was used or consulted.
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2avc_inter_priv.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* A P_Skip is only attempted when the skip prediction's luma SAD is below
 * this limit.  The limit grows with QP because a coarser quantiser can absorb
 * more residual energy.  It is a speed/quality heuristic, not a correctness
 * condition: the hard condition is that n2avc_inter_residual() really returns
 * cbp 0 for the skip prediction. */
static int
skip_sad_limit(int qp)
{
	if (qp < 0)
		qp = 0;
	if (qp > 51)
		qp = 51;
	return 24 + 2 * qp;
}

/* ------------------------------------------------------------------------- */
/* Neighbour access                                                          */
/* ------------------------------------------------------------------------- */

/* A neighbour counts as inter only when it is inside this slice and has
 * refIdx 0; everything else (outside the picture, another slice, intra) is
 * treated as refIdx -1 with vector (0,0), clause 8.4.1.3. */
static int
neighbour_inter(const n2avc_slice *s, int mbx, int mby, int *mvx, int *mvy)
{
	const n2avc_mbinfo *m;

	*mvx = 0;
	*mvy = 0;
	if (s->mb == NULL || !n2avc_avail(s, mbx, mby))
		return 0;
	m = &s->mb[mby * s->mb_w + mbx];
	if (m->ref < 0)
		return 0;
	*mvx = m->mvx;
	*mvy = m->mvy;
	return 1;
}

static int
median3(int a, int b, int c)
{
	if (a > b) {
		int t = a;
		a = b;
		b = t;
	}
	if (b > c)
		b = c;
	return a > b ? a : b;
}

void
n2avc_inter_pmv(const n2avc_slice *s, int mbx, int mby, int *mvx, int *mvy)
{
	int ax, ay, bx, by, cx, cy;
	int a_ok, b_ok, c_ok;

	a_ok = neighbour_inter(s, mbx - 1, mby, &ax, &ay);
	b_ok = neighbour_inter(s, mbx, mby - 1, &bx, &by);

	/* Clause 8.4.1.3: the above-right neighbour is C; when C is not
	 * available the above-left neighbour D takes its place.  (An intra C
	 * stays C and contributes refIdx -1 / mv (0,0).) */
	c_ok = neighbour_inter(s, mbx + 1, mby - 1, &cx, &cy);
	if (!n2avc_avail(s, mbx + 1, mby - 1))
		c_ok = neighbour_inter(s, mbx - 1, mby - 1, &cx, &cy);

	/* "If B and C are not available or are coded in intra prediction mode
	 * and A is available and coded in inter prediction mode, mvL0N = mvL0A." */
	if (!b_ok && !c_ok) {
		if (a_ok) {
			*mvx = ax;
			*mvy = ay;
		} else {
			*mvx = 0;
			*mvy = 0;
		}
		return;
	}

	/* "Otherwise, if exactly one of mvL0A, mvL0B and mvL0C refers to a
	 * reference picture with refIdx equal to 0, use that vector." */
	if ((a_ok ? 1 : 0) + (b_ok ? 1 : 0) + (c_ok ? 1 : 0) == 1) {
		*mvx = a_ok ? ax : b_ok ? bx : cx;
		*mvy = a_ok ? ay : b_ok ? by : cy;
		return;
	}

	/* Otherwise the component-wise median, with unavailable/intra
	 * neighbours contributing (0,0). */
	*mvx = median3(a_ok ? ax : 0, b_ok ? bx : 0, c_ok ? cx : 0);
	*mvy = median3(a_ok ? ay : 0, b_ok ? by : 0, c_ok ? cy : 0);
}

void
n2avc_inter_skip_mv(const n2avc_slice *s, int mbx, int mby, int *mvx, int *mvy)
{
	int ax = 0, ay = 0, bx = 0, by = 0;
	int a_ok, b_ok;

	a_ok = neighbour_inter(s, mbx - 1, mby, &ax, &ay);
	b_ok = neighbour_inter(s, mbx, mby - 1, &bx, &by);

	/* Clause 8.4.1.1: an A or B that is not available, or an A/B that is
	 * inter with refIdx 0 and vector (0,0), makes the skip vector (0,0).
	 * An intra neighbour is available with refIdx -1 and does NOT force a
	 * zero vector: it enters 8.4.1.3 as the "coded in intra prediction
	 * mode" candidate with mv (0,0), exactly like an unavailable one, but
	 * only the 8.4.1.3 special case and the median see it.  (Lead and R2
	 * verified this against a reference decoder; the previous "intra
	 * forces zero" reading was wrong.) */
	if (!n2avc_avail(s, mbx - 1, mby) || !n2avc_avail(s, mbx, mby - 1) ||
	    (a_ok && ax == 0 && ay == 0) || (b_ok && bx == 0 && by == 0)) {
		*mvx = 0;
		*mvy = 0;
		return;
	}
	n2avc_inter_pmv(s, mbx, mby, mvx, mvy);
}

/* ------------------------------------------------------------------------- */
/* Prediction                                                                */
/* ------------------------------------------------------------------------- */

/* Clause 8.4.2.2.1.  The half-sample positions use the 6-tap filter
 * {1, -5, 20, 20, -5, 1} / 32 (shift 5).  A quarter-sample position along one
 * axis is the rounded average of the integer and the adjacent half sample,
 * (a + b + 1) >> 1.  A position that is fractional in both axes is built from
 * the four half-grid samples of its cell with the same averages; when both
 * axes sit at a half sample the horizontal filter runs first at full
 * precision and the vertical one afterwards with a single rounding of
 * (sum + 512) >> 10.
 *
 * This model was verified sample for sample against a reference decoder
 * (ffmpeg, used purely as a black box) on streams whose motion vectors were
 * forced to known fractional positions: the 1-D half and quarter cases, the
 * half/half diagonal, half+quarter in either order, 3/4, and negative
 * vectors all match bit exactly.  The one combination it does not reproduce
 * is quarter/quarter (both axes at 1/4 or 3/4); n2avc_me_valid_q() therefore
 * never accepts such a vector, so the encoder cannot emit one. */
static const int16_t luma_half_tap[6] = { 1, -5, 20, 20, -5, 1 };

static int
clip8(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* Columns and rows -2..18 relative to the block: enough for the 6-tap of
 * every pixel of a 16x16 block (which reads up to +18 at the bottom-right
 * corner) and for the one-half-sample border of the quarter averages. */
#define LUMA_GRID 21

static int
luma_halfgrid(const uint8_t *src, int stride, const int32_t (*rawh)[LUMA_GRID],
              int x, int y, int gx, int gy)
{
	int k, sum;

	if (gy == 0) {
		if (gx == 0)
			return src[y * stride + x];
		if (gx == 2)
			return src[y * stride + x + 1];
		return clip8((int)((rawh[y + 2][x] + 16) >> 5));
	}
	if (gy == 2) {
		if (gx == 0)
			return src[(y + 1) * stride + x];
		if (gx == 2)
			return src[(y + 1) * stride + x + 1];
		return clip8((int)((rawh[y + 3][x] + 16) >> 5));
	}
	/* gy == 1: the half-sample row y + 1/2 */
	if (gx == 0) {
		sum = 0;
		for (k = 0; k < 6; k++)
			sum += luma_half_tap[k] * src[(y + k - 2) * stride + x];
		return clip8((sum + 16) >> 5);
	}
	if (gx == 2) {
		sum = 0;
		for (k = 0; k < 6; k++)
			sum += luma_half_tap[k] * src[(y + k - 2) * stride + x + 1];
		return clip8((sum + 16) >> 5);
	}
	sum = 0;
	for (k = 0; k < 6; k++)
		sum += luma_half_tap[k] * (int)rawh[y + k][x];
	return clip8((sum + 512) >> 10);
}

void
n2avc_inter_luma(const n2avc_slice *s, int mbx, int mby, int mvx, int mvy,
                 uint8_t *dst, int dst_stride)
{
	const int stride = s->ref->y.stride;
	const uint8_t *src = s->ref->y.p + (mby * 16 + (mvy >> 2)) * stride
	                   + mbx * 16 + (mvx >> 2);
	const int xf = mvx & 3;
	const int yf = mvy & 3;
	/* The value lies between the half-grid anchors g = p0 and g = p0 + 1
	 * (each step is half a sample), at weight w: 0 = p0, 2 = p1,
	 * 1 = half-way.  gx/gy count half samples from the integer position. */
	const int x3 = xf == 3;
	const int y3 = yf == 3;
	const int p0x = x3 ? 1 : 0;
	const int p0y = y3 ? 1 : 0;
	const int wx = xf == 0 ? 0 : xf == 2 ? 2 : 1;
	const int wy = yf == 0 ? 0 : yf == 2 ? 2 : 1;
	int32_t rawh[LUMA_GRID][LUMA_GRID];
	uint8_t h[2][2];        /* [j][i] at (p0x + i, p0y + j), half samples */
	int i0 = wx == 2 ? 1 : 0;
	int i1 = wx == 0 ? 0 : 1;
	int j0 = wy == 2 ? 1 : 0;
	int j1 = wy == 0 ? 0 : 1;
	int x, y, i, j, k;

	if (xf == 0 && yf == 0) {
		for (y = 0; y < 16; y++)
			memcpy(dst + y * dst_stride, src + y * stride, 16);
		return;
	}

	if (xf != 0) {
		for (j = 0; j < LUMA_GRID; j++) {
			const uint8_t *r = src + (j - 2) * stride;

			for (i = 0; i < LUMA_GRID; i++) {
				int32_t sum = 0;

				for (k = 0; k < 6; k++)
					sum += luma_half_tap[k] * r[i + k - 2];
				rawh[j][i] = sum;
			}
		}
	}

	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++) {
			int r[2];
			int v;

			for (j = 0; j <= j1; j++) {
				if (j < j0)
					continue;
				for (i = 0; i <= i1; i++) {
					if (i < i0)
						continue;
					h[j][i] = (uint8_t)luma_halfgrid(src, stride, rawh, x, y,
					                                 p0x + i, p0y + j);
				}
			}
			for (j = 0; j <= j1; j++) {
				if (j < j0) {
					r[j] = r[0];
					continue;
				}
				if (wx == 0)
					r[j] = h[j][0];
				else if (wx == 2)
					r[j] = h[j][1];
				else
					r[j] = (h[j][0] + h[j][1] + 1) >> 1;
			}
			if (wy == 0)
				v = r[0];
			else if (wy == 2)
				v = r[1];
			else
				v = (r[0] + r[1] + 1) >> 1;
			dst[y * dst_stride + x] = (uint8_t)v;
		}
	}
}

void
n2avc_inter_predict(const n2avc_slice *s, int mbx, int mby, int mvx, int mvy,
                    uint8_t *py, uint8_t *pcb, uint8_t *pcr)
{
	const int cx = mbx * 8 + (mvx >> 3);
	const int cy = mby * 8 + (mvy >> 3);
	const int xf = mvx & 7;
	const int yf = mvy & 7;
	int y, x;
	int plane;

	n2avc_inter_luma(s, mbx, mby, mvx, mvy, py, 16);

	for (plane = 0; plane < 2; plane++) {
		const n2avc_plane *r = plane == 0 ? &s->ref->cb : &s->ref->cr;
		uint8_t *dst = plane == 0 ? pcb : pcr;

		for (y = 0; y < 8; y++) {
			const uint8_t *row0 = r->p + (cy + y) * r->stride + cx;
			const uint8_t *row1 = row0 + r->stride;

			for (x = 0; x < 8; x++) {
				int a = row0[x], b = row0[x + 1];
				int c = row1[x], d = row1[x + 1];
				int v = (8 - xf) * (8 - yf) * a
				      + xf * (8 - yf) * b
				      + (8 - xf) * yf * c
				      + xf * yf * d;
				dst[y * 8 + x] = (uint8_t)((v + 32) >> 6);
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Small helpers on the current macroblock                                   */
/* ------------------------------------------------------------------------- */

static const uint8_t *
cur_y(const n2avc_slice *s, int mbx, int mby)
{
	return s->src->y.p + (mby * 16) * s->src->y.stride + mbx * 16;
}

static int
sad_packed16(const n2avc_slice *s, int mbx, int mby, const uint8_t *pred)
{
	const uint8_t *cur = cur_y(s, mbx, mby);
	int sum = 0;
	int y, x;

	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++) {
			int d = (int)cur[y * s->src->y.stride + x] - (int)pred[y * 16 + x];
			sum += d < 0 ? -d : d;
		}
	}
	return sum;
}

/* SAD of the flat DC prediction of clause 8.3.3.1 (Intra16x16 DC), used only
 * as the "cost_intra" estimate of the decision. */
static int
sad_dc16(const n2avc_slice *s, int mbx, int mby)
{
	const uint8_t *cur = cur_y(s, mbx, mby);
	int sum = 0;
	int dc;
	int sad = 0;
	int y, x;

	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
			sum += cur[y * s->src->y.stride + x];
	dc = (sum + 128) >> 8;
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++) {
			int d = (int)cur[y * s->src->y.stride + x] - dc;
			sad += d < 0 ? -d : d;
		}
	}
	return sad;
}

static void
save_rec(const n2avc_slice *s, n2avc_inter_priv *p, int mbx, int mby)
{
	int y;

	for (y = 0; y < 16; y++)
		memcpy(p->save_y + y * 16,
		       s->rec->y.p + (mby * 16 + y) * s->rec->y.stride + mbx * 16, 16);
	for (y = 0; y < 8; y++) {
		memcpy(p->save_cb + y * 8,
		       s->rec->cb.p + (mby * 8 + y) * s->rec->cb.stride + mbx * 8, 8);
		memcpy(p->save_cr + y * 8,
		       s->rec->cr.p + (mby * 8 + y) * s->rec->cr.stride + mbx * 8, 8);
	}
}

static void
restore_rec(const n2avc_slice *s, const n2avc_inter_priv *p, int mbx, int mby)
{
	int y;

	for (y = 0; y < 16; y++)
		memcpy(s->rec->y.p + (mby * 16 + y) * s->rec->y.stride + mbx * 16,
		       p->save_y + y * 16, 16);
	for (y = 0; y < 8; y++) {
		memcpy(s->rec->cb.p + (mby * 8 + y) * s->rec->cb.stride + mbx * 8,
		       p->save_cb + y * 8, 8);
		memcpy(s->rec->cr.p + (mby * 8 + y) * s->rec->cr.stride + mbx * 8,
		       p->save_cr + y * 8, 8);
	}
}

static void
copy_pred_to_rec(const n2avc_slice *s, int mbx, int mby,
                 const uint8_t *py, const uint8_t *pcb, const uint8_t *pcr)
{
	int y;

	for (y = 0; y < 16; y++)
		memcpy(s->rec->y.p + (mby * 16 + y) * s->rec->y.stride + mbx * 16,
		       py + y * 16, 16);
	for (y = 0; y < 8; y++) {
		memcpy(s->rec->cb.p + (mby * 8 + y) * s->rec->cb.stride + mbx * 8,
		       pcb + y * 8, 8);
		memcpy(s->rec->cr.p + (mby * 8 + y) * s->rec->cr.stride + mbx * 8,
		       pcr + y * 8, 8);
	}
}

static void
fill_mbinfo(n2avc_slice *s, int mbx, int mby, int mvx, int mvy, int type, int cbp)
{
	n2avc_mbinfo *m = &s->mb[mby * s->mb_w + mbx];

	m->mvx = (int16_t)mvx;
	m->mvy = (int16_t)mvy;
	m->ref = (type == N2MB_PSKIP || type == N2MB_P16) ? 0 : -1;
	m->type = (uint8_t)type;
	m->qp = (uint8_t)s->qp;
	m->cbp = (uint8_t)cbp;
	if (type == N2MB_PSKIP)
		memset(m->nnz, 0, sizeof(m->nnz));
}

/* ------------------------------------------------------------------------- */
/* Slice lifetime                                                            */
/* ------------------------------------------------------------------------- */

/* A fixed pool instead of malloc: a steady-state encoder allocates nothing in
 * the per-picture path (review checklist).  Up to 16 slice threads per encoder
 * (n2avc.h rejects 17) and more than one encoder per process (the TSan test
 * runs two), hence 32 slots.  A slot is owned by exactly one slice between
 * begin() and end(); the atomic hand-over is the only synchronisation needed
 * and it gives the acquire/release edge TSan wants when a slot is reused. */
#define N2AVC_INTER_SLOTS 32

static n2avc_inter_priv g_pool[N2AVC_INTER_SLOTS];
static atomic_int g_pool_owner[N2AVC_INTER_SLOTS];

void *
n2avc_inter_slice_begin(n2avc_slice *s)
{
	int i;

	s->inter_priv = NULL;
	for (i = 0; i < N2AVC_INTER_SLOTS; i++) {
		int free_slot = 0;

		if (atomic_compare_exchange_strong_explicit(&g_pool_owner[i], &free_slot, 1,
		                                            memory_order_acquire, memory_order_relaxed)) {
			n2avc_inter_priv *p = &g_pool[i];

			p->slot = i;
			p->have_full = 0;
			p->first_row = 0;
			p->rows = 0;
			p->mb_w = 0;
			s->inter_priv = p;
			n2avc_me_full_setup(s, p);  /* best effort; diamond search alone is valid */
			return p;
		}
	}
	return NULL;    /* all slots busy: the caller codes the slice intra */
}

void
n2avc_inter_slice_end(n2avc_slice *s)
{
	n2avc_inter_priv *p = (n2avc_inter_priv *)s->inter_priv;

	if (p == NULL)
		return;
	p->have_full = 0;               /* the n2vk result arrays stay for the next owner */
	if (p->slot >= 0 && p->slot < N2AVC_INTER_SLOTS)
		atomic_store_explicit(&g_pool_owner[p->slot], 0, memory_order_release);
	s->inter_priv = NULL;
}

/* ------------------------------------------------------------------------- */
/* One P macroblock                                                          */
/* ------------------------------------------------------------------------- */

int
n2avc_p_mb(n2avc_slice *s, int mbx, int mby)
{
	n2avc_inter_priv *p = (n2avc_inter_priv *)s->inter_priv;
	n2avc_mbinfo saved_mb;
	const int lambda = n2avc_me_lambda(s->qp);
	int pmvx, pmvy;
	int skipx, skipy;
	int skip_valid;
	int sad_skip;
	int cost_skip;
	int skip_ok = 0;
	n2avc_me_result me;
	int cost_inter;
	int cost_intra;
	int best_cost;
	int cbp;

	if (p == NULL)
		return 2;       /* no scratch: let WP03 code the macroblock intra */

	n2avc_inter_pmv(s, mbx, mby, &pmvx, &pmvy);
	n2avc_inter_skip_mv(s, mbx, mby, &skipx, &skipy);

	/* The inferred skip vector must be legal for this block; otherwise a
	 * P_Skip would read outside the reference or decode differently.  It is
	 * in quarter samples and may be fractional (a neighbour's vector is). */
	skip_valid = n2avc_me_valid_q(s, mbx, mby, skipx, skipy);

	if (skip_valid) {
		n2avc_inter_predict(s, mbx, mby, skipx, skipy,
		                    p->pred_y, p->pred_cb, p->pred_cr);
		sad_skip = sad_packed16(s, mbx, mby, p->pred_y);
		cost_skip = sad_skip + lambda;  /* one bit for mb_skip_run */
	} else {
		sad_skip = 1 << 28;
		cost_skip = 1 << 28;
	}

	if (skip_valid && sad_skip < skip_sad_limit(s->qp)) {
		int cbp_skip;

		/* "verify by actually calling it on a scratch copy": save the
		 * reconstruction and the macroblock info, run the real residual
		 * path, put both back.  If we later choose P_Skip the prediction
		 * is written again below. */
		saved_mb = s->mb[mby * s->mb_w + mbx];
		save_rec(s, p, mbx, mby);
		cbp_skip = n2avc_inter_residual(s, mbx, mby,
		                                p->pred_y, p->pred_cb, p->pred_cr);
		restore_rec(s, p, mbx, mby);
		s->mb[mby * s->mb_w + mbx] = saved_mb;
		skip_ok = (cbp_skip == 0);
	}

	n2avc_me_search(s, mbx, mby, pmvx, pmvy, &me);
	cost_inter = me.cost + lambda;          /* + mb_type ue(0) */

	/* cost_intra: SAD of the DC prediction plus an estimate of the intra
	 * syntax (mb_type, prediction mode, cbp).  It only decides whether it
	 * is worth letting WP03 try intra; WP03 makes the real choice. */
	cost_intra = sad_dc16(s, mbx, mby) + lambda * 8;

	best_cost = cost_inter;
	if (skip_ok && cost_skip < best_cost)
		best_cost = cost_skip;

	/* Intra must be cheaper by more than 12 %.  "Nothing changed" is kept:
	 * rec and mbinfo were restored after the skip probe and the inter
	 * residual has not run yet. */
	if ((int64_t)cost_intra * 100 < (int64_t)best_cost * 88)
		return 2;

	if (skip_ok && cost_skip <= best_cost) {
		/* P_Skip: nothing written, skip_run incremented, prediction in
		 * rec, mbinfo filled. */
		n2avc_inter_predict(s, mbx, mby, skipx, skipy,
		                    p->pred_y, p->pred_cb, p->pred_cr);
		copy_pred_to_rec(s, mbx, mby, p->pred_y, p->pred_cb, p->pred_cr);
		fill_mbinfo(s, mbx, mby, skipx, skipy, N2MB_PSKIP, 0);
		s->skip_run++;
		return 1;
	}

	/* P_L0_16x16. */
	n2avc_inter_predict(s, mbx, mby, me.mvx, me.mvy,
	                    p->pred_y, p->pred_cb, p->pred_cr);
	cbp = n2avc_inter_residual(s, mbx, mby, p->pred_y, p->pred_cb, p->pred_cr);

	/* A P_L0_16x16 with cbp 0 and the skip vector must be coded as P_Skip
	 * (clause 7.3.4: otherwise the syntax would be ambiguous with a skip
	 * run).  The prediction is already in rec. */
	if (cbp == 0 && me.mvx == skipx && me.mvy == skipy) {
		fill_mbinfo(s, mbx, mby, skipx, skipy, N2MB_PSKIP, 0);
		s->skip_run++;
		return 1;
	}

	n2avc_flush_skip_run(s);
	n2bw_ue(&s->bw, 0);                                     /* mb_type P_L0_16x16 */
	n2bw_se(&s->bw, me.mvx - pmvx);                         /* mvd_l0[0][0] */
	n2bw_se(&s->bw, me.mvy - pmvy);                         /* mvd_l0[0][1] */
	n2bw_ue(&s->bw, n2avc_cbp_inter_code(cbp));             /* coded_block_pattern */
	if (cbp != 0)
		n2bw_se(&s->bw, 0);                             /* mb_qp_delta */
	n2avc_write_inter_residual(s, mbx, mby, cbp);

	fill_mbinfo(s, mbx, mby, me.mvx, me.mvy, N2MB_P16, cbp);
	return 0;
}
