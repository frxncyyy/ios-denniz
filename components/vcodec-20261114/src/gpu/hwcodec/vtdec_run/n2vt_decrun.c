/*
 * n2vt_decrun.c -- real-size hardware decode through VideoToolbox (decrun, 12 Nov 2026).
 *
 * For every clip given on the command line (raw Annex-B .h264 / .h265 elementary stream):
 *   split into access units (VCL NALs only, 4-byte length prefixes), build the CMVideoFormatDescription from the stream's
 *   own parameter sets (as gpu/video/probe/probe_vt.c does for its embedded vectors), create a VTDecompressionSession with
 *   NO decoder specification and a 420v destination (same as the probe), decode every access unit synchronously, read
 *   UsingHardwareAcceleratedVideoDecoder, and md5 the luma plane (width bytes per row) of every returned picture.
 *
 * One console line per clip (/dev/console, greppable):
 *   N2VTDEC clip=NAME codec=avc1|hvc1 WxH frames=GOT/SENT fps=F dec_hw=X md5first=M md5all=A md5set=S status=... wall_ms=T
 *   md5all = md5 of the per-frame hex md5 lines ("<md5>\n") in callback order; md5set = the same over the sorted lines
 *   (order-independent).  gpu/hwcodec/vtdec_run/expected_md5.sh computes both from ffmpeg's decode on the host.
 * Per-frame lines "N2VTDEC_F clip=NAME i=N md5=M" go to --log (fsync per line) and, with --console-frames, to the console.
 * Final line: N2VTDEC DONE clips=N ok=K.
 *
 * usage: n2vt_decrun [--gate FILE] [--delay S] [--log FILE] [--console-frames] [--max-frames N] CLIP...
 *   --gate: exit 0 at once unless FILE exists; removed at start (one run per installation, like n2vcd_probe).
 * Host self-test: cc -DN2DR_HOST n2vt_decrun.c (no VideoToolbox: prints the access-unit split per clip).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2DR_HOST
#include "probe_apple.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#ifdef N2DR_EXPECTED
#include "n2vt_expected.h"   /* dr5: ffmpeg's per-frame luma md5 (display order), generated from expected.json by build_ios.sh */
#endif


/* local libc helpers: gpu/video's hand-kept libSystem.tbd lacks atoi/qsort/strrchr/fread/fprintf */
static int my_atoi(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
static const char *my_strrchr(const char *s, int ch) { const char *r = NULL; for (; *s; s++) if (*s == ch) r = s; return r; }
static void sort_md5(char (*l)[33], int n)   /* insertion sort: at most a few hundred entries */
{
	int i, j; char t[33];
	for (i = 1; i < n; i++) { memcpy(t, l[i], 33); for (j = i - 1; j >= 0 && memcmp(l[j], t, 33) > 0; j--) memcpy(l[j + 1], l[j], 33); memcpy(l[j + 1], t, 33); }
}

/* ------------------------------------------------------------------ md5 (RFC 1321) ------------------------------------------- */
struct md5 { uint32_t h[4]; uint64_t len; uint8_t buf[64]; size_t n; };
static const uint32_t MD5K[64] = {
	0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,
	0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,
	0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,0xfffa3942,
	0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,
	0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,
	0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
static const uint8_t MD5R[64] = { 7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
	4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
static void md5_block(struct md5 *m, const uint8_t *p)
{
	uint32_t w[16], a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
	int i;
	for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] | (uint32_t)p[4*i+1] << 8 | (uint32_t)p[4*i+2] << 16 | (uint32_t)p[4*i+3] << 24;
	for (i = 0; i < 64; i++) {
		uint32_t f; int g;
		if (i < 16) { f = (b & c) | (~b & d); g = i; }
		else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) & 15; }
		else if (i < 48) { f = b ^ c ^ d; g = (3*i + 5) & 15; }
		else { f = c ^ (b | ~d); g = (7*i) & 15; }
		f += a + MD5K[i] + w[g]; a = d; d = c; c = b; b += (f << MD5R[i]) | (f >> (32 - MD5R[i]));
	}
	m->h[0] += a; m->h[1] += b; m->h[2] += c; m->h[3] += d;
}
static void md5_init(struct md5 *m) { m->h[0]=0x67452301; m->h[1]=0xefcdab89; m->h[2]=0x98badcfe; m->h[3]=0x10325476; m->len=0; m->n=0; }
static void md5_add(struct md5 *m, const uint8_t *p, size_t n)
{
	m->len += n;
	while (n) {
		size_t k = 64 - m->n; if (k > n) k = n;
		memcpy(m->buf + m->n, p, k); m->n += k; p += k; n -= k;
		if (m->n == 64) { md5_block(m, m->buf); m->n = 0; }
	}
}
static void md5_hex(struct md5 *m, char hex[33])
{
	uint64_t bits = m->len * 8; uint8_t pad = 0x80, z = 0, l[8]; int i;
	md5_add(m, &pad, 1);
	while (m->n != 56) md5_add(m, &z, 1);
	for (i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (8*i));
	md5_add(m, l, 8);
	for (i = 0; i < 16; i++) snprintf(hex + 2*i, 3, "%02x", (unsigned)((m->h[i/4] >> (8*(i%4))) & 0xff));
}

