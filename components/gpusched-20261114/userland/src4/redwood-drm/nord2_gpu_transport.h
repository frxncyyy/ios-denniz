/* Nord2 private transport ABI and bounds checks, shared with host tests.
 * SPDX-License-Identifier: MIT */
#ifndef NORD2_GPU_TRANSPORT_H
#define NORD2_GPU_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define NORD2_GPU_SEL_STATUS 0
#define NORD2_GPU_SEL_ALLOC 1   /* scalars {bytes, flags}: flags bit0 = executable, bit1 = GPU-only (kernel refuses any CPU mapping;
                                 * such a buffer needs no cache maintenance after its first use: heap, TLS, GPU-private targets) */
#define NORD2_GPU_SEL_RELEASE 2
#define NORD2_GPU_SEL_SUBMIT_COMPUTE 3
/* A WAIT reporting DONE consumes that job ID; running/fault results do not. */
#define NORD2_GPU_SEL_WAIT 4
#define NORD2_GPU_SEL_FAULT 5
#define NORD2_GPU_SEL_PROBE 6
#define NORD2_GPU_SEL_RESET 7
#define NORD2_GPU_SEL_IMPORT_SURFACE 8
#define NORD2_GPU_SEL_SUBMIT_JM 9
#define NORD2_GPU_SEL_SYNC 10
#define NORD2_GPU_SEL_LOG 11 /* bounded diagnostic text; no GPU work */
/* Asynchronous chains: SUBMIT_JM returns the job id (a per-device monotonic
 * sequence number) without waiting; jobs of one context run strictly in
 * submission order.  WAIT_SEQ(seq, timeout_ms) waits until every job of the
 * calling context with id <= seq is terminal, then reaps them all:
 *   scalarOutput[0] = 4 (all DONE) | 5 FAULTED | 6 CANCELLED | 7 RESET (state of the lowest failing job)
 *                     | 2 RUNNING (timeout, nothing reaped)
 *   scalarOutput[1] = number of terminal jobs <= seq   scalarOutput[2] = lowest failing id (0 none)
 * and, if requested, a nord2_gpu_fault_info for that failing job.  At most
 * NORD2_GPU_MAX_INFLIGHT unfinished jobs per client; SUBMIT_JM returns kIOReturnBusy
 * beyond that (wait, then resubmit). */
#define NORD2_GPU_SEL_WAIT_SEQ 12
/* 0112 display-surface reservation.  RESERVE(n, handles[n] as structure input of n uint32 imported-surface buffer handles) -> id (never 0 when n > 0):
 * until retired, cancelled, expired (200 ms) or its client closes, the framebuffer treats those surfaces as busy although no job writing them has been
 * submitted yet.  n == 0 answers id 0 and is the availability probe; n > 8: BadArgument; more than 4 live reservations in the context: Busy.
 * RETIRE(id, seq): seq != 0 keeps it busy until every job of this context with id <= seq is terminal; seq == 0 cancels; id 0 is a no-op;
 * unknown id: kIOReturnNotFound.  Neither call blocks. */
#define NORD2_GPU_SEL_SURFACE_RESERVE 13
#define NORD2_GPU_SEL_SURFACE_RETIRE 14
/* 0134: the GPU's identity registers (what a Panfrost-style client asks as GET_PARAM); read-only, no GPU work. */
#define NORD2_GPU_SEL_HWINFO 15
#define NORD2_GPU_RESERVE_MAX 8u
#define NORD2_GPU_MAX_INFLIGHT 96u
#define NORD2_GPU_TRANSPORT_VERSION 2u

/* ABI v2: raw arch9 Job Manager chains in the calling client's address
 * space. kind: 1 compute, 2 vertex/tiler + fragment, 3 fragment only. */
struct nord2_gpu_submit_request {
        uint32_t version, kind;
        uint64_t heads[2];
        uint32_t nr_handles, write_mask;
        uint32_t handles[32];
};

/* 0632 ABI v3 = v2 + flags (bit 1 = precise: touch_mask/write_mask are this chain's real access set) + touch_mask; a kernel without 0632
 * refuses version 3 (kIOReturnBadArgument) and the client falls back to v2. */
