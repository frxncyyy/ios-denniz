/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dec.c -- decoder client library of vcodecd.  See n2vcd_dec.h for the contract and
 * include/n2vcd_proto.h rev 2 for the wire format.  One mutex per client serialises every
 * request/reply pair; the sequence number of every reply is checked.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2vcd_dec.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
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

#ifndef N2VCD_PROTO_REV2
#error "include/n2vcd_proto.h rev 2 (decoder messages) is not landed; see daemon/dec/n2vcd_proto_dec_rev2.h.txt"
#endif

#define N2VCD_DEC_MSG_MAX 1024u
#define N2VCD_DEC_BUF_MAX (64u << 20)
#define N2VCD_DEC_ANY_SESSION 0xffffffffu
#define N2VCD_DEC_COPY_CHUNK (1u << 20)

_Static_assert(sizeof(struct n2vcd_hdr) == 16, "n2vcd_hdr layout");
_Static_assert(sizeof(struct n2vcd_hello) == 24, "n2vcd_hello layout");
_Static_assert(sizeof(struct n2vcd_hello_ok) == 40, "n2vcd_hello_ok layout");
_Static_assert(sizeof(struct n2vcd_open) == 80, "n2vcd_open layout");
_Static_assert(sizeof(struct n2vcd_open_ok) == 32, "n2vcd_open_ok layout");
_Static_assert(sizeof(struct n2vcd_simple) == 24, "n2vcd_simple layout");
_Static_assert(sizeof(struct n2vcd_dec_header) == 24, "n2vcd_dec_header layout");
_Static_assert(sizeof(struct n2vcd_dec_header_ok) == 48, "n2vcd_dec_header_ok layout");
_Static_assert(sizeof(struct n2vcd_dec_add_buffer) == 32, "n2vcd_dec_add_buffer layout");
_Static_assert(sizeof(struct n2vcd_dec_add_buffer_ok) == 24, "n2vcd_dec_add_buffer_ok layout");
_Static_assert(sizeof(struct n2vcd_dec_input) == 40, "n2vcd_dec_input layout");
_Static_assert(sizeof(struct n2vcd_dec_input_ok) == 40, "n2vcd_dec_input_ok layout");
_Static_assert(sizeof(struct n2vcd_dec_picture) == 16, "n2vcd_dec_picture layout");
_Static_assert(sizeof(struct n2vcd_dec_picture_ok) == 64, "n2vcd_dec_picture_ok layout");
_Static_assert(sizeof(struct n2vcd_dec_release) == 24, "n2vcd_dec_release layout");

struct n2vcd_dec_pool_buf {
	uint32_t y_handle, c_handle;    /* c_handle 0: Y and C share y_handle, C at y_bytes */
	uint32_t y_bytes, c_bytes;
};

struct n2vcd_dec_client {
	struct n2vcd_transport t;
	pthread_mutex_t lock;
	uint32_t seq;
	_Atomic uint32_t timeout_ms;
	_Atomic uint32_t last_errno;
	_Atomic uint32_t last_detail;
	int broken;
	struct n2vcd_dec_caps caps;
};

struct n2vcd_dec_session {
	struct n2vcd_dec_client *c;
	uint32_t id;
	uint32_t codec;
	uint32_t in_handle, in_bytes;
	int alive;
	int have_header;
	struct n2vcd_dec_geometry geo;
	unsigned nbuf;
	struct n2vcd_dec_pool_buf pool[N2VCD_DEC_MAX_POOL];
};

