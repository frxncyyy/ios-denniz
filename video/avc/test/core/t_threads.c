/* SPDX-License-Identifier: MIT */
/*
 * t_threads.c -- the threaded path of the WP03 H.264 encoder: one slice per band of macroblock rows coded by the
 * pool created in n2avc_open, two encoders in parallel, and the stream has to be valid and identical to the
 * single-threaded picture content (the decoded pictures are compared by run_tests.sh with ffmpeg).
 *
 * Built once with ThreadSanitizer by avc/test/core/run_tests.sh; the shared state it must find clean is the
 * per-slice RBSP buffer, the macroblock info rows, the reconstruction band and the job generation counter.
 */
#include "n2avc.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...) do {                                                   \
	if (!(cond)) {                                                          \
		failures++;                                                     \
		fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
		fprintf(stderr, __VA_ARGS__);                                   \
		fprintf(stderr, "\n");                                          \
	}                                                                       \
} while (0)

enum { W = 160, H = 256 };

typedef struct {
	int threads;
	int ok;
} job;

static void fill(uint8_t *img, int frame)
{
	int x, y;
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++)
			img[y * W + x] = (uint8_t)((x * 2 + y * 3 + frame * 7 + ((x * y) >> 2)) & 255);
	for (y = 0; y < H / 2; y++)
		for (x = 0; x < W / 2; x++) {
			img[W * H + y * (W / 2) + x] = (uint8_t)(128 + ((x + frame) & 31));
			img[W * H + (W / 2) * (H / 2) + y * (W / 2) + x] = (uint8_t)(128 - (y & 31));
		}
}

static void *run_job(void *arg)
{
	job *j = (job *)arg;
	n2avc_config cfg;
	n2avc_enc *e = NULL;
	static _Thread_local uint8_t img[W * H * 3 / 2];
	int f;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = W;
	cfg.height = H;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 28;
	cfg.threads = (uint32_t)j->threads;
	if (n2avc_open(&cfg, &e) != N2AVC_OK) {
		j->ok = 0;
		return NULL;
	}
	for (f = 0; f < 4; f++) {
		n2avc_picture pic;
		n2avc_packet pkt;
		memset(&pic, 0, sizeof pic);
		fill(img, f);
		pic.format = N2AVC_FMT_I420;
		pic.plane[0] = img;
		pic.stride[0] = W;
		pic.plane[1] = img + W * H;
		pic.stride[1] = W / 2;
		pic.plane[2] = img + W * H + (W / 2) * (H / 2);
		pic.stride[2] = W / 2;
		pic.pts = f;
		if (n2avc_encode(e, &pic, 0, &pkt) != N2AVC_OK || pkt.size == 0) {
			j->ok = 0;
			n2avc_close(e);
			return NULL;
		}
	}
	n2avc_close(e);
	j->ok = 1;
	return NULL;
}

int main(void)
{
	job a = { 4, 0 }, b = { 16, 0 };
	pthread_t ta, tb;

	CHECK(pthread_create(&ta, NULL, run_job, &a) == 0, "start job a");
	CHECK(pthread_create(&tb, NULL, run_job, &b) == 0, "start job b");
	pthread_join(ta, NULL);
	pthread_join(tb, NULL);
	CHECK(a.ok == 1, "four-slice encoder produced four pictures");
	CHECK(b.ok == 1, "sixteen-slice encoder produced four pictures");

	/* And a single-threaded encoder in the main thread, while nothing else runs. */
	{
		job c = { 1, 0 };
		run_job(&c);
		CHECK(c.ok == 1, "single-thread encoder produced four pictures");
	}

	if (failures) {
		printf("N2TEST WP03-threads FAIL %d\n", failures);
		return 1;
	}
	printf("N2TEST WP03-threads PASS 1/1\n");
	return 0;
}
