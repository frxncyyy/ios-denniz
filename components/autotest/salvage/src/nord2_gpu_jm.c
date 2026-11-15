/*
 * Nord 2 arch9 JM chain builders.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_jm.h"

#include "nord2_gpu_compat.h"

static bool addr_ok(uint64_t addr, unsigned align)
{
        return addr != 0 && addr < (1ULL << 40) && (addr & (align - 1u)) == 0;
}

bool nord2_gpu_jm_build_compute(uint8_t *out, size_t out_size,
                                const struct nord2_gpu_compute_chain *c)
{
        if (!out || !c || out_size < NORD2_GPU_COMPUTE_CHAIN_BYTES ||
            !addr_ok(c->chain_pa, 128) || !addr_ok(c->shader, 4) ||
            (c->resources && !addr_ok(c->resources, 64)) ||
            (c->thread_storage && !addr_ok(c->thread_storage, 64)) ||
            (c->fau && !addr_ok(c->fau, 8)) || c->fau_count > 128 ||
            !c->workgroup_size[0] || !c->workgroup_size[1] ||
            !c->workgroup_size[2] || !c->workgroup_count[0] ||
            !c->workgroup_count[1] || !c->workgroup_count[2] ||
            c->workgroup_size[0] > 1024 || c->workgroup_size[1] > 1024 ||
            c->workgroup_size[2] > 1024)
                return false;
        nord2_desc_zero(out, NORD2_GPU_COMPUTE_CHAIN_BYTES);
        nord2_desc_compute_job(out, NORD2_JOB_COMPUTE, c->index, 0, 0, false,
                               c->next_pa, (uint32_t *)c->workgroup_size,
                               (uint32_t *)c->workgroup_count,
                               c->allow_merging, c->resources, c->shader,
                               c->thread_storage, c->fau, c->fau_count,
                               c->attribute_offset);
        return true;
}

bool nord2_gpu_jm_build_vertex(uint8_t *out, size_t out_size,
                               const struct nord2_gpu_vertex_chain *v)
{
        if (!out || !v || out_size < NORD2_GPU_VERTEX_CHAIN_BYTES ||
            !addr_ok(v->chain_pa, 128) || !addr_ok(v->position_shader, 4) ||
            !addr_ok(v->position_resources, 64) ||
            !addr_ok(v->tiler_context, 64) ||
            (v->indices && !addr_ok(v->indices, 4)) ||
            (v->position_tls && !addr_ok(v->position_tls, 64)) ||
            (v->position_fau && !addr_ok(v->position_fau, 8)) ||
            (v->varying_shader && !addr_ok(v->varying_shader, 4)) ||
            (v->fragment_shader && !addr_ok(v->fragment_shader, 4)) ||
            !v->index_count || !v->instance_count ||
            v->position_fau_count > 128 || v->varying_fau_count > 128 ||
            v->fragment_fau_count > 128)
                return false;
        struct nord2_draw_state draw = v->draw;
        draw.minimum_z = v->draw.minimum_z;
        draw.maximum_z = v->draw.maximum_z;
        nord2_desc_malloc_vertex_job(
                out, v->index, 0, 0, 0, v->mode, v->index_type,
                v->index_count, v->base_vertex, v->instance_count,
                v->vertex_packet_stride, v->vertex_attribute_stride,
                v->tiler_context, v->scissor_min_x, v->scissor_min_y,
                v->scissor_max_x, v->scissor_max_y, v->indices, v->flags_0,
                v->flags_1, &draw, v->position_resources, v->position_shader,
                v->position_tls, v->position_fau, v->position_fau_count,
                v->position_attribute_offset, v->varying_resources,
                v->varying_shader, v->varying_tls, v->varying_fau,
                v->varying_fau_count, v->varying_attribute_offset,
                v->fragment_resources, v->fragment_shader, v->fragment_tls,
                v->fragment_fau, v->fragment_fau_count,
                v->fragment_attribute_offset);
        return true;
}

bool nord2_gpu_jm_build_fragment(uint8_t *out, size_t out_size,
                                 const struct nord2_gpu_fragment_chain *f)
{
        if (!out || !f || out_size < NORD2_GPU_FRAGMENT_CHAIN_BYTES ||
            !addr_ok(f->chain_pa, 128) || !addr_ok(f->framebuffer, 64) ||
            f->bound_max_x < f->bound_min_x ||
            f->bound_max_y < f->bound_min_y ||
            f->bound_max_x > 0xfffu || f->bound_max_y > 0xfffu)
                return false;
        nord2_desc_zero(out, NORD2_GPU_FRAGMENT_CHAIN_BYTES);
        nord2_desc_fragment_job(out, f->index, 0, 0, false, 0, f->bound_min_x,
                                f->bound_min_y, f->bound_max_x,
                                f->bound_max_y, f->framebuffer);
        return true;
}

bool nord2_gpu_jm_build_compute_program(uint8_t *out, size_t out_size,
                                         const struct nord2_gpu_compute_chain *c)
{
        if (!out || !c || out_size < NORD2_GPU_COMPUTE_PROGRAM_BYTES ||
            !addr_ok(c->shader, 4) ||
            c->chain_pa > (1ull << 40) - NORD2_GPU_COMPUTE_PROGRAM_BYTES)
                return false;
        struct nord2_gpu_compute_chain packet = *c;
        packet.shader = c->chain_pa + NORD2_GPU_COMPUTE_CHAIN_BYTES;
        if (!nord2_gpu_jm_build_compute(out, out_size, &packet))
                return false;
        nord2_desc_shader_program(out + NORD2_GPU_COMPUTE_CHAIN_BYTES,
                                    NORD2_STAGE_COMPUTE, c->shader, 0);
        return true;
}
