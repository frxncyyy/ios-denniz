/*
 * vid_gpu_host_test.c -- WP08's GPU side, on the host: the same inputs through
 * kern/n2vk_ref.c and through gpu/air_video.c's passes on softpipe, every output value
 * compared; then each shader through Panfrost's own Valhall compiler for the Mali-G77.
 *
 * No GPU, no phone.  softpipe is the reference rasteriser of the host Mesa build; Panfrost
 * compiles but does not execute here.  Products stay under gpu/video/build/WP08/.
 *
 * SPDX-License-Identifier: MIT
 */
#include "air_metal.c"
#include "air_video.c"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_dynarray.h"

static int tests_run, tests_failed;
static const char *current_test;
static unsigned long long differing_total;
static struct rwa_vid *gpu;

#define CHECK(cond) do {                                                                        \
	if (!(cond)) {                                                                          \
		fprintf(stderr, "FAILED [%s] %s:%d: %s\n", current_test, __FILE__, __LINE__, #cond);  \
		tests_failed++;                                                                      \
		return;                                                                              \
	}                                                                                        \
} while (0)

static void
start(const char *name)
{
	current_test = name;
	tests_run++;
}

static uint32_t lcg_state;

static unsigned long long
count_diff(const void *a, const void *b, size_t n)
{
	const uint8_t *x = a, *y = b;
	unsigned long long d = 0;

	for (size_t i = 0; i < n; i++)
		d += x[i] != y[i];
	differing_total += d;
	return d;
}

static void
lcg_seed(uint32_t seed)
{
	lcg_state = seed;
}

static uint8_t
lcg_byte(void)
{
	lcg_state = lcg_state * 1664525u + 1013904223u;
	return (uint8_t)(lcg_state >> 24);
}

static void
fill_random(uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		p[i] = lcg_byte();
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * me16
 * ----------------------------------------------------------------------------------------------------------------------------- */

struct padded {
	uint8_t *all, *base;
	uint32_t stride;
};

static int
padded_make(struct padded *p, uint32_t width, uint32_t height, uint32_t pad)
{
	p->stride = width + 2 * pad;
	p->all = calloc(p->stride, height + 2 * pad);
	if (p->all == NULL)
		return 0;
	p->base = p->all + (size_t)pad * p->stride + pad;
	return 1;
}

static uint8_t *
padded_at(struct padded *p, int32_t x, int32_t y, uint32_t pad)
{
	return p->all + (size_t)(y + (int32_t)pad) * p->stride + (size_t)(x + (int32_t)pad);
}

static void
shifted_pictures(struct padded *ref, uint8_t **cur, uint32_t mb_w, uint32_t mb_h, uint32_t pad, int32_t sx, int32_t sy,
                 uint32_t seed)
{
	const uint32_t w = 16 * mb_w, h = 16 * mb_h;

	*cur = malloc((size_t)w * h);
	if (*cur == NULL || !padded_make(ref, w, h, pad)) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}
	lcg_seed(seed);
	fill_random(*cur, (size_t)w * h);
	memset(ref->all, 0, (size_t)ref->stride * (h + 2 * pad));
	for (int32_t y = -pad; y < (int32_t)h + (int32_t)pad; y++)
		for (int32_t x = -pad; x < (int32_t)w + (int32_t)pad; x++) {
			const int32_t cx = x - sx, cy = y - sy;

			if (cx >= 0 && cx < (int32_t)w && cy >= 0 && cy < (int32_t)h)
				*padded_at(ref, x, y, pad) = (*cur)[cy * w + cx];
		}
}

