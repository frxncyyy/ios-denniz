/* SPDX-License-Identifier: MIT */
/*
 * n2hwenc_client.c -- the default client seam over package P03's vcodecd client (gpu/hwcodec/client/n2vcd_client.h)
 * and the install/copy mechanism.  This is the only file in package P04 that names a P03 function.
 *
 * P03 returns N2VCD_E_* values; the seam's contract is "0 or a negative value", so every call goes through
 * n2vcd_status_errno() (P03's own mapping) and the seam never interprets a positive status.  All P03 symbols are weak:
 * a link without the client package still succeeds, every call then reports N2HWENC_CLIENT_ABSENT, and StartSession turns that
 * into kVTVideoEncoderNotAvailableNowErr (VideoToolbox keeps the software encoder).
 *
 * The signatures below are the final ones hw-p03 posted and committed in client/n2vcd_client.h.  If that header changes,
 * this file is the only one in P04 that has to change.
 */
#include "n2hwenc_client.h"

#include <pthread.h>
#include <stddef.h>
#include <string.h>

/* P03's header when the client package is in the tree (it is, at the time of writing); otherwise the same declarations
 * locally, so this object also compiles in a tree without the client package.  Both paths use the same weak symbols. */
#if defined(__has_include)
#if __has_include("n2vcd_client.h")
#include "n2vcd_client.h"
#define N2HWENC_HAVE_P03_HEADER 1
#endif
#endif
#ifndef N2HWENC_HAVE_P03_HEADER
struct n2vcd_client;
struct n2vcd_session;
struct n2vcd_enc_params {
	uint32_t codec, encoder, width, height, fps_num, fps_den, bitrate, gop, profile, level, flags;
};
#endif

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2_WEAK __attribute__((weak_import))
#else
#define N2_WEAK __attribute__((weak))
#endif

extern int n2vcd_connect_path(struct n2vcd_client **out, const char *socket_path, const char *device_path,
                              uint32_t timeout_ms, char *err, size_t errlen) N2_WEAK;
extern int n2vcd_status_errno(int status) N2_WEAK;
extern int n2vcd_enc_open(struct n2vcd_client *c, const struct n2vcd_enc_params *p,
                          struct n2vcd_session **out, char *err, size_t errlen) N2_WEAK;
extern uint32_t n2vcd_session_in_bytes(const struct n2vcd_session *s) N2_WEAK;
extern uint32_t n2vcd_session_out_bytes(const struct n2vcd_session *s) N2_WEAK;
extern int n2vcd_enc_frame(struct n2vcd_session *s, const void *y, const void *chroma,
                           uint32_t y_stride, uint32_t chroma_stride, uint64_t pts, int force_key,
                           void *out, uint32_t cap, uint32_t *out_bytes, int *key) N2_WEAK;
extern int n2vcd_enc_set(struct n2vcd_session *s, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den,
                         int force_key) N2_WEAK;
extern int n2vcd_close(struct n2vcd_session *s) N2_WEAK;
extern void n2vcd_disconnect(struct n2vcd_client *c) N2_WEAK;
extern void n2vcd_set_timeout(struct n2vcd_client *c, uint32_t timeout_ms) N2_WEAK;
extern uint32_t n2vcd_last_detail(const struct n2vcd_client *c) N2_WEAK;

/* Timeouts (opus-dec3 review; tightened by opus-cam for Camera): every receive happens inside VideoToolbox's
 * StartSession/EncodeFrame (or the encoder's worker), and StartSession must fail within ~2 s so VideoToolbox moves on to
 * the software encoder.  HELLO is answered by vcodecd's idle loop in < 100 ms, so connect gets 400 ms; OPEN (ENC_INIT +
 * SET_PARAM + the sequence-header job) took 46-56 ms on the phone at every size (vc16/vc17/vc21/vc22), so it gets 1 s;
 * every later request gets 1 s (a 4K picture is ~69 ms at 273 MHz), after which the session is declared fatal. */
#define N2HWENC_CONNECT_TIMEOUT_MS 400u
#define N2HWENC_OPEN_TIMEOUT_MS    1000u
#define N2HWENC_FRAME_TIMEOUT_MS   1000u
#ifndef N2HWENC_HAVE_P03_HEADER          /* include/n2vcd_proto.h values, for a tree without the client package */
#define N2VCD_E_BUSY 3
#define N2VCD_DETAIL_RESTART 0x52535452u
#endif

/* opus-cam: one relay request at a time per process for every encoder object in this dylib.  The kernel relay
 * identifies a client by PROCESS (include/redwood_vcodec.h: all opens of minor 1 in a process share one client id and one
 * reply queue), so two objects waiting for replies at the same time (e.g. a closing session and a new StartSession in
 * mediaserverd) could each receive the other's datagram; P03's client then sees a wrong sequence number and marks the
 * connection dead.  Holding this lock across each request/reply pair prevents that.  Every holder is bounded by the
 * client timeouts (<= 1 s per call).  The decoder plug-in is a separate image and is NOT covered (see CAMERA_INTEGRATION.md). */
static pthread_mutex_t s_relay = PTHREAD_MUTEX_INITIALIZER;
#define RELAY_LOCKED(expr) __extension__({ int r_; pthread_mutex_lock(&s_relay); r_ = (expr); pthread_mutex_unlock(&s_relay); r_; })

static int status_to_errno(int status)
{
	if (status == 0)
		return 0;
	if (n2vcd_status_errno == NULL)
		return -1;
	return n2vcd_status_errno(status);
}

