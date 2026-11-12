/* SPDX-License-Identifier: MIT */
/*
 * t_vt.c -- host tests of vt/n2hwenc_vt.c against gpu/video's fake CoreFoundation/VideoToolbox runtime and a fake
 * vcodecd client (t_fakeclient.c) that plays back the access units of an ffmpeg-produced vector.
 *
 * Checked here: the create/start/property/encode/complete/invalidate life cycle, the pixel-buffer attributes announced,
 * the property -> OPEN/ENC_SET mapping, one emit per submitted frame with the exact sample bytes and attachments, the
 * format description's SPS/PPS(/VPS), the two fallback errors (no daemon -> kVTVideoEncoderNotAvailableNowErr, bad input
 * -> parameter error with the session still usable), the fatal hardware error path, registration info, and two sessions
 * on two threads for the TSan build.  The expected sample bytes and parameter sets come from the vector, which
 * mp4_extract.py took out of ffmpeg's MP4; no expectation is computed by the object's own code.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fake_apple.h"
#include "n2hwenc_annexb.h"
#include "n2hwenc_vt.h"
#include "t_fakeclient.h"
#include "t_support.h"

#define T_CODEC_AVC1 0x61766331u
#define T_CODEC_HVC1 0x68766331u
#define T_CODEC_HEV1 0x68657631u

static CVPixelBufferRef make_picture(uint32_t width, uint32_t height, uint32_t format, int seed)
{
	CVPixelBufferRef pb = NULL;
	size_t plane, rows, stride, x, y;
	int32_t st = CVPixelBufferCreate(kCFAllocatorDefault, width, height, format, NULL, &pb);

	if (st != 0 || pb == NULL)
		return NULL;
	if (CVPixelBufferLockBaseAddress(pb, 0) != 0) {
		CFRelease((CFTypeRef)pb);
		return NULL;
	}
	for (plane = 0; plane < CVPixelBufferGetPlaneCount(pb); plane++) {
		uint8_t *base = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, plane);
		stride = CVPixelBufferGetBytesPerRowOfPlane(pb, plane);
		rows = n2fake_pixel_buffer_plane_height(pb, plane);
		if (base == NULL) {
			(void)CVPixelBufferUnlockBaseAddress(pb, 0);
			CFRelease((CFTypeRef)pb);
			return NULL;
		}
		for (y = 0; y < rows; y++)
			for (x = 0; x < stride; x++)
				base[y * stride + x] = (uint8_t)(seed * 31 + (int)(x * 7) + (int)(y * 13) + (int)plane * 41);
	}
	(void)CVPixelBufferUnlockBaseAddress(pb, 0);
	return pb;
}

static int read_sample(CMSampleBufferRef sbuf, uint8_t *out, uint32_t capacity, uint32_t *bytes)
{
	CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sbuf);
	size_t length;

	if (bb == NULL)
		return -1;
	length = CMBlockBufferGetDataLength(bb);
	if (length > capacity)
		return -1;
	if (CMBlockBufferCopyDataBytes(bb, 0, length, out) != 0)
		return -1;
	*bytes = (uint32_t)length;
	return 0;
}

static int attachment_bool(CMSampleBufferRef sbuf, CFStringRef key, int *found)
{
	CFArrayRef arr = CMSampleBufferGetSampleAttachmentsArray(sbuf, 0);
	CFDictionaryRef dict;
	CFTypeRef value;

	*found = 0;
	if (arr == NULL || CFArrayGetCount(arr) < 1)
		return 0;
	dict = (CFDictionaryRef)CFArrayGetValueAtIndex(arr, 0);
	if (dict == NULL)
		return 0;
	value = CFDictionaryGetValue(dict, key);
	if (value == NULL || CFGetTypeID(value) != CFBooleanGetTypeID())
		return 0;
	*found = 1;
	return CFBooleanGetValue((CFBooleanRef)value) != 0;
}

/* ------------------------------------------------------------------ factories and registration */

static void test_factories(void)
{
	VTVideoEncoderRef enc = NULL;
	long live = n2fake_live_objects();

	CHECK_EQ(n2hwavc_vt_create(T_CODEC_HVC1, kCFAllocatorDefault, &enc), N2_kVTParameterErr);
	CHECK(enc == NULL);
	CHECK_EQ(n2hwhevc_vt_create(T_CODEC_AVC1, kCFAllocatorDefault, &enc), N2_kVTParameterErr);
	CHECK(enc == NULL);
	CHECK_EQ(n2hwavc_vt_create(0x78787878u, kCFAllocatorDefault, &enc), N2_kVTParameterErr);

	CHECK_EQ(n2hwavc_vt_create(T_CODEC_AVC1, kCFAllocatorDefault, &enc), 0);
	CHECK(enc != NULL);
	CFRelease((CFTypeRef)enc);
	CHECK_EQ(n2hwhevc_vt_create(T_CODEC_HVC1, kCFAllocatorDefault, &enc), 0);
	CHECK(enc != NULL);
	CFRelease((CFTypeRef)enc);
	CHECK_EQ(n2hwhevc_vt_create(T_CODEC_HEV1, kCFAllocatorDefault, &enc), 0);
	CHECK(enc != NULL);
	CFRelease((CFTypeRef)enc);
	CHECK_EQ(n2fake_live_objects(), live);
}

static void test_registration(void)
{
	uint32_t codec = 0, i;
	CFDictionaryRef info = NULL;
	n2_VTVideoEncoderCreateInstance fn = NULL;
	CFTypeRef value;

	n2fake_vt_reset();
	CHECK_EQ(n2hwenc_register_all(), 3);
	CHECK(n2fake_vt_registered(0, &codec, &info, &fn));
	CHECK_EQ(codec, T_CODEC_AVC1);
	CHECK(fn == n2hwavc_vt_create);
	if (info != NULL) {
		value = CFDictionaryGetValue(info, kVTVideoEncoder_IsHardwareAccelerated);
		CHECK(value == (CFTypeRef)kCFBooleanTrue);
		value = CFDictionaryGetValue(info, kVTVideoEncoder_Rating);
		CHECK(value != NULL && CFGetTypeID(value) == CFNumberGetTypeID());
		if (value != NULL) {
			int64_t rating = 0;
			CFNumberGetValue((CFNumberRef)value, kCFNumberSInt64Type, &rating);
			CHECK_EQ(rating, N2HWENC_RATING);
		}
		value = CFDictionaryGetValue(info, N2STR("CMClassImplementationID"));
		CHECK(value != NULL && CFGetTypeID(value) == CFStringGetTypeID());
		if (value != NULL) {
			char text[128];
			CHECK(CFStringGetCString((CFStringRef)value, text, sizeof(text), kCFStringEncodingUTF8));
			CHECK_EQ(strcmp(text, N2HWENC_ID_AVC), 0);
		}
	} else {
		CHECK(0);
	}
	for (i = 1; i < 3; i++) {
		CHECK(n2fake_vt_registered((int)i, &codec, &info, &fn));
		CHECK(codec == (i == 1 ? T_CODEC_HVC1 : T_CODEC_HEV1));
		CHECK(fn == n2hwhevc_vt_create);
	}
	n2fake_vt_reset();
}

