/* SPDX-License-Identifier: MIT */
/*
 * vdec_ref.c -- opus-dec1 reference AP-side H.264 decode driver for the host runner (NOT a product file).
 *
 * Plays the role of the Linux V4L2 decoder (vdec_common_if.c + vdec_vcu_if.c + mtk_vcodec_dec.c) against the phone's
 * vpud running under the interpreter on the fake hardware, for one H.264 stream:
 *   QUERY_CAP -> INIT -> SET_PARAM(FRAME_SIZE, DECODE_MODE) -> header START (fb none) -> per access unit START with one
 *   free frame buffer -> harvest list_disp / list_free / list_free_bs -> RESET (flush) -> DEINIT.
 * It is the executable specification for DEC01 (ap/n2ap_vdec.c) and DEC05 (daemon decode service): every rule it follows
 * is commented with the kernel line or vendor address it comes from.  Struct layouts are written from the kernel's
 * vdec_ipi_msg.h / vcodec_ipi_msg.h with VIDEO_MAX_FRAME = 64 (nord2 kernel include/uapi/linux/videodev2.h:73).
 *
 * Usage: vdec_ref <vendor vfs dir> <stream.h264> [max_frames]
 *   VR_FAKEIRQ=1   answer WAITISR with success without waiting (the fake VDEC raises no interrupt)
 *   VR_LOG=1       print the vendor log
 * Last line: "VR result frames_in=N displayed=M ..." and "VR reached VD-x".
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "n2vhost.h"
#include "n2vcu.h"
#include "n2vc_hw.h"
#include "n2vc_hw_fake.h"
#include "n2ap_venc.h"          /* n2vc_dma_guest_alloc */
#include "n2shim_thread.h"

extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa, uint32_t bytes);
extern void (*n2shim_log)(int prio, const char *tag, const char *text);

/* ---------------------------------------------------------------- kernel layouts (VIDEO_MAX_FRAME = 64) */
enum { IPI_VDEC_COMMON = 1, IPI_VDEC_H264 = 2, IPI_VDEC_H265 = 3 };
enum { DEC_INIT = 0xA000, DEC_START, DEC_END, DEC_DEINIT, DEC_RESET, DEC_SET_PARAM, DEC_QUERY_CAP,
	ACK_BASE = 0xB000, REQ_WAITISR = 0xC000, REQ_GET_FB, REQ_PUT_FB, REQ_LOCK_CORE, REQ_UNLOCK_CORE, REQ_LOCK_LAT,
	REQ_UNLOCK_LAT };
enum { SP_DECODE_MODE = 0, SP_FRAME_SIZE = 1 };
#define MAXF 64
#define MAXP 8
struct ipi_fb { uint64_t fb_va, y_dma, c_dma; int32_t poc; uint64_t ts; uint32_t reserved; };          /* 48 B */
struct ring_bs { uint64_t va[MAXF]; uint32_t ri, wi, cnt, reserved; };                                   /* 0x210 */
struct ring_fb { struct ipi_fb fb[MAXF]; uint32_t ri, wi, cnt, reserved; };                              /* 0xc10 */
struct dec_info { uint32_t dpb_sz, changed; uint64_t bs_dma, bs_fd, fb_dma[MAXP], fb_fd[MAXP], bs_va, fb_va;
	uint32_t planes, index, wait_key, error_map; uint64_t ts; uint32_t queued; };                       /* 200 B */
struct pic_info { uint32_t pic_w, pic_h, buf_w, buf_h, fb_sz[MAXP], bitdepth, layout, fourcc; };
struct vsi {
	struct ring_bs free_bs; struct ring_fb free_fb, disp; struct dec_info dec; struct pic_info pic;
	uint32_t color[17]; int32_t crop[4]; uint32_t fmts[128][3]; uint32_t sizes[128][9];
	uint32_t aspect, fix, fix_svp, interlacing, codec_type; uint8_t crc[256], golden[256]; uint8_t input_driven;
	int32_t general_buf_fd; uint64_t general_buf_dma; uint32_t general_buf_size;
};
_Static_assert(offsetof(struct vsi, free_fb) == 0x210, "list_free");
_Static_assert(offsetof(struct vsi, disp) == 0xe20, "list_disp");
_Static_assert(offsetof(struct vsi, dec) == 0x1a30, "dec (libvpud_vcodec reads vdec_bs_va at vsi+0x1ac8)");
_Static_assert(offsetof(struct vsi, dec.bs_va) == 0x1ac8, "vdec_bs_va");
_Static_assert(offsetof(struct vsi, dec.ts) == 0x1ae8, "timestamp");
_Static_assert(offsetof(struct vsi, pic) == 0x1af8, "pic");

