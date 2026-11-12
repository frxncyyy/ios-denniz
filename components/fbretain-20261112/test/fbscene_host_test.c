/* Host test of 0392 sweet_fb_scene.h with the run fbd1 swap sequence: cc -I../b/iokit/Kernel/arm fbscene_host_test.c */
#include <stdio.h>
#include <string.h>
#include "sweet_fb_scene.h"
static void mk(uint8_t *r, uint32_t mask, uint32_t i0, uint32_t i1, uint32_t i2)
{
	memset(r, 0, 0x46c); uint32_t ids[4] = { i0, i1, i2, 0 };
	for (int l = 0; l < 4; l++) { sweet_fb_scene_wr(r + 0x54 + 4 * l, ids[l]);
		if (ids[l]) { sweet_fb_scene_wr(r + 0x64 + 16 * l + 8, 1125); sweet_fb_scene_wr(r + 0x64 + 16 * l + 12, 2436);
			sweet_fb_scene_wr(r + 0xc4 + 16 * l + 8, 1125); sweet_fb_scene_wr(r + 0xc4 + 16 * l + 12, 2436); } }
	sweet_fb_scene_wr(r + 0x104, mask);
}
int main(void)
{
	struct sweet_fb_scene sc; memset(&sc, 0, sizeof(sc)); uint8_t r[0x46c]; int bad = 0;
	struct { uint32_t m, a, b, c, em, e0, e1; } seq[] = {
		{ 0x7, 2, 0, 0, 0x1, 2, 0 },        /* backboardd alone */
		{ 0x7, 3, 0, 0, 0x1, 3, 0 },
		{ 0x7, 53, 3, 0, 0x3, 53, 3 },      /* detach: swap 1020 */
		{ 0x7, 52, 2, 0, 0x3, 52, 2 },
		{ 0x7, 61, 3, 0, 0x3, 61, 3 },
		{ 0x2, 0, 3, 0, 0x3, 61, 3 },       /* swap 1023: UI only, layer 0 kept */
		{ 0x2, 0, 2, 0, 0x3, 61, 2 },
		{ 0x7, 2, 0, 0, 0x1, 2, 0 },        /* re-composited (selection): layer 1 off */
	};
	for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
		mk(r, seq[i].m, seq[i].a, seq[i].b, seq[i].c);
		uint32_t em = sweet_fb_scene_apply(&sc, r);
		uint32_t i0 = sweet_fb_scene_rd(r + 0x54), i1 = sweet_fb_scene_rd(r + 0x58), w0 = sweet_fb_scene_rd(r + 0xc4 + 8);
		if (em != seq[i].em || i0 != seq[i].e0 || i1 != seq[i].e1 || sweet_fb_scene_rd(r + 0x104) != em || (i0 && w0 != 1125)) {
			bad++; printf("step %u: mask %#x ids %u,%u w0 %u\n", i, em, i0, i1, w0);
		}
	}
	printf(bad ? "FBSCENE_HOST_TEST_FAIL\n" : "FBSCENE_HOST_TEST_OK\n");
	return bad;
}
