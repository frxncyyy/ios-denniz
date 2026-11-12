/* SPDX-License-Identifier: MIT */
/*
 * rwjpeg.c -- client library of /dev/redwood-jpeg (see rwjpeg.h and include/redwood_jpeg.h).
 *
 * The only syscalls here are open/close/ioctl on the device node; every argument check and every JPEG structural decision is
 * local C, and the ioctl is behind a function pointer so the host test exercises this file byte for byte without a device.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rwjpeg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
/* gpu/video/sdk/sysinc has no sys/ioctl.h and this library is compiled with -nostdinc there.  Darwin's prototype is
 * int ioctl(int, unsigned long, ...); declaring it exactly is the whole header. */
extern int ioctl(int fd, unsigned long request, ...);
#else
#include <sys/ioctl.h>
#endif

#define RWJ_MIN_CAP(w, h)   ((size_t)(w) * (size_t)(h) / 2u + 4096u)
#define RWJ_APP_MAX         (65533u)

struct rwjpeg {
	int             fd;             /* -1 for a test handle */
	rwjpeg_ioctl_fn fn;             /* NULL: use fd */
	void           *ctx;
	uint32_t        last_status;    /* out_status of the last encode, for diagnostics */
	int             last_errno;
	uint8_t        *scratch;        /* BGRA -> NV12 conversion buffer */
	size_t          scratch_cap;
};

/* ------------------------------------------------------------------ helpers */

static int
dev_ioctl(rwjpeg *dev, unsigned long request, void *arg)
{
	if (dev == NULL || (dev->fn == NULL && dev->fd < 0))
		return -1;
	if (dev->fn != NULL)
		return dev->fn(dev->ctx, request, arg);
	return ioctl(dev->fd, request, arg);
}

const char *
rwjpeg_status_name(int status)
{
	switch (status) {
	case RWJPEG_OK:         return "ok";
	case RWJPEG_E_INVAL:    return "invalid argument";
	case RWJPEG_E_FALLBACK: return "fallback";
	case RWJPEG_E_NOSPC:    return "too small";
	case RWJPEG_E_NOMEM:    return "out of memory";
	case RWJPEG_E_JPEG:     return "bad jpeg";
	default:                return "unknown";
	}
}

static uint8_t
clamp_u8(int v)
{
	if (v < 0)
		return 0;
	if (v > 255)
		return 255;
	return (uint8_t)v;
}

/* The hardware's size limits (redwood_jpeg.h) and its even-size rule, shared by every entry point. */
static int
size_ok(uint32_t w, uint32_t h)
{
	return w >= 16 && h >= 16 && (w & 1u) == 0 && (h & 1u) == 0 &&
	       w <= RWJ_MAX_WIDTH && h <= RWJ_MAX_HEIGHT &&
	       (uint64_t)w * (uint64_t)h <= (uint64_t)RWJ_MAX_PIXELS;
}

unsigned
rwjpeg_quality_code(unsigned quality)
{
	if (quality < 1)
		quality = 1;
	if (quality > 100)
		quality = 100;
	if (quality >= 97)
		return 0x10u;   /* Q97 */
	if (quality >= 95)
		return 3u;      /* Q95 */
	if (quality >= 90)
		return 7u;      /* Q90 */
	if (quality >= 85)
		return 2u;      /* Q85 */
	if (quality >= 78)
		return 6u;      /* Q78 */
	if (quality >= 72)
		return 0x0fu;   /* Q72 */
	if (quality >= 60)
		return 1u;      /* Q60 */
	if (quality >= 52)
		return 0x0au;   /* Q52 */
	if (quality >= 44)
		return 0x0eu;   /* Q44 */
	if (quality >= 38)
		return 0u;      /* Q38 */
	return 4u;              /* Q30 */
}

