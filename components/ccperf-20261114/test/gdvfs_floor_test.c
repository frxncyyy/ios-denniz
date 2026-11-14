/* ccperf-20261114: host test of 0492 (GPU DVFS "never below the cold-start OPP").
 *  1. min_idx = 0 is 0461 exactly: 2,000,000 random polls (busy, boost, thermal level, acting or not, rails), every output equal.
 *  2. min_idx = k: never below min(k, thermal cap), never above the rails or the thermal cap; identical to 0461 whenever 0461's answer is >= k.
 *     (run with and without the 80 % UI threshold)
 *  3. scenes with the phone's numbers (cold start 654 MHz, rails up to 836 MHz): idle stays at 654 (0461: 350), the Control Center load goes to
 *     the cap and comes back to 654, the thermal ladder still pulls below 654 and the clock returns to 654 afterwards; fewer switches than 0461. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
void old_reset(unsigned, unsigned); void old_step(unsigned, uint64_t, int, unsigned, int, unsigned[5]);
void new_reset(unsigned, unsigned); void new_min(unsigned, unsigned); void new_step(unsigned, uint64_t, int, unsigned, int, unsigned[5]);
static const uint32_t khz[6] = { 350000u, 512000u, 654000u, 805000u, 836000u, 886000u };
static const uint32_t therm_khz[5] = { 886000u, 805000u, 654000u, 512000u, 350000u };
static uint64_t rs = 0x9e3779b97f4a7c15ull;
static unsigned rnd(unsigned n) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)((rs >> 11) % n); }
static unsigned capidx(unsigned max_idx, unsigned level) { unsigned c = max_idx; while (c > 0 && khz[c] > therm_khz[level]) c--; return c; }

static void test_equal(void)
{
	for (unsigned run = 0; run < 2000; run++) {
		unsigned max_idx = rnd(6), cur = rnd(max_idx + 1), level = rnd(5), busy = rnd(1001);
		int act = (int)rnd(2), boost = 0;
		uint64_t now = 1000 + rnd(1000);
		old_reset(max_idx, cur); new_reset(max_idx, cur); new_min(0, 0);
		for (unsigned i = 0; i < 1000; i++) {
			unsigned a[5], b[5];
			if (rnd(8) == 0) busy = rnd(1001); else if (rnd(3) == 0) busy = (busy + rnd(120) > 1060 ? 1000 : (busy + rnd(120) < 60 ? 0 : busy + rnd(120) - 60));
			if (rnd(40) == 0) boost = !boost;
			if (rnd(300) == 0) level = rnd(5);
			now += 20 + (rnd(50) == 0 ? rnd(200) : 0);
			old_step(busy, now, boost, level, act, a); new_step(busy, now, boost, level, act, b);
			for (unsigned k = 0; k < 5; k++) assert(a[k] == b[k]);
		}
	}
	printf("0492 min_idx 0 == 0461: 2000000 polls identical\n");
}

static void test_floor(void)
{
	unsigned long below_old = 0, same = 0, polls = 0;
	for (unsigned run = 0; run < 2000; run++) {
		unsigned max_idx = rnd(6), k = rnd(max_idx + 1), level = rnd(5), busy = rnd(1001);
		int boost = 0;
		uint64_t now = 1000 + rnd(1000);
		old_reset(max_idx, k); new_reset(max_idx, k); new_min(k, rnd(2) ? 800u : 0u);
		for (unsigned i = 0; i < 1000; i++) {
			unsigned a[5], b[5];
			if (rnd(8) == 0) busy = rnd(1001);
			if (rnd(40) == 0) boost = !boost;
			if (rnd(300) == 0) level = rnd(5);
			now += 20;
			unsigned cap = capidx(max_idx, level), lo = k < cap ? k : cap;
			old_step(busy, now, boost, level, 1, a); new_step(busy, now, boost, level, 1, b);
			polls++;
			assert(b[2] == cap && b[0] <= cap && b[0] <= max_idx);          /* never above the rails or the thermal cap */
			assert(b[3] >= lo && b[3] <= cap);                               /* the floor holds, and never lifts the cap */
			if (b[4] >= lo) assert(b[0] >= lo);                              /* at or above the floor: never asked below it */
			else assert(b[0] >= b[4]);                                       /* below it (the cap was lower a moment ago): only upwards */
			assert(b[4] <= max_idx);
			if (a[0] < lo) below_old++;
			if (a[0] == b[0]) same++;
		}
	}
	assert(below_old > 1000);      /* the test did reach states where 0461 goes below the floor */
	printf("0492 floor: %lu polls, never below min(floor, thermal cap), never above the cap; 0461 was below the floor on %lu polls, same answer on %lu\n", polls, below_old, same);
}

