/* MT6893 LVTS thermal sensing: see nord2_gpu_lvts.h.  SPDX-License-Identifier: MIT */
#include "nord2_gpu_lvts.h"

#include <string.h>

#define CTRL(blk, off, n, first, name) { (blk), (off), (n), (first), (name) }
const struct n2lvts_ctrl_desc n2lvts_ctrl[N2LVTS_CTRL_COUNT] = {
        CTRL(N2LVTS_MCU, 0x000, 2, 0, "cpu-big"),      /* LVTS1: MCU_BIG, MCU_BIGBIG */
        CTRL(N2LVTS_MCU, 0x100, 2, 2, "cpu-big2"),     /* LVTS2 */
        CTRL(N2LVTS_MCU, 0x200, 4, 4, "cpu-little"),   /* LVTS3 */
        CTRL(N2LVTS_AP,  0x000, 2, 8, "vpu-mdla"),     /* LVTS4 */
        CTRL(N2LVTS_AP,  0x100, 2, 10, "gpu"),         /* LVTS5: GPU (T11,T12) */
        CTRL(N2LVTS_AP,  0x200, 2, 12, "infra-cam"),   /* LVTS6 */
        CTRL(N2LVTS_AP,  0x300, 3, 14, "md"),          /* LVTS7 */
};
#undef CTRL

static void note(struct n2lvts *l, const char *m)
{
        if (l->ops.log)
                l->ops.log(l->ctx, m);
}
static uint32_t rd(struct n2lvts *l, unsigned c, uint32_t off)
{
        return l->ops.rd(l->ctx, (enum n2lvts_block)n2lvts_ctrl[c].block, n2lvts_ctrl[c].offset + off);
}
static void wr(struct n2lvts *l, unsigned c, uint32_t off, uint32_t v)
{
        l->ops.wr(l->ctx, (enum n2lvts_block)n2lvts_ctrl[c].block, n2lvts_ctrl[c].offset + off, v);
}

bool n2lvts_init(struct n2lvts *l, const struct n2lvts_ops *ops, void *ctx)
{
        if (!l || !ops || !ops->rd || !ops->wr || !ops->udelay)
                return false;
        memset(l, 0, sizeof(*l));
        l->ops = *ops;
        l->ctx = ctx;
        n2lvts_cal_default(&l->cal);
        return true;
}

/* ---------------- calibration ---------------- */
void n2lvts_cal_default(struct n2lvts_cal *c)
{
        memset(c, 0, sizeof(*c));
        c->defaulted = true;
        c->golden = N2LVTS_DEFAULT_GOLDEN;
        for (unsigned i = 0; i < N2LVTS_SENSOR_COUNT; i++)
                c->count_r[i] = N2LVTS_DEFAULT_COUNT_R;
        for (unsigned i = 0; i < N2LVTS_CTRL_COUNT; i++)
                c->count_rc[i] = N2LVTS_DEFAULT_COUNT_RC;
}

bool n2lvts_cal_plausible(const struct n2lvts_cal *c)
{
        /* Generous physical bounds around the vendor defaults; a decode that leaves them is wrong data. */
        if (!c || c->golden < 40u || c->golden > 120u)
                return false;
        for (unsigned i = 0; i < N2LVTS_SENSOR_COUNT; i++)
                if (c->count_r[i] < 10000u || c->count_r[i] > 100000u)
                        return false;
        for (unsigned i = 0; i < N2LVTS_CTRL_COUNT; i++)
                if (c->count_rc[i] < 1000u || c->count_rc[i] > 6000u)
                        return false;
        return true;
}

bool n2lvts_cal_from_efuse(const uint32_t t[22], struct n2lvts_cal *c)
{
        if (!t || !c)
                return false;
        struct n2lvts_cal x;
        memset(&x, 0, sizeof(x));
        x.golden = (t[0] >> 24) & 0xffu;
        for (unsigned k = 0; k < 17; k++)
                x.count_r[k] = t[k + 1] & 0xffffffu;
        x.count_rc[0] = t[21] & 0xffffffu;
        for (unsigned g = 1; g < N2LVTS_CTRL_COUNT; g++) {
                unsigned w = 1 + 3 * (g - 1);                    /* words 1-3, 4-6, ... 16-18 */
                x.count_rc[g] = ((t[w] & 0xff000000u) >> 8) + ((t[w + 1] & 0xff000000u) >> 16) +
                                ((t[w + 2] & 0xff000000u) >> 24);
        }
        bool any = false;                                        /* vendor test: all zero = unfused sample */
        for (unsigned i = 0; i < N2LVTS_SENSOR_COUNT; i++)
                if (i == 0 ? (t[0] & 0xffu) != 0 : t[i] != 0) { any = true; break; }
        if (!any || !n2lvts_cal_plausible(&x)) {
                n2lvts_cal_default(c);
                return false;
        }
        *c = x;
        c->defaulted = false;
        return true;
}

