/*
 * Integrated board sequencing test: rails, mfg_sel, SCPsys MTCMOS, BG3D and
 * the GPU-internal protocol against one register world.  The world faults
 * any GPU register access while MFG is unpowered or BG3D is gated, which is
 * the bus-hang hazard of the previous sequencer.  Models are contract
 * checks, not evidence of hardware behaviour.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_board.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { failures++; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

enum ev { EV_RAIL = 1, EV_MUX, EV_MFG_ON, EV_CG, EV_GPU_RESET, EV_CORES,
          EV_MFG_OFF, EV_RAIL_OFF };

struct world {
        /* PMIC */
        uint8_t m15[0x2000];
        uint16_t m59[0x2000];
        /* SoC */
        uint32_t spm[0x400 / 4];
        uint32_t infra[0x1000 / 4];
        uint32_t topck[0x200 / 4];
        uint32_t apmixed[0x400 / 4];
        uint32_t mfgcfg[0x200 / 4];
        uint32_t gpu[0x3000 / 4];
        /* knobs */
        int fail_mfg_domain;     /* SRAM ack never clears for this domain */
        bool gpu_reset_never;
        bool cores_never;
        bool bus_busy;
        bool bg3d_stuck;
        bool status_disagrees;
        bool foreign_mfg_on_rail;
        unsigned drop_pll_write; /* one-based CON1 write to ignore */
        unsigned pll_writes, clock_writes;
        /* trace */
        unsigned events[64], nev;
        unsigned hazards;
        unsigned pmic_writes;
};

static void ev(struct world *w, unsigned e)
{
        if (w->nev < 64)
                w->events[w->nev++] = e;
}

static bool gpu_accessible(struct world *w)
{
        return (w->spm[0x16c / 4] & 0x1fc) == 0x1fc && !(w->mfgcfg[0] & 1);
}

/* ---- PMIC via rail ops ---- */
static int r15(void *c, uint16_t a, uint8_t *v)
{
        struct world *w = c;
        w->m15[N2RAIL_VGPU_DA_EN] = w->m15[N2RAIL_VGPU_EN] & 1;
        w->m15[N2RAIL_VGPU_DA_VOSEL] = w->m15[N2RAIL_VGPU_VOSEL];
        *v = w->m15[a];
        return 0;
}
static int w15(void *c, uint16_t a, uint8_t v)
{
        struct world *w = c;
        w->pmic_writes++;
        if (a == N2RAIL_VGPU_EN && (v & 1) != (w->m15[a] & 1))
                ev(w, (v & 1) ? EV_RAIL : EV_RAIL_OFF);
        w->m15[a] = v;
        if (a == N2RAIL_VGPU_EN && (v & 1) && w->foreign_mfg_on_rail)
                w->spm[0x308 / 4] |= 12;
        return 0;
}
static int r59(void *c, uint16_t a, uint16_t *v)
{
        struct world *w = c;
        w->m59[N2RAIL_VSRAM_MON / 2] = w->m59[N2RAIL_VSRAM_CON0 / 2] & 1;
        w->m59[N2RAIL_VSRAM_VOSEL1 / 2] =
                (uint16_t)((w->m59[N2RAIL_VSRAM_ELR / 2] & 0x7f) << 8);
        *v = w->m59[a / 2];
        return 0;
}
static int w59(void *c, uint16_t a, uint16_t v)
{
        struct world *w = c;
        w->pmic_writes++;
        w->m59[a / 2] = v;
        return 0;
}
static void dly(void *c, unsigned us) { (void)c; (void)us; }
static const struct n2rail_ops rail_ops = { r15, w15, r59, w59, dly, NULL };

