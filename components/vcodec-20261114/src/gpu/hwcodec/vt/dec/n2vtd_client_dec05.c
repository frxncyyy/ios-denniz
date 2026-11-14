/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_client_dec05.c -- the product adapter of the decoder client seam over DEC05's client library
 * (gpu/hwcodec/client/dec/n2vcd_dec.h).  It is compiled only when include/n2vcd_proto.h has the rev-2 decoder messages
 * (N2VCD_PROTO_REV2); until hw-lead lands them, the file is an empty translation unit and the seam's protocol stand-in
 * remains the default.  run_tests.sh compiles it against DEC05's staged rev-2 header to prove the adapter stays in
 * step with their API.
 */
#include <stddef.h>
#include <stdlib.h>

#include "n2vtd_client.h"

#if __has_include("n2vcd_dec.h")
#include "n2vcd_dec.h"          /* pulls in include/n2vcd_proto.h; rev 2 defines N2VCD_PROTO_REV2 */
#endif

#if defined(N2VCD_PROTO_REV2) && __has_include("n2vcd_dec.h")

#include <pthread.h>
#include <string.h>

/* opus-robust (12 Nov 2026): the relay's client identity is the PROCESS (kernel relay slot = pid + uniqueid), not the
 * descriptor, so two sessions of this plug-in in one mediaserverd (two players, a thumbnailer next to a player) could
 * read each other's replies: DEC05's client then marks its connection broken (seq mismatch) and that session dies.
 * Every request/reply pair of every connection of this image is therefore serialised by one process-wide lock, as
 * P04's encoder objects do (vt/n2hwenc_client.c s_relay).  buffer_rw is a DMA ioctl with no reply queue and stays
 * outside the lock (the 4K detile reads must not wait for another session's request).  The encoder lives in another
 * image (Slim.videoencoder) and has its own lock: an encode and a decode in the same process can still collide
 * (CAMERA_INTEGRATION.md risk 2). */
static pthread_mutex_t s_relay = PTHREAD_MUTEX_INITIALIZER;
#define D5_LOCKED(type, expr) __extension__({ type r_; pthread_mutex_lock(&s_relay); r_ = (expr); \
	pthread_mutex_unlock(&s_relay); r_; })

struct d5_conn {
	struct n2vcd_dec_client *client;
	struct n2vcd_dec_session *session;
	struct n2vcd_dec_geometry geo;
	uint32_t in_bytes;
};

