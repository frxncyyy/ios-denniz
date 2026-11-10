/* SPDX-License-Identifier: MIT */
/*
 * t_fake.c -- literal expectations for the fake Apple runtime (WP02).  Every value checked here is either written out in
 * the package's TASK.md, taken from the standards' fixed structures, or a hand-computed layout number; none is derived
 * by calling the code under test in a circular way.
 */
#include "fake_apple.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;
static long g_initial_live;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

#define CHECK_ST(expr, want) do { \
	OSStatus st__ = (expr); \
	if (st__ != (want)) { \
		fprintf(stderr, "FAIL %s:%d: %s = %d, want %d\n", __FILE__, __LINE__, #expr, \
		        (int)st__, (int)(want)); \
		return 1; \
	} \
} while (0)

/* ------------------------------------------------------------------------------------------------------------------ */
/* CFString / CFNumber / CFBoolean                                                                                   */
/* ------------------------------------------------------------------------------------------------------------------ */

static int test_strings_and_numbers(void)
{
	long base = n2fake_live_objects();
	CFStringRef a, b, c;
	char buf[32];
	int32_t v32 = -123456, o32 = 0;
	int64_t v64 = 5000000000LL, o64 = 0;
	double vd = 3.25, od = 0.0;
	CFNumberRef n32, n64, nd, nd2, n4;
	int32_t four = 4;

	CFRelease(NULL);                        /* the documented NULL-safe call */

	a = n2fake_cfstr("n2fake string");
	b = n2fake_cfstr("n2fake string");
	CHECK(a == b);                          /* interning: same text, same pointer */
	CHECK(CFGetTypeID(a) == CFStringGetTypeID());
	CHECK(CFEqual(a, b));

	c = CFStringCreateWithCString(NULL, "n2fake string", kCFStringEncodingUTF8);
	CHECK(c != NULL);
	CHECK(c != a);                          /* a created string is a new object */
	CHECK(CFEqual(c, a));                   /* ... but equal by content */
	CHECK(CFStringGetCString(c, buf, (CFIndex)sizeof(buf), kCFStringEncodingUTF8));
	CHECK(strcmp(buf, "n2fake string") == 0);
	CHECK(!CFStringGetCString(c, buf, 4, kCFStringEncodingUTF8));       /* too small */
	CHECK(CFStringGetCString(c, buf, 14, kCFStringEncodingUTF8));       /* 13 bytes + NUL */
	CFRelease(c);
	CHECK(n2fake_live_objects() == base);

	n32 = CFNumberCreate(NULL, kCFNumberSInt32Type, &v32);
	CHECK(n32 != NULL);
	CHECK(CFGetTypeID(n32) == CFNumberGetTypeID());
	CHECK(CFNumberGetValue(n32, kCFNumberSInt32Type, &o32));
	CHECK(o32 == -123456);
	o64 = 0;
	CHECK(CFNumberGetValue(n32, kCFNumberSInt64Type, &o64));
	CHECK(o64 == -123456);

	n64 = CFNumberCreate(NULL, kCFNumberSInt64Type, &v64);
	CHECK(CFNumberGetValue(n64, kCFNumberSInt64Type, &o64));
	CHECK(o64 == v64);
	o32 = 0;
	CHECK(!CFNumberGetValue(n64, kCFNumberSInt32Type, &o32));           /* 5e9 does not fit */

	nd = CFNumberCreate(NULL, kCFNumberFloat64Type, &vd);
	CHECK(CFNumberGetValue(nd, kCFNumberFloat64Type, &od));
	CHECK(od == 3.25);
	o64 = 0;
	CHECK(CFNumberGetValue(nd, kCFNumberSInt64Type, &o64));
	CHECK(o64 == 3);

	nd2 = CFNumberCreate(NULL, kCFNumberFloat64Type, &vd);
	n4 = CFNumberCreate(NULL, kCFNumberSInt32Type, &four);
	CHECK(CFEqual(nd, nd2));
	CHECK(!CFEqual(nd, n4));
	{
		double vnan = NAN;
		CFNumberRef nn = CFNumberCreate(NULL, kCFNumberFloat64Type, &vnan);
		CHECK(nn != NULL);
		CHECK(!CFNumberGetValue(nn, kCFNumberSInt32Type, &o32));
		CHECK(!CFNumberGetValue(nn, kCFNumberSInt64Type, &o64));
		CFRelease(nn);
	}
	CFRelease(n32);
	CFRelease(n64);
	CFRelease(nd);
	CFRelease(nd2);
	CFRelease(n4);
	CHECK(n2fake_live_objects() == base);

	CHECK(CFGetTypeID(kCFBooleanTrue) == CFBooleanGetTypeID());
	CHECK(CFGetTypeID(kCFBooleanFalse) == CFBooleanGetTypeID());
	CHECK(CFBooleanGetValue(kCFBooleanTrue) == 1);
	CHECK(CFBooleanGetValue(kCFBooleanFalse) == 0);
	CHECK(CFEqual(kCFBooleanTrue, kCFBooleanTrue));
	CHECK(!CFEqual(kCFBooleanTrue, kCFBooleanFalse));
	CHECK(n2fake_cfstr("kCMSampleAttachmentKey_NotSync") == kCMSampleAttachmentKey_NotSync);
	CHECK(n2fake_cfstr("kCVPixelBufferWidthKey") == kCVPixelBufferWidthKey);
	CFRetain(kCFBooleanTrue);               /* static singletons are immortal */
	CFRelease(kCFBooleanTrue);
	CHECK(n2fake_live_objects() == base);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CFDictionary                                                                                                       */
/* ------------------------------------------------------------------------------------------------------------------ */

static int test_dictionary(void)
{
	long base = n2fake_live_objects();
	CFMutableDictionaryRef d, d2;
	CFStringRef key, key2, ka, kb;
	int32_t one = 1, two = 2, seven = 7;
	CFNumberRef v1, v2, va, vb, nk1, nk2;

	d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	                              &kCFTypeDictionaryValueCallBacks);
	key = CFStringCreateWithCString(NULL, "owned-key", kCFStringEncodingUTF8);
	v1 = CFNumberCreate(NULL, kCFNumberSInt32Type, &one);
	v2 = CFNumberCreate(NULL, kCFNumberSInt32Type, &two);
	CHECK(d && key && v1 && v2);
	CHECK(n2fake_live_objects() == base + 4);

	CFDictionarySetValue(d, key, v1);       /* retains key and value */
	CHECK(n2fake_live_objects() == base + 4);
	CHECK(CFDictionaryGetValue(d, key) == v1);
	CHECK(n2fake_dictionary_count(d) == 1);
	CHECK(n2fake_dictionary_key_at(d, 0) == key);
	CHECK(CFDictionaryGetValue(d, n2fake_cfstr("absent")) == NULL);

	CFRelease(key);                         /* the dictionary still holds the key */
	CFRelease(v1);
	CHECK(n2fake_live_objects() == base + 4);

	key2 = CFStringCreateWithCString(NULL, "owned-key", kCFStringEncodingUTF8);
	CHECK(n2fake_live_objects() == base + 5);
	CHECK(CFDictionaryGetValue(d, key2) == v1);     /* equal by content, same stored value */
	CFDictionarySetValue(d, key2, v2);      /* overwrite: releases the old value; the original key stays */
	CHECK(n2fake_live_objects() == base + 4);        /* d, the original key, key2 (local), v2 */
	CHECK(CFDictionaryGetValue(d, key2) == v2);
	CHECK(n2fake_dictionary_count(d) == 1);          /* overwrite does not append */
	CHECK(n2fake_dictionary_key_at(d, 0) == key);    /* CFDictionarySetValue keeps the existing key */
	CHECK(n2fake_dictionary_key_at(d, 1) == NULL);
	CFRelease(key2);
	CFRelease(v2);
	CHECK(n2fake_live_objects() == base + 3);        /* d, the original key, v2 */
	CFRelease(d);
	CHECK(n2fake_live_objects() == base);

	/* A second dictionary: two keys in insertion order, and number keys compared by value. */
	d2 = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	                               &kCFTypeDictionaryValueCallBacks);
	ka = n2fake_cfstr("alpha");
	kb = n2fake_cfstr("beta");
	va = CFNumberCreate(NULL, kCFNumberSInt32Type, &one);
	vb = CFNumberCreate(NULL, kCFNumberSInt32Type, &two);
	CFDictionarySetValue(d2, ka, va);
	CFDictionarySetValue(d2, kb, vb);
	CHECK(n2fake_dictionary_count(d2) == 2);
	CHECK(n2fake_dictionary_key_at(d2, 0) == ka);
	CHECK(n2fake_dictionary_key_at(d2, 1) == kb);
	CHECK(CFDictionaryGetValue(d2, ka) == va);
	CHECK(CFDictionaryGetValue(d2, kb) == vb);
	CFDictionarySetValue(d2, ka, vb);       /* replace first value */
	CHECK(n2fake_dictionary_count(d2) == 2);
	CHECK(CFDictionaryGetValue(d2, ka) == vb);

	nk1 = CFNumberCreate(NULL, kCFNumberSInt32Type, &seven);
	nk2 = CFNumberCreate(NULL, kCFNumberSInt32Type, &seven);
	CFDictionarySetValue(d2, nk1, kCFBooleanTrue);
	CHECK(CFDictionaryGetValue(d2, nk2) == kCFBooleanTrue);   /* CFEqual on numbers, not pointers */
	CFRelease(nk1);
	CFRelease(nk2);
	CFRelease(va);
	CFRelease(vb);
	CFRelease(d2);
	CHECK(n2fake_live_objects() == base);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CFArray                                                                                                            */
