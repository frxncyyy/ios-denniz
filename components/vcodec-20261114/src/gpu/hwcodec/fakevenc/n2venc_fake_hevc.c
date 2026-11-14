/* SPDX-License-Identifier: MIT */
/*
 * n2venc_fake_hevc.c -- HEVC frames for the fake VENC.  See n2venc_fake_hevc.h and
 * facts/HEVC_ENC_PROTOCOL.md.  Owner: opus-hevc.  Host only.
 *
 * Written from the H.265 specification (ITU-T H.265 v1: 7.3 syntax, 8.4.4.2 intra sample prediction,
 * 8.6 scaling/transform, 9.3 CABAC) as I know it; validated only against ffmpeg as a black-box decoder
 * (fakevenc/run_tests_hevc.sh: the decoded picture must equal this encoder's reconstruction).
 */
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "n2venc_fake_hevc.h"

#define VR_MODE       0x000u   /* bit 2 = HEVC (SetHWModeSel base constants)   [V] constants, meaning [I] */
#define VR_PIC_SIZE   0x114cu  /* (height << 16) | width, HEVC only            [V] 3 sizes */
#define VR_PITCH      0x028u   /* (luma pitch/16) << 20 | ...   [I] same layout as H.264 (320: 0x0140a000) */
#define VR_BS_ADDR    0x064u
#define VR_BS_SIZE    0x068u
#define VR_SRC_LUMA   0x06cu
#define VR_SRC_CHROMA 0x070u
#define N2VENC_HEVC_QP 22

/* ------------------------------------------------------------------ parameter sets */
struct hsps {
	int valid;
	unsigned sps_id, chroma, sep_planes, w, h, bd_y, bd_c;
	unsigned min_cb, ctb, min_tb, max_tb, depth_intra;   /* log2 sizes */
	unsigned scaling, amp, sao, pcm, pcm_min, pcm_max;
};
struct hpps {
	int valid;
	unsigned pps_id, sps_id, dep_slices, output_flag, extra_bits, sign_hiding;
	int init_qp, cb_off, cr_off;
	unsigned tskip, cu_qp_delta, diff_qp_depth, slice_chroma_qp_offsets, tq_bypass, tiles, wpp;
	unsigned lf_across, dbk_ctrl, dbk_override, dbk_disable, scaling, sh_ext;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct hsps g_sps;
static struct hpps g_pps;
static struct n2venc_hevc_stats g_st;

/* ------------------------------------------------------------------ bit reader */
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
	unsigned z = 0;

	while (!b->err && br_u(b, 1) == 0) {
		if (++z > 31) {
			b->err = 1;
			return 0;
		}
	}
	return z ? ((1u << z) - 1u + br_u(b, z)) : 0u;
}

static int br_se(struct br *b)
{
	unsigned k = br_ue(b);

	return (k & 1u) ? (int)((k + 1u) / 2u) : -(int)(k / 2u);
}

static uint32_t unescape(const uint8_t *in, uint32_t n, uint8_t *out)
{
	uint32_t o = 0, z = 0;

	for (uint32_t i = 0; i < n; i++) {
		if (z >= 2 && in[i] == 3) {
			z = 0;
			continue;
		}
		out[o++] = in[i];
		z = in[i] == 0 ? z + 1 : 0;
	}
	return o;
}

static void parse_ptl(struct br *b, unsigned max_sub)
{
	unsigned sub_prof[8] = {0}, sub_lvl[8] = {0};

	br_u(b, 8);                     /* space, tier, profile_idc */
	br_u(b, 32);                    /* compat flags */
	br_u(b, 4);
	br_u(b, 32);                    /* 43 reserved + 1 */
	br_u(b, 12);
	br_u(b, 8);                     /* level_idc */
	for (unsigned i = 0; i < max_sub; i++) {
		sub_prof[i] = br_u(b, 1);
		sub_lvl[i] = br_u(b, 1);
	}
	if (max_sub > 0)
		for (unsigned i = max_sub; i < 8; i++)
			br_u(b, 2);
	for (unsigned i = 0; i < max_sub; i++) {
		if (sub_prof[i]) {
			br_u(b, 32);
			br_u(b, 32);
			br_u(b, 24);
		}
		if (sub_lvl[i])
			br_u(b, 8);
	}
}

static int parse_sps(struct hsps *s, const uint8_t *rbsp, uint32_t n)
{
	struct br b = { rbsp, n, 0, 0 };
	unsigned max_sub, order;

	memset(s, 0, sizeof *s);
	br_u(&b, 4);
	max_sub = br_u(&b, 3);
	br_u(&b, 1);
	parse_ptl(&b, max_sub);
	s->sps_id = br_ue(&b);
	s->chroma = br_ue(&b);
	if (s->chroma == 3)
		s->sep_planes = br_u(&b, 1);
	s->w = br_ue(&b);
	s->h = br_ue(&b);
	if (br_u(&b, 1)) {              /* conformance window: cropping only, not needed here */
		br_ue(&b);
		br_ue(&b);
		br_ue(&b);
		br_ue(&b);
	}
	s->bd_y = 8 + br_ue(&b);
	s->bd_c = 8 + br_ue(&b);
	br_ue(&b);                      /* log2_max_pic_order_cnt_lsb_minus4 */
	order = br_u(&b, 1);
	for (unsigned i = order ? 0 : max_sub; i <= max_sub; i++) {
		br_ue(&b);
		br_ue(&b);
		br_ue(&b);
	}
	s->min_cb = 3 + br_ue(&b);
	s->ctb = s->min_cb + br_ue(&b);
	s->min_tb = 2 + br_ue(&b);
	s->max_tb = s->min_tb + br_ue(&b);
	br_ue(&b);                      /* max_transform_hierarchy_depth_inter */
	s->depth_intra = br_ue(&b);
	s->scaling = br_u(&b, 1);
	if (s->scaling)
		return -1;              /* scaling_list_data not parsed (unsupported anyway) */
	s->amp = br_u(&b, 1);
	s->sao = br_u(&b, 1);
	s->pcm = br_u(&b, 1);
	if (s->pcm) {
		br_u(&b, 4);
		br_u(&b, 4);
		s->pcm_min = 3 + br_ue(&b);
		s->pcm_max = s->pcm_min + br_ue(&b);
		br_u(&b, 1);
	}
	if (b.err || s->w == 0 || s->h == 0 || s->w > 8192 || s->h > 8192 || s->ctb > 6 || s->ctb < 4 ||
	    s->max_tb > 5 || s->min_tb > s->max_tb || s->min_cb > s->ctb || s->max_tb > s->ctb ||
	    (s->w & ((1u << s->min_cb) - 1u)) || (s->h & ((1u << s->min_cb) - 1u)))
		return -1;
	s->valid = 1;
	return 0;
}

