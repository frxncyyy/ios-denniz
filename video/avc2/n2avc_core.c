/* SPDX-License-Identifier: MIT */
/*
 * n2avc_core.c -- the H.264 encoder core (lead, 11 Nov 2026): everything of include/n2avc.h and the "WP03 provides" half of
 * include/n2avc_int.h in one file. Bit writer, NAL units, SPS/PPS/slice headers, 4x4 transform and quantisation, CAVLC,
 * Intra16x16, the residual of inter macroblocks, the slice/picture loop. Inter prediction, motion search and rate control are
 * avc/n2avc_inter.c, n2avc_me.c, n2avc_rc.c (WP04), reached through n2avc_int.h.
 *
 * Written from ITU-T H.264; clause numbers in the comments. The numeric tables are the Recommendation's tables 9-5, 9-7,
 * 9-8, 9-9, 9-10 and 9-4.
 */
#include "n2avc_int.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

void n2avc_deblock_picture(n2avc_frame *rec, const n2avc_mbinfo *mb, int mb_w, int mb_h);      /* avc2/n2avc_deblock.c */

/* ------------------------------------------------------------------ bit writer ------------------------------------------- */

void n2bw_init(n2bw *w, uint8_t *buf, size_t cap)
{
	w->buf = buf; w->cap = cap; w->pos = 0; w->acc = 0; w->nbits = 0; w->overflow = 0;
}

void n2bw_u(n2bw *w, int n, uint32_t v)
{
	if (w->overflow || n <= 0)
		return;
	if (n < 32)
		v &= (1u << n) - 1u;
	w->acc = (w->acc << n) | v;
	w->nbits += n;
	while (w->nbits >= 8) {
		if (w->pos >= w->cap) {
			w->overflow = 1;
			w->nbits = 0;
			return;
		}
		w->buf[w->pos++] = (uint8_t)(w->acc >> (w->nbits - 8));
		w->nbits -= 8;
	}
}

void n2bw_ue(n2bw *w, uint32_t v)
{
	uint64_t x = (uint64_t)v + 1;
	int len = 0;
	while ((x >> len) > 1)
		len++;                          /* len = floor(log2(v + 1)) */
	if (len > 0)
		n2bw_u(w, len, 0);
	if (len == 32) {                        /* v + 1 = 2^32: cannot happen for v <= 2^32 - 2 */
		w->overflow = 1;
		return;
	}
	n2bw_u(w, len + 1, (uint32_t)x);
}

void n2bw_se(n2bw *w, int32_t v)
{
	n2bw_ue(w, v > 0 ? (uint32_t)(2 * (int64_t)v - 1) : (uint32_t)(-2 * (int64_t)v));
}

void n2bw_trailing(n2bw *w)
{
	n2bw_u(w, 1, 1);
	if (!w->overflow && (w->nbits & 7))
		n2bw_u(w, 8 - (w->nbits & 7), 0);
}

void n2bw_align_zero(n2bw *w)
{
	if (!w->overflow && (w->nbits & 7))
		n2bw_u(w, 8 - (w->nbits & 7), 0);
}

size_t n2bw_bits(const n2bw *w) { return w->pos * 8 + (size_t)w->nbits; }
size_t n2bw_flush(n2bw *w) { return w->pos; }

size_t n2avc_nal(uint8_t *dst, size_t cap, const uint8_t *rbsp, size_t n, int nal_ref_idc, int nal_unit_type)
{
	size_t o = 0, i;
	int zeros = 0;
	if (cap < 1)
		return 0;
	dst[o++] = (uint8_t)((nal_ref_idc << 5) | nal_unit_type);
	for (i = 0; i < n; i++) {
		if (zeros >= 2 && rbsp[i] <= 3) {
			if (o >= cap)
				return 0;
			dst[o++] = 3;
			zeros = 0;
		}
		if (o >= cap)
			return 0;
		dst[o++] = rbsp[i];
		zeros = rbsp[i] == 0 ? zeros + 1 : 0;
	}
	return o;
}

/* ------------------------------------------------------------------ tables ----------------------------------------------- */

static const uint8_t ZZ[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 };       /* scan index -> raster (8.5.6) */
static const uint8_t BLK_X[16] = { 0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3 };            /* luma4x4BlkIdx -> x4, y4 (6.4.3) */
static const uint8_t BLK_Y[16] = { 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3 };
static const uint16_t MF[6][3] = { { 13107, 5243, 8066 }, { 11916, 4660, 7490 }, { 10082, 4194, 6554 },
                                   { 9362, 3647, 5825 }, { 8192, 3355, 5243 }, { 7282, 2893, 4559 } };
static const uint8_t VV[6][3] = { { 10, 16, 13 }, { 11, 18, 14 }, { 13, 20, 16 }, { 14, 23, 18 }, { 16, 25, 20 }, { 18, 29, 23 } };
static const uint8_t QPC[52] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
                                 28, 29, 29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38, 39, 39, 39, 39 };
/* position class of a raster index: 0 both coordinates even, 1 both odd, 2 mixed */
static inline int pclass(int r) { int x = r & 3, y = r >> 2; return (x & 1) == 0 && (y & 1) == 0 ? 0 : ((x & 1) && (y & 1) ? 1 : 2); }
/* the same two tables per SCAN position (c9: the quantiser is the hot path) */
static uint16_t MFZ[6][16];
static uint8_t VZ[6][16];
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;
static void tables_init(void)
{
	int q, i;
	for (q = 0; q < 6; q++)
		for (i = 0; i < 16; i++) {
			MFZ[q][i] = MF[q][pclass(ZZ[i])];
			VZ[q][i] = VV[q][pclass(ZZ[i])];
		}
}

