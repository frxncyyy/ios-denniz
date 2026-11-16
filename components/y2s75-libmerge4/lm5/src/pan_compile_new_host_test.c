/* y2s72: pan_compile_host_test.c's compile() over the statistics and noise shaders (see main) */
/* Host check (no GPU): the shaders of the YCbCr pass (y2s38) and of the camera's ISP (y2s39) through Panfrost's own Valhall compiler for the
 * Mali-G77 (0x9091), with asserts and NIR validation on. The gallium driver's passes around it (sysvals, resource indices, the viewport
 * transform in the vertex stage) are not run, and nothing is executed. */
#include "air_metal.c"
#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_dynarray.h"
#include <time.h>

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
   struct timespec t0, t1;
   fprintf(stderr, "compiling %s ...\n", what);
   clock_gettime(CLOCK_MONOTONIC, &t0);
   bifrost_compile_shader_nir(clone, &inputs, &binary, &info);
   clock_gettime(CLOCK_MONOTONIC, &t1);
   printf("%-28s %6u bytes of Valhall code, %.1f ms\n", what, (unsigned)binary.size, (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
   fflush(stdout);
   int ok = binary.size > 0;
   util_dynarray_fini(&binary);
   ralloc_free(clone);
   ralloc_free(nir);
   return ok;
}

int
main(int argc, char **argv)
{
   struct pipe_screen screen = { 0 };
   const nir_shader_compiler_options *options = &bifrost_nir_options_v9;
   int ok = 1, only = argc > 1 ? atoi(argv[1]) : -1;

   glsl_type_singleton_init_or_ref();
   /* y2s72: the shaders y2s65 (statistics) and y2s70 (noise) added, which no host test had put through Panfrost's compiler */
   if (only < 0 || only == 0) ok &= compile(isp_luma_shader(options), &screen, "isp luma (reference)");
   if (only < 0 || only == 1) ok &= compile(isp_nr_smooth_shader(options, 1), &screen, "nr smooth y");
   if (only < 0 || only == 2) ok &= compile(isp_nr_smooth_shader(options, 2), &screen, "nr smooth c");
   if (only < 0 || only == 3) ok &= compile(isp_nr_diff_shader(options), &screen, "nr diff");
   if (only < 0 || only == 4) ok &= compile(isp_nr_apply_shader(options, 1), &screen, "nr apply y");
   if (only < 0 || only == 5) ok &= compile(isp_nr_apply_shader(options, 2), &screen, "nr apply c");
   if (only < 0 || only == 6) ok &= compile(isp_nr_copy_shader(options, 1), &screen, "nr copy y");
   if (only < 0 || only == 7) ok &= compile(isp_nr_copy_shader(options, 2), &screen, "nr copy c");
   if (only < 0 || only == 8) ok &= compile(isp_stats_shader(options, 1), &screen, "isp stats, MIPI RAW10");
   if (only < 0 || only == 9) ok &= compile(isp_stats_shader(options, 4), &screen, "isp stats, 40-bit words");
   /* y2s75 libmerge4: the ISP shaders with the luma folded in (two outputs), the statistics' cells pass, the apply passes (two outputs, one
    * stillness texel) */
   if (only < 0 || only == 10) ok &= compile(isp_cells_shader(options, 1), &screen, "isp cells + Y, MIPI RAW10");
   if (only < 0 || only == 11) ok &= compile(isp_cells_shader(options, 4), &screen, "isp cells + Y, 40-bit words");
   if (only < 0 || only == 12) ok &= compile(isp_full_shader(options, 1), &screen, "isp full + Y, MIPI RAW10");
   if (only < 0 || only == 13) ok &= compile(isp_full_shader(options, 4), &screen, "isp full + Y, 40-bit words");
   if (only < 0 || only == 14) ok &= compile(isp_stats_cells_shader(options, 1), &screen, "isp stats cells, MIPI RAW10");
   if (only < 0 || only == 15) ok &= compile(isp_stats_cells_shader(options, 4), &screen, "isp stats cells, 40-bit");
   if (only < 0 || only == 16) ok &= compile(isp_chroma_shader(options), &screen, "isp chroma");
   for (unsigned i = 0; i < ISP_NRG_COUNT; i++)
      if (only < 0 || only == 17 + (int)i) ok &= compile(isp_nrg_shader(options, i), &screen, i == 0 ? "nrg rows" : i == 1 ? "nrg calm" : i == 2 ? "nrg moving" : i == 3 ? "nrg params" : "nrg still");
   printf(ok ? "pan_compile_new_host_test: all compiled\n" : "pan_compile_new_host_test: FAILED\n");
   return !ok;
}
