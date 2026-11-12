/*
 * Host test of the camera's ISP path (src/camera_isp.c): synthetic MIPI RAW10 frames (RGGB, GRBG, GBRG, BGGR; black levels 64, 42, 16, 0)
 * through isp_measure / isp_awb / isp_ae and isp_render_nv12 / isp_render_bgra / isp_render_full_nv12. The scene is four patches
 * (red, green, blue, grey); what is checked is what the iOS-side picture needs: colours come out where they belong whatever the Bayer
 * order, the output does not depend on the black level (it is the frame header's, not a constant), black and white clip, grey stays grey,
 * NV12 and BGRA agree, and AE moves the exposure the right way. Build: see run_tests.sh.
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "camera_isp.h"

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;

/* the colour (0 R, 1 G, 2 B) of the site (x & 1, y & 1) for each bayer order of redwood_camera.h */
static int colour_at(int bayer, int x, int y)
{
	static const int pat[4][4] = { { 0, 1, 1, 2 }, { 1, 0, 2, 1 }, { 1, 2, 0, 1 }, { 2, 1, 1, 0 } };   /* RGGB GRBG GBRG BGGR */

	return pat[bayer][(y & 1) * 2 + (x & 1)];
}

static void put10(uint8_t *line, int x, int v)
{
	uint8_t *g = line + (x >> 2) * 5;

	g[x & 3] = (uint8_t)(v >> 2);
	g[4] = (uint8_t)((g[4] & ~(3 << ((x & 3) * 2))) | ((v & 3) << ((x & 3) * 2)));
}

/* patches: 0 TL red, 1 TR green, 2 BL blue, 3 BR grey; values above black */
static const int scene[4][3] = { { 700, 60, 60 }, { 60, 700, 60 }, { 60, 60, 700 }, { 400, 400, 400 } };

static struct redwood_camera_frame *make_frame(int w, int h, int bayer, int black, uint8_t **raw_out)
{
	size_t stride = (size_t)w * 5 / 4, bytes = sizeof(struct redwood_camera_frame) + stride * h;
	struct redwood_camera_frame *f = calloc(1, bytes);
	uint8_t *raw = (uint8_t *)f + sizeof(*f);

	memcpy(f->magic, REDWOOD_CAMERA_MAGIC, 8);
	f->header_bytes = sizeof(*f);
	f->width = w; f->height = h; f->stride = (uint32_t)stride; f->format = 1; f->bayer = bayer; f->black = black;
	f->sequence = 1; f->exposure_lines = 1000; f->line_ns = 10000; f->gain = 256; f->gain_unity = 256; f->gain_max = 4096;
	f->frame_lines = 3000; f->frame_lines_min = 3000; f->frame_lines_max = 6000; f->margin = 8; f->sensor = 1;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int p = (y >= h / 2 ? 2 : 0) + (x >= w / 2 ? 1 : 0), c = colour_at(bayer, x, y);
			int v = scene[p][c] * (1023 - black) / 959 + black;      /* the same share of white whatever the black level (white = 1023 - black) */

			put10(raw + (size_t)y * stride, x, v > 1023 ? 1023 : v);
		}
	}
	*raw_out = raw;
	return f;
}

static void at_bgra(const uint8_t *img, size_t stride, int x, int y, int *r, int *g, int *b)
{
	const uint8_t *p = img + (size_t)y * stride + 4 * x;

	*b = p[0]; *g = p[1]; *r = p[2];
	CHECK(p[3] == 255);
}

/* isp_shrink_420: a ramp stays the ramp at every size, a picture of its own size is copied, the ranges go one into the other */
static void
shrink(void)
{
	enum { FW = 96, FH = 54 };
	static const int sizes[5][2] = { { 96, 54 }, { 64, 36 }, { 32, 18 }, { 30, 16 }, { 160, 90 } };       /* (the last: made larger) */
	static uint8_t fy[FW * FH], fc[FW * FH / 2], ty[4 * FW * FH], tc[2 * FW * FH];
	const uint8_t *from[2] = { fy, fc };
	uint8_t *to[2] = { ty, tc };
	size_t fs[2] = { FW, FW };

	for (int y = 0; y < FH; y++) {
		for (int x = 0; x < FW; x++) {
			fy[y * FW + x] = (uint8_t)(16 + x + y);
		}
	}
	for (int y = 0; y < FH / 2; y++) {
		for (int x = 0; x < FW / 2; x++) {
			fc[y * FW + 2 * x] = (uint8_t)(60 + 2 * x);
			fc[y * FW + 2 * x + 1] = (uint8_t)(200 - 2 * y);
		}
	}
	for (int k = 0; k < 5; k++) {
		int w = sizes[k][0], h = sizes[k][1];
		size_t ts[2] = { (size_t)w, (size_t)w };

		memset(ty, 0, sizeof(ty));
		memset(tc, 0, sizeof(tc));
		/* in two pieces, as the device does */
		isp_shrink_420(from, fs, FW, FH, to, ts, (uint32_t)w, (uint32_t)h, 0, (uint32_t)(h / 4 * 2), 0);
		isp_shrink_420(from, fs, FW, FH, to, ts, (uint32_t)w, (uint32_t)h, (uint32_t)(h / 4 * 2), (uint32_t)h, 0);
		for (int y = 1; y < h - 1; y++) {
			for (int x = 1; x < w - 1; x++) {
				double want = 16 + ((x + 0.5) * FW / w - 0.5) + ((y + 0.5) * FH / h - 0.5);
				double off = ty[y * w + x] - want;

				if (off > 1.6 || off < -1.6) {
					printf("FAIL shrink %dx%d: luma at %d,%d is %d, the ramp says %.1f\n", w, h, x, y, ty[y * w + x], want);
					exit(1);
				}
			}
		}
		for (int y = 1; y < h / 2 - 1; y++) {
			for (int x = 1; x < w / 2 - 1; x++) {
				double cb = 60 + 2 * ((x + 0.5) * (FW / 2) / (w / 2) - 0.5), cr = 200 - 2 * ((y + 0.5) * (FH / 2) / (h / 2) - 0.5);
				double o1 = tc[y * w + 2 * x] - cb, o2 = tc[y * w + 2 * x + 1] - cr;

				if (o1 > 1.6 || o1 < -1.6 || o2 > 1.6 || o2 < -1.6) {
					printf("FAIL shrink %dx%d: chroma at %d,%d is %d %d, the ramps say %.1f %.1f\n", w, h, x, y, tc[y * w + 2 * x],
					    tc[y * w + 2 * x + 1], cb, cr);
					exit(1);
				}
			}
		}
		if (k == 0 && (memcmp(ty, fy, sizeof(fy)) != 0 || memcmp(tc, fc, sizeof(fc)) != 0)) {
			printf("FAIL shrink: a picture of its own size is not the picture\n");
			exit(1);
		}
	}
	memset(fy, 16, sizeof(fy));
	memset(fy + FW * FH / 2, 235, FW * FH / 2);
	memset(fc, 128, sizeof(fc));
	{
		size_t ts[2] = { 48, 48 };

		isp_shrink_420(from, fs, FW, FH, to, ts, 48, 26, 0, 26, 1);
		if (ty[0] != 0 || ty[25 * 48 + 47] != 255 || tc[0] != 128) {
			printf("FAIL shrink: video range to full: %d %d %d\n", ty[0], ty[25 * 48 + 47], tc[0]);
			exit(1);
		}
		memset(fy, 0, FW * FH / 2);
		memset(fy + FW * FH / 2, 255, FW * FH / 2);
		isp_shrink_420(from, fs, FW, FH, to, ts, 48, 26, 0, 26, 2);
		if (ty[0] != 16 || ty[25 * 48 + 47] != 235 || tc[0] != 128) {
			printf("FAIL shrink: full range to video: %d %d %d\n", ty[0], ty[25 * 48 + 47], tc[0]);
			exit(1);
		}
	}
	printf("ok shrink: a ramp at four sizes, the copy, the two ranges\n");
}

