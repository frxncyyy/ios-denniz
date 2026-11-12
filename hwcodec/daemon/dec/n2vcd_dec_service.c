/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dec_service.c -- the decoder half of vcodecd.  See n2vcd_dec_service.h for the contract and
 * daemon/dec/n2vcd_proto_dec_rev2.h.txt for the wire text.
 *
 * One session at a time, a fixed pool of N2VCD_DEC_MAX_POOL output entries, no allocation in the
 * request path.  Every reply echoes the request's seq; errors are struct n2vcd_simple with
 * h.type = N2VCD_ERROR exactly as in daemon/vcodecd.c.  No pixel or bitstream byte is copied here:
 * buffers are client DMA handles, the AP side reads them through its own mappings.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2vcd_dec_service.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef ENOSYS
#define ENOSYS 78
#endif
#ifndef EPROTO
#define EPROTO 100
#endif

#define N2VCD_DEC_DEFAULT_IN_BUF (4u << 20)
#define N2VCD_DEC_SEEN_MAX 8u

struct dec_buf {
	uint32_t y_handle, c_handle;
	uint32_t y_pa, c_pa;
	uint32_t y_bytes, c_bytes;
	uint8_t added;          /* registered with the AP side */
	uint8_t outstanding;    /* a DEC_PICTURE_OK was sent and DEC_RELEASE has not arrived */
};

struct dec_session {
	int active;
	uint32_t id, client;
	uint32_t codec, fourcc;
	int have_header;
	uint32_t max_w, max_h;          /* OPEN's optional upper bound (0 = cfg max) */
	uint32_t width, height, stride, buf_h;
	uint32_t out_buf_bytes, y_bytes, c_bytes, dpb_frames;
	unsigned nbuf;
	struct dec_buf buf[N2VCD_DEC_MAX_POOL];
	uint32_t outstanding;
	uint32_t need_reheader; /* a changed input was answered; free + header must come first */
	uint64_t last_activity_ms;
	uint32_t requests;
};

struct n2vcd_dec_service {
	struct n2vcd_dec_service_cfg cfg;
	struct dec_session s;
	uint32_t next_id;
	uint32_t opens;
	int dead;
	int last_send_err;
	uint32_t seen[N2VCD_DEC_SEEN_MAX];
	unsigned nseen;
};

static uint64_t
now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void
svc_log(struct n2vcd_dec_service *s, const char *fmt, ...)
{
	char line[256];
	va_list ap;

	if (!s->cfg.log) {
		return;
	}
	va_start(ap, fmt);
	(void)vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	s->cfg.log(s->cfg.ctx, line);
}

/* ------------------------------------------------------------------ sending */

static void
send_raw(struct n2vcd_dec_service *s, uint32_t to, const void *msg, uint32_t bytes)
{
	if (!s->cfg.send) {
		return;
	}
	int r = s->cfg.send(s->cfg.ctx, to, msg, bytes);

	if (r < 0) {
		s->last_send_err = r;
	}
}

static void
send_error(struct n2vcd_dec_service *s, uint32_t to, uint32_t seq, uint32_t session,
           uint32_t status, uint32_t detail)
{
	struct n2vcd_simple m;

	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = N2VCD_ERROR;
	m.h.seq = seq;
	m.h.session = session;
	m.status = status;
	m.detail = detail;
	send_raw(s, to, &m, (uint32_t)sizeof(m));
}

static void
note_client(struct n2vcd_dec_service *s, uint32_t from)
{
	if (from == 0u) {
		return;
	}
	for (unsigned i = 0; i < s->nseen; i++) {
		if (s->seen[i] == from) {
			return;
		}
	}
	if (s->nseen < N2VCD_DEC_SEEN_MAX) {
		s->seen[s->nseen++] = from;
	}
}

/* ------------------------------------------------------------------ session */

static int
session_owned(const struct n2vcd_dec_service *s, uint32_t session, uint32_t client)
{
	return s->s.active && session == s->s.id && client == s->s.client;
}

static void
session_end(struct n2vcd_dec_service *s, const char *why, int close_ap)
{
	struct dec_session *d = &s->s;

	if (!d->active) {
		return;
	}
	if (close_ap && s->cfg.ap && s->cfg.ap->close) {
		s->cfg.ap->close(s->cfg.ap_ctx);
	}
	svc_log(s, "session %u close client=%u requests=%u pictures_outstanding=%u (%s)",
	        d->id, d->client, d->requests, d->outstanding, why);
	memset(d, 0, sizeof(*d));
}

static void
fatal_guest_stop(struct n2vcd_dec_service *s, uint32_t from, uint32_t seq, uint32_t session,
                 uint32_t code)
{
	for (unsigned i = 0; i < s->nseen; i++) {
		if (s->seen[i] == from) {
			continue;
		}
		send_error(s, s->seen[i], 0u, session, N2VCD_E_GUEST, code);
	}
	send_error(s, from, seq, session, N2VCD_E_GUEST, code);
	svc_log(s, "fatal guest stop session=%u code=%u", session, code);
	s->dead = 1;
	s->s.active = 0;        /* never call the AP close path against a dead guest */
	if (s->cfg.fatal) {
		s->cfg.fatal(s->cfg.ctx, code);
	}
}

