/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_SYS_UTSNAME_H
#define N2_MIN_SYS_UTSNAME_H
struct utsname { char sysname[256], nodename[256], release[256], version[256], machine[256]; };
int uname(struct utsname *);
#endif
