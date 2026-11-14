/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_client.c -- client library of vcodecd.  See n2vcd_client.h for the contract, include/n2vcd_proto.h for the
 * wire format.  One mutex per client serialises every request/reply pair; the sequence number of every reply is
 * checked, so a reply can never be delivered to the wrong caller.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2vcd_client.h"

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

_Static_assert(sizeof(struct n2vcd_hdr) == 16, "n2vcd_hdr layout");
_Static_assert(sizeof(struct n2vcd_hello) == 24, "n2vcd_hello layout");
_Static_assert(sizeof(struct n2vcd_hello_ok) == 40, "n2vcd_hello_ok layout");
_Static_assert(sizeof(struct n2vcd_open) == 80, "n2vcd_open layout");
_Static_assert(sizeof(struct n2vcd_open_ok) == 32, "n2vcd_open_ok layout");
_Static_assert(sizeof(struct n2vcd_enc_frame) == 48, "n2vcd_enc_frame layout");
_Static_assert(sizeof(struct n2vcd_enc_frame_ok) == 48, "n2vcd_enc_frame_ok layout");
_Static_assert(sizeof(struct n2vcd_enc_set) == 32, "n2vcd_enc_set layout");
_Static_assert(sizeof(struct n2vcd_simple) == 24, "n2vcd_simple layout");

/* A datagram never exceeds the frozen protocol's 1024-byte limit (RWVC_MSG_MAX is exactly 1024). */
#define N2VCD_CLIENT_MSG_MAX 1024u
/* Upper bound for the buffer sizes the server may ask for.  The 4K encoder needs about 13 MiB in and 16 MiB out. */
#define N2VCD_CLIENT_BUF_MAX (64u << 20)
/* xact(): no central reply-session check (OPEN_OK carries the new id, which the caller validates). */
#define N2VCD_ANY_SESSION 0xffffffffu

struct n2vcd_client {
	struct n2vcd_transport t;
	pthread_mutex_t lock;
	uint32_t seq;
	_Atomic uint32_t timeout_ms;
	_Atomic uint32_t last_errno;        /* diagnostics; read from any thread without the lock */
	_Atomic uint32_t last_detail;
	int broken;                 /* transport or protocol failure: no request can be matched any more */
	struct n2vcd_caps caps;
};

struct n2vcd_session {
	struct n2vcd_client *c;
	uint32_t id;
	uint32_t width, height, stride;
	uint32_t in_handle, out_handle;
	uint32_t in_bytes, out_bytes;
	uint32_t alive;
};

static void
set_err(char *err, size_t errlen, const char *fmt, ...)
{
	if (!err || errlen == 0) { return; }
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static uint64_t
now_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { return 0; }
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
n2vcd_strstatus(int status)
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
	case ECONNRESET:
	case ECONNABORTED:
	case EBADF:
		return N2VCD_E_SESSION;
	case ETIMEDOUT:
	case EAGAIN:
		return N2VCD_E_HW;
	default:
		return N2VCD_E_HW;
	}
}

static int
errno_fatal(int e)
{
	return e == ENXIO || e == EPIPE || e == ECONNRESET || e == ECONNABORTED || e == EBADF;
}

/* One request, one reply.  The caller holds client->lock.  On an N2VCD_ERROR reply (status != 0) the status is
 * returned; a non-error reply is copied into rep with *got = bytes.  A seq-0 datagram is an unsolicited error
 * (session death) and its status is returned too.  Any malformed or out-of-order datagram marks the client dead. */
