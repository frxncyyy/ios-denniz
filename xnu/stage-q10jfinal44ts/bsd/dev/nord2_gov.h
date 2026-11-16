#ifndef NORD2_GOV_H
#define NORD2_GOV_H
/*
 * 0460: frequency policy of the CPU clusters L (cpu4-6) / B (cpu7) and of the GPU.  Pure (no hardware, no kernel headers), C and C++, host-tested
 * (dvfs-20261113/test).  The callers sample, call these and apply the answer: nord2_dvfs_sched.h for the CPU rows, Nord2GPU.cpp for the GPU OPP.
 *
 *   thermal   the battery gauge is the only temperature: levels 0..4 at 41 / 43 / 45 / 47 C, back down 1 C below a threshold; never read = level 4.
 *   CPU       schedutil shape: wanted MHz = 1.25 * (mean clock of the window) * (busy share of the busiest CPU over 100 ms) [1.67 while frames are
 *             produced]; 60 ms saturated -> at least the hispeed row; 100 ms saturated -> the cap.  Faster at once, slower after 40 ms.  Floors by
 *             activity class: idle, UI (a swap within 200 ms), animation (>= 4 swaps in 100 ms), touch (400 ms after a finger event), jank (600 ms
 *             after a swap that arrived more than 1.6 frame periods after its predecessor: both clusters at the cap).  Nothing above the thermal cap.
 *   GPU       simple_ondemand shape on the exact vendor OPPs the rails cover: busy >= 88 % over 40 ms -> highest allowed OPP; otherwise the lowest
 *             OPP that keeps the projected busy share under 75 % (100 ms window, 100 ms after the last switch); 654 MHz floor while the UI animates.
 *             0492 (kern.n2gdvfs +8, both 0 = exactly 0461): min_idx = an OPP the policy never goes below, whatever the load (the thermal cap still
 *             wins); the caller sets it to the cold-start OPP, so the clock only ever moves between the rate every image ran at so far and the cap.
 *             up_boost_pm = the busy share that sends the clock to the cap while the UI animates or is touched (800: Control Center over the Photos
 *             grid ran at 805..861 permille over 2 s with every third frame late, under the 880 of the general rule).
 */
#include <stdbool.h>
#include <stdint.h>

#define N2G_HIST 8u

/* ---- thermal ladder ---- */
#define N2G_TH_LEVELS  5u
#define N2G_TH_HYST_CC 100
struct n2g_therm { uint8_t level; bool seen; int cc; };

static inline void
n2g_therm_init(struct n2g_therm *t)
{
	t->level = 4; t->seen = false; t->cc = 0;
}

static inline unsigned
n2g_therm_update(struct n2g_therm *t, bool valid, int cc)
{
	static const int up[4] = { 4100, 4300, 4500, 4700 };
	if (!valid) {
		return t->level;                                  /* unreadable: keep the level (never read: minimum clocks) */
	}
	unsigned lv = t->seen ? t->level : 0u;
	while (lv < 4u && cc >= up[lv]) {
		lv++;
	}
	while (lv > 0u && cc < up[lv - 1u] - N2G_TH_HYST_CC) {
		lv--;
	}
	t->level = (uint8_t)lv; t->seen = true; t->cc = cc;
	return lv;
}

/*
 * 0643 (cpu-20261114): the ladder level from the battery gauge AND the SoC sensors (LVTS, hottest of the CPU-big/CPU-little/GPU controllers).
 *   SoC reading usable: battery steps 43.0 / 44.5 / 45.5 / 46.0 C (were 41 / 43 / 45 / 47) and SoC steps 85 / 90 / 95 / 100 C junction (vendor MT6893
 *   CPU/GPU throttle trips are ~95-100 C; +10 C is added to the SoC reading while its calibration is the vendor default instead of the efuse).
 *   SoC reading not usable: the old battery steps, except that level 4 (slowest rows) now starts at 46.0 C (was 47.0): the battery never runs above 46 C
 *   with fast clocks.  A SoC reading counts only between battery - 15 C and 125 C (anything else is a decode problem, not a temperature).
 * The level only goes down when every source allows it (hysteresis: battery 1 C, SoC 3 C).  kill: n2g_therm_soc_off (Nord2GPU / nord2_cpufreq: n2gdv +16).
 */
#define N2G_TH_SOC_HYST_CC 300
static bool n2g_therm_soc_off;

