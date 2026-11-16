#ifndef SWEET_FB_SCENE_H
#define SWEET_FB_SCENE_H
/*
 * 0392 (fbretain-20261112): a SwapEnd record updates only the layers in its mask (+0x104); the others keep what the previous
 * swaps put there.  Pure and host-testable.
 *
 * Why (run fbd1, q10jfd): backboardd detached Photos' drawable to layer 0 with two full swaps (mask 0x7, ids 52/61 + its own
 * frame 2/3 on layer 1, layer 2 = 0 = off), then sent its UI-only updates as mask 0x2 ids 0,3,0,0: layer 1 changed, layer 0 NOT
 * in the mask = unchanged.  The framebuffer read every record as the whole scene, so those swaps showed backboardd's transparent
 * frame alone: black grid from the first UI-only swap on (the kernel's copy of the full swaps was verified good: layer 0 pixels
 * ff34201b... reached the panel).  A selection animation makes backboardd composite Photos into its own frame again (one layer):
 * the photo shows until the next detach + partial swap.
 *
 * The scene keeps per layer the surface id and the two rects (+0x54 id, +0x64 rect a, +0xc4 rect b); a layer in the mask takes
 * the record's values (id 0 = layer off), a layer outside it keeps its own.  The record handed on is rewritten to the whole scene
 * and its mask to the layers that have a surface.
 */
#include <stdbool.h>
#include <stdint.h>

#define SWEET_FB_SCENE_LAYERS 4u

struct sweet_fb_scene {
	uint32_t id[SWEET_FB_SCENE_LAYERS];
	uint8_t  a[SWEET_FB_SCENE_LAYERS][16], b[SWEET_FB_SCENE_LAYERS][16];
};

static inline uint32_t sweet_fb_scene_rd(const uint8_t *p) { uint32_t v; __builtin_memcpy(&v, p, 4); return v; }
static inline void sweet_fb_scene_wr(uint8_t *p, uint32_t v) { __builtin_memcpy(p, &v, 4); }

/* rec: a full SwapEnd record (>= 0x108 bytes), rewritten in place.  Returns the effective mask. */
static inline uint32_t
sweet_fb_scene_apply(struct sweet_fb_scene *sc, uint8_t *rec)
{
	uint32_t mask = sweet_fb_scene_rd(rec + 0x104), eff = 0;
	for (uint32_t l = 0; l < SWEET_FB_SCENE_LAYERS; l++) {
		if (mask & (1u << l)) {
			sc->id[l] = sweet_fb_scene_rd(rec + 0x54 + 4 * l);
			__builtin_memcpy(sc->a[l], rec + 0x64 + 16 * l, 16);
			__builtin_memcpy(sc->b[l], rec + 0xc4 + 16 * l, 16);
		}
	}
	for (uint32_t l = 0; l < SWEET_FB_SCENE_LAYERS; l++) {
		sweet_fb_scene_wr(rec + 0x54 + 4 * l, sc->id[l]);
		__builtin_memcpy(rec + 0x64 + 16 * l, sc->a[l], 16);
		__builtin_memcpy(rec + 0xc4 + 16 * l, sc->b[l], 16);
		if (sc->id[l] != 0) { eff |= 1u << l; }
	}
	sweet_fb_scene_wr(rec + 0x104, eff);
	return eff;
}

#endif /* SWEET_FB_SCENE_H */
