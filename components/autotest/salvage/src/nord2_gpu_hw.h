/*
 * Nord 2 Mali-G77 (tTRx) GPU-internal hardware protocol.
 *
 * Transcribed from the vendor kbase r32p1 JM backend selected by the Nord 2
 * Android kernel (04d00c2565764c3922c9b4826b5acff1106e533f):
 *   backend/gpu/mali_kbase_pm_driver.c   reset, quirks, L2/tiler/shader power
 *   backend/gpu/mali_kbase_jm_hw.c       job slot submit/stop/IRQ
 *   backend/gpu/mali_kbase_l2_mmu_config.c, mali_kbase_cache_policy_backend.c
 *   mmu/mali_kbase_mmu_hw_direct.c       AS LOCK/FLUSH/UPDATE protocol
 *   mmu/backend/mali_kbase_mmu_jm.c      AS memattr/transcfg setup
 *   mali_kbase_js.c                      slot rule and JSn_FEATURES mapping
 *   mali_base_hwconfig_{features,issues}.h, mali_kbase_hw.c (tTRx tables)
 *
 * This module only issues register accesses through caller callbacks and
 * waits with explicit bounds.  It never assumes a READY bit, reset or AS
 * acknowledgement: every success is an observed register state.  It contains
 * no rail, clock or MTCMOS control; the board layer must have powered and
 * clocked the MFG domains before calling anything here.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_HW_H
#define NORD2_GPU_HW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GPU control (r32p1 mali_kbase_gpu_regmap.h / _jm.h). */
enum {
        N2HW_GPU_ID = 0x000,
        N2HW_L2_FEATURES = 0x004,
        N2HW_CORE_FEATURES = 0x008,
        N2HW_TILER_FEATURES = 0x00c,
        N2HW_MEM_FEATURES = 0x010,
        N2HW_MMU_FEATURES = 0x014,
        N2HW_AS_PRESENT = 0x018,
        N2HW_JS_PRESENT = 0x01c,
        N2HW_GPU_IRQ_RAWSTAT = 0x020,
        N2HW_GPU_IRQ_CLEAR = 0x024,
        N2HW_GPU_IRQ_MASK = 0x028,
        N2HW_GPU_IRQ_STATUS = 0x02c,
        N2HW_GPU_COMMAND = 0x030,
        N2HW_GPU_STATUS = 0x034,
        N2HW_LATEST_FLUSH = 0x038,
        N2HW_GPU_FAULTSTATUS = 0x03c,
        N2HW_GPU_FAULTADDRESS_LO = 0x040,
        N2HW_GPU_FAULTADDRESS_HI = 0x044,
        N2HW_L2_CONFIG = 0x048,
        N2HW_THREAD_MAX_THREADS = 0x0a0,
        N2HW_THREAD_MAX_WORKGROUP_SIZE = 0x0a4,
        N2HW_THREAD_MAX_BARRIER_SIZE = 0x0a8,
        N2HW_THREAD_FEATURES = 0x0ac,
        N2HW_TEXTURE_FEATURES_0 = 0x0b0,
        N2HW_JS0_FEATURES = 0x0c0,
        N2HW_SHADER_PRESENT_LO = 0x100,
        N2HW_SHADER_PRESENT_HI = 0x104,
        N2HW_TILER_PRESENT_LO = 0x110,
        N2HW_TILER_PRESENT_HI = 0x114,
        N2HW_L2_PRESENT_LO = 0x120,
        N2HW_L2_PRESENT_HI = 0x124,
        N2HW_SHADER_READY_LO = 0x140,
        N2HW_SHADER_READY_HI = 0x144,
        N2HW_TILER_READY_LO = 0x150,
        N2HW_TILER_READY_HI = 0x154,
        N2HW_L2_READY_LO = 0x160,
        N2HW_L2_READY_HI = 0x164,
        N2HW_SHADER_PWRON_LO = 0x180,
        N2HW_SHADER_PWRON_HI = 0x184,
        N2HW_TILER_PWRON_LO = 0x190,
        N2HW_TILER_PWRON_HI = 0x194,
        N2HW_L2_PWRON_LO = 0x1a0,
        N2HW_L2_PWRON_HI = 0x1a4,
        N2HW_SHADER_PWROFF_LO = 0x1c0,
        N2HW_SHADER_PWROFF_HI = 0x1c4,
        N2HW_TILER_PWROFF_LO = 0x1d0,
        N2HW_TILER_PWROFF_HI = 0x1d4,
        N2HW_L2_PWROFF_LO = 0x1e0,
        N2HW_L2_PWROFF_HI = 0x1e4,
        N2HW_SHADER_PWRTRANS_LO = 0x200,
        N2HW_SHADER_PWRTRANS_HI = 0x204,
        N2HW_TILER_PWRTRANS_LO = 0x210,
        N2HW_TILER_PWRTRANS_HI = 0x214,
        N2HW_L2_PWRTRANS_LO = 0x220,
        N2HW_L2_PWRTRANS_HI = 0x224,
        N2HW_COHERENCY_FEATURES = 0x300,
        N2HW_COHERENCY_ENABLE = 0x304,
        N2HW_JM_CONFIG = 0xf00,
        N2HW_SHADER_CONFIG = 0xf04,
        N2HW_TILER_CONFIG = 0xf08,
        N2HW_L2_MMU_CONFIG = 0xf0c,

