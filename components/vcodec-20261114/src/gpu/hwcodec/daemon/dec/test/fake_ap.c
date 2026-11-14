/* SPDX-License-Identifier: MIT */
/*
 * fake_ap.c -- see fake_ap.h.  Test-only.  The SPS walk is the H.264 7.3.2.1 syntax read through
 * DEC03's AVC_VLD model (n2vld_hook registers 0/8/ue), so the geometry the protocol receives comes
 * from a bit-level parse of the real stream, not from a configured literal.
 */
#define _POSIX_C_SOURCE 200809L

#include "fake_ap.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "n2vld.h"

#define FAKE_MAX_POOL 16u

enum { S_FREE = 0, S_INFLIGHT = 1, S_DISPLAY = 2, S_HELD = 3 };

struct ent {
	uint32_t handle, bytes;
	uint8_t state;
};

struct pic {
	uint32_t index;
	uint64_t pts;
	int32_t poc;
};

struct fake_ap {
	struct fake_ap_cfg cfg;
	uint32_t w, h, stride, buf_h, dpb;
	uint32_t geometry_valid;
	struct ent pool[FAKE_MAX_POOL];
	unsigned nent;
	struct pic pend[FAKE_MAX_POOL];
	unsigned npend;
	struct pic disp[FAKE_MAX_POOL];
	unsigned ndisp;
	uint32_t poc_next;
	int fail_op, fail_err;
	int changed_pending;
	uint32_t changed_seen;
	int have_override;
	struct fake_ap_geometry override_geo;
	struct n2vld *vld;
	struct fake_ap_stats st;
	uint32_t last_pic_index;
	uint64_t last_pic_pts;
};

/* ------------------------------------------------------------------ n2vld SPS walk */

static uint32_t
vld_rd(struct n2vld *v, uint32_t reg)
{
	uint32_t val = 0xdeadbeefu;

	if (n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + reg * 4u, 0, &val) != 1) {
		return 0xdeadbeefu;
	}
	return val;
}

static uint32_t
vld_ue(struct n2vld *v)
{
	return vld_rd(v, N2VLD_AVC_UE);
}

/* n raw bits.  n2vld's AVC window read advances first and returns the *next* 32-bit window, so the
 * consumed value is taken from a peek before the advance (the same convention the vendor's barrel
 * shifter uses: t_vld's literal SPS pass is the model's own test). */
static uint32_t
vld_bits(struct n2vld *v, unsigned n)
{
	uint32_t peek = vld_rd(v, 0);

	(void)vld_rd(v, n);
	return n == 32u ? peek : (peek >> (32u - n));
}

static int
is_high_profile(uint32_t p)
{
	switch (p) {
	case 100: case 110: case 122: case 244: case 44: case 83: case 86: case 118:
	case 128: case 138: case 139: case 134: case 135:
		return 1;
	default:
		return 0;
	}
}

/* Find the first NAL of `type` (Annex B) in `au`.  Returns 1 and the NAL start (with its 3- or
 * 4-byte start code), or 0. */
static int
find_nal(const uint8_t *au, size_t bytes, unsigned type, const uint8_t **start, size_t *len)
{
	size_t i = 0;

	while (i + 3 <= bytes) {
		if (au[i] == 0 && au[i + 1] == 0 && au[i + 2] == 1) {
			size_t j = i + 3;

			if (i > 0 && au[i - 1] == 0) {
				i--;
			}
			if (j < bytes && (au[j] & 0x1f) == type) {
				size_t k = j + 1;

				while (k + 3 <= bytes) {
					if (au[k] == 0 && au[k + 1] == 0 && au[k + 2] == 1) {
						break;
					}
					k++;
				}
				*start = au + i;
				*len = k - i;
				return 1;
			}
			i = j;
			continue;
		}
		i++;
	}
	return 0;
}

