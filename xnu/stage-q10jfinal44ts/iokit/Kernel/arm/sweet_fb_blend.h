#ifndef SWEET_FB_BLEND_H
#define SWEET_FB_BLEND_H
/*
 * 0416: the CPU blend/compose code of 0390 is deleted (overlay-only composition); only the layer geometry (sweet_fb_layer_clip)
 * used by the overlay planner is left.  History of 0390 below.
 *
 * 0390 (fbblend-20261112): the SwapEnd layers are composited, not copied over each other.  Pure and host-testable.
 *
 * Why: CoreAnimation (backboardd) detaches a static, opaque, full-screen app drawable (MobileSlideShow's CAMetalLayer, run
 * thumbcomp5 bbdiag-60: from composite n=2001 on, backboardd no longer draws Photos' surface s112/s119 into its own frame, only
 * the 95x40 / 289x50 blur sources of the "Select" / "..." pills, and its own frame is 00000000 = transparent where the grid is)
 * and hands it to the display as a separate SwapEnd layer under the UI layer.  presentCopy memcpy'd every enabled layer in
 * index order, so the transparent UI layer replaced the grid with black: status bar, pills and tab bar stayed, the grid
 * (and the "Dec 31, 1969" title, drawn by Photos) vanished ~1.5 s after the launch, exactly as on screen.
 *
 * Layers are composited in index order (0 = bottom): the lowest layer that covers a row is copied, every higher layer is
 * blended over it with premultiplied source-over (CoreAnimation's surfaces are premultiplied BGRA), in a cached row buffer,
 * then the row is written once to the (write-combined) panel memory.  A single layer is still a plain copy.
 */
#include <stdbool.h>
#include <stdint.h>


struct sweet_fb_layer_geom {
	int32_t sx, sy, dx, dy, w, h;
};

/*
 * The clipping presentCopy always did (no scaling): rect a gives the source origin, rect b the logical destination and size
 * (the surface size when empty), the logical display is shifted by (offx, offy) onto the fw x fh panel.  Returns false when
 * nothing is left to show.
 */
static inline bool
sweet_fb_layer_clip(struct sweet_fb_layer_geom *g, int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h,
    uint32_t sw, uint32_t sh, int32_t offx, int32_t offy, uint32_t fw, uint32_t fh)
{
	if (w <= 0 || h <= 0) { w = (int32_t)sw; h = (int32_t)sh; }
	if (sx < 0 || sy < 0) { sx = sy = 0; }
	if (dx < 0 || dy < 0) { dx = dy = 0; }
	dx -= offx;
	dy -= offy;
	if (dx < 0) { sx -= dx; w += dx; dx = 0; }
	if (dy < 0) { sy -= dy; h += dy; dy = 0; }
	if (w > (int32_t)sw - sx) { w = (int32_t)sw - sx; }
	if (h > (int32_t)sh - sy) { h = (int32_t)sh - sy; }
	if (w > (int32_t)fw - dx) { w = (int32_t)fw - dx; }
	if (h > (int32_t)fh - dy) { h = (int32_t)fh - dy; }
	g->sx = sx; g->sy = sy; g->dx = dx; g->dy = dy; g->w = w; g->h = h;
	return w > 0 && h > 0;
}

#endif /* SWEET_FB_BLEND_H */
