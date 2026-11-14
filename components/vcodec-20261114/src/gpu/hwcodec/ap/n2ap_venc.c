/* SPDX-License-Identifier: MIT */
/*
 * n2ap_venc.c -- the AP side of a MediaTek MT6893 VENC session in user space.
 *
 * Owner: V41.  This file is the user-space equivalent of
 *   drivers/media/platform/mtk-vcodec/venc_vcu_if.c
 *   drivers/media/platform/mtk-vcodec/venc/venc_common_if.c
 *   drivers/media/platform/mtk-vcodec/mt6885/mtk_vcodec_enc_pm.c   (the clock part only)
 *   drivers/media/platform/mtk-vcodec/mt6885/mtk_vcodec_intr.c     (the encoder ISR only)
 * for one encoder instance.  The codec core, the GCE command lists and the bitstream layout stay in
 * the daemon (the vendor library); everything above is protocol and power sequencing, which is what
 * this library implements against V40's vcu/n2vcu.h model.
 *
 * Message flow (see ap/MESSAGE_FLOW.md for the annotated version):
 *   open:    AP_IPIMSG_ENC_INIT           -> VCU_IPIMSG_ENC_INIT_DONE {vcu_inst_addr}
 *   cap:     AP_IPIMSG_ENC_QUERY_CAP (on IPI_VENC_COMMON) -> VCU_IPIMSG_ENC_QUERY_CAP_ACK
 *   start:   AP_IPIMSG_ENC_SET_PARAM(ENC) -> *_DONE, then SET_PARAM(PREPEND_HEADER) -> *_DONE
 *   encode:  AP_IPIMSG_ENC_ENCODE         -> (POWER_ON, WAIT_ISR, PUT_BUFFER) -> ENCODE_DONE
 *   close:   AP_IPIMSG_ENC_DEINIT         -> VCU_IPIMSG_ENC_DEINIT_DONE
 *
 * The daemon-to-AP requests are not polled: n2vcu calls the registered handler on the daemon's
 * thread while the AP is inside n2vcu_ap_wait_dispatched(), which is the same context in which the
 * Linux kernel runs vcu_enc_ipi_handler() (mtk_vcu.c:1396-1410).
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n2a32.h"
#include "n2ap_venc.h"
#include "n2vc_hw.h"
#include "mtk_venc_ipi.h"
#include "vcu/n2vcu.h"

/* The iOS SDK stubs in gpu/video/sdk/sysinc/errno.h omit EPROTO (the video lead owns that header;
 * posted to hw-ip/hw-lead).  Darwin's value is 100, so the guard keeps the host's errno.h value and
 * supplies the correct one for the arm64 build. */
#ifndef EPROTO
#define EPROTO 100
#endif

#define N2AP_DEFAULT_TIMEOUT_MS 6000u   /* IPI_TIMEOUT_MS, mtk_vcu.c:72 */
#define N2AP_WAIT_ISR_TIMEOUT_MS 500u   /* WAIT_INTR_TIMEOUT_MS, mtk_vcodec_drv.h:35 */
#define N2AP_FREE_RING 8u               /* free-buffer entries remembered per encode */

struct n2ap_free_ent {
	uint64_t bs_va;
	uint64_t frm_va;
	uint32_t bs_size;
	uint32_t is_key;
};

static uint32_t n2ap_cookie_seq;   /* 32-bit AP instance tokens, as the 32-bit daemon stores them */

struct n2ap_venc {
	struct n2vcu *vcu;
	const struct n2vc_hw *hw;
	unsigned ipi_id;
	unsigned timeout_ms;
	uint32_t fourcc;
	uint64_t cookie;          /* AP instance token (venc_inst); fits in 32 bits on purpose */
	uint32_t inst_addr;       /* VCU address of the vsi */
	struct venc_vsi *vsi;
	int opened;
	int aborted;
	int32_t last_status;
	int32_t hw_error;         /* first hw->power / hw->wait_irq failure */
	int power_refs;           /* mtk_vcodec_enc_clock_on/off reference count */
	pthread_mutex_t power_lock;   /* guards power_refs: n2ap_venc_power() and the handler can race */
	int power_sync;           /* 1 once power_lock is initialised */
	uint32_t irq_status;      /* ctx->irq_status, kept across jobs like the kernel's */
	uint32_t prepend_hdr;
	uint32_t proto_errors;

	/* one outstanding AP message */
	uint32_t expecting;       /* AP msg id we wait for, 0 = none (atomic: handler reads it) */
	int reply_seen;           /* guarded by reply_lock; the condvar waits for it */
	uint32_t reply_msg_id;
	int32_t reply_status;
	pthread_mutex_t reply_lock;
	pthread_cond_t reply_cv;
	int reply_sync;           /* 1 once reply_lock/reply_cv are initialised */

	struct venc_vcu_ipi_msg_enc enc_done;
	int enc_done_seen;
	struct n2ap_free_ent freed[N2AP_FREE_RING];
	unsigned nfreed;
	uint32_t last_bs_handle;  /* the bitstream of the most recent encode, for wait_done() */
	uint32_t last_bs_guest;
	uint32_t last_bs_bytes;

	void *query_out;
	uint32_t query_bytes;
	uint32_t query_size;

	n2ap_trace_fn trace;
	void *trace_arg;
};

/* The capability query is the one message the kernel sends on IPI_VENC_COMMON (venc_vcu_if.c:347
 * maps a temporary instance's id 0 to it), so it needs a handler on an id that no session owns.  One
 * query at a time, like the kernel's single ipi_desc[IPI_VENC_COMMON].handler.  The lock guards
 * owner and the query_out/bytes/size fields the daemon-thread handler reads: a reply that arrives
 * after the AP gave up (timeout) must not read fields the AP is clearing. */
static struct {
	struct n2ap_venc *owner;
	int registered;
	pthread_mutex_t lock;
} n2ap_query = { NULL, 0, PTHREAD_MUTEX_INITIALIZER };

static void ven_trace(struct n2ap_venc *e, const char *event, uint32_t a, uint32_t b, uint32_t c)
{
	if (e != NULL && e->trace != NULL) {
		e->trace(e->trace_arg, event, a, b, c);
	}
}

/* nanosleep, not usleep: usleep is not in POSIX.1-2008 and a strict host build (or the iOS SDK
 * headers) need not declare it (hw-ip reported the -Werror failure of the host library). */
static void ven_sleep_ms(unsigned ms)
{
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
		/* keep the remaining time */
	}
}

static uint32_t ven_read32(const void *buf)
{
	uint32_t v;

	memcpy(&v, buf, sizeof(v));
	return v;
}

static int32_t ven_read_status(const void *buf, uint32_t len)
{
	int32_t v;

	if (len < 8u) {
		return 0;
	}
	memcpy(&v, (const uint8_t *)buf + 4, sizeof(v));
	return v;
}

/* ---- reply matching (the kernel's ack wait, mtk_vcu.c:418-446) ------------------------------ */

/*
 * The daemon's reply is dispatched to ven_ipi_handler() on the daemon's thread, possibly before
 * the daemon's receiver thread has come back for the next message (a vpud with a separate service
 * thread does exactly that: IV saw GET_OBJECT re-entered before INIT_DONE arrived).  The kernel
 * therefore waits for the ACK (ipi_id_ack, set when the handler ran) *before* the ipi_done spin;
 * we mirror that with a condition variable the handler signals, and only then call
 * n2vcu_ap_wait_dispatched().  Before rev 3 this code waited for "dispatched" first, which a
 * threaded daemon satisfies too early and produced a spurious -EPROTO.
 */
static void ven_note_reply(struct n2ap_venc *e, uint32_t msg_id, int32_t status)
{
	pthread_mutex_lock(&e->reply_lock);
	e->reply_msg_id = msg_id;
	e->reply_status = status;
	e->reply_seen = 1;
	pthread_cond_broadcast(&e->reply_cv);
	pthread_mutex_unlock(&e->reply_lock);
}

