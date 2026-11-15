/*
 * Production arch9 render-plan composition.
 *
 * The caller owns every buffer and every GPU address.  This module only
 * validates the bounded plan and emits the v9 tiler/framebuffer/render-target
 * descriptors and the two JM chains; it performs no allocation, MMIO or
 * submission.  The kernel transport can therefore reuse the same code that is
 * host-tested here instead of duplicating test-only descriptor assembly.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_RENDER_PLAN_H
#define NORD2_GPU_RENDER_PLAN_H

#include <stdbool.h>
#include <stdint.h>

#include "nord2_gpu_desc.h"
#include "nord2_gpu_jm.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nord2_gpu_render_plan {
        uint32_t width;
        uint32_t height;
        uint32_t row_stride;
        uint64_t target_va;
        uint64_t target_size;
        uint32_t clear_color;

        uint64_t tiler_heap_va;
        uint32_t tiler_heap_size;
        uint64_t tiler_ctx_va;
        uint64_t framebuffer_va;
        uint64_t render_target_va;

        uint64_t resources_va;
        uint64_t vs_shader_va;
        uint64_t vs_fau_va;
        uint8_t vs_fau_count;
        uint64_t fs_shader_va;
        uint64_t fs_fau_va;
        uint8_t fs_fau_count;

        uint64_t vs_chain_va;
        uint64_t fs_chain_va;

        uint8_t *heap_desc;       /* NORD2_DESC_TILER_HEAP_SIZE */
        uint8_t *tiler_ctx;       /* NORD2_DESC_TILER_CONTEXT_SIZE */
        uint8_t *framebuffer;     /* NORD2_DESC_FRAMEBUFFER_SIZE */
        uint8_t *render_target;   /* NORD2_DESC_RENDER_TARGET_SIZE */
        uint8_t *vs_chain;        /* NORD2_GPU_VERTEX_CHAIN_BYTES */
        uint8_t *fs_chain;        /* NORD2_GPU_FRAGMENT_CHAIN_BYTES */
};

bool nord2_gpu_render_plan_build(const struct nord2_gpu_render_plan *plan);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_RENDER_PLAN_H */
