/* Host tests for the shared arch9 texture/sampler helper.
 *
 * Mesa words come from pinned Mesa 81daea6 genxml/v10 pack macros in this
 * test, never from the implementation under test.  No GPU is used. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "genxml/v9_pack.h"
#include "nord2_texture_resources.h"
#include "texture_state_mesa_golden.h"

static uint32_t get32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t get64(const void *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static unsigned failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void mesa_texture(uint32_t out[8], uint32_t dim, uint32_t fmtword,
                         uint32_t w, uint32_t h, uint32_t levels,
                         uint32_t array_size, uint32_t samples, uint32_t depth,
                         uint64_t surfaces, uint32_t swizzle, int interleave,
                         float min_lod, float max_lod)
{
    pan_cast_and_pack(out, TEXTURE, cfg) {
        cfg.dimension = dim;
        cfg.format = fmtword;
        cfg.width = w;
        cfg.height = h;
        cfg.swizzle = swizzle;
        cfg.texel_interleave = interleave;
        cfg.levels = levels;
        cfg.array_size = array_size;
        if (dim == MALI_TEXTURE_DIMENSION_3D)
            cfg.depth = depth;
        else
            cfg.sample_count = samples;
        cfg.surfaces = surfaces;
        cfg.minimum_lod = min_lod;
        cfg.maximum_lod = max_lod;
    }
}

static void mesa_generic(uint32_t out[8], uint64_t ptr, uint32_t size,
                         uint32_t row_stride, uint32_t slice_stride,
                         uint32_t w, uint32_t h, uint32_t clump)
{
    pan_cast_and_pack(out, GENERIC_PLANE, cfg) {
        cfg.clump_ordering = MALI_CLUMP_ORDERING_LINEAR;
        cfg.clump_format = clump;
        cfg.size = size;
        cfg.pointer = ptr;
        cfg.row_stride = row_stride;
        cfg.slice_stride = slice_stride;
        (void)w; (void)h; /* arch9 Generic Plane has no extent fields */
    }
}

static void mesa_astc(uint32_t out[8], uint64_t ptr, uint32_t size,
                      uint32_t row_stride, uint32_t slice_stride,
                      uint32_t w, uint32_t h, uint32_t bw, uint32_t bh,
                      int hdr, int wide)
{
    pan_cast_and_pack(out, ASTC_2D_PLANE, cfg) {
        cfg.clump_ordering = MALI_CLUMP_ORDERING_LINEAR;
        cfg.decode_hdr = hdr;
        cfg.decode_wide = wide;
        cfg.block_width = bw;
        cfg.block_height = bh;
        cfg.size = size;
        cfg.pointer = ptr;
        cfg.row_stride = row_stride;
        cfg.slice_stride = slice_stride;
        (void)w; (void)h; /* arch9 ASTC 2D Plane has no extent fields */
    }
}

static void base_layout(struct n2t_texture_layout *l, uint32_t kind, uint32_t format,
                        uint32_t w, uint32_t h, uint32_t d, uint32_t levels,
                        uint32_t layers, uint32_t samples, uint32_t access)
{
    memset(l, 0, sizeof(*l));
    l->version = N2T_TEXTURE_RESOURCES_VERSION;
    l->kind = kind; l->format = format;
    l->width = w; l->height = h; l->depth = d;
    l->levels = levels; l->layers = layers; l->samples = samples;
    l->array_length = layers; l->access = access;
    l->usage = 1; l->storage_mode = 1;
}

static void level(struct n2t_texture_layout *l, uint32_t i, uint64_t offset,
                  uint32_t row_stride, uint32_t slice_stride, uint32_t layer_stride)
{
    l->level[i].offset = offset;
    l->level[i].row_stride = row_stride;
    l->level[i].slice_stride = slice_stride;
    l->level[i].layer_stride = layer_stride;
    l->level[i].width = (uint32_t)n2t_minify(l->width, i);
    l->level[i].height = (uint32_t)n2t_minify(l->height, i);
    l->level[i].depth = (uint32_t)n2t_minify(l->depth, i);
}

static const uint64_t BACKING = 0x21000000ull;
static const uint64_t DESC = 0x20000000ull;
static const uint64_t PAYLOAD = 0x20001000ull;

static int pack(const struct n2t_texture_layout *l, struct n2t_packed_texture *p,
                uint8_t *payload, size_t capacity)
{
    return n2t_layout_pack_sampled(l, BACKING, l->access & N2T_ACCESS_SAMPLE ? 0x10000000ull : 0x10000000ull,
                                   DESC, PAYLOAD, payload, capacity, p);
}

static void check_texture(uint32_t got[8], uint32_t want[8], const char *what)
{
    for (unsigned i = 0; i < 8; i++)
        CHECK(got[i] == want[i], "%s texture word%u got=%08x want=%08x", what, i, got[i], want[i]);
}

static void check_plane(const uint8_t *got, const uint32_t want[8], const char *what, unsigned n)
{
    for (unsigned i = 0; i < 8; i++) {
        uint32_t v; memcpy(&v, got + 4u * i, 4);
        CHECK(v == want[i], "%s plane%u word%u got=%08x want=%08x", what, n, i, v, want[i]);
    }
}

static void test_qualified_golden(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[4096];
    uint32_t want[8];
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 1024, 0);
    l.level[0].layer_stride = 0;
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "qualified pack");
    uint32_t dwords[8];
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    /* Existing 16x16 RGBA8 golden page: descriptor at 0x120, plane 0x140. */
    memset(want, 0, sizeof(want));
    unsigned char page[4096];
    tegu_texture_mesa_golden_page(DESC, BACKING, page);
    for (unsigned i = 0; i < 8; i++) memcpy(&want[i], page + 0x120 + 4u * i, 4);
    want[4] = (uint32_t)PAYLOAD; want[5] = (uint32_t)(PAYLOAD >> 32);
    check_texture(dwords, want, "golden-page");
    for (unsigned i = 0; i < 8; i++) memcpy(&want[i], page + 0x140 + 4u * i, 4);
    want[2] = (uint32_t)BACKING; want[3] = (uint32_t)(BACKING >> 32);
    CHECK(want[7] != 0, "legacy v10 extent is separate");
    want[7] = 0; /* arch9 v9 plane has no word7 */
    check_plane(payload, want, "golden-page", 0);
    CHECK(p.descriptor_address == DESC && p.payload_address == PAYLOAD, "packed addresses");
    CHECK(p.plane_count == 1 && p.payload_bytes == 32, "packed shape");
    {
        uint32_t tail; memcpy(&tail, payload + 28, 4);
        CHECK(tail == 0, "v9 generic word7 zero");
    }
}

