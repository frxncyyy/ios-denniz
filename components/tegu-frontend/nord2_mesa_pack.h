/* Generated from Mesa81daea66703eeea1a232446e3a36902353fc3045 v9.
 * Copyright2024 Collabora Ltd. SPDX-License-Identifier: MIT
 * Layout macros below are unchanged. See generate_mesa_pack_subset.py. */
#ifndef NORD2_MESA_PACK_H
#define NORD2_MESA_PACK_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define ALIGN_POT(v,a) (((v)+(a)-1)&~((a)-1))
#define IS_POT_NONZERO(x) ((x)&&!((x)&((x)-1)))
static inline unsigned util_logbase2(unsigned x){return 31u-__builtin_clz(x);}
static inline uint64_t util_bitpack_uint(uint64_t v,unsigned s,unsigned e){
 assert(e==63 || !(v>>(e-s+1)));return v<<s;}
static inline uint64_t util_bitpack_sint(int64_t v,unsigned s,unsigned e){
 unsigned n=e-s+1;return ((uint64_t)v & (n==64?~0ull:((1ull<<n)-1)))<<s;}
static inline uint32_t util_bitpack_float(float f){union{float f;uint32_t u;}v={.f=f};return v.u;}
enum mali_attribute_type {
        MALI_ATTRIBUTE_TYPE_1D               =      1,
        MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR   =      2,
        MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR  =      3,
        MALI_ATTRIBUTE_TYPE_1D_PRIMITIVE_INDEX_BUFFER =      5,
        MALI_ATTRIBUTE_TYPE_VERTEX_PACKET    =      6,
};

enum mali_channel {
        MALI_CHANNEL_R                       =      0,
        MALI_CHANNEL_G                       =      1,
        MALI_CHANNEL_B                       =      2,
        MALI_CHANNEL_A                       =      3,
        MALI_CHANNEL_0                       =      4,
        MALI_CHANNEL_1                       =      5,
};

enum mali_depth_source {
        MALI_DEPTH_SOURCE_MINIMUM            =      0,
        MALI_DEPTH_SOURCE_MAXIMUM            =      1,
        MALI_DEPTH_SOURCE_FIXED_FUNCTION     =      2,
        MALI_DEPTH_SOURCE_SHADER             =      3,
};

enum mali_depth_clamp_mode {
        MALI_DEPTH_CLAMP_MODE_0_1            =      1,
        MALI_DEPTH_CLAMP_MODE_BOUNDS         =      2,
};

enum mali_job_type {
        MALI_JOB_TYPE_NOT_STARTED            =      0,
        MALI_JOB_TYPE_NULL                   =      1,
        MALI_JOB_TYPE_WRITE_VALUE            =      2,
        MALI_JOB_TYPE_CACHE_FLUSH            =      3,
        MALI_JOB_TYPE_COMPUTE                =      4,
        MALI_JOB_TYPE_TILER                  =      7,
        MALI_JOB_TYPE_FRAGMENT               =      9,
        MALI_JOB_TYPE_INDEXED_VERTEX         =     10,
        MALI_JOB_TYPE_MALLOC_VERTEX          =     11,
};

enum mali_shader_stage {
        MALI_SHADER_STAGE_COMPUTE            =      1,
        MALI_SHADER_STAGE_FRAGMENT           =      2,
        MALI_SHADER_STAGE_VERTEX             =      3,
};

enum mali_descriptor_type {
        MALI_DESCRIPTOR_TYPE_NULL            =      0,
        MALI_DESCRIPTOR_TYPE_SAMPLER         =      1,
        MALI_DESCRIPTOR_TYPE_TEXTURE         =      2,
        MALI_DESCRIPTOR_TYPE_ATTRIBUTE       =      5,
        MALI_DESCRIPTOR_TYPE_DEPTH_STENCIL   =      7,
        MALI_DESCRIPTOR_TYPE_SHADER          =      8,
        MALI_DESCRIPTOR_TYPE_BUFFER          =      9,
        MALI_DESCRIPTOR_TYPE_PLANE           =     10,
};

enum mali_buffer_type {
        MALI_BUFFER_TYPE_SIMPLE              =      1,
        MALI_BUFFER_TYPE_TILER_HEAP          =      2,
        MALI_BUFFER_TYPE_STRUCTURE           =      3,
        MALI_BUFFER_TYPE_VERTEX_PACKET       =      4,
};

enum mali_draw_mode {
        MALI_DRAW_MODE_NONE                  =      0,
        MALI_DRAW_MODE_POINTS                =      1,
        MALI_DRAW_MODE_LINES                 =      2,
        MALI_DRAW_MODE_LINES_ADJACENCY       =      3,
        MALI_DRAW_MODE_LINE_STRIP            =      4,
        MALI_DRAW_MODE_LINE_STRIP_ADJACENCY  =      5,
        MALI_DRAW_MODE_LINE_LOOP             =      6,
        MALI_DRAW_MODE_TRIANGLES             =      8,
        MALI_DRAW_MODE_TRIANGLES_ADJACENCY   =      9,
        MALI_DRAW_MODE_TRIANGLE_STRIP        =     10,
        MALI_DRAW_MODE_TRIANGLE_STRIP_ADJACENCY =     11,
        MALI_DRAW_MODE_TRIANGLE_FAN          =     12,
        MALI_DRAW_MODE_POLYGON               =     13,
        MALI_DRAW_MODE_QUADS                 =     14,
};

enum mali_exception_access {
        MALI_EXCEPTION_ACCESS_NONE           =      0,
        MALI_EXCEPTION_ACCESS_EXECUTE        =      2,
        MALI_EXCEPTION_ACCESS_READ           =      1,
        MALI_EXCEPTION_ACCESS_WRITE          =      3,
};

enum mali_func {
        MALI_FUNC_NEVER                      =      0,
        MALI_FUNC_LESS                       =      1,
        MALI_FUNC_EQUAL                      =      2,
        MALI_FUNC_LEQUAL                     =      3,
        MALI_FUNC_GREATER                    =      4,
        MALI_FUNC_NOT_EQUAL                  =      5,
        MALI_FUNC_GEQUAL                     =      6,
        MALI_FUNC_ALWAYS                     =      7,
};

enum mali_address_mode {
        MALI_ADDRESS_MODE_FLAT               =      0,
        MALI_ADDRESS_MODE_PACKED             =      1,
        MALI_ADDRESS_MODE_OUT_OF_BOUNDS      =      8,
};

enum mali_format {
        MALI_ETC2_RGB8                       =      1,
        MALI_ETC2_R11_UNORM                  =      2,
        MALI_ETC2_RGBA8                      =      3,
        MALI_ETC2_RG11_UNORM                 =      4,
        MALI_BC1_UNORM                       =      7,
        MALI_BC2_UNORM                       =      8,
        MALI_BC3_UNORM                       =      9,
        MALI_BC4_UNORM                       =     10,
        MALI_BC4_SNORM                       =     11,
        MALI_BC5_UNORM                       =     12,
        MALI_BC5_SNORM                       =     13,
        MALI_BC6H_UF16                       =     14,
        MALI_BC6H_SF16                       =     15,
        MALI_BC7_UNORM                       =     16,
        MALI_ETC2_R11_SNORM                  =     17,
        MALI_ETC2_RG11_SNORM                 =     18,
        MALI_ETC2_RGB8A1                     =     19,
        MALI_ASTC_3D_LDR                     =     20,
        MALI_ASTC_3D_HDR                     =     21,
        MALI_ASTC_2D_LDR                     =     22,
        MALI_ASTC_2D_HDR                     =     23,
        MALI_ASTC_2D_NARROW                  =     62,
        MALI_ASTC_3D_NARROW                  =     63,
        MALI_YUV8                            =     32,
        MALI_YUVA8                           =     33,
        MALI_YUYV8                           =     34,
        MALI_VYUY8                           =     35,
        MALI_Y8_UV8_422                      =     36,
        MALI_Y8_U8_V8_422                    =     37,
        MALI_Y8_UV8_420                      =     38,
        MALI_Y8_U8_V8_420                    =     39,
        MALI_YUV10_A2                        =     40,
        MALI_A2_YUV10                        =     41,
        MALI_YUYAAYVYAA                      =     42,
        MALI_YUYV10                          =     44,
        MALI_VYUY10                          =     45,
        MALI_Y10_UV10_422                    =     46,
        MALI_Y10_UV10_420                    =     47,
        MALI_YUV16                           =     48,
        MALI_YUVA16                          =     49,
        MALI_YUYV16                          =     50,
        MALI_VYUY16                          =     51,
        MALI_Y10X6_U10X6V10X6_422            =     52,
        MALI_Y16_UV16_422                    =     53,
        MALI_Y16_UV16_420                    =     55,
        MALI_RGB565                          =     64,
        MALI_RGB5_A1_UNORM                   =     65,
        MALI_A1_BGR5_UNORM                   =     66,
        MALI_RGB10_A2_UNORM                  =     67,
        MALI_A2_BGR10_UNORM                  =     68,
        MALI_RGB10_A2_SNORM                  =     69,
        MALI_A2_BGR10_SNORM                  =     70,
        MALI_RGB10_A2UI                      =     71,
        MALI_A2_BGR10UI                      =     72,
        MALI_RGB10_A2I                       =     73,
        MALI_A2_BGR10I                       =     74,
        MALI_Z16_UNORM                       =     75,
        MALI_Z32                             =     76,
        MALI_Z24X8_UNORM                     =     77,
        MALI_X8Z24                           =     78,
        MALI_R32_FIXED                       =     81,
        MALI_RG32_FIXED                      =     82,
        MALI_RGB32_FIXED                     =     83,
        MALI_RGBA32_FIXED                    =     84,
        MALI_R11F_G11F_B10F                  =     89,
        MALI_B10F_G11F_R11F                  =     90,
        MALI_R9F_G9F_B9F_E5F                 =     91,
        MALI_E5F_B9F_G9F_R9F                 =     92,
        MALI_S8                              =     93,
        MALI_SNAP_4                          =     94,
        MALI_CONSTANT                        =     95,
        MALI_R8_SNORM                        =     99,
        MALI_R16_SNORM                       =    100,
        MALI_R32_SNORM                       =    101,
        MALI_RG4_SNORM                       =    106,
        MALI_RG8_SNORM                       =    107,
        MALI_RG16_SNORM                      =    108,
        MALI_RG32_SNORM                      =    109,
        MALI_RGB8_SNORM                      =    115,
        MALI_RGB16_SNORM                     =    116,
        MALI_RGB32_SNORM                     =    117,
        MALI_RGBA2_SNORM                     =    121,
        MALI_RGBA4_SNORM                     =    122,
        MALI_RGBA8_SNORM                     =    123,
        MALI_RGBA16_SNORM                    =    124,
        MALI_RGBA32_SNORM                    =    125,
        MALI_R8UI                            =    131,
        MALI_R16UI                           =    132,
        MALI_R32UI                           =    133,
        MALI_RG4UI                           =    138,
        MALI_RG8UI                           =    139,
        MALI_RG16UI                          =    140,
        MALI_RG32UI                          =    141,
        MALI_RGB8UI                          =    147,
        MALI_RGB16UI                         =    148,
        MALI_RGB32UI                         =    149,
        MALI_RGBA2UI                         =    153,
        MALI_RGBA4UI                         =    154,
        MALI_RGBA8UI                         =    155,
        MALI_RGBA16UI                        =    156,
        MALI_RGBA32UI                        =    157,
        MALI_R8_UNORM                        =    163,
        MALI_R16_UNORM                       =    164,
        MALI_R32_UNORM                       =    165,
        MALI_R32F                            =    167,
        MALI_RG4_UNORM                       =    170,
        MALI_RG8_UNORM                       =    171,
        MALI_RG16_UNORM                      =    172,
        MALI_RG32_UNORM                      =    173,
        MALI_RG32F                           =    175,
        MALI_RGB8_UNORM                      =    179,
        MALI_RGB16_UNORM                     =    180,
        MALI_RGB32_UNORM                     =    181,
        MALI_RGB32F                          =    183,
        MALI_RGBA2_UNORM                     =    185,
        MALI_RGBA4_UNORM                     =    186,
        MALI_RGBA8_UNORM                     =    187,
        MALI_RGBA16_UNORM                    =    188,
        MALI_RGBA32_UNORM                    =    189,
        MALI_RGBA32F                         =    191,
        MALI_R8I                             =    195,
        MALI_R16I                            =    196,
        MALI_R32I                            =    197,
        MALI_R16F                            =    199,
        MALI_RG4I                            =    202,
        MALI_RG8I                            =    203,
        MALI_RG16I                           =    204,
        MALI_RG32I                           =    205,
        MALI_RG16F                           =    207,
        MALI_RGB8I                           =    211,
        MALI_RGB16I                          =    212,
        MALI_RGB32I                          =    213,
        MALI_RGB16F                          =    215,
        MALI_RGBA2I                          =    217,
        MALI_RGBA4I                          =    218,
        MALI_RGBA8I                          =    219,
        MALI_RGBA16I                         =    220,
        MALI_RGBA32I                         =    221,
        MALI_RGBA16F                         =    223,
        MALI_RGB5_A1_AU                      =    224,
        MALI_RGB5_A1_PU                      =    225,
        MALI_R5G6B5_AU                       =    226,
        MALI_R5G6B5_PU                       =    227,
        MALI_SNAP4_V                         =    230,
        MALI_R32F_RTZ                        =    231,
        MALI_RGBA4_AU                        =    232,
        MALI_RGBA4_PU                        =    233,
        MALI_RGBA8_TB                        =    237,
        MALI_RGB10_A2_TB                     =    238,
        MALI_RG32F_RTZ                       =    239,
        MALI_TESS_VERTEX_PACK                =    240,
        MALI_RGB8_A2_AU                      =    241,
        MALI_RGB8_A2_PU                      =    242,
        MALI_RGB32F_RTZ                      =    247,
        MALI_RGBA32F_RTZ                     =    255,
};

enum mali_rgb_component_order {
        MALI_RGB_COMPONENT_ORDER_RGBA        =      0,
        MALI_RGB_COMPONENT_ORDER_GRBA        =      2,
        MALI_RGB_COMPONENT_ORDER_BGRA        =      4,
        MALI_RGB_COMPONENT_ORDER_ARGB        =      8,
        MALI_RGB_COMPONENT_ORDER_AGRB        =     10,
        MALI_RGB_COMPONENT_ORDER_ABGR        =     12,
        MALI_RGB_COMPONENT_ORDER_RGB1        =     16,
        MALI_RGB_COMPONENT_ORDER_GRB1        =     18,
        MALI_RGB_COMPONENT_ORDER_BGR1        =     20,
        MALI_RGB_COMPONENT_ORDER_1RGB        =     24,
        MALI_RGB_COMPONENT_ORDER_1GRB        =     26,
        MALI_RGB_COMPONENT_ORDER_1BGR        =     28,
        MALI_RGB_COMPONENT_ORDER_RRRR        =    226,
        MALI_RGB_COMPONENT_ORDER_RRR1        =    227,
        MALI_RGB_COMPONENT_ORDER_RRRA        =    228,
        MALI_RGB_COMPONENT_ORDER_000A        =    229,
        MALI_RGB_COMPONENT_ORDER_0001        =    230,
        MALI_RGB_COMPONENT_ORDER_0000        =    231,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V9    =    232,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V10   =    233,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V11   =    234,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V12   =    235,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V13   =    236,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V14   =    237,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V15   =    238,
        MALI_RGB_COMPONENT_ORDER_SNAP4_V16   =    239,
        MALI_RGB_COMPONENT_ORDER_R000        =    240,
        MALI_RGB_COMPONENT_ORDER_RBGA        =    242,
};

enum mali_yuv_swizzle {
        MALI_YUV_SWIZZLE_YUVA                =      0,
        MALI_YUV_SWIZZLE_YVUA                =      1,
        MALI_YUV_SWIZZLE_UYVA                =      2,
        MALI_YUV_SWIZZLE_UVYA                =      3,
        MALI_YUV_SWIZZLE_VUYA                =      4,
        MALI_YUV_SWIZZLE_VYUA                =      5,
        MALI_YUV_SWIZZLE_Y00A                =      6,
        MALI_YUV_SWIZZLE_YXXA                =      7,
};

enum mali_yuv_conversion_mode {
        MALI_YUV_CONVERSION_MODE_NO_CONVERSION =      0,
        MALI_YUV_CONVERSION_MODE_BT_601      =      3,
        MALI_YUV_CONVERSION_MODE_BT_709      =      4,
        MALI_YUV_CONVERSION_MODE_BT_2020     =      6,
};

enum mali_yuv_cr_siting {
        MALI_YUV_CR_SITING_CO_SITED          =      0,
        MALI_YUV_CR_SITING_CENTER_Y          =      1,
        MALI_YUV_CR_SITING_CENTER_X          =      2,
        MALI_YUV_CR_SITING_CENTER            =      3,
        MALI_YUV_CR_SITING_ONE_QUARTER       =      4,
        MALI_YUV_CR_SITING_THREE_QUARTERS    =      5,
        MALI_YUV_CR_SITING_REPLICATED        =      7,
};

enum mali_astc_2d_dimension {
        MALI_ASTC_2D_DIMENSION_4             =      0,
        MALI_ASTC_2D_DIMENSION_5             =      1,
        MALI_ASTC_2D_DIMENSION_6             =      2,
        MALI_ASTC_2D_DIMENSION_8             =      4,
        MALI_ASTC_2D_DIMENSION_10            =      6,
        MALI_ASTC_2D_DIMENSION_12            =      7,
};

enum mali_astc_3d_dimension {
        MALI_ASTC_3D_DIMENSION_4             =      0,
        MALI_ASTC_3D_DIMENSION_5             =      1,
        MALI_ASTC_3D_DIMENSION_6             =      2,
        MALI_ASTC_3D_DIMENSION_3             =      3,
};

enum mali_pixel_kill {
        MALI_PIXEL_KILL_FORCE_EARLY          =      0,
        MALI_PIXEL_KILL_WEAK_EARLY           =      2,
        MALI_PIXEL_KILL_FORCE_LATE           =      3,
};

enum mali_block_format {
        MALI_BLOCK_FORMAT_NO_WRITE           =      0,
        MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED =      1,
        MALI_BLOCK_FORMAT_LINEAR             =      2,
        MALI_BLOCK_FORMAT_AFBC               =     12,
        MALI_BLOCK_FORMAT_AFBC_TILED         =     13,
};

enum mali_mipmap_mode {
        MALI_MIPMAP_MODE_NEAREST             =      0,
        MALI_MIPMAP_MODE_NONE                =      1,
        MALI_MIPMAP_MODE_PERFORMANCE_TRILINEAR =      2,
        MALI_MIPMAP_MODE_TRILINEAR           =      3,
};

enum mali_lod_algorithm {
        MALI_LOD_ALGORITHM_ISOTROPIC         =      0,
        MALI_LOD_ALGORITHM_ANISOTROPIC       =      3,
};

enum mali_msaa {
        MALI_MSAA_SINGLE                     =      0,
        MALI_MSAA_AVERAGE                    =      1,
        MALI_MSAA_MULTIPLE                   =      2,
        MALI_MSAA_LAYERED                    =      3,
};

enum mali_index_type {
        MALI_INDEX_TYPE_NONE                 =      0,
        MALI_INDEX_TYPE_UINT8                =      1,
        MALI_INDEX_TYPE_UINT16               =      2,
        MALI_INDEX_TYPE_UINT32               =      3,
};

enum mali_occlusion_mode {
        MALI_OCCLUSION_MODE_DISABLED         =      0,
        MALI_OCCLUSION_MODE_PREDICATE        =      1,
        MALI_OCCLUSION_MODE_COUNTER          =      3,
};

enum mali_stencil_op {
        MALI_STENCIL_OP_KEEP                 =      0,
        MALI_STENCIL_OP_REPLACE              =      1,
        MALI_STENCIL_OP_ZERO                 =      2,
        MALI_STENCIL_OP_INVERT               =      3,
        MALI_STENCIL_OP_INCR_WRAP            =      4,
        MALI_STENCIL_OP_DECR_WRAP            =      5,
        MALI_STENCIL_OP_INCR_SAT             =      6,
        MALI_STENCIL_OP_DECR_SAT             =      7,
};

enum mali_texture_dimension {
        MALI_TEXTURE_DIMENSION_CUBE          =      0,
        MALI_TEXTURE_DIMENSION_1D            =      1,
        MALI_TEXTURE_DIMENSION_2D            =      2,
        MALI_TEXTURE_DIMENSION_3D            =      3,
        MALI_TEXTURE_DIMENSION_2D_IMAGE_CROP =      4,
};

enum mali_wrap_mode {
        MALI_WRAP_MODE_REPEAT                =      8,
        MALI_WRAP_MODE_CLAMP_TO_EDGE         =      9,
        MALI_WRAP_MODE_CLAMP_TO_BORDER       =     11,
        MALI_WRAP_MODE_MIRRORED_REPEAT       =     12,
        MALI_WRAP_MODE_MIRRORED_CLAMP_TO_EDGE =     13,
        MALI_WRAP_MODE_MIRRORED_CLAMP_TO_BORDER =     15,
};

enum mali_tile_render_order {
        MALI_TILE_RENDER_ORDER_Z_ORDER       =      0,
        MALI_TILE_RENDER_ORDER_HORIZONTAL    =      1,
        MALI_TILE_RENDER_ORDER_VERTICAL      =      2,
        MALI_TILE_RENDER_ORDER_REVERSE_HORIZONTAL =      5,
        MALI_TILE_RENDER_ORDER_REVERSE_VERTICAL =      6,
};

