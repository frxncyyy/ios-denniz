/* SPDX-License-Identifier: MIT */
/*
 * stub_inter.c -- the WP04 half of include/n2avc_int.h, for the WP03 test suite only.
 *
 * WP04 (inter prediction, motion search, rate control) is a separate package.  Until its files are linked in,
 * this stub provides the four missing objects so that n2avc_enc.c can be tested:
 *
 *   n2avc_inter_slice_begin()  returns a non-NULL marker: there is no per-slice scratch
 *   n2avc_inter_slice_end()    does nothing
 *   n2avc_p_mb()               always returns 2 ("intra is cheaper"), so every macroblock of a P slice is coded
 *                              intra.  That is a legal P slice and it exercises the whole P plumbing: slice_type 5,
 *                              mb_skip_run before every macroblock, the +5 macroblock type offset, and the
 *                              dec_ref_pic_marking syntax.  It is NOT a test of WP04's code.
 *   n2avc_rc_*                 constant QP: the configured qp, or 26.
 *
 * This file never allocates after n2avc_rc_open(), and it never allocates per picture.
 */
#include "n2avc_int.h"

#include <stdlib.h>

struct n2avc_rc {
	int qp;
	uint32_t bitrate;
	uint32_t fps_num, fps_den;
};

void *n2avc_inter_slice_begin(n2avc_slice *s)
{
	(void)s;
	return (void *)1;                       /* not NULL: the slice may call n2avc_p_mb() */
}

void n2avc_inter_slice_end(n2avc_slice *s)
{
	(void)s;
}

int n2avc_p_mb(n2avc_slice *s, int mbx, int mby)
{
	(void)s;
	(void)mbx;
	(void)mby;
	return 2;                               /* "intra is cheaper": WP03 codes the macroblock intra */
}

n2avc_rc *n2avc_rc_open(const n2avc_config *cfg)
{
	n2avc_rc *rc = (n2avc_rc *)malloc(sizeof *rc);
	if (!rc)
		return NULL;
	rc->qp = cfg->qp ? (int)cfg->qp : 26;
	rc->bitrate = cfg->bitrate;
	rc->fps_num = cfg->fps_num;
	rc->fps_den = cfg->fps_den;
	return rc;
}

int n2avc_rc_picture_qp(n2avc_rc *rc, int idr)
{
	(void)idr;
	return rc->qp;
}

void n2avc_rc_picture_done(n2avc_rc *rc, int idr, int qp, size_t bytes)
{
	(void)rc;
	(void)idr;
	(void)qp;
	(void)bytes;
}

void n2avc_rc_reconfigure(n2avc_rc *rc, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den)
{
	rc->bitrate = bitrate;
	rc->fps_num = fps_num;
	rc->fps_den = fps_den;
}

void n2avc_rc_close(n2avc_rc *rc)
{
	free(rc);
}
