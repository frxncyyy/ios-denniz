/*
 * Behavioural register-model tests for nord2_gpu_hw.c.  The model only
 * mimics documented acknowledgement registers (RESET_COMPLETED,
 * PWRTRANS/READY, CLEAN_CACHES_COMPLETED, AS_ACTIVE, COMMAND_NEXT); it is a
 * contract check, not evidence that hardware behaves this way.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_hw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { failures++; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

struct model {
        uint32_t reg[0x3000 / 4];
        /* behaviour knobs */
        int soft_reset_reads;   /* reads until RESET_COMPLETED; <0 never */
        int hard_reset_reads;
        int power_reads;        /* reads of READY until set; <0 never */
        int shader_never;
        int clean_reads;        /* <0 never */
        int as_active_reads;    /* <0 stuck */
        /* state */
        int reset_countdown;
        int clean_countdown;
        int as_countdown[16];
        uint64_t pending_l2, pending_tiler, pending_shader;
        int l2_count, tiler_count, shader_count;
        uint64_t shader_off_pending;
        /* trace */
        uint32_t wlog_off[512], wlog_val[512];
        unsigned wlog;
        unsigned delays;
};

static uint32_t *R(struct model *m, uint32_t off) { return &m->reg[off / 4]; }

static void settle_cores(struct model *m)
{
        if (m->pending_tiler && m->tiler_count >= 0 && --m->tiler_count <= 0) {
                *R(m, N2HW_TILER_READY_LO) |= (uint32_t)m->pending_tiler;
                *R(m, N2HW_L2_READY_LO) |= 1u; /* first L2 with the tiler */
                *R(m, N2HW_TILER_PWRTRANS_LO) = 0;
                m->pending_tiler = 0;
        }
        if (m->pending_l2 && m->l2_count >= 0 && --m->l2_count <= 0) {
                *R(m, N2HW_L2_READY_LO) |= (uint32_t)m->pending_l2;
                *R(m, N2HW_L2_PWRTRANS_LO) = 0;
                m->pending_l2 = 0;
        }
        if (m->pending_shader && m->shader_count >= 0 &&
            --m->shader_count <= 0) {
                *R(m, N2HW_SHADER_READY_LO) |= (uint32_t)m->pending_shader;
                *R(m, N2HW_SHADER_PWRTRANS_LO) = 0;
                m->pending_shader = 0;
        }
        if (m->shader_off_pending) {
                *R(m, N2HW_SHADER_READY_LO) &= ~(uint32_t)m->shader_off_pending;
                m->shader_off_pending = 0;
        }
}

static uint32_t rd(void *ctx, uint32_t off)
{
        struct model *m = ctx;
        if (off == N2HW_GPU_IRQ_RAWSTAT) {
                if (m->reset_countdown > 0 && --m->reset_countdown == 0)
                        *R(m, off) |= N2HW_IRQ_RESET_COMPLETED;
                if (m->clean_countdown > 0 && --m->clean_countdown == 0)
                        *R(m, off) |= N2HW_IRQ_CLEAN_CACHES_COMPLETED;
        }
        if (off == N2HW_L2_READY_LO || off == N2HW_TILER_READY_LO ||
            off == N2HW_SHADER_READY_LO)
                settle_cores(m);
        for (unsigned as = 0; as < 16; as++) {
                if (off == N2HW_AS_REG(as, N2HW_AS_STATUS) &&
                    m->as_countdown[as] > 0 && --m->as_countdown[as] == 0)
                        *R(m, off) &= ~N2HW_AS_STATUS_ACTIVE;
        }
        return *R(m, off);
}

