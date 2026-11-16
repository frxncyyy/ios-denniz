/*
 * y2s72: host test of the bounded wait (air_isp.c isp_warm, isp_account) on softpipe: the shaders are made by the library's own thread,
 * the caller waits at most ISP_WARM_WAIT_MS on a condition (it must come back in well under a second whatever happens), the shaders are
 * then there; ISP_BAD_LIMIT failures in a row turn the path off and every later call answers RWA_ISP_E_GPU at once.
 * Also: a render with the statistics after the warm-up (rwa_isp_render_textures with rwa_isp_want_stats) still gives them.
 */
#include "air_metal.c"
#include <time.h>

static double
now_ms(void)
{
   struct timespec t;

   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int
main(void)
{
   char why[512] = "";
   int failures = 0;

   setenv("RWA_SOFTWARE", "1", 1);
   struct rwa_isp *isp = rwa_isp_create(why, sizeof(why));
   if (isp == NULL) {
      fprintf(stderr, "FAILED: no context: %s\n", why);
      return 1;
   }
   static uint8_t raw[640 * 5 / 4 * 480];
   static uint8_t gamma[4096];
   struct rwa_isp_frame f = { .size = sizeof(f), .width = 640, .height = 480, .stride = 800, .format = 4, .bayer = 3, .black = 64, .raw = raw };
   struct rwa_isp_params p = { .size = sizeof(p), .mode = RWA_ISP_MODE_CELLS, .wb = { 1, 1, 1 }, .digital = 1, .zoom = 1, .gamma = gamma };

   for (int i = 0; i < 4096; i++)
      gamma[i] = (uint8_t)(i >> 4);
   double t0 = now_ms();
   int e = isp_warm(isp, &f, &p, why, sizeof(why));
   double t1 = now_ms();
   printf("first isp_warm: %d after %.1f ms (%s)\n", e, t1 - t0, e ? why : "ready");
   if (t1 - t0 > ISP_WARM_WAIT_MS + 200) {
      fprintf(stderr, "FAILED: the first call waited %.1f ms, more than the bound\n", t1 - t0);
      failures++;
   }
   for (int k = 0; k < 100 && e != RWA_ISP_OK; k++) {
      struct timespec d = { 0, 100000000L };

      nanosleep(&d, NULL);
      e = isp_warm(isp, &f, &p, why, sizeof(why));
   }
   if (e != RWA_ISP_OK || isp->device->isp.cells[1] == NULL || isp->device->isp.luma == NULL || isp->device->isp.chroma == NULL ||
       isp->device->isp.stats[1] == NULL || isp->device->isp.nr[0] == NULL || isp->device->isp.nr[6] == NULL) {
      fprintf(stderr, "FAILED: the shaders are not there after the warm-up (%d: %s)\n", e, why);
      failures++;
   }
   /* a render with the statistics, now that its shaders were made by the other thread */
   {
      struct rwa_device *device = rwa_isp_device(isp);
      struct rwa_texture_shape s = { .pixel_format = 10, .type = 2, .width = 320, .height = 240, .depth = 1, .array_length = 1, .mip_levels = 1,
                                     .sample_count = 1, .usage = 1 | 4 };
      struct rwa_texture *luma = rwa_texture_create(device, &s, why, sizeof(why));
      s.pixel_format = 30; s.width = 160; s.height = 120;
      struct rwa_texture *chroma = rwa_texture_create(device, &s, why, sizeof(why));
      static float stats[64 * 64 * RWA_ISP_STATS_KINDS * 4];
      struct rwa_isp_result r = { .size = sizeof(r) };

      for (size_t i = 0; i < sizeof(raw); i++)
         raw[i] = (uint8_t)(i * 37 + (i >> 7));
      rwa_isp_want_stats(isp, stats, sizeof(stats) / sizeof(stats[0]));
      e = rwa_isp_render_textures(isp, &f, &p, luma, chroma, 320, 240, 0, &r, why, sizeof(why));
      printf("render with statistics: %d, %ux%u blocks of step %u\n", e, r.stats_w, r.stats_h, r.stats_step);
      if (e != RWA_ISP_OK || r.stats_w == 0 || r.stats_h == 0) {
         fprintf(stderr, "FAILED: render %d (%s), statistics %ux%u\n", e, why, r.stats_w, r.stats_h);
         failures++;
      }
      rwa_texture_destroy(luma);
      rwa_texture_destroy(chroma);
   }
   /* the failure count: good renders keep it on; ISP_BAD_LIMIT bad ones in a row turn it off, at once and for good */
   isp_account(RWA_ISP_OK, 1000000);
   isp_account(RWA_ISP_E_GPU, 1000000);
   isp_account(RWA_ISP_OK, 1000000);                  /* (resets) */
   isp_account(RWA_ISP_E_SURFACE, 1000000);           /* (neither) */
   if (isp_warm(isp, &f, &p, why, sizeof(why)) != RWA_ISP_OK) {
      fprintf(stderr, "FAILED: off after one failure\n");
      failures++;
   }
   for (int i = 0; i < ISP_BAD_LIMIT - 1; i++)
      isp_account(i ? RWA_ISP_E_GPU : RWA_ISP_OK, ISP_SLOW_NS + 1);    /* (a slow good one counts as bad) */
   if (isp_warm(isp, &f, &p, why, sizeof(why)) != RWA_ISP_OK) {
      fprintf(stderr, "FAILED: off before %u bad renders\n", ISP_BAD_LIMIT);
      failures++;
   }
   isp_account(RWA_ISP_E_GPU, 1000000);
   t0 = now_ms();
   e = isp_warm(isp, &f, &p, why, sizeof(why));
   t1 = now_ms();
   printf("after %u bad renders: %d in %.2f ms (%s)\n", ISP_BAD_LIMIT, e, t1 - t0, why);
   if (e != RWA_ISP_E_GPU || t1 - t0 > 5) {
      fprintf(stderr, "FAILED: not off, or not at once\n");
      failures++;
   }
   rwa_isp_destroy(isp);
   printf("isp_warm_host_test: %s\n", failures ? "FAILED" : "bounded, made off the thread, off after the limit");
   return failures != 0;
}
