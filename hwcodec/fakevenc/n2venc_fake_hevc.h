/* SPDX-License-Identifier: MIT */
/*
 * n2venc_fake_hevc.h -- HEVC frames for the fake VENC (n2venc_fake.c).  Owner: opus-hevc.
 *
 * libvcodecdrv.so writes the HEVC VPS/SPS/PPS in software (HEVC_EncodeVPS/SPS/PPS, drv 0x154fbc/0x155288/
 * 0x155ac0; the HEVC_Encode*PS_HW variants have no caller) and drives the frame with the same trigger
 * as H.264 (W 0x058 = 4, HEVC_EncodeFrame drv 0x158a16), see facts/HEVC_ENC_PROTOCOL.md.  This module
 * decides from the session's own sequence header whether a frame trigger is an HEVC one and then
 * writes an IDR slice that decodes with exactly those VPS/SPS/PPS: a small lossy intra encoder (CUs of
 * 16x16 or the nearest allowed size, DC prediction, largest allowed TBs, integer DCT residual at QP 22,
 * CABAC; deblocking switched off in the slice header when the PPS allows the override, so the decoded
 * picture equals the model's reconstruction).  The vendor SPS/PPS have PCM, transquant bypass and
 * transform skip disabled (facts/HEVC_ENC_PROTOCOL.md), so a lossless I_PCM picture as in the H.264
 * model is not possible without changing the vendor's headers.  Not supported (counted, stream still
 * written): scaling lists, tiles, WPP, bit depth > 8, chroma formats other than 4:2:0.
 *
 * The hook in n2venc_fake.c (one call in encode_frame(), see apply_hevc_patches.py) passes the core's
 * registers and DMA callbacks.  The sequence header is learned from the start of the bitstream buffer
 * (where the SEQ_HDR job left it) or given with n2venc_hevc_set_headers().
 */
#ifndef N2VENC_FAKE_HEVC_H
#define N2VENC_FAKE_HEVC_H
#include <stdint.h>

struct n2venc_hevc_io {
	void *ctx;                      /* passed back to the callbacks */
	const uint32_t *reg;            /* the core's register file (offset / 4) */
	int (*dma)(void *ctx, uint32_t pa, void *buf, uint32_t bytes, int write);   /* 0 or -errno */
	int (*avail)(void *ctx, uint32_t pa, uint32_t *avail);   /* bytes from pa to the end of its buffer */
};

/* Frame trigger.  Returns -1 when the frame is not HEVC (register 0x000 bit 2 clear: the caller encodes
 * H.264), else the number of
 * bitstream bytes written at reg[0x064/4] << 4 (0 = the buffer of *bs_cap bytes was too small: DRAM irq).
 * *bs_cap gets the capacity used. */
int n2venc_hevc_frame(const struct n2venc_hevc_io *io, uint32_t *bs_cap);

/* Optional: give the Annex-B VPS/SPS/PPS of the session (process-wide; the host runs one session).
 * Returns 0 or -EINVAL when no HEVC SPS+PPS is found. */
int n2venc_hevc_set_headers(const uint8_t *annexb, uint32_t bytes);

/* Forget the learned headers (tests). */
void n2venc_hevc_reset(void);

struct n2venc_hevc_stats {
	unsigned frames, headers_learned, defaults, unsupported, dma_errors, overflow;   /* defaults: vendor config assumed */
	unsigned qp;                    /* slice QP of the last frame */
	uint32_t width, height, last_bytes;
};
void n2venc_hevc_get_stats(struct n2venc_hevc_stats *out);

/* Pure encoder (also used by the unit test): an IDR access unit (one slice NAL, Annex-B start code)
 * for 8-bit NV12 `y`/`uv` (`pitch` bytes per line, `rows` luma lines present; missing lines repeat the
 * last one) following the headers given by n2venc_hevc_set_headers().  `recon` (optional, NULL = none)
 * receives the reconstruction as planar 4:2:0 (W*H + 2*(W/2)*(H/2) bytes, W/H from the SPS).
 * Returns bytes written to out (0 = too small or no headers). */
uint32_t n2venc_hevc_encode_idr(const uint8_t *y, const uint8_t *uv, uint32_t pitch, uint32_t rows,
				uint8_t *out, uint32_t cap, uint8_t *recon);
#endif
