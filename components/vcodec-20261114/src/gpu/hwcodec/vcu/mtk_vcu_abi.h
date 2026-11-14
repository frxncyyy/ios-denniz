/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk_vcu_abi.h -- the /dev/vcu ABI as the 32-bit (AAPCS) vpud daemon sees it.
 *
 * Owner: V40 (gpu/hwcodec/vcu).  Consumer: n2vcu.c, the V40 tests, V41 and the host runner IV.
 *
 * Source: nord2-current-kernel/include/uapi/linux/mtk_vcu_controls.h (structs, ioctl numbers, GCE
 * command and event ids) and nord2-current-kernel/drivers/media/platform/mtk-vcu/mtk_vcu.h:113
 * (enum ipi_id, not UAPI but part of the same contract).  The kernel header declares every member
 * with u8/u32/u64/s32; here they are uint32_t/uint64_t so the file compiles outside the kernel.
 *
 * These are the layouts of a **32-bit ARM/AAPCS** target, not of the 64-bit kernel.  Under AAPCS
 * a 64-bit integer member is 8-byte aligned inside a struct, which is what gives mem_obj 0x20 and
 * share_obj 0x58.  Both numbers are observable in the vendor library: libvpud_vcodec.so builds
 * VCU_SET_OBJECT = 0x40587600 (_IOW with size 0x58), libvcodec_utility.so builds
 * VCU_PA_ALLOCATION = 0xc0207606 (_IOWR with size 0x20).
 *
 * Cross-check (test_v40/layout_probe.c, run by test_v40/run_tests.sh): the kernel header itself is
 * compiled with
 *     clang --target=armv7a-linux-gnueabi -c -I <K>/include/uapi layout_probe.c
 * and every sizeof/offsetof/ioctl constant below is read back out of the object.  The
 * _Static_asserts here are the compile-time half of the same check; the probe is the independent
 * half (it never includes this file).
 *
 * Only the COMPAT_ numbers appear in practice: the daemon is 32-bit, so on the phone the kernel
 * would route it through mtk_vcu_unlocked_compat_ioctl(), and for mem_obj the COMPAT path copies
 * the same bytes into a 64-bit struct (mtk_vcu.c:1974-2120).  The plain VCU_* numbers below are the
 * ones the daemon actually sends (D01 facts/vcu_usage.md section 2).  For share_obj, map_obj,
 * gce_cmds, gce_cmdq_obj, gce_obj, log_test_nofuse the two are identical anyway because no member
 * changes size between the two ABIs.
 */
#ifndef MTK_VCU_ABI_H
#define MTK_VCU_ABI_H
#include <stddef.h>
#include <stdint.h>

#define MTK_VCU_SHARE_BUF_SIZE 80u          /* SHARE_BUF_SIZE */
#define MTK_VCU_LOG_INFO_SIZE 1024u         /* LOG_INFO_SIZE */
#define MTK_VCU_CMDQ_CMD_MAX 2048u          /* VCODEC_CMDQ_CMD_MAX */

/* ---- structs, in the order of mtk_vcu_controls.h ---- */

/*
 * struct mem_obj -- one allocation request/result of VCU_MVA_ALLOCATION / VCU_PA_ALLOCATION and
 * the matching free.  iova and pa are 8-byte aligned members, hence the 4-byte hole after len.
 */
struct mem_obj {
	uint64_t iova;      /*  0 */
	uint32_t len;       /*  8 */
	uint32_t pad0;      /* 12, implicit in C; named so a byte copy is explicit */
	uint64_t pa;        /* 16 */
	uint64_t va;        /* 24 */
};                          /* 32 */

struct map_obj {
	uint32_t map_buf;   /*  0: 0 not mapped, 1 mapped */
	uint32_t map_type;  /*  4: 0 reserved, 1 MM_BASE, 2 MM_CACHEABLE_BASE, 3 PA_BASE */
	uint64_t reserved;  /*  8 */
};                          /* 16 */