/* ------------------------------------------------------------------------------------------------------------------ */

static int test_array(void)
{
	long base = n2fake_live_objects();
	CFMutableArrayRef arr;
	CFStringRef s;
	int32_t v = 42;
	CFNumberRef n;

	arr = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	s = CFStringCreateWithCString(NULL, "one", kCFStringEncodingUTF8);
	n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v);
	CHECK(arr && s && n);
	CFArrayAppendValue(arr, s);
	CFArrayAppendValue(arr, n);
	CHECK(CFArrayGetCount(arr) == 2);
	CHECK(CFArrayGetValueAtIndex(arr, 0) == s);
	CHECK(CFArrayGetValueAtIndex(arr, 1) == n);
	CFRelease(s);
	CFRelease(n);
	CHECK(n2fake_live_objects() == base + 3);        /* the array still holds both values */
	CFRelease(arr);
	CHECK(n2fake_live_objects() == base);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CMBlockBuffer                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */

static int test_block_buffer(void)
{
	uint8_t storage[16];
	uint8_t out[16];
	uint8_t src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };
	uint8_t got[5];
	CMBlockBufferRef bb = NULL;
	CMBlockBufferRef owned = NULL;
	int i;

	memset(storage, 0xAA, sizeof(storage));
	CHECK_ST(CMBlockBufferCreateWithMemoryBlock(NULL, storage, sizeof(storage), NULL, NULL, 4, 8, 0, &bb), 0);
	CHECK(bb != NULL);
	CHECK(CMBlockBufferGetDataLength(bb) == 8);
	CHECK_ST(CMBlockBufferReplaceDataBytes(src, bb, 0, 8), 0);
	for (i = 0; i < 8; i++)
		CHECK(storage[4 + i] == src[i]);
	memset(out, 0, sizeof(out));
	CHECK_ST(CMBlockBufferCopyDataBytes(bb, 0, 8, out), 0);
	CHECK(memcmp(out, src, 8) == 0);
	CHECK_ST(CMBlockBufferCopyDataBytes(bb, 1, 8, out), -12704);      /* 1 + 8 > 8 */
	CHECK_ST(CMBlockBufferCopyDataBytes(bb, 0, 9, out), -12704);
	CHECK_ST(CMBlockBufferCopyDataBytes(bb, 9, 0, out), -12704);      /* offset past the data */
	CHECK_ST(CMBlockBufferReplaceDataBytes(src, bb, 1, 8), -12704);
	CHECK_ST(CMBlockBufferReplaceDataBytes(src, bb, 0, 9), -12704);
	CFRelease(bb);

	bb = NULL;
	CHECK_ST(CMBlockBufferCreateWithMemoryBlock(NULL, storage, sizeof(storage), NULL, NULL, 10, 10, 0, &bb), -12704);
	CHECK(bb == NULL);

	/* Owned block: memoryBlock NULL + AssureMemoryNow; the fake allocates and zeroes it. */
	CHECK_ST(CMBlockBufferCreateWithMemoryBlock(NULL, NULL, 5, kCFAllocatorDefault, NULL, 0, 5,
	                                            N2_kCMBlockBufferAssureMemoryNowFlag, &owned), 0);
	CHECK(CMBlockBufferGetDataLength(owned) == 5);
	memset(got, 0, sizeof(got));
	CHECK_ST(CMBlockBufferCopyDataBytes(owned, 0, 5, got), 0);
	CHECK(got[0] == 0 && got[4] == 0);
	CHECK_ST(CMBlockBufferReplaceDataBytes(hello, owned, 0, 5), 0);
	CHECK_ST(CMBlockBufferCopyDataBytes(owned, 0, 5, got), 0);
	CHECK(memcmp(got, hello, 5) == 0);
	CHECK_ST(CMBlockBufferCopyDataBytes(owned, 0, 6, got), -12704);
	CFRelease(owned);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CMSampleBuffer and the H.264 format description                                                                    */
