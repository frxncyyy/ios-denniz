/* Independent VkFormat83 (R16G16_SFLOAT) vertex-fetch integration test.
 *
 * Covers the shared vertex_fetch builder and the native render attribute
 * packer, comparing the exact 32-byte arch9 ATTRIBUTE descriptor with the
 * pinned Mesa 81daea6 v9_pack oracle.  Also checks the exact 4-byte attribute
 * end and one-byte-short refusal.  No GPU execution.
 */
#include "vertex_fetch.h"
#include "nord2_metal_render.h"
#include "nord2_preload.h"
#include "generic_job_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void n2mb_v9_vertex_attribute(uint32_t out[8], uint32_t format, uint32_t offset,
                              uint32_t buffer_index, uint32_t stride,
                              uint32_t frequency, uint32_t divisor);

#define V83_HW ((207u << 12) | 16u)
static unsigned failures, checks;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL %s\n", what); failures++; }
}

static void test_builder(void)
{
    struct tegu_vertex_attribute a = {
        .location = 0, .buffer_index = 3, .format = 83,
        .offset = 8, .stride = 32, .input_rate = 0, .divisor = 1,
    };
    struct tegu_vertex_buffer buffers[TEGU_VERTEX_SLOTS];
    struct tegu_vertex_draw draw = {
        .first_vertex = 3, .vertex_count = 2, .instance_count = 1,
        .base_instance = 0,
    };
    struct tegu_vertex_state state, before;
    uint32_t expect[8];
    uint64_t end = 0;

    check(tegu_vertex_format_bytes(83) == 4, "format83 byte size is 4");
    check(tegu_vertex_attribute_end(&a, &draw, &end) == 0 && end == 140,
          "format83 exact attribute end 140");
    a.offset = 9; /* unaligned is a builder refusal; end math still 4 bytes */
    check(tegu_vertex_attribute_end(&a, &draw, &end) == 0 && end == 141,
          "format83 offset math is four bytes");
    a.offset = 8;

    memset(buffers, 0, sizeof(buffers));
    buffers[3] = (struct tegu_vertex_buffer){.address = 0x200000ull, .length = 140};
    memset(&state, 0x5a, sizeof(state));
    check(tegu_vertex_fetch_build(1u, &a, 1, buffers, &draw, 0x100000ull, &state) == 0,
          "format83 exact-end builder succeeds");
    check(state.resource_table == (0x100000ull | 4u), "format83 SRT base+count4");
    check(state.buffer_mask == (1u << 3), "format83 buffer mask bit3");
    n2mb_v9_vertex_attribute(expect, V83_HW, 8, 35, 32, 0, 0);
    check(!memcmp(state.descriptors + 0x100, expect, 32),
          "format83 builder descriptor equals v9_pack arch9 oracle");

    memcpy(&before, &state, sizeof(state));
    buffers[3].length = 139;
    check(tegu_vertex_fetch_build(1u, &a, 1, buffers, &draw, 0x100000ull, &state) != 0,
          "format83 one-byte-short buffer refused");
    check(!memcmp(&before, &state, sizeof(state)),
          "format83 one-byte-short output unchanged");

    buffers[3].length = 140;
    a.format = 84; /* not a supported Vulkan format */
    check(tegu_vertex_fetch_build(1u, &a, 1, buffers, &draw, 0x100000ull, &state) != 0,
          "unsupported vertex format refused");
}

static void test_render_attribute(void)
{
    _Alignas(64) uint8_t data[N2R_BLOCK];
    struct tegu_generic_draw_request q;
    struct n2r_addresses addr;
    struct tegu_generic_draw_shader shader = n2_preload_shader;
    struct tegu_vertex_attribute *attr = &shader.attributes[0];
    uint32_t expect[8];

    memset(&q, 0, sizeof(q));
    q.width = q.height = 16;
    q.row_bytes = 64;
    q.target_format = 80;
    q.load_action = 2;
    q.vertex_count = 3;
    q.scissor[0] = q.scissor[1] = 0;
    q.scissor[2] = q.scissor[3] = 16;
    q.binding_count = 1;
    q.bindings[0] = (struct tegu_generic_draw_binding){
        .binding = 0, .kind = TEGU_GENERIC_DRAW_COMBINED,
        .memory = {.token = 6, .bytes = 1024},
        .width = 16, .height = 16, .row_bytes = 64, .format = 80,
    };
    q.vertex.bytes = 12;
    shader.attribute_count = 1;
    *attr = (struct tegu_vertex_attribute){
        .location = 0, .buffer_index = 0, .format = 83,
        .offset = 0, .stride = 4, .input_rate = 0, .divisor = 1,
    };
    memset(&addr, 0, sizeof(addr));
    addr.command = 0x1234000000ull;
    addr.frame = 0x1234000000ull;
    addr.heap = 0x1280000000ull;
    addr.heap_bytes = 2u << 20;
    addr.vertex_code = 0x12a0000000ull;
    addr.fragment_code = 0x12b0000000ull;
    addr.target = 0x12c0000000ull;
    addr.binding[0] = 0x12d0000000ull;
    addr.vertex = 0x12e0000000ull;

    check(n2r_build(data, &shader, &q, &addr, 2, 0) == 0,
          "render builder accepts format83 attribute");
    n2mb_v9_vertex_attribute(expect, V83_HW, 0, 17, 4, 0, 0);
    check(!memcmp(data + 2368, expect, 32),
          "render format83 attribute equals v9_pack arch9 oracle");
    q.vertex.bytes = 11;
    check(n2r_build(data, &shader, &q, &addr, 2, 0) != 0,
          "render format83 one-byte-short vertex span refused");
}

int main(void)
{
    test_builder();
    test_render_attribute();
    printf("format83 vertex fetch: %u checks, %u required failures, 0 pending failures\n", checks, failures);
    return failures ? 1 : 0;
}
