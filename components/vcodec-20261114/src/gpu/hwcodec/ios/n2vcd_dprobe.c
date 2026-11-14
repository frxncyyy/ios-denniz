/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dprobe.c -- boot-time, hands-free DECODE test client of vcodecd on the phone.  Owner: opus-decint (2026-11-11).
 *
 * The decode twin of n2vcd_probe.c.  Started by the launchd job com.nord2.vcodec-dprobe (guest-vcodec.sh, staged by
 * stage-vcodec.sh --decoder):
 *
 *   gate (one-shot, removed BEFORE any request) -> start delay -> optional wait for the encode probe's DONE line
 *   -> wait for vcodecd (RWVC_INFO.server_present) -> HELLO (dec_codecs must name the codec) -> OPEN decoder MM21
 *   -> DEC_HEADER (first access unit) -> DEC_ADD_BUFFER x dpb -> per access unit DEC_INPUT, DEC_PICTURE (display order),
 *   copy the MM21 planes out of the shared DMA buffer (RWVC_DMA_RW), MM21 -> NV12 (mm21/n2_mm21.c), append to
 *   <out>/dec-<pid>.yuv, DEC_RELEASE -> FLUSH + drain -> CLOSE.
 *
 * Wire: include/n2vcd_proto.h rev 2 through the DEC05 client library (client/dec/n2vcd_dec.c) over P03's transport
 * (client/n2vcd_transport.c: relay /dev/redwood-vcodec-client on the phone, SOCK_SEQPACKET + "<socket>.buf" files on
 * the host, vcodecd --dma-dir must point there).
 *
 * Logging: every line goes to stdout (launchd StandardOutPath), to --log FILE (O_APPEND, fsync after every line: a phone
 * reset must not lose the lines before it; console writes from daemons are lost on this port) and, unless --no-console,
 * to /dev/console.  Prefix "N2VCDD".  Per picture: display index, pts, poc, input round trip, daemon hw_us, detile ms
 * and an FNV-1a 64 hash of the cropped NV12 Y and UV planes (the PC computes the same hash over ffmpeg's decode, so the
 * log alone proves bit-exactness even when the .yuv cannot be fetched).  Last line "N2VCDD DONE ok=1 ..." or "ok=0".
 *
 *   n2vcd_dprobe --in FILE [--codec avc1|hvc1|vp08|vp09|av01|mp2v|mp4v|h263] (repeat --in, up to 8 streams, one session each) [--gate PATH] [--keep-gate] [--delay S] [--wait-server S]
 *                [--after FILE [--after-timeout S]] [--frames N] [--out DIR] [--log FILE] [--no-yuv] [--mm21-first]
 *                [--relay DEV | --socket PATH] [--timeout-ms N] [--no-console] [--serial] [--threads N] [--no-hash | --hash-alt]
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include "n2vcd_proto.h"
#include "redwood_vcodec.h"
#include "n2vcd_dec.h"
#include "n2_mm21.h"
#include "n2memlimit.h"     /* vc22: the launchd JetsamMemoryLimit does not apply; raise our own (6 MB effective) */

static int g_console = -1, g_logfd = -1;
static uint64_t g_t0;

static uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static pthread_mutex_t g_say_mu = PTHREAD_MUTEX_INITIALIZER;

/* one log line; sync = fsync the log file (every line except the per-frame ones: an fsync per picture would cost
 * milliseconds on the phone's flash at 60 fps and distort the timing; the stream summary is synced) */
