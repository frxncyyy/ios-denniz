/* SPDX-License-Identifier: MIT */
/*
 * annexb.c -- Annex B scanning, Annex B -> AVCC conversion and the avcC/hvcC records.  See n2hwenc_annexb.h for the
 * specification references.  Original code; no third-party source.
 */
#include "n2hwenc_annexb.h"

#include <string.h>

/* ------------------------------------------------------------------ NAL classification */

int n2hwenc_nal_classify(int codec, const uint8_t *nal, uint32_t bytes, int *key, int *param)
{
	int type;

	if (nal == NULL || bytes == 0)
		return -1;
	if (codec == N2HWENC_AVC) {
		type = nal[0] & 0x1f;
		if (key != NULL)
			*key = (type == 5);                     /* IDR */
		if (param != NULL)
			*param = (type == 7 || type == 8);      /* SPS, PPS */
	} else if (codec == N2HWENC_HEVC) {
		if (bytes < 2)
			return -1;                              /* the HEVC NAL header is two bytes */
		type = (nal[0] >> 1) & 0x3f;
		if (key != NULL)
			*key = (type >= 16 && type <= 23);      /* BLA_W_LP..CRA: every IRAP picture */
		if (param != NULL)
			*param = (type >= 32 && type <= 34);    /* VPS, SPS, PPS */
	} else {
		return -1;
	}
	return type;
}

/* ------------------------------------------------------------------ scan */

uint32_t n2hwenc_annexb_expanded(uint32_t bytes)
{
	/* Worst case every three bytes are a start code: each byte grows by one third, plus the four-byte lengths. */
	uint64_t bound = (uint64_t)bytes + (uint64_t)(bytes / 3u) + 1u + 16u;

	if (bound > 0xffffffffu)
		return 0xffffffffu;
	return (uint32_t)bound;
}

int n2hwenc_annexb_scan(const uint8_t *buf, uint32_t bytes, int codec,
                        struct n2hwenc_nal *nals, uint32_t capacity, uint32_t *count)
{
	uint32_t i = 0, n = 0;
	uint32_t payload = 0;
	int have = 0;

	if (count != NULL)
		*count = 0;
	if (buf == NULL || capacity == 0)
		return -1;

	while (i + 3u <= bytes) {
		if (buf[i] == 0 && buf[i + 1u] == 0 && buf[i + 2u] == 1) {
			if (have) {
				uint32_t end = i;
				uint32_t start = payload;

				while (end > start && buf[end - 1u] == 0)
					end--;                  /* trailing_zero_8bits / the extra zero of a four-byte code */
				if (end == start)
					return -1;              /* start code with no NAL unit */
				if (n == capacity)
					return -1;              /* more NAL units than the caller can describe */
				{
					int key = 0, param = 0;
					int type = n2hwenc_nal_classify(codec, buf + start, end - start, &key, &param);
					if (type < 0)
						return -1;
					nals[n].offset = start;
					nals[n].size = end - start;
					nals[n].type = (uint8_t)type;
					nals[n].key = (uint8_t)(key != 0);
					nals[n].param = (uint8_t)(param != 0);
					nals[n].reserved = 0;
				}
				n++;
			}
			payload = i + 3u;
			have = 1;
			i = payload;
		} else {
			i++;
		}
	}
	if (!have)
		return -1;                              /* not an Annex B buffer */
	{
		uint32_t end = bytes;
		uint32_t start = payload;
		int key = 0, param = 0;
		int type;

		while (end > start && buf[end - 1u] == 0)
			end--;
		if (end == start)
			return -1;
		if (n == capacity)
			return -1;
		type = n2hwenc_nal_classify(codec, buf + start, end - start, &key, &param);
		if (type < 0)
			return -1;
		nals[n].offset = start;
		nals[n].size = end - start;
		nals[n].type = (uint8_t)type;
		nals[n].key = (uint8_t)(key != 0);
		nals[n].param = (uint8_t)(param != 0);
		nals[n].reserved = 0;
		n++;
	}
	if (count != NULL)
		*count = n;
	return 0;
}

/* ------------------------------------------------------------------ Annex B -> AVCC */

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

