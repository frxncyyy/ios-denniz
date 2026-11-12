/* The place of the wide camera's picture in the ultra-wide one's (src/rwcam_align.c): made-up scenes seen through two made-up lenses. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rwcam_align.h"

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

/* a scene: brightness at any place of the ultra-wide camera's field (0 to 1 each way), a sum of blobs and edges */
static struct { float x, y, r, v; } blob[40];
static float edge_x[6], edge_v[6];
static float flat;
static float
scene(float x, float y)
{
	float v = 90;

	for (int i = 0; i < 40; i++) {
		float dx = x - blob[i].x, dy = y - blob[i].y;

		v += blob[i].v * expf(-(dx * dx + dy * dy) / (blob[i].r * blob[i].r));
	}
	for (int i = 0; i < 6; i++) {
		v += x + 0.3f * y > edge_x[i] ? edge_v[i] : 0;
	}
	v = 90 + (v - 90) * (1 - flat);
	return v < 0 ? 0 : v > 255 ? 255 : v;
}
static unsigned seed = 12345;
static float rnd(void) { seed = seed * 1103515245u + 12345u; return (float)((seed >> 8) & 0xffff) / 65535.0f; }

static void
pictures(float ratio, float dx, float dy, float gain_wide, int noise, uint8_t *wide, uint8_t *ultra)
{
	for (int y = 0; y < RWCAM_ALIGN_H; y++) {
		for (int x = 0; x < RWCAM_ALIGN_W; x++) {
			float u = ((float)x + 0.5f) / RWCAM_ALIGN_W, v = ((float)y + 0.5f) / RWCAM_ALIGN_H;
			float a = scene(u, v) + (rnd() - 0.5f) * (float)noise;
			float b = scene(0.5f + dx + (u - 0.5f) / ratio, 0.5f + dy + (v - 0.5f) / ratio) * gain_wide + 10 + (rnd() - 0.5f) * (float)noise;

			ultra[y * RWCAM_ALIGN_W + x] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a);
			wide[y * RWCAM_ALIGN_W + x] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
		}
	}
}

int
main(void)
{
	static uint8_t wide[RWCAM_ALIGN_W * RWCAM_ALIGN_H], ultra[RWCAM_ALIGN_W * RWCAM_ALIGN_H];
	static const struct { float ratio, dx, dy; } lens[] = { { 1.6f, 0, 0 }, { 1.72f, 0.031f, -0.047f }, { 2.0f, -0.06f, 0.02f }, { 1.45f, 0.08f, 0.09f }, { 1.93f, -0.012f, -0.1f } };
	struct rwcam_alignment a, now;
	struct rwcam_align_filter filter;
	double worst_r = 0, worst_d = 0;
	clock_t t0;
	int found = 0, tried = 0;

	memset(&filter, 0, sizeof(filter));
	for (unsigned l = 0; l < sizeof(lens) / sizeof(lens[0]); l++) {
		for (int s = 0; s < 6; s++) {
			for (int i = 0; i < 40; i++) { blob[i].x = rnd(); blob[i].y = rnd(); blob[i].r = 0.01f + 0.06f * rnd(); blob[i].v = (rnd() - 0.4f) * 160; }
			for (int i = 0; i < 6; i++) { edge_x[i] = rnd() * 1.3f; edge_v[i] = (rnd() - 0.5f) * 60; }
			/* (the wide camera's picture brighter or darker, both with noise: the exposures are each camera's own) */
			pictures(lens[l].ratio, lens[l].dx, lens[l].dy, 0.7f + 0.6f * rnd(), 8, wide, ultra);
			rwcam_align(wide, ultra, &a);
			tried++;
			if (a.ok) {
				double er = fabs(a.ratio - lens[l].ratio), ed = fabs(a.dx - lens[l].dx) > fabs(a.dy - lens[l].dy) ? fabs(a.dx - lens[l].dx) : fabs(a.dy - lens[l].dy);

				found++;
				worst_r = er > worst_r ? er : worst_r;
				worst_d = ed > worst_d ? ed : worst_d;
			}
		}
	}
	printf("   %d of %d scenes placed; furthest off: ratio %.3f, place %.4f of the picture\n", found, tried, worst_r, worst_d);
	CHECK(found >= tried * 8 / 10);          /* (a scene that is not placed is no harm: the next one is) */
	CHECK(worst_r < 0.02 && worst_d < 0.006);
	/* a scene with nothing in it, and two pictures of different scenes: no place is given */
	flat = 1;
	pictures(1.6f, 0, 0, 1, 6, wide, ultra);
	rwcam_align(wide, ultra, &a);
	CHECK(!a.ok);
	flat = 0;
	pictures(1.6f, 0, 0, 1, 6, wide, ultra);
	for (int i = 0; i < 40; i++) { blob[i].x = rnd(); blob[i].y = rnd(); }
	{
		static uint8_t other[RWCAM_ALIGN_W * RWCAM_ALIGN_H];

		pictures(1.6f, 0, 0, 1, 6, wide, other);
		rwcam_align(wide, ultra, &a);
		CHECK(!a.ok);
	}
	/* the filter: one finding is not enough; three that agree give their middle; one that is off does not move it */
	a.ok = true; a.score = 0.9f; a.ratio = 1.70f; a.dx = 0.03f; a.dy = -0.04f;
	CHECK(!rwcam_align_add(&filter, &a, &now));
	a.ratio = 1.71f; a.dx = 0.031f;
	CHECK(rwcam_align_add(&filter, &a, &now));
	a.ratio = 1.72f;
	CHECK(rwcam_align_add(&filter, &a, &now) && fabsf(now.ratio - 1.71f) < 0.001f);
	a.ratio = 2.2f; a.dx = -0.1f;
	CHECK(rwcam_align_add(&filter, &a, &now) && fabsf(now.ratio - 1.715f) < 0.01f && fabsf(now.dx - 0.031f) < 0.002f);
	a.ok = false;
	CHECK(rwcam_align_add(&filter, &a, &now) && filter.n == 4);
	t0 = clock();
	pictures(1.7f, 0.02f, 0.01f, 1, 6, wide, ultra);
	for (int i = 0; i < 10; i++) { rwcam_align(wide, ultra, &a); }
	printf("   one search: %.1f ms on this host\n", (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC / 10);
	printf(failures ? "t_align: %d FAILURES\n" : "t_align: all passed\n", failures);
	return failures != 0;
}
