/*
 * gfx200: rw_gfx200_pure.h (the decisions Nord2Metal runs) and the library's reading of rwa_draw.bc_pad (air_metal.c
 * rwa_render_draw: tag check, rtt mask, depth/stencil DontCare bits), plus a model of the render-target pool's rules
 * (rw_g2_rt_take / rw_g2_rt_park / rw_g2_rt_sweep: same shape only, never a buffer the GPU may still use, 2 per shape, budget).
 * cc -O1 -g -fsanitize=address,undefined rw_gfx200_host_test.c && ./a.out  -> 'rw_gfx200: ok'
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rw_gfx200_pure.h"

#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); exit(1); } } while (0)

/* the library side, as air_metal.c reads it */
static unsigned
lib_g2(unsigned bc_magic_ok, unsigned fb_ok, unsigned bc_pad)
{
	return (bc_magic_ok && fb_ok && (bc_pad >> 16) == 0x5254u) ? (bc_pad & 0xffffu) : 0u;
}

static struct rwg2_tex
ms(unsigned fmt, unsigned w, unsigned h)
{
	struct rwg2_tex t = { fmt, RWG2_TYPE_2D_MS, w, h, 1, 1, 1, 4, RWG2_USAGE_RT, true, true };
	return t;
}

static struct rwg2_tex
one(unsigned fmt, unsigned w, unsigned h, unsigned usage)
{
	struct rwg2_tex t = { fmt, RWG2_TYPE_2D, w, h, 1, 1, 1, 1, usage, true, true };
	return t;
}

/* ---- pool model: entries (shape, busy-until seq), same rules as rw_g2_rt_* ---- */
struct ent { struct rwg2_tex s; unsigned long mark; size_t bytes; unsigned long parked; int live; };
#define NE 64
static struct ent pool[NE];
static unsigned long completed, submitted, clock_ns;
static size_t held;
#define MAXB (256u << 20)
#define PER 2u
#define AGE 1500u

static int safe(unsigned long mark) { (void)mark; return 1; }   /* freed only after its command buffers completed (see redwoodmetal.m) */

static void
sweep(int all)
{
	/* newest first == highest index first in this model */
	size_t bytes = 0;
	for (int i = NE - 1; i >= 0; i--) {
		if (!pool[i].live)
			continue;
		unsigned same = 0;
		for (int j = NE - 1; j > i; j--)
			same += pool[j].live && rwg2_same_shape(&pool[j].s, &pool[i].s);
		bytes += pool[i].bytes;
		if ((all || clock_ns - pool[i].parked > AGE || bytes > MAXB || same >= PER) && safe(pool[i].mark)) {
			pool[i].live = 0;
			held -= pool[i].bytes;
			bytes -= pool[i].bytes;
		}
	}
}

static int
park(struct rwg2_tex s)
{
	size_t b = rwg2_tex_bytes(&s);
	if (!rwg2_rtpool_eligible(&s) || b > MAXB / 2)
		return 0;
	/* compact: shift to keep newest at the top */
	int k = -1;
	for (int i = 0; i < NE; i++)
		if (!pool[i].live) { k = i; }
	if (k < 0)
		return 0;
	memmove(&pool[k], &pool[k + 1], (NE - 1 - k) * sizeof(pool[0]));
	pool[NE - 1] = (struct ent){ s, submitted, b, clock_ns, 1 };
	held += b;
	sweep(0);
	return 1;
}

static int
take(struct rwg2_tex s)
{
	if (!rwg2_rtpool_eligible(&s))
		return 0;
	for (int i = NE - 1; i >= 0; i--) {
		if (pool[i].live && rwg2_same_shape(&pool[i].s, &s) && safe(pool[i].mark)) {
			pool[i].live = 0;
			held -= pool[i].bytes;
			return 1;
		}
	}
	return 0;
}