static int
guest_stopped(struct n2vcd_dec_service *s, uint32_t *code)
{
	uint32_t c = 0;

	*code = 0;
	if (s->cfg.ap && s->cfg.ap->guest_stopped && s->cfg.ap->guest_stopped(s->cfg.ap_ctx, &c)) {
		*code = c;
		return 1;
	}
	return 0;
}

static uint32_t
map_error(int r)
{
	switch (r) {
	case -EOPNOTSUPP: return N2VCD_E_UNSUPPORTED;
	case -ENOMEM:
	case -ENOSPC: return N2VCD_E_NOMEM;
	case -EINVAL: return N2VCD_E_ARGS;
	case -EBUSY: return N2VCD_E_BUSY;
	case -EAGAIN: return N2VCD_E_BUSY;      /* no free reference buffer yet */
	case -ETIMEDOUT: return N2VCD_E_HW;
	default: return N2VCD_E_HW;
	}
}

/* The encoder path's err_fatal() (daemon/vcodecd.c): a timed-out or failed hardware job leaves the
 * AP session unusable, so the session goes away but the daemon keeps serving. */
static int
err_fatal(int r)
{
	return r == -ETIMEDOUT || r == -EIO;
}

/* Resolve a client DMA handle.  On the phone cfg.dma_info wraps RWVC_DMA_INFO.  On the host socket
 * transport there is no such ioctl (-ENOSYS) and the AP side of the tests does not need a pa, so a
 * resolver that cannot answer is not an error; a resolver that says "no such handle" is. */
static int
resolve_handle(struct n2vcd_dec_service *s, uint32_t handle, uint32_t *pa, uint32_t *bytes)
{
	int r;

	*pa = 0;
	*bytes = 0;
	if (!s->cfg.dma_info) {
		return 0;
	}
	r = s->cfg.dma_info(s->cfg.ctx, handle, pa, bytes);
	if (r == 0) {
		if (*pa == 0u || *bytes == 0u) {
			return -EINVAL;
		}
		return 0;
	}
	if (r == -ENOSYS || r == -ENOTTY) {
		return 0;
	}
	return r;
}

/* ------------------------------------------------------------------ replies */

static void
reply_open_ok(struct n2vcd_dec_service *s, uint32_t to, uint32_t seq, uint32_t id)
{
	struct n2vcd_open_ok ok;
	uint32_t in_bytes = s->cfg.in_buf_bytes ? s->cfg.in_buf_bytes : N2VCD_DEC_DEFAULT_IN_BUF;

	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_OPEN_OK;
	ok.h.seq = seq;
	ok.h.session = id;
	ok.status = N2VCD_E_OK;
	ok.in_buf_bytes = in_bytes;
	ok.out_buf_bytes = 0;   /* from DEC_HEADER_OK; the stream is not parsed at OPEN */
	send_raw(s, to, &ok, (uint32_t)sizeof(ok));
}

static void
reply_header_ok(struct n2vcd_dec_service *s, uint32_t to, uint32_t seq, uint32_t id)
{
	struct n2vcd_dec_header_ok ok;
	const struct dec_session *d = &s->s;

	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_DEC_HEADER_OK;
	ok.h.seq = seq;
	ok.h.session = id;
	ok.status = N2VCD_E_OK;
	ok.width = d->width;
	ok.height = d->height;
	ok.stride = d->stride;
	ok.out_buf_bytes = d->out_buf_bytes;
	ok.dpb_frames = d->dpb_frames;
	ok.reserved[0] = d->fourcc;
	ok.reserved[1] = d->buf_h;
	send_raw(s, to, &ok, (uint32_t)sizeof(ok));
}

static void
reply_simple(struct n2vcd_dec_service *s, uint32_t to, uint32_t seq, uint32_t session,
             uint32_t type, uint32_t status, uint32_t detail)
{
	struct n2vcd_simple m;

	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = type;
	m.h.seq = seq;
	m.h.session = session;
	m.status = status;
	m.detail = detail;
	send_raw(s, to, &m, (uint32_t)sizeof(m));
}

/* ------------------------------------------------------------------ OPEN */

static int
valid_decoder_size(uint32_t v, uint32_t max)
{
	return v >= 16u && v <= max && (v & 1u) == 0u;
}

