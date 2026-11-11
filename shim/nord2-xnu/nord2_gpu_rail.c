/*
 * Nord 2 MT6893 GPU rails.  See nord2_gpu_rail.h for provenance.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_rail.h"

#include "nord2_gpu_compat.h"

/* Vendor GPUOP(khz, vgpu, vsram, posdiv, aging) in mV x 100, converted to uV
 * and taking max(segment_1, segment_2).  Entries 25..40 use segment 2 vgpu. */
#define OPP(k, vg, vs, pd) { (k), (vg) * 10u, (vs) * 10u, (pd) }
const struct n2rail_opp n2rail_opp_table[N2RAIL_OPP_COUNT] = {
        OPP(886000, 80000, 80000, 2), OPP(879000, 79375, 79375, 2),
        OPP(873000, 78750, 78750, 2), OPP(867000, 78125, 78125, 2),
        OPP(861000, 77500, 77500, 2), OPP(854000, 76875, 76875, 2),
        OPP(848000, 76250, 76250, 2), OPP(842000, 75625, 75625, 2),
        OPP(836000, 75000, 75000, 2), OPP(825000, 74375, 75000, 2),
        OPP(815000, 73750, 75000, 2), OPP(805000, 73125, 75000, 2),
        OPP(795000, 72500, 75000, 2), OPP(785000, 71875, 75000, 2),
        OPP(775000, 71250, 75000, 2), OPP(765000, 70625, 75000, 2),
        OPP(755000, 70000, 75000, 2), OPP(745000, 69375, 75000, 2),
        OPP(735000, 68750, 75000, 2), OPP(725000, 68125, 75000, 2),
        OPP(715000, 67500, 75000, 2), OPP(705000, 66875, 75000, 2),
        OPP(695000, 66250, 75000, 2), OPP(685000, 65625, 75000, 2),
        OPP(675000, 65000, 75000, 2), OPP(654000, 65000, 75000, 2),
        OPP(634000, 64375, 75000, 2), OPP(614000, 64375, 75000, 2),
        OPP(593000, 63750, 75000, 2), OPP(573000, 63750, 75000, 2),
        OPP(553000, 63125, 75000, 2), OPP(532000, 63125, 75000, 2),
        OPP(512000, 62500, 75000, 2), OPP(492000, 62500, 75000, 2),
        OPP(471000, 61875, 75000, 2), OPP(451000, 61875, 75000, 2),
        OPP(431000, 61250, 75000, 2), OPP(410000, 61250, 75000, 2),
        OPP(390000, 60625, 75000, 2), OPP(370000, 60625, 75000, 3),
        OPP(350000, 60000, 75000, 3),
};
#undef OPP

uint32_t n2rail_mfgpll_khz(uint32_t con1)
{
        uint64_t dds = con1 & 0x3fffffu;
        uint32_t posdiv = (con1 >> 24) & 7u;
        return (uint32_t)((((dds * 10u + 5u) * 26u) >> 14) / (1u << posdiv) *
                          100u);
}

bool n2rail_opp_index_exact(uint32_t khz, unsigned *index)
{
        for (unsigned i = 0; i < N2RAIL_OPP_COUNT; i++)
                if (n2rail_opp_table[i].khz == khz) {
                        if (index)
                                *index = i;
                        return true;
                }
        return false;
}

bool n2rail_point_for_khz(uint32_t khz, struct n2rail_point *pt)
{
        if (!pt || !khz || khz > n2rail_opp_table[0].khz)
                return false;
        /* The table is descending.  Use the lowest-frequency entry that is
         * still >= khz: its voltage covers every frequency at or below it. */
        unsigned idx = 0;
        for (unsigned i = 0; i < N2RAIL_OPP_COUNT; i++) {
                if (n2rail_opp_table[i].khz >= khz)
                        idx = i;
        }
        pt->index = idx;
        pt->khz = khz;
        pt->vgpu_uv = n2rail_opp_table[idx].vgpu_uv;
        pt->vsram_uv = n2rail_opp_table[idx].vsram_uv;
        return true;
}

bool n2rail_vgpu_uv(uint8_t sel, uint32_t *uv)
{
        if (!uv || sel < N2RAIL_VGPU_SEL_MIN || sel > N2RAIL_VGPU_SEL_MAX)
                return false;
        *uv = 300000u + (uint32_t)(sel - N2RAIL_VGPU_SEL_MIN) * 6250u;
        return true;
}

