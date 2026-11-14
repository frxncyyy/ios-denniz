/* 0640 host test: the software compositor of the overlay plan (sweet_fb_snapc.h) against a straightforward per-pixel reference.
 * Random layer sets (1..6 layers, BGRA with random alpha incl. 0/255, plane alpha, pm 0 straight / pm 2 premultiplied, csw swaps,
 * 4L cascade), the 0550 band rotation (shown = unrotated reference at the inverse x inside the band), packed 4:2:2 layers with odd
 * edges (CLIP), unreadable layers (magenta, later layers still blended over it), addresses outside every source (magenta), scale /
 * crop subsampling and the run-length packing round trip (decoder = phone.py's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_hw.h"
#include "sweet_fb_cutout.h"
#include "sweet_fb_snapc.h"

#define FW 1080u
#define FH 2400u
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static uint32_t rs = 12345;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static uint32_t rr(uint32_t n) { return n ? rnd() % n : 0; }

/* sources: device ranges backed by host buffers */
struct hsrc { uint64_t dev, len; uint8_t *mem; int fail; };
static struct hsrc S[8]; static unsigned NS;
static int rd(void *ctx, int src, uint64_t off, void *dst, uint32_t n)
{
	(void)ctx;
	if (src < 0 || (unsigned)src >= NS || S[src].fail || off + n > S[src].len) { return 1; }
	memcpy(dst, S[src].mem + off, n); return 0;
}

/* ---- reference ---- */
static uint32_t d255(uint32_t v) { return (v + 127u) / 255u; }
static uint32_t ref_blend(uint32_t con, uint32_t s, uint32_t d, int yuv)
{
	uint32_t pa = con & 0xff, sa = yuv ? 255 : s >> 24;
	if (!(con & 0x100)) return s & 0xffffff;
	uint32_t ea = d255(sa * pa), o = 0;
	for (int sh = 0; sh < 24; sh += 8) {
		uint32_t sc = (s >> sh) & 255, dc = (d >> sh) & 255, v;
		if (!yuv && (con & (1u << 23))) v = d255(sc * pa) + d255(dc * (255 - ea)); else v = d255(sc * ea + dc * (255 - ea));
		o |= (v > 255 ? 255 : v) << sh;
	}
	return o;
}
struct rl { struct sweet_fb_hw_in in; int src; uint32_t con; int yuv; };
/* unrotated picture at panel (x, y) from the layer list (each layer's CON = what the planner gives it, csw applied) */
static uint32_t ref_px(const struct rl *L, unsigned n, int x, int y)
{
	uint32_t d = 0;
	for (unsigned i = 0; i < n; i++) {
		const struct sweet_fb_hw_in *l = &L[i].in;
		if (x < l->dx || x >= l->dx + l->w || y < l->dy || y >= l->dy + l->h) continue;
		if (S[L[i].src].fail) { d = 0xff00ff; continue; }
		int cx = l->sx + x - l->dx, cy = l->sy + y - l->dy;
		const uint8_t *row = S[L[i].src].mem + (l->pa - S[L[i].src].dev) + l->offset + (uint64_t)cy * l->pitch;
		if (!L[i].yuv) {
			const uint8_t *q = row + 4 * cx;
			uint32_t w = q[0] | q[1] << 8 | q[2] << 16 | (uint32_t)q[3] << 24;
			d = ref_blend(L[i].con, sweet_fb_hw_con_decode(L[i].con, w), d, 0);
		} else {
			const uint8_t *q = row + 4 * (cx / 2);
			int uyvy = ((L[i].con >> 12) & 15) == 4;
			uint32_t Y = uyvy ? q[1 + 2 * (cx & 1)] : q[2 * (cx & 1)], cb = uyvy ? q[0] : q[1], cr = uyvy ? q[2] : q[3];
			if (L[i].con & (1u << 24)) { uint32_t t = cb; cb = cr; cr = t; }      /* BYTE_SWAP: Cb/Cr exchanged */
			d = ref_blend(L[i].con, sweet_fb_yuv_ref_rgb((L[i].con >> 16) & 15, Y, cb, cr), d, 1);
		}
	}
	return d;
}