static int
xact(struct n2vcd_client *c, void *req, uint32_t req_bytes,
     void *rep, uint32_t cap, uint32_t *got, uint32_t expect_session)
{
	struct n2vcd_hdr *h = (struct n2vcd_hdr *)req;
	uint8_t buf[N2VCD_CLIENT_MSG_MAX];

	if (c->broken) { return N2VCD_E_SESSION; }
	atomic_store(&c->last_errno, 0);
	atomic_store(&c->last_detail, 0);

	uint32_t seq = c->seq + 1u;
	if (seq == 0) { seq = 1; }
	c->seq = seq;
	h->magic = N2VCD_MAGIC;
	h->seq = seq;

	uint64_t deadline = now_ms() + atomic_load(&c->timeout_ms);
	for (;;) {
		int r = c->t.send(c->t.ctx, req, req_bytes);
		if (r == 0) { break; }
		atomic_store(&c->last_errno, (uint32_t)(-r));
		if (r == -EAGAIN && now_ms() < deadline) {
			sleep_ms(1);
			continue;
		}
		if (errno_fatal(-r)) { c->broken = 1; }
		return map_errno(-r);
	}

	for (;;) {
		uint64_t now = now_ms();
		if (now >= deadline) {
			atomic_store(&c->last_errno, (uint32_t)ETIMEDOUT);
			return N2VCD_E_HW;
		}
		uint32_t left = (uint32_t)(deadline - now);
		if (left > 60000u) { left = 60000u; }
		int n = c->t.recv(c->t.ctx, buf, sizeof(buf), left);
		if (n == -EAGAIN || n == -EINTR) { continue; }
		if (n < 0) {
			atomic_store(&c->last_errno, (uint32_t)(-n));
			if (errno_fatal(-n)) { c->broken = 1; }
			if (-n == EMSGSIZE) { c->broken = 1; }   /* protocol says <= 1024; framing cannot be trusted */
			return map_errno(-n);
		}
		if (n == 0) {                                    /* server never sends empty datagrams */
			atomic_store(&c->last_errno, (uint32_t)EPIPE);
			c->broken = 1;
			return N2VCD_E_SESSION;
		}

		struct n2vcd_hdr hd;
		if ((uint32_t)n < sizeof(hd)) {
			c->broken = 1;
			return N2VCD_E_HW;
		}
		memcpy(&hd, buf, sizeof(hd));
		if (hd.magic != N2VCD_MAGIC) {
			c->broken = 1;
			return N2VCD_E_HW;
		}
		if (hd.seq == 0) {              /* unsolicited error: session death, not a reply */
			if (hd.type != N2VCD_ERROR || (uint32_t)n < sizeof(struct n2vcd_simple)) {
				c->broken = 1;
				return N2VCD_E_HW;
			}
			if (expect_session != 0 && expect_session != N2VCD_ANY_SESSION &&
			    hd.session != expect_session) {
				continue;               /* not about the session we are waiting for */
			}
			struct n2vcd_simple s;
			memcpy(&s, buf, sizeof(s));
			atomic_store(&c->last_detail, s.detail);
			return s.status != 0 ? (int)s.status : N2VCD_E_HW;
		}
		if (hd.seq != seq) {            /* cannot resynchronise a one-reply-per-request stream */
			c->broken = 1;
			return N2VCD_E_HW;
		}
		if (hd.type != N2VCD_ERROR && expect_session != N2VCD_ANY_SESSION &&
		    hd.session != expect_session) {
			c->broken = 1;          /* a reply for another session cannot be matched */
			return N2VCD_E_SESSION;
		}
		if (hd.type == N2VCD_ERROR) {
			if ((uint32_t)n < sizeof(struct n2vcd_simple)) {
				c->broken = 1;
				return N2VCD_E_HW;
			}
			struct n2vcd_simple s;
			memcpy(&s, buf, sizeof(s));
			atomic_store(&c->last_detail, s.detail);
			return s.status != 0 ? (int)s.status : N2VCD_E_HW;
		}
		if ((uint32_t)n > cap) {        /* reply larger than the caller's struct: the header lied */
			c->broken = 1;
			return N2VCD_E_HW;
		}
		memcpy(rep, buf, (size_t)n);
		*got = (uint32_t)n;
		return N2VCD_E_OK;
	}
}

void
n2vcd_set_timeout(struct n2vcd_client *c, uint32_t timeout_ms)
{
	if (!c) { return; }
	atomic_store(&c->timeout_ms, timeout_ms ? timeout_ms : N2VCD_CLIENT_TIMEOUT_MS);
}

uint32_t
n2vcd_last_errno(const struct n2vcd_client *c)
{
	return c ? atomic_load(&c->last_errno) : 0;
}

uint32_t
n2vcd_last_detail(const struct n2vcd_client *c)
{
	return c ? atomic_load(&c->last_detail) : 0;
}

int
n2vcd_info(const struct n2vcd_client *c, struct n2vcd_caps *caps)
{
	if (!c || !caps) { return N2VCD_E_ARGS; }
	*caps = c->caps;
	return N2VCD_E_OK;
}

