/* SPDX-License-Identifier: MIT */
/* probe_kernel.c -- the *kernel's* venc_ipi_msg.h compiled for the host, printed in the same
 * format as probe_mine.c.  Built with -D__KERNEL__ and a two-file stub for linux/compiler.h so the
 * UAPI headers load outside a kernel build; the structs themselves are the kernel's. */
#include "venc_ipi_msg.h"
#include "probe_body.h"

int main(void)
{
	return probe_all();
}
