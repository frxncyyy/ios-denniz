/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_proto.h -- FROZEN: messages between a codec client (VideoToolbox plug-in, test tool) and vcodecd, the daemon that
 * hosts the vendor codec library. Owner: hwcodec-lead. Transport-independent datagrams (<= 1024 bytes): on the phone the
 * kernel relay of /dev/redwood-vcodec-client (RWVC_MSG_SEND/RECV), in host tests a SOCK_SEQPACKET socketpair / UNIX socket.
 * Pixel and bit-stream data never travel in messages: they sit in DMA buffers (RWVC_DMA_ALLOC with the share flag on the
 * phone; a shared file in host tests) named by handle. Little-endian, fixed width, every message starts with n2vcd_hdr.
 *
 * One session = one encoder or decoder instance. Requests are answered in order, one reply per request (same seq).
 */
#ifndef N2VCD_PROTO_H
#define N2VCD_PROTO_H
#include <stdint.h>

#define N2VCD_MAGIC   0x4443564eu   /* "NVCD" */
#define N2VCD_VERSION 1u   /* stays 1 with the rev-2 decoder ids (opus-lead2 decision a): dec ids 20..29 are additive, advertised by HELLO_OK.dec_codecs */
enum n2vcd_type {
	N2VCD_HELLO = 1, N2VCD_HELLO_OK,                 /* version check, capabilities */
	N2VCD_OPEN, N2VCD_OPEN_OK,                       /* create a session */
	N2VCD_ENC_FRAME, N2VCD_ENC_FRAME_OK,
	N2VCD_ENC_SET, N2VCD_ENC_SET_OK,                 /* bitrate / framerate / force key frame at run time */
	N2VCD_DEC_HEADER, N2VCD_DEC_HEADER_OK,           /* parameter sets -> picture geometry, buffer needs */
	N2VCD_DEC_FRAME, N2VCD_DEC_FRAME_OK,
	N2VCD_FLUSH, N2VCD_FLUSH_OK,
	N2VCD_CLOSE, N2VCD_CLOSE_OK,
	N2VCD_ERROR = 100,                               /* reply to any request: status != 0 */
	/* rev 2 (opus-decint landed 2026-11-11, text daemon/dec/n2vcd_proto_dec_rev2.h.txt): decoder ids, additive */

	N2VCD_DEC_ADD_BUFFER = 20, N2VCD_DEC_ADD_BUFFER_OK,
	N2VCD_DEC_FREE_BUFFERS, N2VCD_DEC_FREE_BUFFERS_OK,      /* reply: n2vcd_simple */
	N2VCD_DEC_INPUT, N2VCD_DEC_INPUT_OK,
	N2VCD_DEC_PICTURE, N2VCD_DEC_PICTURE_OK,
	N2VCD_DEC_RELEASE, N2VCD_DEC_RELEASE_OK,                /* reply: n2vcd_simple */
};
enum { N2VCD_CODEC_H264 = 0x31637661 /* 'avc1' */, N2VCD_CODEC_HEVC = 0x31637668 /* 'hvc1' */ };
/* opus-fmt (2026-11-12, proposal): the other hardware decoders (facts/FORMATS.md).  Values = the CMVideoCodecType fourccs
 * VideoToolbox uses ('vp08' is WebKit's), little-endian like 'avc1'.  HELLO_OK.dec_codecs bits: 0 H.264, 1 HEVC, 2 VP8,
 * 3 VP9, 4 AV1, 5 MPEG-1/2, 6 MPEG-4 part 2, 7 H.263.  Decoder only (the VENC encodes H.264/HEVC only). */
enum { N2VCD_CODEC_VP8 = 0x38307076 /* 'vp08' */, N2VCD_CODEC_VP9 = 0x39307076 /* 'vp09' */,
	N2VCD_CODEC_AV1 = 0x31307661 /* 'av01' */, N2VCD_CODEC_MPEG2 = 0x7632706d /* 'mp2v' */,
	N2VCD_CODEC_MPEG4 = 0x7634706d /* 'mp4v' */, N2VCD_CODEC_H263 = 0x33363268 /* 'h263' */ };