/* The weak P03 symbols are absent from this host link: the default client must report "not available now", which is
 * what lets VideoToolbox keep the software encoder registered for the same codec type. */
static void test_no_client(const struct t_vector *v)
{
	struct t_fake_client *f = t_fake_new(v, NULL);
	struct n2fake_session *s;
	const n2_VTVideoEncoderVTable *vt;
	VTVideoEncoderRef enc = NULL;
	CMVideoDimensions dims = { (int32_t)v->width, (int32_t)v->height };

	CHECK(f != NULL);
	t_fake_install(f);
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	vt = s != NULL ? n2fake_session_vtable(s) : NULL;
	n2fake_session_destroy(s);
	t_fake_uninstall();
	t_fake_free(f);
	CHECK(vt != NULL);
	if (vt == NULL)
		return;

	CHECK_EQ(n2hwavc_vt_create(T_CODEC_AVC1, kCFAllocatorDefault, &enc), 0);
	CHECK(enc != NULL);
	CHECK_EQ(vt->encoderClass->startSession(enc, (VTVideoEncoderSession)(uintptr_t)0x1000, dims),
	         N2_kVTVideoEncoderNotAvailableNowErr);
	CFRelease((CFTypeRef)enc);
	CHECK_EQ(n2hwenc_vt_available(), 0);
}

/* ------------------------------------------------------------------ one codec's stream */

struct stream_result {
	int ok;
	uint32_t emits;
	uint32_t checked_samples;
};

static void run_stream(const struct t_vector *v, uint32_t codec, n2_VTVideoEncoderCreateInstance create,
                       struct stream_result *result)
{
	struct t_fake_client *f;
	struct n2fake_session *s;
	CFDictionaryRef attrs;
	const struct n2hwenc_params *params;
	long live0;
	uint32_t i;
	int found;

	result->ok = 0;
	result->emits = 0;
	result->checked_samples = 0;

	f = t_fake_new(codec == T_CODEC_AVC1 ? v : NULL, codec == T_CODEC_AVC1 ? NULL : v);
	if (f == NULL) {
		CHECK(0);
		return;
	}
	t_fake_install(f);
	live0 = n2fake_live_objects();
	s = n2fake_session_create(create, codec, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s == NULL) {
		t_fake_uninstall();
		t_fake_free(f);
		return;
	}
	CHECK_EQ(t_fake_opens(f), 1);
	params = t_fake_open_params(f, 0);
	CHECK(params != NULL);
	if (params != NULL) {
		CHECK_EQ(params->codec, codec == T_CODEC_AVC1 ? 0x31637661u : 0x31637668u);
		CHECK_EQ(params->width, v->width);
		CHECK_EQ(params->height, v->height);
		CHECK_EQ(params->fps_num, 30);
		CHECK_EQ(params->fps_den, 1);
		CHECK_EQ(params->bitrate, 0);
		CHECK_EQ(params->gop, 0);
		CHECK_EQ(params->profile, 0);
		CHECK_EQ(params->level, 0);
		CHECK_EQ(params->flags, 0);
	}

	attrs = n2fake_session_pixel_attributes(s);
	CHECK(attrs != NULL);
	if (attrs != NULL) {
		CFArrayRef formats = (CFArrayRef)CFDictionaryGetValue(attrs, kCVPixelBufferPixelFormatTypeKey);
		CHECK(formats != NULL);
		if (formats != NULL) {
			int have_v = 0, have_f = 0;
			CFIndex n = CFArrayGetCount(formats), k;
			for (k = 0; k < n; k++) {
				int32_t pf = 0;
				CFNumberGetValue((CFNumberRef)CFArrayGetValueAtIndex(formats, k), kCFNumberSInt32Type, &pf);
				if ((uint32_t)pf == N2_PIXFMT_420V)
					have_v = 1;
				if ((uint32_t)pf == N2_PIXFMT_420F)
					have_f = 1;
			}
			CHECK(have_v && have_f);
		}
	}

	for (i = 0; i < v->frame_count; i++) {
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, (int)i);
		CMTime pts = { (int64_t)i, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CMSampleBufferRef sbuf = NULL;
		OSStatus status = 0;
		uint32_t flags = 1, bytes = 0;
		uint8_t payload[65536];
		void *token = NULL;

		CHECK(pb != NULL);
		if (pb == NULL)
			break;
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CFRelease((CFTypeRef)pb);

		CHECK(n2fake_session_emitted(s, (int)i, &status, &flags, &sbuf, &token));
		CHECK_EQ(status, 0);
		CHECK_EQ(flags, 0);
		CHECK(token != NULL);
		CHECK(sbuf != NULL);
		if (sbuf != NULL) {
			CMSampleTimingInfo timing;
			size_t sample_size = 0;
			CHECK(n2fake_sample_timing(sbuf, &timing, &sample_size));
			CHECK_EQ(timing.presentationTimeStamp.value, (int64_t)i);
			CHECK_EQ(timing.duration.value, 1);
			CHECK_EQ(timing.decodeTimeStamp.value, (int64_t)i);
			CHECK_EQ(sample_size, v->frames[i].avcc_bytes);
			CHECK_EQ(read_sample(sbuf, payload, sizeof(payload), &bytes), 0);
			CHECK_EQ(bytes, v->frames[i].avcc_bytes);
			if (bytes == v->frames[i].avcc_bytes && bytes > 0) {
				CHECK_EQ(memcmp(payload, v->frames[i].avcc, bytes), 0);
				result->checked_samples++;
			}

			/* Key frame attachments: NotSync and DependsOnOthers are false on a key frame, true otherwise. */
			{
				int not_sync = attachment_bool(sbuf, kCMSampleAttachmentKey_NotSync, &found);
				CHECK_EQ(found, 1);
				if (found)
					CHECK_EQ(not_sync, v->frames[i].key ? 0 : 1);
			}
			{
				int depends = attachment_bool(sbuf, kCMSampleAttachmentKey_DependsOnOthers, &found);
				CHECK_EQ(found, 1);
				if (found)
					CHECK_EQ(depends, v->frames[i].key ? 0 : 1);
			}

			/* The format description carries exactly the parameter sets of the vector. */
			if (i == 0) {
				CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sbuf);
				size_t count = 0;
				int nal = 0;
				CHECK(fmt != NULL);
				if (fmt != NULL) {
					uint32_t k;
					for (k = 0; k < v->param_count; k++) {
						const uint8_t *ps = NULL;
						size_t ps_size = 0;
						OSStatus st;
						if (v->codec == N2HWENC_AVC)
							st = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, k, &ps, &ps_size,
							                                                       &count, &nal);
						else
							st = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, k, &ps, &ps_size,
							                                                        &count, &nal);
						CHECK_EQ(st, 0);
						CHECK_EQ(ps_size, v->params[k].bytes);
						if (ps != NULL && ps_size == v->params[k].bytes)
							CHECK_EQ(memcmp(ps, v->params[k].data, ps_size), 0);
					}
					CHECK_EQ(count, v->param_count);
					CHECK_EQ(nal, 4);
				}
			}
		}
		result->emits++;
	}
	CHECK_EQ(n2fake_session_complete(s), 0);
	CHECK_EQ(n2fake_session_protocol_errors(s), 0);
	CHECK_EQ(t_fake_frames(f), (int)v->frame_count);
	n2fake_session_destroy(s);
	CHECK_EQ(t_fake_closes(f), 1);
	CHECK_EQ(t_fake_disconnects(f), 1);
	CHECK_EQ(n2fake_live_objects(), live0);
	t_fake_uninstall();
	t_fake_free(f);
	result->ok = 1;
}

