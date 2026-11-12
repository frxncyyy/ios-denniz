/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_client_proto.c -- the decoder client seam implemented directly over the frozen struct n2vcd_transport
 * (include/n2vcd_proto.h), speaking the rev-2 decoder messages (DEC05's text, daemon/dec/n2vcd_proto_dec_rev2.h.txt:
 * DEC_ADD_BUFFER with the optional second plane, OPEN.flags bits 8..15 = layout, DEC_PICTURE_OK last word = poc).
 *
 * This is the stand-in for DEC05's client library.  It is a real client, not a stub: OPEN(decoder), DEC_HEADER,
 * DEC_ADD_BUFFER, DEC_INPUT, DEC_PICTURE, DEC_RELEASE, FREE_BUFFERS, FLUSH, CLOSE, each seq-checked against its reply,
 * with the transport's timeout; the host client test drives it against an in-memory daemon that implements the same
 * messages.  The product build uses n2vtd_client_dec05.c instead when client/dec/n2vcd_dec.h is present.
 */
#define _POSIX_C_SOURCE 200809L
#include "n2vtd_client.h"

#include <stdlib.h>
#include <string.h>

#include "n2vcd_proto.h"

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2VTD_WEAK __attribute__((weak_import))
#else
#define N2VTD_WEAK __attribute__((weak))
#endif

extern int n2vcd_transport_relay(struct n2vcd_transport *t, const char *dev_path) N2VTD_WEAK;
extern int n2vcd_transport_socket(struct n2vcd_transport *t, const char *unix_path) N2VTD_WEAK;

#define N2VTD_RELAY_PATH "/dev/redwood-vcodec-client"
#define N2VTD_MAX_POOL 16

enum {
	N2VTD_W_DEC_ADD_BUFFER = 20, N2VTD_W_DEC_ADD_BUFFER_OK,
	N2VTD_W_DEC_FREE_BUFFERS, N2VTD_W_DEC_FREE_BUFFERS_OK,
	N2VTD_W_DEC_INPUT, N2VTD_W_DEC_INPUT_OK,
	N2VTD_W_DEC_PICTURE, N2VTD_W_DEC_PICTURE_OK,
	N2VTD_W_DEC_RELEASE, N2VTD_W_DEC_RELEASE_OK,
};
enum { N2VTD_W_OPEN_FMT_SHIFT = 8, N2VTD_W_OPEN_FMT_MM21 = 1, N2VTD_W_OPEN_FMT_NV12 = 2 };

struct n2vtd_w_add_buffer {
	struct n2vcd_hdr h;
	uint32_t handle, bytes;
	uint32_t c_handle, c_bytes;
};
struct n2vtd_w_add_buffer_ok { struct n2vcd_hdr h; uint32_t status, index; };
struct n2vtd_w_input { struct n2vcd_hdr h; uint32_t in_handle, in_bytes; uint64_t pts; uint32_t flags, reserved; };
struct n2vtd_w_input_ok {
	struct n2vcd_hdr h;
	uint32_t status, pictures_ready, changed, error_map, hw_us, reserved;
};
struct n2vtd_w_picture { struct n2vcd_hdr h; };
struct n2vtd_w_picture_ok {
	struct n2vcd_hdr h;
	uint32_t status, have;
	uint64_t pts;
	uint32_t index, width, height, stride, buf_h, fourcc, pictures_ready;
	int32_t poc;
};
struct n2vtd_w_release { struct n2vcd_hdr h; uint32_t index, reserved; };

_Static_assert(sizeof(struct n2vcd_hdr) == 16, "n2vcd_hdr is 16 bytes");
_Static_assert(sizeof(struct n2vtd_w_add_buffer) == 32, "DEC_ADD_BUFFER rev 2 is 32 bytes");
_Static_assert(sizeof(struct n2vtd_w_add_buffer_ok) == 24, "DEC_ADD_BUFFER_OK is 24 bytes");
_Static_assert(sizeof(struct n2vtd_w_input) == 40, "DEC_INPUT is 40 bytes");
_Static_assert(sizeof(struct n2vtd_w_input_ok) == 40, "DEC_INPUT_OK is 40 bytes");
_Static_assert(sizeof(struct n2vtd_w_picture) == 16, "DEC_PICTURE is 16 bytes");
_Static_assert(sizeof(struct n2vtd_w_picture_ok) == 64, "DEC_PICTURE_OK is 64 bytes");
_Static_assert(sizeof(struct n2vtd_w_release) == 24, "DEC_RELEASE is 24 bytes");