int n2hwenc_annexb_to_avcc(uint8_t *buf, uint32_t bytes, uint32_t capacity, int codec, int strip_param,
                           uint32_t *out_bytes, uint32_t *out_nals)
{
	struct n2hwenc_nal nals[N2HWENC_NAL_MAX];
	uint32_t n = 0, i, kept = 0;
	uint64_t need = 0, dst_end;

	if (out_bytes != NULL)
		*out_bytes = 0;
	if (out_nals != NULL)
		*out_nals = 0;
	if (n2hwenc_annexb_scan(buf, bytes, codec, nals, N2HWENC_NAL_MAX, &n) != 0)
		return -1;
	for (i = 0; i < n; i++) {
		if (strip_param && nals[i].param)
			continue;
		need += 4u + nals[i].size;
		kept++;
	}
	if (kept == 0 || need > capacity || need > 0xffffffffu)
		return -2;                              /* decided before anything is written: the buffer is untouched */

	/*
	 * Stage 1, parameter sets (only when strip_param): remove each parameter-set NAL unit together with its three-byte
	 * start code by shifting the tail of the buffer left.  Walking backwards keeps the offsets of the earlier entries
	 * valid, and each shift is a memmove with destination < source, so nothing unread can be overwritten.
	 */
	if (strip_param) {
		for (i = n; i-- > 0; ) {
			uint32_t code_start, code_end;

			if (!nals[i].param)
				continue;
			code_start = nals[i].offset - 3u;    /* the scan puts the payload right after "00 00 01" */
			code_end = (i + 1u < n) ? nals[i + 1u].offset - 3u : bytes;
			if (code_end > bytes || code_start >= code_end)
				return -1;
			memmove(buf + code_start, buf + code_end, bytes - code_end);
			bytes -= code_end - code_start;
		}
		if (n2hwenc_annexb_scan(buf, bytes, codec, nals, N2HWENC_NAL_MAX, &n) != 0 || n != kept)
			return -1;
	}

	/*
	 * Stage 2, start codes to four-byte lengths.  Every start code is at most four bytes, so each length is at least as
	 * long as the start code it replaces: the output never shrinks, its byte count grows by (4 - code length) per NAL
	 * unit.  Walking backwards therefore always writes at or to the right of the source it is reading.
	 */
	dst_end = need;
	for (i = n; i-- > 0; ) {
		dst_end -= nals[i].size;
		memmove(buf + (uint32_t)dst_end, buf + nals[i].offset, nals[i].size);
		dst_end -= 4u;
		put_be32(buf + (uint32_t)dst_end, nals[i].size);
	}
	if (dst_end != 0)
		return -1;                              /* cannot happen; refuse to return a half-written sample */
	if (out_bytes != NULL)
		*out_bytes = (uint32_t)need;
	if (out_nals != NULL)
		*out_nals = kept;
	return 0;
}

/* ------------------------------------------------------------------ bit reader (RBSP) */

struct bit_reader {
	const uint8_t *data;
	uint32_t       bits;
	uint32_t       pos;
	int            error;
};

static uint32_t br_bits(struct bit_reader *br, int count)
{
	uint32_t value = 0;
	int i;

	if (count < 0 || count > 32 || br->error) {
		br->error = 1;
		return 0;
	}
	if ((uint64_t)br->pos + (uint32_t)count > br->bits) {
		br->error = 1;
		return 0;
	}
	for (i = 0; i < count; i++) {
		uint32_t byte = br->data[(br->pos + (uint32_t)i) >> 3];
		uint32_t bit = (byte >> (7u - ((br->pos + (uint32_t)i) & 7u))) & 1u;
		value = (value << 1) | bit;
	}
	br->pos += (uint32_t)count;
	return value;
}

static void br_skip(struct bit_reader *br, uint32_t count)
{
	if ((uint64_t)br->pos + count > br->bits) {
		br->error = 1;
		return;
	}
	br->pos += count;
}

static uint32_t br_ue(struct bit_reader *br)
{
	uint32_t zeros = 0;

	while (!br->error && zeros < 32 && br_bits(br, 1) == 0)
		zeros++;
	if (br->error || zeros >= 32) {
		br->error = 1;
		return 0;
	}
	if (zeros == 0)
		return 0;
	return ((1u << zeros) - 1u) + br_bits(br, (int)zeros);
}

/* Copy an EBSP (NAL unit payload with emulation prevention bytes) to an RBSP. */
static int ebsp_to_rbsp(const uint8_t *src, uint32_t bytes, uint8_t *dst, uint32_t capacity, uint32_t *out)
{
	uint32_t i, o = 0;

	if (bytes > capacity)
		return -1;
	for (i = 0; i < bytes; i++) {
		if (i + 2u < bytes && src[i] == 0 && src[i + 1u] == 0 && src[i + 2u] == 3) {
			dst[o++] = 0;
			dst[o++] = 0;
			i += 2;                          /* skip 00 00 03: the 03 is the emulation prevention byte */
		} else {
			dst[o++] = src[i];
		}
	}
	*out = o;
	return 0;
}

