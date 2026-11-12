/* SPDX-License-Identifier: MIT */
/*
 * dec01_client.c -- DEC02: one H.264 decode session through DEC01's AP client
 * (ap/n2ap_vdec.c) on T02's fake hardware, with DEC02's n2tr recorder around it.
 *
 * This is the driver the package's page/cost documents are produced from since DEC01 landed.
 * It is deliberately thin: DEC01 owns the kernel protocol (QUERY_CAP/INIT/SET_PARAM/START/
 * RESET/DEINIT, the LOCK/UNLOCK/WAITISR/GET_FB/PUT_FB service), T02 owns the fake hardware,
 * V40 the /dev/vcu model, V41 the DMA tables; DEC02 only splits the Annex B stream into access
 * units, feeds them in, harvests output buffers, and records what reached a register.
 *
 * Output lines (parsed by tools/classify.py):
 *   DC QUERY_CAP rc=...            capability answer
 *   DC HEADER rc=... changed=... pic=WxH buf=WxH fb_sz=Y/C dpb=N fourcc=XXXX cpu=S
 *   DC START au=N bytes=N rc=N changed=0xN err=0xN cpu=S mmio r=N w=N bad=N lastbad=0xN
 *            gce=N/N disp=N freed=N
 *   DC RESET rc=...
 *   DC result frames_in=N displayed=N freed=N proto_errors=N
 *   DC reached VD-x
 * Last line: "DC finished" on success (exit 0) or "DC failed <what>" (exit 1).
 *
 * Host only: no phone, no vendor file is copied (they are read from --vendor at run time).
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n2a32.h"
#include "n2vc_hw.h"
#include "n2vhost.h"
#include "n2vcu.h"
#include <mtk_vdec_ipi.h>   /* DEC01's layout header (angle brackets: this directory has its own copy for vdec_probe.c) */
#include "n2ap_vdec.h"
#include "n2ap_venc.h"   /* n2vc_dma_guest_alloc / n2vc_dma_lookup_handle (V41) */
#include "n2tr.h"

extern unsigned n2vc_hw_fake_violations(const struct n2vc_hw *hw);
extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa,
                                     uint32_t bytes) __attribute__((weak));
extern void (*n2shim_log)(int prio, const char *tag, const char *text) __attribute__((weak));

#define NBS_MAX 2        /* the vendor copies the AU at START (list_free_bs within the same START), so 2 alternate */
#define NFB_MAX 16       /* dpb + 2, bounded by the client's own queue */
#define BS_BYTES (1u << 20)

struct bs_buf {
	uint32_t handle, pa, guest, share;
};

static struct n2tr *g_tr;
static unsigned g_log_lines;
static char g_last_log[512];

static void capture_log(int prio, const char *tag, const char *text)
{
	(void)prio;
	(void)tag;
	if (text == NULL)
		return;
	g_log_lines++;
	snprintf(g_last_log, sizeof g_last_log, "%s", text);
	if (getenv("N2DEC02_VERBOSE_LOG") != NULL)
		fprintf(stderr, "DC guest: %s\n", text);
}

