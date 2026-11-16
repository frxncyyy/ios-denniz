/* compile-speed measurement shim (host only, LD_PRELOAD into pipeline-replay).
 *
 * SPEED_OUT=<dir>    per-call stage timing (stages.tsv) and, if SPEED_PROF=1,
 *                    sampled backtraces tagged with the current stage (prof.txt).
 * SPEED_CAPTURE=1    also dump every tegu_mali_compile_generic_pair_options
 *                    input (SPIR-V, spec tables, options) to <dir>/cap/NNNN.bin
 *                    plus the output hash, for the standalone mali bench.
 * Forwarding wrappers pass 20 integer-class args through (all bridged
 * prototypes take only pointers/size_t/uint32_t), x86-64 SysV only. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef uint64_t U;
FILE *speed_tsv;
char speed_outdir[512];
static const char OTHER[] = "other";
__thread const char *speed_stage = OTHER;
int speed_capture;

#define NS 200000
#define DEPTH 48
static void *stk[NS][DEPTH];
static unsigned char dep[NS];
static const char *stg[NS];
static volatile unsigned ns;
static void onprof(int s) {
  (void)s;
  unsigned k = ns;
  if (k >= NS) return;
  ns = k + 1;
  stg[k] = speed_stage;
  dep[k] = (unsigned char)backtrace(stk[k], DEPTH);
}
static void dump(void) {
  if (!speed_outdir[0]) return;
  if (speed_tsv) fflush(speed_tsv);
  if (!ns) return;
  struct itimerval z = {0};
  setitimer(ITIMER_PROF, &z, 0);
  char p[600];
  snprintf(p, sizeof p, "%s/prof.txt", speed_outdir);
  FILE *f = fopen(p, "w");
  if (!f) return;
  FILE *m = fopen("/proc/self/maps", "r");
  char line[600];
  while (m && fgets(line, sizeof line, m)) fprintf(f, "MAP %s", line);
  if (m) fclose(m);
  for (unsigned k = 0; k < ns; k++) {
    fprintf(f, "S %s", stg[k]);
    for (int d = 0; d < dep[k]; d++) fprintf(f, " %lx", (unsigned long)stk[k][d]);
    fprintf(f, "\n");
  }
  fclose(f);
}
__attribute__((constructor)) static void init(void) {
  const char *o = getenv("SPEED_OUT");
  if (!o) return;
  extern char *program_invocation_name;
  const char *t = getenv("SPEED_TARGET");
  if (!t) t = "pipeline-replay";
  if (!strstr(program_invocation_name, t)) return;
  snprintf(speed_outdir, sizeof speed_outdir, "%s", o);
  mkdir(speed_outdir, 0755);
  char p[600];
  snprintf(p, sizeof p, "%s/stages.tsv", speed_outdir);
  speed_tsv = fopen(p, "w");
  speed_capture = getenv("SPEED_CAPTURE") && atoi(getenv("SPEED_CAPTURE"));
  if (speed_capture) {
    snprintf(p, sizeof p, "%s/cap", speed_outdir);
    mkdir(p, 0755);
  }
  atexit(dump);
  if (getenv("SPEED_PROF") && atoi(getenv("SPEED_PROF"))) {
    void *t[4];
    backtrace(t, 4);
    struct sigaction a;
    memset(&a, 0, sizeof a);
    a.sa_handler = onprof;
    a.sa_flags = SA_RESTART;
    sigaction(SIGPROF, &a, 0);
    struct itimerval it = {{0, 1000}, {0, 1000}};
    setitimer(ITIMER_PROF, &it, 0);
  }
}
double now_cpu(void) {
  struct timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
double now_wall(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
void record(const char *name, double c0, double w0, U rv, const char *extra) {
  if (!speed_tsv) return;
  fprintf(speed_tsv, "%s\t%.3f\t%.3f\t%llx\t%s\n", name, now_cpu() - c0, now_wall() - w0,
          (unsigned long long)rv, extra ? extra : "");
}

#define A20 U a1, U a2, U a3, U a4, U a5, U a6, U a7, U a8, U a9, U a10, U a11, U a12, U a13, U a14, U a15, U a16, U a17, U a18, U a19, U a20
#define P20 a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20
#define WRAP(name, tag)                                                     \
  U name(A20) {                                                             \
    static U (*real)(A20);                                                  \
    if (!real) real = (U(*)(A20))dlsym(RTLD_NEXT, #name);                   \
    const char *prev = speed_stage;                                               \
    int outer = prev == OTHER;                              \
    if (outer) speed_stage = tag;                                                 \
    double c0 = now_cpu(), w0 = now_wall();                                 \
    U r = real(P20);                                                        \
    if (outer) { record(tag, c0, w0, r, #name); speed_stage = prev; }             \
    return r;                                                               \
  }

WRAP(tegu_air_library_open, "air_open")
WRAP(tegu_air_library_specialize, "air_specialize")
WRAP(tegu_air_render_pipeline_create_with_vertex_descriptor, "air_create")
WRAP(tegu_air_render_pipeline_create_with_blend, "air_create")
WRAP(tegu_air_render_pipeline_create_with_attachments, "air_create")
WRAP(tegu_air_render_pipeline_create_with_attachment_blends, "air_create")
WRAP(tegu_air_render_pipeline_deserialize, "air_deserialize")
WRAP(tegu_air_render_pipeline_serialize, "air_serialize")
WRAP(tegu_air_render_pipeline_plan_generic_specialized, "air_plan")
WRAP(tegu_air_compile_compute, "air_compute")
WRAP(tegu_mali_compile_generic_pair, "mali_other")
WRAP(tegu_mali_compile_generic_pair_rt, "mali_other")
WRAP(tegu_mali_compile_generic_pair_tile_rt, "mali_other")
WRAP(tegu_mali_compile_graphics_pair_resources, "mali_other")
WRAP(tegu_mali_compile_graphics_pair_resources_v7, "mali_other")
WRAP(tegu_mali_compile_fixed_ui_pair_r80, "mali_other")
WRAP(tegu_mali_compile_fixed_ui_texture_pair_r85, "mali_other")
WRAP(tegu_mali_compile_single_pass_blur3_pair, "mali_other")
WRAP(tegu_mali_compile_graphics_pair_texture, "mali_other")
WRAP(tegu_mali_compile_attachment_clear_pair_v2, "mali_other")
WRAP(tegu_mali_compile_compute_v2, "mali_compute")
WRAP(tegu_mali_compile_specialized, "mali_compute")