struct sim { uint64_t now; unsigned sw_old, sw_new, min_old, min_new, max_new, cur_old, cur_new; };
/* work = busy share at 654 MHz in permille (it scales with 1/clock, as dvfs_sim_test does); both policies act */
static void run(struct sim *s, unsigned ms, unsigned work654, int boost, unsigned level)
{
	for (unsigned t = 0; t < ms; t += 20) {
		unsigned a[5], b[5], bo, bn;
		s->now += 20;
		bo = (unsigned)((uint64_t)work654 * 654000u / khz[s->cur_old]); bn = (unsigned)((uint64_t)work654 * 654000u / khz[s->cur_new]);
		old_step(bo > 1000 ? 1000 : bo, s->now, boost, level, 1, a); new_step(bn > 1000 ? 1000 : bn, s->now, boost, level, 1, b);
		if (a[4] != s->cur_old) s->sw_old++;
		if (b[4] != s->cur_new) s->sw_new++;
		s->cur_old = a[4]; s->cur_new = b[4];
		if (s->cur_old < s->min_old) s->min_old = s->cur_old;
		if (s->cur_new < s->min_new) s->min_new = s->cur_new;
		if (s->cur_new > s->max_new) s->max_new = s->cur_new;
	}
}
static void start(struct sim *s) { old_reset(4, 2); new_reset(4, 2); new_min(2, 800); s->now = 5000; s->sw_old = s->sw_new = 0; s->min_old = s->min_new = s->cur_old = s->cur_new = s->max_new = 2; }

static void test_scenes(void)
{
	struct sim s;
	/* idle, then light UI without a touch (no boost for the first 100 ms), idle again, ten times */
	start(&s);
	for (unsigned k = 0; k < 10; k++) { run(&s, 2000, 0, 0, 0); run(&s, 100, 300, 0, 0); run(&s, 900, 300, 1, 0); }
	assert(s.min_new == 2 && s.sw_new == 0);                       /* 0492: the clock never moved */
	assert(s.min_old == 0 && s.sw_old >= 20);                      /* 0461: down to 350 MHz and back for every burst */
	printf("0492 scene idle/light UI x10: boost-only 0 switches, always 654 MHz; 0461 %u switches, down to %u MHz\n", s.sw_old, khz[s.min_old] / 1000);
	/* Control Center over the Photos grid as recorded (busy 850 permille at 654 MHz), cool phone: to the cap (836 MHz) within 100 ms; in this
	 * model the work scales with the clock, so 0461's "lowest OPP under 75 %" rule then settles at 805 MHz (69 %); back to 654 when it ends */
	start(&s);
	run(&s, 1000, 0, 0, 0); run(&s, 100, 850, 1, 0);
	assert(khz[s.cur_new] == 836000 && s.sw_new == 1);
	run(&s, 4900, 850, 1, 0);
	assert(khz[s.cur_new] == 805000 && s.sw_new == 2 && s.max_new == 4);
	assert(khz[s.cur_old] == 654000 && s.sw_old == 2);             /* 0461 alone: 350 when idle, back to 654 for the UI, and a steady 85 % never reaches its 88 % */
	run(&s, 3000, 0, 0, 0);
	assert(khz[s.cur_new] == 654000 && s.sw_new == 3 && s.min_new == 2 && khz[s.cur_old] == 350000);
	/* the same at thermal level 1 (41..43 C): 805 MHz */
	start(&s);
	run(&s, 5000, 850, 1, 1);
	assert(khz[s.cur_new] == 805000);
	/* level 2 (43..45 C): the cap is the cold-start OPP, nothing moves; level 3 and 4: below it (the cap wins), and back when it cools */
	run(&s, 2000, 850, 1, 2); assert(khz[s.cur_new] == 654000);
	run(&s, 2000, 850, 1, 3); assert(khz[s.cur_new] == 512000);
	run(&s, 2000, 850, 1, 4); assert(khz[s.cur_new] == 350000);
	run(&s, 2000, 0, 0, 2);   assert(khz[s.cur_new] == 654000);    /* idle and cooler: back to the floor, not left at 350 */
	run(&s, 2000, 0, 0, 0);   assert(khz[s.cur_new] == 654000 && s.max_new == 3 && s.min_new == 0);
	/* the UI threshold is only for a UI that produces frames: the same 85 % without boost (a background compute load) follows the 88 % rule */
	start(&s);
	run(&s, 5000, 850, 0, 0); assert(khz[s.cur_new] == 654000 && s.sw_new == 0);
	run(&s, 100, 900, 0, 0); assert(khz[s.cur_new] == 836000);
	/* no flip-flop at the edge: 80 % at 654 MHz is 65 % at 805 MHz, which projects to 80 % at 654 MHz: up (cap, then 805) and it stays there */
	start(&s);
	run(&s, 20000, 800, 1, 0); assert(khz[s.cur_new] == 805000 && s.sw_new == 2 && s.min_new == 2);
	start(&s);
	run(&s, 20000, 790, 1, 0); assert(khz[s.cur_new] == 654000 && s.sw_new == 0);
	printf("0492 scenes: CC load (85 %% busy) -> cap 836 MHz, settles 805 (805 at level 1; 0461 alone stays at 654) -> 654 when idle; thermal levels 3/4 still go to 512/350 and return to 654\n");
}

int main(void)
{
	test_equal();
	test_floor();
	test_scenes();
	printf("GDVFS_FLOOR_OK\n");
	return 0;
}
