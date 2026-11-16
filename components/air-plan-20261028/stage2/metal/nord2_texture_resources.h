/* Nord2 arch9 texture/sampler resource contract, version 1.
 *
 * Pure, dependency-free, freestanding C11: no allocation, no mapping, no
 * submission, no cache operations and no resource lifetime. Callers resolve
 * retained GPU addresses; this code validates immutable facts and packs
 * descriptors.
 *
 * Descriptor field semantics are derived from pinned Mesa 81daea6
 * src/panfrost/lib/pan_texture.c, pan_format.c, pan_layout.c and
 * genxml/v9.xml (arch9) and v10.xml only where Valhall pack layout is useful.
 * The shared arch9 plane payload is exactly words0..6; word7 is reserved and
 * is emitted as zero.  The old qualified n2r/arch11 v10 extent tail remains in
 * the legacy path, not in this shared arch9/v2 payload.
 *
 * The old 2D, one-level, one-layer, one-sample path's words0..6 are preserved;
 * callers needing those facts can use format-default swizzle, row_stride per
 * level, layer_stride=0, slice_stride=0 and samples=1.
 *
 * No hardware qualification is implied by a successful pack.  Unsupported
 * cases return a specific negative code and leave every output unchanged. */
#ifndef NORD2_TEXTURE_RESOURCES_H
#define NORD2_TEXTURE_RESOURCES_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define N2T_TEXTURE_RESOURCES_VERSION 1u
#define N2T_MAX_LEVELS 16u
#define N2T_MAX_LAYERS 2048u
#define N2T_MAX_SUBRESOURCES 256u
#define N2T_MAX_DIMENSION 16384u
#define N2T_MAX_DEPTH 2048u
#define N2T_MAX_ADDRESS (1ull << 48)
#define N2T_TEXTURE_DESCRIPTOR_BYTES 32u
#define N2T_TEXTURE_PLANE_BYTES 32u
#define N2T_TEXTURE_SAMPLER_BYTES 32u

/* Stable negative results.  -15 is a deliberate hardware/contract refusal,
 * never an assertion or a silent substitution. */
#define N2T_OK 0
#define N2T_E_INVALID (-1)
#define N2T_E_KIND (-2)
#define N2T_E_FORMAT (-3)
#define N2T_E_DIMENSIONS (-4)
#define N2T_E_LEVELS (-5)
#define N2T_E_LAYERS (-6)
#define N2T_E_SAMPLES (-7)
#define N2T_E_STRIDE (-8)
#define N2T_E_SPAN (-9)
#define N2T_E_OVERFLOW (-10)
#define N2T_E_ADDRESS (-11)
#define N2T_E_CAPACITY (-12)
#define N2T_E_SAMPLER_FIELD (-13)
#define N2T_E_SAMPLER_COMBINATION (-14)
#define N2T_E_UNSUPPORTED (-15)
#define N2T_E_ACCESS (-16)
#define N2T_E_VIEW (-17)
#define N2T_E_OUTPUT (-18)
#define N2T_E_COUNT (-19)

/* Metal MTLTextureType numbering, as used by the installed frontend. */
#define N2T_KIND_1D 0u
#define N2T_KIND_1D_ARRAY 1u
#define N2T_KIND_2D 2u
#define N2T_KIND_2D_ARRAY 3u
#define N2T_KIND_2D_MS 4u
#define N2T_KIND_CUBE 5u
#define N2T_KIND_CUBE_ARRAY 6u
#define N2T_KIND_3D 7u
#define N2T_KIND_2D_MS_ARRAY 8u
#define N2T_KIND_TEXTURE_BUFFER 9u

#define N2T_ACCESS_SAMPLE 1u
#define N2T_ACCESS_STORAGE_READ 2u
#define N2T_ACCESS_STORAGE_WRITE 4u

/* Format flags. */
#define N2T_FMT_COMPRESSED 1u
#define N2T_FMT_SRGB 2u
#define N2T_FMT_DEPTH 4u
#define N2T_FMT_FLOAT 8u
#define N2T_FMT_INTEGER 16u   /* unsigned or signed integer image */
#define N2T_FMT_SIGNED 32u
#define N2T_FMT_STORAGE 64u   /* valid as an image load/store format */
#define N2T_FMT_ALPHA 128u    /* ClampToZero uses transparent black */

/* Metal sampler enums. */
#define N2T_FILTER_NEAREST 0u
#define N2T_FILTER_LINEAR 1u
#define N2T_MIP_NOT_MIPMAPPED 0u
#define N2T_MIP_NEAREST 1u
#define N2T_MIP_LINEAR 2u
#define N2T_ADDRESS_CLAMP_EDGE 0u
#define N2T_ADDRESS_MIRROR_CLAMP_EDGE 1u
#define N2T_ADDRESS_REPEAT 2u
#define N2T_ADDRESS_MIRROR_REPEAT 3u
#define N2T_ADDRESS_CLAMP_ZERO 4u
#define N2T_ADDRESS_CLAMP_BORDER 5u
#define N2T_COMPARE_NEVER 0u
#define N2T_COMPARE_LESS 1u
#define N2T_COMPARE_EQUAL 2u
#define N2T_COMPARE_LEQUAL 3u
#define N2T_COMPARE_GREATER 4u
#define N2T_COMPARE_NOT_EQUAL 5u
#define N2T_COMPARE_GEQUAL 6u
#define N2T_COMPARE_ALWAYS 7u
#define N2T_REDUCTION_AVERAGE 0u
#define N2T_REDUCTION_MIN 1u
#define N2T_REDUCTION_MAX 2u
#define N2T_LOD_ISOTROPIC 0u
#define N2T_LOD_ANISOTROPIC 3u
#define N2T_BORDER_TRANSPARENT_BLACK 0u
#define N2T_BORDER_OPAQUE_BLACK 1u
#define N2T_BORDER_OPAQUE_WHITE 2u
#define N2T_BORDER_CUSTOM_FLOAT 3u
#define N2T_BORDER_CUSTOM_RAW 4u

/* Metal component order values for the 12-bit swizzle word. */
#define N2T_CHANNEL_R 0u
#define N2T_CHANNEL_G 1u
#define N2T_CHANNEL_B 2u
#define N2T_CHANNEL_A 3u
#define N2T_CHANNEL_0 4u
#define N2T_CHANNEL_1 5u
#define N2T_SWIZZLE_IDENTITY 0x688u

struct n2t_format_info {
    uint32_t version;
    uint32_t mali_format;      /* order | pixel format<<12 | sRGB<<20 */
    uint32_t bytes_per_block;
    uint32_t block_width, block_height;
    uint32_t clump_format;     /* GENERIC_PLANE clump format */
    uint32_t astc_plane;       /* 4 = ASTC 2D plane, 0 = generic plane */
    uint32_t astc_block_width, astc_block_height; /* pixels; 0 when generic */
    uint32_t default_swizzle;
    uint32_t flags;
};

struct n2t_level {
    /* View-relative byte offset from the retained binding base.  The caller
     * resolves the base at (view first_layer, view first_level); first_layer
     * and first_level below are physical metadata and are NOT added again. */
    uint64_t offset;
    uint32_t row_stride;       /* bytes between rows/blocks */
    uint32_t slice_stride;     /* 3D z stride or MS sample stride; else 0 */
    uint32_t layer_stride;     /* array/cube layer stride; 0 if layers==1 */
    uint32_t width, height, depth; /* pixels; view-level dimensions */
};

struct n2t_texture_layout {
    uint32_t version;
    uint32_t kind;             /* N2T_KIND_* */
    uint32_t format;           /* allocation Metal MTLPixelFormat */
    uint32_t view_format;      /* 0 = allocation format; else audited view */
    uint32_t width, height, depth; /* view base level dimensions */
    uint32_t levels;           /* view levels, 1..16 */
    uint32_t layers;           /* view layers including cube faces */
    uint32_t samples;
    uint32_t array_length;     /* physical array length; 0 means layers */
    /* Informational only: level[].offset and layer_stride are already
     * view-relative.  Do not add first_layer/first_level to addresses twice. */
    uint32_t first_level, first_layer;
    uint32_t swizzle_valid;    /* 1 = use swizzle_word; 0 = format default */
    uint32_t swizzle_word;     /* 12-bit Mali component swizzle when valid */
    uint32_t usage;            /* Metal usage bits, informational */
    uint32_t storage_mode;     /* Metal storage mode */
    uint32_t access;           /* N2T_ACCESS_* */
    struct n2t_level level[N2T_MAX_LEVELS];
};