int
n2vcd_connect(struct n2vcd_client **out, const struct n2vcd_transport *t,
              uint32_t timeout_ms, char *err, size_t errlen)
{
	if (!out || !t) {
		set_err(err, errlen, "connect: null argument");
		return N2VCD_E_ARGS;
	}
	*out = NULL;
	if (!t->send || !t->recv || !t->buf_alloc || !t->buf_rw || !t->buf_free) {
		set_err(err, errlen, "connect: transport is missing a method");
		return N2VCD_E_ARGS;
	}
	struct n2vcd_client *c = (struct n2vcd_client *)calloc(1, sizeof(*c));
	if (!c) {
		set_err(err, errlen, "connect: out of memory");
		return N2VCD_E_NOMEM;
	}
	c->t = *t;
	if (pthread_mutex_init(&c->lock, NULL) != 0) {
		free(c);
		set_err(err, errlen, "connect: mutex init failed");
		return N2VCD_E_NOMEM;
	}
	atomic_store(&c->timeout_ms, timeout_ms ? timeout_ms : N2VCD_CLIENT_TIMEOUT_MS);

	struct n2vcd_hello req;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_HELLO;
	req.version = N2VCD_VERSION;

	struct n2vcd_hello_ok ok;
	memset(&ok, 0, sizeof(ok));
	uint32_t got = 0;
	int r = xact(c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, 0);
	if (r != N2VCD_E_OK) {
		uint32_t detail = atomic_load(&c->last_detail);
		if (detail) {
			set_err(err, errlen, "HELLO: %s (detail %u)", n2vcd_strstatus(r), detail);
		} else {
			set_err(err, errlen, "HELLO: %s", n2vcd_strstatus(r));
		}
		pthread_mutex_destroy(&c->lock);
		free(c);
		return r;
	}
	if (got != sizeof(ok) || ok.h.type != N2VCD_HELLO_OK || ok.h.session != 0) {
		set_err(err, errlen, "HELLO: malformed reply (%u bytes, type %u, session %u)",
		        got, ok.h.type, ok.h.session);
		pthread_mutex_destroy(&c->lock);
		free(c);
		return N2VCD_E_HW;
	}
	if (ok.version != N2VCD_VERSION) {
		set_err(err, errlen, "HELLO: server version %u, client %u", ok.version, N2VCD_VERSION);
		pthread_mutex_destroy(&c->lock);
		free(c);
		return N2VCD_E_VERSION;
	}
	c->caps.version = ok.version;
	c->caps.enc_codecs = ok.enc_codecs;
	c->caps.dec_codecs = ok.dec_codecs;
	c->caps.max_width = ok.max_width;
	c->caps.max_height = ok.max_height;
	*out = c;
	return N2VCD_E_OK;
}

int
n2vcd_connect_path(struct n2vcd_client **out, const char *socket_path, const char *dev_path,
                   uint32_t timeout_ms, char *err, size_t errlen)
{
	if (!out) {
		set_err(err, errlen, "connect: null output");
		return N2VCD_E_ARGS;
	}
	*out = NULL;
	if (socket_path && dev_path) {
		set_err(err, errlen, "connect: socket_path and dev_path are mutually exclusive");
		return N2VCD_E_ARGS;
	}
	struct n2vcd_transport t;
	memset(&t, 0, sizeof(t));
	int tr = socket_path ? n2vcd_transport_socket(&t, socket_path)
	                     : n2vcd_transport_relay(&t, dev_path);
	if (tr != 0) {
		set_err(err, errlen, "connect: %s transport: %s", socket_path ? "socket" : "relay",
		        strerror(-tr));
		return tr == -ENOMEM ? N2VCD_E_NOMEM : N2VCD_E_HW;
	}
	int r = n2vcd_connect(out, &t, timeout_ms, err, errlen);
	if (r != N2VCD_E_OK && t.close) { t.close(t.ctx); }
	return r;
}

