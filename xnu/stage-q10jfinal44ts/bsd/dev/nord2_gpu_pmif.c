/*
 * MT6893 PMIF SWINF transport.  See nord2_gpu_pmif.h for provenance.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_pmif.h"

#include "nord2_gpu_compat.h"

static uint32_t sta(struct n2pmif *p)
{
        return p->last_sta = p->ops.read(p->ctx, N2PMIF_SWINF2_STA);
}

static bool ready_bits(const struct n2pmif *p, uint32_t v)
{
        return p->kind != N2PMIF_PWRAP || (v & N2PMIF_PWRAP_STA_READY);
}

/* Wait until the channel FSM equals want; bounded by N2PMIF_TIMEOUT_US. */
static bool wait_fsm(struct n2pmif *p, uint32_t want)
{
        for (unsigned waited = 0;; waited += N2PMIF_POLL_US) {
                uint32_t v = sta(p);
                if (ready_bits(p, v) && N2PMIF_FSM(v) == want)
                        return true;
                if (waited >= N2PMIF_TIMEOUT_US)
                        return false;
                p->ops.udelay(p->ctx, N2PMIF_POLL_US);
        }
}

/* Acquire an idle channel.  A WFVLDCLR left by this owner (for example a
 * read whose data was abandoned on timeout) is acknowledged first, as the
 * vendor pmif_leave_fsm_vldclr() does; anything else must drain itself. */
static enum n2pmif_error acquire(struct n2pmif *p)
{
        uint32_t v = sta(p);
        if (!ready_bits(p, v)) {
                p->errors++;
                return N2PMIF_E_NOT_INIT;
        }
        if (N2PMIF_FSM(v) == N2PMIF_FSM_WFVLDCLR && p->owned)
                p->ops.write(p->ctx, N2PMIF_SWINF2_VLD_CLR, 1);
        if (!wait_fsm(p, N2PMIF_FSM_IDLE)) {
                p->errors++;
                return N2PMIF_E_BUSY;
        }
        return N2PMIF_OK;
}

bool n2pmif_init(struct n2pmif *p, enum n2pmif_kind kind,
                 const struct n2pmif_ops *ops, void *ctx)
{
        if (!p || !ops || !ops->read || !ops->write || !ops->udelay ||
            (kind != N2PMIF_SPMI && kind != N2PMIF_PWRAP))
                return false;
        memset(p, 0, sizeof(*p));
        p->ops = *ops;
        p->ctx = ctx;
        p->kind = kind;
        return true;
}

enum n2pmif_error n2pmif_check_ready(struct n2pmif *p)
{
        if (!p || p->kind != N2PMIF_SPMI)
                return N2PMIF_E_ARGS;
        /* is_pmif_spmi_init_done(): PMIF_INIT_DONE bit0. */
        if (!(p->ops.read(p->ctx, N2PMIF_INIT_DONE) & 1u))
                return N2PMIF_E_NOT_INIT;
        return N2PMIF_OK;
}

static enum n2pmif_error transact_read(struct n2pmif *p, uint32_t cmd,
                                       uint32_t *data)
{
        enum n2pmif_error e = acquire(p);
        if (e != N2PMIF_OK)
                return e;
        p->owned = true;
        p->ops.write(p->ctx, N2PMIF_SWINF2_ACC, cmd);
        if (!wait_fsm(p, N2PMIF_FSM_WFVLDCLR)) {
                p->errors++;
                return N2PMIF_E_TIMEOUT;
        }
        *data = p->ops.read(p->ctx, N2PMIF_SWINF2_RDATA);
        p->ops.write(p->ctx, N2PMIF_SWINF2_VLD_CLR, 1);
        p->reads++;
        return N2PMIF_OK;
}

static enum n2pmif_error transact_write(struct n2pmif *p, uint32_t cmd,
                                        uint32_t data)
{
        enum n2pmif_error e = acquire(p);
        if (e != N2PMIF_OK)
                return e;
        p->owned = true;
        p->ops.write(p->ctx, N2PMIF_SWINF2_WDATA, data);
        p->ops.write(p->ctx, N2PMIF_SWINF2_ACC, cmd);
        /* The vendor write returns once queued; the existing Nord 2 PWRAP
         * write additionally waits for idle.  Waiting here makes completion
         * observable before any read-back verification. */
        if (!wait_fsm(p, N2PMIF_FSM_IDLE)) {
                p->errors++;
                return N2PMIF_E_TIMEOUT;
        }
        p->writes++;
        return N2PMIF_OK;
}

static uint32_t spmi_cmd(bool write, uint8_t sid, uint16_t addr)
{
        /* (opc << 30) | (write << 29) | (sid << 24) | (bc << 16) | addr,
         * bc = len - 1 = 0 for a single byte. */
        return (N2PMIF_CMD_EXT_REG_LONG << 30) | ((uint32_t)write << 29) |
               ((uint32_t)sid << 24) | addr;
}

enum n2pmif_error n2pmif_spmi_read(struct n2pmif *p, uint8_t sid,
                                   uint16_t addr, uint8_t *value)
{
        if (!p || p->kind != N2PMIF_SPMI || sid > 0xf || !value)
                return N2PMIF_E_ARGS;
        uint32_t data = 0;
        enum n2pmif_error e = transact_read(p, spmi_cmd(false, sid, addr),
                                            &data);
        if (e == N2PMIF_OK)
                *value = (uint8_t)data;
        return e;
}

enum n2pmif_error n2pmif_spmi_write(struct n2pmif *p, uint8_t sid,
                                    uint16_t addr, uint8_t value)
{
        if (!p || p->kind != N2PMIF_SPMI || sid > 0xf)
                return N2PMIF_E_ARGS;
        return transact_write(p, spmi_cmd(true, sid, addr), value);
}

enum n2pmif_error n2pmif_pwrap_read(struct n2pmif *p, uint16_t addr,
                                    uint16_t *value)
{
        if (!p || p->kind != N2PMIF_PWRAP || !value || (addr & 1u))
                return N2PMIF_E_ARGS;
        uint32_t data = 0;
        enum n2pmif_error e = transact_read(p, addr, &data);
        if (e == N2PMIF_OK)
                *value = (uint16_t)data;
        return e;
}

enum n2pmif_error n2pmif_pwrap_write(struct n2pmif *p, uint16_t addr,
                                     uint16_t value)
{
        if (!p || p->kind != N2PMIF_PWRAP || (addr & 1u))
                return N2PMIF_E_ARGS;
        return transact_write(p, (1u << 29) | addr, value);
}
