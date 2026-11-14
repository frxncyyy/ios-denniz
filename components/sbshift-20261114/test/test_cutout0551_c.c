/* 0551: the header compiles as C too; default off = touch identity; on = the fallback rotation (dx 110: shown x 150 <- unrotated 40) */
#include "sweet_fb_hw.h"
#include "sweet_fb_cutout.h"
struct sweet_fb_cut_cfg sweet_fb_cut_g = SWEET_FB_CUT_DEFAULTS;
uint32_t sweet_fb_cut_live;
int main(void) {
	int32_t x = 100, y = 10; sweet_fb_cut_touch(&sweet_fb_cut_g, 1080u, &x, &y); if (x != 100) return 1;
	struct sweet_fb_cut_cfg c = sweet_fb_cut_g; c.on = 1u; x = 150; sweet_fb_cut_touch(&c, 1080u, &x, &y); return x == 40 ? 0 : 2;
}
