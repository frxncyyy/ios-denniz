/* cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../b/iokit/Kernel/arm pace_host_test.c -o pace_host_test
 * 0397 host test: sweet_fb_pace.h (pacing counters, 90 Hz guard). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sweet_fb_pace.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void
steady(uint64_t P, uint64_t start, double secs, struct sweet_fb_pace *p)
{
	uint64_t n = (uint64_t)(secs * 1e6 / (double)P);
	for (uint64_t i = 0; i <= n; i++) {
		uint64_t t = start + i * P;
		sweet_fb_pace_arrival(p, t - 3000);
		sweet_fb_pace_push(p, t, P, t - 3000);
	}
}

int
main(void)
{
	struct sweet_fb_pace p;
	char buf[768];

	/* 1. fixed 90: every refresh presented */
	sweet_fb_pace_reset(&p);
	steady(11226, 5000000, 3.0, &p);
	sweet_fb_pace_push(&p, 9000000, 11226, 0);          /* closes the last bucket after an idle gap */
	CHECK(p.dropped == 0, "steady 90 dropped %llu", (unsigned long long)p.dropped);
	CHECK(p.k[0] == p.active && p.active > 260, "steady 90 k1 %u active %u", p.k[0], p.active);
	CHECK(p.idle == 1, "idle %u", p.idle);
	uint32_t f = sweet_fb_pace_fps10(&p);
	CHECK(f >= 889 && f <= 892, "steady 90 fps10 %u", f);
	CHECK(p.busy_secs >= 2 && p.rate[6] == p.busy_secs, "steady 90 busy %u rate88+ %u (rate %u/%u/%u/%u/%u/%u/%u)", p.busy_secs, p.rate[6],
	    p.rate[0], p.rate[1], p.rate[2], p.rate[3], p.rate[4], p.rate[5], p.rate[6]);
	CHECK(p.ivl[0] == p.active, "ivl <=12.5 %u", p.ivl[0]);
	CHECK(p.arr[0] >= p.active, "arr bins %u", p.arr[0]);
	CHECK(p.best_run == p.active, "best_run %u", p.best_run);
	int n = sweet_fb_pace_format(&p, buf, 640, 11226, 11226, 1);
	CHECK(n > 0 && n < 640, "format length %d", n);
	CHECK(strstr(buf, "panel_hz=89.1") && strstr(buf, "k1_permille=1000") && strstr(buf, "dropped=0 "), "format: %s", buf);
	printf("90: %s\n", buf);

	/* 2. fixed 60 on a 59.4 Hz panel: k == 1 too, but panel_hz and the rate bins tell it apart */
	sweet_fb_pace_reset(&p);
	steady(16830, 1000000, 2.5, &p);
	sweet_fb_pace_push(&p, 9000000, 16830, 0);
	CHECK(p.dropped == 0 && p.ivl[1] == p.active, "60: dropped %llu ivl18 %u", (unsigned long long)p.dropped, p.ivl[1]);
	CHECK(p.rate[2] == p.busy_secs && p.busy_secs >= 1, "60: rate45-62 %u busy %u", p.rate[2], p.busy_secs);
	sweet_fb_pace_format(&p, buf, sizeof(buf), 16830, 16830, 2);
	CHECK(strstr(buf, "panel_hz=59.4") != NULL, "%s", buf);

	/* 3. every other refresh at 90 Hz (45 fps), swaps ready in time -> drops classified "ready"; late swaps -> "nosw" */
	sweet_fb_pace_reset(&p);
	uint64_t t = 1000000, P = 11226;
	for (int i = 0; i < 100; i++) { sweet_fb_pace_push(&p, t, P, t - 2 * P + 1000); t += 2 * P; }
	CHECK(p.k[1] == 99 && p.dropped == 99 && p.drop_ready == 99 && p.drop_nosw == 0, "45: k2 %u dropped %llu ready %llu nosw %llu", p.k[1],
	    (unsigned long long)p.dropped, (unsigned long long)p.drop_ready, (unsigned long long)p.drop_nosw);
	CHECK(p.ivl[2] == 99, "45: ivl24 %u", p.ivl[2]);
	sweet_fb_pace_reset(&p);
	t = 1000000;
	for (int i = 0; i < 50; i++) { sweet_fb_pace_push(&p, t, P, t - 2000); t += 3 * P; }
	CHECK(p.k[2] == 49 && p.drop_nosw == 98 && p.drop_ready == 0 && p.ivl[3] == 49, "30: k3 %u nosw %llu ready %llu ivl35 %u", p.k[2],
	    (unsigned long long)p.drop_nosw, (unsigned long long)p.drop_ready, p.ivl[3]);

	/* 4. jitter: 11.226 +- 2 ms still k == 1, 2 periods +- 2 ms k == 2 */
	CHECK(sweet_fb_pace_periods(9300, 11226) == 1 && sweet_fb_pace_periods(16000, 11226) == 1 && sweet_fb_pace_periods(17000, 11226) == 2 &&
	    sweet_fb_pace_periods(24000, 11226) == 2 && sweet_fb_pace_periods(0, 11226) == 1 && sweet_fb_pace_periods(5, 0) == 1, "periods");

	/* 5. partial seconds (a 300 ms swipe) are not busy seconds */
	sweet_fb_pace_reset(&p);
	steady(11226, 1200000, 0.3, &p);
	steady(11226, 3500000, 0.3, &p);
	sweet_fb_pace_push(&p, 9000000, 11226, 0);
	CHECK(p.busy_secs == 0 && p.idle == 2 && p.dropped == 0, "swipes: busy %u idle %u", p.busy_secs, p.idle);

	/* 6. transfer bounds */
	sweet_fb_pace_reset(&p);
	sweet_fb_pace_xfer(&p, 8800, 9000);
	sweet_fb_pace_xfer(&p, 9000, 80000);                 /* poll late (prepNext copy): not a measurement */
	sweet_fb_pace_xfer(&p, 9100, 9050);                  /* hi < lo: refused */
	CHECK(p.xfer_n == 1 && p.xfer_sum_us == 8900 && p.poll_late == 2 && p.xfer_max_us == 8900, "xfer n %u sum %llu late %u", p.xfer_n,
	    (unsigned long long)p.xfer_sum_us, p.poll_late);

	/* 7. the 90 Hz guard.  q10jfi fbhwlive: a 9-12 ms transfer seen at a poll 80 ms late (prepNext copy) reverted the panel. */
	struct sweet_fb_p90 g = {0};
	uint32_t why;
	bool old_rule = (false || 80000 > 30000 || false);
	CHECK(old_rule, "the pre-0397 rule reverts on the late poll");
	CHECK(!sweet_fb_p90_judge(&g, false, false, 9500, 80000, &why) && why == SWEET_FB_P90_R_NONE && g.spurious_slow == 1 && g.faults == 0,
	    "late poll is not a fault");
	for (int i = 0; i < 39; i++) { CHECK(!sweet_fb_p90_judge(&g, false, false, 9000, 9250, &why), "normal transfer %d", i); }
	CHECK(!sweet_fb_p90_judge(&g, false, false, 31000, 31200, &why) && why == SWEET_FB_P90_R_SLOW && g.faults == 1, "one slow: fault only");
	CHECK(sweet_fb_p90_judge(&g, false, true, 9000, 9200, &why) && why == SWEET_FB_P90_R_UNDERRUN, "second fault reverts");
	sweet_fb_p90_reverted(&g, why, 100);
	CHECK(g.faults == 0 && g.reverts == 1 && !g.manual, "reverted");
	CHECK(!sweet_fb_p90_retry_due(&g, 100 + SWEET_FB_P90_RETRY_PRESENTS - 1) && sweet_fb_p90_retry_due(&g, 100 + SWEET_FB_P90_RETRY_PRESENTS),
	    "retry after %u presents", SWEET_FB_P90_RETRY_PRESENTS);
	g.retries = SWEET_FB_P90_RETRIES;
	CHECK(!sweet_fb_p90_retry_due(&g, 100000), "retries capped");
	struct sweet_fb_p90 g2 = {0};
	CHECK(sweet_fb_p90_judge(&g2, true, false, 0, 40000, &why) && why == SWEET_FB_P90_R_TIMEOUT, "timeout reverts at once");
	sweet_fb_p90_reverted(&g2, SWEET_FB_P90_R_MANUAL, 10);
	CHECK(g2.manual && !sweet_fb_p90_retry_due(&g2, 100000), "manual 60 never retried");
	CHECK(!strcmp(sweet_fb_p90_reason_name(SWEET_FB_P90_R_SLOW), "slow") && !strcmp(sweet_fb_p90_reason_name(99), "none"), "names");

	/* 8. random fuzz: counters stay consistent */
	sweet_fb_pace_reset(&p);
	srand(7);
	t = 1000000;
	for (int i = 0; i < 200000; i++) {
		t += (uint64_t)(rand() % 60000) + 1;
		if (rand() % 3 == 0) { sweet_fb_pace_arrival(&p, t); continue; }
		sweet_fb_pace_push(&p, t, 11226, (rand() % 2) ? t - (uint64_t)(rand() % 30000) : 0);
	}
	uint32_t ks = p.k[0] + p.k[1] + p.k[2] + p.k[3] + p.k[4], is = 0;
	for (int i = 0; i < 6; i++) { is += p.ivl[i]; }
	CHECK(ks == p.active && is == p.active && p.active + p.idle + 1 == p.pushes && p.drop_ready + p.drop_nosw == p.dropped,
	    "fuzz: k %u ivl %u active %u idle %u pushes %u", ks, is, p.active, p.idle, p.pushes);
	n = sweet_fb_pace_format(&p, buf, sizeof(buf), 11226, 11226, 1);
	CHECK(n > 0 && n < 640, "fuzz format length %d", n);

	if (fails) { printf("FAIL: %d checks\n", fails); return 1; }
	printf("ALL OK (pace + 90 Hz guard)\n");
	return 0;
}
