/* SPDX-License-Identifier: MIT */
/*
 * t_support.h -- host-test support for package P04: check counting, file reading, the vector files produced by
 * mp4_extract.py, and the two HEVC CoreMedia calls that gpu/video's fake runtime does not provide yet (they are declared
 * in n2hwenc_vt.h and implemented test-locally here; on iOS the real CoreMedia symbols are used).
 */
#ifndef T_SUPPORT_H
#define T_SUPPORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "n2_apple.h"
#include "n2hwenc_annexb.h"

extern int t_checks;
extern int t_failures;

#define CHECK(cond) do { \
	t_checks++; \
	if (!(cond)) { \
		t_failures++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	long long a_ = (long long)(a), b_ = (long long)(b); \
	t_checks++; \
	if (a_ != b_) { \
		t_failures++; \
		fprintf(stderr, "FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, a_, b_); \
	} \
} while (0)

/* Print "T_<name> PASS <checks>" and return 0, or the failure list and return 1. */
int t_report(const char *name);

uint8_t *t_read_file(const char *path, uint32_t *bytes);

struct t_param {
	uint8_t        type;
	const uint8_t *data;
	uint32_t       bytes;
};

struct t_frame {
	uint32_t       key;
	const uint8_t *annexb;
	uint32_t       annexb_bytes;
	const uint8_t *avcc;
	uint32_t       avcc_bytes;
};

struct t_vector {
	uint32_t        codec;          /* N2HWENC_AVC / N2HWENC_HEVC */
	uint32_t        width, height;
	const uint8_t  *extradata;
	uint32_t        extradata_bytes;
	struct t_param *params;
	uint32_t        param_count;
	struct t_frame *frames;
	uint32_t        frame_count;
	uint8_t        *storage;        /* the whole file; pointers above point into it */
	uint32_t        storage_bytes;
};

int  t_vector_load(const char *path, struct t_vector *v);
void t_vector_free(struct t_vector *v);

/* The parameter sets of a vector, ready for n2hwenc_avcc_build/n2hwenc_hvcc_build. */
uint32_t t_vector_ps(const struct t_vector *v, struct n2hwenc_ps *out, uint32_t capacity);

#endif /* T_SUPPORT_H */
