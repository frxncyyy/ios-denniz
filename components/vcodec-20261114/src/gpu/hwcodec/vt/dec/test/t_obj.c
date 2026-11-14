/* SPDX-License-Identifier: MIT */
/*
 * t_obj.c -- the VideoToolbox decoder object driven the way VTDecompressionSession drives it, over a scripted decoder
 * client, with the real avcC/hvcC vectors ffmpeg made.
 *
 * Checks: registration and its switch files, the class record (version 2, slots, tile slots NULL), StartSession
 * (parameter sets, geometry, pixel buffer attributes 420v/420f), DecodeFrame (Annex B, emit-once, display order, pts),
 * DoNotOutputFrame, FinishDelayedFrames (drained and dropped frames), CanAcceptFormatDescription, the base class
 * property copy, invalidate/finalize, the unsupported-stream and unavailable-daemon paths, and two sessions on two
 * threads (the suite is also run under TSan).
 */
#include "n2vtd_vt.h"
#include "n2vtd_nal.h"
#include "n2vtd_pixels.h"
#include "t_apple_dec.h"
#include "t_fakeclient_dec.h"
#include "t_support.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CODEC_AVC1 0x61766331u   /* VideoToolbox CMVideoCodecType, not the daemon's word order */
#define CODEC_HVC1 0x68766331u

static char g_swdir[128];

static void switch_setup(const char *name, int hwdec, int nohevc, int off)
{
	snprintf(g_swdir, sizeof(g_swdir), "/tmp/n2hw-DEC06/sw-%s", name);
	mkdir("/tmp/n2hw-DEC06", 0777);
	rmdir(g_swdir);
	mkdir(g_swdir, 0777);
	if (hwdec) {
		char p[160];
		snprintf(p, sizeof(p), "%s/nord2-video-hwdec", g_swdir);
		fclose(fopen(p, "w"));
	}
	if (nohevc) {
		char p[160];
		snprintf(p, sizeof(p), "%s/nord2-video-hwdec-no-hevc", g_swdir);
		fclose(fopen(p, "w"));
	}
	if (off) {
		char p[160];
		snprintf(p, sizeof(p), "%s/nord2-video-off", g_swdir);
		fclose(fopen(p, "w"));
	}
	n2vtd_register_set_switch_dir(g_swdir);
}

static void vector_geometry(const struct t_vector *v, uint32_t stride, uint32_t buf_h, uint32_t *out_bytes)
{
	uint32_t cstride = (stride + 31u) & ~31u;

	*out_bytes = stride * buf_h + cstride * (buf_h / 2u);
	(void)v;
}

static int load_vector(const char *path, struct t_vector *v)
{
	char err[128];

	if (t_vector_load(path, v, err, sizeof(err)) != 0) {
		fprintf(stderr, "t_obj: %s\n", err);
		return -1;
	}
	return 0;
}

static int check_picture(CVPixelBufferRef pb, uint64_t pts, uint32_t w, uint32_t h)
{
	const uint8_t *y = n2vtdt_pixel_plane(pb, 0);
	const uint8_t *c = n2vtdt_pixel_plane(pb, 1);
	size_t ys = n2vtdt_pixel_plane_stride(pb, 0);
	size_t cs = n2vtdt_pixel_plane_stride(pb, 1);
	uint32_t x, yy;
	int ok = 1;

	if (!y || !c)
		return 0;
	for (yy = 0; yy < h; yy++)
		for (x = 0; x < w; x++)
			if (y[(size_t)yy * ys + x] != (uint8_t)((x * 7u + yy * 3u + (uint32_t)pts) & 0xffu))
				ok = 0;
	for (yy = 0; yy < h / 2; yy++)
		for (x = 0; x < w / 2; x++) {
			if (c[(size_t)yy * cs + x * 2u] != (uint8_t)((x * 5u + yy * 11u + (uint32_t)pts + 60u) & 0xffu))
				ok = 0;
			if (c[(size_t)yy * cs + x * 2u + 1u] !=
			    (uint8_t)((x * 13u + yy * 2u + (uint32_t)pts + 200u) & 0xffu))
				ok = 0;
		}
	return ok;
}

static int32_t dict_int32(CFDictionaryRef d, const char *key)
{
	const void *v = n2vtdt_dictionary_get(d, key);
	int32_t out = -1;

	if (!v)
		return -1;
	n2vtdt_number_int32((CFTypeRef)v, &out);
	return out;
}

