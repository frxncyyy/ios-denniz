/* SPDX-License-Identifier: MIT */
/*
 * n2avc_xform.c -- forward transform, quantisation, inverse quantisation, inverse transform and reconstruction
 * of the WP03 H.264 encoder (4:2:0, 8 bit, flat scaling lists).
 *
 * ITU-T H.264 (03/2010): clause 8.5.1/8.5.2 (4x4 residual), 8.5.6 (zig-zag scan, table 8-13), 8.5.9 (scaling
 * functions, normAdjust4x4), 8.5.10 (Intra_16x16 DC), 8.5.11 (chroma DC), 8.5.12 (inverse scaling and transform),
 * 8.5.8/table 8-15 (QP_C).  The forward direction is not normative; the multiplication factors are the standard's
 * (the same numbers as the derivation a^2, ab/2, b^2/4 scaled by 2^qbits, see the table below), the rounding
 * offsets are 1/3 (intra) and 1/6 (inter) of the quantisation step.
 */
#include "n2avc_priv.h"

/* Zig-zag scan, table 8-13: scan index -> (x, y) inside the 4x4 block, and its inverse. */
const uint8_t n2avc_zig_x[16] = { 0, 1, 0, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 3, 2, 3 };
const uint8_t n2avc_zig_y[16] = { 0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3, 2, 1, 2, 3, 3 };


/* Multiplication factors MF of clause 8.5.2 for the three position classes, per QP % 6:
 *   class 0: (i%2, j%2) == (0,0); class 1: (1,1); class 2: the other positions. */
static const uint16_t mf4x4[6][3] = {
	{ 13107, 5243, 8066 },
	{ 11916, 4660, 7490 },
	{ 10082, 4194, 6554 },
	{  9362, 3647, 5825 },
	{  8192, 3355, 5243 },
	{  7282, 2893, 4559 }
};

/* normAdjust4x4 of clause 8.5.9 (equation 8-318), same class order. */
static const uint8_t norm4x4[6][3] = {
	{ 10, 16, 13 },
	{ 11, 18, 14 },
	{ 13, 20, 16 },
	{ 14, 23, 18 },
	{ 16, 25, 20 },
	{ 18, 29, 23 }
};

/* Table 8-15 with chroma_qp_index_offset 0. */
int n2avc_qpc(int qp)
{
	static const uint8_t tab[22] = {
		29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36,
		36, 37, 37, 37, 38, 38, 38, 39, 39, 39, 39
	};
	if (qp < 0)
		qp = 0;
	if (qp < 30)
		return qp;
	if (qp > 51)
		qp = 51;
	return tab[qp - 30];
}

static int cls4(int y, int x)
{
	int a = y & 1, b = x & 1;
	return (a && b) ? 1 : ((a || b) ? 2 : 0);
}

/* ---------------------------------------------------------------- forward */
/* One 1-D forward transform (the butterfly of the standard's analysis, encoder side). */
static void fwd1d(const int *x, int *y)
{
	int e0 = x[0] + x[3];
	int e1 = x[1] + x[2];
	int e2 = x[1] - x[2];
	int e3 = x[0] - x[3];
	y[0] = e0 + e1;
	y[1] = 2 * e3 + e2;
	y[2] = e0 - e1;
	y[3] = e3 - 2 * e2;
}

void n2avc_fwd4x4(const uint8_t *src, int ss, const uint8_t *pred, int ps, int16_t W[16])
{
	int d[16], g[16], t[4], u[4];
	int i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			d[i * 4 + j] = (int)src[i * ss + j] - (int)pred[i * ps + j];
	for (i = 0; i < 4; i++) {                       /* rows */
		t[0] = d[i * 4 + 0]; t[1] = d[i * 4 + 1]; t[2] = d[i * 4 + 2]; t[3] = d[i * 4 + 3];
		fwd1d(t, u);
		g[i * 4 + 0] = u[0]; g[i * 4 + 1] = u[1]; g[i * 4 + 2] = u[2]; g[i * 4 + 3] = u[3];
	}
	for (j = 0; j < 4; j++) {                       /* columns */
		t[0] = g[0 * 4 + j]; t[1] = g[1 * 4 + j]; t[2] = g[2 * 4 + j]; t[3] = g[3 * 4 + j];
		fwd1d(t, u);
		W[0 * 4 + j] = (int16_t)u[0];
		W[1 * 4 + j] = (int16_t)u[1];
		W[2 * 4 + j] = (int16_t)u[2];
		W[3 * 4 + j] = (int16_t)u[3];
	}
}

