/* SPDX-License-Identifier: MIT */
/*
 * n2jpeg_vt.c -- the VideoToolbox video-encoder object around the n2jpeg baseline JPEG encoder (R1).
 *
 * One CMBaseObject subclass with a VTVideoEncoder class record, exactly as include/n2vt_abi.h freezes it, on the pattern of
 * vt/n2avc_vt.c (WP05).  Every submitted frame is one complete JPEG file in one CMSampleBuffer, built from the n2jpeg
 * encoder's buffer; the format description is a 'jpeg' CMVideoFormatDescription of the session's dimensions.
 *
 * JPEG is intra-only: every sample is a sync sample and no frame reordering happens.  The VT "Quality" property (0..1) is
 * mapped to the encoder's quality 1..100 (q * 100, clamped); all other standard properties are accepted and stored, and
 * have no effect, so that clients which set them do not stop at a property error.
 *
 * The input must be NV12 ('420v' or '420f') of the session's dimensions; that is what the pixel-buffer attributes ask for
 * and what the encoder consumes without conversion.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "n2_apple.h"
#include "n2jpeg_vt.h"
#include "n2vt_abi.h"
#include "jpeg/n2jpeg.h"

/* WP01 delivers vt/n2vt_log.h. Until it exists (e.g. while this package is built alone) the log is a no-op. */
#if defined(__has_include)
#if __has_include("n2vt_log.h")
#include "n2vt_log.h"
#define N2JPEG_VT_HAVE_LOG 1
#endif
#endif
#ifndef N2JPEG_VT_HAVE_LOG
static inline void n2vt_say(const char *fmt, ...)
{
	(void)fmt;
}
#endif

#define N2_CODEC_JPEG       0x6a706567u
#define N2_MEDIA_JPEG       0x6a706567u        /* kCMVideoCodecType_JPEG */
#define N2_JPEG_MAX_DIM     8192         /* stills: the X camera is 4032x3024; 8192 bounds the buffer at 32 MiB */
#define N2_JPEG_GROW_SLACK  4096
#define N2_JPEG_MIN_CAP     (256u * 1024u)

/* n2_apple.h does not declare CFArrayGetTypeID; ask CoreFoundation for an array's type id without importing it by making
 * a throw-away empty array.  Only the DataRateLimits property pays for it. */
static int
is_cf_array(CFTypeRef value)
{
	CFTypeID value_type = CFGetTypeID(value);
	CFMutableArrayRef probe = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
	CFTypeID array_type;
	int is_array;

	if (probe == NULL)
		return 0;
	array_type = CFGetTypeID(probe);
	is_array = (value_type == array_type);
	CFRelease(probe);
	return is_array;
}

#define N2_PROP_NUM   1u
#define N2_PROP_BOOL  2u
#define N2_PROP_STR   4u
#define N2_PROP_DICT  8u
#define N2_PROP_ARR   16u
#define N2_PROP_RO    0x80u

enum n2jpeg_prop_id {
	P_QUALITY,
	P_EXPECTED_FRAME_RATE,
	P_REAL_TIME,
	P_MAX_KEYFRAME_INTERVAL,
	P_MAX_KEYFRAME_INTERVAL_DURATION,
	P_AVERAGE_BITRATE,
	P_DATA_RATE_LIMITS,
	P_PROFILE_LEVEL,
	P_ALLOW_FRAME_REORDERING,
	P_ALLOW_TEMPORAL_COMPRESSION,
	P_ALLOW_OPEN_GOP,
	P_PRIORITY,
	P_MAXIMIZE_POWER_EFFICIENCY,
	P_COLOR_PRIMARIES,
	P_TRANSFER_FUNCTION,
	P_YCBCR_MATRIX,
	P_PIXEL_ASPECT_RATIO,
	P_CLEAN_APERTURE,
	P_FIELD_COUNT,
	P_MAX_FRAME_DELAY_COUNT,
	P_SOURCE_FRAME_COUNT,
	P_EXPECTED_DURATION,
	P_BASE_LAYER_FRAME_RATE,
	P_ENCODER_ID,
	P_USING_HW,
	P_PENDING_FRAMES,
	P_COUNT
};

struct n2jpeg_prop {
	const char *name;
	const char *type_name;
	uint32_t    accept;
};

