/* SPDX-License-Identifier: MIT */
/*
 * n2jpeg_phone.c -- the one-shot on-phone test of /dev/redwood-jpeg (opus-jpeg, 12 Nov 2026).
 *
 *   n2jpeg_phone run [--gate PATH] [--keep-gate] [--log FILE] [--out DIR] [--delay S] [--sizes WxH,...] [--q Q,...]
 *                    [--bench N] [--idle-ms MS] [--no-ladder] [--dec FILE]... [--no-dec-self]
 *   n2jpeg_phone pattern WxH OUT.nv12      the test picture (16-padded rows), for the PC check
 *   n2jpeg_phone parse FILE.jpg            parse a JPEG with jdec (geometry, strides; no device; words: jdec/n2jdec_tool)
 *
 * `run` steps (every line "N2JPGP ..." to stdout, /dev/console and, with --log, the log file with fsync per line):
 *   S0 start: gate (one-shot: removed BEFORE any request), delay, jetsam limit raised (a 12 MP job needs ~37 MB of user
 *      memory; the launchd JetsamMemoryLimit does not apply, see include/n2memlimit.h), kern.n2vc.venc_fast if present.
 *   S1 RWJ_INFO (abi 1, caps bit 0).  No device -> "N2JPGP NODEV", exit 2.
 *   S2 J02 selftest 640x480 through the library (eleven quality codes + the ladder) = rwjpeg_tool selftest.
 *   S3 encode matrix through the raw ioctl (errno, out_status, out_hw_us logged): every --sizes x --q, the kernel's test
 *      picture, file OUT/jpeg-WxH-qQ.jpg.  Default sizes 640x480,1920x1080,4032x3024, q 95,90,60.
 *      If a size fails (the 12 MP block-limit question), the "ladder" tries 3840x2160, 3264x2448, 2592x1944 at q90 to
 *      bracket the limit; if 4032x3024 passes it also tries the ABI maximum 4096x3072.
 *   S4 bench: rwjpeg_tool bench 1920x1080 N q90, and 4032x3024 x3 when 12 MP passed.
 *   S5 negative ioctls: width 15, quality 0, stride < width -> EINVAL; capacity one byte short -> ENOSPC; unknown ioctl
 *      -> ENOTTY; counters unchanged.
 *   S6 idle off: sleep --idle-ms (default 3500 > RWJ_IDLE_OFF_MS), RWJ_INFO.powered must be 0, then an encode powers on again.
 *   S7 decode, kernel with 0315 (q10ji, RWJ_INFO.reserved[0] == RWJD_REV): RWJD_JOB steps (include/redwood_jpegdec.h):
 *      E0/E1 RWJD_PROBE (power-on values, no decoder write); then on the FIRST input (small) E2 (open 2015 frame-mode set),
 *      E3 (the 21 hybrid words), and with E4 on (default; --no-e4) E4a..E4i, one change each, until one step gives EOF and a
 *      plausible Y plane; that step on every further input (medium, 12 MP, the phone's own JPEG last).  Every list of
 *      register writes, 0x274/0x278/0x340, hw time and the cropped planes OUT/jdec-NAME-STEP-WxH.<fmt>.yuv are logged/saved
 *      for phone_check.py (djpeg oracle).  --decode-only skips S2-S6 (no encoder job at all).
 *   S7 decode, older kernels: for every --dec file and the phone's own OUT/jpeg-640x480-q90.jpg: parse (jdec/n2jdec_parse.c), log the
 *      geometry and the 21 hybrid words (jdec/n2jdec_words.c, addresses of the K01 claim; [I] slots marked), then
 *      RWJ_DECODE in query mode (dst_stride 0, J04's proposal).  ENOTSUP -> "SKIP" (the kernel has no decoder yet);
 *      otherwise decode, write OUT/jdec-NAME-WxH.<420p|422p|grey>.yuv (cropped planes) and its FNV-1a-64 for the PC.
 *   S8 "N2JPGP DONE pass=P fail=F skip=S" (exit 0 when fail == 0).
 *
 * The PC side is jpeg/phone/phone_check.py (djpeg/ffmpeg validity, PSNR vs the regenerated picture, cjpeg reference,
 * decode vs djpeg).  The host test (jpeg/phone/test/run_tests.sh) builds this file with -DN2JPGP_TEST against a fake device
 * (J02's cjpeg-backed fake + K01's validation rules + a djpeg/ffmpeg-backed decoder).
 */
#if !defined(__APPLE__)
#define _DEFAULT_SOURCE 1             /* usleep, fsync on glibc */
#endif

/* J02's tool, unchanged: its static test picture, marker checker and selftest/bench verbs are the ones the host suite
 * verified byte-for-byte against hwc.py; its main() becomes n2rwj_tool_main(). */
#define main n2rwj_tool_main
#include "../rwjpeg_tool.c"
#undef main

#include "../../jdec/n2jdec.h"

#include <sys/ioctl.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#include "n2memlimit.h"

#define P "N2JPGP "

/* ------------------------------------------------------------------ logging */

static int g_log = -1;

static void
plog(const char *fmt, ...)
{
	char line[1536];
	va_list ap;
	int n, fd;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n >= sizeof(line))
		n = (int)sizeof(line) - 1;
	printf(P "%s\n", line);
	fflush(stdout);
	if (g_log >= 0)
		(void)fsync(g_log);       /* stdout is the log (dup2): a crash keeps every line written so far */
	fd = open("/dev/console", O_WRONLY);
	if (fd >= 0) {
		(void)write(fd, P, sizeof(P) - 1);
		(void)write(fd, line, (size_t)n);
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}
}

static unsigned g_pass, g_fail, g_skip;

static void
verdict(int ok, const char *what)
{
	if (ok > 0) g_pass++;
	else if (ok == 0) g_fail++;
	else g_skip++;
	plog("%s %s", ok > 0 ? "PASS" : ok == 0 ? "FAIL" : "SKIP", what);
}

/* ------------------------------------------------------------------ raw device */

static int g_fd = -1;

#ifdef N2JPGP_TEST
extern int n2jpgp_test_ioctl(unsigned long request, void *arg);
static int dev_open(void) { g_fd = 3; return 0; }
static int xioctl(unsigned long request, void *arg) { return n2jpgp_test_ioctl(request, arg); }
#else
static int
dev_open(void)
{
	const char *path = getenv("N2RWJ_DEV");

	g_fd = open(path ? path : RWJPEG_DEV_PATH, O_RDWR);
	return g_fd >= 0 ? 0 : -1;
}
static int xioctl(unsigned long request, void *arg) { return ioctl(g_fd, request, arg); }
#endif

static int
info_get(struct rwj_info *in)
{
	memset(in, 0, sizeof(*in));
	return xioctl(RWJ_INFO, in);
}

static void
info_log(const char *when, const struct rwj_info *in)
{
	plog("info %s abi=%u max_pixels=%u jobs=%u failed=%u powered=%u last_hw_us=%u caps=0x%x", when, in->abi_version,
	     in->max_pixels, in->jobs_done, in->jobs_failed, in->powered, in->last_hw_us, in->caps);
}

struct enc_result {
	int rc, err;
	struct rwj_encode req;
	uint64_t wall_us;
};

static void
enc_raw(struct enc_result *r, uint32_t w, uint32_t h, unsigned q, const uint8_t *pic, uint32_t stride, uint32_t rows,
        uint8_t *dst, size_t cap)
{
	memset(r, 0, sizeof(*r));
	r->req.width = w;
	r->req.height = h;
	r->req.format = RWJ_FMT_NV12;
	r->req.quality = q;
	r->req.stride = stride;
	r->req.chroma_stride = stride;
	r->req.src_y = (uint64_t)(uintptr_t)pic;
	r->req.src_c = (uint64_t)(uintptr_t)(pic + (size_t)stride * rows);
	r->req.dst = (uint64_t)(uintptr_t)dst;
	r->req.dst_capacity = cap > 0xffffffffu ? 0xffffffffu : (uint32_t)cap;
	uint64_t t0 = now_us();
	errno = 0;
	r->rc = xioctl(RWJ_ENCODE, &r->req);
	r->err = r->rc < 0 ? errno : 0;
	r->wall_us = now_us() - t0;
}

static const char *
status_name(uint32_t s)
{
	switch (s) {
	case 0: return "ok";
	case RWJ_E_TIMEOUT: return "TIMEOUT";
	case RWJ_E_STALL: return "STALL";
	case RWJ_E_POWER: return "POWER";
	case RWJ_E_BUS: return "BUS";
	case RWJ_E_SIZE: return "SIZE";
	default: return "?";
	}
}

