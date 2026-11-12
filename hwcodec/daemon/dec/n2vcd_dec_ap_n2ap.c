/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dec_ap_n2ap.c -- the production AP ops of the decoder service, over DEC01's n2ap_vdec.
 *
 * Session shape (the reference driver dec1/vdec_ref.c, host-verified by opus-dec1):
 *   header  = START with no frame buffer (vsi.dec.index = 0xFF) -> vsi.pic/crop/dpb.
 *   input   = START with one free pool buffer (synchronous mode; the kernel's PUT_FRAME_BUFFER is
 *             not needed), then harvest list_disp/list_free.  The vendor copies the AU into its own
 *             ring and returns the bitstream cookie in list_free_bs within the same START, so the
 *             client's input buffer is reusable as soon as the call returns.
 *   frame buffers: one client DMA buffer per entry (Y at 0, C at stride*buf_h).  n2ap_vdec.c has
 *             the matching branch (job.fb_c_handle == job.fb_handle -> c_pa = fb_pa + fb_sz[0]),
 *             so no synthetic second handle and no plane offset is invented here.
 *
 * The vendor imports each plane by an ion share id; this file publishes each client handle with
 * n2shim_ion_register_share() and keeps one id per handle.  Share ids start at 0x580 (V41's
 * encoder owns 0x520..0x55f); 64 slots are enough for 16 pool entries x 2 planes + 8 bitstream
 * buffers, and a new id is allocated only when a handle is first seen.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2vcd_dec_ap_n2ap.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "mtk_vdec_ipi.h"
#include "n2ap_venc.h"          /* n2vc_dma_guest_register/lookup/forget */
#include "n2ap_vdec.h"
#include "n2vc_hw.h"
#include "n2a32.h"
#include "n2vcu.h"

/* V34's AP-side publish hook (implemented by the shim; weak so a compile-only build links). */
extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa,
                                     uint32_t bytes) __attribute__((weak));
/* opus-decint: frees a foreign share slot (V34's table is 64 entries for the whole daemon). */
extern int n2shim_ion_unregister_share(int share_id) __attribute__((weak));

#define AP_MAX_POOL N2VCD_DEC_MAX_POOL
#define AP_MAX_IN 8u
#define AP_MAX_ENT (AP_MAX_POOL + AP_MAX_IN)

enum { ENT_FREE = 0, ENT_INFLIGHT = 1, ENT_DISPLAY = 2, ENT_HELD = 3 };

struct ap_ent {
	uint32_t handle, c_handle;
	uint32_t pa, c_pa, bytes, c_bytes;
	int32_t share, c_share;
	uint8_t state;          /* ENT_* */
	uint8_t vendor_freed;   /* list_free already returned it */
	uint8_t is_input;
};

struct ap_pic {
	uint32_t index;
	uint64_t pts;
	int32_t poc;
};

struct share_map {
	uint32_t handle;
	int32_t id;
};

struct n2vcd_dec_ap_n2ap {
	struct n2vcd_dec_ap_n2ap_cfg cfg;
	struct n2ap_vdec *dec;
	uint32_t codec;
	struct ap_ent ent[AP_MAX_ENT];
	unsigned nent;          /* registered pool entries (0..AP_MAX_POOL) */
	unsigned nin;           /* input-handle slots at ent[AP_MAX_POOL..] */
	struct ap_pic picq[AP_MAX_POOL];
	unsigned npic;
	uint32_t geometry_valid;
	uint32_t inputs, pictures, flushes;
	int changed_seen;
	/* Ion share ids are process-wide in V34 and a foreign share is never freed, so a handle keeps
	 * its id for the daemon's lifetime (re-registering a used id is -EINVAL).  V41's encoder owns
	 * 0x520..0x55f; this map owns 0x580.. and reuses an id for a handle the kernel hands out
	 * again in a later session.  64 entries is V34's ion table size. */
	struct share_map shares[64];
	unsigned nshares;
	unsigned held;          /* opus-decint: session power references held (1 = VDEC_SOC, 2 = VDEC_CORE) */
	unsigned opens;         /* sessions opened (write_combining == 2 alternates on it) */
	int wc;                 /* write-combining on for the current session */
	int32_t next_share;
};

