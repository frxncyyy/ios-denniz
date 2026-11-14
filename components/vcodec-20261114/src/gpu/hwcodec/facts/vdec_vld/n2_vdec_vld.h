/* SPDX-License-Identifier: MIT */
/*
 * n2_vdec_vld.h -- reference model of the MT6893 VDEC bitstream reader (VLD barrel shifter + AVC_VLD helpers) as the
 * vendor H.264 decoder (libvcodecdrv.so / libvcodec_utility.so on the Nord 2) uses it.  Owner: opus-dec2.
 * Register semantics and evidence: gpu/hwcodec/facts/VDEC_REGISTERS.md ([V] = read from the vendor binary, [I] = inferred).
 * The model is self-contained: the fake hardware (T02 / DEC03) forwards every 32-bit access inside 0x16000000..0x1602ffff
 * to n2vld_access(); a return of 1 means "claimed" (value produced / side effect done), 0 means "not a reader register,
 * use your plain register array".  It reads the bitstream through the mem callback by physical address.
 * It produces no pixels: a decode trigger (AVC_VLD 0x238) only moves the read position to the next start code.
 */
#ifndef N2_VDEC_VLD_H
#define N2_VDEC_VLD_H
#include <stdint.h>

enum { N2VLD_CORE = 0, N2VLD_LAT = 1, N2VLD_UNITS = 2 };

/* Sub-block offsets from 0x16000000 (vdec_drv_init_hw_info @0x1e1b8 in libvcodec_utility.so). [V] */
#define N2VLD_CORE_VLD      0x20000u
#define N2VLD_CORE_VLD_TOP  0x20800u
#define N2VLD_CORE_AVC_VLD  0x22000u
#define N2VLD_CORE_MISC     0x25000u
#define N2VLD_CORE_LARB     0x2e000u   /* larb4 */
#define N2VLD_LAT_VLD       0x11000u
#define N2VLD_LAT_VLD_TOP   0x11800u
#define N2VLD_LAT_AVC_VLD   0x12000u
#define N2VLD_LAT_MISC      0x10000u
#define N2VLD_LAT_LARB      0x0d000u   /* larb5 [I] */

/* returns 0 on success, nonzero if (part of) the range is not readable; unreadable bytes read as 0 */
typedef int (*n2vld_mem_read_fn)(void *ctx, uint64_t pa, uint8_t *dst, uint32_t bytes);

struct n2vld_unit {
	uint32_t vld[0x400 / 4];        /* last written values, VLD sub-block */
	uint32_t avc[0x1000 / 4];       /* last written values, AVC_VLD sub-block */
	uint32_t larb_slp;              /* larb +0x00c */
	uint64_t vstart, vend;          /* vfifo [start, end) in bytes (registers hold pa >> 6) */
	uint64_t abit;                  /* raw bit position, relative to vstart (ring) */
	uint64_t l0;                    /* raw byte index of the last latch (EPB context starts there) */
	uint32_t fetch_ready;           /* VLD 0x0e8 bit 0 */
	uint32_t loaded;                /* a read pointer has been latched */
};

struct n2vld_stats {
	uint64_t shifts, peeks, ue, se, startcode_searches, startcode_misses, epb_skipped, triggers, claimed;
};

struct n2vld {
	struct n2vld_unit u[N2VLD_UNITS];
	n2vld_mem_read_fn mem_read;
	void *mem_ctx;
	int epb;                        /* emulation-prevention removal in AVC_VLD reads (default 1) [I] */
	struct n2vld_stats st;
};

void n2vld_init(struct n2vld *m, n2vld_mem_read_fn rd, void *ctx);
/* off = physical address - 0x16000000; one aligned 32-bit access.  Returns 1 if claimed. */
int n2vld_access(struct n2vld *m, uint32_t off, int is_write, uint32_t *val);
/* debugging/tests: current read byte address of a unit */
uint64_t n2vld_rptr(const struct n2vld *m, int unit);

#endif
