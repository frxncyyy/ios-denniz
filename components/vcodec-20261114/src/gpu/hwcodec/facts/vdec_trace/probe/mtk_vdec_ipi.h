/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk_vdec_ipi.h -- the AP <-> VCU decoder message layouts and the vdec_vsi prefix, taken from
 * the phone's Android kernel (GPL-2.0):
 *
 *   nord2-current-kernel/drivers/media/platform/mtk-vcodec/vdec_ipi_msg.h  (structs, ids)
 *   nord2-current-kernel/drivers/media/platform/mtk-vcodec/vcodec_ipi_msg.h (vdec_dec_info,
 *                                                                          vdec_pic_info)
 *
 * Only struct layouts and enum values are reproduced; no code.  DEC02's probe builds these with
 * the same field widths as the 32-bit daemon sees them (u64 fields are 8-byte aligned on both
 * ARM EABI and x86-64/aarch64, so the layouts are identical; the probe asserts the offsets it
 * uses).  This header is the only GPL-2.0 file of the package.
 */
#ifndef MTK_VDEC_IPI_H
#define MTK_VDEC_IPI_H
#include <stddef.h>
#include <stdint.h>

#define MTK_MAX_DEC_CODECS_SUPPORT 128
#define VIDEO_MAX_PLANES 8
/* DEC_MAX_FB_NUM/DEC_MAX_BS_NUM are VIDEO_MAX_FRAME, and the nord2 kernel has VIDEO_MAX_FRAME = 64
 * (include/uapi/linux/videodev2.h:73).  64 is what makes vsi.dec land at 0x1a30 and
 * dec.vdec_bs_va at 0x1ac8, the offsets libvpud_vcodec.so reads (0xc562); the probe asserts them. */
#define DEC_MAX_FB_NUM 64
#define DEC_MAX_BS_NUM 64

enum vdec_ipi_msgid {
	AP_IPIMSG_DEC_INIT = 0xA000,
	AP_IPIMSG_DEC_START = 0xA001,
	AP_IPIMSG_DEC_END = 0xA002,
	AP_IPIMSG_DEC_DEINIT = 0xA003,
	AP_IPIMSG_DEC_RESET = 0xA004,
	AP_IPIMSG_DEC_SET_PARAM = 0xA005,
	AP_IPIMSG_DEC_QUERY_CAP = 0xA006,

	VCU_IPIMSG_DEC_INIT_ACK = 0xB000,
	VCU_IPIMSG_DEC_START_ACK = 0xB001,
	VCU_IPIMSG_DEC_END_ACK = 0xB002,
	VCU_IPIMSG_DEC_DEINIT_ACK = 0xB003,
	VCU_IPIMSG_DEC_RESET_ACK = 0xB004,
	VCU_IPIMSG_DEC_SET_PARAM_ACK = 0xB005,
	VCU_IPIMSG_DEC_QUERY_CAP_ACK = 0xB006,

	VCU_IPIMSG_DEC_WAITISR = 0xC000,
	VCU_IPIMSG_DEC_GET_FRAME_BUFFER = 0xC001,
	VCU_IPIMSG_DEC_PUT_FRAME_BUFFER = 0xC002,
	VCU_IPIMSG_DEC_LOCK_CORE = 0xC003,
	VCU_IPIMSG_DEC_UNLOCK_CORE = 0xC004,
	VCU_IPIMSG_DEC_LOCK_LAT = 0xC005,
	VCU_IPIMSG_DEC_UNLOCK_LAT = 0xC006
};

enum vdec_ipi_status {
	VDEC_IPI_MSG_STATUS_OK = 0,
	VDEC_IPI_MSG_STATUS_FAIL = -1
};

/* vdec_src_chg_type */
#define VDEC_RES_CHANGE      (1u << 0)
#define VDEC_NEED_SEQ_HEADER (1u << 3)

/* vdec_get_param_type / vdec_set_param_type (order is the kernel's) */
enum {
	GET_PARAM_DISP_FRAME_BUFFER, GET_PARAM_FREE_FRAME_BUFFER, GET_PARAM_FREE_BITSTREAM_BUFFER,
	GET_PARAM_PIC_INFO, GET_PARAM_CROP_INFO, GET_PARAM_DPB_SIZE, GET_PARAM_FRAME_INTERVAL,
	GET_PARAM_ERRORMB_MAP, GET_PARAM_CAPABILITY_SUPPORTED_FORMATS,
	GET_PARAM_CAPABILITY_FRAME_SIZES
};
enum {
	SET_PARAM_DECODE_MODE, SET_PARAM_FRAME_SIZE, SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER,
	SET_PARAM_UFO_MODE, SET_PARAM_CRC_PATH, SET_PARAM_GOLDEN_PATH, SET_PARAM_FB_NUM_PLANES,
	SET_PARAM_WAIT_KEY_FRAME, SET_PARAM_NAL_SIZE_LENGTH, SET_PARAM_OPERATING_RATE,
	SET_PARAM_TOTAL_FRAME_BUFQ_COUNT, SET_PARAM_DEC_LOG
};

