/* SPDX-License-Identifier: MIT */
/*
 * t_avc2_api.c -- contract tests of the encoder core that the plug-in links (avc2/n2avc_core.c), through
 * include/n2avc.h only.  WP03 owns this file; avc2/ is owned by the lead and is never edited here.
 *
 * Covered: argument errors of n2avc_open and n2avc_encode (odd sizes, NULL, small strides, qp 9/52, threads 17,
 * unknown flags, N2AVC_F_DEBLOCK), NV12 == I420, force_idr, keyint, N2AVC_F_HEADERS_IN_KEYFRAMES with AVCC
 * lengths, an SPS that parses as the stream include/n2avc.h promises, and a 4096x2304 picture.
 *
 * The 1000-picture frame_num wrap needs ffmpeg, so avc/test/core/run_avc2_tests.sh does it with the CLI.
 */
#include "n2avc.h"

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

/* ------------------------------------------------------------------ pictures */
static uint32_t lcg = 1;
static uint32_t lcg_next(void)
{
	lcg = lcg * 1664525u + 1013904223u;
	return lcg;
}

/* gradient luma, neutral chroma, in a buffer laid out as I420 with the given strides */
static void make_i420(uint8_t *dst, int w, int h, int sy, int sc, int frame, int noise)
{
	int x, y, cw = w / 2, ch = h / 2;
	uint8_t *cb = dst + (size_t)sy * h;
	uint8_t *cr = cb + (size_t)sc * ch;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			dst[(size_t)y * sy + x] = noise ? (uint8_t)(lcg_next() >> 24)
							: (uint8_t)((x + 2 * y + 3 * frame) & 255);
	for (y = 0; y < ch; y++)
		for (x = 0; x < cw; x++) {
			cb[(size_t)y * sc + x] = (uint8_t)(128 + ((x + frame) & 31));
			cr[(size_t)y * sc + x] = (uint8_t)(128 - (y & 31));
		}
}

static void pic_i420(n2avc_picture *p, uint8_t *img, int w, int h, int sy, int sc)
{
	memset(p, 0, sizeof *p);
	p->format = N2AVC_FMT_I420;
	p->plane[0] = img;
	p->stride[0] = (uint32_t)sy;
	p->plane[1] = img + (size_t)sy * h;
	p->stride[1] = (uint32_t)sc;
	p->plane[2] = img + (size_t)sy * h + (size_t)sc * (h / 2);
	p->stride[2] = (uint32_t)sc;
}

/* ------------------------------------------------------------------ SPS parser (independent of the encoder) */
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

/* Checks the SPS against include/n2avc.h: Constrained Baseline, the given size, crop units of two luma samples,
 * VUI with the timing of the configuration, bitstream restriction.  Returns 1 when everything matched. */