/*
 * struct gce_cmds -- the command list VCU_GCE_SET_CMD_FLUSH replays, packed in parallel arrays;
 * the u64 arrays must be 8-byte aligned, so `cmd` is padded from 2048 (which is already a
 * multiple of 8, so no hole) and the struct ends with 4 bytes of padding after cmd_cnt.
 */
struct gce_cmds {
	uint8_t cmd[MTK_VCU_CMDQ_CMD_MAX];              /*     0 ..  2047 */
	uint64_t addr[MTK_VCU_CMDQ_CMD_MAX];            /*  2048 .. 18431 */
	uint64_t data[MTK_VCU_CMDQ_CMD_MAX];            /* 18432 .. 34815 */
	uint32_t mask[MTK_VCU_CMDQ_CMD_MAX];            /* 34816 .. 43007 */
	uint32_t dma_offset[MTK_VCU_CMDQ_CMD_MAX];      /* 43008 .. 51200-1 */
	uint32_t dma_size[MTK_VCU_CMDQ_CMD_MAX];        /* 51200 .. 59391 */
	uint32_t cmd_cnt;                               /* 59392 */
};                                                          /* 59400 (0xe808) */

struct gce_cmdq_obj {
	uint64_t cmds_user_ptr;  /*  0: guest address of struct gce_cmds */
	uint64_t gce_handle;     /*  8 */
	uint32_t flush_order;    /* 16 */
	uint32_t codec_type;     /* 20: VCU_VDEC 0, VCU_VENC 1 */
	uint32_t core_id;        /* 24 */
	uint32_t secure;         /* 28 */
};                                  /* 32 */

struct gce_obj {
	uint64_t gce_handle;     /*  0 */
	uint32_t flush_order;    /*  8 */
	uint32_t codec_type;     /* 12 */
};                                  /* 16 */

struct share_obj {
	int32_t id;                              /*  0: IPI id */
	uint32_t len;                            /*  4: bytes used in share_buf */
	unsigned char share_buf[MTK_VCU_SHARE_BUF_SIZE];  /* 8 */
};                                                  /* 88 (0x58) */

struct log_test_nofuse {
	char log_info[MTK_VCU_LOG_INFO_SIZE];
};                                                  /* 1024 */

/* ---- enum gce_cmd_id / enum gce_event_id (values are ABI) ---- */

enum gce_cmd_id {
	CMD_READ = 0,       /* read register into the GCE scratch register */
	CMD_WRITE,          /* write register, optional mask */
	CMD_POLL_REG,       /* poll register until (value & mask) == (data & mask), no timeout */
	CMD_WAIT_EVENT,     /* wait for and clear a hardware event */
	CMD_MEM_MV,         /* copy one 32-bit word PA -> PA (a source register is allowed) */
	CMD_POLL_ADDR,      /* poll an address with a timeout */
	CMD_SEC_WRITE,      /* secure-world write, out of scope for the model */
	CMD_MAX
};

