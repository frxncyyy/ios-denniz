/* SPDX-License-Identifier: MIT */
/*
 * n2avc_deblock.c -- the H.264 in-loop deblocking filter of clause 8.7, written from ITU-T H.264 (11/2007).
 *
 * This is the picture-level filter that runs after the whole reconstruction of a picture has been built and before that
 * picture is used as a reference (and before n2avc_reconstruction() returns it).  It implements:
 *
 *   8.7.2.1  bS derivation for frame pictures (no MBAFF, no field pictures, one reference):
 *              bS = 4  at a macroblock edge when either side is intra (I16x16/I_PCM)
 *              bS = 3  at any other edge when either side is intra
 *              bS = 2  when either luma 4x4 block next to the edge has non-zero coefficients
 *              bS = 1  when the two sides use different reference indices or a motion-vector component differs by >= 4
 *                      quarter luma samples
 *              bS = 0  otherwise
 *            Chroma edges inherit the bS of the luma edge that contains the luma sample at (2*x, 2*y), clause 8.7.2.
 *   8.7.2.2  alpha/beta thresholds from Table 8-16, filterSamplesFlag
 *   8.7.2.3  the filters for bS < 4 (including the optional p1/q1 update, luma only)
 *   8.7.2.4  the filters for bS = 4 (the strong filter, with the p-side and q-side conditions tested separately)
 *
 * The filtering order is the normative one of clause 8.7: macroblocks in raster scan order; for each macroblock, each
 * component in the order luma, Cb, Cr; for each component the vertical edges left to right (left macroblock edge first,
 * then x = 4, 8, 12) and then the horizontal edges top to bottom (top macroblock edge first, then y = 4, 8, 12).  For
 * 4:2:0 the chroma edges are the left/top macroblock edges and x = 4 / y = 4.
 *
 * Slice boundaries: with disable_deblocking_filter_idc == 0 (the value n2avc.h promises when N2AVC_F_DEBLOCK is set)
 * clause 8.7 filters every edge inside the picture, including edges between two slices.  Only idc 2 would make a slice
 * boundary special, and this encoder never writes it.  Therefore no slice map is needed here.
 *
 * Preconditions, all guaranteed by the encoder core for the streams this file is used with:
 *   - every slice has disable_deblocking_filter_idc == 0 and slice_alpha_c0_offset_div2 == slice_beta_offset_div2 == 0;
 *   - macroblocks are I16, I_PCM, P_L0_16x16 or P_Skip (one 16x16 partition, one reference, 4x4 transform);
 *   - mbinfo.mvx/mvy are quarter luma samples; nnz[] holds total_coeff of every luma 4x4 block in raster order;
 *   - the picture is 4:2:0 8 bit.
 *
 * No allocation, no libc call, no global state.  The filter reads only samples inside the picture: edges at the picture
 * boundary are not filtered and every other edge has at least three samples on each side.
 *
 * The right shifts in the equations are arithmetic shifts (floor), as the Recommendation's integer arithmetic assumes;
 * clang on x86-64 and arm64 implements >> on a negative int that way.
 */
#include "n2avc_int.h"

/* Table 8-16, alpha' as a function of indexA. */
static const uint8_t ALPHA[52] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	4, 4, 5, 6, 7, 8, 9, 10, 12, 13,
	15, 17, 20, 22, 25, 28, 32, 36, 40, 45, 50, 56, 63, 71, 80, 90, 101, 113, 127, 144, 162, 182, 203, 226, 255, 255
};

/* Table 8-16, beta' as a function of indexB.  Note that the table has no value 5: index 25 is 4 and index 26 is 6. */
static const uint8_t BETA[52] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
	6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18
};

/* Table 8-17, t'C0 as a function of indexA and bS (bS = 1..3; bS = 4 does not use it).  [bS - 1][indexA]. */
static const uint8_t TC0[3][52] = {
	{	/* bS = 1 */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 8, 9, 10, 11, 13
	},
	{	/* bS = 2 */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8, 10, 11, 12, 13, 15, 17
	},
	{	/* bS = 3 */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16, 18, 20, 23, 25
	}
};

/* Table 8-15, QP_C as a function of QP_Y for chroma_qp_index_offset 0. */
static const uint8_t QPC[52] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
	28, 29, 29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38, 39, 39, 39, 39
};

static int iabs(int v)
{
	return v < 0 ? -v : v;
}

