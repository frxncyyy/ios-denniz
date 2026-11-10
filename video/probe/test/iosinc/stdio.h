/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_STDIO_H
#define N2_MIN_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct __sFILE FILE;
extern FILE *__stderrp;
#define stderr __stderrp
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int fputs(const char *, FILE *);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
char *fgets(char *, int, FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int fflush(FILE *);
#define SEEK_SET 0
#define SEEK_END 2
#endif
