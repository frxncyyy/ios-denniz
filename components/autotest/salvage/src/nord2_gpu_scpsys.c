/*
 * Nord 2 MT6893 SCPsys MFG MTCMOS sequencing.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_scpsys.h"

#include <string.h>

#define SPM_POWERON_CONFIG_EN 0x000u
#define SPM_PWR_STATUS 0x16cu
#define SPM_PWR_STATUS_2ND 0x170u
#define SPM_MFG_PWR_CON(d) (0x308u + 4u * (d))

#define SPM_PROJECT_CODE 0xB16u
#define PWR_RST_B (1u << 0)
#define PWR_ISO (1u << 1)
#define PWR_ON (1u << 2)
#define PWR_ON_2ND (1u << 3)
#define PWR_CLK_DIS (1u << 4)
#define SRAM_PDN (1u << 8)
#define SRAM_PDN_ACK (1u << 12)
#define MFG_STA(d) (1u << (2u + (d)))

#define INFRA_PROTECTEN_STA1 0x228u
#define INFRA_PROTECTEN_SET 0x2a0u
#define INFRA_PROTECTEN_CLR 0x2a4u
#define INFRA_PROTECTEN_1_SET 0x2a8u
#define INFRA_PROTECTEN_1_CLR 0x2acu
#define INFRA_PROTECTEN_STA1_1 0x258u
#define INFRA_PROTECTEN_2_SET 0x714u
#define INFRA_PROTECTEN_2_CLR 0x718u
#define INFRA_PROTECTEN_STA1_2 0x724u
#define INFRA_SUB_VDNR_SET 0xbb8u
#define INFRA_SUB_VDNR_CLR 0xbbcu
#define INFRA_SUB_VDNR_STA1 0xbc4u

#define MFG1_STEP1_0 ((1u << 19) | (1u << 20) | (1u << 21))
#define MFG1_STEP1_1 ((1u << 5) | (1u << 6))
#define MFG1_STEP2_0 ((1u << 21) | (1u << 22))
#define MFG1_STEP2_1 (1u << 7)
#define MFG1_STEP2_2 ((1u << 17) | (1u << 19))

static bool wait_spm(struct nord2_scpsys *s, uint32_t off, uint32_t mask,
                     uint32_t want, const char *why)
{
        for (unsigned waited = 0; waited < s->timeout_us; waited += 10) {
                if ((s->ops.read_spm(s->ctx, off) & mask) == want)
                        return true;
                s->ops.udelay(s->ctx, 10);
        }
        if (s->ops.log)
                s->ops.log(s->ctx, why);
        return false;
}

static bool wait_infra(struct nord2_scpsys *s, uint32_t off, uint32_t mask,
                       uint32_t want, const char *why)
{
        for (unsigned waited = 0; waited < s->timeout_us; waited += 10) {
                if ((s->ops.read_infra(s->ctx, off) & mask) == want)
                        return true;
                s->ops.udelay(s->ctx, 10);
        }
        if (s->ops.log)
                s->ops.log(s->ctx, why);
        return false;
}

static void rmw_spm(struct nord2_scpsys *s, uint32_t off, uint32_t set,
                    uint32_t clear)
{
        uint32_t v = s->ops.read_spm(s->ctx, off);
        v = (v | set) & ~clear;
        s->ops.write_spm(s->ctx, off, v);
}

static bool protect_mfg1(struct nord2_scpsys *s, bool protect)
{
        if (protect) {
                s->ops.write_infra(s->ctx, INFRA_PROTECTEN_1_SET, MFG1_STEP1_0);
                if (!wait_infra(s, INFRA_PROTECTEN_STA1_1, MFG1_STEP1_0,
                                MFG1_STEP1_0, "MFG1 protect step1.0 timeout"))
                        return false;
                s->ops.write_infra(s->ctx, INFRA_PROTECTEN_2_SET, MFG1_STEP1_1);
                if (!wait_infra(s, INFRA_PROTECTEN_STA1_2, MFG1_STEP1_1,
                                MFG1_STEP1_1, "MFG1 protect step1.1 timeout"))
                        return false;
                s->ops.write_infra(s->ctx, INFRA_PROTECTEN_SET, MFG1_STEP2_0);
                if (!wait_infra(s, INFRA_PROTECTEN_STA1, MFG1_STEP2_0,
                                MFG1_STEP2_0, "MFG1 protect step2.0 timeout"))
                        return false;
                s->ops.write_infra(s->ctx, INFRA_PROTECTEN_2_SET, MFG1_STEP2_1);
                if (!wait_infra(s, INFRA_PROTECTEN_STA1_2, MFG1_STEP2_1,
                                MFG1_STEP2_1, "MFG1 protect step2.1 timeout"))
                        return false;
                s->ops.write_infra(s->ctx, INFRA_SUB_VDNR_SET, MFG1_STEP2_2);
                if (!wait_infra(s, INFRA_SUB_VDNR_STA1, MFG1_STEP2_2,
                                MFG1_STEP2_2, "MFG1 protect step2.2 timeout"))
                        return false;
                return true;
        }
        /* Release order follows the vendor: 2.0, 2.1, 2.2, 1.0, 1.1. */
        s->ops.write_infra(s->ctx, INFRA_PROTECTEN_CLR, MFG1_STEP2_0);
        s->ops.write_infra(s->ctx, INFRA_PROTECTEN_2_CLR, MFG1_STEP2_1);
        s->ops.write_infra(s->ctx, INFRA_SUB_VDNR_CLR, MFG1_STEP2_2);
        s->ops.write_infra(s->ctx, INFRA_PROTECTEN_1_CLR, MFG1_STEP1_0);
        s->ops.write_infra(s->ctx, INFRA_PROTECTEN_2_CLR, MFG1_STEP1_1);
        return true;
}