static void vsay(int sync, const char *fmt, va_list ap)
{
	char buf[480];
	uint64_t t = now_us() - g_t0;
	int n = snprintf(buf, sizeof buf, "N2VCDD %llu.%03llu ", (unsigned long long)(t / 1000000u),
	                 (unsigned long long)(t / 1000u % 1000u));
	vsnprintf(buf + n, sizeof buf - (size_t)n - 1, fmt, ap);
	size_t len = strnlen(buf, sizeof buf - 2);
	for (size_t i = 0; i < len; i++) if ((unsigned char)buf[i] < 0x20) buf[i] = ' ';
	buf[len++] = '\n';
	pthread_mutex_lock(&g_say_mu);
	fwrite(buf, 1, len, stdout);
	fflush(stdout);
	if (g_logfd >= 0) { (void)!write(g_logfd, buf, len); if (sync) (void)fsync(g_logfd); }
	if (g_console >= 0 && sync) (void)!write(g_console, buf, len);
	pthread_mutex_unlock(&g_say_mu);
}
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsay(1, fmt, ap);
	va_end(ap);
}
static void say_ns(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say_ns(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsay(0, fmt, ap);
	va_end(ap);
}

static void sleep_ms(unsigned ms)
{
	struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

/* ------------------------------------------------------------------------------------------- access units */

struct au { uint32_t off, len; };

static int is_vcl(int hevc, unsigned t) { return hevc ? t <= 31u : (t >= 1u && t <= 5u); }

/* Annex B -> access units.  A new AU starts at a parameter set / SEI / AUD (H.264 6..9, HEVC 32..40) or at a VCL NAL
 * whose first-slice flag is set (H.264 first_mb_in_slice == 0: top bit of the first slice-header byte; HEVC
 * first_slice_segment_in_pic_flag: top bit after the 2-byte NAL header), once the current AU already holds a VCL NAL.
 * Leading bytes before the first start code are attached to the first AU. */
static unsigned split_aus(const uint8_t *b, uint32_t n, int hevc, struct au *out, unsigned max)
{
	unsigned count = 0;
	int have_vcl = 0;
	uint32_t au_start = 0;
	int started = 0;
	for (uint32_t i = 0; i + 3u < n; i++) {
		if (!(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1)) continue;
		uint32_t sc = (i > 0 && b[i - 1] == 0) ? i - 1 : i;     /* include the 4-byte form's leading zero */
		uint32_t nal = i + 3u;
		i += 2;
		if (nal >= n) break;
		unsigned t = hevc ? (b[nal] >> 1) & 63u : b[nal] & 31u;
		uint32_t hdr = hevc ? 2u : 1u;
		int first_slice = nal + hdr < n && (b[nal + hdr] & 0x80u);
		int boundary = 0;
		if (is_vcl(hevc, t)) {
			if (have_vcl && first_slice) boundary = 1;
		} else {
			int lead = hevc ? (t >= 32u && t <= 40u) : (t >= 6u && t <= 9u);
			if (have_vcl && lead) boundary = 1;
		}
		if (!started) { started = 1; au_start = 0; }
		if (boundary) {
			if (count < max) { out[count].off = au_start; out[count].len = sc - au_start; }
			count++;
			au_start = sc;
			have_vcl = 0;
		}
		if (is_vcl(hevc, t)) have_vcl = 1;
	}
	if (started && have_vcl) {
		if (count < max) { out[count].off = au_start; out[count].len = n - au_start; }
		count++;
	}
	return count < max ? count : max;
}

/* opus-fmt: the other decoder formats.  name = CMVideoCodecType fourcc text (include/n2vcd_proto.h), bit = HELLO_OK.dec_codecs. */
enum { K_AVC, K_HEVC, K_IVF, K_MPEG2, K_MPEG4, K_H263 };
static const struct { const char *name; uint32_t proto, bit; int kind; } g_codecs[] = {
	{ "avc1", N2VCD_CODEC_H264, 1u << 0, K_AVC }, { "hvc1", N2VCD_CODEC_HEVC, 1u << 1, K_HEVC },
	{ "vp08", N2VCD_CODEC_VP8, 1u << 2, K_IVF }, { "vp09", N2VCD_CODEC_VP9, 1u << 3, K_IVF },
	{ "av01", N2VCD_CODEC_AV1, 1u << 4, K_IVF }, { "mp2v", N2VCD_CODEC_MPEG2, 1u << 5, K_MPEG2 },
	{ "mp4v", N2VCD_CODEC_MPEG4, 1u << 6, K_MPEG4 }, { "h263", N2VCD_CODEC_H263, 1u << 7, K_H263 },
};
static int codec_index(const char *s)
{
	for (unsigned i = 0; i < sizeof g_codecs / sizeof g_codecs[0]; i++) if (!strcmp(g_codecs[i].name, s)) return (int)i;
	return -1;
}
/* IVF (VP8/VP9/AV1): 32-byte header, then per frame u32 size + u64 pts + payload: one frame (temporal unit) per input. */
static unsigned split_ivf(const uint8_t *b, uint32_t n, struct au *out, unsigned max)
{
	unsigned count = 0;
	uint32_t o = 32;
	if (n < 32 || memcmp(b, "DKIF", 4)) return 0;
	while (o + 12u <= n && count < max) {
		uint32_t sz = (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24;
		if (sz > n - o - 12u) break;
		out[count].off = o + 12u; out[count].len = sz; count++;
		o += 12u + sz;
	}
	return count;
}
/* MPEG-1/2 and MPEG-4 part 2 elementary streams: a unit ends before the first header/picture start code after a picture
 * start code (MPEG-2: picture 00, sequence B3, GOP B8; MPEG-4: VOP B6, VOS B0, GOV B3, VO/VOL 00..2F); headers stay with
 * the following picture.  H.263: one picture per 22-bit PSC (00 00 8x). */
static unsigned split_es(const uint8_t *b, uint32_t n, int kind, struct au *out, unsigned max)
{
	unsigned count = 0;
	uint32_t start = 0;
	int seen_pic = 0;
	for (uint32_t i = 0; i + 3u < n && count < max; i++) {
		int pic, hdr;
		if (kind == K_H263) {
			if (!(b[i] == 0 && b[i + 1] == 0 && (b[i + 2] & 0xfcu) == 0x80u)) continue;
			pic = 1; hdr = 0;
		} else {
			if (!(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1)) continue;
			unsigned c = b[i + 3];
			if (kind == K_MPEG2) { pic = c == 0x00; hdr = c == 0xb3 || c == 0xb8; }
			else { pic = c == 0xb6; hdr = c == 0xb0 || c == 0xb3 || c <= 0x2f; }
		}
		if (seen_pic && (pic || hdr)) { out[count].off = start; out[count].len = i - start; count++; start = i; seen_pic = 0; }
		if (pic) seen_pic = 1;
		i += 2;
	}
	if (seen_pic && count < max) { out[count].off = start; out[count].len = n - start; count++; }
	return count;
}

static uint64_t fnv64(uint64_t h, const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
	return h;
}

/* ---------------------------------------------------------------------------------------------- picture conversion
 * opus-decint (vc27 follow-up): the client-side cost per picture is RWVC_DMA_RW read-back + MM21->NV12 detile + the
 * bit-exactness hash.  Detile and hash are split over g_threads row bands (pthreads, one per band per picture); the read
 * is one ioctl per plane (the kernel serialises it anyway).  Hash "fnvr" (row hash): FNV-1a 64 of every row of the
 * cropped plane, then FNV-1a 64 over those row hashes as 8 little-endian bytes each, rows in order.  It parallelises,
 * the plain FNV of earlier logs does not (17 ms per 4K picture on one core); decint/fnv_frames.c computes both. */
static unsigned g_threads = 4;

struct band {
	const struct n2_mm21_frame *src;
	uint8_t *nv12;
	size_t w, h, cw, ch;
	uint32_t y0, rows;              /* luma rows [y0, y0+rows), y0 even */
	uint64_t *rowh_y, *rowh_c;      /* per-row hashes (whole picture arrays) */
	uint64_t ysum, nz;
	int rc;
};

static void *band_run(void *arg)
{
	struct band *b = (struct band *)arg;
	uint8_t *uv = b->nv12 + b->w * b->h;
	struct n2_nv12_out dst = { b->nv12 + (size_t)b->y0 * b->w, uv + (size_t)(b->y0 / 2u) * b->cw, b->w, b->cw };
	b->rc = n2_mm21_to_nv12(b->src, &dst, 0, b->y0, (uint32_t)b->w, b->rows);
	return NULL;
}

static void *band_hash(void *arg)
{
	struct band *b = (struct band *)arg;
	uint8_t *uv = b->nv12 + b->w * b->h;
	uint64_t ysum = 0, nz = 0;
	for (uint32_t r = b->y0; r < b->y0 + b->rows; r++) {
		const uint8_t *row = b->nv12 + (size_t)r * b->w;
		b->rowh_y[r] = fnv64(0xcbf29ce484222325ull, row, b->w);
		if ((r & 7u) == 0) {      /* the y_mean / y_nonzero sanity figures use every 8th row */
			for (size_t i = 0; i < b->w; i++) { ysum += row[i]; nz += row[i] != 0; }
		}
	}
	uint32_t c0 = b->y0 / 2u, c1 = (b->y0 + b->rows + 1u) / 2u;
	if (c1 > b->ch) c1 = (uint32_t)b->ch;
	for (uint32_t r = c0; r < c1; r++) b->rowh_c[r] = fnv64(0xcbf29ce484222325ull, uv + (size_t)r * b->cw, b->cw);
	b->ysum = ysum;
	b->nz = nz;
	return NULL;
}

/* run fn over n bands: bands 1..n-1 on their own threads, band 0 on the caller */
static void bands_run(struct band *bd, unsigned n, void *(*fn)(void *))
{
	pthread_t th[16];
	unsigned started = 0;
	for (unsigned i = 1; i < n; i++) {
		if (pthread_create(&th[i], NULL, fn, &bd[i]) == 0) started |= 1u << i;
		else fn(&bd[i]);
	}
	fn(&bd[0]);
	for (unsigned i = 1; i < n; i++) if (started & (1u << i)) pthread_join(th[i], NULL);
}

static uint64_t fnv_rows(const uint64_t *rowh, size_t rows)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t r = 0; r < rows; r++) {
		uint8_t le[8];
		for (unsigned k = 0; k < 8; k++) le[k] = (uint8_t)(rowh[r] >> (8u * k));
		h = fnv64(h, le, 8);
	}
	return h;
}

static int read_file(const char *path, uint8_t **out, uint32_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f) return -errno;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -EIO; }
	long sz = ftell(f);
	if (sz <= 0 || sz > (64l << 20)) { fclose(f); return -EFBIG; }
	rewind(f);
	uint8_t *b = (uint8_t *)malloc((size_t)sz);
	if (!b) { fclose(f); return -ENOMEM; }
	if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return -EIO; }
	fclose(f);
	*out = b;
	*n = (uint32_t)sz;
	return 0;
}

