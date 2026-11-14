/* SPDX-License-Identifier: MIT */
/*
 * t_mm21.c -- host tests for n2_mm21 (opus-mm21).
 *
 * Oracles (none of them is n2_mm21's own code):
 *  1. A per-pixel tiler written here directly from the layout formula of facts/MM21_LAYOUT.md: a known NV12 picture is
 *     tiled into MM21 (padding filled with garbage) and the converter must give the picture back, byte for byte.
 *  2. With -DHAVE_LIBYUV: the host's libyuv MM21ToNV12 (linked as a black box; its source is not read) on the same MM21
 *     data must give the same NV12 as n2_mm21 -- an independent implementation of the same layout.
 *  3. Guard bytes around every destination row/plane must stay untouched; sources are allocated to the exact plane size
 *     so an over-read shows under ASan.
 * Usage: t_mm21            -> runs the checks, last line "checks P/T"
 *        t_mm21 --bench N  -> N conversions of a 1920x1080 (buf 1920x1088) frame, direct and staged
 */
#define _POSIX_C_SOURCE 200809L
#include "n2_mm21.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef HAVE_LIBYUV
#include <libyuv/convert.h>
#endif

static unsigned g_pass, g_total;
#define CHECK(cond, ...) do { g_total++; if (cond) { g_pass++; } else { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rng = 0x12345678u;
static uint8_t rnd8(void) { rng = rng * 1664525u + 1013904223u; return (uint8_t)(rng >> 24); }

/* ---- independent reference tiler (formula of MM21_LAYOUT.md, per pixel) ---- */
static size_t ref_y_off(uint32_t stride, uint32_t x, uint32_t y)
{
	uint32_t tile_row = y / 32, tile_col = x / 16, in_y = y % 32, in_x = x % 16;
	return (size_t)tile_row * (stride * 32) + (size_t)tile_col * (16 * 32) + in_y * 16 + in_x;
}
static size_t ref_uv_off(uint32_t stride, uint32_t xb, uint32_t yc)
{
	uint32_t tile_row = yc / 16, tile_col = xb / 16, in_y = yc % 16, in_x = xb % 16;
	return (size_t)tile_row * (stride * 16) + (size_t)tile_col * (16 * 16) + in_y * 16 + in_x;
}

struct pic {                     /* a linear NV12 picture of the full aligned area (stride x buf_h), pitch = stride */
	uint32_t stride, buf_h;
	uint8_t *y, *uv;             /* linear */
	uint8_t *ty, *tuv;           /* the same, tiled (exact plane size allocations) */
};

static void pic_make(struct pic *p, uint32_t stride, uint32_t buf_h, int pattern)
{
	p->stride = stride; p->buf_h = buf_h;
	size_t yb = (size_t)stride * buf_h, cb = yb / 2;
	p->y = malloc(yb); p->uv = malloc(cb); p->ty = malloc(yb); p->tuv = malloc(cb);
	for (uint32_t y = 0; y < buf_h; y++) {
		for (uint32_t x = 0; x < stride; x++) {
			p->y[(size_t)y * stride + x] = pattern ? (uint8_t)(x * 7 + y * 13) : rnd8();
		}
	}
	for (uint32_t y = 0; y < buf_h / 2; y++) {
		for (uint32_t x = 0; x < stride; x++) {
			p->uv[(size_t)y * stride + x] = pattern ? (uint8_t)(x * 3 + y * 29 + (x & 1) * 100) : rnd8();
		}
	}
	memset(p->ty, 0xEE, yb); memset(p->tuv, 0xEE, cb);
	for (uint32_t y = 0; y < buf_h; y++) {
		for (uint32_t x = 0; x < stride; x++) { p->ty[ref_y_off(stride, x, y)] = p->y[(size_t)y * stride + x]; }
	}
	for (uint32_t y = 0; y < buf_h / 2; y++) {
		for (uint32_t x = 0; x < stride; x++) { p->tuv[ref_uv_off(stride, x, y)] = p->uv[(size_t)y * stride + x]; }
	}
}
static void pic_free(struct pic *p) { free(p->y); free(p->uv); free(p->ty); free(p->tuv); }

#define GUARD 0xA5
#define PADB  37                   /* extra bytes per destination row, plus a guard block before/after each plane */

