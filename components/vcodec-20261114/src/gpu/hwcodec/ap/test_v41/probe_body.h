/* SPDX-License-Identifier: MIT */
/* probe_body.h -- the size/offset lines both layout probes print.  probe_mine.c includes
 * ap/mtk_venc_ipi.h, probe_kernel.c includes the kernel's own venc_ipi_msg.h; run_tests.sh diffs
 * the two outputs.  Every line is a sizeof or an offsetof, no interpretation. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define P(name, field) \
	printf("%-32s %6u %6zu\n", #name "." #field, (unsigned)sizeof(struct name), \
	       offsetof(struct name, field))
#define S(name) printf("%-32s %6u\n", #name, (unsigned)sizeof(struct name))

static int probe_all(void)
{
	S(venc_ap_ipi_msg_init);
	P(venc_ap_ipi_msg_init, reserved);
	P(venc_ap_ipi_msg_init, venc_inst);
	S(venc_ap_ipi_query_cap);
	P(venc_ap_ipi_query_cap, ap_inst_addr);
	P(venc_ap_ipi_query_cap, ap_data_addr);
	S(venc_vcu_ipi_query_cap_ack);
	P(venc_vcu_ipi_query_cap_ack, status);
	P(venc_vcu_ipi_query_cap_ack, id);
	P(venc_vcu_ipi_query_cap_ack, ap_data_addr);
	P(venc_vcu_ipi_query_cap_ack, vcu_data_addr);
	S(venc_ap_ipi_msg_set_param);
	P(venc_ap_ipi_msg_set_param, vcu_inst_addr);
	P(venc_ap_ipi_msg_set_param, data_item);
	P(venc_ap_ipi_msg_set_param, data);
	S(venc_ap_ipi_msg_enc);
	P(venc_ap_ipi_msg_enc, input_addr);
	P(venc_ap_ipi_msg_enc, input_size);
	P(venc_ap_ipi_msg_enc, bs_addr);
	P(venc_ap_ipi_msg_enc, bs_size);
	P(venc_ap_ipi_msg_enc, data_offset);
	P(venc_ap_ipi_msg_enc, input_fd);
	P(venc_ap_ipi_msg_enc, bs_fd);
	P(venc_ap_ipi_msg_enc, fb_num_planes);
	P(venc_ap_ipi_msg_enc, bs_mode);
	P(venc_ap_ipi_msg_enc, meta_size);
	P(venc_ap_ipi_msg_enc, meta_fd);
	P(venc_ap_ipi_msg_enc, qpmap);
	S(venc_ap_ipi_msg_deinit);
	S(venc_vcu_ipi_msg_common);
	S(venc_vcu_ipi_msg_init);
	P(venc_vcu_ipi_msg_init, vcu_inst_addr);
	S(venc_vcu_ipi_msg_set_param);
	P(venc_vcu_ipi_msg_set_param, data);
	S(venc_vcu_ipi_msg_enc);
	P(venc_vcu_ipi_msg_enc, state);
	P(venc_vcu_ipi_msg_enc, is_key_frm);
	P(venc_vcu_ipi_msg_enc, bs_size);
	S(venc_vcu_ipi_msg_waitisr);
	P(venc_vcu_ipi_msg_waitisr, irq_status);
	P(venc_vcu_ipi_msg_waitisr, timeout);
	S(venc_vcu_config);
	P(venc_vcu_config, color_desc);
	P(venc_vcu_config, resolutionChange);
	P(venc_vcu_config, dummynal);
	S(venc_info);
	P(venc_info, venc_bs_va);
	P(venc_info, index);
	P(venc_info, qpmap);
	S(ring_input_list);
	P(ring_input_list, venc_bs_va_list);
	P(ring_input_list, bs_size);
	P(ring_input_list, read_idx);
	S(venc_vsi);
	P(venc_vsi, config);
	P(venc_vsi, sizeimage);
	P(venc_vsi, list_free);
	P(venc_vsi, venc);
	P(venc_vsi, sync_mode);
	P(venc_vsi, meta_fd);
	S(mtk_color_desc);
	S(mtk_video_fmt);
	S(mtk_codec_framesizes);
	return 0;
}