/* Wait until FILE gains "DONE " (the encode probe's last line) or "absent: not running" (its gate is off) AFTER byte
 * `from` (its size when this process started: launchd appends, so the previous boot's DONE is already there), or the
 * timeout passes; never an error. */
static void wait_after(const char *path, unsigned timeout_s, uint32_t from)
{
	uint64_t end = now_us() + (uint64_t)timeout_s * 1000000u;
	for (;;) {
		uint8_t *b = NULL;
		uint32_t n = 0;
		int found = 0;
		if (read_file(path, &b, &n) == 0) {
			for (uint32_t i = from < n ? from : n; i + 5u <= n; i++) {
				if (!memcmp(b + i, "DONE ", 5)) { found = 1; break; }
				if (i + 19u <= n && !memcmp(b + i, "absent: not running", 19)) { found = 1; break; }
			}
			free(b);
		}
		if (found) { say("after: %s has DONE, starting", path); return; }
		if (now_us() >= end) { say("after: %s has no DONE within %us, starting anyway", path, timeout_s); return; }
		sleep_ms(1000);
	}
}

/* ---------------------------------------------------------------------------------------------- one stream */

static const char *g_outdir = "/private/var/db/nord2-vcodec/out", *g_relay = "/dev/redwood-vcodec-client", *g_sock;
static int g_no_yuv, g_mm21_first;
static unsigned g_wait_s = 120, g_frames_max = 600, g_timeout_ms = 15000;

static int g_pipeline = 1;
static int g_hash = 1;          /* 1 every stream, 0 none, 2 even-numbered streams only (--hash-alt: A = proof, B = fps) */
#define CONV_Q 16u
#define CONV_MAX_OUT 2u        /* pictures popped and not yet released (the client's share of the dpb+2 pool) */