static int ven_wait_reply(struct n2ap_venc *e)
{
	struct timespec dl;
	uint64_t ns;
	int rc = 0;

	if (clock_gettime(CLOCK_REALTIME, &dl) != 0) {
		return -EIO;
	}
	ns = (uint64_t)dl.tv_nsec + (uint64_t)e->timeout_ms * 1000000ull;
	dl.tv_sec += (time_t)(ns / 1000000000ull);
	dl.tv_nsec = (long)(ns % 1000000000ull);

	pthread_mutex_lock(&e->reply_lock);
	while (!e->reply_seen && rc == 0) {
		rc = pthread_cond_timedwait(&e->reply_cv, &e->reply_lock, &dl);
	}
	if (e->reply_seen) {
		rc = 0;
	} else if (rc == ETIMEDOUT) {
		rc = -ETIMEDOUT;
	} else if (rc != 0) {
		rc = -EIO;
	}
	pthread_mutex_unlock(&e->reply_lock);
	return rc;
}

/* ---- power (mtk_vcodec_enc_pm.c:260-360 and venc_drv_if.c:109-154) -------------------------- */

/*
 * mtk_vcodec_enc_clock_on(ctx, 0) gates larb7/VENC0 and larb8/VENC1 (:271-280).  The C1 gate is
 * not a VENC core-0 dependency, though: on this SoC RWVC_POWER(VENC_C1) is the whole VEN_CORE1
 * power domain + gates + larb8 and power-cycling it resets core 1 (hw-k02, board 11 November:
 * "use VENC only unless core 1 runs").  Core 0 therefore takes exactly one reference on
 * RWVC_BLK_VENC (= VEN domain + vencsys gates 0/4/28 + larb7); a session that actually programs
 * the C1 window must take RWVC_BLK_VENC_C1 itself.  Linux's extra VENC1 clock gate is inside the
 * C1 block, so leaving C1 off is the model's closest equivalent of "core 1 idle".
 */
static int ven_power_ref(struct n2ap_venc *e, int on)
{
	int rc = 0, trace_on = 0, trace_off = 0;

	if (e->hw == NULL || e->hw->power == NULL) {
		return -ENODEV;
	}
	/* The daemon's POWER_ON runs on its thread while an integrator may call
	 * n2ap_venc_power() from the encode thread; the reference count must be atomic as a
	 * unit with the hw->power call, or a race could power the block twice or drop it early. */
	pthread_mutex_lock(&e->power_lock);
	if (on) {
		if (e->power_refs > 0) {
			e->power_refs++;
		} else {
			rc = e->hw->power(e->hw->ctx, RWVC_BLK_VENC, 1);
			if (rc == 0) {
				e->power_refs = 1;
				trace_on = 1;
			}
		}
	} else if (e->power_refs > 0) {
		e->power_refs--;
		if (e->power_refs == 0) {
			rc = e->hw->power(e->hw->ctx, RWVC_BLK_VENC, 0);
			trace_off = 1;
		}
	}
	pthread_mutex_unlock(&e->power_lock);
	if (trace_on) {
		ven_trace(e, "power_on", RWVC_BLK_VENC, 1, 0);
	}
	if (trace_off) {
		ven_trace(e, "power_off", RWVC_BLK_VENC, 0, 0);
	}
	return rc;
}

/*
 * Public form of the same reference counter, for a daemon that programs the VENC through GCE
 * command lists and never sends VCU_IPIMSG_ENC_POWER_ON (libvcodecdrv.so does exactly that; the
 * Linux kernel powers the block in vcu_gce_set_cmd_flush, mtk_vcu.c:994).  The integrator holds
 * one reference around a job: n2ap_venc_power(e, 1) before the first register write,
 * n2ap_venc_power(e, 0) after the job is complete (after n2ap_venc_wait_done() for an
 * asynchronous frame).  POWER_ON/POWER_OFF from a protocol-following daemon share the same
 * counter, so mixing both is safe.
 */
int n2ap_venc_power(struct n2ap_venc *e, int on)
{
	if (e == NULL || !e->opened || e->vsi == NULL || (on != 0 && on != 1)) {
		return -EINVAL;
	}
	return ven_power_ref(e, on);
}

/* ---- reply matching ------------------------------------------------------------------------ */

static int ven_is_reply_for(uint32_t ap_id, uint32_t vcu_id)
{
	switch (ap_id) {
	case AP_IPIMSG_ENC_INIT:
		return vcu_id == VCU_IPIMSG_ENC_INIT_DONE;
	case AP_IPIMSG_ENC_SET_PARAM:
		return vcu_id == VCU_IPIMSG_ENC_SET_PARAM_DONE;
	case AP_IPIMSG_ENC_ENCODE:
		return vcu_id == VCU_IPIMSG_ENC_ENCODE_DONE || vcu_id == VCU_IPIMSG_ENC_ENCODE_ACK;
	case AP_IPIMSG_ENC_DEINIT:
		return vcu_id == VCU_IPIMSG_ENC_DEINIT_DONE;
	case AP_IPIMSG_ENC_QUERY_CAP:
		return vcu_id == VCU_IPIMSG_ENC_QUERY_CAP_ACK;
	default:
		return 0;
	}
}

/* ---- PUT_BUFFER: drain venc_vsi.list_free (venc_common_if.c:354-391) ----------------------- */

static void ven_put_buffer(struct n2ap_venc *e)
{
	struct ring_input_list *r;
	uint32_t n = 0;

	if (e->vsi == NULL) {
		return;
	}
	r = &e->vsi->list_free;
	/* venc_get_free_buffers() reads one entry per PUT_BUFFER-to-AP callback; a daemon that
	 * announces more than the ring can hold is capped, never trusted.  The capture is under
	 * reply_lock because a threaded daemon may send PUT_BUFFER after ENCODE_DONE, i.e. after
	 * the ACK the waiting encode() thread was released by; the lock gives that thread a
	 * happens-before edge for `nfreed`/`freed[]`. */
	pthread_mutex_lock(&e->reply_lock);
	/* list_free is shared memory: the daemon writes it (often from another thread) and sends
	 * PUT_BUFFER, which is the ordering edge.  The AP may also read it right after an ENCODE_ACK
	 * while an asynchronous daemon is still filling it, so `count` is read atomically with
	 * acquire: a daemon that publishes an entry with a release store (the vendor library's plain
	 * store is made visible by the IPI on the phone) can never be half-read here. */
	while (__atomic_load_n(&r->count, __ATOMIC_ACQUIRE) > 0 && n < VENC_MAX_FB_NUM) {
		int32_t idx = r->read_idx;

		if (idx < 0 || idx >= VENC_MAX_FB_NUM) {
			e->proto_errors++;
			break;
		}
		if (e->nfreed < N2AP_FREE_RING) {
			struct n2ap_free_ent *f = &e->freed[e->nfreed++];

			f->bs_va = r->venc_bs_va_list[idx];
			f->frm_va = r->venc_fb_va_list[idx];
			f->bs_size = r->bs_size[idx];
			f->is_key = r->is_key_frm[idx];
		}
		r->read_idx = (idx == VENC_MAX_FB_NUM - 1) ? 0 : idx + 1;
		(void)__atomic_sub_fetch(&r->count, 1, __ATOMIC_RELEASE);
		n++;
	}
	n = e->nfreed;
	pthread_mutex_unlock(&e->reply_lock);
	ven_trace(e, "put_buffer", n, 0, 0);
}

/* ---- the AP IPI handler (vcu_enc_ipi_handler, venc_vcu_if.c:68-191) ------------------------ */