int
main(void)
{
	/* ---- rwg2_rtt_ok ---- */
	struct rwg2_tex f = ms(81, 1125, 2436), t = one(81, 1125, 2436, 0x5);
	CHECK(rwg2_rtt_ok(&f, &t, 2, RWG2_STORE_RESOLVE, 0, 0, 0, 0, 5) == RWG2_RTT_OK, "Maps pass (clear, resolve)");
	CHECK(rwg2_rtt_ok(&f, &t, 0, RWG2_STORE_RESOLVE, 0, 0, 0, 0, 5) == RWG2_RTT_OK, "dontcare load");
	CHECK(rwg2_rtt_ok(&f, &t, 1, RWG2_STORE_RESOLVE, 0, 0, 0, 0, 5) == RWG2_RTT_LOADS, "Load reads the samples");
	CHECK(rwg2_rtt_ok(&f, &t, 2, 3, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_RESOLVE, "store+resolve keeps the samples");
	CHECK(rwg2_rtt_ok(&f, &t, 2, 1, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_RESOLVE, "plain store");
	CHECK(rwg2_rtt_ok(&f, &t, 2, 4, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_RESOLVE, "unknown store");
	CHECK(rwg2_rtt_ok(&f, &t, 2, 2, 0, 0, 0, 0, 0) == RWG2_RTT_NO_DRAWS, "clear-only pass keeps the blit");
	CHECK(rwg2_rtt_ok(NULL, &t, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_OURS, "foreign source");
	CHECK(rwg2_rtt_ok(&f, NULL, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_OURS, "foreign target");
	{ struct rwg2_tex v = t; v.plain = false; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_OURS, "view target"); }
	{ struct rwg2_tex v = t; v.has_mesa = false; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_OURS, "no mesa"); }
	{ struct rwg2_tex v = f; v.sample_count = 2; CHECK(rwg2_rtt_ok(&v, &t, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_SHAPE, "2 samples"); }
	{ struct rwg2_tex v = f; v.type = RWG2_TYPE_2D; CHECK(rwg2_rtt_ok(&v, &t, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_SHAPE, "source not MS"); }
	{ struct rwg2_tex v = t; v.type = 3; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_SHAPE, "array target"); }
	{ struct rwg2_tex v = t; v.sample_count = 4; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_SHAPE, "MS target"); }
	{ struct rwg2_tex v = t; v.array_length = 2; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_SHAPE, "layers"); }
	{ struct rwg2_tex v = t; v.width = 1124; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_SIZE, "size"); }
	{ struct rwg2_tex v = t; v.pixel_format = 80; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_FORMAT, "format"); }
	CHECK(rwg2_rtt_ok(&f, &t, 2, 2, 0, 0, 1, 0, 5) == RWG2_RTT_LEVEL, "resolve level");
	CHECK(rwg2_rtt_ok(&f, &t, 2, 2, 0, 0, 0, 1, 5) == RWG2_RTT_LEVEL, "resolve slice");
	CHECK(rwg2_rtt_ok(&f, &t, 2, 2, 0, 1, 0, 0, 5) == RWG2_RTT_LEVEL, "source slice");
	{ struct rwg2_tex v = t; v.usage = 0x1; CHECK(rwg2_rtt_ok(&f, &v, 2, 2, 0, 0, 0, 0, 5) == RWG2_RTT_NOT_RT, "not RT"); }
	/* exhaustive: OK only when every condition holds */
	unsigned oks = 0, total = 0;
	for (unsigned load = 0; load < 3; load++)
		for (unsigned store = 0; store < 6; store++)
			for (unsigned lv = 0; lv < 2; lv++)
				for (unsigned sc = 1; sc <= 8; sc *= 2)
					for (unsigned u = 0; u < 32; u++)
						for (unsigned nd = 0; nd < 2; nd++) {
							struct rwg2_tex a = f, b = t;
							a.sample_count = sc;
							b.usage = u;
							int r = rwg2_rtt_ok(&a, &b, load, store, lv, 0, 0, 0, nd);
							int want = store == 2 && load != 1 && nd && sc == 4 && !lv && (u & 4);
							CHECK((r == 0) == want, "exhaustive");
							oks += r == 0;
							total++;
						}
	/* ---- bc_pad round trip through the library's reading ---- */
	for (unsigned m = 0; m < 256; m++)
		for (unsigned z = 0; z < 2; z++)
			for (unsigned s = 0; s < 2; s++) {
				unsigned w = rwg2_bcpad(m, z, s), g = lib_g2(1, 1, w);
				CHECK((g & 0xff) == m && !!(g & 0x1000) == z && !!(g & 0x2000) == s, "bcpad round trip");
				CHECK((w == 0) == (m == 0 && !z && !s), "zero when nothing asked");
				CHECK(lib_g2(0, 1, w) == 0 && lib_g2(1, 0, w) == 0, "magics required");
			}
	CHECK(lib_g2(1, 1, 0x12340001u) == 0, "a foreign word is nothing");
	CHECK(lib_g2(1, 1, 0) == 0, "an old driver's zero is nothing");
	/* ---- rtpool eligibility ---- */
	struct rwg2_tex d = ms(260, 1125, 2436);   /* Depth32Float_Stencil8 4x, usage RT */
	CHECK(rwg2_rtpool_eligible(&f) && rwg2_rtpool_eligible(&d), "Maps pair eligible");
	{ struct rwg2_tex v = f; v.usage = 0x5; CHECK(!rwg2_rtpool_eligible(&v), "shader-read never pooled"); }
	{ struct rwg2_tex v = f; v.usage = 0x6; CHECK(!rwg2_rtpool_eligible(&v), "shader-write never pooled"); }
	{ struct rwg2_tex v = f; v.usage = 0x14; CHECK(rwg2_rtpool_eligible(&v), "RT + PFV pooled"); }
	{ struct rwg2_tex v = one(80, 256, 256, 4); CHECK(!rwg2_rtpool_eligible(&v), "small 1x not pooled"); }
	{ struct rwg2_tex v = one(80, 1125, 2436, 4); CHECK(rwg2_rtpool_eligible(&v), "big 1x RT pooled"); }
	{ struct rwg2_tex v = f; v.usage = 0; CHECK(!rwg2_rtpool_eligible(&v), "no usage"); }
	/* ---- pool model: Maps' churn (make pair, draw, free) 15 x a second for 4 s, the GPU 1-3 frames behind ---- */
	unsigned hits = 0, misses = 0;
	srand(7);
	for (unsigned frame = 0; frame < 60 * 4; frame++) {
		clock_ns += 16;
		completed = submitted > 2 ? submitted - (unsigned long)(rand() % 3) : 0;
		if (completed > submitted)
			completed = submitted;
		int a = take(f), b = take(d);
		hits += a + b;
		misses += !a + !b;
		submitted++;
		park(f);
		park(d);
		CHECK(held <= MAXB, "budget");
		unsigned nf = 0;
		for (int i = 0; i < NE; i++)
			nf += pool[i].live && rwg2_same_shape(&pool[i].s, &f);
		CHECK(nf <= PER + 1, "per shape (busy ones may wait one sweep)");
	}
	CHECK(hits > misses * 10, "mostly hits");
	/* idle: everything goes after its age once the GPU is done */
	completed = submitted;
	clock_ns += AGE + 1;
	sweep(0);
	CHECK(held == 0, "idle sweep empties");
	park(f);
	CHECK(take(f), "taken");
	CHECK(!take(f), "only once");
	park(d);
	sweep(1);
	CHECK(held == 0, "allocation refusal empties");
	printf("rw_gfx200: ok (rtt cases %u of %u ok, pool hits %u misses %u)\n", oks, total, hits, misses);
	return 0;
}
