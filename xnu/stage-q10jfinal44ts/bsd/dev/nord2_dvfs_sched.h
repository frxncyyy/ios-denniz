#ifndef NORD2_DVFS_SCHED_H
#define NORD2_DVFS_SCHED_H
/*
 * 0460: the nord2_gov.h policy on the CPU clusters L and B.  One call every N2G_CPU_TICK_MS from the DVFS thread, after the bring-up of nord2_dvfs.h.
 * act = false computes and reports only (the 0172 governor keeps the rows; its fastest allowed row is held at or below the thermal ladder's);
 * act = true writes the rows with n2d_set_row_fast(), the 0172 path: one row per store, N2D_GOV_STEP_MS apart, CCI order included.  Nothing else
 * touches the hardware.  g->pin (mode 3) keeps both clusters at the fastest row the ladder allows: the reference for A/B runs.  LL stays on its bootloader row as before.
 */
#include "nord2_dvfs.h"
#include "nord2_gov.h"

#define N2S_L_LADDER 100      /* log code: a = thermal level 0..4, b = battery cC, c = gauge readable, d = fastest allowed row L | B << 8 */
#define N2S_L_SOC    101      /* 0643: a = thermal level, b = SoC cC + 100000 (0 = not readable), c = 1 calibration approximate, d = 1 used by the ladder */

struct n2s_ops {
	void (*ticks)(void *ctx, int cpu, uint32_t *busy, uint32_t *total);      /* cumulative 10 ms ticks of one CPU: user + system, and those plus idle */
	void (*hints)(void *ctx, struct n2g_hints *h);
	bool (*soc)(void *ctx, int *centi_c, bool *approx);                      /* 0643: hottest LVTS sensor, false = none (may be NULL) */
};

struct n2s_gov {
	struct n2g_cl cl[2];                 /* 0 = L (cpu4-6), 1 = B (cpu7) */
	struct n2g_act act;
	struct n2g_therm th;
	uint64_t last_ms, last_slow_ms;
	unsigned changes, ticks, cls_ticks[N2G_NCLASS];
	uint32_t lates0;
	bool acted;
	bool pin;                            /* mode 3: both clusters at the cap the thermal ladder allows, whatever the load (the A/B reference) */
	bool soc_used;                       /* 0643: the last ladder update used the SoC reading */
	int soc_cc;                          /* 0643: last SoC reading (centi-C), -99999 = none */
};

static bool n2s_prime_floors = true;     /* 0642: false = the 0460 floors (set before n2s_init) */

static inline void
n2s_init(const struct n2d_state *st, struct n2s_gov *g)
{
	/* idle, UI, animation, touch, (jank = cap).  0642: B (cpu7, the prime core that 0642's placement now fills first) gets floors at least as fast as L's
	 * (was 1998 / 2463 / 2463 = slower than L's 2200 / 2354 / 2354, so a UI thread moved to cpu7 would have run slower than on cpu4). */
	static const uint16_t fl[2][N2G_NCLASS] = { { 0, 2200, 2354, 2354, 0 }, { 0, 2463, 2713, 2713, 0 } };
	static const uint16_t fl_0460[2][N2G_NCLASS] = { { 0, 2200, 2354, 2354, 0 }, { 0, 1998, 2463, 2463, 0 } };
	static const uint16_t th[2][N2G_TH_LEVELS] = { { 0, 2354, 2200, 1740, 0 }, { 0, 2713, 2463, 1998, 0 } };
	static const uint16_t hi[2] = { 2200, 2463 };
	for (unsigned k = 0; k < sizeof(*g); k++) {
		((uint8_t *)g)[k] = 0;
	}
	for (unsigned k = 0; k < 2; k++) {
		unsigned c = N2DV_L + k;
		struct n2g_cl *cl = &g->cl[k];
		for (unsigned r = 0; r < N2G_CPU_ROWS; r++) {
			cl->mhz[r] = (uint16_t)n2d_row_mhz(c, r);
		}
		cl->fastest = st->target[c];
		cl->slowest = (uint8_t)n2d_row_at_least(c, k == 0 ? N2D_GOV_IDLE_L_MHZ : N2D_GOV_IDLE_B_MHZ, st->target[c], st->orig[c]);
		cl->ncpu = k == 0 ? 3 : 1;
		cl->hispeed_mhz = hi[k];
		for (unsigned i = 0; i < N2G_NCLASS; i++) {
			cl->floor_mhz[i] = n2s_prime_floors ? fl[k][i] : fl_0460[k][i];
		}
		for (unsigned i = 0; i < N2G_TH_LEVELS; i++) {
			cl->therm_mhz[i] = th[k][i];
		}
		cl->cur = cl->want = st->cur[c];
		cl->u_win = cl->u_fast = -1;
	}
	n2g_therm_init(&g->th);
}

