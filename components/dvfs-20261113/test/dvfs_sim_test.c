/* Host simulation of 0460/0461/0462: nord2_gov.h (policy), nord2_dvfs_sched.h (CPU glue over the 0172 row path), n2clk_retune (GPU clock), nord2_dram_rate.h.
 * Deterministic: a 1 ms clock, synthetic load traces, 10 ms tick counters like processor_cpu_load_info, every CSRAM store recorded. */
#include "nord2_dvfs_sched.h"
#include "nord2_dram_rate.h"
#include "nord2_gpu_clk.h"
#include "nord2_gpu_rail.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ CPU model */
struct sev { uint32_t off, v; uint64_t t; };
struct sim {
	uint64_t now;                       /* ms */
	double busy[8], tot[8];             /* ms */
	double demand[8];                   /* work of the busiest thread on the CPU, in MHz of its cluster: busy share = demand / clock (max 1) */
	struct n2d_state st;
	struct n2d_gov old;
	struct n2s_gov g;
	struct n2g_hints h;
	struct n2g_swap sw;
	int cC; bool gauge_ok;
	struct sev ev[200000]; unsigned nev;
	void (*scen)(struct sim *, uint64_t t);
	uint64_t t0;
	unsigned place, logs, ladder_logs;
	uint64_t swap_next_us; unsigned swap_per_us;      /* running swap stream (0 = none) */
	/* trace of the applied rows at every tick */
	unsigned maxL, minL, maxB, minB;
};
static struct sim S;
static const struct n2d_cfg CFG = { 3000, false, 3, true };

static unsigned mhz_of(const struct sim *s, int cpu) { unsigned c = cpu < 4 ? N2DV_LL : (cpu < 7 ? N2DV_L : N2DV_B); return n2d_row_mhz(c, s->st.cur[c]); }
static void advance(struct sim *s, unsigned ms)
{
	for (unsigned i = 0; i < ms; i++) {
		if (s->scen) s->scen(s, s->now - s->t0);
		while (s->swap_per_us && s->swap_next_us <= s->now * 1000ull) {       /* swaps arrive on their own clock */
			n2g_swap_event(&s->sw, s->swap_next_us);
			s->h.swap_ms = s->swap_next_us / 1000; s->h.swaps = s->sw.count; s->h.lates = s->sw.late; s->h.late_ms = s->sw.late_us / 1000;
			s->swap_next_us += s->swap_per_us;
		}
		for (int c = 4; c < 8; c++) {
			double u = s->demand[c] / (double)mhz_of(s, c);
			s->busy[c] += u > 1.0 ? 1.0 : u; s->tot[c] += 1.0;
		}
		s->now++;
	}
}
static void f_w32(void *c, uint32_t off, uint32_t v) { struct sim *s = c; assert(s->nev < 200000); s->ev[s->nev++] = (struct sev){ off, v, s->now }; }
static uint32_t f_r32(void *c, uint32_t off) { (void)c; (void)off; return 0; }
static int f_mbox(void *c, unsigned ch, const uint32_t *w, unsigned n, uint32_t *r, unsigned nr, unsigned to) { (void)c; (void)ch; (void)w; (void)n; (void)r; (void)nr; (void)to; assert(!"no mailbox in the governor"); return -1; }
static bool f_share(void *c, uint32_t a, uint32_t b) { (void)c; (void)a; (void)b; assert(!"no share"); return false; }
static bool f_rail(void *c, unsigned cl, uint32_t *a, uint32_t *b) { (void)c; (void)cl; (void)a; (void)b; assert(!"no PMIC in the governor"); return false; }
static unsigned f_mhz(void *c, int cpu) { return mhz_of(c, cpu); }
static bool f_gauge(void *c, int *cC) { struct sim *s = c; if (!s->gauge_ok) return false; *cC = s->cC; return true; }
static void f_sleep(void *c, unsigned ms) { advance(c, ms); }
static void f_place(void *c, int m) { struct sim *s = c; (void)m; s->place++; }
static void f_log(void *c, int code, unsigned a, unsigned b, unsigned cc, unsigned d) { struct sim *s = c; (void)a; (void)b; (void)cc; (void)d; s->logs++; if (code == N2S_L_LADDER) s->ladder_logs++; }
static void f_flush(void *c) { (void)c; assert(!"no flush wait in the governor"); }
static int f_util(void *c, int cpu) { (void)c; (void)cpu; return -1; }
static uint64_t f_now(void *c) { return ((struct sim *)c)->now; }
static uint64_t f_kick(void *c) { return ((struct sim *)c)->h.swap_ms; }
static const struct n2d_ops OPS = { f_w32, f_r32, f_mbox, f_share, f_rail, f_mhz, f_gauge, f_sleep, f_place, f_log, f_flush, f_util, f_now, f_kick };
static void s_ticks(void *c, int cpu, uint32_t *busy, uint32_t *tot)
{
	struct sim *s = c;
	uint32_t b = (uint32_t)(s->busy[cpu] / 10.0), i = (uint32_t)((s->tot[cpu] - s->busy[cpu]) / 10.0);     /* user+system and idle are floored separately */
	*busy = b; *tot = b + i;
}
static void s_hints(void *c, struct n2g_hints *h) { *h = ((struct sim *)c)->h; }
static const struct n2s_ops SOPS = { s_ticks, s_hints };

