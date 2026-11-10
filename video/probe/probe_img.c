/*
 * probe_img.c -- the probe's ImageIO measurements.
 *
 *   T05 img.write/read.jpeg|heic|png : a 1024x768 RGBA gradient through CGImageDestination, read back through
 *                                     CGImageSource; for JPEG and HEIC also decode WP09's golden files.
 *   T06 img.cmphoto                  : do the private CMPhoto entry points resolve?  (dlsym only, never called)
 *
 * SPDX-License-Identifier: MIT
 */
#include "probe_core.h"
#include "probe_apple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include("../vectors/embedded_vectors.h")
#    include "../vectors/embedded_vectors.h"
#    define PROBE_HAVE_VECTORS 1
#  endif
#endif
#ifndef PROBE_HAVE_VECTORS
#  define PROBE_HAVE_VECTORS 0
#endif

#define IMG_W 1024
#define IMG_H 768

/* kCGImageAlphaPremultipliedLast, byte order default: bytes R,G,B,A in memory */
#define IMG_BITMAP_INFO 1u

static void fill_gradient(uint8_t *p, int w, int h)
{
	int x, y;
	for (y = 0; y < h; y++) {
		uint8_t *row = p + (size_t)y * w * 4;
		for (x = 0; x < w; x++) {
			row[4 * x + 0] = (uint8_t)((x * 255) / (w - 1));
			row[4 * x + 1] = (uint8_t)((y * 255) / (h - 1));
			row[4 * x + 2] = (uint8_t)((x + y) & 255);
			row[4 * x + 3] = 255;
		}
	}
}

static double mean_rgb(const uint8_t *p, size_t n_pixels)
{
	uint64_t sum = 0;
	size_t i;
	if (!n_pixels)
		return 0.0;
	for (i = 0; i < n_pixels; i++)
		sum += (uint64_t)p[4 * i] + p[4 * i + 1] + p[4 * i + 2];
	return (double)sum / (double)(n_pixels * 3);
}