#define NORD2_GPU_TRANSPORT_VERSION3 3u
#define NORD2_GPU_SUBMIT_PRECISE (1u << 1)
struct nord2_gpu_submit_request3 {
        uint32_t version, kind;
        uint64_t heads[2];
        uint32_t nr_handles, write_mask;
        uint32_t handles[32];
        uint32_t flags, touch_mask;
        uint32_t reserved[2];
};

/* 0661 ABI v4: up to 128 handles, 128-bit write/touch masks; flags bit 1 = precise, bit 2 = truncated.  A kernel without 0661 refuses it
 * (kIOReturnBadArgument); the client falls back to v3 / v2. */
#define NORD2_GPU_TRANSPORT_VERSION4 4u
#define NORD2_GPU_SUBMIT4_MAX 128u
#define NORD2_GPU_SUBMIT_TRUNCATED (1u << 2)
struct nord2_gpu_submit_request4 {
        uint32_t version, kind;
        uint64_t heads[2];
        uint32_t nr_handles, flags;
        uint32_t write_mask[4], touch_mask[4];
        uint32_t handles[128];
        uint32_t reserved[2];
};

struct nord2_gpu_hwinfo {
        uint32_t version;
        uint32_t thread_max_threads, thread_max_workgroup_size, thread_max_barrier, thread_features, coherency_features;
        uint32_t texture_features[4];
        uint32_t js_features[16];
        uint32_t as_present, afbc_features, tls_alloc;     /* the last two are 0 until the kernel reads them */
        uint32_t reserved[3];
};

struct nord2_gpu_status {
        uint32_t abi_version;
        uint32_t enabled;
        uint32_t powered;
        uint32_t arch_ok;
        uint32_t gpu_id;
        uint32_t l2_features;
        uint32_t core_features;
        uint32_t tiler_features;
        uint32_t mem_features;
        uint32_t mmu_features;
        uint32_t js_present;
        uint32_t shader_present_lo;
        uint32_t shader_present_hi;
        uint32_t tiler_present_lo;
        uint32_t tiler_present_hi;
        uint32_t jobs_completed;
        uint32_t jobs_faulted;
        uint32_t jobs_timed_out;
        uint32_t buffers_live;
        uint32_t quarantine;
        uint32_t mfg_pwr_status;
        uint32_t mfg_pwr_status_2nd;
};

struct nord2_gpu_compute_request {
        uint32_t version;
        uint32_t flags;
        uint64_t shader_handle;
        uint64_t resource_handle;
        uint64_t tls_handle;
        uint64_t fau_handle;
        uint32_t shader_offset;
        uint32_t resource_offset;
        uint32_t tls_offset;
        uint32_t fau_offset;
        uint32_t workgroup_size[3];
        uint32_t workgroup_count[3];
        uint32_t fau_count;
        uint32_t attribute_offset;
        uint32_t allow_merging;
        uint32_t reserved;
};

struct nord2_gpu_fault_info {
        uint32_t job_id_lo;
        uint32_t job_id_hi;
        uint32_t state;
        uint32_t status;
        uint32_t fault_status;
        uint32_t first_incomplete_task;
        uint64_t fault_address;
};

#if defined(__cplusplus)
static_assert(sizeof(struct nord2_gpu_compute_request) == 96, "compute ABI");
static_assert(sizeof(struct nord2_gpu_fault_info) == 32, "fault ABI");
#else
_Static_assert(sizeof(struct nord2_gpu_compute_request) == 96, "compute ABI");
_Static_assert(sizeof(struct nord2_gpu_fault_info) == 32, "fault ABI");
#endif


/* Private (driver-allocated) backing is capped per client.  Pages imported
 * from an IOSurface are owned and already wired by IOSurface; they are charged
 * to a separate import budget, never to the private cap.  Per-client hard
 * context limit = private ceiling + import limit.
 *
 * The kernel only manages ~992 MiB of RAM on the Nord 2 window, so the default
 * private cap (160 MiB) is deliberately below the boot-arg ceiling (256 MiB),
 * and a device-wide private budget bounds the sum over all clients.  Both
 * defaults can be changed without a rebuild: boot-args nord2_gpu_priv_mb
 * (per client, 16..256) and nord2_gpu_glob_mb (all clients, 16..512). */
