// 0550 host test: punch-hole band rotation (sweet_fb_cutout.h) against the overlay planner (sweet_fb_hw.h).
// Build: c++ -std=c++17 -Wall -Wextra -Werror -I../b/iokit/Kernel/arm test_cutout.cpp   (also compiled as C: test_cutout_c.c)
#include "sweet_fb_blend.h"
#include "sweet_fb_hw.h"
#include "sweet_fb_cutout.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>

struct sweet_fb_cut_cfg sweet_fb_cut_g = SWEET_FB_CUT_DEFAULTS;
uint32_t sweet_fb_cut_live;

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const uint32_t FW = 1080, FH = 2400;

// What the OVL shows at panel (x, y) for a plan: the stack of (slot source pixel address, opaque) bottom first; an opaque slot
// replaces what is below it (AEN 0), an alpha slot is blended over it.  Pipe k shows x in [540k, 540k+540).
struct Hit { uint32_t addr; bool opaque; bool operator==(const Hit &o) const { return addr == o.addr && opaque == o.opaque; } };
static std::vector<Hit> stack_at(const sweet_fb_hw_plan &p, int32_t x, int32_t y)
{
	std::vector<Hit> st;
	unsigned k = static_cast<unsigned>(x) / (FW / 2);
	int32_t lx = x - static_cast<int32_t>(k * (FW / 2));
	for (unsigned i = 0; i < SWEET_FB_HW_NSLOT; i++) {
		const sweet_fb_hw_slot &s = p.slot[k][i];
		if (!s.en || !(p.src_con[k] & (1u << i))) continue;
		int32_t ox = static_cast<int32_t>(s.offset & 0xffff), oy = static_cast<int32_t>(s.offset >> 16);
		int32_t w = static_cast<int32_t>(s.size & 0xffff), h = static_cast<int32_t>(s.size >> 16);
		if (lx < ox || lx >= ox + w || y < oy || y >= oy + h) continue;
		Hit hit{ s.addr + static_cast<uint32_t>(y - oy) * s.pitch + static_cast<uint32_t>(lx - ox) * 4u, !(s.con & SWEET_FB_HW_CON_AEN) };
		if (hit.opaque) st.clear();
		st.push_back(hit);
	}
	return st;
}

static sweet_fb_hw_in full_layer(uint64_t pa, uint32_t alpha)
{
	// a 1125x2436 CoreAnimation surface clipped to the panel exactly as hwInputs does (sweet_fb_layer_clip, offsets 22/18)
	sweet_fb_layer_geom g;
	bool ok = sweet_fb_layer_clip(&g, 0, 0, 0, 0, 0, 0, 1125, 2436, SWEET_FB_CUT_OFFX, SWEET_FB_CUT_OFFY, FW, FH);
	CHECK(ok, "clip");
	CHECK(g.sx == 22 && g.sy == 18 && g.dx == 0 && g.dy == 0 && g.w == 1080 && g.h == 2400, "today's mapping = crop at (22,18), 1:1: %d %d %d %d %d %d",
	    g.sx, g.sy, g.dx, g.dy, g.w, g.h);
	sweet_fb_hw_in l;
	std::memset(&l, 0, sizeof(l));
	l.pa = pa; l.len = 4608ull * 2436; l.offset = 0; l.pitch = 4608; l.format_ok = 1;
	l.sx = g.sx; l.sy = g.sy; l.dx = g.dx; l.dy = g.dy; l.w = g.w; l.h = g.h; l.alpha = alpha;
	return l;
}

// iOS canvas x of panel pixel under the plan's bottom layer source (layer surface pitch 4608, pa known)
static int32_t ios_x_of(uint32_t addr, uint64_t pa) { return static_cast<int32_t>(((addr - static_cast<uint32_t>(pa)) % 4608u) / 4u); }
static int32_t ios_y_of(uint32_t addr, uint64_t pa) { return static_cast<int32_t>((addr - static_cast<uint32_t>(pa)) / 4608u); }