static int parse_pps(struct hpps *p, const uint8_t *rbsp, uint32_t n)
{
	struct br b = { rbsp, n, 0, 0 };

	memset(p, 0, sizeof *p);
	p->pps_id = br_ue(&b);
	p->sps_id = br_ue(&b);
	p->dep_slices = br_u(&b, 1);
	p->output_flag = br_u(&b, 1);
	p->extra_bits = br_u(&b, 3);
	p->sign_hiding = br_u(&b, 1);
	br_u(&b, 1);                    /* cabac_init_present_flag (P/B only) */
	br_ue(&b);
	br_ue(&b);
	p->init_qp = 26 + br_se(&b);
	br_u(&b, 1);                    /* constrained_intra_pred_flag: every CU is intra anyway */
	p->tskip = br_u(&b, 1);
	p->cu_qp_delta = br_u(&b, 1);
	if (p->cu_qp_delta)
		p->diff_qp_depth = br_ue(&b);
	p->cb_off = br_se(&b);
	p->cr_off = br_se(&b);
	p->slice_chroma_qp_offsets = br_u(&b, 1);
	br_u(&b, 1);                    /* weighted_pred_flag */
	br_u(&b, 1);                    /* weighted_bipred_flag */
	p->tq_bypass = br_u(&b, 1);
	p->tiles = br_u(&b, 1);
	p->wpp = br_u(&b, 1);
	if (p->tiles)
		return -1;
	p->lf_across = br_u(&b, 1);
	p->dbk_ctrl = br_u(&b, 1);
	if (p->dbk_ctrl) {
		p->dbk_override = br_u(&b, 1);
		p->dbk_disable = br_u(&b, 1);
		if (!p->dbk_disable) {
			br_se(&b);
			br_se(&b);
		}
	}
	p->scaling = br_u(&b, 1);
	if (p->scaling)
		return -1;
	br_u(&b, 1);                    /* lists_modification_present_flag */
	br_ue(&b);                      /* log2_parallel_merge_level_minus2 */
	p->sh_ext = br_u(&b, 1);
	if (b.err || p->init_qp < 0 || p->init_qp > 51)
		return -1;
	p->valid = 1;
	return 0;
}

/* Scan Annex-B data for an HEVC SPS + PPS (VPS optional).  0 when both were found. */
static int learn(const uint8_t *d, uint32_t n, struct hsps *sps, struct hpps *pps)
{
	struct hsps s = { 0 };
	struct hpps p = { 0 };
	uint32_t i = 0;
	uint8_t *rb = malloc(n ? n : 1u);

	if (!rb)
		return -ENOMEM;
	while (i + 5 <= n) {
		uint32_t j, e, len;
		unsigned type;

		if (!(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)) {
			i++;
			continue;
		}
		j = i + 3;
		for (e = j; e + 3 <= n && !(d[e] == 0 && d[e + 1] == 0 && (d[e + 2] == 1 || d[e + 2] == 0)); e++)
			;
		if (e + 3 > n)
			e = n;
		if ((d[j] & 0x81u) != 0 || (d[j + 1] & 7u) == 0) {   /* forbidden bit, layer id, tid */
			i = j;
			continue;
		}
		type = (d[j] >> 1) & 63u;
		len = unescape(d + j + 2, e - j - 2, rb);
		if (type == 33 && !s.valid)
			(void)parse_sps(&s, rb, len);
		else if (type == 34 && !p.valid)
			(void)parse_pps(&p, rb, len);
		i = e;
	}
	free(rb);
	if (!s.valid || !p.valid || p.sps_id != s.sps_id)
		return -ENOENT;
	*sps = s;
	*pps = p;
	return 0;
}

/* ------------------------------------------------------------------ bit writer + CABAC encoder */
struct bw { uint8_t *p; uint32_t cap, pos; int err; };

static void bw_bit(struct bw *w, unsigned b)
{
	if ((w->pos >> 3) >= w->cap) {
		w->err = 1;
		return;
	}
	if ((w->pos & 7u) == 0)
		w->p[w->pos >> 3] = 0;
	if (b)
		w->p[w->pos >> 3] |= (uint8_t)(0x80u >> (w->pos & 7u));
	w->pos++;
}

static void bw_u(struct bw *w, unsigned bits, uint32_t v)
{
	while (bits--)
		bw_bit(w, (v >> bits) & 1u);
}

static void bw_ue(struct bw *w, uint32_t v)
{
	unsigned len = 0;

	while ((v + 1u) >> (len + 1u))
		len++;
	bw_u(w, len, 0);
	bw_u(w, len + 1u, v + 1u);
}

static void bw_se(struct bw *w, int v)
{
	bw_ue(w, v > 0 ? (uint32_t)(2 * v - 1) : (uint32_t)(-2 * v));
}

/* H.265 Table 9-48 (= H.264 Table 9-44) and the state transition table 9-49. */
static const uint8_t rtab_lps[64][4] = {
	{128,176,208,240},{128,167,197,227},{128,158,187,216},{123,150,178,205},{116,142,169,195},
	{111,135,160,185},{105,128,152,175},{100,122,144,166},{95,116,137,158},{90,110,130,150},
	{85,104,123,142},{81,99,117,135},{77,94,111,128},{73,89,105,122},{69,85,100,116},
	{66,80,95,110},{62,76,90,104},{59,72,86,99},{56,69,81,94},{53,65,77,89},{51,62,73,85},
	{48,59,69,80},{46,56,66,76},{43,53,63,72},{41,50,59,69},{39,48,56,65},{37,45,54,62},
	{35,43,51,59},{33,41,48,56},{32,39,46,53},{30,37,43,50},{29,35,41,48},{27,33,39,45},
	{26,31,37,43},{24,30,35,41},{23,28,33,39},{22,27,32,37},{21,26,30,35},{20,24,29,33},
	{19,23,27,31},{18,22,26,30},{17,21,25,28},{16,20,23,27},{15,19,22,25},{14,18,21,24},
	{14,17,20,23},{13,16,19,22},{12,15,18,21},{12,14,17,20},{11,14,16,19},{11,13,15,18},
	{10,12,15,17},{10,12,14,16},{9,11,13,15},{9,11,12,14},{8,10,12,14},{8,9,11,13},
	{7,9,11,12},{7,9,10,12},{7,8,10,11},{6,8,9,11},{6,7,9,10},{6,7,8,9},{2,2,2,2},
};
static const uint8_t trans_lps[64] = {
	0,0,1,2,2,4,4,5,6,7,8,9,9,11,11,12,13,13,15,15,16,16,18,18,19,19,21,21,22,22,23,24,
	24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,33,33,34,34,35,35,35,36,36,36,37,37,37,38,38,63,
};

struct ctx { uint8_t state, mps; };
struct cabac { struct bw *w; uint32_t low, range; int first; unsigned outstanding; };

static void ctx_init(struct ctx *c, unsigned init, int qp)
{
	int m = (int)(init >> 4) * 5 - 45, n = (int)((init & 15u) << 3) - 16;
	int pre = ((m * (qp < 0 ? 0 : qp > 51 ? 51 : qp)) >> 4) + n;

	pre = pre < 1 ? 1 : pre > 126 ? 126 : pre;
	c->mps = pre <= 63 ? 0 : 1;
	c->state = (uint8_t)(c->mps ? pre - 64 : 63 - pre);
}

static void put_bit(struct cabac *e, unsigned b)
{
	if (e->first)
		e->first = 0;
	else
		bw_bit(e->w, b);
	while (e->outstanding) {
		bw_bit(e->w, 1u - b);
		e->outstanding--;
	}
}

static void renorm(struct cabac *e)
{
	while (e->range < 256) {
		if (e->low < 256) {
			put_bit(e, 0);
		} else if (e->low >= 512) {
			e->low -= 512;
			put_bit(e, 1);
		} else {
			e->low -= 256;
			e->outstanding++;
		}
		e->range <<= 1;
		e->low <<= 1;
	}
}