/* ---- SPM / INFRA ---- */
static void spm_refresh(struct world *w)
{
        uint32_t sta = 0;
        for (unsigned d = 0; d < 7; d++) {
                uint32_t *con = &w->spm[(0x308 / 4) + d];
                if ((*con & (1u << 2)) && (*con & (1u << 3)))
                        sta |= 1u << (2 + d);
                bool hold = (int)d == w->fail_mfg_domain;
                if ((*con & (1u << 8)) || hold)
                        *con |= 1u << 12;
                else
                        *con &= ~(1u << 12);
        }
        w->spm[0x16c / 4] = sta;
        w->spm[0x170 / 4] = sta;
}
static uint32_t rspm(void *c, uint32_t o)
{
        struct world *w = c;
        spm_refresh(w);
        return w->spm[o / 4] ^ ((w->status_disagrees && o == 0x170) ? 4u : 0);
}
static void wspm(void *c, uint32_t o, uint32_t v)
{
        struct world *w = c;
        if (o >= 0x308 && o <= 0x320) {
                unsigned d = (o - 0x308) / 4;
                uint32_t old = w->spm[o / 4];
                if ((v & 4) && !(old & 4)) {
                        /* PG_MFG0 needs its parent clock mfg_sel open. */
                        if (d == 0 && (w->topck[0x60 / 4] & (1u << 15)))
                                w->hazards++;
                        if (d > 0 && !(w->spm[0x308 / 4] & 4))
                                w->hazards++;
                        ev(w, EV_MFG_ON);
                }
                if (!(v & 4) && (old & 4))
                        ev(w, EV_MFG_OFF);
        }
        w->spm[o / 4] = v;
        spm_refresh(w);
}
static const uint32_t aliases[][3] = {
        {0x2a0, 0x2a4, 0x228}, {0x2a8, 0x2ac, 0x258},
        {0x714, 0x718, 0x724}, {0xbb8, 0xbbc, 0xbc4},
};
static uint32_t rinf(void *c, uint32_t o) { return ((struct world *)c)->infra[o / 4]; }
static void winf(void *c, uint32_t o, uint32_t v)
{
        struct world *w = c;
        for (unsigned i = 0; i < 4; i++) {
                if (o == aliases[i][0]) { w->infra[aliases[i][2] / 4] |= v; return; }
                if (o == aliases[i][1]) { w->infra[aliases[i][2] / 4] &= ~v; return; }
        }
}

/* ---- Clocks ---- */
static uint32_t *clk_reg(struct world *w, enum n2clk_block b, uint32_t off)
{
        return b == N2CLK_TOPCK ? &w->topck[off / 4]
             : b == N2CLK_APMIXED ? &w->apmixed[off / 4] : &w->mfgcfg[off / 4];
}
static uint32_t rclk(void *c, enum n2clk_block b, uint32_t off)
{
        struct world *w = c;
        if (b == N2CLK_MFGCFG && !(w->spm[0x16c / 4] & 4))
                w->hazards++; /* MFGCFG lives in the MFG0 domain */
        if (b == N2CLK_MFGCFG && off == N2CLK_MFG_DEBUG_TOP)
                return w->bus_busy ? 0 : 4;
        return *clk_reg(w, b, off);
}
static void wclk(void *c, enum n2clk_block b, uint32_t off, uint32_t v)
{
        struct world *w = c;
        w->clock_writes++;
        if (b == N2CLK_APMIXED) {
                if ((w->spm[0x16c / 4] & 0x1fc) ||
                    ((w->topck[0x60 / 4] >> 8) & 3u) != 3u ||
                    !(w->m15[N2RAIL_VGPU_EN] & 1u))
                        w->hazards++;
                if (off == N2CLK_MFGPLL_CON1) {
                        if (++w->pll_writes == w->drop_pll_write) return;
                        v &= 0x7fffffffu; /* PCW_CHG self-clears */
                }
        }
        if (b == N2CLK_MFGCFG && !(w->spm[0x16c / 4] & 4))
                w->hazards++;
        if (b == N2CLK_TOPCK && off == N2CLK_CFG_5_SET) {
                w->topck[0x60 / 4] |= v;
                return;
        }
        if (b == N2CLK_TOPCK && off == N2CLK_CFG_5_CLR) {
                if ((v & (1u << 15)) && (w->topck[0x60 / 4] & (1u << 15)))
                        ev(w, EV_MUX);
                w->topck[0x60 / 4] &= ~v;
                return;
        }
        if (b == N2CLK_MFGCFG && off == N2CLK_MFG_CG_CLR) {
                if (!w->bg3d_stuck)
                        w->mfgcfg[0] &= ~v;
                ev(w, EV_CG);
                return;
        }
        if (b == N2CLK_MFGCFG && off == N2CLK_MFG_CG_SET) {
                w->mfgcfg[0] |= v;
                return;
        }
        *clk_reg(w, b, off) = v;
}
static const struct n2clk_ops clk_ops = { rclk, wclk, dly, NULL };

