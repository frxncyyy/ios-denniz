/* Nord2 private transport ABI and bounds checks, shared with host tests.
 * SPDX-License-Identifier: MIT */
#ifndef NORD2_GPU_TRANSPORT_H
#define NORD2_GPU_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define NORD2_GPU_SEL_STATUS 0
#define NORD2_GPU_SEL_ALLOC 1
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
#define NORD2_GPU_TRANSPORT_VERSION 2u

/* ABI v2: raw arch9 Job Manager chains in the calling client's address
 * space. kind: 1 compute, 2 vertex/tiler + fragment, 3 fragment only. */
struct nord2_gpu_submit_request {
        uint32_t version, kind;
        uint64_t heads[2];
        uint32_t nr_handles, write_mask;
        uint32_t handles[32];
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


/* All backing is accounted in XNU's 16 KiB allocation units. */
static inline bool nord2_gpu_allocation_size(uint64_t requested,
                                             uint64_t wired, uint64_t *size)
{
        const uint64_t limit = 64ull << 20;
        if (!requested || requested > limit || wired > limit || !size)
                return false;
        uint64_t rounded = (requested + 16383u) & ~16383ull;
        if (rounded > limit - wired)
                return false;
        *size = rounded;
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
        default: return false;
        }
        return ni == expected_ni && no >= expected_no && si == expected_si &&
               so >= minimum_so;
}
#endif
