/* 0415 copy of the 0414 test: test-layer arrays sized 4 (0402 test 7 signature), build with -Wno-missing-field-initializers (0402 added sweet_fb_hw_in.alpha; 0 = opaque).
 *
 * 0414 (colorfix-20261113): default pm = SWEET_FB_HW_PM_DEFAULT = 0: the driver's default two-layer plan writes layer 1 CON 0x000021ff
 * (CFMT 2 + AEN, the LK-verified layout) and layer 0 0x20ff; only those count as verified (sweet_fb_hw_con_verified); 0x028031ff and
 * 0x038031ff are both UNVERIFIED (user: still orange with 0x028031ff).  The pm 2 / pm 3 model checks below stay as checks of the 0399
 * hypothesis model only.
 * 0399 host test (from 0396's): the OVL model now DECODES every pixel through sweet_fb_hw_con_decode(final CON) -- the colour-order
 * assumption is explicit and checked (0396's model ignored CFMT/BYTE_SWAP, which is why the R/B swap of layer 1 was not caught).
 * plan / blend / address math of sweet_fb_hw.h against
 *   (1) a pixel-level model of the two OVL*_2L pipes (each pipe reads its slots from simulated physical memory with the planned
 *       ADDR/PITCH/SIZE/OFFSET, background BGCLR, layer 0 opaque, layer 1 per the CON variant), compared with
 *   (2) the CPU path itself: sweet_fb_layer_clip + sweet_fb_compose_row (sweet_fb_blend.h, the code presentCopy runs).
 * Plus: register lists (apply then restore = base), rejections, H2 test layers and reference strips, fault policy.
 * Build: cc -O2 -I../b/iokit/Kernel/arm -I<stage>/iokit/Kernel/arm hwfb_host_test.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_blend.h"
#include "sweet_fb_hw.h"

static int fails, quiet;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 30) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(uint32_t n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return n ? (uint32_t)(rng % n) : 0; }

/* simulated physical memory: the display pool (sparse: only what the surfaces use) */
#define MAXSURF 4
struct surf { uint64_t pa; uint32_t w, h, pitch, offset; uint64_t len; uint32_t *mem; };

static uint32_t
mem_read(const struct surf *s, unsigned ns, uint64_t pa)
{
	for (unsigned i = 0; i < ns; i++) {
		if (pa >= s[i].pa && pa + 4 <= s[i].pa + s[i].len) { return s[i].mem[(pa - s[i].pa) / 4]; }
	}
	return 0xdeadbeefu;     /* outside every surface: the plan must never make the OVL read here */
}

/* pixel model of one pipe: what OVL pipe k outputs at ROI (x, y) */
static uint32_t
ovl_px(const struct sweet_fb_hw_plan *p, unsigned k, uint32_t x, uint32_t y, const struct surf *s, unsigned ns, int *bad)
{
	uint32_t out = SWEET_FB_HW_BGCLR_BLACK;
	for (unsigned i = 0; i < SWEET_FB_HW_SLOTS; i++) {
		const struct sweet_fb_hw_slot *l = &p->slot[k][i];
		if (!((p->src_con[k] >> i) & 1u)) { continue; }
		uint32_t ox = l->offset & 0xffffu, oy = l->offset >> 16, w = l->size & 0xffffu, h = l->size >> 16;
		if (x < ox || x >= ox + w || y < oy || y >= oy + h) { continue; }
		uint64_t pa = (uint64_t)l->addr + (uint64_t)(y - oy) * l->pitch + (uint64_t)(x - ox) * 4u;
		uint32_t raw = mem_read(s, ns, pa);
		if (raw == 0xdeadbeefu) { (*bad)++; }
		uint32_t con = sweet_fb_hw_con_final(p, k, i);
		uint32_t px = sweet_fb_hw_con_decode(con, raw);      /* 0399: what the OVL sees under this CON */
		if (!(con & 0x100u)) { out = px; continue; }                       /* AEN 0: opaque */
		if ((con & 0xf000u) == 0x3000u && (con & (1u << 23))) { out = sweet_fb_hw_over_pm(px, out); }   /* premultiplied */
		else { out = sweet_fb_hw_over_straight(px, out); }
	}
	return out;
}

/* the whole panel by the OVL model vs by the CPU path */
static int
compare_panel(const struct sweet_fb_hw_plan *p, const struct surf *s, const struct sweet_fb_layer_geom *g, unsigned n, uint32_t fw, uint32_t fh,
    int stride, int *oob)
{
	int diff = 0;
	uint32_t *row = malloc(fw * 4);
	for (uint32_t y = 0; y < fh; y += (uint32_t)stride) {
		const uint32_t *rows[4];
		for (unsigned i = 0; i < n; i++) {
			rows[i] = (y >= (uint32_t)g[i].dy && y < (uint32_t)(g[i].dy + g[i].h)) ?
			    s[i].mem + ((s[i].offset + (uint64_t)(g[i].sy + (int32_t)(y - g[i].dy)) * s[i].pitch) / 4) + g[i].sx : NULL;
		}
		uint32_t x0 = 0, x1 = 0;
		memset(row, 0, fw * 4);
		bool any = sweet_fb_compose_row(row, rows, g, n, &x0, &x1, NULL);
		for (uint32_t x = 0; x < fw; x++) {
			uint32_t cpu = (any && x >= x0 && x < x1) ? row[x] : 0;
			unsigned k = x / (fw / 2);
			uint32_t hw = ovl_px(p, k, x - k * (fw / 2), y, s, n, oob);
			if ((cpu & 0xffffffu) != (hw & 0xffffffu)) {
				if (diff++ < 3 && !quiet) { printf("  diff at %u,%u cpu %08x hw %08x\n", x, y, cpu, hw); }
			}
		}
	}
	free(row);
	return diff;
}

