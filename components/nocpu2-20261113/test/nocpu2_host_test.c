/* 0416 host test: sweet_fb_blank_action (DCS only, no CPU fade) exhaustive; sweet_fb_layer_clip (the only part of 0390's header left)
 * against a reference clip; the removed CPU-path helpers must not exist (checked by the build of this file: it defines them itself).
 * cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../b/iokit/Kernel/arm nocpu2_host_test.c  (also -x c++) */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "sweet_fb_hw.h"
#include "sweet_fb_blend.h"
/* if 0416 left any of these in the headers, these definitions collide and the test does not compile */
static int sweet_fb_hw_cpu_allowed(void) { return 0; }
static int sweet_fb_blend_px(void) { return 0; }
static int sweet_fb_compose_row(void) { return 0; }
static int sweet_fb_hw_fade_may_run(void) { return 0; }
static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %d %s\n", __LINE__, m); } } while (0)
int main(void)
{
	unsigned n = 0;
	(void)sweet_fb_hw_cpu_allowed(); (void)sweet_fb_blend_px(); (void)sweet_fb_compose_row(); (void)sweet_fb_hw_fade_may_run();
	for (int po = 0; po < 2; po++) for (int bl = 0; bl < 2; bl++) {
		int a = sweet_fb_blank_action(po, bl), w = po ? (bl ? SWEET_FB_BLANK_NONE : SWEET_FB_BLANK_OFF) : (bl ? SWEET_FB_BLANK_ON : SWEET_FB_BLANK_NONE);
		CHECK(a == w, "blank rule"); n++;
	}
	struct sweet_fb_layer_geom g;
	CHECK(sweet_fb_layer_clip(&g, 0, 0, 0, 0, 0, 0, 1125, 2436, 0, 0, 1080, 2400) && g.w == 1080 && g.h == 2400, "full screen clips to the panel");
	CHECK(!sweet_fb_layer_clip(&g, 0, 0, 2000, 0, 10, 10, 100, 100, 0, 0, 1080, 2400), "off-panel layer: nothing");
	CHECK(sweet_fb_layer_clip(&g, 0, 0, 20, 30, 0, 0, 100, 100, 22, 18, 1080, 2400) && g.dx == 0 && g.dy == 12 && g.sx == 2 && g.w == 98, "offset clip");
	n += 3;
	printf("%s: %u cases, %d failures\n", fails ? "FAILED" : "ALL OK", n, fails); return fails != 0;
}