struct n2t_subresource {
    uint64_t offset;           /* backing-relative, including layer stride */
    uint64_t address;          /* resolved address, or offset when base 0 */
    uint64_t span;             /* bytes covered by this entry */
    uint32_t row_stride, slice_stride, size;
    uint32_t width, height, depth, rows, row_bytes;
};

struct n2t_sampler_state {
    uint32_t version;
    uint32_t min_filter, mag_filter, mip_filter;
    uint32_t address_s, address_t, address_r;
    uint32_t normalized;
    uint32_t compare_enabled;  /* plan-derived use: 1 only for compare sample */
    uint32_t compare_function;
    uint32_t max_anisotropy;
    uint32_t reduction;
    uint32_t lod_algorithm;
    uint32_t clamp_integer_coords;
    uint32_t clamp_integer_array;
    uint32_t srgb_override;
    uint32_t seamless_cube;
    uint32_t magnify_cutoff;
    uint32_t border_color_kind; /* presets, custom float, custom raw */
    float lod_min, lod_max, lod_bias;
    float border[4];
    uint32_t border_raw[4];    /* used when kind == N2T_BORDER_CUSTOM_RAW */
};

struct n2t_packed_texture {
    uint32_t version;
    uint32_t descriptor_words[8];
    uint32_t plane_words[8];   /* first payload entry, convenience only */
    uint32_t plane_count;
    uint32_t payload_bytes;
    uint32_t mali_format;
    uint32_t levels, array_size, sample_count;
    uint64_t descriptor_address, payload_address;
};

static inline const char *n2t_error_string(int error)
{
    switch (error) {
    case N2T_OK: return "ok";
    case N2T_E_INVALID: return "invalid argument or version";
    case N2T_E_KIND: return "texture kind unsupported";
    case N2T_E_FORMAT: return "pixel format unsupported";
    case N2T_E_DIMENSIONS: return "dimensions invalid";
    case N2T_E_LEVELS: return "level count invalid";
    case N2T_E_LAYERS: return "layer count invalid";
    case N2T_E_SAMPLES: return "sample count invalid";
    case N2T_E_STRIDE: return "stride invalid";
    case N2T_E_SPAN: return "backing span invalid or overlapping";
    case N2T_E_OVERFLOW: return "arithmetic overflow";
    case N2T_E_ADDRESS: return "address invalid, misaligned or wrapping";
    case N2T_E_CAPACITY: return "output capacity too small";
    case N2T_E_SAMPLER_FIELD: return "sampler field invalid";
    case N2T_E_SAMPLER_COMBINATION: return "sampler combination invalid";
    case N2T_E_UNSUPPORTED: return "unsupported by arch9 contract";
    case N2T_E_ACCESS: return "access flags invalid";
    case N2T_E_VIEW: return "view range invalid";
    case N2T_E_OUTPUT: return "output spans overlap or are invalid";
    case N2T_E_COUNT: return "subresource count bound exceeded";
    default: return "unknown error";
    }
}

static inline int n2t_add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b) return N2T_E_OVERFLOW;
    *out = a + b;
    return N2T_OK;
}

static inline int n2t_mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a && b > UINT64_MAX / a) return N2T_E_OVERFLOW;
    *out = a * b;
    return N2T_OK;
}

static inline uint64_t n2t_minify(uint64_t value, uint32_t levels)
{
    while (levels--) value = value > 1 ? value / 2 : 1;
    return value;
}

static inline uint32_t n2t_ceil_div_u32(uint32_t a, uint32_t b)
{
    return b ? (a + b - 1u) / b : 0;
}

static inline int n2t_finite_float(float value)
{
    union { float f; uint32_t u; } v;
    v.f = value;
    return (v.u & 0x7f800000u) != 0x7f800000u;
}

static inline int n2t_float_bits(float value, uint32_t *out)
{
    union { float f; uint32_t u; } v;
    v.f = value;
    *out = v.u;
    return N2T_OK;
}

/* Header-only format oracle.  Returns N2T_E_FORMAT for a format that is not
 * in the implemented arch9 contract; that is a coverage outcome, not a skip. */
