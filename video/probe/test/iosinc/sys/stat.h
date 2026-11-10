/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_SYS_STAT_H
#define N2_MIN_SYS_STAT_H
#include <sys/types.h>
int mkdir(const char *, mode_t);
#endif