/* ---------------------------------------------------------------- state */
static struct n2vcu *g_vcu; static const struct n2vc_hw *g_hw; static struct n2a32_mem *g_mem;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static uint32_t last_ack, vcu_inst, qc_data; static int32_t last_status; static int acked;
static struct vsi *V;
static int nreq[8], lock_core, lock_lat;

#define NFB 20
struct fb { uint32_t h[2], pa[2], bytes[2], share[2]; int busy, displayed; };   /* busy: owned by the decoder */
static struct fb fbs[NFB]; static int nfb;
#define NBS 4
struct bs { uint32_t h, pa, g, bytes, share; int busy; };
static struct bs bss[NBS];
static unsigned displayed, freed_fb, freed_bs;

static void vlog(int p, const char *tag, const char *t) { (void)p; if (getenv("VR_LOG")) fprintf(stderr, "G[%s] %s\n", tag ? tag : "", t); }

/* Fill vsi.dec with one free frame buffer (kernel: vdec_common_if.c:227-233 at START, vdec_vcu_if.c GET_FRAME_BUFFER).
 * vdec_fb_va is the AP cookie the vendor returns in list_disp / list_free: here 0xF0000000 + index (never 0). */
static int give_fb(void)
{
	for (int i = 0; i < nfb; i++) {
		if (fbs[i].busy)
			continue;
		fbs[i].busy = 1; fbs[i].displayed = 0;
		V->dec.fb_va = 0xF0000000u + (uint32_t)i;
		V->dec.index = (uint32_t)i;
		for (int p = 0; p < 2; p++) { V->dec.fb_dma[p] = fbs[i].pa[p]; V->dec.fb_fd[p] = fbs[i].share[p]; }
		return i;
	}
	V->dec.fb_va = 0; V->dec.index = 0xFF;                /* kernel: index 0xFF when there is none (:253) */
	return -1;
}

static int cookie_fb(uint64_t va) { return (va >= 0xF0000000u && va < 0xF0000000u + (uint64_t)nfb) ? (int)(va - 0xF0000000u) : -1; }

/* Harvest the three rings the vendor fills (kernel vdec_get_fb / vdec_get_bs: read_idx++, count--). */
static void harvest(void)
{
	while (V->disp.cnt) {
		struct ipi_fb *e = &V->disp.fb[V->disp.ri];
		int i = cookie_fb(e->fb_va);
		printf("VR   disp fb=%d poc=%d pts=%llu y=0x%llx\n", i, e->poc, (unsigned long long)e->ts, (unsigned long long)e->y_dma);
		if (i >= 0) { fbs[i].displayed = 1; displayed++; }
		V->disp.ri = (V->disp.ri + 1) % MAXF; V->disp.cnt--;
	}
	while (V->free_fb.cnt) {
		struct ipi_fb *e = &V->free_fb.fb[V->free_fb.ri];
		int i = cookie_fb(e->fb_va);
		if (i >= 0) { fbs[i].busy = 0; freed_fb++; }        /* reference released: reusable */
		V->free_fb.ri = (V->free_fb.ri + 1) % MAXF; V->free_fb.cnt--;
	}
	while (V->free_bs.cnt) {
		uint64_t va = V->free_bs.va[V->free_bs.ri];
		if (va >= 0xB0000000u && va < 0xB0000000u + NBS) { bss[va - 0xB0000000u].busy = 0; freed_bs++; }
		V->free_bs.ri = (V->free_bs.ri + 1) % MAXF; V->free_bs.cnt--;
	}
}

