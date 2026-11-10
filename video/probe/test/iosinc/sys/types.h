/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_SYS_TYPES_H
#define N2_MIN_SYS_TYPES_H
#include <stddef.h>
typedef long ssize_t;
typedef int pid_t;
typedef unsigned int mode_t;
#endif
