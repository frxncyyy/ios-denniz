/*
 * t_kern.c -- host oracle for the three n2vk kernels (WP08).
 *
 * Expected values are NOT taken from n2vk_ref.c:
 *   - me16 on the shifted picture: the displacement is known by construction (sad 0);
 *   - me16 on small pictures: an independently written full search, every candidate
 *     sorted by qsort on (cost, |dx-cx|+|dy-cy|, dy, dx), which is the header's order;
 *   - the DCT table: a literal copy of the lround() values, and the two-pass integer
 *     transform evaluated from that literal table (Python ints at authoring time);
 *   - the quantiser's four tie cases: literal, through a value no 8-bit block reaches.
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2vk.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run, tests_failed;
static const char *current_test;

#define CHECK(cond) do {                                                                       \
	if (!(cond)) {                                                                         \
		fprintf(stderr, "FAILED [%s] %s:%d: %s\n", current_test, __FILE__, __LINE__, #cond); \
		tests_failed++;                                                                     \
		return;                                                                             \
	}                                                                                       \
} while (0)

static void
start(const char *name)
{
	current_test = name;
	tests_run++;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * A deterministic picture: the task's 32-bit LCG, seed 12345, value = state >> 24.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static uint32_t lcg_state;

static void
lcg_seed(uint32_t seed)
{
	lcg_state = seed;
}

static uint8_t
lcg_byte(void)
{
	lcg_state = lcg_state * 1664525u + 1013904223u;
	return (uint8_t)(lcg_state >> 24);
}

/* A padded reference picture: `pad' readable samples on each side of a width x height
 * picture.  The pointer handed to n2vk_me16 is the sample (0, 0); everything at a
 * negative coordinate lies before it, exactly as n2vk.h describes. */
struct padded {
	uint8_t *all;      /* the allocation */
	uint8_t *base;     /* sample (0, 0) of the picture */
	uint32_t stride;
};

static int
padded_make(struct padded *p, uint32_t width, uint32_t height, uint32_t pad)
{
	p->stride = width + 2 * pad;
	p->all = calloc(p->stride, height + 2 * pad);
	if (p->all == NULL)
		return 0;
	p->base = p->all + (size_t)pad * p->stride + pad;
	return 1;
}