/* ------------------------------------------------------------------------------------------------------------------ */

static const uint8_t g_sps[] = { 0x67, 0x42, 0xC0, 0x1E, 0xD9, 0x00 };
static const uint8_t g_pps[] = { 0x68, 0xCE, 0x3C, 0x80 };

static CMFormatDescriptionRef make_test_format(void)
{
	uint8_t sps[sizeof(g_sps)];
	uint8_t pps[sizeof(g_pps)];
	const uint8_t *sets[2];
	size_t sizes[2];
	CMFormatDescriptionRef fmt = NULL;

	memcpy(sps, g_sps, sizeof(sps));
	memcpy(pps, g_pps, sizeof(pps));
	sets[0] = sps;
	sets[1] = pps;
	sizes[0] = sizeof(sps);
	sizes[1] = sizeof(pps);
	if (CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, 2, sets, sizes, 4, &fmt) != 0)
		return NULL;
	return fmt;
}

static int test_h264_format_description(void)
{
	long base = n2fake_live_objects();
	uint8_t sps[sizeof(g_sps)];
	uint8_t pps[sizeof(g_pps)];
	const uint8_t *sets[2];
	size_t sizes[2];
	CMFormatDescriptionRef fmt = NULL;
	CMFormatDescriptionRef fmt2 = NULL;
	const uint8_t *got = NULL;
	size_t gotSize = 0, count = 0;
	int nal = 0;

	memcpy(sps, g_sps, sizeof(sps));
	memcpy(pps, g_pps, sizeof(pps));
	sets[0] = sps;
	sets[1] = pps;
	sizes[0] = sizeof(sps);
	sizes[1] = sizeof(pps);

	CHECK_ST(CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, 2, sets, sizes, 4, &fmt), 0);
	CHECK(fmt != NULL);
	CHECK_ST(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, &got, &gotSize, &count, &nal), 0);
	CHECK(count == 2);
	CHECK(nal == 4);
	CHECK(gotSize == sizeof(g_sps));
	CHECK(memcmp(got, g_sps, sizeof(g_sps)) == 0);
	CHECK_ST(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 1, &got, &gotSize, NULL, NULL), 0);
	CHECK(gotSize == sizeof(g_pps));
	CHECK(memcmp(got, g_pps, sizeof(g_pps)) == 0);

	/* The format description must own copies: mutate the caller's arrays and read it back. */
	sps[1] = 0xFF;
	pps[1] = 0xFF;
	CHECK_ST(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, &got, &gotSize, NULL, NULL), 0);
	CHECK(memcmp(got, g_sps, sizeof(g_sps)) == 0);
	CHECK_ST(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 1, &got, &gotSize, NULL, NULL), 0);
	CHECK(memcmp(got, g_pps, sizeof(g_pps)) == 0);

	CHECK_ST(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 2, &got, &gotSize, NULL, NULL), -12712);
	CFRelease(fmt);
	CHECK(n2fake_live_objects() == base);

	/* Rejections required by the package: count < 2, NAL header length not 1/2/4, first NAL not an SPS. */
	memcpy(sps, g_sps, sizeof(sps));
	memcpy(pps, g_pps, sizeof(pps));
	sets[0] = sps;
	sets[1] = pps;
	sizes[0] = sizeof(sps);
	sizes[1] = sizeof(pps);
	CHECK_ST(CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, 1, sets, sizes, 4, &fmt2), -12712);
	CHECK(fmt2 == NULL);
	CHECK_ST(CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, 2, sets, sizes, 3, &fmt2), -12712);
	CHECK(fmt2 == NULL);
	CHECK_ST(CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, 2, sets, sizes, 0, &fmt2), -12712);
	CHECK(fmt2 == NULL);
	sets[0] = pps;
	sets[1] = sps;
	CHECK_ST(CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, 2, sets, sizes, 4, &fmt2), -12712);
	CHECK(fmt2 == NULL);
	CHECK(n2fake_live_objects() == base);
	return 0;
}

