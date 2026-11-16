/*
 * Host integration test: compose the arch9 render descriptors and both JM
 * chains for a full-screen target.  No GPU execution.
 */
#include "nord2_gpu_desc.h"
#include "nord2_gpu_jm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, m)                                                            \
        do {                                                                    \
                if (!(c)) {                                                     \
                        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, \
                                m);                                             \
                        failures++;                                             \
                }                                                               \
        } while (0)

static uint32_t w(const uint8_t *p, unsigned i)
{
        uint32_t v;
        memcpy(&v, p + i * 4, 4);
        return v;
}

int main(void)
{
        uint8_t heap[NORD2_DESC_TILER_HEAP_SIZE];
        uint8_t tctx[NORD2_DESC_TILER_CONTEXT_SIZE];
        uint8_t fb[NORD2_DESC_FRAMEBUFFER_SIZE];
        uint8_t rt[NORD2_DESC_RENDER_TARGET_SIZE];
        uint8_t vs[NORD2_GPU_VERTEX_CHAIN_BYTES];
        uint8_t fs[NORD2_GPU_FRAGMENT_CHAIN_BYTES];

        const uint32_t width = 1125, height = 2436, stride = width * 4;
        const uint64_t target = 0x10000000;
        const uint64_t heap_va = 0x20000000;
        const uint64_t tctx_va = 0x21000000;
        const uint64_t fb_va = 0x22000000;

        nord2_desc_tiler_heap(heap, 0x100000, heap_va, heap_va,
                              heap_va + 0x100000, NORD2_TILER_CHUNK_1M);
        CHECK(w(heap, 0) == (NORD2_DESC_TYPE_BUFFER |
                             (NORD2_BUFFER_TILER_HEAP << 4) |
                             (NORD2_TILER_CHUNK_1M << 8)),
              "heap type");
        CHECK(w(heap, 1) == 0x100000 && w(heap, 2) == (uint32_t)heap_va,
              "heap range");

        nord2_desc_tiler_context(tctx, 0, width, height, heap_va, 0x155,
                                 NORD2_DESC_TYPE_NULL /* single */, false);
        CHECK(w(tctx, 3) == ((width - 1) | ((height - 1) << 16)), "tctx size");
        CHECK(w(tctx, 4) == 0, "tctx layer");
        CHECK(w(tctx, 6) == (uint32_t)heap_va, "tctx heap");

        struct nord2_render_target r = {
                .base = target,
                .row_stride = stride,
                .surface_stride = stride * height,
                .width = width,
                .height = height,
                .internal_format = NORD2_COLOR_INTERNAL_R8G8B8A8,
                .writeback_format = NORD2_COLOR_R8G8B8A8,
                .block_format = NORD2_BLOCK_LINEAR,
                .swizzle = 0x60a,
                .write_enable = true,
                .clear_color = {0xff0000ffu, 0, 0, 0},
        };
        nord2_desc_render_target(rt, &r);
        CHECK(w(rt, 8) == (uint32_t)target && w(rt, 10) == stride,
              "rt base/stride");
        CHECK(w(rt, 11) == stride * height, "rt surface stride");
        CHECK(w(rt, 12) == 0xff0000ffu, "rt clear");

        struct nord2_framebuffer_info f = {
                .width = width,
                .height = height,
                .bound_min_x = 0,
                .bound_min_y = 0,
                .bound_max_x = width,
                .bound_max_y = height,
                .render_target_count = 1,
                .tile_size_px = 16,
                .tiler_context = tctx_va,
                .sample_count = 1,
        };
        nord2_desc_framebuffer(fb, &f);
        CHECK(w(fb, 8) == ((width - 1) | ((height - 1) << 16)), "fb size");
        CHECK(w(fb, 11) == (2u << 6 /* tie-break */) | (4u << 9) |
                                   (1u << 15) /* point sprite max Y */ |
                                   (1u << 24), /* 1 KiB tile alloc */
              "fb flags");
        CHECK(w(fb, 14) == (uint32_t)tctx_va, "fb tiler");

        struct nord2_gpu_vertex_chain v = {
                .mode = NORD2_DRAW_TRIANGLES,
                .index_count = 3,
                .instance_count = 1,
                .vertex_packet_stride = 16,
                .vertex_attribute_stride = 0,
                .tiler_context = tctx_va,
                .scissor_max_x = width,
                .scissor_max_y = height,
                .position_resources = 0x30000000,
                .position_shader = 0x31000000,
                .position_fau_count = 1,
                .chain_pa = 0x32000000,
                .index = 1,
                .draw = {.minimum_z = 0.0f, .maximum_z = 1.0f},
        };
        CHECK(nord2_gpu_jm_build_vertex(vs, sizeof(vs), &v), "vertex chain");
        CHECK(w(vs, 14) == (uint32_t)tctx_va, "vertex tiler");
        CHECK(w(vs, 27) == (width | (height << 16)), "vertex scissor");

        struct nord2_gpu_fragment_chain fc = {
                .bound_min_x = 0,
                .bound_min_y = 0,
                .bound_max_x = width,
                .bound_max_y = height,
                .framebuffer = fb_va,
                .chain_pa = 0x33000000,
                .index = 2,
        };
        CHECK(nord2_gpu_jm_build_fragment(fs, sizeof(fs), &fc), "fragment");
        CHECK(w(fs, 10) == (uint32_t)fb_va, "fragment fb");
        CHECK((w(fs, 4) >> 1 & 0x7f) == NORD2_JOB_FRAGMENT, "fragment type");

        if (failures) {
                fprintf(stderr, "%d failures\n", failures);
                return 1;
        }
        printf("all render-plan descriptor/chains tests passed\n");
        return 0;
}
