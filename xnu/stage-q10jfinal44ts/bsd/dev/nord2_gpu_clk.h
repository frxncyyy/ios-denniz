/*
 * Nord 2 MT6893 GPU clocks and cold-start normalization.
 *
 * Sources (Android 04d00c2565764c3922c9b4826b5acff1106e533f):
 *   drivers/clk/mediatek/clk-mt6885.c (CONFIG_COMMON_CLK_MT6893):
 *     mfg_sel = MUX_GATE_CLR_SET_UPD(CLK_CFG_5 0x60/SET 0x64/CLR 0x68,
 *       shift 8 width 2, pdn 15, CLK_CFG_UPDATE 0x04 bit 21), parents
 *       {tck_26m, mfgpll_ck, univpll_d6, mainpll_d5_d2};
 *     MFGPLL in APMIXED: CON0 0x268 (EN bit0), CON1 0x26c (PCW, posdiv
 *       [26:24]), CON3 0x274 (PWR_ON bit0, ISO bit1).
 *   drivers/clk/mediatek/clk-mux.c, clk-gate.c: set/clr/upd semantics.
 *   drivers/clk/mediatek/clk-mt6885-mfgcfg.c: BG3D gate bit0 in
 *     MFGCFG (0x13fbf000) STA 0x0 / SET 0x4 / CLR 0x8, parent mfg_ref_ck.
 *   drivers/clk/mediatek/clk-mt6885-pg.c: PG_MFG0's parent is mfg_sel, so
 *     the mux gate must be open before MFG0 powers on.
 *   gpufreq/v1/mt6893/mtk_gpufreq_plat.c: mt_gpufreq_external_cg_control,
 *     mt_gpufreq_set_timestamp, mt_gpufreq_check_bus_idle.
 *
 * A valid inherited OPP is preserved. With every MFG domain confirmed off,
 * board code can normalize an unusable inherited PLL to the vendor minimum
 * 350 MHz /8 OPP. It uses Android's DDS formula, 20us settle and clock parking
 * protocol; mt6893.dts selects mainpll_d5_d2 (parent 3), NOT the stale
 * univpll_d3 comment in gpufreq. Rails must already cover the target OPP.
 * PLL preparation follows clk-pll.c; original PLL/mux state is restored
 * only after MFG domains are off. No live DVFS or frequency hopping.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_CLK_H
#define NORD2_GPU_CLK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2CLK_TOPCK_BASE 0x10000000u
#define N2CLK_APMIXED_BASE 0x1000c000u
#define N2CLK_MFGCFG_BASE 0x13fbf000u

#define N2CLK_CFG_UPDATE 0x004u
#define N2CLK_CFG_5 0x060u
#define N2CLK_CFG_5_SET 0x064u
#define N2CLK_CFG_5_CLR 0x068u
#define N2CLK_MFG_MUX_SHIFT 8u
#define N2CLK_MFG_MUX_MASK 0x3u
#define N2CLK_MFG_PDN_BIT 15u
#define N2CLK_MFG_UPD_BIT 21u
#define N2CLK_MFG_PARENT_MFGPLL 1u
#define N2CLK_MFG_PARENT_PARK 3u
#define N2CLK_BOOT_KHZ 350000u
#define N2CLK_BOOT_POSDIV 3u

#define N2CLK_MFGPLL_CON0 0x268u
#define N2CLK_MFGPLL_CON1 0x26cu
#define N2CLK_MFGPLL_CON3 0x274u

#define N2CLK_MFG_CG_STA 0x000u
#define N2CLK_MFG_CG_SET 0x004u
#define N2CLK_MFG_CG_CLR 0x008u
#define N2CLK_MFG_ASYNC_CON 0x020u
#define N2CLK_MFG_ASYNC_CON_1 0x024u
#define N2CLK_MFG_ASYNC_CON_3 0x02cu
#define N2CLK_MFG_GLOBAL_CON 0x0b0u
#define N2CLK_MFG_QCHANNEL_CON 0x0b4u
#define N2CLK_MFG_TIMESTAMP 0x130u
#define N2CLK_MFG_DEBUG_SEL 0x170u
#define N2CLK_MFG_DEBUG_TOP 0x178u
#define N2CLK_BUS_IDLE_TIMEOUT_US 10000u

enum n2clk_block { N2CLK_TOPCK = 0, N2CLK_APMIXED = 1, N2CLK_MFGCFG = 2 };

struct n2clk_ops {
        uint32_t (*read)(void *ctx, enum n2clk_block b, uint32_t off);
        void (*write)(void *ctx, enum n2clk_block b, uint32_t off,
                      uint32_t value);
        void (*udelay)(void *ctx, unsigned us);
        void (*log)(void *ctx, const char *msg);
};

enum n2clk_error {
        N2CLK_OK = 0,
        N2CLK_E_ARGS,
        N2CLK_E_PLL_OFF,       /* MFGPLL not enabled/powered by boot chain */
        N2CLK_E_PLL_RANGE,     /* frequency/posdiv outside the vendor table */
        N2CLK_E_VERIFY,        /* readback disagrees after a write */
        N2CLK_E_STATE,
        N2CLK_E_BUS_BUSY,      /* MFG bus not idle before power-off */
};

