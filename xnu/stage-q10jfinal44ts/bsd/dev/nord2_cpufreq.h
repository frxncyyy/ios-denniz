#ifndef NORD2_CPUFREQ_H
#define NORD2_CPUFREQ_H
/*
 * Nord2 CPU DVFS for the big clusters through the MediaTek "cpufreq-hw" request block in CSRAM, pure and host-testable
 * (no kernel headers: the register file, the CPU clock measurement, the battery gauge and sleeping come in through struct n2cf_ops).
 *
 * WHY.  Device data (hardware-20261025/c6m-test, P8 kernel A): `NORD2 CPUCLK 0:1791 1:1793 2:1793 3:1791 4:729 5:730 6:730 7:729 MHz`.
 * The A55 cluster runs near its 2.0 GHz rating, the A78 clusters (cpu4-6 rated 2.6 GHz, cpu7 3.0 GHz, DT clock-frequency) run at ~730 MHz.
 * Nothing under XNU requests a frequency, so every cluster keeps whatever the bootloader left; the same symptom was found on the reference
 * device (A76 at 1.2 of 2.2 GHz) and cured by writing the engine's PERF_STATE.
 *
 * WHAT IS NOT KNOWN.  The 0099 `n2csram=1` words (OFFS_* of the legacy cpufreq_v1 driver) decode to nonsense, so the MT6893 CSRAM layout is
 * NOT confirmed to be the cpufreq-hw one (live.dts has the legacy `mediatek,mt6885-dvfsp` node), and the DVFS firmware may not be running at
 * all under this boot chain.  Therefore nothing here trusts a fixed offset:
 *   1. DISCOVER: scan the whole CSRAM for blocks that look like a cpufreq-hw domain (LUT of >= 8 strictly descending MHz rows in [100,4000] in
 *      bits [11:0], ENABLE +0x84 == 1, PERF_STATE +0x88 < rows, HW_STATE +0x8c bit 0 = engine running).
 *   2. VERIFY by measurement: the top row must match one cluster's rated clock, and the measured clock of that cluster's first CPU must be
 *      within 8 % of LUT[PERF_STATE].  The chance that unrelated SRAM contents pass all of that is negligible (fuzzed in the host test).
 *   3. Only a VERIFIED domain is ever written, one LUT row at a time, each row confirmed by measuring the CPU (within 8 % of the row, two-sided, so a firmware that ignores the write is caught after one or two rows); any failure
 *      writes the original PERF_STATE back and freezes the domain.  The firmware owns voltage and PLL (the AP never touches a rail or a PLL).
 * On a layout that does not verify the module only logs (CSRAM dump + verdicts) and writes nothing.
 *
 * THERMAL.  No SoC temperature exists (LVTS not running, firmware thermal SRAM probably stale).  The battery gauge is the only input:
 * no ramp starts at >= 40.0 C; while ramped, >= 43.0 C steps one row slower per tick (never below the original row), back up below 38.0 C;
 * an unreadable gauge holds the current row.  The default cap is 2000 MHz (n2cpu=<MHz>, clamped to the cluster rating).
 */
#include <stdbool.h>
#include <stdint.h>

#define N2CF_CSRAM_PA       0x0011bc00ull
#define N2CF_CSRAM_SIZE     0x1400u
#define N2CF_DOMAIN_SIZE    0x118u
#define N2CF_LUT_ROWS       32u
#define N2CF_MIN_ROWS       8u
#define N2CF_R_ENABLE       0x84u
#define N2CF_R_PERF         0x88u
#define N2CF_R_HWSTATE      0x8cu
#define N2CF_R_LATENCY      0x114u
#define N2CF_HW_RUNNING     1u
#define N2CF_ORACLE_PCT     8u       /* measured clock vs LUT[PERF_STATE] */
#define N2CF_START_MAX_CC   4500     /* 45.0 C: no ramp at or above (0171) */
#define N2CF_THROTTLE_CC    4600     /* 46.0 C: one row slower per tick (0171) */
#define N2CF_RECOVER_CC     4400     /* 44.0 C: one row faster per tick again (0171) */
#define N2CF_DEFAULT_CAP_MHZ 2000u
#define N2CF_CLUSTERS       3u

