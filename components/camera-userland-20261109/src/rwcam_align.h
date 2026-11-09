/*
 * Where the wide camera's picture lies in the ultra-wide camera's: found from a small picture of each, taken at the same time (the two back
 * cameras look at the same scene, the wide one at its middle part). The wide picture is taken to be the part of the ultra-wide one whose middle
 * is (0.5 + dx, 0.5 + dy) and whose size is 1/ratio of it (ratio: the lenses' focal ratio; dx, dy: how far the two lenses' axes are apart in
 * the picture, by where they are mounted and, for near things, by parallax). All candidates on a grid are tried (ratio 1.2 to 2.3, dx and dy up
 * to +-0.15) by the correlation of the two pictures' brightness at 32 x 24 places, then the best one's neighbourhood more finely.
 * No Apple types: built unchanged into the bundle and the host test (test/t_align.c).
 */
#ifndef RWCAM_ALIGN_INCLUDED
#define RWCAM_ALIGN_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#define RWCAM_ALIGN_W   160
#define RWCAM_ALIGN_H   120

struct rwcam_alignment {
	float ratio, dx, dy;
	float score;            /* the correlation at the best place (1: the same picture) */
	float second;           /* the best correlation at a clearly different place: a scene without features has it near score */
	bool ok;                /* score and its distance from second say the place is the one */
};

/* wide, ultra: RWCAM_ALIGN_W x RWCAM_ALIGN_H bytes of brightness each, rows of RWCAM_ALIGN_W */
void rwcam_align(const uint8_t *wide, const uint8_t *ultra, struct rwcam_alignment *a);

/* several findings into one: the middle one of each of the last up to RWCAM_ALIGN_KEPT good ones */
#define RWCAM_ALIGN_KEPT 7
struct rwcam_align_filter {
	struct rwcam_alignment kept[RWCAM_ALIGN_KEPT];
	unsigned n, next;
};
/* true when *now is filled in (at least two good findings that agree) */
bool rwcam_align_add(struct rwcam_align_filter *f, const struct rwcam_alignment *found, struct rwcam_alignment *now);

#endif
