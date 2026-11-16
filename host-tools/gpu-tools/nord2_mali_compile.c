/*
 * Nord 2 host-side arch9 shader compiler probe.
 *
 * This is the real Mesa NIR -> Mali-G77 (Valhall v9) backend, driven through
 * pan_shader_compile() with gpu_id arch major 9.  It is deliberately small:
 * it accepts Vulkan SPIR-V for compute/vertex/fragment, performs the same
 * preprocess/postprocess ordering as the Panfrost Vulkan driver, and writes
 * the machine code plus the reflection metadata the Nord 2 driver needs
 * (TLS size, register count, FAU words, IDVS/varying information).
 *
 * This tool does not execute GPU code and does not claim that arbitrary AIR
 * lowers to this contract.  It is the testable core of the Metal compiler
 * bridge and refuses stages/features the arch9 descriptor backend cannot
 * describe yet.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "compiler/glsl_types.h"
#include "compiler/spirv/nir_spirv.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/compiler/pan_nir.h"
#include "util/u_printf.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* G77: arch major 9, minor 0, product major 0, revision 0. */
#define NORD2_G77_GPU_ID 0x90000000ULL

struct result {
        void *code;
        size_t size;
        struct pan_shader_info info;
        bool ok;
        char error[256];
};

static bool write_file(const char *path, const void *data, size_t size)
{
        FILE *f = fopen(path, "wb");
        if (!f)
                return false;
        if (size && fwrite(data, 1, size, f) != size) {
                fclose(f);
                return false;
        }
        return fclose(f) == 0;
}

static bool valid_span(const void *p, size_t n)
{
        return p != NULL && n != 0 && n <= 16u * 1024u * 1024u;
}

static nir_shader *translate(const uint32_t *words, size_t size,
                             mesa_shader_stage stage, char *error,
                             size_t error_size)
{
        struct spirv_to_nir_options options = {
                .environment = NIR_SPIRV_VULKAN,
                .global_addr_format = nir_address_format_64bit_global,
                .ssbo_addr_format = nir_address_format_64bit_global,
                .ubo_addr_format = nir_address_format_64bit_global,
                .shared_addr_format = nir_address_format_32bit_offset,
                .temp_addr_format = nir_address_format_32bit_offset,
                .constant_addr_format = nir_address_format_64bit_global,
        };
        const nir_shader_compiler_options *nir_opts =
                pan_get_nir_shader_compiler_options(9, false);
        nir_shader *s = spirv_to_nir(words, size / 4, NULL, stage, "main",
                                     &options, nir_opts);
        if (!s) {
                snprintf(error, error_size, "SPIR-V translation failed");
                return NULL;
        }
        NIR_PASS(_, s, nir_lower_variable_initializers, nir_var_function_temp);
        NIR_PASS(_, s, nir_lower_returns);
        NIR_PASS(_, s, nir_inline_functions);
        nir_remove_non_entrypoints(s);
        NIR_PASS(_, s, nir_lower_variable_initializers, ~0);
        NIR_PASS(_, s, nir_split_var_copies);
        NIR_PASS(_, s, nir_lower_var_copies);
        NIR_PASS(_, s, nir_lower_vars_to_ssa);
        NIR_PASS(_, s, nir_lower_system_values);
        NIR_PASS(_, s, nir_lower_explicit_io, nir_var_mem_ssbo,
                 nir_address_format_64bit_global);
        NIR_PASS(_, s, nir_lower_explicit_io, nir_var_mem_global,
                 nir_address_format_64bit_global);
        NIR_PASS(_, s, nir_opt_deref);
        nir_shader_gather_info(s, nir_shader_get_entrypoint(s));
        return s;
}

/* Arch9 FAU loads are byte-addressed with no intrinsic range.  Vulkan SPIR-V
 * carries a base/range pair; fold the base into the address and zero the
 * metadata, matching panvk's push-constant lowering. */
static bool lower_push_constant(nir_builder *b, nir_intrinsic_instr *i,
                                void *data)
{
        (void)data;
        if (i->intrinsic != nir_intrinsic_load_push_constant)
                return false;
        b->cursor = nir_before_instr(&i->instr);
        nir_src_rewrite(&i->src[0],
                        nir_iadd_imm(b, i->src[0].ssa, nir_intrinsic_base(i)));
        nir_intrinsic_set_base(i, 0);
        nir_intrinsic_set_range(i, 0);
        return true;
}

