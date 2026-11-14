/* SPDX-License-Identifier: MIT */
/* t_support.h -- shared check counter and vector reader for the DEC06 host tests. */
#ifndef N2VTD_T_SUPPORT_H
#define N2VTD_T_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int t_checks, t_failures;

void t_check(int ok, const char *expr, const char *file, int line, long got, long want);
void t_check_u64(int ok, const char *expr, const char *file, int line, uint64_t got, uint64_t want);
void t_check_str(int ok, const char *expr, const char *file, int line, const char *got, const char *want);

#define CHECK(cond) t_check((cond) ? 1 : 0, #cond, __FILE__, __LINE__, 0, 0)
#define CHECK_EQ(got, want)                                                                          \
	do {                                                                                         \
		long check_g_ = (long)(got), check_w_ = (long)(want);                                 \
		t_check(check_g_ == check_w_, #got " == " #want, __FILE__, __LINE__, check_g_, check_w_); \
	} while (0)
#define CHECK_EQ_U64(got, want)                                                                      \
	do {                                                                                         \
		uint64_t check_g_ = (uint64_t)(got), check_w_ = (uint64_t)(want);                     \
		t_check_u64(check_g_ == check_w_, #got " == " #want, __FILE__, __LINE__, check_g_,    \
		            check_w_);                                                                   \
	} while (0)
#define CHECK_STR(got, want)                                                                         \
	do {                                                                                         \
		const char *check_g_ = (got), *check_w_ = (want);                                     \
		t_check_str(check_g_ && check_w_ && strcmp(check_g_, check_w_) == 0, #got " == "      \
		            #want, __FILE__, __LINE__, check_g_, check_w_);                           \
	} while (0)

int t_summary(const char *name);          /* prints "T_<NAME> PASS n" or "FAIL ..." and returns 0/1 */

/* The ffmpeg-made vector file (see test/mkvectors.py). */
struct t_vector {
	uint32_t codec;
	uint32_t length_size;
	uint8_t *record;
	size_t record_bytes;
	uint32_t samples;
	uint32_t *sample_bytes;
	uint8_t **sample_data;
	uint8_t *storage;                /* the whole file, freed with t_vector_free */
	size_t storage_bytes;
};

int t_vector_load(const char *path, struct t_vector *out, char *err, size_t err_cap);
void t_vector_free(struct t_vector *v);

/* Read/write a whole file; returns 0 on success. */
int t_read_file(const char *path, uint8_t **data, size_t *bytes);
int t_write_file(const char *path, const void *data, size_t bytes);

#ifdef __cplusplus
}
#endif
#endif
