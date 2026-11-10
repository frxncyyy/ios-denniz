/*
 * stub_wp03.c -- the WP03 side of the frozen seam, as a test double for the
 * WP04 host suite.  It is NOT part of the encoder and is never compiled into
 * the plug-in; it records what n2avc_p_mb writes so that literal expectations
 * (clause 7.3.4 syntax order and values) can be asserted.
 *
 * The recording format is private to the test: a sequence of 5-byte records
 * in n2bw.buf, { op, int32 little-endian }.  ops: 'f' = flush_skip_run(count),
 * 'e' = ue(value), 's' = se(value), 'r' = write_inter_residual(cbp).
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2avc_int.h"

#include <string.h>

/* Set by t_inter.c for the "cbp 0 but the skip SAD is above the threshold"
 * case; read-only while the thread test runs. */
int n2_wp04_force_zero;

static void
push(n2bw *w, unsigned char op, int32_t v)
{
	uint32_t u = (uint32_t)v;

	if (w->pos + 5 > w->cap) {
		w->overflow = 1;
		return;
	}
	w->buf[w->pos++] = op;
	w->buf[w->pos++] = (uint8_t)(u & 0xff);
	w->buf[w->pos++] = (uint8_t)((u >> 8) & 0xff);
	w->buf[w->pos++] = (uint8_t)((u >> 16) & 0xff);
	w->buf[w->pos++] = (uint8_t)((u >> 24) & 0xff);
}

void
n2bw_init(n2bw *w, uint8_t *buf, size_t cap)
{
	memset(w, 0, sizeof(*w));
	w->buf = buf;
	w->cap = cap;
}

void
n2bw_u(n2bw *w, int n, uint32_t v)
{
	push(w, 'u', (int32_t)((uint32_t)n << 24 | (v & 0xffffffu)));
}

void
n2bw_ue(n2bw *w, uint32_t v)
{
	push(w, 'e', (int32_t)v);
}

void
n2bw_se(n2bw *w, int32_t v)
{
	push(w, 's', v);
}

void
n2bw_trailing(n2bw *w)
{
	push(w, 't', 0);
}

void
n2bw_align_zero(n2bw *w)
{
	push(w, 'a', 0);
}

size_t
n2bw_bits(const n2bw *w)
{
	(void)w;
	return 0;
}

size_t
n2bw_flush(n2bw *w)
{
	return w->pos;
}

void
n2avc_flush_skip_run(n2avc_slice *s)
{
	push(&s->bw, 'f', (int32_t)s->skip_run);
	s->skip_run = 0;
}

uint32_t
n2avc_cbp_inter_code(int cbp)
{
	return 1000u + (uint32_t)cbp;
}

/* The test double's residual path: cbp 0 exactly when the prediction equals
 * the source (or when the force-zero knob is set); anything else gets a
 * nonzero pattern.  It reconstructs the prediction into s->rec, which is all
 * the WP04 decision code may rely on. */
int
n2avc_inter_residual(n2avc_slice *s, int mbx, int mby,
                     const uint8_t *pred_y, const uint8_t *pred_cb,
                     const uint8_t *pred_cr)
{
	const uint8_t *cur = s->src->y.p + (mby * 16) * s->src->y.stride + mbx * 16;
	int sad = 0;
	int cbp;
	int y;

	for (y = 0; y < 16; y++) {
		int x;

		for (x = 0; x < 16; x++) {
			int d = (int)cur[y * s->src->y.stride + x] - (int)pred_y[y * 16 + x];
			sad += d < 0 ? -d : d;
		}
	}
	if (n2_wp04_force_zero)
		cbp = 0;
	else if (sad == 0)
		cbp = 0;
	else if (sad < 64)
		cbp = 0x01;             /* luma DC only */
	else
		cbp = 0x25;             /* luma AC + chroma */

	for (y = 0; y < 16; y++)
		memcpy(s->rec->y.p + (mby * 16 + y) * s->rec->y.stride + mbx * 16,
		       pred_y + y * 16, 16);
	for (y = 0; y < 8; y++) {
		memcpy(s->rec->cb.p + (mby * 8 + y) * s->rec->cb.stride + mbx * 8,
		       pred_cb + y * 8, 8);
		memcpy(s->rec->cr.p + (mby * 8 + y) * s->rec->cr.stride + mbx * 8,
		       pred_cr + y * 8, 8);
	}

	memset(s->mb[mby * s->mb_w + mbx].nnz, 0,
	       sizeof(s->mb[mby * s->mb_w + mbx].nnz));
	s->mb[mby * s->mb_w + mbx].nnz[0] = cbp != 0 ? 1 : 0;
	s->mb[mby * s->mb_w + mbx].cbp = (uint8_t)cbp;
	s->coef[0][0] = (int16_t)(cbp + 1);
	return cbp;
}

void
n2avc_write_inter_residual(n2avc_slice *s, int mbx, int mby, int cbp)
{
	(void)mbx;
	(void)mby;
	push(&s->bw, 'r', (int32_t)cbp);
}

/* Not referenced by WP04; defined so that a future test can complete a slice
 * without pulling in all of WP03. */
void
n2avc_intra_mb(n2avc_slice *s, int mbx, int mby)
{
	n2avc_mbinfo *m = &s->mb[mby * s->mb_w + mbx];

	m->type = N2MB_I16;
	m->ref = -1;
	m->mvx = 0;
	m->mvy = 0;
	m->qp = (uint8_t)s->qp;
	m->cbp = 0;
	memset(m->nnz, 0, sizeof(m->nnz));
}
