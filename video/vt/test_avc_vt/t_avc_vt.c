/* SPDX-License-Identifier: MIT */
/*
 * t_avc_vt.c -- host test oracle for the WP05 VideoToolbox glue.
 *
 * Two builds use this file:
 *   (default)      against vt/test_avc_vt/stub_n2avc.c, which records what the glue passes to include/n2avc.h and emits
 *                  one known 20-byte AVCC sample per picture;
 *   -DN2AVC_REAL   against the avc directory's encoder, when WP03's encoder announces itself: then the concatenated
 *                  samples are converted from AVCC to Annex B and decoded by ffmpeg, which must give exactly 10 pictures.
 *
 * Expected values are literals from WP05's TASK.md and from ITU-T H.264's NAL/AVCC layout, not copies of the glue.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_apple.h"
#include "n2_apple.h"
#include "n2avc.h"
#include "n2avc_vt.h"
#include "n2vt_abi.h"

#if !defined(N2AVC_REAL)
struct wp05_stub_stats {
	int      opens;
	int      closes;
	int      reconfigures;
	int      fail_next;
	int      fail_open;
	n2avc_config last_config;
	uint32_t reconfigure_bitrate;
	uint32_t reconfigure_fps_num;
	uint32_t reconfigure_fps_den;
};
extern struct wp05_stub_stats wp05_stub;
#endif

/* The glue logs through WP01's vt/n2vt_log.h.  On the host that implementation opens /dev/console and uses Darwin's
 * getprogname(), so the test provides the sink instead of linking it.  g_last_say lets a case check the one required line. */
static char g_last_say[512];

#ifdef N2_HOST_FAKE
void n2vt_say(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(g_last_say, sizeof(g_last_say), format, args);
	va_end(args);
}
#endif

#define AVC1 0x61766331u
#define HVC1 0x68766331u

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                                       \
	do {                                                                              \
		g_checks++;                                                               \
		if (!(cond)) {                                                            \
			g_failures++;                                                     \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
		}                                                                         \
	} while (0)

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int cf_is_array(CFTypeRef value)
{
	CFMutableArrayRef probe = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
	CFTypeID probe_type;
	int same;

	if (probe == NULL)
		return 0;
	probe_type = CFGetTypeID(probe);
	same = CFGetTypeID(value) == probe_type;
	CFRelease(probe);
	return same;
}

static int64_t cf_i64(CFTypeRef value)
{
	int64_t v = 0;
	double d = 0.0;
	int32_t v32 = 0;

	if (value == NULL || CFGetTypeID(value) != CFNumberGetTypeID())
		return INT64_MIN;
	if (CFNumberGetValue((CFNumberRef)value, kCFNumberSInt64Type, &v))
		return v;
	if (CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &v32))
		return v32;
	if (CFNumberGetValue((CFNumberRef)value, kCFNumberFloat64Type, &d))
		return (int64_t)d;
	return INT64_MIN;
}

static CVPixelBufferRef make_picture(size_t width, size_t height, uint32_t format, int index)
{
	CVPixelBufferRef pb = NULL;
	size_t plane, planes;

	if (CVPixelBufferCreate(kCFAllocatorDefault, width, height, format, NULL, &pb) != 0 || pb == NULL)
		return NULL;
	if (CVPixelBufferLockBaseAddress(pb, 0) != 0) {
		CFRelease(pb);
		return NULL;
	}
	planes = CVPixelBufferGetPlaneCount(pb);
	for (plane = 0; plane < planes; plane++) {
		uint8_t *base = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, plane);
		size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pb, plane);
		size_t rows = (plane == 0) ? height : (height + 1) / 2;
		size_t y;
		unsigned value = (plane == 0) ? (unsigned)(16 + ((index * 13) & 0x7f)) : 128u;
		if (base == NULL)
			continue;
		for (y = 0; y < rows; y++)
			memset(base + y * stride, (int)((value + y * 3) & 0xff), stride);
	}
	CVPixelBufferUnlockBaseAddress(pb, 0);
	return pb;
}

/* *present = attachment exists; returns its boolean value, or -1 when it is not a CFBoolean. */
static int attachment_value(CMSampleBufferRef sample, CFStringRef key, int *present)
{
	CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, 0);
	CFDictionaryRef dict;
	CFTypeRef value;

	*present = 0;
	if (attachments == NULL || CFArrayGetCount(attachments) < 1)
		return 0;
	dict = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
	if (dict == NULL)
		return 0;
	value = CFDictionaryGetValue(dict, key);
	if (value == NULL)
		return 0;
	*present = 1;
	if (CFGetTypeID(value) != CFBooleanGetTypeID())
		return -1;
	return CFBooleanGetValue((CFBooleanRef)value) ? 1 : 0;
}

static CFDictionaryRef make_frame_flags(void)
{
	CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
	                                                        &kCFTypeDictionaryKeyCallBacks,
	                                                        &kCFTypeDictionaryValueCallBacks);
	if (dict != NULL)
		CFDictionarySetValue(dict, N2STR("ForceKeyFrame"), kCFBooleanTrue);
	return dict;
}

/* ------------------------------------------------------------------------------------------------------------------ */

