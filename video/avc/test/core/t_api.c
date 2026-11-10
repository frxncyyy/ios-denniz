/* SPDX-License-Identifier: MIT */
/*
 * t_api.c -- tests of the public interface (include/n2avc.h) of the WP03 H.264 encoder, plus the I_PCM fallback
 * of the macroblock layer (which needs the internal structures).
 *
 * The picture source is generated in the test (deterministic gradient and a 32-bit LCG for noise), so the suite
 * does not depend on vectors/ before WP09 lands.
 */
#include "n2avc_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                                   \
	checks++;                                                               \
	if (!(cond)) {                                                          \
		failures++;                                                     \
		fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
		fprintf(stderr, __VA_ARGS__);                                   \
		fprintf(stderr, "\n");                                          \
	}                                                                       \
} while (0)

/* ------------------------------------------------------------------ source pictures */
static uint32_t lcg = 1;
static uint32_t lcg_next(void)
{
	lcg = lcg * 1664525u + 1013904223u;
	return lcg;
}

/* gradient luma, neutral chroma; noise = the 32-bit LCG (as vectors/gen_yuv.py defines them) */
static void make_picture(uint8_t *dst, int w, int h, int noise, int frame, int stride_y, int stride_c)
{
	int x, y, cw = w / 2, ch = h / 2;
	uint8_t *cb = dst + (size_t)stride_y * h;
	uint8_t *cr = cb + (size_t)stride_c * ch;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			dst[(size_t)y * stride_y + x] = noise ? (uint8_t)(lcg_next() >> 24)
							      : (uint8_t)((x + 2 * y + 3 * frame) & 255);
	for (y = 0; y < ch; y++)
		for (x = 0; x < cw; x++) {
			cb[(size_t)y * stride_c + x] = (uint8_t)(128 + ((x + frame) & 31));
			cr[(size_t)y * stride_c + x] = (uint8_t)(128 - (y & 31));
		}
}

/* ------------------------------------------------------------------ SPS parser (independent of the writer) */
typedef struct { const uint8_t *p; size_t n; size_t bit; } bitrd;

static uint32_t br_u(bitrd *b, int n)
{
	uint32_t v = 0;
	int i;
	for (i = 0; i < n; i++) {
		size_t byte = b->bit >> 3;
		int off = 7 - (int)(b->bit & 7);
		v = (v << 1) | (uint32_t)((byte < b->n ? (b->p[byte] >> off) & 1 : 0));
		b->bit++;
	}
	return v;
}

static uint32_t br_ue(bitrd *b)
{
	int zeros = 0;
	while (br_u(b, 1) == 0)
		zeros++;
	return (uint32_t)((1u << zeros) - 1 + br_u(b, zeros));
}