static uint8_t *
padded_at(struct padded *p, int32_t x, int32_t y, uint32_t pad)
{
	return p->all + (size_t)(y + (int32_t)pad) * p->stride + (size_t)(x + (int32_t)pad);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * The independent brute force: every candidate, sorted.
 * ----------------------------------------------------------------------------------------------------------------------------- */

struct cand {
	uint32_t cost;
	int32_t rank, dy, dx;
};

static int
cand_cmp(const void *va, const void *vb)
{
	const struct cand *a = va, *b = vb;

	if (a->cost != b->cost)
		return a->cost < b->cost ? -1 : 1;
	if (a->rank != b->rank)
		return a->rank < b->rank ? -1 : 1;
	if (a->dy != b->dy)
		return a->dy < b->dy ? -1 : 1;
	if (a->dx != b->dx)
		return a->dx < b->dx ? -1 : 1;
	return 0;
}

static int32_t
imax(int32_t a, int32_t b)
{
	return a > b ? a : b;
}

static int32_t
imin(int32_t a, int32_t b)
{
	return a < b ? a : b;
}

static uint32_t
independent_cost(const n2vk_me16_args *a, uint32_t mx, uint32_t my, int32_t dx, int32_t dy)
{
	uint32_t sum = 0;
	const int32_t pad = (int32_t)a->pad;
	const uint8_t *ref0 = a->ref - (ptrdiff_t)pad * (ptrdiff_t)a->ref_stride - (ptrdiff_t)pad;

	for (int32_t row = 0; row < 16; row++) {
		const uint8_t *c = a->cur + (size_t)(16 * my + (uint32_t)row) * a->cur_stride + 16 * mx;
		const uint8_t *r = ref0 + (size_t)(16 * (int32_t)my + dy + row + pad) * a->ref_stride +
		                   (size_t)(16 * (int32_t)mx + dx + pad);

		for (int32_t col = 0; col < 16; col++) {
			const int32_t d = (int32_t)c[col] - (int32_t)r[col];

			sum += (uint32_t)(d < 0 ? -d : d);
		}
	}
	return sum;
}

/* What the header says the answer is, computed by a different program: enumerate every
 * candidate in the allowed rectangle within `range' of the CLAMPED centre, then sort. */
static void
independent_search(const n2vk_me16_args *a, uint32_t mx, uint32_t my, int32_t *out_dx, int32_t *out_dy, uint32_t *out_cost)
{
	const int32_t pad = (int32_t)a->pad, range = (int32_t)a->range;
	const uint32_t i = my * a->mb_w + mx;
	int32_t lo_x = -pad - 16 * (int32_t)mx;
	int32_t hi_x = 16 * ((int32_t)a->mb_w - 1 - (int32_t)mx) + pad;
	int32_t lo_y = -pad - 16 * (int32_t)my;
	int32_t hi_y = 16 * ((int32_t)a->mb_h - 1 - (int32_t)my) + pad;
	int32_t cx = a->centre_x ? a->centre_x[i] : 0;
	int32_t cy = a->centre_y ? a->centre_y[i] : 0;
	size_t capacity, used = 0;

	cx = imin(imax(cx, lo_x), hi_x);
	cy = imin(imax(cy, lo_y), hi_y);
	capacity = (size_t)(hi_x - lo_x + 1) * (size_t)(hi_y - lo_y + 1);

	struct cand *all = calloc(capacity, sizeof(*all));

	if (all == NULL) {
		fprintf(stderr, "out of memory in the independent search\n");
		exit(2);
	}
	for (int32_t dy = lo_y; dy <= hi_y; dy++) {
		if (dy < cy - range || dy > cy + range)
			continue;
		for (int32_t dx = lo_x; dx <= hi_x; dx++) {
			if (dx < cx - range || dx > cx + range)
				continue;
			all[used].cost = independent_cost(a, mx, my, dx, dy);
			all[used].rank = (dx > cx ? dx - cx : cx - dx) + (dy > cy ? dy - cy : cy - dy);
			all[used].dy = dy;
			all[used].dx = dx;
			used++;
		}
	}
	if (used == 0) {
		fprintf(stderr, "the independent search found no candidate\n");
		exit(2);
	}
	qsort(all, used, sizeof(*all), cand_cmp);
	*out_dx = all[0].dx;
	*out_dy = all[0].dy;
	*out_cost = all[0].cost;
	free(all);
}

static void
compare_with_independent(const n2vk_me16_args *a, const char *what)
{
	const size_t n = (size_t)a->mb_w * a->mb_h;
	int16_t *mvx = calloc(n, sizeof(*mvx)), *mvy = calloc(n, sizeof(*mvy));
	uint32_t *sad = calloc(n, sizeof(*sad));
	int32_t *ex = calloc(n, sizeof(*ex)), *ey = calloc(n, sizeof(*ey));
	uint32_t *ec = calloc(n, sizeof(*ec));

	if (mvx == NULL || mvy == NULL || sad == NULL || ex == NULL || ey == NULL || ec == NULL) {
		fprintf(stderr, "out of memory in %s\n", what);
		exit(2);
	}
	for (uint32_t my = 0; my < a->mb_h; my++)
		for (uint32_t mx = 0; mx < a->mb_w; mx++)
			independent_search(a, mx, my, &ex[my * a->mb_w + mx], &ey[my * a->mb_w + mx], &ec[my * a->mb_w + mx]);
	CHECK(n2vk_ref_me16(a, mvx, mvy, sad) == N2VK_OK);
	for (size_t i = 0; i < n; i++) {
		if (mvx[i] != ex[i] || mvy[i] != ey[i] || sad[i] != ec[i]) {
			fprintf(stderr, "FAILED [%s] %s: mb %zu: got (%d,%d) sad %u, independent (%d,%d) sad %u\n", current_test, what, i,
			        mvx[i], mvy[i], sad[i], ex[i], ey[i], ec[i]);
			tests_failed++;
			free(mvx);
			free(mvy);
			free(sad);
			free(ex);
			free(ey);
			free(ec);
			return;
		}
	}
	free(mvx);
	free(mvy);
	free(sad);
	free(ex);
	free(ey);
	free(ec);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * K1 tests.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static void
test_me16_shift(void)
{
	enum { MB_W = 3, MB_H = 2, PAD = 16, W = 48, H = 32 };
	struct padded ref;
	uint8_t cur[W * H];
	int16_t mvx[MB_W * MB_H], mvy[MB_W * MB_H];
	uint32_t sad[MB_W * MB_H];
	n2vk_me16_args a = { 0 };

	start("me16 shifted reference (+5,-3), pad 16, range 8");
	CHECK(padded_make(&ref, W, H, PAD));
	lcg_seed(12345);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			cur[y * W + x] = lcg_byte();
	memset(ref.all, 0, (size_t)ref.stride * (H + 2 * PAD));
	/* ref(x, y) = cur(x - 5, y + 3): the block at (16mx + 5, 16my - 3) is the macroblock itself */
	for (int y = -PAD; y < H + PAD; y++)
		for (int x = -PAD; x < W + PAD; x++)
			if (x - 5 >= 0 && x - 5 < W && y + 3 >= 0 && y + 3 < H)
				*padded_at(&ref, x, y, PAD) = cur[(y + 3) * W + (x - 5)];

	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 8;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_OK);
	for (int i = 0; i < MB_W * MB_H; i++) {
		CHECK(mvx[i] == 5);
		CHECK(mvy[i] == -3);
		CHECK(sad[i] == 0);
	}
	/* the same answer through the dispatcher with no backend installed (the reference) */
	memset(mvx, 0x55, sizeof(mvx));
	CHECK(n2vk_me16(&a, mvx, mvy, sad) == N2VK_OK);
	for (int i = 0; i < MB_W * MB_H; i++) {
		CHECK(mvx[i] == 5 && mvy[i] == -3 && sad[i] == 0);
	}
	free(ref.all);
}

static void
make_random_picture(struct padded *ref, uint8_t **cur, uint32_t mb_w, uint32_t mb_h, uint32_t pad, uint32_t shift_x,
                    uint32_t shift_y)
{
	const uint32_t w = 16 * mb_w, h = 16 * mb_h;

	*cur = malloc((size_t)w * h);
	if (*cur == NULL || !padded_make(ref, w, h, pad))
		exit(2);
	lcg_seed(987654321u);
	for (uint32_t y = 0; y < h; y++)
		for (uint32_t x = 0; x < w; x++)
			(*cur)[y * w + x] = lcg_byte();
	memset(ref->all, 0, (size_t)ref->stride * (h + 2 * pad));
	for (int32_t y = -(int32_t)pad; y < (int32_t)h + (int32_t)pad; y++)
		for (int32_t x = -(int32_t)pad; x < (int32_t)w + (int32_t)pad; x++) {
			const int32_t sx = x - (int32_t)shift_x, sy = y - (int32_t)shift_y;

			if (sx >= 0 && sx < (int32_t)w && sy >= 0 && sy < (int32_t)h)
				*padded_at(ref, x, y, pad) = (*cur)[sy * w + sx];
		}
}

static void
test_me16_bruteforce_range2(void)
{
	enum { MB_W = 5, MB_H = 3, PAD = 16 };
	struct padded ref;
	uint8_t *cur;
	int16_t cx[MB_W * MB_H], cy[MB_W * MB_H];
	n2vk_me16_args a = { 0 };

	start("me16 equals an independent qsort search, random centres, pad 16, range 2");
	make_random_picture(&ref, &cur, MB_W, MB_H, PAD, 3, 1);
	lcg_seed(424242);
	for (int i = 0; i < MB_W * MB_H; i++) {
		cx[i] = (int16_t)((int)lcg_byte() - 128);
		cy[i] = (int16_t)((int)lcg_byte() - 128);
	}
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = 16 * MB_W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 2;
	a.centre_x = cx;
	a.centre_y = cy;
	compare_with_independent(&a, "random centres");
	/* no centres: the same search around (0, 0) */
	a.centre_x = a.centre_y = NULL;
	compare_with_independent(&a, "zero centres");
	free(cur);
	free(ref.all);
}

static void
test_me16_bruteforce_edge(void)
{
	enum { MB_W = 4, MB_H = 3, PAD = 0 };
	struct padded ref;
	uint8_t *cur;
	n2vk_me16_args a = { 0 };

	start("me16 equals an independent qsort search, pad 0 at every picture edge, range 8");
	make_random_picture(&ref, &cur, MB_W, MB_H, PAD, 1, 2);
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = 16 * MB_W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 8;
	compare_with_independent(&a, "pad 0");
	free(cur);
	free(ref.all);
}

static void
test_me16_all_equal(void)
{
	enum { MB_W = 4, MB_H = 3, PAD = 16, W = 16 * MB_W, H = 16 * MB_H };
	struct padded ref;
	uint8_t cur[W * H];
	int16_t mvx[MB_W * MB_H], mvy[MB_W * MB_H];
	uint32_t sad[MB_W * MB_H];
	n2vk_me16_args a = { 0 };

	start("me16 all-equal pictures: (0,0) everywhere, tie-break to the centre");
	CHECK(padded_make(&ref, W, H, PAD));
	memset(cur, 200, sizeof(cur));
	memset(ref.all, 200, (size_t)ref.stride * (H + 2 * PAD));
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 8;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_OK);
	for (int i = 0; i < MB_W * MB_H; i++) {
		CHECK(mvx[i] == 0);
		CHECK(mvy[i] == 0);
		CHECK(sad[i] == 0);
	}
	free(ref.all);
}