static int test_sample_buffer(void)
{
	long base = n2fake_live_objects();
	uint8_t payload[5] = { 'W', 'O', 'R', 'L', 'D' };
	CMBlockBufferRef bb = NULL;
	CMFormatDescriptionRef fmt;
	CMSampleBufferRef sb = NULL;
	CMSampleTimingInfo timing;
	CMSampleTimingInfo gotTiming;
	size_t sampleSize = 0, gotSize = 0;
	size_t sizes3[3] = { 5, 5, 5 };
	CFArrayRef attachments;
	CFMutableDictionaryRef attach0;

	fmt = make_test_format();
	CHECK(fmt != NULL);
	CHECK_ST(CMBlockBufferCreateWithMemoryBlock(NULL, NULL, 5, kCFAllocatorDefault, NULL, 0, 5,
	                                            N2_kCMBlockBufferAssureMemoryNowFlag, &bb), 0);
	CHECK_ST(CMBlockBufferReplaceDataBytes(payload, bb, 0, 5), 0);

	timing.duration.value = 1000;
	timing.duration.timescale = 30000;
	timing.duration.flags = N2_CMTIME_VALID;
	timing.duration.epoch = 0;
	timing.presentationTimeStamp.value = 90000;
	timing.presentationTimeStamp.timescale = 30000;
	timing.presentationTimeStamp.flags = N2_CMTIME_VALID;
	timing.presentationTimeStamp.epoch = 0;
	timing.decodeTimeStamp.value = -1;
	timing.decodeTimeStamp.timescale = 0;
	timing.decodeTimeStamp.flags = 0;
	timing.decodeTimeStamp.epoch = 0;
	sampleSize = 5;

	CHECK_ST(CMSampleBufferCreateReady(NULL, bb, fmt, 1, 1, &timing, 1, &sampleSize, &sb), 0);
	CHECK(sb != NULL);
	CHECK(CMSampleBufferGetDataBuffer(sb) == bb);
	CHECK(CMSampleBufferGetFormatDescription(sb) == fmt);
	CHECK(n2fake_sample_count(sb) == 1);
	CHECK(n2fake_sample_size_at(sb, 0) == 5);
	CHECK(n2fake_sample_size_at(sb, 1) == 0);
	memset(&gotTiming, 0, sizeof(gotTiming));
	CHECK(n2fake_sample_timing(sb, &gotTiming, &gotSize));
	CHECK(gotSize == 5);
	CHECK(gotTiming.duration.value == 1000);
	CHECK(gotTiming.duration.timescale == 30000);
	CHECK(gotTiming.presentationTimeStamp.value == 90000);
	CHECK(gotTiming.presentationTimeStamp.timescale == 30000);
	CHECK(gotTiming.decodeTimeStamp.value == -1);

	CHECK(CMSampleBufferGetSampleAttachmentsArray(sb, 0) == NULL);
	attachments = CMSampleBufferGetSampleAttachmentsArray(sb, 1);
	CHECK(attachments != NULL);
	CHECK(CFArrayGetCount(attachments) == 1);
	attach0 = (CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
	CHECK(attach0 != NULL);
	CFDictionarySetValue(attach0, kCMSampleAttachmentKey_NotSync, kCFBooleanTrue);
	CHECK(CFDictionaryGetValue(attach0, kCMSampleAttachmentKey_NotSync) == kCFBooleanTrue);
	CHECK(CFDictionaryGetValue(attach0, kCMSampleAttachmentKey_DependsOnOthers) == NULL);
	CHECK(CMSampleBufferGetSampleAttachmentsArray(sb, 0) == attachments);

	CFRelease(sb);
	CFRelease(bb);
	CFRelease(fmt);
	CHECK(n2fake_live_objects() == base);

	/* One attachment dictionary per sample, even with only one timing entry. */
	CHECK_ST(CMBlockBufferCreateWithMemoryBlock(NULL, NULL, 5, kCFAllocatorDefault, NULL, 0, 5,
	                                            N2_kCMBlockBufferAssureMemoryNowFlag, &bb), 0);
	CHECK_ST(CMSampleBufferCreateReady(NULL, bb, NULL, 3, 1, &timing, 3, sizes3, &sb), 0);
	attachments = CMSampleBufferGetSampleAttachmentsArray(sb, 1);
	CHECK(CFArrayGetCount(attachments) == 3);
	CHECK(CFArrayGetValueAtIndex(attachments, 1) != NULL);
	CHECK(n2fake_sample_count(sb) == 3);
	CHECK(n2fake_sample_size_at(sb, 2) == 5);
	CHECK(n2fake_sample_size_at(sb, 3) == 0);
	CFRelease(sb);
	CFRelease(bb);
	CHECK(n2fake_live_objects() == base);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CVPixelBuffer                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */

static int test_pixel_buffer(void)
{
	long base = n2fake_live_objects();
	CVPixelBufferRef pb = NULL;
	size_t y;

	CHECK_ST(CVPixelBufferCreate(NULL, 178, 146, N2_PIXFMT_420V, NULL, &pb), 0);
	CHECK(CVPixelBufferGetWidth(pb) == 178);
	CHECK(CVPixelBufferGetHeight(pb) == 146);
	CHECK(CVPixelBufferGetPixelFormatType(pb) == N2_PIXFMT_420V);
	CHECK(CVPixelBufferGetPlaneCount(pb) == 2);
	CHECK(CVPixelBufferGetBytesPerRowOfPlane(pb, 0) == 192);   /* 178 rounded up to a multiple of 64 */
	CHECK(CVPixelBufferGetBytesPerRowOfPlane(pb, 1) == 192);
	CHECK(n2fake_pixel_buffer_plane_height(pb, 0) == 146);
	CHECK(n2fake_pixel_buffer_plane_height(pb, 1) == 73);
	CHECK(n2fake_pixel_buffer_plane_bytes_used(pb, 0) == 178);
	CHECK(n2fake_pixel_buffer_plane_bytes_used(pb, 1) == 178);
	CHECK(n2fake_pixel_buffer_plane_data_size(pb, 0) == 192 * 146);
	CHECK(n2fake_pixel_buffer_plane_data_size(pb, 1) == 192 * 73);

	CHECK(CVPixelBufferGetBaseAddressOfPlane(pb, 0) == NULL);   /* missing lock is caught */
	CHECK_ST(CVPixelBufferUnlockBaseAddress(pb, 0), -6661);
	CHECK_ST(CVPixelBufferLockBaseAddress(pb, 0), 0);
	for (y = 0; y < 146; y++) {
		uint8_t *row = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0) +
		               y * CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
		CHECK(row != NULL);
		memset(row, (int)(y & 0xff), 178);              /* writes the 178 used bytes */
	}
	CHECK_ST(CVPixelBufferLockBaseAddress(pb, 0), 0); /* nested locks are counted */
	CHECK_ST(CVPixelBufferUnlockBaseAddress(pb, 0), 0);
	CHECK(CVPixelBufferGetBaseAddressOfPlane(pb, 0) != NULL);
	CHECK_ST(CVPixelBufferUnlockBaseAddress(pb, 0), 0);
	CHECK(CVPixelBufferGetBaseAddressOfPlane(pb, 0) == NULL);
	CHECK_ST(CVPixelBufferUnlockBaseAddress(pb, 0), -6661);
	CFRelease(pb);
	CHECK(n2fake_live_objects() == base);

	/* 420f is layout-identical to 420v. */
	CHECK_ST(CVPixelBufferCreate(NULL, 178, 146, N2_PIXFMT_420F, NULL, &pb), 0);
	CHECK(CVPixelBufferGetPlaneCount(pb) == 2);
	CHECK(CVPixelBufferGetBytesPerRowOfPlane(pb, 0) == 192);
	CHECK(n2fake_pixel_buffer_plane_height(pb, 1) == 73);
	CHECK(n2fake_pixel_buffer_plane_bytes_used(pb, 1) == 178);
	CFRelease(pb);
	CHECK(n2fake_live_objects() == base);

	/* i420: three planes, chroma stride rounded from ceil(width/2) = 89 to 128. */
	CHECK_ST(CVPixelBufferCreate(NULL, 178, 146, N2_PIXFMT_I420, NULL, &pb), 0);
	CHECK(CVPixelBufferGetPlaneCount(pb) == 3);
	CHECK(CVPixelBufferGetBytesPerRowOfPlane(pb, 0) == 192);
	CHECK(CVPixelBufferGetBytesPerRowOfPlane(pb, 1) == 128);
	CHECK(CVPixelBufferGetBytesPerRowOfPlane(pb, 2) == 128);
	CHECK(n2fake_pixel_buffer_plane_height(pb, 1) == 73);
	CHECK(n2fake_pixel_buffer_plane_height(pb, 2) == 73);
	CHECK(n2fake_pixel_buffer_plane_bytes_used(pb, 1) == 89);
	CHECK(n2fake_pixel_buffer_plane_bytes_used(pb, 2) == 89);
	CFRelease(pb);
	CHECK(n2fake_live_objects() == base);

	CHECK_ST(CVPixelBufferCreate(NULL, 0, 146, N2_PIXFMT_420V, NULL, &pb), -6661);
	CHECK_ST(CVPixelBufferCreate(NULL, 178, 146, 0x61626364u, NULL, &pb), -6661);
	CHECK(n2fake_live_objects() == base);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Toy VideoToolbox encoder                                                                                           */
/* ------------------------------------------------------------------------------------------------------------------ */

#define TOY_MAGIC 0x746f7921u

struct toy_state {
	uint32_t magic;
	uint32_t pad;
	VTVideoEncoderSession session;
	VTVideoEncoderFrame lastFrame;
	CFTypeRef property;
	int frames;
	int finalized;
};
_Static_assert(sizeof(struct toy_state) <= 64, "toy derived storage is 64 bytes");

static int g_toy_start_calls;
static int g_toy_encode_calls;
static int g_toy_complete_calls;
static int g_toy_finalize_calls;
static VTVideoEncoderSession g_toy_session;
static VTVideoEncoderFrame g_toy_last_frame;
static VTVideoEncoderRef g_toy_encoder;
static CMVideoDimensions g_toy_dims;

static OSStatus toy_start(VTVideoEncoderRef enc, VTVideoEncoderSession session, CMVideoDimensions dims)
{
	struct toy_state *ts = (struct toy_state *)CMBaseObjectGetDerivedStorage(
		VTVideoEncoderGetCMBaseObject(enc));

	if (ts->magic != TOY_MAGIC)
		return N2_kVTVideoEncoderMalfunctionErr;
	ts->session = session;
	g_toy_session = session;
	g_toy_dims = dims;
	g_toy_start_calls++;
	return 0;
}

static OSStatus toy_encode(VTVideoEncoderRef enc, VTVideoEncoderFrame frame, CVImageBufferRef image,
                           const CMTime *pts, const CMTime *duration, CFDictionaryRef frameProperties,
                           uint32_t *infoFlagsOut)
{
	struct toy_state *ts = (struct toy_state *)CMBaseObjectGetDerivedStorage(
		VTVideoEncoderGetCMBaseObject(enc));
	static const uint8_t payload[5] = { 'T', 'O', 'Y', '!', '!' };
	CMBlockBufferRef bb = NULL;
	CMSampleBufferRef sb = NULL;
	CMSampleTimingInfo timing;
	size_t sampleSize = 5;
	OSStatus st;

	(void)image;
	(void)frameProperties;
	if (ts->magic != TOY_MAGIC || !ts->session)
		return N2_kVTVideoEncoderMalfunctionErr;
	if (!pts || !duration)
		return N2_kVTParameterErr;
	if (infoFlagsOut)
		*infoFlagsOut = 0;
	st = CMBlockBufferCreateWithMemoryBlock(NULL, NULL, 5, kCFAllocatorDefault, NULL, 0, 5,
	                                        N2_kCMBlockBufferAssureMemoryNowFlag, &bb);
	if (st == 0)
		st = CMBlockBufferReplaceDataBytes(payload, bb, 0, 5);
	timing.duration = *duration;
	timing.presentationTimeStamp = *pts;
	timing.decodeTimeStamp = *pts;
	if (st == 0)
		st = CMSampleBufferCreateReady(NULL, bb, NULL, 1, 1, &timing, 1, &sampleSize, &sb);
	if (st == 0)
		st = VTEncoderSessionEmitEncodedFrame(ts->session, frame, 0, 0, sb);
	if (sb)
		CFRelease(sb);
	if (bb)
		CFRelease(bb);
	ts->lastFrame = frame;
	ts->frames++;
	g_toy_last_frame = frame;
	g_toy_encode_calls++;
	return st;
}

static OSStatus toy_complete(VTVideoEncoderRef enc, const CMTime *completeUntilPTS)
{
	struct toy_state *ts = (struct toy_state *)CMBaseObjectGetDerivedStorage(
		VTVideoEncoderGetCMBaseObject(enc));

	(void)completeUntilPTS;
	(void)ts;
	g_toy_complete_calls++;
	return 0;
}

static OSStatus toy_copy_supported(VTVideoEncoderRef enc, CFDictionaryRef *dictOut)
{
	CFMutableDictionaryRef d;

	(void)enc;
	if (!dictOut)
		return N2_kVTParameterErr;
	d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	                              &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(d, n2fake_cfstr("ToyProperty"), kCFBooleanTrue);
	*dictOut = d;
	return 0;
}

static OSStatus toy_copy_property(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef a, void *valueOut)
{
	struct toy_state *ts = (struct toy_state *)CMBaseObjectGetDerivedStorage(o);

	(void)a;
	if (!valueOut)
		return N2_kVTParameterErr;
	if (CFEqual(key, n2fake_cfstr("ToyProperty"))) {
		if (!ts->property)
			return N2_kCMBaseObjectError_ValueNotAvailable;
		*(CFTypeRef *)valueOut = CFRetain(ts->property);
		return 0;
	}
	return N2_kCMBaseObjectError_ValueNotAvailable;
}

static OSStatus toy_set_property(CMBaseObjectRef o, CFStringRef key, CFTypeRef value)
{
	struct toy_state *ts = (struct toy_state *)CMBaseObjectGetDerivedStorage(o);
	CFTypeRef old;

	if (CFEqual(key, n2fake_cfstr("ToyProperty"))) {
		old = ts->property;
		ts->property = value ? CFRetain(value) : NULL;
		if (old)
			CFRelease(old);
		return 0;
	}
	return N2_kVTPropertyNotSupportedErr;
}

static void toy_finalize(CMBaseObjectRef o)
{
	struct toy_state *ts = (struct toy_state *)CMBaseObjectGetDerivedStorage(o);

	if (ts->property) {
		CFRelease(ts->property);
		ts->property = NULL;
	}
	ts->finalized++;
	g_toy_finalize_calls++;
}

static const n2_CMBaseClass toy_base_class = {
	.version = 1,
	.derivedStorageSize = 64,
	.equal = NULL,
	.invalidate = NULL,
	.finalize = toy_finalize,
	.copyDebugDescription = NULL,
	.copyProperty = toy_copy_property,
	.setProperty = toy_set_property,
	.reserved8 = NULL,
	.reserved9 = NULL,
	.reserved10 = NULL
};

static const n2_VTVideoEncoderClass toy_encoder_class = {
	.version = 1,
	.startSession = toy_start,
	.encodeFrame = toy_encode,
	.completeFrames = toy_complete,
	.copySupportedPropertyDictionary = toy_copy_supported,
	.setProperties = NULL,
	.copySerializableProperties = NULL,
	.prepareToEncodeFrames = NULL,
	.startTileSession = NULL,
	.prepareToEncodeTiles = NULL,
	.encodeTile = NULL,
	.completeTiles = NULL
};

static const n2_VTVideoEncoderVTable toy_vtable = { NULL, &toy_base_class, &toy_encoder_class };

static OSStatus toy_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	struct toy_state *ts;
	OSStatus st;

	if (codecType != 0x61766331u)               /* 'avc1' */
		return N2_kVTParameterErr;
	st = CMDerivedObjectCreate(allocator, &toy_vtable, VTVideoEncoderGetClassID(),
	                           (CMBaseObjectRef *)instanceOut);
	if (st != 0)
		return st;
	ts = (struct toy_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)*instanceOut);
	ts->magic = TOY_MAGIC;
	g_toy_encoder = *instanceOut;
	return 0;
}

