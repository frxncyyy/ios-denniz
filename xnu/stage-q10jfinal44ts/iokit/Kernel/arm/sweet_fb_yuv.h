#ifndef SWEET_FB_YUV_H
#define SWEET_FB_YUV_H
/*
 * 0490 (yuvovl-20261113): packed 4:2:2 YCbCr IOSurfaces as overlay layers.  Pure and host-testable (test/yuv_host_test.c).
 *
 * What the MT6893 OVL reads (Android DRM driver mtk_disp_ovl.c, mt6885_ovl_driver_data; identical in both vendor trees) [V-code]:
 *   RGB565 / RGB888 / the 32-bit RGB layouts, and two packed 4:2:2 layouts only: UYVY = CFMT 4, YUYV = CFMT 5 (L_CON [15:12]).
 *   No semi-planar or planar input (no second address register per layer): 420v / 420f / NV12 can never be an OVL layer.
 *   The YCbCr -> RGB matrix is chosen per layer by L_CON [19:16] (L_CON_FLD_MTX): 4 = full-range BT.601 ("JPEG"), 6 = video-range
 *   BT.601, 7 = video-range BT.709 (mtk_ovl_yuv_matrix_convert).  Nothing else is programmed for a YUV layer (no EXT_MTX, no
 *   coefficient registers); 4:2:2 needs an even first column and an even column count, so an odd edge reads one column more and
 *   trims it with Ln_CLIP ([7:0] left, [15:8] right) (_ovl_common_config).
 * None of this has run on the phone yet [I]: byte order, matrix codes and the CLIP trim are the DRM driver's, so the path is
 * OFF by default (kern.n2fbyuv = 0) and the mode word carries the test variants to settle them in one boot.
 *
 * kern.n2fbyuv (SWEET_FB_YUV_MODE_*):
 *   bit 0       packed 4:2:2 surfaces ('2vuy', 'yuvs', 'yuvf') are planned as overlay layers (else refused: "format", as before)
 *   bits 7..4   matrix override: 0 = automatic (full range -> 4; else 7 for HD sizes, 6 otherwise), 1..15 = that L_CON MTX code
 *   bit 8       set L_CON BYTE_SWAP on the layer (Cb and Cr exchanged = blue/red tinted picture)
 *   bit 9       exchange the UYVY / YUYV codes (luma and chroma exchanged = green/purple stripes)
 */
#include <stdbool.h>
#include <stdint.h>

#define SWEET_FB_YUV_FCC_2VUY       0x32767579u     /* '2vuy': Cb Y0 Cr Y1, video range */
#define SWEET_FB_YUV_FCC_YUVS       0x79757673u     /* 'yuvs': Y0 Cb Y1 Cr, video range */
#define SWEET_FB_YUV_FCC_YUVF       0x79757666u     /* 'yuvf': Y0 Cb Y1 Cr, full range */
#define SWEET_FB_YUV_FCC_420V       0x34323076u     /* '420v' two planes, video range */
#define SWEET_FB_YUV_FCC_420F       0x34323066u     /* '420f' two planes, full range */

#define SWEET_FB_YUV_CFMT_UYVY      (4u << 12)
#define SWEET_FB_YUV_CFMT_YUYV      (5u << 12)
#define SWEET_FB_YUV_MTX(c)         (((uint32_t)(c) & 0xfu) << 16)
#define SWEET_FB_YUV_MTX_JPEG       4u              /* full-range BT.601 */
#define SWEET_FB_YUV_MTX_BT601      6u
#define SWEET_FB_YUV_MTX_BT709      7u
#define SWEET_FB_YUV_CON_BYTE_SWAP  (1u << 24)
#define SWEET_FB_YUV_CON_MASK       0x010ff000u     /* the L_CON bits a YUV layer owns: CFMT, MTX, BYTE_SWAP */

#define SWEET_FB_YUV_MODE_ON        0x001u
#define SWEET_FB_YUV_MODE_MTX(m)    (((m) >> 4) & 0xfu)
#define SWEET_FB_YUV_MODE_SWAP      0x100u
#define SWEET_FB_YUV_MODE_XCHG      0x200u
#define SWEET_FB_YUV_MODE_VALID     0x3f1u

enum sweet_fb_yuv_kind {
	SWEET_FB_YUV_NONE = 0,      /* not a YCbCr format known here */
	SWEET_FB_YUV_UYVY,          /* packed 4:2:2, the OVL reads it */
	SWEET_FB_YUV_YUYV,          /* packed 4:2:2, the OVL reads it */
	SWEET_FB_YUV_BIPLANAR,      /* 4:2:0 two planes: no OVL input format */
	SWEET_FB_YUV_BAD            /* a packed 4:2:2 code with a layout that is not 2 bytes per pixel in one plane */
};

static inline bool
sweet_fb_yuv_mode_valid(uint32_t mode)
{
	return (mode & ~SWEET_FB_YUV_MODE_VALID) == 0u;
}