static void wr(void *ctx, uint32_t off, uint32_t v)
{
        struct model *m = ctx;
        if (m->wlog < 512) {
                m->wlog_off[m->wlog] = off;
                m->wlog_val[m->wlog++] = v;
        }
        switch (off) {
        case N2HW_GPU_IRQ_CLEAR:
                *R(m, N2HW_GPU_IRQ_RAWSTAT) &= ~v;
                return;
        case N2HW_JOB_IRQ_CLEAR:
                *R(m, N2HW_JOB_IRQ_RAWSTAT) &= ~v;
                return;
        case N2HW_MMU_IRQ_CLEAR:
                *R(m, N2HW_MMU_IRQ_RAWSTAT) &= ~v;
                return;
        case N2HW_GPU_COMMAND:
                if (v == N2HW_CMD_SOFT_RESET)
                        m->reset_countdown = m->soft_reset_reads;
                else if (v == N2HW_CMD_HARD_RESET)
                        m->reset_countdown = m->hard_reset_reads;
                else if (v == N2HW_CMD_CLEAN_INV_CACHES)
                        m->clean_countdown = m->clean_reads;
                if (v <= 2) { /* reset powers everything down */
                        *R(m, N2HW_L2_READY_LO) = 0;
                        *R(m, N2HW_TILER_READY_LO) = 0;
                        *R(m, N2HW_SHADER_READY_LO) = 0;
                }
                return;
        case N2HW_TILER_PWRON_LO:
                m->pending_tiler = v;
                m->tiler_count = m->power_reads;
                *R(m, N2HW_TILER_PWRTRANS_LO) = v;
                return;
        case N2HW_L2_PWRON_LO:
                m->pending_l2 = v;
                m->l2_count = m->power_reads;
                *R(m, N2HW_L2_PWRTRANS_LO) = v;
                return;
        case N2HW_SHADER_PWRON_LO:
                m->pending_shader = v;
                m->shader_count = m->shader_never ? -1 : m->power_reads;
                *R(m, N2HW_SHADER_PWRTRANS_LO) = v;
                return;
        case N2HW_SHADER_PWROFF_LO:
                m->shader_off_pending = v;
                return;
        default:
                break;
        }
        for (unsigned as = 0; as < 16; as++) {
                if (off == N2HW_AS_REG(as, N2HW_AS_COMMAND)) {
                        if (*R(m, N2HW_AS_REG(as, N2HW_AS_STATUS)) &
                            N2HW_AS_STATUS_ACTIVE)
                                failures++, fprintf(stderr,
                                        "FAIL AS command while active\n");
                        *R(m, N2HW_AS_REG(as, N2HW_AS_STATUS)) |=
                                N2HW_AS_STATUS_ACTIVE;
                        m->as_countdown[as] = m->as_active_reads;
                }
        }
        *R(m, off) = v;
}

static void dly(void *ctx, unsigned us) { (void)us; ((struct model *)ctx)->delays++; }

static const struct n2hw_ops ops = { rd, wr, dly, NULL };

static void model_init(struct model *m, uint32_t gpu_id)
{
        memset(m, 0, sizeof(*m));
        *R(m, N2HW_GPU_ID) = gpu_id;
        *R(m, N2HW_AS_PRESENT) = 0xff;
        *R(m, N2HW_JS_PRESENT) = 0x7;
        *R(m, N2HW_JS0_FEATURES) = 0x20e;  /* fragment + set value/flush */
        *R(m, N2HW_JS0_FEATURES + 4) = 0x1fe;
        *R(m, N2HW_JS0_FEATURES + 8) = 0x7e;
        *R(m, N2HW_SHADER_PRESENT_LO) = (uint32_t)N2HW_MT6893_SHADER_PRESENT_9;
        *R(m, N2HW_TILER_PRESENT_LO) = 1;
        *R(m, N2HW_L2_PRESENT_LO) = 1;
        *R(m, N2HW_L2_MMU_CONFIG) = 0xffffffffu;
        *R(m, N2HW_JM_CONFIG) = 0x1;
        *R(m, N2HW_SHADER_CONFIG) = 0x10;
        *R(m, N2HW_TILER_CONFIG) = 0x77;
        m->soft_reset_reads = 3;
        m->hard_reset_reads = 3;
        m->power_reads = 3;
        m->clean_reads = 2;
        m->as_active_reads = 2;
}

#define G77_R0P2 0x90000020u
#define G77_R0P1 0x90000010u