/* ------------------------------------------------------------------ helpers */

static int
dma_ensure(struct n2vcd_dec_ap_n2ap *a, uint32_t handle, uint32_t pa, uint32_t bytes)
{
	uint32_t gpa = 0, gguest = 0, gbytes = 0;
	int r;

	if (handle == 0u || pa == 0u || bytes == 0u) {
		return -EINVAL;
	}
	if (n2vc_dma_lookup_handle(handle, &gpa, &gguest, &gbytes) == 0 && gpa == pa) {
		return 0;
	}
	r = n2vc_dma_guest_register(a->cfg.hw, NULL, handle, pa, bytes, NULL);
	if (r == -EEXIST) {
		(void)n2vc_dma_guest_forget(a->cfg.hw, handle);
		r = n2vc_dma_guest_register(a->cfg.hw, NULL, handle, pa, bytes, NULL);
	}
	return r;
}

static int
share_id_for(struct n2vcd_dec_ap_n2ap *a, uint32_t handle, uint32_t pa, uint32_t bytes)
{
	int s;

	if (n2shim_ion_register_share == NULL) {
		return -EOPNOTSUPP;
	}
	for (unsigned i = 0; i < a->nshares; i++) {
		if (a->shares[i].handle == handle) {
			return a->shares[i].id;         /* same handle, same id: V34 keeps it */
		}
	}
	if (a->next_share == 0) {
		a->next_share = (int32_t)(a->cfg.share_base ? a->cfg.share_base : 0x580u);
	}
	while (a->nshares < 64u) {
		s = a->next_share++;
		if (n2shim_ion_register_share(s, handle, pa, bytes) == 0) {
			a->shares[a->nshares].handle = handle;
			a->shares[a->nshares].id = s;
			a->nshares++;
			return s;
		}
	}
	return -ENOSPC;
}

static int
share_of(struct n2vcd_dec_ap_n2ap *a, struct ap_ent *e)
{
	int s;

	if (e->share >= 0) {
		return e->share;
	}
	s = share_id_for(a, e->handle, e->pa, e->bytes);
	if (s < 0) {
		return s;
	}
	e->share = s;
	if (e->c_handle != 0u && e->c_handle != e->handle) {
		int cs = share_id_for(a, e->c_handle, e->c_pa, e->c_bytes);

		if (cs < 0) {
			return cs;
		}
		e->c_share = cs;
	} else {
		e->c_share = s;
	}
	return s;
}

static void
drop_entries(struct n2vcd_dec_ap_n2ap *a)
{
	for (unsigned i = 0; i < AP_MAX_ENT; i++) {
		struct ap_ent *e = &a->ent[i];

		if (e->handle != 0u) {
			(void)n2vc_dma_guest_forget(a->cfg.hw, e->handle);
		}
		if (e->c_handle != 0u && e->c_handle != e->handle) {
			(void)n2vc_dma_guest_forget(a->cfg.hw, e->c_handle);
		}
	}
	memset(a->ent, 0, sizeof(a->ent));
	for (unsigned i = 0; i < AP_MAX_ENT; i++) {
		a->ent[i].share = -1;
		a->ent[i].c_share = -1;
	}
	a->nent = 0;
	a->nin = 0;
	a->npic = 0;
	a->geometry_valid = 0;
	a->changed_seen = 0;
}

static struct ap_ent *
entry_by_handle(struct n2vcd_dec_ap_n2ap *a, uint32_t handle)
{
	for (unsigned i = 0; i < a->nent; i++) {
		if (a->ent[i].handle == handle) {
			return &a->ent[i];
		}
	}
	return NULL;
}