static void sim_init(struct sim *s, int cC, bool gauge_ok)
{
	memset(s, 0, sizeof(*s));
	s->now = 100000; s->t0 = s->now; s->cC = cC; s->gauge_ok = gauge_ok;
	s->st.inited = true; s->st.orig[0] = 1; s->st.orig[1] = 14; s->st.orig[2] = 15;
	s->st.target[0] = 1; s->st.target[1] = 0; s->st.target[2] = 0;                 /* n2dvfs=3000: the cap rows are L 2600, B 3000 */
	s->st.cur[0] = 1; s->st.cur[1] = 0; s->st.cur[2] = 0;                          /* the bring-up leaves the clusters on the cap rows */
	s->gauge_ok = gauge_ok;
	n2d_gov_init(&OPS, s, &s->st, &s->old);
	n2s_init(&s->st, &s->g);
	assert(s->g.cl[0].slowest == 12 && s->g.cl[1].slowest == 13 && s->g.cl[0].fastest == 0 && s->g.cl[1].fastest == 0);
}
static unsigned L(const struct sim *s) { return n2d_row_mhz(N2DV_L, s->st.cur[N2DV_L]); }
static unsigned Bm(const struct sim *s) { return n2d_row_mhz(N2DV_B, s->st.cur[N2DV_B]); }
/* run until t0 + until_ms; every tick: nothing above the thermal cap */
static unsigned run(struct sim *s, uint64_t until_ms, bool act)
{
	unsigned reports = 0;
	s->maxL = s->maxB = 0; s->minL = s->minB = 99999;
	while (s->now - s->t0 < until_ms) {
		advance(s, N2G_CPU_TICK_MS);
		if (n2s_tick(&OPS, &SOPS, s, &CFG, &s->st, &s->old, &s->g, act)) { reports++; n2s_report_done(&s->g, &s->h); }
		if (act) {
			assert(s->st.cur[N2DV_L] >= n2g_cl_limit(&s->g.cl[0], s->g.th.level) && s->st.cur[N2DV_B] >= n2g_cl_limit(&s->g.cl[1], s->g.th.level));
			assert(s->st.cur[N2DV_L] <= s->g.cl[0].slowest && s->st.cur[N2DV_B] <= s->g.cl[1].slowest && s->st.cur[N2DV_LL] == 1);
		}
		if (L(s) > s->maxL) s->maxL = L(s);
		if (L(s) < s->minL) s->minL = L(s);
		if (Bm(s) > s->maxB) s->maxB = Bm(s);
		if (Bm(s) < s->minB) s->minB = Bm(s);
	}
	return reports;
}
static unsigned req_stores(const struct sim *s, unsigned from, unsigned c) { unsigned n = 0; for (unsigned i = from; i < s->nev; i++) n += s->ev[i].off == N2DV_OFF_REQ + 4 * c; return n; }

/* Every store the governor made is the 0172 single-row step: [CCI first when going faster] limit, request, [CCI last when going slower]; the row moves by exactly one;
 * two steps of one walk are N2D_GOV_STEP_MS apart. */
static void check_row_path(const struct sim *s)
{
	uint8_t row[3] = { 1, 0, 0 };
	unsigned i = 0, steps = 0;
	uint64_t last_t[3] = { 0, 0, 0 };
	while (i < s->nev) {
		const struct sev *e = &s->ev[i];
		bool cci_first = e->off == N2DV_OFF_REQ + 4 * N2DV_CCI;
		if (cci_first) { i++; e = &s->ev[i]; assert(i < s->nev); }
		assert(e->off >= N2DV_OFF_PPM_LIMIT + 4 * N2DV_L && e->off <= N2DV_OFF_PPM_LIMIT + 4 * N2DV_B);      /* LL is never written */
		unsigned c = (e->off - N2DV_OFF_PPM_LIMIT) / 4, step = e->v & 0xffff;
		assert(e->v == (step << 16 | step));
		assert(step == (unsigned)row[c] + 1 || step + 1 == row[c]);                                         /* one row at a time */
		assert(cci_first == (step < row[c]));
		assert(i + 1 < s->nev && s->ev[i + 1].off == N2DV_OFF_REQ + 4 * c && s->ev[i + 1].v == step);
		uint8_t next[3] = { row[0], row[1], row[2] }; next[c] = (uint8_t)step;
		uint32_t cci = n2dv_cci_row(next[N2DV_LL], next[N2DV_B]);
		if (cci_first) assert(s->ev[i - 1].v == cci);
		i += 2;
		if (!cci_first) { assert(i < s->nev && s->ev[i].off == N2DV_OFF_REQ + 4 * N2DV_CCI && s->ev[i].v == cci); i++; }
		if (last_t[c] != 0) assert(e->t >= last_t[c] + N2D_GOV_STEP_MS);                                    /* never two rows of a cluster closer than the step time */
		last_t[c] = e->t; row[c] = (uint8_t)step; steps++;
	}
	assert(row[1] == s->st.cur[1] && row[2] == s->st.cur[2]);
	printf("  row path: %u single-row steps, %u stores, all in the 0172 order, >= %u ms apart per cluster\n", steps, s->nev, N2D_GOV_STEP_MS);
}

