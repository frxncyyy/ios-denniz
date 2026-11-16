/*
 * y2s65: isp_stats_host_test -- the frame's statistics on the GPU (air_isp.c isp_stats_shader, struct rwa_isp_output's stats) on softpipe,
 * against camera_isp.c's isp_measure() of the same frame: the blocks summed must give its numbers EXACTLY (cells, the sums of mean, fair,
 * grey, all 64 histogram bins, bright), and the sharpness from the blocks must be near isp_sharpness() (another sampling of the same
 * measure). Frames: both formats, the four Bayer orders, three black levels, the phone's sizes (4096x3072, 2048x1152, 4096x2304,
 * 3264x2464; QUICK=1: up to 2048x1152). Below: the description of isp_gpu_host_test.c, whose frame maker this is.
 * Host test of the camera's ISP on the GPU (y2s39, air_isp.c) on softpipe, against the camera bundle's own processor ISP: the SAME frames
 * through camera_isp.c's isp_render_nv12 / isp_render_full_nv12 and through rwa_isp_render_textures, plane by plane, byte by byte.
 * No GPU, no phone, no IOSurface (the planes are two textures here; on the phone they are an IOSurface's planes imported in place).
 * What it cannot show: Panfrost running the same shaders, the frame read out of a buffer, the import of a surface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include "camera_isp.h"
#include "air_metal.h"
#include "rwa_isp_api.h"

int rwa_isp_render_textures(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
			    struct rwa_texture *luma, struct rwa_texture *chroma, uint32_t width, uint32_t height, uint32_t flags,
			    struct rwa_isp_result *result, char *why, size_t why_size);
struct rwa_device *rwa_isp_device(struct rwa_isp *isp);
/* the library's calls, linked here (the bundle looks them up: RWA_ISP_LOAD) */
unsigned rwa_isp_api_version(void);
struct rwa_isp *rwa_isp_create(char *why, size_t why_size);
void rwa_isp_destroy(struct rwa_isp *isp);
void *rwa_isp_frame_memory(struct rwa_isp *isp, unsigned slot, size_t header_bytes, size_t frame_bytes, char *why, size_t why_size);
int rwa_isp_render(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
		   const struct rwa_isp_output *output, struct rwa_isp_result *result, char *why, size_t why_size);
int rwa_isp_wait(struct rwa_isp *isp, char *why, size_t why_size);
void rwa_isp_forget_surfaces(struct rwa_isp *isp);

static unsigned long long rng = 88172645463325252ull;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng >> 16); }

/* a frame of noise over a gradient, with a burnt-out patch and a black one, packed as the kernel packs it */
static uint8_t *
make_frame(struct redwood_camera_frame *f, uint32_t width, uint32_t height, uint32_t format, uint32_t bayer, uint32_t black, uint32_t extra)
{
	memset(f, 0, sizeof(*f));
	memcpy(f->magic, REDWOOD_CAMERA_MAGIC, 8);
	f->header_bytes = sizeof(*f); f->width = width; f->height = height; f->stride = (width + 3) / 4 * 5 + extra; f->format = format;
	f->bayer = bayer; f->black = black;
	uint8_t *raw = calloc(f->stride, height);
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x += 4) {
			unsigned v[4];
			uint8_t *g = raw + (size_t)y * f->stride + (x / 4) * 5;
			for (int i = 0; i < 4; i++) {
				unsigned base = (x + i) * 900 / width + (y * 300 / height);
				v[i] = base / 2 + rnd() % 600;
				if ((x + i) > width * 3 / 5 && (x + i) < width * 4 / 5 && y > height / 5 && y < height / 2) v[i] = 1000 + rnd() % 24;   /* burnt out */
				if ((x + i) < width / 6 && y > height * 2 / 3) v[i] = rnd() % (black + 8);                                          /* black */
				if ((x + i) > width / 3 && (x + i) < width / 2 && y < height / 6) v[i] = (i + y) & 1 ? 1023 : 850 + rnd() % 100;      /* one colour clipped */
				v[i] = v[i] > 1023 ? 1023 : v[i];
			}
			if (format == REDWOOD_CAMERA_FORMAT_LE40_RAW10) {
				uint64_t w = (uint64_t)v[0] | (uint64_t)v[1] << 10 | (uint64_t)v[2] << 20 | (uint64_t)v[3] << 30;
				for (int i = 0; i < 5; i++) g[i] = (uint8_t)(w >> (8 * i));
			} else {
				for (int i = 0; i < 4; i++) g[i] = (uint8_t)(v[i] >> 2);
				g[4] = (uint8_t)((v[0] & 3) | (v[1] & 3) << 2 | (v[2] & 3) << 4 | (v[3] & 3) << 6);
			}
		}
		for (uint32_t i = (width + 3) / 4 * 5; i < f->stride; i++) raw[(size_t)y * f->stride + i] = (uint8_t)rnd();   /* the padding is not read */
	}
	return raw;
}

