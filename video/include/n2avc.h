/*
 * n2avc.h -- public interface of the original H.264 encoder of gpu/video/avc (plain C11, no Apple call, no allocation after open).
 * LEAD-OWNED, FROZEN. Implemented by WP03 (avc/n2avc_enc.c); used by WP05 (the VideoToolbox glue), WP10 (the oracle) and tests.
 *
 * The stream it writes (every picture, so that any decoder plays it):
 *   - Constrained Baseline: profile_idc 66, constraint_set0_flag = constraint_set1_flag = 1, level_idc from the table below
 *   - frame_mbs_only_flag 1, pic_order_cnt_type 2, num_ref_frames 1, CAVLC (entropy_coding_mode_flag 0), 4:2:0 8 bit
 *   - one SPS (id 0), one PPS (id 0): pic_init_qp 26, deblocking_filter_control_present_flag 1, no FMO/ASO/redundant pictures
 *   - sizes that are not multiples of 16 are coded with frame_cropping (right/bottom); the picture is padded by repeating the edge
 *   - slices: I (slice_type 7) and P (slice_type 5); `threads` slices per picture, each a whole number of macroblock rows
 *   - disable_deblocking_filter_idc 1 in every slice unless N2AVC_F_DEBLOCK is set (then 0, and the loop filter of clause 8.7
 *     runs on the reconstruction; stage 2)
 *   - no B pictures: decode order = presentation order
 *   - VUI: timing_info (num_units_in_tick = fps_den, time_scale = 2 * fps_num, fixed_frame_rate_flag 0), video_full_range_flag and
 *     colour description from the config; bitstream_restriction with max_num_reorder_frames 0, max_dec_frame_buffering 1
 * level_idc: the smallest of 30 (<= 720x576 @ 25), 31 (<= 1280x720 @ 30), 40 (<= 1920x1088 @ 30), 42 (@ 60), 51 (up to 4096x2304).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2AVC_H
#define N2AVC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2AVC_API_VERSION 1

enum {
	N2AVC_OK         = 0,
	N2AVC_E_ARGUMENT = -1,          /* NULL, size field too small, a dimension out of range, a stride smaller than the width */
	N2AVC_E_MEMORY   = -2,
	N2AVC_E_OVERFLOW = -3,          /* the picture did not fit the output buffer even as I_PCM: nothing was emitted */
	N2AVC_E_STATE    = -4,          /* called after an error or after close */
};

#define N2AVC_F_ANNEXB   0x01u      /* packets carry 00 00 00 01 start codes; default: 4-byte big-endian NAL lengths (AVCC) */
#define N2AVC_F_FULLRANGE 0x02u     /* video_full_range_flag 1 ('420f') */
#define N2AVC_F_DEBLOCK  0x04u      /* in-loop deblocking on (stage 2; WP03 answers N2AVC_E_ARGUMENT until it is implemented) */
#define N2AVC_F_INTRA_ONLY 0x08u    /* every picture an IDR */
#define N2AVC_F_HEADERS_IN_KEYFRAMES 0x10u  /* SPS + PPS NAL units before each IDR's slices (always so with N2AVC_F_ANNEXB) */

typedef struct n2avc_config {
	uint32_t size;                  /* sizeof(n2avc_config) */
	uint32_t width, height;         /* luma samples: 16..4096 x 16..2304 */
	uint32_t fps_num, fps_den;      /* e.g. 30, 1; fps_den 0 -> 30/1 */
	uint32_t bitrate;               /* bits per second; 0 = constant QP `qp` */
	uint32_t qp;                    /* 10..51; with a bitrate: the first picture's QP (0 -> 26) */
	uint32_t keyint;                /* an IDR at least every `keyint` pictures; 0 -> 60; 1 = intra only */
	uint32_t threads;               /* slices per picture, coded in parallel: 1..16; 0 -> 1 */
	uint32_t flags;                 /* N2AVC_F_* */
	uint32_t search_range;          /* motion search, integer samples each way: 0 -> 16; at most 64 */
	uint8_t  colour_primaries, transfer, matrix, reserved;     /* H.264 table E-3..E-5 numbers; 0 -> 1 (BT.709) when height >= 720, else 6 */
} n2avc_config;

typedef struct n2avc_picture {
	uint32_t format;                /* N2AVC_FMT_NV12: plane[0] luma, plane[1] interleaved CbCr; N2AVC_FMT_I420: [0] Y, [1] Cb, [2] Cr */
	const uint8_t *plane[3];
	uint32_t stride[3];             /* bytes per row of each plane */
	int64_t  pts;                   /* copied to the packet */
} n2avc_picture;
#define N2AVC_FMT_NV12 0u
#define N2AVC_FMT_I420 1u

typedef struct n2avc_packet {
	const uint8_t *data;            /* all NAL units of the picture; valid until the next call on this encoder */
	size_t   size;
	int64_t  pts;
	uint32_t keyframe;              /* 1: IDR */
	uint32_t qp;                    /* the picture's slice QP */
	uint32_t frame_num;
} n2avc_packet;

typedef struct n2avc_enc n2avc_enc;

int  n2avc_open(const n2avc_config *config, n2avc_enc **out);
/* The parameter sets as raw NAL units (header byte + escaped RBSP, no start code, no length): what
 * CMVideoFormatDescriptionCreateFromH264ParameterSets wants. Valid until close. */
int  n2avc_headers(n2avc_enc *enc, const uint8_t **sps, size_t *sps_size, const uint8_t **pps, size_t *pps_size);
/* Codes one picture, synchronously. force_idr != 0 makes it an IDR. On success *out describes the packet. */
int  n2avc_encode(n2avc_enc *enc, const n2avc_picture *picture, int force_idr, n2avc_packet *out);
/* Changes the target bitrate / frame rate from the next picture on (0 = keep). */
int  n2avc_reconfigure(n2avc_enc *enc, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den);
/* The encoder's reconstruction of the last coded picture (what a conforming decoder must output, cropped to width x height):
 * three planes Y, Cb, Cr. For the oracle: decode the packet with ffmpeg and compare byte for byte. */
int  n2avc_reconstruction(n2avc_enc *enc, const uint8_t *plane[3], uint32_t stride[3]);
void n2avc_close(n2avc_enc *enc);

#ifdef __cplusplus
}
#endif
#endif