/* ------------------------------------------------------------------ properties */

static void test_properties(const struct t_vector *v)
{
	struct t_fake_client *f;
	struct n2fake_session *s;
	const struct n2hwenc_params *p;
	const struct t_fake_set *set;
	CFTypeRef value = NULL;
	CFDictionaryRef dict = NULL;
	CFDictionaryRef entry;
	int32_t number = 2000000;
	long live0;

	f = t_fake_new(v, NULL);
	CHECK(f != NULL);
	t_fake_install(f);
	live0 = n2fake_live_objects();
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s == NULL) {
		t_fake_uninstall();
		t_fake_free(f);
		return;
	}

	/* opus-cam: before the first picture AverageBitRate/ExpectedFrameRate send NO ENC_SET (it never ran on the phone);
	 * they are folded into the first-picture reopen, whose OPEN carries both. */
	{
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &number);
		CHECK(n != NULL);
		CHECK_EQ(n2fake_session_set_property(s, N2STR("AverageBitRate"), (CFTypeRef)n), 0);
		CFRelease((CFTypeRef)n);
	}
	CHECK_EQ(t_fake_sets(f), 0);
	{
		double fps = 24.0;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &fps);
		CHECK(n != NULL);
		CHECK_EQ(n2fake_session_set_property(s, N2STR("ExpectedFrameRate"), (CFTypeRef)n), 0);
		CFRelease((CFTypeRef)n);
	}
	CHECK_EQ(t_fake_sets(f), 0);

	/* Structural properties before the first picture force one reopen with all of them. */
	{
		int32_t gop = 12;
		int32_t prof = 0;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &gop);
		(void)prof;
		CHECK_EQ(n2fake_session_set_property(s, N2STR("MaxKeyFrameInterval"), (CFTypeRef)n), 0);
		CFRelease((CFTypeRef)n);
	}
	CHECK_EQ(n2fake_session_set_property(s, N2STR("ProfileLevel"), N2STR("H264_High_4_1")), 0);
	CHECK_EQ(n2fake_session_set_property(s, N2STR("RealTime"), kCFBooleanTrue), 0);
	{
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 0);
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };

		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CFRelease((CFTypeRef)pb);
	}
	CHECK_EQ(t_fake_opens(f), 2);
	CHECK_EQ(t_fake_closes(f), 1);
	p = t_fake_open_params(f, 1);
	CHECK(p != NULL);
	if (p != NULL) {
		CHECK_EQ(p->width, v->width);
		CHECK_EQ(p->height, v->height);
		CHECK_EQ(p->bitrate, 2000000);
		CHECK_EQ(p->fps_num, 24);
		CHECK_EQ(p->fps_den, 1);
		CHECK_EQ(p->gop, 12);
		CHECK_EQ(p->profile, 100);
		CHECK_EQ(p->level, 41);
		CHECK_EQ(p->flags, 0);              /* RealTime is never sent: vcodecd refuses OPEN.flags != 0 (cam1-prod) */
	}

	/* After the first picture a bitrate change goes out as ENC_SET with the current frame rate. */
	{
		int32_t rate = 3000000;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &rate);
		CHECK_EQ(n2fake_session_set_property(s, N2STR("AverageBitRate"), (CFTypeRef)n), 0);
		CFRelease((CFTypeRef)n);
	}
	CHECK_EQ(t_fake_sets(f), 1);
	set = t_fake_set_params(f, 0);
	CHECK(set != NULL && set->bitrate == 3000000 && set->fps_num == 24 && set->fps_den == 1 && set->force_key == 0);
	/* A refused ENC_SET is not fatal: the property is accepted, the next picture still encodes. */
	t_fake_fail_set(f, 1);
	{
		int32_t rate = 4000000;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &rate);
		CHECK_EQ(n2fake_session_set_property(s, N2STR("AverageBitRate"), (CFTypeRef)n), 0);
		CFRelease((CFTypeRef)n);
	}
	t_fake_fail_set(f, 0);
	CHECK_EQ(t_fake_sets(f), 2);
	{
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 1);
		CMTime pts = { 1, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		OSStatus st = -1;
		CMSampleBufferRef sb = NULL;
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CFRelease((CFTypeRef)pb);
		CHECK(n2fake_session_emitted(s, 1, &st, NULL, &sb, NULL));
		CHECK_EQ(st, 0);
		CHECK(sb != NULL);
	}

	/* A structural change after the first picture is accepted but does not restart the encoder. */
	{
		int32_t gop = 30;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &gop);
		CHECK_EQ(n2fake_session_set_property(s, N2STR("MaxKeyFrameInterval"), (CFTypeRef)n), 0);
		CFRelease((CFTypeRef)n);
	}
	CHECK_EQ(t_fake_opens(f), 2);

	/* Read-only and read-back properties. */
	CHECK_EQ(n2fake_session_copy_property(s, N2STR("UsingHardwareAcceleratedVideoEncoder"), &value), 0);
	CHECK(value == (CFTypeRef)kCFBooleanTrue);
	CFRelease(value);
	value = NULL;
	CHECK_EQ(n2fake_session_copy_property(s, N2STR("NumberOfPendingFrames"), &value), 0);
	{
		int32_t pending = -1;
		CHECK(value != NULL && CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &pending));
		CHECK_EQ(pending, 0);
	}
	CFRelease(value);
	value = NULL;
	CHECK_EQ(n2fake_session_copy_property(s, N2STR("AverageBitRate"), &value), 0);
	{
		int32_t rate = 0;
		CHECK(value != NULL && CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &rate));
		CHECK_EQ(rate, 4000000);
	}
	CFRelease(value);
	value = NULL;
	CHECK_EQ(n2fake_session_copy_property(s, N2STR("ProfileLevel"), &value), 0);
	CFRelease(value);
	value = NULL;
	CHECK_EQ(n2fake_session_copy_property(s, N2STR("NoSuchProperty"), &value), N2_kCMBaseObjectError_ValueNotAvailable);
	CHECK(value == NULL);
	CHECK_EQ(n2fake_session_set_property(s, N2STR("UsingHardwareAcceleratedVideoEncoder"), kCFBooleanTrue),
	         N2_kVTPropertyNotSupportedErr);

	/* Unknown keys are kept (a recording must not stop); wrong types are refused. */
	{
		int32_t one = 1;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &one);
		CHECK_EQ(n2fake_session_set_property(s, N2STR("FigThreadPriority"), (CFTypeRef)n), 0);
		CFRelease((CFTypeRef)n);
		value = NULL;
		CHECK_EQ(n2fake_session_copy_property(s, N2STR("FigThreadPriority"), &value), 0);
		CHECK(value != NULL && CFGetTypeID(value) == CFNumberGetTypeID());
		CFRelease(value);
		value = NULL;
	}
	CHECK_EQ(n2fake_session_set_property(s, N2STR("AverageBitRate"), N2STR("not a number")), N2_kVTParameterErr);
	CHECK_EQ(n2fake_session_set_property(s, N2STR("RealTime"), N2STR("not a boolean")), N2_kVTParameterErr);

	/* Supported-property dictionary: one entry per key, ReadWriteStatus and PropertyType present. */
	{
		const n2_VTVideoEncoderVTable *vt = n2fake_session_vtable(s);
		CHECK(vt != NULL && vt->encoderClass != NULL && vt->encoderClass->copySupportedPropertyDictionary != NULL);
		if (vt != NULL && vt->encoderClass->copySupportedPropertyDictionary != NULL) {
			CHECK_EQ(vt->encoderClass->copySupportedPropertyDictionary(n2fake_session_encoder(s), &dict), 0);
			CHECK(dict != NULL);
			if (dict != NULL) {
				CHECK(n2fake_dictionary_count(dict) >= 25);
				entry = (CFDictionaryRef)CFDictionaryGetValue(dict, N2STR("AverageBitRate"));
				CHECK(entry != NULL);
				if (entry != NULL) {
					CFTypeRef t = CFDictionaryGetValue(entry, N2STR("PropertyType"));
					CHECK(t != NULL && CFEqual(t, N2STR("Number")));
					t = CFDictionaryGetValue(entry, N2STR("ReadWriteStatus"));
					CHECK(t != NULL && CFEqual(t, N2STR("ReadWrite")));
				}
				entry = (CFDictionaryRef)CFDictionaryGetValue(dict, N2STR("UsingHardwareAcceleratedVideoEncoder"));
				CHECK(entry != NULL);
				if (entry != NULL) {
					CFTypeRef t = CFDictionaryGetValue(entry, N2STR("ReadWriteStatus"));
					CHECK(t != NULL && CFEqual(t, N2STR("ReadOnly")));
				}
				CFRelease((CFTypeRef)dict);
			}
		}
	}

	n2fake_session_destroy(s);
	CHECK_EQ(n2fake_live_objects(), live0);
	t_fake_uninstall();
	t_fake_free(f);
}

