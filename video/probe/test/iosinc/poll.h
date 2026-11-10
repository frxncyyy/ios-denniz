/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_POLL_H
#define N2_MIN_POLL_H
struct pollfd { int fd; short events; short revents; };
#define POLLIN 1
#define POLLHUP 0x10
int poll(struct pollfd *, unsigned, int);
#endif
