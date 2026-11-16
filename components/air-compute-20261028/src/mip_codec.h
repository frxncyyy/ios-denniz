/* Per-format box-filter mip generation for generateMipmapsForTexture (CPU).
 *
 * Pure C, builtins only (no libc headers, no libm). Each texel is decoded to
 * four floats, the source box (same footprint rule as the original filter:
 * source columns [x*sw/w, (x+1)*sw/w), rows likewise) is averaged and the
 * result encoded back with round-to-nearest. Formats: unorm/snorm 8 and 16,
 * half and float32, packed BGR5A1 / B5G6R5 / ABGR4 / RGB10A2 / BGR10A2, and
 * the 8-bit sRGB formats, which Metal filters in linear space. Metal does not
 * generate mipmaps for integer formats; compressed formats are out of scope.
 * Needs xr_decode_table.h (tegu_srgb8_to_linear / _encode_threshold). */
#ifndef TEGU_MIP_CODEC_H
#define TEGU_MIP_CODEC_H
#include <stdint.h>
#include <stddef.h>

enum {TEGU_MIP_UNORM8, TEGU_MIP_SRGB8, TEGU_MIP_SNORM8, TEGU_MIP_UNORM16, TEGU_MIP_SNORM16,
      TEGU_MIP_HALF, TEGU_MIP_FLOAT, TEGU_MIP_PACKED16, TEGU_MIP_PACKED32};

struct tegu_mip_format {
    uint32_t format;
    uint8_t kind, bytes, channels;
    uint8_t bits[4], shift[4]; /* packed kinds only */
};

static inline int tegu_mip_codec(uint32_t format, struct tegu_mip_format *out)
{
    static const struct tegu_mip_format table[] = {
        {1, TEGU_MIP_UNORM8, 1, 1, {0}, {0}}, {10, TEGU_MIP_UNORM8, 1, 1, {0}, {0}},
        {11, TEGU_MIP_SRGB8, 1, 1, {0}, {0}}, {12, TEGU_MIP_SNORM8, 1, 1, {0}, {0}},
        {20, TEGU_MIP_UNORM16, 2, 1, {0}, {0}}, {22, TEGU_MIP_SNORM16, 2, 1, {0}, {0}},
        {25, TEGU_MIP_HALF, 2, 1, {0}, {0}},
        {30, TEGU_MIP_UNORM8, 2, 2, {0}, {0}}, {31, TEGU_MIP_SRGB8, 2, 2, {0}, {0}},
        {32, TEGU_MIP_SNORM8, 2, 2, {0}, {0}},
        {40, TEGU_MIP_PACKED16, 2, 3, {5, 6, 5, 0}, {0, 5, 11, 0}},          /* B5G6R5 */
        {42, TEGU_MIP_PACKED16, 2, 4, {4, 4, 4, 4}, {0, 4, 8, 12}},         /* ABGR4 */
        {43, TEGU_MIP_PACKED16, 2, 4, {5, 5, 5, 1}, {0, 5, 10, 15}},        /* BGR5A1 */
        {55, TEGU_MIP_FLOAT, 4, 1, {0}, {0}},
        {60, TEGU_MIP_UNORM16, 4, 2, {0}, {0}}, {62, TEGU_MIP_SNORM16, 4, 2, {0}, {0}},
        {65, TEGU_MIP_HALF, 4, 2, {0}, {0}},
        {70, TEGU_MIP_UNORM8, 4, 4, {0}, {0}}, {71, TEGU_MIP_SRGB8, 4, 4, {0}, {0}},
        {72, TEGU_MIP_SNORM8, 4, 4, {0}, {0}},
        {80, TEGU_MIP_UNORM8, 4, 4, {0}, {0}}, {81, TEGU_MIP_SRGB8, 4, 4, {0}, {0}},
        {90, TEGU_MIP_PACKED32, 4, 4, {10, 10, 10, 2}, {0, 10, 20, 30}},    /* RGB10A2 */
        {94, TEGU_MIP_PACKED32, 4, 4, {10, 10, 10, 2}, {0, 10, 20, 30}},    /* BGR10A2 */
        {105, TEGU_MIP_FLOAT, 8, 2, {0}, {0}},
        {110, TEGU_MIP_UNORM16, 8, 4, {0}, {0}}, {112, TEGU_MIP_SNORM16, 8, 4, {0}, {0}},
        {115, TEGU_MIP_HALF, 8, 4, {0}, {0}},
        {125, TEGU_MIP_FLOAT, 16, 4, {0}, {0}},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (table[i].format == format) { if (out) *out = table[i]; return 1; }
    return 0;
}

static inline float tegu_mip_half_value(uint16_t h)
{
    unsigned e = (h >> 10) & 31u, m = h & 1023u;
    float v;
    if (e == 31u) v = m ? 0.0f : 65504.0f; /* Inf/NaN never come from a filter */
    else if (!e) v = (float)m * (1.0f / 16777216.0f);
    else v = (float)(1024u + m) * (e >= 25u ? (float)(1u << (e - 25u)) : 1.0f / (float)(1u << (25u - e)));
    return (h & 0x8000u) ? -v : v;
}
/* Nearest-even half of a finite float in half range. */
static inline uint16_t tegu_mip_half_bits(float f)
{
    union { float f; uint32_t u; } x = { f };
    uint32_t sign = (x.u >> 16) & 0x8000u, a = x.u & 0x7fffffffu;
    if (a >= 0x477ff000u) return (uint16_t)(sign | 0x7bffu);
    if (a < 0x38800000u) {                 /* subnormal half */
        if (a < 0x33000000u) return (uint16_t)sign;
        uint32_t m = (a & 0x7fffffu) | 0x800000u, shift = 113u - (a >> 23) + 13u;
        uint32_t q = m >> shift, rem = m & ((1u << shift) - 1u), half = 1u << (shift - 1u);
        if (rem > half || (rem == half && (q & 1u))) q++;
        return (uint16_t)(sign | q);
    }
    uint32_t q = (a - 0x38000000u) >> 13, rem = a & 0x1fffu;
    if (rem > 0x1000u || (rem == 0x1000u && (q & 1u))) q++;
    return (uint16_t)(sign | q);
}
static inline uint32_t tegu_mip_round_unorm(float v, uint32_t max)
{
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f) return max;
    return (uint32_t)(v * (float)max + 0.5f);
}
static inline int32_t tegu_mip_round_snorm(float v, int32_t max)
{
    if (!(v > -1.0f)) return -max;
    if (v >= 1.0f) return max;
    float s = v * (float)max;
    return (int32_t)(s >= 0.0f ? s + 0.5f : s - 0.5f);
}