/* ------------------------------------------------------------------ failure paths */

static void test_failures(const struct t_vector *v)
{
	struct t_fake_client *f;
	struct n2fake_session *s;
	const n2_VTVideoEncoderVTable *vt = NULL;
	CMVideoDimensions dims = { (int32_t)v->width, (int32_t)v->height };
	long live0;

	f = t_fake_new(v, NULL);
	CHECK(f != NULL);
	t_fake_install(f);
	live0 = n2fake_live_objects();

	/* A first session gives us the static vtable for the direct startSession tests below. */
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s != NULL) {
		vt = n2fake_session_vtable(s);
		CHECK(vt != NULL && vt->encoderClass != NULL && vt->encoderClass->startSession != NULL);
		n2fake_session_destroy(s);
	}
	CHECK(vt != NULL);

	/* No daemon: the documented "encoder not available now" status, and the object is cleanly releasable. */
	t_fake_fail_connect(f, 1);
	if (vt != NULL) {
		VTVideoEncoderRef enc = NULL;
		CHECK_EQ(n2hwavc_vt_create(T_CODEC_AVC1, kCFAllocatorDefault, &enc), 0);
		CHECK(enc != NULL);
		CHECK_EQ(vt->encoderClass->startSession(enc, (VTVideoEncoderSession)(uintptr_t)0x1000, dims),
		         N2_kVTVideoEncoderNotAvailableNowErr);
		CFRelease((CFTypeRef)enc);
	}
	t_fake_fail_connect(f, 0);

	/* The daemon answers HELLO but refuses OPEN: same fallback status, and the connection is closed. */
	{
		int disconnects = t_fake_disconnects(f);
		VTVideoEncoderRef enc = NULL;
		t_fake_fail_open(f, 1);
		CHECK_EQ(n2hwavc_vt_create(T_CODEC_AVC1, kCFAllocatorDefault, &enc), 0);
		CHECK_EQ(vt->encoderClass->startSession(enc, (VTVideoEncoderSession)(uintptr_t)0x1000, dims),
		         N2_kVTVideoEncoderNotAvailableNowErr);
		CHECK_EQ(t_fake_disconnects(f), disconnects + 1);
		CFRelease((CFTypeRef)enc);
		t_fake_fail_open(f, 0);
	}

	/* Parameter checks of startSession. */
	if (vt != NULL) {
		VTVideoEncoderRef enc = NULL;
		CMVideoDimensions bad = { 0, 0 };
		CHECK_EQ(n2hwavc_vt_create(T_CODEC_AVC1, kCFAllocatorDefault, &enc), 0);
		CHECK_EQ(vt->encoderClass->startSession(enc, NULL, dims), N2_kVTParameterErr);
		CHECK_EQ(vt->encoderClass->startSession(enc, (VTVideoEncoderSession)(uintptr_t)0x1000, bad),
		         N2_kVTParameterErr);
		bad.width = (int32_t)v->width + 1;
		bad.height = (int32_t)v->height;
		CHECK_EQ(vt->encoderClass->startSession(enc, (VTVideoEncoderSession)(uintptr_t)0x1000, bad),
		         N2_kVTParameterErr);
		CFRelease((CFTypeRef)enc);
	}

	/* Bad pictures: one emit with the parameter error, NULL sample, and the session stays usable. */
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s != NULL) {
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CVPixelBufferRef pb;
		CMSampleBufferRef sbuf = NULL;
		OSStatus status = 0;
		uint32_t flags = 1;

		pb = make_picture(v->width, v->height, N2_PIXFMT_I420, 1);
		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), N2_kVTParameterErr);
		CFRelease((CFTypeRef)pb);
		CHECK(n2fake_session_emitted(s, 0, &status, &flags, &sbuf, NULL));
		CHECK_EQ(status, N2_kVTParameterErr);
		CHECK(sbuf == NULL);

		pb = make_picture(v->width - 16, v->height, N2_PIXFMT_420V, 2);
		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), N2_kVTParameterErr);
		CFRelease((CFTypeRef)pb);
		CHECK(n2fake_session_emitted(s, 1, &status, &flags, &sbuf, NULL));
		CHECK_EQ(status, N2_kVTParameterErr);
		CHECK(sbuf == NULL);
		CHECK_EQ(n2fake_session_protocol_errors(s), 0);

		/* A good picture after the bad ones still works, and is a key frame (first picture of the stream). */
		pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 3);
		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CFRelease((CFTypeRef)pb);
		CHECK(n2fake_session_emitted(s, 2, &status, &flags, &sbuf, NULL));
		CHECK_EQ(status, 0);
		CHECK(sbuf != NULL);
		CHECK_EQ(t_fake_frames(f), 1);          /* the bad pictures never reached the client */
		n2fake_session_destroy(s);
	}

	/* A hardware failure: one emit with the malfunction error, then the session is fatal. */
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s != NULL) {
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 4);
		CMSampleBufferRef sbuf = NULL;
		OSStatus status = 0;
		uint32_t flags = 1;

		t_fake_fail_frame(f, 0);
		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), N2_kVTVideoEncoderMalfunctionErr);
		CHECK(n2fake_session_emitted(s, 0, &status, &flags, &sbuf, NULL));
		CHECK_EQ(status, N2_kVTVideoEncoderMalfunctionErr);
		CHECK(sbuf == NULL);
		/* the next frame is refused before any client call, still with exactly one emit */
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), N2_kVTVideoEncoderMalfunctionErr);
		CHECK(n2fake_session_emitted(s, 1, &status, &flags, &sbuf, NULL));
		CHECK_EQ(status, N2_kVTVideoEncoderMalfunctionErr);
		CFRelease((CFTypeRef)pb);
		CHECK_EQ(n2fake_session_protocol_errors(s), 0);
		n2fake_session_destroy(s);
	}

	/* No parameter sets in the first key frame: the format description cannot be built. */
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s != NULL) {
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 5);
		CMSampleBufferRef sbuf = NULL;
		OSStatus status = 0;
		uint32_t flags = 1;

		t_fake_no_params(f, 1);
		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), N2_kVTVideoEncoderMalfunctionErr);
		CHECK(n2fake_session_emitted(s, 0, &status, &flags, &sbuf, NULL));
		CHECK_EQ(status, N2_kVTVideoEncoderMalfunctionErr);
		CFRelease((CFTypeRef)pb);
		t_fake_no_params(f, 0);
		n2fake_session_destroy(s);
	}

	CHECK_EQ(n2fake_live_objects(), live0);
	t_fake_uninstall();
	t_fake_free(f);
}

