/* SPDX-License-Identifier: MIT */
/*
 * n2avc_mb.c -- the macroblock layer of the WP03 H.264 encoder: the intra macroblock (choice of mode, syntax,
 * reconstruction), the inter residual path used by WP04, I_PCM and the coded_block_pattern mapping.
 *
 * ITU-T H.264 (03/2010): 7.3.5 (macroblock_layer), 7.3.5.3 (residual), table 7-11 (mb types of I slices),
 * table 9-4 (coded_block_pattern), 8.5.1-8.5.12 (reconstruction), 7.3.4 (mb_skip_run).
 */
#include "n2avc_priv.h"

#include <string.h>

/* Table 9-4 (a), column "Inter", inverted: cbp -> codeNum. */
static const uint8_t cbp_inter_code[48] = {
	0,  2,  3,  7,  4,  8, 17, 13,
	5, 18,  9, 14, 10, 15, 16, 11,
	1, 32, 33, 36, 34, 37, 44, 40,
	35, 45, 38, 41, 39, 42, 43, 19,
	6, 24, 25, 20, 26, 21, 46, 28,
	27, 47, 22, 29, 23, 30, 31, 12
};

uint32_t n2avc_cbp_inter_code(int cbp)
{
	if (cbp < 0 || cbp > 47)
		return 0;
	return (uint32_t)cbp_inter_code[cbp];
}

void n2avc_flush_skip_run(n2avc_slice *s)
{
	/* clause 7.3.4: mb_skip_run precedes every macroblock of a CAVLC P slice, ue(0) included.  At the end of a
	 * slice the caller must not call this with skip_run == 0: a trailing zero-length run would make the decoder
	 * parse one more macroblock_layer(). */
	n2bw_ue(&s->bw, s->skip_run);
	s->skip_run = 0;
}

/* I_PCM of clause 7.3.5: pcm_alignment_zero_bit, 256 luma and 2 * 64 chroma samples, verbatim. */
void n2avc_write_ipcm(n2avc_slice *s, int mbx, int mby)
{
	const int ss = s->src->y.stride, scb = s->src->cb.stride, scr = s->src->cr.stride;
	const int rs = s->rec->y.stride, rcb = s->rec->cb.stride, rcr = s->rec->cr.stride;
	const uint8_t *sy = s->src->y.p + (size_t)(mby * 16) * (size_t)ss + (size_t)(mbx * 16);
	const uint8_t *scbp = s->src->cb.p + (size_t)(mby * 8) * (size_t)scb + (size_t)(mbx * 8);
	const uint8_t *scrp = s->src->cr.p + (size_t)(mby * 8) * (size_t)scr + (size_t)(mbx * 8);
	uint8_t *ry = s->rec->y.p + (size_t)(mby * 16) * (size_t)rs + (size_t)(mbx * 16);
	uint8_t *rcbp = s->rec->cb.p + (size_t)(mby * 8) * (size_t)rcb + (size_t)(mbx * 8);
	uint8_t *rcrp = s->rec->cr.p + (size_t)(mby * 8) * (size_t)rcr + (size_t)(mbx * 8);
	n2avc_mbinfo *mi = &s->mb[mby * s->mb_w + mbx];
	int i, j;

	n2bw_ue(&s->bw, (s->slice_type == 0) ? 30u : 25u);      /* I_PCM, table 7-13 / table 7-11 */
	n2bw_align_zero(&s->bw);
	for (i = 0; i < 16; i++)
		for (j = 0; j < 16; j++)
			n2bw_u(&s->bw, 8, sy[i * ss + j]);
	for (i = 0; i < 8; i++)
		for (j = 0; j < 8; j++)
			n2bw_u(&s->bw, 8, scbp[i * scb + j]);
	for (i = 0; i < 8; i++)
		for (j = 0; j < 8; j++)
			n2bw_u(&s->bw, 8, scrp[i * scr + j]);

	for (i = 0; i < 16; i++) {
		memcpy(ry + i * rs, sy + i * ss, 16);
		if (i < 8) {
			memcpy(rcbp + i * rcb, scbp + i * scb, 8);
			memcpy(rcrp + i * rcr, scrp + i * scr, 8);
		}
	}
	mi->type = N2MB_IPCM;
	mi->ref = -1;
	mi->mvx = mi->mvy = 0;
	mi->qp = (uint8_t)s->qp;
	mi->cbp = 47;
	for (i = 0; i < 24; i++)
		mi->nnz[i] = 16;                                /* clause 9.2.1: nN = 16 next to an I_PCM block */
}

/* The Intra_16x16 macroblock: least-SAD luma and chroma modes, residual, syntax, reconstruction.
 * A macroblock whose I_16x16 syntax would take more than 3200 bits is coded as I_PCM instead. */
