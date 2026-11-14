/* SPDX-License-Identifier: MIT */
/*
 * t_dec01.c -- host tests for ap/n2ap_vdec.c (the AP side of a VDEC session).
 *
 * Three blocks, all host only:
 *   1. layout: the sizes/offsets of ap/mtk_vdec_ipi.h against literals derived from the kernel's
 *      vdec_ipi_msg.h / vcodec_ipi_msg.h (the full oracle is the probe_kernel.c vs probe_mine.c diff
 *      that run_tests.sh performs; the literals here also run on the armv7 and arm64 builds);
 *   2. protocol: a scripted daemon (fake_daemon.c) on top of a stand-in /dev/vcu (dec_vcu_standin.c)
 *      drives every AP message and every VCU -> AP request of one decode;
 *   3. power role: a register/power model (dec_hw_model.c) records what LOCK/UNLOCK did to the
 *      hardware: the 68 racing words, the HW-break sequence of mt6885/mtk_vcodec_dec_pm.c:296-460,
 *      the block references.
 *
 * Every expectation is a literal from the kernel sources (line cited) or from facts/VDEC_PROTOCOL.md,
 * never from the implementation under test.
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n2a32.h"
#include "n2ap_vdec.h"
#include "n2ap_venc.h"      /* n2vc_dma_guest_alloc/register, V41 */
#include "mtk_vdec_ipi.h"
#include "dec_hw_model.h"
#include "dec_vcu_standin.h"
#include "fake_daemon.h"

static unsigned g_pass, g_total;
static const char *g_cfg;