static void
test_me16_centre_clamped(void)
{
	enum { MB_W = 3, MB_H = 2, PAD = 0, W = 16 * MB_W, H = 16 * MB_H };
	uint8_t cur[W * H];
	int16_t cx[MB_W * MB_H], cy[MB_W * MB_H];
	int16_t mvx[MB_W * MB_H], mvy[MB_W * MB_H];
	uint32_t sad[MB_W * MB_H];
	n2vk_me16_args a = { 0 };

	start("me16 a centre outside the allowed rectangle is clamped");
	lcg_seed(31415);
	for (int i = 0; i < W * H; i++)
		cur[i] = lcg_byte();
	for (int i = 0; i < MB_W * MB_H; i++) {
		cx[i] = 1000;
		cy[i] = -1000;
	}
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = W;
	a.ref = cur;          /* pad 0: cur is the reference, and no sample outside it is read */
	a.ref_stride = W;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 32;
	a.centre_x = cx;
	a.centre_y = cy;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_OK);
	compare_with_independent(&a, "clamped centre");
}

static void
test_me16_pad0_corners(void)
{
	enum { MB_W = 3, MB_H = 2, W = 16 * MB_W, H = 16 * MB_H };
	uint8_t *cur = malloc((size_t)W * H);       /* exactly W*H bytes: ASan catches any read before/after */
	int16_t mvx[MB_W * MB_H], mvy[MB_W * MB_H];
	uint32_t sad[MB_W * MB_H];
	n2vk_me16_args a = { 0 };

	start("me16 pad 0 at the picture corners never reads outside (ASan)");
	CHECK(cur != NULL);
	lcg_seed(271828);
	for (int i = 0; i < W * H; i++)
		cur[i] = lcg_byte();
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = W;
	a.ref = cur;
	a.ref_stride = W;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = 0;
	a.range = 32;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_OK);
	/* every answer must keep its block inside the picture */
	for (uint32_t my = 0; my < MB_H; my++)
		for (uint32_t mx = 0; mx < MB_W; mx++) {
			const int32_t dx = mvx[my * MB_W + mx], dy = mvy[my * MB_W + mx];

			CHECK(16 * (int32_t)mx + dx >= 0);
			CHECK(16 * (int32_t)mx + dx + 16 <= W);
			CHECK(16 * (int32_t)my + dy >= 0);
			CHECK(16 * (int32_t)my + dy + 16 <= H);
		}
	free(cur);
}

