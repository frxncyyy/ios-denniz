/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_client.h -- client library of vcodecd: the frozen datagrams of include/n2vcd_proto.h over a transport of
 * include/n2vcd_proto.h (kernel relay on the phone, SOCK_SEQPACKET on the host).
 *
 * One client owns one transport.  A client has one encoder session at a time in protocol v1; the server enforces
 * that, the library just reports N2VCD_E_BUSY.  Every call that touches the transport is serialised by the client's
 * mutex, so one session may be driven from several threads: each call either completes with its own reply or fails;
 * replies are never delivered to the wrong caller (the sequence number is checked on every datagram).
 *
 * Errors: every function that can fail returns an N2VCD_E_* value (0 = N2VCD_E_OK); transport and protocol failures
 * are mapped as follows and the positive errno of the last operating-system call is kept for diagnostics:
 *
 *   -ENOMEM                      N2VCD_E_NOMEM
 *   -EINVAL                      N2VCD_E_ARGS
 *   -EBUSY                       N2VCD_E_BUSY
 *   -EOPNOTSUPP                  N2VCD_E_UNSUPPORTED
 *   -ENXIO, -EPIPE, -ECONNRESET  N2VCD_E_SESSION  (server gone; the client is marked dead)
 *   -EAGAIN, -ETIMEDOUT          N2VCD_E_HW       (no reply within the timeout)
 *   short / malformed / wrong-seq datagram, bad reply fields   N2VCD_E_HW (the client is marked dead: the datagram
 *                                                              stream can no longer be matched to requests)
 *   N2VCD_ERROR replies          the reply's status value
 *
 * A guest stop (N2VCD_E_GUEST), a session error, or a watchdog ERROR with seq 0 is session-fatal: the session is
 * marked dead and later calls on it answer N2VCD_E_SESSION without touching the transport.  The caller should close
 * the session, fall back to software for this session, and retry with a fresh n2vcd_connect() at the next
 * StartSession; the old client must not be reused after a guest stop.
 *
 * Buffers: n2vcd_enc_open() allocates the input and output DMA buffers the server asked for (OPEN_OK).  On the phone
 * those are shared-pool buffers (RWVC_DMA_ALLOC with RWVC_DMA_SHARED); on the host they are files below the
 * transport's directory.  The handles travel in N2VCD_ENC_FRAME; n2vcd_enc_frame() copies the picture in and the
 * access unit out.
 */
#ifndef N2VCD_CLIENT_H
#define N2VCD_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "n2vcd_proto.h"

/* Every receive waits at most this long (n2vcd_connect() and n2vcd_set_timeout() can change it). */
#define N2VCD_CLIENT_TIMEOUT_MS 5000u

struct n2vcd_client;
struct n2vcd_session;

struct n2vcd_caps {
	uint32_t version;
	uint32_t enc_codecs;      /* bit 0 H.264, bit 1 HEVC */
	uint32_t dec_codecs;
	uint32_t max_width, max_height;
};

struct n2vcd_enc_params {
	uint32_t codec;           /* N2VCD_CODEC_H264 ('avc1') or N2VCD_CODEC_HEVC ('hvc1') */
	uint32_t encoder;         /* must be 1: this library has no decoder API in protocol v1 */
	uint32_t width, height;   /* picture size; the server bounds it (HELLO_OK.max_*) */
	uint32_t fps_num, fps_den, bitrate;
	uint32_t gop;             /* key-frame distance, 0 = server default (one second) */
	uint32_t profile, level;  /* H.264 profile_idc/level_idc, HEVC profile/level_idc; 0 = default */
	uint32_t flags;           /* bit 0: low latency / real time */
};

/*
 * Connect and say HELLO.  `t` is copied; on success the client owns t->ctx and the caller must NOT close the
 * transport separately (n2vcd_disconnect() does).  On failure the caller still owns `t` and should close it.
 * timeout_ms == 0 selects N2VCD_CLIENT_TIMEOUT_MS.  `err` (optional) gets a one-line reason on failure.
 */
int n2vcd_connect(struct n2vcd_client **out, const struct n2vcd_transport *t,
                  uint32_t timeout_ms, char *err, size_t errlen);

/* Server capabilities from HELLO_OK. */
int n2vcd_info(const struct n2vcd_client *c, struct n2vcd_caps *caps);

/* Change the receive timeout of later calls (0 restores the default).  Does not interrupt a call in progress. */
void n2vcd_set_timeout(struct n2vcd_client *c, uint32_t timeout_ms);

/* Positive errno of the last failed transport/system call, 0 if the last failure was not one. */
uint32_t n2vcd_last_errno(const struct n2vcd_client *c);

/* Detail field of the last N2VCD_ERROR (n2a32 stop code for E_GUEST, errno for others). */
uint32_t n2vcd_last_detail(const struct n2vcd_client *c);