static const struct n2jpeg_prop s_props[P_COUNT] = {
	[P_QUALITY]                       = { "Quality", "Number", N2_PROP_NUM },
	[P_EXPECTED_FRAME_RATE]           = { "ExpectedFrameRate", "Number", N2_PROP_NUM },
	[P_REAL_TIME]                     = { "RealTime", "Boolean", N2_PROP_BOOL },
	[P_MAX_KEYFRAME_INTERVAL]         = { "MaxKeyFrameInterval", "Number", N2_PROP_NUM },
	[P_MAX_KEYFRAME_INTERVAL_DURATION]= { "MaxKeyFrameIntervalDuration", "Number", N2_PROP_NUM },
	[P_AVERAGE_BITRATE]               = { "AverageBitRate", "Number", N2_PROP_NUM },
	[P_DATA_RATE_LIMITS]              = { "DataRateLimits", "Enumeration", N2_PROP_ARR },
	[P_PROFILE_LEVEL]                 = { "ProfileLevel", "Enumeration", N2_PROP_STR },
	[P_ALLOW_FRAME_REORDERING]        = { "AllowFrameReordering", "Boolean", N2_PROP_BOOL },
	[P_ALLOW_TEMPORAL_COMPRESSION]    = { "AllowTemporalCompression", "Boolean", N2_PROP_BOOL },
	[P_ALLOW_OPEN_GOP]                = { "AllowOpenGOP", "Boolean", N2_PROP_BOOL },
	[P_PRIORITY]                      = { "Priority", "Number", N2_PROP_NUM },
	[P_MAXIMIZE_POWER_EFFICIENCY]     = { "MaximizePowerEfficiency", "Boolean", N2_PROP_BOOL },
	[P_COLOR_PRIMARIES]               = { "ColorPrimaries", "Enumeration", N2_PROP_STR },
	[P_TRANSFER_FUNCTION]             = { "TransferFunction", "Enumeration", N2_PROP_STR },
	[P_YCBCR_MATRIX]                  = { "YCbCrMatrix", "Enumeration", N2_PROP_STR },
	[P_PIXEL_ASPECT_RATIO]            = { "PixelAspectRatio", "Enumeration", N2_PROP_DICT },
	[P_CLEAN_APERTURE]                = { "CleanAperture", "Enumeration", N2_PROP_DICT },
	[P_FIELD_COUNT]                   = { "FieldCount", "Number", N2_PROP_NUM },
	[P_MAX_FRAME_DELAY_COUNT]         = { "MaxFrameDelayCount", "Number", N2_PROP_NUM },
	[P_SOURCE_FRAME_COUNT]            = { "SourceFrameCount", "Number", N2_PROP_NUM },
	[P_EXPECTED_DURATION]             = { "ExpectedDuration", "Number", N2_PROP_NUM },
	[P_BASE_LAYER_FRAME_RATE]         = { "BaseLayerFrameRate", "Number", N2_PROP_NUM },
	[P_ENCODER_ID]                    = { "EncoderID", "Enumeration", N2_PROP_STR },
	[P_USING_HW]                      = { "UsingHardwareAcceleratedVideoEncoder", "Boolean", N2_PROP_BOOL | N2_PROP_RO },
	[P_PENDING_FRAMES]                = { "NumberOfPendingFrames", "Number", N2_PROP_NUM | N2_PROP_RO },
};

struct n2jpeg_vt {
	pthread_mutex_t         lock;
	int                     mutex_ok;
	VTVideoEncoderSession   session;        /* not retained: VideoToolbox owns it */
	int32_t                 width, height;
	CMFormatDescriptionRef  fmt;
	n2jpeg_enc             *enc;            /* opened lazily at the first encodeFrame */
	CFMutableDictionaryRef  props;

	double                  quality;        /* 0..1, VideoToolbox's scale */
	int                     quality_set;
	uint8_t                *out;            /* JPEG file buffer, kept between frames */
	size_t                  out_cap;
	int                     no_fmt_logged;
};

/* ------------------------------------------------------------------ helpers */

static struct n2jpeg_vt *
storage(CMBaseObjectRef object)
{
	return (struct n2jpeg_vt *)CMBaseObjectGetDerivedStorage(object);
}

static int
key_is(CFStringRef key, const char *name)
{
	CFStringRef probe = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
	Boolean equal;

	if (probe == NULL)
		return 0;
	equal = CFEqual(key, probe);
	CFRelease(probe);
	return equal != 0;
}

