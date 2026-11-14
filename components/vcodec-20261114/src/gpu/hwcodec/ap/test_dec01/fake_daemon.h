/* SPDX-License-Identifier: MIT */
/*
 * fake_daemon.h -- the VCU side of the decoder protocol, scripted, on top of dec_vcu_standin.h.
 *
 * The product's daemon is the interpreted vendor library; a unit test cannot use it to check "does the
 * AP client answer LOCK_LAT with power plus a racing restore".  This fake daemon plays the same role
 * with the exact message ids and layouts of mtk_vdec_ipi.h, and the test scripts what it does during
 * one START.
 *
 * It runs on its own thread (the real daemon's receiver/service threads are in the vendor library;
 * here one thread serves every IPI id).  The AP handler is called from this thread, which is what
 * vcu_dec_ipi_handler()'s execution context is on Android.
 *
 * Owner: DEC01.  Test-only.
 */
#ifndef FAKE_DAEMON_H
#define FAKE_DAEMON_H
#include <pthread.h>
#include <stdint.h>
#include "dec_vcu_standin.h"

struct decd {
	struct n2vcu *vcu;
	int ipi;                      /* IPI_VDEC_H264 (2) or H265 (3) */
	pthread_t th;
	pthread_mutex_t lock;
	volatile int stop;
	int started;

	/* The daemon's own view of the vsi (it allocates it, like vpud's mem_inst[0]). */
	uint32_t vsi_guest;
	uint64_t ap_cookie;           /* the AP instance token from INIT, echoed in every reply */
	struct vdec_vsi *vsi;
	unsigned ring_entries;        /* entry count of the vsi it handed out (64 unless scripted) */

	/* script: the replies */
	int32_t init_status;
	int32_t cap_status;
	int32_t param_status;
	int32_t start_status;
	int32_t reset_status;
	int32_t end_status;
	int32_t deinit_status;
	/* script: what happens inside one START, in this order */
	unsigned do_lock_lat;         /* LOCK_LAT before the LAT interrupt */
	unsigned do_wait_lat;         /* WAITISR with status = MTK_VDEC_LAT */
	unsigned do_unlock_lat;
	unsigned do_lock_core;
	unsigned do_wait_core;        /* WAITISR with status = MTK_VDEC_CORE */
	unsigned do_unlock_core;
	unsigned do_get_fb;           /* GET_FRAME_BUFFER */
	unsigned do_put_fb;           /* PUT_FRAME_BUFFER (after the fake filled list_disp) */
	unsigned disp_frames;         /* frames queued into list_disp (with or without PUT_FRAME_BUFFER) */
	unsigned free_frames;         /* buffers queued into list_free ... */
	unsigned free_fb_cookie;      /* 1: the first list_free entry is the job's frame buffer */
	/* the picture the daemon "parses" out of the bitstream, written into the vsi on START */
	uint32_t pic_w, pic_h, buf_w, buf_h, fb_sz, fb_sz_c, dpb_sz, changed, fourcc;
	uint32_t crop_l, crop_t, crop_w, crop_h;
	/* counters */
	unsigned init_msgs, cap_msgs, param_msgs, start_msgs, reset_msgs, end_msgs, deinit_msgs;
	unsigned short_msgs, unknown_msgs;
	uint32_t last_start_data[3];
	uint32_t frame_size[2];
	unsigned frame_size_seen;
	uint32_t last_param_id;
	uint32_t last_param_data[8];
	unsigned last_param_count;
	uint32_t last_cap_id;
	uint32_t last_deinit_inst;
	int32_t last_wait_seen;       /* the core id of the last WAITISR the daemon sent */
	uint32_t last_fb_index;       /* what the daemon read back from vsi.dec after GET_FRAME_BUFFER */
	uint32_t last_fb_planes;
	uint64_t last_fb_va;
	uint64_t last_fb_dma[2];
	uint64_t last_fb_fd[2];
	/* the vendor pushes the consumed bitstream cookie into list_free_bs once per START */
	uint64_t last_bs_cookie;
	unsigned bs_pushes;
};

/* Start the fake daemon.  `ring_entries` 0 = 64 (the kernel's VIDEO_MAX_FRAME).  Returns 0/-errno. */
int decd_start(struct decd *d, struct n2vcu *vcu, int ipi, unsigned ring_entries);
void decd_stop(struct decd *d);

/* Script helpers. */
void decd_script_full_frame(struct decd *d);         /* LAT+CORE locks, both WAITISRs, GET/PUT_FB */
void decd_script_header_only(struct decd *d);        /* no locks at all: a header decode */
/* Queue n display entries (the cookie of the last START) without sending a request; a following
 * VCU_IPIMSG_DEC_PUT_FRAME_BUFFER from the test harvests them, or the AP-side harvest does. */
void decd_queue_disp(struct decd *d, unsigned n);

#endif /* FAKE_DAEMON_H */