/* table 9-5 coeff_token, [table][4 * TotalCoeff + TrailingOnes] */
static const uint8_t CT_LEN[4][68] = {
	{ 1,0,0,0, 6,2,0,0, 8,6,3,0, 9,8,7,5, 10,9,8,6, 11,10,9,7, 13,11,10,8, 13,13,11,9, 13,13,13,10, 14,14,13,11, 14,14,14,13,
	  15,15,14,14, 15,15,15,14, 16,15,15,15, 16,16,16,15, 16,16,16,16, 16,16,16,16 },
	{ 2,0,0,0, 6,2,0,0, 6,5,3,0, 7,6,6,4, 8,6,6,4, 8,7,7,5, 9,8,8,6, 11,9,9,6, 11,11,11,7, 12,11,11,9, 12,12,12,11, 12,12,12,11,
	  13,13,13,12, 13,13,13,13, 13,14,13,13, 14,14,14,13, 14,14,14,14 },
	{ 4,0,0,0, 6,4,0,0, 6,5,4,0, 6,5,5,4, 7,5,5,4, 7,5,5,4, 7,6,6,4, 7,6,6,4, 8,7,7,5, 8,8,7,6, 9,8,8,7, 9,9,8,8, 9,9,9,8,
	  10,9,9,9, 10,10,10,10, 10,10,10,10, 10,10,10,10 },
	{ 6,0,0,0, 6,6,0,0, 6,6,6,0, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6,
	  6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6 },
};
static const uint8_t CT_BITS[4][68] = {
	{ 1,0,0,0, 5,1,0,0, 7,4,1,0, 7,6,5,3, 7,6,5,3, 7,6,5,4, 15,6,5,4, 11,14,5,4, 8,10,13,4, 15,14,9,4, 11,10,13,12,
	  15,14,9,12, 11,10,13,8, 15,1,9,12, 11,14,13,8, 7,10,9,12, 4,6,5,8 },
	{ 3,0,0,0, 11,2,0,0, 7,7,3,0, 7,10,9,5, 7,6,5,4, 4,6,5,6, 7,6,5,8, 15,6,5,4, 11,14,13,4, 15,10,9,4, 11,14,13,12, 8,10,9,8,
	  15,14,13,12, 11,10,9,12, 7,11,6,8, 9,8,10,1, 7,6,5,4 },
	{ 15,0,0,0, 15,14,0,0, 11,15,13,0, 8,12,14,12, 15,10,11,11, 11,8,9,10, 9,14,13,9, 8,10,9,8, 15,14,13,13, 11,14,10,12,
	  15,10,13,12, 11,14,9,12, 8,10,13,8, 13,7,9,12, 9,12,11,10, 5,8,7,6, 1,4,3,2 },
	{ 3,0,0,0, 0,1,0,0, 4,5,6,0, 8,9,10,11, 12,13,14,15, 16,17,18,19, 20,21,22,23, 24,25,26,27, 28,29,30,31, 32,33,34,35,
	  36,37,38,39, 40,41,42,43, 44,45,46,47, 48,49,50,51, 52,53,54,55, 56,57,58,59, 60,61,62,63 },
};
static const uint8_t CTC_LEN[20] = { 2,0,0,0, 6,1,0,0, 6,6,3,0, 6,7,7,6, 6,8,8,7 };            /* nC == -1 (chroma DC) */
static const uint8_t CTC_BITS[20] = { 1,0,0,0, 7,1,0,0, 4,6,1,0, 3,3,2,5, 2,3,2,0 };
/* tables 9-7, 9-8: total_zeros, [TotalCoeff - 1][total_zeros] */
static const uint8_t TZ_LEN[15][16] = {
	{ 1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9 }, { 3,3,3,3,3,4,4,4,4,5,5,6,6,6,6 }, { 4,3,3,3,4,4,3,3,4,5,5,6,5,6 },
	{ 5,3,4,4,3,3,3,4,3,4,5,5,5 }, { 4,4,4,3,3,3,3,3,4,5,4,5 }, { 6,5,3,3,3,3,3,3,4,3,6 }, { 6,5,3,3,3,2,3,4,3,6 },
	{ 6,4,5,3,2,2,3,3,6 }, { 6,6,4,2,2,3,2,5 }, { 5,5,3,2,2,2,4 }, { 4,4,3,3,1,3 }, { 4,4,2,1,3 }, { 3,3,1,2 }, { 2,2,1 }, { 1,1 },
};
static const uint8_t TZ_BITS[15][16] = {
	{ 1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1 }, { 7,6,5,4,3,5,4,3,2,3,2,3,2,1,0 }, { 5,7,6,5,4,3,4,3,2,3,2,1,1,0 },
	{ 3,7,5,4,6,5,4,3,3,2,2,1,0 }, { 5,4,3,7,6,5,4,3,2,1,1,0 }, { 1,1,7,6,5,4,3,2,1,1,0 }, { 1,1,5,4,3,3,2,1,1,0 },
	{ 1,1,1,3,3,2,2,1,0 }, { 1,0,1,3,2,1,1,1 }, { 1,0,1,3,2,1,1 }, { 0,1,1,2,1,3 }, { 0,1,1,1,1 }, { 0,1,1,1 }, { 0,1,1 }, { 0,1 },
};
static const uint8_t TZC_LEN[3][4] = { { 1,2,3,3 }, { 1,2,2 }, { 1,1 } };                      /* table 9-9 (a): chroma DC 4:2:0 */
static const uint8_t TZC_BITS[3][4] = { { 1,1,1,0 }, { 1,1,0 }, { 1,0 } };
/* table 9-10 run_before, [min(zerosLeft, 7) - 1][run_before] */
static const uint8_t RB_LEN[7][15] = { { 1,1 }, { 1,2,2 }, { 2,2,2,2 }, { 2,2,2,3,3 }, { 2,2,3,3,3,3 }, { 2,3,3,3,3,3,3 },
                                       { 3,3,3,3,3,3,3,4,5,6,7,8,9,10,11 } };
static const uint8_t RB_BITS[7][15] = { { 1,0 }, { 1,1,0 }, { 3,2,1,0 }, { 3,2,1,1,0 }, { 3,2,3,2,1,0 }, { 3,0,1,3,2,5,4 },
                                        { 7,6,5,4,3,2,1,1,1,1,1,1,1,1,1 } };
/* table 9-4, Inter column for ChromaArrayType 1: codeNum -> coded_block_pattern */
static const uint8_t CBP_INTER[48] = { 0, 16, 1, 2, 4, 8, 32, 3, 5, 10, 12, 15, 47, 7, 11, 13, 14, 6, 9, 31, 35, 37, 42, 44, 33, 34, 36, 40,
                                       39, 43, 45, 46, 17, 18, 20, 24, 19, 21, 26, 28, 23, 27, 29, 30, 22, 25, 38, 41 };

uint32_t n2avc_cbp_inter_code(int cbp)
{
	uint32_t i;
	for (i = 0; i < 48; i++)
		if (CBP_INTER[i] == cbp)
			return i;
	return 0;
}

/* ------------------------------------------------------------------ CAVLC (7.3.5.3.2, 9.2) ------------------------------- */

/* l[0..n-1]: the block's levels in scan order (n = 16, 15 or 4). nC: -1 for chroma DC. Returns TotalCoeff. */
static int cavlc_block(n2bw *w, const int16_t *l, int n, int nC)
{
	int pos[16], tc = 0, t1 = 0, i, tz, zl, sl;

	for (i = 0; i < n; i++)
		if (l[i])
			pos[tc++] = i;
	for (i = tc - 1; i >= 0 && t1 < 3; i--) {
		if (l[pos[i]] == 1 || l[pos[i]] == -1)
			t1++;
		else
			break;
	}
	if (nC < 0) {
		n2bw_u(w, CTC_LEN[4 * tc + t1], CTC_BITS[4 * tc + t1]);
	} else {
		int t = nC < 2 ? 0 : nC < 4 ? 1 : nC < 8 ? 2 : 3;
		n2bw_u(w, CT_LEN[t][4 * tc + t1], CT_BITS[t][4 * tc + t1]);
	}
	if (tc == 0)
		return 0;
	for (i = 0; i < t1; i++)
		n2bw_u(w, 1, l[pos[tc - 1 - i]] < 0);
	sl = (tc > 10 && t1 < 3) ? 1 : 0;
	for (i = tc - 1 - t1; i >= 0; i--) {
		int v = l[pos[i]], a = v < 0 ? -v : v;
		int code = v > 0 ? 2 * v - 2 : -2 * v - 1;
		if (i == tc - 1 - t1 && t1 < 3)
			code -= 2;
		if (sl == 0) {
			if (code < 14) {
				n2bw_u(w, code + 1, 1);
			} else if (code < 30) {
				n2bw_u(w, 15, 1);
				n2bw_u(w, 4, (uint32_t)(code - 14));
			} else {
				n2bw_u(w, 16, 1);
				n2bw_u(w, 12, (uint32_t)(code - 30));
			}
		} else {
			if (code < (15 << sl)) {
				n2bw_u(w, (code >> sl) + 1, 1);
				n2bw_u(w, sl, (uint32_t)code & ((1u << sl) - 1u));
			} else {
				n2bw_u(w, 16, 1);
				n2bw_u(w, 12, (uint32_t)(code - (15 << sl)));
			}
		}
		if (sl == 0)
			sl = 1;
		if (a > (3 << (sl - 1)) && sl < 6)
			sl++;
	}
	tz = pos[tc - 1] + 1 - tc;
	if (tc < n) {
		if (n == 4)
			n2bw_u(w, TZC_LEN[tc - 1][tz], TZC_BITS[tc - 1][tz]);
		else
			n2bw_u(w, TZ_LEN[tc - 1][tz], TZ_BITS[tc - 1][tz]);
	}
	zl = tz;
	for (i = tc - 1; i >= 1 && zl > 0; i--) {
		int run = pos[i] - pos[i - 1] - 1;
		int c = zl > 7 ? 7 : zl;
		n2bw_u(w, RB_LEN[c - 1][run], RB_BITS[c - 1][run]);
		zl -= run;
	}
	return tc;
}