/* ---- GPU ---- */
static uint32_t rgpu(void *c, uint32_t off)
{
        struct world *w = c;
        if (!gpu_accessible(w)) {
                w->hazards++;
                return 0xdeadbeef;
        }
        if (off == N2HW_SHADER_READY_LO && !w->cores_never)
                w->gpu[off / 4] = w->gpu[N2HW_SHADER_PWRON_LO / 4];
        if (off == N2HW_SHADER_READY_LO && w->gpu[N2HW_SHADER_PWROFF_LO / 4]) {
                w->gpu[off / 4] &= ~w->gpu[N2HW_SHADER_PWROFF_LO / 4];
                w->gpu[N2HW_SHADER_PWROFF_LO / 4] = 0;
        }
        if ((off == N2HW_L2_READY_LO || off == N2HW_TILER_READY_LO) &&
            w->gpu[N2HW_TILER_PWRON_LO / 4])
                w->gpu[off / 4] = 1;
        return w->gpu[off / 4];
}
static void wgpu(void *c, uint32_t off, uint32_t v)
{
        struct world *w = c;
        if (!gpu_accessible(w)) {
                w->hazards++;
                return;
        }
        if (off == N2HW_GPU_COMMAND) {
                if (v == N2HW_CMD_SOFT_RESET || v == N2HW_CMD_HARD_RESET) {
                        ev(w, EV_GPU_RESET);
                        if (!w->gpu_reset_never)
                                w->gpu[N2HW_GPU_IRQ_RAWSTAT / 4] |=
                                        N2HW_IRQ_RESET_COMPLETED;
                }
                if (v == N2HW_CMD_CLEAN_INV_CACHES)
                        w->gpu[N2HW_GPU_IRQ_RAWSTAT / 4] |=
                                N2HW_IRQ_CLEAN_CACHES_COMPLETED;
                return;
        }
        if (off == N2HW_GPU_IRQ_CLEAR) {
                w->gpu[N2HW_GPU_IRQ_RAWSTAT / 4] &= ~v;
                return;
        }
        if (off == N2HW_SHADER_PWRON_LO)
                ev(w, EV_CORES);
        w->gpu[off / 4] = v;
}
static const struct n2hw_ops hw_ops = { rgpu, wgpu, dly, NULL };

struct rig {
        struct world w;
        struct n2rail rail;
        struct n2clk clk;
        struct nord2_scpsys sp;
        struct n2hw hw;
        struct nord2_gpu_board b;
};

static void world_cold(struct world *w)
{
        memset(w, 0, sizeof(*w));
        w->fail_mfg_domain = -1;
        w->m15[N2RAIL_VGPU_EN] = 0x04;
        w->m15[N2RAIL_VGPU_VOSEL] = 0x30 + 40;
        w->m59[N2RAIL_VSRAM_ELR / 2] = 24;
        w->topck[0x60 / 4] = (1u << 15) | (3u << 8) | 0x11;  /* gated, sub parent */
        /* Preloader MFGPLL at 675 MHz /4. */
        w->apmixed[N2CLK_MFGPLL_CON0 / 4] = 1;
        w->apmixed[N2CLK_MFGPLL_CON3 / 4] = 1;
        w->apmixed[N2CLK_MFGPLL_CON1 / 4] =
                (2u << 24) | (uint32_t)((675ull * 4 << 14) / 26 + 1);
        w->mfgcfg[0] = 1; /* BG3D gated */
        for (unsigned d = 0; d < 7; d++)
                w->spm[(0x308 / 4) + d] = (1u << 4) | (1u << 1) | (1u << 8);
        w->gpu[N2HW_GPU_ID / 4] = 0x90000020u;
        w->gpu[N2HW_AS_PRESENT / 4] = 0xff;
        w->gpu[N2HW_JS_PRESENT / 4] = 7;
        w->gpu[N2HW_JS0_FEATURES / 4] = 0x20e;
        w->gpu[N2HW_JS0_FEATURES / 4 + 1] = 0x1fe;
        w->gpu[N2HW_SHADER_PRESENT_LO / 4] = 0x00660067u;
        w->gpu[N2HW_TILER_PRESENT_LO / 4] = 1;
        w->gpu[N2HW_L2_PRESENT_LO / 4] = 1;
}