/* ------------------------------------------------------------------ invalidate */

static void test_invalidate(const struct t_vector *v)
{
	struct t_fake_client *f = t_fake_new(v, NULL);
	struct n2fake_session *s;
	const n2_VTVideoEncoderVTable *vt;
	long live0;

	CHECK(f != NULL);
	t_fake_install(f);
	live0 = n2fake_live_objects();
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s == NULL) {
		t_fake_uninstall();
		t_fake_free(f);
		return;
	}
	{
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 0);
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CFRelease((CFTypeRef)pb);
	}
	vt = n2fake_session_vtable(s);
	CHECK(vt != NULL && vt->baseClass != NULL && vt->baseClass->invalidate != NULL);
	if (vt != NULL && vt->baseClass->invalidate != NULL) {
		CHECK_EQ(vt->baseClass->invalidate((CMBaseObjectRef)n2fake_session_encoder(s)), 0);
		CHECK_EQ(vt->baseClass->invalidate((CMBaseObjectRef)n2fake_session_encoder(s)), 0);   /* idempotent */
	}
	CHECK_EQ(t_fake_closes(f), 1);
	CHECK_EQ(t_fake_disconnects(f), 1);
	{
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 1);
		CMTime pts = { 1, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CHECK(pb != NULL);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), N2_kVTInvalidSessionErr);
		CFRelease((CFTypeRef)pb);
		CHECK_EQ(n2fake_session_protocol_errors(s), 0);
	}
	n2fake_session_destroy(s);
	CHECK_EQ(n2fake_live_objects(), live0);
	t_fake_uninstall();
	t_fake_free(f);
}

/* ------------------------------------------------------------------ two sessions, two threads */

struct thread_arg {
	const struct t_vector *v;
	n2_VTVideoEncoderCreateInstance create;
	uint32_t               codec;
	int                    ok;
};

static void *thread_body(void *opaque)
{
	struct thread_arg *a = (struct thread_arg *)opaque;
	struct n2fake_session *s;
	uint32_t i;

	s = n2fake_session_create(a->create, a->codec, (int)a->v->width, (int)a->v->height);
	if (s == NULL)
		return NULL;
	a->ok = 1;
	for (i = 0; i < a->v->frame_count && i < 10; i++) {
		CVPixelBufferRef pb = make_picture(a->v->width, a->v->height, N2_PIXFMT_420V, (int)i);
		CMTime pts = { (int64_t)i, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CMSampleBufferRef sbuf = NULL;
		OSStatus status = 0;
		uint32_t flags = 0, bytes = 0;
		uint8_t payload[65536];

		if (pb == NULL) {
			a->ok = 0;
			break;
		}
		if (n2fake_session_encode(s, pb, pts, duration, NULL) != 0)
			a->ok = 0;
		CFRelease((CFTypeRef)pb);
		if (!n2fake_session_emitted(s, (int)i, &status, &flags, &sbuf, NULL) || status != 0 || sbuf == NULL) {
			a->ok = 0;
			continue;
		}
		if (read_sample(sbuf, payload, sizeof(payload), &bytes) != 0 ||
		    bytes != a->v->frames[i].avcc_bytes ||
		    memcmp(payload, a->v->frames[i].avcc, bytes) != 0)
			a->ok = 0;
	}
	if (n2fake_session_protocol_errors(s) != 0)
		a->ok = 0;
	n2fake_session_destroy(s);
	return NULL;
}

static void test_threads(const struct t_vector *v, const struct t_vector *hevc)
{
	struct t_fake_client *f;
	struct thread_arg avc, hevc_arg;
	pthread_t t1, t2;
	long live0;

	f = t_fake_new(v, hevc);
	CHECK(f != NULL);
	t_fake_install(f);
	live0 = n2fake_live_objects();
	avc.v = v;
	avc.create = n2hwavc_vt_create;
	avc.codec = T_CODEC_AVC1;
	avc.ok = 0;
	hevc_arg.v = hevc;
	hevc_arg.create = n2hwhevc_vt_create;
	hevc_arg.codec = T_CODEC_HVC1;
	hevc_arg.ok = 0;
	CHECK_EQ(pthread_create(&t1, NULL, thread_body, &avc), 0);
	CHECK_EQ(pthread_create(&t2, NULL, thread_body, &hevc_arg), 0);
	CHECK_EQ(pthread_join(t1, NULL), 0);
	CHECK_EQ(pthread_join(t2, NULL), 0);
	CHECK_EQ(avc.ok, 1);
	CHECK_EQ(hevc_arg.ok, 1);
	CHECK_EQ(n2fake_live_objects(), live0);
	t_fake_uninstall();
	t_fake_free(f);
}

/* ------------------------------------------------------------------ availability probe */

static void test_available(const struct t_vector *v)
{
	struct t_fake_client *f = t_fake_new(v, NULL);

	CHECK(f != NULL);
	t_fake_install(f);
	CHECK_EQ(n2hwenc_vt_available(), 1);
	t_fake_fail_connect(f, 1);
	CHECK_EQ(n2hwenc_vt_available(), 0);
	t_fake_fail_connect(f, 0);
	t_fake_uninstall();
	t_fake_free(f);
}

/* ------------------------------------------------------------------ opus-cam: pipeline, repack, retries */

static void picture_hashes(CVPixelBufferRef pb, uint32_t w, uint32_t h, uint64_t *hy, uint64_t *hc)
{
	const uint8_t *y, *c;
	size_t sy, sc;
	uint32_t r, x;

	*hy = *hc = 1469598103934665603ull;
	CVPixelBufferLockBaseAddress(pb, 0);
	y = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
	c = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
	sy = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
	sc = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
	for (r = 0; r < h; r++)
		for (x = 0; x < w; x++)
			*hy = (*hy ^ y[(size_t)r * sy + x]) * 1099511628211ull;
	for (r = 0; r < h / 2; r++)
		for (x = 0; x < w; x++)
			*hc = (*hc ^ c[(size_t)r * sc + x]) * 1099511628211ull;
	CVPixelBufferUnlockBaseAddress(pb, 0);
}

/* The daemon always receives the session stride (align16(width)) and exactly the picture's visible bytes, both when the
 * CVPixelBuffer pitch differs (fake: round64(176) = 192 -> repacked) and when it matches (128 -> direct). */
static void test_strides(const struct t_vector *v)
{
	static const uint32_t widths[2] = { 0, 128 };
	int k;

	for (k = 0; k < 2; k++) {
		uint32_t w = widths[k] ? widths[k] : v->width, h = v->height;
		struct t_fake_client *f = t_fake_new(v, NULL);
		struct n2fake_session *s;
		CVPixelBufferRef pb;
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		uint64_t ey, ec, gy = 0, gc = 0;
		const struct t_fake_frame_args *a;

		t_fake_install(f);
		s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)w, (int)h);
		CHECK(s != NULL);
		if (s == NULL) {
			t_fake_uninstall();
			t_fake_free(f);
			continue;
		}
		pb = make_picture(w, h, N2_PIXFMT_420V, 7 + k);
		picture_hashes(pb, w, h, &ey, &ec);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CHECK_EQ(n2fake_session_complete(s), 0);
		a = t_fake_last_frame(f);
		CHECK_EQ(a->luma_stride, (w + 15u) & ~15u);
		CHECK_EQ(a->chroma_stride, (w + 15u) & ~15u);
		t_fake_hashes(f, &gy, &gc);
		CHECK(gy == ey);
		CHECK(gc == ec);
		CFRelease((CFTypeRef)pb);
		n2fake_session_destroy(s);
		t_fake_uninstall();
		t_fake_free(f);
	}
}

