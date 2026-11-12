/* SPDX-License-Identifier: MIT */
/*
 * t_v41.c -- host test for the V41 package: the AP side of an MT6893 VENC session.
 *
 * The oracle is a scripted fake daemon (a host thread playing vpud) that pulls messages with the
 * n2vcu stand-in's VCU_GET_OBJECT equivalent, records every field the AP sent, answers with the
 * *_DONE message and issues the daemon-to-AP requests (POWER_ON, WAIT_ISR, PUT_BUFFER); plus a
 * recording fake n2vc_hw.  Expected values are literals from the kernel sources and from the
 * message structs of ap/mtk_venc_ipi.h; the layout of those structs is checked separately against
 * the kernel header itself (probe_kernel.c vs probe_mine.c in run_tests.sh).
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2ap_venc.h"
#include "n2vc_hw.h"
#include "vcu/n2vcu.h"

#include "fake_daemon.h"
#include "hw_fake.h"

static unsigned g_pass, g_fail;
static const char *g_build = "?";

#define CHECK(cond) do { \
	if (cond) { g_pass++; } else { \
		g_fail++; \
		if (g_fail <= 40) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
	} \
} while (0)

#define CHECK_EQ_U32(got, want) do { \
	uint32_t _g = (uint32_t)(got), _w = (uint32_t)(want); \
	if (_g == _w) { g_pass++; } else { \
		g_fail++; \
		if (g_fail <= 40) { fprintf(stderr, "FAIL %s:%d: %s = 0x%08x, want 0x%08x\n", \
			__FILE__, __LINE__, #got, _g, _w); } \
	} \
} while (0)

#define CHECK_EQ_I(got, want) do { \
	long long _g = (long long)(got), _w = (long long)(want); \
	if (_g == _w) { g_pass++; } else { \
		g_fail++; \
		if (g_fail <= 40) { fprintf(stderr, "FAIL %s:%d: %s = %lld, want %lld\n", \
			__FILE__, __LINE__, #got, _g, _w); } \
	} \
} while (0)

#define CHECK_EQ_U64(got, want) do { \
	uint64_t _g = (uint64_t)(got), _w = (uint64_t)(want); \
	if (_g == _w) { g_pass++; } else { \
		g_fail++; \
		if (g_fail <= 40) { fprintf(stderr, "FAIL %s:%d: %s = 0x%llx, want 0x%llx\n", \
			__FILE__, __LINE__, #got, (unsigned long long)_g, (unsigned long long)_w); } \
	} \
} while (0)

/* Power API hammer for the TSan build: the refcount and the hw->power call must be one unit. */
struct power_hammer {
	struct n2ap_venc *e;
	unsigned rounds;
	int fails;
};

static void *power_hammer_main(void *arg)
{
	struct power_hammer *p = arg;
	unsigned i;

	for (i = 0; i < p->rounds; i++) {
		if (n2ap_venc_power(p->e, 1) != 0)
			p->fails++;
		if (n2ap_venc_power(p->e, 0) != 0)
			p->fails++;
	}
	return NULL;
}

/* ---- the scripted fake daemon -------------------------------------------------------------- */

struct dc_cfg {
	int init_status, param_status, enc_status, deinit_status, cap_status;
	int init_bad_inst;
	int cap_bad_inst;
	int power_on, power_off, wait_isr, put_buffer, write_bs, skip_encode_reply;
	int encode_ack;              /* answer ENCODE with ENCODE_ACK, like libvpud_vcodec */
	int free_without_put;        /* fill list_free in the ENCODE handler, send no PUT_BUFFER */
	int put_buffer_late;         /* with encode_ack: PUT_BUFFER after the ACK, from the daemon */
	unsigned late_ms;
	int isr_no_irq, short_done, cap_bad_addr;
	int reget_before_reply;      /* emulate vpud's receiver: GET_OBJECT again before the reply */
	uint32_t irq_status;
	uint32_t free_size, free_key;
	uint32_t enc_size, enc_key, enc_state;
	uint32_t cap_entries;
};

struct dc_rec {
	unsigned n_init, n_param, n_encode, n_deinit, n_cap, n_unknown;
	uint32_t init_len, init_reserved, init_msg_id;
	uint64_t init_inst;
	unsigned nparams;
	struct { uint32_t id, item, value, vcu_addr, len; } param[64];
	uint32_t cfg_input, cfg_w, cfg_h, cfg_bw, cfg_bh, cfg_fps, cfg_br, cfg_gop, cfg_intra;
	uint32_t cfg_profile, cfg_level, cfg_prepend, cfg_bitratemode, cfg_iqp, cfg_maxqp;
	uint32_t cfg_color0, cfg_scenario, cfg_operationrate, cfg_wfd, cfg_sync_mode;
	uint32_t enc_len, enc_msg_id, enc_vcu_addr, enc_planes, enc_mode, enc_bsaddr, enc_bssize;
	uint32_t enc_input, enc_input_size, enc_input_fd, enc_bs_fd, enc_meta, enc_qpmap;
	uint64_t enc_bsva, enc_fbva;
	uint32_t wait_rc, wait_irq_status, wait_timeout;
	uint32_t put_rc, put_count_after;
	unsigned n_late_put;
	uint32_t power_on_rc, power_off_rc;
	uint32_t deinit_msg_id, deinit_vcu_addr, deinit_len;
	uint32_t cap_id, cap_inst, cap_data_lo, cap_data_hi, cap_len, cap_vcu_addr;
	uint64_t cookie;
	unsigned vcu_addr_bad;
};

struct dc {
	pthread_mutex_t lock;
	pthread_t th;
	struct n2vcu *vcu;
	struct hwfake *hw;
	uint32_t vsi_addr;
	uint32_t cap_addr;
	int run;
	int pause;
	unsigned daemon_fail;
	char daemon_msg[256];
	struct dc_cfg cfg;
	struct dc_rec rec;
};

static void dc_cfg_defaults(struct dc *d)
{
	memset(&d->cfg, 0, sizeof(d->cfg));
	d->cfg.irq_status = 0x4;    /* MTK_VENC_IRQ_STATUS_FRM */
	d->cfg.enc_size = 0x1200;
	d->cfg.enc_key = 1;
	d->cfg.enc_state = VEN_IPI_MSG_ENC_STATE_FRAME;
	d->cfg.free_size = 0x1234;
	d->cfg.free_key = 1;
	d->cfg.cap_entries = 2;
	d->pause = 0;
}

static void dc_fail(struct dc *d, const char *msg)
{
	pthread_mutex_lock(&d->lock);
	if (d->daemon_fail == 0) {
		snprintf(d->daemon_msg, sizeof(d->daemon_msg), "%s", msg);
	}
	d->daemon_fail++;
	pthread_mutex_unlock(&d->lock);
}

static struct venc_vsi *dc_vsi(struct dc *d)
{
	return (struct venc_vsi *)n2a32_ptr(n2vcu_test_mem(d->vcu), d->vsi_addr);
}

/* Put one entry in venc_vsi.list_free, as the daemon does before a PUT_BUFFER (or, for the
 * synchronous sequence-header answer, before the ENCODE_ACK with no PUT_BUFFER at all). */
static void dc_fill_free(struct dc *d, struct venc_vsi *vsi, const struct dc_cfg *cfg)
{
	pthread_mutex_lock(&d->lock);
	/* The AP may read the ring concurrently (right after an ENCODE_ACK), so publish the entry
	 * with atomics and set `count` last with release, mirroring the daemon's fill-then-PUT_BUFFER
	 * order.  The AP's drain uses an acquire load of count. */
	vsi->list_free.read_idx = 0;
	vsi->list_free.write_idx = 1;
	__atomic_store_n(&vsi->list_free.venc_bs_va_list[0], vsi->venc.venc_bs_va, __ATOMIC_RELAXED);
	__atomic_store_n(&vsi->list_free.venc_fb_va_list[0], vsi->venc.venc_fb_va, __ATOMIC_RELAXED);
	__atomic_store_n(&vsi->list_free.bs_size[0], cfg->free_size, __ATOMIC_RELAXED);
	__atomic_store_n(&vsi->list_free.is_key_frm[0], cfg->free_key, __ATOMIC_RELAXED);
	__atomic_store_n(&vsi->list_free.count, 1, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&d->lock);
}

static void dc_snapshot_config(struct dc *d, struct venc_vsi *vsi)
{
	struct dc_rec *r = &d->rec;

	r->cfg_input = vsi->config.input_fourcc;
	r->cfg_w = vsi->config.pic_w;
	r->cfg_h = vsi->config.pic_h;
	r->cfg_bw = vsi->config.buf_w;
	r->cfg_bh = vsi->config.buf_h;
	r->cfg_fps = vsi->config.framerate;
	r->cfg_br = vsi->config.bitrate;
	r->cfg_gop = vsi->config.gop_size;
	r->cfg_intra = vsi->config.intra_period;
	r->cfg_profile = vsi->config.profile;
	r->cfg_level = vsi->config.level;
	r->cfg_prepend = vsi->config.prependheader;
	r->cfg_bitratemode = vsi->config.bitratemode;
	r->cfg_iqp = vsi->config.i_qp;
	r->cfg_maxqp = vsi->config.max_qp;
	r->cfg_color0 = vsi->config.color_desc.color_primaries;
	r->cfg_scenario = vsi->config.scenario;
	r->cfg_operationrate = vsi->config.operationrate;
	r->cfg_wfd = vsi->config.wfd;
	r->cfg_sync_mode = vsi->sync_mode;
}