struct n2clk_snapshot {
        uint32_t clk_cfg_5;
        uint32_t mfgpll_con0, mfgpll_con1, mfgpll_con3;
        uint32_t khz;
};

struct n2clk {
        struct n2clk_ops ops;
        void *ctx;
        /* Cold-start target (normalize only). 0 = N2CLK_BOOT_KHZ / N2CLK_BOOT_POSDIV;
         * otherwise an exact vendor OPP and its posdiv, validated by the board. */
        uint32_t target_khz, target_posdiv;
        struct n2clk_snapshot before;
        bool mux_on;
        bool mux_ungated_by_us;
        bool mux_parent_changed;
        uint32_t mux_orig_parent;
        bool cg_on;
        bool cg_ungated_by_us;
        bool pll_changed;
        struct n2clk_snapshot after;
        uint32_t retune_step;  /* 0461: last step of n2clk_retune (1 park, 2 PLL, 3 PLL back, 4 unpark) */
};

bool n2clk_init(struct n2clk *c, const struct n2clk_ops *ops, void *ctx);
/* Read-only: MFGPLL state/frequency and mux.  Needs no MFG power. */
enum n2clk_error n2clk_preflight(struct n2clk *c, struct n2clk_snapshot *s);
/* mfg_sel parent = mfgpll and gate open (before MTCMOS MFG0). */
/* normalize requires every MFG domain off and rails >= the 350MHz OPP. */
enum n2clk_error n2clk_mux_on(struct n2clk *c, bool normalize);
/* If PLL was changed, caller must first verify all MFG domains are off. */
enum n2clk_error n2clk_mux_off(struct n2clk *c);
/* BG3D gate, external CG and timestamp (after MTCMOS; MFGCFG powered). */
enum n2clk_error n2clk_mfg_cg_on(struct n2clk *c);
void n2clk_mfg_cg_off(struct n2clk *c);
/* 0461: live frequency change of a powered GPU, the vendor's __mt_gpufreq_clock_switch with parking (the path it uses whenever frequency
 * hopping is not available): mfg_sel -> mainpll_d5_d2 (218.4 MHz), MFGPLL_CON1 = PCW_CHG | posdiv | dds, 20 us, mfg_sel -> mfgpll.
 * khz must be an exact vendor OPP and the rails must already cover the HIGHER of the old and the new OPP (no voltage is touched here).
 * Nothing is written unless mux, gate and PLL are exactly as this module left them.  On a PLL read-back mismatch the old rate is
 * written back before the mux returns.  N2CLK_E_VERIFY with retune_step 4 means the GPU is left on the 218.4 MHz parking clock. */
enum n2clk_error n2clk_retune(struct n2clk *c, uint32_t khz, uint32_t posdiv);
/* mt_gpufreq_check_bus_idle(), bounded. */
enum n2clk_error n2clk_bus_idle(struct n2clk *c);
const char *n2clk_error_string(enum n2clk_error e);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_CLK_H */
