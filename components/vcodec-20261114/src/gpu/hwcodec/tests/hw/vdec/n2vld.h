/* SPDX-License-Identifier: MIT */
/*
 * n2vld.h -- DEC03: model of the MT6893 VDEC bitstream reader (VLD) and AVC_VLD / HEVC_VLD
 * syntax engines for the host fake hardware.
 *
 * What the vendor library actually does (facts and evidence in REGISTERS.md; addresses are
 * libvcodecdrv.so / libvcodec_utility.so exports):
 *   - SPS/PPS/slice-header parsing is software.  parseSliceHeaderHW @0x110eb8 has no caller in
 *     this build; the live slice header is parseSliceHeader (RBSPGetBits/RBSPGetUE).
 *   - Per slice the live path still uses the hardware reader:
 *       H264_HAL_InitH264BarrelShift -> VLD vfifo + read pointer + fetch handshake
 *       (the exported H264_HAL_InitH264BarrelShifter @0xffd18 writes the same registers);
 *       H264_HAL_GetStartCode_8530   -> AVC_VLD start-code search;
 *       H264_HAL_UeCodeNum           -> AVC_VLD 0x8f (first_mb_in_slice of the next slice);
 *       H264_HAL_Read_VldRPtr        -> VLD 0x3b/0x34/0x3f readback.
 *   - AVC_VLD reg 0        = next 32 bits, no advance (big-endian, MSB first);
 *     AVC_VLD reg 1..32    = shift n bits, then return the next 32 bits;
 *     AVC_VLD reg 0x8f/0x90 = ue(v) / se(v);
 *     AVC_VLD reg 0x21     = bits 16..21: bit position inside the barrel (trailing bits);
 *     AVC_VLD reg 0xa3     = written 1 then 0 by the init sequence.
 *   - HEVC_VLD at 0x16028000 is the same bit engine with ue/se at 0x26/0x27 and the trailing
 *     position in 0x21 bits 8..10.
 *   - The LAT core (vendor core index 3) has its own VLD/AVC_VLD at 0x16011000/0x16012000.
 *
 * Emulation-prevention bytes (00 00 03 before a byte <= 03) are removed as bits are read, using
 * the latch/search position as the context start.  The byte stream is a ring
 * [vfifo_start, vfifo_end); reads wrap at the end.
 *
 * Register numbers in this header are word indices inside each block; the hook receives byte
 * offsets (or absolute offsets for RWVC_WIN_VDEC_ALL).  Every semantic is marked [V] or [I] in
 * REGISTERS.md with its evidence.
 */
#ifndef N2VLD_H
#define N2VLD_H
#include <stdint.h>

/* Sub-block offsets from 0x16000000 (vdec_drv_init_hw_info @0x1e1b8 table). */
#define N2VLD_VLD_OFF       0x20000u   /* core VLD */
#define N2VLD_AVC_VLD_OFF   0x22000u   /* core AVC_VLD */
#define N2VLD_HEVC_VLD_OFF  0x28000u   /* HEVC_VLD (inside the core MISC window) */
#define N2VLD_CORE_MISC_OFF 0x25000u
#define N2VLD_LAT_MISC_OFF  0x10000u
#define N2VLD_LAT_VLD_OFF   0x11000u
#define N2VLD_LAT_AVC_VLD_OFF 0x12000u
#define N2VLD_LARB4_OFF     0x2e000u   /* SMI larb4, sleep-protect word at +0x00c */
#define N2VLD_LARB5_OFF     0x0d000u   /* SMI larb5, sleep-protect word at +0x00c */
#define N2VLD_WIN_BYTES     0x1000u

/* Window indices of include/redwood_vcodec.h (rev 2) and the proposed rev 3 VDEC_ALL window.
 * The model maps these by default; n2vld_map_window() overrides or extends the table. */
#define N2VLD_WIN_VDEC_VLD       4u
#define N2VLD_WIN_VDEC_MISC      7u
#define N2VLD_WIN_VDEC_LAT_MISC  8u
#define N2VLD_WIN_VDEC_LAT_VLD   9u
#define N2VLD_WIN_VDEC_ALL       12u

/* Units: core (vendor core index 0) and LAT (vendor core index 3). */
enum { N2VLD_UNIT_CORE = 0, N2VLD_UNIT_LAT = 1, N2VLD_UNITS = 2 };

/* VLD registers the model interprets (word index). */
#define N2VLD_VLD_FETCH_CTRL  0x23u   /* bit 20 starts a fetch, bit 23 loads the barrel shifter */
#define N2VLD_VLD_RPTR        0x2cu   /* read pointer, byte PA                                */
#define N2VLD_VLD_VFIFO_START 0x2du   /* vfifo start address >> 6                            */
#define N2VLD_VLD_VFIFO_END   0x2eu   /* vfifo end address >> 6 (ring end)                   */
#define N2VLD_VLD_SRAM        0x3bu   /* [7:0] sram rd, [15:8] sram wr, [25:24] word index    */
#define N2VLD_VLD_SRAM2       0x34u   /* [23:16] sram level correction                       */
#define N2VLD_VLD_FETCH_OK    0x3au   /* bit 0: fetch complete (bVDecWaitVldFetchOk)         */
#define N2VLD_VLD_PROC        0x3du   /* bit 15: busy flag valid; read 0 in the model        */
#define N2VLD_VLD_DRAMPTR     0x3fu   /* DRAM fetch pointer used by vdec_hal_read_vld_rptr   */
#define N2VLD_VLD_RESET       0x42u   /* written 0x100 then 0; loads the reader at VLD 0x2c  */
#define N2VLD_VLD_DCM         0x50u   /* bit 20: dual-pipe/field mode                        */
#define N2VLD_VLD_0CC         0x33u   /* written 0 by vdec_hal_vld_init_barrel_shift         */

