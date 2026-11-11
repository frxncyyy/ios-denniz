/*
 * MT6893 PMIF software-interface (SWINF) transport for GPU rails.
 *
 * Two controllers share the SWINF register layout used by the AP channel:
 *   PMIF-SPMI 0x10027000 ("mediatek,mt6885-pmif", ap_swinf_no = 2) carries
 *     MT6315 SID 7 (VGPU = 7_vbuck1).  Source: drivers/spmi/mtk-pmif.c,
 *     pmif_spmi_read_cmd()/pmif_spmi_write_cmd(), mt6xxx_regs[].
 *   PMIF-SPI / PWRAP 0x10026000 carries MT6359P (VSRAM_OTHERS).  The Nord 2
 *     input thread is that controller's sole owner; its existing proven
 *     helper uses the same SWINF2 offsets, the STA bit15 readiness bit and
 *     a (1 << 29) write flag without SID/opcode.  This module reproduces
 *     that sequence for the owner-thread hook; it never runs elsewhere.
 *
 * Every wait is bounded.  A transaction left in WFVLDCLR by this channel's
 * owner is acknowledged; a foreign non-idle state is reported, not forced.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_PMIF_H
#define NORD2_GPU_PMIF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2PMIF_SPMI_BASE 0x10027000u
#define N2PMIF_PWRAP_BASE 0x10026000u
#define N2PMIF_INIT_DONE 0x0000u
#define N2PMIF_SWINF2_ACC 0x0c80u
#define N2PMIF_SWINF2_WDATA 0x0c84u
#define N2PMIF_SWINF2_RDATA 0x0c94u
#define N2PMIF_SWINF2_VLD_CLR 0x0ca4u
#define N2PMIF_SWINF2_STA 0x0ca8u

#define N2PMIF_FSM(sta) (((sta) >> 1) & 7u)
#define N2PMIF_FSM_IDLE 0u
#define N2PMIF_FSM_WFVLDCLR 6u
#define N2PMIF_PWRAP_STA_READY (1u << 15)
/* PMIF_CMD_EXT_REG_LONG: SPMI extended long register access (16-bit addr). */
#define N2PMIF_CMD_EXT_REG_LONG 3u
/* pmif_wait_for_state() uses 10 ms. */
#define N2PMIF_TIMEOUT_US 10000u
#define N2PMIF_POLL_US 1u

enum n2pmif_kind {
        N2PMIF_SPMI = 1,  /* opcode/SID encoding, 8-bit data */
        N2PMIF_PWRAP = 2, /* existing Nord 2 PWRAP encoding, 16-bit data */
};

enum n2pmif_error {
        N2PMIF_OK = 0,
        N2PMIF_E_ARGS,
        N2PMIF_E_NOT_INIT,     /* controller not initialised by the boot chain */
        N2PMIF_E_BUSY,         /* foreign transaction / not idle in time */
        N2PMIF_E_TIMEOUT,      /* our read never produced valid data */
};

struct n2pmif_ops {
        uint32_t (*read)(void *ctx, uint32_t off);
        void (*write)(void *ctx, uint32_t off, uint32_t value);
        void (*udelay)(void *ctx, unsigned us);
};

struct n2pmif {
        struct n2pmif_ops ops;
        void *ctx;
        enum n2pmif_kind kind;
        bool owned;          /* this channel issued the last transaction */
        uint32_t last_sta;
        uint32_t reads, writes, errors;
};

bool n2pmif_init(struct n2pmif *p, enum n2pmif_kind kind,
                 const struct n2pmif_ops *ops, void *ctx);
/* PMIF-SPMI only: PMIF_INIT_DONE bit0 must be set by the boot chain. */
enum n2pmif_error n2pmif_check_ready(struct n2pmif *p);
/* MT6315 byte access (SPMI controller). */
enum n2pmif_error n2pmif_spmi_read(struct n2pmif *p, uint8_t sid,
                                   uint16_t addr, uint8_t *value);
enum n2pmif_error n2pmif_spmi_write(struct n2pmif *p, uint8_t sid,
                                    uint16_t addr, uint8_t value);
/* MT6359P 16-bit access (PWRAP controller, owner thread only). */
enum n2pmif_error n2pmif_pwrap_read(struct n2pmif *p, uint16_t addr,
                                    uint16_t *value);
enum n2pmif_error n2pmif_pwrap_write(struct n2pmif *p, uint16_t addr,
                                     uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_PMIF_H */