/* Arch9 graphics sysvals are supplied in the FAU.  Keep one fixed, documented
 * layout shared with the kernel and Metal frontend:
 *   0  viewport scale (vec4 f32)
 *   16 viewport offset (vec4 f32)
 *   32 shader-output flags (u32)
 *   48 blend descriptor (u32) */
enum { N2_FAU_VIEWPORT_SCALE = 0, N2_FAU_VIEWPORT_OFFSET = 16,
       N2_FAU_OUTPUT_FLAGS = 32, N2_FAU_BLEND_DESC = 48 };

static bool lower_graphics_sysvals(nir_builder *b, nir_intrinsic_instr *i,
                                   void *data)
{
        (void)data;
        unsigned offset;
        switch (i->intrinsic) {
        case nir_intrinsic_load_viewport_scale:
                offset = N2_FAU_VIEWPORT_SCALE;
                break;
        case nir_intrinsic_load_viewport_offset:
                offset = N2_FAU_VIEWPORT_OFFSET;
                break;
        case nir_intrinsic_load_shader_output_pan:
                offset = N2_FAU_OUTPUT_FLAGS;
                break;
        case nir_intrinsic_load_blend_descriptor_pan:
                offset = N2_FAU_BLEND_DESC;
                break;
        default:
                return false;
        }
        b->cursor = nir_before_instr(&i->instr);
        nir_def_replace(&i->def,
                        nir_load_push_constant(b, i->def.num_components,
                                               i->def.bit_size,
                                               nir_imm_int(b, offset)));
        return true;
}

static int glsl_type_size(const struct glsl_type *type, bool bindless)
{
        (void)bindless;
        return glsl_count_attribute_slots(type, false);
}

static int compile_stage(const uint32_t *words, size_t size,
                         mesa_shader_stage stage, struct result *out)
{
        char error[256] = {0};
        struct util_dynarray binary = {0};
        nir_shader *s = translate(words, size, stage, error, sizeof(error));
        if (!s) {
                snprintf(out->error, sizeof(out->error), "%s", error);
                return 0;
        }

        if (stage != MESA_SHADER_COMPUTE) {
                nir_assign_io_var_locations(s, nir_var_shader_in);
                nir_assign_io_var_locations(s, nir_var_shader_out);
                NIR_PASS(_, s, nir_lower_io,
                         nir_var_shader_in | nir_var_shader_out,
                         glsl_type_size, nir_lower_io_use_interpolated_input_intrinsics);
        }
        pan_preprocess_nir(s, NORD2_G77_GPU_ID);
        NIR_PASS(_, s, nir_lower_explicit_io, nir_var_mem_push_const,
                 nir_address_format_32bit_offset);
        NIR_PASS(_, s, nir_opt_constant_folding);
        NIR_PASS(_, s, nir_shader_intrinsics_pass, lower_push_constant,
                 nir_metadata_control_flow, NULL);
        NIR_PASS(_, s, nir_shader_intrinsics_pass, lower_graphics_sysvals,
                 nir_metadata_control_flow, NULL);
        NIR_PASS(_, s, nir_opt_constant_folding);

        struct pan_compile_inputs inputs = {
                .gpu_id = NORD2_G77_GPU_ID,
                .gpu_variant = 0,
                .is_blend = false,
                .is_blit = false,
                .robust_modes = 0,
                .robust_descriptors = false,
                .trust_varying_flat_highp_types = true,
        };
        struct pan_varying_layout layout = {0};
        if (stage == MESA_SHADER_VERTEX) {
                pan_varying_collect_formats(&layout, s, NORD2_G77_GPU_ID,
                                            true, true);
                pan_build_varying_layout_compact(&layout, s, NORD2_G77_GPU_ID);
                inputs.varying_layout = &layout;
        }
        pan_postprocess_nir(s, &inputs, &out->info);
        NIR_PASS(_, s, nir_shader_intrinsics_pass, lower_push_constant,
                 nir_metadata_control_flow, NULL);
        NIR_PASS(_, s, nir_shader_intrinsics_pass, lower_graphics_sysvals,
                 nir_metadata_control_flow, NULL);
        NIR_PASS(_, s, nir_opt_constant_folding);
        nir_validate_shader(s, "after pan_postprocess_nir");
        pan_shader_compile(s, &inputs, &binary, &out->info);

        if (!binary.size) {
                snprintf(out->error, sizeof(out->error), "backend produced no code");
                goto fail;
        }
        if (binary.size > 1024u * 1024u ||
            out->info.tls_size > 4096u ||
            out->info.push.count > 128u) {
                snprintf(out->error, sizeof(out->error),
                         "code/TLS/FAU footprint exceeds the North 2 contract");
                goto fail;
        }
        out->code = malloc(binary.size);
        if (!out->code) {
                snprintf(out->error, sizeof(out->error), "output allocation failed");
                goto fail;
        }
        memcpy(out->code, binary.data, binary.size);
        out->size = binary.size;
        out->ok = true;
fail:
        util_dynarray_fini(&binary);
        return out->ok;
}

