/*
 * y2s91 (thumbgrid): the parts of redwoodmetal.m's rw_tg_* code that touch neither Metal, IOSurface nor the GPU, in a header of their
 * own so that rw_thumbgrid_host_test.c checks the very code the driver runs (no phone, no GPU).
 *
 *   rw_tg_ref_bgra      CPU reference of the GPU conversion pass (air_metal.c ycbcr_fragment_shader + the sampler of ycbcr_state):
 *                       luma sampled at its texel centre, chroma BILINEAR at (x + 0.5) / 2 - 0.5 (centre-sited 4:2:0) with clamp to
 *                       edge, BT.601/709/2020, full or video range; out BGRA8.
 *   rw_tg_compare       the GPU's BGRA block against the reference: per-channel tolerance, "gross" (black cell, stripe) threshold.
 *   rw_tg_class         size classes of the buffer pool.
 *   rw_tg_seq_*         which command buffers are still on the GPU: a ring of submit sequence numbers, completions in any order.
 */
#ifndef RW_THUMBGRID_PURE_H
#define RW_THUMBGRID_PURE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline float
rw_tg_clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* one chroma channel (0 Cb, 1 Cr) of the interleaved plane, bilinear at chroma texel coordinates (cx, cy), clamp to edge */
static inline float
rw_tg_chroma(const uint8_t *chroma, size_t chroma_row, unsigned cw, unsigned ch, float cx, float cy, unsigned c)
{
	cx = rw_tg_clampf(cx, 0.0f, (float)(cw - 1));
	cy = rw_tg_clampf(cy, 0.0f, (float)(ch - 1));
	unsigned x0 = (unsigned)cx, y0 = (unsigned)cy;
	unsigned x1 = x0 + 1 < cw ? x0 + 1 : x0, y1 = y0 + 1 < ch ? y0 + 1 : y0;
	float fx = cx - (float)x0, fy = cy - (float)y0;
	float a = chroma[(size_t)y0 * chroma_row + 2 * x0 + c], b = chroma[(size_t)y0 * chroma_row + 2 * x1 + c];
	float d = chroma[(size_t)y1 * chroma_row + 2 * x0 + c], e = chroma[(size_t)y1 * chroma_row + 2 * x1 + c];

	return ((a * (1 - fx) + b * fx) * (1 - fy) + (d * (1 - fx) + e * fx) * fy) / 255.0f;
}

/*
 * The block (bx, by, bw, bh) of a width x height picture, as the pass converts it, into `out` (bw * 4 bytes a row, B G R A).
 * cw/ch: the chroma plane's size in texels, (width + 1) / 2 x (height + 1) / 2 -- also when the GPU reads an even-padded import,
 * whose chroma plane is the same plane (188 = 376 / 2 = (375 + 1) / 2).
 */
static inline void
rw_tg_ref_bgra(const uint8_t *luma, size_t luma_row, const uint8_t *chroma, size_t chroma_row, unsigned width, unsigned height,
	       int matrix, int full, unsigned bx, unsigned by, unsigned bw, unsigned bh, uint8_t *out)
{
	static const double kr_kb[3][2] = { { 0.299, 0.114 }, { 0.2126, 0.0722 }, { 0.2627, 0.0593 } };
	const double kr = kr_kb[matrix < 0 || matrix > 2 ? 0 : matrix][0], kb = kr_kb[matrix < 0 || matrix > 2 ? 0 : matrix][1];
	const double kg = 1.0 - kr - kb;
	const float ys = full ? 1.0f : 255.0f / 219.0f, cs = full ? 1.0f : 255.0f / 224.0f;
	const float y0 = full ? 0.0f : 16.0f / 255.0f, c0 = 128.0f / 255.0f;
	const float rv = (float)(2.0 * (1.0 - kr)) * cs, gu = (float)(-2.0 * kb * (1.0 - kb) / kg) * cs;
	const float gv = (float)(-2.0 * kr * (1.0 - kr) / kg) * cs, bu = (float)(2.0 * (1.0 - kb)) * cs;
	const unsigned cw = (width + 1) / 2, ch = (height + 1) / 2;

	for (unsigned j = 0; j < bh; j++) {
		unsigned y = by + j < height ? by + j : height - 1;

		for (unsigned i = 0; i < bw; i++) {
			unsigned x = bx + i < width ? bx + i : width - 1;
			float yy = (float)luma[(size_t)y * luma_row + x] / 255.0f;
			float cx = ((float)x + 0.5f) * 0.5f - 0.5f, cy = ((float)y + 0.5f) * 0.5f - 0.5f;
			float u = rw_tg_chroma(chroma, chroma_row, cw, ch, cx, cy, 0) - c0;
			float v = rw_tg_chroma(chroma, chroma_row, cw, ch, cx, cy, 1) - c0;
			float l = (yy - y0 > 0.0f ? yy - y0 : 0.0f) * ys;
			float rgb[3] = { l + v * rv, l + u * gu + v * gv, l + u * bu };
			uint8_t *o = out + (size_t)j * bw * 4 + (size_t)i * 4;

			o[0] = (uint8_t)(rw_tg_clampf(rgb[2], 0, 1) * 255.0f + 0.5f);
			o[1] = (uint8_t)(rw_tg_clampf(rgb[1], 0, 1) * 255.0f + 0.5f);
			o[2] = (uint8_t)(rw_tg_clampf(rgb[0], 0, 1) * 255.0f + 0.5f);
			o[3] = 255;
		}
	}
}