static void
handle_open(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_open *req, uint32_t bytes)
{
	uint32_t fmt;
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (s->s.active) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_BUSY, s->s.id);
		return;
	}
	if (req->encoder != 0u) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_UNSUPPORTED, req->encoder);
		return;
	}
	if (req->codec != N2VCD_CODEC_H264 && req->codec != N2VCD_CODEC_HEVC && req->codec != N2VCD_CODEC_VP8 &&
	    req->codec != N2VCD_CODEC_VP9 && req->codec != N2VCD_CODEC_AV1 && req->codec != N2VCD_CODEC_MPEG2 &&
	    req->codec != N2VCD_CODEC_MPEG4 && req->codec != N2VCD_CODEC_H263) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_UNSUPPORTED, req->codec);
		return;
	}
	/* flags bits 8..15 select the output format (rev-2 text); every other bit is reserved. */
	if ((req->flags & ~(uint32_t)(N2VCD_OPEN_FMT_MASK << N2VCD_OPEN_FMT_SHIFT)) != 0u) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_UNSUPPORTED, req->flags);
		return;
	}
	fmt = (req->flags >> N2VCD_OPEN_FMT_SHIFT) & N2VCD_OPEN_FMT_MASK;
	if (fmt == N2VCD_OPEN_FMT_NV12) {
		/* The vendor produces MM21 only (DECODE_PATH.md section 2.3); refuse before any buffer exists. */
		send_error(s, from, req->h.seq, 0u, N2VCD_E_UNSUPPORTED, N2VCD_FMT_NV12);
		return;
	}
	if (fmt != N2VCD_OPEN_FMT_AUTO && fmt != N2VCD_OPEN_FMT_MM21) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_ARGS, fmt);
		return;
	}
	if ((req->width == 0u) != (req->height == 0u)) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_ARGS, req->width << 16 | (req->height & 0xffffu));
		return;
	}
	if (req->width != 0u &&
	    (!valid_decoder_size(req->width, s->cfg.max_width) ||
	     !valid_decoder_size(req->height, s->cfg.max_height))) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_ARGS, req->width << 16 | (req->height & 0xffffu));
		return;
	}
	if (req->fps_num != 0u || req->fps_den != 0u || req->bitrate != 0u || req->gop != 0u ||
	    req->profile != 0u || req->level != 0u) {
		/* Encoder-only fields: refuse rather than silently ignore a caller that thinks it is encoding. */
		send_error(s, from, req->h.seq, 0u, N2VCD_E_ARGS, req->bitrate);
		return;
	}

	r = s->cfg.ap->open(s->cfg.ap_ctx, req->codec);
	if (r != 0) {
		uint32_t st = map_error(r);

		svc_log(s, "OPEN codec=0x%x failed: %d", req->codec, r);
		send_error(s, from, req->h.seq, 0u, st, (uint32_t)(-r));
		return;
	}

	memset(&s->s, 0, sizeof(s->s));
	s->s.active = 1;
	s->s.id = ++s->next_id;
	if (s->s.id == 0u) {
		s->s.id = ++s->next_id;
	}
	s->s.client = from;
	s->s.codec = req->codec;
	s->s.max_w = req->width ? req->width : s->cfg.max_width;
	s->s.max_h = req->height ? req->height : s->cfg.max_height;
	s->s.fourcc = N2VCD_FMT_MM21;
	s->s.last_activity_ms = now_ms();
	s->opens++;
	svc_log(s, "session %u open client=%u codec=0x%x fmt=MM21", s->s.id, from, req->codec);
	reply_open_ok(s, from, req->h.seq, s->s.id);
}

/* ------------------------------------------------------------------ HEADER */