static void test_2d_mips(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[4096];
    uint32_t want[8], dwords[8];
    base_layout(&l, N2T_KIND_2D, 70, 64, 32, 1, 3, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 256, 0, 0);
    level(&l, 1, 8192, 128, 0, 0);
    level(&l, 2, 10240, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "mip pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (187u << 12), 64, 32, 3, 1, 1, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 2.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "mips");
    CHECK(p.plane_count == 3 && p.payload_bytes == 96, "mip plane count");
    for (unsigned i = 0; i < 3; i++) {
        uint32_t h = 32u >> i, w = 64u >> i, row = 256u >> i;
        mesa_generic(want, BACKING + (i == 0 ? 0 : i == 1 ? 8192 : 10240), row * h,
                     row, 0, w, h, 2);
        check_plane(payload + 32u * i, want, "mips", i);
    }
}

static void test_arrays_and_cubes(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[8192];
    uint32_t want[8], dwords[8];
    /* 2D array: 3 layers, 2 levels; whole mip chain stride 1536. */
    base_layout(&l, N2T_KIND_2D_ARRAY, 70, 16, 16, 1, 2, 3, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 1536);
    level(&l, 1, 1024, 64, 0, 1536);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "array pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (187u << 12), 16, 16, 2, 3, 1, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 1.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "2d array");
    CHECK(p.plane_count == 6, "2d array plane count");
    for (unsigned layer = 0; layer < 3; layer++)
        for (unsigned lev = 0; lev < 2; lev++) {
            unsigned n = layer * 2 + lev;
            uint32_t row = lev ? 64u : 64u, h = lev ? 8u : 16u;
            uint64_t ptr = BACKING + layer * 1536u + (lev ? 1024u : 0u);
            mesa_generic(want, ptr, row * h, row, 0, 16u >> lev, h, 2);
            check_plane(payload + 32u * n, want, "2d array", n);
        }
    /* Cube: faces are layers, array_size is cube count. */
    base_layout(&l, N2T_KIND_CUBE, 70, 8, 8, 1, 1, 6, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 1024);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "cube pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_CUBE, (187u << 12), 8, 8, 1, 1, 1, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "cube");
    CHECK(p.plane_count == 6, "cube plane count");
    for (unsigned f = 0; f < 6; f++) {
        mesa_generic(want, BACKING + f * 1024u, 64u * 8u, 64, 0, 8, 8, 2);
        check_plane(payload + 32u * f, want, "cube", f);
    }
    /* Cube array: 2 cubes = 12 faces, array_size 2. */
    base_layout(&l, N2T_KIND_CUBE_ARRAY, 70, 8, 8, 1, 1, 12, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 1024);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "cube array pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_CUBE, (187u << 12), 8, 8, 1, 2, 1, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "cube array");
    CHECK(p.plane_count == 12, "cube array plane count");
}

static void test_3d_ms(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[8192];
    uint32_t want[8], dwords[8];
    /* 3D: one plane per level, depth field, z-slice stride. */
    base_layout(&l, N2T_KIND_3D, 70, 16, 16, 4, 2, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 1024, 0);
    level(&l, 1, 4096, 64, 512, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "3d pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_3D, (187u << 12), 16, 16, 2, 1, 1, 4,
                 PAYLOAD, 0x688, 0, 0.0f, 1.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "3d");
    CHECK(p.plane_count == 2, "3d plane count");
    mesa_generic(want, BACKING, 64u * 16u + 3u * 1024u, 64, 1024, 16, 16, 2);
    check_plane(payload, want, "3d", 0);
    mesa_generic(want, BACKING + 4096, 64u * 8u + 1u * 512u, 64, 512, 8, 8, 2);
    check_plane(payload + 32, want, "3d", 1);
    /* 2D MS: one fused plane per level, sample count log2, sample stride. */
    base_layout(&l, N2T_KIND_2D_MS, 70, 16, 16, 1, 1, 1, 4, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 1024, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "ms pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (187u << 12), 16, 16, 1, 1, 4, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "ms");
    mesa_generic(want, BACKING, 64u * 16u + 3u * 1024u, 64, 1024, 16, 16, 2);
    check_plane(payload, want, "ms", 0);
    /* 2D MS array: one plane per layer, array size, sample stride. */
    base_layout(&l, N2T_KIND_2D_MS_ARRAY, 70, 16, 16, 1, 1, 2, 2, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 1024, 2048);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "ms array pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (187u << 12), 16, 16, 1, 2, 2, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "ms array");
    CHECK(p.plane_count == 2, "ms array plane count");
    mesa_generic(want, BACKING, 64u * 16u + 1u * 1024u, 64, 1024, 16, 16, 2);
    check_plane(payload, want, "ms array", 0);
    mesa_generic(want, BACKING + 2048, 64u * 16u + 1u * 1024u, 64, 1024, 16, 16, 2);
    check_plane(payload + 32, want, "ms array", 1);
}

static void test_compressed(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[8192];
    uint32_t want[8], dwords[8];
    /* ASTC 8x8 LDR: decode RGBA16F, wide, blocks 4x4, rows 4. */
    base_layout(&l, N2T_KIND_2D, 212, 32, 32, 1, 2, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    level(&l, 1, 256, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "astc ldr");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (223u << 12), 32, 32, 2, 1, 1, 1,
                 PAYLOAD, 0x688, 1, 0.0f, 1.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "astc ldr");
    mesa_astc(want, BACKING, 64u * 4u, 64, 0, 32, 32, 4, 4, 0, 1);
    check_plane(payload, want, "astc ldr", 0);
    {
        uint32_t tail; memcpy(&tail, payload + 28, 4);
        CHECK(tail == 0, "v9 astc word7 zero");
    }
    mesa_astc(want, BACKING + 256, 64u * 2u, 64, 0, 16, 16, 4, 4, 0, 1);
    check_plane(payload + 32, want, "astc ldr", 1);
    /* ASTC sRGB: RGBA8 sRGB interchange, no wide bit. */
    base_layout(&l, N2T_KIND_2D, 194, 16, 16, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "astc srgb");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (187u << 12) | (1u << 20), 16, 16, 1, 1, 1, 1,
                 PAYLOAD, 0x688, 1, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "astc srgb");
    mesa_astc(want, BACKING, 64u * 2u, 64, 0, 16, 16, 4, 4, 0, 0);
    check_plane(payload, want, "astc srgb", 0);
    /* ASTC HDR: RGBA16F, HDR and wide. */
    base_layout(&l, N2T_KIND_2D, 230, 16, 16, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "astc hdr");
    mesa_astc(want, BACKING, 64u * 2u, 64, 0, 16, 16, 4, 4, 1, 1);
    check_plane(payload, want, "astc hdr", 0);
    /* ETC2 RGBA8: RGBA8 interchange, ETC clump 37, interleave. */
    base_layout(&l, N2T_KIND_2D, 178, 32, 32, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 128, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "etc2");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (187u << 12), 32, 32, 1, 1, 1, 1,
                 PAYLOAD, 0x688, 1, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "etc2");
    mesa_generic(want, BACKING, 128u * 8u, 128, 0, 32, 32, 37);
    check_plane(payload, want, "etc2", 0);
    /* ETC2 RGB8 and RGB8A1 are 8-byte 4x4 blocks, not 16. */
    base_layout(&l, N2T_KIND_2D, 180, 32, 32, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "etc2 rgb8");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, (187u << 12) | 16u, 32, 32, 1, 1, 1, 1,
                 PAYLOAD, 0x688, 1, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "etc2 rgb8");
    mesa_generic(want, BACKING, 64u * 8u, 64, 0, 32, 32, 32);
    check_plane(payload, want, "etc2 rgb8", 0);
    base_layout(&l, N2T_KIND_2D, 182, 32, 32, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "etc2 rgb8a1");
    mesa_generic(want, BACKING, 64u * 8u, 64, 0, 32, 32, 38);
    check_plane(payload, want, "etc2 rgb8a1", 0);
    /* Compressed kinds outside 2D family refuse. */
    struct n2t_texture_layout bad;
    uint8_t before[4096], after[4096];
    memset(before, 0x5a, sizeof(before)); memcpy(after, before, sizeof(after));
    base_layout(&bad, N2T_KIND_3D, 212, 32, 32, 4, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&bad, 0, 0, 64, 1024, 0);
    CHECK(pack(&bad, &p, after, sizeof(after)) == N2T_E_UNSUPPORTED, "astc 3d refusal");
    CHECK(!memcmp(before, after, sizeof(before)), "astc 3d output unchanged");
}