/* nC of a block (9.2.1). base: 0 luma (4 blocks wide), 16 Cb, 20 Cr (2 wide). */
static int block_nc(const n2avc_slice *s, int mbx, int mby, int base, int x, int y)
{
	int wdt = base == 0 ? 4 : 2, na = -1, nb = -1;
	const n2avc_mbinfo *m = &s->mb[mby * s->mb_w + mbx];

	if (x > 0)
		na = m->nnz[base + y * wdt + x - 1];
	else if (n2avc_avail(s, mbx - 1, mby))
		na = s->mb[mby * s->mb_w + mbx - 1].nnz[base + y * wdt + wdt - 1];
	if (y > 0)
		nb = m->nnz[base + (y - 1) * wdt + x];
	else if (n2avc_avail(s, mbx, mby - 1))
		nb = s->mb[(mby - 1) * s->mb_w + mbx].nnz[base + (wdt - 1) * wdt + x];
	if (na >= 0 && nb >= 0)
		return (na + nb + 1) >> 1;
	return na >= 0 ? na : nb >= 0 ? nb : 0;
}

/* ------------------------------------------------------------------ transform and quantisation (8.5) --------------------- */

static void fdct4(const int d[16], int o[16])
{
	int t[16], i;
	for (i = 0; i < 4; i++) {
		int a = d[4 * i], b = d[4 * i + 1], c = d[4 * i + 2], e = d[4 * i + 3];
		int s0 = a + e, s3 = a - e, s1 = b + c, s2 = b - c;
		t[4 * i] = s0 + s1; t[4 * i + 1] = 2 * s3 + s2; t[4 * i + 2] = s0 - s1; t[4 * i + 3] = s3 - 2 * s2;
	}
	for (i = 0; i < 4; i++) {
		int a = t[i], b = t[4 + i], c = t[8 + i], e = t[12 + i];
		int s0 = a + e, s3 = a - e, s1 = b + c, s2 = b - c;
		o[i] = s0 + s1; o[4 + i] = 2 * s3 + s2; o[8 + i] = s0 - s1; o[12 + i] = s3 - 2 * s2;
	}
}

/* residual d (raster, already scaled: 8.5.12.1 output) -> added to pred, written to dst (8.5.12.2, 8.5.14) */
static void idct4_add(const int d[16], const uint8_t *pred, int ps, uint8_t *dst, int ds)
{
	int t[16], i;
	for (i = 0; i < 4; i++) {
		int d0 = d[4 * i], d1 = d[4 * i + 1], d2 = d[4 * i + 2], d3 = d[4 * i + 3];
		int e0 = d0 + d2, e1 = d0 - d2, e2 = (d1 >> 1) - d3, e3 = d1 + (d3 >> 1);
		t[4 * i] = e0 + e3; t[4 * i + 1] = e1 + e2; t[4 * i + 2] = e1 - e2; t[4 * i + 3] = e0 - e3;
	}
	for (i = 0; i < 4; i++) {
		int d0 = t[i], d1 = t[4 + i], d2 = t[8 + i], d3 = t[12 + i];
		int e0 = d0 + d2, e1 = d0 - d2, e2 = (d1 >> 1) - d3, e3 = d1 + (d3 >> 1);
		int r[4], k;
		r[0] = e0 + e3; r[1] = e1 + e2; r[2] = e1 - e2; r[3] = e0 - e3;
		for (k = 0; k < 4; k++) {
			int v = pred[k * ps + i] + ((r[k] + 32) >> 6);
			dst[k * ds + i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
		}
	}
}

static inline int quant1(int wv, int mf, int f, int qbits)
{
	int a = wv < 0 ? -wv : wv;
	int z = (int)(((int64_t)a * mf + f) >> qbits);
	if (z > 2000)
		z = 2000;                       /* keeps level_prefix <= 15 (9.2.2.1); the reconstruction uses the same value */
	return wv < 0 ? -z : z;
}

/* W (raster) -> levels in scan order lv[first..15]; returns the number of non-zero ones */
static int quant4(const int wv[16], int qp, int intra, int first, int16_t lv[16])
{
	int qbits = 15 + qp / 6, i, nz = 0;
	uint32_t f = (1u << qbits) / (intra ? 3u : 6u);
	const uint16_t *mf = MFZ[qp % 6];
	for (i = 0; i < first; i++)
		lv[i] = 0;
	for (i = first; i < 16; i++) {
		/* 32 bits are enough: |W| <= 36 * 255 for a residual of 8-bit samples, times MF < 2^14, plus f < 2^23 */
		int w = wv[ZZ[i]];
		uint32_t a = (uint32_t)(w < 0 ? -w : w);
		int z = (int)((a * mf[i] + f) >> qbits);
		if (z > 2000)
			z = 2000;
		lv[i] = (int16_t)(w < 0 ? -z : z);
		nz += z != 0;
	}
	return nz;
}

/* levels (scan order, lv[0] ignored when dc_given) -> scaled coefficients d (raster), 8.5.12.1 */
static void dequant4(const int16_t lv[16], int qp, int dc_given, int dc, int d[16])
{
	int i, sh = qp / 6;
	const uint8_t *v = VZ[qp % 6];
	for (i = 0; i < 16; i++)
		d[ZZ[i]] = lv[i] * v[i] * (1 << sh);
	if (dc_given)
		d[0] = dc;
}

/* ------------------------------------------------------------------ macroblock helpers ----------------------------------- */

static void diff4(const uint8_t *src, int ss, const uint8_t *pred, int ps, int d[16])
{
	int x, y;
	for (y = 0; y < 4; y++)
		for (x = 0; x < 4; x++)
			d[4 * y + x] = src[y * ss + x] - pred[y * ps + x];
}

/* The two chroma planes of one macroblock: transform, quantise, reconstruct. Fills coef[16..23], dc_cb, dc_cr, nnz[16..23].
 * Returns the chroma part of coded_block_pattern (0, 1, 2). */
static int chroma_residual(n2avc_slice *s, int mbx, int mby, const uint8_t *pred_cb, const uint8_t *pred_cr, int intra)
{
	n2avc_mbinfo *m = &s->mb[mby * s->mb_w + mbx];
	int qpc = s->qpc, qbits = 15 + qpc / 6, f = (1 << qbits) / (intra ? 3 : 6);
	int comp, any_dc = 0, any_ac = 0, cbp;
	int dcrec[2][4];

	for (comp = 0; comp < 2; comp++) {
		const n2avc_plane *sp = comp ? &s->src->cr : &s->src->cb;
		const uint8_t *src = sp->p + (size_t)(mby * 8) * sp->stride + mbx * 8;
		const uint8_t *pred = comp ? pred_cr : pred_cb;
		int16_t *dcl = comp ? s->dc_cr : s->dc_cb;
		int dc[4], yd[4], b;

		for (b = 0; b < 4; b++) {
			int d[16], wv[16], bx = b & 1, by = b >> 1;
			diff4(src + (by * 4) * sp->stride + bx * 4, sp->stride, pred + by * 4 * 8 + bx * 4, 8, d);
			fdct4(d, wv);
			dc[b] = wv[0];
			m->nnz[16 + comp * 4 + b] = (uint8_t)quant4(wv, qpc, intra, 1, s->coef[16 + comp * 4 + b]);
			any_ac |= m->nnz[16 + comp * 4 + b] != 0;
		}
		yd[0] = dc[0] + dc[1] + dc[2] + dc[3];
		yd[1] = dc[0] - dc[1] + dc[2] - dc[3];
		yd[2] = dc[0] + dc[1] - dc[2] - dc[3];
		yd[3] = dc[0] - dc[1] - dc[2] + dc[3];
		for (b = 0; b < 4; b++) {
			dcl[b] = (int16_t)quant1(yd[b], MF[qpc % 6][0], 2 * f, qbits + 1);
			any_dc |= dcl[b] != 0;
		}
	}
	cbp = any_ac ? 2 : any_dc ? 1 : 0;
	for (comp = 0; comp < 2; comp++) {
		int16_t *dcl = comp ? s->dc_cr : s->dc_cb;
		int fq[4], b;
		if (cbp == 0)
			dcl[0] = dcl[1] = dcl[2] = dcl[3] = 0;
		fq[0] = dcl[0] + dcl[1] + dcl[2] + dcl[3];
		fq[1] = dcl[0] - dcl[1] + dcl[2] - dcl[3];
		fq[2] = dcl[0] + dcl[1] - dcl[2] - dcl[3];
		fq[3] = dcl[0] - dcl[1] - dcl[2] + dcl[3];
		for (b = 0; b < 4; b++)
			dcrec[comp][b] = (fq[b] * (16 * VV[qpc % 6][0]) * (1 << (qpc / 6))) >> 5;         /* 8.5.11.2 */
	}
	for (comp = 0; comp < 2; comp++) {
		n2avc_plane *rp = comp ? &s->rec->cr : &s->rec->cb;
		uint8_t *rec = rp->p + (size_t)(mby * 8) * rp->stride + mbx * 8;
		const uint8_t *pred = comp ? pred_cr : pred_cb;
		int b;
		for (b = 0; b < 4; b++) {
			int d[16], bx = b & 1, by = b >> 1;
			if (cbp < 2) {
				memset(s->coef[16 + comp * 4 + b], 0, sizeof s->coef[0]);
				m->nnz[16 + comp * 4 + b] = 0;
			}
			dequant4(s->coef[16 + comp * 4 + b], qpc, 1, dcrec[comp][b], d);
			idct4_add(d, pred + by * 4 * 8 + bx * 4, 8, rec + (by * 4) * rp->stride + bx * 4, rp->stride);
		}
	}
	return cbp;
}

static void write_chroma(n2avc_slice *s, int mbx, int mby, int cbpc)
{
	int comp, b;
	if (cbpc == 0)
		return;
	cavlc_block(&s->bw, s->dc_cb, 4, -1);
	cavlc_block(&s->bw, s->dc_cr, 4, -1);
	if (cbpc == 2)
		for (comp = 0; comp < 2; comp++)
			for (b = 0; b < 4; b++)
				cavlc_block(&s->bw, s->coef[16 + comp * 4 + b] + 1, 15, block_nc(s, mbx, mby, 16 + comp * 4, b & 1, b >> 1));
}

void n2avc_flush_skip_run(n2avc_slice *s)
{
	n2bw_ue(&s->bw, s->skip_run);
	s->skip_run = 0;
}

/* ------------------------------------------------------------------ inter residual --------------------------------------- */

int n2avc_inter_residual(n2avc_slice *s, int mbx, int mby, const uint8_t *pred_y, const uint8_t *pred_cb, const uint8_t *pred_cr)
{
	n2avc_mbinfo *m = &s->mb[mby * s->mb_w + mbx];
	const uint8_t *src = s->src->y.p + (size_t)(mby * 16) * s->src->y.stride + mbx * 16;
	uint8_t *rec = s->rec->y.p + (size_t)(mby * 16) * s->rec->y.stride + mbx * 16;
	int ss = s->src->y.stride, rs = s->rec->y.stride, k, cbp = 0, cbpc;

	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2, d[16], wv[16];
		diff4(src + y4 * 4 * ss + x4 * 4, ss, pred_y + y4 * 4 * 16 + x4 * 4, 16, d);
		fdct4(d, wv);
		m->nnz[k] = (uint8_t)quant4(wv, s->qp, 0, 0, s->coef[k]);
		if (m->nnz[k])
			cbp |= 1 << ((y4 >> 1) * 2 + (x4 >> 1));
	}
	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2, d[16];
		const uint8_t *p = pred_y + y4 * 4 * 16 + x4 * 4;
		uint8_t *r = rec + y4 * 4 * rs + x4 * 4;
		if (m->nnz[k]) {
			dequant4(s->coef[k], s->qp, 0, 0, d);
			idct4_add(d, p, 16, r, rs);
		} else {
			int y;
			for (y = 0; y < 4; y++)
				memcpy(r + y * rs, p + y * 16, 4);
		}
	}
	cbpc = chroma_residual(s, mbx, mby, pred_cb, pred_cr, 0);
	m->cbp = (uint8_t)(cbp | (cbpc << 4));
	return cbp | (cbpc << 4);
}

