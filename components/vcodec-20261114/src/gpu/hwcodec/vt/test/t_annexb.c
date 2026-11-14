/* SPDX-License-Identifier: MIT */
/*
 * t_annexb.c -- host tests of vt/annexb.c.
 *
 * Part 1 works from literals written out of ITU-T H.264/H.265 and ISO/IEC 14496-15: hand-built Annex B buffers with the
 * exact expected NAL list and conversion, hand-built SPS/PPS/VPS bytes with the exact expected avcC/hvcC records.
 * Part 2 takes the vectors mp4_extract.py produced from ffmpeg's own MP4 muxer: the avcC/hvcC we build from the stream's
 * parameter sets must equal ffmpeg's extradata byte for byte, and every access unit we convert must equal the length-
 * prefixed sample ffmpeg wrote for it.  No expected value comes from annexb.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2hwenc_annexb.h"
#include "t_support.h"

/* ------------------------------------------------------------------ literals */

static const uint8_t avc_au[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x29, 0xe0,
	0x00, 0x00, 0x00, 0x01, 0x68, 0xaa, 0xbb,
	0x00, 0x00, 0x00, 0x01, 0x65, 0x11, 0x22, 0x33,
};

static const uint8_t avc_avcc[] = {
	0x00, 0x00, 0x00, 0x04, 0x65, 0x11, 0x22, 0x33,
};

static void test_scan_avc(void)
{
	struct n2hwenc_nal nals[8];
	uint32_t n = 0;

	CHECK_EQ(n2hwenc_annexb_scan(avc_au, sizeof(avc_au), N2HWENC_AVC, nals, 8, &n), 0);
	CHECK_EQ(n, 3);
	CHECK_EQ(nals[0].type, 7);
	CHECK_EQ(nals[0].key, 0);
	CHECK_EQ(nals[0].param, 1);
	CHECK_EQ(nals[0].offset, 4);
	CHECK_EQ(nals[0].size, 5);
	CHECK_EQ(nals[1].type, 8);
	CHECK_EQ(nals[1].param, 1);
	CHECK_EQ(nals[1].offset, 13);
	CHECK_EQ(nals[1].size, 3);
	CHECK_EQ(nals[2].type, 5);
	CHECK_EQ(nals[2].key, 1);
	CHECK_EQ(nals[2].param, 0);
	CHECK_EQ(nals[2].offset, 20);
	CHECK_EQ(nals[2].size, 4);

	/* three-byte start codes, and a NAL whose trailing zeros belong to a four-byte start code */
	{
		static const uint8_t mixed[] = {
			0x00, 0x00, 0x01, 0x41, 0xaa, 0x00, 0x00, 0x00, 0x01, 0x42, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x01,
			0x43, 0xcc, 0x00, 0x00,
		};
		n = 0;
		CHECK_EQ(n2hwenc_annexb_scan(mixed, sizeof(mixed), N2HWENC_AVC, nals, 8, &n), 0);
		CHECK_EQ(n, 3);
		CHECK_EQ(nals[0].type, 1);
		CHECK_EQ(nals[0].offset, 3);
		CHECK_EQ(nals[0].size, 2);              /* 0xaa; the zeros before the start code are not part of it */
		CHECK_EQ(nals[1].type, 2);
		CHECK_EQ(nals[1].size, 2);
		CHECK_EQ(nals[2].type, 3);
		CHECK_EQ(nals[2].size, 2);
	}
	/* malformed inputs */
	n = 0;
	CHECK_EQ(n2hwenc_annexb_scan((const uint8_t *)"\x00\x00\x01", 3, N2HWENC_AVC, nals, 8, &n), -1);
	CHECK_EQ(n2hwenc_annexb_scan((const uint8_t *)"\x01\x02\x03\x04", 4, N2HWENC_AVC, nals, 8, &n), -1);
	CHECK_EQ(n2hwenc_annexb_scan(NULL, 4, N2HWENC_AVC, nals, 8, &n), -1);
	CHECK_EQ(n2hwenc_annexb_scan(avc_au, sizeof(avc_au), N2HWENC_AVC, nals, 2, &n), -1);   /* capacity */
}

