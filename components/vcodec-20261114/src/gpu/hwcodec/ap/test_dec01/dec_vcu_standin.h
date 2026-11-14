/* SPDX-License-Identifier: MIT */
/*
 * dec_vcu_standin.h -- a stand-in for V40's vcu/n2vcu.h, just enough for ap/n2ap_vdec.c:
 * n2vcu_ap_register, n2vcu_ap_send, n2vcu_ap_pending, n2vcu_ap_wait_dispatched, n2vcu_map_dm_addr and
 * n2vcu_dmem, plus the *daemon* side (fetch / reply / request) that the product gets from the
 * interpreted vendor library.
 *
 * Why a stand-in: the unit tests must exercise the AP client's protocol logic deterministically, with a
 * daemon whose replies and requests the test scripts line by line.  Using V40's real model here would
 * test V40 as well and would need a guest CPU to drive VCU_GET_OBJECT.  The real-vendor path is
 * covered separately by probe_dec01.c, which uses V40 through host/n2vhost.h.
 *
 * Semantics copied from V40's n2vcu.h: the AP handler runs on the daemon's thread and its return value
 * says whether the message was acknowledged; `buf` is a private copy that is written back to the
 * daemon's share area; n2vcu_ap_wait_dispatched() waits until the daemon has entered its fetch for
 * that IPI id after the send.
 *
 * Owner: DEC01.  Test-only.
 */
#ifndef DEC_VCU_STANDIN_H
#define DEC_VCU_STANDIN_H
#include <pthread.h>
#include <stdint.h>
#include "n2a32.h"
#include "n2vc_hw.h"
#include "vcu/n2vcu.h"   /* the prototypes and the n2vcu_ipi_handler typedef; struct n2vcu is opaque */

#define DECV_IPI_MAX 50
#define DECV_SHARE_MAX 80

struct decv_msg {
	int ipi_id;
	uint32_t len;
	uint8_t buf[DECV_SHARE_MAX];
};

/* The opaque tag from vcu/n2vcu.h, defined here for the tests. */
struct n2vcu {
	struct n2a32_mem *mem;
	struct n2vc_hw hw;
	pthread_mutex_t lock;
	pthread_cond_t cv;                 /* a message was queued / the daemon fetched one */
	struct decv_msg q[DECV_IPI_MAX];
	int q_pending[DECV_IPI_MAX];
	n2vcu_ipi_handler fn[DECV_IPI_MAX];
	void *fn_ctx[DECV_IPI_MAX];
	int fetched[DECV_IPI_MAX];         /* "the daemon has entered GET_OBJECT for this id" */
	uint8_t *dmem;
	uint32_t dmem_guest, dmem_bytes, dmem_used;
	unsigned sends, fetches, replies, requests, overwritten, no_handler, bad_len;
};

struct n2vcu *decv_create(struct n2a32_mem *mem, const struct n2vc_hw *hw, uint32_t dmem_bytes);
void decv_destroy(struct n2vcu *v);

/* ---- the daemon side (what the interpreted libvpud_vcodec.so does) ------------------------- */

/* Take one queued AP message for `ipi_id`; blocks up to timeout_ms.  Returns 0 and fills *m, or
 * -ETIMEDOUT.  Marks the id as fetched (releases n2vcu_ap_wait_dispatched). */
int decv_daemon_fetch(struct n2vcu *v, int ipi_id, struct decv_msg *m, unsigned timeout_ms);

/* Same, for any IPI id (the daemon's single receiver thread serves every service). */
int decv_daemon_fetch_any(struct n2vcu *v, struct decv_msg *m, unsigned timeout_ms);

/* Send a reply for `ipi_id` through the registered AP handler.  `m` is copied in, and the handler's
 * write-back is copied into *m, which is what the kernel does with the share area (mtk_vcu.c:1814). */
int decv_daemon_reply(struct n2vcu *v, int ipi_id, struct decv_msg *m);

/* Send a VCU -> AP *request* (LOCK/WAITISR/GET/PUT) and return the handler's return value (1 = "no
 * ack", as vdec_vcu_if.c returns), with the write-back in *m.  Runs synchronously on the caller's
 * thread, like the kernel's handler on the daemon's thread. */
int decv_daemon_request(struct n2vcu *v, int ipi_id, struct decv_msg *m);

/* Bump allocator for the daemon's own memory in the DMEM (the vendor's mem_inst[]).  Returns the guest
 * address, 0 when the DMEM is full. */
uint32_t decv_dmem_alloc(struct n2vcu *v, uint32_t bytes, uint32_t align);

#endif /* DEC_VCU_STANDIN_H */