static int ven_ipi_handler(void *ctx, int ipi_id, void *buf, uint32_t len)
{
	struct n2ap_venc *e = ctx;
	uint32_t msg_id, expecting;

	if (e == NULL || buf == NULL) {
		return 1;   /* dead handler: never ack a message nobody serves */
	}
	if (len < sizeof(uint32_t)) {
		e->proto_errors++;
		return 0;
	}
	msg_id = ven_read32(buf);
	expecting = __atomic_load_n(&e->expecting, __ATOMIC_RELAXED);
	(void)ipi_id;

	/* The kernel rejects a reply whose venc_inst is not this instance before dispatch
	 * (venc_vcu_if.c:97-108); request messages (>= VCU_IPIMSG_VENC_SEND_BASE) are exempt. */
	if (msg_id < VCU_IPIMSG_VENC_SEND_BASE && msg_id >= VCU_IPIMSG_VENC_BASE) {
		uint64_t inst = 0;

		if (len < sizeof(struct venc_vcu_ipi_msg_common)) {
			e->proto_errors++;
			return 1;
		}
		memcpy(&inst, (const uint8_t *)buf + 8, sizeof(inst));
		if (inst != e->cookie) {
			e->proto_errors++;
			return 1;   /* no ack, like the kernel's "vcu != priv" path */
		}
	}

	switch (msg_id) {
	case VCU_IPIMSG_ENC_INIT_DONE: {
		struct venc_vcu_ipi_msg_init m;

		if (len < sizeof(m)) {
			goto short_msg;
		}
		memcpy(&m, buf, sizeof(m));
		e->last_status = m.status;
		if (m.status == VENC_IPI_MSG_STATUS_OK) {
			e->inst_addr = m.vcu_inst_addr;
		}
		if (ven_is_reply_for(expecting, msg_id)) {
			ven_note_reply(e, msg_id, m.status);
		}
		ven_trace(e, "init_done", m.status, m.vcu_inst_addr, 0);
		return 0;
	}
	case VCU_IPIMSG_ENC_SET_PARAM_DONE: {
		struct venc_vcu_ipi_msg_set_param m;

		if (len < sizeof(m)) {
			goto short_msg;
		}
		memcpy(&m, buf, sizeof(m));
		e->last_status = m.status;
		if (ven_is_reply_for(expecting, msg_id)) {
			ven_note_reply(e, msg_id, m.status);
		}
		ven_trace(e, "param_done", m.status, m.param_id, m.data_item);
		return 0;
	}
	case VCU_IPIMSG_ENC_ENCODE_DONE: {
		struct venc_vcu_ipi_msg_enc m;

		if (len < sizeof(m)) {
			goto short_msg;
		}
		memcpy(&m, buf, sizeof(m));
		memcpy(&e->enc_done, &m, sizeof(m));
		e->enc_done_seen = 1;
		e->last_status = m.status;
		if (ven_is_reply_for(expecting, msg_id)) {
			ven_note_reply(e, msg_id, m.status);
		}
		ven_trace(e, "encode_done", m.status, m.bs_size, m.is_key_frm);
		return 0;
	}
	case VCU_IPIMSG_ENC_ENCODE_ACK: {
		struct venc_vcu_ipi_msg_common m;

		if (len < sizeof(m)) {
			goto short_msg;
		}
		memcpy(&m, buf, sizeof(m));
		e->last_status = m.status;
		if (ven_is_reply_for(expecting, msg_id)) {
			ven_note_reply(e, msg_id, m.status);
		}
		ven_trace(e, "encode_done", m.status, 0, 0);
		return 0;
	}
	case VCU_IPIMSG_ENC_DEINIT_DONE: {
		struct venc_vcu_ipi_msg_deinit m;

		if (len < sizeof(m)) {
			goto short_msg;
		}
		memcpy(&m, buf, sizeof(m));
		e->last_status = m.status;
		if (ven_is_reply_for(expecting, msg_id)) {
			ven_note_reply(e, msg_id, m.status);
		}
		ven_trace(e, "deinit", m.status, 0, 0);
		return 0;
	}
	case VCU_IPIMSG_ENC_QUERY_CAP_ACK: {
		struct venc_vcu_ipi_query_cap_ack m;
		uint32_t room;

		if (len < sizeof(m)) {
			goto short_msg;
		}
		memcpy(&m, buf, sizeof(m));
		e->last_status = m.status;
		if (m.ap_inst_addr != e->cookie) {
			e->proto_errors++;
			return 1;
		}
		room = e->query_size * MTK_MAX_ENC_CODECS_SUPPORT;
		if (m.status == VENC_IPI_MSG_STATUS_OK && e->query_out != NULL && e->query_size != 0u &&
		    room <= e->query_bytes) {
			void *src = n2vcu_map_dm_addr(e->vcu, m.vcu_data_addr);

			if (src != NULL) {
				memcpy(e->query_out, src, room);
			} else {
				e->proto_errors++;
				m.status = -EIO;
			}
		}
		if (ven_is_reply_for(expecting, msg_id)) {
			ven_note_reply(e, msg_id, m.status);
		}
		return 0;
	}
	case VCU_IPIMSG_ENC_POWER_ON:
		if (len < sizeof(struct venc_vcu_ipi_msg_common)) {
			goto short_msg;
		}
		e->last_status = ven_read_status(buf, len);
		{
			int rc = ven_power_ref(e, 1);

			if (rc != 0 && e->hw_error == 0) {
				e->hw_error = rc;
			}
		}
		return 1;   /* non-ack, like venc_vcu_if.c:135-148 */
	case VCU_IPIMSG_ENC_POWER_OFF:
		if (len < sizeof(struct venc_vcu_ipi_msg_common)) {
			goto short_msg;
		}
		e->last_status = ven_read_status(buf, len);
		(void)ven_power_ref(e, 0);
		return 1;   /* :149-155 */
	case VCU_IPIMSG_ENC_WAIT_ISR: {
		struct venc_vcu_ipi_msg_waitisr m;
		uint32_t status = 0;
		int rc;

		if (len < sizeof(m)) {
			goto short_msg;
		}
		memcpy(&m, buf, sizeof(m));
		e->last_status = m.status;
		/* mtk_vcodec_wait_for_done_ctx(ctx, 0, MTK_INST_IRQ_RECEIVED,
		 * WAIT_INTR_TIMEOUT_MS) then handle_enc_waitisr_msg(): irq_status is the value the
		 * ISR latched before clearing, timeout is 1 on timeout (:159-169, :58-66). */
		if (e->hw != NULL && e->hw->wait_irq != NULL) {
			rc = e->hw->wait_irq(e->hw->ctx, RWVC_IRQ_VENC, N2AP_WAIT_ISR_TIMEOUT_MS,
			                     &status);
		} else {
			rc = -ENODEV;
		}
		if (rc == 0) {
			e->irq_status = status;
			m.irq_status = e->irq_status;
			m.timeout = 0;
			ven_trace(e, "wait_isr", status, 0, 0);
		} else {
			if (rc != -ETIMEDOUT && e->hw_error == 0) {
				e->hw_error = rc;
			}
			m.irq_status = e->irq_status;   /* the kernel keeps the previous value */
			m.timeout = 1;
			ven_trace(e, "wait_isr", 0, 1, (uint32_t)(-rc));
		}
		memcpy(buf, &m, sizeof(m));   /* copied back to the daemon by the kernel (:1814-1821) */
		return 1;
	}
	case VCU_IPIMSG_ENC_PUT_BUFFER:
		if (len < sizeof(struct venc_vcu_ipi_msg_common)) {
			goto short_msg;
		}
		e->last_status = ven_read_status(buf, len);
		ven_put_buffer(e);
		return 1;   /* :170-173 */
	default:
		e->proto_errors++;
		return 0;   /* the kernel logs "unknown msg id" and ack's it (:176-179) */
	}

short_msg:
	e->proto_errors++;
	if (ven_is_reply_for(expecting, msg_id)) {
		ven_note_reply(e, msg_id, -EPROTO);
	}
	return 0;
}

/* Drop-everything handler, installed before a session is freed: n2vcu has no unregister
 * (vcu_ipi_register has none either), so a late daemon message must not reach freed memory. */
