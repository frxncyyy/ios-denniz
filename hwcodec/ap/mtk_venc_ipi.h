/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk_venc_ipi.h -- the MediaTek mtk-vcodec *encoder* AP <-> VCU message structures, as the 32-bit
 * daemon (vpud / libvcodecdrv.so) sees them.
 *
 * Owner: V41 (gpu/hwcodec/ap).  Consumers: ap/n2ap_venc.c, the host test ap/test_v41, the daemon (IP)
 * and the host runner (IV).
 *
 * Source (GPL-2.0, copied field lists only; the kernel headers are the specification):
 *   nord2-current-kernel/drivers/media/platform/mtk-vcodec/venc_ipi_msg.h
 *   nord2-current-kernel/drivers/media/platform/mtk-vcodec/vcodec_ipi_msg.h
 *
 * The kernel header switches on CONFIG_64BIT for a few unions, specifically so that a 64-bit kernel
 * and a 32-bit VCU see the same bytes; both branches produce the sizes asserted at the bottom of
 * this file (verified against the kernel header itself, compiled twice, by
 * ap/test_v41/probe_kernel.c).  Do not add packing pragmas: every structure here uses natural
 * alignment, and under AAPCS32 a uint64_t member is 8-byte aligned, which is what makes, e.g.,
 * struct venc_vsi 2016 bytes on the 32-bit daemon and on the 64-bit AP alike.
 *
 * VIDEO_MAX_FRAME and VIDEO_MAX_PLANES are the values in the kernel's uapi/linux/videodev2.h of
 * this tree: 64 and 8 (videodev2.h:73-74).  VENC_MAX_FB_NUM and VENC_MAX_BS_NUM are
 * VIDEO_MAX_FRAME (venc_ipi_msg.h:17-18), so struct ring_input_list is 1552 bytes, not 784.
 */
#ifndef MTK_VENC_IPI_H
#define MTK_VENC_IPI_H
#include <stddef.h>
#include <stdint.h>

/* venc_ipi_msg.h:12-26 */
#define MTK_MAX_ENC_CODECS_SUPPORT 64
#define AP_IPIMSG_VENC_BASE        0xC000
#define VCU_IPIMSG_VENC_BASE       0xD000
#define VCU_IPIMSG_VENC_SEND_BASE  0xE000
#define VENC_MAX_FB_NUM            64   /* VIDEO_MAX_FRAME (videodev2.h:73) */
#define VENC_MAX_BS_NUM            64   /* VIDEO_MAX_FRAME */

#define VIDEO_MAX_FRAME  64             /* uapi/linux/videodev2.h:73 */
#define VIDEO_MAX_PLANES 8              /* uapi/linux/videodev2.h:74 */
#define MTK_VCODEC_MAX_PLANES 3         /* mtk_vcodec_drv.h; the V4L2 AP path uses 3 planes */

/* SHARE_BUF_SIZE of uapi/linux/mtk_vcu_controls.h:11: the kernel BUILD_BUG_ONs every message below
 * against it (venc_vcu_if.c:79-92). */
#define MTK_VENC_SHARE_BUF_SIZE 80

/* enum venc_ipi_msg_id (venc_ipi_msg.h:33-50).  The enum is used as uint32_t on the wire. */
enum venc_ipi_msg_id {
	AP_IPIMSG_ENC_INIT = AP_IPIMSG_VENC_BASE,       /* 0xC000 */
	AP_IPIMSG_ENC_SET_PARAM,                        /* 0xC001 */
	AP_IPIMSG_ENC_ENCODE,                           /* 0xC002 */
	AP_IPIMSG_ENC_DEINIT,                           /* 0xC003 */
	AP_IPIMSG_ENC_QUERY_CAP,                        /* 0xC004 */

	VCU_IPIMSG_ENC_INIT_DONE = VCU_IPIMSG_VENC_BASE,/* 0xD000 */
	VCU_IPIMSG_ENC_SET_PARAM_DONE,                  /* 0xD001 */
	VCU_IPIMSG_ENC_ENCODE_DONE,                     /* 0xD002 */
	VCU_IPIMSG_ENC_DEINIT_DONE,                     /* 0xD003 */
	VCU_IPIMSG_ENC_QUERY_CAP_ACK,                   /* 0xD004 */
	VCU_IPIMSG_ENC_ENCODE_ACK,                      /* 0xD005 */

