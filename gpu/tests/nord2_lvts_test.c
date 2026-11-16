/* LVTS thermal: conversion, calibration decode, init sequence and guards against a controller/device register model.
 * Protocol/contract checks derived from the vendor driver; not evidence of silicon behaviour. */
#include "nord2_gpu_lvts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define N 7
struct model {
        uint32_t infra_sta;                   /* bit 10: 1 = thermal clock gated */
        uint32_t reg[N][0x100 / 4];           /* controller registers */
        uint8_t dev[N][256];                  /* LVTS device registers */
        uint32_t rc_count[N][4];              /* what the device reports for RCK on sensor j */
        uint32_t raw[N][4];                   /* MSR raw count the "silicon" produces once sensing */
        int busy_reads[N];                    /* CONFIG reads that still show DEVICE_ACCESS_START after an access */
        bool stuck[N];
        unsigned wrong_id;                    /* controller whose device id is wrong (1+index), 0 none */
        unsigned reads_ctrl, writes_ctrl[N], writes_infra, forbidden, touched_wrong, gate_reads_before_ctrl;
        bool ctrl_read_while_gated;
        unsigned udelays;
        uint32_t last_edata[N][4];
};
static struct model m;

static int ctrl_of(enum n2lvts_block b, uint32_t off, uint32_t *rel)
{
        for (unsigned c = 0; c < N; c++)
                if (n2lvts_ctrl[c].block == b && off >= n2lvts_ctrl[c].offset && off < n2lvts_ctrl[c].offset + 0x100u) {
                        *rel = off - n2lvts_ctrl[c].offset;
                        return (int)c;
                }
        return -1;
}
static uint32_t rd(void *ctx, enum n2lvts_block b, uint32_t off)
{
        (void)ctx;
        if (b == N2LVTS_INFRA) return off == N2LVTS_INFRA_CG_STA ? m.infra_sta : 0;
        uint32_t rel; int c = ctrl_of(b, off, &rel);
        assert(c >= 0 && !(rel & 3));
        m.reads_ctrl++;
        if (m.infra_sta & (1u << 10)) m.ctrl_read_while_gated = true;
        if (rel == N2LVTS_CONFIG) {
                uint32_t v = m.reg[c][rel / 4];
                if (m.stuck[c]) return v | (1u << 24);
                if (m.busy_reads[c] > 0) { m.busy_reads[c]--; return v | (1u << 24); }
                return v & ~(3u << 24);
        }
        if (rel >= N2LVTS_MSR0 && rel < N2LVTS_MSR0 + 16) {
                unsigned j = (rel - N2LVTS_MSR0) / 4;
                return (m.reg[c][N2LVTS_MONCTL0 / 4] & 0x200u) && (m.reg[c][N2LVTS_MONCTL0 / 4] & (1u << j)) ? (0x10000u | m.raw[c][j]) : 0;
        }
        return m.reg[c][rel / 4];
}
static void wr(void *ctx, enum n2lvts_block b, uint32_t off, uint32_t v)
{
        (void)ctx;
        if (b == N2LVTS_INFRA) {
                m.writes_infra++;
                if (off == N2LVTS_INFRA_CG_CLR) m.infra_sta &= ~v;
                else if (off == 0x080u) m.infra_sta |= v;
                else assert(!"unexpected infra write");
                return;
        }
        uint32_t rel; int c = ctrl_of(b, off, &rel);
        assert(c >= 0 && !(rel & 3));
        m.writes_ctrl[c]++;
        /* never: interrupt enable, protection, debug and spare registers */
        if (rel == N2LVTS_MONINT || (rel >= 0xc0 && rel <= 0xcc) || rel >= 0xe8 || (rel >= 0x10 && rel <= 0x20) || (rel >= 0x24 && rel <= 0x34))
                m.forbidden++;
        if (rel >= N2LVTS_EDATA0 && rel < N2LVTS_EDATA0 + 16) m.last_edata[c][(rel - N2LVTS_EDATA0) / 4] = v;
        if (rel == N2LVTS_CONFIG) {
                m.reg[c][rel / 4] = v & 0x00ffffffu;
                if (v & (1u << 24)) {
                        uint32_t op = (v >> 16) & 0xff, reg = (v >> 8) & 0xff, data = v & 0xff;
                        if (op == 0x03 || op == 0x02) {
                                if (op == 0x03) {
                                        if (reg == 0xff && data == 0xff) memset(m.dev[c], 0, 256);
                                        else m.dev[c][reg] = (uint8_t)data;
                                        if (reg == 0x08 && data == 0xf5 && m.dev[c][0x0d] < 4) m.dev[c][0xee] = (uint8_t)m.dev[c][0x0d]; /* selected sensor */
                                        if (reg == 0x03 && data == 0x02) m.dev[c][0xef] = 1; /* RCK counting done */
                                } else {
                                        m.reg[c][N2LVTS_RDATA0 / 4] = reg == 0x00 ? m.rc_count[c][m.dev[c][0x0d] & 3] : m.dev[c][reg];
                                }
                        }
                        if (reg == 0xfc && data == 0x55)                      /* read ID with update */
                                m.reg[c][N2LVTS_ID / 4] = (m.wrong_id == c + 1u) ? 0x99u : 0x81u + c;
                }
                return;
        }
        m.reg[c][rel / 4] = v;
}
static void udelay(void *ctx, unsigned us) { (void)ctx; (void)us; m.udelays++; }
static struct n2lvts_ops ops = { rd, wr, udelay, NULL };

