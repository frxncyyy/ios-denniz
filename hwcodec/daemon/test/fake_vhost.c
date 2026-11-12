/* SPDX-License-Identifier: MIT */
/*
 * fake_vhost.c -- host-test model of the two packages vcodecd depends on, with their REAL interfaces:
 *
 *   IV  host/n2vhost.h:  n2vhost_start / vcu / hw / mem / elf / join / stop_text / stop
 *   V41 ap/n2ap_venc.h:  n2ap_venc_open / start / set_param / encode / wait_done / close +
 *                        n2ap_enc_param_1080p30_{h264,hevc}
 *
 * This is a MODEL, not a stub of the real work: it omits the interpreter and the vendor library on purpose (the vendor blobs
 * stay where they are) and reproduces the contract vcodecd relies on:
 *   - a guest stop is reported by n2vhost_join(h, 0) == 0 + n2vhost_stop_text();
 *   - encode writes one canned Annex B access unit into the bit-stream DMA handle and reports its size and key flag;
 *   - the separate sequence-header job (N2AP_ENC_SEQ_HDR) writes VPS/SPS/PPS and appends a line to $N2IP_FAKE_DIR/seqhdr.log;
 *   - with a file named `async` present, encode returns 0 bytes and wait_done() supplies size/key, like the real GCE path;
 *   - parameter-set calls succeed, runtime bitrate/framerate/force-intra are accepted;
 *   - every call is bounded and every error is an -errno.
 *
 * Control files (all under $N2IP_FAKE_DIR, default /tmp/n2hw-ip/fake):
 *   stop        present: the guest stopped; its integer is the n2a32 stop code (2 UNDEF, 3 FAULT, 8 NOIMPORT)
 *   hang_ms     present: encode sleeps that many milliseconds before doing anything (a hung hardware job)
 *   async       present: encode answers 0 bytes; n2ap_venc_wait_done() completes the job
 *   wait_hang_ms present: wait_done sleeps and returns -ETIMEDOUT
 *   wait_fail   present: wait_done returns -<value>
 *   strip_params present: a key AU is written without its leading SPS/PPS (so the daemon must prepend its cache)
 * Stream: $N2IP_FAKE_STREAM, records [u32 len][u32 key][len bytes], made by split_au.py from an ffmpeg H.264 file.
 * Input proof: if $N2IP_FAKE_INPUT_SEEN is set, encode appends the first 16 bytes of the input buffer and their sum.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2ap_venc.h"
#include "n2vc_hw.h"
#include "n2vhost.h"

#define FAKE_AU_MAX 64u

struct n2vhost {
	int started;
	const struct n2vc_hw *hw;
	char vendor[256];
};

struct n2vcu {
	int unused;
};

struct fake_au {
	uint8_t *data;
	uint32_t len;
	uint32_t key;
};

struct n2ap_venc {
	const struct n2vc_hw *hw;
	uint32_t fourcc;
	struct n2ap_enc_param p;
	uint32_t frame;
	uint32_t force_key;
	struct fake_au au[FAKE_AU_MAX];
	uint32_t naus;
	uint32_t pending_bytes;             /* async completion, handed over by n2ap_venc_wait_done */
	uint32_t pending_key;
};

static const char *
fake_dir(void)
{
	const char *d = getenv("N2IP_FAKE_DIR");
	return d && *d ? d : "/tmp/n2hw-ip/fake";
}

static int
fake_file_exists(const char *name)
{
	char path[512];
	int n = snprintf(path, sizeof(path), "%s/%s", fake_dir(), name);
	return n > 0 && (size_t)n < sizeof(path) && access(path, F_OK) == 0;
}

