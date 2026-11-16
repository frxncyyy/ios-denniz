/*
 * Host tests: PMIF SWINF transport and MT6893 GPU rail policy.
 * Models are protocol/contract checks, not evidence of PMIC behaviour.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_pmif.h"
#include "nord2_gpu_rail.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { failures++; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------------- PMIF FSM model ---------------- */
struct pmif_model {
        bool pwrap;
        uint32_t init_done;
        uint32_t fsm;            /* 0 idle, 2 req, 6 wfvldclr */
        int busy_reads;          /* reads of STA before a request completes */
        int countdown;
        bool pending_read;
        bool foreign_busy;       /* stays in REQ forever */
        bool never_complete;
        uint32_t last_acc, last_wdata, rdata;
        uint8_t spmi[16][0x2000];
        uint16_t pmic[0x2000];
        unsigned acc_writes;
};

static uint32_t pm_rd(void *ctx, uint32_t off)
{
        struct pmif_model *m = ctx;
        if (off == N2PMIF_INIT_DONE)
                return m->init_done;
        if (off == N2PMIF_SWINF2_RDATA)
                return m->rdata;
        if (off == N2PMIF_SWINF2_STA) {
                if (m->foreign_busy)
                        return (m->pwrap ? N2PMIF_PWRAP_STA_READY : 0) | (2u << 1);
                if (m->fsm == 2 && !m->never_complete && --m->countdown <= 0)
                        m->fsm = m->pending_read ? 6 : 0;
                return (m->pwrap ? N2PMIF_PWRAP_STA_READY : 0) | (m->fsm << 1);
        }
        return 0;
}

static void pm_wr(void *ctx, uint32_t off, uint32_t v)
{
        struct pmif_model *m = ctx;
        if (off == N2PMIF_SWINF2_WDATA) {
                m->last_wdata = v;
                return;
        }
        if (off == N2PMIF_SWINF2_VLD_CLR) {
                if (m->fsm == 6)
                        m->fsm = 0;
                return;
        }
        if (off != N2PMIF_SWINF2_ACC)
                return;
        if (m->fsm != 0)
                failures++, fprintf(stderr, "FAIL ACC write while not idle\n");
        m->acc_writes++;
        m->last_acc = v;
        bool write = (v >> 29) & 1u;
        uint16_t addr = (uint16_t)v;
        if (m->pwrap) {
                if (write)
                        m->pmic[addr / 2] = (uint16_t)m->last_wdata;
                else
                        m->rdata = m->pmic[addr / 2];
        } else {
                unsigned sid = (v >> 24) & 0xf;
                if (((v >> 30) & 3u) != N2PMIF_CMD_EXT_REG_LONG || ((v >> 16) & 0xff))
                        failures++, fprintf(stderr, "FAIL SPMI opcode/bc\n");
                if (write)
                        m->spmi[sid][addr] = (uint8_t)m->last_wdata;
                else
                        m->rdata = m->spmi[sid][addr];
        }
        m->pending_read = !write;
        m->fsm = 2;
        m->countdown = m->busy_reads;
}

static void pm_delay(void *ctx, unsigned us) { (void)ctx; (void)us; }
static const struct n2pmif_ops pm_ops = { pm_rd, pm_wr, pm_delay };