struct conv {
	struct n2vcd_dec_session *ss;
	struct n2vcd_dec_client *cl;
	const struct n2vcd_dec_geometry *geo;
	size_t w, h, cw, ch;
	uint8_t *ybuf, *cbuf, *nv12;
	uint64_t *rowh;
	FILE *yf;
	const char *ypath, *outdir;
	int mm21_first;
	int hash;                       /* compute the bit-exactness hashes (else fnvr_* = 0) */
	unsigned run;
	/* queue (pipelined mode) */
	pthread_t th;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	struct n2vcd_dec_pic q[CONV_Q];
	uint64_t qt_us[CONV_Q];
	unsigned qh, qt, out;
	int done, failed;
	/* results */
	unsigned shown;
	uint64_t read_us, detile_us, hash_us, conv_us, t_last;
};

/* read back, detile, hash, log, write, release one picture.  0 or -1 (cv->failed set) */
static int convert_one(struct conv *c, const struct n2vcd_dec_pic *pic, uint64_t t_queued)
{
	const struct n2vcd_dec_geometry *geo = c->geo;
	uint64_t t0 = now_us();
	int e1 = n2vcd_dec_buffer_rw(c->ss, pic->index, 0, 0, c->ybuf, geo->y_bytes, 0);
	int e2 = geo->c_bytes ? n2vcd_dec_buffer_rw(c->ss, pic->index, 1, 0, c->cbuf, geo->c_bytes, 0) : 0;
	uint64_t t1 = now_us();
	if (e1 != N2VCD_E_OK || e2 != N2VCD_E_OK) {
		say("picture %u: buffer read failed %d/%d errno=%u", c->shown, e1, e2, n2vcd_dec_last_errno(c->cl));
		c->failed = 1;
		return -1;
	}
	if (c->mm21_first && c->shown == 0) {
		char mp[640];
		snprintf(mp, sizeof mp, "%s/dec-%d-%u-first.mm21", c->outdir, (int)getpid(), c->run);
		FILE *mf = fopen(mp, "wb");
		if (mf) {
			(void)fwrite(c->ybuf, 1, geo->y_bytes, mf);
			(void)fwrite(c->cbuf, 1, geo->c_bytes, mf);
			fclose(mf);
			say("raw MM21 of the first picture: %s (%u + %u bytes)", mp, geo->y_bytes, geo->c_bytes);
		}
	}
	struct n2_mm21_frame src = { c->ybuf, c->cbuf, geo->stride, geo->stride, geo->buf_h };
	struct band bd[16];
	unsigned n = g_threads ? (g_threads > 16u ? 16u : g_threads) : 1u;
	uint32_t per = (uint32_t)((c->h / n + 31u) & ~31u);
	if (per == 0) per = 32;
	unsigned nb = 0;
	for (uint32_t y0 = 0; y0 < c->h && nb < n; nb++) {
		uint32_t rows = nb == n - 1 || y0 + per >= c->h ? (uint32_t)c->h - y0 : per;
		struct band b = { &src, c->nv12, c->w, c->h, c->cw, c->ch, y0, rows, c->rowh, c->rowh + c->h, 0, 0, 0 };
		bd[nb] = b;
		y0 += rows;
	}
	bands_run(bd, nb, band_run);
	uint64_t t2 = now_us();
	for (unsigned i = 0; i < nb; i++) {
		if (bd[i].rc != 0) { say("picture %u: detile failed %d", c->shown, bd[i].rc); c->failed = 1; return -1; }
	}
	uint64_t hy = 0, hc = 0, ysum = 0, nz = 0;
	if (c->hash) {
		bands_run(bd, nb, band_hash);
		hy = fnv_rows(c->rowh, c->h);
		hc = fnv_rows(c->rowh + c->h, c->ch);
		for (unsigned i = 0; i < nb; i++) { ysum += bd[i].ysum; nz += bd[i].nz; }
	}
	uint64_t t3 = now_us();
	size_t wh = c->w * c->h, sampled = c->w * ((c->h + 7u) / 8u);
	say_ns("picture %u index=%u pts=%llu poc=%d %ux%u queue=%llu read=%llu detile=%llu hash=%llu conv=%llu us y_mean=%llu "
	       "y_nonzero=%llu/%zu fnvr_y=%016llx fnvr_uv=%016llx", c->shown, pic->index, (unsigned long long)pic->pts, pic->poc,
	       pic->width, pic->height, (unsigned long long)(t0 - t_queued), (unsigned long long)(t1 - t0),
	       (unsigned long long)(t2 - t1), (unsigned long long)(t3 - t2), (unsigned long long)(t3 - t0),
	       (unsigned long long)(sampled ? ysum / sampled : 0), (unsigned long long)nz, sampled, (unsigned long long)hy,
	       (unsigned long long)hc);
	if (c->yf && (fwrite(c->nv12, 1, wh + c->cw * c->ch, c->yf) != wh + c->cw * c->ch || fflush(c->yf) != 0)) {
		say("write %s failed", c->ypath);
		c->failed = 1;
		return -1;
	}
	int r = n2vcd_dec_release(c->ss, pic->index);
	if (r != N2VCD_E_OK) { say("RELEASE %u failed: %s", pic->index, n2vcd_dec_strstatus(r)); c->failed = 1; return -1; }
	c->read_us += t1 - t0;
	c->detile_us += t2 - t1;
	c->hash_us += t3 - t2;
	c->conv_us += t3 - t0;
	c->shown++;
	c->t_last = now_us();
	return 0;
}

