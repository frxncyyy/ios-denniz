/*
 * t_jpeg_vt.c -- host tests for the R1 VideoToolbox JPEG glue (vt/n2jpeg_vt.c) against WP02's fake Apple runtime.
 *
 * What is proved here and cannot be proved by the encoder's own tests:
 *   - the vtable is the one include/n2vt_abi.h freezes (version, storage size, the four used class slots, all reserved
 *     slots NULL), and the object is created through CMDerivedObjectCreate with VTVideoEncoderGetClassID();
 *   - startSession asks for NV12 ('420v'/'420f') at the session's dimensions;
 *   - every submitted frame produces exactly one emit, with the exact bytes an independent n2jpeg encode of the same
 *     NV12 picture produces, the submitted timing, a sample size equal to the payload, and a 'jpeg' format description
 *     when the runtime can make one;
 *   - wrong pixel format, wrong dimensions, a NULL image and a session without startSession each produce one error emit
 *     (or a clean error), never a crash and never a silent frame;
 *   - the property surface: Quality maps to the encoder, read-only and unknown properties are refused, the supported
 *     property dictionary exists.
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
#include "n2jpeg.h"
#include "n2jpeg_vt.h"
#include "n2vt_abi.h"

/* The glue calls n2vt_say() from WP01's vt/n2vt_log.h.  The host test provides the sink instead of /dev/console; the real
 * implementation is thread-safe (a mutex), so the sink must be too -- two encoders on two threads do log. */
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

#define CHECK_EQ(a, b) do { \
	long long va = (long long)(a), vb = (long long)(b); \
	g_checks++; \
	if (va != vb) { \
		g_failures++; \
		fprintf(stderr, "FAIL %s:%d: %s = %lld, expected %s = %lld\n", __FILE__, __LINE__, \
		        #a, va, #b, vb); \
	} \
} while (0)

/* -------------------------------------------------------------------------------------------------------------------------------
 * NV12 test picture, independent of the encoder.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static uint8_t *
make_nv12(uint32_t w, uint32_t h, unsigned seed)
{
	uint8_t *p = (uint8_t *)malloc((size_t)w * h * 3u / 2u);
	uint32_t x, y;

	if (p == NULL)
		return NULL;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			p[(size_t)y * w + x] = (uint8_t)((x * 3 + y * 5 + seed * 17) & 255);
	for (y = 0; y < h / 2; y++) {
		for (x = 0; x < w / 2; x++) {
			p[(size_t)w * h + (size_t)y * w + 2 * x] = (uint8_t)(96 + ((x + seed) & 63));
			p[(size_t)w * h + (size_t)y * w + 2 * x + 1] = (uint8_t)(160 - ((y + seed) & 63));
		}
	}
	return p;
}

static int
fill_pixel_buffer(CVPixelBufferRef pb, const uint8_t *nv12, uint32_t w, uint32_t h)
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
		memcpy(y_dst + row * y_stride, nv12 + (size_t)row * w, w);
	for (row = 0; row < h / 2; row++)
		memcpy(uv_dst + row * uv_stride, nv12 + (size_t)w * h + (size_t)row * w, w);
	(void)CVPixelBufferUnlockBaseAddress(pb, 0);
	return 0;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * One independent n2jpeg encode of the same samples, for byte comparison with what the glue emitted.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static uint8_t *
reference_jpeg(const uint8_t *nv12, uint32_t w, uint32_t h, int quality, size_t *size_out)
{
	n2jpeg_config config;
	n2jpeg_picture picture;
	n2jpeg_enc *enc = NULL;
	uint8_t *buf = NULL;
	size_t size = 0;

	memset(&config, 0, sizeof(config));
	config.size = (uint32_t)sizeof(config);
	config.width = (int32_t)w;
	config.height = (int32_t)h;
	config.quality = quality;
	if (n2jpeg_open(&config, &enc) != N2JPEG_OK)
		return NULL;
	buf = (uint8_t *)malloc(n2jpeg_bound(enc));
	if (buf == NULL) {
		n2jpeg_close(enc);
		return NULL;
	}
	memset(&picture, 0, sizeof(picture));
	picture.size = (uint32_t)sizeof(picture);
	picture.y = nv12;
	picture.y_stride = w;
	picture.uv = nv12 + (size_t)w * h;
	picture.uv_stride = w;
	if (n2jpeg_encode(enc, &picture, buf, n2jpeg_bound(enc), &size) != N2JPEG_OK) {
		free(buf);
		n2jpeg_close(enc);
		return NULL;
	}
	n2jpeg_close(enc);
	*size_out = size;
	return buf;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Sample buffer access
 * ----------------------------------------------------------------------------------------------------------------------------- */