/* H.264 Annex B: length of the leading SPS/PPS prefix, 0 if the unit does not start with parameter sets. */
static uint32_t
strip_param_prefix(const uint8_t *p, uint32_t n)
{
	uint32_t off = 0;
	int saw = 0;
	while (off + 4u <= n) {
		uint32_t sc = 0;
		if (p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 1) { sc = 3; }
		else if (p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 0 && p[off + 3] == 1) { sc = 4; }
		if (sc == 0) { return 0; }
		uint32_t nal = off + sc;
		if (nal >= n) { return 0; }
		unsigned type = p[nal] & 0x1fu;
		if (type != 7u && type != 8u) { break; }
		saw = 1;
		uint32_t next = nal + 1u;
		while (next + 3u <= n) {
			if (p[next] == 0 && p[next + 1] == 0 && p[next + 2] == 1) { break; }
			if (next + 4u <= n && p[next] == 0 && p[next + 1] == 0 && p[next + 2] == 0 && p[next + 3] == 1) { break; }
			next++;
		}
		if (next + 3u > n) { return 0; }
		off = next;
	}
	return saw ? off : 0;
}

static int
fake_read_int(const char *name, int *out)
{
	char path[512];
	int n = snprintf(path, sizeof(path), "%s/%s", fake_dir(), name);
	if (n <= 0 || (size_t)n >= sizeof(path)) { return -1; }
	FILE *f = fopen(path, "r");
	if (!f) { return -1; }
	int v = 0;
	if (fscanf(f, "%d", &v) != 1) { v = 0; }
	(void)fclose(f);
	*out = v;
	return 0;
}

/* Test trigger: if `name` exists, write the guest-stop file `code` and report 1.  This lets a test stop the guest
 * from inside an AP call, so the daemon sees it while the request is in flight instead of racing its watchdog. */
static int
fake_trigger_stop(const char *name, int code)
{
	char path[512];
	int n = snprintf(path, sizeof(path), "%s/%s", fake_dir(), name);
	if (n <= 0 || (size_t)n >= sizeof(path) || access(path, F_OK) != 0) { return 0; }
	n = snprintf(path, sizeof(path), "%s/stop", fake_dir());
	if (n <= 0 || (size_t)n >= sizeof(path)) { return 0; }
	FILE *f = fopen(path, "w");
	if (!f) { return 0; }
	fprintf(f, "%d\n", code);
	(void)fclose(f);
	return 1;
}

