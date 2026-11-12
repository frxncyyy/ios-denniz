/* SPDX-License-Identifier: MIT */
/*
 * t_link.c -- link and run check of the default client seam against P03's real vcodecd client library (no fake client
 * installed).  It is not part of the behaviour suite; its value is that the nine weak names in vt/n2hwenc_client.c are
 * resolved by client/n2vcd_client.c and client/n2vcd_transport.c, and that with no relay on the host the availability
 * probe answers 0 instead of crashing.  run_tests.sh runs it only when those client sources are present.
 */
#include <stdio.h>

#include "fake_apple.h"
#include "n2hwenc_vt.h"

int main(void)
{
	VTVideoEncoderRef enc = NULL;
	int registered;

	n2fake_vt_reset();
	registered = n2hwenc_register_all();
	printf("T_LINK registered=%d available=%d\n", registered, n2hwenc_vt_available());
	if (registered != 3)
		return 1;
	if (n2hwavc_vt_create(0x61766331u, kCFAllocatorDefault, &enc) != 0 || enc == NULL)
		return 1;
	CFRelease((CFTypeRef)enc);
	return 0;
}