static void rig_init(struct rig *g)
{
        CHECK(n2rail_init(&g->rail, &rail_ops, &g->w));
        CHECK(n2clk_init(&g->clk, &clk_ops, &g->w));
        const struct nord2_scpsys_ops sop = { rspm, wspm, rinf, winf, dly, NULL };
        CHECK(nord2_scpsys_init(&g->sp, &sop, &g->w, 1000));
        CHECK(n2hw_init(&g->hw, &hw_ops, &g->w));
        CHECK(nord2_gpu_board_init(&g->b, &g->rail, &g->clk, &g->sp, &g->hw,
                                   NULL, NULL));
}

static bool cold_state(const struct world *w)
{
        return (w->m15[N2RAIL_VGPU_EN] & 1) == 0 &&
               w->m15[N2RAIL_VGPU_VOSEL] == 0x30 + 40 &&
               (w->m59[N2RAIL_VSRAM_CON0 / 2] & 1) == 0 &&
               (w->m59[N2RAIL_VSRAM_ELR / 2] & 0x7f) == 24 &&
               (w->topck[0x60 / 4] & (1u << 15)) &&
               ((w->topck[0x60 / 4] >> 8) & 3) == 3 &&
               (w->spm[0x16c / 4] & 0x1fc) == 0 &&
               (w->mfgcfg[0] & 1);
}

static void test_cold_on_off(void)
{
        static struct rig g;
        world_cold(&g.w);
        rig_init(&g);
        CHECK(nord2_gpu_board_power_on(&g.b) == false); /* no preflight */
        unsigned before = g.w.pmic_writes;
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(g.w.pmic_writes == before && g.w.nev == 0); /* read-only */
        CHECK(g.b.report.khz >= 674000 && g.b.report.khz <= 676000);
        CHECK(g.b.report.opp_index == 24 && g.b.report.vgpu_uv == 650000);
        CHECK(nord2_gpu_board_power_on(&g.b));
        CHECK(g.w.hazards == 0);
        CHECK(g.b.powered && g.b.report.stage == N2B_STAGE_READY);
        /* Vendor order: rails, mux, MFG0..6, CG, reset, cores. */
        unsigned i = 0;
        CHECK(g.w.events[i++] == EV_RAIL);
        CHECK(g.w.events[i++] == EV_MUX);
        for (unsigned d = 0; d < 7; d++)
                CHECK(g.w.events[i++] == EV_MFG_ON);
        CHECK(g.w.events[i++] == EV_CG);
        CHECK(g.w.events[i++] == EV_GPU_RESET);
        CHECK(g.w.events[i++] == EV_CORES);
        CHECK(((g.w.topck[0x60 / 4] >> 8) & 3) == 1);
        CHECK(g.w.gpu[N2HW_SHADER_CONFIG / 4] & N2HW_SC_VAR_ALGORITHM);
        CHECK(g.hw.shaders_on == 0x00660067u);
        CHECK(nord2_gpu_board_power_off(&g.b));
        CHECK(g.w.hazards == 0);
        CHECK(cold_state(&g.w));
        CHECK(!g.b.powered && g.b.report.rollback_clean && !g.hw.l2_on);
        /* Off order: MFG reverse then rails. */
        CHECK(g.w.events[g.w.nev - 1] == EV_RAIL_OFF);
        CHECK(nord2_gpu_board_power_off(&g.b) == false);
}