struct n2cf_cluster_def { const char *name; int first_cpu, ncpu; unsigned rated_mhz, top_min_mhz, top_max_mhz; bool writable; };
/* MT6893 (live.dts cpu-map and clock-frequency): cluster0 cpu0-3 A55 2.0 GHz, cluster1 cpu4-6 A78 2.6 GHz, cluster2 cpu7 A78 3.0 GHz */
static const struct n2cf_cluster_def n2cf_clusters[N2CF_CLUSTERS] = {
	{ "ll", 0, 4, 2000, 1800, 2200, false },
	{ "l",  4, 3, 2600, 2300, 2800, true },
	{ "b",  7, 1, 3000, 2800, 3200, true },
};

struct n2cf_lut { unsigned n; unsigned mhz[N2CF_LUT_ROWS]; };

enum n2cf_verdict {
	N2CF_OK = 0, N2CF_NOT_LUT, N2CF_ENGINE_OFF, N2CF_BAD_STATE, N2CF_WRONG_CLUSTER, N2CF_ORACLE_MISMATCH, N2CF_NO_MEASURE,
};

struct n2cf_domain {
	bool verified;
	uint32_t base;                  /* offset of the block in CSRAM */
	unsigned perf0;                 /* PERF_STATE found (the bootloader's request) */
	unsigned cur;                   /* row last written and confirmed */
	bool frozen;
	unsigned measured0;
	struct n2cf_lut lut;
};

enum n2cf_log {
	N2CF_L_DUMP,        /* a = offset, b..d = the next words */
	N2CF_L_CANDIDATE,   /* a = offset, b = rows, c = top MHz */
	N2CF_L_VERDICT,     /* a = cluster (9 = none), b = verdict, c = offset */
	N2CF_L_VERIFIED,    /* a = cluster, b = offset, c = PERF_STATE, d = measured MHz */
	N2CF_L_STEP,        /* a = cluster, b = row, c = row MHz, d = measured MHz */
	N2CF_L_REVERT,      /* a = cluster, b = row restored, c = row MHz wanted, d = measured */
	N2CF_L_DONE,        /* a = cluster, b = start row, c = final row, d = final MHz measured */
	N2CF_L_REFUSED,     /* a = reason: 1 gauge hot, 2 no verified domain, 3 target not faster; b = cluster */
	N2CF_L_GUARD,       /* a = cluster, b = new row, c = gauge cC, d = 0 slower / 1 faster */
	N2CF_L_DOMAIN,      /* a = base, b = ENABLE, c = PERF_STATE, d = HW_STATE of a canonical cpufreq-hw domain (0106) */
	N2CF_L_SRAM,        /* a = state (enum n2cf_sram), b = ones permille, c = zero+ones words, d = equal-neighbour pairs */
	N2CF_L_PLACE,       /* a = placement mode written, b = big MHz, c = little MHz */
};

struct n2cf_ops {
	uint32_t (*rd)(void *ctx, uint32_t off);
	void (*wr)(void *ctx, uint32_t off, uint32_t v);          /* store + barrier */
	unsigned (*mhz)(void *ctx, int cpu);                      /* measured clock, 0 = cannot measure */
	bool (*gauge)(void *ctx, int *centi_c);                   /* battery temperature, false = unreadable */
	void (*sleep_ms)(void *ctx, unsigned ms);
	void (*log)(void *ctx, int code, unsigned a, unsigned b, unsigned c, unsigned d);
};

/* ---- pure helpers ---- */

/* Parse a LUT out of 32 words (bits [11:0] = MHz).  The table ends where a row repeats; false when it is not a descending frequency table. */
static inline bool
n2cf_lut_parse(const uint32_t *w, struct n2cf_lut *l)
{
	unsigned prev = 0;
	l->n = 0;
	for (unsigned i = 0; i < N2CF_LUT_ROWS; i++) {
		unsigned mhz = w[i] & 0xfffu;
		if (mhz == prev) {
			break;
		}
		if (mhz < 100 || mhz > 4000 || (prev != 0 && mhz > prev)) {
			l->n = 0;
			return false;
		}
		l->mhz[l->n++] = mhz;
		prev = mhz;
	}
	return l->n >= N2CF_MIN_ROWS;
}

static inline int
n2cf_cluster_for_top(unsigned top_mhz)
{
	for (unsigned c = 0; c < N2CF_CLUSTERS; c++) {
		if (top_mhz >= n2cf_clusters[c].top_min_mhz && top_mhz <= n2cf_clusters[c].top_max_mhz) {
			return (int)c;
		}
	}
	return -1;
}