static void test_create_and_dimensions(void)
{
	VTVideoEncoderRef object = (VTVideoEncoderRef)(uintptr_t)0x1234;
	long live_before = n2fake_live_objects();
	size_t i;
	static const CMVideoDimensions bad[] = {
		{ 15, 480 }, { 640, 15 }, { 641, 480 }, { 640, 481 }, { 4098, 480 }, { 640, 2306 }, { 0, 0 },
	};

	CHECK(n2avc_vt_create(HVC1, kCFAllocatorDefault, &object) == N2_kVTParameterErr);
	CHECK(object == (VTVideoEncoderRef)(uintptr_t)0x1234);
	CHECK(n2avc_vt_create(AVC1, kCFAllocatorDefault, NULL) == N2_kVTParameterErr);

	CHECK(n2avc_vt_create(AVC1, kCFAllocatorDefault, &object) == 0);
	CHECK(object != NULL);
	if (object != NULL)
		CFRelease((CMBaseObjectRef)object);

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(n2fake_session_create(n2avc_vt_create, AVC1, bad[i].width, bad[i].height) == NULL);

	CHECK(n2fake_live_objects() == live_before);
}

static void test_five_frames(void)
{
	long live_before = n2fake_live_objects();
	struct n2fake_session *session;
	CFDictionaryRef force;
	CVPixelBufferRef pictures[5];
	CMSampleBufferRef first = NULL;
	CMFormatDescriptionRef format;
	size_t i;

	memset(pictures, 0, sizeof(pictures));
	session = n2fake_session_create(n2avc_vt_create, AVC1, 640, 480);
	CHECK(session != NULL);
	if (session == NULL)
		return;

	/* What the encoder told VideoToolbox it wants. */
	{
		CFDictionaryRef attributes = n2fake_session_pixel_attributes(session);
		CFTypeRef formats, width, height, surface;
		CHECK(attributes != NULL);
		formats = CFDictionaryGetValue(attributes, kCVPixelBufferPixelFormatTypeKey);
		width = CFDictionaryGetValue(attributes, kCVPixelBufferWidthKey);
		height = CFDictionaryGetValue(attributes, kCVPixelBufferHeightKey);
		surface = CFDictionaryGetValue(attributes, kCVPixelBufferIOSurfacePropertiesKey);
		CHECK(formats != NULL && cf_is_array(formats));
		CHECK(CFArrayGetCount((CFArrayRef)formats) == 2);
		if (formats != NULL && CFArrayGetCount((CFArrayRef)formats) == 2) {
			CHECK(cf_i64(CFArrayGetValueAtIndex((CFArrayRef)formats, 0)) == (int64_t)N2_PIXFMT_420V);
			CHECK(cf_i64(CFArrayGetValueAtIndex((CFArrayRef)formats, 1)) == (int64_t)N2_PIXFMT_420F);
		}
		CHECK(cf_i64(width) == 640);
		CHECK(cf_i64(height) == 480);
		CHECK(surface != NULL && CFGetTypeID(surface) == CFDictionaryGetTypeID());
		if (surface != NULL && CFGetTypeID(surface) == CFDictionaryGetTypeID())
			CHECK(n2fake_dictionary_count((CFDictionaryRef)surface) == 0);
	}

	force = make_frame_flags();
	CHECK(force != NULL);

	for (i = 0; i < 5; i++) {
		CMTime pts = { 1000 + (int64_t)i * 3000, 30000, N2_CMTIME_VALID, 0 };
		CMTime duration = { 3000, 30000, N2_CMTIME_VALID, 0 };
		pictures[i] = make_picture(640, 480, N2_PIXFMT_420V, (int)i);
		CHECK(pictures[i] != NULL);
		CHECK(n2fake_session_encode(session, pictures[i], pts, duration, i == 3 ? force : NULL) == 0);
	}
	if (force != NULL)
		CFRelease(force);

	CHECK(n2fake_session_protocol_errors(session) == 0);
	CHECK(n2fake_session_emitted(session, 5, NULL, NULL, NULL, NULL) == 0);

	for (i = 0; i < 5; i++) {
		OSStatus status = -1;
		uint32_t flags = 1;
		CMSampleBufferRef sample = NULL;
		void *token = NULL;
		CMSampleTimingInfo timing;
		size_t sample_size = 0;
		CMBlockBufferRef block;
		uint8_t *data;
		size_t length;
		int64_t expected_pts = 1000 + (int64_t)i * 3000;
		int expected_sync = (i == 0 || i == 3);
		int present = 0;
		int value;

		memset(&timing, 0, sizeof(timing));
		CHECK(n2fake_session_emitted(session, (int)i, &status, &flags, &sample, &token) == 1);
		CHECK(status == 0);
		CHECK(flags == 0);
		CHECK(sample != NULL);
		CHECK(token != NULL);
		if (sample == NULL)
			continue;
		if (i == 0)
			first = sample;
		CHECK(n2fake_sample_timing(sample, &timing, &sample_size) == 1);
		CHECK(timing.presentationTimeStamp.value == expected_pts);
		CHECK(timing.presentationTimeStamp.timescale == 30000);
		CHECK(timing.decodeTimeStamp.value == expected_pts);
		CHECK(timing.duration.value == 3000);
		CHECK(timing.duration.timescale == 30000);
		CHECK((timing.presentationTimeStamp.flags & N2_CMTIME_VALID) != 0);

		block = CMSampleBufferGetDataBuffer(sample);
		CHECK(block != NULL);
		if (block == NULL)
			continue;
		length = CMBlockBufferGetDataLength(block);
		CHECK(length > 4);
		CHECK(sample_size == length);
		data = (uint8_t *)malloc(length);
		CHECK(data != NULL);
		if (data == NULL)
			continue;
		CHECK(CMBlockBufferCopyDataBytes(block, 0, length, data) == 0);
		{       /* (lead) AVCC, not Annex B: a chain of 4-byte lengths that ends exactly at the sample's end (one NAL per slice) */
			size_t off = 0, nals = 0;
			while (off + 4 <= length && be32(data + off) > 0 && off + 4 + be32(data + off) <= length) {
				off += 4 + be32(data + off);
				nals++;
			}
			CHECK(off == length);
			CHECK(nals >= 1 && nals <= 16);
		}
		CHECK(data[4] != 0);                      /* NAL header byte */
#if !defined(N2AVC_REAL)
		CHECK(length == 20);
		CHECK(be32(data) == 16);
		CHECK(data[4] == (expected_sync ? 0x65 : 0x41));
		CHECK(be32(data + 8) == (uint32_t)i);
#else
		CHECK((data[0] | data[1] | data[2]) != 0 || data[3] != 1);   /* not a start code */
#endif
		free(data);

		value = attachment_value(sample, kCMSampleAttachmentKey_NotSync, &present);
		if (expected_sync)
			CHECK(present == 0 || value == 0);
		else
			CHECK(present == 1 && value == 1);
		value = attachment_value(sample, kCMSampleAttachmentKey_DependsOnOthers, &present);
		CHECK(present == 1 && value == (expected_sync ? 0 : 1));
	}

	CHECK(n2fake_session_complete(session) == 0);

#if !defined(N2AVC_REAL)
	/* The defaults of the first open, when no property was set: CQP 26, 30/1, four slices, keyint 60, deblocking on
	 * (the lead's c8 rule: at 640x480 the switch enables N2AVC_F_DEBLOCK), full range off. */
	CHECK(wp05_stub.opens == 1);
	CHECK(wp05_stub.last_config.bitrate == 0);
	CHECK(wp05_stub.last_config.qp == 26);
	CHECK(wp05_stub.last_config.fps_num == 30);
	CHECK(wp05_stub.last_config.fps_den == 1);
	CHECK(wp05_stub.last_config.threads == 4);
	CHECK(wp05_stub.last_config.keyint == 60);
	CHECK(wp05_stub.last_config.flags == (uint32_t)N2AVC_F_DEBLOCK);
	CHECK(wp05_stub.last_config.search_range == 0);
#endif

	format = (first != NULL) ? CMSampleBufferGetFormatDescription(first) : NULL;
	CHECK(format != NULL);
	if (format != NULL) {
		const uint8_t *ps = NULL;
		size_t ps_size = 0;
		size_t count = 0;
		int nal_length = 0;
		CHECK(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, &ps, &ps_size, &count,
		                                                         &nal_length) == 0);
		CHECK(nal_length == 4);
		CHECK(count == 2);
#if !defined(N2AVC_REAL)
		CHECK(ps_size == 6 && ps != NULL);
		if (ps_size == 6 && ps != NULL)
			CHECK(memcmp(ps, (const uint8_t[]){ 0x67, 0x42, 0xC0, 0x1E, 0xD9, 0x00 }, 6) == 0);
		CHECK(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 1, &ps, &ps_size, &count,
		                                                         &nal_length) == 0);
		CHECK(ps_size == 4 && ps != NULL);
		if (ps_size == 4 && ps != NULL)
			CHECK(memcmp(ps, (const uint8_t[]){ 0x68, 0xCE, 0x3C, 0x80 }, 4) == 0);
