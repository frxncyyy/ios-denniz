/*
 * 0490 host test: sweet_fb_yuv.h (classification, L_CON format bits, 4:2:2 column rule, reference matrices) and the planner /
 * register lists of sweet_fb_hw.h with packed 4:2:2 layers.  Built twice by make_patch.sh:
 *   -I b                  the full test; prints a digest of BGRA-only plans + register lists at the end
 *   -I a -DYUV_BASE       the same BGRA-only digest from the unpatched header (must be equal: no change for BGRA layers)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_hw.h"

#ifndef YUV_BASE
static unsigned checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#endif

static uint32_t rs = 0x1234567u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint64_t dg = 1469598103934665603ull;
static void dig(uint32_t v) { for (int i = 0; i < 4; i++) { dg ^= (v >> (8 * i)) & 0xffu; dg *= 1099511628211ull; } }

#define FW 1080u
#define FH 2400u
#define PA 0xC1000000ull

static struct sweet_fb_hw_base base(void)
{
	struct sweet_fb_hw_base b;
	memset(&b, 0, sizeof(b));
	b.src_con = 1; b.pitch[0] = 0x00ff1100u; b.gmc[0] = 0x1234; b.fifo[0] = 0x5678; b.datapath = 4; b.gmc_fb = 9; b.fifo_fb = 7;
	return b;
}

/* BGRA-only plans: random layers, every result and register list into the digest */
static void bgra_digest(void)
{
	rs = 0x1234567u;
	for (unsigned t = 0; t < 4000; t++) {
		struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN];
		unsigned n = 1 + rnd() % 4;
		memset(in, 0, sizeof(in));
		for (unsigned i = 0; i < n; i++) {
			uint32_t sw = 64 + rnd() % 1100, sh = 64 + rnd() % 2400;
			in[i].pitch = (sw * 4 + 63) & ~63u; in[i].offset = (rnd() % 4) ? 0 : 4096;
			in[i].pa = PA + (uint64_t)i * 0x1000000u; in[i].len = (uint64_t)in[i].pitch * sh + in[i].offset;
			in[i].format_ok = (rnd() % 16) ? 1 : 0;
			in[i].w = 1 + (int32_t)(rnd() % sw); in[i].h = 1 + (int32_t)(rnd() % sh);
			if (in[i].w > (int32_t)FW) { in[i].w = FW; }
			if (in[i].h > (int32_t)FH) { in[i].h = FH; }
			in[i].sx = (int32_t)(rnd() % (sw - (uint32_t)in[i].w + 1)); in[i].sy = (int32_t)(rnd() % (sh - (uint32_t)in[i].h + 1));
			in[i].dx = (int32_t)(rnd() % (FW - (uint32_t)in[i].w + 1)); in[i].dy = (int32_t)(rnd() % (FH - (uint32_t)in[i].h + 1));
			in[i].alpha = (rnd() % 8) ? 0 : rnd() % 256;
			if (i == 0 && (rnd() % 3) == 0) { in[i].sx = in[i].sy = in[i].dx = in[i].dy = 0; in[i].w = (int32_t)(sw < FW ? sw : FW); }
		}
		struct sweet_fb_hw_plan p;
		int r = sweet_fb_hw_plan_makec(&p, in, n, (rnd() & 1) ? SWEET_FB_HW_DUAL : SWEET_FB_HW_SINGLE, rnd() % 4, FW, FH, 2 + rnd() % 5);
		dig((uint32_t)r);
		if (r != SWEET_FB_HW_OK) { continue; }
		sweet_fb_hw_plan_csw(&p, rnd() % 4);
		struct sweet_fb_hw_base b = base();
		for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
			struct sweet_fb_hw_reg rg[SWEET_FB_HW_MAX_REGS];
			unsigned m = sweet_fb_hw_regs(rg, &p, k, &b);
			dig(m); for (unsigned j = 0; j < m && j < SWEET_FB_HW_MAX_REGS; j++) { dig(rg[j].off); dig(rg[j].val); dig(rg[j].mask); }
			m = sweet_fb_hw_regs4(rg, &p, k, &b);
			dig(m); for (unsigned j = 0; j < m && j < SWEET_FB_HW_MAX_REGS; j++) { dig(rg[j].off); dig(rg[j].val); dig(rg[j].mask); }
			dig(p.src_con[k]); dig(p.nslot[k]);
		}
	}
	printf("BGRA_DIGEST %016llx\n", (unsigned long long)dg);
}