static inline void tegu_mip_decode(const struct tegu_mip_format *f, const uint8_t *p, float out[4])
{
    for (unsigned c = 0; c < 4; c++) out[c] = 0.0f;
    switch (f->kind) {
    case TEGU_MIP_UNORM8: for (unsigned c = 0; c < f->channels; c++) out[c] = p[c] * (1.0f / 255.0f); break;
    case TEGU_MIP_SRGB8:
        for (unsigned c = 0; c < f->channels; c++)
            out[c] = c == 3 ? p[c] * (1.0f / 255.0f) : tegu_srgb8_to_linear[p[c]];
        break;
    case TEGU_MIP_SNORM8:
        for (unsigned c = 0; c < f->channels; c++) {
            float v = (float)(int8_t)p[c] / 127.0f; out[c] = v < -1.0f ? -1.0f : v;
        }
        break;
    case TEGU_MIP_UNORM16: case TEGU_MIP_SNORM16: case TEGU_MIP_HALF:
        for (unsigned c = 0; c < f->channels; c++) {
            uint16_t w; __builtin_memcpy(&w, p + 2u * c, 2);
            if (f->kind == TEGU_MIP_UNORM16) out[c] = w * (1.0f / 65535.0f);
            else if (f->kind == TEGU_MIP_SNORM16) { float v = (float)(int16_t)w / 32767.0f; out[c] = v < -1.0f ? -1.0f : v; }
            else out[c] = tegu_mip_half_value(w);
        }
        break;
    case TEGU_MIP_FLOAT: for (unsigned c = 0; c < f->channels; c++) __builtin_memcpy(&out[c], p + 4u * c, 4); break;
    case TEGU_MIP_PACKED16: case TEGU_MIP_PACKED32: {
        uint32_t w = 0;
        if (f->kind == TEGU_MIP_PACKED16) { uint16_t h; __builtin_memcpy(&h, p, 2); w = h; }
        else __builtin_memcpy(&w, p, 4);
        for (unsigned c = 0; c < f->channels; c++) {
            uint32_t mask = (1u << f->bits[c]) - 1u;
            out[c] = (float)((w >> f->shift[c]) & mask) / (float)mask;
        }
        break;
    }
    }
}

