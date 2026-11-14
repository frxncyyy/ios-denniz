/* SPDX-License-Identifier: MIT */
/*
 * t_rwjpeg.c -- host tests of jpeg/rwjpeg.c against fake_dev.c (cjpeg-backed /dev/redwood-jpeg).
 *
 * Expected values are literals from the specification (include/redwood_jpeg.h, T.81/JFIF marker layout, the integer BGRA
 * formulas printed in rwjpeg.h) or come from an independent parser written here.  Nothing is taken from rwjpeg.c's own
 * helpers except where the test is about their contract.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fake_dev.h"
#include "rwjpeg.h"

static int checks, fails;

#define CHECK(x) do { \
	checks++; \
	if (!(x)) { \
		fails++; \
		printf("FAIL t_rwjpeg.c:%d: %s\n", __LINE__, #x); \
	} \
} while (0)

static void
save(const char *name, const void *data, size_t size)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", fake_scratch(), name);
	f = fopen(path, "wb");
	if (f != NULL) {
		fwrite(data, 1, size, f);
		fclose(f);
	}
}

/* Independent marker walk: SOF dimensions/parts/sampling, DQT/DHT presence.  -1 when malformed. */
static int
parse_jpeg(const uint8_t *p, size_t n, unsigned *w, unsigned *h, unsigned *comp, unsigned *samp,
           int *dqt, int *dht, size_t *sof_at)
{
	size_t pos = 2;

	*w = *h = *comp = *samp = 0;
	*dqt = *dht = 0;
	*sof_at = 0;
	if (n < 4 || p[0] != 0xff || p[1] != 0xd8)
		return -1;
	for (;;) {
		uint8_t marker;
		size_t len;

		if (pos + 4 > n || p[pos] != 0xff)
			return -1;
		marker = p[pos + 1];
		if (marker == 0xff) {
			pos++;
			continue;
		}
		if (marker == 0xda)
			return 0;
		len = ((size_t)p[pos + 2] << 8) | p[pos + 3];
		if (len < 2 || pos + 2 + len > n)
			return -1;
		if (marker == 0xdb)
			*dqt = 1;
		else if (marker == 0xc4)
			*dht = 1;
		else if (marker == 0xc0) {
			if (len < 8)
				return -1;
			*sof_at = pos;
			*h = ((unsigned)p[pos + 5] << 8) | p[pos + 6];
			*w = ((unsigned)p[pos + 7] << 8) | p[pos + 8];
			*comp = p[pos + 9];
			*samp = len >= 9 ? p[pos + 11] : 0;
		}
		pos += 2 + len;
	}
}

static void
fill_nv12(uint8_t *y, uint8_t *c, uint32_t w, uint32_t h, uint32_t stride)
{
	uint32_t x, yy;

	for (yy = 0; yy < h; yy++)
		for (x = 0; x < w; x++)
			y[(size_t)yy * stride + x] = (uint8_t)((x * 3u + yy * 5u) & 0xffu);
	for (yy = 0; yy < h / 2u; yy++)
		for (x = 0; x < w / 2u; x++) {
			c[(size_t)yy * stride + 2u * x] = (uint8_t)(64u + yy) & 0xffu;
			c[(size_t)yy * stride + 2u * x + 1u] = (uint8_t)(192u + x) & 0xffu;
		}
}

/* End of the first segment after SOI (2 when the first marker carries no length). */
static size_t
first_segment_end(const uint8_t *p, size_t n)
{
	size_t len;

	if (n < 6 || p[2] != 0xff || p[3] == 0xda)
		return 2;
	len = ((size_t)p[4] << 8) | p[5];
	if (len < 2 || 4 + len > n)
		return 2;
	return 4 + len;
}

