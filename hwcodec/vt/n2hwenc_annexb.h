/* SPDX-License-Identifier: MIT */
/*
 * n2hwenc_annexb.h -- Annex B byte streams and the ISO/IEC 14496-15 avcC/hvcC records, for the hardware H.264/HEVC
 * VideoToolbox encoder objects (package P04).
 *
 * An Annex B access unit is a sequence of NAL units, each introduced by a three- or four-byte start code
 * (00 00 01 / 00 00 00 01).  The NAL unit itself ends before the next start code; the trailing zero bytes an encoder may
 * insert in front of a start code (trailing_zero_8bits, or the extra zero of a four-byte start code) are not part of it.
 * A length-prefixed (AVCC) sample is the same NAL units, each preceded by its length as a four-byte big-endian number,
 * and nothing else.
 *
 * Everything here is written from ISO/IEC 14496-15 (AVC file format: 5.2.4.1.1 for avcC, HEVC file format: 8.3.3.1.1
 * for hvcC) and ITU-T H.264 (7.3.1, 7.4.1) / ITU-T H.265 (7.3.1.2) for the NAL headers, and the HEVC syntax of
 * ITU-T H.265 7.3.2.2-7.3.2.3 for the profile_tier_level and SPS fields hvcC copies.  No third-party source is involved.
 * SPDX-License-Identifier: MIT
 */
#ifndef N2HWENC_ANNEXB_H
#define N2HWENC_ANNEXB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum n2hwenc_codec {
	N2HWENC_AVC  = 0,               /* H.264 / AVC, codec type 'avc1' */
	N2HWENC_HEVC = 1                /* H.265 / HEVC, codec type 'hvc1' */
};

#define N2HWENC_NAL_MAX 256         /* NAL units per access unit this code handles */

/* One NAL unit found in an Annex B buffer. */
struct n2hwenc_nal {
	uint32_t offset;                /* first payload byte in the buffer (after the start code) */
	uint32_t size;                  /* payload bytes, trailing zero bytes of the start code excluded */
	uint8_t  type;                  /* nal_unit_type: 5 bits (AVC, nal[0] & 0x1f), 6 bits (HEVC, (nal[0] >> 1) & 0x3f) */
	uint8_t  key;                   /* 1 when this NAL alone makes the access unit a key frame:
	                                   AVC type 5 (IDR) / HEVC 16..23 (BLA, IDR, CRA) */
	uint8_t  param;                 /* 1 for a parameter set: AVC 7 (SPS), 8 (PPS); HEVC 32 (VPS), 33 (SPS), 34 (PPS) */
	uint8_t  reserved;
};

/* One parameter set handed to the avcC/hvcC builders. */
struct n2hwenc_ps {
	uint8_t        type;            /* nal_unit_type as above */
	const uint8_t *data;            /* NAL unit bytes, without a start code */
	uint32_t       bytes;
};

/*
 * Scan an Annex B buffer.  Fills up to `capacity` entries and stores the number found in *count.
 * Returns 0, or -1 when the buffer is malformed (start code with no payload), holds no NAL unit, or holds more NAL
 * units than `capacity` (no silent truncation: the caller's capacity is N2HWENC_NAL_MAX).
 */
int n2hwenc_annexb_scan(const uint8_t *buf, uint32_t bytes, int codec,
                        struct n2hwenc_nal *nals, uint32_t capacity, uint32_t *count);

/*
 * Convert an Annex B access unit to a length-prefixed sample, in place.  `capacity` is the buffer's real capacity;
 * the conversion needs at most n2hwenc_annexb_expanded(bytes) bytes.  When strip_param is non-zero every parameter-set
 * NAL unit (see struct n2hwenc_nal.param) is removed from the sample (it belongs in the format description).
 * Stores the sample size in *out_bytes and the number of NAL units kept in *out_nals (either may be NULL).
 * Returns 0, -1 on a malformed stream, or -2 when the result does not fit `capacity`.
 */
int n2hwenc_annexb_to_avcc(uint8_t *buf, uint32_t bytes, uint32_t capacity, int codec, int strip_param,
                           uint32_t *out_bytes, uint32_t *out_nals);

/* Upper bound of the length-prefixed size of an Annex B buffer of `bytes` bytes (every start code three bytes). */
uint32_t n2hwenc_annexb_expanded(uint32_t bytes);

/*
 * Build the avcC record (ISO/IEC 14496-15 5.2.4.1.1) from SPS and PPS NAL units.  All entries whose type is 7 (SPS) are
 * written first, then all type 8 (PPS); at least one of each is required.  Returns the byte count written, or -1 when the
 * sets are incomplete/too large, the first SPS cannot be parsed, or the output does not fit.
 */
int n2hwenc_avcc_build(const struct n2hwenc_ps *sets, uint32_t count, uint8_t *out, uint32_t capacity);

/*
 * Build the hvcC record (ISO/IEC 14496-15 8.3.3.1.1) from VPS (32), SPS (33) and PPS (34) NAL units.  The arrays are
 * written in the order VPS, SPS, PPS, each only when present; an SPS is required (its profile_tier_level and chroma/bit
 * depth fields are copied).  min_spatial_segmentation_idc, parallelismType, avgFrameRate and constantFrameRate are
 * written as 0 ("no restriction signalled"), the values an encoder that does not signal them gets.  Returns the byte
 * count written, or -1.
 */
int n2hwenc_hvcc_build(const struct n2hwenc_ps *sets, uint32_t count, uint8_t *out, uint32_t capacity);

/*
 * Classify one NAL unit.  Fills *key and *param as in struct n2hwenc_nal; returns the nal_unit_type, or -1 when
 * `bytes` is 0.
 */
int n2hwenc_nal_classify(int codec, const uint8_t *nal, uint32_t bytes, int *key, int *param);

#ifdef __cplusplus
}
#endif
#endif /* N2HWENC_ANNEXB_H */