static int quant_one(int w, int mf, int shift, int f)
{
	int a = w < 0 ? -w : w;
	int level = (int)(((int64_t)a * mf + f) >> shift);
	return w < 0 ? -level : level;
}

static int quant4x4_common(const int16_t W[16], int qp, int intra, int16_t zz[16], int skip_dc)
{
	int m = qp % 6;
	int shift = 15 + qp / 6;
	int f = (int)((1u << shift) / (intra ? 3u : 6u));
	int k, nz = 0;

	for (k = 0; k < 16; k++) {
		int x = n2avc_zig_x[k], y = n2avc_zig_y[k];
		int lvl;
		if (skip_dc && k == 0) {
			zz[0] = 0;
			continue;
		}
		lvl = quant_one(W[y * 4 + x], mf4x4[m][cls4(y, x)], shift, f);
		if (lvl > 32767)
			lvl = 32767;
		else if (lvl < -32768)
			lvl = -32768;
		zz[k] = (int16_t)lvl;
		if (lvl)
			nz++;
	}
	return nz;
}

int n2avc_quant4x4(const int16_t W[16], int qp, int intra, int16_t zz[16])
{
	return quant4x4_common(W, qp, intra, zz, 0);
}

int n2avc_quant4x4_ac(const int16_t W[16], int qp, int intra, int16_t zz[16])
{
	return quant4x4_common(W, qp, intra, zz, 1);
}

/* --------------------------------------------------------------- inverse */
static int dequant_one(int level, int m, int cls, int qp)
{
	int ls = 16 * norm4x4[m][cls];
	int q6 = qp / 6;

	if (qp >= 24)
		return (int)(level * ls * (1 << (q6 - 4)));
	return (int)((level * ls + (1 << (3 - q6))) >> (4 - q6));
}

/* Inverse transform of clause 8.5.12.2 on the scaled 4x4 array d (raster), output residual r (raster). */
static void inv_transform(const int d[16], int r[16])
{
	int f[16], g[4], e[4], h[4];
	int i, j;

	for (i = 0; i < 4; i++) {                       /* horizontal pass */
		const int *p = d + i * 4;
		e[0] = p[0] + p[2];
		e[1] = p[0] - p[2];
		e[2] = n2avc_shr(p[1], 1) - p[3];
		e[3] = p[1] + n2avc_shr(p[3], 1);
		f[i * 4 + 0] = e[0] + e[3];
		f[i * 4 + 1] = e[1] + e[2];
		f[i * 4 + 2] = e[1] - e[2];
		f[i * 4 + 3] = e[0] - e[3];
	}
	for (j = 0; j < 4; j++) {                       /* vertical pass */
		g[0] = f[0 * 4 + j] + f[2 * 4 + j];
		g[1] = f[0 * 4 + j] - f[2 * 4 + j];
		g[2] = n2avc_shr(f[1 * 4 + j], 1) - f[3 * 4 + j];
		g[3] = f[1 * 4 + j] + n2avc_shr(f[3 * 4 + j], 1);
		h[0] = g[0] + g[3];
		h[1] = g[1] + g[2];
		h[2] = g[1] - g[2];
		h[3] = g[0] - g[3];
		for (i = 0; i < 4; i++)
			r[i * 4 + j] = n2avc_shr(h[i] + 32, 6);
	}
}