static void test_registration(void)
{
	switch_setup("none", 0, 0, 0);
	n2vtdt_vt_reset();
	n2vtd_vt_availability_reset();
	CHECK_EQ(n2vtd_vt_register_all(), 0);
	CHECK_EQ(n2vtd_register_enabled(0), 0);
	CHECK_EQ(n2vtd_register_enabled(1), 0);

	switch_setup("hwdec", 1, 0, 0);
	n2vtdt_vt_reset();
	CHECK_EQ(n2vtd_vt_register_all(), 2);
	{
		uint32_t codec = 0;
		CFDictionaryRef info = NULL;
		int i;
		int seen_avc = 0, seen_hevc = 0;

		for (i = 0; i < 4; i++) {
			if (!n2vtdt_vt_registered(i, &codec, &info, NULL))
				break;
			if (codec == CODEC_AVC1)
				seen_avc++;
			if (codec == CODEC_HVC1)
				seen_hevc++;
		}
		CHECK_EQ(seen_avc, 1);
		CHECK_EQ(seen_hevc, 1);
		CHECK(info != NULL);
		if (info) {
			const void *id = n2vtdt_dictionary_get(info, "CMClassImplementationID");
			const void *hw = n2vtdt_dictionary_get(info, "VTIsHardwareAccelerated");
			int32_t rating = 0;
			CHECK(id != NULL);
			CHECK(hw != NULL && CFBooleanGetValue((CFBooleanRef)hw));
			{
				const void *r = n2vtdt_dictionary_get(info, "VTRating");
				CHECK(r != NULL);
				if (r) {
					n2vtdt_number_int32((CFTypeRef)r, &rating);
					CHECK_EQ(rating, N2VT_DEC_RATING);
				}
			}
		}
	}

	switch_setup("nohevc", 1, 1, 0);
	n2vtdt_vt_reset();
	CHECK_EQ(n2vtd_vt_register_all(), 1);
	{
		uint32_t codec = 0;
		CHECK(n2vtdt_vt_registered(0, &codec, NULL, NULL));
		CHECK_EQ(codec, CODEC_AVC1);
		CHECK(!n2vtdt_vt_registered(1, &codec, NULL, NULL));
	}

	switch_setup("off", 1, 0, 1);
	n2vtdt_vt_reset();
	CHECK_EQ(n2vtd_vt_register_all(), 0);
	CHECK_EQ(n2vtd_register_enabled(0), 0);
	CHECK_EQ(n2vtd_register_enabled(1), 0);

	/* Back to a working configuration for the object tests. */
	switch_setup("main", 1, 0, 0);
	n2vtdt_vt_reset();
	CHECK_EQ(n2vtd_vt_register_all(), 2);
}

static void test_class_record(void)
{
	/* The static records are only reachable through an object; check them through a live session. */
	struct t_vector v;
	struct n2vtd_params p;
	uint8_t copy[256];
	CMFormatDescriptionRef fmt = NULL;
	struct n2vtdt_session *s;
	struct tfd_config cfg;
	OSStatus status = 0;

	if (load_vector("/tmp/n2hw-DEC06/vectors/avc-high.vec", &v) != 0)
		return;
	memset(&cfg, 0, sizeof(cfg));
	cfg.width = 176;
	cfg.height = 144;
	cfg.stride = 176;
	cfg.buf_h = 160;
	cfg.dpb = 4;
	cfg.format = N2VTD_FMT_MM21;
	cfg.in_bytes = 65536;
	cfg.auto_picture = 1;
	vector_geometry(&v, cfg.stride, cfg.buf_h, &cfg.out_bytes);
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	CHECK_EQ(n2vtdt_format_with_atoms(v.codec, v.record, v.record_bytes, &fmt), 0);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &status);
	CHECK(s != NULL);
	CHECK_EQ(status, 0);
	if (!s) {
		t_vector_free(&v);
		return;
	}
	CHECK_EQ(n2vtdt_session_class(s)->version, 2u);
	CHECK(n2vtdt_session_class(s)->startSession != NULL);
	CHECK(n2vtdt_session_class(s)->decodeFrame != NULL);
	CHECK(n2vtdt_session_class(s)->copySupportedPropertyDictionary != NULL);
	CHECK(n2vtdt_session_class(s)->setProperties != NULL);
	CHECK(n2vtdt_session_class(s)->copySerializableProperties != NULL);
	CHECK(n2vtdt_session_class(s)->canAcceptFormatDescription != NULL);
	CHECK(n2vtdt_session_class(s)->finishDelayedFrames != NULL);
	CHECK(n2vtdt_session_class(s)->startTileSession == NULL);
	CHECK(n2vtdt_session_class(s)->decodeTile == NULL);
	CHECK(n2vtdt_session_class(s)->finishDelayedTiles == NULL);
	CHECK(n2vtdt_session_base(s)->version == 1);
	CHECK(n2vtdt_session_base(s)->derivedStorageSize > 0);
	CHECK(n2vtdt_session_base(s)->finalize != NULL);
	CHECK(n2vtdt_session_base(s)->invalidate != NULL);
	CHECK(n2vtdt_session_base(s)->copyProperty != NULL);
	CHECK(n2vtdt_session_base(s)->setProperty != NULL);

	/* Pixel buffer attributes: NV12, display size, IOSurfaceProperties present. */
	{
		CFDictionaryRef attrs = n2vtdt_session_pixel_attributes(s);
		CHECK(attrs != NULL);
		if (attrs) {
			CHECK_EQ(dict_int32(attrs, "PixelFormatType"), (int32_t)N2_PIXFMT_420V);
			CHECK_EQ(dict_int32(attrs, "Width"), 176);
			CHECK_EQ(dict_int32(attrs, "Height"), 144);
			CHECK(n2vtdt_dictionary_get(attrs, "IOSurfaceProperties") != NULL);
		}
	}

	/* Base property copy. */
	{
		CFTypeRef value = NULL;
		CHECK_EQ(n2vtdt_session_copy_property(s, n2vtdt_cfstr("UsingHardwareAcceleratedVideoDecoder"), &value), 0);
		CHECK(value != NULL && CFBooleanGetValue((CFBooleanRef)value));
		if (value)
			CFRelease(value);
		value = NULL;
		CHECK_EQ(n2vtdt_session_copy_property(s, n2vtdt_cfstr("SomeUnknownKey"), &value), -12783);
		CHECK(value == NULL);
	}

	/* Class property slots. */
	CHECK_EQ(n2vtdt_session_set_properties(s, NULL), -12782);
	{
		CFDictionaryRef dict = NULL;
		CHECK_EQ(n2vtdt_session_copy_supported(s, &dict), 0);
		CHECK(dict != NULL);
		if (dict) {
			CHECK_EQ(n2vtdt_dictionary_count(dict), 0);
			CFRelease((CFTypeRef)dict);
		}
	}

	/* CanAccept: the same parameter sets yes, a changed SPS no. */
	CHECK_EQ(n2vtdt_session_can_accept(s, fmt), 0);
	CHECK_EQ(n2vtd_avcc_parse(v.record, v.record_bytes, &p), 0);
	if (p.nsps > 0) {
		CMFormatDescriptionRef other = NULL;
		size_t off = (size_t)(p.sps[0].data - v.record);
		memcpy(copy, v.record, v.record_bytes);
		copy[off + 3] = (uint8_t)(copy[off + 3] ^ 0x20);
		CHECK_EQ(n2vtdt_format_with_atoms(v.codec, copy, v.record_bytes, &other), 0);
		CHECK_EQ(n2vtdt_session_can_accept(s, other), -12916);
		CFRelease((CFTypeRef)other);
	}
	n2vtdt_session_destroy(s);
	CFRelease((CFTypeRef)fmt);
	t_vector_free(&v);
}

