/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_STDLIB_H
#define N2_MIN_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
char *getenv(const char *);
unsigned long strtoul(const char *, char **, int);
void abort(void) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));
#endif