static void test_identify(void)
{
        struct model m;
        struct n2hw hw;
        model_init(&m, G77_R0P2 | (2u << 20));
        CHECK(n2hw_init(&hw, &ops, &m));
        CHECK(n2hw_identify(&hw) == N2HW_OK);
        CHECK(hw.id.version_minor == 2 && hw.has_issue_ttrx_921);
        CHECK(n2hw_select_slots(&hw) == N2HW_OK);
        CHECK(hw.slot_fragment == 0 && hw.slot_vertex_tiler == 1 &&
              hw.slot_compute == 1);

        model_init(&m, G77_R0P1);
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_identify(&hw) == N2HW_OK);
        CHECK(hw.identified && hw.has_issue_ttrx_3485 && !hw.wa_complete);
        unsigned before = m.wlog;
        CHECK(n2hw_power_on_cores(&hw, 1) == N2HW_E_REVISION_WA);
        CHECK(m.wlog == before);
        model_init(&m, 0x90020020u); /* tBEx, product 2 */
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_identify(&hw) == N2HW_E_PRODUCT);
        model_init(&m, 0xb8a21030u); /* donor arch11 */
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_identify(&hw) == N2HW_E_PRODUCT);
        model_init(&m, G77_R0P2);
        *R(&m, N2HW_SHADER_PRESENT_LO) = 0x1ff; /* contiguous: wrong domains */
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_identify(&hw) == N2HW_E_PRESENT);
        model_init(&m, G77_R0P2);
        *R(&m, N2HW_JS0_FEATURES) = 0x1fe; /* JS0 cannot fragment */
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_identify(&hw) == N2HW_OK);
        CHECK(n2hw_select_slots(&hw) == N2HW_E_SLOTS && !hw.slots_valid);
}

static void test_reset(void)
{
        struct model m;
        struct n2hw hw;
        model_init(&m, G77_R0P2);
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_reset(&hw) == N2HW_OK);
        CHECK(hw.resets_soft == 1 && hw.resets_hard == 0);
        CHECK(!(*R(&m, N2HW_GPU_IRQ_RAWSTAT) & N2HW_IRQ_RESET_COMPLETED));

        model_init(&m, G77_R0P2);
        m.soft_reset_reads = -1;
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_reset(&hw) == N2HW_OK);
        CHECK(hw.resets_soft == 1 && hw.resets_hard == 1);

        model_init(&m, G77_R0P2);
        m.soft_reset_reads = -1;
        m.hard_reset_reads = -1;
        n2hw_init(&hw, &ops, &m);
        unsigned before = m.delays;
        CHECK(n2hw_reset(&hw) == N2HW_E_RESET_TIMEOUT);
        /* Two bounded windows of RESET_TIMEOUT / poll. */
        CHECK(m.delays - before ==
              2 * (N2HW_RESET_TIMEOUT_US / N2HW_POLL_US));

        model_init(&m, G77_R0P2);
        *R(&m, N2HW_GPU_STATUS) = N2HW_GPU_STATUS_PROTECTED;
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_reset(&hw) == N2HW_E_PROTECTED);
}

static int find_write(const struct model *m, uint32_t off, unsigned from)
{
        for (unsigned i = from; i < m->wlog; i++)
                if (m->wlog_off[i] == off)
                        return (int)i;
        return -1;
}

static void test_quirks(void)
{
        struct model m;
        struct n2hw hw;
        model_init(&m, G77_R0P2);
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_apply_quirks(&hw) == N2HW_E_NOT_IDENTIFIED);
        CHECK(n2hw_identify(&hw) == N2HW_OK);
        m.wlog = 0;
        CHECK(n2hw_apply_quirks(&hw) == N2HW_OK);
        int sc = find_write(&m, N2HW_SHADER_CONFIG, 0);
        int ti = find_write(&m, N2HW_TILER_CONFIG, 0);
        int mmu = find_write(&m, N2HW_L2_MMU_CONFIG, 0);
        int jm = find_write(&m, N2HW_JM_CONFIG, 0);
        int coh = find_write(&m, N2HW_COHERENCY_ENABLE, 0);
        CHECK(sc >= 0 && sc < ti && ti < mmu && mmu < jm && jm < coh);
        CHECK(*R(&m, N2HW_SHADER_CONFIG) == (0x10u | N2HW_SC_VAR_ALGORITHM));
        CHECK(*R(&m, N2HW_TILER_CONFIG) == 0x77u);
        CHECK(*R(&m, N2HW_L2_MMU_CONFIG) == (0xffffffffu & ~0x3ff80u));
        CHECK(*R(&m, N2HW_JM_CONFIG) == (0x1u | (0xfu << 16)));
        CHECK(*R(&m, N2HW_COHERENCY_ENABLE) == 31u);
}

