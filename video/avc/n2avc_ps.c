/* SPDX-License-Identifier: MIT */
/*
 * n2avc_ps.c -- sequence parameter set, picture parameter set and slice header of the WP03 H.264 encoder.
 *
 * ITU-T H.264 (03/2010): clause 7.3.2.1.1 (seq_parameter_set_data), 7.3.2.2 (pic_parameter_set_rbsp), 7.3.3
 * (slice_header), E.1.1 (vui_parameters).  The stream shape is the one frozen at the top of include/n2avc.h:
 * Constrained Baseline, CAVLC, one slice per band of macroblock rows, no B pictures, no reordering.
 */
#include "n2avc_priv.h"

#include <stdlib.h>
#include <string.h>

/* Smallest level whose limits cover the picture at the frame rate (include/n2avc.h, "level_idc").
 * Level 3.0: 720x576 at 25; 3.1: 1280x720 at 30; 4.0: 1920x1088 at 30; 4.2: 1920x1088 at 60; 5.1: 4096x2304. */
int n2avc_pick_level(uint32_t w, uint32_t h, uint32_t fps)
{
	if (w <= 720 && h <= 576 && fps <= 25)
		return 30;
	if (w <= 1280 && h <= 720 && fps <= 30)
		return 31;
	if (w <= 1920 && h <= 1088 && fps <= 30)
		return 40;
	if (w <= 1920 && h <= 1088 && fps <= 60)
		return 42;
	return 51;
}

static void write_vui(n2bw *w, const n2avc_enc *e)
{
	n2bw_u(w, 1, 0);                        /* aspect_ratio_info_present_flag */
	n2bw_u(w, 1, 0);                        /* overscan_info_present_flag */
	n2bw_u(w, 1, 1);                        /* video_signal_type_present_flag */
	n2bw_u(w, 3, 5);                        /* video_format: unspecified */
	n2bw_u(w, 1, (e->cfg.flags & N2AVC_F_FULLRANGE) ? 1 : 0);       /* video_full_range_flag */
	n2bw_u(w, 1, 1);                        /* colour_description_present_flag */
	n2bw_u(w, 8, (uint32_t)e->cfg.colour_primaries);
	n2bw_u(w, 8, (uint32_t)e->cfg.transfer);
	n2bw_u(w, 8, (uint32_t)e->cfg.matrix);
	n2bw_u(w, 1, 0);                        /* chroma_loc_info_present_flag */
	n2bw_u(w, 1, 1);                        /* timing_info_present_flag */
	n2bw_u(w, 32, e->fps_den);              /* num_units_in_tick */
	n2bw_u(w, 32, 2u * e->fps_num);         /* time_scale: one picture every two ticks */
	n2bw_u(w, 1, 0);                        /* fixed_frame_rate_flag */
	n2bw_u(w, 1, 0);                        /* nal_hrd_parameters_present_flag */
	n2bw_u(w, 1, 0);                        /* vcl_hrd_parameters_present_flag */
	n2bw_u(w, 1, 0);                        /* pic_struct_present_flag */
	n2bw_u(w, 1, 1);                        /* bitstream_restriction_flag */
	n2bw_u(w, 1, 1);                        /* motion_vectors_over_pic_boundaries_flag */
	n2bw_ue(w, 0);                          /* max_bytes_per_pic_denom */
	n2bw_ue(w, 0);                          /* max_bits_per_mb_denom */
	n2bw_ue(w, 9);                          /* log2_max_mv_length_horizontal */
	n2bw_ue(w, 9);                          /* log2_max_mv_length_vertical */
	n2bw_ue(w, 0);                          /* num_reorder_frames */
	n2bw_ue(w, 1);                          /* max_dec_frame_buffering */
}

/* Packs an RBSP into a NAL unit into a fresh buffer (the parameter sets are built once, in open). */
static int pack_nal(uint8_t **dst, size_t *dst_n, const uint8_t *rbsp, size_t n, int nal_ref_idc, int type)
{
	size_t cap = 1 + n + n / 2 + 8;
	uint8_t *buf = (uint8_t *)malloc(cap);
	size_t got;

	if (!buf)
		return N2AVC_E_MEMORY;
	got = n2avc_nal(buf, cap, rbsp, n, nal_ref_idc, type);
	if (!got) {
		free(buf);
		return N2AVC_E_MEMORY;
	}
	*dst = buf;
	*dst_n = got;
	return N2AVC_OK;
}