bool n2lvts_msr_to_mC(uint32_t msr, uint32_t golden, int32_t *mC)
{
        uint32_t raw = msr & 0xffffu;
        if (!(msr & 0x10000u) || !raw)
                return false;
        /* vendor: ((int)-252500 * raw) >> 14 truncates toward -infinity of the magnitude: -ceil(252500*raw/2^14) */
        int64_t t = -(int64_t)(((uint64_t)252500u * raw + 16383u) >> 14);
        t += (int64_t)golden * 500 + 252500;
        if (t < -40000 || t > 150000)
                return false;
        if (mC)
                *mC = (int32_t)t;
        return true;
}

/* ---------------- device access ---------------- */
static bool dev_wait(struct n2lvts *l, unsigned c, uint32_t bit)
{
        for (unsigned n = 0; n < 100; n++) {
                if (!(rd(l, c, N2LVTS_CONFIG) & bit))
                        return true;
                l->ops.udelay(l->ctx, 2);
        }
        return false;
}
static bool dev_write(struct n2lvts *l, unsigned c, uint32_t cfg_hi, uint32_t reg, uint32_t data)
{
        wr(l, c, N2LVTS_CONFIG, cfg_hi | ((reg & 0xffu) << 8) | (data & 0xffu));
        l->ops.udelay(l->ctx, 3);
        return dev_wait(l, c, 1u << 24);
}
static bool dev_read(struct n2lvts *l, unsigned c, uint32_t reg, uint32_t *v)
{
        wr(l, c, N2LVTS_CONFIG, 0x81020000u | ((reg & 0xffu) << 8));
        l->ops.udelay(l->ctx, 5);
        bool ok = dev_wait(l, c, 1u << 24);
        *v = rd(l, c, N2LVTS_RDATA0);
        return ok;
}
#define DW(c, reg, data) do { if (!dev_write(l, (c), 0x81030000u, (reg), (data))) return N2LVTS_E_BUSY; } while (0)

static bool gate_open(struct n2lvts *l)
{
        return !((l->ops.rd(l->ctx, N2LVTS_INFRA, N2LVTS_INFRA_CG_STA) >> N2LVTS_INFRA_THERM_BIT) & 1u);
}

enum n2lvts_error n2lvts_probe_ctrl(struct n2lvts *l, unsigned c, struct n2lvts_probe *p)
{
        if (!l || !p || c >= N2LVTS_CTRL_COUNT)
                return N2LVTS_E_ARGS;
        memset(p, 0, sizeof(*p));
        p->gate_open = gate_open(l);
        if (!p->gate_open)
                return N2LVTS_E_GATED;                   /* nothing else touched */
        p->clken = rd(l, c, N2LVTS_CLKEN);
        p->monctl0 = rd(l, c, N2LVTS_MONCTL0);
        p->id = rd(l, c, N2LVTS_ID);
        for (unsigned j = 0; j < n2lvts_ctrl[c].nsens && j < 4; j++)
                p->msr[j] = rd(l, c, N2LVTS_MSR0 + 4 * j);
        return N2LVTS_OK;
}

/* One controller, vendor order (wrapper: identification, enable/init devices, RC count, efuse, periodic off,
 * data-fetch setup, initial_all_tc, enable).  Interrupts, protection registers and resets are never written. */
