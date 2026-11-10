/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_DIRENT_H
#define N2_MIN_DIRENT_H
#include <sys/types.h>
typedef struct __dirstream DIR;
struct dirent { unsigned long long d_ino, d_seekoff; unsigned short d_reclen, d_namlen; unsigned char d_type; char d_name[1024]; };
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
#endif
