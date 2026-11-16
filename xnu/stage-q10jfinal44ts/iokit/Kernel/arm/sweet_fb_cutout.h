#ifndef SWEET_FB_CUTOUT_H
#define SWEET_FB_CUTOUT_H
/*
 * 0550 (cutout-20261114): the Nord 2's front-camera punch hole (TOP-LEFT of the 1080x2400 panel) sat on the iOS status bar clock.
 * Pure and host-testable (test/test_cutout.cpp); used by SweetFramebuffer.cpp (overlay plan) and hid/SweetHID.cpp (touch).
 *
 * Geometry today [V-code: SweetFramebuffer.cpp fOffX/fOffY, sweet_fb_blend.h sweet_fb_layer_clip, SweetHID.cpp touch()]:
 *   iOS draws a 1125x2436 canvas (iPhone X, 375x812 pt @3x).  The panel shows its centred 1080x2400 part 1:1 -- NO scaling, so NO
 *   aspect distortion: panel (px,py) = iOS (px+22, py+18); touch uses the same +22/+18.  Cropped: iOS columns 0..21 and 1102..1124
 *   (7.3 / 7.7 pt each side) and rows 0..17 / 2418..2435 (6 pt each).  A uniform fit would need a 0.96 scale (1080/1125) and the
 *   OVL has no scaler [V-code: sweet_fb_hw.h header]; that part of the crop is NOT changed here.
 * Status bar [V-disasm UIKitCore 20H392]: +[_UIStatusBarVisualProvider_Split visualProviderSubclassForScreen:...] picks the subclass
 *   by _nativeDisplayBounds width and _exclusionArea size -> _UIStatusBarVisualProvider_Split1125 (+height 44 pt, +notchSize 209x30
 *   pt, +leadingCenteringOffset -2/3 pt, all constants).  Leading items are centred at round(notch.x/2 + 0.15 * cornerRadius) - 2/3 =
 *   ~46.7 pt (iOS x ~140, panel ~118) in the 83-pt left ear (iOS x 0..248 = panel 0..226).  No defaults key / MobileGestalt answer
 *   moves them (the notch is always centred; FixedSplit insets are class-method constants), so the fix is at presentation level.
 * Hole [I, from the denniz Android device trees: config_mainBuiltInDisplayCutout "M -540,0 L -540,107 L -390,107 L -390,0 Z"
 *   (oneplus-mt6893-devs) / "M -544,0 L -544,115 L -382,115 L -382,0 Z" (PixelExperience) = a bounding box x 0..150(158), y 0..107(115)
 *   in panel pixels; rounded_corner_radius 115 px]: hole centre ~(77,56), radius ~33 (+-12 px: not measured).  The estimated clock
 *   (panel x ~58..178, y ~17..50 for "12:34") overlaps it.
 *
 * Fix = "band rotation" in the overlay plan (CPU-free: extra OVL slots reading the same surfaces).  Inside the status bar band
 * (panel rows 0..band-1) the segment [0, lead_end + lead_dx) is rotated right by lead_dx: the left ear [0, lead_end) is shown at
 * [lead_dx, lead_end + lead_dx) (clock right of the hole) and the empty notch-zone strip [lead_end, lead_end + lead_dx) fills
 * [0, lead_dx) around the hole.  No pixel is lost or invented; seams only show on photographic backgrounds (lock/home screen
 * wallpaper at the top-left).  The trailing rotation (trail_dx, default 24) moves the right ear left the same way: the panel's
 * corner (radius ~95..115 px, anchored 22/18 px inside the iOS corner) would otherwise clip ~6..17 px of the battery [I].
 * Each rotation is two pieces per layer that crosses it, drawn above every layer (layer 0's opaque, upper layers' blended in order:
 * the composite of the band is reproduced exactly, only moved).  Needs layer 0 to cover the band and free slots, else the frame is
 * shown unrotated (counted).  Touch is inverse-mapped with the same rotation while the shown frame is rotated.
 */
