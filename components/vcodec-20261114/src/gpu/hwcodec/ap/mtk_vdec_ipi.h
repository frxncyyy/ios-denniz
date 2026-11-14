/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk_vdec_ipi.h -- the AP <-> VCU decoder message layouts of the MediaTek MT6893 vcodec driver,
 * reproduced as *structure layouts only* from
 *     nord2-current-kernel/drivers/media/platform/mtk-vcodec/vdec_ipi_msg.h
 *     nord2-current-kernel/drivers/media/platform/mtk-vcodec/vcodec_ipi_msg.h
 * (both GPL-2.0).  No code is taken from those files: the structures, enumerations and their values
 * are what the wire protocol is, and both the 64-bit kernel and the phone's 32-bit daemon must agree
 * on them.
 *
 * Owner: DEC01 (gpu/hwcodec/ap).  Consumer: ap/n2ap_vdec.c and its tests.
 *
 * Layout rules that matter (checked by test_dec01 against the kernel header compiled 32- and 64-bit):
 *   - every __u64 field sits at an 8-byte offset in both ABIs (the kernel header's `#ifndef CONFIG_64BIT`
 *     unions exist only to keep the *size* right for a u64 field whose high half is unused); so one set of
 *     structures with uint64_t fields describes both, which is what this file has;
 *   - the share buffer of one IPI message is 80 bytes (SHARE_BUF_SIZE, mtk_vcu.h): every AP message below
 *     is 8..44 bytes and every VCU reply is 8..40;
 *   - `struct vdec_vsi` lives in the daemon's DMEM (it is allocated by the daemon, not by us); its ring
 *     sizes are the *daemon's*, and this kernel's VIDEO_MAX_FRAME is 64.  See N2AP_DEC_MAX_FB_NUM.
 *
 * Nothing here is a driver: no kernel headers, no inline functions, plain C11.
 */
#ifndef MTK_VDEC_IPI_H
#define MTK_VDEC_IPI_H
#include <stddef.h>
#include <stdint.h>
/*
 * struct mtk_video_fmt, struct mtk_codec_framesizes (with struct v4l2_frmsize_stepwise) and
 * struct mtk_color_desc come from the encoder package's ap/mtk_venc_ipi.h, which reproduces the same
 * vcodec_ipi_msg.h structures for the same daemon.  Including it instead of copying them keeps one
 * definition in every translation unit -- two identical struct definitions in one program are a defect
 * and include order would decide which one wins.  That header is GPL-2.0 (kernel layouts) and is not
 * edited here.
 */
#include "mtk_venc_ipi.h"

/* uapi/linux/videodev2.h:73-74 of this kernel tree (used by both the daemon and this header). */
#define MTK_VIDEO_MAX_FRAME  64
#define MTK_VIDEO_MAX_PLANES 8

#define MTK_MAX_DEC_CODECS_SUPPORT 128
/* DEC_MAX_FB_NUM / DEC_MAX_BS_NUM = VIDEO_MAX_FRAME (vdec_ipi_msg.h:11-12).  The daemon allocates the
 * vsi, so these sizes define the offsets of every field after the rings; test_dec01 pins them with the
 * observed vendor log line "vdec_get_addr ..." (facts/VDEC_PROTOCOL.md 2.2) and with a live probe. */
#define N2AP_DEC_MAX_FB_NUM MTK_VIDEO_MAX_FRAME
#define N2AP_DEC_MAX_BS_NUM MTK_VIDEO_MAX_FRAME

/* vdec_ipi_msg.h:24-33 */
enum vdec_src_chg_type {
	VDEC_NO_CHANGE               = (0 << 0),
	VDEC_RES_CHANGE              = (1 << 0),
	VDEC_REALLOC_MV_BUF          = (1 << 1),
	VDEC_HW_NOT_SUPPORT          = (1 << 2),
	VDEC_NEED_SEQ_HEADER         = (1 << 3),
	VDEC_NEED_MORE_OUTPUT_BUF    = (1 << 4),
	VDEC_CROP_CHANGED            = (1 << 5),
	VDEC_OUTPUT_NOT_GENERATED    = (1 << 6),
};

