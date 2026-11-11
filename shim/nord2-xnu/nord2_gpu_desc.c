/*
 * Arch9 (Mali-G77) descriptor packers.  See nord2_gpu_desc.h for provenance.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_gpu_desc.h"

#include "nord2_gpu_compat.h"

static void put32(uint8_t *out, unsigned word, uint32_t value)
{
        memcpy(out + (size_t)word * 4u, &value, sizeof(value));
}

static void put64(uint8_t *out, unsigned word, uint64_t value)
{
        put32(out, word, (uint32_t)value);
        put32(out, word + 1, (uint32_t)(value >> 32));
}

void nord2_desc_job_header(uint8_t *out, enum nord2_job_type type,
                           uint16_t index, uint16_t dependency_1,
                           uint16_t dependency_2, bool barrier,
                           bool suppress_prefetch, uint64_t next)
{
        nord2_desc_zero(out, NORD2_DESC_JOB_HEADER_SIZE);
        /* Exception status / first incomplete task / fault pointer stay zero. */
        put32(out, 4, ((uint32_t)type << 1) | (barrier ? (1u << 8) : 0) |
                              (suppress_prefetch ? (1u << 11) : 0) |
                              ((uint32_t)index << 16));
        put32(out, 5, (uint32_t)dependency_1 |
                              ((uint32_t)dependency_2 << 16));
        put64(out, 6, next);
}

void nord2_desc_shader_environment(uint8_t *out, uint64_t resources,
                                   uint64_t shader, uint64_t thread_storage,
                                   uint64_t fau, uint8_t fau_count,
                                   uint32_t attribute_offset)
{
        nord2_desc_zero(out, NORD2_DESC_SHADER_ENV_SIZE);
        put32(out, 0, attribute_offset);
        put32(out, 1, fau_count);
        put64(out, 8, resources);
        put64(out, 10, shader);
        put64(out, 12, thread_storage);
        put64(out, 14, fau);
}

void nord2_desc_compute_job(uint8_t *out, enum nord2_job_type type,
                            uint16_t index, uint16_t dep1, uint16_t dep2,
                            bool barrier, uint64_t next,
                            uint32_t workgroup_size[3],
                            uint32_t workgroup_count[3],
                            bool allow_merging,
                            uint64_t resources, uint64_t shader,
                            uint64_t thread_storage, uint64_t fau,
                            uint8_t fau_count, uint32_t attribute_offset)
{
        nord2_desc_zero(out, NORD2_DESC_COMPUTE_JOB_SIZE);
        nord2_desc_job_header(out, type, index, dep1, dep2, barrier, false,
                              next);

        /* Compute Payload starts at byte 32 (word 8). */
        put32(out, 8, ((workgroup_size[0] - 1u) & 0x3ffu) |
                              (((workgroup_size[1] - 1u) & 0x3ffu) << 10) |
                              (((workgroup_size[2] - 1u) & 0x3ffu) << 20) |
                              (allow_merging ? (1u << 31) : 0));
        put32(out, 9, 1u | (2u << 14)); /* task increment 1, task axis Z */
        put32(out, 10, workgroup_count[0]);
        put32(out, 11, workgroup_count[1]);
        put32(out, 12, workgroup_count[2]);
        /* Offsets X/Y/Z at words 13..15 stay zero. */
        nord2_desc_shader_environment(out + 32u + 8u * 4u, resources, shader,
                                      thread_storage, fau, fau_count,
                                      attribute_offset);
}

void nord2_desc_fragment_job(uint8_t *out, uint16_t index, uint16_t dep1,
                             uint16_t dep2, bool barrier, uint64_t next,
                             uint32_t bound_min_x, uint32_t bound_min_y,
                             uint32_t bound_max_x, uint32_t bound_max_y,
                             uint64_t framebuffer)
{
        nord2_desc_zero(out, NORD2_DESC_FRAGMENT_JOB_SIZE);
        nord2_desc_job_header(out, NORD2_JOB_FRAGMENT, index, dep1, dep2,
                              barrier, false, next);

        /* Fragment Job Payload at byte 32 (word 8). */
        put32(out, 8, (bound_min_x & 0xfffu) |
                              ((bound_min_y & 0xfffu) << 16));
        put32(out, 9, (bound_max_x & 0xfffu) |
                              ((bound_max_y & 0xfffu) << 16));
        put64(out, 10, framebuffer);
        /* No tile enable map. */
}

