#ifndef NORD2_DVFS_H
#define NORD2_DVFS_H
/*
 * MT6893 CPU DVFS bring-up engine (patch 0116): platform init of the MCUPM firmware, the DVFS hand-over with every cluster pinned at the row the bootloader
 * left, an optional measured row-by-row ramp of the A78 clusters, the battery guard and the 0099 placement switch.  Pure and host-testable: every register,
 * mailbox, rail, clock and gauge access goes through struct n2d_ops.  Derivation of every number and message: README-0107-design.md sections 11, 12 and
 * nord2_cpuhvfs_init.h.  Rules (all tested):
 *   - PREFLIGHT sends and writes nothing: the clocks must match the state every earlier boot showed, the rails must be readable and inside the limits, the
 *     share buffer must be a free region.  Only the two rail pairs proven on the device are READ (LL/CCI pair and L/B pair); B and CCI are derived from the
 *     stock sharing rule.  Any failure stops with nothing touched.
 *   - Every hardware access is announced by a `NORD2 DVFS access` line followed by a flush wait (the log is pulled by the host about every 250 ms), so a hard
 *     reset leaves the last access named in the log.  cfg->max_step stops the sequence early (0 preflight, 1 + share buffer, 2 + platform init, 3 = everything).
 *   - Every step is verified (reply word, CSRAM readback, measured clocks).  Once any init message has gone out, ANY unexpected reply, timeout or
 *     measurement runs the fallback: limits and requests of LL/L/B back to the bootloader rows, then stop; nothing is sent again.
 *   - The ramp moves one row at a time, L and B alternately, never above the cap, never while the gauge is unreadable or at or above 40.0 C, and each row
 *     must be reached (measured within 8 %) before the next one.
 */
#include <stdbool.h>
#include <stdint.h>
#include "nord2_cpuhvfs_init.h"

#define N2D_SHARE_PA       0x70000000u       /* mcupm_share: free first-bank DRAM (no mblock of the live FDT, outside the loader/payload/Wi-Fi claims) */
#define N2D_SHARE_SIZE     0x00200000u
#define N2D_PLT_INIT       0x504c5401u
#define N2D_MBOX_ALIVE     0u                /* channel 0: platform */
#define N2D_MBOX_DVFS      1u                /* channel 1: CPU DVFS */
#define N2D_IPI_TIMEOUT_MS 2000u
#define N2D_START_MAX_CC   4500              /* no ramp step at or above 45.0 C (0171: was 40.0 C) */
#define N2D_THROTTLE_CC    4600              /* one row slower per tick at or above 46.0 C (0171: was 43.0 C) */
#define N2D_RECOVER_CC     4400              /* one row faster per tick again below 44.0 C (0171: was 38.0 C, so a throttled phone stayed at the slowest row until it was nearly cold) */
#define N2D_CLOCK_PCT      8u
#define N2D_SETTLE_MS      50u
#define N2D_SETTLE_TRIES   6u

enum n2d_log {
	N2D_L_PREFLIGHT,    /* a = cluster, b = measured MHz, c = bootloader row, d = row MHz */
	N2D_L_RAIL,         /* a = cluster, b = Vproc (10 uV), c = Vsram (10 uV), d = 1 ok / 0 refused */
	N2D_L_SHARE,        /* a = pa, b = size, c = 1 ok / 0 refused */
	N2D_L_PLT,          /* a = 0 sent, 1 reply, 2 refused, 3 skipped (patch 0119); b = reply word / error */
	N2D_L_CSRAM,        /* a = 0 written, 1 verified ok, 2 mismatching words; b = count */
	N2D_L_IPI,          /* a = command, b = cluster/level, c = status (0 ok, < 0 error), d = first reply word */
	N2D_L_INITOK,       /* a = cluster, b = current row reported by the firmware, c = measured MHz, d = row MHz */
	N2D_L_RAMP,         /* a = cluster, b = row, c = row MHz, d = measured MHz (0 = row not reached) */
	N2D_L_RAMPDONE,     /* a = cluster, b = bootloader row, c = final row, d = final MHz */
	N2D_L_THERMAL,      /* a = reason (1 hot at start, 2 gauge unreadable, 3 stopped hot, 4 step slower, 5 step faster), b = cC, c = cluster, d = row */
	N2D_L_PLACE,        /* a = mode written, b = big MHz, c = little MHz */
	N2D_L_FALLBACK,     /* a = reason (enum n2d_result), b/c/d = bootloader rows of LL/L/B */
	N2D_L_RESULT,       /* a = enum n2d_result */
	N2D_L_ACCESS,       /* a = enum n2d_access, b/c = detail: logged BEFORE the access, followed by ops->flush */
	N2D_L_STAGE,        /* a = the stage limit that stopped the sequence (cfg->max_step) */
	N2D_L_GOV,          /* 0172, every 2 s: a = rowL | rowB << 8, b = utilL | utilB << 16 (permille, 0xffff unknown), c = row changes in the window, d = boost permille of the ticks */
	N2D_L_GOVCLK,       /* 0172, every 2 s: a = cluster, b = row MHz the governor asked for, c = measured MHz */
};

