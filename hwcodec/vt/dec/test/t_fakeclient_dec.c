/* SPDX-License-Identifier: MIT */
/* t_fakeclient_dec.c -- the scripted decoder client behind the n2vtd_client seam (see the header). */
#include "t_fakeclient_dec.h"
#include "n2vtd_pixels.h"
#include "n2vtd_nal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TFD_MAX_POOL 16
#define TFD_MAX_PENDING 32

struct tfd_pic {
	uint64_t pts;
	uint32_t index;
};

struct tfd_entry {
	int used;
	uint32_t bytes;
	uint8_t *data;
};

struct tfd_conn {
	struct tfd_config cfg;
	struct tfd_entry pool[TFD_MAX_POOL];
	uint32_t npool;
	struct tfd_pic pending[TFD_MAX_PENDING];
	uint32_t npending;
	uint8_t au[1 << 20];
	uint32_t au_bytes;
	uint64_t last_pts;
	uint32_t inputs, registered, released, flushes, header_bytes;
	uint32_t open_max_w, open_max_h, open_format;
	int closed;
	int changed_next;
	int ready_override;
	int have_ready_override;
	uint32_t flush_ready;
	uint32_t auto_index;
	uint32_t codec;
	int sps_seen;
	uint32_t inputs_with_sps, inputs_dropped;
};

static struct tfd_config g_cfg;
static _Atomic(struct tfd_conn *) g_last;
static _Atomic(struct tfd_conn *) g_default;
static _Atomic int g_closed_seen;
static _Atomic uint32_t g_registered_seen;
static _Atomic uint32_t g_released_seen;
static _Atomic uint32_t g_flushes_seen;

static void fill_output(struct tfd_conn *c, struct tfd_entry *b, uint64_t pts);

/* dp1-fix: does an Annex B buffer hold a NAL of this kind?  kind 0 = SPS, 1 = slice (VCL). */
static int annexb_has(uint32_t codec, const uint8_t *b, uint32_t n, int kind)
{
	uint32_t i;
	int hevc = codec == N2VTD_CODEC_HVC1;

	for (i = 0; i + 3 < n; i++) {
		if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
			int t = hevc ? (b[i + 3] >> 1) & 0x3f : b[i + 3] & 0x1f;
			if (kind == 0 && t == (hevc ? 33 : 7))
				return 1;
			if (kind == 1 && (hevc ? t < 32 : (t >= 1 && t <= 5)))
				return 1;
			i += 2;
		}
	}
	return 0;
}

static struct tfd_conn *tfd_conn_new(void)
{
	struct tfd_conn *c = (struct tfd_conn *)calloc(1, sizeof(*c));

	if (!c)
		return NULL;
	c->cfg = g_cfg;
	c->auto_index = g_cfg.auto_index;
	return c;
}