static inline unsigned
n2g_therm_from(const int up[4], int hyst, int x, unsigned prev)
{
	unsigned lv = prev;
	while (lv < 4u && x >= up[lv]) {
		lv++;
	}
	while (lv > 0u && x < up[lv - 1u] - hyst) {
		lv--;
	}
	return lv;
}

/* the SoC reading (centi-C) the ladder uses, or false when it is not usable next to this battery reading */
static inline bool
n2g_therm_soc_usable(bool soc_ok, int soc_cc, bool soc_approx, bool bat_ok, int bat_cc, int *eff)
{
	if (!soc_ok || n2g_therm_soc_off || soc_cc > 12500 || (bat_ok && soc_cc < bat_cc - 1500)) {
		return false;
	}
	*eff = soc_cc + (soc_approx ? 1000 : 0);
	return true;
}

static inline unsigned
n2g_therm_update2(struct n2g_therm *t, bool bat_ok, int bat_cc, bool soc_ok, int soc_cc, bool soc_approx)
{
	static const int bat_soc[4] = { 4300, 4450, 4550, 4600 }, bat_only[4] = { 4100, 4300, 4500, 4600 }, soc_up[4] = { 8500, 9000, 9500, 10000 };
	int eff = 0;
	bool soc = n2g_therm_soc_usable(soc_ok, soc_cc, soc_approx, bat_ok, bat_cc, &eff);
	if (!bat_ok && !soc) {
		return t->level;                                  /* nothing readable: keep the level (never read: minimum clocks) */
	}
	unsigned prev = t->seen ? t->level : 0u, lv = 0;
	if (bat_ok) {
		lv = n2g_therm_from(soc ? bat_soc : bat_only, N2G_TH_HYST_CC, bat_cc, prev);
	}
	if (soc) {
		unsigned ls = n2g_therm_from(soc_up, N2G_TH_SOC_HYST_CC, eff, prev);
		lv = ls > lv ? ls : lv;
	}
	t->level = (uint8_t)lv; t->seen = true;
	if (bat_ok) {
		t->cc = bat_cc;
	}
	return lv;
}

/* ---- activity hints: swaps (CoreAnimation) and touches ---- */
#define N2G_SWAP_GAP_US   100000u      /* a longer pause ends the burst: not a late frame */
#define N2G_SWAP_MIN_US   8000u
struct n2g_swap { uint64_t last_us, late_us; uint32_t per_us, count, late; };

/* one swap arrived.  The frame period is learned from the burst itself (shortest spacing, tracking upwards), so 60 fps video on a 90 Hz panel is not jank. */
static inline void
n2g_swap_event(struct n2g_swap *s, uint64_t now_us)
{
	uint64_t dt = s->last_us != 0 && now_us > s->last_us ? now_us - s->last_us : (uint64_t)N2G_SWAP_GAP_US;
	s->last_us = now_us;
	s->count++;
	if (dt >= N2G_SWAP_GAP_US) {
		s->per_us = 0;
		return;
	}
	uint32_t d = (uint32_t)dt;
	if (s->per_us == 0 || d < s->per_us) {
		s->per_us = d < N2G_SWAP_MIN_US ? N2G_SWAP_MIN_US : d;
		return;
	}
	if ((uint64_t)d * 10u > (uint64_t)s->per_us * 16u) {
		s->late++;
		s->late_us = now_us;
	}
	s->per_us += (d - s->per_us) / 4u;
}

struct n2g_hints { uint64_t touch_ms, swap_ms, late_ms; uint32_t swaps, lates; };

enum { N2G_IDLE = 0, N2G_UI, N2G_ANIM, N2G_TOUCH, N2G_JANK, N2G_NCLASS };
#define N2G_UI_MS     200u
#define N2G_ANIM_N    4u
#define N2G_ANIM_MS   300u
#define N2G_TOUCH_MS  400u
#define N2G_JANK_MS   600u
struct n2g_act { uint32_t swaps[N2G_HIST]; unsigned n; uint64_t anim_ms; uint8_t cls; };

