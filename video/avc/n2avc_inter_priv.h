/*
 * n2avc_inter_priv.h -- the private seam between the three WP04 files
 * (n2avc_me.c: motion search, n2avc_inter.c: inter macroblock / prediction /
 * decision, n2avc_rc.c: rate control).  Nothing here is used by WP03 or by
 * the VideoToolbox glue; the only public seam remains include/n2avc_int.h.
 *
 * The prediction helpers are declared here so that the host tests can drive
 * them with literal expectations (a 2x2 chroma checker, a luma copy, the
 * motion-vector predictor and the P_Skip predictor) without duplicating the
 * code under test.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2AVC_INTER_PRIV_H
#define N2AVC_INTER_PRIV_H

#include <stdint.h>

#include "n2avc_int.h"

/* The small-diamond descent is bounded by the task: at most 32 improving steps. */
enum { N2AVC_ME_MAX_STEPS = 32 };

/* What n2avc_me_search() found for one 16x16 block.  All vectors are in
 * quarter luma samples; the search itself works on whole samples, so mvx and
 * mvy are always multiples of 4. */
typedef struct n2avc_me_result {
	int32_t mvx, mvy;       /* chosen vector, quarter samples */
	int32_t pmvx, pmvy;     /* the prediction the mvd is coded against */
	int32_t sad;            /* SAD of the chosen vector: 0..65280 */
	int32_t cost;           /* sad + lambda * (bits(mvd_x) + bits(mvd_y)) */
	int32_t mvd_bits;       /* bits(mvd_x) + bits(mvd_y), clause 9.1.1 */
} n2avc_me_result;

/* Per-slice scratch, owned by n2avc_inter_slice_begin() / _end().  One per
 * slice means the slice threads of WP03 share nothing writable here.
 *
 * The objects come from a fixed pool (N2AVC_INTER_SLOTS) owned by
 * n2avc_inter.c, not from malloc: begin() hands out a free slot and end()
 * returns it, so a steady-state encoder performs no allocation at all in the
 * per-picture path (review checklist).  The n2vk full-search result arrays
 * below are the only heap allocations left; they are grown once per slot and
 * kept for reuse, and they exist only when a non-reference n2vk backend is
 * installed (the reference backend allocates nothing). */
typedef struct n2avc_inter_priv {
	uint8_t pred_y[16 * 16];        /* packed prediction, stride 16 */
	uint8_t pred_cb[8 * 8];         /* packed prediction, stride 8 */
	uint8_t pred_cr[8 * 8];
	uint8_t save_y[16 * 16];        /* rec backup around the P_Skip probe */
	uint8_t save_cb[8 * 8];
	uint8_t save_cr[8 * 8];
	int      slot;                  /* index in the pool, -1 when malloc'd */
	int      have_full;             /* n2vk_me16 answers are usable */
	int      first_row;             /* first macroblock row of this slice */
	int      rows;                  /* macroblock rows in this slice */
	int      mb_w;                  /* picture width in macroblocks */
	size_t   full_cap;              /* entries allocated in full_mvx/full_mvy/full_sad */
	int16_t *full_mvx;              /* rows * mb_w, integer samples */
	int16_t *full_mvy;
	uint32_t *full_sad;
} n2avc_inter_priv;

/* n2avc_me.c */
int  n2avc_me_lambda(int qp);                                   /* table of 52 entries, qp 0..51 */
int  n2avc_me_skip_sad(int qp);                                 /* refinement early-exit threshold */
int  n2avc_se_bits(int32_t v);                                  /* clause 9.1.1 code length */
int  n2avc_me_valid(const n2avc_slice *s, int mbx, int mby, int dx, int dy);
/* Same test for a vector in quarter luma samples (the 6-tap filter reads at
 * most two samples beyond the 16x16 block, which the N2AVC_PAD - 8 rule
 * covers: 24 + 2 <= N2AVC_PAD). */
int  n2avc_me_valid_q(const n2avc_slice *s, int mbx, int mby, int mvx, int mvy);
int  n2avc_me_search(const n2avc_slice *s, int mbx, int mby, int pmvx, int pmvy,
                     n2avc_me_result *out);
/* Fills the slice's full-search arrays when N2AVC_ME_FULL is defined or a
 * non-reference n2vk backend is installed.  A failure (no n2vk symbol, an
 * argument the kernel rejects, out of memory) is not fatal: the diamond
 * search runs alone. */
void n2avc_me_full_setup(n2avc_slice *s, n2avc_inter_priv *p);
int  n2avc_me_full_result(const n2avc_inter_priv *p, int mbx, int mby,
                          int *dx, int *dy, int *sad);

/* n2avc_inter.c */
void n2avc_inter_pmv(const n2avc_slice *s, int mbx, int mby, int *mvx, int *mvy);
void n2avc_inter_skip_mv(const n2avc_slice *s, int mbx, int mby, int *mvx, int *mvy);
/* Builds the 16x16 luma and 8x8 chroma prediction for a quarter-sample vector.
 * py is packed with stride 16, pcb/pcr with stride 8.  The caller guarantees
 * that n2avc_me_valid_q() accepted the vector (or that it is the skip vector).
 * The luma half is clause 8.4.2.2.1 (6-tap, two stages for a diagonal
 * position); the chroma half is the bilinear filter of clause 8.4.2.2.2. */
void n2avc_inter_predict(const n2avc_slice *s, int mbx, int mby, int mvx, int mvy,
                         uint8_t *py, uint8_t *pcb, uint8_t *pcr);

/* The luma half alone, for the sub-pel search's exact cost.  dst is 16x16
 * packed with the given stride.  mvx/mvy are quarter luma samples. */
void n2avc_inter_luma(const n2avc_slice *s, int mbx, int mby, int mvx, int mvy,
                      uint8_t *dst, int dst_stride);

#endif