enum n2d_access {
	N2D_A_RAIL_LL = 1,  /* MT6315 SID 7 VBUCK3 + MT6359P VSRAM_PROC2 */
	N2D_A_RAIL_L,       /* MT6315 SID 6 VBUCK1 + MT6359P VSRAM_PROC1 */
	N2D_A_SHARE,        /* map and write the share buffer header */
	N2D_A_PLT,          /* doorbell on mailbox 0 */
	N2D_A_CSRAM,        /* the pinned CSRAM plan (stores) */
	N2D_A_IPI,          /* doorbell on mailbox 1: b = command */
	N2D_A_ONOFF,        /* sched-disable and cluster-on stores */
	N2D_A_RAMP,         /* limit and request stores for one ramp step: b = cluster, c = row */
};

enum n2d_result {
	N2D_OK_INIT = 1,         /* platform + DVFS hand-over done, clusters pinned at the bootloader rows */
	N2D_OK_RAMPED,           /* the ramp reached the cap rows */
	N2D_E_PRE_CLOCK,         /* preflight: a clock is not what every earlier boot showed (nothing sent) */
	N2D_E_PRE_RAIL,          /* preflight: a rail unreadable or out of the clamps (nothing sent) */
	N2D_E_PRE_SHARE,         /* preflight: the share region was refused (nothing sent) */
	N2D_E_PLT,               /* platform init failed (one message sent, no fallback needed: nothing was configured) */
	N2D_E_CSRAM,             /* CSRAM readback mismatch before any DVFS message (nothing sent to the DVFS channel) */
	N2D_E_IPI,               /* a DVFS message failed: fallback run */
	N2D_E_CLOCK,             /* a clock disagreed after the hand-over or during the ramp: fallback run */
	N2D_E_RAMP,              /* a ramp row was not reached: fallback run */
	N2D_E_ARGS,
	N2D_E_STAGE,             /* stopped on purpose at cfg->max_step (nothing wrong) */
};

struct n2d_ops {
	void (*w32)(void *ctx, uint32_t off, uint32_t v);                      /* CSRAM store + barrier */
	uint32_t (*r32)(void *ctx, uint32_t off);
	/* one mailbox exchange on channel `ch`: store the n words, ring the doorbell, wait up to timeout_ms for the reply bit, read nreply words, clear the bit.
	 * 0 = reply seen, < 0 = busy/timeout/bad arguments (the doorbell may be pending). */
	int (*mbox)(void *ctx, unsigned ch, const uint32_t *w, unsigned n, uint32_t *reply, unsigned nreply, unsigned timeout_ms);
	bool (*share_init)(void *ctx, uint32_t pa, uint32_t size);              /* map, zero, write the stock header, verify the readback */
	bool (*rail)(void *ctx, unsigned cluster, uint32_t *vproc_10uv, uint32_t *vsram_10uv);
	unsigned (*mhz)(void *ctx, int cpu);                                     /* measured clock, 0 = cannot measure */
	bool (*gauge)(void *ctx, int *centi_c);
	void (*sleep_ms)(void *ctx, unsigned ms);
	void (*set_place)(void *ctx, int mode);                                  /* write nord2_sched_mode */
	void (*log)(void *ctx, int code, unsigned a, unsigned b, unsigned c, unsigned d);
	void (*flush)(void *ctx);                                                 /* wait until the host has pulled the log lines written so far (bounded) */
	/* 0172: load-based scaling (all optional; the governor does nothing without them) */
	int (*util)(void *ctx, int cpu);                                          /* busy permille of the CPU over the last ~100 ms, < 0 unknown; called once per CPU per governor tick */
	uint64_t (*now_ms)(void *ctx);
	uint64_t (*ui_kick_ms)(void *ctx);                                        /* time of the last UI activity (a CoreAnimation swap), 0 = none */
};

struct n2d_cfg {
	unsigned cap_mhz;            /* 0 = hand-over only, no ramp; otherwise the cap of L and B (>= 500) */
	bool place;                  /* manage the 0099 placement */
	unsigned max_step;           /* 0 preflight only, 1 + share buffer, 2 + platform init, >= 3 everything */
	bool skip_plt;               /* patch 0119: no share buffer, no platform init.  The stock Image never builds 0x504c5401 (no movz/movk/literal anywhere); P20 sent it and the
	                              * firmware answered 0, an unknown command.  With the flag max_step 1 and 2 stop before the hand-over (nothing sent). */
};

struct n2d_state {
	uint8_t orig[3];             /* bootloader rows LL, L, B */
	uint8_t cur[3];              /* last row written and confirmed */
	uint8_t target[3];           /* ramp targets (= orig when not ramping) */
	unsigned boot_mhz[3];
	bool sent;                   /* a DVFS-channel message went out: fallback applies */
	bool inited;
	bool stopped;
	bool fallback_done;
	int place_mode;
	enum n2d_result result;
};

static const int n2d_first_cpu[3] = { 0, 4, 7 };

static inline unsigned
n2d_row_mhz(unsigned cluster, unsigned row)
{
	return n2dv_l1_pvt[N2DV_NR_FREQ * cluster + row][0];
}

static inline bool
n2d_within_pct(unsigned measured, unsigned wanted, unsigned pct)
{
	if (measured == 0 || wanted == 0) {
		return false;
	}
	unsigned d = measured > wanted ? measured - wanted : wanted - measured;
	return (uint64_t)d * 100u <= (uint64_t)wanted * pct;
}

/* After the hand-over the clock must either be unchanged or at the pinned row (B is pinned 10 % below what the bootloader runs). */
static inline bool
n2d_clock_ok(unsigned measured, unsigned boot_mhz, unsigned row_mhz)
{
	return n2d_within_pct(measured, boot_mhz, N2D_CLOCK_PCT) || n2d_within_pct(measured, row_mhz, N2D_CLOCK_PCT);
}