/* call once per governor tick (20 ms): swaps[] holds the counter of the last ticks, 5 ticks = 100 ms */
static inline unsigned
n2g_act_update(struct n2g_act *a, const struct n2g_hints *h, uint64_t now_ms)
{
	uint32_t old = a->n >= 5u ? a->swaps[(a->n - 5u) % N2G_HIST] : (a->n ? a->swaps[0] : h->swaps);
	a->swaps[a->n % N2G_HIST] = h->swaps;
	a->n++;
	if (h->swaps - old >= N2G_ANIM_N) {
		a->anim_ms = now_ms ? now_ms : 1;
	}
	unsigned cls = N2G_IDLE;
	if (h->late_ms != 0 && now_ms >= h->late_ms && now_ms - h->late_ms < N2G_JANK_MS) {
		cls = N2G_JANK;
	} else if (h->touch_ms != 0 && now_ms >= h->touch_ms && now_ms - h->touch_ms < N2G_TOUCH_MS) {
		cls = N2G_TOUCH;
	} else if (a->anim_ms != 0 && now_ms - a->anim_ms < N2G_ANIM_MS) {
		cls = N2G_ANIM;
	} else if (h->swap_ms != 0 && now_ms >= h->swap_ms && now_ms - h->swap_ms < N2G_UI_MS) {
		cls = N2G_UI;
	}
	a->cls = (uint8_t)cls;
	return cls;
}

/* ---- CPU cluster ---- */
#define N2G_CPU_TICK_MS  20u
#define N2G_CPU_WIN      5u            /* samples: 100 ms */
#define N2G_CPU_FAST     3u            /* samples: 60 ms */
#define N2G_CPU_DOWN_MS  40u
#define N2G_CPU_SAT      900
#define N2G_CPU_HEAVY    950
#define N2G_CPU_ROWS     16u
#define N2G_CPU_MAXCPU   4u
enum { N2G_W_HOLD = 0, N2G_W_UTIL, N2G_W_FLOOR, N2G_W_BURST, N2G_W_HEAVY, N2G_W_THERM, N2G_W_RATE, N2G_W_UP, N2G_W_DOWN, N2G_W_BOOST, N2G_NWHY };

struct n2g_cl {
	/* set by the caller */
	uint16_t mhz[N2G_CPU_ROWS];            /* row 0 fastest */
	uint8_t fastest, slowest, ncpu;        /* rows the governor may use: fastest = the cap row, slowest = the idle row */
	uint16_t floor_mhz[N2G_NCLASS];        /* 0 = the idle row; N2G_JANK is the cap whatever is stored */
	uint16_t hispeed_mhz;
	uint16_t therm_mhz[N2G_TH_LEVELS];     /* cap per thermal level; 0 = no extra cap (level 0), level 4 = the idle row */
	/* state */
	uint32_t busy[N2G_CPU_MAXCPU][N2G_HIST], tot[N2G_CPU_MAXCPU][N2G_HIST];
	uint16_t fhist[N2G_HIST];
	unsigned n;
	uint64_t down_since;
	uint8_t down_best, cur, want, why, limit;
	int u_win, u_fast;
	unsigned favg;
};

/* slowest row of [fastest, slowest] that still offers mhz (n2d_row_at_least) */
static inline unsigned
n2g_row_at_least(const struct n2g_cl *cl, unsigned mhz, unsigned fastest, unsigned slowest)
{
	for (unsigned i = slowest + 1u; i-- > fastest;) {
		if (cl->mhz[i] >= mhz) {
			return i;
		}
	}
	return fastest;
}

/* fastest row allowed at this thermal level */
static inline unsigned
n2g_cl_limit(const struct n2g_cl *cl, unsigned level)
{
	unsigned lim = cl->fastest;
	if (level >= N2G_TH_LEVELS - 1u) {
		return cl->slowest;
	}
	if (level != 0 && cl->therm_mhz[level] != 0) {
		while (lim < cl->slowest && cl->mhz[lim] > cl->therm_mhz[level]) {
			lim++;
		}
	}
	return lim;
}

static inline int
n2g_cl_util(const struct n2g_cl *cl, unsigned win)
{
	int best = -1;
	if (cl->n <= win) {
		return -1;
	}
	unsigned i = (cl->n - 1u) % N2G_HIST, j = (cl->n - 1u - win) % N2G_HIST;
	for (unsigned k = 0; k < cl->ncpu && k < N2G_CPU_MAXCPU; k++) {
		uint32_t db = cl->busy[k][i] - cl->busy[k][j], dt = cl->tot[k][i] - cl->tot[k][j];
		if (dt < 3u || db > dt + 2u) {
			continue;                                 /* too few 10 ms ticks, or a counter restarted */
		}
		int u = (int)((uint64_t)db * 1000u / dt);
		if (u > 1000) {
			u = 1000;
		}
		if (u > best) {
			best = u;
		}
	}
	return best;
}