static void enc_bin(struct cabac *e, struct ctx *c, unsigned bin)
{
	uint32_t lps = rtab_lps[c->state][(e->range >> 6) & 3u];

	e->range -= lps;
	if (bin != c->mps) {
		e->low += e->range;
		e->range = lps;
		if (c->state == 0)
			c->mps = (uint8_t)(1u - c->mps);
		c->state = trans_lps[c->state];
	} else if (c->state < 62) {
		c->state++;
	}
	renorm(e);
}

static void enc_bypass(struct cabac *e, unsigned bin)
{
	e->low <<= 1;
	if (bin)
		e->low += e->range;
	if (e->low >= 1024) {
		put_bit(e, 1);
		e->low -= 1024;
	} else if (e->low < 512) {
		put_bit(e, 0);
	} else {
		e->low -= 512;
		e->outstanding++;
	}
}

static void enc_bypass_bits(struct cabac *e, unsigned bits, uint32_t v)
{
	while (bits--)
		enc_bypass(e, (v >> bits) & 1u);
}

static void enc_term(struct cabac *e, unsigned bin)
{
	e->range -= 2;
	if (bin) {
		e->low += e->range;
		e->range = 2;                       /* EncodeFlush: the last bit written is rbsp_stop_one_bit */
		renorm(e);
		put_bit(e, (e->low >> 9) & 1u);
		bw_u(e->w, 2, ((e->low >> 7) & 3u) | 1u);
	} else {
		renorm(e);
	}
}

/* ------------------------------------------------------------------ contexts (initType 0, I slice) */
enum {
	C_SPLIT = 0,            /* 3 */
	C_TQB = 3,              /* 1 cu_transquant_bypass_flag */
	C_PART = 4,             /* 1 */
	C_PREV_INTRA = 5,       /* 1 */
	C_CHROMA_MODE = 6,      /* 1 */
	C_CBF_LUMA = 7,         /* 2 */
	C_CBF_CHROMA = 9,       /* 4 */
	C_DQP = 13,             /* 2 */
	C_TSKIP = 15,           /* 2: luma, chroma */
	C_LAST_X = 17,          /* 18 */
	C_LAST_Y = 35,          /* 18 */
	C_CSBF = 53,            /* 4 */
	C_SIG = 57,             /* 42 */
	C_GT1 = 99,             /* 24 */
	C_GT2 = 123,            /* 6 */
	C_SPLIT_TF = 129,       /* 3: split_transform_flag, ctxInc 5 - log2TrafoSize */
	C_COUNT = 132
};
static const uint8_t ctx_init_i[C_COUNT] = {
	139, 141, 157,                                          /* split_cu_flag */
	154,                                                    /* cu_transquant_bypass_flag */
	184,                                                    /* part_mode */
	184,                                                    /* prev_intra_luma_pred_flag */
	63,                                                     /* intra_chroma_pred_mode */
	111, 141,                                               /* cbf_luma */
	94, 138, 182, 154,                                      /* cbf_cb / cbf_cr */
	154, 154,                                               /* cu_qp_delta_abs */
	139, 139,                                               /* transform_skip_flag */
	110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111, 79, 108, 123, 63,
	110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111, 79, 108, 123, 63,
	91, 171, 134, 141,                                      /* coded_sub_block_flag */
	111, 111, 125, 110, 110, 94, 124, 108, 124, 107, 125, 141, 179, 153, 125, 107, 125, 141,
	179, 153, 125, 107, 125, 141, 179, 153, 125, 140, 139, 182, 182, 152, 136, 152, 136, 153,
	136, 139, 111, 136, 139, 111,                           /* sig_coeff_flag */
	140, 92, 137, 138, 140, 152, 138, 139, 153, 74, 149, 92, 139, 107, 122, 152,
	140, 179, 166, 182, 140, 227, 122, 197,                 /* coeff_abs_level_greater1_flag */
	138, 153, 136, 167, 152, 152,                           /* coeff_abs_level_greater2_flag */
	153, 138, 138,                                          /* split_transform_flag */
};

/* ------------------------------------------------------------------ transform */
/* HEVC integer DCT coefficient for angle index m (units of pi/64, 0..32); m = 0 only for row 0 (64). */
static const uint8_t dct_mag[33] = {
	64, 90, 90, 90, 89, 88, 87, 85, 83, 82, 80, 78, 75, 73, 70, 67, 64,
	61, 57, 54, 50, 46, 43, 38, 36, 31, 25, 22, 18, 13, 9, 4, 0,
};

static int dct_coef(unsigned log2n, unsigned k, unsigned n)
{
	unsigned a, sign = 0;

	if (k == 0)
		return 64;
	a = ((2u * n + 1u) * k * (32u >> log2n)) % 128u;   /* (2n+1)k*pi/(2N) in pi/64 units */
	if (a > 64)
		a = 128 - a;
	if (a > 32) {
		a = 64 - a;
		sign = 1;
	}
	return sign ? -(int)dct_mag[a] : (int)dct_mag[a];
}

static int clip3(int lo, int hi, int v)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static const int level_scale[6] = { 40, 45, 51, 57, 64, 72 };

/* 8.6.2-8.6.4 for 8-bit, flat scaling, no transform skip: levels -> residual (exact decoder arithmetic). */
static void inv_transform(const int *lev, int *res, unsigned log2n, int qp)
{
	unsigned n = 1u << log2n;
	int d[32 * 32], g[32 * 32];
	int bd = (int)log2n + 3;

	for (unsigned i = 0; i < n * n; i++) {
		long long v = (long long)lev[i] * 16 * level_scale[qp % 6] * (1ll << (qp / 6));

		d[i] = clip3(-32768, 32767, (int)((v + (1ll << (bd - 1))) >> bd));
	}
	for (unsigned x = 0; x < n; x++)           /* columns */
		for (unsigned y = 0; y < n; y++) {
			long long s = 0;

			for (unsigned k = 0; k < n; k++)
				s += (long long)dct_coef(log2n, k, y) * d[k * n + x];
			g[y * n + x] = clip3(-32768, 32767, (int)((s + 64) >> 7));
		}
	for (unsigned y = 0; y < n; y++)           /* rows */
		for (unsigned x = 0; x < n; x++) {
			long long s = 0;

			for (unsigned k = 0; k < n; k++)
				s += (long long)dct_coef(log2n, k, x) * g[y * n + k];
			res[y * n + x] = (int)((s + 2048) >> 12);
		}
}

/* Forward: orthonormal DCT (any accuracy works: the reconstruction uses the exact inverse above). */
static void fwd_quant(const int *res, int *lev, unsigned log2n, int qp)
{
	unsigned n = 1u << log2n;
	double t[32 * 32], basis[32][32];
	double q = (double)level_scale[qp % 6] * (double)(1 << (qp / 6)) / 64.0;

	for (unsigned k = 0; k < n; k++)
		for (unsigned i = 0; i < n; i++)
			basis[k][i] = (k ? sqrt(2.0 / n) : sqrt(1.0 / n)) * cos(3.14159265358979323846 * (2.0 * i + 1.0) * k / (2.0 * n));
	for (unsigned y = 0; y < n; y++)
		for (unsigned k = 0; k < n; k++) {
			double s = 0;

			for (unsigned x = 0; x < n; x++)
				s += basis[k][x] * res[y * n + x];
			t[y * n + k] = s;
		}
	for (unsigned l = 0; l < n; l++)
		for (unsigned k = 0; k < n; k++) {
			double s = 0, a;

			for (unsigned y = 0; y < n; y++)
				s += basis[l][y] * t[y * n + k];
			a = fabs(s) / q + 0.4;
			if (a > 32767)
				a = 32767;
			lev[l * n + k] = s < 0 ? -(int)a : (int)a;
		}
}