/* Parses the SPS NAL of a 4:2:0 Constrained Baseline stream and checks the values include/n2avc.h promises. */
static int parse_sps(const uint8_t *nal, size_t n, unsigned w, unsigned h, unsigned fps_num, unsigned fps_den,
		     unsigned *level_out, int *crop_right, int *crop_bottom, int *full_range)
{
	bitrd b;
	unsigned profile, cons, level, wm1, hm1, frame_mbs_only, crop_flag;

	uint8_t rbsp[128];
	size_t i, o = 0;
	int zeros = 0;

	if (n < 4 || (nal[0] & 31) != 7)
		return 0;
	/* clause 7.4.1: remove emulation_prevention_three_byte before parsing. */
	for (i = 1; i < n && o < sizeof rbsp; i++) {
		if (zeros >= 2 && nal[i] == 3) {
			zeros = 0;
			continue;
		}
		rbsp[o++] = nal[i];
		zeros = (nal[i] == 0) ? zeros + 1 : 0;
	}
	b.p = rbsp;
	b.n = o;
	b.bit = 0;
	profile = br_u(&b, 8);
	cons = br_u(&b, 8);
	level = br_u(&b, 8);
	if (profile != 66)
		return 0;
	if ((cons & 0xc0) != 0xc0)              /* constraint_set0_flag and constraint_set1_flag */
		return 0;
	if (br_ue(&b) != 0)                     /* seq_parameter_set_id */
		return 0;
	if (br_ue(&b) != 4)                     /* log2_max_frame_num_minus4 */
		return 0;
	if (br_ue(&b) != 2)                     /* pic_order_cnt_type */
		return 0;
	if (br_ue(&b) != 1)                     /* max_num_ref_frames */
		return 0;
	if (br_u(&b, 1) != 0)                   /* gaps_in_frame_num_value_allowed_flag */
		return 0;
	wm1 = br_ue(&b);
	hm1 = br_ue(&b);
	frame_mbs_only = br_u(&b, 1);
	if (frame_mbs_only != 1)
		return 0;
	if (br_u(&b, 1) != 1)                   /* direct_8x8_inference_flag */
		return 0;
	crop_flag = br_u(&b, 1);
	*crop_right = *crop_bottom = 0;
	if (crop_flag) {
		if (br_ue(&b) != 0)             /* left */
			return 0;
		*crop_right = (int)br_ue(&b);
		if (br_ue(&b) != 0)             /* top */
			return 0;
		*crop_bottom = (int)br_ue(&b);
	}
	if (wm1 != (w + 15) / 16 - 1 || hm1 != (h + 15) / 16 - 1)
		return 0;
	if (*crop_right * 2 + w != 16 * (wm1 + 1))
		return 0;
	if (*crop_bottom * 2 + h != 16 * (hm1 + 1))
		return 0;
	if (br_u(&b, 1) != 1)                   /* vui_parameters_present_flag */
		return 0;
	if (br_u(&b, 1) != 0)                   /* aspect_ratio_info_present_flag */
		return 0;
	if (br_u(&b, 1) != 0)                   /* overscan_info_present_flag */
		return 0;
	if (br_u(&b, 1) != 1)                   /* video_signal_type_present_flag */
		return 0;
	if (br_u(&b, 3) != 5)                   /* video_format */
		return 0;
	*full_range = (int)br_u(&b, 1);
	if (br_u(&b, 1) != 1)                   /* colour_description_present_flag */
		return 0;
	(void)br_u(&b, 8);
	(void)br_u(&b, 8);
	(void)br_u(&b, 8);
	if (br_u(&b, 1) != 0)                   /* chroma_loc_info_present_flag */
		return 0;
	if (br_u(&b, 1) != 1)                   /* timing_info_present_flag */
		return 0;
	if (br_u(&b, 32) != fps_den)
		return 0;
	if (br_u(&b, 32) != 2 * fps_num)
		return 0;
	if (br_u(&b, 1) != 0)                   /* fixed_frame_rate_flag */
		return 0;
	if (br_u(&b, 1) != 0 || br_u(&b, 1) != 0)       /* hrd flags */
		return 0;
	if (br_u(&b, 1) != 0)                   /* pic_struct_present_flag */
		return 0;
	if (br_u(&b, 1) != 1)                   /* bitstream_restriction_flag */
		return 0;
	if (br_u(&b, 1) != 1)                   /* motion_vectors_over_pic_boundaries_flag */
		return 0;
	if (br_ue(&b) != 0 || br_ue(&b) != 0)   /* max_bytes_per_pic_denom, max_bits_per_mb_denom */
		return 0;
	if (br_ue(&b) != 9 || br_ue(&b) != 9)   /* log2_max_mv_length_horizontal/vertical */
		return 0;
	if (br_ue(&b) != 0)                     /* num_reorder_frames */
		return 0;
	if (br_ue(&b) != 1)                     /* max_dec_frame_buffering */
		return 0;
	*level_out = level;
	return 1;
}

