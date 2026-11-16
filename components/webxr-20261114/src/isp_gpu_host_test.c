/*
 * Host test of the camera's ISP on the GPU (y2s39, air_isp.c) on softpipe, against the camera bundle's own processor ISP: the SAME frames
 * through camera_isp.c's isp_render_nv12 / isp_render_full_nv12 and through rwa_isp_render_textures, plane by plane, byte by byte.
 * No GPU, no phone, no IOSurface (the planes are two textures here; on the phone they are an IOSurface's planes imported in place).
 * What it cannot show: Panfrost running the same shaders, the frame read out of a buffer, the import of a surface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static int failures, cases;
static int worst_cells, worst_full;
static unsigned long long differing_full, compared_full;

static int
one(struct rwa_isp *gpu, const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t width, uint32_t height,
    unsigned mode, int video_range, const char *name)
{
	char why[512] = "";
	const unsigned origin[3] = { 0, 0, 0 }, ysize[3] = { width, height, 1 }, csize[3] = { (width + 1) / 2, (height + 1) / 2, 1 };
	size_t ystride = width + 5, cstride = csize[0] * 2 + 6;
	uint8_t *y_cpu = calloc(ystride, height), *c_cpu = calloc(cstride, csize[1]), *y_gpu = calloc(ystride, height), *c_gpu = calloc(cstride, csize[1]);
	struct rwa_device *device = rwa_isp_device(gpu);
	struct rwa_texture_shape s = { .pixel_format = 10, .type = 2, .width = width, .height = height, .depth = 1, .array_length = 1,
				       .mip_levels = 1, .sample_count = 1, .usage = 1 | 4 };
	struct rwa_texture *luma = rwa_texture_create(device, &s, why, sizeof(why));
	s.pixel_format = 30; s.width = csize[0]; s.height = csize[1];
	struct rwa_texture *chroma = rwa_texture_create(device, &s, why, sizeof(why));
	int worst = 0, bad = 0;
	unsigned long long differing = 0;

	cases++;
	if (luma == NULL || chroma == NULL) { fprintf(stderr, "FAILED %s: no textures: %s\n", name, why); failures++; return 1; }
	if (mode == RWA_ISP_MODE_FULL)
		isp_render_full_nv12(isp, f, raw, y_cpu, ystride, c_cpu, cstride, 0, height, video_range);
	else
		isp_render_nv12(isp, f, raw, y_cpu, ystride, c_cpu, cstride, width, height, 0, height, video_range);

	struct rwa_isp_frame frame = { .size = sizeof(frame), .width = f->width, .height = f->height, .stride = f->stride, .format = f->format,
				       .bayer = f->bayer, .black = f->black, .raw = raw };
	struct rwa_isp_params params = { .size = sizeof(params), .mode = mode, .wb = { isp->wb[0], isp->wb[1], isp->wb[2] }, .digital = isp->digital,
					 .falloff = isp->falloff, .zoom = isp->zoom, .flip = isp->flip, .video_range = (uint32_t)video_range,
					 .lens_shading = isp->lsc.on, .gamma = isp->gamma };
	struct rwa_isp_result result = { .size = sizeof(result) };
	for (int i = 0; i < 9; i++) params.ccm[i] = isp->ccm[i / 3][i % 3];

	int rc = rwa_isp_render_textures(gpu, &frame, &params, luma, chroma, width, height, 0, &result, why, sizeof(why));
	if (rc != RWA_ISP_OK) { fprintf(stderr, "FAILED %s: render %d: %s\n", name, rc, why); failures++; bad = 1; }
	if (!bad && (rwa_texture_read(luma, 0, 0, origin, ysize, y_gpu, ystride, why, sizeof(why)) != 0 ||
	    rwa_texture_read(chroma, 0, 0, origin, csize, c_gpu, cstride, why, sizeof(why)) != 0)) {
		fprintf(stderr, "FAILED %s: read back: %s\n", name, why); failures++; bad = 1;
	}
	for (uint32_t yy = 0; !bad && yy < height; yy++)
		for (uint32_t x = 0; x < width; x++) {
			int d = abs((int)y_cpu[yy * ystride + x] - (int)y_gpu[yy * ystride + x]);
			worst = d > worst ? d : worst; differing += d != 0;
		}
	for (uint32_t yy = 0; !bad && yy < csize[1]; yy++)
		for (uint32_t x = 0; x < csize[0] * 2; x++) {
			int d = abs((int)c_cpu[yy * cstride + x] - (int)c_gpu[yy * cstride + x]);
			worst = d > worst ? d : worst; differing += d != 0;
		}
	if (!bad) {
		unsigned long long total = (unsigned long long)width * height + (unsigned long long)csize[0] * 2 * csize[1];
		if (mode == RWA_ISP_MODE_FULL) { worst_full = worst > worst_full ? worst : worst_full; differing_full += differing; compared_full += total; }
		else worst_cells = worst > worst_cells ? worst : worst_cells;
		if ((mode == RWA_ISP_MODE_CELLS && worst != 0) || worst > 2) {
			fprintf(stderr, "FAILED %s: %llu of %llu bytes differ, by up to %d\n", name, differing, total, worst); failures++; bad = 1;
		}
		if (getenv("VERBOSE") || width > 1000)
			printf("%-58s %ux%u -> %ux%u: worst %d, %llu of %llu differ; GPU-side %.1f + %.1f + %.1f ms (softpipe)\n", name, f->width, f->height,
			       width, height, worst, differing, total, result.prepare_ns / 1e6, result.queue_ns / 1e6, result.wait_ns / 1e6);
	}
	rwa_texture_destroy(luma); rwa_texture_destroy(chroma);
	free(y_cpu); free(c_cpu); free(y_gpu); free(c_gpu);
	return bad;
}

int
main(void)
{
	char why[512] = "", name[128];
	struct redwood_camera_frame f;
	struct isp isp;

	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_isp *gpu = rwa_isp_create(why, sizeof(why));
	if (gpu == NULL) { fprintf(stderr, "FAILED: no context: %s\n", why); return 1; }
	printf("device: %s, API version %u\n", rwa_device_name(rwa_isp_device(gpu)), rwa_isp_api_version());

	/* 1. every format, Bayer order and range; pictures smaller, odd, and not of the sensor's proportions */
	for (unsigned format = 1; format <= 4; format += 3)
		for (unsigned bayer = 0; bayer < 4; bayer++)
			for (int video = 0; video < 2; video++) {
				static const uint32_t sizes[][2] = { { 94, 70 }, { 95, 71 }, { 128, 40 }, { 60, 100 } };
				uint8_t *raw = make_frame(&f, 256, 192, format, bayer, 64, bayer * 3);
				isp_init(&isp);
				isp.wb[0] = 1.9f; isp.wb[2] = 1.55f; isp.digital = 1.3f; isp.falloff = 0.45f;
				for (unsigned s = 0; s < 4; s++) {
					snprintf(name, sizeof(name), "cells format %u bayer %u %s", format, bayer, video ? "video" : "full");
					one(gpu, &isp, &f, raw, sizes[s][0], sizes[s][1], RWA_ISP_MODE_CELLS, video, name);
				}
				free(raw);
			}
	/* 2. zoom and flips (render_line_window), white balances with each colour the least, no falloff, identity matrix, other black levels */
	{
		static const float zooms[] = { 1.0f, 1.0004f, 1.001f, 1.37f, 2.0f, 2.72f, 5.5f, 40.0f };
		static const float wbs[][3] = { { 2.0f, 1.0f, 1.7f }, { 0.7f, 1.0f, 1.2f }, { 1.3f, 1.0f, 0.6f }, { 1.0f, 1.0f, 1.0f } };
		for (unsigned z = 0; z < 8; z++)
			for (unsigned flip = 0; flip < 4; flip++) {
				uint8_t *raw = make_frame(&f, 320, 240, z & 1 ? 4 : 1, (z + flip) & 3, 16 + 40 * (z % 3), 0);
				isp_init(&isp);
				memcpy(isp.wb, wbs[(z + flip) % 4], sizeof(isp.wb));
				isp.digital = 1.0f + 0.4f * (float)flip; isp.falloff = z & 2 ? 0.0f : 0.8f; isp.zoom = zooms[z]; isp.flip = flip;
				if (z == 5) { static const float id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; isp_set_ccm(&isp, id); }
				snprintf(name, sizeof(name), "cells zoom %.4f flip %u", (double)zooms[z], flip);
				one(gpu, &isp, &f, raw, 150, 112, RWA_ISP_MODE_CELLS, (int)(z & 1), name);
				one(gpu, &isp, &f, raw, 113, 85, RWA_ISP_MODE_CELLS, (int)(flip & 1), name);
				free(raw);
			}
	}
	/* 3. every pixel (the stills' path): both formats, the four orders, the identity matrix too */
	for (unsigned format = 1; format <= 4; format += 3)
		for (unsigned bayer = 0; bayer < 4; bayer++) {
			uint8_t *raw = make_frame(&f, 160, 120, format, bayer, 64, 0);
			isp_init(&isp);
			isp.wb[0] = 1.8f; isp.wb[2] = 1.4f; isp.digital = 1.25f;
			snprintf(name, sizeof(name), "full format %u bayer %u", format, bayer);
			one(gpu, &isp, &f, raw, 160, 120, RWA_ISP_MODE_FULL, (int)(bayer & 1), name);
			if (bayer == 2) {
				static const float id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
				isp_set_ccm(&isp, id);
				one(gpu, &isp, &f, raw, 160, 120, RWA_ISP_MODE_FULL, 0, "full identity matrix");
			}
			free(raw);
		}
	/* 4. refusals */
	{
		uint8_t *raw = make_frame(&f, 64, 48, 4, 0, 64, 0);
		struct rwa_isp_frame frame = { .size = sizeof(frame), .width = 64, .height = 48, .stride = f.stride, .format = 4, .raw = raw };
		struct rwa_isp_params params = { .size = sizeof(params), .wb = { 1, 1, 1 }, .digital = 1, .gamma = isp.gamma, .lens_shading = 1 };
		struct rwa_isp_output out = { .size = sizeof(out), .iosurface = raw, .width = 32, .height = 24 };
		cases++;
		if (rwa_isp_render_textures(gpu, &frame, &params, NULL, NULL, 32, 24, 0, NULL, why, sizeof(why)) != RWA_ISP_E_UNSUPPORTED) { fprintf(stderr, "FAILED: lens shading not refused\n"); failures++; }
		params.lens_shading = 0; frame.format = 2;
		if (rwa_isp_render_textures(gpu, &frame, &params, NULL, NULL, 32, 24, 0, NULL, why, sizeof(why)) != RWA_ISP_E_UNSUPPORTED) { fprintf(stderr, "FAILED: format 2 not refused\n"); failures++; }
		frame.format = 4; params.mode = RWA_ISP_MODE_FULL;
		if (rwa_isp_render_textures(gpu, &frame, &params, NULL, NULL, 32, 24, 0, NULL, why, sizeof(why)) != RWA_ISP_E_UNSUPPORTED) { fprintf(stderr, "FAILED: full at another size not refused\n"); failures++; }
		params.mode = 0;
		if (rwa_isp_render_textures(gpu, &frame, &params, NULL, NULL, 32, 24, 0, NULL, why, sizeof(why)) != RWA_ISP_E_SURFACE) { fprintf(stderr, "FAILED: no planes not refused\n"); failures++; }
		if (rwa_isp_render(gpu, &frame, &params, &out, NULL, why, sizeof(why)) != RWA_ISP_E_SURFACE) { fprintf(stderr, "FAILED: no IOSurface framework not refused\n"); failures++; }
		if (rwa_isp_frame_memory(gpu, 0, 120, 1000, why, sizeof(why)) != NULL) { fprintf(stderr, "FAILED: softpipe gave frame memory\n"); failures++; }
		if (rwa_isp_wait(gpu, why, sizeof(why)) != RWA_ISP_OK) { fprintf(stderr, "FAILED: wait\n"); failures++; }
		free(raw);
	}
	/* 5. the sizes of the phone: the IMX766's frame into the preview (softpipe's time says nothing about the GPU's), unless QUICK */
	if (!getenv("QUICK")) {
		uint8_t *raw = make_frame(&f, 4096, 3072, 4, 3, 64, 0);
		isp_init(&isp);
		isp.wb[0] = 1.95f; isp.wb[2] = 1.6f; isp.digital = 1.1f; isp.falloff = 0.3f;
		one(gpu, &isp, &f, raw, 1504, 1128, RWA_ISP_MODE_CELLS, 0, "IMX766 4096x3072 (stride 5120) preview");
		isp.zoom = 2.0f; isp.flip = ISP_FLIP_H;
		one(gpu, &isp, &f, raw, 1504, 1128, RWA_ISP_MODE_CELLS, 0, "IMX766 preview, zoom 2, flipped");
		free(raw);
	}
	rwa_isp_forget_surfaces(gpu);
	rwa_isp_destroy(gpu);
	printf("isp_gpu_host_test: %d cases, %d failed. Cells mode: worst difference %d (0 is the processor's picture bit for bit). "
	       "Full mode: worst difference %d, %llu of %llu bytes differ\n", cases, failures, worst_cells, worst_full, differing_full, compared_full);
	return failures != 0;
}