static enum n2lvts_error start_ctrl(struct n2lvts *l, unsigned c)
{
        const struct n2lvts_ctrl_desc *d = &n2lvts_ctrl[c];
        l->saved_clken[c] = rd(l, c, N2LVTS_CLKEN);
        l->saved_monctl0[c] = rd(l, c, N2LVTS_MONCTL0);
        l->step = 1;
        wr(l, c, N2LVTS_MONCTL0, 0);                                     /* no sensing while configuring */
        wr(l, c, N2LVTS_CLKEN, 1);
        l->step = 2;
        DW(c, 0xff, 0xff);                                               /* reset devices */
        l->step = 3;
        if (!dev_write(l, c, 0x85020000u, 0xfc, 0x55))                   /* read back device id with update */
                return N2LVTS_E_BUSY;
        if ((rd(l, c, N2LVTS_ID) & 0xffu) != 0x81u + c) {
                note(l, "Nord2 LVTS: device id mismatch, controller left as found");
                return N2LVTS_E_ID;
        }
        l->step = 4;                                                     /* lvts_Device_Enable_Init_all_Devices */
        DW(c, 0x03, 0x00); DW(c, 0x04, 0x20); DW(c, 0x05, 0x00);
        DW(c, 0x0a, 0xc4); DW(c, 0x0c, 0x7c); DW(c, 0x09, 0x8d);
        DW(c, 0x08, 0xf5); DW(c, 0x08, 0xfc); DW(c, 0x08, 0xf5); DW(c, 0x07, 0xa6);
        l->step = 5;                                                     /* lvts_device_read_count_RC_N (manual RCK) */
        uint32_t rc_now[4] = {0, 0, 0, 0};
        DW(c, 0x0e, 0x00);
        for (unsigned j = 0; j < d->nsens; j++) {
                DW(c, 0x0d, j); DW(c, 0x06, 0x78); DW(c, 0x07, 0xa6);
                DW(c, 0x08, 0xf5); DW(c, 0x08, 0xfc); DW(c, 0x08, 0xf5);
                l->ops.udelay(l->ctx, 10);
                DW(c, 0x03, 0x02);                                       /* kick RCK counting */
                l->ops.udelay(l->ctx, 30);
                if (!dev_wait(l, c, 1u << 25))
                        return N2LVTS_E_BUSY;                            /* DEVICE_SENSING_STATUS */
                uint32_t v;
                if (!dev_read(l, c, 0x00, &v))
                        return N2LVTS_E_BUSY;
                l->ops.udelay(l->ctx, 5);
                uint32_t rc = v & 0xffffffu, ref = l->cal.count_rc[c];
                /* LVTS_REFINE_MANUAL_RCK_WITH_EFUSE: a missing or >6% off count is replaced by the efuse value */
                bool off = !rc || (ref > rc ? ref * 1000ull / rc : rc * 1000ull / ref) > 1061u;
                rc_now[j] = off ? ref : rc;
                DW(c, 0x08, 0xf1);
                if (!dev_wait(l, c, 1u << 24))
                        return N2LVTS_E_BUSY;
        }
        DW(c, 0x0d, 0x10);
        l->step = 6;                                                     /* lvts_efuse_setting */
        for (unsigned j = 0; j < d->nsens; j++)
                wr(l, c, N2LVTS_EDATA0 + 4 * j,
                   (uint32_t)(((uint64_t)rc_now[j] * l->cal.count_r[d->first_sensor + j]) >> 14));
        l->step = 7;                                                     /* data fetch + initial_all_tc */
        wr(l, c, N2LVTS_TSSEL, 0x13121110u);
        wr(l, c, N2LVTS_MSRCTL0, 0x53);
        wr(l, c, N2LVTS_CALSCALE, 0x300);
        wr(l, c, N2LVTS_MONCTL1, 0x3ff);
        wr(l, c, N2LVTS_MONCTL2, 0x3ff);
        DW(c, 0x06, 0xb8);                                               /* device single mode */
        wr(l, c, N2LVTS_MONCTL1, (1u << 20) | 0x00cu);                   /* group delay 1, period unit 0x0c */
        wr(l, c, N2LVTS_MONCTL2, (1u << 16) | 1u);                       /* filter 1, sensor interval 1 */
        wr(l, c, N2LVTS_MSRCTL0, 0x492);                                 /* 2 of 4 samples */
        l->ops.udelay(l->ctx, 1);
        l->step = 8;
        wr(l, c, N2LVTS_MONCTL0, 0x200u | ((1u << d->nsens) - 1u));      /* enable sensing points */
        return N2LVTS_OK;
}

static void restore_ctrl(struct n2lvts *l, unsigned c)
{
        wr(l, c, N2LVTS_MONCTL0, 0);
        (void)dev_write(l, c, 0x81030000u, 0xff, 0xff);
        wr(l, c, N2LVTS_CLKEN, l->saved_clken[c]);
        if (l->saved_monctl0[c])
                wr(l, c, N2LVTS_MONCTL0, l->saved_monctl0[c]);
}

