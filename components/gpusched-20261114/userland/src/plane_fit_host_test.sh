#!/bin/bash
# y2s43 host check of the arithmetic that decides whether a texture fits a plane (redwooddrawable.m): the three static functions are cut out
# of the file as they are and called with the surfaces of run q10hw.  No phone, no GPU; it does not exercise the kernel calls or the import.
#   T=<scratch dir> bash plane_fit_host_test.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); T=${T:?}; H=$NORD2_WORK/gpu/build/y2s33/src/redwood_drawable.h
{ printf '#include <stdint.h>\n#include <stdio.h>\n#include <stdarg.h>\n#include <stddef.h>\n#pragma GCC diagnostic ignored "-Wmultichar"\n'
  sed -n '/^struct rwd_surface {/,/^};/p' $H
  printf 'static void rwd_say(char *why, size_t n, const char *f, ...) { va_list a; va_start(a, f); vsnprintf(why, n, f, a); va_end(a); }\n'
  for f in rwd_plane_fits_gpu rwd_plain_format_size rwd_plain_fourcc_size; do sed -n "/^static [a-z]*\$/{N;/\n$f(/{:a;N;/\n}\$/!ba;p}}" $HERE/redwooddrawable.m; done
  cat <<'C'
int main(void)
{
	/* the preview scaler's destination of run q10hw (1128x1504 '420f', planes of 1152 bytes a row) and its source (1504x1128) */
	struct rwd_surface d = { .surface = 56, .width = 1128, .height = 1504, .planes = 2, .size = 2600000 };
	char why[512];
	int bad = 0;
	d.planeRowBytes[0] = d.planeRowBytes[1] = 1152; d.planeOffset[0] = 64; d.planeOffset[1] = 1732672;
	struct { unsigned plane, format, w, h; int want; } c[] = {
		{ 0, 53, 282, 1504, 0 },        /* what VideoToolbox asked: four luma bytes a texel */
		{ 1, 53, 282, 752, 0 },         /* two CbCr pairs a texel */
		{ 0, 10, 1128, 1504, 0 }, { 0, 13, 1128, 1504, 0 }, { 1, 30, 564, 752, 0 }, { 1, 23, 564, 752, 0 },
		{ 0, 23, 564, 1504, 0 },        /* two luma bytes a texel */
		{ 0, 53, 1128, 1504, -1 }, { 0, 53, 281, 1504, -1 }, { 1, 53, 564, 752, -1 }, { 0, 10, 282, 1504, -1 }, { 1, 105, 141, 752, 0 },
	};
	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		unsigned texel = rwd_plain_format_size(c[i].format), elem = c[i].plane ? 2 : 1;
		int ok = texel >= elem && texel % elem == 0;
		int r = ok ? rwd_plane_fits_gpu(&d, c[i].plane, texel, c[i].w, c[i].h, why, sizeof(why)) : -1;
		if (r != c[i].want) { bad++; printf("FAILED: plane %u format %u %ux%u gave %d (%s)\n", c[i].plane, c[i].format, c[i].w, c[i].h, r, why); }
	}
	if (rwd_plain_fourcc_size('BGRA') != 4 || rwd_plain_fourcc_size('L00h') != 2 || rwd_plain_fourcc_size('420f') != 0 ||
	    rwd_plain_format_size(80) != 0 || rwd_plain_format_size(540) != 0) { bad++; printf("FAILED: the size tables\n"); }
	printf("plane_fit_host_test: %u cases, %d failed\n", (unsigned)(sizeof(c) / sizeof(c[0])), bad);
	return bad != 0;
}
C
} > $T/plane_fit.c
cc -std=gnu11 -O1 -Wall -Wno-unused-function -o $T/plane_fit $T/plane_fit.c && $T/plane_fit
# y2s49: the one-plane rule (rwd_plain_over_surface), with every format / width the still-image noise reduction asks over its surfaces
# (run q10ig-vt8: 'L00h' 2016x1512 of 4032 bytes a row; its chroma '2C0h' 1008x756 of 4032), and what must stay refused.
{ printf '#include <stdint.h>\n#include <stdio.h>\n#pragma GCC diagnostic ignored "-Wmultichar"\n'
  for f in rwd_plain_over_surface rwd_plain_format_size rwd_plain_fourcc_size; do sed -n "/^static [a-z]*\$/{N;/\n$f(/{:a;N;/\n}\$/!ba;p}}" $HERE/redwooddrawable.m; done
  cat <<'C'
int main(void)
{
	struct { uint32_t fourcc; unsigned sw, sh, row, format, w, h; int want; const char *what; } c[] = {
		{ 'L00h', 2016, 1512, 4032, 25, 2016, 1512, 1, "R16Float, the surface as it is" },
		{ 'L00h', 2016, 1512, 4032, 65, 1008, 1512, 1, "RG16Float at half the width (setPixelBufferFloat16, xFactor 2)" },
		{ 'L00h', 2016, 1512, 4032, 10, 2016, 1512, 1, "R8Unorm at the width (bindFP16PyramidToU8, xFactor 1)" },
		{ 'L00h', 2016, 1512, 4032, 30, 1008, 1512, 1, "RG8Unorm at half the width (xFactor 2: run q10ig-vt8's nil)" },
		{ 'L00h', 2016, 1512, 4032, 30, 2016, 1512, 1, "RG8Unorm at the width (xFactor 1)" },
		{ 'L00h', 2016, 1512, 4032, 23, 2016, 1512, 1, "R16Uint at the width" },
		{ '2C0h', 1008, 756, 4032, 65, 1008, 756, 1, "RG16Float, the chroma as it is" },
		{ '2C0h', 1008, 756, 4032, 53, 1008, 756, 1, "R32Uint over the chroma (xFactor 1)" },
		{ '2C0h', 1008, 756, 4032, 30, 1008, 756, 1, "RG8Unorm over the chroma at its width" },
		{ '2C0h', 1008, 756, 4032, 30, 504, 756, 1, "RG8Unorm over the chroma at half its width" },
		{ '2C0h', 1008, 756, 4032, 23, 1008, 756, 1, "R16Uint over the chroma" },
		{ '2C0h', 1008, 756, 4032, 10, 1008, 756, 1, "R8Unorm over the chroma" },
		{ 'L008', 1128, 1504, 1152, 53, 282, 1504, 1, "R32Uint over four 'L008' bytes (VideoToolbox, y2s43)" },
		{ 'L00h', 2016, 1512, 4032, 65, 2016, 1512, 0, "RG16Float at the full width: twice the row" },
		{ 'L00h', 2016, 1512, 4032, 10, 2016, 756, 0, "another height" },
		{ 'L00h', 2016, 1512, 4032, 10, 4033, 1512, 0, "more bytes than the row" },
		{ 'L00h', 2016, 1512, 4032, 80, 1008, 1512, 0, "a format that is not plain (BGRA8Unorm)" },
		{ '420f', 2016, 1512, 2048, 10, 2016, 1512, 0, "a surface that is not a plain array (two planes: its own rule)" },
		{ 'BGRA', 64, 64, 256, 53, 52, 52, 0, "52x52 over 64x64: another height" },
	};
	int bad = 0;
	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		int r = rwd_plain_over_surface(rwd_plain_fourcc_size(c[i].fourcc), rwd_plain_format_size(c[i].format), c[i].sw, c[i].sh, c[i].row, c[i].w, c[i].h);
		if (r != c[i].want) { bad++; printf("FAILED: %s gave %d\n", c[i].what, r); }
	}
	printf("plain_over_surface_host_test: %u cases, %d failed\n", (unsigned)(sizeof(c) / sizeof(c[0])), bad);
	return bad != 0;
}
C
} > $T/plain_over.c
cc -std=gnu11 -O1 -Wall -Wno-unused-function -o $T/plain_over $T/plain_over.c && $T/plain_over