static void dc_handle(struct dc *d, int ipi_id, uint8_t *buf, uint32_t len)
{
	uint32_t msg_id;
	struct dc_cfg cfg;
	uint64_t cookie;

	memcpy(&msg_id, buf, sizeof(msg_id));
	pthread_mutex_lock(&d->lock);
	cfg = d->cfg;
	cookie = d->rec.cookie;
	pthread_mutex_unlock(&d->lock);

	switch (msg_id) {
	case AP_IPIMSG_ENC_INIT: {
		struct venc_ap_ipi_msg_init m;
		struct venc_vcu_ipi_msg_init out;
		struct venc_vsi *vsi = dc_vsi(d);

		if (len < sizeof(m)) {
			dc_fail(d, "short INIT");
			return;
		}
		memcpy(&m, buf, sizeof(m));
		memset(vsi, 0, sizeof(*vsi));
		vsi->sync_mode = 0;
		vsi->sizeimage[0] = 0x2fd000;
		vsi->sizeimage[1] = 0x180000;
		pthread_mutex_lock(&d->lock);
		d->rec.n_init++;
		d->rec.init_len = len;
		d->rec.init_msg_id = m.msg_id;
		d->rec.init_reserved = m.reserved;
		d->rec.init_inst = m.venc_inst;
		d->rec.cookie = m.venc_inst;
		pthread_mutex_unlock(&d->lock);

		memset(&out, 0, sizeof(out));
		out.msg_id = VCU_IPIMSG_ENC_INIT_DONE;
		out.status = cfg.init_status;
		out.venc_inst = cfg.init_bad_inst ? (m.venc_inst ^ 0x10u) : m.venc_inst;
		out.vcu_inst_addr = d->vsi_addr;
		(void)n2vcu_test_set(d->vcu, ipi_id, &out, sizeof(out));
		return;
	}
	case AP_IPIMSG_ENC_SET_PARAM: {
		struct venc_ap_ipi_msg_set_param m;
		struct venc_vcu_ipi_msg_set_param out;
		struct venc_vsi *vsi = dc_vsi(d);

		if (len < sizeof(m)) {
			dc_fail(d, "short SET_PARAM");
			return;
		}
		memcpy(&m, buf, sizeof(m));
		pthread_mutex_lock(&d->lock);
		if (d->rec.nparams < 64u) {
			d->rec.param[d->rec.nparams].id = m.param_id;
			d->rec.param[d->rec.nparams].item = m.data_item;
			d->rec.param[d->rec.nparams].value = m.data[0];
			d->rec.param[d->rec.nparams].vcu_addr = m.vcu_inst_addr;
			d->rec.param[d->rec.nparams].len = len;
		}
		d->rec.nparams++;
		d->rec.n_param++;
		if (m.vcu_inst_addr != d->vsi_addr) {
			d->rec.vcu_addr_bad++;
		}
		if (m.param_id == VENC_SET_PARAM_ENC) {
			dc_snapshot_config(d, vsi);
			/* the VCU owns these two after VENC_SET_PARAM_ENC */
			vsi->sizeimage[0] = 0x300000;
			vsi->sizeimage[1] = 0x180000;
			vsi->sizeimage[2] = 0;
			vsi->sync_mode = 1;
		}
		if (m.param_id == VENC_SET_PARAM_COLOR_DESC) {
			d->rec.cfg_color0 = vsi->config.color_desc.color_primaries;
		}
		pthread_mutex_unlock(&d->lock);

		memset(&out, 0, sizeof(out));
		out.msg_id = VCU_IPIMSG_ENC_SET_PARAM_DONE;
		out.status = cfg.param_status;
		out.venc_inst = cookie;   /* stored from the INIT message, echoed like vpud */
		out.param_id = m.param_id;
		out.data_item = m.data_item;
		(void)n2vcu_test_set(d->vcu, ipi_id, &out, sizeof(out));
		return;
	}
	case AP_IPIMSG_ENC_ENCODE: {
		struct venc_ap_ipi_msg_enc m;
		struct venc_vcu_ipi_msg_enc out;
		struct venc_vsi *vsi = dc_vsi(d);
		struct venc_vcu_ipi_msg_common req;
		struct venc_vcu_ipi_msg_waitisr wr;
		uint32_t after = 0;
		int hrc = 0;
		static const uint8_t pattern[16] = {
			0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
			0x95, 0xa8, 0x14, 0x01, 0x6e, 0x9b, 0x80, 0x80
		};

		if (len < sizeof(m)) {
			dc_fail(d, "short ENCODE");
			return;
		}
		memcpy(&m, buf, sizeof(m));
		pthread_mutex_lock(&d->lock);
		d->rec.n_encode++;
		d->rec.enc_len = len;
		d->rec.enc_msg_id = m.msg_id;
		d->rec.enc_vcu_addr = m.vcu_inst_addr;
		d->rec.enc_planes = m.fb_num_planes;
		d->rec.enc_mode = m.bs_mode;
		d->rec.enc_bsaddr = m.bs_addr;
		d->rec.enc_bssize = m.bs_size;
		d->rec.enc_input = m.input_addr[0];
		d->rec.enc_input_size = m.input_size[0];
		d->rec.enc_input_fd = (uint32_t)(uint16_t)m.input_fd[0];
		d->rec.enc_bs_fd = (uint32_t)(uint16_t)m.bs_fd;
		d->rec.enc_meta = m.meta_size;
		d->rec.enc_qpmap = m.qpmap;
		if (m.vcu_inst_addr != d->vsi_addr) {
			d->rec.vcu_addr_bad++;
		}
		d->rec.enc_bsva = vsi->venc.venc_bs_va;
		d->rec.enc_fbva = vsi->venc.venc_fb_va;
		pthread_mutex_unlock(&d->lock);

		if (cfg.power_on) {
			memset(&req, 0, sizeof(req));
			req.msg_id = VCU_IPIMSG_ENC_POWER_ON;
			hrc = n2vcu_test_set(d->vcu, ipi_id, &req, sizeof(req));
			pthread_mutex_lock(&d->lock);
			d->rec.power_on_rc = (uint32_t)hrc;
			pthread_mutex_unlock(&d->lock);
		}
		if (cfg.wait_isr) {
			if (!cfg.isr_no_irq) {
				hwfake_set_irq(d->hw, cfg.irq_status);
			}
			memset(&wr, 0, sizeof(wr));
			wr.msg_id = VCU_IPIMSG_ENC_WAIT_ISR;
			wr.venc_inst = (uint64_t)(uintptr_t)vsi;
			hrc = n2vcu_test_set(d->vcu, ipi_id, &wr, sizeof(wr));
			pthread_mutex_lock(&d->lock);
			d->rec.wait_rc = (uint32_t)hrc;
			d->rec.wait_irq_status = wr.irq_status;
			d->rec.wait_timeout = wr.timeout;
			pthread_mutex_unlock(&d->lock);
		}
		if (cfg.put_buffer) {
			dc_fill_free(d, vsi, &cfg);
			memset(&req, 0, sizeof(req));
			req.msg_id = VCU_IPIMSG_ENC_PUT_BUFFER;
			hrc = n2vcu_test_set(d->vcu, ipi_id, &req, sizeof(req));
			after = vsi->list_free.count;
			pthread_mutex_lock(&d->lock);
			d->rec.put_rc = (uint32_t)hrc;
			d->rec.put_count_after = after;
			pthread_mutex_unlock(&d->lock);
		}
		if (cfg.free_without_put) {
			/* The synchronous answer: the ring is filled before the completion message
			 * and the AP drains it itself (kernel mtk_enc_put_buf). */
			dc_fill_free(d, vsi, &cfg);
		}
		if (cfg.write_bs) {
			(void)hwfake_dma_write_pa(d->hw, m.bs_addr, 0, pattern, sizeof(pattern));
		}
		if (cfg.power_off) {
			memset(&req, 0, sizeof(req));
			req.msg_id = VCU_IPIMSG_ENC_POWER_OFF;
			hrc = n2vcu_test_set(d->vcu, ipi_id, &req, sizeof(req));
			pthread_mutex_lock(&d->lock);
			d->rec.power_off_rc = (uint32_t)hrc;
			pthread_mutex_unlock(&d->lock);
		}
		if (cfg.skip_encode_reply) {
			pthread_mutex_lock(&d->lock);
			d->pause = 1;
			pthread_mutex_unlock(&d->lock);
			return;
		}
		memset(&out, 0, sizeof(out));
		out.msg_id = cfg.encode_ack ? VCU_IPIMSG_ENC_ENCODE_ACK : VCU_IPIMSG_ENC_ENCODE_DONE;
		out.status = cfg.enc_status;
		out.venc_inst = cookie;
		out.state = cfg.enc_state;
		out.is_key_frm = cfg.enc_key;
		out.bs_size = cfg.enc_size;
		(void)n2vcu_test_set(d->vcu, ipi_id, &out, cfg.short_done ? 24u :
		                     (cfg.encode_ack ? (uint32_t)sizeof(struct venc_vcu_ipi_msg_common) :
		                      (uint32_t)sizeof(out)));
		if (cfg.encode_ack && cfg.put_buffer_late) {
			struct timespec ts;

			ts.tv_sec = 0;
			ts.tv_nsec = (long)cfg.late_ms * 1000000L;
			(void)nanosleep(&ts, NULL);
			dc_fill_free(d, vsi, &cfg);
			memset(&req, 0, sizeof(req));
			req.msg_id = VCU_IPIMSG_ENC_PUT_BUFFER;
			hrc = n2vcu_test_set(d->vcu, ipi_id, &req, sizeof(req));
			pthread_mutex_lock(&d->lock);
			d->rec.put_rc = (uint32_t)hrc;
			d->rec.n_late_put++;
			pthread_mutex_unlock(&d->lock);
		}
		return;
	}
	case AP_IPIMSG_ENC_DEINIT: {
		struct venc_ap_ipi_msg_deinit m;
		struct venc_vcu_ipi_msg_deinit out;

		if (len < sizeof(m)) {
			dc_fail(d, "short DEINIT");
			return;
		}
		memcpy(&m, buf, sizeof(m));
		pthread_mutex_lock(&d->lock);
		d->rec.n_deinit++;
		d->rec.deinit_msg_id = m.msg_id;
		d->rec.deinit_vcu_addr = m.vcu_inst_addr;
		d->rec.deinit_len = len;
		if (m.vcu_inst_addr != d->vsi_addr) {
			d->rec.vcu_addr_bad++;
		}
		pthread_mutex_unlock(&d->lock);
		memset(&out, 0, sizeof(out));
		out.msg_id = VCU_IPIMSG_ENC_DEINIT_DONE;
		out.status = cfg.deinit_status;
		out.venc_inst = cookie;
		(void)n2vcu_test_set(d->vcu, ipi_id, &out, sizeof(out));
		return;
	}
	case AP_IPIMSG_ENC_QUERY_CAP: {
		struct venc_ap_ipi_query_cap m;
		struct venc_vcu_ipi_query_cap_ack out;
		uint8_t *cap = n2a32_ptr(n2vcu_test_mem(d->vcu), d->cap_addr);
		uint32_t size;

		if (len < sizeof(m)) {
			dc_fail(d, "short QUERY_CAP");
			return;
		}
		memcpy(&m, buf, sizeof(m));
		size = m.id == VENC_GET_PARAM_CAPABILITY_SUPPORTED_FORMATS
			? (uint32_t)sizeof(struct mtk_video_fmt)
			: (uint32_t)sizeof(struct mtk_codec_framesizes);
		memset(cap, 0, 64u * 36u);
		if (m.id == VENC_GET_PARAM_CAPABILITY_SUPPORTED_FORMATS) {
			struct mtk_video_fmt *f = (struct mtk_video_fmt *)cap;

			for (uint32_t i = 0; i < cfg.cap_entries && i < 64u; i++) {
				f[i].fourcc = (i == 0) ? N2AP_FOURCC_H264 : (i == 1) ? N2AP_FOURCC_H265 : 0;
				f[i].type = 1;   /* MTK_FMT_ENC */
				f[i].num_planes = 1;
			}
		} else if (m.id == VENC_GET_PARAM_CAPABILITY_FRAME_SIZES) {
			struct mtk_codec_framesizes *f = (struct mtk_codec_framesizes *)cap;

			if (cfg.cap_entries > 0u) {
				f[0].fourcc = N2AP_FOURCC_H264;
				f[0].profile = 0x11;
				f[0].level = 0x22;
				f[0].stepwise.min_width = 1920;
				f[0].stepwise.max_width = 4096;
				f[0].stepwise.step_width = 16;
				f[0].stepwise.min_height = 1080;
				f[0].stepwise.max_height = 2160;
				f[0].stepwise.step_height = 16;
			}
		}
		pthread_mutex_lock(&d->lock);
		d->rec.n_cap++;
		d->rec.cap_id = m.id;
		d->rec.cap_inst = (uint32_t)m.ap_inst_addr;
		d->rec.cap_data_lo = (uint32_t)m.ap_data_addr;
		d->rec.cap_data_hi = (uint32_t)(m.ap_data_addr >> 32);
		d->rec.cap_len = len;
		pthread_mutex_unlock(&d->lock);
		(void)size;

		memset(&out, 0, sizeof(out));
		out.msg_id = VCU_IPIMSG_ENC_QUERY_CAP_ACK;
		out.status = cfg.cap_status;
		out.ap_inst_addr = cfg.cap_bad_inst ? (m.ap_inst_addr ^ 1u) : m.ap_inst_addr;
		out.id = m.id;
		out.ap_data_addr = m.ap_data_addr;
		out.vcu_data_addr = cfg.cap_bad_addr ? 0u : d->cap_addr;
		pthread_mutex_lock(&d->lock);
		d->rec.cap_vcu_addr = out.vcu_data_addr;
		pthread_mutex_unlock(&d->lock);
		(void)n2vcu_test_set(d->vcu, ipi_id, &out, sizeof(out));
		return;
	}
	default:
		pthread_mutex_lock(&d->lock);
		d->rec.n_unknown++;
		pthread_mutex_unlock(&d->lock);
		return;
	}
}

