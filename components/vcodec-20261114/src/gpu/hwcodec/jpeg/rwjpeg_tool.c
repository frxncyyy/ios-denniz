/* SPDX-License-Identifier: MIT */
/*
 * rwjpeg_tool.c -- the on-phone exerciser of /dev/redwood-jpeg, and its host twin against the test's fake device.
 *
 *   rwjpeg_tool enc in.nv12 WxH Q out.jpg   one encode of a raw NV12 file (stride = W, or 16-padded when the file is padded)
 *   rwjpeg_tool bench WxH N [Q]             N encodes of the kernel's test picture; ms per megapixel, hardware vs wall
 *   rwjpeg_tool selftest [WxH]              the test picture at every hardware quality code; one PASS/FAIL line per code
 *   rwjpeg_tool pattern WxH OUT.nv12        write the kernel's test picture (the bytes selftest/bench encode)
 *   rwjpeg_tool info                        RWJ_INFO of the device
 *
 * Every line is printed to stdout and written to /dev/console (the phone has no terminal when launched from a launchd job),
 * prefixed "N2JPG ".  Exit codes: 0 success, 1 failure/usage, 2 no /dev/redwood-jpeg (software fallback).
 *
 * The test picture is the kernel's n2j_pattern (gpu/hwcodec/kernel/b/bsd/dev/nord2_hwcodec.inc): a horizontal luma ramp with
 * a 16-pixel checkerboard and an 8-level noise term, Cb a vertical ramp, Cr a horizontal ramp, rows padded to 16.  It is
 * generated here with the same integer formula so the phone can compare the JPEG against the picture the kernel used in M1.
 *
 * The host test compiles this file with -DN2RWJ_TOOL_TEST and supplies n2rwj_test_open(), so the command line, the file
 * handling, the marker checker and the selftest logic all run unchanged against a fake device backed by cjpeg.
 *
 * SPDX-License-Identifier: MIT
 */
#if !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L       /* clock_gettime/CLOCK_MONOTONIC on a hosted libc with -std=c11 */
#endif
#include "rwjpeg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TOOL_MAX_LINE 1024

/* ------------------------------------------------------------------ output */

static void
say(const char *fmt, ...)
{
	char line[TOOL_MAX_LINE];
	va_list ap;
	int n, fd;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n >= sizeof(line))
		n = (int)sizeof(line) - 1;
	printf("%s\n", line);
	fflush(stdout);
	fd = open("/dev/console", O_WRONLY);
	if (fd >= 0) {
		(void)write(fd, line, (size_t)n);
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}
}

static uint64_t
now_us(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* ------------------------------------------------------------------ files */

static int
read_file(const char *path, uint8_t **data_out, size_t *size_out)
{
	uint8_t *data = NULL;
	size_t size = 0, cap = 0;
	int fd;

	*data_out = NULL;
	*size_out = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		say("N2JPG error: open(%s): %s", path, strerror(errno));
		return -1;
	}
	for (;;) {
		ssize_t got;

		if (size == cap) {
			size_t next = cap ? cap * 2u : (size_t)1u << 20;
			uint8_t *grown = (uint8_t *)realloc(data, next);

			if (grown == NULL) {
				say("N2JPG error: out of memory reading %s", path);
				free(data);
				(void)close(fd);
				return -1;
			}
			data = grown;
			cap = next;
		}
		got = read(fd, data + size, cap - size);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			say("N2JPG error: read(%s): %s", path, strerror(errno));
			free(data);
			(void)close(fd);
			return -1;
		}
		if (got == 0)
			break;
		size += (size_t)got;
	}
	(void)close(fd);
	*data_out = data;
	*size_out = size;
	return 0;
}