enum mali_task_axis {
        MALI_TASK_AXIS_X                     =      0,
        MALI_TASK_AXIS_Y                     =      1,
        MALI_TASK_AXIS_Z                     =      2,
};

enum mali_attribute_frequency {
        MALI_ATTRIBUTE_FREQUENCY_VERTEX      =      0,
        MALI_ATTRIBUTE_FREQUENCY_INSTANCE    =      1,
};

struct MALI_DESCRIPTOR_HEADER {
   enum mali_descriptor_type            type;
   uint32_t                             ignore;
};

#define MALI_DESCRIPTOR_HEADER_header           \
   .type = MALI_DESCRIPTOR_TYPE_NULL

#define MALI_DESCRIPTOR_HEADER_LENGTH 4

#define MALI_DESCRIPTOR_HEADER_ALIGN 32

struct mali_descriptor_header_packed { uint32_t opaque[1]; };

#define MALI_DESCRIPTOR_HEADER_PACKED_T struct mali_descriptor_header_packed

#define MALI_DESCRIPTOR_HEADER_pack(__packed, __unpacked) \
do { \
   MALI_DESCRIPTOR_HEADER_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->ignore, 4, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_NULL_DESCRIPTOR {
   enum mali_descriptor_type            type;
};

#define MALI_NULL_DESCRIPTOR_header             \
   .type = MALI_DESCRIPTOR_TYPE_NULL

#define MALI_NULL_DESCRIPTOR_LENGTH 32

#define MALI_NULL_DESCRIPTOR_ALIGN 32

struct mali_null_descriptor_packed { uint32_t opaque[8]; };

#define MALI_NULL_DESCRIPTOR_PACKED_T struct mali_null_descriptor_packed

#define MALI_NULL_DESCRIPTOR_pack(__packed, __unpacked) \
do { \
   MALI_NULL_DESCRIPTOR_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3); \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_ATTRIBUTE {
   enum mali_descriptor_type            type;
   enum mali_attribute_type             attribute_type;
   bool                                 offset_enable;
   uint32_t                             format;
   uint32_t                             table;
   enum mali_attribute_frequency        frequency;
   uint32_t                             divisor_r;
   uint32_t                             divisor_e;
   int64_t                              offset;
   uint64_t                             buffer_index;
   uint64_t                             stride;
   uint32_t                             attribute_stride;
   uint32_t                             packet_stride;
   uint64_t                             divisor_d;
};

#define MALI_ATTRIBUTE_header                   \
   .type = MALI_DESCRIPTOR_TYPE_ATTRIBUTE

#define MALI_ATTRIBUTE_LENGTH 32

#define MALI_ATTRIBUTE_ALIGN 32

struct mali_attribute_packed { uint32_t opaque[8]; };

#define MALI_ATTRIBUTE_PACKED_T struct mali_attribute_packed

#define MALI_ATTRIBUTE_pack(__packed, __unpacked) \
do { \
   MALI_ATTRIBUTE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->attribute_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->offset_enable, 8, 8) | \
            util_bitpack_uint((__unpacked)->format, 10, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->table, 0, 5) | \
            util_bitpack_uint((__unpacked)->frequency, 6, 7) | \
            util_bitpack_uint((__unpacked)->divisor_r, 24, 28) | \
            util_bitpack_uint((__unpacked)->divisor_e, 29, 29); \
   __tmp_packed.opaque[ 2] = util_bitpack_sint((__unpacked)->offset, 0, 31); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->buffer_index, 0, 31); \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->attribute_stride, 0, 15) | \
            util_bitpack_uint((__unpacked)->packet_stride, 16, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->divisor_d, 0, 31); \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_blend_operand_a {
        MALI_BLEND_OPERAND_A_ZERO            =      1,
        MALI_BLEND_OPERAND_A_SRC             =      2,
        MALI_BLEND_OPERAND_A_DEST            =      3,
};

enum mali_blend_operand_b {
        MALI_BLEND_OPERAND_B_SRC_MINUS_DEST  =      0,
        MALI_BLEND_OPERAND_B_SRC_PLUS_DEST   =      1,
        MALI_BLEND_OPERAND_B_SRC             =      2,
        MALI_BLEND_OPERAND_B_DEST            =      3,
};

enum mali_blend_operand_c {
        MALI_BLEND_OPERAND_C_ZERO            =      1,
        MALI_BLEND_OPERAND_C_SRC             =      2,
        MALI_BLEND_OPERAND_C_DEST            =      3,
        MALI_BLEND_OPERAND_C_SRC_X_2         =      4,
        MALI_BLEND_OPERAND_C_SRC_ALPHA       =      5,
        MALI_BLEND_OPERAND_C_DEST_ALPHA      =      6,
        MALI_BLEND_OPERAND_C_CONSTANT        =      7,
};

struct MALI_BLEND_FUNCTION {
   enum mali_blend_operand_a            a;
   bool                                 negate_a;
   enum mali_blend_operand_b            b;
   bool                                 negate_b;
   enum mali_blend_operand_c            c;
   bool                                 invert_c;
};

#define MALI_BLEND_FUNCTION_header              \
   0

struct MALI_BLEND_EQUATION {
   struct MALI_BLEND_FUNCTION           rgb;
   struct MALI_BLEND_FUNCTION           alpha;
   uint32_t                             color_mask;
};

#define MALI_BLEND_EQUATION_header              \
   .rgb = { MALI_BLEND_FUNCTION_header },  \
   .alpha = { MALI_BLEND_FUNCTION_header }

#define MALI_BLEND_EQUATION_LENGTH 4

struct mali_blend_equation_packed { uint32_t opaque[1]; };

#define MALI_BLEND_EQUATION_PACKED_T struct mali_blend_equation_packed

#define MALI_BLEND_EQUATION_pack(__packed, __unpacked) \
do { \
   MALI_BLEND_EQUATION_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->rgb.a, 0, 1) | \
            util_bitpack_uint((__unpacked)->rgb.negate_a, 3, 3) | \
            util_bitpack_uint((__unpacked)->rgb.b, 4, 5) | \
            util_bitpack_uint((__unpacked)->rgb.negate_b, 7, 7) | \
            util_bitpack_uint((__unpacked)->rgb.c, 8, 10) | \
            util_bitpack_uint((__unpacked)->rgb.invert_c, 11, 11) | \
            util_bitpack_uint((__unpacked)->alpha.a, 12, 13) | \
            util_bitpack_uint((__unpacked)->alpha.negate_a, 15, 15) | \
            util_bitpack_uint((__unpacked)->alpha.b, 16, 17) | \
            util_bitpack_uint((__unpacked)->alpha.negate_b, 19, 19) | \
            util_bitpack_uint((__unpacked)->alpha.c, 20, 22) | \
            util_bitpack_uint((__unpacked)->alpha.invert_c, 23, 23) | \
            util_bitpack_uint((__unpacked)->color_mask, 28, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_register_file_format {
        MALI_REGISTER_FILE_FORMAT_F16        =      0,
        MALI_REGISTER_FILE_FORMAT_F32        =      1,
        MALI_REGISTER_FILE_FORMAT_I32        =      2,
        MALI_REGISTER_FILE_FORMAT_U32        =      3,
        MALI_REGISTER_FILE_FORMAT_I16        =      4,
        MALI_REGISTER_FILE_FORMAT_U16        =      5,
};

enum mali_blend_mode {
        MALI_BLEND_MODE_SHADER               =      0,
        MALI_BLEND_MODE_OPAQUE               =      1,
        MALI_BLEND_MODE_FIXED_FUNCTION       =      2,
        MALI_BLEND_MODE_OFF                  =      3,
};

struct MALI_BLEND_SHADER {
   uint32_t                             pc;
};

#define MALI_BLEND_SHADER_header                \
   0

#define MALI_BLEND_SHADER_LENGTH 8

struct mali_blend_shader_packed { uint32_t opaque[2]; };

#define MALI_BLEND_SHADER_PACKED_T struct mali_blend_shader_packed

#define MALI_BLEND_SHADER_pack(__packed, __unpacked) \
do { \
   MALI_BLEND_SHADER_PACKED_T __tmp_packed; \
   assert(((__unpacked)->pc & 0xf) == 0); \
   __tmp_packed.opaque[ 0] = 0; \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->pc >> 4, 4, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_INTERNAL_CONVERSION {
   uint32_t                             memory_format;
   bool                                 raw;
};

#define MALI_INTERNAL_CONVERSION_header         \
   0

#define MALI_INTERNAL_CONVERSION_LENGTH 4

#define MALI_INTERNAL_CONVERSION_ALIGN 4

struct mali_internal_conversion_packed { uint32_t opaque[1]; };

#define MALI_INTERNAL_CONVERSION_PACKED_T struct mali_internal_conversion_packed

#define MALI_INTERNAL_CONVERSION_pack(__packed, __unpacked) \
do { \
   MALI_INTERNAL_CONVERSION_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->memory_format, 0, 21) | \
            util_bitpack_uint((__unpacked)->raw, 22, 22); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_BLEND_FIXED_FUNCTION {
   uint32_t                             num_comps;
   uint32_t                             rt;
#define MALI_BIFROST_BLEND_MAX_RT                8
   struct MALI_INTERNAL_CONVERSION      conversion;
};

#define MALI_BIFROST_BLEND_MAX_RT                8

#define MALI_BLEND_FIXED_FUNCTION_header        \
   .num_comps = 1,  \
   .conversion = { MALI_INTERNAL_CONVERSION_header }

#define MALI_BLEND_FIXED_FUNCTION_LENGTH 8

struct mali_blend_fixed_function_packed { uint32_t opaque[2]; };

#define MALI_BLEND_FIXED_FUNCTION_PACKED_T struct mali_blend_fixed_function_packed

#define MALI_BLEND_FIXED_FUNCTION_pack(__packed, __unpacked) \
do { \
   MALI_BLEND_FIXED_FUNCTION_PACKED_T __tmp_packed; \
   assert((__unpacked)->num_comps >= 1); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->num_comps - 1, 3, 4) | \
            util_bitpack_uint((__unpacked)->rt, 16, 19); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->conversion.memory_format, 0, 21) | \
            util_bitpack_uint((__unpacked)->conversion.raw, 22, 22); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_INTERNAL_BLEND {
   enum mali_blend_mode                 mode;
   struct MALI_BLEND_SHADER             shader;
   struct MALI_BLEND_FIXED_FUNCTION     fixed_function;
};

#define MALI_INTERNAL_BLEND_header              \
   .shader = { MALI_BLEND_SHADER_header },  \
   .fixed_function = { MALI_BLEND_FIXED_FUNCTION_header }

#define MALI_INTERNAL_BLEND_LENGTH 8

#define MALI_INTERNAL_BLEND_ALIGN 8

struct mali_internal_blend_packed { uint32_t opaque[2]; };

#define MALI_INTERNAL_BLEND_PACKED_T struct mali_internal_blend_packed

#define MALI_INTERNAL_BLEND_pack(__packed, __unpacked) \
do { \
   MALI_INTERNAL_BLEND_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->mode, 0, 1) | \
            util_bitpack_uint((__unpacked)->fixed_function.num_comps - 1, 3, 4) | \
            util_bitpack_uint((__unpacked)->fixed_function.rt, 16, 19); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->shader.pc >> 4, 4, 31) | \
            util_bitpack_uint((__unpacked)->fixed_function.conversion.memory_format, 0, 21) | \
            util_bitpack_uint((__unpacked)->fixed_function.conversion.raw, 22, 22); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_BLEND {
   bool                                 load_destination;
   bool                                 alpha_to_one;
   bool                                 enable;
   bool                                 srgb;
   bool                                 round_to_fb_precision;
   uint32_t                             blend_constant;
   struct MALI_BLEND_EQUATION           equation;
   struct MALI_INTERNAL_BLEND           internal;
};

#define MALI_BLEND_header                       \
   .load_destination = false,  \
   .enable = true,  \
   .srgb = false,  \
   .round_to_fb_precision = false,  \
   .equation = { MALI_BLEND_EQUATION_header },  \
   .internal = { MALI_INTERNAL_BLEND_header }

#define MALI_BLEND_LENGTH 16

#define MALI_BLEND_ALIGN 16

struct mali_blend_packed { uint32_t opaque[4]; };

#define MALI_BLEND_PACKED_T struct mali_blend_packed

#define MALI_BLEND_pack(__packed, __unpacked) \
do { \
   MALI_BLEND_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->load_destination, 0, 0) | \
            util_bitpack_uint((__unpacked)->alpha_to_one, 8, 8) | \
            util_bitpack_uint((__unpacked)->enable, 9, 9) | \
            util_bitpack_uint((__unpacked)->srgb, 10, 10) | \
            util_bitpack_uint((__unpacked)->round_to_fb_precision, 11, 11) | \
            util_bitpack_uint((__unpacked)->blend_constant, 16, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->equation.rgb.a, 0, 1) | \
            util_bitpack_uint((__unpacked)->equation.rgb.negate_a, 3, 3) | \
            util_bitpack_uint((__unpacked)->equation.rgb.b, 4, 5) | \
            util_bitpack_uint((__unpacked)->equation.rgb.negate_b, 7, 7) | \
            util_bitpack_uint((__unpacked)->equation.rgb.c, 8, 10) | \
            util_bitpack_uint((__unpacked)->equation.rgb.invert_c, 11, 11) | \
            util_bitpack_uint((__unpacked)->equation.alpha.a, 12, 13) | \
            util_bitpack_uint((__unpacked)->equation.alpha.negate_a, 15, 15) | \
            util_bitpack_uint((__unpacked)->equation.alpha.b, 16, 17) | \
            util_bitpack_uint((__unpacked)->equation.alpha.negate_b, 19, 19) | \
            util_bitpack_uint((__unpacked)->equation.alpha.c, 20, 22) | \
            util_bitpack_uint((__unpacked)->equation.alpha.invert_c, 23, 23) | \
            util_bitpack_uint((__unpacked)->equation.color_mask, 28, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->internal.mode, 0, 1) | \
            util_bitpack_uint((__unpacked)->internal.fixed_function.num_comps - 1, 3, 4) | \
            util_bitpack_uint((__unpacked)->internal.fixed_function.rt, 16, 19); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->internal.shader.pc >> 4, 4, 31) | \
            util_bitpack_uint((__unpacked)->internal.fixed_function.conversion.memory_format, 0, 21) | \
            util_bitpack_uint((__unpacked)->internal.fixed_function.conversion.raw, 22, 22); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_BUFFER {
   enum mali_descriptor_type            type;
   enum mali_buffer_type                buffer_type;
   uint64_t                             size;
   uint64_t                             address;
   uint64_t                             stride;
   uint32_t                             packet_stride;
   uint32_t                             attribute_stride;
   struct MALI_INTERNAL_CONVERSION      conversion;
};

#define MALI_BUFFER_header                      \
   .type = MALI_DESCRIPTOR_TYPE_BUFFER,  \
   .buffer_type = MALI_BUFFER_TYPE_SIMPLE,  \
   .conversion = { MALI_INTERNAL_CONVERSION_header }

#define MALI_BUFFER_LENGTH 32

#define MALI_BUFFER_ALIGN 32

struct mali_buffer_packed { uint32_t opaque[8]; };

#define MALI_BUFFER_PACKED_T struct mali_buffer_packed

#define MALI_BUFFER_pack(__packed, __unpacked) \
do { \
   MALI_BUFFER_PACKED_T __tmp_packed; \
   assert(((__unpacked)->packet_stride & 0x3f) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->buffer_type, 4, 7); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->size, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->address, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->address, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->packet_stride >> 6, 0, 15) | \
            util_bitpack_uint((__unpacked)->attribute_stride, 16, 31); \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->conversion.memory_format, 0, 21) | \
            util_bitpack_uint((__unpacked)->conversion.raw, 22, 22); \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_point_size_array_format {
        MALI_POINT_SIZE_ARRAY_FORMAT_NONE    =      0,
        MALI_POINT_SIZE_ARRAY_FORMAT_FP16    =      2,
        MALI_POINT_SIZE_ARRAY_FORMAT_FP32    =      3,
};

struct MALI_PRIMITIVE {
   enum mali_draw_mode                  draw_mode;
   enum mali_index_type                 index_type;
   enum mali_point_size_array_format    point_size_array_format;
   bool                                 primitive_index_enable;
   bool                                 primitive_index_override;
   bool                                 allow_rotating_primitives;
   bool                                 low_depth_cull;
   bool                                 high_depth_cull;
   bool                                 secondary_shader;
   bool                                 primitive_restart;
   bool                                 layer_index_enable;
   bool                                 scissor_array_enable;
   int64_t                              base_vertex_offset;
   uint64_t                             instance_offset;
   uint64_t                             index_count;
};

#define MALI_PRIMITIVE_header                   \
   .draw_mode = MALI_DRAW_MODE_NONE,  \
   .index_type = MALI_INDEX_TYPE_NONE,  \
   .allow_rotating_primitives = true,  \
   .low_depth_cull = true,  \
   .high_depth_cull = true

#define MALI_PRIMITIVE_LENGTH 16

struct mali_primitive_packed { uint32_t opaque[4]; };

#define MALI_PRIMITIVE_PACKED_T struct mali_primitive_packed

#define MALI_PRIMITIVE_pack(__packed, __unpacked) \
do { \
   MALI_PRIMITIVE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->draw_mode, 0, 7) | \
            util_bitpack_uint((__unpacked)->index_type, 8, 10) | \
            util_bitpack_uint((__unpacked)->point_size_array_format, 11, 12) | \
            util_bitpack_uint((__unpacked)->primitive_index_enable, 13, 13) | \
            util_bitpack_uint((__unpacked)->primitive_index_override, 14, 14) | \
            util_bitpack_uint((__unpacked)->allow_rotating_primitives, 15, 15) | \
            util_bitpack_uint((__unpacked)->low_depth_cull, 16, 16) | \
            util_bitpack_uint((__unpacked)->high_depth_cull, 17, 17) | \
            util_bitpack_uint((__unpacked)->secondary_shader, 18, 18) | \
            util_bitpack_uint((__unpacked)->primitive_restart, 19, 19) | \
            util_bitpack_uint((__unpacked)->layer_index_enable, 20, 20) | \
            util_bitpack_uint((__unpacked)->scissor_array_enable, 21, 21); \
   __tmp_packed.opaque[ 1] = util_bitpack_sint((__unpacked)->base_vertex_offset, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->instance_offset, 0, 31); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->index_count, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_SAMPLER {
   enum mali_descriptor_type            type;
   enum mali_wrap_mode                  wrap_mode_r;
   enum mali_wrap_mode                  wrap_mode_t;
   enum mali_wrap_mode                  wrap_mode_s;
   bool                                 round_to_nearest_even;
   bool                                 srgb_override;
   bool                                 seamless_cube_map;
   bool                                 clamp_integer_coordinates;
   bool                                 normalized_coordinates;
   bool                                 clamp_integer_array_indices;
   bool                                 minify_nearest;
   bool                                 magnify_nearest;
   bool                                 magnify_cutoff;
   enum mali_mipmap_mode                mipmap_mode;
   float                                minimum_lod;
   enum mali_func                       compare_function;
   float                                maximum_lod;
   float                                lod_bias;
   uint32_t                             maximum_anisotropy;
   enum mali_lod_algorithm              lod_algorithm;
   uint32_t                             border_color_r;
   uint32_t                             border_color_g;
   uint32_t                             border_color_b;
   uint32_t                             border_color_a;
};

#define MALI_SAMPLER_header                     \
   .type = MALI_DESCRIPTOR_TYPE_SAMPLER,  \
   .wrap_mode_r = MALI_WRAP_MODE_CLAMP_TO_EDGE,  \
   .wrap_mode_t = MALI_WRAP_MODE_CLAMP_TO_EDGE,  \
   .wrap_mode_s = MALI_WRAP_MODE_CLAMP_TO_EDGE,  \
   .round_to_nearest_even = true,  \
   .srgb_override = false,  \
   .seamless_cube_map = true,  \
   .normalized_coordinates = true,  \
   .clamp_integer_array_indices = true,  \
   .minify_nearest = false,  \
   .magnify_nearest = false,  \
   .magnify_cutoff = false,  \
   .mipmap_mode = MALI_MIPMAP_MODE_NEAREST,  \
   .minimum_lod = 0,  \
   .compare_function = MALI_FUNC_NEVER,  \
   .maximum_lod = 0,  \
   .lod_bias = 0,  \
   .maximum_anisotropy = 1,  \
   .lod_algorithm = MALI_LOD_ALGORITHM_ISOTROPIC,  \
   .border_color_r = 0.0,  \
   .border_color_g = 0.0,  \
   .border_color_b = 0.0,  \
   .border_color_a = 0.0

#define MALI_SAMPLER_LENGTH 32

#define MALI_SAMPLER_ALIGN 32

struct mali_sampler_packed { uint32_t opaque[8]; };

#define MALI_SAMPLER_PACKED_T struct mali_sampler_packed