void n2avc_write_inter_residual(n2avc_slice *s, int mbx, int mby, int cbp)
{
	int b;
	for (b = 0; b < 16; b++) {
		int x4 = BLK_X[b], y4 = BLK_Y[b];
		if (cbp & (1 << ((y4 >> 1) * 2 + (x4 >> 1))))
			cavlc_block(&s->bw, s->coef[y4 * 4 + x4], 16, block_nc(s, mbx, mby, 0, x4, y4));
	}
	write_chroma(s, mbx, mby, cbp >> 4);
}

/* ------------------------------------------------------------------ Intra16x16 (8.3.3, 8.3.4) ---------------------------- */

static void chroma_dc_pred(const uint8_t *rec, int rs, int left, int top, uint8_t pred[64])
{
	int b, x, y;
	for (b = 0; b < 4; b++) {
		int bx = b & 1, by = b >> 1, st = 0, sl = 0, v;
		if (top)
			for (x = 0; x < 4; x++)
				st += rec[-rs + bx * 4 + x];
		if (left)
			for (y = 0; y < 4; y++)
				sl += rec[(by * 4 + y) * rs - 1];
		if ((bx == 0 && by == 0) || (bx == 1 && by == 1))
			v = (top && left) ? (st + sl + 4) >> 3 : top ? (st + 2) >> 2 : left ? (sl + 2) >> 2 : 128;
		else if (bx == 1)               /* top right: the row above first */
			v = top ? (st + 2) >> 2 : left ? (sl + 2) >> 2 : 128;
		else                            /* bottom left: the column to the left first */
			v = left ? (sl + 2) >> 2 : top ? (st + 2) >> 2 : 128;
		for (y = 0; y < 4; y++)
			memset(pred + (by * 4 + y) * 8 + bx * 4, v, 4);
	}
}

