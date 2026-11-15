/*
 * Nord 2 arch9 JM job-chain construction.
 *
 * The driver deliberately separates the vertex/tiler chain (feature-selected slot, normally 1) from
 * the fragment chain (feature-selected slot, normally 0), matching the r32p1 JM submission model and
 * Mesa's jm_submit_batch().  Cross-chain ordering is enforced by the scheduler:
 * the fragment chain is only submitted after the vertex/tiler chain reports
 * completion.  A compute chain (feature-selected compute slot) is a single COMPUTE job.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_JM_H
#define NORD2_GPU_JM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nord2_gpu_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NORD2_GPU_JM_MAX_CHAIN 4096u

struct nord2_gpu_compute_chain {
        uint32_t workgroup_size[3];
        uint32_t workgroup_count[3];
        bool allow_merging;
        uint64_t resources;
        uint64_t shader;
        uint64_t thread_storage;
        uint64_t fau;
        uint8_t fau_count;
        uint32_t attribute_offset;
        uint64_t chain_pa;
        uint64_t next_pa;
        uint16_t index;
};

struct nord2_gpu_vertex_chain {
        enum nord2_draw_mode mode;
        enum nord2_index_type index_type;
        uint32_t index_count;
        int32_t base_vertex;
        uint32_t instance_count;
        uint32_t vertex_packet_stride;
        uint32_t vertex_attribute_stride;
        uint64_t tiler_context;
        uint64_t indices;
        uint32_t scissor_min_x, scissor_min_y;
        uint32_t scissor_max_x, scissor_max_y;
        uint32_t flags_0, flags_1;
        struct nord2_draw_state draw;
        uint64_t position_resources, position_shader, position_tls,
                position_fau;
        uint8_t position_fau_count;
        uint32_t position_attribute_offset;
        uint64_t varying_resources, varying_shader, varying_tls, varying_fau;
        uint8_t varying_fau_count;
        uint32_t varying_attribute_offset;
        uint64_t fragment_resources, fragment_shader, fragment_tls,
                fragment_fau;
        uint8_t fragment_fau_count;
        uint32_t fragment_attribute_offset;
        uint64_t chain_pa;
        uint16_t index;
};

struct nord2_gpu_fragment_chain {
        uint32_t bound_min_x, bound_min_y;
        uint32_t bound_max_x, bound_max_y;
        uint64_t framebuffer;
        uint64_t chain_pa;
        uint16_t index;
};

/* Build into caller-owned, GPU-visible command memory.  `chain_pa` is the GPU
 * address of `out`; `next_pa` is the optional next job in this chain. */
bool nord2_gpu_jm_build_compute(uint8_t *out, size_t out_size,
                                const struct nord2_gpu_compute_chain *chain);
bool nord2_gpu_jm_build_vertex(uint8_t *out, size_t out_size,
                               const struct nord2_gpu_vertex_chain *chain);
bool nord2_gpu_jm_build_fragment(uint8_t *out, size_t out_size,
                                 const struct nord2_gpu_fragment_chain *chain);

/* Chain sizes are fixed by the arch9 layouts. */
#define NORD2_GPU_COMPUTE_CHAIN_BYTES NORD2_DESC_COMPUTE_JOB_SIZE
#define NORD2_GPU_VERTEX_CHAIN_BYTES NORD2_DESC_MALLOC_VERTEX_JOB_SIZE
#define NORD2_GPU_FRAGMENT_CHAIN_BYTES NORD2_DESC_FRAGMENT_JOB_SIZE

/* ABI-v1 diagnostic program: no TLS/preload, 64 registers/thread. The
 * environment points at SHADER_PROGRAM, which then points at machine code.
 * c->shader is the binary address for this combined builder only. */
#define NORD2_GPU_COMPUTE_PROGRAM_BYTES \
        (NORD2_GPU_COMPUTE_CHAIN_BYTES + NORD2_DESC_SHADER_PROGRAM_SIZE)
bool nord2_gpu_jm_build_compute_program(uint8_t *out, size_t out_size,
                                         const struct nord2_gpu_compute_chain *c);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_JM_H */