/* ------------------------------------------------------------------ scans */
struct pos { uint8_t x, y; };

static void diag_scan(struct pos *s, unsigned n)
{
	unsigned i = 0;
	int x = 0, y = 0;

	while (i < n * n) {
		while (y >= 0) {
			if (x < (int)n && y < (int)n) {
				s[i].x = (uint8_t)x;
				s[i].y = (uint8_t)y;
				i++;
			}
			y--;
			x++;
		}
		y = x;
		x = 0;
	}
}

/* ------------------------------------------------------------------ encoder state */
struct enc {
	const struct hsps *s;
	const struct hpps *p;
	struct cabac e;
	struct ctx c[C_COUNT];
	unsigned W, H, wctb;
	uint8_t *rec[3];                /* Y, Cb, Cr planes, strides W, W/2, W/2 */
	const uint8_t *sy, *suv;
	uint32_t pitch, rows;
	uint8_t *depth;                 /* CtDepth per min-CB */
	int qp, qpc[3];
	int qp_delta_coded;
	unsigned unsupported;
};

static uint32_t zs(const struct enc *E, unsigned x, unsigned y)
{
	unsigned ctbl = E->s->ctb, tbl = E->s->min_tb;
	uint32_t ctb_addr = (y >> ctbl) * E->wctb + (x >> ctbl), m = 0;
	unsigned bx = (x & ((1u << ctbl) - 1u)) >> tbl, by = (y & ((1u << ctbl) - 1u)) >> tbl;

	for (unsigned i = 0; i < ctbl - tbl; i++)
		m |= (((bx >> i) & 1u) << (2 * i)) | (((by >> i) & 1u) << (2 * i + 1));
	return (ctb_addr << (2 * (ctbl - tbl))) + m;
}

static int avail(const struct enc *E, int xc, int yc, int xn, int yn)
{
	if (xn < 0 || yn < 0 || xn >= (int)E->W || yn >= (int)E->H)
		return 0;
	return zs(E, (unsigned)xn, (unsigned)yn) < zs(E, (unsigned)xc, (unsigned)yc);
}

static int src_px(const struct enc *E, unsigned c, unsigned x, unsigned y)
{
	if (c == 0) {
		if (y >= E->rows)
			y = E->rows - 1;
		return E->sy[(size_t)y * E->pitch + x];
	}
	if (y >= E->rows / 2)
		y = E->rows / 2 - 1;
	return E->suv[(size_t)y * E->pitch + 2 * x + (c - 1)];
}

/* 8.4.4.2.1-.2 + DC (8.4.4.2.5) for a TB at component position (x0,y0), size n. */
static void pred_dc(const struct enc *E, unsigned c, unsigned x0, unsigned y0, unsigned n, int *pred)
{
	unsigned sh = c ? 1 : 0, stride = E->W >> sh;
	int ref[4 * 32 + 1], ok[4 * 32 + 1], any = 0;
	unsigned cnt = 4 * n + 1, i;
	int xl = (int)(x0 << sh), yl = (int)(y0 << sh);
	const uint8_t *r = E->rec[c];
	int dc = 0;

	/* order: p[-1][2n-1] .. p[-1][0], p[-1][-1], p[0][-1] .. p[2n-1][-1] */
	for (i = 0; i < cnt; i++) {
		int x, y;

		if (i < 2 * n) {
			x = -1;
			y = (int)(2 * n - 1 - i);
		} else if (i == 2 * n) {
			x = -1;
			y = -1;
		} else {
			x = (int)(i - 2 * n - 1);
			y = -1;
		}
		{
			int xs = (int)x0 + x, ys = (int)y0 + y;

			ok[i] = avail(E, xl, yl, xs * (1 << sh), ys * (1 << sh));
			ref[i] = ok[i] ? r[(size_t)ys * stride + (unsigned)xs] : 0;
		}
		any |= ok[i];
	}
	if (!any) {
		for (i = 0; i < cnt; i++)
			ref[i] = 128;
	} else {
		if (!ok[0])
			for (i = 1; i < cnt; i++)
				if (ok[i]) {
					ref[0] = ref[i];
					break;
				}
		for (i = 1; i < cnt; i++)
			if (!ok[i])
				ref[i] = ref[i - 1];
	}
	/* left[y] = ref[2n-1-y], top[x] = ref[2n+1+x], corner = ref[2n] */
	for (i = 0; i < n; i++)
		dc += ref[2 * n - 1 - i] + ref[2 * n + 1 + i];
	{
		unsigned log2n = 0;

		while ((1u << log2n) < n)
			log2n++;
		dc = (dc + (int)n) >> (log2n + 1);
	}
	for (i = 0; i < n * n; i++)
		pred[i] = dc;
	if (c == 0 && n < 32) {
		pred[0] = (ref[2 * n - 1] + 2 * dc + ref[2 * n + 1] + 2) >> 2;
		for (i = 1; i < n; i++) {
			pred[i] = (ref[2 * n + 1 + i] + 3 * dc + 2) >> 2;
			pred[i * n] = (ref[2 * n - 1 - i] + 3 * dc + 2) >> 2;
		}
	}
}

/* Code one TB: prediction, residual, levels, reconstruction.  Returns cbf. */
static int make_tb(struct enc *E, unsigned c, unsigned x0, unsigned y0, unsigned log2n, int *lev)
{
	unsigned n = 1u << log2n, stride = E->W >> (c ? 1 : 0);
	int pred[32 * 32], res[32 * 32], rr[32 * 32], nz = 0;
	int qp = E->qpc[c];

	pred_dc(E, c, x0, y0, n, pred);
	for (unsigned y = 0; y < n; y++)
		for (unsigned x = 0; x < n; x++)
			res[y * n + x] = src_px(E, c, x0 + x, y0 + y) - pred[y * n + x];
	fwd_quant(res, lev, log2n, qp);
	for (unsigned i = 0; i < n * n; i++)
		nz |= lev[i] != 0;
	if (nz && E->p->sign_hiding) {
		/* Sign data hiding (7.3.8.11): in every 4x4 group whose first and last nonzero scan positions
		 * are >= 4 apart the sign of the first one is implied by the parity of the sum of |levels|;
		 * fix a mismatch by making the last one larger by 1. */
		struct pos sb[64], s4[16];
		unsigned nsb = n / 4;

		diag_scan(sb, nsb);
		diag_scan(s4, 4);
		for (unsigned i = 0; i < nsb * nsb; i++) {
			int first = -1, last = -1, sum = 0;

			for (int k = 0; k < 16; k++) {
				unsigned x = sb[i].x * 4u + s4[k].x, y = sb[i].y * 4u + s4[k].y;
				int v = lev[y * n + x];

				if (v) {
					if (first < 0)
						first = k;
					last = k;
					sum += abs(v);
				}
			}
			if (first >= 0 && last - first > 3) {
				unsigned fx = sb[i].x * 4u + s4[first].x, fy = sb[i].y * 4u + s4[first].y;
				unsigned lx = sb[i].x * 4u + s4[last].x, ly = sb[i].y * 4u + s4[last].y;
				int neg = lev[fy * n + fx] < 0;

				if ((sum & 1) != neg) {
					int *l = &lev[ly * n + lx];

					*l += *l > 0 ? 1 : -1;
				}
			}
		}
	}
	if (nz)
		inv_transform(lev, rr, log2n, qp);
	for (unsigned y = 0; y < n; y++)
		for (unsigned x = 0; x < n; x++)
			E->rec[c][(size_t)(y0 + y) * stride + x0 + x] =
				(uint8_t)clip3(0, 255, pred[y * n + x] + (nz ? rr[y * n + x] : 0));
	return nz;
}