#define NORD2_GPU_PRIVATE_LIMIT  (160ull << 20)   /* default, per client */
#define NORD2_GPU_PRIVATE_MAX    (256ull << 20)   /* boot-arg ceiling */
#define NORD2_GPU_GLOBAL_PRIVATE_LIMIT (256ull << 20) /* default, all clients */
#define NORD2_GPU_GLOBAL_PRIVATE_MAX   (512ull << 20)
#define NORD2_GPU_IMPORT_LIMIT   (128ull << 20)
#define NORD2_GPU_CONTEXT_LIMIT  (NORD2_GPU_PRIVATE_MAX + NORD2_GPU_IMPORT_LIMIT) /* 384 MiB */

/* All backing is accounted in XNU's 16 KiB allocation units.  `wired` is the
 * context's PRIVATE usage only (total wired minus imported). */
static inline bool nord2_gpu_allocation_size_limit(uint64_t requested,
                                                   uint64_t wired,
                                                   uint64_t limit,
                                                   uint64_t *size)
{
        if (!requested || requested > limit || wired > limit || !size)
                return false;
        uint64_t rounded = (requested + 16383u) & ~16383ull;
        if (rounded > limit - wired)
                return false;
        *size = rounded;
        return true;
}

static inline bool nord2_gpu_allocation_size(uint64_t requested,
                                             uint64_t wired, uint64_t *size)
{
        return nord2_gpu_allocation_size_limit(requested, wired,
                        NORD2_GPU_PRIVATE_LIMIT, size);
}

/* Completion polling (no GPU interrupt is wired up): busy-poll with short delays
 * while a job is young, then fall back to millisecond sleeps.  Returns the delay
 * in microseconds, or 0 for "sleep 1 ms".  A 1 ms IOSleep costs 1-2.5 ms of
 * completion latency per job on this kernel, which dominated small jobs. */
static inline unsigned nord2_gpu_wait_step_us(uint64_t elapsed_us)
{
        if (elapsed_us < 1500u)
                return 20u;
        if (elapsed_us < 6000u)
                return 100u;
        return 0u;
}

/* The kernel command line is capped at 607 characters and the UI image already uses 600, so the two most used
 * experiment switches have short aliases: n2lvts=<n> == nord2_lvts=<n>, n2mhz=<MHz> == nord2_gpu_khz=<MHz*1000>.
 * The full names win when both are present.  Returns false (value unused) for an MHz outside 100..1000. */
static inline bool nord2_gpu_mhz_alias_to_khz(int mhz, uint32_t *khz)
{
        if (mhz < 100 || mhz > 1000 || !khz)
                return false;
        *khz = (uint32_t)mhz * 1000u;
        return true;
}

/* P8 cold-start OPP policy.  The UI (nord2_metal=1) starts at 654 MHz (0.65 V; a 654 MHz boot ran on the device, c6k); images without the UI
 * keep the 350 MHz minimum.  n2mhz=<MHz> / nord2_gpu_khz=<kHz> override either way (n2mhz=350 is the old UI default, n2mhz=886 is opt-in).
 * Both are cold-start only: there is no live OPP switch and no GPU temperature (LVTS level 1: valid_sensors=0), so the only guard is the
 * battery gauge reading at power-on, which is slow and lags the SoC: a hot start never gets a high OPP.  An unreadable gauge never blocks. */
#define NORD2_GPU_DEFAULT_UI_KHZ 654000u
#define NORD2_GPU_BAT_LIMIT_MID_CC  4000    /* 40.0 C: refuse 351..654 MHz at or above */
#define NORD2_GPU_BAT_LIMIT_HIGH_CC 3800    /* 38.0 C: refuse > 654 MHz at or above */
static inline uint32_t nord2_gpu_default_opp_khz(bool metal_ui)
{
        return metal_ui ? NORD2_GPU_DEFAULT_UI_KHZ : 0u;      /* 0 = keep the minimum */
}
static inline bool nord2_gpu_opp_battery_allows(uint32_t khz, bool gauge_valid, int32_t bat_cC)
{
        if (khz <= 350000u || !gauge_valid)
                return true;
        return bat_cC < (khz > NORD2_GPU_DEFAULT_UI_KHZ ? NORD2_GPU_BAT_LIMIT_HIGH_CC : NORD2_GPU_BAT_LIMIT_MID_CC);
}