static void
test_me16_arguments(void)
{
	uint8_t cur[16 * 16] = { 0 }, ref[16 * 16] = { 0 };
	int16_t mvx[1], mvy[1];
	uint32_t sad[1];
	n2vk_me16_args a = { 0 };

	start("me16 argument checks");
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = 16;
	a.ref = ref;
	a.ref_stride = 16;
	a.mb_w = 1;
	a.mb_h = 1;
	CHECK(n2vk_ref_me16(NULL, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_me16(&a, NULL, mvy, sad) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_me16(&a, mvx, NULL, sad) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_me16(&a, mvx, mvy, NULL) == N2VK_E_ARGUMENT);
	a.size = sizeof(a) - 1;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.size = sizeof(a);
	a.cur = NULL;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.cur = cur;
	a.mb_w = 0;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.mb_w = 257;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.mb_w = 1;
	a.mb_h = 0;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.mb_h = 145;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.mb_h = 1;
	a.pad = 65;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.pad = 0;
	a.range = 33;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.range = 0;
	a.cur_stride = 15;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
	a.cur_stride = 16;
	a.centre_x = mvx;
	a.centre_y = NULL;
	CHECK(n2vk_ref_me16(&a, mvx, mvy, sad) == N2VK_E_ARGUMENT);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * K2 tests.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static void
test_half_known(void)
{
	uint8_t a[4] = { 1, 2, 3, 4 }, b[4] = { 255, 255, 255, 254 }, out[1] = { 0 };

	start("half {1,2,3,4} -> 3 and {255,255,255,254} -> 255");
	CHECK(n2vk_ref_half(a, 2, out, 1, 1, 1) == N2VK_OK);
	CHECK(out[0] == 3);
	CHECK(n2vk_ref_half(b, 2, out, 1, 1, 1) == N2VK_OK);
	CHECK(out[0] == 255);
	CHECK(n2vk_half(a, 2, out, 1, 1, 1) == N2VK_OK);
	CHECK(out[0] == 3);
}

static void
test_half_random(void)
{
	enum { W = 37, H = 23, SS = 2 * W + 7, DS = W + 3 };
	uint8_t src[SS * 2 * H], dst[DS * H], expect[DS * H];

	start("half random 37x23, padded strides");
	lcg_seed(1618033);
	for (int i = 0; i < SS * 2 * H; i++)
		src[i] = lcg_byte();
	memset(dst, 0xcc, sizeof(dst));
	memset(expect, 0xcc, sizeof(expect));
	CHECK(n2vk_ref_half(src, SS, dst, DS, W, H) == N2VK_OK);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			const unsigned sum = src[(2 * y) * SS + 2 * x] + src[(2 * y) * SS + 2 * x + 1] +
			                     src[(2 * y + 1) * SS + 2 * x] + src[(2 * y + 1) * SS + 2 * x + 1];

			expect[y * DS + x] = (uint8_t)((sum + 2) >> 2);
		}
	CHECK(memcmp(dst, expect, sizeof(expect)) == 0);
}

static void
test_half_arguments(void)
{
	uint8_t a[64] = { 0 }, b[64] = { 0 };

	start("half argument checks");
	CHECK(n2vk_ref_half(NULL, 8, b, 4, 4, 4) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_half(a, 8, NULL, 4, 4, 4) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_half(a, 8, b, 4, 0, 4) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_half(a, 8, b, 4, 4, 0) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_half(a, 7, b, 4, 4, 4) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_half(a, 8, b, 3, 4, 4) == N2VK_E_ARGUMENT);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * K3 tests.  The literal table and the cosine-pattern expectation were produced with
 * Python integers from n2vk.h's formula (the snippet is in the comment below):
 *
 *   import math
 *   c = [[int(math.floor(8192*(math.sqrt(1/8) if u==0 else 0.5*math.cos((2*x+1)*u*math.pi/16))+0.5))
 *         if (8192*(math.sqrt(1/8) if u==0 else 0.5*math.cos((2*x+1)*u*math.pi/16))) >= 0
 *         else -int(math.floor(-8192*(math.sqrt(1/8) if u==0 else 0.5*math.cos((2*x+1)*u*math.pi/16))+0.5))
 *         for x in range(8)] for u in range(8)]
 *   f = [int(round(100*math.cos((2*x+1)*math.pi/16))) + 128 - 128 for x in range(8)]   # level shift
 *   t = [[(sum(c[u][x]*f[x] for x in range(8))+512) >> 10 for u in range(8)] for y in range(8)]
 *   F = [[(sum(c[v][y]*t[y][u] for y in range(8))+32768) >> 16 for u in range(8)] for v in range(8)]
 *   q = lambda f,Q: (-1 if f < 0 else 1) * ((abs(f)+(Q>>1))//Q)
 *
 * (The plan's "F[0][1] about 400" is 566 with the header's own constants: 8192 * 0.5 *
 * sum(100 cos^2) = 1 639 600 in pass 1, 566 in pass 2.  The literal table is the formula,
 * not the prose.)
 * ----------------------------------------------------------------------------------------------------------------------------- */

static const int32_t literal_c[8][8] = {
	{  2896,  2896,  2896,  2896,  2896,  2896,  2896,  2896 },
	{  4017,  3406,  2276,   799,  -799, -2276, -3406, -4017 },
	{  3784,  1567, -1567, -3784, -3784, -1567,  1567,  3784 },
	{  3406,  -799, -4017, -2276,  2276,  4017,   799, -3406 },
	{  2896, -2896, -2896,  2896,  2896, -2896, -2896,  2896 },
	{  2276, -4017,   799,  3406, -3406,  -799,  4017, -2276 },
	{  1567, -3784,  3784, -1567, -1567,  3784, -3784,  1567 },
	{   799, -2276,  3406, -4017,  4017, -3406,  2276,  -799 },
};

static const int16_t cosine_expected[64] = {
	    0,   189,     0,    -1,     0,     1,     0,     0,
	    0,     0,     0,     0,     0,     0,     0,     0,
	    0,     0,     0,     0,     0,     0,     0,     0,
	    0,     0,     0,     0,     0,     0,     0,     0,
	    0,     0,     0,     0,     0,     0,     0,     0,
	    0,     0,     0,     0,     0,     0,     0,     0,
	    0,     0,     0,     0,     0,     0,     0,     0,
	    0,     0,     0,     0,     0,     0,     0,     0,
};

static int32_t
literal_quant(int32_t f, uint16_t q)
{
	const int32_t magnitude = f < 0 ? -f : f;
	const int32_t rounded = (magnitude + (int32_t)(q >> 1)) / (int32_t)q;

	return f < 0 ? -rounded : rounded;
}

/* The independent transform: literal table, int64 accumulators, its own loops. */
static void
independent_dct(const uint8_t *src, uint32_t stride, uint32_t width, uint32_t height, const uint16_t qtable[64],
                int16_t *coef)
{
	int16_t *out = coef;

	for (uint32_t by = 0; by < height; by += 8) {
		for (uint32_t bx = 0; bx < width; bx += 8, out += 64) {
			int64_t t[8][8];

			for (int y = 0; y < 8; y++)
				for (int u = 0; u < 8; u++) {
					int64_t sum = 0;

					for (int x = 0; x < 8; x++)
						sum += (int64_t)literal_c[u][x] * ((int32_t)src[(by + (uint32_t)y) * stride + bx + (uint32_t)x] - 128);
					t[y][u] = (sum + 512) >> 10;
				}
			for (int v = 0; v < 8; v++)
				for (int u = 0; u < 8; u++) {
					int64_t sum = 0;

					for (int y = 0; y < 8; y++)
						sum += (int64_t)literal_c[v][y] * t[y][u];
					const int32_t f = (int32_t)((sum + 32768) >> 16);

					out[v * 8 + u] = (int16_t)literal_quant(f, qtable[v * 8 + u]);
				}
		}
	}
}

extern int32_t n2vk_ref_quant(int32_t f, uint16_t q);   /* n2vk_ref.c, not part of n2vk.h */

static void
test_dct_table_literal(void)
{
	start("DCT table equals the literal lround() table");

	for (int u = 0; u < 8; u++)
		for (int x = 0; x < 8; x++) {
			const double c = 8192.0 * (u == 0 ? sqrt(1.0 / 8.0)
			                                : 0.5 * cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0));
			const int32_t want = (int32_t)lround(c);

			CHECK(want == literal_c[u][x]);
		}
}

static void
test_dct_constant_positive(void)
{
	uint8_t block[64];
	uint16_t q[64];
	int16_t out[64];

	start("fdct constant 144, Q all 1: DC 128, AC 0");
	memset(block, 144, sizeof(block));
	for (int i = 0; i < 64; i++)
		q[i] = 1;
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 8, q, out) == N2VK_OK);
	CHECK(out[0] == 128);
	for (int i = 1; i < 64; i++)
		CHECK(out[i] == 0);
}