        N2HW_JOB_IRQ_RAWSTAT = 0x1000,
        N2HW_JOB_IRQ_CLEAR = 0x1004,
        N2HW_JOB_IRQ_MASK = 0x1008,
        N2HW_JOB_IRQ_STATUS = 0x100c,
        N2HW_JOB_IRQ_JS_STATE = 0x1010,

        N2HW_MMU_IRQ_RAWSTAT = 0x2000,
        N2HW_MMU_IRQ_CLEAR = 0x2004,
        N2HW_MMU_IRQ_MASK = 0x2008,
        N2HW_MMU_IRQ_STATUS = 0x200c,
};

#define N2HW_JS_REG(slot, reg) (0x1800u + (uint32_t)(slot) * 0x80u + (reg))
enum {
        N2HW_JS_HEAD_LO = 0x00,
        N2HW_JS_HEAD_HI = 0x04,
        N2HW_JS_TAIL_LO = 0x08,
        N2HW_JS_TAIL_HI = 0x0c,
        N2HW_JS_AFFINITY_LO = 0x10,
        N2HW_JS_AFFINITY_HI = 0x14,
        N2HW_JS_CONFIG = 0x18,
        N2HW_JS_COMMAND = 0x20,
        N2HW_JS_STATUS = 0x24,
        N2HW_JS_HEAD_NEXT_LO = 0x40,
        N2HW_JS_HEAD_NEXT_HI = 0x44,
        N2HW_JS_AFFINITY_NEXT_LO = 0x50,
        N2HW_JS_AFFINITY_NEXT_HI = 0x54,
        N2HW_JS_CONFIG_NEXT = 0x58,
        N2HW_JS_COMMAND_NEXT = 0x60,
        N2HW_JS_FLUSH_ID_NEXT = 0x70,
};

#define N2HW_AS_REG(as, reg) (0x2400u + (uint32_t)(as) * 0x40u + (reg))
enum {
        N2HW_AS_TRANSTAB_LO = 0x00,
        N2HW_AS_TRANSTAB_HI = 0x04,
        N2HW_AS_MEMATTR_LO = 0x08,
        N2HW_AS_MEMATTR_HI = 0x0c,
        N2HW_AS_LOCKADDR_LO = 0x10,
        N2HW_AS_LOCKADDR_HI = 0x14,
        N2HW_AS_COMMAND = 0x18,
        N2HW_AS_FAULTSTATUS = 0x1c,
        N2HW_AS_FAULTADDRESS_LO = 0x20,
        N2HW_AS_FAULTADDRESS_HI = 0x24,
        N2HW_AS_STATUS = 0x28,
        N2HW_AS_TRANSCFG_LO = 0x30,
        N2HW_AS_TRANSCFG_HI = 0x34,
        N2HW_AS_FAULTEXTRA_LO = 0x38,
        N2HW_AS_FAULTEXTRA_HI = 0x3c,
};

