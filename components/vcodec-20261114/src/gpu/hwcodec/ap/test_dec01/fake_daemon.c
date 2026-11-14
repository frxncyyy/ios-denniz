/* SPDX-License-Identifier: MIT */
/*
 * fake_daemon.c -- see fake_daemon.h.  One thread: fetch any queued AP message, dispatch on msg_id,
 * answer it, and (for START) send the VCU -> AP requests the script asks for in the order
 * facts/VDEC_PROTOCOL.md 2.1 lists them.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fake_daemon.h"
#include "mtk_vdec_ipi.h"

/* The vsi offsets the daemon uses, from the entry count it allocated (mtk_vdec_ipi.h). */
static uint32_t decd_fb_ring_bytes(unsigned n) { return 48u * n + 16u; }
static uint32_t decd_bs_ring_bytes(unsigned n) { return 8u * n + 16u; }

static struct vdec_vsi_tail *decd_tail(struct decd *d)
{
	uint32_t off = decd_bs_ring_bytes(d->ring_entries) + 2u * decd_fb_ring_bytes(d->ring_entries);

	return (struct vdec_vsi_tail *)((uint8_t *)d->vsi + off);
}

static void decd_reply(struct decd *d, uint32_t msg_id, int32_t status, uint64_t ap_inst, uint32_t extra)
{
	struct decv_msg m;

	memset(&m, 0, sizeof m);
	m.ipi_id = d->ipi;
	m.len = 24;
	{
		uint32_t id = msg_id;

		memcpy(m.buf + 0, &id, 4);
		memcpy(m.buf + 4, &status, 4);
	}
	memcpy(m.buf + 8, &ap_inst, 8);
	if (extra != 0u)
		memcpy(m.buf + 16, &extra, 4);
	(void)decv_daemon_reply(d->vcu, d->ipi, &m);
}

static void decd_request(struct decd *d, uint32_t msg_id, int32_t status, int *out)
{
	struct decv_msg m;

	memset(&m, 0, sizeof m);
	m.ipi_id = d->ipi;
	m.len = 16;
	memcpy(m.buf + 0, &msg_id, 4);
	memcpy(m.buf + 4, &status, 4);
	{
		uint64_t inst = 0;

		memcpy(m.buf + 8, &inst, 8);
	}
	{
		int rc = decv_daemon_request(d->vcu, d->ipi, &m);

		if (out != NULL)
			*out = rc;
	}
}

