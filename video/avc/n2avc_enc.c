/* SPDX-License-Identifier: MIT */
/*
 * n2avc_enc.c -- the public encoder object of the WP03 H.264 encoder: n2avc_open / headers / encode /
 * reconfigure / reconstruction / close (include/n2avc.h).
 *
 * ITU-T H.264 (03/2010): 7.3.4 (slice_data, mb_skip_run), 7.3.2.8 (slice layer), 7.4.1.2.3/7.4.1.2.4 (IDR and
 * reference picture marking), 8.2.1 (picture decoding order).  Threads: one slice per band of macroblock rows,
 * coded by a pool created in open; slices share nothing writable but their own rows (n2avc_avail() makes the
 * macroblocks of another slice "not available", clause 6.4.9).
 */
#include "n2avc_priv.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ allocation */
static int plane_init(n2avc_plane *pl, uint8_t **base, int w, int h, int pad)
{
	size_t stride = (size_t)w + 2u * (size_t)pad;
	size_t rows = (size_t)h + 2u * (size_t)pad;
	uint8_t *b;

	if (stride > (size_t)0x7fffffff)
		return N2AVC_E_MEMORY;
	b = (uint8_t *)malloc(stride * rows);
	if (!b)
		return N2AVC_E_MEMORY;
	memset(b, 0, stride * rows);
	*base = b;
	pl->p = b + (size_t)pad * stride + (size_t)pad;
	pl->stride = (int)stride;
	return N2AVC_OK;
}

static int frame_init(n2avc_frame *f, uint8_t *base[3], int mb_w, int mb_h)
{
	int rc;
	rc = plane_init(&f->y, &base[0], 16 * mb_w, 16 * mb_h, N2AVC_PAD);
	if (rc != N2AVC_OK)
		return rc;
	rc = plane_init(&f->cb, &base[1], 8 * mb_w, 8 * mb_h, N2AVC_PAD / 2);
	if (rc != N2AVC_OK)
		return rc;
	return plane_init(&f->cr, &base[2], 8 * mb_w, 8 * mb_h, N2AVC_PAD / 2);
}

/* ------------------------------------------------------------------ input conversion */
/* Copies a w x h plane into the MB-aligned destination and repeats the last column/row out to W x H. */
static void fill_plane(n2avc_plane *d, const uint8_t *s, int stride, int w, int h, int W, int H)
{
	int x, y;

	for (y = 0; y < h; y++) {
		uint8_t *dp = d->p + (size_t)y * (size_t)d->stride;
		const uint8_t *sp = s + (size_t)y * (size_t)stride;
		memcpy(dp, sp, (size_t)w);
		for (x = w; x < W; x++)
			dp[x] = dp[w - 1];
	}
	for (y = h; y < H; y++)
		memcpy(d->p + (size_t)y * (size_t)d->stride, d->p + (size_t)(h - 1) * (size_t)d->stride, (size_t)W);
}

static void fill_plane_nv12(n2avc_plane *cb, n2avc_plane *cr, const uint8_t *s, int stride,
			    int cw, int ch, int CW, int CH)
{
	int x, y;

	for (y = 0; y < ch; y++) {
		const uint8_t *sp = s + (size_t)y * (size_t)stride;
		uint8_t *bp = cb->p + (size_t)y * (size_t)cb->stride;
		uint8_t *rp = cr->p + (size_t)y * (size_t)cr->stride;
		for (x = 0; x < cw; x++) {
			bp[x] = sp[2 * x];
			rp[x] = sp[2 * x + 1];
		}
		for (x = cw; x < CW; x++) {
			bp[x] = bp[cw - 1];
			rp[x] = rp[cw - 1];
		}
	}
	for (y = ch; y < CH; y++) {
		memcpy(cb->p + (size_t)y * (size_t)cb->stride, cb->p + (size_t)(ch - 1) * (size_t)cb->stride, (size_t)CW);
		memcpy(cr->p + (size_t)y * (size_t)cr->stride, cr->p + (size_t)(ch - 1) * (size_t)cr->stride, (size_t)CW);
	}
}