/*
 * 0551 (sbshift-20261114): OFF BY DEFAULT.  On the phone (q10jcp, user photos IMG_20261114_003513/003547) the rotation moved the clock
 * only to panel x ~125 (the hole is wider/further right than the Android-tree estimate: centre ~(104,72), radius ~35, right edge
 * ~139 +-5 px), and, being a rotation of the composed picture, it also moves the APP content under the translucent status bar: in
 * Photos / scrolling / app-launch zooms the band shows displaced rectangles with seams at x = 72, 299, 830 and 1056 (inherent, not
 * a planner bug: 91% of the frames are a single 1:1 full-screen backboardd layer, which the pieces read correctly).  The status bar
 * is now moved by iOS itself (guest payload sbshift1: UIKitCore Split1125 leading/trailing centring patched), so nothing in the
 * picture is moved.  This code stays as a fallback (n2cut=1 / n2gdv +512 / kern.n2cut=1) with the measured geometry (dx 110 ->
 * clock ~164..293, tdx 32) and is restricted to frames whose ONLY layer is an opaque 1:1 full-panel BGRA layer (any other frame,
 * e.g. the Photos grid + UI two-layer frames, during which pipe 1's upper band rows showed garbled content on the phone -- cause not
 * identified, 6 slots per pipe were in use -- is shown unrotated, reason "layers"); touch follows the frame on the panel as before.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#define SWEET_CUT_U32(x) static_cast<uint32_t>(x)
#define SWEET_CUT_I32(x) static_cast<int32_t>(x)
#define SWEET_CUT_U64(x) static_cast<uint64_t>(x)
#else
#define SWEET_CUT_U32(x) ((uint32_t)(x))
#define SWEET_CUT_I32(x) ((int32_t)(x))
#define SWEET_CUT_U64(x) ((uint64_t)(x))
#endif

/* iOS (Split1125) geometry in canvas pixels */
#define SWEET_FB_CUT_IOS_BAND       132     /* 44 pt status bar */
#define SWEET_FB_CUT_IOS_EAR        249     /* 83 pt left ear = (375 - 209) / 2 */
#define SWEET_FB_CUT_IOS_TRAIL      876     /* 292 pt: right ear start */
#define SWEET_FB_CUT_OFFX           22      /* (1125 - 1080) / 2 */
#define SWEET_FB_CUT_OFFY           18      /* (2436 - 2400) / 2 */

/* hole estimate in panel pixels (documentation + host test; not used by the plan) */
/* 0551: measured on the user's two photos (seams of the 0550 pieces at x 72 / 299 and the band edge y 114 as scale references):
 * centre (102..107, 71..73), radius 33..36 incl. the dark rim -> (104, 72) r 35 +-5 */
#define SWEET_FB_CUT_HOLE_CX        104
#define SWEET_FB_CUT_HOLE_CY        72
#define SWEET_FB_CUT_HOLE_R         35
#define SWEET_FB_CUT_HOLE_TOL       5
/* clock box in panel pixels, unrotated (24 h "00:35", measured: first digit at ~125 with dx 72, 128 px wide, rows ~35..68) */
#define SWEET_FB_CUT_CLOCK_X0       54
#define SWEET_FB_CUT_CLOCK_X1       183
#define SWEET_FB_CUT_CLOCK_Y0       35
#define SWEET_FB_CUT_CLOCK_Y1       68

struct sweet_fb_cut_cfg {
	uint32_t on;            /* 0 off (as before 0550), 1 rotate */
	int32_t band;           /* panel rows of the status bar band */
	int32_t lead_end;       /* panel x where the left ear ends */
	int32_t lead_dx;        /* left ear shift to the right (0 = no leading rotation) */
	int32_t trail_start;    /* panel x where the right ear starts */
	int32_t trail_dx;       /* right ear shift to the left (0 = off; default 24) */
};
#define SWEET_FB_CUT_DEFAULTS { 0u, SWEET_FB_CUT_IOS_BAND - SWEET_FB_CUT_OFFY, SWEET_FB_CUT_IOS_EAR - SWEET_FB_CUT_OFFX, 110, \
	                        SWEET_FB_CUT_IOS_TRAIL - SWEET_FB_CUT_OFFX, 32 }      /* 0551: off, dx 110, tdx 32 */

/* one rotation to the right by r inside [a, b) of the band: src [a, b-r) -> dst a+r ; src [b-r, b) -> dst a */
struct sweet_fb_cut_rot { int32_t a, b, r; };