static void test_failures(void)
{
        static struct rig g;
        /* Each MTCMOS domain failing in turn rolls back to the cold state. */
        for (int d = 0; d < 7; d++) {
                world_cold(&g.w);
                g.w.fail_mfg_domain = d;
                rig_init(&g);
                CHECK(nord2_gpu_board_preflight(&g.b));
                CHECK(!nord2_gpu_board_power_on(&g.b));
                CHECK(g.b.report.stage == N2B_STAGE_MTCMOS &&
                      g.b.report.mtcmos_failed_domain == (uint32_t)d);
                /* The failing domain stays ack-busy, so its off may fail;
                 * then rails must stay on and the board must be uncertain. */
                if (g.b.report.rollback_clean)
                        CHECK(cold_state(&g.w));
                else
                        CHECK(g.b.uncertain && (g.w.m15[N2RAIL_VGPU_EN] & 1));
                CHECK(g.w.hazards == 0);
        }
        /* GPU reset never completes: soft+hard bounded, full rollback. */
        world_cold(&g.w);
        g.w.gpu_reset_never = true;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(!nord2_gpu_board_power_on(&g.b));
        CHECK(g.b.report.stage == N2B_STAGE_RESET &&
              g.b.report.error == N2HW_E_RESET_TIMEOUT);
        CHECK(cold_state(&g.w) && g.b.report.rollback_clean);
        /* r0p1 reaches L2 only; shaders await the private WA MMU/job. */
        world_cold(&g.w);
        g.w.gpu[N2HW_GPU_ID / 4] = 0x90000010u;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(nord2_gpu_board_power_on(&g.b));
        CHECK(g.b.report.stage == N2B_STAGE_READY &&
              g.b.report.gpu_id == 0x90000010u && g.hw.l2_on && !g.hw.shaders_on);
        CHECK(g.hw.has_issue_ttrx_3485 && !g.hw.wa_complete);
        CHECK(nord2_gpu_board_power_off(&g.b) && cold_state(&g.w));
        /* Shader cores never ready. */
        world_cold(&g.w);
        g.w.cores_never = true;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(!nord2_gpu_board_power_on(&g.b));
        CHECK(g.b.report.stage == N2B_STAGE_CORES && cold_state(&g.w));
        CHECK(g.w.hazards == 0);
        /* BG3D does not open: rollback before any GPU MMIO. */
        world_cold(&g.w);
        g.w.bg3d_stuck = true;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(!nord2_gpu_board_power_on(&g.b));
        CHECK(g.b.report.stage == N2B_STAGE_CG && g.w.hazards == 0);
        CHECK(cold_state(&g.w));
        /* Inconsistent MFG state. */
        world_cold(&g.w);
        g.w.spm[0x30c / 4] |= (1u << 2) | (1u << 3); /* MFG1 on, MFG0 off */
        rig_init(&g);
        CHECK(!nord2_gpu_board_preflight(&g.b));
        /* Busy bus: power-off deferred, GPU stays powered. */
        world_cold(&g.w);
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b) && nord2_gpu_board_power_on(&g.b));
        g.w.bus_busy = true;
        CHECK(!nord2_gpu_board_power_off(&g.b));
        CHECK(g.b.powered && (g.w.spm[0x16c / 4] & 0x1fc) == 0x1fc);
        g.w.bus_busy = false;
        CHECK(nord2_gpu_board_power_off(&g.b) && cold_state(&g.w));
}

static void test_warm(void)
{
        /* Boot chain left everything on: nothing this code did not enable
         * may be disabled at power-off. */
        static struct rig g;
        world_cold(&g.w);
        g.w.m15[N2RAIL_VGPU_EN] = 0x05;
        g.w.m15[N2RAIL_VGPU_VOSEL] = 0x30 + 60;
        g.w.m59[N2RAIL_VSRAM_CON0 / 2] = 1;
        g.w.m59[N2RAIL_VSRAM_ELR / 2] = 40;
        g.w.topck[0x60 / 4] = (1u << 8);
        for (unsigned d = 0; d < 7; d++)
                g.w.spm[(0x308 / 4) + d] = (1u << 0) | (1u << 2) | (1u << 3);
        spm_refresh(&g.w);
        g.w.mfgcfg[0] = 1;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(g.b.report.mtcmos_before == 0x1fc);
        CHECK(nord2_gpu_board_power_on(&g.b) && g.w.hazards == 0);
        unsigned mfg_on = 0;
        for (unsigned i = 0; i < g.w.nev; i++)
                mfg_on += g.w.events[i] == EV_MFG_ON;
        CHECK(mfg_on == 0 && g.w.pmic_writes == 0);
        CHECK(nord2_gpu_board_power_off(&g.b));
        CHECK((g.w.spm[0x16c / 4] & 0x1fc) == 0x1fc);
        CHECK((g.w.m15[N2RAIL_VGPU_EN] & 1) && (g.w.m59[N2RAIL_VSRAM_CON0 / 2] & 1));
        CHECK(g.w.mfgcfg[0] & 1); /* BG3D was ours: closed again */
        /* Already-open BG3D is inherited, so stop must leave it open. */
        g.w.mfgcfg[0] = 0;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b) && nord2_gpu_board_power_on(&g.b));
        CHECK(!g.clk.cg_ungated_by_us);
        CHECK(nord2_gpu_board_power_off(&g.b));
        CHECK(!(g.w.mfgcfg[0] & 1));
}

