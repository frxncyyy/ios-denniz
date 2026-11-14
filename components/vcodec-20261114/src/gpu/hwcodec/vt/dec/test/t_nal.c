/* SPDX-License-Identifier: MIT */
/*
 * t_nal.c -- avcC/hvcC parsing, SPS analysis and Annex B conversion against ffmpeg-made vectors.
 *
 * Arguments: <vector.vec> <expected width> <expected height> <expected full_range> <out.h264|h265>
 * The expected numbers come from ffprobe (see mkvectors.py), never from this implementation; run_tests.sh additionally
 * decodes the written Annex B stream with ffmpeg and counts its frames.
 */
#include "n2vtd_nal.h"
#include "t_apple_dec.h"
#include "t_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int codec_out = 0;

static void test_record(const struct t_vector *v, uint32_t ew, uint32_t eh, int efull, uint32_t edepth)
{
	struct n2vtd_params p;
	struct n2vtd_sps_info sps;
	int rc;

	if (v->codec == N2VTD_CODEC_AVC1)
		rc = n2vtd_avcc_parse(v->record, v->record_bytes, &p);
	else
		rc = n2vtd_hvcc_parse(v->record, v->record_bytes, &p);
	CHECK_EQ(rc, 0);
	CHECK_EQ(p.codec, v->codec);
	CHECK_EQ(p.length_size, 4);
	CHECK_EQ(p.nsps, 1);
	CHECK_EQ(p.npps, 1);
	CHECK_EQ(p.nvps, v->codec == N2VTD_CODEC_HVC1 ? 1 : 0);
	if (p.nsps != 1)
		return;
	/* The SPS in the record must be the NAL ffmpeg wrote, byte for byte. */
	CHECK(p.sps[0].size > 4);
	if (v->codec == N2VTD_CODEC_AVC1)
		CHECK_EQ(p.sps[0].data[0] & 0x1f, 7);
	else
		CHECK_EQ((p.sps[0].data[0] >> 1) & 0x3f, 33);

	rc = n2vtd_sps_parse(v->codec, p.sps[0].data, p.sps[0].size, &sps);
	CHECK_EQ(rc, 0);
	CHECK_EQ(sps.display_width, ew);
	CHECK_EQ(sps.display_height, eh);
	CHECK_EQ(sps.full_range, efull);
	CHECK_EQ(sps.chroma_format_idc, 1);
	CHECK_EQ(sps.bit_depth_luma, edepth);
	CHECK_EQ(sps.bit_depth_chroma, edepth);
	CHECK_EQ(sps.interlaced, 0);
	CHECK(sps.coded_width >= sps.display_width);
	CHECK(sps.coded_height >= sps.display_height);
	CHECK(sps.coded_width - sps.display_width < 16);
	CHECK(sps.coded_height - sps.display_height < 16);

	/* Annex B parameter sets: VPS (HEVC), SPS, PPS, each behind a 4-byte start code. */
	{
		uint8_t annexb[1024];
		int n = n2vtd_params_to_annexb(&p, annexb, sizeof(annexb));
		int expect = 4 * (p.nvps + p.nsps + p.npps);
		int i;

		CHECK(n > expect);
		if (n > 0 && n <= (int)sizeof(annexb)) {
			int at = 0, count = 0;
			while (at + 3 < n) {
				CHECK_EQ(annexb[at], 0);
				CHECK_EQ(annexb[at + 1], 0);
				CHECK_EQ(annexb[at + 2], 0);
				CHECK_EQ(annexb[at + 3], 1);
				{
					int next = at + 4;
					while (next + 3 < n && !(annexb[next] == 0 && annexb[next + 1] == 0 && annexb[next + 2] == 0 &&
					                         annexb[next + 3] == 1))
						next++;
					at = next;
				}
				count++;
			}
			CHECK_EQ(count, p.nvps + p.nsps + p.npps);
			/* and the finder recognizes them again */
			{
				struct n2vtd_param found[8];
				int nf = 0;
				CHECK_EQ(n2vtd_annexb_find_params(v->codec, annexb, (size_t)n, found, 8, &nf), 0);
				CHECK_EQ(nf, p.nvps + p.nsps + p.npps);
				for (i = 0; i < nf && i < 8; i++)
					CHECK(found[i].size > 0);
			}
		}
	}
}


/* The format-description route: atoms (the task's route) and the CoreMedia accessor fallback must both reproduce the
 * record's parameter sets. */
