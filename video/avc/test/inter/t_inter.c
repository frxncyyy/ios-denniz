/*
 * t_inter.c -- host oracle for WP04 (H.264 inter prediction, motion search,
 * rate control).  Built twice under ASan+UBSan (with and without
 * -DN2AVC_ME_FULL) and once under TSan; see run_tests.sh.
 *
 * Expected values are literals from ITU-T H.264 clauses 8.4.1.1, 8.4.1.3,
 * 8.4.2.2.2, 9.1.1 and 7.3.4, hand-computed, plus the task's own test cases.
 * The WP03 side of the seam is stub_wp03.c; it records syntax calls instead of
 * writing bits, and reconstructs the prediction.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L

#include "n2avc_inter_priv.h"

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int n2_wp04_force_zero;

static int checks;
static int failures;

static void
check_impl(int ok, int line, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (ok)
		return;
	failures++;
	printf("FAIL line %d: ", line);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

#define CHECK(cond, ...) check_impl((cond) != 0, __LINE__, __VA_ARGS__)

/* ------------------------------------------------------------------------- */
/* Frames                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct tframe {
	n2avc_frame f;
	uint8_t *base[3];
	int w, h;
	int mb_w, mb_h;
} tframe;

static int
tf_alloc(tframe *t, int mb_w, int mb_h)
{
	const int w = mb_w * 16, h = mb_h * 16;
	const int wc = mb_w * 8, hc = mb_h * 8;
	const int ys = w + 2 * N2AVC_PAD;
	const int cs = wc + N2AVC_PAD;
	const int padc = N2AVC_PAD / 2;
	size_t ysize = (size_t)(h + 2 * N2AVC_PAD) * (size_t)ys;
	size_t csize = (size_t)(hc + 2 * padc) * (size_t)cs;

	memset(t, 0, sizeof(*t));
	t->w = w;
	t->h = h;
	t->mb_w = mb_w;
	t->mb_h = mb_h;
	t->base[0] = (uint8_t *)calloc(ysize, 1);
	t->base[1] = (uint8_t *)calloc(csize, 1);
	t->base[2] = (uint8_t *)calloc(csize, 1);
	if (t->base[0] == NULL || t->base[1] == NULL || t->base[2] == NULL) {
		free(t->base[0]);
		free(t->base[1]);
		free(t->base[2]);
		memset(t, 0, sizeof(*t));
		return 0;
	}
	t->f.y.p = t->base[0] + (size_t)N2AVC_PAD * ys + N2AVC_PAD;
	t->f.y.stride = ys;
	t->f.cb.p = t->base[1] + (size_t)padc * cs + padc;
	t->f.cb.stride = cs;
	t->f.cr.p = t->base[2] + (size_t)padc * cs + padc;
	t->f.cr.stride = cs;
	return 1;
}

static void
tf_free(tframe *t)
{
	free(t->base[0]);
	free(t->base[1]);
	free(t->base[2]);
	memset(t, 0, sizeof(*t));
}

static void
plane_extend(n2avc_plane *p, int w, int h, int pad)
{
	int y;

	for (y = 0; y < h; y++) {
		uint8_t *row = p->p + y * p->stride;

		memset(row - pad, row[0], (size_t)pad);
		memset(row + w, row[w - 1], (size_t)pad);
	}
	for (y = 1; y <= pad; y++) {
		memcpy(p->p - y * p->stride - pad, p->p - pad, (size_t)(w + 2 * pad));
		memcpy(p->p + (h - 1 + y) * p->stride - pad, p->p + (h - 1) * p->stride - pad,
		       (size_t)(w + 2 * pad));
	}
}

static void
tf_extend(tframe *t)
{
	plane_extend(&t->f.y, t->w, t->h, N2AVC_PAD);
	plane_extend(&t->f.cb, t->w / 2, t->h / 2, N2AVC_PAD / 2);
	plane_extend(&t->f.cr, t->w / 2, t->h / 2, N2AVC_PAD / 2);
}

static uint32_t
lcg(uint32_t *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

static void
tf_fill_const(tframe *t, int y, int cb, int cr)
{
	int j;

	for (j = 0; j < t->h; j++)
		memset(t->f.y.p + j * t->f.y.stride, y, (size_t)t->w);
	for (j = 0; j < t->h / 2; j++) {
		memset(t->f.cb.p + j * t->f.cb.stride, cb, (size_t)t->w / 2);
		memset(t->f.cr.p + j * t->f.cr.stride, cr, (size_t)t->w / 2);
	}
	tf_extend(t);
}

static void
tf_fill_noise(tframe *t, uint32_t seed)
{
	uint32_t s = seed;
	int i, j;

	for (j = 0; j < t->h; j++)
		for (i = 0; i < t->w; i++)
			t->f.y.p[j * t->f.y.stride + i] = (uint8_t)(lcg(&s) >> 24);
	for (j = 0; j < t->h / 2; j++)
		for (i = 0; i < t->w / 2; i++) {
			t->f.cb.p[j * t->f.cb.stride + i] = (uint8_t)(lcg(&s) >> 24);
			t->f.cr.p[j * t->f.cr.stride + i] = (uint8_t)(lcg(&s) >> 24);
		}
	tf_extend(t);
}

static void
tf_fill_square(tframe *t)
{
	const int x0 = 5, y0 = 5;
	int i, j;

	for (j = 0; j < t->h; j++)
		for (i = 0; i < t->w; i++)
			t->f.y.p[j * t->f.y.stride + i] =
				(i >= x0 && i < x0 + 16 && j >= y0 && j < y0 + 16) ? 0 : 128;
	for (j = 0; j < t->h / 2; j++) {
		memset(t->f.cb.p + j * t->f.cb.stride, 128, (size_t)t->w / 2);
		memset(t->f.cr.p + j * t->f.cr.stride, 128, (size_t)t->w / 2);
	}
	tf_extend(t);
}

/* dst(x, y) = src(x + dx, y + dy); chroma neutral.  dx/dy are whole luma
 * samples and must stay within the source's border. */
static void
tf_shift(tframe *dst, const tframe *src, int dx, int dy)
{
	int i, j;

	for (j = 0; j < dst->h; j++)
		for (i = 0; i < dst->w; i++)
			dst->f.y.p[j * dst->f.y.stride + i] =
				src->f.y.p[(j + dy) * src->f.y.stride + i + dx];
	for (j = 0; j < dst->h / 2; j++) {
		memset(dst->f.cb.p + j * dst->f.cb.stride, 128, (size_t)dst->w / 2);
		memset(dst->f.cr.p + j * dst->f.cr.stride, 128, (size_t)dst->w / 2);
	}
	tf_extend(dst);
}