int
fake_ap_parse_sps(struct fake_ap *f, const void *au, size_t bytes, struct fake_ap_geometry *g)
{
	const uint8_t *nal = NULL;
	size_t nlen = 0;

	if (!f || !au || !g || !f->vld) {
		return 0;
	}
	if (!find_nal((const uint8_t *)au, bytes, 7, &nal, &nlen) || nlen < 5) {
		return 0;
	}
	if (n2vld_set_bytes(f->vld, nal, (uint32_t)nlen) != 0) {
		return 0;
	}
	uint32_t peek = vld_rd(f->vld, 0);

	if (peek == 0x00000001u) {
		(void)vld_bits(f->vld, 8);               /* 4-byte start code: drop the leading zero */
		peek = vld_rd(f->vld, 0);
	}
	if ((peek & 0xffffff00u) != 0x00000100u) {
		return 0;
	}
	(void)vld_bits(f->vld, 32);                      /* start code + NAL header */
	uint32_t profile = vld_bits(f->vld, 8);
	(void)vld_bits(f->vld, 8);                       /* constraint flags */
	(void)vld_bits(f->vld, 8);                       /* level_idc */
	uint32_t chroma = 1;
	int separate_colour = 0;

	if (is_high_profile(profile)) {
		chroma = vld_ue(f->vld);
		if (chroma == 3u) {
			separate_colour = (int)vld_bits(f->vld, 1);
		}
		(void)vld_ue(f->vld);                        /* bit_depth_luma_minus8 */
		(void)vld_ue(f->vld);                        /* bit_depth_chroma_minus8 */
		(void)vld_bits(f->vld, 1);                    /* qpprime_y_zero_transform_bypass */
		if (vld_bits(f->vld, 1) != 0u) {
			return 0;                            /* scaling lists: not modelled here */
		}
	}
	uint32_t sps_id = vld_ue(f->vld);
	uint32_t log2fn = vld_ue(f->vld);
	(void)sps_id;
	(void)log2fn;
	uint32_t poc_type = vld_ue(f->vld);

	if (poc_type == 0u) {
		(void)vld_ue(f->vld);                        /* log2_max_pic_order_cnt_lsb_minus4 */
	} else if (poc_type == 1u) {
		(void)vld_bits(f->vld, 1);
		(void)vld_rd(f->vld, N2VLD_AVC_SE);
		(void)vld_rd(f->vld, N2VLD_AVC_SE);
		(void)vld_ue(f->vld);
	}
	uint32_t maxref = vld_ue(f->vld);
	uint32_t gaps = vld_bits(f->vld, 1);
	uint32_t w_mbs = vld_ue(f->vld) + 1u;
	uint32_t h_map = vld_ue(f->vld) + 1u;
	(void)maxref;
	(void)gaps;
	uint32_t frame_mbs_only = vld_bits(f->vld, 1);
	uint32_t h_mbs = h_map;

	if (!frame_mbs_only) {
		(void)vld_bits(f->vld, 1);                    /* mb_adaptive_frame_field */
		h_mbs = (vld_ue(f->vld) + 1u) * 2u;
	}
	(void)vld_bits(f->vld, 1);                        /* direct_8x8_inference */
	uint32_t crop_l = 0, crop_r = 0, crop_t = 0, crop_b = 0;

	if (vld_bits(f->vld, 1) != 0u) {
		crop_l = vld_ue(f->vld);
		crop_r = vld_ue(f->vld);
		crop_t = vld_ue(f->vld);
		crop_b = vld_ue(f->vld);
	}
	if (w_mbs == 0u || h_mbs == 0u || w_mbs > 256u || h_mbs > 256u) {
		return 0;
	}
	uint32_t coded_w = w_mbs * 16u;
	uint32_t coded_h = h_mbs * 16u;
	uint32_t unit_x = separate_colour ? 1u : (chroma == 3u ? 1u : (chroma == 2u ? 2u : 2u));
	uint32_t unit_y = separate_colour ? 1u : (chroma == 3u ? 1u : (chroma == 2u ? 1u : 2u));

	unit_y *= (2u - frame_mbs_only);
	uint32_t cw = crop_l + crop_r;
	uint32_t ch = crop_t + crop_b;
	uint32_t disp_w = coded_w - cw * unit_x;
	uint32_t disp_h = coded_h - ch * unit_y;

	if (cw * unit_x >= coded_w || ch * unit_y >= coded_h) {
		return 0;
	}
	g->width = disp_w;
	g->height = disp_h;
	g->stride = (coded_w + 15u) & ~15u;
	g->buf_h = (coded_h + 31u) & ~31u;
	g->dpb_frames = f->dpb ? f->dpb : 7u;
	return 1;
}