/* The bootloader state of every earlier boot: LL ~1791, L and B ~730 MHz.  Anything else means this boot is not what the pinned rows were derived from. */
static inline bool
n2d_boot_window_ok(unsigned cluster, unsigned mhz)
{
	static const unsigned lo[3] = { 1700, 650, 650 }, hi[3] = { 1900, 800, 800 };
	return cluster < 3 && mhz >= lo[cluster] && mhz <= hi[cluster];
}

/* First row (fastest) of a cluster whose frequency does not exceed cap_mhz; -1 if none. */
static inline int
n2d_row_for_cap(unsigned cluster, unsigned cap_mhz)
{
	for (unsigned i = 0; i < N2DV_NR_FREQ; i++) {
		if (n2d_row_mhz(cluster, i) <= cap_mhz) {
			return (int)i;
		}
	}
	return -1;
}

/* The share region must be 64 KiB aligned, 2 MiB, below 4 GiB and clear of every reservation the live FDT and the loader know (README 12.7). */
static inline bool
n2d_share_ok(uint64_t pa, uint64_t size)
{
	static const struct { uint64_t base, size; } busy[] = {
		{ 0x40000000, 0x14000000 },                                          /* loader, stock kernel + ramdisk, SWEETXNU payload window */
		{ 0x48080000, 0x00180000 },                                          /* aee_debug_kinfo, pstore, minirdump */
		{ 0x48c00000, 0x00200000 },                                          /* atf-reserved */
		{ 0x5ff00000, 0x00080000 },                                          /* PICACHU */
		{ 0x74000000, 0x00e30000 },                                          /* adsp */
		{ 0x75e00000, 0x00010000 },                                          /* SPM */
		{ 0x76e10000, 0x01df0000 },                                          /* framebuffer */
		{ 0x78c00000, 0x05800000 },                                          /* tee-reserved */
		{ 0x7e400000, 0x00400000 },                                          /* tee-secmem */
		{ 0x7e800000, 0x00041000 },                                          /* gz-log */
		{ 0x7e900000, 0x00100000 },                                          /* MCUPM-reserved */
		{ 0x7ea00000, 0x01400000 },                                          /* gz */
		{ 0x7feb0000, 0x00100000 },                                          /* SSPM-reserved */
		{ 0x7ffbf000, 0x00040000 },                                          /* log_store */
		{ 0x7ffff000, 0x00001000 },                                          /* emi_mbist_buf */
		{ 0x8a000000, 0x03560000 },                                          /* ccci */
		{ 0x8e000000, 0x01800000 },                                          /* ap_md_nc_smem */
		{ 0x8fb00000, 0x01500000 },                                          /* Wi-Fi CONN claim and DMA pool (0x8fb00000 + 0x450000, 0x90000000 + 0xf20000) */
		{ 0x9fa00000, 0x00500000 },                                          /* SCP-reserved */
		{ 0xbd6c0000, 0x02740000 },                                          /* vpu_binary */
		{ 0xbfe00000, 0x00200000 },                                          /* atf-log, atf-ramdump, ccci_tag_mem, up to 4 GiB bank end */
		{ 0xdfe70000, 0x00190000 },                                          /* DPM */
		{ 0xe0000000, 0x20000000 },                                          /* md_mem_usage */
	};
	if (size != N2D_SHARE_SIZE || (pa & 0xffffu) != 0 || pa + size > 0x100000000ull) {
		return false;
	}
	for (unsigned i = 0; i < sizeof(busy) / sizeof(busy[0]); i++) {
		if (pa < busy[i].base + busy[i].size && busy[i].base < pa + size) {
			return false;
		}
	}
	return true;
}

/* Placement: the A78 clusters are about twice as fast per clock as the A55s, so "measured faster than the A55s" is enough; 5 % hysteresis. */
static inline int
n2d_place_decide(unsigned big_mhz, unsigned little_mhz, int cur_mode)
{
	if (big_mhz == 0 || little_mhz == 0) {
		return cur_mode;
	}
	if (cur_mode == 0 && (uint64_t)big_mhz * 100u >= (uint64_t)little_mhz * 105u) {
		return 0xf;
	}
	if (cur_mode != 0 && (uint64_t)big_mhz * 100u < (uint64_t)little_mhz * 95u) {
		return 0;
	}
	return cur_mode;
}

/* Announce a hardware access and give the host time to pull the line before it happens. */
static inline void
n2d_trace(const struct n2d_ops *o, void *ctx, unsigned what, unsigned b, unsigned c)
{
	o->log(ctx, N2D_L_ACCESS, what, b, c, 0);
	o->flush(ctx);
}

static inline void
n2d_fallback(const struct n2d_ops *o, void *ctx, struct n2d_state *st, enum n2d_result why)
{
	if (st->stopped) {
		return;
	}
	if (st->sent) {                                    /* nothing was configured before the first DVFS message: nothing to pin */
		n2dv_fallback_pin(&(const struct n2dv_ops){ o->w32 }, ctx, st->orig);
		o->w32(ctx, N2DV_OFF_REQ + 4 * N2DV_CCI, n2dv_cci_row(st->orig[N2DV_LL], st->orig[N2DV_B]));
		for (unsigned c = 0; c < 3; c++) {
			st->cur[c] = st->orig[c];
		}
		st->fallback_done = true;
	}
	st->stopped = true;
	st->result = why;
	o->log(ctx, N2D_L_FALLBACK, (unsigned)why, st->orig[0], st->orig[1], st->orig[2]);
	o->log(ctx, N2D_L_RESULT, (unsigned)why, 0, 0, 0);
}

