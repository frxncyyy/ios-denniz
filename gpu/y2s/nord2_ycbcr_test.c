/*
 * Host test of nord2_ycbcr.c (no phone): against a floating-point reference, for the three matrices and both ranges.
 *   cc -O3 -Wall -Wextra -o /tmp/ycbcr_test gpu/y2s/nord2_ycbcr_test.c -lm && /tmp/ycbcr_test
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nord2_ycbcr.c"

static void
reference(int y, int cb, int cr, int matrix, int full, int out[3])
{
	static const double k[3][2] = { { 0.299, 0.114 }, { 0.2126, 0.0722 }, { 0.2627, 0.0593 } };
	const double kr = k[matrix][0], kb = k[matrix][1], kg = 1.0 - kr - kb;
	double Y = full ? y / 255.0 : (y < 16 ? 0.0 : (y - 16) / 219.0), U = (cb - 128) / (full ? 255.0 : 224.0), V = (cr - 128) / (full ? 255.0 : 224.0);
	double r = Y + 2.0 * (1.0 - kr) * V, b = Y + 2.0 * (1.0 - kb) * U, g = Y - 2.0 * kb * (1.0 - kb) / kg * U - 2.0 * kr * (1.0 - kr) / kg * V;
	double c[3] = { b, g, r };

	for (int i = 0; i < 3; i++) {
		double v = floor(c[i] * 255.0 + 0.5);

		out[i] = v < 0 ? 0 : v > 255 ? 255 : (int)v;
	}
}

static void
one(int y, int cb, int cr, int matrix, int full, uint8_t px[4])
{
	uint8_t luma[2] = { (uint8_t)y, (uint8_t)y }, chroma[2] = { (uint8_t)cb, (uint8_t)cr }, out[8];

	nord2_ycbcr420_to_bgra(luma, 2, chroma, 2, out, 8, 2, 1, matrix, full);
	assert(memcmp(out, out + 4, 4) == 0);
	memcpy(px, out, 4);
}

int
main(void)
{
	uint8_t px[4];
	int worst = 0;

	/* the test pattern of the camera bundle: white and black bars, grey chroma, in both ranges and every matrix */
	for (int m = 0; m < 3; m++) {
		one(235, 128, 128, m, 0, px); assert(px[0] == 255 && px[1] == 255 && px[2] == 255 && px[3] == 255);
		one(16, 128, 128, m, 0, px); assert(px[0] == 0 && px[1] == 0 && px[2] == 0);
		one(255, 128, 128, m, 1, px); assert(px[0] == 255 && px[1] == 255 && px[2] == 255);
		one(0, 128, 128, m, 1, px); assert(px[0] == 0 && px[1] == 0 && px[2] == 0);
		one(126, 128, 128, m, 0, px); assert(px[0] == px[1] && px[1] == px[2] && px[0] == 128);
	}
	/* BT.601 video-range primaries (the classic studio values): red 81,90,240; green 145,54,34; blue 41,240,110 */
	one(81, 90, 240, 0, 0, px); assert(px[2] >= 253 && px[1] <= 2 && px[0] <= 2);
	one(145, 54, 34, 0, 0, px); assert(px[1] >= 253 && px[2] <= 2 && px[0] <= 2);
	one(41, 240, 110, 0, 0, px); assert(px[0] >= 253 && px[1] <= 2 && px[2] <= 2);
	/* BT.709 video-range red: 63,102,240 */
	one(63, 102, 240, 1, 0, px); assert(px[2] >= 253 && px[1] <= 2 && px[0] <= 2);
	/* every Y with a grid of chroma: within one level of the floating-point reference */
	for (int m = 0; m < 3; m++) {
		for (int full = 0; full < 2; full++) {
			for (int y = 0; y < 256; y++) {
				for (int cb = 0; cb < 256; cb += 5) {
					for (int cr = 0; cr < 256; cr += 3) {
						int want[3];

						one(y, cb, cr, m, full, px);
						reference(y, cb, cr, m, full, want);
						for (int i = 0; i < 3; i++) {
							int d = abs((int)px[i] - want[i]);

							worst = d > worst ? d : worst;
						}
						assert(px[3] == 255);
					}
				}
			}
		}
	}
	assert(worst <= 1);
	/* geometry: odd sizes, row lengths larger than the picture, chroma shared by 2x2 blocks and nothing written outside */
	{
		enum { W = 37, H = 21, LR = 64, CR = 64, OR = W * 4 + 12 };
		static uint8_t luma[LR * H], chroma[CR * ((H + 1) / 2)], out[OR * H + 16], check[4];

		srand(7);
		for (size_t i = 0; i < sizeof(luma); i++) luma[i] = (uint8_t)rand();
		for (size_t i = 0; i < sizeof(chroma); i++) chroma[i] = (uint8_t)rand();
		memset(out, 0xa5, sizeof(out));
		nord2_ycbcr420_to_bgra(luma, LR, chroma, CR, out, OR, W, H, NORD2_YCBCR_709, 0);
		for (int y = 0; y < H; y++) {
			for (int x = 0; x < W; x++) {
				one(luma[y * LR + x], chroma[(y / 2) * CR + (x / 2) * 2], chroma[(y / 2) * CR + (x / 2) * 2 + 1], 1, 0, check);
				assert(memcmp(out + y * OR + x * 4, check, 4) == 0);
			}
			for (int x = W * 4; x < OR; x++) assert(out[y * OR + x] == 0xa5);
		}
		for (size_t i = OR * H; i < sizeof(out); i++) assert(out[i] == 0xa5);
	}
	/* the camera preview's size, timed (host numbers, only to see the loop is not pathological) */
	{
		enum { W = 1128, H = 1504, R = 1152 };
		uint8_t *luma = malloc(R * H), *chroma = malloc(R * (H / 2)), *out = malloc((size_t)W * 4 * H);
		struct timespec a, b;

		for (size_t i = 0; i < (size_t)R * H; i++) luma[i] = (uint8_t)(i * 7);
		for (size_t i = 0; i < (size_t)R * (H / 2); i++) chroma[i] = (uint8_t)(i * 13);
		clock_gettime(CLOCK_MONOTONIC, &a);
		for (int n = 0; n < 20; n++) nord2_ycbcr420_to_bgra(luma, R, chroma, R, out, W * 4, W, H, 0, 1);
		clock_gettime(CLOCK_MONOTONIC, &b);
		printf("1128x1504: %.2f ms a frame on this host\n", ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / 20);
		free(luma); free(chroma); free(out);
	}
	printf("nord2_ycbcr_test: all passed (worst difference from the reference: %d level)\n", worst);
	return 0;
}