static void decd_do_start(struct decd *d, const struct decv_msg *in)
{
	struct vdec_vsi_tail *t = decd_tail(d);
	int rc = 0;

	if (in->len >= 12) {
		memcpy(&d->last_start_data[0], in->buf + 8, 4);
		if (in->len >= 20) {
			memcpy(&d->last_start_data[1], in->buf + 12, 4);
			memcpy(&d->last_start_data[2], in->buf + 16, 4);
		}
	}
	/* what the vendor library does after parsing the header: picture info + change flags */
	t->pic.pic_w = d->pic_w;
	t->pic.pic_h = d->pic_h;
	t->pic.buf_w = d->buf_w;
	t->pic.buf_h = d->buf_h;
	t->pic.fb_sz[0] = d->fb_sz;
	t->pic.fb_sz[1] = d->fb_sz_c;
	t->pic.bitdepth = 8;
	t->pic.layout_mode = 0;
	t->pic.fourcc = d->fourcc;
	t->crop.left = (int32_t)d->crop_l;
	t->crop.top = (int32_t)d->crop_t;
	t->crop.width = d->crop_w;
	t->crop.height = d->crop_h;
	t->dec.dpb_sz = d->dpb_sz;
	t->dec.vdec_changed_info = d->changed;

	/* the request sequence of one H.264 frame (facts/VDEC_PROTOCOL.md 2.3 item 4) */
	if (d->do_lock_lat)
		decd_request(d, VCU_IPIMSG_DEC_LOCK_LAT, 0, NULL);
	if (d->do_wait_lat) {
		int32_t core = MTK_VDEC_LAT;

		d->last_wait_seen = core;
		decd_request(d, VCU_IPIMSG_DEC_WAITISR, core, &rc);
	}
	if (d->do_unlock_lat)
		decd_request(d, VCU_IPIMSG_DEC_UNLOCK_LAT, 0, NULL);
	if (d->do_lock_core)
		decd_request(d, VCU_IPIMSG_DEC_LOCK_CORE, 0, NULL);
	if (d->do_wait_core) {
		int32_t core = MTK_VDEC_CORE;

		d->last_wait_seen = core;
		decd_request(d, VCU_IPIMSG_DEC_WAITISR, core, &rc);
	}
	if (d->do_unlock_core)
		decd_request(d, VCU_IPIMSG_DEC_UNLOCK_CORE, 0, NULL);
	if (d->do_get_fb)
		decd_request(d, VCU_IPIMSG_DEC_GET_FRAME_BUFFER, 0, NULL);
	/* what the daemon's HAL reads out of vsi.dec for this job (after GET, if it asked for one) */
	d->last_fb_index = t->dec.index;
	d->last_fb_va = t->dec.vdec_fb_va;
	d->last_fb_dma[0] = t->dec.fb_dma[0];
	d->last_fb_dma[1] = t->dec.fb_dma[1];
	d->last_fb_fd[0] = t->dec.fb_fd[0];
	d->last_fb_fd[1] = t->dec.fb_fd[1];
	d->last_fb_planes = t->dec.fb_num_planes;
	/* list_disp/list_free are filled whatever the transport: in synchronous mode the vendor queues
	 * them and sends no PUT_FRAME_BUFFER at all (opus-dec1: P=0 over 12 AUs), so the AP must harvest
	 * after the ACK.  The cookie is the one in vsi.dec, which the client wrote before START. */
	{
		uint32_t i;

		for (i = 0; i < d->disp_frames && i < d->ring_entries; i++) {
			struct ring_fb_list *l = &d->vsi->list_disp;
			struct vdec_ipi_fb e;
			uint64_t cookie = t->dec.vdec_fb_va != 0u ? t->dec.vdec_fb_va : d->last_fb_va;

			memset(&e, 0, sizeof e);
			/* the vendor echoes the cookie it was given (vdec_fb_va) */
			e.vdec_fb_va = cookie != 0u ? cookie : 0x1000u + i;
			e.poc = (int32_t)(i * 2u);
			e.timestamp = 0x5000u + i;
			e.y_fb_dma = 0xda800000u + 0x1000u * i;
			l->fb_list[l->write_idx] = e;
			l->write_idx = (l->write_idx + 1u) % d->ring_entries;
			__atomic_add_fetch(&l->count, 1u, __ATOMIC_RELEASE);
		}
		for (i = 0; i < d->free_frames && i < d->ring_entries; i++) {
			struct ring_fb_list *l = &d->vsi->list_free;
			struct vdec_ipi_fb e;

			memset(&e, 0, sizeof e);
			e.vdec_fb_va = (i == 0u && d->free_fb_cookie && t->dec.vdec_fb_va != 0u) ?
			               t->dec.vdec_fb_va : 0x2000u + i;
			l->fb_list[l->write_idx] = e;
			l->write_idx = (l->write_idx + 1u) % d->ring_entries;
			__atomic_add_fetch(&l->count, 1u, __ATOMIC_RELEASE);
		}
	}
	/* The vendor returns every consumed bitstream cookie in list_free_bs within the same START. */
	if (t->dec.vdec_bs_va != 0u) {
		struct ring_bs_list *l = &d->vsi->list_free_bs;

		d->last_bs_cookie = t->dec.vdec_bs_va;
		l->vdec_bs_va_list[l->write_idx] = d->last_bs_cookie;
		l->write_idx = (l->write_idx + 1u) % d->ring_entries;
		__atomic_add_fetch(&l->count, 1u, __ATOMIC_RELEASE);
		d->bs_pushes++;
	}
	if (d->do_put_fb)
		decd_request(d, VCU_IPIMSG_DEC_PUT_FRAME_BUFFER, 0, NULL);
	decd_reply(d, VCU_IPIMSG_DEC_START_ACK, d->start_status, d->ap_cookie, 0);
}

