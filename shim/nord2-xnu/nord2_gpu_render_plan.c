/*
 * Nord 2 arch9 render-plan builder.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_render_plan.h"

#include <stddef.h>

bool nord2_gpu_render_plan_build(const struct nord2_gpu_render_plan *p)
{
        if (!p || !p->heap_desc || !p->tiler_ctx || !p->framebuffer ||
            !p->render_target || !p->vs_chain || !p->fs_chain)
                return false;
        if (!p->width || !p->height || p->width > 0xfffu ||
            p->height > 0xfffu || p->row_stride < (uint64_t)p->width * 4)
                return false;
        if (p->target_size < (uint64_t)p->row_stride * p->height ||
            p->tiler_heap_size < 0x100000 ||
            !p->tiler_heap_va || !p->tiler_ctx_va ||
            !p->framebuffer_va || !p->render_target_va ||
            !p->target_va || !p->resources_va || !p->vs_shader_va ||
            !p->vs_fau_va || !p->fs_shader_va || !p->fs_fau_va ||
            !p->vs_chain_va || !p->fs_chain_va ||
            !p->vs_fau_count || !p->fs_fau_count ||
            p->vs_fau_count > 128 || p->fs_fau_count > 128)
                return false;

        nord2_desc_tiler_heap(p->heap_desc, p->tiler_heap_size,
                              p->tiler_heap_va, p->tiler_heap_va,
                              p->tiler_heap_va + p->tiler_heap_size,
                              NORD2_TILER_CHUNK_1M);
        nord2_desc_tiler_context(p->tiler_ctx, 0, p->width, p->height,
                                 p->tiler_heap_va, 0x155,
                                 NORD2_DESC_TYPE_NULL, false);

        struct nord2_framebuffer_info fb = {
                .width = p->width,
                .height = p->height,
                .bound_min_x = 0,
                .bound_min_y = 0,
                .bound_max_x = p->width,
                .bound_max_y = p->height,
                .render_target_count = 1,
                .tile_size_px = 16,
                .tiler_context = p->tiler_ctx_va,
                .sample_count = 1,
                .first_provoking_vertex = false,
        };
        nord2_desc_framebuffer(p->framebuffer, &fb);

        struct nord2_render_target rt = {
                .base = p->target_va,
                .row_stride = p->row_stride,
                .surface_stride = p->row_stride * p->height,
                .width = p->width,
                .height = p->height,
                .internal_format = NORD2_COLOR_INTERNAL_R8G8B8A8,
                .writeback_format = NORD2_COLOR_R8G8B8A8,
                .block_format = NORD2_BLOCK_LINEAR,
                .swizzle = 0x60a,
                .srgb = false,
                .write_enable = true,
                .clear_color = {p->clear_color, 0, 0, 0},
        };
        nord2_desc_render_target(p->render_target, &rt);

        struct nord2_gpu_vertex_chain vs = {
                .mode = NORD2_DRAW_TRIANGLES,
                .index_type = NORD2_INDEX_NONE,
                .index_count = 3,
                .base_vertex = 0,
                .instance_count = 1,
                .vertex_packet_stride = 16,
                .vertex_attribute_stride = 0,
                .tiler_context = p->tiler_ctx_va,
                .indices = 0,
                .scissor_min_x = 0,
                .scissor_min_y = 0,
                .scissor_max_x = p->width,
                .scissor_max_y = p->height,
                .draw = {
                        .minimum_z = 0.0f,
                        .maximum_z = 1.0f,
                },
                .position_resources = p->resources_va,
                .position_shader = p->vs_shader_va,
                .position_fau = p->vs_fau_va,
                .position_fau_count = p->vs_fau_count,
                .fragment_resources = p->resources_va,
                .fragment_shader = p->fs_shader_va,
                .fragment_fau = p->fs_fau_va,
                .fragment_fau_count = p->fs_fau_count,
                .chain_pa = p->vs_chain_va,
                .index = 1,
        };
        if (!nord2_gpu_jm_build_vertex(p->vs_chain,
                                       NORD2_GPU_VERTEX_CHAIN_BYTES, &vs))
                return false;

        struct nord2_gpu_fragment_chain fs = {
                .bound_min_x = 0,
                .bound_min_y = 0,
                .bound_max_x = p->width,
                .bound_max_y = p->height,
                .framebuffer = p->framebuffer_va,
                .chain_pa = p->fs_chain_va,
                .index = 2,
        };
        return nord2_gpu_jm_build_fragment(p->fs_chain,
                                           NORD2_GPU_FRAGMENT_CHAIN_BYTES,
                                           &fs);
}