static int
compare_me16(struct rwa_vid *gpu, const n2vk_me16_args *a, const char *what)
{
	const size_t n = (size_t)a->mb_w * a->mb_h;
	int16_t *rx = calloc(n, sizeof(*rx)), *ry = calloc(n, sizeof(*ry)), *gx = calloc(n, sizeof(*gx)), *gy = calloc(n, sizeof(*gy));
	uint32_t *rs = calloc(n, sizeof(*rs)), *gs = calloc(n, sizeof(*gs));
	unsigned long long differing = 0;
	int ok = 0;
	char why[512] = "";

	if (rx == NULL || ry == NULL || gx == NULL || gy == NULL || rs == NULL || gs == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}
	if (n2vk_ref_me16(a, rx, ry, rs) != N2VK_OK || rwa_vid_me16(gpu, a, gx, gy, gs, why, sizeof(why)) != RWA_VID_OK) {
		fprintf(stderr, "FAILED [%s] %s: me16 did not answer: %s\n", current_test, what, why);
		tests_failed++;
		goto out;
	}
	for (size_t i = 0; i < n; i++) {
		if (rx[i] != gx[i] || ry[i] != gy[i] || rs[i] != gs[i]) {
			if (differing < 8)
				fprintf(stderr, "%s mb %zu: ref (%d,%d,%u), GPU (%d,%d,%u)\n", what, i, rx[i], ry[i], rs[i], gx[i], gy[i],
				        gs[i]);
			differing++;
		}
	}
	if (differing != 0) {
		fprintf(stderr, "FAILED [%s] %s: %llu of %zu macroblocks differ\n", current_test, what, differing, n);
		tests_failed++;
		goto out;
	}
	differing_total += differing;
	ok = 1;
out:
	free(rx); free(ry); free(gx); free(gy); free(rs); free(gs);
	return ok;
}

static void
test_me16_shift(void)
{
	enum { MB_W = 3, MB_H = 2, PAD = 16 };
	struct padded ref;
	uint8_t *cur;
	n2vk_me16_args a = { 0 };

	start("GPU me16 shifted (+5,-3), pad 16, range 8");
	shifted_pictures(&ref, &cur, MB_W, MB_H, PAD, 5, -3, 12345);
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = 16 * MB_W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 8;
	CHECK(compare_me16(gpu, &a, "shift"));
	free(cur);
	free(ref.all);
}

static void
test_me16_centres(void)
{
	enum { MB_W = 5, MB_H = 3, PAD = 16 };
	struct padded ref;
	uint8_t *cur;
	int16_t cx[MB_W * MB_H], cy[MB_W * MB_H];
	n2vk_me16_args a = { 0 };

	start("GPU me16 random centres, pad 16, range 2");
	shifted_pictures(&ref, &cur, MB_W, MB_H, PAD, 3, 1, 987654321u);
	lcg_seed(424242);
	for (int i = 0; i < MB_W * MB_H; i++) {
		cx[i] = (int16_t)((int)lcg_byte() - 128);
		cy[i] = (int16_t)((int)lcg_byte() - 128);
	}
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = 16 * MB_W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 2;
	a.centre_x = cx;
	a.centre_y = cy;
	CHECK(compare_me16(gpu, &a, "centres"));
	free(cur);
	free(ref.all);
}

static void
test_me16_pad0(void)
{
	enum { MB_W = 4, MB_H = 3, PAD = 0 };
	struct padded ref;
	uint8_t *cur;
	n2vk_me16_args a = { 0 };

	start("GPU me16 pad 0, every edge, range 8");
	shifted_pictures(&ref, &cur, MB_W, MB_H, PAD, 1, 2, 271828);
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = 16 * MB_W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 8;
	CHECK(compare_me16(gpu, &a, "pad 0"));
	free(cur);
	free(ref.all);
}