static void *conv_main(void *arg)
{
	struct conv *c = (struct conv *)arg;
	for (;;) {
		pthread_mutex_lock(&c->mu);
		while (c->qh == c->qt && !c->done && !c->failed) pthread_cond_wait(&c->cv, &c->mu);
		if (c->qh == c->qt || c->failed) { pthread_mutex_unlock(&c->mu); break; }
		struct n2vcd_dec_pic pic = c->q[c->qh % CONV_Q];
		uint64_t tq = c->qt_us[c->qh % CONV_Q];
		c->qh++;
		pthread_mutex_unlock(&c->mu);
		int rc = convert_one(c, &pic, tq);
		pthread_mutex_lock(&c->mu);
		if (c->out) c->out--;
		pthread_cond_broadcast(&c->cv);
		pthread_mutex_unlock(&c->mu);
		if (rc != 0) break;
	}
	return NULL;
}

/* one decoder session over one stream: 1 = every access unit decoded and displayed, 0 = not */
static int decode_one(const char *in, const char *codec_s, unsigned run)
{
	int hevc = !strcmp(codec_s, "hvc1");
	int ci = codec_index(codec_s);              /* opus-fmt; validated in main() */
	const char *outdir = g_outdir, *relay = g_relay, *sock = g_sock;
	int no_yuv = g_no_yuv, mm21_first = g_mm21_first;
	unsigned wait_s = g_wait_s, frames_max = g_frames_max, timeout_ms = g_timeout_ms;
	say("stream %u: %s codec=%s", run, in, codec_s);
	int ok = 0;
	unsigned shown = 0, inputs = 0, errors_map = 0;
	uint64_t hw_us_sum = 0, rtt_us_sum = 0, rtt_max = 0, detile_us_sum = 0;
	uint8_t *bs = NULL, *ybuf = NULL, *cbuf = NULL, *nv12 = NULL;
	uint64_t *rowh = NULL;
	struct conv cv;
	memset(&cv, 0, sizeof cv);
	uint32_t bs_n = 0;
	unsigned nau = 0;
	struct au aus[1024];
	FILE *yf = NULL;
	char ypath[600] = "-";
	struct n2vcd_dec_client *cl = NULL;
	struct n2vcd_dec_session *ss = NULL;
	struct n2vcd_dec_geometry geo;
	memset(&geo, 0, sizeof geo);
	char err[200] = "";

	int r = read_file(in, &bs, &bs_n);
	if (r) { say("cannot read %s: %d (%s)", in, r, strerror(-r)); goto out; }
	if (g_codecs[ci].kind == K_IVF) nau = split_ivf(bs, bs_n, aus, 1024u);
	else if (g_codecs[ci].kind >= K_MPEG2) nau = split_es(bs, bs_n, g_codecs[ci].kind, aus, 1024u);
	else nau = split_aus(bs, bs_n, hevc, aus, 1024u);
	say("input %s: %u bytes, %u access units", in, bs_n, nau);
	if (!nau) goto out;
	if (nau > frames_max) nau = frames_max;

	/* ---- server */
	uint64_t wait_end = now_us() + (uint64_t)wait_s * 1000000u;
	if (!sock) {
#ifdef __APPLE__
		unsigned int zero = 0;     /* q10je: the kernel's per-entry VCT trace delays 2 s per ioctl unless it is 0 */
		int sr = sysctlbyname("kern.n2vctrace", NULL, NULL, &zero, sizeof zero);
		say("sysctl kern.n2vctrace=0 -> %d errno %d", sr, sr ? errno : 0);
#endif
		for (;;) {
			int fd = open(relay, O_RDWR);
			if (fd >= 0) {
				struct rwvc_info info;
				memset(&info, 0, sizeof info);
				int ir = ioctl(fd, RWVC_INFO, &info);
				close(fd);
				if (ir == 0 && info.server_present) {
					say("relay %s abi=%u server_present=1 powered=%#x pool=%u free=%u irq_mode=%u", relay, info.abi_version,
					    info.powered_mask, info.pool_bytes, info.pool_free, info.irq_mode);
					break;
				}
			}
			if (now_us() >= wait_end) { say("vcodecd not serving %s within %us", relay, wait_s); goto out; }
			sleep_ms(1000);
		}
	}
	uint64_t t_c = now_us();
	for (;;) {
		r = n2vcd_dec_connect_path(&cl, sock, sock ? NULL : relay, 5000u, err, sizeof err);
		if (r == N2VCD_E_OK) break;
		if (!sock || now_us() >= wait_end) { say("connect/HELLO failed: %s (%s)", n2vcd_dec_strstatus(r), err); goto out; }
		sleep_ms(200);
	}
	struct n2vcd_dec_caps caps;
	memset(&caps, 0, sizeof caps);
	(void)n2vcd_dec_info(cl, &caps);
	say("HELLO_OK version=%u dec=%#x max=%ux%u in %llums", caps.version, caps.dec_codecs, caps.max_width, caps.max_height,
	    (unsigned long long)((now_us() - t_c) / 1000u));
	if (!(caps.dec_codecs & g_codecs[ci].bit)) { say("daemon offers no %s decoder (vcodecd without --decoder?)", codec_s); goto out; }
	n2vcd_dec_set_timeout(cl, timeout_ms);

	/* ---- OPEN + HEADER + pool */
	uint32_t in_cap = 0;
	uint64_t t = now_us();
	for (unsigned tries = 0;; tries++) {
		r = n2vcd_dec_open(cl, g_codecs[ci].proto, 0, 0, N2VCD_OPEN_FMT_MM21, &ss, &in_cap, err, sizeof err);
		if (r != N2VCD_E_BUSY || tries >= 30) break;
		if (tries == 0) say("OPEN busy (an encoder session is open): retrying every 2 s for 60 s");
		sleep_ms(2000);
	}
	if (r != N2VCD_E_OK) { say("OPEN failed: %s detail=%u (%s)", n2vcd_dec_strstatus(r), n2vcd_dec_last_detail(cl), err); goto out; }
	say("OPEN_OK session=%u in_buf=%u in %llums", n2vcd_dec_session_id(ss), in_cap, (unsigned long long)((now_us() - t) / 1000u));
	t = now_us();
	r = n2vcd_dec_header(ss, bs + aus[0].off, aus[0].len, &geo);
	if (r != N2VCD_E_OK) { say("DEC_HEADER failed: %s detail=%u", n2vcd_dec_strstatus(r), n2vcd_dec_last_detail(cl)); goto close; }
	say("HEADER %ux%u stride=%u buf_h=%u y=%u c=%u out=%u dpb=%u fourcc=%.4s in %llums", geo.width, geo.height, geo.stride,
	    geo.buf_h, geo.y_bytes, geo.c_bytes, geo.out_buf_bytes, geo.dpb_frames, (const char *)&geo.fourcc,
	    (unsigned long long)((now_us() - t) / 1000u));
	if (geo.fourcc != N2VCD_FMT_MM21) { say("output is not MM21"); goto close; }
	t = now_us();
	r = n2vcd_dec_add_buffers(ss, &geo);
	if (r != N2VCD_E_OK) { say("ADD_BUFFER failed: %s detail=%u errno=%u", n2vcd_dec_strstatus(r), n2vcd_dec_last_detail(cl),
	                          n2vcd_dec_last_errno(cl)); goto close; }
	say("pool %u x %u bytes registered in %llums", geo.dpb_frames, geo.out_buf_bytes, (unsigned long long)((now_us() - t) / 1000u));

	size_t w = geo.width, h = geo.height, cw = 2u * ((w + 1u) / 2u), ch = (h + 1u) / 2u;
	ybuf = (uint8_t *)malloc(geo.y_bytes);
	cbuf = (uint8_t *)malloc(geo.c_bytes ? geo.c_bytes : 1u);
	nv12 = (uint8_t *)malloc(w * h + cw * ch);
	rowh = (uint64_t *)malloc((h + ch) * sizeof(uint64_t));
	if (!ybuf || !cbuf || !nv12 || !rowh) { say("out of memory"); goto close; }
	(void)mkdir(outdir, 0755);
	if (!no_yuv) {
		snprintf(ypath, sizeof ypath, "%s/dec-%d-%u-%ux%u.nv12.yuv", outdir, (int)getpid(), run, geo.width, geo.height);
		yf = fopen(ypath, "wb");
		if (!yf) { say("cannot create %s errno=%d", ypath, errno); goto close; }
	}
	cv.ss = ss; cv.cl = cl; cv.geo = &geo; cv.w = w; cv.h = h; cv.cw = cw; cv.ch = ch;
	cv.ybuf = ybuf; cv.cbuf = cbuf; cv.nv12 = nv12; cv.rowh = rowh; cv.yf = yf; cv.ypath = ypath;
	cv.mm21_first = mm21_first; cv.outdir = outdir; cv.run = run;
	cv.hash = g_hash == 1 || (g_hash == 2 && (run & 1u) == 0);
	pthread_mutex_init(&cv.mu, NULL);
	pthread_cond_init(&cv.cv, NULL);
	int conv_thread = 0;
	if (g_pipeline) {
		if (pthread_create(&cv.th, NULL, conv_main, &cv) == 0) conv_thread = 1;
		else say("convert thread not started: serial conversion");
	}
	say("conversion: %s, %u detile/hash threads, max %u pictures held, hashes %s", conv_thread ? "pipelined (own thread)" : "serial",
	    g_threads, CONV_MAX_OUT, cv.hash ? "on" : "OFF (timing run)");

	/* ---- decode loop; `drain` pops every queued picture */
	int failed = 0;
	uint64_t t_first = 0;
	for (unsigned k = 0; k <= nau && !failed && !cv.failed; k++) {
		uint32_t ready = 0;
		if (k < nau) {
			struct n2vcd_dec_input_info ii;
			memset(&ii, 0, sizeof ii);
			uint64_t t_in = now_us();
			if (!t_first) t_first = t_in;
			r = n2vcd_dec_input(ss, bs + aus[k].off, aus[k].len, 1000u + k, 0, &ii);
			uint64_t rtt = now_us() - t_in;
			if (r != N2VCD_E_OK) {
				say("input %u (%u B) failed: %s detail=%u after %llums", k, aus[k].len, n2vcd_dec_strstatus(r),
				    n2vcd_dec_last_detail(cl), (unsigned long long)(rtt / 1000u));
				failed = 1;
				break;
			}
			if (ii.changed) { say("input %u: geometry change reported (not handled by the probe)", k); failed = 1; break; }
			inputs++;
			hw_us_sum += ii.hw_us;
			rtt_us_sum += rtt;
			if (rtt > rtt_max) rtt_max = rtt;
			if (ii.error_map) errors_map++;
			say_ns("input %u bytes=%u rtt=%llu.%03llums hw_us=%u error_map=%#x ready=%u", k, aus[k].len,
			       (unsigned long long)(rtt / 1000u), (unsigned long long)(rtt % 1000u), ii.hw_us, ii.error_map,
			       ii.pictures_ready);
			ready = ii.pictures_ready;
		} else {
			r = n2vcd_dec_flush(ss, &ready);
			if (r != N2VCD_E_OK) { say("FLUSH failed: %s", n2vcd_dec_strstatus(r)); failed = 1; break; }
			say_ns("FLUSH_OK ready=%u", ready);
			ready = ready ? ready : 1u;      /* drain until the daemon says none */
		}
		while (ready && !cv.failed) {
			/* never hold more than CONV_MAX_OUT pictures: the pool has dpb+2 buffers, the vendor needs dpb+1 */
			pthread_mutex_lock(&cv.mu);
			while (conv_thread && cv.out >= CONV_MAX_OUT && !cv.failed) pthread_cond_wait(&cv.cv, &cv.mu);
			pthread_mutex_unlock(&cv.mu);
			struct n2vcd_dec_pic pic;
			memset(&pic, 0, sizeof pic);
			int pr = n2vcd_dec_picture(ss, &pic);
			if (pr == 0) break;
			if (pr < 0) { say("PICTURE failed: %s", n2vcd_dec_strstatus(-pr)); failed = 1; break; }
			ready = pic.pictures_ready;
			if (conv_thread) {
				pthread_mutex_lock(&cv.mu);
				cv.q[cv.qt % CONV_Q] = pic;
				cv.qt_us[cv.qt % CONV_Q] = now_us();
				cv.qt++;
				cv.out++;
				pthread_cond_broadcast(&cv.cv);
				pthread_mutex_unlock(&cv.mu);
			} else if (convert_one(&cv, &pic, now_us()) != 0) {
				failed = 1;
				break;
			}
		}
	}
	if (conv_thread) {
		pthread_mutex_lock(&cv.mu);
		cv.done = 1;
		pthread_cond_broadcast(&cv.cv);
		pthread_mutex_unlock(&cv.mu);
		pthread_join(cv.th, NULL);
	}
	if (cv.failed) failed = 1;
	shown = cv.shown;
	detile_us_sum = cv.detile_us;
	{
		uint64_t wall = cv.t_last && t_first ? cv.t_last - t_first : 0;
		say("stream %u timing: %u pictures in %llu.%03llu ms = %llu.%01llu fps; per picture avg read %llu us, detile %llu us, "
		    "hash %llu us, convert total %llu us, decode rtt %llu us", run, shown,
		    (unsigned long long)(wall / 1000u), (unsigned long long)(wall % 1000u),
		    (unsigned long long)(wall ? (uint64_t)shown * 1000000u / wall : 0),
		    (unsigned long long)(wall ? ((uint64_t)shown * 10000000u / wall) % 10u : 0),
		    (unsigned long long)(shown ? cv.read_us / shown : 0), (unsigned long long)(shown ? cv.detile_us / shown : 0),
		    (unsigned long long)(shown ? cv.hash_us / shown : 0), (unsigned long long)(shown ? cv.conv_us / shown : 0),
		    (unsigned long long)(inputs ? rtt_us_sum / inputs : 0));
	}
	ok = !failed && inputs == nau && shown == nau;

close:
	if (ss) {
		r = n2vcd_dec_close(ss);
		ss = NULL;
		say("CLOSE %s", r == N2VCD_E_OK ? "OK" : n2vcd_dec_strstatus(r));
	}
out:
	if (cl) n2vcd_dec_disconnect(cl);
	if (yf) { fflush(yf); fsync(fileno(yf)); fclose(yf); }
	say("DONE ok=%d codec=%s pictures=%u/%u inputs=%u error_maps=%u size=%ux%u avg_rtt_ms=%llu.%03llu max_rtt_ms=%llu "
	    "hw_ms=%llu detile_ms=%llu file=%s", ok, codec_s, shown, nau, inputs, errors_map, geo.width, geo.height,
	    (unsigned long long)(inputs ? rtt_us_sum / inputs / 1000u : 0), (unsigned long long)(inputs ? rtt_us_sum / inputs % 1000u : 0),
	    (unsigned long long)(rtt_max / 1000u), (unsigned long long)(hw_us_sum / 1000u),
	    (unsigned long long)(detile_us_sum / 1000u), shown ? ypath : "-");
	free(bs); free(ybuf); free(cbuf); free(nv12); free(rowh);
	return ok;
}

