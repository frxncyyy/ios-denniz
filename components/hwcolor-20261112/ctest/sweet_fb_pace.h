#ifndef SWEET_FB_PACE_H
#define SWEET_FB_PACE_H
/*
 * 0397 (fbpace-20261112): present pacing measurement and the 90 Hz panel guard, pure and host-testable (never touches IOKit).
 *
 * Pacing.  Every push (start of a panel transfer from the present thread) is one present.  Between two pushes the panel refreshed
 * k = round(dt / P) times (P = the measured TE period); k - 1 refreshes showed the old picture again (dropped refreshes).  A gap above
 * SWEET_FB_PACE_IDLE_US is the UI being idle, not a drop.  "Fixed 90 fps" = panel at ~89 Hz (P 11.1..11.35 ms), every active interval
 * k == 1 (refreshes_per_present 1 = 100 %), active_fps ~ 89.  A dropped refresh is classified by the arrival time of the swap that
 * was finally pushed: it had arrived at least half a period before the missed slot ("ready": the presenter, the GPU wait or the TE
 * window lost it) or not ("nosw": CoreAnimation delivered late).  Busy seconds (1 s buckets fully inside activity) are histogrammed
 * by presents per second.
 *
 * 90 Hz guard.  The panel goes to 90 Hz (and the link to 750 Mbps) after the first 30 presents; during the next 40 presents a bad
 * transfer reverts both.  Before 0397 "bad" was timeout || measured xfer > 30 ms || DSI INTSTA bit 12, where the measured xfer was
 * taken at the first present-thread poll that saw FRAME_DONE: a 50-110 ms CPU copy of the next frame done inside that poll loop
 * (prepNext, slow WC pool sources since 0394) made a 9 ms transfer look like 80 ms and reverted the panel to 60 Hz for the whole boot
 * (q10jfi fbhwlive), and bit 12 was already cleared by the per-transfer status read.  Now: a timeout reverts at once; an underrun or a
 * transfer still not done at a poll more than 30 ms after its start (the lower bound, not the poll latency) is a fault, two faults
 * revert.  A late-poll "slow" transfer is only counted (spurious_slow).  An automatic revert is retried up to SWEET_FB_P90_RETRIES
 * times, SWEET_FB_P90_RETRY_PRESENTS presents later; a manual 60 (sysctl kern.n2panelhz=60) is never retried.
 */
#include <stdbool.h>
#include <stdint.h>
#if defined(KERNEL)
#include <libkern/libkern.h>      /* snprintf */
#else
#include <stdio.h>
#endif

#define SWEET_FB_PACE_IDLE_US        100000u
#define SWEET_FB_PACE_SEC_US         1000000u
#define SWEET_FB_PACE_ONTIME_US      1000u        /* a FRAME_DONE seen within this of the previous not-done poll: the transfer time is known */
#define SWEET_FB_P90_SLOW_US         30000u
#define SWEET_FB_P90_FAULTS          2u
#define SWEET_FB_P90_RETRIES         2u
#define SWEET_FB_P90_RETRY_PRESENTS  600u

struct sweet_fb_pace {
	uint64_t last_push_us, last_arr_us;
	uint64_t active_us;                 /* sum of the active push intervals */
	uint64_t dropped, drop_ready, drop_nosw;
	uint32_t pushes, active, idle;
	uint32_t k[5];                      /* refreshes per present: 1, 2, 3, 4, 5+ */
	uint32_t ivl[6];                    /* push intervals, ms: <=12.5 <=18 <=24 <=35 <=50 <=100 */
	uint32_t arr[7];                    /* swap arrival gaps, same bins + >100 */
	uint32_t run, best_run;             /* consecutive k == 1 intervals */
	uint64_t sec_start_us, sec_first_us, sec_last_us;
	uint32_t sec_pushes;
	bool     sec_broken;
	uint32_t rate[7];                   /* busy seconds by presents: <30 <45 <62 <75 <85 <88 >=88 */
	uint32_t busy_secs;
	uint64_t xfer_sum_us;
	uint32_t xfer_n, xfer_max_us, poll_late;
};

static inline uint32_t
sweet_fb_pace_ms_bin(uint64_t dt_us)
{
	return dt_us <= 12500 ? 0 : dt_us <= 18000 ? 1 : dt_us <= 24000 ? 2 : dt_us <= 35000 ? 3 : dt_us <= 50000 ? 4 : dt_us <= 100000 ? 5 : 6;
}

static inline uint32_t
sweet_fb_pace_periods(uint64_t dt_us, uint64_t period_us)
{
	if (period_us == 0) { return 1; }
	uint64_t k = (dt_us + period_us / 2) / period_us;
	return k < 1 ? 1 : (k > 0xffffu ? 0xffffu : (uint32_t)k);
}