/* ------------------------------------------------------------------ tests */
static void test_open_errors(void)
{
	n2avc_config cfg;
	n2avc_enc *e = (n2avc_enc *)&cfg;       /* non-NULL so that a stray write is visible */

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = 176;
	cfg.height = 144;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 26;

	CHECK(n2avc_open(NULL, &e) == N2AVC_E_ARGUMENT, "open(NULL)");
	CHECK(n2avc_open(&cfg, NULL) == N2AVC_E_ARGUMENT, "open(out = NULL)");
	{
		n2avc_config bad = cfg;
		bad.size = sizeof cfg - 4;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "size mismatch");
		CHECK(e == NULL, "out cleared on error");
	}
	{
		n2avc_config bad = cfg;
		bad.width = 177;                /* odd: 4:2:0 cannot represent it */
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "odd width");
		bad = cfg;
		bad.height = 145;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "odd height");
		bad = cfg;
		bad.width = 8;                  /* below 16 */
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "width < 16");
		bad = cfg;
		bad.width = 4098;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "width > 4096");
		bad = cfg;
		bad.height = 2306;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "height > 2304");
		bad = cfg;
		bad.qp = 9;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "qp 9");
		bad = cfg;
		bad.qp = 52;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "qp 52");
		bad = cfg;
		bad.threads = 17;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "threads 17");
		bad = cfg;
		bad.search_range = 65;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "search_range 65");
		bad = cfg;
		bad.flags = N2AVC_F_DEBLOCK;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "N2AVC_F_DEBLOCK is not implemented");
		bad = cfg;
		bad.flags = 0x40000000u;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "unknown flag bit");
		bad = cfg;
		bad.fps_num = 0;
		e = NULL;
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "fps_num 0 with fps_den 1");
	}
}

