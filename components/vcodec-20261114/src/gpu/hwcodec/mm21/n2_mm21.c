/* SPDX-License-Identifier: MIT */
/*
 * n2_mm21.c -- MM21 -> NV12 detile (opus-mm21).  See n2_mm21.h and facts/MM21_LAYOUT.md.
 *
 * Inner loop: one destination row = a run of 16-byte pieces, piece k of the row taken from tile column k at
 * src_row + k * tile_bytes (tile_bytes = 16 * tile_h).  The row is split into a head (partial first tile when x is not a
 * multiple of 16), full tiles (NEON: four per iteration, 4 loads + 4 stores of q registers) and a tail (partial last tile).
 * The code is memory-bound: a 1080p frame reads and writes 3,110,400 bytes each.
 *
 * Host testing of the NEON path: with N2_MM21_NEON_EMU defined (tests only), the same NEON code is compiled against
 * n2_mm21_neon_emu.h, a plain-C stand-in for the four intrinsics used, so its indexing and tails run on x86.
 */
#include "n2_mm21.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>

#if defined(N2_MM21_NEON_EMU)
#include "n2_mm21_neon_emu.h"
#define N2_MM21_USE_NEON 1
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#define N2_MM21_USE_NEON 1
#else
#define N2_MM21_USE_NEON 0
#endif

const char *n2_mm21_impl(void)
{
#if defined(N2_MM21_NEON_EMU)
	return "neon-emu";
#elif N2_MM21_USE_NEON
	return "neon";
#else
	return "c";
#endif
}

/* Copy bytes [x, x + w) of one tiled row to dst.  row = plane + (y / th) * stride * th + (y % th) * 16, i.e. the row's
 * 16 bytes inside tile column 0; tb = tile bytes (16 * th). */
static inline void detile_row(const uint8_t *row, size_t tb, uint8_t *dst, uint32_t x, uint32_t w)
{
	uint32_t end = x + w;
	const uint8_t *s = row + (size_t)(x >> 4) * tb;
	uint32_t head = x & 15u;
	if (head) {
		uint32_t n = 16u - head;
		if (n > w) { n = w; }
		memcpy(dst, s + head, n);
		dst += n; x += n; s += tb;
	}
	uint32_t full = (end - x) >> 4;      /* whole 16-byte pieces left (x is now a multiple of 16 or == end) */
#if N2_MM21_USE_NEON
	while (full >= 4u) {
		uint8x16_t a = vld1q_u8(s);
		uint8x16_t b = vld1q_u8(s + tb);
		uint8x16_t c = vld1q_u8(s + 2u * tb);
		uint8x16_t d = vld1q_u8(s + 3u * tb);
		vst1q_u8(dst, a);
		vst1q_u8(dst + 16, b);
		vst1q_u8(dst + 32, c);
		vst1q_u8(dst + 48, d);
		s += 4u * tb; dst += 64; full -= 4u;
	}
	while (full) {
		vst1q_u8(dst, vld1q_u8(s));
		s += tb; dst += 16; full--;
	}
#else
	while (full) {
		memcpy(dst, s, 16);
		s += tb; dst += 16; full--;
	}
#endif
	uint32_t tail = (end - x) & 15u;
	if (tail) { memcpy(dst, s, tail); }
}

/* rows [y, y+h) of a plane whose tile rows start at `plane` */
static void detile_rows(const uint8_t *plane, uint32_t stride, uint32_t th, uint8_t *dst, size_t pitch,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	size_t tb = (size_t)16u * th;
	size_t band = (size_t)stride * th;
	for (uint32_t r = 0; r < h; r++) {
		uint32_t yy = y + r;
		const uint8_t *row = plane + (size_t)(yy / th) * band + (size_t)(yy % th) * 16u;
		detile_row(row, tb, dst + (size_t)r * pitch, x, w);
	}
}

int n2_mm21_detile_plane(const uint8_t *plane, uint32_t stride, uint32_t tile_h,
                         uint8_t *dst, size_t dst_pitch, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	if (!plane || !dst || !w || !h) { return -EINVAL; }
	if (tile_h != N2_MM21_Y_TILE_H && tile_h != N2_MM21_UV_TILE_H) { return -EINVAL; }
	if (!stride || (stride & 15u) || x > stride || w > stride - x || dst_pitch < w) { return -EINVAL; }
	if (h > UINT32_MAX - y) { return -EINVAL; }
	detile_rows(plane, stride, tile_h, dst, dst_pitch, x, y, w, h);
	return 0;
}

