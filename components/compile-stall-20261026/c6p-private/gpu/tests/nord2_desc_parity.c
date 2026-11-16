/*
 * Byte-parity test between the Nord 2 arch9 packers and the pinned Mesa
 * genxml v9 packers.  This is the independent descriptor oracle required by
 * the overnight contract: our production packer must produce the exact same
 * bytes as Mesa for every field exercised here.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "genxml/v9_pack.h"
#include "nord2_gpu_desc.h"
#include "nord2_gpu_jm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

static void compare(const char *name, const uint8_t *ours,
                    const uint32_t *theirs, size_t words)
{
        int mismatch = 0;
        for (size_t i = 0; i < words; i++) {
                uint32_t w;
                memcpy(&w, ours + i * 4, sizeof(w));
                if (w != theirs[i]) {
                        fprintf(stderr,
                                "FAIL %s word %zu: ours=%08x mesa=%08x\n",
                                name, i, w, theirs[i]);
                        mismatch = 1;
                        failures++;
                }
        }
        if (!mismatch)
                printf("PASS %s (%zu words)\n", name, words);
}

static void test_job_header(void)
{
        uint8_t ours[NORD2_DESC_JOB_HEADER_SIZE];
        struct mali_job_header_packed p = {0};
        struct MALI_JOB_HEADER h = {
                .type = MALI_JOB_TYPE_MALLOC_VERTEX,
                .barrier = true,
                .suppress_prefetch = true,
                .index = 7,
                .dependency_1 = 3,
                .dependency_2 = 5,
                .next = 0x123456789abc0ULL,
        };
        nord2_desc_job_header(ours, NORD2_JOB_MALLOC_VERTEX, 7, 3, 5, true,
                              true, h.next);
        MALI_JOB_HEADER_pack(&p, &h);
        compare("job_header", ours, p.opaque, 8);
}

static void test_shader_env(void)
{
        uint8_t ours[NORD2_DESC_SHADER_ENV_SIZE];
        struct mali_shader_environment_packed p = {0};
        struct MALI_SHADER_ENVIRONMENT h = {
                .attribute_offset = 11,
                .fau_count = 5,
                .resources = 0x100000001000ULL,
                .shader = 0x200000002000ULL,
                .thread_storage = 0x300000003000ULL,
                .fau = 0x400000004000ULL,
        };
        nord2_desc_shader_environment(ours, h.resources, h.shader,
                                      h.thread_storage, h.fau, h.fau_count,
                                      h.attribute_offset);
        MALI_SHADER_ENVIRONMENT_pack(&p, &h);
        compare("shader_environment", ours, p.opaque, 16);
}

static void test_compute_job(void)
{
        uint8_t ours[NORD2_DESC_COMPUTE_JOB_SIZE];
        struct mali_compute_job_packed p = {0};
        struct MALI_JOB_HEADER h = {
                .type = MALI_JOB_TYPE_COMPUTE,
                .index = 2,
                .dependency_1 = 0,
                .dependency_2 = 0,
                .next = 0x500000005000ULL,
        };
        struct MALI_COMPUTE_PAYLOAD c = {
                .workgroup_size_x = 1,
                .workgroup_size_y = 2,
                .workgroup_size_z = 3,
                .allow_merging_workgroups = true,
                .task_increment = 1,
                .task_axis = MALI_TASK_AXIS_Z,
                .workgroup_count_x = 4,
                .workgroup_count_y = 5,
                .workgroup_count_z = 6,
                .compute = {
                        .attribute_offset = 0,
                        .fau_count = 2,
                        .resources = 0x600000006000ULL,
                        .shader = 0x700000007000ULL,
                        .thread_storage = 0x800000008000ULL,
                        .fau = 0x900000009000ULL,
                },
        };
        nord2_desc_compute_job(ours, NORD2_JOB_COMPUTE, 2, 0, 0, false,
                               h.next,
                               (uint32_t[]){1, 2, 3}, (uint32_t[]){4, 5, 6},
                               true, c.compute.resources, c.compute.shader,
                               c.compute.thread_storage, c.compute.fau,
                               c.compute.fau_count, 0);
        MALI_JOB_HEADER_pack(&p.HEADER, &h);
        MALI_COMPUTE_PAYLOAD_pack(&p.PAYLOAD, &c);
        compare("compute_job", ours, p.opaque, 32);
}

/* Compare a complete production packet, not just isolated packers. */
static void test_compute_program_packet(void)
{
        uint8_t ours[NORD2_GPU_COMPUTE_PROGRAM_BYTES];
        const uint64_t chain_va = 0x1000000000ull, code_va = 0x1000020000ull;
        struct nord2_gpu_compute_chain chain = {
                .chain_pa = chain_va, .shader = code_va, .index = 1,
                .workgroup_size = {1, 1, 1}, .workgroup_count = {1, 1, 1},
        };
        struct mali_compute_job_packed expected = {0};
        struct MALI_JOB_HEADER header = { .type = MALI_JOB_TYPE_COMPUTE, .index = 1 };
        struct MALI_COMPUTE_PAYLOAD payload = {
                .workgroup_size_x = 1, .workgroup_size_y = 1, .workgroup_size_z = 1,
                .workgroup_count_x = 1, .workgroup_count_y = 1, .workgroup_count_z = 1,
                /* Mesa jm_launch_grid uses increment1/axisZ on arch9. */
                .task_increment = 1, .task_axis = MALI_TASK_AXIS_Z,
                .compute = { .shader = chain_va + 128 },
        };
        struct mali_shader_program_packed shader = {0};
        struct MALI_SHADER_PROGRAM program = {
                .type = MALI_DESCRIPTOR_TYPE_SHADER, .stage = MALI_SHADER_STAGE_COMPUTE,
                .register_allocation = MALI_SHADER_REGISTER_ALLOCATION_64_PER_THREAD,
                .binary = code_va,
        };
        if (!nord2_gpu_jm_build_compute_program(ours, sizeof(ours), &chain)) {
                failures++; return;
        }
        MALI_JOB_HEADER_pack(&expected.HEADER, &header);
        MALI_COMPUTE_PAYLOAD_pack(&expected.PAYLOAD, &payload);
        MALI_SHADER_PROGRAM_pack(&shader, &program);
        compare("compute_packet_environment", ours, expected.opaque, 32);
        compare("compute_packet_program", ours + 128, shader.opaque, 8);
}

