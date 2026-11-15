/*
 * Nord 2 Mali-G77 GPU-internal hardware protocol.  See nord2_gpu_hw.h for
 * the vendor sources each sequence is transcribed from.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_hw.h"

#include "nord2_gpu_compat.h"

static uint32_t rd(struct n2hw *hw, uint32_t off)
{
        return hw->ops.read(hw->ctx, off);
}

static void wr(struct n2hw *hw, uint32_t off, uint32_t value)
{
        hw->ops.write(hw->ctx, off, value);
}

static uint64_t rd64(struct n2hw *hw, uint32_t lo)
{
        return (uint64_t)rd(hw, lo) | ((uint64_t)rd(hw, lo + 4) << 32);
}

static void wr64(struct n2hw *hw, uint32_t lo, uint64_t value)
{
        wr(hw, lo, (uint32_t)value);
        wr(hw, lo + 4, (uint32_t)(value >> 32));
}

static enum n2hw_error fail(struct n2hw *hw, enum n2hw_error e,
                            uint32_t off, uint32_t value, const char *msg)
{
        hw->last_error = e;
        hw->last_offset = off;
        hw->last_value = value;
        if (hw->ops.log && msg)
                hw->ops.log(hw->ctx, msg);
        return e;
}

/* Poll (reg & mask) == want with an explicit time bound. */
static bool wait32(struct n2hw *hw, uint32_t off, uint32_t mask,
                   uint32_t want, unsigned timeout_us, uint32_t *last)
{
        uint32_t v = rd(hw, off);
        for (unsigned waited = 0; (v & mask) != want; waited += N2HW_POLL_US) {
                if (waited >= timeout_us) {
                        if (last)
                                *last = v;
                        return false;
                }
                hw->ops.udelay(hw->ctx, N2HW_POLL_US);
                v = rd(hw, off);
        }
        if (last)
                *last = v;
        return true;
}

bool n2hw_init(struct n2hw *hw, const struct n2hw_ops *ops, void *ctx)
{
        if (!hw || !ops || !ops->read || !ops->write || !ops->udelay)
                return false;
        memset(hw, 0, sizeof(*hw));
        hw->ops = *ops;
        hw->ctx = ctx;
        return true;
}

enum n2hw_error n2hw_identify(struct n2hw *hw)
{
        if (!hw)
                return N2HW_E_ARGS;
        struct n2hw_identity id;
        memset(&id, 0, sizeof(id));
        hw->identified = false;
        id.gpu_id = rd(hw, N2HW_GPU_ID);
        hw->id.gpu_id = id.gpu_id; /* retained for diagnostics on refusal */
        /* tTRx is GPU_ID2_MODEL_MAKE(9, 0) with arch 9.0. */
        if ((id.gpu_id & 0xff0f0000u) != 0x90000000u)
                return fail(hw, N2HW_E_PRODUCT, N2HW_GPU_ID, id.gpu_id,
                            "Nord2 GPU: GPU_ID is not tTRx arch 9.0");
        id.version_status = id.gpu_id & 0xfu;
        id.version_minor = (id.gpu_id >> 4) & 0xffu;
        id.version_major = (id.gpu_id >> 12) & 0xfu;
        /* mali_kbase_hw.c: 0.0.x is r0p0, 0.1.x is r0p1 (TTRX_3485 present,
         * kbase then requires the dummy-job firmware blob). */
        hw->has_issue_ttrx_3485 = id.version_major == 0 && id.version_minor < 2;
        id.l2_features = rd(hw, N2HW_L2_FEATURES);
        id.core_features = rd(hw, N2HW_CORE_FEATURES);
        id.tiler_features = rd(hw, N2HW_TILER_FEATURES);
        id.mem_features = rd(hw, N2HW_MEM_FEATURES);
        id.mmu_features = rd(hw, N2HW_MMU_FEATURES);
        id.as_present = rd(hw, N2HW_AS_PRESENT);
        id.js_present = rd(hw, N2HW_JS_PRESENT);
        id.thread_max_threads = rd(hw, N2HW_THREAD_MAX_THREADS);
        id.thread_max_workgroup_size = rd(hw, N2HW_THREAD_MAX_WORKGROUP_SIZE);
        id.thread_features = rd(hw, N2HW_THREAD_FEATURES);
        id.coherency_features = rd(hw, N2HW_COHERENCY_FEATURES);
        for (unsigned i = 0; i < 4; i++)
                id.texture_features[i] =
                        rd(hw, N2HW_TEXTURE_FEATURES_0 + 4 * i);
        for (unsigned i = 0; i < 16; i++)
                id.js_features[i] = (id.js_present & (1u << i))
                        ? rd(hw, N2HW_JS0_FEATURES + 4 * i) : 0;
        id.shader_present = rd64(hw, N2HW_SHADER_PRESENT_LO);
        id.tiler_present = rd64(hw, N2HW_TILER_PRESENT_LO);
        id.l2_present = rd64(hw, N2HW_L2_PRESENT_LO);
        /* The MTCMOS stacks enabled by the board are exactly the vendor
         * MT_GPU_SHADER_PRESENT_9 set; any core outside it is unpowered. */
        if (!id.shader_present ||
            (id.shader_present & ~N2HW_MT6893_SHADER_PRESENT_9) ||
            id.tiler_present != 1 || !id.l2_present ||
            !(id.as_present & 1u) || (id.js_present & 3u) != 3u)
                return fail(hw, N2HW_E_PRESENT, N2HW_SHADER_PRESENT_LO,
                            (uint32_t)id.shader_present,
                            "Nord2 GPU: implausible core/AS/slot presence");
        hw->id = id;
        /* tTRx r0p2 issue list (and every later revision kbase knows). */
        hw->has_issue_ttrx_2968 = true;
        hw->has_issue_ttrx_921 = true;
        hw->has_issue_ttrx_3076 = true;
        hw->has_issue_gpu2017_1336 = true;
        hw->identified = true;
        return N2HW_OK;
}

