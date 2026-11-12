/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_nal.c -- avcC/hvcC parsing, SPS analysis and length-prefixed <-> Annex B conversion.
 *
 * Sources: ISO/IEC 14496-15 5.2.4.1.1 (AVCDecoderConfigurationRecord) and 8.3.3.1.1 (HEVCDecoderConfigurationRecord),
 * ITU-T H.264 7.3.2.1.1/7.3.2.1.1.1/E.1.1 (SPS, scaling lists, VUI) and H.265 7.3.2.2.1/7.3.2.2.2/7.3.2.2.3/E.2.1 (SPS,
 * short-term reference picture sets, scaling lists, VUI).  Emulation-prevention removal per H.264 7.4.1.  No third-party
 * code; the numbers in the tests are spec literals or ffprobe output.
 */
#include "n2vtd_nal.h"
#include "n2vtd_apple.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------------------------ */
/* MSB-first bit reader over a de-emulated RBSP                                                                       */
/* ------------------------------------------------------------------------------------------------------------------ */

struct n2vtd_br {
	const uint8_t *buf;
	size_t bits;
	size_t pos;
	int error;
};

static void br_init(struct n2vtd_br *b, const uint8_t *buf, size_t bits)
{
	b->buf = buf;
	b->bits = bits;
	b->pos = 0;
	b->error = 0;
}