static int
prop_find(CFStringRef key)
{
	int i;

	for (i = 0; i < (int)P_COUNT; i++)
		if (key_is(key, s_props[i].name))
			return i;
	return -1;
}

static CFStringRef
prop_name_ref(int id)
{
	return CFStringCreateWithCString(kCFAllocatorDefault, s_props[id].name, kCFStringEncodingUTF8);
}

static int
accepts_value(uint32_t accept, CFTypeRef value)
{
	CFTypeID t = CFGetTypeID(value);

	if ((accept & N2_PROP_NUM) && t == CFNumberGetTypeID())
		return 1;
	if ((accept & N2_PROP_BOOL) && t == CFBooleanGetTypeID())
		return 1;
	if ((accept & N2_PROP_STR) && t == CFStringGetTypeID())
		return 1;
	if ((accept & N2_PROP_DICT) && t == CFDictionaryGetTypeID())
		return 1;
	if ((accept & N2_PROP_ARR) && is_cf_array(value))
		return 1;
	return 0;
}

static int
number_double(CFNumberRef number, double *out)
{
	double  vd;
	int64_t v64;
	int32_t v32;

	if (CFNumberGetValue(number, kCFNumberFloat64Type, &vd)) {
		*out = vd;
		return 0;
	}
	if (CFNumberGetValue(number, kCFNumberSInt64Type, &v64)) {
		*out = (double)v64;
		return 0;
	}
	if (CFNumberGetValue(number, kCFNumberSInt32Type, &v32)) {
		*out = (double)v32;
		return 0;
	}
	return -1;
}

static int
jpeg_quality_from_vt(const struct n2jpeg_vt *e)
{
	double value = e->quality_set ? e->quality : 0.9;
	int quality = (int)(value * 100.0 + 0.5);

	if (quality < 1)
		quality = 1;
	if (quality > 100)
		quality = 100;
	return quality;
}

static OSStatus
emit_failure(struct n2jpeg_vt *e, VTVideoEncoderFrame frame, OSStatus status)
{
	if (e->session != NULL)
		(void)VTEncoderSessionEmitEncodedFrame(e->session, frame, status, 0, NULL);
	return status;
}