static inline int n2t_format_query(uint32_t metal, struct n2t_format_info *out)
{
    static const uint8_t astc_dims[15][2] = {
        {4,4},{5,4},{5,5},{6,5},{6,6},{0,0},{8,5},{8,6},{8,8},
        {10,5},{10,6},{10,8},{10,10},{12,10},{12,12}
    };
    struct n2t_format_info f;
    uint32_t i, code, order, flags, bpp, bw, bh, clump, plane, aw, ah, swz, alpha;
    if (!out) return N2T_E_INVALID;
    f.version = N2T_TEXTURE_RESOURCES_VERSION;
    f.mali_format = 0; f.bytes_per_block = 0; f.block_width = 1; f.block_height = 1;
    f.clump_format = 0; f.astc_plane = 0; f.astc_block_width = 0; f.astc_block_height = 0;
    f.default_swizzle = N2T_SWIZZLE_IDENTITY; f.flags = 0;
    code = 0; order = 0; flags = 0; bpp = 0; bw = 1; bh = 1; clump = 0; plane = 0; aw = 0; ah = 0;
    swz = N2T_SWIZZLE_IDENTITY; alpha = 0;
    switch (metal) {
    case 1:   /* A8Unorm: one alpha byte; sample as R then .A=R */
        code = 163; order = 16; bpp = 1; swz = 0x124; alpha = 1; break;
    case 10: code = 163; order = 16; bpp = 1; flags = N2T_FMT_STORAGE; break;
    case 11: code = 163; order = 16; bpp = 1; flags = N2T_FMT_SRGB; break;
    case 12: code = 99;  order = 16; bpp = 1; flags = N2T_FMT_STORAGE; break;
    case 13: code = 131; order = 16; bpp = 1; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 14: code = 195; order = 16; bpp = 1; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 20: code = 164; order = 16; bpp = 2; flags = N2T_FMT_STORAGE; break;
    case 22: code = 100; order = 16; bpp = 2; flags = N2T_FMT_STORAGE; break;
    case 23: code = 132; order = 16; bpp = 2; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 24: code = 196; order = 16; bpp = 2; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 25: code = 199; order = 16; bpp = 2; flags = N2T_FMT_FLOAT | N2T_FMT_STORAGE; break;
    case 30: code = 171; order = 16; bpp = 2; flags = N2T_FMT_STORAGE; break;
    case 31: code = 171; order = 16; bpp = 2; flags = N2T_FMT_SRGB; break;
    case 32: code = 107; order = 16; bpp = 2; flags = N2T_FMT_STORAGE; break;
    case 33: code = 139; order = 16; bpp = 2; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 34: code = 203; order = 16; bpp = 2; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 40: code = 64;  order = 20; bpp = 2; break; /* B5G6R5 */
    case 41: return N2T_E_FORMAT; /* A1BGR5: no independent Mesa mapping used here */
    case 42: code = 186; order = 12; bpp = 2; alpha = 1; break; /* ABGR4 */
    case 43: code = 65;  order = 4;  bpp = 2; alpha = 1; break; /* BGR5A1 */
    case 53: code = 133; order = 16; bpp = 4; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 54: code = 197; order = 16; bpp = 4; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 55: code = 167; order = 16; bpp = 4; flags = N2T_FMT_FLOAT | N2T_FMT_STORAGE; break;
    case 60: code = 172; order = 16; bpp = 4; flags = N2T_FMT_STORAGE; break;
    case 62: code = 108; order = 16; bpp = 4; flags = N2T_FMT_STORAGE; break;
    case 63: code = 140; order = 16; bpp = 4; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 64: code = 204; order = 16; bpp = 4; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 65: code = 207; order = 16; bpp = 4; flags = N2T_FMT_FLOAT | N2T_FMT_STORAGE; break;
    case 70: code = 187; order = 0;  bpp = 4; alpha = 1; flags = N2T_FMT_STORAGE; break;
    case 71: code = 187; order = 0;  bpp = 4; alpha = 1; flags = N2T_FMT_SRGB; break;
    case 72: code = 123; order = 0;  bpp = 4; alpha = 1; flags = N2T_FMT_STORAGE; break;
    case 73: code = 155; order = 0;  bpp = 4; alpha = 1; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 74: code = 219; order = 0;  bpp = 4; alpha = 1; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 80: code = 187; order = 4;  bpp = 4; alpha = 1; flags = N2T_FMT_STORAGE; break;
    case 81: code = 187; order = 4;  bpp = 4; alpha = 1; flags = N2T_FMT_SRGB; break;
    case 90: code = 67;  order = 0;  bpp = 4; alpha = 1; flags = N2T_FMT_STORAGE; break;
    case 91: code = 71;  order = 0;  bpp = 4; alpha = 1; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 92: code = 89;  order = 16; bpp = 4; flags = N2T_FMT_FLOAT; break;
    case 93: code = 91;  order = 16; bpp = 4; flags = N2T_FMT_FLOAT; break;
    case 94: code = 67;  order = 4;  bpp = 4; alpha = 1; flags = N2T_FMT_STORAGE; break;
    case 103: code = 141; order = 16; bpp = 8; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 104: code = 205; order = 16; bpp = 8; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 105: code = 175; order = 16; bpp = 8; flags = N2T_FMT_FLOAT | N2T_FMT_STORAGE; break;
    case 110: code = 188; order = 0;  bpp = 8; alpha = 1; flags = N2T_FMT_STORAGE; break;
    case 112: code = 124; order = 0;  bpp = 8; alpha = 1; flags = N2T_FMT_STORAGE; break;
    case 113: code = 156; order = 0;  bpp = 8; alpha = 1; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 114: code = 220; order = 0;  bpp = 8; alpha = 1; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 115: code = 223; order = 0;  bpp = 8; alpha = 1; flags = N2T_FMT_FLOAT | N2T_FMT_STORAGE; break;
    case 123: code = 157; order = 0;  bpp = 16; alpha = 1; flags = N2T_FMT_INTEGER | N2T_FMT_STORAGE; break;
    case 124: code = 221; order = 0;  bpp = 16; alpha = 1; flags = N2T_FMT_INTEGER | N2T_FMT_SIGNED | N2T_FMT_STORAGE; break;
    case 125: code = 191; order = 0;  bpp = 16; alpha = 1; flags = N2T_FMT_FLOAT | N2T_FMT_STORAGE; break;
    case 250: code = 75;  order = 0;  bpp = 2; flags = N2T_FMT_DEPTH; break;
    case 252: code = 76;  order = 0;  bpp = 4; flags = N2T_FMT_DEPTH; break;
    case 170: /* EAC_R11Unorm */ code = 164; order = 16; bpp = 8; bw = 4; bh = 4; clump = 33; flags = N2T_FMT_COMPRESSED; break;
    case 172: /* EAC_R11Snorm */ code = 100; order = 16; bpp = 8; bw = 4; bh = 4; clump = 34; flags = N2T_FMT_COMPRESSED; break;
    case 174: /* EAC_RG11Unorm */ code = 172; order = 16; bpp = 16; bw = 4; bh = 4; clump = 35; flags = N2T_FMT_COMPRESSED; break;
    case 176: /* EAC_RG11Snorm */ code = 108; order = 16; bpp = 16; bw = 4; bh = 4; clump = 36; flags = N2T_FMT_COMPRESSED; break;
    case 178: /* EAC_RGBA8 */ code = 187; order = 0; bpp = 16; bw = 4; bh = 4; clump = 37; alpha = 1; flags = N2T_FMT_COMPRESSED; break;
    case 179: code = 187; order = 0; bpp = 16; bw = 4; bh = 4; clump = 37; alpha = 1; flags = N2T_FMT_COMPRESSED | N2T_FMT_SRGB; break;
    case 180: /* ETC2 RGB8 */ code = 187; order = 16; bpp = 8; bw = 4; bh = 4; clump = 32; flags = N2T_FMT_COMPRESSED; break;
    case 181: code = 187; order = 16; bpp = 8; bw = 4; bh = 4; clump = 32; flags = N2T_FMT_COMPRESSED | N2T_FMT_SRGB; break;
    case 182: /* ETC2 RGB8A1 */ code = 187; order = 0; bpp = 8; bw = 4; bh = 4; clump = 38; alpha = 1; flags = N2T_FMT_COMPRESSED; break;
    case 183: code = 187; order = 0; bpp = 8; bw = 4; bh = 4; clump = 38; alpha = 1; flags = N2T_FMT_COMPRESSED | N2T_FMT_SRGB; break;
    default: break;
    }
    if (metal >= 186 && metal <= 200) {
        i = metal - 186; if (i >= 15 || !astc_dims[i][0]) return N2T_E_FORMAT;
        code = 187; order = 0; bpp = 16; bw = astc_dims[i][0]; bh = astc_dims[i][1];
        plane = 4; aw = bw; ah = bh; alpha = 1;
        flags = N2T_FMT_COMPRESSED | N2T_FMT_SRGB;
    } else if (metal >= 204 && metal <= 218) {
        i = metal - 204; if (i >= 15 || !astc_dims[i][0]) return N2T_E_FORMAT;
        code = 223; order = 0; bpp = 16; bw = astc_dims[i][0]; bh = astc_dims[i][1];
        plane = 4; aw = bw; ah = bh; alpha = 1; flags = N2T_FMT_COMPRESSED | N2T_FMT_FLOAT;
    } else if (metal >= 222 && metal <= 236) {
        i = metal - 222; if (i >= 15 || !astc_dims[i][0]) return N2T_E_FORMAT;
        code = 223; order = 0; bpp = 16; bw = astc_dims[i][0]; bh = astc_dims[i][1];
        plane = 4; aw = bw; ah = bh; alpha = 1; flags = N2T_FMT_COMPRESSED | N2T_FMT_FLOAT;
    }
    if (!bpp) return N2T_E_FORMAT;
    if (!plane && !(flags & N2T_FMT_COMPRESSED))
        clump = bpp == 1 ? 0u : bpp == 2 ? 1u : bpp == 4 ? 2u : bpp == 8 ? 3u : 4u;
    f.mali_format = order | (code << 12) | ((flags & N2T_FMT_SRGB) ? (1u << 20) : 0u);
    f.bytes_per_block = bpp; f.block_width = bw; f.block_height = bh;
    f.clump_format = clump; f.astc_plane = plane;
    f.astc_block_width = plane ? aw : 0; f.astc_block_height = plane ? ah : 0;
    f.default_swizzle = swz;
    f.flags = flags | (alpha ? N2T_FMT_ALPHA : 0u);
    *out = f;
    return N2T_OK;
}

/* Resolve allocation + optional view format.  A view is accepted only when the
 * two formats use the same block geometry and component order, so a stride or
 * texel size from one format can never be fed to the other.  RGB<->BGR and
 * different bit widths are rejected, not remapped.  sRGB/UNORM and
 * UNORM/SNORM/UINT/SINT bit reinterpretations of the same block layout are
 * allowed; the caller must still have a compiled shader variant for the view.
 * Depth views/twins are deliberately not implied. */
static inline int n2t_view_format_check(uint32_t allocation, uint32_t view,
                                        struct n2t_format_info *allocation_out,
                                        struct n2t_format_info *view_out)
{
    struct n2t_format_info a, v;
    int rc;
    if (!allocation_out || !view_out) return N2T_E_INVALID;
    rc = n2t_format_query(allocation, &a);
    if (rc) return rc;
    if (!view || view == allocation) {
        *allocation_out = a; *view_out = a;
        return N2T_OK;
    }
    rc = n2t_format_query(view, &v);
    if (rc) return rc;
    if ((a.flags | v.flags) & N2T_FMT_DEPTH) return N2T_E_UNSUPPORTED;
    if (a.bytes_per_block != v.bytes_per_block ||
        a.block_width != v.block_width || a.block_height != v.block_height)
        return N2T_E_UNSUPPORTED;
    if ((a.mali_format & 0xfffu) != (v.mali_format & 0xfffu))
        return N2T_E_UNSUPPORTED;
    if ((a.flags & N2T_FMT_COMPRESSED) != (v.flags & N2T_FMT_COMPRESSED))
        return N2T_E_UNSUPPORTED;
    if (a.flags & N2T_FMT_COMPRESSED) {
        if (a.clump_format != v.clump_format || a.astc_plane != v.astc_plane)
            return N2T_E_UNSUPPORTED;
        if (a.astc_plane && v.astc_plane) {
            int ahdr = allocation >= 222u && allocation <= 236u;
            int vhdr = view >= 222u && view <= 236u;
            if (ahdr != vhdr) return N2T_E_UNSUPPORTED; /* LDR/HDR blocks differ */
        }
    }
    *allocation_out = a; *view_out = v;
    return N2T_OK;
}