void n2avc_intra_mb(n2avc_slice *s, int mbx, int mby)
{
	n2avc_mbinfo *mi = &s->mb[mby * s->mb_w + mbx];
	const int qp = s->qp, qpc = s->qpc;
	const int is_p = (s->slice_type == 0);
	const int ss = s->src->y.stride, rs = s->rec->y.stride;
	const int scb = s->src->cb.stride, scr = s->src->cr.stride;
	const int rcb = s->rec->cb.stride, rcr = s->rec->cr.stride;
	uint8_t *srcY = s->src->y.p + (size_t)(mby * 16) * (size_t)ss + (size_t)(mbx * 16);
	uint8_t *recY = s->rec->y.p + (size_t)(mby * 16) * (size_t)rs + (size_t)(mbx * 16);
	uint8_t *srcCb = s->src->cb.p + (size_t)(mby * 8) * (size_t)scb + (size_t)(mbx * 8);
	uint8_t *srcCr = s->src->cr.p + (size_t)(mby * 8) * (size_t)scr + (size_t)(mbx * 8);
	uint8_t *recCb = s->rec->cb.p + (size_t)(mby * 8) * (size_t)rcb + (size_t)(mbx * 8);
	uint8_t *recCr = s->rec->cr.p + (size_t)(mby * 8) * (size_t)rcr + (size_t)(mbx * 8);
	uint8_t py[256], pcb[64], pcr[64];
	int16_t W[16];
	int16_t dc[16];
	int32_t Wdc[16];
	int dcY[16];
	int16_t cdc[4], cW[4], clvl[4];
	int dcC[2][4];
	int luma_mode = 2, chroma_mode = 0, luma_ac = 0;
	int chroma_dc = 0, chroma_ac = 0, chroma_cbp, cbp, mb_type;
	int i, k, comp;
	n2bw save;
	size_t bits_before;

	n2avc_intra_pick_luma(s, mbx, mby, py, &luma_mode);
	n2avc_intra_pick_chroma(s, mbx, mby, pcb, pcr, &chroma_mode);

	/* Luma: 16 4x4 transforms (raster order), DC of each into the Hadamard block, AC quantised separately. */
	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2;
		size_t o = (size_t)(y4 * 4) * (size_t)ss + (size_t)(x4 * 4);
		int nz;
		n2avc_fwd4x4(srcY + o, ss, py + y4 * 4 * 16 + x4 * 4, 16, W);
		dc[k] = W[0];
		nz = n2avc_quant4x4_ac(W, qp, 1, s->coef[k]);
		mi->nnz[k] = (uint8_t)nz;
		if (nz)
			luma_ac = 1;
	}
	n2avc_fwd_dc16(dc, Wdc);
	n2avc_quant_dc16(Wdc, qp, 1, s->dc_y);
	n2avc_inv_dc16(s->dc_y, qp, dcY);

	/* Chroma: same shape, 8x8 per component, DC of the four 4x4 blocks into a 2x2 Hadamard. */
	for (comp = 0; comp < 2; comp++) {
		const uint8_t *sc = comp ? srcCr : srcCb;
		const int cstride = comp ? scr : scb;
		const uint8_t *pc = comp ? pcr : pcb;
		int dcnz = 0;
		for (i = 0; i < 4; i++) {
			int x4 = (i & 1) * 4, y4 = (i >> 1) * 4;
			size_t o = (size_t)y4 * (size_t)cstride + (size_t)x4;
			int nz;
			n2avc_fwd4x4(sc + o, cstride, pc + y4 * 8 + x4, 8, W);
			cdc[i] = W[0];
			nz = n2avc_quant4x4_ac(W, qpc, 1, s->coef[16 + comp * 4 + i]);
			mi->nnz[16 + comp * 4 + i] = (uint8_t)nz;
			if (nz)
				chroma_ac = 1;
		}
		n2avc_fwd_dc4(cdc, cW);
		dcnz = n2avc_quant_dc4(cW, qpc, 1, clvl);
		if (comp == 0) {
			for (i = 0; i < 4; i++)
				s->dc_cb[i] = clvl[i];
		} else {
			for (i = 0; i < 4; i++)
				s->dc_cr[i] = clvl[i];
		}
		if (dcnz)
			chroma_dc = 1;
		n2avc_inv_dc4(clvl, qpc, dcC[comp]);
	}
	chroma_cbp = chroma_ac ? 2 : (chroma_dc ? 1 : 0);
	cbp = (luma_ac ? 15 : 0) | (chroma_cbp << 4);
	mb_type = 1 + luma_mode + 4 * chroma_cbp + (luma_ac ? 12 : 0);
	if (is_p)
		mb_type += 5;                           /* table 7-13: I types are offset by 5 */

	save = s->bw;
	bits_before = n2bw_bits(&s->bw);
	n2bw_ue(&s->bw, (uint32_t)mb_type);
	n2bw_ue(&s->bw, (uint32_t)chroma_mode);          /* intra_chroma_pred_mode, table 7-16 */
	n2bw_se(&s->bw, 0);                              /* mb_qp_delta: every macroblock uses SliceQP_Y */
	n2avc_write_residual(&s->bw, s->dc_y, 16, 0, n2avc_block_nc(s, mbx, mby, 0));
	if (luma_ac) {
		int i8, i4;
		for (i8 = 0; i8 < 4; i8++)
			for (i4 = 0; i4 < 4; i4++) {
				int kb = n2avc_raster_blk(i8, i4);
				n2avc_write_residual(&s->bw, s->coef[kb], 16, 1,
						     n2avc_block_nc(s, mbx, mby, kb));
			}
	}
	if (chroma_cbp & 3) {
		n2avc_write_residual(&s->bw, s->dc_cb, 4, 0, -1);
		n2avc_write_residual(&s->bw, s->dc_cr, 4, 0, -1);
	}
	if (chroma_cbp & 2) {
		for (comp = 0; comp < 2; comp++)
			for (i = 0; i < 4; i++)
				n2avc_write_residual(&s->bw, s->coef[16 + comp * 4 + i], 16, 1,
						     n2avc_block_nc(s, mbx, mby, 16 + comp * 4 + i));
	}

	if (s->bw.overflow || n2bw_bits(&s->bw) - bits_before > 3200) {
		s->bw = save;
		n2avc_write_ipcm(s, mbx, mby);
		return;
	}

	/* Reconstruction, clause 8.5.2/8.5.4: the DC value of each block comes from the DC path, already scaled. */
	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2;
		size_t o = (size_t)(y4 * 4) * (size_t)rs + (size_t)(x4 * 4);
		n2avc_inv4x4_dc(s->coef[k], dcY[k], qp, py + y4 * 4 * 16 + x4 * 4, 16, recY + o, rs);
	}
	for (comp = 0; comp < 2; comp++) {
		uint8_t *rc = comp ? recCr : recCb;
		const int rstride = comp ? rcr : rcb;
		const uint8_t *pc = comp ? pcr : pcb;
		for (i = 0; i < 4; i++) {
			int x4 = (i & 1) * 4, y4 = (i >> 1) * 4;
			size_t o = (size_t)y4 * (size_t)rstride + (size_t)x4;
			n2avc_inv4x4_dc(s->coef[16 + comp * 4 + i], dcC[comp][i], qpc,
					pc + y4 * 8 + x4, 8, rc + o, rstride);
		}
	}

	mi->type = N2MB_I16;
	mi->ref = -1;
	mi->mvx = mi->mvy = 0;
	mi->qp = (uint8_t)qp;
	mi->cbp = (uint8_t)cbp;
}