/* GPU_COMMAND and GPU_IRQ bits. */
#define N2HW_CMD_SOFT_RESET 0x01u
#define N2HW_CMD_HARD_RESET 0x02u
#define N2HW_CMD_CLEAN_CACHES 0x07u
#define N2HW_CMD_CLEAN_INV_CACHES 0x08u
#define N2HW_IRQ_GPU_FAULT (1u << 0)
#define N2HW_IRQ_MULTIPLE_GPU_FAULTS (1u << 7)
#define N2HW_IRQ_RESET_COMPLETED (1u << 8)
#define N2HW_IRQ_POWER_CHANGED_SINGLE (1u << 9)
#define N2HW_IRQ_POWER_CHANGED_ALL (1u << 10)
#define N2HW_IRQ_CLEAN_CACHES_COMPLETED (1u << 17)
#define N2HW_GPU_STATUS_PROTECTED (1u << 7)

/* JS_COMMAND / JS_CONFIG. */
#define N2HW_JS_CMD_START 0x01u
#define N2HW_JS_CMD_SOFT_STOP 0x02u
#define N2HW_JS_CMD_HARD_STOP 0x03u
#define N2HW_JS_CMD_SOFT_STOP_0 0x04u
#define N2HW_JS_CMD_HARD_STOP_0 0x05u
#define N2HW_JS_CMD_SOFT_STOP_1 0x06u
#define N2HW_JS_CMD_HARD_STOP_1 0x07u
#define N2HW_JS_CFG_START_FLUSH_CLEAN_INVALIDATE (3u << 8)
#define N2HW_JS_CFG_JOB_CHAIN_FLAG (1u << 11)
#define N2HW_JS_CFG_END_FLUSH_CLEAN (1u << 12)
#define N2HW_JS_CFG_END_FLUSH_CLEAN_INVALIDATE (3u << 12)
#define N2HW_JS_CFG_THREAD_PRI(n) ((uint32_t)(n) << 16)

/* JSn_FEATURES. */
#define N2HW_JS_FEATURE_NULL_JOB (1u << 1)
#define N2HW_JS_FEATURE_SET_VALUE_JOB (1u << 2)
#define N2HW_JS_FEATURE_CACHE_FLUSH_JOB (1u << 3)
#define N2HW_JS_FEATURE_COMPUTE_JOB (1u << 4)
#define N2HW_JS_FEATURE_VERTEX_JOB (1u << 5)
#define N2HW_JS_FEATURE_GEOMETRY_JOB (1u << 6)
#define N2HW_JS_FEATURE_TILER_JOB (1u << 7)
#define N2HW_JS_FEATURE_FUSED_JOB (1u << 8)
#define N2HW_JS_FEATURE_FRAGMENT_JOB (1u << 9)

/* AS_COMMAND / AS_STATUS / AS_TRANSCFG / AS_MEMATTR. */
#define N2HW_AS_CMD_NOP 0x00u
#define N2HW_AS_CMD_UPDATE 0x01u
#define N2HW_AS_CMD_LOCK 0x02u
#define N2HW_AS_CMD_UNLOCK 0x03u
#define N2HW_AS_CMD_FLUSH_PT 0x04u
#define N2HW_AS_CMD_FLUSH_MEM 0x05u
#define N2HW_AS_STATUS_ACTIVE 0x01u
#define N2HW_AS_TRANSCFG_ADRMODE_UNMAPPED 1u
#define N2HW_AS_TRANSCFG_ADRMODE_AARCH64_4K 6u
#define N2HW_AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK (2ull << 24)
#define N2HW_AS_TRANSCFG_R_ALLOCATE (1ull << 30)
#define N2HW_AS_TRANSTAB_BASE_MASK ((1ull << 40) - (1ull << 4))
/* kbase_mmu_get_as_setup(): indices 0..5 = IMPL_DEF, FORCE_CACHE, WRITE_ALLOC,
 * OUTER_IMPL_DEF, OUTER_WA, NON_CACHEABLE (AArch64 values). */