/* vdec_ipi_msg.h */
struct vdec_ap_ipi_init {
	uint32_t msg_id;
	uint32_t reserved;
	uint64_t ap_inst_addr;
};

struct vdec_vcu_ipi_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
};

struct vdec_vcu_ipi_init_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
	uint32_t vcu_inst_addr;
};

struct vdec_ap_ipi_dec_start {
	uint32_t msg_id;
	uint32_t vcu_inst_addr;
	uint32_t data[3];
	uint32_t reserved;
};

struct vdec_ap_ipi_set_param {
	uint32_t msg_id;
	uint32_t vcu_inst_addr;
	uint32_t id;
	uint32_t data[8];
};

struct vdec_ap_ipi_query_cap {
	uint32_t msg_id;
	uint32_t id;
	uint64_t ap_inst_addr;
	uint64_t ap_data_addr;
};

struct vdec_vcu_ipi_query_cap_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
	uint32_t id;
	uint32_t reserved;
	uint64_t ap_data_addr;
	uint32_t vcu_data_addr;
};

struct vdec_ipi_fb {
	uint64_t vdec_fb_va;
	uint64_t y_fb_dma;
	uint64_t c_fb_dma;
	int32_t poc;
	uint32_t reserved0;
	uint64_t timestamp;
	uint32_t reserved;
	uint32_t reserved1;
};

struct ring_bs_list {
	uint64_t vdec_bs_va_list[DEC_MAX_BS_NUM];
	uint32_t read_idx, write_idx, count, reserved;
};

struct ring_fb_list {
	struct vdec_ipi_fb fb_list[DEC_MAX_FB_NUM];
	uint32_t read_idx, write_idx, count, reserved;
};

struct vdec_dec_info {
	uint32_t dpb_sz;
	uint32_t vdec_changed_info;
	uint64_t bs_dma;
	uint64_t bs_fd;
	uint64_t fb_dma[VIDEO_MAX_PLANES];
	uint64_t fb_fd[VIDEO_MAX_PLANES];
	uint64_t vdec_bs_va;
	uint64_t vdec_fb_va;
	uint32_t fb_num_planes;
	uint32_t index;
	uint32_t wait_key_frame;
	uint32_t error_map;
	uint64_t timestamp;
	uint32_t queued_frame_buf_count;
};

struct vdec_pic_info {
	uint32_t pic_w, pic_h, buf_w, buf_h;
	uint32_t fb_sz[8];
	uint32_t bitdepth;
	uint32_t layout_mode;
	uint32_t fourcc;
};

/* The prefix of struct vdec_vsi up to pic: the fields the AP fills/reads.  The rest of the vsi
 * (color_desc, crop, 128 formats, 128 frame sizes, strings) is not modelled here because the
 * probe never writes or reads it directly; the vendor owns that memory. */
/* The whole vsi as the vendor and the kernel agree on it (the tail from vdec_vsi after pic).  The
 * color_desc is 17 u32 in the layout libvpud_vcodec/libvcodecdrv use (checked against DEC1's
 * reference driver, whose static asserts pin dec and pic). */
struct vdec_vsi_prefix {
	struct ring_bs_list list_free_bs;
	struct ring_fb_list list_free;
	struct ring_fb_list list_disp;
	struct vdec_dec_info dec;
	struct vdec_pic_info pic;
	uint32_t color[17];
	int32_t crop[4];
	uint32_t fmts[MTK_MAX_DEC_CODECS_SUPPORT][3];
	uint32_t sizes[MTK_MAX_DEC_CODECS_SUPPORT][9];
	uint32_t aspect, fix, fix_svp, interlacing, codec_type;
	uint8_t crc[256], golden[256];
	uint8_t input_driven;
	int32_t general_buf_fd;
	uint64_t general_buf_dma;
	uint32_t general_buf_size;
};

/* The four offsets libvpud_vcodec.so (and DEC1's reference driver) depend on. */
_Static_assert(offsetof(struct vdec_vsi_prefix, dec) == 0x1a30, "vsi.dec");
_Static_assert(offsetof(struct vdec_vsi_prefix, dec.vdec_bs_va) == 0x1ac8, "vsi.dec.vdec_bs_va");
_Static_assert(offsetof(struct vdec_vsi_prefix, dec.fb_num_planes) == 0x1ad8, "vsi.dec.fb_num_planes");
_Static_assert(offsetof(struct vdec_vsi_prefix, pic) == 0x1af8, "vsi.pic");

#endif /* MTK_VDEC_IPI_H */
