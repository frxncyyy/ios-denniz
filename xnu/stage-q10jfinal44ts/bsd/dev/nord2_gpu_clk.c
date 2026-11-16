/*
 * Nord 2 MT6893 GPU clock gates and mux.  See nord2_gpu_clk.h.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_clk.h"

#include "nord2_gpu_rail.h"
#include "nord2_gpu_compat.h"

static uint32_t rd(struct n2clk *c, enum n2clk_block b, uint32_t off)
{
        return c->ops.read(c->ctx, b, off);
}

static void wr(struct n2clk *c, enum n2clk_block b, uint32_t off, uint32_t v)
{
        c->ops.write(c->ctx, b, off, v);
}

static void note(struct n2clk *c, const char *msg)
{
        if (c->ops.log)
                c->ops.log(c->ctx, msg);
}

bool n2clk_init(struct n2clk *c, const struct n2clk_ops *ops, void *ctx)
{
        if (!c || !ops || !ops->read || !ops->write || !ops->udelay)
                return false;
        memset(c, 0, sizeof(*c));
        c->ops = *ops;
        c->ctx = ctx;
        return true;
}

enum n2clk_error n2clk_preflight(struct n2clk *c, struct n2clk_snapshot *s)
{
        if (!c || !s)
                return N2CLK_E_ARGS;
        if (c->mux_on || c->cg_on || c->pll_changed)
                return N2CLK_E_STATE;
        memset(s, 0, sizeof(*s));
        s->clk_cfg_5 = rd(c, N2CLK_TOPCK, N2CLK_CFG_5);
        s->mfgpll_con0 = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON0);
        s->mfgpll_con1 = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON1);
        s->mfgpll_con3 = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON3);
        s->khz = n2rail_mfgpll_khz(s->mfgpll_con1);
        c->before = *s;
        /* mtk_pll_is_prepared(): CON0 bit0; mtk_pll_prepare() leaves
         * CON3 PWR_ON set and ISO clear. */
        if (!(s->mfgpll_con0 & 1u) || !(s->mfgpll_con3 & 1u) ||
            (s->mfgpll_con3 & 2u))
                return N2CLK_E_PLL_OFF;
        uint32_t posdiv = (s->mfgpll_con1 >> 24) & 7u;
        /* The vendor table only uses /4 and /8 (POSDIV_POWER_4/_8). */
        struct n2rail_point pt;
        if ((posdiv != 2 && posdiv != 3) ||
            !n2rail_point_for_khz(s->khz, &pt) ||
            s->khz < n2rail_opp_table[N2RAIL_OPP_COUNT - 1].khz)
                return N2CLK_E_PLL_RANGE;
        c->before = *s;
        return N2CLK_OK;
}

static uint32_t mux_parent(uint32_t cfg)
{
        return (cfg >> N2CLK_MFG_MUX_SHIFT) & N2CLK_MFG_MUX_MASK;
}

/* Masked SET/CLR aliases preserve the other CLK_CFG_5 consumers. */
static bool set_parent(struct n2clk *c, uint32_t parent)
{
        if (mux_parent(rd(c, N2CLK_TOPCK, N2CLK_CFG_5)) != parent) {
                wr(c, N2CLK_TOPCK, N2CLK_CFG_5_CLR,
                   N2CLK_MFG_MUX_MASK << N2CLK_MFG_MUX_SHIFT);
                wr(c, N2CLK_TOPCK, N2CLK_CFG_5_SET,
                   parent << N2CLK_MFG_MUX_SHIFT);
                wr(c, N2CLK_TOPCK, N2CLK_CFG_UPDATE, 1u << N2CLK_MFG_UPD_BIT);
        }
        return mux_parent(rd(c, N2CLK_TOPCK, N2CLK_CFG_5)) == parent;
}

static bool set_gate(struct n2clk *c, bool gated)
{
        wr(c, N2CLK_TOPCK, gated ? N2CLK_CFG_5_SET : N2CLK_CFG_5_CLR,
           1u << N2CLK_MFG_PDN_BIT);
        return !!(rd(c, N2CLK_TOPCK, N2CLK_CFG_5) &
                  (1u << N2CLK_MFG_PDN_BIT)) == gated;
}