/* The pipelined object: encodeFrame returns before its picture is encoded, emits happen in submission order on the
 * calling thread (inside later encodeFrame calls or completeFrames), each token exactly once, samples exact. */
static void test_async_stream(const struct t_vector *v, uint32_t codec, n2_VTVideoEncoderCreateInstance create,
                              int in_flight)
{
	struct t_fake_client *f = t_fake_new(codec == T_CODEC_AVC1 ? v : NULL, codec == T_CODEC_AVC1 ? NULL : v);
	struct n2fake_session *s;
	long live0;
	uint32_t i;
	int deferred = 0;

	n2hwenc_set_max_in_flight(in_flight);
	t_fake_install(f);
	t_fake_delay(f, 3000);                      /* 3 ms per picture: frames really are in flight */
	live0 = n2fake_live_objects();
	s = n2fake_session_create(create, codec, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s == NULL)
		goto out;
	for (i = 0; i < v->frame_count; i++) {
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, (int)i);
		CMTime pts = { (int64_t)i * 100, 3000, N2_CMTIME_VALID, 0 };
		CMTime duration = { 100, 3000, N2_CMTIME_VALID, 0 };
		CFTypeRef pending = NULL;
		int32_t n = -1;

		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CFRelease((CFTypeRef)pb);
		/* never more than in_flight outstanding, and some emits really are deferred */
		CHECK_EQ(n2fake_session_copy_property(s, N2STR("NumberOfPendingFrames"), &pending), 0);
		if (pending != NULL) {
			CFNumberGetValue((CFNumberRef)pending, kCFNumberSInt32Type, &n);
			CFRelease(pending);
		}
		CHECK(n >= 0 && n <= in_flight);
		if (n > 0)
			deferred++;
	}
	CHECK_EQ(n2fake_session_complete(s), 0);
	if (in_flight > 1)
		CHECK(deferred > 0);
	for (i = 0; i < v->frame_count; i++) {
		OSStatus st = -1;
		uint32_t flags = 7, bytes = 0;
		CMSampleBufferRef sb = NULL;
		void *token = NULL;
		uint8_t payload[65536];

		CHECK(n2fake_session_emitted(s, (int)i, &st, &flags, &sb, &token));
		CHECK_EQ(st, 0);
		CHECK(sb != NULL);
		if (sb != NULL) {
			CMSampleTimingInfo timing;
			size_t sz = 0;
			int found = 0;
			CHECK(n2fake_sample_timing(sb, &timing, &sz));
			CHECK_EQ(timing.presentationTimeStamp.value, (int64_t)i * 100);
			CHECK_EQ(timing.decodeTimeStamp.value, (int64_t)i * 100);
			CHECK_EQ(timing.duration.value, 100);
			CHECK_EQ(read_sample(sb, payload, sizeof(payload), &bytes), 0);
			CHECK_EQ(bytes, v->frames[i].avcc_bytes);
			if (bytes == v->frames[i].avcc_bytes)
				CHECK_EQ(memcmp(payload, v->frames[i].avcc, bytes), 0);
			CHECK_EQ(attachment_bool(sb, kCMSampleAttachmentKey_NotSync, &found), v->frames[i].key ? 0 : 1);
		}
	}
	CHECK(!n2fake_session_emitted(s, (int)v->frame_count, NULL, NULL, NULL, NULL));
	CHECK_EQ(n2fake_session_protocol_errors(s), 0);
	CHECK_EQ(t_fake_frames(f), (int)v->frame_count);
	n2fake_session_destroy(s);
	CHECK_EQ(t_fake_closes(f), 1);
	CHECK_EQ(n2fake_live_objects(), live0);
out:
	t_fake_uninstall();
	t_fake_free(f);
	n2hwenc_set_max_in_flight(1);
}

