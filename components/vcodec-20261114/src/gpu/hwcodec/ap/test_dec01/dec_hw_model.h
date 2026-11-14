/* SPDX-License-Identifier: MIT */
/*
 * dec_hw_model.h -- a small register/power/DMA model for struct n2vc_hw (include/n2vc_hw.h), used by
 * test_dec01 to exercise the LOCK/UNLOCK power role of ap/n2ap_vdec.c for real: the racing-word
 * save/restore (68 words at VDEC_RACING+0x100) and the HW-break sequence of
 * mt6885/mtk_vcodec_dec_pm.c:296-460.
 *
 * It is deliberately *not* T02's fake (tests/hw/n2vc_hw_fake.c): this one is 120 lines, has no script
 * language, and exists so that a failed expectation points at n2ap_vdec.c rather than at a shared
 * component.  Its power model follows the fake's (a CORE reference implies the SOC block, like the
 * kernel's hidden parent reference) so that the same expectations hold on both.
 *
 * Owner: DEC01.  Test-only; nothing in the product links it.
 */
#ifndef DEC_HW_MODEL_H
#define DEC_HW_MODEL_H
#include <stdint.h>
#include "n2vc_hw.h"

#define DECM_REGS 16384u   /* bytes per window; VDEC_MISC is 0x4000 */
#define DECM_DMA  8u

struct decm_dma {
	uint32_t handle, pa, bytes;
	uint8_t *host;
	int used;
};

struct decm {
	struct n2vc_hw hw;
	/* register file, one array per window: 4 KiB is enough for every window of the frozen list */
	uint32_t reg[RWVC_WIN_COUNT][DECM_REGS / 4];
	unsigned user_refs[RWVC_BLK_COUNT];
	unsigned hidden_refs[RWVC_BLK_COUNT];
	unsigned power_on_calls[RWVC_BLK_COUNT];
	unsigned power_off_calls[RWVC_BLK_COUNT];
	unsigned power_denied;      /* an off with no reference */
	unsigned reg_ops;           /* successful register operations */
	unsigned win_ops[RWVC_WIN_COUNT];   /* register operations per window */
	/* Which block the VDEC_RACING window needs.  The frozen list (redwood_vcodec.h, K02's table and
	 * T02's fake) says VDEC_CORE; mtk_vcodec_dec_pm.c restores/saves those words from *both* the
	 * CORE and the LAT clock path, which is only possible if the registers sit in the SOC's range.
	 * The model defaults to the frozen mapping and a test runs both. */
	unsigned racing_blk;
	unsigned access_denied;     /* an access to a window whose block is off */
	unsigned unknown_window;
	unsigned reads, writes, polls, delays;
	unsigned dma_rw_to_dev, dma_rw_from_dev;   /* dma_rw(..., write=1) / (..., write=0) */
	unsigned irq_waits;
	unsigned irq_timeout_ms;    /* the timeout of the last wait_irq call */
	unsigned irq_timeout_after; /* 1 = the next wait_irq returns -ETIMEDOUT */
	uint32_t irq_status_value;
	unsigned irq;
	unsigned irq_clear_calls;
	struct decm_dma dma[DECM_DMA];
	uint32_t next_pa;
	uint32_t next_handle;
	/* The HW-break poll: after the write to MISC+0x100 with bit 0 the model sets 0x104 to
	 * `hwbreak_done` (0x11 for the CORE path, 0x11 for LAT as well).  A test can set it to 0 to
	 * exercise the timeout path (which then takes 1.02 s; only one test does that). */
	uint32_t hwbreak_done;
	unsigned hwbreak_polls;
	unsigned hwbreak_acks;     /* a write of bit 0 to WIN+0x100 raised WIN+0x104 */
};

/* Free the DMA buffers (call before discarding the model). */
void decm_destroy(struct decm *m);

/* Initialise the model; `pa_base` is the first physical address handed out (page steps of 0x1000). */
void decm_init(struct decm *m, uint32_t pa_base);

/* Number of accesses to `win` (any op). */
unsigned decm_window_ops(const struct decm *m, unsigned win);
/* The register value (no power check; for test read-back). */
uint32_t decm_get(const struct decm *m, unsigned win, uint32_t off);
void decm_set(struct decm *m, unsigned win, uint32_t off, uint32_t v);
/* Was the block ever powered? Number of power(on) calls. */
unsigned decm_power_on(const struct decm *m, unsigned blk);
unsigned decm_power_off(const struct decm *m, unsigned blk);
/* 1 when the block (or its parent) is on. */
int decm_is_on(const struct decm *m, unsigned blk);
/* 1 when every block is off again. */
int decm_all_off(const struct decm *m);

#endif /* DEC_HW_MODEL_H */