static double pcpu(void)
{
	struct timespec t;

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static uint8_t *read_file(const char *path, size_t *bytes)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *p;

	if (f == NULL)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	p = malloc((size_t)n);
	if (p == NULL || fread(p, 1, (size_t)n, f) != (size_t)n) {
		free(p);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*bytes = (size_t)n;
	return p;
}

/* ---- Annex B access-unit splitter (H.264) -----------------------------------------------------
 * The rule the kernel's V4L2 layer and opus-dec1's reference driver use: parameter sets and SEI stay
 * with the following slice; a new access unit starts at a non-VCL NAL (6..9, 14..18) after a VCL NAL,
 * or at a VCL NAL (1/5) whose first_mb_in_slice == 0 (first payload bit 1) after a VCL NAL. */
static const uint8_t *next_sc(const uint8_t *p, const uint8_t *e)
{
	for (; p + 3 <= e; p++)
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	return e;
}

static size_t au_len(const uint8_t *b, size_t n)
{
	const uint8_t *e = b + n, *p = next_sc(b, e);
	int seen_vcl = 0;

	while (p < e) {
		const uint8_t *nal = p + 3;
		int t = nal < e ? (nal[0] & 0x1f) : 0;
		const uint8_t *start = (p > b && p[-1] == 0) ? p - 1 : p;
		int vcl = (t == 1 || t == 5);

		if (seen_vcl &&
		    ((!vcl && (t == 6 || t == 7 || t == 8 || t == 9 || (t >= 14 && t <= 18))) ||
		     (vcl && nal + 1 < e && (nal[1] & 0x80))))
			return (size_t)(start - b);
		if (vcl)
			seen_vcl = 1;
		p = next_sc(nal, e);
	}
	return n;
}

/* ---- output buffers ------------------------------------------------------------------------- */
struct fb_buf {
	struct n2ap_vdec_fb fb;
	uint32_t bytes[2];
	uint32_t pa[2];
};

int main(int argc, char **argv)
{
	const char *vendor = getenv("N2HW_VENDOR_DIR");
	const char *stream = NULL;
	const char *trace = getenv("N2DEC02_TRACE");
	unsigned maxf = 1000, w = 320, h = 240, ring = 0;
	int header_only = 0;
	struct n2vc_hw hw;
	struct n2vhost *host = NULL;
	struct n2vcu *vcu;
	const struct n2vc_hw *vhw;
	struct n2a32_mem *mem;
	struct n2ap_vdec *dec = NULL;
	struct n2ap_vdec_cfg cfg;
	struct n2ap_vdec_job job;
	struct n2ap_vdec_stats st;
	struct n2vcu_stats s0, s1;
	struct vdec_vsi *vsi;
	struct bs_buf bs[NBS_MAX];
	struct fb_buf fbs[NFB_MAX];
	unsigned nbs = 0, nfb = 0;
	uint8_t *data = NULL;
	size_t total = 0, off, n;
	unsigned frames_in = 0, displayed = 0, freed = 0;
	int busy[NFB_MAX];
	int rc, i, reached = 0, old_ok = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--vendor") == 0 && i + 1 < argc)
			vendor = argv[++i];
		else if (strcmp(argv[i], "--stream") == 0 && i + 1 < argc)
			stream = argv[++i];
		else if (strcmp(argv[i], "--maxframes") == 0 && i + 1 < argc)
			maxf = (unsigned)strtoul(argv[++i], NULL, 0);
		else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			unsigned ww, hh;

			if (sscanf(argv[++i], "%ux%u", &ww, &hh) != 2)
				return 2;
			w = ww;
			h = hh;
		} else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc)
			trace = argv[++i];
		else if (strcmp(argv[i], "--ring") == 0 && i + 1 < argc)
			ring = (unsigned)strtoul(argv[++i], NULL, 0);
		else if (strcmp(argv[i], "--header-only") == 0)
			header_only = 1;
		else {
			fprintf(stderr, "usage: %s --vendor DIR --stream FILE [--maxframes N] "
				"[--size WxH] [--trace FILE] [--ring N] [--header-only]\n", argv[0]);
			return 2;
		}
	}
	if (vendor == NULL || stream == NULL) {
		fprintf(stderr, "dec01_client: --vendor and --stream are required\n");
		return 2;
	}
	data = read_file(stream, &total);
	if (data == NULL) {
		fprintf(stderr, "dec01_client: cannot read %s\n", stream);
		return 2;
	}
	if (n2shim_log == NULL)
		n2shim_log = capture_log;

	memset(&hw, 0, sizeof hw);
	if (n2vc_hw_open_fake(&hw, getenv("VR_REC") ? getenv("VR_REC") : "/dev/null") != 0) {
		printf("DC failed: fake hardware open\n");
		return 1;
	}
	g_tr = n2tr_open(&hw, trace);
	if (g_tr == NULL) {
		printf("DC failed: n2tr_open\n");
		return 1;
	}
	rc = n2vhost_start(&host, n2tr_hw(g_tr), vendor);
	if (rc < 0) {
		printf("DC failed: n2vhost_start: %s\n", n2vhost_error(host));
		return 1;
	}
	if (rc > 0) {
		n2vhost_report(host, stdout);
		printf("DC failed: guest stopped during start\n");
		return 1;
	}
	vcu = n2vhost_vcu(host);
	vhw = n2vhost_hw(host);
	mem = n2vhost_mem(host);
	if (vcu == NULL || vhw == NULL || mem == NULL) {
		printf("DC failed: n2vhost pieces missing\n");
		return 1;
	}
	/* vpud spawns its IPI service threads while it starts; a message that arrives before an object
	 * handler exists is dropped (same settle as DEC01's probe and IV's runner). */
	{
		const char *s = getenv("N2VHOST_AP_DELAY_MS");
		unsigned ms = s != NULL && *s != '\0' ? (unsigned)strtoul(s, NULL, 10) : 1000u;
		struct timespec ts;

		ts.tv_sec = (time_t)(ms / 1000u);
		ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
		while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
			;
	}

	if (getenv("N2DEC02_LOGSET") != NULL)
		n2vcu_ap_set_log(vcu, getenv("N2DEC02_LOGSET"));
	memset(&cfg, 0, sizeof cfg);
	cfg.vcu = vcu;
	cfg.hw = vhw;
	cfg.codec_fourcc = N2AP_VDEC_FOURCC_H264;
	cfg.timeout_ms = 6000;
	cfg.pic_w = w;
	cfg.pic_h = h;
	cfg.set_decode_mode = 1;
	cfg.fb_num_planes = 2;
	cfg.ring_entries = ring;
	cfg.query_cap = 1;
	rc = n2ap_vdec_open(&dec, &cfg);
	if (rc != 0) {
		printf("DC failed: n2ap_vdec_open %d (%s); last vendor log: %s\n", rc,
		       n2ap_vdec_strerror(rc), g_last_log);
		n2vhost_report(host, stdout);
		return 1;
	}
	n2tr_mark(g_tr, "init");
	reached = 2;
	printf("DC INIT ok vsi=0x%08x ipi=%u\n", n2ap_vdec_vsi_addr(dec), n2ap_vdec_ipi_id(dec));

	/* One bitstream buffer per access unit: the client never reuses one while the daemon may still
	 * hold it, and this stream is short. */
	for (i = 0; i < NBS_MAX; i++) {
		rc = n2vc_dma_guest_alloc(vhw, mem, BS_BYTES, 0, &bs[i].handle, &bs[i].pa,
		                          &bs[i].guest);
		if (rc != 0 || bs[i].handle == 0) {
			printf("DC failed: bitstream buffer %d: %d\n", i, rc);
			goto out;
		}
		bs[i].share = 0x501u + (uint32_t)i;
		if (n2shim_ion_register_share != NULL) {
			int rr = n2shim_ion_register_share((int)bs[i].share, bs[i].handle, bs[i].pa,
			                                  BS_BYTES);

			if (rr != 0)
				printf("DC WARN ion share bs[%d] 0x%x -> %d\n", i, bs[i].share, rr);
		}
		nbs++;
	}

	/* Header pass (no output buffer: mtk_vcodec_dec.c:2212). */
	off = 0;
	n = au_len(data, total);
	memcpy(n2a32_ptr(mem, bs[0].guest), data, n);
	memset(&job, 0, sizeof job);
	job.bs_handle = bs[0].handle;
	job.bs_fd = (int32_t)bs[0].share;
	job.bs_bytes = BS_BYTES;
	job.bs_length = (uint32_t)n;
	n2tr_mark(g_tr, "header");
	{
		double c0 = pcpu();

		rc = n2ap_vdec_decode(dec, &job);
		vsi = n2ap_vdec_vsi(dec);
		printf("DC HEADER rc=%d changed=0x%x pic=%ux%u buf=%ux%u fb_sz=%u/%u dpb=%u "
		       "fourcc=%.4s cpu=%.3fs\n", rc,
		       vsi ? vsi->dec.vdec_changed_info : 0, vsi ? vsi->pic.pic_w : 0,
		       vsi ? vsi->pic.pic_h : 0, vsi ? vsi->pic.buf_w : 0,
		       vsi ? vsi->pic.buf_h : 0, vsi ? vsi->pic.fb_sz[0] : 0,
		       vsi ? vsi->pic.fb_sz[1] : 0, vsi ? vsi->dec.dpb_sz : 0,
		       vsi ? (char *)&vsi->pic.fourcc : "????", pcpu() - c0);
	}
	if (rc != 0 || (n2ap_vdec_changed(dec) & 1u) == 0)
		goto out;
	reached = 3;

	if (!header_only) {
		unsigned want;

		vsi = n2ap_vdec_vsi(dec);
		want = vsi->dec.dpb_sz + 2u;
		if (want > NFB_MAX)
			want = NFB_MAX;
		if (want < 2)
			want = 2;
		for (i = 0; i < (int)want; i++) {
			int p;

			memset(&fbs[i], 0, sizeof fbs[i]);
			for (p = 0; p < 2; p++) {
				uint32_t g = 0;

				fbs[i].bytes[p] = vsi->pic.fb_sz[p];
				if (fbs[i].bytes[p] == 0)
					continue;
				rc = n2vc_dma_guest_alloc(vhw, mem, fbs[i].bytes[p], 0,
				                          p == 0 ? &fbs[i].fb.handle : &fbs[i].fb.c_handle,
				                          &fbs[i].pa[p], &g);
				if (rc != 0) {
					printf("DC failed: fb %d plane %d: %d\n", i, p, rc);
					goto out;
				}
			}
			fbs[i].fb.bytes = fbs[i].bytes[0];
			fbs[i].fb.fd = (int32_t)(0x800u + (uint32_t)(i * 2));
			fbs[i].fb.c_fd = (int32_t)(0x800u + (uint32_t)(i * 2 + 1));
			fbs[i].fb.index = (uint32_t)i;
			busy[i] = 0;
			if (n2shim_ion_register_share != NULL) {
				int rr = n2shim_ion_register_share((int)fbs[i].fb.fd, fbs[i].fb.handle,
				                                   fbs[i].pa[0], fbs[i].bytes[0]);

				if (rr != 0)
					printf("DC WARN ion share fb[%d] 0x%x -> %d\n", i, fbs[i].fb.fd, rr);
				if (fbs[i].fb.c_handle != 0) {
					rr = n2shim_ion_register_share((int)fbs[i].fb.c_fd,
					                               fbs[i].fb.c_handle, fbs[i].pa[1],
					                               fbs[i].bytes[1]);
					if (rr != 0)
						printf("DC WARN ion share fb[%d]c 0x%x -> %d\n", i,
						       fbs[i].fb.c_fd, rr);
				}
			}
			rc = n2ap_vdec_push_fb(dec, &fbs[i].fb);
			if (rc != 0) {
				printf("DC failed: push_fb %d: %d\n", i, rc);
				goto out;
			}
			nfb++;
		}
		memset(busy, 0, sizeof busy);
		printf("DC BUFS bs=%u fb=%u fb_sz=%u/%u dpb=%u\n", nbs, nfb, fbs[0].bytes[0],
		       fbs[0].bytes[1], vsi->dec.dpb_sz);
		if (getenv("N2DEC02_DEBUG") != NULL)
			for (i = 0; i < (int)nfb; i++)
				printf("DC fb[%d] h=0x%x c=0x%x pa=0x%x/0x%x fd=0x%x\n", i,
				       fbs[i].fb.handle, fbs[i].fb.c_handle, fbs[i].pa[0],
				       fbs[i].pa[1], fbs[i].fb.fd);
	}

	/* Frame pass: the same first access unit is decoded again with an output buffer, exactly like
	 * the V4L2 core keeps the source buffer after the header pass (mtk_vcodec_dec.c). */
	off = 0;
	while (off < total && frames_in < maxf) {
		unsigned bi = frames_in % nbs;   /* alternate two buffers; NBS_MAX is small on purpose (ion table) */
		unsigned d0 = 0, f0 = 0;
		double c0;

		n = au_len(data + off, total - off);
		if (n > BS_BYTES) {
			printf("DC failed: AU %u is %zu bytes, buffer is %u\n", frames_in, n, BS_BYTES);
			goto out;
		}
		memcpy(n2a32_ptr(mem, bs[bi].guest), data + off, n);
		memset(&job, 0, sizeof job);
		job.bs_handle = bs[bi].handle;
		job.bs_fd = (int32_t)bs[bi].share;
		job.bs_bytes = BS_BYTES;
		job.bs_length = (uint32_t)n;
		job.timestamp = 1000u + frames_in;
		/* The kernel's vdec_if_decode(bs, fb) always carries an output buffer; without one the
		 * vendor treats the START as a header pass and never touches the core. */
		{
			int fi;

			for (fi = 0; fi < (int)nfb; fi++)
				if (!busy[fi])
					break;
			if (fi >= (int)nfb) {
				printf("DC failed: no free output buffer at au=%u\n", frames_in);
				goto out;
			}
			busy[fi] = 1;
			job.fb_handle = fbs[fi].fb.handle;
			job.fb_fd = fbs[fi].fb.fd;
			job.fb_bytes = fbs[fi].fb.bytes;
			job.fb_planes = 2;
			job.fb_c_handle = fbs[fi].fb.c_handle;
			job.fb_c_fd = fbs[fi].fb.c_fd;
			job.fb_c_bytes = fbs[fi].bytes[1];
			job.fb_index = fbs[fi].fb.index;
		}
		n2tr_mark(g_tr, "frame");
		{
			struct n2ap_vdec_out o;
			struct n2ap_vdec_freed f;

			n2vcu_stats(vcu, &s0);
			c0 = pcpu();
			rc = n2ap_vdec_decode(dec, &job);
			{
				double cpu = pcpu() - c0;

				n2vcu_stats(vcu, &s1);
				while (n2ap_vdec_pop_displayed(dec, &o) == 1)
					d0++;
				while (n2ap_vdec_pop_freed(dec, &f) == 1) {
					struct n2ap_vdec_fb got;
					int k;

					f0++;
					for (k = 0; k < (int)nfb; k++)
						if (fbs[k].fb.handle == (uint32_t)f.cookie ||
						    (uint64_t)fbs[k].fb.handle == f.cookie)
							busy[k] = 0;
					if (n2ap_vdec_fb_of(dec, f.cookie, &got) == 1)
						(void)n2ap_vdec_push_fb(dec, &got);
				}
				vsi = n2ap_vdec_vsi(dec);
				if (getenv("N2DEC02_DEBUG") != NULL) {
					struct n2ap_vdec_stats ss;

					n2ap_vdec_stats(dec, &ss);
					printf("DC DBG au=%u disp_cnt=%u ri=%u wi=%u st_disp=%u get_fb=%u put_fb=%u\n",
					       frames_in, vsi->list_disp.count, vsi->list_disp.read_idx,
					       vsi->list_disp.write_idx, ss.frames_displayed, ss.get_fb,
					       ss.put_fb);
				}
				printf("DC START au=%u bytes=%zu rc=%d changed=0x%x err=0x%x cpu=%.3fs "
				       "mmio r=%llu w=%llu bad=%llu lastbad=0x%llx gce=%llu/%llu "
				       "disp=%u freed=%u\n", frames_in, n, rc,
				       vsi ? vsi->dec.vdec_changed_info : 0,
				       vsi ? vsi->dec.error_map : 0, cpu,
				       (unsigned long long)(s1.mmio_reads - s0.mmio_reads),
				       (unsigned long long)(s1.mmio_writes - s0.mmio_writes),
				       (unsigned long long)(s1.mmio_bad - s0.mmio_bad),
				       (unsigned long long)s1.last_bad_mmio_addr,
				       (unsigned long long)(s1.gce_flushes - s0.gce_flushes),
				       (unsigned long long)(s1.gce_cmds - s0.gce_cmds), d0, f0);
			}
		}
		if (rc != 0)
			break;
		displayed += d0;
		freed += f0;
		if (reached < 4 && (s1.mmio_reads - s0.mmio_reads) + (s1.mmio_writes - s0.mmio_writes) > 0)
			reached = 4;
		if (reached < 5 && d0 > 0)
			reached = 5;
		if (reached < 6)
			reached = 6;
		off += n;
		frames_in++;
	}
	old_ok = frames_in > 0 && displayed > 0;

	if (!header_only) {
		struct n2ap_vdec_out o;

		rc = n2ap_vdec_flush(dec);
		printf("DC RESET rc=%d\n", rc);
		while (n2ap_vdec_pop_displayed(dec, &o) == 1)
			displayed++;
	}
