/* SPDX-License-Identifier: MIT */
/*
 * fake_dev.c -- host-test stand-in for /dev/redwood-jpeg.  The ioctl contract is include/redwood_jpeg.h; the JPEG bytes come
 * from cjpeg (a real, independent encoder), so every output the library returns is a real JPEG file that djpeg can decode.
 *
 * NV12 is converted to PPM with the JPEG full-range BT.601 inverse so cjpeg sees the picture the hardware would have seen.
 * The quality request goes through the same eleven hardware codes as the kernel and cjpeg is asked for the nominal quality of
 * that code, so the selftest's quality ladder is meaningful.  Failure modes are flags the tests set.
 */
#include "fake_dev.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct fake_dev n2fake_dev;

static uint64_t g_file_serial;

const char *
fake_scratch(void)
{
	const char *dir = getenv("N2J02_SCRATCH");

	if (dir != NULL && dir[0] != '\0')
		return dir;
	return "/tmp";
}

static int
fake_tmp_path(char *buf, size_t cap, const char *suffix)
{
	const char *dir = fake_scratch();
	int n = snprintf(buf, cap, "%s/n2j02-%ld-%llu-%s", dir, (long)getpid(),
	                 (unsigned long long)++g_file_serial, suffix);

	return n > 0 && (size_t)n < cap ? 0 : -1;
}

static uint8_t
fake_clamp(int v)
{
	if (v < 0)
		return 0;
	if (v > 255)
		return 255;
	return (uint8_t)v;
}

static int
fake_write_ppm(const char *path, const uint8_t *y, const uint8_t *c,
               uint32_t w, uint32_t h, uint32_t y_stride, uint32_t c_stride, int nv21)
{
	uint8_t *row = (uint8_t *)malloc((size_t)w * 3u);
	FILE *f;
	uint32_t x, yy;

	if (row == NULL)
		return -1;
	f = fopen(path, "wb");
	if (f == NULL) {
		free(row);
		return -1;
	}
	fprintf(f, "P6\n%u %u\n255\n", w, h);
	for (yy = 0; yy < h; yy++) {
		const uint8_t *luma = y + (size_t)yy * y_stride;
		const uint8_t *chroma = c + (size_t)(yy / 2u) * c_stride;

		for (x = 0; x < w; x++) {
			int Y = luma[x];
			int cb = chroma[2u * (x / 2u)];
			int cr = chroma[2u * (x / 2u) + 1u];
			int d, e, r, g, b;

			if (nv21) {
				int t = cb;

				cb = cr;
				cr = t;
			}
			d = cb - 128;
			e = cr - 128;
			r = Y + ((5743 * e) >> 12);
			g = Y - ((1410 * d + 2925 * e) >> 12);
			b = Y + ((7258 * d) >> 12);
			row[3u * x + 0u] = fake_clamp(r);
			row[3u * x + 1u] = fake_clamp(g);
			row[3u * x + 2u] = fake_clamp(b);
		}
		if (fwrite(row, 3, w, f) != w) {
			fclose(f);
			free(row);
			return -1;
		}
	}
	free(row);
	return fclose(f) == 0 ? 0 : -1;
}

static int
fake_read_file(const char *path, uint8_t **out, size_t *size_out)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data = NULL;
	size_t size = 0, cap = 0;

	*out = NULL;
	*size_out = 0;
	if (f == NULL)
		return -1;
	for (;;) {
		size_t got;

		if (size == cap) {
			size_t next = cap ? cap * 2u : 65536u;
			uint8_t *grown = (uint8_t *)realloc(data, next);

			if (grown == NULL) {
				free(data);
				fclose(f);
				return -1;
			}
			data = grown;
			cap = next;
		}
		got = fread(data + size, 1, cap - size, f);
		size += got;
		if (got == 0)
			break;
	}
	if (ferror(f)) {
		free(data);
		fclose(f);
		return -1;
	}
	fclose(f);
	*out = data;
	*size_out = size;
	return 0;
}

/* The nominal percent of a hardware quality code (redwood_jpeg.h / mtk_jpeg_core.h). */
static unsigned
fake_code_percent(unsigned code)
{
	switch (code) {
	case 0x10: return 97;
	case 0x03: return 95;
	case 0x07: return 90;
	case 0x02: return 85;
	case 0x06: return 78;
	case 0x0f: return 72;
	case 0x01: return 60;
	case 0x0a: return 52;
	case 0x0e: return 44;
	case 0x00: return 38;
	default:   return 30;
	}
}