static void test_decode(void)
{
	struct t_vector v;
	CMFormatDescriptionRef fmt = NULL;
	CMFormatDescriptionRef full_fmt = NULL;
	struct t_vector vf;
	struct n2vtdt_session *s;
	struct tfd_config cfg;
	struct tfd_conn *conn;
	OSStatus status = 0;
	CMSampleBufferRef sb = NULL;
	uint32_t info = 0xffffffffu;

	if (load_vector("/tmp/n2hw-DEC06/vectors/avc-high.vec", &v) != 0)
		return;
	if (load_vector("/tmp/n2hw-DEC06/vectors/avc-full.vec", &vf) != 0) {
		t_vector_free(&v);
		return;
	}
	memset(&cfg, 0, sizeof(cfg));
	cfg.width = 176;
	cfg.height = 144;
	cfg.stride = 176;
	cfg.buf_h = 160;
	cfg.dpb = 4;
	cfg.format = N2VTD_FMT_MM21;
	cfg.in_bytes = 65536;
	cfg.auto_picture = 1;
	cfg.vendor_rules = 1;      /* dp1-fix: nothing is displayed unless the SPS/PPS travel in-band (phone vendor) */
	vector_geometry(&v, cfg.stride, cfg.buf_h, &cfg.out_bytes);
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	n2vtd_vt_stats_reset();
	CHECK_EQ(n2vtdt_format_with_atoms(v.codec, v.record, v.record_bytes, &fmt), 0);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &status);
	CHECK(s != NULL);
	if (!s) {
		t_vector_free(&v);
		t_vector_free(&vf);
		return;
	}
	conn = tfd_last_conn();
	CHECK(conn != NULL);
	CHECK_EQ(tfd_registered(conn), cfg.dpb);
	CHECK_EQ(tfd_open_format(conn), (uint32_t)N2VTD_FMT_MM21);
	CHECK_EQ(tfd_open_max_w(conn), 176u);              /* from the SPS display size */
	CHECK_EQ(tfd_open_max_h(conn), 144u);

	/* One frame: the access unit is converted to Annex B, the picture is detiled and emitted once. */
	CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[0], v.sample_bytes[0], 1000, &sb), 0);
	CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
	CHECK_EQ(info, 0u);                        /* emitted inside decodeFrame */
	CHECK_EQ(n2vtdt_session_emit_count(s), 1);
	{
		OSStatus est = -1;
		uint32_t eflags = 0xffffffffu;
		CVImageBufferRef img = NULL;
		int tok = -1;

		CHECK(n2vtdt_session_emitted(s, 0, &est, &eflags, &img, &tok));
		CHECK_EQ(est, 0);
		CHECK_EQ(eflags, 0u);
		CHECK_EQ(tok, 0);
		CHECK_EQ(n2vtdt_session_token_emitted(s, 0), 1);
		CHECK(img != NULL);
		if (img) {
			CHECK_EQ(CVPixelBufferGetPixelFormatType(img), N2_PIXFMT_420V);
			CHECK_EQ(CVPixelBufferGetWidth(img), 176u);
			CHECK_EQ(CVPixelBufferGetHeight(img), 144u);
			CHECK(check_picture(img, 1000, 176, 144));
		}
	}
	CHECK_EQ(tfd_inputs(conn), 1u);
	CHECK_EQ_U64(tfd_last_pts(conn), 1000u);
	/* dp1-fix: the first input after the configure carries the format's parameter sets (Annex B) in front of the
	 * converted sample; 4-byte prefixes and 4-byte start codes are the same size, so the sample keeps its length. */
	{
		struct n2vtd_params p;
		uint8_t ps[4096];
		int psb;
		const uint8_t *au = tfd_last_au(conn);

		CHECK_EQ(n2vtd_avcc_parse(v.record, v.record_bytes, &p), 0);
		psb = n2vtd_params_to_annexb(&p, ps, sizeof(ps));
		CHECK(psb > 0);
		CHECK_EQ(tfd_last_au_bytes(conn), (uint32_t)psb + v.sample_bytes[0]);
		CHECK(psb > 0 && memcmp(au, ps, (size_t)psb) == 0);
		CHECK(au[0] == 0 && au[1] == 0 && au[2] == 0 && au[3] == 1);
		CHECK((au[4] & 0x1f) == 7);                         /* SPS first */
		CHECK_EQ(tfd_inputs_with_sps(conn), 1u);
		CHECK_EQ(tfd_inputs_dropped(conn), 0u);
		CHECK_EQ_U64(n2vtd_vt_stats()->params_inband, 1u);
	}
	CFRelease((CFTypeRef)sb);
	sb = NULL;

	/* Second frame. */
	CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[1], v.sample_bytes[1], 2000, &sb), 0);
	CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
	CHECK_EQ(n2vtdt_session_emit_count(s), 2);
	/* dp1-fix: a non-sync sample goes as it came (no second copy of the parameter sets). */
	CHECK_EQ(tfd_last_au_bytes(conn), v.sample_bytes[1]);
	CHECK_EQ(tfd_inputs_with_sps(conn), 1u);
	{
		CVImageBufferRef img = NULL;
		int tok = -1;
		CHECK(n2vtdt_session_emitted(s, 1, NULL, NULL, &img, &tok));
		CHECK_EQ(tok, 1);
		CHECK(img && check_picture(img, 2000, 176, 144));
	}
	CFRelease((CFTypeRef)sb);
	sb = NULL;

	/* DoNotOutputFrame: the picture is decoded and released but no image is emitted. */
	CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[2], v.sample_bytes[2], 3000, &sb), 0);
	CHECK_EQ(n2vtdt_session_decode(s, sb, N2_kVTDecodeFrame_DoNotOutputFrame, &info), 0);
	CHECK_EQ(n2vtdt_session_emit_count(s), 3);
	{
		OSStatus est = -1;
		uint32_t eflags = 0;
		CVImageBufferRef img = (CVImageBufferRef)1;

		CHECK(n2vtdt_session_emitted(s, 2, &est, &eflags, &img, NULL));
		CHECK_EQ(est, 0);
		CHECK(eflags & N2_kVTDecodeInfo_FrameDropped);
		CHECK(img == NULL);
	}
	CFRelease((CFTypeRef)sb);
	sb = NULL;

	/* Reorder: two tokens held, then two pictures in display order (2000 before 1000). */
	tfd_set_auto(conn, 0);
	tfd_set_ready(conn, 0);
	CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[3], v.sample_bytes[3], 1000, &sb), 0);
	CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
	CHECK(info & N2_kVTDecodeInfo_Asynchronous);
	CFRelease((CFTypeRef)sb);
	sb = NULL;
	CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[4], v.sample_bytes[4], 2000, &sb), 0);
	tfd_push_picture(conn, 2000, 0);
	tfd_push_picture(conn, 1000, 1);
	tfd_set_ready(conn, 2);
	CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
	CHECK_EQ(n2vtdt_session_emit_count(s), 5);
	{
		CVImageBufferRef img = NULL;
		int tok = -1;

		CHECK(n2vtdt_session_emitted(s, 3, NULL, NULL, &img, &tok));
		CHECK_EQ(tok, 4);                 /* pts 2000 was submitted second */
		CHECK(img && check_picture(img, 2000, 176, 144));
		img = NULL;
		CHECK(n2vtdt_session_emitted(s, 4, NULL, NULL, &img, &tok));
		CHECK_EQ(tok, 3);                 /* pts 1000 submitted first, displayed second */
		CHECK(img && check_picture(img, 1000, 176, 144));
	}
	CFRelease((CFTypeRef)sb);
	sb = NULL;
	CHECK_EQ(tfd_released(conn), 5u);

	/* FinishDelayedFrames drains a held frame. */
	tfd_set_ready(conn, 0);
	CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[0], v.sample_bytes[0], 4000, &sb), 0);
	CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
	CFRelease((CFTypeRef)sb);
	sb = NULL;
	tfd_push_picture(conn, 4000, 0);
	tfd_set_flush_ready(conn, 1);
	CHECK_EQ(n2vtdt_session_finish(s), 0);
	CHECK_EQ(n2vtdt_session_emit_count(s), 6);
	{
		CVImageBufferRef img = NULL;
		int tok = -1;
		CHECK(n2vtdt_session_emitted(s, 5, NULL, NULL, &img, &tok));
		CHECK_EQ(tok, 5);
		CHECK(img && check_picture(img, 4000, 176, 144));
	}

	/* A frame the daemon never returns is emitted as dropped at FLUSH, exactly once. */
	tfd_set_ready(conn, 0);
	tfd_set_flush_ready(conn, 0);
	CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[1], v.sample_bytes[1], 5000, &sb), 0);
	CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
	CFRelease((CFTypeRef)sb);
	sb = NULL;
	CHECK_EQ(n2vtdt_session_finish(s), 0);
	CHECK_EQ(n2vtdt_session_emit_count(s), 7);
	{
		uint32_t eflags = 0;
		CVImageBufferRef img = (CVImageBufferRef)1;
		CHECK(n2vtdt_session_emitted(s, 6, NULL, &eflags, &img, NULL));
		CHECK(eflags & N2_kVTDecodeInfo_FrameDropped);
		CHECK(img == NULL);
	}
	CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
	CHECK_EQ(n2vtd_vt_stats()->pictures_out, 5u);
	CHECK_EQ(n2vtd_vt_stats()->tokens_pending_dropped, 1u);

	/* Geometry change: the daemon answers changed=1, the object drains, frees the pool, re-headers, re-adds and
	 * resubmits the same access unit. */
	tfd_set_auto(conn, 1);
	tfd_set_ready(conn, 1);
	tfd_set_changed_next(conn, 1);
	{
		uint32_t before = tfd_inputs(conn);
		CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[0], v.sample_bytes[0], 6000, &sb), 0);
		CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
		CHECK(tfd_inputs(conn) >= before + 2);          /* the input was not consumed, so it is sent twice */
		CFRelease((CFTypeRef)sb);
		sb = NULL;
		CHECK_EQ(n2vtdt_session_emit_count(s), 8);
		CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
	}

	/* A linear NV12 daemon is handled without the MM21 detile. */
	{
		struct tfd_config ncfg = cfg;
		struct n2vtdt_session *ns;
		CMSampleBufferRef nsb = NULL;
		uint32_t ninfo = 0;

		ncfg.format = N2VTD_FMT_NV12;
		tfd_install(&ncfg);
		n2vtd_vt_availability_reset();
		ns = n2vtdt_session_create(CODEC_AVC1, fmt, &status);
		CHECK(ns != NULL);
		if (ns) {
			CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[0], v.sample_bytes[0], 7000, &nsb), 0);
			CHECK_EQ(n2vtdt_session_decode(ns, nsb, 0, &ninfo), 0);
			CHECK_EQ(n2vtdt_session_emit_count(ns), 1);
			{
				CVImageBufferRef img = NULL;
				CHECK(n2vtdt_session_emitted(ns, 0, NULL, NULL, &img, NULL));
				CHECK(img && check_picture(img, 7000, 176, 144));
			}
			CFRelease((CFTypeRef)nsb);
			n2vtdt_session_destroy(ns);
		}
		/* Restore the MM21 config for the remaining checks. */
		tfd_install(&cfg);
		n2vtd_vt_availability_reset();
	}

	/* A full-range stream must ask for 420f. */
	{
		struct n2vtdt_session *sf;
		CFDictionaryRef attrs;

		CHECK_EQ(n2vtdt_format_with_atoms(vf.codec, vf.record, vf.record_bytes, &full_fmt), 0);
		sf = n2vtdt_session_create(CODEC_AVC1, full_fmt, &status);
		CHECK(sf != NULL);
		if (sf) {
			struct tfd_conn *fc = tfd_last_conn();
			attrs = n2vtdt_session_pixel_attributes(sf);
			CHECK(attrs != NULL);
			if (attrs)
				CHECK_EQ(dict_int32(attrs, "PixelFormatType"), (int32_t)N2_PIXFMT_420F);
			/* Full range must NOT change the daemon layout: OPEN asks for MM21 either way. */
			CHECK_EQ(tfd_open_format(fc), (uint32_t)N2VTD_FMT_MM21);
			n2vtdt_session_destroy(sf);
		}
		CFRelease((CFTypeRef)full_fmt);
	}
	n2vtdt_session_destroy(s);
	CFRelease((CFTypeRef)fmt);
	t_vector_free(&v);
	t_vector_free(&vf);
}

