/*
 * 0530 (heat-20261114): NORD2 CLOCK floor -- the policy, host-testable (test/clockfloor_test.c).
 *
 * The Nord 2 port has no RTC (nord2nortc=1): the calendar starts at 1970 on every boot and jumps ~56 years when timed gets
 * NTP time.  Daemons that keep "last done" dates catch up over that jump.  Measured (unified log of 2026-11-13): dasd's
 * _DASBudgetModulator stores lastModulation = 1970-01-01 00:00:34 at boot (the stored 2026 date is "in the future", so it
 * is replaced by now) and after settimeofday() logs "Last modulated budgets at Thu Jan  1 01:00:34 1970, modulating for
 * 1993998 slots": one 15-minute slot at a time, one coreduetd _DKPredictionQuery (0.45 s CPU) per 8 slots and per widget
 * budget = coreduetd at 80-90 % of an A78 core for the rest of the uptime, every boot (cpu_resource reports since 11-01).
 *
 * Fix: before launchd runs, the calendar is raised to the newest trustworthy time the kernel can see: the root volume's
 * last modification (HFS+ volume header modifyDate, kept by hfs_volupdate on every create/delete/allocation, so it follows
 * the previous session up to its last minutes), a few directory times, and the date this patch was written.  The clock is
 * never moved backwards and never beyond N2CK_MAX_AHEAD after the patch date (a corrupt time stamp must not win).
 */
#ifndef NORD2_CLOCKFLOOR_H
#define NORD2_CLOCKFLOOR_H

#include <stdint.h>

#define N2CK_BUILD_EPOCH   1794528000ULL                  /* 2026-11-13 00:00:00 UTC: the date of this patch */
#define N2CK_MAX_AHEAD     (10ULL * 366 * 86400)          /* candidates later than patch date + 10 years are ignored */
#define N2CK_MAX_CAND      8
#define N2CK_SRC_NONE      0u                             /* the clock was already at or past every candidate */
#define N2CK_SRC_BUILD     1u                             /* the patch date */
#define N2CK_SRC_CAND      2u                             /* + index into the candidate array */

/*
 * now:  calendar seconds at the call.  cand[0..n): candidate seconds (0 = not available).
 * Returns the seconds the calendar should be set to (== now: leave it alone) and says in *src which source won.
 */
static inline uint64_t
n2ck_pick(uint64_t now, const uint64_t *cand, unsigned n, unsigned *src)
{
	uint64_t best = N2CK_BUILD_EPOCH;
	unsigned s = N2CK_SRC_BUILD;

	for (unsigned i = 0; i < n && i < N2CK_MAX_CAND; i++) {
		if (cand[i] > best && cand[i] <= N2CK_BUILD_EPOCH + N2CK_MAX_AHEAD) {
			best = cand[i];
			s = N2CK_SRC_CAND + i;
		}
	}
	if (best <= now) {
		best = now;
		s = N2CK_SRC_NONE;
	}
	if (src != (unsigned *)0) {
		*src = s;
	}
	return best;
}

/*
 * 0540 (rtc-20261114): the MT6359P RTC (bsd/dev/nord2_rtc.h) is the primary source; this floor stays the fallback.
 * now: calendar seconds; floor: n2ck_pick()'s answer (== now when the floor is off or has nothing newer) and its source.
 * rtc_ok/rtc: a valid RTC read (2025..2095), already advanced to the time of this call.
 * Returns the seconds to set (== now: leave the calendar alone): the newest of now, the floor and the RTC, so the clock
 * never goes backwards and never below the floor; an RTC beyond patch date + N2CK_MAX_AHEAD is treated as corrupt.
 */
#define N2CK_SRC_RTC       16u                            /* the RTC won */
#define N2CK_RTC_WAIT_MS   3000u                          /* the first exec waits at most this long for the boot read */

static inline uint64_t
n2ck_pick_rtc(uint64_t now, uint64_t floor, unsigned floor_src, int rtc_ok, uint64_t rtc, unsigned *src)
{
	uint64_t best = now;
	unsigned s = N2CK_SRC_NONE;

	if (floor > best) {
		best = floor;
		s = floor_src;
	}
	if (rtc_ok && rtc <= N2CK_BUILD_EPOCH + N2CK_MAX_AHEAD && rtc > best) {
		best = rtc;
		s = N2CK_SRC_RTC;
	}
	if (src != (unsigned *)0) {
		*src = s;
	}
	return best;
}

#endif /* NORD2_CLOCKFLOOR_H */
