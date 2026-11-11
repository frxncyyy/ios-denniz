/*
 * Nord 2 board power sequencing.  See nord2_gpu_board.h for the vendor
 * order this file follows.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_board.h"

#include "nord2_gpu_compat.h"

#define SPM_PWR_STATUS 0x16cu
#define SPM_PWR_STATUS_2ND 0x170u
#define MFG_STA(d) (1u << (2u + (d)))
#define MFG_ALL 0x1fcu

bool nord2_gpu_board_config_valid(const struct nord2_gpu_board_config *cfg)
{
        if (!cfg)
                return false;
        if (cfg->reg_base != NORD2_GPU_REG_BASE ||
            cfg->reg_size != NORD2_GPU_REG_SIZE ||
            cfg->mfg_base != NORD2_GPU_MFG_BASE ||
            cfg->mfg_size != NORD2_GPU_MFG_SIZE)
                return false;
        if (cfg->irq[0] != NORD2_GPU_IRQ_GPU ||
            cfg->irq[1] != NORD2_GPU_IRQ_MMU ||
            cfg->irq[2] != NORD2_GPU_IRQ_JOB ||
            cfg->irq[3] != NORD2_GPU_IRQ_EVENT ||
            cfg->irq[4] != NORD2_GPU_IRQ_PWR)
                return false;
        return cfg->expected_arch_major == 9 && cfg->expected_arch_minor == 0;
}

static void note(struct nord2_gpu_board *b, const char *msg)
{
        if (b->log)
                b->log(b->ctx, msg);
}

static uint32_t mtcmos_state(struct nord2_gpu_board *b)
{
        const struct nord2_scpsys *s = b->scpsys;
        return s->ops.read_spm(s->ctx, SPM_PWR_STATUS) &
               s->ops.read_spm(s->ctx, SPM_PWR_STATUS_2ND) & MFG_ALL;
}

static bool mtcmos_all_off(struct nord2_gpu_board *b)
{
        const struct nord2_scpsys *s = b->scpsys;
        return !((s->ops.read_spm(s->ctx, SPM_PWR_STATUS) |
                  s->ops.read_spm(s->ctx, SPM_PWR_STATUS_2ND)) & MFG_ALL);
}

bool nord2_gpu_board_init(struct nord2_gpu_board *b, struct n2rail *rail,
                          struct n2clk *clk, struct nord2_scpsys *scpsys,
                          struct n2hw *hw,
                          void (*log)(void *ctx, const char *msg), void *ctx)
{
        if (!b || !rail || !clk || !scpsys || !hw)
                return false;
        memset(b, 0, sizeof(*b));
        b->rail = rail;
        b->clk = clk;
        b->scpsys = scpsys;
        b->hw = hw;
        b->log = log;
        b->ctx = ctx;
        return true;
}

bool nord2_gpu_board_preflight(struct nord2_gpu_board *b)
{
        if (!b || b->powered || b->uncertain)
                return false;
        struct nord2_gpu_board_report *r = &b->report;
        memset(r, 0, sizeof(*r));
        b->preflight_ok = false;
        r->stage = N2B_STAGE_PREFLIGHT;
        const struct nord2_scpsys *s = b->scpsys;
        if ((s->ops.read_spm(s->ctx, SPM_PWR_STATUS) ^
             s->ops.read_spm(s->ctx, SPM_PWR_STATUS_2ND)) & MFG_ALL) {
                r->error = N2CLK_E_STATE;
                note(b, "Nord2 GPU board: MFG power status disagrees");
                return false;
        }
        r->mtcmos_before = mtcmos_state(b);
        /* MFG1 without MFG0, or stacks without MFG1, is not a state the
         * vendor sequence can produce; do not build on it. */
        uint32_t m = r->mtcmos_before;
        if (((m & MFG_STA(1)) && !(m & MFG_STA(0))) ||
            ((m & 0x1f0u) && !(m & MFG_STA(1)))) {
                r->error = 1;
                note(b, "Nord2 GPU board: inconsistent MFG power state");
                return false;
        }
        enum n2clk_error ce = n2clk_preflight(b->clk, &r->clk_before);
        if (ce != N2CLK_OK) {
                /* Android init_volt_by_freq clamps bootloader rates to an
                 * OPP and reprograms MFGPLL. Start at its minimum OPP only
                 * when no MFG domain is powered; never retune a live GPU. */
                if ((ce != N2CLK_E_PLL_RANGE && ce != N2CLK_E_PLL_OFF) ||
                    m || !mtcmos_all_off(b)) {
                        r->error = (uint32_t)ce;
                        note(b, "Nord2 GPU board: live/invalid MFGPLL preflight refused");
                        return false;
                }
                r->normalize_pll = true;
        }
        struct n2rail_point pt;
        uint32_t want = N2CLK_BOOT_KHZ;
        b->clk->target_khz = b->clk->target_posdiv = 0;
        if (r->normalize_pll && b->target_khz && b->target_khz != N2CLK_BOOT_KHZ) {
                unsigned idx;
                if (!n2rail_opp_index_exact(b->target_khz, &idx)) {
                        r->error = N2RAIL_E_FREQ;
                        note(b, "Nord2 GPU board: requested OPP is not a vendor table entry");
                        return false;
                }
                want = b->target_khz;
                b->clk->target_khz = want;
                b->clk->target_posdiv = n2rail_opp_table[idx].posdiv_power;
        }
        if (!n2rail_point_for_khz(r->normalize_pll ? want : r->clk_before.khz, &pt)) {
                r->error = N2RAIL_E_FREQ;
                return false;
        }
        b->rail->point = pt;
        r->khz = pt.khz;
        r->vgpu_uv = pt.vgpu_uv;
        r->vsram_uv = pt.vsram_uv;
        r->opp_index = pt.index;
        enum n2rail_error re = n2rail_capture(b->rail, &r->rail_before);
        if (re != N2RAIL_OK) {
                r->error = (uint32_t)re;
                note(b, "Nord2 GPU board: PMIC readback failed");
                return false;
        }
        b->preflight_ok = true;
        return true;
}