static uint32_t br_bit(struct n2vtd_br *b)
{
	uint32_t v;

	if (b->pos >= b->bits) {
		b->error = 1;
		return 0;
	}
	v = (uint32_t)((b->buf[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u);
	b->pos++;
	return v;
}

static uint32_t br_u(struct n2vtd_br *b, unsigned n)
{
	uint32_t v = 0;
	unsigned i;

	if (n > 32 || b->pos + n > b->bits) {
		b->error = 1;
		b->pos = b->bits;
		return 0;
	}
	for (i = 0; i < n; i++)
		v = (v << 1) | br_bit(b);
	return v;
}

static uint32_t br_ue(struct n2vtd_br *b)
{
	unsigned zeros = 0;
	uint32_t v;

	while (!b->error && br_bit(b) == 0) {
		if (++zeros > 31) {
			b->error = 1;
			return 0;
		}
	}
	if (b->error)
		return 0;
	if (zeros == 0)
		return 0;
	v = br_u(b, zeros);
	if (b->error)
		return 0;
	if (v >= (1u << zeros))
		return (1u << zeros) - 1;      /* cannot overflow the prefix by construction; defensive */
	return (uint32_t)(((uint64_t)1 << zeros) - 1 + v);
}

static int32_t br_se(struct n2vtd_br *b)
{
	uint32_t k = br_ue(b);

	if (b->error)
		return 0;
	if (k & 1u)
		return (int32_t)((k + 1u) / 2u);
	return -(int32_t)(k / 2u);
}

static void br_skip(struct n2vtd_br *b, size_t n)
{
	if (b->pos + n > b->bits) {
		b->error = 1;
		b->pos = b->bits;
		return;
	}
	b->pos += n;
}

/* Remove emulation-prevention bytes (00 00 03 -> 00 00).  Returns the RBSP length, or 0 when it does not fit. */
static size_t rbsp_extract(const uint8_t *src, size_t bytes, uint8_t *dst, size_t cap)
{
	size_t i, o = 0;
	unsigned zeros = 0;

	for (i = 0; i < bytes; i++) {
		uint8_t c = src[i];

		if (zeros >= 2 && c == 0x03) {
			zeros = 0;
			continue;                         /* drop the emulation-prevention byte */
		}
		if (o >= cap)
			return 0;
		dst[o++] = c;
		zeros = (c == 0) ? zeros + 1 : 0;
	}
	return o;
}

static int params_append(struct n2vtd_param *list, int *count, int max, const uint8_t *data, size_t size)
{
	if (*count >= max || size == 0)
		return N2VTD_ENAL_SIZE;
	list[*count].data = data;
	list[*count].size = size;
	(*count)++;
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* avcC                                                                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

int n2vtd_avcc_parse(const uint8_t *rec, size_t bytes, struct n2vtd_params *out)
{
	size_t i = 0;
	unsigned num_sps, num_pps, k;

	if (!rec || !out)
		return N2VTD_ENAL_SIZE;
	memset(out, 0, sizeof(*out));
	out->codec = N2VTD_CODEC_AVC1;
	out->record = rec;
	out->record_size = bytes;
	if (bytes < 7)
		return N2VTD_ENAL_FORMAT;
	if (rec[0] != 1)                          /* configurationVersion */
		return N2VTD_ENAL_FORMAT;
	out->length_size = (uint32_t)(rec[4] & 0x03u) + 1u;
	if (out->length_size == 3)                /* 2 bits cannot express 3 in 14496-15; value 2 is reserved */
		return N2VTD_ENAL_FORMAT;
	i = 5;
	num_sps = rec[i++] & 0x1fu;
	if (num_sps == 0)
		return N2VTD_ENAL_FORMAT;
	for (k = 0; k < num_sps; k++) {
		unsigned len;

		if (i + 2 > bytes)
			return N2VTD_ENAL_SIZE;
		len = ((unsigned)rec[i] << 8) | rec[i + 1];
		i += 2;
		if (len == 0 || i + len > bytes)
			return N2VTD_ENAL_SIZE;
		if (len > N2VTD_MAX_PARAM_BYTES)
			return N2VTD_ENAL_SIZE;
		if (params_append(out->sps, &out->nsps, N2VTD_MAX_SPS, rec + i, len) != 0)
			return N2VTD_ENAL_SIZE;
		i += len;
	}
	if (i + 1 > bytes)
		return N2VTD_ENAL_SIZE;
	num_pps = rec[i++];
	for (k = 0; k < num_pps; k++) {
		unsigned len;

		if (i + 2 > bytes)
			return N2VTD_ENAL_SIZE;
		len = ((unsigned)rec[i] << 8) | rec[i + 1];
		i += 2;
		if (len == 0 || i + len > bytes)
			return N2VTD_ENAL_SIZE;
		if (len > N2VTD_MAX_PARAM_BYTES)
			return N2VTD_ENAL_SIZE;
		if (params_append(out->pps, &out->npps, N2VTD_MAX_PPS, rec + i, len) != 0)
			return N2VTD_ENAL_SIZE;
		i += len;
	}
	/* The High-profile extension may follow; it is not needed for decoding and is not required to be present. */
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* hvcC                                                                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

int n2vtd_hvcc_parse(const uint8_t *rec, size_t bytes, struct n2vtd_params *out)
{
	size_t i;
	unsigned num_arrays, a;

	if (!rec || !out)
		return N2VTD_ENAL_SIZE;
	memset(out, 0, sizeof(*out));
	out->codec = N2VTD_CODEC_HVC1;
	out->record = rec;
	out->record_size = bytes;
	if (bytes < 23)
		return N2VTD_ENAL_FORMAT;
	if (rec[0] != 1)                          /* configurationVersion */
		return N2VTD_ENAL_FORMAT;
	/* general_profile_space/tier/idc [1], compatibility [2..5], constraints [6..11], level [12],
	 * min_spatial_segmentation [13..14], parallelism [15], chroma [16], bit depths [17..18], avgFrameRate [19..20],
	 * constantFrameRate/numTemporalLayers/temporalIdNested/lengthSizeMinusOne [21], numOfArrays [22]. */
	out->length_size = (uint32_t)(rec[21] & 0x03u) + 1u;
	if (out->length_size == 3)
		return N2VTD_ENAL_FORMAT;
	num_arrays = rec[22];
	i = 23;
	for (a = 0; a < num_arrays; a++) {
		unsigned type, num_nalus, n;

		if (i + 3 > bytes)
			return N2VTD_ENAL_SIZE;
		type = rec[i] & 0x3fu;
		num_nalus = ((unsigned)rec[i + 1] << 8) | rec[i + 2];
		i += 3;
		for (n = 0; n < num_nalus; n++) {
			unsigned len;

			if (i + 2 > bytes)
				return N2VTD_ENAL_SIZE;
			len = ((unsigned)rec[i] << 8) | rec[i + 1];
			i += 2;
			if (len == 0 || i + len > bytes)
				return N2VTD_ENAL_SIZE;
			if (len > N2VTD_MAX_PARAM_BYTES)
				return N2VTD_ENAL_SIZE;
			switch (type) {
			case 32:
				if (params_append(out->vps, &out->nvps, N2VTD_MAX_VPS, rec + i, len) != 0)
					return N2VTD_ENAL_SIZE;
				break;
			case 33:
				if (params_append(out->sps, &out->nsps, N2VTD_MAX_SPS, rec + i, len) != 0)
					return N2VTD_ENAL_SIZE;
				break;
			case 34:
				if (params_append(out->pps, &out->npps, N2VTD_MAX_PPS, rec + i, len) != 0)
					return N2VTD_ENAL_SIZE;
				break;
			default:
				/* SEI and other arrays are legal in hvcC; ignore them. */
				break;
			}
			i += len;
		}
	}
	if (out->nsps == 0 || out->npps == 0)
		return N2VTD_ENAL_FORMAT;
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* From a CMFormatDescription                                                                                         */
/* ------------------------------------------------------------------------------------------------------------------ */

int n2vtd_params_from_format(CMFormatDescriptionRef fmt, uint32_t codec, struct n2vtd_params *out)
{
	CFTypeRef atomsValue;
	CFDictionaryRef atoms;
	CFDataRef data;
	const uint8_t *bytes;
	CFIndex length;

	if (!fmt || !out)
		return N2VTD_ENAL_ABSENT;
	if (codec != N2VTD_CODEC_AVC1 && codec != N2VTD_CODEC_HVC1)
		return N2VTD_ENAL_UNSUP;
	if (kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms == NULL)
		return N2VTD_ENAL_ABSENT;
	atomsValue = CMFormatDescriptionGetExtension(fmt, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
	if (!atomsValue || CFGetTypeID(atomsValue) != CFDictionaryGetTypeID())
		return N2VTD_ENAL_ABSENT;
	atoms = (CFDictionaryRef)atomsValue;
	/* N2STR is a literal-only macro on iOS: two calls, not one with a variable. */
	if (codec == N2VTD_CODEC_AVC1)
		data = (CFDataRef)CFDictionaryGetValue(atoms, N2STR("avcC"));
	else
		data = (CFDataRef)CFDictionaryGetValue(atoms, N2STR("hvcC"));
	if (!data || CFGetTypeID((CFTypeRef)data) != CFDataGetTypeID())
		return N2VTD_ENAL_ABSENT;
	bytes = CFDataGetBytePtr(data);
	length = CFDataGetLength(data);
	if (!bytes || length <= 0)
		return N2VTD_ENAL_ABSENT;
	return (codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(bytes, (size_t)length, out)
	                                   : n2vtd_hvcc_parse(bytes, (size_t)length, out);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Parameter sets -> Annex B                                                                                          */
/* ------------------------------------------------------------------------------------------------------------------ */

/* Copy the parameter sets out of a CMVideoFormatDescription via CoreMedia's accessors (no atoms present). */
static int params_load_accessors(CMFormatDescriptionRef fmt, uint32_t codec, uint8_t *blob, size_t blob_cap,
                                 struct n2vtd_params *out)
{
	size_t count = 0, i, used = 0;
	int header_len = 4;
	int nal_bytes;

	memset(out, 0, sizeof(*out));
	out->codec = codec;
	if (codec == N2VTD_CODEC_AVC1) {
		if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, NULL, NULL, &count, &header_len) != 0)
			return N2VTD_ENAL_ABSENT;
	} else if (codec == N2VTD_CODEC_HVC1) {
		if (CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 0, NULL, NULL, &count, &header_len) != 0)
			return N2VTD_ENAL_ABSENT;
	} else {
		return N2VTD_ENAL_UNSUP;
	}
	if (count == 0 || count > 64 || header_len < 1 || header_len > 4)
		return N2VTD_ENAL_FORMAT;
	out->length_size = (uint32_t)header_len;
	for (i = 0; i < count; i++) {
		const uint8_t *ps = NULL;
		size_t size = 0;
		unsigned type;

		nal_bytes = (codec == N2VTD_CODEC_AVC1)
		                    ? CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, i, &ps, &size, NULL, NULL)
		                    : CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, i, &ps, &size, NULL, NULL);
		if (nal_bytes != 0 || !ps || size == 0 || size > N2VTD_MAX_PARAM_BYTES)
			return N2VTD_ENAL_FORMAT;
		if (used + size > blob_cap)
			return N2VTD_ENAL_SIZE;
		memcpy(blob + used, ps, size);
		type = (codec == N2VTD_CODEC_AVC1) ? (ps[0] & 0x1fu) : ((ps[0] >> 1) & 0x3fu);
		if (codec == N2VTD_CODEC_AVC1) {
			if (type == 7) {
				if (params_append(out->sps, &out->nsps, N2VTD_MAX_SPS, blob + used, size) != 0)
					return N2VTD_ENAL_SIZE;
			} else if (type == 8) {
				if (params_append(out->pps, &out->npps, N2VTD_MAX_PPS, blob + used, size) != 0)
					return N2VTD_ENAL_SIZE;
			}
		} else if (type == 32 || type == 33 || type == 34) {
			struct n2vtd_param *list = (type == 32) ? out->vps : (type == 33 ? out->sps : out->pps);
			int *n = (type == 32) ? &out->nvps : (type == 33 ? &out->nsps : &out->npps);
			int max = (type == 32) ? N2VTD_MAX_VPS : (type == 33 ? N2VTD_MAX_SPS : N2VTD_MAX_PPS);
			if (params_append(list, n, max, blob + used, size) != 0)
				return N2VTD_ENAL_SIZE;
		}
		used += size;
	}
	if (out->nsps == 0 || out->npps == 0)
		return N2VTD_ENAL_FORMAT;
	return 0;
}

int n2vtd_params_load(CMFormatDescriptionRef fmt, uint32_t codec, uint8_t *blob, size_t blob_cap,
                      struct n2vtd_params *out)
{
	int rc = n2vtd_params_from_format(fmt, codec, out);

	if (rc == 0)
		return 0;
	if (!blob || !out)
		return rc;
	/* A fresh parse: the caller's blob may hold the previous session's bytes. */
	return params_load_accessors(fmt, codec, blob, blob_cap, out);
}

static int annexb_put(uint8_t *dst, size_t cap, size_t *at, const struct n2vtd_param *p)
{
	if (*at + 4 + p->size > cap)
		return N2VTD_ENAL_SIZE;
	dst[(*at)++] = 0;
	dst[(*at)++] = 0;
	dst[(*at)++] = 0;
	dst[(*at)++] = 1;
	memcpy(dst + *at, p->data, p->size);
	*at += p->size;
	return 0;
}

int n2vtd_params_to_annexb(const struct n2vtd_params *p, uint8_t *dst, size_t cap)
{
	size_t at = 0;
	int i;

	if (!p || !dst)
		return N2VTD_ENAL_SIZE;
	for (i = 0; i < p->nvps; i++)
		if (annexb_put(dst, cap, &at, &p->vps[i]) != 0)
			return N2VTD_ENAL_SIZE;
	for (i = 0; i < p->nsps; i++)
		if (annexb_put(dst, cap, &at, &p->sps[i]) != 0)
			return N2VTD_ENAL_SIZE;
	for (i = 0; i < p->npps; i++)
		if (annexb_put(dst, cap, &at, &p->pps[i]) != 0)
			return N2VTD_ENAL_SIZE;
	return (int)at;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* SPS: H.264                                                                                                         */
/* ------------------------------------------------------------------------------------------------------------------ */

static int avc_scaling_list(struct n2vtd_br *b, unsigned size)
{
	unsigned last = 8, next = 8, j;

	for (j = 0; j < size; j++) {
		if (next != 0) {
			int32_t delta = br_se(b);
			if (b->error)
				return N2VTD_ENAL_SPS;
			next = (unsigned)((last + delta + 256) % 256);
		}
		last = (next == 0) ? last : next;
	}
	return 0;
}

static int avc_vui(struct n2vtd_br *b, struct n2vtd_sps_info *out)
{
	if (br_bit(b)) {                                  /* aspect_ratio_info_present_flag */
		uint32_t idc = br_u(b, 8);
		if (idc == 255) {
			br_u(b, 16);
			br_u(b, 16);
		}
	}
	if (br_bit(b))                                    /* overscan_info_present_flag */
		br_bit(b);                                /* overscan_appropriate_flag */
	if (br_bit(b)) {                                  /* video_signal_type_present_flag */
		br_u(b, 3);                               /* video_format */
		out->full_range = (int)br_bit(b);         /* video_full_range_flag */
		if (br_bit(b)) {                          /* colour_description_present_flag */
			br_u(b, 8);                       /* colour_primaries */
			br_u(b, 8);                       /* transfer_characteristics */
			br_u(b, 8);                       /* matrix_coefficients */
		}
	}
	/* chroma_loc_info, timing, HRD, bitstream_restriction: not needed. */
	return b->error ? N2VTD_ENAL_SPS : 0;
}

static int avc_sps_parse(const uint8_t *nal, size_t bytes, struct n2vtd_sps_info *out)
{
	uint8_t rbsp[N2VTD_MAX_PARAM_BYTES];
	size_t rbsp_bytes;
	struct n2vtd_br b;
	uint32_t profile, sps_id, chroma = 1, frame_mbs_only, crop_flag;
	uint32_t width_mbs, height_map, crop_l = 0, crop_r = 0, crop_t = 0, crop_b = 0;
	uint32_t crop_unit_x, crop_unit_y, sub_w = 2, sub_h = 2;
	int separate_colour_plane = 0;

	if (bytes < 2 || (nal[0] & 0x1fu) != 7)
		return N2VTD_ENAL_SPS;
	rbsp_bytes = rbsp_extract(nal + 1, bytes - 1, rbsp, sizeof(rbsp));
	if (rbsp_bytes < 3)
		return N2VTD_ENAL_SPS;
	br_init(&b, rbsp, rbsp_bytes * 8);
	profile = br_u(&b, 8);
	br_u(&b, 8);                                      /* constraint flags */
	br_u(&b, 8);                                      /* level_idc */
	sps_id = br_ue(&b);
	if (b.error || sps_id > 31)
		return N2VTD_ENAL_SPS;
	if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
	    profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134 ||
	    profile == 135) {
		chroma = br_ue(&b);
		if (chroma > 3)
			return N2VTD_ENAL_SPS;
		if (chroma == 3)
			separate_colour_plane = (int)br_bit(&b);
		out->bit_depth_luma = br_ue(&b) + 8;
		out->bit_depth_chroma = br_ue(&b) + 8;
		br_bit(&b);                               /* qpprime_y_zero_transform_bypass_flag */
		if (br_bit(&b)) {                         /* seq_scaling_matrix_present_flag */
			unsigned n = (chroma != 3) ? 8 : 12, k;
			for (k = 0; k < n; k++) {
				if (!br_bit(&b))
					continue;
				if (avc_scaling_list(&b, (k < 6) ? 16 : 64) != 0)
					return N2VTD_ENAL_SPS;
			}
		}
	} else {
		out->bit_depth_luma = 8;
		out->bit_depth_chroma = 8;
	}
	br_ue(&b);                                        /* log2_max_frame_num_minus4 */
	{
		uint32_t poc_type = br_ue(&b);

		if (poc_type == 0) {
			br_ue(&b);                        /* log2_max_pic_order_cnt_lsb_minus4 */
		} else if (poc_type == 1) {
			br_bit(&b);                       /* delta_pic_order_always_zero_flag */
			br_se(&b);                        /* offset_for_non_ref_pic */
			br_se(&b);                        /* offset_for_top_to_bottom_field */
			{
				uint32_t n = br_ue(&b), k;
				if (n > 255)
					return N2VTD_ENAL_SPS;
				for (k = 0; k < n; k++)
					br_se(&b);
			}
		} else if (poc_type != 2) {
			return N2VTD_ENAL_SPS;
		}
	}
	br_ue(&b);                                        /* max_num_ref_frames */
	br_bit(&b);                                       /* gaps_in_frame_num_value_allowed_flag */
	width_mbs = br_ue(&b) + 1;
	height_map = br_ue(&b) + 1;
	frame_mbs_only = br_bit(&b);
	if (!frame_mbs_only)
		br_bit(&b);                               /* mb_adaptive_frame_field_flag */
	br_bit(&b);                                       /* direct_8x8_inference_flag */
	crop_flag = br_bit(&b);
	if (crop_flag) {
		crop_l = br_ue(&b);
		crop_r = br_ue(&b);
		crop_t = br_ue(&b);
		crop_b = br_ue(&b);
	}
	out->interlaced = !frame_mbs_only;
	if (br_bit(&b))                                   /* vui_parameters_present_flag */
		if (avc_vui(&b, out) != 0)
			return N2VTD_ENAL_SPS;
	if (b.error)
		return N2VTD_ENAL_SPS;

	if (width_mbs > 512 || height_map > 512)          /* 8192 pixels; the VDEC is far smaller */
		return N2VTD_ENAL_SPS;
	out->coded_width = width_mbs * 16;
	out->coded_height = (2 - frame_mbs_only) * height_map * 16;
	out->chroma_format_idc = chroma;
	if (chroma == 0 || separate_colour_plane) {
		crop_unit_x = 1;
		crop_unit_y = 2 - frame_mbs_only;
	} else {
		if (chroma == 1) {
			sub_w = 2;
			sub_h = 2;
		} else if (chroma == 2) {
			sub_w = 2;
			sub_h = 1;
		} else {
			sub_w = 1;
			sub_h = 1;
		}
		crop_unit_x = sub_w;
		crop_unit_y = sub_h * (2 - frame_mbs_only);
	}
	if (crop_l + crop_r >= 0x1000 || crop_t + crop_b >= 0x1000)
		return N2VTD_ENAL_SPS;
	if (crop_unit_x * (crop_l + crop_r) >= out->coded_width ||
	    crop_unit_y * (crop_t + crop_b) >= out->coded_height)
		return N2VTD_ENAL_SPS;
	out->display_width = out->coded_width - crop_unit_x * (crop_l + crop_r);
	out->display_height = out->coded_height - crop_unit_y * (crop_t + crop_b);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* SPS: H.265                                                                                                         */
/* ------------------------------------------------------------------------------------------------------------------ */

static void hevc_profile_tier_level(struct n2vtd_br *b, unsigned max_sub_layers_minus1)
{
	unsigned i;
	uint8_t sub_profile[8], sub_level[8];

	br_u(b, 2);                                       /* general_profile_space */
	br_bit(b);                                        /* general_tier_flag */
	br_u(b, 5);                                       /* general_profile_idc */
	br_u(b, 32);                                      /* general_profile_compatibility_flags */
	br_skip(b, 48);                                   /* general_constraint_indicator_flags */
	br_u(b, 8);                                       /* general_level_idc */
	for (i = 0; i < max_sub_layers_minus1; i++) {
		sub_profile[i] = (uint8_t)br_bit(b);
		sub_level[i] = (uint8_t)br_bit(b);
	}
	if (max_sub_layers_minus1 > 0)
		for (i = max_sub_layers_minus1; i < 8; i++)
			br_skip(b, 2);                    /* reserved_zero_2bits */
	for (i = 0; i < max_sub_layers_minus1; i++) {
		if (sub_profile[i]) {
			br_u(b, 2);
			br_bit(b);
			br_u(b, 5);
			br_u(b, 32);
			br_skip(b, 48);
		}
		if (sub_level[i])
			br_u(b, 8);
	}
}

static int hevc_scaling_list_data(struct n2vtd_br *b)
{
	unsigned size_id, matrix_id;

	for (size_id = 0; size_id < 4; size_id++) {
		unsigned n = (size_id == 3) ? 2u : 6u;
		for (matrix_id = 0; matrix_id < n; matrix_id++) {
			if (!br_bit(b)) {                 /* scaling_list_pred_mode_flag */
				br_ue(b);                 /* scaling_list_pred_matrix_id_delta */
			} else {
				unsigned coef_num = (size_id == 0) ? 1u << 4 : (size_id == 1 ? 1u << 6 : 1u << 6);
				unsigned i;
				int next = 8;
				if (size_id > 1)
					br_se(b);         /* scaling_list_dc_coef_minus8 */
				for (i = 0; i < coef_num; i++) {
					int32_t delta = br_se(b);
					next = (next + delta + 256) % 256;
				}
			}
			if (b->error)
				return N2VTD_ENAL_SPS;
		}
	}
	return 0;
}

/* H.265 7.3.2.2.2/7.4.8: one short-term reference picture set, including the inter-set prediction. */
struct n2vtd_rps {
	int num_negative, num_positive;
	int32_t s0[32];                                   /* negative delta POCs, ascending */
	int32_t s1[32];                                   /* positive delta POCs, descending */
};

static int hevc_st_ref_pic_set(struct n2vtd_br *b, unsigned idx, unsigned num_sets, struct n2vtd_rps *sets)
{
	int inter = 0;
	int i, j;
	int delta_rps = 0;

	if (idx != 0)
		inter = (int)br_bit(b);                   /* inter_ref_pic_set_prediction_flag */
	if (b->error)
		return N2VTD_ENAL_SPS;
	if (inter) {
		int ref = (int)idx - 1;                   /* in the SPS, delta_idx_minus1 is not present */
		int num_delta;
		uint8_t used[33];
		int delta_rps_sign, abs_delta_rps_minus1;
		struct n2vtd_rps out;

		if (ref < 0 || (unsigned)ref >= num_sets)
			return N2VTD_ENAL_SPS;
		num_delta = sets[ref].num_negative + sets[ref].num_positive;
		if (num_delta >= (int)(sizeof(used) / sizeof(used[0])) || num_delta < 0)
			return N2VTD_ENAL_SPS;
		memset(used, 0, sizeof(used));
		delta_rps_sign = (int)br_bit(b);
		abs_delta_rps_minus1 = (int)br_ue(b);
		if (b->error)
			return N2VTD_ENAL_SPS;
		delta_rps = (1 - 2 * delta_rps_sign) * (abs_delta_rps_minus1 + 1);
		for (j = 0; j <= num_delta; j++) {
			used[j] = (uint8_t)br_bit(b);     /* used_by_curr_pic_flag */
			if (!used[j])
				used[j] = (uint8_t)(br_bit(b) ? 1 : 2);  /* use_delta_flag: 2 = present and set */
		}
		if (b->error)
			return N2VTD_ENAL_SPS;
		memset(&out, 0, sizeof(out));
		/* 7.4.8 derivation */
		i = 0;
		for (j = sets[ref].num_positive - 1; j >= 0; j--) {
			int32_t d = sets[ref].s1[j] + delta_rps;
			if (d < 0 && used[sets[ref].num_negative + j])
				out.s0[i++] = d;
		}
		if (delta_rps < 0 && used[num_delta])
			out.s0[i++] = delta_rps;
		for (j = 0; j < sets[ref].num_negative; j++) {
			int32_t d = sets[ref].s0[j] + delta_rps;
			if (d < 0 && used[j])
				out.s0[i++] = d;
		}
		if (i > 31)
			return N2VTD_ENAL_SPS;
		out.num_negative = i;
		i = 0;
		for (j = 0; j < sets[ref].num_negative; j++) {
			int32_t d = sets[ref].s0[j] + delta_rps;
			if (d > 0 && used[j])
				out.s1[i++] = d;
		}
		if (delta_rps > 0 && used[num_delta])
			out.s1[i++] = delta_rps;
		for (j = sets[ref].num_positive - 1; j >= 0; j--) {
			int32_t d = sets[ref].s1[j] + delta_rps;
			if (d > 0 && used[sets[ref].num_negative + j])
				out.s1[i++] = d;
		}
		if (i > 31)
			return N2VTD_ENAL_SPS;
		out.num_positive = i;
		sets[idx] = out;
		return 0;
	} else {
		int num_neg = (int)br_ue(b);
		int num_pos = (int)br_ue(b);
		if (b->error || num_neg > 31 || num_pos > 31)
			return N2VTD_ENAL_SPS;
		sets[idx].num_negative = num_neg;
		sets[idx].num_positive = num_pos;
		for (i = 0; i < num_neg; i++) {
			int32_t delta = (int32_t)br_ue(b) + 1;
			br_bit(b);                        /* used_by_curr_pic_s0_flag */
			sets[idx].s0[i] = -delta;
			if (b->error)
				return N2VTD_ENAL_SPS;
		}
		for (i = 0; i < num_pos; i++) {
			int32_t delta = (int32_t)br_ue(b) + 1;
			br_bit(b);                        /* used_by_curr_pic_s1_flag */
			sets[idx].s1[i] = delta;
			if (b->error)
				return N2VTD_ENAL_SPS;
		}
		return 0;
	}
}

static int hevc_vui(struct n2vtd_br *b, struct n2vtd_sps_info *out)
{
	if (br_bit(b)) {                                  /* aspect_ratio_info_present_flag */
		uint32_t idc = br_u(b, 8);
		if (idc == 255) {
			br_u(b, 16);
			br_u(b, 16);
		}
	}
	if (br_bit(b))                                    /* overscan_info_present_flag */
		br_bit(b);                                /* overscan_appropriate_flag */
	if (br_bit(b)) {                                  /* video_signal_type_present_flag */
		br_u(b, 3);                               /* video_format */
		out->full_range = (int)br_bit(b);         /* video_full_range_flag */
		if (br_bit(b)) {                          /* colour_description_present_flag */
			br_u(b, 8);
			br_u(b, 8);
			br_u(b, 8);
		}
	}
	if (br_bit(b)) {                                  /* chroma_loc_info_present_flag */
		br_ue(b);                                 /* chroma_sample_loc_type_top_field */
		br_ue(b);                                 /* chroma_sample_loc_type_bottom_field */
	}
	br_bit(b);                                        /* neutral_chroma_indication_flag */
	out->interlaced = (int)br_bit(b);                 /* field_seq_flag */
	br_bit(b);                                        /* frame_field_info_present_flag */
	if (br_bit(b)) {                                  /* default_display_window_flag */
		br_ue(b);
		br_ue(b);
		br_ue(b);
		br_ue(b);
	}
	return b->error ? N2VTD_ENAL_SPS : 0;
}

static int hevc_sps_parse(const uint8_t *nal, size_t bytes, struct n2vtd_sps_info *out)
{
	uint8_t rbsp[N2VTD_MAX_PARAM_BYTES];
	size_t rbsp_bytes;
	struct n2vtd_br b;
	unsigned max_sub_layers_minus1;
	uint32_t chroma, width, height, bit_luma, bit_chroma, crop_l = 0, crop_r = 0, crop_t = 0, crop_b = 0;
	uint32_t num_st_rps, i;
	struct n2vtd_rps *sets = NULL;
	int rc = N2VTD_ENAL_SPS;

	if (bytes < 3 || ((nal[0] >> 1) & 0x3fu) != 33)
		return N2VTD_ENAL_SPS;
	rbsp_bytes = rbsp_extract(nal + 2, bytes - 2, rbsp, sizeof(rbsp));
	if (rbsp_bytes < 4)
		return N2VTD_ENAL_SPS;
	br_init(&b, rbsp, rbsp_bytes * 8);
	br_u(&b, 4);                                      /* sps_video_parameter_set_id */
	max_sub_layers_minus1 = br_u(&b, 3);
	br_bit(&b);                                       /* sps_temporal_id_nesting_flag */
	if (max_sub_layers_minus1 > 6)
		return N2VTD_ENAL_SPS;
	hevc_profile_tier_level(&b, max_sub_layers_minus1);
	br_ue(&b);                                        /* sps_seq_parameter_set_id */
	chroma = br_ue(&b);
	if (chroma > 3)
		return N2VTD_ENAL_SPS;
	if (chroma == 3)
		br_bit(&b);                               /* separate_colour_plane_flag */
	width = br_ue(&b);
	height = br_ue(&b);
	if (b.error || width == 0 || height == 0 || width > 8192 || height > 8192)
		return N2VTD_ENAL_SPS;
	if (br_bit(&b)) {                                 /* conformance_window_flag */
		crop_l = br_ue(&b);
		crop_r = br_ue(&b);
		crop_t = br_ue(&b);
		crop_b = br_ue(&b);
		if (b.error)
			return N2VTD_ENAL_SPS;
	}
	bit_luma = br_ue(&b) + 8;
	bit_chroma = br_ue(&b) + 8;
	br_ue(&b);                                        /* log2_max_pic_order_cnt_lsb_minus4 */
	{
		unsigned present = br_bit(&b), start;
		start = present ? 0 : max_sub_layers_minus1;
		for (i = start; i <= max_sub_layers_minus1; i++) {
			br_ue(&b);                        /* sps_max_dec_pic_buffering_minus1 */
			br_ue(&b);                        /* sps_max_num_reorder_pics */
			br_ue(&b);                        /* sps_max_latency_increase_plus1 */
		}
	}
	br_ue(&b);                                        /* log2_min_luma_coding_block_size_minus3 */
	br_ue(&b);                                        /* log2_diff_max_min_luma_coding_block_size */
	br_ue(&b);                                        /* log2_min_luma_transform_block_size_minus2 */
	br_ue(&b);                                        /* log2_diff_max_min_luma_transform_block_size */
	br_ue(&b);                                        /* max_transform_hierarchy_depth_inter */
	br_ue(&b);                                        /* max_transform_hierarchy_depth_intra */
	if (br_bit(&b) && br_bit(&b))                     /* scaling_list_enabled_flag, sps_scaling_list_data_present_flag */
		if (hevc_scaling_list_data(&b) != 0)
			return N2VTD_ENAL_SPS;
	br_bit(&b);                                       /* amp_enabled_flag */
	br_bit(&b);                                       /* sample_adaptive_offset_enabled_flag */
	if (br_bit(&b)) {                                 /* pcm_enabled_flag */
		br_u(&b, 4);                              /* pcm_sample_bit_depth_luma_minus1 */
		br_u(&b, 4);                              /* pcm_sample_bit_depth_chroma_minus1 */
		br_ue(&b);                                /* log2_min_pcm_luma_coding_block_size_minus3 */
		br_ue(&b);                                /* log2_diff_max_min_pcm_luma_coding_block_size */
		br_bit(&b);                               /* pcm_loop_filter_disabled_flag */
	}
	num_st_rps = br_ue(&b);
	if (b.error || num_st_rps > 64)
		return N2VTD_ENAL_SPS;
	if (num_st_rps > 0) {
		sets = (struct n2vtd_rps *)calloc(num_st_rps, sizeof(*sets));
		if (!sets)
			return N2VTD_ENAL_SPS;
		for (i = 0; i < num_st_rps; i++)
			if (hevc_st_ref_pic_set(&b, i, num_st_rps, sets) != 0)
				goto out;
	}
	if (br_bit(&b)) {                                 /* long_term_ref_pics_present_flag */
		uint32_t n = br_ue(&b), k;
		if (b.error || n > 32)
			goto out;
		for (k = 0; k < n; k++) {
			br_ue(&b);                        /* lt_ref_pic_poc_lsb_sps */
			br_bit(&b);                       /* used_by_curr_pic_lt_sps_flag */
		}
	}
	br_bit(&b);                                       /* sps_temporal_mvp_enabled_flag */
	br_bit(&b);                                       /* strong_intra_smoothing_enabled_flag */
	if (br_bit(&b)) {                                 /* vui_parameters_present_flag */
		if (hevc_vui(&b, out) != 0)
			goto out;
	}
	if (b.error)
		goto out;
	if (crop_l + crop_r >= 0x1000 || crop_t + crop_b >= 0x1000)
		goto out;
	{
		uint32_t sub_w = (chroma == 1 || chroma == 2) ? 2u : 1u;
		uint32_t sub_h = (chroma == 1) ? 2u : 1u;
		uint32_t cx = sub_w * (crop_l + crop_r);
		uint32_t cy = sub_h * (crop_t + crop_b);
		if (cx >= width || cy >= height)
			goto out;
		out->coded_width = width;
		out->coded_height = height;
		out->display_width = width - cx;
		out->display_height = height - cy;
	}
	out->chroma_format_idc = chroma;
	out->bit_depth_luma = bit_luma;
	out->bit_depth_chroma = bit_chroma;
	rc = 0;
out:
	free(sets);
	return rc;
}

int n2vtd_sps_parse(uint32_t codec, const uint8_t *nal, size_t bytes, struct n2vtd_sps_info *out)
{
	if (!nal || !out)
		return N2VTD_ENAL_SPS;
	memset(out, 0, sizeof(*out));
	out->full_range = 0;
	if (codec == N2VTD_CODEC_AVC1)
		return avc_sps_parse(nal, bytes, out);
	if (codec == N2VTD_CODEC_HVC1)
		return hevc_sps_parse(nal, bytes, out);
	return N2VTD_ENAL_UNSUP;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Length-prefixed sample -> Annex B                                                                                  */
/* ------------------------------------------------------------------------------------------------------------------ */

/* Index of the next start code at or after `from`, or `bytes` when there is none.  A start code is 00 00 01 or
 * 00 00 00 01; the returned value points at the first byte of the longer form when both match. */
static size_t annexb_next_start(const uint8_t *buf, size_t bytes, size_t from)
{
	size_t i;

	for (i = from; i + 2 < bytes; i++) {
		if (buf[i] != 0 || buf[i + 1] != 0)
			continue;
		if (buf[i + 2] == 1)
			return i;
		if (buf[i + 2] == 0 && i + 3 < bytes && buf[i + 3] == 1)
			return i;
	}
	return bytes;
}

/* First byte of the NAL payload after the start code at `at`, or `bytes`. */
static size_t annexb_payload(const uint8_t *buf, size_t bytes, size_t at)
{
	size_t s = (buf[at + 2] == 1) ? at + 3 : at + 4;

	return (s < bytes) ? s : bytes;
}

static unsigned nal_type_of(uint32_t codec, const uint8_t *nal, size_t bytes)
{
	if (bytes == 0)
		return 64;                                /* out of range for both codecs */
	if (codec == N2VTD_CODEC_AVC1)
		return nal[0] & 0x1fu;
	if (codec == N2VTD_CODEC_HVC1)
		return (unsigned)((nal[0] >> 1) & 0x3fu);
	return 64;
}

/* 1 when the whole buffer parses as length-prefixed NALs and ends exactly at `bytes`. */
static int lengths_valid(const uint8_t *src, size_t bytes, uint32_t length_size)
{
	size_t i = 0;

	if (length_size < 1 || length_size > 4)
		return 0;
	while (i < bytes) {
		uint64_t len = 0;
		uint32_t k;

		if (i + length_size > bytes)
			return 0;
		for (k = 0; k < length_size; k++)
			len = (len << 8) | src[i + k];
		i += length_size;
		if (len > bytes - i)
			return 0;
		i += (size_t)len;
	}
	return i == bytes;
}

int n2vtd_sample_to_annexb(uint32_t codec, const uint8_t *src, size_t bytes, uint32_t length_size, uint8_t *dst,
                           size_t cap, uint64_t *types)
{
	size_t i = 0, o = 0;
	int looks_annexb;
	int looks_lengths = 0;

	if (!src || !dst)
		return N2VTD_ENAL_SIZE;
	if (types)
		*types = 0;
	if (bytes == 0)
		return N2VTD_ENAL_SIZE;
	looks_annexb = (bytes >= 4 && src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 1) ||
	               (bytes >= 3 && src[0] == 0 && src[1] == 0 && src[2] == 1);
	/* A length-prefixed sample can LOOK like Annex B when its first length is small (0x000001xx); the length field
	 * is authoritative when the caller has one, so only fall back to Annex B when it does not parse. */
	if (length_size >= 1 && length_size <= 4)
		looks_lengths = lengths_valid(src, bytes, length_size);
	if (looks_annexb && !looks_lengths) {
		size_t at = 0;

		if (bytes > cap)
			return N2VTD_ENAL_SIZE;
		memcpy(dst, src, bytes);
		while (at < bytes) {
			size_t sc = annexb_next_start(dst, bytes, at);
			size_t p;
			unsigned type;
			if (sc >= bytes)
				break;
			p = annexb_payload(dst, bytes, sc);
			if (p >= bytes)
				break;
			type = nal_type_of(codec, dst + p, bytes - p);
			if (types && type < 64)
				*types |= (uint64_t)1 << type;
			at = p;
		}
		return (int)bytes;
	}
	if (length_size == 0) {
		if (looks_annexb) {
			if (bytes > cap)
				return N2VTD_ENAL_SIZE;
			memcpy(dst, src, bytes);
			return (int)bytes;
		}
		/* Detect: the smallest width whose first length fits the buffer, top byte nonzero. */
		{
			uint32_t ls;
			for (ls = 1; ls <= 4; ls++) {
				uint64_t len = 0;
				uint32_t k;
				if (ls >= bytes)
					break;
				for (k = 0; k < ls; k++)
					len = (len << 8) | src[k];
				if (len > 0 && len <= bytes - ls) {
					length_size = ls;
					break;
				}
			}
		}
		if (length_size == 0)
			return N2VTD_ENAL_FORMAT;
	}
	if (length_size > 4)
		return N2VTD_ENAL_FORMAT;
	while (i < bytes) {
		uint64_t len = 0;
		uint32_t k;

		if (i + length_size > bytes)
			return N2VTD_ENAL_SIZE;
		for (k = 0; k < length_size; k++)
			len = (len << 8) | src[i + k];
		i += length_size;
		if (len == 0)
			continue;
		if (len > bytes - i)
			return N2VTD_ENAL_SIZE;
		if (o + 4 + (size_t)len > cap)
			return N2VTD_ENAL_SIZE;
		dst[o++] = 0;
		dst[o++] = 0;
		dst[o++] = 0;
		dst[o++] = 1;
		memcpy(dst + o, src + i, (size_t)len);
		{
			unsigned type = nal_type_of(codec, src + i, (size_t)len);
			if (types && type < 64)
				*types |= (uint64_t)1 << type;
		}
		o += (size_t)len;
		i += (size_t)len;
	}
	return (int)o;
}

int n2vtd_annexb_find_params(uint32_t codec, const uint8_t *buf, size_t bytes, struct n2vtd_param *out, int max,
                             int *count)
{
	size_t at = 0;
	int n = 0;

	if (!buf || !out || !count)
		return N2VTD_ENAL_SIZE;
	if (codec != N2VTD_CODEC_AVC1 && codec != N2VTD_CODEC_HVC1)
		return N2VTD_ENAL_UNSUP;
	*count = 0;
	while (at < bytes) {
		size_t sc = annexb_next_start(buf, bytes, at);
		size_t payload, end, next;
		unsigned type;

		if (sc >= bytes)
			break;
		payload = annexb_payload(buf, bytes, sc);
		if (payload >= bytes)
			break;
		next = annexb_next_start(buf, bytes, payload);
		end = (next >= bytes) ? bytes : next;
		/* Trim the zero bytes that belong to the following start code prefix. */
		while (end > payload && buf[end - 1] == 0)
			end--;
		type = nal_type_of(codec, buf + payload, end - payload);
		if ((codec == N2VTD_CODEC_AVC1 && (type == 7 || type == 8)) ||
		    (codec == N2VTD_CODEC_HVC1 && (type == 32 || type == 33 || type == 34))) {
			if (n < max) {
				out[n].data = buf + payload;
				out[n].size = end - payload;
			}
			n++;
		}
		at = (next >= bytes) ? bytes : next;
	}
	*count = n;
	return 0;
}

int n2vtd_params_equal(const struct n2vtd_params *a, const struct n2vtd_params *b)
{
	int i;

	if (!a || !b)
		return 0;
	if (a->codec != b->codec || a->nvps != b->nvps || a->nsps != b->nsps || a->npps != b->npps)
		return 0;
	for (i = 0; i < a->nvps; i++)
		if (a->vps[i].size != b->vps[i].size || memcmp(a->vps[i].data, b->vps[i].data, a->vps[i].size) != 0)
			return 0;
	for (i = 0; i < a->nsps; i++)
		if (a->sps[i].size != b->sps[i].size || memcmp(a->sps[i].data, b->sps[i].data, a->sps[i].size) != 0)
			return 0;
	for (i = 0; i < a->npps; i++)
		if (a->pps[i].size != b->pps[i].size || memcmp(a->pps[i].data, b->pps[i].data, a->pps[i].size) != 0)
			return 0;
	return 1;
}
