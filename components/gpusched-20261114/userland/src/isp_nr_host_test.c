/*
 * y2s70 NR2: isp_nr_host_test -- the noise reduction on the GPU (air_isp.c isp_nr_planes, rwa_isp_params denoise/denoise_before) on softpipe,
 * against the bundle's own processor code (nr_reference.inc, cut out of rwcam_render.c: smooth(), with_before()) and denoise()'s order:
 * a sequence of frames (noise over a gradient, a square that moves), levels 1 and 2, with and without the picture before, two sizes; every
 * byte of both planes must be the processor's. Below: the description of isp_gpu_host_test.c, whose frame maker this is.
 * Host test of the camera's ISP on the GPU (y2s39, air_isp.c) on softpipe, against the camera bundle's own processor ISP: the SAME frames
 * through camera_isp.c's isp_render_nv12 / isp_render_full_nv12 and through rwa_isp_render_textures, plane by plane, byte by byte.
 * No GPU, no phone, no IOSurface (the planes are two textures here; on the phone they are an IOSurface's planes imported in place).
 * What it cannot show: Panfrost running the same shaders, the frame read out of a buffer, the import of a surface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static struct { void (*apply)(size_t n, void *context, void (*work)(void *, size_t)); } R;      /* (the reference runs its pieces here) */
#include "nr_reference.inc"
static int failures, cases;

/* a frame: make_frame()'s, with a bright square at (sx, sy) */
static uint8_t *
frame_with_square(struct redwood_camera_frame *f, uint32_t w, uint32_t h, uint32_t sx, uint32_t sy, unsigned long long seed)
{
	rng = seed;
	uint8_t *raw = make_frame(f, w, h, 4, 3, 64, 0);
	for (uint32_t y = sy; y < sy + h / 6 && y < h; y++)
		for (uint32_t x = sx & ~3u; x < sx + w / 6 && x + 3 < w; x += 4) {
			uint64_t v = 700 + (rnd() % 40), word = v | v << 10 | v << 20 | v << 30;
			for (int i = 0; i < 5; i++) raw[(size_t)y * f->stride + (x / 4) * 5 + i] = (uint8_t)(word >> (8 * i));
		}
	return raw;
}