struct out { uint8_t *ybuf, *uvbuf; size_t yp, uvp, ysz, uvsz; struct n2_nv12_out o; };
static void out_make(struct out *d, uint32_t w, uint32_t h)
{
	uint32_t cw = (w + 1) / 2 * 2, ch = (h + 1) / 2;
	d->yp = w + PADB; d->uvp = cw + PADB;
	d->ysz = d->yp * h + 2 * 64; d->uvsz = d->uvp * ch + 2 * 64;
	d->ybuf = malloc(d->ysz); d->uvbuf = malloc(d->uvsz);
	memset(d->ybuf, GUARD, d->ysz); memset(d->uvbuf, GUARD, d->uvsz);
	d->o.y = d->ybuf + 64; d->o.uv = d->uvbuf + 64; d->o.y_pitch = d->yp; d->o.uv_pitch = d->uvp;
}
static void out_free(struct out *d) { free(d->ybuf); free(d->uvbuf); }

/* expected = the linear source picture; also every byte outside the written area must still be GUARD */
static int out_check(const struct out *d, const struct pic *p, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char *what)
{
	uint32_t cw = (w + 1) / 2 * 2, ch = (h + 1) / 2;
	size_t bad = 0, guard = 0;
	for (size_t i = 0; i < d->ysz; i++) {
		long off = (long)i - 64;
		long r = off >= 0 ? off / (long)d->yp : -1, c = off >= 0 ? off % (long)d->yp : -1;
		int inside = off >= 0 && r < (long)h && c < (long)w;
		if (inside) { if (d->ybuf[i] != p->y[(size_t)(y + r) * p->stride + x + c]) { bad++; } }
		else if (d->ybuf[i] != GUARD) { guard++; }
	}
	for (size_t i = 0; i < d->uvsz; i++) {
		long off = (long)i - 64;
		long r = off >= 0 ? off / (long)d->uvp : -1, c = off >= 0 ? off % (long)d->uvp : -1;
		int inside = off >= 0 && r < (long)ch && c < (long)cw;
		if (inside) { if (d->uvbuf[i] != p->uv[(size_t)(y / 2 + r) * p->stride + x + c]) { bad++; } }
		else if (d->uvbuf[i] != GUARD) { guard++; }
	}
	if (bad || guard) { printf("  %s %ux%u+%u+%u: %zu wrong, %zu guard bytes hit\n", what, w, h, x, y, bad, guard); }
	return !bad && !guard;
}

/* fetch from the tiled planes by copy, recording that every request stays inside the plane */
struct fctx { const struct pic *p; unsigned calls, oob; int fail_at; };
static int fetch_copy(void *vctx, int plane, uint32_t offset, uint32_t bytes, uint8_t *staging)
{
	struct fctx *f = vctx;
	size_t psz = (size_t)f->p->stride * f->p->buf_h / (plane ? 2 : 1);
	f->calls++;
	if (f->fail_at && (int)f->calls == f->fail_at) { return -EIO; }
	if ((size_t)offset + bytes > psz) { f->oob++; return -EFAULT; }
	memcpy(staging, (plane ? f->p->tuv : f->p->ty) + offset, bytes);
	return 0;
}

static void one_case(uint32_t stride, uint32_t buf_h, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int pattern)
{
	struct pic p; pic_make(&p, stride, buf_h, pattern);
	struct n2_mm21_frame f = { p.ty, p.tuv, stride, stride, buf_h };
	struct out d;

	out_make(&d, w, h);
	int e = n2_mm21_to_nv12(&f, &d.o, x, y, w, h);
	CHECK(e == 0 && out_check(&d, &p, x, y, w, h, "direct"), "direct stride %u buf_h %u rect %ux%u+%u+%u e=%d",
	      stride, buf_h, w, h, x, y, e);
	out_free(&d);

	out_make(&d, w, h);
	size_t sb = n2_mm21_staging_bytes(&f, x, w);
	uint8_t *st = malloc(sb);
	struct fctx fc = { &p, 0, 0, 0 };
	struct n2_mm21_frame g = { NULL, NULL, stride, stride, buf_h };
	e = n2_mm21_to_nv12_staged(&g, fetch_copy, &fc, st, sb, &d.o, x, y, w, h);
	unsigned want_calls = (unsigned)((y + h + 31) / 32 - y / 32) + (unsigned)(((y / 2) + (h + 1) / 2 + 15) / 16 - (y / 2) / 16);
	CHECK(e == 0 && fc.oob == 0 && fc.calls == want_calls && out_check(&d, &p, x, y, w, h, "staged"),
	      "staged stride %u rect %ux%u+%u+%u e=%d calls %u (want %u) oob %u", stride, w, h, x, y, e, fc.calls,
	      want_calls, fc.oob);
	free(st);
	out_free(&d);

#ifdef HAVE_LIBYUV
	if (x == 0 && y == 0) {           /* libyuv converts from the origin */
		struct out a, b;
		out_make(&a, w, h); out_make(&b, w, h);
		int le = MM21ToNV12(p.ty, (int)stride, p.tuv, (int)stride, a.o.y, (int)a.yp, a.o.uv, (int)a.uvp, (int)w, (int)h);
		e = n2_mm21_to_nv12(&f, &b.o, 0, 0, w, h);
		/* compare the written areas only: libyuv may handle the guard columns differently */
		int same = le == 0 && e == 0;
		for (uint32_t r = 0; same && r < h; r++) { same = !memcmp(a.o.y + r * a.yp, b.o.y + r * b.yp, w); }
		for (uint32_t r = 0; same && r < (h + 1) / 2; r++) {
			same = !memcmp(a.o.uv + r * a.uvp, b.o.uv + r * b.uvp, (w + 1) / 2 * 2);
		}
		CHECK(same, "libyuv MM21ToNV12 differs: stride %u buf_h %u %ux%u (libyuv %d)", stride, buf_h, w, h, le);
		out_free(&a); out_free(&b);
	}
#endif
	pic_free(&p);
}

