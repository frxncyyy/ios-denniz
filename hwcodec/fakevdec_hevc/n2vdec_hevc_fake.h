/* SPDX-License-Identifier: MIT */
/*
 * n2vdec_hevc_fake.h -- opus-hevcdec: host model of the MT6893 VDEC as the vendor HEVC (H.265) decoder sees it.
 *
 * A wrapper around any struct n2vc_hw (normally T02's fake): every VDEC register access (windows 2..11 of
 * include/redwood_vcodec.h rev 2 and the proposed rev 3 RWVC_WIN_VDEC_ALL = 12, 0x16000000 + 0x30000) is served here,
 * everything else (VENC windows, DMA, power bookkeeping) is passed to the inner back end.  It contains no vendor code;
 * the register meanings it implements are in facts/HEVC_DEC_PROTOCOL.md with their [V]/[I] evidence.
 *
 * What it models:
 *   - a register file for 0x16000000..0x1602ffff (read returns the last written value unless a rule below applies);
 *   - its own HEVC bitstream reader for both units: VLD vfifo/read-pointer latch/fetch-ok (core 0x16020000, LAT
 *     0x16011000) feeding HEVC_VLD (core 0x16028000, LAT 0x16015000): peek, shift-n, ue/se, bit count, EPB removal,
 *     start-code search (HEVC_VLD(37)); written independently of DEC03's n2vld (which covers the core HEVC_VLD only);
 *   - larb SLP_CON ack, HW-break ready, WRAP_TOP idle, MISC "all DRAM idle" words;
 *   - the HEVC decode trigger -> "done" for the core (VDEC_MISC 0xa4 bit 16, IRQ RWVC_IRQ_VDEC) and the LAT
 *     (LAT_MISC 0xa4 bit 16, IRQ RWVC_IRQ_VDEC_LAT), and the status words the vendor checks after "done";
 *   - the GCE side: wait_irq() for RWVC_IRQ_VDEC / _LAT returns the done events this model raised, so V40's
 *     gce_run_list() CMD_WAIT_EVENT for VDEC_EVENT_1 / VDEC_LAT_EVENT_* completes.
 * It produces no pixels.
 */
#ifndef N2VDEC_HEVC_FAKE_H
#define N2VDEC_HEVC_FAKE_H
#include <stdint.h>
#include "n2vc_hw.h"

struct n2a32_mem;
struct n2vdec_hevc_fake;

struct n2vdec_hevc_fake_stats {
	uint64_t reads, writes, masked, polls;
	uint64_t core_triggers, lat_triggers, core_irqs, lat_irqs, irq_waits, irq_timeouts;
	uint64_t dma_bytes, dma_peak;   /* DMA bytes allocated through this back end (vendor + AP buffers), now / peak */
};

/* Wrap `inner` (taken over: out->close() closes it too).  `out` is the back end to hand to n2vhost_start().
 * trace_path: NULL or a file receiving one line per VDEC access (repeated identical reads collapsed).  0 or -errno. */
int n2vdec_hevc_fake_open(struct n2vc_hw *out, const struct n2vc_hw *inner, const char *trace_path,
			  struct n2vdec_hevc_fake **model);
/* The guest memory, needed to read bitstream bytes the way coherent DMA would (n2vhost_mem() after start). */
void n2vdec_hevc_fake_set_mem(struct n2vdec_hevc_fake *m, struct n2a32_mem *mem);
void n2vdec_hevc_fake_stats(struct n2vdec_hevc_fake *m, struct n2vdec_hevc_fake_stats *out);
/* Free text marker into the trace (phase names). */
void n2vdec_hevc_fake_mark(struct n2vdec_hevc_fake *m, const char *text);
#endif