/* vdec_ipi_msg.h:35-42 */
enum vdec_ipi_msg_status {
	VDEC_IPI_MSG_STATUS_OK          = 0,
	VDEC_IPI_MSG_STATUS_FAIL        = -1,
	VDEC_IPI_MSG_STATUS_MAX_INST    = -2,
	VDEC_IPI_MSG_STATUS_ILSEQ       = -3,
	VDEC_IPI_MSG_STATUS_INVALID_ID  = -4,
	VDEC_IPI_MSG_STATUS_DMA_FAIL    = -5,
};

/* vdec_ipi_msg.h:49-73 */
enum vdec_ipi_msgid {
	AP_IPIMSG_DEC_INIT              = 0xA000,
	AP_IPIMSG_DEC_START             = 0xA001,
	AP_IPIMSG_DEC_END               = 0xA002,
	AP_IPIMSG_DEC_DEINIT            = 0xA003,
	AP_IPIMSG_DEC_RESET             = 0xA004,
	AP_IPIMSG_DEC_SET_PARAM         = 0xA005,
	AP_IPIMSG_DEC_QUERY_CAP         = 0xA006,

	VCU_IPIMSG_DEC_INIT_ACK         = 0xB000,
	VCU_IPIMSG_DEC_START_ACK        = 0xB001,
	VCU_IPIMSG_DEC_END_ACK          = 0xB002,
	VCU_IPIMSG_DEC_DEINIT_ACK       = 0xB003,
	VCU_IPIMSG_DEC_RESET_ACK        = 0xB004,
	VCU_IPIMSG_DEC_SET_PARAM_ACK    = 0xB005,
	VCU_IPIMSG_DEC_QUERY_CAP_ACK    = 0xB006,

	VCU_IPIMSG_DEC_WAITISR              = 0xC000,
	VCU_IPIMSG_DEC_GET_FRAME_BUFFER     = 0xC001,
	VCU_IPIMSG_DEC_PUT_FRAME_BUFFER     = 0xC002,
	VCU_IPIMSG_DEC_LOCK_CORE            = 0xC003,
	VCU_IPIMSG_DEC_UNLOCK_CORE          = 0xC004,
	VCU_IPIMSG_DEC_LOCK_LAT             = 0xC005,
	VCU_IPIMSG_DEC_UNLOCK_LAT           = 0xC006
};

/* vdec_ipi_msg.h:90-108 */
enum vdec_get_param_type {
	GET_PARAM_DISP_FRAME_BUFFER,
	GET_PARAM_FREE_FRAME_BUFFER,
	GET_PARAM_FREE_BITSTREAM_BUFFER,
	GET_PARAM_PIC_INFO,
	GET_PARAM_CROP_INFO,
	GET_PARAM_DPB_SIZE,
	GET_PARAM_FRAME_INTERVAL,
	GET_PARAM_ERRORMB_MAP,
	GET_PARAM_CAPABILITY_SUPPORTED_FORMATS,
	GET_PARAM_CAPABILITY_FRAME_SIZES,
	GET_PARAM_COLOR_DESC,
	GET_PARAM_ASPECT_RATIO,
	GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS,
	GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS_SVP,
	GET_PARAM_INTERLACING,
	GET_PARAM_CODEC_TYPE,
	GET_PARAM_INPUT_DRIVEN
};

/* vdec_ipi_msg.h:123-136 */
enum vdec_set_param_type {
	SET_PARAM_DECODE_MODE,
	SET_PARAM_FRAME_SIZE,
	SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER,
	SET_PARAM_UFO_MODE,
	SET_PARAM_CRC_PATH,
	SET_PARAM_GOLDEN_PATH,
	SET_PARAM_FB_NUM_PLANES,
	SET_PARAM_WAIT_KEY_FRAME,
	SET_PARAM_NAL_SIZE_LENGTH,
	SET_PARAM_OPERATING_RATE,
	SET_PARAM_TOTAL_FRAME_BUFQ_COUNT,
	SET_PARAM_DEC_LOG,
};

/* vcodec_ipi_msg.h:18-22 (the status a WAITISR reply carries: which core's interrupt was waited for). */
enum mtk_vdec_hw_id {
	MTK_VDEC_CORE = 0,
	MTK_VDEC_LAT  = 1,
	MTK_VDEC_HW_NUM = 2,
};