struct tfd_conn *tfd_last_conn(void)
{
	return atomic_load(&g_last);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Pattern the fake writes into a delivered buffer                                                                    */
/* ------------------------------------------------------------------------------------------------------------------ */

static void pattern_pixels(uint64_t pts, uint32_t w, uint32_t h, uint8_t *y, size_t ys, uint8_t *c, size_t cs)
{
	uint32_t x, yy;

	for (yy = 0; yy < h; yy++)
		for (x = 0; x < w; x++)
			y[(size_t)yy * ys + x] = (uint8_t)((x * 7u + yy * 3u + (uint32_t)pts) & 0xffu);
	for (yy = 0; yy < (h + 1) / 2; yy++)
		for (x = 0; x < (w + 1) / 2; x++) {
			c[(size_t)yy * cs + x * 2u] = (uint8_t)((x * 5u + yy * 11u + (uint32_t)pts + 60u) & 0xffu);
			c[(size_t)yy * cs + x * 2u + 1u] =
			    (uint8_t)((x * 13u + yy * 2u + (uint32_t)pts + 200u) & 0xffu);
		}
}

void tfd_pattern(uint64_t pts, uint32_t w, uint32_t h, uint8_t *y, size_t ys, uint8_t *c, size_t cs)
{
	pattern_pixels(pts, w, h, y, ys, c, cs);
}

static void fill_output(struct tfd_conn *c, struct tfd_entry *b, uint64_t pts)
{
	uint32_t w = c->cfg.width, h = c->cfg.height;
	uint8_t *lin_y = (uint8_t *)malloc((size_t)w * h + 1);
	uint8_t *lin_c = (uint8_t *)malloc((size_t)w * h / 2 + 1);
	size_t cstride = ((size_t)c->cfg.stride + 31u) & ~(size_t)31u;
	uint32_t tx, ty, x, yy;

	if (!lin_y || !lin_c) {
		free(lin_y);
		free(lin_c);
		return;
	}
	pattern_pixels(pts, w, h, lin_y, w, lin_c, w);
	if (c->cfg.format == N2VTD_FMT_NV12) {
		uint32_t copy_w = c->cfg.stride < w ? c->cfg.stride : w;
		for (yy = 0; yy < h && (size_t)yy * c->cfg.stride + copy_w <= b->bytes; yy++)
			memcpy(b->data + (size_t)yy * c->cfg.stride, lin_y + (size_t)yy * w, copy_w);
		for (yy = 0; yy < h / 2; yy++)
			memcpy(b->data + (size_t)c->cfg.stride * c->cfg.buf_h + (size_t)yy * c->cfg.stride,
			       lin_c + (size_t)yy * w, w);
	} else {
		uint32_t tpr_y = c->cfg.stride / 16, tpr_c = (uint32_t)cstride / 16;
		for (ty = 0; ty < c->cfg.buf_h / 32; ty++)
			for (tx = 0; tx < tpr_y; tx++) {
				size_t tile = ((size_t)ty * tpr_y + tx) * 512;
				for (yy = 0; yy < 32; yy++)
					for (x = 0; x < 16; x++) {
						uint32_t sx = tx * 16 + x, sy = ty * 32 + yy;
						uint8_t v = (sx < w && sy < h) ? lin_y[(size_t)sy * w + sx] : 0;
						if (tile + (size_t)yy * 16 + x < b->bytes)
							b->data[tile + (size_t)yy * 16 + x] = v;
					}
			}
		/* Chroma: 16-byte (8 pair) x 16-row tiles, 256 bytes each (the libyuv-verified MM21 layout). */
		for (ty = 0; ty < c->cfg.buf_h / 32; ty++)
			for (tx = 0; tx < tpr_c; tx++) {
				size_t tile = ((size_t)ty * tpr_c + tx) * 256;
				size_t base = (size_t)c->cfg.stride * c->cfg.buf_h;
				for (yy = 0; yy < 16; yy++)
					for (x = 0; x < 16; x++) {
						size_t xb = (size_t)tx * 16u + x;
						uint32_t sy = ty * 16 + yy;
						uint8_t v = (xb < w && sy < h / 2) ? lin_c[(size_t)sy * w + xb] : 0;
						if (base + tile + (size_t)yy * 16u + x < b->bytes)
							b->data[base + tile + (size_t)yy * 16u + x] = v;
					}
			}
	}
	free(lin_y);
	free(lin_c);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Client table                                                                                                       */
/* ------------------------------------------------------------------------------------------------------------------ */

static int tfd_connect(void *ctx, void **conn, const char *socket_path, const char *device_path, uint32_t timeout_ms)
{
	struct tfd_conn *c;

	(void)ctx;
	(void)socket_path;
	(void)device_path;
	(void)timeout_ms;
	if (g_cfg.connect_fail)
		return N2VTD_CLIENT_ABSENT;
	c = tfd_conn_new();
	if (!c)
		return N2VTD_CLIENT_E_NOMEM;
	*conn = c;
	atomic_store(&g_last, c);
	if (atomic_load(&g_default) == NULL)
		atomic_store(&g_default, c);
	return 0;
}

static void tfd_disconnect(void *ctx, void *conn)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;
	uint32_t i;

	(void)ctx;
	if (!c)
		return;
	atomic_store(&g_closed_seen, c->closed);
	atomic_store(&g_registered_seen, c->registered);
	atomic_store(&g_released_seen, c->released);
	atomic_store(&g_flushes_seen, c->flushes);
	for (i = 0; i < TFD_MAX_POOL; i++)
		free(c->pool[i].data);
	if (atomic_load(&g_last) == c)
		atomic_store(&g_last, NULL);
	if (atomic_load(&g_default) == c)
		atomic_store(&g_default, NULL);
	free(c);
}

static int tfd_open(void *ctx, void *conn, uint32_t codec, uint32_t max_w, uint32_t max_h, uint32_t format,
                    uint32_t *in_bytes)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;

