#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sha256.h"
typedef uint64_t U;
extern FILE *speed_tsv; extern char speed_outdir[512]; extern __thread const char *speed_stage; extern int speed_capture;
double now_cpu(void); double now_wall(void); void record(const char *, double, double, U, const char *);
#define stage speed_stage
#define outdir speed_outdir
#define capture speed_capture
/* Exact prototype for the device's entry 11. */
#include "mali_compute_compiler.h"
#include "mali_graphics_compiler.h"
/* Digest of every output of one options compile (shared with the bench). */
#include "outdigest.h"
typedef int (*opts_fn)(const void *, size_t, const void *, size_t, const struct tegu_mali_specialization *, size_t,
                       const struct tegu_mali_specialization *, size_t, const struct tegu_mali_generic_options *,
                       struct tegu_mali_graphics *, struct tegu_mali_graphics *, struct tegu_mali_graphics_pair2 *,
                       struct tegu_mali_generic_plan *, struct tegu_mali_varying_info *, char *, size_t);
static unsigned capn;
static void put(FILE *f, const void *p, uint32_t n) {
  fwrite(&n, 4, 1, f);
  if (n) fwrite(p, 1, n, f);
}
int tegu_mali_compile_generic_pair_options(const void *vs, size_t vn, const void *fs, size_t fn,
                                           const struct tegu_mali_specialization *vc, size_t vcn,
                                           const struct tegu_mali_specialization *fc, size_t fcn,
                                           const struct tegu_mali_generic_options *options,
                                           struct tegu_mali_graphics *vo, struct tegu_mali_graphics *fo,
                                           struct tegu_mali_graphics_pair2 *pair, struct tegu_mali_generic_plan *plan,
                                           struct tegu_mali_varying_info *varying, char *msg, size_t msgn) {
  static opts_fn real;
  if (!real) real = (opts_fn)dlsym(RTLD_NEXT, "tegu_mali_compile_generic_pair_options");
  /* inputs as the caller passed them (plan/varying are inputs too: zeroed) */
  uint32_t optbytes = !options ? 0 : options->version == 1 ? sizeof(struct tegu_mali_generic_options)
                    : options->version == 2 ? sizeof(struct tegu_mali_generic_options2) : 0;
  struct tegu_mali_generic_plan plan_in;
  if (plan) plan_in = *plan;
  const char *prev = stage;
  stage = "mali_options";
  double c0 = now_cpu(), w0 = now_wall();
  int r = real(vs, vn, fs, fn, vc, vcn, fc, fcn, options, vo, fo, pair, plan, varying, msg, msgn);
  uint8_t d[32];
  speed_out_digest(r, vo, fo, pair, plan, varying, msg, msgn, d);
  char extra[200];
  snprintf(extra, sizeof extra, "vs=%zu fs=%zu st=%d out=%02x%02x%02x%02x%02x%02x%02x%02x n=%u", vn, fn, r,
           d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], capn);
  record("mali_options", c0, w0, (U)r, extra);
  stage = prev;
  if (capture) {
    char p[700];
    snprintf(p, sizeof p, "%s/cap/%04u.bin", outdir, capn);
    FILE *f = fopen(p, "wb");
    if (f) {
      fwrite("TSC2", 1, 4, f);
      put(f, vs, (uint32_t)vn);
      put(f, fs, (uint32_t)fn);
      put(f, vc, (uint32_t)(vcn * sizeof *vc));
      put(f, fc, (uint32_t)(fcn * sizeof *fc));
      put(f, options, optbytes);
      put(f, plan ? &plan_in : NULL, plan ? (uint32_t)sizeof plan_in : 0);
      uint32_t flags = (varying ? 1u : 0u) | (pair ? 2u : 0u), mn = (uint32_t)msgn;
      fwrite(&flags, 4, 1, f);
      fwrite(&mn, 4, 1, f);
      fwrite(&r, 4, 1, f);
      fwrite(d, 1, 32, f);
      fclose(f);
    }
  }
  capn++;
  return r;
}
