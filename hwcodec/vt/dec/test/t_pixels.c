/* SPDX-License-Identifier: MIT */
/*
 * t_pixels.c -- the MM21/ NV12 / I420 copy against an independently written reference.
 *
 * The test tiles a synthetic NV12 picture into MM21 with its own forward loop (tile raster order, row-major inside a
 * tile; the opposite direction from the code under test), then checks that the copy reproduces the original pixels,
 * with and without a crop and with padded destination strides.  Nothing here reads the implementation.
 */
#include "n2vtd_pixels.h"
#if __has_include("n2_mm21.h")
#include "n2_mm21.h"          /* opus-mm21 verified converter, the differential oracle */
#define HAVE_N2_MM21 1
#else
#define HAVE_N2_MM21 0
#endif
#include "t_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nv12 {
	uint32_t w, h;
	uint8_t *y, *c;
	size_t ys, cs;
};

static struct nv12 nv12_new(uint32_t w, uint32_t h, size_t ys, size_t cs)
{
	struct nv12 p;

	p.w = w;
	p.h = h;
	p.ys = ys;
	p.cs = cs;
	p.y = (uint8_t *)calloc((size_t)ys, h);
	p.c = (uint8_t *)calloc((size_t)cs, (h + 1) / 2);
	return p;
}

static void nv12_free(struct nv12 *p)
{
	free(p->y);
	free(p->c);
	p->y = p->c = NULL;
}

/* Deterministic picture; the chroma plane is interleaved CbCr. */
static void nv12_fill(struct nv12 *p)
{
	uint32_t x, y;

	for (y = 0; y < p->h; y++)
		for (x = 0; x < p->w; x++)
			p->y[(size_t)y * p->ys + x] = (uint8_t)((x * 7u + y * 3u + 17u) & 0xffu);
	for (y = 0; y < (p->h + 1) / 2; y++) {
		for (x = 0; x < (p->w + 1) / 2; x++) {
			p->c[(size_t)y * p->cs + x * 2u] = (uint8_t)((x * 5u + y * 11u + 60u) & 0xffu);
			p->c[(size_t)y * p->cs + x * 2u + 1u] = (uint8_t)((x * 13u + y * 2u + 200u) & 0xffu);
		}
	}
}

/* Forward tiler, written from the layout description independently of n2vtd_pixels.c. */
static uint8_t *mm21_from_nv12(const struct nv12 *p)
{
	size_t ystride = p->w, cstride = ((size_t)p->w + 31u) & ~(size_t)31u;
	uint8_t *out = (uint8_t *)calloc(1, (size_t)ystride * p->h + (size_t)cstride * ((p->h + 1) / 2));
	uint32_t tpr_y = p->w / 16, tpr_c = cstride / 16;
	uint32_t tx, ty, r, c;

	for (ty = 0; ty < p->h / 32; ty++) {
		for (tx = 0; tx < tpr_y; tx++) {
			size_t tile = ((size_t)ty * tpr_y + tx) * 512;
			for (r = 0; r < 32; r++)
				for (c = 0; c < 16; c++)
					out[tile + (size_t)r * 16 + c] =
					    p->y[(size_t)(ty * 32 + r) * p->ys + (tx * 16 + c)];
		}
	}
	for (ty = 0; ty < p->h / 32; ty++) {              /* chroma plane is h/2 rows = h/32 tile rows */
		for (tx = 0; tx < tpr_c; tx++) {
			size_t tile = ((size_t)ty * tpr_c + tx) * 256;   /* 16 bytes x 16 rows */
			for (r = 0; r < 16; r++)
				for (c = 0; c < 16; c++) {
					size_t xb = (size_t)tx * 16u + c;         /* byte column, Cb at even */
					size_t yc = (size_t)ty * 16u + r;
					uint8_t v = (xb < p->cs && yc < (p->h + 1) / 2) ? p->c[yc * p->cs + xb] : 0;

					out[(size_t)ystride * p->h + tile + (size_t)r * 16u + c] = v;
				}
		}
	}
	return out;
}