void nord2_desc_framebuffer(uint8_t *out,
                            const struct nord2_framebuffer_info *fb)
{
        unsigned tile_log2 = 0;
        uint32_t alloc_bytes;

        nord2_desc_zero(out, NORD2_DESC_FRAMEBUFFER_SIZE);
        for (uint32_t t = fb->tile_size_px; t > 1; t >>= 1)
                tile_log2++;

        /* Internal layer index and frame argument stay zero for the first
         * single-layer pass. */
        put32(out, 8, (fb->width - 1u) | ((fb->height - 1u) << 16));
        put32(out, 9, (fb->bound_min_x & 0xffffu) |
                              ((fb->bound_min_y & 0xffffu) << 16));
        put32(out, 10, (fb->bound_max_x & 0xffffu) |
                               ((fb->bound_max_y & 0xffffu) << 16));

        alloc_bytes = fb->tile_size_px * fb->tile_size_px * 4u *
                      (fb->sample_count ? fb->sample_count : 1u);
        put32(out, 11,
              /* sample count is log2 encoded */
              (((fb->sample_count == 4) ? 2u : 0u) & 0x7u) |
                      ((fb->sample_count == 4 ? 1u : 0u) << 3) |
                      (2u << 6) /* tie-break: minus 180 in 0 out */ |
                      ((tile_log2 & 0xfU) << 9) |
                      (fb->first_provoking_vertex ? (1u << 14) : 0u) |
                      (1u << 15) /* point sprite coord origin max Y */ |
                      (((fb->render_target_count - 1u) & 0xfU) << 19) |
                      (((alloc_bytes >> 10) & 0xffU) << 24));
        /* Color-only pass: no stencil/depth enables, no CRC extension. */
        put64(out, 14, fb->tiler_context);
}

void nord2_desc_render_target(uint8_t *out,
                              const struct nord2_render_target *rt)
{
        uint32_t w0, w1;

        nord2_desc_zero(out, NORD2_DESC_RENDER_TARGET_SIZE);
        w0 = ((uint32_t)rt->internal_format & 0x3fu) << 26;
        /* Internal buffer offset is bytes >> 4 in bits 4..15. */
        w0 |= 0u;
        w1 = (rt->write_enable ? 1u : 0u) |
             (((uint32_t)rt->writeback_format & 0x1fu) << 3) |
             (((uint32_t)rt->block_format & 0xfu) << 8) |
             (0u << 12) /* MSAA single */ |
             (rt->srgb ? (1u << 14) : 0u) |
             (1u << 15) /* dithering enable */ |
             ((rt->swizzle & 0x0fffu) << 16);
        put32(out, 0, w0);
        put32(out, 1, w1);
        put64(out, 8, rt->base);
        put32(out, 10, rt->row_stride);
        put32(out, 11, rt->surface_stride);
        put32(out, 12, rt->clear_color[0]);
        put32(out, 13, rt->clear_color[1]);
        put32(out, 14, rt->clear_color[2]);
        put32(out, 15, rt->clear_color[3]);
}

void nord2_desc_tiler_heap(uint8_t *out, uint32_t size, uint64_t base,
                           uint64_t bottom, uint64_t top,
                           enum nord2_tiler_chunk_size chunk)
{
        nord2_desc_zero(out, NORD2_DESC_TILER_HEAP_SIZE);
        put32(out, 0, NORD2_DESC_TYPE_BUFFER |
                              ((uint32_t)NORD2_BUFFER_TILER_HEAP << 4) |
                              (((uint32_t)chunk & 0x3u) << 8));
        put32(out, 1, size & ~0xfffu);
        put64(out, 2, base);
        put64(out, 4, bottom);
        put64(out, 6, top);
}

void nord2_desc_tiler_context(uint8_t *out, uint64_t polygon_list,
                              uint32_t width, uint32_t height,
                              uint64_t heap, uint32_t hierarchy_mask,
                              uint32_t sample_pattern,
                              bool first_provoking_vertex)
{
        nord2_desc_zero(out, NORD2_DESC_TILER_CONTEXT_SIZE);
        put64(out, 0, polygon_list);
        put32(out, 2, (hierarchy_mask & 0x1fffu) |
                              ((sample_pattern & 0x7u) << 13) |
                              (first_provoking_vertex ? (1u << 18) : 0u));
        put32(out, 3, (width - 1u) | ((height - 1u) << 16));
        put32(out, 4, 0u); /* layer count 1, layer offset 0 */
        put64(out, 6, heap);
        /* Weights (words 8..15) and the 16-word opaque tiler State stay zero;
         * pan_select_tiler_hierarchy_mask only needs the mask and heap size. */
}

