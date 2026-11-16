#include "render_pipeline_v7.h"
#include "native_package_v7.h"
#include "tegu_memory.h"

int tegu_render_parse_v7(const void *bytes, size_t size,
    struct tegu_render_info *out)
{
    if (!bytes || !out) return -1;
    struct tegu_native_v7_info parsed;
    if (tegu_native_v7_parse(bytes, size, &parsed)) return -2;
    const unsigned char *p = (const unsigned char *)bytes;
    struct tegu_render_info info;
    memset(&info, 0, sizeof(info));
    info.color_format = parsed.format;
    info.version = 7;
    info.push_offset = TEGU_RENDER_V7_PUSH;
    info.push_span[0] = tegu_render_word(p + 24);
    info.push_span[1] = tegu_render_word(p + 28);
    info.vertex_mask = parsed.attr_mask;
    info.vertex_count = parsed.attr_count;
    info.position_offset = parsed.position_offset;
    info.secondary_offset = parsed.secondary_offset;
    info.secondary_registers = parsed.secondary_registers;
    info.varying_bytes = parsed.varying_bytes;
    info.secondary_preload = parsed.secondary_preload;
    info.color_count = parsed.attachment_count;
    for (unsigned i = 0; i < 8; ++i) {
        info.color_formats[i] = parsed.attachment[i].format;
        info.color_write_masks[i] = parsed.attachment[i].write_mask;
    }
    info.blend = parsed.blend;
    info.texture = (struct tegu_render_texture_plan) {
        .version = 1, .stage = 16,
        .texture_index = parsed.texture_metal_slot,
        .sampler_index = parsed.sampler_metal_slot,
        .descriptor_set = 0, .binding = 4, .normalized_coordinates = 1
    };
    memset(&info.compositor, 0, sizeof(info.compositor));
    info.compositor.version = 1;
    info.compositor.table = 1;
    info.compositor.descriptor_set = 0;
    info.compositor.ubo_count = 4;
    info.compositor.texture_count = 1;
    info.compositor.sampler_count = 1;
    info.compositor.sample_count = 16;
    info.compositor.texture_stage = 16;
    info.compositor.texture_index = parsed.texture_metal_slot;
    info.compositor.sampler_stage = 16;
    info.compositor.sampler_index = parsed.sampler_metal_slot;
    info.compositor.descriptor_binding = 4;
    info.compositor.normalized_coordinates = 1;
    info.compositor.texel_format_push_offset = parsed.texel_format_push_offset;
    info.compositor.texel_format = 0xd241f2;
    for (unsigned i = 0; i < 4; ++i) {
        info.compositor.ubo_stage[i] = parsed.ubo_stage[i];
        info.compositor.ubo_index[i] = parsed.ubo_metal_slot[i];
        info.compositor.ubo_window[i] = parsed.ubo_window[i];
        info.compositor.push_residual_offset[i] = parsed.push_residual_offset[i];
    }
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t *a = parsed.attributes[i];
        info.vertex[i] = (struct tegu_vertex_attribute) {
            a[0], a[1], a[2], a[3], a[4], a[5], a[6]
        };
    }
    for (unsigned i = 0; i < 2; ++i) {
        struct tegu_render_stage_info *d = &info.stages[i];
        d->code_bytes = parsed.stage[i].bytes;
        d->code_offset = parsed.stage[i].offset;
        d->registers = parsed.stage[i].registers;
        d->fau_words = parsed.stage[i].fau_words;
        d->sysval_mask = parsed.stage[i].sysvals;
        d->push_bytes = parsed.stage[i].push_bytes;
        d->preload = parsed.stage[i].preload;
    }
    *out = info;
    return 0;
}