static void inverse_case(uint32_t stride, uint32_t buf_h, uint32_t w, uint32_t h)
{
	struct pic p; pic_make(&p, stride, buf_h, 0);
	size_t yb = (size_t)stride * buf_h;
	uint8_t *ty = malloc(yb), *tuv = malloc(yb / 2);
	/* source pitch = stride; the area outside w x h must be edge-replicated from the picture */
	int e = n2_nv12_to_mm21(p.y, stride, p.uv, stride, w, h, ty, tuv, stride, buf_h);
	size_t bad = 0;
	uint32_t cw = (w + 1) / 2 * 2, ch = (h + 1) / 2;
	for (uint32_t yy = 0; yy < buf_h; yy++) {
		for (uint32_t xx = 0; xx < stride; xx++) {
			uint32_t sx = xx < w ? xx : w - 1, sy = yy < h ? yy : h - 1;
			if (ty[ref_y_off(stride, xx, yy)] != p.y[(size_t)sy * stride + sx]) { bad++; }
		}
	}
	for (uint32_t yy = 0; yy < buf_h / 2; yy++) {
		for (uint32_t xx = 0; xx < stride; xx++) {
			uint32_t sx = xx < cw ? xx : cw - 2 + (xx & 1), sy = yy < ch ? yy : ch - 1;
			if (tuv[ref_uv_off(stride, xx, yy)] != p.uv[(size_t)sy * stride + sx]) { bad++; }
		}
	}
	CHECK(e == 0 && bad == 0, "inverse stride %u buf_h %u %ux%u: e=%d bad=%zu", stride, buf_h, w, h, e, bad);
	free(ty); free(tuv); pic_free(&p);
}