static int
fake_encode(struct rwj_encode *req)
{
	char ppm[512], jpg[512], cmd[1200];
	const char *cjpeg = getenv("N2J02_CJPEG");
	const uint8_t *y = (const uint8_t *)(uintptr_t)req->src_y;
	const uint8_t *c = (const uint8_t *)(uintptr_t)req->src_c;
	uint8_t *jpeg = NULL;
	size_t jpeg_size = 0;
	unsigned percent;
	uint8_t *dst = (uint8_t *)(uintptr_t)req->dst;

	n2fake_dev.last = *req;
	n2fake_dev.have_last = 1;
	if (req->format != RWJ_FMT_NV12 && req->format != RWJ_FMT_NV21) {
		errno = EINVAL;
		return -1;
	}
	if (req->dst_capacity < (uint64_t)req->width * req->height / 2u + 4096u || n2fake_dev.fail_enospc) {
		errno = ENOSPC;
		req->out_status = RWJ_E_SIZE;
		return -1;
	}
	if (fake_tmp_path(ppm, sizeof(ppm), "ppm") != 0 || fake_tmp_path(jpg, sizeof(jpg), "jpg") != 0)
		return -1;
	if (fake_write_ppm(ppm, y, c, req->width, req->height, req->stride, req->chroma_stride,
	                   req->format == RWJ_FMT_NV21) != 0) {
		errno = EIO;
		return -1;
	}
	percent = fake_code_percent(rwjpeg_quality_code(req->quality));
	snprintf(cmd, sizeof(cmd), "nice -n 10 %s -quality %u -outfile %s %s",
	         cjpeg != NULL ? cjpeg : "cjpeg", percent, jpg, ppm);
	if (system(cmd) != 0) {
		(void)unlink(ppm);
		errno = EIO;
		return -1;
	}
	(void)unlink(ppm);
	if (fake_read_file(jpg, &jpeg, &jpeg_size) != 0) {
		(void)unlink(jpg);
		errno = EIO;
		return -1;
	}
	(void)unlink(jpg);
	if (n2fake_dev.fail_noeoi && jpeg_size > 2)
		jpeg_size -= 2;                 /* truncate the EOI: the library must reject this */
	if (jpeg_size + (size_t)n2fake_dev.pad > req->dst_capacity) {
		free(jpeg);
		errno = ENOSPC;
		req->out_status = RWJ_E_SIZE;
		return -1;
	}
	memcpy(dst, jpeg, jpeg_size);
	if (n2fake_dev.pad > 0)
		memset(dst + jpeg_size, 0xa5, (size_t)n2fake_dev.pad);
	req->out_bytes = (uint32_t)(jpeg_size + (size_t)n2fake_dev.pad);
	req->out_hw_us = 100u + (uint32_t)(jpeg_size / 64u);
	req->out_status = 0;
	n2fake_dev.last = *req;         /* again, now with the result fields */
	n2fake_dev.jobs++;
	free(jpeg);
	return 0;
}

int
fake_ioctl(void *ctx, unsigned long request, void *arg)
{
	(void)ctx;
	if (arg == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (request == RWJ_INFO) {
		struct rwj_info *info = (struct rwj_info *)arg;

		if (n2fake_dev.fail_info) {
			errno = ENODEV;
			return -1;
		}
		memset(info, 0, sizeof(*info));
		info->abi_version = RWJ_ABI_VERSION;
		info->max_pixels = RWJ_MAX_PIXELS;
		info->jobs_done = (uint32_t)n2fake_dev.jobs;
		info->jobs_failed = (uint32_t)n2fake_dev.jobs_failed;
		info->powered = 1;
		info->caps = 1u;
		return 0;
	}
	if (request == RWJ_ENCODE) {
		struct rwj_encode *req = (struct rwj_encode *)arg;

		if (n2fake_dev.fail_eio) {
			n2fake_dev.jobs_failed++;
			req->out_status = RWJ_E_TIMEOUT;
			req->out_bytes = 0;
			errno = EIO;
			return -1;
		}
		if (fake_encode(req) != 0) {
			n2fake_dev.jobs_failed++;
			return -1;
		}
		return 0;
	}
	errno = ENOTTY;
	return -1;
}

int
n2rwj_test_open(rwjpeg **out)
{
	return rwjpeg_open_ioctl(out, fake_ioctl, &n2fake_dev);
}

void
fake_reset(void)
{
	memset(&n2fake_dev, 0, sizeof(n2fake_dev));
}
