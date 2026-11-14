/* SPDX-License-Identifier: MIT */
/*
 * t_v41_real.c -- the V41 API driven through V40's *real* vcu/n2vcu.c instead of the stand-in.
 *
 * The fake daemon here does what vpud does: it calls the /dev/vcu model's ioctl entry points with
 * VCU_GET_OBJECT / VCU_SET_OBJECT and a share_obj in guest memory, exactly the ABI the daemon sees
 * (vcu/n2vcu.c:781-850).  Only a small session is exercised (open, query, start, one frame,
 * close); the full protocol matrix lives in t_v41.c on the stand-in, and the two together prove
 * that ap/n2ap_venc.c works on both sides of the seam.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2ap_venc.h"
#include "n2vc_hw.h"
#include "vcu/mtk_vcu_abi.h"
#include "vcu/n2vcu.h"

#include "hw_fake.h"

static unsigned g_pass, g_fail;

#define CHECK(cond) do { \
	if (cond) { g_pass++; } else { \
		g_fail++; \
		if (g_fail <= 30) { fprintf(stderr, "REAL FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
	} \
} while (0)
#define CHECK_EQ_U32(got, want) do { \
	uint32_t _g = (uint32_t)(got), _w = (uint32_t)(want); \
	if (_g == _w) { g_pass++; } else { \
		g_fail++; \
		if (g_fail <= 30) { fprintf(stderr, "REAL FAIL %s:%d: %s = 0x%x, want 0x%x\n", \
			__FILE__, __LINE__, #got, _g, _w); } \
	} \
} while (0)
#define CHECK_EQ_I(got, want) do { \
	long long _g = (long long)(got), _w = (long long)(want); \
	if (_g == _w) { g_pass++; } else { \
		g_fail++; \
		if (g_fail <= 30) { fprintf(stderr, "REAL FAIL %s:%d: %s = %lld, want %lld\n", \
			__FILE__, __LINE__, #got, _g, _w); } \
	} \
} while (0)

struct rd {
	struct n2vcu *vcu;
	const struct n2vfs_dev *dev;
	struct n2a32_cpu *cpu;
	struct n2a32_mem *mem;
	struct hwfake *hw;
	uint32_t dmem_base;
	uint32_t sh;          /* guest share_obj */
	uint32_t vsi_addr;
	uint64_t cookie;
	int run;
	pthread_t th;
	pthread_mutex_t lock;
	unsigned n_init, n_param, n_encode, n_deinit, n_cap;
	uint32_t enc_mode, enc_bs_size, enc_bs_addr, enc_input, enc_planes;
	uint32_t wait_status, wait_timeout;
	uint32_t cfg_w, cfg_h, cfg_fps, cfg_br, cfg_input;
};

static void share_put(struct rd *r, int id, const void *msg, uint32_t len)
{
	struct n2a32_cpu *c = r->cpu;

	n2a32_st32(c, r->sh + 0, (uint32_t)id);
	n2a32_st32(c, r->sh + 4, len);
	for (uint32_t i = 0; i < 80u; i++) {
		n2a32_st8(c, r->sh + 8 + i, i < len ? ((const uint8_t *)msg)[i] : 0);
	}
}

/* One VCU_GET_OBJECT; returns 0 and the message, or -errno. */
static int vcu_get(struct rd *r, int hint_id, int *out_id, uint8_t *buf, uint32_t *out_len)
{
	struct n2a32_cpu *c = r->cpu;
	uint32_t id = (uint32_t)hint_id;
	uint32_t len;
	int rc;

	n2a32_st32(c, r->sh + 0, id);
	n2a32_st32(c, r->sh + 4, 0);
	for (uint32_t i = 0; i < 80u; i++) {
		n2a32_st8(c, r->sh + 8 + i, 0);
	}
	rc = r->dev->ioctl(r->dev->ctx, c, MTK_VCU_GET_OBJECT, r->sh);
	if (rc != 0) {
		return rc;
	}
	id = n2a32_ld32(c, r->sh + 0);
	len = n2a32_ld32(c, r->sh + 4);
	if (len > 80u) {
		len = 80u;
	}
	for (uint32_t i = 0; i < len; i++) {
		buf[i] = n2a32_ld8(c, r->sh + 8 + i);
	}
	*out_id = (int)id;
	*out_len = len;
	return 0;
}

