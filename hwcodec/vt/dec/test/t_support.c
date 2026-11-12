/* SPDX-License-Identifier: MIT */
/* t_support.c -- check counter and vector reader for the DEC06 host tests. */
#include "t_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int t_checks, t_failures;

void t_check(int ok, const char *expr, const char *file, int line, long got, long want)
{
	t_checks++;
	if (!ok) {
		t_failures++;
		fprintf(stderr, "FAIL %s:%d: %s (got %ld, want %ld)\n", file, line, expr, got, want);
	}
}

void t_check_u64(int ok, const char *expr, const char *file, int line, uint64_t got, uint64_t want)
{
	t_checks++;
	if (!ok) {
		t_failures++;
		fprintf(stderr, "FAIL %s:%d: %s (got %llu, want %llu)\n", file, line, expr,
		        (unsigned long long)got, (unsigned long long)want);
	}
}

void t_check_str(int ok, const char *expr, const char *file, int line, const char *got, const char *want)
{
	t_checks++;
	if (!ok) {
		t_failures++;
		fprintf(stderr, "FAIL %s:%d: %s (got %s, want %s)\n", file, line, expr, got ? got : "(null)",
		        want ? want : "(null)");
	}
}

int t_summary(const char *name)
{
	if (t_failures == 0) {
		printf("T_%s PASS %d\n", name, t_checks);
		return 0;
	}
	printf("T_%s FAIL %d of %d checks\n", name, t_failures, t_checks);
	return 1;
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int t_read_file(const char *path, uint8_t **data, size_t *bytes)
{
	FILE *f = fopen(path, "rb");
	long n;

	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	n = ftell(f);
	if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	*data = (uint8_t *)malloc((size_t)n ? (size_t)n : 1);
	if (!*data) {
		fclose(f);
		return -1;
	}
	if (fread(*data, 1, (size_t)n, f) != (size_t)n) {
		free(*data);
		*data = NULL;
		fclose(f);
		return -1;
	}
	fclose(f);
	*bytes = (size_t)n;
	return 0;
}

int t_write_file(const char *path, const void *data, size_t bytes)
{
	FILE *f = fopen(path, "wb");

	if (!f)
		return -1;
	if (fwrite(data, 1, bytes, f) != bytes) {
		fclose(f);
		return -1;
	}
	return fclose(f) == 0 ? 0 : -1;
}

int t_vector_load(const char *path, struct t_vector *out, char *err, size_t err_cap)
{
	uint8_t *d = NULL;
	size_t n = 0, at;
	uint32_t i;

	memset(out, 0, sizeof(*out));
	if (t_read_file(path, &d, &n) != 0) {
		snprintf(err, err_cap, "cannot read %s", path);
		return -1;
	}
	if (n < 20 || memcmp(d, "N2VD", 4) != 0) {
		snprintf(err, err_cap, "not a vector file");
		free(d);
		return -1;
	}
	if (rd32(d + 4) != 1) {
		snprintf(err, err_cap, "vector version %u", rd32(d + 4));
		free(d);
		return -1;
	}
	out->codec = rd32(d + 8);
	out->length_size = rd32(d + 12);
	at = 16;
	if (at + 4 > n) {
		snprintf(err, err_cap, "truncated record length");
		free(d);
		return -1;
	}
	out->record_bytes = rd32(d + at);
	at += 4;
	if (at + out->record_bytes > n) {
		snprintf(err, err_cap, "truncated record");
		free(d);
		return -1;
	}
	out->record = d + at;
	at += out->record_bytes;
	if (at + 4 > n) {
		snprintf(err, err_cap, "truncated sample count");
		free(d);
		return -1;
	}
	out->samples = rd32(d + at);
	at += 4;
	out->sample_bytes = (uint32_t *)calloc(out->samples ? out->samples : 1, sizeof(*out->sample_bytes));
	out->sample_data = (uint8_t **)calloc(out->samples ? out->samples : 1, sizeof(*out->sample_data));
	for (i = 0; i < out->samples; i++) {
		if (at + 4 > n) {
			snprintf(err, err_cap, "truncated sample %u length", i);
			t_vector_free(out);
			free(d);
			return -1;
		}
		out->sample_bytes[i] = rd32(d + at);
		at += 4;
		if (at + out->sample_bytes[i] > n) {
			snprintf(err, err_cap, "truncated sample %u", i);
			t_vector_free(out);
			free(d);
			return -1;
		}
		out->sample_data[i] = d + at;
		at += out->sample_bytes[i];
	}
	out->storage = d;
	out->storage_bytes = n;
	return 0;
}

void t_vector_free(struct t_vector *v)
{
	free(v->sample_bytes);
	free(v->sample_data);
	free(v->storage);
	memset(v, 0, sizeof(*v));
}