static int
ensure_encoder(struct n2jpeg_vt *e)
{
	n2jpeg_config config;
	int status;

	if (e->enc != NULL)
		return 0;
	memset(&config, 0, sizeof(config));
	config.size = (uint32_t)sizeof(config);
	config.width = e->width;
	config.height = e->height;
	config.quality = jpeg_quality_from_vt(e);
	config.restart_interval = 0;
	config.flags = 0;
	status = n2jpeg_open(&config, &e->enc);
	if (status != N2JPEG_OK || e->enc == NULL) {
		e->enc = NULL;
		n2vt_say("n2jpeg: open failed (%d)", status);
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	if (e->out == NULL) {
		size_t cap = (size_t)e->width * (size_t)e->height / 2u + 65536u;

		if (cap < N2_JPEG_MIN_CAP)
			cap = N2_JPEG_MIN_CAP;
		e->out = (uint8_t *)malloc(cap);
		if (e->out == NULL) {
			n2jpeg_close(e->enc);
			e->enc = NULL;
			return N2_kVTAllocationFailedErr;
		}
		e->out_cap = cap;
	}
	return 0;
}

static int
ensure_format(struct n2jpeg_vt *e)
{
	if (e->fmt != NULL)
		return 0;
	if (CMVideoFormatDescriptionCreate == NULL) {
		/* The fake runtime may not have it and a real CoreMedia always does; without it the sample buffer goes out
		 * with a NULL format description (valid at the CMSampleBuffer level, less useful to the client). */
		if (!e->no_fmt_logged) {
			n2vt_say("n2jpeg: CMVideoFormatDescriptionCreate is absent");
			e->no_fmt_logged = 1;
		}
		return 0;
	}
	if (CMVideoFormatDescriptionCreate(kCFAllocatorDefault, N2_MEDIA_JPEG, e->width, e->height, NULL, &e->fmt) != 0 ||
	    e->fmt == NULL) {
		e->fmt = NULL;
		n2vt_say("n2jpeg: CMVideoFormatDescriptionCreate('jpeg', %dx%d) failed", (int)e->width, (int)e->height);
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	return 0;
}

/* ------------------------------------------------------------------ properties */

static OSStatus
base_copy_property(CMBaseObjectRef object, CFStringRef key, CFAllocatorRef allocator, void *valueOut)
{
	struct n2jpeg_vt *e = storage(object);
	CFTypeRef value;

	(void)allocator;
	if (key == NULL || valueOut == NULL)
		return N2_kVTParameterErr;
	if (CFGetTypeID((CFTypeRef)key) != CFStringGetTypeID())
		return N2_kVTParameterErr;

	if (key_is(key, s_props[P_USING_HW].name)) {
		*(CFTypeRef *)valueOut = CFRetain(kCFBooleanFalse);
		return 0;
	}
	if (key_is(key, s_props[P_PENDING_FRAMES].name)) {
		int32_t zero = 0;
		CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &zero);

		if (number == NULL)
			return N2_kVTAllocationFailedErr;
		*(CFTypeRef *)valueOut = number;
		return 0;
	}

	pthread_mutex_lock(&e->lock);
	value = CFDictionaryGetValue(e->props, key);
	if (value != NULL)
		value = CFRetain(value);
	pthread_mutex_unlock(&e->lock);

	if (value == NULL)
		return N2_kCMBaseObjectError_ValueNotAvailable;
	*(CFTypeRef *)valueOut = value;
	return 0;
}

static OSStatus
base_set_property(CMBaseObjectRef object, CFStringRef key, CFTypeRef value)
{
	struct n2jpeg_vt *e = storage(object);
	char keybuf[128];
	int id;
	OSStatus status = 0;

	if (key == NULL || value == NULL)
		return N2_kVTParameterErr;
	if (CFGetTypeID((CFTypeRef)key) != CFStringGetTypeID())
		return N2_kVTParameterErr;

	id = prop_find(key);
	if (id < 0) {
		if (CFStringGetCString(key, keybuf, (CFIndex)sizeof(keybuf), kCFStringEncodingUTF8))
			n2vt_say("n2jpeg: unsupported property %s", keybuf);
		else
			n2vt_say("n2jpeg: unsupported property (unprintable)");
		return N2_kVTPropertyNotSupportedErr;
	}
	if ((s_props[id].accept & N2_PROP_RO) != 0)
		return N2_kVTPropertyNotSupportedErr;
	if (!accepts_value(s_props[id].accept, value))
		return N2_kVTParameterErr;

	pthread_mutex_lock(&e->lock);
	if (id == P_QUALITY) {
		double q;

		if (number_double((CFNumberRef)value, &q) != 0 || !(q >= 0.0 && q <= 1.0)) {
			status = N2_kVTParameterErr;
		} else {
			int quality = (int)(q * 100.0 + 0.5);

			if (quality < 1)
				quality = 1;
			if (quality > 100)
				quality = 100;
			e->quality = q;
			e->quality_set = 1;
			if (e->enc != NULL && n2jpeg_set_quality(e->enc, quality) != N2JPEG_OK) {
				n2vt_say("n2jpeg: set_quality failed");
				status = N2_kVTVideoEncoderMalfunctionErr;
			}
		}
	}
	if (status == 0)
		CFDictionarySetValue(e->props, key, value);
	pthread_mutex_unlock(&e->lock);
	return status;
}

static CFStringRef
base_copy_debug_description(CMBaseObjectRef object)
{
	(void)object;
	return CFStringCreateWithCString(kCFAllocatorDefault, "n2jpeg baseline JPEG encoder", kCFStringEncodingUTF8);
}

static void
base_finalize(CMBaseObjectRef object)
{
	struct n2jpeg_vt *e = storage(object);

	if (e->enc != NULL) {
		n2jpeg_close(e->enc);
		e->enc = NULL;
	}
	free(e->out);
	e->out = NULL;
	e->out_cap = 0;
	if (e->fmt != NULL) {
		CFRelease(e->fmt);
		e->fmt = NULL;
	}
	if (e->props != NULL) {
		CFRelease(e->props);
		e->props = NULL;
	}
	if (e->mutex_ok) {
		pthread_mutex_destroy(&e->lock);
		e->mutex_ok = 0;
	}
	e->session = NULL;
}

/* ------------------------------------------------------------------ encoder class */

static OSStatus
enc_start_session(VTVideoEncoderRef encoder, VTVideoEncoderSession session, CMVideoDimensions dims)
{
	struct n2jpeg_vt *e = storage((CMBaseObjectRef)encoder);
	CFMutableArrayRef formats = NULL;
	CFMutableDictionaryRef attributes = NULL;
	CFMutableDictionaryRef surface = NULL;
	CFNumberRef width_number = NULL, height_number = NULL, format_number = NULL;
	OSStatus status = N2_kVTAllocationFailedErr;

	if (session == NULL)
		return N2_kVTParameterErr;
	if (dims.width < 2 || dims.height < 2 || dims.width > N2_JPEG_MAX_DIM || dims.height > N2_JPEG_MAX_DIM ||
	    (dims.width & 1) != 0 || (dims.height & 1) != 0) {
		n2vt_say("n2jpeg: rejected %dx%d (NV12 needs even, 2..%d)", (int)dims.width, (int)dims.height, N2_JPEG_MAX_DIM);
		return N2_kVTParameterErr;
	}

	formats = CFArrayCreateMutable(kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
	attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                       &kCFTypeDictionaryValueCallBacks);
	surface = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                    &kCFTypeDictionaryValueCallBacks);
	if (formats == NULL || attributes == NULL || surface == NULL)
		goto done;

	format_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ (int32_t)N2_PIXFMT_420V });
	if (format_number == NULL)
		goto done;
	CFArrayAppendValue(formats, format_number);
	CFRelease(format_number);
	format_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ (int32_t)N2_PIXFMT_420F });
	if (format_number == NULL)
		goto done;
	CFArrayAppendValue(formats, format_number);
	CFRelease(format_number);
	format_number = NULL;

	width_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ dims.width });
	height_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ dims.height });
	if (width_number == NULL || height_number == NULL)
		goto done;

	CFDictionarySetValue(attributes, kCVPixelBufferPixelFormatTypeKey, formats);
	CFDictionarySetValue(attributes, kCVPixelBufferWidthKey, width_number);
	CFDictionarySetValue(attributes, kCVPixelBufferHeightKey, height_number);
	CFDictionarySetValue(attributes, kCVPixelBufferIOSurfacePropertiesKey, surface);

	pthread_mutex_lock(&e->lock);
	if (e->enc != NULL) {
		n2jpeg_close(e->enc);
		e->enc = NULL;
	}
	if (e->fmt != NULL) {
		CFRelease(e->fmt);
		e->fmt = NULL;
	}
	e->session = session;
	e->width = dims.width;
	e->height = dims.height;
	pthread_mutex_unlock(&e->lock);

	status = VTEncoderSessionSetPixelBufferAttributes(session, attributes);