bool n2rail_vsram_uv(uint8_t sel, uint32_t *uv)
{
        if (!uv || sel > N2RAIL_VSRAM_SEL_MAX)
                return false;
        *uv = 500000u + (uint32_t)sel * 6250u;
        return true;
}

uint8_t n2rail_vgpu_sel_for_uv(uint32_t uv)
{
        if (uv <= 300000u)
                return N2RAIL_VGPU_SEL_MIN;
        uint32_t steps = (uv - 300000u + 6249u) / 6250u;
        uint32_t sel = N2RAIL_VGPU_SEL_MIN + steps;
        return sel > N2RAIL_VGPU_SEL_MAX ? 0 : (uint8_t)sel;
}

uint8_t n2rail_vsram_sel_for_uv(uint32_t uv)
{
        if (uv <= 500000u)
                return 0;
        uint32_t sel = (uv - 500000u + 6249u) / 6250u;
        return sel > N2RAIL_VSRAM_SEL_MAX ? 0xff : (uint8_t)sel;
}

bool n2rail_init(struct n2rail *r, const struct n2rail_ops *ops, void *ctx)
{
        if (!r || !ops || !ops->mt6315_read || !ops->mt6315_write ||
            !ops->mt6359_read || !ops->mt6359_write || !ops->udelay)
                return false;
        memset(r, 0, sizeof(*r));
        r->ops = *ops;
        r->ctx = ctx;
        return true;
}

static void note(struct n2rail *r, const char *msg)
{
        if (r->ops.log)
                r->ops.log(r->ctx, msg);
}

enum n2rail_error n2rail_capture(struct n2rail *r, struct n2rail_snapshot *s)
{
        if (!r || !s)
                return N2RAIL_E_ARGS;
        memset(s, 0, sizeof(*s));
        const uint16_t b_addr[5] = {N2RAIL_MT6315_SWCID_H, N2RAIL_VGPU_EN,
                                    N2RAIL_VGPU_VOSEL, N2RAIL_VGPU_DA_EN,
                                    N2RAIL_VGPU_DA_VOSEL};
        uint8_t *b_val[5] = {&s->swcid_h, &s->vgpu_en, &s->vgpu_vosel,
                             &s->vgpu_da_en, &s->vgpu_da_vosel};
        for (unsigned i = 0; i < 5; i++) {
                if (r->ops.mt6315_read(r->ctx, b_addr[i], b_val[i]))
                        return N2RAIL_E_IO;
                s->valid |= 1u << i;
        }
        const uint16_t w_addr[4] = {N2RAIL_VSRAM_CON0, N2RAIL_VSRAM_MON,
                                    N2RAIL_VSRAM_VOSEL1, N2RAIL_VSRAM_ELR};
        uint16_t *w_val[4] = {&s->vsram_con0, &s->vsram_mon,
                              &s->vsram_vosel1, &s->vsram_elr};
        for (unsigned i = 0; i < 4; i++) {
                if (r->ops.mt6359_read(r->ctx, w_addr[i], w_val[i]))
                        return N2RAIL_E_IO;
                s->valid |= 1u << (5 + i);
        }
        return N2RAIL_OK;
}

/* Masked update with full read-back; only `mask` may change. */
static enum n2rail_error upd8(struct n2rail *r, uint16_t addr, uint8_t mask,
                              uint8_t value)
{
        uint8_t old = 0, now = 0;
        if (r->ops.mt6315_read(r->ctx, addr, &old))
                return N2RAIL_E_IO;
        uint8_t want = (uint8_t)((old & ~mask) | (value & mask));
        r->uncertain = true; /* a failed write may still have committed */
        r->writes++;
        if (r->ops.mt6315_write(r->ctx, addr, want) ||
            r->ops.mt6315_read(r->ctx, addr, &now))
                return N2RAIL_E_IO;
        if (now != want)
                return N2RAIL_E_VERIFY;
        r->uncertain = false;
        return N2RAIL_OK;
}

static enum n2rail_error upd16(struct n2rail *r, uint16_t addr, uint16_t mask,
                               uint16_t value)
{
        uint16_t old = 0, now = 0;
        if (r->ops.mt6359_read(r->ctx, addr, &old))
                return N2RAIL_E_IO;
        uint16_t want = (uint16_t)((old & ~mask) | (value & mask));
        r->uncertain = true;
        r->writes++;
        if (r->ops.mt6359_write(r->ctx, addr, want) ||
            r->ops.mt6359_read(r->ctx, addr, &now))
                return N2RAIL_E_IO;
        if (now != want)
                return N2RAIL_E_VERIFY;
        r->uncertain = false;
        return N2RAIL_OK;
}

