/* SPDX-License-Identifier: MIT */
/*
 * p03_absent.c -- test-only definition of the one P03 symbol vcodecd references when it is linked without
 * daemon/test/fake_vhost.c (the IP-REAL binary, which links IV's real library instead).
 *
 * P03 owns client/n2vc_hw_dev.c; that file is not in the tree yet, so nothing in this repository can open
 * /dev/redwood-vcodec as a client.  The real-hardware test binary is started with --fake anyway; this
 * definition makes an accidental real open fail loudly with ENODEV instead of reaching a device.
 */
#include <errno.h>

#include "n2vc_hw.h"

int
n2vc_hw_open_dev(struct n2vc_hw *out, const char *dev_path)
{
	(void)out;
	(void)dev_path;
	return -ENODEV;
}
