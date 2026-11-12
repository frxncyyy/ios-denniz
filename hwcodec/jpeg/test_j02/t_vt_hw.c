/* SPDX-License-Identifier: MIT */
/*
 * t_vt_hw.c -- host tests of jpeg/n2jpeg_hw_vt.c against gpu/video/fake (the fake CoreFoundation/CoreMedia/CoreVideo and the
 * VideoToolbox session driver) with fake_dev.c as /dev/redwood-jpeg.
 *
 * What is checked: the vtable is the frozen one; startSession asks for NV12 and BGRA; a 420v and a 420f frame each produce
 * exactly one emitted CMSampleBuffer holding a complete JPEG of the session size; the Quality property reaches the device
 * request; a device error and a missing device each produce one error emit and -12912; wrong formats/dimensions produce
 * -12902; read-only/unknown properties are refused; the registration info really claims hardware; no object leaks.
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_apple.h"
#include "fake_dev.h"
#include "n2_apple.h"
#include "n2vt_abi.h"
#include "rwjpeg.h"

/* Local declarations: jpeg/n2jpeg_hw_vt.c has no header of its own in the work tree (its factory is consumed by the plug-in). */
extern OSStatus n2jpeg_hw_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut);
extern CFDictionaryRef n2jpeg_hw_vt_copy_registration_info(void);
extern int n2jpeg_hw_vt_available(void);
extern void n2jpeg_hw_vt_test_set_opener(int (*fn)(rwjpeg **out));

/* The plug-in's console log; the test provides the sink instead of /dev/console. */
static char g_last_say[512];
static pthread_mutex_t g_say_lock = PTHREAD_MUTEX_INITIALIZER;

void
n2vt_say(const char *format, ...)
{
	va_list args;

	pthread_mutex_lock(&g_say_lock);
	va_start(args, format);
	vsnprintf(g_last_say, sizeof(g_last_say), format, args);
	va_end(args);
	pthread_mutex_unlock(&g_say_lock);
}

#define JPEG 0x6a706567u
#define AVC1 0x61766331u

static int g_checks;
static int g_failures;