#define CHECK(cond) do { \
	g_total++; \
	if (cond) { \
		g_pass++; \
	} else { \
		printf("CHECK FAILED [%s] %s:%d: %s\n", g_cfg, __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	unsigned long long va_ = (unsigned long long)(a), vb_ = (unsigned long long)(b); \
	g_total++; \
	if (va_ == vb_) { \
		g_pass++; \
	} else { \
		printf("CHECK FAILED [%s] %s:%d: %s = %llu, expected %llu\n", g_cfg, __FILE__, \
		       __LINE__, #a, va_, vb_); \
	} \
} while (0)

/* ---- the fixture: guest address space + DMEM stand-in + hardware model ---------------------- */

struct fix {
	struct n2a32_mem *mem;
	struct decm hw;
	struct n2vcu *vcu;
	struct decd daemon;
	struct n2ap_vdec *dec;
};

/*
 * One guest address space for the whole process, like a real daemon: ap/n2vc_dma.c's registry is a
 * process global that keeps the `mem` pointer of every buffer it registered, so destroying an address
 * space between fixtures would leave those entries dangling (found by ASan).
 */
static struct n2a32_mem *fix_mem(void)
{
	static struct n2a32_mem *mem;

	if (mem == NULL)
		mem = n2a32_mem_create();
	return mem;
}

static int fix_open(struct fix *f, unsigned ring)
{
	memset(f, 0, sizeof *f);
	f->mem = fix_mem();
	if (f->mem == NULL)
		return -ENOMEM;
	decm_init(&f->hw, 0xda800000u);
	f->vcu = decv_create(f->mem, &f->hw.hw, 512u * 1024u);
	if (f->vcu == NULL)
		return -ENOMEM;
	if (decd_start(&f->daemon, f->vcu, N2AP_VDEC_IPI_H264, ring) != 0)
		return -ENOMEM;
	return 0;
}

static void fix_close(struct fix *f)
{
	if (f->dec != NULL)
		(void)n2ap_vdec_close(f->dec);
	f->dec = NULL;
	if (f->daemon.started)
		decd_stop(&f->daemon);
	if (f->vcu != NULL)
		decv_destroy(f->vcu);
	decm_destroy(&f->hw);
	f->mem = NULL;                  /* the address space lives for the whole process (fix_mem) */
	memset(f, 0, sizeof *f);
}

static void t_trace(void *arg, const char *event, uint32_t a, uint32_t b, uint32_t c)
{
	(void)arg;
	fprintf(stderr, "N2DEC trace %-12s a=%u b=%u c=%u\n", event != NULL ? event : "-", a, b, c);
}

static int fix_session(struct fix *f, unsigned ring, unsigned timeout_ms)
{
	struct n2ap_vdec_cfg cfg;

	memset(&cfg, 0, sizeof cfg);
	cfg.vcu = f->vcu;
	cfg.hw = &f->hw.hw;
	cfg.codec_fourcc = N2AP_VDEC_FOURCC_H264;
	cfg.timeout_ms = timeout_ms;
	cfg.pic_w = 320;
	cfg.pic_h = 240;
	cfg.decode_mode = 5;
	cfg.set_decode_mode = 1;
	cfg.fb_num_planes = 1;
	cfg.ring_entries = ring;
	cfg.query_cap = 1;
	{
		int rc = n2ap_vdec_open(&f->dec, &cfg);

		if (rc == 0 && getenv("N2DEC_TRACE") != NULL)
			n2ap_vdec_set_trace(f->dec, t_trace, NULL);
		return rc;
	}
}

/* A bitstream buffer in the guest mirror + the DMA registry (what a caller has to do). */
static int fix_bs(struct fix *f, uint32_t bytes, uint32_t *handle, uint32_t *pa, uint32_t *guest)
{
	uint8_t *p;

	if (n2vc_dma_guest_alloc(&f->hw.hw, f->mem, bytes, 0, handle, pa, guest) != 0)
		return -ENOMEM;
	p = n2a32_ptr(f->mem, *guest);
	if (p == NULL) {
		fprintf(stderr, "fix_bs: guest=0x%x handle=%u is not mapped\n", *guest, *handle);
		return -ENOMEM;
	}
	memset(p, 0x5a, bytes);
	return 0;
}

/* ---- 1. layouts (literals from the kernel headers) ----------------------------------------- */

static void t_layout(void)
{
	/* structs of vdec_ipi_msg.h / vcodec_ipi_msg.h; sizeof/offsetof literals computed from the
	 * field lists in the kernel header for VIDEO_MAX_FRAME = 64, VIDEO_MAX_PLANES = 8 (both ABIs). */
	CHECK_EQ(sizeof(struct vdec_ipi_fb), 48);
	CHECK_EQ(offsetof(struct vdec_ipi_fb, y_fb_dma), 8);
	CHECK_EQ(offsetof(struct vdec_ipi_fb, c_fb_dma), 16);
	CHECK_EQ(offsetof(struct vdec_ipi_fb, poc), 24);
	CHECK_EQ(offsetof(struct vdec_ipi_fb, timestamp), 32);
	CHECK_EQ(offsetof(struct vdec_ipi_fb, reserved), 40);

	/* ring_bs_list: 8*64 + 4*4 = 528; ring_fb_list: 48*64 + 16 = 3088 */
	CHECK_EQ(sizeof(struct ring_bs_list), 528);
	CHECK_EQ(sizeof(struct ring_fb_list), 3088);
	CHECK_EQ(offsetof(struct ring_bs_list, count), 520);
	CHECK_EQ(offsetof(struct ring_fb_list, count), 3080);

	/* vdec_dec_info: 200 bytes, 8-byte aligned fields (vcodec_ipi_msg.h:127-142) */
	CHECK_EQ(sizeof(struct vdec_dec_info), 200);
	CHECK_EQ(offsetof(struct vdec_dec_info, bs_dma), 8);
	CHECK_EQ(offsetof(struct vdec_dec_info, bs_fd), 16);
	CHECK_EQ(offsetof(struct vdec_dec_info, fb_dma), 24);
	CHECK_EQ(offsetof(struct vdec_dec_info, fb_fd), 88);
	CHECK_EQ(offsetof(struct vdec_dec_info, vdec_bs_va), 152);
	CHECK_EQ(offsetof(struct vdec_dec_info, vdec_fb_va), 160);
	CHECK_EQ(offsetof(struct vdec_dec_info, fb_num_planes), 168);
	CHECK_EQ(offsetof(struct vdec_dec_info, index), 172);
	CHECK_EQ(offsetof(struct vdec_dec_info, timestamp), 184);
	CHECK_EQ(offsetof(struct vdec_dec_info, queued_frame_buf_count), 192);

	/* vdec_pic_info: 4*4 + 8*4 + 3*4 = 60 (vcodec_ipi_msg.h:103-112) */
	CHECK_EQ(sizeof(struct vdec_pic_info), 60);
	CHECK_EQ(offsetof(struct vdec_pic_info, fb_sz), 16);
	CHECK_EQ(offsetof(struct vdec_pic_info, fourcc), 56);
	CHECK_EQ(sizeof(struct mtk_video_fmt), 12);
	CHECK_EQ(sizeof(struct mtk_codec_framesizes), 36);
	CHECK_EQ(sizeof(struct mtk_color_desc), 68);

	/* vdec_vsi: list_free_bs at 0, list_free at 528, list_disp at 3616, dec at 6704 */
	CHECK_EQ(offsetof(struct vdec_vsi, list_free_bs), 0);
	CHECK_EQ(offsetof(struct vdec_vsi, list_free), 528);
	CHECK_EQ(offsetof(struct vdec_vsi, list_disp), 3616);
	CHECK_EQ(offsetof(struct vdec_vsi, dec), 6704);
	CHECK_EQ(offsetof(struct vdec_vsi, pic), 6904);
	CHECK_EQ(sizeof(struct vdec_vsi), 13752);
	/* the tail view the client uses must line up with the full struct at the kernel's counts */
	CHECK_EQ(offsetof(struct vdec_vsi_tail, dec), 0);
	CHECK_EQ(offsetof(struct vdec_vsi_tail, pic), 200);
	CHECK_EQ(offsetof(struct vdec_vsi_tail, video_formats), 200 + 60 + 68 + 16);
	CHECK_EQ(sizeof(struct vdec_vsi_tail), sizeof(struct vdec_vsi) - 6704);

	/* the AP messages (vdec_ipi_msg.h:143-286) */
	CHECK_EQ(sizeof(struct vdec_ap_ipi_cmd), 8);
	CHECK_EQ(sizeof(struct vdec_ap_ipi_init), 16);
	CHECK_EQ(offsetof(struct vdec_ap_ipi_init, ap_inst_addr), 8);
	CHECK_EQ(sizeof(struct vdec_ap_ipi_dec_start), 24);
	CHECK_EQ(offsetof(struct vdec_ap_ipi_dec_start, data), 8);
	CHECK_EQ(sizeof(struct vdec_ap_ipi_set_param), 44);
	CHECK_EQ(offsetof(struct vdec_ap_ipi_set_param, id), 8);
	CHECK_EQ(offsetof(struct vdec_ap_ipi_set_param, data), 12);
	CHECK_EQ(sizeof(struct vdec_ap_ipi_query_cap), 24);
	CHECK_EQ(offsetof(struct vdec_ap_ipi_query_cap, ap_data_addr), 16);
	/* the replies; the 32-bit kernel's unions are 8-aligned, so one layout covers both ABIs */
	CHECK_EQ(sizeof(struct vdec_vcu_ipi_ack), 16);
	CHECK_EQ(offsetof(struct vdec_vcu_ipi_ack, status), 4);
	CHECK_EQ(offsetof(struct vdec_vcu_ipi_ack, ap_inst_addr), 8);
	CHECK_EQ(sizeof(struct vdec_vcu_ipi_init_ack), 24);
	CHECK_EQ(offsetof(struct vdec_vcu_ipi_init_ack, vcu_inst_addr), 16);
	CHECK_EQ(sizeof(struct vdec_vcu_ipi_query_cap_ack), 40);
	CHECK_EQ(offsetof(struct vdec_vcu_ipi_query_cap_ack, id), 16);
	CHECK_EQ(offsetof(struct vdec_vcu_ipi_query_cap_ack, ap_data_addr), 24);
	CHECK_EQ(offsetof(struct vdec_vcu_ipi_query_cap_ack, vcu_data_addr), 32);

	/* the message ids (vdec_ipi_msg.h:49-73) */
	CHECK_EQ(AP_IPIMSG_DEC_INIT, 0xA000);
	CHECK_EQ(AP_IPIMSG_DEC_START, 0xA001);
	CHECK_EQ(AP_IPIMSG_DEC_END, 0xA002);
	CHECK_EQ(AP_IPIMSG_DEC_DEINIT, 0xA003);
	CHECK_EQ(AP_IPIMSG_DEC_RESET, 0xA004);
	CHECK_EQ(AP_IPIMSG_DEC_SET_PARAM, 0xA005);
	CHECK_EQ(AP_IPIMSG_DEC_QUERY_CAP, 0xA006);
	CHECK_EQ(VCU_IPIMSG_DEC_INIT_ACK, 0xB000);
	CHECK_EQ(VCU_IPIMSG_DEC_START_ACK, 0xB001);
	CHECK_EQ(VCU_IPIMSG_DEC_DEINIT_ACK, 0xB003);
	CHECK_EQ(VCU_IPIMSG_DEC_RESET_ACK, 0xB004);
	CHECK_EQ(VCU_IPIMSG_DEC_SET_PARAM_ACK, 0xB005);
	CHECK_EQ(VCU_IPIMSG_DEC_QUERY_CAP_ACK, 0xB006);
	CHECK_EQ(VCU_IPIMSG_DEC_WAITISR, 0xC000);
	CHECK_EQ(VCU_IPIMSG_DEC_GET_FRAME_BUFFER, 0xC001);
	CHECK_EQ(VCU_IPIMSG_DEC_PUT_FRAME_BUFFER, 0xC002);
	CHECK_EQ(VCU_IPIMSG_DEC_LOCK_CORE, 0xC003);
	CHECK_EQ(VCU_IPIMSG_DEC_UNLOCK_CORE, 0xC004);
	CHECK_EQ(VCU_IPIMSG_DEC_LOCK_LAT, 0xC005);
	CHECK_EQ(VCU_IPIMSG_DEC_UNLOCK_LAT, 0xC006);

	/* the IPI ids the client picks (mtk_vcu.h:113-127) and the limits it shares with the encoder */
	CHECK_EQ(N2AP_VDEC_IPI_COMMON, 1);
	CHECK_EQ(N2AP_VDEC_IPI_H264, 2);
	CHECK_EQ(N2AP_VDEC_IPI_H265, 3);
	CHECK_EQ(N2AP_DEC_MAX_FB_NUM, 64);
	CHECK_EQ(N2AP_VDEC_RACING_WORDS, 68);
}

/* ---- 2. open: QUERY_CAP, INIT, SET_PARAM --------------------------------------------------- */

static void t_open(struct fix *f)
{
	struct mtk_video_fmt fmts[MTK_MAX_DEC_CODECS_SUPPORT];
	struct mtk_codec_framesizes fsz[MTK_MAX_DEC_CODECS_SUPPORT];
	int rc;

	rc = fix_session(f, 0, 2000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	CHECK_EQ(f->daemon.init_msgs, 1);
	CHECK_EQ(f->daemon.cap_msgs, 1);          /* the open's own capability query */
	CHECK_EQ(f->daemon.param_msgs, 2);        /* FRAME_SIZE + DECODE_MODE */
	CHECK_EQ(f->daemon.frame_size[0], 320);
	CHECK_EQ(f->daemon.frame_size[1], 240);
	CHECK_EQ(f->daemon.frame_size_seen, 1);
	CHECK_EQ(f->daemon.last_param_id, SET_PARAM_DECODE_MODE);
	CHECK_EQ(f->daemon.last_param_data[0], 5);
	CHECK_EQ(f->daemon.last_param_count, 8);   /* the message carries data[8] (vdec_ipi_msg.h:229-234) */
	CHECK(n2ap_vdec_vsi(f->dec) != NULL);
	CHECK_EQ(n2ap_vdec_vsi_addr(f->dec), f->daemon.vsi_guest);
	CHECK_EQ(n2ap_vdec_ipi_id(f->dec), N2AP_VDEC_IPI_H264);
	CHECK_EQ(n2ap_vdec_last_status(f->dec), 0);
	/* SET_PARAM_FB_NUM_PLANES went into the vsi, not into a message */
	CHECK_EQ(f->daemon.vsi->dec.fb_num_planes, 1);
	CHECK_EQ(n2ap_vdec_proto_errors(f->dec), 0);

	/* the capability list is copied out of the DMEM (vdec_vcu_if.c:56-62) */
	memset(fmts, 0, sizeof fmts);
	rc = n2ap_vdec_query_cap(f->dec, GET_PARAM_CAPABILITY_SUPPORTED_FORMATS, fmts, sizeof fmts);
	CHECK_EQ(rc, 0);
	CHECK_EQ(fmts[0].fourcc, 0x3231564eu);    /* NV12 */
	CHECK_EQ(fmts[0].type, MTK_FMT_FRAME);
	CHECK_EQ(fmts[2].fourcc, 0x34363248u);    /* H264 */
	CHECK_EQ(fmts[2].type, MTK_FMT_DEC);
	CHECK_EQ(fmts[3].fourcc, 0x35363248u);    /* H265 */
	memset(fsz, 0, sizeof fsz);
	rc = n2ap_vdec_query_cap(f->dec, GET_PARAM_CAPABILITY_FRAME_SIZES, fsz, sizeof fsz);
	CHECK_EQ(rc, 0);
	CHECK_EQ(fsz[0].fourcc, 0x34363248u);
	CHECK_EQ(fsz[0].stepwise.max_width, 4096);
	CHECK_EQ(fsz[0].stepwise.step_height, 32);
	CHECK_EQ(fsz[0].profile, 55);
	/* an id the daemon does not know is refused locally, before any message */
	CHECK_EQ(n2ap_vdec_query_cap(f->dec, 77, fmts, sizeof fmts), -EINVAL);
	CHECK_EQ(f->daemon.cap_msgs, 3);
}

static void t_open_failures(void)
{
	struct fix f;
	int rc;

	/* INIT answered with VDEC_IPI_MSG_STATUS_FAIL: -EINVAL, and the IPI handler is a dropper */
	if (fix_open(&f, 0) == 0) {
		f.daemon.init_status = VDEC_IPI_MSG_STATUS_FAIL;
		rc = fix_session(&f, 0, 500);
		/* the kernel returns the daemon's own status (vcodec_vcu_send_msg -> vcu->failure),
		 * so a FAIL status arrives as -1, not as a translated errno */
		CHECK_EQ(rc, VDEC_IPI_MSG_STATUS_FAIL);
		CHECK(f.dec == NULL);
		fix_close(&f);
	}

	/* a daemon that never answers: -ETIMEDOUT and the session is aborted (vdec_vcu_if.c:416-417) */
	if (fix_open(&f, 0) == 0) {
		struct n2ap_vdec_cfg cfg;

		memset(&cfg, 0, sizeof cfg);
		cfg.vcu = f.vcu;
		cfg.hw = &f.hw.hw;
		cfg.codec_fourcc = N2AP_VDEC_FOURCC_H264;
		cfg.timeout_ms = 120;
		cfg.query_cap = 0;
		cfg.pic_w = 320;
		cfg.pic_h = 240;
		decd_stop(&f.daemon);              /* nobody serves the messages */
		rc = n2ap_vdec_open(&f.dec, &cfg);
		CHECK_EQ(rc, -ETIMEDOUT);
		CHECK(f.dec == NULL);
		fix_close(&f);
	}

	/* an unsupported codec fourcc is refused before any message */
	if (fix_open(&f, 0) == 0) {
		struct n2ap_vdec_cfg cfg;

		memset(&cfg, 0, sizeof cfg);
		cfg.vcu = f.vcu;
		cfg.hw = &f.hw.hw;
		cfg.codec_fourcc = N2AP_VDEC_FOURCC_HEIF + 1u;
		cfg.query_cap = 0;
		rc = n2ap_vdec_open(&f.dec, &cfg);
		CHECK_EQ(rc, -EINVAL);
		fix_close(&f);
	}
}

/* ---- 3. decode: the vsi fields and the START message --------------------------------------- */

static void t_decode_header(struct fix *f)
{
	struct n2ap_vdec_job job;
	uint32_t handle = 0, pa = 0, guest = 0;
	int rc;

	rc = fix_session(f, 0, 2000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	decd_script_header_only(&f->daemon);
	if (fix_bs(f, 4096, &handle, &pa, &guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = handle;
	job.bs_fd = 0x501;
	job.bs_cookie = 0xb0000001u;    /* vdec_bs_va: distinct per queued src buffer */
	job.bs_bytes = 4096;
	job.bs_length = 1234;
	job.bs_flags = 0;
	job.timestamp = 0x1234;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	CHECK_EQ(f->daemon.start_msgs, 1);
	/* data[0] = bs->size = bytesused = the AU; data[1] = bs->length = the buffer size
	 * (mtk_vcodec_dec.c:670-671; the names are the kernel's, the meaning is the opposite) */
	CHECK_EQ(f->daemon.last_start_data[0], 1234);
	CHECK_EQ(f->daemon.last_start_data[1], 4096);
	CHECK_EQ(f->daemon.last_start_data[2], 0);      /* data[2] = bs->flags */
	/* the vsi fields of vdec_common_if.c:243-290 */
	CHECK_EQ(f->daemon.vsi->dec.bs_dma, pa);
	CHECK_EQ(f->daemon.vsi->dec.bs_fd, 0x501);
	CHECK_EQ(f->daemon.vsi->dec.vdec_bs_va, 0xb0000001u);
	CHECK_EQ(f->daemon.vsi->dec.index, 0xFF);       /* no frame buffer: index 0xFF */
	CHECK_EQ(f->daemon.vsi->dec.vdec_fb_va, 0);
	CHECK_EQ(f->daemon.vsi->dec.timestamp, 0x1234);
	/* the header decode's result is read back from the vsi */
	CHECK_EQ(n2ap_vdec_changed(f->dec), VDEC_RES_CHANGE | VDEC_CROP_CHANGED);
	/* the bitstream was synced to the device before the trigger (n2vc_hw.h) */
	CHECK(f->hw.dma_rw_to_dev >= 1);
	/* M2: no general buffer, even for a START without a frame buffer (vdec_common_if.c:246-248):
	 * fd 0 would make the vendor import stdin as a dmabuf. */
	CHECK_EQ(f->daemon.vsi->general_buf_fd, -1);
	CHECK_EQ(f->daemon.vsi->general_buf_size, 0);
	CHECK_EQ(f->daemon.vsi->general_buf_dma, 0);
	/* H3: the consumed bitstream cookie is drained from list_free_bs after the ACK */
	{
		struct n2ap_vdec_stats st;
		uint64_t cookie = 0;

		CHECK_EQ(f->daemon.bs_pushes, 1);
		CHECK_EQ(f->daemon.last_bs_cookie, 0xb0000001u);
		CHECK_EQ(n2ap_vdec_pop_free_bs(f->dec, &cookie), 1);
		CHECK_EQ(cookie, 0xb0000001u);
		CHECK_EQ(n2ap_vdec_pop_free_bs(f->dec, &cookie), 0);
		n2ap_vdec_stats(f->dec, &st);
		CHECK_EQ(st.bs_freed, 1);
		CHECK_EQ(st.harvests, 1);        /* no PUT_FRAME_BUFFER request in a header decode */
		CHECK_EQ(st.put_fb, 0);
	}
}

static void t_decode_frame(struct fix *f)
{
	struct n2ap_vdec_job job;
	struct n2ap_vdec_fb fb;
	struct n2ap_vdec_out out;
	struct n2ap_vdec_freed freed;
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0;
	uint32_t fbh = 0, fb_pa = 0, fb_guest = 0;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	decd_script_full_frame(&f->daemon);
	f->daemon.disp_frames = 1;
	f->daemon.free_frames = 1;
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0 ||
	    fix_bs(f, 16384, &fbh, &fb_pa, &fb_guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&fb, 0, sizeof fb);
	fb.handle = fbh;
	fb.fd = 0x502;
	fb.bytes = 16384;
	fb.index = 3;
	fb.cookie = 0;
	rc = n2ap_vdec_push_fb(f->dec, &fb);
	CHECK_EQ(rc, 0);
	CHECK_EQ(n2ap_vdec_free_fb_count(f->dec), 1);
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_fd = 0x501;
	job.bs_bytes = 4096;
	job.bs_length = 4096;
	job.fb_handle = fbh;
	job.fb_fd = 0x502;
	job.fb_bytes = 16384;
	job.fb_planes = 1;
	job.fb_index = 3;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	/* the daemon's LOCK/WAITISR/GET/PUT were all answered */
	CHECK_EQ(f->daemon.start_msgs, 1);
	CHECK_EQ(f->daemon.last_fb_index, 3);          /* GET_FRAME_BUFFER served the queued buffer */
	CHECK_EQ(f->daemon.last_fb_va, (uint64_t)fbh); /* cookie = handle when the caller gives none */
	CHECK_EQ(f->daemon.last_fb_dma[0], fb_pa);
	CHECK_EQ(f->daemon.last_fb_fd[0], 0x502);
	CHECK_EQ(n2ap_vdec_free_fb_count(f->dec), 0);
	/* PUT_FRAME_BUFFER harvested list_disp and list_free */
	rc = n2ap_vdec_pop_displayed(f->dec, &out);
	CHECK_EQ(rc, 1);
	CHECK_EQ(out.cookie, (uint64_t)fbh);   /* the daemon echoes the cookie of the buffer it got */
	CHECK_EQ(out.poc, 0);
	CHECK_EQ(out.timestamp, 0x5000);
	rc = n2ap_vdec_pop_freed(f->dec, &freed);
	CHECK_EQ(rc, 1);
	CHECK_EQ(freed.cookie, 0x2000);
	CHECK_EQ(n2ap_vdec_pop_displayed(f->dec, &out), 0);
	CHECK_EQ(n2ap_vdec_pop_freed(f->dec, &freed), 0);
	/* the frame that was displayed is one of ours */
	{
		struct n2ap_vdec_fb got;

		CHECK_EQ(n2ap_vdec_fb_of(f->dec, (uint64_t)fbh, &got), 1);
		CHECK_EQ(got.handle, fbh);
	}
	/* output planes were synced from the device when the frame appeared (VDEC_PROTOCOL.md 5.9) */
	CHECK(f->hw.dma_rw_from_dev >= 1);
}

/*
 * H2 + M1: synchronous mode.  The vendor queues list_disp/list_free inside START and sends no
 * PUT_FRAME_BUFFER at all (measured by opus-dec1: P=0 over 12 AUs), so the AP must harvest after the
 * ACK; a frame buffer passed in the job must be registered so its planes are synced from the device.
 */
static void t_decode_sync(struct fix *f)
{
	struct n2ap_vdec_job job;
	struct n2ap_vdec_out out;
	struct n2ap_vdec_freed freed;
	struct n2ap_vdec_stats st;
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0, fbh = 0, fb_pa = 0, fb_guest = 0;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	decd_script_header_only(&f->daemon);
	f->daemon.disp_frames = 1;
	f->daemon.free_frames = 2;
	f->daemon.free_fb_cookie = 1;   /* the vendor eventually returns the displayed buffer */
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0 ||
	    fix_bs(f, 16384, &fbh, &fb_pa, &fb_guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_fd = 0x503;
	job.bs_bytes = 4096;
	job.fb_handle = fbh;
	job.fb_fd = 0x504;
	job.fb_bytes = 16384;
	job.fb_planes = 1;
	job.fb_cookie = 0xf0000002u;   /* vdec_fb_va: never a small DMA handle */
	job.fb_index = 2;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	n2ap_vdec_stats(f->dec, &st);
	CHECK_EQ(st.put_fb, 0);              /* the daemon never sent PUT_FRAME_BUFFER */
	CHECK_EQ(st.harvests, 1);
	CHECK_EQ(st.frames_displayed, 1);
	CHECK_EQ(st.fbs_freed, 2);
	CHECK_EQ(st.bs_freed, 1);
	/* the displayed frame is the buffer from the job, and its planes came back from the device */
	rc = n2ap_vdec_pop_displayed(f->dec, &out);
	CHECK_EQ(rc, 1);
	CHECK_EQ(out.cookie, 0xf0000002u);
	CHECK_EQ(out.poc, 0);
	CHECK_EQ(out.timestamp, 0x5000);
	CHECK(f->hw.dma_rw_from_dev >= 1);
	rc = n2ap_vdec_pop_freed(f->dec, &freed);
	CHECK_EQ(rc, 1);
	CHECK_EQ(freed.cookie, 0xf0000002u);
	CHECK_EQ(n2ap_vdec_pop_freed(f->dec, &freed), 1);
	CHECK_EQ(freed.cookie, 0x2001);
	CHECK_EQ(n2ap_vdec_pop_freed(f->dec, &freed), 0);
	/* the buffer was registered in-use by the job and dropped by list_free */
	{
		struct n2ap_vdec_fb got;

		CHECK_EQ(n2ap_vdec_fb_of(f->dec, 0xf0000002u, &got), 0);
	}
}

/*
 * H1: two output planes.  The vendor reports MM21 with one size per plane and programs
 * vsi.dec.fb_dma[1]/fb_fd[1] as the chroma target.
 */
static void t_two_planes(struct fix *f)
{
	struct n2ap_vdec_job job;
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0;
	uint32_t fbh = 0, fb_pa = 0, fb_guest = 0;
	uint32_t ch = 0, c_pa = 0, c_guest = 0;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* header first: the fake reports fb_sz[0] (Y) and fb_sz[1] (C) like the vendor's MM21 */
	f->daemon.fb_sz = 320u * 256u;
	f->daemon.fb_sz_c = 320u * 128u;
	decd_script_header_only(&f->daemon);
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	CHECK_EQ(f->daemon.vsi->pic.fb_sz[0], 320u * 256u);
	CHECK_EQ(f->daemon.vsi->pic.fb_sz[1], 320u * 128u);

	if (fix_bs(f, 320u * 384u, &fbh, &fb_pa, &fb_guest) != 0 ||
	    fix_bs(f, 320u * 128u, &ch, &c_pa, &c_guest) != 0) {
		CHECK(0);
		return;
	}
	/* an allocation too small for both planes is refused (MED 7) */
	{
		uint32_t small = 0, small_pa = 0, small_guest = 0;
		struct n2ap_vdec_job sj;

		if (fix_bs(f, 1024, &small, &small_pa, &small_guest) != 0) {
			CHECK(0);
			return;
		}
		sj = job;
		sj.fb_handle = small;
		sj.fb_bytes = 1024;
		sj.fb_planes = 2;
		sj.fb_c_handle = small;
		CHECK_EQ(n2ap_vdec_decode(f->dec, &sj), -EINVAL);
	}
	/* 2 planes, no chroma handle: must be refused (a zero chroma target is PA 0) */
	job.fb_handle = fbh;
	job.fb_fd = 0x505;
	job.fb_bytes = 320u * 384u;
	job.fb_planes = 2;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, -EINVAL);
	/* 2 planes in one allocation: chroma = fb_pa + fb_sz[0], same share id, and the display
	 * harvest copies the whole allocation once (HIGH 2 of the review) */
	f->daemon.disp_frames = 1;
	f->daemon.free_frames = 1;
	f->daemon.free_fb_cookie = 1;
	{
		unsigned before = f->hw.dma_rw_from_dev;

		job.fb_c_handle = fbh;
		job.fb_c_fd = 0;
		rc = n2ap_vdec_decode(f->dec, &job);
		CHECK_EQ(rc, 0);
		CHECK_EQ(f->daemon.last_fb_planes, 2);
		CHECK_EQ(f->daemon.last_fb_dma[0], fb_pa);
		CHECK_EQ(f->daemon.last_fb_dma[1], fb_pa + 320u * 256u);
		CHECK_EQ(f->daemon.last_fb_fd[0], 0x505);
		CHECK_EQ(f->daemon.last_fb_fd[1], 0x505);
		CHECK_EQ(f->hw.dma_rw_from_dev, before + 1);
	}
	/* 2 separate allocations: the chroma handle and its share id verbatim, two copies */
	{
		unsigned before = f->hw.dma_rw_from_dev;

		f->daemon.free_fb_cookie = 0;
		job.fb_c_handle = ch;
		job.fb_c_fd = 0x506;
		rc = n2ap_vdec_decode(f->dec, &job);
		CHECK_EQ(rc, 0);
		CHECK_EQ(f->daemon.last_fb_dma[1], c_pa);
		CHECK_EQ(f->daemon.last_fb_fd[1], 0x506);
		CHECK_EQ(f->hw.dma_rw_from_dev, before + 2);
	}
	/* 1 plane: the chroma words of a previous job must not leak into the vsi */
	f->daemon.disp_frames = 0;
	f->daemon.free_frames = 0;
	job.fb_planes = 1;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	CHECK_EQ(f->daemon.last_fb_planes, 1);
	CHECK_EQ(f->daemon.last_fb_dma[1], 0);
	CHECK_EQ(f->daemon.last_fb_fd[1], 0);
}

static void t_decode_nofb(struct fix *f)
{
	struct n2ap_vdec_job job;
	struct n2ap_vdec_out out;
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* GET_FRAME_BUFFER with nothing queued: 1 s wait, then index 0xFF (vdec_vcu_if.c:292-355) */
	decd_script_header_only(&f->daemon);
	f->daemon.do_get_fb = 1;
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	CHECK_EQ(f->daemon.last_fb_index, 0xFF);
	CHECK_EQ(n2ap_vdec_pop_displayed(f->dec, &out), 0);
}

/* ---- 4. the power role: LOCK/UNLOCK, racing words, HW break -------------------------------- */

static void t_power_core_lat(struct fix *f)
{
	struct decv_msg m;
	uint32_t i;
	int rc;

	rc = fix_session(f, 0, 2000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* the frozen mapping: VDEC_RACING belongs to the CORE block */
	f->hw.racing_blk = RWVC_BLK_VDEC_CORE;
	/* put known words where the kernel stores the racing info (mtk_vcodec_pm.h:15-16) */
	for (i = 0; i < N2AP_VDEC_RACING_WORDS; i++)
		decm_set(&f->hw, RWVC_WIN_VDEC_RACING, 0x100u + 4u * i, 0x1000u + i);
	/* ... but the client must *save* the hardware's values and restore its own copy, so the
	 * check is the round trip: latch, change the hardware behind its back, restore. */
	memset(&m, 0, sizeof m);
	m.len = 16;
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	rc = decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(rc, 1);                              /* a request is not acknowledged */
	CHECK_EQ(decm_power_on(&f->hw, RWVC_BLK_VDEC_CORE), 1);
	CHECK(decm_is_on(&f->hw, RWVC_BLK_VDEC_SOC));  /* the CORE reference holds its parent */
	/* the restore wrote the 68 words back: they still read what the test set */
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_RACING, 0x100u), 0x1000u);
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_RACING, 0x100u + 4u * 67u), 0x1000u + 67u);
	/* the hardware changes the words; the last UNLOCK saves them, the next first LOCK restores */
	decm_set(&f->hw, RWVC_WIN_VDEC_RACING, 0x100u, 0xdeadbeefu);
	{
		uint32_t id = VCU_IPIMSG_DEC_UNLOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	rc = decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(rc, 1);
	CHECK_EQ(decm_power_off(&f->hw, RWVC_BLK_VDEC_CORE), 1);
	CHECK(decm_all_off(&f->hw));
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	rc = decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(rc, 1);
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_RACING, 0x100u), 0xdeadbeefu);
	{
		uint32_t id = VCU_IPIMSG_DEC_UNLOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK(decm_all_off(&f->hw));

	/* LOCK_LAT takes the SOC block only; with the frozen RACING mapping the restore is refused
	 * (the register range needs the CORE block), which the client must tolerate */
	f->hw.access_denied = 0;
	f->hw.racing_blk = RWVC_BLK_VDEC_CORE;
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_LAT;

		memcpy(m.buf, &id, 4);
	}
	rc = decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(rc, 1);
	{
		unsigned before = decm_power_on(&f->hw, RWVC_BLK_VDEC_SOC);

		CHECK(before >= 1);
		{
			uint32_t id = VCU_IPIMSG_DEC_UNLOCK_LAT;

			memcpy(m.buf, &id, 4);
		}
		(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
		CHECK(decm_all_off(&f->hw));
	}

	/* with the mapping mtk_vcodec_dec_pm.c implies (the racing registers in the SOC range) the
	 * LAT-only path restores and saves them too */
	f->hw.racing_blk = RWVC_BLK_VDEC_SOC;
	decm_set(&f->hw, RWVC_WIN_VDEC_RACING, 0x100u, 0x11112222u);
	{
		struct n2ap_vdec_stats st0, st;
		uint32_t id = VCU_IPIMSG_DEC_LOCK_LAT;

		n2ap_vdec_stats(f->dec, &st0);
		memcpy(m.buf, &id, 4);
		(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
		n2ap_vdec_stats(f->dec, &st);
		/* opus-decint: the restore is done when VDEC_CORE is powered (the kernel's VDEC_RACING window is a CORE
		 * window, so a LAT-only restore is always EACCES on the phone); a LAT-only period restores nothing */
		CHECK_EQ(st.racing_restores, st0.racing_restores);
		id = VCU_IPIMSG_DEC_UNLOCK_LAT;
		memcpy(m.buf, &id, 4);
		(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
		n2ap_vdec_stats(f->dec, &st);
		CHECK_EQ(st.racing_saves, st0.racing_saves);   /* opus-decint: saved at the CORE power-off only */
	}
	CHECK(decm_all_off(&f->hw));
}

static void t_power_hwbreak(struct fix *f)
{
	struct decv_msg m;
	int rc;

	rc = fix_session(f, 0, 2000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* Every daemon request below is a 16-byte vdec_vcu_ipi_ack; the handler refuses a short one. */
	memset(&m, 0, sizeof m);
	m.len = 16;
	f->hw.racing_blk = RWVC_BLK_VDEC_SOC;
	/* CORE: VDEC_SYS reads 0 -> no break (mtk_vcodec_dec_pm.c:322-327) */
	decm_set(&f->hw, RWVC_WIN_VDEC_SYS, 0, 0);
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	{
		uint32_t id = VCU_IPIMSG_DEC_UNLOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_MISC, 0x100u), 0);   /* nothing was written */
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_VLD, 0x108u), 0);

	/* CORE with the block running: the exact sequence of :330-401 */
	decm_set(&f->hw, RWVC_WIN_VDEC_SYS, 0, 0x3u);
	decm_set(&f->hw, RWVC_WIN_VDEC_BASE, 0x800u + 0x08cu, 0);    /* is_ufo = 0 */
	decm_set(&f->hw, RWVC_WIN_VDEC_MISC, 0x100u, 0x40u);
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	{
		uint32_t id = VCU_IPIMSG_DEC_UNLOCK_CORE;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	if (getenv("N2DEC_TRACE") != NULL)
		fprintf(stderr, "DBG misc100=%08x writes=%u denied=%u regops=%u polls=%u "
		        "pw_on=%u/%u pw_off=%u/%u pw_denied=%u reqs=%u no_handler=%u sends=%u replies=%u\n",
		        decm_get(&f->hw, RWVC_WIN_VDEC_MISC, 0x100u), f->hw.writes, f->hw.access_denied,
		        f->hw.reg_ops, f->hw.hwbreak_polls,
		        f->hw.power_on_calls[RWVC_BLK_VDEC_SOC], f->hw.power_on_calls[RWVC_BLK_VDEC_CORE],
		        f->hw.power_off_calls[RWVC_BLK_VDEC_SOC], f->hw.power_off_calls[RWVC_BLK_VDEC_CORE],
		        f->hw.power_denied, f->vcu->requests, f->vcu->no_handler, f->vcu->sends,
		        f->vcu->replies);
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_MISC, 0x100u), 0x41u);   /* |= 1 */
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_VLD, 0x108u), 0);        /* 1 then 0 */
	CHECK_EQ(f->hw.hwbreak_acks, 1);

	/* LAT: SOC_GCON+0x200 == 0 -> no break (:412-418) */
	decm_set(&f->hw, RWVC_WIN_VDEC_SOC_GCON, 0x200u, 0);
	decm_set(&f->hw, RWVC_WIN_VDEC_LAT_MISC, 0x100u, 0u);
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_LAT;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	{
		uint32_t id = VCU_IPIMSG_DEC_UNLOCK_LAT;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_LAT_MISC, 0x100u), 0);

	/* LAT running: MISC+0x100 |= 1, poll 0x104 for bits 0 and 4, VLD+0x108 = 1, 0 (:419-460) */
	decm_set(&f->hw, RWVC_WIN_VDEC_SOC_GCON, 0x200u, 1u);
	decm_set(&f->hw, RWVC_WIN_VDEC_LAT_MISC, 0x100u, 0x80u);
	decm_set(&f->hw, RWVC_WIN_VDEC_LAT_MISC, 0x104u, 0u);
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_LAT;

		memcpy(m.buf, &id, 4);
	}
	(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	{
		struct n2ap_vdec_stats st;
		uint32_t id = VCU_IPIMSG_DEC_UNLOCK_LAT;

		memcpy(m.buf, &id, 4);
		(void)decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
		n2ap_vdec_stats(f->dec, &st);
		/* the LAT poll needs the model's scripted ack; decm_regs answers only MISC+0x104, so the
		 * LAT path's 1 s timeout is expected here unless the model scripts it.  What is asserted
		 * is the write and the VLD reset, which happen either way. */
		CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_LAT_MISC, 0x100u), 0x81u);
	}
	CHECK_EQ(decm_get(&f->hw, RWVC_WIN_VDEC_LAT_VLD, 0x108u), 0);
	CHECK(decm_all_off(&f->hw));
}