#define MALI_SAMPLER_pack(__packed, __unpacked) \
do { \
   MALI_SAMPLER_PACKED_T __tmp_packed; \
   assert((__unpacked)->maximum_anisotropy >= 1); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->wrap_mode_r, 8, 11) | \
            util_bitpack_uint((__unpacked)->wrap_mode_t, 12, 15) | \
            util_bitpack_uint((__unpacked)->wrap_mode_s, 16, 19) | \
            util_bitpack_uint((__unpacked)->round_to_nearest_even, 21, 21) | \
            util_bitpack_uint((__unpacked)->srgb_override, 22, 22) | \
            util_bitpack_uint((__unpacked)->seamless_cube_map, 23, 23) | \
            util_bitpack_uint((__unpacked)->clamp_integer_coordinates, 24, 24) | \
            util_bitpack_uint((__unpacked)->normalized_coordinates, 25, 25) | \
            util_bitpack_uint((__unpacked)->clamp_integer_array_indices, 26, 26) | \
            util_bitpack_uint((__unpacked)->minify_nearest, 27, 27) | \
            util_bitpack_uint((__unpacked)->magnify_nearest, 28, 28) | \
            util_bitpack_uint((__unpacked)->magnify_cutoff, 29, 29) | \
            util_bitpack_uint((__unpacked)->mipmap_mode, 30, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_ufixed_clamp((__unpacked)->minimum_lod, 0, 12, 8) | \
            util_bitpack_uint((__unpacked)->compare_function, 13, 15) | \
            util_bitpack_ufixed_clamp((__unpacked)->maximum_lod, 16, 28, 8); \
   __tmp_packed.opaque[ 2] = util_bitpack_sfixed_clamp((__unpacked)->lod_bias, 0, 15, 8) | \
            util_bitpack_uint((__unpacked)->maximum_anisotropy - 1, 16, 20) | \
            util_bitpack_uint((__unpacked)->lod_algorithm, 24, 25); \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->border_color_r, 0, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->border_color_g, 0, 31); \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->border_color_b, 0, 31); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->border_color_a, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_plane_type {
        MALI_PLANE_TYPE_NULL                 =      0,
        MALI_PLANE_TYPE_GENERIC              =      1,
        MALI_PLANE_TYPE_ASTC_2D              =      4,
        MALI_PLANE_TYPE_ASTC_3D              =      5,
        MALI_PLANE_TYPE_AFBC                 =      6,
        MALI_PLANE_TYPE_CHROMA_2P            =      8,
};

enum mali_clump_ordering {
        MALI_CLUMP_ORDERING_TILED_U_INTERLEAVED =      1,
        MALI_CLUMP_ORDERING_LINEAR           =      2,
};

enum mali_clump_format {
        MALI_CLUMP_FORMAT_RAW8               =      0,
        MALI_CLUMP_FORMAT_RAW16              =      1,
        MALI_CLUMP_FORMAT_RAW32              =      2,
        MALI_CLUMP_FORMAT_RAW64              =      3,
        MALI_CLUMP_FORMAT_RAW128             =      4,
        MALI_CLUMP_FORMAT_RAW24              =      8,
        MALI_CLUMP_FORMAT_RAW48              =      9,
        MALI_CLUMP_FORMAT_RAW96              =     10,
        MALI_CLUMP_FORMAT_D32X32             =     16,
        MALI_CLUMP_FORMAT_X32D32             =     17,
        MALI_CLUMP_FORMAT_X32S8X24           =     18,
        MALI_CLUMP_FORMAT_X24S8X32           =     19,
        MALI_CLUMP_FORMAT_X24S8              =     20,
        MALI_CLUMP_FORMAT_S8X24              =     21,
        MALI_CLUMP_FORMAT_S8                 =     22,
        MALI_CLUMP_FORMAT_L4A4               =     24,
        MALI_CLUMP_FORMAT_L8A8               =     25,
        MALI_CLUMP_FORMAT_A8                 =     26,
        MALI_CLUMP_FORMAT_ETC2_RGB8          =     32,
        MALI_CLUMP_FORMAT_ETC2_R11_UNORM     =     33,
        MALI_CLUMP_FORMAT_ETC2_R11_SNORM     =     34,
        MALI_CLUMP_FORMAT_ETC2_RG11_UNORM    =     35,
        MALI_CLUMP_FORMAT_ETC2_RG11_SNORM    =     36,
        MALI_CLUMP_FORMAT_ETC2_RGBA8         =     37,
        MALI_CLUMP_FORMAT_ETC2_RGB8A1        =     38,
        MALI_CLUMP_FORMAT_BC1_UNORM          =     48,
        MALI_CLUMP_FORMAT_BC2_UNORM          =     49,
        MALI_CLUMP_FORMAT_BC3_UNORM          =     50,
        MALI_CLUMP_FORMAT_BC4_UNORM          =     51,
        MALI_CLUMP_FORMAT_BC4_SNORM          =     52,
        MALI_CLUMP_FORMAT_BC5_UNORM          =     53,
        MALI_CLUMP_FORMAT_BC5_SNORM          =     54,
        MALI_CLUMP_FORMAT_BC6H_UF16          =     55,
        MALI_CLUMP_FORMAT_BC6H_SF16          =     56,
        MALI_CLUMP_FORMAT_BC7_UNORM          =     57,
        MALI_CLUMP_FORMAT_Y8_UV8_422         =     64,
        MALI_CLUMP_FORMAT_Y10_UV10_422       =     65,
        MALI_CLUMP_FORMAT_Y16_UV16_422       =     67,
        MALI_CLUMP_FORMAT_Y8_UV8_420         =     72,
        MALI_CLUMP_FORMAT_Y10_UV10_420       =     73,
        MALI_CLUMP_FORMAT_Y16_UV16_420       =     75,
        MALI_CLUMP_FORMAT_YUV420_10X6        =     76,
        MALI_CLUMP_FORMAT_RAW10              =    120,
        MALI_CLUMP_FORMAT_RAW12              =    121,
        MALI_CLUMP_FORMAT_R8G8B8G8           =    124,
        MALI_CLUMP_FORMAT_G8R8G8B8           =    125,
};

enum mali_afbc_superblock_size {
        MALI_AFBC_SUPERBLOCK_SIZE_16X16      =      0,
        MALI_AFBC_SUPERBLOCK_SIZE_32X8       =      1,
        MALI_AFBC_SUPERBLOCK_SIZE_64X4       =      2,
};

enum mali_afbc_compression_mode {
        MALI_AFBC_COMPRESSION_MODE_R8        =      0,
        MALI_AFBC_COMPRESSION_MODE_R8G8      =      3,
        MALI_AFBC_COMPRESSION_MODE_R5G6B5    =      4,
        MALI_AFBC_COMPRESSION_MODE_R4G4B4A4  =      5,
        MALI_AFBC_COMPRESSION_MODE_R5G5B5A1  =      6,
        MALI_AFBC_COMPRESSION_MODE_R8G8B8    =      9,
        MALI_AFBC_COMPRESSION_MODE_R8G8B8A8  =     10,
        MALI_AFBC_COMPRESSION_MODE_R10G10B10A2 =     11,
        MALI_AFBC_COMPRESSION_MODE_R11G11B10 =     12,
        MALI_AFBC_COMPRESSION_MODE_S8        =     14,
        MALI_AFBC_COMPRESSION_MODE_X24S8     =     15,
        MALI_AFBC_COMPRESSION_MODE_YUV420_6C8 =     32,
        MALI_AFBC_COMPRESSION_MODE_YUV420_2C8 =     34,
        MALI_AFBC_COMPRESSION_MODE_YUV420_1C8 =     35,
        MALI_AFBC_COMPRESSION_MODE_YUV422_4C8 =     37,
        MALI_AFBC_COMPRESSION_MODE_YUV422_2C8 =     38,
        MALI_AFBC_COMPRESSION_MODE_YUV422_1C8 =     39,
        MALI_AFBC_COMPRESSION_MODE_YUV420_6C10 =     40,
        MALI_AFBC_COMPRESSION_MODE_YUV420_2C10 =     42,
        MALI_AFBC_COMPRESSION_MODE_YUV420_1C10 =     43,
        MALI_AFBC_COMPRESSION_MODE_YUV422_4C10 =     45,
        MALI_AFBC_COMPRESSION_MODE_YUV422_2C10 =     46,
        MALI_AFBC_COMPRESSION_MODE_YUV422_1C10 =     47,
};

struct MALI_TEXTURE {
   enum mali_descriptor_type            type;
   enum mali_texture_dimension          dimension;
   bool                                 sample_corner_position;
   bool                                 normalize_coordinates;
   uint32_t                             format;
   uint32_t                             width;
   uint32_t                             height;
   uint32_t                             swizzle;
   bool                                 texel_interleave;
   uint32_t                             levels;
   uint32_t                             minimum_level;
   float                                minimum_lod;
   uint32_t                             sample_count;
   float                                maximum_lod;
   uint64_t                             surfaces;
   uint32_t                             array_size;
   uint32_t                             depth;
};

#define MALI_TEXTURE_header                     \
   .type = MALI_DESCRIPTOR_TYPE_TEXTURE,  \
   .sample_corner_position = false,  \
   .normalize_coordinates = false,  \
   .levels = 1,  \
   .minimum_lod = 0,  \
   .sample_count = 1,  \
   .maximum_lod = 0,  \
   .array_size = 1,  \
   .depth = 1

#define MALI_TEXTURE_LENGTH 32

#define MALI_TEXTURE_ALIGN 32

struct mali_texture_packed { uint32_t opaque[8]; };

#define MALI_TEXTURE_PACKED_T struct mali_texture_packed

#define MALI_TEXTURE_pack(__packed, __unpacked) \
do { \
   MALI_TEXTURE_PACKED_T __tmp_packed; \
   assert((__unpacked)->width >= 1); \
   assert((__unpacked)->height >= 1); \
   assert((__unpacked)->levels >= 1); \
   assert(IS_POT_NONZERO((__unpacked)->sample_count)); \
   assert((__unpacked)->array_size >= 1); \
   assert((__unpacked)->depth >= 1); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->dimension, 4, 6) | \
            util_bitpack_uint((__unpacked)->sample_corner_position, 8, 8) | \
            util_bitpack_uint((__unpacked)->normalize_coordinates, 9, 9) | \
            util_bitpack_uint((__unpacked)->format, 10, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->width - 1, 0, 15) | \
            util_bitpack_uint((__unpacked)->height - 1, 16, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->swizzle, 0, 11) | \
            util_bitpack_uint((__unpacked)->texel_interleave, 12, 12) | \
            util_bitpack_uint((__unpacked)->levels - 1, 16, 20) | \
            util_bitpack_uint((__unpacked)->minimum_level, 24, 28); \
   __tmp_packed.opaque[ 3] = util_bitpack_ufixed_clamp((__unpacked)->minimum_lod, 0, 12, 8) | \
            util_bitpack_uint(util_logbase2((__unpacked)->sample_count), 13, 15) | \
            util_bitpack_ufixed_clamp((__unpacked)->maximum_lod, 16, 28, 8); \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->surfaces, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->surfaces, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->array_size - 1, 0, 15); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->depth - 1, 0, 15); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_PLANE_HEADER {
   enum mali_descriptor_type            type;
   enum mali_plane_type                 plane_type;
   uint32_t                             ignore;
};

#define MALI_PLANE_HEADER_header                \
   .type = MALI_DESCRIPTOR_TYPE_PLANE,  \
   .plane_type = MALI_PLANE_TYPE_NULL

#define MALI_PLANE_HEADER_LENGTH 4

#define MALI_PLANE_HEADER_ALIGN 32

struct mali_plane_header_packed { uint32_t opaque[1]; };

#define MALI_PLANE_HEADER_PACKED_T struct mali_plane_header_packed

#define MALI_PLANE_HEADER_pack(__packed, __unpacked) \
do { \
   MALI_PLANE_HEADER_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->plane_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->ignore, 8, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_NULL_PLANE {
   enum mali_descriptor_type            type;
   enum mali_plane_type                 plane_type;
};

#define MALI_NULL_PLANE_header                  \
   .type = MALI_DESCRIPTOR_TYPE_PLANE,  \
   .plane_type = MALI_PLANE_TYPE_NULL

#define MALI_NULL_PLANE_LENGTH 32

#define MALI_NULL_PLANE_ALIGN 32

struct mali_null_plane_packed { uint32_t opaque[8]; };

#define MALI_NULL_PLANE_PACKED_T struct mali_null_plane_packed

#define MALI_NULL_PLANE_pack(__packed, __unpacked) \
do { \
   MALI_NULL_PLANE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->plane_type, 4, 7); \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_GENERIC_PLANE {
   enum mali_descriptor_type            type;
   enum mali_plane_type                 plane_type;
   enum mali_clump_ordering             clump_ordering;
   enum mali_clump_format               clump_format;
   uint64_t                             size;
   uint64_t                             pointer;
   uint64_t                             row_stride;
   uint64_t                             slice_stride;
};

#define MALI_GENERIC_PLANE_header               \
   .type = MALI_DESCRIPTOR_TYPE_PLANE,  \
   .plane_type = MALI_PLANE_TYPE_GENERIC

#define MALI_GENERIC_PLANE_LENGTH 32

#define MALI_GENERIC_PLANE_ALIGN 32

struct mali_generic_plane_packed { uint32_t opaque[8]; };

#define MALI_GENERIC_PLANE_PACKED_T struct mali_generic_plane_packed

#define MALI_GENERIC_PLANE_pack(__packed, __unpacked) \
do { \
   MALI_GENERIC_PLANE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->plane_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->clump_ordering, 8, 11) | \
            util_bitpack_uint((__unpacked)->clump_format, 24, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->size, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->pointer, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->pointer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->slice_stride, 0, 31); \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_ASTC_2D_PLANE {
   enum mali_descriptor_type            type;
   enum mali_plane_type                 plane_type;
   enum mali_clump_ordering             clump_ordering;
   bool                                 decode_hdr;
   bool                                 decode_wide;
   enum mali_astc_2d_dimension          block_width;
   enum mali_astc_2d_dimension          block_height;
   uint64_t                             size;
   uint64_t                             pointer;
   uint64_t                             row_stride;
   uint64_t                             slice_stride;
};

#define MALI_ASTC_2D_PLANE_header               \
   .type = MALI_DESCRIPTOR_TYPE_PLANE,  \
   .plane_type = MALI_PLANE_TYPE_ASTC_2D

#define MALI_ASTC_2D_PLANE_LENGTH 32

#define MALI_ASTC_2D_PLANE_ALIGN 32

struct mali_astc_2d_plane_packed { uint32_t opaque[8]; };

#define MALI_ASTC_2D_PLANE_PACKED_T struct mali_astc_2d_plane_packed

#define MALI_ASTC_2D_PLANE_pack(__packed, __unpacked) \
do { \
   MALI_ASTC_2D_PLANE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->plane_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->clump_ordering, 8, 11) | \
            util_bitpack_uint((__unpacked)->decode_hdr, 24, 24) | \
            util_bitpack_uint((__unpacked)->decode_wide, 25, 25) | \
            util_bitpack_uint((__unpacked)->block_width, 26, 28) | \
            util_bitpack_uint((__unpacked)->block_height, 29, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->size, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->pointer, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->pointer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->slice_stride, 0, 31); \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_ASTC_3D_PLANE {
   enum mali_descriptor_type            type;
   enum mali_plane_type                 plane_type;
   enum mali_clump_ordering             clump_ordering;
   bool                                 decode_hdr;
   bool                                 decode_wide;
   enum mali_astc_3d_dimension          block_width;
   enum mali_astc_3d_dimension          block_height;
   enum mali_astc_3d_dimension          block_depth;
   uint64_t                             size;
   uint64_t                             pointer;
   uint64_t                             row_stride;
   uint64_t                             slice_stride;
};

#define MALI_ASTC_3D_PLANE_header               \
   .type = MALI_DESCRIPTOR_TYPE_PLANE,  \
   .plane_type = MALI_PLANE_TYPE_ASTC_3D

#define MALI_ASTC_3D_PLANE_LENGTH 32

#define MALI_ASTC_3D_PLANE_ALIGN 32

struct mali_astc_3d_plane_packed { uint32_t opaque[8]; };

#define MALI_ASTC_3D_PLANE_PACKED_T struct mali_astc_3d_plane_packed

#define MALI_ASTC_3D_PLANE_pack(__packed, __unpacked) \
do { \
   MALI_ASTC_3D_PLANE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->plane_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->clump_ordering, 8, 11) | \
            util_bitpack_uint((__unpacked)->decode_hdr, 24, 24) | \
            util_bitpack_uint((__unpacked)->decode_wide, 25, 25) | \
            util_bitpack_uint((__unpacked)->block_width, 26, 27) | \
            util_bitpack_uint((__unpacked)->block_height, 28, 29) | \
            util_bitpack_uint((__unpacked)->block_depth, 30, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->size, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->pointer, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->pointer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->slice_stride, 0, 31); \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_AFBC_PLANE {
   enum mali_descriptor_type            type;
   enum mali_plane_type                 plane_type;
   enum mali_afbc_superblock_size       superblock_size;
   bool                                 ytr;
   bool                                 split_block;
   bool                                 alpha_hint;
   bool                                 tiled_header;
   bool                                 prefetch;
   enum mali_afbc_compression_mode      compression_mode;
   uint64_t                             size;
   uint64_t                             pointer;
   uint64_t                             header_row_stride;
   uint64_t                             header_slice_size;
   uint64_t                             header_slice_stride;
};

#define MALI_AFBC_PLANE_header                  \
   .type = MALI_DESCRIPTOR_TYPE_PLANE,  \
   .plane_type = MALI_PLANE_TYPE_AFBC

#define MALI_AFBC_PLANE_LENGTH 32

#define MALI_AFBC_PLANE_ALIGN 32

struct mali_afbc_plane_packed { uint32_t opaque[8]; };

#define MALI_AFBC_PLANE_PACKED_T struct mali_afbc_plane_packed

#define MALI_AFBC_PLANE_pack(__packed, __unpacked) \
do { \
   MALI_AFBC_PLANE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->plane_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->superblock_size, 8, 10) | \
            util_bitpack_uint((__unpacked)->ytr, 16, 16) | \
            util_bitpack_uint((__unpacked)->split_block, 17, 17) | \
            util_bitpack_uint((__unpacked)->alpha_hint, 18, 18) | \
            util_bitpack_uint((__unpacked)->tiled_header, 19, 19) | \
            util_bitpack_uint((__unpacked)->prefetch, 20, 20) | \
            util_bitpack_uint((__unpacked)->compression_mode, 26, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->size, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->pointer, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->pointer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->header_row_stride, 0, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->header_slice_size, 0, 31); \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->header_slice_stride, 0, 31); \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_CHROMA_2P_PLANE {
   enum mali_descriptor_type            type;
   enum mali_plane_type                 plane_type;
   enum mali_clump_ordering             clump_ordering;
   enum mali_clump_format               clump_format;
   uint64_t                             size;
   uint64_t                             pointer;
   uint64_t                             row_stride;
   uint64_t                             secondary_pointer;
};

#define MALI_CHROMA_2P_PLANE_header             \
   .type = MALI_DESCRIPTOR_TYPE_PLANE,  \
   .plane_type = MALI_PLANE_TYPE_CHROMA_2P

#define MALI_CHROMA_2P_PLANE_LENGTH 32

#define MALI_CHROMA_2P_PLANE_ALIGN 32

struct mali_chroma_2p_plane_packed { uint32_t opaque[8]; };

#define MALI_CHROMA_2P_PLANE_PACKED_T struct mali_chroma_2p_plane_packed

#define MALI_CHROMA_2P_PLANE_pack(__packed, __unpacked) \
do { \
   MALI_CHROMA_2P_PLANE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->plane_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->clump_ordering, 8, 11) | \
            util_bitpack_uint((__unpacked)->clump_format, 24, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->size, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->pointer, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->pointer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->secondary_pointer, 0, 63); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->secondary_pointer, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_shader_register_allocation {
        MALI_SHADER_REGISTER_ALLOCATION_64_PER_THREAD =      0,
        MALI_SHADER_REGISTER_ALLOCATION_32_PER_THREAD =      2,
};

enum mali_flush_to_zero_mode {
        MALI_FLUSH_TO_ZERO_MODE_PRESERVE_SUBNORMALS =      0,
        MALI_FLUSH_TO_ZERO_MODE_DX11         =      1,
        MALI_FLUSH_TO_ZERO_MODE_ALWAYS       =      2,
        MALI_FLUSH_TO_ZERO_MODE_ABRUPT       =      3,
};

struct MALI_PRELOAD {
   uint32_t                             r48_r63;
   bool                                 r55;
   bool                                 r56;
   bool                                 r57;
   bool                                 r58;
   bool                                 r59;
   bool                                 r60;
   bool                                 r61;
   bool                                 r62;
   bool                                 r63;
};

#define MALI_PRELOAD_header                     \
   0

#define MALI_PRELOAD_LENGTH 4

struct mali_preload_packed { uint32_t opaque[1]; };

#define MALI_PRELOAD_PACKED_T struct mali_preload_packed

#define MALI_PRELOAD_pack(__packed, __unpacked) \
do { \
   MALI_PRELOAD_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->r48_r63, 0, 15) | \
            util_bitpack_uint((__unpacked)->r55, 7, 7) | \
            util_bitpack_uint((__unpacked)->r56, 8, 8) | \
            util_bitpack_uint((__unpacked)->r57, 9, 9) | \
            util_bitpack_uint((__unpacked)->r58, 10, 10) | \
            util_bitpack_uint((__unpacked)->r59, 11, 11) | \
            util_bitpack_uint((__unpacked)->r60, 12, 12) | \
            util_bitpack_uint((__unpacked)->r61, 13, 13) | \
            util_bitpack_uint((__unpacked)->r62, 14, 14) | \
            util_bitpack_uint((__unpacked)->r63, 15, 15); \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_coverage_bitmask_type {
        MALI_COVERAGE_BITMASK_TYPE_DX        =      0,
        MALI_COVERAGE_BITMASK_TYPE_GL        =      1,
};