static void test_scan_hevc(void)
{
	static const uint8_t au[] = {
		0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c,       /* VPS  (32) */
		0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01,       /* SPS  (33) */
		0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc0,       /* PPS  (34) */
		0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf,       /* IDR_W_RADL (19) */
		0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0xd0,       /* TRAIL_R (1) */
	};
	struct n2hwenc_nal nals[8];
	uint32_t n = 0;

	CHECK_EQ(n2hwenc_annexb_scan(au, sizeof(au), N2HWENC_HEVC, nals, 8, &n), 0);
	CHECK_EQ(n, 5);
	CHECK_EQ(nals[0].type, 32);
	CHECK_EQ(nals[0].param, 1);
	CHECK_EQ(nals[1].type, 33);
	CHECK_EQ(nals[1].param, 1);
	CHECK_EQ(nals[2].type, 34);
	CHECK_EQ(nals[2].param, 1);
	CHECK_EQ(nals[3].type, 19);
	CHECK_EQ(nals[3].key, 1);
	CHECK_EQ(nals[4].type, 1);
	CHECK_EQ(nals[4].key, 0);
	CHECK_EQ(nals[4].param, 0);
}

static void test_convert_avc(void)
{
	uint8_t buf[128];
	uint32_t out = 0, nals = 0;
	int r;

	/* Keep everything: the start codes become four-byte lengths. */
	memcpy(buf, avc_au, sizeof(avc_au));
	r = n2hwenc_annexb_to_avcc(buf, sizeof(avc_au), sizeof(buf), N2HWENC_AVC, 0, &out, &nals);
	CHECK_EQ(r, 0);
	CHECK_EQ(nals, 3);
	CHECK_EQ(out, 4 + 5 + 4 + 3 + 4 + 4);
	{
		static const uint8_t want[] = {
			0x00, 0x00, 0x00, 0x05, 0x67, 0x64, 0x00, 0x29, 0xe0,
			0x00, 0x00, 0x00, 0x03, 0x68, 0xaa, 0xbb,
			0x00, 0x00, 0x00, 0x04, 0x65, 0x11, 0x22, 0x33,
		};
		CHECK_EQ(out, sizeof(want));
		CHECK_EQ(memcmp(buf, want, out < sizeof(want) ? out : sizeof(want)), 0);
	}

	/* Strip the parameter sets: the sample is the IDR alone.  This is what the encoder object emits. */
	memcpy(buf, avc_au, sizeof(avc_au));
	r = n2hwenc_annexb_to_avcc(buf, sizeof(avc_au), sizeof(buf), N2HWENC_AVC, 1, &out, &nals);
	CHECK_EQ(r, 0);
	CHECK_EQ(nals, 1);
	CHECK_EQ(out, 8);
	CHECK_EQ(memcmp(buf, avc_avcc, 8), 0);

	/* Capacity: the exact size fits, one byte less is refused and leaves the buffer alone. */
	{
		uint8_t tight[32];
		memcpy(tight, avc_au, sizeof(avc_au));
		CHECK_EQ(n2hwenc_annexb_to_avcc(tight, sizeof(avc_au), 8, N2HWENC_AVC, 1, &out, &nals), 0);
		CHECK_EQ(out, 8);
		memcpy(tight, avc_au, sizeof(avc_au));
		CHECK_EQ(n2hwenc_annexb_to_avcc(tight, sizeof(avc_au), 7, N2HWENC_AVC, 1, &out, &nals), -2);
		CHECK_EQ(memcmp(tight, avc_au, sizeof(avc_au)), 0);
	}

	/* n2hwenc_annexb_expanded is a real upper bound. */
	CHECK(n2hwenc_annexb_expanded(sizeof(avc_au)) >= sizeof(avc_au));
	CHECK(n2hwenc_annexb_expanded(20) >= 20 + 3);
	CHECK(n2hwenc_annexb_expanded(0) >= 4);
	CHECK(n2hwenc_annexb_expanded(0xffffffffu) == 0xffffffffu);
}

static void test_convert_many_nals(void)
{
	/* 257 one-byte NAL units exceed N2HWENC_NAL_MAX: scan refuses rather than truncating. */
	uint8_t buf[4 * 258];
	uint32_t i, out = 0, n = 0;

	for (i = 0; i < 257; i++) {
		buf[4 * i] = 0;
		buf[4 * i + 1] = 0;
		buf[4 * i + 2] = 1;
		buf[4 * i + 3] = (uint8_t)(0x41);
	}
	CHECK_EQ(n2hwenc_annexb_scan(buf, 4 * 257, N2HWENC_AVC, NULL, 0, &n), -1);
	{
		struct n2hwenc_nal nals[N2HWENC_NAL_MAX];
		CHECK_EQ(n2hwenc_annexb_scan(buf, 4 * 257, N2HWENC_AVC, nals, N2HWENC_NAL_MAX, &n), -1);
		CHECK_EQ(n2hwenc_annexb_to_avcc(buf, 4 * 257, sizeof(buf), N2HWENC_AVC, 0, &out, &n), -1);
	}
	/* and 256 is fine */
	CHECK_EQ(n2hwenc_annexb_scan(buf, 4 * 256, N2HWENC_AVC, (struct n2hwenc_nal[256]){ 0 }, 256, &n), 0);
	CHECK_EQ(n, 256);
}