/* one sample per tick: cumulative busy / total ticks of every CPU of the cluster; cl->cur is the row that ran since the previous sample */
static inline void
n2g_cl_sample(struct n2g_cl *cl, const uint32_t *busy, const uint32_t *tot)
{
	unsigned i = cl->n % N2G_HIST;
	for (unsigned k = 0; k < cl->ncpu && k < N2G_CPU_MAXCPU; k++) {
		cl->busy[k][i] = busy[k];
		cl->tot[k][i] = tot[k];
	}
	cl->fhist[i] = cl->mhz[cl->cur];
	cl->n++;
	cl->u_win = n2g_cl_util(cl, N2G_CPU_WIN);
	cl->u_fast = n2g_cl_util(cl, N2G_CPU_FAST);
	unsigned sum = 0, cnt = cl->n < N2G_CPU_WIN ? cl->n : N2G_CPU_WIN;
	for (unsigned k = 0; k < cnt; k++) {
		sum += cl->fhist[(cl->n - 1u - k) % N2G_HIST];
	}
	cl->favg = cnt ? sum / cnt : cl->mhz[cl->cur];
}

/* the row to run now (cl->want, cl->why and cl->limit are set too) */
static inline unsigned
n2g_cl_decide(struct n2g_cl *cl, uint64_t now_ms, unsigned cls, unsigned therm_level)
{
	unsigned lim = n2g_cl_limit(cl, therm_level), slow = cl->slowest, cur = cl->cur;
	unsigned floor_mhz = cls == N2G_JANK ? cl->mhz[cl->fastest] : (cls < N2G_NCLASS ? cl->floor_mhz[cls] : 0u);
	unsigned floor_row = floor_mhz ? n2g_row_at_least(cl, floor_mhz, lim, slow) : slow;
	unsigned row, why;
	cl->limit = (uint8_t)lim;
	if (cl->u_win < 0) {
		row = cur < floor_row ? cur : floor_row;          /* load unknown: never slower, only the floor can pull up */
		why = row < cur ? N2G_W_FLOOR : N2G_W_HOLD;
	} else {
		unsigned tgt = cls >= N2G_ANIM ? 600u : 800u;
		unsigned need = (unsigned)(((uint64_t)cl->favg * (unsigned)cl->u_win + tgt - 1u) / tgt);
		why = N2G_W_UTIL;
		if (cl->u_win >= N2G_CPU_HEAVY) {
			need = cl->mhz[cl->fastest];
			why = N2G_W_HEAVY;
		} else if (cl->u_fast >= N2G_CPU_SAT && need < cl->hispeed_mhz) {
			need = cl->hispeed_mhz;
			why = N2G_W_BURST;
		}
		row = n2g_row_at_least(cl, need, lim, slow);
		if (floor_row < row) {
			row = floor_row;
			why = N2G_W_FLOOR;
		}
	}
	if (row < lim) {
		row = lim;
	}
	unsigned want = cur;
	if (row < cur) {
		want = row;                                       /* faster: at once */
		cl->down_since = 0;
	} else if (row > cur) {                               /* slower: after the hold, and not below the fastest row asked for meanwhile */
		if (cl->down_since == 0) {
			cl->down_since = now_ms ? now_ms : 1;
			cl->down_best = (uint8_t)row;
		} else if (row < cl->down_best) {
			cl->down_best = (uint8_t)row;
		}
		if (now_ms - cl->down_since >= N2G_CPU_DOWN_MS) {
			want = cl->down_best;
			cl->down_since = 0;
		} else {
			why = N2G_W_RATE;
		}
	} else {
		cl->down_since = 0;
	}
	if (want < lim) {
		want = lim;                                       /* above the thermal cap: down at once */
		why = N2G_W_THERM;
	} else if (want == lim && lim > cl->fastest && row == lim && why != N2G_W_RATE) {
		why = N2G_W_THERM;
	}
	if (want > slow) {
		want = slow;
	}
	cl->want = (uint8_t)want;
	cl->why = (uint8_t)why;
	return want;
}