static void test_formats(void)
{
    struct { uint32_t metal; uint32_t word; uint32_t clump; uint32_t bpp; uint32_t row; } cases[] = {
        {10, (163u << 12) | 16u, 0, 1, 64},
        {30, (171u << 12) | 16u, 1, 2, 64},
        {70, 187u << 12, 2, 4, 64},
        {71, (187u << 12) | (1u << 20), 2, 4, 64},
        {73, 155u << 12, 2, 4, 64},
        {11, (163u << 12) | 16u | (1u << 20), 0, 1, 64},
        {80, (187u << 12) | 4u, 2, 4, 64},
        {115, 223u << 12, 3, 8, 128},
        {105, (175u << 12) | 16u, 3, 8, 128},
        {55, (167u << 12) | 16u, 2, 4, 64},
        {90, 67u << 12, 2, 4, 64},
        {250, 75u << 12, 1, 2, 64},
        {252, 76u << 12, 2, 4, 64},
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct n2t_texture_layout l;
        struct n2t_packed_texture p;
        uint8_t payload[256];
        uint32_t want[8], dwords[8];
        uint32_t w = 16, h = 4;
        base_layout(&l, N2T_KIND_2D, cases[i].metal, w, h, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
        level(&l, 0, 0, cases[i].row, 0, 0);
        CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "format %u pack", cases[i].metal);
        mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, cases[i].word, w, h, 1, 1, 1, 1,
                     PAYLOAD, 0x688, 0, 0.0f, 0.0f);
        memcpy(dwords, p.descriptor_words, sizeof(dwords));
        check_texture(dwords, want, "format");
        mesa_generic(want, BACKING, cases[i].row * h, cases[i].row, 0, w, h, cases[i].clump);
        check_plane(payload, want, "format", i);
    }
}