/* 7.3.8.11 residual_coding, diagonal scan (DC intra mode). */
static void code_residual(struct enc *E, const int *lev, unsigned log2n, unsigned c)
{
	static const uint8_t group_idx[32] = { 0,1,2,3,4,4,5,5,6,6,6,6,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9 };
	static const uint8_t min_in_group[10] = { 0,1,2,3,4,6,8,12,16,24 };
	static const uint8_t ctx_idx_map[16] = { 0,1,4,5,2,3,4,5,6,6,8,8,7,7,8,8 };
	unsigned n = 1u << log2n, nsb = n >> 2;
	struct pos sb[64], s4[16];
	uint8_t csbf[8][8] = { { 0 } };
	int last_sb = -1, last_k = -1;
	unsigned c1 = 1;
	struct cabac *e = &E->e;

	diag_scan(sb, nsb);
	diag_scan(s4, 4);
	if (E->p->tskip && log2n == 2)
		enc_bin(e, &E->c[C_TSKIP + (c ? 1 : 0)], 0);
	for (int i = (int)(nsb * nsb) - 1; i >= 0 && last_sb < 0; i--)
		for (int k = 15; k >= 0; k--)
			if (lev[(sb[i].y * 4u + s4[k].y) * n + sb[i].x * 4u + s4[k].x]) {
				last_sb = i;
				last_k = k;
				break;
			}
	{   /* last_sig_coeff_x/y prefix + suffix */
		unsigned lx = sb[last_sb].x * 4u + s4[last_k].x, ly = sb[last_sb].y * 4u + s4[last_k].y;
		unsigned off = c ? 15 : 3 * (log2n - 2) + ((log2n - 1) >> 2), shift = c ? log2n - 2 : (log2n + 1) >> 2;
		unsigned cmax = (log2n << 1) - 1, px = group_idx[lx], py = group_idx[ly];

		for (unsigned b = 0; b < px; b++)
			enc_bin(e, &E->c[C_LAST_X + off + (b >> shift)], 1);
		if (px < cmax)
			enc_bin(e, &E->c[C_LAST_X + off + (px >> shift)], 0);
		for (unsigned b = 0; b < py; b++)
			enc_bin(e, &E->c[C_LAST_Y + off + (b >> shift)], 1);
		if (py < cmax)
			enc_bin(e, &E->c[C_LAST_Y + off + (py >> shift)], 0);
		if (px > 3)
			enc_bypass_bits(e, (px >> 1) - 1, lx - min_in_group[px]);
		if (py > 3)
			enc_bypass_bits(e, (py >> 1) - 1, ly - min_in_group[py]);
	}
	for (int i = last_sb; i >= 0; i--) {
		unsigned xs = sb[i].x, ys = sb[i].y;
		int abs_l[16], sig_k[16], nsig = 0, infer_dc = 0;
		unsigned any = 0;

		for (int k = 0; k < 16; k++)
			any |= lev[(ys * 4u + s4[k].y) * n + xs * 4u + s4[k].x] != 0;
		if (i < last_sb && i > 0) {
			unsigned r = (xs + 1 < nsb) ? csbf[xs + 1][ys] : 0, b = (ys + 1 < nsb) ? csbf[xs][ys + 1] : 0;

			enc_bin(e, &E->c[C_CSBF + ((r | b) ? 1 : 0) + (c ? 2 : 0)], any);
			csbf[xs][ys] = (uint8_t)any;
			infer_dc = 1;
		} else {
			csbf[xs][ys] = 1;
		}
		if (i == last_sb) {
			sig_k[nsig] = last_k;
			abs_l[nsig++] = abs(lev[(ys * 4u + s4[last_k].y) * n + xs * 4u + s4[last_k].x]);
		}
		if (csbf[xs][ys]) {
			for (int k = (i == last_sb ? last_k - 1 : 15); k >= 0; k--) {
				unsigned xc = xs * 4u + s4[k].x, yc = ys * 4u + s4[k].y;
				int v = lev[yc * n + xc];

				if (k > 0 || !infer_dc) {
					unsigned sig_ctx;

					if (log2n == 2) {
						sig_ctx = ctx_idx_map[(yc << 2) + xc];
					} else if (xc + yc == 0) {
						sig_ctx = 0;
					} else {
						unsigned r = (xs + 1 < nsb) ? csbf[xs + 1][ys] : 0;
						unsigned b = (ys + 1 < nsb) ? csbf[xs][ys + 1] : 0;
						unsigned prev = r + 2u * b, xp = xc & 3u, yp = yc & 3u;

						if (prev == 0)
							sig_ctx = (xp + yp == 0) ? 2 : (xp + yp < 3) ? 1 : 0;
						else if (prev == 1)
							sig_ctx = (yp == 0) ? 2 : (yp == 1) ? 1 : 0;
						else if (prev == 2)
							sig_ctx = (xp == 0) ? 2 : (xp == 1) ? 1 : 0;
						else
							sig_ctx = 2;
						if (c == 0) {
							if (xs > 0 || ys > 0)
								sig_ctx += 3;
							sig_ctx += (log2n == 3) ? 9 : 21;   /* scanIdx 0 */
						} else {
							sig_ctx += (log2n == 3) ? 9 : 12;
						}
					}
					enc_bin(e, &E->c[C_SIG + (c ? 27 : 0) + sig_ctx], v != 0);
					if (v)
						infer_dc = 0;
				}
				if (v) {
					sig_k[nsig] = k;
					abs_l[nsig++] = abs(v);
				}
			}
		}
		if (nsig == 0)
			continue;
		{
			unsigned ctx_set = (i == 0 || c > 0) ? 0 : 2;
			int first_g1 = -1, ng1 = nsig < 8 ? nsig : 8;
			int hidden = E->p->sign_hiding && (sig_k[0] - sig_k[nsig - 1] > 3);
			unsigned rice = 0;
			int first2 = 1;

			if (c1 == 0)
				ctx_set++;
			c1 = 1;
			for (int j = 0; j < ng1; j++) {
				unsigned g1 = abs_l[j] > 1;

				enc_bin(e, &E->c[C_GT1 + (c ? 16 : 0) + ctx_set * 4 + c1], g1);
				if (g1) {
					c1 = 0;
					if (first_g1 < 0)
						first_g1 = j;
				} else if (c1 > 0 && c1 < 3) {
					c1++;
				}
			}
			if (first_g1 >= 0)
				enc_bin(e, &E->c[C_GT2 + (c ? 4 : 0) + ctx_set], abs_l[first_g1] > 2);
			for (int j = 0; j < nsig; j++) {
				unsigned xc = xs * 4u + s4[sig_k[j]].x, yc = ys * 4u + s4[sig_k[j]].y;

				if (hidden && j == nsig - 1)
					break;
				enc_bypass(e, lev[yc * n + xc] < 0);
			}
			for (int j = 0; j < nsig; j++) {
				int base = (j < 8) ? 2 + first2 : 1;

				if (abs_l[j] >= base) {
					uint32_t sym = (uint32_t)(abs_l[j] - base);

					if (sym < (3u << rice)) {
						unsigned q = sym >> rice;

						for (unsigned t = 0; t < q; t++)
							enc_bypass(e, 1);
						enc_bypass(e, 0);
						enc_bypass_bits(e, rice, sym & ((1u << rice) - 1u));
					} else {
						unsigned len = rice;

						sym -= 3u << rice;
						while (sym >= (1u << len)) {
							sym -= 1u << len;
							len++;
						}
						for (unsigned t = 0; t < 3 + len - rice; t++)
							enc_bypass(e, 1);
						enc_bypass(e, 0);
						enc_bypass_bits(e, len, sym);
					}
					if (abs_l[j] > 3 * (1 << rice))
						rice = rice + 1 < 4 ? rice + 1 : 4;
				}
				if (abs_l[j] >= 2)
					first2 = 0;
			}
		}
	}
}