static inline bool
n2cf_within_pct(unsigned measured, unsigned wanted, unsigned pct)
{
	if (measured == 0 || wanted == 0) {
		return false;
	}
	unsigned d = measured > wanted ? measured - wanted : wanted - measured;
	return (uint64_t)d * 100u <= (uint64_t)wanted * pct;
}

/* first row (0 = fastest) whose clock is <= cap; the last row when everything is faster than the cap */
static inline unsigned
n2cf_target_row(const struct n2cf_lut *l, unsigned cap_mhz)
{
	for (unsigned i = 0; i < l->n; i++) {
		if (l->mhz[i] <= cap_mhz) {
			return i;
		}
	}
	return l->n - 1;
}

static inline unsigned
n2cf_cap_for(unsigned requested_mhz, unsigned cluster)
{
	unsigned rated = n2cf_clusters[cluster].rated_mhz;
	if (requested_mhz < 500) {
		requested_mhz = N2CF_DEFAULT_CAP_MHZ;
	}
	return requested_mhz > rated ? rated : requested_mhz;
}

/* Thermal decision for a ramped cluster: -1 one row slower, +1 one row faster, 0 hold.  floor_row = the bootloader's row (slowest allowed),
 * target_row = the requested row (fastest allowed).  Rows count up while the clock goes down. */
static inline int
n2cf_guard_decide(bool valid, int cC, unsigned cur, unsigned floor_row, unsigned target_row)
{
	if (!valid) {
		return 0;
	}
	if (cC >= N2CF_THROTTLE_CC && cur < floor_row) {
		return -1;
	}
	if (cC < N2CF_RECOVER_CC && cur > target_row) {
		return +1;
	}
	return 0;
}

/* ---- discovery and verification ---- */

/* Check one candidate block against the register file and the clock measurement. */
static inline int
n2cf_check_block(const struct n2cf_ops *o, void *ctx, uint32_t base, struct n2cf_domain *d, int *cluster)
{
	uint32_t w[N2CF_LUT_ROWS];
	for (unsigned i = 0; i < N2CF_LUT_ROWS; i++) {
		w[i] = o->rd(ctx, base + 4 * i);
	}
	if (!n2cf_lut_parse(w, &d->lut)) {
		return N2CF_NOT_LUT;
	}
	int c = n2cf_cluster_for_top(d->lut.mhz[0]);
	if (c < 0) {
		return N2CF_WRONG_CLUSTER;
	}
	*cluster = c;
	uint32_t en = o->rd(ctx, base + N2CF_R_ENABLE), perf = o->rd(ctx, base + N2CF_R_PERF), hw = o->rd(ctx, base + N2CF_R_HWSTATE);
	if (en != 1 || !(hw & N2CF_HW_RUNNING)) {
		return N2CF_ENGINE_OFF;
	}
	if (perf >= d->lut.n || (hw & ~0xfu) != 0) {
		return N2CF_BAD_STATE;
	}
	unsigned m = o->mhz(ctx, n2cf_clusters[c].first_cpu);
	if (m == 0) {
		return N2CF_NO_MEASURE;
	}
	if (!n2cf_within_pct(m, d->lut.mhz[perf], N2CF_ORACLE_PCT)) {
		return N2CF_ORACLE_MISMATCH;
	}
	d->base = base;
	d->perf0 = perf;
	d->cur = perf;
	d->measured0 = m;
	d->frozen = false;
	d->verified = true;
	return N2CF_OK;
}