/* scenarios */
static void sc_touch(struct sim *s, uint64_t t) { if (t >= 1000 && t < 1300 && (t % 8) == 0) s->h.touch_ms = s->now; }      /* finger down at 1.0 s, moves until 1.3 s */
static void sc_scroll(struct sim *s, uint64_t t)
{
	s->demand[4] = s->demand[5] = 900; s->demand[7] = 400;
	if (t == 1000) { s->swap_per_us = 11111; s->swap_next_us = s->now * 1000ull; }
	if (t == 2000) s->swap_next_us += 11111;                                    /* one frame late at 2.0 s (22 ms between two swaps) */
	if (t == 4000) s->swap_per_us = 0;
	if (t >= 4000) s->demand[4] = s->demand[5] = s->demand[7] = 0;
}
static void sc_heavy(struct sim *s, uint64_t t) { double d = t >= 1000 && t < 4000 ? 5000 : 0; s->demand[5] = d; s->demand[7] = d; }
static void sc_mid(struct sim *s, uint64_t t) { (void)t; s->demand[4] = 1200; s->demand[7] = 700; }
static void sc_video60(struct sim *s, uint64_t t) { if (t == 500) { s->swap_per_us = 16667; s->swap_next_us = s->now * 1000ull; } s->demand[4] = 300; }
static void sc_therm(struct sim *s, uint64_t t)
{
	s->demand[4] = s->demand[5] = s->demand[6] = s->demand[7] = 5000;
	static const struct { uint64_t t; int c; } tr[] = { { 0, 3900 }, { 4000, 4100 }, { 8000, 4290 }, { 12000, 4300 }, { 16000, 4520 }, { 20000, 4700 }, { 24000, 4610 }, { 28000, 4590 },
		{ 32000, 4410 }, { 36000, 4390 }, { 40000, 4210 }, { 44000, 4190 }, { 48000, 4010 }, { 52000, 3990 } };
	for (unsigned i = 0; i < sizeof(tr) / sizeof(tr[0]); i++) if (t >= tr[i].t) s->cC = tr[i].c;
}

