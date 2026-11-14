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

static int quiet_stages;      /* (the per-stage lines of rwa_render_create are not said for the spec's pipelines) */
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

/* y2s47: what Panfrost's gallium driver does to a shader between its preprocessing and the compiler (pan_shader.c,
 * panfrost_shader_compile): the system values (a texture's size, the viewport, the work group's size, ...) become reads of a uniform
 * buffer, and the resource indices get their tables. These are the driver's own two files, compiled as they are. Without them the
 * compiler met txs / image_size / load_workgroup_size and died, which said nothing about the phone. */
struct panfrost_sysvals_ { unsigned sysvals[128]; unsigned sysval_count; };
bool panfrost_nir_lower_sysvals(nir_shader *s, unsigned arch, void *sysvals);
bool panfrost_nir_lower_res_indices(nir_shader *shader, struct panfrost_compile_inputs *inputs);

/* y2s47: THE VALUES A STAGE ASKS A FUNCTION WITH (SPEC=file; one line a variant: `function: 2=1 1=0` by index or `name=value`).
 * They go through apply_function_constants, as rwa_render_create's and rwa_compute_create_constants' do: a constant the line does
 * not name is undefined. A function with no line is compiled once, every constant a defined 0, as before. */
static char spec_text[65536];
static const char *
spec_variant(const char *name, unsigned which)
{
   size_t n = strlen(name);
   for (const char *l = spec_text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
      if (!strncmp(l, name, n) && l[n] == ':' && which-- == 0)
         return l + n + 1;
   return NULL;
}
static unsigned
spec_parse(const char *line, struct rwa_function_constant *out, unsigned max, char names[][64])
{
   unsigned n = 0;
   while (*line && *line != '\n' && n < max) {
      while (*line == ' ') line++;
      const char *eq = line;
      while (*eq && *eq != '=' && *eq != '\n') eq++;
      if (*eq != '=') break;
      size_t len = (size_t)(eq - line);
      bool number = len > 0;
      for (size_t i = 0; i < len; i++) number &= line[i] >= '0' && line[i] <= '9';
      if (number) { out[n].index = atoi(line); out[n].name = NULL; }
      else { memcpy(names[n], line, MIN2(len, 63)); names[n][MIN2(len, 63)] = 0; out[n].index = -1; out[n].name = names[n]; }
      char *end;
      out[n].bits = strtoull(eq + 1, &end, 0);
      line = end; n++;
   }
   return n;
}

static int bare_module;
static int
compile_one(const struct air_metallib_function *f, struct pipe_screen *screen, const char *variant)
{
   void *mem_ctx = ralloc_context(NULL);
   struct air_constant_value *constants;
   unsigned num_constants = air_module_function_constants(mem_ctx, f->bitcode, f->bitcode_size, &constants);
   char label[256];
   snprintf(label, sizeof(label), "%s", f->name);
   if (getenv("LIST")) {
      printf("  %s:", f->name);
      for (unsigned i = 0; i < num_constants; i++) printf(" %u=%s", constants[i].index, constants[i].name ? constants[i].name : "?");
      printf("\n");
      return 0;
   }
   if (variant) {
      struct rwa_function_constant given[32];
      char names[32][64];
      unsigned n = spec_parse(variant, given, 32, names);
      unsigned matched = apply_function_constants(constants, num_constants, given, n);
      size_t len = strcspn(variant, "\n");
      snprintf(label, sizeof(label), "%s [%.*s ]", f->name, (int)len, variant);
      if (matched != n) { printf("  %-40s THE SPEC NAMES %u CONSTANTS THE FUNCTION HAS NOT GOT\n", label, n - matched); return 4; }
   }
   const struct air_to_nir_options options = { .constants = constants, .num_constants = num_constants };
   struct air_shader_info info = { 0 };
   const char *error = NULL;
   nir_shader *nir = air_to_nir(mem_ctx, f->bitcode, f->bitcode_size, bare_module ? NULL : f->name, &options, &bifrost_nir_options_v9, &info, &error);

   if (!nir) {
      printf("  %-40s NOT TRANSLATED: %s\n", label, error ? error : "?");
      ralloc_free(mem_ctx);
      return 2;
   }
   if (getenv("RES")) {      /* (y2s56) what the translated function reads, by Metal's own indices */
      printf("  %s reads:", label);
      for (unsigned i = 0; i < info.num_resources; i++)
         printf(" %s%u(%s)", info.resources[i].kind == AIR_RESOURCE_TEXTURE ? "tex" : info.resources[i].kind == AIR_RESOURCE_IMAGE ? "img" :
                info.resources[i].kind == AIR_RESOURCE_SAMPLER ? "smp" : "buf", info.resources[i].index,
                info.resources[i].name ? info.resources[i].name : "?");
      printf("\n");
   }
   nir_shader *clone = nir_shader_clone(NULL, nir);
   finalize_for_gallium(clone, screen);
   nir_shader_gather_info(clone, nir_shader_get_entrypoint(clone));
   bool psiz = clone->info.stage == MESA_SHADER_VERTEX && (clone->info.outputs_written & VARYING_BIT_PSIZ);
   /* the viewport the vertex stage now reads is the gallium driver's to supply (a sysval); a constant stands for it here */
   if (clone->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, clone, nir_shader_intrinsics_pass, constant_viewport, nir_metadata_control_flow, NULL);
   if (clone->info.stage == MESA_SHADER_FRAGMENT && (clone->info.outputs_written & BITFIELD_BIT(FRAG_RESULT_COLOR)))
      NIR_PASS(_, clone, nir_lower_fragcolor, clone->info.fs.color_is_dual_source ? 1 : 8);
   bifrost_preprocess_nir(clone, 0x9091);
   struct panfrost_compile_inputs inputs = { .gpu_id = 0x9091 };
   inputs.valhall.use_ld_var_buf = true;
   if (clone->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, clone, pan_nir_lower_static_noperspective, 0);
   struct panfrost_sysvals_ sysvals = { 0 };
   NIR_PASS(_, clone, panfrost_nir_lower_sysvals, 9, &sysvals);
   inputs.pushable_ubos = getenv("PUSH_ALL") ? ~0u : BITFIELD_BIT(0);
   if (sysvals.sysval_count != 0)
      inputs.pushable_ubos |= BITFIELD_BIT(clone->info.num_ubos - 1);
   NIR_PASS(_, clone, panfrost_nir_lower_res_indices, &inputs);
   struct util_dynarray binary;
   struct pan_shader_info pinfo = { 0 };
   util_dynarray_init(&binary, NULL);
   bifrost_compile_shader_nir(clone, &inputs, &binary, &pinfo);
   uint64_t sum = 1469598103934665603ull;       /* so that two variants that came out the same can be seen to have */
   for (unsigned i = 0; i < binary.size; i++)
      sum = (sum ^ ((const uint8_t *)binary.data)[i]) * 1099511628211ull;
   printf("  %-40s %s, %u bytes of Valhall code (%08x), %u system values, %u bytes of thread storage%s%s\n", label,
          gl_shader_stage_name(clone->info.stage), (unsigned)binary.size, (unsigned)(sum ^ (sum >> 32)), sysvals.sysval_count, (unsigned)pinfo.wls_size,
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
   struct panfrost_sysvals_ sysvals = { 0 };      /* y2s47: as panfrost_shader_compile does (see compile_one) */
   NIR_PASS(_, nir, panfrost_nir_lower_sysvals, 9, &sysvals);
   inputs.pushable_ubos = getenv("PUSH_ALL") ? ~0u : BITFIELD_BIT(0);
   if (sysvals.sysval_count != 0)
      inputs.pushable_ubos |= BITFIELD_BIT(nir->info.num_ubos - 1);
   NIR_PASS(_, nir, panfrost_nir_lower_res_indices, &inputs);
   struct util_dynarray binary;
   struct pan_shader_info pinfo = { 0 };
   util_dynarray_init(&binary, NULL);
   bifrost_compile_shader_nir(nir, &inputs, &binary, &pinfo);
   if (!quiet_stages)
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
render_create_shaped(const struct air_metallib_function *vertex, const struct air_metallib_function *fragment, const char *variant,
                     unsigned format, unsigned color_count, bool with_vertex_descriptor, char *why, size_t why_size);
static int
render_create_one(const struct air_metallib_function *vertex, const struct air_metallib_function *fragment)
{
   char why[512] = "";
   int rc = render_create_shaped(vertex, fragment, NULL, 80, 1, false, why, sizeof(why));
   printf("  rwa_render_create(%s, %s): %s%s\n", vertex->name, fragment->name, rc == 0 ? "a pipeline" : "refused: ", rc == 0 ? "" : why);
   return rc;
}
/* y2s47: a pipeline as a stage of GNR.videoprocessor makes it: the vertex function with -[FigMetalContext fullRangeVertexDesc]
 * (one float2 at attribute 0; every vertex function of the library reads a float4 there and nothing else), `color_count` attachments of `format`,
 * and both functions given the variant's values (a function takes the ones it has). */
static int
render_create_shaped(const struct air_metallib_function *vertex, const struct air_metallib_function *fragment, const char *variant,
                     unsigned format, unsigned color_count, bool with_vertex_descriptor, char *why, size_t why_size)
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
   struct rwa_render_shape shape = { .color_count = color_count, .sample_count = 1 };
   struct rwa_render_info info = { 0 };
   struct rwa_function_constant given[32];
   char names[32][64];

   for (unsigned i = 0; i < color_count; i++)
      shape.color_format[i] = format;
   if (with_vertex_descriptor) {
      shape.vertex_attribute[0].format = 29;      /* MTLVertexFormatFloat2, stride 8, per vertex: -[FigMetalContext initRangeVertex] 0x1c789e848 */
      shape.vertex_layout[0].stride = 8;
      shape.vertex_layout[0].step_function = 1;
   }
   if (variant) {
      unsigned n = spec_parse(variant, given, 32, names);
      shape.vertex_constants = shape.fragment_constants = given;
      shape.vertex_constant_count = shape.fragment_constant_count = n;
   }
   ((struct pipe_caps *)&screen.caps)->viewport_transform_lowered = true;
   ((struct pipe_caps *)&screen.caps)->fs_position_is_sysval = true;
   struct rwa_render *render = rwa_render_create(&device, vertex->bitcode, vertex->bitcode_size, vertex->name, fragment->bitcode,
                                                 fragment->bitcode_size, fragment->name, &shape, &info, why, why_size);
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
   unsigned total_compiled = 0, total_variants = 0;

   /* what Panfrost's screen says of itself (pan_screen.c), which decides what finalize_for_gallium does */
   ((struct pipe_caps *)&screen.caps)->viewport_transform_lowered = true;
   ((struct pipe_caps *)&screen.caps)->fs_position_is_sysval = true;

   glsl_type_singleton_init_or_ref();
   if (getenv("SPEC")) {
      FILE *spec = fopen(getenv("SPEC"), "r");
      if (!spec || !fread(spec_text, 1, sizeof(spec_text) - 1, spec)) { printf("SPEC cannot be read\n"); return 2; }
      fclose(spec);
   }
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
      struct air_metallib_function alone = { 0 };
      if (!air_metallib_parse(mem_ctx, data, (size_t)size, &lib, &error)) {
         /* y2s47: ONE MODULE BY ITSELF, as the library keeps the compute modules it accepts on the phone
          * (/private/var/db/nord2-metal/kept/NAME-HASH.air): compiled like a function of a library, its one entry point unnamed */
         if (size < 4 || (memcmp(data, "\xde\xc0\x17\x0b", 4) && memcmp(data, "BC\xc0\xde", 4))) {
            printf("%s: %s\n", argv[a], error ? error : "not a metallib");
            return 2;
         }
         alone.name = strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a];
         alone.bitcode = data;
         alone.bitcode_size = (size_t)size;
         lib.num_functions = 1;
         lib.functions = &alone;
         bare_module = 1;
      } else {
         bare_module = 0;
         printf("%s: %u functions\n", argv[a], lib.num_functions);
      }
      unsigned compiled = 0, variants = 0;
      for (unsigned i = 0; i < lib.num_functions; i++) {
       for (unsigned v = 0; v == 0 || spec_variant(lib.functions[i].name, v); v++) {
         const char *variant = spec_variant(lib.functions[i].name, v);
         /* each in a process of its own, so that an assertion in the compiler is this function's result and not the end of the run */
         fflush(stdout);
         pid_t pid = fork();
         if (pid == 0)
         {
            int rc = compile_one(&lib.functions[i], &screen, variant);

            fflush(stdout);
            _exit(rc);
         }
         int status = 0;
         waitpid(pid, &status, 0);
         variants++;
         if (WIFSIGNALED(status)) {
            printf("  %-40s THE COMPILER DIED: signal %d%s%.*s\n", lib.functions[i].name, WTERMSIG(status), variant ? " with" : "",
                   variant ? (int)strcspn(variant, "\n") : 0, variant ? variant : "");
            faults++;
         } else if (WEXITSTATUS(status) == 0) {
            compiled++;
         } else if (getenv("ALL_MUST_COMPILE")) {
            faults++;
         }
       }
      }
      total_compiled += compiled; total_variants += variants;
      if (!bare_module)
         printf("%s: %u of %u variants of %u functions translated and compiled\n", argv[a], compiled, variants, lib.num_functions);
      /* y2s47: the pipelines the spec names (`pipeline: VERTEX FRAGMENT FORMAT[,FORMAT...]`): every variant of the fragment function,
       * with each format, 1 to 4 attachments (the fewest the library accepts is said) */
      unsigned pipelines = 0, pipelines_made = 0;
      for (unsigned l = 0; spec_variant("pipeline", l); l++) {
         char vname[96], fname[96], formats[96];
         if (sscanf(spec_variant("pipeline", l), " %95s %95s %95s", vname, fname, formats) != 3) continue;
         const struct air_metallib_function *vertex = NULL, *fragment = NULL;
         for (unsigned i = 0; i < lib.num_functions; i++) {
            if (!strcmp(lib.functions[i].name, vname)) vertex = &lib.functions[i];
            if (!strcmp(lib.functions[i].name, fname)) fragment = &lib.functions[i];
         }
         if (!vertex || !fragment) { printf("  pipeline %s + %s: NO SUCH FUNCTION\n", vname, fname); faults++; continue; }
         for (unsigned v = 0; v == 0 || spec_variant(fname, v); v++) {
            const char *variant = spec_variant(fname, v);
            for (const char *f = formats; f && *f; f = strchr(f, ',') ? strchr(f, ',') + 1 : NULL) {
               int made = 0;
               char why[512] = "";
               pipelines++;
               for (unsigned count = 1; count <= 4 && !made; count++) {
                  fflush(stdout);
                  int fds[2];
                  if (pipe(fds)) return 2;
                  pid_t pid = fork();
                  if (pid == 0) {
                     quiet_stages = 1;
                     int rc = render_create_shaped(vertex, fragment, variant, (unsigned)atoi(f), count, true, why, sizeof(why));
                     (void)!write(fds[1], why, strlen(why) + 1);
                     fflush(stdout);
                     _exit(rc);
                  }
                  int status = 0;
                  close(fds[1]);
                  (void)!read(fds[0], why, sizeof(why) - 1);
                  close(fds[0]);
                  waitpid(pid, &status, 0);
                  if (WIFSIGNALED(status)) { snprintf(why, sizeof(why), "DIED: signal %d", WTERMSIG(status)); break; }
                  if (WEXITSTATUS(status) == 0) made = (int)count;
               }
               pipelines_made += made != 0;
               if (!made) {
                  printf("  pipeline %s + %s [%.*s ] format %d: REFUSED: %s\n", vname, fname, variant ? (int)strcspn(variant, "\n") : 0,
                         variant ? variant : "", atoi(f), why);
                  faults++;
               } else if (getenv("SAY_PIPELINES")) {
                  printf("  pipeline %s + %s [%.*s ] format %d: made with %d attachment%s\n", vname, fname,
                         variant ? (int)strcspn(variant, "\n") : 0, variant ? variant : "", atoi(f), made, made == 1 ? "" : "s");
               }
            }
         }
      }
      if (pipelines)
         printf("%s: %u of %u render pipelines made by rwa_render_create\n", argv[a], pipelines_made, pipelines);
      /* and the first vertex function with the first fragment function as one pipeline, through rwa_render_create
       * (not where the spec names pipelines: its vertex functions need their descriptor) */
      if (!pipelines && !bare_module) {
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
#ifdef RWA_REFUSED_TEST
   {
      /* y2s41: a refused module is kept, once, byte for byte, with its sentence; and not at all without the directory */
      static const uint8_t module[] = { 0xde, 0xc0, 0x17, 0x0b, 1, 2, 3, 4, 5, 6, 7 };
      char path[300], back[32];
      int ok = 1, fd;

      snprintf(path, sizeof(path), "rm -rf %s", RWA_REFUSED_DIR);
      (void)!system(path);
      keep_refused("some/entry", module, sizeof(module), "a sentence");
      ok &= access(RWA_REFUSED_DIR, F_OK) != 0;                 /* nothing made when nobody asked */
      mkdir(RWA_REFUSED_DIR, 0777);
      keep_refused("some/entry", module, sizeof(module), "a sentence");
      keep_refused("some/entry", module, sizeof(module), "a sentence");
      uint64_t hash = 1469598103934665603ull;
      for (size_t i = 0; i < sizeof(module); i++)
         hash = (hash ^ module[i]) * 1099511628211ull;
      snprintf(path, sizeof(path), "%s/some_entry-%016llx.air", RWA_REFUSED_DIR, (unsigned long long)hash);
      fd = open(path, O_RDONLY);
      ok &= fd >= 0 && read(fd, back, sizeof(back)) == (ssize_t)sizeof(module) && memcmp(back, module, sizeof(module)) == 0;
      if (fd >= 0)
         close(fd);
      snprintf(path, sizeof(path), "%s/some_entry-%016llx.txt", RWA_REFUSED_DIR, (unsigned long long)hash);
      ok &= access(path, R_OK) == 0;
      snprintf(path, sizeof(path), "ls %s | wc -l | grep -qx 2 && rm -rf %s", RWA_REFUSED_DIR, RWA_REFUSED_DIR);
      ok &= system(path) == 0;
      printf("refused modules are kept: %s\n", ok ? "one .air and one .txt for the module, nothing without the directory" : "FAILED");
      faults += !ok;
#ifdef RWA_KEPT_DIR
      /* y2s42: an accepted compute module is kept the same way where the directory exists (the .air alone), and nowhere else */
      ok = 1;
      snprintf(path, sizeof(path), "rm -rf %s", RWA_KEPT_DIR);
      (void)!system(path);
      keep_module(RWA_KEPT_DIR, false, "a.kernel", module, sizeof(module), NULL);
      ok &= access(RWA_KEPT_DIR, F_OK) != 0;
      mkdir(RWA_KEPT_DIR, 0777);
      keep_module(RWA_KEPT_DIR, false, "a.kernel", module, sizeof(module), NULL);
      keep_module(RWA_KEPT_DIR, false, "a.kernel", module, sizeof(module), NULL);
      snprintf(path, sizeof(path), "%s/a_kernel-%016llx.air", RWA_KEPT_DIR, (unsigned long long)hash);
      fd = open(path, O_RDONLY);
      ok &= fd >= 0 && read(fd, back, sizeof(back)) == (ssize_t)sizeof(module) && memcmp(back, module, sizeof(module)) == 0;
      if (fd >= 0)
         close(fd);
      snprintf(path, sizeof(path), "ls %s | wc -l | grep -qx 1 && rm -rf %s", RWA_KEPT_DIR, RWA_KEPT_DIR);
      ok &= system(path) == 0;
      printf("accepted compute modules are kept: %s\n", ok ? "one .air for the module, nothing without the directory" : "FAILED");
      faults += !ok;
#endif
   }
#endif
   {
      /* y2s44: a view of Metal's 540 gives Cr, Y, Cb in red, green, blue out of the Mali's Y, Cb, Cr; a view of 500 is left as it is */
      struct pipe_sampler_view a = { .swizzle_r = PIPE_SWIZZLE_X, .swizzle_g = PIPE_SWIZZLE_Y, .swizzle_b = PIPE_SWIZZLE_Z, .swizzle_a = PIPE_SWIZZLE_1 }, b = a;
      static const float mali[4] = { 0.9f /* Y */, 0.4f /* Cb */, 0.6f /* Cr */, 1.0f };
      metal_view_swizzle(540, &a);
      metal_view_swizzle(500, &b);
      int ok = mali[a.swizzle_r] == 0.6f && mali[a.swizzle_g] == 0.9f && mali[a.swizzle_b] == 0.4f && a.swizzle_a == PIPE_SWIZZLE_1 &&
               b.swizzle_r == PIPE_SWIZZLE_X && b.swizzle_g == PIPE_SWIZZLE_Y && b.swizzle_b == PIPE_SWIZZLE_Z &&
               pixel_format(540)->pipe == pixel_format(500)->pipe;
      printf("the channels of 540: %s\n", ok ? "red <- Cr, green <- Y, blue <- Cb; 500 untouched" : "FAILED");
      faults += !ok;
   }
   printf("metallib_compile_host_test: %u of %u translated and compiled in all; %d faults\n", total_compiled, total_variants, faults);
   return faults != 0;
}