static void
set_err(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || errlen == 0) {
		return;
	}
	va_start(ap, fmt);
	(void)vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

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
sleep_ms(uint32_t ms)
{
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

const char *
n2vcd_dec_strstatus(int status)
{
	switch (status) {
	case N2VCD_E_OK: return "ok";
	case N2VCD_E_VERSION: return "version";
	case N2VCD_E_UNSUPPORTED: return "unsupported";
	case N2VCD_E_BUSY: return "busy";
	case N2VCD_E_ARGS: return "args";
	case N2VCD_E_HW: return "hardware";
	case N2VCD_E_GUEST: return "guest";
	case N2VCD_E_NOMEM: return "nomem";
	case N2VCD_E_SESSION: return "session";
	default: return "unknown";
	}
}

int
n2vcd_dec_status_errno(int status)
{
	switch (status) {
	case N2VCD_E_OK: return 0;
	case N2VCD_E_VERSION: return -EPROTO;
	case N2VCD_E_UNSUPPORTED: return -EOPNOTSUPP;
	case N2VCD_E_BUSY: return -EBUSY;
	case N2VCD_E_ARGS: return -EINVAL;
	case N2VCD_E_HW:
	case N2VCD_E_GUEST: return -EIO;
	case N2VCD_E_NOMEM: return -ENOMEM;
	case N2VCD_E_SESSION: return -EPIPE;
	default: return -EIO;
	}
}

static int
map_errno(int e)
{
	switch (e) {
	case 0: return N2VCD_E_HW;
	case ENOMEM: return N2VCD_E_NOMEM;
	case EINVAL: return N2VCD_E_ARGS;
	case EBUSY: return N2VCD_E_BUSY;
	case EOPNOTSUPP: return N2VCD_E_UNSUPPORTED;
	case ENXIO:
	case EPIPE:
	case ECONNRESET: return N2VCD_E_SESSION;
	case EAGAIN:
	case ETIMEDOUT: return N2VCD_E_HW;
	default: return N2VCD_E_HW;
	}
}

/* Read the header of a datagram.  0 or -EPROTO. */
static int
peek_hdr(const void *buf, uint32_t bytes, struct n2vcd_hdr *h)
{
	if (bytes < (uint32_t)sizeof(*h)) {
		return -EPROTO;
	}
	memcpy(h, buf, sizeof(*h));
	if (h->magic != N2VCD_MAGIC) {
		return -EPROTO;
	}
	return 0;
}

/*
 * One request/reply transaction.  `expect_type` 0 accepts any non-error reply (used by HELLO/OPEN
 * through their explicit types anyway); `expect_session` N2VCD_DEC_ANY_SESSION skips the session
 * check.  On an N2VCD_ERROR reply the status is returned and the detail stored; seq-0 unsolicited
 * errors are returned the same way.
 */
static int
xact(struct n2vcd_dec_client *c, void *req, uint32_t req_bytes, void *reply, uint32_t reply_max,
     uint32_t *got, uint32_t expect_type, uint32_t expect_session)
{
	uint32_t timeout = atomic_load(&c->timeout_ms);
	uint64_t deadline;
	struct n2vcd_hdr *rh = (struct n2vcd_hdr *)req;
	uint32_t seq;
	int r;

	pthread_mutex_lock(&c->lock);
	if (c->broken) {
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_SESSION;
	}
	memset(reply, 0, reply_max);
	seq = ++c->seq;
	if (seq == 0u) {
		seq = ++c->seq;
	}
	rh->seq = seq;

	if (timeout == 0u) {
		timeout = N2VCD_DEC_TIMEOUT_MS;
	}
	deadline = now_ms() + timeout;
	for (;;) {
		r = c->t.send(c->t.ctx, req, req_bytes);
		if (r == 0) {
			break;
		}
		if (r == -EAGAIN && now_ms() < deadline) {
			sleep_ms(1);
			continue;
		}
		atomic_store(&c->last_errno, (uint32_t)(-r));
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return map_errno(-r);
	}
	for (;;) {
		uint64_t now = now_ms();
		uint32_t remain;
		uint8_t buf[N2VCD_DEC_MSG_MAX];
		struct n2vcd_hdr h;
		int n;

		if (now >= deadline) {
			atomic_store(&c->last_errno, (uint32_t)ETIMEDOUT);
			pthread_mutex_unlock(&c->lock);
			return N2VCD_E_HW;
		}
		remain = (uint32_t)(deadline - now);
		n = c->t.recv(c->t.ctx, buf, (uint32_t)sizeof(buf), remain);
		if (n == -EAGAIN) {
			continue;
		}
		if (n < 0) {
			atomic_store(&c->last_errno, (uint32_t)(-n));
			if (n == -ENXIO || n == -EPIPE || n == -ECONNRESET) {
				c->broken = 1;
			}
			pthread_mutex_unlock(&c->lock);
			return map_errno(-n);
		}
		if (peek_hdr(buf, (uint32_t)n, &h) != 0) {
			c->broken = 1;
			pthread_mutex_unlock(&c->lock);
			return N2VCD_E_HW;
		}
		if (h.type == N2VCD_ERROR) {
			struct n2vcd_simple e;

			if ((uint32_t)n < (uint32_t)sizeof(e)) {
				c->broken = 1;
				pthread_mutex_unlock(&c->lock);
				return N2VCD_E_HW;
			}
			memcpy(&e, buf, sizeof(e));
			if (h.seq != 0u && h.seq != seq) {
				c->broken = 1;
				pthread_mutex_unlock(&c->lock);
				return N2VCD_E_HW;
			}
			atomic_store(&c->last_detail, e.detail);
			pthread_mutex_unlock(&c->lock);
			return (int)e.status;
		}
		if (h.seq != seq) {
			/* a reply to another request: this stream can no longer be matched */
			c->broken = 1;
			pthread_mutex_unlock(&c->lock);
			return N2VCD_E_HW;
		}
		if (expect_type != 0u && h.type != expect_type) {
			c->broken = 1;
			pthread_mutex_unlock(&c->lock);
			return N2VCD_E_HW;
		}
		if (expect_session != N2VCD_DEC_ANY_SESSION && h.session != expect_session) {
			c->broken = 1;
			pthread_mutex_unlock(&c->lock);
			return N2VCD_E_HW;
		}
		if ((uint32_t)n > reply_max) {
			c->broken = 1;
			pthread_mutex_unlock(&c->lock);
			return N2VCD_E_HW;
		}
		memcpy(reply, buf, (uint32_t)n);
		if (got) {
			*got = (uint32_t)n;
		}
		atomic_store(&c->last_detail, 0);
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_OK;
	}
}

static int
sess_status(struct n2vcd_dec_session *s, int status)
{
	if (s && (status == N2VCD_E_GUEST || status == N2VCD_E_SESSION)) {
		s->alive = 0;
	}
	return status;
}

static int
sess_ok(const struct n2vcd_dec_session *s)
{
	return s && s->alive && !s->c->broken;
}

/* Copy host -> session input buffer in bounded chunks. */
static int
copy_in(struct n2vcd_dec_session *s, const void *src, uint32_t bytes)
{
	const uint8_t *p = (const uint8_t *)src;
	uint32_t off = 0;

	while (off < bytes) {
		uint32_t n = bytes - off;

		if (n > N2VCD_DEC_COPY_CHUNK) {
			n = N2VCD_DEC_COPY_CHUNK;
		}
		int r = s->c->t.buf_rw(s->c->t.ctx, s->in_handle, off, (void *)(p + off), n, 1);

		if (r != 0) {
			atomic_store(&s->c->last_errno, (uint32_t)(-r));
			return map_errno(-r);
		}
		off += n;
	}
	return N2VCD_E_OK;
}

/* ------------------------------------------------------------------ connect */

int
n2vcd_dec_connect(struct n2vcd_dec_client **out, const struct n2vcd_transport *t,
                  uint32_t timeout_ms, char *err, size_t errlen)
{
	struct n2vcd_dec_client *c;
	struct n2vcd_hello hello;
	struct n2vcd_hello_ok ok;
	uint32_t got = 0;
	int r;

	if (!out) {
		return N2VCD_E_ARGS;
	}
	*out = NULL;
	if (!t || !t->send || !t->recv || !t->buf_alloc || !t->buf_rw || !t->buf_free) {
		set_err(err, errlen, "connect: incomplete transport");
		return N2VCD_E_ARGS;
	}
	c = (struct n2vcd_dec_client *)calloc(1, sizeof(*c));
	if (!c) {
		set_err(err, errlen, "connect: out of memory");
		return N2VCD_E_NOMEM;
	}
	c->t = *t;
	if (pthread_mutex_init(&c->lock, NULL) != 0) {
		free(c);
		set_err(err, errlen, "connect: mutex");
		return N2VCD_E_NOMEM;
	}
	atomic_store(&c->timeout_ms, timeout_ms ? timeout_ms : N2VCD_DEC_TIMEOUT_MS);

	memset(&hello, 0, sizeof(hello));
	hello.h.magic = N2VCD_MAGIC;
	hello.h.type = N2VCD_HELLO;
	hello.version = N2VCD_VERSION;
	r = xact(c, &hello, (uint32_t)sizeof(hello), &ok, (uint32_t)sizeof(ok), &got, N2VCD_HELLO_OK, 0u);
	if (r != N2VCD_E_OK) {
		set_err(err, errlen, "HELLO: %s (detail %u)", n2vcd_dec_strstatus(r), atomic_load(&c->last_detail));
		if (c->t.close) {
			c->t.close(c->t.ctx);
		}
		pthread_mutex_destroy(&c->lock);
		free(c);
		return r;
	}
	if (got != (uint32_t)sizeof(ok) || ok.version < 1u || ok.version > (uint32_t)N2VCD_VERSION) {
		set_err(err, errlen, "HELLO_OK: bad reply (version %u)", ok.version);
		if (c->t.close) {
			c->t.close(c->t.ctx);
		}
		pthread_mutex_destroy(&c->lock);
		free(c);
		return N2VCD_E_VERSION;
	}
	c->caps.version = ok.version;
	c->caps.dec_codecs = ok.dec_codecs;
	c->caps.max_width = ok.max_width;
	c->caps.max_height = ok.max_height;
	*out = c;
	return N2VCD_E_OK;
}

int
n2vcd_dec_info(const struct n2vcd_dec_client *c, struct n2vcd_dec_caps *caps)
{
	if (!c || !caps) {
		return N2VCD_E_ARGS;
	}
	*caps = c->caps;
	return N2VCD_E_OK;
}

void
n2vcd_dec_set_timeout(struct n2vcd_dec_client *c, uint32_t timeout_ms)
{
	if (!c) {
		return;
	}
	atomic_store(&c->timeout_ms, timeout_ms ? timeout_ms : N2VCD_DEC_TIMEOUT_MS);
}

uint32_t
n2vcd_dec_last_errno(const struct n2vcd_dec_client *c)
{
	return c ? atomic_load(&c->last_errno) : 0u;
}

uint32_t
n2vcd_dec_last_detail(const struct n2vcd_dec_client *c)
{
	return c ? atomic_load(&c->last_detail) : 0u;
}

void
n2vcd_dec_disconnect(struct n2vcd_dec_client *c)
{
	if (!c) {
		return;
	}
	if (c->t.close) {
		c->t.close(c->t.ctx);
	}
	pthread_mutex_destroy(&c->lock);
	free(c);
}

/* ------------------------------------------------------------------ open / header */

int
n2vcd_dec_open(struct n2vcd_dec_client *c, uint32_t codec, uint32_t max_width, uint32_t max_height,
               uint32_t out_format, struct n2vcd_dec_session **out,
               uint32_t *in_buf_bytes, char *err, size_t errlen)
{
	struct n2vcd_open req;
	struct n2vcd_open_ok ok;
	struct n2vcd_dec_session *s;
	uint32_t got = 0;
	int r;

	if (!out) {
		return N2VCD_E_ARGS;
	}
	*out = NULL;
	if (!c) {
		return N2VCD_E_ARGS;
	}
	if (codec != N2VCD_CODEC_H264 && codec != N2VCD_CODEC_HEVC && codec != N2VCD_CODEC_VP8 && codec != N2VCD_CODEC_VP9 &&
	    codec != N2VCD_CODEC_AV1 && codec != N2VCD_CODEC_MPEG2 && codec != N2VCD_CODEC_MPEG4 && codec != N2VCD_CODEC_H263) {
		set_err(err, errlen, "open: codec 0x%x is not a decoder format (n2vcd_proto.h)", codec);
		return N2VCD_E_UNSUPPORTED;
	}
	if (out_format != 0u && out_format != N2VCD_OPEN_FMT_MM21) {
		set_err(err, errlen, "open: output format %u is not MM21", out_format);
		return N2VCD_E_UNSUPPORTED;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_OPEN;
	req.codec = codec;
	req.encoder = 0u;
	req.width = max_width;
	req.height = max_height;
	req.flags = (out_format ? (out_format << N2VCD_OPEN_FMT_SHIFT) : 0u);
	r = xact(c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_OPEN_OK,
	         N2VCD_DEC_ANY_SESSION);
	if (r != N2VCD_E_OK) {
		set_err(err, errlen, "OPEN: %s (detail %u)", n2vcd_dec_strstatus(r), atomic_load(&c->last_detail));
		return r;
	}
	if (got != (uint32_t)sizeof(ok) || ok.status != N2VCD_E_OK || ok.h.session == 0u ||
	    ok.in_buf_bytes == 0u || ok.in_buf_bytes > N2VCD_DEC_BUF_MAX || ok.out_buf_bytes != 0u) {
		set_err(err, errlen, "OPEN_OK: bad reply (session %u in %u out %u)", ok.h.session,
		        ok.in_buf_bytes, ok.out_buf_bytes);
		c->broken = 1;
		return N2VCD_E_HW;
	}
	s = (struct n2vcd_dec_session *)calloc(1, sizeof(*s));
	if (!s) {
		set_err(err, errlen, "open: out of memory");
		return N2VCD_E_NOMEM;
	}
	s->c = c;
	s->id = ok.h.session;
	s->codec = codec;
	s->alive = 1;
	s->in_bytes = ok.in_buf_bytes;
	r = c->t.buf_alloc(c->t.ctx, s->in_bytes, &s->in_handle);
	if (r != 0) {
		struct n2vcd_simple close;
		struct n2vcd_simple reply;

		memset(&close, 0, sizeof(close));
		close.h.magic = N2VCD_MAGIC;
		close.h.type = N2VCD_CLOSE;
		close.h.session = s->id;
		(void)xact(c, &close, (uint32_t)sizeof(close), &reply, (uint32_t)sizeof(reply), NULL,
		           N2VCD_CLOSE_OK, s->id);
		free(s);
		set_err(err, errlen, "open: input buffer allocation failed: %d", r);
		return map_errno(-r);
	}
	if (in_buf_bytes) {
		*in_buf_bytes = s->in_bytes;
	}
	*out = s;
	return N2VCD_E_OK;
}

uint32_t
n2vcd_dec_session_id(const struct n2vcd_dec_session *s)
{
	return s ? s->id : 0u;
}

uint32_t
n2vcd_dec_in_bytes(const struct n2vcd_dec_session *s)
{
	return s ? s->in_bytes : 0u;
}

uint32_t
n2vcd_dec_out_bytes(const struct n2vcd_dec_session *s)
{
	return s && s->have_header ? s->geo.out_buf_bytes : 0u;
}

int
n2vcd_dec_header(struct n2vcd_dec_session *s, const void *au, uint32_t bytes,
                 struct n2vcd_dec_geometry *geo)
{
	struct n2vcd_dec_header req;
	struct n2vcd_dec_header_ok ok;
	uint32_t got = 0;
	uint64_t y;
	int r;

	if (!s || !au || bytes == 0u) {
		return N2VCD_E_ARGS;
	}
	if (!sess_ok(s)) {
		return N2VCD_E_SESSION;
	}
	if (bytes > s->in_bytes) {
		return N2VCD_E_ARGS;
	}
	if (s->nbuf != 0u) {
		return N2VCD_E_BUSY;
	}
	r = copy_in(s, au, bytes);
	if (r != N2VCD_E_OK) {
		return r;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_DEC_HEADER;
	req.h.session = s->id;
	req.in_handle = s->in_handle;
	req.in_bytes = bytes;
	r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_DEC_HEADER_OK, s->id);
	if (r != N2VCD_E_OK) {
		return sess_status(s, r);
	}
	uint32_t buf_h = ok.reserved[1];
	if (got != (uint32_t)sizeof(ok) || ok.status != N2VCD_E_OK ||
	    ok.width == 0u || ok.height == 0u || ok.stride < ok.width || buf_h < ok.height ||
	    (ok.stride & 15u) != 0u || (buf_h & 31u) != 0u ||
	    ok.dpb_frames == 0u || ok.dpb_frames > N2VCD_DEC_MAX_POOL ||
	    ok.reserved[0] == 0u) {
		s->c->broken = 1;
		return N2VCD_E_HW;
	}
	y = (uint64_t)ok.stride * buf_h;
	if (y == 0u || y > 0x40000000u || (uint64_t)ok.out_buf_bytes != y + y / 2u) {
		s->c->broken = 1;
		return N2VCD_E_HW;
	}
	s->geo.width = ok.width;
	s->geo.height = ok.height;
	s->geo.stride = ok.stride;
	s->geo.buf_h = buf_h;
	s->geo.y_bytes = (uint32_t)y;
	s->geo.c_bytes = ok.out_buf_bytes - (uint32_t)y;
	s->geo.out_buf_bytes = ok.out_buf_bytes;
	s->geo.dpb_frames = ok.dpb_frames;
	s->geo.fourcc = ok.reserved[0];
	s->have_header = 1;
	if (geo) {
		*geo = s->geo;
	}
	return N2VCD_E_OK;
}

/* ------------------------------------------------------------------ pool */

static void
free_pool(struct n2vcd_dec_session *s)
{
	for (unsigned i = 0; i < s->nbuf; i++) {
		if (s->pool[i].y_handle != 0u) {
			(void)s->c->t.buf_free(s->c->t.ctx, s->pool[i].y_handle);
		}
		if (s->pool[i].c_handle != 0u) {
			(void)s->c->t.buf_free(s->c->t.ctx, s->pool[i].c_handle);
		}
	}
	memset(s->pool, 0, sizeof(s->pool));
	s->nbuf = 0;
	s->have_header = 0;
	memset(&s->geo, 0, sizeof(s->geo));
}

/* Best effort: tell the daemon to forget whatever pool entries were registered before a failure. */
static void
free_pool_remote(struct n2vcd_dec_session *s)
{
	struct n2vcd_simple req;
	struct n2vcd_simple ok;

	if (!sess_ok(s) || s->nbuf == 0u) {
		return;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_DEC_FREE_BUFFERS;
	req.h.session = s->id;
	(void)xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), NULL,
	           N2VCD_DEC_FREE_BUFFERS_OK, s->id);
}

int
n2vcd_dec_add_buffers(struct n2vcd_dec_session *s, const struct n2vcd_dec_geometry *geo)
{
	unsigned want;

	if (!s || !geo) {
		return N2VCD_E_ARGS;
	}
	if (!sess_ok(s)) {
		return N2VCD_E_SESSION;
	}
	if (!s->have_header || s->nbuf != 0u) {
		return N2VCD_E_SESSION;
	}
	if (geo->dpb_frames == 0u || geo->dpb_frames > N2VCD_DEC_MAX_POOL ||
	    geo->stride != s->geo.stride || geo->buf_h != s->geo.buf_h ||
	    geo->width != s->geo.width || geo->height != s->geo.height ||
	    geo->out_buf_bytes != s->geo.out_buf_bytes || geo->y_bytes != s->geo.y_bytes ||
	    geo->c_bytes != s->geo.c_bytes || geo->fourcc != s->geo.fourcc) {
		return N2VCD_E_ARGS;
	}
	want = geo->dpb_frames;
	int r = N2VCD_E_HW;

	for (unsigned i = 0; i < want; i++) {
		struct n2vcd_dec_add_buffer req;
		struct n2vcd_dec_add_buffer_ok ok;
		struct n2vcd_dec_pool_buf *b = &s->pool[i];
		uint32_t got = 0;

		s->nbuf = i + 1u;               /* cleanup below includes the partial entry */
		/* One buffer per entry: Y at 0, C at y_bytes (rev-2 text; c_handle 0 selects it). */
		r = s->c->t.buf_alloc(s->c->t.ctx, geo->out_buf_bytes, &b->y_handle);
		if (r != 0) {
			s->nbuf = i;
			goto fail;
		}
		b->y_bytes = geo->y_bytes;
		b->c_bytes = geo->c_bytes;
		memset(&req, 0, sizeof(req));
		req.h.magic = N2VCD_MAGIC;
		req.h.type = N2VCD_DEC_ADD_BUFFER;
		req.h.session = s->id;
		req.handle = b->y_handle;
		req.bytes = geo->out_buf_bytes;
		req.c_handle = 0u;
		req.c_bytes = 0u;
		r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got,
		         N2VCD_DEC_ADD_BUFFER_OK, s->id);
		if (r != N2VCD_E_OK) {
			return sess_status(s, r);
		}
		if (got != (uint32_t)sizeof(ok) || ok.status != N2VCD_E_OK || ok.index != i) {
			s->c->broken = 1;
			free_pool_remote(s);
			free_pool(s);
			return N2VCD_E_HW;
		}
	}
	s->nbuf = want;
	return N2VCD_E_OK;
fail:
	free_pool_remote(s);
	free_pool(s);
	return r < 0 ? map_errno(-r) : N2VCD_E_HW;
}