struct n2vtd_conn {
	struct n2vcd_transport t;
	uint32_t next_seq;
	uint32_t session;
	uint32_t timeout_ms;
	uint32_t in_handle;
	uint32_t in_bytes;
	uint32_t y_bytes;
	uint32_t out_bytes;
	struct {
		uint32_t handle;
		uint32_t bytes;
		int used;
	} pool[N2VTD_MAX_POOL];
	uint32_t npool;
};

struct n2vtd_proto {
	struct n2vtd_conn *conn;
	uint32_t timeout_ms;
};

static struct n2vtd_proto g_proto;

static int status_errno(int status)
{
	switch (status) {
	case N2VCD_E_OK:
		return 0;
	case N2VCD_E_VERSION:
	case N2VCD_E_UNSUPPORTED:
	case N2VCD_E_ARGS:
		return N2VTD_CLIENT_E_PROTO;
	case N2VCD_E_BUSY:
	case N2VCD_E_SESSION:
		return N2VTD_CLIENT_E_SESSION;
	case N2VCD_E_HW:
	case N2VCD_E_GUEST:
		return N2VTD_CLIENT_E_HW;
	case N2VCD_E_NOMEM:
		return N2VTD_CLIENT_E_NOMEM;
	default:
		return N2VTD_CLIENT_E_PROTO;
	}
}

static int xact(struct n2vtd_conn *c, void *req, uint32_t req_bytes, void *reply, uint32_t reply_cap,
                uint32_t want_type)
{
	uint32_t seq = c->next_seq++;
	struct n2vcd_hdr *rh = (struct n2vcd_hdr *)reply;
	int n;

	((struct n2vcd_hdr *)req)->magic = N2VCD_MAGIC;
	((struct n2vcd_hdr *)req)->seq = seq;
	((struct n2vcd_hdr *)req)->session = c->session;
	n = c->t.send(c->t.ctx, req, req_bytes);
	if (n != 0)
		return (n == -2 || n == -11) ? N2VTD_CLIENT_E_TIMEOUT : N2VTD_CLIENT_E_SESSION;
	n = c->t.recv(c->t.ctx, reply, reply_cap, c->timeout_ms);
	if (n == -2 || n == -11 || n == 0)
		return N2VTD_CLIENT_E_TIMEOUT;
	if (n < (int)sizeof(*rh))
		return N2VTD_CLIENT_E_PROTO;
	if (rh->magic != N2VCD_MAGIC || rh->seq != seq)
		return N2VTD_CLIENT_E_PROTO;
	if (rh->type == N2VCD_ERROR) {
		struct n2vcd_simple *e = (struct n2vcd_simple *)reply;
		if (n < (int)sizeof(*e))
			return N2VTD_CLIENT_E_PROTO;
		return status_errno((int)e->status);
	}
	if (rh->type != want_type)
		return N2VTD_CLIENT_E_PROTO;
	return 0;
}