/* Drain list_disp / list_free into our state and picture queue. */
static int
harvest(struct n2vcd_dec_ap_n2ap *a)
{
	struct n2ap_vdec_out o;
	struct n2ap_vdec_freed f;
	int r;

	while ((r = n2ap_vdec_pop_displayed(a->dec, &o)) == 1) {
		struct ap_ent *e = entry_by_handle(a, (uint32_t)o.cookie);

		if (!e) {
			continue;
		}
		if (e->state == ENT_INFLIGHT) {
			e->state = ENT_DISPLAY;
		}
		if (a->npic < AP_MAX_POOL) {
			a->picq[a->npic].index = (uint32_t)(e - a->ent);
			a->picq[a->npic].pts = o.timestamp;
			a->picq[a->npic].poc = o.poc;
			a->npic++;
		}
	}
	if (r < 0) {
		return r;
	}
	while ((r = n2ap_vdec_pop_freed(a->dec, &f)) == 1) {
		struct ap_ent *e = entry_by_handle(a, (uint32_t)f.cookie);

		if (!e) {
			continue;
		}
		e->vendor_freed = 1;
		if (e->state == ENT_HELD) {
			e->state = ENT_FREE;
			e->vendor_freed = 0;
		}
	}
	if (r < 0) {
		return r;
	}
	return 0;
}

/* opus-decint (found by decint/run_sim.sh): the client's bitstream lives in ITS DMA buffer; the vendor reads the AU through
 * a guest mapping (V34 ion_mmap -> n2vc_dma_guest_map), which is created zeroed and never filled from the device, and
 * n2ap_vdec then syncs that guest copy TO the device before the START (DEC01's model: the AP wrote the guest copy).  For
 * a client buffer the device is the truth: map it here (the vendor's later mmap returns this same mapping) and copy the
 * AU device -> guest before every START.  Without this the vendor parses zeros ("There is no any H264 instance",
 * Vdec_Drv_H264_open -10) and every later AU is overwritten on the device by the previous guest copy. */