/* Pack the DCD flags shared by DRAW. */
static void draw_flags(uint8_t *out, uint32_t flags_0, uint32_t flags_1)
{
        put32(out, 0, flags_0);
        put32(out, 1, flags_1);
}

void nord2_desc_draw(uint8_t *out, const struct nord2_draw_state *draw,
                     uint64_t resources, uint64_t shader,
                     uint64_t thread_storage, uint64_t fau,
                     uint8_t fau_count, uint32_t attribute_offset)
{
        nord2_desc_zero(out, NORD2_DESC_DRAW_SIZE);
        draw_flags(out, draw->flags_0, draw->flags_1);
        put64(out, 2, (draw->vertex_packet ? 1ull : 0ull) | (draw->vertex_array & ~63ull));
        put32(out, 4, (draw->vertex_packet_stride & 0xffffu) |
                              ((draw->vertex_attribute_stride & 0xffffu)
                               << 16));
        memcpy(out + 6u * 4u, &draw->minimum_z, sizeof(float));
        memcpy(out + 7u * 4u, &draw->maximum_z, sizeof(float));
        put64(out, 10, draw->depth_stencil);
        put64(out, 12, (draw->blend_count & 0xfu) | (draw->blend & ~15ull));
        put64(out, 14, draw->occlusion);
        nord2_desc_shader_environment(out + 64u, resources, shader,
                                      thread_storage, fau, fau_count,
                                      attribute_offset);
}

void nord2_desc_malloc_vertex_job(uint8_t *out, uint16_t index,
                                  uint16_t dep1, uint16_t dep2,
                                  uint64_t next,
                                  enum nord2_draw_mode mode,
                                  enum nord2_index_type index_type,
                                  uint32_t index_count, int32_t base_vertex,
                                  uint32_t instance_count,
                                  uint32_t vertex_packet_stride,
                                  uint32_t vertex_attribute_stride,
                                  uint64_t tiler_context,
                                  uint32_t scissor_min_x,
                                  uint32_t scissor_min_y,
                                  uint32_t scissor_max_x,
                                  uint32_t scissor_max_y,
                                  uint64_t indices,
                                  uint32_t flags_0, uint32_t flags_1,
                                  const struct nord2_draw_state *draw,
                                  uint64_t position_resources,
                                  uint64_t position_shader,
                                  uint64_t position_tls,
                                  uint64_t position_fau,
                                  uint8_t position_fau_count,
                                  uint32_t position_attribute_offset,
                                  uint64_t varying_resources,
                                  uint64_t varying_shader,
                                  uint64_t varying_tls,
                                  uint64_t varying_fau,
                                  uint8_t varying_fau_count,
                                  uint32_t varying_attribute_offset,
                                  uint64_t fragment_resources,
                                  uint64_t fragment_shader,
                                  uint64_t fragment_tls,
                                  uint64_t fragment_fau,
                                  uint8_t fragment_fau_count,
                                  uint32_t fragment_attribute_offset)
{
        nord2_desc_zero(out, NORD2_DESC_MALLOC_VERTEX_JOB_SIZE);
        nord2_desc_job_header(out, NORD2_JOB_MALLOC_VERTEX, index, dep1, dep2,
                              false, false, next);

        /* Primitive at 32. */
        put32(out + 32u, 0,
              ((uint32_t)mode & 0xffu) |
                      (((uint32_t)index_type & 0x7u) << 8) |
                      (1u << 15) /* allow rotating primitives */ |
                      (1u << 16) /* low depth cull */ |
                      (1u << 17) /* high depth cull */);
        put32(out + 32u, 1, (uint32_t)base_vertex);
        put32(out + 32u, 2, 0u); /* instance offset */
        put32(out + 32u, 3, index_count);
        /* Instance count at 48. */
        put32(out + 48u, 0, instance_count);
        /* Allocation at 52. */
        put32(out + 52u, 0, (vertex_packet_stride & 0xffffu) |
                                    ((vertex_attribute_stride & 0xffffu)
                                     << 16));
        /* Tiler pointer at 56. */
        put64(out + 56u, 0, tiler_context);
        /* Scissor at 104. */
        put32(out + 104u, 0, (scissor_min_x & 0xffffu) |
                                    ((scissor_min_y & 0xffffu) << 16));
        put32(out + 108u, 0, (scissor_max_x & 0xffffu) |
                                    ((scissor_max_y & 0xffffu) << 16));
        /* Primitive size at 112: zero means fixed-size 0. */
        /* Indices at 120. */
        put64(out + 120u, 0, indices);
        /* Draw at 128. */
        draw_flags(out + 128u, flags_0, flags_1);
        put64(out + 128u, 2, (draw->vertex_packet ? 1ull : 0ull) | (draw->vertex_array & ~63ull));
        put32(out + 128u, 4, (draw->vertex_packet_stride & 0xffffu) |
                                    ((draw->vertex_attribute_stride & 0xffffu)
                                     << 16));
        memcpy(out + 128u + 6u * 4u, &draw->minimum_z, sizeof(float));
        memcpy(out + 128u + 7u * 4u, &draw->maximum_z, sizeof(float));
        put64(out + 128u + 10u * 4u, 0, draw->depth_stencil);
        put64(out + 128u, 12, (draw->blend_count & 0xfu) | (draw->blend & ~15ull));
        put64(out + 128u + 14u * 4u, 0, draw->occlusion);
        nord2_desc_shader_environment(out + 128u + 16u * 4u,
                                      fragment_resources, fragment_shader,
                                      fragment_tls, fragment_fau,
                                      fragment_fau_count,
                                      fragment_attribute_offset);
        /* Position section is at 256; the generic draw's shader env is unused
         * in the IDVS layout but keep it consistent for decoders. */
        nord2_desc_shader_environment(out + 256u, position_resources,
                                      position_shader, position_tls,
                                      position_fau, position_fau_count,
                                      position_attribute_offset);
        nord2_desc_shader_environment(out + 320u, varying_resources,
                                      varying_shader, varying_tls, varying_fau,
                                      varying_fau_count,
                                      varying_attribute_offset);
}