static int proto_connect(void *ctx, void **conn_out, const char *socket_path, const char *device_path,
                         uint32_t timeout_ms)
{
	struct n2vtd_proto *p = (struct n2vtd_proto *)ctx;
	struct n2vtd_conn *c;
	struct n2vcd_hello hello;
	struct n2vcd_hello_ok ok;
	int rc;

	if (!p || !conn_out)
		return N2VTD_CLIENT_E_ARGS;
	*conn_out = NULL;
	c = (struct n2vtd_conn *)calloc(1, sizeof(*c));
	if (!c)
		return N2VTD_CLIENT_E_NOMEM;
	c->next_seq = 1;
	c->timeout_ms = timeout_ms ? timeout_ms : p->timeout_ms;
	if (socket_path) {
		if (n2vcd_transport_socket == NULL) {
			free(c);
			return N2VTD_CLIENT_ABSENT;
		}
		rc = n2vcd_transport_socket(&c->t, socket_path);
	} else {
		if (n2vcd_transport_relay == NULL) {
			free(c);
			return N2VTD_CLIENT_ABSENT;
		}
		rc = n2vcd_transport_relay(&c->t, device_path ? device_path : N2VTD_RELAY_PATH);
	}
	if (rc != 0) {
		free(c);
		return N2VTD_CLIENT_ABSENT;
	}
	memset(&hello, 0, sizeof(hello));
	hello.h.type = N2VCD_HELLO;
	/* N2VCD_VERSION stays 1 (lead decision 2026-11-11): the decoder ids 20..29 are additive and the daemon advertises
	 * them with HELLO_OK.dec_codecs.  A bump would break the encoder clients that compare the version for equality. */
	hello.version = N2VCD_VERSION;
	rc = xact(c, &hello, sizeof(hello), &ok, sizeof(ok), N2VCD_HELLO_OK);
	if (rc != 0) {
		c->t.close(c->t.ctx);
		free(c);
		return (rc == N2VTD_CLIENT_E_PROTO) ? N2VTD_CLIENT_ABSENT : rc;
	}
	if (ok.version != N2VCD_VERSION || ok.dec_codecs == 0) {
		/* A daemon without decoder capability bits has no decoder service. */
		c->t.close(c->t.ctx);
		free(c);
		return N2VTD_CLIENT_ABSENT;
	}
	p->conn = c;
	*conn_out = c;
	return 0;
}

static void proto_disconnect(void *ctx, void *conn)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;

	(void)ctx;
	if (!c)
		return;
	c->t.close(c->t.ctx);
	free(c);
}

static int proto_open(void *ctx, void *conn, uint32_t codec, uint32_t max_width, uint32_t max_height,
                      uint32_t format, uint32_t *in_bytes)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vcd_open req;
	struct n2vcd_open_ok ok;
	int rc;

	(void)ctx;
	if (!c || !in_bytes)
		return N2VTD_CLIENT_E_ARGS;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_OPEN;
	req.codec = codec;
	req.encoder = 0;
	req.width = max_width;
	req.height = max_height;
	req.flags = (format == 0x3231564eu /* NV12 */ ? N2VTD_W_OPEN_FMT_NV12 : N2VTD_W_OPEN_FMT_MM21)
	            << N2VTD_W_OPEN_FMT_SHIFT;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VCD_OPEN_OK);
	if (rc != 0)
		return rc;
	if (ok.status != 0)
		return status_errno((int)ok.status);
	c->session = ok.h.session;
	*in_bytes = ok.in_buf_bytes;
	if (c->in_bytes == 0) {
		c->in_bytes = ok.in_buf_bytes ? ok.in_buf_bytes : 1;
		rc = c->t.buf_alloc(c->t.ctx, c->in_bytes, &c->in_handle);
		if (rc != 0)
			return N2VTD_CLIENT_E_NOMEM;
	}
	return 0;
}

static void pool_free(struct n2vtd_conn *c)
{
	uint32_t i;

	for (i = 0; i < N2VTD_MAX_POOL; i++) {
		if (c->pool[i].used && c->t.buf_free)
			c->t.buf_free(c->t.ctx, c->pool[i].handle);
		memset(&c->pool[i], 0, sizeof(c->pool[i]));
	}
	c->npool = 0;
}

static int proto_header(void *ctx, void *conn, const uint8_t *annexb, uint32_t bytes, struct n2vtd_header *hdr)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vcd_dec_header req;
	struct n2vcd_dec_header_ok ok;
	int rc;

	(void)ctx;
	if (!c || !hdr || (!annexb && bytes))
		return N2VTD_CLIENT_E_ARGS;
	if (!c->t.buf_alloc || !c->t.buf_rw)
		return N2VTD_CLIENT_ABSENT;
	pool_free(c);
	if (bytes && c->t.buf_rw(c->t.ctx, c->in_handle, 0, (void *)annexb, bytes, 1) != (int)bytes)
		return N2VTD_CLIENT_E_PROTO;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_DEC_HEADER;
	req.in_handle = c->in_handle;
	req.in_bytes = bytes;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VCD_DEC_HEADER_OK);
	if (rc != 0)
		return rc;
	if (ok.status != 0)
		return status_errno((int)ok.status);
	hdr->width = ok.width;
	hdr->height = ok.height;
	hdr->stride = ok.stride;
	hdr->out_bytes = ok.out_buf_bytes;
	hdr->dpb_frames = ok.dpb_frames;
	hdr->buf_h = ok.reserved[1];
	hdr->format = ok.reserved[0];
	hdr->y_bytes = ok.stride * ok.reserved[1];
	hdr->crop_x = 0;
	hdr->crop_y = 0;
	if (hdr->width == 0 || hdr->height == 0 || hdr->stride < hdr->width || hdr->out_bytes == 0 ||
	    hdr->dpb_frames == 0 || hdr->dpb_frames > N2VTD_MAX_POOL || hdr->buf_h == 0 ||
	    hdr->buf_h < hdr->height || hdr->y_bytes > hdr->out_bytes)
		return N2VTD_CLIENT_E_PROTO;
	c->y_bytes = hdr->y_bytes;
	c->out_bytes = hdr->out_bytes;
	return 0;
}

