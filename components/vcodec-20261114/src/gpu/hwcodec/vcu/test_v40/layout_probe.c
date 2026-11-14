/* SPDX-License-Identifier: MIT */
/*
 * layout_probe.c -- V40's independent oracle for the 32-bit /dev/vcu ABI.
 *
 * This file is *not* compiled for the host and never includes mtk_vcu_abi.h.  run_tests.sh
 * compiles it with
 *
 *     clang --target=armv7a-linux-gnueabi -c -I <K>/include/uapi layout_probe.c
 *
 * against the kernel's own uapi/linux/mtk_vcu_controls.h, extracts the .probe table from the
 * object with llvm-objcopy and writes it into probe_values.h.  t_v40.c then checks, for every
 * size, offset and request number, that
 *
 *     probe value  ==  mtk_vcu_abi.h  ==  sizeof/offsetof/literal in the test
 *
 * so a mistake in the header or in the test's hand-built byte layout cannot hide behind the
 * same reading of the header.
 *
 * The kernel header needs u8/u32/s32/u64, IS_ENABLED() and the _IOC macros of
 * asm-generic/ioctl.h; ARM AAPCS gives 64-bit members 8-byte alignment, which is the whole point
 * of the probe, so nothing here may change the alignment (no #pragma pack, no -mabi override).
 *
 * The trailing 0x56433430 sentinel catches a table/extractor length mismatch.
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
typedef unsigned long long compat_u64;
typedef unsigned int compat_ulong_t;

#define offsetof(t, m) __builtin_offsetof(t, m)
#define IS_ENABLED(x) 1

#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT + _IOC_SIZEBITS)
#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U
#define _IOC(dir, type, nr, size) \
	(((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) | \
	 ((nr) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT))
#define _IOW(type, nr, size) _IOC(_IOC_WRITE, (type), (nr), sizeof(size))
#define _IOWR(type, nr, size) _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(size))

#include "linux/mtk_vcu_controls.h"

const unsigned probe_table[] __attribute__((section(".probe"), used)) = {
	sizeof(struct mem_obj),                       /*  0 */
	offsetof(struct mem_obj, iova),               /*  1 */
	offsetof(struct mem_obj, len),                /*  2 */
	offsetof(struct mem_obj, pa),                 /*  3 */
	offsetof(struct mem_obj, va),                 /*  4 */
	sizeof(struct map_obj),                       /*  5 */
	offsetof(struct map_obj, map_buf),            /*  6 */
	offsetof(struct map_obj, map_type),           /*  7 */
	offsetof(struct map_obj, reserved),           /*  8 */
	sizeof(struct gce_cmds),                      /*  9 */
	offsetof(struct gce_cmds, cmd),               /* 10 */
	offsetof(struct gce_cmds, addr),              /* 11 */
	offsetof(struct gce_cmds, data),              /* 12 */
	offsetof(struct gce_cmds, mask),              /* 13 */
	offsetof(struct gce_cmds, dma_offset),        /* 14 */
	offsetof(struct gce_cmds, dma_size),          /* 15 */
	offsetof(struct gce_cmds, cmd_cnt),           /* 16 */
	sizeof(struct gce_cmdq_obj),                  /* 17 */
	offsetof(struct gce_cmdq_obj, cmds_user_ptr), /* 18 */
	offsetof(struct gce_cmdq_obj, gce_handle),    /* 19 */
	offsetof(struct gce_cmdq_obj, flush_order),   /* 20 */
	offsetof(struct gce_cmdq_obj, codec_type),    /* 21 */
	offsetof(struct gce_cmdq_obj, core_id),       /* 22 */
	offsetof(struct gce_cmdq_obj, secure),        /* 23 */
	sizeof(struct gce_obj),                       /* 24 */
	offsetof(struct gce_obj, gce_handle),         /* 25 */
	offsetof(struct gce_obj, flush_order),        /* 26 */
	offsetof(struct gce_obj, codec_type),         /* 27 */
	sizeof(struct share_obj),                     /* 28 */
	offsetof(struct share_obj, id),               /* 29 */
	offsetof(struct share_obj, len),              /* 30 */
	offsetof(struct share_obj, share_buf),        /* 31 */
	sizeof(struct log_test_nofuse),               /* 32 */
	VCU_SET_OBJECT,                               /* 33 */
	VCU_MVA_ALLOCATION,                           /* 34 */
	VCU_MVA_FREE,                                 /* 35 */
	VCU_CACHE_FLUSH_ALL,                          /* 36 */
	VCU_CACHE_FLUSH_BUFF,                         /* 37 */
	VCU_CACHE_INVALIDATE_BUFF,                    /* 38 */
	VCU_PA_ALLOCATION,                            /* 39 */
	VCU_PA_FREE,                                  /* 40 */
	VCU_GCE_SET_CMD_FLUSH,                        /* 41 */
	VCU_GCE_WAIT_CALLBACK,                        /* 42 */
	VCU_GET_OBJECT,                               /* 43 */
	VCU_GET_LOG_OBJECT,                           /* 44 */
	VCU_SET_MMAP_TYPE,                            /* 45 */
	(unsigned)CMD_READ,                           /* 46 */
	(unsigned)CMD_WRITE,                          /* 47 */
	(unsigned)CMD_POLL_REG,                       /* 48 */
	(unsigned)CMD_WAIT_EVENT,                     /* 49 */
	(unsigned)CMD_MEM_MV,                         /* 50 */
	(unsigned)CMD_POLL_ADDR,                      /* 51 */
	(unsigned)CMD_SEC_WRITE,                      /* 52 */
	(unsigned)CMD_MAX,                            /* 53 */
	(unsigned)VENC_EOF,                           /* 54 */
	(unsigned)VENC_EOF_C1,                        /* 55 */
	(unsigned)VDEC_LAT_EVENT_0,                   /* 56 */
	(unsigned)VDEC_EVENT_COUNT,                   /* 57 */
	0x56433430u                                   /* 58: "VC40" sentinel */
};