static void
fake_sleep_ms(long ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

/* ---- IV ------------------------------------------------------------------------------------------------------- */

int
n2vhost_start(struct n2vhost **out, const struct n2vc_hw *hw, const char *vendor_dir)
{
	if (!out || !hw || !vendor_dir || !*vendor_dir) { return -1; }
	if (access(vendor_dir, F_OK) != 0) { return -1; }
	struct n2vhost *vh = (struct n2vhost *)calloc(1, sizeof(*vh));
	if (!vh) { return -1; }
	vh->started = 1;
	vh->hw = hw;
	(void)snprintf(vh->vendor, sizeof(vh->vendor), "%s", vendor_dir);
	*out = vh;
	return 0;
}

struct n2vcu *
n2vhost_vcu(struct n2vhost *vh)
{
	return vh ? (struct n2vcu *)vh : NULL;
}

const struct n2vc_hw *
n2vhost_hw(struct n2vhost *vh)
{
	return vh ? vh->hw : NULL;
}

struct n2a32_mem *
n2vhost_mem(struct n2vhost *vh)
{
	(void)vh;
	return NULL;                         /* the fake has no guest address space */
}

struct n2elf_set *
n2vhost_elf(struct n2vhost *vh)
{
	(void)vh;
	return NULL;                         /* no loaded images, so the daemon's describe is "no image" */
}

/* n2elf_describe is loader code, not part of the fake host set; a test-only definition keeps the fatal-stop line
 * testable without pulling the loader into the daemon test build. */
const char *
n2elf_describe(struct n2elf_set *s, uint32_t addr, char *buf, size_t len)
{
	(void)s;
	if (buf && len) { (void)snprintf(buf, len, "fake:0x%08x", addr); }
	return buf;
}

unsigned
n2vhost_unresolved(const struct n2vhost *vh)
{
	(void)vh;
	return 0;
}

unsigned
n2vhost_milestone(const struct n2vhost *vh)
{
	(void)vh;
	int ms = N2VHOST_MS_B;
	(void)fake_read_int("milestone", &ms);
	if (ms < N2VHOST_MS_NONE || ms > N2VHOST_MS_F) { ms = N2VHOST_MS_B; }
	return (unsigned)ms;
}

const char *
n2vhost_milestone_name(unsigned ms)
{
	switch (ms) {
	case N2VHOST_MS_A: return "M-a";
	case N2VHOST_MS_B: return "M-b";
	case N2VHOST_MS_C: return "M-c";
	case N2VHOST_MS_D: return "M-d";
	case N2VHOST_MS_E: return "M-e";
	case N2VHOST_MS_F: return "M-f";
	default: return "none";
	}
}

const char *
n2vhost_milestone_evidence(const struct n2vhost *vh, unsigned ms)
{
	(void)vh;
	(void)ms;
	return "fake milestone evidence";
}

int
n2vhost_report(struct n2vhost *vh, FILE *out)
{
	(void)vh;
	if (out) {
		fprintf(out, "N2VHOST tables string=1(fake) heap=1(fake) thread=1(fake) stdio=1(fake) android=1(fake) data=1(fake)\n");
		fprintf(out, "N2VHOST reached M-f: fake milestone evidence\n");
	}
	return N2VHOST_MS_F;
}

int
n2vhost_join(struct n2vhost *vh, unsigned timeout_ms)
{
	(void)timeout_ms;
	if (!vh) { return -1; }
	int code = 0;
	return fake_read_int("stop", &code) == 0 ? 0 : -1;
}

const char *
n2vhost_stop_text(struct n2vhost *vh)
{
	(void)vh;
	int code = 0;
	if (fake_read_int("stop", &code) != 0) { return "running or not started"; }
	switch (code) {
	case N2A32_STOP_UNDEF: return "UNDEF 0xe7f001f0 at 0x00001000";
	case N2A32_STOP_FAULT: return "FAULT at 0x00002000";
	case N2A32_STOP_NOIMPORT: return "NOIMPORT fake_import at 0x00003000";
	default: return "fake guest stop";
	}
}

enum n2a32_stop
n2vhost_stop_reason(const struct n2vhost *vh)
{
	(void)vh;
	int code = 0;
	if (fake_read_int("stop", &code) != 0) { return N2A32_STOP_NONE; }
	if (code < N2A32_STOP_NONE || code > N2A32_STOP_NOIMPORT) { return N2A32_STOP_NONE; }
	return (enum n2a32_stop)code;
}

const char *
n2vhost_error(struct n2vhost *vh)
{
	(void)vh;
	return "";
}

void
n2vhost_stop(struct n2vhost *vh)
{
	if (!vh) { return; }
	vh->started = 0;
	free(vh);
}

/* ---- V41 ------------------------------------------------------------------------------------------------------ */

static void
free_stream(struct n2ap_venc *e)
{
	for (uint32_t i = 0; i < e->naus; i++) { free(e->au[i].data); }
	e->naus = 0;
}

static int
load_stream(struct n2ap_venc *e, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) { return -ENOENT; }
	for (;;) {
		uint32_t hdr[2];
		size_t n = fread(hdr, 1, sizeof(hdr), f);
		if (n == 0) { break; }
		if (n != sizeof(hdr) || hdr[0] == 0 || hdr[0] > (16u << 20) || e->naus >= FAKE_AU_MAX) {
			(void)fclose(f);
			free_stream(e);
			return -EINVAL;
		}
		uint8_t *data = (uint8_t *)malloc(hdr[0]);
		if (!data) {
			(void)fclose(f);
			free_stream(e);
			return -ENOMEM;
		}
		if (fread(data, 1, hdr[0], f) != hdr[0]) {
			free(data);
			(void)fclose(f);
			free_stream(e);
			return -EINVAL;
		}
		e->au[e->naus].data = data;
		e->au[e->naus].len = hdr[0];
		e->au[e->naus].key = hdr[1];
		e->naus++;
	}
	(void)fclose(f);
	return e->naus ? 0 : -EINVAL;
}