static void test_power(void)
{
        struct model m;
        struct n2hw hw;
        model_init(&m, G77_R0P2);
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_identify(&hw) == N2HW_OK);
        CHECK(n2hw_power_on_cores(&hw, 0) == N2HW_E_ARGS);
        CHECK(n2hw_power_on_cores(&hw, 1ull << 3) == N2HW_E_ARGS);
        m.wlog = 0;
        CHECK(n2hw_power_on_cores(&hw, N2HW_MT6893_SHADER_PRESENT_9) == N2HW_OK);
        int t = find_write(&m, N2HW_TILER_PWRON_LO, 0);
        int s = find_write(&m, N2HW_SHADER_PWRON_LO, 0);
        CHECK(t >= 0 && s > t);
        CHECK(find_write(&m, N2HW_L2_PWRON_LO, 0) < 0); /* single L2 */
        CHECK(hw.l2_on && hw.shaders_on == N2HW_MT6893_SHADER_PRESENT_9);
        /* Idempotent: no second PWRON write. */
        m.wlog = 0;
        CHECK(n2hw_power_on_cores(&hw, N2HW_MT6893_SHADER_PRESENT_9) == N2HW_OK);
        CHECK(m.wlog == 0);
        /* Off: TTRX_921 clean precedes PWROFF, final clean follows. */
        m.wlog = 0;
        CHECK(n2hw_power_off_cores(&hw) == N2HW_OK);
        int c1 = find_write(&m, N2HW_GPU_COMMAND, 0);
        int off = find_write(&m, N2HW_SHADER_PWROFF_LO, 0);
        int c2 = find_write(&m, N2HW_GPU_COMMAND, (unsigned)off + 1);
        CHECK(c1 >= 0 && off > c1 && c2 > off);
        CHECK(m.wlog_val[c1] == N2HW_CMD_CLEAN_INV_CACHES);
        CHECK(hw.shaders_on == 0 && hw.l2_on);
        CHECK(find_write(&m, N2HW_L2_PWROFF_LO, 0) < 0); /* GPU2017_1336 */

        /* L2 never ready: bounded failure, no shader PWRON attempted. */
        model_init(&m, G77_R0P2);
        m.power_reads = -1;
        n2hw_init(&hw, &ops, &m);
        n2hw_identify(&hw);
        m.wlog = 0;
        CHECK(n2hw_power_on_cores(&hw, 1) == N2HW_E_L2_TIMEOUT);
        CHECK(find_write(&m, N2HW_SHADER_PWRON_LO, 0) < 0 && !hw.l2_on);

        model_init(&m, G77_R0P2);
        m.shader_never = 1;
        n2hw_init(&hw, &ops, &m);
        n2hw_identify(&hw);
        CHECK(n2hw_power_on_cores(&hw, 1) == N2HW_E_SHADER_TIMEOUT);
        CHECK(hw.l2_on && hw.shaders_on == 0);

        /* Cache clean without acknowledgement fails, bounded. */
        model_init(&m, G77_R0P2);
        n2hw_init(&hw, &ops, &m);
        n2hw_identify(&hw);
        CHECK(n2hw_cache_clean_inv(&hw) == N2HW_E_POWER_STATE);
        CHECK(n2hw_power_on_cores(&hw, 1) == N2HW_OK);
        m.clean_reads = -1;
        CHECK(n2hw_cache_clean_inv(&hw) == N2HW_E_CACHE_TIMEOUT);
        CHECK(n2hw_power_off_cores(&hw) == N2HW_E_CACHE_TIMEOUT);
        CHECK(hw.shaders_on == 1); /* never powered off without the flush */
}