/* ------------------------------------------------------------------ output ------------------------------------------------ */
static int g_memlimit_ok = 1;   /* dr6: 0 when the raise failed: clips over 4 MB are skipped instead of being killed */
static int g_log = -1, g_console_frames, g_rehash_ms, g_pace_ms, g_rehash_changed;
static void out_line(const char *s, int console)
{
	size_t n = strlen(s);
	ssize_t r;
	if (console) {
#ifdef N2DR_HOST
		r = write(1, s, n);
#else
		int fd = open("/dev/console", O_WRONLY | O_NONBLOCK | O_NOCTTY);
		if (fd >= 0) { r = write(fd, s, n); close(fd); }
		r = write(1, s, n);   /* StandardOutPath of the job too */
#endif
	}
	if (g_log >= 0) { r = write(g_log, s, n); fsync(g_log); }
	(void)r;
}

#ifndef N2DR_HOST
static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}
#endif

/* ------------------------------------------------------------------ Annex-B split ------------------------------------------ */
struct nal { const uint8_t *p; size_t n; };
struct au { size_t off, len; };            /* into the length-prefixed sample buffer */
struct clip {
	uint32_t codec; int hevc;
	const uint8_t *ps[8]; size_t ps_n[8]; int nps;
	uint8_t *samples; size_t samples_len, samples_cap;
	struct au *aus; int naus, caus;
};

static int next_nal(const uint8_t *d, size_t n, size_t *pos, struct nal *out)
{
	size_t i = *pos, s;
	while (i + 3 <= n && !(d[i] == 0 && d[i+1] == 0 && d[i+2] == 1)) i++;
	if (i + 3 > n) return 0;
	s = i + 3; i = s;
	while (i + 3 <= n && !(d[i] == 0 && d[i+1] == 0 && (d[i+2] == 1 || (d[i+2] == 0 && i + 3 < n && d[i+3] == 1)))) i++;
	if (i + 3 > n) i = n;
	out->p = d + s; out->n = i - s;
	while (out->n && out->p[out->n - 1] == 0) out->n--;   /* trailing zero bytes before the next start code */
	*pos = i;
	return out->n > 0;
}

static void add_ps(struct clip *c, struct nal *x)
{
	int i;
	for (i = 0; i < c->nps; i++)
		if (c->ps_n[i] == x->n && !memcmp(c->ps[i], x->p, x->n)) return;
	if (c->nps < 8) { c->ps[c->nps] = x->p; c->ps_n[c->nps] = x->n; c->nps++; }
}

static int push_bytes(struct clip *c, const uint8_t *p, size_t n)
{
	if (c->samples_len + n + 4 > c->samples_cap) {
		size_t cap = c->samples_cap ? c->samples_cap * 2 : (1u << 20);
		uint8_t *q;
		while (cap < c->samples_len + n + 4) cap *= 2;
		q = realloc(c->samples, cap); if (!q) return -1;
		c->samples = q; c->samples_cap = cap;
	}
	c->samples[c->samples_len++] = (uint8_t)(n >> 24); c->samples[c->samples_len++] = (uint8_t)(n >> 16);
	c->samples[c->samples_len++] = (uint8_t)(n >> 8); c->samples[c->samples_len++] = (uint8_t)n;
	memcpy(c->samples + c->samples_len, p, n); c->samples_len += n;
	return 0;
}