static void test_cpu(void)
{
	struct sim *s = &S;
	unsigned n0;

	printf("CPU 1: idle\n");
	sim_init(s, 3500, true);
	run(s, 1000, true);                                                          /* from the cap rows down to the idle rows */
	assert(L(s) == 1046 && Bm(s) == 1108);
	n0 = s->nev; run(s, 4000, true);
	assert(s->nev == n0 && s->minL == 1046 && s->maxL == 1046 && s->maxB == 1108 && s->g.act.cls == N2G_IDLE);      /* idle: minimum rows, not one store */
	check_row_path(s);

	printf("CPU 2: touch burst (finger down at 1.0 s, moving until 1.3 s, no load)\n");
	sim_init(s, 3500, true); s->scen = sc_touch;
	run(s, 900, true); assert(L(s) == 1046 && Bm(s) == 1108);
	run(s, 1060, true); printf("  +60 ms: L %u B %u\n", L(s), Bm(s));
	assert(L(s) >= 2354 && Bm(s) >= 2463);                                       /* within one tick plus the row walk (2 ms a row) */
	run(s, 1680, true); assert(s->minL >= 2354 && s->minB >= 2463 && s->maxL == 2354 && s->maxB == 2463);         /* held 400 ms after the last finger event, no higher without load */
	run(s, 1800, true); assert(L(s) == 1046 && Bm(s) == 1108);                   /* then down after the 40 ms hold */
	check_row_path(s);

	printf("CPU 3: scroll at 90 fps from 1.0 s, a late frame at 2.0 s, end at 4.0 s (L load 900, B 400 MHz-equivalent)\n");
	sim_init(s, 3500, true); s->scen = sc_scroll;
	run(s, 900, true); printf("  before the scroll (load only): L %u B %u\n", L(s), Bm(s)); assert(L(s) >= 1162 && L(s) <= 1335 && Bm(s) == 1108);     /* 900 / 0.8 = 1125 MHz */
	run(s, 1120, true); printf("  +120 ms: L %u B %u class %u\n", L(s), Bm(s), s->g.act.cls);
	assert(s->g.act.cls == N2G_ANIM && L(s) >= 2354 && Bm(s) >= 2463);
	run(s, 1990, true); assert(s->minL >= 2354 && s->maxL == 2354 && s->minB == 2463 && s->maxB == 2463 && s->sw.late == 0);     /* animation floors, the load alone asks for less */
	run(s, 2060, true); printf("  late frame: lates %u, +60 ms: L %u B %u class %u\n", s->sw.late, L(s), Bm(s), s->g.act.cls);
	assert(s->sw.late == 1 && s->g.act.cls == N2G_JANK && L(s) == 2600 && Bm(s) == 3000);                        /* a late frame: both clusters to the cap */
	run(s, 2580, true); assert(s->minL == 2600 && s->minB == 3000);
	run(s, 2800, true); assert(L(s) == 2354 && Bm(s) == 2463 && s->g.act.cls == N2G_ANIM);                        /* 600 ms later back to the animation floors */
	run(s, 3990, true); assert(s->sw.late == 1 && s->minL == 2354);
	run(s, 4250, true); assert(s->g.act.cls == N2G_UI || s->g.act.cls == N2G_ANIM || s->g.act.cls == N2G_IDLE);
	run(s, 4600, true); assert(L(s) == 1046 && Bm(s) == 1108 && s->g.act.cls == N2G_IDLE);
	check_row_path(s);

	printf("CPU 4: sustained heavy load from 1.0 s to 4.0 s, no UI hint\n");
	sim_init(s, 3500, true); s->scen = sc_heavy;
	run(s, 980, true); assert(L(s) == 1046);
	{
		uint64_t t_hi = 0, t_cap = 0, t_capB = 0;
		while (s->now - s->t0 < 1500) { run(s, s->now - s->t0 + 20, true); if (!t_hi && L(s) >= 2200) t_hi = s->now - s->t0; if (!t_cap && L(s) == 2600) t_cap = s->now - s->t0; if (!t_capB && Bm(s) == 3000) t_capB = s->now - s->t0; }
		printf("  L >= 2200 MHz after %llu ms, L at 2600 after %llu ms, B at 3000 after %llu ms\n", (unsigned long long)t_hi - 1000, (unsigned long long)t_cap - 1000, (unsigned long long)t_capB - 1000);
		assert(t_hi && t_hi - 1000 <= 100 && t_cap && t_cap - 1000 <= 200 && t_capB && t_capB - 1000 <= 200);
	}
	n0 = s->nev; run(s, 4000, true); assert(s->nev == n0 && s->minL == 2600 && s->minB == 3000);                    /* stays at the cap, no row change */
	{
		uint64_t t_idle = 0; unsigned prev = L(s); uint64_t prev_t = 0, min_gap = 9999;
		while (s->now - s->t0 < 5000) {
			run(s, s->now - s->t0 + 20, true);
			if (L(s) != prev) { assert(L(s) < prev); if (prev_t && s->now - prev_t < min_gap) min_gap = s->now - prev_t; prev_t = s->now; prev = L(s); }
			if (!t_idle && L(s) == 1046 && Bm(s) == 1108) t_idle = s->now - s->t0;
		}
		printf("  load gone: idle rows after %llu ms, smallest time between two slower moves %llu ms\n", (unsigned long long)t_idle - 4000, (unsigned long long)min_gap);
		assert(t_idle && t_idle - 4000 <= 400 && min_gap >= N2G_CPU_DOWN_MS);
	}
	check_row_path(s);

	printf("CPU 5: steady medium load (L 1200, B 700 MHz-equivalent), no hint: settles near 1.25 x load, little hunting\n");
	sim_init(s, 3500, true); s->scen = sc_mid;
	run(s, 2000, true);
	n0 = req_stores(s, 0, N2DV_L) + req_stores(s, 0, N2DV_B);
	run(s, 6000, true);
	printf("  L %u..%u MHz, B %u..%u MHz, %u row steps in 4 s\n", s->minL, s->maxL, s->minB, s->maxB, req_stores(s, 0, N2DV_L) + req_stores(s, 0, N2DV_B) - n0);
	assert(s->minL >= 1335 && s->maxL <= 1855 && s->minB >= 921 && s->maxB <= 1258);
	assert(req_stores(s, 0, N2DV_L) + req_stores(s, 0, N2DV_B) - n0 <= 40);
	check_row_path(s);

	printf("CPU 6: 60 fps video on the 90 Hz panel is not jank\n");
	sim_init(s, 3500, true); s->scen = sc_video60;
	run(s, 4000, true);
	assert(s->sw.late == 0 && s->g.act.cls == N2G_ANIM && s->maxL == 2600 /* the start */ && L(s) == 2354);

	printf("CPU 7: thermal ladder under full load (39.0 -> 47.0 -> 39.9 C)\n");
	sim_init(s, 3900, true); s->scen = sc_therm;
	{
		static const struct { uint64_t t; unsigned lv, l, b; } want[] = { { 3900, 0, 2600, 3000 }, { 7900, 1, 2354, 2713 }, { 11900, 1, 2354, 2713 }, { 15900, 2, 2200, 2463 }, { 19900, 3, 1740, 1998 },
			{ 23900, 4, 1046, 1108 }, { 27900, 4, 1046, 1108 }, { 31900, 3, 1740, 1998 }, { 35900, 3, 1740, 1998 }, { 39900, 2, 2200, 2463 }, { 43900, 2, 2200, 2463 }, { 47900, 1, 2354, 2713 },
			{ 51900, 1, 2354, 2713 }, { 55900, 0, 2600, 3000 } };
		for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
			run(s, want[i].t, true);
			printf("  %2llu s: %d.%02d C level %u L %u B %u\n", (unsigned long long)want[i].t / 1000, s->cC / 100, s->cC % 100, s->g.th.level, L(s), Bm(s));
			assert(s->g.th.level == want[i].lv && L(s) == want[i].l && Bm(s) == want[i].b);
		}
		assert(s->ladder_logs == 9);                                             /* the first reading and every level change */
	}
	check_row_path(s);

	printf("CPU 8: gauge unreadable at the start: idle rows only, whatever the load; the first reading opens the ladder\n");
	sim_init(s, 3500, false); s->scen = sc_therm;
	run(s, 3900, true); assert(s->g.th.level == 4 && L(s) == 1046 && Bm(s) == 1108);
	s->scen = sc_heavy; s->t0 = s->now - 1500; s->gauge_ok = true; s->cC = 3500;
	run(s, 3900, true); assert(s->g.th.level == 0 && L(s) == 2600 && Bm(s) == 3000);
	s->gauge_ok = false; run(s, 3990, true); assert(s->g.th.level == 0);          /* unreadable later: the level is kept */

	printf("CPU 9: mode 1 (decisions only): nothing is written, the 0172 governor's limit follows the ladder\n");
	sim_init(s, 4350, true); s->scen = sc_heavy;
	run(s, 3000, false);
	assert(s->nev == 0 && s->st.cur[1] == 0 && s->st.cur[2] == 0);                /* not one store */
	assert(s->g.th.level == 2 && s->old.limit[N2DV_L] == 3 && s->old.limit[N2DV_B] == 4);     /* 2200 / 2463 MHz */
	assert(s->g.cl[0].mhz[s->g.cl[0].want] == 2200);                              /* the decision that would have been written */

	printf("CPU 9b: mode 3 (pinned): cap rows without load, still under the ladder\n");
	sim_init(s, 3500, true); s->g.pin = true;
	run(s, 1000, true); assert(L(s) == 2600 && Bm(s) == 3000 && s->nev == 0);
	s->cC = 4350; run(s, 4000, true); assert(L(s) == 2200 && Bm(s) == 2463);
	s->g.pin = false; run(s, 4400, true); assert(L(s) == 1046 && Bm(s) == 1108);

	printf("CPU 10: stopped state and reports\n");
	sim_init(s, 3500, true);
	assert(run(s, 10000, true) == 4);                                            /* a report every 2 s, none on the first tick */
	s->st.stopped = true; n0 = s->nev; s->demand[5] = 5000; run(s, 11000, true); assert(s->nev == n0);
}