static void test_view_format(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[256];
    base_layout(&l, N2T_KIND_2D, 115, 16, 4, 1, 1, 1, 1, N2T_ACCESS_STORAGE_WRITE);
    l.view_format = 113; /* RGBA16Uint view of RGBA16Float allocation */
    level(&l, 0, 0, 128, 0, 0);
    CHECK(n2t_layout_pack_storage(&l, BACKING, 0x1000, DESC, PAYLOAD,
                                  payload, sizeof(payload), &p) == N2T_OK, "view uint");
    CHECK(((p.descriptor_words[0] >> 22) & 0xffu) == 156u, "view format code");
    l.view_format = 73; /* RGBA8Uint view of RGBA16Float: geometry mismatch */
    CHECK(n2t_layout_pack_storage(&l, BACKING, 0x1000, DESC, PAYLOAD,
                                  payload, sizeof(payload), &p) == N2T_E_UNSUPPORTED, "view mismatch");
    base_layout(&l, N2T_KIND_2D, 70, 16, 4, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    l.view_format = 71; level(&l, 0, 0, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "view srgb");
    CHECK(((p.descriptor_words[0] >> 30) & 1u) == 1u, "view srgb bit");
}

static void test_storage(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[256];
    uint32_t want[8], dwords[8];
    base_layout(&l, N2T_KIND_2D, 73, 16, 4, 1, 1, 1, 1, N2T_ACCESS_STORAGE_WRITE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(n2t_layout_pack_storage(&l, BACKING, 0x1000, DESC, PAYLOAD,
                                  payload, sizeof(payload), &p) == N2T_OK, "storage");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_2D, 155u << 12, 16, 4, 1, 1, 1, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "storage");
    mesa_generic(want, BACKING, 64u * 4u, 64, 0, 16, 4, 2);
    check_plane(payload, want, "storage", 0);
    /* Storage refuses compressed, depth, MS, and missing write/read access. */
    base_layout(&l, N2T_KIND_2D, 212, 32, 32, 1, 1, 1, 1, N2T_ACCESS_STORAGE_WRITE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(n2t_layout_pack_storage(&l, BACKING, 0x1000, DESC, PAYLOAD,
                                  payload, sizeof(payload), &p) == N2T_E_UNSUPPORTED, "storage astc");
    base_layout(&l, N2T_KIND_2D, 70, 16, 4, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(n2t_layout_pack_storage(&l, BACKING, 0x1000, DESC, PAYLOAD,
                                  payload, sizeof(payload), &p) == N2T_E_ACCESS, "storage access");
}


static void test_1d_and_views(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[4096];
    uint32_t want[8], dwords[8];
    base_layout(&l, N2T_KIND_1D, 10, 64, 1, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "1d pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_1D, (163u << 12) | 16u, 64, 1, 1, 1, 1, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "1d");
    /* 1D array with 3 layers. */
    base_layout(&l, N2T_KIND_1D_ARRAY, 10, 64, 1, 1, 1, 3, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 128);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "1d array pack");
    mesa_texture(want, MALI_TEXTURE_DIMENSION_1D, (163u << 12) | 16u, 64, 1, 1, 3, 1, 1,
                 PAYLOAD, 0x688, 0, 0.0f, 0.0f);
    memcpy(dwords, p.descriptor_words, sizeof(dwords));
    check_texture(dwords, want, "1d array");
    CHECK(p.plane_count == 3, "1d array planes");
    for (unsigned i = 0; i < 3; i++) {
        mesa_generic(want, BACKING + 128u * i, 64, 64, 0, 64, 1, 0);
        check_plane(payload + 32u * i, want, "1d array", i);
    }
    /* A nonzero physical base level is informational: dimensions are already
     * the view's, and descriptor levels/maxlod use the view count. */
    base_layout(&l, N2T_KIND_2D, 70, 8, 8, 1, 2, 1, 1, N2T_ACCESS_SAMPLE);
    l.first_level = 2;
    level(&l, 0, 0, 64, 0, 0); level(&l, 1, 512, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "view base level");
    CHECK(((p.descriptor_words[2] >> 16) & 31u) == 1u, "view levels");
    CHECK(((p.descriptor_words[3] >> 16) & 0x1fffu) == 0x0100u, "view maxlod");
}

static void test_bounds(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[256];
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 17, 1, 1, N2T_ACCESS_SAMPLE);
    for (unsigned i = 0; i < 16; i++) level(&l, i, 4096ull * i, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_E_LEVELS, "levels>16");
    base_layout(&l, N2T_KIND_2D_ARRAY, 70, 16, 16, 1, 1, 257, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 1024);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_E_COUNT, "subresource cap");
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    l.first_level = 16;
    level(&l, 0, 0, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_E_LEVELS, "first level bound");
    base_layout(&l, N2T_KIND_2D, 70, 16384, 1, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 65536, 0, 0);
    CHECK(n2t_layout_pack_sampled(&l, BACKING, 0x1000, DESC, PAYLOAD,
                                  payload, sizeof(payload), &p) == N2T_E_SPAN, "backing bound");
}


static void test_coverage_matrix(void)
{
    unsigned supported = 0, refused = 0;
    for (uint32_t metal = 0; metal <= 600; metal++) {
        struct n2t_format_info f;
        int rc = n2t_format_query(metal, &f);
        if (rc == N2T_OK) supported++; else refused++;
    }
    CHECK(supported > 60 && refused > 400, "format matrix %u/%u", supported, refused);
    CHECK(n2t_format_query(41, &(struct n2t_format_info){0}) == N2T_E_FORMAT, "A1BGR5 refusal");
    CHECK(n2t_format_query(260, &(struct n2t_format_info){0}) == N2T_E_FORMAT, "combined depth refusal");
    CHECK(n2t_format_query(552, &(struct n2t_format_info){0}) == N2T_E_FORMAT, "XR refusal");
    struct {
        uint32_t kind, w, h, d, layers, samples, level_w, level_h, level_d;
        uint32_t row, slice, layer_stride, array_length;
        int expect;
    } kinds[] = {
        {N2T_KIND_1D, 16, 1, 1, 1, 1, 16, 1, 1, 64, 0, 0, 1, N2T_OK},
        {N2T_KIND_1D_ARRAY, 16, 1, 1, 3, 1, 16, 1, 1, 64, 0, 64, 3, N2T_OK},
        {N2T_KIND_2D, 16, 16, 1, 1, 1, 16, 16, 1, 64, 0, 0, 1, N2T_OK},
        {N2T_KIND_2D_ARRAY, 16, 16, 1, 3, 1, 16, 16, 1, 64, 0, 1024, 3, N2T_OK},
        {N2T_KIND_2D_MS, 16, 16, 1, 1, 4, 16, 16, 1, 64, 1024, 0, 1, N2T_OK},
        {N2T_KIND_CUBE, 8, 8, 1, 6, 1, 8, 8, 1, 64, 0, 512, 6, N2T_OK},
        {N2T_KIND_CUBE_ARRAY, 8, 8, 1, 12, 1, 8, 8, 1, 64, 0, 512, 12, N2T_OK},
        {N2T_KIND_3D, 8, 8, 4, 1, 1, 8, 8, 4, 64, 512, 0, 1, N2T_OK},
        {N2T_KIND_2D_MS_ARRAY, 16, 16, 1, 2, 2, 16, 16, 1, 64, 1024, 2048, 2, N2T_OK},
        {N2T_KIND_TEXTURE_BUFFER, 16, 1, 1, 1, 1, 16, 1, 1, 64, 0, 0, 1, N2T_E_UNSUPPORTED},
    };
    for (unsigned k = 0; k < sizeof(kinds)/sizeof(kinds[0]); k++) {
        struct n2t_texture_layout l;
        struct n2t_packed_texture p;
        uint8_t payload[4096];
        base_layout(&l, kinds[k].kind, 70, kinds[k].w, kinds[k].h, kinds[k].d, 1,
                    kinds[k].layers, kinds[k].samples, N2T_ACCESS_SAMPLE);
        l.array_length = kinds[k].array_length;
        l.level[0] = (struct n2t_level){0, kinds[k].row, kinds[k].slice,
            kinds[k].layer_stride, kinds[k].level_w, kinds[k].level_h, kinds[k].level_d};
        int rc = pack(&l, &p, payload, sizeof(payload));
        CHECK(rc == kinds[k].expect, "kind %u rc=%d want=%d", kinds[k].kind, rc, kinds[k].expect);
        if (rc == N2T_OK) CHECK(p.plane_count == kinds[k].layers, "kind %u planes", kinds[k].kind);
    }
    /* All level counts 1..16 are accepted for a 2D texture when the chain and
     * backing are provided. */
    for (uint32_t levels = 1; levels <= N2T_MAX_LEVELS; levels++) {
        struct n2t_texture_layout l;
        struct n2t_packed_texture p;
        uint8_t payload[4096];
        uint64_t offset = 0;
        base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, levels, 1, 1, N2T_ACCESS_SAMPLE);
        for (uint32_t i = 0; i < levels; i++) {
            uint32_t w = (uint32_t)n2t_minify(l.width, i);
            uint32_t h = (uint32_t)n2t_minify(l.height, i);
            l.level[i] = (struct n2t_level){offset, 64, 0, 0, w, h, 1};
            offset += (uint64_t)64 * h;
        }
        int rc = n2t_layout_pack_sampled(&l, BACKING, 0x100000, DESC, PAYLOAD,
                                         payload, sizeof(payload), &p);
        CHECK(rc == N2T_OK || rc == N2T_E_COUNT, "levels %u rc=%d", levels, rc);
    }
}


