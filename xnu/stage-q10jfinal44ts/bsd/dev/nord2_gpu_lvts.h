/*
 * Nord 2 MT6893 LVTS thermal sensing (GPU/CPU/SoC temperatures), opt-in.
 *
 * Sources (Android kernel, drivers/misc/mediatek/thermal/mt6893/): mtk_lvts_tc.c, inc/tscpu_lvts_config.h,
 * inc/mach/mtk_thermal.h; arch/arm64/boot/dts/mediatek/mt6893.dts (therm_ctrl@1100b000, efuse@11c10000);
 * drivers/clk/mediatek/clk-mt6885.c (ifrao_therm = infracfg_ao CG0 bit 10, STA 0x090 / CLR 0x084).
 *
 * Seven thermal controllers: MCU (CPU) at 0x11278000 + {0,0x100,0x200}, AP at 0x1100b000 + {0,0x100,0x200,0x300}.
 * GPU is LVTS5 (controller 4, AP+0x100, two sensors, device id 0x85).
 *
 * Safety model:
 *  - every register access goes through struct n2lvts_ops (host register-model tested);
 *  - level 1 (probe) only READS: the infracfg thermal clock gate first (a gated block must never be touched),
 *    then the controller's monitor/MSR registers.  Level 2 runs the vendor init for the selected controllers
 *    (never the hardware-protect / reset registers, never interrupts), level 3 additionally opens the clock gate.
 *  - the efuse calibration cannot be read offline (devinfo index -> MMIO mapping is not linear, the data is handed
 *    over by the bootloader in /chosen atag,devinfo): without it the vendor's own "uncalibrated" defaults are used and
 *    every result is flagged approximate; implausible results are never reported or used.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_LVTS_H
#define NORD2_GPU_LVTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2LVTS_AP_PHYS    0x1100b000u
#define N2LVTS_MCU_PHYS   0x11278000u
#define N2LVTS_BLOCK_BYTES 0x400u        /* four controllers of 0x100 */
#define N2LVTS_INFRA_CG_CLR 0x084u
#define N2LVTS_INFRA_CG_STA 0x090u
#define N2LVTS_INFRA_THERM_BIT 10u       /* 1 = gated */

/* Controller registers (offset from the controller base). */
#define N2LVTS_MONCTL0  0x000u
#define N2LVTS_MONCTL1  0x004u
#define N2LVTS_MONCTL2  0x008u
#define N2LVTS_MONINT   0x00cu
#define N2LVTS_MSRCTL0  0x038u
#define N2LVTS_MSRCTL1  0x03cu
#define N2LVTS_TSSEL    0x040u
#define N2LVTS_CALSCALE 0x048u
#define N2LVTS_ID       0x04cu
#define N2LVTS_CONFIG   0x050u
#define N2LVTS_EDATA0   0x054u           /* + 4*sensor */
#define N2LVTS_MSR0     0x090u           /* + 4*sensor */
#define N2LVTS_RDATA0   0x0b0u
#define N2LVTS_CLKEN    0x0e4u

#define N2LVTS_CTRL_COUNT 7u
#define N2LVTS_SENSOR_COUNT 17u
#define N2LVTS_CTRL_GPU 4u
#define N2LVTS_MASK_GPU (1u << N2LVTS_CTRL_GPU)
#define N2LVTS_MASK_ALL ((1u << N2LVTS_CTRL_COUNT) - 1u)

/* Vendor fallback calibration for an uncalibrated sample (DEFAULT_EFUSE_*). */
#define N2LVTS_DEFAULT_GOLDEN 50u        /* 2 x degC */
#define N2LVTS_DEFAULT_COUNT_R 35000u
#define N2LVTS_DEFAULT_COUNT_RC 2750u

enum n2lvts_block { N2LVTS_AP = 0, N2LVTS_MCU = 1, N2LVTS_INFRA = 2 };

struct n2lvts_ctrl_desc {
        uint8_t block;          /* N2LVTS_AP / N2LVTS_MCU */
        uint16_t offset;        /* controller offset in the block */
        uint8_t nsens;
        uint8_t first_sensor;   /* index into the 17-entry sensor arrays */
        const char *role;
};
extern const struct n2lvts_ctrl_desc n2lvts_ctrl[N2LVTS_CTRL_COUNT];

