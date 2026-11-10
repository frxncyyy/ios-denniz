/* SPDX-License-Identifier: MIT */
/*
 * n2avc_priv.h -- internal interface of the WP03 H.264 encoder core.
 *
 * Everything here is private to avc/ (the public interface is include/n2avc.h, the seam with WP04 is
 * include/n2avc_int.h, both frozen and lead-owned).  Written from ITU-T H.264 (03/2010); clause numbers below
 * refer to that Recommendation.
 */
#ifndef N2AVC_PRIV_H
#define N2AVC_PRIV_H

#include "n2avc_int.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* An Exp-Golomb-style codeword of the standard's tables, read as "len bits, value code" (MSB first). */
typedef struct n2vlc {
	uint8_t  len;           /* 0: this (TotalCoeff, TrailingOnes, ...) combination has no codeword */
	uint8_t  pad;
	uint16_t code;
} n2vlc;

#define N2AVC_MAX_THREADS 16

/* ------------------------------------------------------------------ integers */
static inline int n2avc_imin(int a, int b) { return a < b ? a : b; }
static inline int n2avc_imax(int a, int b) { return a > b ? a : b; }
static inline int n2avc_clip1(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* Arithmetic right shift with floor rounding for negative values (what clause 8.5 requires; C's >> on a negative
 * int is implementation-defined).  n is 0..31. */
static inline int n2avc_shr(int v, int n)
{
	if (n <= 0)
		return v;
	if (v >= 0)
		return v >> n;
	return -(((-v) + (1 << n) - 1) >> n);
}

/* ------------------------------------------------------------- n2avc_bits.c */
static inline void n2avc_put_vlc(n2bw *w, const n2vlc *c)
{
	if (c->len)
		n2bw_u(w, c->len, c->code);
	else
		w->overflow = 1;        /* a combination with no codeword: a bug, never a valid stream */
}

/* ------------------------------------------------------------ n2avc_xform.c */
/* Forward core transform of (src - pred), clause 8.5.1 (encoder side): W in raster order, 4x4. */
void n2avc_fwd4x4(const uint8_t *src, int ss, const uint8_t *pred, int ps, int16_t W[16]);
/* Quantisation of all 16 positions (luma 4x4 / chroma AC path), clause 8.5.2 with MF of the derivation;
 * output in zig-zag scan order (clause 8.5.6).  Returns the number of non-zero levels. */
int  n2avc_quant4x4(const int16_t W[16], int qp, int intra, int16_t zz[16]);
/* Same, but position 0 (the DC, already coded in the DC block) is skipped and set to 0. */
int  n2avc_quant4x4_ac(const int16_t W[16], int qp, int intra, int16_t zz[16]);
/* Inverse quantisation + inverse transform + prediction, clause 8.5.12; zz in zig-zag order, result clipped. */
void n2avc_inv4x4(const int16_t zz[16], int qp, const uint8_t *pred, int ps, uint8_t *rec, int rs);
/* Same, with the DC (position 0) given already scaled, as clause 8.5.2 step 2 and 8.5.4 step 2 prescribe. */
void n2avc_inv4x4_dc(const int16_t zz[16], int dc, int qp, const uint8_t *pred, int ps, uint8_t *rec, int rs);

/* Intra_16x16 DC: 4x4 Hadamard of the 16 block DC values (raster order), quantisation and their inverses.
 * W may need more than 16 bits: a block DC is +-4080 and the Hadamard sums 16 of them. */
void n2avc_fwd_dc16(const int16_t dc[16], int32_t W[16]);
int  n2avc_quant_dc16(const int32_t W[16], int qp, int intra, int16_t zz[16]);
void n2avc_inv_dc16(const int16_t zz[16], int qp, int dc[16]);
/* Chroma DC 2x2 (clause 8.5.11), levels and values in raster order (the order clause 8.5.4 uses). */
void n2avc_fwd_dc4(const int16_t dc[4], int16_t W[4]);
int  n2avc_quant_dc4(const int16_t W[4], int qp, int intra, int16_t lvl[4]);
void n2avc_inv_dc4(const int16_t lvl[4], int qp, int dc[4]);
/* QP_C from QP_Y with chroma_qp_index_offset 0 (table 8-15). */
int  n2avc_qpc(int qp);
/* Sum of absolute differences of a w x h block, for the intra mode decision. */
int  n2avc_sad(const uint8_t *a, int as, const uint8_t *b, int bs, int w, int h);

/* ------------------------------------------------------------ n2avc_cavlc.c */
/* residual_block_cavlc of clause 7.3.5.3.2 for maxNumCoeff positions starting at start_idx, levels in scan order. */
void n2avc_write_residual(n2bw *w, const int16_t *coef, int max_num_coeff, int start_idx, int nC);
/* nC of clause 9.2.1 for 4x4 block blk_idx of macroblock (mbx, mby): 0..15 luma in raster order of the
 * macroblock, 16..19 Cb, 20..23 Cr (raster order of the 2x2 chroma blocks). */
int  n2avc_block_nc(const n2avc_slice *s, int mbx, int mby, int blk_idx);

/* ------------------------------------------------------------ n2avc_intra.c */
/* Intra_16x16 luma prediction of clause 8.3.3 into pred (stride 16), mode 0..3 (vertical, horizontal, DC, plane). */
void n2avc_pred_luma16(int mode, const n2avc_slice *s, int mbx, int mby, uint8_t *pred);
/* Chroma prediction of clause 8.3.4 into pred (stride 8), mode 0..3 (DC, horizontal, vertical, plane). */
void n2avc_pred_chroma(int mode, const n2avc_slice *s, int mbx, int mby, int comp, uint8_t *pred);
int  n2avc_luma16_mode_available(int mode, const n2avc_slice *s, int mbx, int mby);
int  n2avc_chroma_mode_available(int mode, const n2avc_slice *s, int mbx, int mby);
/* Least SAD among the available modes; fills pred (16x16 stride 16) resp. pcb/pcr (8x8 stride 8) with the winner
 * and *mode_out with its mode number.  Returns the SAD of the winner. */
int  n2avc_intra_pick_luma(const n2avc_slice *s, int mbx, int mby, uint8_t *pred, int *mode_out);
int  n2avc_intra_pick_chroma(const n2avc_slice *s, int mbx, int mby, uint8_t *pcb, uint8_t *pcr, int *mode_out);

/* ----------------------------------------------------------------- n2avc_mb.c */
void n2avc_write_ipcm(n2avc_slice *s, int mbx, int mby);
/* True when a real codeword exists for coded_block_pattern (always, for 0..47). */
uint32_t n2avc_cbp_inter_code(int cbp);

/* The raster index (y4 * 4 + x4, the order n2avc_mbinfo.nnz and n2avc_slice.coef use) of the block with
 * luma4x4BlkIdx = i8x8 * 4 + i4x4, which is the order the residual syntax of clause 7.3.5.3.1 codes them in. */
static inline int n2avc_raster_blk(int i8x8, int i4x4)
{
	int x4 = (i8x8 & 1) * 2 + (i4x4 & 1);
	int y4 = (i8x8 >> 1) * 2 + (i4x4 >> 1);
	return y4 * 4 + x4;
}

/* --------------------------------------------------------------- n2avc_enc.c */
/* The slice header of clause 7.3.3 for slice s (first_mb_in_slice, slice_type, frame_num, idr_pic_id). */
void n2avc_write_slice_header(n2avc_slice *s, int first_mb, int idr, int frame_num, int idr_pic_id);

/* ------------------------------------------------------------------ the encoder object */
struct n2avc_worker;

typedef struct n2avc_enc {
	n2avc_config cfg;                       /* with the defaults resolved */
	uint32_t width, height;
	int      mb_w, mb_h;
	int      level_idc;
	uint32_t fps_num, fps_den;
	int      nthreads;                      /* slices per picture, 1..16 */

	n2avc_frame src;                        /* padded picture to code (16*mb_w x 16*mb_h plus borders) */
	n2avc_frame frm[2];                     /* reconstruction buffers; one is `ref` (the last coded picture) */
	int      cur;                           /* index of the frame being written */
	uint8_t *src_base[3];
	uint8_t *frm_base[2][3];

	n2avc_mbinfo *mb;                       /* mb_w * mb_h */
	n2avc_slice   sl[N2AVC_MAX_THREADS];
	uint8_t      *sl_buf[N2AVC_MAX_THREADS];
	size_t        sl_cap[N2AVC_MAX_THREADS];

	uint8_t *out;                           /* the packet buffer */
	size_t   out_cap;
	uint8_t *sps, *pps;                     /* NAL units (header byte + escaped RBSP) */
	size_t   sps_n, pps_n;

	int frame_num;                          /* next frame_num */
	int idr_pic_id;                         /* 0/1, alternates per IDR */
	int cur_idr, cur_frame_num, cur_idr_pic_id;  /* the picture the slice threads are coding */
	int pictures;                           /* pictures coded so far */
	int since_idr;
	int failed;                             /* a previous call failed: N2AVC_E_STATE from now on */
	/* Test instrumentation (not part of the interface): how many macroblocks were coded as I_PCM, in the last
	 * picture and since open.  The oracle tests use it to prove that the I_PCM fallback is exercised. */
	uint32_t dbg_ipcm_last, dbg_ipcm_total;

	n2avc_rc *rc;

	pthread_t th[N2AVC_MAX_THREADS];
	int       nthr_created;
	pthread_mutex_t mu;
	pthread_cond_t  cv_job, cv_done;
	unsigned job_gen;
	int      done, quit;
	struct n2avc_worker { struct n2avc_enc *enc; int idx; } wk[N2AVC_MAX_THREADS];
} n2avc_enc;

/* Builds the SPS and PPS NAL units into e->sps / e->pps (clauses 7.3.2.1.1, 7.3.2.2, E.1.1). */
int n2avc_build_ps(n2avc_enc *e);
/* Smallest level_idc of include/n2avc.h for this picture size and frame rate. */
int n2avc_pick_level(uint32_t w, uint32_t h, uint32_t fps);

#endif