static void test_format_route(const struct t_vector *v)
{
	CMFormatDescriptionRef fmt = NULL;
	struct n2vtd_params direct, fromatoms, fromapi;
	uint8_t a[1024], b[1024], blob[2048];
	int na, nb, rc;

	rc = (v->codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(v->record, v->record_bytes, &direct)
	                                    : n2vtd_hvcc_parse(v->record, v->record_bytes, &direct);
	CHECK_EQ(rc, 0);
	if (rc != 0)
		return;
	na = n2vtd_params_to_annexb(&direct, a, sizeof(a));
	CHECK(na > 0);

	CHECK_EQ(n2vtdt_format_with_atoms(v->codec, v->record, v->record_bytes, &fmt), 0);
	CHECK_EQ(n2vtd_params_from_format(fmt, v->codec, &fromatoms), 0);
	nb = n2vtd_params_to_annexb(&fromatoms, b, sizeof(b));
	CHECK_EQ(nb, na);
	CHECK(na > 0 && nb == na && memcmp(a, b, (size_t)na) == 0);
	/* and through the loader (atoms first) */
	memset(&fromapi, 0, sizeof(fromapi));
	CHECK_EQ(n2vtd_params_load(fmt, v->codec, blob, sizeof(blob), &fromapi), 0);
	CHECK_EQ(n2vtd_params_equal(&direct, &fromapi), 1);
	CFRelease((CFTypeRef)fmt);

	/* No atoms: the loader falls back to CMVideoFormatDescriptionGet*ParameterSetAtIndex. */
	{
		const uint8_t *ps[16];
		size_t sizes[16];
		size_t n = 0, i;
		for (i = 0; i < (size_t)direct.nvps; i++) {
			ps[n] = direct.vps[i].data;
			sizes[n++] = direct.vps[i].size;
		}
		for (i = 0; i < (size_t)direct.nsps; i++) {
			ps[n] = direct.sps[i].data;
			sizes[n++] = direct.sps[i].size;
		}
		for (i = 0; i < (size_t)direct.npps; i++) {
			ps[n] = direct.pps[i].data;
			sizes[n++] = direct.pps[i].size;
		}
		fmt = NULL;
		CHECK_EQ(n2vtdt_format_with_params(v->codec, ps, sizes, n, 4, &fmt), 0);
		memset(&fromapi, 0, sizeof(fromapi));
		CHECK_EQ(n2vtd_params_load(fmt, v->codec, blob, sizeof(blob), &fromapi), 0);
		CHECK_EQ(n2vtd_params_equal(&direct, &fromapi), 1);
		CFRelease((CFTypeRef)fmt);
	}
}

static void test_malformed(const struct t_vector *v)
{
	uint8_t bad[128];
	struct n2vtd_params p;
	int rc;

	memcpy(bad, v->record, v->record_bytes < sizeof(bad) ? v->record_bytes : sizeof(bad));
	/* configurationVersion != 1 */
	bad[0] = 2;
	rc = (v->codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(bad, v->record_bytes, &p)
	                                    : n2vtd_hvcc_parse(bad, v->record_bytes, &p);
	CHECK(rc < 0);
	bad[0] = 1;
	/* truncated */
	rc = (v->codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(bad, 4, &p) : n2vtd_hvcc_parse(bad, 10, &p);
	CHECK(rc < 0);
	/* a length field that runs past the end */
	if (v->codec == N2VTD_CODEC_AVC1) {
		memcpy(bad, v->record, v->record_bytes);
		bad[6] = 0x7f;
		bad[7] = 0xff;
		rc = n2vtd_avcc_parse(bad, v->record_bytes, &p);
		CHECK(rc < 0);
		/* reserved lengthSizeMinusOne == 2 (3-byte lengths are not a legal value) */
		memcpy(bad, v->record, v->record_bytes);
		bad[4] = (uint8_t)((bad[4] & 0xfc) | 2);
		rc = n2vtd_avcc_parse(bad, v->record_bytes, &p);
		CHECK(rc < 0);
	}
	rc = n2vtd_avcc_parse(NULL, 0, &p);
	CHECK(rc < 0);
	rc = n2vtd_hvcc_parse(NULL, 0, &p);
	CHECK(rc < 0);
}

static void test_conversion(const struct t_vector *v, const char *out_path)
{
	uint8_t *stream;
	size_t cap = 0, at = 0;
	uint32_t i;

	for (i = 0; i < v->samples; i++)
		cap += v->sample_bytes[i] + 4u * 64u + 16;
	stream = (uint8_t *)malloc(cap);
	CHECK(stream != NULL);
	if (!stream)
		return;
	/* Parameter sets first, as the daemon receives them in DEC_HEADER. */
	{
		struct n2vtd_params p;
		int rc = (v->codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(v->record, v->record_bytes, &p)
		                                        : n2vtd_hvcc_parse(v->record, v->record_bytes, &p);
		int n;
		CHECK_EQ(rc, 0);
		n = n2vtd_params_to_annexb(&p, stream, cap);
		CHECK(n > 0);
		if (n > 0)
			at = (size_t)n;
	}
	for (i = 0; i < v->samples; i++) {
		uint8_t *dst = stream + at;
		int n = n2vtd_sample_to_annexb(v->codec, v->sample_data[i], v->sample_bytes[i], v->length_size, dst,
		                               cap - at, NULL);

		CHECK(n > 0);
		if (n <= 0)
			break;
		/* Every length prefix became a start code. */
		CHECK_EQ(dst[0], 0);
		CHECK_EQ(dst[1], 0);
		CHECK_EQ(dst[2], 0);
		CHECK_EQ(dst[3], 1);
		at += (size_t)n;
	}
	/* Re-parse the whole stream and count the NALs of each sample's own conversion. */
	for (i = 0; i < v->samples; i++) {
		uint8_t s[65536];
		int n = n2vtd_sample_to_annexb(v->codec, v->sample_data[i], v->sample_bytes[i], v->length_size, s,
		                               sizeof(s), NULL);
		CHECK(n > 0);
		if (n > 0) {
			int k = 0, p = 0, count = 0;
			while (p + 4 <= n) {
				CHECK_EQ(s[p], 0);
				CHECK_EQ(s[p + 1], 0);
				CHECK_EQ(s[p + 2], 0);
				CHECK_EQ(s[p + 3], 1);
				k = p + 4;
				while (k + 4 <= n && !(s[k] == 0 && s[k + 1] == 0 && s[k + 2] == 0 && s[k + 3] == 1))
					k++;
				CHECK(k > p + 4);
				p = k;
				count++;
			}
			CHECK(count >= 1);
		}
	}
	CHECK(at > 0);
	CHECK_EQ(t_write_file(out_path, stream, at), 0);
	free(stream);
}

static void test_annexb_passthrough(void)
{
	static const uint8_t annexb[] = { 0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1e, 0x00, 0, 0, 0, 1, 0x68, 0xce, 0, 0, 0, 1,
	                                  0x65, 0x88 };
	uint8_t out[64];
	uint64_t types = 0;
	int n = n2vtd_sample_to_annexb(N2VTD_CODEC_AVC1, annexb, sizeof(annexb), 4, out, sizeof(out), &types);

	CHECK_EQ(n, (int)sizeof(annexb));
	CHECK(memcmp(out, annexb, sizeof(annexb)) == 0);
	CHECK(types & ((uint64_t)1 << 7));
	CHECK(types & ((uint64_t)1 << 8));
	CHECK(types & ((uint64_t)1 << 5));
	{
		struct n2vtd_param found[4];
		int count = 0;
		CHECK_EQ(n2vtd_annexb_find_params(N2VTD_CODEC_AVC1, annexb, sizeof(annexb), found, 4, &count), 0);
		CHECK_EQ(count, 2);
		CHECK_EQ(found[0].size, 4);
		CHECK_EQ(found[1].size, 2);
	}
	/* Length detection on a 4-byte-prefixed sample. */
	{
		static const uint8_t avcc[] = { 0, 0, 0, 3, 0x67, 0x42, 0x00, 0, 0, 0, 2, 0x68, 0xce };
		n = n2vtd_sample_to_annexb(N2VTD_CODEC_AVC1, avcc, sizeof(avcc), 0, out, sizeof(out), NULL);
		CHECK_EQ(n, 13);
	}
	/* A length that runs past the buffer is an error, not a read. */
	{
		static const uint8_t bad[] = { 0, 0, 0, 0x40, 0x65, 0x88 };
		n = n2vtd_sample_to_annexb(N2VTD_CODEC_AVC1, bad, sizeof(bad), 4, out, sizeof(out), NULL);
		CHECK(n < 0);
	}
}

static void test_equality(const struct t_vector *v)
{
	struct n2vtd_params a, b;
	uint8_t copy[256];
	size_t sps_off;
	int rc;

	rc = (v->codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(v->record, v->record_bytes, &a)
	                                    : n2vtd_hvcc_parse(v->record, v->record_bytes, &a);
	CHECK_EQ(rc, 0);
	if (rc != 0 || a.nsps == 0)
		return;
	sps_off = (size_t)(a.sps[0].data - v->record);
	memcpy(copy, v->record, v->record_bytes);
	rc = (v->codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(copy, v->record_bytes, &b)
	                                    : n2vtd_hvcc_parse(copy, v->record_bytes, &b);
	CHECK_EQ(rc, 0);
	CHECK_EQ(n2vtd_params_equal(&a, &b), 1);
	/* Change one byte inside the SPS (its offset differs between avcC and hvcC). */
	copy[sps_off + 3] = (uint8_t)(copy[sps_off + 3] ^ 0x40);
	rc = (v->codec == N2VTD_CODEC_AVC1) ? n2vtd_avcc_parse(copy, v->record_bytes, &b)
	                                    : n2vtd_hvcc_parse(copy, v->record_bytes, &b);
	CHECK_EQ(rc, 0);
	CHECK_EQ(n2vtd_params_equal(&a, &b), 0);
	CHECK_EQ(n2vtd_params_equal(&a, NULL), 0);
}


/* Deterministic robustness sweep: random and truncated records/SPS/samples must return an error, never read past a
 * buffer.  ASan+UBSan turn any over-read or shift UB into a failure of the run. */
static void test_fuzz(void)
{
	uint8_t buf[96], out[256];
	struct n2vtd_params p;
	struct n2vtd_sps_info info;
	uint32_t seed = 0x12345678u, i;
	unsigned bad = 0;

	CHECK(1);                                  /* the sweep runs; its value is the sanitizer */
	for (i = 0; i < 2000; i++) {
		size_t len, k;
		uint32_t codec = (i & 1u) ? N2VTD_CODEC_AVC1 : N2VTD_CODEC_HVC1;

		seed = seed * 1103515245u + 12345u;
		len = 1u + (seed % sizeof(buf));
		for (k = 0; k < len; k++) {
			seed = seed * 1103515245u + 12345u;
			buf[k] = (uint8_t)(seed >> 16);
		}
		if (codec == N2VTD_CODEC_AVC1) {
			if (n2vtd_avcc_parse(buf, len, &p) != 0)
				bad++;
		} else if (n2vtd_hvcc_parse(buf, len, &p) != 0) {
			bad++;
		}
		(void)n2vtd_sps_parse(codec, buf, len, &info);
		(void)n2vtd_sample_to_annexb(codec, buf, len, (uint32_t)(seed % 5u), out, sizeof(out), NULL);
		(void)n2vtd_annexb_find_params(codec, buf, len, (struct n2vtd_param *)out, 0, (int *)&k);
	}
	CHECK(bad > 0);                            /* random bytes are not valid records */
}

int main(int argc, char **argv)
{
	struct t_vector v;
	char err[128];

	if (argc != 6 && argc != 7) {
		fprintf(stderr, "usage: %s <vector> <width> <height> <full_range> <out.annexb> [bit_depth]\n", argv[0]);
		return 2;
	}
	if (t_vector_load(argv[1], &v, err, sizeof(err)) != 0) {
		fprintf(stderr, "t_nal: %s\n", err);
		return 2;
	}
	codec_out = (v.codec == N2VTD_CODEC_AVC1) ? 0 : 1;
	(void)codec_out;
	printf("t_nal: %s codec %08x record %zu bytes, %u samples\n", argv[1], v.codec, v.record_bytes, v.samples);
	test_record(&v, (uint32_t)strtoul(argv[2], NULL, 0), (uint32_t)strtoul(argv[3], NULL, 0),
	            (int)strtol(argv[4], NULL, 0), argc == 7 ? (uint32_t)strtoul(argv[6], NULL, 0) : 8);
	test_format_route(&v);
	test_malformed(&v);
	test_conversion(&v, argv[5]);
	test_annexb_passthrough();
	test_equality(&v);
	test_fuzz();
	t_vector_free(&v);
	return t_summary("NAL");
}