int
n2vcd_status_errno(int status)
{
	switch (status) {
	case N2VCD_E_OK: return 0;
	case N2VCD_E_VERSION: return -EPROTO;
	case N2VCD_E_UNSUPPORTED: return -EOPNOTSUPP;
	case N2VCD_E_BUSY: return -EBUSY;
	case N2VCD_E_ARGS: return -EINVAL;
	case N2VCD_E_HW: return -EIO;
	case N2VCD_E_GUEST: return -EIO;
	case N2VCD_E_NOMEM: return -ENOMEM;
	case N2VCD_E_SESSION: return -EPIPE;
	default: return -EIO;
	}
}

void
n2vcd_disconnect(struct n2vcd_client *c)
{
	if (!c) { return; }
	if (c->t.close) { c->t.close(c->t.ctx); }
	pthread_mutex_destroy(&c->lock);
	free(c);
}

static uint32_t
align16(uint32_t v)
{
	return (v + 15u) & ~15u;
}

uint32_t n2vcd_session_id(const struct n2vcd_session *s) { return s ? s->id : 0; }
uint32_t n2vcd_session_width(const struct n2vcd_session *s) { return s ? s->width : 0; }
uint32_t n2vcd_session_height(const struct n2vcd_session *s) { return s ? s->height : 0; }
uint32_t n2vcd_session_stride(const struct n2vcd_session *s) { return s ? s->stride : 0; }
uint32_t n2vcd_session_in_bytes(const struct n2vcd_session *s) { return s ? s->in_bytes : 0; }
uint32_t n2vcd_session_out_bytes(const struct n2vcd_session *s) { return s ? s->out_bytes : 0; }

static int
session_mark(struct n2vcd_session *s, int status)
{
	if (s && (status == N2VCD_E_GUEST || status == N2VCD_E_SESSION)) { s->alive = 0; }
	return status;
}

/* Release a server session that OPEN_OK created but the client could not finish setting up.  Best effort: the
 * caller is about to return an allocation error; a transport failure here marks the client broken through xact. */
static void
close_opened_session(struct n2vcd_client *c, uint32_t id)
{
	struct n2vcd_simple close;
	memset(&close, 0, sizeof(close));
	close.h.type = N2VCD_CLOSE;
	close.h.session = id;
	struct n2vcd_simple reply;
	memset(&reply, 0, sizeof(reply));
	uint32_t got = 0;
	(void)xact(c, &close, (uint32_t)sizeof(close), &reply, (uint32_t)sizeof(reply), &got, id);
}

int
n2vcd_enc_open(struct n2vcd_client *c, const struct n2vcd_enc_params *p,
               struct n2vcd_session **out, char *err, size_t errlen)
{
	if (!out) { return N2VCD_E_ARGS; }
	*out = NULL;
	if (!c || !p) {
		set_err(err, errlen, "open: null argument");
		return N2VCD_E_ARGS;
	}
	if (p->encoder != 1u) {
		set_err(err, errlen, "open: encoder must be 1 (no decoder API in protocol v1)");
		return N2VCD_E_UNSUPPORTED;
	}
	if (p->width < 16u || p->height < 16u || p->width > 65535u || p->height > 65535u ||
	    (p->width & 1u) || (p->height & 1u)) {
		set_err(err, errlen, "open: bad picture size %ux%u (min 16, even)", p->width, p->height);
		return N2VCD_E_ARGS;
	}

	struct n2vcd_open req;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_OPEN;
	req.codec = p->codec;
	req.encoder = p->encoder;
	req.width = p->width;
	req.height = p->height;
	req.fps_num = p->fps_num;
	req.fps_den = p->fps_den;
	req.bitrate = p->bitrate;
	req.gop = p->gop;
	req.profile = p->profile;
	req.level = p->level;
	req.flags = p->flags;

	pthread_mutex_lock(&c->lock);
	struct n2vcd_open_ok ok;
	memset(&ok, 0, sizeof(ok));
	uint32_t got = 0;
	int r = xact(c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, N2VCD_ANY_SESSION);
	if (r != N2VCD_E_OK) {
		set_err(err, errlen, "OPEN: %s (detail %u)", n2vcd_strstatus(r), atomic_load(&c->last_detail));
		pthread_mutex_unlock(&c->lock);
		return r;
	}
	if (got != sizeof(ok) || ok.h.type != N2VCD_OPEN_OK) {
		set_err(err, errlen, "OPEN: malformed reply (%u bytes, type %u)", got, ok.h.type);
		c->broken = 1;                  /* an OPEN_OK opened a server session we cannot name reliably */
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_HW;
	}
	if (ok.h.session == 0) {
		set_err(err, errlen, "OPEN: server returned session 0");
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_SESSION;
	}
	if (ok.status != N2VCD_E_OK) {
		set_err(err, errlen, "OPEN: status %s", n2vcd_strstatus((int)ok.status));
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return (int)ok.status;
	}
	if (ok.in_buf_bytes == 0u || ok.out_buf_bytes == 0u ||
	    ok.in_buf_bytes > N2VCD_CLIENT_BUF_MAX || ok.out_buf_bytes > N2VCD_CLIENT_BUF_MAX) {
		set_err(err, errlen, "OPEN: implausible buffer sizes in=%u out=%u", ok.in_buf_bytes, ok.out_buf_bytes);
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_HW;
	}

