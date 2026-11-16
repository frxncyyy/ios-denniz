/* Host check (no GPU, no phone): every function of a .metallib through this library's translator and finalize_for_gallium, then through
 * Panfrost's Valhall compiler for the Mali-G77, with asserts on; and the fault guard of y2s40 around a state creation that faults.
 *   metallib_compile_host_test FILE.metallib [FILE.metallib ...]
 * The gallium driver's own passes around the compiler (sysvals, resource indices) are not run, and nothing is executed: a function this
 * cannot compile for those reasons is reported as such, not as a failure of the library (exit status counts faults in THIS process only). */
#include "air_metal.c"
#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_dynarray.h"
#include <sys/wait.h>

static bool
constant_viewport(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   (void)data;
   if (intr->intrinsic != nir_intrinsic_load_viewport_scale && intr->intrinsic != nir_intrinsic_load_viewport_offset)
      return false;
   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_imm_vec3(b, 512.0f, 384.0f, 0.5f));
   return true;
}

static int
compile_one(const struct air_metallib_function *f, struct pipe_screen *screen)
{
   void *mem_ctx = ralloc_context(NULL);
   struct air_constant_value *constants;
   unsigned num_constants = air_module_function_constants(mem_ctx, f->bitcode, f->bitcode_size, &constants);
   const struct air_to_nir_options options = { .constants = constants, .num_constants = num_constants };
   struct air_shader_info info = { 0 };
   const char *error = NULL;
   nir_shader *nir = air_to_nir(mem_ctx, f->bitcode, f->bitcode_size, f->name, &options, &bifrost_nir_options_v9, &info, &error);

   if (!nir) {
      printf("  %-40s not translated: %s\n", f->name, error ? error : "?");
      ralloc_free(mem_ctx);
      return 2;
   }
   nir_shader *clone = nir_shader_clone(NULL, nir);
   finalize_for_gallium(clone, screen);
   nir_shader_gather_info(clone, nir_shader_get_entrypoint(clone));
   bool psiz = clone->info.stage == MESA_SHADER_VERTEX && (clone->info.outputs_written & VARYING_BIT_PSIZ);
   /* the viewport the vertex stage now reads is the gallium driver's to supply (a sysval); a constant stands for it here */
   if (clone->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, clone, nir_shader_intrinsics_pass, constant_viewport, nir_metadata_control_flow, NULL);
   bifrost_preprocess_nir(clone, 0x9091);
   struct panfrost_compile_inputs inputs = { .gpu_id = 0x9091 };
   if (clone->info.stage == MESA_SHADER_FRAGMENT)
      inputs.valhall.use_ld_var_buf = true;
   if (clone->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, clone, pan_nir_lower_static_noperspective, 0);
   struct util_dynarray binary;
   struct pan_shader_info pinfo = { 0 };
   util_dynarray_init(&binary, NULL);
   bifrost_compile_shader_nir(clone, &inputs, &binary, &pinfo);
   printf("  %-40s %s, %u bytes of Valhall code%s%s\n", f->name, gl_shader_stage_name(clone->info.stage), (unsigned)binary.size,
          psiz ? ", writes a point size" : "", psiz && pinfo.vs.no_psiz_offset ? " (and has the variant without it)" : "");
   util_dynarray_fini(&binary);
   ralloc_free(clone);
   ralloc_free(mem_ctx);
   return 0;
}

/*
 * THE LIBRARY'S OWN PATH: rwa_render_create() with a vertex and a fragment function of the file, over a device whose screen answers as
 * Panfrost's does and whose create_vs_state / create_fs_state do what Panfrost's do with the shader they are handed (preprocess, then the
 * Valhall compiler). Everything this library does to a shader before the back end sees it is then in the test, which compile_one()
 * above leaves out (the stages linked by name, the attachments' formats, the clone).
 */