/* Scan the whole CSRAM at word granularity.  Returns the mask of clusters with a verified domain. */
static inline unsigned
n2cf_discover(const struct n2cf_ops *o, void *ctx, struct n2cf_domain dom[N2CF_CLUSTERS], bool dump)
{
	unsigned mask = 0;
	if (dump) {
		for (uint32_t off = 0; off < N2CF_CSRAM_SIZE; off += 12) {       /* three words per line */
			uint32_t w3[3] = { 0, 0, 0 };
			for (unsigned i = 0; i < 3 && off + 4 * i < N2CF_CSRAM_SIZE; i++) {
				w3[i] = o->rd(ctx, off + 4 * i);
			}
			o->log(ctx, N2CF_L_DUMP, off, w3[0], w3[1], w3[2]);
		}
	}
	for (uint32_t base = 0; base + N2CF_DOMAIN_SIZE <= N2CF_CSRAM_SIZE; base += 4) {
		uint32_t w[N2CF_LUT_ROWS];
		struct n2cf_lut l;
		for (unsigned i = 0; i < N2CF_LUT_ROWS; i++) {
			w[i] = o->rd(ctx, base + 4 * i);
		}
		if (!n2cf_lut_parse(w, &l)) {
			continue;
		}
		o->log(ctx, N2CF_L_CANDIDATE, base, l.n, l.mhz[0], 0);
		struct n2cf_domain d = { 0 };
		int cluster = -1;
		int v = n2cf_check_block(o, ctx, base, &d, &cluster);
		o->log(ctx, N2CF_L_VERDICT, cluster < 0 ? 9u : (unsigned)cluster, (unsigned)v, base, 0);
		if (v == N2CF_OK && !dom[cluster].verified) {
			dom[cluster] = d;
			mask |= 1u << cluster;
			o->log(ctx, N2CF_L_VERIFIED, (unsigned)cluster, base, d.perf0, d.measured0);
		}
		base += 4 * (l.n - 1);      /* a table is not re-examined from its own rows */
	}
	return mask;
}


/* ---- 0106: what the device data showed, and the fixed bases of the family ---- */

/* cpufreq-hw domain blocks of this SoC family (public mediatek-cpufreq-hw bindings: 0x11bc10, +0x120 per domain; three domains on a 1+3+4 part)
 * = CSRAM offsets 0x10, 0x130, 0x250.  Discovery scans every offset anyway; these are logged first, raw, so one boot shows what sits there. */
static const uint32_t n2cf_canonical_bases[N2CF_CLUSTERS] = { 0x10, 0x130, 0x250 };

static inline void
n2cf_probe_canonical(const struct n2cf_ops *o, void *ctx)
{
	for (unsigned i = 0; i < N2CF_CLUSTERS; i++) {
		uint32_t b = n2cf_canonical_bases[i];
		o->log(ctx, N2CF_L_DOMAIN, b, o->rd(ctx, b + N2CF_R_ENABLE), o->rd(ctx, b + N2CF_R_PERF), o->rd(ctx, b + N2CF_R_HWSTATE));
	}
}

/* Is the CSRAM written by anything?  The P9 device dump (3 boots, 10 words compared) showed the signature of SRAM power-up state: 49.7 % ones,
 * 1280 distinct words, no zero or all-ones words, and the same words on every boot except ~3 of 32 bits (a firmware or driver that initialised
 * it would give a table, zeros or counters).  Then no DVFS engine runs and no domain can ever verify. */
enum n2cf_sram { N2CF_SRAM_EMPTY = 0, N2CF_SRAM_NOISE, N2CF_SRAM_STRUCTURED };

static inline int
n2cf_csram_diagnose(const struct n2cf_ops *o, void *ctx, bool log)
{
	unsigned words = N2CF_CSRAM_SIZE / 4, zero = 0, ones = 0, eq = 0;
	unsigned long bits = 0;
	uint32_t prev = 0;
	for (unsigned i = 0; i < words; i++) {
		uint32_t v = o->rd(ctx, 4 * i);
		zero += v == 0 || v == 0xffffffffu;
		for (uint32_t x = v; x; x &= x - 1) {
			bits++;
		}
		eq += i > 0 && v == prev;
		prev = v;
	}
	ones = (unsigned)(bits * 1000ul / (32ul * words));
	int st = N2CF_SRAM_STRUCTURED;
	if (zero * 100 >= words * 97) {
		st = N2CF_SRAM_EMPTY;
	} else if (ones >= 460 && ones <= 540 && zero * 100 <= words && eq * 100 <= words) {
		st = N2CF_SRAM_NOISE;
	}
	if (log) {
		o->log(ctx, N2CF_L_SRAM, (unsigned)st, ones, zero, eq);
	}
	return st;
}

/* ---- 0106: utilisation governor with a UI boost floor (decision logic of the reference's patch 0420) ----
 * Per cluster, from the busiest CPU of the cluster: target 75 % utilisation; up at once, down only after the load stayed low for the hold time;
 * while the UI is active (a render/display thread became runnable within the boost window) the clock never drops below the floor row (A78 2.0 GHz).
 * INPUTS THAT DO NOT EXIST YET: per-CPU busy time and the UI kick need the scheduler hooks (0099 level b exports neither); util < 0 means
 * unknown, and the governor then only enforces the floor/cap rows.  Not exercised on the device: no domain has ever verified. */
