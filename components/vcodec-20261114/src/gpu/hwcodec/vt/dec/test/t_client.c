/* SPDX-License-Identifier: MIT */
/*
 * t_client.c -- the protocol stand-in of the decoder client seam (n2vtd_client_proto.c) against an in-memory daemon
 * speaking the rev-2 decoder messages over the frozen struct n2vcd_transport.
 *
 * The fake transport is installed through the weak n2vcd_transport_socket() the real client would get from P03; every
 * request is checked field by field (magic, type, seq, session, sizes, plane fields, pts) and the reply paths (OK,
 * ERROR, timeout, wrong seq, wrong type) are driven to their documented client errors.
 */
#include "n2vcd_proto.h"
#include "n2vtd_client.h"
#include "t_support.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_MAX_BUFS 32
#define FAKE_MAX_REPLY 256

enum {
	W_DEC_ADD_BUFFER = 20, W_DEC_ADD_BUFFER_OK,
	W_DEC_FREE_BUFFERS, W_DEC_FREE_BUFFERS_OK,
	W_DEC_INPUT, W_DEC_INPUT_OK,
	W_DEC_PICTURE, W_DEC_PICTURE_OK,
	W_DEC_RELEASE, W_DEC_RELEASE_OK,
};

struct w_add_buffer { struct n2vcd_hdr h; uint32_t handle, bytes; uint32_t c_handle, c_bytes; };
struct w_add_buffer_ok { struct n2vcd_hdr h; uint32_t status, index; };
struct w_input { struct n2vcd_hdr h; uint32_t in_handle, in_bytes; uint64_t pts; uint32_t flags, reserved; };
struct w_input_ok { struct n2vcd_hdr h; uint32_t status, pictures_ready, changed, error_map, hw_us, reserved; };
struct w_picture { struct n2vcd_hdr h; };
struct w_picture_ok {
	struct n2vcd_hdr h;
	uint32_t status, have;
	uint64_t pts;
	uint32_t index, width, height, stride, buf_h, fourcc, pictures_ready;
	int32_t poc;
};
struct w_release { struct n2vcd_hdr h; uint32_t index, reserved; };

struct fake_buf {
	int used;
	uint32_t bytes;
	uint8_t *data;
};

static struct {
	int connected;
	uint32_t session;
	uint32_t next_index;
	struct fake_buf bufs[FAKE_MAX_BUFS];
	uint8_t reply[FAKE_MAX_REPLY];
	uint32_t reply_bytes;
	int have_reply;
	int drop_replies;
	int wrong_seq;
	int wrong_type;
	int error_reply;
	uint32_t error_status;
	uint32_t requests;
	uint32_t last_type, last_seq, last_session;
	uint32_t open_codec, open_encoder, open_w, open_h, open_flags;
	uint32_t header_bytes;
	uint8_t header_data[256];
	uint32_t added, released, freed, flushed;
	uint32_t input_bytes;
	uint64_t input_pts;
	uint32_t input_handle;
	uint32_t last_add_handle, last_add_bytes, last_add_c_handle, last_add_c_bytes;
	uint32_t picture_index;
	uint64_t picture_pts;
} g_fake;

static int fake_send(void *ctx, const void *msg, uint32_t bytes);
static int fake_recv(void *ctx, void *msg, uint32_t capacity, uint32_t timeout_ms);
static int fake_buf_alloc(void *ctx, uint32_t bytes, uint32_t *handle);
static int fake_buf_rw(void *ctx, uint32_t handle, uint32_t offset, void *data, uint32_t bytes, int write);
static int fake_buf_free(void *ctx, uint32_t handle);
static void fake_close(void *ctx);

int n2vcd_transport_socket(struct n2vcd_transport *t, const char *unix_path)
{
	(void)unix_path;
	if (!t)
		return -1;
	memset(t, 0, sizeof(*t));
	t->send = fake_send;
	t->recv = fake_recv;
	t->buf_alloc = fake_buf_alloc;
	t->buf_rw = fake_buf_rw;
	t->buf_free = fake_buf_free;
	t->close = fake_close;
	g_fake.connected = 1;
	return 0;
}