static inline bool
n2d_ipi(const struct n2d_ops *o, void *ctx, const uint32_t m[N2DV_IPI_WORDS], unsigned arg)
{
	uint32_t reply[4] = { 0, 0, 0, 0 };
	n2d_trace(o, ctx, N2D_A_IPI, m[0], arg);
	int rc = o->mbox(ctx, N2D_MBOX_DVFS, m, N2DV_IPI_WORDS, reply, 4, N2D_IPI_TIMEOUT_MS);
	o->log(ctx, N2D_L_IPI, m[0], arg, (unsigned)rc, reply[0]);
	return rc == 0;
}

/* Write limits then request for one cluster and wait until the CPU measures the row.  false when it does not. */
static inline bool
n2d_goto_row(const struct n2d_ops *o, void *ctx, struct n2d_state *st, unsigned c, unsigned row)
{
	n2d_trace(o, ctx, N2D_A_RAMP, c, row);
	/* The CCI request follows the vendor CCI map [LL row][B row] (assumption: README 12.7); faster uncore first, slower uncore last. */
	uint8_t next[3] = { st->cur[0], st->cur[1], st->cur[2] };
	next[c] = (uint8_t)row;
	uint32_t cci = n2dv_cci_row(next[N2DV_LL], next[N2DV_B]);
	if (row < st->cur[c]) {
		o->w32(ctx, N2DV_OFF_REQ + 4 * N2DV_CCI, cci);
	}
	o->w32(ctx, N2DV_OFF_PPM_LIMIT + 4 * c, (uint32_t)row << 16 | row);
	o->w32(ctx, N2DV_OFF_REQ + 4 * c, row);
	if (row >= st->cur[c]) {
		o->w32(ctx, N2DV_OFF_REQ + 4 * N2DV_CCI, cci);
	}
	unsigned want = n2d_row_mhz(c, row), m = 0;
	for (unsigned t = 0; t < N2D_SETTLE_TRIES; t++) {
		o->sleep_ms(ctx, N2D_SETTLE_MS);
		m = o->mhz(ctx, n2d_first_cpu[c]);
		if (n2d_within_pct(m, want, N2D_CLOCK_PCT)) {
			st->cur[c] = (uint8_t)row;
			o->log(ctx, N2D_L_RAMP, c, row, want, m);
			return true;
		}
	}
	o->log(ctx, N2D_L_RAMP, c, row, want, 0);
	return false;
}