void n2avc_intra_mb(n2avc_slice *s, int mbx, int mby)
{
	n2avc_mbinfo *m = &s->mb[mby * s->mb_w + mbx];
	const uint8_t *src = s->src->y.p + (size_t)(mby * 16) * s->src->y.stride + mbx * 16;
	uint8_t *rec = s->rec->y.p + (size_t)(mby * 16) * s->rec->y.stride + mbx * 16;
	int ss = s->src->y.stride, rs = s->rec->y.stride;
	int left = n2avc_avail(s, mbx - 1, mby), top = n2avc_avail(s, mbx, mby - 1);
	uint8_t pred[3][256], pcb[64], pcr[64];
	const uint8_t *py;
	int mode, best = 2, x, y, k, qp = s->qp, any_ac = 0, cbpc;
	uint32_t bestsad = 0xffffffffu;
	int dcw[16], dcrec[16], qbits = 15 + qp / 6, f = (1 << qbits) / 3;

	/* candidates: 0 vertical, 1 horizontal, 2 DC */
	{
		int sum = 0, dcv;
		if (top)
			for (x = 0; x < 16; x++)
				sum += rec[-rs + x];
		if (left)
			for (y = 0; y < 16; y++)
				sum += rec[y * rs - 1];
		dcv = (top && left) ? (sum + 16) >> 5 : (top || left) ? (sum + 8) >> 4 : 128;
		memset(pred[2], dcv, 256);
		if (top)
			for (y = 0; y < 16; y++)
				memcpy(pred[0] + y * 16, rec - rs, 16);
		if (left)
			for (y = 0; y < 16; y++)
				memset(pred[1] + y * 16, rec[y * rs - 1], 16);
	}
	for (mode = 0; mode < 3; mode++) {
		uint32_t sad = 0;
		if ((mode == 0 && !top) || (mode == 1 && !left))
			continue;
		for (y = 0; y < 16; y++)
			for (x = 0; x < 16; x++) {
				int d = src[y * ss + x] - pred[mode][y * 16 + x];
				sad += (uint32_t)(d < 0 ? -d : d);
			}
		if (sad < bestsad) {
			bestsad = sad;
			best = mode;
		}
	}
	py = pred[best];

	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2, d[16], wv[16];
		diff4(src + y4 * 4 * ss + x4 * 4, ss, py + y4 * 4 * 16 + x4 * 4, 16, d);
		fdct4(d, wv);
		dcw[k] = wv[0];
		m->nnz[k] = (uint8_t)quant4(wv, qp, 1, 1, s->coef[k]);
		any_ac |= m->nnz[k] != 0;
	}
	{       /* the 16 DC values: Hadamard, halve, quantise (levels in scan order), and the decoder's inverse (8.5.10) */
		int t[16], h[16], c[16], i;
		for (i = 0; i < 4; i++) {
			int a = dcw[4 * i], b = dcw[4 * i + 1], cc = dcw[4 * i + 2], e = dcw[4 * i + 3];
			t[4 * i] = a + b + cc + e; t[4 * i + 1] = a + b - cc - e; t[4 * i + 2] = a - b - cc + e; t[4 * i + 3] = a - b + cc - e;
		}
		for (i = 0; i < 4; i++) {
			int a = t[i], b = t[4 + i], cc = t[8 + i], e = t[12 + i];
			h[i] = (a + b + cc + e + 1) >> 1; h[4 + i] = (a + b - cc - e + 1) >> 1;
			h[8 + i] = (a - b - cc + e + 1) >> 1; h[12 + i] = (a - b + cc - e + 1) >> 1;
		}
		for (i = 0; i < 16; i++) {
			s->dc_y[i] = (int16_t)quant1(h[ZZ[i]], MF[qp % 6][0], 2 * f, qbits + 1);
			c[ZZ[i]] = s->dc_y[i];
		}
		for (i = 0; i < 4; i++) {
			int a = c[4 * i], b = c[4 * i + 1], cc = c[4 * i + 2], e = c[4 * i + 3];
			t[4 * i] = a + b + cc + e; t[4 * i + 1] = a + b - cc - e; t[4 * i + 2] = a - b - cc + e; t[4 * i + 3] = a - b + cc - e;
		}
		for (i = 0; i < 4; i++) {
			int a = t[i], b = t[4 + i], cc = t[8 + i], e = t[12 + i], r[4], j;
			int ls = 16 * VV[qp % 6][0];
			r[0] = a + b + cc + e; r[1] = a + b - cc - e; r[2] = a - b - cc + e; r[3] = a - b + cc - e;
			for (j = 0; j < 4; j++)
				dcrec[4 * j + i] = qp >= 36 ? r[j] * ls * (1 << (qp / 6 - 6)) : (r[j] * ls + (1 << (5 - qp / 6))) >> (6 - qp / 6);
		}
	}
	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2, d[16];
		if (!any_ac)
			m->nnz[k] = 0;
		dequant4(s->coef[k], qp, 1, dcrec[k], d);
		idct4_add(d, py + y4 * 4 * 16 + x4 * 4, 16, rec + y4 * 4 * rs + x4 * 4, rs);
	}
	chroma_dc_pred(s->rec->cb.p + (size_t)(mby * 8) * s->rec->cb.stride + mbx * 8, s->rec->cb.stride, left, top, pcb);
	chroma_dc_pred(s->rec->cr.p + (size_t)(mby * 8) * s->rec->cr.stride + mbx * 8, s->rec->cr.stride, left, top, pcr);
	cbpc = chroma_residual(s, mbx, mby, pcb, pcr, 1);

	m->mvx = m->mvy = 0;
	m->ref = -1;
	m->type = N2MB_I16;
	m->qp = (uint8_t)qp;
	m->cbp = (uint8_t)((any_ac ? 15 : 0) | (cbpc << 4));

	/* macroblock_layer (7.3.5): mb_type (table 7-11), intra_chroma_pred_mode 0 (DC), mb_qp_delta 0, residual */
	n2bw_ue(&s->bw, (uint32_t)((s->slice_type == 0 ? 5 : 0) + 1 + best + 4 * cbpc + (any_ac ? 12 : 0)));
	n2bw_ue(&s->bw, 0);
	n2bw_se(&s->bw, 0);
	cavlc_block(&s->bw, s->dc_y, 16, block_nc(s, mbx, mby, 0, 0, 0));
	if (any_ac) {
		int b;
		for (b = 0; b < 16; b++)
			cavlc_block(&s->bw, s->coef[BLK_Y[b] * 4 + BLK_X[b]] + 1, 15, block_nc(s, mbx, mby, 0, BLK_X[b], BLK_Y[b]));
	}
	write_chroma(s, mbx, mby, cbpc);
}

/* ------------------------------------------------------------------ the encoder ------------------------------------------ */

#define N2AVC_MIN_RC_QP 20

struct n2avc_enc {
	n2avc_config cfg;
	int mb_w, mb_h, nslices, nthreads, level;
	uint8_t *mem[3];                /* allocations of the three pictures */
	n2avc_frame fr[3];              /* [0] source, [1], [2] reconstruction / reference */
	int cur;                        /* index (1 or 2) of the reconstruction being written */
	int have_ref;
	n2avc_mbinfo *mb;
	n2avc_slice *sl;
	uint8_t **slbuf;
	size_t slcap;
	uint8_t *out;
	size_t outcap;
	uint8_t sps[96], pps[32];
	size_t sps_n, pps_n;
	n2avc_rc *rc;
	uint32_t frame_num, since_idr, idr_id;
	int dead;
};

static int alloc_frame(n2avc_enc *e, int i)
{
	int w = e->mb_w * 16, h = e->mb_h * 16, P = N2AVC_PAD;
	size_t ys = (size_t)(w + 2 * P), yh = (size_t)(h + 2 * P), cs = (size_t)(w / 2 + P), ch = (size_t)(h / 2 + P);
	uint8_t *m = calloc(1, ys * yh + 2 * cs * ch + 64);
	if (!m)
		return -1;
	e->mem[i] = m;
	e->fr[i].y.p = m + ys * P + P; e->fr[i].y.stride = (int)ys;
	e->fr[i].cb.p = m + ys * yh + cs * (P / 2) + P / 2; e->fr[i].cb.stride = (int)cs;
	e->fr[i].cr.p = m + ys * yh + cs * ch + cs * (P / 2) + P / 2; e->fr[i].cr.stride = (int)cs;
	return 0;
}

static void extend_plane(n2avc_plane *p, int w, int h, int pad)
{
	int y;
	for (y = 0; y < h; y++) {
		uint8_t *r = p->p + (size_t)y * p->stride;
		memset(r - pad, r[0], (size_t)pad);
		memset(r + w, r[w - 1], (size_t)pad);
	}
	for (y = 1; y <= pad; y++) {
		memcpy(p->p - (size_t)y * p->stride - pad, p->p - pad, (size_t)(w + 2 * pad));
		memcpy(p->p + (size_t)(h - 1 + y) * p->stride - pad, p->p + (size_t)(h - 1) * p->stride - pad, (size_t)(w + 2 * pad));
	}
}