size_t
rwjpeg_eoi_size(const void *jpeg, size_t bytes)
{
	const uint8_t *p = (const uint8_t *)jpeg;
	size_t i;

	if (p == NULL || bytes < 2)
		return 0;
	for (i = bytes - 1; i >= 1; i--) {
		if (p[i - 1] == 0xff && p[i] == 0xd9)
			return i + 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ device */

static void
free_dev(rwjpeg *dev)
{
	if (dev == NULL)
		return;
	if (dev->fd >= 0)
		(void)close(dev->fd);
	free(dev->scratch);
	free(dev);
}

static int
rwjpeg_open_common(rwjpeg **out, int fd, rwjpeg_ioctl_fn fn, void *ctx)
{
	rwjpeg *dev = (rwjpeg *)calloc(1, sizeof(*dev));

	if (dev == NULL)
		return RWJPEG_E_NOMEM;
	dev->fd = fd;
	dev->fn = fn;
	dev->ctx = ctx;
	*out = dev;
	return RWJPEG_OK;
}

int
rwjpeg_open_ioctl(rwjpeg **out, rwjpeg_ioctl_fn fn, void *ctx)
{
	if (out == NULL || fn == NULL)
		return RWJPEG_E_INVAL;
	*out = NULL;
	return rwjpeg_open_common(out, -1, fn, ctx);
}

int
rwjpeg_open(rwjpeg **out, const char *path)
{
	struct rwj_info info;
	rwjpeg *dev = NULL;
	int fd, status;

	if (out == NULL)
		return RWJPEG_E_INVAL;
	*out = NULL;
	if (path == NULL)
		path = RWJPEG_DEV_PATH;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		int e = errno;

		if (e == EISDIR || e == ENOTDIR || e == ENAMETOOLONG || e == ELOOP)
			return RWJPEG_E_INVAL;
		return RWJPEG_E_FALLBACK;
	}

	status = rwjpeg_open_common(&dev, fd, NULL, NULL);
	if (status != RWJPEG_OK) {
		(void)close(fd);
		return status;
	}
	memset(&info, 0, sizeof(info));
	if (dev_ioctl(dev, RWJ_INFO, &info) < 0 || info.abi_version != RWJ_ABI_VERSION) {
		free_dev(dev);
		return RWJPEG_E_FALLBACK;
	}
	*out = dev;
	return RWJPEG_OK;
}

void
rwjpeg_close(rwjpeg *dev)
{
	free_dev(dev);
}

int
rwjpeg_info(rwjpeg *dev, struct rwj_info *info)
{
	if (dev == NULL || info == NULL)
		return RWJPEG_E_INVAL;
	memset(info, 0, sizeof(*info));
	if (dev_ioctl(dev, RWJ_INFO, info) < 0) {
		memset(info, 0, sizeof(*info));
		return RWJPEG_E_FALLBACK;
	}
	return RWJPEG_OK;
}

int
rwjpeg_present(void)
{
	rwjpeg *dev = NULL;
	struct rwj_info info;

	if (rwjpeg_open(&dev, NULL) != RWJPEG_OK)
		return 0;
	if (rwjpeg_info(dev, &info) != RWJPEG_OK) {
		rwjpeg_close(dev);
		return 0;
	}
	rwjpeg_close(dev);
	return info.abi_version == RWJ_ABI_VERSION && (info.caps & 1u) != 0;
}

/* ------------------------------------------------------------------ encode */

static int
encode_picture(rwjpeg *dev, uint32_t format, const void *y, const void *c,
               uint32_t y_stride, uint32_t c_stride, uint32_t w, uint32_t h, unsigned quality,
               void *out, size_t cap, size_t *bytes, uint32_t *hw_us)
{
	struct rwj_encode req;
	size_t need, got;

	if (bytes == NULL)
		return RWJPEG_E_INVAL;
	*bytes = 0;
	if (hw_us != NULL)
		*hw_us = 0;
	if (dev == NULL || y == NULL || c == NULL || out == NULL)
		return RWJPEG_E_INVAL;
	if (!size_ok(w, h))
		return RWJPEG_E_INVAL;
	if (quality < 1 || quality > 100)
		return RWJPEG_E_INVAL;
	if (y_stride < w || c_stride < w)
		return RWJPEG_E_INVAL;

	need = RWJ_MIN_CAP(w, h);
	if (cap < need) {
		*bytes = need;
		return RWJPEG_E_NOSPC;
	}
#if SIZE_MAX > 0xffffffffu
	if (cap > 0xffffffffu)
		cap = 0xffffffffu;
#endif

	memset(&req, 0, sizeof(req));
	req.width = w;
	req.height = h;
	req.format = format;
	req.quality = quality;
	req.stride = y_stride;
	req.chroma_stride = c_stride;
	req.restart_interval = 0;
	req.flags = 0;
	req.src_y = (uint64_t)(uintptr_t)y;
	req.src_c = (uint64_t)(uintptr_t)c;
	req.dst = (uint64_t)(uintptr_t)out;
	req.dst_capacity = (uint32_t)cap;

	if (dev_ioctl(dev, RWJ_ENCODE, &req) < 0) {
		int e = errno;

		dev->last_errno = e;
		dev->last_status = req.out_status;
		if (e == EINVAL)
			return RWJPEG_E_INVAL;
		if (e == ENOSPC) {
			*bytes = need;
			return RWJPEG_E_NOSPC;
		}
		if (e == ENOMEM)
			return RWJPEG_E_NOMEM;
		return RWJPEG_E_FALLBACK;
	}
	dev->last_status = req.out_status;
	dev->last_errno = 0;
	if (req.out_status != 0)
		return RWJPEG_E_FALLBACK;
	if (req.out_bytes < 4 || req.out_bytes > cap)
		return RWJPEG_E_FALLBACK;

	/* The kernel cuts at EOI (redwood_jpeg.h); cut again at the last FF D9 so a kernel that reports the rounded-up size
	 * still gives the caller a complete, exact file.  No EOI at all means the block did not produce a JPEG. */
	got = rwjpeg_eoi_size(out, req.out_bytes);
	if (got == 0)
		return RWJPEG_E_FALLBACK;
	*bytes = got;
	if (hw_us != NULL)
		*hw_us = req.out_hw_us;
	return RWJPEG_OK;
}

int
rwjpeg_encode_nv12(rwjpeg *dev, const void *y, const void *c,
                   uint32_t y_stride, uint32_t c_stride,
                   uint32_t w, uint32_t h, unsigned quality,
                   void *out, size_t cap, size_t *bytes, uint32_t *hw_us)
{
	return encode_picture(dev, RWJ_FMT_NV12, y, c, y_stride, c_stride, w, h, quality, out, cap, bytes, hw_us);
}

int
rwjpeg_encode_nv21(rwjpeg *dev, const void *y, const void *c,
                   uint32_t y_stride, uint32_t c_stride,
                   uint32_t w, uint32_t h, unsigned quality,
                   void *out, size_t cap, size_t *bytes, uint32_t *hw_us)
{
	return encode_picture(dev, RWJ_FMT_NV21, y, c, y_stride, c_stride, w, h, quality, out, cap, bytes, hw_us);
}

int
rwjpeg_bgra_to_nv12(const void *bgra, uint32_t stride, uint32_t w, uint32_t h,
                    void *nv12, uint32_t nv12_stride)
{
	const uint8_t *src = (const uint8_t *)bgra;
	uint8_t *dst = (uint8_t *)nv12;
	uint32_t x, y, cw;

	if (src == NULL || dst == NULL)
		return RWJPEG_E_INVAL;
	if (!size_ok(w, h))
		return RWJPEG_E_INVAL;
	if (stride < w * 4u || nv12_stride < w || (nv12_stride & 1u) != 0)
		return RWJPEG_E_INVAL;

	for (y = 0; y < h; y++) {
		const uint8_t *row = src + (size_t)y * stride;
		uint8_t *luma = dst + (size_t)y * nv12_stride;

		for (x = 0; x < w; x++) {
			const uint8_t *px = row + (size_t)x * 4u;
			int b = px[0], g = px[1], r = px[2];
			int l = (19595 * r + 38470 * g + 7471 * b + 32768) >> 16;

			luma[x] = clamp_u8(l);
		}
	}

	cw = w / 2;
	for (y = 0; y < h / 2; y++) {
		const uint8_t *r0 = src + (size_t)(2u * y) * stride;
		const uint8_t *r1 = r0 + stride;
		uint8_t *chroma = dst + (size_t)h * nv12_stride + (size_t)y * nv12_stride;

		for (x = 0; x < cw; x++) {
			int cb = 0, cr = 0;
			unsigned k;

			for (k = 0; k < 4; k++) {
				const uint8_t *px = ((k & 2u) ? r1 : r0) + (size_t)(2u * x + (k & 1u)) * 4u;
				int b = px[0], g = px[1], r = px[2];
				int cbb = ((-11059 * r - 21709 * g + 32768 * b + 32768) >> 16) + 128;
				int crr = ((32768 * r - 27439 * g - 5329 * b + 32768) >> 16) + 128;

				cb += clamp_u8(cbb);
				cr += clamp_u8(crr);
			}
			chroma[2u * x] = (uint8_t)((cb + 2) >> 2);
			chroma[2u * x + 1u] = (uint8_t)((cr + 2) >> 2);
		}
	}
	return RWJPEG_OK;
}

int
rwjpeg_encode_bgra(rwjpeg *dev, const void *bgra, uint32_t stride,
                   uint32_t w, uint32_t h, unsigned quality,
                   void *out, size_t cap, size_t *bytes, uint32_t *hw_us)
{
	size_t need;
	int status;

	if (bytes == NULL)
		return RWJPEG_E_INVAL;
	*bytes = 0;
	if (hw_us != NULL)
		*hw_us = 0;
	if (dev == NULL || bgra == NULL || out == NULL)
		return RWJPEG_E_INVAL;
	if (!size_ok(w, h) || quality < 1 || quality > 100)
		return RWJPEG_E_INVAL;
	if (stride < w * 4u)
		return RWJPEG_E_INVAL;

	need = (size_t)w * h + (size_t)w * (h / 2u);
	if (dev->scratch_cap < need) {
		uint8_t *grown = (uint8_t *)realloc(dev->scratch, need);

		if (grown == NULL)
			return RWJPEG_E_NOMEM;
		dev->scratch = grown;
		dev->scratch_cap = need;
	}
	status = rwjpeg_bgra_to_nv12(bgra, stride, w, h, dev->scratch, w);
	if (status != RWJPEG_OK)
		return status;
	return rwjpeg_encode_nv12(dev, dev->scratch, dev->scratch + (size_t)w * h, w, w, w, h, quality,
	                          out, cap, bytes, hw_us);
}

/* ------------------------------------------------------------------ marker surgery */

/* Walk the segment chain from SOI to SOS.  Returns the offset of SOS on success, 0 when the chain is broken/not a JPEG, and
 * stores the end of the first segment in *first_end (2 when the first marker is not a length-carrying segment). */
static size_t
jpeg_find_sos(const uint8_t *p, size_t bytes, size_t *first_end)
{
	size_t pos = 2;
	int first = 1;

	if (first_end != NULL)
		*first_end = 2;
	if (bytes < 4 || p[0] != 0xff || p[1] != 0xd8)
		return 0;
	while (pos + 4 <= bytes) {
		uint8_t marker;
		size_t len;

		if (p[pos] != 0xff)
			return 0;
		marker = p[pos + 1];
		if (marker == 0xff) {           /* fill byte */
			pos++;
			continue;
		}
		if (marker == 0xd9)             /* EOI before SOS: malformed */
			return 0;
		if (marker == 0xda) {           /* SOS: found */
			return pos;
		}
		if (marker < 0xc0)              /* no standalone markers before SOS in a baseline file */
			return 0;
		len = ((size_t)p[pos + 2] << 8) | p[pos + 3];
		if (len < 2 || pos + 2 + len > bytes)
			return 0;
		if (first) {
			if (first_end != NULL)
				*first_end = pos + 2 + len;
			first = 0;
		}
		pos += 2 + len;
	}
	return 0;
}

int
rwjpeg_insert_segments(void *jpeg, size_t bytes, size_t cap,
                       const struct rwjpeg_app_segment *segments, size_t n, size_t *out_bytes)
{
	uint8_t *p = (uint8_t *)jpeg;
	uint8_t *block = NULL;
	size_t insertion = 0, insert_at = 2, tail_start = 2, dropped = 0, new_size, pos, i;
	int have_exif = 0;

	if (out_bytes == NULL)
		return RWJPEG_E_INVAL;
	*out_bytes = 0;
	if (p == NULL || (n > 0 && segments == NULL))
		return RWJPEG_E_INVAL;
	if (bytes < 4 || cap < bytes)
		return RWJPEG_E_INVAL;
	if (p[0] != 0xff || p[1] != 0xd8)
		return RWJPEG_E_INVAL;
	if (rwjpeg_eoi_size(p, bytes) == 0)
		return RWJPEG_E_JPEG;
	if (jpeg_find_sos(p, bytes, NULL) == 0)
		return RWJPEG_E_JPEG;

	for (i = 0; i < n; i++) {
		if (segments[i].marker < 0xe0 || segments[i].marker > 0xef)
			return RWJPEG_E_INVAL;
		if (segments[i].size > RWJ_APP_MAX)
			return RWJPEG_E_INVAL;
		if (segments[i].size > 0 && segments[i].data == NULL)
			return RWJPEG_E_INVAL;
		if (segments[i].marker == 0xe1 && segments[i].size >= 6 && segments[i].data != NULL &&
		    memcmp(segments[i].data, "Exif\0\0", 6) == 0)
			have_exif = 1;
		insertion += 4u + segments[i].size;
	}

	/* Where the hardware's first segment ends: drop it only when it really is APP0 and an EXIF APP1 is being added. */
	{
		size_t first_end = 2;

		if (jpeg_find_sos(p, bytes, &first_end) == 0 || first_end > bytes)
			return RWJPEG_E_JPEG;
		if (have_exif && first_end >= 4 && p[2] == 0xff && p[3] == 0xe0) {
			dropped = first_end - 2;        /* the hardware APP0 gives way to the EXIF APP1 */
			insert_at = 2;
			tail_start = first_end;
		} else if (first_end >= 4 && p[2] == 0xff && p[3] == 0xe0) {
			insert_at = first_end;          /* keep JFIF first, put APPn after it */
			tail_start = first_end;
		} else {
			insert_at = 2;
			tail_start = 2;
		}
	}

	new_size = bytes - dropped + insertion;
	if (new_size > cap) {
		*out_bytes = new_size;
		return RWJPEG_E_NOSPC;
	}

	if (insertion > 0) {
		block = (uint8_t *)malloc(insertion);
		if (block == NULL)
			return RWJPEG_E_NOMEM;
		pos = 0;
		for (i = 0; i < n; i++) {
			size_t len = segments[i].size;

			block[pos++] = 0xff;
			block[pos++] = segments[i].marker;
			block[pos++] = (uint8_t)((len + 2u) >> 8);
			block[pos++] = (uint8_t)((len + 2u) & 0xffu);
			if (len > 0)
				memcpy(block + pos, segments[i].data, len);
			pos += len;
		}
	}

	/* Move the tail first, then lay the (already copied) segments down at the insertion point. */
	memmove(p + insert_at + insertion, p + tail_start, bytes - tail_start);
	if (insertion > 0)
		memcpy(p + insert_at, block, insertion);
	free(block);
	*out_bytes = new_size;
	return RWJPEG_OK;
}
