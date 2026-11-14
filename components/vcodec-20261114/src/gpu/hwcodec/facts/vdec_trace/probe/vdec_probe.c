/* SPDX-License-Identifier: MIT */
/*
 * vdec_probe.c -- DEC02: the AP side of one MT6893 H.264 decoder session, driven against the
 * phone's own vpud under the AArch32 interpreter and T02's fake hardware, with n2tr recording
 * every register operation.
 *
 * This is the probe of gpu/hwcodec/facts/VDEC_PROTOCOL.md section 9 turned into a package tool:
 * it plays the role of drivers/media/platform/mtk-vcodec/vdec_vcu_if.c + vdec_common_if.c for
 * one instance (INIT, SET_PARAM x2, QUERY_CAP x2, one or two STARTs, DEINIT) and answers the
 * VCU->AP requests the way the kernel does (LOCK/UNLOCK power references, WAITISR, GET/PUT frame
 * buffer).  It is not a product client: hw-dec01's ap/n2ap_vdec.c is; this file exists to record
 * what the vendor touches and how much work a START costs, and run.sh re-runs it twice to prove
 * the page list is deterministic.
 *
 * Usage (run.sh does all of this):
 *   vdec_probe --vendor DIR --stream FILE --width W --height H [--frames N]
 *              [--trace FILE] [--record FILE] [--json FILE] [--tag NAME]
 *
 * Exit status: 0 when the session reached at least the first START exchange (whatever its
 * status), 1 when the stack did not come up or the guest stopped first.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2vc_hw.h"
#include "n2vhost.h"
#include "vcu/n2vcu.h"

#include "mtk_vdec_ipi.h"
#include "n2tr.h"

#if defined(__APPLE__)
#define N2_WEAK __attribute__((weak_import))
#else
#define N2_WEAK __attribute__((weak))
#endif

/* V34's AP-side share publication (shim/n2shim_android.c).  Weak: the probe still starts
 * without the shim and reports that the vendor's ion_import cannot resolve the buffers. */
extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa, uint32_t bytes) N2_WEAK;

#define IPI_VDEC_COMMON 1
#define IPI_VDEC_H264 2
#define AP_COOKIE 0x1234d0c2u
#define SHARE_BS 0x600
#define SHARE_FB0 0x601
#define SHARE_FB1 0x602
#define BS_COOKIE 0xB0000000u    /* vsi.dec.vdec_bs_va, the vendor bs ring key */
#define FB_COOKIE 0xF0000000u    /* vsi.dec.vdec_fb_va, returned in list_disp/list_free */
#define AP_TIMEOUT_MS 5000u
#define WAIT_IRQ_TIMEOUT_MS 500u

/* ------------------------------------------------------------------ AP state */

struct start_rec {
	const char *kind;             /* "header" or "frame" */
	uint32_t bs_bytes, bs_length;  /* the START message data[0]/data[1] */
	int32_t status;
	uint32_t changed_info;
	uint32_t pic_w, pic_h, buf_w, buf_h, dpb_sz;
	uint64_t gce_flushes, gce_cmds, mmio_reads, mmio_writes, mmio_bad;
	uint64_t last_bad_mmio_addr;
	uint64_t tr_reads, tr_writes, tr_refused, tr_events;
	uint64_t icount_before, icount_after;
	uint64_t cpu_us;
	unsigned requests[8];
};

struct ap {
	struct n2vcu *vcu;
	const struct n2vc_hw *hw;
	struct n2tr *tr;
	uint32_t inst_addr;
	uint8_t *vsi;
	int vsi_ok;

	pthread_mutex_t mu;
	pthread_cond_t cv;
	uint32_t expecting;           /* ack msg id the AP thread waits for, 0 = none */
	int reply_seen;
	uint32_t reply_id;
	int32_t reply_status;

	/* VCU->AP requests seen */
	unsigned n_lock_lat, n_unlock_lat, n_lock_core, n_unlock_core;
	unsigned n_waitisr, n_getfb, n_putfb, n_other;
	int lat_power, core_power;
	uint32_t last_waitisr_status;

	/* query results */
	uint8_t qfmt[MTK_MAX_DEC_CODECS_SUPPORT * 12];
	uint8_t qfs[MTK_MAX_DEC_CODECS_SUPPORT * 36];
	int have_fmt, have_fs;
	uint32_t n_fmt, n_fs;
};


static void put32(void *p, size_t off, uint32_t v) { memcpy((uint8_t *)p + off, &v, 4); }
static uint32_t get32(const void *p, size_t off)
{
	uint32_t v;
	memcpy(&v, (const uint8_t *)p + off, 4);
	return v;
}
static void put64(void *p, size_t off, uint64_t v) { memcpy((uint8_t *)p + off, &v, 8); }
static uint64_t get64(const void *p, size_t off)
{
	uint64_t v;
	memcpy(&v, (const uint8_t *)p + off, 8);
	return v;
}

/* ------------------------------------------------------------------ the AP IPI handler */

