/*
 * y2s75 libmerge4: isp_nrg_host_test -- with_before()'s numbers on the GPU (air_isp.c isp_nrg_shader: rows, calm, moving, params, still) on
 * softpipe, against the processor's own arithmetic (rwcam_render.c with_before(), as air_isp.c's processor path has it), for blocks' differences
 * made to hit every case: random ones, uniform ones (mean v exactly: the ties of 1.7 v + 0.5 and 3 v / 4 + 0.5, v = 5, 15, 25, ...; 2, 6, 10, ...),
 * two-valued ones (means (10 L - 5) / 17 and (4 N - 2) / 3 from unequal counts), all moving and none. Every block's stillness and both nears
 * must be the processor's.
 */
#include "air_metal.c"

static unsigned long long rng = 88172645463325252ull;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng >> 16); }

/* the processor's numbers (air_isp.c's processor path) */
static void
cpu_still(const uint8_t *diff, uint32_t bw, uint32_t bh, int level, uint8_t *out)
{
   uint32_t n = bw * bh, moving = 0;
   uint64_t all = 0, calm = 0, calm_n = 0;
   int near_y = level == 2 ? 18 : 10, near_c = level == 2 ? 14 : 8, limit;

   for (uint32_t b = 0; b < n; b++)
      all += diff[b];
   for (uint32_t b = 0; b < n; b++)
      if ((uint64_t)diff[b] * n <= 2 * all) {
         calm += diff[b];
         calm_n++;
      }
   limit = (int)(1.7 * (calm_n ? (double)calm / (double)calm_n : 0) + 0.5);
   limit = limit < 10 ? 10 : limit > 48 ? 48 : limit;
   int noise3 = (int)(3.0 * (calm_n ? (double)calm / (double)calm_n : 0) / 4 + 0.5);
   noise3 = noise3 > 40 ? 40 : noise3;
   near_y = noise3 > near_y ? noise3 : near_y;
   near_c = noise3 * 3 / 4 > near_c ? noise3 * 3 / 4 : near_c;
   for (uint32_t b = 0; b < n; b++)
      moving += diff[b] > limit;
   int all_moving = (double)moving > 0.35 * n;
   for (uint32_t by = 0; by < bh; by++)
      for (uint32_t bx = 0; bx < bw; bx++) {
         bool s = !all_moving;
         for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
               int64_t nx = (int64_t)bx + dx, ny = (int64_t)by + dy;
               if (nx >= 0 && nx < bw && ny >= 0 && ny < bh && diff[ny * bw + nx] > limit)
                  s = false;
            }
         uint8_t *t = out + 4 * ((size_t)by * bw + bx);
         t[0] = s; t[1] = (uint8_t)near_y; t[2] = (uint8_t)near_c; t[3] = 0;
      }
}

static int failures, cases;

