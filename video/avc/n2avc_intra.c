/* SPDX-License-Identifier: MIT */
/*
 * n2avc_intra.c -- Intra_16x16 and chroma intra prediction and the least-SAD mode decision of the WP03
 * H.264 encoder.
 *
 * ITU-T H.264 (03/2010): 8.3.3 (Intra_16x16 prediction, all four modes and their availability rules), 8.3.4
 * (chroma prediction, including the per-4x4-block DC rules of 8.3.4.1), 6.4.9 (availability of neighbouring
 * macroblocks; here a macroblock of another slice of the same picture counts as not available, as n2avc_avail()
 * implements).
 */
#include "n2avc_priv.h"

#include <string.h>

static int above_ok(const n2avc_slice *s, int mbx, int mby) { return n2avc_avail(s, mbx, mby - 1); }
static int left_ok(const n2avc_slice *s, int mbx, int mby) { return n2avc_avail(s, mbx - 1, mby); }

int n2avc_luma16_mode_available(int mode, const n2avc_slice *s, int mbx, int mby)
{
	switch (mode) {
	case 0: return above_ok(s, mbx, mby);                  /* Intra_16x16_Vertical */
	case 1: return left_ok(s, mbx, mby);                   /* Intra_16x16_Horizontal */
	case 2: return 1;                                      /* Intra_16x16_DC */
	case 3: return above_ok(s, mbx, mby) && left_ok(s, mbx, mby);  /* Intra_16x16_Plane */
	default: return 0;
	}
}

int n2avc_chroma_mode_available(int mode, const n2avc_slice *s, int mbx, int mby)
{
	switch (mode) {
	case 0: return 1;                                      /* Intra_Chroma_DC */
	case 1: return left_ok(s, mbx, mby);                   /* Intra_Chroma_Horizontal */
	case 2: return above_ok(s, mbx, mby);                  /* Intra_Chroma_Vertical */
	case 3: return above_ok(s, mbx, mby) && left_ok(s, mbx, mby);  /* Intra_Chroma_Plane */
	default: return 0;
	}
}

void n2avc_pred_luma16(int mode, const n2avc_slice *s, int mbx, int mby, uint8_t *pred)
{
	const n2avc_plane *r = &s->rec->y;
	const int stride = r->stride;
	const uint8_t *p = r->p + (size_t)(mby * 16) * (size_t)stride + (size_t)(mbx * 16);
	int x, y;

	switch (mode) {
	case 0:                                                 /* 8.3.3.1 */
		for (y = 0; y < 16; y++)
			for (x = 0; x < 16; x++)
				pred[y * 16 + x] = p[x - stride];
		break;
	case 1:                                                 /* 8.3.3.2 */
		for (y = 0; y < 16; y++)
			for (x = 0; x < 16; x++)
				pred[y * 16 + x] = p[y * stride - 1];
		break;
	case 2: {                                               /* 8.3.3.3 */
		int sum = 0, v;
		int above = above_ok(s, mbx, mby), left = left_ok(s, mbx, mby);
		if (above)
			for (x = 0; x < 16; x++)
				sum += p[x - stride];
		if (left)
			for (y = 0; y < 16; y++)
				sum += p[y * stride - 1];
		if (above && left)
			v = (sum + 16) >> 5;
		else if (above || left)
			v = (sum + 8) >> 4;
		else
			v = 128;                                /* 1 << (BitDepthY - 1) */
		for (y = 0; y < 256; y++)
			pred[y] = (uint8_t)v;
		break;
	}
	default: {                                              /* 8.3.3.4, Intra_16x16_Plane */
		int H = 0, V = 0, a, b, c;
		for (x = 0; x < 8; x++)
			H += (x + 1) * ((int)p[8 + x - stride] - (int)p[6 - x - stride]);
		for (y = 0; y < 8; y++)
			V += (y + 1) * ((int)p[(8 + y) * stride - 1] - (int)p[(6 - y) * stride - 1]);
		a = 16 * ((int)p[-1 + 15 * stride] + (int)p[15 - stride]);
		b = (5 * H + 32) >> 6;
		c = (5 * V + 32) >> 6;
		for (y = 0; y < 16; y++)
			for (x = 0; x < 16; x++)
				pred[y * 16 + x] = (uint8_t)n2avc_clip1((a + b * (x - 7) + c * (y - 7) + 16) >> 5);
		break;
	}
	}
}