static int split(struct clip *c, const uint8_t *d, size_t n)
{
	size_t pos = 0; struct nal x; int in_au = 0;
	while (next_nal(d, n, &pos, &x)) {
		int vcl, first = 0, type;
		if (c->hevc) {
			if (x.n < 3) continue;
			type = (x.p[0] >> 1) & 0x3f;
			if (type == 32 || type == 33 || type == 34) { add_ps(c, &x); continue; }
			vcl = type < 32;
			if (vcl) first = (x.p[2] & 0x80) != 0;
		} else {
			if (x.n < 2) continue;
			type = x.p[0] & 0x1f;
			if (type == 7 || type == 8) { add_ps(c, &x); continue; }
			vcl = type >= 1 && type <= 5;
			if (vcl) first = (x.p[1] & 0x80) != 0;   /* first_mb_in_slice == 0 */
		}
		if (!vcl) continue;                         /* AUD, SEI, EOS...: not needed by the decoder */
		if (first || !in_au) {
			if (c->naus == c->caus) {
				int cap = c->caus ? c->caus * 2 : 256;
				struct au *q = realloc(c->aus, sizeof *q * (size_t)cap); if (!q) return -1;
				c->aus = q; c->caus = cap;
			}
			c->aus[c->naus].off = c->samples_len; c->aus[c->naus].len = 0; c->naus++; in_au = 1;
		}
		if (push_bytes(c, x.p, x.n)) return -1;
		c->aus[c->naus - 1].len = c->samples_len - c->aus[c->naus - 1].off;
	}
	return 0;
}

static uint8_t *read_file(const char *path, size_t *n)
{
	int f = open(path, O_RDONLY); uint8_t *b = NULL; size_t cap = 0, len = 0; ssize_t r;
	if (f < 0) return NULL;
	for (;;) {
		if (len == cap) { uint8_t *q; cap = cap ? cap * 2 : (1u << 20); q = realloc(b, cap); if (!q) { free(b); close(f); return NULL; } b = q; }
		r = read(f, b + len, cap - len);
		if (r <= 0) break;
		len += (size_t)r;
	}
	close(f); *n = len; return b;
}

static const char *base_name(const char *p) { const char *s = my_strrchr(p, '/'); return s ? s + 1 : p; }


/* md5all / md5set of the per-frame list */
static void digests(char (*list)[33], int n, char all[33], char set[33])
{
	struct md5 m; int i;
	md5_init(&m);
	for (i = 0; i < n; i++) { md5_add(&m, (const uint8_t *)list[i], 32); md5_add(&m, (const uint8_t *)"\n", 1); }
	md5_hex(&m, all);
	sort_md5(list, n);
	md5_init(&m);
	for (i = 0; i < n; i++) { md5_add(&m, (const uint8_t *)list[i], 32); md5_add(&m, (const uint8_t *)"\n", 1); }
	md5_hex(&m, set);
}

/* ------------------------------------------------------------------ VideoToolbox ------------------------------------------- */
#ifndef N2DR_HOST
struct dec_state { const char *name; int got, ok, first_status, w, h; char (*md5)[33]; int cap; };