enum n2hw_error n2hw_select_slots(struct n2hw *hw)
{
        if (!hw || !hw->identified)
                return N2HW_E_NOT_IDENTIFIED;
        const uint32_t fs = hw->id.js_features[0];
        const uint32_t vt = hw->id.js_features[1];
        hw->slots_valid = false;
        /* kbase_js_get_slot(): FS -> 0, everything else -> 1 (JS2 only for
         * a second core group, which tTRx MC9 does not report here). */
        if (!(fs & N2HW_JS_FEATURE_FRAGMENT_JOB) ||
            !(vt & N2HW_JS_FEATURE_COMPUTE_JOB) ||
            !(vt & N2HW_JS_FEATURE_TILER_JOB))
                return fail(hw, N2HW_E_SLOTS, N2HW_JS0_FEATURES, fs,
                            "Nord2 GPU: JSn_FEATURES violate vendor slot rule");
        hw->slot_fragment = 0;
        hw->slot_vertex_tiler = 1;
        hw->slot_compute = 1;
        hw->slots_valid = true;
        return N2HW_OK;
}

enum n2hw_error n2hw_reset(struct n2hw *hw)
{
        if (!hw)
                return N2HW_E_ARGS;
        uint32_t last = 0;
        /* kbase_pm_init_hw: interrupts off and clear before reset. */
        wr(hw, N2HW_GPU_IRQ_MASK, 0);
        wr(hw, N2HW_GPU_IRQ_CLEAR, 0xffffffffu);
        wr(hw, N2HW_JOB_IRQ_MASK, 0);
        wr(hw, N2HW_JOB_IRQ_CLEAR, 0xffffffffu);
        wr(hw, N2HW_MMU_IRQ_MASK, 0);
        wr(hw, N2HW_MMU_IRQ_CLEAR, 0xffffffffu);
        hw->l2_on = false;
        hw->wa_complete = false;
        hw->shaders_on = 0;
        memset(hw->job_chain_flag, 0, sizeof(hw->job_chain_flag));
        wr(hw, N2HW_GPU_COMMAND, N2HW_CMD_SOFT_RESET);
        hw->resets_soft++;
        if (!wait32(hw, N2HW_GPU_IRQ_RAWSTAT, N2HW_IRQ_RESET_COMPLETED,
                    N2HW_IRQ_RESET_COMPLETED, N2HW_RESET_TIMEOUT_US, &last)) {
                if (hw->ops.log)
                        hw->ops.log(hw->ctx,
                                    "Nord2 GPU: soft reset timeout; hard reset");
                wr(hw, N2HW_GPU_COMMAND, N2HW_CMD_HARD_RESET);
                hw->resets_hard++;
                if (!wait32(hw, N2HW_GPU_IRQ_RAWSTAT,
                            N2HW_IRQ_RESET_COMPLETED,
                            N2HW_IRQ_RESET_COMPLETED, N2HW_RESET_TIMEOUT_US,
                            &last))
                        return fail(hw, N2HW_E_RESET_TIMEOUT,
                                    N2HW_GPU_IRQ_RAWSTAT, last,
                                    "Nord2 GPU: hard reset timeout");
        }
        wr(hw, N2HW_GPU_IRQ_CLEAR, 0xffffffffu);
        uint32_t status = rd(hw, N2HW_GPU_STATUS);
        if (status & N2HW_GPU_STATUS_PROTECTED)
                return fail(hw, N2HW_E_PROTECTED, N2HW_GPU_STATUS, status,
                            "Nord2 GPU: protected mode active after reset");
        return N2HW_OK;
}