static void test_headers_and_encode(void)
{
	n2avc_config cfg;
	n2avc_enc *e = NULL;
	const uint8_t *sps, *pps;
	size_t sps_n, pps_n;
	int crop_r, crop_b, full_range;
	unsigned level = 0;
	int rc;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = 178;                        /* not a multiple of 16: frame_cropping */
	cfg.height = 146;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 26;
	cfg.threads = 2;
	cfg.flags = N2AVC_F_HEADERS_IN_KEYFRAMES;   /* the key frame then carries SPS and PPS in band */
	rc = n2avc_open(&cfg, &e);
	CHECK(rc == N2AVC_OK && e != NULL, "open 178x146 (rc %d)", rc);
	if (!e)
		return;

	CHECK(n2avc_headers(e, &sps, &sps_n, &pps, &pps_n) == N2AVC_OK, "headers");
	CHECK(sps_n > 4 && (sps[0] & 31) == 7 && ((sps[0] >> 5) & 3) == 3, "SPS NAL header 0x67");
	CHECK(pps_n > 1 && (pps[0] & 31) == 8 && ((pps[0] >> 5) & 3) == 3, "PPS NAL header 0x68");
	CHECK(parse_sps(sps, sps_n, 178, 146, 30, 1, &level, &crop_r, &crop_b, &full_range),
	      "SPS parses as the documented Constrained Baseline stream");
	CHECK(crop_r == 7 && crop_b == 7, "crop right %d bottom %d (want 7 and 7)", crop_r, crop_b);
	CHECK(full_range == 0, "video_full_range_flag 0 by default");
	CHECK(level == 31, "level_idc %u for 178x146 at 30 fps", level);
	{
		n2avc_enc *e2 = NULL;
		n2avc_config c2 = cfg;
		unsigned l2 = 0;
		int cr, cb2, fr;
		c2.width = 1920;
		c2.height = 1080;
		c2.threads = 1;
		c2.flags = N2AVC_F_FULLRANGE;
		CHECK(n2avc_open(&c2, &e2) == N2AVC_OK, "open 1920x1080");
		if (e2) {
			CHECK(n2avc_headers(e2, &sps, &sps_n, &pps, &pps_n) == N2AVC_OK, "headers 1080p");
			CHECK(parse_sps(sps, sps_n, 1920, 1080, 30, 1, &l2, &cr, &cb2, &fr),
			      "1080p SPS parses");
			CHECK(l2 == 40, "level_idc %u for 1920x1080 at 30 fps", l2);
			CHECK(fr == 1, "N2AVC_F_FULLRANGE sets video_full_range_flag");
			n2avc_close(e2);
		}
	}

	/* One gradient picture and one with noise; both must encode, and the packet must carry what it promises. */
	{
		int w = 178, h = 146, sy = 192, sc = 96;
		uint8_t *img = (uint8_t *)calloc(1, (size_t)sy * h + 2 * (size_t)sc * (h / 2));
		n2avc_picture pic;
		n2avc_packet pkt;
		int f;

		CHECK(img != NULL, "alloc");
		if (!img) {
			n2avc_close(e);
			return;
		}
		for (f = 0; f < 3; f++) {
			make_picture(img, w, h, f == 2, f, sy, sc);
			memset(&pic, 0, sizeof pic);
			pic.format = N2AVC_FMT_I420;
			pic.plane[0] = img;
			pic.stride[0] = (uint32_t)sy;
			pic.plane[1] = img + (size_t)sy * h;
			pic.stride[1] = (uint32_t)sc;
			pic.plane[2] = img + (size_t)sy * h + (size_t)sc * (h / 2);
			pic.stride[2] = (uint32_t)sc;
			pic.pts = 1000 + f;
			memset(&pkt, 0, sizeof pkt);
			rc = n2avc_encode(e, &pic, 0, &pkt);
			CHECK(rc == N2AVC_OK, "encode picture %d (rc %d)", f, rc);
			if (rc != N2AVC_OK)
				break;
			CHECK(pkt.size > 0 && pkt.data != NULL, "packet %d has bytes", f);
			CHECK(pkt.pts == 1000 + f, "pts is copied (%lld)", (long long)pkt.pts);
			CHECK(pkt.qp == 26, "constant QP 26, got %u", pkt.qp);
			CHECK(pkt.keyframe == (f == 0 ? 1u : 0u), "picture %d keyframe %u", f, pkt.keyframe);
			CHECK(pkt.frame_num == (uint32_t)f, "picture %d frame_num %u", f, pkt.frame_num);
			if (f == 0) {
				/* AVCC by default: SPS, PPS and the IDR slice in key frame order, with 4-byte lengths. */
				size_t off = 0;
				int seen_sps = 0, seen_pps = 0, seen_idr = 0;
				while (off + 4 <= pkt.size) {
					size_t len = ((size_t)pkt.data[off] << 24) | ((size_t)pkt.data[off + 1] << 16) |
						     ((size_t)pkt.data[off + 2] << 8) | pkt.data[off + 3];
					int type;
					off += 4;
					CHECK(len > 0 && off + len <= pkt.size, "AVCC length inside the packet");
					if (!len || off + len > pkt.size)
						break;
					type = pkt.data[off] & 31;
					if (type == 7)
						seen_sps = 1;
					if (type == 8)
						seen_pps = 1;
					if (type == 5)
						seen_idr = 1;
					off += len;
				}
				CHECK(seen_sps && seen_pps && seen_idr, "key frame carries SPS, PPS and IDR (N2AVC_F_HEADERS_IN_KEYFRAMES)");
				CHECK(off == pkt.size, "AVCC lengths cover the packet exactly (%zu of %zu)", off, pkt.size);
			}
		}

		/* The reconstruction of the last picture: three planes, strides that cover the picture. */
		{
			const uint8_t *pl[3];
			uint32_t st[3];
			CHECK(n2avc_reconstruction(e, pl, st) == N2AVC_OK, "reconstruction");
			CHECK(pl[0] && pl[1] && pl[2], "three planes");
			CHECK(st[0] >= 178 && st[1] >= 89 && st[2] >= 89, "strides %u %u %u", st[0], st[1], st[2]);
		}

		/* reconfigure: no error, and the next picture still codes. */
		CHECK(n2avc_reconfigure(e, 800000, 25, 1) == N2AVC_OK, "reconfigure");
		make_picture(img, w, h, 0, 3, sy, sc);
		{
			n2avc_picture pic;
			n2avc_packet pkt;
			memset(&pic, 0, sizeof pic);
			pic.format = N2AVC_FMT_I420;
			pic.plane[0] = img;
			pic.stride[0] = (uint32_t)sy;
			pic.plane[1] = img + (size_t)sy * h;
			pic.stride[1] = (uint32_t)sc;
			pic.plane[2] = img + (size_t)sy * h + (size_t)sc * (h / 2);
			pic.stride[2] = (uint32_t)sc;
			CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_OK, "encode after reconfigure");
		}
		/* Force an IDR. */
		{
			n2avc_picture pic;
			n2avc_packet pkt;
			memset(&pic, 0, sizeof pic);
			pic.format = N2AVC_FMT_I420;
			pic.plane[0] = img;
			pic.stride[0] = (uint32_t)sy;
			pic.plane[1] = img + (size_t)sy * h;
			pic.stride[1] = (uint32_t)sc;
			pic.plane[2] = img + (size_t)sy * h + (size_t)sc * (h / 2);
			pic.stride[2] = (uint32_t)sc;
			CHECK(n2avc_encode(e, &pic, 1, &pkt) == N2AVC_OK, "forced IDR");
			CHECK(pkt.keyframe == 1, "forced IDR is a key frame");
		}
		/* Distrusted inputs. */
		{
			n2avc_picture pic;
			n2avc_packet pkt;
			memset(&pic, 0, sizeof pic);
			pic.format = N2AVC_FMT_I420;
			CHECK(n2avc_encode(e, NULL, 0, &pkt) == N2AVC_E_ARGUMENT, "NULL picture");
			pic.plane[0] = NULL;
			CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_E_ARGUMENT, "NULL plane");
			pic.plane[0] = img;
			pic.stride[0] = 100;            /* smaller than the width */
			pic.plane[1] = img + (size_t)sy * h;
			pic.stride[1] = (uint32_t)sc;
			pic.plane[2] = img + (size_t)sy * h + (size_t)sc * (h / 2);
			pic.stride[2] = (uint32_t)sc;
			CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_E_ARGUMENT, "stride smaller than the width");
			pic.stride[0] = (uint32_t)sy;
			pic.format = 99;
			CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_E_ARGUMENT, "unknown format");
		}
		free(img);
	}
	n2avc_close(e);
}