/* Clamp a boot-arg value given in MiB; 0/absent keeps the default. */
static inline uint64_t nord2_gpu_budget_mib(uint64_t mib, uint64_t lo_mib,
                                            uint64_t hi_mib, uint64_t dflt)
{
        if (!mib)
                return dflt;
        if (mib < lo_mib)
                mib = lo_mib;
        if (mib > hi_mib)
                mib = hi_mib;
        return mib << 20;
}

/* Device-wide check: the request (already rounded) plus every client's private
 * bytes must stay within the global private budget. */
static inline bool nord2_gpu_global_private_ok(uint64_t total_private,
                                               uint64_t rounded,
                                               uint64_t limit)
{
        return total_private <= limit && rounded <= limit - total_private;
}

/* Imported surface pages: 4 KiB granularity (surface offset/length are page
 * aligned), bounded by NORD2_GPU_IMPORT_LIMIT of already-imported bytes. */
static inline bool nord2_gpu_import_size(uint64_t mapped, uint64_t imported,
                                         uint64_t *charged)
{
        const uint64_t limit = NORD2_GPU_IMPORT_LIMIT;
        if (!mapped || (mapped & 4095u) || mapped > limit || imported > limit ||
            !charged || mapped > limit - imported)
                return false;
        *charged = mapped;
        return true;
}

/* Validate counts AND pointers before the IOKit adapter touches any buffer.
 * Descriptor-backed/in-band async requests are not part of ABI v1. */
static inline bool nord2_gpu_call_valid(uint32_t selector,
        uint32_t ni, uint32_t no, size_t si, size_t so,
        const void *iv, void *ov, const void *ib, void *ob)
{
        if ((ni && !iv) || (no && !ov) || (si && !ib) || (so && !ob))
                return false;
        unsigned expected_ni = 0, expected_no = 0;
        size_t expected_si = 0, minimum_so = 0;
        switch (selector) {
        case NORD2_GPU_SEL_LOG: return !ni&&!no&&!so&&si>0&&si<=240;
        case NORD2_GPU_SEL_STATUS: minimum_so = sizeof(struct nord2_gpu_status); break;
        case NORD2_GPU_SEL_HWINFO: minimum_so = sizeof(struct nord2_gpu_hwinfo); break;
        case NORD2_GPU_SEL_ALLOC: expected_ni = 2; expected_no = 3; break;
        case NORD2_GPU_SEL_RELEASE: expected_ni = 1; break;
        case NORD2_GPU_SEL_SUBMIT_COMPUTE:
                expected_si = sizeof(struct nord2_gpu_compute_request);
                expected_no = 1; break;
        case NORD2_GPU_SEL_WAIT:
                expected_ni = 2; expected_no = 1;
                if (so) minimum_so = sizeof(struct nord2_gpu_fault_info);
                break;
        case NORD2_GPU_SEL_FAULT: minimum_so = sizeof(struct nord2_gpu_fault_info); break;
        case NORD2_GPU_SEL_PROBE: break;
        case NORD2_GPU_SEL_RESET: expected_no = 1; break;
        case NORD2_GPU_SEL_IMPORT_SURFACE: expected_ni = 4; expected_no = 3; break;
        case NORD2_GPU_SEL_SUBMIT_JM:
                expected_si = sizeof(struct nord2_gpu_submit_request);
                expected_no = 1; break;
        case NORD2_GPU_SEL_SYNC: expected_ni = 2; break;
        case NORD2_GPU_SEL_SURFACE_RESERVE:
                if (!iv || ((const uint64_t *)iv)[0] > NORD2_GPU_RESERVE_MAX) return false;
                expected_ni = 1; expected_no = 1; expected_si = (size_t)((const uint64_t *)iv)[0] * 4u; break;
        case NORD2_GPU_SEL_SURFACE_RETIRE: expected_ni = 2; break;
        case NORD2_GPU_SEL_WAIT_SEQ:
                expected_ni = 2; expected_no = 3;
                if (so) minimum_so = sizeof(struct nord2_gpu_fault_info);
                break;
        default: return false;
        }
        return ni == expected_ni && no >= expected_no && si == expected_si &&
               so >= minimum_so;
}