/* Pipelined failure, invalidate with pictures in flight, and a client that does not answer within the bound. */
static void test_async_failures(const struct t_vector *v)
{
	struct t_fake_client *f = t_fake_new(v, NULL);
	struct n2fake_session *s;
	long live0 = n2fake_live_objects();
	uint32_t i, frames = 8;
	CMTime duration = { 1, 30, N2_CMTIME_VALID, 0 };

	n2hwenc_set_max_in_flight(2);
	t_fake_install(f);

	/* ENC_FRAME fails at picture 3: 0..2 succeed, 3 and everything after are malfunction emits, one per token. */
	t_fake_delay(f, 2000);
	t_fake_fail_frame(f, 3);
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s != NULL) {
		for (i = 0; i < frames; i++) {
			CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, (int)i);
			CMTime pts = { (int64_t)i, 30, N2_CMTIME_VALID, 0 };
			(void)n2fake_session_encode(s, pb, pts, duration, NULL);
			CFRelease((CFTypeRef)pb);
		}
		CHECK_EQ(n2fake_session_complete(s), 0);
		for (i = 0; i < frames; i++) {
			OSStatus st = 1;
			CMSampleBufferRef sb = NULL;
			CHECK(n2fake_session_emitted(s, (int)i, &st, NULL, &sb, NULL));
			if (i < 3) {
				CHECK_EQ(st, 0);
				CHECK(sb != NULL);
			} else {
				CHECK_EQ(st, N2_kVTVideoEncoderMalfunctionErr);
				CHECK(sb == NULL);
			}
		}
		CHECK(!n2fake_session_emitted(s, (int)frames, NULL, NULL, NULL, NULL));
		CHECK_EQ(n2fake_session_protocol_errors(s), 0);
		n2fake_session_destroy(s);
	}
	t_fake_fail_frame(f, -1);

	/* Invalidate with two pictures outstanding: no emit after it, every picture released, the session closed. */
	t_fake_delay(f, 20000);
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s != NULL) {
		const n2_VTVideoEncoderVTable *vt = n2fake_session_vtable(s);
		int closes = t_fake_closes(f);
		for (i = 0; i < 2; i++) {
			CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, (int)i);
			CMTime pts = { (int64_t)i, 30, N2_CMTIME_VALID, 0 };
			CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
			CFRelease((CFTypeRef)pb);
		}
		CHECK_EQ(vt->baseClass->invalidate((CMBaseObjectRef)n2fake_session_encoder(s)), 0);
		CHECK_EQ(t_fake_closes(f), closes + 1);
		CHECK(!n2fake_session_emitted(s, 1, NULL, NULL, NULL, NULL));
		CHECK_EQ(n2fake_session_protocol_errors(s), 0);
		n2fake_session_destroy(s);
	}

	/* A picture that takes longer than N2HWENC_WAIT_MS (2.5 s): completeFrames returns within the bound with a
	 * malfunction emit for every outstanding token; the late answer is discarded; later pictures fail at once. */
	t_fake_delay(f, 0);
	t_fake_hang_frame(f, 1, 3200000);
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	if (s != NULL) {
		OSStatus st = 1;
		CMSampleBufferRef sb = NULL;
		for (i = 0; i < 3; i++) {
			CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, (int)i);
			CMTime pts = { (int64_t)i, 30, N2_CMTIME_VALID, 0 };
			(void)n2fake_session_encode(s, pb, pts, duration, NULL);
			CFRelease((CFTypeRef)pb);
		}
		CHECK_EQ(n2fake_session_complete(s), 0);
		CHECK(n2fake_session_emitted(s, 0, &st, NULL, &sb, NULL));
		CHECK_EQ(st, 0);
		CHECK(n2fake_session_emitted(s, 1, &st, NULL, &sb, NULL));
		CHECK_EQ(st, N2_kVTVideoEncoderMalfunctionErr);
		CHECK(n2fake_session_emitted(s, 2, &st, NULL, &sb, NULL));
		CHECK_EQ(st, N2_kVTVideoEncoderMalfunctionErr);
		{
			CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 9);
			CMTime pts = { 9, 30, N2_CMTIME_VALID, 0 };
			CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), N2_kVTVideoEncoderMalfunctionErr);
			CFRelease((CFTypeRef)pb);
		}
		CHECK(n2fake_session_emitted(s, 3, &st, NULL, &sb, NULL));
		CHECK_EQ(st, N2_kVTVideoEncoderMalfunctionErr);
		CHECK_EQ(n2fake_session_protocol_errors(s), 0);
		n2fake_session_destroy(s);       /* joins the worker after the late answer */
	}
	t_fake_hang_frame(f, -1, 0);
	CHECK_EQ(n2fake_live_objects(), live0);
	t_fake_uninstall();
	t_fake_free(f);
	n2hwenc_set_max_in_flight(1);
}

/* StartSession retries BUSY and the daemon's restart answer (reconnecting), and gives up within the bound. */
static void test_start_retries(const struct t_vector *v)
{
	struct t_fake_client *f = t_fake_new(v, NULL);
	struct n2fake_session *s;
	int c0, d0;

	t_fake_install(f);
	t_fake_busy_open(f, 2);
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	CHECK_EQ(t_fake_opens(f), 1);
	CHECK_EQ(t_fake_connects(f), 1);
	n2fake_session_destroy(s);

	c0 = t_fake_connects(f);
	d0 = t_fake_disconnects(f);
	t_fake_restart_open(f, 1);
	s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
	CHECK(s != NULL);
	CHECK_EQ(t_fake_connects(f), c0 + 2);       /* the restarting daemon's connection is dropped and reopened */
	CHECK_EQ(t_fake_disconnects(f), d0 + 1);
	n2fake_session_destroy(s);

	/* The first-picture reopen meets the daemon's restart answer (every reopen after an HEVC session, vc24 rule): the
	 * object reconnects and the first picture still encodes. */
	{
		int32_t rate = 5000000;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &rate);
		CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 3);
		CMTime pts = { 0, 30, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 30, N2_CMTIME_VALID, 0 };
		OSStatus st = -1;
		CMSampleBufferRef sb = NULL;
		int opens0;

		s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
		CHECK(s != NULL);
		opens0 = t_fake_opens(f);
		c0 = t_fake_connects(f);
		CHECK_EQ(n2fake_session_set_property(s, N2STR("AverageBitRate"), (CFTypeRef)n), 0);
		t_fake_restart_open(f, 1);
		CHECK_EQ(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
		CHECK(n2fake_session_emitted(s, 0, &st, NULL, &sb, NULL));
		CHECK_EQ(st, 0);
		CHECK(sb != NULL);
		CHECK_EQ(t_fake_opens(f), opens0 + 1);
		CHECK_EQ(t_fake_connects(f), c0 + 1);
		CHECK_EQ(t_fake_open_params(f, opens0)->bitrate, 5000000);
		CFRelease((CFTypeRef)n);
		CFRelease((CFTypeRef)pb);
		n2fake_session_destroy(s);
	}

	/* cam1-prod: the daemon refuses the first-picture reopen.  1 refusal: reopened with the client's bitrate/gop/fps and
	 * no profile/level.  2 refusals: reopened with StartSession's parameters.  3: fatal, one malfunction emit.  Without
	 * a profile there is no middle step: 1 refusal goes straight to StartSession's parameters. */
	{
		int refusals;

		for (refusals = 1; refusals <= 4; refusals++) {
			int no_profile = refusals == 4;
			int refuse = no_profile ? 1 : refusals;
			int32_t rate = 5000000, gop = 15;
			CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &rate);
			CFNumberRef g = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &gop);
			CVPixelBufferRef pb = make_picture(v->width, v->height, N2_PIXFMT_420V, 3);
			CMTime pts = { 0, 30, N2_CMTIME_VALID, 0 };
			CMTime pts1 = { 1, 30, N2_CMTIME_VALID, 0 };
			CMTime duration = { 1, 30, N2_CMTIME_VALID, 0 };
			OSStatus st = -1;
			CMSampleBufferRef sb = NULL;
			const struct n2hwenc_params *p;
			int opens0, closes0;

			s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
			CHECK(s != NULL);
			opens0 = t_fake_opens(f);
			closes0 = t_fake_closes(f);
			CHECK_EQ(n2fake_session_set_property(s, N2STR("AverageBitRate"), (CFTypeRef)n), 0);
			CHECK_EQ(n2fake_session_set_property(s, N2STR("MaxKeyFrameInterval"), (CFTypeRef)g), 0);
			if (!no_profile)
				CHECK_EQ(n2fake_session_set_property(s, N2STR("ProfileLevel"), N2STR("H264_High_4_1")), 0);
			CHECK_EQ(n2fake_session_set_property(s, N2STR("RealTime"), kCFBooleanTrue), 0);
			t_fake_refuse_open(f, refuse);
			(void)n2fake_session_encode(s, pb, pts, duration, NULL);
			CHECK(n2fake_session_emitted(s, 0, &st, NULL, &sb, NULL));
			CHECK_EQ(t_fake_refused(f), refuse);
			p = t_fake_refused_params(f, 0);
			CHECK(p != NULL);
			if (p != NULL) {
				CHECK_EQ(p->bitrate, 5000000);
				CHECK_EQ(p->gop, 15);
				CHECK_EQ(p->profile, no_profile ? 0 : 100);
				CHECK_EQ(p->level, no_profile ? 0 : 41);
				CHECK_EQ(p->flags, 0);
			}
			if (refuse >= 2) {
				p = t_fake_refused_params(f, 1);
				CHECK(p != NULL);
				if (p != NULL) {
					CHECK_EQ(p->bitrate, 5000000);
					CHECK_EQ(p->gop, 15);
					CHECK_EQ(p->profile, 0);
					CHECK_EQ(p->level, 0);
				}
			}
			CHECK_EQ(t_fake_closes(f), closes0 + 1);        /* only StartSession's session was ever open before */
			if (refuse <= 2) {
				CHECK_EQ(st, 0);
				CHECK(sb != NULL);
				CHECK_EQ(t_fake_opens(f), opens0 + 1);
				p = t_fake_open_params(f, opens0);
				CHECK(p != NULL);
				if (p != NULL && refuse == 1 && !no_profile) {
					CHECK_EQ(p->bitrate, 5000000);
					CHECK_EQ(p->gop, 15);
					CHECK_EQ(p->profile, 0);
					CHECK_EQ(p->level, 0);
					CHECK_EQ(p->flags, 0);
				} else if (p != NULL) {
					CHECK(memcmp(p, t_fake_open_params(f, opens0 - 1), sizeof(*p)) == 0);   /* StartSession's OPEN */
				}
				/* the stream goes on */
				st = -1;
				CHECK_EQ(n2fake_session_encode(s, pb, pts1, duration, NULL), 0);
				CHECK_EQ(n2fake_session_complete(s), 0);
				CHECK(n2fake_session_emitted(s, 1, &st, NULL, &sb, NULL));
				CHECK_EQ(st, 0);
				CHECK_EQ(t_fake_opens(f), opens0 + 1);
			} else {
				CHECK_EQ(st, N2_kVTVideoEncoderMalfunctionErr);
				CHECK_EQ(t_fake_opens(f), opens0);
			}
			t_fake_refuse_open(f, 0);
			CFRelease((CFTypeRef)n);
			CFRelease((CFTypeRef)g);
			CFRelease((CFTypeRef)pb);
			n2fake_session_destroy(s);
		}
	}

	/* BUSY for longer than N2HWENC_START_RETRY_MS: fallback status, well inside 2 s. */
	t_fake_busy_open(f, 1000);
	{
		VTVideoEncoderRef enc = NULL;
		CMVideoDimensions dims = { (int32_t)v->width, (int32_t)v->height };
		struct n2fake_session *probe = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width,
		                                                     (int)v->height);
		CHECK(probe == NULL);
		CHECK_EQ(n2hwavc_vt_create(T_CODEC_AVC1, kCFAllocatorDefault, &enc), 0);
		if (enc != NULL) {
			const n2_VTVideoEncoderVTable *vt = NULL;
			struct timespec t0, t1;
			long ms;
			t_fake_busy_open(f, 0);
			s = n2fake_session_create(n2hwavc_vt_create, T_CODEC_AVC1, (int)v->width, (int)v->height);
			vt = s != NULL ? n2fake_session_vtable(s) : NULL;
			n2fake_session_destroy(s);
			t_fake_busy_open(f, 1000);
			clock_gettime(CLOCK_MONOTONIC, &t0);
			if (vt != NULL)
				CHECK_EQ(vt->encoderClass->startSession(enc, (VTVideoEncoderSession)(uintptr_t)0x1000, dims),
				         N2_kVTVideoEncoderNotAvailableNowErr);
			clock_gettime(CLOCK_MONOTONIC, &t1);
			ms = (long)(t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
			CHECK(ms >= 500 && ms < 2000);
			CFRelease((CFTypeRef)enc);
		}
	}
	t_fake_busy_open(f, 0);
	t_fake_uninstall();
	t_fake_free(f);
}