static inline int n2t_kind_is_1d(uint32_t kind)
{
    return kind == N2T_KIND_1D || kind == N2T_KIND_1D_ARRAY;
}

static inline int n2t_kind_is_cube(uint32_t kind)
{
    return kind == N2T_KIND_CUBE || kind == N2T_KIND_CUBE_ARRAY;
}

static inline int n2t_kind_is_ms(uint32_t kind)
{
    return kind == N2T_KIND_2D_MS || kind == N2T_KIND_2D_MS_ARRAY;
}

static inline int n2t_kind_is_array(uint32_t kind)
{
    return kind == N2T_KIND_1D_ARRAY || kind == N2T_KIND_2D_ARRAY ||
           kind == N2T_KIND_2D_MS_ARRAY || kind == N2T_KIND_CUBE_ARRAY;
}

/* Validate the complete view and every subresource before any output write.
 * backing_address == 0 means offsets are checked relative to the backing;
 * otherwise addresses are also bounded by N2T_MAX_ADDRESS. */
static inline int n2t_layout_check(const struct n2t_texture_layout *l,
                                   uint64_t backing_address, uint64_t backing_length)
{
    struct n2t_format_info f, allocation;
    int rc;
    uint32_t i, j, layer_count;
    uint64_t max_end, min_offset, layer_stride = 0, total;
    if (!l || l->version != N2T_TEXTURE_RESOURCES_VERSION) return N2T_E_INVALID;
    rc = n2t_view_format_check(l->format, l->view_format, &allocation, &f);
    if (rc) return rc;
    (void)allocation;
    if (l->kind == N2T_KIND_TEXTURE_BUFFER) return N2T_E_UNSUPPORTED;
    if (l->kind > N2T_KIND_2D_MS_ARRAY) return N2T_E_KIND;
    if (f.flags & N2T_FMT_COMPRESSED) {
        if (l->kind != N2T_KIND_2D && l->kind != N2T_KIND_2D_ARRAY &&
            l->kind != N2T_KIND_CUBE && l->kind != N2T_KIND_CUBE_ARRAY)
            return N2T_E_UNSUPPORTED;
        if (l->samples != 1) return N2T_E_SAMPLES;
    }
    if (!l->width || !l->height || !l->depth) return N2T_E_DIMENSIONS;
    if (l->width > N2T_MAX_DIMENSION || l->height > N2T_MAX_DIMENSION) return N2T_E_DIMENSIONS;
    if (l->depth > N2T_MAX_DEPTH) return N2T_E_DIMENSIONS;
    if (n2t_kind_is_1d(l->kind)) {
        if (l->height != 1 || l->depth != 1) return N2T_E_DIMENSIONS;
    } else if (l->kind == N2T_KIND_3D) {
        if (l->depth < 1 || l->layers != 1 || l->first_layer) return N2T_E_LAYERS;
    } else {
        if (l->depth != 1) return N2T_E_DIMENSIONS;
    }
    if (l->levels < 1 || l->levels > N2T_MAX_LEVELS) return N2T_E_LEVELS;
    if (l->first_level > N2T_MAX_LEVELS - l->levels) return N2T_E_LEVELS;
    layer_count = l->layers;
    if (!layer_count || layer_count > N2T_MAX_LAYERS) return N2T_E_LAYERS;
    if (!n2t_kind_is_array(l->kind) && !n2t_kind_is_cube(l->kind) &&
        l->kind != N2T_KIND_3D && layer_count != 1)
        return N2T_E_LAYERS;
    if (l->first_layer && !l->array_length) return N2T_E_VIEW;
    if (l->first_layer && l->array_length &&
        l->first_layer >= l->array_length) return N2T_E_VIEW;
    if (n2t_kind_is_cube(l->kind)) {
        if (l->kind == N2T_KIND_CUBE && layer_count != 6u) return N2T_E_LAYERS;
        if ((layer_count % 6u) || (l->first_layer % 6u)) return N2T_E_LAYERS;
        if (l->width != l->height) return N2T_E_DIMENSIONS;
    }
    if (l->array_length && (l->first_layer >= l->array_length ||
        layer_count > l->array_length - l->first_layer)) return N2T_E_VIEW;
    if (n2t_kind_is_ms(l->kind)) {
        if (l->levels != 1) return N2T_E_LEVELS;
        if (l->samples != 2 && l->samples != 4 && l->samples != 8) return N2T_E_SAMPLES;
    } else if (l->samples != 1) return N2T_E_SAMPLES;
    if (l->samples > 8) return N2T_E_SAMPLES;
    if (l->access & ~(N2T_ACCESS_SAMPLE | N2T_ACCESS_STORAGE_READ | N2T_ACCESS_STORAGE_WRITE))
        return N2T_E_ACCESS;
    if (!l->access) return N2T_E_ACCESS;
    if (l->usage & ~15u) return N2T_E_ACCESS;
    if (l->storage_mode > 3) return N2T_E_ACCESS;
    if (l->layers > N2T_MAX_SUBRESOURCES / l->levels) return N2T_E_COUNT;
    if (l->swizzle_valid > 1) return N2T_E_INVALID;
    if (l->swizzle_valid) {
        if (l->swizzle_word & ~0xfffu) return N2T_E_INVALID;
        for (i = 0; i < 4; i++) if (((l->swizzle_word >> (3u * i)) & 7u) > N2T_CHANNEL_1)
            return N2T_E_INVALID;
    }
    min_offset = UINT64_MAX; max_end = 0;
    for (i = 0; i < l->levels; i++) {
        const struct n2t_level *lv = &l->level[i];
        uint64_t expect_w = n2t_minify(l->width, i);
        uint64_t expect_h = n2t_minify(l->height, i);
        uint64_t expect_d = n2t_minify(l->depth, i);
        uint32_t rows, row_bytes, blocks;
        uint64_t size, end;
        if (lv->width != expect_w || lv->height != expect_h || lv->depth != expect_d)
            return N2T_E_DIMENSIONS;
        blocks = n2t_ceil_div_u32(lv->width, f.block_width);
        if (!blocks) return N2T_E_DIMENSIONS;
        row_bytes = blocks * f.bytes_per_block;
        if (lv->row_stride < row_bytes || (lv->row_stride & 63u)) return N2T_E_STRIDE;
        rows = n2t_ceil_div_u32(lv->height, f.block_height);
        if (!rows) return N2T_E_DIMENSIONS;
        if ((uint64_t)lv->row_stride * rows > UINT32_MAX) return N2T_E_OVERFLOW;
        size = (uint64_t)lv->row_stride * rows;
        if (l->kind == N2T_KIND_3D || l->samples > 1) {
            if (!lv->slice_stride || (lv->slice_stride & 63u) || lv->slice_stride < size)
                return N2T_E_STRIDE;
            if (l->kind == N2T_KIND_3D) {
                if (n2t_add_u64(size, (uint64_t)(lv->depth - 1) * lv->slice_stride, &size))
                    return N2T_E_OVERFLOW;
            } else {
                if (n2t_add_u64(size, (uint64_t)(l->samples - 1) * lv->slice_stride, &size))
                    return N2T_E_OVERFLOW;
            }
        } else if (lv->slice_stride) {
            /* Pinned Mesa's v10 2D producer writes zero, while the already
             * qualified 16x16 RGBA8 packet writes the level size here.  Both
             * are preserved exactly; a nonzero value must still be a bounded,
             * aligned per-level slice/sample stride. */
            if ((lv->slice_stride & 63u) || lv->slice_stride < size) return N2T_E_STRIDE;
        }
        if (size > UINT32_MAX) return N2T_E_OVERFLOW;
        if (lv->offset & 63u) return N2T_E_STRIDE;
        if (n2t_add_u64(lv->offset, size, &end)) return N2T_E_OVERFLOW;
        if (lv->offset < min_offset) min_offset = lv->offset;
        if (end > max_end) max_end = end;
        for (j = 0; j < i; j++) {
            const struct n2t_level *o = &l->level[j];
            uint64_t osize, oend;
            uint32_t orows = n2t_ceil_div_u32(o->height, f.block_height);
            osize = (uint64_t)o->row_stride * orows;
            if (l->kind == N2T_KIND_3D)
                osize += (uint64_t)(o->depth - 1) * o->slice_stride;
            else if (l->samples > 1)
                osize += (uint64_t)(l->samples - 1) * o->slice_stride;
            if (n2t_add_u64(o->offset, osize, &oend)) return N2T_E_OVERFLOW;
            if (lv->offset < oend && o->offset < end) return N2T_E_SPAN;
        }
    }
    if (layer_count > 1) {
        layer_stride = l->level[0].layer_stride;
        if (!layer_stride) return N2T_E_STRIDE;
        for (i = 0; i < l->levels; i++)
            if (l->level[i].layer_stride != layer_stride) return N2T_E_STRIDE;
        if (max_end - min_offset > layer_stride) return N2T_E_STRIDE;
    } else {
        for (i = 0; i < l->levels; i++) if (l->level[i].layer_stride) return N2T_E_STRIDE;
    }
    if (layer_stride & 63u) return N2T_E_STRIDE;
    if (backing_length) {
        uint64_t last_layer = (uint64_t)(layer_count - 1) * layer_stride;
        uint64_t need;
        if (n2t_add_u64(last_layer, max_end, &need)) return N2T_E_OVERFLOW;
        if (need > backing_length) return N2T_E_SPAN;
        if (backing_address) {
            if (n2t_add_u64(backing_address, need, &total)) return N2T_E_OVERFLOW;
            if (total > N2T_MAX_ADDRESS) return N2T_E_ADDRESS;
        }
    }
    if (backing_address && (backing_address & 63u)) return N2T_E_ADDRESS;
    return N2T_OK;
}