/* vcodec_ipi_msg.h:24-28 */
enum mtk_fmt_type {
	MTK_FMT_DEC = 0,
	MTK_FMT_ENC = 1,
	MTK_FMT_FRAME = 2,
};


/* include/uapi/linux/videodev2.h: struct v4l2_rect */
struct mtk_v4l2_rect {
	int32_t left;
	int32_t top;
	uint32_t width;
	uint32_t height;
};

/* vcodec_ipi_msg.h:103-112 */
struct vdec_pic_info {
	uint32_t pic_w;
	uint32_t pic_h;
	uint32_t buf_w;
	uint32_t buf_h;
	uint32_t fb_sz[MTK_VIDEO_MAX_PLANES];
	uint32_t bitdepth;
	uint32_t layout_mode;
	uint32_t fourcc;
};

/* vcodec_ipi_msg.h:127-142 */
struct vdec_dec_info {
	uint32_t dpb_sz;
	uint32_t vdec_changed_info;
	uint64_t bs_dma;
	uint64_t bs_fd;
	uint64_t fb_dma[MTK_VIDEO_MAX_PLANES];
	uint64_t fb_fd[MTK_VIDEO_MAX_PLANES];
	uint64_t vdec_bs_va;
	uint64_t vdec_fb_va;
	uint32_t fb_num_planes;
	uint32_t index;
	uint32_t wait_key_frame;
	uint32_t error_map;
	uint64_t timestamp;
	uint32_t queued_frame_buf_count;
};

/* vdec_ipi_msg.h:297-304 */
struct vdec_ipi_fb {
	uint64_t vdec_fb_va;
	uint64_t y_fb_dma;
	uint64_t c_fb_dma;
	int32_t poc;
	uint64_t timestamp;
	uint32_t reserved;
};

/* vdec_ipi_msg.h:313-319 */
struct ring_bs_list {
	uint64_t vdec_bs_va_list[N2AP_DEC_MAX_BS_NUM];
	uint32_t read_idx;
	uint32_t write_idx;
	uint32_t count;
	uint32_t reserved;
};

/* vdec_ipi_msg.h:328-334 */
struct ring_fb_list {
	struct vdec_ipi_fb fb_list[N2AP_DEC_MAX_FB_NUM];
	uint32_t read_idx;
	uint32_t write_idx;
	uint32_t count;
	uint32_t reserved;
};

/* vdec_ipi_msg.h:351-372.  The rings come first and depend on the daemon's DEC_MAX_FB_NUM/BS_NUM; the
 * kernel's own build (this file, 64 entries) puts `dec` at offset 6448.  A client that must cope with a
 * daemon built against a different entry count addresses the same fields through struct vdec_vsi_tail
 * (below) at a computed offset instead of through this struct. */
struct vdec_vsi {
	struct ring_bs_list list_free_bs;
	struct ring_fb_list list_free;
	struct ring_fb_list list_disp;
	struct vdec_dec_info dec;
	struct vdec_pic_info pic;
	struct mtk_color_desc color_desc;
	struct mtk_v4l2_rect crop;
	struct mtk_video_fmt video_formats[MTK_MAX_DEC_CODECS_SUPPORT];
	struct mtk_codec_framesizes vdec_framesizes[MTK_MAX_DEC_CODECS_SUPPORT];
	uint32_t aspect_ratio;
	uint32_t fix_buffers;
	uint32_t fix_buffers_svp;
	uint32_t interlacing;
	uint32_t codec_type;
	uint8_t crc_path[256];
	uint8_t golden_path[256];
	uint8_t input_driven;
	int32_t general_buf_fd;
	uint64_t general_buf_dma;
	uint32_t general_buf_size;
};

/*
 * Everything from `dec` on, independent of the ring entry counts.  The client computes
 *   tail = (uint8_t *)vsi + (8*bs_entries + 16) + 2 * (48*fb_entries + 16)
 * and reads `dec`, `pic`, the capability arrays and the general-buffer fields through this type.  For
 * the kernel's own counts (64/64) the offset equals offsetof(struct vdec_vsi, dec), which test_dec01
 * asserts at compile time.
 */