static int pick_level(int mbs, uint32_t fn, uint32_t fd)
{
	static const struct { int level; uint32_t fs, mbps; } T[6] = { { 30, 1620, 40500 }, { 31, 3600, 108000 }, { 40, 8192, 245760 },
	                                                                { 42, 8704, 522240 }, { 51, 36864, 983040 }, { 52, 36864, 2073600 } };
	int i;
	for (i = 0; i < 6; i++)
		if ((uint32_t)mbs <= T[i].fs && (uint64_t)mbs * fn <= (uint64_t)T[i].mbps * fd)
			return T[i].level;
	return 0;
}

static void write_parameter_sets(n2avc_enc *e)
{
	uint8_t rb[96];
	n2bw w;
	const n2avc_config *c = &e->cfg;
	int cr = (e->mb_w * 16 - (int)c->width) / 2, cbm = (e->mb_h * 16 - (int)c->height) / 2;

	n2bw_init(&w, rb, sizeof rb);
	n2bw_u(&w, 8, 66);                      /* profile_idc: Baseline */
	n2bw_u(&w, 8, 0xC0);                    /* constraint_set0, set1; the rest and the reserved bits 0 */
	n2bw_u(&w, 8, (uint32_t)e->level);
	n2bw_ue(&w, 0);                         /* seq_parameter_set_id */
	n2bw_ue(&w, 4);                         /* log2_max_frame_num_minus4: frame_num is 8 bits */
	n2bw_ue(&w, 2);                         /* pic_order_cnt_type */
	n2bw_ue(&w, 1);                         /* max_num_ref_frames */
	n2bw_u(&w, 1, 0);                       /* gaps_in_frame_num_value_allowed_flag */
	n2bw_ue(&w, (uint32_t)e->mb_w - 1);
	n2bw_ue(&w, (uint32_t)e->mb_h - 1);
	n2bw_u(&w, 1, 1);                       /* frame_mbs_only_flag */
	n2bw_u(&w, 1, 1);                       /* direct_8x8_inference_flag */
	n2bw_u(&w, 1, (cr || cbm) ? 1 : 0);     /* frame_cropping_flag */
	if (cr || cbm) {
		n2bw_ue(&w, 0); n2bw_ue(&w, (uint32_t)cr); n2bw_ue(&w, 0); n2bw_ue(&w, (uint32_t)cbm);
	}
	n2bw_u(&w, 1, 1);                       /* vui_parameters_present_flag */
	n2bw_u(&w, 1, 0);                       /* aspect_ratio_info_present_flag */
	n2bw_u(&w, 1, 0);                       /* overscan_info_present_flag */
	n2bw_u(&w, 1, 1);                       /* video_signal_type_present_flag */
	n2bw_u(&w, 3, 5);                       /* video_format: unspecified */
	n2bw_u(&w, 1, (c->flags & N2AVC_F_FULLRANGE) ? 1 : 0);
	n2bw_u(&w, 1, 1);                       /* colour_description_present_flag */
	n2bw_u(&w, 8, c->colour_primaries); n2bw_u(&w, 8, c->transfer); n2bw_u(&w, 8, c->matrix);
	n2bw_u(&w, 1, 0);                       /* chroma_loc_info_present_flag */
	n2bw_u(&w, 1, 1);                       /* timing_info_present_flag */
	n2bw_u(&w, 32, c->fps_den); n2bw_u(&w, 32, 2 * c->fps_num);
	n2bw_u(&w, 1, 0);                       /* fixed_frame_rate_flag */
	n2bw_u(&w, 1, 0); n2bw_u(&w, 1, 0);     /* nal_hrd, vcl_hrd */
	n2bw_u(&w, 1, 0);                       /* pic_struct_present_flag */
	n2bw_u(&w, 1, 1);                       /* bitstream_restriction_flag */
	n2bw_u(&w, 1, 1);                       /* motion_vectors_over_pic_boundaries_flag */
	n2bw_ue(&w, 0); n2bw_ue(&w, 0);         /* max_bytes_per_pic_denom, max_bits_per_mb_denom */
	n2bw_ue(&w, 9); n2bw_ue(&w, 9);         /* log2_max_mv_length_horizontal / vertical */
	n2bw_ue(&w, 0);                         /* max_num_reorder_frames */
	n2bw_ue(&w, 1);                         /* max_dec_frame_buffering */
	n2bw_trailing(&w);
	e->sps_n = n2avc_nal(e->sps, sizeof e->sps, rb, n2bw_flush(&w), 3, 7);

	n2bw_init(&w, rb, sizeof rb);
	n2bw_ue(&w, 0); n2bw_ue(&w, 0);         /* pic_parameter_set_id, seq_parameter_set_id */
	n2bw_u(&w, 1, 0);                       /* entropy_coding_mode_flag: CAVLC */
	n2bw_u(&w, 1, 0);                       /* bottom_field_pic_order_in_frame_present_flag */
	n2bw_ue(&w, 0);                         /* num_slice_groups_minus1 */
	n2bw_ue(&w, 0); n2bw_ue(&w, 0);         /* num_ref_idx_l0/l1_default_active_minus1 */
	n2bw_u(&w, 1, 0); n2bw_u(&w, 2, 0);     /* weighted_pred_flag, weighted_bipred_idc */
	n2bw_se(&w, 0); n2bw_se(&w, 0); n2bw_se(&w, 0);        /* pic_init_qp, pic_init_qs, chroma_qp_index_offset */
	n2bw_u(&w, 1, 1);                       /* deblocking_filter_control_present_flag */
	n2bw_u(&w, 1, 0);                       /* constrained_intra_pred_flag */
	n2bw_u(&w, 1, 0);                       /* redundant_pic_cnt_present_flag */
	n2bw_trailing(&w);
	e->pps_n = n2avc_nal(e->pps, sizeof e->pps, rb, n2bw_flush(&w), 3, 8);
}

int n2avc_open(const n2avc_config *config, n2avc_enc **out)
{
	n2avc_enc *e;
	n2avc_config c;
	int i, mbs;

	if (!config || !out || config->size < sizeof(n2avc_config))
		return N2AVC_E_ARGUMENT;
	c = *config;
	if (c.width < 16 || c.width > 4096 || c.height < 16 || c.height > 2304 || (c.width & 1) || (c.height & 1))
		return N2AVC_E_ARGUMENT;
	if (c.fps_den == 0 || c.fps_num == 0) { c.fps_num = 30; c.fps_den = 1; }
	if (c.qp != 0 && (c.qp < 10 || c.qp > 51))
		return N2AVC_E_ARGUMENT;
	if (c.keyint == 0) c.keyint = 60;
	if (c.threads == 0) c.threads = 1;
	if (c.threads > 16) return N2AVC_E_ARGUMENT;
	if (c.search_range == 0) c.search_range = 16;
	if (c.search_range > 64) return N2AVC_E_ARGUMENT;
	if (c.colour_primaries == 0) c.colour_primaries = c.height >= 720 ? 1 : 6;
	if (c.transfer == 0) c.transfer = c.height >= 720 ? 1 : 6;
	if (c.matrix == 0) c.matrix = c.height >= 720 ? 1 : 6;
	if (c.flags & N2AVC_F_ANNEXB) c.flags |= N2AVC_F_HEADERS_IN_KEYFRAMES;

	pthread_once(&tables_once, tables_init);
	e = calloc(1, sizeof *e);
	if (!e)
		return N2AVC_E_MEMORY;
	e->cfg = c;
	e->mb_w = ((int)c.width + 15) / 16;
	e->mb_h = ((int)c.height + 15) / 16;
	mbs = e->mb_w * e->mb_h;
	e->level = pick_level(mbs, c.fps_num, c.fps_den);
	/* c9: `threads` workers share 2 x threads slices (at most 16, at most one per macroblock row) from a queue, so that a
	 * worker on a slow core or a busy one does not hold the picture back: the fast ones take more slices. */
	e->nthreads = (int)c.threads;
	e->nslices = c.threads > 1 ? 2 * (int)c.threads : 1;
	if (e->nslices > 16) e->nslices = 16;
	if (e->nslices > e->mb_h) e->nslices = e->mb_h;
	if (e->nthreads > e->nslices) e->nthreads = e->nslices;
	e->slcap = (size_t)mbs * 1536 / (size_t)e->nslices + 8192;
	e->outcap = (size_t)mbs * 1536 * 3 / 2 + (size_t)e->nslices * 8192 + 1024;
	e->mb = calloc((size_t)mbs, sizeof *e->mb);
	e->sl = calloc((size_t)e->nslices, sizeof *e->sl);
	e->slbuf = calloc((size_t)e->nslices, sizeof *e->slbuf);
	e->out = malloc(e->outcap);
	if (!e->level || !e->mb || !e->sl || !e->slbuf || !e->out)
		goto fail;
	for (i = 0; i < e->nslices; i++)
		if (!(e->slbuf[i] = malloc(e->slcap)))
			goto fail;
	for (i = 0; i < 3; i++)
		if (alloc_frame(e, i))
			goto fail;
	e->cur = 1;
	e->rc = n2avc_rc_open(config);
	if (!e->rc)
		goto fail;
	write_parameter_sets(e);
	*out = e;
	return N2AVC_OK;
fail:
	{
		int lv = e->level;
		n2avc_close(e);
		return lv ? N2AVC_E_MEMORY : N2AVC_E_ARGUMENT;
	}
}