static void test_transport_mips_layers(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[8192];
    /* Five-level compute-style 2D chain. */
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 5, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    level(&l, 1, 1024, 64, 0, 0);
    level(&l, 2, 1536, 64, 0, 0);
    level(&l, 3, 1792, 64, 0, 0);
    level(&l, 4, 1920, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "5-level chain");
    CHECK(p.plane_count == 5, "5-level planes");
    CHECK(get64(payload + 32 * 4 + 8) == BACKING + 1920, "5-level last address");
    CHECK(get32(payload + 32 * 4 + 4) == 64, "5-level last size");
    /* 3 layers x 4 levels: layer-major payload and per-level spans. */
    base_layout(&l, N2T_KIND_2D_ARRAY, 70, 16, 16, 1, 4, 3, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 1920);
    level(&l, 1, 1024, 64, 0, 1920);
    level(&l, 2, 1536, 64, 0, 1920);
    level(&l, 3, 1792, 64, 0, 1920);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "array 4-level chain");
    CHECK(p.plane_count == 12, "array mip planes");
    CHECK(get64(payload + 32 * 4 + 8) == BACKING + 1920, "array layer1/level0");
    CHECK(get64(payload + 32 * 8 + 8) == BACKING + 3840, "array layer2/level0");
    CHECK(get32(payload + 32 * 7 + 4) == 64 * 2, "array last level size");
    /* Cube 6 faces x 3 levels. */
    base_layout(&l, N2T_KIND_CUBE, 70, 8, 8, 1, 3, 6, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 896);
    level(&l, 1, 512, 64, 0, 896);
    level(&l, 2, 768, 64, 0, 896);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "cube 3-level chain");
    CHECK(p.plane_count == 18, "cube mip faces");
    CHECK(get64(payload + 32 * 3 + 8) == BACKING + 896, "cube face1/level0");
    /* 3D depth slices, one plane per level. */
    base_layout(&l, N2T_KIND_3D, 70, 8, 8, 4, 3, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 512, 0);
    level(&l, 1, 2048, 64, 512, 0);
    level(&l, 2, 3072, 64, 512, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "3d 3-level chain");
    CHECK(p.plane_count == 3, "3d mip planes");
    CHECK(get32(payload + 32 + 4) == 64 * 4 + 512, "3d level1 span");
    /* More than 16 layers are supported when the total plane count fits the
     * bounded payload (Metal cases such as a 17-slice 2D array). */
    base_layout(&l, N2T_KIND_2D_ARRAY, 70, 16, 16, 1, 1, 17, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 1024);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_OK, "17-layer array");
    CHECK(p.plane_count == 17, "17-layer planes");
    /* Explicit caps. */
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 17, 1, 1, N2T_ACCESS_SAMPLE);
    for (unsigned i = 0; i < 16; i++) level(&l, i, 4096ull * i, 64, 0, 0);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_E_LEVELS, "level cap 16");
    base_layout(&l, N2T_KIND_2D_ARRAY, 70, 16, 16, 1, 1, 2049, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 1024);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_E_LAYERS, "layer cap 2048");
    base_layout(&l, N2T_KIND_2D_ARRAY, 70, 16, 16, 1, 16, 17, 1, N2T_ACCESS_SAMPLE);
    for (unsigned i = 0; i < 16; i++) level(&l, i, 4096ull * i, 64, 0, 65536);
    CHECK(pack(&l, &p, payload, sizeof(payload)) == N2T_E_COUNT, "subresource cap 256");
}

