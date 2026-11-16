#ifndef SWEET_FB_SNAPC_H
#define SWEET_FB_SNAPC_H
/*
 * 0640 (snap-20261114): a debugging picture of what the overlay shows, composed in software from the plan that is programmed now.
 * Pure and host-testable (snap-20261114/test/t_snapc.c): no IOKit, no registers, the pixels are read through a callback.
 *
 * Used ONLY when the USB autotest asks for a screenshot (bsd/dev/nord2_autotest.inc command 3, thread context).  Normal display
 * operation stays CPU-free: nothing here runs per frame.  The driver (SweetFramebuffer.cpp AppleCLCD::snapHw) copies the shown plan
 * (fHwShownPlan, = the registers of the frame on the panel) and the device address range of every layer surface under fLock,
 * retains the surfaces, drops the lock and composes from that copy.
 *
 * Model = the register plan, slot by slot, exactly as the OVLs run it (sweet_fb_hw.h): per pipe k (panel columns k*540 ..), a black
 * background, then slots 0..5 in order (2L L0, 2L L1, then the cascaded 4L L0..L3 over the 2L output), each slot reading
 * size.h rows of size.w pixels from addr with pitch and placing them at offset (pipe-local x, y).  The 0550 band rotation pieces are
 * ordinary slots of that plan, so the rotated status bar comes out as the panel shows it with no special case.
 * Per slot, from its final CON (sweet_fb_hw_con_final, incl. the 0399 swap bits):
 *   CFMT 2/3 (32 bit): the word decoded by sweet_fb_hw_con_decode (the colour-order model);  AEN 0: copied (alpha ignored);
 *     AEN 1 without MAN (straight, pm 0 default): out = (s*ea + d*(255-ea)) / 255, ea = sa*pa/255;
 *     AEN 1 with MAN (premultiplied): out = s*pa/255 + d*(255 - ea)/255.  Rounding: +127 then /255 per step (model, not bit-exact HW).
 *   CFMT 4/5 (packed 4:2:2, 0490): UYVY / YUYV, matrix L_CON[19:16] by sweet_fb_yuv_ref_rgb, BYTE_SWAP = Cb/Cr exchanged, Ln_CLIP
 *     trims [7:0] left / [15:8] right columns of the read span; plane alpha as straight.
 *   anything else, an address that lies in no layer's range, a read leaving the range, a failed read: the slot's rectangle is
 *     painted MAGENTA (255,0,255) instead of guessed, and counted (flags SWEET_FB_SNAPC_F_MARKED).
 * Output: R,G,B bytes per pixel or the 0296 run-length packing (a byte c, then c < 128: c + 1 pixels as they are; c >= 128: one
 * pixel shown c - 126 times; runs go on over row ends), every scale-th pixel of every scale-th row of a crop.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef SWEET_FB_HW_H
#error "sweet_fb_snapc.h needs sweet_fb_hw.h first"
#endif

#define SWEET_FB_SNAPC_MAGENTA      0xffff00ffu     /* 0xAARRGGBB */
#define SWEET_FB_SNAPC_MAX_SRC      8u
#define SWEET_FB_SNAPC_TMP_BYTES    (540u * 4u + 64u)

/* flags of a composition (info word, also in the status) */
#define SWEET_FB_SNAPC_F_HW         0x01u           /* composed from the overlay plan */
#define SWEET_FB_SNAPC_F_MARKED     0x02u           /* at least one slot painted magenta (not readable / format not modelled) */
#define SWEET_FB_SNAPC_F_CUT        0x04u           /* the 0550 band rotation is in the plan (live) */
#define SWEET_FB_SNAPC_F_YUV        0x08u           /* a 4:2:2 slot was composed */
#define SWEET_FB_SNAPC_F_PANEL_OFF  0x10u           /* iOS asked display off (RequestPowerChange 0) */
#define SWEET_FB_SNAPC_F_BLANK      0x20u           /* DCS 0x28 display off was sent: the panel is dark */

/* one layer surface: the device address range the plan's slots point into (pool PA, IOVA or carve-out) */
struct sweet_fb_snapc_src { uint64_t dev, len; };

enum { SWEET_FB_SNAPC_K_OFF = 0, SWEET_FB_SNAPC_K_RGB32, SWEET_FB_SNAPC_K_YUV, SWEET_FB_SNAPC_K_BAD };