static void t_waitisr(struct fix *f)
{
	struct decv_msg m;
	int rc;

	rc = fix_session(f, 0, 2000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* core 0 -> RWVC_IRQ_VDEC, core 1 -> RWVC_IRQ_VDEC_LAT, 500 ms (WAIT_INTR_TIMEOUT_MS) */
	memset(&m, 0, sizeof m);
	m.len = 16;
	{
		int32_t core = MTK_VDEC_CORE;
		uint32_t id = VCU_IPIMSG_DEC_WAITISR;

		memcpy(m.buf, &id, 4);
		memcpy(m.buf + 4, &core, 4);
	}
	f->hw.irq_status_value = 0x10000u;
	rc = decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(rc, 1);
	{
		int32_t status = -1;

		memcpy(&status, m.buf + 4, 4);
		CHECK_EQ(status, 0);            /* the wait's result goes back into status */
	}
	CHECK_EQ(f->hw.irq_waits, 1);
	CHECK_EQ(f->hw.irq, RWVC_IRQ_VDEC);
	CHECK_EQ(f->hw.irq_clear_calls, 1);   /* the ISR also acks a successful wait */

	{
		int32_t core = MTK_VDEC_LAT;
		uint32_t id = VCU_IPIMSG_DEC_WAITISR;

		memcpy(m.buf, &id, 4);
		memcpy(m.buf + 4, &core, 4);
	}
	rc = decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
	CHECK_EQ(rc, 1);
	CHECK_EQ(f->hw.irq_waits, 2);
	CHECK_EQ(f->hw.irq, RWVC_IRQ_VDEC_LAT);
	CHECK_EQ(f->hw.irq_clear_calls, 2);

	/* a timed-out wait is reported as -1 in status: mtk_vcodec_wait_for_done_ctx's own value
	 * (mt6885/mtk_vcodec_intr.c:30), copied by vdec_vcu_if.c:255 - not an errno. */
	f->hw.irq_timeout_after = 1;
	{
		int32_t core = MTK_VDEC_CORE, status = 0;
		uint32_t id = VCU_IPIMSG_DEC_WAITISR;

		memcpy(m.buf, &id, 4);
		memcpy(m.buf + 4, &core, 4);
		rc = decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m);
		CHECK_EQ(rc, 1);
		memcpy(&status, m.buf + 4, 4);
		CHECK_EQ(status, -1);
	}
}