__attribute__((weak)) int n2vcd_transport_relay(struct n2vcd_transport *t, const char *dev_path)
{
	(void)t;
	(void)dev_path;
	return -1;
}

static void reply_reset(void)
{
	g_fake.have_reply = 0;
	g_fake.reply_bytes = 0;
}

static void reply_put(const void *msg, uint32_t bytes)
{
	if (bytes > sizeof(g_fake.reply))
		abort();
	memcpy(g_fake.reply, msg, bytes);
	g_fake.reply_bytes = bytes;
	g_fake.have_reply = 1;
}

static int fake_send(void *ctx, const void *msg, uint32_t bytes)
{
	const struct n2vcd_hdr *h = (const struct n2vcd_hdr *)msg;

	(void)ctx;
	(void)bytes;
	reply_reset();
	g_fake.requests++;
	g_fake.last_type = h->type;
	g_fake.last_seq = h->seq;
	g_fake.last_session = h->session;
	if (g_fake.drop_replies)
		return 0;
	if (g_fake.error_reply) {
		struct n2vcd_simple e;

		memset(&e, 0, sizeof(e));
		e.h.magic = N2VCD_MAGIC;
		e.h.type = N2VCD_ERROR;
		e.h.seq = h->seq;
		e.h.session = h->session;
		e.status = g_fake.error_status;
		reply_put(&e, sizeof(e));
		return 0;
	}
	switch (h->type) {
	case N2VCD_HELLO: {
		struct n2vcd_hello_ok ok;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = N2VCD_HELLO_OK;
		ok.h.seq = h->seq;
		ok.version = 1;                /* N2VCD_VERSION stays 1; decoder ids are additive */
		ok.dec_codecs = 3;
		ok.max_width = 4096;
		ok.max_height = 2304;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case N2VCD_OPEN: {
		const struct n2vcd_open *o = (const struct n2vcd_open *)msg;
		struct n2vcd_open_ok ok;
		g_fake.open_codec = o->codec;
		g_fake.open_encoder = o->encoder;
		g_fake.open_w = o->width;
		g_fake.open_h = o->height;
		g_fake.open_flags = o->flags;
		g_fake.session = 7;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = N2VCD_OPEN_OK;
		ok.h.seq = h->seq;
		ok.h.session = 7;
		ok.in_buf_bytes = 65536;
		ok.out_buf_bytes = 122880;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case N2VCD_DEC_HEADER: {
		const struct n2vcd_dec_header *d = (const struct n2vcd_dec_header *)msg;
		struct n2vcd_dec_header_ok ok;

		g_fake.header_bytes = d->in_bytes;
		if (d->in_handle < FAKE_MAX_BUFS && g_fake.bufs[d->in_handle].used) {
			uint32_t n = d->in_bytes < sizeof(g_fake.header_data) ? d->in_bytes : sizeof(g_fake.header_data);
			memcpy(g_fake.header_data, g_fake.bufs[d->in_handle].data, n);
		}
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = N2VCD_DEC_HEADER_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		ok.width = 320;
		ok.height = 240;
		ok.stride = 320;
		ok.out_buf_bytes = 122880;
		ok.dpb_frames = 3;
		ok.reserved[0] = 0x31324d4du;      /* MM21 */
		ok.reserved[1] = 256;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case W_DEC_ADD_BUFFER: {
		const struct w_add_buffer *a = (const struct w_add_buffer *)msg;
		struct w_add_buffer_ok ok;
		g_fake.last_add_handle = a->handle;
		g_fake.last_add_bytes = a->bytes;
		g_fake.last_add_c_handle = a->c_handle;
		g_fake.last_add_c_bytes = a->c_bytes;
		g_fake.added++;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = W_DEC_ADD_BUFFER_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		ok.index = g_fake.next_index++;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case W_DEC_INPUT: {
		const struct w_input *in = (const struct w_input *)msg;
		struct w_input_ok ok;
		g_fake.input_bytes = in->in_bytes;
		g_fake.input_pts = in->pts;
		g_fake.input_handle = in->in_handle;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = W_DEC_INPUT_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		ok.pictures_ready = 1;
		ok.hw_us = 1234;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case W_DEC_PICTURE: {
		struct w_picture_ok ok;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = W_DEC_PICTURE_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		ok.have = 1;
		ok.pts = g_fake.input_pts;
		ok.index = g_fake.picture_index;
		ok.width = 320;
		ok.height = 240;
		ok.stride = 320;
		ok.buf_h = 256;
		ok.fourcc = 0x31324d4du;
		ok.pictures_ready = 0;
		ok.poc = 3;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case W_DEC_RELEASE: {
		struct n2vcd_simple ok;
		g_fake.released++;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = W_DEC_RELEASE_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case W_DEC_FREE_BUFFERS: {
		struct n2vcd_simple ok;
		g_fake.freed++;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = W_DEC_FREE_BUFFERS_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case N2VCD_FLUSH: {
		struct n2vcd_simple ok;
		g_fake.flushed++;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = N2VCD_FLUSH_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		reply_put(&ok, sizeof(ok));
		break;
	}
	case N2VCD_CLOSE: {
		struct n2vcd_simple ok;
		memset(&ok, 0, sizeof(ok));
		ok.h.magic = N2VCD_MAGIC;
		ok.h.type = N2VCD_CLOSE_OK;
		ok.h.seq = h->seq;
		ok.h.session = g_fake.session;
		reply_put(&ok, sizeof(ok));
		break;
	}
	default:
		return -1;
	}
	if (g_fake.have_reply) {
		struct n2vcd_hdr *rh = (struct n2vcd_hdr *)g_fake.reply;
		if (g_fake.wrong_seq)
			rh->seq++;
		if (g_fake.wrong_type)
			rh->type = 0x7fffffffu;
	}
	return 0;
}

static int fake_recv(void *ctx, void *msg, uint32_t capacity, uint32_t timeout_ms)
{
	(void)ctx;
	(void)timeout_ms;
	if (!g_fake.have_reply)
		return -EAGAIN;
	if (g_fake.reply_bytes > capacity)
		return -1;
	memcpy(msg, g_fake.reply, g_fake.reply_bytes);
	g_fake.have_reply = 0;
	return (int)g_fake.reply_bytes;
}

static int fake_buf_alloc(void *ctx, uint32_t bytes, uint32_t *handle)
{
	uint32_t i;

	(void)ctx;
	for (i = 0; i < FAKE_MAX_BUFS; i++) {
		if (g_fake.bufs[i].used)
			continue;
		g_fake.bufs[i].used = 1;
		g_fake.bufs[i].bytes = bytes;
		g_fake.bufs[i].data = (uint8_t *)calloc(1, bytes ? bytes : 1);
		*handle = i;
		return 0;
	}
	return -1;
}

static int fake_buf_rw(void *ctx, uint32_t handle, uint32_t offset, void *data, uint32_t bytes, int write)
{
	(void)ctx;
	if (handle >= FAKE_MAX_BUFS || !g_fake.bufs[handle].used || offset + bytes > g_fake.bufs[handle].bytes)
		return -EINVAL;
	if (write)
		memcpy(g_fake.bufs[handle].data + offset, data, bytes);
	else
		memcpy(data, g_fake.bufs[handle].data + offset, bytes);
	return (int)bytes;
}

static int fake_buf_free(void *ctx, uint32_t handle)
{
	(void)ctx;
	if (handle >= FAKE_MAX_BUFS || !g_fake.bufs[handle].used)
		return -EINVAL;
	free(g_fake.bufs[handle].data);
	memset(&g_fake.bufs[handle], 0, sizeof(g_fake.bufs[handle]));
	return 0;
}

static void fake_close(void *ctx)
{
	(void)ctx;
	g_fake.connected = 0;
}

static void reset_fake(void)
{
	uint32_t i;

	for (i = 0; i < FAKE_MAX_BUFS; i++)
		if (g_fake.bufs[i].used)
			free(g_fake.bufs[i].data);
	memset(&g_fake, 0, sizeof(g_fake));
}

int main(void)
{
	const struct n2vtd_client *cl = n2vtd_client_proto();
	void *conn = NULL;
	uint32_t in_bytes = 0;
	struct n2vtd_header hdr;
	struct n2vtd_input in;
	struct n2vtd_picture pic;
	uint32_t ready = 0;
	uint8_t params[64];
	uint8_t au_buf[128];
	uint8_t io[32], back[32];
	int rc;

	printf("t_client: rev-2 decoder messages over the frozen transport\n");
	reset_fake();
	CHECK_EQ(cl->connect(cl->ctx, &conn, "/fake", NULL, 0), 0);
	CHECK(conn != NULL);
	CHECK_EQ(g_fake.requests, 1);
	CHECK_EQ(g_fake.last_type, N2VCD_HELLO);
	CHECK_EQ(g_fake.last_session, 0u);

	CHECK_EQ(cl->dec_open(cl->ctx, conn, N2VCD_CODEC_H264, 320, 240, 0x31324d4du, &in_bytes), 0);
	CHECK_EQ(in_bytes, 65536u);
	CHECK_EQ(g_fake.open_codec, N2VCD_CODEC_H264);
	CHECK_EQ(g_fake.open_encoder, 0u);
	CHECK_EQ(g_fake.open_w, 320u);
	CHECK_EQ(g_fake.open_h, 240u);
	/* OPEN.flags bits 8..15 = layout (MM21 = 1). */
	CHECK_EQ((g_fake.open_flags >> 8) & 0xffu, 1u);
	CHECK_EQ(g_fake.last_session, 0u);

	for (in_bytes = 0; in_bytes < sizeof(params); in_bytes++)
		params[in_bytes] = (uint8_t)(0x40 + in_bytes);
	CHECK_EQ(cl->dec_header(cl->ctx, conn, params, sizeof(params), &hdr), 0);
	CHECK_EQ(g_fake.last_session, 7u);
	CHECK_EQ(hdr.width, 320u);
	CHECK_EQ(hdr.height, 240u);
	CHECK_EQ(hdr.stride, 320u);
	CHECK_EQ(hdr.buf_h, 256u);
	CHECK_EQ(hdr.out_bytes, 122880u);
	CHECK_EQ(hdr.y_bytes, 320u * 256u);
	CHECK_EQ(hdr.dpb_frames, 3u);
	CHECK_EQ(hdr.format, 0x31324d4du);
	CHECK_EQ(g_fake.header_bytes, (uint32_t)sizeof(params));
	CHECK(memcmp(g_fake.header_data, params, sizeof(params)) == 0);

	CHECK_EQ(cl->dec_add_pool(cl->ctx, conn, &hdr), 0);
	CHECK_EQ(g_fake.added, 3u);
	CHECK_EQ(g_fake.last_add_bytes, 122880u);
	CHECK_EQ(g_fake.last_add_c_handle, 0u);           /* one buffer, C at stride*buf_h */
	CHECK_EQ(g_fake.last_add_c_bytes, 0u);

	/* Plane copy: the stand-in maps plane 1 behind the Y plane. */
	memset(io, 0x5a, sizeof(io));
	CHECK_EQ(cl->buffer_rw(cl->ctx, conn, 0, 0, 0, io, sizeof(io), 1), 0);
	CHECK_EQ(cl->buffer_rw(cl->ctx, conn, 0, 0, 0, back, sizeof(back), 0), 0);
	CHECK(memcmp(io, back, sizeof(io)) == 0);
	CHECK_EQ(cl->buffer_rw(cl->ctx, conn, 0, 1, 0, io, sizeof(io), 1), 0);
	CHECK_EQ(cl->buffer_rw(cl->ctx, conn, 0, 1, 0, back, sizeof(back), 0), 0);
	CHECK(memcmp(io, back, sizeof(io)) == 0);
	CHECK(cl->buffer_rw(cl->ctx, conn, 0, 1, hdr.out_bytes, io, sizeof(io), 1) != 0);
	CHECK(cl->buffer_rw(cl->ctx, conn, 0, 2, 0, io, sizeof(io), 1) != 0);

	memset(&in, 0xff, sizeof(in));
	memset(au_buf, 0xa5, sizeof(au_buf));
	CHECK_EQ(cl->dec_input(cl->ctx, conn, au_buf, 123, 0x1122334455667788ull, &in), 0);
	CHECK_EQ(g_fake.input_bytes, 123u);
	CHECK_EQ_U64(g_fake.input_pts, 0x1122334455667788ull);
	CHECK_EQ(in.pictures_ready, 1u);
	CHECK_EQ(in.hw_us, 1234u);
	CHECK_EQ(in.changed, 0u);

	g_fake.picture_index = 2;
	CHECK_EQ(cl->dec_picture(cl->ctx, conn, &pic), 0);
	CHECK_EQ(pic.have, 1u);
	CHECK_EQ(pic.index, 2u);
	CHECK_EQ(pic.width, 320u);
	CHECK_EQ(pic.height, 240u);
	CHECK_EQ(pic.stride, 320u);
	CHECK_EQ(pic.buf_h, 256u);
	CHECK_EQ(pic.format, 0x31324d4du);
	CHECK_EQ(pic.poc, 3);
	CHECK_EQ_U64(pic.pts, 0x1122334455667788ull);

	CHECK_EQ(cl->dec_release(cl->ctx, conn, 2), 0);
	CHECK_EQ(g_fake.released, 1u);
	CHECK_EQ(cl->dec_flush(cl->ctx, conn, &ready), 0);
	CHECK_EQ(ready, 0u);
	CHECK_EQ(g_fake.flushed, 1u);
	CHECK_EQ(cl->dec_free_buffers(cl->ctx, conn), 0);
	CHECK_EQ(g_fake.freed, 1u);
	CHECK_EQ(cl->dec_close(cl->ctx, conn), 0);
	cl->disconnect(cl->ctx, conn);
	conn = NULL;
	CHECK_EQ(g_fake.connected, 0);

	/* Error mapping. */
	reset_fake();
	CHECK_EQ(cl->connect(cl->ctx, &conn, "/fake", NULL, 0), 0);
	CHECK_EQ(cl->dec_open(cl->ctx, conn, N2VCD_CODEC_HEVC, 0, 0, 0x31324d4du, &in_bytes), 0);
	g_fake.error_reply = 1;
	g_fake.error_status = N2VCD_E_HW;
	CHECK_EQ(cl->dec_input(cl->ctx, conn, params, 10, 1, &in), N2VTD_CLIENT_E_HW);
	g_fake.error_status = N2VCD_E_ARGS;
	CHECK_EQ(cl->dec_input(cl->ctx, conn, params, 10, 1, &in), N2VTD_CLIENT_E_PROTO);
	g_fake.error_status = N2VCD_E_GUEST;
	CHECK_EQ(cl->dec_picture(cl->ctx, conn, &pic), N2VTD_CLIENT_E_HW);
	g_fake.error_status = N2VCD_E_SESSION;
	CHECK_EQ(cl->dec_flush(cl->ctx, conn, &ready), N2VTD_CLIENT_E_SESSION);
	g_fake.error_reply = 0;

	g_fake.wrong_seq = 1;
	CHECK_EQ(cl->dec_input(cl->ctx, conn, params, 10, 1, &in), N2VTD_CLIENT_E_PROTO);
	g_fake.wrong_seq = 0;
	g_fake.wrong_type = 1;
	CHECK_EQ(cl->dec_input(cl->ctx, conn, params, 10, 1, &in), N2VTD_CLIENT_E_PROTO);
	g_fake.wrong_type = 0;
	g_fake.drop_replies = 1;
	CHECK_EQ(cl->dec_input(cl->ctx, conn, params, 10, 1, &in), N2VTD_CLIENT_E_TIMEOUT);
	g_fake.drop_replies = 0;

	/* A picture index that was never registered is a protocol error. */
	g_fake.picture_index = 9;
	CHECK(cl->dec_picture(cl->ctx, conn, &pic) != 0);
	CHECK_EQ(cl->dec_close(cl->ctx, conn), 0);
	cl->disconnect(cl->ctx, conn);
	conn = NULL;

	{
		const struct n2vtd_client *cur = n2vtd_client_current();
		CHECK(cur != NULL);
		CHECK(cur->connect && cur->dec_open && cur->dec_header && cur->dec_add_pool && cur->dec_input &&
		      cur->dec_picture && cur->dec_release && cur->buffer_rw && cur->dec_free_buffers && cur->dec_flush &&
		      cur->dec_close && cur->disconnect);
	}
	reset_fake();
	rc = 0;
	(void)rc;
	return t_summary("CLIENT");
}