int
n2vcd_dec_buffer_handle(const struct n2vcd_dec_session *s, uint32_t index,
                        uint32_t *y_handle, uint32_t *c_handle)
{
	if (!s || index >= s->nbuf) {
		return N2VCD_E_ARGS;
	}
	if (y_handle) {
		*y_handle = s->pool[index].y_handle;
	}
	if (c_handle) {
		*c_handle = s->pool[index].c_handle;
	}
	return N2VCD_E_OK;
}

int
n2vcd_dec_buffer_rw(struct n2vcd_dec_session *s, uint32_t index, int plane, uint32_t offset,
                    void *data, uint32_t bytes, int write)
{
	uint32_t handle, limit, abs_off = offset;
	int r;

	if (!s || !data || plane < 0 || plane > 1 || index >= s->nbuf) {
		return N2VCD_E_ARGS;
	}
	if (plane == 0) {
		handle = s->pool[index].y_handle;
		limit = s->pool[index].y_bytes;
	} else if (s->pool[index].c_handle != 0u) {
		handle = s->pool[index].c_handle;
		limit = s->pool[index].c_bytes;
	} else {
		handle = s->pool[index].y_handle;
		limit = s->pool[index].c_bytes;
		abs_off = offset + s->pool[index].y_bytes;     /* C follows Y in the one-buffer form */
	}
	if (handle == 0u || offset > limit || bytes > limit - offset) {
		return N2VCD_E_ARGS;
	}
	r = s->c->t.buf_rw(s->c->t.ctx, handle, abs_off, data, bytes, write);
	if (r != 0) {
		atomic_store(&s->c->last_errno, (uint32_t)(-r));
		return map_errno(-r);
	}
	return N2VCD_E_OK;
}