static void
test_dct_constant_zero(void)
{
	uint8_t block[64];
	uint16_t q[64];
	int16_t out[64];

	start("fdct constant 0, Q all 1: DC -1024");
	memset(block, 0, sizeof(block));
	for (int i = 0; i < 64; i++)
		q[i] = 1;
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 8, q, out) == N2VK_OK);
	CHECK(out[0] == -1024);
	for (int i = 1; i < 64; i++)
		CHECK(out[i] == 0);
}

static void
test_dct_cosine_pattern(void)
{
	uint8_t block[64];
	uint16_t q[64];
	int16_t out[64];
	int16_t expect[64];

	start("fdct horizontal cosine pattern, Q all 3: literal 64 coefficients");
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
			block[y * 8 + x] = (uint8_t)lround(100.0 * cos((2.0 * x + 1.0) * 3.14159265358979323846 / 16.0)) + 128;
	for (int i = 0; i < 64; i++)
		q[i] = 3;
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 8, q, out) == N2VK_OK);
	CHECK(memcmp(out, cosine_expected, sizeof(out)) == 0);
	independent_dct(block, 8, 8, 8, q, expect);
	CHECK(memcmp(out, expect, sizeof(out)) == 0);
}

static void
test_dct_random(void)
{
	enum { W = 32, H = 24, SS = W + 5 };
	uint8_t src[SS * H];
	uint16_t q[64];
	int16_t out[(W / 8) * (H / 8) * 64], expect[(W / 8) * (H / 8) * 64];

	start("fdct random 32x24 against the literal-table independent transform");
	lcg_seed(112358);
	for (int i = 0; i < SS * H; i++)
		src[i] = lcg_byte();
	lcg_seed(132134);
	for (int i = 0; i < 64; i++)
		q[i] = (uint16_t)(1 + lcg_byte() % 31);
	CHECK(n2vk_ref_fdct8_quant(src, SS, W, H, q, out) == N2VK_OK);
	independent_dct(src, SS, W, H, q, expect);
	CHECK(memcmp(out, expect, sizeof(out)) == 0);
}

