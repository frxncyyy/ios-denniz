/*
 * Host test of rwa_texture_convert_ycbcr (y2s38) on softpipe: no GPU, no phone.  softpipe has no two-plane format, so the source is an RGBA
 * texture holding Y, Cb, Cr in r, g, b -- which is what sampling MTLPixelFormatYCBCR8_420_2P gives on the Mali.  What this shows: the two NIR
 * shaders are valid and do the arithmetic, the pass leaves row 0 on row 0, and the library's other calls still work around it.  What it cannot
 * show: Panfrost's compile of the same shaders, the two-plane sampler view, the batch ordering on the GPU.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "air_metal.h"

static void
reference(int y, int cb, int cr, int matrix, int full, int out[3])      /* B, G, R as nord2_ycbcr_test.c */
{
	static const double k[3][2] = { { 0.299, 0.114 }, { 0.2126, 0.0722 }, { 0.2627, 0.0593 } };
	const double kr = k[matrix][0], kb = k[matrix][1], kg = 1.0 - kr - kb;
	double Y = full ? y / 255.0 : (y < 16 ? 0.0 : (y - 16) / 219.0), U = (cb - 128) / (full ? 255.0 : 224.0), V = (cr - 128) / (full ? 255.0 : 224.0);
	double c[3] = { Y + 2.0 * (1.0 - kb) * U, Y - 2.0 * kb * (1.0 - kb) / kg * U - 2.0 * kr * (1.0 - kr) / kg * V, Y + 2.0 * (1.0 - kr) * V };

	for (int i = 0; i < 3; i++) {
		double v = floor(c[i] * 255.0 + 0.5);
		out[i] = v < 0 ? 0 : v > 255 ? 255 : (int)v;
	}
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED line %d: %s (%s)\n", __LINE__, #x, why); return 1; } } while (0)

int
main(void)
{
	enum { W = 67, H = 41 };
	char why[512] = "";
	static unsigned char src_px[H][W][4], dst_px[H][W][4];
	const unsigned origin[3] = { 0, 0, 0 }, size[3] = { W, H, 1 };
	int worst = 0;

	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *device = rwa_device_open(why, sizeof(why));
	CHECK(device != NULL);
	printf("device: %s\n", rwa_device_name(device));

	struct rwa_texture_shape s = { .pixel_format = 70 /* RGBA8Unorm */, .type = 2 /* 2D */, .width = W, .height = H, .depth = 1,
				       .array_length = 1, .mip_levels = 1, .sample_count = 1, .usage = 1 };
	struct rwa_texture *src = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(src != NULL);
	s.pixel_format = 80; /* BGRA8Unorm */
	s.usage = 1 | 4;
	struct rwa_texture *dst = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(dst != NULL);
	s.usage = 1;
	struct rwa_texture *not_target = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(not_target != NULL);

	/* the bars of the camera bundle in the first rows (white, black: video range), then everything else, different in every pixel */
	srand(11);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			unsigned char *p = src_px[y][x];
			if (y == 0) { p[0] = x & 1 ? 235 : 16; p[1] = 128; p[2] = 128; }
			else if (y == 1) { p[0] = x & 1 ? 255 : 0; p[1] = 128; p[2] = 128; }
			else { p[0] = (unsigned char)rand(); p[1] = (unsigned char)rand(); p[2] = (unsigned char)rand(); }
			p[3] = (unsigned char)rand();          /* alpha of the source is not looked at */
		}
	}
	CHECK(rwa_texture_write(src, 0, 0, origin, size, src_px, W * 4, why, sizeof(why)) == 0);

	/* refusals, each with a sentence and without drawing */
	CHECK(rwa_texture_convert_ycbcr(dst, src, 3, 0, why, sizeof(why)) == -1 && why[0]);
	CHECK(rwa_texture_convert_ycbcr(not_target, src, 0, 0, why, sizeof(why)) == -1 && strstr(why, "rendered to"));
	CHECK(rwa_texture_convert_ycbcr(dst, dst, 0, 0, why, sizeof(why)) == -1);
	CHECK(rwa_texture_convert_ycbcr(NULL, src, 0, 0, why, sizeof(why)) == -1);

	for (int round = 0; round < 2; round++) {              /* twice: the second time the state and the shaders are the kept ones */
		for (int matrix = 0; matrix < 3; matrix++) {
			for (int full = 0; full < 2; full++) {
				memset(dst_px, 0x5a, sizeof(dst_px));
				why[0] = 0;
				CHECK(rwa_texture_convert_ycbcr(dst, src, matrix, full, why, sizeof(why)) == 0);
				CHECK(rwa_finish(device, why, sizeof(why)) == 0);
				CHECK(rwa_texture_read(dst, 0, 0, origin, size, dst_px, W * 4, why, sizeof(why)) == 0);
				for (int y = 0; y < H; y++) {
					for (int x = 0; x < W; x++) {
						int want[3];
						reference(src_px[y][x][0], src_px[y][x][1], src_px[y][x][2], matrix, full, want);
						for (int c = 0; c < 3; c++) {
							int d = abs((int)dst_px[y][x][c] - want[c]);
							if (d > 1) {
								fprintf(stderr, "FAILED matrix %d full %d at (%d,%d) channel %d: %d, want %d\n", matrix, full, x, y, c,
									dst_px[y][x][c], want[c]);
								return 1;
							}
							worst = d > worst ? d : worst;
						}
						if (dst_px[y][x][3] != 255) { fprintf(stderr, "FAILED alpha at (%d,%d)\n", x, y); return 1; }
					}
				}
				if (!full) {        /* the bars: exactly white and black */
					CHECK(dst_px[0][1][0] == 255 && dst_px[0][1][1] == 255 && dst_px[0][1][2] == 255);
					CHECK(dst_px[0][0][0] == 0 && dst_px[0][0][1] == 0 && dst_px[0][0][2] == 0);
				} else {
					CHECK(dst_px[1][1][0] == 255 && dst_px[1][1][1] == 255 && dst_px[1][1][2] == 255);
					CHECK(dst_px[1][0][0] == 0 && dst_px[1][0][1] == 0 && dst_px[1][0][2] == 0);
				}
			}
		}
	}
	/* the library's own calls after the pass: a write and a read of the destination, a copy out of it */
	memset(dst_px, 0x33, sizeof(dst_px));
	CHECK(rwa_texture_write(dst, 0, 0, origin, size, dst_px, W * 4, why, sizeof(why)) == 0);
	CHECK(rwa_texture_copy(not_target, 0, 0, origin, dst, 0, 0, origin, size, why, sizeof(why)) == 0);
	CHECK(rwa_finish(device, why, sizeof(why)) == 0);
	memset(dst_px, 0, sizeof(dst_px));
	CHECK(rwa_texture_read(not_target, 0, 0, origin, size, dst_px, W * 4, why, sizeof(why)) == 0);
	CHECK(dst_px[H - 1][W - 1][2] == 0x33);
	/* and a conversion straight after, then everything destroyed in the order a driver would */
	CHECK(rwa_texture_convert_ycbcr(dst, src, 0, 1, why, sizeof(why)) == 0);
	rwa_texture_destroy(src);
	CHECK(rwa_finish(device, why, sizeof(why)) == 0);
	rwa_texture_destroy(dst);
	rwa_texture_destroy(not_target);
	rwa_device_close(device);
	printf("ycbcr_gpu_host_test: all passed (worst difference from the reference: %d level)\n", worst);
	return 0;
}