static uint8_t *
sample_bytes(CMSampleBufferRef sample, size_t *size_out)
{
	CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
	size_t size;
	uint8_t *bytes;

	*size_out = 0;
	if (block == NULL)
		return NULL;
	size = CMBlockBufferGetDataLength(block);
	if (size == 0)
		return NULL;
	bytes = (uint8_t *)malloc(size);
	if (bytes == NULL)
		return NULL;
	if (CMBlockBufferCopyDataBytes(block, 0, size, bytes) != 0) {
		free(bytes);
		return NULL;
	}
	*size_out = size;
	return bytes;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------------------------------------------------------------------- */

static void
test_vtable(void)
{
	struct n2fake_session *session;
	const n2_VTVideoEncoderVTable *vtable;

	/* include/n2vt_abi.h's own static asserts compile here, which is the layout check. */
	_Static_assert(sizeof(n2_CMBaseClass) == 0x58, "base class layout");
	_Static_assert(sizeof(n2_VTVideoEncoderClass) == 0xa8, "encoder class layout");
	_Static_assert(__builtin_offsetof(n2_VTVideoEncoderClass, startTileSession) == 0x88, "tile slots");

	session = n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 48);
	CHECK(session != NULL);
	if (session == NULL)
		return;
	vtable = n2fake_session_vtable(session);
	CHECK(vtable != NULL && vtable->reserved == NULL);
	CHECK(vtable->baseClass != NULL && vtable->baseClass->version == 1);
	CHECK(vtable->baseClass->derivedStorageSize >= sizeof(void *) * 8);
	CHECK(vtable->baseClass->finalize != NULL);
	CHECK(vtable->baseClass->copyProperty != NULL && vtable->baseClass->setProperty != NULL);
	CHECK(vtable->baseClass->copyDebugDescription != NULL);
	CHECK(vtable->encoderClass != NULL && vtable->encoderClass->version == 1);
	CHECK(vtable->encoderClass->startSession != NULL && vtable->encoderClass->encodeFrame != NULL);
	CHECK(vtable->encoderClass->completeFrames != NULL);
	CHECK(vtable->encoderClass->copySupportedPropertyDictionary != NULL);
	CHECK(vtable->encoderClass->setProperties == NULL);
	CHECK(vtable->encoderClass->copySerializableProperties == NULL);
	CHECK(vtable->encoderClass->prepareToEncodeFrames == NULL);
	CHECK(vtable->encoderClass->startTileSession == NULL);
	CHECK(vtable->encoderClass->prepareToEncodeTiles == NULL);
	CHECK(vtable->encoderClass->encodeTile == NULL);
	CHECK(vtable->encoderClass->completeTiles == NULL);
	CHECK(n2fake_session_protocol_errors(session) == 0);
	n2fake_session_destroy(session);
}

static void
test_create(void)
{
	VTVideoEncoderRef object = (VTVideoEncoderRef)(void *)1;

	CHECK_EQ(n2jpeg_vt_create(JPEG, kCFAllocatorDefault, NULL), N2_kVTParameterErr);
	CHECK_EQ(n2jpeg_vt_create(AVC1, kCFAllocatorDefault, &object), N2_kVTParameterErr);
	CHECK(object == (VTVideoEncoderRef)(void *)1);
	CHECK_EQ(n2jpeg_vt_create(JPEG, kCFAllocatorDefault, &object), 0);
	CHECK(object != NULL);
	if (object != NULL) {
		CMBaseObjectRef base = VTVideoEncoderGetCMBaseObject(object);

		CHECK(base != NULL);
		/* copyDebugDescription through the CMBaseObject's base class. */
		{
			struct n2fake_session *probe = n2fake_session_create(n2jpeg_vt_create, JPEG, 16, 16);

			if (probe != NULL) {
				CFStringRef description = NULL;
				const n2_CMBaseClass *base_class = n2fake_session_vtable(probe)->baseClass;

				description = base_class->copyDebugDescription((CMBaseObjectRef)n2fake_session_encoder(probe));
				CHECK(description != NULL);
				if (description != NULL)
					CFRelease(description);
				n2fake_session_destroy(probe);
			}
		}
		CFRelease(object);
	}
}