/* transform_tree for an intra CU (luma TB at x0,y0 size 2^t, trafo depth d). */
struct tbres { int lev[3][32 * 32]; int cbf[3]; };

static void transform_tree(struct enc *E, unsigned x0, unsigned y0, unsigned t, unsigned d, int parent_cb,
			   int parent_cr)
{
	const struct hsps *s = E->s;
	unsigned max_depth = s->depth_intra;   /* IntraSplitFlag 0 (2Nx2N) */
	int split;

	if (t <= s->max_tb && t > s->min_tb && d < max_depth) {
		split = 0;                      /* coded, we choose no split */
		enc_bin(&E->e, &E->c[C_SPLIT_TF + 5 - t], 0);
	} else
		split = t > s->max_tb;
	if (split) {
		/* The children are predicted from reconstructed neighbours one after the other, so this node's
		 * cbf_cb/cbf_cr are coded as 1 (valid even if every child then codes 0). */
		unsigned h = 1u << (t - 1);
		int cb = 0, cr = 0;

		if (t > 2) {
			if (d == 0 || parent_cb)
				enc_bin(&E->e, &E->c[C_CBF_CHROMA + d], 1);
			if (d == 0 || parent_cr)
				enc_bin(&E->e, &E->c[C_CBF_CHROMA + d], 1);
			cb = parent_cb || d == 0;
			cr = parent_cr || d == 0;
		}
		transform_tree(E, x0, y0, t - 1, d + 1, cb, cr);
		transform_tree(E, x0 + h, y0, t - 1, d + 1, cb, cr);
		transform_tree(E, x0, y0 + h, t - 1, d + 1, cb, cr);
		transform_tree(E, x0 + h, y0 + h, t - 1, d + 1, cb, cr);
		return;
	}
	{
		static _Thread_local struct tbres r;
		int chroma = t > 2;             /* 4:2:0; a 4x4 luma TB never occurs here (CU >= 8, largest TBs) */

		r.cbf[0] = make_tb(E, 0, x0, y0, t, r.lev[0]);
		r.cbf[1] = chroma ? make_tb(E, 1, x0 >> 1, y0 >> 1, t - 1, r.lev[1]) : 0;
		r.cbf[2] = chroma ? make_tb(E, 2, x0 >> 1, y0 >> 1, t - 1, r.lev[2]) : 0;
		if (chroma) {
			if (d == 0 || parent_cb)
				enc_bin(&E->e, &E->c[C_CBF_CHROMA + d], (unsigned)r.cbf[1]);
			else if (r.cbf[1])
				E->unsupported++;   /* cannot happen: parents are coded 1 */
			if (d == 0 || parent_cr)
				enc_bin(&E->e, &E->c[C_CBF_CHROMA + d], (unsigned)r.cbf[2]);
		}
		enc_bin(&E->e, &E->c[C_CBF_LUMA + (d == 0 ? 1 : 0)], (unsigned)r.cbf[0]);
		if ((r.cbf[0] || r.cbf[1] || r.cbf[2]) && E->p->cu_qp_delta && !E->qp_delta_coded) {
			enc_bin(&E->e, &E->c[C_DQP], 0);   /* cu_qp_delta_abs = 0 */
			E->qp_delta_coded = 1;
		}
		if (r.cbf[0])
			code_residual(E, r.lev[0], t, 0);
		if (r.cbf[1])
			code_residual(E, r.lev[1], t - 1, 1);
		if (r.cbf[2])
			code_residual(E, r.lev[2], t - 1, 2);
	}
}

static void coding_unit(struct enc *E, unsigned x0, unsigned y0, unsigned l)
{
	const struct hsps *s = E->s;
	struct cabac *e = &E->e;

	if (E->p->tq_bypass)
		enc_bin(e, &E->c[C_TQB], 0);
	if (l == s->min_cb)
		enc_bin(e, &E->c[C_PART], 1);              /* PART_2Nx2N */
	if (s->pcm && l >= s->pcm_min && l <= s->pcm_max)
		enc_term(e, 0);                         /* pcm_flag = 0 */
	enc_bin(e, &E->c[C_PREV_INTRA], 1);            /* prev_intra_luma_pred_flag: DC is MPM[1] */
	enc_bypass(e, 1);                               /* mpm_idx = 1: "10" */
	enc_bypass(e, 0);
	enc_bin(e, &E->c[C_CHROMA_MODE], 0);           /* intra_chroma_pred_mode = 4 (DM) */
	for (unsigned y = 0; y < (1u << l); y += 1u << s->min_cb)
		for (unsigned x = 0; x < (1u << l); x += 1u << s->min_cb)
			E->depth[((y0 + y) >> s->min_cb) * (E->W >> s->min_cb) + ((x0 + x) >> s->min_cb)] =
				(uint8_t)(s->ctb - l);
	transform_tree(E, x0, y0, l, 0, 1, 1);
}

static void coding_quadtree(struct enc *E, unsigned x0, unsigned y0, unsigned l, unsigned cu_l)
{
	const struct hsps *s = E->s;
	int split;
	unsigned depth = s->ctb - l;

	if (x0 >= E->W || y0 >= E->H)
		return;
	if (x0 + (1u << l) <= E->W && y0 + (1u << l) <= E->H && l > s->min_cb) {
		unsigned inc = 0;
		unsigned mw = E->W >> s->min_cb;

		split = l > cu_l;
		if (avail(E, (int)x0, (int)y0, (int)x0 - 1, (int)y0) &&
		    E->depth[(y0 >> s->min_cb) * mw + ((x0 - 1) >> s->min_cb)] > depth)
			inc++;
		if (avail(E, (int)x0, (int)y0, (int)x0, (int)y0 - 1) &&
		    E->depth[((y0 - 1) >> s->min_cb) * mw + (x0 >> s->min_cb)] > depth)
			inc++;
		enc_bin(&E->e, &E->c[C_SPLIT + inc], (unsigned)split);
	} else {
		split = l > s->min_cb;
	}
	if (E->p->cu_qp_delta && l >= s->ctb - E->p->diff_qp_depth)
		E->qp_delta_coded = 0;
	if (split) {
		unsigned h = 1u << (l - 1);

		coding_quadtree(E, x0, y0, l - 1, cu_l);
		coding_quadtree(E, x0 + h, y0, l - 1, cu_l);
		coding_quadtree(E, x0, y0 + h, l - 1, cu_l);
		coding_quadtree(E, x0 + h, y0 + h, l - 1, cu_l);
	} else {
		coding_unit(E, x0, y0, l);
	}
}