/* A flat picture is reconstructed exactly (no residual anywhere) at any QP: a strong check of the whole
 * prediction/reconstruction path that does not depend on quantisation. */
static void test_flat_exact(void)
{
	n2avc_config cfg;
	n2avc_enc *e = NULL;
	int w = 64, h = 48;
	uint8_t *img = (uint8_t *)malloc((size_t)w * h + 2 * (size_t)(w / 2) * (h / 2));
	n2avc_picture pic;
	n2avc_packet pkt;
	const uint8_t *pl[3];
	uint32_t st[3];
	int x, y, bad = 0;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = (uint32_t)w;
	cfg.height = (uint32_t)h;
	cfg.fps_num = 25;
	cfg.fps_den = 1;
	cfg.qp = 40;
	CHECK(n2avc_open(&cfg, &e) == N2AVC_OK && e, "open for the flat test");
	if (!e || !img) {
		n2avc_close(e);
		free(img);
		return;
	}
	memset(img, 128, (size_t)w * h + 2 * (size_t)(w / 2) * (h / 2));
	memset(&pic, 0, sizeof pic);
	pic.format = N2AVC_FMT_I420;
	pic.plane[0] = img;
	pic.stride[0] = (uint32_t)w;
	pic.plane[1] = img + (size_t)w * h;
	pic.stride[1] = (uint32_t)(w / 2);
	pic.plane[2] = img + (size_t)w * h + (size_t)(w / 2) * (h / 2);
	pic.stride[2] = (uint32_t)(w / 2);
	CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_OK, "encode flat");
	CHECK(pkt.size < 200, "a flat picture is tiny (%zu bytes)", pkt.size);
	CHECK(n2avc_reconstruction(e, pl, st) == N2AVC_OK, "reconstruction of the flat picture");
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			if (pl[0][(size_t)y * st[0] + x] != 128)
				bad++;
	CHECK(bad == 0, "flat luma reconstructed exactly (%d samples differ)", bad);
	n2avc_close(e);
	free(img);
}