static void test_cold_opp_target(void)
{
        static struct rig g;
        /* 0091: opt-in cold-start OPP.  Exact vendor entries only; the rails are
         * raised to that entry's voltages before the PLL is programmed. */
        const uint32_t targets[] = {654000, 886000, 370000, 350000};
        const uint32_t vgpu[] = {650000, 800000, 606250, 600000};
        for (unsigned i = 0; i < 4; i++) {
                world_cold(&g.w);
                g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] = 0;
                g.w.apmixed[N2CLK_MFGPLL_CON0 / 4] = 0;
                g.w.apmixed[N2CLK_MFGPLL_CON3 / 4] = 2;
                uint32_t cfg = g.w.topck[N2CLK_CFG_5 / 4];
                rig_init(&g);
                g.b.target_khz = targets[i];
                CHECK(nord2_gpu_board_preflight(&g.b));
                CHECK(!g.w.pmic_writes && !g.w.clock_writes);
                CHECK(g.b.report.normalize_pll && g.b.report.khz == targets[i]);
                CHECK(g.b.report.vgpu_uv >= vgpu[i] - 6250 && g.b.report.vgpu_uv <= vgpu[i] + 6250);
                CHECK(g.clk.target_khz == (targets[i] == 350000 ? 0 : targets[i]));
                CHECK(nord2_gpu_board_power_on(&g.b));
                CHECK(g.b.report.clk_after.khz == targets[i] && !g.w.hazards);
                CHECK(nord2_gpu_board_power_off(&g.b));
                CHECK(!g.w.hazards && !g.b.uncertain && g.w.topck[N2CLK_CFG_5 / 4] == cfg);
        }
        /* Not a vendor OPP: refused before any PMIC/clock write. */
        world_cold(&g.w);
        g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] = 0;
        g.w.apmixed[N2CLK_MFGPLL_CON0 / 4] = 0;
        g.w.apmixed[N2CLK_MFGPLL_CON3 / 4] = 2;
        rig_init(&g);
        g.b.target_khz = 600000;
        CHECK(!nord2_gpu_board_preflight(&g.b) && g.b.report.error == N2RAIL_E_FREQ);
        CHECK(!g.w.pmic_writes && !g.w.clock_writes);
        /* A valid inherited PLL is never retuned, whatever was requested. */
        world_cold(&g.w);
        rig_init(&g);
        g.b.target_khz = 886000;
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(!g.b.report.normalize_pll && g.b.report.khz == g.b.report.clk_before.khz);
        CHECK(g.clk.target_khz == 0);
}