	(void)ctx;
	if (!c || !in_bytes)
		return N2VTD_CLIENT_E_ARGS;
	if (c->cfg.open_fail)
		return N2VTD_CLIENT_E_HW;
	c->open_max_w = max_w;
	c->open_max_h = max_h;
	c->open_format = format;
	c->codec = codec;
	*in_bytes = c->cfg.in_bytes;
	return 0;
}

static int tfd_header(void *ctx, void *conn, const uint8_t *annexb, uint32_t bytes, struct n2vtd_header *hdr)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;

	(void)ctx;
	if (!c || !hdr)
		return N2VTD_CLIENT_E_ARGS;
	if (c->cfg.header_fail)
		return N2VTD_CLIENT_E_PROTO;
	/* vendor: eVDecDrvInit fails for an HEVC header START without a slice (daemon: header failed -1 -> E_HW) */
	if (c->cfg.vendor_rules && c->codec == N2VTD_CODEC_HVC1 && !annexb_has(c->codec, annexb, bytes, 1))
		return N2VTD_CLIENT_E_HW;
	c->sps_seen = 0;
	c->header_bytes = bytes;
	memset(hdr, 0, sizeof(*hdr));
	hdr->width = c->cfg.width;
	hdr->height = c->cfg.height;
	hdr->stride = c->cfg.stride;
	hdr->buf_h = c->cfg.buf_h;
	hdr->out_bytes = c->cfg.out_bytes;
	hdr->y_bytes = c->cfg.stride * c->cfg.buf_h;
	hdr->dpb_frames = c->cfg.dpb;
	hdr->format = c->cfg.format;
	hdr->crop_x = 0;
	hdr->crop_y = 0;
	return 0;
}

static int tfd_add_pool(void *ctx, void *conn, const struct n2vtd_header *hdr)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;
	uint32_t i;

	(void)ctx;
	if (!c || !hdr || hdr->dpb_frames > TFD_MAX_POOL)
		return N2VTD_CLIENT_E_ARGS;
	for (i = 0; i < hdr->dpb_frames; i++) {
		c->pool[i].used = 1;
		c->pool[i].bytes = hdr->out_bytes;
		c->pool[i].data = (uint8_t *)calloc(1, hdr->out_bytes);
		if (!c->pool[i].data)
			return N2VTD_CLIENT_E_NOMEM;
		c->npool++;
	}
	c->registered = c->npool;
	return 0;
}

static int tfd_free_buffers(void *ctx, void *conn)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;
	uint32_t i;

	(void)ctx;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	for (i = 0; i < TFD_MAX_POOL; i++) {
		free(c->pool[i].data);
		memset(&c->pool[i], 0, sizeof(c->pool[i]));
	}
	c->npool = 0;
	c->registered = 0;
	return 0;
}

