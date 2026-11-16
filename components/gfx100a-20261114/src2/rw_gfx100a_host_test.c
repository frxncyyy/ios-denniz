/* gfx100a host test: rw_gfx100a_pure.h against an independent reference (alias table; edge clamp vs rwtl_region of
 * rw_texlayout_pure.h, i.e. a clamped region is accepted by the real region check and names the same blocks as the asked one). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rw_texlayout_pure.h"
#include "rw_gfx100a_pure.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

int
main(void)
{
	/* 1. alias: exactly 94, 550, 552, 554 -> 80 and 551, 553, 555 -> 81; every other value of 0..4095 unchanged */
	for (unsigned pf = 0; pf < 4096; pf++) {
		unsigned want = pf;

		if (pf == 94 || pf == 550 || pf == 552 || pf == 554)
			want = 80;
		if (pf == 551 || pf == 553 || pf == 555)
			want = 81;
		CHECK(rw_gfx_xr_alias_of(pf) == want, "alias %u -> %u, want %u", pf, rw_gfx_xr_alias_of(pf), want);
	}
	/* 2. edge clamp against the real region check, for ASTC 4x4 .. 12x12 layouts of many sizes */
	static const unsigned fmts[] = { 204, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215, 216, 217, 218 };
	unsigned long tried = 0, clamped = 0, fixed = 0;

	for (unsigned fi = 0; fi < sizeof(fmts) / sizeof(fmts[0]); fi++) {
		for (unsigned W = 1; W <= 70; W += 3) {
			for (unsigned H = 1; H <= 70; H += 5) {
				struct rwtl_layout l;
				const char *why = "";

				if (rwtl_init(&l, fmts[fi], W, H, 1, &why) != 0) {
					CHECK(0, "init %u %ux%u: %s", fmts[fi], W, H, why);
					continue;
				}
				const struct rwtl_level *v = &l.level[0];
				for (unsigned y = 0; y < H; y += l.block_h)
					for (unsigned h = 1; h <= H + 2 * l.block_h; h++) {
						for (unsigned x = 0; x < W; x += l.block_w)
							for (unsigned w = 1; w <= W + 2 * l.block_w; w += 1) {
								uint32_t c0, r0, cs0, rs0, c1, r1, cs1, rs1;
								uint64_t ww = w, hh = h;
								int before = rwtl_region(&l, 0, x, y, w, h, &c0, &r0, &cs0, &rs0, &why);
								int cut = rw_gfx_edge_clamp(x, &ww, v->width, l.block_w) |
									  rw_gfx_edge_clamp(y, &hh, v->height, l.block_h);
								int after = rwtl_region(&l, 0, x, y, ww, hh, &c1, &r1, &cs1, &rs1, &why);
								/* the region in blocks that the asked one covers, and whether it stays inside the
								 * level's whole blocks */
								unsigned ac = (x + w + l.block_w - 1) / l.block_w - x / l.block_w;
								unsigned ar = (y + h + l.block_h - 1) / l.block_h - y / l.block_h;
								int inside = x + w <= v->cols * l.block_w && y + h <= v->rows * l.block_h;
								int aligned_end = (w % l.block_w == 0 || x + w >= v->width) &&
										  (h % l.block_h == 0 || y + h >= v->height);

								tried++;
								clamped += cut != 0;
								if (before == 0) {
									CHECK(!cut && after == 0 && c1 == c0 && cs1 == cs0 && r1 == r0 && rs1 == rs0,
									    "an accepted region changed: %u,%u %ux%u of %ux%u", x, y, w, h, W, H);
								} else if (after == 0) {
									fixed++;
									CHECK(cut && inside && aligned_end && cs1 == ac && rs1 == ar &&
									    c1 == x / l.block_w && r1 == y / l.block_h,
									    "fixed region wrong: fmt %u %u,%u %ux%u of %ux%u -> %llux%llu blocks %ux%u want %ux%u",
									    fmts[fi], x, y, w, h, W, H, (unsigned long long)ww, (unsigned long long)hh,
									    cs1, rs1, ac, ar);
								} else {
									/* still refused: either it leaves the whole blocks, or it was never an edge case */
									CHECK(!inside || !aligned_end || w == 0 || h == 0,
									    "an in-block region still refused: fmt %u %u,%u %ux%u of %ux%u (%s)", fmts[fi],
									    x, y, w, h, W, H, why);
								}
							}
					}
			}
		}
	}
	/* 3. the case from the phone: 787x1705 ASTC 4x4, region 0,1704 787x4 */
	{
		struct rwtl_layout l;
		const char *why = "";
		uint32_t c, r, cs, rs;
		uint64_t w = 787, h = 4;

		rwtl_init(&l, 204, 787, 1705, 1, &why);
		CHECK(rwtl_region(&l, 0, 0, 1704, w, h, &c, &r, &cs, &rs, &why) != 0, "the phone's region was accepted before");
		rw_gfx_edge_clamp(0, &w, 787, 4);
		rw_gfx_edge_clamp(1704, &h, 1705, 4);
		CHECK(rwtl_region(&l, 0, 0, 1704, w, h, &c, &r, &cs, &rs, &why) == 0 && r == 426 && rs == 1 && cs == 197,
		    "the phone's region: %s row %u rows %u cols %u", why, r, rs, cs);
	}
	printf("rw_gfx100a: %s (alias 4096 values; %lu regions, %lu clamped, %lu refused before and accepted after)\n",
	    fails ? "FAILED" : "ok", tried, clamped, fixed);
	return fails != 0;
}