static void test_fragment_job(void)
{
        uint8_t ours[NORD2_DESC_FRAGMENT_JOB_SIZE];
        struct mali_fragment_job_packed p = {0};
        struct MALI_JOB_HEADER h = {
                .type = MALI_JOB_TYPE_FRAGMENT,
                .index = 9,
                .next = 0,
        };
        struct MALI_FRAGMENT_JOB_PAYLOAD f = {
                .bound_min_x = 1,
                .bound_min_y = 2,
                .bound_max_x = 1125,
                .bound_max_y = 2436,
                .tile_render_order = MALI_TILE_RENDER_ORDER_Z_ORDER,
                .has_tile_enable_map = false,
                .framebuffer = 0xa0000000a000ULL,
        };
        nord2_desc_fragment_job(ours, 9, 0, 0, false, 0, 1, 2, 1125, 2436,
                                f.framebuffer);
        MALI_JOB_HEADER_pack(&p.HEADER, &h);
        MALI_FRAGMENT_JOB_PAYLOAD_pack(&p.PAYLOAD, &f);
        compare("fragment_job", ours, p.opaque, 16);
}

static void test_framebuffer(void)
{
        uint8_t ours[NORD2_DESC_FRAMEBUFFER_SIZE];
        struct mali_framebuffer_packed p = {0};
        struct MALI_FRAMEBUFFER_PARAMETERS f = {
                .pre_frame_0 = MALI_PRE_POST_FRAME_SHADER_MODE_NEVER,
                .pre_frame_1 = MALI_PRE_POST_FRAME_SHADER_MODE_NEVER,
                .post_frame = MALI_PRE_POST_FRAME_SHADER_MODE_NEVER,
                .downscale_geometry = MALI_DOWNSCALE_NONE,
                .internal_layer_index = 0,
                .frame_argument = 0,
                .sample_locations = 0,
                .frame_shader_dcds = 0,
                .width = 1125,
                .height = 2436,
                .bound_min_x = 0,
                .bound_min_y = 0,
                .bound_max_x = 1125,
                .bound_max_y = 2436,
                .sample_count = 1,
                .sample_pattern = MALI_SAMPLE_PATTERN_SINGLE_SAMPLED,
                .tie_break_rule = MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT,
                .effective_tile_size = 16,
                .first_provoking_vertex = false,
                .point_sprite_coord_origin_max_y = true,
                .blend_suppress_inf = false,
                .blend_suppress_nan = false,
                .blend_suppress_denorm = false,
                .render_target_count = 1,
                .color_buffer_allocation = 1024,
                .s_clear = 0,
                .s_write_enable = false,
                .s_preload_enable = false,
                .s_unload_enable = false,
                .z_internal_format = MALI_Z_INTERNAL_FORMAT_D16,
                .z_write_enable = false,
                .z_preload_enable = false,
                .z_unload_enable = false,
                .has_zs_crc_extension = false,
                .empty_tile_read_enable = false,
                .empty_tile_write_enable = false,
                .crc_read_enable = false,
                .crc_write_enable = false,
                .z_clear = 0.0f,
                .tiler = 0xb0000000b000ULL,
        };
        struct nord2_framebuffer_info fb = {
                .width = 1125,
                .height = 2436,
                .bound_min_x = 0,
                .bound_min_y = 0,
                .bound_max_x = 1125,
                .bound_max_y = 2436,
                .render_target_count = 1,
                .tile_size_px = 16,
                .tiler_context = 0xb0000000b000ULL,
                .sample_count = 1,
                .first_provoking_vertex = false,
        };
        nord2_desc_framebuffer(ours, &fb);
        MALI_FRAMEBUFFER_PARAMETERS_pack(&p.PARAMETERS, &f);
        compare("framebuffer", ours, p.opaque, 32);
}

