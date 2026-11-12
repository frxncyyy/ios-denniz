/* Host test of 0390 sweet_fb_blend.h: cc -O2 -I../b/iokit/Kernel/arm fbblend_host_test.c && ./a.out */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_blend.h"

static uint32_t ref_px(uint32_t s, uint32_t d)
{
	uint32_t a = s >> 24, o = 0;
	for (int c = 0; c < 32; c += 8) {
		uint32_t sc = (s >> c) & 255, dc = (d >> c) & 255;
		uint32_t v = sc + (dc * (255 - a) + 127) / 255;
		if (v > 255) v = 255;
		o |= v << c;
	}
	return o;
}

/* presentCopy's clipping before 0390, verbatim logic */
static int old_clip(int32_t *o, int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h,
    uint32_t sw, uint32_t sh, int32_t offx, int32_t offy, uint32_t fw, uint32_t fh)
{
	if (w <= 0 || h <= 0) { w = sw; h = sh; }
	if (sx < 0 || sy < 0) { sx = sy = 0; }
	if (dx < 0 || dy < 0) { dx = dy = 0; }
	dx -= offx; dy -= offy;
	if (dx < 0) { sx -= dx; w += dx; dx = 0; }
	if (dy < 0) { sy -= dy; h += dy; dy = 0; }
	if (w > (int32_t)sw - sx) w = sw - sx;
	if (h > (int32_t)sh - sy) h = sh - sy;
	if (w > (int32_t)fw - dx) w = fw - dx;
	if (h > (int32_t)fh - dy) h = fh - dy;
	o[0] = sx; o[1] = sy; o[2] = dx; o[3] = dy; o[4] = w; o[5] = h;
	return w > 0 && h > 0;
}