#define N2VCD_DEC_CODECS_ALL 0xffu
enum { N2VCD_E_OK = 0, N2VCD_E_VERSION = 1, N2VCD_E_UNSUPPORTED = 2, N2VCD_E_BUSY = 3, N2VCD_E_ARGS = 4, N2VCD_E_HW = 5,
	N2VCD_E_GUEST = 6 /* the hosted library stopped: detail = n2a32 stop reason */, N2VCD_E_NOMEM = 7, N2VCD_E_SESSION = 8 };
/* opus-enc4k: detail of an OPEN answered N2VCD_E_BUSY by a vcodecd started with --restart-on-switch: the daemon exits right
 * after the reply and launchd restarts it; reconnect (RWVC_INFO.server_present / the socket) and repeat HELLO + OPEN. */
#define N2VCD_DETAIL_RESTART 0x52535452u   /* "RSTR" */

struct n2vcd_hdr { uint32_t magic, type, seq, session; };        /* session 0 before OPEN_OK */
struct n2vcd_hello { struct n2vcd_hdr h; uint32_t version, reserved; };
struct n2vcd_hello_ok { struct n2vcd_hdr h; uint32_t version, enc_codecs, dec_codecs, max_width, max_height, reserved; };   /* codec bits: 1 H.264, 2 HEVC */
struct n2vcd_open {
	struct n2vcd_hdr h;
	uint32_t codec, encoder;                         /* N2VCD_CODEC_*; 1 = encoder, 0 = decoder */
	uint32_t width, height;                          /* encoder: picture size; decoder: 0 (from the stream) */
	uint32_t fps_num, fps_den, bitrate;              /* encoder; bits per second */
	uint32_t gop;                                    /* encoder: key frame distance, 0 = default (fps) */
	uint32_t profile, level;                         /* encoder: H.264 profile_idc (66/77/100) / level_idc; 0 = default */
	uint32_t flags, reserved[5];                     /* bit 0: low latency / real time */
};
struct n2vcd_open_ok { struct n2vcd_hdr h; uint32_t status, in_buf_bytes, out_buf_bytes, reserved; };   /* sizes the client must allocate */
struct n2vcd_enc_frame {
	struct n2vcd_hdr h;
	uint32_t in_handle;                              /* DMA buffer: NV12, luma at 0 with `stride`, chroma at stride * align16(height) */
	uint32_t stride;
	uint32_t out_handle, out_capacity;               /* DMA buffer for the access unit (Annex B byte stream with start codes) */
	uint64_t pts;
	uint32_t force_key, reserved;
};
struct n2vcd_enc_frame_ok { struct n2vcd_hdr h; uint32_t status, out_bytes, key_frame, hw_us; uint64_t pts;
	uint32_t header_bytes;                           /* > 0: the access unit starts with SPS/PPS(/VPS) of this many bytes */
	uint32_t reserved; };
struct n2vcd_enc_set { struct n2vcd_hdr h; uint32_t bitrate, fps_num, fps_den, force_key; };
struct n2vcd_dec_header { struct n2vcd_hdr h; uint32_t in_handle, in_bytes; };                      /* Annex B parameter sets */
struct n2vcd_dec_header_ok { struct n2vcd_hdr h; uint32_t status, width, height, stride, out_buf_bytes, dpb_frames, reserved[2]; };
struct n2vcd_dec_frame { struct n2vcd_hdr h; uint32_t in_handle, in_bytes; uint32_t out_handle, out_capacity; uint64_t pts; };
struct n2vcd_dec_frame_ok { struct n2vcd_hdr h; uint32_t status, have_picture; uint64_t pts;       /* pts of the picture returned (display order) */
	uint32_t width, height, stride, hw_us; };
struct n2vcd_simple { struct n2vcd_hdr h; uint32_t status, detail; };                               /* FLUSH*, CLOSE*, *_SET_OK, ERROR */

#ifndef N2VCD_PROTO_REV2
#define N2VCD_PROTO_REV2 2u     /* rev-2 decoder messages are present */
#endif
enum { N2VCD_FMT_MM21 = 0x31324d4d /* 'MM21': 8-bit 4:2:0, Y 16x32 tiles, CbCr 16x16 tiles (2 planes) */,
	N2VCD_FMT_NV12 = 0x3231564e /* 'NV12' linear, only if the vendor ever accepts the layout */ };