static void dec_cb(void *refcon, void *src, OSStatus status, uint32_t flags, CVImageBufferRef img, CMTime pts, CMTime dur)
{
	struct dec_state *s = refcon;
	size_t w, h, stride, y; uint8_t *base; struct md5 m; char hex[33], line[160];
	(void)src; (void)dur;
	s->got++;
	if (status != 0 || !img) { if (!s->first_status) s->first_status = status ? (int)status : -1; return; }
	w = CVPixelBufferGetWidth(img); h = CVPixelBufferGetHeight(img);
	if (CVPixelBufferGetPlaneCount(img) < 1 || CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly) != 0) return;
	base = CVPixelBufferGetBaseAddressOfPlane(img, 0); stride = CVPixelBufferGetBytesPerRowOfPlane(img, 0);
	if (base && stride >= w) {
		md5_init(&m);
		for (y = 0; y < h; y++) md5_add(&m, base + y * stride, w);
		md5_hex(&m, hex);
		if (g_rehash_ms > 0) {   /* dr3 diagnostic: does the picture change under us after delivery (surface reuse)? */
			struct timespec ts; char hex2[33];
			ts.tv_sec = g_rehash_ms / 1000; ts.tv_nsec = (long)(g_rehash_ms % 1000) * 1000000L;
			nanosleep(&ts, NULL);
			md5_init(&m);
			for (y = 0; y < h; y++) md5_add(&m, base + y * stride, w);
			md5_hex(&m, hex2);
			if (memcmp(hex, hex2, 32)) { g_rehash_changed++; snprintf(line, sizeof line, "N2VTDEC_CHANGED clip=%s i=%d first=%s later=%s\n", s->name, s->ok, hex, hex2); out_line(line, 1); }
		}
		if (s->ok < s->cap) memcpy(s->md5[s->ok], hex, 33);
		snprintf(line, sizeof line, "N2VTDEC_F clip=%s i=%d md5=%s %zux%zu pts=%lld fl=%x\n", s->name, s->ok, hex, w, h,
		         (long long)pts.value, (unsigned)flags);   /* dr2: pts = decode index of the sample VT attributes it to */
		out_line(line, g_console_frames);
		s->ok++; s->w = (int)w; s->h = (int)h;
	}
	CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
}

static CFStringRef k_using_hw_dec(void)
{
	void *p = dlsym(N2_RTLD_DEFAULT, "kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder");
	return p ? *(const CFStringRef *)p
	         : CFStringCreateWithCString(NULL, "UsingHardwareAcceleratedVideoDecoder", kCFStringEncodingUTF8);
}

static CFDictionaryRef make_420v_attrs(void)
{
	CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	int32_t fmt = (int32_t)N2_PIXFMT_420V; CFNumberRef n;
	if (!d) return NULL;
	n = CFNumberCreate(NULL, kCFNumberSInt32Type, &fmt);
	if (n) { CFDictionarySetValue(d, kCVPixelBufferPixelFormatTypeKey, n); CFRelease(n); }
	return d;
}