enum gce_event_id {
	VDEC_EVENT_0 = 0,   /* pic_start */
	VDEC_EVENT_1,       /* decode done */
	VDEC_EVENT_2,       /* vdec_pause */
	VDEC_EVENT_3,       /* dec_error */
	VDEC_EVENT_4,       /* mc_busy_overflow | mdec_timeout */
	VDEC_EVENT_5,       /* all_dram_req & ... */
	VDEC_EVENT_6,       /* ini_fetch_rdy */
	VDEC_EVENT_7,       /* process_flag */
	VDEC_EVENT_8,       /* search_start_code_done */
	VDEC_EVENT_9,       /* ref_reorder_done */
	VDEC_EVENT_10,      /* wp_tble_done */
	VDEC_EVENT_11,      /* count_sram_clr_done */
	VDEC_EVENT_12,
	VDEC_EVENT_13,
	VDEC_EVENT_14,
	VDEC_EVENT_15,      /* queue counter OP threshold */
	VDEC_LAT_EVENT_0,
	VDEC_LAT_EVENT_1,
	VDEC_LAT_EVENT_2,
	VDEC_LAT_EVENT_3,
	VDEC_LAT_EVENT_4,
	VDEC_LAT_EVENT_5,
	VDEC_LAT_EVENT_6,
	VDEC_LAT_EVENT_7,
	VDEC_LAT_EVENT_8,
	VDEC_LAT_EVENT_9,
	VDEC_LAT_EVENT_10,
	VDEC_LAT_EVENT_11,
	VDEC_LAT_EVENT_12,
	VDEC_LAT_EVENT_13,
	VDEC_LAT_EVENT_14,
	VDEC_LAT_EVENT_15,
	VDEC_EVENT_COUNT,   /* 32 */
	VENC_EOF = VDEC_EVENT_COUNT,   /* 32 */
	VENC_CMDQ_PAUSE_DONE,          /* 33 */
	VENC_MB_DONE,                  /* 34 */
	VENC_128BYTE_CNT_DONE,         /* 35 */
	VENC_EOF_C1,                   /* 36 */
	VENC_WP_2ND_DONE,              /* 37 */
	VENC_WP_3ND_DONE,              /* 38 */
	VENC_SPS_DONE,                 /* 39 */
	VENC_PPS_DONE                  /* 40 */
};

/* ---- enum ipi_id (mtk_vcu.h:113-142) and the VCU codec instances ---- */

enum mtk_vcu_ipi_id {
	MTK_IPI_VCU_INIT = 0,
	MTK_IPI_VDEC_COMMON,
	MTK_IPI_VDEC_H264,
	MTK_IPI_VDEC_H265,
	MTK_IPI_VDEC_HEIF,
	MTK_IPI_VDEC_VP8,
	MTK_IPI_VDEC_VP9,
	MTK_IPI_VDEC_MPEG4,
	MTK_IPI_VDEC_H263,
	MTK_IPI_VDEC_MPEG12,
	MTK_IPI_VDEC_WMV,
	MTK_IPI_VDEC_RV30,
	MTK_IPI_VDEC_RV40,
	MTK_IPI_VDEC_AV1,
	MTK_IPI_VENC_COMMON,
	MTK_IPI_VENC_H264,
	MTK_IPI_VENC_H265,
	MTK_IPI_VENC_HEIF,
	MTK_IPI_VENC_VP8,
	MTK_IPI_VENC_MPEG4,
	MTK_IPI_VENC_HYBRID_H264,
	MTK_IPI_VENC_H263,
	MTK_IPI_MDP,
	MTK_IPI_MDP_1,
	MTK_IPI_MDP_2,
	MTK_IPI_MDP_3,
	MTK_IPI_CAMERA,
	MTK_IPI_MAX = 50
};

enum vcu_codec_type {
	VCU_VDEC = 0,
	VCU_VENC,
	VCU_CODEC_MAX
};

/* GCE event ids above this value are dropped by vcu_set_gce_cmd (mtk_vcu.c:746-751). */
#define MTK_VCU_GCE_EVENT_MAX 64u

/*
 * ipi_id_to_inst_id() (mtk_vcu.c:118-125): ids below IPI_VENC_COMMON are the decoder instance,
 * everything else (including out-of-range garbage) is the encoder instance.
 */
static inline unsigned mtk_vcu_ipi_instance(int ipi_id)
{
	return (ipi_id < MTK_IPI_VENC_COMMON && ipi_id >= MTK_IPI_VCU_INIT) ? VCU_VDEC : VCU_VENC;
}

/* ---- ioctl request numbers ----
 *
 * _IOC(dir,type,nr,size) = (dir << 30) | (size << 16) | (type << 8) | nr, with 'v' = 0x76,
 * _IOW = 1, _IOWR = 3 (asm-generic/ioctl.h).  Values computed from the sizes above and
 * confirmed by layout_probe.c against the kernel header.
 *
 * The kernel dispatches on the whole 32-bit request: a request with the right nr but a different
 * size field does not match any case and returns -1 (EPERM) through the default branch
 * (mtk_vcu.c:1966-1968).  n2vcu.c reproduces that, except for nr 11/12 (-*LOG*) where the kernel
 * header's size is not derivable: VCU_SET_LOG_OBJECT names `struct log_test`, which is defined
 * nowhere in this kernel tree.
 */