/* ------------------------------------------------------------------ model */

static void
emit_pic(struct fake_ap *f, const struct pic *p)
{
	if (f->ndisp < FAKE_MAX_POOL) {
		f->disp[f->ndisp++] = *p;
	}
	return;
}

/* Move delayed pictures to the display queue, modelling display order. */
static void
drain_pending(struct fake_ap *f, int all)
{
	while (f->npend > 0) {
		if (!all && f->npend <= f->cfg.reorder) {
			return;
		}
		struct pic p;

		if (f->cfg.swap && f->npend >= 2 && (!all || f->npend >= 2)) {
			/* B-frame-ish order: the second picture is displayed before the first. */
			p = f->pend[1];
			struct pic q = f->pend[0];

			memmove(&f->pend[0], &f->pend[2], (f->npend - 2u) * sizeof(f->pend[0]));
			f->npend -= 2u;
			emit_pic(f, &p);
			emit_pic(f, &q);
			continue;
		}
		p = f->pend[0];
		memmove(&f->pend[0], &f->pend[1], (f->npend - 1u) * sizeof(f->pend[0]));
		f->npend--;
		emit_pic(f, &p);
	}
}

static int
pool_free_index(struct fake_ap *f, uint32_t *out)
{
	for (unsigned i = 0; i < f->nent; i++) {
		if (f->pool[i].state == S_FREE) {
			*out = i;
			return 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ buffer files (e2e) */

static int
path_of(const struct fake_ap *f, uint32_t handle, char *buf, size_t buflen)
{
	if (!f->cfg.dma_dir) {
		return -ENOSYS;
	}
	int n = snprintf(buf, buflen, "%s/%u", f->cfg.dma_dir, handle);

	return (n > 0 && (size_t)n < buflen) ? 0 : -ENAMETOOLONG;
}

int
fake_ap_read_input(const struct fake_ap *f, uint32_t handle, uint32_t bytes, uint8_t *out)
{
	char path[256];
	int fd;
	ssize_t got;

	if (path_of(f, handle, path, sizeof(path)) != 0) {
		return -ENOSYS;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		return -errno;
	}
	got = read(fd, out, bytes);
	close(fd);
	return got == (ssize_t)bytes ? 0 : -EIO;
}

static int
write_pattern(const struct fake_ap *f, uint32_t handle, uint32_t y_bytes, uint64_t pts, uint32_t index)
{
	char path[256];
	uint8_t *buf;
	int fd;
	ssize_t wrote;
	uint32_t total = y_bytes + y_bytes / 2u;

	if (path_of(f, handle, path, sizeof(path)) != 0) {
		return -ENOSYS;
	}
	buf = (uint8_t *)malloc(total);
	if (!buf) {
		return -ENOMEM;
	}
	for (uint32_t i = 0; i < total; i++) {
		buf[i] = (uint8_t)((i * 131u) ^ (uint32_t)pts ^ (index * 7u) ^ f->cfg.marker);
	}
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		free(buf);
		return -errno;
	}
	wrote = write(fd, buf, total);
	close(fd);
	free(buf);
	return wrote == (ssize_t)total ? 0 : -EIO;
}

int
fake_ap_check_picture(const struct fake_ap *f, uint32_t handle, uint32_t y_bytes, uint64_t pts,
                      uint32_t index)
{
	char path[256];
	uint8_t *buf;
	int fd;
	ssize_t got;
	uint32_t total = y_bytes + y_bytes / 2u;
	int ok = 1;

	if (path_of(f, handle, path, sizeof(path)) != 0) {
		return 0;
	}
	buf = (uint8_t *)malloc(total);
	if (!buf) {
		return 0;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		free(buf);
		return 0;
	}
	got = read(fd, buf, total);
	close(fd);
	if (got != (ssize_t)total) {
		free(buf);
		return 0;
	}
	for (uint32_t i = 0; i < total && ok; i++) {
		if (buf[i] != (uint8_t)((i * 131u) ^ (uint32_t)pts ^ (index * 7u) ^ f->cfg.marker)) {
			ok = 0;
		}
	}
	free(buf);
	return ok;
}

/* ------------------------------------------------------------------ ops */

static int
op_open(void *ctx, uint32_t codec)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	if (!f) {
		return -EINVAL;
	}
	if (f->fail_op == 0) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	if (codec != N2VCD_CODEC_H264 && codec != N2VCD_CODEC_HEVC) {
		return -EOPNOTSUPP;
	}
	f->st.opens++;
	f->geometry_valid = 0;
	f->nent = 0;
	f->npend = 0;
	f->ndisp = 0;
	f->changed_pending = 0;
	f->changed_seen = 0;
	f->poc_next = 0;
	memset(f->pool, 0, sizeof(f->pool));
	return 0;
}

static int
op_header(void *ctx, const struct n2vcd_dec_ap_buffer *in, uint32_t in_bytes,
          struct n2vcd_dec_ap_geometry *geo)
{
	struct fake_ap *f = (struct fake_ap *)ctx;
	struct fake_ap_geometry g;

	if (!f || !in || !geo || in_bytes == 0u) {
		return -EINVAL;
	}
	if (f->fail_op == 1) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	f->st.headers++;
	memset(&g, 0, sizeof(g));
	int parsed = 0;

	if (f->changed_pending && f->cfg.changed_w != 0u) {
		g.width = f->cfg.changed_w;
		g.height = f->cfg.changed_h;
		g.stride = (f->cfg.changed_w + 15u) & ~15u;
		g.buf_h = f->cfg.changed_buf_h ? f->cfg.changed_buf_h : ((f->cfg.changed_h + 31u) & ~31u);
		g.dpb_frames = f->dpb ? f->dpb : 7u;
		f->changed_pending = 0;
		parsed = 1;
	}
	if (!parsed && f->cfg.dma_dir) {
		uint8_t *au = (uint8_t *)malloc(in_bytes);

		if (au) {
			if (fake_ap_read_input(f, in->handle, in_bytes, au) == 0) {
				parsed = fake_ap_parse_sps(f, au, in_bytes, &g);
			}
			free(au);
		}
	}
	if (!parsed && f->have_override) {
		g = f->override_geo;
		parsed = 1;
	}
	if (!parsed) {
		g.width = f->cfg.crop_w ? f->cfg.crop_w : f->cfg.pic_w;
		g.height = f->cfg.crop_h ? f->cfg.crop_h : f->cfg.pic_h;
		g.stride = f->cfg.buf_w ? f->cfg.buf_w : ((g.width + 15u) & ~15u);
		g.buf_h = f->cfg.buf_h ? f->cfg.buf_h : ((g.height + 31u) & ~31u);
		g.dpb_frames = f->cfg.dpb_frames ? f->cfg.dpb_frames : 7u;
	}
	if (g.width == 0u || g.height == 0u) {
		return -EINVAL;
	}
	f->w = g.width;
	f->h = g.height;
	f->stride = g.stride;
	f->buf_h = g.buf_h;
	f->dpb = g.dpb_frames;
	f->geometry_valid = 1;
	memset(geo, 0, sizeof(*geo));
	geo->width = g.width;
	geo->height = g.height;
	geo->stride = g.stride;
	geo->buf_h = g.buf_h;
	geo->out_buf_bytes = g.stride * g.buf_h + g.stride * g.buf_h / 2u;
	geo->dpb_frames = g.dpb_frames;
	geo->fourcc = N2VCD_FMT_MM21;
	return 0;
}

static int
op_add_buffer(void *ctx, uint32_t index, const struct n2vcd_dec_ap_buffer *y,
              const struct n2vcd_dec_ap_buffer *c)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	(void)c;
	if (!f || !y || index != f->nent || f->nent >= FAKE_MAX_POOL) {
		return -EINVAL;
	}
	if (f->fail_op == 2) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	f->pool[f->nent].handle = y->handle;
	f->pool[f->nent].bytes = y->bytes;
	f->pool[f->nent].state = S_FREE;
	f->nent++;
	f->st.adds++;
	if (f->nent > f->st.pool_max) {
		f->st.pool_max = f->nent;
	}
	return 0;
}