int n2avc_build_ps(n2avc_enc *e)
{
	uint8_t rbsp[256];
	n2bw w;
	int rc;
	int cw = (int)(16 * e->mb_w - e->width);        /* samples to crop on the right */
	int ch = (int)(16 * e->mb_h - e->height);
	int crop = (cw > 0 || ch > 0);

	/* seq_parameter_set_data(), clause 7.3.2.1.1 */
	n2bw_init(&w, rbsp, sizeof rbsp);
	n2bw_u(&w, 8, 66);                      /* profile_idc: Baseline */
	n2bw_u(&w, 1, 1);                       /* constraint_set0_flag */
	n2bw_u(&w, 1, 1);                       /* constraint_set1_flag: Constrained Baseline */
	n2bw_u(&w, 1, 0);                       /* constraint_set2_flag */
	n2bw_u(&w, 1, 0);                       /* constraint_set3_flag */
	n2bw_u(&w, 1, 0);                       /* constraint_set4_flag */
	n2bw_u(&w, 1, 0);                       /* constraint_set5_flag */
	n2bw_u(&w, 2, 0);                       /* reserved_zero_2bits */
	n2bw_u(&w, 8, (uint32_t)e->level_idc);
	n2bw_ue(&w, 0);                         /* seq_parameter_set_id */
	n2bw_ue(&w, 4);                         /* log2_max_frame_num_minus4: frame_num u(8) */
	n2bw_ue(&w, 2);                         /* pic_order_cnt_type */
	n2bw_ue(&w, 1);                         /* max_num_ref_frames */
	n2bw_u(&w, 1, 0);                       /* gaps_in_frame_num_value_allowed_flag */
	n2bw_ue(&w, (uint32_t)(e->mb_w - 1));   /* pic_width_in_mbs_minus1 */
	n2bw_ue(&w, (uint32_t)(e->mb_h - 1));   /* pic_height_in_map_units_minus1 */
	n2bw_u(&w, 1, 1);                       /* frame_mbs_only_flag */
	n2bw_u(&w, 1, 1);                       /* direct_8x8_inference_flag */
	n2bw_u(&w, 1, crop ? 1 : 0);            /* frame_cropping_flag */
	if (crop) {
		/* CropUnitX = SubWidthC = 2, CropUnitY = SubHeightC * (2 - frame_mbs_only_flag) = 2. */
		n2bw_ue(&w, 0);                                  /* frame_crop_left_offset */
		n2bw_ue(&w, (uint32_t)(cw / 2));                 /* frame_crop_right_offset */
		n2bw_ue(&w, 0);                                  /* frame_crop_top_offset */
		n2bw_ue(&w, (uint32_t)(ch / 2));                 /* frame_crop_bottom_offset */
	}
	n2bw_u(&w, 1, 1);                       /* vui_parameters_present_flag */
	write_vui(&w, e);
	n2bw_trailing(&w);
	if (w.overflow || n2bw_flush(&w) == 0)
		return N2AVC_E_MEMORY;
	rc = pack_nal(&e->sps, &e->sps_n, rbsp, w.pos, 3, 7);
	if (rc != N2AVC_OK)
		return rc;

	/* pic_parameter_set_rbsp(), clause 7.3.2.2 */
	n2bw_init(&w, rbsp, sizeof rbsp);
	n2bw_ue(&w, 0);                         /* pic_parameter_set_id */
	n2bw_ue(&w, 0);                         /* seq_parameter_set_id */
	n2bw_u(&w, 1, 0);                       /* entropy_coding_mode_flag: CAVLC */
	n2bw_u(&w, 1, 0);                       /* bottom_field_pic_order_in_frame_present_flag */
	n2bw_ue(&w, 0);                         /* num_slice_groups_minus1 */
	n2bw_ue(&w, 0);                         /* num_ref_idx_l0_default_active_minus1 */
	n2bw_ue(&w, 0);                         /* num_ref_idx_l1_default_active_minus1 */
	n2bw_u(&w, 1, 0);                       /* weighted_pred_flag */
	n2bw_u(&w, 2, 0);                       /* weighted_bipred_idc */
	n2bw_se(&w, 0);                         /* pic_init_qp_minus26 */
	n2bw_se(&w, 0);                         /* pic_init_qs_minus26 */
	n2bw_se(&w, 0);                         /* chroma_qp_index_offset */
	n2bw_u(&w, 1, 1);                       /* deblocking_filter_control_present_flag */
	n2bw_u(&w, 1, 0);                       /* constrained_intra_pred_flag */
	n2bw_u(&w, 1, 0);                       /* redundant_pic_cnt_present_flag */
	n2bw_trailing(&w);
	if (w.overflow || n2bw_flush(&w) == 0)
		return N2AVC_E_MEMORY;
	return pack_nal(&e->pps, &e->pps_n, rbsp, w.pos, 3, 8);
}

/* slice_header(), clause 7.3.3, for the stream shape frozen in n2avc.h. */
void n2avc_write_slice_header(n2avc_slice *s, int first_mb, int idr, int frame_num, int idr_pic_id)
{
	n2bw *w = &s->bw;
	int is_p = (s->slice_type == 0);

	n2bw_ue(w, (uint32_t)first_mb);         /* first_mb_in_slice */
	n2bw_ue(w, is_p ? 5u : 7u);             /* slice_type: 5 P, 7 I (all slices of an IDR are I) */
	n2bw_ue(w, 0);                          /* pic_parameter_set_id */
	n2bw_u(w, 8, (uint32_t)frame_num);      /* frame_num, log2_max_frame_num = 8 */
	if (idr)
		n2bw_ue(w, (uint32_t)idr_pic_id);/* idr_pic_id */
	if (is_p) {
		n2bw_u(w, 1, 0);                /* num_ref_idx_active_override_flag: defaults (one reference) */
		n2bw_u(w, 1, 0);                /* ref_pic_list_modification_flag_l0 */
	}
	if (idr) {
		n2bw_u(w, 1, 0);                /* no_output_of_prior_pics_flag */
		n2bw_u(w, 1, 0);                /* long_term_reference_flag */
	} else {
		n2bw_u(w, 1, 0);                /* adaptive_ref_pic_marking_mode_flag */
	}
	n2bw_se(w, s->qp - 26);                 /* slice_qp_delta: pic_init_qp 26 */
	n2bw_ue(w, 1);                          /* disable_deblocking_filter_idc: 1 in every slice */
}
