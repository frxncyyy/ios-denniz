/* SPDX-License-Identifier: MIT */
/*
 * t_deblock.c -- host unit tests of avc2/n2avc_deblock.c (H.264 clause 8.7).
 *
 * Every expected sample value below was computed by hand from the Recommendation's equations and tables (the arithmetic
 * is written out in the comment of each case).  The test does not reimplement the filter: it builds a tiny picture and
 * mbinfo array, calls n2avc_deblock_picture() and compares chosen samples.  The full bit-exactness test against a real
 * decoder is run by run_tests.sh, not here.
 *
 * Covered:
 *   A  1x1 macroblock, inter, identical vectors/no coefficients   -> bS 0 everywhere, picture unchanged (picture edges
 *                                                                  are never filtered)
 *   B  2x1, P16 vs P16 with |mv| difference 4                     -> bS 1 luma + chroma bS inherited from luma
 *   C  2x1, P16 vs P16 same vector, one coded luma block          -> bS 2 from coefficients, per 4x4 block and per
 *                                                                  chroma position (block row 0 only)
 *   D  2x1, I16 next to P16                                       -> bS 4, strong luma filter + chroma weak filter
 *   E  1x1 I16, an internal edge                                  -> bS 3
 *   F  2x1, mv difference but |p0-q0| >= alpha                    -> filterSamplesFlag 0, unchanged
 *   G  1x2, horizontal macroblock edge with mv difference         -> bS 1 along a horizontal edge
 *   H  2x1, I16 next to P16 with |p0-q0| >= (alpha>>2)+2          -> bS 4, weak branch of 8.7.2.4
 *   I  robustness: odd macroblock counts and random content under ASan/UBSan, and plane pointers NULL
 */
#include "n2avc_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The function under test.  It lives in avc2/n2avc_deblock.c; include/n2avc_int.h is frozen and lead-owned, so the test
 * declares it locally.  The lead's integration adds the same declaration next to the call in avc2/n2avc_core.c (or to the
 * frozen header). */
void n2avc_deblock_picture(n2avc_frame *rec, const n2avc_mbinfo *mb, int mb_w, int mb_h);

static int checks, failures;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

struct pic {
	uint8_t y[96 * 64];
	uint8_t cb[48 * 32];
	uint8_t cr[48 * 32];
	int w, h;
	n2avc_frame f;
};

static void pic_init(struct pic *p, int w, int h)
{
	memset(p, 0, sizeof *p);
	p->w = w;
	p->h = h;
	p->f.y.p = p->y;  p->f.y.stride = w;
	p->f.cb.p = p->cb; p->f.cb.stride = w / 2;
	p->f.cr.p = p->cr; p->f.cr.stride = w / 2;
}

static void fill_all(struct pic *p, int v)
{
	memset(p->y, v, (size_t)p->w * p->h);
	memset(p->cb, v, (size_t)(p->w / 2) * (p->h / 2));
	memset(p->cr, v, (size_t)(p->w / 2) * (p->h / 2));
}

static void set_col(struct pic *p, int comp, int x, int v)
{
	uint8_t *pl = comp == 0 ? p->y : comp == 1 ? p->cb : p->cr;
	int stride = comp == 0 ? p->w : p->w / 2;
	int h = comp == 0 ? p->h : p->h / 2;
	int y;

	for (y = 0; y < h; y++)
		pl[(size_t)y * stride + x] = (uint8_t)v;
}

static void set_px(struct pic *p, int comp, int x, int y, int v)
{
	uint8_t *pl = comp == 0 ? p->y : comp == 1 ? p->cb : p->cr;
	int stride = comp == 0 ? p->w : p->w / 2;

	pl[(size_t)y * stride + x] = (uint8_t)v;
}

static int get_px(const struct pic *p, int comp, int x, int y)
{
	const uint8_t *pl = comp == 0 ? p->y : comp == 1 ? p->cb : p->cr;
	int stride = comp == 0 ? p->w : p->w / 2;

	return pl[(size_t)y * stride + x];
}

static void mb_reset(n2avc_mbinfo *m, int type, int ref, int mvx, int mvy, int qp)
{
	memset(m, 0, sizeof *m);
	m->type = (uint8_t)type;
	m->ref = (int8_t)ref;
	m->mvx = (int16_t)mvx;
	m->mvy = (int16_t)mvy;
	m->qp = (uint8_t)qp;
}

