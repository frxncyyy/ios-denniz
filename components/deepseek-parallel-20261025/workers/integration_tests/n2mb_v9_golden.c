/* Independent arch9 (pinned Mesa 81daea6 genxml/v9_pack.h) golden page.
 *
 * Same 16x16 RGBA8 sampled texture / nearest sampler / generic plane as the
 * v10 golden used by the texture owner, but packed through the arch9 macros
 * that the Nord2 backend actually targets.  Test-only oracle, no production
 * code.
 */
#include <string.h>
#include "genxml/v9_pack.h"

void n2mb_v9_vertex_attribute(uint32_t out[8], uint32_t format, uint32_t offset,
                              uint32_t buffer_index, uint32_t stride,
                              uint32_t frequency, uint32_t divisor)
{
    _Alignas(32) unsigned char p[32];
    memset(p, 0, sizeof(p));
    pan_cast_and_pack(p, ATTRIBUTE, cfg) {
        cfg.type = MALI_DESCRIPTOR_TYPE_ATTRIBUTE;
        cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
        cfg.offset_enable = true;
        cfg.format = format;
        cfg.table = 0;
        cfg.frequency = frequency ? MALI_ATTRIBUTE_FREQUENCY_INSTANCE
                                  : MALI_ATTRIBUTE_FREQUENCY_VERTEX;
        cfg.divisor_r = divisor;
        cfg.offset = (int64_t)offset;
        cfg.buffer_index = buffer_index;
        cfg.stride = stride;
        cfg.attribute_stride = stride;
    }
    memcpy(out, p, sizeof(p));
}

void n2mb_v9_golden_page(uint64_t descriptor, uint64_t backing,
                         unsigned char page[4096])
{
    _Alignas(64) unsigned char p[4096];
    memset(p, 0, sizeof(p));
    pan_cast_and_pack(p, RESOURCE, cfg) {
        cfg.address = descriptor + 0x100;
        cfg.size = 64;
    }
    pan_cast_and_pack(p + 0x100, SAMPLER, cfg) {
        cfg.minify_nearest = true;
        cfg.magnify_nearest = true;
    }
    pan_cast_and_pack(p + 0x120, TEXTURE, cfg) {
        cfg.dimension = MALI_TEXTURE_DIMENSION_2D;
        cfg.format = (MALI_RGBA8_UNORM << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
        cfg.width = 16;
        cfg.height = 16;
        cfg.swizzle = MALI_CHANNEL_R | (MALI_CHANNEL_G << 3) |
                      (MALI_CHANNEL_B << 6) | (MALI_CHANNEL_A << 9);
        cfg.surfaces = descriptor + 0x140;
    }
    pan_cast_and_pack(p + 0x140, GENERIC_PLANE, cfg) {
        cfg.clump_ordering = MALI_CLUMP_ORDERING_LINEAR;
        cfg.clump_format = MALI_CLUMP_FORMAT_RAW32;
        cfg.size = 1024;
        cfg.pointer = backing;
        cfg.row_stride = 64;
        cfg.slice_stride = 1024;
        /* arch9 GENERIC_PLANE has no width/height fields; word7 is not
         * defined by the v9 pack macro and is checked against the v10 golden
         * only where the production packer emits it. */
    }
    memcpy(page, p, 4096);
}