static bool pll_update(struct n2clk *c, uint32_t off, uint32_t mask, uint32_t bits)
{
        uint32_t old = rd(c, N2CLK_APMIXED, off);
        if ((old & mask) != (bits & mask))
                wr(c, N2CLK_APMIXED, off, (old & ~mask) | (bits & mask));
        return (rd(c, N2CLK_APMIXED, off) & mask) == (bits & mask);
}

static bool pll_rate(struct n2clk *c, uint32_t con1)
{
        /* PCW_CHG is a trigger; do not require it to remain latched. */
        wr(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON1, con1 | 0x80000000u);
        c->ops.udelay(c->ctx, 20);
        return (rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON1) & 0x7fffffffu) ==
               (con1 & 0x7fffffffu);
}

static void capture_clock(struct n2clk *c, struct n2clk_snapshot *s)
{
        s->clk_cfg_5 = rd(c, N2CLK_TOPCK, N2CLK_CFG_5);
        s->mfgpll_con0 = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON0);
        s->mfgpll_con1 = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON1);
        s->mfgpll_con3 = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON3);
        s->khz = n2rail_mfgpll_khz(s->mfgpll_con1);
}

enum n2clk_error n2clk_mux_on(struct n2clk *c, bool normalize)
{
        if (!c)
                return N2CLK_E_ARGS;
        if (c->mux_on || c->pll_changed)
                return N2CLK_E_STATE;
        enum n2clk_error result = N2CLK_E_VERIFY;
        uint32_t cfg = rd(c, N2CLK_TOPCK, N2CLK_CFG_5);
        c->mux_orig_parent = mux_parent(cfg);
        c->mux_ungated_by_us = !!(cfg & (1u << N2CLK_MFG_PDN_BIT));
        c->mux_parent_changed = c->mux_orig_parent != N2CLK_MFG_PARENT_MFGPLL;
        c->mux_on = true; /* ownership before the first potentially failing write */
        if (normalize) {
                /* Match the captured PLL before claiming it; no other owner may
                 * change it between read-only preflight and power-on. */
                struct n2clk_snapshot now;
                capture_clock(c, &now);
                if (now.mfgpll_con0 != c->before.mfgpll_con0 ||
                    ((now.mfgpll_con1 ^ c->before.mfgpll_con1) & 0x7fffffffu) ||
                    now.mfgpll_con3 != c->before.mfgpll_con3)
                        { result = N2CLK_E_STATE; goto done; }
                c->mux_parent_changed = true;
                if (!set_parent(c, N2CLK_MFG_PARENT_PARK) || !set_gate(c, false))
                        goto done;
                c->pll_changed = true;
                /* mtk_pll_prepare(): power, de-isolate, enable, settle.
                 * MFGPLL has no tuner or HAVE_RST_BAR in clk-mt6885.c. */
                if (!pll_update(c, N2CLK_MFGPLL_CON3, 1u, 1u))
                        goto done;
                c->ops.udelay(c->ctx, 1);
                if (!pll_update(c, N2CLK_MFGPLL_CON3, 2u, 0))
                        goto done;
                c->ops.udelay(c->ctx, 1);
                if (!pll_update(c, N2CLK_MFGPLL_CON0, 1u, 1u))
                        goto done;
                c->ops.udelay(c->ctx, 20);
                /* Unmodified Android __mt_gpufreq_calculate_dds arithmetic. */
                uint32_t khz = c->target_khz ? c->target_khz : N2CLK_BOOT_KHZ;
                uint32_t posdiv = c->target_khz ? c->target_posdiv : N2CLK_BOOT_POSDIV;
                uint32_t dds = (((khz / 100u * (1u << posdiv)) << 14) / 26u + 5u) / 10u;
                uint32_t con1 = (posdiv << 24) | dds;
                if (!pll_rate(c, con1) ||
                    n2rail_mfgpll_khz(rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON1)) != khz)
                        goto done;
        }
        if (!set_parent(c, N2CLK_MFG_PARENT_MFGPLL) || !set_gate(c, false)) {
                note(c, "Nord2 GPU clk: mfg_sel did not take effect");
                goto done;
        }
        result = N2CLK_OK;