out:
	n2ap_vdec_stats(dec, &st);
	printf("DC result frames_in=%u displayed=%u freed=%u proto_errors=%u locks=%u/%u "
	       "wait_isr=%u get_fb=%u put_fb=%u\n", frames_in, displayed, freed,
	       n2ap_vdec_proto_errors(dec), st.locks_core, st.locks_lat, st.wait_isr, st.get_fb,
	       st.put_fb);
	printf("DC fake: register violations=%u guest log lines=%u\n",
	       n2vc_hw_fake_violations(&hw), g_log_lines);
	n2tr_mark(g_tr, "deinit");
	(void)n2ap_vdec_close(dec);
	{
		struct n2vcu_stats vst;

		n2vcu_stats(vcu, &vst);
		printf("DC vcu-final: mmio_r=%llu mmio_w=%llu mmio_bad=%llu lastbad=0x%llx "
		       "gce_flushes=%llu gce_cmds=%llu unknown=%llu\n",
		       (unsigned long long)vst.mmio_reads, (unsigned long long)vst.mmio_writes,
		       (unsigned long long)vst.mmio_bad, (unsigned long long)vst.last_bad_mmio_addr,
		       (unsigned long long)vst.gce_flushes, (unsigned long long)vst.gce_cmds,
		       (unsigned long long)vst.unknown_requests);
	}
	n2tr_free(g_tr);
	g_tr = NULL;
	n2vhost_stop(host);
	free(data);
	if (!old_ok || reached < 6) {
		printf("DC failed: %s (frames_in=%u displayed=%u reached=%d)\n",
		       header_only ? "header-only run is not a decode" : "no picture displayed",
		       frames_in, displayed, reached);
		return 1;
	}
	printf("DC reached VD-%c\n", (char)('a' + reached));
	printf("DC finished\n");
	return 0;
}
