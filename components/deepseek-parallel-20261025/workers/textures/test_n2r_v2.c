/* Host test for n2r v2 sidecar: old and new paths are built in the same
 * process, the old/default packet stays byte-identical, and a sidecar entry is
 * authoritative (never silently falls back).  No GPU submission. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "genxml/v9_pack.h"
#include "nord2_metal_render.h"
#include "nord2_preload.h"

static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t get64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static struct tegu_generic_draw_request request(uint32_t w, uint32_t h, uint32_t row,
                                                 uint32_t format, uint64_t bytes)
{
    struct tegu_generic_draw_request q;
    memset(&q, 0, sizeof(q));
    q.width = w; q.height = h; q.row_bytes = row; q.target_format = format;
    q.load_action = 2; q.vertex_count = 3; q.scissor[0] = 0; q.scissor[1] = 0;
    q.scissor[2] = w; q.scissor[3] = h; q.binding_count = 1;
    q.bindings[0].kind = TEGU_GENERIC_DRAW_COMBINED;
    q.bindings[0].binding = 0;
    q.bindings[0].memory.token = 6;
    q.bindings[0].memory.bytes = bytes;
    q.bindings[0].width = w; q.bindings[0].height = h;
    q.bindings[0].row_bytes = row; q.bindings[0].format = format;
    return q;
}

static struct n2r_addresses addresses(void)
{
    struct n2r_addresses a;
    memset(&a, 0, sizeof(a));
    a.command = 0x1234000000ull; a.frame = a.command;
    a.heap = 0x1280000000ull; a.heap_bytes = 2u << 20;
    a.vertex_code = 0x12a0000000ull; a.fragment_code = 0x12b0000000ull;
    a.target = 0x12c0000000ull; a.binding[0] = 0x12d0000000ull;
    return a;
}

static void baseline_sampler(struct n2t_sampler_state *s)
{
    memset(s, 0, sizeof(*s));
    s->version = N2T_TEXTURE_RESOURCES_VERSION;
    s->min_filter = N2T_FILTER_NEAREST; s->mag_filter = N2T_FILTER_NEAREST;
    s->mip_filter = N2T_MIP_NEAREST;
    s->address_s = s->address_t = s->address_r = N2T_ADDRESS_CLAMP_EDGE;
    s->normalized = 1; s->max_anisotropy = 1;
    s->clamp_integer_array = 1; s->seamless_cube = 1;
    s->lod_min = 0; s->lod_max = 0;
}

static int build_old(uint8_t *m, const struct tegu_generic_draw_request *q,
                     const struct n2r_addresses *a)
{
    return n2r_build(m, &n2_preload_shader, q, a, 2, a->command + N2R_BLOCK + N2R_JOB);
}

static int build_new(uint8_t *m, const struct tegu_generic_draw_request *q,
                     const struct n2r_addresses *a,
                     const struct n2r_texture_resources *tr)
{
    return n2r_build_v2(m, &n2_preload_shader, q, a, tr, 2, a->command + N2R_BLOCK + N2R_JOB);
}


static void mesa_attribute_83(uint32_t out[8])
{
    pan_cast_and_pack(out, ATTRIBUTE, cfg) {
        cfg.type = MALI_DESCRIPTOR_TYPE_ATTRIBUTE;
        cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
        cfg.offset_enable = true;
        cfg.format = (207u << 12) | 16u;
        cfg.table = 0;
        cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX;
        cfg.offset = 0;
        cfg.buffer_index = 17;
        cfg.stride = 4;
    }
}

static void test_old_exact(void)
{
    uint8_t oldp[N2R_BLOCK], newp[N2R_BLOCK], empty[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(64, 64, 256, 80, 16384);
    struct n2r_addresses a = addresses();
    struct n2r_texture_resources tr;
    struct n2r_texture_binding b;
    memset(&tr, 0, sizeof(tr));
    tr.version = N2R_TEXTURE_RESOURCES_VERSION; tr.count = 1; tr.bindings = &b;
    memset(&b, 0, sizeof(b));
    b.binding = 0; b.kind = TEGU_GENERIC_DRAW_COMBINED; b.backing_length = 16384;
    b.layout.version = N2T_TEXTURE_RESOURCES_VERSION; b.layout.kind = N2T_KIND_2D;
    b.layout.format = 80; b.layout.width = 64; b.layout.height = 64; b.layout.depth = 1;
    b.layout.levels = 1; b.layout.layers = 1; b.layout.samples = 1;
    b.layout.array_length = 1; b.layout.access = N2T_ACCESS_SAMPLE;
    b.layout.usage = 1; b.layout.storage_mode = 1;
    b.layout.level[0] = (struct n2t_level){0, 256, 0, 0, 64, 64, 1};
    baseline_sampler(&b.sampler);
    assert(!build_old(oldp, &q, &a));
    assert(!build_new(newp, &q, &a, &tr));
    /* Everything before the binding tables is unchanged. */
    assert(!memcmp(oldp, newp, 4096));
    /* The sampler descriptor is byte-identical. */
    assert(!memcmp(oldp + 5120, newp + 5120, 32));
    /* The texture descriptor differs only in the plane payload pointer. */
    for (unsigned i = 0; i < 8; i++) {
        if (i == 4 || i == 5) continue;
        assert(get32(oldp + 4096 + 4u * i) == get32(newp + 4096 + 4u * i));
    }
    assert(get64(newp + 4096 + 16) == a.command + N2R_PAYLOAD);
    assert(get64(oldp + 4096 + 16) == a.command + 6144);
    /* Legacy plane words0..6 are identical; arch9 v2 word7 is reserved zero
     * while the old qualified v10-tail bytes stay nonzero. */
    assert(!memcmp(oldp + 6144, newp + N2R_PAYLOAD, 28));
    assert(get32(newp + N2R_PAYLOAD + 28) == 0);
    assert(get32(oldp + 6144 + 28) != 0);
    /* Old plane slot is clean in v2; old payload tail was clean already. */
    for (unsigned i = 0; i < 32; i++) assert(newp[6144 + i] == 0);
    for (unsigned i = N2R_PAYLOAD; i < N2R_BLOCK; i++) assert(oldp[i] == 0);
    /* An empty sidecar is exactly the old builder. */
    tr.count = 0;
    assert(!build_new(empty, &q, &a, &tr));
    assert(!memcmp(oldp, empty, N2R_BLOCK));
    /* An empty sidecar keeps the old exact span rule; a binding without a
     * sidecar cannot smuggle a larger span through a v2 request. */
    q.bindings[0].memory.bytes = 16384 + 64;
    tr.count = 0;
    assert(build_new(empty, &q, &a, &tr) == -1);
    tr.count = 1; b.backing_length = 16384 + 64;
    q.bindings[0].memory.bytes = 16384;
    b.layout.level[0].row_stride = 256;
    assert(build_new(empty, &q, &a, &tr) == -1); /* backing_length > memory.bytes */
    b.backing_length = 16384; q.bindings[0].memory.bytes = 16384;
    /* A malformed sidecar is authoritative, not a fallback. */
    b.layout.version = 0;
    memset(empty, 0x5a, sizeof(empty));
    assert(build_new(empty, &q, &a, &tr) == -1);
    b.layout.version = N2T_TEXTURE_RESOURCES_VERSION;
    b.kind = TEGU_GENERIC_DRAW_SAMPLER; /* request says COMBINED */
    assert(build_new(empty, &q, &a, &tr) == -1);
    b.kind = TEGU_GENERIC_DRAW_COMBINED;
    b.sampler.version = 0;
    assert(build_new(empty, &q, &a, &tr) == -1);
    puts("PASS n2r old packet preserved and v2 sidecar authoritative");
}