static int
bs_refresh(struct n2vcd_dec_ap_n2ap *a, uint32_t handle, uint32_t alloc_bytes, uint32_t au_bytes)
{
	uint32_t guest = 0;
	int r;

	if (!a->cfg.mem) {
		return -EINVAL;
	}
	r = n2vc_dma_guest_map(a->cfg.hw, a->cfg.mem, handle, alloc_bytes, &guest);
	if (r != 0) {
		return r;
	}
	r = n2vc_dma_sync(a->cfg.hw, a->cfg.mem, guest, 0, au_bytes, 0);
	if (r != 0) {
		return r;
	}
	/* opus-decint: zero the bytes after the AU (the client buffer is reused, so they hold the tail of an earlier,
	 * longer AU).  The vendor's HEVC start-code search reads past the end of the AU ("find NALs i4index > 
	 * u4InputDataSize") and found stale start codes there on the PC; zeros contain no start code.  n2ap_vdec's
	 * to_device sync carries the zeros to the device copy the hardware reads. */
	if (au_bytes < alloc_bytes) {
		uint32_t pad = alloc_bytes - au_bytes < 4096u ? alloc_bytes - au_bytes : 4096u;
		uint8_t *g = (uint8_t *)n2a32_ptr(a->cfg.mem, guest + au_bytes);
		if (g) {
			memset(g, 0, pad);
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ ops */

static void release_held(struct n2vcd_dec_ap_n2ap *a);

static int
ap_open(void *ctx, uint32_t codec)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	struct n2ap_vdec_cfg cfg;
	uint32_t fourcc;
	int r;

	if (!a || !a->cfg.vcu || !a->cfg.hw) {
		return -EINVAL;
	}
	if (codec == N2VCD_CODEC_H264) {
		fourcc = N2AP_VDEC_FOURCC_H264;
	} else if (codec == N2VCD_CODEC_HEVC) {
		fourcc = N2AP_VDEC_FOURCC_H265;
	} else if (codec == N2VCD_CODEC_VP8) {          /* opus-fmt: facts/FORMATS.md */
		fourcc = N2AP_VDEC_FOURCC_VP8;
	} else if (codec == N2VCD_CODEC_VP9) {
		fourcc = N2AP_VDEC_FOURCC_VP9;
	} else if (codec == N2VCD_CODEC_AV1) {
		fourcc = N2AP_VDEC_FOURCC_AV1;
	} else if (codec == N2VCD_CODEC_MPEG2) {
		fourcc = N2AP_VDEC_FOURCC_MPEG2;
	} else if (codec == N2VCD_CODEC_MPEG4) {
		fourcc = N2AP_VDEC_FOURCC_MPEG4;
	} else if (codec == N2VCD_CODEC_H263) {
		fourcc = N2AP_VDEC_FOURCC_H263;
	} else {
		return -EOPNOTSUPP;
	}
	if (a->dec) {
		(void)n2ap_vdec_close(a->dec);
		a->dec = NULL;
	}
	release_held(a);
	drop_entries(a);
	memset(&cfg, 0, sizeof(cfg));
	cfg.vcu = a->cfg.vcu;
	cfg.hw = a->cfg.hw;
	cfg.codec_fourcc = fourcc;
	cfg.timeout_ms = a->cfg.timeout_ms;
	cfg.fb_num_planes = 2;          /* MM21 has a chroma plane */
	cfg.set_decode_mode = 1;
	cfg.decode_mode = 0;
	cfg.output_fourcc = N2AP_VDEC_FOURCC_MM21;     /* DEC01: a non-MM21 header fails -ENOTSUP, never UFO */
	cfg.fake_irq = a->cfg.fake_irq;
	cfg.query_cap = 1;      /* opus-decint: as the kernel (vdec_vcu_if.c) and dec1/vdec_ref.c: QUERY_CAP before INIT */
	r = n2ap_vdec_open(&a->dec, &cfg);
	if (r != 0) {
		a->dec = NULL;
		return r;
	}
	if (a->cfg.trace) {
		n2ap_vdec_set_trace(a->dec, a->cfg.trace, a->cfg.trace_arg);
	}
	/* opus-decint (phone run vc19): hold one VDEC_SOC + VDEC_CORE reference for the whole session, so the vendor's
	 * per-frame LOCK/UNLOCK does not power-cycle the domains every picture (Linux keeps them up through runtime PM; on
	 * q10je every VDEC_CORE power-off timed out on the bus-protection ack, 43 ms per frame).  Released at close. */
	{
		int wc = a->cfg.write_combining == 1 || (a->cfg.write_combining == 2 && (a->opens & 1u));
		a->opens++;
		n2vcu_set_write_combining(a->cfg.vcu, wc);
		a->wc = wc;
		if (a->cfg.trace) {
			a->cfg.trace(a->cfg.trace_arg, "wc", (uint32_t)wc, a->opens, 0);
		}
	}
	a->held = 0;
	if (a->cfg.hw->power && a->cfg.hw->power(a->cfg.hw->ctx, RWVC_BLK_VDEC_SOC, 1) == 0) {
		a->held |= 1u;
		if (a->cfg.hw->power(a->cfg.hw->ctx, RWVC_BLK_VDEC_CORE, 1) == 0) {
			a->held |= 2u;
		}
	}
	a->codec = codec;
	return 0;
}

static void
release_held(struct n2vcd_dec_ap_n2ap *a)
{
	if (a->held & 2u) {
		(void)a->cfg.hw->power(a->cfg.hw->ctx, RWVC_BLK_VDEC_CORE, 0);
	}
	if (a->held & 1u) {
		(void)a->cfg.hw->power(a->cfg.hw->ctx, RWVC_BLK_VDEC_SOC, 0);
	}
	a->held = 0;
}

static int
ap_header(void *ctx, const struct n2vcd_dec_ap_buffer *in, uint32_t in_bytes,
          struct n2vcd_dec_ap_geometry *geo)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	struct n2ap_vdec_job job;
	struct vdec_vsi *v;
	int r;

	if (!a || !a->dec || !in || !geo) {
		return -EINVAL;
	}
	r = dma_ensure(a, in->handle, in->pa, in->bytes);
	if (r != 0) {
		return r;
	}
	struct ap_ent *ie = NULL;

	for (unsigned i = AP_MAX_POOL; i < AP_MAX_POOL + a->nin; i++) {
		if (a->ent[i].handle == in->handle) {
			ie = &a->ent[i];
			break;
		}
	}
	if (!ie && a->nin < AP_MAX_IN) {
		ie = &a->ent[AP_MAX_POOL + a->nin];
		ie->handle = in->handle;
		ie->pa = in->pa;
		ie->bytes = in->bytes;
		ie->share = -1;
		ie->c_share = -1;
		ie->is_input = 1;
		a->nin++;
	}
	if (!ie) {
		return -ENOMEM;
	}
	r = share_of(a, ie);
	if (r < 0) {
		return r;
	}
	memset(&job, 0, sizeof(job));
	job.bs_handle = in->handle;
	job.bs_fd = ie->share;
	job.bs_bytes = in->bytes;
	job.bs_length = in_bytes;
	job.fb_handle = 0;
	job.fb_index = 0xFF;
	r = bs_refresh(a, in->handle, in->bytes, in_bytes);
	if (r != 0) {
		return r;
	}
	r = n2ap_vdec_decode(a->dec, &job);
	if (r != 0) {
		return r;
	}
	r = harvest(a);
	if (r != 0) {
		return r;
	}
	v = n2ap_vdec_vsi(a->dec);
	if (!v) {
		return -EIO;
	}
	uint32_t w = v->crop.width ? v->crop.width : v->pic.pic_w;
	uint32_t h = v->crop.height ? v->crop.height : v->pic.pic_h;

	if (w == 0u || h == 0u || v->pic.buf_w == 0u || v->pic.buf_h == 0u) {
		return -EPROTO;
	}
	if (v->crop.left != 0 || v->crop.top != 0) {
		/* The wire text carries the display size, not an origin.  Every measured stream has 0,0
		 * (DEC03); a nonzero origin would silently shift DEC06's detile, so refuse it instead. */
		return -EOPNOTSUPP;
	}
	uint32_t y = v->pic.buf_w * v->pic.buf_h;
	uint32_t total = v->pic.fb_sz[0] + v->pic.fb_sz[1];

	if (total == 0u) {
		total = y + y / 2u;
	}
	memset(geo, 0, sizeof(*geo));
	geo->width = w;
	geo->height = h;
	geo->stride = v->pic.buf_w;
	geo->buf_h = v->pic.buf_h;
	geo->out_buf_bytes = total;
	geo->dpb_frames = v->dec.dpb_sz + 3u;
	if (geo->dpb_frames > AP_MAX_POOL) {
		geo->dpb_frames = AP_MAX_POOL;
	}
	if (geo->dpb_frames < 1u) {
		geo->dpb_frames = 1u;
	}
	if (v->pic.fourcc == 0u || v->pic.fourcc == 0x31324d4du /* 'MM21' */) {
		geo->fourcc = N2VCD_FMT_MM21;
	} else {
		return -EOPNOTSUPP;
	}
	a->geometry_valid = 1;
	return 0;
}

static int
ap_add_buffer(void *ctx, uint32_t index, const struct n2vcd_dec_ap_buffer *y,
              const struct n2vcd_dec_ap_buffer *c)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	struct ap_ent *e;
	int r;

	if (!a || index >= AP_MAX_POOL || !y) {
		return -EINVAL;
	}
	if (index != a->nent) {
		return -EINVAL;
	}
	if (a->nent >= AP_MAX_POOL) {
		return -ENOSPC;
	}
	r = dma_ensure(a, y->handle, y->pa, y->bytes);
	if (r != 0) {
		return r;
	}
	if (c && c->handle != 0u && c->handle != y->handle) {
		r = dma_ensure(a, c->handle, c->pa, c->bytes);
		if (r != 0) {
			return r;
		}
	}
	e = &a->ent[index];
	memset(e, 0, sizeof(*e));
	e->handle = y->handle;
	e->pa = y->pa;
	e->bytes = y->bytes;
	e->c_handle = (c && c->handle != 0u) ? c->handle : 0u;
	e->c_pa = (c && c->handle != 0u && c->handle != y->handle) ? c->pa : y->pa;
	e->c_bytes = c ? c->bytes : 0u;
	e->share = -1;
	e->c_share = -1;
	e->state = ENT_FREE;
	r = share_of(a, e);
	if (r < 0) {
		memset(e, 0, sizeof(*e));
		e->share = -1;
		e->c_share = -1;
		return r;
	}
	a->nent++;
	return 0;
}