static int
op_input(void *ctx, const struct n2vcd_dec_ap_buffer *in, uint32_t in_bytes, uint64_t pts,
         uint32_t flags, struct n2vcd_dec_ap_result *res)
{
	struct fake_ap *f = (struct fake_ap *)ctx;
	uint32_t index;

	if (!f || !in || !res || in_bytes == 0u) {
		return -EINVAL;
	}
	if (f->fail_op == 3) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	if (!f->geometry_valid) {
		return -EINVAL;
	}
	f->st.inputs++;
	f->st.last_in_bytes = in_bytes;
	f->st.last_pts = pts;
	f->st.last_flags = flags;
	f->changed_seen++;
	if (f->cfg.changed_after != 0u && f->changed_seen == f->cfg.changed_after) {
		f->changed_pending = 1;
		memset(res, 0, sizeof(*res));
		res->changed = 1;
		return 0;
	}
	if (!pool_free_index(f, &index)) {
		return -EAGAIN;
	}
	f->pool[index].state = S_INFLIGHT;
	if (f->cfg.dma_dir && write_pattern(f, f->pool[index].handle, f->stride * f->buf_h, pts,
	                                    (uint32_t)(index + 1u)) != 0) {
		f->pool[index].state = S_FREE;
		return -EIO;
	}
	f->pend[f->npend].index = index;
	f->pend[f->npend].pts = pts;
	f->pend[f->npend].poc = (int32_t)f->poc_next++;
	f->npend++;
	drain_pending(f, 0);
	for (unsigned i = 0; i < f->ndisp; i++) {
		f->pool[f->disp[i].index].state = S_DISPLAY;
	}
	f->st.outstanding_max = f->ndisp;
	memset(res, 0, sizeof(*res));
	res->pictures_ready = f->ndisp;
	res->error_map = 0;
	return 0;
}