static void reset_model(void)
{
        memset(&m, 0, sizeof(m));
        for (unsigned c = 0; c < N; c++) { m.reg[c][N2LVTS_CLKEN / 4] = 0; for (unsigned j = 0; j < 4; j++) { m.rc_count[c][j] = 2750; m.raw[c][j] = 16384; } }
}
static int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

int main(void)
{
        /* ---- conversion (vendor formula) ---- */
        int32_t t;
        assert(n2lvts_msr_to_mC(0x10000u | 16384u, 50, &t) && t == 25000);          /* raw 2^14, golden 25 C */
        assert(n2lvts_msr_to_mC(0x10000u | 16384u, 56, &t) && t == 28000);          /* golden 28 C */
        assert(n2lvts_msr_to_mC(0x10000u | 12000u, 50, &t) && abs32(t - (int32_t)(277500 - 252500.0 * 12000 / 16384)) <= 2);
        assert(n2lvts_msr_to_mC(0x10000u | 11000u, 50, &t) && t > 80000);           /* hotter = smaller count */
        assert(!n2lvts_msr_to_mC(16384u, 50, &t));                                  /* valid bit clear */
        assert(!n2lvts_msr_to_mC(0x10000u, 50, &t));                                /* raw 0 */
        assert(!n2lvts_msr_to_mC(0x10000u | 100u, 50, &t));                         /* 276 C: implausible */
        assert(!n2lvts_msr_to_mC(0x10000u | 0xffffu, 50, &t));                      /* below -40 C */
        /* ---- calibration ---- */
        struct n2lvts_cal cal;
        n2lvts_cal_default(&cal);
        assert(cal.defaulted && cal.golden == 50 && n2lvts_cal_plausible(&cal));
        uint32_t ef[22] = {0};
        assert(!n2lvts_cal_from_efuse(ef, &cal) && cal.defaulted);                  /* unfused: vendor defaults */
        /* implausible rc bytes (24-bit sum of the top bytes of three words) are refused as a whole */
        ef[0] = (56u << 24) | 1u;
        for (unsigned k = 1; k <= 17; k++) ef[k] = ((0x0au + k) << 24) | (33000u + 100u * k);
        ef[21] = 2800u;
        assert(!n2lvts_cal_from_efuse(ef, &cal) && cal.defaulted && cal.golden == 50);
        /* implausible golden temperature (100 C) */
        for (unsigned k = 1; k <= 17; k++) ef[k] = 33000u;
        memset(ef + 18, 0, 3 * 4); ef[0] = (200u << 24) | 1u; ef[21] = 2800u;
        assert(!n2lvts_cal_from_efuse(ef, &cal) && cal.defaulted && cal.golden == 50);
        /* plausible fused sample: rc of every controller 2750 = bytes 0x00,0x0a,0xbe in the top bytes of three words */
        ef[0] = (56u << 24) | 1u;
        for (unsigned k = 1; k <= 17; k++) ef[k] = 33000u + 100u * k;
        for (unsigned g = 0; g < 6; g++) { ef[2 + 3 * g] |= 0x0au << 24; ef[3 + 3 * g] |= 0xbeu << 24; }
        assert(n2lvts_cal_from_efuse(ef, &cal) && !cal.defaulted && cal.golden == 56);
        assert(cal.count_r[0] == 33100 && cal.count_r[16] == 34700 && cal.count_rc[0] == 2800);
        for (unsigned g = 1; g < 7; g++) assert(cal.count_rc[g] == 2750);
        assert(n2lvts_cal_plausible(&cal));

        /* ---- read-only probe never touches a gated block ---- */
        struct n2lvts l; struct n2lvts_probe p;
        reset_model(); m.infra_sta = 1u << 10;
        assert(n2lvts_init(&l, &ops, NULL));
        assert(n2lvts_probe_ctrl(&l, N2LVTS_CTRL_GPU, &p) == N2LVTS_E_GATED && !p.gate_open && !m.reads_ctrl && !m.writes_infra);
        assert(n2lvts_read(&l, N2LVTS_MASK_GPU) == 0 && !m.reads_ctrl);
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, false) == N2LVTS_E_GATED && !m.writes_ctrl[4] && !m.writes_infra);
        m.infra_sta = 0;
        assert(n2lvts_probe_ctrl(&l, N2LVTS_CTRL_GPU, &p) == N2LVTS_OK && p.gate_open && !p.monctl0 && !m.writes_ctrl[4]);
        assert(n2lvts_read(&l, N2LVTS_MASK_GPU) == 0);                              /* not sensing: no data, no writes */
        assert(!m.writes_ctrl[4] && n2lvts_probe_ctrl(&l, 7, &p) == N2LVTS_E_ARGS);
        /* an already-running controller (bootloader/firmware) is readable without any write */
        m.reg[4][N2LVTS_MONCTL0 / 4] = 0x203;
        assert(n2lvts_read(&l, N2LVTS_MASK_GPU) == 2 && l.temp_ok[10] && l.temp_ok[11] && l.temp_mC[10] == 25000 && !m.writes_ctrl[4]);

        /* ---- vendor init of the GPU controller only ---- */
        reset_model(); m.reg[4][N2LVTS_CLKEN / 4] = 0; n2lvts_init(&l, &ops, NULL);
        m.rc_count[4][0] = 2750; m.rc_count[4][1] = 2800;                          /* within 6%: measured value used */
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, false) == N2LVTS_OK && l.running == N2LVTS_MASK_GPU);
        for (unsigned c = 0; c < N; c++) if (c != 4) assert(m.writes_ctrl[c] == 0);
        assert(m.forbidden == 0 && m.writes_infra == 0);
        assert(m.reg[4][N2LVTS_MONCTL0 / 4] == 0x203 && m.reg[4][N2LVTS_CLKEN / 4] == 1);
        assert(m.reg[4][N2LVTS_TSSEL / 4] == 0x13121110u && m.reg[4][N2LVTS_CALSCALE / 4] == 0x300 && m.reg[4][N2LVTS_MSRCTL0 / 4] == 0x492);
        assert(m.reg[4][N2LVTS_MONCTL1 / 4] == ((1u << 20) | 0xc) && m.reg[4][N2LVTS_MONCTL2 / 4] == ((1u << 16) | 1));
        assert(m.last_edata[4][0] == (uint32_t)(((uint64_t)2750 * 35000) >> 14) && m.last_edata[4][1] == (uint32_t)(((uint64_t)2800 * 35000) >> 14));
        assert(m.dev[4][0x06] == 0xb8 && m.dev[4][0x0d] == 0x10 && m.dev[4][0x04] == 0x20 && m.dev[4][0x0c] == 0x7c);
        assert(m.reg[4][N2LVTS_ID / 4] == 0x85);
        m.raw[4][0] = 16384; m.raw[4][1] = 15000;
        assert(n2lvts_read(&l, N2LVTS_MASK_GPU) == 2 && l.temp_mC[10] == 25000 && l.temp_mC[11] > 30000 && l.temp_mC[11] < 60000);
        assert(!m.ctrl_read_while_gated);
        n2lvts_stop(&l);
        assert(l.running == 0 && m.reg[4][N2LVTS_MONCTL0 / 4] == 0 && m.reg[4][N2LVTS_CLKEN / 4] == 0 && m.forbidden == 0);

        /* RC refinement: zero or >6% off count falls back to the efuse RC value */
        reset_model(); n2lvts_init(&l, &ops, NULL);
        m.rc_count[4][0] = 0; m.rc_count[4][1] = 3200;                              /* 16% off */
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, false) == N2LVTS_OK);
        assert(m.last_edata[4][0] == (uint32_t)(((uint64_t)2750 * 35000) >> 14) && m.last_edata[4][1] == (uint32_t)(((uint64_t)2750 * 35000) >> 14));
        n2lvts_stop(&l);

        /* ---- failures leave the controller as found ---- */
        reset_model(); n2lvts_init(&l, &ops, NULL);
        m.reg[4][N2LVTS_CLKEN / 4] = 0; m.reg[4][N2LVTS_MONCTL0 / 4] = 0x201; m.wrong_id = 5;   /* ctrl 4 = id 0x85 expected, model says 0x99 */
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, false) == N2LVTS_E_ID && l.running == 0);
        assert(m.reg[4][N2LVTS_CLKEN / 4] == 0 && m.reg[4][N2LVTS_MONCTL0 / 4] == 0x201 && m.forbidden == 0);
        reset_model(); n2lvts_init(&l, &ops, NULL); m.stuck[4] = true;
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, false) == N2LVTS_E_BUSY && l.running == 0 && m.reg[4][N2LVTS_CLKEN / 4] == 0);
        reset_model(); n2lvts_init(&l, &ops, NULL); l.cal.golden = 5;
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, false) == N2LVTS_E_CAL && !m.writes_ctrl[4]);   /* implausible calibration: nothing written */
        assert(n2lvts_start(&l, 0, false) == N2LVTS_E_ARGS && n2lvts_start(&l, 1u << 9, false) == N2LVTS_E_ARGS);

        /* ---- level 3: opening and re-closing the clock gate ---- */
        reset_model(); m.infra_sta = 1u << 10; n2lvts_init(&l, &ops, NULL);
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, true) == N2LVTS_OK && l.ungated_by_us && !(m.infra_sta & (1u << 10)) && !m.ctrl_read_while_gated);
        n2lvts_stop(&l);
        assert((m.infra_sta & (1u << 10)) && !l.ungated_by_us);
        reset_model(); m.infra_sta = 1u << 10; n2lvts_init(&l, &ops, NULL); m.wrong_id = 5;
        assert(n2lvts_start(&l, N2LVTS_MASK_GPU, true) == N2LVTS_E_ID && (m.infra_sta & (1u << 10)));   /* failure re-gates */

        /* ---- guard and throttle design ---- */
        assert(n2lvts_allows_cold_start_opp(60000, true, true, 65000) && !n2lvts_allows_cold_start_opp(66000, true, true, 65000));
        assert(!n2lvts_allows_cold_start_opp(56000, true, false, 65000) && n2lvts_allows_cold_start_opp(54000, true, false, 65000));  /* 10 C margin */
        assert(n2lvts_allows_cold_start_opp(99000, false, true, 65000));                                                             /* unknown never blocks */
        struct n2lvts_throttle th = { 70000, 80000, 5000, 0, 0 };
        assert(n2lvts_throttle_step(&th, 69999, true) == 0 && n2lvts_throttle_step(&th, 70000, true) == 1);
        assert(n2lvts_throttle_step(&th, 66000, true) == 1);                         /* inside hysteresis */
        assert(n2lvts_throttle_step(&th, 64999, true) == 0);
        assert(n2lvts_throttle_step(&th, 85000, true) == 2 && n2lvts_throttle_step(&th, 76000, true) == 2);
        assert(n2lvts_throttle_step(&th, 74999, true) == 1);
        for (unsigned i = 0; i < N2LVTS_THROTTLE_INVALID_MAX; i++) n2lvts_throttle_step(&th, 0, false);
        assert(th.level == 1);                                                       /* unreadable: stays capped, not minimum */
        struct n2lvts_throttle th2 = { 70000, 80000, 5000, 0, 0 };
        for (unsigned i = 0; i < 20; i++) n2lvts_throttle_step(&th2, 0, false);
        assert(th2.level == 1 && n2lvts_throttle_khz_cap(0) == 886000 && n2lvts_throttle_khz_cap(1) == 654000 && n2lvts_throttle_khz_cap(2) == 350000);
        puts("nord2_lvts_test: conversion, efuse decode, gated-probe safety, GPU-only init order, forbidden-register check, fail-safe restore, guard and throttle design pass (register model)");
        return 0;
}