static int
ap_input(void *ctx, const struct n2vcd_dec_ap_buffer *in, uint32_t in_bytes, uint64_t pts,
         uint32_t flags, struct n2vcd_dec_ap_result *res)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	struct n2ap_vdec_job job;
	struct ap_ent *fb = NULL, *ie = NULL;
	struct vdec_vsi *v;
	int r;

	(void)flags;
	if (!a || !a->dec || !in || !res) {
		return -EINVAL;
	}
	if (!a->geometry_valid) {
		return -EINVAL;
	}
	(void)harvest(a);
	for (unsigned i = AP_MAX_POOL; i < AP_MAX_POOL + a->nin; i++) {
		if (a->ent[i].handle == in->handle) {
			ie = &a->ent[i];
			break;
		}
	}
	if (!ie) {
		if (a->nin >= AP_MAX_IN) {
			return -ENOMEM;
		}
		ie = &a->ent[AP_MAX_POOL + a->nin];
		r = dma_ensure(a, in->handle, in->pa, in->bytes);
		if (r != 0) {
			return r;
		}
		ie->handle = in->handle;
		ie->pa = in->pa;
		ie->bytes = in->bytes;
		ie->share = -1;
		ie->c_share = -1;
		ie->is_input = 1;
		a->nin++;
	}
	r = share_of(a, ie);
	if (r < 0) {
		return r;
	}
	memset(res, 0, sizeof(*res));
	unsigned npic_before = a->npic;
	/* VDEC_NEED_MORE_OUTPUT_BUF (changed bit 4) means the input was not consumed and the kernel
	 * decodes it again with another capture buffer (mtk_vcodec_dec.c:710-790).  Retry with a
	 * second free reference up to three times; if the vendor still asks, report a change so the
	 * client drains and resends rather than silently losing the AU. */
	for (unsigned attempt = 0;; attempt++) {
		fb = NULL;
		for (unsigned i = 0; i < a->nent; i++) {
			if (a->ent[i].state == ENT_FREE) {
				fb = &a->ent[i];
				break;
			}
		}
		if (!fb) {
			return -EAGAIN;     /* no reference buffer: N2VCD_E_BUSY at the protocol layer */
		}
		memset(&job, 0, sizeof(job));
		job.bs_handle = in->handle;
		job.bs_fd = ie->share;
		job.bs_bytes = in->bytes;       /* allocation size (header: -> data[1]) */
		job.bs_length = in_bytes;       /* valid bytes   (header: -> data[0]) */
		job.fb_handle = fb->handle;
		job.fb_fd = fb->share;
		job.fb_bytes = fb->bytes;
		job.fb_planes = fb->c_bytes ? 2u : 1u;
		job.fb_c_handle = fb->c_bytes ? fb->handle : 0u;   /* one allocation: C after Y */
		job.fb_c_fd = fb->share;
		job.fb_c_bytes = fb->c_bytes;
		job.fb_index = (uint32_t)(fb - a->ent);
		job.timestamp = pts;
		r = bs_refresh(a, in->handle, in->bytes, in_bytes);
		if (r != 0) {
			return r;
		}
		r = n2ap_vdec_decode(a->dec, &job);
		if (r != 0) {
			return r;
		}
		v = n2ap_vdec_vsi(a->dec);
		if (!v) {
			return -EIO;
		}
		uint32_t changed = n2ap_vdec_changed(a->dec);

		res->error_map = v->dec.error_map;
		if ((changed & 4u) != 0u) {
			if (attempt < 2u) {
				continue;       /* not consumed: same input, another free buffer */
			}
			res->changed = 1u;
			a->changed_seen = 1;
			a->npic = npic_before;
			break;
		}
		if ((changed & 1u) != 0u) {
			res->changed = 1u;      /* resolution change: not consumed */
			a->changed_seen = 1;
			break;
		}
		fb->state = ENT_INFLIGHT;
		a->inputs++;
		break;
	}
	r = harvest(a);
	if (r != 0) {
		return r;
	}
	if (res->changed) {
		a->npic = npic_before;  /* a changed START displays nothing (the fake queues one) */
	}
	res->pictures_ready = a->npic;
	return 0;
}