#else
		(void)ps;
		(void)ps_size;
#endif
	}

	for (i = 0; i < 5; i++)
		if (pictures[i] != NULL)
			CFRelease(pictures[i]);
	n2fake_session_destroy(session);
#if !defined(N2AVC_REAL)
	CHECK(wp05_stub.closes == 1);
#endif
	CHECK(n2fake_live_objects() == live_before);
}

static void test_properties(void)
{
	long live_before = n2fake_live_objects();
	struct n2fake_session *session;
	CVPixelBufferRef picture;
	CFTypeRef value = NULL;
	CFNumberRef number;
	CFMutableArrayRef limits;
	CMTime pts = { 0, 30, N2_CMTIME_VALID, 0 };
	CMTime duration = { 1, 30, N2_CMTIME_VALID, 0 };
	int32_t one = 1, four = 4;

	session = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
	CHECK(session != NULL);
	if (session == NULL)
		return;

#if !defined(N2AVC_REAL)
	memset(&wp05_stub, 0, sizeof(wp05_stub));
#endif
	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ 500000 });
	CHECK(number != NULL);
	CHECK(n2fake_session_set_property(session, N2STR("AverageBitRate"), number) == 0);
	CFRelease(number);

	picture = make_picture(64, 64, N2_PIXFMT_420V, 0);
	CHECK(picture != NULL);
	CHECK(n2fake_session_encode(session, picture, pts, duration, NULL) == 0);

#if !defined(N2AVC_REAL)
	CHECK(wp05_stub.opens == 1);
	CHECK(wp05_stub.last_config.bitrate == 500000);
	CHECK(wp05_stub.reconfigures == 0);
	CHECK(wp05_stub.last_config.width == 64);
	CHECK(wp05_stub.last_config.height == 64);
	CHECK(wp05_stub.last_config.keyint == 60);
#endif

	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ 250000 });
	CHECK(number != NULL);
	CHECK(n2fake_session_set_property(session, N2STR("AverageBitRate"), number) == 0);
	CFRelease(number);
#if !defined(N2AVC_REAL)
	CHECK(wp05_stub.reconfigures == 1);
	CHECK(wp05_stub.reconfigure_bitrate == 250000);
	CHECK(wp05_stub.reconfigure_fps_num == 30);
	CHECK(wp05_stub.reconfigure_fps_den == 1);
