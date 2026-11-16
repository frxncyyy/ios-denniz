#ifndef SWEET_FB_PRESENT_H
#define SWEET_FB_PRESENT_H
/*
 * Presentation scheduling for the SWEET/NORD2 framebuffer, pure and host-testable (never touches IOKit).
 *
 *  - vsync is an absolute schedule: the next deadline is advanced by whole periods from the previous
 *    DEADLINE, before any work is done, so a slow present cannot stretch the tick clock CoreAnimation paces
 *    itself on.  A handler that really runs a whole period late resynchronises instead of bursting.
 *  - panel presents run on their own thread; the vsync handler only decides whether to hand over a frame.
 *  - a swap is presented only when the GPU is done writing its surfaces; while a surface is busy the previous
 *    (complete) frame stays on the panel, but never longer than SWEET_FB_BUSY_MAX_TICKS.
 *  - the panel transfer is started and polled, never spun on.
 */
#include <stdbool.h>
#include <stdint.h>

#define SWEET_FB_DEFAULT_HZ 30u          /* panel present rate; one panel transfer plus the copy is ~20-25 ms */
#define SWEET_FB_BUSY_MAX_TICKS 6u       /* 100 ms at 60 Hz: then present anyway */
#define SWEET_FB_TE_LEAD_US_DEFAULT 1000u   /* 0156: the push starts this long before the predicted TE (the write must be ahead of the panel scan) */
#define SWEET_FB_TE_WAIT_US 25000u       /* 0155: panel TE not seen: push anyway, and stop waiting for TE for a second */
#define SWEET_FB_PRES_WAIT_US 8000u      /* 0150/0151: a presenter still finishing the previous frame is waited for this long at the tick (4 ms still lost a whole period in ~10-20% of the frames: 3.3 ms GPU wait + 2.2 copy + 11 transfer ~ 16.5 ms) */
#define SWEET_FB_BUSY_WAIT_US 10000u     /* 0149: a swap whose GPU work is still in flight at the tick is waited for, this long at most, before it is skipped */
#define SWEET_FB_XFER_TIMEOUT_US 40000u  /* frame-done not seen: give up on this transfer */
#define SWEET_FB_XFER_POLL_US 250u
#define SWEET_FB_STAT_INTERVAL_US 5000000u

/* hz == 0 or >= 60: every tick; otherwise every 60/hz-th tick. */
static inline uint32_t
sweet_fb_flush_ticks(uint32_t hz)
{
	return (hz == 0 || hz >= 60) ? 1 : 60 / hz;
}

static inline bool
sweet_fb_flush_due(uint64_t tick, uint32_t flush_ticks)
{
	return flush_ticks <= 1 || (tick % flush_ticks) == 0;
}

/* Advance an absolute deadline by one period.  *next == 0: first call, schedule from now.
 * A deadline already passed by less than half a period fires at once and keeps its phase; if the handler ran
 * more than half a period past it the schedule restarts from now (returns true) instead of bursting. */
static inline bool
sweet_fb_next_tick(uint64_t *next, uint64_t now, uint64_t period)
{
	if (*next == 0) {
		*next = now + period;
		return false;
	}
	*next += period;
	if (now > *next + period / 2) {
		*next = now + period;
		return true;
	}
	return false;
}

enum { SWEET_FB_BUSY_PRESENT = 0, SWEET_FB_BUSY_SKIP = 1, SWEET_FB_BUSY_FORCE = 2 };

/* A frame with GPU-busy surfaces waits for a complete one, at most max ticks. */
static inline int
sweet_fb_busy_decision(bool busy, uint32_t *busy_ticks, uint32_t max_ticks)
{
	if (!busy) {
		*busy_ticks = 0;
		return SWEET_FB_BUSY_PRESENT;
	}
	if (*busy_ticks >= max_ticks) {
		*busy_ticks = 0;
		return SWEET_FB_BUSY_FORCE;
	}
	(*busy_ticks)++;
	return SWEET_FB_BUSY_SKIP;
}

enum { SWEET_FB_XFER_WAIT = 0, SWEET_FB_XFER_DONE = 1, SWEET_FB_XFER_TIMEOUT = 2 };

static inline int
sweet_fb_xfer_state(bool frame_done, uint64_t elapsed_us)
{
	if (frame_done) {
		return SWEET_FB_XFER_DONE;
	}
	return elapsed_us >= SWEET_FB_XFER_TIMEOUT_US ? SWEET_FB_XFER_TIMEOUT : SWEET_FB_XFER_WAIT;
}

/* Present timing, windowed; reported at most every SWEET_FB_STAT_INTERVAL_US and only if something presented. */
struct sweet_fb_present_stats {
	uint32_t n, timeouts, busy_skips, busy_forced, late_ticks, legacy;
	uint32_t busy_same, busy_diff, busy_maxrun, busy_waits, busy_wait_ok, pres_waits, pres_wait_ok, te_waits, te_miss, te_wait_max, dbl_preps, dbl_hits, te_lead_starts;
	uint64_t te_wait_us;
	uint64_t busy_wait_us;   /* 0148: skips with the same / a different newest surface than the previous skip; longest run of skips */
	uint64_t sum_us, sum_copy_us, sum_xfer_us;
	uint32_t max_us, max_copy_us, max_xfer_us;
	uint64_t last_report_us;
};

static inline void
sweet_fb_stats_note(struct sweet_fb_present_stats *s, uint64_t copy_us, uint64_t xfer_us, bool legacy, bool timeout)
{
	uint64_t total = copy_us + xfer_us;
	s->n++;
	s->sum_us += total;
	s->sum_copy_us += copy_us;
	s->sum_xfer_us += xfer_us;
	s->legacy += legacy;
	s->timeouts += timeout;
	if (total > s->max_us) { s->max_us = (uint32_t)total; }
	if (copy_us > s->max_copy_us) { s->max_copy_us = (uint32_t)copy_us; }
	if (xfer_us > s->max_xfer_us) { s->max_xfer_us = (uint32_t)xfer_us; }
}

static inline bool
sweet_fb_stats_due(const struct sweet_fb_present_stats *s, uint64_t now_us)
{
	return s->n != 0 && now_us - s->last_report_us >= SWEET_FB_STAT_INTERVAL_US;
}

static inline void
sweet_fb_stats_reset(struct sweet_fb_present_stats *s, uint64_t now_us)
{
	*s = (struct sweet_fb_present_stats){0};
	s->last_report_us = now_us;
}
#endif