static void test_pmif(void)
{
        struct pmif_model m;
        struct n2pmif p;
        memset(&m, 0, sizeof(m));
        m.busy_reads = 3;
        CHECK(n2pmif_init(&p, N2PMIF_SPMI, &pm_ops, &m));
        CHECK(n2pmif_check_ready(&p) == N2PMIF_E_NOT_INIT);
        m.init_done = 1;
        CHECK(n2pmif_check_ready(&p) == N2PMIF_OK);
        m.spmi[7][0x1449] = 0x5a;
        uint8_t v = 0;
        CHECK(n2pmif_spmi_read(&p, 7, 0x1449, &v) == N2PMIF_OK && v == 0x5a);
        CHECK(m.last_acc == ((3u << 30) | (7u << 24) | 0x1449u));
        CHECK(m.fsm == 0);
        CHECK(n2pmif_spmi_write(&p, 7, 0x1440, 0x09) == N2PMIF_OK);
        CHECK(m.last_acc == ((3u << 30) | (1u << 29) | (7u << 24) | 0x1440u));
        CHECK(m.spmi[7][0x1440] == 0x09);
        CHECK(n2pmif_spmi_read(&p, 16, 0, &v) == N2PMIF_E_ARGS);
        CHECK(n2pmif_pwrap_read(&p, 0x10, (uint16_t *)&v) == N2PMIF_E_ARGS);

        /* Our own stale WFVLDCLR is acknowledged first. */
        m.fsm = 6;
        CHECK(n2pmif_spmi_read(&p, 7, 0x1449, &v) == N2PMIF_OK);
        /* Foreign transaction never drains: bounded BUSY, no ACC write. */
        unsigned acc = m.acc_writes;
        m.foreign_busy = true;
        CHECK(n2pmif_spmi_write(&p, 7, 0x1440, 1) == N2PMIF_E_BUSY);
        CHECK(m.acc_writes == acc);
        m.foreign_busy = false;
        /* Stale WFVLDCLR not ours: not cleared, BUSY. */
        struct n2pmif fresh;
        n2pmif_init(&fresh, N2PMIF_SPMI, &pm_ops, &m);
        m.fsm = 6;
        CHECK(n2pmif_spmi_read(&fresh, 7, 0x1449, &v) == N2PMIF_E_BUSY);
        m.fsm = 0;
        /* Read that never completes: TIMEOUT, then next call recovers. */
        m.never_complete = true;
        CHECK(n2pmif_spmi_read(&p, 7, 0x1449, &v) == N2PMIF_E_TIMEOUT);
        m.never_complete = false;
        m.fsm = 6; /* the request eventually landed */
        CHECK(n2pmif_spmi_read(&p, 7, 0x1449, &v) == N2PMIF_OK);

        /* PWRAP encoding identical to the existing Nord 2 helper. */
        struct pmif_model w;
        memset(&w, 0, sizeof(w));
        w.pwrap = true;
        w.busy_reads = 2;
        w.pmic[0x1f08 / 2] = 0x1234;
        CHECK(n2pmif_init(&p, N2PMIF_PWRAP, &pm_ops, &w));
        uint16_t x = 0;
        CHECK(n2pmif_pwrap_read(&p, 0x1f08, &x) == N2PMIF_OK && x == 0x1234);
        CHECK(w.last_acc == 0x1f08u);
        CHECK(n2pmif_pwrap_write(&p, 0x1f08, 0x1235) == N2PMIF_OK);
        CHECK(w.last_acc == ((1u << 29) | 0x1f08u) && w.last_wdata == 0x1235);
        CHECK(n2pmif_pwrap_write(&p, 0x1f09, 0) == N2PMIF_E_ARGS);
        CHECK(n2pmif_spmi_read(&p, 7, 0, &v) == N2PMIF_E_ARGS);
}

/* ---------------- Rail model ---------------- */
struct rail_model {
        uint8_t m15[0x2000];
        uint16_t m59[0x2000];
        unsigned op;             /* counts every read/write */
        unsigned fail_at;        /* 1-based op to fail; 0 never */
        unsigned fail_at2;       /* second injected failure */
        bool ignore_en_write;    /* RG accepts but DA never follows */
        bool da_vosel_wrong;
        unsigned writes;
        uint16_t waddr[64];
        unsigned wop[64];        /* op number of each write */
};

static void sync_da(struct rail_model *m)
{
        m->m15[N2RAIL_VGPU_DA_EN] = m->ignore_en_write ? 0 :
                                     (m->m15[N2RAIL_VGPU_EN] & 1u);
        m->m15[N2RAIL_VGPU_DA_VOSEL] = (uint8_t)(m->m15[N2RAIL_VGPU_VOSEL] +
                                                 (m->da_vosel_wrong ? 1 : 0));
        uint16_t sel = m->m59[N2RAIL_VSRAM_ELR / 2] & 0x7f;
        m->m59[N2RAIL_VSRAM_MON / 2] = m->m59[N2RAIL_VSRAM_CON0 / 2] & 1u;
        m->m59[N2RAIL_VSRAM_VOSEL1 / 2] = (uint16_t)(sel << 8);
}

