/* SPDX-License-Identifier: MIT */
/* fake_dev.h -- host-test fake /dev/redwood-jpeg (test_j02 only).  Not part of the library or the tool. */
#ifndef N2J02_FAKE_DEV_H
#define N2J02_FAKE_DEV_H

#include <stdint.h>

#include "rwjpeg.h"

struct fake_dev {
	int              fail_info;      /* RWJ_INFO: -1 ENODEV */
	int              fail_eio;       /* RWJ_ENCODE: -1 EIO with out_status = RWJ_E_TIMEOUT */
	int              fail_enospc;    /* RWJ_ENCODE: -1 ENOSPC with out_status = RWJ_E_SIZE */
	int              fail_noeoi;     /* RWJ_ENCODE: return the JPEG without its EOI */
	int              pad;            /* append this many 0xA5 bytes after EOI; out_bytes includes them */
	struct rwj_encode last;          /* the last RWJ_ENCODE request as received */
	int              have_last;
	uint64_t         jobs;
	uint64_t         jobs_failed;
};

extern struct fake_dev n2fake_dev;

int  fake_ioctl(void *ctx, unsigned long request, void *arg);
int  n2rwj_test_open(rwjpeg **out);
void fake_reset(void);
const char *fake_scratch(void);

#endif
