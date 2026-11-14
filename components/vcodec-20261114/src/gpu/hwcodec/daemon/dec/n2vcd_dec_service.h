/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dec_service.h -- the decoder half of vcodecd: the protocol state machine between a client
 * (client/dec/n2vcd_dec.c) and an AP-side VDEC session (daemon/dec/n2vcd_dec_ap_n2ap.c, which
 * wraps ap/n2ap_vdec.c).  Wire format: include/n2vcd_proto.h rev 2 (text:
 * daemon/dec/n2vcd_proto_dec_rev2.h.txt).
 *
 * Why this is a separate module from vcodecd.c (hw-ip owns that file): the decoder has its own
 * session type, output pool and picture queue, while the daemon's socket/relay receive loop, the
 * vhost guest, the watchdog and the exit-70 policy are shared.  hw-ip's dispatcher calls
 * n2vcd_dec_service_handle() for every datagram; N2VCD_OPEN with encoder == 0 and every
 * N2VCD_DEC_* type are ours (n2vcd_dec_service_owns()), the encoder types stay with vcodecd.c.
 *
 * Sessions: one decoder session at a time (N2VCD_E_BUSY, detail = the active session id), the same
 * limit the encoder path enforces.  A client that closes its transport, times out or dies releases
 * the session.  Session idle time is bounded exactly as in vcodecd.c: n2vcd_dec_service_tick()
 * drops a session after cfg.session_idle_ms without a request (0 disables).
 *
 * Fatal guest stop: an AP-side guest stop is fatal for the whole daemon.  The service answers the
 * in-flight request with N2VCD_E_GUEST + the n2a32 stop code, sends the same event with seq 0 to
 * every other client that has sent traffic, logs one line through cfg.log and calls cfg.fatal.
 * vcodecd's callback must do what fatal_guest_stop() in daemon/vcodecd.c does (drop power refs
 * best effort, log, _exit(70)); the service never touches the AP session after a stop.
 *
 * No printing from this file: cfg.log is the only output.  No allocation after create (a fixed
 * pool of N2VCD_DEC_MAX_POOL entries), so a long decode cannot fragment the daemon.
 *
 * Threading: one thread owns the service (the daemon's request thread); every entry point must be
 * called from it.  The AP ops run synchronously inside that thread, which is also where the VCU
 * model delivers the vendor's LOCK/WAIT/GET/PUT requests.
 */
#ifndef N2VCD_DEC_SERVICE_H
#define N2VCD_DEC_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "n2vcd_proto.h"

#define N2VCD_DEC_MAX_POOL 16u          /* ap/n2ap_vdec.h N2AP_VDEC_FB_QUEUE */
#define N2VCD_DEC_MAX_WIDTH 4096u
#define N2VCD_DEC_MAX_HEIGHT 2304u   /* opus-decint: 4096x2176 (DCI 4K) and 4096x2304 fit; the vendor caps H.264/HEVC at 4096 per side */
#define N2VCD_DEC_MSG_MAX 1024u         /* RWVC_MSG_MAX; a datagram is never larger */

/* One client DMA buffer as the AP side needs it.  pa is 0 when the transport cannot resolve it
 * (host socket tests); the real daemon fills it from RWVC_DMA_INFO, which is what the AP side
 * registers in its n2vc_dma table. */
struct n2vcd_dec_ap_buffer {
	uint32_t handle;
	uint32_t pa;
	uint32_t bytes;
};

struct n2vcd_dec_ap_geometry {
	uint32_t width, height;         /* display size (crop), e.g. 1920x1080 */
	uint32_t stride;                /* Y pitch, coded width buf_w */
	uint32_t buf_h;                 /* aligned coded height */
	uint32_t out_buf_bytes;         /* Y + C bytes of one pool entry */
	uint32_t dpb_frames;            /* pool entries needed, 1..N2VCD_DEC_MAX_POOL */
	uint32_t fourcc;                /* N2VCD_FMT_MM21 (or another N2VCD_FMT_*) */
};

struct n2vcd_dec_ap_result {
	uint32_t pictures_ready;
	uint32_t changed;
	uint32_t error_map;
	uint32_t hw_us;
};

struct n2vcd_dec_ap_picture {
	uint32_t index;                 /* registered pool index holding the picture */
	uint64_t pts;                   /* display-order timestamp (the vendor's vsi timestamp) */
	int32_t poc;
	uint32_t pictures_ready;        /* still queued after this one */
};

/*
 * The AP session behind one decoder session.  All calls synchronous, 0/-errno.  `header` performs
 * the vendor's header-only START (no frame buffer) and returns the geometry; after a resolution
 * change it is called again with the same input.  `input` consumes one Annex B access unit and
 * returns how many pictures are queued; `picture` pops the next one in display order (1 = have,
 * 0 = none).  `release` returns a pool index to the AP side (it may defer the actual re-queue until
 * the vendor frees the reference).  `free_buffers` forgets the whole pool and is only called with
 * no picture outstanding.
 *
 * Errors: -EOPNOTSUPP (codec/format), -EINVAL (bad argument), -ENOMEM, -EBUSY (no free reference
 * buffer yet), -ETIMEDOUT (hardware), -EIO (hardware), -EPROTO (vendor protocol violation).
 * A guest stop is NOT an errno: guest_stopped() is the only source of that event, exactly as
 * guest_stopped() works in daemon/vcodecd.c.
 */
struct n2vcd_dec_ap_ops {
	int (*open)(void *ctx, uint32_t codec);
	int (*header)(void *ctx, const struct n2vcd_dec_ap_buffer *in, uint32_t in_bytes,
	              struct n2vcd_dec_ap_geometry *geo);
	int (*add_buffer)(void *ctx, uint32_t index, const struct n2vcd_dec_ap_buffer *y,
	                  const struct n2vcd_dec_ap_buffer *c);
	int (*input)(void *ctx, const struct n2vcd_dec_ap_buffer *in, uint32_t in_bytes,
	             uint64_t pts, uint32_t flags, struct n2vcd_dec_ap_result *res);
	int (*picture)(void *ctx, struct n2vcd_dec_ap_picture *pic);
	int (*release)(void *ctx, uint32_t index);
	int (*free_buffers)(void *ctx);
	int (*flush)(void *ctx, uint32_t *pictures_ready);
	void (*close)(void *ctx);
	/* 1 and *code = the n2a32 stop reason when the hosted guest has stopped. */
	int (*guest_stopped)(void *ctx, uint32_t *code);
};

struct n2vcd_dec_service_cfg {
	int (*send)(void *ctx, uint32_t to, const void *msg, uint32_t bytes);   /* required */
	/* Optional: handle -> physical address for the AP side (RWVC_DMA_INFO on the phone). */
	int (*dma_info)(void *ctx, uint32_t handle, uint32_t *pa, uint32_t *bytes);
	void *ctx;                      /* first argument of send/dma_info/log/fatal */
	const struct n2vcd_dec_ap_ops *ap;                                      /* required */
	void *ap_ctx;
	void (*log)(void *ctx, const char *line);        /* optional, one line per event */
	void (*fatal)(void *ctx, uint32_t code);         /* guest stop: vcodecd replies, then _exit(70) */
	uint32_t session_idle_ms;       /* 0 = never drop (vcodecd passes its --session-idle-ms) */
	uint32_t in_buf_bytes;          /* suggested input buffer size; 0 = 4 MiB */
	uint32_t max_width, max_height; /* 0 = 4096 x 2160 */
};

struct n2vcd_dec_service;

struct n2vcd_dec_service *n2vcd_dec_service_create(const struct n2vcd_dec_service_cfg *cfg);
void n2vcd_dec_service_destroy(struct n2vcd_dec_service *s);

/* 1 when this datagram belongs to the decoder service: a well-formed header of a N2VCD_DEC_* type,
 * or N2VCD_OPEN with encoder == 0.  Never sends anything. */
int n2vcd_dec_service_owns(const void *msg, uint32_t bytes);

/* Handle one datagram from client `from`.
 *   0            handled, a reply was sent through cfg.send (a send failure is handled internally)
 *   -EOPNOTSUPP  not a decoder datagram: the caller's encoder path must handle it
 *   -ENODEV      the service already died from a guest stop (no reply sent) */
int n2vcd_dec_service_handle(struct n2vcd_dec_service *s, uint32_t from, const void *msg, uint32_t bytes);

/* A client closed its transport (the transport's close notice).  Frees its session, if any. */
void n2vcd_dec_service_client_gone(struct n2vcd_dec_service *s, uint32_t from);

/* Idle timeout + guest-stop poll.  Call at least once per second from the daemon's receive loop
 * (a 0 timeout_wait_ms receive is fine).  Returns 0, or -ENODEV after a fatal guest stop. */
int n2vcd_dec_service_tick(struct n2vcd_dec_service *s, uint64_t now_ms);

/* Test/diagnostic accessors (no wire meaning). */
uint32_t n2vcd_dec_service_session(const struct n2vcd_dec_service *s);   /* 0 = none */
uint32_t n2vcd_dec_service_open_count(const struct n2vcd_dec_service *s);
int n2vcd_dec_service_dead(const struct n2vcd_dec_service *s);

#endif /* N2VCD_DEC_SERVICE_H */