static void
sequence(struct rwa_isp *gpu, uint32_t fw, uint32_t fh, uint32_t pw, uint32_t ph, int level, int with_before_on)
{
	char why[512] = "", name[96];
	struct rwa_device *device = rwa_isp_device(gpu);
	struct rwa_texture_shape s = { .pixel_format = 10, .type = 2, .width = pw, .height = ph, .depth = 1, .array_length = 1, .mip_levels = 1,
				       .sample_count = 1, .usage = 1 | 4 };
	struct rwa_texture *luma = rwa_texture_create(device, &s, why, sizeof(why));
	uint32_t cw = (pw + 1) & ~1u, ch = (ph + 1) / 2;
	s.pixel_format = 30; s.width = cw / 2; s.height = ch;
	struct rwa_texture *chroma = rwa_texture_create(device, &s, why, sizeof(why));
	uint8_t *y = malloc((size_t)pw * ph), *uv = malloc((size_t)cw * ch), *gy = malloc((size_t)pw * ph), *guv = malloc((size_t)cw * ch);
	uint8_t *by = malloc((size_t)pw * ph), *buv = malloc((size_t)cw * ch);
	bool have_before = false;
	const unsigned origin[3] = { 0, 0, 0 }, ys[3] = { pw, ph, 1 }, cs[3] = { cw / 2, ch, 1 };
	struct isp isp;

	isp_init(&isp);
	snprintf(name, sizeof(name), "%ux%u -> %ux%u level %d%s", fw, fh, pw, ph, level, with_before_on ? " with the picture before" : "");
	for (int k = 0; k < 6; k++) {
		struct redwood_camera_frame f;
		uint8_t *raw = frame_with_square(&f, fw, fh, fw / 8 + (uint32_t)k * fw / 30, fh / 3, 1000 + (unsigned long long)k * 7);
		cases++;
		/* the processor: the picture, then denoise() as rwcam_render.c does it */
		isp_render_nv12(&isp, &f, raw, y, pw, uv, cw, pw, ph, 0, ph, true);
		if (with_before_on && !have_before) { memcpy(by, y, (size_t)pw * ph); memcpy(buv, uv, (size_t)cw * ch); have_before = true; }
		for (int pass = 0; pass < level; pass++) smooth(uv, cw, cw, ch, 2, NULL, 0);
		if (level == 2) smooth(y, pw, pw, ph, 1, NULL, 16);
		if (with_before_on) with_before(y, pw, uv, cw, pw, ph, by, buv, level);
		/* the GPU */
		struct rwa_isp_frame frame = { .size = sizeof(frame), .width = f.width, .height = f.height, .stride = f.stride, .format = f.format,
					       .bayer = f.bayer, .black = f.black, .raw = raw };
		struct rwa_isp_params params = { .size = sizeof(params), .mode = RWA_ISP_MODE_CELLS, .wb = { isp.wb[0], isp.wb[1], isp.wb[2] },
						 .digital = isp.digital, .falloff = isp.falloff, .zoom = isp.zoom, .flip = isp.flip, .video_range = 1,
						 .gamma = isp.gamma, .denoise = (uint32_t)level, .denoise_before = (uint32_t)with_before_on };
		for (int i = 0; i < 9; i++) params.ccm[i] = isp.ccm[i / 3][i % 3];
		int rc = rwa_isp_render_textures(gpu, &frame, &params, luma, chroma, pw, ph, 0, NULL, why, sizeof(why));
		if (rc != RWA_ISP_OK || rwa_texture_read(luma, 0, 0, origin, ys, gy, pw, why, sizeof(why)) != 0 ||
		    rwa_texture_read(chroma, 0, 0, origin, cs, guv, cw, why, sizeof(why)) != 0) {
			fprintf(stderr, "FAILED %s frame %d: render %d: %s\n", name, k, rc, why); failures++; free(raw); break;
		}
		size_t dy = 0, dc = 0; int worst = 0;
		for (size_t i = 0; i < (size_t)pw * ph; i++) if (gy[i] != y[i]) { dy++; worst = abs(gy[i] - y[i]) > worst ? abs(gy[i] - y[i]) : worst; }
		for (size_t i = 0; i < (size_t)cw * ch; i++) if (guv[i] != uv[i]) { dc++; worst = abs(guv[i] - uv[i]) > worst ? abs(guv[i] - uv[i]) : worst; }
		if (dy || dc) { fprintf(stderr, "FAILED %s frame %d: %zu brightness and %zu colour bytes differ (worst %d)\n", name, k, dy, dc, worst); failures++; }
		free(raw);
	}
	printf("  %s: 6 frames compared\n", name);
	rwa_texture_destroy(luma); rwa_texture_destroy(chroma);
	free(y); free(uv); free(gy); free(guv); free(by); free(buv);
}

int
main(void)
{
	char why[512] = "";
	setenv("RWA_SOFTWARE", "1", 1);
	for (int level = 1; level <= 2; level++)
		for (int wb = 0; wb <= 1; wb++) {
			struct rwa_isp *gpu = rwa_isp_create(why, sizeof(why));      /* (a context of its own: no picture before from another run) */
			if (gpu == NULL) { fprintf(stderr, "FAILED: no context: %s\n", why); return 1; }
			sequence(gpu, 640, 480, 320, 240, level, wb);
			sequence(gpu, 1024, 768, 642, 482, level, wb);          /* (a size not a multiple of 8, odd halves) */
			rwa_isp_destroy(gpu);
		}
	printf("isp_nr_host_test: %d frames, %s\n", cases, failures ? "FAILED" : "every byte the processor's");
	return failures != 0;
}