static _Atomic unsigned s_trace_counts[16];

static void trace_event(void *ctx, int event, uint32_t a, uint32_t b)
{
	(void)ctx;
	(void)a;
	(void)b;
	if (event > 0 && event < (int)(sizeof(s_trace_counts) / sizeof(s_trace_counts[0])))
		atomic_fetch_add_explicit(&s_trace_counts[event], 1, memory_order_relaxed);
}

int main(int argc, char **argv)
{
	struct t_vector avc, hevc;
	const char *avc_path = argc > 1 ? argv[1] : NULL;
	const char *hevc_path = argc > 2 ? argv[2] : NULL;
	struct stream_result r;

	if (avc_path == NULL || hevc_path == NULL) {
		fprintf(stderr, "usage: t_vt <avc vector> <hevc vector>\n");
		return 2;
	}
	n2hwenc_set_trace(trace_event, NULL);
	n2hwenc_set_max_in_flight(1);               /* the original synchronous checks first */
	if (t_vector_load(avc_path, &avc) != 0 || t_vector_load(hevc_path, &hevc) != 0) {
		fprintf(stderr, "t_vt: cannot load the vectors\n");
		return 2;
	}
	CHECK_EQ(avc.codec, N2HWENC_AVC);
	CHECK_EQ(hevc.codec, N2HWENC_HEVC);

	test_no_client(&avc);
	test_factories();
	test_registration();
	test_available(&avc);

	memset(&r, 0, sizeof(r));
	run_stream(&avc, T_CODEC_AVC1, n2hwavc_vt_create, &r);
	CHECK_EQ(r.emits, avc.frame_count);
	CHECK_EQ(r.checked_samples, avc.frame_count);

	memset(&r, 0, sizeof(r));
	run_stream(&hevc, T_CODEC_HVC1, n2hwhevc_vt_create, &r);
	CHECK_EQ(r.emits, hevc.frame_count);
	CHECK_EQ(r.checked_samples, hevc.frame_count);

	test_properties(&avc);
	test_failures(&avc);
	test_invalidate(&avc);
	test_threads(&avc, &hevc);

	/* opus-cam: repack, pipeline (2 and 3 in flight, both codecs), pipelined failures, StartSession retries. */
	test_strides(&avc);
	test_async_stream(&avc, T_CODEC_AVC1, n2hwavc_vt_create, 2);
	test_async_stream(&avc, T_CODEC_AVC1, n2hwavc_vt_create, 3);
	test_async_stream(&hevc, T_CODEC_HVC1, n2hwhevc_vt_create, 2);
	test_async_failures(&avc);
	test_start_retries(&avc);

	/* The trace hook saw every important transition (it is the library's only diagnostic channel). */
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_CONNECTED], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_OPENED], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_REOPENED], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_FRAME], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_FORMAT], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_ENC_SET], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_INVALIDATE], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_FRAME_FAILED], memory_order_relaxed) > 0);
	CHECK(atomic_load_explicit(&s_trace_counts[N2HWENC_TRACE_CONNECT_FAILED], memory_order_relaxed) > 0);

	t_vector_free(&avc);
	t_vector_free(&hevc);
	return t_report("VT");
}