enum mali_warp_limit {
        MALI_WARP_LIMIT_FULL                 =      0,
        MALI_WARP_LIMIT_HALF                 =      1,
        MALI_WARP_LIMIT_ONE_QUARTER          =      2,
        MALI_WARP_LIMIT_ONE_HEIGHT           =      3,
};

struct MALI_SHADER_PROGRAM {
   enum mali_descriptor_type            type;
   enum mali_shader_stage               stage;
   enum mali_coverage_bitmask_type      fragment_coverage_bitmask_type;
   enum mali_warp_limit                 vertex_warp_limit;
   bool                                 suppress_nan;
   enum mali_flush_to_zero_mode         flush_to_zero_mode;
   bool                                 suppress_inf;
   bool                                 requires_helper_threads;
   bool                                 shader_contains_jump_ex;
   enum mali_shader_register_allocation register_allocation;
   struct MALI_PRELOAD                  preload;
   uint64_t                             binary;
};

#define MALI_SHADER_PROGRAM_header              \
   .type = MALI_DESCRIPTOR_TYPE_SHADER,  \
   .preload = { MALI_PRELOAD_header }

#define MALI_SHADER_PROGRAM_LENGTH 32

#define MALI_SHADER_PROGRAM_ALIGN 32

struct mali_shader_program_packed { uint32_t opaque[8]; };

#define MALI_SHADER_PROGRAM_PACKED_T struct mali_shader_program_packed

#define MALI_SHADER_PROGRAM_pack(__packed, __unpacked) \
do { \
   MALI_SHADER_PROGRAM_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->stage, 4, 7) | \
            util_bitpack_uint((__unpacked)->fragment_coverage_bitmask_type, 8, 8) | \
            util_bitpack_uint((__unpacked)->vertex_warp_limit, 8, 9) | \
            util_bitpack_uint((__unpacked)->suppress_nan, 16, 16) | \
            util_bitpack_uint((__unpacked)->flush_to_zero_mode, 17, 18) | \
            util_bitpack_uint((__unpacked)->suppress_inf, 19, 19) | \
            util_bitpack_uint((__unpacked)->requires_helper_threads, 28, 28) | \
            util_bitpack_uint((__unpacked)->shader_contains_jump_ex, 29, 29) | \
            util_bitpack_uint((__unpacked)->register_allocation, 30, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->preload.r48_r63, 0, 15) | \
            util_bitpack_uint((__unpacked)->preload.r55, 7, 7) | \
            util_bitpack_uint((__unpacked)->preload.r56, 8, 8) | \
            util_bitpack_uint((__unpacked)->preload.r57, 9, 9) | \
            util_bitpack_uint((__unpacked)->preload.r58, 10, 10) | \
            util_bitpack_uint((__unpacked)->preload.r59, 11, 11) | \
            util_bitpack_uint((__unpacked)->preload.r60, 12, 12) | \
            util_bitpack_uint((__unpacked)->preload.r61, 13, 13) | \
            util_bitpack_uint((__unpacked)->preload.r62, 14, 14) | \
            util_bitpack_uint((__unpacked)->preload.r63, 15, 15); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->binary, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->binary, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_SCISSOR {
   uint32_t                             scissor_minimum_x;
   uint32_t                             scissor_minimum_y;
   uint32_t                             scissor_maximum_x;
   uint32_t                             scissor_maximum_y;
};

#define MALI_SCISSOR_header                     \
   0

#define MALI_SCISSOR_LENGTH 8

struct mali_scissor_packed { uint32_t opaque[2]; };

#define MALI_SCISSOR_PACKED_T struct mali_scissor_packed

#define MALI_SCISSOR_pack(__packed, __unpacked) \
do { \
   MALI_SCISSOR_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->scissor_minimum_x, 0, 15) | \
            util_bitpack_uint((__unpacked)->scissor_minimum_y, 16, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->scissor_maximum_x, 0, 15) | \
            util_bitpack_uint((__unpacked)->scissor_maximum_y, 16, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_LOCAL_STORAGE {
   uint32_t                             tls_size;
   uint32_t                             wls_instances;
#define MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM      2147483648
   uint32_t                             wls_size_base;
   uint32_t                             wls_size_scale;
   uint64_t                             tls_base_pointer;
   enum mali_address_mode               tls_address_mode;
   uint64_t                             wls_base_pointer;
};

#define MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM      2147483648

#define MALI_LOCAL_STORAGE_header               \
   .wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM

#define MALI_LOCAL_STORAGE_LENGTH 32

#define MALI_LOCAL_STORAGE_ALIGN 64

struct mali_local_storage_packed { uint32_t opaque[8]; };

#define MALI_LOCAL_STORAGE_PACKED_T struct mali_local_storage_packed

#define MALI_LOCAL_STORAGE_pack(__packed, __unpacked) \
do { \
   MALI_LOCAL_STORAGE_PACKED_T __tmp_packed; \
   assert(IS_POT_NONZERO((__unpacked)->wls_instances)); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->tls_size, 0, 4); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint(util_logbase2((__unpacked)->wls_instances), 0, 4) | \
            util_bitpack_uint((__unpacked)->wls_size_base, 5, 6) | \
            util_bitpack_uint((__unpacked)->wls_size_scale, 8, 12); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->tls_base_pointer, 0, 47); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->tls_base_pointer, 0, 47) >> 32 | \
            util_bitpack_uint((__unpacked)->tls_address_mode, 28, 31); \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->wls_base_pointer, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->wls_base_pointer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_color_buffer_internal_format {
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW_VALUE =      0,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8 =      1,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_R10G10B10A2 =      2,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A2 =      3,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_R4G4B4A4 =      4,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_R5G6B5A0 =      5,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_R5G5B5A1 =      6,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_R16F =     16,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RG16F =     17,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RGBA16F =     18,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RG11F_B10F =     20,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW8 =     32,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW16 =     33,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW32 =     34,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW64 =     35,
        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW128 =     36,
};

enum mali_color_format {
        MALI_COLOR_FORMAT_RAW8               =      0,
        MALI_COLOR_FORMAT_RAW16              =      1,
        MALI_COLOR_FORMAT_RAW24              =      2,
        MALI_COLOR_FORMAT_RAW32              =      3,
        MALI_COLOR_FORMAT_RAW48              =      4,
        MALI_COLOR_FORMAT_RAW64              =      5,
        MALI_COLOR_FORMAT_RAW96              =      6,
        MALI_COLOR_FORMAT_RAW128             =      7,
        MALI_COLOR_FORMAT_RAW192             =      8,
        MALI_COLOR_FORMAT_RAW256             =      9,
        MALI_COLOR_FORMAT_RAW384             =     10,
        MALI_COLOR_FORMAT_RAW512             =     11,
        MALI_COLOR_FORMAT_RAW768             =     12,
        MALI_COLOR_FORMAT_RAW1024            =     13,
        MALI_COLOR_FORMAT_RAW1536            =     14,
        MALI_COLOR_FORMAT_RAW2048            =     15,
        MALI_COLOR_FORMAT_R8                 =     16,
        MALI_COLOR_FORMAT_R8G8               =     17,
        MALI_COLOR_FORMAT_R8G8B8             =     18,
        MALI_COLOR_FORMAT_R8G8B8A8           =     19,
        MALI_COLOR_FORMAT_R4G4B4A4           =     20,
        MALI_COLOR_FORMAT_R5G6B5             =     21,
        MALI_COLOR_FORMAT_R8G8B8_FROM_R8G8B8A2 =     22,
        MALI_COLOR_FORMAT_R10G10B10A2        =     24,
        MALI_COLOR_FORMAT_A2B10G10R10        =     25,
        MALI_COLOR_FORMAT_R5G5B5A1           =     28,
        MALI_COLOR_FORMAT_A1B5G5R5           =     29,
        MALI_COLOR_FORMAT_NATIVE             =     31,
};

enum mali_float_color_format {
        MALI_FLOAT_COLOR_FORMAT_RAW8         =      0,
        MALI_FLOAT_COLOR_FORMAT_RAW16        =      1,
        MALI_FLOAT_COLOR_FORMAT_RAW24        =      2,
        MALI_FLOAT_COLOR_FORMAT_RAW32        =      3,
        MALI_FLOAT_COLOR_FORMAT_RAW48        =      4,
        MALI_FLOAT_COLOR_FORMAT_RAW64        =      5,
        MALI_FLOAT_COLOR_FORMAT_RAW96        =      6,
        MALI_FLOAT_COLOR_FORMAT_RAW128       =      7,
        MALI_FLOAT_COLOR_FORMAT_R16F         =     16,
        MALI_FLOAT_COLOR_FORMAT_RG16F        =     17,
        MALI_FLOAT_COLOR_FORMAT_RGB16F       =     18,
        MALI_FLOAT_COLOR_FORMAT_RGBA16F      =     19,
        MALI_FLOAT_COLOR_FORMAT_RG11F_B10F   =     20,
};

enum mali_downsampling_accumulation_mode {
        MALI_DOWNSAMPLING_ACCUMULATION_MODE_UNSIGNED_NORMALIZED_INTEGER =      0,
        MALI_DOWNSAMPLING_ACCUMULATION_MODE_SIGNED_NORMALIZED_INTEGER =      1,
};

enum mali_sample_layout {
        MALI_SAMPLE_LAYOUT_ORDERED_4X_GRID   =      0,
        MALI_SAMPLE_LAYOUT_ROTATED_4X_GRID   =      1,
        MALI_SAMPLE_LAYOUT_D3D_8X_GRID       =      2,
        MALI_SAMPLE_LAYOUT_D3D_16X_GRID      =      3,
};

enum mali_zs_format {
        MALI_ZS_FORMAT_D16                   =      1,
        MALI_ZS_FORMAT_D24                   =      2,
        MALI_ZS_FORMAT_D24X8                 =      4,
        MALI_ZS_FORMAT_D24S8                 =      5,
        MALI_ZS_FORMAT_D32                   =     14,
};

enum mali_zs_preload_format {
        MALI_ZS_PRELOAD_FORMAT_D32_S8X24     =      4,
};

enum mali_s_format {
        MALI_S_FORMAT_S8                     =      1,
        MALI_S_FORMAT_X24S8                  =      4,
};

enum mali_tie_break_rule {
        MALI_TIE_BREAK_RULE_0_IN_180_OUT     =      0,
        MALI_TIE_BREAK_RULE_0_OUT_180_IN     =      1,
        MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT =      2,
        MALI_TIE_BREAK_RULE_MINUS_180_OUT_0_IN =      3,
        MALI_TIE_BREAK_RULE_90_IN_270_OUT    =      4,
        MALI_TIE_BREAK_RULE_90_OUT_270_IN    =      5,
        MALI_TIE_BREAK_RULE_MINUS_90_IN_90_OUT =      6,
        MALI_TIE_BREAK_RULE_MINUS_90_OUT_90_IN =      7,
};

struct MALI_RT_BUFFER {
   uint64_t                             base;
   uint64_t                             row_stride;
   uint64_t                             surface_stride;
};

#define MALI_RT_BUFFER_header                   \
   0

#define MALI_RT_BUFFER_LENGTH 16

struct mali_rt_buffer_packed { uint32_t opaque[4]; };

#define MALI_RT_BUFFER_PACKED_T struct mali_rt_buffer_packed

#define MALI_RT_BUFFER_pack(__packed, __unpacked) \
do { \
   MALI_RT_BUFFER_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->base, 0, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->surface_stride, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_sample_pattern {
        MALI_SAMPLE_PATTERN_SINGLE_SAMPLED   =      0,
        MALI_SAMPLE_PATTERN_ORDERED_4X_GRID  =      1,
        MALI_SAMPLE_PATTERN_ROTATED_4X_GRID  =      2,
        MALI_SAMPLE_PATTERN_D3D_8X_GRID      =      3,
        MALI_SAMPLE_PATTERN_D3D_16X_GRID     =      4,
};

enum mali_z_internal_format {
        MALI_Z_INTERNAL_FORMAT_D16           =      0,
        MALI_Z_INTERNAL_FORMAT_D24           =      1,
        MALI_Z_INTERNAL_FORMAT_D32           =      2,
};

enum mali_pre_post_frame_shader_mode {
        MALI_PRE_POST_FRAME_SHADER_MODE_NEVER =      0,
        MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS =      1,
        MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT =      2,
        MALI_PRE_POST_FRAME_SHADER_MODE_EARLY_ZS_ALWAYS =      3,
};

enum mali_downscale {
        MALI_DOWNSCALE_NONE                  =      0,
        MALI_DOWNSCALE_2X                    =      1,
        MALI_DOWNSCALE_4X                    =      2,
};

struct MALI_FRAMEBUFFER_PARAMETERS {
   enum mali_pre_post_frame_shader_mode pre_frame_0;
   enum mali_pre_post_frame_shader_mode pre_frame_1;
   enum mali_pre_post_frame_shader_mode post_frame;
   enum mali_downscale                  downscale_geometry;
   uint32_t                             internal_layer_index;
   uint64_t                             frame_argument;
   uint64_t                             sample_locations;
   uint64_t                             frame_shader_dcds;
   uint32_t                             width;
   uint32_t                             height;
   uint32_t                             bound_min_x;
   uint32_t                             bound_min_y;
   uint32_t                             bound_max_x;
   uint32_t                             bound_max_y;
   uint32_t                             sample_count;
   enum mali_sample_pattern             sample_pattern;
   enum mali_tie_break_rule             tie_break_rule;
   uint32_t                             effective_tile_size;
   bool                                 first_provoking_vertex;
   bool                                 point_sprite_coord_origin_max_y;
   bool                                 blend_suppress_inf;
   bool                                 blend_suppress_nan;
   bool                                 blend_suppress_denorm;
   uint32_t                             render_target_count;
   uint32_t                             color_buffer_allocation;
   uint32_t                             s_clear;
   bool                                 s_write_enable;
   bool                                 s_preload_enable;
   bool                                 s_unload_enable;
   enum mali_z_internal_format          z_internal_format;
   bool                                 z_write_enable;
   bool                                 z_preload_enable;
   bool                                 z_unload_enable;
   bool                                 has_zs_crc_extension;
   bool                                 empty_tile_read_enable;
   bool                                 empty_tile_write_enable;
   bool                                 crc_read_enable;
   bool                                 crc_write_enable;
   float                                z_clear;
   uint64_t                             tiler;
};

#define MALI_FRAMEBUFFER_PARAMETERS_header      \
   .sample_count = 1,  \
   .point_sprite_coord_origin_max_y = true

#define MALI_FRAMEBUFFER_PARAMETERS_LENGTH 64

#define MALI_FRAMEBUFFER_PARAMETERS_ALIGN 64

struct mali_framebuffer_parameters_packed { uint32_t opaque[16]; };

#define MALI_FRAMEBUFFER_PARAMETERS_PACKED_T struct mali_framebuffer_parameters_packed

#define MALI_FRAMEBUFFER_PARAMETERS_pack(__packed, __unpacked) \
do { \
   MALI_FRAMEBUFFER_PARAMETERS_PACKED_T __tmp_packed; \
   assert((__unpacked)->width >= 1); \
   assert((__unpacked)->height >= 1); \
   assert(IS_POT_NONZERO((__unpacked)->sample_count)); \
   assert(IS_POT_NONZERO((__unpacked)->effective_tile_size)); \
   assert((__unpacked)->render_target_count >= 1); \
   assert(((__unpacked)->color_buffer_allocation & 0x3ff) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->pre_frame_0, 0, 2) | \
            util_bitpack_uint((__unpacked)->pre_frame_1, 3, 5) | \
            util_bitpack_uint((__unpacked)->post_frame, 6, 8) | \
            util_bitpack_uint((__unpacked)->downscale_geometry, 9, 10) | \
            util_bitpack_uint((__unpacked)->internal_layer_index, 24, 31); \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->frame_argument, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->frame_argument, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->sample_locations, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->sample_locations, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->frame_shader_dcds, 0, 63); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->frame_shader_dcds, 0, 63) >> 32; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->width - 1, 0, 15) | \
            util_bitpack_uint((__unpacked)->height - 1, 16, 31); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->bound_min_x, 0, 15) | \
            util_bitpack_uint((__unpacked)->bound_min_y, 16, 31); \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->bound_max_x, 0, 15) | \
            util_bitpack_uint((__unpacked)->bound_max_y, 16, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint(util_logbase2((__unpacked)->sample_count), 0, 2) | \
            util_bitpack_uint((__unpacked)->sample_pattern, 3, 5) | \
            util_bitpack_uint((__unpacked)->tie_break_rule, 6, 8) | \
            util_bitpack_uint(util_logbase2((__unpacked)->effective_tile_size), 9, 12) | \
            util_bitpack_uint((__unpacked)->first_provoking_vertex, 14, 14) | \
            util_bitpack_uint((__unpacked)->point_sprite_coord_origin_max_y, 15, 15) | \
            util_bitpack_uint((__unpacked)->blend_suppress_inf, 16, 16) | \
            util_bitpack_uint((__unpacked)->blend_suppress_nan, 17, 17) | \
            util_bitpack_uint((__unpacked)->blend_suppress_denorm, 18, 18) | \
            util_bitpack_uint((__unpacked)->render_target_count - 1, 19, 22) | \
            util_bitpack_uint((__unpacked)->color_buffer_allocation >> 10, 24, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->s_clear, 0, 7) | \
            util_bitpack_uint((__unpacked)->s_write_enable, 8, 8) | \
            util_bitpack_uint((__unpacked)->s_preload_enable, 9, 9) | \
            util_bitpack_uint((__unpacked)->s_unload_enable, 10, 10) | \
            util_bitpack_uint((__unpacked)->z_internal_format, 16, 17) | \
            util_bitpack_uint((__unpacked)->z_write_enable, 18, 18) | \
            util_bitpack_uint((__unpacked)->z_preload_enable, 19, 19) | \
            util_bitpack_uint((__unpacked)->z_unload_enable, 20, 20) | \
            util_bitpack_uint((__unpacked)->has_zs_crc_extension, 21, 21) | \
            util_bitpack_uint((__unpacked)->empty_tile_read_enable, 28, 28) | \
            util_bitpack_uint((__unpacked)->empty_tile_write_enable, 29, 29) | \
            util_bitpack_uint((__unpacked)->crc_read_enable, 30, 30) | \
            util_bitpack_uint((__unpacked)->crc_write_enable, 31, 31); \
   __tmp_packed.opaque[13] = util_bitpack_float((__unpacked)->z_clear); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->tiler, 0, 63); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->tiler, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_CRC {
   uint32_t                             render_target;
   uint64_t                             row_stride;
   uint64_t                             clear_color;
   uint64_t                             base;
};

#define MALI_CRC_header                         \
   0

#define MALI_CRC_LENGTH 64

#define MALI_CRC_ALIGN 64

struct mali_crc_packed { uint32_t opaque[16]; };

#define MALI_CRC_PACKED_T struct mali_crc_packed

#define MALI_CRC_pack(__packed, __unpacked) \
do { \
   MALI_CRC_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->render_target, 13, 15); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->clear_color, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->clear_color, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->base, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = 0; \
   __tmp_packed.opaque[ 9] = 0; \
   __tmp_packed.opaque[10] = 0; \
   __tmp_packed.opaque[11] = 0; \
   __tmp_packed.opaque[12] = 0; \
   __tmp_packed.opaque[13] = 0; \
   __tmp_packed.opaque[14] = 0; \
   __tmp_packed.opaque[15] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_ZS_TARGET {
   enum mali_zs_format                  write_format;
   enum mali_block_format               block_format;
   enum mali_msaa                       msaa;
   bool                                 clean_tile_write_enable;
   uint64_t                             base;
   uint64_t                             row_stride;
   uint64_t                             surface_stride;
};

#define MALI_ZS_TARGET_header                   \
   .msaa = MALI_MSAA_SINGLE

#define MALI_ZS_TARGET_LENGTH 64

#define MALI_ZS_TARGET_ALIGN 64

struct mali_zs_target_packed { uint32_t opaque[16]; };

#define MALI_ZS_TARGET_PACKED_T struct mali_zs_target_packed

#define MALI_ZS_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_ZS_TARGET_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->write_format, 0, 3) | \
            util_bitpack_uint((__unpacked)->block_format, 4, 7) | \
            util_bitpack_uint((__unpacked)->msaa, 8, 9) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->base, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->base, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->surface_stride, 0, 31); \
   __tmp_packed.opaque[12] = 0; \
   __tmp_packed.opaque[13] = 0; \
   __tmp_packed.opaque[14] = 0; \
   __tmp_packed.opaque[15] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_AFBC_ZS_TARGET {
   enum mali_zs_format                  write_format;
   enum mali_block_format               block_format;
   enum mali_msaa                       msaa;
   bool                                 reverse_issue_order;
   bool                                 clean_tile_write_enable;
   uint64_t                             header;
   uint64_t                             header_row_stride;
   uint64_t                             body_offset;
};

