/* Resource packing and admission reused from private Tegu7f6f7ca9.
 * v9 compatibility is checked against the Mesa descriptor oracle. No CSF. */
#define GD_ENTRIES 32u
static void gd_resource(uint8_t *p, uint64_t address, uint64_t bytes)
{
    gd_put64(p, (address & ((1ull << 56) - 1)) | (1ull << 56)); gd_put64(p + 8, bytes);
}
static void gd_buffer(uint8_t *p, uint64_t address, uint32_t bytes)
{
    memset(p, 0, 32); gd_put32(p, 9u | (1u << 4)); gd_put32(p + 4, bytes); gd_put64(p + 8, address);
}
/* Linear 2D texture and its plane, as fixed_ui_texture_resources.c.  Mali
 * format words (Mesa MALI_PACK_FMT): BGRA8 = RGBA8_UNORM/BGRA order,
 * R8 = R8_UNORM/R001 (RGB1). */
/* Metal format -> Mali format word (Mesa MALI_PACK_FMT), texel size, plane
 * clump format and descriptor swizzle.  A8 samples R8 with (0,0,0,R). */
static uint32_t gd_format(uint32_t metal)
{
    switch (metal) {
    case 1: case 10: return (163u << 12) | 16u;   /* R8_UNORM, R001 */
    case 30: return (171u << 12) | 16u;           /* RG8_UNORM, RG01 */
    case 43: return (65u << 12) | 4u;             /* RGB5_A1_UNORM, BGRA (as n2t) */
    case 115: return 223u << 12;                  /* RGBA16F, RGBA */
    case 70: return 187u << 12;                   /* RGBA8_UNORM, RGBA */
    default: return (187u << 12) | 4u;            /* BGRA8 */
    }
}
static uint32_t gd_bytes_per_texel(uint32_t metal)
{
    return metal == 1 || metal == 10 ? 1u : metal == 30 || metal == 43 ? 2u : metal == 115 ? 8u : 4u;
}
static uint32_t gd_clump(uint32_t metal)
{
    uint32_t bytes = gd_bytes_per_texel(metal);
    return bytes == 1 ? 0u : bytes == 2 ? 1u : bytes == 8 ? 3u : 2u; /* RAW8/16/64/32 */
}
static uint32_t gd_swizzle(uint32_t metal) { return metal == 1 ? 0x124u : 0x688u; }
/* Metal ASTC 2D formats: 186-200 sRGB, 204-218 LDR (block widths/heights). */
static int gd_astc(uint32_t metal, uint32_t *bw, uint32_t *bh)
{
    static const uint8_t dims[15][2] = {{4,4},{5,4},{5,5},{6,5},{6,6},{0,0},{8,5},{8,6},{8,8},
        {10,5},{10,6},{10,8},{10,10},{12,10},{12,12}};
    uint32_t i = metal >= 186 && metal <= 200 ? metal - 186 :
        metal >= 204 && metal <= 218 ? metal - 204 :
        metal >= 222 && metal <= 236 ? metal - 222 : 99;
    if (i >= 15 || !dims[i][0]) return 0;
    *bw = dims[i][0]; *bh = dims[i][1];
    return 1;
}
/* Mali v10 ASTC 2D Dimension: 4->0, 5->1, 6->2, 8->4, 10->6, 12->7. */
static uint32_t gd_astc_dim(uint32_t d) { return d == 4 ? 0 : d == 5 ? 1 : d == 6 ? 2 : d == 8 ? 4 : d == 10 ? 6 : 7; }
static void gd_texture(uint8_t *p, uint8_t *plane, uint64_t plane_va, uint64_t texels,
    uint32_t width, uint32_t height, uint32_t row_bytes, uint32_t metal_format)
{
    uint32_t bw, bh;
    if (gd_astc(metal_format, &bw, &bh)) {
        /* Valhall: the texture format is the decode interchange format (RGBA8
         * sRGB, else RGBA16F); the ASTC 2D plane carries the block size. */
        int srgb = metal_format <= 200;
        int hdr = metal_format >= 222 && metal_format <= 236;
        uint32_t fmt = srgb ? (187u << 12) | (1u << 20) : 223u << 12;
        memset(p, 0, 32);
        gd_put32(p, 2u | (2u << 4) | (fmt << 10));
        gd_put32(p + 4, (width - 1u) | ((height - 1u) << 16)); gd_put32(p + 8, 0x688u); gd_put64(p + 16, plane_va);
        memset(plane, 0, 32);
        uint32_t rows = (height + bh - 1u) / bh;
        gd_put32(plane, 10u | (4u << 4) | (2u << 8) | ((srgb ? 0u : 1u) << 25) |
            (hdr ? (1u << 24) : 0u) | (gd_astc_dim(bw) << 26) | (gd_astc_dim(bh) << 29));
        gd_put32(plane + 4, row_bytes * rows);
        gd_put64(plane + 8, texels); gd_put32(plane + 16, row_bytes); gd_put32(plane + 28, (width - 1u) | ((height - 1u) << 16));
        return;
    }
    memset(p, 0, 32);
    gd_put32(p, 2u | (2u << 4) | (gd_format(metal_format) << 10));
    gd_put32(p + 4, (width - 1u) | ((height - 1u) << 16)); gd_put32(p + 8, gd_swizzle(metal_format)); gd_put64(p + 16, plane_va);
    memset(plane, 0, 32);
    /* Generic plane: clump format is the texel size (RAW8 0, RAW32 2). */
    uint32_t clump = gd_clump(metal_format);
    gd_put32(plane, 10u | (1u << 4) | (2u << 8) | (clump << 24)); gd_put32(plane + 4, row_bytes * height);
    gd_put64(plane + 8, texels); gd_put32(plane + 16, row_bytes); gd_put32(plane + 28, (width - 1u) | ((height - 1u) << 16));
}
static int gd_sampler(uint8_t *p, uint32_t word)
{
    uint32_t wrap[3];
    if (word & ~0xffffu) return -1;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t w = (word >> (4 + 4 * i)) & 15u;
        if (w && w != 8 && w != 9 && w != 11 && w != 12 && w != 13) return -1;
        wrap[i] = w ? w : 9u;
    }
    memset(p, 0, 32);
    /* Type, wraps R/T/S, round-to-even, seamless, normalized, clamp array
     * index; nearest unless the word asks for linear. */
    gd_put32(p, 1u | (wrap[2] << 8) | (wrap[1] << 12) | (wrap[0] << 16) | (1u << 21) | (1u << 23) |
        (1u << 25) | (1u << 26) | ((word & 1u) ? 0 : 1u << 27) | ((word & 2u) ? 0 : 1u << 28));
    return 0;
}
static uint64_t gd_texture_expected_bytes(const struct tegu_generic_draw_binding *b)
{
    uint32_t bw, bh;
    if (gd_astc(b->format, &bw, &bh)) return (uint64_t)b->row_bytes * ((b->height + bh - 1u) / bh);
    return (uint64_t)b->row_bytes * b->height;
}
static int gd_request_ok(const struct tegu_generic_draw_shader *sh, const struct tegu_generic_draw_request *q, const struct n2r_texture_resources *tr)
{
    if (!sh->vertex_code || !sh->fragment_code || !sh->vertex_bytes || !sh->fragment_bytes ||
        sh->position_offset >= sh->secondary_offset || sh->secondary_offset >= sh->vertex_bytes ||
        !sh->varying_bytes || sh->varying_bytes > 64 || !sh->vertex_registers || sh->vertex_registers > 64 ||
        !sh->secondary_registers || sh->secondary_registers > 64 ||
        !sh->fragment_registers || sh->fragment_registers > 64 ||
        /* A stage that reads no push constant uses fewer than 32 FAU words. */
        sh->vertex_fau_words > 32 + TEGU_GENERIC_DRAW_PUSH_BYTES / 4 ||
        sh->fragment_fau_words > 32 + TEGU_GENERIC_DRAW_PUSH_BYTES / 4 ||
        /* No attributes: the vertex shader pulls its data by vertex_id. */
        sh->attribute_count > TEGU_GENERIC_DRAW_MAX_ATTRIBUTES ||
        (sh->flags & ~TEGU_GENERIC_SHADER_TILE_FETCH)) N2R_FAIL(0);
    if (!n2r_bpp(q->target_format) || q->width < 1 || q->height < 1 || q->width > 4096 || q->height > 4096 || (q->row_bytes & 63) ||
        q->row_bytes < q->width * n2r_bpp(q->target_format) || q->row_bytes > 65536 || q->load_action > 2 ||
        q->primitive > TEGU_GENERIC_DRAW_TRIANGLE_STRIP ||
        q->vertex_count < (q->primitive == TEGU_GENERIC_DRAW_LINES ? 2u : 3u) || q->vertex_count > TEGU_GENERIC_DRAW_MAX_VERTICES ||
        (q->primitive == TEGU_GENERIC_DRAW_TRIANGLES && q->vertex_count % 3) ||
        (q->primitive == TEGU_GENERIC_DRAW_LINES && q->vertex_count % 2) ||
        (!sh->attribute_count && (q->vertex.token || q->vertex.bytes)) ||
        (q->index_size != 0 && q->index_size != 2 && q->index_size != 4) ||
        (q->index_size && (sh->attribute_count || !q->index.token || (q->index.offset % q->index_size) ||
            q->index.bytes < (uint64_t)q->vertex_count * q->index_size || q->index.bytes > 0xffffffffu)) ||
        (!q->index_size && (q->index.token || q->index.bytes)) ||
        (16u + sh->varying_bytes) * q->vertex_count > TEGU_GENERIC_DRAW_GEOMETRY_BUDGET ||
        !q->scissor[2] || !q->scissor[3] || q->scissor[0] >= q->width || q->scissor[1] >= q->height ||
        q->scissor[2] > q->width - q->scissor[0] || q->scissor[3] > q->height - q->scissor[1] ||
        q->binding_count > TEGU_GENERIC_DRAW_MAX_BINDINGS ||
        q->vertex_push_bytes > TEGU_GENERIC_DRAW_PUSH_BYTES || q->fragment_push_bytes > TEGU_GENERIC_DRAW_PUSH_BYTES ||
        q->constant_bytes > TEGU_GENERIC_DRAW_CONSTANT_BYTES) N2R_FAIL(0);
    unsigned tile_bindings = 0;
    for (uint32_t i = 0; i < q->binding_count; i++) {
        const struct tegu_generic_draw_binding *b = &q->bindings[i];
        if (b->binding >= GD_ENTRIES) N2R_FAIL(0);
        for (uint32_t j = 0; j < i; j++) if (q->bindings[j].binding == b->binding) N2R_FAIL(0);
        switch (b->kind) {
        case TEGU_GENERIC_DRAW_BUFFER:
            if (!b->memory.token || !b->memory.bytes || b->memory.bytes > UINT32_MAX) N2R_FAIL(0);
            break;
        case TEGU_GENERIC_DRAW_CONSTANT:
            if (!q->constant_bytes) N2R_FAIL(0);
            break;
        case TEGU_GENERIC_DRAW_COMBINED: case TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE: case TEGU_GENERIC_DRAW_TEXTURE: {
            uint32_t bw, bh;
            const struct n2r_texture_binding *tb = tr ? n2r_texture_lookup(tr, b->binding) : 0;
            if (tb) {
                /* Authoritative v2 sidecar: n2t validates the allocation,
                 * view format, shape, strides and every span against the SAME
                 * retained memory.bytes.  Do not let the legacy whitelist or
                 * gd_format fallback reject or remap it here. */
                if (!b->memory.token || !b->memory.bytes) N2R_FAIL(0);
                break;
            }
            if (b->format >= 222 && b->format <= 236) N2R_FAIL(0);
            if (gd_astc(b->format, &bw, &bh)) {
                /* Rows of 16-byte ASTC blocks. */
                uint64_t need = (uint64_t)b->row_bytes * ((b->height + bh - 1u) / bh);
                if (!b->memory.token || !b->width || !b->height || b->width > 4096 || b->height > 4096 ||
                    (b->row_bytes & 63) || b->row_bytes < (b->width + bw - 1u) / bw * 16u || (b->memory.offset & 63) ||
                    b->memory.bytes != need) N2R_FAIL(0);
                break;
            }
            uint64_t need = (uint64_t)b->row_bytes * b->height;
            if (!b->memory.token || !b->width || !b->height || b->width > 4096 || b->height > 4096 ||
                (b->format && b->format != 80 && b->format != 70 && b->format != 10 && b->format != 1 &&
                 b->format != 30 && b->format != 43 && b->format != 115) ||
                (b->row_bytes & 63) || b->row_bytes < b->width * gd_bytes_per_texel(b->format) || (b->memory.offset & 63) ||
                b->memory.bytes != need) N2R_FAIL(0);
            break;
        }
        case TEGU_GENERIC_DRAW_TILE_ATTACHMENT:
            if (!(sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH) || b->memory.token || b->memory.offset || b->memory.bytes ||
                b->width || b->height || b->row_bytes || b->sampler || b->format || b->reserved) N2R_FAIL(0);
            tile_bindings++;
            break;
        case TEGU_GENERIC_DRAW_ATTACHMENT:
            if (sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH) N2R_FAIL(0);
            break;
        case TEGU_GENERIC_DRAW_SAMPLER:
        case TEGU_GENERIC_DRAW_NEUTRAL_IMAGE: case TEGU_GENERIC_DRAW_NEUTRAL_BUFFER:
            break;
        default:
            N2R_FAIL(0);
        }
    }
    if (!!tile_bindings != !!(sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH)) N2R_FAIL(0);
    return 1;
}

