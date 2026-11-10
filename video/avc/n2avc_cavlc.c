/* SPDX-License-Identifier: MIT */
/*
 * n2avc_cavlc.c -- CAVLC residual coding of the WP03 H.264 encoder (clause 7.3.5.3.2 and clause 9.2).
 *
 * Written from ITU-T H.264 (03/2010): 9.2.1 (coeff_token and nC), 9.2.2 (levels, suffixLength adaptation),
 * 9.2.3 (total_zeros, run_before), 9.2.4 (placement of the coefficients).
 */
#include "n2avc_priv.h"
#include "n2avc_tables.h"

/* Number of bits needed to code v >= 1: floor(log2(v)). */
static int ilog2(int v)
{
	int n = 0;
	while (v > 1) {
		v >>= 1;
		n++;
	}
	return n;
}

/* level_prefix and level_suffix of clause 9.2.2 for one levelCode (which is what the decoder reconstructs before
 * the TrailingOnes bias and the even/odd mapping); suffixLength is the current state. */
static void write_level_code(n2bw *w, int code, int sl)
{
	if (sl == 0) {
		if (code < 14) {
			n2bw_u(w, code, 0);
			n2bw_u(w, 1, 1);
		} else if (code < 30) {
			n2bw_u(w, 14, 0);
			n2bw_u(w, 1, 1);
			n2bw_u(w, 4, (uint32_t)(code - 14));
		} else {
			int v = code - 30 + 4096;
			int bits = ilog2(v);
			n2bw_u(w, bits + 3, 0);                 /* level_prefix = bits + 3 zeros, then the 1 */
			n2bw_u(w, 1, 1);
			n2bw_u(w, bits, (uint32_t)(v - (1 << bits)));
		}
	} else {
		int base = 15 << sl;
		if (code < base) {
			n2bw_u(w, code >> sl, 0);
			n2bw_u(w, 1, 1);
			n2bw_u(w, sl, (uint32_t)(code & ((1 << sl) - 1)));
		} else if (code < base + 4096) {
			n2bw_u(w, 15, 0);
			n2bw_u(w, 1, 1);
			n2bw_u(w, 12, (uint32_t)(code - base));
		} else {
			int v = code - base + 4096;
			int bits = ilog2(v);
			n2bw_u(w, bits + 3, 0);
			n2bw_u(w, 1, 1);
			n2bw_u(w, bits, (uint32_t)(v - (1 << bits)));
		}
	}
}