/* common validation; cw = chroma bytes per row, ch = chroma rows */
static int check_rect(const struct n2_mm21_frame *s, const struct n2_nv12_out *d, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t *cw, uint32_t *ch)
{
	if (!s || !d || !d->y || !d->uv) { return -EINVAL; }
	if (!s->y_stride || !s->uv_stride || (s->y_stride & 15u) || (s->uv_stride & 15u)) { return -EINVAL; }
	if ((s->buf_h & 31u) || (x & 1u) || (y & 1u) || !w || !h) { return -EINVAL; }
	if (w > 0xffffu * 16u || h > 0xffffu * 32u) { return -EINVAL; }
	*cw = ((w + 1u) >> 1) * 2u;
	*ch = (h + 1u) >> 1;
	if (x > s->y_stride || w > s->y_stride - x) { return -EINVAL; }
	if (x > s->uv_stride || *cw > s->uv_stride - x) { return -EINVAL; }
	if (s->buf_h && (y > s->buf_h || h > s->buf_h - y)) { return -EINVAL; }
	/* an odd h at the bottom of buf_h is impossible (buf_h even), so the chroma rows stay within buf_h / 2 */
	if (d->y_pitch < w || d->uv_pitch < *cw) { return -EINVAL; }
	return 0;
}

int n2_mm21_to_nv12(const struct n2_mm21_frame *src, const struct n2_nv12_out *dst,
                    uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	uint32_t cw, ch;
	int e = check_rect(src, dst, x, y, w, h, &cw, &ch);
	if (e) { return e; }
	if (!src->y || !src->uv) { return -EINVAL; }
	detile_rows(src->y, src->y_stride, N2_MM21_Y_TILE_H, dst->y, dst->y_pitch, x, y, w, h);
	detile_rows(src->uv, src->uv_stride, N2_MM21_UV_TILE_H, dst->uv, dst->uv_pitch, x, y / 2u, cw, ch);
	return 0;
}

/* byte range of one tile row that covers columns [x, x + w): [c0 * tb, c1 * tb) */
static void tile_cols(uint32_t x, uint32_t w, uint32_t *c0, uint32_t *c1)
{
	*c0 = x >> 4;
	*c1 = (x + w + 15u) >> 4;
}

size_t n2_mm21_staging_bytes(const struct n2_mm21_frame *src, uint32_t x, uint32_t w)
{
	if (!src || !w) { return 0; }
	uint32_t c0, c1, d0, d1;
	tile_cols(x, w, &c0, &c1);
	tile_cols(x, ((w + 1u) >> 1) * 2u, &d0, &d1);
	size_t yb = (size_t)(c1 - c0) * 16u * N2_MM21_Y_TILE_H;
	size_t cb = (size_t)(d1 - d0) * 16u * N2_MM21_UV_TILE_H;
	return yb > cb ? yb : cb;
}

/* one plane through the staging buffer, tile row by tile row */
static int staged_plane(n2_mm21_fetch_fn fetch, void *ctx, int plane, uint32_t stride, uint32_t th,
                        uint8_t *staging, uint8_t *dst, size_t pitch, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	uint32_t c0, c1;
	tile_cols(x, w, &c0, &c1);
	size_t tb = (size_t)16u * th;
	uint32_t xr = x - c0 * 16u;                 /* x relative to the first fetched tile column */
	uint32_t r = 0;
	while (r < h) {
		uint32_t yy = y + r;
		uint32_t tr = yy / th;
		uint32_t in0 = yy % th;                 /* first row inside this tile row */
		uint32_t n = th - in0;
		if (n > h - r) { n = h - r; }
		uint64_t off = (uint64_t)tr * stride * th + (uint64_t)c0 * tb;
		if (off > UINT32_MAX) { return -EINVAL; }
		int e = fetch(ctx, plane, (uint32_t)off, (uint32_t)((c1 - c0) * tb), staging);
		if (e) { return e < 0 ? e : -EIO; }
		/* staging holds tile columns c0..c1-1 of tile row tr: a plane of stride (c1-c0)*16 and one tile row */
		for (uint32_t k = 0; k < n; k++) {
			detile_row(staging + (size_t)(in0 + k) * 16u, tb, dst + (size_t)(r + k) * pitch, xr, w);
		}
		r += n;
	}
	return 0;
}

int n2_mm21_to_nv12_staged(const struct n2_mm21_frame *src, n2_mm21_fetch_fn fetch, void *ctx,
                           uint8_t *staging, size_t staging_bytes, const struct n2_nv12_out *dst,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	uint32_t cw, ch;
	int e = check_rect(src, dst, x, y, w, h, &cw, &ch);
	if (e) { return e; }
	if (!fetch || !staging || staging_bytes < n2_mm21_staging_bytes(src, x, w)) { return -EINVAL; }
	e = staged_plane(fetch, ctx, 0, src->y_stride, N2_MM21_Y_TILE_H, staging, dst->y, dst->y_pitch, x, y, w, h);
	if (e) { return e; }
	return staged_plane(fetch, ctx, 1, src->uv_stride, N2_MM21_UV_TILE_H, staging, dst->uv, dst->uv_pitch,
	                    x, y / 2u, cw, ch);
}