static void
handle_header(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_dec_header *req, uint32_t bytes)
{
	struct n2vcd_dec_ap_buffer in;
	struct n2vcd_dec_ap_geometry geo;
	uint32_t pa = 0, len = 0, y_bytes;
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	if (s->s.nbuf != 0u) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_BUSY, 1u);   /* free the pool first */
		return;
	}
	uint32_t in_max = s->cfg.in_buf_bytes ? s->cfg.in_buf_bytes : N2VCD_DEC_DEFAULT_IN_BUF;

	if (req->in_handle == 0u || req->in_bytes == 0u || req->in_bytes > in_max) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->in_bytes);
		return;
	}
	r = resolve_handle(s, req->in_handle, &pa, &len);
	if (r != 0) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->in_handle);
		return;
	}
	if (len != 0u && req->in_bytes > len) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, len);
		return;
	}
	in.handle = req->in_handle;
	in.pa = pa;
	in.bytes = len ? len : req->in_bytes;

	memset(&geo, 0, sizeof(geo));
	r = s->cfg.ap->header(s->cfg.ap_ctx, &in, req->in_bytes, &geo);
	if (r != 0) {
		uint32_t st = map_error(r);

		svc_log(s, "session %u header failed: %d", s->s.id, r);
		send_error(s, from, req->h.seq, s->s.id, st, (uint32_t)(-r));
		if (err_fatal(r)) {
			session_end(s, "header failed", 1);
		}
		return;
	}
	if (!valid_decoder_size(geo.width, s->s.max_w) ||
	    !valid_decoder_size(geo.height, s->s.max_h) ||
	    geo.stride < geo.width || (geo.stride & 15u) != 0u ||
	    geo.buf_h < geo.height || (geo.buf_h & 31u) != 0u ||
	    geo.dpb_frames == 0u || geo.dpb_frames > N2VCD_DEC_MAX_POOL) {
		svc_log(s, "session %u header geometry rejected: %ux%u stride %u buf_h %u dpb %u",
		        s->s.id, geo.width, geo.height, geo.stride, geo.buf_h, geo.dpb_frames);
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_HW,
		           (geo.dpb_frames > N2VCD_DEC_MAX_POOL) ? geo.dpb_frames : geo.height);
		session_end(s, "bad header geometry", 1);
		return;
	}
	y_bytes = geo.stride * geo.buf_h;
	if (y_bytes == 0u || y_bytes > 0x40000000u || geo.out_buf_bytes != y_bytes + y_bytes / 2u) {
		svc_log(s, "session %u header plane sizes rejected: stride %u buf_h %u out %u",
		        s->s.id, geo.stride, geo.buf_h, geo.out_buf_bytes);
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_HW, geo.out_buf_bytes);
		session_end(s, "bad plane sizes", 1);
		return;
	}
	if (geo.fourcc != N2VCD_FMT_MM21) {
		/* Only MM21 is decodable on the iOS side today (DEC06 detiles it). */
		svc_log(s, "session %u header format 0x%08x unsupported", s->s.id, geo.fourcc);
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_UNSUPPORTED, geo.fourcc);
		session_end(s, "unsupported output format", 1);
		return;
	}

	s->s.width = geo.width;
	s->s.height = geo.height;
	s->s.stride = geo.stride;
	s->s.buf_h = geo.buf_h;
	s->s.out_buf_bytes = geo.out_buf_bytes;
	s->s.y_bytes = y_bytes;
	s->s.c_bytes = geo.out_buf_bytes - y_bytes;
	s->s.dpb_frames = geo.dpb_frames;
	s->s.fourcc = geo.fourcc;
	s->s.have_header = 1;
	s->s.need_reheader = 0;
	svc_log(s, "session %u header %ux%u stride=%u buf_h=%u dpb=%u mm21", s->s.id, geo.width, geo.height,
	        geo.stride, geo.buf_h, geo.dpb_frames);
	reply_header_ok(s, from, req->h.seq, s->s.id);
}

/* ------------------------------------------------------------------ pool */

static void
handle_add_buffer(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_dec_add_buffer *req,
                  uint32_t bytes)
{
	struct n2vcd_dec_ap_buffer y, c;
	uint32_t pa = 0, len = 0;
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	if (!s->s.have_header) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_SESSION, 0u);
		return;
	}
	if (s->s.need_reheader) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_BUSY, 2u);
		return;
	}
	if (s->s.nbuf >= s->s.dpb_frames || s->s.nbuf >= N2VCD_DEC_MAX_POOL) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_NOMEM, s->s.dpb_frames);
		return;
	}
	/* A single buffer carries Y at 0 and C at stride*buf_h (c_handle 0).  Two buffers are allowed too. */
	int sep = req->c_handle != 0u && req->c_handle != req->handle;

	if (req->handle == 0u || req->bytes < s->s.y_bytes ||
	    (s->s.c_bytes != 0u && sep && req->c_bytes < s->s.c_bytes) ||
	    (s->s.c_bytes == 0u && req->c_handle != 0u) ||
	    (!sep && req->bytes < s->s.y_bytes + s->s.c_bytes)) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->bytes);
		return;
	}
	for (unsigned i = 0; i < s->s.nbuf; i++) {
		if (s->s.buf[i].y_handle == req->handle || s->s.buf[i].c_handle == req->handle ||
		    (sep && (s->s.buf[i].y_handle == req->c_handle ||
		             s->s.buf[i].c_handle == req->c_handle))) {
			send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->handle);
			return;
		}
	}
	memset(&y, 0, sizeof(y));
	memset(&c, 0, sizeof(c));
	y.handle = req->handle;
	y.bytes = s->s.y_bytes;
	r = resolve_handle(s, req->handle, &pa, &len);
	if (r != 0) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->handle);
		return;
	}
	y.pa = pa;
	if (len != 0u) {
		if (req->bytes > len) {
			send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, len);
			return;
		}
		y.bytes = len;
	}
	if (sep) {
		c.handle = req->c_handle;
		c.bytes = s->s.c_bytes;
		pa = 0;
		len = 0;
		r = resolve_handle(s, req->c_handle, &pa, &len);
		if (r != 0) {
			send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->c_handle);
			return;
		}
		c.pa = pa;
		if (len != 0u) {
			if (req->c_bytes > len) {
				send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, len);
				return;
			}
			c.bytes = len;
		}
	} else if (s->s.c_bytes != 0u) {
		/* One buffer: the C plane is the same allocation, y_bytes further in.  The AP side has
		 * the matching branch (n2ap_vdec.c decode(): fb_c_handle == fb_handle). */
		c.handle = req->handle;
		c.pa = y.pa ? y.pa + s->s.y_bytes : 0u;
		c.bytes = s->s.c_bytes;
	}

	r = s->cfg.ap->add_buffer(s->cfg.ap_ctx, s->s.nbuf, &y, s->s.c_bytes ? &c : NULL);
	if (r != 0) {
		uint32_t st = map_error(r);

		svc_log(s, "session %u add_buffer %u failed: %d", s->s.id, s->s.nbuf, r);
		send_error(s, from, req->h.seq, s->s.id, st, (uint32_t)(-r));
		if (err_fatal(r)) {
			session_end(s, "add_buffer failed", 1);
		}
		return;
	}
	struct dec_buf *b = &s->s.buf[s->s.nbuf];

	memset(b, 0, sizeof(*b));
	b->y_handle = req->handle;
	b->c_handle = req->c_handle;
	b->y_pa = y.pa;
	b->c_pa = c.pa;
	b->y_bytes = y.bytes;
	b->c_bytes = c.bytes;
	b->added = 1;

	struct n2vcd_dec_add_buffer_ok ok;

	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_DEC_ADD_BUFFER_OK;
	ok.h.seq = req->h.seq;
	ok.h.session = s->s.id;
	ok.status = N2VCD_E_OK;
	ok.index = s->s.nbuf;
	s->s.nbuf++;
	send_raw(s, from, &ok, (uint32_t)sizeof(ok));
}