#define N2HWENC_RBSP_MAX 4096u

/* ------------------------------------------------------------------ avcC */

struct avc_sps_info {
	uint8_t  profile_idc;
	uint8_t  profile_compatibility;
	uint8_t  level_idc;
	uint32_t chroma_format_idc;
	uint32_t bit_depth_luma_minus8;
	uint32_t bit_depth_chroma_minus8;
};

static int avc_parse_sps(const uint8_t *nal, uint32_t bytes, struct avc_sps_info *info)
{
	uint8_t rbsp[N2HWENC_RBSP_MAX];
	uint32_t rbsp_bytes = 0;
	struct bit_reader br;

	if (nal == NULL || bytes < 4 || bytes > N2HWENC_RBSP_MAX)
		return -1;
	if (ebsp_to_rbsp(nal + 1, bytes - 1, rbsp, sizeof(rbsp), &rbsp_bytes) != 0 || rbsp_bytes < 3)
		return -1;
	info->profile_idc = rbsp[0];
	info->profile_compatibility = rbsp[1];
	info->level_idc = rbsp[2];
	info->chroma_format_idc = 1;                /* 4:2:0, the value the avcC extension must carry for 4:2:0 when absent */
	info->bit_depth_luma_minus8 = 0;
	info->bit_depth_chroma_minus8 = 0;

	if (info->profile_idc == 100 || info->profile_idc == 110 || info->profile_idc == 122 ||
	    info->profile_idc == 144) {
		/* The three fields avcC repeats for these profiles (ISO/IEC 14496-15 5.2.4.1.1).  sps_seq_parameter_set_id
		 * comes before them (ITU-T H.264 7.3.2.1.1). */
		br.data = rbsp + 3;
		br.bits = (rbsp_bytes - 3u) * 8u;
		br.pos = 0;
		br.error = 0;
		(void)br_ue(&br);                       /* seq_parameter_set_id */
		info->chroma_format_idc = br_ue(&br);
		if (info->chroma_format_idc == 3)
			(void)br_bits(&br, 1);          /* separate_colour_plane_flag */
		info->bit_depth_luma_minus8 = br_ue(&br);
		info->bit_depth_chroma_minus8 = br_ue(&br);
		if (br.error || info->chroma_format_idc > 3 || info->bit_depth_luma_minus8 > 7 ||
		    info->bit_depth_chroma_minus8 > 7)
			return -1;
	}
	return 0;
}

int n2hwenc_avcc_build(const struct n2hwenc_ps *sets, uint32_t count, uint8_t *out, uint32_t capacity)
{
	struct avc_sps_info sps;
	uint32_t i, nsps = 0, npps = 0, at;

	if (sets == NULL || out == NULL || capacity < 7)
		return -1;
	for (i = 0; i < count; i++) {
		if (sets[i].type == 7)
			nsps++;
		else if (sets[i].type == 8)
			npps++;
	}
	if (nsps == 0 || npps == 0 || nsps > 31 || npps > 255)
		return -1;
	/* Parse the first SPS in the list. */
	for (i = 0; i < count; i++) {
		if (sets[i].type == 7) {
			if (avc_parse_sps(sets[i].data, sets[i].bytes, &sps) != 0)
				return -1;
			break;
		}
	}

	/* Header: 7 bytes; each SPS/PPS costs 2 + n. */
	{
		uint64_t need = 7u;
		for (i = 0; i < count; i++) {
			if (sets[i].type == 7 || sets[i].type == 8) {
				need += 2u + sets[i].bytes;
				if (sets[i].bytes > 0xffffu)
					return -1;
			}
		}
		if ((sps.profile_idc == 100 || sps.profile_idc == 110 || sps.profile_idc == 122 ||
		     sps.profile_idc == 144))
			need += 4u;
		if (need > capacity)
			return -1;
	}

	at = 0;
	out[at++] = 1;                              /* configurationVersion */
	out[at++] = sps.profile_idc;                /* AVCProfileIndication */
	out[at++] = sps.profile_compatibility;      /* profile_compatibility */
	out[at++] = sps.level_idc;                  /* AVCLevelIndication */
	out[at++] = 0xff;                           /* 6 bits reserved + lengthSizeMinusOne = 3 */
	out[at++] = (uint8_t)(0xe0u | nsps);        /* 3 bits reserved + numOfSequenceParameterSets */
	for (i = 0; i < count; i++) {
		if (sets[i].type != 7)
			continue;
		out[at++] = (uint8_t)(sets[i].bytes >> 8);
		out[at++] = (uint8_t)sets[i].bytes;
		memcpy(out + at, sets[i].data, sets[i].bytes);
		at += sets[i].bytes;
	}
	out[at++] = (uint8_t)npps;
	for (i = 0; i < count; i++) {
		if (sets[i].type != 8)
			continue;
		out[at++] = (uint8_t)(sets[i].bytes >> 8);
		out[at++] = (uint8_t)sets[i].bytes;
		memcpy(out + at, sets[i].data, sets[i].bytes);
		at += sets[i].bytes;
	}
	if (sps.profile_idc == 100 || sps.profile_idc == 110 || sps.profile_idc == 122 ||
	    sps.profile_idc == 144) {
		out[at++] = (uint8_t)(0xfcu | (sps.chroma_format_idc & 3u));
		out[at++] = (uint8_t)(0xf8u | (sps.bit_depth_luma_minus8 & 7u));
		out[at++] = (uint8_t)(0xf8u | (sps.bit_depth_chroma_minus8 & 7u));
		out[at++] = 0;                      /* numOfSequenceParameterSetExt */
	}
	return (int)at;
}