	uint32_t stride = align16(p->width);
	uint32_t rows = align16(p->height);
	uint64_t need = (uint64_t)stride * rows * 3u / 2u;
	if (need == 0 || need > 0xffffffffu || ok.in_buf_bytes < need) {
		set_err(err, errlen, "OPEN: in_buf_bytes %u < NV12 %ux%u (%llu)", ok.in_buf_bytes, stride, rows,
		        (unsigned long long)need);
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_HW;
	}

	struct n2vcd_session *s = (struct n2vcd_session *)calloc(1, sizeof(*s));
	if (!s) {
		set_err(err, errlen, "OPEN: out of memory");
		close_opened_session(c, ok.h.session);
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_NOMEM;
	}
	s->c = c;
	s->id = ok.h.session;
	s->width = p->width;
	s->height = p->height;
	s->stride = stride;
	s->in_bytes = ok.in_buf_bytes;
	s->out_bytes = ok.out_buf_bytes;

	int br = c->t.buf_alloc(c->t.ctx, s->in_bytes, &s->in_handle);
	if (br != 0 || s->in_handle == 0) {
		if (br < 0) { atomic_store(&c->last_errno, (uint32_t)(-br)); }
		set_err(err, errlen, "OPEN: input buffer (%u bytes): %s", s->in_bytes,
		        br < 0 ? strerror(-br) : "no handle");
		/* Release the server session we just created.  Failure is not recoverable here: the caller gets the
		 * allocation error; n2vcd_disconnect() closes the transport, which drops the session on the phone. */
		close_opened_session(c, s->id);
		free(s);
		pthread_mutex_unlock(&c->lock);
		return br == -ENOMEM ? N2VCD_E_NOMEM : N2VCD_E_HW;
	}
	br = c->t.buf_alloc(c->t.ctx, s->out_bytes, &s->out_handle);
	if (br != 0 || s->out_handle == 0) {
		if (br < 0) { atomic_store(&c->last_errno, (uint32_t)(-br)); }
		set_err(err, errlen, "OPEN: output buffer (%u bytes): %s", s->out_bytes,
		        br < 0 ? strerror(-br) : "no handle");
		(void)c->t.buf_free(c->t.ctx, s->in_handle);
		close_opened_session(c, s->id);
		free(s);
		pthread_mutex_unlock(&c->lock);
		return br == -ENOMEM ? N2VCD_E_NOMEM : N2VCD_E_HW;
	}

	s->alive = 1;
	*out = s;
	pthread_mutex_unlock(&c->lock);
	return N2VCD_E_OK;
}

/* Copy one plane into the DMA buffer.  The contiguous path needs (rows-1)*pitch + row_bytes source bytes. */
static int
copy_plane(struct n2vcd_client *c, uint32_t handle, uint32_t at, const uint8_t *src,
           uint32_t rows, uint32_t row_bytes, uint32_t src_pitch, uint32_t dst_pitch)
{
	if (rows == 0 || row_bytes == 0) { return 0; }
	if (src_pitch == dst_pitch) {
		uint64_t contiguous = (uint64_t)(rows - 1u) * dst_pitch + row_bytes;
		if (contiguous > 0xffffffffu) { return -EINVAL; }
		return c->t.buf_rw(c->t.ctx, handle, at, (void *)(uintptr_t)src, (uint32_t)contiguous, 1);
	}
	for (uint32_t row = 0; row < rows; row++) {
		int r = c->t.buf_rw(c->t.ctx, handle, at + row * dst_pitch,
		                    (void *)(uintptr_t)(src + (size_t)row * src_pitch), row_bytes, 1);
		if (r != 0) { return r; }
	}
	return 0;
}