/* ------------------------------------------------------------------ decode loop */

int
n2vcd_dec_input(struct n2vcd_dec_session *s, const void *au, uint32_t bytes, uint64_t pts,
                uint32_t flags, struct n2vcd_dec_input_info *info)
{
	struct n2vcd_dec_input req;
	struct n2vcd_dec_input_ok ok;
	uint32_t got = 0;
	int r;

	if (!s || !au || bytes == 0u) {
		return N2VCD_E_ARGS;
	}
	if (!sess_ok(s)) {
		return N2VCD_E_SESSION;
	}
	if (!s->have_header) {
		return N2VCD_E_SESSION;
	}
	if (bytes > s->in_bytes || (flags & ~1u) != 0u) {
		return N2VCD_E_ARGS;
	}
	r = copy_in(s, au, bytes);
	if (r != N2VCD_E_OK) {
		return r;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_DEC_INPUT;
	req.h.session = s->id;
	req.in_handle = s->in_handle;
	req.in_bytes = bytes;
	req.pts = pts;
	req.flags = flags;
	r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_DEC_INPUT_OK, s->id);
	if (r != N2VCD_E_OK) {
		return sess_status(s, r);
	}
	if (got != (uint32_t)sizeof(ok) || ok.status != N2VCD_E_OK || ok.pictures_ready > N2VCD_DEC_MAX_POOL) {
		s->c->broken = 1;
		return N2VCD_E_HW;
	}
	if (info) {
		info->pictures_ready = ok.pictures_ready;
		info->changed = ok.changed;
		info->error_map = ok.error_map;
		info->hw_us = ok.hw_us;
	}
	return N2VCD_E_OK;
}