/* ------------------------------------------------------------------ activity / thermal units */
static void test_units(void)
{
	struct n2g_therm t;
	n2g_therm_init(&t);
	assert(t.level == 4 && n2g_therm_update(&t, false, 0) == 4);
	assert(n2g_therm_update(&t, true, 4099) == 0 && n2g_therm_update(&t, true, 4100) == 1 && n2g_therm_update(&t, true, 4001) == 1 && n2g_therm_update(&t, true, 3999) == 0);
	assert(n2g_therm_update(&t, true, 4800) == 4 && n2g_therm_update(&t, true, 4600) == 4 && n2g_therm_update(&t, true, 4599) == 3 && n2g_therm_update(&t, true, 3000) == 0);
	assert(n2g_therm_update(&t, true, 4500) == 3 && n2g_therm_update(&t, false, 9999) == 3);

	struct n2g_swap w; memset(&w, 0, sizeof(w));
	uint64_t us = 1000000;
	for (int i = 0; i < 90; i++) { n2g_swap_event(&w, us); us += 11111; }
	assert(w.late == 0 && w.per_us >= 11000 && w.per_us <= 11200 && w.count == 90);
	us += 11111; n2g_swap_event(&w, us); assert(w.late == 1 && w.late_us == us);       /* a 22 ms gap in a 90 fps stream */
	us += 11111; n2g_swap_event(&w, us); us += 11111; n2g_swap_event(&w, us); assert(w.late == 1);
	us += 500000; n2g_swap_event(&w, us); assert(w.late == 1 && w.per_us == 0);          /* a pause is not a late frame */
	for (int i = 0; i < 60; i++) { us += 16667; n2g_swap_event(&w, us); }
	assert(w.late == 1);                                                                /* 60 fps content */
	us += 4000; n2g_swap_event(&w, us);                                                 /* two swaps bunched: the period estimate drops to the 8 ms floor ... */
	unsigned l0 = w.late; for (int i = 0; i < 60; i++) { us += 16667; n2g_swap_event(&w, us); }
	printf("units: 60 fps after a bunched pair: %u false late frames before the period is learned again\n", w.late - l0);
	assert(w.late - l0 <= 3);                                                           /* ... and recovers within three frames */

	assert(n2g_gpu_max_idx(750000, 750000) == 4 && n2g_gpu_max_idx(800000, 800000) == 5 && n2g_gpu_max_idx(650000, 750000) == 2 && n2g_gpu_max_idx(600000, 750000) == 0 &&
	    n2g_gpu_max_idx(800000, 750000) == 4 && n2g_gpu_max_idx(743750, 750000) == 3);
	for (unsigned i = 0; i < N2G_GPU_NOPP; i++) { unsigned oi; assert(n2rail_opp_index_exact(n2g_gpu_khz[i], &oi) && n2g_gpu_index(n2g_gpu_khz[i]) == (int)i); }   /* every OPP is an exact vendor entry */
	assert(n2g_gpu_index(500000) == -1);
	for (unsigned i = 0; i < N2G_GPU_NOPP; i++) {      /* the rails n2g_gpu_max_idx asks for are the vendor table's */
		unsigned oi; (void)n2rail_opp_index_exact(n2g_gpu_khz[i], &oi);
		assert(n2g_gpu_max_idx(n2rail_opp_table[oi].vgpu_uv, n2rail_opp_table[oi].vsram_uv) == i);
		if (n2rail_opp_table[oi].vgpu_uv >= 6250) assert(n2g_gpu_max_idx(n2rail_opp_table[oi].vgpu_uv - 6250, n2rail_opp_table[oi].vsram_uv) < i || i == 0);
	}
}

