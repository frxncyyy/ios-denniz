/* SPDX-License-Identifier: MIT */
/*
 * n2hwenc_client.h -- the one seam between the VideoToolbox encoder object (vt/n2hwenc_vt.c) and the vcodecd client
 * library (package P03, gpu/hwcodec/client/n2vcd_client.h).  The object never calls a client function directly: it calls
 * through this table, which keeps the object testable with a canned stream and lets a different P03 signature cost one
 * adapter file (vt/n2hwenc_client.c) instead of a rewrite.
 *
 * The default table (n2hwenc_client_current() before any install) maps on to P03's functions by weak symbol, so a link
 * without the client package still works: every call then reports "no client" and StartSession returns
 * kVTVideoEncoderNotAvailableNowErr, which makes VideoToolbox keep using the software encoder.
 */
#ifndef N2HWENC_CLIENT_H
#define N2HWENC_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct n2vcd_client;                    /* P03: one connection to vcodecd */
struct n2vcd_session;                   /* P03: one encoder session */

/* What n2vcd_open carries (include/n2vcd_proto.h, struct n2vcd_open) plus the codec selector. */
struct n2hwenc_params {
	uint32_t codec;                 /* N2VCD_CODEC_H264 ('avc1') or N2VCD_CODEC_HEVC ('hvc1') */
	uint32_t width, height;
	uint32_t fps_num, fps_den;
	uint32_t bitrate;               /* bits per second */
	uint32_t gop;                   /* key frame distance, 0 = the daemon's default */
	uint32_t profile, level;        /* H.264 profile_idc/level_idc; HEVC profile_idc and level_idc = 30 * level */
	uint32_t flags;                 /* bit 0: real time / low latency */
};

struct n2hwenc_client {
	void *ctx;
	/* Open the connection (HELLO).  socket_path/device_path NULL means the phone relay.  Returns 0 or a negative value. */
	int (*connect)(void *ctx, struct n2vcd_client **client, const char *socket_path, const char *device_path);
	/* OPEN: one encoder session.  in_bytes/out_bytes are the buffer sizes the client allocated. */
	int (*enc_open)(void *ctx, struct n2vcd_client *client, const struct n2hwenc_params *params,
	                struct n2vcd_session **session, uint32_t *in_bytes, uint32_t *out_bytes);
	/* ENC_FRAME: NV12 luma/chroma host pointers; out receives one Annex B access unit. */
	int (*enc_frame)(void *ctx, struct n2vcd_session *session, const uint8_t *y, const uint8_t *cbcr,
	                 uint32_t luma_stride, uint32_t chroma_stride, uint64_t pts, int force_key,
	                 uint8_t *out, uint32_t out_capacity, uint32_t *out_bytes, int *key);
	/* ENC_SET: bitrate / frame rate / a forced key frame at run time. */
	int (*enc_set)(void *ctx, struct n2vcd_session *session, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den,
	               int force_key);
	int (*close_session)(void *ctx, struct n2vcd_session *session);
	int (*disconnect)(void *ctx, struct n2vcd_client *client);
};

/* Install a table (copied by value).  NULL restores the default.  Returns 0, or -1 when a required entry is NULL. */
int n2hwenc_client_install(const struct n2hwenc_client *client);

/* The current table; never NULL.  The returned pointer is owned by this module and stays valid until the next install. */
const struct n2hwenc_client *n2hwenc_client_current(void);

/* The default table's error when the client library is absent (all P03 weak symbols are NULL). */
#define N2HWENC_CLIENT_ABSENT (-4096)

/* enc_open results the encoder object retries inside StartSession (opus-cam): another session is open or closing
 * (-EBUSY), and the daemon's restart-on-switch answer (BUSY with detail N2VCD_DETAIL_RESTART: vcodecd exits and launchd
 * starts a fresh one, so the connection must be reopened). */
#define N2HWENC_CLIENT_BUSY    (-16)
#define N2HWENC_CLIENT_RESTART (-4097)

#ifdef __cplusplus
}
#endif
#endif /* N2HWENC_CLIENT_H */