static void *
fake_create_shader(struct pipe_context *pipe, const struct pipe_shader_state *templ)
{
   nir_shader *nir = templ->ir.nir;
   (void)pipe;
   if (nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, constant_viewport, nir_metadata_control_flow, NULL);
   bool psiz = nir->info.stage == MESA_SHADER_VERTEX && (nir->info.outputs_written & VARYING_BIT_PSIZ);
   bifrost_preprocess_nir(nir, 0x9091);
   struct panfrost_compile_inputs inputs = { .gpu_id = 0x9091 };
   inputs.valhall.use_ld_var_buf = true;
   if (nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, nir, pan_nir_lower_static_noperspective, 0);
   struct util_dynarray binary;
   struct pan_shader_info pinfo = { 0 };
   util_dynarray_init(&binary, NULL);
   bifrost_compile_shader_nir(nir, &inputs, &binary, &pinfo);
   printf("  rwa_render_create: %s stage '%s' compiled, %u bytes%s\n", gl_shader_stage_name(nir->info.stage),
          nir->info.name ? nir->info.name : "?", (unsigned)binary.size,
          psiz ? (pinfo.vs.no_psiz_offset ? ", writes a point size and has the variant without it" : ", writes a point size") : "");
   util_dynarray_fini(&binary);
   return nir;
}
static void *fake_state(struct pipe_context *pipe, const void *state) { (void)pipe; (void)state; return (void *)(uintptr_t)16; }
static void *fake_velems(struct pipe_context *pipe, unsigned n, const struct pipe_vertex_element *e) { (void)pipe; (void)n; (void)e; return (void *)(uintptr_t)16; }
static bool fake_format(struct pipe_screen *s, enum pipe_format f, enum pipe_texture_target t, unsigned a, unsigned b, unsigned bind) { (void)s; (void)f; (void)t; (void)a; (void)b; (void)bind; return true; }
static const char *fake_name(struct pipe_screen *s) { (void)s; return "Panfrost's compiler on the build machine"; }
static struct pipe_resource *
fake_resource(struct pipe_screen *screen, const struct pipe_resource *templ)
{
   struct pipe_resource *r = calloc(1, sizeof(*r));
   *r = *templ;
   pipe_reference_init(&r->reference, 1);
   r->screen = screen;
   return r;
}
static void fake_nothing(struct pipe_context *pipe, void *state) { (void)pipe; (void)state; }
static void fake_no_buffers(struct pipe_context *pipe, unsigned n, const struct pipe_vertex_buffer *b) { (void)pipe; (void)n; (void)b; }
static void fake_resource_destroy(struct pipe_screen *screen, struct pipe_resource *r) { (void)screen; free(r); }
static void fake_subdata(struct pipe_context *pipe, struct pipe_resource *r, unsigned usage, unsigned offset, unsigned size, const void *data) { (void)pipe; (void)r; (void)usage; (void)offset; (void)size; (void)data; }

static int
render_create_one(const struct air_metallib_function *vertex, const struct air_metallib_function *fragment)
{
   struct pipe_screen screen = { .is_format_supported = fake_format, .get_name = fake_name, .resource_create = fake_resource,
                                 .resource_destroy = fake_resource_destroy };
   struct pipe_context pipe = { .screen = &screen, .create_vs_state = fake_create_shader, .create_fs_state = fake_create_shader,
                                .create_blend_state = (void *)fake_state, .create_depth_stencil_alpha_state = (void *)fake_state,
                                .create_rasterizer_state = (void *)fake_state, .create_vertex_elements_state = fake_velems,
                                .buffer_subdata = fake_subdata,
                                /* a refused pipeline is taken apart again (rwa_render_destroy) */
                                .bind_vs_state = fake_nothing, .bind_fs_state = fake_nothing, .bind_blend_state = fake_nothing,
                                .bind_depth_stencil_alpha_state = fake_nothing, .bind_rasterizer_state = fake_nothing,
                                .bind_vertex_elements_state = fake_nothing, .delete_vs_state = fake_nothing,
                                .delete_fs_state = fake_nothing, .delete_blend_state = fake_nothing,
                                .delete_depth_stencil_alpha_state = fake_nothing, .delete_rasterizer_state = fake_nothing,
                                .delete_vertex_elements_state = fake_nothing, .set_vertex_buffers = fake_no_buffers };
   struct rwa_device device = { .screen = &screen, .pipe = &pipe, .nir_options = &bifrost_nir_options_v9 };
   struct rwa_render_shape shape = { .color_format = { 80 }, .color_count = 1, .sample_count = 1 };
   struct rwa_render_info info = { 0 };
   char why[512] = "";

   ((struct pipe_caps *)&screen.caps)->viewport_transform_lowered = true;
   ((struct pipe_caps *)&screen.caps)->fs_position_is_sysval = true;
   struct rwa_render *render = rwa_render_create(&device, vertex->bitcode, vertex->bitcode_size, vertex->name, fragment->bitcode,
                                                 fragment->bitcode_size, fragment->name, &shape, &info, why, sizeof(why));
   printf("  rwa_render_create(%s, %s): %s%s\n", vertex->name, fragment->name, render ? "a pipeline" : "refused: ", render ? "" : why);
   return render ? 0 : 3;
}

#ifndef NO_GUARD_TEST      /* (a library before y2s40 has no guard: -DNO_GUARD_TEST compiles its shaders only) */
/* a back end that faults, for the guard */
static void *
faulting_create(struct pipe_context *pipe, const struct pipe_shader_state *templ)
{
   (void)pipe;
   return (void *)(uintptr_t)*(volatile int *)((uintptr_t)templ->ir.nir & 0x38);        /* a read through (almost) NULL */
}
static void *
aborting_create(struct pipe_context *pipe, const struct pipe_shader_state *templ)
{
   (void)pipe; (void)templ;
   abort();
}
static void *
working_create(struct pipe_context *pipe, const struct pipe_shader_state *templ)
{
   (void)pipe;
   return (void *)templ;
}
static int seen_outside;
static void outside(int sig) { seen_outside = sig; }