struct sweet_fb_snapc_slot {
	uint32_t kind;              /* SWEET_FB_SNAPC_K_* */
	int32_t src;                /* source index (-1: none) */
	uint64_t off;               /* byte offset in the source of the first read pixel of row 0 */
	uint32_t pitch, w, h, ox, oy, con, clip, bpp;
};

struct sweet_fb_snapc {
	uint32_t pw, fh;            /* pipe width (540), panel height */
	struct sweet_fb_snapc_slot s[SWEET_FB_HW_PIPES][SWEET_FB_HW_NSLOT];
	uint32_t marked;            /* slots that cannot be composed (painted magenta) */
	uint32_t flags;
};

/* reads n bytes at byte offset off of source src into dst; 0 = done */
typedef int (*sweet_fb_snapc_read_fn)(void *ctx, int src, uint64_t off, void *dst, uint32_t n);

/* Turns a plan into composition slots (addresses -> source, offset).  Returns the number of slots that will be painted magenta. */
static inline uint32_t
sweet_fb_snapc_prepare(struct sweet_fb_snapc *c, const struct sweet_fb_hw_plan *p, const struct sweet_fb_snapc_src *src, unsigned nsrc,
    uint32_t fw, uint32_t fh)
{
	__builtin_memset(c, 0, sizeof(*c));
	c->pw = fw / SWEET_FB_HW_PIPES; c->fh = fh; c->flags = SWEET_FB_SNAPC_F_HW;
	for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
		for (unsigned i = 0; i < SWEET_FB_HW_NSLOT; i++) {
			const struct sweet_fb_hw_slot *sl = &p->slot[k][i];
			struct sweet_fb_snapc_slot *o = &c->s[k][i];
			o->src = -1;
			if (!sl->en || !(p->src_con[k] & (1u << i))) { o->kind = SWEET_FB_SNAPC_K_OFF; continue; }
			o->con = sweet_fb_hw_con_final(p, k, i);
			o->pitch = sl->pitch; o->w = sl->size & 0xffffu; o->h = sl->size >> 16; o->ox = sl->offset & 0xffffu; o->oy = sl->offset >> 16;
			o->clip = sl->clip;
			uint32_t cf = SWEET_FB_HW_CON_CFMT(o->con);
			o->kind = (cf == 2u || cf == 3u) ? SWEET_FB_SNAPC_K_RGB32 : ((cf == 4u || cf == 5u) ? SWEET_FB_SNAPC_K_YUV : SWEET_FB_SNAPC_K_BAD);
			o->bpp = o->kind == SWEET_FB_SNAPC_K_YUV ? 2u : 4u;
			if (o->kind == SWEET_FB_SNAPC_K_YUV) { c->flags |= SWEET_FB_SNAPC_F_YUV; }
			if (o->w == 0u || o->h == 0u) { o->kind = SWEET_FB_SNAPC_K_OFF; continue; }
			/* geometry clamped to the pipe / panel (the planner never exceeds them; a corrupt plan must not overrun the row buffer) */
			if (o->ox >= c->pw || o->oy >= fh) { o->kind = SWEET_FB_SNAPC_K_OFF; continue; }
			if (o->w > c->pw - o->ox + ((o->kind == SWEET_FB_SNAPC_K_YUV) ? 2u : 0u)) { o->kind = SWEET_FB_SNAPC_K_BAD; o->w = c->pw - o->ox; }
			if (o->h > fh - o->oy) { o->h = fh - o->oy; }
			if (o->kind == SWEET_FB_SNAPC_K_BAD) { c->marked++; continue; }
			uint64_t need = (uint64_t)(o->h - 1u) * o->pitch + (uint64_t)o->w * o->bpp;
			for (unsigned j = 0; j < nsrc; j++) {
				if ((uint64_t)sl->addr >= src[j].dev && (uint64_t)sl->addr - src[j].dev < src[j].len &&
				    (uint64_t)sl->addr - src[j].dev + need <= src[j].len) {
					o->src = (int32_t)j; o->off = (uint64_t)sl->addr - src[j].dev; break;
				}
			}
			if (o->src < 0 || o->pitch == 0u) { o->kind = SWEET_FB_SNAPC_K_BAD; c->marked++; }
		}
	}
	if (c->marked) { c->flags |= SWEET_FB_SNAPC_F_MARKED; }
	return c->marked;
}

static inline uint32_t
sweet_fb_snapc_div255(uint32_t v)
{
	return (v + 127u) / 255u;
}