static void test_avcc_literal(void)
{
	struct n2hwenc_ps sets[2];
	static const uint8_t sps_high[] = { 0x67, 0x64, 0x00, 0x29, 0xac };   /* profile 100, level 41, chroma 1, 8 bit */
	static const uint8_t sps_base[] = { 0x67, 0x42, 0xc0, 0x1e, 0xd9 };   /* profile 66, level 30 */
	static const uint8_t pps[] = { 0x68, 0xeb, 0xe3, 0xcb };
	uint8_t out[64];
	int n;

	sets[0].type = 7; sets[0].data = sps_high; sets[0].bytes = sizeof(sps_high);
	sets[1].type = 8; sets[1].data = pps; sets[1].bytes = sizeof(pps);
	n = n2hwenc_avcc_build(sets, 2, out, sizeof(out));
	{
		static const uint8_t want[] = {
			0x01, 0x64, 0x00, 0x29, 0xff, 0xe1, 0x00, 0x05, 0x67, 0x64, 0x00, 0x29, 0xac,
			0x01, 0x00, 0x04, 0x68, 0xeb, 0xe3, 0xcb,
			0xfd, 0xf8, 0xf8, 0x00,
		};
		CHECK_EQ(n, (int)sizeof(want));
		CHECK_EQ(memcmp(out, want, sizeof(want)), 0);
	}

	sets[0].data = sps_base;
	n = n2hwenc_avcc_build(sets, 2, out, sizeof(out));
	{
		static const uint8_t want[] = {
			0x01, 0x42, 0xc0, 0x1e, 0xff, 0xe1, 0x00, 0x05, 0x67, 0x42, 0xc0, 0x1e, 0xd9,
			0x01, 0x00, 0x04, 0x68, 0xeb, 0xe3, 0xcb,
		};
		CHECK_EQ(n, (int)sizeof(want));
		CHECK_EQ(memcmp(out, want, sizeof(want)), 0);
	}

	/* Incomplete sets and too small an output are refused. */
	sets[0].type = 8;
	CHECK_EQ(n2hwenc_avcc_build(sets, 1, out, sizeof(out)), -1);
	sets[0].type = 7;
	CHECK_EQ(n2hwenc_avcc_build(sets, 1, out, sizeof(out)), -1);
	sets[0].type = 7; sets[1].type = 8;
	CHECK_EQ(n2hwenc_avcc_build(sets, 2, out, 10), -1);
	/* An SPS with no profile byte cannot be parsed. */
	sets[0].data = (const uint8_t *)"\x67\x64";
	sets[0].bytes = 2;
	CHECK_EQ(n2hwenc_avcc_build(sets, 2, out, sizeof(out)), -1);
}

/* ------------------------------------------------------------------ bit writer for the hvcC literal SPS */

struct bit_writer {
	uint8_t  buf[128];
	uint32_t bits;
};

static void bw_put(struct bit_writer *bw, uint32_t value, int count)
{
	int i;

	for (i = count - 1; i >= 0; i--) {
		uint32_t bit = (value >> i) & 1u;
		if (bit)
			bw->buf[bw->bits >> 3] |= (uint8_t)(1u << (7u - (bw->bits & 7u)));
		bw->bits++;
	}
}

static void bw_ue(struct bit_writer *bw, uint32_t value)
{
	uint32_t v = value + 1u, bits = 0, t;

	for (t = v; t > 1; t >>= 1)
		bits++;
	bw_put(bw, 0, (int)bits);
	bw_put(bw, v, (int)bits + 1);
}

static void bw_trailing(struct bit_writer *bw)
{
	bw_put(bw, 1, 1);
	while ((bw->bits & 7u) != 0)
		bw_put(bw, 0, 1);
}