static void test_render_target(void)
{
        uint8_t ours[NORD2_DESC_RENDER_TARGET_SIZE];
        struct mali_rgb_render_target_packed p = {0};
        struct MALI_RGB_RENDER_TARGET r = {
                .internal_buffer_offset = 0,
                .yuv_enable = false,
                .dithered_clear = false,
                .internal_format = MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8,
                .write_enable = true,
                .writeback_format = MALI_COLOR_FORMAT_R8G8B8A8,
                .writeback_float_format = 0,
                .writeback_block_format = MALI_BLOCK_FORMAT_LINEAR,
                .writeback_msaa = MALI_MSAA_SINGLE,
                .srgb = false,
                .dithering_enable = true,
                .swizzle = 0x60a,
                .clean_tile_write_enable = false,
                .writeback_buffer = {
                        .base = 0xc0000000c000ULL,
                        .row_stride = 4500,
                        .surface_stride = 4500 * 2436,
                },
                .clear = { .color_0 = 0, .color_1 = 0, .color_2 = 0, .color_3 = 0 },
        };
        struct nord2_render_target rt = {
                .base = r.writeback_buffer.base,
                .row_stride = r.writeback_buffer.row_stride,
                .surface_stride = r.writeback_buffer.surface_stride,
                .width = 1125,
                .height = 2436,
                .internal_format = NORD2_COLOR_INTERNAL_R8G8B8A8,
                .writeback_format = NORD2_COLOR_R8G8B8A8,
                .block_format = NORD2_BLOCK_LINEAR,
                .swizzle = 0x60a,
                .srgb = false,
                .write_enable = true,
                .clear_color = {0, 0, 0, 0},
        };
        nord2_desc_render_target(ours, &rt);
        MALI_RGB_RENDER_TARGET_pack(&p, &r);
        compare("render_target", ours, p.opaque, 16);
}

