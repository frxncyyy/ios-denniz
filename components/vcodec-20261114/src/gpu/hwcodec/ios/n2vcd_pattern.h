/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_pattern.h -- the moving NV12 test pattern of n2vcd_probe (opus-ios; table form opus-enc4k, 11 Nov 2026).
 *
 * One definition shared by the phone probe and the host reference generator (n2vcd_pattern_gen.c), so the PSNR check of
 * vcodec_check.py compares the decoded stream with exactly the pictures the probe encoded.  Values are identical to the
 * first phone run's per-pixel formula (vc14):
 *   luma   Y(i,j,f) = 16 + v*219/255,  v = (i + 2f) & 255, inverted (255 - v) on the 32x32 checkerboard cells where
 *          ((i/32) + (j/32) + f/8) is odd: a ramp that moves 2 px per frame, cells that flip every 8 frames
 *   chroma U(i,j) = 64 + j*128/(h/2), V(i,j) = 192 - i*128/(w/2) (static)
 * Rows are built from a 256-entry table, so a 4096x2160 picture costs a few ms on the phone's CPU, not ~9 M divisions.
 */
#ifndef N2VCD_PATTERN_H
#define N2VCD_PATTERN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Writes the w x h picture `frame` into p: luma rows of `stride` bytes, chroma (interleaved UV) at stride * align16(h),
 * same stride.  Padding columns/rows are not written (the caller zeroes the buffer once). */
static inline void n2vcd_pattern_nv12(uint8_t *p, uint32_t w, uint32_t h, uint32_t stride, uint32_t frame)
{
	uint8_t lut[256], ilut[256];
	for (unsigned v = 0; v < 256u; v++) {
		lut[v] = (uint8_t)(16u + v * 219u / 255u);
		ilut[v] = (uint8_t)(16u + (255u - v) * 219u / 255u);
	}
	uint32_t ha = (h + 15u) & ~15u;
	uint8_t *y = p, *uv = p + (size_t)stride * ha;
	/* two distinct row kinds per 32-row band: cell parity even / odd at column 0 */
	for (uint32_t j = 0; j < h; j++) {
		uint8_t *row = y + (size_t)j * stride;
		if (j & 31u) {                      /* rows inside a band repeat the band's first row */
			memcpy(row, row - stride, w);
			continue;
		}
		uint32_t par0 = (j / 32u + frame / 8u) & 1u;
		for (uint32_t i = 0; i < w; i++) {
			uint32_t v = (i + 2u * frame) & 255u;
			row[i] = (((i / 32u) & 1u) ^ par0) ? ilut[v] : lut[v];
		}
	}
	uint32_t hh = h / 2u ? h / 2u : 1u, wh = w / 2u ? w / 2u : 1u;
	for (uint32_t j = 0; j < h / 2u; j++) {
		uint8_t *row = uv + (size_t)j * stride;
		if (j == 0) {
			for (uint32_t i = 0; i < w / 2u; i++) {
				row[2u * i] = (uint8_t)(64u + (j * 128u) / hh);
				row[2u * i + 1u] = (uint8_t)(192u - (i * 128u) / wh);
			}
			continue;
		}
		memcpy(row, uv, w & ~1u);           /* V depends on i only */
		uint8_t u = (uint8_t)(64u + (j * 128u) / hh);
		for (uint32_t i = 0; i < w / 2u; i++) row[2u * i] = u;
	}
}

#endif