/* true every N2D_GOV_SLOW_TICK_MS: the caller prints the DECISION line and then calls n2s_report_done() */
static inline bool
n2s_tick(const struct n2d_ops *o, const struct n2s_ops *so, void *ctx, const struct n2d_cfg *cfg, struct n2d_state *st, struct n2d_gov *old, struct n2s_gov *g, bool act)
{
	if (st->stopped || !st->inited) {
		return false;
	}
	uint64_t now = o->now_ms(ctx);
	if (g->last_ms != 0 && now - g->last_ms > 250u) {             /* the thread was away (mode 0, a long row walk): the windows start again */
		g->cl[0].n = g->cl[1].n = g->act.n = 0;
		g->cl[0].down_since = g->cl[1].down_since = 0;
	}
	g->last_ms = now ? now : 1;
	bool slow = g->last_slow_ms == 0 || now - g->last_slow_ms >= N2D_GOV_SLOW_TICK_MS, first = g->last_slow_ms == 0;
	if (slow) {
		int cC = 0, sc = 0, eff = 0;
		bool approx = false;
		unsigned was = g->th.level;
		bool ok = o->gauge(ctx, &cC);
		bool sok = so->soc != NULL && so->soc(ctx, &sc, &approx);                     /* 0643 */
		unsigned lv = n2g_therm_update2(&g->th, ok, cC, sok, sc, approx);
		bool used = n2g_therm_soc_usable(sok, sc, approx, ok, cC, &eff);
		if (lv != was || first) {
			o->log(ctx, N2S_L_LADDER, lv, ok ? (unsigned)cC : 0u, ok, n2g_cl_limit(&g->cl[0], lv) | n2g_cl_limit(&g->cl[1], lv) << 8);
		}
		if (lv != was || first || used != g->soc_used) {
			o->log(ctx, N2S_L_SOC, lv, sok ? (unsigned)(sc + 100000) : 0u, approx, used);
		}
		g->soc_used = used;
		g->soc_cc = sok ? sc : -99999;
		g->last_slow_ms = now ? now : 1;
	}
	struct n2g_hints h = { 0, 0, 0, 0, 0 };
	so->hints(ctx, &h);
	unsigned cls = n2g_act_update(&g->act, &h, now);
	if (g->pin) {
		cls = N2G_JANK;
	}
	g->ticks++;
	g->cls_ticks[cls]++;
	g->acted = act;
	for (unsigned k = 0; k < 2; k++) {
		unsigned c = N2DV_L + k;
		struct n2g_cl *cl = &g->cl[k];
		uint32_t busy[N2G_CPU_MAXCPU] = { 0, 0, 0, 0 }, tot[N2G_CPU_MAXCPU] = { 0, 0, 0, 0 };
		for (unsigned i = 0; i < cl->ncpu; i++) {
			so->ticks(ctx, n2d_first_cpu[c] + (int)i, &busy[i], &tot[i]);
		}
		cl->cur = st->cur[c];
		n2g_cl_sample(cl, busy, tot);
		unsigned want = n2g_cl_decide(cl, now, cls, g->th.level);
		if (act) {
			if (want != st->cur[c]) {
				n2d_set_row_fast(o, ctx, st, c, want);
				g->changes++;
			}
		} else if (old) {                                         /* the 0172 governor acts: it may not pick a row above the ladder's */
			if (old->limit[c] < cl->limit) {
				old->limit[c] = cl->limit;
			}
			if (old->limit[c] > old->slowest[c]) {
				old->limit[c] = old->slowest[c];
			}
		}
	}
	if (!slow || first) {
		return false;
	}
	if (act) {                                                    /* what the 0172 slow tick did besides its thermal guard */
		for (unsigned c = N2DV_L; c <= N2DV_B; c++) {
			o->log(ctx, N2D_L_GOVCLK, c, n2d_row_mhz(c, st->cur[c]), o->mhz(ctx, n2d_first_cpu[c]), 0);
		}
		if (cfg->place) {
			int next = n2d_place_decide(n2d_row_mhz(N2DV_L, st->target[N2DV_L]), n2d_row_mhz(N2DV_LL, st->cur[N2DV_LL]), st->place_mode);
			if (next != st->place_mode) {
				st->place_mode = next;
				o->set_place(ctx, next);
				o->log(ctx, N2D_L_PLACE, (unsigned)next, n2d_row_mhz(N2DV_L, st->target[N2DV_L]), n2d_row_mhz(N2DV_LL, st->cur[N2DV_LL]), 0);
			}
		}
	}
	return true;
}

static inline void
n2s_report_done(struct n2s_gov *g, const struct n2g_hints *h)
{
	g->changes = g->ticks = 0;
	for (unsigned i = 0; i < N2G_NCLASS; i++) {
		g->cls_ticks[i] = 0;
	}
	g->lates0 = h->lates;
}
#endif