static int chroma_qp(int qpi)
{
	static const uint8_t t[14] = { 29, 30, 31, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37 };

	qpi = clip3(0, 57, qpi);
	if (qpi < 30)
		return qpi;
	if (qpi > 43)
		return qpi - 6;
	return t[qpi - 30];
}

static uint32_t encode_locked(const uint8_t *y, const uint8_t *uv, uint32_t pitch, uint32_t rows, uint8_t *out,
			      uint32_t cap, uint8_t *recon, int qp)
{
	const struct hsps *s = &g_sps;
	const struct hpps *p = &g_pps;
	struct enc E;
	struct bw w;
	uint8_t *rbsp = NULL;
	uint32_t rcap, o = 0, z = 0;
	unsigned cu_l;
	size_t ysz, csz;

	if (!s->valid || !p->valid || !y || !uv || !out || rows < 2 || pitch < s->w)
		return 0;
	memset(&E, 0, sizeof E);
	E.s = s;
	E.p = p;
	E.W = s->w;
	E.H = s->h;
	E.wctb = (E.W + (1u << s->ctb) - 1u) >> s->ctb;
	E.sy = y;
	E.suv = uv;
	E.pitch = pitch;
	E.rows = rows;
	if (s->chroma != 1 || s->bd_y != 8 || s->bd_c != 8 || p->wpp)
		E.unsupported++;
	E.qp = qp;
	E.qpc[0] = E.qp;
	E.qpc[1] = chroma_qp(E.qp + p->cb_off);
	E.qpc[2] = chroma_qp(E.qp + p->cr_off);
	ysz = (size_t)E.W * E.H;
	csz = (size_t)(E.W / 2) * (E.H / 2);
	E.rec[0] = malloc(ysz);
	E.rec[1] = malloc(csz);
	E.rec[2] = malloc(csz);
	E.depth = calloc((E.W >> s->min_cb) * (E.H >> s->min_cb), 1);
	rcap = (uint32_t)(ysz * 3 + 4096);
	rbsp = malloc(rcap);
	if (!E.rec[0] || !E.rec[1] || !E.rec[2] || !E.depth || !rbsp)
		goto fail;
	w.p = rbsp;
	w.cap = rcap;
	w.pos = 0;
	w.err = 0;
	/* slice_segment_header, IDR_W_RADL, I slice */
	bw_u(&w, 1, 1);                         /* first_slice_segment_in_pic_flag */
	bw_u(&w, 1, 0);                         /* no_output_of_prior_pics_flag */
	bw_ue(&w, p->pps_id);
	for (unsigned i = 0; i < p->extra_bits; i++)
		bw_u(&w, 1, 0);                 /* slice_reserved_flag */
	bw_ue(&w, 2);                           /* slice_type I */
	if (p->output_flag)
		bw_u(&w, 1, 1);                 /* pic_output_flag */
	if (s->sao) {
		bw_u(&w, 1, 0);                 /* slice_sao_luma_flag */
		bw_u(&w, 1, 0);                 /* slice_sao_chroma_flag */
	}
	bw_se(&w, E.qp - p->init_qp);           /* slice_qp_delta */
	if (p->slice_chroma_qp_offsets) {
		bw_se(&w, 0);
		bw_se(&w, 0);
	}
	{
		int dbk_off = p->dbk_disable;

		if (p->dbk_override) {
			bw_u(&w, 1, 1);         /* deblocking_filter_override_flag */
			bw_u(&w, 1, 1);         /* slice_deblocking_filter_disabled_flag */
			dbk_off = 1;
		}
		if (p->lf_across && !dbk_off)
			bw_u(&w, 1, 0);         /* slice_loop_filter_across_slices_enabled_flag */
	}
	if (p->wpp)
		bw_ue(&w, 0);                   /* num_entry_point_offsets */
	if (p->sh_ext)
		bw_ue(&w, 0);
	bw_u(&w, 1, 1);                         /* byte_alignment() */
	while (w.pos & 7u)
		bw_u(&w, 1, 0);
	/* slice_segment_data */
	for (unsigned i = 0; i < C_COUNT; i++)
		ctx_init(&E.c[i], ctx_init_i[i], E.qp);
	E.e.w = &w;
	E.e.low = 0;
	E.e.range = 510;
	E.e.first = 1;
	E.e.outstanding = 0;
	cu_l = 4;
	if (cu_l < s->min_cb)
		cu_l = s->min_cb;
	if (cu_l > s->ctb)
		cu_l = s->ctb;
	{
		unsigned hctb = (E.H + (1u << s->ctb) - 1u) >> s->ctb, total = E.wctb * hctb, k = 0;

		for (unsigned cy = 0; cy < hctb; cy++)
			for (unsigned cx = 0; cx < E.wctb; cx++) {
				coding_quadtree(&E, cx << s->ctb, cy << s->ctb, s->ctb, cu_l);
				enc_term(&E.e, ++k == total);      /* end_of_slice_segment_flag */
			}
	}
	while (w.pos & 7u)
		bw_u(&w, 1, 0);                 /* rbsp_slice_segment_trailing_bits alignment */
	if (w.err)
		goto fail;
	/* NAL: start code, header (type 19 IDR_W_RADL, layer 0, tid 1), emulation prevention */
	if (cap < 6)
		goto fail;
	out[o++] = 0;
	out[o++] = 0;
	out[o++] = 0;
	out[o++] = 1;
	out[o++] = 19 << 1;
	out[o++] = 1;
	for (uint32_t i = 0; i < (w.pos >> 3); i++) {
		if (z >= 2 && rbsp[i] <= 3) {
			if (o >= cap)
				goto fail;
			out[o++] = 3;
			z = 0;
		}
		if (o >= cap)
			goto fail;
		out[o++] = rbsp[i];
		z = rbsp[i] == 0 ? z + 1 : 0;
	}
	if (recon) {
		memcpy(recon, E.rec[0], ysz);
		memcpy(recon + ysz, E.rec[1], csz);
		memcpy(recon + ysz + csz, E.rec[2], csz);
	}
	g_st.unsupported += E.unsupported;
	g_st.qp = (unsigned)E.qp;
	g_st.width = E.W;
	g_st.height = E.H;
	goto done;
fail:
	o = 0;
done:
	free(E.rec[0]);
	free(E.rec[1]);
	free(E.rec[2]);
	free(E.depth);
	free(rbsp);
	return o;
}

uint32_t n2venc_hevc_encode_idr(const uint8_t *y, const uint8_t *uv, uint32_t pitch, uint32_t rows,
				uint8_t *out, uint32_t cap, uint8_t *recon)
{
	uint32_t n;

	pthread_mutex_lock(&g_lock);
	n = encode_locked(y, uv, pitch, rows, out, cap, recon, N2VENC_HEVC_QP);
	pthread_mutex_unlock(&g_lock);
	return n;
}