static void reconstruct(const int16_t zz[16], int qp, int dc, int have_dc,
			const uint8_t *pred, int ps, uint8_t *rec, int rs)
{
	int d[16], r[16];
	int m = qp % 6, i, j;

	for (i = 0; i < 16; i++)
		d[i] = 0;
	for (i = 0; i < 16; i++) {
		int x = n2avc_zig_x[i], y = n2avc_zig_y[i];
		d[y * 4 + x] = dequant_one(zz[i], m, cls4(y, x), qp);
	}
	if (have_dc)                    /* 8.5.12.1: d00 is the already scaled DC of the DC block (8.5.2/8.5.4) */
		d[0] = dc;
	inv_transform(d, r);
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			rec[i * rs + j] = (uint8_t)n2avc_clip1((int)pred[i * ps + j] + r[i * 4 + j]);
}

void n2avc_inv4x4(const int16_t zz[16], int qp, const uint8_t *pred, int ps, uint8_t *rec, int rs)
{
	reconstruct(zz, qp, 0, 0, pred, ps, rec, rs);
}

void n2avc_inv4x4_dc(const int16_t zz[16], int dc, int qp, const uint8_t *pred, int ps, uint8_t *rec, int rs)
{
	reconstruct(zz, qp, dc, 1, pred, ps, rec, rs);
}

/* ------------------------------------------------------------ DC blocks */
/* 4x4 Hadamard with the matrix of equation 8-324 (symmetric: the same kernel both ways). */
static void hadamard16(const int32_t *in, int32_t *out)
{
	int32_t t[16];
	int i, j;

	for (i = 0; i < 4; i++) {
		t[i * 4 + 0] = in[i * 4 + 0] + in[i * 4 + 1] + in[i * 4 + 2] + in[i * 4 + 3];
		t[i * 4 + 1] = in[i * 4 + 0] + in[i * 4 + 1] - in[i * 4 + 2] - in[i * 4 + 3];
		t[i * 4 + 2] = in[i * 4 + 0] - in[i * 4 + 1] - in[i * 4 + 2] + in[i * 4 + 3];
		t[i * 4 + 3] = in[i * 4 + 0] - in[i * 4 + 1] + in[i * 4 + 2] - in[i * 4 + 3];
	}
	for (j = 0; j < 4; j++)
		for (i = 0; i < 4; i++) {
			int32_t a = t[0 * 4 + j], b = t[1 * 4 + j], c = t[2 * 4 + j], d = t[3 * 4 + j];
			switch (i) {
			case 0: out[i * 4 + j] = a + b + c + d; break;
			case 1: out[i * 4 + j] = a + b - c - d; break;
			case 2: out[i * 4 + j] = a - b - c + d; break;
			default: out[i * 4 + j] = a - b + c - d; break;
			}
		}
}

void n2avc_fwd_dc16(const int16_t dc[16], int32_t W[16])
{
	int32_t in[16];
	int i;
	for (i = 0; i < 16; i++)
		in[i] = dc[i];
	hadamard16(in, W);
}

int n2avc_quant_dc16(const int32_t W[16], int qp, int intra, int16_t zz[16])
{
	int m = qp % 6;
	/* The inverse path is dcY = (f * LevelScale(0,0) + 2^(5-q6)) >> (6-q6) with f = H c H (gain 16) and the
	 * 4x4 inverse transform's final >> 6, so the quantiser must satisfy MF / 2^shift = 0.1 at QP % 6 == 0:
	 * shift = 15 + qP/6 + 2 (equation 8-326 with the Hadamard's gain of 16). */
	int shift = 15 + qp / 6 + 2;
	int f = (int)((1u << shift) / (intra ? 3u : 6u));
	uint32_t mf = mf4x4[m][0];
	int k, nz = 0;

	for (k = 0; k < 16; k++) {
		int x = n2avc_zig_x[k], y = n2avc_zig_y[k];
		int32_t w = W[y * 4 + x];
		int64_t a = w < 0 ? -(int64_t)w : (int64_t)w;
		int lvl = (int)((a * mf + (int64_t)f) >> shift);
		if (w < 0)
			lvl = -lvl;
		if (lvl > 32767)
			lvl = 32767;
		else if (lvl < -32768)
			lvl = -32768;
		zz[k] = (int16_t)lvl;
		if (lvl)
			nz++;
	}
	return nz;
}