#define MALI_AFBC_ZS_TARGET_header              \
   .msaa = MALI_MSAA_SINGLE

#define MALI_AFBC_ZS_TARGET_LENGTH 64

#define MALI_AFBC_ZS_TARGET_ALIGN 64

struct mali_afbc_zs_target_packed { uint32_t opaque[16]; };

#define MALI_AFBC_ZS_TARGET_PACKED_T struct mali_afbc_zs_target_packed

#define MALI_AFBC_ZS_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_AFBC_ZS_TARGET_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->write_format, 0, 3) | \
            util_bitpack_uint((__unpacked)->block_format, 4, 7) | \
            util_bitpack_uint((__unpacked)->msaa, 8, 9) | \
            util_bitpack_uint((__unpacked)->reverse_issue_order, 30, 30) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->header, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->header, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->header_row_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->body_offset, 0, 31); \
   __tmp_packed.opaque[12] = 0; \
   __tmp_packed.opaque[13] = 0; \
   __tmp_packed.opaque[14] = 0; \
   __tmp_packed.opaque[15] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_S_TARGET {
   enum mali_s_format                   write_format;
   enum mali_block_format               block_format;
   enum mali_msaa                       msaa;
   bool                                 clean_tile_write_enable;
   uint64_t                             base;
   uint64_t                             row_stride;
   uint64_t                             surface_stride;
};

#define MALI_S_TARGET_header                    \
   .msaa = MALI_MSAA_SINGLE

#define MALI_S_TARGET_LENGTH 64

#define MALI_S_TARGET_ALIGN 64

struct mali_s_target_packed { uint32_t opaque[16]; };

#define MALI_S_TARGET_PACKED_T struct mali_s_target_packed

#define MALI_S_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_S_TARGET_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->write_format, 16, 19) | \
            util_bitpack_uint((__unpacked)->block_format, 20, 23) | \
            util_bitpack_uint((__unpacked)->msaa, 24, 25) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = 0; \
   __tmp_packed.opaque[ 9] = 0; \
   __tmp_packed.opaque[10] = 0; \
   __tmp_packed.opaque[11] = 0; \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->base, 0, 63); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->base, 0, 63) >> 32; \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->surface_stride, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_AFBC_S_TARGET {
   enum mali_s_format                   write_format;
   enum mali_block_format               block_format;
   enum mali_msaa                       msaa;
   bool                                 reverse_issue_order;
   bool                                 clean_tile_write_enable;
   uint64_t                             header;
   uint64_t                             header_row_stride;
   uint64_t                             body_offset;
};

#define MALI_AFBC_S_TARGET_header               \
   .msaa = MALI_MSAA_SINGLE

#define MALI_AFBC_S_TARGET_LENGTH 64

#define MALI_AFBC_S_TARGET_ALIGN 64

struct mali_afbc_s_target_packed { uint32_t opaque[16]; };

#define MALI_AFBC_S_TARGET_PACKED_T struct mali_afbc_s_target_packed

#define MALI_AFBC_S_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_AFBC_S_TARGET_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->write_format, 16, 19) | \
            util_bitpack_uint((__unpacked)->block_format, 20, 23) | \
            util_bitpack_uint((__unpacked)->msaa, 24, 25) | \
            util_bitpack_uint((__unpacked)->reverse_issue_order, 30, 30) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = 0; \
   __tmp_packed.opaque[ 9] = 0; \
   __tmp_packed.opaque[10] = 0; \
   __tmp_packed.opaque[11] = 0; \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->header, 0, 63); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->header, 0, 63) >> 32; \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->header_row_stride, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->body_offset, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_ZS_CRC_EXTENSION {
   struct MALI_CRC                      crc;
   struct MALI_AFBC_ZS_TARGET           afbc_zs;
   struct MALI_ZS_TARGET                zs;
   struct MALI_AFBC_S_TARGET            afbc_s;
   struct MALI_S_TARGET                 s;
};

#define MALI_ZS_CRC_EXTENSION_header            \
   .crc = { MALI_CRC_header },  \
   .afbc_zs = { MALI_AFBC_ZS_TARGET_header },  \
   .zs = { MALI_ZS_TARGET_header },  \
   .afbc_s = { MALI_AFBC_S_TARGET_header },  \
   .s = { MALI_S_TARGET_header }

#define MALI_ZS_CRC_EXTENSION_LENGTH 64

#define MALI_ZS_CRC_EXTENSION_ALIGN 64

struct mali_zs_crc_extension_packed { uint32_t opaque[16]; };

#define MALI_ZS_CRC_EXTENSION_PACKED_T struct mali_zs_crc_extension_packed

#define MALI_ZS_CRC_EXTENSION_pack(__packed, __unpacked) \
do { \
   MALI_ZS_CRC_EXTENSION_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->crc.render_target, 13, 15) | \
            util_bitpack_uint((__unpacked)->afbc_zs.write_format, 0, 3) | \
            util_bitpack_uint((__unpacked)->afbc_zs.block_format, 4, 7) | \
            util_bitpack_uint((__unpacked)->afbc_zs.msaa, 8, 9) | \
            util_bitpack_uint((__unpacked)->afbc_zs.reverse_issue_order, 30, 30) | \
            util_bitpack_uint((__unpacked)->afbc_zs.clean_tile_write_enable, 31, 31) | \
            util_bitpack_uint((__unpacked)->zs.write_format, 0, 3) | \
            util_bitpack_uint((__unpacked)->zs.block_format, 4, 7) | \
            util_bitpack_uint((__unpacked)->zs.msaa, 8, 9) | \
            util_bitpack_uint((__unpacked)->zs.clean_tile_write_enable, 31, 31) | \
            util_bitpack_uint((__unpacked)->afbc_s.write_format, 16, 19) | \
            util_bitpack_uint((__unpacked)->afbc_s.block_format, 20, 23) | \
            util_bitpack_uint((__unpacked)->afbc_s.msaa, 24, 25) | \
            util_bitpack_uint((__unpacked)->afbc_s.reverse_issue_order, 30, 30) | \
            util_bitpack_uint((__unpacked)->afbc_s.clean_tile_write_enable, 31, 31) | \
            util_bitpack_uint((__unpacked)->s.write_format, 16, 19) | \
            util_bitpack_uint((__unpacked)->s.block_format, 20, 23) | \
            util_bitpack_uint((__unpacked)->s.msaa, 24, 25) | \
            util_bitpack_uint((__unpacked)->s.clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->crc.row_stride, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->crc.clear_color, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->crc.clear_color, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->crc.base, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->crc.base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->afbc_zs.header, 0, 63) | \
            util_bitpack_uint((__unpacked)->zs.base, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->afbc_zs.header, 0, 63) >> 32 | \
            util_bitpack_uint((__unpacked)->zs.base, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->afbc_zs.header_row_stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->zs.row_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->afbc_zs.body_offset, 0, 31) | \
            util_bitpack_uint((__unpacked)->zs.surface_stride, 0, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->afbc_s.header, 0, 63) | \
            util_bitpack_uint((__unpacked)->s.base, 0, 63); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->afbc_s.header, 0, 63) >> 32 | \
            util_bitpack_uint((__unpacked)->s.base, 0, 63) >> 32; \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->afbc_s.header_row_stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->s.row_stride, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->afbc_s.body_offset, 0, 31) | \
            util_bitpack_uint((__unpacked)->s.surface_stride, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_yuv_conv_k6 {
        MALI_YUV_CONV_K6_0                   =      0,
        MALI_YUV_CONV_K6_16                  =      1,
};

enum mali_yuv_conv_k7_clamp {
        MALI_YUV_CONV_K7_CLAMP_MINUS_128_TO_127 =      0,
        MALI_YUV_CONV_K7_CLAMP_MINUS_112_TO_111 =      1,
        MALI_YUV_CONV_K7_CLAMP_0_TO_255      =      2,
        MALI_YUV_CONV_K7_CLAMP_16_TO_239     =      3,
};

enum mali_yuv_conv_k8 {
        MALI_YUV_CONV_K8_220                 =      0,
        MALI_YUV_CONV_K8_256                 =      1,
};

struct MALI_RT_CLEAR {
   uint64_t                             color_0;
   uint64_t                             color_1;
   uint64_t                             color_2;
   uint64_t                             color_3;
};

#define MALI_RT_CLEAR_header                    \
   0

#define MALI_RT_CLEAR_LENGTH 16

struct mali_rt_clear_packed { uint32_t opaque[4]; };

#define MALI_RT_CLEAR_PACKED_T struct mali_rt_clear_packed

#define MALI_RT_CLEAR_pack(__packed, __unpacked) \
do { \
   MALI_RT_CLEAR_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->color_0, 0, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->color_1, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->color_2, 0, 31); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->color_3, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_RGB_RENDER_TARGET {
   uint32_t                             internal_buffer_offset;
   bool                                 yuv_enable;
   bool                                 dithered_clear;
   enum mali_color_buffer_internal_format internal_format;
   bool                                 write_enable;
   enum mali_color_format               writeback_format;
   enum mali_float_color_format         writeback_float_format;
   enum mali_block_format               writeback_block_format;
   enum mali_msaa                       writeback_msaa;
   bool                                 srgb;
   bool                                 dithering_enable;
   uint32_t                             swizzle;
   bool                                 clean_tile_write_enable;
   struct MALI_RT_BUFFER                writeback_buffer;
   struct MALI_RT_CLEAR                 clear;
};

#define MALI_RGB_RENDER_TARGET_header           \
   .yuv_enable = false,  \
   .writeback_buffer = { MALI_RT_BUFFER_header },  \
   .clear = { MALI_RT_CLEAR_header }

#define MALI_RGB_RENDER_TARGET_LENGTH 64

#define MALI_RGB_RENDER_TARGET_ALIGN 64

struct mali_rgb_render_target_packed { uint32_t opaque[16]; };

#define MALI_RGB_RENDER_TARGET_PACKED_T struct mali_rgb_render_target_packed

#define MALI_RGB_RENDER_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_RGB_RENDER_TARGET_PACKED_T __tmp_packed; \
   assert(((__unpacked)->internal_buffer_offset & 0xf) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->internal_format, 26, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->writeback_float_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->writeback_buffer.base, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->writeback_buffer.base, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->writeback_buffer.row_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->writeback_buffer.surface_stride, 0, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->clear.color_0, 0, 31); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->clear.color_1, 0, 31); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->clear.color_2, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->clear.color_3, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_YUV_RENDER_TARGET {
   uint32_t                             internal_buffer_offset;
   bool                                 yuv_enable;
   bool                                 dithered_clear;
   enum mali_color_buffer_internal_format internal_format;
   bool                                 write_enable;
   enum mali_color_format               writeback_format;
   enum mali_block_format               writeback_block_format;
   enum mali_msaa                       writeback_msaa;
   bool                                 srgb;
   bool                                 dithering_enable;
   uint32_t                             swizzle;
   bool                                 clean_tile_write_enable;
   enum mali_yuv_swizzle                yuv_swizzle;
   bool                                 full_range;
   enum mali_yuv_conversion_mode        conversion_mode;
   enum mali_yuv_cr_siting              cr_siting;
   bool                                 unsigned_cr_range;
   uint64_t                             plane_0_base;
   uint64_t                             plane_1_base;
   uint64_t                             plane_2_base;
   uint64_t                             plane_0_stride;
   uint64_t                             plane_1_2_stride;
   struct MALI_RT_CLEAR                 clear;
};

#define MALI_YUV_RENDER_TARGET_header           \
   .yuv_enable = false,  \
   .clear = { MALI_RT_CLEAR_header }

#define MALI_YUV_RENDER_TARGET_LENGTH 64

#define MALI_YUV_RENDER_TARGET_ALIGN 64

struct mali_yuv_render_target_packed { uint32_t opaque[16]; };

#define MALI_YUV_RENDER_TARGET_PACKED_T struct mali_yuv_render_target_packed

#define MALI_YUV_RENDER_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_YUV_RENDER_TARGET_PACKED_T __tmp_packed; \
   assert(((__unpacked)->internal_buffer_offset & 0xf) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->internal_format, 26, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->yuv_swizzle, 16, 18) | \
            util_bitpack_uint((__unpacked)->full_range, 20, 20) | \
            util_bitpack_uint((__unpacked)->conversion_mode, 21, 24) | \
            util_bitpack_uint((__unpacked)->cr_siting, 25, 27) | \
            util_bitpack_uint((__unpacked)->unsigned_cr_range, 28, 28); \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->plane_0_base, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->plane_0_base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->plane_1_base, 0, 63); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->plane_1_base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->plane_2_base, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->plane_2_base, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->plane_0_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->plane_1_2_stride, 0, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->clear.color_0, 0, 31); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->clear.color_1, 0, 31); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->clear.color_2, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->clear.color_3, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_AFBC_RGB_RENDER_TARGET {
   uint32_t                             internal_buffer_offset;
   bool                                 yuv_enable;
   bool                                 dithered_clear;
   enum mali_color_buffer_internal_format internal_format;
   bool                                 write_enable;
   enum mali_color_format               writeback_format;
   enum mali_float_color_format         writeback_float_format;
   enum mali_block_format               writeback_block_format;
   enum mali_msaa                       writeback_msaa;
   bool                                 srgb;
   bool                                 dithering_enable;
   uint32_t                             swizzle;
   bool                                 clean_tile_write_enable;
   bool                                 yuv_transform;
   bool                                 split_block;
   bool                                 wide_block;
   bool                                 reverse_issue_order;
   bool                                 front_buffer;
   bool                                 alpha_hint;
   enum mali_afbc_compression_mode      compression_mode;
   uint64_t                             header;
   uint64_t                             row_stride;
   uint64_t                             body_offset;
   struct MALI_RT_CLEAR                 clear;
};

#define MALI_AFBC_RGB_RENDER_TARGET_header      \
   .yuv_enable = false,  \
   .clear = { MALI_RT_CLEAR_header }

#define MALI_AFBC_RGB_RENDER_TARGET_LENGTH 64

#define MALI_AFBC_RGB_RENDER_TARGET_ALIGN 64

struct mali_afbc_rgb_render_target_packed { uint32_t opaque[16]; };

#define MALI_AFBC_RGB_RENDER_TARGET_PACKED_T struct mali_afbc_rgb_render_target_packed

#define MALI_AFBC_RGB_RENDER_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_AFBC_RGB_RENDER_TARGET_PACKED_T __tmp_packed; \
   assert(((__unpacked)->internal_buffer_offset & 0xf) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->internal_format, 26, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->writeback_float_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->yuv_transform, 0, 0) | \
            util_bitpack_uint((__unpacked)->split_block, 1, 1) | \
            util_bitpack_uint((__unpacked)->wide_block, 2, 2) | \
            util_bitpack_uint((__unpacked)->reverse_issue_order, 3, 3) | \
            util_bitpack_uint((__unpacked)->front_buffer, 4, 4) | \
            util_bitpack_uint((__unpacked)->alpha_hint, 5, 5) | \
            util_bitpack_uint((__unpacked)->compression_mode, 10, 15); \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->header, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->header, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->body_offset, 0, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->clear.color_0, 0, 31); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->clear.color_1, 0, 31); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->clear.color_2, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->clear.color_3, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_AFBC_YUV_RENDER_TARGET {
   uint32_t                             internal_buffer_offset;
   bool                                 yuv_enable;
   bool                                 dithered_clear;
   enum mali_color_buffer_internal_format internal_format;
   bool                                 write_enable;
   enum mali_color_format               writeback_format;
   enum mali_block_format               writeback_block_format;
   enum mali_msaa                       writeback_msaa;
   bool                                 srgb;
   bool                                 dithering_enable;
   uint32_t                             swizzle;
   bool                                 clean_tile_write_enable;
   bool                                 yuv_transform;
   bool                                 split_block;
   bool                                 wide_block;
   bool                                 reverse_issue_order;
   bool                                 front_buffer;
   bool                                 alpha_hint;
   enum mali_afbc_compression_mode      compression_mode;
   enum mali_yuv_swizzle                yuv_swizzle;
   bool                                 full_range;
   enum mali_yuv_conversion_mode        conversion_mode;
   enum mali_yuv_cr_siting              cr_siting;
   bool                                 unsigned_cr_range;
   uint64_t                             header;
   uint64_t                             row_stride;
   uint64_t                             body_offset;
   struct MALI_RT_CLEAR                 clear;
};

#define MALI_AFBC_YUV_RENDER_TARGET_header      \
   .yuv_enable = true,  \
   .clear = { MALI_RT_CLEAR_header }

#define MALI_AFBC_YUV_RENDER_TARGET_LENGTH 64

#define MALI_AFBC_YUV_RENDER_TARGET_ALIGN 64

struct mali_afbc_yuv_render_target_packed { uint32_t opaque[16]; };

#define MALI_AFBC_YUV_RENDER_TARGET_PACKED_T struct mali_afbc_yuv_render_target_packed

#define MALI_AFBC_YUV_RENDER_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_AFBC_YUV_RENDER_TARGET_PACKED_T __tmp_packed; \
   assert(((__unpacked)->internal_buffer_offset & 0xf) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->internal_format, 26, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->yuv_transform, 0, 0) | \
            util_bitpack_uint((__unpacked)->split_block, 1, 1) | \
            util_bitpack_uint((__unpacked)->wide_block, 2, 2) | \
            util_bitpack_uint((__unpacked)->reverse_issue_order, 3, 3) | \
            util_bitpack_uint((__unpacked)->front_buffer, 4, 4) | \
            util_bitpack_uint((__unpacked)->alpha_hint, 5, 5) | \
            util_bitpack_uint((__unpacked)->compression_mode, 10, 15) | \
            util_bitpack_uint((__unpacked)->yuv_swizzle, 16, 18) | \
            util_bitpack_uint((__unpacked)->full_range, 20, 20) | \
            util_bitpack_uint((__unpacked)->conversion_mode, 21, 24) | \
            util_bitpack_uint((__unpacked)->cr_siting, 25, 27) | \
            util_bitpack_uint((__unpacked)->unsigned_cr_range, 28, 28); \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->header, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->header, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->row_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->body_offset, 0, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->clear.color_0, 0, 31); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->clear.color_1, 0, 31); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->clear.color_2, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->clear.color_3, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_RENDER_TARGET {
   struct MALI_RGB_RENDER_TARGET        rgb;
   struct MALI_YUV_RENDER_TARGET        yuv;
   struct MALI_AFBC_RGB_RENDER_TARGET   afbc_rgb;
   struct MALI_AFBC_YUV_RENDER_TARGET   afbc_yuv;
};

#define MALI_RENDER_TARGET_header               \
   .rgb = { MALI_RGB_RENDER_TARGET_header },  \
   .yuv = { MALI_YUV_RENDER_TARGET_header },  \
   .afbc_rgb = { MALI_AFBC_RGB_RENDER_TARGET_header },  \
   .afbc_yuv = { MALI_AFBC_YUV_RENDER_TARGET_header }

#define MALI_RENDER_TARGET_LENGTH 64

#define MALI_RENDER_TARGET_ALIGN 64

struct mali_render_target_packed { uint32_t opaque[16]; };

#define MALI_RENDER_TARGET_PACKED_T struct mali_render_target_packed