int main(void)
{
	int bad = 0;
	srand(7);
	/* 1. pixel blend against a per-channel reference (exact rounding allowed +-1) */
	for (int i = 0; i < 2000000; i++) {
		uint32_t a = rand() & 255, s = 0, d = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
		for (int c = 0; c < 24; c += 8) s |= (uint32_t)(rand() % (a + 1)) << c;     /* premultiplied */
		s |= a << 24;
		if (i & 1) s = (uint32_t)rand() ^ ((uint32_t)rand() << 16);                  /* also non-premultiplied: saturate */
		uint32_t g = sweet_fb_blend_px(s, d), r = ref_px(s, d);
		for (int c = 0; c < 32; c += 8) {
			int dg = (int)((g >> c) & 255) - (int)((r >> c) & 255);
			if (dg > 1 || dg < -1) { if (bad++ < 5) printf("px s=%08x d=%08x got %08x ref %08x\n", s, d, g, r); break; }
		}
	}
	if (sweet_fb_blend_px(0x00000000u, 0xff414443u) != 0xff414443u) { bad++; printf("clear over grid changed it\n"); }
	if (sweet_fb_blend_px(0xff112233u, 0xff414443u) != 0xff112233u) { bad++; printf("opaque not replacing\n"); }
	/* 2. clipping identical to the pre-0390 code */
	for (int i = 0; i < 1000000; i++) {
		int32_t v[6]; for (int k = 0; k < 6; k++) v[k] = (rand() % 3000) - 300;
		uint32_t sw = rand() % 1300, sh = rand() % 2600;
		int32_t o[6]; struct sweet_fb_layer_geom g;
		int r1 = old_clip(o, v[0], v[1], v[2], v[3], v[4], v[5], sw, sh, 22, 18, 1080, 2400);
		int r2 = sweet_fb_layer_clip(&g, v[0], v[1], v[2], v[3], v[4], v[5], sw, sh, 22, 18, 1080, 2400);
		if (r1 != r2 || (r1 && (o[0] != g.sx || o[1] != g.sy || o[2] != g.dx || o[3] != g.dy || o[4] != g.w || o[5] != g.h))) {
			if (bad++ < 5) printf("clip mismatch %d\n", i);
		}
	}
	/* 3. the thumbcomp5 frame: layer 0 = Photos' opaque grid (1125x2436), layer 1 = backboardd's UI, transparent except a tab bar */
	enum { LW = 1125, LH = 2436, FW = 1080, FH = 2400 };
	uint32_t *photos = malloc(LW * LH * 4), *ui = malloc(LW * LH * 4), *panel = calloc(FW * FH, 4), *row = malloc(FW * 4);
	for (int i = 0; i < LW * LH; i++) { photos[i] = 0xff414443u; ui[i] = 0; }
	for (int y = 2200; y < LH; y++) for (int x = 0; x < LW; x++) ui[y * LW + x] = 0xff202020u;      /* opaque tab bar */
	for (int x = 0; x < LW; x++) ui[1000 * LW + x] = 0x80000000u;                                 /* 50% black shadow row */
	struct sweet_fb_layer_geom g[2];
	sweet_fb_layer_clip(&g[0], 0, 0, 0, 0, LW, LH, LW, LH, 22, 18, FW, FH);
	sweet_fb_layer_clip(&g[1], 0, 0, 0, 0, LW, LH, LW, LH, 22, 18, FW, FH);
	struct sweet_fb_blend_stats st = { 0 };
	for (int y = 0; y < FH; y++) {
		const uint32_t *rows[2];
		for (int i = 0; i < 2; i++) {
			const uint32_t *base = i ? ui : photos;
			rows[i] = (y >= g[i].dy && y < g[i].dy + g[i].h) ? base + (g[i].sy + y - g[i].dy) * LW + g[i].sx : NULL;
		}
		uint32_t x0, x1;
		if (sweet_fb_compose_row(row, rows, g, 2, &x0, &x1, &st)) memcpy(panel + y * FW + x0, row + x0, (x1 - x0) * 4);
	}
	if (panel[500 * FW + 500] != 0xff414443u) { bad++; printf("grid pixel %08x\n", panel[500 * FW + 500]); }
	if (panel[2300 * FW + 10] != 0xff202020u) { bad++; printf("tab bar pixel %08x\n", panel[2300 * FW + 10]); }
	uint32_t sh = panel[(1000 - 18) * FW + 3];
	if (sh != ref_px(0x80000000u, 0xff414443u)) { bad++; printf("shadow pixel %08x\n", sh); }
	printf("upper-layer px opaque=%llu clear=%llu mixed=%llu\n", (unsigned long long)st.opaque, (unsigned long long)st.clear,
	    (unsigned long long)st.mixed);
	/* the pre-0390 copy of the same frame: black grid */
	for (int y = 0; y < FH; y++) memcpy(panel + y * FW, ui + (y + 18) * LW + 22, FW * 4);
	printf("pre-0390 copy grid pixel %08x (the bug)\n", panel[500 * FW + 500]);
	/* 4. partial-width upper layer wider span: uncovered pixels black, covered blended */
	{
		uint32_t a[4] = { 0xff010101u, 0xff010101u, 0, 0 }, b[4] = { 0xff090909u, 0xff090909u, 0xff090909u, 0xff090909u };
		struct sweet_fb_layer_geom gg[2] = { { 0, 0, 2, 0, 2, 1 }, { 0, 0, 3, 0, 3, 1 } };
		const uint32_t *rr[2] = { a, b };
		uint32_t rw[8] = { 7, 7, 7, 7, 7, 7, 7, 7 }, x0, x1;
		sweet_fb_compose_row(rw, rr, gg, 2, &x0, &x1, NULL);
		if (x0 != 2 || x1 != 6 || rw[2] != 0xff010101u || rw[3] != 0xff090909u || rw[5] != 0xff090909u) { bad++; printf("span\n"); }
	}
	printf(bad ? "FBBLEND_HOST_TEST_FAIL %d\n" : "FBBLEND_HOST_TEST_OK\n", bad);
	return bad != 0;
}