/* ---- GPU ---- */
#define N2G_GPU_POLL_MS   20u
#define N2G_GPU_NOPP      6u
#define N2G_GPU_UP_PM     880u
#define N2G_GPU_DOWN_PM   750u
#define N2G_GPU_RATE_MS   40u
#define N2G_GPU_DOWN_MS   100u
#define N2G_GPU_BOOST_KHZ 654000u
static const uint32_t n2g_gpu_khz[N2G_GPU_NOPP] = { 350000u, 512000u, 654000u, 805000u, 836000u, 886000u };       /* exact vendor OPP entries */
static const uint32_t n2g_gpu_therm_khz[N2G_TH_LEVELS] = { 886000u, 805000u, 654000u, 512000u, 350000u };

struct n2g_gpu {
	uint8_t max_idx;                       /* highest OPP whose table voltage the rails cover */
	uint8_t min_idx;                       /* 0492: lowest OPP the policy may ask for (0 = no limit, as 0461); never lifts the thermal cap */
	uint16_t up_boost_pm;                  /* 0492: "up" threshold while boost is set (0 = N2G_GPU_UP_PM, as 0461) */
	uint8_t cur, want, why, cap, floor;
	uint16_t u[N2G_HIST];
	unsigned n;
	uint64_t last_switch_ms;
	unsigned u_up, u_down;
};

static inline int
n2g_gpu_index(uint32_t khz)
{
	for (unsigned i = 0; i < N2G_GPU_NOPP; i++) {
		if (n2g_gpu_khz[i] == khz) {
			return (int)i;
		}
	}
	return -1;
}

/* highest OPP both rails cover (uV as programmed; the vendor table needs 750/750 mV up to 836 MHz, 800/800 mV for 886 MHz; lower entries need less) */
static inline unsigned
n2g_gpu_max_idx(uint32_t vgpu_uv, uint32_t vsram_uv)
{
	if (vgpu_uv >= 800000u && vsram_uv >= 800000u) {
		return 5;
	}
	if (vgpu_uv >= 750000u && vsram_uv >= 750000u) {
		return 4;
	}
	if (vgpu_uv >= 731250u && vsram_uv >= 750000u) {
		return 3;
	}
	if (vgpu_uv >= 650000u && vsram_uv >= 750000u) {
		return 2;
	}
	if (vgpu_uv >= 625000u && vsram_uv >= 750000u) {
		return 1;
	}
	return 0;
}

static inline void
n2g_gpu_sample(struct n2g_gpu *g, unsigned busy_pm)
{
	g->u[g->n % N2G_HIST] = (uint16_t)(busy_pm > 1000u ? 1000u : busy_pm);
	g->n++;
	unsigned a = 0, b = 0, na = g->n < 2u ? g->n : 2u, nb = g->n < 5u ? g->n : 5u;
	for (unsigned k = 0; k < nb; k++) {
		unsigned v = g->u[(g->n - 1u - k) % N2G_HIST];
		b += v;
		if (k < na) {
			a += v;
		}
	}
	g->u_up = a / na;
	g->u_down = b / nb;
}

static inline unsigned
n2g_gpu_decide(struct n2g_gpu *g, uint64_t now_ms, bool boost, unsigned therm_level)
{
	unsigned cap = g->max_idx, cur = g->cur, floor = 0, want = cur, why = N2G_W_HOLD;
	uint32_t tk = n2g_gpu_therm_khz[therm_level < N2G_TH_LEVELS ? therm_level : N2G_TH_LEVELS - 1u];
	while (cap > 0 && n2g_gpu_khz[cap] > tk) {
		cap--;
	}
	if (boost) {
		while (floor < cap && n2g_gpu_khz[floor] < N2G_GPU_BOOST_KHZ) {
			floor++;
		}
	}
	if (floor < g->min_idx) {                                 /* 0492 */
		floor = g->min_idx < cap ? g->min_idx : cap;
	}
	g->cap = (uint8_t)cap; g->floor = (uint8_t)floor;
	bool rate_ok = now_ms - g->last_switch_ms >= N2G_GPU_RATE_MS;
	unsigned up_pm = boost && g->up_boost_pm != 0 ? g->up_boost_pm : N2G_GPU_UP_PM;      /* 0492 */
	if (cur > cap) {
		want = cap; why = N2G_W_THERM;                    /* at once */
	} else if (cur < floor) {
		if (rate_ok) {
			want = floor; why = N2G_W_BOOST;
		}
	} else if (g->u_up >= up_pm && cur < cap) {
		if (rate_ok) {
			want = cap; why = N2G_W_UP;
		}
	} else if (g->n >= 5u && now_ms - g->last_switch_ms >= N2G_GPU_DOWN_MS) {
		unsigned ud = g->u_up > g->u_down ? g->u_up : g->u_down, i = floor;
		while (i < cur && (uint64_t)n2g_gpu_khz[cur] * ud >= (uint64_t)n2g_gpu_khz[i] * N2G_GPU_DOWN_PM) {
			i++;
		}
		if (i < cur) {
			want = i; why = N2G_W_DOWN;
		}
	}
	g->want = (uint8_t)want; g->why = (uint8_t)why;
	return want;
}

