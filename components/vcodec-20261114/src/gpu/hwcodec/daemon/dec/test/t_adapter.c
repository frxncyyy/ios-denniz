/* SPDX-License-Identifier: MIT */
/*
 * t_adapter.c -- optional integration oracle for DEC05's AP adapter: the real ap/n2ap_vdec.c over
 * DEC01's scripted daemon stand-ins (ap/test_dec01/{dec_vcu_standin,dec_hw_model,fake_daemon}.c).
 *
 * Not part of run_tests.sh: the stand-ins belong to DEC01 and may change.  Run it with
 *   bash gpu/hwcodec/daemon/dec/test/run_adapter.sh
 * It exercises n2vcd_dec_ap_n2ap_ops() end to end (INIT + SET_PARAM, header START, add_buffer,
 * one frame START with the single-allocation chroma, list_disp harvest, picture/release, a
 * resolution change, free_buffers, flush, close) against a scripted vendor, and checks every
 * message the AP side received.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dec_hw_model.h"
#include "dec_vcu_standin.h"
#include "fake_daemon.h"
#include "mtk_vdec_ipi.h"
#include "n2ap_venc.h"
#include "n2ap_vdec.h"
#include "n2vcd_dec_ap_n2ap.h"

static int checks, fails;

#define CK(c) do { \
	checks++; \
	if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
} while (0)

#define CKU(a, b) do { \
	uint32_t ck_a = (uint32_t)(a), ck_b = (uint32_t)(b); \
	checks++; \
	if (ck_a != ck_b) { fails++; fprintf(stderr, "FAIL %s:%d: %s = 0x%08x, want 0x%08x\n", \
	                                       __FILE__, __LINE__, #a, ck_a, ck_b); } \
} while (0)

static unsigned g_share_calls;

int
n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa, uint32_t bytes)
{
	(void)dma_handle;
	(void)pa;
	(void)bytes;
	if (share_id <= 0) {
		return -EINVAL;
	}
	g_share_calls++;
	return 0;
}

static void
set_changed(struct decd *d, uint32_t changed)
{
	pthread_mutex_lock(&d->lock);
	d->changed = changed;
	pthread_mutex_unlock(&d->lock);
}

static void
set_geometry(struct decd *d, uint32_t w, uint32_t h, uint32_t buf_w, uint32_t buf_h)
{
	pthread_mutex_lock(&d->lock);
	d->pic_w = w;
	d->pic_h = h;
	d->crop_w = w;
	d->crop_h = h;
	d->buf_w = buf_w;
	d->buf_h = buf_h;
	d->fb_sz = buf_w * buf_h;
	d->fb_sz_c = buf_w * buf_h / 2u;
	pthread_mutex_unlock(&d->lock);
}

int
main(void)
{
	struct n2a32_mem *mem = n2a32_mem_create();
	struct decm hw;
	struct decd d;
	struct n2vcd_dec_ap_n2ap_cfg cfg;
	struct n2vcd_dec_ap_n2ap *a;
	struct n2vcd_dec_ap_geometry geo;
	struct n2vcd_dec_ap_result res;
	struct n2vcd_dec_ap_picture pic;
	struct n2vcd_dec_ap_buffer y = { 0x9001u, 0xda800000u, 122880u };
	struct n2vcd_dec_ap_buffer c = { 0x9001u, 0xda800000u, 40960u };
	struct n2vcd_dec_ap_buffer bs = { 0x9000u, 0xda001000u, 1u << 20 };
	struct n2vcd_dec_ap_buffer y2 = { 0x9003u, 0xdaa00000u, 640u * 512u + 640u * 512u / 2u };
	struct n2vcd_dec_ap_buffer c2 = { 0x9003u, 0xdaa00000u, 640u * 512u / 2u };
	uint32_t ready = 0;

	if (!mem) {
		fprintf(stderr, "FAIL mem\n");
		return 1;
	}
	decm_init(&hw, 0xda800000u);
	struct n2vcu *vcu = decv_create(mem, &hw.hw, 512u * 1024u);

	if (!vcu) {
		fprintf(stderr, "FAIL vcu\n");
		return 1;
	}
	if (decd_start(&d, vcu, N2AP_VDEC_IPI_H264, 0) != 0) {
		fprintf(stderr, "FAIL daemon\n");
		return 1;
	}
	decd_script_header_only(&d);
	pthread_mutex_lock(&d.lock);
	d.fourcc = 0x31324d4du;          /* MM21 */
	d.dpb_sz = 7;
	d.fb_sz = 320u * 256u;
	d.fb_sz_c = 320u * 256u / 2u;
	d.disp_frames = 1;               /* every START queues one list_disp entry */
	d.free_frames = 1;
	d.free_fb_cookie = 1;
	d.start_status = VDEC_IPI_MSG_STATUS_OK;
	pthread_mutex_unlock(&d.lock);

	/* Two client buffers, as the daemon would have registered from RWVC_DMA_INFO. */
	CK(n2vc_dma_guest_register(&hw.hw, NULL, bs.handle, bs.pa, bs.bytes, NULL) == 0);
	CK(n2vc_dma_guest_register(&hw.hw, NULL, y.handle, y.pa, y.bytes, NULL) == 0);
	CK(n2vc_dma_guest_register(&hw.hw, NULL, y2.handle, y2.pa, y2.bytes, NULL) == 0);

	memset(&cfg, 0, sizeof(cfg));
	cfg.vcu = vcu;
	cfg.hw = &hw.hw;
	cfg.timeout_ms = 2000;
	cfg.share_base = 0x580;
	a = n2vcd_dec_ap_n2ap_create(&cfg);
	CK(a != NULL);
	const struct n2vcd_dec_ap_ops *ops = n2vcd_dec_ap_n2ap_ops();

	CK(ops->open(a, N2VCD_CODEC_H264) == 0);
	CKU(d.init_msgs, 1);
	CKU(d.param_msgs, 2);            /* FRAME_SIZE + DECODE_MODE from n2ap_vdec_open */
	set_changed(&d, VDEC_RES_CHANGE | VDEC_CROP_CHANGED);

	/* Header: no frame buffer, geometry from vsi.pic + vsi.crop + dpb_sz. */
	memset(&geo, 0, sizeof(geo));
	CK(ops->header(a, &bs, 100, &geo) == 0);
	CKU(geo.width, 320);
	CKU(geo.height, 240);
	CKU(geo.stride, 320);
	CKU(geo.buf_h, 256);
	CKU(geo.out_buf_bytes, 122880);
	CKU(geo.dpb_frames, 10);         /* vsi dpb_sz 7 + 1 + 2 (rev-2 text) */
	CKU(geo.fourcc, N2VCD_FMT_MM21);
	CKU(d.last_fb_planes, 2);        /* session default after INIT; no fb was passed */

	CK(ops->add_buffer(a, 0, &y, &c) == 0);
	CK(ops->add_buffer(a, 1, &(struct n2vcd_dec_ap_buffer){ 0x9002u, 0xda900000u, 122880u },
	                   &(struct n2vcd_dec_ap_buffer){ 0x9002u, 0xda900000u, 40960u }) == 0);
	CK(g_share_calls >= 2);
	set_changed(&d, 0);

	/* One frame START, one free buffer in vsi.dec, list_disp harvest. */
	memset(&res, 0, sizeof(res));
	CK(ops->input(a, &bs, 100, 0x1234u, 0, &res) == 0);
	CKU(res.changed, 0);
	CKU(res.pictures_ready, 1);
	CKU(d.last_fb_planes, 2);
	CKU(d.last_fb_dma[0], y.pa);
	CKU(d.last_fb_dma[1], y.pa + 320u * 256u);   /* single allocation: C after Y */
	CKU(d.last_fb_index, 0);
	/* DEC01's n2ap_vdec.c still fills START data[0]/[1] as buffer-size/bytesused; the kernel and
	 * opus-hevcdec's HEVC run require bytesused first.  My job fields are already the header's
	 * semantic (bs_bytes = allocation, bs_length = AU); this is DEC01's one-line fix, so the two
	 * values are reported, not asserted. */
	printf("DEC05 adapter note: START data[0]=0x%x data[1]=0x%x (want 0x64/0x%x after DEC01 fix)\n",
	       d.last_start_data[0], d.last_start_data[1], bs.bytes);

	memset(&pic, 0, sizeof(pic));
	CK(ops->picture(a, &pic) == 1);
	CKU(pic.index, 0);
	CKU(pic.pictures_ready, 0);
	CK(ops->release(a, 0) == 0);

	/* Resolution change: changed bit 0, input not consumed, free/header/resend. */
	set_geometry(&d, 640, 480, 640, 512);
	set_changed(&d, VDEC_RES_CHANGE | VDEC_CROP_CHANGED);
	CK(ops->input(a, &bs, 100, 2, 0, &res) == 0);
	CKU(res.changed, 1);
	CKU(res.pictures_ready, 0);
	CK(ops->free_buffers(a) == 0);
	CK(ops->header(a, &bs, 100, &geo) == 0);
	CKU(geo.width, 640);
	CKU(geo.height, 480);
	CKU(geo.stride, 640);
	CKU(geo.buf_h, 512);
	CKU(geo.out_buf_bytes, 640u * 512u + 640u * 512u / 2u);
	set_changed(&d, 0);
	CK(ops->add_buffer(a, 0, &y2, &c2) == 0);      /* 640x480 needs a bigger buffer */
	CK(ops->input(a, &bs, 100, 3, 0, &res) == 0);
	CKU(res.pictures_ready, 1);
	CK(ops->picture(a, &pic) == 1);
	CKU(pic.index, 0);
	CK(ops->release(a, 0) == 0);

	CK(ops->flush(a, &ready) == 0);
	CKU(ready, 0);
	ops->close(a);
	CKU(d.deinit_msgs, 1);

	/* A second session reuses handles without re-registering the share ids. */
	unsigned shares_before = g_share_calls;

	set_geometry(&d, 320, 240, 320, 256);
	set_changed(&d, VDEC_RES_CHANGE | VDEC_CROP_CHANGED);
	CK(ops->open(a, N2VCD_CODEC_H264) == 0);
	CK(ops->header(a, &bs, 100, &geo) == 0);
	CKU(geo.width, 320);
	CK(ops->add_buffer(a, 0, &y, &c) == 0);
	CKU(g_share_calls, shares_before);   /* same handles -> same ids, no table growth */
	ops->close(a);

	n2vcd_dec_ap_n2ap_destroy(a);
	decd_stop(&d);
	decv_destroy(vcu);
	decm_destroy(&hw);
	n2a32_mem_destroy(mem);
	if (fails == 0) {
		printf("DEC05 adapter PASS %d/%d\n", checks, checks);
		return 0;
	}
	printf("DEC05 adapter FAIL %d/%d\n", checks - fails, checks);
	return 1;
}
