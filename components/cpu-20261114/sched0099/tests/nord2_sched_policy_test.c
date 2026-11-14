/* Host test of osfmk/kern/nord2_sched_policy.h (patch 0099). cc -fsanitize=address,undefined */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/osfmk/kern/nord2_sched_policy.h"

#define BIG 0xf0u       /* Nord2: cpu4-7 Cortex-A78 */
#define ALL 0xffu
#define RT 97           /* BASEPRI_RTQUEUES */
#define KF 80           /* MINPRI_KERNEL */
#define FG 47           /* BASEPRI_FOREGROUND */
#define MODE (N2S_PLACE | N2S_PREEMPT | N2S_KEEPFREE | N2S_PULL)

static int checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } checks++; } while (0)

static struct n2s_view
view(uint32_t idle, uint32_t running)
{
	struct n2s_view v;
	memset(&v, 0, sizeof(v));
	v.avail = ALL;
	v.idle = idle;
	v.running = running;
	for (int i = 0; i < N2S_MAX_CPUS; i++) {
		v.cur_pri[i] = 4;
	}
	return v;
}

int
main(void)
{
	int r;
	struct n2s_view v = view(ALL, 0);

	/* mode 0, no big CPUs or no little CPUs: always the stock choice */
	CHECK(n2s_choose(0, BIG, &v, -1, true, false, 54, FG, &r) == -1 && r == N2S_R_FALLBACK);
	CHECK(n2s_choose(MODE, 0, &v, -1, true, false, 54, FG, &r) == -1 && r == N2S_R_FALLBACK);
	CHECK(n2s_choose(MODE, ALL, &v, -1, true, false, 54, FG, &r) == -1);
	v.avail = BIG;  /* only big CPUs available (e.g. little ones derecommended) */
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, false, 54, FG, &r) == -1);
	v = view(ALL, 0);

	/* PLACE: eligible thread, all idle -> lowest big CPU, or the hint if it is an idle big CPU */
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, false, 54, FG, &r) == 4 && r == N2S_R_FAST_IDLE);
	CHECK(n2s_choose(MODE, BIG, &v, 7, true, false, 54, FG, &r) == 7);
	CHECK(n2s_choose(MODE, BIG, &v, 1, true, false, 54, FG, &r) == 4);    /* hint on a little CPU ignored */
	/* only cpu6 idle among big */
	v = view(0x4fu, 0xb0u);
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, false, 54, FG, &r) == 6);

	/* PREEMPT: no big idle; victim = lowest-priority unbound big CPU below threshold and thread pri */
	v = view(0x0fu, 0xf0u);
	v.cur_pri[4] = 60; v.cur_pri[5] = 31; v.cur_pri[6] = 20; v.cur_pri[7] = 4; v.bound[7] = 1;
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, false, 54, FG, &r) == 6 && r == N2S_R_FAST_PREEMPT);
	CHECK(n2s_choose(MODE & ~N2S_PREEMPT, BIG, &v, -1, true, false, 54, FG, &r) == -1 && r == N2S_R_SPILL);
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, true, 54, FG, &r) == -1 && r == N2S_R_SPILL);   /* decayed */
	v.cur_pri[5] = 50; v.cur_pri[6] = 50;   /* every unbound big CPU runs an eligible thread */
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, false, 54, FG, &r) == -1 && r == N2S_R_SPILL);
	v.cur_pri[6] = 40;
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, false, 38, FG, &r) == -1);  /* victim not below the thread */
	CHECK(n2s_choose(MODE, BIG, &v, -1, true, false, 41, FG, &r) == 6);

	/* KEEPFREE: non-eligible thread takes an idle little CPU, the hint if it is one */
	v = view(ALL, 0);
	CHECK(n2s_choose(MODE, BIG, &v, 5, false, false, 31, FG, &r) == 0 && r == N2S_R_KEPT_SLOW);
	CHECK(n2s_choose(MODE, BIG, &v, 2, false, false, 31, FG, &r) == 2);
	CHECK(n2s_choose(N2S_PLACE, BIG, &v, 5, false, false, 31, FG, &r) == -1 && r == N2S_R_FALLBACK);
	v = view(0xf0u, 0x0fu);         /* little CPUs all busy */
	CHECK(n2s_choose(MODE, BIG, &v, -1, false, false, 31, FG, &r) == -1);

	/* eligibility */
	CHECK(n2s_eligible(0, 47, 4, FG, RT, false));
	CHECK(n2s_eligible(0, 4, 47, FG, RT, false));          /* base priority counts */
	CHECK(!n2s_eligible(0, 46, 31, FG, RT, false));
	CHECK(!n2s_eligible(0, 97, 97, FG, RT, false));        /* realtime: XNU RT path */
	CHECK(!n2s_eligible(0, 80, 80, FG, RT, true));         /* kernel thread */
	CHECK(n2s_eligible(N2S_KERNEL, 80, 80, FG, RT, true));

	/* PULL */
	CHECK(n2s_avoid(MODE, BIG, 1, 0x10u, true, false));
	CHECK(!n2s_avoid(MODE, BIG, 5, 0x10u, true, false));   /* already on a big CPU */
	CHECK(!n2s_avoid(MODE, BIG, 1, 0x0fu, true, false));   /* no big CPU idle */
	CHECK(!n2s_avoid(MODE, BIG, 1, 0x10u, false, false));
	CHECK(!n2s_avoid(MODE, BIG, 1, 0x10u, true, true));    /* bound */
	CHECK(!n2s_avoid(N2S_PULL, BIG, 1, 0x10u, true, false));
	v = view(0x10u, 0xefu);
	v.cur_pri[0] = 31; v.cur_pri[1] = 54; v.cur_pri[2] = 60; v.bound[2] = 1; v.cur_pri[3] = 81;
	CHECK(n2s_pull_target(MODE, BIG, 4, &v, FG, RT, KF) == 1);
	CHECK(n2s_pull_target(MODE | N2S_KERNEL, BIG, 4, &v, FG, RT, KF) == 3);
	CHECK(n2s_pull_target(MODE, BIG, 1, &v, FG, RT, KF) == -1);     /* a little CPU going idle */
	CHECK(n2s_pull_target(N2S_PLACE, BIG, 4, &v, FG, RT, KF) == -1);

	/* big-core detection from ADT compatible lists (Nord2 mkadt-nord2.py layout) */
	static const char a78[] = "mediatek,mt6893-cpu\0arm,cortex-a78\0ARM,v8";
	static const char a55[] = "mediatek,mt6893-cpu\0arm,cortex-a55\0ARM,v8";
	static const char x1[] = "arm,cortex-x1";
	CHECK(n2s_compatible_is_big(a78, sizeof(a78)));
	CHECK(!n2s_compatible_is_big(a55, sizeof(a55)));
	CHECK(n2s_compatible_is_big(x1, sizeof(x1) - 1));       /* unterminated last string */
	CHECK(!n2s_compatible_is_big("arm,cortex-a7", 13));
	CHECK(!n2s_compatible_is_big("", 0));

	/* MHz: 262144 cycles */
	CHECK(n2s_mhz(262144, 87381) == 3000);
	CHECK(n2s_mhz(262144, 131072) == 2000);
	CHECK(n2s_mhz(262144, 524288) == 500);
	CHECK(n2s_mhz(262144, 0) == 0);

	printf("nord2_sched_policy_test PASS %d checks\n", checks);
	return 0;
}
