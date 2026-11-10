/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_ERRNO_H
#define N2_MIN_ERRNO_H
int *__error(void);
#define errno (*__error())
#define EINTR 4
#define ENOENT 2
#define EEXIST 17
#endif