/* Write the gradient with ImageIO, read it back, report everything. */
static int test_write_read_size(struct probe_ctx *c, const char *ext, CFStringRef uti, size_t IW, size_t IH)
{
	uint64_t t0, t1;
	uint8_t *pixels = malloc(IW * IH * 4);
	uint8_t *readpix = NULL;
	CGColorSpaceRef cs = NULL;
	CGContextRef ctx = NULL;
	CGImageRef img = NULL;
	CFMutableDataRef data = NULL;
	CGImageDestinationRef dest = NULL;
	CGImageSourceRef src = NULL;
	CGImageRef got = NULL;
	size_t size = 0, rw = 0, rh = 0;
	double mean = 0.0;
	int finalize = 0, pass = 1;

	if (!pixels) {
		probe_say(c, "alloc", "failed");
		return 1;
	}
	fill_gradient(pixels, IW, IH);
	cs = CGColorSpaceCreateDeviceRGB();
	ctx = cs ? CGBitmapContextCreate(pixels, IW, IH, 8, IW * 4, cs, IMG_BITMAP_INFO) : NULL;
	img = ctx ? CGBitmapContextCreateImage(ctx) : NULL;
	probe_say(c, "context", "%d", img != NULL);
	if (!img) {
		pass = 0;
		goto out;
	}
	data = CFDataCreateMutable(NULL, 0);
	dest = data ? CGImageDestinationCreateWithData(data, uti, 1, NULL) : NULL;
	probe_say(c, "dest", "%d", dest != NULL);
	if (!dest) {
		pass = 0;
		goto out;
	}
	t0 = probe_now_ns();
	CGImageDestinationAddImage(dest, img, NULL);
	finalize = CGImageDestinationFinalize(dest) ? 1 : 0;
	t1 = probe_now_ns();
	probe_say(c, "write_ms", "%.0f", (double)(t1 - t0) / 1e6);
	size = data ? (size_t)CFDataGetLength(data) : 0;
	probe_say(c, "finalize", "%d", finalize);
	probe_say(c, "bytes", "%zu", size);
	if (!finalize || size == 0) {
		pass = 0;
		goto out;
	}
	if (ext[0] == 'h' || IW > 2000) {    /* (lead) keep the HEIC (and the 12 MP JPEG) for decoding on the build machine */
		char path[160];
		FILE *f;
		snprintf(path, sizeof path, "/private/var/db/nord2-video/t05-%zux%zu.%s", IW, IH, ext);
		f = fopen(path, "wb");
		if (f) {
			size_t wr = fwrite(CFDataGetBytePtr((CFDataRef)data), 1, size, f);
			fclose(f);
			probe_say(c, "saved", "%s %zu", path, wr);
		}
	}
	t0 = probe_now_ns();
	src = CGImageSourceCreateWithData(data, NULL);
	got = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
	probe_say(c, "source", "%d", got != NULL);
	if (!got) {
		pass = 0;
		goto out;
	}
	rw = CGImageGetWidth(got);
	rh = CGImageGetHeight(got);
	probe_say(c, "read_wh", "%zux%zu", rw, rh);
	if (rw != IW || rh != IH)
		pass = 0;
	readpix = malloc(rw * rh * 4);
	if (readpix) {
		CGContextRef ctx2 = CGBitmapContextCreate(readpix, rw, rh, 8, rw * 4, cs, IMG_BITMAP_INFO);
		if (ctx2) {
			CGRect rect;
			rect.origin.x = 0;
			rect.origin.y = 0;
			rect.size.width = (CGFloat)rw;
			rect.size.height = (CGFloat)rh;
			CGContextDrawImage(ctx2, rect, got);
			mean = mean_rgb(readpix, rw * rh);
			t1 = probe_now_ns();
			probe_say(c, "read_ms", "%.0f", (double)(t1 - t0) / 1e6);
			if (rw == IW && rh == IH) {     /* (lead) how far from the source, whole picture and per quadrant, bytes 0..2 of each pixel */
				double q[4] = { 0, 0, 0, 0 }, n[4] = { 0, 0, 0, 0 };
				size_t x, y;
				int k;
				for (y = 0; y < rh; y += 3)
					for (x = 0; x < rw; x += 3) {
						const uint8_t *a = pixels + (y * rw + x) * 4, *b = readpix + (y * rw + x) * 4;
						int qi = (y >= rh / 2) * 2 + (x >= rw / 2);
						for (k = 0; k < 3; k++)
							q[qi] += a[k] > b[k] ? a[k] - b[k] : b[k] - a[k];
						n[qi] += 3;
					}
				probe_say(c, "absdiff_quadrants", "%.1f %.1f %.1f %.1f", q[0] / n[0], q[1] / n[1], q[2] / n[2], q[3] / n[3]);
				probe_say(c, "src_px", "%u,%u,%u,%u", pixels[0], pixels[1], pixels[2], pixels[3]);
				probe_say(c, "read_px", "%u,%u,%u,%u", readpix[0], readpix[1], readpix[2], readpix[3]);
			}
			CGContextRelease(ctx2);
		}
	}
	probe_say(c, "mean", "%.1f", mean);
	if (mean < 1.0)
		pass = 0;
	(void)ext;

out:
	if (got)
		CGImageRelease(got);
	if (src)
		CFRelease(src);
	if (dest)
		CFRelease(dest);
	if (data)
		CFRelease(data);
	if (img)
		CGImageRelease(img);
	if (ctx)
		CGContextRelease(ctx);
	if (cs)
		CGColorSpaceRelease(cs);
	free(readpix);
	free(pixels);
	return pass ? 0 : 1;
}

static int test_write_read(struct probe_ctx *c, const char *ext, CFStringRef uti)
{
	return test_write_read_size(c, ext, uti, IMG_W, IMG_H);
}

static int t_jpeg(struct probe_ctx *c)
{
	return test_write_read(c, "jpeg", N2STR("public.jpeg"));
}

static int t_heic(struct probe_ctx *c)
{
	return test_write_read(c, "heic", N2STR("public.heic"));
}