static void
test_start_session(void)
{
	struct n2fake_session *session;
	CFDictionaryRef attributes;
	CFArrayRef formats;
	CFIndex count, i;
	int saw_420v = 0, saw_420f = 0;

	session = n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 48);
	CHECK(session != NULL);
	if (session == NULL)
		return;
	attributes = n2fake_session_pixel_attributes(session);
	CHECK(attributes != NULL);
	formats = attributes != NULL ? (CFArrayRef)CFDictionaryGetValue(attributes, kCVPixelBufferPixelFormatTypeKey) : NULL;
	CHECK(formats != NULL);
	count = formats != NULL ? CFArrayGetCount(formats) : 0;
	CHECK(count == 2);
	for (i = 0; i < count; i++) {
		CFNumberRef number = (CFNumberRef)CFArrayGetValueAtIndex(formats, i);
		int32_t format = 0;

		if (number != NULL && CFGetTypeID((CFTypeRef)number) == CFNumberGetTypeID() &&
		    CFNumberGetValue(number, kCFNumberSInt32Type, &format)) {
			if (format == (int32_t)N2_PIXFMT_420V)
				saw_420v = 1;
			if (format == (int32_t)N2_PIXFMT_420F)
				saw_420f = 1;
		}
	}
	CHECK(saw_420v && saw_420f);
	n2fake_session_destroy(session);

	/* Odd dimensions cannot be NV12 and are refused before anything is registered. */
	CHECK(n2fake_session_create(n2jpeg_vt_create, JPEG, 65, 48) == NULL);
	CHECK(n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 47) == NULL);
	CHECK(n2fake_session_create(n2jpeg_vt_create, JPEG, 0, 48) == NULL);
	CHECK(n2fake_session_create(n2jpeg_vt_create, JPEG, 8194, 64) == NULL);      /* beyond the still-image limit */
	{
		struct n2fake_session *ok = n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 48);

		CHECK(ok != NULL);
		n2fake_session_destroy(ok);
	}
}

static void
test_one_sample_per_frame(void)
{
	struct n2fake_session *session;
	CVPixelBufferRef pb = NULL;
	uint8_t *nv12;
	uint8_t *reference;
	size_t reference_size = 0;
	int frame;

	nv12 = make_nv12(64, 48, 1);
	CHECK(nv12 != NULL);
	if (nv12 == NULL)
		return;
	reference = reference_jpeg(nv12, 64, 48, 90, &reference_size);
	CHECK(reference != NULL && reference_size > 100);

	session = n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 48);
	CHECK(session != NULL);
	if (session == NULL) {
		free(nv12);
		free(reference);
		return;
	}
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 64, 48, N2_PIXFMT_420V, NULL, &pb) == 0 && pb != NULL);
	if (pb != NULL)
		CHECK(fill_pixel_buffer(pb, nv12, 64, 48) == 0);

	for (frame = 0; frame < 3 && pb != NULL; frame++) {
		CMTime pts, duration;
		OSStatus status;
		CMSampleBufferRef sample = NULL;
		void *token = NULL;
		size_t size = 0;
		uint8_t *bytes;

		pts.value = frame * 100;
		pts.timescale = 1;
		pts.flags = N2_CMTIME_VALID;
		pts.epoch = 0;
		duration.value = 100;
		duration.timescale = 1;
		duration.flags = N2_CMTIME_VALID;
		duration.epoch = 0;
		status = n2fake_session_encode(session, pb, pts, duration, NULL);
		CHECK_EQ(status, 0);
		CHECK(n2fake_session_emitted(session, frame, NULL, NULL, &sample, &token) == 1);
		CHECK(sample != NULL);
		bytes = sample != NULL ? sample_bytes(sample, &size) : NULL;
		CHECK(bytes != NULL);
		CHECK(size > 100 && size == reference_size);
		if (bytes != NULL && reference != NULL)
			CHECK(size == reference_size && memcmp(bytes, reference, size) == 0);
		if (bytes != NULL) {
			CHECK(bytes[0] == 0xff && bytes[1] == 0xd8);
			CHECK(bytes[size - 2] == 0xff && bytes[size - 1] == 0xd9);
			free(bytes);
		}
		{
			CMSampleTimingInfo timing;
			size_t sample_size = 0;

			CHECK(n2fake_sample_timing(sample, &timing, &sample_size) == 1);
			CHECK(timing.presentationTimeStamp.value == frame * 100);
			CHECK(timing.duration.value == 100);
			CHECK(sample_size == size);
		}
		{
			CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sample);

			if (CMVideoFormatDescriptionCreate != NULL) {
				CHECK(fmt != NULL);
			} else {
				/* WP02's fake does not have CMVideoFormatDescriptionCreate yet; the glue logs once and
				 * passes NULL.  Record the branch instead of pretending. */
				CHECK(fmt == NULL);
				CHECK(strstr(g_last_say, "CMVideoFormatDescriptionCreate") != NULL);
			}
		}
		CHECK(n2fake_sample_count(sample) == 1);
	}
	CHECK(n2fake_session_protocol_errors(session) == 0);
	n2fake_session_destroy(session);
	if (pb != NULL)
		CFRelease(pb);
	free(nv12);
	free(reference);
}