static int
enc_frame_impl(struct n2vcd_session *s,
               const void *y, const void *chroma,
               uint32_t y_stride, uint32_t chroma_stride,
               uint64_t pts, int force_key,
               void *out, uint32_t cap, struct n2vcd_frame_info *info)
{
	if (info) { memset(info, 0, sizeof(*info)); }
	if (!s || !y || !chroma) { return N2VCD_E_ARGS; }
	if (y_stride < s->width || chroma_stride < s->width) { return N2VCD_E_ARGS; }
	if (out != NULL && cap < s->out_bytes) { return N2VCD_E_ARGS; }

	struct n2vcd_client *c = s->c;
	pthread_mutex_lock(&c->lock);
	if (!s->alive) {
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_SESSION;
	}
	if (c->broken) {
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_SESSION;
	}

	uint32_t chroma_rows = s->height / 2u;
	uint32_t chroma_at = s->stride * align16(s->height);
	uint64_t chroma_end = (uint64_t)chroma_at + (uint64_t)(chroma_rows ? chroma_rows - 1u : 0u) * s->stride +
	                      s->width;
	if (chroma_rows == 0 || chroma_end > s->in_bytes) {
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_HW;              /* OPEN_OK sizes do not describe an NV12 picture of this geometry */
	}

	int r = copy_plane(c, s->in_handle, 0, (const uint8_t *)y, s->height, s->width, y_stride, s->stride);
	if (r == 0) {
		r = copy_plane(c, s->in_handle, chroma_at, (const uint8_t *)chroma, chroma_rows, s->width,
		               chroma_stride, s->stride);
	}
	if (r != 0) {
		atomic_store(&c->last_errno, (uint32_t)(-r));
		pthread_mutex_unlock(&c->lock);
		return map_errno(-r);
	}

	struct n2vcd_enc_frame req;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_ENC_FRAME;
	req.h.session = s->id;
	req.in_handle = s->in_handle;
	req.stride = s->stride;
	req.out_handle = s->out_handle;
	req.out_capacity = s->out_bytes;
	req.pts = pts;
	req.force_key = force_key ? 1u : 0u;

	struct n2vcd_enc_frame_ok ok;
	memset(&ok, 0, sizeof(ok));
	uint32_t got = 0;
	r = xact(c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, s->id);
	if (r != N2VCD_E_OK) {
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, r);
	}
	if (got != sizeof(ok) || ok.h.type != N2VCD_ENC_FRAME_OK || ok.h.session != s->id) {
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, N2VCD_E_HW);
	}
	if (ok.status != N2VCD_E_OK) {
		int st = (int)ok.status;
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, st);
	}
	if (ok.pts != pts) {
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, N2VCD_E_HW);
	}
	if (ok.out_bytes > s->out_bytes) {
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, N2VCD_E_HW);
	}

	if (out != NULL && ok.out_bytes > 0u) {
		int br = c->t.buf_rw(c->t.ctx, s->out_handle, 0, out, ok.out_bytes, 0);
		if (br != 0) {
			atomic_store(&c->last_errno, (uint32_t)(-br));
			pthread_mutex_unlock(&c->lock);
			return map_errno(-br);
		}
	}

	if (info) {
		info->bytes = ok.out_bytes;
		info->key_frame = ok.key_frame ? 1u : 0u;
		info->hw_us = ok.hw_us;
		info->header_bytes = ok.header_bytes;
	}
	pthread_mutex_unlock(&c->lock);
	return N2VCD_E_OK;
}

int
n2vcd_enc_frame(struct n2vcd_session *s,
                const void *y, const void *chroma,
                uint32_t y_stride, uint32_t chroma_stride,
                uint64_t pts, int force_key,
                void *out, uint32_t cap, uint32_t *out_bytes, int *key)
{
	struct n2vcd_frame_info info;
	int r = enc_frame_impl(s, y, chroma, y_stride, chroma_stride, pts, force_key, out, cap, &info);
	if (out_bytes) { *out_bytes = info.bytes; }
	if (key) { *key = info.key_frame ? 1 : 0; }
	return r;
}

