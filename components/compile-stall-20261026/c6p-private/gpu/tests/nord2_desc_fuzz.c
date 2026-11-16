/*
 * Deterministic randomized byte-parity tests for the arch9 packers against
 * Mesa's generated v9 packers.  Fixed examples live in nord2_desc_parity.c;
 * this covers field combinations.
 */
#include "genxml/v9_pack.h"
#include "nord2_gpu_desc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t state = 0x6e324770u; /* "n2Gp" */
static uint32_t rnd(void)
{
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
}
static unsigned rr(unsigned lo, unsigned hi)
{
        return lo + (rnd() % (hi - lo + 1));
}
static uint32_t al(uint32_t v, unsigned a)
{
        return (v + a - 1) & ~(a - 1);
}
static int failures;
static void cmp(const char *name, const uint8_t *ours, const uint32_t *theirs,
                size_t words)
{
        for (size_t i = 0; i < words; i++) {
                uint32_t w;
                memcpy(&w, ours + i * 4, 4);
                if (w != theirs[i]) {
                        fprintf(stderr, "FAIL %s iter word %zu: %08x != %08x\n",
                                name, i, w, theirs[i]);
                        failures++;
                        return;
                }
        }
}

int main(void)
{
        for (unsigned it = 0; it < 2000; it++) {
                /* Job header */
                {
                        uint8_t out[NORD2_DESC_JOB_HEADER_SIZE];
                        struct mali_job_header_packed p = {0};
                        struct MALI_JOB_HEADER h = {
                                .type = (enum mali_job_type)rr(1, 11),
                                .barrier = rnd() & 1,
                                .suppress_prefetch = rnd() & 1,
                                .relax_dependency_1 = false,
                                .relax_dependency_2 = false,
                                .index = rr(1, 0xffff),
                                .dependency_1 = rr(0, 0xffff),
                                .dependency_2 = rr(0, 0xffff),
                                .next = ((uint64_t)rnd() << 32) | rnd(),
                        };
                        nord2_desc_job_header(out, (enum nord2_job_type)h.type,
                                              h.index, h.dependency_1,
                                              h.dependency_2, h.barrier,
                                              h.suppress_prefetch, h.next);
                        MALI_JOB_HEADER_pack(&p, &h);
                        cmp("job", out, p.opaque, 8);
                }

                /* Compute job */
                {
                        uint8_t out[NORD2_DESC_COMPUTE_JOB_SIZE];
                        struct mali_compute_job_packed p = {0};
                        struct MALI_JOB_HEADER h = {
                                .type = MALI_JOB_TYPE_COMPUTE,
                                .index = rr(1, 0xffff),
                        };
                        struct MALI_COMPUTE_PAYLOAD c = {
                                .workgroup_size_x = rr(1, 1024),
                                .workgroup_size_y = rr(1, 1024),
                                .workgroup_size_z = rr(1, 64),
                                .allow_merging_workgroups = rnd() & 1,
                                .task_increment = 1,
                                .task_axis = MALI_TASK_AXIS_Z,
                                .workgroup_count_x = rr(1, 65535),
                                .workgroup_count_y = rr(1, 65535),
                                .workgroup_count_z = rr(1, 65535),
                                .compute = {
                                        .attribute_offset = rnd(),
                                        .fau_count = rr(0, 128),
                                        .resources = ((uint64_t)rnd() << 32) | rnd(),
                                        .shader = ((uint64_t)rnd() << 32) | rnd(),
                                        .thread_storage = ((uint64_t)rnd() << 32) | rnd(),
                                        .fau = ((uint64_t)rnd() << 32) | rnd(),
                                },
                        };
                        nord2_desc_compute_job(out, NORD2_JOB_COMPUTE, h.index, 0,
                                               0, false, 0,
                                               (uint32_t[]){c.workgroup_size_x,
                                                             c.workgroup_size_y,
                                                             c.workgroup_size_z},
                                               (uint32_t[]){c.workgroup_count_x,
                                                             c.workgroup_count_y,
                                                             c.workgroup_count_z},
                                               c.allow_merging_workgroups,
                                               c.compute.resources,
                                               c.compute.shader,
                                               c.compute.thread_storage,
                                               c.compute.fau,
                                               c.compute.fau_count,
                                               c.compute.attribute_offset);
                        MALI_JOB_HEADER_pack(&p.HEADER, &h);
                        MALI_COMPUTE_PAYLOAD_pack(&p.PAYLOAD, &c);
                        cmp("compute", out, p.opaque, 32);
                }
                /* Fragment job */
                {
                        uint8_t out[NORD2_DESC_FRAGMENT_JOB_SIZE];
                        struct mali_fragment_job_packed p = {0};
                        struct MALI_JOB_HEADER h = {
                                .type = MALI_JOB_TYPE_FRAGMENT,
                                .index = rr(1, 0xffff),
                        };
                        struct MALI_FRAGMENT_JOB_PAYLOAD f = {
                                .bound_min_x = rr(0, 0xfff),
                                .bound_min_y = rr(0, 0xfff),
                                .bound_max_x = rr(0, 0xfff),
                                .bound_max_y = rr(0, 0xfff),
                                .framebuffer = ((uint64_t)rnd() << 32) | rnd(),
                        };
                        if (f.bound_max_x < f.bound_min_x)
                                f.bound_max_x = f.bound_min_x;
                        if (f.bound_max_y < f.bound_min_y)
                                f.bound_max_y = f.bound_min_y;
                        nord2_desc_fragment_job(out, h.index, 0, 0, false, 0,
                                                f.bound_min_x, f.bound_min_y,
                                                f.bound_max_x, f.bound_max_y,
                                                f.framebuffer);
                        MALI_JOB_HEADER_pack(&p.HEADER, &h);
                        MALI_FRAGMENT_JOB_PAYLOAD_pack(&p.PAYLOAD, &f);
                        cmp("fragment", out, p.opaque, 16);
                }
                /* Framebuffer */
                {
                        uint8_t out[NORD2_DESC_FRAMEBUFFER_SIZE];
                        struct mali_framebuffer_packed p = {0};
                        uint32_t width = rr(1, 65535), height = rr(1, 65535);
                        uint32_t tile = rr(0, 4) == 0 ? 32 : 16;
                        uint32_t samples = (rnd() & 1) ? 4 : 1;
                        struct MALI_FRAMEBUFFER_PARAMETERS f = {
                                .width = width, .height = height,
                                .bound_min_x = rr(0, width - 1),
                                .bound_min_y = rr(0, height - 1),
                                .bound_max_x = width,
                                .bound_max_y = height,
                                .sample_count = samples,
                                .sample_pattern = samples == 4
                                                         ? MALI_SAMPLE_PATTERN_ORDERED_4X_GRID
                                                         : MALI_SAMPLE_PATTERN_SINGLE_SAMPLED,
                                .tie_break_rule = MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT,
                                .effective_tile_size = tile,
                                .point_sprite_coord_origin_max_y = true,
                                .render_target_count = rr(1, 8),
                                .color_buffer_allocation =
                                        tile * tile * 4 * samples,
                                .tiler = ((uint64_t)rnd() << 32) | rnd(),
                        };
                        struct nord2_framebuffer_info fb = {
                                .width = width, .height = height,
                                .bound_min_x = f.bound_min_x,
                                .bound_min_y = f.bound_min_y,
                                .bound_max_x = width, .bound_max_y = height,
                                .render_target_count = f.render_target_count,
                                .tile_size_px = tile,
                                .tiler_context = f.tiler,
                                .sample_count = samples,
                                .first_provoking_vertex = false,
                        };
                        nord2_desc_framebuffer(out, &fb);
                        MALI_FRAMEBUFFER_PARAMETERS_pack(&p.PARAMETERS, &f);
                        cmp("fb", out, p.opaque, 32);
                }
                /* RGB render target */
                {
                        uint8_t out[NORD2_DESC_RENDER_TARGET_SIZE];
                        struct mali_rgb_render_target_packed p = {0};
                        struct MALI_RGB_RENDER_TARGET r = {
                                .internal_format = MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8,
                                .write_enable = true,
                                .writeback_format = MALI_COLOR_FORMAT_R8G8B8A8,
                                .writeback_block_format = MALI_BLOCK_FORMAT_LINEAR,
                                .dithering_enable = true,
                                .swizzle = rr(0, 0xfff),
                                .writeback_buffer = {
                                        .base = ((uint64_t)rnd() << 32) | rnd(),
                                        .row_stride = al(rr(1, 65536), 64),
                                        .surface_stride = rr(1, 0xffffffff),
                                },
                                .clear = {rnd(), rnd(), rnd(), rnd()},
                        };
                        struct nord2_render_target rt = {
                                .base = r.writeback_buffer.base,
                                .row_stride = r.writeback_buffer.row_stride,
                                .surface_stride = r.writeback_buffer.surface_stride,
                                .internal_format = NORD2_COLOR_INTERNAL_R8G8B8A8,
                                .writeback_format = NORD2_COLOR_R8G8B8A8,
                                .block_format = NORD2_BLOCK_LINEAR,
                                .swizzle = r.swizzle,
                                .write_enable = true,
                                .clear_color = {r.clear.color_0,
                                                r.clear.color_1,
                                                r.clear.color_2,
                                                r.clear.color_3},
                        };
                        nord2_desc_render_target(out, &rt);
                        MALI_RGB_RENDER_TARGET_pack(&p, &r);
                        cmp("rt", out, p.opaque, 16);
                }
                /* Tiler heap */
                {
                        uint8_t out[NORD2_DESC_TILER_HEAP_SIZE];
                        struct mali_tiler_heap_packed p = {0};
                        struct MALI_TILER_HEAP h = {
                                .type = MALI_DESCRIPTOR_TYPE_BUFFER,
                                .buffer_type = MALI_BUFFER_TYPE_TILER_HEAP,
                                .chunk_size = MALI_CHUNK_SIZE_2_MIB,
                                .size = al(rr(4096, 0x200000), 4096),
                                .base = ((uint64_t)rnd() << 32) | rnd(),
                                .bottom = ((uint64_t)rnd() << 32) | rnd(),
                                .top = ((uint64_t)rnd() << 32) | rnd(),
                        };
                        nord2_desc_tiler_heap(out, h.size, h.base, h.bottom,
                                              h.top, NORD2_TILER_CHUNK_2M);
                        MALI_TILER_HEAP_pack(&p, &h);
                        cmp("heap", out, p.opaque, 8);
                }
                /* Texture */
                {
                        uint8_t out[NORD2_DESC_TEXTURE_SIZE];
                        struct mali_texture_packed p = {0};
                        uint32_t w = rr(1, 4096), h = rr(1, 4096);
                        uint32_t levels = rr(1, 8);
                        struct MALI_TEXTURE t = {
                                .type = MALI_DESCRIPTOR_TYPE_TEXTURE,
                                .dimension = MALI_TEXTURE_DIMENSION_2D,
                                .format = NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM,
                                .width = w, .height = h, .swizzle = 0x688,
                                .levels = levels, .sample_count = 1,
                                .surfaces = ((uint64_t)rnd() << 32) | rnd(),
                                .array_size = 1, .depth = 1,
                        };
                        nord2_desc_texture(out, NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM,
                                           w, h, levels, t.surfaces, 0);
                        MALI_TEXTURE_pack(&p, &t);
                        cmp("texture", out, p.opaque, 8);
                }
                /* Attribute */
                {
                        uint8_t out[NORD2_DESC_ATTRIBUTE_SIZE];
                        struct mali_attribute_packed p = {0};
                        struct MALI_ATTRIBUTE a = {
                                .type = MALI_DESCRIPTOR_TYPE_ATTRIBUTE,
                                .attribute_type = MALI_ATTRIBUTE_TYPE_1D,
                                .format = NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM,
                                .offset = (int)(rnd() & 0x7fffffff),
                                .buffer_index = rnd(),
                                .stride = rnd(),
                        };
                        nord2_desc_attribute(out, a.attribute_type, a.format,
                                             a.offset, a.buffer_index,
                                             a.stride);
                        MALI_ATTRIBUTE_pack(&p, &a);
                        cmp("attr", out, p.opaque, 8);
                }
                /* Sampler */
                {
                        uint8_t out[NORD2_DESC_SAMPLER_SIZE];
                        struct mali_sampler_packed p = {0};
                        struct MALI_SAMPLER s = {
                                .type = MALI_DESCRIPTOR_TYPE_SAMPLER,
                                .wrap_mode_r = MALI_WRAP_MODE_CLAMP_TO_EDGE,
                                .wrap_mode_t = MALI_WRAP_MODE_CLAMP_TO_EDGE,
                                .wrap_mode_s = MALI_WRAP_MODE_CLAMP_TO_EDGE,
                                .round_to_nearest_even = true,
                                .seamless_cube_map = true,
                                .normalized_coordinates = true,
                                .clamp_integer_array_indices = true,
                                .mipmap_mode = MALI_MIPMAP_MODE_NEAREST,
                                .maximum_anisotropy = rr(1, 32),
                        };
                        nord2_desc_sampler(out, 9, 9, 9, false, false, false,
                                           s.maximum_anisotropy);
                        MALI_SAMPLER_pack(&p, &s);
                        cmp("sampler", out, p.opaque, 8);
                }
        }
        if (failures) {
                fprintf(stderr, "%d randomized descriptor mismatches\n",
                        failures);
                return 1;
        }
        printf("all randomized descriptor parity checks passed\n");
        return 0;
}