static void test_invalidate_and_failures(void)
{
	struct t_vector v;
	CMFormatDescriptionRef fmt = NULL;
	struct n2vtdt_session *s;
	struct tfd_config cfg;
	OSStatus status = 0;
	CMSampleBufferRef sb = NULL;
	uint32_t info = 0;
	CMBaseObjectRef obj;

	if (load_vector("/tmp/n2hw-DEC06/vectors/avc-high.vec", &v) != 0)
		return;
	memset(&cfg, 0, sizeof(cfg));
	cfg.width = 176;
	cfg.height = 144;
	cfg.stride = 176;
	cfg.buf_h = 160;
	cfg.dpb = 4;
	cfg.format = N2VTD_FMT_MM21;
	cfg.in_bytes = 65536;
	cfg.auto_picture = 1;
	vector_geometry(&v, cfg.stride, cfg.buf_h, &cfg.out_bytes);
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	CHECK_EQ(n2vtdt_format_with_atoms(v.codec, v.record, v.record_bytes, &fmt), 0);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &status);
	CHECK(s != NULL);
	if (!s) {
		t_vector_free(&v);
		return;
	}
	obj = n2vtdt_session_object(s);
	CHECK(obj != NULL);
	if (obj) {
		CMBaseObjectRef identity = VTVideoDecoderGetCMBaseObject((VTVideoDecoderRef)obj);
		CHECK(identity == obj);
		/* invalidate is idempotent and closes the daemon session; decode afterwards reports invalid session. */
		CHECK_EQ(n2vtdt_session_base(s)->invalidate(obj), 0);
		CHECK_EQ(n2vtdt_session_base(s)->invalidate(obj), 0);
		/* invalidate disconnects and frees the client connection: the fake keeps the final state. */
		CHECK_EQ(tfd_seen_closed(), 1);
		CHECK_EQ(tfd_seen_registered(), 0u);
		CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[0], v.sample_bytes[0], 1000, &sb), 0);
		CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), N2_kVTInvalidSessionErr);
		CFRelease((CFTypeRef)sb);
		sb = NULL;
	}
	n2vtdt_session_destroy(s);
	CFRelease((CFTypeRef)fmt);
	t_vector_free(&v);

	/* connect failure: the factory must return -12913 so VT can try Apple's decoder. */
	memset(&cfg, 0, sizeof(cfg));
	cfg.connect_fail = 1;
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	{
		VTVideoDecoderRef dec = NULL;
		OSStatus st = n2vtd_vt_create_avc(CODEC_AVC1, NULL, &dec);
		CHECK_EQ(st, N2_kVTVideoDecoderNotAvailableNowErr);
		CHECK(dec == NULL);
	}
	/* open failure inside StartSession is not retried by VT; it must be a documented error, not a crash. */
	memset(&cfg, 0, sizeof(cfg));
	cfg.open_fail = 1;
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	if (load_vector("/tmp/n2hw-DEC06/vectors/avc-high.vec", &v) != 0)
		return;
	CHECK_EQ(n2vtdt_format_with_atoms(v.codec, v.record, v.record_bytes, &fmt), 0);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &status);
	CHECK(s == NULL);
	CHECK_EQ(status, N2_kVTVideoDecoderMalfunctionErr);
	CFRelease((CFTypeRef)fmt);
	t_vector_free(&v);
}