static void
fill_surface(struct surf *s, bool premul_alpha, bool opaque)
{
	s->mem = malloc(s->len);
	for (uint64_t i = 0; i < s->len / 4; i++) {
		uint32_t a = opaque ? 255 : (rnd(4) == 0 ? 0 : (rnd(3) == 0 ? 255 : rnd(256)));
		uint32_t r = rnd(256), gg = rnd(256), b = rnd(256);
		if (premul_alpha) { r = r * a / 255; gg = gg * a / 255; b = b * a / 255; }
		if (a == 0 && rnd(2)) { r = gg = b = 0; }
		s->mem[i] = (a << 24) | (r << 16) | (gg << 8) | b;
	}
}

int
main(void)
{
	const uint32_t FW = 1080, FH = 2400, LW = 1125, LH = 2436;
	const int32_t OX = (LW - FW) / 2, OY = (LH - FH) / 2;

	/* 1. H3: the full-screen CA surface (1125x2436, pitch 4544) in the pool: crop (22,18), exact addresses per pipe */
	{
		struct sweet_fb_layer_geom g;
		CHECK(sweet_fb_layer_clip(&g, 0, 0, 0, 0, LW, LH, LW, LH, OX, OY, FW, FH), "clip");
		CHECK(g.sx == 22 && g.sy == 18 && g.dx == 0 && g.dy == 0 && g.w == 1080 && g.h == 2400, "geom %d %d %d %d %d %d", g.sx, g.sy, g.dx, g.dy, g.w, g.h);
		struct sweet_fb_hw_in in = { 0xc1000000ull, 11075584ull, 0, 4544, 1, g.sx, g.sy, g.dx, g.dy, g.w, g.h };
		struct sweet_fb_hw_plan p;
		int r = sweet_fb_hw_plan_make(&p, &in, 1, SWEET_FB_HW_SINGLE, 2, FW, FH);
		CHECK(r == SWEET_FB_HW_OK, "single plan %d", r);
		CHECK(p.slot[0][0].addr == 0xc1000000u + 18 * 4544 + 22 * 4, "pipe0 addr %08x", p.slot[0][0].addr);
		CHECK(p.slot[1][0].addr == 0xc1000000u + 18 * 4544 + (22 + 540) * 4, "pipe1 addr %08x", p.slot[1][0].addr);
		CHECK(p.slot[0][0].size == ((2400u << 16) | 540u) && p.slot[1][0].size == p.slot[0][0].size, "size");
		CHECK(p.slot[0][0].offset == 0 && p.slot[1][0].offset == 0, "offset");
		CHECK(p.slot[0][0].con == SWEET_FB_HW_CON_OPAQUE && p.src_con[0] == 1 && p.src_con[1] == 1 && !p.slot[0][1].en, "con/src_con");
		CHECK(p.slot[0][0].pitch == 4544, "pitch");
		/* the same surface in ordinary memory, a non-covering one, two layers in SINGLE */
		struct sweet_fb_hw_in z = in; z.pa = 0;
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_SINGLE, 2, FW, FH) == SWEET_FB_HW_W_MEMORY, "iobmd -> memory");
		z = in; z.w = 1000;
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_SINGLE, 2, FW, FH) == SWEET_FB_HW_W_COVER, "cover");
		struct sweet_fb_hw_in two[2] = { in, in };
		CHECK(sweet_fb_hw_plan_make(&p, two, 2, SWEET_FB_HW_SINGLE, 2, FW, FH) == SWEET_FB_HW_W_COUNT, "count");
		CHECK(sweet_fb_hw_plan_make(&p, two, 2, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_OK, "dual 2");
		CHECK(sweet_fb_hw_plan_make(&p, &in, 1, SWEET_FB_HW_OFF, 2, FW, FH) == SWEET_FB_HW_W_MODE, "mode off");
		CHECK(sweet_fb_hw_plan_make(&p, &in, 1, SWEET_FB_HW_DRY, 2, FW, FH) == SWEET_FB_HW_W_MODE, "mode dry");
		CHECK(sweet_fb_hw_plan_make(&p, &in, 0, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_W_NONE, "none");
		z = in; z.format_ok = 0;
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_W_FORMAT, "format");
		z = in; z.pa = 0xcff00000ull;      /* runs past the pool end */
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_W_RANGE, "pool end");
		z = in; z.pa = 0x1c0000000ull;     /* above 4 GiB (XNU RAM) */
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_W_RANGE, "above 4G");
		z = in; z.len = 2417ull * 4544 + 1102 * 4 - 4;      /* the last pixel read lies outside the surface */
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_W_RANGE, "len");
		z.len += 4;
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_OK, "len exact");
		z = in; z.pitch = 0x10000;
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_W_PITCH, "pitch max");
		z = in; z.pitch = 4542;
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_DUAL, 2, FW, FH) == SWEET_FB_HW_W_PITCH, "pitch align");
		z = in; z.pa = 0x76e10000ull;      /* the carve-out is reachable too (test patterns) */
		z.len = 4544ull * 2436;
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_SINGLE, 2, FW, FH) == SWEET_FB_HW_OK, "carve-out reachable");
		z.pa = 0x78400000ull;      /* runs past the carve-out end */
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_SINGLE, 2, FW, FH) == SWEET_FB_HW_W_RANGE, "carve-out end");
		z.pa = 0x80000000ull;      /* between the windows */
		CHECK(sweet_fb_hw_plan_make(&p, &z, 1, SWEET_FB_HW_SINGLE, 2, FW, FH) == SWEET_FB_HW_W_RANGE, "not a window");
	}

	/* 2. random 1-2 layer scenes: OVL model == CPU path (pm 2 = premultiplied), never a read outside a surface */
	int scenes = 0, ok_scenes = 0, total_diff = 0, total_oob = 0, rejected = 0;
	for (int t = 0; t < 3000; t++) {
		unsigned n = 1 + rnd(2);
		struct surf s[2];
		struct sweet_fb_hw_in in[4];
		struct sweet_fb_layer_geom g[2];
		uint64_t pa = 0xc0000000ull + (uint64_t)rnd(16) * 0x1000000ull;
		bool vis_all = true;
		for (unsigned i = 0; i < n; i++) {
			bool full = rnd(3) == 0;
			s[i].w = full ? LW : 16 + rnd(1200);
			s[i].h = full ? LH : 16 + rnd(1400);
			s[i].pitch = ((s[i].w * 4 + 63) & ~63u) + 64u * rnd(3);
			s[i].offset = 4096u * rnd(2);
			s[i].len = (uint64_t)s[i].offset + (uint64_t)s[i].pitch * s[i].h;
			s[i].pa = pa; pa += (s[i].len + 0xffff) & ~0xffffull;
			fill_surface(&s[i], true, i == 0);
			int32_t sx = full ? 0 : (int32_t)rnd(8), sy = full ? 0 : (int32_t)rnd(8);
			int32_t dx = full ? 0 : (int32_t)rnd(LW), dy = full ? 0 : (int32_t)rnd(LH);
			int32_t w = rnd(4) == 0 ? 0 : (int32_t)s[i].w, h = w == 0 ? 0 : (int32_t)s[i].h;
			if (!sweet_fb_layer_clip(&g[i], sx, sy, dx, dy, w, h, s[i].w, s[i].h, OX, OY, FW, FH)) { vis_all = false; }
			struct sweet_fb_hw_in l = { s[i].pa, s[i].len, s[i].offset, s[i].pitch, 1, g[i].sx, g[i].sy, g[i].dx, g[i].dy, g[i].w, g[i].h };
			in[i] = l;
		}
		if (vis_all) {
			struct sweet_fb_hw_plan p;
			int r = sweet_fb_hw_plan_make(&p, in, n, SWEET_FB_HW_DUAL, 2, FW, FH);
			scenes++;
			if (r == SWEET_FB_HW_OK) {
				int oob = 0;
				int d = compare_panel(&p, s, g, n, FW, FH, 37 + (int)rnd(20), &oob);
				total_diff += d; total_oob += oob;
				if (d == 0 && oob == 0) { ok_scenes++; }
				CHECK(d == 0 && oob == 0, "scene %d n=%u diff %d oob %d", t, n, d, oob);
				/* every planned address is 32-bit, 4-aligned, inside its surface */
				for (unsigned k = 0; k < 2; k++) {
					for (unsigned i = 0; i < n; i++) {
						if (!p.slot[k][i].en) { continue; }
						CHECK((p.slot[k][i].addr & 3u) == 0 && p.slot[k][i].addr >= s[i].pa && p.slot[k][i].addr < s[i].pa + s[i].len, "addr");
						CHECK((p.slot[k][i].size & 0xffffu) <= FW / 2 && (p.slot[k][i].offset & 0xffffu) + (p.slot[k][i].size & 0xffffu) <= FW / 2, "pipe width");
					}
				}
			} else { rejected++; }
		}
		for (unsigned i = 0; i < n; i++) { free(s[i].mem); }
	}
	printf("random scenes: %d planned+compared %d exact, %d rejected, pixel diffs %d, out-of-surface reads %d\n", scenes, ok_scenes, rejected, total_diff, total_oob);
	CHECK(ok_scenes > 2000, "too few scenes");

	/* 3. register lists: apply then restore = base; values and masks */
	{
		struct sweet_fb_hw_base b;
		memset(&b, 0, sizeof(b));
		b.src_con = 0x00000201u; b.bgclr = 0x12345678u;
		b.con[0] = 0x21ffu; b.size[0] = (2400u << 16) | 540u; b.pitch[0] = 0x00ab1100u; b.rdma_ctrl[0] = 0x81u; b.gmc[0] = 0x40404040u; b.fifo[0] = 0x0123u;
		b.addr[0] = 0x76e10000u; b.addr[1] = 0x11110000u; b.rdma_ctrl[1] = 0x80u; b.pitch_msb[0] = 0x100u;
		CHECK(sweet_fb_hw_base_ok(&b, 0x76e10000u, 0x1100u, 540, 2400), "base ok");
		CHECK(!sweet_fb_hw_base_ok(&b, 0x77806000u, 0x1100u, 540, 2400), "base addr");
		uint32_t regs[0x1000 / 4];
		memset(regs, 0, sizeof(regs));
		regs[SWEET_FB_HW_R_SRC_CON / 4] = b.src_con; regs[SWEET_FB_HW_R_ROI_BGCLR / 4] = b.bgclr;
		for (unsigned i = 0; i < 2; i++) {
			regs[SWEET_FB_HW_R_CON(i) / 4] = b.con[i]; regs[SWEET_FB_HW_R_SRC_SIZE(i) / 4] = b.size[i]; regs[SWEET_FB_HW_R_OFFSET(i) / 4] = b.offset[i];
			regs[SWEET_FB_HW_R_PITCH(i) / 4] = b.pitch[i]; regs[SWEET_FB_HW_R_PITCH_MSB(i) / 4] = b.pitch_msb[i]; regs[SWEET_FB_HW_R_CLIP(i) / 4] = b.clip[i];
			regs[SWEET_FB_HW_R_RDMA_CTRL(i) / 4] = b.rdma_ctrl[i]; regs[SWEET_FB_HW_R_RDMA_GMC(i) / 4] = b.gmc[i]; regs[SWEET_FB_HW_R_RDMA_FIFO(i) / 4] = b.fifo[i];
			regs[SWEET_FB_HW_R_ADDR(i) / 4] = b.addr[i];
		}
		uint32_t saved[0x1000 / 4];
		memcpy(saved, regs, sizeof(regs));
		struct sweet_fb_hw_in two[2] = {
			{ 0xc1000000ull, 11075584ull, 0, 4544, 1, 22, 18, 0, 0, 1080, 2400 },
			{ 0xc2000000ull, 4544ull * 300, 0, 4544, 1, 0, 0, 400, 1000, 300, 300 } };
		struct sweet_fb_hw_plan p;
		CHECK(sweet_fb_hw_plan_make(&p, two, 2, SWEET_FB_HW_DUAL, SWEET_FB_HW_PM_DEFAULT, FW, FH) == SWEET_FB_HW_OK, "plan");
		struct sweet_fb_hw_reg r[SWEET_FB_HW_MAX_REGS];
		unsigned nr = sweet_fb_hw_regs(r, &p, 0, &b);
		CHECK(nr <= SWEET_FB_HW_MAX_REGS, "regs count %u", nr);
		for (unsigned i = 0; i < nr; i++) { regs[r[i].off / 4] = r[i].val; CHECK(r[i].off < 0x1000 && (r[i].off & 3) == 0, "off"); }
		CHECK(regs[SWEET_FB_HW_R_SRC_CON / 4] == 0x203u, "src_con keeps upper bits: %08x", regs[SWEET_FB_HW_R_SRC_CON / 4]);
		CHECK(regs[SWEET_FB_HW_R_ROI_BGCLR / 4] == SWEET_FB_HW_BGCLR_BLACK, "bgclr");
		CHECK(regs[SWEET_FB_HW_R_PITCH(0) / 4] == (0x00ab0000u | 4544u), "L0 pitch keeps LK upper bits %08x", regs[SWEET_FB_HW_R_PITCH(0) / 4]);
		CHECK(regs[SWEET_FB_HW_R_PITCH(1) / 4] == 4544u, "L1 pitch");
		CHECK(regs[SWEET_FB_HW_R_CON(1) / 4] == 0x000021ffu, "L1 con %08x (0414 default: CFMT 2 + AEN straight)", regs[SWEET_FB_HW_R_CON(1) / 4]);
		CHECK(regs[SWEET_FB_HW_R_CON(0) / 4] == 0x20ffu, "L0 con");
		CHECK(regs[SWEET_FB_HW_R_RDMA_GMC(1) / 4] == b.gmc[0] && regs[SWEET_FB_HW_R_RDMA_FIFO(1) / 4] == b.fifo[0], "L1 gmc/fifo from L0");
		CHECK((regs[SWEET_FB_HW_R_RDMA_CTRL(1) / 4] & 1u) && regs[SWEET_FB_HW_R_RDMA_CTRL(1) / 4] == 0x81u, "L1 rdma on");
		CHECK(regs[SWEET_FB_HW_R_OFFSET(1) / 4] == ((1000u << 16) | 400u), "L1 offset");
		CHECK(regs[SWEET_FB_HW_R_PITCH_MSB(0) / 4] == 0x100u, "msb keeps upper bits");
		/* pipe 1 for the same plan: tile at 400..699 -> pipe1 x 0..159, source x + 140 */
		struct sweet_fb_hw_reg r1[SWEET_FB_HW_MAX_REGS];
		unsigned n1 = sweet_fb_hw_regs(r1, &p, 1, &b);
		uint32_t a1 = 0;
		for (unsigned i = 0; i < n1; i++) { if (r1[i].off == SWEET_FB_HW_R_ADDR(1)) { a1 = r1[i].val; } }
		CHECK(a1 == 0xc2000000u + 140u * 4u, "pipe1 L1 addr %08x", a1);
		/* single-layer plan disables L1 (RDMA off) */
		struct sweet_fb_hw_plan p1;
		CHECK(sweet_fb_hw_plan_make(&p1, two, 1, SWEET_FB_HW_SINGLE, 2, FW, FH) == SWEET_FB_HW_OK, "plan1");
		nr = sweet_fb_hw_regs(r, &p1, 0, &b);
		for (unsigned i = 0; i < nr; i++) { regs[r[i].off / 4] = r[i].val; }
		CHECK((regs[SWEET_FB_HW_R_SRC_CON / 4] & 3u) == 1u && !(regs[SWEET_FB_HW_R_RDMA_CTRL(1) / 4] & 1u), "single: L1 off");
		/* restore */
		nr = sweet_fb_hw_regs_base(r, 0, &b, 0x76e10000u);
		CHECK(nr <= SWEET_FB_HW_MAX_REGS, "base regs count %u", nr);
		CHECK(r[0].off == SWEET_FB_HW_R_SRC_CON, "layer enable first");
		for (unsigned i = 0; i < nr; i++) { regs[r[i].off / 4] = r[i].val; }
		CHECK(memcmp(regs, saved, sizeof(regs)) == 0, "restore = base");
		nr = sweet_fb_hw_regs_base(r, 0, &b, 0x77806000u);
		for (unsigned i = 0; i < nr; i++) { if (r[i].off == SWEET_FB_HW_R_ADDR(0)) { CHECK(r[i].val == 0x77806000u, "restore to buffer B"); } }
		/* every list fits; every write is read back with a non-empty mask */
		for (unsigned i = 0; i < nr; i++) { CHECK(r[i].mask != 0, "mask"); }
	}

	/* 4. H2: test layers plan inside the carve-out slot 3; references = the model's output for pm 2 / pm 0 */
	{
		const uint64_t slot = 0x76e10000ull + 2ull * 4352 * 2400;
		CHECK(slot + 4352ull * 2400 <= SWEET_FB_HW_FB_END, "slot fits");
		struct surf ps = { slot, FW, FH, 4352, 0, 4352ull * FH, NULL };
		ps.mem = malloc(ps.len);
		for (uint32_t y = 0; y < FH; y++) { for (uint32_t x = 0; x < FW; x++) { ps.mem[(uint64_t)y * 1088 + x] = sweet_fb_hw_pattern_px(x, y, FW, FH); } }
		for (uint32_t t = 1; t <= 4; t++) {
			struct sweet_fb_hw_in in[4];
			uint32_t pm = 9;
			unsigned n = sweet_fb_hw_test_layers(in, t, slot, 4352, FW, FH, 2, &pm);
			struct sweet_fb_hw_plan p;
			int r = sweet_fb_hw_plan_make(&p, in, n, SWEET_FB_HW_DUAL, pm, FW, FH);
			CHECK(r == SWEET_FB_HW_OK, "test %u plan %d", t, r);
			CHECK(n == (t == 1 ? 1u : 2u), "test %u layers", t);
			if (t >= 2) {
				CHECK(p.slot[0][1].en && p.slot[1][1].en, "tile on both pipes (seam)");
				CHECK((p.slot[0][1].size & 0xffffu) == 150 && (p.slot[1][1].size & 0xffffu) == 150, "150+150 px");
				CHECK((p.slot[1][1].offset & 0xffffu) == 0 && (p.slot[0][1].offset & 0xffffu) == 390, "offsets");
				CHECK(p.slot[1][1].addr == p.slot[0][1].addr + 150 * 4, "seam addr");
			}
			/* model output of the gradient rows vs the reference strips */
			if (t == 2 || t == 3) {
				int bad = 0, oob = 0;
				uint32_t refy = t == 3 ? SWEET_FB_HW_REF_PM_Y : SWEET_FB_HW_REF_ST_Y;
				for (uint32_t row = 0; row < SWEET_FB_HW_TILE / 2; row += 7) {
					for (uint32_t x = SWEET_FB_HW_GRAD_X; x < SWEET_FB_HW_GRAD_X + SWEET_FB_HW_TILE; x++) {
						unsigned k = x / 540;
						uint32_t hw = ovl_px(&p, k, x - k * 540, SWEET_FB_HW_GRAD_Y + row, &ps, 1, &oob);
						uint32_t ref = ps.mem[(uint64_t)(refy + row) * 1088 + x];
						if ((hw & 0xffffffu) != (ref & 0xffffffu)) { bad++; }
					}
				}
				CHECK(bad == 0 && oob == 0, "test %u: HW gradient != its reference strip (%d px)", t, bad);
			}
		}
		/* the two references differ visibly (so a photo can tell them apart): mid gradient */
		uint32_t pmv = ps.mem[(uint64_t)SWEET_FB_HW_REF_PM_Y * 1088 + SWEET_FB_HW_GRAD_X + 150];
		uint32_t stv = ps.mem[(uint64_t)SWEET_FB_HW_REF_ST_Y * 1088 + SWEET_FB_HW_GRAD_X + 150];
		int dr = (int)((pmv >> 16) & 0xff) - (int)((stv >> 16) & 0xff);
		CHECK(dr > 40, "references too close: pm %08x st %08x", pmv, stv);
		printf("H2 references at mid gradient: premultiplied %08x, straight %08x (red differs by %d)\n", pmv, stv, dr);
		/* our premultiplied reference = the CPU path's blend, every alpha */
		int mism = 0;
		for (uint32_t tx = 0; tx < SWEET_FB_HW_TILE; tx++) {
			uint32_t s = sweet_fb_hw_tile_px(tx, 0);
			CHECK(((s >> 16) & 0xff) <= (s >> 24) && ((s >> 8) & 0xff) <= (s >> 24) && (s & 0xff) <= (s >> 24), "tile premultiplied");
			uint32_t cpu = (s >> 24) == 255 ? s : (s == 0 ? SWEET_FB_HW_BLUE : sweet_fb_blend_px(s, SWEET_FB_HW_BLUE));
			if ((cpu & 0xffffffu) != (sweet_fb_hw_over_pm(s, SWEET_FB_HW_BLUE) & 0xffffffu)) { mism++; }
		}
		CHECK(mism == 0, "REF_PM differs from sweet_fb_blend_px in %d px", mism);
		/* exhaustive: over_pm == CPU blend for every premultiplied source alpha/value and destination value per channel */
		mism = 0;
		for (uint32_t a = 0; a < 256; a += 1) {
			for (uint32_t v = 0; v <= a; v += 3) {
				for (uint32_t d = 0; d < 256; d += 5) {
					uint32_t s = (a << 24) | (v << 16) | (v << 8) | v, dd = 0xff000000u | (d << 16) | (d << 8) | d;
					if ((sweet_fb_blend_px(s, dd) & 0xffffffu) != (sweet_fb_hw_over_pm(s, dd) & 0xffffffu)) { mism++; }
				}
			}
		}
		CHECK(mism == 0, "over_pm vs blend_px exhaustive: %d", mism);
		/* seam markers */
		CHECK(ps.mem[100 * 1088 + 539] == 0xffff0000u && ps.mem[100 * 1088 + 540] == 0xff00ff00u, "seam markers");
		free(ps.mem);
	}

	/* 4b. 0399 colour order, explicit.  'BGRA' words as the CPU path reads them (0xAARRGGBB). */
	{
		const uint32_t red = 0xffff0000u, blue = 0xff0000ffu, iosblue = 0xff007affu, iosorange = 0xffff7a00u;
		/* [V-dev] LK / layer 0: CFMT 2 straight, opaque: identity */
		CHECK(sweet_fb_hw_con_decode(0x21ffu, red) == red && sweet_fb_hw_con_decode(SWEET_FB_HW_CON_OPAQUE, blue) == blue, "CFMT2 identity");
		/* [V-dev] the bug: 0396's premultiplied value 0x038031ff showed iOS blue as orange */
		CHECK(sweet_fb_hw_con_decode(0x038031ffu, iosblue) == iosorange, "0x038031ff swaps R/B (photo)");
		/* the fix: 0x028031ff = identity for every word */
		int bad = 0;
		for (uint32_t i = 0; i < 100000; i++) {
			uint32_t w = (uint32_t)rnd(0xffffffffu) ^ (rnd(2) << 31);
			if (sweet_fb_hw_con_decode(sweet_fb_hw_con_upper(2), w) != w) { bad++; }
			if (sweet_fb_hw_con_decode(sweet_fb_hw_con_upper(0), w) != w) { bad++; }
			if (sweet_fb_hw_con_decode(SWEET_FB_HW_CON_OPAQUE, w) != w) { bad++; }
		}
		CHECK(bad == 0, "pm 0 / pm 2 / layer 0 must decode every BGRA word to itself (%d)", bad);
		/* every plan the driver can make with csw 0 and pm 0/2: every enabled slot's final CON is identity */
		struct sweet_fb_hw_in two[2] = {
			{ 0xc1000000ull, 11075584ull, 0, 4544, 1, 22, 18, 0, 0, 1080, 2400 },
			{ 0xc2000000ull, 4544ull * 300, 0, 4544, 1, 0, 0, 400, 1000, 300, 300 } };
		for (uint32_t pm = 0; pm <= 3; pm++) {
			for (uint32_t csw = 0; csw <= 3; csw++) {
				struct sweet_fb_hw_plan p;
				CHECK(sweet_fb_hw_plan_make(&p, two, 2, SWEET_FB_HW_DUAL, pm, FW, FH) == SWEET_FB_HW_OK, "plan");
				sweet_fb_hw_plan_csw(&p, csw);
				bool l0ok = sweet_fb_hw_con_decode(sweet_fb_hw_con_final(&p, 0, 0), iosblue) == iosblue;
				bool l1ok = sweet_fb_hw_con_decode(sweet_fb_hw_con_final(&p, 1, 1), iosblue) == iosblue;
				bool want_l0 = !(csw & 2u), want_l1 = (pm == 3) ? (csw & 1u) != 0 : !(csw & 1u);
				if (pm == 1) { want_l1 = false; }       /* legacy alpha-first layout: never right for BGRA */
				CHECK(l0ok == want_l0 && l1ok == want_l1, "pm %u csw %u: layer0 %d layer1 %d", pm, csw, l0ok, l1ok);
				/* registers carry exactly the final CON */
				struct sweet_fb_hw_base b0; memset(&b0, 0, sizeof(b0));
				struct sweet_fb_hw_reg r[SWEET_FB_HW_MAX_REGS];
				unsigned nr = sweet_fb_hw_regs(r, &p, 1, &b0);
				for (unsigned i = 0; i < nr; i++) {
					if (r[i].off == SWEET_FB_HW_R_CON(1)) { CHECK(r[i].val == sweet_fb_hw_con_final(&p, 1, 1), "reg CON L1"); }
					if (r[i].off == SWEET_FB_HW_R_CON(0)) { CHECK(r[i].val == sweet_fb_hw_con_final(&p, 1, 0), "reg CON L0"); }
				}
			}
		}
		/* test 6 (colour test): layer 1's strip == layer 0's strip on the panel model with pm 2 csw 0; R/B swapped with csw 1 */
		const uint64_t slot = 0x76e10000ull + 2ull * 4352 * 2400;
		struct surf ps = { slot, FW, FH, 4352, 0, 4352ull * FH, NULL };
		ps.mem = malloc(ps.len);
		for (uint32_t y = 0; y < FH; y++) { for (uint32_t x = 0; x < FW; x++) { ps.mem[(uint64_t)y * 1088 + x] = sweet_fb_hw_pattern_px(x, y, FW, FH); } }
		for (uint32_t csw = 0; csw <= 1; csw++) {
			struct sweet_fb_hw_in in[4]; uint32_t pm = 9;
			unsigned n = sweet_fb_hw_test_layers(in, 6, slot, 4352, FW, FH, 2, &pm);
			struct sweet_fb_hw_plan p;
			CHECK(n == 2 && pm == 2 && sweet_fb_hw_plan_make(&p, in, n, SWEET_FB_HW_DUAL, pm, FW, FH) == SWEET_FB_HW_OK, "test 6 plan");
			sweet_fb_hw_plan_csw(&p, csw);
			int diff = 0, oob = 0, l0red = 0;
			for (uint32_t row = 0; row < SWEET_FB_HW_RGB_ROWS; row += 3) {
				for (uint32_t x = SWEET_FB_HW_GRAD_X; x < SWEET_FB_HW_GRAD_X + SWEET_FB_HW_TILE; x++) {
					unsigned k = x / 540;
					uint32_t top = ovl_px(&p, k, x - k * 540, SWEET_FB_HW_RGB_Y0 + row, &ps, 1, &oob);
					uint32_t bot = ovl_px(&p, k, x - k * 540, SWEET_FB_HW_RGB_L1_Y + row, &ps, 1, &oob);
					if ((top & 0xffffffu) != (bot & 0xffffffu)) { diff++; }
					if (x == SWEET_FB_HW_GRAD_X + 10 && (top & 0xffffffu) == 0xff0000u) { l0red++; }
				}
			}
			CHECK(oob == 0 && l0red > 0, "test 6: layer 0 strip starts red");
			CHECK(csw == 0 ? diff == 0 : diff > 0, "test 6 csw %u: layer 1 strip vs layer 0 strip diff %d", csw, diff);
			if (csw == 1) { printf("colour test model: csw=1 makes layer 1's strip differ in %d sampled px (blue|green|red) -> the photo tells the setting apart\n", diff); }
		}
		free(ps.mem);
		/* the 0396 bug reproduced by the model: random two-layer scenes with pm 3 differ from the CPU path, with pm 2 they do not */
		int d3 = 0, d2 = 0;
		for (int t = 0; t < 40; t++) {
			struct surf s2[2];
			struct sweet_fb_layer_geom g[2];
			struct sweet_fb_hw_in in2[2];
			for (unsigned i = 0; i < 2; i++) {
				s2[i].w = i == 0 ? LW : 200; s2[i].h = i == 0 ? LH : 200; s2[i].pitch = (s2[i].w * 4 + 63) & ~63u; s2[i].offset = 0;
				s2[i].len = (uint64_t)s2[i].pitch * s2[i].h; s2[i].pa = 0xc0000000ull + i * 0x1000000ull;
				fill_surface(&s2[i], true, i == 0);
				CHECK(sweet_fb_layer_clip(&g[i], 0, 0, i ? 300 + (int32_t)rnd(400) : 0, i ? 100 + (int32_t)rnd(1000) : 0, 0, 0, s2[i].w, s2[i].h, OX, OY, FW, FH), "clip");
				struct sweet_fb_hw_in l = { s2[i].pa, s2[i].len, 0, s2[i].pitch, 1, g[i].sx, g[i].sy, g[i].dx, g[i].dy, g[i].w, g[i].h };
				in2[i] = l;
			}
			for (uint32_t pm = 2; pm <= 3; pm++) {
				quiet = pm == 3;
				struct sweet_fb_hw_plan p;
				CHECK(sweet_fb_hw_plan_make(&p, in2, 2, SWEET_FB_HW_DUAL, pm, FW, FH) == SWEET_FB_HW_OK, "plan");
				int oob = 0, d = compare_panel(&p, s2, g, 2, FW, FH, 97, &oob);
				if (pm == 2) { d2 += d; } else { d3 += d; }
			}
			free(s2[0].mem); free(s2[1].mem);
		}
		CHECK(d2 == 0 && d3 > 0, "pm2 diffs %d (must be 0), pm3 diffs %d (must be > 0: the model sees the 0396 bug)", d2, d3);
		printf("colour order: pm 2 (0x028031ff) two-layer scenes exact; pm 3 (0x038031ff, 0396 default) differs in %d sampled px (= the photo bug)\n", d3);
	}

	/* 5. fault / race / fence policy */
	{
		uint32_t run = 0;
		CHECK(sweet_fb_hw_frame_fault(0x0003u, 0, false, false, &run) == SWEET_FB_HW_F_NONE, "normal bits (REG_CMT, FME_CPL)");
		CHECK(sweet_fb_hw_frame_fault(0x7fc003u, 0, false, false, &run) == SWEET_FB_HW_F_NONE, "b14..b22 seen on every frame are not faults");
		CHECK(sweet_fb_hw_frame_fault(1u << 2, 0, false, false, &run) == SWEET_FB_HW_F_UNDERFLOW, "FME_UND");
		CHECK(sweet_fb_hw_frame_fault(1u << 10, 0, false, false, &run) == SWEET_FB_HW_F_UNDERFLOW, "SMI underflow");
		CHECK(sweet_fb_hw_frame_fault(0, 1u << 12, false, false, &run) == SWEET_FB_HW_F_DSI, "dsi");
		CHECK(sweet_fb_hw_frame_fault(0, 0, true, false, &run) == SWEET_FB_HW_F_TIMEOUT, "timeout");
		run = 0;
		int f = 0;
		for (unsigned i = 0; i < SWEET_FB_HW_RACE_LIMIT - 1; i++) { f |= sweet_fb_hw_frame_fault(0, 0, false, true, &run); }
		CHECK(f == 0, "races below limit");
		(void)sweet_fb_hw_frame_fault(0, 0, false, false, &run);
		CHECK(run == 0, "race run resets");
		for (unsigned i = 0; i < SWEET_FB_HW_RACE_LIMIT; i++) { f = sweet_fb_hw_frame_fault(0, 0, false, true, &run); }
		CHECK(f == SWEET_FB_HW_F_RACE, "race limit");
		bool b0[2] = { false, false }, b1[2] = { false, true };
		CHECK(sweet_fb_hw_fence_ok(b0, b0, 2), "fence idle");
		CHECK(!sweet_fb_hw_fence_ok(b1, b0, 2) && !sweet_fb_hw_fence_ok(b0, b1, 2), "fence busy/pending");
		/* 0414: default pm and the verified set */
		CHECK(SWEET_FB_HW_PM_DEFAULT == 0u && sweet_fb_hw_con_upper(SWEET_FB_HW_PM_DEFAULT) == 0x000021ffu, "0414 default pm 0 = 0x21ff");
		CHECK(sweet_fb_hw_con_verified(sweet_fb_hw_con_upper(SWEET_FB_HW_PM_DEFAULT)) && sweet_fb_hw_con_verified(SWEET_FB_HW_CON_OPAQUE), "default CONs verified");
		CHECK(!sweet_fb_hw_con_verified(0x008031ffu) && !sweet_fb_hw_con_verified(0x028031ffu) && !sweet_fb_hw_con_verified(0x038031ffu) &&
		    !sweet_fb_hw_con_verified(0x010020ffu), "MAN / swap values unverified");
		{   /* default plan, csw 0: both layers verified and identity in the model, on both pipes */
			struct sweet_fb_hw_in two[2] = {
				{ 0xc1000000ull, 11075584ull, 0, 4544, 1, 22, 18, 0, 0, 1080, 2400 },
				{ 0xc2000000ull, 4544ull * 300, 0, 4544, 1, 0, 0, 400, 1000, 300, 300 } };
			struct sweet_fb_hw_plan p;
			CHECK(sweet_fb_hw_plan_make(&p, two, 2, SWEET_FB_HW_DUAL, SWEET_FB_HW_PM_DEFAULT, FW, FH) == SWEET_FB_HW_OK, "default plan");
			sweet_fb_hw_plan_csw(&p, 0);
			int nv = 0;
			for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
				for (unsigned i = 0; i < SWEET_FB_HW_SLOTS; i++) {
					if (!p.slot[k][i].en) { continue; }
					uint32_t c = sweet_fb_hw_con_final(&p, k, i);
					if (!sweet_fb_hw_con_verified(c) || sweet_fb_hw_con_decode(c, 0xff007affu) != 0xff007affu) { nv++; }
				}
			}
			CHECK(nv == 0, "default plan: %d enabled slots with an unverified / non-identity CON", nv);
			printf("0414 default: layer 0 CON %08x, layer 1 CON %08x (verified CFMT 2 layout)\n", sweet_fb_hw_con_final(&p, 0, 0), sweet_fb_hw_con_final(&p, 1, 1));
		}
		CHECK(sweet_fb_hw_con_upper(0) == 0x21ffu && sweet_fb_hw_con_upper(1) == 0x008031ffu && sweet_fb_hw_con_upper(2) == 0x028031ffu &&
		    sweet_fb_hw_con_upper(3) == 0x038031ffu, "con variants");
	}

	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
	return fails ? 1 : 0;
}