static bool rollback(struct nord2_gpu_board *b)
{
        bool clean = true;
        if (b->cg_by_us) {
                n2clk_mfg_cg_off(b->clk);
                b->cg_by_us = false;
        }
        for (unsigned d = NORD2_GPU_MTCMOS_COUNT; d > 0; d--) {
                if (!b->mtcmos_by_us[d - 1])
                        continue;
                if (!nord2_scpsys_mfg_off(b->scpsys, d - 1))
                        clean = false;
                else
                        b->mtcmos_by_us[d - 1] = false;
        }
        if (b->mux_by_us && clean) {
                if ((b->clk->pll_changed && !mtcmos_all_off(b)) ||
                    n2clk_mux_off(b->clk) != N2CLK_OK)
                        clean = false;
                else
                        b->mux_by_us = false;
        }
        if (b->rails_by_us && clean) {
                if (n2rail_off(b->rail) != N2RAIL_OK)
                        clean = false;
                else
                        b->rails_by_us = false;
        }
        b->report.mtcmos_after = mtcmos_state(b);
        b->report.rollback_clean = clean;
        if (!(b->report.mtcmos_after & MFG_STA(1))) {
                /* MFG top is off: L2 and cores lost power with it. */
                b->hw->l2_on = false;
                b->hw->shaders_on = 0;
        }
        if (!clean) {
                /* Never cut rails/clocks under a domain that would not power
                 * off; leave everything for a manual recovery decision. */
                b->uncertain = true;
                note(b, "Nord2 GPU board: rollback incomplete; manual reset");
        }
        return clean;
}

static bool fail(struct nord2_gpu_board *b, uint32_t stage, uint32_t error,
                 const char *msg)
{
        b->report.stage = stage;
        b->report.error = error;
        note(b, msg);
        (void)rollback(b);
        return false;
}

