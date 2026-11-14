/* SPDX-License-Identifier: MIT */
/*
 * probe_body.h -- the size/offset lines both layout probes print.  probe_mine.c includes
 * ap/mtk_vdec_ipi.h, probe_kernel.c includes the kernel's own vdec_ipi_msg.h; run_tests.sh diffs the
 * two outputs, once for the 64-bit and once for the 32-bit branch of that header.
 *
 * Every line is a sizeof or an offsetof, no interpretation.  The struct names are the kernel's, so the
 * same source compiles against both headers.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DEC_P(name, field) \
	printf("%-34s %6u %6zu\n", #name "." #field, (unsigned)sizeof(struct name), \
	       offsetof(struct name, field))
#define DEC_S(name) printf("%-34s %6u\n", #name, (unsigned)sizeof(struct name))

static int probe_all(void)
{
	/* AP -> VCU (vdec_ipi_msg.h) */
	DEC_S(vdec_ap_ipi_cmd);
	DEC_P(vdec_ap_ipi_cmd, vcu_inst_addr);
	DEC_S(vdec_ap_ipi_init);
	DEC_P(vdec_ap_ipi_init, reserved);
	DEC_P(vdec_ap_ipi_init, ap_inst_addr);
	DEC_S(vdec_ap_ipi_dec_start);
	DEC_P(vdec_ap_ipi_dec_start, vcu_inst_addr);
	DEC_P(vdec_ap_ipi_dec_start, data);
	DEC_P(vdec_ap_ipi_dec_start, reserved);
	DEC_S(vdec_ap_ipi_set_param);
	DEC_P(vdec_ap_ipi_set_param, vcu_inst_addr);
	DEC_P(vdec_ap_ipi_set_param, id);
	DEC_P(vdec_ap_ipi_set_param, data);
	DEC_S(vdec_ap_ipi_query_cap);
	DEC_P(vdec_ap_ipi_query_cap, id);
	DEC_P(vdec_ap_ipi_query_cap, ap_inst_addr);
	DEC_P(vdec_ap_ipi_query_cap, ap_data_addr);

	/* VCU -> AP (vdec_ipi_msg.h) */
	DEC_S(vdec_vcu_ipi_ack);
	DEC_P(vdec_vcu_ipi_ack, status);
	DEC_P(vdec_vcu_ipi_ack, ap_inst_addr);
	DEC_S(vdec_vcu_ipi_init_ack);
	DEC_P(vdec_vcu_ipi_init_ack, status);
	DEC_P(vdec_vcu_ipi_init_ack, ap_inst_addr);
	DEC_P(vdec_vcu_ipi_init_ack, vcu_inst_addr);
	DEC_S(vdec_vcu_ipi_query_cap_ack);
	DEC_P(vdec_vcu_ipi_query_cap_ack, status);
	DEC_P(vdec_vcu_ipi_query_cap_ack, ap_inst_addr);
	DEC_P(vdec_vcu_ipi_query_cap_ack, id);
	DEC_P(vdec_vcu_ipi_query_cap_ack, ap_data_addr);
	DEC_P(vdec_vcu_ipi_query_cap_ack, vcu_data_addr);

	/* the shared memory (vdec_ipi_msg.h / vcodec_ipi_msg.h) */
	DEC_S(vdec_ipi_fb);
	DEC_P(vdec_ipi_fb, y_fb_dma);
	DEC_P(vdec_ipi_fb, c_fb_dma);
	DEC_P(vdec_ipi_fb, poc);
	DEC_P(vdec_ipi_fb, timestamp);
	DEC_P(vdec_ipi_fb, reserved);
	DEC_S(ring_bs_list);
	DEC_P(ring_bs_list, read_idx);
	DEC_P(ring_bs_list, count);
	DEC_S(ring_fb_list);
	DEC_P(ring_fb_list, fb_list);
	DEC_P(ring_fb_list, read_idx);
	DEC_P(ring_fb_list, count);
	DEC_S(vdec_dec_info);
	DEC_P(vdec_dec_info, vdec_changed_info);
	DEC_P(vdec_dec_info, bs_dma);
	DEC_P(vdec_dec_info, bs_fd);
	DEC_P(vdec_dec_info, fb_dma);
	DEC_P(vdec_dec_info, fb_fd);
	DEC_P(vdec_dec_info, vdec_bs_va);
	DEC_P(vdec_dec_info, vdec_fb_va);
	DEC_P(vdec_dec_info, fb_num_planes);
	DEC_P(vdec_dec_info, index);
	DEC_P(vdec_dec_info, timestamp);
	DEC_P(vdec_dec_info, queued_frame_buf_count);
	DEC_S(vdec_pic_info);
	DEC_P(vdec_pic_info, fb_sz);
	DEC_P(vdec_pic_info, bitdepth);
	DEC_P(vdec_pic_info, fourcc);
	DEC_S(mtk_color_desc);
	DEC_S(mtk_video_fmt);
	DEC_P(mtk_video_fmt, type);
	DEC_S(mtk_codec_framesizes);
	DEC_P(mtk_codec_framesizes, stepwise);
	DEC_S(vdec_vsi);
	DEC_P(vdec_vsi, list_free);
	DEC_P(vdec_vsi, list_disp);
	DEC_P(vdec_vsi, dec);
	DEC_P(vdec_vsi, pic);
	DEC_P(vdec_vsi, color_desc);
	DEC_P(vdec_vsi, crop);
	DEC_P(vdec_vsi, video_formats);
	DEC_P(vdec_vsi, vdec_framesizes);
	DEC_P(vdec_vsi, aspect_ratio);
	DEC_P(vdec_vsi, crc_path);
	DEC_P(vdec_vsi, input_driven);
	DEC_P(vdec_vsi, general_buf_dma);
	DEC_P(vdec_vsi, general_buf_size);
	printf("%-34s %6u\n", "MTK_MAX_DEC_CODECS_SUPPORT", (unsigned)MTK_MAX_DEC_CODECS_SUPPORT);
	return 0;
}