static void test_cold_pll_normalization(void)
{
        static struct rig g;
        /* Off, under-range, over-range, unsupported divider. Each case
         * reaches 350MHz only with supplies on and every MFG domain off. */
        const uint32_t bad_rates[] = {0, 0x040c4ec5u, 0x022489d9u, 0x011aec4fu};
        for (unsigned i = 0; i < 4; i++) {
                world_cold(&g.w);
                g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] = bad_rates[i];
                if (!i) {
                        g.w.apmixed[N2CLK_MFGPLL_CON0 / 4] = 0;
                        g.w.apmixed[N2CLK_MFGPLL_CON3 / 4] = 2;
                }
                g.w.topck[N2CLK_CFG_5 / 4] |= 0x92550025u; /* other consumers */
                uint32_t cfg = g.w.topck[N2CLK_CFG_5 / 4];
                uint32_t con0 = g.w.apmixed[N2CLK_MFGPLL_CON0 / 4];
                uint32_t con3 = g.w.apmixed[N2CLK_MFGPLL_CON3 / 4];
                rig_init(&g);
                CHECK(nord2_gpu_board_preflight(&g.b));
                CHECK(!g.w.pmic_writes && !g.w.clock_writes);
                CHECK(g.b.report.normalize_pll && g.b.report.khz == 350000);
                CHECK(g.b.report.vgpu_uv == 600000 && g.b.report.vsram_uv == 750000);
                CHECK(nord2_gpu_board_power_on(&g.b));
                CHECK(g.b.report.clk_after.khz == 350000);
                CHECK(g.w.pll_writes == 1 && !g.w.hazards);
                CHECK(nord2_gpu_board_power_off(&g.b));
                CHECK(g.w.pll_writes == 2 && !g.w.hazards && !g.b.uncertain);
                CHECK(g.w.topck[N2CLK_CFG_5 / 4] == cfg);
                CHECK(g.w.apmixed[N2CLK_MFGPLL_CON0 / 4] == con0);
                CHECK(g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] == bad_rates[i]);
                CHECK(g.w.apmixed[N2CLK_MFGPLL_CON3 / 4] == con3);
        }
        /* Failed PCW write never powers a domain and rolls all supplies back. */
        world_cold(&g.w);
        g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] = bad_rates[2];
        g.w.drop_pll_write = 1;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(!nord2_gpu_board_power_on(&g.b));
        CHECK(g.b.report.stage == N2B_STAGE_MUX && cold_state(&g.w));
        CHECK(g.b.report.rollback_clean && !g.w.hazards);
        /* Failed restoration must retain supplies/ownership, not pretend off. */
        world_cold(&g.w);
        g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] = bad_rates[2];
        g.w.drop_pll_write = 2;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b) && nord2_gpu_board_power_on(&g.b));
        CHECK(!nord2_gpu_board_power_off(&g.b));
        CHECK(g.b.uncertain && g.clk.pll_changed && g.b.rails_by_us);
        CHECK(g.w.m15[N2RAIL_VGPU_EN] & 1u);
        CHECK(!g.w.hazards);
        /* Any inherited live domain forbids cold normalization. */
        world_cold(&g.w);
        g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] = bad_rates[2];
        g.w.spm[0x308 / 4] |= 12;
        rig_init(&g);
        CHECK(!nord2_gpu_board_preflight(&g.b));
        CHECK(!g.w.pmic_writes && !g.w.clock_writes);
        /* A half-acknowledged power transition is not equivalent to off. */
        world_cold(&g.w); g.w.status_disagrees = true;
        rig_init(&g);
        CHECK(!nord2_gpu_board_preflight(&g.b));
        CHECK(!g.w.pmic_writes && !g.w.clock_writes);
        /* A domain appearing during supply enable must keep its rails. */
        world_cold(&g.w);
        g.w.apmixed[N2CLK_MFGPLL_CON1 / 4] = bad_rates[2];
        g.w.foreign_mfg_on_rail = true;
        rig_init(&g);
        CHECK(nord2_gpu_board_preflight(&g.b));
        CHECK(!nord2_gpu_board_power_on(&g.b));
        CHECK(g.b.uncertain && g.b.rails_by_us && !g.w.clock_writes);
        CHECK(g.w.m15[N2RAIL_VGPU_EN] & 1u);
}

static void test_config(void)
{
        struct nord2_gpu_board_config cfg = {
                .reg_base = NORD2_GPU_REG_BASE, .reg_size = NORD2_GPU_REG_SIZE,
                .mfg_base = NORD2_GPU_MFG_BASE, .mfg_size = NORD2_GPU_MFG_SIZE,
                .irq = {362, 363, 364, 365, 366},
                .expected_arch_major = 9, .expected_arch_minor = 0,
        };
        CHECK(nord2_gpu_board_config_valid(&cfg));
        cfg.irq[2] = 364 + 32;
        CHECK(!nord2_gpu_board_config_valid(&cfg));
        cfg.irq[2] = 364;
        cfg.mfg_base = 0x10001000;
        CHECK(!nord2_gpu_board_config_valid(&cfg));
        CHECK(!nord2_gpu_board_config_valid(NULL));
}

int main(void)
{
        test_config();
        test_cold_on_off();
        test_failures();
        test_warm();
        test_cold_pll_normalization();
        test_cold_opp_target();
        if (failures) {
                fprintf(stderr, "nord2_board_test: %u failures\n", failures);
                return 1;
        }
        printf("nord2_board_test: vendor order, no pre-clock GPU MMIO, ownership "
               "rollback, warm-boot preservation pass (register world)\n");
        return 0;
}