#endif
	CHECK(n2fake_session_copy_property(session, N2STR("AverageBitRate"), &value) == 0);
	CHECK(cf_i64(value) == 250000);
	if (value != NULL)
		CFRelease(value);

	/* Frame rate: 29.97 becomes 2997/100 in the n2avc config. */
	{
		double fps = 29.97;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &fps);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("ExpectedFrameRate"), number) == 0);
		CFRelease(number);
#if !defined(N2AVC_REAL)
		CHECK(wp05_stub.reconfigures == 2);
		CHECK(wp05_stub.reconfigure_fps_num == 2997);
		CHECK(wp05_stub.reconfigure_fps_den == 100);
#endif
	}

	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &four);
	CHECK(number != NULL);
	CHECK(n2fake_session_set_property(session, N2STR("NumberOfSlices"), number) == 0);
	CFRelease(number);
	CHECK(n2fake_session_copy_property(session, N2STR("NumberOfSlices"), &value) == 0);
	CHECK(cf_i64(value) == 4);
	if (value != NULL)
		CFRelease(value);

	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &four);
	CHECK(number != NULL);
	CHECK(n2fake_session_set_property(session, N2STR("N2Threads"), number) == 0);
	CFRelease(number);

	/* DataRateLimits {10000 bytes, 1 second} caps the 250000 bit/s average at 80000 bit/s. */
	limits = CFArrayCreateMutable(kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
	CHECK(limits != NULL);
	if (limits != NULL) {
		CFNumberRef bytes = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ 10000 });
		CFNumberRef seconds = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &one);
		CHECK(bytes != NULL && seconds != NULL);
		if (bytes != NULL && seconds != NULL) {
			CFArrayAppendValue(limits, bytes);
			CFArrayAppendValue(limits, seconds);
			CHECK(n2fake_session_set_property(session, N2STR("DataRateLimits"), limits) == 0);
#if !defined(N2AVC_REAL)
			CHECK(wp05_stub.reconfigures == 3);
			CHECK(wp05_stub.reconfigure_bitrate == 80000);
#endif
		}
		if (bytes != NULL)
			CFRelease(bytes);
		if (seconds != NULL)
			CFRelease(seconds);
		CFRelease(limits);
	}

	{
		double quality = 0.5;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &quality);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("Quality"), number) == 0);
		CFRelease(number);
	}
	CHECK(n2fake_session_set_property(session, N2STR("RealTime"), kCFBooleanTrue) == 0);
	CHECK(n2fake_session_set_property(session, N2STR("AllowFrameReordering"), kCFBooleanFalse) == 0);
	CHECK(n2fake_session_set_property(session, N2STR("ProfileLevel"),
	                                  N2STR("H264_Baseline_AutoLevel")) == 0);
	CHECK(n2fake_session_set_property(session, N2STR("ColorPrimaries"), N2STR("ITU_R_709_2")) == 0);
	CHECK(n2fake_session_set_property(session, N2STR("TransferFunction"), N2STR("SMPTE_C")) == 0);
	CHECK(n2fake_session_set_property(session, N2STR("YCbCrMatrix"), N2STR("ITU_R_601_4")) == 0);

	/* Unknown key, wrong type, read-only. */
	g_last_say[0] = '\0';
	CHECK(n2fake_session_set_property(session, N2STR("NoSuchVideoProperty"), kCFBooleanTrue) == 0);     /* (lead) kept, said */
	CHECK(strstr(g_last_say, "NoSuchVideoProperty") != NULL);
	CHECK(n2fake_session_set_property(session, N2STR("AverageBitRate"), kCFBooleanTrue) == N2_kVTParameterErr);
	CHECK(n2fake_session_set_property(session, N2STR("RealTime"), N2STR("yes")) == N2_kVTParameterErr);
	CHECK(n2fake_session_set_property(session, N2STR("UsingHardwareAcceleratedVideoEncoder"), kCFBooleanTrue) ==
	      N2_kVTPropertyNotSupportedErr);
	CHECK(n2fake_session_set_property(session, N2STR("NumberOfPendingFrames"), kCFBooleanTrue) ==
	      N2_kVTPropertyNotSupportedErr);
	CHECK(n2fake_session_set_property(session, N2STR("Quality"), kCFBooleanTrue) == N2_kVTParameterErr);

	/* NaN and out-of-range numbers are rejected, not cast. */
	{
		double nan_value = __builtin_nan("");
		double huge_value = 1e30;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &nan_value);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("Quality"), number) == N2_kVTParameterErr);
		CHECK(n2fake_session_set_property(session, N2STR("ExpectedFrameRate"), number) == N2_kVTParameterErr);
		CHECK(n2fake_session_set_property(session, N2STR("MaxKeyFrameIntervalDuration"), number) ==
		      N2_kVTParameterErr);
		CFRelease(number);
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &huge_value);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("AverageBitRate"), number) == N2_kVTParameterErr);
		CFRelease(number);
	}

	/* Read-only values and a value that was never set. */
	CHECK(n2fake_session_copy_property(session, N2STR("UsingHardwareAcceleratedVideoEncoder"), &value) == 0);
	CHECK(value == kCFBooleanFalse || (value != NULL && CFEqual(value, kCFBooleanFalse)));
	if (value != NULL)
		CFRelease(value);
	value = NULL;
	CHECK(n2fake_session_copy_property(session, N2STR("NumberOfPendingFrames"), &value) == 0);
	CHECK(cf_i64(value) == 0);
	if (value != NULL)
		CFRelease(value);
	value = NULL;
	CHECK(n2fake_session_copy_property(session, N2STR("ExpectedDuration"), &value) ==
	      N2_kCMBaseObjectError_ValueNotAvailable);
	CHECK(value == NULL);
	value = NULL;
	CHECK(n2fake_session_copy_property(session, N2STR("NeverSetVideoProperty"), &value) ==
	      N2_kCMBaseObjectError_ValueNotAvailable);
	CHECK(value == NULL);
	value = NULL;
	CHECK(n2fake_session_copy_property(session, N2STR("ProfileLevel"), &value) == 0);
	CHECK(value != NULL && CFEqual(value, N2STR("H264_Baseline_AutoLevel")));
	if (value != NULL)
		CFRelease(value);

	CFRelease(picture);
	n2fake_session_destroy(session);

