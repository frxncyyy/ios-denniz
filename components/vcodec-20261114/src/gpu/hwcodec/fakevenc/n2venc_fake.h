/* SPDX-License-Identifier: MIT */
/*
 * n2venc_fake.h -- behavioural model of the MT6893 VENC core (H.264 path) for host runs of the unmodified
 * vendor encoder (libvcodecdrv.so under the AArch32 interpreter).  Owner: opus-venc.
 *
 * It sits between the /dev/vcu model (V40) / AP side (V41) and any other n2vc_hw back end (normally T02's
 * n2vc_hw_open_fake): every operation is forwarded to the inner back end (so its power rules, violation
 * count and recorder keep working), and the RWVC_WIN_VENC / RWVC_WIN_VENC_C1 windows additionally get the
 * VENC behaviour the library depends on (facts/VENC_PROTOCOL.md):
 *
 *   0x0a8 soft reset      write 0 / 1, reads back the last value
 *   0x058 trigger         bit 2 (FRM) encodes one frame: an IDR slice of I_PCM macroblocks built from the
 *                         NV12 source at 0x06c/0x070 (<<4), written to the bitstream buffer at 0x064 (<<4);
 *                         BYTE_CNT 0x098 = slice bytes, IRQ status 0x05c |= 0x4, one interrupt on the
 *                         core's RWVC_IRQ_VENC(_C1) line (the GCE event VENC_EOF / VENC_EOF_C1)
 *   0x05c irq status      read only; bits SPS 0x1, PPS 0x2, FRM 0x4, DRAM(bitstream full) 0x8
 *   0x060 irq ack         write 1s clear 0x05c bits
 *   POLL ops              evaluated on the model (the inner fake's array does not see the model's status)
 *
 * The bitstream is synthetic (no motion search, no rate control) but valid H.264: an IDR access unit whose
 * slice header follows the SPS/PPS the library wrote itself.  Those are learned from n2venc_fake_set_headers()
 * or, failing that, by scanning the start of the bitstream buffer when the frame is triggered (the vendor
 * writes SPS/PPS there in software for the SEQ_HDR job); without either, Baseline defaults are used.
 * Only CAVLC (entropy_coding_mode_flag 0) and 4:2:0 are produced; anything else is counted in `unsupported`.
 */
#ifndef N2VENC_FAKE_H
#define N2VENC_FAKE_H
#include <stdint.h>
#include "n2vc_hw.h"

struct n2venc_fake;

/* Build `outer` on top of `inner` (which must stay valid until outer.close).  outer.close() closes the inner
 * back end too.  Returns 0 or -errno.  `log_path` NULL = no own log (the inner recorder still records). */
int n2venc_fake_wrap(const struct n2vc_hw *inner, struct n2vc_hw *outer, struct n2venc_fake **fake,
		     const char *log_path);

/* Give the model the Annex-B SPS/PPS of the session (optional, see above).  Returns 0 or -EINVAL. */
int n2venc_fake_set_headers(struct n2venc_fake *f, const uint8_t *annexb, uint32_t bytes);

struct n2venc_fake_stats {
	unsigned resets, triggers, frames, irqs_raised, irqs_taken, acks, polls, poll_fail;
	unsigned overflow, dma_errors, unsupported, headers_learned;
	uint32_t last_bytes, last_bs_pa, last_luma_pa, last_chroma_pa, last_mb_w, last_mb_h;
};
void n2venc_fake_get_stats(const struct n2venc_fake *f, struct n2venc_fake_stats *out);

/* The fake that owns `hw` (an outer struct returned by n2venc_fake_wrap), else NULL. */
struct n2venc_fake *n2venc_fake_of(const struct n2vc_hw *hw);
#endif