static inline void tegu_mip_encode(const struct tegu_mip_format *f, const float in[4], uint8_t *p)
{
    switch (f->kind) {
    case TEGU_MIP_UNORM8: for (unsigned c = 0; c < f->channels; c++) p[c] = (uint8_t)tegu_mip_round_unorm(in[c], 255); break;
    case TEGU_MIP_SRGB8:
        for (unsigned c = 0; c < f->channels; c++) {
            if (c == 3) { p[c] = (uint8_t)tegu_mip_round_unorm(in[c], 255); continue; }
            unsigned lo = 0, hi = 255; /* first code whose upper threshold exceeds v */
            while (lo < hi) { unsigned mid = (lo + hi) / 2u; if (tegu_srgb8_encode_threshold[mid] <= in[c]) lo = mid + 1u; else hi = mid; }
            p[c] = (uint8_t)lo;
        }
        break;
    case TEGU_MIP_SNORM8: for (unsigned c = 0; c < f->channels; c++) p[c] = (uint8_t)(int8_t)tegu_mip_round_snorm(in[c], 127); break;
    case TEGU_MIP_UNORM16: case TEGU_MIP_SNORM16: case TEGU_MIP_HALF:
        for (unsigned c = 0; c < f->channels; c++) {
            uint16_t w = f->kind == TEGU_MIP_UNORM16 ? (uint16_t)tegu_mip_round_unorm(in[c], 65535) :
                         f->kind == TEGU_MIP_SNORM16 ? (uint16_t)(int16_t)tegu_mip_round_snorm(in[c], 32767) :
                         tegu_mip_half_bits(in[c]);
            __builtin_memcpy(p + 2u * c, &w, 2);
        }
        break;
    case TEGU_MIP_FLOAT: for (unsigned c = 0; c < f->channels; c++) __builtin_memcpy(p + 4u * c, &in[c], 4); break;
    case TEGU_MIP_PACKED16: case TEGU_MIP_PACKED32: {
        uint32_t w = 0;
        for (unsigned c = 0; c < f->channels; c++)
            w |= tegu_mip_round_unorm(in[c], (1u << f->bits[c]) - 1u) << f->shift[c];
        if (f->kind == TEGU_MIP_PACKED16) { uint16_t h = (uint16_t)w; __builtin_memcpy(p, &h, 2); }
        else __builtin_memcpy(p, &w, 4);
        break;
    }
    }
}

/* One level from the previous one: dw x dh destination texels from sw x sh
 * source texels (the original filter's box footprint). */
static inline void tegu_mip_downsample(const struct tegu_mip_format *f,
    const uint8_t *src, size_t sstride, size_t sw, size_t sh,
    uint8_t *dst, size_t dstride, size_t dw, size_t dh)
{
    for (size_t y = 0; y < dh; y++)
        for (size_t x = 0; x < dw; x++) {
            size_t x0 = x * sw / dw, x1 = (x + 1) * sw / dw, y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            float sum[4] = {0, 0, 0, 0}, t[4];
            for (size_t sy = y0; sy < y1; sy++)
                for (size_t sx = x0; sx < x1; sx++) {
                    tegu_mip_decode(f, src + sy * sstride + sx * f->bytes, t);
                    for (unsigned c = 0; c < 4; c++) sum[c] += t[c];
                }
            float n = (float)((x1 - x0) * (y1 - y0));
            for (unsigned c = 0; c < 4; c++) sum[c] /= n;
            tegu_mip_encode(f, sum, dst + y * dstride + x * f->bytes);
        }
}
#endif
