/*
 * 0394 host test: the coherent-copy decision (sweet_fb_coherent.h) against a simulated GPU writer.
 * One surface of H rows, each row holds the generation of the frame that last wrote it.  A GPU job writing frame g+1 is promised
 * (reservation, busy) at t_resv, starts writing at t_start and writes the rows in order over t_dur, done (not busy) at its end.
 * The kernel copy takes COPY_US for the whole surface (row r is read at t0 + r * COPY_US / H).  Old (0393): one copy.  New (0394):
 * the presentCopy loop as implemented (pre-wait, busy check per chunk, rows hash of 16 sampled rows, verdict, wait for idle).
 * A presented frame is MIXED when its rows come from more than one generation.
 * cc -O2 -I../b/iokit/Kernel/arm fbcoh_host_test.c -o /dev/null... (run_test.sh)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "sweet_fb_coherent.h"

#define H 2400
#define COPY_US 2600.0
#define SYNC_ROWS 16

struct job { double t_resv, t_start, t_dur; };

static bool busy_at(const struct job *j, double t) { return t >= j->t_resv && t < j->t_start + j->t_dur; }
/* generation of row r at time t (0 before the job wrote it, 1 after) */
static int gen_at(const struct job *j, int r, double t) {
	if (t < j->t_start) return 0;
	double wt = j->t_start + j->t_dur * (double)r / H;
	return t >= wt ? 1 : 0;
}
static uint64_t hash_at(const struct job *j, double t) {
	uint64_t h = 1469598103934665603ULL;
	for (int k = 0; k < SYNC_ROWS; k++) { int r = (int)((int64_t)(H - 1) * k / (SYNC_ROWS - 1)); h = (h ^ (uint64_t)gen_at(j, r, t)) * 1099511628211ULL; }
	return h;
}

struct res { bool mixed; int gen; unsigned copies; double waited, total; bool gave_up, skipped; };

static struct res copy_old(const struct job *j, double t0) {
	struct res r = { 0 };
	int g0 = gen_at(j, 0, t0), g = g0;
	for (int y = 0; y < H; y++) { g = gen_at(j, y, t0 + COPY_US * y / H); if (g != g0) r.mixed = true; }
	r.gen = g; r.copies = 1; r.total = COPY_US; return r;
}

static void wait_idle(const struct job *j, double *t, double *waited) {
	while (busy_at(j, *t)) {
		uint32_t p = sweet_fb_coh_poll_us((uint64_t)*waited);
		if (p == 0) break;
		*t += p; *waited += p;
	}
}

static struct res copy_new(const struct job *j, double t0, bool skip_ok) {
	struct res r = { 0 };
	double t = t0, waited = 0;
	if (busy_at(j, t)) wait_idle(j, &t, &waited);
	for (unsigned att = 0;; att++) {
		bool chk = sweet_fb_coh_chunk_checks(true, att, (uint64_t)waited, skip_ok);
		uint64_t h0 = hash_at(j, t);
		bool abort = false, mixed = false;
		int g0 = gen_at(j, 0, t), g = g0;
		double tc = t;
		for (int y = 0; y < H; y++) {
			tc = t + COPY_US * y / H;
			if (chk && sweet_fb_coh_check_due((uint32_t)y) && busy_at(j, tc)) { abort = true; break; }
			g = gen_at(j, y, tc); if (g != g0) mixed = true;
		}
		if (!abort) tc = t + COPY_US;
		t = tc;
		r.copies = att + 1;
		bool raced = hash_at(j, t) != h0;
		bool busy_after = !abort && busy_at(j, t);
		int v = sweet_fb_coh_verdict(true, att, abort, raced, busy_after, (uint64_t)waited, skip_ok);
		if (v == SWEET_FB_COH_SKIP) { r.skipped = true; r.mixed = false; break; }      /* the panel keeps the previous (complete) frame */
		if (v == SWEET_FB_COH_SHOW || v == SWEET_FB_COH_GIVE_UP) {
			if (abort) { fprintf(stderr, "BUG: shown copy was aborted\n"); exit(2); }
			r.mixed = mixed; r.gen = g; r.gave_up = v == SWEET_FB_COH_GIVE_UP; break;
		}
		wait_idle(j, &t, &waited);
	}
	r.waited = waited; r.total = t - t0; return r;
}

