/* 0642 host test: n2s_choose2 prime-first vs n2s_choose (0099). cc -fsanitize=address,undefined */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/osfmk/kern/nord2_sched_policy.h"

#define BIG 0xf0u
#define MODE (N2S_PLACE | N2S_PREEMPT | N2S_KEEPFREE | N2S_PULL)
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { if (fails++ < 20) fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int
main(void)
{
	struct n2s_view v; int r, r2;
	memset(&v, 0, sizeof(v)); v.avail = 0xff; v.idle = 0xf0;
	CHECK(n2s_choose2(MODE, BIG, &v, -1, true, false, 54, 37, &r, true) == 7 && r == N2S_R_FAST_IDLE);
	CHECK(n2s_choose2(MODE, BIG, &v, 4, true, false, 54, 37, &r, true) == 7);        /* prime beats the hint */
	CHECK(n2s_choose(MODE, BIG, &v, 4, true, false, 54, 37, &r) == 4);               /* 0099 rule unchanged */
	v.idle = 0x70;                                                                     /* prime busy */
	CHECK(n2s_choose2(MODE, BIG, &v, 4, true, false, 54, 37, &r, true) == 4);        /* then the hint */
	CHECK(n2s_choose2(MODE, BIG, &v, 1, true, false, 54, 37, &r, true) == 6);        /* little hint: fastest idle big */
	CHECK(n2s_choose2(MODE, BIG, &v, -1, false, false, 20, 37, &r, true) == -1 || r == N2S_R_KEPT_SLOW);
	/* random: prime_first never changes the reason, never picks a non-idle or non-fast CPU when it picks via FAST_IDLE, and equals 0099 otherwise */
	srand(7);
	for (int it = 0; it < 2000000; it++) {
		memset(&v, 0, sizeof(v));
		v.avail = (uint32_t)rand() & 0xff; v.idle = (uint32_t)rand() & 0xff; v.running = (uint32_t)rand() & 0xff & ~v.idle;
		for (int i = 0; i < 8; i++) { v.cur_pri[i] = (int16_t)(rand() % 100); v.bound[i] = rand() % 4 == 0; }
		int mode = rand() & 0x1f, hint = rand() % 10 - 1, pri = rand() % 100, th = 30 + rand() % 20;
		uint32_t fast = (rand() % 8) ? BIG : (uint32_t)rand() & 0xff;
		bool el = rand() & 1, dec = rand() % 4 == 0;
		int a = n2s_choose(mode, fast, &v, hint, el, dec, pri, th, &r);
		int b = n2s_choose2(mode, fast, &v, hint, el, dec, pri, th, &r2, true);
		CHECK(r == r2);
		if (r2 == N2S_R_FAST_IDLE) {
			uint32_t f = fast & v.avail, ok = v.idle & v.avail & f;
			CHECK(b >= 0 && ((ok >> b) & 1u));
			int prime = 31 - __builtin_clz(f);
			if ((ok >> prime) & 1u) CHECK(b == prime);
			else if (hint >= 0 && hint < 32 && ((ok >> hint) & 1u)) CHECK(b == hint);
			else CHECK(b == 31 - __builtin_clz(ok));
		} else {
			CHECK(a == b);
		}
	}
	printf(fails ? "PRIME_TEST FAILED %d of %d\n" : "PRIME_TEST_OK (%d checks)\n", fails ? fails : checks, checks);
	return fails != 0;
}