static void ap_note_reply(struct ap *a, uint32_t id, int32_t status)
{
	pthread_mutex_lock(&a->mu);
	a->reply_seen = 1;
	a->reply_id = id;
	a->reply_status = status;
	pthread_cond_broadcast(&a->cv);
	pthread_mutex_unlock(&a->mu);
}

static int ap_handler(void *ctx, int ipi_id, void *buf, uint32_t len)
{
	struct ap *a = ctx;
	uint32_t id;

	if (a == NULL || buf == NULL || len < 4)
		return 0;
	id = get32(buf, 0);
	if (getenv("N2DEC02_DEBUG"))
		fprintf(stderr, "N2DEC02 handler ipi=%d msg=0x%x len=%u\n", ipi_id, id, len);
	(void)ipi_id;

	switch (id) {
	case VCU_IPIMSG_DEC_INIT_ACK:
		if (len < 24)
			return 0;
		if (get32(buf, 4) == 0 && get64(buf, 8) == AP_COOKIE) {
			a->inst_addr = get32(buf, 16);
			a->vsi = n2vcu_map_dm_addr(a->vcu, a->inst_addr);
			a->vsi_ok = a->vsi != NULL;
		}
		ap_note_reply(a, id, (int32_t)get32(buf, 4));
		return 0;

	case VCU_IPIMSG_DEC_QUERY_CAP_ACK: {
		uint32_t qid = get32(buf, 16);
		int32_t st = (int32_t)get32(buf, 4);
		uint32_t vcu_addr = len >= 40 ? get32(buf, 32) : 0;
		void *src = vcu_addr ? n2vcu_map_dm_addr(a->vcu, vcu_addr) : NULL;

		if (st == 0 && src != NULL && qid == GET_PARAM_CAPABILITY_SUPPORTED_FORMATS) {
			memcpy(a->qfmt, src, sizeof(a->qfmt));
			a->have_fmt = 1;
			a->n_fmt = sizeof(a->qfmt) / 12u;
		} else if (st == 0 && src != NULL && qid == GET_PARAM_CAPABILITY_FRAME_SIZES) {
			memcpy(a->qfs, src, sizeof(a->qfs));
			a->have_fs = 1;
			a->n_fs = sizeof(a->qfs) / 36u;
		}
		ap_note_reply(a, id, st);
		return 0;
	}

	case VCU_IPIMSG_DEC_START_ACK:
	case VCU_IPIMSG_DEC_END_ACK:
	case VCU_IPIMSG_DEC_DEINIT_ACK:
	case VCU_IPIMSG_DEC_RESET_ACK:
	case VCU_IPIMSG_DEC_SET_PARAM_ACK:
		ap_note_reply(a, id, (int32_t)get32(buf, 4));
		return 0;

	case VCU_IPIMSG_DEC_WAITISR: {
		uint32_t core = get32(buf, 4);   /* the kernel passes the core id in status */
		unsigned irq = core == 1 ? RWVC_IRQ_VDEC_LAT : RWVC_IRQ_VDEC;
		uint32_t status = 0;
		int rc = a->hw->wait_irq ? a->hw->wait_irq(a->hw->ctx, irq, WAIT_IRQ_TIMEOUT_MS, &status) : -ENOSYS;

		a->n_waitisr++;
		a->last_waitisr_status = status;
		put32(buf, 4, rc == 0 ? 0u : (uint32_t)rc);
		return 1;   /* the kernel returns 1: no ACK for a request */
	}

	case VCU_IPIMSG_DEC_LOCK_LAT:
		a->n_lock_lat++;
		if (!a->lat_power) {
			if (a->hw->power)
				(void)a->hw->power(a->hw->ctx, RWVC_BLK_VDEC_SOC, 1);
			a->lat_power = 1;
		}
		return 1;

	case VCU_IPIMSG_DEC_UNLOCK_LAT:
		a->n_unlock_lat++;
		if (a->lat_power) {
			if (a->hw->power)
				(void)a->hw->power(a->hw->ctx, RWVC_BLK_VDEC_SOC, 0);
			a->lat_power = 0;
		}
		return 1;

	case VCU_IPIMSG_DEC_LOCK_CORE:
		a->n_lock_core++;
		if (!a->core_power) {
			if (a->hw->power)
				(void)a->hw->power(a->hw->ctx, RWVC_BLK_VDEC_CORE, 1);
			a->core_power = 1;
		}
		return 1;

	case VCU_IPIMSG_DEC_UNLOCK_CORE:
		a->n_unlock_core++;
		if (a->core_power) {
			if (a->hw->power)
				(void)a->hw->power(a->hw->ctx, RWVC_BLK_VDEC_CORE, 0);
			a->core_power = 0;
		}
		return 1;

	case VCU_IPIMSG_DEC_GET_FRAME_BUFFER:
		/* The kernel fills vsi->dec with one free buffer; ours is filled by the caller
		 * before the START (the whole point of the probe is one known frame buffer). */
		a->n_getfb++;
		return 1;

	case VCU_IPIMSG_DEC_PUT_FRAME_BUFFER:
		a->n_putfb++;
		return 1;

	default:
		a->n_other++;
		return 1;
	}
}

/* ------------------------------------------------------------------ send + wait */

