/* heat-20261114/test/clockfloor_test.c: host test of the 0530 policy (b/bsd/kern/nord2_clockfloor.h) + a model of dasd's slot count. */
#include <assert.h>
#include <stdio.h>
#include "nord2_clockfloor.h"

/* dasd: -[_DASBudgetModulator numberOfModulationFromDate:toDate:] = floor((now - last) / 900) (fcvtms), widget slot loop runs for n >= 1 */
static long slots(long long last, long long now) { long long d = now - last; return (long)(d >= 0 ? d / 900 : -((-d + 899) / 900)); }

int
main(void)
{
	unsigned src = 99;
	const uint64_t B = N2CK_BUILD_EPOCH;
	uint64_t c[N2CK_MAX_CAND] = { 0 };

	/* 1970 clock, nothing known: the patch date */
	assert(n2ck_pick(25, c, 5, &src) == B && src == N2CK_SRC_BUILD);
	/* 1970 clock, volume modified a day after the patch date: the volume wins */
	c[0] = B + 86400;
	assert(n2ck_pick(25, c, 5, &src) == B + 86400 && src == N2CK_SRC_CAND);
	/* a newer directory wins over the volume */
	c[2] = B + 90000;
	assert(n2ck_pick(25, c, 5, &src) == B + 90000 && src == N2CK_SRC_CAND + 2);
	/* 1970-stamped candidates (written before NTP on the old kernels) never win */
	c[0] = 113; c[2] = 254; c[1] = 3600;
	assert(n2ck_pick(25, c, 5, &src) == B && src == N2CK_SRC_BUILD);
	/* a corrupt far-future stamp is ignored, a stamp exactly at the limit is taken */
	c[0] = B + N2CK_MAX_AHEAD + 1; c[1] = B + 5;
	assert(n2ck_pick(25, c, 5, &src) == B + 5 && src == N2CK_SRC_CAND + 1);
	c[0] = B + N2CK_MAX_AHEAD;
	assert(n2ck_pick(25, c, 5, &src) == B + N2CK_MAX_AHEAD && src == N2CK_SRC_CAND);
	/* the clock is never moved backwards (an RTC or an earlier setter already did better) */
	c[0] = B + 100;
	assert(n2ck_pick(B + 5000, c, 5, &src) == B + 5000 && src == N2CK_SRC_NONE);
	assert(n2ck_pick(B + 100, c, 5, &src) == B + 100 && src == N2CK_SRC_NONE);
	/* more candidates than the array bound are not read; NULL src is accepted */
	assert(n2ck_pick(0, c, 1000, (unsigned *)0) == B + 100);
	assert(n2ck_pick(7, c, 0, &src) == B && src == N2CK_SRC_BUILD);

	/* what the fix buys (the numbers of the 2026-11-13 unified log): old kernel, clock 34 s at dasd's first modulation, NTP = 2026-11-13 19:35:49 UTC */
	const long long ntp = 1794598549;
	long old = slots(34, ntp);
	assert(old == 1993998);                              /* "modulating for 1993998 slots from then to now" */
	/* new kernel: the phone was off for a day; the clock starts at the volume time, dasd stores that, NTP corrects by one day */
	const long long later = ntp + 10 * 86400;            /* a boot ten days after the log, the volume last written a day before it */
	c[0] = (uint64_t)(later - 86400); c[1] = c[2] = 0;
	uint64_t boot = n2ck_pick(20, c, 5, &src);
	assert(boot == (uint64_t)(later - 86400) && slots((long long)boot + 34, later) == 95);
	/* a volume older than the patch date (the very first boot of an old root image): the patch date, 10 days = 960 slots = 120 queries */
	c[0] = 113;
	assert(n2ck_pick(20, c, 5, &src) == B && slots((long long)B + 34, later) == (later - (long long)B - 34) / 900);
	c[0] = (uint64_t)(later - 86400);
	/* stored lastModulation older than the floor clock: a normal catch-up, not a reset to "now" */
	assert(slots((long long)boot - 3600, (long long)boot + 34) == 4);
	printf("slots: old kernel %ld (= %ld coreduetd queries per widget budget at 8 slots each), new kernel after one day off 95 (12 queries)\n", old, old / 8);
	puts("CLOCKFLOOR_TEST_OK");
	return 0;
}