static void test_tiler_heap(void)
{
        uint8_t ours[NORD2_DESC_TILER_HEAP_SIZE];
        struct mali_tiler_heap_packed p = {0};
        struct MALI_TILER_HEAP h = {
                .type = MALI_DESCRIPTOR_TYPE_BUFFER,
                .buffer_type = MALI_BUFFER_TYPE_TILER_HEAP,
                .chunk_size = MALI_CHUNK_SIZE_2_MIB,
                .partitioning = MALI_CHUNK_PARTITION_DYNAMIC,
                .size = 0x200000,
                .base = 0xd0000000d000ULL,
                .bottom = 0xd0000000d000ULL,
                .top = 0xd0000000d000ULL + 0x200000,
        };
        nord2_desc_tiler_heap(ours, h.size, h.base, h.bottom, h.top,
                              NORD2_TILER_CHUNK_2M);
        MALI_TILER_HEAP_pack(&p, &h);
        compare("tiler_heap", ours, p.opaque, 8);
}

static void test_tiler_context(void)
{
        uint8_t ours[NORD2_DESC_TILER_CONTEXT_SIZE];
        struct mali_tiler_context_packed p = {0};
        struct MALI_TILER_CONTEXT t = {
                .polygon_list = 0xe0000000e000ULL,
                .hierarchy_mask = 0x155,
                .sample_pattern = MALI_SAMPLE_PATTERN_SINGLE_SAMPLED,
                .update_cost_table = false,
                .sample_test_disable = false,
                .first_provoking_vertex = false,
                .fb_width = 1125,
                .fb_height = 2436,
                .layer_count = 1,
                .layer_offset = 0,
                .heap = 0xf0000000f000ULL,
        };
        nord2_desc_tiler_context(ours, t.polygon_list, t.fb_width, t.fb_height,
                                 t.heap, t.hierarchy_mask,
                                 MALI_SAMPLE_PATTERN_SINGLE_SAMPLED,
                                 false);
        MALI_TILER_CONTEXT_pack(&p, &t);
        compare("tiler_context", ours, p.opaque, 48);
}

static void test_resource(void)
{
        uint8_t ours[NORD2_DESC_RESOURCE_SIZE];
        struct mali_resource_packed p = {0};
        struct MALI_RESOURCE r = {
                .address = 0x123456789abc0ULL,
                .contains_descriptors = true,
                .size = 4096,
        };
        nord2_desc_resource(ours, r.address, (uint32_t)r.size,
                            r.contains_descriptors);
        MALI_RESOURCE_pack(&p, &r);
        compare("resource_table_entry", ours, p.opaque, 4);
}

static void test_buffer(void)
{
        uint8_t ours[NORD2_DESC_BUFFER_SIZE];
        struct mali_buffer_packed p = {0};
        struct MALI_BUFFER b = {
                .type = MALI_DESCRIPTOR_TYPE_BUFFER,
                .buffer_type = MALI_BUFFER_TYPE_SIMPLE,
                .size = 4096,
                .address = 0x123456789abc0ULL,
        };
        nord2_desc_buffer(ours, b.address, (uint32_t)b.size,
                          NORD2_BUFFER_SIMPLE);
        MALI_BUFFER_pack(&p, &b);
        compare("buffer_descriptor", ours, p.opaque, 8);
}