static void
tf_copy(tframe *dst, const tframe *src)
{
	int j;

	for (j = 0; j < dst->h; j++)
		memcpy(dst->f.y.p + j * dst->f.y.stride, src->f.y.p + j * src->f.y.stride,
		       (size_t)dst->w);
	for (j = 0; j < dst->h / 2; j++) {
		memcpy(dst->f.cb.p + j * dst->f.cb.stride, src->f.cb.p + j * src->f.cb.stride,
		       (size_t)dst->w / 2);
		memcpy(dst->f.cr.p + j * dst->f.cr.stride, src->f.cr.p + j * src->f.cr.stride,
		       (size_t)dst->w / 2);
	}
	tf_extend(dst);
}

/* ------------------------------------------------------------------------- */
/* Slice and bit-record helpers                                              */
/* ------------------------------------------------------------------------- */

typedef struct tslice {
	n2avc_slice s;
	uint8_t bwbuf[8192];
} tslice;

static void
ts_init(tslice *t, tframe *src, tframe *ref, tframe *rec, n2avc_mbinfo *mb,
        int mb_w, int mb_h, int first, int end, int qp, int range)
{
	memset(t, 0, sizeof(*t));
	t->s.slice_type = 0;
	t->s.qp = qp;
	t->s.qpc = qp;
	t->s.mb_w = mb_w;
	t->s.mb_h = mb_h;
	t->s.first_row = first;
	t->s.end_row = end;
	t->s.src = &src->f;
	t->s.rec = &rec->f;
	t->s.ref = &ref->f;
	t->s.mb = mb;
	t->s.search_range = range;
	n2bw_init(&t->s.bw, t->bwbuf, sizeof(t->bwbuf));
}

typedef struct brec {
	char op;
	int32_t val;
} brec;

static int
records(const tslice *t, brec *out, int cap)
{
	size_t i = 0;
	int n = 0;

	while (i + 5 <= t->s.bw.pos && n < cap) {
		uint32_t u = (uint32_t)t->bwbuf[i + 1]
		           | (uint32_t)t->bwbuf[i + 2] << 8
		           | (uint32_t)t->bwbuf[i + 3] << 16
		           | (uint32_t)t->bwbuf[i + 4] << 24;

		out[n].op = (char)t->bwbuf[i];
		out[n].val = (int32_t)u;
		i += 5;
		n++;
	}
	return n;
}

static void
init_mb_intra(n2avc_mbinfo *mb, int n)
{
	int i;

	memset(mb, 0, sizeof(*mb) * (size_t)n);
	for (i = 0; i < n; i++) {
		mb[i].ref = -1;
		mb[i].type = N2MB_I16;
	}
}

/* ------------------------------------------------------------------------- */
/* Tables                                                                    */
/* ------------------------------------------------------------------------- */

static void
test_tables(void)
{
	static const struct { int qp, lambda; } lit[] = {
		{ 0, 1 }, { 10, 1 }, { 12, 1 }, { 17, 2 }, { 20, 2 }, { 21, 3 },
		{ 26, 5 }, { 32, 9 }, { 38, 19 }, { 44, 37 }, { 51, 83 }
	};
	size_t i;
	int qp;

	for (i = 0; i < sizeof(lit) / sizeof(lit[0]); i++)
		CHECK(n2avc_me_lambda(lit[i].qp) == lit[i].lambda,
		      "lambda(%d) = %d, expected %d", lit[i].qp,
		      n2avc_me_lambda(lit[i].qp), lit[i].lambda);
	for (qp = 0; qp < 51; qp++)
		CHECK(n2avc_me_lambda(qp) <= n2avc_me_lambda(qp + 1),
		      "lambda is not monotone at %d", qp);
	CHECK(n2avc_me_lambda(-3) == 1, "lambda(-3) must clamp to 1");
	CHECK(n2avc_me_lambda(99) == 83, "lambda(99) must clamp to 83");

	/* clause 9.1.1: se(v) -> codeNum = 2|v| - (v > 0), length 2*floor(log2(codeNum+1))+1 */
	CHECK(n2avc_se_bits(0) == 1, "se_bits(0)");
	CHECK(n2avc_se_bits(1) == 3, "se_bits(1)");
	CHECK(n2avc_se_bits(-1) == 3, "se_bits(-1)");
	CHECK(n2avc_se_bits(2) == 5, "se_bits(2)");
	CHECK(n2avc_se_bits(-2) == 5, "se_bits(-2)");
	CHECK(n2avc_se_bits(3) == 5, "se_bits(3)");
	CHECK(n2avc_se_bits(4) == 7, "se_bits(4)");
	CHECK(n2avc_se_bits(12) == 9, "se_bits(12)");
	CHECK(n2avc_se_bits(-4) == 7, "se_bits(-4)");
}

/* ------------------------------------------------------------------------- */
/* Clause 8.4.1.3: 16x16 motion vector prediction                            */
/* ------------------------------------------------------------------------- */