void rwa_isp_want_stats(struct rwa_isp *isp, float *stats, uint32_t floats);
static int failures, cases;
static double worst_ratio = 1.0;

static int
stats_case(struct rwa_isp *gpu, uint32_t w, uint32_t h, uint32_t format, uint32_t bayer, uint32_t black)
{
	char why[512] = "", name[96];
	struct redwood_camera_frame f;
	uint8_t *raw = make_frame(&f, w, h, format, bayer, black, format == 4 ? 0 : 11);
	struct isp isp;
	struct isp_stats cpu;
	const uint32_t pw = 64, ph = 48;
	struct rwa_device *device = rwa_isp_device(gpu);
	struct rwa_texture_shape s = { .pixel_format = 10, .type = 2, .width = pw, .height = ph, .depth = 1, .array_length = 1, .mip_levels = 1,
				       .sample_count = 1, .usage = 1 | 4 };
	struct rwa_texture *luma = rwa_texture_create(device, &s, why, sizeof(why));
	s.pixel_format = 30; s.width = pw / 2; s.height = ph / 2;
	struct rwa_texture *chroma = rwa_texture_create(device, &s, why, sizeof(why));
	uint32_t floats = 64u * 1024 * 4;
	float *st = calloc(floats, sizeof(float));
	int bad = 0;

	snprintf(name, sizeof(name), "%ux%u format %u bayer %u black %u", w, h, format, bayer, black);
	cases++;
	isp_init(&isp);
	isp_measure(&f, raw, &cpu);
	struct rwa_isp_frame frame = { .size = sizeof(frame), .width = f.width, .height = f.height, .stride = f.stride, .format = f.format,
				       .bayer = f.bayer, .black = f.black, .raw = raw };
	struct rwa_isp_params params = { .size = sizeof(params), .mode = RWA_ISP_MODE_CELLS, .wb = { isp.wb[0], isp.wb[1], isp.wb[2] },
					 .digital = isp.digital, .falloff = isp.falloff, .zoom = 1, .gamma = isp.gamma };
	struct rwa_isp_result result = { .size = sizeof(result) };
	for (int i = 0; i < 9; i++) params.ccm[i] = isp.ccm[i / 3][i % 3];
	rwa_isp_want_stats(gpu, st, floats);
	int rc = rwa_isp_render_textures(gpu, &frame, &params, luma, chroma, pw, ph, 0, &result, why, sizeof(why));
	if (rc != RWA_ISP_OK || result.stats_w == 0) { fprintf(stderr, "FAILED %s: render %d, stats %ux%u: %s\n", name, rc, result.stats_w, result.stats_h, why); failures++; bad = 1; }
	double sum[3][4] = { { 0 } }, hist[64] = { 0 }, edges = 0, light = 0, n = 0;
	for (uint32_t bi = 0; !bad && bi < result.stats_w * result.stats_h; bi++) {
		const float *k = st + (size_t)bi * RWA_ISP_STATS_KINDS * 4;
		for (int t = 0; t < 3; t++) for (int c = 0; c < 4; c++) sum[t][c] += k[t * 4 + c];
		for (int b = 0; b < 64; b++) hist[b] += k[(3 + b / 4) * 4 + b % 4];
		edges += k[19 * 4]; light += k[19 * 4 + 1]; n += k[19 * 4 + 2];
	}
	if (!bad) {
		int bright = 0;
		for (int i = 63, above = 0; i >= 0; i--) { above += (int)hist[i]; if (above * 100 >= (int)sum[0][3]) { bright = i * 16 + 8; break; } }
		bool same = (uint32_t)sum[0][3] == cpu.cells && (uint32_t)sum[1][3] == cpu.fair_cells && (uint32_t)sum[2][3] == cpu.grey_cells &&
			    bright == (int)cpu.bright;
		for (int c = 0; c < 3; c++) {
			same = same && (cpu.cells == 0 || (long long)sum[0][c] == (long long)(cpu.mean[c] * cpu.cells + 0.5));
			same = same && (cpu.fair_cells == 0 || (long long)sum[1][c] == (long long)(cpu.fair[c] * cpu.fair_cells + 0.5));
			same = same && (cpu.grey_cells == 0 || (long long)sum[2][c] == (long long)(cpu.grey[c] * cpu.grey_cells + 0.5));
		}
		for (int b = 0; b < 64; b++) same = same && (uint32_t)hist[b] == cpu.hist[b];
		double gs = n > 0 && light >= n * 16.0 ? edges / n / ((light / n) * (light / n)) * 1e4 : 0;
		double cs = isp_sharpness(&f, raw, 0, 0, w, h), ratio = cs > 0 ? gs / cs : (gs == 0 ? 1 : 99);
		if (fabs(log(ratio)) > fabs(log(worst_ratio))) worst_ratio = ratio;
		if (!same || ratio < 0.7 || ratio > 1.43) {
			fprintf(stderr, "FAILED %s: cells %.0f/%u fair %.0f/%u grey %.0f/%u bright %d/%.0f; sharpness GPU %.3f CPU %.3f\n", name,
				sum[0][3], cpu.cells, sum[1][3], cpu.fair_cells, sum[2][3], cpu.grey_cells, bright, cpu.bright, gs, cs);
			failures++;
		} else {
			printf("  %s: %ux%u blocks of step %u, %u cells, exact; sharpness %.3f against %.3f\n", name, result.stats_w, result.stats_h,
			       result.stats_step, cpu.cells, gs, cs);
		}
	}
	rwa_texture_destroy(luma); rwa_texture_destroy(chroma); free(st); free(raw);
	return bad;
}