int
n2vcd_dec_picture(struct n2vcd_dec_session *s, struct n2vcd_dec_pic *frame)
{
	struct n2vcd_dec_picture req;
	struct n2vcd_dec_picture_ok ok;
	uint32_t got = 0;
	int r;

	if (!s || !frame) {
		return N2VCD_E_ARGS;
	}
	if (!sess_ok(s)) {
		return N2VCD_E_SESSION;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_DEC_PICTURE;
	req.h.session = s->id;
	r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_DEC_PICTURE_OK, s->id);
	if (r != N2VCD_E_OK) {
		return sess_status(s, r);
	}
	if (got != (uint32_t)sizeof(ok) || ok.status != N2VCD_E_OK) {
		s->c->broken = 1;
		return N2VCD_E_HW;
	}
	if (!ok.have) {
		return 0;
	}
	if (ok.index >= s->nbuf || ok.pictures_ready > N2VCD_DEC_MAX_POOL) {
		s->c->broken = 1;
		return N2VCD_E_HW;
	}
	memset(frame, 0, sizeof(*frame));
	frame->index = ok.index;
	frame->y_handle = s->pool[ok.index].y_handle;
	frame->c_handle = s->pool[ok.index].c_handle;
	frame->pts = ok.pts;
	frame->poc = ok.poc;
	frame->width = ok.width;
	frame->height = ok.height;
	frame->stride = ok.stride;
	frame->buf_h = ok.buf_h;
	frame->fourcc = ok.fourcc;
	frame->pictures_ready = ok.pictures_ready;
	return 1;
}