/* ------------------------------------------------------------------ helpers */

static int
parse_list_sizes(const char *s, uint32_t *ws, uint32_t *hs, unsigned cap)
{
	unsigned n = 0;
	char buf[512], *save = NULL, *tok;

	snprintf(buf, sizeof(buf), "%s", s);
	for (tok = strtok_r(buf, ",", &save); tok && n < cap; tok = strtok_r(NULL, ",", &save)) {
		if (!parse_size(tok, &ws[n], &hs[n]) || !dims_ok(ws[n], hs[n]))
			return -1;
		n++;
	}
	return (int)n;
}

static int
parse_list_q(const char *s, unsigned *qs, unsigned cap)
{
	unsigned n = 0;
	char buf[256], *save = NULL, *tok;

	snprintf(buf, sizeof(buf), "%s", s);
	for (tok = strtok_r(buf, ",", &save); tok && n < cap; tok = strtok_r(NULL, ",", &save)) {
		unsigned q = parse_uint(tok, 0);
		if (q < 1 || q > 100)
			return -1;
		qs[n++] = q;
	}
	return (int)n;
}

static uint64_t
fnv64(const uint8_t *p, size_t n, uint64_t h)
{
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

static void
sysctl_note(void)
{
#ifdef __APPLE__
	int v = -1;
	size_t len = sizeof(v);

	if (sysctlbyname("kern.n2vc.venc_fast", &v, &len, NULL, 0) == 0)
		plog("sysctl kern.n2vc.venc_fast=%d (VENC 624 MHz when 1; the JPEG driver's own VEN reference always asks 624)", v);
	else
		plog("sysctl kern.n2vc.venc_fast absent (errno %d): image without 0312", errno);
#else
	plog("sysctl: not Darwin");
#endif
}

/* ------------------------------------------------------------------ S3 encode one size/quality and save */

static uint8_t *g_pic, *g_jpeg;
static size_t g_pic_cap, g_jpeg_cap;
static uint32_t g_pic_w, g_pic_h;

static int
ensure_buffers(uint32_t w, uint32_t h)
{
	size_t pic = (size_t)align16(w) * align16(h) * 3u / 2u, jcap;

	if (pic > g_pic_cap) {
		free(g_pic);
		g_pic = (uint8_t *)malloc(pic);
		g_pic_cap = g_pic ? pic : 0;
		g_pic_w = g_pic_h = 0;
	}
	jcap = (size_t)w * h + (size_t)w * h / 2u + 65536u;
	if (jcap > g_jpeg_cap) {
		free(g_jpeg);
		g_jpeg = (uint8_t *)malloc(jcap);
		g_jpeg_cap = g_jpeg ? jcap : 0;
	}
	if (g_pic == NULL || g_jpeg == NULL)
		return -1;
	if (g_pic_w != w || g_pic_h != h) {
		test_pattern(g_pic, w, h, align16(w), align16(h));
		g_pic_w = w;
		g_pic_h = h;
	}
	return 0;
}

/* returns 1 pass, 0 fail */
static int
encode_case(const char *outdir, uint32_t w, uint32_t h, unsigned q, int save, const char *tag)
{
	struct enc_result r;
	char why[128], path[600], what[256];
	uint32_t stride = align16(w), rows = align16(h);
	int ok;

	if (ensure_buffers(w, h) != 0) {
		plog("enc %ux%u q=%u: out of memory (%zu + %zu bytes)", w, h, q, g_pic_cap, g_jpeg_cap);
		snprintf(what, sizeof(what), "%s %ux%u q=%u (no memory)", tag, w, h, q);
		verdict(0, what);
		return 0;
	}
	enc_raw(&r, w, h, q, g_pic, stride, rows, g_jpeg, g_jpeg_cap);
	snprintf(what, sizeof(what), "%s %ux%u q=%u", tag, w, h, q);
	if (r.rc != 0) {
		plog("enc %ux%u q=%u code=0x%x rc=%d errno=%d (%s) out_status=%u (%s) out_bytes=%u out_hw_us=%u wall_us=%llu",
		     w, h, q, rwjpeg_quality_code(q), r.rc, r.err, strerror(r.err), r.req.out_status,
		     status_name(r.req.out_status), r.req.out_bytes, r.req.out_hw_us, (unsigned long long)r.wall_us);
		verdict(0, what);
		return 0;
	}
	ok = r.req.out_bytes >= 4 && r.req.out_bytes <= g_jpeg_cap &&
	     jpeg_check(g_jpeg, r.req.out_bytes, w, h, why, sizeof(why)) == 0;
	if (!ok && r.req.out_bytes >= 4 && r.req.out_bytes <= g_jpeg_cap)
		plog("enc %ux%u q=%u: %u bytes but %s", w, h, q, r.req.out_bytes, why);
	path[0] = 0;
	if (save && r.req.out_bytes <= g_jpeg_cap) {
		snprintf(path, sizeof(path), "%s/jpeg-%ux%u-q%u.jpg", outdir, w, h, q);
		if (write_file(path, g_jpeg, r.req.out_bytes) != 0)
			path[0] = 0;
	}
	{
		double mp = (double)w * h / 1e6;
		plog("enc %ux%u q=%u code=0x%x bytes=%u bpp=%.3f hw_us=%u wall_us=%llu hw_ms_per_mp=%.3f mp_per_s_hw=%.1f fnv=%016llx -> %s",
		     w, h, q, rwjpeg_quality_code(q), r.req.out_bytes, r.req.out_bytes * 8.0 / ((double)w * h), r.req.out_hw_us,
		     (unsigned long long)r.wall_us, r.req.out_hw_us / 1000.0 / mp,
		     r.req.out_hw_us ? mp / (r.req.out_hw_us / 1e6) : 0.0,
		     (unsigned long long)fnv64(g_jpeg, r.req.out_bytes <= g_jpeg_cap ? r.req.out_bytes : 0, 0xcbf29ce484222325ull),
		     path[0] ? path : "(not saved)");
	}
	verdict(ok, what);
	return ok;
}

/* ------------------------------------------------------------------ S5 negatives */

static void
negative(const char *name, struct rwj_encode *req, int want_errno)
{
	char what[160];
	int rc, e;

	errno = 0;
	rc = xioctl(RWJ_ENCODE, req);
	e = rc < 0 ? errno : 0;
	snprintf(what, sizeof(what), "negative %s -> errno %d (%s), want %d (%s)", name, e, strerror(e), want_errno,
	         strerror(want_errno));
	verdict(rc < 0 && e == want_errno, what);
}

static void
step_negatives(void)
{
	struct rwj_info before, after;
	struct rwj_encode base, r;
	uint32_t w = 640, h = 480;
	char what[200];
	int rc, e;

	if (ensure_buffers(w, h) != 0) {
		verdict(0, "negatives (no memory)");
		return;
	}
	(void)info_get(&before);
	memset(&base, 0, sizeof(base));
	base.width = w; base.height = h; base.format = RWJ_FMT_NV12; base.quality = 90;
	base.stride = align16(w); base.chroma_stride = align16(w);
	base.src_y = (uint64_t)(uintptr_t)g_pic;
	base.src_c = (uint64_t)(uintptr_t)(g_pic + (size_t)align16(w) * align16(h));
	base.dst = (uint64_t)(uintptr_t)g_jpeg;
	base.dst_capacity = (uint32_t)g_jpeg_cap;

	r = base; r.width = 15; negative("width 15", &r, EINVAL);
	r = base; r.quality = 0; negative("quality 0", &r, EINVAL);
	r = base; r.stride = w - 16; r.chroma_stride = w - 16; negative("stride < width", &r, EINVAL);
	r = base; r.dst_capacity = w * h / 2u + 4096u - 1u; negative("capacity minimum - 1", &r, ENOSPC);
	{
		struct rwj_info x;
		errno = 0;
		rc = xioctl(RWJ_IOC(0x40000000u, 9, struct rwj_info), &x);
		e = rc < 0 ? errno : 0;
		snprintf(what, sizeof(what), "negative unknown ioctl nr 9 -> errno %d (%s), want ENOTTY", e, strerror(e));
		verdict(rc < 0 && e == ENOTTY, what);
	}
	(void)info_get(&after);
	snprintf(what, sizeof(what), "negatives started no job (jobs %u->%u, failed %u->%u)", before.jobs_done,
	         after.jobs_done, before.jobs_failed, after.jobs_failed);
	verdict(before.jobs_done == after.jobs_done && before.jobs_failed == after.jobs_failed, what);
}

/* ------------------------------------------------------------------ S7 decode */

/* Proposed decode layout inside K01's 40 MiB claim (RW_CLAIM_JPEG_PA 0xd8000000 .. 0xda800000; the vcodec pool starts at
 * 0xda800000): bitstream <= 7 MiB at +0, three 16 KiB index tables at +7 MiB, planes from +8 MiB (32 MiB: a 4096x3072 4:2:2
 * picture is 25.2 MB).  The encoder's split (24 MiB source + 16 MiB output) is NOT enough for a 12 MP decode output. */
#define CLAIM_PA     0xd8000000u
#define CLAIM_IDX_PA 0xd8700000u
#define CLAIM_OUT_PA 0xd8800000u
#define CLAIM_END    0xda800000u

static const char *
fmt_name(uint32_t f)
{
	return f == N2JDEC_FMT_420P ? "420p" : f == N2JDEC_FMT_422P ? "422p" : f == N2JDEC_FMT_GREY ? "grey" : "none";
}

static void
decode_one(const char *path, const char *outdir)
{
	uint8_t *file = NULL;
	size_t size = 0;
	n2jdec_jpeg j;
	n2jdec_words wd;
	char what[700], line[700];
	const char *base = strrchr(path, '/');
	int rc, e;

	base = base ? base + 1 : path;
	if (read_file(path, &file, &size) != 0) {
		snprintf(what, sizeof(what), "decode %s (cannot read)", base);
		verdict(0, what);
		return;
	}
	rc = n2jdec_parse(file, size, &j);
	if (rc != N2JDEC_OK) {
		plog("dec %s: %zu bytes, parser: %s", base, size, n2jdec_strerror(rc));
		snprintf(what, sizeof(what), "decode %s parse", base);
		verdict(0, what);
		free(file);
		return;
	}
	plog("dec %s: %zu bytes %ux%u comps=%u sampling=%u (H0V0 %ux%u) mcu=%ux%u total_mcu=%u dri=%u out=%s strides y=%u c=%u planes y=%u c=%u",
	     base, size, j.width, j.height, j.ncomp, j.sampling, j.comp[0].h, j.comp[0].v, j.mcu_w, j.mcu_h, j.total_mcu,
	     j.restart_interval, fmt_name(j.out_fmt), j.comp[0].stride, j.ncomp == 3 ? j.comp[1].stride : 0,
	     j.comp[0].plane_bytes, j.ncomp == 3 ? j.comp[1].plane_bytes : 0);
	if (n2jdec_words_supported(&j)) {
		n2jdec_addr a;
		unsigned i, n = 0;

		a.src_pa = CLAIM_PA;
		a.out_y = CLAIM_OUT_PA;
		a.out_u = CLAIM_OUT_PA + ((j.comp[0].plane_bytes + 0xfffu) & ~0xfffu);
		a.out_v = a.out_u + ((j.ncomp == 3 ? j.comp[1].plane_bytes : 0) + 0xfffu) / 0x1000u * 0x1000u;
		a.index_pa = CLAIM_IDX_PA;  /* 3 x 16 KiB index tables (J04: allocate 3x) */
		a.index_size = 16384u;
		if (n2jdec_fill_words(&j, &a, &wd) == N2JDEC_OK) {
			for (i = 0; i < N2JDEC_WORDS && n + 12 < sizeof(line); i++)
				n += (unsigned)snprintf(line + n, sizeof(line) - n, "%s%08x%s", i ? " " : "", wd.data[i],
				                        (wd.hypothesis_mask >> i) & 1u ? "?" : "");
			plog("dec %s words(K01 claim PAs; ? = [I] hypothesis) %s", base, line);
			uint64_t end = (uint64_t)a.out_v + (j.ncomp == 3 ? j.comp[2].plane_bytes : 0);
			if (size > 7u * 1024u * 1024u || end > CLAIM_END)
				plog("dec %s: does NOT fit the proposed claim layout (bitstream %zu, planes end %#llx > %#x)", base, size,
				     (unsigned long long)end, CLAIM_END);
			plog("dec %s index 0x338=%08x 0x36c=%08x 0x370=%08x 0x374=%08x total_mcu=%u unit_num=%u", base,
			     wd.index_pa[0], wd.index_pa[1], wd.index_pa[2], wd.index_pa[3], wd.total_mcu, wd.unit_num);
		}
	} else {
		plog("dec %s: geometry outside the hardware's word formulas (n2jdec_words_supported 0)", base);
	}

	/* query (J04 proposal: dst_stride == 0) */
	struct rwj_decode d;
	memset(&d, 0, sizeof(d));
	d.src = (uint64_t)(uintptr_t)file;
	d.src_bytes = (uint32_t)size;
	errno = 0;
	rc = xioctl(RWJ_DECODE, &d);
	e = rc < 0 ? errno : 0;
	if (rc < 0 && (e == ENOTSUP || e == EOPNOTSUPP)) {
		snprintf(what, sizeof(what), "decode %s: kernel RWJ_DECODE answers ENOTSUP (no hardware decoder in this image)", base);
		verdict(-1, what);
		free(file);
		return;
	}
	if (rc < 0 || d.out_status != 0) {
		plog("dec %s: query rc=%d errno=%d (%s) out_status=%u", base, rc, e, strerror(e), d.out_status);
		snprintf(what, sizeof(what), "decode %s query", base);
		verdict(0, what);
		free(file);
		return;
	}
	plog("dec %s: query %ux%u fmt=%u stride=%u cap_y=%u cap_c=%u", base, d.out_width, d.out_height, d.out_format,
	     d.dst_stride, d.dst_capacity_y, d.dst_capacity_c);
	if (d.out_width != j.width || d.out_height != j.height || d.dst_stride != j.comp[0].stride ||
	    d.out_format != j.out_fmt) {
		snprintf(what, sizeof(what), "decode %s query disagrees with the user-space parser (%ux%u stride %u fmt %u)", base,
		         j.width, j.height, j.comp[0].stride, j.out_fmt);
		verdict(0, what);
		free(file);
		return;
	}
	{
		uint8_t *py = (uint8_t *)malloc(d.dst_capacity_y ? d.dst_capacity_y : 1);
		uint8_t *pc = (uint8_t *)malloc(d.dst_capacity_c ? d.dst_capacity_c : 1);
		uint32_t cstride = j.ncomp == 3 ? j.comp[1].stride : 0;
		uint32_t cw = 0, ch = 0;
		uint64_t t0, h = 0xcbf29ce484222325ull;
		char opath[600];
		int fd;

		if (!py || !pc) {
			free(py); free(pc); free(file);
			snprintf(what, sizeof(what), "decode %s (no memory)", base);
			verdict(0, what);
			return;
		}
		memset(py, 0, d.dst_capacity_y);
		memset(pc, 0, d.dst_capacity_c);
		d.dst_y = (uint64_t)(uintptr_t)py;
		d.dst_c = (uint64_t)(uintptr_t)pc;
		t0 = now_us();
		errno = 0;
		rc = xioctl(RWJ_DECODE, &d);
		e = rc < 0 ? errno : 0;
		t0 = now_us() - t0;
		if (rc < 0 || d.out_status != 0) {
			plog("dec %s: decode rc=%d errno=%d (%s) out_status=%u (%s) wall_us=%llu", base, rc, e, strerror(e),
			     d.out_status, status_name(d.out_status), (unsigned long long)t0);
			snprintf(what, sizeof(what), "decode %s", base);
			verdict(0, what);
			free(py); free(pc); free(file);
			return;
		}
		if (j.out_fmt == N2JDEC_FMT_420P) { cw = (j.width + 1) / 2; ch = (j.height + 1) / 2; }
		else if (j.out_fmt == N2JDEC_FMT_422P) { cw = (j.width + 1) / 2; ch = j.height; }
		snprintf(opath, sizeof(opath), "%s/jdec-%.*s-%ux%u.%s.yuv", outdir, (int)strcspn(base, "."), base, j.width,
		         j.height, fmt_name(j.out_fmt));
		fd = open(opath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		{
			uint32_t y;
			for (y = 0; y < j.height; y++) {
				h = fnv64(py + (size_t)y * d.dst_stride, j.width, h);
				if (fd >= 0) (void)!write(fd, py + (size_t)y * d.dst_stride, j.width);
			}
			for (int p = 0; p < 2 && cw; p++) {
				const uint8_t *plane = pc + (size_t)p * (d.dst_capacity_c / 2u);
				for (y = 0; y < ch; y++) {
					h = fnv64(plane + (size_t)y * cstride, cw, h);
					if (fd >= 0) (void)!write(fd, plane + (size_t)y * cstride, cw);
				}
			}
		}
		if (fd >= 0) close(fd);
		plog("dec %s: decoded %ux%u %s wall_us=%llu fnv=%016llx -> %s", base, j.width, j.height, fmt_name(j.out_fmt),
		     (unsigned long long)t0, (unsigned long long)h, fd >= 0 ? opath : "(not saved)");
		snprintf(what, sizeof(what), "decode %s (pixels: compare on the PC with phone_check.py)", base);
		verdict(fd >= 0, what);
		free(py); free(pc);
	}
	free(file);
}

/* ------------------------------------------------------------------ S7 hardware decode through RWJD_JOB (kernel 0315, q10ji) */

#include "redwood_jpegdec.h"

#define JD_MAXR 64
#define JD_SEARCH_TIMEOUT_MS 500u      /* a 640x480 decode is milliseconds; jp4: every miss cost the full 2 s */
struct jd_list { struct rwjd_reg r[JD_MAXR]; unsigned n; int bad; };

static void
jd_add(struct jd_list *l, uint32_t off, uint32_t value)
{
	unsigned i;
	for (i = 0; i < l->n; i++)            /* a later write of the same offset replaces the earlier one, except 0x090 */
		if (l->r[i].off == off && off != 0x090u && off != 0x274u) { l->r[i].value = value; return; }
	if (l->n < JD_MAXR) { l->r[l->n].off = off; l->r[l->n].value = value; l->n++; }
	else l->bad = 1;
}

static void
jd_set(struct jd_list *l, uint32_t off, uint32_t value)    /* replace in place (E4 variants), add before the trigger if absent */
{
	unsigned i;
	for (i = 0; i < l->n; i++)
		if (l->r[i].off == off) { l->r[i].value = value; return; }
	if (l->n >= 2 && l->n < JD_MAXR) {      /* keep [.. 0x274 clear, 0x240 trigger] last */
		l->r[l->n] = l->r[l->n - 1]; l->r[l->n - 1] = l->r[l->n - 2];
		l->r[l->n - 2].off = off; l->r[l->n - 2].value = value; l->n++;
	}
}

struct jd_geo {
	n2jdec_jpeg j;
	n2jdec_addr a;
	uint32_t yb, cb, dst_bytes;           /* plane bytes (Y, one chroma), bytes copied out from RWJD_OUT_PA */
};

static int
jd_geometry(const uint8_t *file, size_t size, struct jd_geo *g, const char *base)
{
	int rc = n2jdec_parse(file, size, &g->j);
	if (rc != N2JDEC_OK) { plog("hwdec %s: parser: %s", base, n2jdec_strerror(rc)); return -1; }
	if (!n2jdec_words_supported(&g->j)) { plog("hwdec %s: sampling outside the word formulas", base); return -1; }
	g->yb = g->j.comp[0].plane_bytes;
	g->cb = g->j.ncomp == 3 ? g->j.comp[1].plane_bytes : 0;
	g->a.src_pa = RWJD_SRC_PA;
	g->a.index_pa = RWJD_IDX_PA;
	g->a.index_size = 16384u;
	g->a.out_y = RWJD_OUT_PA;
	g->a.out_u = RWJD_OUT_PA + ((g->yb + 0xfffu) & ~0xfffu);
	g->a.out_v = g->a.out_u + ((g->cb + 0xfffu) & ~0xfffu);
	g->dst_bytes = g->a.out_v + g->cb - RWJD_OUT_PA;
	if (size > RWJD_SRC_MAX - 256u || g->dst_bytes > RWJD_OUT_MAX) {
		plog("hwdec %s: does not fit the decoder claim (bitstream %zu, planes %u)", base, size, g->dst_bytes);
		return -1;
	}
	plog("hwdec %s: %ux%u comps=%u sampling=%u out=%s mcu=%ux%u total_mcu=%u dri=%u strides %u/%u planes Y@%#x U@%#x V@%#x copy %u",
	     base, g->j.width, g->j.height, g->j.ncomp, g->j.sampling, fmt_name(g->j.out_fmt), g->j.mcu_w, g->j.mcu_h,
	     g->j.total_mcu, g->j.restart_interval, g->j.comp[0].stride, g->j.ncomp == 3 ? g->j.comp[1].stride : 0,
	     g->a.out_y, g->a.out_u, g->a.out_v, g->dst_bytes);
	return 0;
}

/* E2: the open 2015 frame-mode set (n2jdec_supplementary, J04 PHONE_EXPERIMENTS E2): both output banks, hybrid-only
 * registers 0, then clear 0x274 and trigger 0x240 last. */
static int
jd_build_e2(const struct jd_geo *g, struct jd_list *l)
{
	n2jdec_reg s[48];
	unsigned n = 0, i;
	memset(l, 0, sizeof(*l));
	if (n2jdec_supplementary(&g->j, &g->a, s, 48, &n) != N2JDEC_OK) return -1;
	for (i = 0; i < n; i++)
		if (s[i].off != 0x240u) jd_add(l, s[i].off, s[i].value);
	jd_add(l, 0x14c, g->a.out_y); jd_add(l, 0x150, g->a.out_u); jd_add(l, 0x154, g->a.out_v);
	static const uint32_t hy[] = { 0x31c, 0x330, 0x334, 0x338, 0x33c, 0x344, 0x36c, 0x370, 0x374 };
	for (i = 0; i < sizeof(hy) / sizeof(hy[0]); i++) jd_add(l, hy[i], 0);
	jd_add(l, 0x274, 0x37); jd_add(l, 0x240, 0);
	return l->bad ? -1 : 0;
}

/* E3: jpeg_drv_hybrid_dec_start's data[0..18] in slot order, the four index pointers, the 2015 strides and bank 0, then
 * clear 0x274 and data[19] (0x240) last. */
static int
jd_build_e3(const struct jd_geo *g, struct jd_list *l)
{
	n2jdec_words w;
	unsigned n, i;
	const n2jdec_word_info *t = n2jdec_word_table(&n);
	memset(l, 0, sizeof(*l));
	if (n2jdec_fill_words(&g->j, &g->a, &w) != N2JDEC_OK) return -1;
	for (i = 0; i < n && i < 19; i++) jd_add(l, t[i].off, w.data[i]);
	jd_add(l, 0x338, w.index_pa[0]); jd_add(l, 0x36c, w.index_pa[1]); jd_add(l, 0x370, w.index_pa[2]); jd_add(l, 0x374, w.index_pa[3]);
	jd_add(l, 0x158, g->j.comp[0].stride); jd_add(l, 0x15c, g->j.ncomp == 3 ? g->j.comp[1].stride : 0);
	jd_add(l, 0x140, g->a.out_y); jd_add(l, 0x144, g->a.out_u); jd_add(l, 0x148, g->a.out_v);
	jd_add(l, 0x274, 0x37); jd_add(l, 0x240, w.data[19]);
	return l->bad ? -1 : 0;
}

/* E4 variants of E3 (J04 PHONE_EXPERIMENTS E4a..E4i), one change each */
static const char *const jd_e4_names[] = { "E4a1", "E4a2", "E4a3", "E4b", "E4c", "E4d", "E4e", "E4f", "E4g1", "E4g2", "E4h", "E4i" };
#define JD_E4_COUNT 12u
static void
jd_apply_e4(unsigned v, const struct jd_geo *g, struct jd_list *l)
{
	uint32_t uvb = (uint32_t)g->j.uv_brz_w << 8;
	switch (v) {
	case 0: case 1: case 2: jd_set(l, 0x17c, v + 1u); break;                           /* operation mode 1/2/3 */
	case 3: jd_set(l, 0x330, g->j.width); jd_set(l, 0x334, g->j.height); break;        /* dimensions instead of strides */
	case 4: jd_set(l, 0x330, 0); jd_set(l, 0x334, 0); break;
	case 5: jd_set(l, 0x31c, uvb); break;     /* BRZ: 0x31C is an address-class register: only 0 is legal for 4:2:0 */
	case 6: jd_set(l, 0x31c, RWJD_IDX_PA + 0xc000u); break;    /* a zeroed 16 KiB table: the kernel zeroes 64 KiB, 3x16K are the index tables */
	case 7: jd_set(l, 0x31c, RWJD_SRC_PA + ((g->j.file_bytes + 15u) & ~15u)); break;   /* FILE_BRP */
	case 8: jd_set(l, 0x33c, 0); jd_set(l, 0x344, g->j.total_mcu); break;
	case 9: jd_set(l, 0x33c, 0); jd_set(l, 0x344, 0); break;
	case 10: jd_set(l, 0x338, 0); jd_set(l, 0x36c, 0); jd_set(l, 0x370, 0); jd_set(l, 0x374, 0); break;
	case 11: {                                                                          /* 0x090 0x00/0x01 */
		unsigned i, seen = 0;
		for (i = 0; i < l->n; i++) if (l->r[i].off == 0x090u && seen++ == 1) l->r[i].value = 0x01;
		break; }
	}
}

/*
 * F steps (jp6, after phone run jp4-jpegdec on q10jj).  That run's kernel lines show the block DOES start: 0x278 state
 * 2 (HEADER) whenever 0x344 was 0 (E2, E4g2) and state 3 (VLD) whenever 0x344 was total_mcu-1 / total_mcu (E3, E4a..g1,
 * E4h), never an interrupt.  Upstream Linux names 0x344 JPGDEC_REG_BIT_STREAM_SIZE (mtk_jpeg_dec_reg.h; written next to
 * FILE_TOTAL_SIZE in mtk_jpeg_dec_set_bs_info) and its power-on value here is 0xffffff00: the J04 guess "last MCU" told
 * the block the stream is 1199 bytes long.  Also: n2jdec_supplementary() lacks 0x0FC DU_NUM (set_config writes it), and
 * E2/E3 overwrite the power-on values of 0x31C (0x17f), 0x330 (0xffc1), 0x33C (0x10) with guesses.
 *   F0  open 2015 set + 0x0FC, both banks, NO hybrid-only register written (power-on values, if the reset keeps them)
 *   F1  F0 + 0x344 = file bytes          F2  F0 + 0x344 = align128(file bytes)
 *   F3  E3 (21 hybrid words) with 0x344 = file bytes
 *   F4  F3 without the [I] slots 0x31C/0x330/0x334/0x33C (left at whatever the reset gives)
 */
static const char *const jd_f_names[] = { "F0", "F1", "F2", "F3", "F4" };
#define JD_F_COUNT 5u

static void
jd_del(struct jd_list *l, uint32_t off)
{
	unsigned i, k = 0;
	for (i = 0; i < l->n; i++) if (l->r[i].off != off) l->r[k++] = l->r[i];
	l->n = k;
}

static int
jd_build_f(unsigned v, const struct jd_geo *g, struct jd_list *l)
{
	static const uint32_t hy[] = { 0x31c, 0x330, 0x334, 0x338, 0x33c, 0x344, 0x36c, 0x370, 0x374 };
	static const uint32_t slots[] = { 0x31c, 0x330, 0x334, 0x33c };
	n2jdec_words w;
	unsigned i;
	if (v <= 2) {
		if (jd_build_e2(g, l) != 0 || n2jdec_fill_words(&g->j, &g->a, &w) != N2JDEC_OK) return -1;
		for (i = 0; i < sizeof(hy) / sizeof(hy[0]); i++) jd_del(l, hy[i]);
		jd_set(l, 0x0fc, w.data[2]);                                   /* DU_NUM, missing from the supplementary set */
		if (v == 1) jd_set(l, 0x344, g->j.file_bytes);
		if (v == 2) jd_set(l, 0x344, (g->j.file_bytes + 127u) & ~127u);
		return l->bad ? -1 : 0;
	}
	if (jd_build_e3(g, l) != 0) return -1;
	jd_set(l, 0x344, g->j.file_bytes);
	if (v == 4) for (i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) jd_del(l, slots[i]);
	return l->bad ? -1 : 0;
}

/*
 * G steps (jp8, after phone run jp6-jpegdec on q10jj).  F3 decoded dec640.jpg (75 dB vs libjpeg) but ran AFTER F0..F2,
 * and the post-reset probes show the 0x090 reset keeps 0x0FC..0x170: F3 writes none of 0x0F8/0x160/0x164/0x16C/0x170, so
 * it ran with F0's IMG_STRIDE 0x280/0x140, WDMA_CTRL 0x70407 and PAUSE_MCU_NUM 1200 -- right for 640x480 only.  The two
 * larger inputs then ran with those 640x480 values (final window dump: 160=280 164=140 16c=70407 170=4b0) and stopped in
 * 0x278 state 6 without an interrupt.
 *   G0  = the F3 list in the untouched power-on state (first job of the boot): does F3 need the 2015 registers at all?
 *   F5  = F3 + 0x0F8, 0x160, 0x164, 0x16C, 0x170 of THIS picture (n2jdec_supplementary values)
 *   F6  = F5 + 0x248 FILE_BRP + 0x320 (the whole 2015 set)        F5n = F5 with 0x170 = power-on 0x7ffffff (no pause)
 *   diagnostics on a larger input that F5 decoded, one stale 640x480 value each (expected to FAIL = names the culprit):
 *   F5p 0x170 = first input's total_mcu;  F5s 0x160/0x164 = first input's strides;  F5w 0x16C = first input's word
 *   (a diagnostic whose stale value equals this input's own is skipped: dec640n has dec640's geometry)
 *   F5i = F5 with the index tables 0x5000 apart (0x370 = base+0x5000, 0x374 = base+0xa000; still inside the 64 KiB the
 *         kernel zeroes): only if F5, F6 and F5n miss on a larger input (index table of 16 KiB too small for 16200/47628 MCUs?)
 * Hypothesis 2 (bitstream / 0x344 >= 64 KiB) has its own inputs, not a step: dec640n.jpg (640x480 noise q100, > 64 KiB)
 * and dec1920f.jpg (1920x1080 gradient 4:2:0, no DRI, < 64 KiB), both run with F5 after the jp6 inputs.
 */
static int
jd_build_f5(unsigned v, const struct jd_geo *g, struct jd_list *l)      /* v: 0 F5, 1 F6, 2 F5n, 3 F5i */
{
	n2jdec_reg s[48];
	unsigned n = 0, i;
	if (jd_build_f(3, g, l) != 0 || n2jdec_supplementary(&g->j, &g->a, s, 48, &n) != N2JDEC_OK) return -1;
	for (i = 0; i < n; i++) {
		uint32_t o = s[i].off;
		if (o == 0x0f8u || o == 0x160u || o == 0x164u || o == 0x16cu || o == 0x170u || (v == 1 && (o == 0x248u || o == 0x320u)))
			jd_set(l, o, s[i].value);
	}
	if (v == 2) jd_set(l, 0x170, 0x7ffffffu);
	if (v == 3) { jd_set(l, 0x370, RWJD_IDX_PA + 0x5000u); jd_set(l, 0x374, RWJD_IDX_PA + 0xa000u); }
	return l->bad ? -1 : 0;
}

static uint32_t
jd_supp(const struct jd_geo *g, uint32_t off)
{
	n2jdec_reg s[48];
	unsigned n = 0, i;
	if (n2jdec_supplementary(&g->j, &g->a, s, 48, &n) != N2JDEC_OK) return 0;
	for (i = 0; i < n; i++) if (s[i].off == off) return s[i].value;
	return 0;
}

/* the list of a named step (the search result) for another input */
static int
jd_build_named(const char *step, const struct jd_geo *g, struct jd_list *l)
{
	unsigned k;
	if (!strcmp(step, "F5")) return jd_build_f5(0, g, l);
	if (!strcmp(step, "F6")) return jd_build_f5(1, g, l);
	if (!strcmp(step, "F5n")) return jd_build_f5(2, g, l);
	if (!strcmp(step, "F5i")) return jd_build_f5(3, g, l);
	if (!strcmp(step, "G0")) return jd_build_f(3, g, l);
	for (k = 0; k < JD_F_COUNT; k++) if (!strcmp(step, jd_f_names[k])) return jd_build_f(k, g, l);
	if (!strcmp(step, "E2")) return jd_build_e2(g, l);
	if (jd_build_e3(g, l) != 0) return -1;
	for (k = 0; k < JD_E4_COUNT; k++) if (!strcmp(step, jd_e4_names[k])) jd_apply_e4(k, g, l);
	return 0;
}

static void
jd_log_list(const char *base, const char *step, const struct jd_list *l)
{
	char line[1200];
	unsigned i, n = 0;
	for (i = 0; i < l->n && n + 20 < sizeof(line); i++)
		n += (unsigned)snprintf(line + n, sizeof(line) - n, "%s%03x=%x", i ? " " : "", l->r[i].off, l->r[i].value);
	plog("hwdec %s %s regs(%u): %s", base, step, l->n, line);
}

static double
jd_sqrt(double v)                      /* no libm: Newton */
{
	double x = v > 1.0 ? v : 1.0;
	if (v <= 0.0) return 0.0;
	for (int i = 0; i < 40; i++) x = 0.5 * (x + v / x);
	return x;
}

/* one RWJD_JOB; returns 1 when EOF and a plausible picture, 0 otherwise; writes the planes for the PC either way.
 * search: 0 = a result (PASS/FAIL), 1 = a search step (a miss is a SKIP), 2 = a diagnostic (never PASS/FAIL, no file) */
static int
jd_run(const char *base, const char *step, const uint8_t *file, size_t size, const struct jd_geo *g, struct jd_list *l,
       const char *outdir, uint8_t *dst, int search)
{
	struct rwjd_job d;
	uint64_t t0;
	int rc, e;
	char what[300];

	jd_log_list(base, step, l);
	memset(&d, 0, sizeof(d));
	d.src = (uint64_t)(uintptr_t)file; d.src_bytes = (uint32_t)size;
	d.regs = (uint64_t)(uintptr_t)l->r; d.nregs = l->n;
	d.dst = (uint64_t)(uintptr_t)dst; d.dst_bytes = g->dst_bytes;
	d.timeout_ms = search ? JD_SEARCH_TIMEOUT_MS : RWJD_TIMEOUT_MAX_MS;
	memset(dst, 0, g->dst_bytes);
	t0 = now_us();
	errno = 0;
	rc = xioctl(RWJD_JOB, &d);
	e = rc < 0 ? errno : 0;
	t0 = now_us() - t0;
	plog("hwdec %s %s: rc=%d errno=%d (%s) out_status=%u (%s) hw_us=%u wall_us=%llu irq(0x274)=%#x state(0x278)=%#x mcu(0x340)=%u total_mcu=%u",
	     base, step, rc, e, e ? strerror(e) : "ok", d.out_status, status_name(d.out_status), d.out_hw_us,
	     (unsigned long long)t0, d.out_irq, d.out_state, d.out_mcu, g->j.total_mcu);
	snprintf(what, sizeof(what), "hwdec %s %s", base, step);
	if (rc != 0)       /* 0316: sys_ioctl copies nothing out on an error, the out_* words above are NOT the hardware's */
		plog("hwdec %s %s: failed job: status/irq/state/mcu are only in the kernel line 'NORD2 JPEG: dec %u regs -> ...' (console, in order)",
		     base, step, l->n);
	if (rc != 0) {
		if (search == 2 && e == EIO) { strncat(what, " (diagnostic: no decode)", sizeof(what) - strlen(what) - 1); verdict(-1, what); }
		else if (search && e == EIO) { strncat(what, " (search miss: next step)", sizeof(what) - strlen(what) - 1); verdict(-1, what); }
		else verdict(0, what);
		return 0;
	}
	/* plausibility on the phone: Y statistics over the visible area; the real check is phone_check.py vs djpeg */
	{
		uint64_t sum = 0, sq = 0, h = 0xcbf29ce484222325ull;
		uint32_t x, y, W = g->j.width, H = g->j.height, cs = g->j.ncomp == 3 ? g->j.comp[1].stride : 0, cw = 0, ch = 0;
		const uint8_t *py = dst, *pu = dst + (g->a.out_u - RWJD_OUT_PA), *pv = dst + (g->a.out_v - RWJD_OUT_PA);
		char opath[600];
		int fd;
		for (y = 0; y < H; y++) for (x = 0; x < W; x++) { uint32_t v = py[(size_t)y * g->j.comp[0].stride + x]; sum += v; sq += v * v; }
		double n = (double)W * H, mean = sum / n, var = sq / n - mean * mean;
		if (g->j.out_fmt == N2JDEC_FMT_420P) { cw = (W + 1) / 2; ch = (H + 1) / 2; }
		else if (g->j.out_fmt == N2JDEC_FMT_422P) { cw = (W + 1) / 2; ch = H; }
		snprintf(opath, sizeof(opath), "%s/jdec-%.*s-%s-%ux%u.%s.yuv", outdir, (int)strcspn(base, "."), base, step, W, H,
		         fmt_name(g->j.out_fmt));
		fd = search == 2 ? -1 : open(opath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		for (y = 0; y < H; y++) { h = fnv64(py + (size_t)y * g->j.comp[0].stride, W, h); if (fd >= 0) (void)!write(fd, py + (size_t)y * g->j.comp[0].stride, W); }
		for (int p = 0; p < 2 && cw; p++) {
			const uint8_t *pl = p ? pv : pu;
			for (y = 0; y < ch; y++) { h = fnv64(pl + (size_t)y * cs, cw, h); if (fd >= 0) (void)!write(fd, pl + (size_t)y * cs, cw); }
		}
		if (fd >= 0) close(fd);
		int ok = var > 4.0 && mean > 2.0 && mean < 253.0;
		plog("hwdec %s %s: Y mean %.1f stddev %.1f fnv=%016llx -> %s (%s)", base, step, mean, jd_sqrt(var),
		     (unsigned long long)h, fd >= 0 ? opath : "(not saved)", ok ? "plausible" : "NOT a picture");
		if (search == 2) { strncat(what, ok ? " (diagnostic: EOF + plausible Y)" : " (diagnostic: EOF, no picture)", sizeof(what) - strlen(what) - 1); verdict(-1, what); }
		else if (!ok && search) { strncat(what, " (EOF but no picture: next step)", sizeof(what) - strlen(what) - 1); verdict(-1, what); }
		else verdict(ok, what);
		return ok;
	}
}

struct jd_input { const char *path, *base; uint8_t *file; size_t size; struct jd_geo g; int ok; };

static void
jd_probe(const char *tag)
{
	static const uint32_t offs[] = { 0x090, 0x0f8, 0x0fc, 0x140, 0x144, 0x148, 0x14c, 0x150, 0x154, 0x158, 0x15c, 0x160, 0x164,
		0x16c, 0x170, 0x17c, 0x200, 0x20c, 0x210, 0x224, 0x23c, 0x240, 0x248, 0x24c, 0x270, 0x274, 0x278, 0x31c, 0x320, 0x330,
		0x334, 0x338, 0x33c, 0x340, 0x344, 0x348, 0x36c, 0x370, 0x374 };
	struct rwjd_reg r[sizeof(offs) / sizeof(offs[0])];
	struct rwjd_job d;
	char line[1200];
	unsigned i, n = 0;
	int rc, e;

	for (i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) { r[i].off = offs[i]; r[i].value = 0xdeadbeefu; }
	memset(&d, 0, sizeof(d));
	d.regs = (uint64_t)(uintptr_t)r; d.nregs = (uint32_t)(sizeof(offs) / sizeof(offs[0])); d.flags = RWJD_PROBE;
	errno = 0;
	rc = xioctl(RWJD_JOB, &d);
	e = rc < 0 ? errno : 0;
	for (i = 0; i < d.nregs && n + 20 < sizeof(line); i++)
		n += (unsigned)snprintf(line + n, sizeof(line) - n, "%s%03x=%x", i ? " " : "", r[i].off, r[i].value);
	if (tag) {         /* after a step: the kernel has already written 0x274 = 0x37 and 0x090 = 0x00/0x10 (what does the reset keep?) */
		plog("hwdec after %s (post-reset probe): rc=%d errno=%d -> %s", tag, rc, e, line);
		return;
	}
	plog("hwdec E0/E1 probe: rc=%d errno=%d (%s) out_status=%u -> %s", rc, e, e ? strerror(e) : "ok", d.out_status, line);
	verdict(rc == 0 && d.out_status == 0, "hwdec E0/E1 probe (power-on register values, no decoder write)");
}

/* the whole window 0x000..0x3fc, read only (4 RWJD_PROBE calls), non-zero words logged.  Run LAST: a read of an unknown
 * offset cannot spoil the search then. */
static void
jd_dump_window(void)
{
	for (uint32_t base = 0; base < 0x400u; base += 0x100u) {
		struct rwjd_reg r[64];
		struct rwjd_job d;
		char line[1200];
		unsigned i, n = 0, nz = 0;
		int rc, e;
		for (i = 0; i < 64; i++) { r[i].off = base + 4u * i; r[i].value = 0xdeadbeefu; }
		memset(&d, 0, sizeof(d));
		d.regs = (uint64_t)(uintptr_t)r; d.nregs = 64; d.flags = RWJD_PROBE;
		errno = 0;
		rc = xioctl(RWJD_JOB, &d);
		e = rc < 0 ? errno : 0;
		line[0] = 0;
		for (i = 0; i < 64 && n + 20 < sizeof(line); i++)
			if (r[i].value) { n += (unsigned)snprintf(line + n, sizeof(line) - n, "%s%03x=%x", nz ? " " : "", r[i].off, r[i].value); nz++; }
		plog("hwdec window %03x-%03x: rc=%d errno=%d non-zero %u/64: %s", base, base + 0xfcu, rc, e, nz, line);
		if (rc != 0) break;
	}
}

/* S7 with the kernel decoder: probe; E2, E3, then E4a..i on the first (small) input until one gives a picture; that step
 * on every other input (medium, 12 MP).  Returns 0 when it ran. */
static int
hw_decode_all(const char **paths, unsigned n, const char *outdir, int e4)
{
	struct jd_input in[10];
	unsigned i, k, cnt = 0;
	uint8_t *dst = NULL;
	uint32_t maxdst = 0;
	struct jd_list l;
	char step[16] = "";
	int found = 0;

	jd_probe(NULL);
	for (i = 0; i < n && cnt < 10; i++) {
		struct jd_input *p = &in[cnt];
		memset(p, 0, sizeof(*p));
		p->path = paths[i];
		p->base = strrchr(paths[i], '/') ? strrchr(paths[i], '/') + 1 : paths[i];
		if (read_file(paths[i], &p->file, &p->size) != 0) continue;
		if (jd_geometry(p->file, p->size, &p->g, p->base) != 0) { free(p->file); verdict(0, "hwdec geometry"); continue; }
		if (p->g.dst_bytes > maxdst) maxdst = p->g.dst_bytes;
		cnt++;
	}
	if (cnt == 0) { plog("hwdec: no usable input"); return 0; }
	dst = (uint8_t *)malloc(maxdst);
	if (dst == NULL) { plog("hwdec: no memory for %u bytes", maxdst); verdict(0, "hwdec memory"); goto out; }

	/* jp8: G0 = the F3 list while 0x0F8/0x160..0x170 still hold their power-on values (first decoder write of the boot),
	 * then F5 (F3 + this picture's 2015 geometry registers).  Only if F5 misses: the jp6 order F0..F4, E2, E3, E4. */
	if (jd_build_f(3, &in[0].g, &l) == 0) {
		int g0 = jd_run(in[0].base, "G0", in[0].file, in[0].size, &in[0].g, &l, outdir, dst, 1);
		plog("hwdec G0 (F3 list, power-on 0x160/0x164/0x16c/0x170): %s", g0 ? "decodes: F3 does not need the 2015 geometry registers"
		     : "no picture: F3 needs registers it does not write (jp6: left by F0..F2)");
		if (!g0) jd_probe("G0");
	}
	if (jd_build_f5(0, &in[0].g, &l) == 0) {
		if (jd_run(in[0].base, "F5", in[0].file, in[0].size, &in[0].g, &l, outdir, dst, 1)) { snprintf(step, sizeof(step), "F5"); found = 1; }
		else jd_probe("F5");
	}
	for (k = 0; !found && k < JD_F_COUNT; k++) {
		if (jd_build_f(k, &in[0].g, &l) != 0) continue;
		if (jd_run(in[0].base, jd_f_names[k], in[0].file, in[0].size, &in[0].g, &l, outdir, dst, 1)) {
			snprintf(step, sizeof(step), "%s", jd_f_names[k]); found = 1;
		} else jd_probe(jd_f_names[k]);
	}
	if (!found && jd_build_e2(&in[0].g, &l) == 0) {
		if (jd_run(in[0].base, "E2", in[0].file, in[0].size, &in[0].g, &l, outdir, dst, 1)) { snprintf(step, sizeof(step), "E2"); found = 1; }
		else jd_probe("E2");
	}
	if (!found && jd_build_e3(&in[0].g, &l) == 0) {
		if (jd_run(in[0].base, "E3", in[0].file, in[0].size, &in[0].g, &l, outdir, dst, 1)) { snprintf(step, sizeof(step), "E3"); found = 1; }
		else jd_probe("E3");
	}
	for (k = 0; !found && e4 && k < JD_E4_COUNT; k++) {
		if (jd_build_e3(&in[0].g, &l) != 0) break;
		if (k == 5 && in[0].g.j.uv_brz_w == 0) { plog("hwdec E4d skipped: uv_brz_w 0 (same as E3)"); continue; }
		jd_apply_e4(k, &in[0].g, &l);
		if (jd_run(in[0].base, jd_e4_names[k], in[0].file, in[0].size, &in[0].g, &l, outdir, dst, 1)) {
			snprintf(step, sizeof(step), "%s", jd_e4_names[k]); found = 1;
		}
	}
	plog("hwdec search on %s: %s", in[0].base, found ? step : "no step gave a picture");
	if (!found) verdict(0, "hwdec search: no step (F, E2, E3, E4) decoded the first input");
	for (i = 1; i < cnt; i++) {
		char what[200];
		if (!found) { snprintf(what, sizeof(what), "hwdec %s (no working step found on %s)", in[i].base, in[0].base); verdict(-1, what); continue; }
		if (jd_build_named(step, &in[i].g, &l) != 0) { snprintf(what, sizeof(what), "hwdec %s build", in[i].base); verdict(0, what); continue; }
		/* jp6: F3 = F5 without the per-picture 2015 registers, which is only right for the first input's geometry */
		const char *st = !strcmp(step, "F3") ? "F5" : step;
		if (st != step && jd_build_named(st, &in[i].g, &l) != 0) continue;
		int ok = jd_run(in[i].base, st, in[i].file, in[i].size, &in[i].g, &l, outdir, dst, 0);
		if (!ok) jd_probe(st);
		if (strcmp(st, "F5") != 0) continue;
		if (!ok) {                         /* same boot: the whole 2015 set, then no pause index, then larger index tables */
			static const char *const alt[] = { "F6", "F5n", "F5i" };
			for (k = 0; k < 3 && !ok; k++) {
				if (jd_build_named(alt[k], &in[i].g, &l) != 0) continue;
				ok = jd_run(in[i].base, alt[k], in[i].file, in[i].size, &in[i].g, &l, outdir, dst, 1);
				if (!ok) jd_probe(alt[k]);
			}
			continue;
		}
		/* F5 decoded it: put ONE of the first input's values back at a time (what jp6's F3 ran with) */
		for (k = 0; k < 3; k++) {
			static const char *const dn[] = { "F5p", "F5s", "F5w" };
			static const uint32_t dof[3][2] = { { 0x170, 0x170 }, { 0x160, 0x164 }, { 0x16c, 0x16c } };
			if (jd_supp(&in[0].g, dof[k][0]) == jd_supp(&in[i].g, dof[k][0]) && jd_supp(&in[0].g, dof[k][1]) == jd_supp(&in[i].g, dof[k][1])) {
				plog("hwdec %s %s skipped: same 0x%03x/0x%03x as %s", in[i].base, dn[k], dof[k][0], dof[k][1], in[0].base);
				continue;
			}
			if (jd_build_f5(0, &in[i].g, &l) != 0) break;
			jd_set(&l, dof[k][0], jd_supp(&in[0].g, dof[k][0]));
			jd_set(&l, dof[k][1], jd_supp(&in[0].g, dof[k][1]));
			(void)jd_run(in[i].base, dn[k], in[i].file, in[i].size, &in[i].g, &l, outdir, dst, 2);
		}
	}
out:
	jd_dump_window();
	free(dst);
	for (i = 0; i < cnt; i++) free(in[i].file);
	return 0;
}

/* ------------------------------------------------------------------ run */

static int
cmd_run(int argc, char **argv)
{
	const char *gate = NULL, *logpath = NULL, *outdir = "/private/var/db/nord2-vcodec/out";
	const char *sizes_s = "640x480,1920x1080,4032x3024", *q_s = "95,90,60";
	const char *decs[8];
	unsigned ndec = 0, delay = 0, bench = 10, idle_ms = 3500, keep_gate = 0, ladder = 1, dec_self = 1, decode_only = 0, e4 = 1;
	uint32_t ws[8], hs[8];
	unsigned qs[8];
	int nsz, nq, i;
	struct rwj_info in;
	char buf[400];
	int big_ok = -1;

	for (i = 2; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--gate") && v) { gate = v; i++; }
		else if (!strcmp(a, "--keep-gate")) keep_gate = 1;
		else if (!strcmp(a, "--log") && v) { logpath = v; i++; }
		else if (!strcmp(a, "--out") && v) { outdir = v; i++; }
		else if (!strcmp(a, "--delay") && v) { delay = parse_uint(v, 0); i++; }
		else if (!strcmp(a, "--sizes") && v) { sizes_s = v; i++; }
		else if (!strcmp(a, "--q") && v) { q_s = v; i++; }
		else if (!strcmp(a, "--bench") && v) { bench = parse_uint(v, 10); i++; }
		else if (!strcmp(a, "--idle-ms") && v) { idle_ms = parse_uint(v, 3500); i++; }
		else if (!strcmp(a, "--no-ladder")) ladder = 0;
		else if (!strcmp(a, "--no-dec-self")) dec_self = 0;
		else if (!strcmp(a, "--decode-only")) { decode_only = 1; }
		else if (!strcmp(a, "--no-e4")) e4 = 0;
		else if (!strcmp(a, "--dec") && v && ndec < 8) { decs[ndec++] = v; i++; }
		else { fprintf(stderr, "n2jpeg_phone: bad argument %s\n", a); return 1; }
	}
	nsz = parse_list_sizes(sizes_s, ws, hs, 8);
	nq = parse_list_q(q_s, qs, 8);
	if (nsz <= 0 || nq <= 0) { fprintf(stderr, "n2jpeg_phone: bad --sizes/--q\n"); return 1; }

	if (gate) {
		if (access(gate, F_OK) != 0) { printf(P "gate %s absent: not running\n", gate); return 0; }
	}
	if (logpath) {
		g_log = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (g_log >= 0) { (void)dup2(g_log, STDOUT_FILENO); }
	}
	plog("start pid=%d out=%s sizes=%s q=%s bench=%u idle_ms=%u decode_inputs=%u self=%u", (int)getpid(), outdir, sizes_s,
	     q_s, bench, idle_ms, ndec, dec_self);
	if (gate && !keep_gate)
		plog("gate %s %s (one run per installation)", gate, unlink(gate) == 0 ? "removed" : "NOT removed");
	if (delay) { plog("delay %u s", delay); sleep(delay); }
	n2ml_raise(512, buf, sizeof(buf));
	plog("jetsam %s", buf);
	(void)mkdir(outdir, 0755);
	sysctl_note();

	/* S1 */
	if (dev_open() != 0) {
		plog("NODEV open %s: %s", RWJPEG_DEV_PATH, strerror(errno));
		plog("DONE pass=0 fail=1 skip=0 (no device)");
		return 2;
	}
	if (info_get(&in) != 0) {
		plog("NODEV RWJ_INFO: %s", strerror(errno));
		plog("DONE pass=0 fail=1 skip=0 (no RWJ_INFO)");
		return 2;
	}
	info_log("boot", &in);
	verdict(in.abi_version == RWJ_ABI_VERSION && (in.caps & 1u) && in.max_pixels >= 4032u * 3024u,
	        "info abi 1, encoder cap, max_pixels >= 12.2 MP");

	if (decode_only) {
		plog("decode only: S2-S6 (encode) skipped");
		goto s7;
	}
	/* S2: the library's selftest (J02) -- one PASS for its 12/12 */
	plog("S2 library selftest 640x480 (rwjpeg_tool selftest; N2JPG lines)");
	{
		char *av[] = { "rwjpeg_tool", "selftest", "640x480", NULL };
		int rc = n2rwj_tool_main(3, av);
		fflush(stdout);
		verdict(rc == 0, "library selftest 640x480 (11 quality codes + ladder)");
	}

	/* S3 */
	plog("S3 encode matrix (raw ioctl)");
	for (int s = 0; s < nsz; s++) {
		for (int k = 0; k < nq; k++) {
			int ok = encode_case(outdir, ws[s], hs[s], qs[k], 1, "enc");
			if (ws[s] == 4032 && hs[s] == 3024)
				big_ok = big_ok == 0 ? 0 : ok;
			if (!ok) {
				(void)info_get(&in);
				info_log("after failure", &in);
			}
		}
	}
	if (ladder && big_ok >= 0) {
		if (big_ok) {
			plog("S3b 12 MP passed: ABI maximum 4096x3072");
			encode_case(outdir, 4096, 3072, 90, 1, "limit");
		} else {
			static const uint32_t lw[] = { 3840, 3264, 2592 }, lh[] = { 2160, 2448, 1944 };
			plog("S3b 12 MP FAILED: bracketing the block limit");
			for (unsigned l = 0; l < 3; l++)
				if (encode_case(outdir, lw[l], lh[l], 90, 1, "limit")) break;
		}
	}

	/* S4 */
	if (bench) {
		char n[16];
		snprintf(n, sizeof(n), "%u", bench);
		plog("S4 bench (rwjpeg_tool bench; N2JPG lines)");
		char *av[] = { "rwjpeg_tool", "bench", "1920x1080", n, "90", NULL };
		verdict(n2rwj_tool_main(5, av) == 0, "bench 1920x1080 q90");
		if (big_ok > 0) {
			char *av2[] = { "rwjpeg_tool", "bench", "4032x3024", "3", "90", NULL };
			verdict(n2rwj_tool_main(5, av2) == 0, "bench 4032x3024 q90");
		}
		fflush(stdout);
	}

	/* S5 */
	plog("S5 negative ioctls");
	step_negatives();

	/* S6 */
	if (idle_ms) {
		plog("S6 idle %u ms (kernel powers VEN/JPGENC off %u ms after the last job)", idle_ms, RWJ_IDLE_OFF_MS);
		usleep((useconds_t)idle_ms * 1000u);
		(void)info_get(&in);
		info_log("idle", &in);
		verdict(in.powered == 0, "idle off: RWJ_INFO.powered 0");
		{
			uint64_t t0 = now_us();
			int ok = encode_case(outdir, 640, 480, 90, 0, "after idle (power on again)");
			plog("after idle: wall incl. power-on %llu us", (unsigned long long)(now_us() - t0));
			(void)ok;
		}
	}

s7:
	/* S7 */
	plog("S7 decode");
	{
		const char *dl[10];
		unsigned dn = 0;
		char self[600];
		snprintf(self, sizeof(self), "%s/jpeg-640x480-q90.jpg", outdir);
		(void)info_get(&in);
		if (in.reserved[0] == RWJD_REV) {
			plog("S7 kernel decoder present (RWJ_INFO.reserved[0] = %u): RWJD_JOB steps E0/E1, G0, F5 (F0..F4, E2, E3%s)", in.reserved[0],
			     e4 ? ", E4a..i" : "");
			for (unsigned d = 0; d < ndec && dn < 9; d++) dl[dn++] = decs[d];
			if (dec_self && access(self, R_OK) == 0 && dn < 10) dl[dn++] = self;   /* the phone's own JPEG last */
			hw_decode_all(dl, dn, outdir, e4);
		} else {
			plog("S7 kernel without RWJD (reserved[0] = %u): RWJ_DECODE query path", in.reserved[0]);
			if (dec_self) {
				if (access(self, R_OK) == 0) decode_one(self, outdir);
				else plog("dec self: %s absent (640x480 q90 not in the matrix or failed)", self);
			}
			for (unsigned d = 0; d < ndec; d++)
				decode_one(decs[d], outdir);
		}
	}

	(void)info_get(&in);
	info_log("end", &in);
	plog("DONE pass=%u fail=%u skip=%u", g_pass, g_fail, g_skip);
	free(g_pic);
	free(g_jpeg);
	if (g_fd >= 0 && g_fd != 3) close(g_fd);
	return g_fail == 0 ? 0 : 1;
}

static int
cmd_parse(const char *path)
{
	uint8_t *file = NULL;
	size_t size = 0;
	n2jdec_jpeg j;
	if (read_file(path, &file, &size) != 0) return 1;
	int rc = n2jdec_parse(file, size, &j);
	printf("parse %s: %s %ux%u comps=%u out_fmt=%u stride_y=%u stride_c=%u supported=%d\n", path, n2jdec_strerror(rc),
	       j.width, j.height, j.ncomp, j.out_fmt, j.comp[0].stride, j.ncomp == 3 ? j.comp[1].stride : 0,
	       rc == 0 ? n2jdec_words_supported(&j) : 0);
	free(file);
	return rc == 0 ? 0 : 1;
}

int
main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "run"))
		return cmd_run(argc, argv);
	if (argc == 4 && !strcmp(argv[1], "pattern")) {
		char *av[] = { "rwjpeg_tool", "pattern", argv[2], argv[3], NULL };
		return n2rwj_tool_main(4, av);
	}
	if (argc == 3 && !strcmp(argv[1], "parse"))
		return cmd_parse(argv[2]);
	fprintf(stderr, "usage: n2jpeg_phone run [options] | pattern WxH OUT.nv12 | parse FILE.jpg\n");
	return 1;
}
