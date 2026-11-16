/* Two-plane YCbCr IOSurface texels as Metal reads them (CPU decode for the
 * frontend's sampled-only shadows of pixel formats 500-545).
 *
 * Pure, dependency-free C (no libc headers, no libm; __builtin_memcpy). Arithmetic follows the measured Metal
 * model documented by the pinned metal2vulkan translator
 * (crates/metal2vulkan/src/bind/ycbcr_convert.rs module docs; Apple M4 EVs
 * XRR2-ycc1/ycc2, TXN2-ycr1/ycr3/ycq1/ycp1/ycv1):
 *   chroma 4:2:0  9/3/3/1 of the four nearest pairs, C = (S + 8) >> 4
 *   chroma 4:2:2  3/1 across, C = (S + 2) >> 2;   4:4:4 its own pair
 *   converted     u = 10 - b, Cb' = (Cb << u) - 512, Cr' = (Cr << u) - 512,
 *                 L = 9576 ((Y << u) - 64) (video) or
 *                 256 floor((32768 Y + floor(m/2)) / m), m = 2^b - 1 (full),
 *                 v = clamp((L + kb Cb' + kr Cr' + 2048) >> 12, 0, 2048) / 2048
 *   sRGB twins    q = clamp((T + 512) >> 10, 0, 8192) through the sRGB knots
 *                 (tegu_srgb_knots_8192, generated), / 8192
 *   codes         red Cr, green Y, blue Cb, alpha 1 (RGBA8 / RGB10A2 texels)
 * Converted values are exact in half floats; sRGB steps above 1/4 round to
 * half with ties away from zero (Metal's own half read of them). */
#ifndef TEGU_YCBCR_DECODE_H
#define TEGU_YCBCR_DECODE_H
#include <stdint.h>
#include <stddef.h>

enum {TEGU_YCBCR_SUB_420, TEGU_YCBCR_SUB_422, TEGU_YCBCR_SUB_444};
enum {TEGU_YCBCR_OUT_CONVERTED, TEGU_YCBCR_OUT_SRGB, TEGU_YCBCR_OUT_CODES};
enum {TEGU_YCBCR_BT601, TEGU_YCBCR_BT709, TEGU_YCBCR_BT2020};

struct tegu_ycbcr_layout {
    unsigned bits, packed, full_range, sub, output, matrix;
};

/* Metal pixel format -> layout facts of the format (range/matrix come from the
 * surface). Returns 0 for a format that is not a two-plane YCbCr format this
 * decoder implements. */