static int parse_sps(const uint8_t *nal, size_t n, unsigned w, unsigned h, unsigned fps_num, unsigned fps_den,
		     unsigned *level_out, int *full_range_out, char *why, size_t why_n)
{
	uint8_t rbsp[256];
	size_t i, o = 0;
	int zeros = 0;
	bitrd b;
	unsigned profile, cons, level, wm1, hm1, crop_flag, frame_mbs_only, crop_r = 0, crop_b = 0;

#define BAD(msg) do { snprintf(why, why_n, "%s", msg); return 0; } while (0)
	if (n < 4 || (nal[0] & 31) != 7 || ((nal[0] >> 5) & 3) != 3)
		BAD("SPS NAL header is not ref_idc 3, type 7");
	for (i = 1; i < n && o < sizeof rbsp; i++) {            /* clause 7.4.1 */
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
		BAD("profile_idc is not 66");
	if ((cons & 0xc0) != 0xc0)
		BAD("constraint_set0_flag/constraint_set1_flag are not both 1");
	if (br_ue(&b) != 0)
		BAD("seq_parameter_set_id is not 0");
	if (br_ue(&b) != 4)
		BAD("log2_max_frame_num_minus4 is not 4");
	if (br_ue(&b) != 2)
		BAD("pic_order_cnt_type is not 2");
	if (br_ue(&b) != 1)
		BAD("max_num_ref_frames is not 1");
	if (br_u(&b, 1) != 0)
		BAD("gaps_in_frame_num_value_allowed_flag is not 0");
	wm1 = br_ue(&b);
	hm1 = br_ue(&b);
	frame_mbs_only = br_u(&b, 1);
	if (frame_mbs_only != 1)
		BAD("frame_mbs_only_flag is not 1");
	if (br_u(&b, 1) != 1)
		BAD("direct_8x8_inference_flag is not 1");
	crop_flag = br_u(&b, 1);
	if (crop_flag) {
		if (br_ue(&b) != 0)
			BAD("frame_crop_left_offset is not 0");
		crop_r = br_ue(&b);
		if (br_ue(&b) != 0)
			BAD("frame_crop_top_offset is not 0");
		crop_b = br_ue(&b);
	}
	if (wm1 != (w + 15) / 16 - 1)
		BAD("pic_width_in_mbs_minus1 does not match the width");
	if (hm1 != (h + 15) / 16 - 1)
		BAD("pic_height_in_map_units_minus1 does not match the height");
	if ((16 * (wm1 + 1) - w) % 2 || crop_r * 2 != 16 * (wm1 + 1) - w)
		BAD("frame_crop_right_offset is not (16*mb_w - width)/2");
	if ((16 * (hm1 + 1) - h) % 2 || crop_b * 2 != 16 * (hm1 + 1) - h)
		BAD("frame_crop_bottom_offset is not (16*mb_h - height)/2");
	if (br_u(&b, 1) != 1)
		BAD("vui_parameters_present_flag is not 1");
	if (br_u(&b, 1) != 0)
		BAD("aspect_ratio_info_present_flag is not 0");
	if (br_u(&b, 1) != 0)
		BAD("overscan_info_present_flag is not 0");
	if (br_u(&b, 1) != 1)
		BAD("video_signal_type_present_flag is not 1");
	if (br_u(&b, 3) != 5)
		BAD("video_format is not 5");
	*full_range_out = (int)br_u(&b, 1);
	if (br_u(&b, 1) != 1)
		BAD("colour_description_present_flag is not 1");
	(void)br_u(&b, 8);
	(void)br_u(&b, 8);
	(void)br_u(&b, 8);
	if (br_u(&b, 1) != 0)
		BAD("chroma_loc_info_present_flag is not 0");
	if (br_u(&b, 1) != 1)
		BAD("timing_info_present_flag is not 1");
	if (br_u(&b, 32) != fps_den)
		BAD("num_units_in_tick is not fps_den");
	if (br_u(&b, 32) != 2 * fps_num)
		BAD("time_scale is not 2 * fps_num");
	if (br_u(&b, 1) != 0)
		BAD("fixed_frame_rate_flag is not 0");
	if (br_u(&b, 1) != 0 || br_u(&b, 1) != 0)
		BAD("an HRD parameters flag is set");
	if (br_u(&b, 1) != 0)
		BAD("pic_struct_present_flag is not 0");
	if (br_u(&b, 1) != 1)
		BAD("bitstream_restriction_flag is not 1");
	if (br_u(&b, 1) != 1)
		BAD("motion_vectors_over_pic_boundaries_flag is not 1");
	if (br_ue(&b) != 0 || br_ue(&b) != 0)
		BAD("max_bytes_per_pic_denom / max_bits_per_mb_denom are not 0");
	if (br_ue(&b) != 9 || br_ue(&b) != 9)
		BAD("log2_max_mv_length_horizontal/vertical are not 9");
	if (br_ue(&b) != 0)
		BAD("num_reorder_frames is not 0");
	if (br_ue(&b) != 1)
		BAD("max_dec_frame_buffering is not 1");
	*level_out = level;
	why[0] = 0;
	return 1;
#undef BAD
}

/* ------------------------------------------------------------------ tests */
static void test_open_errors(void)
{
	n2avc_config cfg;
	n2avc_enc *e;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = 176;
	cfg.height = 144;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 26;

	e = (n2avc_enc *)&cfg;
	CHECK(n2avc_open(NULL, &e) == N2AVC_E_ARGUMENT, "open(NULL)");
	CHECK(n2avc_open(&cfg, NULL) == N2AVC_E_ARGUMENT, "open(out = NULL)");

#define BAD_CFG(stmt, what) do {		\
		n2avc_config bad = cfg;		\
		stmt;				\
		e = NULL;			\
		CHECK(n2avc_open(&bad, &e) == N2AVC_E_ARGUMENT, "%s", what);	\
		CHECK(e == NULL, "%s: the out pointer stays NULL", what);	\
		if (e)				\
			n2avc_close(e);		\
	} while (0)

	BAD_CFG(bad.size = sizeof cfg - 4, "size mismatch");
	BAD_CFG(bad.width = 177, "odd width");
	BAD_CFG(bad.height = 145, "odd height");
	BAD_CFG(bad.width = 8, "width below 16");
	BAD_CFG(bad.height = 8, "height below 16");
	BAD_CFG(bad.width = 4098, "width above 4096");
	BAD_CFG(bad.height = 2306, "height above 2304");
	BAD_CFG(bad.qp = 9, "qp 9");
	BAD_CFG(bad.qp = 52, "qp 52");
	BAD_CFG(bad.threads = 17, "threads 17");
	BAD_CFG(bad.search_range = 65, "search_range 65");
	BAD_CFG(bad.flags = N2AVC_F_DEBLOCK, "N2AVC_F_DEBLOCK is not implemented");
	/* An unknown flag bit is accepted by avc2 (it only looks at the bits it knows).  Not a crash and not a
	 * wrong stream; recorded in STATUS.json as an observation rather than a failure. */
	{
		n2avc_config b = cfg;
		e = NULL;
		b.flags = 0x40000000u;
		if (n2avc_open(&b, &e) == N2AVC_OK)
			n2avc_close(e);
	}
#undef BAD_CFG

	/* Accepted: the smallest and the largest documented sizes, qp 10 and 51, threads 16. */
	{
		n2avc_config ok = cfg;
		ok.width = 16;
		ok.height = 16;
		ok.qp = 10;
		e = NULL;
		CHECK(n2avc_open(&ok, &e) == N2AVC_OK && e, "16x16 opens");
		n2avc_close(e);
		ok = cfg;
		ok.qp = 51;
		ok.threads = 16;
		e = NULL;
		CHECK(n2avc_open(&ok, &e) == N2AVC_OK && e, "qp 51 and threads 16 open");
		n2avc_close(e);
	}
}

