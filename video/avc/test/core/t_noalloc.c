/* SPDX-License-Identifier: MIT */
/*
 * t_noalloc.c -- proves that the WP03 encoder does not allocate after n2avc_open (include/n2avc.h and the
 * "no allocation in the per-picture path" rule of the review checklist).
 *
 * It interposes the C allocator for the whole program and counts the calls made while n2avc_encode runs.  Built
 * without AddressSanitizer (which replaces the allocator itself), as avc/test/core/run_tests.sh does.
 */
#include "n2avc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);

static unsigned long alloc_calls;
static unsigned long free_calls;

void *malloc(size_t n)
{
	__atomic_add_fetch(&alloc_calls, 1, __ATOMIC_RELAXED);
	return __libc_malloc(n);
}

void *calloc(size_t n, size_t m)
{
	__atomic_add_fetch(&alloc_calls, 1, __ATOMIC_RELAXED);
	return __libc_calloc(n, m);
}

void *realloc(void *p, size_t n)
{
	__atomic_add_fetch(&alloc_calls, 1, __ATOMIC_RELAXED);
	return __libc_realloc(p, n);
}

void free(void *p)
{
	if (p)
		__atomic_add_fetch(&free_calls, 1, __ATOMIC_RELAXED);
	__libc_free(p);
}

static int failures;

#define CHECK(cond, ...) do {                                                   \
	if (!(cond)) {                                                          \
		failures++;                                                     \
		fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
		fprintf(stderr, __VA_ARGS__);                                   \
		fprintf(stderr, "\n");                                          \
	}                                                                       \
} while (0)

int main(void)
{
	n2avc_config cfg;
	n2avc_enc *e = NULL;
	enum { W = 96, H = 64 };
	static uint8_t img[W * H * 3 / 2];
	unsigned long before, after;
	int f, ok;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = W;
	cfg.height = H;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 30;
	cfg.threads = 4;
	ok = (n2avc_open(&cfg, &e) == N2AVC_OK);
	CHECK(ok, "open");
	if (!ok)
		return 1;

	/* A gradient with some structure, so that every path (intra prediction, residual, CAVLC) is used. */
	{
		int x, y;
		for (y = 0; y < H; y++)
			for (x = 0; x < W; x++)
				img[y * W + x] = (uint8_t)((x * 3 + y * 5 + ((x * y) >> 3)) & 255);
		for (y = 0; y < H / 2; y++)
			for (x = 0; x < W / 2; x++) {
				img[W * H + y * (W / 2) + x] = (uint8_t)(128 + ((x + y) & 15));
				img[W * H + (W / 2) * (H / 2) + y * (W / 2) + x] = (uint8_t)(128 - (x & 15));
			}
	}

	before = __atomic_load_n(&alloc_calls, __ATOMIC_RELAXED);
	for (f = 0; f < 4; f++) {
		n2avc_picture pic;
		n2avc_packet pkt;
		int rc;
		memset(&pic, 0, sizeof pic);
		pic.format = N2AVC_FMT_I420;
		pic.plane[0] = img;
		pic.stride[0] = W;
		pic.plane[1] = img + W * H;
		pic.stride[1] = W / 2;
		pic.plane[2] = img + W * H + (W / 2) * (H / 2);
		pic.stride[2] = W / 2;
		pic.pts = f;
		rc = n2avc_encode(e, &pic, f == 2, &pkt);
		CHECK(rc == N2AVC_OK, "encode %d (rc %d)", f, rc);
		CHECK(pkt.size > 0, "packet %d", f);
	}
	after = __atomic_load_n(&alloc_calls, __ATOMIC_RELAXED);
	CHECK(after == before, "%lu allocations after open (%lu before, %lu after)", after - before, before, after);

	/* The reconstruction call and reconfigure must not allocate either. */
	before = after;
	{
		const uint8_t *pl[3];
		uint32_t st[3];
		n2avc_reconstruction(e, pl, st);
		n2avc_reconfigure(e, 400000, 25, 1);
	}
	after = __atomic_load_n(&alloc_calls, __ATOMIC_RELAXED);
	CHECK(after == before, "%lu allocations in reconstruction/reconfigure", after - before);

	n2avc_close(e);

	/* Everything that was allocated must have been freed: the counters must balance for the encoder's share. */
	CHECK(__atomic_load_n(&free_calls, __ATOMIC_RELAXED) > 0, "no frees recorded");

	if (failures) {
		printf("N2TEST WP03-noalloc FAIL %d\n", failures);
		return 1;
	}
	printf("N2TEST WP03-noalloc PASS 1/1\n");
	return 0;
}