int
n2ap_venc_open(struct n2ap_venc **out, struct n2vcu *vcu, const struct n2vc_hw *hw, uint32_t codec_fourcc,
               unsigned timeout_ms)
{
	(void)timeout_ms;
	if (!out || !vcu || !hw) { return -EINVAL; }
	if (codec_fourcc != N2AP_FOURCC_H264 && codec_fourcc != N2AP_FOURCC_H265) { return -EINVAL; }
	const char *stream = getenv("N2IP_FAKE_STREAM");
	if (!stream || !*stream) { return -ENOENT; }
	struct n2ap_venc *e = (struct n2ap_venc *)calloc(1, sizeof(*e));
	if (!e) { return -ENOMEM; }
	e->hw = hw;
	e->fourcc = codec_fourcc;
	int r = load_stream(e, stream);
	if (r != 0) {
		free(e);
		return r;
	}
	/* V41 references both VENC power blocks for the lifetime of a session (mt6885/mtk_vcodec_enc_pm.c:273-279). */
	if (hw->power) {
		if (hw->power(hw->ctx, RWVC_BLK_VENC, 1) != 0 ||
		    hw->power(hw->ctx, RWVC_BLK_VENC_C1, 1) != 0) {
			(void)hw->power(hw->ctx, RWVC_BLK_VENC, 0);
			free_stream(e);
			free(e);
			return -EIO;
		}
	}
	*out = e;
	return 0;
}

void
n2ap_enc_param_1080p30_h264(struct n2ap_enc_param *p)
{
	if (!p) { return; }
	memset(p, 0, sizeof(*p));
	p->input_yuv_fmt = N2AP_YUV_NV12;
	p->width = 1920;
	p->height = 1080;
	p->buf_width = 1920;
	p->buf_height = 1088;
	p->frm_rate = 30;
	p->bitrate = 20000000;
	p->i_qp = 51;
	p->p_qp = 51;
	p->b_qp = 51;
	p->max_qp = 51;
	p->seq_hdr_mode = 0;
}

void
n2ap_enc_param_1080p30_hevc(struct n2ap_enc_param *p)
{
	if (!p) { return; }
	n2ap_enc_param_1080p30_h264(p);
	p->profile = 0;                     /* V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN */
	p->level = 5;                       /* V4L2_MPEG_VIDEO_HEVC_LEVEL_4 */
	p->tier = 0;
}

int
n2ap_venc_start(struct n2ap_venc *e, struct n2ap_enc_param *p)
{
	if (!e || !p || p->width == 0 || p->height == 0) { return -EINVAL; }
	e->p = *p;
	const char *params = getenv("N2IP_FAKE_PARAMS");
	if (params && *params) {
		FILE *f = fopen(params, "a");
		if (f) {
			fprintf(f, "fourcc=%u profile=%u level=%u width=%u height=%u fps=%u bitrate=%u gop=%u\n",
			        e->fourcc, p->profile, p->level, p->width, p->height, p->frm_rate, p->bitrate, p->gop_size);
			(void)fclose(f);
		}
	}
	return 0;
}

int
n2ap_venc_set_param(struct n2ap_venc *e, unsigned type, struct n2ap_enc_param *p)
{
	if (!e) { return -EINVAL; }
	if (fake_trigger_stop("stop_on_set", N2A32_STOP_UNDEF)) { return -EIO; }
	int stop = 0;
	if (fake_read_int("stop", &stop) == 0) { return -EIO; }
	int fail = 0;
	if (fake_read_int("set_fail", &fail) == 0 && fail > 0) {
		int want = fail == 1 ? (int)VENC_SET_PARAM_ADJUST_BITRATE
		         : fail == 2 ? (int)VENC_SET_PARAM_FORCE_INTRA : -1;
		if (want < 0 || (unsigned)want == type) {
			return fail == 1 ? -ETIMEDOUT : -EPROTO;
		}
	}
	switch (type) {
	case VENC_SET_PARAM_FORCE_INTRA:
		e->force_key = 1;
		return 0;
	case VENC_SET_PARAM_PREPEND_SPSPPS_TO_IDR:
		if (!p) { return -EINVAL; }
		e->p.prependheader = p->prependheader;
		return 0;
	case VENC_SET_PARAM_ADJUST_BITRATE:
		if (!p) { return -EINVAL; }
		e->p.bitrate = p->bitrate;
		return 0;
	case VENC_SET_PARAM_ADJUST_FRAMERATE:
		if (!p) { return -EINVAL; }
		e->p.frm_rate = p->frm_rate;
		return 0;
	case VENC_SET_PARAM_ENC:
		if (!p) { return -EINVAL; }
		e->p = *p;
		return 0;
	default:
		return -EINVAL;
	}
}