static void expect_col(const struct pic *p, int comp, int x, int v, const char *what)
{
	int stride = comp == 0 ? p->w : p->w / 2;
	int h = comp == 0 ? p->h : p->h / 2;
	const uint8_t *pl = comp == 0 ? p->y : comp == 1 ? p->cb : p->cr;
	int y, bad = -1;

	for (y = 0; y < h; y++)
		if (pl[(size_t)y * stride + x] != v) {
			bad = y;
			break;
		}
	CHECK(bad < 0, "%s: column %d component %d: first bad row %d (%d, expected %d)",
	      what, x, comp, bad, bad < 0 ? -1 : pl[(size_t)bad * stride + x], v);
}

static void expect_row(const struct pic *p, int comp, int y, int v, const char *what)
{
	int stride = comp == 0 ? p->w : p->w / 2;
	int w = comp == 0 ? p->w : p->w / 2;
	const uint8_t *pl = comp == 0 ? p->y : comp == 1 ? p->cb : p->cr;
	int x, bad = -1;

	for (x = 0; x < w; x++)
		if (pl[(size_t)y * stride + x] != v) {
			bad = x;
			break;
		}
	CHECK(bad < 0, "%s: row %d component %d: first bad column %d (%d, expected %d)",
	      what, y, comp, bad, bad < 0 ? -1 : pl[(size_t)bad * stride + x], v);
}

/* A: no filtering at all. */
static void case_a_no_filter(void)
{
	struct pic p;
	n2avc_mbinfo mb[1];

	pic_init(&p, 16, 16);
	fill_all(&p, 137);
	mb_reset(&mb[0], N2MB_P16, 0, 0, 0, 26);
	n2avc_deblock_picture(&p.f, mb, 1, 1);
	expect_col(&p, 0, 7, 137, "A luma");
	expect_col(&p, 1, 3, 137, "A cb");
	expect_col(&p, 2, 3, 137, "A cr");
}

/*
 * B: vertical macroblock edge, bS = 1.
 * Both macroblocks P16, QP 26, p side (100) against q side (110), q vector 4 quarter samples to the right.
 * qPav = 26, indexA = indexB = 26: alpha = 15, beta = 6, t'C0(bS=1, 26) = 1.
 * |p0-q0| = 10 < 15, ap = aq = 0 < 6  ->  tC = 1 + 1 + 1 = 3.
 * delta = Clip3(-3, 3, ((10 << 2) + (100 - 110) + 4) >> 3) = Clip3(-3, 3, 34 >> 3) = 3
 * p0' = 103, q0' = 107.
 * p1' = 100 + Clip3(-1, 1, (100 + ((100+110+1)>>1) - 200) >> 1) = 100 + Clip3(-1,1,2) = 101
 * q1' = 110 + Clip3(-1, 1, (110 + 105 - 220) >> 1) = 110 - 1 = 109
 * Chroma inherits bS = 1: tC = t'C0 + 1 = 2, delta = Clip3(-2,2,4) = 2 -> p0' 102, q0' 108, p1/q1 unchanged.
 */
static void case_b_bs1(void)
{
	struct pic p;
	n2avc_mbinfo mb[2];

	pic_init(&p, 32, 16);
	fill_all(&p, 100);
	{
		int x, y;
		for (y = 0; y < 16; y++)
			for (x = 16; x < 32; x++)
				set_px(&p, 0, x, y, 110);
		for (y = 0; y < 8; y++)
			for (x = 8; x < 16; x++) {
				set_px(&p, 1, x, y, 110);
				set_px(&p, 2, x, y, 110);
			}
	}
	mb_reset(&mb[0], N2MB_P16, 0, 0, 0, 26);
	mb_reset(&mb[1], N2MB_P16, 0, 4, 0, 26);

	n2avc_deblock_picture(&p.f, mb, 2, 1);

	expect_col(&p, 0, 13, 100, "B p2 untouched");
	expect_col(&p, 0, 14, 101, "B p1");
	expect_col(&p, 0, 15, 103, "B p0");
	expect_col(&p, 0, 16, 107, "B q0");
	expect_col(&p, 0, 17, 109, "B q1");
	expect_col(&p, 0, 18, 110, "B q2 untouched");
	expect_col(&p, 0, 19, 110, "B q3 untouched");
	expect_col(&p, 0, 12, 100, "B internal p side untouched");
	expect_col(&p, 0, 20, 110, "B internal q side untouched");
	expect_col(&p, 1, 7, 102, "B cb p0");
	expect_col(&p, 1, 8, 108, "B cb q0");
	expect_col(&p, 1, 6, 100, "B cb p1 unchanged");
	expect_col(&p, 1, 9, 110, "B cb q1 unchanged");
	expect_col(&p, 2, 7, 102, "B cr p0");
	expect_col(&p, 2, 8, 108, "B cr q0");
}