static inline int tegu_ycbcr_format(uint32_t format, struct tegu_ycbcr_layout *l)
{
    static const struct {uint16_t format; uint8_t bits, packed, sub, output;} table[] = {
        {500, 8, 0, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_CONVERTED},
        {502, 8, 0, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_CONVERTED},
        {503, 8, 0, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_CONVERTED},
        {505, 10, 0, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_CONVERTED},
        {506, 10, 0, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_CONVERTED},
        {507, 10, 0, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_CONVERTED},
        {508, 10, 1, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_CONVERTED},
        {509, 10, 1, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_CONVERTED},
        {510, 10, 1, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_CONVERTED},
        {520, 8, 0, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_SRGB},
        {522, 8, 0, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_SRGB},
        {523, 8, 0, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_SRGB},
        {525, 10, 0, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_SRGB},
        {526, 10, 0, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_SRGB},
        {527, 10, 0, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_SRGB},
        {528, 10, 1, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_SRGB},
        {529, 10, 1, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_SRGB},
        {530, 10, 1, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_SRGB},
        {540, 8, 0, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_CODES},
        {541, 8, 0, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_CODES},
        {542, 8, 0, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_CODES},
        {543, 10, 0, TEGU_YCBCR_SUB_420, TEGU_YCBCR_OUT_CODES},
        {544, 10, 0, TEGU_YCBCR_SUB_422, TEGU_YCBCR_OUT_CODES},
        {545, 10, 0, TEGU_YCBCR_SUB_444, TEGU_YCBCR_OUT_CODES},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (table[i].format == format) {
            l->bits = table[i].bits; l->packed = table[i].packed; l->sub = table[i].sub;
            l->output = table[i].output; l->full_range = 0; l->matrix = TEGU_YCBCR_BT601;
            return 1;
        }
    return 0;
}

/* CoreVideo surface OSType -> bits, packing, range and the chroma plane's own
 * subsampling. A format reads a surface whose chroma plane holds at least its
 * own resolution (finer chroma is read at the format's positions). */
static inline int tegu_ycbcr_surface(uint32_t fourcc, unsigned *bits, unsigned *packed,
                                     unsigned *full_range, unsigned *sub)
{
    static const struct {uint32_t fourcc; uint8_t bits, packed, full, sub;} s[] = {
        {0x34323076u, 8, 0, 0, 0}, {0x34323066u, 8, 0, 1, 0},   /* 420v 420f */
        {0x34323276u, 8, 0, 0, 1}, {0x34323266u, 8, 0, 1, 1},   /* 422v 422f */
        {0x34343476u, 8, 0, 0, 2}, {0x34343466u, 8, 0, 1, 2},   /* 444v 444f */
        {0x78343230u, 10, 0, 0, 0}, {0x78663230u, 10, 0, 1, 0}, /* x420 xf20 */
        {0x78343232u, 10, 0, 0, 1}, {0x78663232u, 10, 0, 1, 1}, /* x422 xf22 */
        {0x78343434u, 10, 0, 0, 2}, {0x78663434u, 10, 0, 1, 2}, /* x444 xf44 */
        {0x70343230u, 10, 1, 0, 0}, {0x70663230u, 10, 1, 1, 0}, /* p420 pf20 */
        {0x70343232u, 10, 1, 0, 1}, {0x70663232u, 10, 1, 1, 1}, /* p422 pf22 */
        {0x70343434u, 10, 1, 0, 2}, {0x70663434u, 10, 1, 1, 2}, /* p444 pf44 */
    };
    for (size_t i = 0; i < sizeof(s) / sizeof(s[0]); i++)
        if (s[i].fourcc == fourcc) {
            *bits = s[i].bits; *packed = s[i].packed; *full_range = s[i].full; *sub = s[i].sub;
            return 1;
        }
    return 0;
}

/* Bytes per plane element as IOSurface reports them (0 for packed rows). */
static inline unsigned tegu_ycbcr_luma_bytes(const struct tegu_ycbcr_layout *l)
{
    return l->packed ? 0u : l->bits == 8 ? 1u : 2u;
}

static inline unsigned tegu_ycbcr_code(const uint8_t *row, unsigned index, unsigned bits, unsigned packed)
{
    if (packed) {
        uint32_t w; __builtin_memcpy(&w, row + (size_t)(index / 3u) * 4u, 4);
        return (w >> (10u * (index % 3u))) & 1023u;
    }
    if (bits == 8) return row[index];
    {
        uint16_t w; __builtin_memcpy(&w, row + (size_t)index * 2u, 2);
        return (unsigned)w >> 6;
    }
}

/* value * 2^-shift as an IEEE half; value <= 2^14, normal results only
 * (the callers' nonzero values are >= 2^-13). Ties away from zero. */
static inline uint16_t tegu_ycbcr_half(uint32_t value, int shift)
{
    int msb = 31;
    uint32_t mantissa;
    if (!value) return 0;
    while (!(value >> msb)) msb--;
    if (msb <= 10) mantissa = (value << (10 - msb)) & 0x3ffu;
    else {
        uint32_t drop = (uint32_t)(msb - 10), rounded = (value + (1u << (drop - 1u))) >> drop;
        if (rounded >> 11) { rounded >>= 1; msb++; }
        mantissa = rounded & 0x3ffu;
    }
    return (uint16_t)(((uint32_t)(msb - shift + 15) << 10) | mantissa);
}

static inline void tegu_ycbcr_coefficients(unsigned full, unsigned matrix, int k[3][2])
{
    static const int table[2][3][3][2] = {
        { {{-9, 13123}, {-3218, -6686}, {16591, 9}},     /* video 601 */
          {{-1, 14743}, {-1753, -4383}, {17373, 1}},     /* video 709 */
          {{0, 13806}, {-1540, -5349}, {17614, -1}} },   /* video 2020 */
        { {{-8, 11483}, {-2816, -5850}, {14518, 8}},     /* full 601 */
          {{-1, 12900}, {-1534, -3835}, {15201, 1}},     /* full 709 */
          {{0, 12080}, {-1348, -4680}, {15412, -1}} },   /* full 2020 */
    };
    __builtin_memcpy(k, table[full ? 1 : 0][matrix < 3 ? matrix : 0], sizeof(table[0][0]));
}

/* Filtered chroma codes (Cb, Cr) of texel (x, y) from a chroma plane of
 * cw x ch pairs (2 codes each) at `base` with `row` bytes per row. */
static inline void tegu_ycbcr_chroma(const struct tegu_ycbcr_layout *l, const uint8_t *base, size_t row,
                                     unsigned cw, unsigned ch, unsigned x, unsigned y, unsigned out[2])
{
    unsigned cx[2], cy[2], wx[2], wy[2], shift;
    int halvex = l->sub != TEGU_YCBCR_SUB_444, halvey = l->sub == TEGU_YCBCR_SUB_420;
    if (halvex) {
        int near = (int)(x / 2u), far = (x & 1u) ? near + 1 : near - 1;
        if (far < 0) far = 0;
        if (far > (int)cw - 1) far = (int)cw - 1;
        if (near > (int)cw - 1) near = (int)cw - 1;
        cx[0] = (unsigned)near; cx[1] = (unsigned)far; wx[0] = 3; wx[1] = 1;
    } else { cx[0] = cx[1] = x < cw ? x : cw - 1; wx[0] = 4; wx[1] = 0; }
    if (halvey) {
        int near = (int)(y / 2u), far = (y & 1u) ? near + 1 : near - 1;
        if (far < 0) far = 0;
        if (far > (int)ch - 1) far = (int)ch - 1;
        if (near > (int)ch - 1) near = (int)ch - 1;
        cy[0] = (unsigned)near; cy[1] = (unsigned)far; wy[0] = 3; wy[1] = 1;
    } else { cy[0] = cy[1] = y < ch ? y : ch - 1; wy[0] = 4; wy[1] = 0; }
    shift = (halvex ? 2u : 2u) + (halvey ? 2u : 2u); /* weights sum to 16 */
    for (unsigned c = 0; c < 2; c++) {
        unsigned s = 0;
        for (unsigned j = 0; j < 2; j++)
            for (unsigned i = 0; i < 2; i++) {
                unsigned w = wx[i] * wy[j];
                if (w) s += w * tegu_ycbcr_code(base + (size_t)cy[j] * row, 2u * cx[i] + c, l->bits, l->packed);
            }
        out[c] = (s + (1u << (shift - 1u))) >> shift;
    }
}

/* Decode one texel row. `out` is RGBA16F halves (converted/sRGB) or 4-byte
 * texels (codes: RGBA8 for 8 bits, RGB10A2 for 10 bits). srgb_knots is the
 * 513-entry knot table on the grid of 1/8192. */
static inline void tegu_ycbcr_decode_row(const struct tegu_ycbcr_layout *l,
    const uint8_t *luma, size_t luma_row, const uint8_t *chroma, size_t chroma_row,
    unsigned width, unsigned cw, unsigned ch, unsigned y, void *out, const uint16_t *srgb_knots)
{
    int k[3][2];
    unsigned u = 10u - l->bits, m = (1u << l->bits) - 1u;
    tegu_ycbcr_coefficients(l->full_range, l->matrix, k);
    for (unsigned x = 0; x < width; x++) {
        unsigned Y = tegu_ycbcr_code(luma + (size_t)y * luma_row, x, l->bits, l->packed), C[2];
        tegu_ycbcr_chroma(l, chroma, chroma_row, cw, ch, x, y, C);
        if (l->output == TEGU_YCBCR_OUT_CODES) {
            uint32_t t;
            if (l->bits == 8) t = C[1] | (Y << 8) | (C[0] << 16) | 0xff000000u;
            else t = C[1] | (Y << 10) | (C[0] << 20) | (3u << 30);
            __builtin_memcpy((uint8_t *)out + (size_t)x * 4u, &t, 4);
            continue;
        }
        int64_t L = l->full_range ? (int64_t)256 * (int64_t)(((uint64_t)Y * 32768u + m / 2u) / m)
                                  : (int64_t)9576 * ((int64_t)(Y << u) - 64);
        int64_t cb = (int64_t)(C[0] << u) - 512, cr = (int64_t)(C[1] << u) - 512;
        uint16_t *h = (uint16_t *)out + (size_t)x * 4u;
        for (unsigned c = 0; c < 3; c++) {
            int64_t T = L + k[c][0] * cb + k[c][1] * cr;
            if (l->output == TEGU_YCBCR_OUT_CONVERTED) {
                int64_t v = (T + 2048) >> 12;
                v = v < 0 ? 0 : v > 2048 ? 2048 : v;
                h[c] = tegu_ycbcr_half((uint32_t)v, 11);
            } else {
                int64_t q = (T + 512) >> 10;
                unsigned i, f, num;
                q = q < 0 ? 0 : q > 8192 ? 8192 : q;
                i = (unsigned)(q >> 4); if (i > 511) i = 511;
                f = (unsigned)q - 16u * i;
                /* (16 K[i] + (K[i+1] - K[i]) f) / 16, nearest even */
                num = 16u * srgb_knots[i] + (unsigned)(srgb_knots[i + 1] - srgb_knots[i]) * f;
                {
                    unsigned step = num >> 4, rem = num & 15u;
                    if (rem > 8u || (rem == 8u && (step & 1u))) step++;
                    h[c] = tegu_ycbcr_half(step, 13);
                }
            }
        }
        h[3] = 0x3c00u;
    }
}

/* Fast path for the CoreAnimation case (8-bit unpacked codes, converted RGB):
 * per-surface lookup tables (luma term, chroma products, the 2049 halves of
 * v / 2048) and per-row vertical chroma blends, bit-identical to
 * tegu_ycbcr_decode_row (checked by tests/ycbcr_decode_test.py). */
struct tegu_ycbcr_fast {
    int32_t luma[256], cb[3][256], cr[3][256];
    uint16_t half[2049];
};
static inline int tegu_ycbcr_fast_init(const struct tegu_ycbcr_layout *l, struct tegu_ycbcr_fast *t)
{
    int k[3][2];
    if (l->bits != 8 || l->packed || l->output != TEGU_YCBCR_OUT_CONVERTED) return 0;
    tegu_ycbcr_coefficients(l->full_range, l->matrix, k);
    for (unsigned c = 0; c < 256; c++) {
        t->luma[c] = l->full_range ? 256 * (int32_t)((c * 32768u + 127u) / 255u) : 9576 * ((int32_t)(c << 2) - 64);
        for (unsigned ch = 0; ch < 3; ch++) {
            t->cb[ch][c] = k[ch][0] * ((int32_t)(c << 2) - 512);
            t->cr[ch][c] = k[ch][1] * ((int32_t)(c << 2) - 512);
        }
    }
    for (unsigned v = 0; v <= 2048; v++) t->half[v] = tegu_ycbcr_half(v, 11);
    return 1;
}
/* One row through the tables; `blend` is scratch of 2*cw uint16 (vertical
 * chroma sums for this row: 3*near + far, or 4*own). */
static inline void tegu_ycbcr_fast_row(const struct tegu_ycbcr_layout *l, const struct tegu_ycbcr_fast *t,
    const uint8_t *luma, size_t luma_row, const uint8_t *chroma, size_t chroma_row,
    unsigned width, unsigned cw, unsigned ch, unsigned y, uint16_t *out, uint16_t *blend)
{
    const uint8_t *lr = luma + (size_t)y * luma_row, *c0, *c1;
    unsigned wy0, wy1;
    if (l->sub == TEGU_YCBCR_SUB_420) {
        int near = (int)(y / 2u), far = (y & 1u) ? near + 1 : near - 1;
        if (far < 0) far = 0;
        if (far > (int)ch - 1) far = (int)ch - 1;
        if (near > (int)ch - 1) near = (int)ch - 1;
        c0 = chroma + (size_t)near * chroma_row; c1 = chroma + (size_t)far * chroma_row; wy0 = 3; wy1 = 1;
    } else {
        c0 = c1 = chroma + (size_t)(y < ch ? y : ch - 1u) * chroma_row; wy0 = 4; wy1 = 0;
    }
    for (unsigned i = 0; i < 2u * cw; i++) blend[i] = (uint16_t)(wy0 * c0[i] + wy1 * c1[i]);
    for (unsigned x = 0; x < width; x++) {
        unsigned cb, cr;
        if (l->sub == TEGU_YCBCR_SUB_444) {
            unsigned cx = x < cw ? x : cw - 1u;
            cb = ((unsigned)blend[2u * cx] * 4u + 8u) >> 4; cr = ((unsigned)blend[2u * cx + 1u] * 4u + 8u) >> 4;
        } else {
            int near = (int)(x / 2u), far = (x & 1u) ? near + 1 : near - 1;
            if (far < 0) far = 0;
            if (far > (int)cw - 1) far = (int)cw - 1;
            if (near > (int)cw - 1) near = (int)cw - 1;
            cb = (3u * blend[2u * (unsigned)near] + blend[2u * (unsigned)far] + 8u) >> 4;
            cr = (3u * blend[2u * (unsigned)near + 1u] + blend[2u * (unsigned)far + 1u] + 8u) >> 4;
        }
        int32_t L = t->luma[lr[x]];
        uint16_t *h = out + (size_t)x * 4u;
        for (unsigned c = 0; c < 3; c++) {
            int32_t v = (L + t->cb[c][cb] + t->cr[c][cr] + 2048) >> 12;
            h[c] = t->half[v < 0 ? 0 : v > 2048 ? 2048 : v];
        }
        h[3] = 0x3c00u;
    }
}

/* Converted halves (v / 2048, v in 0..2048) -> RGB10A2 unorm texels
 * (code = round(v * 1023 / 2048), alpha 3): the memory-halving shadow form,
 * within half a 10-bit step of Metal's 1/2048 grid. */
static inline uint32_t tegu_ycbcr_code10(uint16_t h)
{
    unsigned e = (h >> 10) & 31u, m = h & 1023u, v;
    if (!e) v = 0;
    else v = e >= 14 ? (1024u + m) << (e - 14u) : (1024u + m) >> (14u - e);
    if (v > 2048u) v = 2048u;
    return (v * 1023u + 1024u) >> 11;
}
static inline void tegu_ycbcr_pack10_row(const uint16_t *halves, uint32_t *out, unsigned width)
{
    for (unsigned x = 0; x < width; x++)
        out[x] = tegu_ycbcr_code10(halves[x * 4u]) | (tegu_ycbcr_code10(halves[x * 4u + 1u]) << 10) |
                 (tegu_ycbcr_code10(halves[x * 4u + 2u]) << 20) | (3u << 30);
}
#endif