static void
test_dct_quant_ties(void)
{
	start("fdct quantiser: -4 -> -1, -5 -> -2, 4 -> 1, 5 -> 2 at Q 3");
	CHECK(n2vk_ref_quant(-5, 3) == -2);
	CHECK(n2vk_ref_quant(-4, 3) == -1);
	CHECK(n2vk_ref_quant(4, 3) == 1);
	CHECK(n2vk_ref_quant(5, 3) == 2);
	CHECK(n2vk_ref_quant(-6, 3) == -2);
	CHECK(n2vk_ref_quant(6, 3) == 2);
	CHECK(n2vk_ref_quant(0, 3) == 0);
}

static void
test_dct_arguments(void)
{
	uint8_t block[64] = { 0 };
	uint16_t q[64];
	int16_t out[64];

	start("fdct argument checks");
	for (int i = 0; i < 64; i++)
		q[i] = 1;
	CHECK(n2vk_ref_fdct8_quant(NULL, 8, 8, 8, q, out) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 8, NULL, out) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 8, q, NULL) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_fdct8_quant(block, 8, 12, 8, q, out) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 10, q, out) == N2VK_E_ARGUMENT);
	CHECK(n2vk_ref_fdct8_quant(block, 7, 8, 8, q, out) == N2VK_E_ARGUMENT);
	q[17] = 0;
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 8, q, out) == N2VK_E_ARGUMENT);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Dispatch tests.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static atomic_int backend_calls;
static atomic_int me16_calls;

static int
failing_me16(void *ctx, const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad)
{
	(void)ctx;
	(void)a;
	(void)mvx;
	(void)mvy;
	(void)sad;
	atomic_fetch_add(&me16_calls, 1);
	return N2VK_E_GPU;
}