#define N2CF_GOV_TARGET_PERMILLE 750
#define N2CF_GOV_DOWN_HOLD_MS    100
#define N2CF_GOV_BOOST_MS        100
#define N2CF_GOV_FLOOR_MHZ       2000u

struct n2cf_gov {
	uint64_t below_since_ms;        /* 0 = load is not low */
	uint64_t boost_until_ms;
};

static inline void
n2cf_gov_kick(struct n2cf_gov *g, uint64_t now_ms)
{
	g->boost_until_ms = now_ms + N2CF_GOV_BOOST_MS;
}

/* Row to run next.  cur = current row, fastest_row = row of the cap, slowest_row = the bootloader's row (never slower), util_permille of the
 * busiest CPU at the current clock (<0 unknown). */
static inline unsigned
n2cf_gov_row(struct n2cf_gov *g, const struct n2cf_lut *l, unsigned cur, unsigned fastest_row, unsigned slowest_row, int util_permille,
    uint64_t now_ms)
{
	unsigned floor_row = n2cf_target_row(l, N2CF_GOV_FLOOR_MHZ);
	unsigned want = cur;
	if (floor_row < fastest_row) {
		floor_row = fastest_row;
	}
	if (util_permille >= 0) {
		/* frequency the load needs so that it would sit at the target: mhz * util / target, rounded up to a row */
		uint64_t need = ((uint64_t)l->mhz[cur] * (unsigned)util_permille + N2CF_GOV_TARGET_PERMILLE - 1) / N2CF_GOV_TARGET_PERMILLE;
		unsigned row = fastest_row;             /* the slowest row (largest index) that still offers `need`; nothing does: the fastest */
		for (unsigned i = slowest_row + 1; i-- > fastest_row;) {
			if (l->mhz[i] >= need) {
				row = i;
				break;
			}
		}
		if (row < cur) {                /* faster: at once */
			want = row;
			g->below_since_ms = 0;
		} else if (row > cur) {         /* slower: only after the hold */
			if (g->below_since_ms == 0) {
				g->below_since_ms = now_ms ? now_ms : 1;
			}
			if (now_ms - g->below_since_ms >= N2CF_GOV_DOWN_HOLD_MS) {
				want = cur + 1;         /* one row per tick */
				g->below_since_ms = now_ms;
			}
		} else {
			g->below_since_ms = 0;
		}
	}
	if (now_ms < g->boost_until_ms && want > floor_row) {
		want = floor_row;
	}
	if (want < fastest_row) {
		want = fastest_row;
	}
	if (want > slowest_row) {
		want = slowest_row;
	}
	return want;
}

/* ---- 0106: scheduler placement follows the measured advantage ----
 * Core-aware placement (0099, mode 0xf) puts the render threads on the big cores.  It only helps when they are really faster: a 730 MHz A78 is
 * about an A55 at 1.5 GHz, so with the big cores at their boot clock it would trade nothing for migrations.  Enable above 1.3x the little clock,
 * disable again below 1.15x (hysteresis); never touch a mode the user set by boot-arg. */
static inline int
n2cf_place_decide(unsigned big_mhz, unsigned little_mhz, int cur_mode)
{
	if (big_mhz == 0 || little_mhz == 0) {
		return cur_mode;
	}
	if (cur_mode == 0 && (uint64_t)big_mhz * 100u >= (uint64_t)little_mhz * 130u) {
		return 0xf;
	}
	if (cur_mode != 0 && (uint64_t)big_mhz * 100u < (uint64_t)little_mhz * 115u) {
		return 0;
	}
	return cur_mode;
}

/* ---- ramp ---- */