int n2venc_hevc_set_headers(const uint8_t *annexb, uint32_t bytes)
{
	struct hsps s;
	struct hpps p;
	int rc;

	if (!annexb)
		return -EINVAL;
	rc = learn(annexb, bytes, &s, &p);
	if (rc)
		return -EINVAL;
	pthread_mutex_lock(&g_lock);
	g_sps = s;
	g_pps = p;
	g_st.headers_learned++;
	pthread_mutex_unlock(&g_lock);
	return 0;
}

void n2venc_hevc_reset(void)
{
	pthread_mutex_lock(&g_lock);
	memset(&g_sps, 0, sizeof g_sps);
	memset(&g_pps, 0, sizeof g_pps);
	memset(&g_st, 0, sizeof g_st);
	pthread_mutex_unlock(&g_lock);
}

void n2venc_hevc_get_stats(struct n2venc_hevc_stats *out)
{
	if (!out)
		return;
	pthread_mutex_lock(&g_lock);
	*out = g_st;
	pthread_mutex_unlock(&g_lock);
}

/* The VPS/SPS/PPS configuration libvcodecdrv.so writes for 320x240, 640x480 and 1920x1080 alike
 * (host runs, facts/HEVC_ENC_PROTOCOL.md section 4); only the size and level_idc change. */
static void vendor_defaults(struct hsps *s, struct hpps *p, unsigned w, unsigned h)
{
	memset(s, 0, sizeof *s);
	memset(p, 0, sizeof *p);
	s->chroma = 1;
	s->w = w;
	s->h = h;
	s->bd_y = s->bd_c = 8;
	s->min_cb = 4;
	s->ctb = 5;
	s->min_tb = 3;
	s->max_tb = 4;
	s->depth_intra = 0;
	s->sao = 1;
	s->valid = (w % 16u) == 0 && (h % 16u) == 0 && w <= 8192 && h <= 8192;
	p->sign_hiding = 1;
	p->init_qp = 26;
	p->cu_qp_delta = 1;
	p->lf_across = 1;
	p->dbk_ctrl = 1;
	p->dbk_override = 1;
	p->valid = 1;
}

/* Read `rows` lines of `pitch` bytes at pa; lines past the end of the buffer repeat the last line. */
static int read_plane(const struct n2venc_hevc_io *io, uint32_t pa, uint8_t *dst, uint32_t pitch, uint32_t rows)
{
	uint32_t avail = 0, got;

	if (io->avail(io->ctx, pa, &avail) != 0)
		return -ENOENT;
	got = avail / pitch;
	if (got > rows)
		got = rows;
	if (got == 0 || io->dma(io->ctx, pa, dst, got * pitch, 0) != 0)
		return -EIO;
	for (uint32_t r = got; r < rows; r++)
		memcpy(dst + (size_t)r * pitch, dst + (size_t)(got - 1) * pitch, pitch);
	return 0;
}

int n2venc_hevc_frame(const struct n2venc_hevc_io *io, uint32_t *bs_cap)
{
	uint32_t bs_pa, y_pa, c_pa, pitch, cap, avail = 0, n = 0, rows;
	uint8_t *yb = NULL, *cb = NULL, *out = NULL;
	int have;

	if (!io || !io->reg || !io->dma || !io->avail || !bs_cap)
		return -1;
	bs_pa = io->reg[VR_BS_ADDR / 4u] << 4;
	y_pa = io->reg[VR_SRC_LUMA / 4u] << 4;
	c_pa = io->reg[VR_SRC_CHROMA / 4u] << 4;
	pitch = ((io->reg[VR_PITCH / 4u] >> 20) & 0xfffu) * 16u;
	cap = io->reg[VR_BS_SIZE / 4u] << 7;
	/* 0x000 bit 2: HEVC_SetHWModeSel starts from 0x02000004 (drv 0x157dd8), H264_SetHWModeSel from
	 * 0x0200bf60 (drv 0x13e8b2); both write 0x000 before every frame trigger. */
	if (!(io->reg[VR_MODE / 4u] & 4u))
		return -1;              /* an H.264 frame: the H.264 model handles it */
	{
		/* A separate SEQ_HDR job leaves VPS/SPS/PPS at the start of the bitstream buffer: learn them
		 * (again: a new session may have changed them).  With headers joined to the IDR (PREPEND,
		 * the daemon's mode) the library keeps them elsewhere until the frame is done; then use the
		 * configuration the library writes for every size (facts/HEVC_ENC_PROTOCOL.md section 4) with
		 * the picture size of register 0x114c. */
		uint8_t scan[512];
		uint32_t sn, pw = io->reg[VR_PIC_SIZE / 4u] & 0xffffu, ph = io->reg[VR_PIC_SIZE / 4u] >> 16;

		if (io->avail(io->ctx, bs_pa, &avail) == 0) {
			sn = avail < sizeof scan ? avail : (uint32_t)sizeof scan;
			if (io->dma(io->ctx, bs_pa, scan, sn, 0) == 0)
				(void)n2venc_hevc_set_headers(scan, sn);
		}
		pthread_mutex_lock(&g_lock);
		have = g_sps.valid && g_pps.valid;
		if ((!have || g_sps.w != pw || g_sps.h != ph) && pw && ph) {
			vendor_defaults(&g_sps, &g_pps, pw, ph);
			g_st.defaults++;
			have = g_sps.valid;
		}
		pthread_mutex_unlock(&g_lock);
		if (!have) {
			pthread_mutex_lock(&g_lock);
			g_st.unsupported++;
			pthread_mutex_unlock(&g_lock);
			return -1;
		}
	}
	pthread_mutex_lock(&g_lock);
	g_st.frames++;
	pthread_mutex_unlock(&g_lock);
	if (io->avail(io->ctx, bs_pa, &avail) != 0)
		goto dma_err;
	if (!cap || cap > avail)
		cap = avail;
	*bs_cap = cap;
	if (!pitch)
		pitch = g_sps.w;
	rows = (g_sps.h + 15u) & ~15u;
	yb = malloc((size_t)pitch * rows);
	cb = malloc((size_t)pitch * rows / 2);
	out = malloc(cap ? cap : 1u);
	if (!yb || !cb || !out || pitch < g_sps.w)
		goto dma_err;
	if (read_plane(io, y_pa, yb, pitch, rows) != 0 || read_plane(io, c_pa, cb, pitch, rows / 2) != 0)
		goto dma_err;
	/* Like the hardware's rate control would, coarser quantisation until the picture fits the buffer. */
	for (int qp = N2VENC_HEVC_QP; qp <= 51 && n == 0; qp = qp + 6 > 51 && qp < 51 ? 51 : qp + 6) {
		pthread_mutex_lock(&g_lock);
		n = encode_locked(yb, cb, pitch, rows, out, cap, NULL, qp);
		pthread_mutex_unlock(&g_lock);
	}
	if (n == 0) {
		pthread_mutex_lock(&g_lock);
		g_st.overflow++;
		pthread_mutex_unlock(&g_lock);
	} else if (io->dma(io->ctx, bs_pa, out, n, 1) != 0) {
		goto dma_err;
	}
	pthread_mutex_lock(&g_lock);
	g_st.last_bytes = n;
	pthread_mutex_unlock(&g_lock);
	free(yb);
	free(cb);
	free(out);
	return (int)n;
dma_err:
	pthread_mutex_lock(&g_lock);
	g_st.dma_errors++;
	pthread_mutex_unlock(&g_lock);
	free(yb);
	free(cb);
	free(out);
	return 0;
}