static int
ap_picture(void *ctx, struct n2vcd_dec_ap_picture *pic)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	int r;

	if (!a || !pic) {
		return -EINVAL;
	}
	if (a->npic == 0u) {
		r = harvest(a);
		if (r != 0) {
			return r;
		}
	}
	if (a->npic == 0u) {
		return 0;
	}
	struct ap_pic p = a->picq[0];

	memmove(&a->picq[0], &a->picq[1], (a->npic - 1u) * sizeof(a->picq[0]));
	a->npic--;
	memset(pic, 0, sizeof(*pic));
	pic->index = p.index;
	pic->pts = p.pts;
	pic->poc = p.poc;
	pic->pictures_ready = a->npic;
	a->pictures++;
	return 1;
}

static int
ap_release(void *ctx, uint32_t index)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	struct ap_ent *e;

	if (!a || index >= a->nent) {
		return -EINVAL;
	}
	e = &a->ent[index];
	if (e->state != ENT_DISPLAY && e->state != ENT_HELD) {
		return -EINVAL;
	}
	if (e->vendor_freed) {
		e->state = ENT_FREE;
		e->vendor_freed = 0;
	} else {
		e->state = ENT_HELD;    /* the vendor still holds the reference; harvest frees it */
	}
	(void)harvest(a);
	return 0;
}