static int tfd_input(void *ctx, void *conn, const uint8_t *au, uint32_t bytes, uint64_t pts,
                     struct n2vtd_input *result)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;

	(void)ctx;
	if (!c || !result || !au)
		return N2VTD_CLIENT_E_ARGS;
	if (c->cfg.input_fail)
		return N2VTD_CLIENT_E_HW;
	if (bytes > sizeof(c->au))
		return N2VTD_CLIENT_E_ARGS;
	c->au_bytes = bytes;
	memcpy(c->au, au, bytes);
	c->last_pts = pts;
	c->inputs++;
	memset(result, 0, sizeof(*result));
	if (annexb_has(c->codec, au, bytes, 0)) {
		c->sps_seen = 1;
		c->inputs_with_sps++;
	}
	if (c->cfg.vendor_rules && !c->sps_seen) {
		/* vendor: START answers OK, nothing reaches list_disp */
		c->inputs_dropped++;
		result->hw_us = 42;
		return 0;
	}
	if (c->changed_next) {
		c->changed_next = 0;
		c->changed_next = 0;
		result->changed = 1;
		return 0;
	}
	if (c->cfg.auto_picture) {
		if (c->npending < TFD_MAX_PENDING) {
			c->pending[c->npending].pts = pts;
			c->pending[c->npending].index = c->auto_index % (c->npool ? c->npool : 1);
			c->auto_index++;
			c->npending++;
		}
	}
	result->pictures_ready = c->have_ready_override ? (uint32_t)c->ready_override : c->npending;
	result->hw_us = 42;
	return 0;
}

static int tfd_picture(void *ctx, void *conn, struct n2vtd_picture *p)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;
	struct tfd_pic pic;

	(void)ctx;
	if (!c || !p)
		return N2VTD_CLIENT_E_ARGS;
	memset(p, 0, sizeof(*p));
	if (c->npending == 0)
		return 0;
	pic = c->pending[0];
	c->npending--;
	memmove(&c->pending[0], &c->pending[1], (size_t)c->npending * sizeof(c->pending[0]));
	p->have = 1;
	p->pts = pic.pts;
	p->poc = (int32_t)pic.index;
	p->index = pic.index;
	p->width = c->cfg.width;
	p->height = c->cfg.height;
	p->stride = c->cfg.stride;
	p->buf_h = c->cfg.buf_h;
	p->format = c->cfg.format;
	p->pictures_ready = c->npending;
	if (pic.index < c->npool && c->pool[pic.index].used)
		fill_output(c, &c->pool[pic.index], pic.pts);
	return 0;
}

static int tfd_release(void *ctx, void *conn, uint32_t index)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;

	(void)ctx;
	(void)index;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	c->released++;
	return 0;
}

static int tfd_buffer_rw(void *ctx, void *conn, uint32_t index, int plane, uint32_t offset, void *data,
                         uint32_t bytes, int write)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;
	size_t y_bytes, start, size;

	(void)ctx;
	if (!c || !data || index >= TFD_MAX_POOL || !c->pool[index].used)
		return N2VTD_CLIENT_E_ARGS;
	y_bytes = (size_t)c->cfg.stride * c->cfg.buf_h;
	if (plane == 0) {
		start = 0;
		size = y_bytes;
	} else if (plane == 1) {
		if (c->cfg.out_bytes < y_bytes)
			return N2VTD_CLIENT_E_ARGS;
		start = y_bytes;
		size = c->cfg.out_bytes - y_bytes;
	} else {
		return N2VTD_CLIENT_E_ARGS;
	}
	if (offset > size || bytes > size - offset || start + offset + bytes > c->pool[index].bytes)
		return N2VTD_CLIENT_E_ARGS;
	if (write)
		memcpy(c->pool[index].data + start + offset, data, bytes);
	else
		memcpy(data, c->pool[index].data + start + offset, bytes);
	return 0;
}

static int tfd_flush(void *ctx, void *conn, uint32_t *ready)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;

	(void)ctx;
	if (!c || !ready)
		return N2VTD_CLIENT_E_ARGS;
	c->flushes++;
	c->npending = c->flush_ready < c->npending ? c->flush_ready : c->npending;
	*ready = c->npending;
	return 0;
}

static int tfd_close(void *ctx, void *conn)
{
	struct tfd_conn *c = (struct tfd_conn *)conn;

	(void)ctx;
	if (c) {
		uint32_t i;
		c->closed = 1;
		for (i = 0; i < TFD_MAX_POOL; i++) {
			free(c->pool[i].data);
			memset(&c->pool[i], 0, sizeof(c->pool[i]));
		}
		c->npool = 0;
		c->registered = 0;
	}
	return 0;
}

