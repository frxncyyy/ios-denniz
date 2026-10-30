#ifndef NORD2_CPUHVFS_INIT_H
#define NORD2_CPUHVFS_INIT_H
/*
 * MT6893 CPU DVFS ("cpuhvfs", MCUPM firmware) initial hand-over, pure and host-testable: step 3 of README-0107-design.md section 12.6.  No kernel headers and
 * no register access: every CSRAM store goes through struct n2dv_ops.  NOT part of any kernel image yet.
 *
 * Source of every number: the vendor 4.19 tree on disk (work/nord2-current-kernel/drivers/misc/mediatek/base/power/cpufreq_v1/src): v2/mtk_cpufreq_hybrid.c
 * (cpuhvfs_pre_module_init -> init_cpuhvfs_debug_repo, cpuhvfs_pvt_tbl_create, cpuhvfs_set_init_ptbl; cpuhvfs_set_init_volt, cpuhvfs_set_init_sta,
 * cpuhvfs_set_cluster_on_off, cpuhvfs_set_dvfs, cpuhvfs_set_min_max), mach/mt6893/{mtk_cpufreq_platform.[ch],mtk_cpufreq_opp_pv_table.h}, cross-checked with
 * the stock Image (IPI command numbers 0, 1, 10, the 4-word message, level 1).  What the AP really does (README-0107-design.md 12.7):
 *   1. CSRAM: debug-repo guards, zero 0x2a0..0x1400, PPM limit defaults, the per-chip PVT table at +0x10 (4 clusters x 36 words), the CCI map at +0xfa0;
 *   2. IPI cmd 0 {0, segment 0}; 3. IPI cmd 10 per cluster {j, Vproc, Vsram} (10 uV units, read back from the PMICs); 4. IPI cmd 1 {1, cpu level};
 *   5. sched-dvfs-disable word, cluster-on magics, then frequency REQUESTS are one 32-bit store of a row index at +0x37c + 4*cluster.
 * Voltages are never written by the AP: the firmware owns Vproc, Vsram and the PLLs.
 */
#include <stdbool.h>
#include <stdint.h>
#include "nord2_cpuhvfs_tables.h"

#define N2DV_CSRAM_PA          0x0011bc00ull
#define N2DV_CSRAM_SIZE        0x1400u
#define N2DV_NR_FREQ           16u
#define N2DV_LL 0u
#define N2DV_L  1u
#define N2DV_B  2u
#define N2DV_CCI 3u
#define N2DV_NR_CLUSTERS 4u

#define N2DV_OFF_TBL           0x0010u        /* 4 x 36 words: [0..15] {Vproc<<16 | MHz}, [16..31] {clk_div<<16 | post_div}, [32] = 0xffffffff, [33..35] = 0 */
#define N2DV_OFF_TBL_END       0x0250u
#define N2DV_OFF_DATA          0x02a0u        /* firmware/driver data, zeroed by init */
#define N2DV_OFF_SCHED_DIS     0x02bcu
#define N2DV_OFF_PPM_LIMIT     0x0320u        /* + 4*cluster: (limit_row << 16) | base_row */
#define N2DV_OFF_CUR_VPROC     0x032cu
#define N2DV_OFF_CUR_FREQ      0x0354u        /* + 4*cluster: current row, written by the firmware */
#define N2DV_OFF_REQ           0x037cu        /* + 4*cluster: requested row, written by the AP (OFFS_WFI_S) */
#define N2DV_OFF_CCI_MAP       0x0fa0u
#define N2DV_CCI_MAP_ZERO      0x01fcu        /* memset_io size of the vendor code */
#define N2DV_CCI_MAP_BYTES     512u           /* 32 x 16 bytes stored */
#define N2DV_OFF_CLUSTER_ONOFF 0x1224u        /* + 4*cluster: 'CCON' / 'CCOF' */
#define N2DV_GUARD0            0x55aa55aau
#define N2DV_GUARD1            0xaa55aa55u
#define N2DV_CLUSTER_ON        0x43434f4eu
#define N2DV_CLUSTER_OFF       0x43434f46u