static void *daemon_main(void *arg)
{
	struct dc *d = arg;
	uint8_t buf[80];
	int id = -1;
	uint32_t len = 0;

	for (;;) {
		int run, pause, reget;

		pthread_mutex_lock(&d->lock);
		run = d->run;
		pause = d->pause;
		pthread_mutex_unlock(&d->lock);
		if (!run) {
			break;
		}
		if (pause) {
			struct timespec ts = { 0, 1000000L };

			(void)nanosleep(&ts, NULL);
			continue;
		}
		if (n2vcu_test_get(d->vcu, N2AP_IPI_VENC_H264, &id, buf, sizeof(buf), &len, 100u) != 0) {
			continue;
		}
		/* vpud's receiver thread hands the message to a service thread and returns to
		 * GET_OBJECT at once; the reply then arrives later.  Emulate that with a
		 * non-blocking second GET before handling (it only sets "dispatched"). */
		pthread_mutex_lock(&d->lock);
		reget = d->cfg.reget_before_reply;
		pthread_mutex_unlock(&d->lock);
		if (reget) {
			n2vcu_test_reget(d->vcu, N2AP_IPI_VENC_H264);
		}
		dc_handle(d, id, buf, len);
	}
	return NULL;
}

static void dc_start(struct dc *d, struct n2vcu *vcu, struct hwfake *hw)
{
	memset(d, 0, sizeof(*d));
	pthread_mutex_init(&d->lock, NULL);
	d->vcu = vcu;
	d->hw = hw;
	d->vsi_addr = n2vcu_test_dmem(vcu) + 0x1000u;
	d->cap_addr = n2vcu_test_dmem(vcu) + 0x8000u;
	d->run = 1;
	dc_cfg_defaults(d);
	pthread_create(&d->th, NULL, daemon_main, d);
}

static void dc_stop(struct dc *d)
{
	pthread_mutex_lock(&d->lock);
	d->run = 0;
	d->pause = 0;
	pthread_mutex_unlock(&d->lock);
	pthread_join(d->th, NULL);
}

static void dc_reset_records(struct dc *d)
{
	pthread_mutex_lock(&d->lock);
	{
		/* The session cookie is state, not a record: it must survive a reset. */
		uint64_t cookie = d->rec.cookie;

		memset(&d->rec, 0, sizeof(d->rec));
		d->rec.cookie = cookie;
	}
	pthread_mutex_unlock(&d->lock);
}

/* ---- helpers ------------------------------------------------------------------------------ */

static struct n2ap_venc *open_h264(struct n2vcu *vcu, const struct n2vc_hw *hw, unsigned timeout)
{
	struct n2ap_venc *e = NULL;
	int rc = n2ap_venc_open(&e, vcu, hw, N2AP_FOURCC_H264, timeout);

	CHECK_EQ_I(rc, 0);
	if (rc != 0) {
		return NULL;
	}
	CHECK(e != NULL);
	return e;
}

static void fill_pattern(uint8_t *p, uint32_t n, uint32_t seed)
{
	for (uint32_t i = 0; i < n; i++) {
		p[i] = (uint8_t)(seed + i * 7u);
	}
}

/* ---- tests -------------------------------------------------------------------------------- */

static void test_layout_literals(void)
{
	CHECK_EQ_U32(AP_IPIMSG_ENC_INIT, 0xC000u);
	CHECK_EQ_U32(AP_IPIMSG_ENC_SET_PARAM, 0xC001u);
	CHECK_EQ_U32(AP_IPIMSG_ENC_ENCODE, 0xC002u);
	CHECK_EQ_U32(AP_IPIMSG_ENC_DEINIT, 0xC003u);
	CHECK_EQ_U32(AP_IPIMSG_ENC_QUERY_CAP, 0xC004u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_INIT_DONE, 0xD000u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_SET_PARAM_DONE, 0xD001u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_ENCODE_DONE, 0xD002u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_DEINIT_DONE, 0xD003u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_QUERY_CAP_ACK, 0xD004u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_ENCODE_ACK, 0xD005u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_POWER_ON, 0xE000u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_POWER_OFF, 0xE001u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_WAIT_ISR, 0xE002u);
	CHECK_EQ_U32(VCU_IPIMSG_ENC_PUT_BUFFER, 0xE003u);
	CHECK_EQ_U32(VENC_SET_PARAM_ENC, 0u);
	CHECK_EQ_U32(VENC_SET_PARAM_PREPEND_HEADER, 7u);
	CHECK_EQ_U32(VENC_SET_PARAM_LOG, 29u);
	CHECK_EQ_U32(VENC_SET_PARAM_ENABLE_DUMMY_NAL, 30u);
	CHECK_EQ_U32(VENC_BS_MODE_SEQ_HDR, 2u);
	CHECK_EQ_U32(VENC_BS_MODE_FRAME, 3u);
	CHECK_EQ_U32(VENC_BS_MODE_FRAME_FINAL, 4u);
	CHECK_EQ_U32(sizeof(struct venc_ap_ipi_msg_init), 16u);
	CHECK_EQ_U32(sizeof(struct venc_ap_ipi_query_cap), 24u);
	CHECK_EQ_U32(sizeof(struct venc_vcu_ipi_query_cap_ack), 40u);
	CHECK_EQ_U32(sizeof(struct venc_ap_ipi_msg_set_param), 48u);
	CHECK_EQ_U32(sizeof(struct venc_ap_ipi_msg_enc), 76u);
	CHECK_EQ_U32(sizeof(struct venc_ap_ipi_msg_deinit), 8u);
	CHECK_EQ_U32(sizeof(struct venc_vcu_ipi_msg_common), 16u);
	CHECK_EQ_U32(sizeof(struct venc_vcu_ipi_msg_init), 24u);
	CHECK_EQ_U32(sizeof(struct venc_vcu_ipi_msg_set_param), 48u);
	CHECK_EQ_U32(sizeof(struct venc_vcu_ipi_msg_enc), 32u);
	CHECK_EQ_U32(sizeof(struct venc_vcu_ipi_msg_deinit), 16u);
	CHECK_EQ_U32(sizeof(struct venc_vcu_ipi_msg_waitisr), 24u);
	CHECK_EQ_U32(sizeof(struct venc_vsi), 2016u);
	CHECK_EQ_U32(sizeof(struct ring_input_list), 1552u);
	CHECK_EQ_U32(offsetof(struct venc_vsi, sync_mode), 1992u);
}