#ifndef YUV_BASE
static uint32_t regval(const struct sweet_fb_hw_reg *r, unsigned n, uint32_t off, uint32_t *mask)
{
	for (unsigned i = 0; i < n; i++) { if (r[i].off == off) { if (mask) { *mask = r[i].mask; } return r[i].val; } }
	CHECK(!"register not in the list");
	return 0xdeadbeefu;
}

static void test_classify(void)
{
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1) == SWEET_FB_YUV_UYVY);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_2VUY, 0, 4, 2, 1) == SWEET_FB_YUV_UYVY);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_YUVS, 0, 2, 1, 1) == SWEET_FB_YUV_YUYV);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_YUVF, 0, 4, 2, 1) == SWEET_FB_YUV_YUYV);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_2VUY, 0, 4, 1, 1) == SWEET_FB_YUV_BAD);     /* the BGRA default layout */
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_2VUY, 1, 2, 1, 1) == SWEET_FB_YUV_BAD);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_YUVS, 0, 2, 1, 2) == SWEET_FB_YUV_BAD);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_YUVS, 0, 2, 2, 1) == SWEET_FB_YUV_BAD);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_420V, 2, 1, 1, 1) == SWEET_FB_YUV_BIPLANAR);
	CHECK(sweet_fb_yuv_classify(SWEET_FB_YUV_FCC_420F, 2, 1, 1, 1) == SWEET_FB_YUV_BIPLANAR);
	CHECK(sweet_fb_yuv_classify(0x42475241u, 0, 4, 1, 1) == SWEET_FB_YUV_NONE);
	CHECK(SWEET_FB_YUV_FCC_2VUY == (((uint32_t)'2' << 24) | ('v' << 16) | ('u' << 8) | 'y'));
	CHECK(SWEET_FB_YUV_FCC_YUVS == (((uint32_t)'y' << 24) | ('u' << 16) | ('v' << 8) | 's'));
	CHECK(SWEET_FB_YUV_FCC_YUVF == (((uint32_t)'y' << 24) | ('u' << 16) | ('v' << 8) | 'f'));
	CHECK(SWEET_FB_YUV_FCC_420V == (((uint32_t)'4' << 24) | ('2' << 16) | ('0' << 8) | 'v'));
	CHECK(SWEET_FB_YUV_FCC_420F == (((uint32_t)'4' << 24) | ('2' << 16) | ('0' << 8) | 'f'));
}