static void einval_cases(void)
{
	uint8_t s[2048], dy[2048], duv[1024];
	struct n2_mm21_frame f = { s, s, 32, 32, 32 };
	struct n2_nv12_out o = { dy, duv, 32, 32 };
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 0, 32, 32) == 0, "valid 32x32");
	CHECK(n2_mm21_to_nv12(NULL, &o, 0, 0, 2, 2) == -EINVAL, "NULL src");
	CHECK(n2_mm21_to_nv12(&f, NULL, 0, 0, 2, 2) == -EINVAL, "NULL dst");
	CHECK(n2_mm21_to_nv12(&f, &o, 1, 0, 2, 2) == -EINVAL, "odd x");
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 1, 2, 2) == -EINVAL, "odd y");
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 0, 0, 2) == -EINVAL, "w 0");
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 0, 2, 0) == -EINVAL, "h 0");
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 0, 33, 2) == -EINVAL, "w > stride");
	CHECK(n2_mm21_to_nv12(&f, &o, 16, 0, 18, 2) == -EINVAL, "x + w > stride");
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 0, 31, 2) == 0, "odd w 31: chroma 32 fits");
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 30, 2, 4) == -EINVAL, "y + h > buf_h");
	CHECK(n2_mm21_to_nv12(&f, &o, 0, 0, 2, 33) == -EINVAL, "h > buf_h");
	struct n2_mm21_frame f2 = f; f2.y_stride = 24;
	CHECK(n2_mm21_to_nv12(&f2, &o, 0, 0, 2, 2) == -EINVAL, "stride 24");
	f2 = f; f2.uv_stride = 0;
	CHECK(n2_mm21_to_nv12(&f2, &o, 0, 0, 2, 2) == -EINVAL, "uv stride 0");
	f2 = f; f2.buf_h = 48;
	CHECK(n2_mm21_to_nv12(&f2, &o, 0, 0, 2, 2) == -EINVAL, "buf_h 48");
	f2 = f; f2.buf_h = 0;
	CHECK(n2_mm21_to_nv12(&f2, &o, 0, 0, 2, 40) == 0, "buf_h 0 = unchecked");
	struct n2_nv12_out o2 = o; o2.y_pitch = 7;
	CHECK(n2_mm21_to_nv12(&f, &o2, 0, 0, 8, 2) == -EINVAL, "y pitch < w");
	o2 = o; o2.uv_pitch = 7;
	CHECK(n2_mm21_to_nv12(&f, &o2, 0, 0, 7, 2) == -EINVAL, "uv pitch < 2*ceil(w/2)");
	o2 = o; o2.y_pitch = 7; o2.uv_pitch = 8;
	CHECK(n2_mm21_to_nv12(&f, &o2, 0, 0, 7, 2) == 0, "odd w 7, y pitch 7, uv pitch 8");
	CHECK(n2_mm21_detile_plane(s, 32, 8, dy, 32, 0, 0, 2, 2) == -EINVAL, "tile_h 8");
	CHECK(n2_mm21_detile_plane(s, 32, 16, dy, 1, 0, 0, 2, 2) == -EINVAL, "plane pitch < w");
	uint8_t st[512];
	CHECK(n2_mm21_to_nv12_staged(&f, NULL, NULL, st, sizeof st, &o, 0, 0, 2, 2) == -EINVAL, "staged NULL fetch");
	CHECK(n2_mm21_to_nv12_staged(&f, fetch_copy, NULL, st, 511, &o, 0, 0, 2, 2) == -EINVAL, "staging too small");
	CHECK(n2_mm21_staging_bytes(&f, 0, 32) == 1024, "staging 32 wide = 2 luma tiles = 1024");
	CHECK(n2_mm21_staging_bytes(&f, 14, 4) == 1024, "staging 14..18 spans 2 tiles");
	CHECK(n2_mm21_y_bytes(1920, 1088) == 2088960 && n2_mm21_uv_bytes(1920, 1088) == 1044480,
	      "1080p plane sizes = vendor fb_sz (DECODE_PATH.md)");
	CHECK(n2_nv12_to_mm21(s, 32, s, 32, 2, 2, dy, duv, 32, 16) == -EINVAL, "inverse buf_h 16");

	/* a fetch error is returned unchanged and stops the conversion */
	struct pic p; pic_make(&p, 64, 64, 1);
	struct n2_mm21_frame g = { NULL, NULL, 64, 64, 64 };
	uint8_t oy[64 * 64], ouv[64 * 32], stg[2048];
	struct n2_nv12_out oo = { oy, ouv, 64, 64 };
	struct fctx fc = { &p, 0, 0, 2 };
	CHECK(n2_mm21_to_nv12_staged(&g, fetch_copy, &fc, stg, sizeof stg, &oo, 0, 0, 64, 64) == -EIO && fc.calls == 2,
	      "fetch error propagates");
	pic_free(&p);
}