static void
handle_free_buffers(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_simple *req,
                    uint32_t bytes)
{
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	if (s->s.outstanding != 0u) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_BUSY, s->s.outstanding);
		return;
	}
	if (s->s.nbuf == 0u) {
		reply_simple(s, from, req->h.seq, s->s.id, N2VCD_DEC_FREE_BUFFERS_OK, N2VCD_E_OK, 0u);
		return;
	}
	r = s->cfg.ap->free_buffers(s->cfg.ap_ctx);
	if (r != 0) {
		send_error(s, from, req->h.seq, s->s.id, map_error(r), (uint32_t)(-r));
		if (err_fatal(r)) {
			session_end(s, "free_buffers failed", 1);
		}
		return;
	}
	memset(s->s.buf, 0, sizeof(s->s.buf));
	s->s.nbuf = 0;
	s->s.have_header = 0;
	s->s.need_reheader = 0;
	reply_simple(s, from, req->h.seq, s->s.id, N2VCD_DEC_FREE_BUFFERS_OK, N2VCD_E_OK, 0u);
}

/* ------------------------------------------------------------------ INPUT */

static void
handle_input(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_dec_input *req, uint32_t bytes)
{
	struct n2vcd_dec_ap_buffer in;
	struct n2vcd_dec_ap_result res;
	uint32_t pa = 0, len = 0;
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	if (!s->s.have_header) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_SESSION, 0u);
		return;
	}
	if (s->s.need_reheader) {
		/* The previous input reported a geometry change: drain, release, free, header. */
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_BUSY, 2u);
		return;
	}
	if ((req->flags & ~1u) != 0u || req->reserved != 0u) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->flags);
		return;
	}
	uint32_t in_max = s->cfg.in_buf_bytes ? s->cfg.in_buf_bytes : N2VCD_DEC_DEFAULT_IN_BUF;

	if (req->in_handle == 0u || req->in_bytes == 0u || req->in_bytes > in_max) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->in_bytes);
		return;
	}
	r = resolve_handle(s, req->in_handle, &pa, &len);
	if (r != 0) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->in_handle);
		return;
	}
	if (len != 0u && req->in_bytes > len) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, len);
		return;
	}
	in.handle = req->in_handle;
	in.pa = pa;
	in.bytes = len ? len : req->in_bytes;

	memset(&res, 0, sizeof(res));
	uint64_t t0 = now_ms();
	r = s->cfg.ap->input(s->cfg.ap_ctx, &in, req->in_bytes, req->pts, req->flags, &res);
	uint64_t elapsed = now_ms() - t0;

	if (r != 0) {
		uint32_t st = map_error(r);
		uint32_t detail = (uint32_t)(-r);

		if (r == -ETIMEDOUT && elapsed > 0u) {
			detail = (uint32_t)elapsed;
		}
		svc_log(s, "session %u input failed: %d", s->s.id, r);
		send_error(s, from, req->h.seq, s->s.id, st, detail);
		if (err_fatal(r)) {
			session_end(s, "input failed", 1);
		}
		return;
	}
	if (!res.hw_us) {
		res.hw_us = (uint32_t)elapsed;
	}
	if (res.changed) {
		s->s.need_reheader = 1;
	}
	s->s.requests++;
	s->s.last_activity_ms = now_ms();
	if (res.pictures_ready > N2VCD_DEC_MAX_POOL) {
		res.pictures_ready = N2VCD_DEC_MAX_POOL;
	}
	{
		struct n2vcd_dec_input_ok ok;

		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = N2VCD_DEC_INPUT_OK;
		ok.h.seq = req->h.seq;
		ok.h.session = s->s.id;
		ok.status = N2VCD_E_OK;
		ok.pictures_ready = res.pictures_ready;
		ok.changed = res.changed ? 1u : 0u;
		ok.error_map = res.error_map;
		ok.hw_us = res.hw_us;
		send_raw(s, from, &ok, (uint32_t)sizeof(ok));
	}
}

