/* 0416: TEST-ONLY reference model (the CPU blend of 0390, deleted from the kernel): the host tests compare the overlay register
 * output against it pixel by pixel.  Include after sweet_fb_blend.h. */
#ifndef SWEET_FB_BLEND_REF_H
#define SWEET_FB_BLEND_REF_H
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

#endif