#define N2HW_AS_MEMATTR_VALUE                                              \
        (0x88ull | (0x8full << 8) | (0x8dull << 16) | (0x88ull << 24) |    \
         (0x8dull << 32) | (0x4cull << 40))
#define N2HW_LOCK_REGION_MIN_LOG2 15u
#define N2HW_LOCK_REGION_MAX_LOG2 48u

/* Quirk bits (mali_kbase_gpu_regmap.h). */
#define N2HW_SC_VAR_ALGORITHM (1u << 29)
#define N2HW_IDVS_GROUP_SIZE_SHIFT 16u
#define N2HW_IDVS_GROUP_SIZE_DEFAULT 0xfu
#define N2HW_L2_MMU_TTRX_READ_LIMIT_MASK (0x3fu << 7)   /* GENMASK(12,7) */
#define N2HW_L2_MMU_TTRX_WRITE_LIMIT_MASK (0x1fu << 13) /* GENMASK(17,13) */
#define N2HW_COHERENCY_NONE 31u

/* MT6893 vendor shader presence: MT_GPU_SHADER_PRESENT_9 in
 * gpufreq/v1/include/mt6893/mtk_gpufreq_plat.h (stacks 0,1,2,5,6). */
#define N2HW_MT6893_SHADER_PRESENT_9 0x00660067ull

/* Bounds.  RESET_TIMEOUT is 500 ms in mali_kbase_defs.h. */
#define N2HW_RESET_TIMEOUT_US 500000u
#define N2HW_POWER_TIMEOUT_US 100000u
#define N2HW_CACHE_TIMEOUT_US 100000u
#define N2HW_AS_TIMEOUT_US 100000u
#define N2HW_POLL_US 10u

enum n2hw_error {
        N2HW_OK = 0,
        N2HW_E_ARGS,
        N2HW_E_NOT_IDENTIFIED,
        N2HW_E_PRODUCT,           /* not a tTRx (Mali-G77) arch 9.0 */
        N2HW_E_REVISION_WA,       /* r0p0/r0p1 need the TTRX_3485 dummy job blob */
        N2HW_E_PRESENT,           /* core/L2/tiler presence implausible */
        N2HW_E_SLOTS,             /* JSn_FEATURES do not satisfy the vendor rule */
        N2HW_E_RESET_TIMEOUT,     /* neither soft nor hard reset completed */
        N2HW_E_PROTECTED,         /* protected mode still active after reset */
        N2HW_E_L2_TIMEOUT,
        N2HW_E_SHADER_TIMEOUT,
        N2HW_E_CACHE_TIMEOUT,
        N2HW_E_AS_TIMEOUT,
        N2HW_E_AS_RANGE,
        N2HW_E_SLOT_BUSY,
        N2HW_E_STOP_TIMEOUT,
        N2HW_E_POWER_STATE,
};

struct n2hw_ops {
        uint32_t (*read)(void *ctx, uint32_t off);
        void (*write)(void *ctx, uint32_t off, uint32_t value);
        void (*udelay)(void *ctx, unsigned us);
        void (*log)(void *ctx, const char *msg);
};

struct n2hw_identity {
        uint32_t gpu_id;
        uint32_t version_major, version_minor, version_status;
        uint32_t l2_features, core_features, tiler_features, mem_features;
        uint32_t mmu_features, as_present, js_present;
        uint32_t thread_max_threads, thread_max_workgroup_size;
        uint32_t thread_features, coherency_features;
        uint32_t texture_features[4];
        uint32_t js_features[16];
        uint64_t shader_present, tiler_present, l2_present;
};