#if !defined(N2AVC_REAL)
	/* A 420f ('full range') first picture must reach n2avc_config.flags as N2AVC_F_FULLRANGE. */
	{
		struct n2fake_session *full_range;
		CVPixelBufferRef full_picture;
		CMTime fps_pts = { 0, 30, N2_CMTIME_VALID, 0 };
		CMTime one_frame = { 1, 30, N2_CMTIME_VALID, 0 };
		memset(&wp05_stub, 0, sizeof(wp05_stub));
		full_range = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
		CHECK(full_range != NULL);
		if (full_range != NULL) {
			full_picture = make_picture(64, 64, N2_PIXFMT_420F, 6);
			CHECK(full_picture != NULL);
			CHECK(n2fake_session_encode(full_range, full_picture, fps_pts, one_frame, NULL) == 0);
			CHECK(wp05_stub.last_config.flags == (uint32_t)(N2AVC_F_FULLRANGE | N2AVC_F_DEBLOCK));
			if (full_picture != NULL)
				CFRelease(full_picture);
			n2fake_session_destroy(full_range);
		}
	}
#endif

	CHECK(n2fake_live_objects() == live_before);
}

static void test_supported_dictionary(void)
{
	long live_before = n2fake_live_objects();
	struct n2fake_session *session = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
	const n2_VTVideoEncoderVTable *vtable;
	VTVideoEncoderRef encoder;
	CFDictionaryRef dict = NULL;
	CFStringRef key;
	CFDictionaryRef entry;
	CFTypeRef type, status;

	CHECK(session != NULL);
	if (session == NULL)
		return;
	vtable = n2fake_session_vtable(session);
	encoder = n2fake_session_encoder(session);
	CHECK(vtable != NULL);
	CHECK(encoder != NULL);
	CHECK(vtable != NULL && vtable->encoderClass != NULL);
	CHECK(vtable != NULL && vtable->encoderClass != NULL &&
	      vtable->encoderClass->copySupportedPropertyDictionary != NULL);
	if (vtable == NULL || vtable->encoderClass == NULL ||
	    vtable->encoderClass->copySupportedPropertyDictionary == NULL) {
		n2fake_session_destroy(session);
		return;
	}

	CHECK(vtable->encoderClass->copySupportedPropertyDictionary(encoder, &dict) == 0);
	CHECK(dict != NULL);
	if (dict == NULL) {
		n2fake_session_destroy(session);
		return;
	}
	/* AverageBitRate ExpectedFrameRate MaxKeyFrameInterval MaxKeyFrameIntervalDuration RealTime Quality
	 * NumberOfSlices N2Threads DataRateLimits ProfileLevel AllowFrameReordering AllowTemporalCompression
	 * H264EntropyMode ColorPrimaries TransferFunction YCbCrMatrix PixelAspectRatio CleanAperture FieldCount
	 * MaxFrameDelayCount Priority SourceFrameCount ExpectedDuration BaseLayerFrameRate EncoderID UsingHardware...
	 * NumberOfPendingFrames AllowOpenGOP MaximizePowerEfficiency = 29. */
	CHECK(n2fake_dictionary_count(dict) == 29);

	key = N2STR("AverageBitRate");
	entry = (CFDictionaryRef)CFDictionaryGetValue(dict, key);
	CHECK(entry != NULL && CFGetTypeID(entry) == CFDictionaryGetTypeID());
	if (entry != NULL) {
		type = CFDictionaryGetValue(entry, N2STR("PropertyType"));
		status = CFDictionaryGetValue(entry, N2STR("ReadWriteStatus"));
		CHECK(type != NULL && CFEqual(type, N2STR("Number")));
		CHECK(status != NULL && CFEqual(status, N2STR("ReadWrite")));
	}

	entry = (CFDictionaryRef)CFDictionaryGetValue(dict, N2STR("RealTime"));
	CHECK(entry != NULL);
	if (entry != NULL) {
		type = CFDictionaryGetValue(entry, N2STR("PropertyType"));
		CHECK(type != NULL && CFEqual(type, N2STR("Boolean")));
	}

	entry = (CFDictionaryRef)CFDictionaryGetValue(dict, N2STR("ProfileLevel"));
	CHECK(entry != NULL);
	if (entry != NULL) {
		type = CFDictionaryGetValue(entry, N2STR("PropertyType"));
		CHECK(type != NULL && CFEqual(type, N2STR("Enumeration")));
	}

	entry = (CFDictionaryRef)CFDictionaryGetValue(dict, N2STR("UsingHardwareAcceleratedVideoEncoder"));
	CHECK(entry != NULL);
	if (entry != NULL) {
		type = CFDictionaryGetValue(entry, N2STR("PropertyType"));
		status = CFDictionaryGetValue(entry, N2STR("ReadWriteStatus"));
		CHECK(type != NULL && CFEqual(type, N2STR("Boolean")));
		CHECK(status != NULL && CFEqual(status, N2STR("ReadOnly")));
	}

	CFRelease(dict);
	n2fake_session_destroy(session);
	CHECK(n2fake_live_objects() == live_before);
}