/* NV12 input must produce exactly the same packets as I420 input of the same picture. */
static void test_nv12_matches_i420(void)
{
	n2avc_config cfg;
	n2avc_enc *ei = NULL, *en = NULL;
	int w = 64, h = 48, cw = 32, ch = 24;
	uint8_t *i420 = (uint8_t *)malloc((size_t)w * h + 2 * (size_t)cw * ch);
	uint8_t *nv12 = (uint8_t *)malloc((size_t)w * h + 2 * (size_t)cw * ch);
	uint8_t p1[65536], p2[65536];
	size_t n1 = 0, n2 = 0;
	n2avc_picture pic;
	n2avc_packet pkt;
	int x, y;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = (uint32_t)w;
	cfg.height = (uint32_t)h;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 20;
	if (!i420 || !nv12) {
		free(i420);
		free(nv12);
		return;
	}
	n2avc_open(&cfg, &ei);
	n2avc_open(&cfg, &en);
	CHECK(ei && en, "open both encoders");
	if (!ei || !en) {
		n2avc_close(ei);
		n2avc_close(en);
		free(i420);
		free(nv12);
		return;
	}
	make_picture(i420, w, h, 0, 0, w, cw);
	/* Build the NV12 version of the same picture. */
	for (y = 0; y < h; y++)
		memcpy(nv12 + (size_t)y * w, i420 + (size_t)y * w, (size_t)w);
	for (y = 0; y < ch; y++)
		for (x = 0; x < cw; x++) {
			nv12[(size_t)w * h + (size_t)y * w + 2 * x] = i420[(size_t)w * h + (size_t)y * cw + x];
			nv12[(size_t)w * h + (size_t)y * w + 2 * x + 1] = i420[(size_t)w * h + (size_t)cw * ch + (size_t)y * cw + x];
		}

	memset(&pic, 0, sizeof pic);
	pic.format = N2AVC_FMT_I420;
	pic.plane[0] = i420;
	pic.stride[0] = (uint32_t)w;
	pic.plane[1] = i420 + (size_t)w * h;
	pic.stride[1] = (uint32_t)cw;
	pic.plane[2] = i420 + (size_t)w * h + (size_t)cw * ch;
	pic.stride[2] = (uint32_t)cw;
	CHECK(n2avc_encode(ei, &pic, 0, &pkt) == N2AVC_OK, "encode I420");
	if (pkt.size <= sizeof p1) {
		memcpy(p1, pkt.data, pkt.size);
		n1 = pkt.size;
	}

	memset(&pic, 0, sizeof pic);
	pic.format = N2AVC_FMT_NV12;
	pic.plane[0] = nv12;
	pic.stride[0] = (uint32_t)w;
	pic.plane[1] = nv12 + (size_t)w * h;
	pic.stride[1] = (uint32_t)w;
	CHECK(n2avc_encode(en, &pic, 0, &pkt) == N2AVC_OK, "encode NV12");
	if (pkt.size <= sizeof p2) {
		memcpy(p2, pkt.data, pkt.size);
		n2 = pkt.size;
	}
	CHECK(n1 && n1 == n2 && memcmp(p1, p2, n1) == 0, "NV12 and I420 give the same packet");
	n2avc_close(ei);
	n2avc_close(en);
	free(i420);
	free(nv12);
}

/* I_PCM fallback: a noise macroblock whose I_16x16 syntax does not fit the slice buffer must be recoded as
 * I_PCM (clause 7.3.5), which is lossless, so the reconstruction equals the source exactly. */