static void decd_dispatch(struct decd *d, const struct decv_msg *in)
{
	uint32_t msg_id = 0, inst = 0;
	uint64_t ap_inst = 0;

	if (in->len < 4u) {
		d->short_msgs++;
		return;
	}
	memcpy(&msg_id, in->buf + 0, 4);
	if (in->len >= 8u)
		memcpy(&inst, in->buf + 4, 4);
	(void)ap_inst;

	switch (msg_id) {
	case AP_IPIMSG_DEC_INIT: {
		uint64_t ap_inst = 0;

		d->init_msgs++;
		if (in->len >= 16u)
			memcpy(&ap_inst, in->buf + 8, 8);   /* the kernel echoes ap_inst_addr */
		d->ap_cookie = ap_inst;
		decd_reply(d, VCU_IPIMSG_DEC_INIT_ACK, d->init_status, ap_inst, d->vsi_guest);
		return;
	}
	case AP_IPIMSG_DEC_QUERY_CAP: {
		struct vdec_vcu_ipi_query_cap_ack a;
		uint32_t cap_id = 0;

		d->cap_msgs++;
		if (in->len >= 8u)
			memcpy(&cap_id, in->buf + 4, 4);
		d->last_cap_id = cap_id;
		memset(&a, 0, sizeof a);
		a.msg_id = VCU_IPIMSG_DEC_QUERY_CAP_ACK;
		a.status = d->cap_status;
		a.ap_inst_addr = 0;
		if (in->len >= 16u)
			memcpy(&a.ap_inst_addr, in->buf + 8, 8);
		a.id = cap_id;
		/* the daemon's own capability area, filled with the literal list of
		 * facts/VDEC_PROTOCOL.md 2.4 (a subset: NV12 + MM21 output, H264/H265 input) */
		{
			uint32_t area = decv_dmem_alloc(d->vcu, 128u * 64u, 16u);
			struct mtk_video_fmt *fmts = n2vcu_map_dm_addr(d->vcu, area);

			if (fmts != NULL) {
				if (cap_id == GET_PARAM_CAPABILITY_SUPPORTED_FORMATS) {
					fmts[0].fourcc = 0x3231564eu;   /* NV12, MTK_FMT_FRAME */
					fmts[0].type = MTK_FMT_FRAME;
					fmts[0].num_planes = 1;
					fmts[1].fourcc = 0x31324d4du;   /* MM21 */
					fmts[1].type = MTK_FMT_FRAME;
					fmts[1].num_planes = 1;
					fmts[2].fourcc = 0x34363248u;   /* H264, MTK_FMT_DEC */
					fmts[2].type = MTK_FMT_DEC;
					fmts[2].num_planes = 1;
					fmts[3].fourcc = 0x35363248u;   /* H265 */
					fmts[3].type = MTK_FMT_DEC;
					fmts[3].num_planes = 1;
				} else {
					struct mtk_codec_framesizes *fs = (struct mtk_codec_framesizes *)fmts;

					fs[0].fourcc = 0x34363248u;
					fs[0].profile = 55;
					fs[0].level = 16;
					fs[0].stepwise.min_width = 16;
					fs[0].stepwise.max_width = 4096;
					fs[0].stepwise.step_width = 16;
					fs[0].stepwise.min_height = 16;
					fs[0].stepwise.max_height = 2176;
					fs[0].stepwise.step_height = 32;
				}
				a.vcu_data_addr = area;
			}
		}
		{
			struct decv_msg m;

			memset(&m, 0, sizeof m);
			m.ipi_id = d->ipi;
			m.len = sizeof a;
			memcpy(m.buf, &a, sizeof a);
			(void)decv_daemon_reply(d->vcu, d->ipi, &m);
		}
		return;
	}
	case AP_IPIMSG_DEC_SET_PARAM: {
		uint32_t id = 0;

		d->param_msgs++;
		if (in->len >= 12u)
			memcpy(&id, in->buf + 8, 4);
		d->last_param_id = id;
		if (id == SET_PARAM_FRAME_SIZE && in->len >= 20u) {
			memcpy(&d->frame_size[0], in->buf + 12, 4);
			memcpy(&d->frame_size[1], in->buf + 16, 4);
			d->frame_size_seen++;
		}
		d->last_param_count = 0;
		if (in->len > 12u) {
			uint32_t n = (in->len - 12u) / 4u;

			if (n > 8u)
				n = 8u;
			memcpy(d->last_param_data, in->buf + 12, n * 4u);
			d->last_param_count = n;
		}
		/* the generic ack is checked against the instance too (vdec_vcu_if.c:221-225); the
		 * client compares the 8 bytes at +8 with its cookie, so echo the cookie it sent. */
		decd_reply(d, VCU_IPIMSG_DEC_SET_PARAM_ACK, d->param_status, d->ap_cookie, 0);
		return;
	}
	case AP_IPIMSG_DEC_START:
		d->start_msgs++;
		decd_do_start(d, in);
		return;
	case AP_IPIMSG_DEC_RESET:
		d->reset_msgs++;
		decd_reply(d, VCU_IPIMSG_DEC_RESET_ACK, d->reset_status, d->ap_cookie, 0);
		return;
	case AP_IPIMSG_DEC_END:
		d->end_msgs++;
		decd_reply(d, VCU_IPIMSG_DEC_END_ACK, d->end_status, d->ap_cookie, 0);
		return;
	case AP_IPIMSG_DEC_DEINIT:
		d->deinit_msgs++;
		d->last_deinit_inst = inst;
		decd_reply(d, VCU_IPIMSG_DEC_DEINIT_ACK, d->deinit_status, d->ap_cookie, 0);
		return;
	default:
		d->unknown_msgs++;
		return;
	}
}