done:
        capture_clock(c, &c->after);
        return result;
}

enum n2clk_error n2clk_mux_off(struct n2clk *c)
{
        if (!c)
                return N2CLK_E_ARGS;
        if (!c->mux_on)
                return N2CLK_OK;
        if (c->pll_changed) {
                /* Caller verified all MFG domains off. Park before restoring
                 * the original rate, which may be outside our allowed OPPs. */
                if (!set_parent(c, N2CLK_MFG_PARENT_PARK) ||
                    !pll_rate(c, c->before.mfgpll_con1))
                        return N2CLK_E_VERIFY;
                if (!pll_update(c, N2CLK_MFGPLL_CON0, 1u, c->before.mfgpll_con0))
                        return N2CLK_E_VERIFY;
                /* Restore ISO before PWR, as in mtk_pll_unprepare(). */
                if (!pll_update(c, N2CLK_MFGPLL_CON3, 2u, c->before.mfgpll_con3) ||
                    !pll_update(c, N2CLK_MFGPLL_CON3, 1u, c->before.mfgpll_con3))
                        return N2CLK_E_VERIFY;
                c->ops.udelay(c->ctx, 20);
                c->pll_changed = false;
        }
        if (c->mux_parent_changed && !set_parent(c, c->mux_orig_parent))
                return N2CLK_E_VERIFY;
        if (c->mux_ungated_by_us && !set_gate(c, true))
                return N2CLK_E_VERIFY;
        c->mux_parent_changed = c->mux_ungated_by_us = c->mux_on = false;
        return N2CLK_OK;
}

enum n2clk_error n2clk_retune(struct n2clk *c, uint32_t khz, uint32_t posdiv)
{
        if (!c)
                return N2CLK_E_ARGS;
        if (!c->mux_on || !c->cg_on)
                return N2CLK_E_STATE;
        unsigned opp = 0;
        if (!n2rail_opp_index_exact(khz, &opp) ||
            n2rail_opp_table[opp].posdiv_power != posdiv)
                return N2CLK_E_PLL_RANGE;
        uint32_t dds = (((khz / 100u * (1u << posdiv)) << 14) / 26u + 5u) / 10u;
        uint32_t con1 = (posdiv << 24) | dds;
        if (n2rail_mfgpll_khz(con1) != khz)
                return N2CLK_E_PLL_RANGE;
        uint32_t cfg = rd(c, N2CLK_TOPCK, N2CLK_CFG_5);
        uint32_t con3 = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON3);
        uint32_t old = rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON1) & 0x7fffffffu;
        if (mux_parent(cfg) != N2CLK_MFG_PARENT_MFGPLL ||
            (cfg & (1u << N2CLK_MFG_PDN_BIT)) ||
            !(rd(c, N2CLK_APMIXED, N2CLK_MFGPLL_CON0) & 1u) ||
            !(con3 & 1u) || (con3 & 2u) ||
            old != (c->after.mfgpll_con1 & 0x7fffffffu))
                return N2CLK_E_STATE;
        if (con1 == old)
                return N2CLK_OK;
        c->retune_step = 1;
        if (!set_parent(c, N2CLK_MFG_PARENT_PARK)) {
                (void)set_parent(c, N2CLK_MFG_PARENT_MFGPLL);
                return N2CLK_E_VERIFY;
        }
        c->pll_changed = true;  /* mux_off restores the boot chain's rate */
        c->retune_step = 2;
        bool ok = pll_rate(c, con1);
        if (!ok) {
                c->retune_step = 3;
                (void)pll_rate(c, old);
        }
        c->retune_step = 4;
        if (!set_parent(c, N2CLK_MFG_PARENT_MFGPLL))
                return N2CLK_E_VERIFY;
        c->retune_step = 0;
        capture_clock(c, &c->after);
        return ok ? N2CLK_OK : N2CLK_E_VERIFY;
}

static void rmw(struct n2clk *c, uint32_t off, uint32_t set, uint32_t clear)
{
        uint32_t v = rd(c, N2CLK_MFGCFG, off);
        wr(c, N2CLK_MFGCFG, off, (v | set) & ~clear);
}