/* ------------------------------------------------------------------ hvcC */

struct hevc_sps_info {
	uint32_t profile_space, tier, profile_idc;
	uint32_t compatibility;                     /* 32 bits */
	uint64_t constraint;                        /* 48 bits */
	uint32_t level_idc;
	uint32_t max_sub_layers_minus1;
	uint32_t temporal_id_nesting;
	uint32_t chroma_format_idc;
	uint32_t bit_depth_luma_minus8;
	uint32_t bit_depth_chroma_minus8;
};

static int hevc_parse_sps(const uint8_t *nal, uint32_t bytes, struct hevc_sps_info *info)
{
	uint8_t rbsp[N2HWENC_RBSP_MAX];
	uint32_t rbsp_bytes = 0, i;
	struct bit_reader br;
	uint32_t sub_profile[8], sub_level[8];

	if (nal == NULL || bytes < 7 || bytes > N2HWENC_RBSP_MAX)
		return -1;
	/* The NAL header is 2 bytes; the RBSP starts after it. */
	if (ebsp_to_rbsp(nal + 2, bytes - 2, rbsp, sizeof(rbsp), &rbsp_bytes) != 0 || rbsp_bytes < 5)
		return -1;
	br.data = rbsp;
	br.bits = rbsp_bytes * 8u;
	br.pos = 0;
	br.error = 0;

	(void)br_bits(&br, 4);                      /* sps_video_parameter_set_id */
	info->max_sub_layers_minus1 = br_bits(&br, 3);
	info->temporal_id_nesting = br_bits(&br, 1);
	if (info->max_sub_layers_minus1 > 6)
		return -1;

	/* profile_tier_level(1, sps_max_sub_layers_minus1), ITU-T H.265 7.3.3 */
	info->profile_space = br_bits(&br, 2);
	info->tier = br_bits(&br, 1);
	info->profile_idc = br_bits(&br, 5);
	info->compatibility = br_bits(&br, 32);
	info->constraint = ((uint64_t)br_bits(&br, 16) << 32);
	info->constraint |= br_bits(&br, 32);
	info->level_idc = br_bits(&br, 8);

	for (i = 0; i < info->max_sub_layers_minus1; i++) {
		sub_profile[i] = br_bits(&br, 1);
		sub_level[i] = br_bits(&br, 1);
	}
	if (info->max_sub_layers_minus1 > 0) {
		for (i = info->max_sub_layers_minus1; i < 8; i++)
			br_skip(&br, 2);                /* reserved_zero_2bits */
	}
	for (i = 0; i < info->max_sub_layers_minus1; i++) {
		if (sub_profile[i])
			br_skip(&br, 88);               /* the sub-layer profile fields */
		if (sub_level[i])
			br_skip(&br, 8);
	}
	if (br.error)
		return -1;

	(void)br_ue(&br);                            /* sps_seq_parameter_set_id */
	info->chroma_format_idc = br_ue(&br);
	if (info->chroma_format_idc == 3)
		(void)br_bits(&br, 1);                  /* separate_colour_plane_flag */
	(void)br_ue(&br);                            /* pic_width_in_luma_samples */
	(void)br_ue(&br);                            /* pic_height_in_luma_samples */
	if (br_bits(&br, 1)) {                       /* conformance_window_flag */
		(void)br_ue(&br);
		(void)br_ue(&br);
		(void)br_ue(&br);
		(void)br_ue(&br);
	}
	info->bit_depth_luma_minus8 = br_ue(&br);
	info->bit_depth_chroma_minus8 = br_ue(&br);
	if (br.error || info->chroma_format_idc > 3 || info->bit_depth_luma_minus8 > 7 ||
	    info->bit_depth_chroma_minus8 > 7)
		return -1;
	return 0;
}

