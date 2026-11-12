#ifndef SWEET_FB_BLEND_H
#define SWEET_FB_BLEND_H
/*
 * 0390 (fbblend-20261112): the SwapEnd layers are composited, not copied over each other.  Pure and host-testable.
 *
 * Why: CoreAnimation (backboardd) detaches a static, opaque, full-screen app drawable (MobileSlideShow's CAMetalLayer, run
 * thumbcomp5 bbdiag-60: from composite n=2001 on, backboardd no longer draws Photos' surface s112/s119 into its own frame, only
 * the 95x40 / 289x50 blur sources of the "Select" / "..." pills, and its own frame is 00000000 = transparent where the grid is)
 * and hands it to the display as a separate SwapEnd layer under the UI layer.  presentCopy memcpy'd every enabled layer in
 * index order, so the transparent UI layer replaced the grid with black: status bar, pills and tab bar stayed, the grid
 * (and the "Dec 31, 1969" title, drawn by Photos) vanished ~1.5 s after the launch, exactly as on screen.
 *
 * Layers are composited in index order (0 = bottom): the lowest layer that covers a row is copied, every higher layer is
 * blended over it with premultiplied source-over (CoreAnimation's surfaces are premultiplied BGRA), in a cached row buffer,
 * then the row is written once to the (write-combined) panel memory.  A single layer is still a plain copy.
 */
#include <stdbool.h>
#include <stdint.h>

struct sweet_fb_blend_stats {
	uint64_t opaque, clear, mixed;          /* pixels of the blended (non-bottom) layers */
};

/* d = s + d * (255 - a(s)) / 255 per channel (exact rounding of x/255 for x <= 255*255). */
static inline uint32_t
sweet_fb_blend_px(uint32_t s, uint32_t d)
{
	uint32_t ia = 255u - (s >> 24);
	uint32_t rb = (d & 0x00ff00ffu) * ia + 0x00800080u;
	uint32_t ag = ((d >> 8) & 0x00ff00ffu) * ia + 0x00800080u;
	rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
	ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
	uint32_t o = rb | ag;
	/* per channel saturating add (a premultiplied source never overflows, a non-premultiplied one must not wrap) */
	uint32_t sum_rb = (s & 0x00ff00ffu) + (o & 0x00ff00ffu);
	uint32_t sum_ag = ((s >> 8) & 0x00ff00ffu) + ((o >> 8) & 0x00ff00ffu);
	sum_rb |= ((sum_rb & 0x01000100u) >> 8) * 0xffu;
	sum_ag |= ((sum_ag & 0x01000100u) >> 8) * 0xffu;
	return (sum_rb & 0x00ff00ffu) | ((sum_ag & 0x00ff00ffu) << 8);
}

static inline void
sweet_fb_blend_row(uint32_t *dst, const uint32_t *src, uint32_t n, struct sweet_fb_blend_stats *st)
{
	uint64_t op = 0, cl = 0, mx = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t s = src[i], a = s >> 24;
		if (a == 0xffu) { dst[i] = s; op++; }
		else if (s == 0) { cl++; }
		else { dst[i] = sweet_fb_blend_px(s, dst[i]); mx++; }
	}
	if (st != 0) { st->opaque += op; st->clear += cl; st->mixed += mx; }
}

struct sweet_fb_layer_geom {
	int32_t sx, sy, dx, dy, w, h;
};

/*
 * The clipping presentCopy always did (no scaling): rect a gives the source origin, rect b the logical destination and size
 * (the surface size when empty), the logical display is shifted by (offx, offy) onto the fw x fh panel.  Returns false when
 * nothing is left to show.
 */
static inline bool
sweet_fb_layer_clip(struct sweet_fb_layer_geom *g, int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h,
    uint32_t sw, uint32_t sh, int32_t offx, int32_t offy, uint32_t fw, uint32_t fh)
{
	if (w <= 0 || h <= 0) { w = (int32_t)sw; h = (int32_t)sh; }
	if (sx < 0 || sy < 0) { sx = sy = 0; }
	if (dx < 0 || dy < 0) { dx = dy = 0; }
	dx -= offx;
	dy -= offy;
	if (dx < 0) { sx -= dx; w += dx; dx = 0; }
	if (dy < 0) { sy -= dy; h += dy; dy = 0; }
	if (w > (int32_t)sw - sx) { w = (int32_t)sw - sx; }
	if (h > (int32_t)sh - sy) { h = (int32_t)sh - sy; }
	if (w > (int32_t)fw - dx) { w = (int32_t)fw - dx; }
	if (h > (int32_t)fh - dy) { h = (int32_t)fh - dy; }
	g->sx = sx; g->sy = sy; g->dx = dx; g->dy = dy; g->w = w; g->h = h;
	return w > 0 && h > 0;
}

/* One panel row of the composition.  rows[i] points at layer i's source pixel for panel x = geom[i].dx on this row, or is 0
 * when layer i does not cover the row.  row is a cached buffer of fw pixels.  Returns the written span [*x0, *x1); pixels of
 * the span that no layer covers are black. */
static inline bool
sweet_fb_compose_row(uint32_t *row, const uint32_t *const rows[], const struct sweet_fb_layer_geom geom[], unsigned n,
    uint32_t *x0, uint32_t *x1, struct sweet_fb_blend_stats *st)
{
	int first = -1;
	int32_t lo = 0, hi = 0;
	for (unsigned i = 0; i < n; i++) {
		if (rows[i] == 0) { continue; }
		if (first < 0) { first = (int)i; lo = geom[i].dx; hi = geom[i].dx + geom[i].w; }
		else {
			if (geom[i].dx < lo) { lo = geom[i].dx; }
			if (geom[i].dx + geom[i].w > hi) { hi = geom[i].dx + geom[i].w; }
		}
	}
	if (first < 0) { return false; }
	const struct sweet_fb_layer_geom *b = &geom[first];
	if (b->dx != lo || b->dx + b->w != hi) {
		for (int32_t x = lo; x < hi; x++) { row[x] = 0; }
	}
	for (int32_t x = 0; x < b->w; x++) { row[b->dx + x] = rows[first][x]; }
	for (unsigned i = (unsigned)first + 1; i < n; i++) {
		if (rows[i] == 0) { continue; }
		sweet_fb_blend_row(row + geom[i].dx, rows[i], (uint32_t)geom[i].w, st);
	}
	*x0 = (uint32_t)lo;
	*x1 = (uint32_t)hi;
	return true;
}

#endif /* SWEET_FB_BLEND_H */
