/* SPDX-License-Identifier: MIT */
/*
 * probe_arm.c -- the 32-bit AAPCS half of the layout check: compiling ap/mtk_vdec_ipi.h for armv7
 * proves that the structures keep the kernel's layout when the *daemon's* ABI (AAPCS32: 8-byte
 * alignment for uint64_t) is in effect.  Nothing runs; the asserts are the test.
 *
 * Owner: DEC01.
 */
#include <stddef.h>
#include "mtk_vdec_ipi.h"

/* The AP messages (vdec_ipi_msg.h:143-286) */
_Static_assert(sizeof(struct vdec_ap_ipi_cmd) == 8, "vdec_ap_ipi_cmd is 8 bytes");
_Static_assert(sizeof(struct vdec_ap_ipi_init) == 16, "vdec_ap_ipi_init is 16 bytes");
_Static_assert(offsetof(struct vdec_ap_ipi_init, ap_inst_addr) == 8, "ap_inst_addr at +8");
_Static_assert(sizeof(struct vdec_ap_ipi_dec_start) == 24, "vdec_ap_ipi_dec_start is 24 bytes");
_Static_assert(offsetof(struct vdec_ap_ipi_dec_start, data) == 8, "data at +8");
_Static_assert(sizeof(struct vdec_ap_ipi_set_param) == 44, "vdec_ap_ipi_set_param is 44 bytes");
_Static_assert(offsetof(struct vdec_ap_ipi_set_param, id) == 8, "id at +8");
_Static_assert(offsetof(struct vdec_ap_ipi_set_param, data) == 12, "data at +12");
_Static_assert(sizeof(struct vdec_ap_ipi_query_cap) == 24, "vdec_ap_ipi_query_cap is 24 bytes");
_Static_assert(offsetof(struct vdec_ap_ipi_query_cap, ap_data_addr) == 16, "ap_data_addr at +16");

/* The replies: the 32-bit kernel's unions are 4-byte wide but 8-aligned, so both ABIs agree */
_Static_assert(sizeof(struct vdec_vcu_ipi_ack) == 16, "vdec_vcu_ipi_ack is 16 bytes");
_Static_assert(offsetof(struct vdec_vcu_ipi_ack, ap_inst_addr) == 8, "ap_inst_addr at +8");
_Static_assert(sizeof(struct vdec_vcu_ipi_init_ack) == 24, "init_ack is 24 bytes");
_Static_assert(offsetof(struct vdec_vcu_ipi_init_ack, vcu_inst_addr) == 16, "vcu_inst_addr at +16");
_Static_assert(sizeof(struct vdec_vcu_ipi_query_cap_ack) == 40, "query_cap_ack is 40 bytes");
_Static_assert(offsetof(struct vdec_vcu_ipi_query_cap_ack, ap_data_addr) == 24, "ap_data_addr at +24");
_Static_assert(offsetof(struct vdec_vcu_ipi_query_cap_ack, vcu_data_addr) == 32, "vcu_data_addr at +32");

/* The shared memory: the daemon allocates it, so these offsets are the wire format */
_Static_assert(sizeof(struct vdec_ipi_fb) == 48, "vdec_ipi_fb is 48 bytes");
_Static_assert(sizeof(struct vdec_dec_info) == 200, "vdec_dec_info is 200 bytes");
_Static_assert(offsetof(struct vdec_dec_info, bs_fd) == 16, "bs_fd at +16");
_Static_assert(offsetof(struct vdec_dec_info, fb_fd) == 88, "fb_fd at +88");
_Static_assert(offsetof(struct vdec_dec_info, vdec_fb_va) == 160, "vdec_fb_va at +160");
_Static_assert(sizeof(struct vdec_pic_info) == 60, "vdec_pic_info is 60 bytes");
_Static_assert(sizeof(struct ring_bs_list) == 528, "ring_bs_list is 528 bytes (64 entries)");
_Static_assert(sizeof(struct ring_fb_list) == 3088, "ring_fb_list is 3088 bytes (64 entries)");
_Static_assert(offsetof(struct vdec_vsi, dec) == 6704, "dec at +6704");
_Static_assert(offsetof(struct vdec_vsi, pic) == 6904, "pic at +6904");
_Static_assert(sizeof(struct vdec_vsi) == 13752, "vdec_vsi is 13752 bytes");
_Static_assert(sizeof(struct vdec_vsi_tail) == sizeof(struct vdec_vsi) - 6704, "the tail view fits");

int main(void)
{
	return 0;
}