static inline int n2t_layout_validate(const struct n2t_texture_layout *l,
                                      uint64_t backing_address, uint64_t backing_length)
{
    return n2t_layout_check(l, backing_address, backing_length);
}

static inline int n2t_layout_subresource(const struct n2t_texture_layout *l,
                                         uint64_t backing_address, uint32_t level,
                                         uint32_t layer, struct n2t_subresource *out)
{
    struct n2t_format_info f;
    uint32_t rows, row_bytes, blocks;
    uint64_t offset, size, address, span;
    int rc;
    if (!out) return N2T_E_INVALID;
    rc = n2t_layout_check(l, backing_address, 0);
    if (rc) return rc;
    if (level >= l->levels || layer >= l->layers) return N2T_E_VIEW;
    rc = n2t_format_query(l->format, &f);
    if (rc) return rc;
    blocks = n2t_ceil_div_u32(l->level[level].width, f.block_width);
    row_bytes = blocks * f.bytes_per_block;
    rows = n2t_ceil_div_u32(l->level[level].height, f.block_height);
    size = (uint64_t)l->level[level].row_stride * rows;
    if (l->kind == N2T_KIND_3D)
        size += (uint64_t)(l->level[level].depth - 1) * l->level[level].slice_stride;
    else if (l->samples > 1)
        size += (uint64_t)(l->samples - 1) * l->level[level].slice_stride;
    if (n2t_add_u64(l->level[level].offset,
                    (uint64_t)layer * l->level[level].layer_stride, &offset)) return N2T_E_OVERFLOW;
    if (n2t_add_u64(offset, size, &span)) return N2T_E_OVERFLOW;
    if (n2t_add_u64(backing_address, offset, &address)) return N2T_E_OVERFLOW;
    out->offset = offset; out->address = address; out->span = span;
    out->row_stride = l->level[level].row_stride; out->slice_stride = l->level[level].slice_stride;
    out->size = (uint32_t)size;
    out->width = l->level[level].width; out->height = l->level[level].height;
    out->depth = l->level[level].depth; out->rows = rows; out->row_bytes = row_bytes;
    return N2T_OK;
}

static inline uint32_t n2t_astc_dim(uint32_t pixels)
{
    switch (pixels) {
    case 4: return 0; case 5: return 1; case 6: return 2; case 8: return 4;
    case 10: return 6; case 12: return 7; default: return 15;
    }
}

static inline uint32_t n2t_log2_u32(uint32_t v)
{
    switch (v) {
    case 1: return 0; case 2: return 1; case 4: return 2; case 8: return 3;
    default: return 255;
    }
}

static inline uint32_t n2t_wrap_mode(uint32_t mode)
{
    switch (mode) {
    case N2T_ADDRESS_CLAMP_EDGE: return 9;
    case N2T_ADDRESS_MIRROR_CLAMP_EDGE: return 13;
    case N2T_ADDRESS_REPEAT: return 8;
    case N2T_ADDRESS_MIRROR_REPEAT: return 12;
    case N2T_ADDRESS_CLAMP_ZERO:
    case N2T_ADDRESS_CLAMP_BORDER: return 11;
    default: return 0xffffffffu;
    }
}

static inline uint32_t n2t_compare_func(uint32_t m)
{
    /* Metal/Vulkan compare is reference-vs-sample; Mali's compare operand
     * order is mirrored.  panvk pan_flip_compare_func: LESS/GREATER and
     * LEQUAL/GEQUAL swap.  Shadow-map pixel semantics remain unqualified. */
    switch (m) {
    case N2T_COMPARE_NEVER: return 0;
    case N2T_COMPARE_LESS: return 4;
    case N2T_COMPARE_EQUAL: return 2;
    case N2T_COMPARE_LEQUAL: return 6;
    case N2T_COMPARE_GREATER: return 1;
    case N2T_COMPARE_NOT_EQUAL: return 5;
    case N2T_COMPARE_GEQUAL: return 3;
    case N2T_COMPARE_ALWAYS: return 7;
    default: return 0xffffffffu;
    }
}

static inline uint32_t n2t_ufixed8(float value, uint32_t max)
{
    float scaled;
    if (value <= 0.0f) return 0;
    scaled = value * 256.0f;
    if (scaled >= (float)max) return max;
    return (uint32_t)(scaled + 0.5f);
}

static inline uint32_t n2t_sfixed8(float value)
{
    float scaled;
    int32_t rounded;
    if (value <= -128.0f) return 0x8000u;
    if (value >= 127.99609375f) return 0x7fffu;
    scaled = value * 256.0f;
    rounded = (int32_t)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
    return (uint32_t)rounded & 0xffffu;
}

static inline int n2t_sampler_validate(const struct n2t_sampler_state *s)
{
    uint32_t i;
    if (!s || s->version != N2T_TEXTURE_RESOURCES_VERSION) return N2T_E_INVALID;
    if (s->min_filter > 1 || s->mag_filter > 1 || s->mip_filter > 2) return N2T_E_SAMPLER_FIELD;
    if (s->address_s > 5 || s->address_t > 5 || s->address_r > 5) return N2T_E_SAMPLER_FIELD;
    if (s->normalized > 1 || s->clamp_integer_coords > 1 || s->clamp_integer_array > 1 ||
        s->srgb_override > 1 || s->seamless_cube > 1 || s->magnify_cutoff > 1)
        return N2T_E_SAMPLER_FIELD;
    if (s->compare_enabled > 1 || s->compare_function > 7) return N2T_E_SAMPLER_FIELD;
    if (s->max_anisotropy < 1 || s->max_anisotropy > 16) return N2T_E_SAMPLER_FIELD;
    if (s->reduction != N2T_REDUCTION_AVERAGE) return N2T_E_UNSUPPORTED;
    if (s->lod_algorithm != N2T_LOD_ISOTROPIC && s->lod_algorithm != N2T_LOD_ANISOTROPIC)
        return N2T_E_SAMPLER_FIELD;
    if (s->lod_algorithm == N2T_LOD_ANISOTROPIC && s->max_anisotropy < 2)
        return N2T_E_SAMPLER_COMBINATION;
    if (s->border_color_kind > N2T_BORDER_CUSTOM_RAW) return N2T_E_SAMPLER_FIELD;
    if (!n2t_finite_float(s->lod_min) || !n2t_finite_float(s->lod_max) ||
        !n2t_finite_float(s->lod_bias)) return N2T_E_SAMPLER_FIELD;
    if (s->lod_min < 0.0f || s->lod_max < s->lod_min) return N2T_E_SAMPLER_COMBINATION;
    if (s->border_color_kind == N2T_BORDER_CUSTOM_FLOAT) {
        for (i = 0; i < 4; i++) if (!n2t_finite_float(s->border[i])) return N2T_E_SAMPLER_FIELD;
    }
    return N2T_OK;
}