static int r15(void *ctx, uint16_t a, uint8_t *v)
{
        struct rail_model *m = ctx;
        if (++m->op == m->fail_at || m->op == m->fail_at2)
                return -1;
        sync_da(m);
        *v = m->m15[a];
        return 0;
}
static int w15(void *ctx, uint16_t a, uint8_t v)
{
        struct rail_model *m = ctx;
        if (++m->op == m->fail_at || m->op == m->fail_at2)
                return -1;
        if (m->writes < 64) {
                m->waddr[m->writes] = a;
                m->wop[m->writes] = m->op;
        }
        m->writes++;
        m->m15[a] = v;
        return 0;
}
static int r59(void *ctx, uint16_t a, uint16_t *v)
{
        struct rail_model *m = ctx;
        if (++m->op == m->fail_at || m->op == m->fail_at2)
                return -1;
        sync_da(m);
        *v = m->m59[a / 2];
        return 0;
}
static int w59(void *ctx, uint16_t a, uint16_t v)
{
        struct rail_model *m = ctx;
        if (++m->op == m->fail_at || m->op == m->fail_at2)
                return -1;
        if (m->writes < 64) {
                m->waddr[m->writes] = a;
                m->wop[m->writes] = m->op;
        }
        m->writes++;
        m->m59[a / 2] = v;
        return 0;
}
static void rdelay(void *ctx, unsigned us) { (void)ctx; (void)us; }
static const struct n2rail_ops rail_ops = { r15, w15, r59, w59, rdelay, NULL };

static void rail_cold(struct rail_model *m)
{
        memset(m, 0, sizeof(*m));
        m->m15[N2RAIL_VGPU_EN] = 0x04;               /* vbuck3 (CPU) on */
        m->m15[N2RAIL_VGPU_VOSEL] = 0x30 + 40;       /* 0.55 V */
        m->m59[N2RAIL_VSRAM_ELR / 2] = 0x2a80 | 24;  /* 0.65 V, upper bits kept */
        m->m59[N2RAIL_VSRAM_CON0 / 2] = 0x0200;      /* off, other bits */
}

static void test_tables(void)
{
        CHECK(n2rail_opp_table[0].khz == 886000 &&
              n2rail_opp_table[0].vgpu_uv == 800000);
        CHECK(n2rail_opp_table[25].vgpu_uv == 650000); /* segment 2 */
        CHECK(n2rail_opp_table[40].khz == 350000 &&
              n2rail_opp_table[40].vgpu_uv == 600000 &&
              n2rail_opp_table[40].posdiv_power == 3);
        for (unsigned i = 1; i < N2RAIL_OPP_COUNT; i++) {
                CHECK(n2rail_opp_table[i].khz < n2rail_opp_table[i - 1].khz);
                CHECK(n2rail_opp_table[i].vgpu_uv <= n2rail_opp_table[i - 1].vgpu_uv);
                int diff = (int)n2rail_opp_table[i].vsram_uv -
                           (int)n2rail_opp_table[i].vgpu_uv;
                CHECK(diff >= 0 && diff <= N2RAIL_BUCK_DIFF_MAX_UV);
        }
        /* Vendor PLL decode: pcw for 886 MHz at posdiv /4. */
        uint32_t pcw = (uint32_t)((886ull * 4 << 14) / 26);
        uint32_t khz = n2rail_mfgpll_khz((2u << 24) | 0x80000000u | pcw);
        CHECK(khz >= 885000 && khz <= 886000);
        struct n2rail_point pt;
        CHECK(n2rail_point_for_khz(886000, &pt) && pt.index == 0);
        CHECK(n2rail_point_for_khz(880000, &pt) && pt.index == 0 &&
              pt.vgpu_uv == 800000);
        CHECK(n2rail_point_for_khz(879000, &pt) && pt.index == 1);
        CHECK(n2rail_point_for_khz(400000, &pt) && pt.index == 37);
        CHECK(n2rail_point_for_khz(100000, &pt) && pt.index == 40);
        CHECK(!n2rail_point_for_khz(887000, &pt));
        /* 0091: every vendor OPP is an exact cold-start target: its DDS word
         * (Android __mt_gpufreq_calculate_dds) reads back as exactly that
         * frequency, the VCO stays in range, and the table is strictly descending. */
        for (unsigned i = 0; i < N2RAIL_OPP_COUNT; i++) {
                const struct n2rail_opp *o = &n2rail_opp_table[i];
                unsigned idx = 99;
                CHECK(n2rail_opp_index_exact(o->khz, &idx) && idx == i);
                uint32_t dds = (((o->khz / 100u * (1u << o->posdiv_power)) << 14) / 26u + 5u) / 10u;
                CHECK(n2rail_mfgpll_khz((o->posdiv_power << 24) | dds) == o->khz);
                CHECK(o->khz * (1u << o->posdiv_power) >= 1500000u && o->khz * (1u << o->posdiv_power) <= 3800000u);
                CHECK(i == 0 || n2rail_opp_table[i - 1].khz > o->khz);
                CHECK(n2rail_point_for_khz(o->khz, &pt) && pt.index == i);
        }
        CHECK(!n2rail_opp_index_exact(600000, NULL) && !n2rail_opp_index_exact(0, NULL) && n2rail_opp_index_exact(350000, NULL));
        CHECK(!n2rail_point_for_khz(0, &pt));
        uint32_t uv = 0;
        CHECK(n2rail_vgpu_uv(0x30, &uv) && uv == 300000);
        CHECK(n2rail_vgpu_uv(0xbf, &uv) && uv == 1193750);
        CHECK(!n2rail_vgpu_uv(0x2f, &uv) && !n2rail_vgpu_uv(0xc0, &uv));
        CHECK(n2rail_vgpu_sel_for_uv(800000) == 0x30 + 80);
        CHECK(n2rail_vgpu_sel_for_uv(800001) == 0x30 + 81);
        CHECK(n2rail_vsram_sel_for_uv(750000) == 40);
        CHECK(n2rail_vsram_uv(0x6f, &uv) && uv == 1193750 &&
              !n2rail_vsram_uv(0x70, &uv));
}