/*
 * C: bS = 2 from a coded luma 4x4 block, QP 40, same vector.
 * The right macroblock has nnz[0] = 1 (block x4=0, y4=0) and nothing else.  At the macroblock edge the q-side block rows
 * 1..3 have no coefficients -> bS 0; only rows 0..3 use bS 2.
 * indexA = indexB = 40: alpha = 80, beta = 13, t'C0(bS=2, 40) = 5, ap = aq = 0 -> tC = 7.
 * delta = Clip3(-7,7,((30<<2) + 0 + 4)>>3) = Clip3(-7,7,15) = 7 -> p0' 107, q0' 123.
 * p1' = 100 + Clip3(-5,5,(100 + ((100+130+1)>>1) - 200)>>1) = 100 + 5 = 105
 * q1' = 130 + Clip3(-5,5,(130 + 115 - 260)>>1) = 130 - 5 = 125
 * Chroma: QPC(40) = 36, alpha = 50, beta = 10, t'C0(bS=2,36) = 3; chroma tC = 3 + 1 = 4, delta = 4.
 * Only chroma rows 0 and 1 map to luma block row 0 (rows 2..7 map to blocks without coefficients -> bS 0).
 */
static void case_c_bs2(void)
{
	struct pic p;
	n2avc_mbinfo mb[2];
	int x, y;

	pic_init(&p, 32, 16);
	fill_all(&p, 100);
	for (y = 0; y < 16; y++)
		for (x = 16; x < 32; x++)
			set_px(&p, 0, x, y, 130);
	for (x = 16; x < 32; x++)
		set_px(&p, 0, x, 5, 200);   /* stops the horizontal edge at y=4 from re-filtering rows 2..4 */
	for (y = 0; y < 8; y++)
		for (x = 8; x < 16; x++) {
			set_px(&p, 1, x, y, 130);
			set_px(&p, 2, x, y, 130);
		}
	mb_reset(&mb[0], N2MB_P16, 0, 0, 0, 40);
	mb_reset(&mb[1], N2MB_P16, 0, 0, 0, 40);
	mb[1].nnz[0] = 1;

	n2avc_deblock_picture(&p.f, mb, 2, 1);

	for (y = 0; y < 4; y++) {
		CHECK(get_px(&p, 0, 14, y) == 105, "C luma p1 row %d = %d", y, get_px(&p, 0, 14, y));
		CHECK(get_px(&p, 0, 15, y) == 107, "C luma p0 row %d = %d", y, get_px(&p, 0, 15, y));
		CHECK(get_px(&p, 0, 16, y) == 123, "C luma q0 row %d = %d", y, get_px(&p, 0, 16, y));
		CHECK(get_px(&p, 0, 17, y) == 125, "C luma q1 row %d = %d", y, get_px(&p, 0, 17, y));
	}
	for (y = 4; y < 16; y++) {
		CHECK(get_px(&p, 0, 15, y) == 100, "C luma p0 row %d unchanged", y);
		if (y != 5)
			CHECK(get_px(&p, 0, 16, y) == 130, "C luma q0 row %d unchanged", y);
	}
	for (y = 0; y < 2; y++) {
		CHECK(get_px(&p, 1, 7, y) == 104, "C cb p0 row %d = %d", y, get_px(&p, 1, 7, y));
		CHECK(get_px(&p, 1, 8, y) == 126, "C cb q0 row %d = %d", y, get_px(&p, 1, 8, y));
		CHECK(get_px(&p, 2, 7, y) == 104, "C cr p0 row %d", y);
		CHECK(get_px(&p, 2, 8, y) == 126, "C cr q0 row %d", y);
	}
	for (y = 2; y < 8; y++) {
		CHECK(get_px(&p, 1, 8, y) == 130, "C cb q0 row %d unchanged", y);
		CHECK(get_px(&p, 2, 8, y) == 130, "C cr q0 row %d unchanged", y);
	}
}