static void test_rotation_math(const sweet_fb_cut_cfg &c, const char *name)
{
	sweet_fb_cut_rot rt[2]; unsigned n = 0;
	CHECK(sweet_fb_cut_rots(&c, FW, rt, &n), "%s: valid", name);
	std::vector<int> seen(FW, 0);
	for (int32_t x = 0; x < static_cast<int32_t>(FW); x++) {
		int32_t f = sweet_fb_cut_fwd_x(rt, n, x);
		CHECK(f >= 0 && f < static_cast<int32_t>(FW), "%s: fwd range %d -> %d", name, x, f);
		if (f >= 0 && f < static_cast<int32_t>(FW)) seen[static_cast<size_t>(f)]++;
		CHECK(sweet_fb_cut_inv_x(rt, n, f) == x, "%s: round trip %d", name, x);
		CHECK(sweet_fb_cut_fwd_x(rt, n, sweet_fb_cut_inv_x(rt, n, x)) == x, "%s: inverse round trip %d", name, x);
		if (x > 0) {      // no scaling: neighbours stay neighbours except at the rotation seams (a translation per piece)
			int32_t d = f - sweet_fb_cut_fwd_x(rt, n, x - 1);
			bool seam = false;
			for (unsigned i = 0; i < n; i++) { if (x == rt[i].a || x == rt[i].b || x == rt[i].b - rt[i].r) seam = true; }
			CHECK(seam || d == 1, "%s: unit step at %d (%d)", name, x, d);
		}
	}
	for (uint32_t x = 0; x < FW; x++) CHECK(seen[x] == 1, "%s: bijection at %u", name, x);
	// outside the band the touch map is the identity
	for (int32_t y = c.band; y < c.band + 3; y++) {
		int32_t x = 100, yy = y; sweet_fb_cut_touch(&c, FW, &x, &yy); CHECK(x == 100 && yy == y, "%s: identity below band", name);
	}
}

// plan with the rotation vs plan without: every band pixel shows exactly the stack the unrotated plan shows at inv(x);
// every other pixel is unchanged; the touch map lands on the iOS pixel that is drawn there.
static void test_plan(const sweet_fb_cut_cfg &c, std::vector<sweet_fb_hw_in> in, uint32_t cap, int expect, const char *name)
{
	sweet_fb_hw_plan p0, p1;
	int r = sweet_fb_hw_plan_makec(&p0, in.data(), static_cast<unsigned>(in.size()), SWEET_FB_HW_DUAL, 0, FW, FH, cap);
	CHECK(r == SWEET_FB_HW_OK, "%s: base plan %d", name, r);
	p1 = p0;
	int cw = sweet_fb_cut_plan(&p1, in.data(), static_cast<unsigned>(in.size()), &c, 0, FW, cap);
	CHECK(cw == expect, "%s: cut %s, expected %s", name, sweet_fb_cut_why_name(cw), sweet_fb_cut_why_name(expect));
	if (cw != SWEET_FB_CUT_OK) { CHECK(std::memcmp(&p0, &p1, sizeof(p0)) == 0, "%s: plan untouched on refusal", name); return; }
	sweet_fb_cut_rot rt[2]; unsigned n = 0;
	sweet_fb_cut_rots(&c, FW, rt, &n);
	for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) CHECK(p1.nslot[k] <= cap, "%s: slots %u <= cap %u", name, p1.nslot[k], cap);
	int bad = 0;
	for (int32_t y = 0; y < c.band + 4 && bad < 5; y++) {
		for (int32_t x = 0; x < static_cast<int32_t>(FW) && bad < 5; x++) {
			int32_t ux = y < c.band ? sweet_fb_cut_inv_x(rt, n, x) : x;
			if (stack_at(p1, x, y) != stack_at(p0, ux, y)) { bad++; CHECK(false, "%s: pixel %d,%d != unrotated %d,%d", name, x, y, ux, y); }
			// touch: shown (x, y) -> iOS canvas point; must be the bottom layer's source pixel drawn there
			int32_t tx = x, ty = y;
			sweet_fb_cut_touch(&c, FW, &tx, &ty);
			std::vector<Hit> st = stack_at(p1, x, y);
			if (!st.empty() && in[0].pa != 0) {
				int32_t ix = ios_x_of(st[0].addr, in[0].pa), iy = ios_y_of(st[0].addr, in[0].pa);
				if (ix != tx + SWEET_FB_CUT_OFFX || iy != ty + SWEET_FB_CUT_OFFY) {
					bad++; CHECK(false, "%s: touch %d,%d -> iOS %d,%d but drawn iOS %d,%d", name, x, y, tx + 22, ty + 18, ix, iy);
				}
			}
		}
	}
	for (int32_t y = c.band; y < static_cast<int32_t>(FH); y += 97)       // below the band: same picture
		for (int32_t x = 0; x < static_cast<int32_t>(FW); x += 13) CHECK(stack_at(p1, x, y) == stack_at(p0, x, y), "%s: below band %d,%d", name, x, y);
	// memory: every piece reads inside a layer's validated source rectangle
	for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++)
		for (unsigned i = p0.nslot[k]; i < p1.nslot[k]; i++) {
			const sweet_fb_hw_slot &s = p1.slot[k][i];
			uint64_t first = s.addr, last = s.addr + static_cast<uint64_t>((s.size >> 16) - 1) * s.pitch + (s.size & 0xffff) * 4u;
			bool inside = false;
			for (const sweet_fb_hw_in &l : in) {
				uint64_t lf = l.pa + l.offset + static_cast<uint64_t>(l.sy) * l.pitch + static_cast<uint64_t>(l.sx) * 4u;
				uint64_t ll = l.pa + l.offset + static_cast<uint64_t>(l.sy + l.h - 1) * l.pitch + static_cast<uint64_t>(l.sx + l.w) * 4u;
				if (first >= lf && last <= ll) inside = true;
			}
			CHECK(inside, "%s: piece %u/%u reads outside every layer", name, k, i);
			CHECK((s.offset & 0xffff) + (s.size & 0xffff) <= FW / 2, "%s: piece inside its pipe ROI", name);
		}
}