#define CHECK(cond) do { \
	g_checks++; \
	if (!(cond)) { \
		g_failures++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static int
no_device_open(rwjpeg **out)
{
	*out = NULL;
	return RWJPEG_E_FALLBACK;
}

static uint8_t *
make_nv12(uint32_t w, uint32_t h)
{
	uint8_t *p = (uint8_t *)malloc((size_t)w * h * 3u / 2u);
	uint32_t x, y;

	if (p == NULL)
		return NULL;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			p[(size_t)y * w + x] = (uint8_t)((x * 5u + y * 3u) & 0xffu);
	for (y = 0; y < h / 2u; y++)
		for (x = 0; x < w / 2u; x++) {
			p[(size_t)w * h + (size_t)y * w + 2u * x] = (uint8_t)(64u + (y & 31u));
			p[(size_t)w * h + (size_t)y * w + 2u * x + 1u] = (uint8_t)(192u - (x & 31u));
		}
	return p;
}

static int
fill_buffer(CVPixelBufferRef pb, const uint8_t *tight, uint32_t w, uint32_t h)
{
	uint8_t *y_dst, *uv_dst;
	size_t y_stride, uv_stride;
	uint32_t row;

	if (CVPixelBufferLockBaseAddress(pb, 0) != 0)
		return -1;
	y_dst = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
	uv_dst = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
	y_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
	uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
	if (y_dst == NULL || uv_dst == NULL) {
		(void)CVPixelBufferUnlockBaseAddress(pb, 0);
		return -1;
	}
	for (row = 0; row < h; row++)
		memcpy(y_dst + (size_t)row * y_stride, tight + (size_t)row * w, w);
	for (row = 0; row < h / 2u; row++)
		memcpy(uv_dst + (size_t)row * uv_stride, tight + (size_t)w * h + (size_t)row * w, w);
	(void)CVPixelBufferUnlockBaseAddress(pb, 0);
	return 0;
}

static int
check_jpeg(const uint8_t *p, size_t n, unsigned want_w, unsigned want_h)
{
	size_t pos = 2;
	unsigned w = 0, h = 0;

	if (n < 4 || p[0] != 0xff || p[1] != 0xd8 || p[n - 2] != 0xff || p[n - 1] != 0xd9)
		return -1;
	while (pos + 4 <= n) {
		uint8_t marker = p[pos + 1];
		size_t len;

		if (p[pos] != 0xff)
			return -1;
		if (marker == 0xff) {
			pos++;
			continue;
		}
		if (marker == 0xda)
			break;
		len = ((size_t)p[pos + 2] << 8) | p[pos + 3];
		if (len < 2 || pos + 2 + len > n)
			return -1;
		if (marker == 0xc0) {
			h = ((unsigned)p[pos + 5] << 8) | p[pos + 6];
			w = ((unsigned)p[pos + 7] << 8) | p[pos + 8];
		}
		pos += 2 + len;
	}
	return (w == want_w && h == want_h) ? 0 : -1;
}

static int
emitted_jpeg(struct n2fake_session *s, int index, uint8_t *jpeg, size_t cap, size_t *size_out,
             OSStatus *status_out)
{
	CMSampleBufferRef sbuf = NULL;
	CMBlockBufferRef block;
	OSStatus status = 12345;
	size_t size;

	if (!n2fake_session_emitted(s, index, &status, NULL, &sbuf, NULL))
		return -1;
	if (status_out != NULL)
		*status_out = status;
	if (status != 0 || sbuf == NULL)
		return -1;
	block = CMSampleBufferGetDataBuffer(sbuf);
	if (block == NULL)
		return -1;
	size = CMBlockBufferGetDataLength(block);
	if (size > cap)
		return -1;
	if (CMBlockBufferCopyDataBytes(block, 0, size, jpeg) != 0)
		return -1;
	*size_out = size;
	return 0;
}

int
main(void)
{
	VTVideoEncoderRef enc = NULL;
	struct n2fake_session *s;
	const n2_VTVideoEncoderVTable *vt;
	CFDictionaryRef props = NULL;
	CFDictionaryRef info = NULL;
	uint8_t *nv12 = make_nv12(640, 480);
	uint8_t *jpeg = (uint8_t *)malloc(640 * 480 + 65536);
	size_t jpeg_size = 0;
	CVPixelBufferRef pb = NULL;
	CFTypeRef value = NULL;
	OSStatus status;
	CMTime pts = { 1001, 30000, N2_CMTIME_VALID, 0 };
	CMTime dur = { 1001, 30000, N2_CMTIME_VALID, 0 };
	int32_t fmt;
	CFArrayRef formats;

	CHECK(nv12 != NULL && jpeg != NULL);

	/* ---- the factory refuses anything but 'jpeg' and a NULL out pointer ---- */
	CHECK(n2jpeg_hw_vt_create(AVC1, kCFAllocatorDefault, &enc) == N2_kVTParameterErr);
	CHECK(n2jpeg_hw_vt_create(JPEG, kCFAllocatorDefault, NULL) == N2_kVTParameterErr);

	n2jpeg_hw_vt_test_set_opener(n2rwj_test_open);
	fake_reset();

	/* ---- a session: startSession asks for the three formats; the vtable is the frozen one ---- */
	s = n2fake_session_create(n2jpeg_hw_vt_create, JPEG, 640, 480);
	CHECK(s != NULL);
	if (s == NULL)
		goto done;
	vt = n2fake_session_vtable(s);
	CHECK(vt != NULL && vt->baseClass != NULL && vt->encoderClass != NULL);
	CHECK(vt->encoderClass->version == 1 && vt->baseClass->version == 1);
	CHECK(vt->baseClass->derivedStorageSize > 0);
	CHECK(vt->encoderClass->startSession != NULL && vt->encoderClass->encodeFrame != NULL);
	CHECK(vt->encoderClass->completeFrames != NULL && vt->encoderClass->copySupportedPropertyDictionary != NULL);
	CHECK(vt->encoderClass->setProperties == NULL && vt->encoderClass->copySerializableProperties == NULL);
	CHECK(vt->encoderClass->prepareToEncodeFrames == NULL && vt->encoderClass->startTileSession == NULL);
	CHECK(vt->encoderClass->prepareToEncodeTiles == NULL && vt->encoderClass->encodeTile == NULL);
	CHECK(vt->encoderClass->completeTiles == NULL);

	formats = (CFArrayRef)CFDictionaryGetValue(n2fake_session_pixel_attributes(s), kCVPixelBufferPixelFormatTypeKey);
	CHECK(formats != NULL && CFArrayGetCount(formats) == 3);
	if (formats != NULL) {
		fmt = 0;
		(void)CFNumberGetValue((CFNumberRef)CFArrayGetValueAtIndex(formats, 0), kCFNumberSInt32Type, &fmt);
		CHECK(fmt == (int32_t)N2_PIXFMT_420V);
		(void)CFNumberGetValue((CFNumberRef)CFArrayGetValueAtIndex(formats, 1), kCFNumberSInt32Type, &fmt);
		CHECK(fmt == (int32_t)N2_PIXFMT_420F);
		(void)CFNumberGetValue((CFNumberRef)CFArrayGetValueAtIndex(formats, 2), kCFNumberSInt32Type, &fmt);
		CHECK(fmt == (int32_t)0x42475241);
	}

	/* ---- a 420v frame: one emit, a real JPEG of 640x480, the default quality 90 ---- */
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 640, 480, N2_PIXFMT_420V, NULL, &pb) == 0);
	CHECK(fill_buffer(pb, nv12, 640, 480) == 0);
	CHECK(n2fake_session_encode(s, pb, pts, dur, NULL) == 0);
	CHECK(n2fake_session_protocol_errors(s) == 0);
	CHECK(emitted_jpeg(s, 0, jpeg, 640 * 480 + 65536, &jpeg_size, &status) == 0);
	CHECK(status == 0 && jpeg_size > 1000);
	CHECK(check_jpeg(jpeg, jpeg_size, 640, 480) == 0);
	CHECK(n2fake_dev.last.format == RWJ_FMT_NV12 && n2fake_dev.last.quality == 90);
	CHECK(n2fake_dev.last.width == 640 && n2fake_dev.last.height == 480);
	{
		CMSampleBufferRef sbuf = NULL;
		CMSampleTimingInfo timing;
		size_t sample_size = 0;

		CHECK(n2fake_session_emitted(s, 0, NULL, NULL, &sbuf, NULL) && sbuf != NULL);
		CHECK(n2fake_sample_timing(sbuf, &timing, &sample_size) == 1);
		CHECK(sample_size == jpeg_size);
		CHECK(timing.presentationTimeStamp.value == pts.value &&
		      timing.presentationTimeStamp.timescale == pts.timescale &&
		      timing.presentationTimeStamp.flags == pts.flags);
		CHECK(timing.duration.value == dur.value && timing.duration.timescale == dur.timescale);
	}
	CFRelease(pb);
	pb = NULL;

	/* ---- Quality 0.5 reaches the device as 50; 1.5 is refused ---- */
	{
		double half = 0.5;

		value = (CFTypeRef)CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &half);
	}
	CHECK(n2fake_session_set_property(s, N2STR("Quality"), value) == 0);
	CFRelease(value);
	value = NULL;
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 640, 480, N2_PIXFMT_420F, NULL, &pb) == 0);
	CHECK(fill_buffer(pb, nv12, 640, 480) == 0);
	CHECK(n2fake_session_encode(s, pb, pts, dur, NULL) == 0);
	CHECK(n2fake_dev.last.quality == 50);
	CHECK(emitted_jpeg(s, 1, jpeg, 640 * 480 + 65536, &jpeg_size, &status) == 0);
	CHECK(status == 0 && check_jpeg(jpeg, jpeg_size, 640, 480) == 0);
	CFRelease(pb);
	pb = NULL;
	{
		double over = 1.5;

		value = (CFTypeRef)CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &over);
		CHECK(n2fake_session_set_property(s, N2STR("Quality"), value) == N2_kVTParameterErr);
		CFRelease(value);
		value = NULL;
	}

	/* ---- properties: hardware flag true, pending 0, unknown/read-only refused ---- */
	CHECK(n2fake_session_copy_property(s, N2STR("UsingHardwareAcceleratedVideoEncoder"), &value) == 0);
	CHECK(value == (CFTypeRef)kCFBooleanTrue);
	if (value != NULL)
		CFRelease(value);
	value = NULL;
	CHECK(n2fake_session_copy_property(s, N2STR("NumberOfPendingFrames"), &value) == 0);
	if (value != NULL) {
		int32_t pending = -1;

		CHECK(CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &pending) == 1 && pending == 0);
		CFRelease(value);
		value = NULL;
	}
	CHECK(n2fake_session_set_property(s, N2STR("NoSuchProperty"), N2STR("x")) == N2_kVTPropertyNotSupportedErr);
	CHECK(n2fake_session_set_property(s, N2STR("UsingHardwareAcceleratedVideoEncoder"), kCFBooleanTrue) ==
	      N2_kVTPropertyNotSupportedErr);

	/* ---- the supported-property dictionary ---- */
	CHECK(vt->encoderClass->copySupportedPropertyDictionary
	      (n2fake_session_encoder(s), &props) == 0);
	CHECK(props != NULL && CFDictionaryGetValue(props, N2STR("Quality")) != NULL);
	CHECK(props != NULL && CFDictionaryGetValue(props, N2STR("UsingHardwareAcceleratedVideoEncoder")) != NULL);
	if (props != NULL)
		CFRelease(props);
	props = NULL;
	CHECK(n2fake_session_complete(s) == 0);

	/* ---- wrong dimensions, wrong format, NULL image: one error emit each, -12902 ---- */
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 320, 240, N2_PIXFMT_420V, NULL, &pb) == 0);
	CHECK(n2fake_session_encode(s, pb, pts, dur, NULL) == N2_kVTParameterErr);
	CHECK(n2fake_session_emitted(s, 2, &status, NULL, NULL, NULL) && status == N2_kVTParameterErr);
	CFRelease(pb);
	pb = NULL;
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 640, 480, N2_PIXFMT_I420, NULL, &pb) == 0);
	CHECK(n2fake_session_encode(s, pb, pts, dur, NULL) == N2_kVTParameterErr);
	CHECK(n2fake_session_emitted(s, 3, &status, NULL, NULL, NULL) && status == N2_kVTParameterErr);
	CFRelease(pb);
	pb = NULL;
	CHECK(n2fake_session_encode(s, NULL, pts, dur, NULL) == N2_kVTParameterErr);
	CHECK(n2fake_session_emitted(s, 4, &status, NULL, NULL, NULL) && status == N2_kVTParameterErr);

	/* ---- a device error is -12912 (a VTError), and the same object works again afterwards ---- */
	n2fake_dev.fail_eio = 1;
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 640, 480, N2_PIXFMT_420V, NULL, &pb) == 0);
	CHECK(fill_buffer(pb, nv12, 640, 480) == 0);
	CHECK(n2fake_session_encode(s, pb, pts, dur, NULL) == N2_kVTVideoEncoderMalfunctionErr);
	CHECK(n2fake_session_emitted(s, 5, &status, NULL, NULL, NULL) && status == N2_kVTVideoEncoderMalfunctionErr);
	n2fake_dev.fail_eio = 0;
	CHECK(n2fake_session_encode(s, pb, pts, dur, NULL) == 0);
	CHECK(emitted_jpeg(s, 6, jpeg, 640 * 480 + 65536, &jpeg_size, &status) == 0);
	CHECK(status == 0 && check_jpeg(jpeg, jpeg_size, 640, 480) == 0);
	CFRelease(pb);
	pb = NULL;
	CHECK(n2fake_session_protocol_errors(s) == 0);
	n2fake_session_destroy(s);
	s = NULL;

	/* ---- no device at all: registration is not for this process, and a frame cannot be encoded ---- */
	n2jpeg_hw_vt_test_set_opener(no_device_open);
	s = n2fake_session_create(n2jpeg_hw_vt_create, JPEG, 128, 128);
	CHECK(s != NULL);
	if (s != NULL) {
		uint8_t *small = make_nv12(128, 128);

		CHECK(small != NULL);
		CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 128, 128, N2_PIXFMT_420V, NULL, &pb) == 0);
		CHECK(fill_buffer(pb, small, 128, 128) == 0);
		CHECK(n2fake_session_encode(s, pb, pts, dur, NULL) == N2_kVTVideoEncoderMalfunctionErr);
		CHECK(n2fake_session_emitted(s, 0, &status, NULL, NULL, NULL) && status == N2_kVTVideoEncoderMalfunctionErr);
		CFRelease(pb);
		pb = NULL;
		free(small);
		n2fake_session_destroy(s);
		s = NULL;
	}
	CHECK(n2jpeg_hw_vt_available() == 0);
	n2jpeg_hw_vt_test_set_opener(n2rwj_test_open);

	/* ---- registration info claims hardware and names its own implementation ---- */
	info = n2jpeg_hw_vt_copy_registration_info();
	CHECK(info != NULL);
	if (info != NULL) {
		char id[128];

		CHECK(CFDictionaryGetValue(info, kVTVideoEncoder_IsHardwareAccelerated) == (const void *)kCFBooleanTrue);
		CHECK(CFDictionaryGetValue(info, N2STR("CMClassImplementationID")) != NULL);
		CHECK(CFStringGetCString((CFStringRef)CFDictionaryGetValue(info, N2STR("CMClassImplementationID")),
		                         id, (CFIndex)sizeof(id), kCFStringEncodingUTF8));
		CHECK(strcmp(id, "com.nord2.videotoolbox.videoencoder.jpeg.mtk") == 0);
		CFRelease(info);
		info = NULL;
	}

done:
	if (s != NULL)
		n2fake_session_destroy(s);
	if (pb != NULL)
		CFRelease(pb);
	if (value != NULL)
		CFRelease(value);
	free(jpeg);
	free(nv12);
	n2fake_vt_reset();
	CHECK(n2fake_live_objects() == 0);

	printf("T_VT_HW %s %d/%d\n", g_failures == 0 ? "PASS" : "FAIL", g_checks - g_failures, g_checks);
	return g_failures == 0 ? 0 : 1;
}