/* Text for an N2VCD_E_* value (never NULL). */
const char *n2vcd_strstatus(int status);

/* Close the transport and free the client.  All sessions must have been closed first. */
void n2vcd_disconnect(struct n2vcd_client *c);

/*
 * Open one encoder session and allocate its input and output buffers.  On success *out is a new session; on failure
 * it is NULL and no buffer or server session is left behind (a CLOSE is sent if the buffers could not be allocated).
 */
int n2vcd_enc_open(struct n2vcd_client *c, const struct n2vcd_enc_params *p,
                   struct n2vcd_session **out, char *err, size_t errlen);

uint32_t n2vcd_session_id(const struct n2vcd_session *s);
uint32_t n2vcd_session_width(const struct n2vcd_session *s);
uint32_t n2vcd_session_height(const struct n2vcd_session *s);
uint32_t n2vcd_session_stride(const struct n2vcd_session *s);       /* align16(width): NV12 row pitch in the buffer */
uint32_t n2vcd_session_in_bytes(const struct n2vcd_session *s);
uint32_t n2vcd_session_out_bytes(const struct n2vcd_session *s);    /* every access unit fits in this */

/*
 * Encode one NV12 picture.  `y` is width x height bytes at pitch y_stride; `chroma` is the interleaved UV plane,
 * width/2 x height/2 pairs (width bytes per row) at pitch chroma_stride.  Both planes are copied (and re-packed to
 * the session stride) into the input buffer.  When a pitch equals the session stride the plane is copied in one
 * transport call, so the source must then be contiguous for (rows-1)*stride + width bytes.
 *
 * The access unit (Annex B, an SPS/PPS/VPS prefix when header_bytes > 0) is copied into `out` (capacity `cap`).
 * `cap` must be >= n2vcd_session_out_bytes(); otherwise N2VCD_E_ARGS is returned before anything is sent.
 * `out` may be NULL with cap 0 to encode without a copy.  *out_bytes (optional) gets the access-unit size,
 * *key (optional) is 1 for a key frame.  The server echoes pts; a mismatch is a protocol failure.
 *
 * n2vcd_enc_frame_ex() is the same with the daemon's timing and parameter-set fields; the CLI uses it so its
 * per-frame line can print the hardware time.
 */
int n2vcd_enc_frame(struct n2vcd_session *s,
                    const void *y, const void *chroma,
                    uint32_t y_stride, uint32_t chroma_stride,
                    uint64_t pts, int force_key,
                    void *out, uint32_t cap, uint32_t *out_bytes, int *key);

struct n2vcd_frame_info {
	uint32_t bytes;           /* access-unit size written to the output buffer */
	uint32_t key_frame;       /* 1 for a key frame */
	uint32_t hw_us;           /* the daemon's wall time of the encode call */
	uint32_t header_bytes;    /* > 0: the access unit starts with SPS/PPS(/VPS) of this many bytes */
};

int n2vcd_enc_frame_ex(struct n2vcd_session *s,
                       const void *y, const void *chroma,
                       uint32_t y_stride, uint32_t chroma_stride,
                       uint64_t pts, int force_key,
                       void *out, uint32_t cap, struct n2vcd_frame_info *info);

/* Run-time bitrate / frame rate / force-key request; 0 leaves the value unchanged. */
int n2vcd_enc_set(struct n2vcd_session *s, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den,
                  int force_key);

/* FLUSH: v1's encode is synchronous, so this is always a success (kept for protocol completeness). */
int n2vcd_flush(struct n2vcd_session *s);

/* Send CLOSE, free the session's buffers and the session.  Always frees; the return value reports the CLOSE.  A
 * session that already died (E_GUEST/E_SESSION was reported by an earlier call) returns N2VCD_E_OK here: the death
 * was already reported, and this call only releases local resources. */
int n2vcd_close(struct n2vcd_session *s);

/*
 * Small helpers for callers that prefer a path-based connect and negative errno results (P04's VideoToolbox
 * adapter).  n2vcd_connect_path(): exactly one of socket_path/dev_path may be non-NULL; both NULL selects the phone
 * relay's default node.  On success the client owns the transport, as with n2vcd_connect(); on failure the
 * transport is closed here.  n2vcd_status_errno(): N2VCD_E_OK -> 0, every other status -> the closest negative
 * errno (-EPROTO version, -EOPNOTSUPP unsupported, -EBUSY, -EINVAL args, -EIO hardware/guest, -ENOMEM, -EPIPE
 * session).
 */
int n2vcd_connect_path(struct n2vcd_client **out, const char *socket_path, const char *dev_path,
                       uint32_t timeout_ms, char *err, size_t errlen);
int n2vcd_status_errno(int status);

#endif