static inline int n2t_sampler_pack_for_format(const struct n2t_sampler_state *s,
                                               const struct n2t_format_info *f,
                                               uint8_t out[N2T_TEXTURE_SAMPLER_BYTES])
{
    uint32_t w0, w1, w2, border[4];
    uint32_t wr, wt, ws, mip, compare, i;
    int rc;
    if (!out) return N2T_E_INVALID;
    rc = n2t_sampler_validate(s);
    if (rc) return rc;
    wr = n2t_wrap_mode(s->address_r);
    wt = n2t_wrap_mode(s->address_t);
    ws = n2t_wrap_mode(s->address_s);
    /* compare_enabled is a per-use fact.  A regular (non-compare) sample must
     * not acquire a hardware compare function just because a Metal sampler
     * object carries a non-Never compareFunction for a different use.  Pinned
     * panvk panvk_translate_sampler_compare_func also emits NEVER when
     * compareEnable is false. */
    compare = s->compare_enabled ? n2t_compare_func(s->compare_function) : 0u;
    if (wr == 0xffffffffu || wt == 0xffffffffu || ws == 0xffffffffu ||
        compare == 0xffffffffu) return N2T_E_SAMPLER_FIELD;
    if (s->border_color_kind == N2T_BORDER_CUSTOM_RAW && !f) return N2T_E_UNSUPPORTED;
    if (f) {
        /* Compare sampling is a depth-texture operation on this contract. */
        if (s->compare_enabled && !(f->flags & N2T_FMT_DEPTH))
            return N2T_E_UNSUPPORTED;
        /* Integer textures require nearest magnification/minification and no
         * anisotropic filtering; do not silently ask the sampler to filter. */
        if ((f->flags & N2T_FMT_INTEGER) &&
            (s->min_filter != N2T_FILTER_NEAREST || s->mag_filter != N2T_FILTER_NEAREST ||
             s->mip_filter == N2T_MIP_LINEAR || s->max_anisotropy > 1))
            return N2T_E_UNSUPPORTED;
        if ((f->flags & N2T_FMT_DEPTH) &&
            (s->address_s >= N2T_ADDRESS_CLAMP_ZERO || s->address_t >= N2T_ADDRESS_CLAMP_ZERO ||
             s->address_r >= N2T_ADDRESS_CLAMP_ZERO))
            return N2T_E_UNSUPPORTED;
        if ((f->flags & N2T_FMT_INTEGER) &&
            (s->address_s == N2T_ADDRESS_CLAMP_ZERO || s->address_t == N2T_ADDRESS_CLAMP_ZERO ||
             s->address_r == N2T_ADDRESS_CLAMP_ZERO))
            return N2T_E_UNSUPPORTED;
        if ((f->flags & N2T_FMT_INTEGER) &&
            (s->address_s == N2T_ADDRESS_CLAMP_BORDER || s->address_t == N2T_ADDRESS_CLAMP_BORDER ||
             s->address_r == N2T_ADDRESS_CLAMP_BORDER) &&
            s->border_color_kind != N2T_BORDER_CUSTOM_RAW)
            return N2T_E_UNSUPPORTED;
    } else if (s->address_s == N2T_ADDRESS_CLAMP_ZERO ||
               s->address_t == N2T_ADDRESS_CLAMP_ZERO ||
               s->address_r == N2T_ADDRESS_CLAMP_ZERO) {
        return N2T_E_UNSUPPORTED; /* format-dependent ClampToZero */
    }
    border[0] = border[1] = border[2] = border[3] = 0;
    if (s->address_s == N2T_ADDRESS_CLAMP_ZERO || s->address_t == N2T_ADDRESS_CLAMP_ZERO ||
        s->address_r == N2T_ADDRESS_CLAMP_ZERO) {
        if (!f) return N2T_E_UNSUPPORTED;
        (void)n2t_float_bits((f->flags & N2T_FMT_ALPHA) ? 0.0f : 1.0f, &border[3]);
    } else if (s->border_color_kind || s->address_s == N2T_ADDRESS_CLAMP_BORDER ||
               s->address_t == N2T_ADDRESS_CLAMP_BORDER || s->address_r == N2T_ADDRESS_CLAMP_BORDER) {
        if (s->border_color_kind == N2T_BORDER_CUSTOM_RAW) {
            if (f && !(f->flags & N2T_FMT_INTEGER)) return N2T_E_UNSUPPORTED;
            border[0] = s->border_raw[0]; border[1] = s->border_raw[1];
            border[2] = s->border_raw[2]; border[3] = s->border_raw[3];
        } else if (s->border_color_kind == N2T_BORDER_TRANSPARENT_BLACK) {
            if (f && (f->flags & N2T_FMT_INTEGER)) return N2T_E_UNSUPPORTED;
            border[0] = border[1] = border[2] = border[3] = 0;
        } else if (s->border_color_kind == N2T_BORDER_OPAQUE_BLACK) {
            if (f && (f->flags & N2T_FMT_INTEGER)) return N2T_E_UNSUPPORTED;
            (void)n2t_float_bits(1.0f, &border[3]);
        } else if (s->border_color_kind == N2T_BORDER_OPAQUE_WHITE) {
            if (f && (f->flags & N2T_FMT_INTEGER)) return N2T_E_UNSUPPORTED;
            (void)n2t_float_bits(1.0f, &border[0]);
            (void)n2t_float_bits(1.0f, &border[1]);
            (void)n2t_float_bits(1.0f, &border[2]);
            (void)n2t_float_bits(1.0f, &border[3]);
        } else {
            if (f && (f->flags & (N2T_FMT_INTEGER | N2T_FMT_DEPTH))) return N2T_E_UNSUPPORTED;
            for (i = 0; i < 4; i++) {
                if (f && !(f->flags & N2T_FMT_FLOAT) && (s->border[i] < 0.0f || s->border[i] > 1.0f))
                    return N2T_E_SAMPLER_COMBINATION;
                (void)n2t_float_bits(s->border[i], &border[i]);
            }
        }
    }
    switch (s->mip_filter) {
    case N2T_MIP_NOT_MIPMAPPED: mip = 1; break;
    case N2T_MIP_NEAREST: mip = 0; break;
    default: mip = 3; break;
    }
    w0 = 1u |
        (wr << 8) | (wt << 12) | (ws << 16) |
        (1u << 21) | /* round to nearest even: existing qualified default */
        (s->srgb_override << 22) | (s->seamless_cube << 23) |
        (s->clamp_integer_coords << 24) | (s->normalized << 25) |
        (s->clamp_integer_array << 26) |
        ((s->min_filter == N2T_FILTER_NEAREST ? 1u : 0u) << 27) |
        ((s->mag_filter == N2T_FILTER_NEAREST ? 1u : 0u) << 28) |
        (s->magnify_cutoff << 29) | (mip << 30);
    w1 = n2t_ufixed8(s->lod_min, 8191u) |
         (compare << 13) | (n2t_ufixed8(s->lod_max, 8191u) << 16);
    w2 = n2t_sfixed8(s->lod_bias) |
         (((s->max_anisotropy - 1u) & 31u) << 16) |
         ((s->max_anisotropy > 1 ? N2T_LOD_ANISOTROPIC : s->lod_algorithm) << 24);
    out[0] = (uint8_t)w0; out[1] = (uint8_t)(w0 >> 8); out[2] = (uint8_t)(w0 >> 16); out[3] = (uint8_t)(w0 >> 24);
    out[4] = (uint8_t)w1; out[5] = (uint8_t)(w1 >> 8); out[6] = (uint8_t)(w1 >> 16); out[7] = (uint8_t)(w1 >> 24);
    out[8] = (uint8_t)w2; out[9] = (uint8_t)(w2 >> 8); out[10] = (uint8_t)(w2 >> 16); out[11] = (uint8_t)(w2 >> 24);
    out[12] = out[13] = out[14] = out[15] = 0;
    for (i = 0; i < 4; i++) {
        uint32_t v = border[i];
        out[16 + 4u * i] = (uint8_t)v; out[17 + 4u * i] = (uint8_t)(v >> 8);
        out[18 + 4u * i] = (uint8_t)(v >> 16); out[19 + 4u * i] = (uint8_t)(v >> 24);
    }
    return N2T_OK;
}