void n2avc_close(n2avc_enc *e)
{
	int i;
	if (!e)
		return;
	if (e->rc)
		n2avc_rc_close(e->rc);
	for (i = 0; i < 3; i++)
		free(e->mem[i]);
	if (e->slbuf)
		for (i = 0; i < e->nslices; i++)
			free(e->slbuf[i]);
	free(e->slbuf); free(e->sl); free(e->mb); free(e->out);
	free(e);
}

int n2avc_headers(n2avc_enc *e, const uint8_t **sps, size_t *sps_size, const uint8_t **pps, size_t *pps_size)
{
	if (!e || !sps || !sps_size || !pps || !pps_size)
		return N2AVC_E_ARGUMENT;
	*sps = e->sps; *sps_size = e->sps_n; *pps = e->pps; *pps_size = e->pps_n;
	return N2AVC_OK;
}

int n2avc_reconfigure(n2avc_enc *e, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den)
{
	if (!e || e->dead)
		return N2AVC_E_STATE;
	n2avc_rc_reconfigure(e->rc, bitrate, fps_num, fps_den);
	return N2AVC_OK;
}

int n2avc_reconstruction(n2avc_enc *e, const uint8_t *plane[3], uint32_t stride[3])
{
	const n2avc_frame *f;
	if (!e || !plane || !stride || !e->have_ref)
		return N2AVC_E_STATE;
	f = &e->fr[3 - e->cur];         /* the picture coded last: cur was swapped after it */
	plane[0] = f->y.p; plane[1] = f->cb.p; plane[2] = f->cr.p;
	stride[0] = (uint32_t)f->y.stride; stride[1] = (uint32_t)f->cb.stride; stride[2] = (uint32_t)f->cr.stride;
	return N2AVC_OK;
}

static void load_source(n2avc_enc *e, const n2avc_picture *p)
{
	n2avc_frame *f = &e->fr[0];
	int w = (int)e->cfg.width, h = (int)e->cfg.height, W = e->mb_w * 16, H = e->mb_h * 16, x, y, cw = w / 2, ch = h / 2;

	for (y = 0; y < H; y++) {
		uint8_t *d = f->y.p + (size_t)y * f->y.stride;
		if (y < h) {
			memcpy(d, p->plane[0] + (size_t)y * p->stride[0], (size_t)w);
			if (W > w)
				memset(d + w, d[w - 1], (size_t)(W - w));
		} else {
			memcpy(d, d - f->y.stride, (size_t)W);
		}
	}
	for (y = 0; y < H / 2; y++) {
		uint8_t *db = f->cb.p + (size_t)y * f->cb.stride, *dr = f->cr.p + (size_t)y * f->cr.stride;
		if (y < ch) {
			if (p->format == N2AVC_FMT_NV12) {
				const uint8_t *sp = p->plane[1] + (size_t)y * p->stride[1];
				for (x = 0; x < cw; x++) {
					db[x] = sp[2 * x];
					dr[x] = sp[2 * x + 1];
				}
			} else {
				memcpy(db, p->plane[1] + (size_t)y * p->stride[1], (size_t)cw);
				memcpy(dr, p->plane[2] + (size_t)y * p->stride[2], (size_t)cw);
			}
			if (W / 2 > cw) {
				memset(db + cw, db[cw - 1], (size_t)(W / 2 - cw));
				memset(dr + cw, dr[cw - 1], (size_t)(W / 2 - cw));
			}
		} else {
			memcpy(db, db - f->cb.stride, (size_t)(W / 2));
			memcpy(dr, dr - f->cr.stride, (size_t)(W / 2));
		}
	}
}

struct slice_job { n2avc_enc *e; n2avc_slice *s; int idr; size_t bytes; };
struct slice_queue { struct slice_job *job; int count; int next; pthread_mutex_t lock; };
static void *slice_run(void *arg);
static void *slice_worker(void *arg)
{
	struct slice_queue *q = arg;
	for (;;) {
		int i;
		pthread_mutex_lock(&q->lock);
		i = q->next < q->count ? q->next++ : -1;
		pthread_mutex_unlock(&q->lock);
		if (i < 0)
			return NULL;
		slice_run(&q->job[i]);
	}
}

static void *slice_run(void *arg)
{
	struct slice_job *j = arg;
	n2avc_enc *e = j->e;
	n2avc_slice *s = j->s;
	int mbx, mby, inter = 0;

	/* slice_header (7.3.3) */
	n2bw_ue(&s->bw, (uint32_t)(s->first_row * s->mb_w));    /* first_mb_in_slice */
	n2bw_ue(&s->bw, s->slice_type == 2 ? 7 : 5);            /* slice_type: all slices of the picture alike */
	n2bw_ue(&s->bw, 0);                                     /* pic_parameter_set_id */
	n2bw_u(&s->bw, 8, e->frame_num & 255);
	if (j->idr)
		n2bw_ue(&s->bw, e->idr_id & 1);
	if (s->slice_type == 0) {
		n2bw_u(&s->bw, 1, 0);                           /* num_ref_idx_active_override_flag */
		n2bw_u(&s->bw, 1, 0);                           /* ref_pic_list_modification_flag_l0 */
	}
	if (j->idr) {
		n2bw_u(&s->bw, 1, 0);                           /* no_output_of_prior_pics_flag */
		n2bw_u(&s->bw, 1, 0);                           /* long_term_reference_flag */
	} else {
		n2bw_u(&s->bw, 1, 0);                           /* adaptive_ref_pic_marking_mode_flag */
	}
	n2bw_se(&s->bw, s->qp - 26);                            /* slice_qp_delta */
	if (e->cfg.flags & N2AVC_F_DEBLOCK) {                   /* the loop filter of clause 8.7 (avc2/n2avc_deblock.c, R2) */
		n2bw_ue(&s->bw, 0);                             /* disable_deblocking_filter_idc 0: every edge, also between slices */
		n2bw_se(&s->bw, 0);                             /* slice_alpha_c0_offset_div2 */
		n2bw_se(&s->bw, 0);                             /* slice_beta_offset_div2 */
	} else {
		n2bw_ue(&s->bw, 1);                             /* disable_deblocking_filter_idc 1: no loop filter */
	}

	if (s->slice_type == 0) {
		s->inter_priv = n2avc_inter_slice_begin(s);
		inter = s->inter_priv != NULL;
	}
	for (mby = s->first_row; mby < s->end_row; mby++)
		for (mbx = 0; mbx < s->mb_w; mbx++) {
			if (s->slice_type == 2) {
				n2avc_intra_mb(s, mbx, mby);
			} else {
				int r = inter ? n2avc_p_mb(s, mbx, mby) : 2;
				if (r == 2) {
					n2avc_flush_skip_run(s);
					n2avc_intra_mb(s, mbx, mby);
				}
#ifdef N2AVC_TRACE
				{
					extern int printf(const char *, ...);
					const n2avc_mbinfo *m = &s->mb[mby * s->mb_w + mbx];
					printf("f%u mb %d,%d r=%d type=%d mv=%d,%d ref=%d cbp=%d\n", e->frame_num, mbx, mby, r, m->type, m->mvx, m->mvy, m->ref, m->cbp);
				}
#endif
			}
			if (s->bw.overflow)
				goto done;
		}
	if (s->slice_type == 0 && s->skip_run > 0)
		n2avc_flush_skip_run(s);
	n2bw_trailing(&s->bw);
done:
	if (inter)
		n2avc_inter_slice_end(s);
	j->bytes = n2bw_flush(&s->bw);
	return NULL;
}