static void
check(struct rwa_isp *isp, const uint8_t *diff, uint32_t bw, uint32_t bh, int level, const char *what)
{
   struct rwa_device *device = isp->device;
   char why[512] = "";
   uint32_t n = bw * bh;
   uint8_t *want = malloc(4 * (size_t)n), *got = malloc(4 * (size_t)n);
   struct rwa_texture *dt = isp_texture(device, 10, bw, bh, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, sizeof(why));
   struct rwa_texture *rows = isp_texture(device, 123, 1, bh, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, sizeof(why));
   struct rwa_texture *calm = isp_texture(device, 123, 1, bh, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, sizeof(why));
   struct rwa_texture *moving = isp_texture(device, 123, 1, bh, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, sizeof(why));
   struct rwa_texture *params = isp_texture(device, 123, 1, 1, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, sizeof(why));
   struct rwa_texture *st = isp_texture(device, 73, bw, bh, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, sizeof(why));
   const unsigned origin[3] = { 0, 0, 0 }, size[3] = { bw, bh, 1 };
   struct isp_block gb;
   uint64_t lm, nm;
   void *f[ISP_NRG_COUNT];

   cases++;
   cpu_still(diff, bw, bh, level, want);
   rwa_texture_write(dt, 0, 0, origin, size, diff, bw, why, sizeof(why));
   for (unsigned i = 0; i < ISP_NRG_COUNT; i++)
      f[i] = isp_nrg_fs(device, i, why, sizeof(why));
   memset(&gb, 0, sizeof(gb));
   isp_nr_tie_masks(&lm, &nm);
   gb.q[0] = (int32_t)bw; gb.q[1] = (int32_t)bh; gb.q[2] = (int32_t)n; gb.q[3] = (int32_t)((uint32_t)(0.35 * (double)n) + 1);
   gb.q[4] = level == 2 ? 18 : 10; gb.q[5] = level == 2 ? 14 : 8;
   gb.q[6] = (int32_t)(uint32_t)lm; gb.q[7] = (int32_t)(uint32_t)(lm >> 32); gb.q[8] = (int32_t)(uint32_t)nm; gb.gq[0] = (int32_t)(uint32_t)(nm >> 32);
   struct rwa_texture *const t_rows[1] = { dt }, *const t_calm[2] = { dt, rows }, *const t_moving[2] = { dt, calm };
   struct rwa_texture *const t_params[1] = { moving }, *const t_still[2] = { dt, params };
   int e = 0;
   {
      RWA_GUARD();
      if (!ycbcr_state(device, why, sizeof(why)))
         e = -1;
      if (device->isp.nearest == NULL)
         device->isp.nearest = device->pipe->create_sampler_state(device->pipe, &(struct pipe_sampler_state){
            .wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE, .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE, .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
            .min_img_filter = PIPE_TEX_FILTER_NEAREST, .mag_img_filter = PIPE_TEX_FILTER_NEAREST, .min_mip_filter = PIPE_TEX_MIPFILTER_NONE });
#define P(dst, w, tex, c) (gb.out_w = (int32_t)(dst)->res->width0, gb.out_h = (int32_t)(dst)->res->height0, \
                           e = e ? e : isp_pass(device, dst, f[w], tex, c, &gb, #w, why, sizeof(why)))
      P(rows, ISP_NRG_ROWS, t_rows, 1); P(calm, ISP_NRG_CALM, t_calm, 2); P(moving, ISP_NRG_MOVING, t_moving, 2);
      P(params, ISP_NRG_PARAMS, t_params, 1); P(st, ISP_NRG_STILL, t_still, 2);
#undef P
   }
   if (e != 0 || rwa_texture_read(st, 0, 0, origin, size, got, 4 * (size_t)bw, why, sizeof(why)) != 0) {
      fprintf(stderr, "FAILED %s: passes %d: %s\n", what, e, why);
      failures++;
   } else if (memcmp(want, got, 4 * (size_t)n) != 0) {
      size_t bad = 0, first = 0;
      for (size_t i = 0; i < 4 * (size_t)n; i++)
         if (want[i] != got[i] && bad++ == 0)
            first = i;
      fprintf(stderr, "FAILED %s (%ux%u, level %d): %zu bytes differ, first at block %zu byte %zu: processor %u, GPU %u\n", what, bw, bh, level,
              bad, first / 4, first % 4, want[first], got[first]);
      failures++;
   }
   rwa_texture_destroy(dt); rwa_texture_destroy(rows); rwa_texture_destroy(calm); rwa_texture_destroy(moving);
   rwa_texture_destroy(params); rwa_texture_destroy(st);
   free(want); free(got);
}

int
main(void)
{
   char why[512] = "", what[96];
   static const uint32_t sizes[][2] = { { 17, 9 }, { 40, 30 }, { 81, 61 }, { 240, 135 } };

   setenv("RWA_SOFTWARE", "1", 1);
   struct rwa_isp *isp = rwa_isp_create(why, sizeof(why));
   if (isp == NULL) {
      fprintf(stderr, "FAILED: no context: %s\n", why);
      return 1;
   }
   uint64_t lm, nm;
   isp_nr_tie_masks(&lm, &nm);
   printf("tie tables: limit %016llx, noise3 %016llx\n", (unsigned long long)lm, (unsigned long long)nm);
   for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
      uint32_t bw = sizes[z][0], bh = sizes[z][1], n = bw * bh;
      uint8_t *d = malloc(n);
      for (int level = 1; level <= 2; level++) {
         /* uniform: mean v exactly (ties of both roundings at v = 5, 15, 25, ...; 2, 6, 10, ...), all moving above the limit */
         for (unsigned v = 0; v <= 64; v++) {
            memset(d, (int)v, n);
            snprintf(what, sizeof(what), "uniform %u", v);
            check(isp, d, bw, bh, level, what);
         }
         /* two values: a mean (10 L - 5) / 17 (limit tie) from 17 k blocks with sum 17 k * that ... built as a blocks of p and b of q */
         for (unsigned L = 6; L <= 52 && n >= 17; L++) {
            /* mean m = (10 L - 5) / 17: 17 blocks summing to 10 L - 5, repeated; the rest copies of that pattern */
            for (uint32_t i = 0; i < n; i++) {
               uint32_t k = i % 17, total = 10 * L - 5, base = total / 17, extra = total % 17;
               d[i] = (uint8_t)(base + (k < extra ? 1 : 0));
            }
            snprintf(what, sizeof(what), "limit tie L=%u", L);
            check(isp, d, bw, bh, level, what);
         }
         for (unsigned N = 1; N <= 44 && n >= 3; N++) {
            for (uint32_t i = 0; i < n; i++) {
               uint32_t k = i % 3, total = 4 * N - 2, base = total / 3, extra = total % 3;
               d[i] = (uint8_t)(base + (k < extra ? 1 : 0));
            }
            snprintf(what, sizeof(what), "noise3 tie N=%u", N);
            check(isp, d, bw, bh, level, what);
         }
         /* random: calm backgrounds with moving patches and outliers */
         for (int r = 0; r < 24; r++) {
            unsigned bg = rnd() % 40, spread = 1 + rnd() % 20;
            for (uint32_t i = 0; i < n; i++) {
               unsigned v = bg + rnd() % spread;
               if (rnd() % 100 < (unsigned)r * 2)
                  v = 40 + rnd() % 216;
               d[i] = (uint8_t)(v > 255 ? 255 : v);
            }
            snprintf(what, sizeof(what), "random %d", r);
            check(isp, d, bw, bh, level, what);
         }
      }
      free(d);
   }
   rwa_isp_destroy(isp);
   printf("isp_nrg_host_test: %d cases, %s\n", cases, failures ? "FAILED" : "every block's stillness and nears the processor's");
   return failures != 0;
}