int main(void) {
	srand(394);
	unsigned unit_fail = 0;
	/* unit checks of the pure helpers */
	if (sweet_fb_coh_verdict(true, 0, false, false, false, 0, true) != SWEET_FB_COH_SHOW) unit_fail++;
	if (sweet_fb_coh_verdict(false, 0, true, true, true, 0, true) != SWEET_FB_COH_SHOW) unit_fail++;
	if (sweet_fb_coh_verdict(true, 0, true, false, false, 0, true) != SWEET_FB_COH_RETRY) unit_fail++;
	if (sweet_fb_coh_verdict(true, SWEET_FB_COH_MAX_ATTEMPTS - 1, true, false, false, 0, false) != SWEET_FB_COH_GIVE_UP) unit_fail++;
	if (sweet_fb_coh_verdict(true, SWEET_FB_COH_MAX_ATTEMPTS - 1, true, false, false, 0, true) != SWEET_FB_COH_SKIP) unit_fail++;
	if (sweet_fb_coh_verdict(true, 0, false, true, false, SWEET_FB_COH_BUDGET_US, false) != SWEET_FB_COH_GIVE_UP) unit_fail++;
	if (!sweet_fb_coh_skip_ok(true, true, 0) || sweet_fb_coh_skip_ok(true, false, 0) || sweet_fb_coh_skip_ok(false, true, 0) ||
	    sweet_fb_coh_skip_ok(true, true, SWEET_FB_COH_MAX_SKIPS)) unit_fail++;
	if (sweet_fb_coh_chunk_checks(true, SWEET_FB_COH_MAX_ATTEMPTS - 1, 0, false) || !sweet_fb_coh_chunk_checks(true, SWEET_FB_COH_MAX_ATTEMPTS - 1, 0, true)) unit_fail++;
	if (sweet_fb_coh_check_due(0) || !sweet_fb_coh_check_due(128) || sweet_fb_coh_check_due(129)) unit_fail++;
	if (sweet_fb_coh_poll_us(SWEET_FB_COH_BUDGET_US) != 0 || sweet_fb_coh_poll_us(0) != 1000 || sweet_fb_coh_poll_us(SWEET_FB_COH_BUDGET_US - 500) != 100) unit_fail++;
	/* the scroll case: the app's next frame into this surface starts anywhere from 12 ms before to 6 ms after the copy starts;
	 * render 3..12 ms; reservation 0..4 ms before the writes start */
	bool ok = true;
	for (int pol = 0; pol < 2; pol++) {
	bool skip_ok = pol == 0;
	unsigned n = 0, old_mixed = 0, new_mixed = 0, gave_up = 0, skipped = 0, copies2 = 0;
	double new_total_max = 0, waited_max = 0, waited_sum = 0;
	srand(394);
	for (int i = 0; i < 200000; i++) {
		struct job j;
		j.t_start = -12000.0 + (rand() % 18000);
		j.t_dur = 3000.0 + (rand() % 9000);
		j.t_resv = j.t_start - (rand() % 4000);
		struct res o = copy_old(&j, 0), w = copy_new(&j, 0, skip_ok);
		if (w.skipped) skipped++;
		n++;
		if (o.mixed) old_mixed++;
		if (w.mixed) new_mixed++;
		if (w.gave_up) gave_up++;
		if (w.copies > 1) copies2++;
		if (w.total > new_total_max) new_total_max = w.total;
		if (w.waited > waited_max) waited_max = w.waited;
		waited_sum += w.waited;
		if (w.waited > SWEET_FB_COH_BUDGET_US + 1000 + 1e-6) unit_fail++;     /* budget bound (+ one poll) */
	}
	printf("policy %s: cases=%u old_mixed=%u (%.2f%%) new_mixed=%u (%.3f%%) skipped=%u (%.2f%%) gave_up=%u recopies=%u waited_avg_us=%.0f waited_max_us=%.0f total_max_us=%.0f\n",
	    skip_ok ? "double-buffered (skip allowed)" : "single buffer (no skip)", n, old_mixed, 100.0 * old_mixed / n, new_mixed, 100.0 * new_mixed / n,
	    skipped, 100.0 * skipped / n, gave_up, copies2, waited_sum / n, waited_max, new_total_max);
	if (skip_ok && new_mixed != 0) ok = false;              /* double-buffered: a mixed frame is never shown */
	if (!skip_ok && new_mixed * 2 > old_mixed) ok = false;  /* single buffer: at least halved */
	}
	/* a never-idle surface (single-buffered writer): bounded, one complete copy shown */
	{
		struct job j = { -1e9, 500.0, 1e9 };
		struct res w = copy_new(&j, 0, false);
		if (!w.gave_up || w.copies > SWEET_FB_COH_MAX_ATTEMPTS || w.waited > SWEET_FB_COH_BUDGET_US + 1000) unit_fail++;
		printf("never-idle: copies=%u waited_us=%.0f total_us=%.0f gave_up=%d\n", w.copies, w.waited, w.total, w.gave_up);
	}
	printf("unit_fail=%u\n", unit_fail);
	ok = ok && unit_fail == 0;
	printf("%s\n", ok ? "ALL OK" : "FAIL");
	return ok ? 0 : 1;
}