static uint32_t make_hevc_sps(uint8_t *out, uint32_t capacity)
{
	struct bit_writer bw;

	memset(&bw, 0, sizeof(bw));
	bw_put(&bw, 0, 4);                  /* sps_video_parameter_set_id */
	bw_put(&bw, 0, 3);                  /* sps_max_sub_layers_minus1 */
	bw_put(&bw, 1, 1);                  /* sps_temporal_id_nesting_flag */
	bw_put(&bw, 0, 2);                  /* general_profile_space */
	bw_put(&bw, 0, 1);                  /* general_tier_flag */
	bw_put(&bw, 1, 5);                  /* general_profile_idc (Main) */
	bw_put(&bw, 0x60000000u, 32);       /* general_profile_compatibility_flags */
	bw_put(&bw, 0x9000u, 16);           /* general_constraint_indicator_flags, high 16 bits */
	bw_put(&bw, 0x00000000u, 32);       /* low 32 bits */
	bw_put(&bw, 60, 8);                 /* general_level_idc (2.0) */
	bw_ue(&bw, 0);                      /* sps_seq_parameter_set_id */
	bw_ue(&bw, 1);                      /* chroma_format_idc: 4:2:0 */
	bw_ue(&bw, 16);                     /* width */
	bw_ue(&bw, 16);                     /* height */
	bw_put(&bw, 0, 1);                  /* conformance_window_flag */
	bw_ue(&bw, 0);                      /* bit_depth_luma_minus8 */
	bw_ue(&bw, 0);                      /* bit_depth_chroma_minus8 */
	bw_trailing(&bw);

	if (capacity < 2 + (bw.bits + 7) / 8)
		return 0;
	out[0] = 0x42;                      /* nal_unit_type 33 */
	out[1] = 0x01;
	memcpy(out + 2, bw.buf, (bw.bits + 7) / 8);
	return 2 + (bw.bits + 7) / 8;
}

static void test_hvcc_literal(void)
{
	static const uint8_t vps[] = { 0x40, 0x01, 0x0c, 0x01, 0xff };
	static const uint8_t pps[] = { 0x44, 0x01, 0xc0, 0x73 };
	uint8_t sps[64];
	uint32_t sps_bytes;
	struct n2hwenc_ps sets[3];
	uint8_t out[256];
	int n;

	sps_bytes = make_hevc_sps(sps, sizeof(sps));
	CHECK(sps_bytes > 0);
	sets[0].type = 32; sets[0].data = vps; sets[0].bytes = sizeof(vps);
	sets[1].type = 33; sets[1].data = sps; sets[1].bytes = sps_bytes;
	sets[2].type = 34; sets[2].data = pps; sets[2].bytes = sizeof(pps);
	n = n2hwenc_hvcc_build(sets, 3, out, sizeof(out));

	{
		uint8_t want[256];
		uint32_t at = 0;

		want[at++] = 0x01;                          /* configurationVersion */
		want[at++] = 0x01;                          /* profile_space 0, tier 0, profile_idc 1 */
		want[at++] = 0x60; want[at++] = 0x00; want[at++] = 0x00; want[at++] = 0x00;
		want[at++] = 0x90; want[at++] = 0x00; want[at++] = 0x00;
		want[at++] = 0x00; want[at++] = 0x00; want[at++] = 0x00;
		want[at++] = 60;
		want[at++] = 0xf0; want[at++] = 0x00;
		want[at++] = 0xfc;
		want[at++] = 0xfd;
		want[at++] = 0xf8;
		want[at++] = 0xf8;
		want[at++] = 0x00; want[at++] = 0x00;
		want[at++] = 0x0f;                          /* constant 0, temporal layers 1, nested 1, lengthSize 3 */
		want[at++] = 0x03;
		want[at++] = 0x20; want[at++] = 0x00; want[at++] = 0x01;
		want[at++] = 0x00; want[at++] = (uint8_t)sizeof(vps);
		memcpy(want + at, vps, sizeof(vps)); at += sizeof(vps);
		want[at++] = 0x21; want[at++] = 0x00; want[at++] = 0x01;
		want[at++] = (uint8_t)(sps_bytes >> 8); want[at++] = (uint8_t)sps_bytes;
		memcpy(want + at, sps, sps_bytes); at += sps_bytes;
		want[at++] = 0x22; want[at++] = 0x00; want[at++] = 0x01;
		want[at++] = 0x00; want[at++] = (uint8_t)sizeof(pps);
		memcpy(want + at, pps, sizeof(pps)); at += sizeof(pps);

		CHECK_EQ(n, (int)at);
		CHECK_EQ(memcmp(out, want, at < (uint32_t)n ? at : (uint32_t)n), 0);
	}

	/* no SPS -> refused; too small -> refused */
	sets[1].type = 34;
	CHECK_EQ(n2hwenc_hvcc_build(sets, 3, out, sizeof(out)), -1);
	sets[1].type = 33;
	CHECK_EQ(n2hwenc_hvcc_build(sets, 3, out, 10), -1);
}

/* ------------------------------------------------------------------ ffmpeg vectors */