/* Steps 1 and 2 (and 3 when cfg->cap_mhz != 0).  Returns the final result; the state holds the rows. */
static inline enum n2d_result
n2d_bringup(const struct n2d_ops *o, void *ctx, const struct n2d_cfg *cfg, struct n2d_state *st)
{
	st->orig[0] = st->orig[1] = st->orig[2] = 0;
	st->sent = st->inited = st->stopped = st->fallback_done = false;
	st->place_mode = 0;
	if (cfg->cap_mhz != 0 && cfg->cap_mhz < 500) {
		st->stopped = true;
		o->log(ctx, N2D_L_RESULT, N2D_E_ARGS, 0, 0, 0);
		return st->result = N2D_E_ARGS;
	}

	/* ---- preflight: nothing is sent or written ---- */
	for (unsigned c = 0; c < 3; c++) {
		unsigned m = o->mhz(ctx, n2d_first_cpu[c]);
		int row = n2d_boot_window_ok(c, m) ? n2dv_row_for_mhz(c, m) : -1;
		o->log(ctx, N2D_L_PREFLIGHT, c, m, row < 0 ? 99u : (unsigned)row, row < 0 ? 0 : n2d_row_mhz(c, (unsigned)row));
		if (row < 0) {
			st->stopped = true;
			o->log(ctx, N2D_L_RESULT, N2D_E_PRE_CLOCK, 0, 0, 0);
			return st->result = N2D_E_PRE_CLOCK;
		}
		st->boot_mhz[c] = m;
		st->orig[c] = st->cur[c] = st->target[c] = (uint8_t)row;
	}
	/* Only the two rail pairs proven on the device are read (c6t): LL/CCI share Vproc2 + Vsram2, L/B share Vproc1 + Vsram1 (mtk_cpufreq_platform.c).  B and CCI take the
	 * values of their partners; they are printed as derived.  All four are judged before the verdict. */
	uint32_t rv[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
	bool read_ok[2];
	for (unsigned c = 0; c < 2; c++) {
		n2d_trace(o, ctx, c == 0 ? N2D_A_RAIL_LL : N2D_A_RAIL_L, c, 0);
		read_ok[c] = o->rail(ctx, c, &rv[c][0], &rv[c][1]);
	}
	rv[N2DV_B][0] = rv[N2DV_L][0]; rv[N2DV_B][1] = rv[N2DV_L][1];
	rv[N2DV_CCI][0] = rv[N2DV_LL][0]; rv[N2DV_CCI][1] = rv[N2DV_LL][1];
	bool rails_good = true;
	for (unsigned c = 0; c < N2DV_NR_CLUSTERS; c++) {
		bool ok = read_ok[c < 2 ? c : (c == N2DV_B ? 1 : 0)] && n2dv_rails_ok(rv[c][0], rv[c][1]);     /* the stock probe's own check plus garbage floors */
		o->log(ctx, N2D_L_RAIL, c, rv[c][0], rv[c][1], ok);
		rails_good = rails_good && ok;
	}
	if (!rails_good) {
		st->stopped = true;
		o->log(ctx, N2D_L_RESULT, N2D_E_PRE_RAIL, 0, 0, 0);
		return st->result = N2D_E_PRE_RAIL;
	}
	if (cfg->max_step == 0) {
		st->stopped = true;
		o->log(ctx, N2D_L_STAGE, 0, 0, 0, 0);
		return st->result = N2D_E_STAGE;
	}
	if (cfg->skip_plt) {
		o->log(ctx, N2D_L_PLT, 3, 0, 0, 0);                           /* skipped on purpose: the stock kernel sends no platform init */
		if (cfg->max_step < 3) {
			st->stopped = true;
			o->log(ctx, N2D_L_STAGE, cfg->max_step, 0, 0, 0);
			return st->result = N2D_E_STAGE;
		}
	} else {
		n2d_trace(o, ctx, N2D_A_SHARE, N2D_SHARE_PA, N2D_SHARE_SIZE);
		if (!n2d_share_ok(N2D_SHARE_PA, N2D_SHARE_SIZE) || !o->share_init(ctx, N2D_SHARE_PA, N2D_SHARE_SIZE)) {
			o->log(ctx, N2D_L_SHARE, N2D_SHARE_PA, N2D_SHARE_SIZE, 0, 0);
			st->stopped = true;
			o->log(ctx, N2D_L_RESULT, N2D_E_PRE_SHARE, 0, 0, 0);
			return st->result = N2D_E_PRE_SHARE;
		}
		o->log(ctx, N2D_L_SHARE, N2D_SHARE_PA, N2D_SHARE_SIZE, 1, 0);
		if (cfg->max_step == 1) {
			st->stopped = true;
			o->log(ctx, N2D_L_STAGE, 1, 0, 0, 0);
			return st->result = N2D_E_STAGE;
		}

		/* ---- step 1: platform init (stock mcupm_plt_init): {0x504C5401, phys, size}, reply word must be exactly 1 ---- */
		{
			uint32_t w[3] = { N2D_PLT_INIT, N2D_SHARE_PA, N2D_SHARE_SIZE }, reply[1] = { 0 };
			o->log(ctx, N2D_L_PLT, 0, w[1], w[2], 0);
			n2d_trace(o, ctx, N2D_A_PLT, w[1], w[2]);
			int rc = o->mbox(ctx, N2D_MBOX_ALIVE, w, 3, reply, 1, N2D_IPI_TIMEOUT_MS);
			o->log(ctx, N2D_L_PLT, rc == 0 ? 1u : 2u, rc == 0 ? reply[0] : (unsigned)rc, 0, 0);
			if (rc != 0 || reply[0] != 1) {
				st->stopped = true;                                    /* nothing configured yet, so nothing to pin */
				o->log(ctx, N2D_L_RESULT, N2D_E_PLT, 0, 0, 0);
				return st->result = N2D_E_PLT;
			}
		}
		if (cfg->max_step == 2) {
			st->stopped = true;
			o->log(ctx, N2D_L_STAGE, 2, 0, 0, 0);
			return st->result = N2D_E_STAGE;
		}

	}

	/* ---- step 2: DVFS hand-over, every cluster pinned at the bootloader row ---- */
	{
		struct n2dv_plan plan = { .level = N2DV_ONLY_LEVEL, .pin = true, .row = { st->orig[0], st->orig[1], st->orig[2] } };
		struct n2dv_ops wops = { o->w32 };
		n2d_trace(o, ctx, N2D_A_CSRAM, 0, 0);
		if (n2dv_csram_init(&wops, ctx, &plan) != 0) {
			st->stopped = true;
			o->log(ctx, N2D_L_RESULT, N2D_E_ARGS, 0, 0, 0);
			return st->result = N2D_E_ARGS;
		}
		o->log(ctx, N2D_L_CSRAM, 0, 0, 0, 0);
		unsigned bad = 0;
		for (unsigned k = 0; k < (N2DV_OFF_TBL_END - N2DV_OFF_TBL) / 4u; k++) {
			bad += o->r32(ctx, N2DV_OFF_TBL + 4 * k) != n2dv_pvt_word(k);
		}
		for (unsigned k = 0; k < N2DV_CCI_MAP_BYTES / 4u; k++) {
			const uint8_t *b = &n2dv_l1_cci[0][0] + 4 * k;
			uint32_t want = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
			bad += o->r32(ctx, N2DV_OFF_CCI_MAP + 4 * k) != want;
		}
		for (unsigned c = 0; c < 3; c++) {
			bad += o->r32(ctx, N2DV_OFF_PPM_LIMIT + 4 * c) != ((uint32_t)st->orig[c] << 16 | st->orig[c]);
			bad += o->r32(ctx, N2DV_OFF_REQ + 4 * c) != st->orig[c];
		}
		o->log(ctx, N2D_L_CSRAM, bad ? 2u : 1u, bad, 0, 0);
		if (bad) {
			st->stopped = true;                                    /* no DVFS message sent yet */
			o->log(ctx, N2D_L_RESULT, N2D_E_CSRAM, 0, 0, 0);
			return st->result = N2D_E_CSRAM;
		}
	}
	uint32_t m[N2DV_IPI_WORDS];
	st->sent = true;                                                  /* from here on a failure runs the fallback */
	n2dv_msg_init_ptbl(m);
	if (!n2d_ipi(o, ctx, m, 0)) {
		n2d_fallback(o, ctx, st, N2D_E_IPI);
		return st->result;
	}
	for (unsigned c = 0; c < N2DV_NR_CLUSTERS; c++) {        /* the voltages read in the preflight: nothing moved a rail since, and no further PMIC access is made */
		if (!n2dv_msg_init_volt(m, c, rv[c][0], rv[c][1]) || !n2d_ipi(o, ctx, m, c)) {
			n2d_fallback(o, ctx, st, N2D_E_IPI);
			return st->result;
		}
	}
	n2dv_msg_init_sta(m, N2DV_ONLY_LEVEL);
	if (!n2d_ipi(o, ctx, m, N2DV_ONLY_LEVEL)) {
		n2d_fallback(o, ctx, st, N2D_E_IPI);
		return st->result;
	}
	{
		struct n2dv_ops wops = { o->w32 };
		n2d_trace(o, ctx, N2D_A_ONOFF, 0, 0);
		n2dv_sched_disable(&wops, ctx);
		for (unsigned c = 0; c < 3; c++) {
			n2dv_cluster_on(&wops, ctx, c);
		}
	}
	o->sleep_ms(ctx, 100);
	for (unsigned c = 0; c < 3; c++) {
		unsigned meas = o->mhz(ctx, n2d_first_cpu[c]), want = n2d_row_mhz(c, st->orig[c]);
		o->log(ctx, N2D_L_INITOK, c, o->r32(ctx, N2DV_OFF_CUR_FREQ + 4 * c), meas, want);
		if (!n2d_clock_ok(meas, st->boot_mhz[c], want)) {
			n2d_fallback(o, ctx, st, N2D_E_CLOCK);
			return st->result;
		}
	}
	st->inited = true;
	st->result = N2D_OK_INIT;
	o->log(ctx, N2D_L_RESULT, N2D_OK_INIT, 0, 0, 0);
	if (cfg->cap_mhz == 0) {
		return st->result;
	}

	/* ---- step 3: the ramp, one row at a time, L and B alternately, up to the cap ---- */
	int tl = n2d_row_for_cap(N2DV_L, cfg->cap_mhz), tb = n2d_row_for_cap(N2DV_B, cfg->cap_mhz);
	if (tl < 0 || tb < 0) {
		return st->result;
	}
	st->target[N2DV_L] = (uint8_t)(tl < st->orig[N2DV_L] ? tl : st->orig[N2DV_L]);       /* set before the gauge test: the tick ramps later once the battery is cool */
	st->target[N2DV_B] = (uint8_t)(tb < st->orig[N2DV_B] ? tb : st->orig[N2DV_B]);
	int cC = 0;
	if (!o->gauge(ctx, &cC)) {
		o->log(ctx, N2D_L_THERMAL, 2, 0, 0, 0);
		return st->result;
	}
	if (cC >= N2D_START_MAX_CC) {
		o->log(ctx, N2D_L_THERMAL, 1, (unsigned)cC, 0, 0);
		return st->result;
	}
	for (;;) {
		bool moved = false;
		for (unsigned c = N2DV_L; c <= N2DV_B; c++) {
			if (st->cur[c] <= st->target[c]) {
				continue;
			}
			if (!o->gauge(ctx, &cC)) {
				o->log(ctx, N2D_L_THERMAL, 2, 0, c, st->cur[c]);
				return st->result;
			}
			if (cC >= N2D_START_MAX_CC) {
				o->log(ctx, N2D_L_THERMAL, 3, (unsigned)cC, c, st->cur[c]);
				return st->result;
			}
			if (!n2d_goto_row(o, ctx, st, c, (unsigned)st->cur[c] - 1u)) {
				n2d_fallback(o, ctx, st, N2D_E_RAMP);
				return st->result;
			}
			moved = true;
		}
		if (!moved) {
			break;
		}
	}
	for (unsigned c = N2DV_L; c <= N2DV_B; c++) {
		o->log(ctx, N2D_L_RAMPDONE, c, st->orig[c], st->cur[c], o->mhz(ctx, n2d_first_cpu[c]));
	}
	st->result = N2D_OK_RAMPED;
	o->log(ctx, N2D_L_RESULT, N2D_OK_RAMPED, 0, 0, 0);
	return st->result;
}

/* Every two seconds after the bring-up: the battery guard for the ramped clusters, a sanity check of the clocks, and the placement switch. */
static inline void
n2d_tick(const struct n2d_ops *o, void *ctx, const struct n2d_cfg *cfg, struct n2d_state *st)
{
	if (st->stopped || !st->inited) {
		return;
	}
	if (cfg->cap_mhz != 0) {
		int cC = 0;
		if (o->gauge(ctx, &cC)) {
			for (unsigned c = N2DV_L; c <= N2DV_B && !st->stopped; c++) {
				if (cC >= N2D_THROTTLE_CC && st->cur[c] < st->orig[c]) {
					o->log(ctx, N2D_L_THERMAL, 4, (unsigned)cC, c, (unsigned)st->cur[c] + 1u);
					if (!n2d_goto_row(o, ctx, st, c, (unsigned)st->cur[c] + 1u)) {
						n2d_fallback(o, ctx, st, N2D_E_CLOCK);
					}
				} else if (cC < N2D_RECOVER_CC && st->cur[c] > st->target[c]) {
					o->log(ctx, N2D_L_THERMAL, 5, (unsigned)cC, c, (unsigned)st->cur[c] - 1u);
					if (!n2d_goto_row(o, ctx, st, c, (unsigned)st->cur[c] - 1u)) {
						n2d_fallback(o, ctx, st, N2D_E_RAMP);
					}
				}
			}
			if (!st->stopped && st->result == N2D_OK_INIT && st->cur[N2DV_L] <= st->target[N2DV_L] && st->cur[N2DV_B] <= st->target[N2DV_B] &&
			    (st->target[N2DV_L] < st->orig[N2DV_L] || st->target[N2DV_B] < st->orig[N2DV_B])) {
				st->result = N2D_OK_RAMPED;
				o->log(ctx, N2D_L_RESULT, N2D_OK_RAMPED, 0, 0, 0);
			}
		}
	}
	if (!st->stopped && cfg->place) {
		unsigned big = o->mhz(ctx, n2d_first_cpu[N2DV_L]), little = o->mhz(ctx, n2d_first_cpu[N2DV_LL]);
		int next = n2d_place_decide(big, little, st->place_mode);
		if (next != st->place_mode) {
			st->place_mode = next;
			o->set_place(ctx, next);
			o->log(ctx, N2D_L_PLACE, (unsigned)next, big, little, 0);
		}
	}
}

/* ---- 0172: load-based frequency scaling ----
 * The clusters used to sit at the cap (L 2.6 / B 3.0 GHz) whatever the load.  Every N2D_GOV_TICK_MS the busiest CPU of L (cpu4-6) and of B (cpu7) is measured; the row that would put
 * it at N2D_GOV_TARGET_PERMILLE is chosen: faster at once, slower only after N2D_GOV_DOWN_HOLD_MS below it.  While the UI is active (a CoreAnimation swap within N2D_GOV_BOOST_MS) the
 * clusters never run below the floor MHz.  The thermal guard no longer steps the clock itself: it moves the fastest row the governor may pick (limit), one row per 2 s.
 * Rows are written like n2d_goto_row does (CCI order included) but without the 50 ms settle wait and the measurement; the clock is measured and logged every 2 s instead. */
#define N2D_GOV_TICK_MS         50u
#define N2D_GOV_TARGET_PERMILLE 600u
#define N2D_GOV_DOWN_HOLD_MS    150u
#define N2D_GOV_BOOST_MS        150u
#define N2D_GOV_FLOOR_L_MHZ     1740u        /* while the UI is active */
#define N2D_GOV_FLOOR_B_MHZ     1998u
#define N2D_GOV_IDLE_L_MHZ      1046u        /* slowest the governor goes (the bootloader rows 700/659 MHz are only for the fallback) */
#define N2D_GOV_IDLE_B_MHZ      1108u
#define N2D_GOV_SLOW_TICK_MS    2000u
#define N2D_GOV_STEP_MS         2u

struct n2d_gov {
	uint8_t limit[3];            /* fastest row the thermal guard allows (>= st->target) */
	uint8_t slowest[3];          /* slowest row the governor uses (<= st->orig) */
	uint64_t below_since[3];
	uint64_t last_slow_ms;
	unsigned ticks, boost_ticks, changes;
	int util[3];
};

/* last row whose clock is at least mhz (slowest row that still offers it), clamped to [fastest, slowest] */
static inline unsigned
n2d_row_at_least(unsigned cluster, unsigned mhz, unsigned fastest, unsigned slowest)
{
	for (unsigned i = slowest + 1; i-- > fastest;) {
		if (n2d_row_mhz(cluster, i) >= mhz) {
			return i;
		}
	}
	return fastest;
}

static inline void
n2d_set_row_fast(const struct n2d_ops *o, void *ctx, struct n2d_state *st, unsigned c, unsigned row)
{
	/* One row at a time (the transition the firmware was verified with), N2D_GOV_STEP_MS apart; a jump over 12 rows costs ~25 ms.  A multi-row jump in one store crashed the phone (p73). */
	while (st->cur[c] != row) {
		unsigned step = st->cur[c] > row ? (unsigned)st->cur[c] - 1u : (unsigned)st->cur[c] + 1u;
		uint8_t next[3] = { st->cur[0], st->cur[1], st->cur[2] };
		next[c] = (uint8_t)step;
		uint32_t cci = n2dv_cci_row(next[N2DV_LL], next[N2DV_B]);
		if (step < st->cur[c]) {
			o->w32(ctx, N2DV_OFF_REQ + 4 * N2DV_CCI, cci);
		}
		o->w32(ctx, N2DV_OFF_PPM_LIMIT + 4 * c, (uint32_t)step << 16 | step);
		o->w32(ctx, N2DV_OFF_REQ + 4 * c, step);
		if (step >= st->cur[c]) {
			o->w32(ctx, N2DV_OFF_REQ + 4 * N2DV_CCI, cci);
		}
		st->cur[c] = (uint8_t)step;
		if (st->cur[c] != row) {
			o->sleep_ms(ctx, N2D_GOV_STEP_MS);
		}
	}
}

static inline void
n2d_gov_init(const struct n2d_ops *o, void *ctx, const struct n2d_state *st, struct n2d_gov *g)
{
	int cC = 0;
	bool valid = o->gauge(ctx, &cC);
	for (unsigned c = 0; c < 3; c++) {
		g->limit[c] = st->target[c];
		g->slowest[c] = st->orig[c];
		g->below_since[c] = 0;
		g->util[c] = -1;
	}
	for (unsigned c = N2DV_L; c <= N2DV_B; c++) {
		unsigned idle = n2d_row_at_least(c, c == N2DV_L ? N2D_GOV_IDLE_L_MHZ : N2D_GOV_IDLE_B_MHZ, st->target[c], st->orig[c]);
		g->slowest[c] = (uint8_t)idle;
		if (!valid || cC >= N2D_START_MAX_CC) {
			g->limit[c] = (uint8_t)idle;           /* hot or unknown at start: nothing above the idle row until the guard says it is cool */
		}
	}
	g->last_slow_ms = 0;
	g->ticks = g->boost_ticks = g->changes = 0;
}

static inline void
n2d_gov_tick(const struct n2d_ops *o, void *ctx, const struct n2d_cfg *cfg, struct n2d_state *st, struct n2d_gov *g)
{
	if (st->stopped || !st->inited || !o->util || !o->now_ms) {
		return;
	}
	uint64_t now = o->now_ms(ctx);
	uint64_t kick = o->ui_kick_ms ? o->ui_kick_ms(ctx) : 0;
	bool boost = kick != 0 && now >= kick && now - kick < N2D_GOV_BOOST_MS;
	g->ticks++;
	if (boost) {
		g->boost_ticks++;
	}
	for (unsigned c = N2DV_L; c <= N2DV_B; c++) {
		int u = -1;
		unsigned ncpu = c == N2DV_L ? 3u : 1u;
		for (unsigned k = 0; k < ncpu; k++) {
			int x = o->util(ctx, n2d_first_cpu[c] + (int)k);
			if (x > u) {
				u = x;
			}
		}
		g->util[c] = u;
		unsigned fastest = g->limit[c], slowest = g->slowest[c], cur = st->cur[c];
		if (fastest > slowest) {
			slowest = fastest;
		}
		unsigned want = cur;
		if (u >= 0) {
			uint64_t need = ((uint64_t)n2d_row_mhz(c, cur) * (unsigned)u + N2D_GOV_TARGET_PERMILLE - 1) / N2D_GOV_TARGET_PERMILLE;
			unsigned row = n2d_row_at_least(c, (unsigned)need, fastest, slowest);
			if (row < cur) {
				want = row;                                       /* faster: at once */
				g->below_since[c] = 0;
			} else if (row > cur) {                               /* slower: after the hold */
				if (g->below_since[c] == 0) {
					g->below_since[c] = now ? now : 1;
				}
				if (now - g->below_since[c] >= N2D_GOV_DOWN_HOLD_MS) {
					want = row;
				}
			} else {
				g->below_since[c] = 0;
			}
		}
		if (boost) {
			unsigned floor_row = n2d_row_at_least(c, c == N2DV_L ? N2D_GOV_FLOOR_L_MHZ : N2D_GOV_FLOOR_B_MHZ, fastest, slowest);
			if (want > floor_row) {
				want = floor_row;
			}
		}
		if (want < fastest) {
			want = fastest;
		}
		if (want > slowest) {
			want = slowest;
		}
		if (want != cur) {
			n2d_set_row_fast(o, ctx, st, c, want);
			g->changes++;
		}
	}
	if (g->last_slow_ms == 0 || now - g->last_slow_ms >= N2D_GOV_SLOW_TICK_MS) {
		bool first = g->last_slow_ms == 0;
		g->last_slow_ms = now ? now : 1;
		int cC = 0;
		if (o->gauge(ctx, &cC)) {                                 /* thermal guard: move the fastest allowed row, one row per slow tick */
			for (unsigned c = N2DV_L; c <= N2DV_B; c++) {
				if (cC >= N2D_THROTTLE_CC && g->limit[c] < g->slowest[c]) {
					g->limit[c]++;
					o->log(ctx, N2D_L_THERMAL, 4, (unsigned)cC, c, g->limit[c]);
				} else if (cC < N2D_RECOVER_CC && g->limit[c] > st->target[c]) {
					g->limit[c]--;
					o->log(ctx, N2D_L_THERMAL, 5, (unsigned)cC, c, g->limit[c]);
				}
			}
		}
		if (!first) {
			for (unsigned c = N2DV_L; c <= N2DV_B; c++) {
				o->log(ctx, N2D_L_GOVCLK, c, n2d_row_mhz(c, st->cur[c]), o->mhz(ctx, n2d_first_cpu[c]), 0);
			}
			unsigned ul = g->util[N2DV_L] < 0 ? 0xffffu : (unsigned)g->util[N2DV_L], ub = g->util[N2DV_B] < 0 ? 0xffffu : (unsigned)g->util[N2DV_B];
			o->log(ctx, N2D_L_GOV, st->cur[N2DV_L] | (unsigned)st->cur[N2DV_B] << 8, ul | ub << 16, g->changes, g->ticks ? g->boost_ticks * 1000u / g->ticks : 0u);
		}
		g->ticks = g->boost_ticks = g->changes = 0;
		if (!st->stopped && cfg->place) {                          /* placement follows the cap, not the momentary clock */
			int next = n2d_place_decide(n2d_row_mhz(N2DV_L, st->target[N2DV_L]), n2d_row_mhz(N2DV_LL, st->cur[N2DV_LL]), st->place_mode);
			if (next != st->place_mode) {
				st->place_mode = next;
				o->set_place(ctx, next);
				o->log(ctx, N2D_L_PLACE, (unsigned)next, n2d_row_mhz(N2DV_L, st->target[N2DV_L]), n2d_row_mhz(N2DV_LL, st->cur[N2DV_LL]), 0);
			}
		}
	}
}
#endif