int
n2ap_venc_encode(struct n2ap_venc *e, uint32_t frame_handle, uint32_t bs_handle, unsigned flags,
                 struct n2ap_enc_result *res)
{
	if (!e || !res || bs_handle == 0) { return -EINVAL; }
	if ((flags & 0x3u) == N2AP_ENC_SEQ_HDR) {
		/* The separate sequence-header job: the vendor writes VPS/SPS/PPS (no picture).  The model emits the
		 * parameter-set prefix of its first key AU (H.264) or a small valid VPS/SPS/PPS (HEVC). */
		if (fake_trigger_stop("stop_on_encode", N2A32_STOP_UNDEF)) { return -EIO; }
		uint8_t hdr[1024];
		uint32_t len = 0;
		if (e->fourcc == N2AP_FOURCC_H265) {
			static const uint8_t hevc[] = {
				0x00, 0x00, 0x00, 0x01, 0x40, 0x01,      /* VPS */
				0x00, 0x00, 0x00, 0x01, 0x42, 0x01,      /* SPS */
				0x00, 0x00, 0x00, 0x01, 0x44, 0x01       /* PPS */
			};
			if (sizeof(hevc) > sizeof(hdr)) { return -EIO; }
			memcpy(hdr, hevc, sizeof(hevc));
			len = (uint32_t)sizeof(hevc);
		} else {
			for (uint32_t i = 0; i < e->naus; i++) {
				if (e->au[i].key) {
					len = strip_param_prefix(e->au[i].data, e->au[i].len);
					if (len > sizeof(hdr)) { len = 0; }
					if (len) { memcpy(hdr, e->au[i].data, len); }
					break;
				}
			}
		}
		if (len == 0u) { return -EIO; }
		if (e->hw->dma_rw(e->hw->ctx, bs_handle, 0, hdr, len, 1) != 0) { return -EIO; }
		memset(res, 0, sizeof(*res));
		res->bytes = len;
		res->is_key_frm = 0;
		char path[512];
		int n = snprintf(path, sizeof(path), "%s/seqhdr.log", fake_dir());
		if (n > 0 && (size_t)n < sizeof(path)) {
			FILE *f = fopen(path, "a");
			if (f) {
				fprintf(f, "fourcc=%u bytes=%u\n", e->fourcc, len);
				(void)fclose(f);
			}
		}
		return 0;
	}
	if (frame_handle == 0) { return -EINVAL; }
	if ((flags & 0x3u) != N2AP_ENC_FRAME) { return -EINVAL; }
	if (fake_trigger_stop("stop_on_encode", N2A32_STOP_UNDEF)) { return -EIO; }
	int hang = 0;
	if (fake_read_int("hang_ms", &hang) == 0 && hang > 0) {
		fake_sleep_ms(hang);
		return -ETIMEDOUT;              /* what V41 returns when its bounded wait expires */
	}
	int enc_err = 0;
	if (fake_read_int("enc_err", &enc_err) == 0 && enc_err != 0) {
		return enc_err < 0 ? enc_err : -enc_err;
	}
	int stop = 0;
	if (fake_read_int("stop", &stop) == 0) { return -EIO; }

	const char *seen = getenv("N2IP_FAKE_INPUT_SEEN");
	if (seen && *seen) {
		uint8_t head[16];
		if (e->hw->dma_rw(e->hw->ctx, frame_handle, 0, head, sizeof(head), 0) == 0) {
			uint32_t sum = 0;
			for (unsigned i = 0; i < sizeof(head); i++) { sum += head[i]; }
			FILE *f = fopen(seen, "ab");
			if (f) {
				(void)fwrite(head, 1, sizeof(head), f);
				(void)fwrite(&sum, 1, sizeof(sum), f);
				(void)fclose(f);
			}
		}
	}