	VCU_IPIMSG_ENC_POWER_ON = VCU_IPIMSG_VENC_SEND_BASE, /* 0xE000 */
	VCU_IPIMSG_ENC_POWER_OFF,                       /* 0xE001 */
	VCU_IPIMSG_ENC_WAIT_ISR,                        /* 0xE002 */
	VCU_IPIMSG_ENC_PUT_BUFFER                       /* 0xE003 */
};

/* enum venc_get_param_type (venc_ipi_msg.h:53-64) */
enum venc_get_param_type {
	VENC_GET_PARAM_CAPABILITY_SUPPORTED_FORMATS = 0,
	VENC_GET_PARAM_CAPABILITY_FRAME_SIZES = 1,
	VENC_GET_PARAM_FREE_BUFFERS = 2,
	VENC_GET_PARAM_ROI_RC_QP = 3,
	VENC_GET_PARAM_RESOLUTION_CHANGE = 4,
	VENC_GET_PARAM_REFBUF_FRAME_NUM = 5
};

/* enum venc_set_param_type (venc_ipi_msg.h:78-111).  The comment there warns that the order must
 * match the VCU's copy; it is part of the wire contract, keep the numbering. */
enum venc_set_param_type {
	VENC_SET_PARAM_ENC = 0,
	VENC_SET_PARAM_FORCE_INTRA,
	VENC_SET_PARAM_ADJUST_BITRATE,
	VENC_SET_PARAM_ADJUST_FRAMERATE,
	VENC_SET_PARAM_GOP_SIZE,
	VENC_SET_PARAM_INTRA_PERIOD,
	VENC_SET_PARAM_SKIP_FRAME,
	VENC_SET_PARAM_PREPEND_HEADER,
	VENC_SET_PARAM_TS_MODE,
	VENC_SET_PARAM_SCENARIO,
	VENC_SET_PARAM_NONREFP,
	VENC_SET_PARAM_DETECTED_FRAMERATE,
	VENC_SET_PARAM_RFS_ON,
	VENC_SET_PARAM_PREPEND_SPSPPS_TO_IDR,
	VENC_SET_PARAM_OPERATION_RATE,
	VENC_SET_PARAM_BITRATE_MODE,
	VENC_SET_PARAM_ROI_ON,
	VENC_SET_PARAM_HEIF_GRID_SIZE,
	VENC_SET_PARAM_COLOR_DESC,
	VENC_SET_PARAM_SEC_MODE,
	VENC_SET_PARAM_TSVC,
	VENC_SET_PARAM_NONREFPFREQ,
	VENC_SET_PARAM_ADJUST_MAX_QP,
	VENC_SET_PARAM_ADJUST_MIN_QP,
	VENC_SET_PARAM_ADJUST_I_P_QP_DELTA,
	VENC_SET_PARAM_ADJUST_FRAME_LEVEL_QP,
	VENC_SET_PARAM_MAX_REFP_NUM,
	VENC_SET_PARAM_REFP_DISTANCE,
	VENC_SET_PARAM_REFP_FRMNUM,
	VENC_SET_PARAM_LOG,
	VENC_SET_PARAM_ENABLE_DUMMY_NAL,
	VENC_SET_PARAM_COUNT
};

/* enum venc_ipi_msg_status (venc_ipi_msg.h:234-237) */
enum venc_ipi_msg_status {
	VENC_IPI_MSG_STATUS_OK = 0,
	VENC_IPI_MSG_STATUS_FAIL = 1
};

/* enum venc_ipi_msg_enc_state (venc_ipi_msg.h:295-300) */
enum venc_ipi_msg_enc_state {
	VEN_IPI_MSG_ENC_STATE_FRAME = 0,
	VEN_IPI_MSG_ENC_STATE_PART,
	VEN_IPI_MSG_ENC_STATE_SKIP,
	VEN_IPI_MSG_ENC_STATE_ERROR
};

/* enum venc_bs_mode (venc_ipi_msg.h:456-465) */
enum venc_bs_mode {
	VENC_BS_MODE_SPS = 0,
	VENC_BS_MODE_PPS,
	VENC_BS_MODE_SEQ_HDR,
	VENC_BS_MODE_FRAME,
	VENC_BS_MODE_FRAME_FINAL,
	VENC_BS_MODE_MAX
};