/* A surface's format and element layout -> kind.  Element layouts of packed 4:2:2: 2 bytes x 1 pixel or 4 bytes x 2 pixels. */
static inline enum sweet_fb_yuv_kind
sweet_fb_yuv_classify(uint32_t fcc, uint32_t planes, uint32_t bpe, uint32_t ew, uint32_t eh)
{
	if (fcc == SWEET_FB_YUV_FCC_420V || fcc == SWEET_FB_YUV_FCC_420F) { return SWEET_FB_YUV_BIPLANAR; }
	if (fcc != SWEET_FB_YUV_FCC_2VUY && fcc != SWEET_FB_YUV_FCC_YUVS && fcc != SWEET_FB_YUV_FCC_YUVF) { return SWEET_FB_YUV_NONE; }
	if (planes != 0u || eh != 1u || !((bpe == 2u && ew == 1u) || (bpe == 4u && ew == 2u))) { return SWEET_FB_YUV_BAD; }
	return fcc == SWEET_FB_YUV_FCC_2VUY ? SWEET_FB_YUV_UYVY : SWEET_FB_YUV_YUYV;
}

static inline bool
sweet_fb_yuv_full_range(uint32_t fcc)
{
	return fcc == SWEET_FB_YUV_FCC_YUVF || fcc == SWEET_FB_YUV_FCC_420F;
}

/* automatic matrix: the surface carries no colour tags here, so the usual size rule (HD = BT.709) */
static inline uint32_t
sweet_fb_yuv_matrix(uint32_t fcc, uint32_t width, uint32_t height)
{
	if (sweet_fb_yuv_full_range(fcc)) { return SWEET_FB_YUV_MTX_JPEG; }
	return (width >= 1280u || height > 576u) ? SWEET_FB_YUV_MTX_BT709 : SWEET_FB_YUV_MTX_BT601;
}

/*
 * The L_CON format bits (CFMT | MTX | BYTE_SWAP; never 0 for a planned layer) of a surface under a mode word, or 0 = not an
 * overlay YUV layer (off, another format, or a layout the OVL cannot read).  Alpha bits are the planner's.
 */
static inline uint32_t
sweet_fb_yuv_con(uint32_t mode, uint32_t fcc, uint32_t planes, uint32_t bpe, uint32_t ew, uint32_t eh, uint32_t width, uint32_t height)
{
	enum sweet_fb_yuv_kind k = sweet_fb_yuv_classify(fcc, planes, bpe, ew, eh);
	if (!(mode & SWEET_FB_YUV_MODE_ON) || !sweet_fb_yuv_mode_valid(mode) || (k != SWEET_FB_YUV_UYVY && k != SWEET_FB_YUV_YUYV)) { return 0u; }
	bool uyvy = (k == SWEET_FB_YUV_UYVY);
	if (mode & SWEET_FB_YUV_MODE_XCHG) { uyvy = !uyvy; }
	uint32_t mtx = SWEET_FB_YUV_MODE_MTX(mode) ? SWEET_FB_YUV_MODE_MTX(mode) : sweet_fb_yuv_matrix(fcc, width, height);
	return (uyvy ? SWEET_FB_YUV_CFMT_UYVY : SWEET_FB_YUV_CFMT_YUYV) | SWEET_FB_YUV_MTX(mtx) |
	       ((mode & SWEET_FB_YUV_MODE_SWAP) ? SWEET_FB_YUV_CON_BYTE_SWAP : 0u);
}

/*
 * 4:2:2 column rule: the columns [sx, sx + w) the layer shows become the columns [*rx, *rx + *rw) the OVL reads (even start,
 * even count, at most one more on each side); returns the Ln_CLIP value that trims the extra ones.  sx >= 0, w > 0.
 */
static inline uint32_t
sweet_fb_yuv_span(int32_t sx, int32_t w, int32_t *rx, int32_t *rw)
{
	uint32_t clip = 0u;
	if (sx & 1) { sx -= 1; w += 1; clip |= 1u; }
	if (w & 1) { w += 1; clip |= 1u << 8; }
	*rx = sx; *rw = w;
	return clip;
}

/* the L_CON word of a YUV layer: no per-pixel alpha, so opaque (AEN 0) unless a plane alpha 1..254 is asked for */
static inline uint32_t
sweet_fb_yuv_con_alpha(uint32_t ycon, uint32_t alpha)
{
	uint32_t a = (alpha == 0u || alpha >= 255u) ? 255u : alpha;
	return (ycon & SWEET_FB_YUV_CON_MASK) | (a == 255u ? 0xffu : ((1u << 8) | a));
}

/*
 * Reference conversion of one sample to 0x00RRGGBB under a matrix code (what a correct layer must show; the host test checks
 * the grey axis and the primaries, the phone test compares a pattern against it).  Unknown codes return 0xff000000.
 */
static inline uint32_t
sweet_fb_yuv_ref_rgb(uint32_t mtx, uint32_t y, uint32_t cb, uint32_t cr)
{
	int32_t c, d = (int32_t)cb - 128, e = (int32_t)cr - 128, r, g, b;
	switch (mtx) {
	case SWEET_FB_YUV_MTX_JPEG:
		c = (int32_t)y * 65536;
		r = c + 91881 * e; g = c - 22554 * d - 46802 * e; b = c + 116130 * d;
		break;
	case SWEET_FB_YUV_MTX_BT601:
		c = ((int32_t)y - 16) * 76309;
		r = c + 104597 * e; g = c - 25675 * d - 53279 * e; b = c + 132201 * d;
		break;
	case SWEET_FB_YUV_MTX_BT709:
		c = ((int32_t)y - 16) * 76309;
		r = c + 117489 * e; g = c - 13975 * d - 34925 * e; b = c + 138438 * d;
		break;
	default:
		return 0xff000000u;
	}
	r = (r + 32768) >> 16; g = (g + 32768) >> 16; b = (b + 32768) >> 16;
	r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

#endif /* SWEET_FB_YUV_H */
