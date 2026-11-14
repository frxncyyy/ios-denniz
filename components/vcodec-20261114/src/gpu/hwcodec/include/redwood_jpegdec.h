/* SPDX-License-Identifier: MIT */
/*
 * redwood_jpegdec.h -- user/kernel ABI of the MT6893 hardware JPEG DECODER experiment path on /dev/redwood-jpeg
 * (kernel patch 0315, opus-pool, 12 Nov 2026). Additive to the frozen redwood_jpeg.h: one new ioctl, RWJD_JOB.
 *
 * Model (Android's hybrid driver does the same: the vendor library computes the register words, the kernel programs
 * them): user space parses the JPEG and computes the decoder words (gpu/hwcodec/jdec, J04: n2jdec_fill_words() /
 * n2jdec_supplementary() with the RWJD_* addresses below); the kernel copies the bitstream into its claim, checks every
 * register against an allow-list, refuses any DMA-address register that is not 0 or inside the decoder claim, powers
 * VEN + the jpgdec clock + larb7 ports 11/12 (IOMMU bypass), writes the words in the given order (the list ends with
 * the trigger), polls 0x274, resets the block and copies the plane area out. RWJD_PROBE writes no decoder register
 * at all and returns the value of every listed offset (J04 experiments E0/E1: the power-on defaults).
 *
 * Decoder claim (free low DRAM, outside XNU's window; the RWVC pool moved to 0xA0000000 in 0313):
 *   0xDA800000 +16 MiB  bitstream (RWJD_SRC_PA; zero padded to 128 after src_bytes)
 *   0xDB800000 +1 MiB   index tables (RWJD_IDX_PA; the first 64 KiB are zeroed before every job: 3 x 16 KiB used)
 *   0xDB900000 +64 MiB  output planes (RWJD_OUT_PA; Y/Cb/Cr wherever the words put them inside it)
 *   0xDF900000 +3 MiB   guard, never a legal address (an overrun of the planes lands here, not in DPM at 0xDFE70000)
 */
#ifndef REDWOOD_JPEGDEC_H
#define REDWOOD_JPEGDEC_H
#include <stdint.h>

#define RWJD_REV        1u
#define RWJD_PA         0xda800000u
#define RWJD_BYTES      0x05400000u
#define RWJD_SRC_PA     RWJD_PA
#define RWJD_SRC_MAX    0x01000000u
#define RWJD_IDX_PA     (RWJD_PA + 0x01000000u)
#define RWJD_IDX_BYTES  0x00100000u
#define RWJD_OUT_PA     (RWJD_PA + 0x01100000u)
#define RWJD_OUT_MAX    0x04000000u
#define RWJD_LEGAL_END  (RWJD_OUT_PA + RWJD_OUT_MAX)    /* address registers must be < this */
#define RWJD_DEC_PA     0x17040000u                     /* jpgdec core 0 (mt6893.dts jpgdec@17040000) */
#define RWJD_REGS_MAX   64u
#define RWJD_TIMEOUT_MAX_MS 2000u

struct rwjd_reg { uint32_t off, value; };              /* off: 4-aligned, < 0x380, in the allow-list (kernel) */
enum { RWJD_PROBE = 1u };                              /* flags */
struct rwjd_job {                                      /* RWJD_JOB */
	uint64_t src;                                  /* user JPEG bytes (unused with RWJD_PROBE) */
	uint64_t regs;                                 /* user rwjd_reg[nregs]; with RWJD_PROBE the values are results */
	uint64_t dst;                                  /* user buffer for the first dst_bytes of the plane area (0: none) */
	uint32_t src_bytes, nregs, dst_bytes, flags;
	uint32_t timeout_ms;                           /* poll limit after the last write, <= RWJD_TIMEOUT_MAX_MS */
	uint32_t out_status;                           /* 0 = EOF (0x274 bit 0), else RWJ_E_TIMEOUT / RWJ_E_STALL (error bits) / RWJ_E_POWER / RWJ_E_BUS */
	uint32_t out_hw_us;                            /* last write -> done */
	uint32_t out_irq, out_state, out_mcu;          /* 0x274, 0x278, 0x340 before the post-job reset */
	uint32_t reserved[2];
};
#define RWJD_JOB RWJ_IOC(0xc0000000u, 4, struct rwjd_job)
#endif