void nord2_desc_buffer(uint8_t *out, uint64_t address, uint32_t size,
                       enum nord2_buffer_type type)
{
        nord2_desc_zero(out, NORD2_DESC_BUFFER_SIZE);
        put32(out, 0, NORD2_DESC_TYPE_BUFFER |
                              (((uint32_t)type & 0xfu) << 4));
        put32(out, 1, size);
        put64(out, 2, address);
}

void nord2_desc_resource(uint8_t *out, uint64_t address, uint32_t size,
                         bool contains_descriptors)
{
        nord2_desc_zero(out, NORD2_DESC_RESOURCE_SIZE);
        /* Resource table entry: 56-bit address, contains-descriptors at bit24
         * of the second word, 64-bit size at words 2..3. */
        put32(out, 0, (uint32_t)address);
        put32(out, 1, ((uint32_t)(address >> 32) & 0x00ffffffu) |
                              (contains_descriptors ? (1u << 24) : 0u));
        put64(out, 2, size);
}

void nord2_desc_texture(uint8_t *out, enum nord2_pixel_format format,
                        uint32_t width, uint32_t height, uint32_t levels,
                        uint64_t surfaces, uint32_t row_stride)
{
        (void)row_stride; /* stored in the Generic Plane descriptor */
        nord2_desc_zero(out, NORD2_DESC_TEXTURE_SIZE);
        put32(out, 0, NORD2_DESC_TYPE_TEXTURE |
                              (2u << 4) /* 2D */ |
                              (((uint32_t)format & 0x3fffffu) << 10));
        put32(out, 1, ((width - 1u) & 0xffffu) |
                              (((height - 1u) & 0xffffu) << 16));
        put32(out, 2, 0x688u /* component order R,G,B,A */ |
                              (((levels - 1u) & 0x1fu) << 16));
        put32(out, 3, 0u); /* maximum LOD is a separate sampler-side value */
        put64(out, 4, surfaces);
        /* array size 0 (one layer), depth 0 (one slice) */
}

void nord2_desc_plane(uint8_t *out, uint64_t base, uint32_t size,
                      uint32_t row_stride, uint32_t slice_stride)
{
        nord2_desc_zero(out, NORD2_DESC_RESOURCE_SIZE);
        put32(out, 0, NORD2_DESC_TYPE_PLANE | (1u << 4) |
                              (2u << 8) /* clump ordering: linear */ |
                              (124u << 24) /* clump format R8G8B8G8 */);
        put32(out, 1, size & ~0xfffu);
        put64(out, 2, base);
        put32(out, 4, row_stride);
        put32(out, 6, slice_stride);
}