static void test_texture(void)
{
        uint8_t ours[NORD2_DESC_TEXTURE_SIZE];
        struct mali_texture_packed p = {0};
        struct MALI_TEXTURE t = {
                .type = MALI_DESCRIPTOR_TYPE_TEXTURE,
                .dimension = MALI_TEXTURE_DIMENSION_2D,
                .sample_corner_position = false,
                .normalize_coordinates = false,
                .format = NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM,
                .width = 1125,
                .height = 2436,
                .swizzle = 0x688,
                .texel_interleave = false,
                .levels = 1,
                .minimum_level = 0,
                .minimum_lod = 0.0f,
                .sample_count = 1,
                .maximum_lod = 0.0f,
                .surfaces = 0x110000001100ULL,
                .array_size = 1,
                .depth = 1,
        };
        nord2_desc_texture(ours, NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM,
                           t.width, t.height, t.levels, t.surfaces, 0);
        MALI_TEXTURE_pack(&p, &t);
        compare("texture", ours, p.opaque, 8);
}

static void test_sampler(void)
{
        uint8_t ours[NORD2_DESC_SAMPLER_SIZE];
        struct mali_sampler_packed p = {0};
        struct MALI_SAMPLER s = {
                .type = MALI_DESCRIPTOR_TYPE_SAMPLER,
                .wrap_mode_r = MALI_WRAP_MODE_CLAMP_TO_EDGE,
                .wrap_mode_t = MALI_WRAP_MODE_CLAMP_TO_EDGE,
                .wrap_mode_s = MALI_WRAP_MODE_CLAMP_TO_EDGE,
                .round_to_nearest_even = true,
                .srgb_override = false,
                .seamless_cube_map = true,
                .clamp_integer_coordinates = false,
                .normalized_coordinates = true,
                .clamp_integer_array_indices = true,
                .minify_nearest = false,
                .magnify_nearest = false,
                .magnify_cutoff = false,
                .mipmap_mode = MALI_MIPMAP_MODE_NEAREST,
                .minimum_lod = 0.0f,
                .compare_function = MALI_FUNC_NEVER,
                .maximum_lod = 0.0f,
                .lod_bias = 0.0f,
                .maximum_anisotropy = 1,
                .lod_algorithm = MALI_LOD_ALGORITHM_ISOTROPIC,
        };
        nord2_desc_sampler(ours, 9, 9, 9, false, false, false, 1);
        MALI_SAMPLER_pack(&p, &s);
        compare("sampler", ours, p.opaque, 8);
}

static void test_attribute(void)
{
        uint8_t ours[NORD2_DESC_ATTRIBUTE_SIZE];
        struct mali_attribute_packed p = {0};
        struct MALI_ATTRIBUTE a = {
                .type = MALI_DESCRIPTOR_TYPE_ATTRIBUTE,
                .attribute_type = MALI_ATTRIBUTE_TYPE_1D,
                .offset_enable = false,
                .format = NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM,
                .table = 0,
                .frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX,
                .divisor_r = 0,
                .divisor_e = 0,
                .offset = 0,
                .buffer_index = 2,
                .stride = 16,
        };
        nord2_desc_attribute(ours, a.attribute_type, a.format, a.offset,
                             a.buffer_index, a.stride);
        MALI_ATTRIBUTE_pack(&p, &a);
        compare("attribute", ours, p.opaque, 8);
}

static void test_shader_program(void)
{
        uint8_t ours[NORD2_DESC_SHADER_PROGRAM_SIZE];
        struct mali_shader_program_packed p = {0};
        struct MALI_SHADER_PROGRAM sp = {
                .type = MALI_DESCRIPTOR_TYPE_SHADER,
                .stage = MALI_SHADER_STAGE_COMPUTE,
                .suppress_nan = false,
                .flush_to_zero_mode = MALI_FLUSH_TO_ZERO_MODE_PRESERVE_SUBNORMALS,
                .suppress_inf = false,
                .requires_helper_threads = false,
                .shader_contains_jump_ex = false,
                .register_allocation = MALI_SHADER_REGISTER_ALLOCATION_64_PER_THREAD,
                .preload = {0},
                .binary = 0x120000001200ULL,
        };
        nord2_desc_shader_program(ours, NORD2_STAGE_COMPUTE, sp.binary,
                                  0);
        MALI_SHADER_PROGRAM_pack(&p, &sp);
        compare("shader_program", ours, p.opaque, 8);
}