static inline uint32_t
sweet_fb_pace_rate_bin(uint32_t n)
{
	return n < 30 ? 0 : n < 45 ? 1 : n < 62 ? 2 : n < 75 ? 3 : n < 85 ? 4 : n < 88 ? 5 : 6;
}

static inline void
sweet_fb_pace_reset(struct sweet_fb_pace *p)
{
	__builtin_memset(p, 0, sizeof(*p));
}

/* close every 1 s bucket that ended before now; a bucket counts only if activity covered it (first push within IDLE of its start,
 * last push within IDLE of its end, no idle gap inside) */
static inline void
sweet_fb_pace_sec_roll(struct sweet_fb_pace *p, uint64_t now_us)
{
	if (p->sec_start_us == 0) { p->sec_start_us = now_us; return; }
	while (now_us >= p->sec_start_us + SWEET_FB_PACE_SEC_US) {
		uint64_t s = p->sec_start_us, e = s + SWEET_FB_PACE_SEC_US;
		if (!p->sec_broken && p->sec_pushes != 0 && p->sec_first_us <= s + SWEET_FB_PACE_IDLE_US && p->sec_last_us + SWEET_FB_PACE_IDLE_US >= e) {
			p->rate[sweet_fb_pace_rate_bin(p->sec_pushes)]++;
			p->busy_secs++;
		}
		p->sec_pushes = 0; p->sec_broken = false; p->sec_first_us = 0; p->sec_last_us = 0;
		p->sec_start_us = e;
		if (now_us - p->sec_start_us > 4 * (uint64_t)SWEET_FB_PACE_SEC_US) { p->sec_start_us = now_us; }      /* long idle: restart the grid */
	}
}

/* one push at now_us; period_us = the panel's TE period; arr_us = when the pushed swap arrived (0 = unknown) */
static inline void
sweet_fb_pace_push(struct sweet_fb_pace *p, uint64_t now_us, uint64_t period_us, uint64_t arr_us)
{
	sweet_fb_pace_sec_roll(p, now_us);
	if (p->last_push_us != 0 && now_us > p->last_push_us) {
		uint64_t dt = now_us - p->last_push_us;
		if (dt > SWEET_FB_PACE_IDLE_US) {
			p->idle++; p->run = 0; p->sec_broken = true;
		} else {
			uint32_t k = sweet_fb_pace_periods(dt, period_us);
			p->active++; p->active_us += dt;
			p->ivl[sweet_fb_pace_ms_bin(dt)]++;
			p->k[k >= 5 ? 4 : k - 1]++;
			if (k == 1) {
				if (++p->run > p->best_run) { p->best_run = p->run; }
			} else {
				p->run = 0;
				p->dropped += k - 1;
				if (arr_us != 0 && arr_us + period_us / 2 <= p->last_push_us + period_us) { p->drop_ready += k - 1; }
				else { p->drop_nosw += k - 1; }
			}
		}
	}
	if (p->sec_pushes == 0) { p->sec_first_us = now_us; }
	p->sec_pushes++; p->sec_last_us = now_us;
	p->pushes++;
	p->last_push_us = now_us;
}

static inline void
sweet_fb_pace_arrival(struct sweet_fb_pace *p, uint64_t now_us)
{
	if (p->last_arr_us != 0 && now_us > p->last_arr_us) { p->arr[sweet_fb_pace_ms_bin(now_us - p->last_arr_us)]++; }
	p->last_arr_us = now_us;
}

/* one finished transfer: lo = last poll that saw it running, hi = the poll that saw FRAME_DONE (both since the start) */
static inline void
sweet_fb_pace_xfer(struct sweet_fb_pace *p, uint64_t lo_us, uint64_t hi_us)
{
	if (hi_us < lo_us || hi_us - lo_us > SWEET_FB_PACE_ONTIME_US) { p->poll_late++; return; }
	uint64_t t = (lo_us + hi_us) / 2;
	p->xfer_sum_us += t; p->xfer_n++;
	if (t > p->xfer_max_us) { p->xfer_max_us = (uint32_t)t; }
}

/* active presents per second x10 (one decimal) */
static inline uint32_t
sweet_fb_pace_fps10(const struct sweet_fb_pace *p)
{
	return p->active_us ? (uint32_t)(((uint64_t)p->active * 10000000ull + p->active_us / 2) / p->active_us) : 0;
}

