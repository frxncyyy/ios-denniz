/*
 * Host test for the production arch9 render-plan builder.  No GPU execution.
 */
#include "nord2_gpu_render_plan.h"

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

static uint32_t word(const uint8_t *p, unsigned i)
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

        struct nord2_gpu_render_plan p = {
                .width = width,
                .height = height,
                .row_stride = stride,
                .target_va = 0x10000000,
                .target_size = (uint64_t)stride * height,
                .clear_color = 0xff0000ffu,
                .tiler_heap_va = 0x20000000,
                .tiler_heap_size = 0x100000,
                .tiler_ctx_va = 0x21000000,
                .framebuffer_va = 0x22000000,
                .render_target_va = 0x23000000,
                .resources_va = 0x24000000,
                .vs_shader_va = 0x25000000,
                .vs_fau_va = 0x26000000,
                .vs_fau_count = 9,
                .fs_shader_va = 0x27000000,
                .fs_fau_va = 0x28000000,
                .fs_fau_count = 17,
                .vs_chain_va = 0x29000000,
                .fs_chain_va = 0x2a000000,
                .heap_desc = heap,
                .tiler_ctx = tctx,
                .framebuffer = fb,
                .render_target = rt,
                .vs_chain = vs,
                .fs_chain = fs,
        };

        CHECK(nord2_gpu_render_plan_build(&p), "build");
        CHECK(word(heap, 1) == 0x100000, "heap size");
        CHECK(word(tctx, 3) == ((width - 1) | ((height - 1) << 16)),
              "tiler size");
        CHECK(word(fb, 8) == ((width - 1) | ((height - 1) << 16)),
              "framebuffer size");
        CHECK(word(fb, 14) == (uint32_t)p.tiler_ctx_va, "framebuffer tiler");
        CHECK(word(rt, 8) == (uint32_t)p.target_va, "target base");
        CHECK(word(rt, 10) == stride, "target stride");
        CHECK(word(rt, 12) == p.clear_color, "target clear");
        CHECK(word(vs, 14) == (uint32_t)p.tiler_ctx_va, "vertex tiler");
        CHECK(word(vs, 27) == (width | (height << 16)), "vertex scissor");
        CHECK(word(fs, 10) == (uint32_t)p.framebuffer_va, "fragment fb");

        p.fs_fau_count = 0;
        CHECK(!nord2_gpu_render_plan_build(&p), "reject zero FAU");
        p.fs_fau_count = 17;
        p.row_stride = width * 3;
        CHECK(!nord2_gpu_render_plan_build(&p), "reject short stride");
        p.row_stride = stride;
        p.framebuffer = NULL;
        CHECK(!nord2_gpu_render_plan_build(&p), "reject missing buffer");

        if (failures) {
                fprintf(stderr, "%d failures\n", failures);
                return 1;
        }
        printf("all production render-plan builder tests passed\n");
        return 0;
}