/* one 0xAARRGGBB source pixel s of a slot with CON con over the destination d (0x..RRGGBB) */
static inline uint32_t
sweet_fb_snapc_blend(uint32_t con, uint32_t s, uint32_t d, bool yuv)
{
	uint32_t pa = con & 0xffu, sa = yuv ? 255u : (s >> 24);
	if (!(con & SWEET_FB_HW_CON_AEN)) { return s & 0xffffffu; }
	uint32_t ea = sweet_fb_snapc_div255(sa * pa), out = 0;
	bool man = !yuv && (con & SWEET_FB_HW_CON_MAN) != 0u;
	for (unsigned sh = 0; sh < 24; sh += 8) {
		uint32_t sc = (s >> sh) & 0xffu, dc = (d >> sh) & 0xffu, v;
		if (man) { v = sweet_fb_snapc_div255(sc * pa) + sweet_fb_snapc_div255(dc * (255u - ea)); }
		else { v = sweet_fb_snapc_div255(sc * ea + dc * (255u - ea)); }
		out |= (v > 255u ? 255u : v) << sh;
	}
	return out;
}

/*
 * Composes panel row y of pipe k into row[0..pw) (0x00RRGGBB; row starts black).  tmp: SWEET_FB_SNAPC_TMP_BYTES bytes.
 * Returns the slots of this row whose read failed (painted magenta, counted into c->marked once per slot).
 */
static inline uint32_t
sweet_fb_snapc_row(struct sweet_fb_snapc *c, unsigned k, uint32_t y, uint32_t *row, uint8_t *tmp, sweet_fb_snapc_read_fn rd, void *ctx)
{
	uint32_t failed = 0;
	for (uint32_t x = 0; x < c->pw; x++) { row[x] = 0u; }
	for (unsigned i = 0; i < SWEET_FB_HW_NSLOT; i++) {
		struct sweet_fb_snapc_slot *o = &c->s[k][i];
		if (o->kind == SWEET_FB_SNAPC_K_OFF || y < o->oy || y >= o->oy + o->h) { continue; }
		uint32_t x0 = o->ox, n = o->w;
		bool bad = o->kind == SWEET_FB_SNAPC_K_BAD;
		uint32_t bytes = n * o->bpp;
		if (!bad && (bytes > SWEET_FB_SNAPC_TMP_BYTES || rd(ctx, o->src, o->off + (uint64_t)(y - o->oy) * o->pitch, tmp, bytes) != 0)) {
			bad = true; o->kind = SWEET_FB_SNAPC_K_BAD; c->marked++; c->flags |= SWEET_FB_SNAPC_F_MARKED; failed++;
		}
		if (bad) {
			for (uint32_t x = 0; x < n && x0 + x < c->pw; x++) { row[x0 + x] = SWEET_FB_SNAPC_MAGENTA & 0xffffffu; }
			continue;
		}
		if (o->kind == SWEET_FB_SNAPC_K_RGB32) {
			for (uint32_t x = 0; x < n && x0 + x < c->pw; x++) {
				uint32_t w = (uint32_t)tmp[4 * x] | (uint32_t)tmp[4 * x + 1] << 8 | (uint32_t)tmp[4 * x + 2] << 16 | (uint32_t)tmp[4 * x + 3] << 24;
				row[x0 + x] = sweet_fb_snapc_blend(o->con, sweet_fb_hw_con_decode(o->con, w), row[x0 + x], false);
			}
		} else {      /* packed 4:2:2: pairs of pixels share Cb/Cr; the CLIP columns of the read span are not shown */
			uint32_t left = o->clip & 0xffu, right = (o->clip >> 8) & 0xffu, mtx = (o->con >> 16) & 0xfu;
			bool uyvy = SWEET_FB_HW_CON_CFMT(o->con) == 4u, swap = (o->con & SWEET_FB_YUV_CON_BYTE_SWAP) != 0u;
			for (uint32_t x = left; x + right < n; x++) {
				uint32_t px = x0 + x - left;
				if (px >= c->pw) { break; }
				const uint8_t *q = tmp + 4u * (x / 2u);
				uint32_t yy = uyvy ? q[1 + 2 * (x & 1u)] : q[2 * (x & 1u)];
				uint32_t cb = uyvy ? q[0] : q[1], cr = uyvy ? q[2] : q[3];
				if (swap) { uint32_t t = cb; cb = cr; cr = t; }
				uint32_t rgb = sweet_fb_yuv_ref_rgb(mtx, yy, cb, cr);
				if (rgb == 0xff000000u) { rgb = SWEET_FB_SNAPC_MAGENTA & 0xffffffu; c->flags |= SWEET_FB_SNAPC_F_MARKED; }
				row[px] = sweet_fb_snapc_blend(o->con, rgb, row[px], true);
			}
		}
	}
	return failed;
}