static int handler(void *ctx, int id, void *buf, uint32_t len)
{
	uint32_t *w = buf, msg = w[0]; int32_t st = (int32_t)w[1];
	(void)ctx; (void)id;
	if (msg >= REQ_WAITISR && msg <= REQ_UNLOCK_LAT) {
		nreq[msg - REQ_WAITISR]++;
		switch (msg) {
		case REQ_LOCK_LAT: case REQ_LOCK_CORE:      /* vdec_decode_prepare: hw lock + power + clocks (mtk_vcodec_dec_pm.c:154-251) */
			if (g_hw->power) {
				if (!lock_core && !lock_lat) g_hw->power(g_hw->ctx, RWVC_BLK_VDEC_SOC, 1);
				if (msg == REQ_LOCK_CORE && !lock_core++) g_hw->power(g_hw->ctx, RWVC_BLK_VDEC_CORE, 1);
				if (msg == REQ_LOCK_LAT) lock_lat++;
			}
			break;
		case REQ_UNLOCK_LAT: case REQ_UNLOCK_CORE:
			if (g_hw->power) {
				if (msg == REQ_UNLOCK_CORE && lock_core && !--lock_core) g_hw->power(g_hw->ctx, RWVC_BLK_VDEC_CORE, 0);
				if (msg == REQ_UNLOCK_LAT && lock_lat) lock_lat--;
				if (!lock_core && !lock_lat) g_hw->power(g_hw->ctx, RWVC_BLK_VDEC_SOC, 0);
			}
			break;
		case REQ_WAITISR: {                         /* status in = core id (0 CORE, 1 LAT); out = 0 or -1 (vdec_vcu_if.c:241-256) */
			uint32_t s = 0; int rc = 0;
			if (!getenv("VR_FAKEIRQ"))
				rc = g_hw->wait_irq(g_hw->ctx, st == 1 ? RWVC_IRQ_VDEC_LAT : RWVC_IRQ_VDEC, 500, &s);
			w[1] = (uint32_t)(rc == 0 ? 0 : -1);
			break; }
		case REQ_GET_FB:                            /* vdec_vcu_if.c:279-330: one free fb or index 0xFF */
			give_fb();
			break;
		case REQ_PUT_FB:                            /* mtk_vdec_put_fb: harvest the rings now */
			harvest();
			break;
		}
		return 1;                                   /* answered in place, no ACK (the kernel returns 1) */
	}
	pthread_mutex_lock(&mu);
	last_ack = msg; last_status = st; acked = 1;
	if (msg == ACK_BASE + 0 && len >= 20) vcu_inst = w[4];
	if (msg == ACK_BASE + 6) qc_data = w[8];
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mu);
	return 0;
}

static int send_wait(int ipi, const void *m, uint32_t len, unsigned ms)
{
	struct timespec ts; int rc = 0;
	uint32_t want = (*(const uint32_t *)m - DEC_INIT) + ACK_BASE;
	pthread_mutex_lock(&mu); acked = 0; pthread_mutex_unlock(&mu);
	if (n2vcu_ap_send(g_vcu, ipi, m, len) != 0) return -100;
	clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
	pthread_mutex_lock(&mu);
	while (!acked && rc == 0) rc = pthread_cond_timedwait(&cv, &mu, &ts);
	int got = acked; uint32_t a = last_ack; int32_t s = last_status;
	pthread_mutex_unlock(&mu);
	(void)n2vcu_ap_wait_dispatched(g_vcu, ipi, 2000);   /* V41 rev 3 ordering: ACK, then the daemon is back in GET_OBJECT */
	if (!got) return -ETIMEDOUT;
	return a == want ? s : -200;
}

/* ---------------------------------------------------------------- Annex B access-unit splitter */
static const uint8_t *next_sc(const uint8_t *p, const uint8_t *e)
{
	for (; p + 3 <= e; p++)
		if (p[0] == 0 && p[1] == 0 && p[2] == 1) return p;
	return e;
}
/* Returns the length of the access unit starting at b (parameter sets/SEI stay with the following slice).
 * A new AU starts at a non-VCL NAL (6..9, 14..18) after a VCL NAL, or at a VCL NAL (1/5) whose first_mb_in_slice == 0
 * (first payload bit 1) after a VCL NAL. */