static int run_clip(const char *path, const char *name, struct clip *c, int max_frames)
{
	CMFormatDescriptionRef fmt = NULL; VTDecompressionSessionRef session = NULL; VTDecompressionOutputCallbackRecord cb;
	CFDictionaryRef attrs; OSStatus st; struct dec_state s; int i, hw = -1, sent = 0, first_err = 0, n = c->naus;
	double t0, t1; char line[512], all[33] = "-", set[33] = "-", first[33] = "-";
	(void)path;

	if (max_frames > 0 && n > max_frames) n = max_frames;
	memset(&s, 0, sizeof s); s.name = name; s.cap = n + 16; s.md5 = calloc((size_t)s.cap, 33);
	if (c->hevc) st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(NULL, (size_t)c->nps, c->ps, c->ps_n, 4, NULL, &fmt);
	else st = CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, (size_t)c->nps, c->ps, c->ps_n, 4, &fmt);
	if (st != 0 || !fmt) {
		snprintf(line, sizeof line, "N2VTDEC clip=%s codec=%s 0x0 frames=0/%d fps=0 dec_hw=-1 md5first=- status=fmt:%d nps=%d\n",
		         name, c->hevc ? "hvc1" : "avc1", n, (int)st, c->nps);
		out_line(line, 1); free(s.md5); return 1;
	}
	attrs = make_420v_attrs();
	memset(&cb, 0, sizeof cb); cb.decompressionOutputCallback = dec_cb; cb.decompressionOutputRefCon = &s;
	st = VTDecompressionSessionCreate(NULL, fmt, NULL, attrs, &cb, &session);
	if (st != 0 || !session) {
		snprintf(line, sizeof line, "N2VTDEC clip=%s codec=%s 0x0 frames=0/%d fps=0 dec_hw=-1 md5first=- status=session:%d\n",
		         name, c->hevc ? "hvc1" : "avc1", n, (int)st);
		out_line(line, 1); if (attrs) CFRelease(attrs); CFRelease(fmt); free(s.md5); return 1;
	}
	{ CFTypeRef v = NULL; if (VTSessionCopyProperty((VTSessionRef)session, k_using_hw_dec(), NULL, &v) == 0 && v) {
		if (CFGetTypeID(v) == CFBooleanGetTypeID()) hw = CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0; CFRelease(v); } }
	t0 = now_ms();
	for (i = 0; i < n; i++) {
		CMBlockBufferRef bb = NULL; CMSampleBufferRef sb = NULL; CMSampleTimingInfo tm; size_t sz = c->aus[i].len;
		VTDecodeInfoFlags info = 0;
		st = CMBlockBufferCreateWithMemoryBlock(NULL, NULL, sz, kCFAllocatorDefault, NULL, 0, sz, N2_kCMBlockBufferAssureMemoryNowFlag, &bb);
		if (st == 0) st = CMBlockBufferReplaceDataBytes(c->samples + c->aus[i].off, bb, 0, sz);
		memset(&tm, 0, sizeof tm);
		tm.duration.value = 1; tm.duration.timescale = 30; tm.duration.flags = N2_CMTIME_VALID;
		tm.presentationTimeStamp.value = i; tm.presentationTimeStamp.timescale = 30; tm.presentationTimeStamp.flags = N2_CMTIME_VALID;
		if (st == 0) st = CMSampleBufferCreateReady(NULL, bb, fmt, 1, 1, &tm, 1, &sz, &sb);
		if (bb) CFRelease(bb);
		if (st == 0) { st = VTDecompressionSessionDecodeFrame(session, sb, 0, NULL, &info); CFRelease(sb); }
		if (g_pace_ms > 0) { struct timespec ts; ts.tv_sec = g_pace_ms / 1000; ts.tv_nsec = (long)(g_pace_ms % 1000) * 1000000L; nanosleep(&ts, NULL); }
		sent++;
		if (st != 0 && !first_err) first_err = (int)st;
		if (st != 0 && first_err && i > 4 && s.ok == 0) break;   /* nothing decodes: stop early */
	}
	VTDecompressionSessionWaitForAsynchronousFrames(session);
	t1 = now_ms();
	if (hw < 0) { CFTypeRef v = NULL; if (VTSessionCopyProperty((VTSessionRef)session, k_using_hw_dec(), NULL, &v) == 0 && v) {
		if (CFGetTypeID(v) == CFBooleanGetTypeID()) hw = CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0; CFRelease(v); } }
	VTDecompressionSessionInvalidate(session); CFRelease(session); CFRelease(fmt); if (attrs) CFRelease(attrs);
#ifdef N2DR_EXPECTED
	{   /* dr5: compare on the phone; console gets one compact line + at most 8 mismatching frames */
		const char *base = name; char bname[160]; size_t bl; int e, k, eq = 0, nb = 0, shown = 0; char bad[200]; size_t bu = 0;
		const char *at = my_strrchr(name, '@');
		bl = at ? (size_t)(at - name) : strlen(name); if (bl >= sizeof bname) bl = sizeof bname - 1;
		memcpy(bname, name, bl); bname[bl] = 0; base = bname; bad[0] = 0;
		for (e = 0; e < N2DR_NEXP; e++) if (!strcmp(n2dr_exp[e].clip, base)) break;
		if (e < N2DR_NEXP) {
			for (k = 0; k < s.ok && k < s.cap; k++) {
				int okf = k < n2dr_exp[e].n && !memcmp(s.md5[k], n2dr_exp[e].md5[k], 32);
				if (okf) { eq++; continue; }
				nb++;
				if (bu + 8 < sizeof bad) bu += (size_t)snprintf(bad + bu, sizeof bad - bu, "%s%d", bu ? "," : "", k);
				if (shown < 8) { shown++; snprintf(line, sizeof line, "N2VTDEC_F clip=%s i=%d md5=%s want=%s\n", name, k, s.md5[k],
				                                   k < n2dr_exp[e].n ? n2dr_exp[e].md5[k] : "-"); out_line(line, 1); }
			}
			snprintf(line, sizeof line, "N2VTDEC_CMP clip=%s equal=%d/%d expected=%d bad=%d first_bad=%s\n", name, eq, s.ok, n2dr_exp[e].n, nb,
			         bad[0] ? bad : "-");
		} else snprintf(line, sizeof line, "N2VTDEC_CMP clip=%s no-reference\n", name);
		out_line(line, 1);
	}
