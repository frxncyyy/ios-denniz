/* SPDX-License-Identifier: MIT */
/*
 * probe_dec01.c -- host probe: the AP side of one MediaTek MT6893 decoder session against the phone's
 * own codec stack (bin/vpud + libvpud_vcodec.so + libvcodecdrv.so + libvcodec_utility.so + libvcodec_oal.so)
 * running under the AArch32 interpreter on T02's fake hardware.
 *
 * Owner: DEC01 (gpu/hwcodec/ap/test_dec01).  Host only: nothing here touches the phone, no vendor file is
 * copied, and every byte the vendor set needs is read at run time from the extracted /vendor
 * (default gpu/build/cam-vendor-20261109/vfs).
 *
 * Message sequence (facts/VDEC_PROTOCOL.md 2.1-2.3), each step a milestone:
 *   VD-a  AP_IPIMSG_DEC_QUERY_CAP on IPI_VDEC_COMMON answered; H264/H265 and NV12 in the lists
 *   VD-b  AP_IPIMSG_DEC_INIT answered, vsi mapped inside the DMEM, FRAME_SIZE/DECODE_MODE accepted
 *   VD-c  AP_IPIMSG_DEC_START accepted (the vendor's "get_ringbuf input handle NULL" is gone)
 *   VD-d  the first VDEC register access: a LOCK_CORE/LOCK_LAT from the daemon reached the hardware
 *   VD-e  the first WAITISR: the daemon waited for the decoder's done interrupt
 *   VD-f  a frame START failed (the first unmet need after VD-e)
 *
 * Usage:
 *   n2vdec_probe [--vendor DIR] [--size WxH] [--bitstream FILE] [--ring N] [--seconds N] [--quiet]
 * Environment: N2HW_VENDOR_DIR (overridden by --vendor), N2VHOST_LOG_INDEX as in host/n2vhost.c.
 *
 * Last line: "N2VDEC reached VD-x: ..." or "N2VDEC first-unmet VD-x: ...".
 */
#include <errno.h>
#include <pthread.h>
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
#include "mtk_vdec_ipi.h"
#include "n2ap_vdec.h"
#include "n2ap_venc.h"   /* n2vc_dma_guest_register, V41 */

/* T02's fake hardware extra controls (tests/hw). */
extern unsigned n2vc_hw_fake_violations(const struct n2vc_hw *hw);
extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa, uint32_t bytes)
	__attribute__((weak));
/* V33's log sink: the guest's __android_log_print lines arrive here. */
extern void (*n2shim_log)(int prio, const char *tag, const char *text) __attribute__((weak));

static int g_verbose = 1;
static char g_last_log[512];
static char g_ringbuf_log[512];
static unsigned g_ringbuf_lines;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;

	if (!g_verbose)
		return;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fflush(stdout);
}

static int g_verbose_log;
static unsigned g_log_lines;

static void capture_log(int prio, const char *tag, const char *text)
{
	(void)prio;
	(void)tag;
	if (text == NULL)
		return;
	g_log_lines++;
	snprintf(g_last_log, sizeof g_last_log, "%s", text);
	if (g_verbose_log)
		fprintf(stderr, "N2VDEC guest: %s\n", text);
	if (strstr(text, "ringbuf") != NULL) {
		g_ringbuf_lines++;
		snprintf(g_ringbuf_log, sizeof g_ringbuf_log, "%s", text);
	}
}

/* ---- the bitstream: Annex-B access units ----------------------------------------------------- */

struct probe_au {
	const uint8_t *p;
	uint32_t n;
};

#define NBS_PROBE 2u   /* the vendor copies the AU at START (DEC02: 2 is enough) */

/* Split an Annex-B stream into access units: a new AU starts at the second and later VCL NAL
 * (H.264 types 1-5, HEVC types 0-31); SPS/PPS/AUD stay with the following VCL.  The start code
 * belongs to the AU it introduces.  Returns the AU count (<= max). */
static unsigned split_aus(const uint8_t *b, uint32_t n, int hevc, struct probe_au *aus, unsigned max)
{
	uint32_t starts[1024];
	unsigned ns = 0, count = 0, k;
	int have_au = 0, have_vcl = 0;
	uint32_t au_start = 0;
	uint32_t i;

	for (i = 0; i + 3u < n && ns < 1024u; i++) {
		if (b[i] == 0u && b[i + 1u] == 0u && b[i + 2u] == 1u) {
			starts[ns++] = i;
			i += 2u;
		}
	}
	if (ns == 0u || max == 0u)
		return 0;
	for (k = 0; k < ns; k++) {
		uint32_t nal = starts[k] + 3u;
		uint32_t end = (k + 1u < ns) ? starts[k + 1u] : n;
		int type, vcl;

		if (nal >= end)
			continue;
		type = hevc ? (int)((b[nal] >> 1) & 0x3fu) : (int)(b[nal] & 0x1fu);
		vcl = hevc ? (type <= 31) : (type >= 1 && type <= 5);
		if (!have_au) {
			au_start = starts[k];
			have_au = 1;
		}
		if (vcl && have_vcl) {
			if (count == max)
				return count;
			aus[count].p = b + au_start;
			aus[count].n = starts[k] - au_start;
			count++;
			au_start = starts[k];
			have_vcl = 1;
		} else if (vcl) {
			have_vcl = 1;
		}
	}
	if (have_au && count < max) {
		aus[count].p = b + au_start;
		aus[count].n = n - au_start;
		count++;
	}
	return count;
}