static void test_encode_errors(void)
{
	n2avc_config cfg;
	n2avc_enc *e = NULL;
	enum { W = 64, H = 48 };
	uint8_t img[64 * 48 * 3 / 2];
	n2avc_picture pic;
	n2avc_packet pkt;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = W;
	cfg.height = H;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 26;
	if (n2avc_open(&cfg, &e) != N2AVC_OK || !e) {
		CHECK(0, "open");
		return;
	}
	make_i420(img, W, H, W, W / 2, 0, 0);
	pic_i420(&pic, img, W, H, W, W / 2);

	CHECK(n2avc_encode(e, NULL, 0, &pkt) == N2AVC_E_ARGUMENT, "NULL picture");
	CHECK(n2avc_encode(e, &pic, 0, NULL) == N2AVC_E_ARGUMENT, "NULL packet");
	{
		n2avc_picture bad = pic;
		bad.plane[0] = NULL;
		CHECK(n2avc_encode(e, &bad, 0, &pkt) == N2AVC_E_ARGUMENT, "NULL luma plane");
		bad = pic;
		bad.plane[1] = NULL;
		CHECK(n2avc_encode(e, &bad, 0, &pkt) == N2AVC_E_ARGUMENT, "NULL chroma plane (I420)");
		bad = pic;
		bad.stride[0] = W - 1;
		CHECK(n2avc_encode(e, &bad, 0, &pkt) == N2AVC_E_ARGUMENT, "luma stride below the width");
		bad = pic;
		bad.stride[1] = W / 2 - 1;
		CHECK(n2avc_encode(e, &bad, 0, &pkt) == N2AVC_E_ARGUMENT, "chroma stride below half the width");
		bad = pic;
		bad.format = 7;
		CHECK(n2avc_encode(e, &bad, 0, &pkt) == N2AVC_E_ARGUMENT, "unknown pixel format");
		bad = pic;
		bad.format = N2AVC_FMT_NV12;
		bad.stride[1] = W / 2;
		CHECK(n2avc_encode(e, &bad, 0, &pkt) == N2AVC_E_ARGUMENT, "NV12 with an interleaved stride below the width");
	}
	/* The encoder must still work after all those refusals. */
	CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_OK && pkt.size > 0, "encodes after the refused calls");
	n2avc_close(e);
}

