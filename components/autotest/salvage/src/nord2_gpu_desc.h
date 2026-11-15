/*
 * Nord 2 Mali-G77 (Valhall arch9) job-manager descriptor packers.
 *
 * The layouts below are transcribed field-for-field from the pinned Mesa
 * genxml v9.xml (commit 81daea66703eeea1a232446e3a36902353fc3045).  A
 * descriptor is a byte string, not a C struct: every setter writes explicit
 * 32-bit words so that the result is independent of compiler bitfield layout.
 *
 * Descriptor sizes are the Mesa "packed" sizes in bytes:
 *   job header              128
 *   compute job             128
 *   fragment job            128
 *   malloc vertex job       384
 *   tiler heap               32
 *   tiler context           192
 *   framebuffer             128
 *   RGB render target        64
 *   shader environment       64
 *   resource (buffer)        32
 *   texture                  32
 *   sampler                  32
 *   attribute                32
 *   shader program           32
 *   local storage            32
 *   blend                    16
 *   depth/stencil            32
 *   draw (inside job)       128
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_GPU_DESC_H
#define NORD2_GPU_DESC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORD2_DESC_JOB_HEADER_SIZE        128u
#define NORD2_DESC_COMPUTE_JOB_SIZE       128u
#define NORD2_DESC_FRAGMENT_JOB_SIZE      128u
#define NORD2_DESC_MALLOC_VERTEX_JOB_SIZE 384u
#define NORD2_DESC_TILER_HEAP_SIZE         32u
#define NORD2_DESC_TILER_CONTEXT_SIZE     192u
#define NORD2_DESC_FRAMEBUFFER_SIZE       128u
#define NORD2_DESC_RENDER_TARGET_SIZE      64u
#define NORD2_DESC_SHADER_ENV_SIZE         64u
#define NORD2_DESC_BUFFER_SIZE             32u
#define NORD2_DESC_RESOURCE_SIZE           16u
#define NORD2_DESC_TEXTURE_SIZE            32u
#define NORD2_DESC_SAMPLER_SIZE            32u
#define NORD2_DESC_ATTRIBUTE_SIZE          32u
#define NORD2_DESC_SHADER_PROGRAM_SIZE     32u
#define NORD2_DESC_LOCAL_STORAGE_SIZE      32u
#define NORD2_DESC_BLEND_SIZE              16u
#define NORD2_DESC_DEPTH_STENCIL_SIZE      32u
#define NORD2_DESC_DRAW_SIZE              128u

/* Alignment requirements from v9.xml (aggregate/job descriptors). */
#define NORD2_DESC_JOB_ALIGN               128u
#define NORD2_DESC_TILER_CONTEXT_ALIGN      64u
#define NORD2_DESC_FRAMEBUFFER_ALIGN        64u
#define NORD2_DESC_RENDER_TARGET_ALIGN      64u
#define NORD2_DESC_SHADER_ENV_ALIGN         64u
#define NORD2_DESC_RESOURCE_ALIGN           64u
#define NORD2_DESC_DESCRIPTOR_ALIGN         32u

/* enum Descriptor Type */
enum nord2_desc_type {
        NORD2_DESC_TYPE_NULL = 0,
        NORD2_DESC_TYPE_SAMPLER = 1,
        NORD2_DESC_TYPE_TEXTURE = 2,
        NORD2_DESC_TYPE_ATTRIBUTE = 5,
        NORD2_DESC_TYPE_DEPTH_STENCIL = 7,
        NORD2_DESC_TYPE_SHADER = 8,
        NORD2_DESC_TYPE_BUFFER = 9,
        NORD2_DESC_TYPE_PLANE = 10,
};

/* enum Job Type */
enum nord2_job_type {
        NORD2_JOB_NOT_STARTED = 0,
        NORD2_JOB_NULL = 1,
        NORD2_JOB_WRITE_VALUE = 2,
        NORD2_JOB_CACHE_FLUSH = 3,
        NORD2_JOB_COMPUTE = 4,
        NORD2_JOB_TILER = 7,
        NORD2_JOB_FRAGMENT = 9,
        NORD2_JOB_INDEXED_VERTEX = 10,
        NORD2_JOB_MALLOC_VERTEX = 11,
};

/* enum Shader stage */
enum nord2_shader_stage {
        NORD2_STAGE_COMPUTE = 1,
        NORD2_STAGE_FRAGMENT = 2,
        NORD2_STAGE_VERTEX = 3,
};

/* enum Buffer Type */
enum nord2_buffer_type {
        NORD2_BUFFER_SIMPLE = 1,
        NORD2_BUFFER_TILER_HEAP = 2,
        NORD2_BUFFER_STRUCTURE = 3,
        NORD2_BUFFER_VERTEX_PACKET = 4,
};