static void test_local_storage(void)
{
        uint8_t ours[NORD2_DESC_LOCAL_STORAGE_SIZE];
        struct mali_local_storage_packed p = {0};
        struct MALI_LOCAL_STORAGE ls = {
                .tls_size = 0,
                .wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM,
                .wls_size_base = 1,
                .wls_size_scale = 0,
                .tls_base_pointer = 0,
                .tls_address_mode = MALI_ADDRESS_MODE_PACKED,
                .wls_base_pointer = 0,
        };
        nord2_desc_local_storage(ours, 0, 0, 0, 0);
        MALI_LOCAL_STORAGE_pack(&p, &ls);
        compare("local_storage", ours, p.opaque, 8);
}

static void test_depth_stencil(void)
{
        uint8_t ours[NORD2_DESC_DEPTH_STENCIL_SIZE];
        struct mali_depth_stencil_packed p = {0};
        struct MALI_DEPTH_STENCIL ds = {
                .type = MALI_DESCRIPTOR_TYPE_DEPTH_STENCIL,
                .front_compare_function = MALI_FUNC_ALWAYS,
                .front_stencil_fail = MALI_STENCIL_OP_KEEP,
                .front_depth_fail = MALI_STENCIL_OP_KEEP,
                .front_depth_pass = MALI_STENCIL_OP_KEEP,
                .back_compare_function = MALI_FUNC_ALWAYS,
                .back_stencil_fail = MALI_STENCIL_OP_KEEP,
                .back_depth_fail = MALI_STENCIL_OP_KEEP,
                .back_depth_pass = MALI_STENCIL_OP_KEEP,
                .stencil_from_shader = false,
                .stencil_test_enable = false,
                .front_write_mask = 0xff,
                .back_write_mask = 0xff,
                .front_value_mask = 0xff,
                .back_value_mask = 0xff,
                .front_reference_value = 0,
                .back_reference_value = 0,
                .depth_cull_enable = true,
                .depth_clamp_mode = MALI_DEPTH_CLAMP_MODE_0_1,
                .depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION,
                .depth_write_enable = false,
                .depth_bias_enable = false,
                .depth_function = MALI_FUNC_LESS,
                .depth_units = 0.0f,
                .depth_factor = 0.0f,
                .depth_bias_clamp = 0.0f,
        };
        uint32_t base_flags = ((uint32_t)ds.front_compare_function << 4) |
                              ((uint32_t)ds.front_stencil_fail << 7) |
                              ((uint32_t)ds.front_depth_fail << 10) |
                              ((uint32_t)ds.front_depth_pass << 13) |
                              ((uint32_t)ds.back_compare_function << 16) |
                              ((uint32_t)ds.back_stencil_fail << 19) |
                              ((uint32_t)ds.back_depth_fail << 22) |
                              ((uint32_t)ds.back_depth_pass << 25) |
                              ((uint32_t)ds.stencil_from_shader << 30) |
                              ((uint32_t)ds.stencil_test_enable << 31);
        uint32_t masks = ds.front_write_mask | (ds.back_write_mask << 8) |
                         (ds.front_value_mask << 16) |
                         (ds.back_value_mask << 24);
        uint32_t functions = ds.front_reference_value |
                             (ds.back_reference_value << 8);
        uint32_t depth_flags = ((uint32_t)ds.depth_cull_enable << 22) |
                               ((uint32_t)ds.depth_clamp_mode << 23) |
                               ((uint32_t)ds.depth_source << 25) |
                               ((uint32_t)ds.depth_write_enable << 27) |
                               ((uint32_t)ds.depth_bias_enable << 28) |
                               ((uint32_t)ds.depth_function << 29);
        nord2_desc_depth_stencil(ours, NORD2_DESC_TYPE_DEPTH_STENCIL | base_flags,
                                 masks, functions,
                                 depth_flags, 0.0f, 0.0f, 0.0f);
        MALI_DEPTH_STENCIL_pack(&p, &ds);
        compare("depth_stencil", ours, p.opaque, 8);
}