static inline int n2t_sampler_pack(const struct n2t_sampler_state *s,
                                   uint8_t out[N2T_TEXTURE_SAMPLER_BYTES])
{
    return n2t_sampler_pack_for_format(s, NULL, out);
}

/* Combined sampler/texture restrictions that neither object can see alone. */
static inline int n2t_sampler_layout_check(const struct n2t_sampler_state *s,
                                           const struct n2t_texture_layout *l)
{
    int rc = n2t_sampler_validate(s);
    struct n2t_format_info fi;
    if (rc) return rc;
    if (!l) return N2T_E_INVALID;
    if (s->compare_enabled) {
        rc = n2t_format_query(l->view_format ? l->view_format : l->format, &fi);
        if (rc) return rc;
        if (!(fi.flags & N2T_FMT_DEPTH)) return N2T_E_UNSUPPORTED;
    }
    if (!s->normalized) {
        if (l->kind != N2T_KIND_1D && l->kind != N2T_KIND_2D) return N2T_E_UNSUPPORTED;
        if (l->layers != 1 || l->levels != 1) return N2T_E_UNSUPPORTED;
        if (s->address_s != N2T_ADDRESS_CLAMP_EDGE ||
            s->address_t != N2T_ADDRESS_CLAMP_EDGE ||
            s->address_r != N2T_ADDRESS_CLAMP_EDGE) return N2T_E_UNSUPPORTED;
    }
    if (n2t_kind_is_cube(l->kind) && l->samples != 1) return N2T_E_SAMPLES;
    return N2T_OK;
}

static inline void n2t_store32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline void n2t_store64(uint8_t *p, uint64_t v)
{
    n2t_store32(p, (uint32_t)v); n2t_store32(p + 4, (uint32_t)(v >> 32));
}

static inline uint32_t n2t_swizzle_word(const struct n2t_texture_layout *l,
                                        const struct n2t_format_info *f)
{
    return l->swizzle_valid ? l->swizzle_word : f->default_swizzle;
}

static inline int n2t_pack_layout(const struct n2t_texture_layout *l,
                                  uint64_t backing_address, uint64_t backing_length,
                                  uint64_t descriptor_address, uint64_t payload_address,
                                  void *payload, size_t payload_capacity, int storage,
                                  struct n2t_packed_texture *result)
{
    struct n2t_format_info f, allocation;
    struct n2t_packed_texture local;
    uint32_t i, layer, level, planes, swz, interleave, lvl_field, maxlod, samples, array;
    uint32_t view_metal = 0;
    uint64_t payload_bytes, need;
    int rc;
    if (!l || !payload || !result) return N2T_E_INVALID;
    view_metal = l->view_format ? l->view_format : l->format;
    rc = n2t_layout_check(l, backing_address, backing_length);
    if (rc) return rc;
    rc = n2t_view_format_check(l->format, l->view_format, &allocation, &f);
    if (rc) return rc;
    (void)allocation;
    if (storage) {
        if (!(f.flags & N2T_FMT_STORAGE)) return N2T_E_UNSUPPORTED;
        if (f.flags & (N2T_FMT_DEPTH | N2T_FMT_COMPRESSED | N2T_FMT_SRGB)) return N2T_E_UNSUPPORTED;
        if (allocation.flags & N2T_FMT_SRGB) return N2T_E_UNSUPPORTED;
        if (l->samples != 1 || l->storage_mode == 3) return N2T_E_UNSUPPORTED;
        if (!(l->access & (N2T_ACCESS_STORAGE_READ | N2T_ACCESS_STORAGE_WRITE))) return N2T_E_ACCESS;
    } else {
        if (!(l->access & N2T_ACCESS_SAMPLE)) return N2T_E_ACCESS;
        if (l->storage_mode == 3) return N2T_E_UNSUPPORTED;
    }
    if (!backing_address || !backing_length) return N2T_E_ADDRESS;
    if (!descriptor_address || (descriptor_address & 31u) ||
        (descriptor_address > N2T_MAX_ADDRESS - N2T_TEXTURE_DESCRIPTOR_BYTES)) return N2T_E_ADDRESS;
    if (!payload_address || (payload_address & 31u) ||
        (payload_address > N2T_MAX_ADDRESS - 1)) return N2T_E_ADDRESS;
    planes = l->layers * l->levels;
    if (!planes || planes > N2T_MAX_SUBRESOURCES) return N2T_E_COUNT;
    if (n2t_mul_u64(planes, N2T_TEXTURE_PLANE_BYTES, &payload_bytes)) return N2T_E_OVERFLOW;
    if (payload_capacity < payload_bytes) return N2T_E_CAPACITY;
    if (n2t_add_u64(payload_address, payload_bytes, &need)) return N2T_E_OVERFLOW;
    if (need > N2T_MAX_ADDRESS) return N2T_E_ADDRESS;
    if (descriptor_address < payload_address + payload_bytes &&
        payload_address < descriptor_address + N2T_TEXTURE_DESCRIPTOR_BYTES) return N2T_E_OUTPUT;
    samples = l->samples;
    array = l->layers / (n2t_kind_is_cube(l->kind) ? 6u : 1u);
    swz = storage ? N2T_SWIZZLE_IDENTITY : n2t_swizzle_word(l, &f);
    interleave = (f.flags & N2T_FMT_COMPRESSED) ? 1u : 0u;
    lvl_field = l->levels - 1u;
    maxlod = storage ? 0u : (l->levels - 1u) << 8;
    local.version = N2T_TEXTURE_RESOURCES_VERSION;
    local.mali_format = f.mali_format;
    local.plane_count = planes;
    local.payload_bytes = (uint32_t)payload_bytes;
    local.levels = l->levels; local.array_size = array; local.sample_count = samples;
    local.descriptor_address = descriptor_address; local.payload_address = payload_address;
    /* type | dimension<<4 | format<<10 */
    local.descriptor_words[0] = 2u |
        ((n2t_kind_is_cube(l->kind) ? 0u : n2t_kind_is_1d(l->kind) ? 1u :
          (l->kind == N2T_KIND_3D ? 3u : 2u)) << 4) |
        ((f.mali_format & 0x3fffffu) << 10);
    local.descriptor_words[1] = (l->width - 1u) | ((l->height - 1u) << 16);
    local.descriptor_words[2] = swz | (interleave << 12) | (lvl_field << 16);
    local.descriptor_words[3] = 0 |
        ((l->kind == N2T_KIND_3D ? 0u : n2t_log2_u32(samples)) << 13) |
        (maxlod << 16);
    local.descriptor_words[4] = (uint32_t)payload_address;
    local.descriptor_words[5] = (uint32_t)(payload_address >> 32);
    local.descriptor_words[6] = array - 1u;
    local.descriptor_words[7] = l->kind == N2T_KIND_3D ? (l->depth - 1u) : 0u;
    for (i = 0; i < 8; i++) local.plane_words[i] = 0;
    /* Two passes: every refusal precedes the first payload store. */
    for (layer = 0; layer < l->layers; layer++) {
        for (level = 0; level < l->levels; level++) {
            struct n2t_subresource sr;
            rc = n2t_layout_subresource(l, backing_address, level, layer, &sr);
            if (rc) return rc;
            if (sr.size > UINT32_MAX) return N2T_E_OVERFLOW;
        }
    }
    for (layer = 0, i = 0; layer < l->layers; layer++) {
        for (level = 0; level < l->levels; level++, i++) {
            struct n2t_subresource sr;
            uint8_t *p = (uint8_t *)payload + (size_t)i * N2T_TEXTURE_PLANE_BYTES;
            uint32_t w0, w1;
            (void)n2t_layout_subresource(l, backing_address, level, layer, &sr);
            if (f.astc_plane) {
                w0 = 10u | (f.astc_plane << 4) | (2u << 8) |
                     (((f.flags & N2T_FMT_COMPRESSED) && (f.mali_format & (1u << 20)) == 0) ? (1u << 25) : 0u) |
                     (n2t_astc_dim(f.astc_block_width) << 26) |
                     (n2t_astc_dim(f.astc_block_height) << 29);
                if (f.flags & N2T_FMT_SRGB) w0 &= ~(1u << 25);
                /* ASTC HDR LDR distinction: HDR uses RGBA16F interchange and
                 * both decode bits; LDR uses wide RGBA16F without HDR. */
                if (view_metal >= 222u && view_metal <= 236u) {
                    w0 |= (1u << 24) | (1u << 25);
                }
            } else {
                w0 = 10u | (1u << 4) | (2u << 8) | (f.clump_format << 24);
            }
            w1 = sr.size;
            n2t_store32(p, w0); n2t_store32(p + 4, w1);
            n2t_store64(p + 8, sr.address);
            n2t_store32(p + 16, sr.row_stride);
            n2t_store32(p + 20, 0);
            n2t_store32(p + 24, sr.slice_stride);
            /* Word7 is v10-only extent.  Pinned arch9 v9.xml Generic/ASTC
             * planes end at word6; leave the reserved word zero. */
            n2t_store32(p + 28, 0);
            if (i == 0) {
                local.plane_words[0] = w0; local.plane_words[1] = w1;
                local.plane_words[2] = (uint32_t)sr.address; local.plane_words[3] = (uint32_t)(sr.address >> 32);
                local.plane_words[4] = sr.row_stride; local.plane_words[5] = 0;
                local.plane_words[6] = sr.slice_stride;
                local.plane_words[7] = 0;
            }
        }
    }
    *result = local;
    return N2T_OK;
}