void n2avc_pred_chroma(int mode, const n2avc_slice *s, int mbx, int mby, int comp, uint8_t *pred)
{
	const n2avc_plane *r = comp ? &s->rec->cr : &s->rec->cb;
	const int stride = r->stride;
	const uint8_t *p = r->p + (size_t)(mby * 8) * (size_t)stride + (size_t)(mbx * 8);
	int x, y, xo, yo;
	int above = above_ok(s, mbx, mby), left = left_ok(s, mbx, mby);

	switch (mode) {
	case 1:                                                 /* 8.3.4.2, Intra_Chroma_Horizontal */
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				pred[y * 8 + x] = p[y * stride - 1];
		break;
	case 2:                                                 /* 8.3.4.3, Intra_Chroma_Vertical */
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				pred[y * 8 + x] = p[x - stride];
		break;
	case 3: {                                               /* 8.3.4.4, Intra_Chroma_Plane */
		int H = 0, V = 0, a, b, c;
		for (x = 0; x < 4; x++)
			H += (x + 1) * ((int)p[4 + x - stride] - (int)p[2 - x - stride]);
		for (y = 0; y < 4; y++)
			V += (y + 1) * ((int)p[(4 + y) * stride - 1] - (int)p[(2 - y) * stride - 1]);
		a = 16 * ((int)p[-1 + 7 * stride] + (int)p[7 - stride]);
		b = (34 * H + 32) >> 6;                 /* ChromaArrayType != 3: factor 34 */
		c = (34 * V + 32) >> 6;
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				pred[y * 8 + x] = (uint8_t)n2avc_clip1((a + b * (x - 3) + c * (y - 3) + 16) >> 5);
		break;
	}
	default:                                                /* 8.3.4.1, Intra_Chroma_DC, per 4x4 block */
		for (yo = 0; yo < 8; yo += 4)
			for (xo = 0; xo < 8; xo += 4) {
				int sumA = 0, sumL = 0, v;
				if (above)
					for (x = 0; x < 4; x++)
						sumA += p[xo + x - stride];
				if (left)
					for (y = 0; y < 4; y++)
						sumL += p[(yo + y) * stride - 1];
				if (xo == 0 && yo == 0) {
					if (above && left)
						v = (sumA + sumL + 4) >> 3;
					else if (above)
						v = (sumA + 2) >> 2;
					else if (left)
						v = (sumL + 2) >> 2;
					else
						v = 128;
				} else if (yo == 0) {           /* xO > 0, yO == 0: above preferred */
					if (above)
						v = (sumA + 2) >> 2;
					else if (left)
						v = (sumL + 2) >> 2;
					else
						v = 128;
				} else if (xo == 0) {           /* xO == 0, yO > 0: left preferred */
					if (left)
						v = (sumL + 2) >> 2;
					else if (above)
						v = (sumA + 2) >> 2;
					else
						v = 128;
				} else {                        /* both > 0 */
					if (above && left)
						v = (sumA + sumL + 4) >> 3;
					else if (above)
						v = (sumA + 2) >> 2;
					else if (left)
						v = (sumL + 2) >> 2;
					else
						v = 128;
				}
				for (y = 0; y < 4; y++)
					for (x = 0; x < 4; x++)
						pred[(yo + y) * 8 + xo + x] = (uint8_t)v;
			}
		break;
	}
}

int n2avc_intra_pick_luma(const n2avc_slice *s, int mbx, int mby, uint8_t *pred, int *mode_out)
{
	const int ss = s->src->y.stride;
	const uint8_t *src = s->src->y.p + (size_t)(mby * 16) * (size_t)ss + (size_t)(mbx * 16);
	uint8_t tmp[256];
	int mode, best = 2, best_sad = 0x7fffffff;

	for (mode = 0; mode < 4; mode++) {
		int sad;
		if (!n2avc_luma16_mode_available(mode, s, mbx, mby))
			continue;
		n2avc_pred_luma16(mode, s, mbx, mby, tmp);
		sad = n2avc_sad(src, ss, tmp, 16, 16, 16);
		if (sad < best_sad) {
			best_sad = sad;
			best = mode;
			memcpy(pred, tmp, sizeof tmp);
		}
	}
	*mode_out = best;
	return best_sad;
}

int n2avc_intra_pick_chroma(const n2avc_slice *s, int mbx, int mby, uint8_t *pcb, uint8_t *pcr, int *mode_out)
{
	const int scb = s->src->cb.stride, scr = s->src->cr.stride;
	const uint8_t *scbp = s->src->cb.p + (size_t)(mby * 8) * (size_t)scb + (size_t)(mbx * 8);
	const uint8_t *scrp = s->src->cr.p + (size_t)(mby * 8) * (size_t)scr + (size_t)(mbx * 8);
	uint8_t tcb[64], tcr[64];
	int mode, best = 0, best_sad = 0x7fffffff;

	for (mode = 0; mode < 4; mode++) {
		int sad;
		if (!n2avc_chroma_mode_available(mode, s, mbx, mby))
			continue;
		n2avc_pred_chroma(mode, s, mbx, mby, 0, tcb);
		n2avc_pred_chroma(mode, s, mbx, mby, 1, tcr);
		sad = n2avc_sad(scbp, scb, tcb, 8, 8, 8) + n2avc_sad(scrp, scr, tcr, 8, 8, 8);
		if (sad < best_sad) {
			best_sad = sad;
			best = mode;
			memcpy(pcb, tcb, sizeof tcb);
			memcpy(pcr, tcr, sizeof tcr);
		}
	}
	*mode_out = best;
	return best_sad;
}
