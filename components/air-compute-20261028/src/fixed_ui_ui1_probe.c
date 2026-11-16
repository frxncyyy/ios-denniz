/* Host-only probe: runs the R80 frontend plan, the R80 Mali compile and every
 * TFU1 writer predicate for one translated fixed-UI pipeline, then reports
 * which predicate refuses and the fields it checks.  Used with
 * probe_fixed_ui_ui1.py on the reconstructed live lock-screen variants. */
#include <stdio.h>
#include <string.h>
#include "air_bridge.h"
#include "mali_graphics_compiler.h"
#include "native_package_ui1.c"
#include "native_package_ui1_writer.c"
#include "fixed_ui_ui1_diag.h"

int tegu_fixed_ui_ui1_probe(const TeguAIRRenderPipeline *pipeline, const void *vs_spirv, size_t vs_length,
    const void *fs_spirv, size_t fs_length, char *out, size_t out_length)
{
    char message[1024] = {0};
    char gate[512] = {0};
    int r80 = tegu_air_render_pipeline_native_check_fixed_ui_r80(pipeline, 80, gate, sizeof(gate));
    if (r80) {
        char textured[512] = {0};
        int r85 = tegu_air_render_pipeline_native_check_fixed_ui_r85_texture(pipeline, 80, textured, sizeof(textured));
        return snprintf(out, out_length, "{\"r80_gate\":%d,\"r80_message\":\"%s\",\"r85_gate\":%d,\"r85_message\":\"%s\"}",
            r80, gate, r85, textured);
    }
    TeguAIRRenderResource resources[2];
    memset(resources, 0, sizeof(resources));
    resources[0].kind = 1; resources[0].stage = 1; resources[0].index = 2; resources[0].offset = 19; resources[0].length = 512;
    resources[1].kind = 1; resources[1].stage = 2; resources[1].index = 1; resources[1].offset = 277; resources[1].length = 512;
    TeguAIRRenderDraw draw = {1, 0, 3, 1, 0};
    TeguAIRFixedUiR80Plan front;
    memset(&front, 0, sizeof(front));
    int plan = tegu_air_render_pipeline_plan_fixed_ui_r80(pipeline, resources, 2, &draw, &front, message, sizeof(message));
    if (plan) return snprintf(out, out_length, "{\"plan\":%d,\"message\":\"%s\"}", plan, message);
    struct tegu_mali_graphics vs, fs;
    struct tegu_mali_graphics_pair2 pair;
    struct tegu_mali_fixed_ui_plan back;
    memset(&vs, 0, sizeof(vs)); memset(&fs, 0, sizeof(fs)); memset(&pair, 0, sizeof(pair)); memset(&back, 0, sizeof(back));
    int compiled = tegu_mali_compile_fixed_ui_pair_r80(vs_spirv, vs_length, fs_spirv, fs_length,
        &vs, &fs, &pair, &back, message, sizeof(message));
    if (compiled) return snprintf(out, out_length, "{\"plan\":0,\"compile\":%d,\"message\":\"%s\"}", compiled, message);
    int n = snprintf(out, out_length, "{\"plan\":0,\"compile\":0,\"writer\":");
    if (n > 0 && (size_t)n < out_length) n += tegu_ui1_diagnose(&vs, &fs, &pair, &back, &front, out + n, out_length - (size_t)n);
    if (n > 0 && (size_t)n + 1 < out_length) { out[n++] = '}'; out[n] = 0; }
    tegu_mali_graphics_free(&vs);
    tegu_mali_graphics_free(&fs);
    return n;
}