static int def_connect(void *ctx, struct n2vcd_client **client, const char *socket_path, const char *device_path)
{
	char err[128];
	int status;

	(void)ctx;
	if (n2vcd_connect_path == NULL || n2vcd_status_errno == NULL)
		return N2HWENC_CLIENT_ABSENT;
	err[0] = '\0';
	status = RELAY_LOCKED(n2vcd_connect_path(client, socket_path, device_path, N2HWENC_CONNECT_TIMEOUT_MS, err,
	                                         sizeof(err)));
	if (status != 0)
		return status_to_errno(status);
	return 0;
}

static int def_enc_open(void *ctx, struct n2vcd_client *client, const struct n2hwenc_params *p,
                        struct n2vcd_session **session, uint32_t *in_bytes, uint32_t *out_bytes)
{
	struct n2vcd_enc_params params;
	char err[128];
	int status;

	(void)ctx;
	if (n2vcd_enc_open == NULL || n2vcd_session_in_bytes == NULL || n2vcd_session_out_bytes == NULL)
		return N2HWENC_CLIENT_ABSENT;
	memset(&params, 0, sizeof(params));
	params.codec = p->codec;
	params.encoder = 1;
	params.width = p->width;
	params.height = p->height;
	params.fps_num = p->fps_num;
	params.fps_den = p->fps_den;
	params.bitrate = p->bitrate;
	params.gop = p->gop;
	params.profile = p->profile;
	params.level = p->level;
	params.flags = p->flags;
	err[0] = '\0';
	if (n2vcd_set_timeout != NULL)
		n2vcd_set_timeout(client, N2HWENC_OPEN_TIMEOUT_MS);
	pthread_mutex_lock(&s_relay);
	status = n2vcd_enc_open(client, &params, session, err, sizeof(err));
	if (status == N2VCD_E_BUSY && n2vcd_last_detail != NULL && n2vcd_last_detail(client) == N2VCD_DETAIL_RESTART) {
		pthread_mutex_unlock(&s_relay);
		return N2HWENC_CLIENT_RESTART;
	}
	pthread_mutex_unlock(&s_relay);
	if (status != 0)
		return status_to_errno(status);
	if (*session == NULL)
		return -1;
	if (n2vcd_set_timeout != NULL)
		n2vcd_set_timeout(client, N2HWENC_FRAME_TIMEOUT_MS);
	if (in_bytes != NULL)
		*in_bytes = n2vcd_session_in_bytes(*session);
	if (out_bytes != NULL)
		*out_bytes = n2vcd_session_out_bytes(*session);
	return 0;
}

static int def_enc_frame(void *ctx, struct n2vcd_session *session, const uint8_t *y, const uint8_t *cbcr,
                         uint32_t luma_stride, uint32_t chroma_stride, uint64_t pts, int force_key,
                         uint8_t *out, uint32_t out_capacity, uint32_t *out_bytes, int *key)
{
	int status;

	(void)ctx;
	if (n2vcd_enc_frame == NULL || n2vcd_status_errno == NULL)
		return N2HWENC_CLIENT_ABSENT;
	status = RELAY_LOCKED(n2vcd_enc_frame(session, y, cbcr, luma_stride, chroma_stride, pts, force_key, out,
	                                      out_capacity, out_bytes, key));
	return status_to_errno(status);
}

static int def_enc_set(void *ctx, struct n2vcd_session *session, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den,
                       int force_key)
{
	int status;

	(void)ctx;
	if (n2vcd_enc_set == NULL || n2vcd_status_errno == NULL)
		return N2HWENC_CLIENT_ABSENT;
	status = RELAY_LOCKED(n2vcd_enc_set(session, bitrate, fps_num, fps_den, force_key));
	return status_to_errno(status);
}

static int def_close_session(void *ctx, struct n2vcd_session *session)
{
	int status;

	(void)ctx;
	if (n2vcd_close == NULL || n2vcd_status_errno == NULL)
		return N2HWENC_CLIENT_ABSENT;
	status = RELAY_LOCKED(n2vcd_close(session));
	return status_to_errno(status);
}

static int def_disconnect(void *ctx, struct n2vcd_client *client)
{
	(void)ctx;
	if (n2vcd_disconnect == NULL)
		return N2HWENC_CLIENT_ABSENT;
	pthread_mutex_lock(&s_relay);
	n2vcd_disconnect(client);
	pthread_mutex_unlock(&s_relay);
	return 0;
}

static const struct n2hwenc_client s_default = {
	.ctx = NULL,
	.connect = def_connect,
	.enc_open = def_enc_open,
	.enc_frame = def_enc_frame,
	.enc_set = def_enc_set,
	.close_session = def_close_session,
	.disconnect = def_disconnect,
};

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static struct n2hwenc_client s_current;
static int s_have_installed;

static int complete(const struct n2hwenc_client *c)
{
	return c != NULL && c->connect != NULL && c->enc_open != NULL && c->enc_frame != NULL &&
	       c->enc_set != NULL && c->close_session != NULL && c->disconnect != NULL;
}

int n2hwenc_client_install(const struct n2hwenc_client *client)
{
	if (client != NULL && !complete(client))
		return -1;
	pthread_mutex_lock(&s_lock);
	if (client != NULL) {
		s_current = *client;
		s_have_installed = 1;
	} else {
		memset(&s_current, 0, sizeof(s_current));
		s_have_installed = 0;
	}
	pthread_mutex_unlock(&s_lock);
	return 0;
}

const struct n2hwenc_client *n2hwenc_client_current(void)
{
	const struct n2hwenc_client *result;

	pthread_mutex_lock(&s_lock);
	result = s_have_installed ? &s_current : &s_default;
	pthread_mutex_unlock(&s_lock);
	return result;
}