static int check_region(const struct nv12 *ref, uint32_t cx, uint32_t cy, uint32_t cw, uint32_t ch, uint8_t *dy,
                        size_t dys, uint8_t *dc, size_t dcs)
{
	uint32_t x, y;
	int ok = 1;

	for (y = 0; y < ch; y++)
		for (x = 0; x < cw; x++)
			if (dy[(size_t)y * dys + x] != ref->y[(size_t)(cy + y) * ref->ys + cx + x])
				ok = 0;
	for (y = 0; y < (ch + 1) / 2; y++)
		for (x = 0; x < ((cw + 1) / 2) * 2; x++)
			if (dc[(size_t)y * dcs + x] != ref->c[(size_t)((cy / 2) + y) * ref->cs + (cx / 2) * 2u + x])
				ok = 0;
	return ok;
}

static void run_case(uint32_t w, uint32_t h, uint32_t cx, uint32_t cy, uint32_t cw, uint32_t ch, size_t pad)
{
	struct nv12 ref = nv12_new(w, h, w, w);
	uint8_t *mm21;
	uint8_t *dy, *dc;
	size_t dys = cw + pad, dcs = ((cw + 1u) & ~1u) + pad;
	size_t csrc = ((size_t)w + 31u) & ~(size_t)31u;
	struct n2vtd_src_plane y, c;
	int rc;

	nv12_fill(&ref);
	mm21 = mm21_from_nv12(&ref);
	dy = (uint8_t *)calloc(dys ? dys : 1, ch);
	dc = (uint8_t *)calloc(dcs ? dcs : 1, (ch + 1) / 2);
	y.data = mm21;
	y.stride = w;
	c.data = mm21 + (size_t)w * h;
	c.stride = csrc;
	rc = n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, w, h, cx, cy, cw, ch, dy, dys, dc, dcs);
	CHECK_EQ(rc, 0);
	CHECK(check_region(&ref, cx, cy, cw, ch, dy, dys, dc, dcs));

	/* The same picture presented as linear NV12. */
	{
		uint8_t *dy2 = (uint8_t *)calloc(dys ? dys : 1, ch);
		uint8_t *dc2 = (uint8_t *)calloc(dcs ? dcs : 1, (ch + 1) / 2);
		struct n2vtd_src_plane ly, lc;

		ly.data = ref.y;
		ly.stride = ref.ys;
		lc.data = ref.c;
		lc.stride = ref.cs;
		rc = n2vtd_copy_to_nv12(N2VTD_FMT_NV12, &ly, &lc, w, h, cx, cy, cw, ch, dy2, dys, dc2, dcs);
		CHECK_EQ(rc, 0);
		CHECK(check_region(&ref, cx, cy, cw, ch, dy2, dys, dc2, dcs));
		free(dy2);
		free(dc2);
	}
	/* I420: split the interleaved chroma into U and V and back. */
	{
		uint8_t *u = (uint8_t *)calloc((size_t)(w / 2) * (h / 2) + 1, 1);
		uint8_t *v = (uint8_t *)calloc((size_t)(w / 2) * (h / 2) + 1, 1);
		uint8_t *dy3 = (uint8_t *)calloc(dys ? dys : 1, ch);
		uint8_t *dc3 = (uint8_t *)calloc(dcs ? dcs : 1, (ch + 1) / 2);
		struct n2vtd_src_plane up, vp;
		uint32_t x, yy;

		for (yy = 0; yy < h / 2; yy++)
			for (x = 0; x < w / 2; x++) {
				u[(size_t)yy * (w / 2) + x] = ref.c[(size_t)yy * ref.cs + (size_t)x * 2u];
				v[(size_t)yy * (w / 2) + x] = ref.c[(size_t)yy * ref.cs + (size_t)x * 2u + 1u];
			}
		up.data = u;
		up.stride = w / 2;
		vp.data = v;
		vp.stride = w / 2;
		{
			struct n2vtd_src_plane ly;
			ly.data = ref.y;
			ly.stride = ref.ys;
			rc = n2vtd_copy_i420_to_nv12(&ly, &up, &vp, w, h, cx, cy, cw, ch, dy3, dys, dc3, dcs);
		}
		CHECK_EQ(rc, 0);
		CHECK(check_region(&ref, cx, cy, cw, ch, dy3, dys, dc3, dcs));
		free(u);
		free(v);
		free(dy3);
		free(dc3);
	}
	free(dy);
	free(dc);
	free(mm21);
	nv12_free(&ref);
}


