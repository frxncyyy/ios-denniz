/* SPDX-License-Identifier: MIT */
/*
 * n2ap_vdec.c -- the AP side of a MediaTek MT6893 VDEC session in user space.
 *
 * Owner: DEC01.  This file is the user-space equivalent of
 *   drivers/media/platform/mtk-vcodec/vdec_vcu_if.c
 *   drivers/media/platform/mtk-vcodec/vdec/vdec_common_if.c
 *   drivers/media/platform/mtk-vcodec/mt6885/mtk_vcodec_dec_pm.c   (clocks, racing words, HW break)
 *   drivers/media/platform/mtk-vcodec/mt6885/mtk_vcodec_intr.c     (the decoder ISR, as a wait)
 * for one decoder instance.  The codec core, the GCE lists and the bitstream parsing stay in the daemon
 * (the vendor library); everything above is protocol and power sequencing.
 *
 * Message flow (ap/VDEC_FLOW.md has the annotated version):
 *   cap:     AP_IPIMSG_DEC_QUERY_CAP (IPI_VDEC_COMMON) -> VCU_IPIMSG_DEC_QUERY_CAP_ACK
 *   open:    AP_IPIMSG_DEC_INIT                        -> VCU_IPIMSG_DEC_INIT_ACK {vcu_inst_addr}
 *   param:   AP_IPIMSG_DEC_SET_PARAM(FRAME_SIZE, ...)  -> VCU_IPIMSG_DEC_SET_PARAM_ACK
 *   decode:  AP_IPIMSG_DEC_START                       -> (LOCK/WAITISR/GET_FB/PUT_FB*) -> START_ACK
 *   flush:   AP_IPIMSG_DEC_RESET                       -> VCU_IPIMSG_DEC_RESET_ACK
 *   close:   AP_IPIMSG_DEC_DEINIT                      -> VCU_IPIMSG_DEC_DEINIT_ACK
 *
 * The daemon-to-AP requests are not polled: n2vcu calls the registered handler on the daemon's thread
 * while the AP sits in n2vcu_ap_wait_dispatched(), the same context in which the Linux kernel runs
 * vcu_dec_ipi_handler() (mtk_vcu.c:1396-1410).  Waits the kernel makes blocking (GET_FRAME_BUFFER's
 * one-second wait for a free buffer) are bounded here too; the API has no way to be interrupted.
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* V41's DMA registry (ap/n2vc_dma.c): handle -> pa/guest/bytes, for the message fields and the cache
 * sync (n2vc_dma_lookup_handle / n2vc_dma_sync). */
#include "n2ap_venc.h"
#include "n2a32.h"
#include "n2ap_vdec.h"
#include "n2vc_hw.h"
#include "mtk_vdec_ipi.h"
#include "vcu/n2vcu.h"

/* The iOS SDK stubs in gpu/video/sdk/sysinc/errno.h omit EPROTO/ENOTSUP; Darwin's values. */
#ifndef EPROTO
#define EPROTO 100
#endif
#ifndef ENOTSUP
#define ENOTSUP 45
#endif

#define DEC_DEFAULT_TIMEOUT_MS 6000u   /* IPI_TIMEOUT_MS, mtk_vcu.c:72 */
#define DEC_WAIT_ISR_TIMEOUT_MS 500u   /* WAIT_INTR_TIMEOUT_MS, mtk_vcodec_drv.h:35 */
#define DEC_GET_FB_TIMEOUT_MS  1000u   /* msecs_to_jiffies(1000), vdec_vcu_if.c:292 */
#define DEC_RACING_OFFSET      0x100u  /* MTK_VDEC_RACING_INFO_OFFSET */
#define DEC_REG_BATCH          128u    /* rwvc_reg_op per hw->regs call */

/* vsi ring geometry as a function of the entry count (mtk_vdec_ipi.h: ring_bs_list/ring_fb_list). */
static uint32_t vsi_bs_bytes(unsigned n) { return 8u * n + 16u; }
static uint32_t vsi_fb_bytes(unsigned n) { return 48u * n + 16u; }
static uint32_t vsi_dec_offset(unsigned n) { return vsi_bs_bytes(n) + 2u * vsi_fb_bytes(n); }

struct n2ap_vdec {
	struct n2vcu *vcu;
	const struct n2vc_hw *hw;
	unsigned ipi_id;
	unsigned timeout_ms;
	uint32_t fourcc;
	uint64_t cookie;          /* AP instance token; the daemon only echoes it, kept < 2^32 */
	uint32_t inst_addr;       /* VCU address of the vsi */
	struct vdec_vsi *vsi;     /* the mapped vsi (64-entry view; only the rings are read through it) */
	struct vdec_vsi_tail *t;  /* dec/pic/... at the offset the daemon's ring count implies */
	unsigned ring_entries;
	uint32_t cfg_planes;      /* vsi.dec.fb_num_planes the client asked for (or the codec default) */
	uint32_t cfg_fourcc;      /* required output fourcc, 0 = any */
	uint32_t dmem_guest;      /* the daemon's DMEM window, for the set_ring_entries bound */
	uint32_t dmem_bytes;
	int opened, aborted, no_power, fake_irq;
	int32_t last_status;
	int32_t hw_error;
	uint32_t proto_errors;
	int flushing;

	/* one outstanding AP message */
	uint32_t expecting;
	int reply_seen;
	uint32_t reply_msg_id;
	int32_t reply_status;
	pthread_mutex_t lock;     /* reply + client queues + power state */
	pthread_cond_t reply_cv;
	pthread_cond_t fb_cv;     /* GET_FRAME_BUFFER waits for a free buffer here */
	int sync_done;

	struct vdec_vcu_ipi_ack ack;   /* the last generic ack (status/ap_inst_addr) */

	/* power role (mtk_vcodec_dec_pm.c) */
	int active;               /* dec_active_cnt: LOCKs minus UNLOCKs */
	int soc_refs, core_refs;
	int soc_common;           /* 1: the SOC block is held for the whole active period */
	int racing_pending;       /* opus-decint: restore refused at a LAT-only lock, redo at the CORE lock */
	uint32_t racing[N2AP_VDEC_RACING_WORDS];
	int racing_valid;

	/* frame-buffer queues */
	struct n2ap_vdec_fb free_q[N2AP_VDEC_FB_QUEUE];
	unsigned nfree;
	struct n2ap_vdec_fb in_use[N2AP_VDEC_FB_QUEUE];
	unsigned n_in_use;
	struct n2ap_vdec_out out_q[N2AP_VDEC_OUT_QUEUE];
	unsigned nout;
	struct n2ap_vdec_freed freed_q[N2AP_VDEC_FREE_QUEUE];
	unsigned nfreed;
	uint64_t bs_free_q[N2AP_VDEC_FREE_QUEUE];
	unsigned nbs_freed;

	struct n2ap_vdec_stats st;

	void *query_out;
	uint32_t query_bytes;
	uint32_t query_size;

	n2ap_vdec_trace_fn trace;
	void *trace_arg;
};

static uint32_t dec_cookie_seq;

/*
 * Handler quiescence.  The AP handler runs on the daemon's thread (n2vcu hands it the payload from
 * VCU_SET_OBJECT), so close() must not free the session while a handler invocation is still inside it:
 * the sender is woken by dec_note_reply() from within the handler, so it can run ahead of the handler's
 * epilogue.  Every session in this process is registered here; the handler only dereferences `ctx`
 * when it finds it in the table, and close() removes the entry and waits for the in-flight calls to
 * return before unregistering and freeing.  A late call with a stale ctx is ignored without a read.
 */
#define DEC_MAX_LIVE 16
static struct {
	pthread_mutex_t lock;
	pthread_cond_t cv;
	struct n2ap_vdec *s[DEC_MAX_LIVE];
	unsigned inflight[DEC_MAX_LIVE];
} dec_live = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, { NULL }, { 0 } };

static int dec_live_add(struct n2ap_vdec *d)
{
	unsigned i;
	int rc = -ENOSPC;

	pthread_mutex_lock(&dec_live.lock);
	for (i = 0; i < DEC_MAX_LIVE; i++) {
		if (dec_live.s[i] == NULL && dec_live.inflight[i] == 0u) {
			dec_live.s[i] = d;
			rc = 0;
			break;
		}
	}
	pthread_mutex_unlock(&dec_live.lock);
	return rc;
}

/* -1 when this session is not (or no longer) live; otherwise the slot whose inflight count the caller
 * must drop with dec_live_leave(). */
static int dec_live_enter(struct n2ap_vdec *d)
{
	unsigned i;
	int slot = -1;

	if (d == NULL)
		return -1;
	pthread_mutex_lock(&dec_live.lock);
	for (i = 0; i < DEC_MAX_LIVE; i++) {
		if (dec_live.s[i] == d) {
			dec_live.inflight[i]++;
			slot = (int)i;
			break;
		}
	}
	pthread_mutex_unlock(&dec_live.lock);
	return slot;
}

static void dec_live_leave(int slot)
{
	if (slot < 0)
		return;
	pthread_mutex_lock(&dec_live.lock);
	if (dec_live.inflight[slot] > 0u)
		dec_live.inflight[slot]--;
	if (dec_live.inflight[slot] == 0u)
		pthread_cond_broadcast(&dec_live.cv);
	pthread_mutex_unlock(&dec_live.lock);
}

/* Remove the session from the table and wait for handler calls that are already inside it.  No new
 * call can enter afterwards: the table entry is gone before the wait starts. */
static void dec_live_remove_wait(struct n2ap_vdec *d)
{
	unsigned i;

	if (d == NULL)
		return;
	pthread_mutex_lock(&dec_live.lock);
	for (i = 0; i < DEC_MAX_LIVE; i++) {
		if (dec_live.s[i] == d) {
			dec_live.s[i] = NULL;
			while (dec_live.inflight[i] != 0u)
				pthread_cond_wait(&dec_live.cv, &dec_live.lock);
			break;
		}
	}
	pthread_mutex_unlock(&dec_live.lock);
}

/* The QUERY_CAP handler lives on an IPI id no session owns (IPI_VDEC_COMMON) and the kernel has exactly
 * one handler slot for it (vcu_ipi_register), so this is a single global: one query at a time. */
static struct {
	struct n2ap_vdec *owner;
	int registered;
	pthread_mutex_t lock;
} dec_query = { NULL, 0, PTHREAD_MUTEX_INITIALIZER };

static void dec_trace(struct n2ap_vdec *d, const char *event, uint32_t a, uint32_t b, uint32_t c)
{
	if (d != NULL && d->trace != NULL)
		d->trace(d->trace_arg, event, a, b, c);
}

/* Protocol defects are counted from the handler thread, from the client's thread and from close();
 * the counter is atomic so the reader never races. */
static void dec_proto(struct n2ap_vdec *d)
{
	(void)__atomic_add_fetch(&d->proto_errors, 1u, __ATOMIC_RELAXED);
}