static void test_lock_region(void)
{
        uint64_t la = 0;
        /* Vendor comment example in bytes: [0x4F000, 0x50FFF] -> 128 KiB. */
        CHECK(n2hw_lock_region(0x4f000, 0x2000, &la) && la == (0x40000 | 16));
        /* Minimum region 32 KiB. */
        CHECK(n2hw_lock_region(0x1000, 0x1000, &la) && la == (0x0 | 14));
        CHECK(n2hw_lock_region(0x123456000ull, 0x1000, &la) &&
              la == (0x123450000ull | 14));
        CHECK(n2hw_lock_region(0x1000000000ull, 0x400000, &la) &&
              la == (0x1000000000ull | 21));
        CHECK(n2hw_lock_region(0, 1ull << 44, &la) && la == 43);
        CHECK(!n2hw_lock_region(0x1001, 0x1000, &la));
        CHECK(!n2hw_lock_region(0x1000, 0, &la));
        CHECK(!n2hw_lock_region(0xffffffffff000ull, 0x2000, &la));
        /* Exhaustive property check against the definition. */
        srand(7);
        for (unsigned i = 0; i < 20000; i++) {
                uint64_t va = ((uint64_t)rand() << 12) & ((1ull << 40) - 1);
                uint64_t size = (((uint64_t)rand() % 4096) + 1) << 12;
                CHECK(n2hw_lock_region(va, size, &la));
                unsigned log2 = (unsigned)(la & 0x3f) + 1;
                uint64_t base = la & ~0x3full;
                CHECK(log2 >= 15 && log2 <= 48);
                CHECK((base & ((1ull << log2) - 1)) == 0);
                CHECK(base <= va && va + size - 1 <= base + (1ull << log2) - 1);
                if (log2 > 15) { /* minimality */
                        uint64_t half = 1ull << (log2 - 1);
                        CHECK((va & ~(half - 1)) != ((va + size - 1) & ~(half - 1)));
                }
        }
}

static void test_as(void)
{
        struct model m;
        struct n2hw hw;
        model_init(&m, G77_R0P2);
        n2hw_init(&hw, &ops, &m);
        CHECK(n2hw_as_configure(&hw, 1, 0x80000000ull) == N2HW_E_NOT_IDENTIFIED);
        n2hw_identify(&hw);
        CHECK(n2hw_as_configure(&hw, 1, 0x80000800ull) == N2HW_E_ARGS);
        CHECK(n2hw_as_configure(&hw, 9, 0x80000000ull) == N2HW_E_ARGS);
        m.wlog = 0;
        CHECK(n2hw_as_configure(&hw, 1, 0x80001000ull) == N2HW_OK);
        CHECK(*R(&m, N2HW_AS_REG(1, N2HW_AS_TRANSCFG_LO)) ==
              (uint32_t)(6u | (2u << 24) | (1u << 30)));
        CHECK(*R(&m, N2HW_AS_REG(1, N2HW_AS_TRANSTAB_LO)) == 0x80001000u);
        CHECK(*R(&m, N2HW_AS_REG(1, N2HW_AS_MEMATTR_LO)) == 0x888d8f88u);
        CHECK(*R(&m, N2HW_AS_REG(1, N2HW_AS_MEMATTR_HI)) == 0x4c8du);
        int cmd = find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND), 0);
        CHECK(cmd >= 0 && m.wlog_val[cmd] == N2HW_AS_CMD_UPDATE);
        CHECK(find_write(&m, N2HW_AS_REG(1, N2HW_AS_TRANSTAB_LO), 0) < cmd);

        m.wlog = 0;
        CHECK(n2hw_as_flush(&hw, 1, 0x1000000000ull, 0x10000, false) == N2HW_OK);
        int lk = find_write(&m, N2HW_AS_REG(1, N2HW_AS_LOCKADDR_LO), 0);
        int c1 = find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND), 0);
        int c2 = find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND), (unsigned)c1 + 1);
        CHECK(lk >= 0 && lk < c1 && c1 < c2);
        CHECK(m.wlog_val[c1] == N2HW_AS_CMD_LOCK &&
              m.wlog_val[c2] == N2HW_AS_CMD_FLUSH_PT);
        CHECK(m.wlog_val[lk] == (0u | 15)); /* 64 KiB at 0x1000000000 */
        CHECK(!(*R(&m, N2HW_AS_REG(1, N2HW_AS_STATUS)) & 1));

        m.wlog = 0;
        CHECK(n2hw_as_disable(&hw, 1) == N2HW_OK);
        c1 = find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND), 0);
        c2 = find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND), (unsigned)c1 + 1);
        int c3 = find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND), (unsigned)c2 + 1);
        CHECK(m.wlog_val[c1] == N2HW_AS_CMD_LOCK &&
              m.wlog_val[c2] == N2HW_AS_CMD_FLUSH_MEM &&
              m.wlog_val[c3] == N2HW_AS_CMD_UPDATE);
        CHECK(*R(&m, N2HW_AS_REG(1, N2HW_AS_TRANSCFG_LO)) == 1u);
        CHECK(*R(&m, N2HW_AS_REG(1, N2HW_AS_TRANSTAB_LO)) == 0u);

        /* Stuck AS: the operation fails, the second command is not issued. */
        m.as_active_reads = -1;
        m.wlog = 0;
        CHECK(n2hw_as_flush(&hw, 1, 0, 0x1000, true) == N2HW_E_AS_TIMEOUT);
        c1 = find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND), 0);
        CHECK(c1 >= 0 && find_write(&m, N2HW_AS_REG(1, N2HW_AS_COMMAND),
                                    (unsigned)c1 + 1) < 0);
        CHECK(hw.last_error == N2HW_E_AS_TIMEOUT);
}