/* --- OPEN.flags bits 8..15 (decoder): requested output format ------------------------ */
enum { N2VCD_OPEN_FMT_SHIFT = 8, N2VCD_OPEN_FMT_MASK = 0xffu,
	N2VCD_OPEN_FMT_AUTO = 0, N2VCD_OPEN_FMT_MM21 = 1, N2VCD_OPEN_FMT_NV12 = 2 };

/* --- structs, paste after struct n2vcd_simple --------------------------------------- */
/* rev-2 meaning of struct n2vcd_dec_header_ok (layout unchanged):
 *   width, height   = display size (vsi.crop[2..3]), e.g. 1920x1080
 *   stride          = buf_w (vsi.pic.buf_w), e.g. 1920; also the MM21 row pitch in bytes of the Y plane
 *   out_buf_bytes   = bytes of ONE output buffer = fb_sz[0] + fb_sz[1] (1080p MM21: 2088960 + 1044480)
 *                     Y plane at offset 0 with y_bytes = stride * buf_h, C plane with c_bytes =
 *                     out_buf_bytes - y_bytes (each plane is its own RWVC DMA buffer)
 *   dpb_frames      = buffers the client must register: vsi.dec.dpb_sz + 1 (V4L2 GET_PARAM_DPB_SIZE)
 *                     + 2 (one decoding, one held by the client), clamped to 16
 *   reserved[0]     = N2VCD_FMT_* of the output,  reserved[1] = buf_h (vsi.pic.buf_h, e.g. 1088) */
struct n2vcd_dec_add_buffer { struct n2vcd_hdr h; uint32_t handle, bytes;      /* Y plane / whole buffer */
	uint32_t c_handle, c_bytes; };   /* 0,0: C at stride*buf_h inside handle; else a second buffer */
struct n2vcd_dec_add_buffer_ok { struct n2vcd_hdr h; uint32_t status, index; };      /* index 0..15 */
struct n2vcd_dec_input { struct n2vcd_hdr h; uint32_t in_handle, in_bytes; uint64_t pts;
	uint32_t flags, reserved; };                       /* flags bit 0: discontinuity (seek) -> RESET first */
struct n2vcd_dec_input_ok { struct n2vcd_hdr h; uint32_t status, pictures_ready;
	uint32_t changed;                                  /* 1: geometry change, input not consumed (see flow) */
	uint32_t error_map;                                /* vsi.dec.error_map of this input, 0 = clean */
	uint32_t hw_us, reserved; };
struct n2vcd_dec_picture { struct n2vcd_hdr h; };
struct n2vcd_dec_picture_ok { struct n2vcd_hdr h; uint32_t status, have; uint64_t pts;
	uint32_t index;                                    /* registered buffer holding the picture; client owns it */
	uint32_t width, height, stride, buf_h, fourcc;     /* display size, Y pitch, aligned height, N2VCD_FMT_* */
	uint32_t pictures_ready;                           /* still queued after this one */
	int32_t poc; };                                    /* AMENDED 2: was reserved, same offset */
struct n2vcd_dec_release { struct n2vcd_hdr h; uint32_t index, reserved; };

/* client library (P03), used by the plug-in and the tools; `transport` hides relay vs socket */
struct n2vcd_transport {
	void *ctx;
	int (*send)(void *ctx, const void *msg, uint32_t bytes);
	int (*recv)(void *ctx, void *msg, uint32_t capacity, uint32_t timeout_ms);      /* bytes or -errno */
	int (*buf_alloc)(void *ctx, uint32_t bytes, uint32_t *handle);
	int (*buf_rw)(void *ctx, uint32_t handle, uint32_t offset, void *data, uint32_t bytes, int write);
	int (*buf_free)(void *ctx, uint32_t handle);
	void (*close)(void *ctx);
};
int n2vcd_transport_relay(struct n2vcd_transport *t, const char *dev_path);        /* P03: "/dev/redwood-vcodec-client" */
int n2vcd_transport_socket(struct n2vcd_transport *t, const char *unix_path);      /* P03: host tests */
#endif