static void test_mips(void)
{
    uint8_t m[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 70, 1792);
    struct n2r_addresses a = addresses();
    struct n2r_texture_resources tr;
    struct n2r_texture_binding b;
    memset(&tr, 0, sizeof(tr));
    tr.version = N2R_TEXTURE_RESOURCES_VERSION; tr.count = 1; tr.bindings = &b;
    memset(&b, 0, sizeof(b));
    b.binding = 0; b.kind = TEGU_GENERIC_DRAW_COMBINED; b.backing_length = 1792;
    b.layout.version = N2T_TEXTURE_RESOURCES_VERSION; b.layout.kind = N2T_KIND_2D;
    b.layout.format = 70; b.layout.width = 16; b.layout.height = 16; b.layout.depth = 1;
    b.layout.levels = 3; b.layout.layers = 1; b.layout.samples = 1;
    b.layout.array_length = 1; b.layout.access = N2T_ACCESS_SAMPLE;
    b.layout.usage = 1; b.layout.storage_mode = 1;
    b.layout.level[0] = (struct n2t_level){0, 64, 0, 0, 16, 16, 1};
    b.layout.level[1] = (struct n2t_level){1024, 64, 0, 0, 8, 8, 1};
    b.layout.level[2] = (struct n2t_level){1536, 64, 0, 0, 4, 4, 1};
    baseline_sampler(&b.sampler);
    assert(!build_new(m, &q, &a, &tr));
    uint32_t tex0 = get32(m + 4096);
    assert(((tex0 >> 4) & 7u) == 2u);
    assert(((get32(m + 4096 + 8) >> 16) & 31u) == 2u);
    assert(((get32(m + 4096 + 12) >> 16) & 0x1fffu) == 0x0200u);
    assert(get32(m + N2R_PAYLOAD + 4) == 64u * 16u);
    assert(get32(m + N2R_PAYLOAD + 32 + 4) == 64u * 8u);
    assert(get32(m + N2R_PAYLOAD + 64 + 4) == 64u * 4u);
    assert(get64(m + N2R_PAYLOAD + 8) == a.binding[0]);
    assert(get64(m + N2R_PAYLOAD + 32 + 8) == a.binding[0] + 1024);
    assert(get64(m + N2R_PAYLOAD + 64 + 8) == a.binding[0] + 1536);
    puts("PASS n2r v2 mip table levels/lod/plane order");
}

static void test_cube(void)
{
    uint8_t m[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(8, 8, 64, 70, 6144);
    struct n2r_addresses a = addresses();
    struct n2r_texture_resources tr;
    struct n2r_texture_binding b;
    memset(&tr, 0, sizeof(tr)); tr.version = N2R_TEXTURE_RESOURCES_VERSION;
    tr.count = 1; tr.bindings = &b; memset(&b, 0, sizeof(b));
    b.binding = 0; b.kind = TEGU_GENERIC_DRAW_COMBINED; b.backing_length = 6144;
    b.layout.version = N2T_TEXTURE_RESOURCES_VERSION; b.layout.kind = N2T_KIND_CUBE;
    b.layout.format = 70; b.layout.width = 8; b.layout.height = 8; b.layout.depth = 1;
    b.layout.levels = 1; b.layout.layers = 6; b.layout.samples = 1;
    b.layout.array_length = 6; b.layout.access = N2T_ACCESS_SAMPLE;
    b.layout.usage = 1; b.layout.storage_mode = 1;
    b.layout.level[0] = (struct n2t_level){0, 64, 0, 1024, 8, 8, 1};
    baseline_sampler(&b.sampler);
    assert(!build_new(m, &q, &a, &tr));
    assert(((get32(m + 4096) >> 4) & 7u) == 0u);      /* cube dimension */
    assert(((get32(m + 4096 + 24) & 0xffffu) + 1u) == 1u); /* one cube */
    for (unsigned f = 0; f < 6; f++)
        assert(get64(m + N2R_PAYLOAD + 32u * f + 8) == a.binding[0] + 1024u * f);
    puts("PASS n2r v2 cube face plane order");
}

static void test_sampler_only(void)
{
    uint8_t oldp[N2R_BLOCK], newp[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 70, 0);
    struct n2r_addresses a = addresses();
    struct n2r_texture_resources tr;
    struct n2r_texture_binding b;
    q.binding_count = 1;
    q.bindings[0] = (struct tegu_generic_draw_binding){.kind = TEGU_GENERIC_DRAW_SAMPLER, .binding = 0, .sampler = 0};
    memset(&tr, 0, sizeof(tr)); tr.version = N2R_TEXTURE_RESOURCES_VERSION;
    tr.count = 1; tr.bindings = &b; memset(&b, 0, sizeof(b));
    b.binding = 0; b.kind = TEGU_GENERIC_DRAW_SAMPLER;
    baseline_sampler(&b.sampler);
    assert(!build_old(oldp, &q, &a));
    assert(!build_new(newp, &q, &a, &tr));
    assert(!memcmp(oldp, newp, N2R_BLOCK));
    puts("PASS n2r v2 sampler-only baseline equality");
}


static void test_v2_formats(void)
{
    struct { uint32_t format, w, h, row, bytes; } cases[] = {
        {125, 4, 1, 64, 64},   /* RGBA32Float */
        {90, 4, 1, 64, 64},    /* RGB10A2 */
        {94, 4, 1, 64, 64},    /* BGR10A2 */
        {65, 4, 1, 64, 64},    /* RG16F */
        {25, 4, 1, 64, 64},    /* R16F */
        {55, 4, 1, 64, 64},    /* R32F */
        {30, 4, 1, 64, 64},    /* RG8 */
        {110, 4, 1, 64, 64},   /* RGBA16Unorm */
        {180, 32, 32, 64, 512},/* ETC2 RGB8, 8-byte blocks */
        {250, 16, 4, 64, 256}, /* depth16 */
    };
    for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
        uint8_t m[N2R_BLOCK], oldp[N2R_BLOCK];
        struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
        q.bindings[0].width = cases[c].w; q.bindings[0].height = cases[c].h;
        q.bindings[0].row_bytes = cases[c].row; q.bindings[0].format = cases[c].format;
        q.bindings[0].memory.bytes = cases[c].bytes;
        struct n2r_addresses a = addresses();
        struct n2r_texture_resources tr;
        struct n2r_texture_binding b;
        memset(&tr, 0, sizeof(tr)); tr.version = N2R_TEXTURE_RESOURCES_VERSION;
        tr.count = 1; tr.bindings = &b; memset(&b, 0, sizeof(b));
        b.binding = 0; b.kind = TEGU_GENERIC_DRAW_COMBINED; b.backing_length = cases[c].bytes;
        b.layout.version = N2T_TEXTURE_RESOURCES_VERSION; b.layout.kind = N2T_KIND_2D;
        b.layout.format = cases[c].format; b.layout.width = cases[c].w;
        b.layout.height = cases[c].h; b.layout.depth = 1; b.layout.levels = 1;
        b.layout.layers = 1; b.layout.samples = 1; b.layout.array_length = 1;
        b.layout.access = N2T_ACCESS_SAMPLE; b.layout.usage = 1; b.layout.storage_mode = 1;
        b.layout.level[0] = (struct n2t_level){0, cases[c].row, 0, 0,
            cases[c].w, cases[c].h, 1};
        baseline_sampler(&b.sampler);
        { int vrc = n2r_validate_v2(&n2_preload_shader, &q, &tr); if (vrc) fprintf(stderr, "format %u vrc=%d\n", cases[c].format, vrc); assert(vrc == 0); }
        assert(!build_new(m, &q, &a, &tr));
        /* The identical legacy request must keep its old refusal, except
         * formats the legacy whitelist already accepted (RG8). */
        if (cases[c].format != 30) assert(build_old(oldp, &q, &a) == -1);
        uint32_t tex0 = get32(m + 4096);
        assert(((tex0 >> 4) & 7u) == 2u);
    }
    puts("PASS n2r v2 formats 125/ETC2/depth accepted while legacy refuses");
}