static void *decd_thread(void *arg)
{
	struct decd *d = arg;

	while (!__atomic_load_n(&d->stop, __ATOMIC_ACQUIRE)) {
		struct decv_msg m;

		memset(&m, 0, sizeof m);
		if (decv_daemon_fetch_any(d->vcu, &m, 20) != 0)
			continue;
		pthread_mutex_lock(&d->lock);
		decd_dispatch(d, &m);
		pthread_mutex_unlock(&d->lock);
	}
	return NULL;
}

int decd_start(struct decd *d, struct n2vcu *vcu, int ipi, unsigned ring_entries)
{
	memset(d, 0, sizeof *d);
	d->vcu = vcu;
	d->ipi = ipi;
	d->ring_entries = ring_entries != 0u ? ring_entries : N2AP_DEC_MAX_FB_NUM;
	d->pic_w = 320;
	d->pic_h = 240;
	d->buf_w = 320;
	d->buf_h = 256;
	d->fb_sz = 320u * 256u;
	d->crop_w = 320;
	d->crop_h = 240;
	d->dpb_sz = 9;
	d->changed = VDEC_RES_CHANGE | VDEC_CROP_CHANGED;
	d->fourcc = 0x5331324eu;   /* 'M21S' in the vendor's list, as facts/VDEC_PROTOCOL.md 2.4 shows */
	d->start_status = VDEC_IPI_MSG_STATUS_OK;
	if (pthread_mutex_init(&d->lock, NULL) != 0)
		return -ENOMEM;
	d->vsi_guest = decv_dmem_alloc(vcu, 4096u + 2u * decd_fb_ring_bytes(d->ring_entries), 64u);
	if (d->vsi_guest == 0u) {
		pthread_mutex_destroy(&d->lock);
		return -ENOMEM;
	}
	d->vsi = n2vcu_map_dm_addr(vcu, d->vsi_guest);
	if (d->vsi == NULL) {
		pthread_mutex_destroy(&d->lock);
		return -EIO;
	}
	d->started = 1;
	if (pthread_create(&d->th, NULL, decd_thread, d) != 0) {
		d->started = 0;
		pthread_mutex_destroy(&d->lock);
		return -ENOMEM;
	}
	return 0;
}

void decd_stop(struct decd *d)
{
	if (!d->started)
		return;
	__atomic_store_n(&d->stop, 1, __ATOMIC_RELEASE);
	pthread_join(d->th, NULL);
	d->started = 0;
	pthread_mutex_destroy(&d->lock);
}

void decd_script_full_frame(struct decd *d)
{
	d->do_lock_lat = d->do_wait_lat = d->do_unlock_lat = 1;
	d->do_lock_core = d->do_wait_core = d->do_unlock_core = 1;
	d->do_get_fb = d->do_put_fb = 1;
	d->disp_frames = 1;
}

void decd_script_header_only(struct decd *d)
{
	d->do_lock_lat = d->do_wait_lat = d->do_unlock_lat = 0;
	d->do_lock_core = d->do_wait_core = d->do_unlock_core = 0;
	d->do_get_fb = d->do_put_fb = 0;
	d->disp_frames = 0;
}

void decd_queue_disp(struct decd *d, unsigned n)
{
	struct vdec_vsi_tail *t = decd_tail(d);
	unsigned i;

	pthread_mutex_lock(&d->lock);
	for (i = 0; i < n && i < d->ring_entries; i++) {
		struct ring_fb_list *l = &d->vsi->list_disp;
		struct vdec_ipi_fb e;

		memset(&e, 0, sizeof e);
		e.vdec_fb_va = t->dec.vdec_fb_va != 0u ? t->dec.vdec_fb_va : 0x1000u + i;
		e.poc = (int32_t)(i * 2u);
		e.timestamp = 0x5000u + i;
		l->fb_list[l->write_idx] = e;
		l->write_idx = (l->write_idx + 1u) % d->ring_entries;
		__atomic_add_fetch(&l->count, 1u, __ATOMIC_RELEASE);
	}
	pthread_mutex_unlock(&d->lock);
}