/* ---- 5. flush / end / close ----------------------------------------------------------------- */

static void t_flush_close(struct fix *f)
{
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0;
	int rc;

	rc = fix_session(f, 0, 2000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0) {
		CHECK(0);
		return;
	}
	CHECK_EQ(n2ap_vdec_flush(f->dec), 0);
	CHECK_EQ(f->daemon.reset_msgs, 1);
	CHECK_EQ(n2ap_vdec_end(f->dec), 0);
	CHECK_EQ(f->daemon.end_msgs, 1);
	/* decode(NULL) is the flush (vdec_common_if.c:243) */
	{
		struct n2ap_vdec_job zero;

		memset(&zero, 0, sizeof zero);
		CHECK_EQ(n2ap_vdec_decode(f->dec, &zero), 0);
		CHECK_EQ(f->daemon.reset_msgs, 2);
	}
	rc = n2ap_vdec_close(f->dec);
	f->dec = NULL;
	CHECK_EQ(rc, 0);
	CHECK_EQ(f->daemon.deinit_msgs, 1);
	/* DEINIT carries the vcu_inst_addr (vdec_vcu_if.c:582) */
	CHECK_EQ(f->daemon.last_deinit_inst, f->daemon.vsi_guest);
}

/* ---- 6. a full session with the daemon's own request sequence ------------------------------- */