/* AVC_VLD registers the model interprets (word index). */
#define N2VLD_AVC_PEEK        0x00u   /* next 32 bits, no advance                            */
#define N2VLD_AVC_CTRL        0x21u   /* bits 16..21: bit position inside the barrel          */
#define N2VLD_AVC_TRIG        0x8eu   /* write bit 0: decode trigger                          */
#define N2VLD_AVC_UE          0x8fu   /* ue(v)                                                */
#define N2VLD_AVC_SE          0x90u   /* se(v)                                                */
#define N2VLD_AVC_RESET       0xa3u   /* 1 then 0 resets the syntax engine                    */
#define N2VLD_AVC_SC_STAT     0xb2u   /* start-code status; bit 11 = accept after a timeout   */
#define N2VLD_AVC_SC_SEARCH   0xb6u   /* write 1: search the next 00 00 01; bit 0 = busy      */
#define N2VLD_AVC_BITS_MAX    32u     /* word n in 1..32 = consume n bits, return next 32     */

/* HEVC_VLD 0x16028000: same bit engine, its own ue/se words and 0x21 layout. */
#define N2VLD_HEVC_UE         0x26u
#define N2VLD_HEVC_SE         0x27u

/* Returned by ue/se when the bitstream has no terminating one bit.  The vendor never sees it:
 * vdec_hal_h264_uecodenum first reads AVC_VLD 0 and returns -1 when that word is zero. */
#define N2VLD_ERR 0xffffffffu

struct n2vld;
struct n2vld_stats {
	uint64_t reads, writes;
	uint64_t vld_reads, vld_writes;
	uint64_t avc_reads, avc_writes;
	uint64_t hevc_reads, hevc_writes;
	uint64_t ue, se, bits_read, peeks, shifts;
	uint64_t reloads, bus_errors;
	uint64_t epb_skipped, startcode_searches, startcode_misses, triggers;
};

/* Where the model gets the bytes of a vfifo ring.  read_pa copies `bytes` from physical address
 * `pa`; the window is one DMA buffer in every vendor use.  Returns 0 or -errno. */
struct n2vld_bus {
	int (*read_pa)(void *ctx, uint32_t pa, void *dst, uint32_t bytes);
	void *ctx;
};

/* bus may be NULL: the model then only serves n2vld_set_bytes() loads (unit tests). */
struct n2vld *n2vld_create(const struct n2vld_bus *bus);
void n2vld_destroy(struct n2vld *v);

/* Forget the bit positions and cached bytes; register shadows stay. */
void n2vld_reset(struct n2vld *v);

/* Override the window table entry for `win` (base absolute address, window bytes).  Returns 0,
 * -EINVAL, or -ENOSPC when more than six windows are mapped. */
int n2vld_map_window(struct n2vld *v, unsigned win, uint32_t base, uint32_t bytes);

/* The T02 hook: returns 1 when handled (*value set for a read), 0 for "default handling",
 * -errno on a model error.  The default table maps win 4/7/8/9 to the VDEC sub-blocks and win 12
 * to the whole 0x16000000..0x1602ffff range; every other window/offset is left to the fake.
 * The caller must have validated the window and powered the block before calling the hook. */
int n2vld_hook(void *ctx, unsigned win, uint32_t off, int is_write, uint32_t *value);

/* Test/driver helpers.  n2vld_set_bytes() installs the exact bytes the hardware would read
 * (start code and emulation-prevention bytes included; the model strips 00 00 03 in-band) and
 * rewinds both units.  Returns 0 or -ENOMEM. */
int n2vld_set_bytes(struct n2vld *v, const void *bytes, uint32_t len);
int n2vld_set_vfifo(struct n2vld *v, uint32_t start, uint32_t end);   /* core, as VLD 0x2d/0x2e */
int n2vld_set_vfifo_unit(struct n2vld *v, unsigned unit, uint32_t start, uint32_t end);
const uint8_t *n2vld_rbsp(const struct n2vld *v, uint32_t *len);       /* direct bytes, EPB removed */
uint32_t n2vld_rbsp_len(const struct n2vld *v);
uint64_t n2vld_bitpos(const struct n2vld *v);                          /* core */
uint64_t n2vld_bitpos_unit(const struct n2vld *v, unsigned unit);
uint32_t n2vld_vfifo_start(const struct n2vld *v);
uint32_t n2vld_vfifo_end(const struct n2vld *v);
uint32_t n2vld_vfifo_start_unit(const struct n2vld *v, unsigned unit);
uint32_t n2vld_vfifo_end_unit(const struct n2vld *v, unsigned unit);
uint32_t n2vld_rptr_unit(const struct n2vld *v, unsigned unit);        /* current byte PA */
uint32_t n2vld_reg(const struct n2vld *v, unsigned win, uint32_t reg); /* shadow value only */
void n2vld_stats(const struct n2vld *v, struct n2vld_stats *out);
const char *n2vld_last_error(const struct n2vld *v);   /* never NULL, "" when none */

#endif