static uint64_t now_us(clockid_t id)
{
	struct timespec ts;

	if (clock_gettime(id, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static int ap_send(struct ap *a, unsigned ipi, const void *msg, uint32_t len, uint32_t ap_msg_id,
                   uint32_t expect_ack)
{
	struct timespec deadline;
	int rc;

	pthread_mutex_lock(&a->mu);
	a->expecting = expect_ack;
	a->reply_seen = 0;
	a->reply_id = 0;
	a->reply_status = 0;
	pthread_mutex_unlock(&a->mu);

	rc = n2vcu_ap_send(a->vcu, (int)ipi, msg, len);
	if (rc != 0) {
		pthread_mutex_lock(&a->mu);
		a->expecting = 0;
		pthread_mutex_unlock(&a->mu);
		fprintf(stderr, "N2DEC02 ap_send ipi=%u msg=0x%x failed: %d\n", ipi, ap_msg_id, rc);
		return rc;
	}
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += AP_TIMEOUT_MS / 1000u;
	deadline.tv_nsec += (long)(AP_TIMEOUT_MS % 1000u) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock(&a->mu);
	while (!a->reply_seen) {
		rc = pthread_cond_timedwait(&a->cv, &a->mu, &deadline);
		if (rc == ETIMEDOUT)
			break;
	}
	rc = a->reply_seen ? 0 : -ETIMEDOUT;
	pthread_mutex_unlock(&a->mu);
	a->expecting = 0;
	if (rc != 0) {
		struct n2vcu_stats st;

		n2vcu_stats(a->vcu, &st);
		fprintf(stderr, "N2DEC02 timeout waiting for ack 0x%x (sent 0x%x): pending=%d "
		        "set_object=%llu get_object=%llu acks=%llu\n", expect_ack, ap_msg_id,
		        n2vcu_ap_pending(a->vcu, (int)ipi),
		        (unsigned long long)st.set_object, (unsigned long long)st.get_object,
		        (unsigned long long)st.ipi_acks);
		return rc;
	}
	/* The kernel also waits for the daemon to come back for its next message. */
	rc = n2vcu_ap_wait_dispatched(a->vcu, (int)ipi, AP_TIMEOUT_MS);
	return rc;
}

/* The daemon registers its IPI handlers on the service threads it creates while starting; a message
 * that arrives before that is dropped by the vendor ("Get ipi_id ... obj handler is NULL").  IV's
 * runner waits 1 s (host/n2vhost.c ap_settle); the probe waits the same, overridable. */
static void ap_settle(void)
{
	const char *s = getenv("N2DEC02_SETTLE_MS");
	unsigned ms = s && *s ? (unsigned)strtoul(s, NULL, 10) : 1000u;
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

/* The vendor's own log lines (vpud logs through n2shim_log, host/n2vhost.c:1336).  The probe
 * prints them so a run shows *why* the daemon refused a message; run.sh keeps them out of the
 * trace/JSON that PAGES.md is built from. */
extern void (*n2shim_log)(int prio, const char *tag, const char *text);

static void probe_guest_log(int prio, const char *tag, const char *text)
{
	printf("N2DEC02 G %d/%s: %s\n", prio, tag ? tag : "?", text ? text : "");
	fflush(stdout);
}

/* ------------------------------------------------------------------ vsi helpers */

static struct vdec_dec_info vsi_get_dec(struct ap *a)
{
	struct vdec_dec_info d;

	memset(&d, 0, sizeof(d));
	if (a->vsi_ok)
		memcpy(&d, a->vsi + offsetof(struct vdec_vsi_prefix, dec), sizeof(d));
	return d;
}

static void vsi_put_dec(struct ap *a, const struct vdec_dec_info *d)
{
	if (a->vsi_ok)
		memcpy(a->vsi + offsetof(struct vdec_vsi_prefix, dec), d, sizeof(*d));
}

static struct vdec_pic_info vsi_get_pic(struct ap *a)
{
	struct vdec_pic_info p;

	memset(&p, 0, sizeof(p));
	if (a->vsi_ok)
		memcpy(&p, a->vsi + offsetof(struct vdec_vsi_prefix, pic), sizeof(p));
	return p;
}

/* Experimental instance seeding (N2DEC02_SEED="1a34=10,1ac8=10000,1acc=600"): write words into
 * the vendor's instance area after INIT.  This is how the probe identifies which private field the
 * vendor expects the AP to set; it is a diagnostic, not part of the protocol. */
static void seed_instance(struct ap *a)
{
	const char *s = getenv("N2DEC02_SEED");
	char buf[256];
	char *p, *save = NULL;

	if (s == NULL || !a->vsi_ok || strlen(s) >= sizeof buf)
		return;
	strcpy(buf, s);
	for (p = strtok_r(buf, ",", &save); p != NULL; p = strtok_r(NULL, ",", &save)) {
		char *eq = strchr(p, '=');
		unsigned long off, val;

		if (eq == NULL)
			continue;
		*eq = '\0';
		off = strtoul(p, NULL, 16);
		val = strtoul(eq + 1, NULL, 16);
		if (off + 4 <= 0x1c000) {
			put32(a->vsi, off, (uint32_t)val);
			printf("N2DEC02 seed inst+0x%05lx = 0x%08lx\n", off, val);
		}
	}
}

/* ------------------------------------------------------------------ counters */

struct stats_snap {
	struct n2vcu_stats vcu;
	struct n2tr_stats tr;
	uint64_t tr_events;
};

static void snap(struct ap *a, struct stats_snap *s)
{
	memset(s, 0, sizeof(*s));
	n2vcu_stats(a->vcu, &s->vcu);
	n2tr_stats(a->tr, &s->tr);
	s->tr_events = n2tr_events(a->tr);
}

struct icount {
	uint64_t value;
	int valid;
};

/* n2vhost_report() prints the live guest icount (its "N2VHOST stop ... icount=" line is not
 * printed while the guest runs, but the report's milestone lines are harmless); capture it to
 * a memory stream and parse it.  This avoids an API addition to host/n2vhost.h (asked to IV). */
static struct icount guest_icount(struct n2vhost *h)
{
	struct icount r = { 0, 0 };
	char *buf = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&buf, &len);

	if (f == NULL)
		return r;
	(void)n2vhost_report(h, f);
	fclose(f);
	if (buf != NULL) {
		const char *p = strstr(buf, "icount=");

		if (p != NULL) {
			r.value = strtoull(p + 7, NULL, 10);
			r.valid = 1;
		}
		free(buf);
	}
	return r;
}

/* ------------------------------------------------------------------ session */

static int do_start(struct ap *a, struct n2vhost *host, const char *kind,
                    const struct vdec_dec_info *d, struct start_rec *r)
{
	struct stats_snap s0, s1;
	struct icount ic0, ic1;
	struct vdec_dec_info got;
	struct vdec_pic_info p;
	uint32_t msg[20];
	uint64_t cpu0;
	int rc;

	vsi_put_dec(a, d);
	n2tr_mark(a->tr, kind);
	snap(a, &s0);
	ic0 = guest_icount(host);
	cpu0 = now_us(CLOCK_PROCESS_CPUTIME_ID);

	/* data[0] = bs->size, data[1] = bs->length, data[2] = bs->flags (vdec_common_if.c:267). */
	memset(msg, 0, sizeof(msg));
	put32(msg, 0, AP_IPIMSG_DEC_START);
	put32(msg, 4, a->inst_addr);
	put32(msg, 8, r->bs_bytes);
	put32(msg, 12, r->bs_length);
	put32(msg, 16, 0);
	rc = ap_send(a, IPI_VDEC_H264, msg, 24, AP_IPIMSG_DEC_START, VCU_IPIMSG_DEC_START_ACK);

	snap(a, &s1);
	ic1 = guest_icount(host);
	memset(r, 0, sizeof(*r));
	r->kind = kind;
	r->status = (int32_t)a->reply_status;
	got = vsi_get_dec(a);
	r->changed_info = got.vdec_changed_info;
	p = vsi_get_pic(a);
	r->pic_w = p.pic_w;
	r->pic_h = p.pic_h;
	r->buf_w = p.buf_w;
	r->buf_h = p.buf_h;
	r->dpb_sz = got.dpb_sz;
	r->gce_flushes = s1.vcu.gce_flushes - s0.vcu.gce_flushes;
	r->gce_cmds = s1.vcu.gce_cmds - s0.vcu.gce_cmds;
	r->mmio_reads = s1.vcu.mmio_reads - s0.vcu.mmio_reads;
	r->mmio_writes = s1.vcu.mmio_writes - s0.vcu.mmio_writes;
	r->mmio_bad = s1.vcu.mmio_bad - s0.vcu.mmio_bad;
	r->last_bad_mmio_addr = s1.vcu.last_bad_mmio_addr;
	r->tr_reads = s1.tr.reg_reads - s0.tr.reg_reads;
	r->tr_writes = s1.tr.reg_writes + s1.tr.reg_writes_mask - s0.tr.reg_writes - s0.tr.reg_writes_mask;
	r->tr_refused = s1.tr.refused - s0.tr.refused;
	r->tr_events = s1.tr_events - s0.tr_events;
	r->icount_before = ic0.value;
	r->icount_after = ic1.value;
	r->cpu_us = now_us(CLOCK_PROCESS_CPUTIME_ID) - cpu0;
	r->requests[0] = a->n_lock_lat;
	r->requests[1] = a->n_unlock_lat;
	r->requests[2] = a->n_lock_core;
	r->requests[3] = a->n_unlock_core;
	r->requests[4] = a->n_waitisr;
	r->requests[5] = a->n_getfb;
	r->requests[6] = a->n_putfb;
	r->requests[7] = a->n_other;

	printf("N2DEC02 start %s rc=%d status=%d changed=0x%x pic=%ux%u buf=%ux%u dpb=%u "
	       "gce=%" PRIu64 "/%" PRIu64 " mmio=%" PRIu64 "r/%" PRIu64 "w/%" PRIu64 "bad last=0x%08x "
	       "ev=%" PRIu64 " reads=%" PRIu64 " writes=%" PRIu64 " refused=%" PRIu64
	       " icount=%" PRIu64 "%s cpu=%" PRIu64 "us\n",
	       kind, rc, (int)r->status, r->changed_info, r->pic_w, r->pic_h, r->buf_w, r->buf_h,
	       r->dpb_sz, r->gce_flushes, r->gce_cmds, r->mmio_reads, r->mmio_writes, r->mmio_bad,
	       (unsigned)r->last_bad_mmio_addr, r->tr_events, r->tr_reads, r->tr_writes, r->tr_refused,
	       r->icount_after - r->icount_before, ic1.valid ? "" : " (no icount)", r->cpu_us);
	return rc;
}

static int run_session(struct ap *a, struct n2vhost *host, const uint8_t *stream, uint32_t nstream,
                       uint32_t width, uint32_t height,
                       struct start_rec *recs, unsigned max_recs, unsigned *nrecs)
{
	const struct n2vc_hw *hw = a->hw;
	uint32_t bs_handle = 0, bs_pa = 0, bs_bytes;
	uint32_t fb_handle[2] = { 0, 0 }, fb_pa[2] = { 0, 0 }, fb_bytes[2];
	uint32_t msg[20];
	struct vdec_dec_info d;
	int rc, i;
	unsigned nr = 0;

	*nrecs = 0;
	bs_bytes = (nstream + 0xfffu) & ~0xfffu;
	if (bs_bytes < 0x100000u)
		bs_bytes = 0x100000u;   /* DEC1's reference uses 1 MiB per bs buffer */
	fb_bytes[0] = ((width * height * 3u / 2u) + 0xfffu) & ~0xfffu;
	fb_bytes[1] = fb_bytes[0];
	if (fb_bytes[0] < 0x10000u)
		fb_bytes[0] = 0x10000u;

	/* Buffers first: the vendor imports the share ids, so they must exist before START. */
	if (hw->dma_alloc == NULL ||
	    hw->dma_alloc(hw->ctx, bs_bytes, 0, &bs_handle, &bs_pa) != 0) {
		fprintf(stderr, "N2DEC02 dma_alloc(bs) failed\n");
		return -1;
	}
	/* The bytes go into the *guest mapping* of the share (that is what the vendor reads through
	 * ion_mmap); n2vc_dma_sync(to_device) copies that mapping into the back end's DMA mirror for
	 * the hardware.  Writing only the mirror would be wiped by the first to_device sync. */
	for (i = 0; i < 2; i++) {
		if (hw->dma_alloc == NULL ||
		    hw->dma_alloc(hw->ctx, fb_bytes[i], 0, &fb_handle[i], &fb_pa[i]) != 0) {
			fprintf(stderr, "N2DEC02 dma_alloc(fb%d) failed\n", i);
			return -1;
		}
	}
	/* The vendor reads the bitstream through a guest mapping of the share (ion_mmap is
	 * n2vc_dma_guest_map, plain guest memory).  The AP side must copy the bytes into that
	 * mapping before the job, exactly as n2ap_venc does with n2vc_dma_sync(to_device=1);
	 * without this the H.264 open sees zeros and fails with "invalid NALU". */
	if (n2vhost_mem(host) != NULL) {
		struct n2a32_mem *mem = n2vhost_mem(host);
		uint32_t g = 0;

		if (n2vc_dma_guest_map(hw, mem, bs_handle, bs_bytes, &g) == 0 && g != 0) {
			int rc2;

			uint8_t *gp = n2a32_ptr(mem, g);

			memcpy(gp, stream, nstream);
			rc2 = n2vc_dma_sync(hw, mem, g, 0, nstream, 1);
			printf("N2DEC02 bs guest mapping 0x%08x to_device rc=%d first=%02x%02x%02x%02x\n",
			       g, rc2, gp[0], gp[1], gp[2], gp[3]);
		}
		for (i = 0; i < 2; i++) {
			uint32_t fg = 0;

			if (n2vc_dma_guest_map(hw, mem, fb_handle[i], fb_bytes[i], &fg) == 0 && fg != 0)
				printf("N2DEC02 fb%d guest mapping 0x%08x\n", i, fg);
		}
	} else {
		fprintf(stderr, "N2DEC02 no n2vc_dma_guest_map/n2vc_dma_sync\n");
	}
	if (n2shim_ion_register_share == NULL) {
		fprintf(stderr, "N2DEC02 no n2shim_ion_register_share: the vendor's ion_import will fail\n");
	} else {
		int r1 = n2shim_ion_register_share(SHARE_BS, bs_handle, bs_pa, bs_bytes);
		int r2 = n2shim_ion_register_share(SHARE_FB0, fb_handle[0], fb_pa[0], fb_bytes[0]);
		int r3 = n2shim_ion_register_share(SHARE_FB1, fb_handle[1], fb_pa[1], fb_bytes[1]);

		printf("N2DEC02 buffers bs handle=%u pa=0x%08x bytes=%u share=%d; fb0 handle=%u pa=0x%08x share=%d; "
		       "fb1 handle=%u pa=0x%08x share=%d\n", bs_handle, bs_pa, bs_bytes, r1, fb_handle[0],
		       fb_pa[0], r2, fb_handle[1], fb_pa[1], r3);
		if (r1 != 0 || r2 != 0 || r3 != 0)
			fprintf(stderr, "N2DEC02 share registration failed (%d/%d/%d)\n", r1, r2, r3);
	}

	/* QUERY_CAP: formats (8) and frame sizes (9) on IPI_VDEC_COMMON, like vdec_if_query_cap(). */
	for (i = 0; i < 2; i++) {
		memset(msg, 0, sizeof(msg));
		put32(msg, 0, AP_IPIMSG_DEC_QUERY_CAP);
		put32(msg, 4, i == 0 ? GET_PARAM_CAPABILITY_SUPPORTED_FORMATS
		                     : GET_PARAM_CAPABILITY_FRAME_SIZES);
		put64(msg, 8, AP_COOKIE);   /* md32_ipi_send refuses an ack whose bytes 8..15 are 0 */
		put64(msg, 16, 0x1000);
		rc = ap_send(a, IPI_VDEC_H264, msg, 24, AP_IPIMSG_DEC_QUERY_CAP,
		             VCU_IPIMSG_DEC_QUERY_CAP_ACK);
		printf("N2DEC02 query_cap id=%u rc=%d status=%d\n", i == 0 ? 8u : 9u, rc,
		       (int)a->reply_status);
		if (rc != 0)
			return -1;
	}

	/* INIT (vdec_if_init): svp_mode 0 in the reserved field, AP cookie in ap_inst_addr. */
	memset(msg, 0, sizeof(msg));
	put32(msg, 0, AP_IPIMSG_DEC_INIT);
	put32(msg, 4, 0);
	put64(msg, 8, AP_COOKIE);
	rc = ap_send(a, IPI_VDEC_H264, msg, 16, AP_IPIMSG_DEC_INIT, VCU_IPIMSG_DEC_INIT_ACK);
	printf("N2DEC02 init rc=%d status=%d vcu_inst=0x%08x vsi=%s\n", rc, (int)a->reply_status,
	       a->inst_addr, a->vsi_ok ? "mapped" : "NOT MAPPED");
	if (rc != 0 || !a->vsi_ok)
		return -1;

	/* SET_PARAM_FB_NUM_PLANES is vsi-only (vdec_common_if.c:216-219): the kernel writes the vsi
	 * field directly, before the first decode.  general_buf_fd = -1 says "no general buffer"
	 * (vdec_vcu_if.c:349-352).  Both are read during the vendor's H.264 open. */
	{
		struct vdec_dec_info d0 = vsi_get_dec(a);
		struct vdec_vsi_prefix *v = (struct vdec_vsi_prefix *)a->vsi;

		d0.fb_num_planes = 2;
		vsi_put_dec(a, &d0);
		v->general_buf_fd = -1;
		v->general_buf_dma = 0;
		v->general_buf_size = 0;
	}

	/* SET_PARAM FRAME_SIZE (2 values) and DECODE_MODE (1 value), vdec_if_set_param(). */
	memset(msg, 0, sizeof(msg));
	put32(msg, 0, AP_IPIMSG_DEC_SET_PARAM);
	put32(msg, 4, a->inst_addr);
	put32(msg, 8, SET_PARAM_FRAME_SIZE);
	put32(msg, 12, width);
	put32(msg, 16, height);
	rc = ap_send(a, IPI_VDEC_H264, msg, 44, AP_IPIMSG_DEC_SET_PARAM, VCU_IPIMSG_DEC_SET_PARAM_ACK);
	printf("N2DEC02 set_param frame_size %ux%u rc=%d status=%d\n", width, height, rc,
	       (int)a->reply_status);
	if (rc != 0)
		return -1;

	memset(msg, 0, sizeof(msg));
	put32(msg, 0, AP_IPIMSG_DEC_SET_PARAM);
	put32(msg, 4, a->inst_addr);
	put32(msg, 8, SET_PARAM_DECODE_MODE);
	put32(msg, 12, 0);   /* normal decode */
	rc = ap_send(a, IPI_VDEC_H264, msg, 44, AP_IPIMSG_DEC_SET_PARAM, VCU_IPIMSG_DEC_SET_PARAM_ACK);
	printf("N2DEC02 set_param decode_mode rc=%d status=%d\n", rc, (int)a->reply_status);
	if (rc != 0)
		return -1;

	seed_instance(a);

	/* START 1: header only (fb == NULL, index 0xFF), exactly vdec_if_decode(src, NULL). */
	memset(&d, 0, sizeof(d));
	d.bs_dma = bs_pa;
	d.bs_fd = SHARE_BS;
	/* The vendor's get_ringbuf() takes vsi.dec.vdec_bs_va (libvpud_vcodec.so 0xc562 reads
	 * inst+0x1ac8/+0x1acc, i.e. dec.vdec_bs_va with VIDEO_MAX_FRAME=64) as its ring key: a
	 * non-zero AP cookie, not a size.  Zero is what it reports as "input handle NULL". */
	d.vdec_bs_va = BS_COOKIE;
	d.vdec_fb_va = 0;
	d.index = 0xFF;
	d.fb_num_planes = 2;
	if (max_recs > 0) {
		struct start_rec *r = &recs[0];

		r->bs_bytes = bs_bytes;
		r->bs_length = nstream;
		rc = do_start(a, host, "header", &d, r);
		nr = 1;
	} else {
		rc = -1;
	}

	/* START 2: the same access unit with one frame buffer, i.e. a real frame decode.  Only
	 * when the first START was accepted: a header failure means nothing below can work. */
	if (rc == 0 && a->reply_status == 0 && nr < max_recs) {
		struct start_rec *r = &recs[nr];

		memset(&d, 0, sizeof(d));
		d.bs_dma = bs_pa;
		d.bs_fd = SHARE_BS;
		d.vdec_bs_va = BS_COOKIE;
		d.vdec_fb_va = FB_COOKIE + 0u;
		d.index = 0;
		d.fb_num_planes = 2;
		d.fb_dma[0] = fb_pa[0];
		d.fb_dma[1] = fb_pa[1];
		d.fb_fd[0] = SHARE_FB0;
		d.fb_fd[1] = SHARE_FB1;
		r->bs_bytes = bs_bytes;
		r->bs_length = nstream;
		rc = do_start(a, host, "frame", &d, r);
		nr++;
	}

	*nrecs = nr;

	/* DEINIT, then the guest is stopped by the caller. */
	memset(msg, 0, sizeof(msg));
	put32(msg, 0, AP_IPIMSG_DEC_DEINIT);
	put32(msg, 4, a->inst_addr);
	rc = ap_send(a, IPI_VDEC_H264, msg, 8, AP_IPIMSG_DEC_DEINIT, VCU_IPIMSG_DEC_DEINIT_ACK);
	printf("N2DEC02 deinit rc=%d status=%d\n", rc, (int)a->reply_status);
	return 0;
}

/* ------------------------------------------------------------------ main */

static uint8_t *read_file(const char *path, uint32_t *n)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf = NULL;
	long len;
	size_t got;

	*n = 0;
	if (f == NULL) {
		fprintf(stderr, "N2DEC02 cannot open %s: %s\n", path, strerror(errno));
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	buf = malloc((size_t)len + 1);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}
	got = fread(buf, 1, (size_t)len, f);
	fclose(f);
	if (got != (size_t)len) {
		free(buf);
		return NULL;
	}
	*n = (uint32_t)len;
	return buf;
}

int main(int argc, char **argv)
{
	const char *vendor = NULL, *stream_path = NULL, *trace = NULL, *record = NULL, *json_path = NULL;
	uint32_t width = 320, height = 240;
	uint8_t *stream;
	uint32_t nstream = 0;
	struct n2vc_hw fake;
	struct n2tr *tr;
	const struct n2vc_hw *thw;
	struct n2vhost *host = NULL;
	struct n2vcu *vcu;
	struct ap a;
	struct start_rec recs[4];
	unsigned nrecs = 0;
	FILE *json = NULL;
	int i, rc, status = 1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--vendor") == 0 && i + 1 < argc)
			vendor = argv[++i];
		else if (strcmp(argv[i], "--stream") == 0 && i + 1 < argc)
			stream_path = argv[++i];
		else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
			width = (uint32_t)strtoul(argv[++i], NULL, 10);
		else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
			height = (uint32_t)strtoul(argv[++i], NULL, 10);
		else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc)
			trace = argv[++i];
		else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
			record = argv[++i];
		else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc)
			json_path = argv[++i];
		else {
			fprintf(stderr, "vdec_probe: unknown argument %s\n", argv[i]);
			return 2;
		}
	}
	if (stream_path == NULL) {
		fprintf(stderr, "vdec_probe: --stream is required\n");
		return 2;
	}
	stream = read_file(stream_path, &nstream);
	if (stream == NULL || nstream == 0) {
		fprintf(stderr, "vdec_probe: empty stream %s\n", stream_path);
		return 2;
	}

	if (n2vc_hw_open_fake(&fake, record) != 0) {
		fprintf(stderr, "vdec_probe: n2vc_hw_open_fake failed\n");
		return 2;
	}
	tr = n2tr_open(&fake, trace);
	if (tr == NULL) {
		fprintf(stderr, "vdec_probe: n2tr_open failed\n");
		return 2;
	}
	thw = n2tr_hw(tr);

	memset(&a, 0, sizeof(a));
	pthread_mutex_init(&a.mu, NULL);
	pthread_cond_init(&a.cv, NULL);

	rc = n2vhost_start(&host, thw, vendor);
	if (host == NULL || rc < 0) {
		printf("N2DEC02 start failed: %s\n", host ? n2vhost_error(host) : "no host");
		goto out;
	}
	vcu = n2vhost_vcu(host);
	if (vcu == NULL) {
		printf("N2DEC02 no /dev/vcu model in the build\n");
		goto out;
	}
	a.vcu = vcu;
	a.hw = n2vhost_hw(host);
	a.tr = tr;
	if (n2shim_log == NULL)
		n2shim_log = probe_guest_log;
	if (n2vcu_ap_register(vcu, IPI_VDEC_COMMON, ap_handler, &a) != 0 ||
	    n2vcu_ap_register(vcu, IPI_VDEC_H264, ap_handler, &a) != 0) {
		printf("N2DEC02 n2vcu_ap_register failed\n");
		goto out;
	}
	if (getenv("N2DEC02_DEBUG")) {
		int k;

		for (k = 1; k < 50; k++)
			(void)n2vcu_ap_register(vcu, k, ap_handler, &a);
	}

	ap_settle();
	rc = run_session(&a, host, stream, nstream, width, height, recs, 4, &nrecs);
	if (rc == 0)
		status = 0;

	/* Let the daemon settle, then stop it (bounded, like IV's runner). */
	(void)n2vhost_join(host, 1000);

	if (json_path != NULL)
		json = fopen(json_path, "w");
	if (json != NULL) {
		struct stats_snap fin;
		struct icount ic;

		snap(&a, &fin);
		ic = guest_icount(host);
		fprintf(json, "{\n");
		fprintf(json, "\"stream\": \"%s\", \"width\": %u, \"height\": %u, \"stream_bytes\": %u,\n",
			stream_path, width, height, nstream);
		fprintf(json, "\"have_fmt\": %d, \"have_fs\": %d, \"fmt_entries\": %u, \"fs_entries\": %u,\n",
			a.have_fmt, a.have_fs, a.n_fmt, a.n_fs);
		fprintf(json, "\"requests\": {\"lock_lat\": %u, \"unlock_lat\": %u, \"lock_core\": %u, "
		        "\"unlock_core\": %u, \"waitisr\": %u, \"get_fb\": %u, \"put_fb\": %u, \"other\": %u},\n",
			a.n_lock_lat, a.n_unlock_lat, a.n_lock_core, a.n_unlock_core, a.n_waitisr,
			a.n_getfb, a.n_putfb, a.n_other);
		fprintf(json, "\"guest_icount_total\": %" PRIu64 ", \"guest_icount_valid\": %d,\n",
			ic.value, ic.valid);
		fprintf(json, "\"run_stats\": {\"gce_flushes\": %" PRIu64 ", \"gce_cmds\": %" PRIu64 ", "
		        "\"mmio_reads\": %" PRIu64 ", \"mmio_writes\": %" PRIu64 ", \"mmio_bad\": %" PRIu64
		        ", \"last_bad_mmio_addr\": %" PRIu64 "},\n",
			fin.vcu.gce_flushes, fin.vcu.gce_cmds, fin.vcu.mmio_reads, fin.vcu.mmio_writes,
			fin.vcu.mmio_bad, fin.vcu.last_bad_mmio_addr);
		fprintf(json, "\"trace\": {\"reads\": %" PRIu64 ", \"writes\": %" PRIu64 ", \"refused\": %" PRIu64
		        ", \"events\": %" PRIu64 ", \"vdec_all_reads\": %" PRIu64 ", \"vdec_all_writes\": %" PRIu64 "},\n",
			fin.tr.reg_reads, fin.tr.reg_writes + fin.tr.reg_writes_mask, fin.tr.refused,
			fin.tr_events, fin.tr.vdec_all_reads, fin.tr.vdec_all_writes);
		fprintf(json, "\"starts\": [");
		for (i = 0; i < (int)nrecs; i++) {
			struct start_rec *r = &recs[i];

			fprintf(json, "%s\n {\"kind\": \"%s\", \"status\": %d, \"changed_info\": %u, "
			        "\"pic_w\": %u, \"pic_h\": %u, \"buf_w\": %u, \"buf_h\": %u, \"dpb_sz\": %u, "
			        "\"gce_flushes\": %" PRIu64 ", \"gce_cmds\": %" PRIu64 ", \"mmio_reads\": %" PRIu64
			        ", \"mmio_writes\": %" PRIu64 ", \"mmio_bad\": %" PRIu64
			        ", \"last_bad_mmio_addr\": %" PRIu64 ", \"tr_reads\": %" PRIu64
			        ", \"tr_writes\": %" PRIu64 ", \"tr_refused\": %" PRIu64 ", \"tr_events\": %" PRIu64
			        ", \"icount\": %" PRIu64 ", \"cpu_us\": %" PRIu64 "}",
			        i ? "," : "", r->kind, r->status, r->changed_info, r->pic_w, r->pic_h, r->buf_w,
			        r->buf_h, r->dpb_sz, r->gce_flushes, r->gce_cmds, r->mmio_reads, r->mmio_writes,
			        r->mmio_bad, r->last_bad_mmio_addr, r->tr_reads, r->tr_writes, r->tr_refused,
			        r->tr_events, r->icount_after - r->icount_before, r->cpu_us);
		}
		fprintf(json, "]\n}\n");
		fclose(json);
	}

out:
	if (host != NULL) {
		printf("N2DEC02 guest %s\n", n2vhost_stop_text(host));
		n2vhost_stop(host);
	}
	n2tr_free(tr);
	if (fake.close)
		fake.close(fake.ctx);
	free(stream);
	return status;
}