static void test_unsupported(void)
{
	struct t_vector v;
	CMFormatDescriptionRef fmt = NULL;
	struct n2vtdt_session *s;
	struct tfd_config cfg;
	OSStatus status = 0;
	CMSampleBufferRef sb = NULL;
	uint32_t info = 0;

	if (load_vector("/tmp/n2hw-DEC06/vectors/hevc-main10.vec", &v) != 0)
		return;
	memset(&cfg, 0, sizeof(cfg));
	cfg.width = 176;
	cfg.height = 144;
	cfg.stride = 192;
	cfg.buf_h = 160;
	cfg.dpb = 4;
	cfg.format = N2VTD_FMT_MM21;
	cfg.in_bytes = 65536;
	cfg.auto_picture = 1;
	vector_geometry(&v, cfg.stride, cfg.buf_h, &cfg.out_bytes);
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	CHECK_EQ(n2vtdt_format_with_atoms(v.codec, v.record, v.record_bytes, &fmt), 0);
	s = n2vtdt_session_create(CODEC_HVC1, fmt, &status);
	CHECK(s != NULL);                          /* StartSession must not fail: VT does not retry it */
	if (s) {
		CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[0], v.sample_bytes[0], 1000, &sb), 0);
		CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), N2_kVTVideoDecoderUnsupportedDataFormatErr);
		CHECK_EQ(n2vtdt_session_emit_count(s), 0);
		CFRelease((CFTypeRef)sb);
		n2vtdt_session_destroy(s);
	}
	CFRelease((CFTypeRef)fmt);
	t_vector_free(&v);
}