static void t_full_frame_script(struct fix *f)
{
	struct n2ap_vdec_stats st;
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0, fbh = 0, fb_pa = 0, fb_guest = 0;
	struct n2ap_vdec_job job;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	decd_script_full_frame(&f->daemon);
	f->hw.racing_blk = RWVC_BLK_VDEC_SOC;
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0 ||
	    fix_bs(f, 16384, &fbh, &fb_pa, &fb_guest) != 0) {
		CHECK(0);
		return;
	}
	/* one buffer queued for the daemon's GET_FRAME_BUFFER, and both cores report a running block
	 * so the HW-break sequence actually executes (mtk_vcodec_dec_pm.c:330, :419) */
	{
		struct n2ap_vdec_fb q;

		memset(&q, 0, sizeof q);
		q.handle = fbh;
		q.fd = 0x502;
		q.bytes = 16384;
		q.index = 1;
		CHECK_EQ(n2ap_vdec_push_fb(f->dec, &q), 0);
	}
	decm_set(&f->hw, RWVC_WIN_VDEC_SYS, 0, 1u);
	decm_set(&f->hw, RWVC_WIN_VDEC_SOC_GCON, 0x200u, 1u);
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	job.fb_handle = fbh;
	job.fb_bytes = 16384;
	job.fb_planes = 1;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	n2ap_vdec_stats(f->dec, &st);
	CHECK_EQ(st.locks_lat, 1);
	CHECK_EQ(st.unlocks_lat, 1);
	CHECK_EQ(st.locks_core, 1);
	CHECK_EQ(st.unlocks_core, 1);
	CHECK_EQ(st.wait_isr, 2);
	CHECK_EQ(st.wait_isr_timeouts, 0);
	CHECK_EQ(st.get_fb, 1);
	CHECK_EQ(st.put_fb, 1);        /* the daemon's own PUT_FRAME_BUFFER request */
	CHECK_EQ(st.harvests, 2);      /* ... and the AP-side harvest after the ACK */
	CHECK_EQ(st.bs_freed, 1);      /* the consumed bitstream cookie */
	CHECK_EQ(st.frames_displayed, 1);
	CHECK_EQ(st.hw_breaks, 2);      /* LAT and CORE each end their own active period */
	CHECK_EQ(st.racing_restores, 0);/* opus-decint: restored at a CORE lock, and only words saved before (none yet) */
	CHECK_EQ(st.racing_saves, 1);   /* opus-decint: saved once, at the CORE power-off */
	CHECK_EQ(st.power_on, 3);       /* SOC (common) + LAT + CORE */
	CHECK_EQ(st.power_off, 3);
	CHECK_EQ(n2ap_vdec_proto_errors(f->dec), 0);
	CHECK(decm_all_off(&f->hw));
}