#define MALI_RENDER_TARGET_pack(__packed, __unpacked) \
do { \
   MALI_RENDER_TARGET_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->rgb.internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->rgb.yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->rgb.dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->rgb.internal_format, 26, 31) | \
            util_bitpack_uint((__unpacked)->yuv.internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->yuv.yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->yuv.dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->yuv.internal_format, 26, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.internal_format, 26, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.internal_buffer_offset >> 4, 4, 15) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.yuv_enable, 24, 24) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.dithered_clear, 25, 25) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.internal_format, 26, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->rgb.write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->rgb.writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->rgb.writeback_float_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->rgb.writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->rgb.writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->rgb.srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->rgb.dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->rgb.swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->rgb.clean_tile_write_enable, 31, 31) | \
            util_bitpack_uint((__unpacked)->yuv.write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->yuv.writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->yuv.writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->yuv.writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->yuv.srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->yuv.dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->yuv.swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->yuv.clean_tile_write_enable, 31, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.writeback_float_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.clean_tile_write_enable, 31, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.write_enable, 0, 0) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.writeback_format, 3, 7) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.writeback_block_format, 8, 11) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.writeback_msaa, 12, 13) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.srgb, 14, 14) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.dithering_enable, 15, 15) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.swizzle, 16, 27) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.clean_tile_write_enable, 31, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->yuv.yuv_swizzle, 16, 18) | \
            util_bitpack_uint((__unpacked)->yuv.full_range, 20, 20) | \
            util_bitpack_uint((__unpacked)->yuv.conversion_mode, 21, 24) | \
            util_bitpack_uint((__unpacked)->yuv.cr_siting, 25, 27) | \
            util_bitpack_uint((__unpacked)->yuv.unsigned_cr_range, 28, 28) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.yuv_transform, 0, 0) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.split_block, 1, 1) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.wide_block, 2, 2) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.reverse_issue_order, 3, 3) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.front_buffer, 4, 4) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.alpha_hint, 5, 5) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.compression_mode, 10, 15) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.yuv_transform, 0, 0) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.split_block, 1, 1) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.wide_block, 2, 2) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.reverse_issue_order, 3, 3) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.front_buffer, 4, 4) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.alpha_hint, 5, 5) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.compression_mode, 10, 15) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.yuv_swizzle, 16, 18) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.full_range, 20, 20) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.conversion_mode, 21, 24) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.cr_siting, 25, 27) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.unsigned_cr_range, 28, 28); \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->yuv.plane_0_base, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->yuv.plane_0_base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->yuv.plane_1_base, 0, 63); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->yuv.plane_1_base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->rgb.writeback_buffer.base, 0, 63) | \
            util_bitpack_uint((__unpacked)->yuv.plane_2_base, 0, 63) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.header, 0, 63) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.header, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->rgb.writeback_buffer.base, 0, 63) >> 32 | \
            util_bitpack_uint((__unpacked)->yuv.plane_2_base, 0, 63) >> 32 | \
            util_bitpack_uint((__unpacked)->afbc_rgb.header, 0, 63) >> 32 | \
            util_bitpack_uint((__unpacked)->afbc_yuv.header, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->rgb.writeback_buffer.row_stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->yuv.plane_0_stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.row_stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.row_stride, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->rgb.writeback_buffer.surface_stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->yuv.plane_1_2_stride, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.body_offset, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.body_offset, 0, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->rgb.clear.color_0, 0, 31) | \
            util_bitpack_uint((__unpacked)->yuv.clear.color_0, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.clear.color_0, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.clear.color_0, 0, 31); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->rgb.clear.color_1, 0, 31) | \
            util_bitpack_uint((__unpacked)->yuv.clear.color_1, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.clear.color_1, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.clear.color_1, 0, 31); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->rgb.clear.color_2, 0, 31) | \
            util_bitpack_uint((__unpacked)->yuv.clear.color_2, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.clear.color_2, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.clear.color_2, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->rgb.clear.color_3, 0, 31) | \
            util_bitpack_uint((__unpacked)->yuv.clear.color_3, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_rgb.clear.color_3, 0, 31) | \
            util_bitpack_uint((__unpacked)->afbc_yuv.clear.color_3, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

enum mali_chunk_size {
        MALI_CHUNK_SIZE_256_KIB              =      0,
        MALI_CHUNK_SIZE_512_KIB              =      1,
        MALI_CHUNK_SIZE_1_MIB                =      2,
        MALI_CHUNK_SIZE_2_MIB                =      3,
};

enum mali_chunk_partition {
        MALI_CHUNK_PARTITION_DYNAMIC         =      0,
        MALI_CHUNK_PARTITION_STATIC_25       =      1,
        MALI_CHUNK_PARTITION_STATIC_50       =      2,
        MALI_CHUNK_PARTITION_STATIC_75       =      3,
};

struct MALI_TILER_HEAP {
   enum mali_descriptor_type            type;
   enum mali_buffer_type                buffer_type;
   enum mali_chunk_size                 chunk_size;
   enum mali_chunk_partition            partitioning;
   uint64_t                             size;
   uint64_t                             base;
   uint64_t                             bottom;
   uint64_t                             top;
};

#define MALI_TILER_HEAP_header                  \
   .type = MALI_DESCRIPTOR_TYPE_BUFFER,  \
   .buffer_type = MALI_BUFFER_TYPE_TILER_HEAP,  \
   .chunk_size = MALI_CHUNK_SIZE_2_MIB,  \
   .partitioning = MALI_CHUNK_PARTITION_DYNAMIC

#define MALI_TILER_HEAP_LENGTH 32

#define MALI_TILER_HEAP_ALIGN 32

struct mali_tiler_heap_packed { uint32_t opaque[8]; };

#define MALI_TILER_HEAP_PACKED_T struct mali_tiler_heap_packed

#define MALI_TILER_HEAP_pack(__packed, __unpacked) \
do { \
   MALI_TILER_HEAP_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->buffer_type, 4, 7) | \
            util_bitpack_uint((__unpacked)->chunk_size, 8, 9) | \
            util_bitpack_uint((__unpacked)->partitioning, 10, 11); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint(ALIGN_POT((__unpacked)->size, 4096), 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->base, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->base, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->bottom, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->bottom, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->top, 0, 63); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->top, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_TILER_WEIGHTS {
   uint32_t                             weight0;
   uint32_t                             weight1;
   uint32_t                             weight2;
   uint32_t                             weight3;
   uint32_t                             weight4;
   uint32_t                             weight5;
   uint32_t                             weight6;
   uint32_t                             weight7;
};

#define MALI_TILER_WEIGHTS_header               \
   0

#define MALI_TILER_WEIGHTS_LENGTH 32

struct mali_tiler_weights_packed { uint32_t opaque[8]; };

#define MALI_TILER_WEIGHTS_PACKED_T struct mali_tiler_weights_packed

#define MALI_TILER_WEIGHTS_pack(__packed, __unpacked) \
do { \
   MALI_TILER_WEIGHTS_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->weight0, 16, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->weight1, 16, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->weight2, 16, 31); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->weight3, 16, 31); \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->weight4, 16, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->weight5, 16, 31); \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->weight6, 16, 31); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->weight7, 16, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_TILER_STATE {
   uint64_t                             word0;
   uint64_t                             word1;
   uint64_t                             word2;
   uint64_t                             word3;
   uint64_t                             word4;
   uint64_t                             word5;
   uint64_t                             word6;
   uint64_t                             word7;
   uint64_t                             word8;
   uint64_t                             word9;
   uint64_t                             word10;
   uint64_t                             word11;
   uint64_t                             word12;
   uint64_t                             word13;
   uint64_t                             word14;
   uint64_t                             word15;
};

#define MALI_TILER_STATE_header                 \
   0

#define MALI_TILER_STATE_LENGTH 64

struct mali_tiler_state_packed { uint32_t opaque[16]; };

#define MALI_TILER_STATE_PACKED_T struct mali_tiler_state_packed

#define MALI_TILER_STATE_pack(__packed, __unpacked) \
do { \
   MALI_TILER_STATE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->word0, 0, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->word1, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->word2, 0, 31); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->word3, 0, 31); \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->word4, 0, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->word5, 0, 31); \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->word6, 0, 31); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->word7, 0, 31); \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->word8, 0, 31); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->word9, 0, 31); \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->word10, 0, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->word11, 0, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->word12, 0, 31); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->word13, 0, 31); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->word14, 0, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->word15, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_TILER_CONTEXT {
   uint64_t                             polygon_list;
   uint32_t                             hierarchy_mask;
   enum mali_sample_pattern             sample_pattern;
   bool                                 update_cost_table;
   bool                                 sample_test_disable;
   bool                                 first_provoking_vertex;
   uint32_t                             fb_width;
   uint32_t                             fb_height;
   uint32_t                             layer_count;
   int32_t                              layer_offset;
   uint64_t                             heap;
   struct MALI_TILER_WEIGHTS            weights;
   struct MALI_TILER_STATE              state;
};

#define MALI_TILER_CONTEXT_header               \
   .layer_count = 1,  \
   .weights = { MALI_TILER_WEIGHTS_header },  \
   .state = { MALI_TILER_STATE_header }

#define MALI_TILER_CONTEXT_LENGTH 192

#define MALI_TILER_CONTEXT_ALIGN 64

struct mali_tiler_context_packed { uint32_t opaque[48]; };

#define MALI_TILER_CONTEXT_PACKED_T struct mali_tiler_context_packed

#define MALI_TILER_CONTEXT_pack(__packed, __unpacked) \
do { \
   MALI_TILER_CONTEXT_PACKED_T __tmp_packed; \
   assert((__unpacked)->fb_width >= 1); \
   assert((__unpacked)->fb_height >= 1); \
   assert((__unpacked)->layer_count >= 1); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->polygon_list, 0, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->polygon_list, 0, 63) >> 32; \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->hierarchy_mask, 0, 12) | \
            util_bitpack_uint((__unpacked)->sample_pattern, 13, 15) | \
            util_bitpack_uint((__unpacked)->update_cost_table, 16, 16) | \
            util_bitpack_uint((__unpacked)->sample_test_disable, 17, 17) | \
            util_bitpack_uint((__unpacked)->first_provoking_vertex, 18, 18); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->fb_width - 1, 0, 15) | \
            util_bitpack_uint((__unpacked)->fb_height - 1, 16, 31); \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->layer_count - 1, 0, 7) | \
            util_bitpack_sint((__unpacked)->layer_offset, 8, 16); \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->heap, 0, 63); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->heap, 0, 63) >> 32; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->weights.weight0, 16, 31); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->weights.weight1, 16, 31); \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->weights.weight2, 16, 31); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->weights.weight3, 16, 31); \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->weights.weight4, 16, 31); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->weights.weight5, 16, 31); \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->weights.weight6, 16, 31); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->weights.weight7, 16, 31); \
   __tmp_packed.opaque[16] = 0; \
   __tmp_packed.opaque[17] = 0; \
   __tmp_packed.opaque[18] = 0; \
   __tmp_packed.opaque[19] = 0; \
   __tmp_packed.opaque[20] = 0; \
   __tmp_packed.opaque[21] = 0; \
   __tmp_packed.opaque[22] = 0; \
   __tmp_packed.opaque[23] = 0; \
   __tmp_packed.opaque[24] = 0; \
   __tmp_packed.opaque[25] = 0; \
   __tmp_packed.opaque[26] = 0; \
   __tmp_packed.opaque[27] = 0; \
   __tmp_packed.opaque[28] = 0; \
   __tmp_packed.opaque[29] = 0; \
   __tmp_packed.opaque[30] = 0; \
   __tmp_packed.opaque[31] = 0; \
   __tmp_packed.opaque[32] = util_bitpack_uint((__unpacked)->state.word0, 0, 31); \
   __tmp_packed.opaque[33] = util_bitpack_uint((__unpacked)->state.word1, 0, 31); \
   __tmp_packed.opaque[34] = util_bitpack_uint((__unpacked)->state.word2, 0, 31); \
   __tmp_packed.opaque[35] = util_bitpack_uint((__unpacked)->state.word3, 0, 31); \
   __tmp_packed.opaque[36] = util_bitpack_uint((__unpacked)->state.word4, 0, 31); \
   __tmp_packed.opaque[37] = util_bitpack_uint((__unpacked)->state.word5, 0, 31); \
   __tmp_packed.opaque[38] = util_bitpack_uint((__unpacked)->state.word6, 0, 31); \
   __tmp_packed.opaque[39] = util_bitpack_uint((__unpacked)->state.word7, 0, 31); \
   __tmp_packed.opaque[40] = util_bitpack_uint((__unpacked)->state.word8, 0, 31); \
   __tmp_packed.opaque[41] = util_bitpack_uint((__unpacked)->state.word9, 0, 31); \
   __tmp_packed.opaque[42] = util_bitpack_uint((__unpacked)->state.word10, 0, 31); \
   __tmp_packed.opaque[43] = util_bitpack_uint((__unpacked)->state.word11, 0, 31); \
   __tmp_packed.opaque[44] = util_bitpack_uint((__unpacked)->state.word12, 0, 31); \
   __tmp_packed.opaque[45] = util_bitpack_uint((__unpacked)->state.word13, 0, 31); \
   __tmp_packed.opaque[46] = util_bitpack_uint((__unpacked)->state.word14, 0, 31); \
   __tmp_packed.opaque[47] = util_bitpack_uint((__unpacked)->state.word15, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_FRAMEBUFFER_PADDING {
   int dummy;
};

#define MALI_FRAMEBUFFER_PADDING_header         \
   0

#define MALI_FRAMEBUFFER_PADDING_LENGTH 64

struct mali_framebuffer_padding_packed { uint32_t opaque[16]; };

#define MALI_FRAMEBUFFER_PADDING_PACKED_T struct mali_framebuffer_padding_packed

#define MALI_FRAMEBUFFER_PADDING_pack(__packed, __unpacked) \
do { \
   MALI_FRAMEBUFFER_PADDING_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = 0; \
   __tmp_packed.opaque[ 1] = 0; \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = 0; \
   __tmp_packed.opaque[ 9] = 0; \
   __tmp_packed.opaque[10] = 0; \
   __tmp_packed.opaque[11] = 0; \
   __tmp_packed.opaque[12] = 0; \
   __tmp_packed.opaque[13] = 0; \
   __tmp_packed.opaque[14] = 0; \
   __tmp_packed.opaque[15] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct mali_framebuffer_packed {
   union {
      uint32_t opaque[32];
      struct {
         struct mali_framebuffer_parameters_packed PARAMETERS;
         struct mali_framebuffer_padding_packed PADDING;
      };
   };
};

#define MALI_FRAMEBUFFER_PACKED_T struct mali_framebuffer_packed

#define MALI_FRAMEBUFFER_LENGTH 128

#define MALI_FRAMEBUFFER_ALIGN 64

#define MALI_FRAMEBUFFER_SECTION_PARAMETERS_TYPE struct MALI_FRAMEBUFFER_PARAMETERS

#define MALI_FRAMEBUFFER_SECTION_PARAMETERS_PACKED_TYPE MALI_FRAMEBUFFER_PARAMETERS_PACKED_T

#define MALI_FRAMEBUFFER_SECTION_PARAMETERS_header MALI_FRAMEBUFFER_PARAMETERS_header

#define MALI_FRAMEBUFFER_SECTION_PARAMETERS_pack MALI_FRAMEBUFFER_PARAMETERS_pack

#define MALI_FRAMEBUFFER_SECTION_PARAMETERS_OFFSET 0

#define MALI_FRAMEBUFFER_SECTION_PADDING_TYPE struct MALI_FRAMEBUFFER_PADDING

#define MALI_FRAMEBUFFER_SECTION_PADDING_PACKED_TYPE MALI_FRAMEBUFFER_PADDING_PACKED_T

#define MALI_FRAMEBUFFER_SECTION_PADDING_header MALI_FRAMEBUFFER_PADDING_header

#define MALI_FRAMEBUFFER_SECTION_PADDING_pack MALI_FRAMEBUFFER_PADDING_pack

#define MALI_FRAMEBUFFER_SECTION_PADDING_OFFSET 64

struct MALI_FRAMEBUFFER_POINTER {
   bool                                 type;
   bool                                 zs_crc_extension_present;
   uint32_t                             render_target_count;
   uint64_t                             pointer;
};

#define MALI_FRAMEBUFFER_POINTER_header         \
   .type = true

#define MALI_FRAMEBUFFER_POINTER_LENGTH 8

struct mali_framebuffer_pointer_packed { uint32_t opaque[2]; };

#define MALI_FRAMEBUFFER_POINTER_PACKED_T struct mali_framebuffer_pointer_packed

#define MALI_FRAMEBUFFER_POINTER_pack(__packed, __unpacked) \
do { \
   MALI_FRAMEBUFFER_POINTER_PACKED_T __tmp_packed; \
   assert((__unpacked)->render_target_count >= 1); \
   assert(((__unpacked)->pointer & 0x3f) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 0) | \
            util_bitpack_uint((__unpacked)->zs_crc_extension_present, 1, 1) | \
            util_bitpack_uint((__unpacked)->render_target_count - 1, 2, 5) | \
            util_bitpack_uint((__unpacked)->pointer >> 6, 6, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->pointer >> 6, 6, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_JOB_HEADER {
   uint64_t                             exception_status;
   uint64_t                             first_incomplete_task;
   uint64_t                             fault_pointer;
   enum mali_job_type                   type;
   bool                                 barrier;
   bool                                 suppress_prefetch;
   bool                                 relax_dependency_1;
   bool                                 relax_dependency_2;
   uint32_t                             index;
   uint32_t                             dependency_1;
   uint32_t                             dependency_2;
   uint64_t                             next;
};

#define MALI_JOB_HEADER_header                  \
   0

#define MALI_JOB_HEADER_LENGTH 32

#define MALI_JOB_HEADER_ALIGN 128

struct mali_job_header_packed { uint32_t opaque[8]; };

#define MALI_JOB_HEADER_PACKED_T struct mali_job_header_packed

#define MALI_JOB_HEADER_pack(__packed, __unpacked) \
do { \
   MALI_JOB_HEADER_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->exception_status, 0, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->first_incomplete_task, 0, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->fault_pointer, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->fault_pointer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->type, 1, 7) | \
            util_bitpack_uint((__unpacked)->barrier, 8, 8) | \
            util_bitpack_uint((__unpacked)->suppress_prefetch, 11, 11) | \
            util_bitpack_uint((__unpacked)->relax_dependency_1, 14, 14) | \
            util_bitpack_uint((__unpacked)->relax_dependency_2, 15, 15) | \
            util_bitpack_uint((__unpacked)->index, 16, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->dependency_1, 0, 15) | \
            util_bitpack_uint((__unpacked)->dependency_2, 16, 31); \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->next, 0, 63); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->next, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_FRAGMENT_JOB_PAYLOAD {
   uint32_t                             bound_min_x;
   uint32_t                             bound_min_y;
   uint32_t                             bound_max_x;
   uint32_t                             bound_max_y;
   enum mali_tile_render_order          tile_render_order;
   bool                                 has_tile_enable_map;
   uint64_t                             framebuffer;
   uint64_t                             tile_enable_map;
   uint32_t                             tile_enable_map_row_stride;
};

#define MALI_FRAGMENT_JOB_PAYLOAD_header        \
   0

#define MALI_FRAGMENT_JOB_PAYLOAD_LENGTH 32

struct mali_fragment_job_payload_packed { uint32_t opaque[8]; };

#define MALI_FRAGMENT_JOB_PAYLOAD_PACKED_T struct mali_fragment_job_payload_packed

#define MALI_FRAGMENT_JOB_PAYLOAD_pack(__packed, __unpacked) \
do { \
   MALI_FRAGMENT_JOB_PAYLOAD_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->bound_min_x, 0, 11) | \
            util_bitpack_uint((__unpacked)->bound_min_y, 16, 27); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->bound_max_x, 0, 11) | \
            util_bitpack_uint((__unpacked)->bound_max_y, 16, 27) | \
            util_bitpack_uint((__unpacked)->tile_render_order, 28, 30) | \
            util_bitpack_uint((__unpacked)->has_tile_enable_map, 31, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->framebuffer, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->framebuffer, 0, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->tile_enable_map, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->tile_enable_map, 0, 63) >> 32; \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->tile_enable_map_row_stride, 0, 7); \
   __tmp_packed.opaque[ 7] = 0; \
   *(__packed) = __tmp_packed; \
} while (0);

struct mali_fragment_job_packed {
   union {
      uint32_t opaque[16];
      struct {
         struct mali_job_header_packed HEADER;
         struct mali_fragment_job_payload_packed PAYLOAD;
      };
   };
};

#define MALI_FRAGMENT_JOB_PACKED_T struct mali_fragment_job_packed

#define MALI_FRAGMENT_JOB_LENGTH 64

#define MALI_FRAGMENT_JOB_ALIGN 128

#define MALI_FRAGMENT_JOB_SECTION_HEADER_TYPE struct MALI_JOB_HEADER

#define MALI_FRAGMENT_JOB_SECTION_HEADER_PACKED_TYPE MALI_JOB_HEADER_PACKED_T

#define MALI_FRAGMENT_JOB_SECTION_HEADER_header MALI_JOB_HEADER_header

#define MALI_FRAGMENT_JOB_SECTION_HEADER_pack MALI_JOB_HEADER_pack

#define MALI_FRAGMENT_JOB_SECTION_HEADER_OFFSET 0

#define MALI_FRAGMENT_JOB_SECTION_PAYLOAD_TYPE struct MALI_FRAGMENT_JOB_PAYLOAD

#define MALI_FRAGMENT_JOB_SECTION_PAYLOAD_PACKED_TYPE MALI_FRAGMENT_JOB_PAYLOAD_PACKED_T

#define MALI_FRAGMENT_JOB_SECTION_PAYLOAD_header MALI_FRAGMENT_JOB_PAYLOAD_header

#define MALI_FRAGMENT_JOB_SECTION_PAYLOAD_pack MALI_FRAGMENT_JOB_PAYLOAD_pack

#define MALI_FRAGMENT_JOB_SECTION_PAYLOAD_OFFSET 32

enum mali_write_value_type {
        MALI_WRITE_VALUE_TYPE_CYCLE_COUNTER  =      1,
        MALI_WRITE_VALUE_TYPE_SYSTEM_TIMESTAMP =      2,
        MALI_WRITE_VALUE_TYPE_ZERO           =      3,
        MALI_WRITE_VALUE_TYPE_IMMEDIATE_8    =      4,
        MALI_WRITE_VALUE_TYPE_IMMEDIATE_16   =      5,
        MALI_WRITE_VALUE_TYPE_IMMEDIATE_32   =      6,
        MALI_WRITE_VALUE_TYPE_IMMEDIATE_64   =      7,
};

struct MALI_WRITE_VALUE_JOB_PAYLOAD {
   uint64_t                             address;
   enum mali_write_value_type           type;
   uint64_t                             immediate_value;
};

#define MALI_WRITE_VALUE_JOB_PAYLOAD_header     \
   0

#define MALI_WRITE_VALUE_JOB_PAYLOAD_LENGTH 24

struct mali_write_value_job_payload_packed { uint32_t opaque[6]; };

#define MALI_WRITE_VALUE_JOB_PAYLOAD_PACKED_T struct mali_write_value_job_payload_packed

