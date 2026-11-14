/* SPDX-License-Identifier: MIT */
/*
 * n2vc_hw_fake.h -- T02: extra controls for the fake n2vc_hw back end.
 *
 * include/n2vc_hw.h is frozen and declares only
 *     int n2vc_hw_open_fake(struct n2vc_hw *out, const char *record_path);
 * This header adds the test-only entry points that the frozen interface has no
 * room for.  It does not change struct n2vc_hw: an integrator that only needs
 * the frozen interface includes n2vc_hw.h and never this file.
 *
 * Script grammar (one rule per line, '#' starts a comment, numbers are C
 * integers: decimal or 0x...):
 *
 *   after write WIN OFF VALUE&MASK -> set WIN OFF VALUE
 *   after write WIN OFF VALUE -> set WIN OFF VALUE
 *   irq N fires K ms after write WIN OFF [VALUE[&MASK]]
 *   read WIN OFF returns VALUE [once]
 *   irq N status VALUE
 *   set WIN OFF VALUE
 *
 * WIN is one of VENC, VENC_C1, VDEC_SYS, VDEC_BASE, VDEC_VLD, VDEC_MC,
 * VDEC_MV, VDEC_MISC, VDEC_LAT_MISC, VDEC_LAT_VLD, VDEC_RACING,
 * VDEC_SOC_GCON, VDEC_ALL (case-insensitive) or 0..12.  VDEC_ALL is 0x30000
 * bytes and its power block is per 4 KiB page (ABI rev 3); its two larb pages
 * accept a write only at offset 0x00c bit 0.  A rule without VALUE matches
 * any write to the offset.  For a masked write the trigger value is
 * (value & mask).  Rules may chain; a chain longer than 32 is an error
 * (-ELOOP) and counts as a violation.  A "read" rule takes precedence over
 * the register array until it is loaded away; "once" makes it a single read.
 * "set" is a load-time initial value (not a register access, no power needed).
 *
 * Record format (one line per operation, flushed; record_path NULL = off):
 *   POWER VENC on -> 0
 *   W VENC 0x0010 0x00000001
 *   W VENC 0x0010 0x00000003 mask 0x0000000f
 *   R VENC 0x0010 0x00000001
 *   R VENC 0x0010 0x12345678 script
 *   POLL VENC 0x0010 0x00000001 mask 0x00000001 -> ok reads=1
 *   DELAY 100 us
 *   IRQ wait 0 timeout_ms=100 -> 0 status=0x00000001
 *   IRQ clear 0
 *   SCRIPT set VENC 0x0014 = 0x00000002
 *   SCRIPT irq 0 in 5 ms
 *   DMA alloc handle=1 pa=0xda800000 bytes=4096 flags=0
 *   DMA free handle=1
 *   DMA rw handle=1 off=0x0000 bytes=16 W
 */
#ifndef N2VC_HW_FAKE_H
#define N2VC_HW_FAKE_H
#include <stdint.h>
#include "n2vc_hw.h"

struct n2vc_hw_fake_stats {
	uint64_t reads, writes, masked_writes, polls, delays;
	uint64_t irq_waits, irq_fired, irq_timeouts;
	uint64_t dma_allocs, dma_frees, dma_rw;
	uint64_t script_actions, violations;
};

/* Load (or replace) the rule set.  Returns 0, -errno.  The previous rule set
 * is kept on a parse error.  The rules are copied, so the file may be deleted
 * afterwards. */
int n2vc_hw_fake_script(struct n2vc_hw *hw, const char *path);

/* Number of register operations that hit a window whose power block is off
 * (each one also returns -EACCES to the caller). */
unsigned n2vc_hw_fake_violations(const struct n2vc_hw *hw);

void n2vc_hw_fake_stats(const struct n2vc_hw *hw, struct n2vc_hw_fake_stats *out);

/* The frozen regs() callback has no poll_timeout_us field, so the fake uses
 * this value for every RWVC_OP_POLL.  Default 100000 us (100 ms); 0 means one
 * read.  Returns 0 or -EINVAL (above 1000000 us). */
int n2vc_hw_fake_set_poll_timeout(struct n2vc_hw *hw, uint32_t us);

/* Host mirror of a DMA handle, for tests that want to inspect bytes without
 * going through dma_rw.  Returns 0, -EINVAL for an unknown handle. */
int n2vc_hw_fake_dma_host(struct n2vc_hw *hw, uint32_t handle, void **host, uint32_t *bytes);

/* Last script parse error, e.g. "line 4: bad window".  Valid until the next
 * n2vc_hw_fake_script() call; never NULL. */
const char *n2vc_hw_fake_last_error(const struct n2vc_hw *hw);

#endif