#endif
	if (s.ok > 0) { memcpy(first, s.md5[0], 33); digests(s.md5, s.ok < s.cap ? s.ok : s.cap, all, set); }
	snprintf(line, sizeof line,
	         "N2VTDEC clip=%s codec=%s %dx%d frames=%d/%d fps=%.1f dec_hw=%d md5first=%s md5all=%s md5set=%s status=%d cb_err=%d wall_ms=%.0f\n",
	         name, c->hevc ? "hvc1" : "avc1", s.w, s.h, s.ok, sent, (t1 > t0 && s.ok) ? s.ok * 1000.0 / (t1 - t0) : 0.0, hw,
	         first, all, set, first_err, s.first_status, t1 - t0);
	out_line(line, 1);
	free(s.md5);
	return (s.ok == n && first_err == 0) ? 0 : 1;
}
#endif

#ifndef N2DR_HOST
/* dr6: phone dr5 killed this tool (sig 9, reason ns 1 code 0x7 = jetsam per-process limit) while it read the 27 MB camera
 * stream: the launchd JetsamMemoryLimit does not apply on this port (vc22, include/n2memlimit.h), the effective limit is
 * ~6 MB.  Raise our own limit like vcodecd / n2vcd_dprobe (memorystatus_control, root), looked up with dlsym so no TBD
 * change is needed.  Commands from xnu bsd/sys/kern_memorystatus.h (7 = SET_MEMLIMIT_PROPERTIES, 8 = GET). */
struct n2dr_mlprops { int32_t active; uint32_t active_attr; int32_t inactive; uint32_t inactive_attr; };
static void raise_memlimit(int32_t mb)
{
	typedef int (*msc_t)(uint32_t, int32_t, uint32_t, void *, size_t);
	msc_t msc = (msc_t)dlsym(N2_RTLD_DEFAULT, "memorystatus_control");
	struct n2dr_mlprops before, want, after; int rs = -2, ra = -2; char line[200];
	memset(&before, 0, sizeof before); memset(&after, 0, sizeof after);
	if (msc) {
		(void)msc(8, (int32_t)getpid(), 0, &before, sizeof before);
		want.active = mb; want.active_attr = 1; want.inactive = mb; want.inactive_attr = 1;
		rs = msc(7, (int32_t)getpid(), 0, &want, sizeof want);
		ra = msc(8, (int32_t)getpid(), 0, &after, sizeof after);
	}
	snprintf(line, sizeof line, "N2VTDEC MEMLIMIT %s before=%d set=%d MB rc=%d after=%d rc=%d\n", msc ? "memorystatus_control" : "absent",
	         before.active, mb, rs, after.active, ra);
	out_line(line, 1);
	g_memlimit_ok = msc && ra == 0 && after.active >= mb;
}
#endif

int main(int argc, char **argv)
{
	const char *gate = NULL, *logp = NULL; int delay = 0, max_frames = 0, i, nclips = 0, ok = 0, pass, repeat_rehash = 0; char line[256], cname[200];
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--gate") && i + 1 < argc) gate = argv[++i];
		else if (!strcmp(argv[i], "--delay") && i + 1 < argc) delay = my_atoi(argv[++i]);
		else if (!strcmp(argv[i], "--log") && i + 1 < argc) logp = argv[++i];
		else if (!strcmp(argv[i], "--max-frames") && i + 1 < argc) max_frames = my_atoi(argv[++i]);
		else if (!strcmp(argv[i], "--console-frames")) g_console_frames = 1;
		else if (!strcmp(argv[i], "--rehash-ms") && i + 1 < argc) g_rehash_ms = my_atoi(argv[++i]);
		else if (!strcmp(argv[i], "--pace-ms") && i + 1 < argc) g_pace_ms = my_atoi(argv[++i]);
		else if (!strcmp(argv[i], "--repeat-rehash-ms") && i + 1 < argc) repeat_rehash = my_atoi(argv[++i]);
		else if (argv[i][0] == '-' && argv[i][1] == '-') { out_line("n2vt_decrun: bad argument\n", 1); return 2; }
	}
	if (gate) {
		if (access(gate, F_OK) != 0) return 0;
		unlink(gate);
	}
	if (logp) g_log = open(logp, O_WRONLY | O_CREAT | O_APPEND, 0644);
	snprintf(line, sizeof line, "N2VTDEC START delay=%d pid=%d\n", delay, (int)getpid());
	out_line(line, 1);