static int
write_file(const char *path, const void *data, size_t size)
{
	const uint8_t *p = (const uint8_t *)data;
	size_t done = 0;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		say("N2JPG error: open(%s): %s", path, strerror(errno));
		return -1;
	}
	while (done < size) {
		ssize_t put = write(fd, p + done, size - done);

		if (put < 0) {
			if (errno == EINTR)
				continue;
			say("N2JPG error: write(%s): %s", path, strerror(errno));
			(void)close(fd);
			return -1;
		}
		done += (size_t)put;
	}
	if (close(fd) != 0) {
		say("N2JPG error: close(%s): %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ the kernel's test picture */

static uint32_t
align16(uint32_t v)
{
	return (v + 15u) & ~15u;
}

/* n2j_pattern() from the kernel, same integer arithmetic: stride x rows luma, then rows/2 chroma lines of `stride` bytes. */
static void
test_pattern(uint8_t *p, uint32_t w, uint32_t h, uint32_t stride, uint32_t rows)
{
	uint32_t x, y;
	uint8_t *c = p + (size_t)stride * rows;

	for (y = 0; y < rows; y++) {
		uint32_t yy = y < h ? y : h - 1;

		for (x = 0; x < stride; x++) {
			uint32_t xx = x < w ? x : w - 1;

			p[(size_t)y * stride + x] = (uint8_t)(16u + xx * 180u / w +
				(((((xx >> 4) + (yy >> 4)) & 1u) ? 40u : 0u)) + ((xx * 7u + yy * 13u) & 7u));
		}
	}
	for (y = 0; y < rows / 2u; y++) {
		uint32_t yy = 2u * y < h ? 2u * y : h - 1;

		for (x = 0; x < stride / 2u; x++) {
			uint32_t xx = 2u * x < w ? 2u * x : w - 1;

			c[(size_t)y * stride + 2u * x] = (uint8_t)(78u + yy * 100u / h);         /* Cb */
			c[(size_t)y * stride + 2u * x + 1u] = (uint8_t)(78u + xx * 100u / w);     /* Cr */
		}
	}
}

/* ------------------------------------------------------------------ JPEG structure check */

struct jpeg_info {
	unsigned width, height;
	unsigned components;
	unsigned sampling;              /* first component's H/V sampling nibble */
	unsigned sof_marker;            /* 0xc0, 0xc2, ... */
	int      dqt, dht, dri;
};

/* Walk the marker chain from SOI to SOS.  Returns 0 when a complete baseline-shaped file is found. */
static int
jpeg_parse(const uint8_t *p, size_t n, struct jpeg_info *info)
{
	size_t pos = 2;

	memset(info, 0, sizeof(*info));
	if (n < 4 || p[0] != 0xff || p[1] != 0xd8)
		return -1;
	while (pos + 2 <= n) {
		uint8_t marker;
		size_t len;

		if (p[pos] != 0xff)
			return -1;
		marker = p[pos + 1];
		if (marker == 0xff) {
			pos++;
			continue;
		}
		if (marker == 0xd9)
			return -1;                              /* EOI before SOS */
		if (marker == 0xda)
			return 0;                               /* SOS: the entropy data follows */
		if ((marker >= 0xd0 && marker <= 0xd7) || marker == 0x01)
			return -1;                              /* not valid before SOS */
		if (pos + 4 > n)
			return -1;
		len = ((size_t)p[pos + 2] << 8) | p[pos + 3];
		if (len < 2 || pos + 2 + len > n)
			return -1;

		switch (marker) {
		case 0xdb:
			info->dqt = 1;
			break;
		case 0xc4:
			info->dht = 1;
			break;
		case 0xdd:
			info->dri = 1;
			break;
		default:
			if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2 || marker == 0xc3) {
				const uint8_t *q = p + pos + 4;

				if (len < 8)
					return -1;
				info->sof_marker = marker;
				info->height = ((unsigned)q[1] << 8) | q[2];
				info->width = ((unsigned)q[3] << 8) | q[4];
				info->components = q[5];
				info->sampling = len >= 9 ? q[7] : 0;    /* first component's sampling factors */
			}
			break;
		}
		pos += 2 + len;
	}
	return -1;
}

/* One complete JPEG file of exactly w x h, baseline 4:2:0, with the two tables the decoder needs. */
static int
jpeg_check(const uint8_t *p, size_t n, unsigned w, unsigned h, char *why, size_t why_cap)
{
	struct jpeg_info info;

	if (n < 4 || p[0] != 0xff || p[1] != 0xd8) {
		snprintf(why, why_cap, "no SOI");
		return -1;
	}
	if (p[n - 2] != 0xff || p[n - 1] != 0xd9) {
		snprintf(why, why_cap, "no EOI at %zu", n);
		return -1;
	}
	if (jpeg_parse(p, n, &info) != 0) {
		snprintf(why, why_cap, "marker chain does not reach SOS");
		return -1;
	}
	if (info.sof_marker != 0xc0) {
		snprintf(why, why_cap, "SOF is 0x%02x, not baseline SOF0", info.sof_marker);
		return -1;
	}
	if (info.width != w || info.height != h) {
		snprintf(why, why_cap, "SOF is %ux%u, want %ux%u", info.width, info.height, w, h);
		return -1;
	}
	if (info.components != 3) {
		snprintf(why, why_cap, "%u components, want 3", info.components);
		return -1;
	}
	if (info.sampling != 0x22) {
		snprintf(why, why_cap, "sampling 0x%02x, want 0x22 (4:2:0)", info.sampling);
		return -1;
	}
	if (!info.dqt || !info.dht) {
		snprintf(why, why_cap, "missing %s", !info.dqt ? "DQT" : "DHT");
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ device */

static int
tool_open(rwjpeg **dev)
{
#ifdef N2RWJ_TOOL_TEST
	extern int n2rwj_test_open(rwjpeg **out);

	return n2rwj_test_open(dev);
#else
	const char *path = getenv("N2RWJ_DEV");

	return rwjpeg_open(dev, path);
#endif
}

static unsigned
parse_size(const char *s, uint32_t *w, uint32_t *h)
{
	unsigned a, b;

	if (sscanf(s, "%ux%u", &a, &b) != 2)
		return 0;
	*w = (uint32_t)a;
	*h = (uint32_t)b;
	return 1;
}

/* The block's limits (redwood_jpeg.h): even, 16..8192, at most 12.6 MP. */
static int
dims_ok(uint32_t w, uint32_t h)
{
	return w >= 16 && h >= 16 && (w & 1u) == 0 && (h & 1u) == 0 &&
	       w <= RWJ_MAX_WIDTH && h <= RWJ_MAX_HEIGHT &&
	       (uint64_t)w * h <= (uint64_t)RWJ_MAX_PIXELS;
}

static unsigned
parse_uint(const char *s, unsigned fallback)
{
	char *end = NULL;
	unsigned long v = strtoul(s, &end, 10);

	if (end == s || *end != '\0')
		return fallback;
	return (unsigned)v;
}

static uint8_t *
alloc_jpeg(uint32_t w, uint32_t h, size_t *cap_out)
{
	/* The kernel wants at least w*h/2 + 4096; a worst-case JPEG is under 1.5 bytes per pixel for a photograph, so give
	 * the encoder room and let rwjpeg report too-small instead of failing inside the kernel. */
	size_t cap = (size_t)w * h + (size_t)w * h / 2u + 65536u;

	*cap_out = cap;
	return (uint8_t *)malloc(cap);
}

/* ------------------------------------------------------------------ verbs */

static int
verb_info(void)
{
	rwjpeg *dev = NULL;
	struct rwj_info info;
	int status = tool_open(&dev);

	if (status != RWJPEG_OK) {
		say("N2JPG info: no device (%s)", rwjpeg_status_name(status));
		return status == RWJPEG_E_FALLBACK ? 2 : 1;
	}
	status = rwjpeg_info(dev, &info);
	rwjpeg_close(dev);
	if (status != RWJPEG_OK) {
		say("N2JPG info: RWJ_INFO failed (%s)", rwjpeg_status_name(status));
		return 1;
	}
	say("N2JPG info abi=%u max_pixels=%u jobs=%u failed=%u powered=%u hw_us=%u caps=0x%x",
	    info.abi_version, info.max_pixels, info.jobs_done, info.jobs_failed, info.powered, info.last_hw_us, info.caps);
	return 0;
}

static int
verb_enc(const char *in_path, const char *size_arg, unsigned quality, const char *out_path)
{
	uint32_t w, h, stride, rows;
	uint8_t *file = NULL;
	size_t file_size = 0, cap = 0, jpeg_size = 0;
	uint8_t *jpeg = NULL;
	uint64_t t0, t1;
	uint32_t hw_us = 0;
	rwjpeg *dev = NULL;
	int status, rc = 1;

	if (!parse_size(size_arg, &w, &h) || !dims_ok(w, h)) {
		say("N2JPG error: size '%s' is not a usable even WxH up to %ux%u", size_arg, RWJ_MAX_WIDTH, RWJ_MAX_HEIGHT);
		return 1;
	}
	if (read_file(in_path, &file, &file_size) != 0)
		return 1;
	if (file_size == (size_t)w * h * 3u / 2u) {
		stride = w;
		rows = h;
	} else if (file_size == (size_t)align16(w) * align16(h) * 3u / 2u) {
		stride = align16(w);
		rows = align16(h);
	} else {
		say("N2JPG error: %s is %zu bytes; %ux%u NV12 is %zu (tight) or %zu (16-padded rows)",
		    in_path, file_size, w, h, (size_t)w * h * 3u / 2u, (size_t)align16(w) * align16(h) * 3u / 2u);
		free(file);
		return 1;
	}
	jpeg = alloc_jpeg(w, h, &cap);
	if (jpeg == NULL) {
		say("N2JPG error: out of memory");
		free(file);
		return 1;
	}

	status = tool_open(&dev);
	if (status != RWJPEG_OK) {
		say("N2JPG enc %ux%u q=%u: no device (%s)", w, h, quality, rwjpeg_status_name(status));
		rc = status == RWJPEG_E_FALLBACK ? 2 : 1;
		goto done;
	}
	t0 = now_us();
	status = rwjpeg_encode_nv12(dev, file, file + (size_t)stride * rows, stride, stride, w, h, quality,
	                            jpeg, cap, &jpeg_size, &hw_us);
	t1 = now_us();
	if (status != RWJPEG_OK) {
		say("N2JPG enc %ux%u q=%u: encode failed (%s)", w, h, quality, rwjpeg_status_name(status));
		rc = status == RWJPEG_E_FALLBACK ? 2 : 1;
		goto done;
	}
	{
		char why[128];

		if (jpeg_check(jpeg, jpeg_size, w, h, why, sizeof(why)) != 0) {
			say("N2JPG enc %ux%u q=%u: %zu bytes, but %s", w, h, quality, jpeg_size, why);
			goto done;
		}
	}
	if (write_file(out_path, jpeg, jpeg_size) != 0)
		goto done;
	say("N2JPG enc %ux%u q=%u code=0x%x bytes=%zu hw_us=%u wall_us=%llu -> %s",
	    w, h, quality, rwjpeg_quality_code(quality), jpeg_size, hw_us, (unsigned long long)(t1 - t0), out_path);
	rc = 0;

done:
	rwjpeg_close(dev);
	free(jpeg);
	free(file);
	return rc;
}

static int
verb_bench(const char *size_arg, unsigned count, unsigned quality)
{
	uint32_t w, h, stride, rows;
	uint8_t *picture = NULL;
	uint8_t *jpeg = NULL;
	size_t cap = 0, jpeg_size = 0, pixels;
	uint64_t wall_total = 0, hw_total = 0;
	unsigned i;
	rwjpeg *dev = NULL;
	int status, rc = 1;

	if (!parse_size(size_arg, &w, &h) || !dims_ok(w, h) || count == 0) {
		say("N2JPG error: usage: bench WxH N [Q] (WxH even, 16..%u)", RWJ_MAX_WIDTH);
		return 1;
	}
	stride = align16(w);
	rows = align16(h);
	picture = (uint8_t *)malloc((size_t)stride * rows * 3u / 2u);
	jpeg = alloc_jpeg(w, h, &cap);
	if (picture == NULL || jpeg == NULL) {
		say("N2JPG error: out of memory");
		goto done;
	}
	test_pattern(picture, w, h, stride, rows);

	status = tool_open(&dev);
	if (status != RWJPEG_OK) {
		say("N2JPG bench %ux%u: no device (%s)", w, h, rwjpeg_status_name(status));
		rc = status == RWJPEG_E_FALLBACK ? 2 : 1;
		goto done;
	}
	for (i = 0; i < count; i++) {
		uint64_t t0 = now_us(), t1;
		uint32_t hw_us = 0;

		status = rwjpeg_encode_nv12(dev, picture, picture + (size_t)stride * rows, stride, stride, w, h, quality,
		                            jpeg, cap, &jpeg_size, &hw_us);
		t1 = now_us();
		if (status != RWJPEG_OK) {
			say("N2JPG bench %ux%u: encode %u failed (%s)", w, h, i, rwjpeg_status_name(status));
			rc = status == RWJPEG_E_FALLBACK ? 2 : 1;
			goto done;
		}
		wall_total += t1 - t0;
		hw_total += hw_us;
	}
	pixels = (size_t)w * h;
	{
		double mp = (double)pixels / 1000000.0;
		double wall_frame = (double)wall_total / (double)count / 1000.0;
		double hw_frame = (double)hw_total / (double)count / 1000.0;

		say("N2JPG bench %ux%u n=%u q=%u code=0x%x bytes=%zu wall_ms_frame=%.3f hw_ms_frame=%.3f "
		    "wall_ms_per_mp=%.3f hw_ms_per_mp=%.3f wall_fps=%.1f",
		    w, h, count, quality, rwjpeg_quality_code(quality), jpeg_size, wall_frame, hw_frame,
		    wall_frame / mp, hw_frame / mp, wall_frame > 0.0 ? 1000.0 / wall_frame : 0.0);
	}
	rc = 0;

done:
	rwjpeg_close(dev);
	free(jpeg);
	free(picture);
	return rc;
}

static int
verb_pattern(const char *size_arg, const char *out_path)
{
	uint32_t w, h, stride, rows;
	uint8_t *picture;

	if (!parse_size(size_arg, &w, &h) || !dims_ok(w, h)) {
		say("N2JPG error: size '%s' is not a usable even WxH", size_arg);
		return 1;
	}
	stride = align16(w);
	rows = align16(h);
	picture = (uint8_t *)malloc((size_t)stride * rows * 3u / 2u);
	if (picture == NULL) {
		say("N2JPG error: out of memory");
		return 1;
	}
	test_pattern(picture, w, h, stride, rows);
	if (write_file(out_path, picture, (size_t)stride * rows * 3u / 2u) != 0) {
		free(picture);
		return 1;
	}
	say("N2JPG pattern %ux%u stride=%u rows=%u bytes=%zu -> %s", w, h, stride, rows,
	    (size_t)stride * rows * 3u / 2u, out_path);
	free(picture);
	return 0;
}

static int
verb_selftest(const char *size_arg)
{
	/* The quality-percent values whose nearest code is each of the hardware's eleven codes (redwood_jpeg.h). */
	static const unsigned qualities[] = { 97, 95, 90, 85, 78, 72, 60, 52, 44, 38, 30 };
	static const unsigned codes[] = { 0x10, 3, 7, 2, 6, 0xf, 1, 0xa, 0xe, 0, 4 };
	uint32_t w = 640, h = 480, stride, rows;
	uint8_t *picture = NULL;
	uint8_t *jpeg = NULL;
	size_t cap = 0, jpeg_size = 0;
	size_t sizes[sizeof(qualities) / sizeof(qualities[0])];
	unsigned i, passed = 0, total = 0;
	rwjpeg *dev = NULL;
	int status, rc = 1;

	if (size_arg != NULL && (!parse_size(size_arg, &w, &h) || !dims_ok(w, h))) {
		say("N2JPG error: size '%s' is not a usable even WxH", size_arg);
		return 1;
	}
	stride = align16(w);
	rows = align16(h);
	picture = (uint8_t *)malloc((size_t)stride * rows * 3u / 2u);
	jpeg = alloc_jpeg(w, h, &cap);
	if (picture == NULL || jpeg == NULL) {
		say("N2JPG error: out of memory");
		goto done;
	}
	test_pattern(picture, w, h, stride, rows);

	status = tool_open(&dev);
	if (status != RWJPEG_OK) {
		say("N2JPG selftest %ux%u FAIL no device (%s)", w, h, rwjpeg_status_name(status));
		rc = status == RWJPEG_E_FALLBACK ? 2 : 1;
		goto done;
	}

	for (i = 0; i < sizeof(qualities) / sizeof(qualities[0]); i++) {
		char why[128];
		uint32_t hw_us = 0;

		total++;
		sizes[i] = 0;
		status = rwjpeg_encode_nv12(dev, picture, picture + (size_t)stride * rows, stride, stride, w, h,
		                            qualities[i], jpeg, cap, &jpeg_size, &hw_us);
		if (status != RWJPEG_OK) {
			say("N2JPG selftest q=%u code=0x%x FAIL encode (%s)", qualities[i], codes[i],
			    rwjpeg_status_name(status));
			if (status == RWJPEG_E_FALLBACK) {
				rc = 2;
				goto done;
			}
			continue;
		}
		if (rwjpeg_quality_code(qualities[i]) != codes[i]) {
			say("N2JPG selftest q=%u code=0x%x FAIL our own mapping gives 0x%x", qualities[i], codes[i],
			    rwjpeg_quality_code(qualities[i]));
			continue;
		}
		if (jpeg_check(jpeg, jpeg_size, w, h, why, sizeof(why)) != 0) {
			say("N2JPG selftest q=%u code=0x%x FAIL %s", qualities[i], codes[i], why);
			continue;
		}
		sizes[i] = jpeg_size;
		passed++;
		say("N2JPG selftest q=%u code=0x%x bytes=%zu hw_us=%u PASS", qualities[i], codes[i], jpeg_size, hw_us);
	}

	/* The ladder: the highest quality code must be bigger than the lowest on this picture.  A kernel that ignored the
	 * quality field returns the same size for both and is caught here. */
	total++;
	if (sizes[0] > 0 && sizes[sizeof(sizes) / sizeof(sizes[0]) - 1] > 0 &&
	    sizes[0] > sizes[sizeof(sizes) / sizeof(sizes[0]) - 1]) {
		passed++;
		say("N2JPG selftest q=97 (%zu bytes) > q=30 (%zu bytes) PASS",
		    sizes[0], sizes[sizeof(sizes) / sizeof(sizes[0]) - 1]);
	} else {
		say("N2JPG selftest quality ladder FAIL q97=%zu q30=%zu",
		    sizes[0], sizes[sizeof(sizes) / sizeof(sizes[0]) - 1]);
	}

	rc = (passed == total) ? 0 : 1;
	say("N2JPG selftest %ux%u %s %u/%u", w, h, rc == 0 ? "PASS" : "FAIL", passed, total);

done:
	rwjpeg_close(dev);
	free(jpeg);
	free(picture);
	return rc;
}

static void
usage(void)
{
	say("usage: rwjpeg_tool enc IN.nv12 WxH Q OUT.jpg | bench WxH N [Q] | selftest [WxH] | pattern WxH OUT.nv12 | info");
	say("       N2RWJ_DEV overrides the device path (default %s)", RWJPEG_DEV_PATH);
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		usage();
		return 1;
	}
	if (strcmp(argv[1], "enc") == 0) {
		if (argc != 6) {
			usage();
			return 1;
		}
		return verb_enc(argv[2], argv[3], parse_uint(argv[4], 0), argv[5]);
	}
	if (strcmp(argv[1], "bench") == 0) {
		unsigned count, quality = 90;

		if (argc != 4 && argc != 5) {
			usage();
			return 1;
		}
		count = parse_uint(argv[3], 0);
		if (argc == 5)
			quality = parse_uint(argv[4], 0);
		if (quality < 1 || quality > 100) {
			say("N2JPG error: quality must be 1..100");
			return 1;
		}
		return verb_bench(argv[2], count, quality);
	}
	if (strcmp(argv[1], "selftest") == 0) {
		if (argc > 3) {
			usage();
			return 1;
		}
		return verb_selftest(argc == 3 ? argv[2] : NULL);
	}
	if (strcmp(argv[1], "pattern") == 0) {
		if (argc != 4) {
			usage();
			return 1;
		}
		return verb_pattern(argv[2], argv[3]);
	}
	if (strcmp(argv[1], "info") == 0 && argc == 2)
		return verb_info();
	usage();
	return 1;
}