static double now_ms(void)
{
	struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* hot: the same source frame every time (fits the host LLC); cold: cycle through NF copies (NF * 3 MB > LLC), which is
 * closer to the phone, where the DMA buffer is cache-cleaned/invalidated before every read. */
#define NF 8
static volatile uint8_t g_sink;
static int bench(int n)
{
	const uint32_t W = 1920, H = 1080, S = 1920, BH = 1088;
	const size_t YB = (size_t)S * BH, CB = YB / 2;
	struct pic p; pic_make(&p, S, BH, 1);
	uint8_t *fy[NF], *fuv[NF];
	for (int k = 0; k < NF; k++) {
		fy[k] = malloc(YB); fuv[k] = malloc(CB);
		memcpy(fy[k], p.ty, YB); memcpy(fuv[k], p.tuv, CB);
	}
	size_t yp = 1920, uvp = 1920;       /* typical CVPixelBuffer bytesPerRow for 1920 (64-aligned) */
	uint8_t *oy = malloc(yp * H), *ouv = malloc(uvp * H / 2);
	struct n2_nv12_out o = { oy, ouv, yp, uvp };
	struct fctx fc = { &p, 0, 0, 0 };
	struct n2_mm21_frame g = { NULL, NULL, S, S, BH };
	size_t sb = n2_mm21_staging_bytes(&g, 0, W);
	uint8_t *st = malloc(sb);
	uint8_t *big = malloc(YB + CB);
	double hot = 1e9, cold = 1e9, cold_sum = 0, stg = 1e9, mc = 1e9;
	for (int i = 0; i < n + 5; i++) {
		struct n2_mm21_frame fh = { fy[0], fuv[0], S, S, BH };
		struct n2_mm21_frame fcold = { fy[i % NF], fuv[i % NF], S, S, BH };
		double t0 = now_ms();
		n2_mm21_to_nv12(&fh, &o, 0, 0, W, H);
		double t1 = now_ms();
		n2_mm21_to_nv12(&fcold, &o, 0, 0, W, H);
		double t2 = now_ms();
		n2_mm21_to_nv12_staged(&g, fetch_copy, &fc, st, sb, &o, 0, 0, W, H);
		double t3 = now_ms();
		memcpy(big, fy[(i + 3) % NF], YB); memcpy(big + YB, fuv[(i + 3) % NF], CB);
		double t4 = now_ms();
		g_sink = big[i % (YB + CB)] ^ oy[i % (yp * H)];
		if (i < 5) { continue; }
		if (t1 - t0 < hot) { hot = t1 - t0; }
		if (t2 - t1 < cold) { cold = t2 - t1; }
		cold_sum += t2 - t1;
		if (t3 - t2 < stg) { stg = t3 - t2; }
		if (t4 - t3 < mc) { mc = t4 - t3; }
	}
	printf("bench impl=%s 1920x1080 (buf 1920x1088) n=%d: hot best %.3f ms; cold best %.3f mean %.3f ms; "
	       "staged(memcpy fetch, = 1 extra copy) best %.3f ms; cold memcpy of the 3133440 B best %.3f ms\n",
	       n2_mm21_impl(), n, hot, cold, cold_sum / n, stg, mc);
	for (int k = 0; k < NF; k++) { free(fy[k]); free(fuv[k]); }
	free(oy); free(ouv); free(st); free(big); pic_free(&p);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "--bench")) { return bench(argc >= 3 ? atoi(argv[2]) : 100); }
	printf("impl=%s\n", n2_mm21_impl());
	/* full pictures at their vendor geometry (DECODE_PATH.md: 320x240 -> buf 320x256, 1920x1080 -> buf 1920x1088) */
	one_case(320, 256, 0, 0, 320, 240, 0);
	one_case(1920, 1088, 0, 0, 1920, 1080, 0);
	one_case(1920, 1088, 0, 0, 1920, 1088, 1);
	one_case(1280, 736, 0, 0, 1280, 720, 0);
	one_case(16, 32, 0, 0, 16, 32, 0);
	one_case(3840, 2176, 0, 0, 3840, 2160, 1);
	/* sizes that are not tile multiples, stride padded */
	one_case(32, 64, 0, 0, 17, 33, 0);
	one_case(16, 32, 0, 0, 1, 1, 0);
	one_case(16, 32, 0, 0, 2, 2, 0);
	one_case(48, 32, 0, 0, 30, 18, 0);
	one_case(352, 288, 0, 0, 350, 287, 0);
	one_case(864, 480, 0, 0, 854, 480, 0);
	one_case(208, 128, 0, 0, 199, 101, 0);
	/* crops with offsets (x, y even), odd sizes, partial head and tail tiles */
	one_case(64, 64, 2, 4, 13, 7, 0);
	one_case(64, 64, 14, 30, 4, 4, 0);
	one_case(64, 64, 16, 32, 16, 32, 0);
	one_case(320, 256, 8, 8, 304, 224, 0);
	one_case(1920, 1088, 0, 4, 1920, 1080, 0);
	one_case(1920, 1088, 6, 2, 1913, 1085, 0);
	one_case(96, 96, 62, 62, 33, 33, 0);
	one_case(96, 96, 94, 94, 1, 1, 0);
	for (int i = 0; i < 40; i++) {             /* random geometry */
		uint32_t stride = 16u * (1u + rnd8() % 24u), buf_h = 32u * (1u + rnd8() % 6u);
		uint32_t x = (rnd8() % stride) & ~1u, y = (rnd8() % buf_h) & ~1u;
		uint32_t w = 1u + rnd8() % (stride - x), h = 1u + rnd8() % (buf_h - y);
		if (((w + 1) / 2 * 2) > stride - x) { w = (stride - x) & ~1u; if (!w) { continue; } }
		one_case(stride, buf_h, x, y, w, h, 0);
	}
	inverse_case(320, 256, 320, 240);
	inverse_case(32, 64, 17, 33);
	inverse_case(48, 32, 47, 31);
	inverse_case(16, 32, 1, 1);
	einval_cases();
	printf("checks %u/%u\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
