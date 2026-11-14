/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dec_ap_n2ap.h -- daemon/dec's adapter from the decoder service's AP ops to DEC01's
 * ap/n2ap_vdec.c (the user-space V4L2 decoder driver).  The daemon owns the vhost/vcu and passes
 * them here; one adapter drives one VDEC session at a time, like vcodecd's encoder session.
 *
 * Host-compiled in DEC05's suite (compile + a stand-in exercise); the real vendor path is DEC01's
 * probe and the phone run.  No print from this file.
 */
#ifndef N2VCD_DEC_AP_N2AP_H
#define N2VCD_DEC_AP_N2AP_H

#include <stdint.h>

#include "n2vcd_dec_service.h"

struct n2vcu;
struct n2vc_hw;
struct n2a32_mem;
struct n2vcd_dec_ap_n2ap;

struct n2vcd_dec_ap_n2ap_cfg {
	struct n2vcu *vcu;              /* V40's /dev/vcu model (required) */
	const struct n2vc_hw *hw;       /* hardware back end (required) */
	unsigned timeout_ms;            /* per IPI wait; 0 = n2ap_vdec default (6000) */
	uint32_t share_base;            /* V34 ion share id base; 0 = 0x580 (64 slots) */
	/* The daemon's guest-stop check (vcodecd has one already: n2vhost_join + n2shim failure).
	 * Without it the service cannot turn a dead vpud into the fatal exit-70 path. */
	int (*guest_stopped)(void *ctx, uint32_t *code);
	void *guest_ctx;
	/* opus-decint: host tests only (vcodecd --fake --fake-vdec-irq): n2ap_vdec answers WAITISR
	 * "done" without waiting, because T02's fake raises no VDEC interrupt.  Never set on the phone. */
	int fake_irq;
	/* opus-decint: the daemon's guest memory (n2vhost_mem): the client bitstream's guest mirror is refreshed from the
	 * device before every START (bs_refresh in the .c).  Required. */
	struct n2a32_mem *mem;
	/* opus-decint (perf): DEC04 write-combining of guest register stores during decoder sessions (vcu-global, the
	 * encoder is never active at the same time).  0 = off, 1 = on, 2 = alternate: on for every second session (A/B in
	 * one boot).  The session's state is logged by the trace hook as event "wc". */
	int write_combining;
	/* opus-decint: optional n2ap_vdec trace hook (vcodecd logs the first events of a session) */
	void (*trace)(void *arg, const char *event, uint32_t a, uint32_t b, uint32_t c);
	void *trace_arg;
};

struct n2vcd_dec_ap_n2ap *n2vcd_dec_ap_n2ap_create(const struct n2vcd_dec_ap_n2ap_cfg *cfg);
void n2vcd_dec_ap_n2ap_destroy(struct n2vcd_dec_ap_n2ap *a);
const struct n2vcd_dec_ap_ops *n2vcd_dec_ap_n2ap_ops(void);

/* Diagnostics. */
uint32_t n2vcd_dec_ap_n2ap_inputs(const struct n2vcd_dec_ap_n2ap *a);
uint32_t n2vcd_dec_ap_n2ap_pictures(const struct n2vcd_dec_ap_n2ap *a);
uint32_t n2vcd_dec_ap_n2ap_flushes(const struct n2vcd_dec_ap_n2ap *a);

#endif /* N2VCD_DEC_AP_N2AP_H */