static int
op_picture(void *ctx, struct n2vcd_dec_ap_picture *pic)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	if (!f || !pic) {
		return -EINVAL;
	}
	if (f->fail_op == 4) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	if (f->ndisp == 0u) {
		return 0;
	}
	struct pic p = f->disp[0];

	memmove(&f->disp[0], &f->disp[1], (f->ndisp - 1u) * sizeof(f->disp[0]));
	f->ndisp--;
	memset(pic, 0, sizeof(*pic));
	pic->index = p.index;
	pic->pts = p.pts;
	pic->poc = p.poc;
	pic->pictures_ready = f->ndisp;
	f->last_pic_index = p.index;
	f->last_pic_pts = p.pts;
	f->st.pictures++;
	return 1;
}

static int
op_release(void *ctx, uint32_t index)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	if (!f || index >= f->nent) {
		return -EINVAL;
	}
	if (f->fail_op == 5) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	if (f->pool[index].state != S_DISPLAY && f->pool[index].state != S_HELD) {
		return -EINVAL;
	}
	f->pool[index].state = S_FREE;
	f->st.releases++;
	return 0;
}

static int
op_free_buffers(void *ctx)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	if (!f) {
		return -EINVAL;
	}
	if (f->fail_op == 6) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	for (unsigned i = 0; i < f->nent; i++) {
		if (f->pool[i].state == S_DISPLAY || f->pool[i].state == S_INFLIGHT || f->pool[i].state == S_HELD) {
			return -EBUSY;
		}
	}
	f->nent = 0;
	f->npend = 0;
	f->ndisp = 0;
	f->geometry_valid = 0;
	f->st.frees++;
	return 0;
}