/* IPI id 1 (mailbox 1), 4-word messages {cmd, arg0, arg1, arg2}; numbering of the build with CONFIG_MTK_CPU_MSSV (confirmed in the stock Image) */
#define N2DV_IPI_WORDS         4u
#define N2DV_CMD_INIT_PTBL     0u
#define N2DV_CMD_INIT          1u
#define N2DV_CMD_CLUSTER_ONOFF 2u            /* unused: ENABLE_CLUSTER_ONOFF_SRAM stores the magic instead */
#define N2DV_CMD_SET_FREQ      4u            /* debug path only */
#define N2DV_CMD_INIT_VOLT     10u
/* Limits for the init voltages, 10 uV units.  The stock probe (mtk_cpufreq_main.c, _mt_cpufreq_pdrv_probe) checks ONLY Vsram >= Vproc and Vsram - Vproc <=
 * MAX_DIFF_VSRAM_VPROC (25000 = 250 mV) on the rails it reads.  The platform MIN_VPROC_VOLT 60000 / MIN_VSRAM_VOLT 85000 are floors for voltages the AP itself SETS
 * (non-hybrid path); they do not apply to what the bootloader left: device reading P18 (c6t): L at its 700 MHz row had Vproc 650.00 mV and Vsram 750.00 mV, the
 * hardware-tracked Vsram = Vproc + NORMAL_DIFF_VRSAM_VPROC (100 mV) below the 850 mV floor.  Here the floors only reject a garbage read: the PMIC ranges start at
 * 500 mV (Vsram LDO) and the lowest table row is 650 mV. */
#define N2DV_MIN_VPROC         50000u
#define N2DV_MAX_VPROC         112000u
#define N2DV_MIN_VSRAM         50000u
#define N2DV_MAX_VSRAM         112000u
#define N2DV_MAX_VSRAM_DIFF    25000u
#define N2DV_ONLY_LEVEL        1u            /* the only level whose tables are embedded (FY_30G26G / CCI_30G26G): this phone */

struct n2dv_ops {
	void (*w32)(void *ctx, uint32_t off, uint32_t value);
};

/* pin = 1: instead of the full-range defaults (limit 0 = fastest, base 15 = slowest) write min = max = row[c] for LL, L, B, and write the matching
 * REQUEST words (including the CCI request taken from the vendor CCI map) so that the firmware's first decision keeps every cluster on the row the
 * bootloader left; zeroing the request words alone would mean "row 0 = fastest". */
struct n2dv_plan {
	unsigned level;
	bool pin;
	uint8_t row[3];
};

/* The level of this chip from the efuse words (`_mt_cpufreq_get_cpu_level` in the stock Image): devinfo[7] low byte 0x10 -> 0, 0x40 -> 1, else bit 13 of devinfo[0x84]. */
static inline unsigned
n2dv_level_from_devinfo(uint32_t devinfo7, uint32_t devinfo84)
{
	if ((devinfo7 & 0xffu) == 0x10u) {
		return 0;
	}
	if ((devinfo7 & 0xffu) == 0x40u) {
		return 1;
	}
	return (devinfo84 >> 13) & 1u;
}

/* Fastest row of LL/L/B whose frequency does not exceed `mhz` by more than 1 %; -1 if every row is faster (or the cluster is not LL/L/B). */
static inline int
n2dv_row_for_mhz(unsigned cluster, unsigned mhz)
{
	if (cluster > N2DV_B) {
		return -1;
	}
	for (unsigned i = 0; i < N2DV_NR_FREQ; i++) {
		if ((uint64_t)n2dv_l1_pvt[N2DV_NR_FREQ * cluster + i][0] * 100u <= (uint64_t)mhz * 101u) {
			return (int)i;
		}
	}
	return -1;
}

static inline bool
n2dv_plan_ok(const struct n2dv_plan *p)
{
	if (p == 0 || p->level != N2DV_ONLY_LEVEL) {
		return false;
	}
	if (p->pin) {
		for (unsigned c = 0; c < 3; c++) {
			if (p->row[c] >= N2DV_NR_FREQ) {
				return false;
			}
		}
	}
	return true;
}

/* CCI row for given LL and B rows (vendor map, first half, [LL row][B row]); the choice for the pinned CCI request is an ASSUMPTION, see README 12.7. */
static inline unsigned
n2dv_cci_row(unsigned ll_row, unsigned b_row)
{
	return n2dv_l1_cci[ll_row & 15u][b_row & 15u];
}