/* ---- 0296 picture packing (same bytes as the old AppleCLCD::snap) ---- */
struct sweet_fb_snapc_pack { uint8_t *d, *lit; uint32_t prev, run, packed; };

static inline void
sweet_fb_snapc_pack_init(struct sweet_fb_snapc_pack *pk, uint8_t *buf, bool packed)
{
	pk->d = buf; pk->lit = NULL; pk->prev = 0; pk->run = 0; pk->packed = packed ? 1u : 0u;
}

static inline void
sweet_fb_snapc_pack_flush(struct sweet_fb_snapc_pack *pk)
{
	if (pk->run >= 2u) { *pk->d++ = (uint8_t)(126u + pk->run); pk->lit = NULL; }
	else if (pk->run == 1u) { if (pk->lit == NULL || *pk->lit == 127u) { pk->lit = pk->d++; *pk->lit = 0; } else { (*pk->lit)++; } }
	if (pk->run != 0u) { *pk->d++ = (uint8_t)(pk->prev >> 16); *pk->d++ = (uint8_t)(pk->prev >> 8); *pk->d++ = (uint8_t)pk->prev; }
	pk->run = 0;
}

static inline void
sweet_fb_snapc_pack_put(struct sweet_fb_snapc_pack *pk, uint32_t p)
{
	p &= 0xffffffu;
	if (!pk->packed) { *pk->d++ = (uint8_t)(p >> 16); *pk->d++ = (uint8_t)(p >> 8); *pk->d++ = (uint8_t)p; return; }
	if (pk->run != 0u && p == pk->prev && pk->run < 129u) { pk->run++; return; }
	sweet_fb_snapc_pack_flush(pk);
	pk->prev = p; pk->run = 1;
}

/* worst-case output bytes for w x h pixels */
static inline uint64_t
sweet_fb_snapc_bound(uint32_t w, uint32_t h)
{
	return (uint64_t)w * h * 3u + (uint64_t)w * h / 64u + 64u;
}

/*
 * The whole picture: crop (x0, y0, pw, ph) of the fw x fh panel, every scale-th pixel / row, into out (sweet_fb_snapc_bound bytes).
 * line: fw words, tmp: SWEET_FB_SNAPC_TMP_BYTES.  info: [0] width, [1] height, [2] bytes, [3] scale | packed << 4 (the 0296 words).
 */
static inline void
sweet_fb_snapc_picture(struct sweet_fb_snapc *c, const uint32_t want[6], uint32_t fw, uint32_t fh, uint8_t *out, uint32_t *line, uint8_t *tmp,
    sweet_fb_snapc_read_fn rd, void *ctx, uint32_t info[4])
{
	uint32_t scale = want[0], packed = want[1] ? 1u : 0u;
	uint32_t x0 = want[2], y0 = want[3], pw = want[4], ph = want[5];
	if (scale < 1u || scale > 8u) { scale = 2u; }
	if (pw == 0u || ph == 0u || x0 >= fw || y0 >= fh) { x0 = y0 = 0u; pw = fw; ph = fh; }
	if (pw > fw - x0) { pw = fw - x0; }
	if (ph > fh - y0) { ph = fh - y0; }
	uint32_t w = pw / scale, h = ph / scale;
	struct sweet_fb_snapc_pack pk;
	sweet_fb_snapc_pack_init(&pk, out, packed != 0u);
	for (uint32_t j = 0; j < h; j++) {
		uint32_t y = y0 + j * scale;
		uint32_t xa = x0, xb = x0 + (w ? (w - 1u) * scale : 0u);
		for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
			uint32_t c0 = k * c->pw;
			if (w == 0u || xb < c0 || xa >= c0 + c->pw) { continue; }      /* this pipe has no sampled column in the crop */
			(void)sweet_fb_snapc_row(c, k, y, line + c0, tmp, rd, ctx);
		}
		for (uint32_t i = 0; i < w; i++) { sweet_fb_snapc_pack_put(&pk, line[x0 + i * scale]); }
	}
	if (packed) { sweet_fb_snapc_pack_flush(&pk); }
	info[0] = w; info[1] = h; info[2] = (uint32_t)(pk.d - out); info[3] = scale | packed << 4;
}

#endif /* SWEET_FB_SNAPC_H */
