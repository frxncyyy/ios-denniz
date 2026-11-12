/* SPDX-License-Identifier: MIT */
/*
 * n2tr.h -- DEC02: recording wrapper around a struct n2vc_hw back end.
 *
 * The wrapper is transparent: every callback is forwarded to the inner back end (T02's fake) and
 * every register operation, power transition, interrupt wait and DMA call is written to a text
 * trace that run.sh/classify.py turns into PAGES.md and COST.md.  It exists because the T02
 * recorder only knows the twelve frozen RWVC_WIN_* names and V40 drops an access that no window
 * covers before the back end sees it; RWVC_WIN_VDEC_ALL (the one window the decode path needs,
 * DEC07/DEC04) is modelled here when the tree's V40 does not have it yet.
 *
 * Window 12 (VDEC_ALL) modelling, per the DEC07 proposal:
 *   base 0x16000000, size 0x30000
 *   offsets 0x00000..0x1ffff are powered by RWVC_BLK_VDEC_SOC (VDE)
 *   offsets 0x20000..0x2ffff are powered by RWVC_BLK_VDEC_CORE (VDE2)
 *   an access to a page whose block is off is refused with -EACCES, like every other window.
 * VDEC_CORE takes a hidden reference on VDEC_SOC, the same rule T02's fake uses for the frozen
 * windows (n2vc_hw_fake.h "Power model").
 *
 * Trace format (one event per line, no timestamps, so a re-run is byte-identical):
 *   MARK <phase>                              phase marker from the probe
 *   P <block> <on|off> <rc>
 *   R <win> <off> <value>
 *   W <win> <off> <value>
 *   WM <win> <off> <value> <mask>
 *   PLL <win> <off> <value> <mask> <rc> <reads>
 *   DLY <us>
 *   X <win> <off> <rd|wr> <rc>                refused access (power off)
 *   IRQ <irq> <timeout_ms> <rc> <status>
 *   IRQC <irq>
 *   DA <bytes> <handle> <pa>                  dma_alloc
 *   DF <handle>                               dma_free
 *   DR <handle> <off> <bytes> <W|R>           dma_rw
 *   BAD <win> <off> <rc> <done>/<count>       a forwarded batch that failed
 * Every event ends with "t<k>", the guest thread that issued it (k = order of first appearance of
 * that host thread, t0 = the AP thread).  The vendor runs its LAT and CORE work on two guest
 * threads, so the interleaving between them is scheduling-dependent while each thread's own order
 * is not; classify.py orders the "first access" table by (thread, per-thread sequence) so the
 * generated documents are identical on a re-run.
 * Windows are numeric ids 0..11 (RWVC_WIN_*) and 12 = VDEC_ALL.
 */
#ifndef N2TR_H
#define N2TR_H
#include <stdint.h>
#include <stdio.h>

#include "n2vc_hw.h"

struct n2tr;

/* Open a recording wrapper around `inner` (copied; must stay valid until n2tr_free()).
 * `path` may be NULL to record nothing (counters still work).  NULL on allocation failure. */
struct n2tr *n2tr_open(const struct n2vc_hw *inner, const char *path);
/* The wrapped back end to hand to n2vcu/n2vhost.  Valid until n2tr_free(). */
const struct n2vc_hw *n2tr_hw(struct n2tr *tr);
/* Close the trace file (flushed) and release the wrapper.  Does not close the inner back end. */
void n2tr_free(struct n2tr *tr);

/* Phase marker (e.g. "init", "header", "frame"); copied. */
void n2tr_mark(struct n2tr *tr, const char *phase);

struct n2tr_stats {
	uint64_t reg_reads, reg_writes, reg_writes_mask, polls, delays;
	uint64_t refused;                 /* accesses refused for a powered-off VDEC_ALL page */
	uint64_t forwarding_errors;       /* inner regs() returned non-zero */
	uint64_t power_on[RWVC_BLK_COUNT], power_off[RWVC_BLK_COUNT];
	uint64_t irq_waits, irq_clears;
	uint64_t dma_allocs, dma_frees, dma_rw;
	uint64_t vdec_all_reads, vdec_all_writes;
};
void n2tr_stats(const struct n2tr *tr, struct n2tr_stats *out);

/* Power state as this wrapper tracked it (outermost refcount > 0).  1/0. */
int n2tr_powered(const struct n2tr *tr, unsigned block);

/* Raw counter of trace events written (for per-START deltas). */
uint64_t n2tr_events(const struct n2tr *tr);

#endif /* N2TR_H */