static void test_dma_direct(struct n2a32_mem *mem, const struct n2vc_hw *hw, struct hwfake *fake)
{
	uint32_t h = 0, pa = 0, guest = 0, gh = 0, gpa = 0, gguest = 0;
	uint32_t bh = 0, bpa = 0, bguest = 0, got_pa = 0, got_guest = 0, got_bytes = 0;
	uint8_t src[64], dst[64];

	for (unsigned i = 0; i < sizeof(src); i++) {
		src[i] = (uint8_t)(i + 1u);
	}
	memset(dst, 0, sizeof(dst));

	CHECK_EQ_I(n2vc_dma_guest_alloc(hw, mem, 0x2000, 0, &h, &pa, &guest), 0);
	CHECK(h != 0u);
	CHECK(pa >= RW_CLAIM_VCODEC_PA);
	CHECK_EQ_U32(guest & 0xfffu, 0u);
	CHECK_EQ_I(n2vc_dma_lookup_handle(h, &got_pa, &got_guest, &got_bytes), 0);
	CHECK_EQ_U32(got_pa, pa);
	CHECK_EQ_U32(got_guest, guest);
	CHECK_EQ_U32(got_bytes, 0x2000u);
	CHECK_EQ_I(n2vc_dma_guest_map(hw, mem, h, 0x1000, &gh), 0);
	CHECK_EQ_U32(gh, guest);
	CHECK_EQ_I(n2vc_dma_guest_map(hw, mem, h, 0x2001, &gh), -EINVAL);
	CHECK_EQ_I(n2vc_dma_guest_map(hw, mem, 0xdead, 0x1000, &gh), 0);
	CHECK_EQ_U32(gh & 0xfffu, 0u);
	{
		uint32_t again = 0;

		CHECK_EQ_I(n2vc_dma_guest_map(hw, mem, 0xdead, 0x800, &again), 0);
		CHECK_EQ_U32(again, gh);
		CHECK_EQ_I(n2vc_dma_lookup_pa(0, NULL, NULL, NULL), -ENOENT);
		CHECK_EQ_I(n2vc_dma_guest_unmap(hw, mem, gh), 0);
	}

	/* zero-filled by n2a32_mem_alloc */
	{
		uint8_t z[16];
		memcpy(z, n2a32_ptr(mem, guest), sizeof(z));
		CHECK_EQ_U32(z[0] | z[7] | z[15], 0u);
	}

	/* guest -> device */
	memcpy(n2a32_ptr(mem, guest + 0x40), src, sizeof(src));
	CHECK_EQ_I(n2vc_dma_sync(hw, mem, guest, 0x40, sizeof(src), 1), 0);
	CHECK_EQ_I(hwfake_dma_read(fake, h, 0x40, dst, sizeof(dst)), 0);
	CHECK(memcmp(src, dst, sizeof(src)) == 0);

	/* device -> guest */
	memset(dst, 0, sizeof(dst));
	for (unsigned i = 0; i < sizeof(dst); i++) {
		dst[i] = (uint8_t)(0x80u + i);
	}
	CHECK_EQ_I(hwfake_dma_write(fake, h, 0x100, dst, sizeof(dst)), 0);
	{
		uint8_t check[64];

		CHECK_EQ_I(n2vc_dma_sync(hw, mem, guest, 0x100, sizeof(check), 0), 0);
		memcpy(check, n2a32_ptr(mem, guest + 0x100), sizeof(check));
		CHECK(memcmp(check, dst, sizeof(check)) == 0);
	}
	/* the caller's m must match the one the entry was registered with */
	CHECK_EQ_I(n2vc_dma_sync(hw, NULL, guest, 0, 16, 1), 0);
	/* bounds */
	CHECK_EQ_I(n2vc_dma_sync(hw, mem, guest, 0x1ff0, 0x20, 1), -ERANGE);
	CHECK_EQ_I(n2vc_dma_sync(hw, mem, guest + 4, 0, 16, 1), -EINVAL); /* base only */
	CHECK_EQ_I(n2vc_dma_sync(hw, mem, guest + 0x2000, 0, 16, 1), -ENOENT);
	CHECK_EQ_I(n2vc_dma_sync(hw, mem, 0, 0, 16, 1), -EINVAL);

	/* lookup_pa inside / one past the end */
	CHECK_EQ_I(n2vc_dma_lookup_pa(pa + 0x123, &got_pa, &got_guest, &got_bytes), 0);
	CHECK_EQ_U32(got_pa, h);
	CHECK_EQ_U32(got_guest, guest + 0x123);
	CHECK_EQ_U32(got_bytes, 0x2000u - 0x123u);
	CHECK_EQ_I(n2vc_dma_lookup_pa(pa + 0x2000, NULL, NULL, NULL), -ENOENT);

	/* a second buffer, then unmap both */
	CHECK_EQ_I(n2vc_dma_guest_alloc(hw, mem, 0x1000, 0, &bh, &bpa, &bguest), 0);
	CHECK_EQ_I(n2vc_dma_guest_register(hw, mem, bh, bpa, 0x1000, &gguest), 0);
	CHECK_EQ_U32(gguest, bguest);
	CHECK_EQ_I(n2vc_dma_guest_register(hw, mem, bh, bpa + 4, 0x1000, &gguest), -EEXIST);
	CHECK_EQ_I(n2vc_dma_guest_unmap(hw, mem, bguest + 1), -EINVAL);
	/* IV -> V41: a guest ion_munmap only drops the importer's view.  The AP-registered entry (and
	 * the mapping it syncs through) stays until the AP releases it with guest_forget(). */
	CHECK_EQ_I(n2vc_dma_guest_unmap(hw, mem, bguest), 0);
	CHECK_EQ_I(n2vc_dma_lookup_handle(bh, NULL, NULL, NULL), 0);
	CHECK_EQ_I(n2vc_dma_guest_unmap(hw, mem, bguest), 0);
	CHECK_EQ_I(n2vc_dma_guest_forget(hw, bh), 0);            /* owns -> frees the DMA handle */
	CHECK_EQ_I(n2vc_dma_lookup_handle(bh, NULL, NULL, NULL), -ENOENT);

	/* a caller-owned handle: register, unmap (view only), the caller forgets and frees */
	CHECK_EQ_I(hw->dma_alloc(hw->ctx, 0x1000, 0, &gh, &gpa), 0);
	CHECK_EQ_I(n2vc_dma_guest_register(hw, mem, gh, gpa, 0x1000, &bguest), 0);
	CHECK_EQ_I(n2vc_dma_guest_unmap(hw, mem, bguest), 0);
	CHECK_EQ_I(n2vc_dma_lookup_handle(gh, NULL, NULL, NULL), 0);
	CHECK_EQ_I(n2vc_dma_guest_forget(hw, gh), 0);
	CHECK_EQ_I(hw->dma_free(hw->ctx, gh), 0);

	/* pa-only registration: what vcodecd does with a client buffer that lives outside the
	 * daemon's guest address space (m == NULL, no guest address).  This is the path
	 * n2ap_venc_encode() uses for an unmapped handle: pa from the registry, sync skipped,
	 * tagged handle as the vsi cookie. */
	{
		uint32_t ph = 0, ppa = 0, lpa = 0, lguest = 1, lbytes = 0, pg = 0;

		CHECK_EQ_I(n2vc_dma_guest_register(NULL, NULL, 1, 0, 0x1000, NULL), -EINVAL);
		CHECK_EQ_I(n2vc_dma_guest_register(hw, NULL, 0, 0, 0x1000, NULL), -EINVAL);
		CHECK_EQ_I(n2vc_dma_guest_register(hw, NULL, 1, 0, 0, NULL), -EINVAL);
		CHECK_EQ_I(hw->dma_alloc(hw->ctx, 0x3000, 0, &ph, &ppa), 0);
		CHECK_EQ_I(n2vc_dma_guest_register(hw, NULL, ph, ppa, 0x3000, NULL), 0);
		CHECK_EQ_I(n2vc_dma_lookup_handle(ph, &lpa, &lguest, &lbytes), 0);
		CHECK_EQ_U32(lpa, ppa);
		CHECK_EQ_U32(lguest, 0u);           /* no guest mapping */
		CHECK_EQ_U32(lbytes, 0x3000u);
		CHECK_EQ_I(n2vc_dma_lookup_pa(ppa + 0x20, &lpa, &lguest, &lbytes), 0);
		CHECK_EQ_U32(lpa, ph);
		CHECK_EQ_U32(lguest, 0u);           /* pa-only: no guest address to report */
		CHECK_EQ_U32(lbytes, 0x3000u - 0x20u);
		/* idempotent; a different pa or a larger size for the same handle is a conflict */
		CHECK_EQ_I(n2vc_dma_guest_register(hw, NULL, ph, ppa, 0x3000, NULL), 0);
		CHECK_EQ_I(n2vc_dma_guest_register(hw, NULL, ph, ppa + 4, 0x3000, NULL), -EEXIST);
		CHECK_EQ_I(n2vc_dma_guest_register(hw, NULL, ph, ppa, 0x3001, NULL), -EEXIST);
		/* guest_map() later attaches an address space to the same entry and keeps pa/bytes */
		CHECK_EQ_I(n2vc_dma_guest_map(hw, mem, ph, 0x1000, &pg), 0);
		CHECK(pg != 0u);
		CHECK_EQ_U32(pg & 0xfffu, 0u);
		CHECK_EQ_I(n2vc_dma_lookup_handle(ph, &lpa, &lguest, &lbytes), 0);
		CHECK_EQ_U32(lpa, ppa);
		CHECK_EQ_U32(lguest, pg);
		CHECK_EQ_U32(lbytes, 0x3000u);
		CHECK_EQ_I(n2vc_dma_guest_map(hw, mem, ph, 0x800, &gh), 0);
		CHECK_EQ_U32(gh, pg);               /* same address again */
		CHECK_EQ_I(n2vc_dma_guest_map(hw, mem, ph, 0x4000, &gh), -EINVAL);
		/* IV -> V41: unmap keeps the AP-registered entry; forget is what releases it */
		CHECK_EQ_I(n2vc_dma_guest_unmap(hw, mem, pg), 0);
		CHECK_EQ_I(n2vc_dma_lookup_handle(ph, NULL, NULL, NULL), 0);
		CHECK_EQ_I(n2vc_dma_guest_forget(hw, ph), 0);
		CHECK_EQ_I(n2vc_dma_lookup_handle(ph, NULL, NULL, NULL), -ENOENT);
		CHECK_EQ_I(hw->dma_free(hw->ctx, ph), 0);   /* register() did not own it */
	}

	/* forget: the only way to release a pa-only entry (unmap needs a guest address) */
	{
		uint32_t fh = 0, fpa = 0, oh = 0, opa = 0, og = 0;

		CHECK_EQ_I(hw->dma_alloc(hw->ctx, 0x1000, 0, &fh, &fpa), 0);
		CHECK_EQ_I(n2vc_dma_guest_register(hw, NULL, fh, fpa, 0x1000, NULL), 0);
		CHECK_EQ_I(n2vc_dma_guest_forget(NULL, fh), -EINVAL);
		CHECK_EQ_I(n2vc_dma_guest_forget(hw, 0), -EINVAL);
		CHECK_EQ_I(n2vc_dma_guest_forget(hw, fh), 0);
		CHECK_EQ_I(n2vc_dma_lookup_handle(fh, NULL, NULL, NULL), -ENOENT);
		CHECK_EQ_I(n2vc_dma_lookup_pa(fpa, NULL, NULL, NULL), -ENOENT);
		CHECK_EQ_I(n2vc_dma_guest_forget(hw, fh), -ENOENT);
		CHECK_EQ_I(hw->dma_free(hw->ctx, fh), 0);
		/* an entry guest_alloc() created is freed by forget */
		CHECK_EQ_I(n2vc_dma_guest_alloc(hw, mem, 0x1000, 0, &oh, &opa, &og), 0);
		CHECK_EQ_I(n2vc_dma_guest_forget(hw, oh), 0);
		CHECK_EQ_I(hw->dma_free(hw->ctx, oh), -ENOENT);
	}
}

struct dma_thread_arg {
	struct n2a32_mem *mem;
	const struct n2vc_hw *hw;
	struct hwfake *fake;
	unsigned id;
	int fail;
};

static void *dma_thread(void *arg)
{
	struct dma_thread_arg *a = arg;
	uint32_t h = 0, pa = 0, guest = 0;
	uint8_t src[256], dst[256];

	if (n2vc_dma_guest_alloc(a->hw, a->mem, sizeof(src), 0, &h, &pa, &guest) != 0) {
		a->fail = 1;
		return NULL;
	}
	fill_pattern(src, sizeof(src), a->id);
	for (unsigned r = 0; r < 200; r++) {
		memcpy(n2a32_ptr(a->mem, guest), src, sizeof(src));
		if (n2vc_dma_sync(a->hw, a->mem, guest, 0, sizeof(src), 1) != 0) {
			a->fail = 1;
			break;
		}
		if (n2vc_dma_lookup_pa(pa, NULL, NULL, NULL) != 0) {
			a->fail = 1;
			break;
		}
		memset(n2a32_ptr(a->mem, guest), 0, sizeof(src));
		if (n2vc_dma_sync(a->hw, a->mem, guest, 0, sizeof(src), 0) != 0) {
			a->fail = 1;
			break;
		}
		memcpy(dst, n2a32_ptr(a->mem, guest), sizeof(dst));
		if (memcmp(src, dst, sizeof(src)) != 0) {
			a->fail = 1;
			break;
		}
	}
	if (n2vc_dma_guest_unmap(a->hw, a->mem, guest) != 0) {
		a->fail = 1;
	}
	return NULL;
}

static void test_dma_threads(struct n2a32_mem *mem, const struct n2vc_hw *hw, struct hwfake *fake)
{
	pthread_t th[4];
	struct dma_thread_arg args[4];

	for (unsigned i = 0; i < 4; i++) {
		args[i].mem = mem;
		args[i].hw = hw;
		args[i].fake = fake;
		args[i].id = i + 1u;
		args[i].fail = 0;
		pthread_create(&th[i], NULL, dma_thread, &args[i]);
	}
	for (unsigned i = 0; i < 4; i++) {
		pthread_join(th[i], NULL);
		CHECK_EQ_I(args[i].fail, 0);
	}
}