static int t_png(struct probe_ctx *c)
{
	return test_write_read(c, "png", N2STR("public.png"));
}

/* Decode one embedded file and report its dimensions (and for JPEG the luma mean). */
static int decode_golden(struct probe_ctx *c, const char *name, const uint8_t *bytes, size_t size,
                         unsigned want_w, unsigned want_h, const char *label)
{
	CFDataRef data;
	CGImageSourceRef src;
	CGImageRef img;
	size_t w = 0, h = 0;
	char key[32];
	int pass = 1;

	probe_say(c, "golden", "%s", label);
	if (!bytes || size == 0) {
		probe_say(c, "golden_bytes", "0");
		return 2;
	}
	data = CFDataCreate(NULL, bytes, (CFIndex)size);
	src = data ? CGImageSourceCreateWithData(data, NULL) : NULL;
	img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
	if (data)
		CFRelease(data);
	if (src)
		CFRelease(src);
	if (!img) {
		probe_say(c, "golden_decoded", "0");
		return 1;
	}
	w = CGImageGetWidth(img);
	h = CGImageGetHeight(img);
	CGImageRelease(img);
	snprintf(key, sizeof key, "%s_wh", name);
	probe_say(c, key, "%zux%zu", w, h);
	if (w != want_w || h != want_h) {
		probe_say(c, "golden_match", "0");
		pass = 0;
	} else {
		probe_say(c, "golden_match", "1");
	}
	return pass ? 0 : 1;
}

static int t_jpeg_full(struct probe_ctx *c)
{
	int r = t_jpeg(c);
#if PROBE_HAVE_VECTORS
	{
		int g = decode_golden(c, "golden_jpeg", n2vec_jpeg, (size_t)n2vec_jpeg_size,
		                       N2VEC_JPEG_WIDTH, N2VEC_JPEG_HEIGHT, "vectors/jpeg-q85");
		if (g == 1)
			r = 1;
	}
#else
	probe_say(c, "golden", "absent");
#endif
	return r;
}

static int t_heic_full(struct probe_ctx *c)
{
	int r = t_heic(c);
#if PROBE_HAVE_VECTORS
	{
		int g = decode_golden(c, "golden_heic", n2vec_heic, (size_t)n2vec_heic_size,
		                       N2VEC_HEIC_WIDTH, N2VEC_HEIC_HEIGHT, "vectors/heic");
		if (g == 1)
			r = 1;
	}
#else
	probe_say(c, "golden", "absent");
#endif
	return r;
}

/* Which CMPhoto entry points exist in this process?  dlsym only: nothing private is called. */
static int t_cmphoto(struct probe_ctx *c)
{
	void *h = dlopen(N2_CMPHOTO_PATH, N2_RTLD_LAZY | N2_RTLD_LOCAL);
	int a, b;

	probe_say(c, "dlopen", "%d", h != NULL);
	if (!h) {
		const char *e = dlerror();
		probe_say(c, "error", "%s", e ? e : "(none)");
	}
	a = dlsym(h ? h : N2_RTLD_DEFAULT, "CMPhotoCompressionSessionCreate") != NULL;
	b = dlsym(h ? h : N2_RTLD_DEFAULT, "CMPhotoJPEGCreateJPEGFromCVPixelBuffer") != NULL;
	probe_say(c, "CMPhotoCompressionSessionCreate", "%d", a);
	probe_say(c, "CMPhotoJPEGCreateJPEGFromCVPixelBuffer", "%d", b);
	/* A measurement: it passes when the question was answered, whichever way. */
	return 0;
}

/* (lead) The same 176x144 picture as a JPEG and as a HEIC made on the build machine, both decoded here and compared:
 * tells a wrong HEIC DECODE (or its colour conversion) from a wrong HEIC ENCODE in img.write/read.heic. */