static int copy_picture(n2avc_enc *e, const n2avc_picture *p)
{
	int w = (int)e->width, h = (int)e->height;
	int cw = w / 2, ch = h / 2;
	int W = 16 * e->mb_w, H = 16 * e->mb_h;
	int CW = 8 * e->mb_w, CH = 8 * e->mb_h;

	if (p->stride[0] < e->width)
		return N2AVC_E_ARGUMENT;
	fill_plane(&e->src.y, p->plane[0], (int)p->stride[0], w, h, W, H);
	if (p->format == N2AVC_FMT_NV12) {
		if (p->stride[1] < e->width || !p->plane[1])
			return N2AVC_E_ARGUMENT;
		fill_plane_nv12(&e->src.cb, &e->src.cr, p->plane[1], (int)p->stride[1], cw, ch, CW, CH);
	} else {
		if (!p->plane[1] || !p->plane[2])
			return N2AVC_E_ARGUMENT;
		if (p->stride[1] < (uint32_t)cw || p->stride[2] < (uint32_t)cw)
			return N2AVC_E_ARGUMENT;
		fill_plane(&e->src.cb, p->plane[1], (int)p->stride[1], cw, ch, CW, CH);
		fill_plane(&e->src.cr, p->plane[2], (int)p->stride[2], cw, ch, CW, CH);
	}
	return N2AVC_OK;
}

/* ------------------------------------------------------------------ borders */
/* Repeats the edge samples of the coded picture out into the N2AVC_PAD border (as include/n2avc.h promises the
 * reconstruction and the reference to be padded). */
static void extend_plane(n2avc_plane *pl, int w, int h, int pad)
{
	uint8_t *base = pl->p;
	int stride = pl->stride;
	int x, y;

	for (y = 0; y < h; y++) {
		uint8_t *row = base + (size_t)y * (size_t)stride;
		for (x = 1; x <= pad; x++) {
			row[-x] = row[0];
			row[w - 1 + x] = row[w - 1];
		}
	}
	for (y = 1; y <= pad; y++) {
		memcpy(base - (size_t)y * (size_t)stride - pad, base - pad, (size_t)(w + 2 * pad));
		memcpy(base + (size_t)(h - 1 + y) * (size_t)stride - pad,
		       base + (size_t)(h - 1) * (size_t)stride - pad, (size_t)(w + 2 * pad));
	}
}

static void extend_frame(n2avc_frame *f, int mb_w, int mb_h)
{
	extend_plane(&f->y, 16 * mb_w, 16 * mb_h, N2AVC_PAD);
	extend_plane(&f->cb, 8 * mb_w, 8 * mb_h, N2AVC_PAD / 2);
	extend_plane(&f->cr, 8 * mb_w, 8 * mb_h, N2AVC_PAD / 2);
}

/* ------------------------------------------------------------------ slice coding */
static void slice_run(n2avc_enc *e, int idx)
{
	n2avc_slice *s = &e->sl[idx];
	int mby, mbx;
	int intra_slice = (s->slice_type != 0) || !s->inter_priv;

	n2bw_init(&s->bw, e->sl_buf[idx], e->sl_cap[idx]);
	n2avc_write_slice_header(s, s->first_row * s->mb_w, e->cur_idr, e->cur_frame_num, e->cur_idr_pic_id);
	s->skip_run = 0;
	for (mby = s->first_row; mby < s->end_row; mby++) {
		for (mbx = 0; mbx < s->mb_w; mbx++) {
			if (!intra_slice) {
				int r = n2avc_p_mb(s, mbx, mby);
				if (r == 2) {
					n2avc_flush_skip_run(s);
					n2avc_intra_mb(s, mbx, mby);
				}
			} else {
				n2avc_intra_mb(s, mbx, mby);
			}
		}
	}
	if (s->slice_type == 0 && s->skip_run > 0)
		n2avc_flush_skip_run(s);
	n2bw_trailing(&s->bw);
	(void)n2bw_flush(&s->bw);
}

static void *slice_worker(void *arg)
{
	struct n2avc_worker *w = (struct n2avc_worker *)arg;
	n2avc_enc *e = w->enc;
	unsigned gen = 0;

	pthread_mutex_lock(&e->mu);
	for (;;) {
		while (!e->quit && e->job_gen == gen)
			pthread_cond_wait(&e->cv_job, &e->mu);
		if (e->quit)
			break;
		gen = e->job_gen;
		pthread_mutex_unlock(&e->mu);
		slice_run(e, w->idx);
		pthread_mutex_lock(&e->mu);
		e->done++;
		pthread_cond_signal(&e->cv_done);
	}
	pthread_mutex_unlock(&e->mu);
	return NULL;
}