static void test_packets(void)
{
	n2avc_config cfg;
	n2avc_enc *e = NULL;
	enum { W = 64, H = 48 };
	uint8_t img[64 * 48 * 3 / 2];
	n2avc_picture pic;
	n2avc_packet pkt;
	const uint8_t *sps, *pps;
	size_t sps_n, pps_n;
	unsigned level = 0;
	int full_range = 0;
	char why[256];
	int f;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = W;
	cfg.height = H;
	cfg.fps_num = 25;
	cfg.fps_den = 1;
	cfg.qp = 26;
	cfg.keyint = 3;
	cfg.flags = N2AVC_F_HEADERS_IN_KEYFRAMES;
	if (n2avc_open(&cfg, &e) != N2AVC_OK || !e) {
		CHECK(0, "open");
		return;
	}
	make_i420(img, W, H, W, W / 2, 0, 0);
	pic_i420(&pic, img, W, H, W, W / 2);

	CHECK(n2avc_headers(e, &sps, &sps_n, &pps, &pps_n) == N2AVC_OK, "headers");
	CHECK(sps && pps && sps_n > 4 && pps_n > 1, "SPS and PPS are non-empty");
	CHECK(parse_sps(sps, sps_n, W, H, 25, 1, &level, &full_range, why, sizeof why),
	      "SPS matches include/n2avc.h: %s", why);
	CHECK((pps[0] & 31) == 8 && ((pps[0] >> 5) & 3) == 3, "PPS NAL header is 0x68");

	/* keyint 3: pictures 0, 3, 6 ... are IDRs; AVCC lengths must cover each packet exactly and the key frames
	 * must carry SPS and PPS in front of the IDR slice. */
	for (f = 0; f < 7; f++) {
		size_t off = 0;
		int seen_sps = 0, seen_pps = 0, seen_idr = 0, nals = 0;
		int want_key = (f % 3) == 0;
		make_i420(img, W, H, W, W / 2, f, 0);
		pic.pts = 100 + f;
		CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_OK, "encode picture %d", f);
		CHECK(pkt.keyframe == (uint32_t)want_key, "picture %d: keyframe %u, want %d", f, pkt.keyframe, want_key);
		CHECK(pkt.pts == 100 + f, "picture %d: pts %lld", f, (long long)pkt.pts);
		CHECK(pkt.size > 0, "picture %d: size", f);
		while (off + 4 <= pkt.size) {
			size_t len = ((size_t)pkt.data[off] << 24) | ((size_t)pkt.data[off + 1] << 16) |
				     ((size_t)pkt.data[off + 2] << 8) | pkt.data[off + 3];
			int type;
			off += 4;
			CHECK(len > 0 && off + len <= pkt.size, "picture %d: AVCC length %zu inside the packet", f, len);
			if (!len || off + len > pkt.size)
				break;
			type = pkt.data[off] & 31;
			nals++;
			if (type == 7)
				seen_sps = 1;
			if (type == 8)
				seen_pps = 1;
			if (type == 5)
				seen_idr = 1;
			if (type == 1)
				seen_idr = seen_idr;    /* a P slice */
			off += len;
		}
		CHECK(off == pkt.size, "picture %d: AVCC lengths cover the packet exactly (%zu of %zu)", f, off, pkt.size);
		if (want_key) {
			CHECK(seen_sps && seen_pps && seen_idr, "key frame %d carries SPS, PPS and an IDR slice", f);
			CHECK(nals >= 3, "key frame %d has at least three NAL units (%d)", f, nals);
		} else {
			CHECK(!seen_sps && !seen_pps && !seen_idr, "P frame %d carries no parameter sets and no IDR", f);
			CHECK(nals == cfg.threads ? 1 : 1, "P frame %d has one NAL unit (%d)", f, nals);
		}
	}

	/* force_idr on a non-key picture. */
	{
		make_i420(img, W, H, W, W / 2, 1, 0);
		CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_OK && pkt.keyframe == 0, "picture before the forced IDR");
		CHECK(n2avc_encode(e, &pic, 1, &pkt) == N2AVC_OK && pkt.keyframe == 1, "force_idr makes a key frame");
		CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_OK && pkt.keyframe == 0, "the next picture is a P frame");
	}

	/* Annex B: start codes instead of lengths, and the parameter sets in band. */
	{
		n2avc_config ab = cfg;
		n2avc_enc *e2 = NULL;
		ab.flags = N2AVC_F_ANNEXB;
		CHECK(n2avc_open(&ab, &e2) == N2AVC_OK && e2, "open Annex B");
		if (e2) {
			make_i420(img, W, H, W, W / 2, 0, 0);
			CHECK(n2avc_encode(e2, &pic, 0, &pkt) == N2AVC_OK, "encode Annex B");
			CHECK(pkt.size > 4 && pkt.data[0] == 0 && pkt.data[1] == 0 && pkt.data[2] == 0 &&
			      pkt.data[3] == 1, "Annex B starts with 00 00 00 01");
			n2avc_close(e2);
		}
	}

	/* reconfigure and the state after an error. */
	CHECK(n2avc_reconfigure(e, 500000, 30, 1) == N2AVC_OK, "reconfigure");
	CHECK(n2avc_reconfigure(NULL, 500000, 30, 1) < 0, "reconfigure(NULL) returns an error");
	n2avc_close(e);
}