static inline int n2t_layout_pack_sampled(const struct n2t_texture_layout *l,
                                          uint64_t backing_address, uint64_t backing_length,
                                          uint64_t descriptor_address, uint64_t payload_address,
                                          void *payload, size_t payload_capacity,
                                          struct n2t_packed_texture *result)
{
    return n2t_pack_layout(l, backing_address, backing_length, descriptor_address,
                           payload_address, payload, payload_capacity, 0, result);
}

static inline int n2t_layout_pack_storage(const struct n2t_texture_layout *l,
                                          uint64_t backing_address, uint64_t backing_length,
                                          uint64_t descriptor_address, uint64_t payload_address,
                                          void *payload, size_t payload_capacity,
                                          struct n2t_packed_texture *result)
{
    return n2t_pack_layout(l, backing_address, backing_length, descriptor_address,
                           payload_address, payload, payload_capacity, 1, result);
}

static inline void n2t_texture_copy_descriptor(const struct n2t_packed_texture *packed, void *out)
{
    uint8_t *p = (uint8_t *)out;
    uint32_t i;
    if (!packed || !out) return;
    for (i = 0; i < 8; i++) n2t_store32(p + 4u * i, packed->descriptor_words[i]);
}

/* Small, bounded diagnostic copy.  Never truncates silently: an overlong
 * string is cut at why_capacity-1 and remains NUL terminated. */
static inline void n2t_error_copy(int error, char *why, size_t why_capacity)
{
    const char *text = n2t_error_string(error);
    size_t i = 0;
    if (!why || !why_capacity) return;
    while (text[i] && i + 1u < why_capacity) { why[i] = text[i]; i++; }
    why[i] = '\0';
}

/* Compute-facing convenience wrappers around the same validation.  They do
 * not create a second packer; each forwards to n2t_layout_pack_* below. */
/* backing_length 0 means shape-only validation (no span bound); the public
 * packers reject a 0 backing length/address. */
static inline int n2t_texture_validate(const struct n2t_texture_layout *l,
                                       uint64_t backing_length, uint32_t access)
{
    int rc;
    if (!l) return N2T_E_INVALID;
    if (access && !(l->access & access)) return N2T_E_ACCESS;
    rc = n2t_layout_check(l, 0, backing_length);
    return rc;
}

static inline int n2t_texture_pack(const struct n2t_texture_layout *l,
                                   uint64_t backing_address, uint64_t backing_length,
                                   uint64_t descriptor_address, uint64_t payload_address,
                                   uint8_t out[N2T_TEXTURE_DESCRIPTOR_BYTES],
                                   char *why, size_t why_capacity)
{
    uint8_t scratch[N2T_MAX_SUBRESOURCES * N2T_TEXTURE_PLANE_BYTES];
    struct n2t_packed_texture packed;
    int rc;
    if (!out) return N2T_E_INVALID;
    rc = n2t_layout_pack_sampled(l, backing_address, backing_length,
                                  descriptor_address, payload_address,
                                  scratch, sizeof(scratch), &packed);
    if (rc) { n2t_error_copy(rc, why, why_capacity); return rc; }
    n2t_texture_copy_descriptor(&packed, out);
    if (why && why_capacity) why[0] = '\0';
    return N2T_OK;
}

static inline int n2t_storage_pack(const struct n2t_texture_layout *l,
                                   uint64_t backing_address, uint64_t backing_length,
                                   uint64_t descriptor_address, uint64_t payload_address,
                                   uint8_t out[N2T_TEXTURE_DESCRIPTOR_BYTES],
                                   char *why, size_t why_capacity)
{
    uint8_t scratch[N2T_MAX_SUBRESOURCES * N2T_TEXTURE_PLANE_BYTES];
    struct n2t_packed_texture packed;
    int rc;
    if (!out) return N2T_E_INVALID;
    rc = n2t_layout_pack_storage(l, backing_address, backing_length,
                                  descriptor_address, payload_address,
                                  scratch, sizeof(scratch), &packed);
    if (rc) { n2t_error_copy(rc, why, why_capacity); return rc; }
    n2t_texture_copy_descriptor(&packed, out);
    if (why && why_capacity) why[0] = '\0';
    return N2T_OK;
}

static inline int n2t_plane_pack(const struct n2t_texture_layout *l,
                                 uint64_t backing_address, uint32_t level,
                                 uint32_t layer, uint8_t out[N2T_TEXTURE_PLANE_BYTES],
                                 char *why, size_t why_capacity)
{
    struct n2t_format_info f, allocation;
    struct n2t_subresource sr;
    uint32_t w0, view_metal;
    int rc;
    if (!l || !out || !backing_address) return N2T_E_INVALID;
    view_metal = l->view_format ? l->view_format : l->format;
    rc = n2t_layout_check(l, backing_address, 0);
    if (rc) { n2t_error_copy(rc, why, why_capacity); return rc; }
    rc = n2t_view_format_check(l->format, l->view_format, &allocation, &f);
    (void)allocation;
    if (rc) { n2t_error_copy(rc, why, why_capacity); return rc; }
    rc = n2t_layout_subresource(l, backing_address, level, layer, &sr);
    if (rc) { n2t_error_copy(rc, why, why_capacity); return rc; }
    if (sr.size > UINT32_MAX) { rc = N2T_E_OVERFLOW; n2t_error_copy(rc, why, why_capacity); return rc; }
    if (f.astc_plane) {
        w0 = 10u | (f.astc_plane << 4) | (2u << 8) |
             (((f.flags & N2T_FMT_COMPRESSED) && (f.mali_format & (1u << 20)) == 0) ? (1u << 25) : 0u) |
             (n2t_astc_dim(f.astc_block_width) << 26) |
             (n2t_astc_dim(f.astc_block_height) << 29);
        if (f.flags & N2T_FMT_SRGB) w0 &= ~(1u << 25);
        if (view_metal >= 222u && view_metal <= 236u) w0 |= (1u << 24) | (1u << 25);
    } else {
        w0 = 10u | (1u << 4) | (2u << 8) | (f.clump_format << 24);
    }
    n2t_store32(out, w0);
    n2t_store32(out + 4, sr.size);
    n2t_store64(out + 8, sr.address);
    n2t_store32(out + 16, sr.row_stride);
    n2t_store32(out + 20, 0);
    n2t_store32(out + 24, sr.slice_stride);
    n2t_store32(out + 28, 0); /* v9 plane has no word7 */
    if (why && why_capacity) why[0] = '\0';
    return N2T_OK;
}

static inline int n2t_resource_entry(uint64_t address, uint64_t bytes,
                                     uint8_t out[16])
{
    if (!out || !address || (address & ((1ull << 56) - 1)) != address) return N2T_E_ADDRESS;
    n2t_store64(out, (address & ((1ull << 56) - 1)) | (1ull << 56));
    n2t_store64(out + 8, bytes);
    return N2T_OK;
}

#ifdef __cplusplus
}
#endif
#endif