static unsigned rise_settle_us(uint32_t delta_uv, unsigned fixed)
{
        /* __calculate_v{gpu,sram}_settletime(rising): dV / 12.5 mV + c. */
        return delta_uv / 12500u + fixed;
}

static enum n2rail_error undo(struct n2rail *r)
{
        enum n2rail_error worst = N2RAIL_OK;
        /* Reverse of on(): VGPU enable, VGPU voltage, VSRAM enable, VSRAM
         * voltage.  Lowering happens only after the rail is off again or
         * when the original value was what the chip ran with before. */
        if (r->vgpu_enabled_by_us) {
                if (upd8(r, N2RAIL_VGPU_EN, 0x01, 0x00) != N2RAIL_OK)
                        worst = N2RAIL_E_ROLLBACK;
                else
                        r->vgpu_enabled_by_us = false;
        }
        if (r->vgpu_raised && worst == N2RAIL_OK) {
                if (upd8(r, N2RAIL_VGPU_VOSEL, 0xff, r->vgpu_orig_sel) !=
                    N2RAIL_OK)
                        worst = N2RAIL_E_ROLLBACK;
                else
                        r->vgpu_raised = false;
        }
        if (r->vsram_enabled_by_us && worst == N2RAIL_OK) {
                if (upd16(r, N2RAIL_VSRAM_CON0, 0x0001, 0x0000) != N2RAIL_OK)
                        worst = N2RAIL_E_ROLLBACK;
                else
                        r->vsram_enabled_by_us = false;
        }
        if (r->vsram_raised && worst == N2RAIL_OK) {
                if (upd16(r, N2RAIL_VSRAM_ELR, 0x007f, r->vsram_orig_elr) !=
                    N2RAIL_OK)
                        worst = N2RAIL_E_ROLLBACK;
                else
                        r->vsram_raised = false;
        }
        if (worst != N2RAIL_OK) {
                r->uncertain = true;
                note(r, "Nord2 GPU rail: rollback incomplete; rails uncertain");
        }
        return worst;
}

static enum n2rail_error fail_on(struct n2rail *r, enum n2rail_error e,
                                 const char *msg)
{
        note(r, msg);
        enum n2rail_error u = undo(r);
        return u != N2RAIL_OK ? u : e;
}

enum n2rail_error n2rail_on(struct n2rail *r, const struct n2rail_point *pt)
{
        if (!r || !pt || pt->index >= N2RAIL_OPP_COUNT)
                return N2RAIL_E_ARGS;
        if (r->on || r->uncertain)
                return N2RAIL_E_STATE;
        r->point = *pt;
        r->step = 1;
        enum n2rail_error e = n2rail_capture(r, &r->before);
        if (e != N2RAIL_OK)
                return e;
        const struct n2rail_snapshot *b = &r->before;
        uint8_t vgpu_sel = b->vgpu_vosel;
        uint8_t vsram_sel = (uint8_t)(b->vsram_elr & 0x7fu);
        uint32_t vgpu_uv = 0, vsram_uv = 0;
        if (!n2rail_vgpu_uv(vgpu_sel, &vgpu_uv) ||
            !n2rail_vsram_uv(vsram_sel, &vsram_uv))
                return N2RAIL_E_SELECTOR;
        /* Never run above the sign-off table: refuse rather than lower. */
        if (vgpu_uv > n2rail_opp_table[0].vgpu_uv)
                return N2RAIL_E_OVERVOLT;
        uint8_t want_vgpu = vgpu_uv >= pt->vgpu_uv
                ? vgpu_sel : n2rail_vgpu_sel_for_uv(pt->vgpu_uv);
        uint8_t want_vsram = vsram_uv >= pt->vsram_uv
                ? vsram_sel : n2rail_vsram_sel_for_uv(pt->vsram_uv);
        uint32_t final_vgpu = 0, final_vsram = 0;
        if (!n2rail_vgpu_uv(want_vgpu, &final_vgpu) ||
            !n2rail_vsram_uv(want_vsram, &final_vsram))
                return N2RAIL_E_SELECTOR;
        int32_t diff = (int32_t)final_vsram - (int32_t)final_vgpu;
        if (diff > N2RAIL_BUCK_DIFF_MAX_UV || diff < N2RAIL_BUCK_DIFF_MIN_UV)
                return N2RAIL_E_DIFF;