static int proto_add_pool(void *ctx, void *conn, const struct n2vtd_header *hdr)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	uint32_t i;

	(void)ctx;
	if (!c || !hdr)
		return N2VTD_CLIENT_E_ARGS;
	if (!c->t.buf_alloc)
		return N2VTD_CLIENT_ABSENT;
	if (hdr->dpb_frames > N2VTD_MAX_POOL)
		return N2VTD_CLIENT_E_ARGS;
	for (i = 0; i < hdr->dpb_frames; i++) {
		struct n2vtd_w_add_buffer req;
		struct n2vtd_w_add_buffer_ok ok;
		uint32_t handle = 0;
		int rc = c->t.buf_alloc(c->t.ctx, hdr->out_bytes, &handle);

		if (rc != 0) {
			pool_free(c);
			return N2VTD_CLIENT_E_NOMEM;
		}
		memset(&req, 0, sizeof(req));
		req.h.type = N2VTD_W_DEC_ADD_BUFFER;
		req.handle = handle;
		req.bytes = hdr->out_bytes;
		req.c_handle = 0;
		req.c_bytes = 0;
		rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VTD_W_DEC_ADD_BUFFER_OK);
		if (rc != 0) {
			c->t.buf_free(c->t.ctx, handle);
			pool_free(c);
			return rc;
		}
		if (ok.status != 0 || ok.index >= N2VTD_MAX_POOL) {
			c->t.buf_free(c->t.ctx, handle);
			pool_free(c);
			return status_errno((int)ok.status) ? status_errno((int)ok.status) : N2VTD_CLIENT_E_PROTO;
		}
		c->pool[ok.index].used = 1;
		c->pool[ok.index].handle = handle;
		c->pool[ok.index].bytes = hdr->out_bytes;
		c->npool++;
	}
	return 0;
}

static int proto_input(void *ctx, void *conn, const uint8_t *au, uint32_t bytes, uint64_t pts,
                       struct n2vtd_input *result)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vtd_w_input req;
	struct n2vtd_w_input_ok ok;
	int rc;

	(void)ctx;
	if (!c || !result || !au || bytes == 0 || bytes > c->in_bytes)
		return N2VTD_CLIENT_E_ARGS;
	if (c->t.buf_rw(c->t.ctx, c->in_handle, 0, (void *)au, bytes, 1) != (int)bytes)
		return N2VTD_CLIENT_E_PROTO;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VTD_W_DEC_INPUT;
	req.in_handle = c->in_handle;
	req.in_bytes = bytes;
	req.pts = pts;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VTD_W_DEC_INPUT_OK);
	if (rc != 0)
		return rc;
	if (ok.status != 0)
		return status_errno((int)ok.status);
	result->pictures_ready = ok.pictures_ready;
	result->changed = ok.changed;
	result->error_map = ok.error_map;
	result->hw_us = ok.hw_us;
	return 0;
}

static int proto_picture(void *ctx, void *conn, struct n2vtd_picture *picture)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vtd_w_picture req;
	struct n2vtd_w_picture_ok ok;
	int rc;

	(void)ctx;
	if (!c || !picture)
		return N2VTD_CLIENT_E_ARGS;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VTD_W_DEC_PICTURE;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VTD_W_DEC_PICTURE_OK);
	if (rc != 0)
		return rc;
	if (ok.status != 0)
		return status_errno((int)ok.status);
	memset(picture, 0, sizeof(*picture));
	picture->have = ok.have;
	picture->pts = ok.pts;
	picture->poc = ok.poc;
	picture->index = ok.index;
	picture->width = ok.width;
	picture->height = ok.height;
	picture->stride = ok.stride;
	picture->buf_h = ok.buf_h;
	picture->format = ok.fourcc;
	picture->pictures_ready = ok.pictures_ready;
	if (picture->have && (picture->width == 0 || picture->height == 0 || picture->stride < picture->width ||
	                      picture->buf_h < picture->height || picture->index >= N2VTD_MAX_POOL ||
	                      !c->pool[picture->index].used))
		return N2VTD_CLIENT_E_PROTO;
	return 0;
}