/* ------------------------------------------------------------------ GPU policy */
struct gsim { struct n2g_gpu g; uint64_t now; unsigned switches; double work; bool boost; unsigned level; bool act; unsigned ups, downs; };
static void gstep(struct gsim *s, unsigned ms)
{
	for (unsigned t = 0; t < ms; t += N2G_GPU_POLL_MS) {
		s->now += N2G_GPU_POLL_MS;
		double u = s->work / (double)n2g_gpu_khz[s->g.cur];
		n2g_gpu_sample(&s->g, (unsigned)((u > 1.0 ? 1.0 : u) * 1000.0));
		unsigned want = n2g_gpu_decide(&s->g, s->now, s->boost, s->level);
		assert(want <= s->g.max_idx && n2g_gpu_khz[want] <= n2g_gpu_therm_khz[s->level]);       /* never above the rails or the thermal cap */
		if (s->act && want != s->g.cur) { if (want > s->g.cur) s->ups++; else s->downs++; n2g_gpu_applied(&s->g, want, s->now); s->switches++; }
	}
}
static void ginit(struct gsim *s) { memset(s, 0, sizeof(*s)); s->now = 50000; s->g.cur = 2; s->g.max_idx = 4; s->act = true; }
static unsigned gk(const struct gsim *s) { return n2g_gpu_khz[s->g.cur] / 1000; }
static void test_gpu(void)
{
	struct gsim s;
	printf("GPU 1: idle from the cold-start OPP\n");
	ginit(&s); gstep(&s, 80); assert(gk(&s) == 654);                             /* not before 100 ms of history */
	gstep(&s, 60); assert(gk(&s) == 350 && s.switches == 1);
	gstep(&s, 5000); assert(gk(&s) == 350 && s.switches == 1);
	printf("GPU 2: UI boost (touch / swap burst): 654 MHz floor at once, released 100 ms after the load is gone\n");
	s.boost = true; gstep(&s, 20); assert(gk(&s) == 654 && s.g.why == N2G_W_BOOST);
	s.work = 200000; gstep(&s, 1000); assert(gk(&s) == 654 && s.switches == 2);
	s.boost = false; gstep(&s, 200); assert(gk(&s) == 350 && s.switches == 3);    /* 200/350 = 57 % */
	printf("GPU 3: heavy (98 %% busy at 654): highest allowed OPP within 60 ms, no oscillation\n");
	ginit(&s); s.work = 640000; gstep(&s, 60); assert(gk(&s) == 836 && s.switches == 1);
	gstep(&s, 5000); assert(gk(&s) == 836 && s.switches == 1);                    /* 76.6 % at 836; 805 would be 79.5 %: stays */
	printf("GPU 4: load drops to 60 %% of 836: one step to the lowest OPP under 75 %% projected, then stable\n");
	s.work = 500000; gstep(&s, 400); assert(gk(&s) == 805 && s.switches == 2);
	gstep(&s, 5000); assert(gk(&s) == 805 && s.switches == 2);
	s.work = 250000; gstep(&s, 400); assert(gk(&s) == 350 && s.switches <= 4);    /* 71 % at 350; one stop on the way while the 100 ms window still holds the old load */
	{ unsigned sw = s.switches; gstep(&s, 5000); assert(gk(&s) == 350 && s.switches == sw);
	  s.work = 320000; gstep(&s, 5000);                                           /* 91 % at 350 -> the cap, then down to the fitting OPP */
	  printf("  320 MHz-equivalent load from 350: settles at %u MHz after %u switches\n", gk(&s), s.switches - sw);
	  assert(gk(&s) == 512 && s.switches - sw <= 3); }
	printf("GPU 5: hysteresis sweep: for every constant load the clock settles (at most 2 switches) and the busy share stays under 88 %% unless at the cap\n");
	for (unsigned w = 0; w <= 900000; w += 10000) {
		ginit(&s); s.work = w; gstep(&s, 3000);
		unsigned sw = s.switches; gstep(&s, 3000);
		assert(s.switches == sw && sw <= 2);
		assert(w * 1000ull / n2g_gpu_khz[s.g.cur] < N2G_GPU_UP_PM || s.g.cur == s.g.max_idx);
	}
	printf("GPU 6: thermal ladder caps at once, recovery only through the normal policy\n");
	ginit(&s); s.work = 800000; gstep(&s, 200); assert(gk(&s) == 836);
	s.level = 1; gstep(&s, 20); assert(gk(&s) == 805 && s.g.why == N2G_W_THERM);
	s.level = 2; gstep(&s, 20); assert(gk(&s) == 654);
	s.level = 3; gstep(&s, 20); assert(gk(&s) == 512);
	s.level = 4; gstep(&s, 20); assert(gk(&s) == 350);
	s.boost = true; gstep(&s, 500); assert(gk(&s) == 350);                        /* the boost floor does not beat the cap */
	s.level = 0; gstep(&s, 100); assert(gk(&s) == 836);
	printf("GPU 7: decisions only: the clock never moves, the decision is still made\n");
	ginit(&s); s.act = false; s.work = 640000; gstep(&s, 2000); assert(gk(&s) == 654 && s.switches == 0 && n2g_gpu_khz[s.g.want] == 836000 && s.g.why == N2G_W_UP);
	s.work = 0; gstep(&s, 2000); assert(gk(&s) == 654 && n2g_gpu_khz[s.g.want] == 350000 && s.g.why == N2G_W_DOWN);
	printf("GPU 8: rails at 0.80 V would allow 886; at 0.65 V nothing above 654\n");
	ginit(&s); s.g.max_idx = 5; s.work = 900000; gstep(&s, 200); assert(gk(&s) == 886);
	ginit(&s); s.g.max_idx = 2; s.work = 900000; gstep(&s, 2000); assert(gk(&s) == 654 && s.switches == 0);
}