/* One VCU_SET_OBJECT; the model dispatches to the registered AP handler and copies the payload
 * (possibly modified by the handler) back into our guest buffer. */
static int vcu_set(struct rd *r, int id, void *msg, uint32_t len)
{
	struct n2a32_cpu *c = r->cpu;
	int rc;

	share_put(r, id, msg, len);
	rc = r->dev->ioctl(r->dev->ctx, c, MTK_VCU_SET_OBJECT, r->sh);
	/* mtk_vcu.c:1814-1821 copies the (handler-modified) share_obj back to the daemon. */
	for (uint32_t i = 0; i < len; i++) {
		((uint8_t *)msg)[i] = n2a32_ld8(c, r->sh + 8 + i);
	}
	return rc;
}

static void *rdaemon(void *arg)
{
	struct rd *r = arg;
	uint8_t buf[80];

	for (;;) {
		uint32_t msg_id, len = 0;
		int id = 15, rc;

		pthread_mutex_lock(&r->lock);
		if (!r->run) {
			pthread_mutex_unlock(&r->lock);
			break;
		}
		pthread_mutex_unlock(&r->lock);
		if (vcu_get(r, 15, &id, buf, &len) != 0) {
			continue;
		}
		memcpy(&msg_id, buf, 4);
		if (msg_id == AP_IPIMSG_ENC_INIT) {
			struct venc_ap_ipi_msg_init m;
			struct venc_vcu_ipi_msg_init out;
			struct venc_vsi *vsi = n2a32_ptr(r->mem, r->vsi_addr);

			memcpy(&m, buf, sizeof(m));
			memset(vsi, 0, sizeof(*vsi));
			vsi->sizeimage[0] = 0x300000;
			memset(&out, 0, sizeof(out));
			out.msg_id = VCU_IPIMSG_ENC_INIT_DONE;
			out.venc_inst = m.venc_inst;
			out.vcu_inst_addr = r->vsi_addr;
			pthread_mutex_lock(&r->lock);
			r->cookie = m.venc_inst;
			r->n_init++;
			pthread_mutex_unlock(&r->lock);
			rc = vcu_set(r, id, &out, sizeof(out));
			(void)rc;
		} else if (msg_id == AP_IPIMSG_ENC_SET_PARAM) {
			struct venc_ap_ipi_msg_set_param m;
			struct venc_vcu_ipi_msg_set_param out;
			struct venc_vsi *vsi = n2a32_ptr(r->mem, r->vsi_addr);

			memcpy(&m, buf, sizeof(m));
			pthread_mutex_lock(&r->lock);
			r->n_param++;
			if (m.param_id == VENC_SET_PARAM_ENC) {
				r->cfg_input = vsi->config.input_fourcc;
				r->cfg_w = vsi->config.pic_w;
				r->cfg_h = vsi->config.pic_h;
				r->cfg_fps = vsi->config.framerate;
				r->cfg_br = vsi->config.bitrate;
				vsi->sizeimage[0] = 0x300000;
				vsi->sync_mode = 0;
			}
			memset(&out, 0, sizeof(out));
			out.msg_id = VCU_IPIMSG_ENC_SET_PARAM_DONE;
			out.venc_inst = r->cookie;
			out.param_id = m.param_id;
			out.data_item = m.data_item;
			pthread_mutex_unlock(&r->lock);
			rc = vcu_set(r, id, &out, sizeof(out));
			(void)rc;
		} else if (msg_id == AP_IPIMSG_ENC_ENCODE) {
			struct venc_ap_ipi_msg_enc m;
			struct venc_vcu_ipi_msg_enc out;
			struct venc_vcu_ipi_msg_common req;
			struct venc_vcu_ipi_msg_waitisr wr;

			memcpy(&m, buf, sizeof(m));
			pthread_mutex_lock(&r->lock);
			r->n_encode++;
			r->enc_mode = m.bs_mode;
			r->enc_bs_size = m.bs_size;
			r->enc_bs_addr = m.bs_addr;
			r->enc_input = m.input_addr[0];
			r->enc_planes = m.fb_num_planes;
			pthread_mutex_unlock(&r->lock);
			memset(&req, 0, sizeof(req));
			req.msg_id = VCU_IPIMSG_ENC_POWER_ON;
			req.venc_inst = r->cookie;
			(void)vcu_set(r, id, &req, sizeof(req));
			hwfake_set_irq(r->hw, 0x4);
			memset(&wr, 0, sizeof(wr));
			wr.msg_id = VCU_IPIMSG_ENC_WAIT_ISR;
			wr.venc_inst = r->cookie;
			(void)vcu_set(r, id, &wr, sizeof(wr));
			pthread_mutex_lock(&r->lock);
			r->wait_status = wr.irq_status;
			r->wait_timeout = wr.timeout;
			pthread_mutex_unlock(&r->lock);
			memset(&out, 0, sizeof(out));
			out.msg_id = VCU_IPIMSG_ENC_ENCODE_DONE;
			out.venc_inst = r->cookie;
			out.state = VEN_IPI_MSG_ENC_STATE_FRAME;
			out.is_key_frm = 1;
			out.bs_size = 0x2345;
			(void)vcu_set(r, id, &out, sizeof(out));
		} else if (msg_id == AP_IPIMSG_ENC_DEINIT) {
			struct venc_vcu_ipi_msg_deinit out;

			pthread_mutex_lock(&r->lock);
			r->n_deinit++;
			memset(&out, 0, sizeof(out));
			out.msg_id = VCU_IPIMSG_ENC_DEINIT_DONE;
			out.venc_inst = r->cookie;
			pthread_mutex_unlock(&r->lock);
			(void)vcu_set(r, id, &out, sizeof(out));
		} else if (msg_id == AP_IPIMSG_ENC_QUERY_CAP) {
			struct venc_ap_ipi_query_cap m;
			struct venc_vcu_ipi_query_cap_ack out;
			uint32_t cap_addr = r->dmem_base + 0x8000u;
			struct mtk_video_fmt *f = n2a32_ptr(r->mem, cap_addr);

			memcpy(&m, buf, sizeof(m));
			memset(f, 0, 64u * 12u);
			f[0].fourcc = N2AP_FOURCC_H264;
			f[0].type = 1;
			f[0].num_planes = 1;
			memset(&out, 0, sizeof(out));
			out.msg_id = VCU_IPIMSG_ENC_QUERY_CAP_ACK;
			out.ap_inst_addr = m.ap_inst_addr;
			out.id = m.id;
			out.ap_data_addr = m.ap_data_addr;
			out.vcu_data_addr = cap_addr;
			pthread_mutex_lock(&r->lock);
			r->n_cap++;
			pthread_mutex_unlock(&r->lock);
			(void)vcu_set(r, id, &out, sizeof(out));
		}
	}
	return NULL;
}