int n2_nv12_to_mm21(const uint8_t *src_y, size_t src_y_pitch, const uint8_t *src_uv, size_t src_uv_pitch,
                    uint32_t w, uint32_t h, uint8_t *dst_y, uint8_t *dst_uv, uint32_t stride, uint32_t buf_h)
{
	if (!src_y || !src_uv || !dst_y || !dst_uv || !w || !h) { return -EINVAL; }
	if (!stride || (stride & 15u) || !buf_h || (buf_h & 31u) || w > stride || h > buf_h) { return -EINVAL; }
	uint32_t cw = ((w + 1u) >> 1) * 2u, ch = (h + 1u) >> 1;
	if (src_y_pitch < w || src_uv_pitch < cw || cw > stride) { return -EINVAL; }
	for (uint32_t yy = 0; yy < buf_h; yy++) {
		const uint8_t *s = src_y + (size_t)(yy < h ? yy : h - 1u) * src_y_pitch;
		uint8_t *t = dst_y + (size_t)(yy >> 5) * stride * 32u + (size_t)(yy & 31u) * 16u;
		for (uint32_t xx = 0; xx < stride; xx++) {
			t[(size_t)(xx >> 4) * 512u + (xx & 15u)] = s[xx < w ? xx : w - 1u];
		}
	}
	for (uint32_t yy = 0; yy < buf_h / 2u; yy++) {
		const uint8_t *s = src_uv + (size_t)(yy < ch ? yy : ch - 1u) * src_uv_pitch;
		uint8_t *t = dst_uv + (size_t)(yy >> 4) * stride * 16u + (size_t)(yy & 15u) * 16u;
		for (uint32_t xx = 0; xx < stride; xx++) {
			uint32_t sx = xx < cw ? xx : cw - 2u + (xx & 1u);   /* replicate the last Cb,Cr pair */
			t[(size_t)(xx >> 4) * 256u + (xx & 15u)] = s[sx];
		}
	}
	return 0;
}

/* ---- opus-decint: banded multithreaded conversion ---------------------------------------------------------------- */
struct n2_mm21_band {
	const struct n2_mm21_frame *src;
	struct n2_nv12_out dst;
	uint32_t x, y, w, h;
	int rc;
};

static void *n2_mm21_band_run(void *arg)
{
	struct n2_mm21_band *b = (struct n2_mm21_band *)arg;
	b->rc = n2_mm21_to_nv12(b->src, &b->dst, b->x, b->y, b->w, b->h);
	return NULL;
}

int n2_mm21_to_nv12_mt(const struct n2_mm21_frame *src, const struct n2_nv12_out *dst,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h, unsigned threads)
{
	struct n2_mm21_band b[16];
	pthread_t th[16];
	unsigned n, i, started = 0;
	uint32_t per, off;
	int rc = 0;

	if (!src || !dst || (y & 1u))
		return -EINVAL;
	n = threads == 0u ? 1u : threads > 16u ? 16u : threads;
	per = ((h / n) + 31u) & ~31u;           /* a multiple of 32 rows: whole luma (and chroma) tile rows per band */
	if (n == 1u || per == 0u || h <= 64u)
		return n2_mm21_to_nv12(src, dst, x, y, w, h);
	for (i = 0, off = 0; i < n && off < h; i++) {
		uint32_t rows = (i == n - 1u || off + per >= h) ? h - off : per;
		b[i].src = src;
		b[i].dst.y = dst->y + (size_t)off * dst->y_pitch;
		b[i].dst.uv = dst->uv + (size_t)(off / 2u) * dst->uv_pitch;
		b[i].dst.y_pitch = dst->y_pitch;
		b[i].dst.uv_pitch = dst->uv_pitch;
		b[i].x = x;
		b[i].y = y + off;
		b[i].w = w;
		b[i].h = rows;
		b[i].rc = 0;
		off += rows;
	}
	n = i;
	for (i = 1; i < n; i++) {
		if (pthread_create(&th[i], NULL, n2_mm21_band_run, &b[i]) == 0)
			started |= 1u << i;
		else
			(void)n2_mm21_band_run(&b[i]);
	}
	(void)n2_mm21_band_run(&b[0]);
	for (i = 1; i < n; i++)
		if (started & (1u << i))
			(void)pthread_join(th[i], NULL);
	for (i = 0; i < n; i++)
		if (b[i].rc != 0 && rc == 0)
			rc = b[i].rc;
	return rc;
}