/* the rotations of a config on a fw-wide panel (0..2); false = config invalid (nothing rotated) */
static inline bool
sweet_fb_cut_rots(const struct sweet_fb_cut_cfg *c, uint32_t fw, struct sweet_fb_cut_rot rot[2], unsigned *nrot)
{
	int32_t w = SWEET_CUT_I32(fw), half = w / 2;
	*nrot = 0;
	if (c->on == 0u) { return true; }
	if (c->band <= 0 || c->band > 400 || c->lead_dx < 0 || c->trail_dx < 0 || c->lead_end <= 0 || c->trail_start >= w) { return false; }
	if (c->lead_dx > 0) {
		if (c->lead_end + c->lead_dx > half) { return false; }      /* pipe 0 only (x < 540) */
		rot[*nrot].a = 0; rot[*nrot].b = c->lead_end + c->lead_dx; rot[*nrot].r = c->lead_dx; (*nrot)++;
	}
	if (c->trail_dx > 0) {
		if (c->trail_start - c->trail_dx < half) { return false; }  /* pipe 1 only */
		rot[*nrot].a = c->trail_start - c->trail_dx; rot[*nrot].b = w; rot[*nrot].r = w - c->trail_start; (*nrot)++;
	}
	return true;
}

/* shown (panel) x of an unrotated panel x in the band */
static inline int32_t
sweet_fb_cut_fwd_x(const struct sweet_fb_cut_rot *rt, unsigned n, int32_t x)
{
	for (unsigned i = 0; i < n; i++) {
		const struct sweet_fb_cut_rot *q = &rt[i];
		if (x < q->a || x >= q->b) { continue; }
		return x < q->b - q->r ? x + q->r : x - (q->b - q->r - q->a);
	}
	return x;
}

/* unrotated panel x of a shown x in the band (touch) */
static inline int32_t
sweet_fb_cut_inv_x(const struct sweet_fb_cut_rot *rt, unsigned n, int32_t x)
{
	for (unsigned i = 0; i < n; i++) {
		const struct sweet_fb_cut_rot *q = &rt[i];
		if (x < q->a || x >= q->b) { continue; }
		return x < q->a + q->r ? x - q->a + (q->b - q->r) : x - q->r;
	}
	return x;
}

/* touch: panel point as shown -> panel point of the unrotated picture (the caller adds the +22/+18 canvas offset) */
static inline void
sweet_fb_cut_touch(const struct sweet_fb_cut_cfg *c, uint32_t fw, int32_t *x, int32_t *y)
{
	struct sweet_fb_cut_rot rt[2];
	unsigned n = 0;
	if (*y < 0 || *y >= c->band || !sweet_fb_cut_rots(c, fw, rt, &n)) { return; }
	*x = sweet_fb_cut_inv_x(rt, n, *x);
}

/* the plan part needs sweet_fb_hw.h first (SweetFramebuffer.cpp); hid/SweetHID.cpp only uses the touch part above */
enum { SWEET_FB_CUT_OK = 0, SWEET_FB_CUT_OFF, SWEET_FB_CUT_CFG, SWEET_FB_CUT_COVER, SWEET_FB_CUT_YUV, SWEET_FB_CUT_SLOTS, SWEET_FB_CUT_LAYERS,
       SWEET_FB_CUT_NREASON };
static inline const char *
sweet_fb_cut_why_name(int w)
{
	static const char *const nm[SWEET_FB_CUT_NREASON] = { "ok", "off", "config", "cover", "yuv", "slots", "layers" };
	return (w >= 0 && w < SWEET_FB_CUT_NREASON) ? nm[w] : "?";
}

#ifdef SWEET_FB_HW_H
/*
 * Adds the rotation pieces to a plan made by sweet_fb_hw_plan_makec from in[0..n) (same fw, pm, cap).  On anything but
 * SWEET_FB_CUT_OK the plan is left exactly as it was (the frame is shown unrotated).  Every piece reads a sub-rectangle of a
 * layer's already validated source rectangle (same surface, same pitch), so no new memory range is read.
 */