static void
test_quality_property(void)
{
	struct n2fake_session *session;
	CVPixelBufferRef pb = NULL;
	uint8_t *nv12;
	size_t sizes[2] = { 0, 0 };
	double qualities[2] = { 0.1, 0.95 };
	int i;

	nv12 = make_nv12(64, 48, 2);
	CHECK(nv12 != NULL);
	if (nv12 == NULL)
		return;
	session = n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 48);
	CHECK(session != NULL);
	if (session == NULL) {
		free(nv12);
		return;
	}
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 64, 48, N2_PIXFMT_420F, NULL, &pb) == 0 && pb != NULL);
	if (pb != NULL)
		CHECK(fill_pixel_buffer(pb, nv12, 64, 48) == 0);

	for (i = 0; i < 2 && pb != NULL; i++) {
		CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &qualities[i]);
		CMTime pts = { i, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CMSampleBufferRef sample = NULL;
		uint8_t *bytes;

		CHECK(number != NULL);
		CHECK_EQ(n2fake_session_set_property(session, N2STR("Quality"), number), 0);
		if (number != NULL)
			CFRelease(number);
		CHECK_EQ(n2fake_session_encode(session, pb, pts, duration, NULL), 0);
		CHECK(n2fake_session_emitted(session, i, NULL, NULL, &sample, NULL) == 1);
		bytes = sample != NULL ? sample_bytes(sample, &sizes[i]) : NULL;
		CHECK(bytes != NULL);
		free(bytes);
	}
	CHECK(sizes[0] > 0 && sizes[1] > sizes[0]);

	/* Out-of-range quality and the wrong CF type are refused, and the next frame still comes out. */
	{
		CFNumberRef bad = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &(double){ 1.5 });
		CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, "high", kCFStringEncodingUTF8);
		CMTime pts = { 5, 1, N2_CMTIME_VALID, 0 };
		CMTime one = { 1, 1, N2_CMTIME_VALID, 0 };
		CMSampleBufferRef sample = NULL;
		uint8_t *bytes;
		size_t size = 0;

		CHECK(bad != NULL && text != NULL);
		if (bad != NULL)
			CHECK_EQ(n2fake_session_set_property(session, N2STR("Quality"), bad), N2_kVTParameterErr);
		if (text != NULL)
			CHECK_EQ(n2fake_session_set_property(session, N2STR("Quality"), text), N2_kVTParameterErr);
		CHECK_EQ(n2fake_session_set_property(session, N2STR("NoSuchProperty"), kCFBooleanTrue), N2_kVTPropertyNotSupportedErr);
		CHECK_EQ(n2fake_session_set_property(session, N2STR("NumberOfPendingFrames"), kCFBooleanTrue), N2_kVTPropertyNotSupportedErr);
		CHECK_EQ(n2fake_session_encode(session, pb, pts, one, NULL), 0);
		CHECK(n2fake_session_emitted(session, 2, NULL, NULL, &sample, NULL) == 1);
		bytes = sample != NULL ? sample_bytes(sample, &size) : NULL;
		CHECK(bytes != NULL && size > 100);
		free(bytes);
		if (bad != NULL)
			CFRelease(bad);
		if (text != NULL)
			CFRelease(text);
	}
	n2fake_session_destroy(session);
	if (pb != NULL)
		CFRelease(pb);
	free(nv12);
}