static inline bool
n2cf_set_row(const struct n2cf_ops *o, void *ctx, unsigned cluster, struct n2cf_domain *d, unsigned row)
{
	unsigned lat = o->rd(ctx, d->base + N2CF_R_LATENCY);
	unsigned settle = lat > 0 && lat < 20000 ? lat / 1000 + 3 : 5;       /* us -> ms, margin */
	o->wr(ctx, d->base + N2CF_R_PERF, row);
	o->sleep_ms(ctx, settle);
	unsigned m = 0;
	for (unsigned t = 0; t < 5; t++) {
		m = o->mhz(ctx, n2cf_clusters[cluster].first_cpu);
		if (n2cf_within_pct(m, d->lut.mhz[row], N2CF_ORACLE_PCT)) {
			d->cur = row;
			o->log(ctx, N2CF_L_STEP, cluster, row, d->lut.mhz[row], m);
			return true;
		}
		o->sleep_ms(ctx, 10);
	}
	o->wr(ctx, d->base + N2CF_R_PERF, d->perf0);
	d->frozen = true;
	d->cur = d->perf0;
	o->log(ctx, N2CF_L_REVERT, cluster, d->perf0, d->lut.mhz[row], m);
	return false;
}

/* Step a verified, writable domain toward the row for cap_mhz, one row at a time, each confirmed by measurement. */
static inline bool
n2cf_ramp(const struct n2cf_ops *o, void *ctx, unsigned cluster, struct n2cf_domain *d, unsigned cap_mhz)
{
	if (!d->verified || d->frozen || !n2cf_clusters[cluster].writable) {
		return false;
	}
	unsigned target = n2cf_target_row(&d->lut, n2cf_cap_for(cap_mhz, cluster));
	if (target >= d->cur) {
		o->log(ctx, N2CF_L_REFUSED, 3, cluster, d->cur, target);
		return false;
	}
	unsigned start = d->cur;
	while (d->cur > target) {
		if (!n2cf_set_row(o, ctx, cluster, d, d->cur - 1)) {
			return false;
		}
	}
	unsigned m = o->mhz(ctx, n2cf_clusters[cluster].first_cpu);
	o->log(ctx, N2CF_L_DONE, cluster, start, d->cur, m);
	return true;
}

/* Move a verified domain to `row` (either direction), one row at a time, each row confirmed by measurement (a miss reverts and freezes). */
static inline bool
n2cf_goto_row(const struct n2cf_ops *o, void *ctx, unsigned cluster, struct n2cf_domain *d, unsigned row)
{
	if (!d->verified || d->frozen || !n2cf_clusters[cluster].writable || row >= d->lut.n) {
		return false;
	}
	while (d->cur != row) {
		if (!n2cf_set_row(o, ctx, cluster, d, d->cur > row ? d->cur - 1 : d->cur + 1)) {
			return false;
		}
	}
	return true;
}

/* One guard tick for a ramped cluster (the bootloader's row is d->perf0). */
static inline void
n2cf_guard_tick(const struct n2cf_ops *o, void *ctx, unsigned cluster, struct n2cf_domain *d, unsigned target_row)
{
	int cC = 0;
	bool valid = o->gauge(ctx, &cC);
	if (!d->verified || d->frozen) {
		return;
	}
	int dir = n2cf_guard_decide(valid, cC, d->cur, d->perf0, target_row);
	if (dir == 0) {
		return;
	}
	unsigned row = (unsigned)((int)d->cur + (dir < 0 ? 1 : -1));       /* slower = higher row index */
	o->wr(ctx, d->base + N2CF_R_PERF, row);
	d->cur = row;
	o->log(ctx, N2CF_L_GUARD, cluster, row, (unsigned)cC, dir < 0 ? 0u : 1u);
}

/* Ramp every verified writable cluster to its cap, unless the battery gauge says the phone is already warm.  Returns the mask of ramped clusters. */
static inline unsigned
n2cf_start(const struct n2cf_ops *o, void *ctx, struct n2cf_domain dom[N2CF_CLUSTERS], unsigned cap_mhz)
{
	unsigned done = 0;
	int cC = 0;
	if (o->gauge(ctx, &cC) && cC >= N2CF_START_MAX_CC) {
		o->log(ctx, N2CF_L_REFUSED, 1, (unsigned)cC, 0, 0);
		return 0;
	}
	for (unsigned c = 0; c < N2CF_CLUSTERS; c++) {
		if (!dom[c].verified) {
			if (n2cf_clusters[c].writable) {
				o->log(ctx, N2CF_L_REFUSED, 2, c, 0, 0);
			}
			continue;
		}
		if (n2cf_ramp(o, ctx, c, &dom[c], cap_mhz)) {
			done |= 1u << c;
		}
	}
	return done;
}

#endif