enum n2hw_error n2hw_apply_quirks(struct n2hw *hw)
{
        if (!hw || !hw->identified)
                return N2HW_E_NOT_IDENTIFIED;
        /* kbase_set_gpu_quirks (JM, not tMIx): JM_CONFIG plus the IDVS
         * group size; CONFIG_MALI_CORESTACK=n so no manual power bit. */
        uint32_t jm = rd(hw, N2HW_JM_CONFIG);
        jm |= N2HW_IDVS_GROUP_SIZE_DEFAULT << N2HW_IDVS_GROUP_SIZE_SHIFT;
        /* kbase_set_sc_quirks: tTRx prod_id is above both legacy ranges. */
        uint32_t sc = rd(hw, N2HW_SHADER_CONFIG);
        if (hw->has_issue_ttrx_2968)
                sc |= N2HW_SC_VAR_ALGORITHM;
        /* kbase_set_tiler_quirks: no tTRx tiler issue; value unchanged. */
        uint32_t tiler = rd(hw, N2HW_TILER_CONFIG);
        /* kbase_set_mmu_quirks: tTRx read GENMASK(12,7)=0, write
         * GENMASK(17,13)=0; COHERENCY_NONE adds no snoop disparity bit. */
        uint32_t mmu = rd(hw, N2HW_L2_MMU_CONFIG);
        mmu &= ~(N2HW_L2_MMU_TTRX_READ_LIMIT_MASK |
                 N2HW_L2_MMU_TTRX_WRITE_LIMIT_MASK);
        /* kbase_pm_hw_issues_apply order. */
        wr(hw, N2HW_SHADER_CONFIG, sc);
        wr(hw, N2HW_TILER_CONFIG, tiler);
        wr(hw, N2HW_L2_MMU_CONFIG, mmu);
        wr(hw, N2HW_JM_CONFIG, jm);
        /* kbase_cache_set_coherency_mode(system_coherency=COHERENCY_NONE)
         * because the MT6893 mali node has no system-coherency property. */
        wr(hw, N2HW_COHERENCY_ENABLE, N2HW_COHERENCY_NONE);
        hw->quirk_shader_config = sc;
        hw->quirk_tiler_config = tiler;
        hw->quirk_l2_mmu_config = mmu;
        hw->quirk_jm_config = jm;
        return N2HW_OK;
}

static void invoke(struct n2hw *hw, uint32_t reg_lo, uint64_t cores)
{
        /* kbase_pm_invoke writes only nonzero halves. */
        if ((uint32_t)cores)
                wr(hw, reg_lo, (uint32_t)cores);
        if ((uint32_t)(cores >> 32))
                wr(hw, reg_lo + 4, (uint32_t)(cores >> 32));
}

static bool wait_cores(struct n2hw *hw, uint32_t ready_lo, uint32_t trans_lo,
                       uint64_t mask, uint64_t want)
{
        for (unsigned waited = 0;; waited += N2HW_POLL_US) {
                uint64_t ready = rd64(hw, ready_lo);
                uint64_t trans = rd64(hw, trans_lo) & ~ready;
                if ((ready & mask) == want && !(trans & mask))
                        return true;
                if (waited >= N2HW_POWER_TIMEOUT_US) {
                        hw->last_offset = ready_lo;
                        hw->last_value = (uint32_t)ready;
                        return false;
                }
                hw->ops.udelay(hw->ctx, N2HW_POLL_US);
        }
}