/* struct mtk_color_desc (vcodec_ipi_msg.h:42-57): 17 u32, 68 bytes. */
struct mtk_color_desc {
	uint32_t color_primaries;
	uint32_t transform_character;
	uint32_t matrix_coeffs;
	uint32_t display_primaries_x[3];
	uint32_t display_primaries_y[3];
	uint32_t white_point_x;
	uint32_t white_point_y;
	uint32_t max_display_mastering_luminance;
	uint32_t min_display_mastering_luminance;
	uint32_t max_content_light_level;
	uint32_t max_pic_light_level;
	uint32_t is_hdr;
	uint32_t full_range;
};

/* struct mtk_video_fmt (vcodec_ipi_msg.h:32-36) and struct v4l2_frmsize_stepwise's six u32
 * (uapi/linux/videodev2.h), as embedded in struct mtk_codec_framesizes (vcodec_ipi_msg.h:41-46). */
struct mtk_video_fmt {
	uint32_t fourcc;
	uint32_t type;          /* enum mtk_fmt_type: MTK_FMT_DEC 0, MTK_FMT_ENC 1, MTK_FMT_FRAME 2 */
	uint32_t num_planes;
};

struct mtk_codec_framesizes {
	uint32_t fourcc;
	uint32_t profile;
	uint32_t level;
	struct {
		uint32_t min_width;
		uint32_t max_width;
		uint32_t step_width;
		uint32_t min_height;
		uint32_t max_height;
		uint32_t step_height;
	} stepwise;
};

/* ---- AP -> VCU (venc_ipi_msg.h) ------------------------------------------------------------- */

/* struct venc_ap_ipi_msg_init (:121-133): 16 bytes.  venc_inst is the AP instance cookie
 * (venc_vcu_if.c:315); reserved exists only so the 64-bit kernel and 32-bit VCU agree. */
struct venc_ap_ipi_msg_init {
	uint32_t msg_id;
	uint32_t reserved;
	uint64_t venc_inst;
};

/* struct venc_ap_ipi_query_cap (:139-160): 24 bytes.  The kernel uses the CONFIG_64BIT branch,
 * the daemon the #ifndef one; both are {u32,u32,u64,u64} in memory (the union is 8 bytes and
 * 8-aligned), so this declaration is valid for both. */
struct venc_ap_ipi_query_cap {
	uint32_t msg_id;
	uint32_t id;            /* enum venc_get_param_type */
	uint64_t ap_inst_addr;  /* AP instance cookie (the kernel truncates it to 32 bits, :360) */
	uint64_t ap_data_addr;  /* AP result buffer */
};

/* struct venc_ap_ipi_msg_set_param (:172-182): 48 bytes.  For VENC_SET_PARAM_ENC the values
 * themselves travel in the shared struct venc_vsi (venc_common_if.c:330-400), not in data[]. */
struct venc_ap_ipi_msg_set_param {
	uint32_t msg_id;
	uint32_t vcu_inst_addr; /* the vsi address the VCU returned in INIT_DONE */
	uint32_t param_id;      /* enum venc_set_param_type */
	uint32_t data_item;
	uint32_t data[8];
};

/* struct venc_ap_ipi_msg_enc (:196-221): 76 bytes.  input_fd/bs_fd are dma-buf fds on Android
 * (get_mapped_fd, venc_vcu_if.c:545); our daemon gets no fd (0) and the AP syncs the DMA mirror
 * itself.  meta_* belong to HDR dynamic metadata, always 0 in our path. */
struct venc_ap_ipi_msg_enc {
	uint32_t msg_id;
	uint32_t vcu_inst_addr;
	uint32_t input_addr[3];
	uint32_t input_size[3];
	uint32_t bs_addr;
	uint32_t bs_size;
	uint32_t data_offset[3];
	int16_t  input_fd[3];
	int16_t  bs_fd;
	uint8_t  fb_num_planes;
	uint8_t  bs_mode;       /* enum venc_bs_mode */
	uint32_t meta_size;
	int16_t  meta_fd;
	uint32_t qpmap;
};

/* struct venc_ap_ipi_msg_deinit (:228-233): 8 bytes. */
struct venc_ap_ipi_msg_deinit {
	uint32_t msg_id;
	uint32_t vcu_inst_addr;
};