static void test_sparse_bindings(void)
{
    uint8_t m[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 0);
    q.binding_count = 2;
    q.bindings[0] = (struct tegu_generic_draw_binding){
        .kind = TEGU_GENERIC_DRAW_COMBINED, .binding = 3,
        .memory = {6, 0, 64}, .width = 4, .height = 1, .row_bytes = 64, .format = 70};
    q.bindings[1] = (struct tegu_generic_draw_binding){
        .kind = TEGU_GENERIC_DRAW_COMBINED, .binding = 9,
        .memory = {7, 0, 64}, .width = 4, .height = 1, .row_bytes = 64, .format = 80};
    struct n2r_addresses a = addresses();
    a.binding[0] = 0x30000000ull; a.binding[1] = 0x40000000ull;
    struct n2r_texture_resources tr;
    struct n2r_texture_binding b[2];
    memset(&tr, 0, sizeof(tr)); tr.version = N2R_TEXTURE_RESOURCES_VERSION;
    tr.count = 2; tr.bindings = b; memset(b, 0, sizeof(b));
    for (unsigned i = 0; i < 2; i++) {
        b[i].binding = i ? 9 : 3;
        b[i].kind = TEGU_GENERIC_DRAW_COMBINED;
        b[i].backing_length = 64;
        b[i].layout.version = N2T_TEXTURE_RESOURCES_VERSION;
        b[i].layout.kind = N2T_KIND_2D;
        b[i].layout.format = i ? 80 : 70;
        b[i].layout.width = 4; b[i].layout.height = 1; b[i].layout.depth = 1;
        b[i].layout.levels = 1; b[i].layout.layers = 1; b[i].layout.samples = 1;
        b[i].layout.array_length = 1; b[i].layout.access = N2T_ACCESS_SAMPLE;
        b[i].layout.usage = 1; b[i].layout.storage_mode = 1;
        b[i].layout.level[0] = (struct n2t_level){0, 64, 0, 0, 4, 1, 1};
        baseline_sampler(&b[i].sampler);
    }
    assert(n2r_validate_v2(&n2_preload_shader, &q, &tr) == 0);
    assert(!build_new(m, &q, &a, &tr));
    assert(get64(m + 4096 + 3u * 32 + 16) == a.command + N2R_PAYLOAD);
    assert(get64(m + 4096 + 9u * 32 + 16) == a.command + N2R_PAYLOAD + 32);
    assert(get64(m + N2R_PAYLOAD + 8) == a.binding[0]);
    assert(get64(m + N2R_PAYLOAD + 32 + 8) == a.binding[1]);
    /* An orphan sidecar entry is refused before any build. */
    b[1].binding = 8;
    assert(n2r_validate_v2(&n2_preload_shader, &q, &tr) == -1);
    assert(build_new(m, &q, &a, &tr) == -1);
    puts("PASS n2r v2 sparse binding positions 3/9 and orphan refusal");
}