enum n2hw_error n2hw_power_on_l2(struct n2hw *hw)
{
        if (!hw || !hw->identified)
                return N2HW_E_NOT_IDENTIFIED;
        const struct n2hw_identity *id = &hw->id;
        if (!hw->l2_on) {
                /* KBASE_L2_OFF (JM): powering the tiler powers L2 slice 0;
                 * other slices are explicit. */
                invoke(hw, N2HW_TILER_PWRON_LO, id->tiler_present);
                if (id->l2_present != 1)
                        invoke(hw, N2HW_L2_PWRON_LO, id->l2_present & ~1ull);
                /* KBASE_L2_PEND_ON. */
                if (!wait_cores(hw, N2HW_L2_READY_LO, N2HW_L2_PWRTRANS_LO,
                                id->l2_present, id->l2_present) ||
                    !wait_cores(hw, N2HW_TILER_READY_LO,
                                N2HW_TILER_PWRTRANS_LO, id->tiler_present,
                                id->tiler_present))
                        return fail(hw, N2HW_E_L2_TIMEOUT, hw->last_offset,
                                    hw->last_value,
                                    "Nord2 GPU: L2/tiler power-on timeout");
                hw->l2_on = true;
        }
        return N2HW_OK;
}

enum n2hw_error n2hw_power_on_cores(struct n2hw *hw, uint64_t shader_mask)
{
        if (!hw || !hw->identified)
                return N2HW_E_NOT_IDENTIFIED;
        if (!shader_mask || (shader_mask & ~hw->id.shader_present))
                return N2HW_E_ARGS;
        if (hw->has_issue_ttrx_3485 && !hw->wa_complete)
                return fail(hw, N2HW_E_REVISION_WA, N2HW_GPU_ID, hw->id.gpu_id,
                            "Nord2 GPU: TTRX_3485 must complete before shaders");
        enum n2hw_error e = n2hw_power_on_l2(hw);
        if (e != N2HW_OK) return e;
        uint64_t add = shader_mask & ~hw->shaders_on;
        if (add) {
                invoke(hw, N2HW_SHADER_PWRON_LO, add);
                if (!wait_cores(hw, N2HW_SHADER_READY_LO,
                                N2HW_SHADER_PWRTRANS_LO, shader_mask,
                                shader_mask))
                        return fail(hw, N2HW_E_SHADER_TIMEOUT,
                                    hw->last_offset, hw->last_value,
                                    "Nord2 GPU: shader power-on timeout");
        }
        hw->shaders_on |= shader_mask;
        return N2HW_OK;
}

/* kbase_dummy_job_wa_execute for the hash-pinned vendor flags=6 image.
 * This is not a regular submission: descriptor writeback must be disabled,
 * there is no generic flush/priority/chain flag, and shaders power off again.
 * Unlike the vendor helper, every wait is checked and failure stops the path. */