/* dp4: HEVC lazy configure against a fake with the vendor's rules (a DEC_HEADER without a slice fails, nothing is
 * displayed until an INPUT carried the parameter sets).  StartSession: OPEN, pixel buffer attributes from the SPS
 * (176x144), no DEC_HEADER, no pool.  First DecodeFrame (IDR): DEC_HEADER = VPS/SPS/PPS + that AU, pool, then the same
 * AU as the first INPUT with the parameter sets in front; pictures are detiled and compared pixel by pixel.  Three
 * vendor geometries: equal to the SPS, padded (176x152: output stays 176x144, the copy crops), and different
 * (320x180: the vendor wins and the attributes are sent again). */
static void hevc_lazy_case(uint32_t vw, uint32_t vh, uint32_t ow, uint32_t oh, const char *log_needle)
{
	struct t_vector v;
	CMFormatDescriptionRef fmt = NULL;
	struct n2vtdt_session *s;
	struct tfd_config cfg;
	struct tfd_conn *conn;
	OSStatus status = 0;
	int k;

	if (load_vector("/tmp/n2hw-DEC06/vectors/hevc-main.vec", &v) != 0)
		return;
	memset(&cfg, 0, sizeof(cfg));
	cfg.width = vw;
	cfg.height = vh;
	cfg.stride = (vw + 15u) & ~15u;
	cfg.buf_h = (vh + 31u) & ~31u;
	cfg.dpb = 6;
	cfg.format = N2VTD_FMT_MM21;
	cfg.in_bytes = 65536;
	cfg.auto_picture = 1;
	cfg.vendor_rules = 1;
	vector_geometry(&v, cfg.stride, cfg.buf_h, &cfg.out_bytes);
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	n2vtd_vt_stats_reset();
	CHECK_EQ(n2vtdt_format_with_atoms(v.codec, v.record, v.record_bytes, &fmt), 0);
	s = n2vtdt_session_create(CODEC_HVC1, fmt, &status);
	CHECK(s != NULL);
	if (!s) {
		t_vector_free(&v);
		CFRelease((CFTypeRef)fmt);
		return;
	}
	conn = tfd_last_conn();
	CHECK(conn != NULL);
	CHECK_EQ(tfd_header_bytes(conn), 0u);
	CHECK_EQ(tfd_registered(conn), 0u);
	CHECK_EQ(tfd_open_max_w(conn), 176u);
	{
		CFDictionaryRef attrs = n2vtdt_session_pixel_attributes(s);   /* at StartSession, from the SPS */
		CHECK(attrs != NULL);
		if (attrs) {
			CHECK_EQ(dict_int32(attrs, "PixelFormatType"), (int32_t)N2_PIXFMT_420V);
			CHECK_EQ(dict_int32(attrs, "Width"), 176);
			CHECK_EQ(dict_int32(attrs, "Height"), 144);
		}
	}
	CHECK_EQ(n2vtd_vt_stats()->headers_deferred, 1u);
	for (k = 0; k < (int)v.samples; k++) {
		CMSampleBufferRef sb = NULL;
		uint32_t info = 0;

		CHECK_EQ(n2vtdt_sample_create(fmt, v.sample_data[k], v.sample_bytes[k], 1000 + k, &sb), 0);
		CHECK_EQ(n2vtdt_session_decode(s, sb, 0, &info), 0);
		CFRelease((CFTypeRef)sb);
		if (k == 0) {
			struct n2vtd_params p;
			uint8_t ps[4096];
			int psb;

			CHECK_EQ(n2vtd_hvcc_parse(v.record, v.record_bytes, &p), 0);
			psb = n2vtd_params_to_annexb(&p, ps, sizeof(ps));
			CHECK(psb > 0);
			/* DEC_HEADER = parameter sets + the IDR; first INPUT = parameter sets + the IDR again */
			CHECK_EQ(tfd_header_bytes(conn), (uint32_t)psb + v.sample_bytes[0]);
			CHECK_EQ(tfd_registered(conn), cfg.dpb);
			CHECK_EQ(tfd_last_au_bytes(conn), (uint32_t)psb + v.sample_bytes[0]);
			CHECK(psb > 0 && memcmp(tfd_last_au(conn), ps, (size_t)psb) == 0);
			CHECK_EQ(tfd_inputs_with_sps(conn), 1u);
		}
	}
	CHECK_EQ(tfd_inputs(conn), v.samples);
	CHECK_EQ(tfd_inputs_dropped(conn), 0u);
	CHECK_EQ(n2vtdt_session_emit_count(s), (int)v.samples);
	for (k = 0; k < n2vtdt_session_emit_count(s); k++) {
		OSStatus est = -1;
		uint32_t fl = 1;
		CVImageBufferRef img = NULL;
		int tok = -1;

		CHECK(n2vtdt_session_emitted(s, k, &est, &fl, &img, &tok));
		CHECK_EQ(est, 0);
		CHECK_EQ(fl, 0u);
		CHECK(img != NULL);
		CHECK_EQ(n2vtdt_session_token_emitted(s, tok), 1);
		if (img) {
			CHECK_EQ(CVPixelBufferGetWidth(img), ow);
			CHECK_EQ(CVPixelBufferGetHeight(img), oh);
			CHECK(check_picture(img, 1000 + (uint64_t)k, ow, oh));
		}
	}
	{
		CFDictionaryRef attrs = n2vtdt_session_pixel_attributes(s);
		CHECK(attrs && dict_int32(attrs, "Width") == (int32_t)ow && dict_int32(attrs, "Height") == (int32_t)oh);
	}
	CHECK_EQ(n2vtdt_session_finish(s), 0);
	CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
	CHECK_EQ(n2vtd_vt_stats()->headers_with_slice, 1u);
	CHECK_EQ(n2vtd_vt_stats()->pictures_out, (uint64_t)v.samples);
	CHECK(n2vtdt_log_contains("DEC_HEADER waits for the first IRAP"));
	CHECK(n2vtdt_log_contains("session configured at the first IRAP"));
	if (log_needle)
		CHECK(n2vtdt_log_contains(log_needle));
	n2vtdt_session_destroy(s);
	CHECK_EQ(tfd_seen_closed(), 1);
	CFRelease((CFTypeRef)fmt);
	t_vector_free(&v);
}