struct n2hw {
        struct n2hw_ops ops;
        void *ctx;
        struct n2hw_identity id;
        bool identified;
        bool has_issue_ttrx_3485;     /* mandatory firmware before normal jobs */
        bool wa_complete;
        uint32_t wa_runs;
        bool has_issue_ttrx_2968;     /* SC_VAR_ALGORITHM */
        bool has_issue_ttrx_921;      /* flush L2 before shader power-off */
        bool has_issue_ttrx_3076;     /* reset after a job bus fault */
        bool has_issue_gpu2017_1336;  /* L2 stays powered while GPU is on */
        unsigned slot_fragment;
        unsigned slot_vertex_tiler;
        unsigned slot_compute;
        bool slots_valid;
        bool job_chain_flag[16];      /* JOBCHAIN_DISAMBIGUATION toggle */
        bool l2_on;
        uint64_t shaders_on;
        uint32_t quirk_shader_config, quirk_tiler_config;
        uint32_t quirk_l2_mmu_config, quirk_jm_config;
        uint32_t last_error;
        uint32_t last_offset, last_value;  /* register that failed a wait */
        uint32_t resets_soft, resets_hard;
};

struct n2hw_job_event {
        unsigned slot;
        bool failed;
        uint32_t status;    /* JS_STATUS on failure, 0 on success */
        uint64_t head;      /* JS_HEAD at the time of the IRQ */
};

struct n2hw_mmu_fault {
        unsigned as;
        bool bus_error;
        uint32_t status;
        uint64_t address;
};

bool n2hw_init(struct n2hw *hw, const struct n2hw_ops *ops, void *ctx);

/* Read-only identification; r0p0/r0p1 require WA before shader use.  Requires the
 * MFG domains, BG3D gate and reset already completed by the caller. */
enum n2hw_error n2hw_identify(struct n2hw *hw);
/* Vendor slot rule: fragment -> JS0, vertex/tiler/compute -> JS1, each
 * verified against JSn_FEATURES (kbase core_reqs_from_jsn_features). */
enum n2hw_error n2hw_select_slots(struct n2hw *hw);

/* kbase_pm_do_reset(): soft reset, hard reset fallback, bounded. */
enum n2hw_error n2hw_reset(struct n2hw *hw);
/* kbase_pm_hw_issues_detect/apply + coherency mode. */
enum n2hw_error n2hw_apply_quirks(struct n2hw *hw);
/* L2 alone is needed to install the r0p1 workaround MMU before shaders. */
enum n2hw_error n2hw_power_on_l2(struct n2hw *hw);
/* Exact flags=6 / JS0 / 4GiB chain from vendor valhall-1691526.wa.
 * AS0 must already map the immutable payload; failures keep memory pinned. */
enum n2hw_error n2hw_dummy_job_wa(struct n2hw *hw);
/* L2 via TILER_PWRON (+ extra L2 slices), then shader cores. */
enum n2hw_error n2hw_power_on_cores(struct n2hw *hw, uint64_t shader_mask);
/* TTRX_921 flush, shader off, final flush (GPU2017_1336 keeps L2 on). */
enum n2hw_error n2hw_power_off_cores(struct n2hw *hw);
enum n2hw_error n2hw_cache_clean_inv(struct n2hw *hw);

/* AS protocol (mali_kbase_mmu_hw_direct.c). */
bool n2hw_lock_region(uint64_t va, uint64_t size, uint64_t *lockaddr);
enum n2hw_error n2hw_as_configure(struct n2hw *hw, unsigned as,
                                  uint64_t transtab);
enum n2hw_error n2hw_as_flush(struct n2hw *hw, unsigned as, uint64_t va,
                              uint64_t size, bool flush_mem);
enum n2hw_error n2hw_as_disable(struct n2hw *hw, unsigned as);

/* Job slots.  Single outstanding chain per slot: HEAD_NEXT must be free. */
enum n2hw_error n2hw_js_start(struct n2hw *hw, unsigned slot, unsigned as,
                              uint64_t head, uint64_t affinity);
void n2hw_js_stop(struct n2hw *hw, unsigned slot, bool hard);
bool n2hw_js_idle(struct n2hw *hw, unsigned slot);
/* Collect and clear job IRQ events; returns the number written. */
unsigned n2hw_job_irq(struct n2hw *hw, struct n2hw_job_event *events,
                      unsigned max_events);
unsigned n2hw_mmu_irq(struct n2hw *hw, struct n2hw_mmu_fault *faults,
                      unsigned max_faults);

const char *n2hw_error_string(enum n2hw_error e);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_HW_H */
