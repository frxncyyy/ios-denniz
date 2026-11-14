/* 0631 host test of n2g_gpu_decide_fast (nord2_gov.h): scenarios + 2,000,000 random polls with properties; the 0461/0492 policy is
 * untouched (its own suite: ccperf-20261114/test). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "nord2_gov.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d %s (i=%lu)\n", __func__, __LINE__, #x, it); exit(1); } } while (0)
static unsigned long it;
static uint64_t rs = 88172645463325252ull;
static unsigned rnd(unsigned n) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs % n); }
static unsigned step(struct n2g_gpu *g, struct n2g_gfast *f, uint64_t ms, unsigned pm, bool boost, unsigned th, unsigned depth) {
	n2g_gpu_sample(g, pm);
	unsigned w = n2g_gpu_decide_fast(g, f, ms, boost, th, depth);
	if (w != g->cur) n2g_gpu_applied(g, w, ms);
	return w;
}
int main(void) {
	/* rails cover 836 (idx 4), cold start 654 (idx 2) */
	struct n2g_gpu g = {0}; struct n2g_gfast f = {0}; g.max_idx = 4; g.min_idx = 2; g.cur = 2;
	uint64_t ms = 1000;
	CHECK(step(&g, &f, ms, 100, false, 0, 0) == 2);                 /* idle: stays at the floor */
	ms += 20; CHECK(step(&g, &f, ms, 520, false, 0, 0) == 4);        /* one hot sample: straight to 836 */
	ms += 20; CHECK(step(&g, &f, ms, 100, false, 0, 0) == 4);        /* no immediate drop */
	for (int k = 0; k < 23; k++) { ms += 20; CHECK(step(&g, &f, ms, 100, false, 0, 0) == 4); }   /* 480 ms */
	ms += 20; CHECK(step(&g, &f, ms, 100, false, 0, 0) == 2);        /* 500 ms cold: back to 654 */
	ms += 20; CHECK(step(&g, &f, ms, 320, false, 0, 7) == 2);        /* rate limit 40 ms after the switch */
	ms += 20; CHECK(step(&g, &f, ms, 320, false, 0, 7) == 4);        /* 32 % with 7 queued: hot */
	for (int k = 0; k < 40; k++) { ms += 20; step(&g, &f, ms, 10, false, 0, 0); }
	CHECK(g.cur == 2);
	ms += 20; CHECK(step(&g, &f, ms, 360, true, 0, 0) == 4);         /* UI boost at 36 % */
	ms += 20; CHECK(step(&g, &f, ms, 900, false, 1, 0) == 3);        /* 41 C: cap 805 at once */
	ms += 20; CHECK(step(&g, &f, ms, 900, false, 2, 0) == 2);        /* 43 C: 654 */
	ms += 20; CHECK(step(&g, &f, ms, 900, false, 3, 0) == 1);        /* 45 C: 512, below the floor */
	ms += 20; CHECK(step(&g, &f, ms, 0, false, 3, 0) == 1);
	f.perf = true; for (int k = 0; k < 3; k++) { ms += 20; step(&g, &f, ms, 0, false, 0, 0); }
	CHECK(g.cur == 4);                                              /* performance: the cap even when idle */
	ms += 20; CHECK(step(&g, &f, ms, 0, false, 2, 0) == 2);
	/* random properties */
	for (it = 0; it < 2000000; it++) {
		if (rnd(5000) == 0) { g = (struct n2g_gpu){0}; f = (struct n2g_gfast){0}; g.max_idx = (uint8_t)(2 + rnd(4)); g.min_idx = (uint8_t)rnd(3); g.cur = (uint8_t)rnd(g.max_idx + 1); f.perf = rnd(4) == 0; }
		unsigned th = rnd(100) < 90 ? 0 : rnd(5), pm = rnd(1001), depth = rnd(20); bool boost = rnd(3) == 0;
		unsigned before = g.cur; uint64_t last = g.last_switch_ms, hot_before = f.hot_ms;
		ms += 20;
		unsigned w = step(&g, &f, ms, pm, boost, th, depth);
		CHECK(w <= g.cap && g.cap <= g.max_idx && n2g_gpu_khz[g.cap] <= n2g_gpu_therm_khz[th]);
		bool hot = f.perf || pm >= 500 || (depth >= 6 && pm >= 300) || (boost && pm >= 350);
		if (w < before) {
			CHECK(before > g.cap || (!hot && ms - hot_before >= 500 && ms - last >= 500));   /* down only for heat or after 500 ms cold */
			CHECK(w == g.cap || w == g.floor);
		}
		if (w > before) CHECK(ms - last >= 40 && (w == g.cap || w == g.floor));
		if (hot && before < g.cap && ms - last >= 40) CHECK(w == g.cap);                     /* up within one poll */
		if (before > g.cap) CHECK(w == g.cap);
	}
	printf("GFAST_HOST_TEST_OK (scenarios + %lu random polls)\n", it);
	return 0;
}