static int
ap_free_buffers(void *ctx)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	int r;

	if (!a) {
		return -EINVAL;
	}
	if (a->npic != 0u) {
		return -EBUSY;
	}
	if (a->dec && a->changed_seen) {
		/* Resolution change: clear the vendor's state before the client registers a new pool. */
		r = n2ap_vdec_flush(a->dec);
		if (r != 0) {
			return r;
		}
		(void)harvest(a);
		a->npic = 0;
	}
	for (unsigned i = 0; i < AP_MAX_ENT; i++) {
		struct ap_ent *e = &a->ent[i];

		if (e->handle != 0u) {
			(void)n2vc_dma_guest_forget(a->cfg.hw, e->handle);
		}
		if (e->c_handle != 0u && e->c_handle != e->handle) {
			(void)n2vc_dma_guest_forget(a->cfg.hw, e->c_handle);
		}
	}
	memset(a->ent, 0, sizeof(a->ent));
	for (unsigned i = 0; i < AP_MAX_ENT; i++) {
		a->ent[i].share = -1;
		a->ent[i].c_share = -1;
	}
	a->nent = 0;
	a->nin = 0;
	a->geometry_valid = 0;
	a->changed_seen = 0;
	return 0;
}

static int
ap_flush(void *ctx, uint32_t *pictures_ready)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;
	int r;

	if (!a || !a->dec) {
		return -EINVAL;
	}
	r = n2ap_vdec_flush(a->dec);
	if (r != 0) {
		return r;
	}
	a->flushes++;
	r = harvest(a);
	if (r != 0) {
		return r;
	}
	if (pictures_ready) {
		*pictures_ready = a->npic;
	}
	return 0;
}