#if PROBE_HAVE_VECTORS
static uint8_t *decode_rgba(const uint8_t *bytes, size_t size, size_t *w, size_t *h)
{
	CFDataRef data = CFDataCreate(NULL, bytes, (CFIndex)size);
	CGImageSourceRef src = data ? CGImageSourceCreateWithData(data, NULL) : NULL;
	CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
	uint8_t *px = NULL;
	if (img) {
		CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
		*w = CGImageGetWidth(img);
		*h = CGImageGetHeight(img);
		if (*w && *h && *w <= 8192 && *h <= 8192)
			px = calloc(*w * *h, 4);
		if (px && cs) {
			CGContextRef ctx = CGBitmapContextCreate(px, *w, *h, 8, *w * 4, cs, IMG_BITMAP_INFO);
			if (ctx) {
				CGRect r;
				r.origin.x = 0; r.origin.y = 0; r.size.width = (CGFloat)*w; r.size.height = (CGFloat)*h;
				CGContextDrawImage(ctx, r, img);
				CGContextRelease(ctx);
			}
		}
		if (cs)
			CGColorSpaceRelease(cs);
		CGImageRelease(img);
	}
	if (src)
		CFRelease(src);
	if (data)
		CFRelease(data);
	return px;
}
#endif

static int t_golden_compare(struct probe_ctx *c)
{
#if PROBE_HAVE_VECTORS
	size_t jw = 0, jh = 0, hw = 0, hh = 0, i, n;
	uint8_t *j = decode_rgba(n2vec_jpeg, (size_t)n2vec_jpeg_size, &jw, &jh);
	uint8_t *h = decode_rgba(n2vec_heic, (size_t)n2vec_heic_size, &hw, &hh);
	double sum = 0;
	int pass = 0;
	probe_say(c, "jpeg", "%d %zux%zu", j != NULL, jw, jh);
	probe_say(c, "heic", "%d %zux%zu", h != NULL, hw, hh);
	if (j && h && jw == hw && jh == hh) {
		n = jw * jh;
		for (i = 0; i < n * 4; i++)
			if ((i & 3) != 3)
				sum += j[i] > h[i] ? j[i] - h[i] : h[i] - j[i];
		probe_say(c, "absdiff", "%.2f", sum / (double)(n * 3));
		probe_say(c, "jpeg_px", "%u,%u,%u,%u", j[0], j[1], j[2], j[3]);
		probe_say(c, "heic_px", "%u,%u,%u,%u", h[0], h[1], h[2], h[3]);
		probe_say(c, "jpeg_mid", "%u,%u,%u", j[(n / 2 + jw / 2) * 4], j[(n / 2 + jw / 2) * 4 + 1], j[(n / 2 + jw / 2) * 4 + 2]);
		probe_say(c, "heic_mid", "%u,%u,%u", h[(n / 2 + jw / 2) * 4], h[(n / 2 + jw / 2) * 4 + 1], h[(n / 2 + jw / 2) * 4 + 2]);
		pass = sum / (double)(n * 3) < 12.0;
	}
	free(j);
	free(h);
	return pass ? 0 : 1;
#else
	probe_say(c, "golden", "absent");
	return 2;
#endif
}

/* (lead) the camera's still size: how long does a 12 MP picture take in each format? */
static int t_big_jpeg(struct probe_ctx *c) { return test_write_read_size(c, "jpeg", N2STR("public.jpeg"), 4032, 3024); }
static int t_big_heic(struct probe_ctx *c) { return test_write_read_size(c, "heic", N2STR("public.heic"), 4032, 3024); }

const struct probe_test probe_img_tests[] = {
	{ "img.write/read.jpeg", t_jpeg_full, 60 },
	{ "img.write/read.heic", t_heic_full, 60 },
	{ "img.write/read.png",  t_png,       60 },
	{ "img.cmphoto",         t_cmphoto,   20 },
	{ "img.golden.heic-vs-jpeg", t_golden_compare, 30 },
	{ "img.big.jpeg",        t_big_jpeg,  90 },
	{ "img.big.heic",        t_big_heic,  180 },
};
const unsigned probe_img_tests_count = sizeof probe_img_tests / sizeof probe_img_tests[0];