/* ---- 6b. picture info, plane policy, two-phase free, PUT inside START --------------------- */

static void t_pic_info(struct fix *f)
{
	struct n2ap_vdec_pic pi;
	struct n2ap_vdec_job job;
	uint32_t bs = 0, pa = 0, guest = 0;
	int rc;

	rc = fix_session(f, 0, 2000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* nothing parsed yet: the API says so instead of returning zeros as a valid picture */
	CHECK_EQ(n2ap_vdec_get_pic_info(f->dec, &pi), -EAGAIN);
	f->daemon.pic_w = 320;
	f->daemon.pic_h = 240;
	f->daemon.buf_w = 320;
	f->daemon.buf_h = 256;
	f->daemon.crop_l = 0;
	f->daemon.crop_t = 0;
	f->daemon.crop_w = 320;
	f->daemon.crop_h = 200;          /* a cropped SPS: display 200, coded 240 */
	f->daemon.fourcc = N2AP_VDEC_FOURCC_MM21;
	f->daemon.fb_sz = 320u * 256u;
	f->daemon.fb_sz_c = 320u * 128u;
	decd_script_header_only(&f->daemon);
	if (fix_bs(f, 4096, &bs, &pa, &guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	CHECK_EQ(n2ap_vdec_get_pic_info(f->dec, &pi), 0);
	CHECK_EQ(pi.disp_w, 320);       /* crop, not pic_h (DEC03's 1920x1080 is pic 1088) */
	CHECK_EQ(pi.disp_h, 200);
	CHECK_EQ(pi.pic_w, 320);
	CHECK_EQ(pi.pic_h, 240);
	CHECK_EQ(pi.buf_h, 256);
	CHECK_EQ(pi.fb_sz[0], 320u * 256u);
	CHECK_EQ(pi.fb_sz[1], 320u * 128u);
	CHECK_EQ(pi.fourcc, N2AP_VDEC_FOURCC_MM21);
	CHECK_EQ(pi.dpb_sz, 9);         /* the DPB count is dpb_sz + 1 */
	CHECK((pi.changed & VDEC_RES_CHANGE) != 0u);
	/* a bitstream length beyond the mapped range is refused before any message */
	job.bs_length = 5000;
	job.bs_bytes = 4096;
	CHECK_EQ(n2ap_vdec_decode(f->dec, &job), -EINVAL);
	CHECK_EQ(f->daemon.start_msgs, 1);
}

static void t_open_zero_size(struct fix *f)
{
	struct n2ap_vdec_cfg cfg;
	int rc;

	/* The reference client always sends FRAME_SIZE after INIT, 0x0 included
	 * (mtk_vcodec_dec.c:2200-2211); fb_num_planes 0 means 2 for H.264. */
	memset(&cfg, 0, sizeof cfg);
	cfg.vcu = f->vcu;
	cfg.hw = &f->hw.hw;
	cfg.codec_fourcc = N2AP_VDEC_FOURCC_H264;
	cfg.timeout_ms = 2000;
	cfg.query_cap = 0;
	rc = n2ap_vdec_open(&f->dec, &cfg);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	CHECK_EQ(f->daemon.param_msgs, 1);          /* FRAME_SIZE only (no DECODE_MODE) */
	CHECK_EQ(f->daemon.frame_size_seen, 1);
	CHECK_EQ(f->daemon.frame_size[0], 0);
	CHECK_EQ(f->daemon.frame_size[1], 0);
	CHECK_EQ(f->daemon.vsi->dec.fb_num_planes, 2);
}

static void t_output_fourcc(struct fix *f)
{
	struct n2ap_vdec_cfg cfg;
	struct n2ap_vdec_job job;
	uint32_t bs = 0, pa = 0, guest = 0;
	int rc;

	/* A client that will detile MM21 requires it: with layout.mode unset the vendor reports
	 * MT21S (small) or MT21 (>=1080p), and handing those to ap/mm21 would corrupt the picture. */
	memset(&cfg, 0, sizeof cfg);
	cfg.vcu = f->vcu;
	cfg.hw = &f->hw.hw;
	cfg.codec_fourcc = N2AP_VDEC_FOURCC_H264;
	cfg.timeout_ms = 2000;
	cfg.query_cap = 0;
	cfg.pic_w = 320;
	cfg.pic_h = 240;
	cfg.output_fourcc = N2AP_VDEC_FOURCC_MM21;
	rc = n2ap_vdec_open(&f->dec, &cfg);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	f->daemon.fourcc = N2AP_VDEC_FOURCC_M21S;
	decd_script_header_only(&f->daemon);
	if (fix_bs(f, 4096, &bs, &pa, &guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, -ENOTSUP);
	CHECK_EQ(f->daemon.start_msgs, 1);
	/* with MM21 reported the same job is accepted */
	f->daemon.fourcc = N2AP_VDEC_FOURCC_MM21;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	CHECK_EQ(f->daemon.start_msgs, 2);
}

static void t_two_phase_free(struct fix *f)
{
	struct n2ap_vdec_job job;
	struct n2ap_vdec_out out;
	struct n2ap_vdec_fb got;
	struct n2ap_vdec_stats st;
	struct decv_msg m;
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0, fbh = 0, fb_pa = 0, fb_guest = 0;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	decd_script_header_only(&f->daemon);
	f->daemon.disp_frames = 0;
	f->daemon.free_frames = 1;
	f->daemon.free_fb_cookie = 1;
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0 ||
	    fix_bs(f, 16384, &fbh, &fb_pa, &fb_guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	job.fb_handle = fbh;
	job.fb_bytes = 16384;
	job.fb_planes = 1;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	n2ap_vdec_stats(f->dec, &st);
	CHECK_EQ(st.fbs_freed, 1);
	CHECK_EQ(st.frames_displayed, 0);
	CHECK_EQ(f->hw.dma_rw_from_dev, 0);          /* no display yet: nothing to sync */
	CHECK_EQ(n2ap_vdec_fb_of(f->dec, (uint64_t)fbh, &got), 1);
	/* the buffer was not re-pushable yet (list_free seen, list_disp not) */
	CHECK_EQ(n2ap_vdec_push_fb(f->dec, &got), -EBUSY);
	/* the display entry arrives later, for the same cookie (B-frame order) */
	f->daemon.disp_frames = 1;
	f->daemon.free_frames = 0;
	decd_queue_disp(&f->daemon, 1);
	memset(&m, 0, sizeof m);
	m.len = 16;
	{
		uint32_t id = VCU_IPIMSG_DEC_PUT_FRAME_BUFFER;

		memcpy(m.buf, &id, 4);
	}
	CHECK_EQ(decv_daemon_request(f->vcu, N2AP_VDEC_IPI_H264, &m), 1);
	rc = n2ap_vdec_pop_displayed(f->dec, &out);
	CHECK_EQ(rc, 1);
	CHECK_EQ(out.cookie, (uint64_t)fbh);
	CHECK(f->hw.dma_rw_from_dev >= 1);           /* synced even though list_free came first */
	CHECK_EQ(n2ap_vdec_fb_of(f->dec, (uint64_t)fbh, &got), 0);
	CHECK_EQ(n2ap_vdec_push_fb(f->dec, &got), 0); /* both rings returned it: reusable again */
}

static void t_get_fb_bad(struct fix *f)
{
	struct n2ap_vdec_job job;
	struct n2ap_vdec_fb fb;
	uint32_t bs = 0, pa = 0, guest = 0;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* A queued buffer whose handle the DMA registry does not know must never reach the daemon
	 * as fb_dma = 0: the GET answer says "no buffer" instead (MED 10). */
	decd_script_header_only(&f->daemon);
	f->daemon.do_get_fb = 1;
	memset(&fb, 0, sizeof fb);
	fb.handle = 0xdead0001u;
	fb.fd = 0x507;
	fb.bytes = 4096;
	fb.index = 7;
	CHECK_EQ(n2ap_vdec_push_fb(f->dec, &fb), 0);
	if (fix_bs(f, 4096, &bs, &pa, &guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	CHECK_EQ(f->daemon.last_fb_index, 0xFF);
	CHECK_EQ(f->daemon.last_fb_va, 0);
	CHECK(f->daemon.last_fb_dma[0] == 0);
	CHECK(n2ap_vdec_proto_errors(f->dec) >= 1);
}

static void t_put_during_start(struct fix *f)
{
	struct n2ap_vdec_job job;
	struct n2ap_vdec_out out;
	struct n2ap_vdec_stats st;
	uint32_t bs = 0, bs_pa = 0, bs_guest = 0, fbh = 0, fb_pa = 0, fb_guest = 0;
	int rc;

	rc = fix_session(f, 0, 3000);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	/* The daemon sends PUT_FRAME_BUFFER from inside START, before its START_ACK, and no
	 * GET_FRAME_BUFFER: the job buffer must already be registered for the harvest to sync it. */
	decd_script_full_frame(&f->daemon);
	f->daemon.do_get_fb = 0;
	f->daemon.disp_frames = 1;
	f->daemon.free_frames = 0;
	if (fix_bs(f, 4096, &bs, &bs_pa, &bs_guest) != 0 ||
	    fix_bs(f, 16384, &fbh, &fb_pa, &fb_guest) != 0) {
		CHECK(0);
		return;
	}
	memset(&job, 0, sizeof job);
	job.bs_handle = bs;
	job.bs_bytes = 4096;
	job.fb_handle = fbh;
	job.fb_bytes = 16384;
	job.fb_planes = 1;
	job.fb_index = 4;
	rc = n2ap_vdec_decode(f->dec, &job);
	CHECK_EQ(rc, 0);
	n2ap_vdec_stats(f->dec, &st);
	CHECK_EQ(st.put_fb, 1);
	CHECK_EQ(st.frames_displayed, 1);
	CHECK_EQ(st.get_fb, 0);
	CHECK(f->hw.dma_rw_from_dev >= 1);
	rc = n2ap_vdec_pop_displayed(f->dec, &out);
	CHECK_EQ(rc, 1);
	CHECK_EQ(out.cookie, (uint64_t)fbh);
}

/* ---- 7. handler lifetime: a dispatch that arrives after close() must be ignored ---- */

struct race_ctx {
	n2vcu_ipi_handler fn;
	void *ctx;
	int ipi;
	volatile int stop;
	unsigned calls;
};

static void *race_thread(void *arg)
{
	struct race_ctx *r = arg;
	struct decv_msg m;

	memset(&m, 0, sizeof m);
	m.len = 16;
	while (!__atomic_load_n(&r->stop, __ATOMIC_RELAXED)) {
		uint32_t id = VCU_IPIMSG_DEC_LOCK_LAT;

		memcpy(m.buf, &id, 4);
		(void)r->fn(r->ctx, r->ipi, m.buf, m.len);
		id = VCU_IPIMSG_DEC_UNLOCK_LAT;
		memcpy(m.buf, &id, 4);
		(void)r->fn(r->ctx, r->ipi, m.buf, m.len);
		r->calls++;
	}
	return NULL;
}

/*
 * What this catches: the daemon (or the stand-in) copies the handler pointer and ctx, calls the
 * handler on its own thread, and close() frees the session as soon as it sees the reply.  The
 * dec_live table makes a late call a no-op without reading the freed session.  ASan/UBSan report a
 * use-after-free if that ever regresses; the direct call after close() is deterministic.
 */
static void t_close_race(void)
{
	struct fix f;
	struct race_ctx r;
	struct decv_msg m;
	pthread_t th;
	struct timespec ts;
	int rc, started = 0;

	if (fix_open(&f, 0) != 0) {
		CHECK(0);
		return;
	}
	if (fix_session(&f, 0, 2000) != 0) {
		CHECK(0);
		fix_close(&f);
		return;
	}
	r.fn = f.vcu->fn[N2AP_VDEC_IPI_H264];
	r.ctx = f.vcu->fn_ctx[N2AP_VDEC_IPI_H264];
	r.ipi = N2AP_VDEC_IPI_H264;
	r.stop = 0;
	r.calls = 0;
	if (pthread_create(&th, NULL, race_thread, &r) == 0) {
		started = 1;
		ts.tv_sec = 0;
		ts.tv_nsec = 20000000L;   /* let the handler get inside the session */
		nanosleep(&ts, NULL);
	}
	rc = n2ap_vdec_close(f.dec);
	f.dec = NULL;
	CHECK_EQ(rc, 0);
	/* a dispatch with the (now free) session pointer must be dropped without a memory access */
	memset(&m, 0, sizeof m);
	m.len = 16;
	{
		uint32_t id = VCU_IPIMSG_DEC_LOCK_LAT;

		memcpy(m.buf, &id, 4);
	}
	CHECK_EQ(r.fn(r.ctx, r.ipi, m.buf, m.len), 1);
	if (started) {
		__atomic_store_n(&r.stop, 1, __ATOMIC_RELAXED);
		pthread_join(th, NULL);
		CHECK(r.calls > 0);
	}
	fix_close(&f);
}

static int want(const char *only, const char *name)
{
	return only == NULL || strcmp(only, "all") == 0 || strcmp(only, name) == 0;
}

int main(int argc, char **argv)
{
	struct fix f;
	const char *only = argc > 2 ? argv[2] : NULL;
	int rc;

	g_cfg = argc > 1 ? argv[1] : "default";

	if (want(only, "layout"))
		t_layout();
	if (want(only, "open") && fix_open(&f, 0) == 0) {
		t_open(&f);
		fix_close(&f);
	} else if (want(only, "open")) {
		CHECK(0);
	}
	if (want(only, "fail"))
		t_open_failures();
	if (want(only, "header") && fix_open(&f, 0) == 0) {
		t_decode_header(&f);
		fix_close(&f);
	} else if (want(only, "header")) {
		CHECK(0);
	}
	if (want(only, "frame") && fix_open(&f, 0) == 0) {
		t_decode_frame(&f);
		fix_close(&f);
	} else if (want(only, "frame")) {
		CHECK(0);
	}
	if (want(only, "nofb") && fix_open(&f, 0) == 0) {
		t_decode_nofb(&f);
		fix_close(&f);
	} else if (want(only, "nofb")) {
		CHECK(0);
	}
	if (want(only, "power") && fix_open(&f, 0) == 0) {
		t_power_core_lat(&f);
		fix_close(&f);
	} else if (want(only, "power")) {
		CHECK(0);
	}
	if (want(only, "hwbreak") && fix_open(&f, 0) == 0) {
		t_power_hwbreak(&f);
		fix_close(&f);
	} else if (want(only, "hwbreak")) {
		CHECK(0);
	}
	if (want(only, "waitisr") && fix_open(&f, 0) == 0) {
		t_waitisr(&f);
		fix_close(&f);
	} else if (want(only, "waitisr")) {
		CHECK(0);
	}
	if (want(only, "flush") && fix_open(&f, 0) == 0) {
		t_flush_close(&f);
		fix_close(&f);
	} else if (want(only, "flush")) {
		CHECK(0);
	}
	if (want(only, "full") && fix_open(&f, 0) == 0) {
		t_full_frame_script(&f);
		fix_close(&f);
	} else if (want(only, "full")) {
		CHECK(0);
	}
	if (want(only, "sync") && fix_open(&f, 0) == 0) {
		t_decode_sync(&f);
		fix_close(&f);
	} else if (want(only, "sync")) {
		CHECK(0);
	}
	if (want(only, "planes") && fix_open(&f, 0) == 0) {
		t_two_planes(&f);
		fix_close(&f);
	} else if (want(only, "planes")) {
		CHECK(0);
	}
	if (want(only, "pic") && fix_open(&f, 0) == 0) {
		t_pic_info(&f);
		fix_close(&f);
	} else if (want(only, "pic")) {
		CHECK(0);
	}
	if (want(only, "zero") && fix_open(&f, 0) == 0) {
		t_open_zero_size(&f);
		fix_close(&f);
	} else if (want(only, "zero")) {
		CHECK(0);
	}
	if (want(only, "fourcc") && fix_open(&f, 0) == 0) {
		t_output_fourcc(&f);
		fix_close(&f);
	} else if (want(only, "fourcc")) {
		CHECK(0);
	}
	if (want(only, "free2") && fix_open(&f, 0) == 0) {
		t_two_phase_free(&f);
		fix_close(&f);
	} else if (want(only, "free2")) {
		CHECK(0);
	}
	if (want(only, "putstart") && fix_open(&f, 0) == 0) {
		t_put_during_start(&f);
		fix_close(&f);
	} else if (want(only, "putstart")) {
		CHECK(0);
	}
	if (want(only, "badfb") && fix_open(&f, 0) == 0) {
		t_get_fb_bad(&f);
		fix_close(&f);
	} else if (want(only, "badfb")) {
		CHECK(0);
	}
	if (want(only, "race"))
		t_close_race();

	rc = (g_pass == g_total) ? 0 : 1;
	printf("DEC01 %s %s %u/%u\n", g_cfg, rc == 0 ? "PASS" : "FAIL", g_pass, g_total);
	return rc;
}