static int
failing_half(void *ctx, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t w, uint32_t h)
{
	(void)ctx;
	(void)src;
	(void)src_stride;
	(void)dst;
	(void)dst_stride;
	(void)w;
	(void)h;
	atomic_fetch_add(&backend_calls, 1);
	return N2VK_E_GPU;
}

static int
good_half(void *ctx, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t w, uint32_t h)
{
	(void)ctx;
	atomic_fetch_add(&backend_calls, 1);
	return n2vk_ref_half(src, src_stride, dst, dst_stride, w, h);
}

static int
unsupported_half(void *ctx, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t w, uint32_t h)
{
	(void)ctx;
	(void)src;
	(void)src_stride;
	(void)dst;
	(void)dst_stride;
	(void)w;
	(void)h;
	atomic_fetch_add(&backend_calls, 1);
	return N2VK_E_UNSUPPORTED;
}

static void
test_dispatch_three_strikes(void)
{
	uint8_t a[4] = { 1, 2, 3, 4 }, out[1];
	const n2vk_backend bad = { .size = sizeof(bad), .name = "bad", .half = failing_half, .ctx = NULL };
	const n2vk_backend good = { .size = sizeof(good), .name = "good", .half = good_half, .ctx = NULL };

	start("dispatch: three E_GPU in a row then the reference, fourth call not made");
	n2vk_use(&bad);
	atomic_store(&backend_calls, 0);
	for (int i = 0; i < 3; i++) {
		out[0] = 0;
		CHECK(n2vk_half(a, 2, out, 1, 1, 1) == N2VK_OK);
		CHECK(out[0] == 3);
	}
	CHECK(atomic_load(&backend_calls) == 3);
	out[0] = 0;
	CHECK(n2vk_half(a, 2, out, 1, 1, 1) == N2VK_OK);
	CHECK(out[0] == 3);
	CHECK(atomic_load(&backend_calls) == 3);       /* the backend was not asked again */
	/* n2vk_use resets the strikes */
	n2vk_use(&good);
	CHECK(n2vk_half(a, 2, out, 1, 1, 1) == N2VK_OK);
	CHECK(atomic_load(&backend_calls) == 4);
	n2vk_use(&bad);
	CHECK(n2vk_half(a, 2, out, 1, 1, 1) == N2VK_OK);
	CHECK(atomic_load(&backend_calls) == 5);
	n2vk_use(NULL);
	CHECK(n2vk_backend_name() != NULL && strcmp(n2vk_backend_name(), "reference") == 0);
}

static void
test_dispatch_per_kernel(void)
{
	uint8_t a[4] = { 1, 2, 3, 4 }, out[1];
	uint8_t cur[16 * 16] = { 0 };
	int16_t mvx[1], mvy[1];
	uint32_t sad[1];
	uint16_t q[64];
	int16_t coef[64];
	uint8_t src[64] = { 0 };
	const n2vk_backend bad = { .size = sizeof(bad), .name = "bad", .half = failing_half, .me16 = failing_me16, .ctx = NULL };
	n2vk_me16_args m = { 0 };

	start("dispatch: strikes are per kernel (me16 and half disable independently)");
	n2vk_use(&bad);
	atomic_store(&backend_calls, 0);
	atomic_store(&me16_calls, 0);
	for (int i = 0; i < 5; i++)
		CHECK(n2vk_half(a, 2, out, 1, 1, 1) == N2VK_OK);
	CHECK(atomic_load(&backend_calls) == 3);
	m.size = sizeof(m);
	m.cur = cur;
	m.cur_stride = 16;
	m.ref = cur;
	m.ref_stride = 16;
	m.mb_w = 1;
	m.mb_h = 1;
	m.pad = 0;
	m.range = 0;
	for (int i = 0; i < 5; i++)
		CHECK(n2vk_me16(&m, mvx, mvy, sad) == N2VK_OK);
	CHECK(atomic_load(&me16_calls) == 3);       /* half's three did not disable me16 */
	CHECK(mvx[0] == 0 && mvy[0] == 0 && sad[0] == 0);
	for (int i = 0; i < 64; i++)
		q[i] = 1;
	CHECK(n2vk_fdct8_quant(src, 8, 8, 8, q, coef) == N2VK_OK);   /* no fdct member: reference */
	CHECK(n2vk_fdct8_quant(src, 8, 8, 8, q, coef) == N2VK_OK);
	n2vk_use(NULL);
}

static void
test_dispatch_unsupported(void)
{
	uint8_t a[4] = { 1, 2, 3, 4 }, out[1];
	const n2vk_backend uns = { .size = sizeof(uns), .name = "uns", .half = unsupported_half, .ctx = NULL };

	start("dispatch: E_UNSUPPORTED always falls back and never strikes");
	n2vk_use(&uns);
	atomic_store(&backend_calls, 0);
	for (int i = 0; i < 10; i++) {
		out[0] = 0;
		CHECK(n2vk_half(a, 2, out, 1, 1, 1) == N2VK_OK);
		CHECK(out[0] == 3);
	}
	CHECK(atomic_load(&backend_calls) == 10);
	n2vk_use(NULL);
}