static int proto_release(void *ctx, void *conn, uint32_t index)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vtd_w_release req;
	struct n2vcd_simple ok;
	int rc;

	(void)ctx;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VTD_W_DEC_RELEASE;
	req.index = index;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VTD_W_DEC_RELEASE_OK);
	if (rc != 0)
		return rc;
	return status_errno((int)ok.status);
}

static int proto_buffer_rw(void *ctx, void *conn, uint32_t index, int plane, uint32_t offset, void *data,
                           uint32_t bytes, int write)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	uint32_t base = 0, size;

	(void)ctx;
	if (!c || !data || index >= N2VTD_MAX_POOL || !c->pool[index].used || !c->t.buf_rw)
		return N2VTD_CLIENT_E_ARGS;
	if (plane == 0) {
		base = 0;
		size = c->y_bytes;
	} else if (plane == 1) {
		base = c->y_bytes;
		size = c->out_bytes - c->y_bytes;
	} else {
		return N2VTD_CLIENT_E_ARGS;
	}
	if (offset > size || bytes > size - offset)
		return N2VTD_CLIENT_E_ARGS;
	return c->t.buf_rw(c->t.ctx, c->pool[index].handle, base + offset, data, bytes, write) == (int)bytes
	           ? 0
	           : N2VTD_CLIENT_E_PROTO;
}

static int proto_free_buffers(void *ctx, void *conn)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vcd_simple req, ok;
	int rc;

	(void)ctx;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VTD_W_DEC_FREE_BUFFERS;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VTD_W_DEC_FREE_BUFFERS_OK);
	pool_free(c);
	if (rc != 0)
		return rc;
	return status_errno((int)ok.status);
}

static int proto_flush(void *ctx, void *conn, uint32_t *pictures_ready)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vcd_simple req, ok;
	int rc;

	(void)ctx;
	if (!c || !pictures_ready)
		return N2VTD_CLIENT_E_ARGS;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_FLUSH;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VCD_FLUSH_OK);
	if (rc != 0)
		return rc;
	rc = status_errno((int)ok.status);
	if (rc != 0)
		return rc;
	*pictures_ready = ok.detail;
	return 0;
}

static int proto_close(void *ctx, void *conn)
{
	struct n2vtd_conn *c = (struct n2vtd_conn *)conn;
	struct n2vcd_simple req, ok;
	int rc;

	(void)ctx;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	memset(&req, 0, sizeof(req));
	req.h.type = N2VCD_CLOSE;
	rc = xact(c, &req, sizeof(req), &ok, sizeof(ok), N2VCD_CLOSE_OK);
	if (rc != 0)
		return rc;
	return status_errno((int)ok.status);
}

static int proto_set_timeout(void *ctx, uint32_t ms)
{
	struct n2vtd_proto *p = (struct n2vtd_proto *)ctx;

	if (!p)
		return N2VTD_CLIENT_E_ARGS;
	p->timeout_ms = ms;
	if (p->conn)
		p->conn->timeout_ms = ms;
	return 0;
}

static const struct n2vtd_client g_proto_table = {
	.ctx = &g_proto,
	.connect = proto_connect,
	.dec_open = proto_open,
	.dec_header = proto_header,
	.dec_add_pool = proto_add_pool,
	.dec_input = proto_input,
	.dec_picture = proto_picture,
	.dec_release = proto_release,
	.buffer_rw = proto_buffer_rw,
	.dec_free_buffers = proto_free_buffers,
	.dec_flush = proto_flush,
	.dec_close = proto_close,
	.disconnect = proto_disconnect,
	.set_timeout = proto_set_timeout,
};

const struct n2vtd_client *n2vtd_client_proto(void)
{
	if (g_proto.timeout_ms == 0)
		g_proto.timeout_ms = 2000;
	return &g_proto_table;
}