static void test_error_paths(void)
{
	long live_before = n2fake_live_objects();
	struct n2fake_session *session;
	CVPixelBufferRef good, wrong;
	OSStatus status = 0;
	uint32_t flags = 99;
	CMSampleBufferRef sample = NULL;
	void *token = NULL;
	CMTime pts = { 7, 30, N2_CMTIME_VALID, 0 };
	CMTime duration = { 1, 30, N2_CMTIME_VALID, 0 };

	session = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
	CHECK(session != NULL);
	if (session == NULL)
		return;
	good = make_picture(64, 64, N2_PIXFMT_420V, 1);
	wrong = make_picture(96, 64, N2_PIXFMT_420V, 2);
	CHECK(good != NULL && wrong != NULL);

	/* NULL image: one emit with the parameter error and no sample buffer. */
	CHECK(n2fake_session_encode(session, NULL, pts, duration, NULL) == N2_kVTParameterErr);
	CHECK(n2fake_session_emitted(session, 0, &status, &flags, &sample, &token) == 1);
	CHECK(status == N2_kVTParameterErr);
	CHECK(flags == 0);
	CHECK(sample == NULL);
	CHECK(token != NULL);

	/* Wrong size: same, and the session stays usable. */
	CHECK(n2fake_session_encode(session, wrong, pts, duration, NULL) == N2_kVTParameterErr);
	CHECK(n2fake_session_emitted(session, 1, &status, &flags, &sample, &token) == 1);
	CHECK(status == N2_kVTParameterErr);
	CHECK(sample == NULL);

	CHECK(n2fake_session_encode(session, good, pts, duration, NULL) == 0);
	CHECK(n2fake_session_emitted(session, 2, &status, &flags, &sample, &token) == 1);
	CHECK(status == 0);
	CHECK(sample != NULL);
	CHECK(n2fake_session_emitted(session, 3, NULL, NULL, NULL, NULL) == 0);
	CHECK(n2fake_session_protocol_errors(session) == 0);

	CFRelease(good);
	CFRelease(wrong);
	n2fake_session_destroy(session);

#if !defined(N2AVC_REAL)
	/* An n2avc failure at open and at encode: one malfunction emit with a NULL sample, then usable again. */
	memset(&wp05_stub, 0, sizeof(wp05_stub));
	session = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
	CHECK(session != NULL);
	if (session != NULL) {
		good = make_picture(64, 64, N2_PIXFMT_420V, 3);
		CHECK(good != NULL);
		wp05_stub.fail_open = 1;
		g_last_say[0] = '\0';
		CHECK(n2fake_session_encode(session, good, pts, duration, NULL) == N2_kVTVideoEncoderMalfunctionErr);
		CHECK(strstr(g_last_say, "n2avc_open") != NULL);
		CHECK(n2fake_session_emitted(session, 0, &status, &flags, &sample, &token) == 1);
		CHECK(status == N2_kVTVideoEncoderMalfunctionErr);
		CHECK(sample == NULL);

		wp05_stub.fail_next = 1;
		g_last_say[0] = '\0';
		CHECK(n2fake_session_encode(session, good, pts, duration, NULL) == N2_kVTVideoEncoderMalfunctionErr);
		CHECK(strstr(g_last_say, "n2avc_encode") != NULL);
		CHECK(n2fake_session_emitted(session, 1, &status, &flags, &sample, &token) == 1);
		CHECK(status == N2_kVTVideoEncoderMalfunctionErr);
		CHECK(sample == NULL);

		CHECK(n2fake_session_encode(session, good, pts, duration, NULL) == 0);
		CHECK(n2fake_session_emitted(session, 2, &status, &flags, &sample, &token) == 1);
		CHECK(status == 0);
		CHECK(sample != NULL);
		CHECK(n2fake_session_emitted(session, 3, NULL, NULL, NULL, NULL) == 0);
		CHECK(n2fake_session_protocol_errors(session) == 0);
		CFRelease(good);
		n2fake_session_destroy(session);
	}
#endif

	CHECK(n2fake_live_objects() == live_before);
}