static int test_vt_registry_and_session(void)
{
	long base = n2fake_live_objects();
	CFMutableDictionaryRef info, attrs;
	CFStringRef propKey, propValue, missing;
	CFTypeRef copied = NULL;
	uint32_t codec = 0;
	CFDictionaryRef gotInfo = NULL;
	n2_VTVideoEncoderCreateInstance gotFn = NULL;
	CVPixelBufferRef pb = NULL;
	struct n2fake_session *s;
	CMTime pts, pts2, duration;
	OSStatus status = -1;
	uint32_t flags = 0;
	CMSampleBufferRef sb = NULL, sb2 = NULL;
	void *token0 = NULL, *token1 = NULL, *tokenX = NULL, *savedToken0 = NULL;
	CMSampleTimingInfo timing;
	size_t sampleSize = 0;
	CMBlockBufferRef bb;
	uint8_t bytes[5];
	int32_t pixfmt = (int32_t)N2_PIXFMT_420V;
	CMBaseObjectRef obj = NULL;

	n2fake_vt_reset();
	CHECK(!n2fake_vt_registered(0, NULL, NULL, NULL));
	CHECK_ST(VTRegisterVideoEncoder(0, toy_create), N2_kVTParameterErr);
	CHECK_ST(VTRegisterVideoEncoder('avc1', NULL), N2_kVTParameterErr);
	CHECK_ST(CMDerivedObjectCreate(NULL, &toy_vtable, 0x12345678ul, &obj), -12780);
	CHECK(obj == NULL);

	CHECK_ST(VTRegisterVideoEncoder('avc1', toy_create), 0);
	info = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	                                &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(info, n2fake_cfstr("CMClassImplementationID"), n2fake_cfstr("test"));
	CHECK_ST(VTRegisterVideoEncoderWithInfo('hvc1', info, toy_create), 0);
	CHECK(n2fake_vt_registered(0, &codec, &gotInfo, &gotFn));
	CHECK(codec == 0x61766331u);
	CHECK(gotInfo == NULL);
	CHECK(gotFn == toy_create);
	CHECK(n2fake_vt_registered(1, &codec, &gotInfo, &gotFn));
	CHECK(codec == 0x68766331u);                 /* 'hvc1' */
	CHECK(gotInfo == info);                      /* retained, not copied */
	CHECK(gotFn == toy_create);
	CHECK(!n2fake_vt_registered(2, NULL, NULL, NULL));
	CFRelease(info);                             /* the registry keeps it alive */

	CHECK_ST(CVPixelBufferCreate(NULL, 320, 240, N2_PIXFMT_420V, NULL, &pb), 0);
	s = n2fake_session_create(toy_create, 0x61766331u, 320, 240);
	CHECK(s != NULL);
	CHECK(g_toy_start_calls == 1);
	CHECK(g_toy_dims.width == 320 && g_toy_dims.height == 240);
	CHECK(g_toy_encoder != NULL);
	CHECK(n2fake_session_encoder(s) == g_toy_encoder);
	CHECK(n2fake_session_vtable(s) == &toy_vtable);
	CHECK(VTVideoEncoderGetCMBaseObject(g_toy_encoder) == (CMBaseObjectRef)g_toy_encoder);
	CHECK(CMBaseObjectGetDerivedStorage(VTVideoEncoderGetCMBaseObject(g_toy_encoder)) != NULL);
	{
		CFDictionaryRef supported = NULL;
		CHECK_ST(n2fake_session_vtable(s)->encoderClass->copySupportedPropertyDictionary(
			n2fake_session_encoder(s), &supported), 0);
		CHECK(supported != NULL);
		CHECK(CFDictionaryGetValue(supported, n2fake_cfstr("ToyProperty")) == kCFBooleanTrue);
		CFRelease(supported);
	}
	CHECK(g_toy_finalize_calls == 0);

	propKey = n2fake_cfstr("ToyProperty");
	propValue = n2fake_cfstr("hello");
	missing = n2fake_cfstr("NoSuchProperty");
	CHECK_ST(n2fake_session_set_property(s, propKey, propValue), 0);
	CHECK_ST(n2fake_session_copy_property(s, propKey, &copied), 0);
	CHECK(copied != NULL && CFEqual(copied, propValue));
	CFRelease(copied);
	copied = NULL;
	CHECK_ST(n2fake_session_set_property(s, missing, propValue), N2_kVTPropertyNotSupportedErr);
	CHECK_ST(n2fake_session_copy_property(s, missing, &copied), N2_kCMBaseObjectError_ValueNotAvailable);
	CHECK(copied == NULL);

	CHECK(n2fake_session_pixel_attributes(s) == NULL);
	attrs = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	                                  &kCFTypeDictionaryValueCallBacks);
	{
		CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &pixfmt);
		CFDictionarySetValue(attrs, kCVPixelBufferPixelFormatTypeKey, n);
		CFRelease(n);
	}
	CHECK_ST(VTEncoderSessionSetPixelBufferAttributes(g_toy_session, attrs), 0);
	CHECK(n2fake_session_pixel_attributes(s) == attrs);
	CFRelease(attrs);
	CHECK(n2fake_session_pixel_attributes(s) != NULL);

	pts.value = 0;
	pts.timescale = 30;
	pts.flags = N2_CMTIME_VALID;
	pts.epoch = 0;
	duration.value = 1;
	duration.timescale = 30;
	duration.flags = N2_CMTIME_VALID;
	duration.epoch = 0;
	CHECK_ST(n2fake_session_encode(s, pb, pts, duration, NULL), 0);
	CHECK(g_toy_encode_calls == 1);
	CHECK(n2fake_session_emitted(s, 0, &status, &flags, &sb, &token0) == 1);
	CHECK(status == 0);
	CHECK(flags == 0);
	CHECK(sb != NULL);
	CHECK(token0 != NULL);
	CHECK(token0 == (void *)g_toy_last_frame);
	savedToken0 = token0;
	CHECK(n2fake_session_emitted(s, 1, NULL, NULL, NULL, NULL) == 0);
	CHECK(n2fake_session_protocol_errors(s) == 0);

	CHECK(n2fake_sample_count(sb) == 1);
	CHECK(n2fake_sample_timing(sb, &timing, &sampleSize));
	CHECK(sampleSize == 5);
	CHECK(timing.presentationTimeStamp.value == 0);
	CHECK(timing.presentationTimeStamp.timescale == 30);
	CHECK(timing.duration.value == 1);
	CHECK(timing.duration.timescale == 30);
	bb = CMSampleBufferGetDataBuffer(sb);
	CHECK(bb != NULL);
	CHECK(CMBlockBufferGetDataLength(bb) == 5);
	memset(bytes, 0, sizeof(bytes));
	CHECK_ST(CMBlockBufferCopyDataBytes(bb, 0, 5, bytes), 0);
	CHECK(memcmp(bytes, "TOY!!", 5) == 0);

	pts2.value = 1;
	pts2.timescale = 30;
	pts2.flags = N2_CMTIME_VALID;
	pts2.epoch = 0;
	CHECK_ST(n2fake_session_encode(s, pb, pts2, duration, NULL), 0);
	CHECK(n2fake_session_emitted(s, 1, &status, &flags, &sb2, &token1) == 1);
	CHECK(token1 != NULL && token1 != token0);
	CHECK(sb2 != NULL);

	/* Protocol errors: a second emit for token 0, then an unknown token.  Both are recorded and sticky-counted. */
	CHECK_ST(VTEncoderSessionEmitEncodedFrame(g_toy_session, (VTVideoEncoderFrame)token0, 0, 0, sb), 0);
	CHECK(n2fake_session_protocol_errors(s) == 1);
	CHECK_ST(VTEncoderSessionEmitEncodedFrame(g_toy_session, (VTVideoEncoderFrame)(uintptr_t)0x1234u, 0, 0,
	                                          NULL), 0);
	CHECK(n2fake_session_protocol_errors(s) == 2);
	CHECK(n2fake_session_emitted(s, 2, &status, &flags, &sb, &token0) == 1);
	CHECK(token0 == savedToken0);
	CHECK(n2fake_session_emitted(s, 3, &status, &flags, &sb, &tokenX) == 1);
	CHECK(sb == NULL);
	CHECK(tokenX == (void *)(uintptr_t)0x1234u);

	CHECK_ST(n2fake_session_complete(s), 0);
	CHECK(g_toy_complete_calls == 1);
	CHECK(g_toy_finalize_calls == 0);
	n2fake_session_destroy(s);
	CHECK(g_toy_finalize_calls == 1);
	n2fake_vt_reset();
	CFRelease(pb);
	CHECK(n2fake_live_objects() == base);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Concurrent use of the shared machinery (intern table, atomic refcounts, live counter)                              */