static void test_oversized_binding_count(void)
{
    uint8_t m[N2R_BLOCK], before[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct n2r_texture_resources tr;
    struct n2r_texture_binding b;
    memset(&tr, 0, sizeof(tr)); tr.version = N2R_TEXTURE_RESOURCES_VERSION;
    tr.count = 1; tr.bindings = &b; memset(&b, 0, sizeof(b));
    b.binding = 0; b.kind = TEGU_GENERIC_DRAW_COMBINED; b.backing_length = 1024;
    b.layout.version = N2T_TEXTURE_RESOURCES_VERSION; b.layout.kind = N2T_KIND_2D;
    b.layout.format = 80; b.layout.width = 16; b.layout.height = 16; b.layout.depth = 1;
    b.layout.levels = 1; b.layout.layers = 1; b.layout.samples = 1;
    b.layout.array_length = 1; b.layout.access = N2T_ACCESS_SAMPLE;
    b.layout.usage = 1; b.layout.storage_mode = 1;
    b.layout.level[0] = (struct n2t_level){0, 64, 0, 0, 16, 16, 1};
    baseline_sampler(&b.sampler);
    memset(m, 0x5a, sizeof(m)); memcpy(before, m, sizeof(m));
    /* One past the embedded request array.  The preflight must reject before
     * scanning q->bindings[], so ASan sees no out-of-bounds access. */
    q.binding_count = TEGU_GENERIC_DRAW_MAX_BINDINGS + 1u;
    assert(n2r_validate_v2(&n2_preload_shader, &q, &tr) == -1);
    assert(n2r_validate(&n2_preload_shader, &q) == -1);
    assert(build_new(m, &q, &a, &tr) == -1);
    assert(!memcmp(m, before, sizeof(m)));
    /* Integer boundary. */
    q.binding_count = UINT32_MAX;
    assert(n2r_validate_v2(&n2_preload_shader, &q, &tr) == -1);
    assert(build_new(m, &q, &a, &tr) == -1);
    assert(!memcmp(m, before, sizeof(m)));
    /* The same oversized request without a sidecar must also refuse before
     * gd_request_ok sees the embedded array. */
    q.binding_count = TEGU_GENERIC_DRAW_MAX_BINDINGS + 1u;
    assert(n2r_validate(&n2_preload_shader, &q) == -1);
    assert(build_new(m, &q, &a, NULL) == -1);
    assert(!memcmp(m, before, sizeof(m)));
    /* Sidecar count is also bounded before its pointer is followed. */
    q.binding_count = 1;
    tr.count = UINT32_MAX;
    assert(n2r_validate_v2(&n2_preload_shader, &q, &tr) == -1);
    assert(build_new(m, &q, &a, &tr) == -1);
    assert(!memcmp(m, before, sizeof(m)));
    tr.count = TEGU_GENERIC_DRAW_MAX_BINDINGS + 1u;
    tr.bindings = NULL;
    assert(n2r_validate_v2(&n2_preload_shader, &q, &tr) == -1);
    assert(!memcmp(m, before, sizeof(m)));
    puts("PASS n2r oversized request/sidecar counts refused before indexing, output preserved");
}


static void test_vk83_attribute(void)
{
    uint8_t m[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct tegu_generic_draw_shader sh = n2_preload_shader;
    uint32_t want[8];
    q.vertex.token = 6; q.vertex.bytes = 64;
    a.vertex = 0x50000000ull;
    sh.attribute_count = 1;
    sh.attributes[0] = (struct tegu_vertex_attribute){0, 0, 83, 0, 4, 0, 1};
    mesa_attribute_83(want);
    assert(!n2r_build(m, &sh, &q, &a, 2, a.command + N2R_BLOCK + N2R_JOB));
    /* n2r descriptor base is 2368; word0..4 are written, word5..7 zero. */
    for (unsigned i = 0; i < 5; i++) {
        uint32_t got; memcpy(&got, m + 2368 + 32 * 0 + 4u * i, 4);
        assert(got == want[i]);
    }
    for (unsigned i = 5; i < 8; i++) {
        uint32_t got; memcpy(&got, m + 2368 + 4u * i, 4);
        assert(got == 0);
    }
    /* The v9 descriptor is exactly 32 bytes; byte 16..19 carries stride 4. */
    uint32_t stride; memcpy(&stride, m + 2368 + 16, 4);
    assert(stride == 4);
    puts("PASS n2r VkFormat83/RG16F attribute descriptor matches v9 ATTRIBUTE oracle");
}


static void test_draw_options(void)
{
    uint8_t m[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct tegu_generic_draw_shader sh = n2_preload_shader;
    struct n2r_draw_options opt;
    int32_t vb; uint32_t bi;

    q.vertex.token = 6; q.vertex.bytes = 64;
    a.vertex = 0x50000000ull;
    sh.vertex_fau_words = 18;
    sh.attribute_count = 2;
    sh.attributes[0] = (struct tegu_vertex_attribute){0, 0, 83, 0, 4, 0, 1};
    sh.attributes[1] = (struct tegu_vertex_attribute){1, 0, 83, 16, 4, 1, 1};
    memset(&opt, 0, sizeof(opt));
    opt.version = N2R_DRAW_OPTIONS_VERSION;
    opt.flags = N2R_DRAW_FLAT_SHADING | N2R_DRAW_WRITE_VERTEX_BASE | N2R_DRAW_WRITE_BASE_INSTANCE;
    opt.vertex_base = -3;
    opt.base_instance = 5;
    assert(!n2r_build_v3(m, &sh, &q, &a, NULL, &opt, 2, a.command + N2R_BLOCK + N2R_JOB));
    pan_unpack((const struct mali_tiler_context_packed *)(m + 512), TILER_CONTEXT, tc);
    assert(tc.first_provoking_vertex);
    pan_unpack((const struct mali_primitive_packed *)(m + N2R_JOB + 32), PRIMITIVE, prim);
    assert(prim.base_vertex_offset == 0);
    memcpy(&vb, m + 1664 + 64, 4); memcpy(&bi, m + 1664 + 68, 4);
    assert(vb == -3 && bi == 5);
    assert(get32(m + 2368 + 0u * 32 + 8) == 0u);
    assert(get32(m + 2368 + 1u * 32 + 8) == 36u); /* 16 + 5*4 */
    /* Default v1/v2 path must not write either FAU word or the tiler bit. */
    memset(m, 0x5a, sizeof(m));
    assert(!n2r_build(m, &sh, &q, &a, 2, a.command + N2R_BLOCK + N2R_JOB));
    pan_unpack((const struct mali_tiler_context_packed *)(m + 512), TILER_CONTEXT, tc2);
    assert(!tc2.first_provoking_vertex);
    memcpy(&vb, m + 1664 + 64, 4); memcpy(&bi, m + 1664 + 68, 4);
    assert(vb == 0 && bi == 0);
    assert(get32(m + 2368 + 1u * 32 + 8) == 16u);
    /* Invalid option/fau combinations refuse before output mutation. */
    {
        uint8_t before[N2R_BLOCK];
        struct n2r_draw_options bad = opt;
        memset(m, 0x5a, sizeof(m)); memcpy(before, m, sizeof(m));
        bad.version = 0;
        assert(n2r_validate_v3(&sh, &q, NULL, &bad) == -1);
        assert(n2r_build_v3(m, &sh, &q, &a, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        bad = opt; bad.flags |= 0x80000000u;
        assert(n2r_validate_v3(&sh, &q, NULL, &bad) == -1);
        bad = opt; sh.vertex_fau_words = 16;
        assert(n2r_validate_v3(&sh, &q, NULL, &bad) == -1);
        sh.vertex_fau_words = 18;
    }
    puts("PASS n2r draw options: flat provoking, VB/BI FAU, BI attr rebase");
}



static void test_thread_storage(void)
{
    uint8_t m[N2R_BLOCK], oldp[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct n2r_draw_options opt;
    static const uint32_t sizes[] = {16, 32, 64, 192, 32768};
    static const uint32_t shifts[] = {0, 1, 2, 4, 11};

    memset(&opt, 0, sizeof(opt));
    opt.version = N2R_DRAW_OPTIONS_VERSION;
    {
        static const uint32_t shift_in[] = {0, 16, 17, 32, 192, 32768};
        static const uint32_t shift_out[] = {0, 0, 1, 1, 4, 11};
        uint64_t total = 0;
        for (unsigned i = 0; i < sizeof(shift_in)/sizeof(shift_in[0]); i++)
            assert(n2r_thread_storage_shift(shift_in[i]) == shift_out[i]);
        assert(!n2r_thread_storage_total(16, 2048, 4, &total) && total == 16ull*2048*4);
        assert(!n2r_thread_storage_total(17, 2048, 4, &total) && total == 32ull*2048*4);
        assert(!n2r_thread_storage_total(0, 2048, 4, &total) && total == 0);
        assert(n2r_thread_storage_total(N2R_THREAD_STORAGE_MAX + 1, 2048, 4, &total) == -1);
        assert(n2r_thread_storage_total(16, 0, 4, &total) == -1);
        assert(n2r_thread_storage_total(16, 2048, 0, &total) == -1);
        assert(n2r_thread_storage_total(16, 2048, 4, 0) == -1);
    }
    a.tls = 0x123456789000ull;
    for (unsigned i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        opt.thread_storage_bytes = sizes[i];
        assert(!n2r_validate_v3(&n2_preload_shader, &q, NULL, &opt));
        assert(!n2r_build_v3(m, &n2_preload_shader, &q, &a, NULL, &opt, 2,
                            a.command + N2R_BLOCK + N2R_JOB));
        pan_unpack((const struct mali_local_storage_packed *)(m + 832),
                   LOCAL_STORAGE, tls0);
        assert(tls0.tls_size == shifts[i]);
        assert(tls0.tls_address_mode == MALI_ADDRESS_MODE_PACKED);
        assert(tls0.tls_base_pointer == (a.tls >> 8));
        assert(tls0.wls_instances == MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM);
        assert(tls0.wls_base_pointer == 0 && tls0.wls_size_base == 0);
        struct MALI_LOCAL_STORAGE want = {MALI_LOCAL_STORAGE_header};
        struct mali_local_storage_packed wp;
        want.tls_size = shifts[i];
        want.tls_address_mode = MALI_ADDRESS_MODE_PACKED;
        want.tls_base_pointer = a.tls >> 8;
        MALI_LOCAL_STORAGE_pack(&wp, &want);
        assert(!memcmp(m + 832, &wp, MALI_LOCAL_STORAGE_LENGTH));
        assert(get32(m + 832) == shifts[i]);
        assert(get32(m + 832 + 4) == 31u); /* NO_WORKGROUP_MEM log2 */
        assert(get32(m + 832 + 8) == (uint32_t)(a.tls >> 8));
        assert((get32(m + 832 + 12) & 0x0fffffffu) ==
               (uint32_t)((a.tls >> 8) >> 32));
        assert((get32(m + 832 + 12) >> 28) == MALI_ADDRESS_MODE_PACKED);
        pan_unpack((const struct mali_draw_packed *)(m + N2R_JOB + 128), DRAW, dr0);
        pan_unpack((const struct mali_shader_environment_packed *)(m + N2R_JOB + 256),
                   SHADER_ENVIRONMENT, vs0);
        pan_unpack((const struct mali_shader_environment_packed *)(m + N2R_JOB + 320),
                   SHADER_ENVIRONMENT, vs1);
        assert(dr0.shader.thread_storage == a.frame + 832);
        assert(vs0.thread_storage == a.frame + 832);
        assert(vs1.thread_storage == a.frame + 832);
    }
    /* TLS zero is byte-identical to the historical packet. */
    opt.thread_storage_bytes = 0;
    a.tls = 0;
    assert(!n2r_build_v3(m, &n2_preload_shader, &q, &a, NULL, &opt, 2,
                         a.command + N2R_BLOCK + N2R_JOB));
    assert(!n2r_build(oldp, &n2_preload_shader, &q, &a, 2,
                      a.command + N2R_BLOCK + N2R_JOB));
    assert(!memcmp(m, oldp, N2R_BLOCK));
    assert(get32(m + 832) == 0 && get32(m + 832 + 4) == 31u);

    /* Every invalid TLS shape refuses before output mutation. */
    {
        uint8_t before[N2R_BLOCK];
        struct n2r_draw_options bad = opt;
        memset(m, 0x5a, sizeof(m)); memcpy(before, m, sizeof(m));
        bad.thread_storage_bytes = N2R_THREAD_STORAGE_MAX + 1;
        assert(n2r_validate_v3(&n2_preload_shader, &q, NULL, &bad) == -1);
        assert(n2r_build_v3(m, &n2_preload_shader, &q, &a, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        bad = opt; bad.thread_storage_bytes = 16; bad.thread_storage_reserved = 1;
        assert(n2r_validate_v3(&n2_preload_shader, &q, NULL, &bad) == -1);
        bad.thread_storage_reserved = 0;
        a.tls = 0x123456789001ull;
        assert(!n2r_validate_v3(&n2_preload_shader, &q, NULL, &bad));
        assert(n2r_build_v3(m, &n2_preload_shader, &q, &a, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        a.tls = 1ull << 56;
        assert(n2r_build_v3(m, &n2_preload_shader, &q, &a, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        a.tls = 0;
        assert(n2r_build_v3(m, &n2_preload_shader, &q, &a, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        bad.thread_storage_bytes = 0; a.tls = 0x123456789000ull;
        assert(!n2r_build_v3(m, &n2_preload_shader, &q, &a, NULL, &bad, 2,
                            a.command + N2R_BLOCK + N2R_JOB));
        assert(get32(m + 832) == 0 && get64(m + 832 + 8) == 0); /* TLS base ignored at size0 */
        if(memcmp(m, oldp, N2R_BLOCK)){for(unsigned z=0;z<N2R_BLOCK;z++)if(m[z]!=oldp[z]){fprintf(stderr,"TLS0 diff off=%u m=%02x old=%02x\n",z,m[z],oldp[z]);break;}}
        assert(!memcmp(m, oldp, N2R_BLOCK));
    }
    puts("PASS n2r TLS: v9 LOCAL_STORAGE shift/PACKED/base, both stage pointers, TLS0 identity");
}

static void test_mrt_targets(void)
{
    uint8_t m[N2R_BLOCK], oldp[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct n2r_render_targets rts;
    struct n2r_render_target t[3];
    memset(t, 0, sizeof(t));
    t[0] = (struct n2r_render_target){80,2,0,0,0x60000000ull,64,16,16,{0xff0000ffu,0,0,0}};
    t[1] = (struct n2r_render_target){80,2,0,0,0x61000000ull,64,16,16,{0xff00ff00u,0,0,0}};
    t[2] = (struct n2r_render_target){10,2,15,0,0x62000000ull,64,16,16,{0xffu,0,0,0}};
    rts.version = N2R_RENDER_TARGETS_VERSION; rts.count = 3;
    rts.sample_locations = a.command + 12288u; rts.targets = t;
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == 0);
    assert(!n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2, a.command + N2R_BLOCK + N2R_JOB));
    pan_unpack((const struct mali_framebuffer_parameters_packed *)m, FRAMEBUFFER_PARAMETERS, fb);
    assert(fb.render_target_count == 3);
    assert(fb.color_buffer_allocation == 3072u);
    assert(fb.sample_locations == a.command + 12288u);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m + 128), RGB_RENDER_TARGET, rt0);
    assert(rt0.writeback_buffer.base == 0x60000000ull && rt0.internal_buffer_offset == 0);
    assert(rt0.swizzle == 0x60a && rt0.write_enable);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m + 128 + 64), RGB_RENDER_TARGET, rt1);
    assert(rt1.writeback_buffer.base == 0x61000000ull && rt1.internal_buffer_offset == 1024);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m + 128 + 128), RGB_RENDER_TARGET, rt2);
    assert(rt2.writeback_buffer.base == 0x62000000ull && rt2.internal_buffer_offset == 2048);
    assert(rt2.writeback_format == MALI_COLOR_FORMAT_R8 && rt2.swizzle == 0x688);
    pan_unpack((const struct mali_draw_packed *)(m + N2R_JOB + 128), DRAW, dr0);
    assert(dr0.blend_count == 3 && dr0.flags_1.render_target_mask == 4u);
    assert(get32(m + 12288) == 0x00800080u);
    /* v4 with no MRT sidecar is the existing v1 packet. */
    assert(!n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, NULL, 2, a.command + N2R_BLOCK + N2R_JOB));
    assert(!n2r_build(oldp, &n2_preload_shader, &q, &a, 2, a.command + N2R_BLOCK + N2R_JOB));
    assert(!memcmp(m, oldp, N2R_BLOCK));
    /* Invalid MRT shape refuses before output mutation. */
    {
        uint8_t before[N2R_BLOCK]; struct n2r_render_target bad = t[2];
        memset(m, 0x5a, sizeof(m)); memcpy(before, m, sizeof(m));
        bad.width = 8; t[2] = bad;
        assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
        assert(n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        t[2] = bad; t[2] = (struct n2r_render_target){10,2,15,0,0x62000000ull,64,16,16,{0xffu,0,0,0}};
    }
    puts("PASS n2r v4 3-target MRT descriptors/blends/tile offsets vs v9 oracle");
}


static void test_mrt_per_target_blends(void)
{
    uint8_t m[N2R_BLOCK], base[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct n2r_render_targets rts;
    struct n2r_render_target t[3];
    struct tegu_blend_inputs bl[3];

    memset(t, 0, sizeof(t));
    memset(bl, 0, sizeof(bl));
    t[0] = (struct n2r_render_target){80,2,15,0,0x60000000ull,64,16,16,{0xff0000ffu,0,0,0}};
    t[1] = (struct n2r_render_target){80,2,0,0,0x61000000ull,64,16,16,{0xff00ff00u,0,0,0}};
    t[2] = (struct n2r_render_target){10,2,15,0,0x62000000ull,64,16,16,{0xffu,0,0,0}};
    /* source-over on RT0, disabled/no-write on RT1, opaque R8 on RT2 */
    bl[0] = (struct tegu_blend_inputs){1,1,0,1,5,0,1,5,15};
    bl[1] = (struct tegu_blend_inputs){1,0,0,1,0,0,1,0,0};
    bl[2] = (struct tegu_blend_inputs){1,0,0,1,0,0,1,0,15};

    memset(&rts, 0, sizeof(rts));
    rts.version = N2R_RENDER_TARGETS_VERSION; rts.count = 3;
    rts.sample_locations = a.command + 12288u; rts.targets = t; rts.blends = NULL;
    assert(!n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts));
    assert(!n2r_build_v4(base, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2,
                        a.command + N2R_BLOCK + N2R_JOB));

    rts.version = N2R_RENDER_TARGETS_BLEND_VERSION; rts.blends = bl;
    assert(!n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts));
    assert(!n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2,
                        a.command + N2R_BLOCK + N2R_JOB));
    assert(memcmp(m, base, N2R_BLOCK)); /* per-target states are material */

    pan_unpack((const struct mali_blend_packed *)(m + 928), BLEND, b0);
    pan_unpack((const struct mali_blend_packed *)(m + 944), BLEND, b1);
    pan_unpack((const struct mali_blend_packed *)(m + 960), BLEND, b2);
    struct mali_blend_packed p0, p1, p2;
    MALI_BLEND_pack(&p0, &b0); MALI_BLEND_pack(&p1, &b1); MALI_BLEND_pack(&p2, &b2);
    assert(!memcmp(m + 928, &p0, MALI_BLEND_LENGTH));
    assert(!memcmp(m + 944, &p1, MALI_BLEND_LENGTH));
    assert(!memcmp(m + 960, &p2, MALI_BLEND_LENGTH));
    assert(b0.load_destination && b0.equation.color_mask == 15);
    assert(b1.load_destination && b1.equation.color_mask == 0);
    assert(!b2.load_destination && b2.equation.color_mask == 15);
    assert(b0.internal.fixed_function.rt == 0 &&
           b1.internal.fixed_function.rt == 1 &&
           b2.internal.fixed_function.rt == 2);
    assert(b0.equation.rgb.b != b2.equation.rgb.b ||
           b0.equation.rgb.c != b2.equation.rgb.c);
    assert(memcmp(m + 928, m + 960, MALI_BLEND_LENGTH));
    pan_unpack((const struct mali_draw_packed *)(m + N2R_JOB + 128), DRAW, dr0);
    assert(dr0.blend == a.command + 928 && dr0.blend_count == 3 &&
           dr0.flags_1.render_target_mask == 5u);

    /* Version 1 ignores the new pointer and keeps the old packet. */
    rts.version = N2R_RENDER_TARGETS_VERSION; rts.blends = bl;
    assert(!n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2,
                        a.command + N2R_BLOCK + N2R_JOB));
    assert(!memcmp(m, base, N2R_BLOCK));

    /* Version 2 never falls back or writes on invalid/mismatched blends. */
    {
        uint8_t before[N2R_BLOCK];
        struct n2r_render_targets bad = rts;
        memset(m, 0x5a, sizeof(m)); memcpy(before, m, sizeof(m));
        bad.version = N2R_RENDER_TARGETS_BLEND_VERSION; bad.blends = NULL;
        assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &bad) == -1);
        assert(n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        bad.blends = bl; bl[1].write_mask = 15;
        assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &bad) == -1);
        assert(n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        bl[1].write_mask = 0; bl[2].rgb_op = 9;
        assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &bad) == -1);
        assert(n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &bad, 2, 0) == -1);
        assert(!memcmp(m, before, sizeof(m)));
        bl[2].rgb_op = 0;
    }
    puts("PASS n2r v4 per-target BLEND descriptors: distinct equations/masks, v9 pack round-trip");
}