#ifndef N2DR_HOST
	raise_memlimit(1024);   /* dr6: before any clip is read */
#endif
	if (delay > 0) sleep((unsigned)delay);
	/* dr4: --repeat-rehash-ms R runs every clip a second time with --rehash-ms R; that pass's clip names get "@rR" */
	for (pass = 0; pass < (repeat_rehash > 0 ? 2 : 1); pass++) {
	if (pass == 1) { g_rehash_ms = repeat_rehash; snprintf(line, sizeof line, "N2VTDEC PASS 2 rehash_ms=%d\n", repeat_rehash); out_line(line, 1); }
	for (i = 1; i < argc; i++) {
		struct clip c; uint8_t *data; size_t n; const char *a = argv[i], *ext;
		if (!strcmp(a, "--gate") || !strcmp(a, "--delay") || !strcmp(a, "--log") || !strcmp(a, "--max-frames") ||
		    !strcmp(a, "--rehash-ms") || !strcmp(a, "--pace-ms") || !strcmp(a, "--repeat-rehash-ms")) { i++; continue; }
		if (a[0] == '-') continue;
		memset(&c, 0, sizeof c); nclips++;
		ext = my_strrchr(a, '.');
		c.hevc = ext && (!strcmp(ext, ".h265") || !strcmp(ext, ".hevc"));
		if (!g_memlimit_ok) {
			int fd = open(a, O_RDONLY); off_t sz = fd >= 0 ? lseek(fd, 0, SEEK_END) : 0;
			if (fd >= 0) close(fd);
			if (sz > 4 * 1024 * 1024) {
				snprintf(line, sizeof line, "N2VTDEC clip=%s codec=%s 0x0 frames=0/0 fps=0 dec_hw=-1 md5first=- status=skipped:memlimit bytes=%lld\n",
				         base_name(a), c.hevc ? "hvc1" : "avc1", (long long)sz);
				out_line(line, 1); continue;
			}
		}
		data = read_file(a, &n);
		if (!data || split(&c, data, n) || c.naus == 0 || c.nps < (c.hevc ? 3 : 2)) {
			snprintf(line, sizeof line, "N2VTDEC clip=%s codec=%s 0x0 frames=0/0 fps=0 dec_hw=-1 md5first=- status=read:%s aus=%d nps=%d\n",
			         base_name(a), c.hevc ? "hvc1" : "avc1", data ? "parse" : "open", c.naus, c.nps);
			out_line(line, 1); free(data); free(c.samples); free(c.aus); continue;
		}
#ifdef N2DR_HOST
		snprintf(line, sizeof line, "N2VTDEC_HOST clip=%s codec=%s aus=%d nps=%d bytes=%zu\n", base_name(a), c.hevc ? "hvc1" : "avc1",
		         c.naus, c.nps, c.samples_len);
		out_line(line, 1); ok++;
		(void)max_frames; (void)digests;
#else
		if (pass) snprintf(cname, sizeof cname, "%s@r%d", base_name(a), g_rehash_ms);
		else snprintf(cname, sizeof cname, "%s", base_name(a));
		if (run_clip(a, cname, &c, max_frames) == 0) ok++;
#endif
		free(data); free(c.samples); free(c.aus);
	}
	}
	snprintf(line, sizeof line, "N2VTDEC DONE clips=%d ok=%d changed_after_delivery=%d rehash_ms=%d pace_ms=%d\n", nclips, ok,
	         g_rehash_changed, g_rehash_ms, g_pace_ms);
	out_line(line, 1);
	if (g_log >= 0) close(g_log);
	return ok == nclips ? 0 : 1;
}