#define MALI_WRITE_VALUE_JOB_PAYLOAD_pack(__packed, __unpacked) \
do { \
   MALI_WRITE_VALUE_JOB_PAYLOAD_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->address, 0, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->address, 0, 63) >> 32; \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->type, 0, 31); \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->immediate_value, 0, 63); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->immediate_value, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_CACHE_FLUSH_JOB_PAYLOAD {
   bool                                 clean_shader_core_ls;
   bool                                 invalidate_shader_core_ls;
   bool                                 invalidate_shader_core_other;
   bool                                 job_manager_clean;
   bool                                 job_manager_invalidate;
   bool                                 tiler_clean;
   bool                                 tiler_invalidate;
   bool                                 l2_clean;
   bool                                 l2_invalidate;
};

#define MALI_CACHE_FLUSH_JOB_PAYLOAD_header     \
   0

#define MALI_CACHE_FLUSH_JOB_PAYLOAD_LENGTH 8

struct mali_cache_flush_job_payload_packed { uint32_t opaque[2]; };

#define MALI_CACHE_FLUSH_JOB_PAYLOAD_PACKED_T struct mali_cache_flush_job_payload_packed

#define MALI_CACHE_FLUSH_JOB_PAYLOAD_pack(__packed, __unpacked) \
do { \
   MALI_CACHE_FLUSH_JOB_PAYLOAD_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->clean_shader_core_ls, 0, 0) | \
            util_bitpack_uint((__unpacked)->invalidate_shader_core_ls, 1, 1) | \
            util_bitpack_uint((__unpacked)->invalidate_shader_core_other, 2, 2) | \
            util_bitpack_uint((__unpacked)->job_manager_clean, 16, 16) | \
            util_bitpack_uint((__unpacked)->job_manager_invalidate, 17, 17) | \
            util_bitpack_uint((__unpacked)->tiler_clean, 24, 24) | \
            util_bitpack_uint((__unpacked)->tiler_invalidate, 25, 25); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->l2_clean, 0, 0) | \
            util_bitpack_uint((__unpacked)->l2_invalidate, 1, 1); \
   *(__packed) = __tmp_packed; \
} while (0);

struct mali_write_value_job_packed {
   union {
      uint32_t opaque[14];
      struct {
         struct mali_job_header_packed HEADER;
         struct mali_write_value_job_payload_packed PAYLOAD;
      };
   };
};

#define MALI_WRITE_VALUE_JOB_PACKED_T struct mali_write_value_job_packed

#define MALI_WRITE_VALUE_JOB_LENGTH 56

#define MALI_WRITE_VALUE_JOB_ALIGN 128

#define MALI_WRITE_VALUE_JOB_SECTION_HEADER_TYPE struct MALI_JOB_HEADER

#define MALI_WRITE_VALUE_JOB_SECTION_HEADER_PACKED_TYPE MALI_JOB_HEADER_PACKED_T

#define MALI_WRITE_VALUE_JOB_SECTION_HEADER_header MALI_JOB_HEADER_header

#define MALI_WRITE_VALUE_JOB_SECTION_HEADER_pack MALI_JOB_HEADER_pack

#define MALI_WRITE_VALUE_JOB_SECTION_HEADER_OFFSET 0

#define MALI_WRITE_VALUE_JOB_SECTION_PAYLOAD_TYPE struct MALI_WRITE_VALUE_JOB_PAYLOAD

#define MALI_WRITE_VALUE_JOB_SECTION_PAYLOAD_PACKED_TYPE MALI_WRITE_VALUE_JOB_PAYLOAD_PACKED_T

#define MALI_WRITE_VALUE_JOB_SECTION_PAYLOAD_header MALI_WRITE_VALUE_JOB_PAYLOAD_header

#define MALI_WRITE_VALUE_JOB_SECTION_PAYLOAD_pack MALI_WRITE_VALUE_JOB_PAYLOAD_pack

#define MALI_WRITE_VALUE_JOB_SECTION_PAYLOAD_OFFSET 32

struct mali_cache_flush_job_packed {
   union {
      uint32_t opaque[10];
      struct {
         struct mali_job_header_packed HEADER;
         struct mali_cache_flush_job_payload_packed PAYLOAD;
      };
   };
};

#define MALI_CACHE_FLUSH_JOB_PACKED_T struct mali_cache_flush_job_packed

#define MALI_CACHE_FLUSH_JOB_LENGTH 40

#define MALI_CACHE_FLUSH_JOB_ALIGN 128

#define MALI_CACHE_FLUSH_JOB_SECTION_HEADER_TYPE struct MALI_JOB_HEADER

#define MALI_CACHE_FLUSH_JOB_SECTION_HEADER_PACKED_TYPE MALI_JOB_HEADER_PACKED_T

#define MALI_CACHE_FLUSH_JOB_SECTION_HEADER_header MALI_JOB_HEADER_header

#define MALI_CACHE_FLUSH_JOB_SECTION_HEADER_pack MALI_JOB_HEADER_pack

#define MALI_CACHE_FLUSH_JOB_SECTION_HEADER_OFFSET 0

#define MALI_CACHE_FLUSH_JOB_SECTION_PAYLOAD_TYPE struct MALI_CACHE_FLUSH_JOB_PAYLOAD

#define MALI_CACHE_FLUSH_JOB_SECTION_PAYLOAD_PACKED_TYPE MALI_CACHE_FLUSH_JOB_PAYLOAD_PACKED_T

#define MALI_CACHE_FLUSH_JOB_SECTION_PAYLOAD_header MALI_CACHE_FLUSH_JOB_PAYLOAD_header

#define MALI_CACHE_FLUSH_JOB_SECTION_PAYLOAD_pack MALI_CACHE_FLUSH_JOB_PAYLOAD_pack

#define MALI_CACHE_FLUSH_JOB_SECTION_PAYLOAD_OFFSET 32

struct MALI_SHADER_ENVIRONMENT {
   uint64_t                             attribute_offset;
   uint32_t                             fau_count;
   uint64_t                             resources;
   uint64_t                             shader;
   uint64_t                             thread_storage;
   uint64_t                             fau;
#define MALI_RESOURCE_TABLE_SIZE_ALIGNMENT       4
};

#define MALI_RESOURCE_TABLE_SIZE_ALIGNMENT       4

#define MALI_SHADER_ENVIRONMENT_header          \
   0

#define MALI_SHADER_ENVIRONMENT_LENGTH 64

#define MALI_SHADER_ENVIRONMENT_ALIGN 64

struct mali_shader_environment_packed { uint32_t opaque[16]; };

#define MALI_SHADER_ENVIRONMENT_PACKED_T struct mali_shader_environment_packed

#define MALI_SHADER_ENVIRONMENT_pack(__packed, __unpacked) \
do { \
   MALI_SHADER_ENVIRONMENT_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->attribute_offset, 0, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->fau_count, 0, 7); \
   __tmp_packed.opaque[ 2] = 0; \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = 0; \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = 0; \
   __tmp_packed.opaque[ 7] = 0; \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->resources, 0, 63); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->resources, 0, 63) >> 32; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->shader, 0, 63); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->shader, 0, 63) >> 32; \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->thread_storage, 0, 63); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->thread_storage, 0, 63) >> 32; \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->fau, 0, 63); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->fau, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_COMPUTE_PAYLOAD {
   uint32_t                             workgroup_size_x;
   uint32_t                             workgroup_size_y;
   uint32_t                             workgroup_size_z;
   bool                                 allow_merging_workgroups;
   uint32_t                             task_increment;
   enum mali_task_axis                  task_axis;
   uint64_t                             workgroup_count_x;
   uint64_t                             workgroup_count_y;
   uint64_t                             workgroup_count_z;
   uint64_t                             offset_x;
   uint64_t                             offset_y;
   uint64_t                             offset_z;
   struct MALI_SHADER_ENVIRONMENT       compute;
};

#define MALI_COMPUTE_PAYLOAD_header             \
   .task_increment = 1,  \
   .compute = { MALI_SHADER_ENVIRONMENT_header }

#define MALI_COMPUTE_PAYLOAD_LENGTH 96

struct mali_compute_payload_packed { uint32_t opaque[24]; };

#define MALI_COMPUTE_PAYLOAD_PACKED_T struct mali_compute_payload_packed

#define MALI_COMPUTE_PAYLOAD_pack(__packed, __unpacked) \
do { \
   MALI_COMPUTE_PAYLOAD_PACKED_T __tmp_packed; \
   assert((__unpacked)->workgroup_size_x >= 1); \
   assert((__unpacked)->workgroup_size_y >= 1); \
   assert((__unpacked)->workgroup_size_z >= 1); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->workgroup_size_x - 1, 0, 9) | \
            util_bitpack_uint((__unpacked)->workgroup_size_y - 1, 10, 19) | \
            util_bitpack_uint((__unpacked)->workgroup_size_z - 1, 20, 29) | \
            util_bitpack_uint((__unpacked)->allow_merging_workgroups, 31, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->task_increment, 0, 13) | \
            util_bitpack_uint((__unpacked)->task_axis, 14, 15); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->workgroup_count_x, 0, 31); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->workgroup_count_y, 0, 31); \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->workgroup_count_z, 0, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_uint((__unpacked)->offset_x, 0, 31); \
   __tmp_packed.opaque[ 6] = util_bitpack_uint((__unpacked)->offset_y, 0, 31); \
   __tmp_packed.opaque[ 7] = util_bitpack_uint((__unpacked)->offset_z, 0, 31); \
   __tmp_packed.opaque[ 8] = util_bitpack_uint((__unpacked)->compute.attribute_offset, 0, 31); \
   __tmp_packed.opaque[ 9] = util_bitpack_uint((__unpacked)->compute.fau_count, 0, 7); \
   __tmp_packed.opaque[10] = 0; \
   __tmp_packed.opaque[11] = 0; \
   __tmp_packed.opaque[12] = 0; \
   __tmp_packed.opaque[13] = 0; \
   __tmp_packed.opaque[14] = 0; \
   __tmp_packed.opaque[15] = 0; \
   __tmp_packed.opaque[16] = util_bitpack_uint((__unpacked)->compute.resources, 0, 63); \
   __tmp_packed.opaque[17] = util_bitpack_uint((__unpacked)->compute.resources, 0, 63) >> 32; \
   __tmp_packed.opaque[18] = util_bitpack_uint((__unpacked)->compute.shader, 0, 63); \
   __tmp_packed.opaque[19] = util_bitpack_uint((__unpacked)->compute.shader, 0, 63) >> 32; \
   __tmp_packed.opaque[20] = util_bitpack_uint((__unpacked)->compute.thread_storage, 0, 63); \
   __tmp_packed.opaque[21] = util_bitpack_uint((__unpacked)->compute.thread_storage, 0, 63) >> 32; \
   __tmp_packed.opaque[22] = util_bitpack_uint((__unpacked)->compute.fau, 0, 63); \
   __tmp_packed.opaque[23] = util_bitpack_uint((__unpacked)->compute.fau, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct mali_compute_job_packed {
   union {
      uint32_t opaque[32];
      struct {
         struct mali_job_header_packed HEADER;
         struct mali_compute_payload_packed PAYLOAD;
      };
   };
};

#define MALI_COMPUTE_JOB_PACKED_T struct mali_compute_job_packed

#define MALI_COMPUTE_JOB_LENGTH 128

#define MALI_COMPUTE_JOB_ALIGN 128

#define MALI_COMPUTE_JOB_SECTION_HEADER_TYPE struct MALI_JOB_HEADER

#define MALI_COMPUTE_JOB_SECTION_HEADER_PACKED_TYPE MALI_JOB_HEADER_PACKED_T

#define MALI_COMPUTE_JOB_SECTION_HEADER_header MALI_JOB_HEADER_header

#define MALI_COMPUTE_JOB_SECTION_HEADER_pack MALI_JOB_HEADER_pack

#define MALI_COMPUTE_JOB_SECTION_HEADER_OFFSET 0

#define MALI_COMPUTE_JOB_SECTION_PAYLOAD_TYPE struct MALI_COMPUTE_PAYLOAD

#define MALI_COMPUTE_JOB_SECTION_PAYLOAD_PACKED_TYPE MALI_COMPUTE_PAYLOAD_PACKED_T

#define MALI_COMPUTE_JOB_SECTION_PAYLOAD_header MALI_COMPUTE_PAYLOAD_header

#define MALI_COMPUTE_JOB_SECTION_PAYLOAD_pack MALI_COMPUTE_PAYLOAD_pack

#define MALI_COMPUTE_JOB_SECTION_PAYLOAD_OFFSET 32

struct MALI_RESOURCE {
   uint64_t                             address;
   bool                                 contains_descriptors;
   uint64_t                             size;
};

#define MALI_RESOURCE_header                    \
   .contains_descriptors = true

#define MALI_RESOURCE_LENGTH 16

#define MALI_RESOURCE_ALIGN 64

struct mali_resource_packed { uint32_t opaque[4]; };

#define MALI_RESOURCE_PACKED_T struct mali_resource_packed

#define MALI_RESOURCE_pack(__packed, __unpacked) \
do { \
   MALI_RESOURCE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->address, 0, 55); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->address, 0, 55) >> 32 | \
            util_bitpack_uint((__unpacked)->contains_descriptors, 24, 24); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->size, 0, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->size, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_DEPTH_STENCIL {
   enum mali_descriptor_type            type;
   enum mali_func                       front_compare_function;
   enum mali_stencil_op                 front_stencil_fail;
   enum mali_stencil_op                 front_depth_fail;
   enum mali_stencil_op                 front_depth_pass;
   enum mali_func                       back_compare_function;
   enum mali_stencil_op                 back_stencil_fail;
   enum mali_stencil_op                 back_depth_fail;
   enum mali_stencil_op                 back_depth_pass;
   bool                                 stencil_from_shader;
   bool                                 stencil_test_enable;
   uint32_t                             front_write_mask;
   uint32_t                             back_write_mask;
   uint32_t                             front_value_mask;
   uint32_t                             back_value_mask;
   uint32_t                             front_reference_value;
   uint32_t                             back_reference_value;
   bool                                 depth_cull_enable;
   enum mali_depth_clamp_mode           depth_clamp_mode;
   enum mali_depth_source               depth_source;
   bool                                 depth_write_enable;
   bool                                 depth_bias_enable;
   enum mali_func                       depth_function;
   float                                depth_units;
   float                                depth_factor;
   float                                depth_bias_clamp;
};

#define MALI_DEPTH_STENCIL_header               \
   .type = MALI_DESCRIPTOR_TYPE_DEPTH_STENCIL,  \
   .depth_cull_enable = true,  \
   .depth_clamp_mode = MALI_DEPTH_CLAMP_MODE_0_1,  \
   .depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION

#define MALI_DEPTH_STENCIL_LENGTH 32

#define MALI_DEPTH_STENCIL_ALIGN 32

struct mali_depth_stencil_packed { uint32_t opaque[8]; };

#define MALI_DEPTH_STENCIL_PACKED_T struct mali_depth_stencil_packed

#define MALI_DEPTH_STENCIL_pack(__packed, __unpacked) \
do { \
   MALI_DEPTH_STENCIL_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->type, 0, 3) | \
            util_bitpack_uint((__unpacked)->front_compare_function, 4, 6) | \
            util_bitpack_uint((__unpacked)->front_stencil_fail, 7, 9) | \
            util_bitpack_uint((__unpacked)->front_depth_fail, 10, 12) | \
            util_bitpack_uint((__unpacked)->front_depth_pass, 13, 15) | \
            util_bitpack_uint((__unpacked)->back_compare_function, 16, 18) | \
            util_bitpack_uint((__unpacked)->back_stencil_fail, 19, 21) | \
            util_bitpack_uint((__unpacked)->back_depth_fail, 22, 24) | \
            util_bitpack_uint((__unpacked)->back_depth_pass, 25, 27) | \
            util_bitpack_uint((__unpacked)->stencil_from_shader, 30, 30) | \
            util_bitpack_uint((__unpacked)->stencil_test_enable, 31, 31); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->front_write_mask, 0, 7) | \
            util_bitpack_uint((__unpacked)->back_write_mask, 8, 15) | \
            util_bitpack_uint((__unpacked)->front_value_mask, 16, 23) | \
            util_bitpack_uint((__unpacked)->back_value_mask, 24, 31); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->front_reference_value, 0, 7) | \
            util_bitpack_uint((__unpacked)->back_reference_value, 8, 15); \
   __tmp_packed.opaque[ 3] = 0; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->depth_cull_enable, 22, 22) | \
            util_bitpack_uint((__unpacked)->depth_clamp_mode, 23, 24) | \
            util_bitpack_uint((__unpacked)->depth_source, 25, 26) | \
            util_bitpack_uint((__unpacked)->depth_write_enable, 27, 27) | \
            util_bitpack_uint((__unpacked)->depth_bias_enable, 28, 28) | \
            util_bitpack_uint((__unpacked)->depth_function, 29, 31); \
   __tmp_packed.opaque[ 5] = util_bitpack_float((__unpacked)->depth_units); \
   __tmp_packed.opaque[ 6] = util_bitpack_float((__unpacked)->depth_factor); \
   __tmp_packed.opaque[ 7] = util_bitpack_float((__unpacked)->depth_bias_clamp); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_VERTEX_ARRAY {
   bool                                 packet;
   uint64_t                             pointer;
   uint32_t                             vertex_packet_stride;
   uint32_t                             vertex_attribute_stride;
};

#define MALI_VERTEX_ARRAY_header                \
   0

#define MALI_VERTEX_ARRAY_LENGTH 12

struct mali_vertex_array_packed { uint32_t opaque[3]; };

#define MALI_VERTEX_ARRAY_PACKED_T struct mali_vertex_array_packed