/* ------------------------------------------------------------------ PICTURE */

static void
handle_picture(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_dec_picture *req,
               uint32_t bytes)
{
	struct n2vcd_dec_ap_picture pic;
	struct n2vcd_dec_picture_ok ok;
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	memset(&pic, 0, sizeof(pic));
	r = s->cfg.ap->picture(s->cfg.ap_ctx, &pic);
	if (r < 0) {
		svc_log(s, "session %u picture failed: %d", s->s.id, r);
		send_error(s, from, req->h.seq, s->s.id, map_error(r), (uint32_t)(-r));
		if (err_fatal(r)) {
			session_end(s, "picture failed", 1);
		}
		return;
	}
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_DEC_PICTURE_OK;
	ok.h.seq = req->h.seq;
	ok.h.session = s->s.id;
	ok.status = N2VCD_E_OK;
	if (r == 0) {
		ok.have = 0;
		ok.width = s->s.width;
		ok.height = s->s.height;
		ok.stride = s->s.stride;
		ok.buf_h = s->s.buf_h;
		ok.fourcc = s->s.fourcc;
		send_raw(s, from, &ok, (uint32_t)sizeof(ok));
		return;
	}
	if (pic.index >= s->s.nbuf || !s->s.buf[pic.index].added) {
		svc_log(s, "session %u picture with unregistered index %u", s->s.id, pic.index);
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_HW, pic.index);
		session_end(s, "picture index", 1);
		return;
	}
	if (s->s.buf[pic.index].outstanding) {
		svc_log(s, "session %u picture index %u returned twice", s->s.id, pic.index);
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_HW, pic.index);
		session_end(s, "picture twice", 1);
		return;
	}
	s->s.buf[pic.index].outstanding = 1;
	s->s.outstanding++;
	if (pic.pictures_ready > N2VCD_DEC_MAX_POOL) {
		pic.pictures_ready = N2VCD_DEC_MAX_POOL;
	}
	ok.have = 1;
	ok.pts = pic.pts;
	ok.index = pic.index;
	ok.width = s->s.width;
	ok.height = s->s.height;
	ok.stride = s->s.stride;
	ok.buf_h = s->s.buf_h;
	ok.fourcc = s->s.fourcc;
	ok.pictures_ready = pic.pictures_ready;
	ok.poc = pic.poc;
	send_raw(s, from, &ok, (uint32_t)sizeof(ok));
}

static void
handle_release(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_dec_release *req,
               uint32_t bytes)
{
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	if (req->index >= s->s.nbuf || !s->s.buf[req->index].added || !s->s.buf[req->index].outstanding) {
		send_error(s, from, req->h.seq, s->s.id, N2VCD_E_ARGS, req->index);
		return;
	}
	r = s->cfg.ap->release(s->cfg.ap_ctx, req->index);
	if (r != 0) {
		svc_log(s, "session %u release %u failed: %d", s->s.id, req->index, r);
		send_error(s, from, req->h.seq, s->s.id, map_error(r), (uint32_t)(-r));
		if (err_fatal(r)) {
			session_end(s, "release failed", 1);
		}
		return;
	}
	s->s.buf[req->index].outstanding = 0;
	s->s.outstanding--;
	reply_simple(s, from, req->h.seq, s->s.id, N2VCD_DEC_RELEASE_OK, N2VCD_E_OK, req->index);
}

/* ------------------------------------------------------------------ FLUSH / CLOSE */

static void
handle_flush(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_simple *req, uint32_t bytes)
{
	uint32_t ready = 0;
	int r;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	if (s->s.have_header) {
		r = s->cfg.ap->flush(s->cfg.ap_ctx, &ready);
		if (r != 0) {
			send_error(s, from, req->h.seq, s->s.id, map_error(r), (uint32_t)(-r));
			if (err_fatal(r)) {
				session_end(s, "flush failed", 1);
			}
			return;
		}
	}
	if (ready > N2VCD_DEC_MAX_POOL) {
		ready = N2VCD_DEC_MAX_POOL;
	}
	s->s.last_activity_ms = now_ms();
	reply_simple(s, from, req->h.seq, s->s.id, N2VCD_FLUSH_OK, N2VCD_E_OK, ready);
}

static void
handle_close(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_simple *req, uint32_t bytes)
{
	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(s, req->h.session, from)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_SESSION, s->s.active ? s->s.id : 0u);
		return;
	}
	uint32_t id = s->s.id;

	session_end(s, "closed", 1);
	reply_simple(s, from, req->h.seq, id, N2VCD_CLOSE_OK, N2VCD_E_OK, 0u);
}