enum n2hw_error n2hw_dummy_job_wa(struct n2hw *hw)
{
        if (!hw || !hw->identified || !hw->slots_valid || !hw->l2_on ||
            !hw->has_issue_ttrx_3485 || hw->shaders_on || hw->wa_complete)
                return N2HW_E_ARGS;
        for (unsigned slot = 0; slot < 16; slot++)
                if ((hw->id.js_present & (1u << slot)) && !n2hw_js_idle(hw, slot))
                        return fail(hw, N2HW_E_SLOT_BUSY, N2HW_JOB_IRQ_JS_STATE,
                                    rd(hw, N2HW_JOB_IRQ_JS_STATE), "Nord2 GPU WA: slot busy");
        uint32_t last = rd(hw, N2HW_MMU_IRQ_RAWSTAT);
        if (last) return fail(hw, N2HW_E_REVISION_WA, N2HW_MMU_IRQ_RAWSTAT,
                              last, "Nord2 GPU WA: preexisting MMU fault");
        uint32_t gm = rd(hw, N2HW_GPU_IRQ_MASK), jm = rd(hw, N2HW_JOB_IRQ_MASK);
        wr(hw, N2HW_GPU_IRQ_MASK, 0);
        wr(hw, N2HW_JOB_IRQ_MASK, 0);
        enum n2hw_error e = N2HW_E_REVISION_WA;
        uint64_t cores = hw->id.shader_present;
        hw->shaders_on = cores; /* keep ownership even if acknowledgement fails */
        wr64(hw, N2HW_SHADER_PWRON_LO, cores);
        if (!wait_cores(hw, N2HW_SHADER_READY_LO, N2HW_SHADER_PWRTRANS_LO, cores, cores))
                goto failed;
        wr(hw, N2HW_JOB_IRQ_CLEAR, 0x10001u);
        wr64(hw, N2HW_JS_REG(0, N2HW_JS_HEAD_NEXT_LO), 0x100000000ull);
        wr64(hw, N2HW_JS_REG(0, N2HW_JS_AFFINITY_NEXT_LO), cores);
        wr(hw, N2HW_JS_REG(0, N2HW_JS_CONFIG_NEXT), 1u << 15); /* disable writeback, AS0 */
        wr(hw, N2HW_JS_REG(0, N2HW_JS_COMMAND_NEXT), N2HW_JS_CMD_START);
        for (unsigned us = 0; ; us += 10) {
                last = rd(hw, N2HW_JOB_IRQ_RAWSTAT) & 0x10001u;
                if (last || us >= 1000) break;
                hw->ops.udelay(hw->ctx, 10);
        }
        wr(hw, N2HW_JOB_IRQ_CLEAR, last);
        if (last != 1) {
                hw->last_offset = N2HW_JS_REG(0, N2HW_JS_STATUS);
                hw->last_value = rd(hw, hw->last_offset);
                goto failed;
        }
        last = rd(hw, N2HW_MMU_IRQ_RAWSTAT);
        if (last || !n2hw_js_idle(hw, 0)) {
                hw->last_offset = last ? N2HW_MMU_IRQ_RAWSTAT : N2HW_JOB_IRQ_JS_STATE;
                hw->last_value = rd(hw, hw->last_offset);
                goto failed;
        }
        /* LOGICAL_SHADER_POWER: temporary job before normal shader lifetime. */
        wr64(hw, N2HW_SHADER_PWROFF_LO, cores);
        if (!wait_cores(hw, N2HW_SHADER_READY_LO, N2HW_SHADER_PWRTRANS_LO, cores, 0))
                goto failed;
        hw->shaders_on = 0;
        wr(hw, N2HW_GPU_IRQ_CLEAR, 0xffffffffu);
        hw->wa_complete = true;
        hw->wa_runs++;
        e = N2HW_OK;
failed:
        wr(hw, N2HW_GPU_IRQ_MASK, gm);
        wr(hw, N2HW_JOB_IRQ_MASK, jm);
        if (e != N2HW_OK)
                return fail(hw, e, hw->last_offset, hw->last_value,
                            "Nord2 GPU WA: failed; DMA memory retained, no normal jobs");
        if (hw->ops.log) hw->ops.log(hw->ctx, "Nord2 GPU WA: TTRX_3485 completed");
        return N2HW_OK;
}

enum n2hw_error n2hw_cache_clean_inv(struct n2hw *hw)
{
        if (!hw)
                return N2HW_E_ARGS;
        if (!hw->l2_on)
                return N2HW_E_POWER_STATE;
        uint32_t last = 0;
        wr(hw, N2HW_GPU_IRQ_CLEAR, N2HW_IRQ_CLEAN_CACHES_COMPLETED);
        wr(hw, N2HW_GPU_COMMAND, N2HW_CMD_CLEAN_INV_CACHES);
        if (!wait32(hw, N2HW_GPU_IRQ_RAWSTAT,
                    N2HW_IRQ_CLEAN_CACHES_COMPLETED,
                    N2HW_IRQ_CLEAN_CACHES_COMPLETED, N2HW_CACHE_TIMEOUT_US,
                    &last))
                return fail(hw, N2HW_E_CACHE_TIMEOUT, N2HW_GPU_IRQ_RAWSTAT,
                            last, "Nord2 GPU: cache clean timeout");
        wr(hw, N2HW_GPU_IRQ_CLEAR, N2HW_IRQ_CLEAN_CACHES_COMPLETED);
        return N2HW_OK;
}

enum n2hw_error n2hw_power_off_cores(struct n2hw *hw)
{
        if (!hw)
                return N2HW_E_ARGS;
        enum n2hw_error e = N2HW_OK;
        if (hw->shaders_on) {
                /* KBASE_SHADERS_WAIT_FINISHED_CORESTACK_ON with TTRX_921. */
                if (hw->has_issue_ttrx_921 &&
                    (e = n2hw_cache_clean_inv(hw)) != N2HW_OK)
                        return e;
                invoke(hw, N2HW_SHADER_PWROFF_LO, hw->shaders_on);
                if (!wait_cores(hw, N2HW_SHADER_READY_LO,
                                N2HW_SHADER_PWRTRANS_LO, hw->shaders_on, 0))
                        return fail(hw, N2HW_E_SHADER_TIMEOUT,
                                    hw->last_offset, hw->last_value,
                                    "Nord2 GPU: shader power-off timeout");
                hw->shaders_on = 0;
        }
        /* GPU2017_1336 (WA1): L2 stays on; flush before the GPU loses power.
         * The MTCMOS power-off by the board then removes L2 state. */
        if (hw->l2_on)
                e = n2hw_cache_clean_inv(hw);
        return e;
}

