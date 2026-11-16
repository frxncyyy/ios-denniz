/*
 * Host tests for the arch9 JM chain builders (production paths used by the
 * kernel user client).  No hardware execution.
 */
#include "nord2_gpu_desc.h"
#include "nord2_gpu_jm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, msg)                                                        \
        do {                                                                    \
                if (!(cond)) {                                                  \
                        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, \
                                msg);                                           \
                        failures++;                                             \
                }                                                               \
        } while (0)

static uint32_t w(const uint8_t *p, unsigned i)
{
        uint32_t v;
        memcpy(&v, p + i * 4, 4);
        return v;
}

static void test_compute(void)
{
        uint8_t out[NORD2_GPU_COMPUTE_CHAIN_BYTES];
        memset(out, 0xa5, sizeof(out));
        struct nord2_gpu_compute_chain c = {
                .workgroup_size = {8, 4, 1},
                .workgroup_count = {2, 3, 1},
                .allow_merging = true,
                .resources = 0x100000,
                .shader = 0x200000,
                .thread_storage = 0x300000,
                .fau = 0x400000,
                .fau_count = 2,
                .attribute_offset = 5,
                .chain_pa = 0x100000,
                .next_pa = 0,
                .index = 9,
        };
        CHECK(nord2_gpu_jm_build_compute(out, sizeof(out), &c), "build");
        CHECK(w(out, 4) == ((uint32_t)NORD2_JOB_COMPUTE << 1) | (9u << 16),
              "job header");
        CHECK(w(out, 8) == (7u) | (3u << 10) | (0u << 20) | (1u << 31),
              "workgroup size");
        CHECK(w(out, 9) == 1u | (2u << 14), "task axis");
        CHECK(w(out, 10) == 2 && w(out, 11) == 3 && w(out, 12) == 1,
              "counts");
        CHECK(w(out, 16) == 5 && w(out, 17) == 2, "shader env header");
        CHECK(w(out, 24) == 0x100000, "resources");
        CHECK(w(out, 26) == 0x200000, "shader");
        CHECK(w(out, 28) == 0x300000, "tls");
        CHECK(w(out, 30) == 0x400000, "fau");

        uint8_t before[sizeof(out)];
        memcpy(before, out, sizeof(out));
        c.shader = 0x200001; /* unaligned */
        CHECK(!nord2_gpu_jm_build_compute(out, sizeof(out), &c), "unaligned");
        CHECK(!memcmp(before, out, sizeof(out)), "refusal immutable");
        c = (struct nord2_gpu_compute_chain){
                .workgroup_size = {1, 1, 1},
                .workgroup_count = {1, 1, 1},
                .resources = 0x100000,
                .shader = 0x200000,
                .chain_pa = 0x100000,
                .index = 1,
        };
        CHECK(!nord2_gpu_jm_build_compute(out, 64, &c), "short buffer");
        c.chain_pa = 0x100001;
        CHECK(!nord2_gpu_jm_build_compute(out, sizeof(out), &c), "bad chain");
        c.chain_pa = 0x100000;
        c.resources = 0;
        c.thread_storage = 0;
        c.fau = 0;
        CHECK(nord2_gpu_jm_build_compute(out, sizeof(out), &c),
              "no-resource compute");
        c.resources = 0x100000;
        c.workgroup_count[0] = 0;
        CHECK(!nord2_gpu_jm_build_compute(out, sizeof(out), &c), "zero count");
}

static void test_vertex_fragment(void)
{
        uint8_t out[NORD2_GPU_VERTEX_CHAIN_BYTES];
        struct nord2_gpu_vertex_chain v = {
                .mode = NORD2_DRAW_TRIANGLES,
                .index_type = NORD2_INDEX_NONE,
                .index_count = 3,
                .base_vertex = 0,
                .instance_count = 1,
                .vertex_packet_stride = 16,
                .vertex_attribute_stride = 0,
                .tiler_context = 0x500000,
                .indices = 0,
                .scissor_min_x = 0,
                .scissor_min_y = 0,
                .scissor_max_x = 1125,
                .scissor_max_y = 2436,
                .draw = {.minimum_z = 0.0f, .maximum_z = 1.0f},
                .position_resources = 0x600000,
                .position_shader = 0x700000,
                .position_fau_count = 1,
                .varying_resources = 0x680000,
                .varying_shader = 0x780000,
                .varying_fau_count = 1,
                .chain_pa = 0x800000,
                .index = 1,
        };
        CHECK(nord2_gpu_jm_build_vertex(out, sizeof(out), &v), "vertex build");
        CHECK(w(out, 4) == ((uint32_t)NORD2_JOB_MALLOC_VERTEX << 1) | (1u << 16),
              "vertex type");
        CHECK(w(out, 8) == ((uint32_t)NORD2_DRAW_TRIANGLES) | (1u << 15) |
                                    (1u << 16) | (1u << 17),
              "primitive");
        CHECK(w(out, 12) == 1, "instance count");
        CHECK(w(out, 13) == (16u | (0u << 16)), "allocation");
        CHECK(w(out, 14) == 0x500000, "tiler pointer");
        CHECK(w(out, 26) == 0 && w(out, 27) == (1125u | (2436u << 16)),
              "scissor");
        CHECK(w(out, 38) == 0, "draw min z");
        CHECK(w(out, 39) == 0x3f800000, "draw max z");
        CHECK(w(out, 72) == 0x600000 && w(out, 74) == 0x700000,
              "position env");
        CHECK(w(out, 88) == 0x680000 && w(out, 90) == 0x780000,
              "varying env");

        uint8_t f[NORD2_GPU_FRAGMENT_CHAIN_BYTES];
        struct nord2_gpu_fragment_chain frag = {
                .bound_min_x = 0,
                .bound_min_y = 0,
                .bound_max_x = 1125,
                .bound_max_y = 2436,
                .framebuffer = 0x900000,
                .chain_pa = 0xa00000,
                .index = 2,
        };
        CHECK(nord2_gpu_jm_build_fragment(f, sizeof(f), &frag), "fragment");
        CHECK(w(f, 4) == ((uint32_t)NORD2_JOB_FRAGMENT << 1) | (2u << 16),
              "fragment type");
        CHECK(w(f, 8) == (0u | (0u << 16)), "bbox min");
        CHECK(w(f, 9) == (1125u | (2436u << 16)), "bbox max");
        CHECK(w(f, 10) == 0x900000, "framebuffer");
        frag.bound_max_x = 4096;
        CHECK(!nord2_gpu_jm_build_fragment(f, sizeof(f), &frag), "bbox limit");
}

int main(void)
{
        test_compute();
        test_vertex_fragment();
        if (failures) {
                fprintf(stderr, "%d failures\n", failures);
                return 1;
        }
        printf("all JM builder tests passed\n");
        return 0;
}