#define MALI_VERTEX_ARRAY_pack(__packed, __unpacked) \
do { \
   MALI_VERTEX_ARRAY_PACKED_T __tmp_packed; \
   assert(((__unpacked)->pointer & 0x3f) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->packet, 0, 0) | \
            util_bitpack_uint((__unpacked)->pointer >> 6, 6, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->pointer >> 6, 6, 63) >> 32; \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->vertex_packet_stride, 0, 15) | \
            util_bitpack_uint((__unpacked)->vertex_attribute_stride, 16, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_DCD_FLAGS_0 {
   bool                                 allow_forward_pixel_to_kill;
   bool                                 allow_forward_pixel_to_be_killed;
   enum mali_pixel_kill                 pixel_kill_operation;
   enum mali_pixel_kill                 zs_update_operation;
   bool                                 allow_primitive_reorder;
   bool                                 overdraw_alpha0;
   bool                                 overdraw_alpha1;
   bool                                 clean_fragment_write;
   bool                                 primitive_barrier;
   bool                                 evaluate_per_sample;
   bool                                 aligned_line_ends;
   enum mali_occlusion_mode             occlusion_query;
   bool                                 front_face_ccw;
   bool                                 cull_front_face;
   bool                                 cull_back_face;
   bool                                 multisample_enable;
   bool                                 shader_modifies_coverage;
   bool                                 alpha_to_coverage_invert;
   bool                                 alpha_to_coverage;
   bool                                 scissor_to_bounding_box;
};

#define MALI_DCD_FLAGS_0_header                 \
   .occlusion_query = MALI_OCCLUSION_MODE_DISABLED

#define MALI_DCD_FLAGS_0_LENGTH 4

struct mali_dcd_flags_0_packed { uint32_t opaque[1]; };

#define MALI_DCD_FLAGS_0_PACKED_T struct mali_dcd_flags_0_packed

#define MALI_DCD_FLAGS_0_pack(__packed, __unpacked) \
do { \
   MALI_DCD_FLAGS_0_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->allow_forward_pixel_to_kill, 0, 0) | \
            util_bitpack_uint((__unpacked)->allow_forward_pixel_to_be_killed, 1, 1) | \
            util_bitpack_uint((__unpacked)->pixel_kill_operation, 2, 3) | \
            util_bitpack_uint((__unpacked)->zs_update_operation, 4, 5) | \
            util_bitpack_uint((__unpacked)->allow_primitive_reorder, 6, 6) | \
            util_bitpack_uint((__unpacked)->overdraw_alpha0, 7, 7) | \
            util_bitpack_uint((__unpacked)->overdraw_alpha1, 8, 8) | \
            util_bitpack_uint((__unpacked)->clean_fragment_write, 9, 9) | \
            util_bitpack_uint((__unpacked)->primitive_barrier, 10, 10) | \
            util_bitpack_uint((__unpacked)->evaluate_per_sample, 11, 11) | \
            util_bitpack_uint((__unpacked)->aligned_line_ends, 13, 13) | \
            util_bitpack_uint((__unpacked)->occlusion_query, 14, 15) | \
            util_bitpack_uint((__unpacked)->front_face_ccw, 16, 16) | \
            util_bitpack_uint((__unpacked)->cull_front_face, 17, 17) | \
            util_bitpack_uint((__unpacked)->cull_back_face, 18, 18) | \
            util_bitpack_uint((__unpacked)->multisample_enable, 19, 19) | \
            util_bitpack_uint((__unpacked)->shader_modifies_coverage, 20, 20) | \
            util_bitpack_uint((__unpacked)->alpha_to_coverage_invert, 21, 21) | \
            util_bitpack_uint((__unpacked)->alpha_to_coverage, 22, 22) | \
            util_bitpack_uint((__unpacked)->scissor_to_bounding_box, 23, 23); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_DCD_FLAGS_1 {
   uint32_t                             sample_mask;
   uint32_t                             render_target_mask;
};

#define MALI_DCD_FLAGS_1_header                 \
   0

#define MALI_DCD_FLAGS_1_LENGTH 4

struct mali_dcd_flags_1_packed { uint32_t opaque[1]; };

#define MALI_DCD_FLAGS_1_PACKED_T struct mali_dcd_flags_1_packed

#define MALI_DCD_FLAGS_1_pack(__packed, __unpacked) \
do { \
   MALI_DCD_FLAGS_1_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->sample_mask, 0, 15) | \
            util_bitpack_uint((__unpacked)->render_target_mask, 16, 23); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_DRAW {
   struct MALI_DCD_FLAGS_0              flags_0;
   struct MALI_DCD_FLAGS_1              flags_1;
   struct MALI_VERTEX_ARRAY             vertex_array;
   float                                minimum_z;
   float                                maximum_z;
   uint64_t                             depth_stencil;
   uint32_t                             blend_count;
   uint64_t                             blend;
   uint64_t                             occlusion;
   struct MALI_SHADER_ENVIRONMENT       shader;
};

#define MALI_DRAW_header                        \
   .flags_0 = { MALI_DCD_FLAGS_0_header },  \
   .flags_1 = { MALI_DCD_FLAGS_1_header },  \
   .vertex_array = { MALI_VERTEX_ARRAY_header },  \
   .shader = { MALI_SHADER_ENVIRONMENT_header }

#define MALI_DRAW_LENGTH 128

#define MALI_DRAW_ALIGN 64

struct mali_draw_packed { uint32_t opaque[32]; };

#define MALI_DRAW_PACKED_T struct mali_draw_packed

#define MALI_DRAW_pack(__packed, __unpacked) \
do { \
   MALI_DRAW_PACKED_T __tmp_packed; \
   assert(((__unpacked)->blend & 0xf) == 0); \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->flags_0.allow_forward_pixel_to_kill, 0, 0) | \
            util_bitpack_uint((__unpacked)->flags_0.allow_forward_pixel_to_be_killed, 1, 1) | \
            util_bitpack_uint((__unpacked)->flags_0.pixel_kill_operation, 2, 3) | \
            util_bitpack_uint((__unpacked)->flags_0.zs_update_operation, 4, 5) | \
            util_bitpack_uint((__unpacked)->flags_0.allow_primitive_reorder, 6, 6) | \
            util_bitpack_uint((__unpacked)->flags_0.overdraw_alpha0, 7, 7) | \
            util_bitpack_uint((__unpacked)->flags_0.overdraw_alpha1, 8, 8) | \
            util_bitpack_uint((__unpacked)->flags_0.clean_fragment_write, 9, 9) | \
            util_bitpack_uint((__unpacked)->flags_0.primitive_barrier, 10, 10) | \
            util_bitpack_uint((__unpacked)->flags_0.evaluate_per_sample, 11, 11) | \
            util_bitpack_uint((__unpacked)->flags_0.aligned_line_ends, 13, 13) | \
            util_bitpack_uint((__unpacked)->flags_0.occlusion_query, 14, 15) | \
            util_bitpack_uint((__unpacked)->flags_0.front_face_ccw, 16, 16) | \
            util_bitpack_uint((__unpacked)->flags_0.cull_front_face, 17, 17) | \
            util_bitpack_uint((__unpacked)->flags_0.cull_back_face, 18, 18) | \
            util_bitpack_uint((__unpacked)->flags_0.multisample_enable, 19, 19) | \
            util_bitpack_uint((__unpacked)->flags_0.shader_modifies_coverage, 20, 20) | \
            util_bitpack_uint((__unpacked)->flags_0.alpha_to_coverage_invert, 21, 21) | \
            util_bitpack_uint((__unpacked)->flags_0.alpha_to_coverage, 22, 22) | \
            util_bitpack_uint((__unpacked)->flags_0.scissor_to_bounding_box, 23, 23); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->flags_1.sample_mask, 0, 15) | \
            util_bitpack_uint((__unpacked)->flags_1.render_target_mask, 16, 23); \
   __tmp_packed.opaque[ 2] = util_bitpack_uint((__unpacked)->vertex_array.packet, 0, 0) | \
            util_bitpack_uint((__unpacked)->vertex_array.pointer >> 6, 6, 63); \
   __tmp_packed.opaque[ 3] = util_bitpack_uint((__unpacked)->vertex_array.pointer >> 6, 6, 63) >> 32; \
   __tmp_packed.opaque[ 4] = util_bitpack_uint((__unpacked)->vertex_array.vertex_packet_stride, 0, 15) | \
            util_bitpack_uint((__unpacked)->vertex_array.vertex_attribute_stride, 16, 31); \
   __tmp_packed.opaque[ 5] = 0; \
   __tmp_packed.opaque[ 6] = util_bitpack_float((__unpacked)->minimum_z); \
   __tmp_packed.opaque[ 7] = util_bitpack_float((__unpacked)->maximum_z); \
   __tmp_packed.opaque[ 8] = 0; \
   __tmp_packed.opaque[ 9] = 0; \
   __tmp_packed.opaque[10] = util_bitpack_uint((__unpacked)->depth_stencil, 0, 63); \
   __tmp_packed.opaque[11] = util_bitpack_uint((__unpacked)->depth_stencil, 0, 63) >> 32; \
   __tmp_packed.opaque[12] = util_bitpack_uint((__unpacked)->blend_count, 0, 3) | \
            util_bitpack_uint((__unpacked)->blend >> 4, 4, 63); \
   __tmp_packed.opaque[13] = util_bitpack_uint((__unpacked)->blend >> 4, 4, 63) >> 32; \
   __tmp_packed.opaque[14] = util_bitpack_uint((__unpacked)->occlusion, 0, 63); \
   __tmp_packed.opaque[15] = util_bitpack_uint((__unpacked)->occlusion, 0, 63) >> 32; \
   __tmp_packed.opaque[16] = util_bitpack_uint((__unpacked)->shader.attribute_offset, 0, 31); \
   __tmp_packed.opaque[17] = util_bitpack_uint((__unpacked)->shader.fau_count, 0, 7); \
   __tmp_packed.opaque[18] = 0; \
   __tmp_packed.opaque[19] = 0; \
   __tmp_packed.opaque[20] = 0; \
   __tmp_packed.opaque[21] = 0; \
   __tmp_packed.opaque[22] = 0; \
   __tmp_packed.opaque[23] = 0; \
   __tmp_packed.opaque[24] = util_bitpack_uint((__unpacked)->shader.resources, 0, 63); \
   __tmp_packed.opaque[25] = util_bitpack_uint((__unpacked)->shader.resources, 0, 63) >> 32; \
   __tmp_packed.opaque[26] = util_bitpack_uint((__unpacked)->shader.shader, 0, 63); \
   __tmp_packed.opaque[27] = util_bitpack_uint((__unpacked)->shader.shader, 0, 63) >> 32; \
   __tmp_packed.opaque[28] = util_bitpack_uint((__unpacked)->shader.thread_storage, 0, 63); \
   __tmp_packed.opaque[29] = util_bitpack_uint((__unpacked)->shader.thread_storage, 0, 63) >> 32; \
   __tmp_packed.opaque[30] = util_bitpack_uint((__unpacked)->shader.fau, 0, 63); \
   __tmp_packed.opaque[31] = util_bitpack_uint((__unpacked)->shader.fau, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_COUNT {
   uint64_t                             count;
};

#define MALI_COUNT_header                       \
   0

#define MALI_COUNT_LENGTH 4

struct mali_count_packed { uint32_t opaque[1]; };

#define MALI_COUNT_PACKED_T struct mali_count_packed

#define MALI_COUNT_pack(__packed, __unpacked) \
do { \
   MALI_COUNT_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->count, 0, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_ALLOCATION {
   uint32_t                             vertex_packet_stride;
   uint32_t                             vertex_attribute_stride;
};

#define MALI_ALLOCATION_header                  \
   0

#define MALI_ALLOCATION_LENGTH 4

struct mali_allocation_packed { uint32_t opaque[1]; };

#define MALI_ALLOCATION_PACKED_T struct mali_allocation_packed

#define MALI_ALLOCATION_pack(__packed, __unpacked) \
do { \
   MALI_ALLOCATION_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->vertex_packet_stride, 0, 15) | \
            util_bitpack_uint((__unpacked)->vertex_attribute_stride, 16, 31); \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_TILER_POINTER {
   uint64_t                             address;
};

#define MALI_TILER_POINTER_header               \
   0

#define MALI_TILER_POINTER_LENGTH 8

struct mali_tiler_pointer_packed { uint32_t opaque[2]; };

#define MALI_TILER_POINTER_PACKED_T struct mali_tiler_pointer_packed

#define MALI_TILER_POINTER_pack(__packed, __unpacked) \
do { \
   MALI_TILER_POINTER_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->address, 0, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->address, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_PRIMITIVE_SIZE {
   float                                fixed_sized;
   uint64_t                             size_array;
};

#define MALI_PRIMITIVE_SIZE_header              \
   0

#define MALI_PRIMITIVE_SIZE_LENGTH 8

struct mali_primitive_size_packed { uint32_t opaque[2]; };

#define MALI_PRIMITIVE_SIZE_PACKED_T struct mali_primitive_size_packed

#define MALI_PRIMITIVE_SIZE_pack(__packed, __unpacked) \
do { \
   MALI_PRIMITIVE_SIZE_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_float((__unpacked)->fixed_sized) | \
            util_bitpack_uint((__unpacked)->size_array, 0, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->size_array, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct MALI_INDICES {
   uint64_t                             address;
};

#define MALI_INDICES_header                     \
   0

#define MALI_INDICES_LENGTH 8

struct mali_indices_packed { uint32_t opaque[2]; };

#define MALI_INDICES_PACKED_T struct mali_indices_packed

#define MALI_INDICES_pack(__packed, __unpacked) \
do { \
   MALI_INDICES_PACKED_T __tmp_packed; \
   __tmp_packed.opaque[ 0] = util_bitpack_uint((__unpacked)->address, 0, 63); \
   __tmp_packed.opaque[ 1] = util_bitpack_uint((__unpacked)->address, 0, 63) >> 32; \
   *(__packed) = __tmp_packed; \
} while (0);

struct mali_malloc_vertex_job_packed {
   union {
      uint32_t opaque[96];
      struct {
         struct mali_job_header_packed HEADER;
         struct mali_primitive_packed PRIMITIVE;
         struct mali_count_packed INSTANCE_COUNT;
         struct mali_allocation_packed ALLOCATION;
         struct mali_tiler_pointer_packed TILER;
         uint32_t __pad64;
         uint32_t __pad68;
         uint32_t __pad72;
         uint32_t __pad76;
         uint32_t __pad80;
         uint32_t __pad84;
         uint32_t __pad88;
         uint32_t __pad92;
         uint32_t __pad96;
         uint32_t __pad100;
         struct mali_scissor_packed SCISSOR;
         struct mali_primitive_size_packed PRIMITIVE_SIZE;
         struct mali_indices_packed INDICES;
         struct mali_draw_packed DRAW;
         struct mali_shader_environment_packed POSITION;
         struct mali_shader_environment_packed VARYING;
      };
   };
};

#define MALI_MALLOC_VERTEX_JOB_PACKED_T struct mali_malloc_vertex_job_packed

#define MALI_MALLOC_VERTEX_JOB_LENGTH 384

#define MALI_MALLOC_VERTEX_JOB_ALIGN 128

#define MALI_MALLOC_VERTEX_JOB_SECTION_HEADER_TYPE struct MALI_JOB_HEADER

#define MALI_MALLOC_VERTEX_JOB_SECTION_HEADER_PACKED_TYPE MALI_JOB_HEADER_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_HEADER_header MALI_JOB_HEADER_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_HEADER_pack MALI_JOB_HEADER_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_HEADER_OFFSET 0

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_TYPE struct MALI_PRIMITIVE

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_PACKED_TYPE MALI_PRIMITIVE_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_header MALI_PRIMITIVE_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_pack MALI_PRIMITIVE_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_OFFSET 32

#define MALI_MALLOC_VERTEX_JOB_SECTION_INSTANCE_COUNT_TYPE struct MALI_COUNT

#define MALI_MALLOC_VERTEX_JOB_SECTION_INSTANCE_COUNT_PACKED_TYPE MALI_COUNT_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_INSTANCE_COUNT_header MALI_COUNT_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_INSTANCE_COUNT_pack MALI_COUNT_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_INSTANCE_COUNT_OFFSET 48

#define MALI_MALLOC_VERTEX_JOB_SECTION_ALLOCATION_TYPE struct MALI_ALLOCATION

#define MALI_MALLOC_VERTEX_JOB_SECTION_ALLOCATION_PACKED_TYPE MALI_ALLOCATION_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_ALLOCATION_header MALI_ALLOCATION_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_ALLOCATION_pack MALI_ALLOCATION_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_ALLOCATION_OFFSET 52

#define MALI_MALLOC_VERTEX_JOB_SECTION_TILER_TYPE struct MALI_TILER_POINTER

#define MALI_MALLOC_VERTEX_JOB_SECTION_TILER_PACKED_TYPE MALI_TILER_POINTER_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_TILER_header MALI_TILER_POINTER_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_TILER_pack MALI_TILER_POINTER_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_TILER_OFFSET 56

#define MALI_MALLOC_VERTEX_JOB_SECTION_SCISSOR_TYPE struct MALI_SCISSOR

#define MALI_MALLOC_VERTEX_JOB_SECTION_SCISSOR_PACKED_TYPE MALI_SCISSOR_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_SCISSOR_header MALI_SCISSOR_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_SCISSOR_pack MALI_SCISSOR_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_SCISSOR_OFFSET 104

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_SIZE_TYPE struct MALI_PRIMITIVE_SIZE

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_SIZE_PACKED_TYPE MALI_PRIMITIVE_SIZE_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_SIZE_header MALI_PRIMITIVE_SIZE_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_SIZE_pack MALI_PRIMITIVE_SIZE_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_PRIMITIVE_SIZE_OFFSET 112

#define MALI_MALLOC_VERTEX_JOB_SECTION_INDICES_TYPE struct MALI_INDICES

#define MALI_MALLOC_VERTEX_JOB_SECTION_INDICES_PACKED_TYPE MALI_INDICES_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_INDICES_header MALI_INDICES_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_INDICES_pack MALI_INDICES_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_INDICES_OFFSET 120

#define MALI_MALLOC_VERTEX_JOB_SECTION_DRAW_TYPE struct MALI_DRAW

#define MALI_MALLOC_VERTEX_JOB_SECTION_DRAW_PACKED_TYPE MALI_DRAW_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_DRAW_header MALI_DRAW_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_DRAW_pack MALI_DRAW_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_DRAW_OFFSET 128

#define MALI_MALLOC_VERTEX_JOB_SECTION_POSITION_TYPE struct MALI_SHADER_ENVIRONMENT

#define MALI_MALLOC_VERTEX_JOB_SECTION_POSITION_PACKED_TYPE MALI_SHADER_ENVIRONMENT_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_POSITION_header MALI_SHADER_ENVIRONMENT_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_POSITION_pack MALI_SHADER_ENVIRONMENT_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_POSITION_OFFSET 256

#define MALI_MALLOC_VERTEX_JOB_SECTION_VARYING_TYPE struct MALI_SHADER_ENVIRONMENT

#define MALI_MALLOC_VERTEX_JOB_SECTION_VARYING_PACKED_TYPE MALI_SHADER_ENVIRONMENT_PACKED_T

#define MALI_MALLOC_VERTEX_JOB_SECTION_VARYING_header MALI_SHADER_ENVIRONMENT_header

#define MALI_MALLOC_VERTEX_JOB_SECTION_VARYING_pack MALI_SHADER_ENVIRONMENT_pack

#define MALI_MALLOC_VERTEX_JOB_SECTION_VARYING_OFFSET 320

struct mali_tiler_job_packed {
   union {
      uint32_t opaque[64];
      struct {
         struct mali_job_header_packed HEADER;
         struct mali_primitive_packed PRIMITIVE;
         struct mali_count_packed INSTANCE_COUNT;
         struct mali_count_packed VERTEX_COUNT;
         struct mali_tiler_pointer_packed TILER;
         uint32_t __pad64;
         uint32_t __pad68;
         uint32_t __pad72;
         uint32_t __pad76;
         uint32_t __pad80;
         uint32_t __pad84;
         uint32_t __pad88;
         uint32_t __pad92;
         uint32_t __pad96;
         uint32_t __pad100;
         struct mali_scissor_packed SCISSOR;
         struct mali_primitive_size_packed PRIMITIVE_SIZE;
         struct mali_indices_packed INDICES;
         struct mali_draw_packed DRAW;
      };
   };
};

#define MALI_TILER_JOB_PACKED_T struct mali_tiler_job_packed

#define MALI_TILER_JOB_LENGTH 256

#define MALI_TILER_JOB_ALIGN 128

#define MALI_TILER_JOB_SECTION_HEADER_TYPE struct MALI_JOB_HEADER

#define MALI_TILER_JOB_SECTION_HEADER_PACKED_TYPE MALI_JOB_HEADER_PACKED_T

#define MALI_TILER_JOB_SECTION_HEADER_header MALI_JOB_HEADER_header

#define MALI_TILER_JOB_SECTION_HEADER_pack MALI_JOB_HEADER_pack

#define MALI_TILER_JOB_SECTION_HEADER_OFFSET 0

#define MALI_TILER_JOB_SECTION_PRIMITIVE_TYPE struct MALI_PRIMITIVE

#define MALI_TILER_JOB_SECTION_PRIMITIVE_PACKED_TYPE MALI_PRIMITIVE_PACKED_T

#define MALI_TILER_JOB_SECTION_PRIMITIVE_header MALI_PRIMITIVE_header

#define MALI_TILER_JOB_SECTION_PRIMITIVE_pack MALI_PRIMITIVE_pack

#define MALI_TILER_JOB_SECTION_PRIMITIVE_OFFSET 32

#define MALI_TILER_JOB_SECTION_INSTANCE_COUNT_TYPE struct MALI_COUNT

#define MALI_TILER_JOB_SECTION_INSTANCE_COUNT_PACKED_TYPE MALI_COUNT_PACKED_T

#define MALI_TILER_JOB_SECTION_INSTANCE_COUNT_header MALI_COUNT_header

#define MALI_TILER_JOB_SECTION_INSTANCE_COUNT_pack MALI_COUNT_pack

#define MALI_TILER_JOB_SECTION_INSTANCE_COUNT_OFFSET 48

#define MALI_TILER_JOB_SECTION_VERTEX_COUNT_TYPE struct MALI_COUNT

#define MALI_TILER_JOB_SECTION_VERTEX_COUNT_PACKED_TYPE MALI_COUNT_PACKED_T

#define MALI_TILER_JOB_SECTION_VERTEX_COUNT_header MALI_COUNT_header

#define MALI_TILER_JOB_SECTION_VERTEX_COUNT_pack MALI_COUNT_pack

#define MALI_TILER_JOB_SECTION_VERTEX_COUNT_OFFSET 52

#define MALI_TILER_JOB_SECTION_TILER_TYPE struct MALI_TILER_POINTER

#define MALI_TILER_JOB_SECTION_TILER_PACKED_TYPE MALI_TILER_POINTER_PACKED_T

#define MALI_TILER_JOB_SECTION_TILER_header MALI_TILER_POINTER_header

#define MALI_TILER_JOB_SECTION_TILER_pack MALI_TILER_POINTER_pack

#define MALI_TILER_JOB_SECTION_TILER_OFFSET 56

#define MALI_TILER_JOB_SECTION_SCISSOR_TYPE struct MALI_SCISSOR

#define MALI_TILER_JOB_SECTION_SCISSOR_PACKED_TYPE MALI_SCISSOR_PACKED_T

#define MALI_TILER_JOB_SECTION_SCISSOR_header MALI_SCISSOR_header

#define MALI_TILER_JOB_SECTION_SCISSOR_pack MALI_SCISSOR_pack

#define MALI_TILER_JOB_SECTION_SCISSOR_OFFSET 104

#define MALI_TILER_JOB_SECTION_PRIMITIVE_SIZE_TYPE struct MALI_PRIMITIVE_SIZE

#define MALI_TILER_JOB_SECTION_PRIMITIVE_SIZE_PACKED_TYPE MALI_PRIMITIVE_SIZE_PACKED_T

#define MALI_TILER_JOB_SECTION_PRIMITIVE_SIZE_header MALI_PRIMITIVE_SIZE_header

#define MALI_TILER_JOB_SECTION_PRIMITIVE_SIZE_pack MALI_PRIMITIVE_SIZE_pack

#define MALI_TILER_JOB_SECTION_PRIMITIVE_SIZE_OFFSET 112

#define MALI_TILER_JOB_SECTION_INDICES_TYPE struct MALI_INDICES

#define MALI_TILER_JOB_SECTION_INDICES_PACKED_TYPE MALI_INDICES_PACKED_T

#define MALI_TILER_JOB_SECTION_INDICES_header MALI_INDICES_header

#define MALI_TILER_JOB_SECTION_INDICES_pack MALI_INDICES_pack

#define MALI_TILER_JOB_SECTION_INDICES_OFFSET 120

#define MALI_TILER_JOB_SECTION_DRAW_TYPE struct MALI_DRAW

#define MALI_TILER_JOB_SECTION_DRAW_PACKED_TYPE MALI_DRAW_PACKED_T

#define MALI_TILER_JOB_SECTION_DRAW_header MALI_DRAW_header

#define MALI_TILER_JOB_SECTION_DRAW_pack MALI_DRAW_pack

#define MALI_TILER_JOB_SECTION_DRAW_OFFSET 128
#endif