int
n2vcd_dec_release(struct n2vcd_dec_session *s, uint32_t index)
{
	struct n2vcd_dec_release req;
	struct n2vcd_simple ok;
	uint32_t got = 0;
	int r;

	if (!s || index >= s->nbuf) {
		return N2VCD_E_ARGS;
	}
	if (!sess_ok(s)) {
		return N2VCD_E_SESSION;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_DEC_RELEASE;
	req.h.session = s->id;
	req.index = index;
	r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_DEC_RELEASE_OK, s->id);
	return sess_status(s, r);
}

int
n2vcd_dec_free_buffers(struct n2vcd_dec_session *s)
{
	struct n2vcd_simple req;
	struct n2vcd_simple ok;
	uint32_t got = 0;
	int r;

	if (!s) {
		return N2VCD_E_ARGS;
	}
	if (!sess_ok(s)) {
		free_pool(s);
		return N2VCD_E_SESSION;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_DEC_FREE_BUFFERS;
	req.h.session = s->id;
	r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_DEC_FREE_BUFFERS_OK, s->id);
	if (r == N2VCD_E_OK) {
		free_pool(s);
	}
	return sess_status(s, r);
}

int
n2vcd_dec_flush(struct n2vcd_dec_session *s, uint32_t *pictures_ready)
{
	struct n2vcd_simple req;
	struct n2vcd_simple ok;
	uint32_t got = 0;
	int r;

	if (!s) {
		return N2VCD_E_ARGS;
	}
	if (!sess_ok(s)) {
		return N2VCD_E_SESSION;
	}
	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_FLUSH;
	req.h.session = s->id;
	r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_FLUSH_OK, s->id);
	if (r != N2VCD_E_OK) {
		return sess_status(s, r);
	}
	if (got != (uint32_t)sizeof(ok) || ok.status != N2VCD_E_OK || ok.detail > N2VCD_DEC_MAX_POOL) {
		s->c->broken = 1;
		return N2VCD_E_HW;
	}
	if (pictures_ready) {
		*pictures_ready = ok.detail;
	}
	return N2VCD_E_OK;
}

int
n2vcd_dec_close(struct n2vcd_dec_session *s)
{
	struct n2vcd_simple req;
	struct n2vcd_simple ok;
	uint32_t got = 0;
	int r = N2VCD_E_OK;

	if (!s) {
		return N2VCD_E_ARGS;
	}
	if (s->alive && !s->c->broken) {
		memset(&req, 0, sizeof(req));
		req.h.magic = N2VCD_MAGIC;
		req.h.type = N2VCD_CLOSE;
		req.h.session = s->id;
		r = xact(s->c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_CLOSE_OK, s->id);
		if (r == N2VCD_E_GUEST || r == N2VCD_E_SESSION) {
			r = N2VCD_E_OK;         /* the death was already reported by an earlier call */
		}
	}
	free_pool(s);
	if (s->in_handle != 0u) {
		(void)s->c->t.buf_free(s->c->t.ctx, s->in_handle);
	}
	free(s);
	return r;
}