/* Differential oracle: opus-mm21's converter is byte-identical to libyuv's MM21ToNV12 (facts/MM21_LAYOUT.md section 2),
 * so every geometry here is converted by both and the destinations are compared byte for byte. */
#if HAVE_N2_MM21
static void differential_case(uint32_t w, uint32_t h, uint32_t cx, uint32_t cy, uint32_t cw, uint32_t ch, size_t pad,
                              uint32_t seed)
{
	struct nv12 ref = nv12_new(w, h, w, w);
	struct n2vtd_src_plane y, c;
	uint8_t *mm21;
	uint8_t *dy, *dc, *oy, *oc;
	size_t dys = cw + pad, dcs = ((cw + 1u) & ~1u) + pad;
	struct n2_mm21_frame src;
	struct n2_nv12_out dst;
	uint32_t x, yy;
	int rc;

	nv12_fill(&ref);
	for (yy = 0; yy < h; yy++)
		for (x = 0; x < w; x++)
			ref.y[(size_t)yy * w + x] ^= (uint8_t)(seed + x + yy);
	for (yy = 0; yy < (h + 1) / 2; yy++)
		for (x = 0; x < w; x++)
			ref.c[(size_t)yy * w + x] ^= (uint8_t)(seed * 3u + x + yy);
	mm21 = mm21_from_nv12(&ref);
	dy = (uint8_t *)calloc(dys ? dys : 1, ch);
	dc = (uint8_t *)calloc(dcs ? dcs : 1, (ch + 1) / 2);
	oy = (uint8_t *)calloc(dys ? dys : 1, ch);
	oc = (uint8_t *)calloc(dcs ? dcs : 1, (ch + 1) / 2);
	y.data = mm21;
	y.stride = w;
	c.data = mm21 + (size_t)w * h;
	c.stride = ((size_t)w + 31u) & ~(size_t)31u;
	rc = n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, w, h, cx, cy, cw, ch, dy, dys, dc, dcs);
	CHECK_EQ(rc, 0);
	src.y = mm21;
	src.uv = mm21 + (size_t)w * h;
	src.y_stride = w;
	src.uv_stride = (uint32_t)c.stride;
	src.buf_h = h;
	dst.y = oy;
	dst.uv = oc;
	dst.y_pitch = dys;
	dst.uv_pitch = dcs;
	rc = n2_mm21_to_nv12(&src, &dst, cx, cy, cw, ch);
	CHECK_EQ(rc, 0);
	if (rc == 0) {
		CHECK(memcmp(dy, oy, (size_t)dys * ch) == 0);
		CHECK(memcmp(dc, oc, (size_t)dcs * ((ch + 1) / 2)) == 0);
	}
	free(dy);
	free(dc);
	free(oy);
	free(oc);
	free(mm21);
	nv12_free(&ref);
}
#endif /* HAVE_N2_MM21 */

int main(void)
{
	static const uint32_t sizes[][2] = { { 64, 64 }, { 96, 64 }, { 320, 256 }, { 176, 128 } };
	size_t i;

	printf("t_pixels: MM21 -> NV12 against the reference tiler\n");
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		run_case(sizes[i][0], sizes[i][1], 0, 0, sizes[i][0], sizes[i][1], 0);
		/* Padded destination, as a CVPixelBuffer may have. */
		run_case(sizes[i][0], sizes[i][1], 0, 0, sizes[i][0], sizes[i][1], 64);
	}
	/* Crops: even origins and sizes for 4:2:0. */
	run_case(320, 256, 0, 0, 320, 240, 0);
	run_case(320, 256, 16, 32, 160, 128, 0);
	run_case(320, 256, 32, 8, 256, 240, 32);

	/* Differential against opus-mm21 / libyuv: fixed cases, then 40 pseudo-random geometries. */
