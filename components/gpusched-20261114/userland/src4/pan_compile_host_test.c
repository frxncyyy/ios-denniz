/* Host check (no GPU): the shaders of the YCbCr pass (y2s38) and of the camera's ISP (y2s39) through Panfrost's own Valhall compiler for the
 * Mali-G77 (0x9091), with asserts and NIR validation on. The gallium driver's passes around it (sysvals, resource indices, the viewport
 * transform in the vertex stage) are not run, and nothing is executed. */
#include "air_metal.c"
#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_dynarray.h"

static int
compile(nir_shader *nir, struct pipe_screen *screen, const char *what)
{
   nir_shader *clone = nir_shader_clone(NULL, nir);
   finalize_for_gallium(clone, screen);
   nir_shader_gather_info(clone, nir_shader_get_entrypoint(clone));
   bifrost_preprocess_nir(clone, 0x9091);
   struct panfrost_compile_inputs inputs = { .gpu_id = 0x9091 };
   if (clone->info.stage == MESA_SHADER_FRAGMENT)
      inputs.valhall.use_ld_var_buf = true;
   if (clone->info.stage == MESA_SHADER_VERTEX)       /* as the gallium driver does (pan_shader.c) */
      NIR_PASS(_, clone, pan_nir_lower_static_noperspective, 0);
   struct util_dynarray binary;
   struct pan_shader_info info = { 0 };
   util_dynarray_init(&binary, NULL);
   bifrost_compile_shader_nir(clone, &inputs, &binary, &info);
   printf("%-28s %5u bytes of Valhall code\n", what, (unsigned)binary.size);
   int ok = binary.size > 0;
   util_dynarray_fini(&binary);
   ralloc_free(clone);
   ralloc_free(nir);
   return ok;
}

int
main(void)
{
   struct pipe_screen screen = { 0 };
   const nir_shader_compiler_options *options = &bifrost_nir_options_v9;
   int ok = 1;

   glsl_type_singleton_init_or_ref();
   ok &= compile(ycbcr_vertex_shader(options), &screen, "quad vertex");
   for (int m = 0; m < 3; m++)
      for (int f = 0; f < 2; f++)
         ok &= compile(ycbcr_fragment_shader(options, m, f), &screen, "ycbcr fragment");
   ok &= compile(isp_cells_shader(options, 1), &screen, "isp cells, MIPI RAW10");
   ok &= compile(isp_cells_shader(options, 4), &screen, "isp cells, 40-bit words");
   ok &= compile(isp_full_shader(options, 1), &screen, "isp full, MIPI RAW10");
   ok &= compile(isp_full_shader(options, 4), &screen, "isp full, 40-bit words");
   ok &= compile(isp_luma_shader(options), &screen, "isp luma");
   ok &= compile(isp_chroma_shader(options), &screen, "isp chroma");
   printf(ok ? "pan_compile_test: all compiled\n" : "pan_compile_test: FAILED\n");
   return !ok;
}
