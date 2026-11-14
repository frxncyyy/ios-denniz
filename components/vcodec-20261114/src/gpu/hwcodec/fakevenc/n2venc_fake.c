/* SPDX-License-Identifier: MIT */
/*
 * n2venc_fake.c -- behavioural VENC model on top of another n2vc_hw back end.  See n2venc_fake.h and
 * facts/VENC_PROTOCOL.md (register meanings, which of them are verified from libvcodecdrv.so and which are
 * inferred).  Owner: opus-venc.  Host only; nothing here touches real hardware.
 */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2venc_fake.h"

/* V41 (ap/n2vc_dma.c).  n2vc_dma_lookup_pa is declared by the frozen n2vc_hw.h; lookup_handle lives in
 * ap/n2ap_venc.h, re-declared here so this file builds without the AP headers.  Both are weak: without
 * the DMA registry the model cannot reach buffers and counts dma_errors instead. */
extern int n2vc_dma_lookup_pa(uint32_t pa, uint32_t *handle, uint32_t *guest_addr, uint32_t *bytes)
	__attribute__((weak));
extern int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes)
	__attribute__((weak));

/* ---- register map (facts/VENC_PROTOCOL.md section 3) ---- */
#define VR_WINDOW_BYTES   0x3000u   /* what vpud maps per core (mmap 0x17020000 / 0x17820000, len 0x3000) */
#define VR_MB_SIZE        0x024u    /* (mb_rows << 16) | mb_cols                         [I] from 320x240 */
#define VR_PITCH          0x028u    /* (luma pitch/16) << 20 | (chroma pitch/16) << 11    [I] 320: 0x0140a000, 640: 0x02814000 */
#define VR_TRIGGER        0x058u    /* 1 SPS, 2 PPS, 4 FRM                               [V] write 4 */
#define VR_IRQ_STATUS     0x05cu    /* kernel MTK_VENC_IRQ_STATUS_OFFSET                 [V] */
#define VR_IRQ_ACK        0x060u    /* kernel MTK_VENC_IRQ_ACK_OFFSET                    [V] */
#define VR_BS_ADDR        0x064u    /* bitstream buffer pa >> 4                          [V] value */
#define VR_BS_SIZE        0x068u    /* bitstream buffer size >> 7                        [I] 1 MiB -> 0x2000 */
#define VR_SRC_LUMA       0x06cu    /* source luma pa >> 4                               [V] value */
#define VR_SRC_CHROMA     0x070u    /* source chroma pa >> 4                             [V] value */
#define VR_SRC_CHROMA2    0x094u    /* third plane pa >> 4 (0 for NV12)                  [V] value */
#define VR_BS_BYTES       0x098u    /* bitstream byte count after FRM done               [V] read as size */
#define VR_RESET          0x0a8u    /* soft reset: 0 then 1                              [V] */
#define IRQ_SPS 0x1u
#define IRQ_PPS 0x2u
#define IRQ_FRM 0x4u
#define IRQ_DRAM 0x8u

struct h264_hdr {
	int valid;
	unsigned profile_idc, chroma_format_idc, log2_max_frame_num, poc_type, log2_max_poc_lsb;
	unsigned delta_pic_order_always_zero, frame_mbs_only, mb_w, mb_h;
	unsigned pps_id, entropy, bottom_field_pic_order, num_slice_groups, deblock_ctrl, redundant_pic_cnt;
};

struct n2venc_fake {
	struct n2vc_hw inner;
	struct n2vc_hw *outer;
	pthread_mutex_t lock;
	uint32_t reg[2][VR_WINDOW_BYTES / 4u];
	unsigned pending[2];
	struct h264_hdr hdr;
	struct n2venc_fake_stats st;
	FILE *log;
};

static pthread_mutex_t g_reg_lock = PTHREAD_MUTEX_INITIALIZER;
#define N2VENC_MAX_INSTANCES 4
static struct n2venc_fake *g_inst[N2VENC_MAX_INSTANCES];