/* ------------------------------------------------------------------ packet assembly */
static int emit_nal(n2avc_enc *e, size_t *total, const uint8_t *rbsp, size_t n, int ref_idc, int type, int annexb)
{
	size_t room, got;

	if (n == 0)
		return N2AVC_OK;
	if (*total + 8 + n + n / 2 > e->out_cap)
		return N2AVC_E_OVERFLOW;
	if (annexb) {
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 1;
	} else {
		if (n + n / 2 + 1 > 0xffffffffu)
			return N2AVC_E_OVERFLOW;
		e->out[(*total)++] = 0;                         /* 4-byte big-endian NAL length, filled in after */
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 0;
	}
	room = e->out_cap - *total;
	got = n2avc_nal(e->out + *total, room, rbsp, n, ref_idc, type);
	if (!got)
		return N2AVC_E_OVERFLOW;
	if (!annexb) {
		size_t len = got;
		e->out[*total - 4] = (uint8_t)(len >> 24);
		e->out[*total - 3] = (uint8_t)(len >> 16);
		e->out[*total - 2] = (uint8_t)(len >> 8);
		e->out[*total - 1] = (uint8_t)len;
	}
	*total += got;
	return N2AVC_OK;
}

/* A NAL unit that is already packed (the parameter sets): start code or length, then the bytes as they are. */
static int emit_raw_nal(n2avc_enc *e, size_t *total, const uint8_t *nal, size_t n, int annexb)
{
	if (*total + 4 + n > e->out_cap)
		return N2AVC_E_OVERFLOW;
	if (annexb) {
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 0;
		e->out[(*total)++] = 1;
	} else {
		e->out[(*total)++] = (uint8_t)(n >> 24);
		e->out[(*total)++] = (uint8_t)(n >> 16);
		e->out[(*total)++] = (uint8_t)(n >> 8);
		e->out[(*total)++] = (uint8_t)n;
	}
	memcpy(e->out + *total, nal, n);
	*total += n;
	return N2AVC_OK;
}