/* ------------------------------------------------------------------ GPU clock retune */
struct cm { uint32_t cfg5, shadow, con0, con1, con3, cg; unsigned nw; struct { int b; uint32_t off, v, parent; } w[64]; bool pll_stuck, park_stuck; unsigned delays; uint32_t eff_khz_min, eff_khz_max; };
static uint32_t cm_parent(const struct cm *m) { return (m->cfg5 >> 8) & 3; }
static uint32_t c_rd(void *x, enum n2clk_block b, uint32_t off)
{
	struct cm *m = x;
	if (b == N2CLK_TOPCK && off == N2CLK_CFG_5) return m->cfg5;
	if (b == N2CLK_APMIXED) return off == N2CLK_MFGPLL_CON0 ? m->con0 : (off == N2CLK_MFGPLL_CON1 ? m->con1 : (off == N2CLK_MFGPLL_CON3 ? m->con3 : 0));
	if (b == N2CLK_MFGCFG && off == N2CLK_MFG_CG_STA) return m->cg;
	return 0;
}
static void c_wr(void *x, enum n2clk_block b, uint32_t off, uint32_t v)
{
	struct cm *m = x;
	assert(m->nw < 64); m->w[m->nw].b = b; m->w[m->nw].off = off; m->w[m->nw].v = v; m->w[m->nw].parent = cm_parent(m); m->nw++;
	if (b == N2CLK_TOPCK) {
		if (off == N2CLK_CFG_5_SET) m->shadow |= v;
		else if (off == N2CLK_CFG_5_CLR) m->shadow &= ~v;
		else if (off == N2CLK_CFG_UPDATE) { assert(v == 1u << 21); if (!(m->park_stuck && ((m->shadow >> 8) & 3) == 3)) m->cfg5 = m->shadow; }
		else assert(!"unexpected TOPCK store");
		m->cfg5 = (m->cfg5 & 0x300u) | (m->shadow & ~0x300u);                  /* the gate bit is immediate, the mux field waits for the update */
	} else if (b == N2CLK_APMIXED) {
		if (off == N2CLK_MFGPLL_CON1) { assert(cm_parent(m) == 3 || !"the PLL moves only while the GPU is on the parking clock"); if (!m->pll_stuck) m->con1 = v; }
		else if (off == N2CLK_MFGPLL_CON0) m->con0 = v;
		else if (off == N2CLK_MFGPLL_CON3) m->con3 = v;
		else assert(!"unexpected APMIXED store");
	} else if (off == N2CLK_MFG_CG_CLR) m->cg &= ~v;
}
static void c_delay(void *x, unsigned us) { struct cm *m = x; m->delays += us; }
static void test_clk(void)
{
	struct cm m; struct n2clk c; struct n2clk_snapshot snap;
	const struct n2clk_ops ops = { c_rd, c_wr, c_delay, NULL };
	memset(&m, 0, sizeof(m));
	m.cfg5 = m.shadow = 0x01010103; m.con0 = 0x000003e1; m.con1 = 0x031aec4e; m.con3 = 0x80000001; m.cg = 1;       /* the boot chain's state (r5b log) */
	assert(n2clk_init(&c, &ops, &m));
	assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_STATE && m.nw == 0);            /* not powered by us yet: nothing written */
	assert(n2clk_preflight(&c, &snap) == N2CLK_E_PLL_RANGE && snap.khz == 349900);
	c.target_khz = 654000; c.target_posdiv = 2;
	assert(n2clk_mux_on(&c, true) == N2CLK_OK && (m.con1 & 0x7fffffffu) == 0x02192762 && cm_parent(&m) == 1);       /* the cold start of every boot: con1=82192762 */
	assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_STATE);                         /* BG3D gate not open yet */
	assert(n2clk_mfg_cg_on(&c) == N2CLK_OK);
	printf("CLK: cold start 654 MHz reproduced (CON1 %08x)\n", m.con1);

	/* 654 -> 836: exact store order */
	m.nw = 0; m.delays = 0;
	assert(n2clk_retune(&c, 836000, 2) == N2CLK_OK);
	assert(m.nw == 7);
	assert(m.w[0].b == N2CLK_TOPCK && m.w[0].off == N2CLK_CFG_5_CLR && m.w[0].v == 0x300 && m.w[1].off == N2CLK_CFG_5_SET && m.w[1].v == 0x300 && m.w[2].off == N2CLK_CFG_UPDATE);       /* park: mainpll_d5_d2 */
	assert(m.w[3].b == N2CLK_APMIXED && m.w[3].off == N2CLK_MFGPLL_CON1 && m.w[3].parent == 3 && (m.w[3].v & 0x80000000u) && n2rail_mfgpll_khz(m.w[3].v) == 836000 && ((m.w[3].v >> 24) & 7) == 2);
	assert(m.w[4].off == N2CLK_CFG_5_CLR && m.w[5].off == N2CLK_CFG_5_SET && m.w[5].v == 0x100 && m.w[6].off == N2CLK_CFG_UPDATE && m.delays == 20);                                        /* 20 us, back to mfgpll */
	assert(cm_parent(&m) == 1 && n2rail_mfgpll_khz(m.con1) == 836000 && c.after.khz == 836000 && c.retune_step == 0 && !(m.cfg5 & 0x8000));
	assert((m.cfg5 & ~0x300u) == (0x01010103u & ~0x300u) && m.con0 == 0x3e1 && m.con3 == 0x80000001);              /* nothing else in CLK_CFG_5 or the PLL changed */
	/* every OPP both ways, 350 with the /8 post divider */
	static const uint32_t seq[] = { 350000, 886000, 512000, 805000, 654000, 350000, 654000 };
	for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
		unsigned oi; assert(n2rail_opp_index_exact(seq[i], &oi));
		m.nw = 0; assert(n2clk_retune(&c, seq[i], n2rail_opp_table[oi].posdiv_power) == N2CLK_OK && n2rail_mfgpll_khz(m.con1) == seq[i] && cm_parent(&m) == 1 && m.nw == 7);
		assert(((m.con1 >> 24) & 7) == (seq[i] <= 370000 ? 3u : 2u));
	}
	m.nw = 0; assert(n2clk_retune(&c, 654000, 2) == N2CLK_OK && m.nw == 0);       /* already there: nothing written */
	assert((m.con1 & 0x7fffffffu) == 0x02192762);                                 /* and bit-identical to the cold-start value */
	/* refusals: nothing written */
	assert(n2clk_retune(&c, 500000, 2) == N2CLK_E_PLL_RANGE && m.nw == 0);        /* not a vendor entry */
	assert(n2clk_retune(&c, 900000, 2) == N2CLK_E_PLL_RANGE && n2clk_retune(&c, 340000, 3) == N2CLK_E_PLL_RANGE && n2clk_retune(&c, 654000, 3) == N2CLK_E_PLL_RANGE &&
	    n2clk_retune(&c, 350000, 2) == N2CLK_E_PLL_RANGE && m.nw == 0);            /* the post divider must be the table's */
	{ uint32_t keep = m.con1; m.con1 ^= 0x10; assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_STATE && m.nw == 0); m.con1 = keep; }       /* somebody else moved the PLL */
	{ uint32_t keep = m.cfg5; m.cfg5 = m.shadow = (keep & ~0x300u) | 0x300u; assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_STATE && m.nw == 0); m.cfg5 = m.shadow = keep; }   /* mux not on mfgpll */
	{ m.cfg5 |= 0x8000; m.shadow |= 0x8000; assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_STATE && m.nw == 0); m.cfg5 &= ~0x8000u; m.shadow &= ~0x8000u; }                     /* gate closed */
	{ m.con0 &= ~1u; assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_STATE && m.nw == 0); m.con0 |= 1; }
	/* the PLL does not take the value: the old rate is written back, the mux returns to mfgpll at the old clock */
	m.pll_stuck = true; m.nw = 0;
	assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_VERIFY && cm_parent(&m) == 1 && n2rail_mfgpll_khz(m.con1) == 654000 && c.retune_step == 0);
	assert(m.nw == 8 && m.w[3].off == N2CLK_MFGPLL_CON1 && m.w[4].off == N2CLK_MFGPLL_CON1 && n2rail_mfgpll_khz(m.w[4].v) == 654000);
	m.pll_stuck = false; assert(n2clk_retune(&c, 836000, 2) == N2CLK_OK && n2clk_retune(&c, 654000, 2) == N2CLK_OK);
	/* the mux does not park: the PLL is never touched */
	m.park_stuck = true; m.nw = 0;
	assert(n2clk_retune(&c, 836000, 2) == N2CLK_E_VERIFY && cm_parent(&m) == 1 && n2rail_mfgpll_khz(m.con1) == 654000);
	for (unsigned i = 0; i < m.nw; i++) assert(m.w[i].b == N2CLK_TOPCK);
	m.park_stuck = false;
	/* power-off afterwards restores the boot chain's PLL word */
	n2clk_mfg_cg_off(&c);
	assert(n2clk_mux_off(&c) == N2CLK_OK && (m.con1 & 0x7fffffffu) == 0x031aec4e);
	printf("CLK: retune order, all OPPs, refusals without a store, PLL / mux failure recovery: ok\n");
}