static void flog(struct n2venc_fake *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void flog(struct n2venc_fake *f, const char *fmt, ...)
{
	va_list ap;

	if (!f->log)
		return;
	va_start(ap, fmt);
	vfprintf(f->log, fmt, ap);
	va_end(ap);
	fputc('\n', f->log);
	fflush(f->log);
}

/* ------------------------------------------------------------------ DMA through the inner back end */

static int dma_locate(uint32_t pa, uint32_t *handle, uint32_t *offset, uint32_t *avail)
{
	uint32_t h = 0, rem = 0, total = 0, base = 0;

	if (!n2vc_dma_lookup_pa || !n2vc_dma_lookup_handle)
		return -ENOSYS;
	if (n2vc_dma_lookup_pa(pa, &h, NULL, &rem) != 0)
		return -ENOENT;
	if (n2vc_dma_lookup_handle(h, &base, NULL, &total) != 0 || total < rem)
		return -ENOENT;
	*handle = h;
	*offset = total - rem;
	*avail = rem;
	(void)base;
	return 0;
}

static int dma_xfer(struct n2venc_fake *f, uint32_t pa, void *buf, uint32_t bytes, int write)
{
	uint32_t h, off, avail;
	int rc = dma_locate(pa, &h, &off, &avail);

	if (rc)
		return rc;
	if (bytes > avail)
		return -ERANGE;
	if (!f->inner.dma_rw)
		return -ENOSYS;
	return f->inner.dma_rw(f->inner.ctx, h, off, buf, bytes, write);
}

/* ------------------------------------------------------------------ bit reader / writer */

struct br { const uint8_t *p; uint32_t n, pos; int err; };

static unsigned br_u(struct br *b, unsigned bits)
{
	unsigned v = 0;

	while (bits--) {
		if (b->pos >= b->n * 8u) {
			b->err = 1;
			return 0;
		}
		v = (v << 1) | ((b->p[b->pos >> 3] >> (7u - (b->pos & 7u))) & 1u);
		b->pos++;
	}
	return v;
}

static unsigned br_ue(struct br *b)
{
	unsigned zeros = 0;

	while (!b->err && br_u(b, 1) == 0) {
		if (++zeros > 31) {
			b->err = 1;
			return 0;
		}
	}
	return zeros ? ((1u << zeros) - 1u + br_u(b, zeros)) : 0u;
}

static int br_se(struct br *b)
{
	unsigned k = br_ue(b);

	return (k & 1u) ? (int)((k + 1u) / 2u) : -(int)(k / 2u);
}

struct bw { uint8_t *p; uint32_t cap, pos; int err; };

static void bw_u(struct bw *w, unsigned bits, uint32_t v)
{
	while (bits--) {
		uint32_t byte = w->pos >> 3;

		if (byte >= w->cap) {
			w->err = 1;
			return;
		}
		if ((w->pos & 7u) == 0)
			w->p[byte] = 0;
		if ((v >> bits) & 1u)
			w->p[byte] |= (uint8_t)(0x80u >> (w->pos & 7u));
		w->pos++;
	}
}

static void bw_ue(struct bw *w, uint32_t v)
{
	uint32_t x = v + 1u;
	unsigned len = 0;

	while ((x >> len) > 1u)
		len++;
	bw_u(w, len, 0);
	bw_u(w, len + 1u, x);
}

static void bw_se(struct bw *w, int v)
{
	bw_ue(w, v > 0 ? (uint32_t)(2 * v - 1) : (uint32_t)(-2 * v));
}

/* ------------------------------------------------------------------ SPS / PPS (ITU-T H.264 7.3.2.1, 7.3.2.2) */

static uint32_t unescape(const uint8_t *in, uint32_t n, uint8_t *out)
{
	uint32_t i, o = 0, zeros = 0;

	for (i = 0; i < n; i++) {
		if (zeros >= 2 && in[i] == 3) {
			zeros = 0;
			continue;
		}
		zeros = in[i] == 0 ? zeros + 1 : 0;
		out[o++] = in[i];
	}
	return o;
}

static void skip_scaling_list(struct br *b, unsigned size)
{
	int last = 8, next = 8;
	unsigned j;

	for (j = 0; j < size && !b->err; j++) {
		if (next != 0)
			next = (last + br_se(b) + 256) % 256;
		last = next == 0 ? last : next;
	}
}

static int parse_sps(struct h264_hdr *h, const uint8_t *rbsp, uint32_t n)
{
	struct br b = {rbsp, n, 0, 0};
	unsigned i, cnt;

	h->profile_idc = br_u(&b, 8);
	(void)br_u(&b, 16);                         /* constraint flags, level_idc */
	(void)br_ue(&b);                            /* seq_parameter_set_id */
	h->chroma_format_idc = 1;
	if (h->profile_idc == 100 || h->profile_idc == 110 || h->profile_idc == 122 || h->profile_idc == 244 ||
	    h->profile_idc == 44 || h->profile_idc == 83 || h->profile_idc == 86 || h->profile_idc == 118 ||
	    h->profile_idc == 128 || h->profile_idc == 138 || h->profile_idc == 139 || h->profile_idc == 134 ||
	    h->profile_idc == 135) {
		h->chroma_format_idc = br_ue(&b);
		if (h->chroma_format_idc == 3)
			(void)br_u(&b, 1);
		(void)br_ue(&b);
		(void)br_ue(&b);
		(void)br_u(&b, 1);
		if (br_u(&b, 1))
			for (i = 0; i < (h->chroma_format_idc == 3 ? 12u : 8u); i++)
				if (br_u(&b, 1))
					skip_scaling_list(&b, i < 6 ? 16u : 64u);
	}
	h->log2_max_frame_num = br_ue(&b) + 4u;
	h->poc_type = br_ue(&b);
	if (h->poc_type == 0) {
		h->log2_max_poc_lsb = br_ue(&b) + 4u;
	} else if (h->poc_type == 1) {
		h->delta_pic_order_always_zero = br_u(&b, 1);
		(void)br_se(&b);
		(void)br_se(&b);
		cnt = br_ue(&b);
		for (i = 0; i < cnt && !b.err; i++)
			(void)br_se(&b);
	}
	(void)br_ue(&b);                            /* max_num_ref_frames */
	(void)br_u(&b, 1);
	h->mb_w = br_ue(&b) + 1u;
	h->mb_h = br_ue(&b) + 1u;
	h->frame_mbs_only = br_u(&b, 1);
	if (!h->frame_mbs_only)
		h->mb_h *= 2u;
	return b.err ? -EINVAL : 0;
}

static int parse_pps(struct h264_hdr *h, const uint8_t *rbsp, uint32_t n)
{
	struct br b = {rbsp, n, 0, 0};

	h->pps_id = br_ue(&b);
	(void)br_ue(&b);
	h->entropy = br_u(&b, 1);
	h->bottom_field_pic_order = br_u(&b, 1);
	h->num_slice_groups = br_ue(&b) + 1u;
	if (h->num_slice_groups != 1)
		return -EINVAL;                         /* FMO is not produced by this encoder */
	(void)br_ue(&b);
	(void)br_ue(&b);
	(void)br_u(&b, 1);
	(void)br_u(&b, 2);
	(void)br_se(&b);
	(void)br_se(&b);
	(void)br_se(&b);
	h->deblock_ctrl = br_u(&b, 1);
	(void)br_u(&b, 1);
	h->redundant_pic_cnt = br_u(&b, 1);
	return b.err ? -EINVAL : 0;
}

/* Scan an Annex-B buffer for an SPS (7) and a PPS (8).  Returns 0 when both parsed. */
static int learn_headers(struct h264_hdr *out, const uint8_t *p, uint32_t n)
{
	struct h264_hdr h;
	int have_sps = 0, have_pps = 0;
	uint32_t i = 0;
	uint8_t *tmp = malloc(n ? n : 1u);

	if (!tmp)
		return -ENOMEM;
	memset(&h, 0, sizeof h);
	while (i + 3u < n) {
		uint32_t s, e;

		if (!(p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1)) {
			i++;
			continue;
		}
		s = i + 3u;
		for (e = s; e + 2u < n && !(p[e] == 0 && p[e + 1] == 0 && (p[e + 2] == 1 || p[e + 2] == 0)); e++)
			;
		if (e + 2u >= n)
			e = n;
		if (s < e) {
			unsigned type = p[s] & 0x1fu;
			uint32_t m = unescape(p + s + 1u, e - s - 1u, tmp);

			if (type == 7 && parse_sps(&h, tmp, m) == 0)
				have_sps = 1;
			else if (type == 8 && have_sps && parse_pps(&h, tmp, m) == 0)
				have_pps = 1;
		}
		i = e;
		if (have_sps && have_pps)
			break;
	}
	free(tmp);
	if (!(have_sps && have_pps))
		return -ENOENT;
	h.valid = 1;
	*out = h;
	return 0;
}

int n2venc_fake_set_headers(struct n2venc_fake *f, const uint8_t *annexb, uint32_t bytes)
{
	struct h264_hdr h;

	if (!f || !annexb || !bytes || learn_headers(&h, annexb, bytes) != 0)
		return -EINVAL;
	pthread_mutex_lock(&f->lock);
	f->hdr = h;
	f->st.headers_learned++;
	pthread_mutex_unlock(&f->lock);
	return 0;
}

/* ------------------------------------------------------------------ the synthetic frame */

/* Build one IDR access unit (Annex B, 4-byte start code, nal_ref_idc 3, type 5) of I_PCM macroblocks.
 * Returns the byte count, or 0 when `cap` is too small. */
static uint32_t build_idr(const struct h264_hdr *h, unsigned mb_w, unsigned mb_h, const uint8_t *y,
			  const uint8_t *uv, uint32_t pitch, uint8_t *out, uint32_t cap)
{
	uint32_t rbsp_cap = 64u + (uint32_t)mb_w * mb_h * 390u;
	uint8_t *rbsp = malloc(rbsp_cap);
	struct bw w;
	uint32_t i, o, zeros, n;
	unsigned mx, my, r, c;

	if (!rbsp)
		return 0;
	w.p = rbsp;
	w.cap = rbsp_cap;
	w.pos = 0;
	w.err = 0;
	/* slice_header() for an IDR I slice (7.3.3) */
	bw_ue(&w, 0);                               /* first_mb_in_slice */
	bw_ue(&w, 7);                               /* slice_type I (all slices of the picture) */
	bw_ue(&w, h->pps_id);
	bw_u(&w, h->log2_max_frame_num, 0);         /* frame_num */
	if (!h->frame_mbs_only)
		bw_u(&w, 1, 0);                         /* field_pic_flag */
	bw_ue(&w, 0);                               /* idr_pic_id */
	if (h->poc_type == 0) {
		bw_u(&w, h->log2_max_poc_lsb, 0);       /* pic_order_cnt_lsb */
		if (h->bottom_field_pic_order)
			bw_se(&w, 0);
	} else if (h->poc_type == 1 && !h->delta_pic_order_always_zero) {
		bw_se(&w, 0);
		if (h->bottom_field_pic_order)
			bw_se(&w, 0);
	}
	if (h->redundant_pic_cnt)
		bw_ue(&w, 0);
	bw_u(&w, 1, 0);                             /* no_output_of_prior_pics_flag */
	bw_u(&w, 1, 0);                             /* long_term_reference_flag */
	bw_se(&w, 0);                               /* slice_qp_delta */
	if (h->deblock_ctrl)
		bw_ue(&w, 1);                           /* disable_deblocking_filter_idc: off (PCM needs none) */
	/* slice_data(): CAVLC, every macroblock I_PCM (mb_type 25 in an I slice, Table 7-11) */
	for (my = 0; my < mb_h; my++) {
		for (mx = 0; mx < mb_w; mx++) {
			bw_ue(&w, 25);
			while (w.pos & 7u)
				bw_u(&w, 1, 0);                 /* pcm_alignment_zero_bit */
			for (r = 0; r < 16; r++)
				for (c = 0; c < 16; c++)
					bw_u(&w, 8, y[(size_t)(my * 16u + r) * pitch + mx * 16u + c]);
			for (r = 0; r < 8; r++)            /* Cb: even bytes of the NV12 chroma plane */
				for (c = 0; c < 8; c++)
					bw_u(&w, 8, uv[(size_t)(my * 8u + r) * pitch + (mx * 8u + c) * 2u]);
			for (r = 0; r < 8; r++)            /* Cr: odd bytes */
				for (c = 0; c < 8; c++)
					bw_u(&w, 8, uv[(size_t)(my * 8u + r) * pitch + (mx * 8u + c) * 2u + 1u]);
		}
	}
	bw_u(&w, 1, 1);                             /* rbsp_stop_one_bit */
	while (w.pos & 7u)
		bw_u(&w, 1, 0);
	if (w.err) {
		free(rbsp);
		return 0;
	}
	n = w.pos >> 3;
	if (cap < 5u) {
		free(rbsp);
		return 0;
	}
	out[0] = 0;
	out[1] = 0;
	out[2] = 0;
	out[3] = 1;
	out[4] = 0x65;                              /* forbidden 0, nal_ref_idc 3, nal_unit_type 5 */
	o = 5;
	zeros = 0;
	for (i = 0; i < n; i++) {
		if (zeros >= 2 && rbsp[i] <= 3) {
			if (o >= cap)
				break;
			out[o++] = 3;                       /* emulation_prevention_three_byte */
			zeros = 0;
		}
		if (o >= cap)
			break;
		out[o++] = rbsp[i];
		zeros = rbsp[i] == 0 ? zeros + 1 : 0;
	}
	free(rbsp);
	return i == n ? o : 0;
}

static void raise_irq(struct n2venc_fake *f, unsigned core, uint32_t bits)
{
	f->reg[core][VR_IRQ_STATUS / 4u] |= bits;
	f->pending[core]++;
	f->st.irqs_raised++;
}

static void encode_frame(struct n2venc_fake *f, unsigned core)
{
	uint32_t *R = f->reg[core];
	uint32_t bs_pa = R[VR_BS_ADDR / 4u] << 4, y_pa = R[VR_SRC_LUMA / 4u] << 4, c_pa = R[VR_SRC_CHROMA / 4u] << 4;
	uint32_t mb_w = R[VR_MB_SIZE / 4u] & 0xffffu, mb_h = R[VR_MB_SIZE / 4u] >> 16;
	uint32_t pitch = ((R[VR_PITCH / 4u] >> 20) & 0xfffu) * 16u, bs_cap = R[VR_BS_SIZE / 4u] << 7;
	uint32_t h_, off, avail, ybytes, cbytes, n = 0;
	uint8_t *y = NULL, *uv = NULL, *out = NULL, *scan = NULL;
	struct h264_hdr h = f->hdr;

	f->st.frames++;
	/* Learn SPS/PPS from the start of the bitstream buffer if nobody gave them (the SEQ_HDR job of the
	 * session wrote them there in software and flushed them). */
	if (!h.valid && dma_locate(bs_pa, &h_, &off, &avail) == 0) {
		uint32_t sn = avail < 256u ? avail : 256u;

		scan = malloc(sn);
		if (scan && dma_xfer(f, bs_pa, scan, sn, 0) == 0 && learn_headers(&h, scan, sn) == 0) {
			f->hdr = h;
			f->st.headers_learned++;
		}
		free(scan);
	}
	if (!h.valid) {                             /* Baseline defaults (what this vendor writes for 320x240) */
		memset(&h, 0, sizeof h);
		h.valid = 1;
		h.profile_idc = 66;
		h.chroma_format_idc = 1;
		h.log2_max_frame_num = 4;
		h.poc_type = 2;
		h.frame_mbs_only = 1;
		h.mb_w = mb_w;
		h.mb_h = mb_h;
	}
	if (h.entropy || h.chroma_format_idc != 1)
		f->st.unsupported++;                    /* CABAC / non-4:2:0: still CAVLC 4:2:0 output */
	if (h.mb_w && h.mb_h && (h.mb_w != mb_w || h.mb_h != mb_h)) {
		f->st.unsupported++;                    /* register geometry disagrees with the SPS: follow the SPS */
		mb_w = h.mb_w;
		mb_h = h.mb_h;
	}
	if (!pitch)
		pitch = mb_w * 16u;
	f->st.last_bs_pa = bs_pa;
	f->st.last_luma_pa = y_pa;
	f->st.last_chroma_pa = c_pa;
	f->st.last_mb_w = mb_w;
	f->st.last_mb_h = mb_h;
	ybytes = pitch * mb_h * 16u;
	cbytes = pitch * mb_h * 8u;
	y = malloc(ybytes ? ybytes : 1u);
	uv = malloc(cbytes ? cbytes : 1u);
	if (!y || !uv || mb_w == 0 || mb_h == 0 || pitch < mb_w * 16u) {
		f->st.dma_errors++;
		goto done;
	}
	/* The source may be shorter than the padded MB rows (e.g. 1080 lines): read what exists, repeat the
	 * last line, as the hardware's padding does for the encoder input. */
	if (dma_xfer(f, y_pa, y, ybytes, 0) != 0) {
		uint32_t got = 0;

		if (dma_locate(y_pa, &h_, &off, &avail) == 0)
			got = (avail / pitch) * pitch;
		if (got > ybytes)
			got = ybytes;
		if (!got || dma_xfer(f, y_pa, y, got, 0) != 0) {
			f->st.dma_errors++;
			memset(y, 128, ybytes);
		} else {
			for (off = got; off < ybytes; off += pitch)
				memcpy(y + off, y + got - pitch, pitch);
		}
	}
	if (dma_xfer(f, c_pa, uv, cbytes, 0) != 0) {
		uint32_t got = 0;

		if (dma_locate(c_pa, &h_, &off, &avail) == 0)
			got = (avail / pitch) * pitch;
		if (got > cbytes)
			got = cbytes;
		if (!got || dma_xfer(f, c_pa, uv, got, 0) != 0) {
			f->st.dma_errors++;
			memset(uv, 128, cbytes);
		} else {
			for (off = got; off < cbytes; off += pitch)
				memcpy(uv + off, uv + got - pitch, pitch);
		}
	}
	if (dma_locate(bs_pa, &h_, &off, &avail) != 0) {
		f->st.dma_errors++;
		goto done;
	}
	if (!bs_cap || bs_cap > avail)
		bs_cap = avail;
	out = malloc(bs_cap ? bs_cap : 1u);
	if (!out)
		goto done;
	n = build_idr(&h, mb_w, mb_h, y, uv, pitch, out, bs_cap);
	if (n == 0) {
		/* Bitstream buffer full: the hardware stops with the DRAM interrupt instead of FRM. */
		f->st.overflow++;
		R[VR_BS_BYTES / 4u] = bs_cap;
		raise_irq(f, core, IRQ_DRAM);
		flog(f, "FRM core %u overflow cap %u", core, bs_cap);
		goto out_free;
	}
	if (dma_xfer(f, bs_pa, out, n, 1) != 0)
		f->st.dma_errors++;
done:
	R[VR_BS_BYTES / 4u] = n;
	f->st.last_bytes = n;
	raise_irq(f, core, IRQ_FRM);
	flog(f, "FRM core %u %ux%u MBs src y=0x%08x c=0x%08x pitch %u -> bs 0x%08x %u bytes", core, mb_w, mb_h,
	     y_pa, c_pa, pitch, bs_pa, n);
out_free:
	free(y);
	free(uv);
	free(out);
}

/* Model side effects of a register write (value already merged). */
static void model_write(struct n2venc_fake *f, unsigned core, uint32_t off, uint32_t value)
{
	uint32_t *R = f->reg[core];

	switch (off) {
	case VR_IRQ_STATUS:
		return;                                 /* read only */
	case VR_IRQ_ACK:
		R[VR_IRQ_STATUS / 4u] &= ~value;
		R[off / 4u] = value;
		f->st.acks++;
		return;
	case VR_RESET:
		R[off / 4u] = value;
		if (value == 0) {
			R[VR_IRQ_STATUS / 4u] = 0;
			R[VR_BS_BYTES / 4u] = 0;
			f->pending[core] = 0;
			f->st.resets++;
		}
		return;
	case VR_TRIGGER:
		R[off / 4u] = value;
		f->st.triggers++;
		if (value & IRQ_FRM) {
			encode_frame(f, core);
		} else if (value & (IRQ_SPS | IRQ_PPS)) {
			/* Hardware SPS/PPS mode (the HEVC path uses it; H.264 writes them in software). */
			f->st.unsupported++;
			R[VR_BS_BYTES / 4u] = 0;
			raise_irq(f, core, value & (IRQ_SPS | IRQ_PPS));
		}
		return;
	default:
		R[off / 4u] = value;
		return;
	}
}

static int is_venc_window(unsigned win, unsigned *core)
{
	if (win == RWVC_WIN_VENC) {
		*core = 0;
		return 1;
	}
	if (win == RWVC_WIN_VENC_C1) {
		*core = 1;
		return 1;
	}
	return 0;
}

static int fake_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct n2venc_fake *f = ctx;
	unsigned i, d, core;
	int rc = 0;

	for (i = 0; i < count; i++) {
		struct rwvc_reg_op o = ops[i];

		if (!is_venc_window(o.window, &core) || o.op == RWVC_OP_DELAY_US || o.offset >= VR_WINDOW_BYTES ||
		    (o.offset & 3u)) {
			rc = f->inner.regs(f->inner.ctx, &ops[i], 1, &d);
			if (rc)
				break;
			continue;
		}
		if (o.op == RWVC_OP_POLL) {
			/* Power check and record through the inner back end, condition on the model. */
			struct rwvc_reg_op rd = {RWVC_OP_READ, o.window, 0, o.offset, 0, 0};

			rc = f->inner.regs(f->inner.ctx, &rd, 1, &d);
			if (rc)
				break;
			pthread_mutex_lock(&f->lock);
			f->st.polls++;
			if ((f->reg[core][o.offset / 4u] & o.mask) != (o.value & o.mask)) {
				f->st.poll_fail++;
				rc = -ETIMEDOUT;
			}
			pthread_mutex_unlock(&f->lock);
			if (rc)
				break;
			continue;
		}
		rc = f->inner.regs(f->inner.ctx, &ops[i], 1, &d);
		if (rc)
			break;
		pthread_mutex_lock(&f->lock);
		if (o.op == RWVC_OP_READ) {
			ops[i].value = f->reg[core][o.offset / 4u];
		} else if (o.op == RWVC_OP_WRITE || o.op == RWVC_OP_WRITE_MASK) {
			uint32_t v = o.op == RWVC_OP_WRITE ? o.value
				: ((f->reg[core][o.offset / 4u] & ~o.mask) | (o.value & o.mask));

			model_write(f, core, o.offset, v);
		}
		pthread_mutex_unlock(&f->lock);
	}
	if (done)
		*done = i;
	return rc;
}

