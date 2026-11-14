/* camprev: the video data output's new size (rwcam_vdo_target, extracted from src/rwcam_diag.c by run_tests.sh into vdo_target.inc) */
#include <stdio.h>
#include <stdlib.h>
#include "vdo_target.inc"
static int bad;
static void want(int w, int h, int div, int ok, int ew, int eh)
{
	int nw = -1, nh = -1, r = rwcam_vdo_target(w, h, div, &nw, &nh);
	if (r != ok || (ok && (nw != ew || nh != eh))) { printf("FAIL %dx%d /%d -> %d %dx%d (want %d %dx%d)\n", w, h, div, r, nw, nh, ok, ew, eh); bad++; }
}
int main(void)
{
	want(4096, 3072, 2, 1, 2048, 1536); want(3072, 4096, 2, 1, 1536, 2048);
	want(4032, 3024, 2, 1, 2016, 1512); want(3024, 4032, 2, 1, 1504, 2005 & ~1);
	want(3088, 2320, 2, 1, 1536, 1152); want(4096, 3072, 4, 1, 1024, 768);
	want(4096, 3072, 0, 0, 0, 0);                   /* kill switch */
	want(1504, 1128, 2, 0, 0, 0); want(1920, 1440, 2, 0, 0, 0);           /* already small */
	want(3840, 2160, 2, 0, 0, 0); want(4096, 2304, 2, 0, 0, 0); want(2160, 3840, 2, 0, 0, 0);   /* films 16:9 */
	want(3024, 3024, 2, 0, 0, 0);                   /* square */
	want(90, 90, 2, 0, 0, 0); want(0, 0, 2, 0, 0, 0); want(-4096, -3072, 2, 0, 0, 0);
	/* every size: the result is smaller, 16-aligned, even, the same shape within 1 % */
	for (int w = 16; w <= 8192; w += 8) for (int h = 16; h <= 8192; h += 8) {
		int nw, nh;
		if (!rwcam_vdo_target(w, h, 2, &nw, &nh)) continue;
		if (nw % 16 || nh % 2 || nw > w / 2 || nh > h / 2 + 1 || (long)w * h <= 6000000L || abs(nw * h - nh * w) > w * 2 + h * 16) { printf("FAIL sweep %dx%d -> %dx%d\n", w, h, nw, nh); bad++; break; }
	}
	printf(bad ? "T_VDO FAILED\n" : "T_VDO OK\n");
	return bad != 0;
}