struct n2lvts_ops {
        uint32_t (*rd)(void *ctx, enum n2lvts_block b, uint32_t off);
        void (*wr)(void *ctx, enum n2lvts_block b, uint32_t off, uint32_t v);
        void (*udelay)(void *ctx, unsigned us);
        void (*log)(void *ctx, const char *msg);
};

struct n2lvts_cal {
        bool defaulted;                          /* vendor fake values: temperatures are approximate */
        uint32_t golden;                         /* 2 x degC */
        uint32_t count_r[N2LVTS_SENSOR_COUNT];
        uint32_t count_rc[N2LVTS_CTRL_COUNT];
};

enum n2lvts_error {
        N2LVTS_OK = 0,
        N2LVTS_E_ARGS,
        N2LVTS_E_GATED,        /* infracfg thermal clock gate closed: block not touched */
        N2LVTS_E_CAL,          /* calibration implausible */
        N2LVTS_E_BUSY,         /* device access never completed */
        N2LVTS_E_ID,           /* device id mismatch: controller left as found */
        N2LVTS_E_NODATA,       /* no valid sample before the deadline */
};

struct n2lvts_probe {
        bool gate_open;
        uint32_t clken, monctl0, id, msr[4];
};

struct n2lvts {
        struct n2lvts_ops ops;
        void *ctx;
        struct n2lvts_cal cal;
        uint32_t running;                        /* controllers we started and verified */
        uint32_t saved_clken[N2LVTS_CTRL_COUNT], saved_monctl0[N2LVTS_CTRL_COUNT];
        bool ungated_by_us;
        uint32_t step;                           /* last init step, for diagnostics */
        int32_t temp_mC[N2LVTS_SENSOR_COUNT];
        uint32_t raw[N2LVTS_SENSOR_COUNT];
        bool temp_ok[N2LVTS_SENSOR_COUNT];
};

bool n2lvts_init(struct n2lvts *l, const struct n2lvts_ops *ops, void *ctx);

/* Calibration.  efuse words are devinfo indices 116..119, 190..207 in this order (22 words). */
void n2lvts_cal_default(struct n2lvts_cal *c);
bool n2lvts_cal_from_efuse(const uint32_t w[22], struct n2lvts_cal *c);   /* false: all zero -> defaults, or implausible */
bool n2lvts_cal_plausible(const struct n2lvts_cal *c);

/* raw MSR -> milli-degC (vendor formula: -252.5 * raw / 2^14 + golden/2 + 252.5).  false: invalid bit, raw 0, or outside -40..150 C. */
bool n2lvts_msr_to_mC(uint32_t msr, uint32_t golden, int32_t *mC);

/* Level 1: read-only. */
enum n2lvts_error n2lvts_probe_ctrl(struct n2lvts *l, unsigned ctrl, struct n2lvts_probe *p);
/* Level 2/3: vendor init of the controllers in mask (open_gate = level 3). */
enum n2lvts_error n2lvts_start(struct n2lvts *l, uint32_t mask, bool open_gate);
/* Refresh temp_mC/temp_ok for every controller that is running or measuring (read-only). */
unsigned n2lvts_read(struct n2lvts *l, uint32_t mask);
/* Stop what we started: sensing off, devices reset, clock enable restored; re-gate only if we opened it. */
void n2lvts_stop(struct n2lvts *l);
const char *n2lvts_error_string(enum n2lvts_error e);

/* ---- guard and (design only, not wired) throttle policy ---- */
/* Cold-start high-OPP guard: false = refuse the request and use the minimum OPP.  Uncalibrated readings
 * count with a 10 C safety margin.  An unreadable sensor never blocks (nothing known). */
bool n2lvts_allows_cold_start_opp(int32_t temp_mC, bool valid, bool calibrated, int32_t max_mC);

struct n2lvts_throttle {
        int32_t soft_mC, hard_mC, hyst_mC;       /* e.g. 70 C / 80 C / 5 C */
        uint8_t level;                           /* 0 free, 1 mid cap, 2 minimum */
        uint8_t invalid_run;                     /* consecutive unreadable samples */
};
#define N2LVTS_THROTTLE_INVALID_MAX 5u
/* One decision step; hysteresis on the way down.  Persistently unreadable sensors go to level 1, never higher. */
uint8_t n2lvts_throttle_step(struct n2lvts_throttle *t, int32_t temp_mC, bool valid);
uint32_t n2lvts_throttle_khz_cap(uint8_t level);   /* 886000 / 654000 / 350000 */

#ifdef __cplusplus
}
#endif
#endif