struct rw_tg_cmp {
	unsigned over;          /* pixels with a channel beyond `tol` */
	unsigned gross;         /* pixels with a channel beyond `gross_tol` (black cell, wrong picture, stripe) */
	unsigned maxdiff;
	unsigned sumdiff;       /* over all channels, for the mean */
};

/* both blocks bw x bh, B G R A, `a_row` / `b_row` bytes a row; alpha is not compared (the pass writes 1) */
static inline void
rw_tg_compare(const uint8_t *a, size_t a_row, const uint8_t *b, size_t b_row, unsigned bw, unsigned bh, unsigned tol,
	      unsigned gross_tol, struct rw_tg_cmp *r)
{
	memset(r, 0, sizeof(*r));
	for (unsigned j = 0; j < bh; j++) {
		for (unsigned i = 0; i < bw; i++) {
			const uint8_t *p = a + (size_t)j * a_row + 4 * i, *q = b + (size_t)j * b_row + 4 * i;
			unsigned m = 0;

			for (unsigned c = 0; c < 3; c++) {
				unsigned d = p[c] > q[c] ? (unsigned)(p[c] - q[c]) : (unsigned)(q[c] - p[c]);

				m = d > m ? d : m;
				r->sumdiff += d;
			}
			r->over += m > tol;
			r->gross += m > gross_tol;
			r->maxdiff = m > r->maxdiff ? m : r->maxdiff;
		}
	}
}

/* a block is a MISMATCH when any pixel is gross or more than 2 of its pixels are beyond the tolerance */
static inline int
rw_tg_is_mismatch(const struct rw_tg_cmp *r)
{
	return r->gross > 0 || r->over > 2;
}

/*
 * Which block of a width x height picture the k-th check reads (16x16, or the picture when smaller): a rotation of interior,
 * right edge, bottom edge, bottom-right corner, top-left -- the right edge of 375-wide cells is where the thin lines were seen.
 */
static inline void
rw_tg_check_block(unsigned long k, unsigned width, unsigned height, unsigned *bx, unsigned *by, unsigned *bw, unsigned *bh, int *edge)
{
	unsigned w = width < 16 ? width : 16, h = height < 16 ? height : 16;
	uint32_t r = (uint32_t)(k * 2654435761u);

	*bw = w;
	*bh = h;
	*edge = 1;
	switch (k % 5) {
	case 0: *bx = (r % (width - w + 1)); *by = ((r >> 16) % (height - h + 1)); *edge = 0; break;
	case 1: *bx = width - w; *by = (r >> 8) % (height - h + 1); break;
	case 2: *bx = (r >> 4) % (width - w + 1); *by = height - h; break;
	case 3: *bx = width - w; *by = height - h; break;
	default: *bx = 0; *by = 0; break;
	}
}

/* the pool's size class of a request: powers of two from 4 KiB to 64 KiB, then 64 KiB steps to 1 MiB, then 1 MiB steps */
static inline size_t
rw_tg_class(size_t length)
{
	if (length <= 4096)
		return 4096;
	if (length <= 65536) {
		size_t c = 4096;

		while (c < length)
			c <<= 1;
		return c;
	}
	if (length <= (1u << 20))
		return (length + 65535) & ~(size_t)65535;
	return (length + (1u << 20) - 1) & ~(size_t)((1u << 20) - 1);
}

/*
 * Submit sequence numbers: rw_tg_seq_submit gives the next one (it is on the GPU from then on), rw_tg_seq_done says it came back, in
 * any order. rw_tg_seq_low is the oldest one still out (or the next to be given when none is): something let go when `next` was N
 * is no longer read by the GPU once low > N - 1 ... i.e. once every submit up to N-1 has come back: rw_tg_seq_safe(s, N).
 * One ring for the process; a window of RW_TG_SEQ_RING submits (beyond it rw_tg_seq_safe answers no). The caller holds the lock.
 */
#define RW_TG_SEQ_RING 4096u
struct rw_tg_seq {
	uint64_t next;                  /* the next number to give; numbers start at 1 */
	uint64_t low;                   /* every number below it has come back */
	uint64_t untracked;             /* submits given 0 (window full) and not back yet: nothing is safe while any is out */
	uint8_t done[RW_TG_SEQ_RING];   /* came back, for numbers in [low, next) */
};

static inline void
rw_tg_seq_init(struct rw_tg_seq *q)
{
	memset(q, 0, sizeof(*q));
	q->next = 1;
	q->low = 1;
}

static inline uint64_t
rw_tg_seq_submit(struct rw_tg_seq *q)
{
	if (q->next - q->low >= RW_TG_SEQ_RING) {
		q->untracked++;
		return 0;               /* too many out: not tracked (0 = "unknown": nothing is safe until it is back) */
	}
	uint64_t s = q->next++;

	q->done[s % RW_TG_SEQ_RING] = 0;
	return s;
}

static inline void
rw_tg_seq_done(struct rw_tg_seq *q, uint64_t s)
{
	if (s == 0) {
		q->untracked -= q->untracked > 0;
		return;
	}
	if (s < q->low || s >= q->next) {
		return;
	}
	q->done[s % RW_TG_SEQ_RING] = 1;
	while (q->low < q->next && q->done[q->low % RW_TG_SEQ_RING]) {
		q->low++;
	}
}

/* the mark of "now": what is let go now is safe to reuse once every submit given so far has come back */
static inline uint64_t
rw_tg_seq_mark(const struct rw_tg_seq *q)
{
	return q->next;
}

static inline int
rw_tg_seq_safe(const struct rw_tg_seq *q, uint64_t mark)
{
	return q->low >= mark && q->untracked == 0;
}

#endif
