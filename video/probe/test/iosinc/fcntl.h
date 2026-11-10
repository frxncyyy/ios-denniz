/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_FCNTL_H
#define N2_MIN_FCNTL_H
#include <sys/types.h>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0x200
#define O_APPEND 0x8
#define O_NONBLOCK 0x4
#define O_NOCTTY 0x20000
int open(const char *, int, ...);
#endif