static uint32_t line[FW]; static uint8_t tmp[SWEET_FB_SNAPC_TMP_BYTES];
static uint8_t out[FW * FH * 3 + FW * FH / 64 + 64], unp[FW * FH * 3];

static uint32_t decode(const uint8_t *d, uint32_t bytes, uint8_t *o)      /* = phone.py */
{
	uint32_t i = 0, n = 0;
	while (i < bytes) {
		uint32_t c = d[i++];
		if (c < 128) { memcpy(o + n, d + i, 3 * (c + 1)); n += 3 * (c + 1); i += 3 * (c + 1); }
		else { for (uint32_t k = 0; k < c - 126; k++) { memcpy(o + n, d + i, 3); n += 3; } i += 3; }
	}
	return n;
}

static unsigned tests, cut_ok, yuv_n, fail_n, rgb_px;
static void one(int mode)
{
	/* sources */
	NS = 0;
	uint64_t bases[3] = { 0xC0000000ull + 0x10000ull * rr(64), 0x90000000ull + 0x100000ull * rr(64), 0x76e10000ull };
	unsigned nl = 1 + rr(6);
	struct rl L[6]; memset(L, 0, sizeof L);
	uint32_t pm = rr(2) ? 0u : 2u, csw = rr(4) == 0 ? rr(4) : 0;
	int want_cut = mode != 1 && rr(2);
	if (want_cut && rr(3)) nl = 1;      /* 0551: the rotation applies to single full-screen layer frames only */
	if (want_cut) csw = 0;      /* band pieces of layer 0 sit in upper slots: with kern.n2fbhwcsw != 0 they take the upper-slot swap (HW behaviour, not the unrotated reference) */
	for (unsigned i = 0; i < nl; i++) {
		struct hsrc *h = &S[NS];
		int yuv = mode == 2 && i > 0 && rr(2);
		uint32_t sw = yuv ? 64 + rr(700) : 32 + rr(1100), sh = 16 + rr(2400), bpp = yuv ? 2 : 4;
		if (i == 0 && want_cut) { sw = FW + rr(50); sh = FH + rr(40); }
		uint32_t pitch = ((sw * bpp + 63) / 64) * 64 + 64 * rr(2), off = 64 * rr(4);
		h->dev = bases[i % 3] + 0x2000000ull * i; h->len = (uint64_t)off + (uint64_t)pitch * sh; h->mem = malloc(h->len); h->fail = 0;
		for (uint64_t b = 0; b < h->len; b++) {
			uint32_t v = rnd();
			h->mem[b] = (uint8_t)((b & 3) == 3 && !yuv ? (v % 5 == 0 ? 0 : v % 5 == 1 ? 255 : v) : v);
			if (rr(97) == 0 && b + 64 < h->len) { memset(h->mem + b, h->mem[b], 48); b += 47; }      /* flat runs for the packer */
		}
		struct sweet_fb_hw_in *l = &L[i].in;
		l->pa = h->dev; l->len = h->len; l->offset = off; l->pitch = pitch;
		l->format_ok = yuv ? sweet_fb_yuv_con(0x001u | (rr(2) ? 0x100u : 0u), rr(2) ? SWEET_FB_YUV_FCC_2VUY : SWEET_FB_YUV_FCC_YUVS, 0, 2, 1, 1, sw, sh) : 1u;
		l->w = 1 + (int32_t)rr(sw < FW ? sw : FW); l->h = 1 + (int32_t)rr(sh < FH ? sh : FH);
		l->sx = (int32_t)rr(sw - (uint32_t)l->w + 1); l->sy = (int32_t)rr(sh - (uint32_t)l->h + 1);
		l->dx = (int32_t)rr(FW - (uint32_t)l->w + 1); l->dy = (int32_t)rr(FH - (uint32_t)l->h + 1);
		if (i == 0 && want_cut) { l->w = FW; l->h = FH; l->dx = l->dy = 0; l->sx = (int32_t)rr(sw - FW + 1); l->sy = (int32_t)rr(sh - FH + 1); }
		if (yuv && l->sx + l->w + 1 > (int32_t)sw) { l->w = (int32_t)sw - l->sx - 1; if (l->w < 1) { l->sx = 0; l->w = 2; } }
		l->alpha = (rr(4) == 0 && !(i == 0 && want_cut)) ? 1 + rr(254) : 0;
		L[i].src = (int)NS; L[i].yuv = yuv; yuv_n += yuv; NS++;
	}
	struct sweet_fb_hw_in in[6]; for (unsigned i = 0; i < nl; i++) in[i] = L[i].in;
	struct sweet_fb_hw_plan p;
	int r = sweet_fb_hw_plan_makec(&p, in, nl, SWEET_FB_HW_DUAL, pm, FW, FH, 6);
	if (r != SWEET_FB_HW_OK) { for (unsigned i = 0; i < NS; i++) free(S[i].mem); return; }
	struct sweet_fb_cut_cfg cc = SWEET_FB_CUT_DEFAULTS;
	cc.on = 1u;      /* 0551 made it off by default (kern.n2cut=1 / n2gdv +512 turn it on) */
	if (rr(3) == 0) { cc.lead_dx = 20 + (int32_t)rr(80); cc.trail_dx = (int32_t)rr(40); cc.band = 40 + (int32_t)rr(150); }
	int cw = want_cut ? sweet_fb_cut_plan(&p, in, nl, &cc, pm, FW, 6) : SWEET_FB_CUT_OFF;
	sweet_fb_hw_plan_csw(&p, csw);
	/* each layer's CON as the planner made it (same as its first slot), with the csw swap of that slot class */
	for (unsigned i = 0; i < nl; i++) {
		uint32_t a = (L[i].in.alpha == 0 || L[i].in.alpha >= 255) ? 255 : L[i].in.alpha;
		uint32_t con = (i == 0 && a == 255) ? SWEET_FB_HW_CON_OPAQUE : ((sweet_fb_hw_con_upper(pm) & ~0xffu) | a);
		if (L[i].yuv) con = sweet_fb_yuv_con_alpha(L[i].in.format_ok, L[i].in.alpha);
		L[i].con = con ^ (i == 0 ? p.con_xor[0] : p.con_xor[1]);
	}
	int failing = mode == 3 ? (int)rr(nl) : -1;
	if (failing >= 0) { S[failing].fail = 1; fail_n++; }
	struct sweet_fb_snapc_src src[8]; for (unsigned i = 0; i < NS; i++) { src[i].dev = S[i].dev; src[i].len = S[i].len; }
	struct sweet_fb_snapc c;
	uint32_t marked = sweet_fb_snapc_prepare(&c, &p, src, NS, FW, FH);
	CHECK(marked == 0);
	struct sweet_fb_cut_rot rt[2]; unsigned nr = 0;
	if (cw == SWEET_FB_CUT_OK) { CHECK(sweet_fb_cut_rots(&cc, FW, rt, &nr)); cut_ok++; }
	/* full picture, unpacked */
	uint32_t want[6] = {1, 0, 0, 0, 0, 0}, info[4];
	sweet_fb_snapc_picture(&c, want, FW, FH, out, line, tmp, rd, NULL, info);
	CHECK(info[0] == FW && info[1] == FH && info[2] == FW * FH * 3 && info[3] == 1);
	CHECK(failing < 0 ? !(c.flags & SWEET_FB_SNAPC_F_MARKED) : 1);
	for (uint32_t y = 0; y < FH; y++) {
		for (uint32_t x = 0; x < FW; x++) {
			int ux = (int)x;
			if (nr && (int)y < cc.band) ux = sweet_fb_cut_inv_x(rt, nr, (int)x);
			uint32_t e = ref_px(L, nl, ux, (int)y);
			const uint8_t *g = out + 3 * ((uint64_t)y * FW + x);
			uint32_t got = (uint32_t)g[0] << 16 | g[1] << 8 | g[2];
			if (got != e) {
				printf("FAIL mode %d test %u: pixel %u,%u got %06x want %06x (layers %u pm %u csw %u cut %s nr %u band %d)\n", mode, tests, x, y, got, e, nl, pm, csw,
				    sweet_fb_cut_why_name(cw), nr, cc.band);
				for (unsigned i = 0; i < nl; i++) printf("  L%u %d,%d %dx%d src %d,%d a %u yuv %d con %08x\n", i, L[i].in.dx, L[i].in.dy, L[i].in.w, L[i].in.h, L[i].in.sx, L[i].in.sy, L[i].in.alpha, L[i].yuv, L[i].con);
				exit(1);
			}
		}
	}
	rgb_px += FW * FH;
	memcpy(unp, out, (size_t)FW * FH * 3);
	/* subsampled crops, packed and unpacked: equal to the full picture sampled */
	for (int k = 0; k < 3; k++) {
		uint32_t sc = 1 + rr(8), x0 = rr(FW), y0 = rr(FH), cw2 = rr(4) ? 1 + rr(FW) : 0, ch = 1 + rr(FH), pk = rr(2);
		uint32_t w2[6] = {sc, pk, x0, y0, cw2, ch};
		if (failing >= 0) { S[failing].fail = 1; }
		(void)sweet_fb_snapc_prepare(&c, &p, src, NS, FW, FH);
		sweet_fb_snapc_picture(&c, w2, FW, FH, out, line, tmp, rd, NULL, info);
		uint32_t ex0 = x0, ey0 = y0, ew = cw2, eh = ch;
		if (ew == 0 || eh == 0 || ex0 >= FW || ey0 >= FH) { ex0 = ey0 = 0; ew = FW; eh = FH; }
		if (ew > FW - ex0) ew = FW - ex0;
		if (eh > FH - ey0) eh = FH - ey0;
		CHECK(info[0] == ew / sc && info[1] == eh / sc && info[3] == (sc | pk << 4));
		static uint8_t dec[FW * FH * 3];
		uint32_t nb = pk ? decode(out, info[2], dec) : info[2];
		if (!pk) memcpy(dec, out, info[2]);
		CHECK(nb == info[0] * info[1] * 3);
		CHECK(info[2] <= sweet_fb_snapc_bound(info[0], info[1]));
		for (uint32_t j = 0; j < info[1]; j++)
			for (uint32_t i = 0; i < info[0]; i++)
				CHECK(memcmp(dec + 3 * ((uint64_t)j * info[0] + i), unp + 3 * ((uint64_t)(ey0 + j * sc) * FW + ex0 + i * sc), 3) == 0);
	}
	tests++;
	for (unsigned i = 0; i < NS; i++) free(S[i].mem);
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 40;
	/* fixed checks: an address outside every source and an unknown CFMT are painted magenta, counted, never guessed */
	{
		static uint8_t m[64 * 64 * 4]; memset(m, 0x80, sizeof m);
		NS = 1; S[0].dev = 0xC0000000ull; S[0].len = sizeof m; S[0].mem = m; S[0].fail = 0;
		struct sweet_fb_hw_plan p; memset(&p, 0, sizeof p);
		p.slot[0][0] = (struct sweet_fb_hw_slot){1, 0xC0000000u, 256, (10u << 16) | 10u, (5u << 16) | 5u, SWEET_FB_HW_CON_OPAQUE, 0};
		p.slot[0][1] = (struct sweet_fb_hw_slot){1, 0xB0000000u, 256, (4u << 16) | 4u, (6u << 16) | 6u, 0x21ffu, 0};      /* not in a source */
		p.slot[1][0] = (struct sweet_fb_hw_slot){1, 0xC0000000u, 256, (4u << 16) | 4u, (0u << 16) | 0u, 0x10ffu, 0};      /* CFMT 1: not modelled */
		p.slot[1][1] = (struct sweet_fb_hw_slot){1, 0xC0000000u, 256, (70u << 16) | 4u, (0u << 16) | 100u, 0x21ffu, 0};   /* reads past the end */
		p.src_con[0] = 3; p.src_con[1] = 3;
		struct sweet_fb_snapc_src s0 = {S[0].dev, S[0].len};
		struct sweet_fb_snapc c;
		CHECK(sweet_fb_snapc_prepare(&c, &p, &s0, 1, FW, FH) == 3 && (c.flags & SWEET_FB_SNAPC_F_MARKED));
		uint32_t row[540];
		sweet_fb_snapc_row(&c, 0, 7, row, tmp, rd, NULL);
		CHECK(row[4] == 0 && row[5] == 0x808080 && row[6] == 0xff00ff && row[9] == 0xff00ff && row[10] == 0x808080 && row[15] == 0);
		sweet_fb_snapc_row(&c, 1, 2, row, tmp, rd, NULL);
		CHECK(row[0] == 0xff00ff && row[3] == 0xff00ff && row[4] == 0 && row[100] == 0xff00ff && row[104] == 0);
		/* a slot whose read fails at run time: magenta from then on, counted once */
		p.slot[1][0].con = 0x20ffu; p.slot[1][1].en = 0; p.src_con[1] = 1;
		CHECK(sweet_fb_snapc_prepare(&c, &p, &s0, 1, FW, FH) == 1);
		S[0].fail = 1; sweet_fb_snapc_row(&c, 1, 1, row, tmp, rd, NULL); S[0].fail = 0;
		CHECK(row[0] == 0xff00ff && c.marked == 2);
		/* the range check is exact: a slot whose last row ends on the source's last byte is read, one more row is not */
		{
			struct sweet_fb_hw_plan q; memset(&q, 0, sizeof q);
			q.slot[0][0] = (struct sweet_fb_hw_slot){1, 0xC0000000u, 256, (64u << 16) | 64u, 0, SWEET_FB_HW_CON_OPAQUE, 0};
			q.src_con[0] = 1;
			CHECK(sweet_fb_snapc_prepare(&c, &q, &s0, 1, FW, FH) == 0);
			q.slot[0][0].size = (65u << 16) | 64u;
			CHECK(sweet_fb_snapc_prepare(&c, &q, &s0, 1, FW, FH) == 1);
			q.slot[0][0].size = (64u << 16) | 64u; q.slot[0][0].addr = 0xC0000004u;
			CHECK(sweet_fb_snapc_prepare(&c, &q, &s0, 1, FW, FH) == 1);
		}
		/* corrupt geometry never writes outside the row */
		p.slot[0][1] = (struct sweet_fb_hw_slot){1, 0xC0000000u, 256, (1u << 16) | 600u, (0u << 16) | 500u, 0x20ffu, 0};
		CHECK(sweet_fb_snapc_prepare(&c, &p, &s0, 1, FW, FH) >= 1);
		row[539] = 1; sweet_fb_snapc_row(&c, 0, 0, row, tmp, rd, NULL); CHECK(row[539] == 0xff00ff);
	}
	for (int i = 0; i < n; i++) one(i % 4);
	printf("t_snapc OK: %u random plans (%u with the band rotation, %u YUV layers, %u with an unreadable layer), %u pixels compared exactly\n",
	    tests, cut_ok, yuv_n, fail_n, rgb_px);
	return cut_ok && yuv_n && fail_n ? 0 : 1;
}