static int
guard_test(void)
{
   struct pipe_context pipe = { .create_vs_state = faulting_create, .create_fs_state = aborting_create };
   struct pipe_shader_state templ = { 0 };
   struct sigaction before[RWA_GUARD_SIGNALS], after;
   int crashed = -1, ok = 1;

   signal(SIGFPE, outside);                    /* somebody's handler, which must be back afterwards */
   for (unsigned i = 0; i < RWA_GUARD_SIGNALS; i++)
      sigaction(rwa_guard_signals[i], NULL, &before[i]);
   ok &= guarded_create_state(&pipe, MESA_SHADER_VERTEX, &templ, &crashed) == NULL && crashed == SIGSEGV;
   if (!ok) printf("  (a fault: crashed %d)\n", crashed);
   ok &= guarded_create_state(&pipe, MESA_SHADER_FRAGMENT, &templ, &crashed) == NULL && crashed == SIGABRT;
   if (!ok) printf("  (an abort: crashed %d)\n", crashed);
   pipe.create_fs_state = working_create;
   ok &= guarded_create_state(&pipe, MESA_SHADER_FRAGMENT, &templ, &crashed) == &templ && crashed == 0;
   ok &= guarded_create_state(&pipe, MESA_SHADER_VERTEX, &templ, &crashed) == NULL && crashed == SIGSEGV;        /* and again */
   for (unsigned i = 0; i < RWA_GUARD_SIGNALS; i++) {
      sigaction(rwa_guard_signals[i], NULL, &after);
      ok &= after.sa_handler == before[i].sa_handler;
      if (!ok) printf("  (handler %u not back)\n", i);
   }
   raise(SIGFPE);
   ok &= seen_outside == SIGFPE && !rwa_guard_armed;
   printf("the fault guard: %s\n", ok ? "a fault and an abort inside come back as refusals, the handlers that were there are back" : "FAILED");
   return ok;
}
#else
static int guard_test(void) { return 1; }
#endif

int
main(int argc, char **argv)
{
   struct pipe_screen screen = { 0 };
   int faults = 0;

   /* what Panfrost's screen says of itself (pan_screen.c), which decides what finalize_for_gallium does */
   ((struct pipe_caps *)&screen.caps)->viewport_transform_lowered = true;
   ((struct pipe_caps *)&screen.caps)->fs_position_is_sysval = true;

   glsl_type_singleton_init_or_ref();
   for (int a = 1; a < argc; a++) {
      FILE *file = fopen(argv[a], "rb");
      if (!file) { printf("%s: cannot be read\n", argv[a]); return 2; }
      fseek(file, 0, SEEK_END);
      long size = ftell(file);
      rewind(file);
      uint8_t *data = malloc((size_t)size);
      if (fread(data, 1, (size_t)size, file) != (size_t)size) return 2;
      fclose(file);
      void *mem_ctx = ralloc_context(NULL);
      struct air_metallib lib = { 0 };
      const char *error = NULL;
      if (!air_metallib_parse(mem_ctx, data, (size_t)size, &lib, &error)) { printf("%s: %s\n", argv[a], error ? error : "not a metallib"); return 2; }
      printf("%s: %u functions\n", argv[a], lib.num_functions);
      for (unsigned i = 0; i < lib.num_functions; i++) {
         /* each in a process of its own, so that an assertion in the compiler is this function's result and not the end of the run */
         fflush(stdout);
         pid_t pid = fork();
         if (pid == 0)
         {
            int rc = compile_one(&lib.functions[i], &screen);

            fflush(stdout);
            _exit(rc);
         }
         int status = 0;
         waitpid(pid, &status, 0);
         if (WIFSIGNALED(status)) {
            printf("  %-40s THE COMPILER DIED: signal %d\n", lib.functions[i].name, WTERMSIG(status));
            faults++;
         }
      }
      /* and the first vertex function with the first fragment function as one pipeline, through rwa_render_create */
      {
         const struct air_metallib_function *vertex = NULL, *fragment = NULL;

         for (unsigned i = 0; i < lib.num_functions; i++) {
            const char *type = air_metallib_type_name(lib.functions[i].type);

            if (type && strstr(type, "ertex") && !vertex)
               vertex = &lib.functions[i];
            if (type && strstr(type, "ragment") && !fragment)
               fragment = &lib.functions[i];
         }
         if (vertex && fragment) {
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
               int rc = render_create_one(vertex, fragment);

               fflush(stdout);
               _exit(rc);
            }
            int status = 0;
            waitpid(pid, &status, 0);
            if (WIFSIGNALED(status)) {
               printf("  rwa_render_create(%s, %s) DIED: signal %d\n", vertex->name, fragment->name, WTERMSIG(status));
               faults++;
            } else if (WEXITSTATUS(status) != 0) {
               faults++;
            }
         }
      }
      ralloc_free(mem_ctx);
      free(data);
   }
   if (!guard_test())
      faults++;
   printf("metallib_compile_host_test: %d faults\n", faults);
   return faults != 0;
}
