/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_client.h -- the one seam between the VideoToolbox decoder object (n2vtd_obj.c) and the decode client of
 * vcodecd (package DEC05, client/dec/n2vcd_dec.h).  The object never calls a client function directly: it calls
 * through this table, which keeps the object testable with a canned picture source.
 *
 * Two implementations:
 *   n2vtd_client_dec05.c   the product adapter over DEC05's n2vcd_dec_* API (compiled when client/dec/n2vcd_dec.h is
 *                          present; it is the default table in that build);
 *   n2vtd_client_proto.c   a stand-in that speaks the rev-2 decoder messages directly over the frozen
 *                          struct n2vcd_transport.  It exists because this package was written before DEC05 landed, and
 *                          it is what the host client test drives; it keeps the plug-in buildable without client/dec.
 *
 * Every call returns 0 on success or one of the negative N2VTD_CLIENT_E_* values.
 */
#ifndef N2VTD_CLIENT_H
#define N2VTD_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	N2VTD_CLIENT_ABSENT = -4096,    /* no client library / transport in this link */
	N2VTD_CLIENT_E_PROTO = -4097,   /* the daemon answered something the protocol does not allow */
	N2VTD_CLIENT_E_TIMEOUT = -4098, /* no reply within the client's timeout */
	N2VTD_CLIENT_E_NOMEM = -4099,
	N2VTD_CLIENT_E_HW = -4100,      /* the daemon reported N2VCD_E_HW / E_GUEST */
	N2VTD_CLIENT_E_SESSION = -4101, /* daemon/session gone; the object must not reuse this connection */
	N2VTD_CLIENT_E_ARGS = -4102,
	N2VTD_CLIENT_E_UNSUPPORTED = -4103,
	/* opus-robust (12 Nov 2026): dec_open only.  The daemon answered BUSY (another session is open: an encoder, or a
	 * second decoder), or BUSY with N2VCD_DETAIL_RESTART (--restart-on-switch: vcodecd exits 75 and launchd starts a
	 * fresh one).  Both are worth a short retry; StartSession then falls back to the software decoder. */
	N2VTD_CLIENT_E_BUSY = -4104,
	N2VTD_CLIENT_E_RESTART = -4105,
};

/* DEC_HEADER_OK: display size (crop), coded pitch/height, one output buffer's bytes, pool size, layout. */
struct n2vtd_header {
	uint32_t width, height;         /* display rectangle (vsi.crop), e.g. 1920x1080 */
	uint32_t stride;                /* Y pitch in bytes (vsi.pic.buf_w) */
	uint32_t buf_h;                 /* aligned picture height (vsi.pic.buf_h), e.g. 1088 */
	uint32_t out_bytes;             /* bytes of ONE pool entry */
	uint32_t y_bytes;               /* bytes of the Y plane inside an entry (stride * buf_h) */
	uint32_t dpb_frames;            /* pool entries to register */
	uint32_t format;                /* N2VTD_FMT_MM21 / _NV12 */
	uint32_t crop_x, crop_y;        /* clean-aperture origin inside the coded picture (usually 0,0) */
};

struct n2vtd_input {
	uint32_t pictures_ready;        /* how many DEC_PICTURE calls will succeed now */
	uint32_t changed;               /* 1: geometry change, the input was not consumed */
	uint32_t error_map;             /* vendor error map for this access unit, 0 = clean */
	uint32_t hw_us;
};

struct n2vtd_picture {
	uint32_t have;                  /* 0: nothing ready */
	uint32_t index;                 /* pool entry holding the picture */
	uint64_t pts;                   /* the pts passed to DEC_INPUT for this picture */
	int32_t poc;                    /* vendor display-order count, for traces */
	uint32_t width, height;         /* display size */
	uint32_t stride, buf_h;         /* source pitch / aligned height */
	uint32_t format;                /* N2VTD_FMT_* */
	uint32_t pictures_ready;        /* still queued after this one */
};

struct n2vtd_client {
	void *ctx;
	/* Open the connection and say HELLO.  socket_path NULL and device_path NULL = the phone relay.  timeout_ms 0 =
	 * the client's default; the availability probe passes its short budget here. */
	int (*connect)(void *ctx, void **conn, const char *socket_path, const char *device_path, uint32_t timeout_ms);
	/* OPEN(encoder = 0, codec, optional maximum size, requested layout) -> OPEN_OK; *in_bytes receives the
	 * bitstream buffer size the client allocated. */
	int (*dec_open)(void *ctx, void *conn, uint32_t codec, uint32_t max_width, uint32_t max_height, uint32_t format,
	                uint32_t *in_bytes);
	/* DEC_HEADER: Annex B parameter sets -> geometry and buffer needs. */
	int (*dec_header)(void *ctx, void *conn, const uint8_t *annexb, uint32_t bytes, struct n2vtd_header *hdr);
	/* Register the output pool (hdr->dpb_frames entries of hdr->out_bytes).  The client owns the buffers. */
	int (*dec_add_pool)(void *ctx, void *conn, const struct n2vtd_header *hdr);
	/* DEC_INPUT: one Annex B access unit.  The input buffer is reusable when this returns. */
	int (*dec_input)(void *ctx, void *conn, const uint8_t *au, uint32_t bytes, uint64_t pts,
	                 struct n2vtd_input *result);
	/* DEC_PICTURE: pop the next picture in display order. */
	int (*dec_picture)(void *ctx, void *conn, struct n2vtd_picture *picture);
	/* DEC_RELEASE: give the pool entry back after its pixels have been copied. */
	int (*dec_release)(void *ctx, void *conn, uint32_t index);
	/* Copy one plane of a pool entry: plane 0 = Y, 1 = C; offset relative to the plane; write != 0 host -> buffer. */
	int (*buffer_rw)(void *ctx, void *conn, uint32_t index, int plane, uint32_t offset, void *data, uint32_t bytes,
	                 int write);
	/* DEC_FREE_BUFFERS (resolution change / close). */
	int (*dec_free_buffers)(void *ctx, void *conn);
	/* FLUSH: the daemon drains the DPB; *pictures_ready receives how many DEC_PICTURE calls will succeed. */
	int (*dec_flush)(void *ctx, void *conn, uint32_t *pictures_ready);
	int (*dec_close)(void *ctx, void *conn);
	void (*disconnect)(void *ctx, void *conn);
	/* Optional: the reply timeout of subsequent calls (ms).  May be NULL. */
	int (*set_timeout)(void *ctx, uint32_t ms);
};

/* Install a table (copied by value).  NULL restores the default.  Returns 0, or -1 when an entry is NULL. */
int n2vtd_client_install(const struct n2vtd_client *client);

/* The current table; never NULL.  The default is DEC05's adapter when this build has it, else the protocol stand-in. */
const struct n2vtd_client *n2vtd_client_current(void);

/* The protocol stand-in (always available; the host client test drives it). */
const struct n2vtd_client *n2vtd_client_proto(void);

#ifdef __cplusplus
}
#endif
#endif /* N2VTD_CLIENT_H */