static inline int
sweet_fb_pace_format(const struct sweet_fb_pace *p, char *buf, size_t len, uint64_t te_us, uint64_t hb_us, uint32_t mode)
{
	uint32_t f = sweet_fb_pace_fps10(p);
	uint32_t hz10 = te_us ? (uint32_t)((10000000ull + te_us / 2) / te_us) : 0;
	uint32_t k1pm = p->active ? (uint32_t)((uint64_t)p->k[0] * 1000u / p->active) : 0;
	return snprintf(buf, len,
	    "panel_hz=%u.%u te_us=%llu hb_us=%llu mode=%u pushes=%u active=%u idle_gaps=%u active_fps=%u.%u k1_permille=%u "
	    "refresh_per_present_1/2/3/4/5+=%u/%u/%u/%u/%u dropped=%llu (ready %llu nosw %llu) best_run=%u "
	    "ivl_ms<=12.5/18/24/35/50/100=%u/%u/%u/%u/%u/%u arr_ms<=12.5/18/24/35/50/100/more=%u/%u/%u/%u/%u/%u/%u "
	    "busy_s=%u rate_s<30/45/62/75/85/88/88+=%u/%u/%u/%u/%u/%u/%u xfer_true_avg_us=%llu max=%u n=%u poll_late=%u",
	    hz10 / 10, hz10 % 10, (unsigned long long)te_us, (unsigned long long)hb_us, mode, p->pushes, p->active, p->idle, f / 10, f % 10, k1pm,
	    p->k[0], p->k[1], p->k[2], p->k[3], p->k[4], (unsigned long long)p->dropped, (unsigned long long)p->drop_ready,
	    (unsigned long long)p->drop_nosw, p->best_run,
	    p->ivl[0], p->ivl[1], p->ivl[2], p->ivl[3], p->ivl[4], p->ivl[5],
	    p->arr[0], p->arr[1], p->arr[2], p->arr[3], p->arr[4], p->arr[5], p->arr[6],
	    p->busy_secs, p->rate[0], p->rate[1], p->rate[2], p->rate[3], p->rate[4], p->rate[5], p->rate[6],
	    (unsigned long long)(p->xfer_n ? p->xfer_sum_us / p->xfer_n : 0), p->xfer_max_us, p->xfer_n, p->poll_late);
}

/* ---- 90 Hz guard ---- */
enum { SWEET_FB_P90_R_NONE = 0, SWEET_FB_P90_R_TIMEOUT = 1, SWEET_FB_P90_R_UNDERRUN = 2, SWEET_FB_P90_R_SLOW = 3, SWEET_FB_P90_R_MANUAL = 4 };

struct sweet_fb_p90 {
	uint32_t faults;            /* in the current guard window */
	uint32_t reverts, retries, last_reason, revert_at;
	bool     manual;            /* kern.n2panelhz=60: no automatic retry */
	uint64_t spurious_slow;     /* measured > 30 ms only because the poll was late (not a fault) */
};

static inline const char *
sweet_fb_p90_reason_name(uint32_t r)
{
	return r == SWEET_FB_P90_R_TIMEOUT ? "timeout" : r == SWEET_FB_P90_R_UNDERRUN ? "underrun" : r == SWEET_FB_P90_R_SLOW ? "slow" :
	    r == SWEET_FB_P90_R_MANUAL ? "manual" : "none";
}

/* one transfer inside the guard window.  Returns true when the panel must go back to 60 Hz (*reason set). */
static inline bool
sweet_fb_p90_judge(struct sweet_fb_p90 *g, bool timeout, bool underrun, uint64_t lo_us, uint64_t hi_us, uint32_t *reason)
{
	*reason = SWEET_FB_P90_R_NONE;
	if (timeout) { *reason = SWEET_FB_P90_R_TIMEOUT; return true; }
	if (underrun) { g->faults++; *reason = SWEET_FB_P90_R_UNDERRUN; }
	else if (lo_us > SWEET_FB_P90_SLOW_US) { g->faults++; *reason = SWEET_FB_P90_R_SLOW; }
	else if (hi_us > SWEET_FB_P90_SLOW_US) { g->spurious_slow++; }
	return g->faults >= SWEET_FB_P90_FAULTS;
}

static inline void
sweet_fb_p90_reverted(struct sweet_fb_p90 *g, uint32_t reason, uint32_t presents)
{
	g->reverts++; g->last_reason = reason; g->revert_at = presents; g->faults = 0;
	g->manual = reason == SWEET_FB_P90_R_MANUAL;
}

static inline bool
sweet_fb_p90_retry_due(const struct sweet_fb_p90 *g, uint32_t presents)
{
	return g->reverts != 0 && !g->manual && g->retries < SWEET_FB_P90_RETRIES && presents - g->revert_at >= SWEET_FB_P90_RETRY_PRESENTS;
}
#endif