/* Inter residual of one macroblock (WP04 calls this after writing mb_type/mvd/cbp): transform, quantise,
 * reconstruct in place and fill the neighbour state.  Returns the coded_block_pattern. */
int n2avc_inter_residual(n2avc_slice *s, int mbx, int mby, const uint8_t *pred_y, const uint8_t *pred_cb,
			 const uint8_t *pred_cr)
{
	n2avc_mbinfo *mi = &s->mb[mby * s->mb_w + mbx];
	const int qp = s->qp, qpc = s->qpc;
	const int ss = s->src->y.stride, rs = s->rec->y.stride;
	const int scb = s->src->cb.stride, scr = s->src->cr.stride;
	const int rcb = s->rec->cb.stride, rcr = s->rec->cr.stride;
	uint8_t *srcY = s->src->y.p + (size_t)(mby * 16) * (size_t)ss + (size_t)(mbx * 16);
	uint8_t *recY = s->rec->y.p + (size_t)(mby * 16) * (size_t)rs + (size_t)(mbx * 16);
	uint8_t *srcCb = s->src->cb.p + (size_t)(mby * 8) * (size_t)scb + (size_t)(mbx * 8);
	uint8_t *srcCr = s->src->cr.p + (size_t)(mby * 8) * (size_t)scr + (size_t)(mbx * 8);
	uint8_t *recCb = s->rec->cb.p + (size_t)(mby * 8) * (size_t)rcb + (size_t)(mbx * 8);
	uint8_t *recCr = s->rec->cr.p + (size_t)(mby * 8) * (size_t)rcr + (size_t)(mbx * 8);
	int16_t W[16];
	int16_t cdc[4], cW[4], clvl[4];
	int dcC[2][4];
	int luma = 0, chroma_cbp = 0, chroma_dc = 0, chroma_ac = 0;
	int k, i, comp;

	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2;
		size_t o = (size_t)(y4 * 4) * (size_t)ss + (size_t)(x4 * 4);
		int nz;
		n2avc_fwd4x4(srcY + o, ss, pred_y + y4 * 4 * 16 + x4 * 4, 16, W);
		nz = n2avc_quant4x4(W, qp, 0, s->coef[k]);
		mi->nnz[k] = (uint8_t)nz;
	}
	for (i = 0; i < 4; i++) {
		int any = 0;
		for (k = 0; k < 4; k++)
			if (mi->nnz[n2avc_raster_blk(i, k)])
				any = 1;
		if (any)
			luma |= 1 << i;                         /* CodedBlockPatternLuma, table 7-15 */
	}

	for (comp = 0; comp < 2; comp++) {
		const uint8_t *sc = comp ? srcCr : srcCb;
		const uint8_t *pc = comp ? pred_cr : pred_cb;
		const int cstride = comp ? scr : scb;
		int dcnz;
		for (i = 0; i < 4; i++) {
			int x4 = (i & 1) * 4, y4 = (i >> 1) * 4;
			size_t o = (size_t)y4 * (size_t)cstride + (size_t)x4;
			int nz;
			n2avc_fwd4x4(sc + o, cstride, pc + y4 * 8 + x4, 8, W);
			cdc[i] = W[0];
			nz = n2avc_quant4x4(W, qpc, 0, s->coef[16 + comp * 4 + i]);
			mi->nnz[16 + comp * 4 + i] = (uint8_t)nz;
			if (nz)
				chroma_ac = 1;
		}
		n2avc_fwd_dc4(cdc, cW);
		dcnz = n2avc_quant_dc4(cW, qpc, 0, clvl);
		if (comp == 0) {
			for (i = 0; i < 4; i++)
				s->dc_cb[i] = clvl[i];
		} else {
			for (i = 0; i < 4; i++)
				s->dc_cr[i] = clvl[i];
		}
		if (dcnz)
			chroma_dc = 1;
		n2avc_inv_dc4(clvl, qpc, dcC[comp]);
	}
	chroma_cbp = chroma_ac ? 2 : (chroma_dc ? 1 : 0);

	for (k = 0; k < 16; k++) {
		int x4 = k & 3, y4 = k >> 2;
		size_t o = (size_t)(y4 * 4) * (size_t)rs + (size_t)(x4 * 4);
		n2avc_inv4x4(s->coef[k], qp, pred_y + y4 * 4 * 16 + x4 * 4, 16, recY + o, rs);
	}
	for (comp = 0; comp < 2; comp++) {
		uint8_t *rc = comp ? recCr : recCb;
		const int rstride = comp ? rcr : rcb;
		const uint8_t *pc = comp ? pred_cr : pred_cb;
		for (i = 0; i < 4; i++) {
			int x4 = (i & 1) * 4, y4 = (i >> 1) * 4;
			size_t o = (size_t)y4 * (size_t)rstride + (size_t)x4;
			n2avc_inv4x4_dc(s->coef[16 + comp * 4 + i], dcC[comp][i], qpc,
					pc + y4 * 8 + x4, 8, rc + o, rstride);
		}
	}
	mi->qp = (uint8_t)qp;
	mi->cbp = (uint8_t)(luma | (chroma_cbp << 4));
	return luma | (chroma_cbp << 4);
}

void n2avc_write_inter_residual(n2avc_slice *s, int mbx, int mby, int cbp)
{
	int cc = (cbp >> 4) & 3;
	int i8, i4;

	for (i8 = 0; i8 < 4; i8++)
		if (cbp & (1 << i8))
			for (i4 = 0; i4 < 4; i4++) {
				int k = n2avc_raster_blk(i8, i4);
				n2avc_write_residual(&s->bw, s->coef[k], 16, 0, n2avc_block_nc(s, mbx, mby, k));
			}
	if (cc & 3) {
		n2avc_write_residual(&s->bw, s->dc_cb, 4, 0, -1);
		n2avc_write_residual(&s->bw, s->dc_cr, 4, 0, -1);
	}
	if (cc & 2) {
		int comp;
		for (comp = 0; comp < 2; comp++)
			for (i4 = 0; i4 < 4; i4++) {
				int k = 16 + comp * 4 + i4;
				n2avc_write_residual(&s->bw, s->coef[k], 16, 1, n2avc_block_nc(s, mbx, mby, k));
			}
	}
}
