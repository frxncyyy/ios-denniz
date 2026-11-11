/*
 * Nord 2 MT6893 SCPsys MFG MTCMOS sequencing.
 *
 * Transcribed from drivers/clk/mediatek/clk-mt6885-pg.c (MT6893 table):
 * SPM PWR_CON 0x308..0x320, PWR_STATUS 0x16c, PWR_STATUS_2ND 0x170,
 * POWERON_CONFIG_EN 0x0; MFG1 bus-protection SET/CLR on infracfg 0x10001000.
 * This module only sequences MTCMOS; it does not touch VGPU/VSRAM rails,
 * PLLs, voltages or frequency.  It is not called while the rail callbacks
 * refuse, so an unpowered GPU is never switched here.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_SCPSYS_H
#define NORD2_GPU_SCPSYS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nord2_scpsys_ops {
        uint32_t (*read_spm)(void *ctx, uint32_t off);
        void (*write_spm)(void *ctx, uint32_t off, uint32_t value);
        uint32_t (*read_infra)(void *ctx, uint32_t off);
        void (*write_infra)(void *ctx, uint32_t off, uint32_t value);
        void (*udelay)(void *ctx, unsigned us);
        void (*log)(void *ctx, const char *msg);
};

struct nord2_scpsys {
        struct nord2_scpsys_ops ops;
        void *ctx;
        unsigned timeout_us;
};

bool nord2_scpsys_init(struct nord2_scpsys *s,
                       const struct nord2_scpsys_ops *ops, void *ctx,
                       unsigned timeout_us);
/* domain 0 == MFG0 ... 6 == MFG6. */
bool nord2_scpsys_mfg_on(struct nord2_scpsys *s, unsigned domain);
bool nord2_scpsys_mfg_off(struct nord2_scpsys *s, unsigned domain);
bool nord2_scpsys_mfg_status(const struct nord2_scpsys *s, unsigned domain,
                             bool *on);
bool nord2_scpsys_mfg_all_on(struct nord2_scpsys *s);
void nord2_scpsys_mfg_all_off(struct nord2_scpsys *s);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_SCPSYS_H */