/* ------------------------------------------------------------------ public interface */
int n2avc_open(const n2avc_config *cfg, n2avc_enc **out)
{
	n2avc_enc *e;
	int rc, i;

	if (!cfg || !out)
		return N2AVC_E_ARGUMENT;
	*out = NULL;
	if (cfg->size != sizeof(n2avc_config))
		return N2AVC_E_ARGUMENT;
	if (cfg->width < 16 || cfg->width > 4096 || cfg->height < 16 || cfg->height > 2304)
		return N2AVC_E_ARGUMENT;
	if ((cfg->width & 1) || (cfg->height & 1))
		return N2AVC_E_ARGUMENT;                /* 4:2:0 needs even dimensions */
	if (cfg->threads > N2AVC_MAX_THREADS)
		return N2AVC_E_ARGUMENT;
	if (cfg->search_range > 64)
		return N2AVC_E_ARGUMENT;
	if (cfg->flags & ~(N2AVC_F_ANNEXB | N2AVC_F_FULLRANGE | N2AVC_F_DEBLOCK | N2AVC_F_INTRA_ONLY |
			   N2AVC_F_HEADERS_IN_KEYFRAMES))
		return N2AVC_E_ARGUMENT;
	if (cfg->flags & N2AVC_F_DEBLOCK)
		return N2AVC_E_ARGUMENT;                /* stage 2: not implemented, and it must not pretend to be */
	if (cfg->qp > 51 || (cfg->qp && cfg->qp < 10))
		return N2AVC_E_ARGUMENT;
	if (cfg->fps_num == 0 && cfg->fps_den != 0)
		return N2AVC_E_ARGUMENT;

	e = (n2avc_enc *)calloc(1, sizeof *e);
	if (!e)
		return N2AVC_E_MEMORY;
	e->cfg = *cfg;
	e->width = cfg->width;
	e->height = cfg->height;
	e->mb_w = (int)((cfg->width + 15) / 16);
	e->mb_h = (int)((cfg->height + 15) / 16);
	e->fps_num = cfg->fps_num ? cfg->fps_num : 30;
	e->fps_den = cfg->fps_den ? cfg->fps_den : 1;
	e->cfg.fps_num = e->fps_num;
	e->cfg.fps_den = e->fps_den;
	e->cfg.qp = cfg->qp ? cfg->qp : 26;
	e->cfg.keyint = cfg->keyint ? cfg->keyint : 60;
	e->cfg.search_range = cfg->search_range ? cfg->search_range : 16;
	e->level_idc = n2avc_pick_level(e->width, e->height, e->fps_num / (e->fps_den ? e->fps_den : 1));
	e->nthreads = cfg->threads ? (int)cfg->threads : 1;
	if (e->nthreads > e->mb_h)
		e->nthreads = e->mb_h;
	if (e->cfg.colour_primaries == 0)
		e->cfg.colour_primaries = (e->height >= 720) ? 1 : 6;
	if (e->cfg.transfer == 0)
		e->cfg.transfer = (e->height >= 720) ? 1 : 6;
	if (e->cfg.matrix == 0)
		e->cfg.matrix = (e->height >= 720) ? 1 : 6;

	rc = frame_init(&e->src, e->src_base, e->mb_w, e->mb_h);
	if (rc != N2AVC_OK)
		goto fail;
	for (i = 0; i < 2; i++) {
		rc = frame_init(&e->frm[i], e->frm_base[i], e->mb_w, e->mb_h);
		if (rc != N2AVC_OK)
			goto fail;
	}
	e->mb = (n2avc_mbinfo *)calloc((size_t)(e->mb_w * e->mb_h), sizeof *e->mb);
	if (!e->mb) {
		rc = N2AVC_E_MEMORY;
		goto fail;
	}

	/* One RBSP buffer per slice: 1.5 x the raw size of the slice's own area, plus 64 KiB (I_PCM is 384 bytes
	 * per macroblock, the raw size of a macroblock is 384 samples, so this always holds the worst case). */
	for (i = 0; i < e->nthreads; i++) {
		int rows = e->mb_h / e->nthreads + (i < e->mb_h % e->nthreads ? 1 : 0);
		size_t band = (size_t)(16 * e->mb_w) * (size_t)(16 * rows) * 3u / 2u;
		e->sl_cap[i] = band + band / 2u + 65536u;
		e->sl_buf[i] = (uint8_t *)malloc(e->sl_cap[i]);
		if (!e->sl_buf[i]) {
			rc = N2AVC_E_MEMORY;
			goto fail;
		}
	}
	/* Worst case a picture can occupy: every macroblock as I_PCM (about its raw size) plus the escaping of
	 * clause 7.4.1, which can grow a NAL by half again. */
	e->out_cap = (size_t)(16 * e->mb_w) * (size_t)(16 * e->mb_h) * 3u + 3u * 65536u + 4096u;
	e->out = (uint8_t *)malloc(e->out_cap);
	if (!e->out) {
		rc = N2AVC_E_MEMORY;
		goto fail;
	}
	e->rc = n2avc_rc_open(&e->cfg);
	if (!e->rc) {
		rc = N2AVC_E_MEMORY;
		goto fail;
	}
	rc = n2avc_build_ps(e);
	if (rc != N2AVC_OK)
		goto fail;

	if (e->nthreads > 1) {
		pthread_mutex_init(&e->mu, NULL);
		pthread_cond_init(&e->cv_job, NULL);
		pthread_cond_init(&e->cv_done, NULL);
		e->job_gen = 0;
		e->done = 0;
		e->quit = 0;
		for (i = 0; i < e->nthreads - 1; i++) {
			e->wk[i].enc = e;
			e->wk[i].idx = i;
			if (pthread_create(&e->th[i], NULL, slice_worker, &e->wk[i]) != 0)
				break;
		}
		e->nthr_created = i;
		if (e->nthr_created != e->nthreads - 1) {
			/* Fewer workers than slices: fall back to one slice per picture on this thread. */
			pthread_mutex_lock(&e->mu);
			e->quit = 1;
			pthread_cond_broadcast(&e->cv_job);
			pthread_mutex_unlock(&e->mu);
			for (i = 0; i < e->nthr_created; i++)
				pthread_join(e->th[i], NULL);
			pthread_cond_destroy(&e->cv_done);
			pthread_cond_destroy(&e->cv_job);
			pthread_mutex_destroy(&e->mu);
			e->nthr_created = 0;
			e->nthreads = 1;
		}
	}
	*out = e;
	return N2AVC_OK;

fail:
	n2avc_close(e);
	return rc;
}