/* ---- VCU -> AP (venc_ipi_msg.h) ------------------------------------------------------------- */

/* struct venc_vcu_ipi_msg_common (:246-251): 16 bytes.  Used by POWER_ON, POWER_OFF, WAIT_ISR and
 * PUT_BUFFER (0xE000-0xE003) and by the *_DONE messages whose payload carries nothing else. */
struct venc_vcu_ipi_msg_common {
	uint32_t msg_id;
	int32_t  status;        /* enum venc_ipi_msg_status */
	uint64_t venc_inst;
};

/* struct venc_vcu_ipi_msg_init (:262-282): 24 bytes; vcu_inst_addr at 16 is the vsi address the AP
 * maps with vcu_mapping_dm_addr() (venc_vcu_if.c:17-25). */
struct venc_vcu_ipi_msg_init {
	uint32_t msg_id;
	int32_t  status;
	uint64_t venc_inst;
	uint32_t vcu_inst_addr;
	uint32_t reserved;
};

/* struct venc_vcu_ipi_msg_set_param (:293-309): 48 bytes. */
struct venc_vcu_ipi_msg_set_param {
	uint32_t msg_id;
	int32_t  status;
	uint64_t venc_inst;
	uint32_t param_id;
	uint32_t data_item;
	uint32_t data[6];
};

/* struct venc_vcu_ipi_msg_enc (:322-343): 32 bytes.  The kernel's vcu_enc_ipi_handler() has no
 * case for VCU_IPIMSG_ENC_ENCODE_DONE (venc_vcu_if.c:123-179), so it falls into the default
 * branch: status/signaled are set, state/is_key_frm/bs_size are ignored.  The frame size of the
 * normal path reaches the V4L2 layer through the free-buffer ring in venc_vsi.list_free instead
 * (venc_get_free_buffers, venc_common_if.c:463-489; mtk_enc_put_buf, mtk_vcodec_enc.c:113-178).
 * n2ap_venc.c records both. */
struct venc_vcu_ipi_msg_enc {
	uint32_t msg_id;
	int32_t  status;
	uint64_t venc_inst;
	uint32_t state;         /* enum venc_ipi_msg_enc_state */
	uint32_t is_key_frm;
	uint32_t bs_size;
	uint32_t reserved;
};

/* struct venc_vcu_ipi_msg_deinit (:350-357): 16 bytes. */
struct venc_vcu_ipi_msg_deinit {
	uint32_t msg_id;
	int32_t  status;
	uint64_t venc_inst;
};

/* struct venc_vcu_ipi_msg_waitisr (:367-380): 24 bytes.  The AP handler fills irq_status from the
 * encoder context and timeout=1 when mtk_vcodec_wait_for_done_ctx() timed out
 * (venc_vcu_if.c:58-66, :159-169); the same buffer is copied back to the daemon. */
struct venc_vcu_ipi_msg_waitisr {
	uint32_t msg_id;
	int32_t  status;
	uint64_t venc_inst;
	uint32_t irq_status;
	uint32_t timeout;
};

/* struct venc_vcu_ipi_query_cap_ack (:165-190): 40 bytes.  The VCU writes the capability array into
 * its DMEM and returns its address; the AP copies sizeof(element) * MTK_MAX_ENC_CODECS_SUPPORT
 * bytes to ap_data_addr (venc_vcu_if.c:27-56). */
struct venc_vcu_ipi_query_cap_ack {
	uint32_t msg_id;
	int32_t  status;
	uint64_t ap_inst_addr;
	uint32_t id;
	uint32_t reserved;      /* implicit in the 64-bit kernel layout; explicit here */
	uint64_t ap_data_addr;
	uint32_t vcu_data_addr;
	uint32_t reserved2;     /* tail padding in both ABIs (size 40) */
};

/* ---- the shared encoder instance (venc_ipi_msg.h:382-700) ----------------------------------- */