int main(int argc, char **argv)
{
        glsl_type_singleton_init_or_ref();
        u_printf_singleton_init_or_ref();
        if (argc != 6) {
                fprintf(stderr,
                        "usage: %s <compute|vertex|fragment> <input.spv> "
                        "<code.bin> <meta.json> <disasm.txt>\n", argv[0]);
                return 2;
        }
        mesa_shader_stage stage;
        if (!strcmp(argv[1], "compute"))
                stage = MESA_SHADER_COMPUTE;
        else if (!strcmp(argv[1], "vertex"))
                stage = MESA_SHADER_VERTEX;
        else if (!strcmp(argv[1], "fragment"))
                stage = MESA_SHADER_FRAGMENT;
        else {
                fprintf(stderr, "unsupported stage %s\n", argv[1]);
                return 2;
        }

        FILE *f = fopen(argv[2], "rb");
        if (!f) {
                fprintf(stderr, "open %s: %s\n", argv[2], strerror(errno));
                return 2;
        }
        if (fseek(f, 0, SEEK_END) || ftell(f) < 0) {
                fclose(f);
                return 2;
        }
        long len = ftell(f);
        rewind(f);
        if (len < 20 || len > 16 * 1024 * 1024 || (len % 4)) {
                fprintf(stderr, "invalid SPIR-V length %ld\n", len);
                fclose(f);
                return 2;
        }
        uint32_t *spv = malloc((size_t)len);
        if (!spv || fread(spv, 1, (size_t)len, f) != (size_t)len) {
                fprintf(stderr, "read failed\n");
                fclose(f);
                free(spv);
                return 2;
        }
        fclose(f);
        if (spv[0] != 0x07230203u) {
                fprintf(stderr, "bad SPIR-V magic\n");
                free(spv);
                return 2;
        }

        struct result r = {0};
        int ok = compile_stage(spv, (size_t)len, stage, &r);
        free(spv);
        if (!ok) {
                fprintf(stderr, "%s\n", r.error);
                return 1;
        }

        if (!write_file(argv[3], r.code, r.size)) {
                fprintf(stderr, "write %s failed\n", argv[3]);
                return 2;
        }

        FILE *meta = fopen(argv[4], "wb");
        if (!meta) {
                fprintf(stderr, "write %s failed\n", argv[4]);
                return 2;
        }
        fprintf(meta,
                "{\n"
                "  \"stage\": \"%s\",\n"
                "  \"gpu_id\": \"0x%016llx\",\n"
                "  \"arch\": 9,\n"
                "  \"binary_size\": %zu,\n"
                "  \"work_registers\": %u,\n"
                "  \"tls_size\": %u,\n"
                "  \"fau_words\": %u,\n"
                "  \"preload\": \"0x%016llx\",\n"
                "  \"idvs\": %s,\n"
                "  \"secondary_enable\": %s,\n"
                "  \"attributes_read\": \"0x%016llx\",\n"
                "  \"outputs_written\": \"0x%016llx\",\n"
                "  \"writes_global\": %s,\n"
                "  \"writes_coverage\": %s,\n"
                "  \"can_discard\": %s\n"
                "}\n",
                argv[1], (unsigned long long)NORD2_G77_GPU_ID, r.size,
                r.info.work_reg_count, r.info.tls_size, r.info.push.count,
                (unsigned long long)r.info.preload,
                r.info.vs.idvs ? "true" : "false",
                r.info.vs.secondary_enable ? "true" : "false",
                (unsigned long long)r.info.attributes_read,
                (unsigned long long)r.info.outputs_written,
                r.info.writes_global ? "true" : "false",
                r.info.fs.writes_coverage ? "true" : "false",
                r.info.fs.can_discard ? "true" : "false");
        fclose(meta);

        FILE *dis = fopen(argv[5], "wb");
        if (dis) {
                pan_disassemble(dis, r.code, r.size, NORD2_G77_GPU_ID, true);
                fclose(dis);
        }
        free(r.code);
        u_printf_singleton_decref();
        glsl_type_singleton_decref();
        return 0;
}
