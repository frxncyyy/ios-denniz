/* SPDX-License-Identifier: MIT */
/*
 * n2vc_ioctl.h -- the one operating-system call the hardware back end and the relay transport need, plus the
 * test-only injection point used by client/test/t_client.c.
 *
 * Product builds never define N2VC_TEST_HOOKS: the setter disappears and the wrapper calls ioctl(2).  The test build
 * (-DN2VC_TEST_HOOKS) replaces every ioctl with a recording function, so requests can be compared byte for byte
 * against literals built from include/redwood_vcodec.h without a kernel device.
 */
#ifndef N2VC_IOCTL_H
#define N2VC_IOCTL_H

#include <stdint.h>

#if defined(__APPLE__)
/* Darwin's ioctl(2): the SDK stub set of gpu/video has no sys/ioctl.h (same workaround as daemon/n2vcd_srv.c). */
extern int ioctl(int fd, unsigned long request, ...);
#else
#include <sys/ioctl.h>
#endif

/* The injected call has the same result convention as ioctl(2): 0 / -1 with errno. */
typedef int (*n2vc_ioctl_fn)(void *ctx, int fd, unsigned long request, void *arg);

#if defined(N2VC_TEST_HOOKS)
void n2vc_dev_test_set_ioctl(n2vc_ioctl_fn fn, void *ctx);      /* n2vc_hw_dev.c */
void n2vcd_relay_test_set_ioctl(n2vc_ioctl_fn fn, void *ctx);   /* n2vcd_transport.c */
void n2vcd_srv_test_set_ioctl(n2vc_ioctl_fn fn, void *ctx);     /* n2vcd_server.c */
#endif

#endif