int n2avc_headers(n2avc_enc *e, const uint8_t **sps, size_t *sps_size, const uint8_t **pps, size_t *pps_size)
{
	if (!e || !sps || !sps_size || !pps || !pps_size)
		return N2AVC_E_ARGUMENT;
	if (e->failed)
		return N2AVC_E_STATE;
	*sps = e->sps;
	*sps_size = e->sps_n;
	*pps = e->pps;
	*pps_size = e->pps_n;
	return N2AVC_OK;
}

int n2avc_encode(n2avc_enc *e, const n2avc_picture *picture, int force_idr, n2avc_packet *out)
{
	int idr, qp, i, rc;
	size_t total = 0;
	int annexb;

	if (!e || !out)
		return N2AVC_E_ARGUMENT;
	if (e->failed)
		return N2AVC_E_STATE;
	if (!picture || !picture->plane[0])
		return N2AVC_E_ARGUMENT;
	if (picture->format != N2AVC_FMT_NV12 && picture->format != N2AVC_FMT_I420)
		return N2AVC_E_ARGUMENT;

	idr = (e->pictures == 0) || force_idr || (e->cfg.flags & N2AVC_F_INTRA_ONLY) ||
	      (e->cfg.keyint && e->since_idr >= (int)e->cfg.keyint);
	qp = n2avc_rc_picture_qp(e->rc, idr);
	if (qp < 10)
		qp = 10;
	if (qp > 51)
		qp = 51;

	rc = copy_picture(e, picture);
	if (rc != N2AVC_OK)
		return rc;

	e->cur_idr = idr;
	e->cur_frame_num = idr ? 0 : (e->frame_num & 0xff);
	e->cur_idr_pic_id = e->idr_pic_id;
	annexb = (e->cfg.flags & N2AVC_F_ANNEXB) != 0;

	for (i = 0; i < e->nthreads; i++) {
		n2avc_slice *s = &e->sl[i];
		int base_rows = e->mb_h / e->nthreads;
		int extra = e->mb_h % e->nthreads;
		s->slice_type = idr ? 2 : 0;
		s->qp = qp;
		s->qpc = n2avc_qpc(qp);
		s->mb_w = e->mb_w;
		s->mb_h = e->mb_h;
		s->first_row = i * base_rows + (i < extra ? i : extra);
		s->end_row = s->first_row + base_rows + (i < extra ? 1 : 0);
		s->src = &e->src;
		s->rec = &e->frm[e->cur];
		s->ref = idr ? NULL : &e->frm[1 - e->cur];
		s->mb = e->mb;
		s->search_range = (int)e->cfg.search_range;
		s->skip_run = 0;
		s->inter_priv = NULL;
	}
	if (!idr) {
		for (i = 0; i < e->nthreads; i++)
			e->sl[i].inter_priv = n2avc_inter_slice_begin(&e->sl[i]);
	}

	if (e->nthreads > 1) {
		pthread_mutex_lock(&e->mu);
		e->done = 0;
		e->job_gen++;
		pthread_cond_broadcast(&e->cv_job);
		pthread_mutex_unlock(&e->mu);
		slice_run(e, e->nthreads - 1);
		pthread_mutex_lock(&e->mu);
		while (e->done < e->nthreads - 1)
			pthread_cond_wait(&e->cv_done, &e->mu);
		pthread_mutex_unlock(&e->mu);
	} else {
		slice_run(e, 0);
	}
	if (!idr) {
		for (i = 0; i < e->nthreads; i++)
			n2avc_inter_slice_end(&e->sl[i]);
	}

	for (i = 0; i < e->nthreads; i++) {
		if (e->sl[i].bw.overflow) {
			e->failed = 1;
			return N2AVC_E_OVERFLOW;
		}
	}

	if (idr && (annexb || (e->cfg.flags & N2AVC_F_HEADERS_IN_KEYFRAMES))) {
		rc = emit_raw_nal(e, &total, e->sps, e->sps_n, annexb);
		if (rc != N2AVC_OK)
			goto overflow;
		rc = emit_raw_nal(e, &total, e->pps, e->pps_n, annexb);
		if (rc != N2AVC_OK)
			goto overflow;
	}
	for (i = 0; i < e->nthreads; i++) {
		n2avc_slice *s = &e->sl[i];
		size_t n = s->bw.pos;
		rc = emit_nal(e, &total, e->sl_buf[i], n, idr ? 3 : 2, idr ? 5 : 1, annexb);
		if (rc != N2AVC_OK)
			goto overflow;
	}

	{
		uint32_t k, n = 0;
		for (k = 0; k < (uint32_t)(e->mb_w * e->mb_h); k++)
			if (e->mb[k].type == N2MB_IPCM)
				n++;
		e->dbg_ipcm_last = n;
		e->dbg_ipcm_total += n;
	}
	extend_frame(&e->frm[e->cur], e->mb_w, e->mb_h);
	e->cur = 1 - e->cur;
	e->pictures++;
	if (idr) {
		e->since_idr = 0;
		e->frame_num = 1;
		e->idr_pic_id ^= 1;
	} else {
		e->since_idr++;
		e->frame_num = (e->frame_num + 1) & 0xff;
	}

	out->data = e->out;
	out->size = total;
	out->pts = picture->pts;
	out->keyframe = idr ? 1u : 0u;
	out->qp = (uint32_t)qp;
	out->frame_num = (uint32_t)e->cur_frame_num;
	n2avc_rc_picture_done(e->rc, idr, qp, total);
	return N2AVC_OK;

overflow:
	e->failed = 1;
	return N2AVC_E_OVERFLOW;
}