static void
ap_close(void *ctx)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;

	if (!a) {
		return;
	}
	if (a->dec) {
		(void)n2ap_vdec_close(a->dec);
		a->dec = NULL;
	}
	release_held(a);
	if (a->wc) {
		n2vcu_set_write_combining(a->cfg.vcu, 0);   /* the encoder path never runs with combining */
		a->wc = 0;
	}
	drop_entries(a);
	/* opus-decint: the session's vendor instance is gone (DEINIT): give the published shares back, or a daemon that
	 * serves several sessions (the phone test decodes 2-3 streams in one boot) runs V34's 64-slot table out:
	 * 34 vendor slots + (dpb+2) per session -> ADD_BUFFER -ENOSPC at the third session. */
	if (n2shim_ion_unregister_share) {
		for (unsigned i = 0; i < a->nshares; i++) {
			(void)n2shim_ion_unregister_share(a->shares[i].id);
		}
		a->nshares = 0;
	}
	a->codec = 0;
}

static int
ap_guest_stopped(void *ctx, uint32_t *code)
{
	struct n2vcd_dec_ap_n2ap *a = (struct n2vcd_dec_ap_n2ap *)ctx;

	if (code) {
		*code = 0;
	}
	if (!a || !a->cfg.guest_stopped) {
		return 0;       /* the daemon supplies its own check when it has one */
	}
	return a->cfg.guest_stopped(a->cfg.guest_ctx, code) ? 1 : 0;
}

static const struct n2vcd_dec_ap_ops g_ops = {
	.open = ap_open,
	.header = ap_header,
	.add_buffer = ap_add_buffer,
	.input = ap_input,
	.picture = ap_picture,
	.release = ap_release,
	.free_buffers = ap_free_buffers,
	.flush = ap_flush,
	.close = ap_close,
	.guest_stopped = ap_guest_stopped,
};

const struct n2vcd_dec_ap_ops *
n2vcd_dec_ap_n2ap_ops(void)
{
	return &g_ops;
}

struct n2vcd_dec_ap_n2ap *
n2vcd_dec_ap_n2ap_create(const struct n2vcd_dec_ap_n2ap_cfg *cfg)
{
	struct n2vcd_dec_ap_n2ap *a;

	if (!cfg || !cfg->vcu || !cfg->hw) {
		return NULL;
	}
	a = (struct n2vcd_dec_ap_n2ap *)calloc(1, sizeof(*a));
	if (!a) {
		return NULL;
	}
	a->cfg = *cfg;
	for (unsigned i = 0; i < AP_MAX_ENT; i++) {
		a->ent[i].share = -1;
		a->ent[i].c_share = -1;
	}
	return a;
}

void
n2vcd_dec_ap_n2ap_destroy(struct n2vcd_dec_ap_n2ap *a)
{
	if (!a) {
		return;
	}
	ap_close(a);
	free(a);
}

uint32_t
n2vcd_dec_ap_n2ap_inputs(const struct n2vcd_dec_ap_n2ap *a)
{
	return a ? a->inputs : 0u;
}

uint32_t
n2vcd_dec_ap_n2ap_pictures(const struct n2vcd_dec_ap_n2ap *a)
{
	return a ? a->pictures : 0u;
}

uint32_t
n2vcd_dec_ap_n2ap_flushes(const struct n2vcd_dec_ap_n2ap *a)
{
	return a ? a->flushes : 0u;
}