static int ven_drop_handler(void *ctx, int ipi_id, void *buf, uint32_t len)
{
	(void)ctx;
	(void)ipi_id;
	(void)buf;
	(void)len;
	return 1;
}

/* ---- send + wait (vcu_ipi_send, mtk_vcu.c:383-476) ----------------------------------------- */

static int ven_send(struct n2ap_venc *e, unsigned ipi_id, const void *msg, uint32_t len,
                    uint32_t ap_msg_id)
{
	int rc;

	if (e->aborted) {
		return -EIO;
	}
	__atomic_store_n(&e->expecting, ap_msg_id, __ATOMIC_RELAXED);
	pthread_mutex_lock(&e->reply_lock);
	e->reply_seen = 0;
	e->reply_msg_id = 0;
	e->reply_status = 0;
	pthread_mutex_unlock(&e->reply_lock);

	rc = n2vcu_ap_send(e->vcu, (int)ipi_id, msg, len);
	if (rc != 0) {
		__atomic_store_n(&e->expecting, 0u, __ATOMIC_RELAXED);
		return rc;
	}
	/* The kernel's first half: wait for the ACK, i.e. the daemon's reply dispatched to the
	 * handler (mtk_vcu.c:435, :1411-1412).  A threaded daemon may re-enter GET_OBJECT
	 * before it answers, so this must come before the ipi_done wait. */
	rc = ven_wait_reply(e);
	if (rc != 0) {
		__atomic_store_n(&e->expecting, 0u, __ATOMIC_RELAXED);
		e->aborted = 1;
		ven_trace(e, "timeout", ipi_id, ap_msg_id, e->timeout_ms);
		return rc;
	}
	/* The kernel's second half: ipi_done, i.e. the daemon has come back for its next message
	 * (mtk_vcu.c:470), which is only reached after every reply has been dispatched. */
	rc = n2vcu_ap_wait_dispatched(e->vcu, (int)ipi_id, e->timeout_ms);
	__atomic_store_n(&e->expecting, 0u, __ATOMIC_RELAXED);
	if (rc != 0) {
		e->aborted = 1;
		ven_trace(e, "timeout", ipi_id, ap_msg_id, e->timeout_ms);
		return rc;
	}
	if (e->hw_error != 0) {
		int herr = e->hw_error;

		e->hw_error = 0;
		ven_trace(e, "hw", ap_msg_id, (uint32_t)(-herr), 0);
		return herr;
	}
	if (e->reply_status != VENC_IPI_MSG_STATUS_OK) {
		/* A daemon that reports VENC_IPI_MSG_STATUS_FAIL maps to -EINVAL, exactly like
		 * vcu_enc_send_msg()'s vcu->failure check (:230-231); a malformed message that the
		 * handler detected itself carries its own negative code. */
		return e->reply_status < 0 ? e->reply_status : -EINVAL;
	}
	return 0;
}

/* ---- open / close ------------------------------------------------------------------------- */

static unsigned ven_ipi_for_fourcc(uint32_t fourcc)
{
	/* venc_init(), venc_common_if.c:232-271; VP8/VP9 are rejected earlier by venc_if_init()
	 * (:34-52) because this kernel builds the VCU encoder, not the VPU one. */
	switch (fourcc) {
	case N2AP_FOURCC_H264:
		return N2AP_IPI_VENC_H264;
	case N2AP_FOURCC_H265:
		return N2AP_IPI_VENC_H265;
	case N2AP_FOURCC_HEIF:
		return N2AP_IPI_VENC_HEIF;
	case N2AP_FOURCC_MPEG4:
		return N2AP_IPI_VENC_MPEG4;
	case N2AP_FOURCC_H263:
		return N2AP_IPI_VENC_H263;
	default:
		return 0;
	}
}

static void ven_free(struct n2ap_venc *e)
{
	if (e->reply_sync) {
		pthread_cond_destroy(&e->reply_cv);
		pthread_mutex_destroy(&e->reply_lock);
		e->reply_sync = 0;
	}
	if (e->power_sync) {
		pthread_mutex_destroy(&e->power_lock);
		e->power_sync = 0;
	}
	free(e);
}