/* ------------------------------------------------------------------------------------------------------------------ */

#define N2FAKE_THREADS 4
#define N2FAKE_THREAD_ITERS 100

static void *thread_worker(void *arg)
{
	long i;

	(void)arg;
	for (i = 0; i < N2FAKE_THREAD_ITERS; i++) {
		CFStringRef s1 = n2fake_cfstr("shared-interned");
		CFStringRef s2 = n2fake_cfstr("shared-interned");
		CFStringRef owned;
		CFMutableDictionaryRef d;
		CVPixelBufferRef pb = NULL;
		int32_t v = (int32_t)i;
		CFNumberRef n;

		if (s1 != s2)
			return (void *)(uintptr_t)1;
		d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
		                              &kCFTypeDictionaryValueCallBacks);
		n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v);
		CFDictionarySetValue(d, kCVPixelBufferWidthKey, n);
		if (CFDictionaryGetValue(d, kCVPixelBufferWidthKey) != n) {
			CFRelease(n);
			CFRelease(d);
			return (void *)(uintptr_t)1;
		}
		CFRelease(n);
		CFRelease(d);
		if (CVPixelBufferCreate(NULL, 64, 64, N2_PIXFMT_420V, NULL, &pb) != 0)
			return (void *)(uintptr_t)1;
		(void)CVPixelBufferLockBaseAddress(pb, 0);
		if (CVPixelBufferGetBaseAddressOfPlane(pb, 0) == NULL) {
			CFRelease(pb);
			return (void *)(uintptr_t)1;
		}
		(void)CVPixelBufferUnlockBaseAddress(pb, 0);
		CFRelease(pb);
		owned = CFStringCreateWithCString(NULL, "thread-string", kCFStringEncodingUTF8);
		if (!owned)
			return (void *)(uintptr_t)1;
		CFRelease(owned);
	}
	return NULL;
}