bool n2hw_lock_region(uint64_t va, uint64_t size, uint64_t *lockaddr)
{
        /* lock_region(): smallest naturally aligned power-of-two region
         * containing [va, va + size - 1], at least 32 KiB. */
        if (!lockaddr || !size || (va & 0xfffu) || (size & 0xfffu) ||
            va >= (1ull << 48) || size > (1ull << 48) - va)
                return false;
        uint64_t end = va + size - 1;
        uint64_t diff = va ^ end;
        unsigned log2 = 0;
        while (diff) {
                log2++;
                diff >>= 1;
        }
        if (log2 > N2HW_LOCK_REGION_MAX_LOG2)
                return false;
        if (log2 < N2HW_LOCK_REGION_MIN_LOG2)
                log2 = N2HW_LOCK_REGION_MIN_LOG2;
        *lockaddr = (va & ~((1ull << log2) - 1)) | (log2 - 1);
        return true;
}

static enum n2hw_error as_wait_ready(struct n2hw *hw, unsigned as)
{
        uint32_t last = 0;
        if (!wait32(hw, N2HW_AS_REG(as, N2HW_AS_STATUS),
                    N2HW_AS_STATUS_ACTIVE, 0, N2HW_AS_TIMEOUT_US, &last))
                return fail(hw, N2HW_E_AS_TIMEOUT,
                            N2HW_AS_REG(as, N2HW_AS_STATUS), last,
                            "Nord2 GPU: AS_ACTIVE stuck");
        return N2HW_OK;
}

static enum n2hw_error as_cmd(struct n2hw *hw, unsigned as, uint32_t cmd)
{
        /* write_cmd(): wait until the MMU accepts another command. */
        enum n2hw_error e = as_wait_ready(hw, as);
        if (e == N2HW_OK)
                wr(hw, N2HW_AS_REG(as, N2HW_AS_COMMAND), cmd);
        return e;
}

static bool as_valid(const struct n2hw *hw, unsigned as)
{
        return as < 16 && (hw->id.as_present & (1u << as));
}

static enum n2hw_error as_program(struct n2hw *hw, unsigned as,
                                  uint64_t transtab, uint64_t transcfg)
{
        wr64(hw, N2HW_AS_REG(as, N2HW_AS_TRANSCFG_LO), transcfg);
        wr64(hw, N2HW_AS_REG(as, N2HW_AS_TRANSTAB_LO), transtab);
        wr64(hw, N2HW_AS_REG(as, N2HW_AS_MEMATTR_LO), N2HW_AS_MEMATTR_VALUE);
        enum n2hw_error e = as_cmd(hw, as, N2HW_AS_CMD_UPDATE);
        /* kbase does not wait after UPDATE; the next command waits.  Wait
         * here as well so the caller observes completion directly. */
        return e == N2HW_OK ? as_wait_ready(hw, as) : e;
}

enum n2hw_error n2hw_as_configure(struct n2hw *hw, unsigned as,
                                  uint64_t transtab)
{
        if (!hw || !hw->identified)
                return N2HW_E_NOT_IDENTIFIED;
        if (!as_valid(hw, as) || !transtab ||
            (transtab & ~N2HW_AS_TRANSTAB_BASE_MASK) || (transtab & 0xfffu))
                return N2HW_E_ARGS;
        /* kbase_mmu_hw_configure(): AArch64 4K, write-back PTW, read
         * allocate; COHERENCY_NONE leaves PTW shareability clear. */
        uint64_t transcfg = N2HW_AS_TRANSCFG_ADRMODE_AARCH64_4K |
                            N2HW_AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK |
                            N2HW_AS_TRANSCFG_R_ALLOCATE;
        return as_program(hw, as, transtab, transcfg);
}