	if (e->frame >= e->naus) { e->frame = 0; }
	if (e->force_key) {
		for (uint32_t i = 0; i < e->naus; i++) {
			uint32_t at = (e->frame + i) % e->naus;
			if (e->au[at].key) { e->frame = at; break; }
		}
		e->force_key = 0;
	}
	struct fake_au *a = &e->au[e->frame];
	const uint8_t *src = a->data;
	uint32_t len = a->len;
	if (fake_file_exists("strip_params") && a->key) {
		uint32_t cut = strip_param_prefix(a->data, a->len);
		if (cut > 0u && cut < a->len) { src += cut; len -= cut; }
	}
	if (e->hw->dma_rw(e->hw->ctx, bs_handle, 0, (void *)src, len, 1) != 0) { return -EIO; }
	memset(res, 0, sizeof(*res));
	res->bytes = len;
	res->is_key_frm = a->key;
	e->pending_bytes = len;
	e->pending_key = a->key;
	e->frame = (e->frame + 1u) % e->naus;
	int async = 0;
	if (fake_read_int("async", &async) == 0 && async > 0) {
		res->bytes = 0;                 /* what the real GCE path returns at the ENCODE ACK */
	}
	return 0;
}

int
n2ap_venc_wait_done(struct n2ap_venc *e, unsigned timeout_ms, struct n2ap_enc_result *res)
{
	(void)timeout_ms;
	if (!e || !res) { return -EINVAL; }
	int hang = 0;
	if (fake_read_int("wait_hang_ms", &hang) == 0 && hang > 0) {
		fake_sleep_ms(hang);
		return -ETIMEDOUT;
	}
	int fail = 0;
	if (fake_read_int("wait_fail", &fail) == 0 && fail != 0) { return fail < 0 ? fail : -fail; }
	if (e->pending_bytes == 0u) { return -ETIMEDOUT; }
	res->bytes = e->pending_bytes;
	res->is_key_frm = e->pending_key;
	e->pending_bytes = 0;
	return 0;
}

int
n2ap_venc_close(struct n2ap_venc *e)
{
	if (!e) { return -EINVAL; }
	if (e->hw && e->hw->power) {
		(void)e->hw->power(e->hw->ctx, RWVC_BLK_VENC_C1, 0);
		(void)e->hw->power(e->hw->ctx, RWVC_BLK_VENC, 0);
	}
	free_stream(e);
	free(e);
	return 0;
}

/* ---- P03 / kernel stand-ins ----------------------------------------------------------------------------------- */

int
n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes)
{
	(void)handle; (void)pa; (void)guest_addr; (void)bytes;
	return -ENOENT;                      /* the fake AP model does not need PAs */
}

int
n2vc_dma_guest_register(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle, uint32_t pa,
                        uint32_t bytes, uint32_t *guest_addr)
{
	(void)h; (void)m; (void)handle; (void)pa; (void)bytes; (void)guest_addr;
	return -ENOSYS;                      /* no real registry in the fake AP model */
}

int
n2vc_dma_guest_forget(const struct n2vc_hw *h, uint32_t handle)
{
	(void)h; (void)handle;
	return -ENOSYS;
}

/* The fake host set has no guest address space (n2vhost_mem returns NULL), so the daemon takes the plain
 * dma_alloc fallback; the symbol must still exist for the link. */
int
n2vc_dma_guest_alloc(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t bytes, uint32_t flags,
                     uint32_t *handle, uint32_t *pa, uint32_t *guest_addr)
{
	(void)h; (void)m; (void)bytes; (void)flags; (void)handle; (void)pa; (void)guest_addr;
	return -ENOSYS;
}

/* P03 (client/n2vc_hw_dev.c) is not in the tree yet.  vcodecd references it even when it is started with --fake, so the
 * host test provides the one symbol it needs and refuses the real device loudly: no test can accidentally talk to hardware. */
int
n2vc_hw_open_dev(struct n2vc_hw *out, const char *dev_path)
{
	(void)out;
	(void)dev_path;
	return -ENODEV;
}