/* struct venc_vcu_config (:382-443): 220 bytes.  Written by the AP before VENC_SET_PARAM_ENC. */
struct venc_vcu_config {
	uint32_t input_fourcc;
	uint32_t bitrate;
	uint32_t pic_w;
	uint32_t pic_h;
	uint32_t buf_w;
	uint32_t buf_h;
	uint32_t gop_size;
	uint32_t intra_period;
	uint32_t framerate;
	uint32_t profile;
	uint32_t level;
	uint32_t wfd;
	uint32_t operationrate;
	uint32_t scenario;
	uint32_t prependheader;
	uint32_t bitratemode;
	uint32_t roi_rc_qp;
	uint32_t roion;
	uint32_t heif_grid_size;
	struct mtk_color_desc color_desc;
	uint32_t resolutionChange;
	uint32_t max_w;
	uint32_t max_h;
	uint32_t num_b_frame;
	uint32_t slbc_ready;
	uint32_t i_qp;
	uint32_t p_qp;
	uint32_t b_qp;
	uint32_t svp_mode;
	uint32_t svp_is_hal_secure_handle;
	uint32_t tsvc;
	uint32_t max_qp;
	uint32_t min_qp;
	uint32_t i_p_qp_delta;
	uint32_t qp_control_mode;
	uint32_t frame_level_qp;
	uint32_t maxrefpnum;
	uint32_t maxrefbufFrameNum;
	uint32_t dummynal;
};

/* struct venc_info (:448-477): 184 bytes.  venc_bs_va/venc_fb_va are opaque AP cookies the daemon
 * echoes back through the free-buffer ring; the kernel stores pointers to its own buffer structs
 * there (venc_common_if.c:126-150). */
struct venc_info {
	uint64_t bs_dma;
	uint64_t bs_fd;
	uint64_t fb_dma[VIDEO_MAX_PLANES];
	uint64_t fb_fd[VIDEO_MAX_PLANES];
	uint64_t venc_bs_va;
	uint64_t venc_fb_va;
	uint32_t fb_num_planes;
	uint32_t index;
	uint64_t timestamp;
	uint32_t roimap;
	uint32_t qpmap;
};

/* struct ring_input_list (:486-501): 1552 bytes with VENC_MAX_FB_NUM=64. */
struct ring_input_list {
	uint64_t venc_bs_va_list[VENC_MAX_FB_NUM];
	uint64_t venc_fb_va_list[VENC_MAX_FB_NUM];
	uint32_t bs_size[VENC_MAX_FB_NUM];
	uint32_t is_key_frm[VENC_MAX_FB_NUM];
	int32_t  read_idx;
	int32_t  write_idx;
	int32_t  count;
	int32_t  reserved;
};

/* struct venc_vsi (:527-550): 2016 bytes.  Allocated by the VCU side and shared to the AP; the AP
 * writes config, venc.venc_bs_va/venc_fb_va/timestamp/roimap and meta_*, the VCU writes sizeimage,
 * sync_mode and list_free (venc_common_if.c:330-400, :463-489; venc_vcu_if.c:515-609). */
struct venc_vsi {
	struct venc_vcu_config config;      /* 0 */
	uint32_t sizeimage[VIDEO_MAX_PLANES];   /* 220 */
	struct ring_input_list list_free;   /* 256 */
	struct venc_info venc;              /* 1808 */
	uint32_t sync_mode;                 /* 1992 */
	uint32_t meta_size;                 /* 1996 */
	uint64_t meta_addr;                 /* 2000 */
	int16_t  meta_fd;                   /* 2008 */
	uint8_t  reserved[6];               /* tail padding to 2016; the kernel has implicit padding */
};