void n2avc_write_residual(n2bw *w, const int16_t *coef, int max_num_coeff, int start_idx, int nC)
{
	int pos[16], lvl[16];
	int n = 0, i, k, trailing = 0, num_pos, suffix_len;
	int cls = n2avc_nc_class(nC);

	num_pos = max_num_coeff - start_idx;            /* scanned positions: 16, 15, 8 or 4 */
	if (num_pos > 16)
		num_pos = 16;
	for (k = 0; k < num_pos; k++) {
		int v = coef[start_idx + k];
		if (v) {
			pos[n] = k;                             /* position relative to start_idx */
			lvl[n] = v;
			n++;
		}
	}
	/* Trailing ones: the last 0..3 coefficients of the scan, all +-1 (clause 9.2.2). */
	for (i = n - 1; i >= 0 && trailing < 3; i--) {
		if (lvl[i] == 1 || lvl[i] == -1)
			trailing++;
		else
			break;
	}
	n2avc_put_vlc(w, &n2avc_cw_coeff_token[cls][n][trailing]);
	if (n == 0)
		return;

	suffix_len = (n > 10 && trailing < 3) ? 1 : 0;  /* clause 9.2.2, initial suffixLength */
	for (i = n - 1; i >= 0; i--) {
		int level = lvl[i];
		int code;
		if (i >= n - trailing) {
			n2bw_u(w, 1, level < 0 ? 1u : 0u);      /* trailing_ones_sign_flag */
			continue;
		}
		code = (level > 0) ? (2 * level - 2) : (-2 * level - 1);
		if (i == n - 1 - trailing && trailing < 3)
			code -= 2;                              /* clause 9.2.2 step 7, inverse */
		write_level_code(w, code, suffix_len);
		/* Steps 9 and 10 of clause 9.2.2: the decoder's state machine, to stay in step with it. */
		if (suffix_len == 0)
			suffix_len = 1;
		{
			int a = level < 0 ? -level : level;
			if (a > (3 << (suffix_len - 1)) && suffix_len < 6)
				suffix_len++;
		}
	}

	{
		/* total_zeros counts the zero positions from the start of the scan up to and including the
		 * highest-frequency coefficient (clause 9.2.3 with 9.2.4: the last run, the one that is not coded,
		 * is the number of zeros below the lowest-frequency coefficient).  It is not the number of zeros of
		 * the whole block: with a single coefficient at position 0 the total_zeros is 0. */
		int total_zeros = pos[n - 1] + 1 - n;
		if (n < num_pos) {
			if (max_num_coeff == 4)
				n2avc_put_vlc(w, &n2avc_cw_total_zeros_dc[n][total_zeros]);
			else
				n2avc_put_vlc(w, &n2avc_cw_total_zeros[n][total_zeros]);
		}
		if (n > 1) {
			int zeros_left = total_zeros;
			/* Clause 9.2.3/9.2.4: the runs are transmitted in the order of the levels, from the
			 * highest-frequency coefficient down; run[i] is the gap just below level[i], and run[n-1]
			 * (the zeros below the lowest coefficient) is implied. */
			for (i = 0; i < n - 1; i++) {
				int run = pos[n - 1 - i] - pos[n - 2 - i] - 1;
				if (zeros_left > 0) {
					int zl = zeros_left > 6 ? 7 : zeros_left;
					n2avc_put_vlc(w, &n2avc_cw_run_before[run][zl]);
					zeros_left -= run;
				}
			}
		}
	}
	/* run[n - 1] = zeros_left is implied (clause 9.2.3) */
}

int n2avc_block_nc(const n2avc_slice *s, int mbx, int mby, int blk_idx)
{
	int nA = 0, nB = 0, availA = 0, availB = 0;

	if (blk_idx < 16) {
		int x4 = blk_idx & 3, y4 = blk_idx >> 2;
		if (x4 > 0) {
			availA = 1;
			nA = s->mb[mby * s->mb_w + mbx].nnz[blk_idx - 1];
		} else if (n2avc_avail(s, mbx - 1, mby)) {
			availA = 1;
			nA = s->mb[mby * s->mb_w + mbx - 1].nnz[blk_idx + 3];
		}
		if (y4 > 0) {
			availB = 1;
			nB = s->mb[mby * s->mb_w + mbx].nnz[blk_idx - 4];
		} else if (n2avc_avail(s, mbx, mby - 1)) {
			availB = 1;
			nB = s->mb[(mby - 1) * s->mb_w + mbx].nnz[blk_idx + 12];
		}
	} else {
		int base = (blk_idx < 20) ? 16 : 20;
		int i = blk_idx - base, cx = i & 1, cy = i >> 1;
		if (cx > 0) {
			availA = 1;
			nA = s->mb[mby * s->mb_w + mbx].nnz[blk_idx - 1];
		} else if (n2avc_avail(s, mbx - 1, mby)) {
			availA = 1;
			nA = s->mb[mby * s->mb_w + mbx - 1].nnz[base + 1 + 2 * cy];
		}
		if (cy > 0) {
			availB = 1;
			nB = s->mb[mby * s->mb_w + mbx].nnz[blk_idx - 2];
		} else if (n2avc_avail(s, mbx, mby - 1)) {
			availB = 1;
			nB = s->mb[(mby - 1) * s->mb_w + mbx].nnz[base + 2 + cx];
		}
	}
	if (availA && availB)
		return (nA + nB + 1) >> 1;
	if (availA)
		return nA;
	if (availB)
		return nB;
	return 0;
}