int n2hwenc_hvcc_build(const struct n2hwenc_ps *sets, uint32_t count, uint8_t *out, uint32_t capacity)
{
	struct hevc_sps_info sps;
	uint32_t i, at = 0;
	uint32_t arrays = 0, nvps = 0, nsps = 0, npps = 0;
	uint64_t need;
	int have_sps = 0;

	if (sets == NULL || out == NULL || capacity < 23)
		return -1;
	for (i = 0; i < count; i++) {
		switch (sets[i].type) {
		case 32: nvps++; break;
		case 33: nsps++; break;
		case 34: npps++; break;
		default: break;
		}
	}
	if (nsps == 0)
		return -1;
	for (i = 0; i < count; i++) {
		if (sets[i].type == 33) {
			if (hevc_parse_sps(sets[i].data, sets[i].bytes, &sps) != 0)
				return -1;
			have_sps = 1;
			break;
		}
	}
	if (!have_sps)
		return -1;
	if (nvps > 0) arrays++;
	if (nsps > 0) arrays++;
	if (npps > 0) arrays++;
	if (arrays == 0 || arrays > 255)
		return -1;

	need = 23u;
	for (i = 0; i < count; i++) {
		if (sets[i].type < 32 || sets[i].type > 34)
			continue;
		if (sets[i].bytes > 0xffffu)
			return -1;
		need += 3u + 2u + sets[i].bytes;        /* array header + numNalus + length */
	}
	if (need > capacity)
		return -1;

	out[at++] = 1;                              /* configurationVersion */
	out[at++] = (uint8_t)((sps.profile_space << 6) | (sps.tier << 5) | sps.profile_idc);
	out[at++] = (uint8_t)(sps.compatibility >> 24);
	out[at++] = (uint8_t)(sps.compatibility >> 16);
	out[at++] = (uint8_t)(sps.compatibility >> 8);
	out[at++] = (uint8_t)sps.compatibility;
	out[at++] = (uint8_t)(sps.constraint >> 40);
	out[at++] = (uint8_t)(sps.constraint >> 32);
	out[at++] = (uint8_t)(sps.constraint >> 24);
	out[at++] = (uint8_t)(sps.constraint >> 16);
	out[at++] = (uint8_t)(sps.constraint >> 8);
	out[at++] = (uint8_t)sps.constraint;
	out[at++] = (uint8_t)sps.level_idc;
	out[at++] = 0xf0;                           /* reserved 1111 + min_spatial_segmentation_idc (12) high nibble = 0 */
	out[at++] = 0x00;
	out[at++] = 0xfc;                           /* reserved 111111 + parallelismType 0 */
	out[at++] = (uint8_t)(0xfcu | (sps.chroma_format_idc & 3u));   /* reserved 111111 + chromaFormat */
	out[at++] = (uint8_t)(0xf8u | (sps.bit_depth_luma_minus8 & 7u));
	out[at++] = (uint8_t)(0xf8u | (sps.bit_depth_chroma_minus8 & 7u));
	out[at++] = 0;                              /* avgFrameRate */
	out[at++] = 0;
	out[at++] = (uint8_t)(((sps.max_sub_layers_minus1 + 1u) << 3) |
	                      ((sps.temporal_id_nesting & 1u) << 2) | 3u);
	out[at++] = (uint8_t)arrays;

	for (i = 32; i <= 34; i++) {
		uint32_t j, n = 0;

		for (j = 0; j < count; j++)
			if (sets[j].type == (uint8_t)i)
				n++;
		if (n == 0)
			continue;
		out[at++] = (uint8_t)i;             /* array_completeness 0, reserved 0, NAL_unit_type */
		out[at++] = (uint8_t)(n >> 8);
		out[at++] = (uint8_t)n;
		for (j = 0; j < count; j++) {
			if (sets[j].type != (uint8_t)i)
				continue;
			out[at++] = (uint8_t)(sets[j].bytes >> 8);
			out[at++] = (uint8_t)sets[j].bytes;
			memcpy(out + at, sets[j].data, sets[j].bytes);
			at += sets[j].bytes;
		}
	}
	return (int)at;
}
