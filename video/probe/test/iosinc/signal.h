/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_SIGNAL_H
#define N2_MIN_SIGNAL_H
typedef void (*__sighandler_t)(int);
__sighandler_t signal(int, __sighandler_t);
int raise(int);
int kill(int, int);
#define SIGKILL 9
#define SIGPIPE 13
#define SIG_IGN ((__sighandler_t)1)
#endif