static void test_js(void)
{
        struct model m;
        struct n2hw hw;
        model_init(&m, G77_R0P2);
        n2hw_init(&hw, &ops, &m);
        n2hw_identify(&hw);
        CHECK(n2hw_js_start(&hw, 1, 1, 0x1000000000ull, 1) == N2HW_E_NOT_IDENTIFIED);
        n2hw_select_slots(&hw);
        CHECK(n2hw_js_start(&hw, 1, 1, 0x1000000000ull, 1) == N2HW_E_ARGS); /* cores off */
        n2hw_power_on_cores(&hw, N2HW_MT6893_SHADER_PRESENT_9);
        CHECK(n2hw_js_start(&hw, 1, 1, 0x1000000010ull, 1) == N2HW_E_ARGS);
        CHECK(n2hw_js_start(&hw, 3, 1, 0x1000000000ull, 1) == N2HW_E_ARGS);
        CHECK(n2hw_js_start(&hw, 1, 1, 0x1000000000ull, 1ull << 3) == N2HW_E_ARGS);
        m.wlog = 0;
        CHECK(n2hw_js_start(&hw, 1, 2, 0x1000000040ull,
                            N2HW_MT6893_SHADER_PRESENT_9) == N2HW_OK);
        uint32_t cfg = *R(&m, N2HW_JS_REG(1, N2HW_JS_CONFIG_NEXT));
        CHECK(cfg == (2u | (3u << 8) | (1u << 11) | (1u << 12) | (8u << 16)));
        CHECK(*R(&m, N2HW_JS_REG(1, N2HW_JS_HEAD_NEXT_LO)) == 0x40u &&
              *R(&m, N2HW_JS_REG(1, N2HW_JS_HEAD_NEXT_HI)) == 0x10u);
        CHECK(*R(&m, N2HW_JS_REG(1, N2HW_JS_AFFINITY_NEXT_LO)) == 0x00660067u);
        int start = find_write(&m, N2HW_JS_REG(1, N2HW_JS_COMMAND_NEXT), 0);
        CHECK(start == (int)m.wlog - 1); /* START is the last write */
        /* NEXT still busy: second start refused. */
        CHECK(n2hw_js_start(&hw, 1, 2, 0x1000000040ull, 1) == N2HW_E_SLOT_BUSY);
        *R(&m, N2HW_JS_REG(1, N2HW_JS_COMMAND_NEXT)) = 0;
        CHECK(n2hw_js_start(&hw, 1, 2, 0x1000000080ull, 1) == N2HW_OK);
        CHECK(!(*R(&m, N2HW_JS_REG(1, N2HW_JS_CONFIG_NEXT)) & (1u << 11)));

        /* IRQ decode: slot1 done, slot0 failed with zero status. */
        *R(&m, N2HW_JOB_IRQ_RAWSTAT) = (1u << 1) | (1u << 16);
        *R(&m, N2HW_JS_REG(0, N2HW_JS_STATUS)) = 0;
        *R(&m, N2HW_JS_REG(1, N2HW_JS_HEAD_LO)) = 0x80;
        struct n2hw_job_event ev[4];
        unsigned n = n2hw_job_irq(&hw, ev, 4);
        CHECK(n == 2);
        CHECK(ev[0].slot == 0 && ev[0].failed && ev[0].status == 4);
        CHECK(ev[1].slot == 1 && !ev[1].failed && ev[1].status == 0 &&
              ev[1].head == 0x80);
        CHECK(*R(&m, N2HW_JOB_IRQ_RAWSTAT) == 0);
        /* Bus fault status preserved. */
        *R(&m, N2HW_JOB_IRQ_RAWSTAT) = 1u << 17;
        *R(&m, N2HW_JS_REG(1, N2HW_JS_STATUS)) = 0x48;
        CHECK(n2hw_job_irq(&hw, ev, 4) == 1 && ev[0].status == 0x48);

        /* Idle requires both JS_STATE bits and NEXT clear. */
        *R(&m, N2HW_JOB_IRQ_JS_STATE) = 1u << 17;
        CHECK(!n2hw_js_idle(&hw, 1));
        *R(&m, N2HW_JOB_IRQ_JS_STATE) = 0;
        *R(&m, N2HW_JS_REG(1, N2HW_JS_COMMAND_NEXT)) = 1;
        CHECK(!n2hw_js_idle(&hw, 1));
        *R(&m, N2HW_JS_REG(1, N2HW_JS_COMMAND_NEXT)) = 0;
        CHECK(n2hw_js_idle(&hw, 1));
        m.wlog = 0;
        n2hw_js_stop(&hw, 1, true);
        CHECK(m.wlog == 1 && m.wlog_off[0] == N2HW_JS_REG(1, N2HW_JS_COMMAND) &&
              m.wlog_val[0] == N2HW_JS_CMD_HARD_STOP);

        /* MMU fault decode and clear. */
        *R(&m, N2HW_MMU_IRQ_RAWSTAT) = (1u << 2) | (1u << 18) | (1u << 3);
        *R(&m, N2HW_AS_REG(2, N2HW_AS_FAULTSTATUS)) = 0xc3;
        *R(&m, N2HW_AS_REG(2, N2HW_AS_FAULTADDRESS_LO)) = 0x1000;
        *R(&m, N2HW_AS_REG(2, N2HW_AS_FAULTADDRESS_HI)) = 0x10;
        struct n2hw_mmu_fault f[4];
        n = n2hw_mmu_irq(&hw, f, 4);
        CHECK(n == 2 && f[0].as == 2 && f[0].bus_error &&
              f[0].address == 0x1000001000ull && f[1].as == 3 && !f[1].bus_error);
        CHECK(*R(&m, N2HW_MMU_IRQ_RAWSTAT) == 0);
}

int main(void)
{
        test_identify();
        test_reset();
        test_quirks();
        test_power();
        test_lock_region();
        test_as();
        test_js();
        if (failures) {
                fprintf(stderr, "nord2_hw_test: %u failures\n", failures);
                return 1;
        }
        printf("nord2_hw_test: identify/slots/reset/quirks/power/cache/AS/"
               "lock-region/JS/IRQ contracts pass (register model)\n");
        return 0;
}