static void test_mrt_msaa(void)
{
    uint8_t m[N2R_BLOCK], before[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct n2r_render_targets rts;
    struct n2r_render_target t[3];
    struct n2r_resolve_target r[3];
    uint32_t samples[3] = {4, 4, 4};

    memset(t, 0, sizeof(t)); memset(r, 0, sizeof(r));
    t[0] = (struct n2r_render_target){80,2,15,0,0x60000000ull,64,16,16,{0xff0000ffu,0,0,0}};
    t[1] = (struct n2r_render_target){80,2,15,0,0x61000000ull,64,16,16,{0xff00ff00u,0,0,0}};
    t[2] = (struct n2r_render_target){10,2,15,0,0x62000000ull,64,16,16,{0xffu,0,0,0}};
    r[2] = (struct n2r_resolve_target){0x63000000ull,10,64,16,16,0};
    memset(&rts, 0, sizeof(rts));
    rts.version = N2R_RENDER_TARGETS_MSAA_VERSION; rts.count = 3;
    rts.sample_locations = a.command + 12288u; rts.targets = t;
    rts.blends = NULL; rts.sample_counts = samples; rts.resolves = r;
    assert(!n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts));
    assert(!n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2,
                        a.command + N2R_BLOCK + N2R_JOB));
    pan_unpack((const struct mali_framebuffer_parameters_packed *)m,
               FRAMEBUFFER_PARAMETERS, fb0);
    assert(fb0.sample_count == 4 &&
           fb0.sample_pattern == MALI_SAMPLE_PATTERN_ROTATED_4X_GRID);
    assert(fb0.color_buffer_allocation == 12288u);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m + 128),
               RGB_RENDER_TARGET, rt0);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m + 128 + 64),
               RGB_RENDER_TARGET, rt1);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m + 128 + 128),
               RGB_RENDER_TARGET, rt2);
    assert(rt0.internal_buffer_offset == 0 && rt1.internal_buffer_offset == 4096 &&
           rt2.internal_buffer_offset == 8192);
    assert(rt0.writeback_msaa == MALI_MSAA_LAYERED &&
           rt1.writeback_msaa == MALI_MSAA_LAYERED &&
           rt2.writeback_msaa == MALI_MSAA_AVERAGE);
    assert(rt0.writeback_buffer.base == 0x60000000ull &&
           rt2.writeback_buffer.base == 0x63000000ull &&
           rt2.writeback_buffer.row_stride == 64);
    assert(get32(m + 12288) == 0x00500070u &&
           get32(m + 12288 + 4) == 0x007000b0u &&
           get32(m + 12288 + 8) == 0x00900050u &&
           get32(m + 12288 + 12) == 0x00b00090u &&
           get32(m + 12288 + 32 * 4) == 0x00800080u);
    pan_unpack((const struct mali_draw_packed *)(m + N2R_JOB + 128), DRAW, dr);
    assert(dr.blend_count == 3 && dr.flags_1.render_target_mask == 7u);

    /* Version 1/2 bytes remain untouched by the new fields. */
    memset(&rts, 0, sizeof(rts));
    rts.version = N2R_RENDER_TARGETS_VERSION; rts.count = 3;
    rts.sample_locations = a.command + 12288u; rts.targets = t;
    assert(!n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts));
    assert(!n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2,
                        a.command + N2R_BLOCK + N2R_JOB));

    /* Invalid version3 combinations must not mutate output. */
    rts.version = N2R_RENDER_TARGETS_MSAA_VERSION; rts.sample_counts = samples; rts.resolves = r;
    memset(m, 0x5a, sizeof(m)); memcpy(before, m, sizeof(m));
    samples[0] = 2;
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
    assert(n2r_build_v4(m, &n2_preload_shader, &q, &a, NULL, NULL, &rts, 2, 0) == -1);
    assert(!memcmp(m, before, sizeof(m)));
    samples[0] = 4; samples[1] = 1;
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
    samples[1] = 4; rts.sample_counts = NULL;
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
    rts.sample_counts = samples; r[2].address = 0;
    samples[0] = 1; samples[1] = 1; samples[2] = 1; r[2].address = 0x63000000ull;
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
    /* restore a valid v3 shape */
    samples[0] = samples[1] = samples[2] = 4;
    r[2] = (struct n2r_resolve_target){0x63000000ull,10,64,16,16,0};
    assert(!n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts));
    r[2].width = 8;
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
    r[2].width = 16; r[2].format = 80; /* target is R8 */
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
    r[2].format = 10; r[2].address = t[0].address;
    assert(n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts) == -1);
    r[2].address = 0x63000000ull;
    assert(!n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts));
    rts.resolves = NULL; /* sample4 without resolve is LAYERED, valid */
    assert(!n2r_validate_v4(&n2_preload_shader, &q, NULL, NULL, &rts));
    puts("PASS n2r v3 MSAA: sample_count/pattern, tile allocation, resolve writeback, LUT");
}