static void test_vector(const char *path, uint32_t want_codec)
{
	struct t_vector v;
	struct n2hwenc_ps sets[16];
	uint32_t n, i;
	uint8_t out[8192];

	if (t_vector_load(path, &v) != 0) {
		t_checks++;
		t_failures++;
		fprintf(stderr, "FAIL cannot load vector %s\n", path);
		return;
	}
	CHECK_EQ(v.codec, want_codec);
	CHECK(v.width >= 16 && v.height >= 16);
	CHECK(v.frame_count > 0);
	CHECK(v.param_count >= 2);

	n = t_vector_ps(&v, sets, 16);
	CHECK_EQ(n, v.param_count);
	{
		int built;
		if (v.codec == N2HWENC_AVC)
			built = n2hwenc_avcc_build(sets, n, out, sizeof(out));
		else
			built = n2hwenc_hvcc_build(sets, n, out, sizeof(out));
		CHECK_EQ(built, (int)v.extradata_bytes);
		if (built == (int)v.extradata_bytes && built > 0) {
			if (memcmp(out, v.extradata, v.extradata_bytes) != 0) {
				t_checks++;
				t_failures++;
				fprintf(stderr, "FAIL %s: %s record differs from ffmpeg extradata\n", path,
				        v.codec == N2HWENC_AVC ? "avcC" : "hvcC");
			} else {
				t_checks++;
			}
		}
	}

	for (i = 0; i < v.frame_count; i++) {
		const struct t_frame *f = &v.frames[i];
		uint8_t buf[32768];
		uint32_t out_bytes = 0, nals = 0;
		struct n2hwenc_nal nal_list[N2HWENC_NAL_MAX];
		uint32_t nal_count = 0;
		int r, any_key = 0;
		uint32_t k;

		CHECK(f->annexb_bytes <= sizeof(buf));
		if (f->annexb_bytes > sizeof(buf))
			break;
		memcpy(buf, f->annexb, f->annexb_bytes);
		r = n2hwenc_annexb_scan(buf, f->annexb_bytes, (int)v.codec, nal_list, N2HWENC_NAL_MAX, &nal_count);
		CHECK_EQ(r, 0);
		CHECK(nal_count > 0);
		if (r != 0 || nal_count == 0)
			continue;
		for (k = 0; k < nal_count; k++)
			any_key |= nal_list[k].key;
		CHECK_EQ(any_key, f->key);              /* an access unit is a key frame iff it carries an IRAP/IDR NAL */

		r = n2hwenc_annexb_to_avcc(buf, f->annexb_bytes, sizeof(buf), (int)v.codec, 0, &out_bytes, &nals);
		CHECK_EQ(r, 0);
		CHECK_EQ(out_bytes, f->avcc_bytes);
		CHECK_EQ(nals, nal_count);
		if (out_bytes == f->avcc_bytes && out_bytes > 0 && memcmp(buf, f->avcc, out_bytes) != 0) {
			t_checks++;
			t_failures++;
			fprintf(stderr, "FAIL %s frame %u: AVCC differs from ffmpeg's sample\n", path, i);
		} else {
			t_checks++;
		}

		/* The vcodecd case: parameter sets prepended on a key frame, then stripped again by the object. */
		if (f->key) {
			uint8_t with_ps[32768];
			uint32_t at = 0, j;

			for (j = 0; j < v.param_count; j++) {
				static const uint8_t sc[4] = { 0, 0, 0, 1 };
				if (at + 4 + v.params[j].bytes > sizeof(with_ps)) {
					CHECK(0);
					break;
				}
				memcpy(with_ps + at, sc, 4); at += 4;
				memcpy(with_ps + at, v.params[j].data, v.params[j].bytes);
				at += v.params[j].bytes;
			}
			if (at + f->annexb_bytes <= sizeof(with_ps)) {
				memcpy(with_ps + at, f->annexb, f->annexb_bytes);
				at += f->annexb_bytes;
				r = n2hwenc_annexb_to_avcc(with_ps, at, sizeof(with_ps), (int)v.codec, 1, &out_bytes, &nals);
				CHECK_EQ(r, 0);
				CHECK_EQ(out_bytes, f->avcc_bytes);
				if (out_bytes == f->avcc_bytes && out_bytes > 0)
					CHECK_EQ(memcmp(with_ps, f->avcc, out_bytes), 0);
			}
		}
	}

	t_vector_free(&v);
}

int main(int argc, char **argv)
{
	int i;

	test_scan_avc();
	test_scan_hevc();
	test_convert_avc();
	test_convert_many_nals();
	test_avcc_literal();
	test_hvcc_literal();

	for (i = 1; i < argc; i++) {
		const char *path = argv[i];
		test_vector(path, strstr(path, "hevc") != NULL ? N2HWENC_HEVC : N2HWENC_AVC);
	}
	return t_report("ANNEXB");
}