void nord2_desc_sampler(uint8_t *out,
                        uint32_t wrap_r, uint32_t wrap_t, uint32_t wrap_s,
                        bool min_nearest, bool mag_nearest,
                        bool mip_linear, uint32_t max_anisotropy)
{
        nord2_desc_zero(out, NORD2_DESC_SAMPLER_SIZE);
        put32(out, 0, NORD2_DESC_TYPE_SAMPLER |
                              ((wrap_r & 0xfu) << 8) |
                              ((wrap_t & 0xfu) << 12) |
                              ((wrap_s & 0xfu) << 16) |
                              (1u << 21) /* round to nearest even */ |
                              (1u << 23) /* seamless cube map */ |
                              (1u << 25) /* normalized coordinates */ |
                              (1u << 26) /* clamp integer array indices */ |
                              (min_nearest ? (1u << 27) : 0u) |
                              (mag_nearest ? (1u << 28) : 0u) |
                              ((mip_linear ? 2u : 0u) << 30));
        put32(out, 2, ((max_anisotropy - 1u) & 0x1fu) << 16);
}

void nord2_desc_attribute(uint8_t *out, uint32_t attribute_type,
                          uint32_t format, uint32_t offset,
                          uint32_t buffer_index, uint32_t stride)
{
        nord2_desc_zero(out, NORD2_DESC_ATTRIBUTE_SIZE);
        put32(out, 0, NORD2_DESC_TYPE_ATTRIBUTE |
                              ((attribute_type & 0xfu) << 4) |
                              ((format & 0x3fffffu) << 10));
        put32(out, 2, offset);
        put32(out, 3, buffer_index);
        put32(out, 4, stride);
}

void nord2_desc_shader_program(uint8_t *out, enum nord2_shader_stage stage,
                               uint64_t binary, uint32_t preload)
{
        nord2_desc_zero(out, NORD2_DESC_SHADER_PROGRAM_SIZE);
        put32(out, 0, NORD2_DESC_TYPE_SHADER |
                              (((uint32_t)stage & 0xfu) << 4));
        put32(out, 1, preload & 0xffffu);
        put64(out, 2, binary);
}

void nord2_desc_local_storage(uint8_t *out, uint32_t tls_size,
                              uint64_t tls_base, uint32_t wls_size,
                              uint64_t wls_base)
{
        nord2_desc_zero(out, NORD2_DESC_LOCAL_STORAGE_SIZE);
        put32(out, 0, tls_size & 0x1fu);
        /* No workgroup memory: 31 WLS instances (log2 marker) and the
         * Mesa default size base of one. */
        put32(out, 1, wls_size ? (wls_size & 0x1fu)
                               : (0x1fu | (1u << 5)));
        put32(out, 2, (uint32_t)(tls_base & 0xffffffffffffULL));
        put32(out, 3, (uint32_t)((tls_base >> 32) & 0xffffULL) |
                              (1u << 28) /* address mode: base + offset */);
        put64(out, 4, wls_base);
}

void nord2_desc_blend(uint8_t *out, bool load_destination,
                      uint32_t blend_constant, uint32_t equation_rgb,
                      uint32_t equation_alpha, uint32_t color_mask,
                      uint32_t num_components, uint32_t render_target,
                      uint32_t conversion_format, bool conversion_raw,
                      bool enable)
{
        nord2_desc_zero(out, NORD2_DESC_BLEND_SIZE);
        put32(out, 0, (load_destination ? 1u : 0u) |
                              (enable ? (1u << 9) : 0u) |
                              ((blend_constant & 0xffffu) << 16));
        put32(out, 1, (equation_rgb & 0xfffu) |
                              ((equation_alpha & 0xfffu) << 12) |
                              ((color_mask & 0xfu) << 28));
        /* Internal Blend, Fixed-Function mode (2).  Num Comps is minus(1),
         * RT is 4 bits; the conversion word follows. */
        put32(out, 2, 2u | (((num_components - 1u) & 0x3u) << 3) |
                              ((render_target & 0xfu) << 16));
        put32(out, 3, (conversion_format & 0x3fffffu) |
                              (conversion_raw ? (1u << 22) : 0u));
}

void nord2_desc_depth_stencil(uint8_t *out, uint32_t base_flags,
                              uint32_t masks, uint32_t functions,
                              uint32_t depth_flags, float units,
                              float factor, float clamp)
{
        nord2_desc_zero(out, NORD2_DESC_DEPTH_STENCIL_SIZE);
        put32(out, 0, base_flags);
        put32(out, 1, masks);
        put32(out, 2, functions);
        put32(out, 4, depth_flags);
        memcpy(out + 5u * 4u, &units, sizeof(units));
        memcpy(out + 6u * 4u, &factor, sizeof(factor));
        memcpy(out + 7u * 4u, &clamp, sizeof(clamp));
}
