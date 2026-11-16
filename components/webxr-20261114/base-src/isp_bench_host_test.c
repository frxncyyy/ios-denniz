/*
 * y2s75 libmerge4: isp_bench_host_test -- per-stage timings of the camera's GPU render (rwa_isp_render_textures, the statistics asked as the
 * bundle asks them) on softpipe, for the picture sizes of the phone's streams, from a 4096x3072 RAW10 frame as the wide camera gives it:
 *   1504x1128 cells, noise 2 with the picture before   (photo preview)
 *   1920x1080 cells, noise 2 with the picture before   (1080p video)
 *   3840x2160 cells, noise 2                           (4K video: the bundle asks the picture before only up to 4 MP)
 *   4096x3072 full,  noise 2                           (the still)
 * Built against the air_metal.c of the library given (bench-host-test.sh SRC), so that libmerge3 and libmerge4 are timed by the same program.
 * softpipe is the processor drawing every pass: its time is the passes' work (fragments, texel reads) and the library's own processor time,
 * not the Mali's; what it shows is how much work each render queues, and prepare / readback times of the processor's side.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include "air_metal.h"
#include "rwa_isp_api.h"

int rwa_isp_render_textures(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
			    struct rwa_texture *luma, struct rwa_texture *chroma, uint32_t width, uint32_t height, uint32_t flags,
			    struct rwa_isp_result *result, char *why, size_t why_size);
struct rwa_device *rwa_isp_device(struct rwa_isp *isp);
void rwa_isp_want_stats(struct rwa_isp *isp, float *stats, uint32_t floats);
struct rwa_isp *rwa_isp_create(char *why, size_t why_size);
void rwa_isp_destroy(struct rwa_isp *isp);

static double
now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* the process's processor time: what softpipe spent, without the time the process was stopped (the build machine's thermal guard
 * freezes it at 85 C) */
static double
cpu_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static unsigned long long rng = 88172645463325252ull;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng >> 16); }

int
main(int argc, char **argv)
{
	const char *label = argc > 1 ? argv[1] : "?";
	int iters = getenv("BENCH_ITERS") ? atoi(getenv("BENCH_ITERS")) : 3;
	unsigned only = getenv("BENCH_CASES") ? (unsigned)strtoul(getenv("BENCH_CASES"), NULL, 0) : ~0u;     /* a mask of the cases below */
	char why[512] = "";
	static const struct { uint32_t w, h, mode, level, before; const char *name; } cases[] = {
		{ 1504, 1128, RWA_ISP_MODE_CELLS, 2, 1, "1504x1128 preview" },
		{ 1920, 1080, RWA_ISP_MODE_CELLS, 2, 1, "1920x1080 video  " },
		{ 3840, 2160, RWA_ISP_MODE_CELLS, 2, 0, "3840x2160 video  " },
		{ 4096, 3072, RWA_ISP_MODE_FULL,  2, 0, "4096x3072 still  " },
	};
	const uint32_t fw = 4096, fh = 3072, stride = fw / 4 * 5;
	uint8_t *raw = malloc((size_t)stride * fh), gamma[4096];
	static float stats[1 << 20];

	setenv("RWA_SOFTWARE", "1", 1);
	for (size_t i = 0; i < (size_t)stride * fh; i++)
		raw[i] = (uint8_t)(rnd() & 0xff);
	for (int i = 0; i < 4096; i++)
		gamma[i] = (uint8_t)(i * 255 / 4095);
	printf("isp_bench_host_test (%s), softpipe, frame %ux%u RAW10 (%zu bytes), %d timed renders a case after one untimed\n", label, fw, fh,
	       (size_t)stride * fh, iters);
	printf("%-18s %9s %9s %9s %9s %9s %9s\n", "case", "prepare", "queue", "wait", "after", "total ms", "cpu ms");
	for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		if (!(only >> c & 1))
			continue;
		struct rwa_isp *isp = rwa_isp_create(why, sizeof(why));
		if (isp == NULL) { fprintf(stderr, "no context: %s\n", why); return 1; }
		struct rwa_device *device = rwa_isp_device(isp);
		uint32_t pw = cases[c].w, ph = cases[c].h;
		struct rwa_texture_shape s = { .pixel_format = 10, .type = 2, .width = pw, .height = ph, .depth = 1, .array_length = 1, .mip_levels = 1,
					       .sample_count = 1, .usage = 1 | 4 };
		struct rwa_texture *luma = rwa_texture_create(device, &s, why, sizeof(why));
		s.pixel_format = 30; s.width = (pw + 1) / 2; s.height = (ph + 1) / 2;
		struct rwa_texture *chroma = rwa_texture_create(device, &s, why, sizeof(why));
		struct rwa_isp_frame frame = { .size = sizeof(frame), .width = fw, .height = fh, .stride = stride, .format = 1, .bayer = 0, .black = 64,
					       .raw = raw };
		struct rwa_isp_params params = { .size = sizeof(params), .mode = cases[c].mode, .wb = { 1.6f, 1.0f, 2.1f }, .digital = 1.2f,
						 .ccm = { 1.5f, -0.3f, -0.2f, -0.2f, 1.4f, -0.2f, -0.1f, -0.4f, 1.5f }, .falloff = 0.3f, .zoom = 1.0f,
						 .video_range = 1, .gamma = gamma, .denoise = cases[c].level, .denoise_before = cases[c].before };
		double sp = 0, sq = 0, sw = 0, sa = 0, st = 0, sc = 0;
		int ok = 0;
		for (int k = 0; k <= iters; k++) {
			struct rwa_isp_result r = { .size = sizeof(r) };
			raw[(size_t)k * 977] ^= 0x55;            /* (another frame, as far as anything could tell) */
			rwa_isp_want_stats(isp, stats, sizeof(stats) / sizeof(stats[0]));
			double t0 = now_ms(), c0 = cpu_ms();
			int e = rwa_isp_render_textures(isp, &frame, &params, luma, chroma, pw, ph, 0, &r, why, sizeof(why));
			double t = now_ms() - t0, cpu = cpu_ms() - c0;
			if (e != RWA_ISP_OK) { fprintf(stderr, "%s: render %d: %s\n", cases[c].name, e, why); return 1; }
			if (k == 0)
				continue;
			double p = r.prepare_ns / 1e6, q = r.queue_ns / 1e6, w = r.wait_ns / 1e6;
			sp += p; sq += q; sw += w; sa += t - p - q - w; st += t; sc += cpu; ok++;
		}
		printf("%-18s %9.2f %9.2f %9.2f %9.2f %9.2f %9.2f\n", cases[c].name, sp / ok, sq / ok, sw / ok, sa / ok, st / ok, sc / ok);
		fflush(stdout);
		rwa_texture_destroy(luma);
		rwa_texture_destroy(chroma);
		rwa_isp_destroy(isp);
	}
	free(raw);
	return 0;
}