/* ------------------------------------------------------------------ HELLO (standalone use) */

static void
handle_hello(struct n2vcd_dec_service *s, uint32_t from, const struct n2vcd_hello *req, uint32_t bytes)
{
	struct n2vcd_hello_ok ok;

	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(s, from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (req->version < 1u || req->version > (uint32_t)N2VCD_VERSION) {
		send_error(s, from, req->h.seq, 0u, N2VCD_E_VERSION, req->version);
		return;
	}
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_HELLO_OK;
	ok.h.seq = req->h.seq;
	ok.h.session = 0u;
	ok.version = N2VCD_VERSION;
	ok.enc_codecs = 0u;     /* the decoder service alone: vcodecd's own HELLO adds the encoders */
	ok.dec_codecs = N2VCD_DEC_CODECS_ALL;   /* opus-fmt: bit 0 H.264, 1 HEVC, 2 VP8, 3 VP9, 4 AV1, 5 MPEG-2, 6 MPEG-4, 7 H.263 */
	ok.max_width = s->cfg.max_width ? s->cfg.max_width : N2VCD_DEC_MAX_WIDTH;
	ok.max_height = s->cfg.max_height ? s->cfg.max_height : N2VCD_DEC_MAX_HEIGHT;
	send_raw(s, from, &ok, (uint32_t)sizeof(ok));
}

/* ------------------------------------------------------------------ API */

struct n2vcd_dec_service *
n2vcd_dec_service_create(const struct n2vcd_dec_service_cfg *cfg)
{
	struct n2vcd_dec_service *s;

	if (!cfg || !cfg->send || !cfg->ap || !cfg->ap->open || !cfg->ap->header || !cfg->ap->add_buffer ||
	    !cfg->ap->input || !cfg->ap->picture || !cfg->ap->release || !cfg->ap->free_buffers ||
	    !cfg->ap->flush || !cfg->ap->close) {
		return NULL;
	}
	s = (struct n2vcd_dec_service *)calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	s->cfg = *cfg;
	if (s->cfg.max_width == 0u || s->cfg.max_width > N2VCD_DEC_MAX_WIDTH) {
		s->cfg.max_width = N2VCD_DEC_MAX_WIDTH;
	}
	if (s->cfg.max_height == 0u || s->cfg.max_height > N2VCD_DEC_MAX_HEIGHT) {
		s->cfg.max_height = N2VCD_DEC_MAX_HEIGHT;
	}
	if (s->cfg.in_buf_bytes == 0u) {
		s->cfg.in_buf_bytes = N2VCD_DEC_DEFAULT_IN_BUF;
	}
	return s;
}

void
n2vcd_dec_service_destroy(struct n2vcd_dec_service *s)
{
	if (!s) {
		return;
	}
	if (s->s.active && !s->dead) {
		session_end(s, "service destroy", 1);
	}
	free(s);
}

int
n2vcd_dec_service_owns(const void *msg, uint32_t bytes)
{
	struct n2vcd_hdr h;

	if (!msg || bytes < (uint32_t)sizeof(h)) {
		return 0;
	}
	memcpy(&h, msg, sizeof(h));
	if (h.magic != N2VCD_MAGIC) {
		return 0;
	}
	if (h.type == N2VCD_OPEN) {
		struct n2vcd_open o;

		if (bytes < (uint32_t)sizeof(o)) {
			return 0;
		}
		memcpy(&o, msg, sizeof(o));
		return o.encoder == 0u;
	}
	if (h.type == N2VCD_DEC_HEADER || h.type == N2VCD_DEC_FRAME) {
		return 1;       /* v1 ids with rev-2 meaning (DEC_FRAME is refused as unsupported) */
	}
	if (h.type >= N2VCD_DEC_ADD_BUFFER && h.type <= N2VCD_DEC_RELEASE_OK) {
		return 1;
	}
	return 0;
}

int
n2vcd_dec_service_handle(struct n2vcd_dec_service *s, uint32_t from, const void *msg, uint32_t bytes)
{
	struct n2vcd_hdr h;

	if (!s) {
		return -EINVAL;
	}
	if (s->dead) {
		return -ENODEV;
	}
	if (!msg || bytes < (uint32_t)sizeof(struct n2vcd_hdr)) {
		return -EOPNOTSUPP;
	}
	memcpy(&h, msg, sizeof(h));
	if (h.magic != N2VCD_MAGIC) {
		return -EOPNOTSUPP;
	}
	/* FLUSH and CLOSE carry a v1 id shared with the encoder: they are ours only for our session. */
	int ours = n2vcd_dec_service_owns(msg, bytes) || h.type == N2VCD_HELLO ||
	           ((h.type == N2VCD_FLUSH || h.type == N2VCD_CLOSE) && session_owned(s, h.session, from));

	if (!ours) {
		return -EOPNOTSUPP;
	}
	if (bytes > N2VCD_DEC_MSG_MAX) {
		send_error(s, from, 0u, 0u, N2VCD_E_ARGS, bytes);
		return 0;
	}
	memcpy(&h, msg, sizeof(h));
	if (h.seq == 0u) {
		/* seq 0 is reserved for unsolicited events (daemon/vcodecd.c); a request with it is refused. */
		send_error(s, from, 0u, h.session, N2VCD_E_ARGS, 0u);
		return 0;
	}
	note_client(s, from);
	s->last_send_err = 0;

	uint32_t code = 0;

	if (guest_stopped(s, &code)) {
		fatal_guest_stop(s, from, h.seq, h.session, code);
		return -ENODEV;
	}

	if (h.type == N2VCD_HELLO) {
		struct n2vcd_hello req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_hello(s, from, &req, bytes);
	} else if (h.type == N2VCD_OPEN) {
		struct n2vcd_open req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_open(s, from, &req, bytes);
	} else if (h.type == N2VCD_DEC_HEADER) {
		struct n2vcd_dec_header req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_header(s, from, &req, bytes);
	} else if (h.type == N2VCD_DEC_ADD_BUFFER) {
		struct n2vcd_dec_add_buffer req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_add_buffer(s, from, &req, bytes);
	} else if (h.type == N2VCD_DEC_FREE_BUFFERS) {
		struct n2vcd_simple req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_free_buffers(s, from, &req, bytes);
	} else if (h.type == N2VCD_DEC_INPUT) {
		struct n2vcd_dec_input req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_input(s, from, &req, bytes);
	} else if (h.type == N2VCD_DEC_PICTURE) {
		struct n2vcd_dec_picture req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_picture(s, from, &req, bytes);
	} else if (h.type == N2VCD_DEC_RELEASE) {
		struct n2vcd_dec_release req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_release(s, from, &req, bytes);
	} else if (h.type == N2VCD_FLUSH) {
		struct n2vcd_simple req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_flush(s, from, &req, bytes);
	} else if (h.type == N2VCD_CLOSE) {
		struct n2vcd_simple req;

		if (bytes < (uint32_t)sizeof(req)) {
			send_error(s, from, h.seq, h.session, N2VCD_E_ARGS, bytes);
			return 0;
		}
		memcpy(&req, msg, sizeof(req));
		handle_close(s, from, &req, bytes);
	} else {
		send_error(s, from, h.seq, h.session, N2VCD_E_UNSUPPORTED, h.type);
		return 0;
	}

	/* Any request on the session counts as activity for the idle timer, including a picture poll. */
	if (s->s.active && s->s.client == from) {
		s->s.last_activity_ms = now_ms();
	}

	/* A reply that could not be delivered to the session owner means that client is gone. */
	if (s->last_send_err == -EPIPE || s->last_send_err == -ENXIO || s->last_send_err == -ECONNRESET) {
		if (s->s.active && s->s.client == from) {
			session_end(s, "client gone", 1);
		}
	}
	return 0;
}

void
n2vcd_dec_service_client_gone(struct n2vcd_dec_service *s, uint32_t from)
{
	if (!s || s->dead) {
		return;
	}
	if (s->s.active && s->s.client == from) {
		session_end(s, "client gone", 1);
	}
}

int
n2vcd_dec_service_tick(struct n2vcd_dec_service *s, uint64_t now)
{
	uint32_t code = 0;

	if (!s) {
		return -EINVAL;
	}
	if (s->dead) {
		return -ENODEV;
	}
	/* vcodecd's watchdog does the same only while a session is active: with no session there is
	 * nothing to fail, and the next request reports the stop to its caller. */
	if (s->s.active && guest_stopped(s, &code)) {
		fatal_guest_stop(s, s->s.client, 0u, s->s.id, code);
		return -ENODEV;
	}
	if (s->cfg.session_idle_ms != 0u && s->s.active) {
		uint64_t last = s->s.last_activity_ms;

		if (last != 0u && now > last && now - last > (uint64_t)s->cfg.session_idle_ms) {
			uint32_t client = s->s.client;
			uint32_t id = s->s.id;

			svc_log(s, "session %u idle for %llu ms (limit %u), dropping it", id,
			        (unsigned long long)(now - last), s->cfg.session_idle_ms);
			send_error(s, client, 0u, id, N2VCD_E_SESSION, 0u);
			session_end(s, "idle timeout", 1);
		}
	}
	return 0;
}

uint32_t
n2vcd_dec_service_session(const struct n2vcd_dec_service *s)
{
	return s && s->s.active ? s->s.id : 0u;
}

uint32_t
n2vcd_dec_service_open_count(const struct n2vcd_dec_service *s)
{
	return s ? s->opens : 0u;
}

int
n2vcd_dec_service_dead(const struct n2vcd_dec_service *s)
{
	return s ? s->dead : 1;
}