struct vdec_vsi_tail {
	struct vdec_dec_info dec;
	struct vdec_pic_info pic;
	struct mtk_color_desc color_desc;
	struct mtk_v4l2_rect crop;
	struct mtk_video_fmt video_formats[MTK_MAX_DEC_CODECS_SUPPORT];
	struct mtk_codec_framesizes vdec_framesizes[MTK_MAX_DEC_CODECS_SUPPORT];
	uint32_t aspect_ratio;
	uint32_t fix_buffers;
	uint32_t fix_buffers_svp;
	uint32_t interlacing;
	uint32_t codec_type;
	uint8_t crc_path[256];
	uint8_t golden_path[256];
	uint8_t input_driven;
	int32_t general_buf_fd;
	uint64_t general_buf_dma;
	uint32_t general_buf_size;
};

/*
 * The offsets the vendor library hard-codes (DEC02's trace: 0x1ac8 is what get_ringbuf reads).
 * With VIDEO_MAX_FRAME=64 and this ABI they are fixed; a client that computed them from a different
 * header would write every dec field 0xd00 bytes too low (DEC02's first probe).  Compile-time, so a
 * future layout change cannot silently break the vendor hand-over.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(offsetof(struct vdec_vsi, list_free) == 0x210, "list_free at 0x210");
_Static_assert(offsetof(struct vdec_vsi, list_disp) == 0xe20, "list_disp at 0xe20");
_Static_assert(offsetof(struct vdec_vsi, dec) == 0x1a30, "dec at 0x1a30");
_Static_assert(offsetof(struct vdec_vsi, dec.bs_fd) == 0x1a40, "dec.bs_fd at 0x1a40");
_Static_assert(offsetof(struct vdec_vsi, dec.vdec_bs_va) == 0x1ac8, "dec.vdec_bs_va at 0x1ac8");
_Static_assert(offsetof(struct vdec_vsi, pic) == 0x1af8, "pic at 0x1af8");
_Static_assert(offsetof(struct vdec_vsi, crop) == 0x1b78, "crop at 0x1b78");
#endif

/* vdec_ipi_msg.h:143-146 */
struct vdec_ap_ipi_cmd {
	uint32_t msg_id;
	uint32_t vcu_inst_addr;
};

/* vdec_ipi_msg.h:154-165: the 32-bit kernel's union is 4 bytes wide but 8-aligned, so the offsets
 * are the same in both ABIs; the high half of ap_inst_addr is not used by the daemon. */
struct vdec_vcu_ipi_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
};

/* vdec_ipi_msg.h:173-184 */
struct vdec_ap_ipi_init {
	uint32_t msg_id;
	uint32_t reserved;         /* svp_mode */
	uint64_t ap_inst_addr;
};

/* vdec_ipi_msg.h:193-205 */
struct vdec_vcu_ipi_init_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
	uint32_t vcu_inst_addr;
	uint32_t reserved;         /* tail padding: 24 bytes on the wire, like the kernel's struct */
};

/* vdec_ipi_msg.h:215-220 */
struct vdec_ap_ipi_dec_start {
	uint32_t msg_id;
	uint32_t vcu_inst_addr;
	/* vdec_common_if.c:267-269: [0] = bs->size, [1] = bs->length, [2] = bs->flags.  The names are
	 * the kernel's and are the opposite of what they suggest: mtk_vcodec_dec.c:670-671 sets
	 *   bs->size   = planes[0].bytesused  -> the valid access-unit bytes
	 *   bs->length = planes[0].length     -> the buffer/allocation size
	 * so [0] is the AU length and [1] the size of the mapped bitstream buffer. */
	uint32_t data[3];
	uint32_t reserved;
};

/* vdec_ipi_msg.h:229-234 */
struct vdec_ap_ipi_set_param {
	uint32_t msg_id;
	uint32_t vcu_inst_addr;
	uint32_t id;
	uint32_t data[8];
};

/* vdec_ipi_msg.h:242-258 */
struct vdec_ap_ipi_query_cap {
	uint32_t msg_id;
	uint32_t id;
	uint64_t ap_inst_addr;
	uint64_t ap_data_addr;
};

/* vdec_ipi_msg.h:267-286 */
struct vdec_vcu_ipi_query_cap_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
	uint32_t id;
	uint32_t reserved;         /* alignment of ap_data_addr (8) in both ABIs */
	uint64_t ap_data_addr;
	uint32_t vcu_data_addr;
	uint32_t reserved2;        /* tail padding: 40 bytes */
};

#endif /* MTK_VDEC_IPI_H */
