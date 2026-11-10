/*
 * probe_core.h -- the probe's test runner and its small, portable helpers.  FROZEN INTERFACE (WP06 uses it too).
 *
 * A test is a plain function: it reports what it measured with probe_say() and returns 0 pass / 1 fail / 2 skipped.
 * probe_run() runs each test in a forked child so that a crash, a hang or a leaked framework state cannot take the
 * rest of the probe with it, and appends one JSON object per test to the output file.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PROBE_CORE_H
#define PROBE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct probe_ctx;

/* one result field of the running test */
void probe_say(struct probe_ctx *c, const char *key, const char *fmt, ...);

struct probe_test {
	const char *name;
	int (*fn)(struct probe_ctx *c);
	unsigned timeout_s;
};

/* fn returns 0 pass, 1 fail, 2 skipped */
int probe_run(const struct probe_test *tests, unsigned count, const char *out_path);
uint64_t probe_now_ns(void);

/* the deterministic test picture: Y = (x + 2*y + 3*frame) & 255 with a 32x32 square of value 235 at
 * (16 + 4*frame, 16 + 2*frame); for chroma sample (cx, cy): Cb = 128 + ((cx + frame) & 31), Cr = 128 - (cy & 31) */
void probe_fill_nv12(uint8_t *y, size_t ystride, uint8_t *uv, size_t uvstride, int w, int h, int frame);
double probe_psnr(const uint8_t *a, size_t astride, const uint8_t *b, size_t bstride, int w, int h);
void probe_md5(const void *data, size_t n, char hex[33]);

#ifdef __cplusplus
}
#endif
#endif