static void test_hevc_lazy(void)
{
	hevc_lazy_case(176, 144, 176, 144, NULL);
	hevc_lazy_case(176, 152, 176, 144, "padding cropped");
	hevc_lazy_case(320, 180, 320, 180, "replaces 176x144");
}

/* Two sessions on two threads: the probe cache, the client table and the registration registry are process-global. */
static struct {
	const struct t_vector *v;
	CMFormatDescriptionRef fmt;
	int ok;
} g_thread_ctx[2];

static void *thread_decode(void *arg)
{
	int idx = (int)(intptr_t)arg;
	struct n2vtdt_session *s;
	OSStatus status = 0;
	CMSampleBufferRef sb = NULL;
	uint32_t info = 0;

	g_thread_ctx[idx].ok = 0;
	s = n2vtdt_session_create(CODEC_AVC1, g_thread_ctx[idx].fmt, &status);
	if (!s)
		return NULL;
	if (n2vtdt_sample_create(g_thread_ctx[idx].fmt, g_thread_ctx[idx].v->sample_data[0],
	                         g_thread_ctx[idx].v->sample_bytes[0], 1000, &sb) != 0) {
		n2vtdt_session_destroy(s);
		return NULL;
	}
	if (n2vtdt_session_decode(s, sb, 0, &info) == 0 && n2vtdt_session_emit_count(s) == 1 &&
	    n2vtdt_session_protocol_errors(s) == 0)
		g_thread_ctx[idx].ok = 1;
	CFRelease((CFTypeRef)sb);
	n2vtdt_session_destroy(s);
	return NULL;
}