static void test_t2_formats(void)
{
    uint8_t m[N2R_BLOCK], before[N2R_BLOCK];
    struct tegu_generic_draw_request q = request(16, 16, 64, 80, 1024);
    struct n2r_addresses a = addresses();
    struct n2r_render_targets rts;
    struct n2r_render_target t[3];
    uint32_t clears[3][4];

    assert(n2r_bpp(90)==4 && n2r_bpp(94)==4 && n2r_bpp(65)==4 &&
           n2r_bpp(25)==2 && n2r_bpp(125)==16 && n2r_bpp(55)==4 &&
           n2r_bpp(30)==2 && n2r_bpp(110)==8 && n2r_bpp(115)==8);
    assert(n2r_bpp(552)==0 && n2r_bpp(554)==0); /* XR not represented exactly */
    memset(t,0,sizeof(t)); memset(clears,0,sizeof(clears));
    t[0]=(struct n2r_render_target){90,2,15,0,0x60000000ull,64,16,16,{0,0,0,0}};
    t[1]=(struct n2r_render_target){65,2,15,0,0x61000000ull,64,16,16,{0,0,0,0}};
    t[2]=(struct n2r_render_target){110,2,15,0,0x62000000ull,128,16,16,{0,0,0,0}};
    const double c[4]={0.25,0.5,0.75,1.0};
    { uint32_t x[4]; assert(n2r_clear(552,c,x)==-1); }
    assert(!n2r_clear(90,c,clears[0]));
    assert(!n2r_clear(65,c,clears[1]));
    assert(!n2r_clear(110,c,clears[2]));
    memcpy(t[0].clear_words,clears[0],sizeof(clears[0]));
    memcpy(t[1].clear_words,clears[1],sizeof(clears[1]));
    memcpy(t[2].clear_words,clears[2],sizeof(clears[2]));
    memset(&rts,0,sizeof(rts));
    rts.version=N2R_RENDER_TARGETS_VERSION;rts.count=3;
    rts.sample_locations=a.command+12288u;rts.targets=t;
    assert(!n2r_validate_v4(&n2_preload_shader,&q,NULL,NULL,&rts));
    assert(!n2r_build_v4(m,&n2_preload_shader,&q,&a,NULL,NULL,&rts,2,
                        a.command+N2R_BLOCK+N2R_JOB));
    pan_unpack((const struct mali_framebuffer_parameters_packed *)m,
               FRAMEBUFFER_PARAMETERS,fb2);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m+128),
               RGB_RENDER_TARGET,tr0);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m+128+64),
               RGB_RENDER_TARGET,tr1);
    pan_unpack((const struct mali_rgb_render_target_packed *)(m+128+128),
               RGB_RENDER_TARGET,tr2);
    assert(fb2.color_buffer_allocation==4096u); /* 2*4*256 + 8*256 */
    assert(tr0.internal_format==MALI_COLOR_BUFFER_INTERNAL_FORMAT_R10G10B10A2 &&
           tr0.writeback_format==MALI_COLOR_FORMAT_R10G10B10A2 && tr0.swizzle==0x688);
    assert(tr1.internal_format==MALI_COLOR_BUFFER_INTERNAL_FORMAT_RG16F &&
           tr1.writeback_float_format==MALI_FLOAT_COLOR_FORMAT_RG16F);
    assert(tr2.internal_format==MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW64 &&
           tr2.writeback_format==MALI_COLOR_FORMAT_RAW64);
    assert(tr0.internal_buffer_offset==0 && tr1.internal_buffer_offset==1024 &&
           tr2.internal_buffer_offset==2048);
    assert(clears[0][0]==(0x100u|(0x200u<<10)|(0x2ffu<<20)|(3u<<30)));
    assert(clears[1][0]==0x38003400u); /* half {.25,.5} */
    /* Exercise every admitted T2 target mapping in a one-target job. */
    {
        static const uint32_t forms[] = {94,25,55,125,30,110};
        for (unsigned z=0; z<sizeof(forms)/sizeof(forms[0]); z++) {
            uint32_t f=forms[z], row=(16u*n2r_bpp(f)+63u)&~63u;
            struct n2r_render_target one=(struct n2r_render_target){
                f,2,15,0,0x64000000ull,row,16,16,{0,0,0,0}};
            assert(!n2r_clear(f,c,one.clear_words));
            rts.version=N2R_RENDER_TARGETS_VERSION;rts.count=1;rts.targets=&one;
            rts.sample_locations=0;rts.blends=NULL;
            assert(!n2r_validate_v4(&n2_preload_shader,&q,NULL,NULL,&rts));
            assert(!n2r_build_v4(m,&n2_preload_shader,&q,&a,NULL,NULL,&rts,2,
                                a.command+N2R_BLOCK+N2R_JOB));
            pan_unpack((const struct mali_rgb_render_target_packed *)(m+128),
                       RGB_RENDER_TARGET,trx);
            assert(trx.writeback_buffer.row_stride==row);
            assert(trx.swizzle==(f==94?0x60a:0x688));
            assert(trx.internal_format==(f==94?MALI_COLOR_BUFFER_INTERNAL_FORMAT_R10G10B10A2:
                                         f==25?MALI_COLOR_BUFFER_INTERNAL_FORMAT_R16F:
                                         f==55?MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW32:
                                         f==125?MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW128:
                                         f==30?MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW16:
                                         MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW64));
        }
    }
    /* Unsupported target formats refuse before mutation. */
    rts.version=N2R_RENDER_TARGETS_VERSION;rts.count=1;rts.targets=t;
    rts.sample_locations=0;rts.blends=NULL;rts.sample_counts=NULL;rts.resolves=NULL;
    memcpy(before,m,sizeof(before));
    t[0].format=552;
    assert(n2r_validate_v4(&n2_preload_shader,&q,NULL,NULL,&rts)==-1);
    assert(n2r_build_v4(m,&n2_preload_shader,&q,&a,NULL,NULL,&rts,2,0)==-1);
    assert(!memcmp(m,before,sizeof(m)));
    puts("PASS n2r T2 formats: 10-bit/RG16F/RGBA16 target mapping and clear packing");
}

int main(void)
{
    test_old_exact();
    test_mips();
    test_vk83_attribute();
    test_cube();
    test_v2_formats();
    test_sparse_bindings();
    test_sampler_only();
    test_draw_options();
    test_thread_storage();
    test_mrt_targets();
    test_mrt_per_target_blends();
    test_mrt_msaa();
    test_t2_formats();
    test_oversized_binding_count();
    puts("PASS n2r v2 host suite");
    return 0;
}