static void
test_me16_wide(void)
{
	enum { MB_W = 3, MB_H = 2, PAD = 16 };
	struct padded ref;
	uint8_t *cur;
	n2vk_me16_args a = { 0 };

	start("GPU me16 range 16: sixteen passes ping-pong the best texture");
	shifted_pictures(&ref, &cur, MB_W, MB_H, PAD, -7, 4, 1618033);
	a.size = sizeof(a);
	a.cur = cur;
	a.cur_stride = 16 * MB_W;
	a.ref = ref.base;
	a.ref_stride = ref.stride;
	a.mb_w = MB_W;
	a.mb_h = MB_H;
	a.pad = PAD;
	a.range = 16;
	CHECK(compare_me16(gpu, &a, "range 16"));
	free(cur);
	free(ref.all);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * half and fdct
 * ----------------------------------------------------------------------------------------------------------------------------- */

static void
test_half(void)
{
	enum { W = 37, H = 23, SS = 2 * W + 7, DS = W + 3 };
	uint8_t src[SS * 2 * H], expect[DS * H], got[DS * H];
	char why[512] = "";

	start("GPU half 37x23");
	lcg_seed(1618033);
	fill_random(src, sizeof(src));
	memset(expect, 0xcc, sizeof(expect));
	memset(got, 0xcc, sizeof(got));
	CHECK(n2vk_ref_half(src, SS, expect, DS, W, H) == N2VK_OK);
	CHECK(rwa_vid_half(gpu, src, SS, got, DS, W, H, why, sizeof(why)) == RWA_VID_OK);
	CHECK(count_diff(expect, got, sizeof(expect)) == 0);
}

static void
test_fdct_random(void)
{
	enum { W = 32, H = 24, SS = W + 5 };
	uint8_t src[SS * H];
	uint16_t q[64];
	int16_t expect[W * H], got[W * H];
	char why[512] = "";

	start("GPU fdct random 32x24");
	lcg_seed(112358);
	fill_random(src, sizeof(src));
	lcg_seed(132134);
	for (int i = 0; i < 64; i++)
		q[i] = (uint16_t)(1 + lcg_byte() % 31);
	CHECK(n2vk_ref_fdct8_quant(src, SS, W, H, q, expect) == N2VK_OK);
	CHECK(rwa_vid_fdct8_quant(gpu, src, SS, W, H, q, got, why, sizeof(why)) == RWA_VID_OK);
	for (int i = 0; i < W * H; i++)
		if (expect[i] != got[i] && count_diff(&expect[i], &got[i], 2) && i < 200)
			fprintf(stderr, "fdct coef %d (u %d, v %d) block %d: ref %d gpu %d\n", i, i % 8, (i / 8) % 8, i / 64,
			        expect[i], got[i]);
	CHECK(memcmp(expect, got, sizeof(expect)) == 0);
}

static void
test_fdct_pattern(void)
{
	uint8_t block[64];
	uint16_t q[64];
	int16_t expect[64], got[64];
	char why[512] = "";

	start("GPU fdct cosine pattern, Q 3");
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
			block[y * 8 + x] = (uint8_t)lround(100.0 * cos((2.0 * x + 1.0) * 3.14159265358979323846 / 16.0)) + 128;
	for (int i = 0; i < 64; i++)
		q[i] = 3;
	CHECK(n2vk_ref_fdct8_quant(block, 8, 8, 8, q, expect) == N2VK_OK);
	CHECK(rwa_vid_fdct8_quant(gpu, block, 8, 8, 8, q, got, why, sizeof(why)) == RWA_VID_OK);
	CHECK(count_diff(expect, got, sizeof(expect)) == 0);
}

static void
test_backend(void)
{
	enum { W = 37, H = 23, SS = 2 * W + 7, DS = W + 3 };
	uint8_t src[SS * 2 * H], expect[DS * H], got[DS * H];
	n2vk_backend backend = { 0 };
	char why[512] = "";

	start("GPU backend through n2vk_use and n2vk_half");
	lcg_seed(4242);
	fill_random(src, sizeof(src));
	memset(expect, 0xcc, sizeof(expect));
	memset(got, 0xcc, sizeof(got));
	CHECK(n2vk_ref_half(src, SS, expect, DS, W, H) == N2VK_OK);
	CHECK(rwa_vid_backend(&backend, gpu) == RWA_VID_OK);
	CHECK(backend.size == sizeof(backend));
	n2vk_use(&backend);
	CHECK(n2vk_half(src, SS, got, DS, W, H) == N2VK_OK);
	n2vk_use(NULL);
	CHECK(count_diff(expect, got, sizeof(expect)) == 0);
}

static void
test_arguments(void)
{
	char why[512] = "";
	uint8_t a[64] = { 0 };
	uint8_t out[64];
	uint16_t q[64];
	n2vk_me16_args m = { 0 };

	start("GPU argument checks return RWA_VID_E_ARGUMENT");
	for (int i = 0; i < 64; i++)
		q[i] = 1;
	CHECK(rwa_vid_half(gpu, NULL, 2, out, 1, 1, 1, why, sizeof(why)) == RWA_VID_E_ARGUMENT);
	CHECK(rwa_vid_me16(gpu, NULL, NULL, NULL, NULL, why, sizeof(why)) == RWA_VID_E_ARGUMENT);
	m.size = sizeof(m);
	CHECK(rwa_vid_me16(gpu, &m, NULL, NULL, NULL, why, sizeof(why)) == RWA_VID_E_ARGUMENT);
	q[7] = 0;
	CHECK(rwa_vid_fdct8_quant(gpu, a, 8, 8, 8, q, (int16_t *)out, why, sizeof(why)) == RWA_VID_E_ARGUMENT);
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Panfrost's Valhall compiler
 * ----------------------------------------------------------------------------------------------------------------------------- */

static int
pan_compile(nir_shader *nir, const char *what)
{
	nir_shader *clone = nir_shader_clone(NULL, nir);
	finalize_for_gallium(clone, &(struct pipe_screen){ 0 });
	nir_shader_gather_info(clone, nir_shader_get_entrypoint(clone));
	bifrost_preprocess_nir(clone, 0x9091);
	struct panfrost_compile_inputs inputs = { .gpu_id = 0x9091 };
	struct util_dynarray binary;
	struct pan_shader_info info = { 0 };

	if (clone->info.stage == MESA_SHADER_FRAGMENT)
		inputs.valhall.use_ld_var_buf = true;
	util_dynarray_init(&binary, NULL);
	bifrost_compile_shader_nir(clone, &inputs, &binary, &info);
	printf("panfrost: %-20s %5u bytes of Valhall code\n", what, (unsigned)binary.size);
	int ok = binary.size > 0;
	util_dynarray_fini(&binary);
	ralloc_free(clone);
	ralloc_free(nir);
	return ok;
}

static void
test_panfrost(void)
{
	start("Panfrost compiles all four shaders");

	CHECK(pan_compile(rwa_vid_me16_shader(&bifrost_nir_options_v9), "me16"));
	CHECK(pan_compile(rwa_vid_half_shader(&bifrost_nir_options_v9), "half"));
	CHECK(pan_compile(rwa_vid_dct_rows_shader(&bifrost_nir_options_v9), "dct rows"));
	CHECK(pan_compile(rwa_vid_dct_cols_shader(&bifrost_nir_options_v9), "dct columns"));
}

/* ----------------------------------------------------------------------------------------------------------------------------- */

int
main(void)
{
	char why[512] = "";

	setenv("RWA_SOFTWARE", "1", 1);
	gpu = rwa_vid_create(why, sizeof(why));
	if (gpu == NULL) {
		fprintf(stderr, "FAILED: no GPU context: %s\n", why);
		printf("N2TEST WP08-GPU FAIL 0/0\n");
		return 1;
	}
	printf("device: %s, rwa_vid API %u\n", rwa_device_name(rwa_vid_device(gpu)), rwa_vid_api_version());

	test_me16_shift();
	test_me16_centres();
	test_me16_pad0();
	test_me16_wide();
	test_half();
	test_fdct_random();
	test_fdct_pattern();
	test_backend();
	test_arguments();
	test_panfrost();

	rwa_vid_destroy(gpu);
	if (tests_failed != 0) {
		printf("N2TEST WP08-GPU FAIL %d/%d\n", tests_run - tests_failed, tests_run);
		return 1;
	}
	printf("N2TEST WP08-GPU PASS %d/%d (%llu differing values)\n", tests_run, tests_run, differing_total);
	return 0;
}