bool nord2_scpsys_init(struct nord2_scpsys *s,
                       const struct nord2_scpsys_ops *ops, void *ctx,
                       unsigned timeout_us)
{
        if (!s || !ops || !ops->read_spm || !ops->write_spm ||
            !ops->read_infra || !ops->write_infra || !ops->udelay ||
            !timeout_us)
                return false;
        memset(s, 0, sizeof(*s));
        s->ops = *ops;
        s->ctx = ctx;
        s->timeout_us = timeout_us;
        return true;
}

bool nord2_scpsys_mfg_status(const struct nord2_scpsys *s, unsigned domain,
                             bool *on)
{
        if (!s || domain > 6 || !on)
                return false;
        uint32_t mask = MFG_STA(domain);
        *on = (s->ops.read_spm(s->ctx, SPM_PWR_STATUS) & mask) &&
              (s->ops.read_spm(s->ctx, SPM_PWR_STATUS_2ND) & mask);
        return true;
}

bool nord2_scpsys_mfg_on(struct nord2_scpsys *s, unsigned domain)
{
        if (!s || domain > 6)
                return false;
        uint32_t con = SPM_MFG_PWR_CON(domain);
        uint32_t mask = MFG_STA(domain);
        s->ops.write_spm(s->ctx, SPM_POWERON_CONFIG_EN,
                         (SPM_PROJECT_CODE << 16) | 1u);
        rmw_spm(s, con, PWR_ON, 0);
        rmw_spm(s, con, PWR_ON_2ND, 0);
        if (!wait_spm(s, SPM_PWR_STATUS, mask, mask, "MFG on timeout") ||
            !wait_spm(s, SPM_PWR_STATUS_2ND, mask, mask,
                      "MFG on 2nd timeout"))
                return false;
        rmw_spm(s, con, 0, PWR_CLK_DIS);
        rmw_spm(s, con, 0, PWR_ISO);
        rmw_spm(s, con, PWR_RST_B, 0);
        rmw_spm(s, con, 0, SRAM_PDN);
        if (!wait_spm(s, con, SRAM_PDN_ACK, 0, "MFG SRAM ack timeout"))
                return false;
        if (domain == 1 && !protect_mfg1(s, false))
                return false;
        return true;
}

bool nord2_scpsys_mfg_off(struct nord2_scpsys *s, unsigned domain)
{
        if (!s || domain > 6)
                return false;
        uint32_t con = SPM_MFG_PWR_CON(domain);
        uint32_t mask = MFG_STA(domain);
        s->ops.write_spm(s->ctx, SPM_POWERON_CONFIG_EN,
                         (SPM_PROJECT_CODE << 16) | 1u);
        if (domain == 1 && !protect_mfg1(s, true))
                return false;
        rmw_spm(s, con, SRAM_PDN, 0);
        if (!wait_spm(s, con, SRAM_PDN_ACK, SRAM_PDN_ACK,
                      "MFG SRAM pdn timeout"))
                return false;
        rmw_spm(s, con, PWR_ISO, 0);
        rmw_spm(s, con, PWR_CLK_DIS, 0);
        rmw_spm(s, con, 0, PWR_RST_B);
        rmw_spm(s, con, 0, PWR_ON);
        rmw_spm(s, con, 0, PWR_ON_2ND);
        if (!wait_spm(s, SPM_PWR_STATUS, mask, 0, "MFG off timeout") ||
            !wait_spm(s, SPM_PWR_STATUS_2ND, mask, 0,
                      "MFG off 2nd timeout"))
                return false;
        return true;
}

bool nord2_scpsys_mfg_all_on(struct nord2_scpsys *s)
{
        if (!s)
                return false;
        for (unsigned d = 0; d < 7; d++) {
                if (!nord2_scpsys_mfg_on(s, d)) {
                        for (unsigned r = d; r > 0; r--)
                                (void)nord2_scpsys_mfg_off(s, r - 1);
                        return false;
                }
        }
        return true;
}

void nord2_scpsys_mfg_all_off(struct nord2_scpsys *s)
{
        if (!s)
                return;
        for (unsigned d = 7; d > 0; d--)
                (void)nord2_scpsys_mfg_off(s, d - 1);
}