static int g_hevc, g_ipi = IPI_VDEC_H264;
/* HEVC: nal_unit_type = (byte0 >> 1) & 0x3f; VCL 0..31; a new AU starts at VPS/SPS/PPS/AUD/prefix SEI (32..35, 39,
 * 41..44, 48..55) after a VCL NAL, or at a VCL NAL with first_slice_segment_in_pic_flag (bit 7 of byte 2). */
static size_t au_len_hevc(const uint8_t *b, size_t n)
{
	const uint8_t *e = b + n, *p = next_sc(b, e);
	int seen_vcl = 0;
	while (p < e) {
		const uint8_t *nal = p + 3; int t = nal < e ? ((nal[0] >> 1) & 0x3f) : 0;
		const uint8_t *start = (p > b && p[-1] == 0) ? p - 1 : p;
		int vcl = t < 32;
		if (seen_vcl && ((!vcl && ((t >= 32 && t <= 35) || t == 39 || (t >= 41 && t <= 44) || (t >= 48 && t <= 55))) ||
				 (vcl && nal + 2 < e && (nal[2] & 0x80))))
			return (size_t)(start - b);
		if (vcl) seen_vcl = 1;
		p = next_sc(nal, e);
	}
	return n;
}

static size_t au_len(const uint8_t *b, size_t n)
{
	if (g_hevc) return au_len_hevc(b, n);
	const uint8_t *e = b + n, *p = next_sc(b, e);
	int seen_vcl = 0;
	while (p < e) {
		const uint8_t *nal = p + 3; int t = nal < e ? (nal[0] & 0x1f) : 0;
		const uint8_t *start = (p > b && p[-1] == 0) ? p - 1 : p;
		int vcl = (t == 1 || t == 5);
		if (seen_vcl && ((!vcl && (t == 6 || t == 7 || t == 8 || t == 9 || (t >= 14 && t <= 18))) ||
				 (vcl && nal + 1 < e && (nal[1] & 0x80))))
			return (size_t)(start - b);
		if (vcl) seen_vcl = 1;
		p = next_sc(nal, e);
	}
	return n;
}

static int start_au(const uint8_t *au, size_t n, int with_fb, uint64_t pts)
{
	int bi = -1;
	for (int i = 0; i < NBS; i++) if (!bss[i].busy) { bi = i; break; }
	if (bi < 0) { harvest(); for (int i = 0; i < NBS; i++) if (!bss[i].busy) { bi = i; break; } }
	if (bi < 0 || n > bss[bi].bytes) return -1000;
	memcpy(n2a32_ptr(g_mem, bss[bi].g), au, n);
	/* kernel vdec_common_if.c:219-259 */
	V->dec.bs_va = 0xB0000000u + (uint32_t)bi;         /* non-zero cookie: get_ringbuf(vsi+0x1ac8) rejects 0 */
	V->dec.bs_dma = bss[bi].pa;
	V->dec.bs_fd = bss[bi].share;                       /* "fd" = ion share id resolved by the V34 ion shim */
	V->general_buf_fd = -1; V->general_buf_size = 0; V->general_buf_dma = 0;
	V->dec.ts = pts;
	if (with_fb) give_fb(); else { V->dec.fb_va = 0; V->dec.index = 0xFF; }
	bss[bi].busy = 1;
	/* bitstream to_device sync would go here on the phone (n2vc_dma_sync) */
	uint32_t st[6] = { DEC_START, vcu_inst, (uint32_t)bss[bi].bytes, (uint32_t)n, 0, 0 };   /* data[0] size, [1] length, [2] flags */
	int rc = send_wait(g_ipi, st, sizeof st, 20000);
	harvest();
	return rc;
}