int main(void)
{
	struct n2a32_mem *mem;
	struct hwfake fake;
	struct n2vc_hw hw;
	struct n2vcu *vcu;
	struct n2a32_cpu cpu;
	struct rd r;
	struct n2ap_venc *e = NULL;
	struct n2ap_enc_param p;
	struct n2ap_enc_result res;
	struct mtk_video_fmt fmts[MTK_MAX_ENC_CODECS_SUPPORT];
	uint32_t base = 0, dlen = 0, fh = 0, fpa = 0, fguest = 0, bh = 0, bpa = 0, bguest = 0;
	int rc;

	mem = n2a32_mem_create();
	CHECK(mem != NULL);
	if (mem == NULL) {
		return 1;
	}
	hwfake_init(&hw, &fake, RW_CLAIM_VCODEC_PA, RW_CLAIM_VCODEC_PA + RW_CLAIM_VCODEC_BYTES);
	vcu = n2vcu_create(mem, &hw);
	CHECK(vcu != NULL);
	if (vcu == NULL) {
		goto out;
	}
	n2vcu_set_get_timeout_ms(vcu, 100);   /* the model's default 10 s would stall teardown */
	memset(&r, 0, sizeof(r));
	r.vcu = vcu;
	r.mem = mem;
	r.hw = &fake;
	r.dev = n2vcu_dev(vcu);
	CHECK(r.dev != NULL);
	if (r.dev == NULL) {
		goto out_vcu;
	}
	/* n2vcu.c only uses the little load/store helpers on this cpu, which need c->mem; the
	 * interpreter itself is not linked here (t_v41.c exercises the AP code, not the guest). */
	memset(&cpu, 0, sizeof(cpu));
	cpu.mem = mem;
	cpu.thumb = 1;
	r.cpu = &cpu;
	r.sh = n2a32_mem_alloc(mem, 0x1000, 8);
	CHECK(r.sh != 0);
	CHECK_EQ_I(n2vcu_dmem(vcu, &base, &dlen), 0);
	CHECK_EQ_U32(dlen, 0x80000u);
	r.dmem_base = base;
	r.vsi_addr = base + 0x1000u;
	pthread_mutex_init(&r.lock, NULL);
	r.run = 1;
	pthread_create(&r.th, NULL, rdaemon, &r);

	rc = n2ap_venc_open(&e, vcu, &hw, N2AP_FOURCC_H264, 500);
	CHECK_EQ_I(rc, 0);
	if (rc == 0) {
		CHECK_EQ_U32(n2ap_venc_ipi_id(e), 15u);
		CHECK(n2ap_venc_vsi(e) != NULL);
		rc = n2ap_venc_query_cap(e, 0, fmts, sizeof(fmts));
		CHECK_EQ_I(rc, 0);
		CHECK_EQ_U32(fmts[0].fourcc, N2AP_FOURCC_H264);
		n2ap_enc_param_1080p30_h264(&p);
		rc = n2ap_venc_start(e, &p);
		CHECK_EQ_I(rc, 0);
		pthread_mutex_lock(&r.lock);
		CHECK_EQ_U32(r.cfg_input, 6u);
		CHECK_EQ_U32(r.cfg_w, 1920u);
		CHECK_EQ_U32(r.cfg_h, 1080u);
		CHECK_EQ_U32(r.cfg_fps, 30u);
		CHECK_EQ_U32(r.cfg_br, 20000000u);
		pthread_mutex_unlock(&r.lock);
		CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &fh, &fpa, &fguest), 0);
		CHECK_EQ_I(n2vc_dma_guest_alloc(&hw, mem, 0x4000, 0, &bh, &bpa, &bguest), 0);
		rc = n2ap_venc_encode(e, fh, bh, N2AP_ENC_FRAME, &res);
		CHECK_EQ_I(rc, 0);
		CHECK_EQ_U32(res.bytes, 0x2345u);
		CHECK_EQ_U32(res.is_key_frm, 1u);
		pthread_mutex_lock(&r.lock);
		CHECK_EQ_U32(r.enc_mode, 3u);
		CHECK_EQ_U32(r.enc_planes, 1u);
		CHECK_EQ_U32(r.enc_input, fpa);
		CHECK_EQ_U32(r.enc_bs_addr, bpa);
		CHECK_EQ_U32(r.wait_status, 0x4u);
		CHECK_EQ_U32(r.wait_timeout, 0u);
		pthread_mutex_unlock(&r.lock);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 1u);
		CHECK_EQ_I(n2ap_venc_close(e), 0);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC), 0u);
		CHECK_EQ_U32(hwfake_refs(&fake, RWVC_BLK_VENC_C1), 0u);
		pthread_mutex_lock(&r.lock);
		CHECK_EQ_U32(r.n_init, 1u);
		CHECK_EQ_U32(r.n_param, 1u);   /* SEPARATE header mode: only ENC at stream start */
		CHECK_EQ_U32(r.n_encode, 1u);
		CHECK_EQ_U32(r.n_deinit, 1u);
		CHECK_EQ_U32(r.n_cap, 1u);
		pthread_mutex_unlock(&r.lock);
		CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, bguest), 0);
		CHECK_EQ_I(n2vc_dma_guest_unmap(&hw, mem, fguest), 0);
	}

	pthread_mutex_lock(&r.lock);
	r.run = 0;
	pthread_mutex_unlock(&r.lock);
	n2vcu_abort(vcu);   /* break the daemon's VCU_GET_OBJECT wait */
	pthread_join(r.th, NULL);
	pthread_mutex_destroy(&r.lock);
	for (unsigned i = 0; i < HWFAKE_MAX_DMA; i++) {
		if (fake.dma[i].used) {
			free(fake.dma[i].host);
			fake.dma[i].used = 0;
		}
	}
out_vcu:
	n2vcu_destroy(vcu);
out:
	n2a32_mem_destroy(mem);
	printf("V41 real n2vcu %s %u/%u\n", g_fail == 0 ? "PASS" : "FAIL", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