/* ---- the bitstream --------------------------------------------------------------------------- */

static uint8_t *read_file(const char *path, uint32_t *bytes)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *p;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	p = malloc((size_t)n);
	if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
		free(p);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*bytes = (uint32_t)n;
	return p;
}

/* A minimal Annex-B stream (SPS, PPS, IDR) so the probe needs no encoder to exercise START.  This is a
 * 16x16 baseline stream: the first thing the vendor stack does with it is parse SPS, which is what the
 * first hardware access would be.  With --bitstream the file wins. */
static const uint8_t k_min_h264[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a, 0xf8, 0x41, 0xa2,
	0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80,
	0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x0a, 0xf2, 0x62, 0x80,
};

/* ---- the AP library's trace ------------------------------------------------------------------ */

static struct {
	unsigned locks, unlocks, wait_isr, get_fb, put_fb, hw_breaks, racing;
	unsigned first_lock_logged;
} g_ev;
static unsigned g_reached;   /* 'a'..'e' */
static unsigned g_frames_in, g_displayed;

static void ap_trace(void *arg, const char *event, uint32_t a, uint32_t b, uint32_t c)
{
	(void)arg;
	if (event == NULL)
		return;
	if (strcmp(event, "lock") == 0) {
		g_ev.locks++;
		if (!g_ev.first_lock_logged) {
			g_ev.first_lock_logged = 1;
			g_reached = 'd';
			say("N2VDEC reached VD-d: first VDEC access - LOCK_%s from the daemon answered "
			    "(block power + racing words)\n", a ? "CORE" : "LAT");
		}
		say("N2VDEC trace lock core=%u active=%u rc=%u\n", a, b, c);
	} else if (strcmp(event, "unlock") == 0) {
		g_ev.unlocks++;
		say("N2VDEC trace unlock core=%u active=%u rc=%u\n", a, b, c);
	} else if (strcmp(event, "wait_isr") == 0) {
		g_ev.wait_isr++;
		if (g_ev.wait_isr == 1 && g_reached < 'e')
			g_reached = 'e';
		say("N2VDEC trace wait_isr core=%u timeout=%u\n", a, b);
		if (g_reached == 'e' && g_ev.wait_isr == 1)
			say("N2VDEC reached VD-e: first WAITISR - the daemon waited for the VDEC %s interrupt\n",
			    a == 0 ? "CORE" : "LAT");
	} else if (strcmp(event, "get_fb") == 0) {
		g_ev.get_fb++;
		say("N2VDEC trace get_fb index=%u cookie=0x%x\n", a, c);
	} else if (strcmp(event, "put_fb") == 0) {
		g_ev.put_fb++;
		say("N2VDEC trace put_fb disp=%u free=%u\n", a, b);
	} else if (strcmp(event, "hw_break") == 0) {
		g_ev.hw_breaks++;
	} else if (strcmp(event, "racing_restore") == 0 ||
	           strcmp(event, "racing_save") == 0) {
		g_ev.racing++;
	} else if (strcmp(event, "power") == 0) {
		say("N2VDEC trace power blk=%u on=%u rc=%u\n", a, b, c);
	} else {
		say("N2VDEC trace %s a=%u b=%u c=%u\n", event, a, b, c);
	}
}

/* ---- capability list ------------------------------------------------------------------------- */

static void fourcc_str(uint32_t fourcc, char out[5])
{
	out[0] = (char)(fourcc & 0xff);
	out[1] = (char)((fourcc >> 8) & 0xff);
	out[2] = (char)((fourcc >> 16) & 0xff);
	out[3] = (char)((fourcc >> 24) & 0xff);
	out[4] = '\0';
}