int n2avc_encode(n2avc_enc *e, const n2avc_picture *pic, int force_idr, n2avc_packet *out)
{
	struct slice_job job[16];
	pthread_t th[16];
	int started[16];
	int idr, qp, i, tries, W, H;
	size_t o = 0;
	n2avc_frame *rec;

	if (!e || !pic || !out || !pic->plane[0] || !pic->plane[1] || pic->format > N2AVC_FMT_I420 ||
	    (pic->format == N2AVC_FMT_I420 && !pic->plane[2]))
		return N2AVC_E_ARGUMENT;
	if (pic->stride[0] < e->cfg.width || pic->stride[1] < (pic->format == N2AVC_FMT_NV12 ? e->cfg.width : e->cfg.width / 2) ||
	    (pic->format == N2AVC_FMT_I420 && pic->stride[2] < e->cfg.width / 2))
		return N2AVC_E_ARGUMENT;
	if (e->dead)
		return N2AVC_E_STATE;
	W = e->mb_w * 16; H = e->mb_h * 16;
	idr = !e->have_ref || force_idr || e->since_idr >= e->cfg.keyint || (e->cfg.flags & N2AVC_F_INTRA_ONLY) || e->cfg.keyint == 1;
	if (idr)
		e->frame_num = 0;
	load_source(e, pic);
	qp = n2avc_rc_picture_qp(e->rc, idr);
	if (qp < 10) qp = 10;
	/* c9: with a bitrate target the rate control used to run down to QP 10 on easy or dark scenes (phone: 75 kB per picture of
	 * a black room) -- invisible quality for a lot of processor time and heat. Under QP 20 nothing is gained that the eye sees. */
	if (e->cfg.bitrate != 0 && qp < N2AVC_MIN_RC_QP) qp = N2AVC_MIN_RC_QP;
	if (qp > 51) qp = 51;
	rec = &e->fr[e->cur];

	for (tries = 0;; tries++) {
		int over = 0;
		for (i = 0; i < e->nslices; i++) {
			n2avc_slice *s = &e->sl[i];
			memset(s, 0, sizeof *s);
			n2bw_init(&s->bw, e->slbuf[i], e->slcap);
			s->slice_type = idr ? 2 : 0;
			s->qp = qp; s->qpc = QPC[qp];
			s->mb_w = e->mb_w; s->mb_h = e->mb_h;
			s->first_row = e->mb_h * i / e->nslices;
			s->end_row = e->mb_h * (i + 1) / e->nslices;
			s->src = &e->fr[0]; s->rec = rec; s->ref = idr ? NULL : &e->fr[3 - e->cur];
			s->mb = e->mb;
			s->search_range = (int)e->cfg.search_range;
			job[i].e = e; job[i].s = s; job[i].idr = idr; job[i].bytes = 0;
		}
		for (i = 0; i < 16; i++)
			started[i] = 0;
		{
			struct slice_queue q;
			q.job = job; q.count = e->nslices; q.next = 0;
			pthread_mutex_init(&q.lock, NULL);
			for (i = 1; i < e->nthreads; i++)
				started[i] = pthread_create(&th[i], NULL, slice_worker, &q) == 0;
			slice_worker(&q);               /* this thread works too, and does everything if no thread could be made */
			for (i = 1; i < e->nthreads; i++)
				if (started[i])
					pthread_join(th[i], NULL);
			pthread_mutex_destroy(&q.lock);
		}
		for (i = 0; i < e->nslices; i++)
			over |= e->sl[i].bw.overflow;
		if (!over)
			break;
		if (qp >= 51 || tries >= 6) {
			e->dead = 1;
			return N2AVC_E_OVERFLOW;
		}
		qp = qp + 6 > 51 ? 51 : qp + 6;         /* did not fit: the same picture again, coarser */
	}

	/* the packet */
#define PUT_NAL(src, n, ref, type, raw) do { \
		size_t hdr = 4, k; \
		if (o + hdr + 1 > e->outcap) { e->dead = 1; return N2AVC_E_OVERFLOW; } \
		if (raw) { if (o + hdr + (n) > e->outcap) { e->dead = 1; return N2AVC_E_OVERFLOW; } memcpy(e->out + o + hdr, (src), (n)); k = (n); } \
		else k = n2avc_nal(e->out + o + hdr, e->outcap - o - hdr, (src), (n), (ref), (type)); \
		if (k == 0) { e->dead = 1; return N2AVC_E_OVERFLOW; } \
		if (e->cfg.flags & N2AVC_F_ANNEXB) { e->out[o] = 0; e->out[o + 1] = 0; e->out[o + 2] = 0; e->out[o + 3] = 1; } \
		else { e->out[o] = (uint8_t)(k >> 24); e->out[o + 1] = (uint8_t)(k >> 16); e->out[o + 2] = (uint8_t)(k >> 8); e->out[o + 3] = (uint8_t)k; } \
		o += hdr + k; } while (0)
	if (idr && (e->cfg.flags & N2AVC_F_HEADERS_IN_KEYFRAMES)) {
		PUT_NAL(e->sps, e->sps_n, 3, 7, 1);
		PUT_NAL(e->pps, e->pps_n, 3, 8, 1);
	}
	for (i = 0; i < e->nslices; i++)
		PUT_NAL(e->slbuf[i], job[i].bytes, idr ? 3 : 2, idr ? 5 : 1, 0);
#undef PUT_NAL

	if (e->cfg.flags & N2AVC_F_DEBLOCK)
		n2avc_deblock_picture(rec, e->mb, e->mb_w, e->mb_h);
	extend_plane(&rec->y, W, H, N2AVC_PAD);
	extend_plane(&rec->cb, W / 2, H / 2, N2AVC_PAD / 2);
	extend_plane(&rec->cr, W / 2, H / 2, N2AVC_PAD / 2);
	n2avc_rc_picture_done(e->rc, idr, qp, o);

	out->data = e->out; out->size = o; out->pts = pic->pts;
	out->keyframe = (uint32_t)idr; out->qp = (uint32_t)qp; out->frame_num = e->frame_num;
	e->cur = 3 - e->cur;
	e->have_ref = 1;
	e->frame_num = (e->frame_num + 1) & 255;
	e->since_idr = idr ? 1 : e->since_idr + 1;
	if (idr)
		e->idr_id++;
	return N2AVC_OK;
}