bool nord2_gpu_board_power_on(struct nord2_gpu_board *b)
{
        if (!b || b->powered || b->uncertain || !b->preflight_ok)
                return false;
        /* One power-on attempt per preflight: a retry re-reads the state. */
        b->preflight_ok = false;
        struct nord2_gpu_board_report *r = &b->report;
        if (r->normalize_pll && !mtcmos_all_off(b)) {
                r->error = N2CLK_E_STATE;
                note(b, "Nord2 GPU board: MFG changed before cold PLL init");
                return false;
        }

        /* 1. Buck: VSRAM_GPU then VGPU. */
        r->stage = N2B_STAGE_RAILS;
        struct n2rail_point pt = b->rail->point;
        enum n2rail_error re = n2rail_on(b->rail, &pt);
        if (re != N2RAIL_OK) {
                if (b->rail->uncertain)
                        b->uncertain = true;
                return fail(b, N2B_STAGE_RAILS, (uint32_t)re,
                            "Nord2 GPU board: rail power-on refused");
        }
        b->rails_by_us = true;
        r->rail_after = b->rail->after;

        /* 2. PG_MFG0 parent: normalize a cold invalid PLL if needed, then
         * mfg_sel = mfgpll, gate open. Rails already cover the target rate. */
        r->stage = N2B_STAGE_MUX;
        if (r->normalize_pll && !mtcmos_all_off(b)) {
                /* A foreign domain appeared. Keep its supplies intact. */
                b->uncertain = true;
                r->error = N2CLK_E_STATE;
                note(b, "Nord2 GPU board: MFG changed during rail init; rails retained");
                return false;
        }
        b->mux_by_us = true;
        enum n2clk_error ce = n2clk_mux_on(b->clk, r->normalize_pll);
        r->clk_after = b->clk->after;
        if (ce != N2CLK_OK)
                return fail(b, N2B_STAGE_MUX, (uint32_t)ce,
                            "Nord2 GPU board: mfg_sel failed");

        /* 3. MTCMOS in clock-tree order: MFG0, MFG1, then the stacks of
         * MT_GPU_SHADER_PRESENT_9 (MFG2..MFG6). */
        r->stage = N2B_STAGE_MTCMOS;
        for (unsigned d = 0; d < NORD2_GPU_MTCMOS_COUNT; d++) {
                if (r->mtcmos_before & MFG_STA(d))
                        continue;
                b->mtcmos_by_us[d] = true;
                if (!nord2_scpsys_mfg_on(b->scpsys, d)) {
                        r->mtcmos_failed_domain = d;
                        return fail(b, N2B_STAGE_MTCMOS, d,
                                    "Nord2 GPU board: MTCMOS power-on failed");
                }
        }
        if (mtcmos_state(b) != MFG_ALL)
                return fail(b, N2B_STAGE_MTCMOS, 0xff,
                            "Nord2 GPU board: MFG status incomplete");

        /* 4. CG: BG3D, external CG, timestamp. */
        r->stage = N2B_STAGE_CG;
        ce = n2clk_mfg_cg_on(b->clk);
        if (ce != N2CLK_OK) {
                b->cg_by_us = b->clk->cg_on;
                return fail(b, N2B_STAGE_CG, (uint32_t)ce,
                            "Nord2 GPU board: BG3D gate failed");
        }
        b->cg_by_us = true;

        /* 5. kbase_pm_init_hw(): first GPU MMIO access happens only now. */
        r->stage = N2B_STAGE_RESET;
        enum n2hw_error he = n2hw_reset(b->hw);
        if (he != N2HW_OK)
                return fail(b, N2B_STAGE_RESET, (uint32_t)he,
                            "Nord2 GPU board: GPU reset failed");
        r->stage = N2B_STAGE_IDENTIFY;
        he = n2hw_identify(b->hw);
        r->gpu_id = b->hw->id.gpu_id;
        if (he != N2HW_OK)
                return fail(b, N2B_STAGE_IDENTIFY, (uint32_t)he,
                            "Nord2 GPU board: GPU identity refused");
        r->shader_present = b->hw->id.shader_present;
        r->stage = N2B_STAGE_SLOTS;
        he = n2hw_select_slots(b->hw);
        if (he != N2HW_OK)
                return fail(b, N2B_STAGE_SLOTS, (uint32_t)he,
                            "Nord2 GPU board: job slots refused");
        r->stage = N2B_STAGE_QUIRKS;
        he = n2hw_apply_quirks(b->hw);
        if (he != N2HW_OK)
                return fail(b, N2B_STAGE_QUIRKS, (uint32_t)he,
                            "Nord2 GPU board: quirks failed");
        r->stage = N2B_STAGE_CORES;
        /* r0p1 must first map/run its firmware through AS0 in the core layer. */
        he = b->hw->has_issue_ttrx_3485 ? n2hw_power_on_l2(b->hw) :
                n2hw_power_on_cores(b->hw, b->hw->id.shader_present);
        if (he != N2HW_OK) {
                (void)n2hw_power_off_cores(b->hw);
                return fail(b, N2B_STAGE_CORES, (uint32_t)he,
                            "Nord2 GPU board: core power-on failed");
        }
        r->stage = N2B_STAGE_READY;
        r->mtcmos_after = mtcmos_state(b);
        b->powered = true;
        return true;
}

bool nord2_gpu_board_power_off(struct nord2_gpu_board *b)
{
        if (!b || !b->powered)
                return false;
        struct nord2_gpu_board_report *r = &b->report;
        r->stage = N2B_STAGE_POWER_OFF;
        bool ok = true;
        enum n2hw_error he = n2hw_power_off_cores(b->hw);
        if (he != N2HW_OK) {
                r->error = (uint32_t)he;
                ok = false;
        }
        if (b->cg_by_us && n2clk_bus_idle(b->clk) != N2CLK_OK)
                ok = false;
        if (!ok) {
                /* The vendor would block forever here; keep the GPU powered
                 * and report instead of cutting power under traffic. */
                note(b, "Nord2 GPU board: power-off deferred; GPU busy");
                return false;
        }
        b->powered = false;
        return rollback(b);
}

const char *nord2_gpu_stage_string(uint32_t stage)
{
        switch (stage) {
        case N2B_STAGE_NONE: return "none";
        case N2B_STAGE_PREFLIGHT: return "preflight";
        case N2B_STAGE_RAILS: return "rails";
        case N2B_STAGE_MUX: return "mfg_sel";
        case N2B_STAGE_MTCMOS: return "mtcmos";
        case N2B_STAGE_CG: return "bg3d";
        case N2B_STAGE_RESET: return "reset";
        case N2B_STAGE_IDENTIFY: return "identify";
        case N2B_STAGE_SLOTS: return "slots";
        case N2B_STAGE_QUIRKS: return "quirks";
        case N2B_STAGE_CORES: return "cores";
        case N2B_STAGE_READY: return "ready";
        case N2B_STAGE_POWER_OFF: return "power-off";
        }
        return "unknown";
}
