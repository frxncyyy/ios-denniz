/*
 * webxr1 host test (softpipe: no GPU, no phone). The extended-range formats 550-555 (what WebKit's GPU process renders its page tiles in on
 * a wide-colour device) are now made by the library as 8-bit BGRA (550-553) / BGRX (554/555): a render target of each is created, cleared
 * to a colour and read back, and the bytes must be that colour in B, G, R order (alpha = the cleared alpha for BGRA; for BGRX the alpha byte
 * is not checked, sampling gives 1). Then 80 (BGRA8Unorm) for control, and 556 must still be refused.
 *   webxr_host_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "air_metal.h"

int
main(void)
{
	enum { W = 8, H = 4 };
	static unsigned char px[H][W][4];
	char why[512] = "";
	const unsigned origin[3] = { 0, 0, 0 }, size[3] = { W, H, 1 };
	const unsigned formats[] = { 80, 550, 551, 552, 553, 554, 555 };
	const float clear[4] = { 1.0f, 0.5f, 0.25f, 0.75f };   /* R G B A */
	int bad = 0;

	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *device = rwa_device_open(why, sizeof(why));
	if (device == NULL) {
		fprintf(stderr, "no device: %s\n", why);
		return 1;
	}
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		unsigned f = formats[i];
		int srgb = f == 551 || f == 553 || f == 555;
		int noalpha = f == 554 || f == 555;
		struct rwa_texture_shape s = { .pixel_format = f, .type = 2, .width = W, .height = H, .depth = 1, .array_length = 1,
					       .mip_levels = 1, .sample_count = 1, .usage = 1 | 4 };
		struct rwa_texture *t = rwa_texture_create(device, &s, why, sizeof(why));

		if (t == NULL) {
			printf("format %u: REFUSED: %s\n", f, why);
			bad++;
			continue;
		}
		struct rwa_texture *const col[1] = { t };
		struct rwa_draw d;

		memset(&d, 0, sizeof(d));
		d.color = col;
		d.color_count = 1;
		d.clear_color = clear;
		if (rwa_render_clear(device, &d, why, sizeof(why)) != 0 || rwa_finish(device, why, sizeof(why)) != 0 ||
		    rwa_texture_read(t, 0, 0, origin, size, px, W * 4, why, sizeof(why)) != 0) {
			printf("format %u: clear/read failed: %s\n", f, why);
			bad++;
			continue;
		}
		/* expected bytes: B G R (A); sRGB encodes 0.5 -> 188, 0.25 -> 137, 1 -> 255 */
		int want[4] = { srgb ? 137 : 64, srgb ? 188 : 128, 255, 191 };
		int wrong = 0;

		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				for (int c = 0; c < (noalpha ? 3 : 4); c++)
					wrong += abs((int)px[y][x][c] - want[c]) > 1;
		printf("format %u: made, cleared, read: %02x %02x %02x %02x (want %02x %02x %02x %s) %s\n", f, px[0][0][0], px[0][0][1],
		    px[0][0][2], px[0][0][3], want[0], want[1], want[2], noalpha ? "x" : "bf", wrong ? "WRONG" : "ok");
		bad += wrong != 0;
		rwa_texture_destroy(t);
	}
	struct rwa_texture_shape s = { .pixel_format = 556, .type = 2, .width = W, .height = H, .depth = 1, .array_length = 1,
				       .mip_levels = 1, .sample_count = 1, .usage = 1 };
	struct rwa_texture *t = rwa_texture_create(device, &s, why, sizeof(why));
	printf("format 556: %s\n", t == NULL ? "refused (expected)" : "MADE (unexpected)");
	bad += t != NULL;
	printf("webxr_host_test on %s: %s\n", rwa_device_name(device), bad ? "FAIL" : "ALL OK");
	return bad != 0;
}
