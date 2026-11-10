/*
 * n2avc_int.h -- the seam inside the H.264 encoder between WP03 (core: bit writer, parameter sets, transform, CAVLC, intra,
 * the picture/slice/macroblock loop) and WP04 (inter prediction, motion search, rate control). LEAD-OWNED, FROZEN: a change is
 * asked for on the board and made by the lead.
 *
 * Clause numbers are those of ITU-T H.264 (08/2021). All code is written from the Recommendation; nothing is copied from
 * x264, FFmpeg, OpenH264 or JM (their licences do not fit; they are used only as black-box oracles on the build machine).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2AVC_INT_H
#define N2AVC_INT_H

#include "n2avc.h"

/* ---- bit writer (WP03: avc/n2avc_bits.c) ---- */
typedef struct n2bw {
	uint8_t  *buf;          /* RBSP bytes (no emulation prevention here) */
	size_t   cap, pos;      /* bytes */
	uint64_t acc;           /* pending bits, MSB first */
	int      nbits;         /* number of pending bits in acc (0..63) */
	int      overflow;      /* set once a write did not fit; later writes are ignored; the caller checks at the end */
} n2bw;
void   n2bw_init(n2bw *w, uint8_t *buf, size_t cap);
void   n2bw_u(n2bw *w, int n, uint32_t v);      /* n = 1..32 bits of v, MSB first */
void   n2bw_ue(n2bw *w, uint32_t v);            /* Exp-Golomb, clause 9.1; v <= 2^32 - 2 */
void   n2bw_se(n2bw *w, int32_t v);             /* signed Exp-Golomb, clause 9.1.1 */
void   n2bw_trailing(n2bw *w);                  /* rbsp_trailing_bits: a 1 then zeros to the byte boundary */
void   n2bw_align_zero(n2bw *w);                /* zeros to the byte boundary (before pcm samples) */
size_t n2bw_bits(const n2bw *w);                /* bits written so far */
size_t n2bw_flush(n2bw *w);                     /* writes pending whole bytes; returns the byte count (must be byte aligned) */
/* One NAL unit from an RBSP: header byte (nal_ref_idc << 5 | nal_unit_type) + payload with emulation_prevention_three_byte
 * (clause 7.4.1). Returns the bytes written, 0 when dst is too small. No start code, no length. */
size_t n2avc_nal(uint8_t *dst, size_t cap, const uint8_t *rbsp, size_t n, int nal_ref_idc, int nal_unit_type);

/* ---- pictures ---- */
#define N2AVC_PAD 32            /* luma samples of border around every reconstructed picture; chroma has N2AVC_PAD / 2 */
typedef struct n2avc_plane { uint8_t *p; int stride; } n2avc_plane;    /* p = sample (0,0); rows -pad .. h+pad-1 are readable */
typedef struct n2avc_frame { n2avc_plane y, cb, cr; } n2avc_frame;      /* y: 16*mb_w x 16*mb_h, cb/cr: half of that */

/* ---- per-macroblock state kept for neighbours (one per macroblock of the picture) ---- */
enum { N2MB_I16 = 0, N2MB_IPCM = 1, N2MB_P16 = 2, N2MB_PSKIP = 3 };
typedef struct n2avc_mbinfo {
	int16_t mvx, mvy;       /* luma motion vector in quarter samples (integer search: multiples of 4); 0 for intra */
	int8_t  ref;            /* 0: inter, reference index 0; -1: intra */
	uint8_t type;           /* N2MB_* */
	uint8_t qp;             /* QP_Y the macroblock was coded with */
	uint8_t cbp;            /* bits 0..3 luma 8x8 quadrants (raster), bits 4..5 chroma: 0 none, 1 DC only, 2 DC + AC */
	uint8_t nnz[24];        /* total_coeff of each 4x4 block, for nC (clause 9.2.1): [y4 * 4 + x4] luma 0..15 in RASTER order of
	                         * the macroblock (not the zig-zag blkIdx order), 16..19 Cb (y2 * 2 + x2), 20..23 Cr. I_PCM: all 16.
	                         * P_Skip and blocks not coded: 0. The Intra16x16 DC block is not counted here. */
} n2avc_mbinfo;

/* ---- one slice being coded (each slice has its own; slices of a picture are coded on different threads) ---- */
typedef struct n2avc_slice {
	n2bw     bw;                    /* slice_data RBSP; the slice header is already written when the macroblock loop starts */
	int      slice_type;            /* 0: P, 2: I (the value mod 5 of clause 7.4.3) */
	int      qp;                    /* SliceQP_Y: every macroblock of the slice uses it (mb_qp_delta is always 0) */
	int      qpc;                   /* QP_C for it (table 8-15, chroma_qp_index_offset 0) */
	int      mb_w, mb_h;            /* the picture, in macroblocks */
	int      first_row, end_row;    /* macroblock rows of this slice: first_row <= mby < end_row */
	const n2avc_frame *src;         /* the picture to code, padded to 16 * mb_w x 16 * mb_h by repeating the edge */
	n2avc_frame *rec;               /* reconstruction being built (borders are extended by WP03 after the whole picture) */
	const n2avc_frame *ref;         /* the previous reconstruction with its borders; NULL in an I slice */
	n2avc_mbinfo *mb;               /* [mby * mb_w + mbx] for the whole picture; see n2avc_avail() before reading a neighbour */
	uint32_t skip_run;              /* P_Skip macroblocks since the last coded one (mb_skip_run, clause 7.3.4) */
	int      search_range;          /* integer samples */
	void     *inter_priv;           /* WP04's per-slice scratch, from n2avc_inter_slice_begin() */
	int16_t  coef[24][16];          /* quantised levels of the current macroblock, each 4x4 in zig-zag scan order (clause 8.5.6,
	                                 * frame scan), indexed like nnz[]; for Intra16x16 and chroma, entry [..][0] is unused (the DC
	                                 * is in dc_*) */
	int16_t  dc_y[16], dc_cb[4], dc_cr[4];  /* Intra16x16DCLevel (zig-zag order), chroma DC levels (raster order) */
} n2avc_slice;

