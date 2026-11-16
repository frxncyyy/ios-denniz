/*
 * y2s45 host test (softpipe: no GPU, no phone): a kernel that writes two textures, then one that writes one, then the first again, through
 * rwa_compute_create / rwa_dispatch, with the textures read back.  What it shows: the library's binding and unbinding of image slots in the
 * order that killed mediaserverd in run q10ia works and gives the right pictures.  What it cannot show: Panfrost's own set_shader_images
 * (the fault was a bit of ITS mask left set over a NULL resource; softpipe keeps no such mask), which only the phone runs.
 *   image_slots_host_test two.bc one.bc
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "air_metal.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED line %d: %s (%s)\n", __LINE__, #x, why); return 1; } } while (0)

static size_t
slurp(const char *path, unsigned char *to, size_t room)
{
	FILE *f = fopen(path, "rb");
	size_t n = f ? fread(to, 1, room, f) : 0;

	if (f)
		fclose(f);
	return n;
}

int
main(int argc, char **argv)
{
	enum { W = 16, H = 8 };
	static unsigned char two[1 << 16], one[1 << 16], px[H][W][4];
	char why[512] = "";
	const unsigned origin[3] = { 0, 0, 0 }, size[3] = { W, H, 1 }, groups[3] = { W, H, 1 }, each[3] = { 1, 1, 1 };

	if (argc != 3)
		return 2;
	size_t n2 = slurp(argv[1], two, sizeof(two)), n1 = slurp(argv[2], one, sizeof(one));
	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *device = rwa_device_open(why, sizeof(why));
	CHECK(device != NULL && n2 > 4 && n1 > 4);
	struct rwa_compute_info info;
	struct rwa_compute *k2 = rwa_compute_create(device, two, n2, "nord2_write_two", &info, why, sizeof(why));
	CHECK(k2 != NULL);
	struct rwa_compute *k1 = rwa_compute_create(device, one, n1, "nord2_write_one", &info, why, sizeof(why));
	CHECK(k1 != NULL);
	struct rwa_texture_shape s = { .pixel_format = 70 /* RGBA8Unorm */, .type = 2, .width = W, .height = H, .depth = 1, .array_length = 1,
				       .mip_levels = 1, .sample_count = 1, .usage = 1 | 2 };
	struct rwa_texture *a = rwa_texture_create(device, &s, why, sizeof(why)), *b = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(a != NULL && b != NULL);
	struct rwa_texture *const both[2] = { a, b }, *const first[1] = { a };
	const struct rwa_bindings bind2 = { .textures = both, .texture_count = 2 }, bind1 = { .textures = first, .texture_count = 1 };
	static const unsigned char want_a2[4] = { 64, 128, 191, 255 }, want_b[4] = { 255, 0, 255, 0 }, want_a1[4] = { 255, 255, 0, 255 };
	int bad = 0;

	for (int round = 0; round < 3; round++) {
		const struct rwa_bindings *bind = round == 1 ? &bind1 : &bind2;

		CHECK(rwa_dispatch(round == 1 ? k1 : k2, bind, groups, each, why, sizeof(why)) == 0);
		CHECK(rwa_finish(device, why, sizeof(why)) == 0);
		CHECK(rwa_texture_read(a, 0, 0, origin, size, px, W * 4, why, sizeof(why)) == 0);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				for (int c = 0; c < 4; c++)
					bad += abs((int)px[y][x][c] - (int)(round == 1 ? want_a1 : want_a2)[c]) > 1;
		CHECK(rwa_texture_read(b, 0, 0, origin, size, px, W * 4, why, sizeof(why)) == 0);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				for (int c = 0; c < 4; c++)
					bad += px[y][x][c] != want_b[c];
	}
	printf("image_slots_host_test: two images, one, two again on %s: %d wrong bytes\n", rwa_device_name(device), bad);
	return bad != 0;
}