static void test_refusals(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t payload[4096], before[4096];
    memset(before, 0x5a, sizeof(before)); memcpy(payload, before, sizeof(before));
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
#define REFUSE(mut, want, what) do { struct n2t_texture_layout x = l; mut; memcpy(payload, before, sizeof(before)); \
    int rc = pack(&x, &p, payload, sizeof(payload)); CHECK(rc == (want), "%s got %d want %d", what, rc, want); \
    CHECK(!memcmp(payload, before, sizeof(before)) || rc == N2T_E_CAPACITY, "%s output changed", what); } while (0)
    REFUSE(x.version = 0, N2T_E_INVALID, "version");
    REFUSE(x.kind = 9, N2T_E_UNSUPPORTED, "buffer texture");
    REFUSE(x.format = 999, N2T_E_FORMAT, "unknown format");
    REFUSE(x.width = 0, N2T_E_DIMENSIONS, "width0");
    REFUSE(x.height = 0, N2T_E_DIMENSIONS, "height0");
    REFUSE(x.levels = 0, N2T_E_LEVELS, "levels0");
    REFUSE(x.layers = 0, N2T_E_LAYERS, "layers0");
    REFUSE(x.samples = 2, N2T_E_SAMPLES, "samples2 nonms");
    REFUSE(x.access = 0, N2T_E_ACCESS, "access0");
    REFUSE(x.usage = 16, N2T_E_ACCESS, "usage16");
    REFUSE(x.level[0].row_stride = 60, N2T_E_STRIDE, "row60");
    REFUSE(x.level[0].row_stride = 32, N2T_E_STRIDE, "row32");
    REFUSE(x.level[0].offset = 4, N2T_E_STRIDE, "offset4");
    REFUSE(x.level[0].layer_stride = 64, N2T_E_STRIDE, "layer on nonarray");
    REFUSE(x.swizzle_valid = 1; x.swizzle_word = 0x6f8, N2T_E_INVALID, "swizzle6/7");
    REFUSE(x.swizzle_valid = 1; x.swizzle_word = 0x1000, N2T_E_INVALID, "swizzle bit12");
    REFUSE(x.width = 20, N2T_E_DIMENSIONS, "width mismatch level");
    /* span beyond backing length */
    {
        struct n2t_texture_layout x = l;
        uint8_t scratch[64];
        CHECK(n2t_layout_pack_sampled(&x, BACKING, 1023, DESC, PAYLOAD,
                                      scratch, sizeof(scratch), &p) == N2T_E_SPAN, "span");
    }
    /* mip overlap */
    {
        struct n2t_texture_layout x;
        base_layout(&x, N2T_KIND_2D, 70, 16, 16, 1, 2, 1, 1, N2T_ACCESS_SAMPLE);
        level(&x, 0, 0, 64, 0, 0); level(&x, 1, 512, 64, 0, 0);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_E_SPAN, "mip overlap");
    }
    /* descriptor/payload overlap and capacity */
    {
        struct n2t_texture_layout x = l;
        uint8_t scratch[64];
        CHECK(n2t_layout_pack_sampled(&x, BACKING, 0x1000, DESC, DESC,
                                      scratch, sizeof(scratch), &p) == N2T_E_OUTPUT, "output overlap");
        CHECK(n2t_layout_pack_sampled(&x, BACKING, 0x1000, DESC, PAYLOAD,
                                      scratch, 31, &p) == N2T_E_CAPACITY, "capacity");
    }
    /* 3D layers > 1, MS with levels > 1, cube faces not exactly 6, and
     * cube exact square geometry. */
    {
        struct n2t_texture_layout x;
        base_layout(&x, N2T_KIND_3D, 70, 16, 16, 4, 1, 2, 1, N2T_ACCESS_SAMPLE);
        level(&x, 0, 0, 64, 1024, 0);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_E_LAYERS, "3d layers");
        base_layout(&x, N2T_KIND_2D_MS, 70, 16, 16, 1, 2, 1, 4, N2T_ACCESS_SAMPLE);
        level(&x, 0, 0, 64, 1024, 0);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_E_LEVELS, "ms levels");
        base_layout(&x, N2T_KIND_CUBE, 70, 8, 8, 1, 1, 5, 1, N2T_ACCESS_SAMPLE);
        level(&x, 0, 0, 64, 0, 512);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_E_LAYERS, "cube faces");
        base_layout(&x, N2T_KIND_CUBE, 70, 8, 8, 1, 1, 12, 1, N2T_ACCESS_SAMPLE);
        level(&x, 0, 0, 64, 0, 1024);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_E_LAYERS, "ordinary cube 12 faces");
        base_layout(&x, N2T_KIND_CUBE, 70, 8, 4, 1, 1, 6, 1, N2T_ACCESS_SAMPLE);
        level(&x, 0, 0, 64, 0, 1024);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_E_DIMENSIONS, "cube nonsquare");
    }
    /* Zero backing span and 2^48 address are refusals for the public packer. */
    {
        struct n2t_texture_layout x = l;
        uint8_t scratch[64];
        CHECK(n2t_layout_pack_sampled(&x, BACKING, 0, DESC, PAYLOAD,
                                      scratch, sizeof(scratch), &p) == N2T_E_ADDRESS, "zero backing span");
        CHECK(n2t_layout_pack_sampled(&x, 1ull << 48, 0x1000, DESC, PAYLOAD,
                                      scratch, sizeof(scratch), &p) == N2T_E_ADDRESS, "backing at 2^48");
    }
    /* A valid all-zero swizzle (RRRR) is selectable only with the explicit
     * validity bit; bit12 outside the 12-bit field is rejected. */
    {
        struct n2t_texture_layout x = l;
        x.swizzle_valid = 1; x.swizzle_word = 0x000;
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_OK, "RRRR swizzle");
        CHECK((p.descriptor_words[2] & 0xfffu) == 0u, "RRRR descriptor");
    }
    /* first_layer is physical metadata; addresses stay view-relative. */
    {
        struct n2t_texture_layout x;
        base_layout(&x, N2T_KIND_2D_ARRAY, 70, 8, 8, 1, 1, 6, 1, N2T_ACCESS_SAMPLE);
        x.array_length = 18; x.first_layer = 12;
        level(&x, 0, 0, 64, 0, 512);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_OK, "first_layer view");
        CHECK(get64(payload + 8) == BACKING, "first_layer not added");
        CHECK(get64(payload + 32 + 8) == BACKING + 512, "layer stride");
        /* A 2D single-layer view of a 2D-array physical texture. */
        base_layout(&x, N2T_KIND_2D, 70, 8, 8, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
        x.array_length = 8; x.first_layer = 3;
        level(&x, 0, 0, 64, 0, 0);
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_OK, "2d slice view");
        CHECK(get64(payload + 8) == BACKING, "2d slice view-relative");
        x.array_length = 0;
        CHECK(pack(&x, &p, payload, sizeof(payload)) == N2T_E_VIEW, "2d slice needs array");
    }
    /* Public wrappers reject NULL before dereferencing it. */
    {
        uint8_t d[32], pl[32], sb[32], sr_before[sizeof(struct n2t_subresource)];
        struct n2t_subresource sr;
        memset(d, 0x5a, sizeof(d)); memset(pl, 0x5a, sizeof(pl));
        memset(sb, 0x5a, sizeof(sb)); memset(&sr, 0x5a, sizeof(sr));
        memcpy(sr_before, &sr, sizeof(sr));
        CHECK(n2t_layout_pack_sampled(NULL, BACKING, 0x1000, DESC, PAYLOAD,
                                      payload, sizeof(payload), &p) == N2T_E_INVALID, "null pack");
        CHECK(!memcmp(d, sb, sizeof(d)), "null pack output helper untouched");
        CHECK(n2t_texture_pack(NULL, BACKING, 0x1000, DESC, PAYLOAD, d, NULL, 0) == N2T_E_INVALID, "null texture pack");
        CHECK(!memcmp(d, sb, sizeof(d)), "null texture pack output unchanged");
        CHECK(n2t_storage_pack(NULL, BACKING, 0x1000, DESC, PAYLOAD, d, NULL, 0) == N2T_E_INVALID, "null storage pack");
        CHECK(!memcmp(d, sb, sizeof(d)), "null storage pack output unchanged");
        CHECK(n2t_plane_pack(NULL, BACKING, 0, 0, pl, NULL, 0) == N2T_E_INVALID, "null plane pack");
        CHECK(!memcmp(pl, sb, sizeof(pl)), "null plane pack output unchanged");
        CHECK(n2t_layout_subresource(NULL, BACKING, 0, 0, &sr) == N2T_E_INVALID, "null subresource");
        CHECK(!memcmp(&sr, sr_before, sizeof(sr)), "null subresource output unchanged");
    }
}

