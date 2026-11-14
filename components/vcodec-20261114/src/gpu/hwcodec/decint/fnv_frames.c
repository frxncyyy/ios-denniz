/* SPDX-License-Identifier: MIT */
/* opus-decint: per-frame hashes of cropped NV12 frames on stdin (Y then UV), exactly as n2vcd_dprobe logs them:
 *   fnv  = FNV-1a 64 over the whole plane (older logs: fnv_y/fnv_uv)
 *   fnvr = FNV-1a 64 over the per-row FNV-1a 64 hashes, each as 8 little-endian bytes (logs since vc27: fnvr_y/fnvr_uv)
 * usage: fnv_frames W H < frames.nv12   -> one line "fnv_y fnv_uv fnvr_y fnvr_uv" per frame */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint64_t fnv(uint64_t h, const unsigned char *p, size_t n)
{
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
	return h;
}
static uint64_t fnvr(const unsigned char *p, size_t pitch, size_t rows)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t r = 0; r < rows; r++) {
		uint64_t rh = fnv(0xcbf29ce484222325ull, p + r * pitch, pitch);
		unsigned char le[8];
		for (int k = 0; k < 8; k++) le[k] = (unsigned char)(rh >> (8 * k));
		h = fnv(h, le, 8);
	}
	return h;
}
int main(int argc, char **argv)
{
	if (argc != 3) return 2;
	size_t w = strtoul(argv[1], NULL, 0), h = strtoul(argv[2], NULL, 0);
	size_t cw = 2 * ((w + 1) / 2), ch = (h + 1) / 2, ys = w * h, cs = cw * ch;
	unsigned char *b = malloc(ys + cs);
	if (!b) return 1;
	while (fread(b, 1, ys + cs, stdin) == ys + cs)
		printf("%016llx %016llx %016llx %016llx\n", (unsigned long long)fnv(0xcbf29ce484222325ull, b, ys),
		       (unsigned long long)fnv(0xcbf29ce484222325ull, b + ys, cs), (unsigned long long)fnvr(b, w, h),
		       (unsigned long long)fnvr(b + ys, cw, ch));
	return 0;
}