static void test_ipcm_fallback(void)
{
	n2avc_frame src, rec;
	uint8_t *sb[3] = { NULL, NULL, NULL }, *rb[3] = { NULL, NULL, NULL };
	n2avc_mbinfo mb;
	n2avc_slice s;
	uint8_t buf[512];
	int x, y, bad = 0, stride;
	int ok = 0;

	memset(&src, 0, sizeof src);
	memset(&rec, 0, sizeof rec);
	memset(&s, 0, sizeof s);
	memset(&mb, 0, sizeof mb);
	stride = 16 + 2 * N2AVC_PAD;
	sb[0] = (uint8_t *)calloc((size_t)stride * (16 + 2 * N2AVC_PAD), 1);
	rb[0] = (uint8_t *)calloc((size_t)stride * (16 + 2 * N2AVC_PAD), 1);
	{
		int cs = 8 + N2AVC_PAD;
		sb[1] = (uint8_t *)calloc((size_t)cs * (8 + N2AVC_PAD), 1);
		sb[2] = (uint8_t *)calloc((size_t)cs * (8 + N2AVC_PAD), 1);
		rb[1] = (uint8_t *)calloc((size_t)cs * (8 + N2AVC_PAD), 1);
		rb[2] = (uint8_t *)calloc((size_t)cs * (8 + N2AVC_PAD), 1);
		if (!sb[0] || !rb[0] || !sb[1] || !sb[2] || !rb[1] || !rb[2])
			goto done;
		src.y.p = sb[0] + N2AVC_PAD * stride + N2AVC_PAD;
		src.y.stride = stride;
		src.cb.p = sb[1] + (N2AVC_PAD / 2) * cs + N2AVC_PAD / 2;
		src.cb.stride = cs;
		src.cr.p = sb[2] + (N2AVC_PAD / 2) * cs + N2AVC_PAD / 2;
		src.cr.stride = cs;
		rec.y.p = rb[0] + N2AVC_PAD * stride + N2AVC_PAD;
		rec.y.stride = stride;
		rec.cb.p = rb[1] + (N2AVC_PAD / 2) * cs + N2AVC_PAD / 2;
		rec.cb.stride = cs;
		rec.cr.p = rb[2] + (N2AVC_PAD / 2) * cs + N2AVC_PAD / 2;
		rec.cr.stride = cs;
	}
	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
			src.y.p[y * stride + x] = (uint8_t)(lcg_next() >> 24);
	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++) {
			src.cb.p[y * src.cb.stride + x] = (uint8_t)(lcg_next() >> 24);
			src.cr.p[y * src.cr.stride + x] = (uint8_t)(lcg_next() >> 24);
		}

	s.mb = &mb;
	s.mb_w = 1;
	s.mb_h = 1;
	s.first_row = 0;
	s.end_row = 1;
	s.qp = 10;
	s.qpc = n2avc_qpc(10);
	s.slice_type = 2;
	s.src = &src;
	s.rec = &rec;
	s.skip_run = 0;
	n2bw_init(&s.bw, buf, sizeof buf);
	n2avc_intra_mb(&s, 0, 0);
	CHECK(mb.type == N2MB_IPCM, "a noise macroblock that does not fit 3200 bits is I_PCM (type %d)", mb.type);
	CHECK(!s.bw.overflow, "the I_PCM macroblock fits the buffer");
	/* mb_type ue(25) = 000011010, then pcm_alignment_zero_bit to the next byte boundary. */
	CHECK(s.bw.pos > 380, "an I_PCM macroblock is about 384 bytes (%zu)", s.bw.pos);
	if (mb.type == N2MB_IPCM) {
		for (y = 0; y < 16; y++)
			for (x = 0; x < 16; x++)
				if (rec.y.p[y * rec.y.stride + x] != src.y.p[y * stride + x])
					bad++;
		CHECK(bad == 0, "I_PCM reconstruction is lossless (%d luma samples differ)", bad);
		bad = 0;
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				if (rec.cb.p[y * rec.cb.stride + x] != src.cb.p[y * src.cb.stride + x])
					bad++;
		CHECK(bad == 0, "I_PCM chroma reconstruction is lossless (%d samples differ)", bad);
		/* The bit pattern starts with ue(25). */
		{
			char bits[16];
			int k;
			for (k = 0; k < 9; k++)
				bits[k] = (char)('0' + ((buf[k / 8] >> (7 - (k & 7))) & 1));
			bits[9] = 0;
			CHECK(strcmp(bits, "000011010") == 0, "I_PCM mb_type is ue(25): %s", bits);
		}
		ok = 1;
	}
	(void)ok;

done:
	free(sb[0]);
	free(sb[1]);
	free(sb[2]);
	free(rb[0]);
	free(rb[1]);
	free(rb[2]);
}

int main(void)
{
	test_open_errors();
	test_headers_and_encode();
	test_flat_exact();
	test_nv12_matches_i420();
	test_ipcm_fallback();
	if (failures) {
		printf("N2TEST WP03-api FAIL %d of %d checks\n", failures, checks);
		return 1;
	}
	printf("N2TEST WP03-api PASS %d/%d\n", checks, checks);
	return 0;
}