static void test_con(void)
{
	/* the DRM driver's values: fmt_uyvy 4 << 12, fmt_yuyv 5 << 12, MTX 4 / 6 / 7 << 16 */
	CHECK(sweet_fb_yuv_con(1, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1920, 1080) == 0x00074000u);
	CHECK(sweet_fb_yuv_con(1, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1280, 720) == 0x00074000u);
	CHECK(sweet_fb_yuv_con(1, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 720, 576) == 0x00064000u);
	CHECK(sweet_fb_yuv_con(1, SWEET_FB_YUV_FCC_YUVS, 0, 4, 2, 1, 640, 480) == 0x00065000u);
	CHECK(sweet_fb_yuv_con(1, SWEET_FB_YUV_FCC_YUVF, 0, 2, 1, 1, 1920, 1080) == 0x00045000u);
	CHECK(sweet_fb_yuv_con(0x61, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1920, 1080) == 0x00064000u);    /* matrix override */
	CHECK(sweet_fb_yuv_con(0x101, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1920, 1080) == 0x01074000u);   /* byte swap */
	CHECK(sweet_fb_yuv_con(0x201, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1920, 1080) == 0x00075000u);   /* exchanged */
	CHECK(sweet_fb_yuv_con(0x201, SWEET_FB_YUV_FCC_YUVS, 0, 2, 1, 1, 640, 480) == 0x00064000u);
	CHECK(sweet_fb_yuv_con(0, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1920, 1080) == 0u);                /* off */
	CHECK(sweet_fb_yuv_con(0x3f0, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1920, 1080) == 0u);            /* variants without bit 0 */
	CHECK(sweet_fb_yuv_con(3, SWEET_FB_YUV_FCC_2VUY, 0, 2, 1, 1, 1920, 1080) == 0u);                /* reserved bit */
	CHECK(sweet_fb_yuv_con(1, SWEET_FB_YUV_FCC_420F, 2, 1, 1, 1, 1920, 1080) == 0u);
	CHECK(sweet_fb_yuv_con(1, SWEET_FB_YUV_FCC_2VUY, 0, 4, 1, 1, 1920, 1080) == 0u);
	CHECK(sweet_fb_yuv_con(1, 0x42475241u, 0, 4, 1, 1, 1920, 1080) == 0u);
	CHECK(sweet_fb_yuv_mode_valid(0) && sweet_fb_yuv_mode_valid(0x3f1) && !sweet_fb_yuv_mode_valid(2) && !sweet_fb_yuv_mode_valid(0x400));
	for (uint32_t m = 0; m <= 0x3ff; m++) {     /* a planned layer always has format bits, all inside the owned mask and the read-back mask */
		uint32_t c = sweet_fb_yuv_con(m, SWEET_FB_YUV_FCC_YUVS, 0, 2, 1, 1, 100, 100);
		CHECK((c != 0u) == (sweet_fb_yuv_mode_valid(m) && (m & 1u)));
		CHECK((c & ~SWEET_FB_YUV_CON_MASK) == 0u && (c & ~0x03fff1ffu) == 0u);
		if (c) { uint32_t f = (c >> 12) & 0xfu; CHECK(f == 4u || f == 5u); }
	}
	CHECK(sweet_fb_yuv_con_alpha(0x00074000u, 0) == 0x000740ffu);
	CHECK(sweet_fb_yuv_con_alpha(0x00074000u, 255) == 0x000740ffu);
	CHECK(sweet_fb_yuv_con_alpha(0x01065000u, 128) == 0x01065180u);
}

static void test_span(void)
{
	for (int32_t sx = 0; sx < 40; sx++) {
		for (int32_t w = 1; w < 40; w++) {
			int32_t rx = -1, rw = -1;
			uint32_t c = sweet_fb_yuv_span(sx, w, &rx, &rw);
			int32_t cl = (int32_t)(c & 0xffu), cr = (int32_t)((c >> 8) & 0xffu);
			CHECK((c & ~0x0101u) == 0u);
			CHECK((rx & 1) == 0 && (rw & 1) == 0 && rw > 0);
			CHECK(rx + cl == sx && rw - cl - cr == w);
			CHECK(rx >= 0 && rx <= sx && rx + rw >= sx + w && rx + rw <= sx + w + 1);
		}
	}
}

static void test_matrix(void)
{
	CHECK(sweet_fb_yuv_ref_rgb(SWEET_FB_YUV_MTX_BT601, 16, 128, 128) == 0x000000u);
	CHECK(sweet_fb_yuv_ref_rgb(SWEET_FB_YUV_MTX_BT601, 235, 128, 128) == 0xffffffu);
	CHECK(sweet_fb_yuv_ref_rgb(SWEET_FB_YUV_MTX_BT709, 16, 128, 128) == 0x000000u);
	CHECK(sweet_fb_yuv_ref_rgb(SWEET_FB_YUV_MTX_BT709, 235, 128, 128) == 0xffffffu);
	CHECK(sweet_fb_yuv_ref_rgb(SWEET_FB_YUV_MTX_JPEG, 0, 128, 128) == 0x000000u);
	CHECK(sweet_fb_yuv_ref_rgb(SWEET_FB_YUV_MTX_JPEG, 255, 128, 128) == 0xffffffu);
	CHECK(sweet_fb_yuv_ref_rgb(SWEET_FB_YUV_MTX_BT601, 126, 128, 128) == 0x808080u);
	CHECK(sweet_fb_yuv_ref_rgb(5, 126, 128, 128) == 0xff000000u);
	/* primaries (8-bit codes of pure red / green / blue), within 2 codes */
	static const struct { uint32_t m, y, cb, cr, rgb; } t[] = {
		{ SWEET_FB_YUV_MTX_BT601, 81, 90, 240, 0xff0000u }, { SWEET_FB_YUV_MTX_BT601, 145, 54, 34, 0x00ff00u },
		{ SWEET_FB_YUV_MTX_BT601, 41, 240, 110, 0x0000ffu }, { SWEET_FB_YUV_MTX_BT709, 63, 102, 240, 0xff0000u },
		{ SWEET_FB_YUV_MTX_BT709, 173, 42, 26, 0x00ff00u }, { SWEET_FB_YUV_MTX_BT709, 32, 240, 118, 0x0000ffu },
		{ SWEET_FB_YUV_MTX_JPEG, 76, 85, 255, 0xff0000u }, { SWEET_FB_YUV_MTX_JPEG, 150, 44, 21, 0x00ff00u },
		{ SWEET_FB_YUV_MTX_JPEG, 29, 255, 107, 0x0000ffu },
	};
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		uint32_t v = sweet_fb_yuv_ref_rgb(t[i].m, t[i].y, t[i].cb, t[i].cr);
		for (int s = 0; s < 24; s += 8) { int d = (int)((v >> s) & 0xff) - (int)((t[i].rgb >> s) & 0xff); CHECK(d >= -2 && d <= 2); }
	}
}