#if !defined(N2AVC_REAL)
static void test_config_math(void)
{
	long live_before = n2fake_live_objects();
	struct n2fake_session *session;
	CVPixelBufferRef picture;
	CFNumberRef number;
	CMTime pts = { 0, 30, N2_CMTIME_VALID, 0 };
	CMTime duration = { 1, 30, N2_CMTIME_VALID, 0 };
	int32_t value;

	picture = make_picture(64, 64, N2_PIXFMT_420V, 7);
	CHECK(picture != NULL);

	/* 10 fps, 2 s keyframe duration = 20 pictures; MaxKeyFrameInterval 50 loses to it; four slices. */
	memset(&wp05_stub, 0, sizeof(wp05_stub));
	session = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
	CHECK(session != NULL);
	if (session != NULL) {
		value = 10;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("ExpectedFrameRate"), number) == 0);
		CFRelease(number);
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &(double){ 2.0 });
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("MaxKeyFrameIntervalDuration"), number) == 0);
		CFRelease(number);
		value = 50;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("MaxKeyFrameInterval"), number) == 0);
		CFRelease(number);
		value = 4;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("NumberOfSlices"), number) == 0);
		CFRelease(number);

		CHECK(n2fake_session_encode(session, picture, pts, duration, NULL) == 0);
		CHECK(wp05_stub.last_config.fps_num == 10);
		CHECK(wp05_stub.last_config.fps_den == 1);
		CHECK(wp05_stub.last_config.keyint == 20);
		CHECK(wp05_stub.last_config.threads == 4);
		CHECK(wp05_stub.last_config.bitrate == 0);
		CHECK(wp05_stub.last_config.qp == 26);
		n2fake_session_destroy(session);
	}

	/* Quality 0.5 with no bitrate: QP = round(51 - 41*0.5) = 31. */
	memset(&wp05_stub, 0, sizeof(wp05_stub));
	session = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
	CHECK(session != NULL);
	if (session != NULL) {
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &(double){ 0.5 });
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("Quality"), number) == 0);
		CFRelease(number);
		CHECK(n2fake_session_encode(session, picture, pts, duration, NULL) == 0);
		CHECK(wp05_stub.last_config.qp == 31);
		n2fake_session_destroy(session);
	}

	/* The smaller keyframe interval wins: 5 over 20. */
	memset(&wp05_stub, 0, sizeof(wp05_stub));
	session = n2fake_session_create(n2avc_vt_create, AVC1, 64, 64);
	CHECK(session != NULL);
	if (session != NULL) {
		value = 10;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("ExpectedFrameRate"), number) == 0);
		CFRelease(number);
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &(double){ 2.0 });
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("MaxKeyFrameIntervalDuration"), number) == 0);
		CFRelease(number);
		value = 5;
		number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		CHECK(number != NULL);
		CHECK(n2fake_session_set_property(session, N2STR("MaxKeyFrameInterval"), number) == 0);
		CFRelease(number);
		CHECK(n2fake_session_encode(session, picture, pts, duration, NULL) == 0);
		CHECK(wp05_stub.last_config.keyint == 5);
		n2fake_session_destroy(session);
	}

	if (picture != NULL)
		CFRelease(picture);
	CHECK(n2fake_live_objects() == live_before);
}
#endif

struct thread_arg {
	struct n2fake_session *session;
	CVPixelBufferRef picture;
	OSStatus result;
};

static void *encode_thread(void *opaque)
{
	struct thread_arg *arg = (struct thread_arg *)opaque;
	int i;

	arg->result = 0;
	for (i = 0; i < 8; i++) {
		CMTime pts = { i, 30, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 30, N2_CMTIME_VALID, 0 };
		OSStatus status = n2fake_session_encode(arg->session, arg->picture, pts, duration, NULL);
		if (status != 0) {
			arg->result = status;
			break;
		}
	}
	return NULL;
}

static void test_two_threads(void)
{
	long live_before = n2fake_live_objects();
	struct n2fake_session *a, *b;
	struct thread_arg args[2];
	pthread_t threads[2];
	int i;

	a = n2fake_session_create(n2avc_vt_create, AVC1, 176, 144);
	b = n2fake_session_create(n2avc_vt_create, AVC1, 176, 144);
	CHECK(a != NULL && b != NULL);
	if (a == NULL || b == NULL)
		return;

	memset(args, 0, sizeof(args));
	args[0].session = a;
	args[1].session = b;
	args[0].picture = make_picture(176, 144, N2_PIXFMT_420V, 4);
	args[1].picture = make_picture(176, 144, N2_PIXFMT_420V, 5);
	CHECK(args[0].picture != NULL && args[1].picture != NULL);
	args[0].result = args[1].result = -1;

	CHECK(pthread_create(&threads[0], NULL, encode_thread, &args[0]) == 0);
	CHECK(pthread_create(&threads[1], NULL, encode_thread, &args[1]) == 0);
	CHECK(pthread_join(threads[0], NULL) == 0);
	CHECK(pthread_join(threads[1], NULL) == 0);
	CHECK(args[0].result == 0);
	CHECK(args[1].result == 0);
	CHECK(n2fake_session_protocol_errors(a) == 0);
	CHECK(n2fake_session_protocol_errors(b) == 0);
	for (i = 0; i < 8; i++) {
		CHECK(n2fake_session_emitted(a, i, NULL, NULL, NULL, NULL) == 1);
		CHECK(n2fake_session_emitted(b, i, NULL, NULL, NULL, NULL) == 1);
	}

	for (i = 0; i < 2; i++)
		if (args[i].picture != NULL)
			CFRelease(args[i].picture);
	n2fake_session_destroy(a);
	n2fake_session_destroy(b);
	CHECK(n2fake_live_objects() == live_before);
}

#if defined(N2AVC_REAL)

static int write_bytes(FILE *file, const void *data, size_t size)
{
	return fwrite(data, 1, size, file) == size;
}

