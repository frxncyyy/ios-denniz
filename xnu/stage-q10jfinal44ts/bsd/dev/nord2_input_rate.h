/* 0407 (fpsmeter-20261114): touch sampling rate as the HID stack sees it, measurement only (no extra I2C traffic).
 * SPDX-License-Identifier: MIT
 *
 * The NORD2 input thread polls the FT3518 over bit-banged I2C0 and then sleeps 10 ms; a finger's position reaches the HID stack at
 * the rate of that loop.  While a finger is down every decoded report is counted and the gap since the previous one is binned;
 * the I2C read time of each report (start of the first read -> decode) is summed.  One line per window with finger activity:
 *   NORD2 INPUT RATE: win_ms= reports= (r/s) moves= errors= gap_ms<=10/12/16/20/25/33/50/more=.. gap_avg_us= gap_max_us= read_avg_us= read_max_us=
 * Pure logic; the caller owns the struct (input thread only, no lock). */
#ifndef NORD2_INPUT_RATE_H
#define NORD2_INPUT_RATE_H
#ifndef KERNEL
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#endif

#define N2IR_WINDOW_US  5000000ull

struct n2ir {
	uint64_t win_start_us, last_us, gap_sum_us, read_sum_us;
	uint32_t reports, moves, errors, gaps, gap_max_us, read_max_us, gap[8];
};

static inline unsigned
n2ir_bin(uint64_t gap_us)
{
	static const uint32_t lim[7] = { 10000, 12000, 16000, 20000, 25000, 33000, 50000 };
	unsigned b = 0;
	while (b < 7 && gap_us > lim[b]) { b++; }
	return b;
}

/* A decoded report: active = fingers down after it, moves = HID events it produced, read_us = its I2C time. */
static inline void
n2ir_report(struct n2ir *r, uint64_t now_us, uint32_t active, uint32_t moves, uint64_t read_us)
{
	if (active == 0) { r->last_us = 0; r->moves += moves; return; }      /* lift: the next touch starts a new gap chain */
	r->reports++; r->moves += moves;
	r->read_sum_us += read_us; if (read_us > r->read_max_us) { r->read_max_us = (uint32_t)read_us; }
	if (r->last_us != 0 && now_us > r->last_us) {
		uint64_t g = now_us - r->last_us;
		r->gap[n2ir_bin(g)]++; r->gaps++; r->gap_sum_us += g;
		if (g > r->gap_max_us) { r->gap_max_us = (uint32_t)(g > 0xffffffffull ? 0xffffffffull : g); }
	}
	r->last_us = now_us;
}

static inline void n2ir_error(struct n2ir *r) { r->errors++; }

/* 1 = the window is over and had finger activity: format it (then n2ir_reset).  -1 = over, nothing to say (reset silently). 0 = not yet. */
static inline int
n2ir_due(const struct n2ir *r, uint64_t now_us)
{
	if (r->win_start_us == 0 || now_us - r->win_start_us < N2IR_WINDOW_US) { return 0; }
	return (r->reports || r->moves || r->errors) ? 1 : -1;
}

static inline void
n2ir_reset(struct n2ir *r, uint64_t now_us)
{
	uint64_t last = r->last_us;
	*r = (struct n2ir){ 0 };
	r->win_start_us = now_us; r->last_us = last;      /* a finger held across the edge keeps its gap chain */
}

static inline int
n2ir_format(const struct n2ir *r, uint64_t now_us, char *buf, size_t len)
{
	uint64_t win = now_us > r->win_start_us ? now_us - r->win_start_us : 1;
	uint64_t r10 = ((uint64_t)r->reports * 10000000ull + win / 2) / win;
	return snprintf(buf, len, "win_ms=%llu reports=%u (%llu.%llu/s) moves=%u errors=%u gap_ms<=10/12/16/20/25/33/50/more=%u/%u/%u/%u/%u/%u/%u/%u "
	    "gap_avg_us=%llu gap_max_us=%u read_avg_us=%llu read_max_us=%u",
	    (unsigned long long)(win / 1000), r->reports, (unsigned long long)(r10 / 10), (unsigned long long)(r10 % 10), r->moves, r->errors,
	    r->gap[0], r->gap[1], r->gap[2], r->gap[3], r->gap[4], r->gap[5], r->gap[6], r->gap[7],
	    (unsigned long long)(r->gaps ? r->gap_sum_us / r->gaps : 0), r->gap_max_us,
	    (unsigned long long)(r->reports ? r->read_sum_us / r->reports : 0), r->read_max_us);
}
#endif