int
main(void)
{
	rwjpeg *dev = NULL;
	struct rwj_info info;
	uint8_t *y = NULL, *c = NULL, *out = NULL, *out2 = NULL, *orig = NULL;
	size_t cap, bytes = 0, bytes2 = 0, orig_size = 0, need;
	uint32_t hw_us = 0;
	unsigned w, h, comp, samp;
	int dqt, dht;
	size_t sof_at;
	int i;

	/* ---- status names and the quality table -- literals from redwood_jpeg.h ---- */
	CHECK(strcmp(rwjpeg_status_name(RWJPEG_OK), "ok") == 0);
	CHECK(strcmp(rwjpeg_status_name(RWJPEG_E_FALLBACK), "fallback") == 0);
	{
		static const struct { unsigned q, code; } tab[] = {
			{ 97, 0x10 }, { 98, 0x10 }, { 100, 0x10 }, { 96, 3 }, { 95, 3 }, { 94, 7 }, { 90, 7 },
			{ 89, 2 }, { 85, 2 }, { 84, 6 }, { 78, 6 }, { 77, 0x0f }, { 72, 0x0f }, { 71, 1 },
			{ 60, 1 }, { 59, 0x0a }, { 52, 0x0a }, { 51, 0x0e }, { 44, 0x0e }, { 43, 0 }, { 38, 0 },
			{ 37, 4 }, { 1, 4 }, { 0, 4 }, { 101, 0x10 },
		};

		for (i = 0; i < (int)(sizeof(tab) / sizeof(tab[0])); i++)
			CHECK(rwjpeg_quality_code(tab[i].q) == tab[i].code);
	}

	/* ---- the EOI cut ---- */
	{
		static const uint8_t a[] = { 0xff, 0xd8, 0x00, 0x11, 0xff, 0xd9 };
		static const uint8_t b[] = { 0xff, 0xd8, 0x12, 0x34, 0xff, 0xd9, 0xa5, 0xa5, 0xa5 };
		static const uint8_t d[] = { 0xff, 0xd8, 0x12, 0x34 };
		static const uint8_t e[] = { 0xff, 0xd8, 0xff, 0x00, 0xff, 0xd9 };

		CHECK(rwjpeg_eoi_size(a, sizeof(a)) == sizeof(a));
		CHECK(rwjpeg_eoi_size(b, sizeof(b)) == 6);
		CHECK(rwjpeg_eoi_size(d, sizeof(d)) == 0);
		CHECK(rwjpeg_eoi_size(e, sizeof(e)) == sizeof(e));
		CHECK(rwjpeg_eoi_size(NULL, 10) == 0);
		CHECK(rwjpeg_eoi_size(a, 1) == 0);
	}

	/* ---- open / info and the no-device path ---- */
	CHECK(rwjpeg_open_ioctl(NULL, fake_ioctl, &n2fake_dev) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_open_ioctl(&dev, NULL, &n2fake_dev) == RWJPEG_E_INVAL);
	CHECK(dev == NULL);
	CHECK(rwjpeg_open_ioctl(&dev, fake_ioctl, &n2fake_dev) == RWJPEG_OK);
	CHECK(dev != NULL);
	CHECK(rwjpeg_info(dev, &info) == RWJPEG_OK);
	CHECK(info.abi_version == RWJ_ABI_VERSION && info.max_pixels == RWJ_MAX_PIXELS && info.caps == 1u);
	CHECK(rwjpeg_info(NULL, &info) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_info(dev, NULL) == RWJPEG_E_INVAL);
	n2fake_dev.fail_info = 1;
	memset(&info, 0x5a, sizeof(info));
	CHECK(rwjpeg_info(dev, &info) == RWJPEG_E_FALLBACK);
	{
		const uint8_t *q = (const uint8_t *)&info;

		for (i = 0; i < (int)sizeof(info); i++)
			CHECK(q[i] == 0);
	}
	n2fake_dev.fail_info = 0;
	{
		rwjpeg *none = (rwjpeg *)0x1;

		CHECK(rwjpeg_open(&none, "/nonexistent-n2hw-j02/jpeg") == RWJPEG_E_FALLBACK);
		CHECK(none == NULL);
		CHECK(rwjpeg_open(&none, "/") == RWJPEG_E_INVAL);
		CHECK(none == NULL);
		CHECK(rwjpeg_open(&none, NULL) == RWJPEG_E_FALLBACK);
		CHECK(none == NULL);
	}
	CHECK(rwjpeg_present() == 0);

	/* ---- one 640x480 NV12 encode, checked against the request and by parsing the JPEG ---- */
	w = 640;
	h = 480;
	cap = (size_t)w * h / 2u + 4096u;
	need = cap;
	y = (uint8_t *)malloc((size_t)w * h);
	c = (uint8_t *)malloc((size_t)w * h / 2u);
	out = (uint8_t *)malloc(cap + 64);
	out2 = (uint8_t *)malloc(cap + 64);
	CHECK(y != NULL && c != NULL && out != NULL && out2 != NULL);
	fill_nv12(y, c, w, h, w);
	memset(out, 0, cap + 64);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, w, h, 90, out, cap, &bytes, &hw_us) == RWJPEG_OK);
	CHECK(bytes > 1000 && bytes <= cap);
	CHECK(out[0] == 0xff && out[1] == 0xd8);
	CHECK(out[bytes - 2] == 0xff && out[bytes - 1] == 0xd9);
	CHECK(parse_jpeg(out, bytes, &w, &h, &comp, &samp, &dqt, &dht, &sof_at) == 0);
	CHECK(w == 640 && h == 480 && comp == 3 && samp == 0x22);
	CHECK(dqt && dht);
	CHECK(rwjpeg_eoi_size(out, bytes) == bytes);
	CHECK(n2fake_dev.have_last == 1);
	CHECK(n2fake_dev.last.width == 640 && n2fake_dev.last.height == 480);
	CHECK(n2fake_dev.last.format == RWJ_FMT_NV12);
	CHECK(n2fake_dev.last.quality == 90);
	CHECK(n2fake_dev.last.stride == 640 && n2fake_dev.last.chroma_stride == 640);
	CHECK(n2fake_dev.last.src_y == (uint64_t)(uintptr_t)y && n2fake_dev.last.src_c == (uint64_t)(uintptr_t)c);
	CHECK(n2fake_dev.last.dst_capacity == cap);
	CHECK(hw_us == n2fake_dev.last.out_hw_us && hw_us > 0);
	orig_size = bytes;
	orig = (uint8_t *)malloc(orig_size);
	CHECK(orig != NULL);
	memcpy(orig, out, orig_size);
	save("enc640.jpg", out, bytes);

	/* ---- padding after EOI is cut, and the reported hardware time is the device's ---- */
	n2fake_dev.pad = 37;
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, out2, cap, &bytes2, &hw_us) == RWJPEG_OK);
	n2fake_dev.pad = 0;
	CHECK(bytes2 == orig_size);
	CHECK(memcmp(out, out2, orig_size) == 0);
	CHECK(n2fake_dev.last.out_bytes == orig_size + 37);

	/* ---- buffer sizes: the library's minimum, the device's ENOSPC, and the no-EOI failure ---- */
	bytes = 12345;
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, out, need - 1, &bytes, NULL) == RWJPEG_E_NOSPC);
	CHECK(bytes == need);
	n2fake_dev.fail_enospc = 1;
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, out, need, &bytes, NULL) == RWJPEG_E_NOSPC);
	CHECK(bytes == need);
	n2fake_dev.fail_enospc = 0;
	n2fake_dev.fail_eio = 1;
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_FALLBACK);
	n2fake_dev.fail_eio = 0;
	n2fake_dev.fail_noeoi = 1;
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_FALLBACK);
	n2fake_dev.fail_noeoi = 0;
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_OK);

	/* ---- caller mistakes are INVAL, never a fallback ---- */
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 641, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 8, 8, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 4096, 3074, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 0, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 101, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, 639, w, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, 639, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, NULL, c, w, w, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, NULL, w, w, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, NULL, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(dev, y, c, w, w, 640, 480, 90, out, cap, NULL, NULL) == RWJPEG_E_INVAL);
	CHECK(rwjpeg_encode_nv12(NULL, y, c, w, w, 640, 480, 90, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);

	/* ---- NV21 is a different format word on the wire ---- */
	CHECK(rwjpeg_encode_nv21(dev, y, c, w, w, 640, 480, 85, out, cap, &bytes, NULL) == RWJPEG_OK);
	CHECK(n2fake_dev.last.format == RWJ_FMT_NV21);

	/* ---- BGRA -> NV12: the four full-range BT.601 primaries, and 2x2 chroma averaging ---- */
	{
		/* 16x16, 2x2 blocks of red, green / blue, white in BGRA byte order. */
		uint8_t bgra[16 * 16 * 4];
		uint8_t nv12[16 * 16 * 3 / 2];
		uint8_t wide[(16 * 4 + 8) * 16];
		uint8_t nv12_wide[16 * 16 * 3 / 2 + 8];
		static const uint8_t colors[4][4] = {
			{ 0, 0, 255, 255 },     /* red */
			{ 0, 255, 0, 255 },     /* green */
			{ 255, 0, 0, 255 },     /* blue */
			{ 255, 255, 255, 255 }  /* white */
		};
		static const uint8_t luma[4] = { 76, 150, 29, 255 };
		uint32_t x, yy;

		for (yy = 0; yy < 16; yy++)
			for (x = 0; x < 16; x++) {
				const uint8_t *col = colors[(yy & 1u) * 2u + (x & 1u)];
				uint8_t *px = bgra + ((size_t)yy * 16u + x) * 4u;

				px[0] = col[0]; px[1] = col[1]; px[2] = col[2]; px[3] = col[3];
			}
		memset(nv12, 0xee, sizeof(nv12));
		CHECK(rwjpeg_bgra_to_nv12(bgra, 16 * 4, 16, 16, nv12, 16) == RWJPEG_OK);
		for (yy = 0; yy < 16; yy++)
			for (x = 0; x < 16; x++)
				CHECK(nv12[(size_t)yy * 16 + x] == luma[(yy & 1u) * 2u + (x & 1u)]);
		for (yy = 0; yy < 8; yy++)
			for (x = 0; x < 8; x++) {
				CHECK(nv12[256 + (size_t)yy * 16 + 2u * x] == 128);
				CHECK(nv12[256 + (size_t)yy * 16 + 2u * x + 1u] == 128);
			}

		/* A wider stride with sentinel bytes must give the same picture (no read past width). */
		memset(wide, 0xaa, sizeof(wide));
		for (yy = 0; yy < 16; yy++)
			memcpy(wide + (size_t)yy * (16 * 4 + 8), bgra + (size_t)yy * 64, 64);
		memset(nv12_wide, 0xdd, sizeof(nv12_wide));
		CHECK(rwjpeg_bgra_to_nv12(wide, 16 * 4 + 8, 16, 16, nv12_wide, 16) == RWJPEG_OK);
		CHECK(memcmp(nv12, nv12_wide, sizeof(nv12)) == 0);
		for (x = 0; x < 8; x++)
			CHECK(nv12_wide[sizeof(nv12) + x] == 0xdd);

		CHECK(rwjpeg_bgra_to_nv12(bgra, 16 * 4, 17, 16, nv12, 17) == RWJPEG_E_INVAL);
		CHECK(rwjpeg_bgra_to_nv12(bgra, 16 * 4 - 1, 16, 16, nv12, 16) == RWJPEG_E_INVAL);
		CHECK(rwjpeg_bgra_to_nv12(bgra, 16 * 4, 16, 16, nv12, 15) == RWJPEG_E_INVAL);
		CHECK(rwjpeg_bgra_to_nv12(NULL, 64, 16, 16, nv12, 16) == RWJPEG_E_INVAL);
		CHECK(rwjpeg_bgra_to_nv12(bgra, 64, 16, 16, NULL, 16) == RWJPEG_E_INVAL);

		/* And through the device: the request is NV12 at stride 16. */
		CHECK(rwjpeg_encode_bgra(dev, bgra, 64, 16, 16, 95, out, cap, &bytes, &hw_us) == RWJPEG_OK);
		CHECK(n2fake_dev.last.format == RWJ_FMT_NV12);
		CHECK(n2fake_dev.last.width == 16 && n2fake_dev.last.height == 16);
		CHECK(n2fake_dev.last.stride == 16 && n2fake_dev.last.chroma_stride == 16);
		CHECK(n2fake_dev.last.quality == 95);
		CHECK(parse_jpeg(out, bytes, &w, &h, &comp, &samp, &dqt, &dht, &sof_at) == 0);
		CHECK(w == 16 && h == 16 && comp == 3 && samp == 0x22);
		save("enc16_bgra.jpg", out, bytes);
		CHECK(rwjpeg_encode_bgra(dev, NULL, 64, 16, 16, 95, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
		CHECK(rwjpeg_encode_bgra(dev, bgra, 63, 16, 16, 95, out, cap, &bytes, NULL) == RWJPEG_E_INVAL);
	}

	/* ---- marker surgery ---- */
	{
		static const uint8_t exif[16] = { 'E', 'x', 'i', 'f', 0, 0, 'J', '0', '2', '0', '1', '2', '3', '4', '5', '6' };
		static const uint8_t xmp[] = { 'h', 't', 't', 'p', ':', '/', '/', 'n', 's', '.', 'a', 'd', 'o', 'b', 'e', '.',
			'c', 'o', 'm', '/', 'x', 'a', 'p', '/', '1', '.', '0', '/', 0 };
		static const uint8_t icc[8] = { 'I', 'C', 'C', '_', 'P', 'R', 'O', 0 };
		struct rwjpeg_app_segment segs[2];
		size_t app0_end = first_segment_end(orig, orig_size);
		size_t app0_len = app0_end - 2;
		size_t buf_cap = orig_size + 4096;
		uint8_t *buf = (uint8_t *)malloc(buf_cap);
		uint8_t *buf2 = (uint8_t *)malloc(buf_cap);
		size_t new_size = 0, new_size2 = 0;

		CHECK(buf != NULL && buf2 != NULL);
		CHECK(app0_end > 2 && app0_len >= 16);

		memcpy(buf, orig, orig_size);
		segs[0].marker = 0xe1;
		memset(segs[0].reserved, 0, sizeof(segs[0].reserved));
		segs[0].size = (uint32_t)sizeof(exif);
		segs[0].data = exif;
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, segs, 1, &new_size) == RWJPEG_OK);
		CHECK(new_size == orig_size - app0_len + 4 + sizeof(exif));
		CHECK(buf[0] == 0xff && buf[1] == 0xd8);
		CHECK(buf[2] == 0xff && buf[3] == 0xe1);
		CHECK(buf[4] == (uint8_t)(((sizeof(exif) + 2) >> 8)) && buf[5] == (uint8_t)((sizeof(exif) + 2) & 0xff));
		CHECK(memcmp(buf + 6, exif, sizeof(exif)) == 0);
		/* APP0 is gone: the hardware's next segment (DQT) starts right after the EXIF segment. */
		CHECK(buf[6 + sizeof(exif)] == 0xff);
		CHECK(buf[6 + sizeof(exif) + 1] == orig[app0_end + 1]);
		CHECK(memcmp(buf + 6 + sizeof(exif), orig + app0_end, orig_size - app0_end) == 0);
		CHECK(buf[new_size - 2] == 0xff && buf[new_size - 1] == 0xd9);
		CHECK(parse_jpeg(buf, new_size, &w, &h, &comp, &samp, &dqt, &dht, &sof_at) == 0);
		CHECK(w == 640 && h == 480);
		save("enc640_exif.jpg", buf, new_size);

		/* Exact capacity: one byte short is reported, never written. */
		memcpy(buf2, orig, orig_size);
		CHECK(rwjpeg_insert_segments(buf2, orig_size, new_size - 1, segs, 1, &new_size2) == RWJPEG_E_NOSPC);
		CHECK(new_size2 == new_size);
		CHECK(memcmp(buf2, orig, orig_size) == 0);
		CHECK(rwjpeg_insert_segments(buf2, orig_size, new_size, segs, 1, &new_size2) == RWJPEG_OK);
		CHECK(new_size2 == new_size && memcmp(buf2, buf, new_size) == 0);

		/* Without EXIF the APP0 stays first, and the XMP APP1 + ICC APP2 follow it in order. */
		memcpy(buf, orig, orig_size);
		segs[0].marker = 0xe1;
		segs[0].size = (uint32_t)sizeof(xmp);
		segs[0].data = xmp;
		segs[1].marker = 0xe2;
		segs[1].size = (uint32_t)sizeof(icc);
		segs[1].data = icc;
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, segs, 2, &new_size) == RWJPEG_OK);
		CHECK(new_size == orig_size + 4 + sizeof(xmp) + 4 + sizeof(icc));
		CHECK(memcmp(buf, orig, app0_end) == 0);                 /* APP0 untouched, still first */
		CHECK(buf[app0_end] == 0xff && buf[app0_end + 1] == 0xe1);
		CHECK(memcmp(buf + app0_end + 4, xmp, sizeof(xmp)) == 0);
		CHECK(buf[app0_end + 4 + sizeof(xmp)] == 0xff &&
		      buf[app0_end + 4 + sizeof(xmp) + 1] == 0xe2);
		CHECK(memcmp(buf + app0_end + 4 + sizeof(xmp) + 4, icc, sizeof(icc)) == 0);
		CHECK(memcmp(buf + app0_end + 4 + sizeof(xmp) + 4 + sizeof(icc), orig + app0_end,
		             orig_size - app0_end) == 0);
		CHECK(parse_jpeg(buf, new_size, &w, &h, &comp, &samp, &dqt, &dht, &sof_at) == 0);
		CHECK(w == 640 && h == 480);

		/* No segment at all is the identity. */
		memcpy(buf, orig, orig_size);
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, NULL, 0, &new_size) == RWJPEG_OK);
		CHECK(new_size == orig_size && memcmp(buf, orig, orig_size) == 0);

		/* Broken inputs are refused and leave the buffer alone. */
		memcpy(buf, orig, orig_size);
		segs[0].marker = 0xe1;
		segs[0].size = (uint32_t)sizeof(exif);
		segs[0].data = exif;
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, NULL, 1, &new_size) == RWJPEG_E_INVAL);
		segs[0].marker = 0xd8;
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, segs, 1, &new_size) == RWJPEG_E_INVAL);
		segs[0].marker = 0xe1;
		segs[0].size = 65534;
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, segs, 1, &new_size) == RWJPEG_E_INVAL);
		segs[0].size = 10;
		segs[0].data = NULL;
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, segs, 1, &new_size) == RWJPEG_E_INVAL);
		segs[0].data = exif;
		CHECK(rwjpeg_insert_segments(buf, 3, buf_cap, segs, 1, &new_size) == RWJPEG_E_INVAL);
		CHECK(rwjpeg_insert_segments(buf, orig_size, orig_size - 1, segs, 1, &new_size) == RWJPEG_E_INVAL);
		memcpy(buf, orig, orig_size);
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, segs, 1, NULL) == RWJPEG_E_INVAL);
		buf[0] = 0;
		CHECK(rwjpeg_insert_segments(buf, orig_size, buf_cap, segs, 1, &new_size) == RWJPEG_E_INVAL);
		memcpy(buf, orig, orig_size);
		CHECK(rwjpeg_insert_segments(buf, orig_size - 2, buf_cap, segs, 1, &new_size) == RWJPEG_E_JPEG);
		CHECK(memcmp(buf, orig, orig_size) == 0);

		free(buf2);
		free(buf);
	}

	/* ---- close ---- */
	rwjpeg_close(dev);
	rwjpeg_close(NULL);

	free(orig);
	free(out2);
	free(out);
	free(c);
	free(y);

	printf("T_RWJPEG %s %d/%d\n", fails == 0 ? "PASS" : "FAIL", checks - fails, checks);
	return fails == 0 ? 0 : 1;
}