static void test_real_decode(const char *outdir)
{
	long live_before = n2fake_live_objects();
	struct n2fake_session *session;
	CVPixelBufferRef pictures[10];
	CMSampleBufferRef first = NULL;
	CMFormatDescriptionRef format;
	const uint8_t *sps = NULL, *pps = NULL;
	size_t sps_size = 0, pps_size = 0, count = 0;
	int nal_length = 0;
	char h264_path[1024], yuv_path[1024], log_path[1024], command[2560];
	FILE *file;
	size_t i;
	int ok;

	memset(pictures, 0, sizeof(pictures));
	session = n2fake_session_create(n2avc_vt_create, AVC1, 176, 144);
	CHECK(session != NULL);
	if (session == NULL)
		return;

	for (i = 0; i < 10; i++) {
		CMTime pts = { (int64_t)i, 10, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 10, N2_CMTIME_VALID, 0 };
		pictures[i] = make_picture(176, 144, N2_PIXFMT_420V, (int)i);
		CHECK(pictures[i] != NULL);
		CHECK(n2fake_session_encode(session, pictures[i], pts, duration, NULL) == 0);
	}
	CHECK(n2fake_session_protocol_errors(session) == 0);
	CHECK(n2fake_session_emitted(session, 10, NULL, NULL, NULL, NULL) == 0);

	CHECK(n2fake_session_emitted(session, 0, NULL, NULL, &first, NULL) == 1);
	format = (first != NULL) ? CMSampleBufferGetFormatDescription(first) : NULL;
	CHECK(format != NULL);
	if (format == NULL) {
		n2fake_session_destroy(session);
		return;
	}
	CHECK(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, &sps, &sps_size, &count,
	                                                         &nal_length) == 0);
	CHECK(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 1, &pps, &pps_size, &count,
	                                                         &nal_length) == 0);
	CHECK(sps != NULL && sps_size > 0 && pps != NULL && pps_size > 0);

	snprintf(h264_path, sizeof(h264_path), "%s/wp05_real_176x144.h264", outdir);
	snprintf(yuv_path, sizeof(yuv_path), "%s/wp05_real_176x144.yuv", outdir);
	snprintf(log_path, sizeof(log_path), "%s/wp05_real_ffmpeg.log", outdir);
	file = fopen(h264_path, "wb");
	CHECK(file != NULL);
	if (file == NULL) {
		n2fake_session_destroy(session);
		return;
	}
	ok = 1;
	ok = ok && write_bytes(file, "\x00\x00\x00\x01", 4);
	ok = ok && write_bytes(file, sps, sps_size);
	ok = ok && write_bytes(file, "\x00\x00\x00\x01", 4);
	ok = ok && write_bytes(file, pps, pps_size);

	for (i = 0; i < 10 && ok; i++) {
		CMSampleBufferRef sample = NULL;
		CMBlockBufferRef block;
		uint8_t *data;
		size_t length, offset = 0;

		if (n2fake_session_emitted(session, (int)i, NULL, NULL, &sample, NULL) != 1 || sample == NULL) {
			ok = 0;
			break;
		}
		block = CMSampleBufferGetDataBuffer(sample);
		length = CMBlockBufferGetDataLength(block);
		data = (uint8_t *)malloc(length);
		if (data == NULL) {
			ok = 0;
			break;
		}
		if (CMBlockBufferCopyDataBytes(block, 0, length, data) != 0) {
			free(data);
			ok = 0;
			break;
		}
		while (offset + 4 <= length) {
			uint32_t nal_size = be32(data + offset);
			offset += 4;
			if (nal_size == 0 || offset + nal_size > length) {
				ok = 0;
				break;
			}
			if (!write_bytes(file, "\x00\x00\x00\x01", 4) ||
			    !write_bytes(file, data + offset, nal_size)) {
				ok = 0;
				break;
			}
			offset += nal_size;
		}
		free(data);
		if (offset != length)
			ok = 0;
	}
	if (fclose(file) != 0)
		ok = 0;
	CHECK(ok);

	if (ok) {
		long log_size = -1;
		snprintf(command, sizeof(command),
		         "ffmpeg -v error -y -f h264 -i \"%s\" -f rawvideo -pix_fmt yuv420p \"%s\" 2>\"%s\"",
		         h264_path, yuv_path, log_path);
		CHECK(system(command) == 0);
		/* A decodable picture count is not enough: byte-stream syntax errors must be absent too. */
		file = fopen(log_path, "rb");
		CHECK(file != NULL);
		if (file != NULL) {
			if (fseek(file, 0, SEEK_END) == 0)
				log_size = ftell(file);
			if (log_size > 0) {
				char *text = (char *)malloc((size_t)log_size + 1);
				if (text != NULL && fseek(file, 0, SEEK_SET) == 0 &&
				    fread(text, 1, (size_t)log_size, file) == (size_t)log_size) {
					text[log_size] = '\0';
					fprintf(stderr, "ffmpeg stderr for the real stream:\n%s\n", text);
				}
				free(text);
			}
			fclose(file);
			CHECK(log_size == 0);
			remove(log_path);
		}
		file = fopen(yuv_path, "rb");
		CHECK(file != NULL);
		if (file != NULL) {
			long size;
			if (fseek(file, 0, SEEK_END) != 0)
				size = -1;
			else
				size = ftell(file);
			fclose(file);
			/* 176x144 YUV420p = 38016 bytes per picture; exactly ten pictures. */
			CHECK(size == (long)(10 * 176 * 144 * 3 / 2));
		}
		remove(yuv_path);
	}

	for (i = 0; i < 10; i++)
		if (pictures[i] != NULL)
			CFRelease(pictures[i]);
	n2fake_session_destroy(session);
	CHECK(n2fake_live_objects() == live_before);
}

#endif /* N2AVC_REAL */

int main(int argc, char **argv)
{
	const char *outdir = (argc > 1) ? argv[1] : "build/WP05";

	test_create_and_dimensions();
	test_five_frames();
	test_properties();
#if !defined(N2AVC_REAL)
	test_config_math();
#endif
	test_supported_dictionary();
	test_error_paths();
	test_two_threads();
#if defined(N2AVC_REAL)
	test_real_decode(outdir);
#else
	(void)outdir;
#endif

	if (g_failures != 0) {
		fprintf(stderr, "T_AVC_VT FAIL %d/%d checks failed\n", g_failures, g_checks);
		return 1;
	}
	printf("T_AVC_VT PASS %d\n", g_checks);
	return 0;
}