#define MTK_VCU_SET_OBJECT             0x40587600u   /* _IOW ('v', 0, share_obj)        0x58 */
#define MTK_VCU_MVA_ALLOCATION         0xc0207601u   /* _IOWR('v', 1, mem_obj)          0x20 */
#define MTK_VCU_MVA_FREE               0xc0207602u   /* _IOWR('v', 2, mem_obj)          0x20 */
#define MTK_VCU_CACHE_FLUSH_ALL        0xc0207603u   /* _IOWR('v', 3, mem_obj) ignored  0x20 */
#define MTK_VCU_CACHE_FLUSH_BUFF       0xc0207604u   /* _IOWR('v', 4, mem_obj)          0x20 */
#define MTK_VCU_CACHE_INVALIDATE_BUFF  0xc0207605u   /* _IOWR('v', 5, mem_obj)          0x20 */
#define MTK_VCU_PA_ALLOCATION          0xc0207606u   /* _IOWR('v', 6, mem_obj)          0x20 */
#define MTK_VCU_PA_FREE                0xc0207607u   /* _IOWR('v', 7, mem_obj)          0x20 */
#define MTK_VCU_GCE_SET_CMD_FLUSH      0x40207608u   /* _IOW ('v', 8, gce_cmdq_obj)     0x20 */
#define MTK_VCU_GCE_WAIT_CALLBACK      0x40107609u   /* _IOW ('v', 9, gce_obj)          0x10 */
#define MTK_VCU_GET_OBJECT             0xc058760au   /* _IOWR('v',10, share_obj)        0x58 */
#define MTK_VCU_GET_LOG_OBJECT         0x4400760bu   /* _IOW ('v',11, log_test_nofuse) 0x400 */
#define MTK_VCU_SET_MMAP_TYPE          0x4010760du   /* _IOW ('v',13, map_obj)          0x10 */

/* VCU_SET_LOG_OBJECT = _IOW('v',12,struct log_test): struct log_test does not exist in the kernel
 * tree, so the size field cannot be derived.  0x400 is the size of the only log struct that does
 * exist; the model accepts nr 12 with any size (task: "accept"). */
#define MTK_VCU_SET_LOG_OBJECT         0x4400760cu
#define MTK_VCU_SET_LOG_OBJECT_UNDERIVED 1

/* ---- _Static_assert on every size and offset (32-bit AAPCS layout) ---- */

_Static_assert(sizeof(struct mem_obj) == 0x20, "mem_obj size");
_Static_assert(offsetof(struct mem_obj, iova) == 0, "mem_obj.iova");
_Static_assert(offsetof(struct mem_obj, len) == 8, "mem_obj.len");
_Static_assert(offsetof(struct mem_obj, pa) == 0x10, "mem_obj.pa");
_Static_assert(offsetof(struct mem_obj, va) == 0x18, "mem_obj.va");

_Static_assert(sizeof(struct map_obj) == 0x10, "map_obj size");
_Static_assert(offsetof(struct map_obj, map_buf) == 0, "map_obj.map_buf");
_Static_assert(offsetof(struct map_obj, map_type) == 4, "map_obj.map_type");
_Static_assert(offsetof(struct map_obj, reserved) == 8, "map_obj.reserved");

_Static_assert(sizeof(struct gce_cmds) == 0xe808, "gce_cmds size");
_Static_assert(offsetof(struct gce_cmds, cmd) == 0, "gce_cmds.cmd");
_Static_assert(offsetof(struct gce_cmds, addr) == 0x800, "gce_cmds.addr");
_Static_assert(offsetof(struct gce_cmds, data) == 0x4800, "gce_cmds.data");
_Static_assert(offsetof(struct gce_cmds, mask) == 0x8800, "gce_cmds.mask");
_Static_assert(offsetof(struct gce_cmds, dma_offset) == 0xa800, "gce_cmds.dma_offset");
_Static_assert(offsetof(struct gce_cmds, dma_size) == 0xc800, "gce_cmds.dma_size");
_Static_assert(offsetof(struct gce_cmds, cmd_cnt) == 0xe800, "gce_cmds.cmd_cnt");