static void test_threads(void)
{
	struct t_vector v;
	struct tfd_config cfg;
	pthread_t t[2];
	int i;

	if (load_vector("/tmp/n2hw-DEC06/vectors/avc-high.vec", &v) != 0)
		return;
	memset(&cfg, 0, sizeof(cfg));
	cfg.width = 176;
	cfg.height = 144;
	cfg.stride = 176;
	cfg.buf_h = 160;
	cfg.dpb = 4;
	cfg.format = N2VTD_FMT_MM21;
	cfg.in_bytes = 65536;
	cfg.auto_picture = 1;
	vector_geometry(&v, cfg.stride, cfg.buf_h, &cfg.out_bytes);
	tfd_install(&cfg);
	n2vtd_vt_availability_reset();
	for (i = 0; i < 2; i++) {
		g_thread_ctx[i].v = &v;
		g_thread_ctx[i].ok = 0;
		if (n2vtdt_format_with_atoms(v.codec, v.record, v.record_bytes, &g_thread_ctx[i].fmt) != 0)
			return;
	}
	pthread_create(&t[0], NULL, thread_decode, (void *)(intptr_t)0);
	pthread_create(&t[1], NULL, thread_decode, (void *)(intptr_t)1);
	pthread_join(t[0], NULL);
	pthread_join(t[1], NULL);
	CHECK(g_thread_ctx[0].ok == 1);
	CHECK(g_thread_ctx[1].ok == 1);
	for (i = 0; i < 2; i++)
		CFRelease((CFTypeRef)g_thread_ctx[i].fmt);
	t_vector_free(&v);
}

int main(void)
{
	printf("t_obj: decoder object, class record, registration and frames\n");
	n2vtdt_log_reset();
	test_registration();
	test_class_record();
	test_decode();
	test_invalidate_and_failures();
	test_unsupported();
	test_hevc_lazy();
	test_threads();
	/* The console channel (DEC08 rule 8): registration, session start and the first picture must say so. */
	CHECK(n2vtdt_log_contains("registered 2 hardware decoder"));
	CHECK(n2vtdt_log_contains("session start"));
	CHECK(n2vtdt_log_contains("first picture"));
	CHECK(n2vtdt_log_contains("avc1 decoder not offered"));   /* the deliberate connect-failure case */
	return t_summary("OBJ");
}