static void test_draw_high_va(void)
{
        uint8_t ours[NORD2_DESC_DRAW_SIZE];
        struct mali_draw_packed expected = {0};
        struct MALI_DRAW draw = {0};
        draw.vertex_array.packet = true;
        draw.vertex_array.pointer = 0x1234567800ull;
        draw.blend = 0x2345678900ull;
        draw.blend_count = 3;
        draw.depth_stencil = 0x3456789000ull;
        draw.maximum_z = 1;
        draw.shader.shader = 0x4567890000ull;
        struct nord2_draw_state state = {
                .vertex_packet = true, .vertex_array = draw.vertex_array.pointer,
                .blend = draw.blend, .blend_count = draw.blend_count,
                .depth_stencil = draw.depth_stencil, .maximum_z = 1,
        };
        nord2_desc_draw(ours, &state, 0, draw.shader.shader, 0, 0, 0, 0);
        MALI_DRAW_pack(&expected, &draw);
        compare("draw_64bit_vertex_and_blend", ours, expected.opaque, NORD2_DESC_DRAW_SIZE/4);
}

static void test_blend(void)
{
        uint8_t ours[NORD2_DESC_BLEND_SIZE];
        struct mali_blend_packed p = {0};
        struct MALI_BLEND b = {0};
        b.load_destination = true;
        b.enable = true;
        b.blend_constant = 0x1234;
        b.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
        b.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
        b.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
        b.equation.alpha = b.equation.rgb;
        b.equation.color_mask = 0xf;
        b.internal.mode = MALI_BLEND_MODE_FIXED_FUNCTION;
        b.internal.fixed_function.num_comps = 4;
        b.internal.fixed_function.rt = 0;
        b.internal.fixed_function.conversion.memory_format =
                NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM;
        uint32_t eq_rgb = ((uint32_t)b.equation.rgb.a) |
                          ((uint32_t)b.equation.rgb.negate_a << 3) |
                          ((uint32_t)b.equation.rgb.b << 4) |
                          ((uint32_t)b.equation.rgb.negate_b << 7) |
                          ((uint32_t)b.equation.rgb.c << 8) |
                          ((uint32_t)b.equation.rgb.invert_c << 11);
        uint32_t eq_alpha = ((uint32_t)b.equation.alpha.a) |
                            ((uint32_t)b.equation.alpha.negate_a << 3) |
                            ((uint32_t)b.equation.alpha.b << 4) |
                            ((uint32_t)b.equation.alpha.negate_b << 7) |
                            ((uint32_t)b.equation.alpha.c << 8) |
                            ((uint32_t)b.equation.alpha.invert_c << 11);
        nord2_desc_blend(ours, b.load_destination, b.blend_constant, eq_rgb,
                         eq_alpha, b.equation.color_mask,
                         b.internal.fixed_function.num_comps,
                         b.internal.fixed_function.rt,
                         b.internal.fixed_function.conversion.memory_format,
                         false, b.enable);
        MALI_BLEND_pack(&p, &b);
        compare("blend", ours, p.opaque, 4);
}

int main(void)
{
        test_job_header();
        test_shader_env();
        test_compute_job();
        test_compute_program_packet();
        test_fragment_job();
        test_framebuffer();
        test_render_target();
        test_tiler_heap();
        test_tiler_context();
        test_resource();
        test_buffer();
        test_texture();
        test_sampler();
        test_attribute();
        test_shader_program();
        test_local_storage();
        test_depth_stencil();
        test_blend();
        test_draw_high_va();
        if (failures) {
                fprintf(stderr, "%u descriptor mismatches\n", failures);
                return 1;
        }
        printf("all descriptor byte-parity checks passed\n");
        return 0;
}