/* what the modelled OVL shows of one slot row: pairs from addr, the CLIP columns dropped; out[] = (Y << 16) | (Cb << 8) | Cr */
static unsigned ovl_row(const uint8_t *mem, uint64_t mem_pa, const struct sweet_fb_hw_slot *s, uint32_t row, uint32_t *out)
{
	uint32_t w = s->size & 0xffffu, cl = s->clip & 0xffu, cr = (s->clip >> 8) & 0xffu, n = 0;
	const uint8_t *p = mem + (s->addr - mem_pa) + (uint64_t)row * s->pitch;
	bool uyvy = ((s->con >> 12) & 0xfu) == 4u;
	for (uint32_t x = cl; x < w - cr; x++) {
		const uint8_t *q = p + (x / 2u) * 4u;
		uint32_t y = uyvy ? q[1 + 2 * (x & 1u)] : q[2 * (x & 1u)], cb = uyvy ? q[0] : q[1], crr = uyvy ? q[2] : q[3];
		out[n++] = (y << 16) | (cb << 8) | crr;
	}
	return n;
}

static void test_plan(void)
{
	struct sweet_fb_hw_plan p;
	struct sweet_fb_hw_in in[3];
	struct sweet_fb_hw_base b = base();
	struct sweet_fb_hw_reg rg[SWEET_FB_HW_MAX_REGS];
	uint32_t mask = 0;

	/* 1280x720 '2vuy' at 0,0 over the seam, even everywhere: no CLIP, 2 bytes per pixel */
	memset(in, 0, sizeof(in));
	in[0].pa = PA; in[0].pitch = 2560; in[0].len = 2560u * 720u; in[0].format_ok = 0x00074000u;
	in[0].w = 1080; in[0].h = 700; in[0].sx = 100; in[0].sy = 10; in[0].dx = 0; in[0].dy = 200;
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_OK);
	CHECK(p.slot[0][0].addr == (uint32_t)(PA + 10u * 2560u + 100u * 2u) && p.slot[1][0].addr == (uint32_t)(PA + 10u * 2560u + 640u * 2u));
	CHECK(p.slot[0][0].size == ((700u << 16) | 540u) && p.slot[1][0].size == ((700u << 16) | 540u));
	CHECK(p.slot[0][0].clip == 0 && p.slot[1][0].clip == 0 && p.slot[0][0].con == 0x000740ffu && p.slot[1][0].con == 0x000740ffu);
	CHECK(p.slot[0][0].offset == (200u << 16) && p.slot[0][0].pitch == 2560);
	unsigned m = sweet_fb_hw_regs(rg, &p, 0, &b);
	CHECK(m <= SWEET_FB_HW_MAX_REGS);
	CHECK(regval(rg, m, SWEET_FB_HW_R_CON(0), &mask) == 0x000740ffu && (0x000740ffu & ~mask) == 0u);
	CHECK(regval(rg, m, SWEET_FB_HW_R_CLIP(0), NULL) == 0u && regval(rg, m, SWEET_FB_HW_R_PITCH(0), NULL) == (0x00ff0000u | 2560u));

	/* odd destination: pipe 0 shows 439 columns (right CLIP), pipe 1 starts on an odd source column (left CLIP) */
	in[0].sx = 0; in[0].w = 978; in[0].dx = 101;
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_OK);
	CHECK(p.slot[0][0].size == ((700u << 16) | 440u) && p.slot[0][0].clip == 0x100u && p.slot[0][0].offset == ((200u << 16) | 101u));
	CHECK(p.slot[1][0].addr == (uint32_t)(PA + 10u * 2560u + 438u * 2u) && p.slot[1][0].size == ((700u << 16) | 540u));
	CHECK(p.slot[1][0].clip == 0x1u && p.slot[1][0].offset == (200u << 16));
	m = sweet_fb_hw_regs(rg, &p, 1, &b);
	CHECK(regval(rg, m, SWEET_FB_HW_R_CLIP(0), &mask) == 1u && mask == 0xffffffffu);

	/* upper slots (BGRA below, YUV above; then in a 4L slot with plane alpha) */
	in[1] = in[0]; in[0].format_ok = 1; in[0].pitch = 4352; in[0].len = 4352u * 2400u; in[0].sx = in[0].sy = in[0].dx = in[0].dy = 0;
	in[0].w = 1080; in[0].h = 2400; in[1].pa = PA + 0x2000000u; in[2] = in[1]; in[2].alpha = 128; in[2].format_ok = 0x00065000u;
	CHECK(sweet_fb_hw_plan_makec(&p, in, 3, SWEET_FB_HW_DUAL, 0, FW, FH, 6) == SWEET_FB_HW_OK);
	CHECK(p.slot[0][0].con == SWEET_FB_HW_CON_OPAQUE && p.slot[0][0].clip == 0 && p.slot[0][1].con == 0x000740ffu && p.slot[0][2].con == 0x00065180u);
	m = sweet_fb_hw_regs(rg, &p, 0, &b);
	CHECK(regval(rg, m, SWEET_FB_HW_R_CON(1), NULL) == 0x000740ffu && regval(rg, m, SWEET_FB_HW_R_CLIP(1), NULL) == 0x100u);
	CHECK(regval(rg, m, SWEET_FB_HW_R_CLIP(0), NULL) == 0u);
	m = sweet_fb_hw_regs4(rg, &p, 1, &b);
	CHECK(m <= SWEET_FB_HW_MAX_REGS);
	CHECK(regval(rg, m, SWEET_FB_HW_R_CON(0), NULL) == 0x00065180u && regval(rg, m, SWEET_FB_HW_R_CLIP(0), NULL) == 1u);

	/* refusals: a pitch that holds the shown columns but not the extra one; a surface end inside the extra column */
	memset(in, 0, sizeof(in));
	in[0].pa = PA; in[0].pitch = 100; in[0].len = 100u * 10u; in[0].format_ok = 0x00064000u;
	in[0].w = 49; in[0].h = 10; in[0].sx = 1;      /* reads columns 0..49 = 100 bytes: fits */
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_OK);
	in[0].pitch = 96;                              /* 98 bytes shown, 100 read */
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_W_PITCH);
	in[0].pitch = 100; in[0].len = 999;
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_W_RANGE);
	in[0].len = 1000; in[0].pitch = 102;           /* not a multiple of 4 */
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_W_PITCH);
	in[0].pitch = 100; in[0].format_ok = 0;
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_W_FORMAT);
	/* a BGRA layer of the same geometry needs 4 bytes per pixel: the 2-byte pitch is refused for it */
	in[0].format_ok = 1;
	CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2) == SWEET_FB_HW_W_PITCH);
}