static inline void
n2g_gpu_applied(struct n2g_gpu *g, unsigned idx, uint64_t now_ms)
{
	g->cur = (uint8_t)idx;
	g->last_switch_ms = now_ms;
}

/* 0631: "fast ramp" GPU profile (n2gdv +8192 acts with it; its decisions are always computed and logged as a shadow, GDVFS2 line).
 * Inputs every 20 ms poll: the last sample's busy (not the 40 ms average), the GPU queue depth (unfinished jobs of every context) and the
 * UI boost (animation / touch).  Up: straight to the cap as soon as the GPU is "hot" (busy >= 50 %, or >= 30 % with >= 6 jobs queued, or
 * >= 35 % while the UI animates or is touched; performance mode = always hot).  Down: straight to the floor only after 500 ms without a hot
 * sample and 500 ms after the last switch (few switches: every switch holds job starts until the slots drain).  Cap = the same thermal
 * ladder as 0461 (battery: < 41 C the rails' cap 836 MHz, 41 C 805, 43 C 654, 45 C 512, 47 C 350) and never above the OPP the rails cover;
 * floor = min_idx (the cold-start OPP with the UI profile) unless the thermal cap is lower. */
#define N2G_GF_UP_PM       500u
#define N2G_GF_DEPTH       6u
#define N2G_GF_DEPTH_PM    300u
#define N2G_GF_BOOST_PM    350u
#define N2G_GF_HOLD_MS     500u
struct n2g_gfast { uint64_t hot_ms; bool perf; };

static inline unsigned
n2g_gpu_decide_fast(struct n2g_gpu *g, struct n2g_gfast *f, uint64_t now_ms, bool boost, unsigned therm_level, unsigned depth)
{
	unsigned cap = g->max_idx, cur = g->cur, floor = g->min_idx, want = cur, why = N2G_W_HOLD;
	uint32_t tk = n2g_gpu_therm_khz[therm_level < N2G_TH_LEVELS ? therm_level : N2G_TH_LEVELS - 1u];
	while (cap > 0 && n2g_gpu_khz[cap] > tk) {
		cap--;
	}
	if (floor > cap) {
		floor = cap;
	}
	g->cap = (uint8_t)cap; g->floor = (uint8_t)floor;
	unsigned u = g->n ? g->u[(g->n - 1u) % N2G_HIST] : 0u;
	bool hot = f->perf || u >= N2G_GF_UP_PM || (depth >= N2G_GF_DEPTH && u >= N2G_GF_DEPTH_PM) || (boost && u >= N2G_GF_BOOST_PM);
	if (hot) {
		f->hot_ms = now_ms ? now_ms : 1u;
	}
	bool rate_ok = now_ms - g->last_switch_ms >= N2G_GPU_RATE_MS;
	if (cur > cap) {
		want = cap; why = N2G_W_THERM;                    /* at once */
	} else if (hot && cur < cap) {
		if (rate_ok) {
			want = cap; why = f->perf ? N2G_W_FLOOR : N2G_W_UP;
		}
	} else if (cur < floor) {
		if (rate_ok) {
			want = floor; why = N2G_W_FLOOR;
		}
	} else if (!hot && cur > floor && now_ms - f->hot_ms >= N2G_GF_HOLD_MS && now_ms - g->last_switch_ms >= N2G_GF_HOLD_MS) {
		want = floor; why = N2G_W_DOWN;
	}
	g->want = (uint8_t)want; g->why = (uint8_t)why;
	return want;
}

/* what the GPU side tells the 2 s DECISION line (written by Nord2GPU.cpp, 0461; all zero without it) */
struct n2g_gpu_report { uint32_t want_khz, cur_khz, cap_khz, busy_pm, mode, why, switches, errors; };
#endif