int n2ap_venc_open(struct n2ap_venc **out, struct n2vcu *vcu, const struct n2vc_hw *hw,
                   uint32_t codec_fourcc, unsigned timeout_ms)
{
	struct venc_ap_ipi_msg_init msg;
	struct n2ap_venc *e;
	uint32_t base = 0, dlen = 0;
	unsigned ipi_id;
	int rc;

	if (out == NULL || vcu == NULL || hw == NULL) {
		return -EINVAL;
	}
	*out = NULL;
	ipi_id = ven_ipi_for_fourcc(codec_fourcc);
	if (ipi_id == 0u) {
		return -EINVAL;
	}
	e = calloc(1, sizeof(*e));
	if (e == NULL) {
		return -ENOMEM;
	}
	if (pthread_mutex_init(&e->power_lock, NULL) != 0) {
		ven_free(e);
		return -ENOMEM;
	}
	e->power_sync = 1;
	if (pthread_mutex_init(&e->reply_lock, NULL) != 0) {
		ven_free(e);
		return -ENOMEM;
	}
	if (pthread_cond_init(&e->reply_cv, NULL) != 0) {
		pthread_mutex_destroy(&e->reply_lock);
		ven_free(e);
		return -ENOMEM;
	}
	e->reply_sync = 1;
	e->vcu = vcu;
	e->hw = hw;
	e->ipi_id = ipi_id;
	e->fourcc = codec_fourcc;
	e->timeout_ms = timeout_ms != 0u ? timeout_ms : N2AP_DEFAULT_TIMEOUT_MS;
	/* vcu_inst_addr/venc_inst are opaque to the 32-bit daemon, which only copies them back;
	 * a 32-bit token is therefore enough and avoids handing a 64-bit host pointer to code that
	 * may keep it in a 32-bit field.  The high half identifies the owner. */
	e->cookie = 0x4e320000u |
	            (uint64_t)(__atomic_add_fetch(&n2ap_cookie_seq, 1u, __ATOMIC_RELAXED) & 0xffffu);

	rc = n2vcu_ap_register(vcu, (int)ipi_id, ven_ipi_handler, e);
	if (rc != 0) {
		ven_free(e);
		return rc;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_ENC_INIT;
	msg.venc_inst = e->cookie;
	ven_trace(e, "init", ipi_id, codec_fourcc, 0);
	rc = ven_send(e, ipi_id, &msg, sizeof(msg), AP_IPIMSG_ENC_INIT);
	if (rc != 0) {
		(void)n2vcu_ap_register(vcu, (int)ipi_id, ven_drop_handler, NULL);
		ven_free(e);
		return rc;
	}
	if (e->inst_addr == 0u || (e->inst_addr & 7u) != 0u) {
		(void)n2vcu_ap_register(vcu, (int)ipi_id, ven_drop_handler, NULL);
		ven_free(e);
		return -EIO;
	}
	if (n2vcu_dmem(vcu, &base, &dlen) != 0 || dlen < sizeof(struct venc_vsi) ||
	    e->inst_addr < base || (uint64_t)(e->inst_addr - base) + sizeof(struct venc_vsi) > dlen) {
		(void)n2vcu_ap_register(vcu, (int)ipi_id, ven_drop_handler, NULL);
		ven_free(e);
		return -EIO;
	}
	e->vsi = (struct venc_vsi *)n2vcu_map_dm_addr(vcu, e->inst_addr);
	if (e->vsi == NULL) {
		(void)n2vcu_ap_register(vcu, (int)ipi_id, ven_drop_handler, NULL);
		ven_free(e);
		return -EIO;
	}
	e->opened = 1;
	*out = e;
	return 0;
}

int n2ap_venc_close(struct n2ap_venc *e)
{
	int rc = 0;

	if (e == NULL) {
		return -EINVAL;
	}
	if (e->opened && !e->aborted) {
		struct venc_ap_ipi_msg_deinit msg;

		memset(&msg, 0, sizeof(msg));
		msg.msg_id = AP_IPIMSG_ENC_DEINIT;
		msg.vcu_inst_addr = e->inst_addr;
		rc = ven_send(e, e->ipi_id, &msg, sizeof(msg), AP_IPIMSG_ENC_DEINIT);
	}
	/* Unbalanced POWER_ON/POWER_OFF from the daemon: drop the references we still hold. */
	while (e->power_refs > 0) {
		int prc = ven_power_ref(e, 0);

		if (prc != 0 && rc == 0) {
			rc = prc;
		}
	}
	pthread_mutex_lock(&n2ap_query.lock);
	if (n2ap_query.owner == e) {
		n2ap_query.owner = NULL;
	}
	pthread_mutex_unlock(&n2ap_query.lock);
	(void)n2vcu_ap_register(e->vcu, (int)e->ipi_id, ven_drop_handler, NULL);
	ven_free(e);
	return rc;
}

unsigned n2ap_venc_ipi_id(const struct n2ap_venc *e)
{
	return e != NULL ? e->ipi_id : 0u;
}

struct venc_vsi *n2ap_venc_vsi(const struct n2ap_venc *e)
{
	return e != NULL ? e->vsi : NULL;
}

int32_t n2ap_venc_last_status(const struct n2ap_venc *e)
{
	return e != NULL ? e->last_status : 0;
}

int n2ap_venc_aborted(const struct n2ap_venc *e)
{
	return e != NULL ? e->aborted : 0;
}

void n2ap_venc_set_trace(struct n2ap_venc *e, n2ap_trace_fn fn, void *arg)
{
	if (e != NULL) {
		e->trace = fn;
		e->trace_arg = arg;
	}
}

/* ---- capability query (venc_vcu_if.c:27-56, :329-368) -------------------------------------- */

static int ven_query_handler(void *ctx, int ipi_id, void *buf, uint32_t len)
{
	struct n2ap_venc *e;
	int rc;

	(void)ctx;
	pthread_mutex_lock(&n2ap_query.lock);
	e = n2ap_query.owner;
	if (e == NULL) {
		rc = 1;   /* nobody asked: do not ack */
	} else {
		/* Held across the handler so a reply that arrives after query_cap() gave up
		 * cannot read query_out while the AP is clearing it. */
		rc = ven_ipi_handler(e, ipi_id, buf, len);
	}
	pthread_mutex_unlock(&n2ap_query.lock);
	return rc;
}

int n2ap_venc_query_cap(struct n2ap_venc *e, unsigned id, void *out, uint32_t out_bytes)
{
	struct venc_ap_ipi_query_cap msg;
	uint32_t size;
	int rc;

	if (e == NULL || !e->opened || out == NULL) {
		return -EINVAL;
	}
	switch (id) {
	case VENC_GET_PARAM_CAPABILITY_SUPPORTED_FORMATS:
		size = (uint32_t)sizeof(struct mtk_video_fmt);
		break;
	case VENC_GET_PARAM_CAPABILITY_FRAME_SIZES:
		size = (uint32_t)sizeof(struct mtk_codec_framesizes);
		break;
	default:
		return -EINVAL;
	}
	if (out_bytes < size * MTK_MAX_ENC_CODECS_SUPPORT) {
		return -EINVAL;
	}
	if (e->aborted) {
		return -EIO;
	}
	pthread_mutex_lock(&n2ap_query.lock);
	if (n2ap_query.owner != NULL) {
		pthread_mutex_unlock(&n2ap_query.lock);
		return -EBUSY;
	}
	if (!n2ap_query.registered) {
		rc = n2vcu_ap_register(e->vcu, N2AP_IPI_VENC_COMMON, ven_query_handler, NULL);
		if (rc != 0) {
			pthread_mutex_unlock(&n2ap_query.lock);
			return rc;
		}
		n2ap_query.registered = 1;
	}
	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_ENC_QUERY_CAP;
	msg.id = id;
	msg.ap_inst_addr = e->cookie;
	msg.ap_data_addr = (uint64_t)(uintptr_t)out;
	e->query_out = out;
	e->query_bytes = out_bytes;
	e->query_size = size;
	n2ap_query.owner = e;
	pthread_mutex_unlock(&n2ap_query.lock);
	rc = ven_send(e, N2AP_IPI_VENC_COMMON, &msg, sizeof(msg), AP_IPIMSG_ENC_QUERY_CAP);
	pthread_mutex_lock(&n2ap_query.lock);
	e->query_out = NULL;
	e->query_bytes = 0;
	e->query_size = 0;
	n2ap_query.owner = NULL;
	pthread_mutex_unlock(&n2ap_query.lock);
	return rc;
}

/* ---- set_param (venc_vcu_if.c:370-513 + venc_common_if.c:454-560) -------------------------- */

static uint32_t ven_h265_profile(uint32_t profile)
{
	/* venc_h265_get_profile(), venc_common_if.c:15-28; the V4L2 values are
	 * MAIN=0, MAIN_STILL_PICTURE=1, MAIN_10=2 (uapi/linux/v4l2-controls.h:754-758). */
	switch (profile) {
	case 0: return 2;   /* V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN */
	case 2: return 4;   /* V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10 */
	case 1: return 8;   /* V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE */
	default: return 0;
	}
}

static uint32_t ven_h265_level(uint32_t level, uint32_t tier)
{
	/* venc_h265_get_level(): levels 0..12 map to 2*level + (tier == MAIN ? 0 : 1), else 26. */
	if (level > 12u) {
		return 26;
	}
	return level * 2u + (tier == 0u ? 0u : 1u);
}

static uint32_t ven_mpeg4_profile(uint32_t profile)
{
	/* venc_mpeg4_get_profile(): identity for 0..4, else 100. */
	return profile <= 4u ? profile : 100u;
}

static uint32_t ven_mpeg4_level(uint32_t level)
{
	/* venc_mpeg4_get_level(): identity for 0..7, else 4. */
	return level <= 7u ? level : 4u;
}

static void ven_write_config(struct n2ap_venc *e, const struct n2ap_enc_param *p)
{
	struct venc_vcu_config *c = &e->vsi->config;

	c->input_fourcc = p->input_yuv_fmt;
	c->bitrate = p->bitrate;
	c->pic_w = p->width;
	c->pic_h = p->height;
	c->buf_w = p->buf_width;
	c->buf_h = p->buf_height;
	c->gop_size = p->gop_size;
	c->framerate = p->frm_rate;
	c->intra_period = p->intra_period;
	c->operationrate = p->operationrate;
	c->bitratemode = p->bitratemode;
	c->roion = p->roion;
	c->scenario = p->scenario;
	c->prependheader = p->prependheader;
	c->heif_grid_size = p->heif_grid_size;
	c->max_w = p->max_w;
	c->max_h = p->max_h;
	c->num_b_frame = p->num_b_frame;
	c->slbc_ready = p->slbc_ready;
	c->i_qp = p->i_qp;
	c->p_qp = p->p_qp;
	c->b_qp = p->b_qp;
	c->svp_mode = p->svp_mode;
	c->tsvc = p->tsvc;
	c->max_qp = p->max_qp;
	c->min_qp = p->min_qp;
	c->i_p_qp_delta = p->i_p_qp_delta;
	c->qp_control_mode = p->qp_control_mode;
	if (p->qp_control_mode != 0u) {
		c->frame_level_qp = p->frame_level_qp;
	}
	c->dummynal = p->dummynal;
	if (p->color_desc != NULL) {
		memcpy(&c->color_desc, p->color_desc, sizeof(c->color_desc));
	} else {
		memset(&c->color_desc, 0, sizeof(c->color_desc));
	}
	c->maxrefpnum = p->maxrefpnum;
	switch (e->ipi_id) {
	case N2AP_IPI_VENC_H264:
	case N2AP_IPI_VENC_HYBRID_H264:
		c->profile = p->profile;
		c->level = p->level;
		break;
	case N2AP_IPI_VENC_H265:
	case N2AP_IPI_VENC_HEIF:
		c->profile = ven_h265_profile(p->profile);
		c->level = ven_h265_level(p->level, p->tier);
		break;
	case N2AP_IPI_VENC_MPEG4:
		c->profile = ven_mpeg4_profile(p->profile);
		c->level = ven_mpeg4_level(p->level);
		break;
	default:
		break;   /* H263: venc_common_if.c leaves profile/level as they are (zero) */
	}
	c->wfd = 0;
}

int n2ap_venc_set_param(struct n2ap_venc *e, unsigned type, struct n2ap_enc_param *p)
{
	struct venc_ap_ipi_msg_set_param msg;
	uint32_t item = 0;
	uint32_t value = 0;
	int rc;

	if (e == NULL || !e->opened || e->vsi == NULL) {
		return -EINVAL;
	}
	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_ENC_SET_PARAM;
	msg.vcu_inst_addr = e->inst_addr;
	msg.param_id = type;

	switch (type) {
	case VENC_SET_PARAM_ENC:
		if (p == NULL) {
			return -EINVAL;
		}
		ven_write_config(e, p);
		item = 0;
		break;
	case VENC_SET_PARAM_FORCE_INTRA:
	case VENC_SET_PARAM_SKIP_FRAME:
		item = 0;
		break;
	case VENC_SET_PARAM_PREPEND_HEADER:
		e->prepend_hdr = 1;
		item = 0;
		break;
	case VENC_SET_PARAM_COLOR_DESC:
		if (p == NULL || p->color_desc == NULL) {
			return -EINVAL;
		}
		memcpy(&e->vsi->config.color_desc, p->color_desc,
		       sizeof(e->vsi->config.color_desc));
		item = 0;   /* passed via vsi, venc_vcu_if.c:456-458 */
		break;
	case VENC_SET_PARAM_ADJUST_BITRATE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->bitrate;
		break;
	case VENC_SET_PARAM_ADJUST_FRAMERATE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->frm_rate;
		break;
	case VENC_SET_PARAM_GOP_SIZE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->gop_size;
		break;
	case VENC_SET_PARAM_INTRA_PERIOD:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->intra_period;
		break;
	case VENC_SET_PARAM_SCENARIO:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->scenario;
		break;
	case VENC_SET_PARAM_NONREFP:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->nonrefp;
		break;
	case VENC_SET_PARAM_NONREFPFREQ:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->nonrefpfreq;
		break;
	case VENC_SET_PARAM_DETECTED_FRAMERATE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->detectframerate;
		break;
	case VENC_SET_PARAM_RFS_ON:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->rfs;
		break;
	case VENC_SET_PARAM_PREPEND_SPSPPS_TO_IDR:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->prependheader;
		break;
	case VENC_SET_PARAM_OPERATION_RATE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->operationrate;
		break;
	case VENC_SET_PARAM_BITRATE_MODE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->bitratemode;
		break;
	case VENC_SET_PARAM_ROI_ON:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->roion;
		break;
	case VENC_SET_PARAM_HEIF_GRID_SIZE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->heif_grid_size;
		break;
	case VENC_SET_PARAM_SEC_MODE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->svp_mode;
		break;
	case VENC_SET_PARAM_TSVC:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->tsvc;
		break;
	case VENC_SET_PARAM_ADJUST_MAX_QP:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->max_qp;
		break;
	case VENC_SET_PARAM_ADJUST_MIN_QP:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->min_qp;
		break;
	case VENC_SET_PARAM_ADJUST_I_P_QP_DELTA:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->i_p_qp_delta;
		break;
	case VENC_SET_PARAM_ADJUST_FRAME_LEVEL_QP:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->frame_level_qp;
		break;
	case VENC_SET_PARAM_MAX_REFP_NUM:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->maxrefpnum;
		break;
	case VENC_SET_PARAM_REFP_DISTANCE:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->refpdistance;
		break;
	case VENC_SET_PARAM_REFP_FRMNUM:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->refpfrmnum;
		break;
	case VENC_SET_PARAM_ENABLE_DUMMY_NAL:
		if (p == NULL) return -EINVAL;
		item = 1; value = p->dummynal;
		break;
	case VENC_SET_PARAM_TS_MODE:
	case VENC_SET_PARAM_LOG:
		/* vcu_enc_set_param() has no case for these and returns -EINVAL before sending
		 * (:499-501); venc_common_if.c's LOG case falls through with the same result. */
		return -EINVAL;
	default:
		return -EINVAL;
	}
	msg.data_item = item;
	if (item != 0u) {
		msg.data[0] = value;
	}
	ven_trace(e, "set_param", type, item, item ? value : 0u);
	rc = ven_send(e, e->ipi_id, &msg, sizeof(msg), AP_IPIMSG_ENC_SET_PARAM);
	if (rc != 0) {
		return rc;
	}
	if (type == VENC_SET_PARAM_ENC && p != NULL) {
		for (unsigned i = 0; i < MTK_VCODEC_MAX_PLANES; i++) {
			p->sizeimage[i] = e->vsi->sizeimage[i];
		}
		p->sync_mode = e->vsi->sync_mode;
	}
	return 0;
}

int n2ap_venc_start(struct n2ap_venc *e, struct n2ap_enc_param *p)
{
	int rc;

	if (e == NULL || p == NULL) {
		return -EINVAL;
	}
	/* mtk_vcodec_enc.c:1857 then :1874-1892.  The kernel sends PREPEND_HEADER only for the
	 * codecs in that condition: H.264, H.265, HEIF and MPEG4, never H.263. */
	rc = n2ap_venc_set_param(e, VENC_SET_PARAM_ENC, p);
	if (rc != 0) {
		return rc;
	}
	if (p->seq_hdr_mode != 0u &&
	    (e->ipi_id == N2AP_IPI_VENC_H264 || e->ipi_id == N2AP_IPI_VENC_HYBRID_H264 ||
	     e->ipi_id == N2AP_IPI_VENC_H265 || e->ipi_id == N2AP_IPI_VENC_HEIF ||
	     e->ipi_id == N2AP_IPI_VENC_MPEG4)) {
		rc = n2ap_venc_set_param(e, VENC_SET_PARAM_PREPEND_HEADER, NULL);
	}
	return rc;
}

void n2ap_enc_param_1080p30_h264(struct n2ap_enc_param *p)
{
	if (p == NULL) {
		return;
	}
	memset(p, 0, sizeof(*p));
	/* No on-chip-SRAM (SLBC) routing in our build: the vendor library must take the DRAM path
	 * (fgSLBCReady = 0; the field is venc_vcu_config offset 160, venc_common_if.c:468-526). */
	p->slbc_ready = 0;
	p->input_yuv_fmt = N2AP_YUV_NV12;       /* V4L2_PIX_FMT_NV12 -> VENC_YUV_FORMAT_NV12 */
	p->profile = 0;                         /* V4L2_CID_MPEG_VIDEO_H264_PROFILE default, mtk_vcodec_enc.c:2757-2759 */
	p->level = 0;                           /* V4L2_CID_MPEG_VIDEO_H264_LEVEL default, :2757 */
	p->width = 1920;
	p->height = 1080;
	p->buf_width = 1920;
	p->buf_height = 1088;                   /* H.264 coded height for 1080p, 32-aligned */
	p->frm_rate = 30;
	p->bitrate = 20000000;                  /* V4L2_CID_MPEG_VIDEO_BITRATE default, :2730 */
	p->intra_period = 0;                    /* V4L2_CID_MPEG_VIDEO_H264_I_PERIOD default 0 */
	p->gop_size = 0;                        /* V4L2_CID_MPEG_VIDEO_GOP_SIZE default 0 */
	p->bitratemode = 0;                     /* V4L2_MPEG_VIDEO_BITRATE_MODE_VBR */
	p->i_qp = 51;                           /* RC_I/P/B_FRAME_QP defaults 51 (:2747-2752) */
	p->p_qp = 51;
	p->b_qp = 51;
	p->max_qp = 51;
	/* V4L2_CID_MPEG_VIDEO_HEADER_MODE is registered with max JOINED and default SEPARATE
	 * (mtk_vcodec_enc.c:2753-2755), so a stock session encodes the sequence header as a separate
	 * job before its first frame (m2mops_venc_device_run, :2598-2615) instead of setting
	 * PREPEND_HEADER at stream start.  A client that wants SPS/PPS joined sets this to 1. */
	p->seq_hdr_mode = 0;
}

void n2ap_enc_param_1080p30_hevc(struct n2ap_enc_param *p)
{
	if (p == NULL) {
		return;
	}
	n2ap_enc_param_1080p30_h264(p);
	p->profile = 0;   /* V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN default, mtk_vcodec_enc.c:2760-2762 */
	p->level = 5;     /* V4L2_MPEG_VIDEO_HEVC_LEVEL_4 default, :2769-2772 */
	p->tier = 0;      /* V4L2_MPEG_VIDEO_HEVC_TIER_MAIN default, :2773-2776 */
}

/* ---- encode (venc_vcu_if.c:515-609 + venc_common_if.c:120-217) ----------------------------- */

/* Opaque cookie for venc_info.venc_bs_va / venc_fb_va.  The kernel stores the address of its own
 * buffer struct (venc_common_if.c:126-167) and the daemon only echoes it back; we use the guest
 * address when the buffer is guest-mapped (so it is also meaningful to a trace), else a tagged
 * handle that can never collide with a page-aligned guest address. */
static uint64_t ven_cookie(uint32_t handle, uint32_t guest)
{
	if (guest != 0u) {
		return (uint64_t)guest;
	}
	return 0x80000000u | (uint64_t)(handle & 0x7fffffffu);
}

int n2ap_venc_encode_fd(struct n2ap_venc *e, uint32_t frame_handle, int frame_fd,
                        uint32_t bs_handle, int bs_fd, unsigned flags,
                        struct n2ap_enc_result *res)
{
	struct venc_ap_ipi_msg_enc msg;
	struct n2ap_enc_result result;
	uint32_t frame_pa = 0, frame_bytes = 0, frame_guest = 0;
	uint32_t bs_pa = 0, bs_bytes = 0, bs_guest = 0;
	uint32_t bs_mode;
	uint64_t bs_cookie;
	int rc;

	if (e == NULL || !e->opened || e->vsi == NULL || res == NULL || bs_handle == 0u) {
		return -EINVAL;
	}
	if (e->aborted) {
		return -EIO;
	}
	if ((flags & N2AP_ENC_SEQ_HDR) != 0u) {
		bs_mode = VENC_BS_MODE_SEQ_HDR;
		frame_handle = 0;
	} else if ((flags & 0x3u) == N2AP_ENC_FRAME_FINAL) {
		bs_mode = VENC_BS_MODE_FRAME_FINAL;
	} else {
		bs_mode = VENC_BS_MODE_FRAME;
	}
	/* FRAME_FINAL without a frame = the EOS flush (mtk_vcodec_enc.c:1928) */
	if (bs_mode != VENC_BS_MODE_SEQ_HDR && bs_mode != VENC_BS_MODE_FRAME_FINAL &&
	    frame_handle == 0u) {
		return -EINVAL;   /* only the header and the EOS flush may have no frame */
	}

	if (n2vc_dma_lookup_handle(bs_handle, &bs_pa, &bs_guest, &bs_bytes) != 0) {
		return -EINVAL;
	}
	if (bs_pa == 0u || bs_bytes == 0u) {
		return -EINVAL;
	}
	if (frame_handle != 0u) {
		if (n2vc_dma_lookup_handle(frame_handle, &frame_pa, &frame_guest, &frame_bytes) != 0) {
			return -EINVAL;
		}
		if (frame_pa == 0u || frame_bytes == 0u) {
			return -EINVAL;
		}
	}

	/* venc_common_if.c:126-167: the vsi carries the AP cookies, the message the
	 * physical addresses. */
	e->vsi->venc.venc_bs_va = ven_cookie(bs_handle, bs_guest);
	e->vsi->venc.venc_fb_va = frame_handle != 0u ? ven_cookie(frame_handle, frame_guest) : 0;
	e->vsi->venc.timestamp = 0;   /* our API has no timestamp yet (kernel: frm_buf->timestamp) */
	e->vsi->venc.roimap = 0;
	e->vsi->venc.qpmap = 0;
	e->vsi->meta_fd = 0;
	e->vsi->meta_size = 0;
	e->vsi->meta_addr = 0;

	/* Frozen n2vc_hw.h: "the AP-side driver calls n2vc_dma_sync() around every hardware job
	 * (to_device before the trigger, from_device after the done interrupt)". */
	if (frame_guest != 0u) {
		(void)n2vc_dma_sync(e->hw, NULL, frame_guest, 0, frame_bytes, 1);
	}
	if (bs_guest != 0u) {
		(void)n2vc_dma_sync(e->hw, NULL, bs_guest, 0, bs_bytes, 1);
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_ENC_ENCODE;
	msg.vcu_inst_addr = e->inst_addr;
	if (frame_handle != 0u) {
		msg.fb_num_planes = 1;   /* single-plane NV12/NV21; see the limitation in STATUS.md */
		msg.input_addr[0] = frame_pa;
		msg.input_size[0] = frame_bytes;
		msg.data_offset[0] = 0;
		/* The vendor ring buffer calls ion_import() on these (vcodecdrv ringbuf).  The
		 * integrator registers the AP's own DMA handles as shares with V34 and passes their ids;
		 * 0 means "no fd", which the daemon only tolerates for the sequence-header job. */
		msg.input_fd[0] = (int16_t)frame_fd;
	}
	msg.bs_addr = bs_pa;
	msg.bs_size = bs_bytes;
	msg.bs_fd = (int16_t)bs_fd;
	msg.bs_mode = (uint8_t)bs_mode;
	msg.meta_size = 0;
	msg.meta_fd = 0;
	msg.qpmap = 0;

	pthread_mutex_lock(&e->reply_lock);
	e->enc_done_seen = 0;
	e->nfreed = 0;
	e->last_bs_handle = bs_handle;
	e->last_bs_guest = bs_guest;
	e->last_bs_bytes = bs_bytes;
	pthread_mutex_unlock(&e->reply_lock);
	ven_trace(e, "encode", bs_mode, bs_bytes, frame_bytes);
	rc = ven_send(e, e->ipi_id, &msg, sizeof(msg), AP_IPIMSG_ENC_ENCODE);
	if (rc != 0) {
		return rc;
	}
	/* The kernel drains the free-buffer ring itself after every encode (mtk_enc_put_buf in
	 * synchronous mode, mtk_vcodec_enc.c:2590/:1931).  The daemon answers the sequence-header job
	 * with an ACK and leaves the header size only in list_free, so read it before the lookup below
	 * (board: opus-venc -> V41, 11 November). */
	ven_put_buffer(e);

	/* The normal frame path reports size and key through venc_vsi.list_free
	 * (venc_get_free_buffers, venc_common_if.c:354-391; mtk_enc_put_buf, mtk_vcodec_enc.c:
	 * 113-178), because the kernel's ENCODE_DONE case is missing.  Prefer the ring when the
	 * daemon filled one for our bitstream cookie; otherwise fall back to the fields of
	 * VCU_IPIMSG_ENC_ENCODE_DONE, which is what a plain daemon answers. */
	memset(&result, 0, sizeof(result));
	bs_cookie = e->vsi->venc.venc_bs_va;
	pthread_mutex_lock(&e->reply_lock);
	for (unsigned i = 0; i < e->nfreed; i++) {
		if (e->freed[i].bs_va == bs_cookie && e->freed[i].bs_size != 0u) {
			result.bytes = e->freed[i].bs_size;
			result.is_key_frm = e->freed[i].is_key;
			result.state = VEN_IPI_MSG_ENC_STATE_FRAME;
			result.from_free_list = 1;
			break;
		}
	}
	if (!result.from_free_list) {
		/* A daemon that echoes a different cookie (the vendor's ring is filled from its
		 * own record) still has exactly one completed job in this single-session API. */
		for (unsigned i = e->nfreed; i > 0; i--) {
			if (e->freed[i - 1].bs_size != 0u) {
				result.bytes = e->freed[i - 1].bs_size;
				result.is_key_frm = e->freed[i - 1].is_key;
				result.state = VEN_IPI_MSG_ENC_STATE_FRAME;
				result.from_free_list = 1;
				break;
			}
		}
	}
	if (!result.from_free_list && e->enc_done_seen) {
		result.bytes = e->enc_done.bs_size;
		result.is_key_frm = e->enc_done.is_key_frm;
		result.state = e->enc_done.state;
	}
	pthread_mutex_unlock(&e->reply_lock);
	if (bs_guest != 0u && result.bytes != 0u) {
		uint32_t n = result.bytes < bs_bytes ? result.bytes : bs_bytes;

		(void)n2vc_dma_sync(e->hw, NULL, bs_guest, 0, n, 0);
	}
	*res = result;
	return 0;
}

/*
 * The product daemon calls n2ap_venc_encode(), not encode_fd(), and the vendor ring buffer inside
 * libvpud_vcodec still ion_import()s the two ids from the IPI message.  When V34's ion shim is in
 * the link, publish the handles through its integrator hook so that call keeps working with fd 0
 * absent: a stable id per handle, allocated in 0x520..0x55f (outside the host runner's explicit
 * 0x500/0x501) and reused for every job.  Weak symbol: a link without the shim keeps fd 0.
 */
extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa,
                                     uint32_t bytes) __attribute__((weak));

#define N2AP_SHARE_MIN   0x520
#define N2AP_SHARE_IDS   64
#define N2AP_SHARE_SLOTS 64

struct n2ap_share_ent {
	uint32_t handle;
	uint32_t bytes;
	int16_t id;
};

static struct {
	pthread_mutex_t lock;
	struct n2ap_share_ent e[N2AP_SHARE_SLOTS];
	unsigned used;
	unsigned next;
} n2ap_shares = { PTHREAD_MUTEX_INITIALIZER, {{0, 0, 0}}, 0, 0 };

static int16_t ven_share_fd(uint32_t handle, uint32_t pa, uint32_t bytes)
{
	unsigned i, tries;
	int16_t id = 0;

	if (n2shim_ion_register_share == NULL || handle == 0u || pa == 0u || bytes == 0u) {
		return 0;
	}
	pthread_mutex_lock(&n2ap_shares.lock);
	for (i = 0; i < n2ap_shares.used; i++) {
		if (n2ap_shares.e[i].handle == handle) {
			id = n2ap_shares.e[i].id;
			pthread_mutex_unlock(&n2ap_shares.lock);
			return id;
		}
	}
	if (n2ap_shares.used >= N2AP_SHARE_SLOTS) {
		pthread_mutex_unlock(&n2ap_shares.lock);
		return 0;   /* table full: no fd rather than a random share id */
	}
	for (tries = 0; tries < N2AP_SHARE_IDS; tries++) {
		int cand = N2AP_SHARE_MIN + (int)(n2ap_shares.next++ % N2AP_SHARE_IDS);

		if (n2shim_ion_register_share(cand, handle, pa, bytes) == 0) {
			n2ap_shares.e[n2ap_shares.used].handle = handle;
			n2ap_shares.e[n2ap_shares.used].bytes = bytes;
			n2ap_shares.e[n2ap_shares.used].id = (int16_t)cand;
			n2ap_shares.used++;
			id = (int16_t)cand;
			break;
		}
	}
	pthread_mutex_unlock(&n2ap_shares.lock);
	return id;
}

int n2ap_venc_encode(struct n2ap_venc *e, uint32_t frame_handle, uint32_t bs_handle,
                     unsigned flags, struct n2ap_enc_result *res)
{
	uint32_t pa = 0, guest = 0, bytes = 0;
	int frame_fd = 0, bs_fd = 0;

	if (e != NULL && e->opened) {
		if (frame_handle != 0u && n2vc_dma_lookup_handle(frame_handle, &pa, &guest, &bytes) == 0) {
			frame_fd = ven_share_fd(frame_handle, pa, bytes);
		}
		if (bs_handle != 0u && n2vc_dma_lookup_handle(bs_handle, &pa, &guest, &bytes) == 0) {
			bs_fd = ven_share_fd(bs_handle, pa, bytes);
		}
	}
	return n2ap_venc_encode_fd(e, frame_handle, frame_fd, bs_handle, bs_fd, flags, res);
}

int n2ap_venc_wait_done(struct n2ap_venc *e, unsigned timeout_ms,
                        struct n2ap_enc_result *res)
{
	uint64_t cookie;
	struct timespec t0, now;
	uint32_t bs_guest = 0, bs_bytes = 0;
	unsigned i;

	if (e == NULL || !e->opened || e->vsi == NULL || res == NULL) {
		return -EINVAL;
	}
	cookie = e->vsi->venc.venc_bs_va;
	pthread_mutex_lock(&e->reply_lock);
	bs_guest = e->last_bs_guest;
	bs_bytes = e->last_bs_bytes;
	pthread_mutex_unlock(&e->reply_lock);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		int found = 0;

		pthread_mutex_lock(&e->reply_lock);
		for (i = 0; i < e->nfreed; i++) {
			if (e->freed[i].bs_va == cookie && e->freed[i].bs_size != 0u) {
				res->bytes = e->freed[i].bs_size;
				res->is_key_frm = e->freed[i].is_key;
				res->state = VEN_IPI_MSG_ENC_STATE_FRAME;
				res->from_free_list = 1;
				found = 1;
				break;
			}
		}
		if (!found) {
			/* A daemon that echoes a different cookie (the vendor's ring is filled from
			 * its own record) still has exactly one completed job in this single-session
			 * API; take the newest nonzero entry rather than reporting a timeout. */
			for (i = e->nfreed; i > 0; i--) {
				if (e->freed[i - 1].bs_size != 0u) {
					res->bytes = e->freed[i - 1].bs_size;
					res->is_key_frm = e->freed[i - 1].is_key;
					res->state = VEN_IPI_MSG_ENC_STATE_FRAME;
					res->from_free_list = 1;
					found = 1;
					break;
				}
			}
		}
		if (!found && e->enc_done_seen && e->enc_done.bs_size != 0u) {
			res->bytes = e->enc_done.bs_size;
			res->is_key_frm = e->enc_done.is_key_frm;
			res->state = e->enc_done.state;
			res->from_free_list = 0;
			found = 1;
		}
		pthread_mutex_unlock(&e->reply_lock);
		if (found) {
			/* The job completed after the AP had already returned from the ACK; bring the
			 * bytes the daemon reported into the guest mirror now (the encode-time sync
			 * could only copy the pre-job state, and a pa-only buffer needs no copy). */
			if (bs_guest != 0u && res->bytes != 0u) {
				uint32_t n = res->bytes < bs_bytes ? res->bytes : bs_bytes;

				(void)n2vc_dma_sync(e->hw, NULL, bs_guest, 0, n, 0);
			}
			return 0;
		}
		if (e->aborted) {
			return -EIO;
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((unsigned)((now.tv_sec - t0.tv_sec) * 1000 +
		               (now.tv_nsec - t0.tv_nsec) / 1000000) >= timeout_ms) {
			return -ETIMEDOUT;
		}
		ven_sleep_ms(1);
	}
}
