/* SPDX-License-Identifier: MIT */
/*
 * n2vdec_fmt_fake.h -- opus-fmt: host model of the MT6893 VDEC sub-blocks the vendor's VP8 / VP9 / AV1 / MPEG-1/2/4 /
 * H.263 decoders touch beyond what opus-hevcdec's model (fakevdec_hevc) covers.
 *
 * A wrapper around any struct n2vc_hw, normally the HEVC model (which wraps T02's fake): EVERY op is passed to the inner
 * back end first (kernel-parity checks, HEVC model state, records); for the registers listed in facts/FORMATS.md section 4
 * this model then overrides the value read back (or the poll result).  No vendor code; no pixels.
 *
 * Modelled (offsets from 0x16000000; [I] = from the vendor's polling code, see FORMATS.md):
 *   - second bitstream reader BS2 / "VP8VLD2" at 0x26800 (VP8 partition 2): vfifo (0xb4/0xb8), rptr latch (0xb0, 0x108),
 *     init fetch (0x8c bit 20) -> fetch ok (0xe8 bit 0), process flag (0xf4) idle;
 *   - the core VLD (0x20000) fetch-ok / process flag, in case the inner model is plain T02 (no HEVC model);
 *   - VP8 / VP9 / AV1 / MPEG "decode finished" status words the vendor polls after its trigger: answered "done";
 *   - larb SLP_CON ack (0x0d00c / 0x2e00c), HW-break ready (MISC/LAT_MISC 0x104), MISC 0x110..0x13c idle.
 */
#ifndef N2VDEC_FMT_FAKE_H
#define N2VDEC_FMT_FAKE_H
#include <stdint.h>
#include "n2vc_hw.h"

struct n2a32_mem;
struct n2vdec_fmt_fake;

struct n2vdec_fmt_fake_stats { uint64_t ops, overrides, triggers; };

int n2vdec_fmt_fake_open(struct n2vc_hw *out, const struct n2vc_hw *inner, const char *trace_path,
			 struct n2vdec_fmt_fake **model);
void n2vdec_fmt_fake_set_mem(struct n2vdec_fmt_fake *m, struct n2a32_mem *mem);
void n2vdec_fmt_fake_stats(struct n2vdec_fmt_fake *m, struct n2vdec_fmt_fake_stats *out);
#endif