/*
 * D: bS = 4, strong luma filter.  Left I16 (flat 100), right P16 with q0=104, q1=100, q2=q3=104.
 * QP 26: alpha = 15, beta = 6.  ap = 0 < 6, aq = |104-104| = 0 < 6, |p0-q0| = 4 < (15>>2)+2 = 5 -> strong on both sides.
 * p0' = (p2 + 2p1 + 2p0 + 2q0 + q1 + 4) >> 3 = (100+200+200+208+100+4)>>3 = 812>>3 = 101
 * p1' = (p2 + p1 + p0 + q0 + 2) >> 2 = 406>>2 = 101
 * p2' = (2p3 + 3p2 + p1 + p0 + q0 + 4) >> 3 = 808>>3 = 101
 * q0' = (p1 + 2p0 + 2q0 + 2q1 + q2 + 4) >> 3 = 816>>3 = 102
 * q1' = (p0 + q0 + q1 + q2 + 2) >> 2 = 410>>2 = 102
 * q2' = (2q3 + 3q2 + q1 + q0 + p0 + 4) >> 3 = 828>>3 = 103
 * Chroma: left 100, right q0=104, q1=100, q2=104; bS inherited 4; chroma weak filter
 * p0' = (2*100 + 100 + 100 + 2) >> 2 = 100, q0' = (2*100 + 104 + 100 + 2) >> 2 = 101.
 */
static void case_d_bs4_strong(void)
{
	struct pic p;
	n2avc_mbinfo mb[2];
	int y;

	pic_init(&p, 32, 16);
	fill_all(&p, 100);
	for (y = 0; y < 16; y++) {
		int x;
		for (x = 16; x < 32; x++)
			set_px(&p, 0, x, y, 104);
		set_px(&p, 0, 17, y, 100);
	}
	for (y = 0; y < 8; y++) {
		int x;
		for (x = 8; x < 16; x++) {
			set_px(&p, 1, x, y, 104);
			set_px(&p, 2, x, y, 104);
		}
		set_px(&p, 1, 9, y, 100);
		set_px(&p, 2, 9, y, 100);
	}
	mb_reset(&mb[0], N2MB_I16, -1, 0, 0, 26);
	mb_reset(&mb[1], N2MB_P16, 0, 0, 0, 26);

	n2avc_deblock_picture(&p.f, mb, 2, 1);

	expect_col(&p, 0, 12, 100, "D p3 untouched");
	expect_col(&p, 0, 13, 101, "D p2");
	expect_col(&p, 0, 14, 101, "D p1");
	expect_col(&p, 0, 15, 101, "D p0");
	expect_col(&p, 0, 16, 102, "D q0");
	expect_col(&p, 0, 17, 102, "D q1");
	expect_col(&p, 0, 18, 103, "D q2");
	expect_col(&p, 0, 19, 104, "D q3 untouched");
	expect_col(&p, 0, 11, 100, "D left internal edge unchanged");
	expect_col(&p, 0, 20, 104, "D right internal edge unchanged");
	expect_col(&p, 1, 7, 100, "D cb p0 unchanged");
	expect_col(&p, 1, 8, 101, "D cb q0");
	expect_col(&p, 1, 9, 100, "D cb q1 unchanged");
	expect_col(&p, 2, 7, 100, "D cr p0 unchanged");
	expect_col(&p, 2, 8, 101, "D cr q0");
}

/*
 * E: bS = 3 at an internal edge of an intra macroblock, QP 26.
 * p2 = 100, p1 = 104, p0 = 108, q0 = 112, q1 = 116, q2 = 120.
 * |p0-q0| = 4 < 15, |p1-p0| = 4 < 6, |q1-q0| = 4 < 6.
 * ap = 8 >= 6, aq = 8 >= 6 -> tC = t'C0(bS=3,26) = 1.
 * delta = Clip3(-1,1,((4<<2) + (104-116) + 4)>>3) = Clip3(-1,1,1) = 1 -> p0' 109, q0' 111; p1/q1/p2/q2 unchanged.
 */