static int fake_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct n2venc_fake *f = ctx;
	unsigned core = irq == RWVC_IRQ_VENC ? 0u : 1u;

	if (irq == RWVC_IRQ_VENC || irq == RWVC_IRQ_VENC_C1) {
		pthread_mutex_lock(&f->lock);
		if (f->pending[core]) {
			f->pending[core]--;
			f->st.irqs_taken++;
			if (status)
				*status = f->reg[core][VR_IRQ_STATUS / 4u];
			pthread_mutex_unlock(&f->lock);
			return 0;
		}
		pthread_mutex_unlock(&f->lock);
	}
	return f->inner.wait_irq(f->inner.ctx, irq, timeout_ms, status);
}

static int fake_irq_clear(void *ctx, unsigned irq)
{
	struct n2venc_fake *f = ctx;

	if (irq == RWVC_IRQ_VENC || irq == RWVC_IRQ_VENC_C1) {
		pthread_mutex_lock(&f->lock);
		f->pending[irq == RWVC_IRQ_VENC ? 0 : 1] = 0;
		pthread_mutex_unlock(&f->lock);
	}
	return f->inner.irq_clear ? f->inner.irq_clear(f->inner.ctx, irq) : 0;
}

static int fake_power(void *ctx, unsigned block, int on)
{
	struct n2venc_fake *f = ctx;

	return f->inner.power(f->inner.ctx, block, on);
}