#if HAVE_N2_MM21
	differential_case(320, 256, 0, 0, 320, 240, 0, 1);
	differential_case(320, 256, 16, 32, 160, 128, 0, 2);
	differential_case(320, 256, 32, 8, 256, 240, 32, 3);
	differential_case(64, 64, 0, 0, 64, 64, 0, 4);
	{
		uint32_t i, seed = 12345;
		for (i = 0; i < 40; i++) {
			uint32_t w, h, cw, ch, cx, cy;
			seed = seed * 1103515245u + 12345u;
			w = 64u + ((seed >> 8) % 17u) * 16u;               /* 64..320, multiple of 16 */
			seed = seed * 1103515245u + 12345u;
			h = 32u + ((seed >> 8) % 8u) * 32u;                /* 32..256, multiple of 32 */
			seed = seed * 1103515245u + 12345u;
			cw = 1u + ((seed >> 8) % w);
			seed = seed * 1103515245u + 12345u;
			ch = 1u + ((seed >> 8) % h);
			cx = ((seed >> 4) % ((w - cw) / 2u + 1u)) * 2u;
			seed = seed * 1103515245u + 12345u;
			cy = ((seed >> 4) % ((h - ch) / 2u + 1u)) * 2u;
			differential_case(w, h, cx, cy, cw, ch, 0, seed);
		}
	}
#else
	printf("t_pixels: opus-mm21 converter absent, differential skipped\n");
#endif

	/* Plane size arithmetic: the vendor's own numbers for 1080p MM21. */
	CHECK_EQ_U64(n2vtd_frame_bytes(N2VTD_FMT_MM21, 1920, 1088), 2088960u + 1044480u);
	CHECK_EQ_U64(n2vtd_frame_bytes(N2VTD_FMT_MM21, 320, 256), 81920u + 40960u);
	CHECK_EQ_U64(n2vtd_frame_bytes(N2VTD_FMT_NV12, 1920, 1088), 2088960u + 1044480u);
	CHECK_EQ_U64(n2vtd_frame_bytes(N2VTD_FMT_I420, 320, 256), 81920u + 20480u * 2u);
	CHECK_EQ_U64(n2vtd_frame_bytes(N2VTD_FMT_MM21, 1920, 1088), n2vtd_frame_bytes(N2VTD_FMT_NV12, 1920, 1088));

	/* Refusals: geometry that a 4:2:0 copy cannot express. */
	{
		struct nv12 ref = nv12_new(64, 64, 64, 64);
		uint8_t *mm21;
		uint8_t dy[64 * 64], dc[64 * 32];
		struct n2vtd_src_plane y, c;

		nv12_fill(&ref);
		mm21 = mm21_from_nv12(&ref);
		y.data = mm21;
		y.stride = 64;
		c.data = mm21 + 64 * 64;
		c.stride = 64;
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 64, 1, 0, 32, 32, dy, 64, dc, 64) < 0);
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 64, 0, 1, 32, 32, dy, 64, dc, 64) < 0);
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 64, 48, 0, 32, 32, dy, 64, dc, 64) < 0);
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 64, 0, 48, 32, 32, dy, 64, dc, 64) < 0);
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 64, 0, 0, 0, 32, dy, 64, dc, 64) < 0);
		y.stride = 60;
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 64, 0, 0, 32, 32, dy, 64, dc, 64) < 0);
		y.stride = 64;
		CHECK(n2vtd_copy_to_nv12(0x12345678u, &y, &c, 64, 64, 0, 0, 32, 32, dy, 64, dc, 64) < 0);
		/* A height that is not a multiple of 32 would need a partial chroma tile row: refused, not guessed. */
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 48, 0, 0, 64, 48, dy, 64, dc, 64) < 0);
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, NULL, &c, 64, 64, 0, 0, 32, 32, dy, 64, dc, 64) < 0);
		CHECK(n2vtd_copy_to_nv12(N2VTD_FMT_MM21, &y, &c, 64, 64, 0, 0, 32, 32, NULL, 64, dc, 64) < 0);
		CHECK_EQ_U64(n2vtd_frame_bytes(N2VTD_FMT_MM21, 65, 64), 0);
		CHECK_EQ_U64(n2vtd_frame_bytes(0xdeadbeefu, 64, 64), 0);
		free(mm21);
		nv12_free(&ref);
	}
	return t_summary("PIXELS");
}