/* First hardware-backend error wins; written from the handler thread and read by the sender, so
 * atomic rather than locked (dec_send must not take the client mutex it may already hold paths into). */
static void dec_hw_error(struct n2ap_vdec *d, int rc)
{
	int zero = 0;

	if (rc != 0)
		(void)__atomic_compare_exchange_n(&d->hw_error, &zero, rc, 0,
		                                  __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

static void dec_sleep_us(unsigned us)
{
	struct timespec ts;

	ts.tv_sec = (time_t)(us / 1000000u);
	ts.tv_nsec = (long)(us % 1000000u) * 1000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
		/* keep the remaining time */
	}
}

static uint32_t dec_rd32(const void *p)
{
	uint32_t v;

	memcpy(&v, p, sizeof v);
	return v;
}

/* ---- register helpers (frozen n2vc_hw.h) ---------------------------------------------------- */

static int dec_reg_rd(struct n2ap_vdec *d, unsigned win, uint32_t off, uint32_t *out)
{
	struct rwvc_reg_op op = { RWVC_OP_READ, (uint8_t)win, 0, off, 0, 0 };
	unsigned done = 0;
	int rc;

	if (d->hw == NULL || d->hw->regs == NULL)
		return -ENODEV;
	rc = d->hw->regs(d->hw->ctx, &op, 1, &done);
	if (rc != 0 || done != 1)
		return rc != 0 ? rc : -EIO;
	*out = op.value;
	return 0;
}

static int dec_reg_wr(struct n2ap_vdec *d, unsigned win, uint32_t off, uint32_t v)
{
	struct rwvc_reg_op op = { RWVC_OP_WRITE, (uint8_t)win, 0, off, v, 0 };
	unsigned done = 0;
	int rc;

	if (d->hw == NULL || d->hw->regs == NULL)
		return -ENODEV;
	rc = d->hw->regs(d->hw->ctx, &op, 1, &done);
	if (rc != 0 || done != 1)
		return rc != 0 ? rc : -EIO;
	return 0;
}

/* ---- power: racing words + HW break + the block references (mtk_vcodec_dec_pm.c) ------------- */

/* The 68 racing words at VDEC_RACING+0x100: read (save) or write (restore) them in two batches. */
static int dec_racing(struct n2ap_vdec *d, int save)
{
	struct rwvc_reg_op ops[DEC_REG_BATCH];
	unsigned base = 0;
	int rc = 0;

	if (d->hw == NULL || d->hw->regs == NULL)
		return -ENODEV;
	while (base < N2AP_VDEC_RACING_WORDS) {
		unsigned n = N2AP_VDEC_RACING_WORDS - base;
		unsigned done = 0, i;

		if (n > DEC_REG_BATCH)
			n = DEC_REG_BATCH;
		for (i = 0; i < n; i++) {
			ops[i].op = save ? RWVC_OP_READ : RWVC_OP_WRITE;
			ops[i].window = (uint8_t)RWVC_WIN_VDEC_RACING;
			ops[i].reserved = 0;
			ops[i].offset = DEC_RACING_OFFSET + 4u * (base + i);
			ops[i].value = save ? 0u : d->racing[base + i];
			ops[i].mask = 0;
		}
		rc = d->hw->regs(d->hw->ctx, ops, n, &done);
		if (rc != 0 || done != n) {
			rc = rc != 0 ? rc : -EIO;
			break;
		}
		if (save) {
			for (i = 0; i < n; i++)
				d->racing[base + i] = ops[i].value;
		}
		base += n;
	}
	if (rc == 0 && save)
		d->racing_valid = 1;
	if (rc == 0)
		d->st.racing_saves += save ? 1u : 0u;
	if (rc == 0 && !save)
		d->st.racing_restores++;
	return rc;
}

/* mtk_vdec_hw_break(dev, MTK_VDEC_CORE), mtk_vcodec_dec_pm.c:296-401. */
static int dec_hw_break_core(struct n2ap_vdec *d)
{
	uint32_t v = 0, cg = 0, ufo = 0;
	unsigned waited_us = 0;
	int is_ufo;
	int rc;

	rc = dec_reg_rd(d, RWVC_WIN_VDEC_SYS, 0, &v);
	if (rc != 0)
		return rc;
	if (v == 0) {
		dec_trace(d, "hw_break", MTK_VDEC_CORE, 0, rc);   /* "not HW break since clk off" */
		return 0;
	}
	rc = dec_reg_rd(d, RWVC_WIN_VDEC_BASE, 0x800u + 0x08cu, &ufo);
	is_ufo = (rc == 0) && (ufo & 1u);
	rc = dec_reg_rd(d, RWVC_WIN_VDEC_MISC, 0x100u, &v);
	if (rc != 0)
		return rc;
	rc = dec_reg_wr(d, RWVC_WIN_VDEC_MISC, 0x100u, v | 1u);
	if (rc != 0)
		return rc;
	if (is_ufo) {
		rc = dec_reg_rd(d, RWVC_WIN_VDEC_BASE, 0x800u + 0x01cu, &v);
		if (rc != 0)
			return rc;
		rc = dec_reg_wr(d, RWVC_WIN_VDEC_BASE, 0x800u + 0x01cu, v & ~2u);
		if (rc != 0)
			return rc;
	}
	/* The kernel polls 20 ms, then 1 s, then gives up and still resets the VLD. */
	for (;;) {
		rc = dec_reg_rd(d, RWVC_WIN_VDEC_MISC, 0x104u, &cg);
		if (rc != 0)
			return rc;
		if ((cg & 0x11u) == 0x11u) {
			if (!is_ufo)
				break;
			rc = dec_reg_rd(d, RWVC_WIN_VDEC_BASE, 0x800u + 0x08cu, &ufo);
			if (rc != 0)
				return rc;
			if ((ufo & 0x11000u) == 0x11000u)
				break;
		}
		if (waited_us >= N2AP_VDEC_HW_BREAK_TIMEOUT_US) {
			/* second stage of the kernel's timeout: up to 1 s in total */
			if (waited_us >= N2AP_VDEC_HW_BREAK_TIMEOUT_US + 1000000u) {
				d->st.hw_break_timeouts++;
				dec_trace(d, "hw_break", MTK_VDEC_CORE, 1, cg);
				break;
			}
		}
		dec_sleep_us(50);
		waited_us += 50;
	}
	if (is_ufo) {
		rc = dec_reg_rd(d, RWVC_WIN_VDEC_BASE, 0x800u + 0x01cu, &v);
		if (rc != 0)
			return rc;
		rc = dec_reg_wr(d, RWVC_WIN_VDEC_BASE, 0x800u + 0x01cu, v | 2u);
		if (rc != 0)
			return rc;
	}
	rc = dec_reg_wr(d, RWVC_WIN_VDEC_VLD, 0x108u, 1u);
	if (rc != 0)
		return rc;
	rc = dec_reg_wr(d, RWVC_WIN_VDEC_VLD, 0x108u, 0u);
	if (rc != 0)
		return rc;
	d->st.hw_breaks++;
	dec_trace(d, "hw_break", MTK_VDEC_CORE, 0, 0);
	return 0;
}

/* mtk_vdec_hw_break(dev, MTK_VDEC_LAT), mtk_vcodec_dec_pm.c:402-460. */
static int dec_hw_break_lat(struct n2ap_vdec *d)
{
	uint32_t v = 0, cg = 0;
	unsigned waited_us = 0;
	int rc;

	rc = dec_reg_rd(d, RWVC_WIN_VDEC_SOC_GCON, 0x200u, &v);
	if (rc != 0)
		return rc;
	if (v == 0) {
		dec_trace(d, "hw_break", MTK_VDEC_LAT, 0, 0);
		return 0;
	}
	rc = dec_reg_rd(d, RWVC_WIN_VDEC_LAT_MISC, 0x100u, &v);
	if (rc != 0)
		return rc;
	rc = dec_reg_wr(d, RWVC_WIN_VDEC_LAT_MISC, 0x100u, v | 1u);
	if (rc != 0)
		return rc;
	for (;;) {
		rc = dec_reg_rd(d, RWVC_WIN_VDEC_LAT_MISC, 0x104u, &cg);
		if (rc != 0)
			return rc;
		if ((cg & 0x1u) != 0u && (cg & 0x10u) != 0u)
			break;
		if (waited_us >= N2AP_VDEC_HW_BREAK_TIMEOUT_US + 1000000u) {
			d->st.hw_break_timeouts++;
			dec_trace(d, "hw_break", MTK_VDEC_LAT, 1, cg);
			break;
		}
		dec_sleep_us(50);
		waited_us += 50;
	}
	rc = dec_reg_wr(d, RWVC_WIN_VDEC_LAT_VLD, 0x108u, 1u);
	if (rc != 0)
		return rc;
	rc = dec_reg_wr(d, RWVC_WIN_VDEC_LAT_VLD, 0x108u, 0u);
	if (rc != 0)
		return rc;
	d->st.hw_breaks++;
	dec_trace(d, "hw_break", MTK_VDEC_LAT, 0, 0);
	return 0;
}

static int dec_block_power(struct n2ap_vdec *d, int core, int on)
{
	unsigned blk = core ? RWVC_BLK_VDEC_CORE : RWVC_BLK_VDEC_SOC;
	int rc;

	if (d->no_power)
		return 0;
	if (d->hw == NULL || d->hw->power == NULL)
		return -ENODEV;
	rc = d->hw->power(d->hw->ctx, blk, on);
	if (rc == 0) {
		if (on)
			d->st.power_on++;
		else
			d->st.power_off++;
	} else if (on) {
		dec_hw_error(d, rc);
	}
	/* opus-decint (phone run vc19): a failed power-OFF is not a decode error.  The picture is finished (both done
	 * interrupts were seen) and the kernel has already dropped our reference (on a bus-protection ack timeout it leaves
	 * the domain powered and keeps the parent's reference); failing the START here threw away a decoded picture. */
	dec_trace(d, "power", blk, (uint32_t)(on != 0), (uint32_t)(rc == 0 ? 0u : (uint32_t)(-rc)));
	return rc;
}

/*
 * LOCK_CORE/LOCK_LAT -> vdec_decode_prepare() -> mtk_vcodec_dec_clock_on(): power the block, and on the
 * first active user restore the racing words (mtk_vcodec_dec_pm.c:200-213).
 * UNLOCK_* -> mtk_vcodec_dec_clock_off(): on the last user save them, break the hardware still running
 * on this core, then drop the block (mtk_vcodec_dec_pm.c:253-288).
 */
static int dec_lock(struct n2ap_vdec *d, int core)
{
	int rc;

	pthread_mutex_lock(&d->lock);
	if (core)
		d->st.locks_core++;
	else
		d->st.locks_lat++;
	/* mtk_vcodec_dec_clock_on() enables MT_CG_SOC in *both* branches (:170-186), and the racing words
	 * live in the SOC's register range (0x16004000), so the first user takes the SOC block whatever
	 * core it locks; a CORE lock then takes the CORE block too (the model powers its parent SOC with
	 * it, like the kernel's hidden parent reference). */
	rc = 0;
	if (d->active == 0) {
		rc = dec_block_power(d, 0, 1);
		if (rc == 0) {
			d->soc_common = 1;
			d->racing_pending = 1;   /* restored once VDEC_CORE is powered (below) */
		}
	}
	if (rc == 0 && core)
		rc = dec_block_power(d, 1, 1);
	/* opus-decint: the kernel's VDEC_RACING window (and RWVC_WIN_VDEC_ALL page 0x04) belongs to VDEC_CORE, so the
	 * restore of a LAT-first period is refused with EACCES (DEC02 measured it every AU: the vendor locks LAT before
	 * CORE).  Do it as soon as the CORE lock has powered the core, before the vendor's first core register access
	 * (the LOCK_CORE ack is what lets the vendor touch the core).  Same words, same order as clock_on. */
	if (rc == 0 && core && d->racing_pending) {
		d->racing_pending = 0;
		/* only words this session saved from the hardware go back: before the first save there is nothing
		 * to restore, and writing the zero-initialised copy over the core's reset values could only harm */
		if (d->racing_valid && dec_racing(d, 0) == 0)
			dec_trace(d, "racing_restore", N2AP_VDEC_RACING_WORDS, 1, 0);
	}
	if (rc == 0) {
		if (core)
			d->core_refs++;
		else
			d->soc_refs++;
		d->active++;
	}
	dec_trace(d, "lock", (uint32_t)core, (uint32_t)d->active,
	          (uint32_t)(rc == 0 ? 0u : (uint32_t)(-rc)));
	pthread_mutex_unlock(&d->lock);
	return rc;
}

static int dec_unlock(struct n2ap_vdec *d, int core)
{
	int rc = 0, prc, had_ref;

	pthread_mutex_lock(&d->lock);
	if (core) {
		d->st.unlocks_core++;
		had_ref = d->core_refs > 0;
		if (!had_ref)
			dec_proto(d);
		else
			d->core_refs--;
	} else {
		d->st.unlocks_lat++;
		had_ref = d->soc_refs > 0;
		if (!had_ref)
			dec_proto(d);
		else
			d->soc_refs--;
	}
	if (d->active > 0)
		d->active--;
	else
		dec_proto(d);
	/* mtk_vcodec_dec_clock_off(): the last user saves the racing words (mtk_vcodec_dec_pm.c:269-280) ...
	 * opus-decint: the words live in the VDEC_CORE domain (kernel window VDEC_RACING / VDEC_ALL page 0x04), so they are
	 * saved when the last CORE reference goes, just before the core is powered off; a LAT unlock that ends the active
	 * period after the core is already off (the HEVC/timeout order) has nothing left to read (EACCES on the phone). */
	if (core && had_ref && d->core_refs == 0) {
		if (dec_racing(d, 1) == 0)
			dec_trace(d, "racing_save", N2AP_VDEC_RACING_WORDS, 0, 0);
	}
	/* ... and every clock_off breaks the core it is releasing, still clocked (:282, :296-460). */
	if (had_ref) {
		if (core)
			(void)dec_hw_break_core(d);
		else
			(void)dec_hw_break_lat(d);
	}
	if (core) {
		prc = dec_block_power(d, 1, 0);
		if (prc != 0 && rc == 0)
			rc = prc;
	}
	if (d->active == 0 && d->soc_common) {
		d->soc_common = 0;
		d->racing_pending = 0;
		prc = dec_block_power(d, 0, 0);
		if (prc != 0 && rc == 0)
			rc = prc;
	}
	dec_trace(d, "unlock", (uint32_t)core, (uint32_t)d->active,
	          (uint32_t)(rc == 0 ? 0u : (uint32_t)(-rc)));
	pthread_mutex_unlock(&d->lock);
	return rc;
}

/* ---- reply matching (the kernel's ack wait, mtk_vcu.c:418-446) -------------------------------- */

static void dec_note_reply(struct n2ap_vdec *d, uint32_t msg_id, int32_t status)
{
	pthread_mutex_lock(&d->lock);
	d->reply_msg_id = msg_id;
	d->reply_status = status;
	d->reply_seen = 1;
	pthread_cond_broadcast(&d->reply_cv);
	pthread_mutex_unlock(&d->lock);
}

static int dec_wait_reply(struct n2ap_vdec *d)
{
	struct timespec dl;
	uint64_t ns;
	int rc = 0;

	if (clock_gettime(CLOCK_REALTIME, &dl) != 0)
		return -EIO;
	ns = (uint64_t)dl.tv_nsec + (uint64_t)d->timeout_ms * 1000000ull;
	dl.tv_sec += (time_t)(ns / 1000000000ull);
	dl.tv_nsec = (long)(ns % 1000000000ull);
	pthread_mutex_lock(&d->lock);
	while (!d->reply_seen && rc == 0)
		rc = pthread_cond_timedwait(&d->reply_cv, &d->lock, &dl);
	if (d->reply_seen)
		rc = 0;
	else if (rc == ETIMEDOUT)
		rc = -ETIMEDOUT;
	else if (rc != 0)
		rc = -EIO;
	pthread_mutex_unlock(&d->lock);
	return rc;
}

static int dec_is_reply_for(uint32_t ap_id, uint32_t vcu_id)
{
	switch (ap_id) {
	case AP_IPIMSG_DEC_INIT:
		return vcu_id == VCU_IPIMSG_DEC_INIT_ACK;
	case AP_IPIMSG_DEC_START:
		return vcu_id == VCU_IPIMSG_DEC_START_ACK;
	case AP_IPIMSG_DEC_END:
		return vcu_id == VCU_IPIMSG_DEC_END_ACK;
	case AP_IPIMSG_DEC_DEINIT:
		return vcu_id == VCU_IPIMSG_DEC_DEINIT_ACK;
	case AP_IPIMSG_DEC_RESET:
		return vcu_id == VCU_IPIMSG_DEC_RESET_ACK;
	case AP_IPIMSG_DEC_SET_PARAM:
		return vcu_id == VCU_IPIMSG_DEC_SET_PARAM_ACK;
	case AP_IPIMSG_DEC_QUERY_CAP:
		return vcu_id == VCU_IPIMSG_DEC_QUERY_CAP_ACK;
	default:
		return 0;
	}
}

/* ---- the frame-buffer queues ----------------------------------------------------------------- */

/* meter the daemon's structure into the client's: find the cookie of an in-use buffer. */
static struct n2ap_vdec_fb *dec_fb_by_cookie(struct n2ap_vdec *d, uint64_t cookie)
{
	unsigned i;

	for (i = 0; i < d->n_in_use; i++)
		if (d->in_use[i].cookie == cookie)
			return &d->in_use[i];
	return NULL;
}

static void dec_fb_drop_in_use(struct n2ap_vdec *d, struct n2ap_vdec_fb *fb)
{
	unsigned i;

	for (i = 0; i < d->n_in_use; i++) {
		if (&d->in_use[i] == fb) {
			d->in_use[i] = d->in_use[--d->n_in_use];
			memset(&d->in_use[d->n_in_use], 0, sizeof d->in_use[0]);
			return;
		}
	}
}

/* from_device sync of one buffer's planes, when it has a guest mirror (n2vc_dma_lookup_handle). */
static void dec_fb_sync_out(struct n2ap_vdec *d, uint32_t handle, uint32_t pa, uint64_t dma, uint32_t bytes)
{
	uint32_t lpa = 0, guest = 0, lbytes = 0;

	(void)pa;
	if (d->hw == NULL)
		return;
	if (handle != 0 && n2vc_dma_lookup_handle(handle, &lpa, &guest, &lbytes) == 0 && guest != 0) {
		if (lbytes != 0 && (bytes == 0 || bytes > lbytes))
			bytes = lbytes;
		(void)n2vc_dma_sync(d->hw, NULL, guest, 0, bytes, 0);
		d->st.syncs++;
	} else if (dma != 0) {
		/* The buffer was registered by pa only: the caller owns its visibility, nothing to copy. */
		(void)lpa;
	}
}

/* GET_FRAME_BUFFER (vdec_vcu_if.c:287-384): pop a free buffer, fill vsi.dec, index 0xFF on timeout. */
static int dec_get_frame_buffer(struct n2ap_vdec *d)
{
	struct n2ap_vdec_fb fb;
	struct timespec dl;
	int have = 0;
	uint32_t planes, i;

	pthread_mutex_lock(&d->lock);
	if (d->nfree > 0) {
		fb = d->free_q[0];
		memmove(&d->free_q[0], &d->free_q[1], (d->nfree - 1) * sizeof d->free_q[0]);
		d->nfree--;
		have = 1;
	} else if (d->flushing) {
		have = 0;   /* the kernel returns at once in MTK_STATE_FLUSH */
	} else {
		/* wait_event_interruptible_timeout(ctx->fm_wq, ..., 1 s) */
		if (clock_gettime(CLOCK_REALTIME, &dl) == 0) {
			uint64_t ns = (uint64_t)dl.tv_nsec + (uint64_t)DEC_GET_FB_TIMEOUT_MS * 1000000ull;

			dl.tv_sec += (time_t)(ns / 1000000000ull);
			dl.tv_nsec = (long)(ns % 1000000000ull);
			while (d->nfree == 0 && !d->flushing) {
				if (pthread_cond_timedwait(&d->fb_cv, &d->lock, &dl) != 0)
					break;
			}
		}
		if (d->nfree > 0) {
			fb = d->free_q[0];
			memmove(&d->free_q[0], &d->free_q[1], (d->nfree - 1) * sizeof d->free_q[0]);
			d->nfree--;
			have = 1;
		}
	}
	if (!have) {
		d->t->dec.index = 0xFF;     /* vdec_vcu_if.c:353 */
		d->t->dec.vdec_fb_va = 0;
		for (i = 0; i < MTK_VIDEO_MAX_PLANES; i++) {
			d->t->dec.fb_dma[i] = 0;
			d->t->dec.fb_fd[i] = 0;
		}
		d->st.get_fb_empty++;
		dec_trace(d, "get_fb", 0, 0, 0);
		pthread_mutex_unlock(&d->lock);
		return 0;
	}
	planes = fb.c_handle != 0u ? 2u : 1u;
	{
		uint32_t pa = 0, guest = 0, bytes = 0, c_pa = 0;
		int bad = 0;

		if (n2vc_dma_lookup_handle(fb.handle, &pa, &guest, &bytes) != 0 || pa == 0u)
			bad = 1;
		if (!bad && planes >= 2u) {
			if (fb.c_handle == fb.handle) {
				if (d->t->pic.fb_sz[0] == 0u)
					bad = 1;
				else
					c_pa = pa + d->t->pic.fb_sz[0];
			} else if (n2vc_dma_lookup_handle(fb.c_handle, &c_pa, &guest, &bytes) != 0 ||
			           c_pa == 0u) {
				bad = 1;
			}
		}
		if (bad) {
			/* vdec_vcu_if.c:347-355 has no failure path: the daemon must see "no buffer",
			 * never a zero DMA address it would program as the output target. */
			dec_proto(d);
			d->t->dec.index = 0xFF;
			d->t->dec.vdec_fb_va = 0;
			for (i = 0; i < MTK_VIDEO_MAX_PLANES; i++) {
				d->t->dec.fb_dma[i] = 0;
				d->t->dec.fb_fd[i] = 0;
			}
			d->st.get_fb_empty++;
			dec_trace(d, "get_fb", 0, 1, 0);
			pthread_mutex_unlock(&d->lock);
			return 0;
		}
		d->t->dec.vdec_fb_va = fb.cookie;
		d->t->dec.index = fb.index;
		d->t->dec.fb_num_planes = planes;
		d->t->dec.fb_dma[0] = pa;
		d->t->dec.fb_fd[0] = (uint64_t)(uint32_t)fb.fd;
		for (i = 1; i < MTK_VIDEO_MAX_PLANES; i++) {
			d->t->dec.fb_dma[i] = 0;
			d->t->dec.fb_fd[i] = 0;
		}
		if (planes >= 2u) {
			d->t->dec.fb_dma[1] = c_pa;
			d->t->dec.fb_fd[1] = (uint64_t)(uint32_t)(fb.c_handle == fb.handle ?
			                                          fb.fd : fb.c_fd);
		}
	}
	/* no general_buf in this client: the kernel writes -1 and leaves size 0 (vdec_vcu_if.c:347-351) */
	d->t->general_buf_fd = -1;
	d->t->general_buf_dma = 0;
	d->t->general_buf_size = 0;
	if (d->n_in_use < N2AP_VDEC_FB_QUEUE) {
		fb.in_disp = 0;
		fb.in_free = 0;
		d->in_use[d->n_in_use++] = fb;
	} else {
		dec_proto(d);
	}
	d->st.get_fb++;
	dec_trace(d, "get_fb", fb.index, 0, (uint32_t)(fb.cookie & 0xffffffffu));
	pthread_mutex_unlock(&d->lock);
	return 1;
}

/*
 * PUT_FRAME_BUFFER (mtk_vdec_put_fb, mtk_vcodec_dec.c:474-549): harvest list_disp and list_free; the
 * same harvester also drains list_free_bs, which the kernel reads through GET_PARAM_FREE_BITSTREAM_BUFFER
 * (vdec_vcu_if.c:394-410).  `from_daemon` distinguishes the daemon's explicit request from the AP-side
 * harvest after a synchronous START (the vendor sends no PUT_FRAME_BUFFER in that mode).
 *
 * list_free can arrive before list_disp for the same cookie (B-frames), so the in-use record carries
 * two flags and is dropped only when both rings have returned it.  The device->guest copies are done
 * after the mutex is dropped: they can be megabytes and no other client-queue access needs to wait.
 */
struct dec_sync_req {
	uint32_t handle;
	uint32_t bytes;
};

static void dec_harvest(struct n2ap_vdec *d, int from_daemon)
{
	unsigned n_disp = 0, n_free = 0, n_bs = 0, n_sync = 0, i;
	struct dec_sync_req sync[N2AP_VDEC_OUT_QUEUE * 2u];

	pthread_mutex_lock(&d->lock);
	/* list_disp: display order, POC and timestamp come from the daemon (vdec_get_fb(list_disp)) */
	while (__atomic_load_n(&d->vsi->list_disp.count, __ATOMIC_ACQUIRE) > 0) {
		struct ring_fb_list *l = &d->vsi->list_disp;
		uint32_t idx = l->read_idx;
		struct vdec_ipi_fb e;

		if (idx >= d->ring_entries) {
			dec_proto(d);
			break;
		}
		memcpy(&e, &l->fb_list[idx], sizeof e);
		{
			struct n2ap_vdec_fb *fb = dec_fb_by_cookie(d, e.vdec_fb_va);

			if (d->nout < N2AP_VDEC_OUT_QUEUE) {
				struct n2ap_vdec_out *o = &d->out_q[d->nout++];

				o->cookie = e.vdec_fb_va;
				o->poc = e.poc;
				o->timestamp = e.timestamp;
				o->y_dma = e.y_fb_dma;
				o->c_dma = e.c_fb_dma;
			} else {
				dec_proto(d);   /* client too slow: the entry is consumed below */
			}
			d->st.frames_displayed++;
			if (fb != NULL) {
				/* The picture is done even if the client queue was full: sync it and let
				 * the buffer become reusable (a dropped entry must not leak the fb). */
				fb->in_disp = 1;
				if (n_sync + 2u <= sizeof sync / sizeof sync[0]) {
					if (fb->c_handle == fb->handle) {
						/* both planes in one allocation: one whole-buffer copy */
						sync[n_sync].handle = fb->handle;
						sync[n_sync].bytes = 0;
						n_sync++;
					} else {
						sync[n_sync].handle = fb->handle;
						sync[n_sync].bytes = fb->bytes;
						n_sync++;
						if (fb->c_handle != 0u) {
							sync[n_sync].handle = fb->c_handle;
							sync[n_sync].bytes = 0;
							n_sync++;
						}
					}
				} else {
					dec_proto(d);   /* cannot happen: one request per frame buffer */
				}
				if (fb->in_free)
					dec_fb_drop_in_use(d, fb);
			}
		}
		l->read_idx = (idx + 1u == d->ring_entries) ? 0u : idx + 1u;
		(void)__atomic_sub_fetch(&l->count, 1u, __ATOMIC_RELEASE);
		n_disp++;
	}
	/* list_free: buffers the daemon no longer references */
	while (__atomic_load_n(&d->vsi->list_free.count, __ATOMIC_ACQUIRE) > 0) {
		struct ring_fb_list *l = &d->vsi->list_free;
		uint32_t idx = l->read_idx;
		struct vdec_ipi_fb e;

		if (idx >= d->ring_entries) {
			dec_proto(d);
			break;
		}
		memcpy(&e, &l->fb_list[idx], sizeof e);
		if (d->nfreed < N2AP_VDEC_FREE_QUEUE) {
			struct n2ap_vdec_freed *f = &d->freed_q[d->nfreed++];

			f->cookie = e.vdec_fb_va;
			f->y_dma = e.y_fb_dma;
			f->c_dma = e.c_fb_dma;
			d->st.fbs_freed++;
		} else {
			dec_proto(d);
		}
		{
			struct n2ap_vdec_fb *fb = dec_fb_by_cookie(d, e.vdec_fb_va);

			if (fb != NULL) {
				fb->in_free = 1;
				if (fb->in_disp)
					dec_fb_drop_in_use(d, fb);
			}
		}
		l->read_idx = (idx + 1u == d->ring_entries) ? 0u : idx + 1u;
		(void)__atomic_sub_fetch(&l->count, 1u, __ATOMIC_RELEASE);
		n_free++;
	}
	/* list_free_bs: the bitstream cookie the daemon consumed (64-entry ring: must be drained) */
	while (__atomic_load_n(&d->vsi->list_free_bs.count, __ATOMIC_ACQUIRE) > 0) {
		struct ring_bs_list *l = &d->vsi->list_free_bs;
		uint32_t idx = l->read_idx;
		uint64_t cookie = 0;

		if (idx >= d->ring_entries) {
			dec_proto(d);
			break;
		}
		memcpy(&cookie, &l->vdec_bs_va_list[idx], sizeof cookie);
		if (d->nbs_freed < N2AP_VDEC_FREE_QUEUE) {
			d->bs_free_q[d->nbs_freed++] = cookie;
			d->st.bs_freed++;
		} else {
			dec_proto(d);
		}
		l->read_idx = (idx + 1u == d->ring_entries) ? 0u : idx + 1u;
		(void)__atomic_sub_fetch(&l->count, 1u, __ATOMIC_RELEASE);
		n_bs++;
	}
	if (from_daemon)
		d->st.put_fb++;
	d->st.harvests++;
	pthread_mutex_unlock(&d->lock);

	for (i = 0; i < n_sync; i++)
		dec_fb_sync_out(d, sync[i].handle, 0, 0, sync[i].bytes);
	dec_trace(d, "put_fb", n_disp, n_free, n_bs);
}

/* ---- the AP IPI handler (vcu_dec_ipi_handler, vdec_vcu_if.c:185-407) ------------------------- */

static int dec_dispatch(struct n2ap_vdec *d, int ipi_id, void *buf, uint32_t len)
{
	uint32_t msg_id, expecting;

	if (d == NULL || buf == NULL)
		return 1;
	if (len < sizeof(uint32_t)) {
		dec_proto(d);
		return 0;
	}
	msg_id = dec_rd32(buf);
	expecting = __atomic_load_n(&d->expecting, __ATOMIC_RELAXED);
	(void)ipi_id;

	/* Replies are checked against this instance before dispatch (vdec_vcu_if.c:221-225); the daemon's
	 * own requests (>= VCU_IPIMSG_DEC_WAITISR) are exempt. */
	if (msg_id < VCU_IPIMSG_DEC_WAITISR && msg_id >= VCU_IPIMSG_DEC_INIT_ACK) {
		uint64_t inst;

		if (len < 16u) {
			dec_proto(d);
			return 1;
		}
		memcpy(&inst, (const uint8_t *)buf + 8, sizeof inst);
		if (inst != d->cookie) {
			dec_proto(d);
			return 1;   /* no ack */
		}
	}

	switch (msg_id) {
	case VCU_IPIMSG_DEC_INIT_ACK: {
		struct vdec_vcu_ipi_init_ack m;

		if (len < 16u)
			goto short_msg;
		memset(&m, 0, sizeof m);
		memcpy(&m, buf, len < sizeof m ? len : sizeof m);
		d->last_status = m.status;
		if (m.status == VDEC_IPI_MSG_STATUS_OK)
			d->inst_addr = m.vcu_inst_addr;
		/* Nothing below touches the session after dec_note_reply() has woken the sender: the
		 * trace hook is the last access, and close() also quiesces the handler (dec_live). */
		dec_trace(d, "init_done", (uint32_t)m.status, m.vcu_inst_addr, 0);
		if (dec_is_reply_for(expecting, msg_id))
			dec_note_reply(d, msg_id, m.status);
		return 0;
	}
	case VCU_IPIMSG_DEC_QUERY_CAP_ACK: {
		struct vdec_vcu_ipi_query_cap_ack m;
		uint32_t room;

		if (len < 16u)
			goto short_msg;
		memset(&m, 0, sizeof m);
		memcpy(&m, buf, len < sizeof m ? len : sizeof m);
		d->last_status = m.status;
		if (m.ap_inst_addr != d->cookie) {
			dec_proto(d);
			return 1;
		}
		room = d->query_size * MTK_MAX_DEC_CODECS_SUPPORT;
		if (m.status == VDEC_IPI_MSG_STATUS_OK && d->query_out != NULL && d->query_size != 0u &&
		    room <= d->query_bytes) {
			void *src = n2vcu_map_dm_addr(d->vcu, m.vcu_data_addr);

			if (src != NULL)
				memcpy(d->query_out, src, room);
			else {
				dec_proto(d);
				m.status = -EIO;
			}
		}
		dec_trace(d, "cap_done", (uint32_t)m.status, m.id, m.vcu_data_addr);
		if (dec_is_reply_for(expecting, msg_id))
			dec_note_reply(d, msg_id, m.status);
		return 0;
	}
	case VCU_IPIMSG_DEC_START_ACK:
	case VCU_IPIMSG_DEC_END_ACK:
	case VCU_IPIMSG_DEC_DEINIT_ACK:
	case VCU_IPIMSG_DEC_RESET_ACK:
	case VCU_IPIMSG_DEC_SET_PARAM_ACK: {
		struct vdec_vcu_ipi_ack m;

		if (len < 8u)
			goto short_msg;
		memset(&m, 0, sizeof m);
		memcpy(&m, buf, len < sizeof m ? len : sizeof m);
		d->ack = m;
		d->last_status = m.status;
		if (msg_id == VCU_IPIMSG_DEC_START_ACK)
			dec_trace(d, "decode_done", (uint32_t)m.status, 0, 0);
		else if (msg_id == VCU_IPIMSG_DEC_SET_PARAM_ACK)
			dec_trace(d, "param_done", (uint32_t)m.status, 0, 0);
		else if (msg_id == VCU_IPIMSG_DEC_RESET_ACK)
			dec_trace(d, "reset_ack", (uint32_t)m.status, 0, 0);
		else if (msg_id == VCU_IPIMSG_DEC_DEINIT_ACK)
			dec_trace(d, "deinit", (uint32_t)m.status, 0, 0);
		if (dec_is_reply_for(expecting, msg_id))
			dec_note_reply(d, msg_id, m.status);
		return 0;
	}
	case VCU_IPIMSG_DEC_LOCK_CORE:
	case VCU_IPIMSG_DEC_UNLOCK_CORE:
	case VCU_IPIMSG_DEC_LOCK_LAT:
	case VCU_IPIMSG_DEC_UNLOCK_LAT: {
		int core = (msg_id == VCU_IPIMSG_DEC_LOCK_CORE || msg_id == VCU_IPIMSG_DEC_UNLOCK_CORE);

		if (len < sizeof(struct vdec_vcu_ipi_ack))
			goto short_msg;
		if (msg_id == VCU_IPIMSG_DEC_LOCK_CORE || msg_id == VCU_IPIMSG_DEC_LOCK_LAT)
			(void)dec_lock(d, core);
		else
			(void)dec_unlock(d, core);
		return 1;   /* non-ack, like vdec_vcu_if.c:271-286 */
	}
	case VCU_IPIMSG_DEC_WAITISR: {
		struct vdec_vcu_ipi_ack m;
		uint32_t status;
		int rc;

		if (len < sizeof m)
			goto short_msg;
		memcpy(&m, buf, sizeof m);
		status = (uint32_t)m.status;   /* on entry: the core id (MTK_VDEC_CORE/LAT) */
		{
			unsigned irq = status == (uint32_t)MTK_VDEC_LAT ? RWVC_IRQ_VDEC_LAT : RWVC_IRQ_VDEC;
			uint32_t irq_status = 0;

			if (d->fake_irq) {
				/* host-only: T02's fake has no VDEC interrupt source */
				rc = 0;
				d->st.wait_isr++;
			} else if (d->hw != NULL && d->hw->wait_irq != NULL) {
				rc = d->hw->wait_irq(d->hw->ctx, irq, DEC_WAIT_ISR_TIMEOUT_MS, &irq_status);
				d->st.wait_isr++;
				if (rc == 0 && d->hw->irq_clear != NULL)
					(void)d->hw->irq_clear(d->hw->ctx, irq);   /* the ISR acks on success */
				if (rc != 0) {
					d->st.wait_isr_timeouts++;
					if (rc != -ETIMEDOUT && rc != -EINTR)
						dec_hw_error(d, rc);
				}
			} else {
				rc = -ENODEV;
				dec_hw_error(d, rc);
			}
			(void)irq_status;
		}
		/* mtk_vcodec_wait_for_done_ctx returns exactly 0, -1 (timeout, :30) or -2
		 * (interrupted, :36); the daemon reads it back from status (vdec_vcu_if.c:255). */
		if (rc == 0)
			m.status = 0;
		else if (rc == -EINTR)
			m.status = -2;
		else
			m.status = -1;
		memcpy(buf, &m, sizeof m);   /* copied back to the daemon (mtk_vcu.c:1814-1821) */
		dec_trace(d, "wait_isr", status, (uint32_t)(m.status < 0 ? -m.status : 0), 0);
		return 1;
	}
	case VCU_IPIMSG_DEC_GET_FRAME_BUFFER:
		if (len < sizeof(struct vdec_vcu_ipi_ack))
			goto short_msg;
		(void)dec_get_frame_buffer(d);
		return 1;
	case VCU_IPIMSG_DEC_PUT_FRAME_BUFFER:
		if (len < sizeof(struct vdec_vcu_ipi_ack))
			goto short_msg;
		dec_harvest(d, 1);
		return 1;
	default:
		dec_proto(d);
		return 0;   /* the kernel logs "invalid msg" and acks */
	}

short_msg:
	dec_proto(d);
	if (dec_is_reply_for(expecting, msg_id))
		dec_note_reply(d, msg_id, -EPROTO);
	return 0;
}

static int dec_ipi_handler(void *ctx, int ipi_id, void *buf, uint32_t len)
{
	struct n2ap_vdec *d = ctx;
	int slot = dec_live_enter(d);
	int rc;

	if (slot < 0)
		return 1;   /* the session is gone: a late dispatch must not touch its memory */
	rc = dec_dispatch(d, ipi_id, buf, len);
	dec_live_leave(slot);
	return rc;
}

static int dec_drop_handler(void *ctx, int ipi_id, void *buf, uint32_t len)
{
	(void)ctx;
	(void)ipi_id;
	(void)buf;
	(void)len;
	return 1;
}

/* ---- send + wait (vcu_ipi_send, mtk_vcu.c:383-476) ------------------------------------------- */

static int dec_send(struct n2ap_vdec *d, unsigned ipi_id, const void *msg, uint32_t len,
                    uint32_t ap_msg_id)
{
	int rc;

	if (d->aborted)
		return -EIO;
	__atomic_store_n(&d->expecting, ap_msg_id, __ATOMIC_RELAXED);
	pthread_mutex_lock(&d->lock);
	d->reply_seen = 0;
	d->reply_msg_id = 0;
	d->reply_status = 0;
	pthread_mutex_unlock(&d->lock);

	rc = n2vcu_ap_send(d->vcu, (int)ipi_id, msg, len);
	if (rc != 0) {
		__atomic_store_n(&d->expecting, 0u, __ATOMIC_RELAXED);
		return rc;
	}
	/* first half: the ACK (the daemon's reply dispatched to the handler) */
	rc = dec_wait_reply(d);
	if (rc != 0) {
		__atomic_store_n(&d->expecting, 0u, __ATOMIC_RELAXED);
		d->aborted = 1;
		dec_trace(d, "timeout", ipi_id, ap_msg_id, d->timeout_ms);
		return rc;
	}
	/* second half: ipi_done (the daemon came back for its next message) */
	rc = n2vcu_ap_wait_dispatched(d->vcu, (int)ipi_id, d->timeout_ms);
	__atomic_store_n(&d->expecting, 0u, __ATOMIC_RELAXED);
	if (rc != 0) {
		d->aborted = 1;
		dec_trace(d, "timeout", ipi_id, ap_msg_id, d->timeout_ms);
		return rc;
	}
	{
		int herr = __atomic_exchange_n(&d->hw_error, 0, __ATOMIC_RELAXED);

		if (herr != 0) {
			dec_trace(d, "hw", ap_msg_id, (uint32_t)(-herr), 0);
			return herr;
		}
	}
	if (d->reply_status != VDEC_IPI_MSG_STATUS_OK) {
		/* VDEC_IPI_MSG_STATUS_FAIL (-1) maps to -EINVAL like vdec_vcu_if.c:443's vcu->failure;
		 * a handler-detected defect carries its own code. */
		return d->reply_status < 0 ? d->reply_status : -EINVAL;
	}
	return 0;
}

/* ---- the vsi --------------------------------------------------------------------------------- */

/* Map dec/pic/format fields for the ring entry count the daemon uses. */
static void dec_bind_vsi(struct n2ap_vdec *d)
{
	uint32_t off = vsi_dec_offset(d->ring_entries);

	d->t = (struct vdec_vsi_tail *)((uint8_t *)d->vsi + off);
}

/* ---- open / close ---------------------------------------------------------------------------- */

static unsigned dec_ipi_for_fourcc(uint32_t fourcc)
{
	switch (fourcc) {
	case 0:
	case N2AP_VDEC_FOURCC_H264:
		return N2AP_VDEC_IPI_H264;
	case N2AP_VDEC_FOURCC_H265:
		return N2AP_VDEC_IPI_H265;
	case N2AP_VDEC_FOURCC_HEIF:
		return N2AP_VDEC_IPI_HEIF;
	case N2AP_VDEC_FOURCC_VP8:      /* opus-fmt: vdec_common_if.c:107-137 */
		return N2AP_VDEC_IPI_VP8;
	case N2AP_VDEC_FOURCC_VP9:
		return N2AP_VDEC_IPI_VP9;
	case N2AP_VDEC_FOURCC_AV1:
		return N2AP_VDEC_IPI_AV1;
	case N2AP_VDEC_FOURCC_MPEG2:
		return N2AP_VDEC_IPI_MPEG12;
	case N2AP_VDEC_FOURCC_MPEG4:
		return N2AP_VDEC_IPI_MPEG4;
	case N2AP_VDEC_FOURCC_H263:
		return N2AP_VDEC_IPI_H263;
	default:
		return 0;
	}
}

static void dec_free(struct n2ap_vdec *d)
{
	/* No handler may still be running with this session: both close() and every open() error path
	 * come through here.  A no-op when the session never became live. */
	dec_live_remove_wait(d);
	if (d->sync_done) {
		pthread_cond_destroy(&d->reply_cv);
		pthread_cond_destroy(&d->fb_cv);
		pthread_mutex_destroy(&d->lock);
	}
	free(d);
}

static int dec_query_handler(void *ctx, int ipi_id, void *buf, uint32_t len)
{
	struct n2ap_vdec *d;
	int rc;

	(void)ctx;
	pthread_mutex_lock(&dec_query.lock);
	d = dec_query.owner;
	if (d == NULL)
		rc = 1;
	else
		rc = dec_ipi_handler(d, ipi_id, buf, len);
	pthread_mutex_unlock(&dec_query.lock);
	return rc;
}

int n2ap_vdec_open(struct n2ap_vdec **out, const struct n2ap_vdec_cfg *cfg)
{
	struct vdec_ap_ipi_init msg;
	struct n2ap_vdec *d;
	uint32_t base = 0, dlen = 0;
	unsigned ipi_id;
	int rc;

	if (out == NULL || cfg == NULL || cfg->vcu == NULL || cfg->hw == NULL)
		return -EINVAL;
	*out = NULL;
	ipi_id = dec_ipi_for_fourcc(cfg->codec_fourcc);
	if (ipi_id == 0u)
		return -EINVAL;
	d = calloc(1, sizeof(*d));
	if (d == NULL)
		return -ENOMEM;
	if (pthread_mutex_init(&d->lock, NULL) != 0) {
		dec_free(d);
		return -ENOMEM;
	}
	if (pthread_cond_init(&d->reply_cv, NULL) != 0) {
		pthread_mutex_destroy(&d->lock);
		dec_free(d);
		return -ENOMEM;
	}
	if (pthread_cond_init(&d->fb_cv, NULL) != 0) {
		pthread_cond_destroy(&d->reply_cv);
		pthread_mutex_destroy(&d->lock);
		dec_free(d);
		return -ENOMEM;
	}
	d->sync_done = 1;
	d->vcu = cfg->vcu;
	d->hw = cfg->hw;
	d->ipi_id = ipi_id;
	d->fourcc = cfg->codec_fourcc;
	d->timeout_ms = cfg->timeout_ms != 0u ? cfg->timeout_ms : DEC_DEFAULT_TIMEOUT_MS;
	d->no_power = cfg->no_power;
	d->fake_irq = cfg->fake_irq;
	d->cfg_fourcc = cfg->output_fourcc;
	d->ring_entries = cfg->ring_entries != 0u ? cfg->ring_entries : N2AP_DEC_MAX_FB_NUM;
	d->cookie = 0x4e440000u |
	            (uint64_t)(__atomic_add_fetch(&dec_cookie_seq, 1u, __ATOMIC_RELAXED) & 0xffffu);
	/* The vendor's MM21 output has two planes; the kernel sets this at open (SET_PARAM_FB_NUM_PLANES,
	 * mtk_vcodec_dec.c:1663), and the value must be in the vsi before the first START. */
	if (cfg->fb_num_planes != 0u)
		d->cfg_planes = cfg->fb_num_planes;
	else
		d->cfg_planes = cfg->codec_fourcc == N2AP_VDEC_FOURCC_HEIF ? 1u : 2u;

	rc = dec_live_add(d);
	if (rc != 0) {
		dec_free(d);
		return rc;
	}
	rc = n2vcu_ap_register(d->vcu, (int)ipi_id, dec_ipi_handler, d);
	if (rc != 0) {
		dec_free(d);
		return rc;
	}

	if (cfg->query_cap) {
		struct mtk_video_fmt fmts[MTK_MAX_DEC_CODECS_SUPPORT];

		rc = n2ap_vdec_query_cap(d, GET_PARAM_CAPABILITY_SUPPORTED_FORMATS, fmts, sizeof fmts);
		if (rc != 0) {
			(void)n2vcu_ap_register(d->vcu, (int)ipi_id, dec_drop_handler, NULL);
			dec_free(d);
			return rc;
		}
	}

	memset(&msg, 0, sizeof msg);
	msg.msg_id = AP_IPIMSG_DEC_INIT;
	msg.ap_inst_addr = d->cookie;
	dec_trace(d, "init", ipi_id, d->fourcc, 0);
	rc = dec_send(d, ipi_id, &msg, sizeof msg, AP_IPIMSG_DEC_INIT);
	if (rc != 0) {
		(void)n2vcu_ap_register(d->vcu, (int)ipi_id, dec_drop_handler, NULL);
		dec_free(d);
		return rc;
	}
	if (d->inst_addr == 0u || (d->inst_addr & 7u) != 0u) {
		(void)n2vcu_ap_register(d->vcu, (int)ipi_id, dec_drop_handler, NULL);
		dec_free(d);
		return -EIO;
	}
	/* The client addresses dec/pic/crop/... through the tail view, so the whole tail must be
	 * inside the DMEM window, not only up to `dec` (vdec_vsi is 13752 bytes at 64 entries; the
	 * daemon's instance area is larger still). */
	if (n2vcu_dmem(d->vcu, &base, &dlen) != 0 || d->inst_addr < base ||
	    (uint64_t)(d->inst_addr - base) + vsi_dec_offset(d->ring_entries) +
	    sizeof(struct vdec_vsi_tail) > dlen) {
		(void)n2vcu_ap_register(d->vcu, (int)ipi_id, dec_drop_handler, NULL);
		dec_free(d);
		return -EIO;
	}
	d->dmem_guest = base;
	d->dmem_bytes = dlen;
	d->vsi = (struct vdec_vsi *)n2vcu_map_dm_addr(d->vcu, d->inst_addr);
	if (d->vsi == NULL) {
		(void)n2vcu_ap_register(d->vcu, (int)ipi_id, dec_drop_handler, NULL);
		dec_free(d);
		return -EIO;
	}
	dec_bind_vsi(d);
	/* From here on the session is usable: the SET_PARAM helpers below reject a session that is not
	 * "opened", and they are part of the open sequence (vdec_init() sends FRAME_SIZE right after the
	 * INIT ack, mtk_vcodec_dec.c:2207). */
	d->opened = 1;
	d->t->dec.fb_num_planes = d->cfg_planes;
	/* vdec_init() always sends FRAME_SIZE (mtk_vcodec_dec.c:2200-2211, with 0/0 when the caller
	 * does not know it yet) before DECODE_MODE; the reference client does the same. */
	rc = n2ap_vdec_set_frame_size(d, cfg->pic_w, cfg->pic_h);
	if (rc != 0) {
		d->opened = 0;
		(void)n2vcu_ap_register(d->vcu, (int)ipi_id, dec_drop_handler, NULL);
		dec_free(d);
		return rc;
	}
	if (cfg->set_decode_mode) {
		rc = n2ap_vdec_set_decode_mode(d, cfg->decode_mode);
		if (rc != 0) {
			d->opened = 0;
			(void)n2vcu_ap_register(d->vcu, (int)ipi_id, dec_drop_handler, NULL);
			dec_free(d);
			return rc;
		}
	}
	*out = d;
	return 0;
}

int n2ap_vdec_close(struct n2ap_vdec *d)
{
	int rc = 0;

	if (d == NULL)
		return -EINVAL;
	if (d->opened && !d->aborted) {
		struct vdec_ap_ipi_cmd msg;

		memset(&msg, 0, sizeof msg);
		msg.msg_id = AP_IPIMSG_DEC_DEINIT;
		msg.vcu_inst_addr = d->inst_addr;
		rc = dec_send(d, d->ipi_id, &msg, sizeof msg, AP_IPIMSG_DEC_DEINIT);
	}
	/* Unbalanced LOCKs from the daemon: drop what we still hold, last-user path included.  The
	 * count is read under the mutex the handler updates it with, so a request racing this close
	 * cannot make the loop miss a reference. */
	for (;;) {
		int prc, active, core;

		pthread_mutex_lock(&d->lock);
		active = d->active;
		core = d->core_refs > 0;
		pthread_mutex_unlock(&d->lock);
		if (active <= 0)
			break;
		prc = dec_unlock(d, core);
		if (prc != 0 && rc == 0)
			rc = prc;
	}
	pthread_mutex_lock(&dec_query.lock);
	if (dec_query.owner == d)
		dec_query.owner = NULL;
	pthread_mutex_unlock(&dec_query.lock);
	(void)n2vcu_ap_register(d->vcu, (int)d->ipi_id, dec_drop_handler, NULL);
	d->opened = 0;
	d->vsi = NULL;
	d->t = NULL;
	dec_free(d);
	return rc;
}

unsigned n2ap_vdec_ipi_id(const struct n2ap_vdec *d)
{
	return d != NULL ? d->ipi_id : 0u;
}

struct vdec_vsi *n2ap_vdec_vsi(const struct n2ap_vdec *d)
{
	return d != NULL ? d->vsi : NULL;
}

uint32_t n2ap_vdec_vsi_addr(const struct n2ap_vdec *d)
{
	return d != NULL ? d->inst_addr : 0u;
}

int32_t n2ap_vdec_last_status(const struct n2ap_vdec *d)
{
	return d != NULL ? d->last_status : 0;
}

uint32_t n2ap_vdec_changed(const struct n2ap_vdec *d)
{
	return (d != NULL && d->t != NULL) ? d->t->dec.vdec_changed_info : 0u;
}

int n2ap_vdec_aborted(const struct n2ap_vdec *d)
{
	return d != NULL ? d->aborted : 0;
}

unsigned n2ap_vdec_proto_errors(const struct n2ap_vdec *d)
{
	return d != NULL ? __atomic_load_n(&d->proto_errors, __ATOMIC_RELAXED) : 0u;
}

void n2ap_vdec_stats(const struct n2ap_vdec *d, struct n2ap_vdec_stats *out)
{
	if (out == NULL)
		return;
	if (d == NULL) {
		memset(out, 0, sizeof *out);
		return;
	}
	*out = d->st;
}

void n2ap_vdec_set_trace(struct n2ap_vdec *d, n2ap_vdec_trace_fn fn, void *arg)
{
	if (d != NULL) {
		d->trace = fn;
		d->trace_arg = arg;
	}
}

int n2ap_vdec_set_ring_entries(struct n2ap_vdec *d, unsigned entries)
{
	uint32_t off;

	if (d == NULL || d->vsi == NULL || entries < 1u || entries > 256u)
		return -EINVAL;
	off = vsi_dec_offset(entries);
	/* The tail view must stay inside the DMEM window the daemon handed out. */
	if (d->inst_addr < d->dmem_guest ||
	    (uint64_t)(d->inst_addr - d->dmem_guest) + off + sizeof(struct vdec_vsi_tail) >
	    d->dmem_bytes)
		return -EINVAL;
	d->ring_entries = entries;
	dec_bind_vsi(d);
	/* The planes field lives in the tail: restore the session value there. */
	d->t->dec.fb_num_planes = d->cfg_planes;
	return 0;
}

const char *n2ap_vdec_strerror(int rc)
{
	switch (rc) {
	case 0: return "ok";
	case -EINVAL: return "-EINVAL";
	case -ENOMEM: return "-ENOMEM";
	case -ENODEV: return "-ENODEV";
	case -EIO: return "-EIO";
	case -EBUSY: return "-EBUSY";
	case -ENOSPC: return "-ENOSPC";
	case -ETIMEDOUT: return "-ETIMEDOUT";
	case -EPROTO: return "-EPROTO";
	case -ENOTSUP: return "-ENOTSUP";
	case -EAGAIN: return "-EAGAIN";
	case VDEC_IPI_MSG_STATUS_FAIL: return "VDEC_IPI_MSG_STATUS_FAIL";
	default: return "error";
	}
}

/* ---- QUERY_CAP (vdec_vcu_if.c:39-68, :594-625) ------------------------------------------------ */

int n2ap_vdec_query_cap(struct n2ap_vdec *d, unsigned id, void *out, uint32_t out_bytes)
{
	struct vdec_ap_ipi_query_cap msg;
	uint32_t size;
	int rc;

	if (d == NULL || out == NULL)
		return -EINVAL;
	switch (id) {
	case GET_PARAM_CAPABILITY_SUPPORTED_FORMATS:
		size = (uint32_t)sizeof(struct mtk_video_fmt);
		break;
	case GET_PARAM_CAPABILITY_FRAME_SIZES:
		size = (uint32_t)sizeof(struct mtk_codec_framesizes);
		break;
	default:
		return -EINVAL;
	}
	if (out_bytes < size * MTK_MAX_DEC_CODECS_SUPPORT)
		return -EINVAL;
	if (d->aborted)
		return -EIO;
	pthread_mutex_lock(&dec_query.lock);
	if (dec_query.owner != NULL) {
		pthread_mutex_unlock(&dec_query.lock);
		return -EBUSY;
	}
	if (!dec_query.registered) {
		rc = n2vcu_ap_register(d->vcu, N2AP_VDEC_IPI_COMMON, dec_query_handler, NULL);
		if (rc != 0) {
			pthread_mutex_unlock(&dec_query.lock);
			return rc;
		}
		dec_query.registered = 1;
	}
	memset(&msg, 0, sizeof msg);
	msg.msg_id = AP_IPIMSG_DEC_QUERY_CAP;
	msg.id = id;
	msg.ap_inst_addr = d->cookie;
	msg.ap_data_addr = (uint64_t)(uintptr_t)out;
	d->query_out = out;
	d->query_bytes = out_bytes;
	d->query_size = size;
	dec_query.owner = d;
	pthread_mutex_unlock(&dec_query.lock);
	dec_trace(d, "query_cap", id, 0, 0);
	rc = dec_send(d, N2AP_VDEC_IPI_COMMON, &msg, sizeof msg, AP_IPIMSG_DEC_QUERY_CAP);
	pthread_mutex_lock(&dec_query.lock);
	d->query_out = NULL;
	d->query_bytes = 0;
	d->query_size = 0;
	dec_query.owner = NULL;
	pthread_mutex_unlock(&dec_query.lock);
	return rc;
}

/* ---- SET_PARAM (vdec_vcu_if.c:627-651 + vdec_common_if.c:597-644) ---------------------------- */

int n2ap_vdec_set_param(struct n2ap_vdec *d, unsigned id, const uint32_t *data, unsigned count)
{
	struct vdec_ap_ipi_set_param msg;
	unsigned i;
	int rc;

	if (d == NULL || !d->opened || d->vsi == NULL || d->t == NULL)
		return -EINVAL;
	switch (id) {
	case SET_PARAM_FRAME_SIZE:
	case SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER:
		if (data == NULL || count < 2u)
			return -EINVAL;
		break;
	case SET_PARAM_DECODE_MODE:
	case SET_PARAM_NAL_SIZE_LENGTH:
	case SET_PARAM_WAIT_KEY_FRAME:
	case SET_PARAM_OPERATING_RATE:
	case SET_PARAM_TOTAL_FRAME_BUFQ_COUNT:
		if (data == NULL || count < 1u)
			return -EINVAL;
		break;
	case SET_PARAM_FB_NUM_PLANES:
		/* vsi only: vdec_common_if.c:635-639 */
		if (data == NULL)
			return -EINVAL;
		d->t->dec.fb_num_planes = data[0];
		dec_trace(d, "set_param", id, 0, data[0]);
		return 0;
	case SET_PARAM_UFO_MODE:
		/* the kernel accepts it and sends nothing (vdec_common_if.c:619-620) */
		return 0;
	case SET_PARAM_CRC_PATH:
	case SET_PARAM_GOLDEN_PATH:
	case SET_PARAM_DEC_LOG:
	default:
		return -EINVAL;
	}
	if (d->aborted)
		return -EIO;
	memset(&msg, 0, sizeof msg);
	msg.msg_id = AP_IPIMSG_DEC_SET_PARAM;
	msg.vcu_inst_addr = d->inst_addr;
	msg.id = id;
	for (i = 0; i < count && i < 8u; i++)
		msg.data[i] = data[i];
	dec_trace(d, "set_param", id, count, data != NULL ? data[0] : 0u);
	rc = dec_send(d, d->ipi_id, &msg, sizeof msg, AP_IPIMSG_DEC_SET_PARAM);
	return rc;
}

int n2ap_vdec_set_frame_size(struct n2ap_vdec *d, uint32_t w, uint32_t h)
{
	uint32_t data[2];

	data[0] = w;
	data[1] = h;
	return n2ap_vdec_set_param(d, SET_PARAM_FRAME_SIZE, data, 2);
}

int n2ap_vdec_set_decode_mode(struct n2ap_vdec *d, uint32_t mode)
{
	return n2ap_vdec_set_param(d, SET_PARAM_DECODE_MODE, &mode, 1);
}

/* ---- decode (vdec_common_if.c:217-310 + vdec_vcu_if.c:546-570) -------------------------------- */

static int dec_sync_to_device(struct n2ap_vdec *d, uint32_t handle, uint32_t bytes)
{
	uint32_t pa = 0, guest = 0, lbytes = 0;

	if (d->hw == NULL || handle == 0)
		return 0;
	if (n2vc_dma_lookup_handle(handle, &pa, &guest, &lbytes) != 0 || guest == 0)
		return 0;
	if (lbytes != 0 && (bytes == 0 || bytes > lbytes))
		bytes = lbytes;
	(void)n2vc_dma_sync(d->hw, NULL, guest, 0, bytes, 1);
	d->st.syncs++;
	return 0;
}

int n2ap_vdec_flush(struct n2ap_vdec *d)
{
	struct vdec_ap_ipi_cmd msg;
	int rc;

	if (d == NULL || !d->opened)
		return -EINVAL;
	if (d->aborted)
		return -EIO;
	pthread_mutex_lock(&d->lock);
	d->flushing = 1;
	pthread_cond_broadcast(&d->fb_cv);   /* wake a GET_FRAME_BUFFER wait (MTK_STATE_FLUSH) */
	pthread_mutex_unlock(&d->lock);
	memset(&msg, 0, sizeof msg);
	msg.msg_id = AP_IPIMSG_DEC_RESET;
	msg.vcu_inst_addr = d->inst_addr;
	dec_trace(d, "flush", 0, 0, 0);
	rc = dec_send(d, d->ipi_id, &msg, sizeof msg, AP_IPIMSG_DEC_RESET);
	pthread_mutex_lock(&d->lock);
	d->flushing = 0;
	pthread_mutex_unlock(&d->lock);
	/* Harvest whatever the reset released even when the ACK failed: the rings are the truth and
	 * the caller must not leak buffers because a reply was late. */
	dec_harvest(d, 0);
	return rc;
}

int n2ap_vdec_end(struct n2ap_vdec *d)
{
	struct vdec_ap_ipi_cmd msg;

	if (d == NULL || !d->opened)
		return -EINVAL;
	if (d->aborted)
		return -EIO;
	memset(&msg, 0, sizeof msg);
	msg.msg_id = AP_IPIMSG_DEC_END;
	msg.vcu_inst_addr = d->inst_addr;
	return dec_send(d, d->ipi_id, &msg, sizeof msg, AP_IPIMSG_DEC_END);
}

int n2ap_vdec_decode(struct n2ap_vdec *d, const struct n2ap_vdec_job *job)
{
	struct vdec_ap_ipi_dec_start msg;
	uint32_t bs_pa = 0, bs_guest = 0, bs_bytes = 0;
	uint32_t fb_pa = 0, fb_guest = 0, fb_bytes = 0;
	uint32_t c_pa = 0, c_guest = 0, c_bytes = 0;
	uint32_t planes, i, size, length;
	uint64_t fb_cookie;
	int rc, fb_added = 0;

	if (d == NULL || !d->opened || d->vsi == NULL || d->t == NULL)
		return -EINVAL;
	if (job == NULL || job->bs_handle == 0)
		return n2ap_vdec_flush(d);
	if (d->aborted)
		return -EIO;
	if (n2vc_dma_lookup_handle(job->bs_handle, &bs_pa, &bs_guest, &bs_bytes) != 0)
		return -EINVAL;
	if (bs_pa == 0u || bs_bytes == 0u)
		return -EINVAL;
	/* Kernel names are reversed relative to their meaning (mtk_vcodec_dec.c:670-671):
	 * data[0] = bs->size   = planes[0].bytesused = the access-unit length,
	 * data[1] = bs->length = planes[0].length    = the mapped buffer size.
	 * Neither may exceed the buffer the handle names. */
	size = job->bs_bytes != 0u ? job->bs_bytes : bs_bytes;
	length = job->bs_length != 0u ? job->bs_length : size;
	if (size > bs_bytes || length > size)
		return -EINVAL;

	/* fb_planes == 0 means the session default: 2 for H.264/H.265 (the vendor reports MM21 with
	 * one size per plane), 1 for HEIF.  Never downgrade silently when a chroma plane is needed. */
	planes = job->fb_handle != 0u ?
	         (job->fb_planes != 0u ? job->fb_planes : d->cfg_planes) : 0u;
	if (planes > MTK_VIDEO_MAX_PLANES)
		return -EINVAL;
	if (job->fb_handle != 0) {
		if (n2vc_dma_lookup_handle(job->fb_handle, &fb_pa, &fb_guest, &fb_bytes) != 0)
			return -EINVAL;
		if (fb_pa == 0u)
			return -EINVAL;
		if (planes >= 2u) {
			if (job->fb_c_handle == job->fb_handle) {
				/* Both planes in one allocation: chroma starts after the Y plane. */
				if (d->t->pic.fb_sz[0] == 0u)
					return -EINVAL;
				if (d->t->pic.fb_sz[1] != 0u &&
				    (uint64_t)d->t->pic.fb_sz[0] + d->t->pic.fb_sz[1] > fb_bytes)
					return -EINVAL;
				c_pa = fb_pa + d->t->pic.fb_sz[0];
				c_bytes = fb_bytes - d->t->pic.fb_sz[0];
			} else if (job->fb_c_handle != 0u) {
				if (n2vc_dma_lookup_handle(job->fb_c_handle, &c_pa, &c_guest,
				                           &c_bytes) != 0 || c_pa == 0u)
					return -EINVAL;
				if (d->t->pic.fb_sz[0] != 0u && fb_bytes < d->t->pic.fb_sz[0])
					return -EINVAL;
				if (d->t->pic.fb_sz[1] != 0u && c_bytes < d->t->pic.fb_sz[1])
					return -EINVAL;
			} else if (d->t->pic.fb_sz[1] != 0u) {
				/* The vendor reports a chroma plane: a zero target would be a DMA write
				 * to physical address 0. */
				return -EINVAL;
			} else {
				planes = 1u;   /* nothing needs a chroma plane for this format */
			}
		}
	}

	/* vdec_common_if.c:243-290, in the same order. */
	d->t->dec.vdec_bs_va = job->bs_cookie != 0u ? job->bs_cookie : (uint64_t)job->bs_handle;
	d->t->dec.bs_dma = bs_pa;
	d->t->dec.bs_fd = (uint64_t)(uint32_t)job->bs_fd;
	d->t->dec.vdec_fb_va = 0;
	for (i = 0; i < MTK_VIDEO_MAX_PLANES; i++) {
		d->t->dec.fb_dma[i] = 0;
		d->t->dec.fb_fd[i] = 0;
	}
	fb_cookie = job->fb_cookie != 0u ? job->fb_cookie : (uint64_t)job->fb_handle;
	if (job->fb_handle != 0u) {
		d->t->dec.vdec_fb_va = fb_cookie;
		d->t->dec.index = job->fb_index;
		d->t->dec.fb_num_planes = planes;
		d->t->dec.fb_dma[0] = fb_pa;
		d->t->dec.fb_fd[0] = (uint64_t)(uint32_t)job->fb_fd;
		if (planes >= 2u) {
			d->t->dec.fb_dma[1] = c_pa;
			d->t->dec.fb_fd[1] = (uint64_t)(uint32_t)(job->fb_c_handle == job->fb_handle ?
			                                          job->fb_fd : job->fb_c_fd);
		}
	} else {
		d->t->dec.index = 0xFF;
	}
	/* vdec_common_if.c:246-248: no general buffer unless the caller has one; 0 imports fd 0. */
	d->t->general_buf_fd = -1;
	d->t->general_buf_dma = 0;
	d->t->general_buf_size = 0;
	d->t->dec.queued_frame_buf_count = 0;
	d->t->dec.timestamp = job->timestamp;

	/* Register the output buffer BEFORE the trigger: the daemon can send PUT_FRAME_BUFFER from
	 * inside the START, and its harvest must find this cookie to sync the planes. */
	if (job->fb_handle != 0u) {
		struct n2ap_vdec_fb jb;

		pthread_mutex_lock(&d->lock);
		if (dec_fb_by_cookie(d, fb_cookie) == NULL) {
			if (d->n_in_use >= N2AP_VDEC_FB_QUEUE) {
				pthread_mutex_unlock(&d->lock);
				return -ENOSPC;
			}
			memset(&jb, 0, sizeof jb);
			jb.handle = job->fb_handle;
			/* == handle marks both planes inside one allocation */
			jb.c_handle = planes >= 2u ? (job->fb_c_handle != 0u ? job->fb_c_handle :
			                              job->fb_handle) : 0u;
			jb.fd = job->fb_fd;
			jb.c_fd = job->fb_c_fd;
			jb.bytes = job->fb_bytes != 0u ? job->fb_bytes : fb_bytes;
			jb.index = job->fb_index;
			jb.cookie = fb_cookie;
			d->in_use[d->n_in_use++] = jb;
			fb_added = 1;
		}
		pthread_mutex_unlock(&d->lock);
	}

	/* frozen n2vc_hw.h: "to_device before the trigger"; output planes are synced from_device
	 * when they appear in list_disp. */
	dec_sync_to_device(d, job->bs_handle, bs_bytes);

	memset(&msg, 0, sizeof msg);
	msg.msg_id = AP_IPIMSG_DEC_START;
	msg.vcu_inst_addr = d->inst_addr;
	msg.data[0] = length;   /* bs->size   = bytesused = AU bytes (opus-hevcdec's finding) */
	msg.data[1] = size;     /* bs->length = the mapped buffer size */
	msg.data[2] = job->bs_flags;
	dec_trace(d, "decode", msg.data[0], msg.data[1], job->fb_handle);
	rc = dec_send(d, d->ipi_id, &msg, sizeof msg, AP_IPIMSG_DEC_START);
	if (rc != 0) {
		if (fb_added) {
			struct n2ap_vdec_fb *fb;

			pthread_mutex_lock(&d->lock);
			fb = dec_fb_by_cookie(d, fb_cookie);
			if (fb != NULL)
				dec_fb_drop_in_use(d, fb);
			pthread_mutex_unlock(&d->lock);
		}
		if (!d->aborted)
			dec_harvest(d, 0);
		return rc;
	}

	/* The output format the daemon reported is the contract DEC05/DEC06 detile against.  With
	 * vendor.mtk.vdec.frame.layout.mode unset the vendor keeps UFO: MT21 >= 1080p, M21S below;
	 * neither is MM21.  Refusing here beats handing the plug-in a layout it would corrupt. */
	if (d->cfg_fourcc != 0u && (d->t->dec.vdec_changed_info & VDEC_RES_CHANGE) != 0u &&
	    d->t->pic.fourcc != d->cfg_fourcc) {
		dec_trace(d, "fourcc", d->t->pic.fourcc, d->cfg_fourcc, 0);
		dec_harvest(d, 0);
		return -ENOTSUP;
	}

	/* Sync mode has no PUT_FRAME_BUFFER: harvest now, like the kernel's worker does after
	 * vdec_if_decode() returns. */
	dec_harvest(d, 0);
	dec_trace(d, "decode_done", d->t->dec.vdec_changed_info, 0, 0);
	return 0;
}

/* ---- output buffers -------------------------------------------------------------------------- */

int n2ap_vdec_push_fb(struct n2ap_vdec *d, const struct n2ap_vdec_fb *fb)
{
	uint64_t cookie;
	int rc = 0;

	if (d == NULL || fb == NULL || fb->handle == 0u)
		return -EINVAL;
	cookie = fb->cookie != 0u ? fb->cookie : (uint64_t)fb->handle;
	pthread_mutex_lock(&d->lock);
	if (d->nfree >= N2AP_VDEC_FB_QUEUE) {
		rc = -ENOSPC;
	} else if (dec_fb_by_cookie(d, cookie) != NULL) {
		/* Still in flight: the daemon has not returned it from both rings yet, or the client
		 * has not consumed its displayed picture.  Reusing it now would alias a live frame. */
		rc = -EBUSY;
	} else {
		d->free_q[d->nfree] = *fb;
		d->free_q[d->nfree].cookie = cookie;
		d->free_q[d->nfree].in_disp = 0;
		d->free_q[d->nfree].in_free = 0;
		d->nfree++;
		pthread_cond_broadcast(&d->fb_cv);
	}
	pthread_mutex_unlock(&d->lock);
	return rc;
}

unsigned n2ap_vdec_free_fb_count(const struct n2ap_vdec *d)
{
	unsigned n;

	if (d == NULL)
		return 0u;
	pthread_mutex_lock((pthread_mutex_t *)&d->lock);
	n = d->nfree;
	pthread_mutex_unlock((pthread_mutex_t *)&d->lock);
	return n;
}

int n2ap_vdec_pop_displayed(struct n2ap_vdec *d, struct n2ap_vdec_out *o)
{
	int rc = 0;

	if (d == NULL || o == NULL)
		return -EINVAL;
	pthread_mutex_lock(&d->lock);
	if (d->nout == 0) {
		rc = 0;
	} else {
		*o = d->out_q[0];
		memmove(&d->out_q[0], &d->out_q[1], (d->nout - 1) * sizeof d->out_q[0]);
		d->nout--;
		rc = 1;
	}
	pthread_mutex_unlock(&d->lock);
	return rc;
}

int n2ap_vdec_pop_freed(struct n2ap_vdec *d, struct n2ap_vdec_freed *f)
{
	int rc = 0;

	if (d == NULL || f == NULL)
		return -EINVAL;
	pthread_mutex_lock(&d->lock);
	if (d->nfreed == 0) {
		rc = 0;
	} else {
		*f = d->freed_q[0];
		memmove(&d->freed_q[0], &d->freed_q[1], (d->nfreed - 1) * sizeof d->freed_q[0]);
		d->nfreed--;
		rc = 1;
	}
	pthread_mutex_unlock(&d->lock);
	return rc;
}

int n2ap_vdec_pop_free_bs(struct n2ap_vdec *d, uint64_t *bs_cookie)
{
	int rc = 0;

	if (d == NULL || bs_cookie == NULL)
		return -EINVAL;
	pthread_mutex_lock(&d->lock);
	if (d->nbs_freed != 0u) {
		*bs_cookie = d->bs_free_q[0];
		memmove(&d->bs_free_q[0], &d->bs_free_q[1],
		        (d->nbs_freed - 1) * sizeof d->bs_free_q[0]);
		d->nbs_freed--;
		rc = 1;
	}
	pthread_mutex_unlock(&d->lock);
	return rc;
}

int n2ap_vdec_fb_of(const struct n2ap_vdec *d, uint64_t cookie, struct n2ap_vdec_fb *fb)
{
	unsigned i;
	int rc = 0;

	if (d == NULL || fb == NULL)
		return -EINVAL;
	pthread_mutex_lock((pthread_mutex_t *)&d->lock);
	for (i = 0; i < d->n_in_use; i++) {
		if (d->in_use[i].cookie == cookie) {
			*fb = d->in_use[i];
			rc = 1;
			break;
		}
	}
	pthread_mutex_unlock((pthread_mutex_t *)&d->lock);
	return rc;
}

int n2ap_vdec_get_pic_info(const struct n2ap_vdec *d, struct n2ap_vdec_pic *o)
{
	if (d == NULL || o == NULL || d->vsi == NULL || d->t == NULL)
		return -EINVAL;
	memset(o, 0, sizeof *o);
	if (d->t->pic.pic_w == 0u && d->t->pic.pic_h == 0u &&
	    (d->t->dec.vdec_changed_info & VDEC_RES_CHANGE) == 0u)
		return -EAGAIN;
	o->disp_w = d->t->crop.width;
	o->disp_h = d->t->crop.height;
	o->pic_w = d->t->pic.pic_w;
	o->pic_h = d->t->pic.pic_h;
	o->buf_w = d->t->pic.buf_w;
	o->buf_h = d->t->pic.buf_h;
	o->fb_sz[0] = d->t->pic.fb_sz[0];
	o->fb_sz[1] = d->t->pic.fb_sz[1];
	o->fourcc = d->t->pic.fourcc;
	o->bitdepth = d->t->pic.bitdepth;
	o->layout_mode = d->t->pic.layout_mode;
	o->dpb_sz = d->t->dec.dpb_sz;
	o->changed = d->t->dec.vdec_changed_info;
	return 0;
}
