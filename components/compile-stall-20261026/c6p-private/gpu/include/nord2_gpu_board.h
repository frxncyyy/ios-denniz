/*
 * Nord 2 MT6893 / Mali-G77 board power sequencing.
 *
 * Order from the Nord 2 Android kernel 04d00c2565764c3922c9b4826b5acff1106e533f:
 *   platform/mt6893 pm_callback_power_on_nolock()
 *     -> mt_gpufreq_power_control(POWER_ON, CG_ON, MTCMOS_ON, BUCK_ON):
 *        buck (VSRAM_GPU, VGPU), MTCMOS (mfg_async, mfg, the stacks of
 *        MT_GPU_SHADER_PRESENT_9; PG_MFG0's parent clock mfg_sel opens
 *        first), CG (BG3D + external CG), then mt_gpufreq_set_timestamp().
 *   kbase_pm_init_hw(): reset, quirks, coherency; kbase_pm_l2_update_state()
 *     and kbase_pm_shaders_update_state(): L2/tiler then shader cores.
 *   Power off: cores (TTRX_921 flush), bus idle, CG, MTCMOS reverse, buck
 *     reverse (VGPU, VSRAM).
 *
 * Every step is recorded with ownership.  A failure rolls back exactly the
 * steps this sequencer performed; a domain, rail or gate that was already on
 * before preflight is never turned off by this code.  The preflight is
 * read-only and never touches GPU MMIO while MFG power is unknown.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_BOARD_H
#define NORD2_GPU_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "nord2_gpu_clk.h"
#include "nord2_gpu_hw.h"
#include "nord2_gpu_rail.h"
#include "nord2_gpu_scpsys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NORD2_GPU_REG_BASE 0x13000000u
#define NORD2_GPU_REG_SIZE 0x4000u
#define NORD2_GPU_MFG_BASE 0x13fbf000u
#define NORD2_GPU_MFG_SIZE 0x1000u
#define NORD2_GPU_IRQ_GPU 362u
#define NORD2_GPU_IRQ_MMU 363u
#define NORD2_GPU_IRQ_JOB 364u
#define NORD2_GPU_IRQ_EVENT 365u
#define NORD2_GPU_IRQ_PWR 366u
#define NORD2_GPU_MTCMOS_COUNT 7u

struct nord2_gpu_board_config {
        uint64_t reg_base;
        uint64_t reg_size;
        uint64_t mfg_base;
        uint64_t mfg_size;
        uint32_t irq[5];
        uint32_t expected_arch_major;
        uint32_t expected_arch_minor;
};

bool nord2_gpu_board_config_valid(const struct nord2_gpu_board_config *cfg);

enum nord2_gpu_stage {
        N2B_STAGE_NONE = 0,
        N2B_STAGE_PREFLIGHT,
        N2B_STAGE_RAILS,
        N2B_STAGE_MUX,
        N2B_STAGE_MTCMOS,
        N2B_STAGE_CG,
        N2B_STAGE_RESET,
        N2B_STAGE_IDENTIFY,
        N2B_STAGE_SLOTS,
        N2B_STAGE_QUIRKS,
        N2B_STAGE_CORES,
        N2B_STAGE_READY,
        N2B_STAGE_POWER_OFF,
};

struct nord2_gpu_board_report {
        uint32_t stage;            /* enum nord2_gpu_stage reached/failed */
        uint32_t error;            /* engine-specific error code */
        uint32_t mtcmos_before;    /* SPM PWR_STATUS & PWR_STATUS_2ND */
        uint32_t mtcmos_after;
        uint32_t mtcmos_failed_domain;
        uint32_t khz, vgpu_uv, vsram_uv, opp_index;
        struct n2rail_snapshot rail_before, rail_after;
        struct n2clk_snapshot clk_before;
        struct n2clk_snapshot clk_after;
        bool normalize_pll;       /* cold-only recovery to vendor minimum OPP */
        uint32_t gpu_id;
        uint64_t shader_present;
        bool rollback_clean;       /* false: something may still be on */
};

struct nord2_gpu_board {
        struct n2rail *rail;
        struct n2clk *clk;
        struct nord2_scpsys *scpsys;
        struct n2hw *hw;
        void (*log)(void *ctx, const char *msg);
        void *ctx;
        struct nord2_gpu_board_report report;
        bool mtcmos_by_us[NORD2_GPU_MTCMOS_COUNT];
        bool rails_by_us, mux_by_us, cg_by_us;
        bool preflight_ok;         /* last preflight passed, nothing since */
        bool powered;              /* everything up to READY verified */
        bool uncertain;            /* rollback incomplete: do not retry */
};

bool nord2_gpu_board_init(struct nord2_gpu_board *b, struct n2rail *rail,
                          struct n2clk *clk, struct nord2_scpsys *scpsys,
                          struct n2hw *hw,
                          void (*log)(void *ctx, const char *msg), void *ctx);
/* Read-only: SPM status, MFGPLL, rails; picks the vendor operating point. */
bool nord2_gpu_board_preflight(struct nord2_gpu_board *b);
/* Full staged power-on with rollback; requires a successful preflight. */
bool nord2_gpu_board_power_on(struct nord2_gpu_board *b);
/* Ordered power-off of what this sequencer owns.  Returns false if any
 * step failed; the GPU is then treated as possibly powered. */
bool nord2_gpu_board_power_off(struct nord2_gpu_board *b);
const char *nord2_gpu_stage_string(uint32_t stage);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_BOARD_H */