static void
test_pmv(void)
{
	enum { MW = 4, MH = 3, N = MW * MH };
	n2avc_mbinfo mb[N];
	tframe src, ref, rec;
	tslice t;
	int x, y;

	CHECK(tf_alloc(&src, MW, MH) && tf_alloc(&ref, MW, MH) && tf_alloc(&rec, MW, MH),
	      "pmv frame allocation");
	tf_fill_const(&src, 128, 128, 128);
	tf_fill_const(&ref, 128, 128, 128);
	tf_fill_const(&rec, 0, 0, 0);

	/* All neighbours inter with (0,0) unless a case says otherwise. */
	memset(mb, 0, sizeof(mb));
	for (y = 0; y < MH; y++)
		for (x = 0; x < MW; x++) {
			mb[y * MW + x].ref = 0;
			mb[y * MW + x].type = N2MB_P16;
		}
	ts_init(&t, &src, &ref, &rec, mb, MW, MH, 0, MH, 26, 16);

	/* A=(4,8) B=(12,-4) C=(0,0) -> component-wise median (4,0). */
	mb[1 * MW + 0].mvx = 4;  mb[1 * MW + 0].mvy = 8;
	mb[0 * MW + 1].mvx = 12; mb[0 * MW + 1].mvy = -4;
	mb[0 * MW + 2].mvx = 0;  mb[0 * MW + 2].mvy = 0;
	{
		int mx, my;

		n2avc_inter_pmv(&t.s, 1, 1, &mx, &my);
		CHECK(mx == 4 && my == 0, "median A(4,8) B(12,-4) C(0,0) -> (%d,%d), expected (4,0)", mx, my);
	}

	/* Only A inter -> A. */
	mb[0 * MW + 1].ref = -1;
	mb[0 * MW + 2].ref = -1;
	{
		int mx, my;

		n2avc_inter_pmv(&t.s, 1, 1, &mx, &my);
		CHECK(mx == 4 && my == 8, "only A inter -> (%d,%d), expected (4,8)", mx, my);
	}

	/* A unavailable; B=(8,8), C=(8,8) -> median of (0,0),(8,8),(8,8) = (8,8). */
	memset(mb, 0, sizeof(mb));
	for (y = 0; y < MH; y++)
		for (x = 0; x < MW; x++) {
			mb[y * MW + x].ref = 0;
			mb[y * MW + x].type = N2MB_P16;
		}
	mb[0 * MW + 0].mvx = 8; mb[0 * MW + 0].mvy = 8;
	mb[0 * MW + 1].mvx = 8; mb[0 * MW + 1].mvy = 8;
	{
		int mx, my;

		n2avc_inter_pmv(&t.s, 0, 1, &mx, &my);
		CHECK(mx == 8 && my == 8, "A unavailable B=C=(8,8) -> (%d,%d), expected (8,8)", mx, my);
	}

	/* B and C unavailable (slice starts at row 1), A=(4,4) -> A. */
	memset(mb, 0, sizeof(mb));
	for (y = 0; y < MH; y++)
		for (x = 0; x < MW; x++) {
			mb[y * MW + x].ref = 0;
			mb[y * MW + x].type = N2MB_P16;
		}
	mb[1 * MW + 0].mvx = 4; mb[1 * MW + 0].mvy = 4;
	{
		tslice t2 = t;
		int mx, my;

		t2.s.first_row = 1;
		t2.s.end_row = MH;
		n2avc_inter_pmv(&t2.s, 1, 1, &mx, &my);
		CHECK(mx == 4 && my == 4, "B,C unavailable, A=(4,4) -> (%d,%d), expected (4,4)", mx, my);
	}

	/* C unavailable at the right edge -> D is used: A=(0,0), B=(4,0), D=(12,-4)
	 * -> median (4,0). */
	memset(mb, 0, sizeof(mb));
	for (y = 0; y < MH; y++)
		for (x = 0; x < MW; x++) {
			mb[y * MW + x].ref = 0;
			mb[y * MW + x].type = N2MB_P16;
		}
	mb[1 * MW + 2].mvx = 0;  mb[1 * MW + 2].mvy = 0;
	mb[0 * MW + 3].mvx = 4;  mb[0 * MW + 3].mvy = 0;
	mb[0 * MW + 2].mvx = 12; mb[0 * MW + 2].mvy = -4;
	{
		int mx, my;

		n2avc_inter_pmv(&t.s, MW - 1, 1, &mx, &my);
		CHECK(mx == 4 && my == 0, "C unavailable, D=(12,-4) -> (%d,%d), expected (4,0)", mx, my);
	}

	/* A intra, B=(4,8), C=(12,-4) -> median (0,0),(4,8),(12,-4) = (4,0). */
	memset(mb, 0, sizeof(mb));
	for (y = 0; y < MH; y++)
		for (x = 0; x < MW; x++) {
			mb[y * MW + x].ref = 0;
			mb[y * MW + x].type = N2MB_P16;
		}
	mb[1 * MW + 0].ref = -1; mb[1 * MW + 0].type = N2MB_I16;
	mb[0 * MW + 1].mvx = 4;  mb[0 * MW + 1].mvy = 8;
	mb[0 * MW + 2].mvx = 12; mb[0 * MW + 2].mvy = -4;
	{
		int mx, my;

		n2avc_inter_pmv(&t.s, 1, 1, &mx, &my);
		CHECK(mx == 4 && my == 0, "A intra -> (%d,%d), expected (4,0)", mx, my);
	}

	/* Exactly one inter neighbour, C=(3,7) -> C. */
	memset(mb, 0, sizeof(mb));
	for (y = 0; y < MH; y++)
		for (x = 0; x < MW; x++)
			mb[y * MW + x].ref = -1;
	mb[0 * MW + 2].ref = 0;
	mb[0 * MW + 2].mvx = 3;
	mb[0 * MW + 2].mvy = 7;
	{
		int mx, my;

		n2avc_inter_pmv(&t.s, 1, 1, &mx, &my);
		CHECK(mx == 3 && my == 7, "only C inter -> (%d,%d), expected (3,7)", mx, my);
	}

	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* Clause 8.4.1.1: P_Skip vector                                             */
/* ------------------------------------------------------------------------- */

static void
test_skip_mv(void)
{
	enum { MW = 4, MH = 3, N = MW * MH };
	n2avc_mbinfo mb[N];
	tframe src, ref, rec;
	tslice t;
	int x, y;

	CHECK(tf_alloc(&src, MW, MH) && tf_alloc(&ref, MW, MH) && tf_alloc(&rec, MW, MH),
	      "skip_mv frame allocation");
	tf_fill_const(&src, 128, 128, 128);
	tf_fill_const(&ref, 128, 128, 128);
	tf_fill_const(&rec, 0, 0, 0);

	memset(mb, 0, sizeof(mb));
	for (y = 0; y < MH; y++)
		for (x = 0; x < MW; x++) {
			mb[y * MW + x].ref = 0;
			mb[y * MW + x].type = N2MB_P16;
		}
	ts_init(&t, &src, &ref, &rec, mb, MW, MH, 0, MH, 26, 16);

	/* A unavailable -> (0,0). */
	mb[0 * MW + 0].mvx = 4;
	mb[0 * MW + 0].mvy = 0;
	{
		int mx, my;

		n2avc_inter_skip_mv(&t.s, 0, 1, &mx, &my);
		CHECK(mx == 0 && my == 0, "skip A unavailable -> (%d,%d), expected (0,0)", mx, my);
	}

	/* A=(0,0) -> (0,0). */
	mb[1 * MW + 0].mvx = 0; mb[1 * MW + 0].mvy = 0;
	mb[0 * MW + 1].mvx = 4; mb[0 * MW + 1].mvy = 0;
	mb[0 * MW + 2].mvx = 4; mb[0 * MW + 2].mvy = 0;
	{
		int mx, my;

		n2avc_inter_skip_mv(&t.s, 1, 1, &mx, &my);
		CHECK(mx == 0 && my == 0, "skip A=(0,0) -> (%d,%d), expected (0,0)", mx, my);
	}

	/* A=B=C=(4,0) -> prediction (4,0). */
	mb[1 * MW + 0].mvx = 4; mb[1 * MW + 0].mvy = 0;
	{
		int mx, my;

		n2avc_inter_skip_mv(&t.s, 1, 1, &mx, &my);
		CHECK(mx == 4 && my == 0, "skip A=B=C=(4,0) -> (%d,%d), expected (4,0)", mx, my);
	}

	/* A intra, B=C=(4,0): the intra neighbour is available with refIdx -1,
	 * so it does not force (0,0); the 8.4.1.3 median of (0,0), (4,0), (4,0)
	 * is (4,0).  (Lead and R2 verified this against a reference decoder:
	 * clause 8.4.1.1 names only unavailable neighbours and inter neighbours
	 * with a zero vector.) */
	mb[1 * MW + 0].ref = -1;
	mb[1 * MW + 0].type = N2MB_I16;
	{
		int mx, my;

		n2avc_inter_skip_mv(&t.s, 1, 1, &mx, &my);
		CHECK(mx == 4 && my == 0, "skip A intra, B=C=(4,0) -> (%d,%d), expected (4,0)", mx, my);
	}

	/* B unavailable -> (0,0). */
	mb[1 * MW + 0].ref = 0;
	mb[1 * MW + 0].type = N2MB_P16;
	mb[1 * MW + 0].mvx = 4; mb[1 * MW + 0].mvy = 0;
	{
		int mx, my;

		n2avc_inter_skip_mv(&t.s, 1, 0, &mx, &my);
		CHECK(mx == 0 && my == 0, "skip B unavailable -> (%d,%d), expected (0,0)", mx, my);
	}

	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* Prediction: luma copy and clause 8.4.2.2.2 chroma                         */
/* ------------------------------------------------------------------------- */

static void
test_predict(void)
{
	tframe src, ref, rec;
	tslice t;
	n2avc_mbinfo mb;
	uint8_t py[256], pcb[64], pcr[64];
	int i, j;

	CHECK(tf_alloc(&src, 1, 1) && tf_alloc(&ref, 1, 1) && tf_alloc(&rec, 1, 1),
	      "predict frame allocation");
	memset(&mb, 0, sizeof(mb));
	mb.ref = -1;
	ts_init(&t, &src, &ref, &rec, &mb, 1, 1, 0, 1, 26, 16);

	/* 2x2 chroma checker {0,255;255,0} with mv fraction 4/8 gives 128.
	 * Tile it over the whole plane so every predicted sample sees a checker. */
	for (j = 0; j < ref.h / 2 + 1; j++)
		for (i = 0; i < ref.w / 2 + 1; i++) {
			ref.f.cb.p[j * ref.f.cb.stride + i] = (uint8_t)(((i + j) & 1) ? 255 : 0);
			ref.f.cr.p[j * ref.f.cr.stride + i] = (uint8_t)(((i + j) & 1) ? 0 : 255);
		}
	for (j = 0; j < 32; j++)
		memset(ref.f.y.p + j * ref.f.y.stride, 128, 32);
	n2avc_inter_predict(&t.s, 0, 0, 4, 4, py, pcb, pcr);
	for (i = 0; i < 64; i++)
		CHECK(pcb[i] == 128, "chroma bilinear xF=yF=4 sample %d = %d, expected 128", i, pcb[i]);
	for (i = 0; i < 64; i++)
		CHECK(pcr[i] == 128, "chroma bilinear (reversed checker) sample %d = %d, expected 128", i, pcr[i]);

	/* Luma: an integer vector copies the reference block. */
	for (j = 0; j < 32; j++)
		for (i = 0; i < 32; i++)
			ref.f.y.p[j * ref.f.y.stride + i] = (uint8_t)((i * 3 + j * 5) & 0xff);
	n2avc_inter_predict(&t.s, 0, 0, 8, -4, py, pcb, pcr);
	for (j = 0; j < 16; j++)
		for (i = 0; i < 16; i++)
			CHECK(py[j * 16 + i] == ref.f.y.p[(j - 1) * ref.f.y.stride + i + 2],
			      "luma copy at (%d,%d): %d != %d", i, j, py[j * 16 + i],
			      ref.f.y.p[(j - 1) * ref.f.y.stride + i + 2]);

	/* A single reference sample at the half-chroma position must be used. */
	memset(ref.base[0], 0, (size_t)(ref.h + 2 * N2AVC_PAD) * (size_t)(ref.w + 2 * N2AVC_PAD));
	ref.f.y.p[1 * ref.f.y.stride + 1] = 200;
	n2avc_inter_predict(&t.s, 0, 0, 4, 4, py, pcb, pcr);
	CHECK(py[0] == 200, "luma mv (1,1) sample (0,0) = %d, expected 200", py[0]);

	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* Luma interpolation, literal values from clause 8.4.2.2.1                  */
/* ------------------------------------------------------------------------- */

static void
test_predict_literals(void)
{
	tframe src, ref, rec;
	tslice t;
	n2avc_mbinfo mb;
	uint8_t py[256], pcb[64], pcr[64];
	int i, xf, yf, ok;

	CHECK(tf_alloc(&src, 1, 1) && tf_alloc(&ref, 1, 1) && tf_alloc(&rec, 1, 1),
	      "luma literals frame allocation");
	memset(&mb, 0, sizeof(mb));
	mb.ref = -1;
	ts_init(&t, &src, &ref, &rec, &mb, 1, 1, 0, 1, 26, 16);

	/* (1) A constant picture is invariant under every fractional position
	 * (the 6-tap coefficients sum to 32 and the quarter average to 1). */
	tf_fill_const(&ref, 100, 128, 128);
	tf_extend(&ref);
	for (xf = 0; xf < 4; xf++)
		for (yf = 0; yf < 4; yf++) {
			ok = 1;
			n2avc_inter_predict(&t.s, 0, 0, xf, yf, py, pcb, pcr);
			for (i = 0; i < 256; i++)
				if (py[i] != 100)
					ok = 0;
			CHECK(ok, "constant 100, mv (%d,%d): a luma sample is not 100", xf, yf);
		}

	/* (2) A vertical edge at x = 8 (0 left, 255 right).  By clause
	 * 8.4.2.2.1 the half sample at x = 7.5 is
	 *   (20*255 - 5*255 + 1*255 + 16) >> 5 = (4080 + 16) >> 5 = 128,
	 * the quarter at x = 7.25 is (0 + 128 + 1) >> 1 = 64 and the one at
	 * x = 7.75 is (128 + 255 + 1) >> 1 = 192. */
	for (i = 0; i < 16; i++) {
		int j;

		for (j = 0; j < 16; j++)
			ref.f.y.p[i * ref.f.y.stride + j] = (uint8_t)(j < 8 ? 0 : 255);
	}
	memset(ref.f.cb.p, 128, (size_t)8 * (size_t)ref.f.cb.stride);
	memset(ref.f.cr.p, 128, (size_t)8 * (size_t)ref.f.cr.stride);
	tf_extend(&ref);
	n2avc_inter_predict(&t.s, 0, 0, 2, 0, py, pcb, pcr);
	CHECK(py[7] == 128, "half sample at x=7.5: %d, expected 128", py[7]);
	CHECK(py[0] == 0 && py[15] == 255, "half sample far from the edge: %d/%d, expected 0/255",
	      py[0], py[15]);
	n2avc_inter_predict(&t.s, 0, 0, 1, 0, py, pcb, pcr);
	CHECK(py[7] == 64, "quarter sample at x=7.25: %d, expected 64", py[7]);
	n2avc_inter_predict(&t.s, 0, 0, 3, 0, py, pcb, pcr);
	CHECK(py[7] == 192, "three-quarter sample at x=7.75: %d, expected 192", py[7]);

	/* (3) The same edge turned by 90 degrees, for the vertical filter. */
	for (i = 0; i < 16; i++) {
		int j;

		for (j = 0; j < 16; j++)
			ref.f.y.p[i * ref.f.y.stride + j] = (uint8_t)(i < 8 ? 0 : 255);
	}
	tf_extend(&ref);
	n2avc_inter_predict(&t.s, 0, 0, 0, 2, py, pcb, pcr);
	CHECK(py[7 * 16] == 128, "half sample at y=7.5: %d, expected 128", py[7 * 16]);
	n2avc_inter_predict(&t.s, 0, 0, 0, 1, py, pcb, pcr);
	CHECK(py[7 * 16] == 64, "quarter sample at y=7.25: %d, expected 64", py[7 * 16]);

	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* Motion search and P_L0_16x16 syntax                                       */
/* ------------------------------------------------------------------------- */

static void
test_me_shift(void)
{
	tframe src, ref, rec;
	tslice t;
	n2avc_mbinfo mb[16];
	brec recs[16];
	int n;

	CHECK(tf_alloc(&src, 4, 4) && tf_alloc(&ref, 4, 4) && tf_alloc(&rec, 4, 4),
	      "me_shift frame allocation");
	tf_fill_square(&ref);
	tf_shift(&src, &ref, 3, -2);
	tf_fill_const(&rec, 0, 0, 0);
	init_mb_intra(mb, 16);
	ts_init(&t, &src, &ref, &rec, mb, 4, 4, 0, 4, 26, 16);
	CHECK(n2avc_inter_slice_begin(&t.s) != NULL, "slice_begin");
	CHECK(t.s.inter_priv != NULL, "slice_begin did not store its scratch");

	/* Directly: the shifted reference is found with SAD 0. */
	{
		n2avc_me_result r;

		n2avc_me_search(&t.s, 0, 0, 0, 0, &r);
		CHECK(r.mvx == 12 && r.mvy == -8, "search shift found mv (%d,%d), expected (12,-8)", r.mvx, r.mvy);
		CHECK(r.sad == 0, "search shift SAD = %d, expected 0", r.sad);
		CHECK(n2avc_me_valid(&t.s, 0, 0, r.mvx / 4, r.mvy / 4), "search shift vector must be legal");
	}

	/* Through the macroblock decision: P_L0_16x16, mvd = (12,-8) - (0,0). */
	{
		int ret = n2avc_p_mb(&t.s, 0, 0);

		CHECK(ret == 0, "shift p_mb returned %d, expected 0 (P_L0_16x16)", ret);
		CHECK(mb[0].type == N2MB_P16, "shift mbinfo type %u, expected P16", mb[0].type);
		CHECK(mb[0].ref == 0, "shift mbinfo ref %d, expected 0", mb[0].ref);
		CHECK(mb[0].mvx == 12 && mb[0].mvy == -8,
		      "shift mbinfo mv (%d,%d), expected (12,-8)", mb[0].mvx, mb[0].mvy);
		CHECK(mb[0].cbp == 0, "shift cbp %u, expected 0 (exact match)", mb[0].cbp);
		n = records(&t, recs, 16);
		CHECK(n == 6, "shift syntax record count %d, expected 6", n);
		if (n == 6) {
			CHECK(recs[0].op == 'f' && recs[0].val == 0, "shift flush_skip_run(%d), expected 0", recs[0].val);
			CHECK(recs[1].op == 'e' && recs[1].val == 0, "shift mb_type ue(%d), expected 0", recs[1].val);
			CHECK(recs[2].op == 's' && recs[2].val == 12, "shift mvd_x se(%d), expected 12", recs[2].val);
			CHECK(recs[3].op == 's' && recs[3].val == -8, "shift mvd_y se(%d), expected -8", recs[3].val);
			CHECK(recs[4].op == 'e' && recs[4].val == 1000, "shift cbp ue(%d), expected 1000", recs[4].val);
			CHECK(recs[5].op == 'r' && recs[5].val == 0, "shift residual cbp %d, expected 0", recs[5].val);
		}
		CHECK(t.s.bw.overflow == 0, "shift bit writer overflowed");
	}

	/* The same vector through n2avc_me_search with a non-zero prediction:
	 * mvd is still measured against the prediction, not against zero. */
	{
		n2avc_me_result r;

		n2avc_me_search(&t.s, 0, 0, 8, -4, &r);
		/* The RD cost may prefer a different vector once the prediction is
		 * (2,-1); only require that the reported mvd is consistent. */
		CHECK(r.pmvx == 8 && r.pmvy == -4, "search pmv (%d,%d)", r.pmvx, r.pmvy);
		CHECK(r.mvd_bits == n2avc_se_bits(r.mvx - 8) + n2avc_se_bits(r.mvy + 4),
		      "search mvd_bits %d is not the clause 9.1.1 length", r.mvd_bits);
		CHECK(r.cost == r.sad + n2avc_me_lambda(26) * r.mvd_bits,
		      "search cost %d != sad %d + lambda*mvd_bits %d", r.cost, r.sad, r.mvd_bits);
	}

	n2avc_inter_slice_end(&t.s);
	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* P_Skip                                                                    */
/* ------------------------------------------------------------------------- */

static void
test_static_skip(void)
{
	tframe src, ref, rec;
	tslice t;
	n2avc_mbinfo mb[4];
	int x, y;

	CHECK(tf_alloc(&src, 2, 2) && tf_alloc(&ref, 2, 2) && tf_alloc(&rec, 2, 2),
	      "static frame allocation");
	tf_fill_noise(&ref, 7);
	tf_copy(&src, &ref);
	tf_fill_const(&rec, 0, 0, 0);
	init_mb_intra(mb, 4);
	ts_init(&t, &src, &ref, &rec, mb, 2, 2, 0, 2, 32, 16);
	CHECK(n2avc_inter_slice_begin(&t.s) != NULL, "static slice_begin");

	for (y = 0; y < 2; y++)
		for (x = 0; x < 2; x++) {
			int ret = n2avc_p_mb(&t.s, x, y);

			CHECK(ret == 1, "static p_mb(%d,%d) returned %d, expected 1 (P_Skip)", x, y, ret);
			CHECK(mb[y * 2 + x].type == N2MB_PSKIP, "static mbinfo(%d,%d) type %u, expected PSKIP",
			      x, y, mb[y * 2 + x].type);
			CHECK(mb[y * 2 + x].cbp == 0, "static mbinfo(%d,%d) cbp %u", x, y, mb[y * 2 + x].cbp);
		}
	CHECK(t.s.skip_run == 4, "static skip_run %u, expected 4", t.s.skip_run);
	CHECK(t.s.bw.pos == 0, "static wrote %zu syntax bytes, expected 0", t.s.bw.pos);
	for (y = 0; y < src.h; y++)
		for (x = 0; x < src.w; x++)
			CHECK(rec.f.y.p[y * rec.f.y.stride + x] == src.f.y.p[y * src.f.y.stride + x],
			      "static reconstruction (%d,%d) differs", x, y);

	n2avc_inter_slice_end(&t.s);
	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

static void
test_force_skip(void)
{
	tframe src, ref, rec;
	tslice t;
	n2avc_mbinfo mb[1];
	int i, j;

	CHECK(tf_alloc(&src, 1, 1) && tf_alloc(&ref, 1, 1) && tf_alloc(&rec, 1, 1),
	      "force_skip frame allocation");
	/* A horizontal ramp in the source and the same ramp 8 higher in the
	 * reference.  search_range is 0 here, so (0,0) is the only vector the
	 * search may return and it is also the skip vector, while its SAD
	 * (135) is above the QP 10 skip threshold, so the P_Skip path itself
	 * is not taken.  A flat DC prediction is far off the ramp (SAD
	 * 16384), so intra does not win either.  The test double then forces
	 * the residual to cbp 0, which must still come out as P_Skip: a
	 * P_L0_16x16 with cbp 0 and the skip vector may not be coded (clause
	 * 7.3.4 makes the syntax ambiguous with a skip run). */
	for (j = 0; j < 16; j++)
		for (i = 0; i < 16; i++) {
			src.f.y.p[j * src.f.y.stride + i] = (uint8_t)(16 * i);
			ref.f.y.p[j * ref.f.y.stride + i] = (uint8_t)(16 * i + 8 > 255 ? 255 : 16 * i + 8);
		}
	for (j = 0; j < 8; j++) {
		memset(src.f.cb.p + j * src.f.cb.stride, 128, 8);
		memset(src.f.cr.p + j * src.f.cr.stride, 128, 8);
		memset(ref.f.cb.p + j * ref.f.cb.stride, 128, 8);
		memset(ref.f.cr.p + j * ref.f.cr.stride, 128, 8);
	}
	tf_extend(&src);
	tf_extend(&ref);
	tf_fill_const(&rec, 0, 0, 0);
	init_mb_intra(mb, 1);
	ts_init(&t, &src, &ref, &rec, mb, 1, 1, 0, 1, 10, 0);
	CHECK(n2avc_inter_slice_begin(&t.s) != NULL, "force_skip slice_begin");

	n2_wp04_force_zero = 1;
	{
		int ret = n2avc_p_mb(&t.s, 0, 0);

		CHECK(ret == 1, "force_skip p_mb returned %d, expected 1 (never P16 with cbp 0 and skip mv)", ret);
		CHECK(mb[0].type == N2MB_PSKIP, "force_skip type %u, expected PSKIP", mb[0].type);
		CHECK(t.s.skip_run == 1, "force_skip skip_run %u, expected 1", t.s.skip_run);
		CHECK(t.s.bw.pos == 0, "force_skip wrote syntax");
		CHECK(rec.f.y.p[0] == ref.f.y.p[0], "force_skip reconstruction %u, expected the skip prediction %u",
		      rec.f.y.p[0], ref.f.y.p[0]);
	}
	n2_wp04_force_zero = 0;

	n2avc_inter_slice_end(&t.s);
	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* Intra decision                                                            */
/* ------------------------------------------------------------------------- */

static void
test_intra_decision(void)
{
	tframe src, ref, rec;
	tslice t;
	n2avc_mbinfo mb[1];
	int j;

	CHECK(tf_alloc(&src, 1, 1) && tf_alloc(&ref, 1, 1) && tf_alloc(&rec, 1, 1),
	      "intra frame allocation");
	tf_fill_noise(&ref, 99);
	tf_fill_const(&src, 100, 128, 128);
	tf_fill_const(&rec, 0x5a, 0x5a, 0x5a);
	memset(mb, 0, sizeof(mb));
	mb[0].ref = 7;
	mb[0].type = 0xAA;
	ts_init(&t, &src, &ref, &rec, mb, 1, 1, 0, 1, 10, 16);
	CHECK(n2avc_inter_slice_begin(&t.s) != NULL, "intra slice_begin");

	{
		int ret = n2avc_p_mb(&t.s, 0, 0);

		CHECK(ret == 2, "intra p_mb returned %d, expected 2", ret);
		CHECK(t.s.bw.pos == 0, "intra wrote syntax");
		CHECK(t.s.skip_run == 0, "intra changed skip_run to %u", t.s.skip_run);
		CHECK(mb[0].ref == 7 && mb[0].type == 0xAA, "intra changed mbinfo");
		for (j = 0; j < 16; j++)
			CHECK(rec.f.y.p[j * rec.f.y.stride + 3] == 0x5a,
			      "intra changed rec row %d", j);
	}

	n2avc_inter_slice_end(&t.s);
	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* Bounds                                                                    */
/* ------------------------------------------------------------------------- */

static void
test_bounds(void)
{
	tframe src, ref, rec;
	tslice t;
	n2avc_mbinfo mb[1];
	n2avc_me_result r;

	CHECK(tf_alloc(&src, 1, 1) && tf_alloc(&ref, 1, 1) && tf_alloc(&rec, 1, 1),
	      "bounds 16x16 frame allocation");
	tf_fill_noise(&ref, 3);
	tf_fill_noise(&src, 4);
	tf_fill_const(&rec, 0, 0, 0);
	init_mb_intra(mb, 1);
	ts_init(&t, &src, &ref, &rec, mb, 1, 1, 0, 1, 26, 64);
	CHECK(n2avc_inter_slice_begin(&t.s) != NULL, "bounds slice_begin");
	n2avc_me_search(&t.s, 0, 0, 0, 0, &r);
	/* Quarter samples: the block must stay within N2AVC_PAD - 8 = 24
	 * samples of the 16x16 picture, so the integer part of the vector is
	 * in [-24, 24] and a fractional tail can add up to three quarter
	 * samples on the positive side. */
	CHECK(r.mvx >= -96 && r.mvx <= 99 && r.mvy >= -96 && r.mvy <= 99,
	      "16x16 range 64 search returned (%d,%d), outside the padded area", r.mvx, r.mvy);
	CHECK(n2avc_me_valid_q(&t.s, 0, 0, r.mvx, r.mvy),
	      "16x16 range 64 vector is not legal");
	(void)n2avc_p_mb(&t.s, 0, 0);      /* ASan watches every read */
	n2avc_inter_slice_end(&t.s);
	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);

	/* The awkward 178x146 (12x10 macroblocks) size, every macroblock, range 64. */
	{
		enum { MW = 12, MH = 10 };
		n2avc_mbinfo mb2[MW * MH];
		tframe s2, r2, c2;
		tslice t2;
		int x, y;

		CHECK(tf_alloc(&s2, MW, MH) && tf_alloc(&r2, MW, MH) && tf_alloc(&c2, MW, MH),
		      "bounds 178x146 frame allocation");
		tf_fill_noise(&r2, 11);
		tf_fill_noise(&s2, 12);
		tf_fill_const(&c2, 0, 0, 0);
		init_mb_intra(mb2, MW * MH);
		ts_init(&t2, &s2, &r2, &c2, mb2, MW, MH, 0, MH, 26, 64);
		CHECK(n2avc_inter_slice_begin(&t2.s) != NULL, "bounds 178x146 slice_begin");
		for (y = 0; y < MH; y++)
			for (x = 0; x < MW; x++) {
				int ret = n2avc_p_mb(&t2.s, x, y);

				CHECK(ret >= 0 && ret <= 2, "178x146 p_mb(%d,%d) returned %d", x, y, ret);
				if (mb2[y * MW + x].ref >= 0) {
					int mvx = mb2[y * MW + x].mvx;
					int mvy = mb2[y * MW + x].mvy;
					CHECK(n2avc_me_valid_q(&t2.s, x, y, mvx, mvy),
					      "178x146 mv (%d,%d) at (%d,%d) is outside the legal area",
					      mvx, mvy, x, y);
				}
			}
		n2avc_inter_slice_end(&t2.s);
		tf_free(&s2);
		tf_free(&r2);
		tf_free(&c2);
	}
}

/* ------------------------------------------------------------------------- */
/* Four slice threads (TSan)                                                 */
/* ------------------------------------------------------------------------- */

typedef struct thread_arg {
	tframe *src, *ref, *rec;
	n2avc_mbinfo *mb;
	int mb_w, mb_h, first, last, qp, range;
	int bad;
} thread_arg;

static void *
thread_fn(void *p)
{
	thread_arg *a = (thread_arg *)p;
	tslice t;
	int x, y;

	ts_init(&t, a->src, a->ref, a->rec, a->mb, a->mb_w, a->mb_h,
	        a->first, a->last, a->qp, a->range);
	if (n2avc_inter_slice_begin(&t.s) == NULL) {
		a->bad = 1;
		return NULL;
	}
	for (y = a->first; y < a->last; y++)
		for (x = 0; x < a->mb_w; x++) {
			int ret = n2avc_p_mb(&t.s, x, y);

			if (ret == 2) {
				n2avc_flush_skip_run(&t.s);
				n2avc_intra_mb(&t.s, x, y);
			} else if (ret != 0 && ret != 1) {
				a->bad = 1;
			}
		}
	n2avc_inter_slice_end(&t.s);
	return NULL;
}

static void
test_threads(void)
{
	enum { MW = 4, MH = 8, NT = 4, ROWS = MH / NT };
	tframe src, ref, rec;
	n2avc_mbinfo mb[MW * MH];
	pthread_t th[NT];
	thread_arg args[NT];
	int i, bad = 0;

	CHECK(tf_alloc(&src, MW, MH) && tf_alloc(&ref, MW, MH) && tf_alloc(&rec, MW, MH),
	      "threads frame allocation");
	tf_fill_noise(&ref, 41);
	tf_shift(&src, &ref, 1, 1);
	tf_fill_const(&rec, 0, 0, 0);
	init_mb_intra(mb, MW * MH);

	for (i = 0; i < NT; i++) {
		args[i].src = &src;
		args[i].ref = &ref;
		args[i].rec = &rec;
		args[i].mb = mb;
		args[i].mb_w = MW;
		args[i].mb_h = MH;
		args[i].first = i * ROWS;
		args[i].last = (i + 1) * ROWS;
		args[i].qp = 26;
		args[i].range = 16;
		args[i].bad = 0;
		CHECK(pthread_create(&th[i], NULL, thread_fn, &args[i]) == 0,
		      "pthread_create %d", i);
	}
	for (i = 0; i < NT; i++)
		pthread_join(th[i], NULL);
	for (i = 0; i < NT; i++)
		bad |= args[i].bad;
	CHECK(bad == 0, "a slice thread reported a bad return or allocation");
	for (i = 0; i < MW * MH; i++) {
		CHECK(mb[i].type == N2MB_P16 || mb[i].type == N2MB_PSKIP || mb[i].type == N2MB_I16,
		      "threads mbinfo %d type %u", i, mb[i].type);
		if (mb[i].ref >= 0)
			CHECK((mb[i].mvx & 3) == 0 && (mb[i].mvy & 3) == 0,
			      "threads mv %d not whole-sample", i);
	}

	tf_free(&src);
	tf_free(&ref);
	tf_free(&rec);
}

/* ------------------------------------------------------------------------- */
/* Rate control                                                              */
/* ------------------------------------------------------------------------- */

static void
rc_config(n2avc_config *c, uint32_t bitrate, uint32_t qp)
{
	memset(c, 0, sizeof(*c));
	c->size = (uint32_t)sizeof(*c);
	c->width = 640;
	c->height = 480;
	c->fps_num = 30;
	c->fps_den = 1;
	c->bitrate = bitrate;
	c->qp = qp;
	c->keyint = 60;
}

static void
test_rc_basics(void)
{
	n2avc_config cfg;
	n2avc_rc *rc;
	n2avc_rc *hi, *lo;
	n2avc_config ch, cl;

	rc_config(&cfg, 0, 33);
	rc = n2avc_rc_open(&cfg);
	CHECK(rc != NULL, "rc_open constant");
	if (rc != NULL) {
		CHECK(n2avc_rc_picture_qp(rc, 0) == 33, "constant QP P");
		CHECK(n2avc_rc_picture_qp(rc, 1) == 33, "constant QP IDR");
		n2avc_rc_picture_done(rc, 0, 33, 1000);
		CHECK(n2avc_rc_picture_qp(rc, 0) == 33, "constant QP changed after picture_done");
		n2avc_rc_reconfigure(rc, 0, 0, 0);
		CHECK(n2avc_rc_picture_qp(rc, 0) == 33, "constant QP changed after reconfigure(0,0,0)");
		n2avc_rc_close(rc);
	}

	rc_config(&cfg, 0, 0);
	rc = n2avc_rc_open(&cfg);
	CHECK(rc != NULL, "rc_open constant default");
	if (rc != NULL) {
		CHECK(n2avc_rc_picture_qp(rc, 1) == 26, "constant QP default qp 0 -> 26");
		n2avc_rc_close(rc);
	}

	rc_config(&cfg, 1000000, 26);
	rc = n2avc_rc_open(&cfg);
	CHECK(rc != NULL, "rc_open rate");
	if (rc != NULL) {
		int p = n2avc_rc_picture_qp(rc, 0);
		int i = n2avc_rc_picture_qp(rc, 1);

		CHECK(p == 26, "configured first QP %d, expected 26", p);
		CHECK(i == 24, "IDR QP %d, expected QP_P - 2 = 24", i);
		n2avc_rc_close(rc);
	}

	rc_config(&ch, 8000000, 0);
	rc_config(&cl, 200000, 0);
	hi = n2avc_rc_open(&ch);
	lo = n2avc_rc_open(&cl);
	CHECK(hi != NULL && lo != NULL, "rc_open bpp pair");
	if (hi != NULL && lo != NULL) {
		int qh = n2avc_rc_picture_qp(hi, 0);
		int ql = n2avc_rc_picture_qp(lo, 0);

		CHECK(qh < ql, "first QP from bpp: high bitrate %d should be below low bitrate %d", qh, ql);
		CHECK(qh >= 10 && ql <= 51, "first QP out of range (%d,%d)", qh, ql);
		/* The exact value of the table is a calibration choice (the
		 * table is set so that the first IDR, coded at QP_P - 2,
		 * lands near its 4x budget on high-entropy content), so only
		 * the monotone shape and the range are asserted here; the
		 * closed-loop test below judges the behaviour. */
		CHECK(qh >= 10 && qh <= 51 && ql >= 10 && ql <= 51,
		      "8 Mbit/s first QP %d, 200 kbit/s first QP %d out of range", qh, ql);
	}
	n2avc_rc_close(hi);
	n2avc_rc_close(lo);

	/* The task's 1 Mbit/s 640x480p30 case (bpp = 0.1085): the first P QP
	 * must be inside the legal range and the first IDR exactly two
	 * coarser (n2avc_int.h), and the table must be monotone in bpp: a
	 * lower rate can only give a coarser first QP. */
	rc_config(&cfg, 1000000, 0);
	rc = n2avc_rc_open(&cfg);
	CHECK(rc != NULL, "rc_open 1 Mbit/s");
	if (rc != NULL) {
		int p = n2avc_rc_picture_qp(rc, 0);
		int i = n2avc_rc_picture_qp(rc, 1);

		CHECK(p >= 10 && p <= 51, "1 Mbit/s first P QP %d out of range", p);
		CHECK(i == (p - 2 < 10 ? 10 : p - 2), "1 Mbit/s first IDR QP %d, expected QP_P - 2", i);
		n2avc_rc_close(rc);
	}
}

/* An independent picture-size model for the closed loop: 84 000 bytes at
 * QP 26, halving every 6 QP.  It knows nothing about the controller. */
static size_t
model_bytes(int qp)
{
	double v = 84000.0 * pow(2.0, -(double)qp / 6.0);
	long r = lround(v);

	if (r < 1)
		r = 1;
	return (size_t)r;
}

static int
rc_sim(uint32_t bitrate0, uint32_t bitrate1, int n, int *qps, size_t *total)
{
	n2avc_config cfg;
	n2avc_rc *rc;
	int i;
	size_t sum = 0;

	rc_config(&cfg, bitrate0, 0);
	rc = n2avc_rc_open(&cfg);
	if (rc == NULL)
		return 0;
	for (i = 0; i < n; i++) {
		int idr = (i % 60) == 0;
		int qp = n2avc_rc_picture_qp(rc, idr);
		size_t bytes;

		if (i == 30 && bitrate1 != 0)
			n2avc_rc_reconfigure(rc, bitrate1, 0, 0);
		bytes = model_bytes(qp);
		qps[i] = qp;
		sum += bytes;
		n2avc_rc_picture_done(rc, idr, qp, bytes);
	}
	n2avc_rc_close(rc);
	*total = sum;
	return 1;
}

static void
test_rc_closed_loop(void)
{
	enum { N = 60 };
	int qps[N], qps2[N];
	size_t total = 0, total2 = 0;
	size_t window = 0;
	int i;

	CHECK(rc_sim(1000000, 0, N, qps, &total), "rc simulation");
	CHECK(total >= 200000 && total <= 300000,
	      "1 Mbit/s x 30 fps x 60 pictures: %zu bytes, expected 250000 +-20%%", total);
	for (i = 0; i < N; i++)
		CHECK(qps[i] >= 10 && qps[i] <= 51, "QP[%d] = %d out of range", i, qps[i]);
	for (i = 2; i < N; i++) {
		if (qps[i] != 0 && i % 60 != 0 && (i - 1) % 60 != 0)
			CHECK(abs(qps[i] - qps[i - 1]) <= 4,
			      "QP step %d -> %d at picture %d exceeds 4", qps[i - 1], qps[i], i);
	}
	for (i = 30; i < N; i++)
		window += model_bytes(qps[i]);
	CHECK(window >= 93750 && window <= 156250,
	      "pictures 30..59: %zu bytes, expected 125000 +-25%%", window);

	/* Deterministic: the same input twice gives the same QP sequence. */
	CHECK(rc_sim(1000000, 0, N, qps2, &total2), "rc simulation repeat");
	CHECK(total == total2, "rc total is not deterministic: %zu vs %zu", total, total2);
	for (i = 0; i < N; i++)
		CHECK(qps[i] == qps2[i], "QP[%d] is not deterministic: %d vs %d", i, qps[i], qps2[i]);

	/* Reconfigure from 1 to 2 Mbit/s halfway: QP must fall and steps stay <=4. */
	CHECK(rc_sim(1000000, 2000000, N, qps2, &total2), "rc simulation reconfigure");
	for (i = 31; i < N; i++)
		if (i % 60 != 0 && (i - 1) % 60 != 0)
			CHECK(abs(qps2[i] - qps2[i - 1]) <= 4,
			      "reconfigure QP step %d -> %d at picture %d exceeds 4",
			      qps2[i - 1], qps2[i], i);
	CHECK(qps2[N - 1] < qps2[29],
	      "after 2 Mbit/s reconfigure the last QP %d should be below the 1 Mbit/s QP %d",
	      qps2[N - 1], qps2[29]);

	{
		int lo = qps[0], hi = qps[0];

		for (i = 1; i < N; i++) {
			if (qps[i] < lo)
				lo = qps[i];
			if (qps[i] > hi)
				hi = qps[i];
		}
		printf("  rc closed loop: 60 pictures 640x480 1 Mbit/s 30 fps -> %zu bytes"
		       " (target 250000, %+.1f%%), QP %d..%d\n",
		       total, 100.0 * ((double)total - 250000.0) / 250000.0, lo, hi);
	}
}

/* ------------------------------------------------------------------------- */

int
main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "default";

	test_tables();
	test_pmv();
	test_skip_mv();
	test_predict();
	test_predict_literals();
	test_me_shift();
	test_static_skip();
	test_force_skip();
	test_intra_decision();
	test_bounds();
	test_threads();
	test_rc_basics();
	test_rc_closed_loop();

	if (failures != 0) {
		printf("N2TEST WP04 BIN %s FAIL %d/%d checks\n", name, failures, checks);
		return 1;
	}
	printf("N2TEST WP04 BIN %s PASS %d/%d\n", name, checks, checks);
	return 0;
}