static void
test_dispatch_name(void)
{
	const n2vk_backend named = { .size = sizeof(named), .name = "a-backend" };

	start("dispatch: n2vk_backend_name and n2vk_use(NULL)");
	n2vk_use(NULL);
	CHECK(strcmp(n2vk_backend_name(), "reference") == 0);
	n2vk_use(&named);
	CHECK(strcmp(n2vk_backend_name(), "a-backend") == 0);
	n2vk_use(NULL);
	CHECK(strcmp(n2vk_backend_name(), "reference") == 0);
}

/* ---- threads: two callers, one switcher ---------------------------------------------------------------- */

static uint8_t thread_src[64 * 48], thread_expect[32 * 24];
static atomic_int thread_failures;

static int
toggle_half(void *ctx, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t w, uint32_t h)
{
	(void)ctx;
	atomic_fetch_add(&backend_calls, 1);
	/* valid work when it succeeds; every other call says E_GPU, so both routes are taken */
	if ((atomic_load(&backend_calls) & 1u) != 0)
		return N2VK_E_GPU;
	return n2vk_ref_half(src, src_stride, dst, dst_stride, w, h);
}

static void *
caller_thread(void *arg)
{
	const int iterations = *(const int *)arg;

	for (int i = 0; i < iterations; i++) {
		uint8_t out[32 * 24];

		memset(out, 0xcc, sizeof(out));
		if (n2vk_half(thread_src, 64, out, 32, 32, 24) != N2VK_OK || memcmp(out, thread_expect, sizeof(out)) != 0)
			atomic_fetch_add(&thread_failures, 1);
	}
	return NULL;
}

static void *
switch_thread(void *arg)
{
	const int iterations = *(const int *)arg;
	const n2vk_backend a = { .size = sizeof(a), .name = "toggle", .half = toggle_half, .ctx = NULL };
	const n2vk_backend b = { .size = sizeof(b), .name = "good", .half = good_half, .ctx = NULL };

	for (int i = 0; i < iterations; i++) {
		switch (i % 3) {
		case 0: n2vk_use(&a); break;
		case 1: n2vk_use(&b); break;
		default: n2vk_use(NULL); break;
		}
	}
	n2vk_use(NULL);
	return NULL;
}

static void
test_dispatch_threads(int iterations)
{
	pthread_t c1, c2, sw;

	start("dispatch: two callers and one switcher, TSan");
	lcg_seed(777);
	for (int i = 0; i < (int)sizeof(thread_src); i++)
		thread_src[i] = lcg_byte();
	CHECK(n2vk_ref_half(thread_src, 64, thread_expect, 32, 32, 24) == N2VK_OK);
	atomic_store(&thread_failures, 0);
	atomic_store(&backend_calls, 0);
	n2vk_use(NULL);
	CHECK(pthread_create(&c1, NULL, caller_thread, &iterations) == 0);
	CHECK(pthread_create(&c2, NULL, caller_thread, &iterations) == 0);
	CHECK(pthread_create(&sw, NULL, switch_thread, &iterations) == 0);
	pthread_join(c1, NULL);
	pthread_join(c2, NULL);
	pthread_join(sw, NULL);
	CHECK(atomic_load(&thread_failures) == 0);
	CHECK(strcmp(n2vk_backend_name(), "reference") == 0);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------------------------------------------------------------- */

int
main(int argc, char **argv)
{
	int only_threads = getenv("N2VK_ONLY_THREADS") != NULL;
	int iterations = 2000;

	if (argc > 1)
		iterations = atoi(argv[1]);
	if (only_threads) {
		test_dispatch_threads(iterations > 0 ? iterations : 2000);
	} else {
		test_me16_shift();
		test_me16_bruteforce_range2();
		test_me16_bruteforce_edge();
		test_me16_all_equal();
		test_me16_centre_clamped();
		test_me16_pad0_corners();
		test_me16_arguments();
		test_half_known();
		test_half_random();
		test_half_arguments();
		test_dct_table_literal();
		test_dct_constant_positive();
		test_dct_constant_zero();
		test_dct_cosine_pattern();
		test_dct_random();
		test_dct_quant_ties();
		test_dct_arguments();
		test_dispatch_three_strikes();
		test_dispatch_per_kernel();
		test_dispatch_unsupported();
		test_dispatch_name();
		test_dispatch_threads(iterations);
	}

	if (tests_failed != 0) {
		printf("N2TEST WP08-CORE %s FAIL %d/%d\n", only_threads ? "TSAN" : "ASAN", tests_run - tests_failed, tests_run);
		return 1;
	}
	printf("N2TEST WP08-CORE %s PASS %d/%d\n", only_threads ? "TSAN" : "ASAN", tests_run, tests_run);
	return 0;
}