/* ---- IOConnectTrap6 fast path (0111) ---------------------------------------------------------------------------------------------------
 * IOConnectTrap6(conn, NORD2_GPU_TRAP_CALL, &call, sizeof(call), 0, 0, 0, 0) runs the same handler as IOConnectCallMethod on the caller's thread
 * without a Mach message (the frontend's probe is gpu/metal/tegu/nord2_transport.inc; the layout below is byte-identical to its copy and the
 * host test compares both).  XNU answers success without running anything when a client has no trap at the index, so `done` is the only proof
 * that the call executed; the method's own IOReturn is in `result`. */
#define NORD2_GPU_TRAP_SCALARS 4u
#define NORD2_GPU_TRAP_STRUCT_IN_MAX 16384u   /* the LOG selector's bound */
#define NORD2_GPU_TRAP_STRUCT_OUT_MAX 256u    /* largest reply is nord2_gpu_status (88 bytes) */
/* The frontend's nord2_transport.inc defines the same macros and the same struct (a translation unit may include it BEFORE this header, e.g. the
 * frontend host tests): the frontend copy wins and the block below is skipped.  Both are compared byte-for-byte by nord2_trap_abi_test. */
#ifndef NORD2_GPU_TRAP_CALL
#define NORD2_GPU_TRAP_CALL 0u
#define NORD2_GPU_TRAP_MAGIC 0x4e325443u   /* 'N2TC' */
#define NORD2_GPU_TRAP_DONE 0x4e325444u    /* 'N2TD' */
struct nord2_gpu_trap_call {
        uint32_t magic, selector, scalar_in_count, scalar_out_count;
        uint64_t scalar_in[4], scalar_out[4];
        uint64_t struct_in, struct_in_size, struct_out, struct_out_size;
        int32_t result;
        uint32_t done;
};
#endif

/* Reject a trap call before any byte of it is used.  `size_arg` is p2 of the trap.  The selector and the counts are checked again, with the
 * buffers, by nord2_gpu_call_valid() inside the shared handler. */
static inline bool nord2_gpu_trap_call_ok(const struct nord2_gpu_trap_call *c, uint64_t size_arg)
{
        if (!c || size_arg != sizeof(*c) || c->magic != NORD2_GPU_TRAP_MAGIC || c->done != 0)
                return false;
        if (c->scalar_in_count > NORD2_GPU_TRAP_SCALARS || c->scalar_out_count > NORD2_GPU_TRAP_SCALARS)
                return false;
        if (c->struct_in_size > NORD2_GPU_TRAP_STRUCT_IN_MAX || (c->struct_in_size && !c->struct_in))
                return false;
        if (c->struct_out_size > NORD2_GPU_TRAP_STRUCT_OUT_MAX || (c->struct_out_size && !c->struct_out))
                return false;
        return true;
}

/* Fill the reply of an executed call from what the shared handler left in its argument block; counts are clamped to what the caller offered
 * and to the 4 scalars of the ABI.  Returns the number of bytes of struct output to copy out. */
static inline uint64_t nord2_gpu_trap_finish(struct nord2_gpu_trap_call *c, int32_t method_result, const uint64_t *scalars,
                                              uint32_t scalar_count, uint64_t struct_out_size)
{
        c->result = method_result;
        c->scalar_out_count = scalar_count > NORD2_GPU_TRAP_SCALARS ? NORD2_GPU_TRAP_SCALARS : scalar_count;
        for (uint32_t i = 0; i < c->scalar_out_count; i++)
                c->scalar_out[i] = scalars[i];
        for (uint32_t i = c->scalar_out_count; i < NORD2_GPU_TRAP_SCALARS; i++)
                c->scalar_out[i] = 0;
        if (struct_out_size > NORD2_GPU_TRAP_STRUCT_OUT_MAX)
                struct_out_size = NORD2_GPU_TRAP_STRUCT_OUT_MAX;
        c->struct_out_size = struct_out_size;
        c->done = NORD2_GPU_TRAP_DONE;
        return struct_out_size;
}
#endif