static void test_sampler(void)
{
    struct n2t_sampler_state s;
    uint8_t out[32], before[32];
    uint32_t want[8];
    memset(&s, 0, sizeof(s));
    s.version = N2T_TEXTURE_RESOURCES_VERSION;
    s.min_filter = N2T_FILTER_NEAREST; s.mag_filter = N2T_FILTER_NEAREST;
    s.mip_filter = N2T_MIP_NEAREST;
    s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_EDGE;
    s.normalized = 1; s.max_anisotropy = 1;
    s.clamp_integer_array = 1; s.seamless_cube = 1;
    s.lod_min = 0; s.lod_max = 0;
    pan_cast_and_pack(want, SAMPLER, cfg) {
        cfg.minify_nearest = true; cfg.magnify_nearest = true;
        cfg.mipmap_mode = MALI_MIPMAP_MODE_NEAREST;
        cfg.wrap_mode_s = MALI_WRAP_MODE_CLAMP_TO_EDGE;
        cfg.wrap_mode_t = MALI_WRAP_MODE_CLAMP_TO_EDGE;
        cfg.wrap_mode_r = MALI_WRAP_MODE_CLAMP_TO_EDGE;
        cfg.normalized_coordinates = true;
        cfg.seamless_cube_map = true;
        cfg.clamp_integer_array_indices = true;
        cfg.maximum_anisotropy = 1;
    }
    CHECK(n2t_sampler_pack(&s, out) == N2T_OK, "sampler baseline");
    CHECK(!memcmp(out, want, 32), "sampler baseline mesa");
    /* Full state: linear trilinear mirror/repeat, aniso, LOD, white border. */
    s.min_filter = s.mag_filter = N2T_FILTER_LINEAR;
    s.mip_filter = N2T_MIP_LINEAR;
    s.address_s = N2T_ADDRESS_REPEAT; s.address_t = N2T_ADDRESS_MIRROR_REPEAT; s.address_r = N2T_ADDRESS_MIRROR_CLAMP_EDGE;
    s.max_anisotropy = 4; s.lod_algorithm = N2T_LOD_ANISOTROPIC;
    s.lod_min = 0.5f; s.lod_max = 3.25f; s.lod_bias = -0.5f;
    s.border_color_kind = N2T_BORDER_OPAQUE_WHITE;
    memset(want, 0, sizeof(want));
    pan_cast_and_pack(want, SAMPLER, cfg) {
        cfg.minify_nearest = false; cfg.magnify_nearest = false;
        cfg.mipmap_mode = MALI_MIPMAP_MODE_TRILINEAR;
        cfg.wrap_mode_s = MALI_WRAP_MODE_REPEAT;
        cfg.wrap_mode_t = MALI_WRAP_MODE_MIRRORED_REPEAT;
        cfg.wrap_mode_r = MALI_WRAP_MODE_MIRRORED_CLAMP_TO_EDGE;
        cfg.normalized_coordinates = true;
        cfg.seamless_cube_map = true;
        cfg.clamp_integer_array_indices = true;
        cfg.maximum_anisotropy = 4;
        cfg.lod_algorithm = MALI_LOD_ALGORITHM_ANISOTROPIC;
        cfg.minimum_lod = 0.5f; cfg.maximum_lod = 3.25f; cfg.lod_bias = -0.5f;
        cfg.border_color_r = 0x3f800000u; cfg.border_color_g = 0x3f800000u;
        cfg.border_color_b = 0x3f800000u; cfg.border_color_a = 0x3f800000u;
    }
    CHECK(n2t_sampler_pack(&s, out) == N2T_OK, "sampler full");
    CHECK(!memcmp(out, want, 32), "sampler full mesa");
    /* Compare is per use; disabled compare never leaks a Metal function. */
    s.compare_enabled = 0; s.compare_function = N2T_COMPARE_LESS;
    CHECK(n2t_sampler_pack(&s, out) == N2T_OK, "compare disabled");
    CHECK((((uint32_t)out[4] | ((uint32_t)out[5] << 8) | ((uint32_t)out[6] << 16) | ((uint32_t)out[7] << 24)) >> 13 & 7u) == 0, "compare disabled bits");
    s.compare_enabled = 1;
    CHECK(n2t_sampler_pack(&s, out) == N2T_OK, "compare enabled");
    CHECK((((uint32_t)out[4] | ((uint32_t)out[5] << 8) | ((uint32_t)out[6] << 16) | ((uint32_t)out[7] << 24)) >> 13 & 7u) == 4, "compare flip");
    /* Compare is only valid with a depth view. */
    struct n2t_format_info rgba, r8, u8, depth;
    CHECK(n2t_format_query(70, &rgba) == N2T_OK && n2t_format_query(10, &r8) == N2T_OK && n2t_format_query(73, &u8) == N2T_OK, "format query");
    CHECK(n2t_format_query(252, &depth) == N2T_OK, "depth format");
    CHECK(n2t_sampler_pack_for_format(&s, &rgba, out) == N2T_E_UNSUPPORTED, "compare on color");
    CHECK(n2t_sampler_pack_for_format(&s, &depth, out) == N2T_OK, "compare on depth");
    s.compare_enabled = 0;
    /* ClampToZero alpha/no-alpha needs a format. */
    s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_ZERO;
    CHECK(n2t_sampler_pack(&s, out) == N2T_E_UNSUPPORTED, "clamp zero no format");
    CHECK(n2t_sampler_pack_for_format(&s, &rgba, out) == N2T_OK, "clamp zero alpha");
    CHECK(out[28] == 0 && out[29] == 0 && out[30] == 0 && out[31] == 0, "clamp zero alpha border");
    CHECK(n2t_sampler_pack_for_format(&s, &r8, out) == N2T_OK, "clamp zero no alpha");
    CHECK(0x3f800000u == ((uint32_t)out[28] | ((uint32_t)out[29] << 8) | ((uint32_t)out[30] << 16) | ((uint32_t)out[31] << 24)), "clamp zero no alpha border");
    /* Custom float border and presets for normalized float views. */
    s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_BORDER;
    s.border_color_kind = N2T_BORDER_CUSTOM_FLOAT;
    s.border[0] = 0.25f; s.border[1] = 0.5f; s.border[2] = 0.75f; s.border[3] = 1.0f;
    CHECK(n2t_sampler_pack_for_format(&s, &rgba, out) == N2T_OK, "custom float border");
    CHECK(out[16] == 0x00 && out[17] == 0x00 && out[18] == 0x80 && out[19] == 0x3e, "custom border R");
    s.border_color_kind = N2T_BORDER_OPAQUE_BLACK;
    CHECK(n2t_sampler_pack_for_format(&s, &rgba, out) == N2T_OK, "opaque black border");
    /* Integer border requires exact raw words. */
    s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_BORDER;
    s.border_color_kind = N2T_BORDER_OPAQUE_WHITE;
    CHECK(n2t_sampler_pack_for_format(&s, &u8, out) == N2T_E_UNSUPPORTED, "integer preset border");
    /* Integer filtering must be nearest; do not silently filter. */
    s.border_color_kind = 0;
    s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_EDGE;
    s.min_filter = s.mag_filter = N2T_FILTER_LINEAR;
    CHECK(n2t_sampler_pack_for_format(&s, &u8, out) == N2T_E_UNSUPPORTED, "integer linear");
    s.min_filter = s.mag_filter = N2T_FILTER_NEAREST;
    s.mip_filter = N2T_MIP_NEAREST;
    s.max_anisotropy = 1;
    s.lod_algorithm = N2T_LOD_ISOTROPIC;
    s.border_color_kind = N2T_BORDER_CUSTOM_RAW;
    s.border_raw[0] = 0x11223344; s.border_raw[1] = 0x55667788;
    s.border_raw[2] = 0x99aabbcc; s.border_raw[3] = 0xddeeff00;
    CHECK(n2t_sampler_pack_for_format(&s, &u8, out) == N2T_OK, "integer raw border");
    CHECK(out[16] == 0x44 && out[20] == 0x88 && out[24] == 0xcc && out[28] == 0x00, "integer raw words");
    /* Refusals: reduction, invalid filters, LOD order, aniso. */
    memset(before, 0xa5, sizeof(before)); memcpy(out, before, sizeof(out));
    struct n2t_sampler_state bad = s;
    bad.border_color_kind = 0;
    bad.address_s = N2T_ADDRESS_CLAMP_EDGE;
    bad.reduction = N2T_REDUCTION_MIN;
    CHECK(n2t_sampler_pack(&bad, out) == N2T_E_UNSUPPORTED && !memcmp(out, before, 32), "reduction refusal");
    bad = s; bad.reduction = 0; bad.min_filter = 2;
    CHECK(n2t_sampler_pack(&bad, out) == N2T_E_SAMPLER_FIELD, "filter refusal");
    bad = s; bad.min_filter = 0; bad.lod_min = 2; bad.lod_max = 1;
    CHECK(n2t_sampler_pack(&bad, out) == N2T_E_SAMPLER_COMBINATION, "lod order refusal");
    bad = s; bad.max_anisotropy = 17;
    CHECK(n2t_sampler_pack(&bad, out) == N2T_E_SAMPLER_FIELD, "aniso refusal");
}