/* ------------------------------------------------------------------ DRAM rate decode */
static uint32_t dr_nao, dr_ao[0x2000 / 4];
static uint32_t dr_rd(void *c, unsigned b, uint32_t off) { (void)c; if (b == 0) { assert(off == 0x50c); return dr_nao; } assert(off < 0x2000 && (off & 3) == 0); return dr_ao[off / 4]; }
static void test_dram(void)
{
	struct n2dr r;
	memset(dr_ao, 0xff, sizeof(dr_ao));                                           /* garbage everywhere else */
	dr_nao = 0xfffcfeff;                                                          /* shu_lv 0, pll 0 */
	dr_ao[0x704 / 4] = 0x5200ffff; dr_ao[0x708 / 4] = 0xfff3fff8; dr_ao[0x874 / 4] = ~4u; dr_ao[0x744 / 4] = ~0x100u; dr_ao[0x8b4 / 4] = ~2u; dr_ao[0x70c / 4] = ~0x40u; dr_ao[0x870 / 4] = ~0x00200000u;
	n2dr_read(dr_rd, NULL, &r); assert(r.shu_lv == 0 && r.pll_id == 0 && r.raw == 4264 && r.mbps == 4266);
	dr_nao = 0x00020100;                                                          /* shuffle level 2, the second PLL: registers at + 0x700 * 2 and + 0x20 */
	dr_ao[(0x724 + 0xe00) / 4] = 0x5b000000; dr_ao[(0x728 + 0xe00) / 4] = 1; dr_ao[(0x874 + 0xe00) / 4] = 0; dr_ao[(0x744 + 0xe00) / 4] = 0; dr_ao[(0x8b4 + 0xe00) / 4] = 0; dr_ao[(0x70c + 0xe00) / 4] = 0; dr_ao[(0x870 + 0xe00) / 4] = 0;
	n2dr_read(dr_rd, NULL, &r); assert(r.shu_lv == 2 && r.pll_id == 1 && r.posdiv == 1 && r.raw == 2366 && r.mbps == 2400);
	dr_ao[(0x728 + 0xe00) / 4] = 2; dr_ao[(0x724 + 0xe00) / 4] = 0x5c000000; n2dr_read(dr_rd, NULL, &r); assert(r.raw == 1196 && r.mbps == 1200);
	assert(n2dr_decode(3718) == 3733 && n2dr_decode(3068) == 3200 && n2dr_decode(1859) == 1866 && n2dr_decode(1534) == 1600 && n2dr_decode(819) == 800 && n2dr_decode(777) == 777);
	printf("DRAM: fmeter_v1 decode ok\n");
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	test_units();
	test_cpu();
	test_gpu();
	test_clk();
	test_dram();
	printf("ALL OK\n");
	return 0;
}
