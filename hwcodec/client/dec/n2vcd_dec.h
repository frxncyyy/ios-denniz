/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dec.h -- decoder client library of vcodecd.  Wire format: include/n2vcd_proto.h rev 2
 * (text: daemon/dec/n2vcd_proto_dec_rev2.h.txt); the server side is daemon/dec/n2vcd_dec_service.c.
 * Used by DEC06's VideoToolbox decoder object and by n2vcd_cli's decoder tool.
 *
 * One client owns one transport (P03's relay or socket transport, frozen n2vcd_proto.h).  Every
 * call that touches the transport is serialised by one mutex, so a session may be driven from
 * several threads; replies are matched by sequence number and a wrong/extra datagram marks the
 * client dead instead of being delivered to the wrong caller.
 *
 * Typical use (the client never sees a pixel copy done by the daemon):
 *   connect -> info -> open(codec, MM21) -> header(AU with SPS/PPS)
 *   -> add_buffers(geo) [allocates dpb_frames x (Y,C) shared DMA buffers]
 *   loop: input(AU, pts) -> while pictures_ready: picture(); copy/detile
 *                             from buffer(handle) into a CVPixelBuffer; release(index)
 *   flush -> drain pictures -> close -> disconnect
 *
 * Errors: every function returns an N2VCD_E_* value, N2VCD_E_OK = 0.  The client's `last_errno`
 * keeps the positive errno of the failed system/transport call and `last_detail` the daemon's
 * detail word.  A guest stop (N2VCD_E_GUEST), a session error (N2VCD_E_SESSION), or a seq-0
 * watchdog/error datagram is session-fatal: the session is marked dead and later calls answer
 * N2VCD_E_SESSION without touching the transport.  A malformed or out-of-sequence reply marks the
 * whole client dead.  The caller should then close the session and fall back to software, and
 * reconnect with a fresh client at the next decoder session.
 *
 * Timeouts: every receive is bounded (default 5000 ms, n2vcd_dec_set_timeout()).  The decoder's
 * first frame can take longer than a frame interval while the vendor library initialises the
 * blocks; DEC06 should use at least 1000 ms per request and 2000 ms for open/header.
 */
#ifndef N2VCD_DEC_H
#define N2VCD_DEC_H

#include <stddef.h>
#include <stdint.h>

#include "n2vcd_proto.h"

#define N2VCD_DEC_TIMEOUT_MS 5000u
#define N2VCD_DEC_MAX_POOL 16u         /* daemon/dec/n2vcd_dec_service.h N2VCD_DEC_MAX_POOL */

struct n2vcd_dec_client;
struct n2vcd_dec_session;

struct n2vcd_dec_caps {
	uint32_t version;
	uint32_t dec_codecs;      /* bit 0 H.264, bit 1 HEVC */
	uint32_t max_width, max_height;
};

struct n2vcd_dec_geometry {
	uint32_t width, height;   /* display size (crop): put these in the CVPixelBuffer */
	uint32_t stride;          /* Y pitch in bytes, the coded width (buf_w) */
	uint32_t buf_h;           /* aligned coded height (buf_h); the C plane starts after Y */
	uint32_t y_bytes;         /* allocate this for the Y plane of one pool entry */
	uint32_t c_bytes;         /* allocate this for the C plane (0 for single-plane) */
	uint32_t out_buf_bytes;   /* y_bytes + c_bytes (the wire value) */
	uint32_t dpb_frames;      /* pool entries to register (1..N2VCD_DEC_MAX_POOL) */
	uint32_t fourcc;          /* N2VCD_FMT_MM21 today */
};

struct n2vcd_dec_input_info {
	uint32_t pictures_ready;  /* drain with n2vcd_dec_picture() */
	uint32_t changed;         /* 1: geometry change; the input was not consumed */
	uint32_t error_map;       /* vendor error_map of this access unit, 0 = clean */
	uint32_t hw_us;           /* daemon wall time of the input */
};

struct n2vcd_dec_pic {
	uint32_t index;           /* registered pool entry; release it after copying */
	uint32_t y_handle, c_handle;
	uint64_t pts;             /* display-order timestamp */
	int32_t poc;              /* vendor picture order count */
	uint32_t width, height, stride, buf_h, fourcc;
	uint32_t pictures_ready;  /* still queued after this one */
};

/* Connect and say HELLO.  `t` is copied; on success the client owns t->ctx and the caller must NOT
 * close the transport separately.  On failure the caller still owns `t`.  timeout_ms == 0 selects
 * N2VCD_DEC_TIMEOUT_MS. */
int n2vcd_dec_connect(struct n2vcd_dec_client **out, const struct n2vcd_transport *t,
                      uint32_t timeout_ms, char *err, size_t errlen);

/* Same, creating P03's transport: exactly one of socket_path (host) and dev_path (phone relay) may
 * be non-NULL; both NULL selects the phone relay's default node.  Lives in n2vcd_dec_path.c so a
 * test binary that does not link P03's transports still links this client.  On failure the
 * transport is closed here. */
int n2vcd_dec_connect_path(struct n2vcd_dec_client **out, const char *socket_path, const char *dev_path,
                           uint32_t timeout_ms, char *err, size_t errlen);

int n2vcd_dec_info(const struct n2vcd_dec_client *c, struct n2vcd_dec_caps *caps);
void n2vcd_dec_set_timeout(struct n2vcd_dec_client *c, uint32_t timeout_ms);
uint32_t n2vcd_dec_last_errno(const struct n2vcd_dec_client *c);
uint32_t n2vcd_dec_last_detail(const struct n2vcd_dec_client *c);
const char *n2vcd_dec_strstatus(int status);            /* never NULL */
int n2vcd_dec_status_errno(int status);                 /* N2VCD_E_OK -> 0, else -errno */
void n2vcd_dec_disconnect(struct n2vcd_dec_client *c);

/* Open one decoder session; `out_format` is 0/auto or N2VCD_OPEN_FMT_MM21.  `max_width`/`max_height`
 * are an optional upper bound for the stream (0 = from the stream, bounded by the daemon).  On
 * success *in_buf_bytes is the bitstream buffer size the client allocated internally. */
int n2vcd_dec_open(struct n2vcd_dec_client *c, uint32_t codec, uint32_t max_width, uint32_t max_height,
                   uint32_t out_format, struct n2vcd_dec_session **out,
                   uint32_t *in_buf_bytes, char *err, size_t errlen);

uint32_t n2vcd_dec_session_id(const struct n2vcd_dec_session *s);
uint32_t n2vcd_dec_in_bytes(const struct n2vcd_dec_session *s);
uint32_t n2vcd_dec_out_bytes(const struct n2vcd_dec_session *s);   /* one pool entry, after header */

/* Parse the parameter sets of `au` (an Annex B access unit; the first frame of the stream works) and
 * fill *geo.  The buffer is copied into the session's input buffer.  The same AU must be sent with
 * n2vcd_dec_input() afterwards: the header START does not consume the picture. */
int n2vcd_dec_header(struct n2vcd_dec_session *s, const void *au, uint32_t bytes,
                     struct n2vcd_dec_geometry *geo);

/* Allocate and register geo->dpb_frames output entries (Y and C buffers per entry).  Must follow a
 * successful n2vcd_dec_header(); after n2vcd_dec_free_buffers() a new header and pool are needed. */
int n2vcd_dec_add_buffers(struct n2vcd_dec_session *s, const struct n2vcd_dec_geometry *geo);

/* Handles of a registered entry (for a caller that maps the buffers itself). */
int n2vcd_dec_buffer_handle(const struct n2vcd_dec_session *s, uint32_t index,
                            uint32_t *y_handle, uint32_t *c_handle);
/* Copy to/from one plane: 0 = Y, 1 = C.  `offset` is relative to the plane; `bytes` is bounded by
 * the plane size.  write != 0 copies host -> buffer.  This is how DEC06 copies/detiles a picture
 * into its CVPixelBuffer (the daemon never copies pixels). */
int n2vcd_dec_buffer_rw(struct n2vcd_dec_session *s, uint32_t index, int plane, uint32_t offset,
                        void *data, uint32_t bytes, int write);

/* Feed one Annex B access unit.  `bytes <= n2vcd_dec_in_bytes()`.  The input buffer is free again
 * when the call returns (the vendor copies the AU into its own ring). */
int n2vcd_dec_input(struct n2vcd_dec_session *s, const void *au, uint32_t bytes, uint64_t pts,
                    uint32_t flags, struct n2vcd_dec_input_info *info);

/* Pop one displayed picture in display order: 1 = *frame filled, 0 = none queued, <0 = N2VCD_E_*. */
int n2vcd_dec_picture(struct n2vcd_dec_session *s, struct n2vcd_dec_pic *frame);

/* Return a picture's pool entry to the daemon.  Call after copying/detiling it; a double release is
 * refused with N2VCD_E_ARGS. */
int n2vcd_dec_release(struct n2vcd_dec_session *s, uint32_t index);

/* Drop the whole pool (after draining and releasing every picture), e.g. before a resolution change. */
int n2vcd_dec_free_buffers(struct n2vcd_dec_session *s);

/* Flush: drain the delayed pictures.  *pictures_ready (optional) is the number to pop. */
int n2vcd_dec_flush(struct n2vcd_dec_session *s, uint32_t *pictures_ready);

/* Send CLOSE, free every registered/allocated buffer and the session.  Always frees; the return
 * value reports the CLOSE (a session that already died returns N2VCD_E_OK: the death was reported). */
int n2vcd_dec_close(struct n2vcd_dec_session *s);

#endif /* N2VCD_DEC_H */