/* random 4:2:2 layers: every slot stays inside the surface, and the modelled OVL shows exactly the surface's samples */
static void test_random(void)
{
	static uint8_t mem[1284 * 2 * 64 + 4096 + 64];
	static uint32_t out[1100];
	unsigned planned = 0, clipped = 0;
	for (size_t i = 0; i < sizeof(mem); i++) { mem[i] = (uint8_t)rnd(); }
	for (unsigned t = 0; t < 6000; t++) {
		struct sweet_fb_hw_in l;
		struct sweet_fb_hw_plan p;
		memset(&l, 0, sizeof(l));
		uint32_t sw = 2 + rnd() % 1280, sh = 1 + rnd() % 64, pad = (rnd() % 3) * 4;
		bool uyvy = (rnd() & 1) != 0;
		l.pitch = ((sw * 2 + 3) & ~3u) + pad; l.offset = (rnd() & 1) ? 0 : 4096; l.pa = PA; l.len = (uint64_t)l.pitch * sh + l.offset;
		if (l.len > sizeof(mem)) { continue; }
		l.format_ok = (uyvy ? SWEET_FB_YUV_CFMT_UYVY : SWEET_FB_YUV_CFMT_YUYV) | SWEET_FB_YUV_MTX(6);
		l.w = 1 + (int32_t)(rnd() % (sw < FW ? sw : FW)); l.h = 1 + (int32_t)(rnd() % sh);
		l.sx = (int32_t)(rnd() % (sw - (uint32_t)l.w + 1)); l.sy = (int32_t)(rnd() % (sh - (uint32_t)l.h + 1));
		l.dx = (int32_t)(rnd() % (FW - (uint32_t)l.w + 1)); l.dy = (int32_t)(rnd() % (FH - (uint32_t)l.h + 1));
		int r = sweet_fb_hw_plan_makec(&p, &l, 1, SWEET_FB_HW_DUAL, 0, FW, FH, 2);
		int32_t vx, vw;
		(void)sweet_fb_yuv_span(l.sx, l.w, &vx, &vw);
		if ((uint32_t)(vx + vw) * 2u > l.pitch) { CHECK(r == SWEET_FB_HW_W_PITCH); continue; }
		if ((uint64_t)l.offset + (uint64_t)(l.sy + l.h - 1) * l.pitch + (uint64_t)(vx + vw) * 2u > l.len) { CHECK(r == SWEET_FB_HW_W_RANGE); continue; }
		CHECK(r == SWEET_FB_HW_OK);
		if (r != SWEET_FB_HW_OK) { continue; }
		planned++;
		int32_t shown = 0;
		for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
			const struct sweet_fb_hw_slot *s = &p.slot[k][0];
			if (!s->en) { continue; }
			uint32_t w = s->size & 0xffffu, cl = s->clip & 0xffu, cr = (s->clip >> 8) & 0xffu;
			uint64_t a0 = s->addr - (PA + l.offset), a1 = a0 + (uint64_t)(l.h - 1) * l.pitch + (uint64_t)w * 2u;
			if (s->clip) { clipped++; }
			CHECK((s->size >> 16) == (uint32_t)l.h && (w & 1u) == 0u && cl <= 1u && cr <= 1u && w > cl + cr);
			CHECK(((a0 % l.pitch) & 3u) == 0u);                                  /* starts on a sample pair */
			CHECK(s->addr >= PA + l.offset && l.offset + a1 <= l.len);            /* inside the surface */
			CHECK((a0 % l.pitch) + (uint64_t)w * 2u <= l.pitch);                  /* inside the row */
			CHECK((s->offset >> 16) == (uint32_t)l.dy);
			int32_t px = (int32_t)(s->offset & 0xffffu) + (int32_t)(k * (FW / SWEET_FB_HW_PIPES));   /* panel column of the first shown pixel */
			uint32_t row = rnd() % (uint32_t)l.h;
			unsigned n = ovl_row(mem, PA, s, row, out);
			CHECK(n == w - cl - cr);
			for (unsigned x = 0; x < n; x++) {
				int32_t c = l.sx + (px + (int32_t)x - l.dx);                     /* the surface column this panel column must show */
				const uint8_t *q = mem + l.offset + (uint64_t)(l.sy + (int32_t)row) * l.pitch + (uint64_t)(c / 2) * 4u;
				uint32_t y = uyvy ? q[1 + 2 * (c & 1)] : q[2 * (c & 1)], cb = uyvy ? q[0] : q[1], crr = uyvy ? q[2] : q[3];
				if (out[x] != ((y << 16) | (cb << 8) | crr)) { CHECK(!"sample mismatch"); break; }
			}
			shown += (int32_t)n;
		}
		CHECK(shown == l.w);
	}
	CHECK(planned > 2000 && clipped > 1000);
	printf("random: planned %u, slots with CLIP %u\n", planned, clipped);
}
#endif

int
main(void)
{
#ifndef YUV_BASE
	test_classify();
	test_con();
	test_span();
	test_matrix();
	test_plan();
	test_random();
	printf("%s: %u checks, %u failed\n", fails ? "FAILED" : "PASS", checks, fails);
	if (fails) { return 1; }
#endif
	bgra_digest();
	return 0;
}