_Static_assert(sizeof(struct gce_cmdq_obj) == 0x20, "gce_cmdq_obj size");
_Static_assert(offsetof(struct gce_cmdq_obj, cmds_user_ptr) == 0, "gce_cmdq_obj.cmds_user_ptr");
_Static_assert(offsetof(struct gce_cmdq_obj, gce_handle) == 8, "gce_cmdq_obj.gce_handle");
_Static_assert(offsetof(struct gce_cmdq_obj, flush_order) == 0x10, "gce_cmdq_obj.flush_order");
_Static_assert(offsetof(struct gce_cmdq_obj, codec_type) == 0x14, "gce_cmdq_obj.codec_type");
_Static_assert(offsetof(struct gce_cmdq_obj, core_id) == 0x18, "gce_cmdq_obj.core_id");
_Static_assert(offsetof(struct gce_cmdq_obj, secure) == 0x1c, "gce_cmdq_obj.secure");

_Static_assert(sizeof(struct gce_obj) == 0x10, "gce_obj size");
_Static_assert(offsetof(struct gce_obj, gce_handle) == 0, "gce_obj.gce_handle");
_Static_assert(offsetof(struct gce_obj, flush_order) == 8, "gce_obj.flush_order");
_Static_assert(offsetof(struct gce_obj, codec_type) == 0xc, "gce_obj.codec_type");

_Static_assert(sizeof(struct share_obj) == 0x58, "share_obj size");
_Static_assert(offsetof(struct share_obj, id) == 0, "share_obj.id");
_Static_assert(offsetof(struct share_obj, len) == 4, "share_obj.len");
_Static_assert(offsetof(struct share_obj, share_buf) == 8, "share_obj.share_buf");

_Static_assert(sizeof(struct log_test_nofuse) == 0x400, "log_test_nofuse size");

_Static_assert(MTK_VCU_SET_OBJECT == 0x40587600u, "SET_OBJECT number");
_Static_assert(MTK_VCU_MVA_ALLOCATION == 0xc0207601u, "MVA_ALLOCATION number");
_Static_assert(MTK_VCU_MVA_FREE == 0xc0207602u, "MVA_FREE number");
_Static_assert(MTK_VCU_CACHE_FLUSH_ALL == 0xc0207603u, "CACHE_FLUSH_ALL number");
_Static_assert(MTK_VCU_CACHE_FLUSH_BUFF == 0xc0207604u, "CACHE_FLUSH_BUFF number");
_Static_assert(MTK_VCU_CACHE_INVALIDATE_BUFF == 0xc0207605u, "CACHE_INVALIDATE_BUFF number");
_Static_assert(MTK_VCU_PA_ALLOCATION == 0xc0207606u, "PA_ALLOCATION number");
_Static_assert(MTK_VCU_PA_FREE == 0xc0207607u, "PA_FREE number");
_Static_assert(MTK_VCU_GCE_SET_CMD_FLUSH == 0x40207608u, "GCE_SET_CMD_FLUSH number");
_Static_assert(MTK_VCU_GCE_WAIT_CALLBACK == 0x40107609u, "GCE_WAIT_CALLBACK number");
_Static_assert(MTK_VCU_GET_OBJECT == 0xc058760au, "GET_OBJECT number");
_Static_assert(MTK_VCU_GET_LOG_OBJECT == 0x4400760bu, "GET_LOG_OBJECT number");
_Static_assert(MTK_VCU_SET_MMAP_TYPE == 0x4010760du, "SET_MMAP_TYPE number");

#endif /* MTK_VCU_ABI_H */