static int d5_err(int status)
{
	switch (status) {
	case N2VCD_E_OK:
		return 0;
	case N2VCD_E_UNSUPPORTED:
	case N2VCD_E_ARGS:
	case N2VCD_E_VERSION:
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

static int d5_connect(void *ctx, void **conn_out, const char *socket_path, const char *device_path,
                      uint32_t timeout_ms)
{
	struct d5_conn *c;
	char err[128];
	int rc;

	(void)ctx;
	if (!conn_out)
		return N2VTD_CLIENT_E_ARGS;
	*conn_out = NULL;
	c = (struct d5_conn *)calloc(1, sizeof(*c));
	if (!c)
		return N2VTD_CLIENT_E_NOMEM;
	rc = D5_LOCKED(int, n2vcd_dec_connect_path(&c->client, socket_path, device_path,
	                            timeout_ms ? timeout_ms : N2VCD_DEC_TIMEOUT_MS, err, sizeof(err)));
	if (rc != N2VCD_E_OK) {
		free(c);
		return N2VTD_CLIENT_ABSENT;
	}
	*conn_out = c;
	return 0;
}

static void d5_disconnect(void *ctx, void *conn)
{
	struct d5_conn *c = (struct d5_conn *)conn;

	(void)ctx;
	if (!c)
		return;
	pthread_mutex_lock(&s_relay);
	n2vcd_dec_disconnect(c->client);
	pthread_mutex_unlock(&s_relay);
	free(c);
}

static int d5_open(void *ctx, void *conn, uint32_t codec, uint32_t max_width, uint32_t max_height, uint32_t format,
                   uint32_t *in_bytes)
{
	struct d5_conn *c = (struct d5_conn *)conn;
	char err[128];
	uint32_t detail;
	int rc;

	(void)ctx;
	if (!c || !in_bytes)
		return N2VTD_CLIENT_E_ARGS;
	pthread_mutex_lock(&s_relay);
	rc = n2vcd_dec_open(c->client, codec, max_width, max_height,
	                    format == 0x3231564eu /* NV12 */ ? 2u : 1u /* MM21 */, &c->session, in_bytes, err,
	                    sizeof(err));
	detail = n2vcd_dec_last_detail(c->client);
	pthread_mutex_unlock(&s_relay);
	/* BUSY (another session) and BUSY/RESTART (vcodecd --restart-on-switch exits for a fresh process) are the two
	 * answers worth a retry: say which, so StartSession can retry them and fall back to software afterwards. */
	if (rc == N2VCD_E_BUSY)
		return detail == N2VCD_DETAIL_RESTART ? N2VTD_CLIENT_E_RESTART : N2VTD_CLIENT_E_BUSY;
	if (rc != N2VCD_E_OK)
		return d5_err(rc);
	c->in_bytes = *in_bytes;
	return 0;
}

static int d5_header(void *ctx, void *conn, const uint8_t *annexb, uint32_t bytes, struct n2vtd_header *hdr)
{
	struct d5_conn *c = (struct d5_conn *)conn;
	int rc;

	(void)ctx;
	if (!c || !hdr || (!annexb && bytes))
		return N2VTD_CLIENT_E_ARGS;
	rc = D5_LOCKED(int, n2vcd_dec_header(c->session, annexb, bytes, &c->geo));
	if (rc != N2VCD_E_OK)
		return d5_err(rc);
	memset(hdr, 0, sizeof(*hdr));
	hdr->width = c->geo.width;
	hdr->height = c->geo.height;
	hdr->stride = c->geo.stride;
	hdr->buf_h = c->geo.buf_h;
	hdr->out_bytes = c->geo.out_buf_bytes;
	hdr->y_bytes = c->geo.y_bytes;
	hdr->dpb_frames = c->geo.dpb_frames;
	hdr->format = c->geo.fourcc;
	hdr->crop_x = 0;
	hdr->crop_y = 0;
	return 0;
}

static int d5_add_pool(void *ctx, void *conn, const struct n2vtd_header *hdr)
{
	struct d5_conn *c = (struct d5_conn *)conn;
	int rc;

	(void)ctx;
	if (!c || !hdr)
		return N2VTD_CLIENT_E_ARGS;
	rc = D5_LOCKED(int, n2vcd_dec_add_buffers(c->session, &c->geo));
	return d5_err(rc);
}

static int d5_input(void *ctx, void *conn, const uint8_t *au, uint32_t bytes, uint64_t pts, struct n2vtd_input *result)
{
	struct d5_conn *c = (struct d5_conn *)conn;
	struct n2vcd_dec_input_info info;
	int rc;

	(void)ctx;
	if (!c || !result || !au)
		return N2VTD_CLIENT_E_ARGS;
	memset(&info, 0, sizeof(info));
	rc = D5_LOCKED(int, n2vcd_dec_input(c->session, au, bytes, pts, 0, &info));
	if (rc != N2VCD_E_OK)
		return d5_err(rc);
	result->pictures_ready = info.pictures_ready;
	result->changed = info.changed;
	result->error_map = info.error_map;
	result->hw_us = info.hw_us;
	return 0;
}

static int d5_picture(void *ctx, void *conn, struct n2vtd_picture *picture)
{
	struct d5_conn *c = (struct d5_conn *)conn;
	struct n2vcd_dec_pic pic;
	int rc;

	(void)ctx;
	if (!c || !picture)
		return N2VTD_CLIENT_E_ARGS;
	memset(picture, 0, sizeof(*picture));
	rc = D5_LOCKED(int, n2vcd_dec_picture(c->session, &pic));
	if (rc == 0)
		return 0;
	if (rc < 0)
		return d5_err(rc);
	picture->have = 1;
	picture->index = pic.index;
	picture->pts = pic.pts;
	picture->poc = pic.poc;
	picture->width = pic.width;
	picture->height = pic.height;
	picture->stride = pic.stride;
	picture->buf_h = pic.buf_h;
	picture->format = pic.fourcc;
	picture->pictures_ready = pic.pictures_ready;
	return 0;
}

static int d5_release(void *ctx, void *conn, uint32_t index)
{
	struct d5_conn *c = (struct d5_conn *)conn;

	(void)ctx;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	return d5_err(D5_LOCKED(int, n2vcd_dec_release(c->session, index)));
}

static int d5_buffer_rw(void *ctx, void *conn, uint32_t index, int plane, uint32_t offset, void *data, uint32_t bytes,
                        int write)
{
	struct d5_conn *c = (struct d5_conn *)conn;

	(void)ctx;
	if (!c || !data || (plane != 0 && plane != 1))
		return N2VTD_CLIENT_E_ARGS;
	return d5_err(n2vcd_dec_buffer_rw(c->session, index, plane, offset, data, bytes, write));
}

static int d5_free_buffers(void *ctx, void *conn)
{
	struct d5_conn *c = (struct d5_conn *)conn;

	(void)ctx;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	return d5_err(D5_LOCKED(int, n2vcd_dec_free_buffers(c->session)));
}

static int d5_flush(void *ctx, void *conn, uint32_t *pictures_ready)
{
	struct d5_conn *c = (struct d5_conn *)conn;

	(void)ctx;
	if (!c || !pictures_ready)
		return N2VTD_CLIENT_E_ARGS;
	return d5_err(D5_LOCKED(int, n2vcd_dec_flush(c->session, pictures_ready)));
}

static int d5_close(void *ctx, void *conn)
{
	struct d5_conn *c = (struct d5_conn *)conn;
	int rc;

	(void)ctx;
	if (!c)
		return N2VTD_CLIENT_E_ARGS;
	rc = D5_LOCKED(int, n2vcd_dec_close(c->session));
	c->session = NULL;
	return d5_err(rc);
}

static const struct n2vtd_client g_dec05_table = {
	.ctx = NULL,
	.connect = d5_connect,
	.dec_open = d5_open,
	.dec_header = d5_header,
	.dec_add_pool = d5_add_pool,
	.dec_input = d5_input,
	.dec_picture = d5_picture,
	.dec_release = d5_release,
	.buffer_rw = d5_buffer_rw,
	.dec_free_buffers = d5_free_buffers,
	.dec_flush = d5_flush,
	.dec_close = d5_close,
	.disconnect = d5_disconnect,
	.set_timeout = NULL,        /* the timeout is passed to n2vcd_dec_connect_path */
};

const struct n2vtd_client *n2vtd_client_dec05(void)
{
	return &g_dec05_table;
}

#endif /* N2VCD_PROTO_REV2 && n2vcd_dec.h */