enum n2lvts_error n2lvts_start(struct n2lvts *l, uint32_t mask, bool open_gate)
{
        if (!l || !mask || (mask & ~N2LVTS_MASK_ALL))
                return N2LVTS_E_ARGS;
        if (!n2lvts_cal_plausible(&l->cal))
                return N2LVTS_E_CAL;
        if (!gate_open(l)) {
                if (!open_gate)
                        return N2LVTS_E_GATED;
                l->ops.wr(l->ctx, N2LVTS_INFRA, N2LVTS_INFRA_CG_CLR, 1u << N2LVTS_INFRA_THERM_BIT);
                l->ops.udelay(l->ctx, 10);
                if (!gate_open(l))
                        return N2LVTS_E_GATED;
                l->ungated_by_us = true;
        }
        enum n2lvts_error e = N2LVTS_OK;
        for (unsigned c = 0; c < N2LVTS_CTRL_COUNT; c++) {
                if (!(mask & (1u << c)))
                        continue;
                enum n2lvts_error r = start_ctrl(l, c);
                if (r != N2LVTS_OK) {
                        restore_ctrl(l, c);
                        e = r;
                        break;
                }
                l->running |= 1u << c;
        }
        if (e != N2LVTS_OK)
                n2lvts_stop(l);                              /* all-or-nothing for what we started */
        return e;
}

unsigned n2lvts_read(struct n2lvts *l, uint32_t mask)
{
        unsigned good = 0;
        if (!l || !gate_open(l))
                return 0;
        for (unsigned c = 0; c < N2LVTS_CTRL_COUNT; c++) {
                if (!(mask & (1u << c)))
                        continue;
                bool sensing = (rd(l, c, N2LVTS_MONCTL0) & 0x200u) != 0;
                for (unsigned j = 0; j < n2lvts_ctrl[c].nsens; j++) {
                        unsigned s = n2lvts_ctrl[c].first_sensor + j;
                        uint32_t msr = sensing ? rd(l, c, N2LVTS_MSR0 + 4 * j) : 0;
                        l->raw[s] = msr & 0xffffu;
                        l->temp_ok[s] = sensing && n2lvts_msr_to_mC(msr, l->cal.golden, &l->temp_mC[s]);
                        good += l->temp_ok[s];
                }
        }
        return good;
}

void n2lvts_stop(struct n2lvts *l)
{
        if (!l)
                return;
        for (unsigned c = 0; c < N2LVTS_CTRL_COUNT; c++)
                if (l->running & (1u << c))
                        restore_ctrl(l, c);
        l->running = 0;
        if (l->ungated_by_us) {
                /* Re-gating needs the SET alias (0x080); the gate was closed when we found it. */
                l->ops.wr(l->ctx, N2LVTS_INFRA, 0x080u, 1u << N2LVTS_INFRA_THERM_BIT);
                l->ungated_by_us = false;
        }
}

const char *n2lvts_error_string(enum n2lvts_error e)
{
        switch (e) {
        case N2LVTS_OK: return "ok";
        case N2LVTS_E_ARGS: return "bad arguments";
        case N2LVTS_E_GATED: return "thermal clock gate closed";
        case N2LVTS_E_CAL: return "calibration implausible";
        case N2LVTS_E_BUSY: return "device access timed out";
        case N2LVTS_E_ID: return "device id mismatch";
        case N2LVTS_E_NODATA: return "no valid sample";
        }
        return "?";
}

/* ---------------- guard and throttle policy ---------------- */
bool n2lvts_allows_cold_start_opp(int32_t temp_mC, bool valid, bool calibrated, int32_t max_mC)
{
        if (!valid)
                return true;
        return temp_mC <= (calibrated ? max_mC : max_mC - 10000);
}

uint8_t n2lvts_throttle_step(struct n2lvts_throttle *t, int32_t temp_mC, bool valid)
{
        if (!valid) {
                if (t->invalid_run < 255)
                        t->invalid_run++;
                if (t->invalid_run >= N2LVTS_THROTTLE_INVALID_MAX && t->level < 1)
                        t->level = 1;                    /* unknown temperature: cap, never minimum */
                return t->level;
        }
        t->invalid_run = 0;
        if (temp_mC >= t->hard_mC)
                t->level = 2;
        else if (temp_mC >= t->soft_mC && t->level < 1)
                t->level = 1;
        else if (t->level == 2 && temp_mC < t->hard_mC - t->hyst_mC)
                t->level = 1;
        else if (t->level == 1 && temp_mC < t->soft_mC - t->hyst_mC)
                t->level = 0;
        return t->level;
}

uint32_t n2lvts_throttle_khz_cap(uint8_t level)
{
        return level == 0 ? 886000u : level == 1 ? 654000u : 350000u;
}
