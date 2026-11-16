/*
 * Nord 2 MT6893 GPU rails: VSRAM_GPU then VGPU (mt_gpufreq_buck_control).
 *
 * VGPU      = MT6315 SID 7 "7_vbuck1" (mt6893.dts _vgpu-supply)
 *             drivers/regulator/mt6315-regulator.c, MT_BUCK(...,1,...,0x3):
 *             EN BUCK_TOP_CON0[0] (0x1440), VOSEL BUCK_TOP_ELR0 (0x1449),
 *             DA_EN BUCK_VBUCK1_DBG4[0] (0x149d), DA_VOSEL DBG0 (0x1499),
 *             300000 uV + (sel - 0x30) * 6250 uV for sel 0x30..0xbf,
 *             enable ramp 256 us (mt6315_s7.dtsi).
 * VSRAM_GPU = MT6359P "vsram_others" LDO (_vsram_gpu-supply)
 *             drivers/regulator/mt6359p-regulator.c MT_LDO_REGULAR:
 *             EN LDO_VSRAM_OTHERS_CON0[0] (0x1f08), DA_B_EN MON[0] (0x1f0c),
 *             DA_VOSEL VOSEL1[14:8] (0x1f10), RG VOSEL ELR[6:0] (0x1b48),
 *             500000 uV + sel * 6250 uV for sel 0..0x6f, ramp 240 us.
 *
 * Operating point: the vendor default (PTPOD-disabled) OPP table of
 * gpufreq/v1/include/mt6893/mtk_gpufreq_internal_plat.h, taking the higher
 * voltage of segment 1 and segment 2 per entry so the efuse segment need not
 * be read. The board preserves a valid preloader frequency, or initializes
 * a cold GPU at the minimum 350MHz OPP. Voltages are only raised and only
 * within the table's sign-off range.  Each rail is disabled or restored on
 * rollback/power-off only if this module enabled or changed it.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_RAIL_H
#define NORD2_GPU_RAIL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2RAIL_MT6315_SID 7u
#define N2RAIL_VGPU_EN 0x1440u
#define N2RAIL_VGPU_VOSEL 0x1449u
#define N2RAIL_VGPU_DA_VOSEL 0x1499u
#define N2RAIL_VGPU_DA_EN 0x149du
#define N2RAIL_MT6315_SWCID_H 0x000bu
#define N2RAIL_VSRAM_ELR 0x1b48u
#define N2RAIL_VSRAM_CON0 0x1f08u
#define N2RAIL_VSRAM_MON 0x1f0cu
#define N2RAIL_VSRAM_VOSEL1 0x1f10u

#define N2RAIL_VGPU_SEL_MIN 0x30u
#define N2RAIL_VGPU_SEL_MAX 0xbfu
#define N2RAIL_VSRAM_SEL_MAX 0x6fu
#define N2RAIL_VGPU_RAMP_US 256u
#define N2RAIL_VSRAM_RAMP_US 240u
#define N2RAIL_OPP_COUNT 41u
/* BUCK_DIFF_MAX/MIN in mV x 100: VSRAM - VGPU within [-100 mV, +300 mV]. */
#define N2RAIL_BUCK_DIFF_MAX_UV 300000
#define N2RAIL_BUCK_DIFF_MIN_UV (-100000)

struct n2rail_opp {
        uint32_t khz;
        uint32_t vgpu_uv;
        uint32_t vsram_uv;
        uint8_t posdiv_power;
};

struct n2rail_point {
        unsigned index;
        uint32_t khz;          /* running or planned cold-start frequency */
        uint32_t vgpu_uv;      /* minimum required */
        uint32_t vsram_uv;
};

struct n2rail_snapshot {
        uint8_t swcid_h;
        uint8_t vgpu_en, vgpu_vosel, vgpu_da_en, vgpu_da_vosel;
        uint16_t vsram_con0, vsram_mon, vsram_vosel1, vsram_elr;
        uint32_t valid;        /* bit per field read successfully */
};

struct n2rail_ops {
        int (*mt6315_read)(void *ctx, uint16_t addr, uint8_t *value);
        int (*mt6315_write)(void *ctx, uint16_t addr, uint8_t value);
        int (*mt6359_read)(void *ctx, uint16_t addr, uint16_t *value);
        int (*mt6359_write)(void *ctx, uint16_t addr, uint16_t value);
        void (*udelay)(void *ctx, unsigned us);
        void (*log)(void *ctx, const char *msg);
};

enum n2rail_error {
        N2RAIL_OK = 0,
        N2RAIL_E_ARGS,
        N2RAIL_E_IO,           /* transport failure */
        N2RAIL_E_STATE,        /* already on / not on */
        N2RAIL_E_FREQ,         /* PLL frequency outside the vendor table */
        N2RAIL_E_OVERVOLT,     /* programmed voltage above sign-off: refuse */
        N2RAIL_E_SELECTOR,     /* selector outside the regulator range */
        N2RAIL_E_DIFF,         /* VSRAM - VGPU outside BUCK_DIFF */
        N2RAIL_E_VERIFY,       /* readback after a write disagrees */
        N2RAIL_E_ROLLBACK,     /* rollback itself failed: rails uncertain */
};

struct n2rail {
        struct n2rail_ops ops;
        void *ctx;
        struct n2rail_snapshot before, after;
        struct n2rail_point point;
        bool on;
        bool uncertain;        /* a write may have committed unverified */
        bool vsram_enabled_by_us, vgpu_enabled_by_us;
        bool vsram_raised, vgpu_raised;
        uint8_t vgpu_orig_sel;
        uint16_t vsram_orig_elr;
        uint32_t step;         /* last step attempted, for diagnostics */
        uint32_t writes;
};

extern const struct n2rail_opp n2rail_opp_table[N2RAIL_OPP_COUNT];

/* __mt_gpufreq_get_cur_freq(): ((pcw*10+5)*26 >> 14) / 2^posdiv * 100. */
uint32_t n2rail_mfgpll_khz(uint32_t con1);
/* Conservative requirement for a running frequency, or false. */
bool n2rail_point_for_khz(uint32_t khz, struct n2rail_point *point);
bool n2rail_vgpu_uv(uint8_t sel, uint32_t *uv);
bool n2rail_vsram_uv(uint8_t sel, uint32_t *uv);
uint8_t n2rail_vgpu_sel_for_uv(uint32_t uv);   /* rounds up */
uint8_t n2rail_vsram_sel_for_uv(uint32_t uv);  /* rounds up */

bool n2rail_init(struct n2rail *r, const struct n2rail_ops *ops, void *ctx);
enum n2rail_error n2rail_capture(struct n2rail *r, struct n2rail_snapshot *s);
/* VSRAM then VGPU; complete rollback on failure. */
enum n2rail_error n2rail_on(struct n2rail *r, const struct n2rail_point *pt);
/* VGPU then VSRAM; only what this module changed. */
enum n2rail_error n2rail_off(struct n2rail *r);
const char *n2rail_error_string(enum n2rail_error e);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_RAIL_H */