enum n2hw_error n2hw_as_flush(struct n2hw *hw, unsigned as, uint64_t va,
                              uint64_t size, bool flush_mem)
{
        if (!hw || !hw->identified)
                return N2HW_E_NOT_IDENTIFIED;
        uint64_t lockaddr = 0;
        if (!as_valid(hw, as))
                return N2HW_E_ARGS;
        if (!n2hw_lock_region(va, size, &lockaddr))
                return fail(hw, N2HW_E_AS_RANGE, 0, 0,
                            "Nord2 GPU: invalid AS lock region");
        /* kbase_mmu_hw_do_operation(): LOCKADDR, LOCK, op, wait. */
        wr64(hw, N2HW_AS_REG(as, N2HW_AS_LOCKADDR_LO), lockaddr);
        enum n2hw_error e = as_cmd(hw, as, N2HW_AS_CMD_LOCK);
        if (e == N2HW_OK)
                e = as_cmd(hw, as, flush_mem ? N2HW_AS_CMD_FLUSH_MEM
                                             : N2HW_AS_CMD_FLUSH_PT);
        if (e == N2HW_OK)
                e = as_wait_ready(hw, as);
        return e;
}

enum n2hw_error n2hw_as_disable(struct n2hw *hw, unsigned as)
{
        if (!hw || !hw->identified)
                return N2HW_E_NOT_IDENTIFIED;
        if (!as_valid(hw, as))
                return N2HW_E_ARGS;
        /* kbase_mmu_disable(): drain the whole AS, then UNMAPPED. */
        enum n2hw_error e = n2hw_as_flush(hw, as, 0, 1ull << 44, true);
        if (e != N2HW_OK)
                return e;
        return as_program(hw, as, 0, N2HW_AS_TRANSCFG_ADRMODE_UNMAPPED);
}

enum n2hw_error n2hw_js_start(struct n2hw *hw, unsigned slot, unsigned as,
                              uint64_t head, uint64_t affinity)
{
        if (!hw || !hw->identified || !hw->slots_valid)
                return N2HW_E_NOT_IDENTIFIED;
        if (slot >= 16 || !(hw->id.js_present & (1u << slot)) ||
            !as_valid(hw, as) || !head || (head & 0x3fu) ||
            head >= (1ull << 48) || !affinity ||
            (affinity & ~hw->shaders_on))
                return N2HW_E_ARGS;
        if (hw->has_issue_ttrx_3485 && !hw->wa_complete)
                return N2HW_E_REVISION_WA;
        /* kbasep_jm_is_js_free(): COMMAND_NEXT reads zero when free. */
        uint32_t next = rd(hw, N2HW_JS_REG(slot, N2HW_JS_COMMAND_NEXT));
        if (next)
                return fail(hw, N2HW_E_SLOT_BUSY,
                            N2HW_JS_REG(slot, N2HW_JS_COMMAND_NEXT), next,
                            "Nord2 GPU: job slot NEXT busy");
        /* kbase_job_hw_submit(): no flush reduction (no flush ID tracking),
         * clean+invalidate on start, CLEAN_ONLY_SAFE clean at the end,
         * medium priority, JOBCHAIN_DISAMBIGUATION alternating flag. */
        uint32_t cfg = as | N2HW_JS_CFG_START_FLUSH_CLEAN_INVALIDATE |
                       N2HW_JS_CFG_END_FLUSH_CLEAN | N2HW_JS_CFG_THREAD_PRI(8);
        if (!hw->job_chain_flag[slot])
                cfg |= N2HW_JS_CFG_JOB_CHAIN_FLAG;
        hw->job_chain_flag[slot] = !hw->job_chain_flag[slot];
        wr64(hw, N2HW_JS_REG(slot, N2HW_JS_HEAD_NEXT_LO), head);
        wr64(hw, N2HW_JS_REG(slot, N2HW_JS_AFFINITY_NEXT_LO), affinity);
        wr(hw, N2HW_JS_REG(slot, N2HW_JS_CONFIG_NEXT), cfg);
        wr(hw, N2HW_JS_REG(slot, N2HW_JS_COMMAND_NEXT), N2HW_JS_CMD_START);
        return N2HW_OK;
}

void n2hw_js_stop(struct n2hw *hw, unsigned slot, bool hard)
{
        if (!hw || slot >= 16)
                return;
        /* kbasep_job_slot_soft_or_hard_stop_do_action: JS_COMMAND. */
        wr(hw, N2HW_JS_REG(slot, N2HW_JS_COMMAND),
           hard ? N2HW_JS_CMD_HARD_STOP : N2HW_JS_CMD_SOFT_STOP);
}