static void case_e_bs3(void)
{
	struct pic p;
	n2avc_mbinfo mb[1];

	pic_init(&p, 16, 16);
	fill_all(&p, 120);
	set_col(&p, 0, 0, 100);
	set_col(&p, 0, 1, 100);   /* p2 */
	set_col(&p, 0, 2, 104);   /* p1 */
	set_col(&p, 0, 3, 108);   /* p0 */
	set_col(&p, 0, 4, 112);   /* q0 */
	set_col(&p, 0, 5, 116);   /* q1 */
	set_col(&p, 0, 6, 120);   /* q2 */
	mb_reset(&mb[0], N2MB_I16, -1, 0, 0, 26);

	n2avc_deblock_picture(&p.f, mb, 1, 1);

	expect_col(&p, 0, 2, 104, "E p1 unchanged (ap >= beta)");
	expect_col(&p, 0, 3, 109, "E p0");
	expect_col(&p, 0, 4, 111, "E q0");
	expect_col(&p, 0, 5, 116, "E q1 unchanged (aq >= beta)");
	expect_col(&p, 0, 6, 119, "E q2 becomes p1 of the x=8 edge");
}

/* F: bS = 1 is computed but filterSamplesFlag is 0 because |p0-q0| = 20 >= alpha = 15. */
static void case_f_no_filter_samples(void)
{
	struct pic p;
	n2avc_mbinfo mb[2];
	int x, y;

	pic_init(&p, 32, 16);
	fill_all(&p, 100);
	for (y = 0; y < 16; y++)
		for (x = 16; x < 32; x++)
			set_px(&p, 0, x, y, 120);
	for (y = 0; y < 8; y++)
		for (x = 8; x < 16; x++) {
			set_px(&p, 1, x, y, 120);
			set_px(&p, 2, x, y, 120);
		}
	mb_reset(&mb[0], N2MB_P16, 0, 0, 0, 26);
	mb_reset(&mb[1], N2MB_P16, 0, 4, 0, 26);

	n2avc_deblock_picture(&p.f, mb, 2, 1);

	expect_col(&p, 0, 15, 100, "F p0 unchanged");
	expect_col(&p, 0, 16, 120, "F q0 unchanged");
	expect_col(&p, 1, 7, 100, "F cb p0 unchanged");
	expect_col(&p, 1, 8, 120, "F cb q0 unchanged");
}

/* G: horizontal macroblock edge, bS = 1 (same arithmetic as case B, rotated). */
static void case_g_horizontal(void)
{
	struct pic p;
	n2avc_mbinfo mb[2];
	int x, y;

	pic_init(&p, 16, 32);
	fill_all(&p, 100);
	for (y = 16; y < 32; y++)
		for (x = 0; x < 16; x++)
			set_px(&p, 0, x, y, 110);
	for (y = 8; y < 16; y++)
		for (x = 0; x < 8; x++) {
			set_px(&p, 1, x, y, 110);
			set_px(&p, 2, x, y, 110);
		}
	mb_reset(&mb[0], N2MB_P16, 0, 0, 0, 26);
	mb_reset(&mb[1], N2MB_P16, 0, 0, 4, 26);

	n2avc_deblock_picture(&p.f, mb, 1, 2);

	expect_row(&p, 0, 13, 100, "G p2 untouched");
	expect_row(&p, 0, 14, 101, "G p1");
	expect_row(&p, 0, 15, 103, "G p0");
	expect_row(&p, 0, 16, 107, "G q0");
	expect_row(&p, 0, 17, 109, "G q1");
	expect_row(&p, 0, 18, 110, "G q2 untouched");
	expect_row(&p, 1, 7, 102, "G cb p0");
	expect_row(&p, 1, 8, 108, "G cb q0");
	expect_row(&p, 2, 7, 102, "G cr p0");
	expect_row(&p, 2, 8, 108, "G cr q0");
}

/*
 * H: bS = 4 but the strong condition fails: |p0-q0| = 12 >= (alpha>>2)+2 = 5.  QP 26, alpha 15, beta 6.
 * filterSamplesFlag: 12 < 15, |p1-p0| = 0, |q1-q0| = 0 -> filtering with the weak branch of 8.7.2.4:
 * p0' = (2*p1 + p0 + q1 + 2)>>2 = (200+100+112+2)>>2 = 103; q0' = (224+112+100+2)>>2 = 109; p1/q1/p2/q2 unchanged.
 */