enum n2clk_error n2clk_mfg_cg_on(struct n2clk *c)
{
        if (!c)
                return N2CLK_E_ARGS;
        if (c->cg_on || !c->mux_on)
                return N2CLK_E_STATE;
        /* mt_gpufreq_cg_control(POWER_ON): clk_prepare_enable(BG3D) then
         * external CG.  mtk_clk_gate_ops_setclr enable = write CLR. */
        c->cg_ungated_by_us = !!(rd(c, N2CLK_MFGCFG, N2CLK_MFG_CG_STA) & 1u);
        if (c->cg_ungated_by_us)
                wr(c, N2CLK_MFGCFG, N2CLK_MFG_CG_CLR, 1u);
        if (rd(c, N2CLK_MFGCFG, N2CLK_MFG_CG_STA) & 1u) {
                note(c, "Nord2 GPU clk: BG3D gate still closed");
                return N2CLK_E_VERIFY;
        }
        c->cg_on = true;
        /* mt_gpufreq_external_cg_control(), source order. */
        rmw(c, N2CLK_MFG_ASYNC_CON, (1u << 22) | (1u << 23) | (1u << 24) |
            (1u << 25), 0);
        rmw(c, N2CLK_MFG_ASYNC_CON_3, (1u << 12) | (1u << 13) | (1u << 14) |
            (1u << 15), 0);
        rmw(c, N2CLK_MFG_GLOBAL_CON, 0, (1u << 10) | (1u << 9));
        rmw(c, N2CLK_MFG_QCHANNEL_CON, 1u << 4, 0);
        rmw(c, N2CLK_MFG_GLOBAL_CON, 0, (1u << 19) | (1u << 8));
        rmw(c, N2CLK_MFG_ASYNC_CON_1, 1u << 0, 0);
        /* pm_callback_power_on_nolock(): mt_gpufreq_set_timestamp(). */
        wr(c, N2CLK_MFGCFG, N2CLK_MFG_TIMESTAMP, 0x3u);
        return N2CLK_OK;
}

void n2clk_mfg_cg_off(struct n2clk *c)
{
        if (!c || !c->cg_on)
                return;
        /* clk_disable_unprepare(subsys_mfg_cg): write SET. */
        if (c->cg_ungated_by_us)
                wr(c, N2CLK_MFGCFG, N2CLK_MFG_CG_SET, 1u);
        c->cg_ungated_by_us = false;
        c->cg_on = false;
}

enum n2clk_error n2clk_bus_idle(struct n2clk *c)
{
        if (!c || !c->cg_on)
                return N2CLK_E_STATE;
        /* mt_gpufreq_check_bus_idle(): the vendor writes these whole words
         * and polls without a bound; this copy is bounded. */
        wr(c, N2CLK_MFGCFG, N2CLK_MFG_QCHANNEL_CON, 0x1u);
        wr(c, N2CLK_MFGCFG, N2CLK_MFG_DEBUG_SEL, 0x3u);
        for (unsigned waited = 0; waited < N2CLK_BUS_IDLE_TIMEOUT_US;
             waited += 10) {
                if (rd(c, N2CLK_MFGCFG, N2CLK_MFG_DEBUG_TOP) & 0x4u)
                        return N2CLK_OK;
                c->ops.udelay(c->ctx, 10);
        }
        note(c, "Nord2 GPU clk: MFG bus not idle");
        return N2CLK_E_BUS_BUSY;
}

const char *n2clk_error_string(enum n2clk_error e)
{
        switch (e) {
        case N2CLK_OK: return "ok";
        case N2CLK_E_ARGS: return "invalid arguments";
        case N2CLK_E_PLL_OFF: return "MFGPLL not running";
        case N2CLK_E_PLL_RANGE: return "MFGPLL outside vendor table";
        case N2CLK_E_VERIFY: return "clock readback mismatch";
        case N2CLK_E_STATE: return "invalid clock state";
        case N2CLK_E_BUS_BUSY: return "MFG bus busy";
        }
        return "unknown";
}
