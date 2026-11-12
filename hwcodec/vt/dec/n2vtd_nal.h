/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_nal.h -- H.264/HEVC parameter sets for the VideoToolbox decoder object: the avcC (ISO/IEC 14496-15 5.2.4.1.1)
 * and hvcC (8.3.3.1.1) records that a CMFormatDescription carries in its sample-description extension atoms, the SPS
 * fields that decide the output picture (coded/display size, chroma, bit depth, VUI video_full_range_flag), and the
 * conversion between the length-prefixed samples of MP4 and the Annex B byte stream the daemon takes.
 *
 * Written from the specifications and from the ARM reference bit-reader rules; no third-party source. Everything here is
 * pure data transformation, no Apple calls except n2vtd_params_from_format() (which uses CoreMedia to fetch the atoms).
 */
#ifndef N2VTD_NAL_H
#define N2VTD_NAL_H

#include <stddef.h>
#include <stdint.h>

#include "n2_apple.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Two spellings of the same four-character codes, and they are NOT the same integer:
 *  - the daemon's N2VCD_CODEC_* (include/n2vcd_proto.h) store the characters in memory order, 'a','v','c','1' as a
 *    little-endian word = 0x31637661;
 *  - VideoToolbox's CMVideoCodecType is the FourCC value 'a'<<24 | 'v'<<16 | 'c'<<8 | '1' = 0x61766331.
 * The factories receive the latter, everything sent to vcodecd uses the former. */
#define N2VTD_CODEC_AVC1 0x31637661u    /* daemon: 'avc1' bytes in memory order */
#define N2VTD_CODEC_HVC1 0x31637668u    /* daemon: 'hvc1' bytes in memory order */
#define N2VTD_FOURCC_AVC1 0x61766331u   /* VideoToolbox CMVideoCodecType 'avc1' */
#define N2VTD_FOURCC_HVC1 0x68766331u   /* VideoToolbox CMVideoCodecType 'hvc1' */
#define N2VTD_FOURCC_HEV1 0x68657631u   /* VideoToolbox CMVideoCodecType 'hev1' */

#define N2VTD_MAX_VPS 4
#define N2VTD_MAX_SPS 8
#define N2VTD_MAX_PPS 16
#define N2VTD_MAX_PARAM_BYTES 4096      /* one parameter set; VPS/SPS/PPS of 8-bit 4:2:0 are far smaller */

/* One parameter set: borrowed bytes (a slice of the record passed in, or of the caller's blob). */
struct n2vtd_param {
	const uint8_t *data;
	size_t size;
};

/* Parsed avcC/hvcC.  The struct borrows `record`: it must stay valid while any n2vtd_param points into it. */
struct n2vtd_params {
	uint32_t codec;                 /* N2VTD_CODEC_AVC1 / _HVC1 */
	uint32_t length_size;           /* NAL length field width in the samples, 1..4 */
	struct n2vtd_param vps[N2VTD_MAX_VPS];
	struct n2vtd_param sps[N2VTD_MAX_SPS];
	struct n2vtd_param pps[N2VTD_MAX_PPS];
	int nvps, nsps, npps;
	const uint8_t *record;
	size_t record_size;
};

/* Errors (negative, so 0 always means success). */
enum {
	N2VTD_ENAL_FORMAT  = -1,        /* the record is not a valid avcC/hvcC of a supported profile */
	N2VTD_ENAL_SIZE    = -2,        /* a buffer is too small or a length runs past the end */
	N2VTD_ENAL_UNSUP   = -3,        /* codec not supported by this module */
	N2VTD_ENAL_ABSENT  = -4,        /* no avcC/hvcC atom in the format description */
	N2VTD_ENAL_SPS     = -5,        /* SPS could not be read (truncated / unsupported profile) */
};

/* avcC / hvcC record parsers.  On success every n2vtd_param points into rec. */
int n2vtd_avcc_parse(const uint8_t *rec, size_t bytes, struct n2vtd_params *out);
int n2vtd_hvcc_parse(const uint8_t *rec, size_t bytes, struct n2vtd_params *out);

/* The parameter sets of a CMFormatDescription: read the 'avcC'/'hvcC' atom out of the sample-description extension
 * atoms (the extension-dictionary route of the task) and parse it.  Returns 0, or N2VTD_ENAL_ABSENT when the format
 * description has no atoms (then the caller may use CMVideoFormatDescriptionGet*ParameterSetAtIndex instead). */
int n2vtd_params_from_format(CMFormatDescriptionRef fmt, uint32_t codec, struct n2vtd_params *out);

/* Like n2vtd_params_from_format(), with the CMVideoFormatDescriptionGet*ParameterSetAtIndex route as the fallback.
 * The returned n2vtd_param entries point either into the format description (atoms) or into `blob`, which must stay
 * valid while `out` is used.  Returns 0 or a negative N2VTD_ENAL_*. */
int n2vtd_params_load(CMFormatDescriptionRef fmt, uint32_t codec, uint8_t *blob, size_t blob_cap,
                      struct n2vtd_params *out);

/* Annex B form of the parameter sets: VPS (HEVC only), SPS, PPS, each with a 4-byte start code.  Writes at most cap
 * bytes and returns the number written, or N2VTD_ENAL_SIZE when they do not fit. */
int n2vtd_params_to_annexb(const struct n2vtd_params *p, uint8_t *dst, size_t cap);

/* SPS fields the decoder object needs.  `full_range` is VUI video_full_range_flag (0 = limited/420v, 1 = full/420f). */
struct n2vtd_sps_info {
	uint32_t coded_width, coded_height;     /* macroblock/CTB aligned */
	uint32_t display_width, display_height; /* after the conformance window */
	uint32_t chroma_format_idc;             /* 1 = 4:2:0 */
	uint32_t bit_depth_luma, bit_depth_chroma;
	int full_range;
	int interlaced;                         /* frame_mbs_only_flag == 0 / field_seq_flag */
};

/* Parse one SPS NAL (with or without its header byte; `bytes` counts the whole NAL).  Returns 0 or N2VTD_ENAL_SPS. */
int n2vtd_sps_parse(uint32_t codec, const uint8_t *nal, size_t bytes, struct n2vtd_sps_info *out);

/* Convert one length-prefixed sample (the CMBlockBuffer of a CMSampleBuffer) to Annex B.  length_size comes from the
 * avcC/hvcC record; 0 means "detect from the first length field" (only accepted by callers that know the sample is
 * length-prefixed).  `types` (may be NULL) receives a bitmap of NAL unit types seen, interpreted for `codec`.
 * Returns the number of bytes written, or a negative N2VTD_ENAL_*. */
int n2vtd_sample_to_annexb(uint32_t codec, const uint8_t *src, size_t bytes, uint32_t length_size, uint8_t *dst,
                           size_t cap, uint64_t *types);

/* Collect the parameter-set NALs of an Annex B buffer (AVC 7/8, HEVC 32/33/34).  At most max entries are stored and
 * *count receives how many were found.  Returns 0, or a negative error. */
int n2vtd_annexb_find_params(uint32_t codec, const uint8_t *buf, size_t bytes, struct n2vtd_param *out, int max,
                             int *count);

/* True when two parameter-set lists hold byte-identical SPS and PPS (and VPS for HEVC).  Used by
 * CanAcceptFormatDescription: VideoToolbox only lets us continue when the new format is the same stream. */
int n2vtd_params_equal(const struct n2vtd_params *a, const struct n2vtd_params *b);

#ifdef __cplusplus
}
#endif
#endif /* N2VTD_NAL_H */