static void
test_properties_and_dictionary(void)
{
	struct n2fake_session *session = n2fake_session_create(n2jpeg_vt_create, JPEG, 32, 32);
	CFTypeRef value = NULL;

	CHECK(session != NULL);
	if (session == NULL)
		return;

	CHECK_EQ(n2fake_session_copy_property(session, N2STR("UsingHardwareAcceleratedVideoEncoder"), &value), 0);
	CHECK(value != NULL && CFGetTypeID(value) == CFBooleanGetTypeID());
	if (value != NULL) {
		CHECK(CFBooleanGetValue((CFBooleanRef)value) == 0);
		CFRelease(value);
		value = NULL;
	}
	CHECK_EQ(n2fake_session_copy_property(session, N2STR("NumberOfPendingFrames"), &value), 0);
	CHECK(value != NULL && CFGetTypeID(value) == CFNumberGetTypeID());
	if (value != NULL) {
		int32_t pending = -1;
		CHECK(CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &pending));
		CHECK(pending == 0);
		CFRelease(value);
		value = NULL;
	}
	CHECK_EQ(n2fake_session_copy_property(session, N2STR("Quality"), &value), N2_kCMBaseObjectError_ValueNotAvailable);
	CHECK_EQ(n2fake_session_copy_property(session, N2STR("NoSuchProperty"), &value), N2_kCMBaseObjectError_ValueNotAvailable);

	{
		CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ 1 });
		CHECK(number != NULL);
		if (number != NULL) {
			CHECK_EQ(n2fake_session_set_property(session, N2STR("RealTime"), kCFBooleanTrue), 0);
			CHECK_EQ(n2fake_session_set_property(session, N2STR("MaxKeyFrameInterval"), number), 0);
			CHECK_EQ(n2fake_session_copy_property(session, N2STR("Quality"), &value), N2_kCMBaseObjectError_ValueNotAvailable);
			CFRelease(number);
		}
	}

	{
		const n2_VTVideoEncoderClass *klass = n2fake_session_vtable(session)->encoderClass;
		CFDictionaryRef supported = NULL;

		CHECK_EQ(klass->copySupportedPropertyDictionary(n2fake_session_encoder(session), &supported), 0);
		CHECK(supported != NULL);
		if (supported != NULL) {
			CHECK(n2fake_dictionary_count(supported) >= 20);
			CHECK(CFDictionaryGetValue(supported, N2STR("Quality")) != NULL);
			CHECK(CFDictionaryGetValue(supported, N2STR("UsingHardwareAcceleratedVideoEncoder")) != NULL);
			{
				CFDictionaryRef entry = (CFDictionaryRef)CFDictionaryGetValue(supported, N2STR("Quality"));
				CFStringRef type = entry != NULL ? (CFStringRef)CFDictionaryGetValue(entry, N2STR("PropertyType")) : NULL;
				CFStringRef status = entry != NULL ? (CFStringRef)CFDictionaryGetValue(entry, N2STR("ReadWriteStatus")) : NULL;

				CHECK(type != NULL && CFEqual(type, N2STR("Number")));
				CHECK(status != NULL && CFEqual(status, N2STR("ReadWrite")));
			}
			{
				CFDictionaryRef entry = (CFDictionaryRef)CFDictionaryGetValue(supported, N2STR("UsingHardwareAcceleratedVideoEncoder"));
				CFStringRef status = entry != NULL ? (CFStringRef)CFDictionaryGetValue(entry, N2STR("ReadWriteStatus")) : NULL;

				CHECK(status != NULL && CFEqual(status, N2STR("ReadOnly")));
			}
			CFRelease(supported);
		}
	}
	CHECK(n2fake_session_complete(session) == 0);
	n2fake_session_destroy(session);
}