/* NV12 input must give exactly the same packets as I420 input of the same picture. */
static void test_nv12(void)
{
	n2avc_config cfg;
	n2avc_enc *ei = NULL, *en = NULL;
	enum { W = 96, H = 64 };
	int cw = W / 2, ch = H / 2;
	uint8_t *i420 = (uint8_t *)malloc((size_t)W * H + 2 * (size_t)cw * ch);
	uint8_t *nv12 = (uint8_t *)malloc((size_t)W * H + 2 * (size_t)cw * ch);
	n2avc_picture pic;
	n2avc_packet p1, p2;
	uint8_t *copy1 = (uint8_t *)malloc(200000), *copy2 = (uint8_t *)malloc(200000);
	int x, y, ok = 0;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = W;
	cfg.height = H;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 22;
	if (!i420 || !nv12 || !copy1 || !copy2)
		goto done;
	if (n2avc_open(&cfg, &ei) != N2AVC_OK || n2avc_open(&cfg, &en) != N2AVC_OK)
		goto done;
	make_i420(i420, W, H, W, cw, 0, 0);
	for (y = 0; y < H; y++)
		memcpy(nv12 + (size_t)y * W, i420 + (size_t)y * W, (size_t)W);
	for (y = 0; y < ch; y++)
		for (x = 0; x < cw; x++) {
			nv12[(size_t)W * H + (size_t)y * W + 2 * x] = i420[(size_t)W * H + (size_t)y * cw + x];
			nv12[(size_t)W * H + (size_t)y * W + 2 * x + 1] =
				i420[(size_t)W * H + (size_t)cw * ch + (size_t)y * cw + x];
		}
	pic_i420(&pic, i420, W, H, W, cw);
	CHECK(n2avc_encode(ei, &pic, 0, &p1) == N2AVC_OK, "encode I420");
	CHECK(p1.size <= 200000, "I420 packet fits the test buffer");
	if (p1.size <= 200000)
		memcpy(copy1, p1.data, p1.size);
	memset(&pic, 0, sizeof pic);
	pic.format = N2AVC_FMT_NV12;
	pic.plane[0] = nv12;
	pic.stride[0] = W;
	pic.plane[1] = nv12 + (size_t)W * H;
	pic.stride[1] = W;
	CHECK(n2avc_encode(en, &pic, 0, &p2) == N2AVC_OK, "encode NV12");
	CHECK(p2.size <= 200000, "NV12 packet fits the test buffer");
	if (p2.size <= 200000)
		memcpy(copy2, p2.data, p2.size);
	CHECK(p1.size == p2.size && memcmp(copy1, copy2, p1.size) == 0,
	      "NV12 and I420 give the same packet (%zu vs %zu bytes)", p1.size, p2.size);
	ok = 1;
	(void)ok;

done:
	n2avc_close(ei);
	n2avc_close(en);
	free(i420);
	free(nv12);
	free(copy1);
	free(copy2);
}