static const struct n2vtd_client g_tfd_client = {
	.ctx = &g_cfg,
	.connect = tfd_connect,
	.dec_open = tfd_open,
	.dec_header = tfd_header,
	.dec_add_pool = tfd_add_pool,
	.dec_input = tfd_input,
	.dec_picture = tfd_picture,
	.dec_release = tfd_release,
	.buffer_rw = tfd_buffer_rw,
	.dec_free_buffers = tfd_free_buffers,
	.dec_flush = tfd_flush,
	.dec_close = tfd_close,
	.disconnect = tfd_disconnect,
	.set_timeout = NULL,
};

void tfd_install(const struct tfd_config *config)
{
	g_cfg = *config;
	if (g_cfg.in_bytes == 0)
		g_cfg.in_bytes = 65536;
	if (g_cfg.auto_index == 0)
		g_cfg.auto_index = 1;
	atomic_store(&g_last, NULL);
	atomic_store(&g_default, NULL);
	n2vtd_client_install(&g_tfd_client);
}

int tfd_push_picture(struct tfd_conn *c, uint64_t pts, uint32_t index)
{
	if (!c || c->npending >= TFD_MAX_PENDING)
		return -1;
	c->pending[c->npending].pts = pts;
	c->pending[c->npending].index = index;
	c->npending++;
	return 0;
}

void tfd_set_changed_next(struct tfd_conn *c, int on)
{
	if (c)
		c->changed_next = on;
}

void tfd_set_auto(struct tfd_conn *c, int on)
{
	if (c)
		c->cfg.auto_picture = on;
}

void tfd_set_ready(struct tfd_conn *c, int ready)
{
	if (c) {
		c->ready_override = ready;
		c->have_ready_override = 1;
	}
}

void tfd_set_flush_ready(struct tfd_conn *c, uint32_t ready)
{
	if (c)
		c->flush_ready = ready;
}

uint32_t tfd_inputs(struct tfd_conn *c)
{
	return c ? c->inputs : 0;
}

uint64_t tfd_last_pts(struct tfd_conn *c)
{
	return c ? c->last_pts : 0;
}

uint32_t tfd_last_au_bytes(struct tfd_conn *c)
{
	return c ? c->au_bytes : 0;
}

const uint8_t *tfd_last_au(struct tfd_conn *c)
{
	return c ? c->au : NULL;
}

uint32_t tfd_registered(struct tfd_conn *c)
{
	return c ? c->registered : 0;
}

uint32_t tfd_released(struct tfd_conn *c)
{
	return c ? c->released : 0;
}

uint32_t tfd_flushes(struct tfd_conn *c)
{
	return c ? c->flushes : 0;
}

int tfd_closed(struct tfd_conn *c)
{
	return c ? c->closed : 0;
}

uint32_t tfd_header_bytes(struct tfd_conn *c)
{
	return c ? c->header_bytes : 0;
}

uint32_t tfd_open_format(struct tfd_conn *c)
{
	return c ? c->open_format : 0;
}

uint32_t tfd_open_max_w(struct tfd_conn *c)
{
	return c ? c->open_max_w : 0;
}

uint32_t tfd_open_max_h(struct tfd_conn *c)
{
	return c ? c->open_max_h : 0;
}

int tfd_seen_closed(void)
{
	return atomic_load(&g_closed_seen);
}

uint32_t tfd_seen_registered(void)
{
	return atomic_load(&g_registered_seen);
}

uint32_t tfd_seen_released(void)
{
	return atomic_load(&g_released_seen);
}

uint32_t tfd_seen_flushes(void)
{
	return atomic_load(&g_flushes_seen);
}

uint32_t tfd_inputs_with_sps(struct tfd_conn *c)
{
	return c ? c->inputs_with_sps : 0;
}

uint32_t tfd_inputs_dropped(struct tfd_conn *c)
{
	return c ? c->inputs_dropped : 0;
}