int main(int argc, char **argv)
{
	struct n2a32_mem *mem;
	struct hwfake fake;
	struct n2vc_hw hw;
	struct n2vcu *vcu;
	struct dc dc;
	struct n2ap_venc *e;
	int rc;

	if (argc > 1) {
		g_build = argv[1];
	}
	mem = n2a32_mem_create();
	CHECK(mem != NULL);
	if (mem == NULL) {
		goto out;
	}
	hwfake_init(&hw, &fake, RW_CLAIM_VCODEC_PA, RW_CLAIM_VCODEC_PA + RW_CLAIM_VCODEC_BYTES);
	vcu = n2vcu_create(mem, &hw);
	CHECK(vcu != NULL);
	if (vcu == NULL) {
		goto out_mem;
	}
	dc_start(&dc, vcu, &fake);

	test_layout_literals();

	/* ---- open ---- */
	dc_reset_records(&dc);
	e = open_h264(vcu, &hw, 500);
	if (e != NULL) {
		struct venc_vsi *vsi = n2ap_venc_vsi(e);

		CHECK_EQ_U32(n2ap_venc_ipi_id(e), 15u);
		CHECK(vsi != NULL);
		CHECK_EQ_I(n2ap_venc_aborted(e), 0);
		pthread_mutex_lock(&dc.lock);
		CHECK_EQ_U32(dc.rec.n_init, 1u);
		CHECK_EQ_U32(dc.rec.init_len, 16u);
		CHECK_EQ_U32(dc.rec.init_msg_id, 0xC000u);
		CHECK_EQ_U32(dc.rec.init_reserved, 0u);
		CHECK(dc.rec.init_inst != 0u);
		CHECK(dc.rec.init_inst < 0x100000000ull);   /* the 32-bit daemon must not lose bits */
		pthread_mutex_unlock(&dc.lock);
		CHECK_EQ_U32(vsi->sizeimage[0], 0x2fd000u);
		CHECK_EQ_U32(vsi->sync_mode, 0u);

		/* ---- n2ap_venc_power: the explicit AP reference a GCE daemon needs ---- */
		CHECK_EQ_I(n2ap_venc_power(NULL, 1), -EINVAL);
		CHECK_EQ_I(n2ap_venc_power(e, 2), -EINVAL);
		CHECK_EQ_I(n2ap_venc_power(e, 1), 0);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 1u);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC_C1), 0u);
		CHECK_EQ_I(n2ap_venc_power(e, 1), 0);   /* per-session reference count */
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 1u);
		CHECK_EQ_I(n2ap_venc_power(e, 0), 0);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 1u);
		CHECK_EQ_I(n2ap_venc_power(e, 0), 0);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 0u);
		CHECK_EQ_I(n2ap_venc_power(e, 0), 0);   /* an extra off is a no-op, like clock_off */
		{
			enum { HAMMERS = 4 };
			struct power_hammer ph[HAMMERS];
			pthread_t th[HAMMERS];
			unsigned i;

			for (i = 0; i < HAMMERS; i++) {
				ph[i].e = e;
				ph[i].rounds = 50;
				ph[i].fails = 0;
				CHECK_EQ_I(pthread_create(&th[i], NULL, power_hammer_main, &ph[i]), 0);
			}
			for (i = 0; i < HAMMERS; i++) {
				(void)pthread_join(th[i], NULL);
				CHECK_EQ_I(ph[i].fails, 0);
			}
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 0u);
		}

		/* ---- query capability: formats ---- */
		dc_reset_records(&dc);
		{
			struct mtk_video_fmt fmts[MTK_MAX_ENC_CODECS_SUPPORT];

			memset(fmts, 0xee, sizeof(fmts));
			rc = n2ap_venc_query_cap(e, VENC_GET_PARAM_CAPABILITY_SUPPORTED_FORMATS,
			                         fmts, sizeof(fmts));
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.n_cap, 1u);
			CHECK_EQ_U32(dc.rec.cap_id, 0u);
			CHECK_EQ_U32(dc.rec.cap_inst, (uint32_t)dc.rec.cookie);
			CHECK_EQ_U32((uint32_t)(dc.rec.cookie >> 32), 0u);
			CHECK_EQ_U32(dc.rec.cap_data_lo, (uint32_t)(uintptr_t)fmts);
			CHECK_EQ_U32(dc.rec.cap_data_hi, (uint32_t)((uint64_t)(uintptr_t)fmts >> 32));
			CHECK_EQ_U32(dc.rec.cap_len, 24u);
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_U32(fmts[0].fourcc, N2AP_FOURCC_H264);
			CHECK_EQ_U32(fmts[0].type, 1u);
			CHECK_EQ_U32(fmts[0].num_planes, 1u);
			CHECK_EQ_U32(fmts[1].fourcc, N2AP_FOURCC_H265);
			CHECK_EQ_U32(fmts[2].fourcc, 0u);
			CHECK_EQ_U32(fmts[10].fourcc, 0u);
		}
		/* ---- query capability: frame sizes ---- */
		dc_reset_records(&dc);
		{
			struct mtk_codec_framesizes fs[MTK_MAX_ENC_CODECS_SUPPORT];

			memset(fs, 0, sizeof(fs));
			rc = n2ap_venc_query_cap(e, VENC_GET_PARAM_CAPABILITY_FRAME_SIZES, fs,
			                         sizeof(fs));
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.n_cap, 1u);
			CHECK_EQ_U32(dc.rec.cap_id, 1u);
			CHECK_EQ_U32(dc.rec.cap_len, 24u);
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_U32(fs[0].fourcc, N2AP_FOURCC_H264);
			CHECK_EQ_U32(fs[0].profile, 0x11u);
			CHECK_EQ_U32(fs[0].level, 0x22u);
			CHECK_EQ_U32(fs[0].stepwise.max_width, 4096u);
			CHECK_EQ_U32(fs[0].stepwise.step_height, 16u);
			CHECK_EQ_U32(fs[1].fourcc, 0u);
		}
		/* bad arguments: no message must be sent */
		dc_reset_records(&dc);
		{
			struct mtk_video_fmt small;
			uint8_t big[64u * 36u];

			CHECK_EQ_I(n2ap_venc_query_cap(e, 0, &small, sizeof(small)), -EINVAL);
			CHECK_EQ_I(n2ap_venc_query_cap(e, 7, big, sizeof(big)), -EINVAL);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.n_cap, 0u);
			pthread_mutex_unlock(&dc.lock);
		}

		/* ---- stream start: ENC then PREPEND_HEADER ---- */
		dc_reset_records(&dc);
		{
			struct n2ap_enc_param p;
			struct mtk_color_desc cd;

			memset(&cd, 0, sizeof(cd));
			n2ap_enc_param_1080p30_h264(&p);
			CHECK_EQ_U32(p.input_yuv_fmt, 6u);
			CHECK_EQ_U32(p.width, 1920u);
			CHECK_EQ_U32(p.buf_height, 1088u);
			CHECK_EQ_U32(p.frm_rate, 30u);
			CHECK_EQ_U32(p.bitrate, 20000000u);
			CHECK_EQ_U32(p.seq_hdr_mode, 0u);   /* separate: the control's default */
			{
				struct n2ap_enc_param hp;

				/* HEVC control defaults (mtk_vcodec_enc.c:2760-2776) */
				n2ap_enc_param_1080p30_hevc(&hp);
				CHECK_EQ_U32(hp.input_yuv_fmt, 6u);
				CHECK_EQ_U32(hp.width, 1920u);
				CHECK_EQ_U32(hp.buf_height, 1088u);
				CHECK_EQ_U32(hp.profile, 0u);   /* MAIN */
				CHECK_EQ_U32(hp.level, 5u);     /* V4L2_MPEG_VIDEO_HEVC_LEVEL_4 */
				CHECK_EQ_U32(hp.tier, 0u);      /* MAIN */
				CHECK_EQ_U32(hp.seq_hdr_mode, 0u);
				n2ap_enc_param_1080p30_hevc(NULL);   /* no crash */
			}
			rc = n2ap_venc_start(e, &p);
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.n_param, 1u);
			CHECK_EQ_U32(dc.rec.param[0].id, 0u);
			CHECK_EQ_U32(dc.rec.param[0].item, 0u);
			CHECK_EQ_U32(dc.rec.param[0].len, 48u);
			CHECK_EQ_U32(dc.rec.cfg_input, 6u);
			CHECK_EQ_U32(dc.rec.cfg_w, 1920u);
			CHECK_EQ_U32(dc.rec.cfg_h, 1080u);
			CHECK_EQ_U32(dc.rec.cfg_bw, 1920u);
			CHECK_EQ_U32(dc.rec.cfg_bh, 1088u);
			CHECK_EQ_U32(dc.rec.cfg_fps, 30u);
			CHECK_EQ_U32(dc.rec.cfg_br, 20000000u);
			CHECK_EQ_U32(dc.rec.cfg_gop, 0u);
			CHECK_EQ_U32(dc.rec.cfg_intra, 0u);
			CHECK_EQ_U32(dc.rec.cfg_profile, 0u);
			CHECK_EQ_U32(dc.rec.cfg_level, 0u);
			CHECK_EQ_U32(dc.rec.cfg_iqp, 51u);
			CHECK_EQ_U32(dc.rec.cfg_maxqp, 51u);
			CHECK_EQ_U32(dc.rec.cfg_wfd, 0u);
			CHECK_EQ_U32(dc.rec.vcu_addr_bad, 0u);
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_U32(p.sizeimage[0], 0x300000u);
			CHECK_EQ_U32(p.sizeimage[1], 0x180000u);
			CHECK_EQ_U32(p.sizeimage[2], 0u);
			CHECK_EQ_U32(p.sync_mode, 1u);
		}

		/* ---- the per-type set_param table (venc_vcu_if.c:387-502) ---- */
		{
			static const struct { unsigned type, item; uint32_t value; } tab[] = {
				{ 1u, 0u, 0u },            /* FORCE_INTRA */
				{ 2u, 1u, 3000000u },      /* ADJUST_BITRATE */
				{ 3u, 1u, 24u },           /* ADJUST_FRAMERATE */
				{ 4u, 1u, 12u },           /* GOP_SIZE */
				{ 5u, 1u, 24u },           /* INTRA_PERIOD */
				{ 6u, 0u, 0u },            /* SKIP_FRAME */
				{ 9u, 1u, 7u },            /* SCENARIO */
				{ 10u, 1u, 3u },           /* NONREFP */
				{ 11u, 1u, 29u },          /* DETECTED_FRAMERATE */
				{ 12u, 1u, 1u },           /* RFS_ON */
				{ 13u, 1u, 1u },           /* PREPEND_SPSPPS_TO_IDR */
				{ 14u, 1u, 4u },           /* OPERATION_RATE */
				{ 15u, 1u, 1u },           /* BITRATE_MODE */
				{ 16u, 1u, 2u },           /* ROI_ON */
				{ 17u, 1u, 0x00100010u },  /* HEIF_GRID_SIZE */
				{ 19u, 1u, 0u },           /* SEC_MODE */
				{ 20u, 1u, 9u },           /* TSVC */
				{ 21u, 1u, 5u },           /* NONREFPFREQ */
				{ 22u, 1u, 40u },          /* ADJUST_MAX_QP */
				{ 23u, 1u, 10u },          /* ADJUST_MIN_QP */
				{ 24u, 1u, 0xfffffffeu },  /* ADJUST_I_P_QP_DELTA = -2 */
				{ 25u, 1u, 33u },          /* ADJUST_FRAME_LEVEL_QP */
				{ 26u, 1u, 4u },           /* MAX_REFP_NUM */
				{ 27u, 1u, 2u },           /* REFP_DISTANCE */
				{ 28u, 1u, 6u },           /* REFP_FRMNUM */
				{ 30u, 1u, 1u },           /* ENABLE_DUMMY_NAL */
			};
			struct n2ap_enc_param p;
			unsigned base;
			struct mtk_color_desc cd;
			unsigned before;

			n2ap_enc_param_1080p30_h264(&p);
			p.bitrate = 3000000;
			p.frm_rate = 24;
			p.gop_size = 12;
			p.intra_period = 24;
			p.scenario = 7;
			p.nonrefp = 3;
			p.nonrefpfreq = 5;
			p.detectframerate = 29;
			p.rfs = 1;
			p.prependheader = 1;
			p.operationrate = 4;
			p.bitratemode = 1;
			p.roion = 2;
			p.heif_grid_size = 0x00100010;
			p.tsvc = 9;
			p.max_qp = 40;
			p.min_qp = 10;
			p.i_p_qp_delta = 0xfffffffeu;
			p.frame_level_qp = 33;
			p.maxrefpnum = 4;
			p.refpdistance = 2;
			p.refpfrmnum = 6;
			p.dummynal = 1;
			memset(&cd, 0, sizeof(cd));
			cd.color_primaries = 0xcafe1234u;
			p.color_desc = &cd;

			dc_reset_records(&dc);
			for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
				rc = n2ap_venc_set_param(e, tab[i].type, &p);
				CHECK_EQ_I(rc, 0);
				pthread_mutex_lock(&dc.lock);
				CHECK(dc.rec.nparams > 0u);
				base = dc.rec.nparams > 0u ? dc.rec.nparams - 1u : 0u;
				CHECK_EQ_U32(dc.rec.param[base].id, tab[i].type);
				CHECK_EQ_U32(dc.rec.param[base].item, tab[i].item);
				CHECK_EQ_U32(dc.rec.param[base].value, tab[i].value);
				pthread_mutex_unlock(&dc.lock);
			}
			/* FORCE_INTRA with p == NULL is what mtk_vcodec_enc.c:2100 passes */
			rc = n2ap_venc_set_param(e, VENC_SET_PARAM_FORCE_INTRA, NULL);
			CHECK_EQ_I(rc, 0);
			/* PREPEND_HEADER is the one the JOINED header mode sends at stream start; with a
			 * NULL parameter, like mtk_vcodec_enc.c:1884-1887. */
			pthread_mutex_lock(&dc.lock);
			before = dc.rec.n_param;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_set_param(e, VENC_SET_PARAM_PREPEND_HEADER, NULL);
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.n_param, before + 1u);
			CHECK_EQ_U32(dc.rec.param[dc.rec.nparams - 1u].id, 7u);
			CHECK_EQ_U32(dc.rec.param[dc.rec.nparams - 1u].item, 0u);
			pthread_mutex_unlock(&dc.lock);
			/* COLOR_DESC travels in the vsi, data_item 0 (:456-458) */
			p.color_desc = &cd;
			rc = n2ap_venc_set_param(e, VENC_SET_PARAM_COLOR_DESC, &p);
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK(dc.rec.nparams > 0u);
			base = dc.rec.nparams > 0u ? dc.rec.nparams - 1u : 0u;
			CHECK_EQ_U32(dc.rec.param[base].id, 18u);
			CHECK_EQ_U32(dc.rec.param[base].item, 0u);
			CHECK_EQ_U32(dc.rec.cfg_color0, 0xcafe1234u);
			CHECK_EQ_U32(dc.rec.param[base].vcu_addr, dc.vsi_addr);
			pthread_mutex_unlock(&dc.lock);
			/* types the VCU switch rejects: no message */
			dc_reset_records(&dc);
			CHECK_EQ_I(n2ap_venc_set_param(e, 8u, &p), -EINVAL);    /* TS_MODE */
			CHECK_EQ_I(n2ap_venc_set_param(e, 29u, &p), -EINVAL);   /* LOG */
			CHECK_EQ_I(n2ap_venc_set_param(e, 0x7fu, &p), -EINVAL);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.n_param, 0u);
			pthread_mutex_unlock(&dc.lock);
		}

		/* ---- encode: full daemon sequence, free-list result ---- */
		{
			uint32_t fh = 0, fpa = 0, fguest = 0, bh = 0, bpa = 0, bguest = 0;
			struct n2ap_enc_result res;
			uint8_t expect[16];

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 1920u * 1088u * 3u / 2u, 0, &fh, &fpa,
			                                &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x100000, 0, &bh, &bpa, &bguest), 0);
			memset(expect, 0, sizeof(expect));
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 1;
			dc.cfg.wait_isr = 1;
			dc.cfg.put_buffer = 1;
			dc.cfg.write_bs = 1;
			dc.cfg.enc_status = 0;
			pthread_mutex_unlock(&dc.lock);
			dc_reset_records(&dc);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0x1234u);      /* the free-list entry wins */
			CHECK_EQ_U32(res.is_key_frm, 1u);
			CHECK_EQ_U32(res.from_free_list, 1u);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.n_encode, 1u);
			CHECK_EQ_U32(dc.rec.enc_len, 76u);
			CHECK_EQ_U32(dc.rec.enc_msg_id, 0xC002u);
			CHECK_EQ_U32(dc.rec.enc_vcu_addr, dc.vsi_addr);
			CHECK_EQ_U32(dc.rec.enc_planes, 1u);
			CHECK_EQ_U32(dc.rec.enc_mode, 3u);
			CHECK_EQ_U32(dc.rec.enc_input, fpa);
			CHECK_EQ_U32(dc.rec.enc_input_size, 1920u * 1088u * 3u / 2u);
			CHECK_EQ_U32(dc.rec.enc_input_fd, 0u);
			CHECK_EQ_U32(dc.rec.enc_bs_fd, 0u);   /* n2ap_venc_encode is the 0/0 wrapper */
			CHECK_EQ_U32(dc.rec.enc_bsaddr, bpa);
			CHECK_EQ_U32(dc.rec.enc_bssize, 0x100000u);
			CHECK_EQ_U32(dc.rec.enc_meta, 0u);
			CHECK_EQ_U32(dc.rec.enc_qpmap, 0u);
			CHECK_EQ_U64(dc.rec.enc_bsva, bguest);
			CHECK_EQ_U64(dc.rec.enc_fbva, fguest);
			CHECK_EQ_U32(dc.rec.power_on_rc, 1u);   /* non-ack */
			CHECK_EQ_U32(dc.rec.wait_rc, 1u);
			CHECK_EQ_U32(dc.rec.wait_irq_status, 0x4u);
			CHECK_EQ_U32(dc.rec.wait_timeout, 0u);
			CHECK_EQ_U32(dc.rec.put_rc, 1u);
			CHECK_EQ_U32(dc.rec.put_count_after, 0u);
			CHECK_EQ_U32(dc.rec.vcu_addr_bad, 0u);
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 1u);
			/* core 0 takes no VENC_C1 reference: that block is the whole core-1 power
			 * domain, not a bare clock gate (hw-k02). */
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC_C1), 0u);
			CHECK_EQ_I(n2vc_dma_sync(&hw, mem, bguest, 0, sizeof(expect), 0), 0);
			memcpy(expect, n2a32_ptr(mem, bguest), sizeof(expect));
			CHECK_EQ_U32(expect[0], 0x00u);
			CHECK_EQ_U32(expect[1], 0x00u);
			CHECK_EQ_U32(expect[2], 0x00u);
			CHECK_EQ_U32(expect[3], 0x01u);
			CHECK_EQ_U32(expect[4], 0x67u);     /* the daemon's synthetic bitstream */
			CHECK_EQ_U32(expect[5], 0x42u);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}

		/* ---- asynchronous GCE answer: ENCODE_ACK now, PUT_BUFFER later ---- */
		{
			uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x10000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x10000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 0;
			dc.cfg.power_off = 0;
			dc.cfg.wait_isr = 0;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			dc.cfg.encode_ack = 1;
			dc.cfg.put_buffer_late = 1;
			dc.cfg.late_ms = 20;
			dc.cfg.reget_before_reply = 1;   /* vpud's receiver is a separate thread */
			dc.cfg.free_size = 0x2222;
			dc.cfg.free_key = 0;
			dc.cfg.enc_size = 0x9999;   /* the ACK must not carry it */
			dc.cfg.enc_status = 0;
			pthread_mutex_unlock(&dc.lock);
			dc_reset_records(&dc);
			memset(&res, 0xcc, sizeof(res));
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0u);           /* the ACK has no size */
			CHECK_EQ_U32(res.from_free_list, 0u);
			rc = n2ap_venc_wait_done(e, 1000, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0x2222u);
			CHECK_EQ_U32(res.is_key_frm, 0u);
			CHECK_EQ_U32(res.from_free_list, 1u);
			pthread_mutex_lock(&dc.lock);
			CHECK(dc.rec.n_late_put >= 1u);
			pthread_mutex_unlock(&dc.lock);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.put_buffer_late = 0;
			dc.cfg.reget_before_reply = 0;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}

		/* ---- asynchronous job that never completes: bounded -ETIMEDOUT ---- */
		{
			uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.encode_ack = 1;
			dc.cfg.put_buffer_late = 0;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			rc = n2ap_venc_wait_done(e, 30, &res);
			CHECK_EQ_I(rc, -ETIMEDOUT);
			CHECK_EQ_I(n2ap_venc_aborted(e), 0);   /* a slow job is not a protocol failure */
			pthread_mutex_lock(&dc.lock);
			dc.cfg.encode_ack = 0;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}

		/* ---- sequence header: the size exists only in list_free (no PUT_BUFFER);
		 * the AP must drain the ring itself after the encode ---- */
		{
			uint32_t bh = 0, bguest = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.encode_ack = 1;
			dc.cfg.free_without_put = 1;
			dc.cfg.put_buffer_late = 0;
			dc.cfg.free_size = 0x1du;
			dc.cfg.free_key = 0;
			dc.cfg.enc_status = 0;
			pthread_mutex_unlock(&dc.lock);
			dc_reset_records(&dc);
			rc = n2ap_venc_encode(e, 0, bh, N2AP_ENC_SEQ_HDR, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0x1du);
			CHECK_EQ_U32(res.is_key_frm, 0u);
			CHECK_EQ_U32(res.from_free_list, 1u);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.enc_mode, 2u);
			CHECK_EQ_U32(dc.rec.enc_planes, 0u);
			dc.cfg.free_without_put = 0;
			dc.cfg.encode_ack = 0;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
		}

		/* ---- encode: POWER_OFF, DONE-only result, key frame 0 ---- */
		{
			uint32_t fh = 0, fpa = 0, fguest = 0, bh = 0, bpa = 0, bguest = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x10000, 0, &fh, &fpa, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x10000, 0, &bh, &bpa, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 1;
			dc.cfg.power_off = 1;
			dc.cfg.wait_isr = 0;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			dc.cfg.enc_size = 0x1200;
			dc.cfg.enc_key = 0;
			pthread_mutex_unlock(&dc.lock);
			dc_reset_records(&dc);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0x1200u);
			CHECK_EQ_U32(res.is_key_frm, 0u);
			CHECK_EQ_U32(res.from_free_list, 0u);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.power_off_rc, 1u);
			pthread_mutex_unlock(&dc.lock);
			/* encode #1 powered on and never powered off; this POWER_ON is a second
			 * reference and this POWER_OFF releases only that one (the clock framework
			 * refcounts the same way), so one reference remains until close. */
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 1u);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC_C1), 0u);

			/* sequence header: no frame plane at all */
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 0;
			dc.cfg.power_off = 0;
			dc.cfg.write_bs = 0;
			dc.cfg.enc_size = 0x30;
			dc.cfg.enc_key = 0;
			pthread_mutex_unlock(&dc.lock);
			dc_reset_records(&dc);
			rc = n2ap_venc_encode(e, 0, bh, N2AP_ENC_SEQ_HDR, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0x30u);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.enc_mode, 2u);
			CHECK_EQ_U32(dc.rec.enc_planes, 0u);
			CHECK_EQ_U32(dc.rec.enc_input, 0u);
			CHECK_EQ_U32(dc.rec.enc_input_size, 0u);
			pthread_mutex_unlock(&dc.lock);

			/* final frame */
			dc_reset_records(&dc);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME_FINAL, &res);
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.enc_mode, 4u);
			CHECK_EQ_U32(dc.rec.enc_planes, 1u);
			pthread_mutex_unlock(&dc.lock);

			/* EOS flush: FRAME_FINAL without a frame is legal (mtk_vcodec_enc.c:1928) */
			dc_reset_records(&dc);
			rc = n2ap_venc_encode(e, 0, bh, N2AP_ENC_FRAME_FINAL, &res);
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.enc_mode, 4u);
			CHECK_EQ_U32(dc.rec.enc_planes, 0u);
			CHECK_EQ_U32(dc.rec.enc_input, 0u);
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}

		/* ---- daemon error status: -EINVAL, session survives ---- */
		{
			uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.enc_status = 1;
			dc.cfg.power_on = 0;
			dc.cfg.power_off = 0;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, -EINVAL);
			CHECK_EQ_I(n2ap_venc_last_status(e), 1);
			CHECK_EQ_I(n2ap_venc_aborted(e), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.enc_status = 0;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}

		/* ---- encode with pa-only registered handles (vcodecd's client buffers): no sync,
		 *      tagged handle cookies.  This is the IP integration path. ---- */
		{
			uint32_t fh = 0, fpa = 0, bh = 0, bpa = 0;
			struct n2ap_enc_result res;
			unsigned rw_before;

			CHECK_EQ_I(hw.dma_alloc(hw.ctx, 0x8000, 0, &fh, &fpa), 0);
			CHECK_EQ_I(hw.dma_alloc(hw.ctx, 0x8000, 0, &bh, &bpa), 0);
			CHECK_EQ_I(n2vc_dma_guest_register(&hw, NULL, fh, fpa, 0x8000, NULL), 0);
			CHECK_EQ_I(n2vc_dma_guest_register(&hw, NULL, bh, bpa, 0x8000, NULL), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 0;
			dc.cfg.power_off = 0;
			dc.cfg.wait_isr = 0;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			dc.cfg.enc_status = 0;
			dc.cfg.enc_size = 0x777;
			dc.cfg.enc_key = 1;
			pthread_mutex_unlock(&dc.lock);
			dc_reset_records(&dc);
			rw_before = hwfake_count_events(&fake, HWFAKE_EV_DMA_RW);
			/* The dma-buf share ids travel in input_fd[0]/bs_fd (IV's real-vpud fix). */
			rc = n2ap_venc_encode_fd(e, fh, 0x500, bh, 0x501, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0x777u);
			CHECK_EQ_U32(res.is_key_frm, 1u);
			CHECK_EQ_U32(hwfake_count_events(&fake, HWFAKE_EV_DMA_RW), rw_before);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.enc_input, fpa);
			CHECK_EQ_U32(dc.rec.enc_bsaddr, bpa);
			CHECK_EQ_U32(dc.rec.enc_input_fd, 0x500u);
			CHECK_EQ_U32(dc.rec.enc_bs_fd, 0x501u);
			CHECK_EQ_U64(dc.rec.enc_bsva, 0x80000000u | (uint64_t)bh);
			CHECK_EQ_U64(dc.rec.enc_fbva, 0x80000000u | (uint64_t)fh);
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_forget(&hw, fh), 0);
			CHECK_EQ_I(n2vc_dma_guest_forget(&hw, bh), 0);
			CHECK_EQ_I(hw.dma_free(hw.ctx, fh), 0);
			CHECK_EQ_I(hw.dma_free(hw.ctx, bh), 0);
		}

		/* ---- vcodecd's separate sequence-header job: dma_alloc() then encode().
		 *      The AP resolves the bitstream handle in n2vc_dma, so the daemon must register
		 *      the handle it allocated itself.  This is the exact daemon/vcodecd.c
		 *      read_sequence_header() pattern; without the registration the first job of a
		 *      session returns -EINVAL before any message is sent (found 11 Nov). ---- */
		{
			uint32_t bh = 0, bpa = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(hw.dma_alloc(hw.ctx, 0x40000, 0, &bh, &bpa), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.enc_status = 0;
			dc.cfg.enc_size = 29;
			dc.cfg.enc_key = 0;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			pthread_mutex_unlock(&dc.lock);
			dc_reset_records(&dc);
			rc = n2ap_venc_encode(e, 0, bh, N2AP_ENC_SEQ_HDR, &res);
			CHECK_EQ_I(rc, -EINVAL);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_I(dc.rec.n_encode, 0);   /* refused before the IPI message */
			pthread_mutex_unlock(&dc.lock);
			/* The fix vcodecd needs: register {handle, pa, bytes} before the job. */
			CHECK_EQ_I(n2vc_dma_guest_register(&hw, NULL, bh, bpa, 0x40000, NULL), 0);
			rc = n2ap_venc_encode(e, 0, bh, N2AP_ENC_SEQ_HDR, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 29u);
			CHECK_EQ_U32(res.is_key_frm, 0u);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_I(dc.rec.n_encode, 1);
			CHECK_EQ_U32(dc.rec.enc_mode, VENC_BS_MODE_SEQ_HDR);
			CHECK_EQ_U32(dc.rec.enc_bsaddr, bpa);
			CHECK_EQ_U32(dc.rec.enc_bssize, 0x40000u);
			CHECK_EQ_U32(dc.rec.enc_input, 0u);
			CHECK_EQ_U32(dc.rec.enc_planes, 0u);
			CHECK_EQ_U64(dc.rec.enc_bsva, 0x80000000u | (uint64_t)bh);
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_forget(&hw, bh), 0);
			CHECK_EQ_I(hw.dma_free(hw.ctx, bh), 0);
		}

		/* ---- bad arguments ---- */
		{
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2ap_venc_encode(e, 0, 0, N2AP_ENC_FRAME, &res), -EINVAL);
			CHECK_EQ_I(n2ap_venc_encode(e, 0, 0x7777, N2AP_ENC_FRAME, &res), -EINVAL);
			CHECK_EQ_I(n2ap_venc_encode(NULL, 0, 1, 0, &res), -EINVAL);
		}

		/* ---- a short ENCODE_DONE is a protocol error, not a size ---- */
		{
			uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.short_done = 1;
			dc.cfg.power_on = 0;
			dc.cfg.power_off = 0;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, -EPROTO);
			CHECK_EQ_I(n2ap_venc_aborted(e), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.short_done = 0;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}

		/* ---- WAIT_ISR with no interrupt: timeout=1 and the previous irq_status ---- */
		{
			uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;
			struct n2ap_enc_result res;

			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.wait_isr = 1;
			dc.cfg.isr_no_irq = 1;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.wait_timeout, 1u);
			CHECK_EQ_U32(dc.rec.wait_irq_status, 0x4u);   /* ctx->irq_status is not cleared */
			pthread_mutex_unlock(&dc.lock);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.isr_no_irq = 0;
			dc.cfg.wait_isr = 0;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}

		/* ---- close (DEINIT, drop handler, power already off) ---- */
		dc_reset_records(&dc);
		rc = n2ap_venc_close(e);
		CHECK_EQ_I(rc, 0);
		pthread_mutex_lock(&dc.lock);
		CHECK_EQ_U32(dc.rec.n_deinit, 1u);
		CHECK_EQ_U32(dc.rec.deinit_msg_id, 0xC003u);
		CHECK_EQ_U32(dc.rec.deinit_len, 8u);
		CHECK_EQ_U32(dc.rec.deinit_vcu_addr, dc.vsi_addr);
		pthread_mutex_unlock(&dc.lock);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 0u);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC_C1), 0u);
	}


	/* ---- hw->power fails on a session that holds no reference: -EIO, then recovery ---- */
	{
		struct n2ap_venc *pe = NULL;
		struct n2ap_enc_param p;
		uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;
		struct n2ap_enc_result res;

		rc = n2ap_venc_open(&pe, vcu, &hw, N2AP_FOURCC_H265, 300);
		CHECK_EQ_I(rc, 0);
		if (rc == 0) {
			n2ap_enc_param_1080p30_h264(&p);
			p.width = 640;
			p.height = 480;
			p.buf_width = 640;
			p.buf_height = 480;
			p.seq_hdr_mode = 0;
			CHECK_EQ_I(n2ap_venc_start(pe, &p), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.wait_isr = 0;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			dc.cfg.power_on = 1;
			dc.cfg.power_off = 0;
			pthread_mutex_unlock(&dc.lock);
			hwfake_fail_power(&fake, 1);
			rc = n2ap_venc_encode(pe, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, -EIO);
			CHECK_EQ_I(n2ap_venc_aborted(pe), 0);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 0u);
			hwfake_fail_power(&fake, 0);
			rc = n2ap_venc_encode(pe, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 1u);
			CHECK_EQ_I(n2ap_venc_close(pe), 0);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 0u);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC_C1), 0u);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 0;
			pthread_mutex_unlock(&dc.lock);
		}
	}

	/* ---- open-time failures ---- */
	dc_reset_records(&dc);
	CHECK_EQ_I(n2ap_venc_open(NULL, vcu, &hw, N2AP_FOURCC_H264, 100), -EINVAL);
	{
		struct n2ap_venc *bad = (struct n2ap_venc *)0x1;

		CHECK_EQ_I(n2ap_venc_open(&bad, vcu, &hw, N2AP_FOURCC_NV12, 100), -EINVAL);
		CHECK(bad == NULL);
		CHECK_EQ_I(n2ap_venc_open(&bad, vcu, &hw, N2AP_IPI_VENC_VP8, 100), -EINVAL);
		CHECK(bad == NULL);
	}
	pthread_mutex_lock(&dc.lock);
	CHECK_EQ_U32(dc.rec.n_init, 0u);
	pthread_mutex_unlock(&dc.lock);

	/* INIT answered with a failure status */
	dc_reset_records(&dc);
	pthread_mutex_lock(&dc.lock);
	dc.cfg.init_status = 1;
	pthread_mutex_unlock(&dc.lock);
	{
		struct n2ap_venc *bad = NULL;

		CHECK_EQ_I(n2ap_venc_open(&bad, vcu, &hw, N2AP_FOURCC_H264, 200), -EINVAL);
		CHECK(bad == NULL);
		pthread_mutex_lock(&dc.lock);
		dc.cfg.init_status = 0;
		pthread_mutex_unlock(&dc.lock);
	}

	/* INIT answered with the wrong instance cookie: the handler refuses it (returns 1, no ack),
	 * so the AP times out exactly like the kernel's ack wait would. */
	dc_reset_records(&dc);
	pthread_mutex_lock(&dc.lock);
	dc.cfg.init_bad_inst = 1;
	pthread_mutex_unlock(&dc.lock);
	{
		struct n2ap_venc *bad = NULL;

		CHECK_EQ_I(n2ap_venc_open(&bad, vcu, &hw, N2AP_FOURCC_H264, 200), -ETIMEDOUT);
		CHECK(bad == NULL);
		pthread_mutex_lock(&dc.lock);
		dc.cfg.init_bad_inst = 0;
		pthread_mutex_unlock(&dc.lock);
	}

	/* query with an error status and with a mis-echoed instance pointer */
	{
		struct n2ap_venc *q = open_h264(vcu, &hw, 300);
		struct mtk_video_fmt fmts[MTK_MAX_ENC_CODECS_SUPPORT];

		if (q != NULL) {
			pthread_mutex_lock(&dc.lock);
			dc.cfg.cap_status = 1;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2ap_venc_query_cap(q, 0, fmts, sizeof(fmts)), -EINVAL);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.cap_status = 0;
			dc.cfg.cap_bad_inst = 1;
			pthread_mutex_unlock(&dc.lock);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.cap_bad_inst = 0;
			dc.cfg.cap_bad_addr = 1;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2ap_venc_query_cap(q, 0, fmts, sizeof(fmts)), -EIO);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.cap_bad_addr = 0;
			dc.cfg.cap_bad_inst = 1;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2ap_venc_query_cap(q, 0, fmts, sizeof(fmts)), -ETIMEDOUT);
			CHECK_EQ_I(n2ap_venc_aborted(q), 1);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.cap_bad_inst = 0;
			pthread_mutex_unlock(&dc.lock);
			CHECK_EQ_I(n2ap_venc_close(q), 0);   /* aborted: no DEINIT */
		}
	}

	/* ---- the timeout path: the daemon fetches and never answers ---- */
	{
		struct n2ap_venc *te = open_h264(vcu, &hw, 120);
		uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;
		struct n2ap_enc_result res;

		if (te != NULL) {
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			dc_reset_records(&dc);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 1;
			dc.cfg.wait_isr = 1;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			dc.cfg.skip_encode_reply = 1;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_encode(te, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, -ETIMEDOUT);
			CHECK_EQ_I(n2ap_venc_aborted(te), 1);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.skip_encode_reply = 0;
			dc.cfg.power_on = 0;
			dc.cfg.wait_isr = 0;
			dc.pause = 0;
			pthread_mutex_unlock(&dc.lock);
			/* close must clean the power reference the daemon left on */
			CHECK_EQ_I(n2ap_venc_close(te), 0);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 0u);
			CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC_C1), 0u);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}
	}

	/* ---- H265/HEIF/MPEG4/H263 IPI id selection and profile conversion ---- */
	{
		static const struct { uint32_t fourcc, ipi; } ids[] = {
			{ N2AP_FOURCC_H264, 15u },
			{ N2AP_FOURCC_H265, 16u },
			{ N2AP_FOURCC_HEIF, 17u },
			{ N2AP_FOURCC_MPEG4, 19u },
			{ N2AP_FOURCC_H263, 21u },
		};

		for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
			struct n2ap_venc *s = NULL;
			struct n2ap_enc_param p;

			rc = n2ap_venc_open(&s, vcu, &hw, ids[i].fourcc, 300);
			CHECK_EQ_I(rc, 0);
			if (rc != 0) {
				continue;
			}
			CHECK_EQ_U32(n2ap_venc_ipi_id(s), ids[i].ipi);
			n2ap_enc_param_1080p30_h264(&p);
			p.width = 640;
			p.height = 480;
			p.buf_width = 640;
			p.buf_height = 480;
			p.profile = 2;   /* HEVC MAIN_10 / MPEG4 CORE / H264 HIGH */
			p.level = 6;     /* HEVC 4_1 / MPEG4 4 / H264 2_1 */
			p.tier = 1;      /* HEVC high tier */
			/* H264 exercises the JOINED header mode (two set_params at stream start); H263
			 * asks for it too but the kernel's condition (mtk_vcodec_enc.c:1874-1882)
			 * excludes H263, so it must still send only ENC.  The others use the
			 * SEPARATE default (one). */
			p.seq_hdr_mode = (ids[i].fourcc == N2AP_FOURCC_H264 ||
			                  ids[i].fourcc == N2AP_FOURCC_H263) ? 1u : 0u;
			p.prependheader = 1;   /* vcodecd's setting: SPS/PPS before every IDR */
			dc_reset_records(&dc);
			rc = n2ap_venc_start(s, &p);
			CHECK_EQ_I(rc, 0);
			pthread_mutex_lock(&dc.lock);
			CHECK_EQ_U32(dc.rec.cfg_prepend, 1u);
			CHECK_EQ_U32(dc.rec.n_param, ids[i].fourcc == N2AP_FOURCC_H264 ? 2u : 1u);
			if (ids[i].fourcc == N2AP_FOURCC_HEIF) {
				CHECK_EQ_U32(dc.rec.cfg_profile, 4u);    /* MAIN_10 -> 4 */
				CHECK_EQ_U32(dc.rec.cfg_level, 13u);     /* level 6, high tier -> 2*6+1 */
			} else if (ids[i].fourcc == N2AP_FOURCC_MPEG4) {
				CHECK_EQ_U32(dc.rec.cfg_profile, 2u);    /* CORE -> 2 */
				CHECK_EQ_U32(dc.rec.cfg_level, 6u);      /* LEVEL_4 -> 6 */
			} else if (ids[i].fourcc == N2AP_FOURCC_H263) {
				CHECK_EQ_U32(dc.rec.cfg_profile, 0u);
				CHECK_EQ_U32(dc.rec.cfg_level, 0u);
			} else if (ids[i].fourcc == N2AP_FOURCC_H265) {
				CHECK_EQ_U32(dc.rec.cfg_profile, 4u);    /* MAIN_10 -> 4 */
				CHECK_EQ_U32(dc.rec.cfg_level, 13u);     /* 2*6+1 */
			} else {
				CHECK_EQ_U32(dc.rec.cfg_profile, 2u);    /* H264 passes the V4L2 value */
				CHECK_EQ_U32(dc.rec.cfg_level, 6u);
			}
			pthread_mutex_unlock(&dc.lock);
			/* close() returns the first error of DEINIT + power-down; a daemon that
			 * fails DEINIT is reported, and the session is still freed. */
			pthread_mutex_lock(&dc.lock);
			if (ids[i].fourcc == N2AP_FOURCC_H263) {
				dc.cfg.deinit_status = 1;
			}
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_close(s);
			CHECK_EQ_I(rc, ids[i].fourcc == N2AP_FOURCC_H263 ? -EINVAL : 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.deinit_status = 0;
			pthread_mutex_unlock(&dc.lock);
		}
	}

	/* ---- a threaded daemon like vpud: the receiver re-enters GET_OBJECT before the service
	 *      thread sends the reply.  The AP must wait for the ACK (reply dispatched to the
	 *      handler) first and only then for "dispatched" (IV's real-vpud finding). ---- */
	{
		struct n2ap_venc *re = NULL;
		struct n2ap_enc_param p;
		struct n2ap_enc_result res;
		uint32_t fh = 0, fguest = 0, bh = 0, bguest = 0;

		pthread_mutex_lock(&dc.lock);
		dc.cfg.reget_before_reply = 1;
		pthread_mutex_unlock(&dc.lock);
		rc = n2ap_venc_open(&re, vcu, &hw, N2AP_FOURCC_H264, 1000);
		CHECK_EQ_I(rc, 0);
		if (rc == 0) {
			n2ap_enc_param_1080p30_h264(&p);
			CHECK_EQ_I(n2ap_venc_start(re, &p), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, NULL, &fguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, NULL, &bguest), 0);
			pthread_mutex_lock(&dc.lock);
			dc.cfg.power_on = 0;
			dc.cfg.power_off = 0;
			dc.cfg.wait_isr = 0;
			dc.cfg.put_buffer = 0;
			dc.cfg.write_bs = 0;
			dc.cfg.enc_status = 0;
			dc.cfg.enc_size = 0x99;
			dc.cfg.enc_key = 1;
			pthread_mutex_unlock(&dc.lock);
			rc = n2ap_venc_encode(re, fh, bh, N2AP_ENC_FRAME, &res);
			CHECK_EQ_I(rc, 0);
			CHECK_EQ_U32(res.bytes, 0x99u);
			CHECK_EQ_U32(res.is_key_frm, 1u);
			CHECK_EQ_I(n2ap_venc_close(re), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
			CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
		}
		pthread_mutex_lock(&dc.lock);
		dc.cfg.reget_before_reply = 0;
		pthread_mutex_unlock(&dc.lock);
	}

	/* ---- DMA mirror: direct and threaded ---- */
	test_dma_direct(mem, &hw, &fake);
	test_dma_threads(mem, &hw, &fake);

	dc_stop(&dc);
	pthread_mutex_lock(&dc.lock);
	CHECK_EQ_I(dc.daemon_fail, 0u);
	if (dc.daemon_fail != 0) {
		fprintf(stderr, "daemon failure: %s\n", dc.daemon_msg);
	}
	pthread_mutex_unlock(&dc.lock);
	pthread_mutex_destroy(&dc.lock);

	/* free whatever the fake DMA pool still holds */
	for (unsigned i = 0; i < HWFAKE_MAX_DMA; i++) {
		if (fake.dma[i].used) {
			free(fake.dma[i].host);
			fake.dma[i].used = 0;
		}
	}
	n2vcu_destroy(vcu);
out_mem:
	n2a32_mem_destroy(mem);
out:
	printf("V41 build %s %s %u/%u\n", g_build, g_fail == 0 ? "PASS" : "FAIL", g_pass,
	       g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