int
n2vcd_enc_frame_ex(struct n2vcd_session *s,
                   const void *y, const void *chroma,
                   uint32_t y_stride, uint32_t chroma_stride,
                   uint64_t pts, int force_key,
                   void *out, uint32_t cap, struct n2vcd_frame_info *info)
{
	return enc_frame_impl(s, y, chroma, y_stride, chroma_stride, pts, force_key, out, cap, info);
}

int
n2vcd_enc_set(struct n2vcd_session *s, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den, int force_key)
{
	if (!s) { return N2VCD_E_ARGS; }
	struct n2vcd_client *c = s->c;
	pthread_mutex_lock(&c->lock);
	if (!s->alive || c->broken) {
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_SESSION;
	}
	struct n2vcd_enc_set req;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_ENC_SET;
	req.h.session = s->id;
	req.bitrate = bitrate;
	req.fps_num = fps_num;
	req.fps_den = fps_den;
	req.force_key = force_key ? 1u : 0u;

	struct n2vcd_simple ok;
	memset(&ok, 0, sizeof(ok));
	uint32_t got = 0;
	int r = xact(c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, s->id);
	if (r != N2VCD_E_OK) {
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, r);
	}
	if (got != sizeof(ok) || ok.h.type != N2VCD_ENC_SET_OK || ok.h.session != s->id) {
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, N2VCD_E_HW);
	}
	int st = (int)ok.status;
	pthread_mutex_unlock(&c->lock);
	return session_mark(s, st);
}

int
n2vcd_flush(struct n2vcd_session *s)
{
	if (!s) { return N2VCD_E_ARGS; }
	struct n2vcd_client *c = s->c;
	pthread_mutex_lock(&c->lock);
	if (!s->alive || c->broken) {
		pthread_mutex_unlock(&c->lock);
		return N2VCD_E_SESSION;
	}
	struct n2vcd_simple req;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_FLUSH;
	req.h.session = s->id;
	struct n2vcd_simple ok;
	memset(&ok, 0, sizeof(ok));
	uint32_t got = 0;
	int r = xact(c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, s->id);
	if (r != N2VCD_E_OK) {
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, r);
	}
	if (got != sizeof(ok) || ok.h.type != N2VCD_FLUSH_OK || ok.h.session != s->id) {
		c->broken = 1;
		pthread_mutex_unlock(&c->lock);
		return session_mark(s, N2VCD_E_HW);
	}
	int st = (int)ok.status;
	pthread_mutex_unlock(&c->lock);
	return session_mark(s, st);
}

int
n2vcd_close(struct n2vcd_session *s)
{
	if (!s) { return N2VCD_E_ARGS; }
	struct n2vcd_client *c = s->c;
	pthread_mutex_lock(&c->lock);
	int status = N2VCD_E_OK;
	if (s->alive && !c->broken) {
		struct n2vcd_simple req;
		memset(&req, 0, sizeof(req));
		req.h.type = N2VCD_CLOSE;
		req.h.session = s->id;
		struct n2vcd_simple ok;
		memset(&ok, 0, sizeof(ok));
		uint32_t got = 0;
		int r = xact(c, &req, (uint32_t)sizeof(req), &ok, (uint32_t)sizeof(ok), &got, s->id);
		if (r != N2VCD_E_OK) {
			status = r;
		} else if (got != sizeof(ok) || ok.h.type != N2VCD_CLOSE_OK || ok.h.session != s->id) {
			c->broken = 1;
			status = N2VCD_E_HW;
		} else if (ok.status != N2VCD_E_OK) {
			status = (int)ok.status;
		}
	}
	s->alive = 0;
	if (s->out_handle) {
		int br = c->t.buf_free(c->t.ctx, s->out_handle);
		if (status == N2VCD_E_OK && br != 0) {
			atomic_store(&c->last_errno, (uint32_t)(-br));
			status = map_errno(-br);
		}
		s->out_handle = 0;
	}
	if (s->in_handle) {
		int br = c->t.buf_free(c->t.ctx, s->in_handle);
		if (status == N2VCD_E_OK && br != 0) {
			atomic_store(&c->last_errno, (uint32_t)(-br));
			status = map_errno(-br);
		}
		s->in_handle = 0;
	}
	pthread_mutex_unlock(&c->lock);
	free(s);
	return status;
}
