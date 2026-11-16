/*
 * y2s46 host test (softpipe: no GPU, no phone): texture_choice.ll through rwa_compute_create / rwa_dispatch.  What it shows: a texture
 * taken out of a local array by a run-time place, and one chosen by a phi of pointers, read the texture they should (the translator's
 * ptr_slots and its choices).  What it cannot show: Panfrost's code for it (compiled for the Mali by metallib_compile_host_test with
 * the still image processor's own four fusion shaders, executed only on the phone).
 *   texture_choice_host_test choice.bc
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "air_metal.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED line %d: %s (%s)\n", __LINE__, #x, why); return 1; } } while (0)

int
main(int argc, char **argv)
{
	enum { W = 16, H = 8 };
	static unsigned char code[1 << 16], pa[H][W][4], pb[H][W][4], po[H][W][4];
	char why[512] = "";
	const unsigned origin[3] = { 0, 0, 0 }, size[3] = { W, H, 1 }, groups[3] = { W, H, 1 }, each[3] = { 1, 1, 1 };
	FILE *f = argc == 2 ? fopen(argv[1], "rb") : NULL;
	size_t n = f ? fread(code, 1, sizeof(code), f) : 0;
	int bad = 0;

	if (f)
		fclose(f);
	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *device = rwa_device_open(why, sizeof(why));
	CHECK(device != NULL && n > 4);
	struct rwa_compute_info info;
	struct rwa_compute *k = rwa_compute_create(device, code, n, "nord2_texture_choice", &info, why, sizeof(why));
	CHECK(k != NULL);
	struct rwa_texture_shape s = { .pixel_format = 70 /* RGBA8Unorm */, .type = 2, .width = W, .height = H, .depth = 1, .array_length = 1,
				       .mip_levels = 1, .sample_count = 1, .usage = 1 | 2 };
	struct rwa_texture *a = rwa_texture_create(device, &s, why, sizeof(why)), *b = rwa_texture_create(device, &s, why, sizeof(why));
	struct rwa_texture *o = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(a != NULL && b != NULL && o != NULL);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			pa[y][x][0] = (unsigned char)(x * 7 + y * 13 + 1);
			pb[y][x][0] = (unsigned char)(255 - x * 5 - y * 11);
			pa[y][x][3] = pb[y][x][3] = 255;
		}
	}
	CHECK(rwa_texture_write(a, 0, 0, origin, size, pa, W * 4, why, sizeof(why)) == 0);
	CHECK(rwa_texture_write(b, 0, 0, origin, size, pb, W * 4, why, sizeof(why)) == 0);
	struct rwa_texture *const t[3] = { a, b, o };
	const struct rwa_bindings bind = { .textures = t, .texture_count = 3 };
	CHECK(rwa_dispatch(k, &bind, groups, each, why, sizeof(why)) == 0);
	CHECK(rwa_finish(device, why, sizeof(why)) == 0);
	CHECK(rwa_texture_read(o, 0, 0, origin, size, po, W * 4, why, sizeof(why)) == 0);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			unsigned char want_r = x & 1 ? pb[y][x][0] : pa[y][x][0], want_g = y & 2 ? pb[y][x][0] : pa[y][x][0];

			bad += po[y][x][0] != want_r || po[y][x][1] != want_g || po[y][x][3] != 255;
		}
	}
	printf("texture_choice_host_test: a texture out of a local by a run-time place and by a phi of pointers, on %s: %d of %d texels wrong\n",
	       rwa_device_name(device), bad, W * H);
	return bad != 0;
}