#ifdef ISP_LINES
/* isp_render_nv12 a line at a time against pixel by pixel: the same bytes, whatever the zoom, the flip, the place, the light and the range */
static void
lines(void)
{
	enum { W = 320, H = 240, OW = 150, OH = 86 };
	static uint8_t y[2][OW * OH], uv[2][OW * OH];
	int tried = 0;

	for (int bayer = 0; bayer < 4; bayer++) {
		for (int k = 0; k < 12; k++) {
			uint8_t *raw;
			struct redwood_camera_frame *f = make_frame(W, H, bayer, k % 3 == 0 ? 64 : 16, &raw);
			struct isp isp;
			int ow = k % 4 == 3 ? OW - 1 : OW, oh = k % 5 == 4 ? OH - 1 : OH;

			/* (the scene's patches and, every other time, noise up to the sensor's white over them: the highlights' way too) */
			for (size_t i = 0; k % 2 && i < (size_t)f->stride * H; i++) {
				raw[i] = (uint8_t)(raw[i] + (i * 2654435761u >> 24) % (k % 4 == 1 ? 200 : 40));
			}
			isp_init(&isp);
			isp.wb[0] = 1.9f; isp.wb[2] = 1.6f;
			isp.digital = k % 3 == 2 ? 2.5f : 1;
			isp.falloff = k % 2 ? 0.3f : 0;
			isp.zoom = k < 3 ? 1 : 1 + 0.37f * (float)k;
			isp.flip = (unsigned)(k % 4);
			isp.shift_x = k > 6 ? 0.07f : 0;
			isp.shift_y = k > 8 ? -0.05f : 0;
			for (int way = 0; way < 2; way++) {
				isp_reference = way;
				memset(y[way], 0, sizeof(y[way]));
				memset(uv[way], 0, sizeof(uv[way]));
				/* (in two pieces, the second from an odd row: as the device's pieces) */
				isp_render_nv12(&isp, f, raw, y[way], OW, uv[way], OW, (uint32_t)ow, (uint32_t)oh, 0, 31, k % 2);
				isp_render_nv12(&isp, f, raw, y[way], OW, uv[way], OW, (uint32_t)ow, (uint32_t)oh, 32, (uint32_t)oh, k % 2);
			}
			isp_reference = 0;
			if (memcmp(y[0], y[1], sizeof(y[0])) != 0 || memcmp(uv[0], uv[1], sizeof(uv[0])) != 0) {
				printf("FAIL lines: bayer %d, case %d: a line at a time is not pixel by pixel\n", bayer, k);
				exit(1);
			}
			tried++;
			free(f);
		}
	}
	printf("ok lines: %d pictures a line at a time, byte for byte the pixel by pixel ones\n", tried);
}

#endif

