/* 0550: the header compiles as C too (host-test conventions of sweet_fb_hw.h) */
#include "sweet_fb_hw.h"
#include "sweet_fb_cutout.h"
struct sweet_fb_cut_cfg sweet_fb_cut_g = SWEET_FB_CUT_DEFAULTS;
uint32_t sweet_fb_cut_live;
int main(void) { int32_t x = 100, y = 10; sweet_fb_cut_touch(&sweet_fb_cut_g, 1080u, &x, &y); return x == 28 ? 0 : 1; }