int main(int argc, char **argv)
{
	const char *vendor = getenv("N2HW_VENDOR_DIR");
	const char *bs_path = NULL;
	const char *record = getenv("N2HW_RECORD");
	const char *codec = "h264";
	uint32_t fourcc = N2AP_VDEC_FOURCC_H264;
	int no_frame = 0, require_mm21 = 0, fake_irq = 0;
	unsigned w = 320, h = 240, seconds = 5, ring = 0, max_au_arg = 0;
	struct n2vc_hw hw;
	struct n2vhost *host = NULL;
	struct n2vcu *vcu;
	const struct n2vc_hw *vhw;
	struct n2ap_vdec *dec = NULL;
	struct n2ap_vdec_cfg cfg;
	int want_cap = 1;
	struct n2ap_vdec_job job;
	struct mtk_video_fmt *fmts = NULL;
	struct mtk_codec_framesizes *fsz = NULL;
	uint8_t *bs_data = (uint8_t *)k_min_h264;
	uint32_t bs_bytes = (uint32_t)sizeof(k_min_h264);
	struct probe_au aus[64];
	unsigned n_aus = 0;
	struct probe_bs {
		uint32_t handle, pa, guest, bytes;
		int32_t fd;
		uint64_t cookie;
		int busy;
	} bss[NBS_PROBE];
	unsigned nbs = 0;
	uint32_t inst = 0, dmem = 0, dmem_bytes = 0;
	int rc, i, have_h264 = 0, have_h265 = 0, have_nv12 = 0;
	char fc[5];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--vendor") == 0 && i + 1 < argc) {
			vendor = argv[++i];
		} else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			unsigned ww, hh;

			if (sscanf(argv[++i], "%ux%u", &ww, &hh) != 2 || ww == 0 || hh == 0) {
				fprintf(stderr, "probe: bad --size\n");
				return 2;
			}
			w = ww;
			h = hh;
		} else if (strcmp(argv[i], "--bitstream") == 0 && i + 1 < argc) {
			bs_path = argv[++i];
		} else if (strcmp(argv[i], "--codec") == 0 && i + 1 < argc) {
			codec = argv[++i];
		} else if (strcmp(argv[i], "--ring") == 0 && i + 1 < argc) {
			ring = (unsigned)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
			seconds = (unsigned)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--max-au") == 0 && i + 1 < argc) {
			max_au_arg = (unsigned)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
			record = argv[++i];
		} else if (strcmp(argv[i], "--no-frame") == 0) {
			no_frame = 1;
		} else if (strcmp(argv[i], "--no-cap") == 0) {
			want_cap = 0;
		} else if (strcmp(argv[i], "--require-mm21") == 0) {
			require_mm21 = 1;
		} else if (strcmp(argv[i], "--fake-irq") == 0) {
			fake_irq = 1;
		} else if (strcmp(argv[i], "--quiet") == 0) {
			g_verbose = 0;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("usage: %s [--vendor DIR] [--codec h264|h265|heif] [--size WxH] "
			       "[--bitstream FILE] [--ring N] [--seconds N] [--max-au N] [--no-frame] "
			       "[--require-mm21] [--fake-irq] [--quiet]\n", argv[0]);
			return 0;
		} else {
			fprintf(stderr, "probe: unknown argument %s\n", argv[i]);
			return 2;
		}
	}
	if (strcmp(codec, "h264") == 0 || strcmp(codec, "avc1") == 0) {
		fourcc = N2AP_VDEC_FOURCC_H264;
	} else if (strcmp(codec, "h265") == 0 || strcmp(codec, "hev1") == 0 ||
	           strcmp(codec, "hvc1") == 0) {
		fourcc = N2AP_VDEC_FOURCC_H265;
	} else if (strcmp(codec, "heif") == 0) {
		fourcc = N2AP_VDEC_FOURCC_HEIF;
	} else {
		fprintf(stderr, "probe: unknown --codec %s\n", codec);
		return 2;
	}
	if (bs_path) {
		uint8_t *p = read_file(bs_path, &bs_bytes);

		if (p == NULL) {
			fprintf(stderr, "probe: cannot read %s\n", bs_path);
			return 2;
		}
		bs_data = p;
	}
	{
		unsigned nau = split_aus(bs_data, bs_bytes, fourcc == N2AP_VDEC_FOURCC_H265,
		                         aus, (unsigned)(sizeof aus / sizeof aus[0]));

		if (nau == 0u) {
			printf("N2VDEC first-unmet VD-c: no Annex-B access unit in the bitstream\n");
			return 1;
		}
		n_aus = nau;
	}

	g_verbose_log = getenv("N2VDEC_VERBOSE_LOG") != NULL;
	if (n2shim_log == NULL)
		n2shim_log = capture_log;

	memset(&hw, 0, sizeof hw);
	if (n2vc_hw_open_fake(&hw, record) != 0) {
		printf("N2VDEC first-unmet VD-a: the fake hardware back end could not be opened\n");
		return 1;
	}
	rc = n2vhost_start(&host, &hw, vendor);
	if (rc < 0) {
		printf("N2VDEC first-unmet VD-a: n2vhost_start failed: %s\n", n2vhost_error(host));
		return 1;
	}
	if (rc > 0) {
		/* the guest stopped before its main loop: report why, then continue (the AP side may still
		 * have been served, and the report names the first unmet need) */
		n2vhost_report(host, stdout);
	}
	vcu = n2vhost_vcu(host);
	vhw = n2vhost_hw(host);
	if (vcu == NULL || vhw == NULL) {
		printf("N2VDEC first-unmet VD-a: the /dev/vcu model or the hardware wrapper is missing\n");
		return 1;
	}

	/* vpud creates its IPI service threads while it starts; a message that arrives before that is
	 * dropped with "obj handler is NULL" (IV measured the same for the encoder).  Give it a moment,
	 * like host/n2vhost.c's ap_settle(). */
	{
		const char *s = getenv("N2VHOST_AP_DELAY_MS");
		unsigned ms = s != NULL && *s != '\0' ? (unsigned)strtoul(s, NULL, 10) : 1000u;
		struct timespec ts;

		ts.tv_sec = (time_t)(ms / 1000u);
		ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
		while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
			;
	}

	/* VD-a: QUERY_CAP.  n2ap_vdec_open() sends it before INIT when cfg.query_cap is set; the list is
	 * printed here from a second query so the probe itself shows the vendor's answer. */
	fmts = calloc(MTK_MAX_DEC_CODECS_SUPPORT, sizeof *fmts);
	fsz = calloc(MTK_MAX_DEC_CODECS_SUPPORT, sizeof *fsz);
	if (fmts == NULL || fsz == NULL) {
		printf("N2VDEC first-unmet VD-a: out of memory\n");
		return 1;
	}
	memset(&cfg, 0, sizeof cfg);
	cfg.vcu = vcu;
	cfg.hw = vhw;
	cfg.codec_fourcc = fourcc;
	cfg.timeout_ms = 6000;
	cfg.pic_w = w;
	cfg.pic_h = h;
	cfg.decode_mode = 0;
	cfg.set_decode_mode = 1;
	cfg.fb_num_planes = 0;   /* codec default: 2 for H.264/H.265 (MM21), 1 for HEIF */
	cfg.output_fourcc = require_mm21 ? N2AP_VDEC_FOURCC_MM21 : 0u;
	cfg.fake_irq = fake_irq;
	cfg.ring_entries = ring;
	cfg.query_cap = want_cap;
	rc = n2ap_vdec_open(&dec, &cfg);
	if (rc != 0) {
		struct n2vcu_stats vst;

		n2vcu_stats(vcu, &vst);
		say("N2VDEC vcu: set_object=%llu get_object=%llu timeouts=%llu ipi_queued=%llu "
		    "ipi_overwritten=%llu acks=%llu mmaps=%llu\n",
		    (unsigned long long)vst.set_object, (unsigned long long)vst.get_object,
		    (unsigned long long)vst.get_object_timeouts, (unsigned long long)vst.ipi_queued,
		    (unsigned long long)vst.ipi_overwritten, (unsigned long long)vst.ipi_acks,
		    (unsigned long long)vst.mmaps);
		n2vhost_report(host, stdout);
		printf("N2VDEC first-unmet VD-a: n2ap_vdec_open returned %d (%s); last vendor log: %s\n",
		       rc, n2ap_vdec_strerror(rc), g_last_log);
		if (dec != NULL)
			printf("N2VDEC open detail: last_status=%d proto_errors=%u\n",
			       (int)n2ap_vdec_last_status(dec), n2ap_vdec_proto_errors(dec));
		return 1;
	}
	n2ap_vdec_set_trace(dec, ap_trace, NULL);
	rc = want_cap ? n2ap_vdec_query_cap(dec, GET_PARAM_CAPABILITY_SUPPORTED_FORMATS, fmts,
	                                    MTK_MAX_DEC_CODECS_SUPPORT * (uint32_t)sizeof *fmts) : -1;
	if (rc == 0) {
		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (fmts[i].fourcc == 0)
				continue;
			fourcc_str(fmts[i].fourcc, fc);
			say("N2VDEC cap fmt[%d] '%s' type=%u planes=%u\n", i, fc, fmts[i].type,
			    fmts[i].num_planes);
			if (strcmp(fc, "H264") == 0 && fmts[i].type == MTK_FMT_DEC)
				have_h264 = 1;
			if (strcmp(fc, "H265") == 0 && fmts[i].type == MTK_FMT_DEC)
				have_h265 = 1;
			if (strcmp(fc, "NV12") == 0)
				have_nv12 = 1;
		}
	}
	rc = want_cap ? n2ap_vdec_query_cap(dec, GET_PARAM_CAPABILITY_FRAME_SIZES, fsz,
	                                    MTK_MAX_DEC_CODECS_SUPPORT * (uint32_t)sizeof *fsz) : -1;
	if (rc == 0) {
		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (fsz[i].fourcc == 0)
				continue;
			fourcc_str(fsz[i].fourcc, fc);
			say("N2VDEC cap size '%s' profile=%u level=%u %ux%u step %ux%u\n", fc,
			    fsz[i].profile, fsz[i].level, fsz[i].stepwise.min_width,
			    fsz[i].stepwise.max_width, fsz[i].stepwise.step_width,
			    fsz[i].stepwise.step_height);
		}
	}
	if (!want_cap) {
		say("N2VDEC --no-cap: capability lists not queried\n");
	} else if (!have_h264 || !have_h265 || !have_nv12) {
		printf("N2VDEC first-unmet VD-a: capability list lacks %s%s%s\n",
		       have_h264 ? "" : "H264 ", have_h265 ? "" : "H265 ", have_nv12 ? "" : "NV12");
		goto out;
	}
	g_reached = 'a';
	inst = n2ap_vdec_vsi_addr(dec);
	if (n2vcu_dmem(vcu, &dmem, &dmem_bytes) != 0 || inst < dmem ||
	    (uint64_t)(inst - dmem) + 4096u > dmem_bytes) {
		printf("N2VDEC first-unmet VD-b: the vsi at 0x%08x is outside the %u-byte DMEM at 0x%08x\n",
		       inst, dmem_bytes, dmem);
		goto out;
	}
	g_reached = 'b';
	say("N2VDEC reached VD-b: QUERY_CAP + INIT + SET_PARAM answered for %s; vsi 0x%08x inside "
	    "dmem 0x%08x+%u\n", codec, inst, dmem, dmem_bytes);
	say("N2VDEC layout: sizeof(vsi)=%zu dec@%zu sizeof(dec)=%zu sizeof(pic)=%zu\n",
	    sizeof(struct vdec_vsi), offsetof(struct vdec_vsi, dec), sizeof(struct vdec_dec_info),
	    sizeof(struct vdec_pic_info));

	/* The bitstream: four DMA buffers with guest mirrors, cycled like the kernel's V4L2 source
	 * queue (the daemon reads through ion_mmap, so the bytes must be in the guest's own pages),
	 * each with its own ion share id and a distinct non-zero vdec_bs_va cookie. */
	{
		unsigned i;

		for (i = 0; i < NBS_PROBE; i++) {
			uint8_t *gp;

			rc = n2vc_dma_guest_alloc(vhw, n2vhost_mem(host), bs_bytes + 4096u, 0,
			                          &bss[i].handle, &bss[i].pa, &bss[i].guest);
			if (rc != 0 || bss[i].handle == 0u) {
				printf("N2VDEC first-unmet VD-c: n2vc_dma_guest_alloc(%u) returned %d\n",
				       bs_bytes + 4096u, rc);
				goto out;
			}
			gp = n2a32_ptr(n2vhost_mem(host), bss[i].guest);
			if (gp == NULL) {
				printf("N2VDEC first-unmet VD-c: the guest mirror 0x%08x is not mapped\n",
				       bss[i].guest);
				goto out;
			}
			bss[i].fd = 0x700 + (int32_t)i;
			bss[i].cookie = 0xB0000000u + i;
			bss[i].busy = 0;
			if (n2shim_ion_register_share != NULL) {
				rc = n2shim_ion_register_share(bss[i].fd, bss[i].handle, bss[i].pa,
				                               bs_bytes + 4096u);
				if (rc != 0) {
					say("N2VDEC note: bitstream share %d registration failed (%d)\n",
					    bss[i].fd, rc);
					if (vhw->dma_free != NULL)
						(void)vhw->dma_free(vhw->ctx, bss[i].handle);
					break;
				}
			} else if (i == 0u) {
				say("N2VDEC warning: V34's n2shim_ion_register_share is not in this build\n");
			}
			nbs++;
		}
	}
	if (nbs == 0u) {
		printf("N2VDEC first-unmet VD-c: no bitstream share could be registered\n");
		goto out;
	}
	/* VD-c: the first access unit, no output buffer (the kernel's header-decode call). */
	memset(&job, 0, sizeof job);
	memcpy(n2a32_ptr(n2vhost_mem(host), bss[0].guest), aus[0].p, aus[0].n);
	bss[0].busy = 1;
	job.bs_handle = bss[0].handle;
	job.bs_fd = bss[0].fd;
	job.bs_cookie = bss[0].cookie;
	job.bs_bytes = bs_bytes + 4096u;
	job.bs_length = aus[0].n;
	rc = n2ap_vdec_decode(dec, &job);
	{
		uint64_t cookie = 0;

		while (n2ap_vdec_pop_free_bs(dec, &cookie) == 1) {
			unsigned i;

			for (i = 0; i < nbs; i++)
				if (bss[i].cookie == cookie)
					bss[i].busy = 0;
		}
	}
	if (rc == 0) {
		g_reached = 'c';
		say("N2VDEC reached VD-c: DEC_START accepted, changed=0x%x (ringbuf log lines seen: %u)\n",
		    n2ap_vdec_changed(dec), g_ringbuf_lines);
	} else {
		printf("N2VDEC first-unmet VD-c: DEC_START returned %d (%s); ringbuf logs: %u; "
		       "vendor log lines: %u; last vendor log: %s\n", rc, n2ap_vdec_strerror(rc),
		       g_ringbuf_lines, g_log_lines, g_ringbuf_log[0] ? g_ringbuf_log : g_last_log);
		goto out;
	}

	/* Let the daemon send whatever decode requests it has; the handler answers them in the
	 * daemon's own thread, so the counters are the observable. */
	{
		struct timespec ts;

		ts.tv_sec = (time_t)(seconds > 0 ? seconds - 1 : 0);
		ts.tv_nsec = 200000000L;
		nanosleep(&ts, NULL);
	}
	{
		struct vdec_vsi *vsi = n2ap_vdec_vsi(dec);

		if (vsi != NULL) {
			struct vdec_vsi_tail *t = (struct vdec_vsi_tail *)
				((uint8_t *)vsi + 528u + 2u * 3088u);

			say("N2VDEC vsi after header: changed=0x%x dpb_sz=%u pic=%ux%u buf=%ux%u bitdepth=%u "
			    "fourcc=0x%08x fb_sz=%u/%u input_driven=%u\n",
			    t->dec.vdec_changed_info, t->dec.dpb_sz, t->pic.pic_w, t->pic.pic_h,
			    t->pic.buf_w, t->pic.buf_h, t->pic.bitdepth, t->pic.fourcc, t->pic.fb_sz[0],
			    t->pic.fb_sz[1], vsi->input_driven);
		}
	}
	say("N2VDEC counters: locks=%u unlocks=%u wait_isr=%u get_fb=%u put_fb=%u hw_breaks=%u\n",
	    g_ev.locks, g_ev.unlocks, g_ev.wait_isr, g_ev.get_fb, g_ev.put_fb, g_ev.hw_breaks);

	/* Stage 2: decode every access unit with a pool of output buffers, the way the kernel's
	 * worker does: one buffer in vsi.dec per START (synchronous mode; the vendor sends no
	 * GET_FRAME_BUFFER), harvest list_disp/list_free/list_free_bs after the ACK, return a buffer
	 * to the pool only when the API says it is no longer in flight.  This is the call that makes
	 * the daemon program the core (LOCK/WAITISR), so VD-d/VD-e happen here. */
	if (!no_frame) {
		struct probe_fb {
			uint32_t handle, pa, guest, bytes;
			uint32_t chandle, cpa, cguest, cbytes;
			int32_t fd, cfd;
			uint64_t cookie;
			int busy;
		} fbs[16];
		unsigned nfb = 0, i, au;
		uint32_t fb_bytes = w * h * 3u / 2u;
		uint32_t planes = 1;
		uint32_t fourcc_now = 0;
		struct vdec_vsi *vsi = n2ap_vdec_vsi(dec);
		unsigned max_au;

		if (vsi != NULL) {
			struct vdec_vsi_tail *t = (struct vdec_vsi_tail *)
				((uint8_t *)vsi + 528u + 2u * 3088u);
			uint64_t total = (uint64_t)t->pic.fb_sz[0] + t->pic.fb_sz[1];

			fourcc_now = t->pic.fourcc;
			if (t->pic.fb_sz[0] != 0u && total <= 64ull * 1024ull * 1024ull) {
				fb_bytes = (uint32_t)total;
				planes = t->pic.fb_sz[1] != 0u ? 2u : 1u;
			}
		}
		max_au = max_au_arg != 0u ? max_au_arg : n_aus;
		if (max_au > n_aus)
			max_au = n_aus;
		/* The ion table is a shared 64-slot budget (V34 N2AND_ION_BUFS; vpud takes ~34), so
		 * the pool is dpb+2 frame buffers x 2 planes + 2 bitstreams, like the reference client
		 * and DEC02's measured harness.  A failed registration stops the pool: an unregistered
		 * share id would make the vendor abandon the frame silently. */
		{
			unsigned want = 12u;

			if (vsi != NULL) {
				struct vdec_vsi_tail *t = (struct vdec_vsi_tail *)
					((uint8_t *)vsi + 528u + 2u * 3088u);

				if (t->dec.dpb_sz != 0u)
					want = t->dec.dpb_sz + 2u;
			}
			if (want > 16u)
				want = 16u;
			if (want < 4u)
				want = 4u;

			for (i = 0; i < want; i++) {
				uint32_t y_bytes = fb_bytes, c_bytes = 0;

				if (planes >= 2u) {
					y_bytes = vsi != NULL ?
					          ((struct vdec_vsi_tail *)((uint8_t *)vsi + 528u +
					           2u * 3088u))->pic.fb_sz[0] : fb_bytes / 3u * 2u;
					c_bytes = fb_bytes - y_bytes;
				}
				/* two allocations per buffer, like the reference client and the kernel's
				 * V4L2 planes: the vendor imports plane 1 by its own fd */
				rc = n2vc_dma_guest_alloc(vhw, n2vhost_mem(host), y_bytes, 0,
				                          &fbs[nfb].handle, &fbs[nfb].pa,
				                          &fbs[nfb].guest);
				if (rc != 0)
					break;
				fbs[nfb].bytes = y_bytes;
				fbs[nfb].fd = 0x800 + (int32_t)(nfb * 2);
				fbs[nfb].cookie = 0xF0000000u + nfb;   /* must not be a small DMA handle */
				fbs[nfb].busy = 0;
				if (n2shim_ion_register_share != NULL) {
					rc = n2shim_ion_register_share(fbs[nfb].fd, fbs[nfb].handle,
					                               fbs[nfb].pa, y_bytes);
					if (rc != 0) {
						say("N2VDEC note: share %d registration failed (%d); pool stops at "
						    "%u\n", fbs[nfb].fd, rc, nfb);
						if (vhw->dma_free != NULL)
							(void)vhw->dma_free(vhw->ctx, fbs[nfb].handle);
						break;
					}
				}
				if (planes >= 2u) {
					rc = n2vc_dma_guest_alloc(vhw, n2vhost_mem(host), c_bytes, 0,
					                          &fbs[nfb].chandle, &fbs[nfb].cpa,
					                          &fbs[nfb].cguest);
					if (rc != 0) {
						if (vhw->dma_free != NULL)
							(void)vhw->dma_free(vhw->ctx, fbs[nfb].handle);
						break;
					}
					fbs[nfb].cbytes = c_bytes;
					fbs[nfb].cfd = 0x801 + (int32_t)(nfb * 2);
					if (n2shim_ion_register_share != NULL) {
						rc = n2shim_ion_register_share(fbs[nfb].cfd, fbs[nfb].chandle,
						                               fbs[nfb].cpa, c_bytes);
						if (rc != 0) {
							say("N2VDEC note: share %d registration failed (%d); pool "
							    "stops at %u\n", fbs[nfb].cfd, rc, nfb);
							if (vhw->dma_free != NULL) {
								(void)vhw->dma_free(vhw->ctx, fbs[nfb].chandle);
								(void)vhw->dma_free(vhw->ctx, fbs[nfb].handle);
							}
							break;
						}
					}
				}
				nfb++;
			}
		}
		say("N2VDEC frame buffers: %u x %u bytes, %u plane(s), fourcc=0x%08x, %u AU(s)\n",
		    nfb, fb_bytes, planes, fourcc_now, max_au);
		if (nfb == 0u) {
			say("N2VDEC note: no output buffer could be allocated\n");
		}
		for (au = 0; au < max_au; au++) {
			struct n2ap_vdec_fb pf;
			unsigned slot = nfb, bslot = nbs;
			uint8_t *gp;

			for (i = 0; i < nbs; i++) {
				if (!bss[i].busy) {
					bslot = i;
					break;
				}
			}
			if (bslot == nbs) {
				say("N2VDEC note: AU %u has no free bitstream buffer\n", au);
				break;
			}
			for (i = 0; i < nfb; i++) {
				if (!fbs[i].busy) {
					slot = i;
					break;
				}
			}
			if (slot == nfb) {
				say("N2VDEC note: AU %u needs a buffer still referenced by the DPB\n", au);
				break;
			}
			gp = n2a32_ptr(n2vhost_mem(host), bss[bslot].guest);
			if (gp == NULL) {
				say("N2VDEC note: the bitstream guest mirror is not mapped\n");
				break;
			}
			memcpy(gp, aus[au].p, aus[au].n);
			memset(&pf, 0, sizeof pf);
			pf.handle = fbs[slot].handle;
			pf.fd = fbs[slot].fd;
			pf.bytes = fbs[slot].bytes;
			pf.index = slot;
			pf.c_handle = planes >= 2u ? fbs[slot].chandle : 0u;
			pf.c_fd = fbs[slot].cfd;
			pf.cookie = fbs[slot].cookie;
			job.fb_handle = pf.handle;
			job.fb_fd = pf.fd;
			job.fb_bytes = pf.bytes;
			job.fb_planes = planes;
			job.fb_c_handle = pf.c_handle;
			job.fb_c_fd = pf.c_fd;
			job.fb_cookie = pf.cookie;
			job.fb_index = pf.index;
			job.bs_handle = bss[bslot].handle;
			job.bs_fd = bss[bslot].fd;
			job.bs_cookie = bss[bslot].cookie;
			job.bs_bytes = bs_bytes + 4096u;
			job.bs_length = aus[au].n;
			job.timestamp = 0x1000u + au;
			if (getenv("N2VDEC_DEBUG") != NULL)
				say("N2VDEC au=%u slot=%u bs=%u cookie=0x%llx fb=0x%x fd=%d "
				    "c=0x%x cfd=%d planes=%u\n", au, slot, job.bs_handle,
				    (unsigned long long)job.fb_cookie, job.fb_handle, (int)job.fb_fd,
				    job.fb_c_handle, (int)job.fb_c_fd, job.fb_planes);
			fbs[slot].busy = 1;
			bss[bslot].busy = 1;
			rc = n2ap_vdec_decode(dec, &job);
			if (rc != 0) {
				printf("N2VDEC first-unmet VD-f: frame START %u returned %d (%s); last "
				       "vendor log: %s\n", au, rc, n2ap_vdec_strerror(rc), g_last_log);
				fbs[slot].busy = 0;
				bss[bslot].busy = 0;
				break;
			}
			/* the daemon returned the consumed input buffer: mark it free */
			{
				uint64_t cookie = 0;

				while (n2ap_vdec_pop_free_bs(dec, &cookie) == 1) {
					unsigned k;

					for (k = 0; k < nbs; k++)
						if (bss[k].cookie == cookie)
							bss[k].busy = 0;
				}
			}
			/* the API's in-use set decides when a buffer is reusable: list_free and
			 * list_disp must both have returned it (B-frames can reorder them) */
			for (i = 0; i < nfb; i++) {
				struct n2ap_vdec_fb got;

				if (n2ap_vdec_fb_of(dec, fbs[i].cookie, &got) == 0)
					fbs[i].busy = 0;
			}
			{
				struct n2ap_vdec_out o;
				unsigned nd = 0;

				while (n2ap_vdec_pop_displayed(dec, &o) == 1)
					nd++;
				g_frames_in++;
				g_displayed += nd;
			}
		}
		/* EOS: the DPB still holds the reordered tail; RESET flushes it and the harvest collects
		 * the last list_disp entries (the kernel does this from vdec_if_decode(bs == NULL)). */
		if (g_frames_in != 0u) {
			struct n2ap_vdec_out o;
			unsigned nd = 0;

			rc = n2ap_vdec_flush(dec);
			if (rc != 0)
				say("N2VDEC note: flush returned %d (%s)\n", rc, n2ap_vdec_strerror(rc));
			while (n2ap_vdec_pop_displayed(dec, &o) == 1)
				nd++;
			g_displayed += nd;
			if (nd != 0u)
				say("N2VDEC flush: %u more displayed picture(s)\n", nd);
		}
		say("N2VDEC frames: in=%u displayed=%u\n", g_frames_in, g_displayed);
	}
	{
		struct n2ap_vdec_out o;
		struct n2ap_vdec_freed fr;
		uint64_t cookie = 0;
		unsigned nd = 0, nf = 0, nb = 0;

		while (n2ap_vdec_pop_displayed(dec, &o) == 1) {
			if (nd == 0u)
				say("N2VDEC first display: cookie=0x%llx poc=%d ts=0x%llx y=0x%llx c=0x%llx\n",
				    (unsigned long long)o.cookie, (int)o.poc,
				    (unsigned long long)o.timestamp, (unsigned long long)o.y_dma,
				    (unsigned long long)o.c_dma);
			nd++;
		}
		while (n2ap_vdec_pop_freed(dec, &fr) == 1)
			nf++;
		while (n2ap_vdec_pop_free_bs(dec, &cookie) == 1)
			nb++;
		say("N2VDEC harvest: displayed=%u freed=%u bs_free=%u (frames only with --fake-irq on "
		    "the fake)\n", nd, nf, nb);
	}
	{
		struct n2ap_vdec_stats st;

		n2ap_vdec_stats(dec, &st);
		say("N2VDEC stats: locks core=%u lat=%u, power on=%u off=%u, racing restores=%u saves=%u, "
		    "hw_breaks=%u timeouts=%u, wait_isr=%u (%u timeouts), get_fb=%u (%u empty), put_fb=%u, "
		    "displayed=%u freed=%u syncs=%u proto_errors=%u\n",
		    st.locks_core, st.locks_lat, st.power_on, st.power_off, st.racing_restores,
		    st.racing_saves, st.hw_breaks, st.hw_break_timeouts, st.wait_isr,
		    st.wait_isr_timeouts, st.get_fb, st.get_fb_empty, st.put_fb, st.frames_displayed,
		    st.fbs_freed, st.syncs, n2ap_vdec_proto_errors(dec));
	}
	{
		unsigned viol = n2vc_hw_fake_violations(&hw);

		say("N2VDEC fake: register violations=%u%s\n", viol, viol != 0u ?
		    " (expected: the frozen window table puts VDEC_RACING in the CORE block, so the "
		    "first-lock racing restore during a LAT-only period is refused with EACCES; see "
		    "ap/VDEC_FLOW.md section 5)" : "");
	}

out:
	if (dec != NULL)
		(void)n2ap_vdec_close(dec);
	{
		struct n2vcu_stats vst;

		n2vcu_stats(vcu, &vst);
		say("N2VDEC vcu-final: set_object=%llu get_object=%llu allocations=%llu gce_flushes=%llu "
		    "mmaps=%llu mmio_r=%llu mmio_w=%llu unknown=%llu\n",
		    (unsigned long long)vst.set_object, (unsigned long long)vst.get_object,
		    (unsigned long long)vst.allocations, (unsigned long long)vst.gce_flushes,
		    (unsigned long long)vst.mmaps, (unsigned long long)vst.mmio_reads,
		    (unsigned long long)vst.mmio_writes, (unsigned long long)vst.unknown_requests);
	}
	n2vhost_stop(host);
	free(fmts);
	free(fsz);
	if (g_reached >= 'c') {
		printf("N2VDEC reached VD-%c\n", (char)g_reached);
		return 0;
	}
	return 1;
}