bool n2hw_js_idle(struct n2hw *hw, unsigned slot)
{
        if (!hw || slot >= 16)
                return false;
        /* JOB_IRQ_JS_STATE: bit n current active, bit 16+n next busy. */
        uint32_t state = rd(hw, N2HW_JOB_IRQ_JS_STATE);
        return !(state & ((1u << slot) | (1u << (slot + 16)))) &&
               !rd(hw, N2HW_JS_REG(slot, N2HW_JS_COMMAND_NEXT));
}

unsigned n2hw_job_irq(struct n2hw *hw, struct n2hw_job_event *events,
                      unsigned max_events)
{
        if (!hw || !events || !max_events)
                return 0;
        uint32_t raw = rd(hw, N2HW_JOB_IRQ_RAWSTAT);
        unsigned n = 0;
        for (unsigned slot = 0; slot < 16 && n < max_events; slot++) {
                uint32_t done = 1u << slot, failed = 1u << (slot + 16);
                if (!(raw & (done | failed)))
                        continue;
                struct n2hw_job_event *ev = &events[n++];
                ev->slot = slot;
                ev->failed = (raw & failed) != 0;
                ev->status = ev->failed
                        ? rd(hw, N2HW_JS_REG(slot, N2HW_JS_STATUS)) : 0;
                /* A failure IRQ with NOT_STARTED/zero status is not DONE:
                 * kbase maps it to TERMINATED. */
                if (ev->failed && ev->status == 0)
                        ev->status = 0x04;
                ev->head = rd64(hw, N2HW_JS_REG(slot, N2HW_JS_HEAD_LO));
                /* kbase_job_done clears exactly the bits it consumed. */
                wr(hw, N2HW_JOB_IRQ_CLEAR, raw & (done | failed));
        }
        return n;
}

unsigned n2hw_mmu_irq(struct n2hw *hw, struct n2hw_mmu_fault *faults,
                      unsigned max_faults)
{
        if (!hw || !faults || !max_faults)
                return 0;
        uint32_t raw = rd(hw, N2HW_MMU_IRQ_RAWSTAT);
        unsigned n = 0;
        for (unsigned as = 0; as < 16 && n < max_faults; as++) {
                uint32_t pf = 1u << as, bf = 1u << (as + 16);
                if (!(raw & (pf | bf)))
                        continue;
                struct n2hw_mmu_fault *f = &faults[n++];
                f->as = as;
                f->bus_error = (raw & bf) != 0;
                f->status = rd(hw, N2HW_AS_REG(as, N2HW_AS_FAULTSTATUS));
                f->address = rd64(hw, N2HW_AS_REG(as, N2HW_AS_FAULTADDRESS_LO));
                /* kbase_mmu_hw_clear_fault(): page and bus bits for this AS. */
                wr(hw, N2HW_MMU_IRQ_CLEAR, raw & (pf | bf));
        }
        return n;
}

const char *n2hw_error_string(enum n2hw_error e)
{
        switch (e) {
        case N2HW_OK: return "ok";
        case N2HW_E_ARGS: return "invalid arguments";
        case N2HW_E_NOT_IDENTIFIED: return "GPU not identified";
        case N2HW_E_PRODUCT: return "not Mali-G77 tTRx";
        case N2HW_E_REVISION_WA: return "revision needs TTRX_3485 workaround";
        case N2HW_E_PRESENT: return "implausible presence masks";
        case N2HW_E_SLOTS: return "job slot features mismatch";
        case N2HW_E_RESET_TIMEOUT: return "reset timeout";
        case N2HW_E_PROTECTED: return "protected mode after reset";
        case N2HW_E_L2_TIMEOUT: return "L2/tiler power timeout";
        case N2HW_E_SHADER_TIMEOUT: return "shader power timeout";
        case N2HW_E_CACHE_TIMEOUT: return "cache clean timeout";
        case N2HW_E_AS_TIMEOUT: return "AS command timeout";
        case N2HW_E_AS_RANGE: return "AS lock region invalid";
        case N2HW_E_SLOT_BUSY: return "job slot busy";
        case N2HW_E_STOP_TIMEOUT: return "job stop not acknowledged";
        case N2HW_E_POWER_STATE: return "invalid power state";
        }
        return "unknown";
}