static void test_sampler_layout(void)
{
    struct n2t_sampler_state s;
    struct n2t_texture_layout l;
    memset(&s, 0, sizeof(s)); s.version = N2T_TEXTURE_RESOURCES_VERSION;
    s.min_filter = s.mag_filter = N2T_FILTER_LINEAR; s.mip_filter = N2T_MIP_LINEAR;
    s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_EDGE;
    s.normalized = 1; s.max_anisotropy = 1; s.clamp_integer_array = 1; s.seamless_cube = 1;
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 2, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0); level(&l, 1, 1024, 64, 0, 0);
    CHECK(n2t_sampler_layout_check(&s, &l) == N2T_OK, "sampler/layout");
    s.normalized = 0;
    CHECK(n2t_sampler_layout_check(&s, &l) == N2T_E_UNSUPPORTED, "unnormalized mips");
    base_layout(&l, N2T_KIND_2D, 70, 16, 16, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(n2t_sampler_layout_check(&s, &l) == N2T_OK, "unnormalized 2d");
    s.address_s = N2T_ADDRESS_REPEAT;
    CHECK(n2t_sampler_layout_check(&s, &l) == N2T_E_UNSUPPORTED, "unnormalized repeat");
}

static void test_compute_wrappers(void)
{
    struct n2t_texture_layout l;
    struct n2t_packed_texture p;
    uint8_t desc[32], plane[32], entry[16], payload[64];
    char why[64];
    base_layout(&l, N2T_KIND_2D, 70, 16, 4, 1, 1, 1, 1, N2T_ACCESS_SAMPLE);
    level(&l, 0, 0, 64, 0, 0);
    CHECK(n2t_texture_validate(&l, 0x1000, N2T_ACCESS_SAMPLE) == N2T_OK, "wrapper validate");
    CHECK(n2t_texture_validate(&l, 0x1000, N2T_ACCESS_STORAGE_WRITE) == N2T_E_ACCESS, "wrapper access");
    CHECK(n2t_texture_pack(&l, BACKING, 0x1000, DESC, PAYLOAD, desc, why, sizeof(why)) == N2T_OK, "wrapper pack");
    CHECK(n2t_plane_pack(&l, BACKING, 0, 0, plane, why, sizeof(why)) == N2T_OK, "wrapper plane");
    CHECK(n2t_storage_pack(&l, BACKING, 0x1000, DESC, PAYLOAD, desc, why, sizeof(why)) == N2T_E_ACCESS, "wrapper storage access");
    l.access = N2T_ACCESS_STORAGE_WRITE; l.format = 73;
    CHECK(n2t_storage_pack(&l, BACKING, 0x1000, DESC, PAYLOAD, desc, why, sizeof(why)) == N2T_OK, "wrapper storage");
    l.format = 70; l.access = N2T_ACCESS_SAMPLE;
    CHECK(n2t_resource_entry(BACKING, 64, entry) == N2T_OK, "resource entry");
    CHECK(get64(entry) == (BACKING | (1ull << 56)) && get64(entry + 8) == 64, "resource entry words");
    why[0] = 'x'; why[1] = '\0';
    l.level[0].row_stride = 4;
    CHECK(n2t_texture_pack(&l, BACKING, 0x1000, DESC, PAYLOAD, desc, why, sizeof(why)) == N2T_E_STRIDE, "wrapper refusal");
    CHECK(why[0] && strcmp(why, "x"), "wrapper reason");
    (void)p; (void)payload;
}

int main(void)
{
    test_qualified_golden();
    test_2d_mips();
    test_arrays_and_cubes();
    test_3d_ms();
    test_1d_and_views();
    test_bounds();
    test_transport_mips_layers();
    test_compressed();
    test_coverage_matrix();
    test_formats();
    test_view_format();
    test_storage();
    test_refusals();
    test_sampler();
    test_sampler_layout();
    test_compute_wrappers();
    printf("%s: %u checks, %u failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures != 0;
}