/* ---------------------------------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
	const char *gate = NULL, *after = NULL, *logpath = NULL, *codec_s = NULL;
	const char *ins[12], *codecs[12];
	unsigned nin = 0;
	int keep_gate = 0, no_console = 0;
	unsigned delay_s = 0, after_s = 240;
	g_t0 = now_us();
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--gate") && v) { gate = v; i++; }
		else if (!strcmp(a, "--keep-gate")) keep_gate = 1;
		else if (!strcmp(a, "--delay") && v) { delay_s = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--wait-server") && v) { g_wait_s = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--after") && v) { after = v; i++; }
		else if (!strcmp(a, "--after-timeout") && v) { after_s = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--codec") && v) { codec_s = v; i++; }
		else if (!strcmp(a, "--in") && v) {
			if (nin == 12) { fprintf(stderr, "n2vcd_dprobe: at most 12 --in\n"); return 2; }
			ins[nin] = v; codecs[nin] = NULL; nin++; i++;
		}
		else if (!strcmp(a, "--frames") && v) { g_frames_max = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--out") && v) { g_outdir = v; i++; }
		else if (!strcmp(a, "--log") && v) { logpath = v; i++; }
		else if (!strcmp(a, "--no-yuv")) g_no_yuv = 1;
		else if (!strcmp(a, "--mm21-first")) g_mm21_first = 1;
		else if (!strcmp(a, "--serial")) g_pipeline = 0;
		else if (!strcmp(a, "--no-hash")) g_hash = 0;
		else if (!strcmp(a, "--hash-alt")) g_hash = 2;
		else if (!strcmp(a, "--threads") && v) { g_threads = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--relay") && v) { g_relay = v; i++; }
		else if (!strcmp(a, "--socket") && v) { g_sock = v; i++; }
		else if (!strcmp(a, "--timeout-ms") && v) { g_timeout_ms = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--no-console")) no_console = 1;
		else { fprintf(stderr, "n2vcd_dprobe: bad argument %s (see the head comment of n2vcd_dprobe.c)\n", a); return 2; }
		if (!strcmp(a, "--codec") && nin) codecs[nin - 1] = codec_s;   /* --codec after --in applies to that stream */
	}
	/* codec per stream: an explicit --codec following it, else the file name (.h265/.hevc/.265 -> hvc1), else avc1 */
	for (unsigned k = 0; k < nin; k++) {
		if (!codecs[k]) {
			const char *dot = strrchr(ins[k], '.');
			codecs[k] = dot && (!strcmp(dot, ".h265") || !strcmp(dot, ".hevc") || !strcmp(dot, ".265")) ? "hvc1" : "avc1";
			/* opus-fmt: .m2v/.mpg mp2v, .m4v/.cmp mp4v, .h263/.263 h263, .ivf from the IVF fourcc (VP80/VP90/AV01) */
			if (dot && (!strcmp(dot, ".m2v") || !strcmp(dot, ".mpg"))) codecs[k] = "mp2v";
			if (dot && (!strcmp(dot, ".m4v") || !strcmp(dot, ".cmp"))) codecs[k] = "mp4v";
			if (dot && (!strcmp(dot, ".h263") || !strcmp(dot, ".263"))) codecs[k] = "h263";
			if (dot && !strcmp(dot, ".ivf")) {
				unsigned char hd[12] = { 0 };
				FILE *f = fopen(ins[k], "rb");
				if (f) { if (fread(hd, 1, sizeof hd, f) != sizeof hd) hd[0] = 0; fclose(f); }
				codecs[k] = !memcmp(hd + 8, "VP80", 4) ? "vp08" : !memcmp(hd + 8, "VP90", 4) ? "vp09" :
				            !memcmp(hd + 8, "AV01", 4) ? "av01" : "?";
			}
		}
		if (codec_index(codecs[k]) < 0) {
			fprintf(stderr, "n2vcd_dprobe: codec avc1|hvc1|vp08|vp09|av01|mp2v|mp4v|h263 (%s)\n", codecs[k]);
			return 2;
		}
	}
	if (!nin || !g_frames_max || g_timeout_ms < 1000u) {
		fprintf(stderr, "n2vcd_dprobe: need --in FILE (up to 12), frames >= 1, timeout >= 1000 ms\n");
		return 2;
	}
	if (!no_console) g_console = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK);
	if (logpath) g_logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (gate) {
		if (access(gate, F_OK) != 0) { say("gate %s absent: not running", gate); return 0; }
		if (!keep_gate) say("gate %s %s (one run per installation)", gate, unlink(gate) == 0 ? "removed" : "NOT removed");
	}
	uint32_t after_from = 0;
	if (after) {
		struct stat st;
		if (stat(after, &st) == 0 && st.st_size > 0) after_from = (uint32_t)st.st_size;
	}
	say("start pid=%d streams=%u transport=%s delay=%us mm21=%s after=%s from byte %u", (int)getpid(), nin,
	    g_sock ? g_sock : g_relay, delay_s, n2_mm21_impl(), after ? after : "-", after_from);
	{
		/* phone run vc22: SIGKILL (jetsam per-process limit, 6 MB effective) at the 1080p stream (Y+C+NV12 = 7.5 MB) */
		char ml[400], u[96];
		int lr = n2ml_raise(3072, ml, sizeof ml);
		say("jetsam %s %s; %s", lr == 0 ? "raised" : "NOT raised", ml, n2ml_usage_str(u, sizeof u));
	}
	if (delay_s) { sleep_ms(delay_s * 1000u); say("woke after %us", delay_s); }
	if (after) wait_after(after, after_s, after_from);
	unsigned good = 0;
	for (unsigned k = 0; k < nin; k++) good += (unsigned)decode_one(ins[k], codecs[k], k);
	{
		char u[96];
		say("ALL ok=%d streams=%u/%u %s", good == nin, good, nin, n2ml_usage_str(u, sizeof u));
	}
	if (g_logfd >= 0) close(g_logfd);
	return good == nin ? 0 : 1;
}