/* enum Draw Mode (subset) */
enum nord2_draw_mode {
        NORD2_DRAW_NONE = 0,
        NORD2_DRAW_POINTS = 1,
        NORD2_DRAW_LINES = 2,
        NORD2_DRAW_LINE_STRIP = 4,
        NORD2_DRAW_TRIANGLES = 8,
        NORD2_DRAW_TRIANGLE_STRIP = 10,
        NORD2_DRAW_TRIANGLE_FAN = 12,
};

/* enum Index Type */
enum nord2_index_type {
        NORD2_INDEX_NONE = 0,
        NORD2_INDEX_U8 = 1,
        NORD2_INDEX_U16 = 2,
        NORD2_INDEX_U32 = 3,
};

/* enum Pixel Format: the v9 format word is (channels in low nibbles, type in
 * 8..15, swizzle in 16..).  Only the formats needed by the initial linear
 * BGRA8 and R8 paths are defined here; unknown formats must be refused by the
 * caller rather than guessed. */
enum nord2_pixel_format {
        NORD2_PIXEL_FORMAT_NONE = 0,
        /* MALI_PACK_FMT: component order | (MALI_RGBA8_UNORM=187 << 12).
         * Component order RGBA=0, BGRA=4, RGB1=16 (used for R001). */
        NORD2_PIXEL_FORMAT_B8G8R8A8_UNORM = 0x000bb004u,
        NORD2_PIXEL_FORMAT_R8G8B8A8_UNORM = 0x000bb000u,
        NORD2_PIXEL_FORMAT_R8_UNORM = 0x000a3010u,
        NORD2_PIXEL_FORMAT_R32_FLOAT = 0x000a7010u,
};

/* enum Color Buffer Internal Format */
enum nord2_color_internal_format {
        NORD2_COLOR_INTERNAL_RAW = 0,
        NORD2_COLOR_INTERNAL_R8G8B8A8 = 1,
        NORD2_COLOR_INTERNAL_R10G10B10A2 = 2,
        NORD2_COLOR_INTERNAL_R8G8B8A2 = 3,
        NORD2_COLOR_INTERNAL_R4G4B4A4 = 4,
        NORD2_COLOR_INTERNAL_R5G6B5A0 = 5,
        NORD2_COLOR_INTERNAL_R5G5B5A1 = 6,
        NORD2_COLOR_INTERNAL_R16F = 16,
        NORD2_COLOR_INTERNAL_RG16F = 17,
        NORD2_COLOR_INTERNAL_RGB16F = 18,
        NORD2_COLOR_INTERNAL_RGBA16F = 19,
};

/* enum Color Format (writeback) */
enum nord2_color_format {
        NORD2_COLOR_RAW8 = 0,
        NORD2_COLOR_RAW16 = 1,
        NORD2_COLOR_RAW32 = 3,
        NORD2_COLOR_R8 = 16,
        NORD2_COLOR_R8G8 = 17,
        NORD2_COLOR_R8G8B8 = 18,
        NORD2_COLOR_R8G8B8A8 = 19,
        NORD2_COLOR_R10G10B10A2 = 24,
        NORD2_COLOR_A2B10G10R10 = 25,
        NORD2_COLOR_NATIVE = 31,
};

/* enum Block Format */
enum nord2_block_format {
        NORD2_BLOCK_NONE = 0,
        NORD2_BLOCK_TILED_U_INTERLEAVED = 1,
        NORD2_BLOCK_LINEAR = 2,
        NORD2_BLOCK_AFBC = 12,
        NORD2_BLOCK_AFBC_TILED = 13,
};

/* enum Chunk Size for the tiler heap. */
enum nord2_tiler_chunk_size {
        NORD2_TILER_CHUNK_256K = 0,
        NORD2_TILER_CHUNK_512K = 1,
        NORD2_TILER_CHUNK_1M = 2,
        NORD2_TILER_CHUNK_2M = 3,
};

/* A render target description in the form the arch9 packer needs. */
struct nord2_render_target {
        uint64_t base;              /* GPU VA of the linear surface */
        uint32_t row_stride;        /* bytes between rows */
        uint32_t surface_stride;    /* bytes between slices */
        uint32_t width;
        uint32_t height;
        enum nord2_color_internal_format internal_format;
        enum nord2_color_format writeback_format;
        enum nord2_block_format block_format;
        uint32_t swizzle;           /* Component Swizzle, 4 nibbles */
        bool srgb;
        bool write_enable;
        uint32_t clear_color[4];    /* packed RT Clear words */
};

struct nord2_framebuffer_info {
        uint32_t width;
        uint32_t height;
        uint32_t bound_min_x, bound_min_y;
        uint32_t bound_max_x, bound_max_y;
        uint32_t render_target_count;   /* 1..8 */
        uint32_t tile_size_px;          /* power of two */
        uint64_t tiler_context;         /* GPU VA of a Tiler Context */
        uint32_t sample_count;          /* 1 or 4 */
        bool first_provoking_vertex;
};