int main()
{
	sweet_fb_cut_cfg c = SWEET_FB_CUT_DEFAULTS;
	CHECK(c.band == 114 && c.lead_end == 227 && c.lead_dx == 72 && c.trail_start == 854 && c.trail_dx == 24, "defaults");
	test_rotation_math(c, "default");
	sweet_fb_cut_cfg t = c; t.trail_dx = 12;
	test_rotation_math(t, "trailing 12");
	sweet_fb_cut_cfg lo = c; lo.trail_dx = 0;
	test_rotation_math(lo, "leading only");

	// the estimated clock box, shown, clears the estimated hole (with its +-12 px tolerance) and stays inside the band / pipe 0
	{
		sweet_fb_cut_rot rt[2]; unsigned n = 0; sweet_fb_cut_rots(&c, FW, rt, &n);
		int32_t x0 = sweet_fb_cut_fwd_x(rt, n, SWEET_FB_CUT_CLOCK_X0), x1 = sweet_fb_cut_fwd_x(rt, n, SWEET_FB_CUT_CLOCK_X1 - 1) + 1;
		int32_t hole_right = SWEET_FB_CUT_HOLE_CX + SWEET_FB_CUT_HOLE_R + SWEET_FB_CUT_HOLE_TOL;
		CHECK(x1 - x0 == SWEET_FB_CUT_CLOCK_X1 - SWEET_FB_CUT_CLOCK_X0, "clock moved in one piece");
		CHECK(x0 >= hole_right, "clock left %d clears hole right %d (+tol)", x0, hole_right);
		CHECK(SWEET_FB_CUT_CLOCK_Y1 <= c.band, "clock inside band");
		// before 0550 the clock overlapped the hole
		CHECK(SWEET_FB_CUT_CLOCK_X0 < SWEET_FB_CUT_HOLE_CX + SWEET_FB_CUT_HOLE_R, "unrotated clock overlaps the hole (the bug)");
		// whole hole disc: every shown pixel inside it comes from the notch-zone strip (no leading-ear content under the hole)
		for (int32_t y = SWEET_FB_CUT_HOLE_CY - SWEET_FB_CUT_HOLE_R; y <= SWEET_FB_CUT_HOLE_CY + SWEET_FB_CUT_HOLE_R; y++)
			for (int32_t x = SWEET_FB_CUT_HOLE_CX - SWEET_FB_CUT_HOLE_R; x <= SWEET_FB_CUT_HOLE_CX + SWEET_FB_CUT_HOLE_R; x++) {
				int32_t dx = x - SWEET_FB_CUT_HOLE_CX, dy = y - SWEET_FB_CUT_HOLE_CY;
				if (dx * dx + dy * dy > SWEET_FB_CUT_HOLE_R * SWEET_FB_CUT_HOLE_R || y < 0 || y >= c.band) continue;
				int32_t u = sweet_fb_cut_inv_x(rt, n, x);
				if (u < c.lead_end && u >= SWEET_FB_CUT_CLOCK_X0) { CHECK(false, "hole pixel %d,%d shows leading content x %d", x, y, u); y = 9999; break; }
			}
	}
	// configs refused
	{ sweet_fb_cut_cfg b = c; b.lead_dx = 400; sweet_fb_cut_rot rt[2]; unsigned n; CHECK(!sweet_fb_cut_rots(&b, FW, rt, &n), "ear past pipe 0 refused"); }
	{ sweet_fb_cut_cfg b = c; b.trail_dx = 400; sweet_fb_cut_rot rt[2]; unsigned n; CHECK(!sweet_fb_cut_rots(&b, FW, rt, &n), "trail past pipe 1 refused"); }
	{ sweet_fb_cut_cfg b = c; b.band = 0; sweet_fb_cut_rot rt[2]; unsigned n; CHECK(!sweet_fb_cut_rots(&b, FW, rt, &n), "band 0 refused"); }

	const uint64_t PA0 = 0xC0000000ull, PA1 = 0xC1000000ull, PA2 = 0xC2000000ull;
	// 1 full-screen layer (the usual CoreAnimation frame): 1 + 2 slots in pipe 0
	test_plan(c, { full_layer(PA0, 255) }, 6, SWEET_FB_CUT_OK, "1 layer");
	test_plan(t, { full_layer(PA0, 255) }, 6, SWEET_FB_CUT_OK, "1 layer + trailing 12");
	test_plan(lo, { full_layer(PA0, 255) }, 6, SWEET_FB_CUT_OK, "1 layer, leading only");
	// 2 layers (Photos: grid + backboardd UI with the status bar): 2 + 4 slots
	test_plan(c, { full_layer(PA0, 255), full_layer(PA1, 255) }, 6, SWEET_FB_CUT_OK, "2 layers");
	// 2 layers, the upper one a small pill outside the band: 2 + 2 slots
	{
		sweet_fb_hw_in pill = full_layer(PA1, 255); pill.dx = 300; pill.dy = 900; pill.w = 200; pill.h = 80;
		test_plan(c, { full_layer(PA0, 255), pill }, 6, SWEET_FB_CUT_OK, "2 layers, upper outside band");
		sweet_fb_hw_in top = full_layer(PA1, 128); top.dx = 40; top.dy = 10; top.w = 150; top.h = 60;      // plane alpha, crosses both pieces
		test_plan(c, { full_layer(PA0, 255), top }, 6, SWEET_FB_CUT_OK, "2 layers, upper partly in band with plane alpha");
	}
	// refusals: plan untouched
	test_plan(c, { full_layer(PA0, 255) }, 2, SWEET_FB_CUT_SLOTS, "2L only (4L off): no room");
	test_plan(c, { full_layer(PA0, 255), full_layer(PA1, 255), full_layer(PA2, 255) }, 6, SWEET_FB_CUT_SLOTS, "3 full layers");
	{
		sweet_fb_hw_in l0 = full_layer(PA0, 255); l0.dy = 200; l0.h = 2200; l0.sy = 218;
		test_plan(c, { l0, full_layer(PA1, 255) }, 6, SWEET_FB_CUT_COVER, "bottom layer below the band");
		sweet_fb_hw_in y = full_layer(PA1, 255); y.format_ok = 2; y.pitch = 2304;
		y.dx = 0; y.dy = 0; y.w = 200; y.h = 100; y.sx = 0; y.sy = 0;
		test_plan(c, { full_layer(PA0, 255), y }, 6, SWEET_FB_CUT_YUV, "YUV layer in the band");
	}
	{ sweet_fb_cut_cfg off = c; off.on = 0; test_plan(off, { full_layer(PA0, 255) }, 6, SWEET_FB_CUT_OFF, "kill switch"); }
	{ sweet_fb_cut_cfg off = c; off.on = 0; int32_t x = 10, y = 10; sweet_fb_cut_touch(&off, FW, &x, &y); CHECK(x == 10, "touch identity when off"); }

	// touch: today's mapping (no rotation) is pixel = point*3 - (22,18), i.e. normalised x = (px + 22) / 1125: no distortion
	for (int32_t px = 0; px < 1080; px += 7) {
		double nx = static_cast<double>(px + 22) / 1125.0, ny = static_cast<double>(px * 2 + 18) / 2436.0;
		CHECK(static_cast<int32_t>(nx * 1125.0 + 0.5) - 22 == px && static_cast<int32_t>(ny * 2436.0 + 0.5) - 18 == px * 2, "uniform touch scale");
	}
	std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
	return fails ? 1 : 0;
}