static void
test_error_paths(void)
{
	struct n2fake_session *session = n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 48);
	CVPixelBufferRef good = NULL, i420 = NULL, wrong = NULL;
	uint8_t *nv12;

	nv12 = make_nv12(64, 48, 3);
	CHECK(nv12 != NULL);
	CHECK(session != NULL);
	if (session == NULL || nv12 == NULL) {
		free(nv12);
		n2fake_session_destroy(session);
		return;
	}
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 64, 48, N2_PIXFMT_420V, NULL, &good) == 0 && good != NULL);
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 64, 48, N2_PIXFMT_I420, NULL, &i420) == 0 && i420 != NULL);
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 32, 32, N2_PIXFMT_420V, NULL, &wrong) == 0 && wrong != NULL);
	if (good != NULL)
		CHECK(fill_pixel_buffer(good, nv12, 64, 48) == 0);

	{
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CMSampleBufferRef sample = NULL;
		OSStatus status = 0;

		/* NULL image: one emit carrying an error status, no sample buffer. */
		CHECK_EQ(n2fake_session_encode(session, NULL, pts, duration, NULL), N2_kVTParameterErr);
		CHECK(n2fake_session_emitted(session, 0, &status, NULL, &sample, NULL) == 1);
		CHECK_EQ(status, N2_kVTParameterErr);
		CHECK(sample == NULL);

		/* i420 is not NV12: refused, and the status is the parameter error. */
		CHECK_EQ(n2fake_session_encode(session, i420, pts, duration, NULL), N2_kVTParameterErr);
		CHECK(n2fake_session_emitted(session, 1, &status, NULL, &sample, NULL) == 1);
		CHECK_EQ(status, N2_kVTParameterErr);

		/* A frame of the wrong size is refused. */
		CHECK_EQ(n2fake_session_encode(session, wrong, pts, duration, NULL), N2_kVTParameterErr);
		CHECK(n2fake_session_emitted(session, 2, &status, NULL, &sample, NULL) == 1);
		CHECK_EQ(status, N2_kVTParameterErr);

		/* The encoder still works after the three failures. */
		CHECK_EQ(n2fake_session_encode(session, good, pts, duration, NULL), 0);
		CHECK(n2fake_session_emitted(session, 3, &status, NULL, &sample, NULL) == 1);
		CHECK_EQ(status, 0);
		CHECK(sample != NULL);
	}
	CHECK(n2fake_session_protocol_errors(session) == 0);
	n2fake_session_destroy(session);
	if (good != NULL)
		CFRelease(good);
	if (i420 != NULL)
		CFRelease(i420);
	if (wrong != NULL)
		CFRelease(wrong);
	free(nv12);

	/* A session that never started must answer N2_kVTInvalidSessionErr rather than crash. */
	{
		VTVideoEncoderRef encoder = NULL;
		CVPixelBufferRef pb = NULL;
		uint8_t *pixels = make_nv12(16, 16, 4);
		CMTime pts = { 0, 1, N2_CMTIME_VALID, 0 };

		CHECK_EQ(n2jpeg_vt_create(JPEG, kCFAllocatorDefault, &encoder), 0);
		CHECK(encoder != NULL && pixels != NULL);
		if (encoder != NULL && pixels != NULL &&
		    CVPixelBufferCreate(kCFAllocatorDefault, 16, 16, N2_PIXFMT_420V, NULL, &pb) == 0 && pb != NULL) {
			const n2_VTVideoEncoderClass *klass = NULL;
			CMBaseObjectRef base = VTVideoEncoderGetCMBaseObject(encoder);
			struct n2fake_session *probe = n2fake_session_create(n2jpeg_vt_create, JPEG, 16, 16);

			CHECK(base != NULL);
			CHECK(probe != NULL);
			if (probe != NULL) {
				klass = n2fake_session_vtable(probe)->encoderClass;
				n2fake_session_destroy(probe);
			}
			if (klass != NULL) {
				uint32_t flags = 0;
				CHECK_EQ(klass->encodeFrame(encoder, (VTVideoEncoderFrame)(void *)1, (CVImageBufferRef)pb,
				                            &pts, &pts, NULL, &flags), N2_kVTInvalidSessionErr);
			}
			CFRelease(pb);
		}
		if (encoder != NULL)
			CFRelease(encoder);
		free(pixels);
	}
}