int n2avc_reconfigure(n2avc_enc *e, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den)
{
	if (!e)
		return N2AVC_E_ARGUMENT;
	if (e->failed)
		return N2AVC_E_STATE;
	if (fps_num && fps_den) {
		e->fps_num = fps_num;
		e->fps_den = fps_den;
		e->cfg.fps_num = fps_num;
		e->cfg.fps_den = fps_den;
	}
	if (bitrate)
		e->cfg.bitrate = bitrate;
	/* The SPS is not re-emitted: its timing_info keeps the values of open(). */
	n2avc_rc_reconfigure(e->rc, e->cfg.bitrate, e->fps_num, e->fps_den);
	return N2AVC_OK;
}

int n2avc_reconstruction(n2avc_enc *e, const uint8_t *plane[3], uint32_t stride[3])
{
	int last;

	if (!e || !plane || !stride)
		return N2AVC_E_ARGUMENT;
	if (e->failed)
		return N2AVC_E_STATE;
	if (e->pictures == 0)
		return N2AVC_E_STATE;
	last = 1 - e->cur;
	plane[0] = e->frm[last].y.p;
	plane[1] = e->frm[last].cb.p;
	plane[2] = e->frm[last].cr.p;
	stride[0] = (uint32_t)e->frm[last].y.stride;
	stride[1] = (uint32_t)e->frm[last].cb.stride;
	stride[2] = (uint32_t)e->frm[last].cr.stride;
	return N2AVC_OK;
}

void n2avc_close(n2avc_enc *e)
{
	int i;

	if (!e)
		return;
	if (e->nthr_created > 0) {
		pthread_mutex_lock(&e->mu);
		e->quit = 1;
		pthread_cond_broadcast(&e->cv_job);
		pthread_mutex_unlock(&e->mu);
		for (i = 0; i < e->nthr_created; i++)
			pthread_join(e->th[i], NULL);
		pthread_cond_destroy(&e->cv_done);
		pthread_cond_destroy(&e->cv_job);
		pthread_mutex_destroy(&e->mu);
		e->nthr_created = 0;
	}
	for (i = 0; i < N2AVC_MAX_THREADS; i++)
		free(e->sl_buf[i]);
	free(e->out);
	free(e->sps);
	free(e->pps);
	free(e->mb);
	for (i = 0; i < 3; i++) {
		free(e->src_base[i]);
		free(e->frm_base[0][i]);
		free(e->frm_base[1][i]);
	}
	n2avc_rc_close(e->rc);
	free(e);
}