static void case_h_bs4_weak(void)
{
	struct pic p;
	n2avc_mbinfo mb[2];
	int x, y;

	pic_init(&p, 32, 16);
	fill_all(&p, 100);
	for (y = 0; y < 16; y++)
		for (x = 16; x < 32; x++)
			set_px(&p, 0, x, y, 112);
	for (y = 0; y < 8; y++)
		for (x = 8; x < 16; x++) {
			set_px(&p, 1, x, y, 112);
			set_px(&p, 2, x, y, 112);
		}
	mb_reset(&mb[0], N2MB_I16, -1, 0, 0, 26);
	mb_reset(&mb[1], N2MB_P16, 0, 0, 0, 26);

	n2avc_deblock_picture(&p.f, mb, 2, 1);

	expect_col(&p, 0, 13, 100, "H p2 unchanged");
	expect_col(&p, 0, 14, 100, "H p1 unchanged");
	expect_col(&p, 0, 15, 103, "H p0");
	expect_col(&p, 0, 16, 109, "H q0");
	expect_col(&p, 0, 17, 112, "H q1 unchanged");
	expect_col(&p, 1, 7, 103, "H cb p0");
	expect_col(&p, 1, 8, 109, "H cb q0");
	expect_col(&p, 2, 7, 103, "H cr p0");
	expect_col(&p, 2, 8, 109, "H cr q0");
}

/* I: odd macroblock counts, mixed types/vectors, random content; both calls must give identical output. */
static void case_i_robustness(void)
{
	static struct pic p, q;
	n2avc_mbinfo mb[5 * 3];
	uint32_t seed = 12345;
	int i, x, y, w = 5 * 16, h = 3 * 16;

	pic_init(&p, w, h);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			seed = seed * 1664525u + 1013904223u;
			set_px(&p, 0, x, y, (int)(seed >> 24));
		}
	for (y = 0; y < h / 2; y++)
		for (x = 0; x < w / 2; x++) {
			seed = seed * 1664525u + 1013904223u;
			set_px(&p, 1, x, y, (int)(seed >> 24));
			seed = seed * 1664525u + 1013904223u;
			set_px(&p, 2, x, y, (int)(seed >> 24));
		}
	q = p;
	q.f.y.p = q.y; q.f.cb.p = q.cb; q.f.cr.p = q.cr;
	for (i = 0; i < 15; i++) {
		int type = (i * 7 + 3) % 4;
		mb_reset(&mb[i], type == 0 ? N2MB_I16 : type == 1 ? N2MB_IPCM : type == 2 ? N2MB_P16 : N2MB_PSKIP,
		         (type == 0 || type == 1) ? -1 : 0, (i % 5) * 4 - 8, (i % 3) * 4 - 4, 10 + (i * 9) % 42);
		if (type == 2) {
			int b;
			for (b = 0; b < 16; b++)
				mb[i].nnz[b] = (uint8_t)((i + b) % 3);
		}
	}
	n2avc_deblock_picture(&p.f, mb, 5, 3);
	n2avc_deblock_picture(&q.f, mb, 5, 3);
	CHECK(memcmp(p.y, q.y, (size_t)w * h) == 0 &&
	      memcmp(p.cb, q.cb, (size_t)(w / 2) * (h / 2)) == 0 &&
	      memcmp(p.cr, q.cr, (size_t)(w / 2) * (h / 2)) == 0, "I: two runs differ");

	/* NULL and nonsensical arguments must be ignored, not dereferenced. */
	n2avc_deblock_picture(NULL, mb, 5, 3);
	n2avc_deblock_picture(&p.f, NULL, 5, 3);
	n2avc_deblock_picture(&p.f, mb, 0, 3);
	n2avc_deblock_picture(&p.f, mb, 5, -1);
	{
		n2avc_frame f = p.f;
		f.y.p = NULL;
		n2avc_deblock_picture(&f, mb, 5, 3);
	}
	CHECK(1, "I: NULL arguments survived");
}


/*
 * J: exact-size heap planes.  Unlike struct pic (fixed larger arrays), these allocations have ASan redzones immediately after
 * the last sample of each plane, so any read or write outside the picture is caught even at the outer macroblock edges.
 */