struct nord2_draw_state {
        uint64_t vertex_array;      /* GPU VA of the vertex array descriptor or 0 */
        bool vertex_packet;
        uint32_t vertex_packet_stride;
        uint32_t vertex_attribute_stride;
        float minimum_z;
        float maximum_z;
        uint64_t depth_stencil;     /* GPU VA of depth/stencil descriptor */
        uint64_t blend;             /* GPU VA of blend descriptor array */
        uint32_t blend_count;
        uint64_t occlusion;         /* GPU VA of occlusion query buffer */
        uint32_t flags_0;
        uint32_t flags_1;
};

/* --- primitive writers -------------------------------------------------- */

void nord2_desc_job_header(uint8_t *out, enum nord2_job_type type,
                           uint16_t index, uint16_t dependency_1,
                           uint16_t dependency_2, bool barrier,
                           bool suppress_prefetch, uint64_t next);
void nord2_desc_shader_environment(uint8_t *out, uint64_t resources,
                                   uint64_t shader, uint64_t thread_storage,
                                   uint64_t fau, uint8_t fau_count,
                                   uint32_t attribute_offset);
void nord2_desc_compute_job(uint8_t *out, enum nord2_job_type type,
                            uint16_t index, uint16_t dep1, uint16_t dep2,
                            bool barrier, uint64_t next,
                            uint32_t workgroup_size[3],
                            uint32_t workgroup_count[3],
                            bool allow_merging,
                            uint64_t resources, uint64_t shader,
                            uint64_t thread_storage, uint64_t fau,
                            uint8_t fau_count, uint32_t attribute_offset);
void nord2_desc_fragment_job(uint8_t *out, uint16_t index, uint16_t dep1,
                             uint16_t dep2, bool barrier, uint64_t next,
                             uint32_t bound_min_x, uint32_t bound_min_y,
                             uint32_t bound_max_x, uint32_t bound_max_y,
                             uint64_t framebuffer);
void nord2_desc_framebuffer(uint8_t *out,
                            const struct nord2_framebuffer_info *fb);
void nord2_desc_render_target(uint8_t *out,
                              const struct nord2_render_target *rt);
void nord2_desc_tiler_heap(uint8_t *out, uint32_t size, uint64_t base,
                           uint64_t bottom, uint64_t top,
                           enum nord2_tiler_chunk_size chunk);
void nord2_desc_tiler_context(uint8_t *out, uint64_t polygon_list,
                              uint32_t width, uint32_t height,
                              uint64_t heap, uint32_t hierarchy_mask,
                              uint32_t sample_pattern,
                              bool first_provoking_vertex);
void nord2_desc_draw(uint8_t *out, const struct nord2_draw_state *draw,
                     uint64_t resources, uint64_t shader,
                     uint64_t thread_storage, uint64_t fau,
                     uint8_t fau_count, uint32_t attribute_offset);
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
                                  uint32_t fragment_attribute_offset);
void nord2_desc_buffer(uint8_t *out, uint64_t address, uint32_t size,
                       enum nord2_buffer_type type);
void nord2_desc_resource(uint8_t *out, uint64_t address, uint32_t size,
                         bool contains_descriptors);
void nord2_desc_plane(uint8_t *out, uint64_t base, uint32_t size,
                      uint32_t row_stride, uint32_t slice_stride);
void nord2_desc_texture(uint8_t *out, enum nord2_pixel_format format,
                        uint32_t width, uint32_t height, uint32_t levels,
                        uint64_t surfaces, uint32_t row_stride);
void nord2_desc_sampler(uint8_t *out,
                        uint32_t wrap_r, uint32_t wrap_t, uint32_t wrap_s,
                        bool min_nearest, bool mag_nearest,
                        bool mip_linear, uint32_t max_anisotropy);
void nord2_desc_attribute(uint8_t *out, uint32_t attribute_type,
                          uint32_t format, uint32_t offset,
                          uint32_t buffer_index, uint32_t stride);
void nord2_desc_shader_program(uint8_t *out, enum nord2_shader_stage stage,
                               uint64_t binary, uint32_t preload);
void nord2_desc_local_storage(uint8_t *out, uint32_t tls_size,
                              uint64_t tls_base, uint32_t wls_size,
                              uint64_t wls_base);
void nord2_desc_blend(uint8_t *out, bool load_destination,
                      uint32_t blend_constant, uint32_t equation_rgb,
                      uint32_t equation_alpha, uint32_t color_mask,
                      uint32_t num_components, uint32_t render_target,
                      uint32_t conversion_format, bool conversion_raw,
                      bool enable);
void nord2_desc_depth_stencil(uint8_t *out, uint32_t base_flags,
                              uint32_t masks, uint32_t functions,
                              uint32_t depth_flags, float units,
                              float factor, float clamp);

/* Small helpers shared by the kernel driver and host tests. */
static inline void nord2_desc_zero(uint8_t *out, size_t size)
{
        for (size_t i = 0; i < size; i++)
                out[i] = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* NORD2_GPU_DESC_H */
