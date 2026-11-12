/* SPDX-License-Identifier: MIT */
/*
 * redwood_jpeg.h -- FROZEN user/kernel ABI of /dev/redwood-jpeg (XNU character device, mode 0666), the MT6893 hardware JPEG
 * encoder (and, once track J proves it, decoder). Owner: hwcodec-lead. Shared by the kernel (bsd/dev/nord2_jpegdev.inc), the
 * VideoToolbox plug-in back end and the tools. Fixed-width fields only; every struct is a multiple of 8 bytes.
 *
 * Verified on the phone 2026-11-11 with patch 0298 (gpu/hwcodec/runs/q10hw1): 640x480 NV12 in 0.73 ms, 1920x1080 in 4.6 ms,
 * complete JFIF files (APP0, 2 DQT, SOF0 4:2:0, 4 DHT, SOS), the hardware's size (DMA_ADDR0 - DST_ADDR0) is rounded up: the bytes after EOI are
 * padding. The KERNEL trims to the last FF D9 before copyout, so out_bytes is the exact JPEG size.
 *
 * Model: open; ioctl(RWJ_ENCODE) is synchronous and serialised in the kernel (one hardware job at a time, callers sleep
 * interruptibly); the kernel copies the picture in and the JPEG out (copyin/copyout), powers the VEN domain on demand and
 * off again RWJ_IDLE_OFF_MS after the last job. close() needs nothing.
 */
#ifndef REDWOOD_JPEG_H
#define REDWOOD_JPEG_H
#include <stdint.h>

#define RWJ_ABI_VERSION   1u
#define RWJ_MAX_WIDTH     8192u
#define RWJ_MAX_HEIGHT    8192u
#define RWJ_MAX_PIXELS    (4096u * 3072u)       /* 12.6 MP: the kernel's source buffer is 24 MiB (NV12 with 16-padding) */
#define RWJ_IDLE_OFF_MS   3000u
enum { RWJ_FMT_NV12 = 2, RWJ_FMT_NV21 = 3, RWJ_FMT_YUYV = 0, RWJ_FMT_YVYU = 1 };   /* = the hardware's codes; BGRA is converted in user space */
enum { RWJ_FLAG_EXIF_GAP = 1u };              /* hardware "EXIF" mode: see FACTS.md once K01/J02 have measured what it emits */
/* quality 1..100 is mapped by the kernel to the nearest hardware code not above it (mtk_jpeg_core.c): >=97 Q97(0x10), >=95 Q95(3),
 * >=90 Q90(7), >=85 Q85(2), >=78 Q78(6), >=72 Q72(0xf), >=60 Q60(1), >=52 Q52(0xa), >=44 Q44(0xe), >=38 Q38(0), else Q30(4) */

struct rwj_info {                             /* RWJ_INFO */
	uint32_t abi_version, max_pixels;
	uint32_t jobs_done, jobs_failed;
	uint32_t powered, last_hw_us;             /* domain on now; hardware time of the last job */
	uint32_t caps;                            /* bit 0 encoder, bit 1 decoder */
	uint32_t reserved[9];
};
struct rwj_encode {                           /* RWJ_ENCODE */
	uint32_t width, height;                   /* even, 16..RWJ_MAX_*; width * height <= RWJ_MAX_PIXELS */
	uint32_t format;                          /* RWJ_FMT_* */
	uint32_t quality;                         /* 1..100 */
	uint32_t stride;                          /* bytes per source line, >= width (>= 2 * width for YUYV) */
	uint32_t chroma_stride;                   /* NV12/NV21: bytes per chroma line; 0 = stride */
	uint32_t restart_interval;                /* MCUs, 0 = none */
	uint32_t flags;
	uint64_t src_y;                           /* user address: height lines of `stride` (YUYV: the whole picture) */
	uint64_t src_c;                           /* user address: height / 2 lines of chroma_stride (unused for YUYV) */
	uint64_t dst;                             /* user address of the output buffer */
	uint32_t dst_capacity;                    /* bytes; the kernel needs width * height / 2 + 4096 at least, else ENOSPC */
	uint32_t out_bytes;                       /* result: JPEG size (padding after EOI already cut by the kernel) */
	uint32_t out_hw_us;                       /* result: start -> done, microseconds */
	uint32_t out_status;                      /* result: 0 ok, else RWJ_E_* (errno is EIO then) */
};
enum { RWJ_E_TIMEOUT = 1, RWJ_E_STALL = 2, RWJ_E_POWER = 3, RWJ_E_BUS = 4, RWJ_E_SIZE = 5 };
struct rwj_decode {                           /* RWJ_DECODE: reserved for track J's decoder work; the kernel answers ENOTSUP until then */
	uint64_t src, dst_y, dst_c;
	uint32_t src_bytes, dst_stride, dst_capacity_y, dst_capacity_c;
	uint32_t out_width, out_height, out_format, out_status;
	uint32_t reserved[4];
};
/* BSD ioctl encoding, group 'J' */
#define RWJ_IOC(dir, nr, type)  ((dir) | ((uint32_t)(sizeof(type) & 0x1fff) << 16) | ((uint32_t)'J' << 8) | (nr))
#define RWJ_INFO    RWJ_IOC(0x40000000u, 1, struct rwj_info)       /* _IOR  */
#define RWJ_ENCODE  RWJ_IOC(0xc0000000u, 2, struct rwj_encode)     /* _IOWR */
#define RWJ_DECODE  RWJ_IOC(0xc0000000u, 3, struct rwj_decode)     /* _IOWR */
#endif