static void case_j_exact_bounds(void)
{
	enum { W = 32, H = 32 };
	uint8_t *y = malloc((size_t)W * H);
	uint8_t *cb = malloc((size_t)(W / 2) * (H / 2));
	uint8_t *cr = malloc((size_t)(W / 2) * (H / 2));
	n2avc_frame f;
	n2avc_mbinfo mb[4];
	int i, x, y2;

	if (!y || !cb || !cr) {
		CHECK(0, "J: allocation failed");
		free(y); free(cb); free(cr);
		return;
	}
	for (i = 0; i < W * H; i++) y[i] = (uint8_t)(100 + (i * 3) % 40);
	for (i = 0; i < (W / 2) * (H / 2); i++) { cb[i] = (uint8_t)(100 + i % 30); cr[i] = (uint8_t)(120 - i % 30); }
	f.y.p = y; f.y.stride = W; f.cb.p = cb; f.cb.stride = W / 2; f.cr.p = cr; f.cr.stride = W / 2;
	for (i = 0; i < 4; i++)
		mb_reset(&mb[i], N2MB_I16, -1, 0, 0, 26);
	/* Intra internal and macroblock edges all filter; every read must stay inside the four exact planes. */
	n2avc_deblock_picture(&f, mb, 2, 2);
	CHECK(1, "J: exact 32x32 intra picture survived");
	{
		int lo = 255, hi = 0;
		for (y2 = 0; y2 < H; y2++)
			for (x = 0; x < W; x++) {
				int v = y[(size_t)y2 * W + x];
				if (v < lo) lo = v;
				if (v > hi) hi = v;
			}
		CHECK(lo >= 0 && hi <= 255, "J: samples in range (%d..%d)", lo, hi);
	}
	free(y); free(cb); free(cr);
}

/*
 * K: the documented maximum 4096x2304 (256x144 macroblocks) with heap planes exactly the picture size.  The filter has no
 * allocation and its indices are ints up to 4096/2304; this checks the bounds and the pointer arithmetic at the maximum.
 */
static void case_k_max_size(void)
{
	enum { MBW = 256, MBH = 144, W = MBW * 16, H = MBH * 16 };
	size_t yn = (size_t)W * H, cn = yn / 4;
	uint8_t *y = malloc(yn), *cb = malloc(cn), *cr = malloc(cn);
	n2avc_mbinfo *mb = calloc((size_t)MBW * MBH, sizeof *mb);
	n2avc_frame f;
	int m;

	if (!y || !cb || !cr || !mb) {
		CHECK(0, "K: allocation failed");
		free(y); free(cb); free(cr); free(mb);
		return;
	}
	memset(y, 100, yn);
	memset(cb, 100, cn);
	memset(cr, 100, cn);
	for (m = 0; m < MBW * MBH; m++) {
		int mbx = m % MBW, mby = m / MBW;

		mb[m].type = N2MB_P16;
		mb[m].ref = 0;
		mb[m].qp = 26;
		mb[m].mvx = (int16_t)((mbx & 1) ? 4 : 0);   /* bS = 1 on every other vertical edge */
		mb[m].mvy = 0;
		if (((mbx + mby) % 37) == 0) {
			mb[m].type = N2MB_I16;
			mb[m].ref = -1;
		}
	}
	f.y.p = y; f.y.stride = W; f.cb.p = cb; f.cb.stride = W / 2; f.cr.p = cr; f.cr.stride = W / 2;
	n2avc_deblock_picture(&f, mb, MBW, MBH);
	CHECK(y[0] == 100 && y[yn - 1] == 100, "K: 4096x2304 corners unchanged");
	CHECK(cb[cn - 1] == 100 && cr[cn - 1] == 100, "K: chroma corners unchanged");
	free(y); free(cb); free(cr); free(mb);
}

int main(void)
{
	case_a_no_filter();
	case_b_bs1();
	case_c_bs2();
	case_d_bs4_strong();
	case_e_bs3();
	case_f_no_filter_samples();
	case_g_horizontal();
	case_h_bs4_weak();
	case_i_robustness();
	case_j_exact_bounds();
	case_k_max_size();

	if (failures) {
		printf("N2TEST R2 FAIL %d/%d\n", checks - failures, checks);
		return 1;
	}
	printf("N2TEST R2 PASS %d/%d\n", checks, checks);
	return 0;
}