/* Clip3(x, y, z) of clause 5.7. */
static int clip3(int lo, int hi, int v)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* Clip1 for 8-bit samples. */
static int clip1(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

static int mb_is_intra(const n2avc_mbinfo *m)
{
	return m->ref < 0 || m->type == N2MB_I16 || m->type == N2MB_IPCM;
}

/* QP_Y used by the deblocking filter: an I_PCM macroblock has qPz = 0 (clause 8.7.2). */
static int mb_qp_luma(const n2avc_mbinfo *m)
{
	return m->type == N2MB_IPCM ? 0 : (int)m->qp;
}

/*
 * bS of one luma 4x4 block edge (clause 8.7.2.1, frame picture, one reference, 16x16 partition).  p and q are the two
 * macroblocks, (xp4, yp4) and (xq4, yq4) the 4x4 luma blocks inside them on the two sides of the edge.  mb_edge is 1 when
 * the edge is also a macroblock edge.
 */
static int luma_bs(const n2avc_mbinfo *p, int xp4, int yp4, const n2avc_mbinfo *q, int xq4, int yq4, int mb_edge)
{
	int pintra = mb_is_intra(p), qintra = mb_is_intra(q);

	if (mb_edge && (pintra || qintra))
		return 4;
	if (pintra || qintra)
		return 3;
	if (p->nnz[yp4 * 4 + xp4] != 0 || q->nnz[yq4 * 4 + xq4] != 0)
		return 2;
	if (p->ref != q->ref)
		return 1;
	if (iabs((int)p->mvx - (int)q->mvx) >= 4 || iabs((int)p->mvy - (int)q->mvy) >= 4)
		return 1;
	return 0;
}

/*
 * bS of the luma edge that contains the luma sample at (edge, pos): when vertical, the vertical edge at luma x = edge and
 * luma row pos; otherwise the horizontal edge at luma y = edge and luma column pos.  edge is a multiple of 4 and strictly
 * inside the picture.  Used both for luma 4x4 block edges and, with (2*x, 2*y) of the chroma q0, for chroma edges.
 */
static int luma_bs_at(const n2avc_mbinfo *mb, int mb_w, int vertical, int edge, int pos)
{
	int bxq, byq, bxp, byp, mb_edge;
	const n2avc_mbinfo *p, *q;

	if (vertical) {
		bxq = edge >> 2;
		bxp = bxq - 1;
		byq = byp = pos >> 2;
	} else {
		byq = edge >> 2;
		byp = byq - 1;
		bxq = bxp = pos >> 2;
	}
	mb_edge = (edge & 15) == 0;
	p = &mb[(byp >> 2) * mb_w + (bxp >> 2)];
	q = &mb[(byq >> 2) * mb_w + (bxq >> 2)];
	return luma_bs(p, bxp & 3, byp & 3, q, bxq & 3, byq & 3, mb_edge);
}

/*
 * The filter of clauses 8.7.2.2, 8.7.2.3 and 8.7.2.4 for one set of eight samples across one edge.  q0 points at the q0
 * sample; step is 1 for a vertical edge (samples along x) and the plane stride for a horizontal edge.  chroma != 0 selects
 * chromaStyleFiltering = 1 (a chroma edge of 4:2:0), which never updates p1/q1 and uses the "else" branch of 8.7.2.4.
 */
static inline void filter_one(uint8_t *q0p, int step, int bS, int alpha, int beta, int index_a, int chroma)
{
	int p3, p2, p1, p0, q0, q1, q2, q3;
	int np0, np1, np2, nq0, nq1, nq2;

	if (bS == 0)
		return;

	p0 = q0p[-step];
	p1 = q0p[-2 * step];
	p2 = q0p[-3 * step];
	p3 = q0p[-4 * step];
	q0 = q0p[0];
	q1 = q0p[step];
	q2 = q0p[2 * step];
	q3 = q0p[3 * step];

	/* filterSamplesFlag, equation 8-470. */
	if (!(iabs(p0 - q0) < alpha && iabs(p1 - p0) < beta && iabs(q1 - q0) < beta))
		return;

	if (bS < 4) {
		int ap = iabs(p2 - p0), aq = iabs(q2 - q0);
		int tc0 = TC0[bS - 1][index_a];
		int tc = tc0 + (chroma ? 1 : (ap < beta ? 1 : 0) + (aq < beta ? 1 : 0));
		int delta = clip3(-tc, tc, (((q0 - p0) * 4) + (p1 - q1) + 4) >> 3);

		np0 = clip1(p0 + delta);                            /* 8-471, 8-472 */
		nq0 = clip1(q0 - delta);                            /* 8-473 */
		np1 = p1;
		nq1 = q1;
		if (!chroma && ap < beta)                           /* 8-480 */
			np1 = clip1(p1 + clip3(-tc0, tc0, (p2 + ((p0 + q0 + 1) >> 1) - (p1 << 1)) >> 1));
		if (!chroma && aq < beta)                           /* 8-482 */
			nq1 = clip1(q1 + clip3(-tc0, tc0, (q2 + ((p0 + q0 + 1) >> 1) - (q1 << 1)) >> 1));
		np2 = p2;                                           /* 8-484 */
		nq2 = q2;                                           /* 8-485 */
	} else {
		int ap = iabs(p2 - p0), aq = iabs(q2 - q0);
		int strong = !chroma && iabs(p0 - q0) < ((alpha >> 2) + 2);

		if (strong && ap < beta) {                          /* 8-486 .. 8-489 */
			np0 = (p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3;
			np1 = (p2 + p1 + p0 + q0 + 2) >> 2;
			np2 = (2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3;
		} else {                                            /* 8-490 .. 8-492 */
			np0 = (2 * p1 + p0 + q1 + 2) >> 2;
			np1 = p1;
			np2 = p2;
		}
		if (strong && aq < beta) {                          /* 8-493 .. 8-496 */
			nq0 = (p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3;
			nq1 = (p0 + q0 + q1 + q2 + 2) >> 2;
			nq2 = (2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3;
		} else {                                            /* 8-497 .. 8-499 */
			nq0 = (2 * q1 + q0 + p1 + 2) >> 2;
			nq1 = q1;
			nq2 = q2;
		}
	}

	q0p[-3 * step] = (uint8_t)clip1(np2);     /* p2 (only changed by the strong filter) */
	q0p[-2 * step] = (uint8_t)clip1(np1);
	q0p[-step] = (uint8_t)clip1(np0);
	q0p[0] = (uint8_t)clip1(nq0);
	q0p[step] = (uint8_t)clip1(nq1);
	q0p[2 * step] = (uint8_t)clip1(nq2);      /* q2 (only changed by the strong filter) */
}

/* indexA/indexB, alpha and beta of one edge (clause 8.7.2.2, filterOffsetA = filterOffsetB = 0). */
static void edge_thresholds(int qp_p, int qp_q, int *alpha, int *beta, int *index_a)
{
	int qpav = (qp_p + qp_q + 1) >> 1;
	int ia = clip3(0, 51, qpav);
	int ib = clip3(0, 51, qpav);

	*index_a = ia;
	*alpha = ALPHA[ia];
	*beta = BETA[ib];
}

/* The four vertical luma edges of macroblock (mbx, mby): dx is 0 (left edge, not at the picture boundary), 4, 8 or 12. */
static void luma_vertical(n2avc_frame *rec, const n2avc_mbinfo *mb, int mb_w, int mbx, int mby, int dx)
{
	const n2avc_mbinfo *q = &mb[mby * mb_w + mbx];
	const n2avc_mbinfo *p = dx == 0 ? &mb[mby * mb_w + mbx - 1] : q;
	int alpha, beta, index_a, k;
	int x = mbx * 16 + dx;
	int xq4 = dx >> 2, xp4 = dx == 0 ? 3 : xq4 - 1;

	edge_thresholds(mb_qp_luma(p), mb_qp_luma(q), &alpha, &beta, &index_a);
	for (k = 0; k < 4; k++) {
		int bS = luma_bs(p, xp4, k, q, xq4, k, dx == 0);
		int j;

		if (bS == 0 || alpha == 0)
			continue;
		for (j = 0; j < 4; j++) {
			uint8_t *q0 = rec->y.p + (size_t)(mby * 16 + k * 4 + j) * rec->y.stride + x;

			filter_one(q0, 1, bS, alpha, beta, index_a, 0);
		}
	}
}

/* The four horizontal luma edges of macroblock (mbx, mby): dy is 0 (top edge, not at the picture boundary), 4, 8 or 12. */
static void luma_horizontal(n2avc_frame *rec, const n2avc_mbinfo *mb, int mb_w, int mbx, int mby, int dy)
{
	const n2avc_mbinfo *q = &mb[mby * mb_w + mbx];
	const n2avc_mbinfo *p = dy == 0 ? &mb[(mby - 1) * mb_w + mbx] : q;
	int alpha, beta, index_a, k;
	int y = mby * 16 + dy;
	int yq4 = dy >> 2, yp4 = dy == 0 ? 3 : yq4 - 1;

	edge_thresholds(mb_qp_luma(p), mb_qp_luma(q), &alpha, &beta, &index_a);
	for (k = 0; k < 4; k++) {
		int bS = luma_bs(p, k, yp4, q, k, yq4, dy == 0);
		int j;

		if (bS == 0 || alpha == 0)
			continue;
		for (j = 0; j < 4; j++) {
			uint8_t *q0 = rec->y.p + (size_t)y * rec->y.stride + (mbx * 16 + k * 4 + j);

			filter_one(q0, rec->y.stride, bS, alpha, beta, index_a, 0);
		}
	}
}

/*
 * A vertical chroma edge of macroblock (mbx, mby): dcx is 0 (left edge, not at the picture boundary) or 4.  Each of the
 * eight chroma sample positions takes the bS of the luma edge through the luma sample at (2*x, 2*y), clause 8.7.2.
 */
static void chroma_vertical(n2avc_plane *pl, const n2avc_mbinfo *mb, int mb_w, int mbx, int mby, int dcx)
{
	const n2avc_mbinfo *q = &mb[mby * mb_w + mbx];
	const n2avc_mbinfo *p = dcx == 0 ? &mb[mby * mb_w + mbx - 1] : q;
	int alpha, beta, index_a, j;
	int x = mbx * 8 + dcx;

	edge_thresholds(QPC[mb_qp_luma(p)], QPC[mb_qp_luma(q)], &alpha, &beta, &index_a);
	for (j = 0; j < 8; j++) {
		int cy = mby * 8 + j;
		int bS = luma_bs_at(mb, mb_w, 1, 2 * x, 2 * cy);
		uint8_t *q0 = pl->p + (size_t)cy * pl->stride + x;

		filter_one(q0, 1, bS, alpha, beta, index_a, 1);
	}
}

/*
 * A horizontal chroma edge of macroblock (mbx, mby): dcy is 0 (top edge, not at the picture boundary) or 4.
 */
static void chroma_horizontal(n2avc_plane *pl, const n2avc_mbinfo *mb, int mb_w, int mbx, int mby, int dcy)
{
	const n2avc_mbinfo *q = &mb[mby * mb_w + mbx];
	const n2avc_mbinfo *p = dcy == 0 ? &mb[(mby - 1) * mb_w + mbx] : q;
	int alpha, beta, index_a, j;
	int y = mby * 8 + dcy;

	edge_thresholds(QPC[mb_qp_luma(p)], QPC[mb_qp_luma(q)], &alpha, &beta, &index_a);
	for (j = 0; j < 8; j++) {
		int cx = mbx * 8 + j;
		int bS = luma_bs_at(mb, mb_w, 0, 2 * y, 2 * cx);
		uint8_t *q0 = pl->p + (size_t)y * pl->stride + cx;

		filter_one(q0, pl->stride, bS, alpha, beta, index_a, 1);
	}
}

/*
 * Deblock (clause 8.7) one reconstructed picture after all its slices have been coded.  See the top of this file for the
 * preconditions.  Raster order of macroblocks; per macroblock luma (vertical then horizontal), Cb, Cr.
 *
 * NOTE: this function is called by avc2/n2avc_core.c (lead-owned).  With a NULL plane or a meaningless size it does
 * nothing, so a future caller cannot make it read or write through a bad pointer.
 */
void n2avc_deblock_picture(n2avc_frame *rec, const n2avc_mbinfo *mb, int mb_w, int mb_h)
{
	int mbx, mby;

	if (!rec || !mb || mb_w <= 0 || mb_h <= 0)
		return;
	if (!rec->y.p || !rec->cb.p || !rec->cr.p)
		return;

	for (mby = 0; mby < mb_h; mby++) {
		for (mbx = 0; mbx < mb_w; mbx++) {
			if (mbx > 0)
				luma_vertical(rec, mb, mb_w, mbx, mby, 0);
			luma_vertical(rec, mb, mb_w, mbx, mby, 4);
			luma_vertical(rec, mb, mb_w, mbx, mby, 8);
			luma_vertical(rec, mb, mb_w, mbx, mby, 12);

			if (mby > 0)
				luma_horizontal(rec, mb, mb_w, mbx, mby, 0);
			luma_horizontal(rec, mb, mb_w, mbx, mby, 4);
			luma_horizontal(rec, mb, mb_w, mbx, mby, 8);
			luma_horizontal(rec, mb, mb_w, mbx, mby, 12);

			if (mbx > 0)
				chroma_vertical(&rec->cb, mb, mb_w, mbx, mby, 0);
			chroma_vertical(&rec->cb, mb, mb_w, mbx, mby, 4);
			if (mby > 0)
				chroma_horizontal(&rec->cb, mb, mb_w, mbx, mby, 0);
			chroma_horizontal(&rec->cb, mb, mb_w, mbx, mby, 4);

			if (mbx > 0)
				chroma_vertical(&rec->cr, mb, mb_w, mbx, mby, 0);
			chroma_vertical(&rec->cr, mb, mb_w, mbx, mby, 4);
			if (mby > 0)
				chroma_horizontal(&rec->cr, mb, mb_w, mbx, mby, 0);
			chroma_horizontal(&rec->cr, mb, mb_w, mbx, mby, 4);
		}
	}
}