static void test_rail_cold_on_off(void)
{
        struct rail_model m;
        struct n2rail r;
        struct n2rail_point pt;
        rail_cold(&m);
        CHECK(n2rail_init(&r, &rail_ops, &m));
        CHECK(n2rail_point_for_khz(675000, &pt)); /* 0.65 V / 0.75 V */
        CHECK(n2rail_on(&r, &pt) == N2RAIL_OK);
        /* Order: VSRAM voltage, VSRAM enable, VGPU voltage, VGPU enable. */
        CHECK(m.writes == 4);
        CHECK(m.waddr[0] == N2RAIL_VSRAM_ELR && m.waddr[1] == N2RAIL_VSRAM_CON0 &&
              m.waddr[2] == N2RAIL_VGPU_VOSEL && m.waddr[3] == N2RAIL_VGPU_EN);
        CHECK((m.m59[N2RAIL_VSRAM_ELR / 2] & 0x7f) == 40);
        CHECK((m.m59[N2RAIL_VSRAM_ELR / 2] & ~0x7f) == 0x2a80); /* preserved */
        CHECK(m.m59[N2RAIL_VSRAM_CON0 / 2] == 0x0201);
        CHECK(m.m15[N2RAIL_VGPU_VOSEL] == 0x30 + 56);
        CHECK(m.m15[N2RAIL_VGPU_EN] == 0x05); /* vbuck3 preserved */
        CHECK(r.on && r.vsram_enabled_by_us && r.vgpu_enabled_by_us);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_STATE);
        CHECK(n2rail_off(&r) == N2RAIL_OK);
        CHECK(m.m15[N2RAIL_VGPU_EN] == 0x04 && m.m15[N2RAIL_VGPU_VOSEL] == 0x30 + 40);
        CHECK(m.m59[N2RAIL_VSRAM_CON0 / 2] == 0x0200 &&
              m.m59[N2RAIL_VSRAM_ELR / 2] == (0x2a80 | 24));
        CHECK(!r.on && !r.uncertain);
        CHECK(n2rail_off(&r) == N2RAIL_E_STATE);
}

static void test_rail_warm(void)
{
        /* Boot chain left both rails on at sufficient voltage: no writes at
         * all, and power-off does not disable rails it did not enable. */
        struct rail_model m;
        struct n2rail r;
        struct n2rail_point pt;
        rail_cold(&m);
        m.m15[N2RAIL_VGPU_EN] = 0x05;
        m.m15[N2RAIL_VGPU_VOSEL] = 0x30 + 70;
        m.m59[N2RAIL_VSRAM_CON0 / 2] = 0x0201;
        m.m59[N2RAIL_VSRAM_ELR / 2] = 48;
        n2rail_init(&r, &rail_ops, &m);
        n2rail_point_for_khz(675000, &pt);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_OK && m.writes == 0);
        CHECK(n2rail_off(&r) == N2RAIL_OK && m.writes == 0);
        CHECK(m.m15[N2RAIL_VGPU_EN] == 0x05 && m.m59[N2RAIL_VSRAM_CON0 / 2] == 0x0201);

        /* Over sign-off: refuse without writing. */
        rail_cold(&m);
        m.m15[N2RAIL_VGPU_VOSEL] = 0x30 + 81; /* 0.80625 V */
        n2rail_init(&r, &rail_ops, &m);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_OVERVOLT && m.writes == 0);
        rail_cold(&m);
        m.m15[N2RAIL_VGPU_VOSEL] = 0x10; /* invalid selector */
        n2rail_init(&r, &rail_ops, &m);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_SELECTOR && m.writes == 0);
        rail_cold(&m);
        m.m59[N2RAIL_VSRAM_ELR / 2] = 0x6f; /* 1.19 V: diff > 300 mV */
        n2rail_init(&r, &rail_ops, &m);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_DIFF && m.writes == 0);
}