/* 1 when macroblock (mbx, mby) is inside the picture and in this slice (availability, clause 6.4.9: other slices are "not
 * available"). */
static inline int n2avc_avail(const n2avc_slice *s, int mbx, int mby)
{
	return mbx >= 0 && mbx < s->mb_w && mby >= s->first_row && mby < s->end_row;
}

/* ---- WP03 provides (avc/n2avc_mb.c, n2avc_xform.c, n2avc_cavlc.c) ---- */

/* Writes ue(skip_run) and zeroes it. Called, in a P slice only, immediately before the mb_type of every macroblock that is not
 * skipped (by WP04 for P_L0_16x16, by WP03 for intra), and by WP03 at the end of the slice when skip_run > 0. */
void n2avc_flush_skip_run(n2avc_slice *s);

/* Inter residual of one macroblock: source minus the given prediction (pred_y 16x16 stride 16, pred_cb / pred_cr 8x8 stride 8),
 * 4x4 transform and quantisation (clause 8.5; inter rounding offset 1/6), chroma DC 2x2, inverse and reconstruction into s->rec.
 * Fills s->coef / s->dc_cb / s->dc_cr and this macroblock's nnz[]; a luma 8x8 quadrant whose four blocks are all zero gets its
 * cbp bit cleared. Returns the coded_block_pattern value (luma | chroma << 4), 0..47. Writes nothing to the bit stream. */
int  n2avc_inter_residual(n2avc_slice *s, int mbx, int mby, const uint8_t *pred_y, const uint8_t *pred_cb, const uint8_t *pred_cr);
/* Writes residual() of clause 7.3.5.3 for an inter macroblock from s->coef (after the caller has written mb_type, mvd,
 * coded_block_pattern and, when cbp != 0, mb_qp_delta = se(0)). */
void n2avc_write_inter_residual(n2avc_slice *s, int mbx, int mby, int cbp);
/* Codes one whole Intra16x16 macroblock (choice of mode, syntax, reconstruction, mbinfo). In a P slice the caller has already
 * called n2avc_flush_skip_run(); mb_type is written with the +5 offset of table 7-13 there. */
void n2avc_intra_mb(n2avc_slice *s, int mbx, int mby);
/* The mapped Exp-Golomb code number of coded_block_pattern for Inter prediction (table 9-4, ChromaArrayType 1). */
uint32_t n2avc_cbp_inter_code(int cbp);
/* Sum of absolute transformed differences is NOT provided; WP04 uses SAD. */

/* ---- WP04 provides (avc/n2avc_inter.c, n2avc_me.c, n2avc_rc.c) ---- */

/* Per-slice scratch. begin returns NULL only when out of memory (the encoder then codes the slice intra). */
void *n2avc_inter_slice_begin(n2avc_slice *s);
void  n2avc_inter_slice_end(n2avc_slice *s);
/* One macroblock of a P slice. WP04 searches the motion (integer samples, inside the padded reference: every sample read must
 * be within N2AVC_PAD - 8 luma samples of the picture), predicts the vector (clause 8.4.1.3, 16x16 median; 8.4.1.1 for P_Skip),
 * builds the prediction (luma: copy; chroma: clause 8.4.2.2.2 bilinear, eighth samples) and decides:
 *   returns 1: P_Skip. Nothing written, s->skip_run incremented, prediction copied into s->rec, mbinfo filled
 *              (type N2MB_PSKIP, the skip vector, ref 0, cbp 0, nnz 0).
 *   returns 0: P_L0_16x16. It called n2avc_flush_skip_run(), wrote mb_type ue(0), mvd_l0 se x then se y (quarter samples),
 *              coded_block_pattern ue(n2avc_cbp_inter_code(cbp)), mb_qp_delta se(0) if cbp != 0, called
 *              n2avc_write_inter_residual(); mbinfo filled (type N2MB_P16).
 *   returns 2: intra is cheaper (or the vector does not fit): nothing written, nothing changed; WP03 then calls
 *              n2avc_flush_skip_run() and n2avc_intra_mb(), which fills mbinfo with ref -1.
 * A P_L0_16x16 macroblock whose cbp is 0 and whose vector equals the skip vector MUST be returned as P_Skip. */
int   n2avc_p_mb(n2avc_slice *s, int mbx, int mby);

/* Rate control: one object per encoder. qp for the next picture; then what the picture cost. Constant QP when bitrate is 0.
 * Requirements: after the first second the mean bitrate over any 2 s window stays within +-25 % of the target on the oracle's
 * sequences; QP changes by at most 4 between consecutive P pictures; IDR pictures use QP_P - 2 (clamped 10..51). */
typedef struct n2avc_rc n2avc_rc;
n2avc_rc *n2avc_rc_open(const n2avc_config *cfg);
int   n2avc_rc_picture_qp(n2avc_rc *rc, int idr);
void  n2avc_rc_picture_done(n2avc_rc *rc, int idr, int qp, size_t bytes);
void  n2avc_rc_reconfigure(n2avc_rc *rc, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den);
void  n2avc_rc_close(n2avc_rc *rc);

#endif