int
main(void)
{
	char why[512] = "";
	setenv("RWA_SOFTWARE", "1", 1);           /* (softpipe, as isp_gpu_host_test) */
	struct rwa_isp *gpu = rwa_isp_create(why, sizeof(why));
	if (gpu == NULL) { fprintf(stderr, "FAILED: no context: %s\n", why); return 1; }
	stats_case(gpu, 640, 480, 4, 3, 64);
	stats_case(gpu, 640, 480, 1, 0, 64);
	stats_case(gpu, 1000, 600, 4, 1, 16);
	stats_case(gpu, 520, 392, 1, 2, 0);
	stats_case(gpu, 2048, 1152, 4, 3, 64);           /* q10jm's 60/s mode */
	if (!getenv("QUICK")) {
		stats_case(gpu, 4096, 3072, 4, 3, 64);   /* the main camera, step 8: 49 152 cells */
		stats_case(gpu, 4096, 2304, 4, 3, 64);
		stats_case(gpu, 3264, 2464, 4, 0, 64);
	}
	rwa_isp_destroy(gpu);
	printf("isp_stats_host_test: %d cases, %s (sharpness from the blocks within %.2fx of the processor's)\n", cases, failures ? "FAILED" : "all exact",
	       worst_ratio);
	return failures != 0;
}