static void test_rail_fault_positions(void)
{
        /* Inject a transport failure at every operation of a cold power-on
         * and verify the rails always return to the initial state or the
         * engine reports uncertainty (never silently half-on). */
        struct rail_model ref;
        struct n2rail r;
        struct n2rail_point pt;
        n2rail_point_for_khz(675000, &pt);
        rail_cold(&ref);
        n2rail_init(&r, &rail_ops, &ref);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_OK);
        unsigned total = ref.op;
        unsigned clean = 0, uncertain = 0, verified_on = 0;
        for (unsigned k = 1; k <= total; k++) {
                struct rail_model m;
                rail_cold(&m);
                m.fail_at = k;
                n2rail_init(&r, &rail_ops, &m);
                enum n2rail_error e = n2rail_on(&r, &pt);
                m.fail_at = 0;
                if (e == N2RAIL_OK) {
                        /* Only the optional post-verification snapshot may
                         * fail without failing the power-on. */
                        CHECK(r.on && r.step == 6 && !r.uncertain);
                        CHECK((m.m15[N2RAIL_VGPU_EN] & 1) &&
                              (m.m59[N2RAIL_VSRAM_CON0 / 2] & 1));
                        verified_on++;
                        continue;
                }
                CHECK(!r.on);
                struct rail_model cold;
                rail_cold(&cold);
                bool restored = m.m15[N2RAIL_VGPU_EN] == cold.m15[N2RAIL_VGPU_EN] &&
                        m.m15[N2RAIL_VGPU_VOSEL] == cold.m15[N2RAIL_VGPU_VOSEL] &&
                        m.m59[N2RAIL_VSRAM_CON0 / 2] == cold.m59[N2RAIL_VSRAM_CON0 / 2] &&
                        m.m59[N2RAIL_VSRAM_ELR / 2] == cold.m59[N2RAIL_VSRAM_ELR / 2];
                if (restored)
                        clean++;
                else {
                        CHECK(e == N2RAIL_E_ROLLBACK || r.uncertain);
                        uncertain++;
                }
                if (r.uncertain)
                        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_STATE);
        }
        CHECK(clean + uncertain + verified_on == total && clean > 0);
        printf("rail fault injection: %u positions, %u restored, %u flagged "
               "uncertain, %u after verified-on\n",
               total, clean, uncertain, verified_on);

        /* DA never follows the enable: verify failure and rollback. */
        struct rail_model m;
        rail_cold(&m);
        m.ignore_en_write = true;
        n2rail_init(&r, &rail_ops, &m);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_VERIFY);
        CHECK(m.m15[N2RAIL_VGPU_EN] == 0x04 && m.m59[N2RAIL_VSRAM_CON0 / 2] == 0x0200);
        /* Double fault: the VGPU enable write fails and so does the first
         * rollback write.  The engine must report ROLLBACK and refuse to
         * run again until an operator recovers the rails. */
        struct rail_model probe;
        rail_cold(&probe);
        n2rail_init(&r, &rail_ops, &probe);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_OK && probe.writes == 4 &&
              probe.waddr[3] == N2RAIL_VGPU_EN);
        unsigned en_write_op = probe.wop[3];
        CHECK(en_write_op != 0);
        rail_cold(&m);
        m.fail_at = en_write_op;
        m.fail_at2 = en_write_op + 1; /* the read-before-write of rollback */
        n2rail_init(&r, &rail_ops, &m);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_ROLLBACK && r.uncertain && !r.on);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_STATE);
        rail_cold(&m);
        m.da_vosel_wrong = true;
        n2rail_init(&r, &rail_ops, &m);
        CHECK(n2rail_on(&r, &pt) == N2RAIL_E_VERIFY && !r.on);
}

int main(void)
{
        test_pmif();
        test_tables();
        test_rail_cold_on_off();
        test_rail_warm();
        test_rail_fault_positions();
        if (failures) {
                fprintf(stderr, "nord2_rail_test: %u failures\n", failures);
                return 1;
        }
        printf("nord2_rail_test: PMIF SWINF transport and rail policy pass (models)\n");
        return 0;
}
