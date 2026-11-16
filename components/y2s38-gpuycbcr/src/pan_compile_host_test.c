/* Host check (no GPU): the two shaders of rwa_texture_convert_ycbcr through Panfrost's own Valhall compiler for the Mali-G77 (0x9091).
 * The gallium driver's passes around it (sysvals, resource indices, the viewport transform in the vertex stage) are not run. */
#include "air_metal.c"
#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_dynarray.h"

static int
compile(nir_shader *nir, struct pipe_screen *screen, const char *what)
{
   nir_shader *clone = nir_shader_clone(NULL, nir);
   if (getenv("SHOW")) clone->info.internal = false;
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
   printf("%s: %u bytes of Valhall code", what, (unsigned)binary.size);
   if (clone->info.stage == MESA_SHADER_FRAGMENT)
      printf(", textures %u, outputs written 0x%llx", (unsigned)info.texture_count, (unsigned long long)info.outputs_written);
   printf("\n");
   int ok = binary.size > 0;
   util_dynarray_fini(&binary);
   ralloc_free(clone);
   return ok;
}

int
main(void)
{
   struct pipe_screen screen = { 0 };
   const nir_shader_compiler_options *options = &bifrost_nir_options_v9;
   int ok = 1;

   glsl_type_singleton_init_or_ref();
   { extern int bifrost_debug; if (getenv("SHOW")) bifrost_debug |= 0x0002; }
   nir_shader *vs = ycbcr_vertex_shader(options);
   ok &= compile(vs, &screen, "vertex");
   for (int m = 0; m < 3; m++)
      for (int f = 0; f < 2; f++) {
         char name[40];
         snprintf(name, sizeof(name), "fragment matrix %d range %s", m, f ? "full" : "video");
         nir_shader *fs = ycbcr_fragment_shader(options, m, f);
         ok &= compile(fs, &screen, name);
         ralloc_free(fs);
      }
   ralloc_free(vs);
   printf(ok ? "pan_compile_test: all compiled\n" : "pan_compile_test: FAILED\n");
   return !ok;
}