static inline int
sweet_fb_cut_plan(struct sweet_fb_hw_plan *p, const struct sweet_fb_hw_in *in, unsigned n, const struct sweet_fb_cut_cfg *c,
    uint32_t pm, uint32_t fw, uint32_t cap)
{
	struct sweet_fb_cut_rot rt[2];
	unsigned nr = 0;
	if (c->on == 0u) { return SWEET_FB_CUT_OFF; }
	if (!sweet_fb_cut_rots(c, fw, rt, &nr)) { return SWEET_FB_CUT_CFG; }
	if (nr == 0u || n == 0u) { return SWEET_FB_CUT_OFF; }
	/* 0551: only a frame whose single layer is an opaque, 1:1, full-panel BGRA layer (the usual backboardd frame) */
	if (n != 1u || in[0].format_ok != 1u || (in[0].alpha != 0u && in[0].alpha < 255u) || in[0].dx != 0 || in[0].dy != 0 ||
	    SWEET_CUT_U32(in[0].w) != fw || in[0].h < c->band) { return SWEET_FB_CUT_LAYERS; }
	cap &= 0xffu;
	if (cap > SWEET_FB_HW_NSLOT) { cap = SWEET_FB_HW_NSLOT; }
	int32_t pw = SWEET_CUT_I32(fw / SWEET_FB_HW_PIPES), band = c->band;
	struct sweet_fb_hw_plan q = *p;
	for (unsigned ri = 0; ri < nr; ri++) {
		const struct sweet_fb_cut_rot *r = &rt[ri];
		unsigned k = r->a < pw ? 0u : 1u;
		int32_t c0 = SWEET_CUT_I32(k) * pw;
		/* layer 0 (opaque bottom) must cover the whole rotated rectangle */
		const struct sweet_fb_hw_in *l0 = &in[0];
		if (l0->dx > r->a || l0->dx + l0->w < r->b || l0->dy > 0 || l0->dy + l0->h < band) { return SWEET_FB_CUT_COVER; }
		unsigned ns = q.nslot[k];
		for (unsigned i = 0; i < n; i++) {
			const struct sweet_fb_hw_in *l = &in[i];
			for (unsigned piece = 0; piece < 2u; piece++) {
				int32_t s0 = piece == 0u ? r->a : r->b - r->r, s1 = piece == 0u ? r->b - r->r : r->b;
				int32_t delta = piece == 0u ? r->r : -(r->b - r->r - r->a);
				int32_t x0 = l->dx > s0 ? l->dx : s0, x1 = l->dx + l->w < s1 ? l->dx + l->w : s1;
				int32_t y0 = l->dy > 0 ? l->dy : 0, y1 = l->dy + l->h < band ? l->dy + l->h : band;
				if (x1 <= x0 || y1 <= y0) { continue; }
				if (l->format_ok != 1u) { return SWEET_FB_CUT_YUV; }
				if (ns >= cap) { return SWEET_FB_CUT_SLOTS; }
				int32_t sx = l->sx + (x0 - l->dx), sy = l->sy + (y0 - l->dy), dx = x0 + delta;
				struct sweet_fb_hw_slot *s = &q.slot[k][ns];
				uint32_t a = (l->alpha == 0u || l->alpha >= 255u) ? 255u : l->alpha;
				s->en = 1u;
				s->addr = SWEET_CUT_U32(l->pa + l->offset + SWEET_CUT_U64(sy) * SWEET_CUT_U64(l->pitch) + SWEET_CUT_U64(sx) * 4ull);
				s->pitch = l->pitch;
				s->size = (SWEET_CUT_U32(y1 - y0) << 16) | SWEET_CUT_U32(x1 - x0);
				s->offset = (SWEET_CUT_U32(y0) << 16) | SWEET_CUT_U32(dx - c0);
				s->con = (i == 0u && a == 255u) ? SWEET_FB_HW_CON_OPAQUE : ((sweet_fb_hw_con_upper(pm) & ~0xffu) | a);
				s->clip = 0u;
				q.src_con[k] |= 1u << ns;
				ns++;
				q.nslot[k] = ns;
			}
		}
	}
	*p = q;
	return SWEET_FB_CUT_OK;
}
#endif /* SWEET_FB_HW_H */

#ifdef __cplusplus
extern "C" {
#endif
extern struct sweet_fb_cut_cfg sweet_fb_cut_g;      /* SweetFramebuffer.cpp: kern.n2cut* */
extern uint32_t sweet_fb_cut_live;                  /* 1 = the frame on the panel is rotated (touch follows it) */
#ifdef __cplusplus
}
#endif

#endif /* SWEET_FB_CUTOUT_H */