static void
test_second_size(void)
{
	struct n2fake_session *session;
	CVPixelBufferRef pb = NULL;
	uint8_t *small;
	uint8_t *reference;
	size_t reference_size = 0;

	small = make_nv12(48, 32, 5);
	CHECK(small != NULL);
	reference = reference_jpeg(small, 48, 32, 90, &reference_size);
	CHECK(reference != NULL);

	/* A second encoder object at another size: dimensions reach n2jpeg and the payload is the independent encode.
	 * (Repeated StartSession on one live object is not exercised: WP02's fake has no accessor for its session
	 * handle, and calling the vtable with a made-up pointer would test the fake, not the glue.) */
	session = n2fake_session_create(n2jpeg_vt_create, JPEG, 48, 32);
	CHECK(session != NULL);
	if (session == NULL) {
		free(small);
		free(reference);
		return;
	}
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 48, 32, N2_PIXFMT_420V, NULL, &pb) == 0 && pb != NULL);
	if (pb != NULL)
		CHECK(fill_pixel_buffer(pb, small, 48, 32) == 0);
	{
		CMTime pts = { 7, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CMSampleBufferRef sample = NULL;
		size_t size = 0;
		uint8_t *bytes;

		CHECK_EQ(n2fake_session_encode(session, pb, pts, duration, NULL), 0);
		CHECK(n2fake_session_emitted(session, 0, NULL, NULL, &sample, NULL) == 1);
		bytes = sample != NULL ? sample_bytes(sample, &size) : NULL;
		CHECK(bytes != NULL && size == reference_size && memcmp(bytes, reference, size) == 0);
		free(bytes);
	}
	n2fake_session_destroy(session);
	if (pb != NULL)
		CFRelease(pb);
	free(small);
	free(reference);
}

/* Two encoders on two threads: the per-encoder mutex must be the only shared thing.  Compiled with TSan separately. */
struct thread_job {
	int             index;
	int             failures;
	int             frames;
};

static void *
thread_main(void *argument)
{
	struct thread_job *job = (struct thread_job *)argument;
	uint8_t *nv12 = make_nv12(64, 48, (unsigned)job->index);
	struct n2fake_session *session;
	CVPixelBufferRef pb = NULL;

	if (nv12 == NULL)
		return NULL;
	session = n2fake_session_create(n2jpeg_vt_create, JPEG, 64, 48);
	if (session == NULL) {
		free(nv12);
		return NULL;
	}
	if (CVPixelBufferCreate(kCFAllocatorDefault, 64, 48, N2_PIXFMT_420V, NULL, &pb) != 0 || pb == NULL ||
	    fill_pixel_buffer(pb, nv12, 64, 48) != 0) {
		n2fake_session_destroy(session);
		free(nv12);
		return NULL;
	}
	for (job->frames = 0; job->frames < 3; job->frames++) {
		CMTime pts = { job->frames, 1, N2_CMTIME_VALID, 0 };
		CMTime duration = { 1, 1, N2_CMTIME_VALID, 0 };
		CMSampleBufferRef sample = NULL;
		uint8_t *bytes;
		size_t size = 0;

		if (n2fake_session_encode(session, pb, pts, duration, NULL) != 0) {
			job->failures++;
			continue;
		}
		if (n2fake_session_emitted(session, job->frames, NULL, NULL, &sample, NULL) != 1) {
			job->failures++;
			continue;
		}
		bytes = sample != NULL ? sample_bytes(sample, &size) : NULL;
		if (bytes == NULL || size < 100) {
			job->failures++;
		}
		free(bytes);
	}
	if (n2fake_session_protocol_errors(session) != 0)
		job->failures++;
	n2fake_session_destroy(session);
	CFRelease(pb);
	free(nv12);
	return NULL;
}

static void
test_threads(void)
{
	pthread_t threads[2];
	struct thread_job jobs[2];

	memset(jobs, 0, sizeof(jobs));
	jobs[0].index = 1;
	jobs[1].index = 2;
	CHECK(pthread_create(&threads[0], NULL, thread_main, &jobs[0]) == 0);
	CHECK(pthread_create(&threads[1], NULL, thread_main, &jobs[1]) == 0);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	CHECK(jobs[0].frames == 3 && jobs[0].failures == 0);
	CHECK(jobs[1].frames == 3 && jobs[1].failures == 0);
}

int
main(void)
{
	long before = n2fake_live_objects();

	test_vtable();
	test_create();
	test_start_session();
	test_one_sample_per_frame();
	test_quality_property();
	test_properties_and_dictionary();
	test_error_paths();
	test_second_size();
	test_threads();

	CHECK(n2fake_live_objects() == before);

	if (g_failures != 0) {
		printf("T_JPEG_VT FAIL %d of %d\n", g_failures, g_checks);
		return 1;
	}
	printf("T_JPEG_VT PASS %d\n", g_checks);
	return 0;
}