static int fake_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct n2venc_fake *f = ctx;

	return f->inner.dma_alloc(f->inner.ctx, bytes, flags, handle, pa);
}

static int fake_dma_free(void *ctx, uint32_t handle)
{
	struct n2venc_fake *f = ctx;

	return f->inner.dma_free(f->inner.ctx, handle);
}

static int fake_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct n2venc_fake *f = ctx;

	return f->inner.dma_rw(f->inner.ctx, handle, offset, host, bytes, write);
}

static void fake_close(void *ctx)
{
	struct n2venc_fake *f = ctx;
	unsigned i;

	pthread_mutex_lock(&g_reg_lock);
	for (i = 0; i < N2VENC_MAX_INSTANCES; i++)
		if (g_inst[i] == f)
			g_inst[i] = NULL;
	pthread_mutex_unlock(&g_reg_lock);
	if (f->inner.close)
		f->inner.close(f->inner.ctx);
	if (f->log)
		fclose(f->log);
	pthread_mutex_destroy(&f->lock);
	free(f);
}

int n2venc_fake_wrap(const struct n2vc_hw *inner, struct n2vc_hw *outer, struct n2venc_fake **fake,
		     const char *log_path)
{
	struct n2venc_fake *f;
	unsigned i;

	if (!inner || !outer || !inner->regs || !inner->power || !inner->wait_irq || !inner->dma_alloc ||
	    !inner->dma_free || !inner->dma_rw)
		return -EINVAL;
	f = calloc(1, sizeof *f);
	if (!f)
		return -ENOMEM;
	f->inner = *inner;
	pthread_mutex_init(&f->lock, NULL);
	if (log_path)
		f->log = fopen(log_path, "w");
	memset(outer, 0, sizeof *outer);
	outer->ctx = f;
	outer->power = fake_power;
	outer->regs = fake_regs;
	outer->wait_irq = fake_wait_irq;
	outer->irq_clear = fake_irq_clear;
	outer->dma_alloc = fake_dma_alloc;
	outer->dma_free = fake_dma_free;
	outer->dma_rw = fake_dma_rw;
	outer->close = fake_close;
	f->outer = outer;
	pthread_mutex_lock(&g_reg_lock);
	for (i = 0; i < N2VENC_MAX_INSTANCES; i++)
		if (!g_inst[i]) {
			g_inst[i] = f;
			break;
		}
	pthread_mutex_unlock(&g_reg_lock);
	if (fake)
		*fake = f;
	return 0;
}

struct n2venc_fake *n2venc_fake_of(const struct n2vc_hw *hw)
{
	struct n2venc_fake *r = NULL;
	unsigned i;

	if (!hw || hw->regs != fake_regs)
		return NULL;
	pthread_mutex_lock(&g_reg_lock);
	for (i = 0; i < N2VENC_MAX_INSTANCES; i++)
		if (g_inst[i] && g_inst[i] == hw->ctx)
			r = g_inst[i];
	pthread_mutex_unlock(&g_reg_lock);
	return r;
}

void n2venc_fake_get_stats(const struct n2venc_fake *f, struct n2venc_fake_stats *out)
{
	if (!f || !out)
		return;
	pthread_mutex_lock((pthread_mutex_t *)&f->lock);
	*out = f->st;
	pthread_mutex_unlock((pthread_mutex_t *)&f->lock);
}