int main(void)
{
	shrink();
#ifdef ISP_LINES
	lines();
#endif
	static const int blacks[4] = { 64, 42, 16, 0 };
	enum { W = 64, H = 48, OW = 32, OH = 24 };
	uint8_t first_bgra[OW * OH * 4];
	bool have_first = false;

	for (int bayer = 0; bayer < 4; bayer++) {
		for (int bi = 0; bi < 4; bi++) {
			uint8_t *raw;
			struct redwood_camera_frame *f = make_frame(W, H, bayer, blacks[bi], &raw);
			struct isp isp;
			struct isp_stats st;
			uint8_t bgra[OW * OH * 4];

			isp_init(&isp);
			isp_measure(f, raw, &st);
			/* means over the cells: R = (700+60+60+400)/4 = 305, G = (60+700+60+400)/4 = 305 ... -> all equal; and above black (black taken off) */
			{
				double k = (1023 - blacks[bi]) / 959.0, lo = 280 * k, hi = 330 * k;

				CHECK(st.mean[0] > lo && st.mean[0] < hi);
				CHECK(st.mean[1] > lo && st.mean[1] < hi);
				CHECK(st.mean[2] > lo && st.mean[2] < hi);
			}
			isp_render_bgra(&isp, f, raw, bgra, OW * 4, OW, OH, 0, OH);
			int r, g, b;

			at_bgra(bgra, OW * 4, OW / 4, OH / 4, &r, &g, &b);          /* red patch */
			CHECK(r > g + 80 && r > b + 80);
			at_bgra(bgra, OW * 4, 3 * OW / 4, OH / 4, &r, &g, &b);      /* green */
			CHECK(g > r + 80 && g > b + 80);
			at_bgra(bgra, OW * 4, OW / 4, 3 * OH / 4, &r, &g, &b);      /* blue */
			CHECK(b > r + 80 && b > g + 80);
			at_bgra(bgra, OW * 4, 3 * OW / 4, 3 * OH / 4, &r, &g, &b);  /* grey */
			CHECK(abs(r - g) <= 3 && abs(g - b) <= 3 && g > 90 && g < 230);
			if (!have_first) {
				memcpy(first_bgra, bgra, sizeof(bgra));
				have_first = true;
			} else {
				/* the same scene (as a share of white) must give the same picture whatever the Bayer order and the black level */
				int worst = 0;

				for (size_t i = 0; i < sizeof(bgra); i++) {
					int d = abs((int)bgra[i] - (int)first_bgra[i]);

					worst = d > worst ? d : worst;
				}
				if (worst > 2) {
					printf("bayer %d black %d: differs from bayer 0 black 64 by %d\n", bayer, blacks[bi], worst);
				}
				CHECK(worst <= 2);
			}
			/* NV12 against BGRA: Y of the grey patch equals the BGRA grey's luma; chroma of the grey patch is 128 */
			uint8_t y[OW * OH], uv[OW * OH / 2];

			isp_render_nv12(&isp, f, raw, y, OW, uv, OW, OW, OH, 0, OH, false);
			at_bgra(bgra, OW * 4, 3 * OW / 4, 3 * OH / 4, &r, &g, &b);
			CHECK(abs(y[(3 * OH / 4) * OW + 3 * OW / 4] - g) <= 2);
			CHECK(abs(uv[(3 * OH / 4 / 2) * OW + (3 * OW / 4 & ~1)] - 128) <= 2);
			CHECK(abs(uv[(3 * OH / 4 / 2) * OW + (3 * OW / 4 & ~1) + 1] - 128) <= 2);
			/* the red patch: Cr high, Cb low */
			CHECK(uv[(OH / 4 / 2) * OW + (OW / 4 & ~1) + 1] > 190);
			CHECK(uv[(OH / 4 / 2) * OW + (OW / 4 & ~1)] < 128);
			/* video range: luma of black stays >= 16 */
			isp_render_nv12(&isp, f, raw, y, OW, uv, OW, OW, OH, 0, OH, true);
			for (int i = 0; i < OW * OH; i++) {
				CHECK(y[i] >= 16 && y[i] <= 235);
			}
			/* every pixel (bilinear): sensor size, same colours in the patch centres */
			{
				uint8_t *fy = malloc(W * H), *fuv = malloc(W * H / 2);

				isp_render_full_nv12(&isp, f, raw, fy, W, fuv, W, 0, H, false);
				CHECK(fuv[(H / 4 / 2) * W + (W / 4 & ~1) + 1] > 190);          /* red: Cr */
				CHECK(fuv[(3 * H / 4 / 2) * W + (3 * W / 4 & ~1) + 1] > 100 && fuv[(3 * H / 4 / 2) * W + (3 * W / 4 & ~1) + 1] < 156);   /* grey */
				free(fy); free(fuv);
			}
			free(f);
		}
	}

	/* black clips to 0 and a saturated frame to 255, at any black level */
	for (int bi = 0; bi < 4; bi++) {
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, blacks[bi], &raw);
		uint8_t bgra[OW * OH * 4];
		struct isp isp;
		size_t stride = f->stride;

		isp_init(&isp);
		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, blacks[bi]);          /* exactly black */
			}
		}
		isp_render_bgra(&isp, f, raw, bgra, OW * 4, OW, OH, 0, OH);
		for (size_t i = 0; i < sizeof(bgra); i += 4) {
			CHECK(bgra[i] <= 1 && bgra[i + 1] <= 1 && bgra[i + 2] <= 1);
		}
		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, 1023);
			}
		}
		isp_render_bgra(&isp, f, raw, bgra, OW * 4, OW, OH, 0, OH);
		for (size_t i = 0; i < sizeof(bgra); i += 4) {
			CHECK(bgra[i] >= 254 && bgra[i + 1] >= 254 && bgra[i + 2] >= 254);
		}
		free(f);
	}

	/* the window off the middle (the ultra-wide camera's picture laid on the wide one's): none is the picture as ever; some moves it, inside the sensor */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
		struct isp isp;
		static uint8_t y0[64 * 48], y1[64 * 48], y2[64 * 48], uv[64 * 24];
		size_t stride = f->stride;
		unsigned long left0 = 0, left1 = 0;

		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, 64 + (x * 800 / W));        /* (darker on the left, brighter on the right) */
			}
		}
		isp_init(&isp);
		isp.zoom = 2;
		isp_render_nv12(&isp, f, raw, y0, 64, uv, 64, 64, 48, 0, 48, false);
		isp.shift_x = isp.shift_y = 0;
		isp_render_nv12(&isp, f, raw, y1, 64, uv, 64, 64, 48, 0, 48, false);
		CHECK(memcmp(y0, y1, sizeof(y0)) == 0);
		isp.shift_x = 0.2f;
		isp_render_nv12(&isp, f, raw, y1, 64, uv, 64, 64, 48, 0, 48, false);
		for (int i = 0; i < 48; i++) { left0 += y0[i * 64 + 2]; left1 += y1[i * 64 + 2]; }
		CHECK(left1 > left0 + 48 * 10);                  /* (the window moved right: its left edge is brighter) */
		isp.shift_x = 5;                                /* (as far as the sensor goes, no further) */
		isp_render_nv12(&isp, f, raw, y2, 64, uv, 64, 64, 48, 0, 48, false);
		isp.shift_x = 0.25f;
		isp_render_nv12(&isp, f, raw, y1, 64, uv, 64, 64, 48, 0, 48, false);
		CHECK(memcmp(y1, y2, sizeof(y1)) == 0);
		free(f);
	}

	/* AE: a dark frame asks for more exposure, a bright one for less; a kernel that does not say its limits is not asked */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
		struct isp isp;
		struct isp_stats st;
		struct redwood_camera_command c;
		size_t stride = f->stride;

		for (int level = 0; level < 2; level++) {
			for (int yy = 0; yy < H; yy++) {
				for (int x = 0; x < W; x++) {
					put10(raw + (size_t)yy * stride, x, 64 + (level ? 900 : 12));
				}
			}
			isp_init(&isp);
			isp_measure(f, raw, &st);
			bool asked = isp_ae(&isp, f, &st, &c);

			CHECK(asked && c.command == REDWOOD_CAMERA_EXPOSURE);
			if (level == 0) {
				CHECK(c.a * (double)c.b > 1000.0 * 256);     /* more exposure x gain than the frame had */
			} else {
				CHECK(c.a * (double)c.b < 1000.0 * 256);     /* less */
			}
		}
		f->gain_unity = 0;
		isp_init(&isp);
		CHECK(!isp_ae(&isp, f, &st, &c));
		f->gain_unity = 256; f->frame_lines_min = 0;
		CHECK(!isp_ae(&isp, f, &st, &c));
		free(f);
	}

	/* AE and a kernel that starts its session anew (run q10ha): the frames never show what was asked; AE asks again, then goes on from what they have */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
		struct isp isp;
		struct isp_stats st;
		struct redwood_camera_command c, first;
		size_t stride = f->stride;
		int again = 0, fresh = 0;

		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, 64 + 12);
			}
		}
		isp_init(&isp);
		isp_measure(f, raw, &st);
		CHECK(isp_ae(&isp, f, &st, &first));
		for (int n = 1; n <= 9; n++) {          /* the frames keep the exposure the sensor started with */
			memset(&c, 0, sizeof(c));
			if (isp_ae(&isp, f, &st, &c)) {
				if (n == 3 || n == 6) {
					again += c.a == first.a && c.b == first.b && c.c == first.c;
				} else if (n == 9) {
					fresh += c.command == REDWOOD_CAMERA_EXPOSURE && c.a * (double)c.b > 1000.0 * 256;
				} else {
					CHECK(0);
				}
			}
		}
		CHECK(again == 2 && fresh == 1);
		/* and frames that do have what was asked: AE goes on once ISP_AE_SETTLE of them have been seen (the first ones were taken with the old exposure) */
		f->exposure_lines = isp.asked_lines; f->gain = isp.asked_gain;
		for (int n = 0; n < ISP_AE_SETTLE; n++) {
			CHECK(!isp_ae(&isp, f, &st, &c));
		}
		(void)isp_ae(&isp, f, &st, &c);         /* (a command if there is more to ask than the sensor's limits have given already) */
		CHECK(isp.asked_waits == 0 && isp.settled <= ISP_AE_SETTLE);
		free(f);
	}

	/*
	 * AE and a lamp in the picture (run q10hv: bands that move). The picture is dim but for the lamp, whose level is at white whatever the
	 * exposure: the exposure never goes under one period of the mains (953 lines here) and is a whole number of them; daylight (all of the
	 * picture bright) does go under it, and does not come back for a little less light.
	 */
	{
		struct redwood_camera_frame f;
		struct isp isp;
		struct isp_stats st;
		struct redwood_camera_command c;
		double lines = 2859, gain = 4, room = 0.012, least = 1e9;
		int not_whole = 0;

		memset(&f, 0, sizeof(f));
		memset(&st, 0, sizeof(st));
		f.width = 4096; f.height = 3072; f.black = 64; f.gain_unity = 256; f.gain_max = 16 * 256; f.frame_lines_min = 3176; f.margin = 48;
		f.line_ns = 10494; f.frame_lines_max = 4 * 3176;
		isp_init(&isp);
		for (int n = 0; n < 400; n++) {
			double light = n < 200 ? room : n < 300 ? 3.0 : 1.6;     /* the room with the lamp; daylight; a cloud */
			double level = light * lines * gain;

			st.mean[1] = level > 959 ? 959 : level;
			st.bright = n < 200 ? 959 : (level * 1.5 > 959 ? 959 : level * 1.5);
			f.exposure_lines = (uint32_t)lines; f.gain = (uint32_t)(gain * 256 + 0.5);
			if (isp_ae(&isp, &f, &st, &c)) {
				lines = c.a; gain = c.b / 256.0;
				if (n < 200) {
					least = lines < least ? lines : least;
					not_whole += c.a % 953 > 2 && c.a % 953 < 951;
				}
			}
			if (n == 199) { CHECK(lines >= 951 && not_whole == 0 && least >= 951); }
			if (n == 299) { CHECK(lines < 400); }                    /* (daylight: well under the period) */
		}
		CHECK(lines < 900);                                             /* (a cloud: still under it, not to and fro) */
	}

	/*
	 * AE and the highlights, for a camera that was hidden and is shown (run q10hw: the ultra-wide camera twice as bright as the wide one):
	 * with a lamp in the picture (the brightest 1 % at white) the level goes to half the target, whether AE was "where it should be" at the
	 * full target before or not; and levels without their brightest 1 % follow the share of the target they are told.
	 */
	{
		struct redwood_camera_frame f;
		struct isp isp;
		struct isp_stats st;
		struct redwood_camera_command c;
		double lines = 2859, gain = 8, light = 0.0063, target = 0.15 * 959, level = 0;

		memset(&f, 0, sizeof(f));
		memset(&st, 0, sizeof(st));
		f.width = 3264; f.height = 2448; f.black = 64; f.gain_unity = 256; f.gain_max = 16 * 256; f.frame_lines_min = 2548; f.margin = 48;
		f.line_ns = 13080; f.frame_lines_max = 4 * 2548;
		isp_init(&isp);
		for (int n = 0; n < 240; n++) {
			level = light * lines * gain;
			st.mean[1] = level > 959 ? 959 : level;
			st.bright = n < 80 ? 0 : 959;           /* hidden (the header's levels); then shown, a lamp in the picture */
			isp.hidden_aim = n < 40 ? 1 : 0.5f;     /* (the other camera sees the lamp from frame 40 on) */
			f.exposure_lines = (uint32_t)lines; f.gain = (uint32_t)(gain * 256 + 0.5);
			if (isp_ae(&isp, &f, &st, &c)) { lines = c.a; gain = c.b / 256.0; }
			if (n == 39) { CHECK(level > target * 0.85 && level < target * 1.15 && isp.steady); }
			if (n == 79) { CHECK(level > target * 0.42 && level < target * 0.58); }
		}
		CHECK(level > target * 0.42 && level < target * 0.58 && isp.aim > 0.49f && isp.aim < 0.6f);
		/* shown at the full target, "where it should be", when the lamp comes into the picture: down to the half all the same */
		isp_init(&isp);
		lines = 2859; gain = 8;
		for (int n = 0; n < 160; n++) {
			level = light * lines * gain;
			st.mean[1] = level;
			st.bright = n < 80 ? level * 1.5 : 959;
			f.exposure_lines = (uint32_t)lines; f.gain = (uint32_t)(gain * 256 + 0.5);
			if (isp_ae(&isp, &f, &st, &c)) { lines = c.a; gain = c.b / 256.0; }
			if (n == 79) { CHECK(level > target * 0.85 && level < target * 1.15 && isp.steady); }
		}
		CHECK(level > target * 0.42 && level < target * 0.58);
	}

	/*
	 * AE in the dark (run q10hz: noise; 30 ms x 64 x 4 in the logs): the frames get longer before the gain goes over ISP_AE_GAIN_FIRST,
	 * the gain never over ISP_AE_GAIN_MOST nor the digital one over ISP_AE_DIGITAL_MOST, the time a whole number of 10 ms; and a little more
	 * light does not bring the short frames back at once.
	 */
	{
		struct redwood_camera_frame f;
		struct isp isp;
		struct isp_stats st;
		struct redwood_camera_command c;
		double lines = 2859, gain = 1, frame = 3176, target = 0.15 * 959;
		int to_long = 0, to_short = 0;

		memset(&f, 0, sizeof(f));
		memset(&st, 0, sizeof(st));
		f.width = 4096; f.height = 3072; f.black = 64; f.gain_unity = 256; f.gain_max = 64 * 256; f.frame_lines_min = 3176; f.margin = 48;
		f.line_ns = 10494; f.frame_lines_max = 8 * 3176;
		isp_init(&isp);
		isp.max_frame_lines = 2 * 3176;
		for (int n = 0; n < 900; n++) {
			/* room light; dim (gain 3 would do); dark (longer frames); darker than there is gain for; dim again, a little brighter; room */
			double light = n < 100 ? 0.05 : n < 250 ? 0.017 : n < 400 ? 0.004 : n < 550 ? 0.0001 : n < 700 ? 0.0052 : 0.05;
			double level = light * lines * gain;
			bool was_long = frame > 3176 + 8;

			st.mean[1] = level > 959 ? 959 : level;
			st.bright = level * 1.4 > 959 ? 959 : level * 1.4;
			f.exposure_lines = (uint32_t)lines; f.gain = (uint32_t)(gain * 256 + 0.5); f.frame_lines = (uint32_t)frame;
			if (isp_ae(&isp, &f, &st, &c)) {
				lines = c.a; gain = c.b / 256.0; frame = c.c ? c.c : 3176;
				CHECK(gain <= ISP_AE_GAIN_MOST + 0.01 && isp.digital <= ISP_AE_DIGITAL_MOST + 0.01 && c.a <= 2 * 3176 - 48);
				CHECK(c.a < 953 || c.a % 953 <= 2 || c.a % 953 >= 951);
			}
			to_long += !was_long && frame > 3176 + 8;
			to_short += was_long && frame <= 3176 + 8;
			if (n == 99 || n == 899) { CHECK(frame <= 3176 && gain < 1.6 && level > target * 0.8 && level < target * 1.25); }
			if (n == 249) { CHECK(frame <= 3176 && gain > 2.5 && gain < ISP_AE_GAIN_FIRST && level > target * 0.8); }
			if (n == 399) { CHECK(frame > 5000 && gain > 3 && gain < 9 && level > target * 0.8 && level < target * 1.25); }
			if (n == 549) { CHECK(frame > 5000 && gain > 15.9 && isp.digital > 1.99f); }   /* (all there is: the picture stays dark) */
			if (n == 699) { CHECK(frame > 5000 && level > target * 0.8 && level < target * 1.25); }  /* (a third more light: the frames stay long) */
		}
		CHECK(to_long == 1 && to_short == 1);
	}

	/*
	 * AE against a sensor as the kernel's is (run q10ht: the exposure changed 40 to 110 times in 150 frames): a command is in the header
	 * HEADER frames after it was given and in the picture EFFECT frames after that. For every such delay: the level is at its target within
	 * 30 frames, never more than a half over it on the way, and once there no command is given in 150 frames of the same light; a light
	 * twenty times weaker, then sixty times stronger, the same again; and the exposure is a whole number of 10 ms whenever it is 10 ms or more.
	 */
	for (int header = 0; header <= 2; header++) {
		for (int effect = 0; effect <= 2; effect++) {
			struct redwood_camera_frame f;
			struct isp isp;
			struct isp_stats st;
			struct redwood_camera_command c;
			/* what the sensor has: [0] in the picture now ... the newest last */
			double lines_q[8], gain_q[8], light = 0.004, target, worst_over = 0;
			int commands = 0, steady_commands = 0, settled_at = -1, bad_times = 0, phase_start = 0, late = 0;

			memset(&f, 0, sizeof(f));
			memset(&st, 0, sizeof(st));
			f.width = 4096; f.height = 3072; f.black = 64; f.gain_unity = 256; f.gain_max = 16 * 256; f.frame_lines_min = 3176; f.margin = 48;
			f.line_ns = 10494;             /* (30 frames a second: a 10 ms period is 953 lines) */
			f.frame_lines_max = 4 * 3176;
			isp_init(&isp);
			target = 0.15 * (1023 - 64);
			for (int i = 0; i < 8; i++) { lines_q[i] = 1000; gain_q[i] = 2; }
			for (int n = 0; n < 600; n++) {
				double level;

				if (n == 200) { light /= 20; phase_start = n; settled_at = -1; }
				if (n == 400) { light *= 60; phase_start = n; settled_at = -1; }
				level = light * lines_q[0] * gain_q[0];
				level = level > 959 ? 959 : level;
				st.mean[1] = level;
				st.bright = level * 1.6 > 959 ? 959 : level * 1.6;
				f.exposure_lines = (uint32_t)lines_q[effect];
				f.gain = (uint32_t)(gain_q[effect] * 256 + 0.5);
				if (isp_ae(&isp, &f, &st, &c)) {
					commands++;
					steady_commands += settled_at >= 0;
					lines_q[7] = c.a;
					gain_q[7] = c.b / 256.0;
					if (c.a >= 953 && (c.a % 953 > 2 && c.a % 953 < 951)) { bad_times++; }
				}
				level *= isp.digital;
				if (settled_at < 0 && level > target * 0.8 && level < target * 1.25) {
					settled_at = n;
					late += n - phase_start > 30;
				} else if (settled_at >= 0) {
					/* (once there: how far it goes off again, either way) */
					double off = level > target ? level / target : target / level;

					worst_over = off > worst_over ? off : worst_over;
				}
				/* the sensor's delays: the newest command moves one place a frame until it is `header + effect' frames old */
				for (int i = 0; i < 7; i++) {
					if (i >= header + effect) { lines_q[i] = lines_q[7]; gain_q[i] = gain_q[7]; }
					else { lines_q[i] = lines_q[i + 1]; gain_q[i] = gain_q[i + 1]; }
				}
			}
			if (getenv("VERBOSE")) {
				printf("   AE, header %d effect %d frames late: %d commands, %d after settling, late %d, worst %.2f off the target once there, %d times not 10 ms\n",
				    header, effect, commands, steady_commands, late, worst_over, bad_times);
			}
			/* (at most the two last small steps in each of the three lights after the level is first there; never off by a third again) */
			CHECK(steady_commands <= 6 && late == 0 && bad_times == 0 && settled_at >= 0 && commands <= 24);
			CHECK(worst_over < 1.34);
		}
	}

	/* format 4 (the CAMSV DMA's little-endian 40-bit words) gives the very pictures and levels format 1 (MIPI RAW10) does */
	{
		for (int bayer = 0; bayer < 4; bayer++) {
			uint8_t *raw, *raw4;
			struct redwood_camera_frame *f = make_frame(W, H, bayer, 64, &raw), *f4 = make_frame(W, H, bayer, 64, &raw4);
			struct isp isp;
			struct isp_stats st, st4;
			static uint8_t y[OW * OH], uv[OW * OH / 2], y4[OW * OH], uv4[OW * OH / 2];
			static uint8_t fy[W * H], fuv[W * H / 2], fy4[W * H], fuv4[W * H / 2];

			for (size_t g = 0; g < (size_t)f->stride * H; g += 5) {
				const uint8_t *m = raw + g;
				uint64_t x = 0;

				for (int k = 0; k < 4; k++) {
					x |= (uint64_t)(m[k] << 2 | (m[4] >> (k * 2) & 3)) << (10 * k);
				}
				for (int k = 0; k < 5; k++) {
					raw4[g + k] = (uint8_t)(x >> (8 * k));
				}
			}
			f4->format = REDWOOD_CAMERA_FORMAT_LE40_RAW10;
			isp_init(&isp);
			isp_measure(f, raw, &st);
			isp_measure(f4, raw4, &st4);
			CHECK(st.cells == st4.cells && st.mean[0] == st4.mean[0] && st.mean[1] == st4.mean[1] && st.mean[2] == st4.mean[2]);
			isp_render_nv12(&isp, f, raw, y, OW, uv, OW, OW, OH, 0, OH, false);
			isp_render_nv12(&isp, f4, raw4, y4, OW, uv4, OW, OW, OH, 0, OH, false);
			CHECK(memcmp(y, y4, sizeof(y)) == 0 && memcmp(uv, uv4, sizeof(uv)) == 0);
			isp_render_full_nv12(&isp, f, raw, fy, W, fuv, W, 0, H, false);
			isp_render_full_nv12(&isp, f4, raw4, fy4, W, fuv4, W, 0, H, false);
			CHECK(memcmp(fy, fy4, sizeof(fy)) == 0 && memcmp(fuv, fuv4, sizeof(fuv)) == 0);
			free(f); free(f4);
		}
	}

	/* zoom: the middle 1/zoom of the picture, from the sensor's own pixels; flip: left/right and top/bottom exchanged; both formats, all Bayer orders */
	{
		for (int bayer = 0; bayer < 4; bayer++) {
			for (int fmt = 1; fmt <= 4; fmt += 3) {
				uint8_t *raw;
				struct redwood_camera_frame *f = make_frame(W, H, bayer, 64, &raw);
				struct isp isp;
				static uint8_t a[OW * OH * 4], z[OW * OH * 4];
				int r, g, b, r2, g2, b2;

				if (fmt == 4) {
					for (size_t k = 0; k < (size_t)f->stride * H; k += 5) {
						uint8_t *m = raw + k;
						uint64_t x = 0;

						for (int i = 0; i < 4; i++) {
							x |= (uint64_t)(m[i] << 2 | (m[4] >> (i * 2) & 3)) << (10 * i);
						}
						for (int i = 0; i < 5; i++) {
							m[i] = (uint8_t)(x >> (8 * i));
						}
					}
					f->format = REDWOOD_CAMERA_FORMAT_LE40_RAW10;
				}
				isp_init(&isp);
				isp_render_bgra(&isp, f, raw, a, OW * 4, OW, OH, 0, OH);
				/* zoom 1.0 with a flip of both: the picture turned half round (the window path against the cell path: the same colours) */
				isp.flip = ISP_FLIP_H | ISP_FLIP_V;
				isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
				for (int q = 0; q < 4; q++) {
					int x = (q & 1 ? 3 : 1) * OW / 4, y = (q & 2 ? 3 : 1) * OH / 4;

					at_bgra(a, OW * 4, x, y, &r, &g, &b);
					at_bgra(z, OW * 4, OW - 1 - x, OH - 1 - y, &r2, &g2, &b2);
					CHECK(abs(r - r2) <= 3 && abs(g - g2) <= 3 && abs(b - b2) <= 3);
				}
				/* flip H only: left and right patches exchanged, top and bottom not */
				isp.flip = ISP_FLIP_H;
				isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
				at_bgra(a, OW * 4, OW / 4, OH / 4, &r, &g, &b);
				at_bgra(z, OW * 4, 3 * OW / 4, OH / 4, &r2, &g2, &b2);
				CHECK(abs(r - r2) <= 3 && abs(g - g2) <= 3 && abs(b - b2) <= 3);
				/* zoom 4: every quarter of the picture is still its patch (the patches meet in the middle), and a pixel near the middle of a
				 * quarter is what the unzoomed picture has an eighth of the way from the middle */
				isp.flip = 0;
				isp.zoom = 4;
				isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
				for (int q = 0; q < 4; q++) {
					int x = (q & 1 ? 3 : 1) * OW / 4, y = (q & 2 ? 3 : 1) * OH / 4;
					int ux = OW / 2 + (x - OW / 2) / 4, uy = OH / 2 + (y - OH / 2) / 4;

					at_bgra(a, OW * 4, ux, uy, &r, &g, &b);
					at_bgra(z, OW * 4, x, y, &r2, &g2, &b2);
					CHECK(abs(r - r2) <= 3 && abs(g - g2) <= 3 && abs(b - b2) <= 3);
				}
				free(f);
			}
		}
		/* the window is the middle: a frame white in its middle half only is all white at zoom 2 (and not at zoom 1) */
		{
			uint8_t *raw;
			struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
			struct isp isp;
			static uint8_t z[OW * OH * 4];
			int dark = 0;

			for (int yy = 0; yy < H; yy++) {
				for (int x = 0; x < W; x++) {
					bool in = x >= W / 4 - 2 && x < 3 * W / 4 + 2 && yy >= H / 4 - 2 && yy < 3 * H / 4 + 2;

					put10(raw + (size_t)yy * f->stride, x, in ? 1023 : 64);
				}
			}
			isp_init(&isp);
			isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
			CHECK(z[0] < 10 && z[(OH / 2) * OW * 4 + (OW / 2) * 4] > 250);
			isp.zoom = 2;
			isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
			for (size_t i = 0; i < sizeof(z); i += 4) {
				dark += z[i] < 250 || z[i + 1] < 250 || z[i + 2] < 250;
			}
			CHECK(dark == 0);
			free(f);
		}
	}

	/* highlights (run q10hf: strong white lamps green in the front camera): what is at the sensor's white is white whatever the white balance,
	 * a pixel with one channel burnt out has no colour, and clipped cells do not pull the white balance */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
		struct isp isp;
		struct isp_stats st;
		static uint8_t z[OW * OH * 4];
		static const float gains[4][3] = { { 0.6f, 1, 0.7f }, { 2.0f, 1, 1.6f }, { 1, 1, 1 }, { 0.9f, 1, 2.5f } };
		size_t stride = f->stride;

		for (int k = 0; k < 4; k++) {
			for (int half = 0; half < 2; half++) {          /* everything at white; then green at white and red, blue at 60 % */
				int off = 0;

				for (int yy = 0; yy < H; yy++) {
					for (int x = 0; x < W; x++) {
						put10(raw + (size_t)yy * stride, x, half && colour_at(0, x, yy) != 1 ? 64 + 575 : 1023);
					}
				}
				isp_init(&isp);
				isp.wb[0] = gains[k][0]; isp.wb[2] = gains[k][2];
				isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
				for (size_t i = 0; i < sizeof(z); i += 4) {
					off += z[i] < 250 || abs(z[i] - z[i + 1]) > 3 || abs(z[i + 1] - z[i + 2]) > 3;
				}
				CHECK(off == 0);
			}
		}
		/* the top half burnt out, the bottom half a colour with half as much red and blue as green: the gains go to 2, not towards 1 */
		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, yy < H / 2 ? 1023 : 64 + (colour_at(0, x, yy) == 1 ? 400 : 200));
			}
		}
		isp_init(&isp);
		isp.wb_free = true;
		isp_measure(f, raw, &st);
		CHECK(st.fair_cells > 0 && st.fair_cells < st.cells && st.fair[1] > 395 && st.fair[1] < 405);
		for (int i = 0; i < 40; i++) {
			isp_awb(&isp, &st);
		}
		CHECK(isp.wb[0] > 1.95f && isp.wb[0] < 2.05f && isp.wb[2] > 1.95f && isp.wb[2] < 2.05f);
		/* nothing but clipped cells: the white balance stays as it is */
		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, 1023);
			}
		}
		isp_measure(f, raw, &st);
		isp_awb(&isp, &st);
		CHECK(st.fair_cells == 0 && isp.wb[0] > 1.95f);
		free(f);
	}

	/* colour (run q10hg): the white balance keeps to real lights -- a grey wall is grey in daylight and a little warm in a warm lamp's light, a
	 * picture full of yellow stays yellow --, a bright colour the sensor did not clip keeps its colour, skin is skin, and the lens's falloff is
	 * undone by the square of the distance from the middle */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
		struct isp isp;
		struct isp_stats st;
		static uint8_t z[OW * OH * 4];
		size_t stride = f->stride;
		int r, g, b;
		/* what the sensor gives for a grey of 300 in: daylight, a warm lamp, cold shade (the gains' inverses); then a yellow and skin in daylight */
		static const double scenes[5][3] = { { 300 / 2.0, 300, 300 / 1.7 }, { 300 / 1.3, 300, 300 / 2.9 }, { 300 / 2.3, 300, 300 / 1.45 },
			{ 420 / 2.0, 380, 60 / 1.7 }, { 330 / 2.0, 200, 140 / 1.7 } };

		for (int k = 0; k < 5; k++) {
			for (int yy = 0; yy < H; yy++) {
				for (int x = 0; x < W; x++) {
					/* (skin: a face before a grey wall, the left half of the picture) */
					put10(raw + (size_t)yy * stride, x, 64 + (int)scenes[k == 4 && x < W / 2 ? 0 : k][colour_at(0, x, yy)]);
				}
			}
			isp_init(&isp);
			isp.wb[0] = isp.wb_day[0]; isp.wb[2] = isp.wb_day[1];
			isp_measure(f, raw, &st);
			for (int i = 0; i < 60; i++) {
				isp_awb(&isp, &st);
			}
			isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
			at_bgra(z, OW * 4, 3 * OW / 4, OH / 2, &r, &g, &b);
			if (getenv("VERBOSE")) { printf("   | colour %d: %d %d %d, wb %.2f %.2f\n", k, r, g, b, isp.wb[0], isp.wb[2]); }
			if (k == 0) {
				CHECK(abs(r - g) <= 3 && abs(g - b) <= 3);                      /* daylight: grey */
			} else if (k == 1) {
				CHECK(r > g && g > b && r - b >= 8 && r - b <= 70);             /* a warm lamp: a little warm, not orange, not grey */
			} else if (k == 2) {
				CHECK(abs(r - g) <= 12 && abs(g - b) <= 12);                    /* shade: (nearly) grey */
			} else if (k == 3) {
				CHECK(r > 150 && g > 130 && b < g - 60 && b < r - 60);          /* yellow stays yellow */
			} else {
				CHECK(r > g + 15 && g > b + 5 && r - b < 150);                  /* skin: red over green over blue */
			}
		}
		/* a strong red the sensor did not clip (red at 800 of 959, twice that after its gain) is red, not white */
		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, 64 + (colour_at(0, x, yy) == 0 ? 800 : 90));
			}
		}
		isp_init(&isp);
		isp.wb[0] = 2.0f; isp.wb[2] = 1.7f;
		isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
		at_bgra(z, OW * 4, OW / 2, OH / 2, &r, &g, &b);
		CHECK(r > 240 && g < r - 100 && b < r - 100);
		/* falloff 0.5: a flat grey comes out 1.5 times as much light at a corner as in the middle (before the tone curve), the middle unchanged */
		for (int yy = 0; yy < H; yy++) {
			for (int x = 0; x < W; x++) {
				put10(raw + (size_t)yy * stride, x, 64 + 200);
			}
		}
		{
			int mid0, mid1, corner1, r2, g2, b2;

			isp_init(&isp);
			isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
			at_bgra(z, OW * 4, OW / 2, OH / 2, &mid0, &g, &b);
			isp.falloff = 0.5f;
			isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
			at_bgra(z, OW * 4, OW / 2, OH / 2, &mid1, &g, &b);
			at_bgra(z, OW * 4, 0, 0, &corner1, &g2, &b2);
			CHECK(abs(mid0 - mid1) <= 1 && corner1 > mid1 + 15 && abs(corner1 - g2) <= 2 && abs(g2 - b2) <= 2);
			isp.zoom = 2; isp.flip = ISP_FLIP_H;            /* (and by the place on the SENSOR: zoomed to its middle, hardly any) */
			isp_render_bgra(&isp, f, raw, z, OW * 4, OW, OH, 0, OH);
			at_bgra(z, OW * 4, 0, 0, &r2, &g2, &b2);
			CHECK(r2 > mid1 && r2 < corner1 - 8);
		}
		free(f);
	}

	/* autofocus by contrast: a sharp picture measures sharper than a soft one, and the scan leaves the lens at the sharpest position */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
		size_t stride = f->stride;
		double sharp, soft, flat;

		for (int pass = 0; pass < 3; pass++) {
			for (int yy = 0; yy < H; yy++) {
				for (int x = 0; x < W; x++) {
					int stripe = (x / 2) & 1;       /* cells light and dark in turn; soft: a third of the difference; flat: none */

					put10(raw + (size_t)yy * stride, x, 64 + (pass == 0 ? (stripe ? 700 : 100) : pass == 1 ? (stripe ? 500 : 300) : 400));
				}
			}
			double v = isp_sharpness(f, raw, 0, 0, W, H);

			if (pass == 0) { sharp = v; } else if (pass == 1) { soft = v; } else { flat = v; }
		}
		CHECK(sharp > 4 * soft && soft > 0 && flat == 0);
		CHECK(isp_sharpness(f, raw, W, H, 8, 8) == 0);          /* (a window outside the frame) */
		free(f);

		static const int peaks[5] = { 0, 137, 500, 888, 1023 };

		for (int k = 0; k < 5; k++) {
			struct isp_af af;
			uint32_t lens = 300, move = 0, moves = 0, quiet_moves = 0;
			double peak = peaks[k];
			uint32_t was[3] = { 300, 300, 300 };            /* (a frame shows the lens of two frames before) */

			uint32_t far_moves = 0, biggest = 0, search_frames = 0;

			memset(&af, 0, sizeof(af));
			af.pos = 300;                                   /* (where the frames say the lens is) */
			for (int frame = 0; frame < 700; frame++) {
				double d = ((double)was[0] - peak) / 150.0, s = exp(-d * d) + 0.02;

				if (frame == 400) {
					CHECK(af.state == 0 && abs((int)lens - peaks[k]) <= 26);
					peak = peaks[k] < 512 ? peaks[k] + 450 : peaks[k] - 450;       /* the scene changes */
				}
				af.seen = was[0];
				if (isp_af(&af, s, 1023, &move)) {
					uint32_t by = move > lens ? move - lens : lens - move;

					CHECK(move <= 1023);
					biggest = by > biggest ? by : biggest;
					lens = move;
					moves++;
					quiet_moves += frame >= 300 && frame < 400;
					/* (in focus and the scene the same: a search that has to look stays near, it does not sweep) */
					far_moves += frame >= 250 && frame < 400 && abs((int)move - peaks[k]) > 80;
				}
				search_frames += frame >= 400 && af.state != 0;
				was[0] = was[1]; was[1] = was[2]; was[2] = lens;
			}
			if (getenv("VERBOSE")) { printf("   AF peak %d then %.0f: lens %u, %u moves, biggest %u, far %u, search frames %u, scans %u state %u\n", peaks[k], peak, lens, moves, biggest, far_moves, search_frames, af.scans, af.state); }
			CHECK(moves > 3 && quiet_moves == 0 && af.scans >= 2 && af.state == 0);
			CHECK(fabs((double)lens - peak) <= 26);
			/* never more than a 32nd of the range in a frame; nothing far from the focus while the scene is the same; 450 away found in 2 s */
			CHECK(biggest <= 2 * (1023 / ISP_AF_SLEW) + 1 && far_moves == 0 && search_frames < 45);
			CHECK(!isp_af(&af, 1.0, 0, &move));             /* (no lens to move) */
		}
	}

	/*
	 * Autofocus with the sharpness as it is on the phone (run q10ia: one frame's is off the next one's by a third; "it practically never
	 * acts by itself"): from a lens at 0, never in focus; a scene that stays (no search that goes far, hardly any at all); the camera
	 * turned from a near thing to a far one and back (found each time, by itself, within 4 s); a scene with nothing to tell by is left.
	 */
	for (int run = 0; run < 6; run++) {
		struct isp_af af;
		unsigned seed = 77u + 1000u * (unsigned)run;
		uint32_t lens = 0, move = 0, was[3] = { 0, 0, 0 }, still_searches = 0, still_far = 0, found_at[3] = { 0, 0, 0 };
		double peak = 300;

		memset(&af, 0, sizeof(af));
		for (int frame = 0; frame < 1500; frame++) {
			double d, s, noise;
			uint32_t state = af.state;

			if (frame == 600) { peak = 650; }
			if (frame == 1050) { peak = 300; }
			d = ((double)was[0] - peak) / 110.0;
			seed = seed * 1103515245u + 12345u;
			noise = 0.75 + 0.5 * (double)((seed >> 12) & 0xfff) / 4095.0;
			s = (40 * exp(-d * d) + 3) * noise;
			af.seen = was[0];
			af.light = 1.0;
			if (isp_af(&af, s, 1023, &move)) {
				lens = move;
				still_far += frame >= 250 && frame < 600 && abs((int)move - 300) > 90;
			}
			still_searches += frame >= 250 && frame < 600 && state == 0 && af.state != 0;
			was[0] = was[1]; was[1] = was[2]; was[2] = lens;
			if (frame == 249) { found_at[0] = lens; }
			if (frame == 599 + 120) { found_at[1] = lens; }
			if (frame == 1049 + 120) { found_at[2] = lens; }
		}
		if (getenv("VERBOSE")) {
			printf("   AF, noisy: lens %u at first, %u after near to far, %u after far to near; %u searches (%u moves far) while the scene stayed\n",
			    found_at[0], found_at[1], found_at[2], still_searches, still_far);
		}
		CHECK(abs((int)found_at[0] - 300) <= 70 && abs((int)found_at[1] - 650) <= 70 && abs((int)found_at[2] - 300) <= 70);
		CHECK(still_searches <= 2 && still_far == 0);
	}

	/* a tap: the levels of a window only (the scene's patches: red top left, grey bottom right), and a lens that is to stay where its scan left it */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(W, H, 0, 64, &raw);
		struct isp_stats all, red, grey, none;
		struct isp_af af;
		uint32_t move = 0, moves = 0, lens = 0;

		isp_measure(f, raw, &all);
		isp_measure_window(f, raw, 4, 4, W / 2 - 8, H / 2 - 8, &red);
		isp_measure_window(f, raw, W / 2 + 4, H / 2 + 4, W / 2 - 8, H / 2 - 8, &grey);
		isp_measure_window(f, raw, W, H, 8, 8, &none);
		CHECK(red.cells > 0 && red.cells < all.cells && red.mean[0] > 650 && red.mean[1] < 80 && red.mean[2] < 80);
		CHECK(grey.mean[0] > 380 && grey.mean[0] < 420 && grey.mean[1] > 380 && grey.mean[1] < 420 && grey.mean[2] > 380 && grey.mean[2] < 420);
		CHECK(none.cells == all.cells);                         /* (a window outside the frame: the whole frame) */
		free(f);
		memset(&af, 0, sizeof(af));
		af.hold = true;
		for (int frame = 0; frame < 400; frame++) {
			double d = ((double)lens - 600) / 150.0, sh = frame < 200 ? exp(-d * d) + 0.02 : 0.001;   /* (then the scene changes: not followed) */

			af.seen = lens;
			if (isp_af(&af, sh, 1023, &move)) {
				lens = move;
				moves += frame >= 200;
			}
		}
		CHECK(af.scans >= 1 && af.scans <= 2 && moves == 0 && abs((int)lens - 600) <= 30);    /* (the look over the range and the small steps after it: one focusing) */
		isp_af_again(&af);                                      /* (a new tap is: a search at once, from where the lens is) */
		{
			int moved = 0;

			for (int i = 0; i < 3; i++) { moved += isp_af(&af, 0.5, 1023, &move); }
			CHECK(moved >= 1 && abs((int)move - (int)lens) <= 1023 / 16 + 1);
		}
		/* a picture with nothing to tell by (dark: the sharpness 0 everywhere): the lens is left where it is (run q10hz: it went to 0) */
		memset(&af, 0, sizeof(af));
		af.pos = 300;
		lens = 300;
		for (int frame = 0; frame < 200; frame++) {
			if (isp_af(&af, 0.0, 1023, &move)) { lens = move; }
		}
		CHECK(lens == 300 && af.state == 0);
	}

	/* speed, for the record: a 4096x3072 frame to a 1504x1128 NV12 preview, one thread */
	{
		uint8_t *raw;
		struct redwood_camera_frame *f = make_frame(4096, 3072, 0, 64, &raw);
		struct isp isp;
		uint8_t *y = malloc(1504 * 1128), *uv = malloc(1504 * 1128 / 2);
		struct timespec t0, t1;

		isp_init(&isp);
		clock_gettime(CLOCK_MONOTONIC, &t0);
		isp_render_nv12(&isp, f, raw, y, 1504, uv, 1504, 1504, 1128, 0, 1128, false);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		printf("info: 4096x3072 RAW10 -> 1504x1128 NV12: %.1f ms on this host (one thread)\n",
		    (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
		free(y); free(uv); free(f);
	}

	printf(failures ? "t_isp: %d FAILURES\n" : "t_isp: all passed\n", failures);
	return failures != 0;
}