static int test_concurrent_refcount(void)
{
	long base = n2fake_live_objects();
	pthread_t threads[N2FAKE_THREADS];
	void *result = NULL;
	int i;

	for (i = 0; i < N2FAKE_THREADS; i++)
		CHECK(pthread_create(&threads[i], NULL, thread_worker, NULL) == 0);
	for (i = 0; i < N2FAKE_THREADS; i++) {
		CHECK(pthread_join(threads[i], &result) == 0);
		CHECK(result == NULL);
	}
	CHECK(n2fake_live_objects() == base);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Link coverage: the address of every declared function and constant must resolve                                   */
/* ------------------------------------------------------------------------------------------------------------------ */

static void (*volatile g_fn_sink)(void);
static const void *volatile g_obj_sink;

#define TOUCH_FN(name) do { g_fn_sink = (void (*)(void))(name); } while (0)
#define TOUCH_OBJ(name) do { g_obj_sink = (const void *)&(name); } while (0)

static int test_symbols_link(void)
{
	TOUCH_FN(CFRetain);
	TOUCH_FN(CFRelease);
	TOUCH_FN(CFGetTypeID);
	TOUCH_FN(CFEqual);
	TOUCH_FN(CFNumberGetTypeID);
	TOUCH_FN(CFBooleanGetTypeID);
	TOUCH_FN(CFStringGetTypeID);
	TOUCH_FN(CFDictionaryGetTypeID);
	TOUCH_FN(CFDictionaryCreateMutable);
	TOUCH_FN(CFDictionarySetValue);
	TOUCH_FN(CFDictionaryGetValue);
	TOUCH_FN(CFArrayCreateMutable);
	TOUCH_FN(CFArrayAppendValue);
	TOUCH_FN(CFArrayGetCount);
	TOUCH_FN(CFArrayGetValueAtIndex);
	TOUCH_FN(CFNumberCreate);
	TOUCH_FN(CFNumberGetValue);
	TOUCH_FN(CFBooleanGetValue);
	TOUCH_FN(CFStringCreateWithCString);
	TOUCH_FN(CFStringGetCString);

	TOUCH_FN(CMBlockBufferCreateWithMemoryBlock);
	TOUCH_FN(CMBlockBufferReplaceDataBytes);
	TOUCH_FN(CMBlockBufferGetDataLength);
	TOUCH_FN(CMBlockBufferCopyDataBytes);
	TOUCH_FN(CMSampleBufferCreateReady);
	TOUCH_FN(CMSampleBufferGetSampleAttachmentsArray);
	TOUCH_FN(CMSampleBufferGetDataBuffer);
	TOUCH_FN(CMSampleBufferGetFormatDescription);
	TOUCH_FN(CMVideoFormatDescriptionCreateFromH264ParameterSets);
	TOUCH_FN(CMVideoFormatDescriptionGetH264ParameterSetAtIndex);

	TOUCH_FN(CVPixelBufferLockBaseAddress);
	TOUCH_FN(CVPixelBufferUnlockBaseAddress);
	TOUCH_FN(CVPixelBufferGetBaseAddressOfPlane);
	TOUCH_FN(CVPixelBufferGetBytesPerRowOfPlane);
	TOUCH_FN(CVPixelBufferGetWidth);
	TOUCH_FN(CVPixelBufferGetHeight);
	TOUCH_FN(CVPixelBufferGetPlaneCount);
	TOUCH_FN(CVPixelBufferGetPixelFormatType);
	TOUCH_FN(CVPixelBufferCreate);

	TOUCH_FN(VTVideoEncoderGetClassID);
	TOUCH_FN(VTVideoEncoderGetCMBaseObject);
	TOUCH_FN(VTRegisterVideoEncoder);
	TOUCH_FN(VTRegisterVideoEncoderWithInfo);
	TOUCH_FN(VTEncoderSessionSetPixelBufferAttributes);
	TOUCH_FN(VTEncoderSessionEmitEncodedFrame);
	TOUCH_FN(CMDerivedObjectCreate);
	TOUCH_FN(CMBaseObjectGetDerivedStorage);

	TOUCH_OBJ(kCFAllocatorDefault);
	TOUCH_OBJ(kCFAllocatorNull);
	TOUCH_OBJ(kCFBooleanTrue);
	TOUCH_OBJ(kCFBooleanFalse);
	TOUCH_OBJ(kCFTypeDictionaryKeyCallBacks);
	TOUCH_OBJ(kCFTypeDictionaryValueCallBacks);
	TOUCH_OBJ(kCFTypeArrayCallBacks);
	TOUCH_OBJ(kCMSampleAttachmentKey_NotSync);
	TOUCH_OBJ(kCMSampleAttachmentKey_DependsOnOthers);
	TOUCH_OBJ(kCVPixelBufferPixelFormatTypeKey);
	TOUCH_OBJ(kCVPixelBufferWidthKey);
	TOUCH_OBJ(kCVPixelBufferHeightKey);
	TOUCH_OBJ(kCVPixelBufferIOSurfacePropertiesKey);
	TOUCH_OBJ(kVTVideoEncoder_IsHardwareAccelerated);
	TOUCH_OBJ(kVTVideoEncoder_Rating);

	CHECK(g_fn_sink != NULL);
	CHECK(g_obj_sink != NULL);
	CHECK(kCFAllocatorDefault != kCFAllocatorNull);
	CHECK(kCFBooleanTrue != kCFBooleanFalse);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */

struct test_case {
	const char *name;
	int (*fn)(void);
};

int main(void)
{
	static const struct test_case tests[] = {
		{ "strings_numbers", test_strings_and_numbers },
		{ "dictionary", test_dictionary },
		{ "array", test_array },
		{ "block_buffer", test_block_buffer },
		{ "sample_buffer", test_sample_buffer },
		{ "h264_format_description", test_h264_format_description },
		{ "pixel_buffer", test_pixel_buffer },
		{ "vt_registry_session", test_vt_registry_and_session },
		{ "concurrent_refcount", test_concurrent_refcount },
		{ "symbols_link", test_symbols_link }
	};
	size_t i, passed = 0;
	size_t total = sizeof(tests) / sizeof(tests[0]);

	g_initial_live = n2fake_live_objects();
	for (i = 0; i < total; i++) {
		int r = tests[i].fn();
		if (r == 0) {
			passed++;
			printf("ok %s\n", tests[i].name);
		} else {
			g_failed++;
			printf("not ok %s\n", tests[i].name);
		}
	}
	if (n2fake_live_objects() != g_initial_live) {
		fprintf(stderr, "FAIL: live objects %ld at end, %ld at start\n",
		        n2fake_live_objects(), g_initial_live);
		g_failed++;
	}
	if (g_failed || passed != total) {
		printf("N2TEST WP02 FAIL %zu/%zu\n", passed, total);
		return 1;
	}
	printf("N2TEST WP02 PASS %zu/%zu\n", passed, total);
	return 0;
}
