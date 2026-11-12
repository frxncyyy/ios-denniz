/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_pattern_gen.c -- host tool (opus-enc4k): writes the probe's test pictures as packed NV12 (w x h, no padding) for
 * the PSNR check of vcodec_check.py, and self-checks the table generator against the original per-pixel formula.
 *
 *   cc -O2 -I gpu/hwcodec/ios -o /tmp/n2vcd_pattern_gen gpu/hwcodec/ios/n2vcd_pattern_gen.c
 *   n2vcd_pattern_gen WxH FRAMES OUT.nv12      reference frames 0..FRAMES-1 (OUT "-" = stdout)
 *   n2vcd_pattern_gen --selftest               table form == vc14 formula at 13 sizes x 20 frames
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n2vcd_pattern.h"

/* the probe's per-pixel formula as it ran in vc14 (n2vcd_probe.c c24a6a1b, make_nv12) */
static void ref_nv12(uint8_t *p, uint32_t w, uint32_t h, uint32_t stride, uint32_t frame)
{
	uint32_t ha = (h + 15u) & ~15u;
	uint8_t *y = p, *uv = p + (size_t)stride * ha;
	for (uint32_t j = 0; j < h; j++)
		for (uint32_t i = 0; i < w; i++) {
			uint32_t v = (i + 2u * frame) & 255u;
			if (((i / 32u) + (j / 32u) + frame / 8u) & 1u) v = 255u - v;
			y[(size_t)j * stride + i] = (uint8_t)(16u + v * 219u / 255u);
		}
	for (uint32_t j = 0; j < h / 2u; j++)
		for (uint32_t i = 0; i < w / 2u; i++) {
			uv[(size_t)j * stride + 2u * i] = (uint8_t)(64u + (j * 128u) / (h / 2u ? h / 2u : 1u));
			uv[(size_t)j * stride + 2u * i + 1u] = (uint8_t)(192u - (i * 128u) / (w / 2u ? w / 2u : 1u));
		}
}

static int selftest(void)
{
	static const uint32_t sz[][2] = { {16, 16}, {18, 34}, {176, 144}, {320, 240}, {640, 480}, {642, 482}, {1280, 720},
	                                  {1920, 1080}, {1918, 1078}, {2560, 1440}, {3840, 2160}, {4096, 2160}, {100, 2000} };
	unsigned bad = 0, n = 0;
	for (unsigned s = 0; s < sizeof sz / sizeof sz[0]; s++) {
		uint32_t w = sz[s][0], h = sz[s][1], stride = (w + 15u) & ~15u, ha = (h + 15u) & ~15u;
		size_t bytes = (size_t)stride * ha * 3u / 2u;
		uint8_t *a = calloc(1, bytes), *b = calloc(1, bytes);
		if (!a || !b) return 1;
		for (uint32_t f = 0; f < 20u; f++) {
			ref_nv12(a, w, h, stride, f);
			n2vcd_pattern_nv12(b, w, h, stride, f);
			n++;
			if (memcmp(a, b, bytes)) { bad++; fprintf(stderr, "MISMATCH %ux%u frame %u\n", w, h, f); }
		}
		free(a); free(b);
	}
	printf("N2TEST pattern %s %u/%u\n", bad ? "FAIL" : "PASS", n - bad, n);
	return bad != 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest();
	uint32_t w, h;
	if (argc != 4 || sscanf(argv[1], "%ux%u", &w, &h) != 2 || w < 16 || h < 16 || (w & 1) || (h & 1)) {
		fprintf(stderr, "usage: n2vcd_pattern_gen WxH FRAMES OUT.nv12 | --selftest\n");
		return 2;
	}
	unsigned frames = (unsigned)strtoul(argv[2], NULL, 0);
	uint32_t stride = (w + 15u) & ~15u, ha = (h + 15u) & ~15u;
	uint8_t *p = calloc(1, (size_t)stride * ha * 3u / 2u);
	FILE *o = strcmp(argv[3], "-") ? fopen(argv[3], "wb") : stdout;   /* "-": stdout, for an ffmpeg pipe */
	if (!p || !o) { perror(argv[3]); return 1; }
	for (unsigned f = 0; f < frames; f++) {
		n2vcd_pattern_nv12(p, w, h, stride, f);
		for (uint32_t j = 0; j < h; j++) fwrite(p + (size_t)j * stride, 1, w, o);
		for (uint32_t j = 0; j < h / 2u; j++) fwrite(p + (size_t)stride * ha + (size_t)j * stride, 1, w, o);
	}
	return fclose(o) != 0;
}