        /* 1. VSRAM voltage (rising order: VSRAM before VGPU). */
        r->step = 2;
        if (want_vsram != vsram_sel) {
                r->vsram_orig_elr = vsram_sel;
                r->vsram_raised = true;
                e = upd16(r, N2RAIL_VSRAM_ELR, 0x007f, want_vsram);
                if (e != N2RAIL_OK)
                        return fail_on(r, e, "Nord2 GPU rail: VSRAM vosel");
                if (b->vsram_mon & 1u)
                        r->ops.udelay(r->ctx, rise_settle_us(
                                final_vsram - vsram_uv, 8));
        }
        /* 2. VSRAM enable. */
        r->step = 3;
        if (!(b->vsram_mon & 1u)) {
                r->vsram_enabled_by_us = true;
                e = upd16(r, N2RAIL_VSRAM_CON0, 0x0001, 0x0001);
                if (e != N2RAIL_OK)
                        return fail_on(r, e, "Nord2 GPU rail: VSRAM enable");
                r->ops.udelay(r->ctx, N2RAIL_VSRAM_RAMP_US);
        }
        uint16_t mon = 0, vosel1 = 0;
        if (r->ops.mt6359_read(r->ctx, N2RAIL_VSRAM_MON, &mon) ||
            r->ops.mt6359_read(r->ctx, N2RAIL_VSRAM_VOSEL1, &vosel1))
                return fail_on(r, N2RAIL_E_IO, "Nord2 GPU rail: VSRAM read");
        if (!(mon & 1u) || ((vosel1 >> 8) & 0x7fu) != want_vsram)
                return fail_on(r, N2RAIL_E_VERIFY,
                               "Nord2 GPU rail: VSRAM DA mismatch");
        /* 3. VGPU voltage. */
        r->step = 4;
        if (want_vgpu != vgpu_sel) {
                r->vgpu_orig_sel = vgpu_sel;
                r->vgpu_raised = true;
                e = upd8(r, N2RAIL_VGPU_VOSEL, 0xff, want_vgpu);
                if (e != N2RAIL_OK)
                        return fail_on(r, e, "Nord2 GPU rail: VGPU vosel");
                if (b->vgpu_da_en & 1u)
                        r->ops.udelay(r->ctx, rise_settle_us(
                                final_vgpu - vgpu_uv, 9));
        }
        /* 4. VGPU enable. */
        r->step = 5;
        if (!(b->vgpu_da_en & 1u)) {
                r->vgpu_enabled_by_us = true;
                e = upd8(r, N2RAIL_VGPU_EN, 0x01, 0x01);
                if (e != N2RAIL_OK)
                        return fail_on(r, e, "Nord2 GPU rail: VGPU enable");
                r->ops.udelay(r->ctx, N2RAIL_VGPU_RAMP_US);
        }
        uint8_t da_en = 0, da_sel = 0;
        if (r->ops.mt6315_read(r->ctx, N2RAIL_VGPU_DA_EN, &da_en) ||
            r->ops.mt6315_read(r->ctx, N2RAIL_VGPU_DA_VOSEL, &da_sel))
                return fail_on(r, N2RAIL_E_IO, "Nord2 GPU rail: VGPU read");
        if (!(da_en & 1u) || da_sel != want_vgpu)
                return fail_on(r, N2RAIL_E_VERIFY,
                               "Nord2 GPU rail: VGPU DA mismatch");
        r->step = 6;
        (void)n2rail_capture(r, &r->after);
        r->on = true;
        return N2RAIL_OK;
}

enum n2rail_error n2rail_off(struct n2rail *r)
{
        if (!r)
                return N2RAIL_E_ARGS;
        if (!r->on)
                return N2RAIL_E_STATE;
        r->step = 10;
        enum n2rail_error e = undo(r);
        if (e == N2RAIL_OK)
                r->on = false;
        return e;
}

const char *n2rail_error_string(enum n2rail_error e)
{
        switch (e) {
        case N2RAIL_OK: return "ok";
        case N2RAIL_E_ARGS: return "invalid arguments";
        case N2RAIL_E_IO: return "PMIC transport failure";
        case N2RAIL_E_STATE: return "invalid rail state";
        case N2RAIL_E_FREQ: return "frequency outside vendor table";
        case N2RAIL_E_OVERVOLT: return "voltage above sign-off";
        case N2RAIL_E_SELECTOR: return "selector out of range";
        case N2RAIL_E_DIFF: return "VSRAM/VGPU difference outside limits";
        case N2RAIL_E_VERIFY: return "read-back mismatch";
        case N2RAIL_E_ROLLBACK: return "rollback failed";
        }
        return "unknown";
}
