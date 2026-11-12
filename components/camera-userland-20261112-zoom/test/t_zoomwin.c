/*
 * (camera-userland-20261112-zoom) The zoomed picture's new line (render_line_window takes the two lines under the window apart once) against
 * the old one, byte for byte: test/ref/camera_isp_ref.c is camera_isp.c as it was (camera-userland-20261112-cam60), built with its public
 * names given a ref_ prefix (run_tests.sh). Every Bayer order, both packings, zoom 1 to 32, flips, the window moved, odd and even sizes,
 * lens shading on and off, video and full range; then the time of both at 1280x720 and 1920x1080 of a 4096x3072 frame (one thread).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "camera_isp.h"

void ref_isp_render_nv12(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *y, size_t y_stride,
    uint8_t *uv, size_t uv_stride, uint32_t width, uint32_t height, uint32_t first_row, uint32_t end_row, bool video_range);
void ref_isp_render_bgra(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *out, size_t stride,
    uint32_t width, uint32_t height, uint32_t first_row, uint32_t end_row);

static int failures, checks;

static double now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static struct redwood_camera_frame *frame(uint32_t w, uint32_t h, uint32_t bayer, uint32_t format, unsigned seed, uint8_t **raw)
{
	size_t stride = ((size_t)w * 5 / 4 + 63) & ~(size_t)63;
	struct redwood_camera_frame *f = calloc(1, sizeof(*f) + stride * h + 64);

	memcpy(f->magic, REDWOOD_CAMERA_MAGIC, 8);
	f->header_bytes = sizeof(*f);
	f->width = w; f->height = h; f->stride = (uint32_t)stride; f->format = format; f->bayer = bayer; f->black = 64;
	f->sequence = 1; f->exposure_lines = 1000; f->line_ns = 10000; f->gain = 256; f->gain_unity = 256; f->gain_max = 4096;
	f->frame_lines = 3000; f->frame_lines_min = 3000; f->frame_lines_max = 6000; f->margin = 8; f->sensor = 1;
	*raw = (uint8_t *)f + sizeof(*f);
	srand(seed);
	for (size_t i = 0; i < stride * h; i++) {
		(*raw)[i] = (uint8_t)rand();
	}
	return f;
}

static void compare(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t w, uint32_t h, bool video,
    const char *what)
{
	size_t ny = (size_t)w * h, nuv = (size_t)((w + 1) & ~1u) * ((h + 1) / 2);
	uint8_t *y0 = calloc(1, ny), *y1 = calloc(1, ny), *u0 = calloc(1, nuv), *u1 = calloc(1, nuv);
	uint8_t *b0 = calloc(4, ny), *b1 = calloc(4, ny);

	ref_isp_render_nv12(isp, f, raw, y0, w, u0, (w + 1) & ~1u, w, h, 0, h, video);
	isp_render_nv12(isp, f, raw, y1, w, u1, (w + 1) & ~1u, w, h, 0, h, video);
	ref_isp_render_bgra(isp, f, raw, b0, (size_t)w * 4, w, h, 0, h);
	isp_render_bgra(isp, f, raw, b1, (size_t)w * 4, w, h, 0, h);
	checks++;
	if (memcmp(y0, y1, ny) != 0 || memcmp(u0, u1, nuv) != 0 || memcmp(b0, b1, ny * 4) != 0) {
		printf("FAIL: %s (zoom %.3f flip %u shift %.3f,%.3f %ux%u of %ux%u bayer %u format %u lsc %d)\n", what, (double)isp->zoom,
		    isp->flip, (double)isp->shift_x, (double)isp->shift_y, w, h, f->width, f->height, f->bayer, f->format, (int)isp->lsc.on);
		failures++;
	}
	free(y0); free(y1); free(u0); free(u1); free(b0); free(b1);
}

int main(void)
{
	static struct isp isp;
	static const float zooms[] = { 1.0f, 1.0004f, 1.0006f, 1.013f, 1.1f, 1.37f, 1.6f, 2.0f, 2.7f, 3.3f, 7.9f, 32.0f, 40.0f };
	static const float shifts[][2] = { { 0, 0 }, { 0.03f, -0.02f }, { -0.5f, 0.5f }, { 0.0123f, 0.0f } };
	static const uint32_t sizes[][4] = { { 640, 480, 320, 240 }, { 642, 482, 321, 239 }, { 4096, 3072, 1280, 720 }, { 3264, 2448, 1504, 1128 },
		{ 64, 48, 640, 480 }, { 8, 6, 33, 17 } };

	for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
		for (uint32_t bayer = 0; bayer < 4; bayer++) {
			for (uint32_t format = 0; format < 2; format++) {
				uint8_t *raw;
				uint32_t fmt = format ? REDWOOD_CAMERA_FORMAT_LE40_RAW10 : 1;
				struct redwood_camera_frame *f = frame(sizes[si][0], sizes[si][1], bayer, fmt, si * 8 + bayer * 2 + format, &raw);

				for (unsigned zi = 0; zi < sizeof(zooms) / sizeof(zooms[0]); zi++) {
					for (uint32_t flip = 0; flip < 4; flip++) {
						for (unsigned sh = 0; sh < 4; sh++) {
							/* (the big frames: fewer combinations, they are slow under the sanitizers) */
							if (sizes[si][0] >= 3000 && (sh > 1 || (flip != 0 && flip != 3) || zi % 3 != 0)) {
								continue;
							}
							isp_init(&isp);
							isp.zoom = zooms[zi];
							isp.flip = flip;
							isp.shift_x = shifts[sh][0];
							isp.shift_y = shifts[sh][1];
							isp.lsc.on = (zi + flip + sh) % 5 == 0;
							if (isp.lsc.on) {
								for (int c = 0; c < 3; c++) {
									for (int yy = 0; yy < LSC_H; yy++) {
										for (int xx = 0; xx < LSC_W; xx++) {
											isp.lsc.gain[c][yy][xx] = 1.0f + 0.01f * (float)((xx * 7 + yy * 3 + c) % 40);
										}
									}
								}
							}
							compare(&isp, f, raw, sizes[si][2], sizes[si][3], (zi + sh) & 1, "window");
						}
					}
				}
				free(f);
			}
		}
	}
	printf("t_zoomwin: %d pictures compared, %d differ\n", checks, failures);
	if (getenv("ZOOMWIN_TIME") != NULL) {
		static const uint32_t outs[][2] = { { 1280, 720 }, { 1920, 1080 } };
		uint8_t *raw;
		struct redwood_camera_frame *f = frame(4096, 3072, 3, REDWOOD_CAMERA_FORMAT_LE40_RAW10, 7, &raw);
		uint8_t *y = malloc(1920 * 1080), *uv = malloc(1920 * 541);

		for (size_t i = 0; i < (size_t)f->stride * f->height; i++) {
			raw[i] = (uint8_t)(64 + rand() % 128);     /* (a scene of middle greys: no pixel near white) */
		}

		for (unsigned o = 0; o < 2; o++) {
			for (int k = 0; k < 3; k++) {
				float z = k == 0 ? 1.0f : k == 1 ? 1.1f : 2.0f;
				double best[2] = { 1e9, 1e9 };

				isp_init(&isp);
				isp.zoom = z;
				for (int r = 0; r < 5; r++) {
					double t0 = now_ms();

					ref_isp_render_nv12(&isp, f, raw, y, outs[o][0], uv, outs[o][0], outs[o][0], outs[o][1], 0, outs[o][1], false);
					double t1 = now_ms();

					isp_render_nv12(&isp, f, raw, y, outs[o][0], uv, outs[o][0], outs[o][0], outs[o][1], 0, outs[o][1], false);
					double t2 = now_ms();

					best[0] = t1 - t0 < best[0] ? t1 - t0 : best[0];
					best[1] = t2 - t1 < best[1] ? t2 - t1 : best[1];
				}
				printf("t_zoomwin: %ux%u of 4096x3072, zoom %.1f: before %.1f ms, now %.1f ms (one thread, best of 5)\n", outs[o][0],
				    outs[o][1], (double)z, best[0], best[1]);
			}
		}
	}
	return failures != 0;
}