done:
	if (format_number != NULL)
		CFRelease(format_number);
	if (width_number != NULL)
		CFRelease(width_number);
	if (height_number != NULL)
		CFRelease(height_number);
	if (formats != NULL)
		CFRelease(formats);
	if (attributes != NULL)
		CFRelease(attributes);
	if (surface != NULL)
		CFRelease(surface);
	return status;
}

static OSStatus
enc_encode_frame(VTVideoEncoderRef encoder, VTVideoEncoderFrame frame, CVImageBufferRef image,
                 const CMTime *pts, const CMTime *duration, CFDictionaryRef frameProperties,
                 uint32_t *infoFlagsOut)
{
	struct n2jpeg_vt *e = storage((CMBaseObjectRef)encoder);
	CVPixelBufferRef pb = (CVPixelBufferRef)image;
	n2jpeg_picture picture;
	CMTime zero = { 0, 1, N2_CMTIME_VALID, 0 };
	CMSampleTimingInfo timing;
	CMBlockBufferRef block = NULL;
	CMSampleBufferRef sample = NULL;
	CFArrayRef attachments;
	uint32_t pixel_format;
	size_t width, height, plane_count, jpeg_size = 0;
	int32_t lock_status;
	OSStatus status = 0;
	int r;

	(void)frameProperties;
	if (infoFlagsOut != NULL)
		*infoFlagsOut = 0;

	pthread_mutex_lock(&e->lock);

	if (e->session == NULL) {
		pthread_mutex_unlock(&e->lock);
		return N2_kVTInvalidSessionErr;
	}
	if (pb == NULL) {
		status = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	pixel_format = CVPixelBufferGetPixelFormatType(pb);
	if (pixel_format != N2_PIXFMT_420V && pixel_format != N2_PIXFMT_420F) {
		status = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	width = CVPixelBufferGetWidth(pb);
	height = CVPixelBufferGetHeight(pb);
	plane_count = CVPixelBufferGetPlaneCount(pb);
	if (width != (size_t)e->width || height != (size_t)e->height || plane_count < 2) {
		status = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	lock_status = CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
	if (lock_status != 0) {
		status = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	memset(&picture, 0, sizeof(picture));
	picture.size = (uint32_t)sizeof(picture);
	picture.y = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
	picture.uv = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
	{
		size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
		size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);

		if (picture.y == NULL || picture.uv == NULL || y_stride < width || uv_stride < width ||
		    y_stride > 0xffffffffu || uv_stride > 0xffffffffu) {
			(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
			status = emit_failure(e, frame, N2_kVTParameterErr);
			pthread_mutex_unlock(&e->lock);
			return status;
		}
		picture.y_stride = (uint32_t)y_stride;
		picture.uv_stride = (uint32_t)uv_stride;
	}

	status = ensure_encoder(e);
	if (status != 0) {
		(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
		status = emit_failure(e, frame, status);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	r = n2jpeg_encode(e->enc, &picture, e->out, e->out_cap, &jpeg_size);
	if (r == N2JPEG_E_TOO_SMALL) {
		uint8_t *grown;

		n2vt_say("n2jpeg: %dx%d needs %zu bytes, growing from %zu", (int)e->width, (int)e->height, jpeg_size, e->out_cap);
		grown = (uint8_t *)realloc(e->out, jpeg_size + N2_JPEG_GROW_SLACK);
		if (grown == NULL) {
			(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
			status = emit_failure(e, frame, N2_kVTAllocationFailedErr);
			pthread_mutex_unlock(&e->lock);
			return status;
		}
		e->out = grown;
		e->out_cap = jpeg_size + N2_JPEG_GROW_SLACK;
		r = n2jpeg_encode(e->enc, &picture, e->out, e->out_cap, &jpeg_size);
	}
	(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
	if (r != N2JPEG_OK) {
		n2vt_say("n2jpeg: encode failed (%d)", r);
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	if (jpeg_size == 0 || jpeg_size > 0x7fffffffu) {
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	status = ensure_format(e);
	if (status != 0) {
		status = emit_failure(e, frame, status);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, NULL, jpeg_size, kCFAllocatorDefault, NULL,
	                                       0, jpeg_size, N2_kCMBlockBufferAssureMemoryNowFlag, &block) != 0 ||
	    block == NULL) {
		n2vt_say("n2jpeg: CMBlockBufferCreateWithMemoryBlock failed");
		if (block != NULL)
			CFRelease(block);
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	if (CMBlockBufferReplaceDataBytes(e->out, block, 0, jpeg_size) != 0) {
		n2vt_say("n2jpeg: CMBlockBufferReplaceDataBytes failed");
		CFRelease(block);
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	timing.duration = (duration != NULL) ? *duration : zero;
	timing.presentationTimeStamp = (pts != NULL) ? *pts : zero;
	timing.decodeTimeStamp = timing.presentationTimeStamp;

	if (CMSampleBufferCreateReady(kCFAllocatorDefault, block, e->fmt, 1, 1, &timing, 1, &jpeg_size,
	                              &sample) != 0 || sample == NULL) {
		n2vt_say("n2jpeg: CMSampleBufferCreateReady failed");
		if (sample != NULL)
			CFRelease(sample);
		CFRelease(block);
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	attachments = CMSampleBufferGetSampleAttachmentsArray(sample, 1);
	if (attachments != NULL && CFArrayGetCount(attachments) > 0) {
		CFMutableDictionaryRef dict = (CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);

		if (dict != NULL) {
			/* Every JPEG sample is a sync sample and depends on no other picture. */
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_NotSync, kCFBooleanFalse);
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_DependsOnOthers, kCFBooleanFalse);
		}
	}

	status = VTEncoderSessionEmitEncodedFrame(e->session, frame, 0, 0, sample);
	CFRelease(sample);
	CFRelease(block);
	pthread_mutex_unlock(&e->lock);
	return status;
}

static OSStatus
enc_complete_frames(VTVideoEncoderRef encoder, const CMTime *completeUntilPTS)
{
	struct n2jpeg_vt *e = storage((CMBaseObjectRef)encoder);

	(void)completeUntilPTS;
	pthread_mutex_lock(&e->lock);
	/* Encoding is synchronous: nothing is ever pending. */
	pthread_mutex_unlock(&e->lock);
	return 0;
}

static OSStatus
enc_copy_supported_properties(VTVideoEncoderRef encoder, CFDictionaryRef *dictOut)
{
	CFMutableDictionaryRef dict;
	int i;

	(void)encoder;
	if (dictOut == NULL)
		return N2_kVTParameterErr;
	*dictOut = NULL;

	dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                 &kCFTypeDictionaryValueCallBacks);
	if (dict == NULL)
		return N2_kVTAllocationFailedErr;

	for (i = 0; i < (int)P_COUNT; i++) {
		CFMutableDictionaryRef entry = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
		                                                         &kCFTypeDictionaryKeyCallBacks,
		                                                         &kCFTypeDictionaryValueCallBacks);
		CFStringRef type_string;
		CFStringRef key;

		if (entry == NULL)
			continue;
		type_string = CFStringCreateWithCString(kCFAllocatorDefault, s_props[i].type_name, kCFStringEncodingUTF8);
		if (type_string != NULL) {
			CFDictionarySetValue(entry, N2STR("PropertyType"), type_string);
			CFRelease(type_string);
		}
		CFDictionarySetValue(entry, N2STR("ReadWriteStatus"),
		                     (s_props[i].accept & N2_PROP_RO) ? N2STR("ReadOnly") : N2STR("ReadWrite"));
		key = prop_name_ref(i);
		if (key != NULL) {
			CFDictionarySetValue(dict, key, entry);
			CFRelease(key);
		}
		CFRelease(entry);
	}

	*dictOut = dict;
	return 0;
}

/* ------------------------------------------------------------------ vtable */

static const n2_CMBaseClass s_base_class = {
	.version = 1,
	.derivedStorageSize = sizeof(struct n2jpeg_vt),
	.equal = NULL,
	.invalidate = NULL,
	.finalize = base_finalize,
	.copyDebugDescription = base_copy_debug_description,
	.copyProperty = base_copy_property,
	.setProperty = base_set_property,
	.reserved8 = NULL,
	.reserved9 = NULL,
	.reserved10 = NULL,
};

static const n2_VTVideoEncoderClass s_encoder_class = {
	.version = 1,
	.startSession = enc_start_session,
	.encodeFrame = enc_encode_frame,
	.completeFrames = enc_complete_frames,
	.copySupportedPropertyDictionary = enc_copy_supported_properties,
	.setProperties = NULL,
	.copySerializableProperties = NULL,
	.prepareToEncodeFrames = NULL,
	.startTileSession = NULL,
	.prepareToEncodeTiles = NULL,
	.encodeTile = NULL,
	.completeTiles = NULL,
};

static const n2_VTVideoEncoderVTable s_vtable = {
	.reserved = NULL,
	.baseClass = &s_base_class,
	.encoderClass = &s_encoder_class,
};

/* ------------------------------------------------------------------ entry point */

OSStatus
n2jpeg_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	CMBaseObjectRef object = NULL;
	struct n2jpeg_vt *e;
	OSStatus status;

	if (instanceOut == NULL)
		return N2_kVTParameterErr;
	if (codecType != N2_CODEC_JPEG)
		return N2_kVTParameterErr;

	status = CMDerivedObjectCreate(allocator, &s_vtable, VTVideoEncoderGetClassID(), &object);
	if (status != 0) {
		if (object != NULL)
			CFRelease(object);
		return status;
	}
	if (object == NULL)
		return N2_kVTAllocationFailedErr;

	e = storage(object);
	if (pthread_mutex_init(&e->lock, NULL) != 0) {
		CFRelease(object);
		return N2_kVTAllocationFailedErr;
	}
	e->mutex_ok = 1;
	e->props = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                     &kCFTypeDictionaryValueCallBacks);
	if (e->props == NULL) {
		CFRelease(object);
		return N2_kVTAllocationFailedErr;
	}

	e->quality = 0.9;
	e->quality_set = 0;
	*instanceOut = (VTVideoEncoderRef)object;
	return 0;
}
