/* Standalone Mali compile bench + equivalence check (host only).
 *   malibench <libnord2_mali_runtime.so> <reps> <cap.bin>...
 * Replays captured tegu_mali_compile_generic_pair_options inputs against the
 * given library, prints per-capture best/median thread-CPU ms and whether the
 * output digest equals the captured one (EQ) or not (DIFF). Optional
 * SPEED_PROF=1 SPEED_OUT=dir samples backtraces (tag = capture index). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "sha256.h"
#include "mali_compute_compiler.h"
#include "mali_graphics_compiler.h"
#include "outdigest.h"

typedef int (*opts_fn)(const void *, size_t, const void *, size_t, const struct tegu_mali_specialization *, size_t,
                       const struct tegu_mali_specialization *, size_t, const struct tegu_mali_generic_options *,
                       struct tegu_mali_graphics *, struct tegu_mali_graphics *, struct tegu_mali_graphics_pair2 *,
                       struct tegu_mali_generic_plan *, struct tegu_mali_varying_info *, char *, size_t);
typedef void (*free_fn)(struct tegu_mali_graphics *);

#define NS 400000
#define DEPTH 48
static void *stk[NS][DEPTH];
static unsigned char dep[NS];
static int tagv[NS];
static volatile unsigned ns;
static volatile int curtag = -1;
static void onprof(int s) {
  (void)s;
  unsigned k = ns;
  if (k >= NS || curtag < 0) return;
  ns = k + 1;
  tagv[k] = curtag;
  dep[k] = (unsigned char)backtrace(stk[k], DEPTH);
}
static double cpu_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
struct cap {
  uint8_t *vs, *fs, *vc, *fc, *opt, *plan;
  uint32_t vn, fn, vcn, fcn, on, pn, flags, msgn;
  int status;
  uint8_t digest[32];
};
static uint8_t *get(FILE *f, uint32_t *n) {
  if (fread(n, 4, 1, f) != 1) return NULL;
  uint8_t *p = malloc(*n ? *n : 1);
  if (*n && fread(p, 1, *n, f) != *n) { free(p); return NULL; }
  return p;
}
static int load(const char *path, struct cap *c) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  char m[4];
  int ok = fread(m, 1, 4, f) == 4 && !memcmp(m, "TSC2", 4);
  ok = ok && (c->vs = get(f, &c->vn)) && (c->fs = get(f, &c->fn)) && (c->vc = get(f, &c->vcn)) &&
       (c->fc = get(f, &c->fcn)) && (c->opt = get(f, &c->on)) && (c->plan = get(f, &c->pn));
  ok = ok && fread(&c->flags, 4, 1, f) == 1 && fread(&c->msgn, 4, 1, f) == 1 && fread(&c->status, 4, 1, f) == 1 &&
       fread(c->digest, 1, 32, f) == 32;
  fclose(f);
  return ok ? 0 : -1;
}
static int cmpd(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}
int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: %s lib.so reps cap.bin...\n", argv[0]); return 2; }
  void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
  opts_fn compile = (opts_fn)dlsym(h, "tegu_mali_compile_generic_pair_options");
  free_fn gfree = (free_fn)dlsym(h, "tegu_mali_graphics_free");
  if (!compile || !gfree) { fprintf(stderr, "missing symbols\n"); return 2; }
  int reps = atoi(argv[2]);
  const char *po = getenv("SPEED_OUT");
  int prof = getenv("SPEED_PROF") && atoi(getenv("SPEED_PROF")) && po;
  if (prof) {
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
  double total_best = 0, total_med = 0;
  int ndiff = 0, n = 0;
  for (int i = 3; i < argc; i++) {
    struct cap c;
    memset(&c, 0, sizeof c);
    if (load(argv[i], &c)) { fprintf(stderr, "bad capture %s\n", argv[i]); return 2; }
    double *t = calloc((size_t)reps, sizeof *t);
    int eq = 1, st = 0;
    for (int r = 0; r < reps; r++) {
      struct tegu_mali_graphics vo, fo;
      struct tegu_mali_graphics_pair2 pair;
      struct tegu_mali_generic_plan plan;
      struct tegu_mali_varying_info var;
      memset(&vo, 0, sizeof vo); memset(&fo, 0, sizeof fo); memset(&pair, 0, sizeof pair);
      memset(&var, 0, sizeof var);
      memset(&plan, 0, sizeof plan);
      if (c.pn == sizeof plan) memcpy(&plan, c.plan, sizeof plan);
      char *msg = calloc(1, c.msgn ? c.msgn : 1);
      curtag = i - 3;
      double t0 = cpu_ms();
      st = compile(c.vs, c.vn, c.fs, c.fn, (const void *)c.vc, c.vcn / sizeof(struct tegu_mali_specialization),
                   (const void *)c.fc, c.fcn / sizeof(struct tegu_mali_specialization),
                   c.on ? (const void *)c.opt : NULL, &vo, &fo, (c.flags & 2) ? &pair : NULL, &plan,
                   (c.flags & 1) ? &var : NULL, msg, c.msgn);
      t[r] = cpu_ms() - t0;
      curtag = -1;
      uint8_t d[32];
      speed_out_digest(st, &vo, &fo, (c.flags & 2) ? &pair : NULL, &plan, (c.flags & 1) ? &var : NULL, msg, c.msgn, d);
      if (memcmp(d, c.digest, 32)) eq = 0;
      gfree(&vo); gfree(&fo);
      free(msg);
    }
    qsort(t, (size_t)reps, sizeof *t, cmpd);
    printf("%s\tvs=%u\tfs=%u\tst=%d\tbest=%.3f\tmed=%.3f\t%s\n", argv[i], c.vn, c.fn, st, t[0], t[reps / 2],
           eq ? "EQ" : "DIFF");
    total_best += t[0]; total_med += t[reps / 2]; ndiff += !eq; n++;
    free(t);
  }
  printf("TOTAL n=%d best=%.3f med=%.3f diff=%d\n", n, total_best, total_med, ndiff);
  if (prof) {
    struct itimerval z = {0};
    setitimer(ITIMER_PROF, &z, 0);
    char p[600];
    snprintf(p, sizeof p, "%s/prof.txt", po);
    FILE *f = fopen(p, "w");
    FILE *m = fopen("/proc/self/maps", "r");
    char line[600];
    while (f && m && fgets(line, sizeof line, m)) fprintf(f, "MAP %s", line);
    if (m) fclose(m);
    for (unsigned k = 0; f && k < ns; k++) {
      fprintf(f, "S c%d", tagv[k]);
      for (int d = 0; d < dep[k]; d++) fprintf(f, " %lx", (unsigned long)stk[k][d]);
      fprintf(f, "\n");
    }
    if (f) fclose(f);
  }
  return ndiff ? 1 : 0;
}