/* word of the PVT table region, index k = 0..143 */
static inline uint32_t
n2dv_pvt_word(unsigned k)
{
	unsigned c = k / 36u, i = k % 36u;
	if (c >= N2DV_NR_CLUSTERS) {
		return 0;
	}
	if (i < N2DV_NR_FREQ) {
		const uint16_t *r = n2dv_l1_pvt[N2DV_NR_FREQ * c + i];
		return ((uint32_t)(r[1] & 0xfffu) << 16) | (uint32_t)r[0];
	}
	if (i < 2 * N2DV_NR_FREQ) {
		const uint16_t *r = n2dv_l1_pvt[N2DV_NR_FREQ * c + i - N2DV_NR_FREQ];
		return ((uint32_t)(r[3] & (c == N2DV_B ? 0xfffu : 0xffu)) << 16) | (uint32_t)(r[2] & 0xffu);
	}
	return i == 32 ? 0xffffffffu : 0;
}

/* Stores of the AP-side CSRAM initialisation, in the vendor order, 32-bit stores only.  Returns 0, or -1 (nothing written) for a plan that is not allowed. */
static inline int
n2dv_csram_init(const struct n2dv_ops *ops, void *ctx, const struct n2dv_plan *plan)
{
	if (ops == 0 || ops->w32 == 0 || !n2dv_plan_ok(plan)) {
		return -1;
	}
	/* init_cpuhvfs_debug_repo */
	ops->w32(ctx, 0x0, N2DV_GUARD0);
	ops->w32(ctx, 0x4, N2DV_GUARD1);
	ops->w32(ctx, 0x8, N2DV_GUARD0);
	ops->w32(ctx, 0xc, N2DV_GUARD1);
	/* The vendor zero loop clears the limit and request windows too.  In the pinned plan those words are stored ONCE, with the pinned value, inside the loop: the
	 * request window never holds 0 (= the fastest row) even for a moment, whatever the firmware is doing. */
	for (uint32_t off = N2DV_OFF_DATA; off < N2DV_CSRAM_SIZE; off += 4) {
		uint32_t v = 0;
		if (plan->pin && off >= N2DV_OFF_PPM_LIMIT && off < N2DV_OFF_PPM_LIMIT + 12) {
			unsigned c = (off - N2DV_OFF_PPM_LIMIT) / 4;
			v = (uint32_t)plan->row[c] << 16 | plan->row[c];
		} else if (plan->pin && off >= N2DV_OFF_REQ && off < N2DV_OFF_REQ + 16) {
			unsigned c = (off - N2DV_OFF_REQ) / 4;
			v = c < 3 ? plan->row[c] : n2dv_cci_row(plan->row[N2DV_LL], plan->row[N2DV_B]);
		} else if (!plan->pin && off >= N2DV_OFF_PPM_LIMIT && off < N2DV_OFF_PPM_LIMIT + 12) {
			v = 0u << 16 | (N2DV_NR_FREQ - 1u);                                              /* default plan: full range, as the vendor driver */
		}
		ops->w32(ctx, off, v);
	}
	ops->w32(ctx, N2DV_OFF_DATA, N2DV_GUARD0);
	/* cpuhvfs_pvt_tbl_create */
	for (unsigned k = 0; k < (N2DV_OFF_TBL_END - N2DV_OFF_TBL) / 4u; k++) {
		ops->w32(ctx, N2DV_OFF_TBL + 4 * k, n2dv_pvt_word(k));
	}
	/* CCI map: memset 0x1fc bytes, then 512 bytes of table; one store per word, same final bytes */
	for (unsigned k = 0; k < N2DV_CCI_MAP_BYTES / 4u; k++) {
		const uint8_t *b = &n2dv_l1_cci[0][0] + 4 * k;
		ops->w32(ctx, N2DV_OFF_CCI_MAP + 4 * k, (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
	}
	return 0;
}

/* after cmd 1 (cpuhvfs_set_sched_dvfs_disable(1)) and per online cluster (cpuhvfs_set_cluster_on_off) */
static inline void
n2dv_sched_disable(const struct n2dv_ops *ops, void *ctx)
{
	ops->w32(ctx, N2DV_OFF_SCHED_DIS, 1);
}

static inline void
n2dv_cluster_on(const struct n2dv_ops *ops, void *ctx, unsigned cluster)
{
	if (cluster < N2DV_NR_CLUSTERS) {
		ops->w32(ctx, N2DV_OFF_CLUSTER_ONOFF + 4 * cluster, N2DV_CLUSTER_ON);
	}
}

/* the fallback after ANY failure once an init message was sent: pin limits and requests to the rows and stop (nothing is undone, nothing is sent again) */
static inline void
n2dv_fallback_pin(const struct n2dv_ops *ops, void *ctx, const uint8_t row[3])
{
	for (unsigned c = 0; c < 3; c++) {
		ops->w32(ctx, N2DV_OFF_PPM_LIMIT + 4 * c, (uint32_t)row[c] << 16 | row[c]);
		ops->w32(ctx, N2DV_OFF_REQ + 4 * c, row[c]);
	}
}

/* 4-word messages */
static inline void
n2dv_msg_init_ptbl(uint32_t m[N2DV_IPI_WORDS])
{
	m[0] = N2DV_CMD_INIT_PTBL;
	m[1] = 0;                                              /* segment code 0 */
	m[2] = m[3] = 0;                                       /* uninitialised stack words in the stock driver */
}

static inline void
n2dv_msg_init_sta(uint32_t m[N2DV_IPI_WORDS], unsigned level)
{
	m[0] = N2DV_CMD_INIT;
	m[1] = level;
	m[2] = m[3] = 0;
}

/* The stock probe's own rail check plus sanity bounds: Vsram >= Vproc, Vsram - Vproc <= 250 mV, both inside [500 mV, 1120 mV]. */
static inline bool
n2dv_rails_ok(uint32_t vproc_10uv, uint32_t vsram_10uv)
{
	return vproc_10uv >= N2DV_MIN_VPROC && vproc_10uv <= N2DV_MAX_VPROC && vsram_10uv >= N2DV_MIN_VSRAM && vsram_10uv <= N2DV_MAX_VSRAM && vsram_10uv >= vproc_10uv &&
	       vsram_10uv - vproc_10uv <= N2DV_MAX_VSRAM_DIFF;
}

/* cmd 10: {10, cluster, Vproc, Vsram}, the rail voltages in 10 uV units (µV / 10) as the stock driver reads them from the regulators.  false (message not built)
 * for a cluster that does not exist or rails that fail n2dv_rails_ok. */
static inline bool
n2dv_msg_init_volt(uint32_t m[N2DV_IPI_WORDS], unsigned cluster, uint32_t vproc_10uv, uint32_t vsram_10uv)
{
	if (cluster >= N2DV_NR_CLUSTERS || !n2dv_rails_ok(vproc_10uv, vsram_10uv)) {
		return false;
	}
	m[0] = N2DV_CMD_INIT_VOLT;
	m[1] = cluster;
	m[2] = vproc_10uv;
	m[3] = vsram_10uv;
	return true;
}

/* PMIC readback (the registers the stock regulators read; bucks and LDOs share the 6.25 mV step):
 *   LL and CCI: Vproc2 = MT6315 SID 7 VBUCK3 DA_VOSEL (0x1599, 8 bit),  Vsram2 = MT6359P VSRAM_PROC2 DA_VOSEL (VOSEL1 0x1eb0 [14:8])
 *   L and B:    Vproc1 = MT6315 SID 6 VBUCK1 DA_VOSEL (0x1499, 8 bit),  Vsram1 = MT6359P VSRAM_PROC1 DA_VOSEL (VOSEL1 0x1e90 [14:8])
 * vproc: 300000 uV + (sel - 0x30) * 6250 for sel 0x30..0xbf; vsram: 500000 uV + sel * 6250 for sel 0..0x6f; results in 10 uV units, 0 = invalid selector. */
struct n2dv_rail_regs {
	uint8_t vproc_sid;
	uint16_t vproc_reg;
	uint16_t vsram_reg;
};

static inline struct n2dv_rail_regs
n2dv_cluster_rails(unsigned cluster)
{
	struct n2dv_rail_regs r;
	if (cluster == N2DV_LL || cluster == N2DV_CCI) {
		r.vproc_sid = 7; r.vproc_reg = 0x1599; r.vsram_reg = 0x1eb0;
	} else {
		r.vproc_sid = 6; r.vproc_reg = 0x1499; r.vsram_reg = 0x1e90;
	}
	return r;
}

static inline uint32_t
n2dv_vproc_10uv(unsigned sel)
{
	return (sel >= 0x30u && sel <= 0xbfu) ? 30000u + (sel - 0x30u) * 625u : 0;
}

static inline uint32_t
n2dv_vsram_10uv(unsigned sel)
{
	return sel <= 0x6fu ? 50000u + sel * 625u : 0;
}
#endif