void n2avc_inv_dc16(const int16_t zz[16], int qp, int dc[16])
{
	int32_t c[16], f[16];
	int m = qp % 6;
	int ls = 16 * norm4x4[m][0];
	int q6 = qp / 6;
	int i;

	for (i = 0; i < 16; i++) {
		int x = n2avc_zig_x[i], y = n2avc_zig_y[i];
		c[y * 4 + x] = zz[i];           /* inverse scan of clause 8.5.6 */
	}
	hadamard16(c, f);
	for (i = 0; i < 16; i++) {
		int32_t v;
		if (qp >= 36)
			v = (int32_t)((int64_t)f[i] * ls * (1LL << (q6 - 6)));
		else
			v = (int32_t)(((int64_t)f[i] * ls + (1 << (5 - q6))) >> (6 - q6));
		dc[i] = (int)v;
	}
}

/* 2x2 Hadamard of equation 8-328. */
void n2avc_fwd_dc4(const int16_t dc[4], int16_t W[4])
{
	int c0 = dc[0], c1 = dc[1], c2 = dc[2], c3 = dc[3];
	W[0] = (int16_t)(c0 + c1 + c2 + c3);
	W[1] = (int16_t)(c0 - c1 + c2 - c3);
	W[2] = (int16_t)(c0 + c1 - c2 - c3);
	W[3] = (int16_t)(c0 - c1 - c2 + c3);
}

int n2avc_quant_dc4(const int16_t W[4], int qp, int intra, int16_t lvl[4])
{
	int m = qp % 6;
	/* Equation 8-330: dcC = ((f * LevelScale(0,0)) << (qP/6)) >> 5 with f = H2 c H2 (gain 4), so
	 * MF / 2^shift = 1 / (2 * LevelScale(0,0)) at QP % 6 == 0: shift = 15 + qP/6 + 1. */
	int shift = 15 + qp / 6 + 1;
	int f = (int)((1u << shift) / (intra ? 3u : 6u));
	uint32_t mf = mf4x4[m][0];
	int k, nz = 0;

	for (k = 0; k < 4; k++) {
		int w = W[k];
		int a = w < 0 ? -w : w;
		int v = (int)(((int64_t)a * mf + f) >> shift);
		if (w < 0)
			v = -v;
		lvl[k] = (int16_t)v;
		if (v)
			nz++;
	}
	return nz;
}

/* floor(v / 2^n) for a 64-bit value, n = 0..31 */
static long long shr64(long long v, int n)
{
	if (n <= 0)
		return v;
	if (v >= 0)
		return v >> n;
	return -(((-v) + ((1LL << n) - 1)) >> n);
}

void n2avc_inv_dc4(const int16_t lvl[4], int qp, int dc[4])
{
	int c0 = lvl[0], c1 = lvl[1], c2 = lvl[2], c3 = lvl[3];
	int f0 = c0 + c1 + c2 + c3;
	int f1 = c0 - c1 + c2 - c3;
	int f2 = c0 + c1 - c2 - c3;
	int f3 = c0 - c1 - c2 + c3;
	int m = qp % 6;
	int ls = 16 * norm4x4[m][0];
	int q6 = qp / 6;

	/* equation 8-330: dcC = (( f * LevelScale4x4( qP%6, 0, 0 ) ) << ( qP/6 )) >> 5; the shift is written as a
	 * multiplication because shifting a negative value left is undefined in C. */
	dc[0] = (int)shr64((long long)f0 * ls * (1LL << q6), 5);
	dc[1] = (int)shr64((long long)f1 * ls * (1LL << q6), 5);
	dc[2] = (int)shr64((long long)f2 * ls * (1LL << q6), 5);
	dc[3] = (int)shr64((long long)f3 * ls * (1LL << q6), 5);
}

int n2avc_sad(const uint8_t *a, int as, const uint8_t *b, int bs, int w, int h)
{
	int x, y, s = 0;
	for (y = 0; y < h; y++) {
		const uint8_t *pa = a + y * as, *pb = b + y * bs;
		for (x = 0; x < w; x++)
			s += pa[x] > pb[x] ? pa[x] - pb[x] : pb[x] - pa[x];
	}
	return s;
}