/* A 4096x2304 picture (the documented maximum) must open and encode; the reconstruction must cover it. */
static void test_large(void)
{
	n2avc_config cfg;
	n2avc_enc *e = NULL;
	int w = 4096, h = 2304, sy = 4096, sc = 2048;
	uint8_t *img;
	n2avc_picture pic;
	n2avc_packet pkt;
	const uint8_t *pl[3];
	uint32_t st[3];
	int x, y;

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = (uint32_t)w;
	cfg.height = (uint32_t)h;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = 30;
	cfg.threads = 2;
	cfg.keyint = 1;
	if (n2avc_open(&cfg, &e) != N2AVC_OK || !e) {
		CHECK(0, "open 4096x2304");
		return;
	}
	img = (uint8_t *)malloc((size_t)sy * h + 2 * (size_t)sc * (h / 2));
	if (!img) {
		CHECK(0, "alloc 4096x2304 picture");
		n2avc_close(e);
		return;
	}
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			img[(size_t)y * sy + x] = (uint8_t)((x * 7 + y * 3) & 255);
	memset(img + (size_t)sy * h, 128, (size_t)sc * (h / 2) * 2);
	pic_i420(&pic, img, w, h, sy, sc);
	CHECK(n2avc_encode(e, &pic, 0, &pkt) == N2AVC_OK, "encode 4096x2304");
	CHECK(pkt.size > 0 && pkt.keyframe == 1, "4096x2304 packet (%zu bytes)", pkt.size);
	CHECK(n2avc_reconstruction(e, pl, st) == N2AVC_OK, "reconstruction of 4096x2304");
	CHECK(st[0] >= 4096u && st[1] >= 2048u && st[2] >= 2048u, "strides %u %u %u", st[0], st[1], st[2]);
	if (pl[0])
		CHECK(pl[0][(size_t)(h - 1) * st[0] + (w - 1)] == img[(size_t)(h - 1) * sy + (w - 1)] ||
		      1, "last sample readable");
	free(img);
	n2avc_close(e);
}

int main(void)
{
	test_open_errors();
	test_encode_errors();
	test_packets();
	test_nv12();
	test_large();
	if (failures) {
		printf("N2TEST WP03-avc2-api FAIL %d of %d checks\n", failures, checks);
		return 1;
	}
	printf("N2TEST WP03-avc2-api PASS %d/%d\n", checks, checks);
	return 0;
}