static int
op_flush(void *ctx, uint32_t *pictures_ready)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	if (!f) {
		return -EINVAL;
	}
	if (f->fail_op == 7) {
		int e = f->fail_err;

		f->fail_op = -1;
		return e;
	}
	f->st.flushes++;
	drain_pending(f, 1);
	for (unsigned i = 0; i < f->ndisp; i++) {
		f->pool[f->disp[i].index].state = S_DISPLAY;
	}
	if (pictures_ready) {
		*pictures_ready = f->ndisp;
	}
	return 0;
}

static void
op_close(void *ctx)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	if (!f) {
		return;
	}
	f->st.closes++;
	f->nent = 0;
	f->npend = 0;
	f->ndisp = 0;
	f->geometry_valid = 0;
}

static int
op_guest_stopped(void *ctx, uint32_t *code)
{
	struct fake_ap *f = (struct fake_ap *)ctx;

	if (!f || !f->cfg.guest_stop) {
		if (code) {
			*code = 0;
		}
		return 0;
	}
	if (code) {
		*code = f->cfg.guest_code;
	}
	return 1;
}

static const struct n2vcd_dec_ap_ops g_ops = {
	.open = op_open,
	.header = op_header,
	.add_buffer = op_add_buffer,
	.input = op_input,
	.picture = op_picture,
	.release = op_release,
	.free_buffers = op_free_buffers,
	.flush = op_flush,
	.close = op_close,
	.guest_stopped = op_guest_stopped,
};

const struct n2vcd_dec_ap_ops *
fake_ap_ops(void)
{
	return &g_ops;
}

struct fake_ap *
fake_ap_create(const struct fake_ap_cfg *cfg)
{
	struct fake_ap *f;

	if (!cfg) {
		return NULL;
	}
	f = (struct fake_ap *)calloc(1, sizeof(*f));
	if (!f) {
		return NULL;
	}
	f->cfg = *cfg;
	f->vld = n2vld_create(NULL);
	if (!f->vld) {
		free(f);
		return NULL;
	}
	f->fail_op = -1;
	return f;
}

void
fake_ap_destroy(struct fake_ap *f)
{
	if (!f) {
		return;
	}
	n2vld_destroy(f->vld);
	free(f);
}

void
fake_ap_fail_next(struct fake_ap *f, int op, int err)
{
	if (f) {
		f->fail_op = op;
		f->fail_err = err;
	}
}

void
fake_ap_stats(const struct fake_ap *f, struct fake_ap_stats *out)
{
	if (f && out) {
		*out = f->st;
	}
}

void
fake_ap_set_guest_stop(struct fake_ap *f, uint32_t code)
{
	if (f) {
		f->cfg.guest_stop = 1;
		f->cfg.guest_code = code;
	}
}

void
fake_ap_set_geometry(struct fake_ap *f, const struct fake_ap_geometry *g)
{
	if (f && g) {
		f->override_geo = *g;
		f->have_override = 1;
	}
}

uint32_t
fake_ap_pool_count(const struct fake_ap *f)
{
	return f ? f->nent : 0u;
}