/* The message sizes the kernel BUILD_BUG_ONs against SHARE_BUF_SIZE (venc_vcu_if.c:79-92). */
_Static_assert(sizeof(struct venc_ap_ipi_msg_init) == 16, "venc_ap_ipi_msg_init");
_Static_assert(sizeof(struct venc_ap_ipi_query_cap) == 24, "venc_ap_ipi_query_cap");
_Static_assert(sizeof(struct venc_ap_ipi_msg_set_param) == 48, "venc_ap_ipi_msg_set_param");
_Static_assert(sizeof(struct venc_ap_ipi_msg_enc) == 76, "venc_ap_ipi_msg_enc");
_Static_assert(sizeof(struct venc_ap_ipi_msg_deinit) == 8, "venc_ap_ipi_msg_deinit");
_Static_assert(sizeof(struct venc_vcu_ipi_query_cap_ack) == 40, "venc_vcu_ipi_query_cap_ack");
_Static_assert(sizeof(struct venc_vcu_ipi_msg_common) == 16, "venc_vcu_ipi_msg_common");
_Static_assert(sizeof(struct venc_vcu_ipi_msg_init) == 24, "venc_vcu_ipi_msg_init");
_Static_assert(sizeof(struct venc_vcu_ipi_msg_set_param) == 48, "venc_vcu_ipi_msg_set_param");
_Static_assert(sizeof(struct venc_vcu_ipi_msg_enc) == 32, "venc_vcu_ipi_msg_enc");
_Static_assert(sizeof(struct venc_vcu_ipi_msg_deinit) == 16, "venc_vcu_ipi_msg_deinit");
_Static_assert(sizeof(struct venc_vcu_ipi_msg_waitisr) == 24, "venc_vcu_ipi_msg_waitisr");
_Static_assert(sizeof(struct venc_ap_ipi_msg_enc) <= MTK_VENC_SHARE_BUF_SIZE, "enc fits share_buf");
_Static_assert(sizeof(struct venc_vcu_ipi_query_cap_ack) <= MTK_VENC_SHARE_BUF_SIZE, "ack fits share_buf");

/* Shared-instance layout (offsets checked against the kernel header by ap/test_v41). */
_Static_assert(offsetof(struct venc_ap_ipi_msg_init, venc_inst) == 8, "init.venc_inst");
_Static_assert(offsetof(struct venc_ap_ipi_query_cap, ap_data_addr) == 16, "query_cap.ap_data_addr");
_Static_assert(offsetof(struct venc_vcu_ipi_query_cap_ack, id) == 16, "ack.id");
_Static_assert(offsetof(struct venc_vcu_ipi_query_cap_ack, vcu_data_addr) == 32, "ack.vcu_data_addr");
_Static_assert(offsetof(struct venc_ap_ipi_msg_set_param, data) == 16, "set_param.data");
_Static_assert(offsetof(struct venc_ap_ipi_msg_enc, bs_addr) == 32, "enc.bs_addr");
_Static_assert(offsetof(struct venc_ap_ipi_msg_enc, fb_num_planes) == 60, "enc.fb_num_planes");
_Static_assert(offsetof(struct venc_vcu_ipi_msg_init, vcu_inst_addr) == 16, "vcu_init.vcu_inst_addr");
_Static_assert(offsetof(struct venc_vcu_ipi_msg_enc, bs_size) == 24, "vcu_enc.bs_size");
_Static_assert(offsetof(struct venc_vcu_ipi_msg_waitisr, irq_status) == 16, "waitisr.irq_status");
_Static_assert(sizeof(struct venc_vcu_config) == 220, "venc_vcu_config");
_Static_assert(offsetof(struct venc_vcu_config, color_desc) == 76, "config.color_desc");
_Static_assert(offsetof(struct venc_vcu_config, dummynal) == 216, "config.dummynal");
_Static_assert(sizeof(struct mtk_color_desc) == 68, "mtk_color_desc");
_Static_assert(sizeof(struct mtk_video_fmt) == 12, "mtk_video_fmt");
_Static_assert(sizeof(struct mtk_codec_framesizes) == 36, "mtk_codec_framesizes");
_Static_assert(sizeof(struct venc_info) == 184, "venc_info");
_Static_assert(offsetof(struct venc_info, venc_bs_va) == 144, "info.venc_bs_va");
_Static_assert(sizeof(struct ring_input_list) == 1552, "ring_input_list");
_Static_assert(offsetof(struct ring_input_list, bs_size) == 1024, "ring.bs_size");
_Static_assert(sizeof(struct venc_vsi) == 2016, "venc_vsi");
_Static_assert(offsetof(struct venc_vsi, sizeimage) == 220, "vsi.sizeimage");
_Static_assert(offsetof(struct venc_vsi, list_free) == 256, "vsi.list_free");
_Static_assert(offsetof(struct venc_vsi, venc) == 1808, "vsi.venc");
_Static_assert(offsetof(struct venc_vsi, sync_mode) == 1992, "vsi.sync_mode");
_Static_assert(offsetof(struct venc_vsi, meta_addr) == 2000, "vsi.meta_addr");
_Static_assert(offsetof(struct venc_vsi, meta_fd) == 2008, "vsi.meta_fd");

#endif /* MTK_VENC_IPI_H */
