/*
 * gfxall17 (rot): host test of rwa_texture_rotate on softpipe (no GPU, no phone).  What this shows: the NIR shader is valid, the three
 * mappings put every texel exactly where the comment over rwa_texture_rotate says (nearest, texel-exact, odd and non-square sizes),
 * 1 and 3 undo each other, 2 undoes itself, and the refusals (sizes, a destination that is not a render target) refuse.  What it cannot
 * show: Panfrost's compile/batch ordering, the IOSurface import, which DIRECTION Apple's rotation 3 means (that is the phone's test).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "air_metal.h"

int rwa_texture_rotate(struct rwa_texture *dst, struct rwa_texture *src, int mapping, char *why, size_t why_size);
int rwa_finish(struct rwa_device *device, char *why, size_t why_size);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED line %d: %s (%s)\n", __LINE__, #x, why); return 1; } } while (0)

enum { LW = 67, LH = 41 };      /* the logical (landscape) picture: LW x LH; the surface: LH x LW */
static unsigned char logical[LH][LW][4], back[LH][LW][4], surf[LW][LH][4], same[LH][LW][4];

static int
px_eq(const unsigned char *a, const unsigned char *b)
{
	return memcmp(a, b, 4) == 0;
}

int
main(void)
{
	char why[512] = "";
	const unsigned o[3] = { 0, 0, 0 }, lsize[3] = { LW, LH, 1 }, ssize[3] = { LH, LW, 1 };

	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *device = rwa_device_open(why, sizeof(why));
	CHECK(device != NULL);
	printf("device: %s\n", rwa_device_name(device));

	struct rwa_texture_shape ls = { .pixel_format = 80, .type = 2, .width = LW, .height = LH, .depth = 1, .array_length = 1,
					.mip_levels = 1, .sample_count = 1, .usage = 1 | 4 };
	struct rwa_texture_shape ss = ls;
	ss.width = LH;
	ss.height = LW;
	struct rwa_texture *lt = rwa_texture_create(device, &ls, why, sizeof(why));
	struct rwa_texture *lt2 = rwa_texture_create(device, &ls, why, sizeof(why));
	struct rwa_texture *st = rwa_texture_create(device, &ss, why, sizeof(why));
	CHECK(lt && lt2 && st);
	struct rwa_texture_shape nr = ss;
	nr.usage = 1;
	struct rwa_texture *not_target = rwa_texture_create(device, &nr, why, sizeof(why));
	CHECK(not_target);

	for (int y = 0; y < LH; y++)
		for (int x = 0; x < LW; x++) {
			unsigned char *p = logical[y][x];
			p[0] = (unsigned char)x; p[1] = (unsigned char)y; p[2] = (unsigned char)(x * 7 + y * 13); p[3] = 255;
		}
	CHECK(rwa_texture_write(lt, 0, 0, o, lsize, logical, LW * 4, why, sizeof(why)) == 0);

	for (int mapping = 1; mapping <= 3; mapping += 2) {
		/* out: logical -> surface */
		CHECK(rwa_texture_rotate(st, lt, mapping, why, sizeof(why)) == 0);
		CHECK(rwa_finish(device, why, sizeof(why)) == 0);
		CHECK(rwa_texture_read(st, 0, 0, o, ssize, surf, LH * 4, why, sizeof(why)) == 0);
		unsigned bad = 0;
		const unsigned W = LH, H = LW;     /* the surface's size */
		for (unsigned y = 0; y < H; y++)
			for (unsigned x = 0; x < W; x++) {
				unsigned lx = mapping == 3 ? y : H - 1 - y, ly = mapping == 3 ? W - 1 - x : x;
				if (!px_eq(surf[y][x], logical[ly][lx])) {
					if (bad++ < 4)
						fprintf(stderr, "mapping %d: surface (%u,%u) = %u,%u,%u wants logical (%u,%u)\n", mapping, x, y,
							surf[y][x][0], surf[y][x][1], surf[y][x][2], lx, ly);
				}
			}
		CHECK(bad == 0);
		/* in: surface -> logical with the inverse, must give the picture back */
		CHECK(rwa_texture_rotate(lt2, st, 4 - mapping, why, sizeof(why)) == 0);
		CHECK(rwa_finish(device, why, sizeof(why)) == 0);
		CHECK(rwa_texture_read(lt2, 0, 0, o, lsize, back, LW * 4, why, sizeof(why)) == 0);
		CHECK(memcmp(back, logical, sizeof(back)) == 0);
		printf("mapping %d: %ux%u -> %ux%u exact (%u texels), inverse %d gives the picture back\n", mapping, LW, LH, W, H, W * H,
		       4 - mapping);
	}
	/* half turn, same size */
	CHECK(rwa_texture_rotate(lt2, lt, 2, why, sizeof(why)) == 0);
	CHECK(rwa_finish(device, why, sizeof(why)) == 0);
	CHECK(rwa_texture_read(lt2, 0, 0, o, lsize, same, LW * 4, why, sizeof(why)) == 0);
	for (int y = 0; y < LH; y++)
		for (int x = 0; x < LW; x++)
			CHECK(px_eq(same[y][x], logical[LH - 1 - y][LW - 1 - x]));
	printf("mapping 2: half turn exact\n");

	/* refusals */
	CHECK(rwa_texture_rotate(lt2, lt, 3, why, sizeof(why)) != 0);           /* same size for a quarter turn */
	printf("refused as it should: %s\n", why);
	CHECK(rwa_texture_rotate(st, lt, 2, why, sizeof(why)) != 0);            /* other size for a half turn */
	printf("refused as it should: %s\n", why);
	CHECK(rwa_texture_rotate(not_target, lt, 3, why, sizeof(why)) != 0);    /* not a render target */
	printf("refused as it should: %s\n", why);
	CHECK(rwa_texture_rotate(st, lt, 0, why, sizeof(why)) != 0);
	printf("refused as it should: %s\n", why);

	/* the library still draws its other passes afterwards (the shared ycbcr state was not disturbed) */
	CHECK(rwa_texture_convert_ycbcr(lt2, lt, 0, 1, why, sizeof(why)) == 0);
	CHECK(rwa_finish(device, why, sizeof(why)) == 0);

	rwa_texture_destroy(not_target);
	rwa_texture_destroy(st);
	rwa_texture_destroy(lt2);
	rwa_texture_destroy(lt);
	rwa_device_close(device);
	printf("ROT_HOST_TEST_OK\n");
	return 0;
}