static double pcpu(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv)
{
	struct n2vc_hw hw; struct n2vhost *h = NULL; int rc, reached = 0; unsigned frames_in = 0, maxf = argc > 3 ? (unsigned)atoi(argv[3]) : 1000;
	const char *names[] = { "none", "VD-a QUERY_CAP", "VD-b INIT", "VD-c header START", "VD-d first VDEC register", "VD-e WAITISR",
		"VD-f frame START acked", "VD-g picture displayed" };
	if (argc < 3) { fprintf(stderr, "usage: vdec_ref vfs stream.h264 [max_frames]\n"); return 2; }
	setvbuf(stdout, NULL, _IOLBF, 0);
	n2shim_log = vlog;
	FILE *f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 2; }
	static uint8_t sbuf[8 << 20]; size_t sn = fread(sbuf, 1, sizeof sbuf, f); fclose(f);
	if (n2vc_hw_open_fake(&hw, getenv("VR_REC") ? getenv("VR_REC") : "/dev/null")) { puts("VR fake open failed"); return 1; }
	rc = n2vhost_start(&h, &hw, argv[1]);
	if (rc != 0) { printf("VR start rc=%d %s\n", rc, h ? n2vhost_error(h) : ""); return 1; }
	g_vcu = n2vhost_vcu(h); g_hw = n2vhost_hw(h); g_mem = n2vhost_mem(h);
	if (getenv("VR_HEVC") && *getenv("VR_HEVC")) { g_hevc = 1; g_ipi = IPI_VDEC_H265; }
	n2vcu_ap_register(g_vcu, g_ipi, handler, NULL);
	n2vcu_ap_register(g_vcu, IPI_VDEC_COMMON, handler, NULL);
	struct timespec sl = { 1, 0 }; nanosleep(&sl, NULL);   /* let vpud reach VCU_GET_OBJECT */

	uint32_t qc[6] = { DEC_QUERY_CAP, 8, 0xCAFE1000u, 0, 0xCAFE2000u, 0 };
	rc = send_wait(IPI_VDEC_COMMON, qc, sizeof qc, 4000);
	printf("VR QUERY_CAP rc=%d data=0x%08x\n", rc, qc_data);
	if (rc == 0) reached = 1;
	if (getenv("VR_LOGSET")) n2vcu_ap_set_log(g_vcu, getenv("VR_LOGSET"));   /* e.g. "-vpud_log 3" (vpud's own option text) */
	uint32_t init[4] = { DEC_INIT, 0 /* svp_mode */, 0xCAFE0000u, 0 };
	rc = send_wait(g_ipi, init, sizeof init, 8000);
	V = rc == 0 ? n2vcu_map_dm_addr(g_vcu, vcu_inst) : NULL;
	printf("VR INIT rc=%d vsi=0x%08x\n", rc, vcu_inst);
	if (!V) goto out;
	reached = 2;
	V->dec.planes = 2;                                     /* SET_PARAM_FB_NUM_PLANES is vsi-only */
	{ uint32_t sp[11] = { DEC_SET_PARAM, vcu_inst, SP_FRAME_SIZE, 0, 0 };   /* 0x0 = from the stream */
	  rc = send_wait(g_ipi, sp, sizeof sp, 4000); }
	{ uint32_t sp[11] = { DEC_SET_PARAM, vcu_inst, SP_DECODE_MODE, 0 };
	  rc |= send_wait(g_ipi, sp, sizeof sp, 4000); }
	if (getenv("VR_UFO")) {                                /* SET_PARAM_UFO_MODE (3): the kernel never sends it (vdec_common_if.c:621) */
		uint32_t sp[11] = { DEC_SET_PARAM, vcu_inst, 3, (uint32_t)atoi(getenv("VR_UFO")) };
		rc |= send_wait(g_ipi, sp, sizeof sp, 4000);
	}
	printf("VR SET_PARAM rc=%d\n", rc);

	for (int i = 0; i < NBS; i++) {
		bss[i].bytes = 1u << 20;
		if (n2vc_dma_guest_alloc(g_hw, g_mem, bss[i].bytes, 0, &bss[i].h, &bss[i].pa, &bss[i].g)) { puts("VR bs alloc failed"); goto out; }
		bss[i].share = 0x700u + (uint32_t)i;
		n2shim_ion_register_share((int)bss[i].share, bss[i].h, bss[i].pa, bss[i].bytes);
	}
	size_t off = 0, n = au_len(sbuf, sn);
	double c0 = pcpu();
	rc = start_au(sbuf, n, 0, 0);                          /* header decode: fb none (mtk_vcodec_dec.c:2212) */
	printf("VR header START rc=%d changed=0x%x pic=%ux%u buf=%ux%u fb_sz=%u/%u dpb=%u fourcc=%.4s cpu=%.3fs\n", rc, V->dec.changed,
		V->pic.pic_w, V->pic.pic_h, V->pic.buf_w, V->pic.buf_h, V->pic.fb_sz[0], V->pic.fb_sz[1], V->dec.dpb_sz,
		(char *)&V->pic.fourcc, pcpu() - c0);
	if (rc != 0 || !(V->dec.changed & 1)) goto out;
	reached = 3;
	nfb = (int)V->dec.dpb_sz + 2; if (nfb > NFB) nfb = NFB;   /* GET_PARAM_DPB_SIZE = dpb_sz + 1, plus one in flight */
	for (int i = 0; i < nfb; i++)
		for (int p = 0; p < 2; p++) {
			uint32_t g;
			fbs[i].bytes[p] = V->pic.fb_sz[p];
			if (n2vc_dma_guest_alloc(g_hw, g_mem, fbs[i].bytes[p], 0, &fbs[i].h[p], &fbs[i].pa[p], &g)) { puts("VR fb alloc failed"); goto out; }
			fbs[i].share[p] = 0x800u + (uint32_t)(i * 2 + p);
			n2shim_ion_register_share((int)fbs[i].share[p], fbs[i].h[p], fbs[i].pa[p], fbs[i].bytes[p]);
		}
	/* The same first buffer is decoded again with a frame buffer (V4L2 keeps the source buffer after the header pass). */
	while (off < sn && frames_in < maxf) {
		n = au_len(sbuf + off, sn - off);
		c0 = pcpu();
		struct n2vcu_stats s0, s1; n2vcu_stats(g_vcu, &s0);
		rc = start_au(sbuf + off, n, 1, 1000u + frames_in);
		n2vcu_stats(g_vcu, &s1);
		printf("VR START au=%u bytes=%zu rc=%d changed=0x%x err=0x%x cpu=%.3fs mmio r=%llu w=%llu bad=%llu req W=%d G=%d P=%d LC=%d UC=%d LL=%d UL=%d\n",
			frames_in, n, rc, V->dec.changed, V->dec.error_map, pcpu() - c0,
			(unsigned long long)(s1.mmio_reads - s0.mmio_reads), (unsigned long long)(s1.mmio_writes - s0.mmio_writes),
			(unsigned long long)(s1.mmio_bad - s0.mmio_bad), nreq[0], nreq[1], nreq[2], nreq[3], nreq[4], nreq[5], nreq[6]);
		if (s1.mmio_reads + s1.mmio_writes > 0 && reached < 4) reached = 4;
		if (nreq[0] && reached < 5) reached = 5;
		if (rc != 0) break;
		if (reached < 6) reached = 6;
		if (displayed && reached < 7) reached = 7;
		off += n; frames_in++;
	}
	{ uint32_t r[2] = { DEC_RESET, vcu_inst };            /* EOS: vdec_decode(NULL) = flush, pictures still queued come out */
	  rc = send_wait(g_ipi, r, sizeof r, 8000); harvest(); printf("VR RESET rc=%d\n", rc); }
	{ uint32_t d[2] = { DEC_DEINIT, vcu_inst }; rc = send_wait(g_ipi, d, sizeof d, 8000); printf("VR DEINIT rc=%d\n", rc); }
out:
	{ uint32_t fi = 0, fa = 0; int fr = n2shim_thread_failure(); n2shim_thread_failure_detail(&fi, &fa);
	  if (fr) printf("VR guest stop reason=%d addr=0x%08x (%s)\n", fr, fa, n2vhost_stop_text(h)); }
	if (getenv("VR_THREADS")) {                            /* per-thread CPU (Linux /proc), to find where host time goes */
		char cmd[128];
		snprintf(cmd, sizeof cmd, "for t in /proc/%d/task/*; do echo \"VR thread $(cat $t/comm) $(cut -d' ' -f14,15 $t/stat)\"; done", (int)getpid());
		if (system(cmd) != 0) printf("VR thread listing failed\n");
	}
	printf("VR result frames_in=%u displayed=%u freed_fb=%u freed_bs=%u\n", frames_in, displayed, freed_fb, freed_bs);
	printf("VR reached %s\n", names[reached]);
	n2vhost_stop(h);
	_exit(reached >= (getenv("VR_MIN") && *getenv("VR_MIN") ? atoi(getenv("VR_MIN")) : 6) ? 0 : 1);
}
