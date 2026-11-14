/* SPDX-License-Identifier: MIT */
/*
 * n2jpeg_hw_vt.c -- the VideoToolbox video-encoder object around the MT6893 hardware JPEG encoder (/dev/redwood-jpeg).
 *
 * Same class, same ABI and same behaviour as gpu/video/vt/n2jpeg_vt.c (the software reserve JPEG encoder): one CMBaseObject
 * subclass with the VTVideoEncoder class record of include/n2vt_abi.h, one complete JPEG file per submitted frame in one
 * CMSampleBuffer, the VT "Quality" property (0..1) mapped to 1..100, every other standard property accepted and stored with
 * no effect (so clients do not stop at a property error).
 *
 * The difference is the back end: instead of the n2jpeg software encoder this object encodes through jpeg/rwjpeg.c, i.e. the
 * hardware block.  It accepts NV12 ('420v'/'420f') and 32-bit BGRA pixels -- the block eats NV12 and rwjpeg converts BGRA with
 * integer full-range BT.601 arithmetic, which is the colour model JFIF declares.
 *
 * Failure: if the device is absent or a job fails, the frame is finished with VTEncoderSessionEmitEncodedFrame(error) and
 * encodeFrame returns N2_kVTVideoEncoderMalfunctionErr (-12912, a public VTErrors.h value).  It never silently produces a
 * software JPEG: the caller decides whether to fall back to n2jpeg_vt.c.
 *
 * Registration: the factory is n2jpeg_hw_vt_create; n2jpeg_hw_vt_copy_registration_info() returns an info dictionary with
 * CMClassImplementationID = n2jpeg_hw_vt_id (a distinct ID from the software object, so clients can choose) and
 * kVTVideoEncoder_IsHardwareAccelerated = true.  n2jpeg_hw_vt_available() says whether the device answers; the plug-in should
 * register this object only when it does (and register the software object otherwise), because VideoToolbox keeps no
 * per-frame fallback between registered encoders.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "n2_apple.h"
#include "n2vt_abi.h"
#include "rwjpeg.h"

/* WP01's console log, when this file is compiled next to gpu/video/vt (the plug-in build).  Built alone, the log is silent. */
#if defined(__has_include)
#if __has_include("n2vt_log.h")
#include "n2vt_log.h"
#define N2JPEG_HW_VT_HAVE_LOG 1
#endif
#endif
#ifndef N2JPEG_HW_VT_HAVE_LOG
static inline void n2vt_say(const char *fmt, ...)
{
	(void)fmt;
}
#endif

/* CMVideoFormatDescriptionCreate is a public CoreMedia call that include/n2_apple.h does not declare; same weak treatment as
 * vt/n2jpeg_vt.h (absent weak import -> NULL check, not a load failure). */
#ifdef N2_HOST_FAKE
extern OSStatus CMVideoFormatDescriptionCreate(CFAllocatorRef allocator, uint32_t codecType, int32_t width, int32_t height,
                                               CFDictionaryRef extensions, CMFormatDescriptionRef *formatDescriptionOut)
	__attribute__((weak));
#else
extern OSStatus CMVideoFormatDescriptionCreate(CFAllocatorRef allocator, uint32_t codecType, int32_t width, int32_t height,
                                               CFDictionaryRef extensions, CMFormatDescriptionRef *formatDescriptionOut)
	__attribute__((weak_import));
#endif

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2JPEG_HW_WEAK __attribute__((weak_import))
#else
#define N2JPEG_HW_WEAK __attribute__((weak))
#endif
/* The registration key of a device whose factory n2vt_abi.h declares without the weak attribute (same pattern as the shim). */
extern const CFStringRef kVTVideoEncoder_IsHardwareAccelerated N2JPEG_HW_WEAK;

#define N2_CODEC_JPEG       0x6a706567u
#define N2_MEDIA_JPEG       0x6a706567u
#define N2_PIXFMT_BGRA      0x42475241u        /* kCVPixelFormatType_32BGRA */
#define N2_JPEG_MAX_DIM     8192
#define N2_JPEG_GROW_SLACK  4096
#define N2_JPEG_MIN_CAP     (256u * 1024u)

/* The encoder ID this object registers with (distinct from N2VT_ID_JPEG, the software object). */
#define N2JPEG_HW_IMPLEMENTATION_ID "com.nord2.videotoolbox.videoencoder.jpeg.mtk"

#define N2_PROP_NUM   1u
#define N2_PROP_BOOL  2u
#define N2_PROP_STR   4u
#define N2_PROP_DICT  8u
#define N2_PROP_ARR   16u
#define N2_PROP_RO    0x80u

enum n2jpeg_hw_prop_id {
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

struct n2jpeg_hw_prop {
	const char *name;
	const char *type_name;
	uint32_t    accept;
};

static const struct n2jpeg_hw_prop s_props[P_COUNT] = {
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

struct n2jpeg_hw_vt {
	pthread_mutex_t         lock;
	int                     mutex_ok;
	VTVideoEncoderSession   session;
	int32_t                 width, height;
	CMFormatDescriptionRef  fmt;
	rwjpeg                 *dev;            /* opened lazily at the first encodeFrame */
	CFMutableDictionaryRef  props;

	double                  quality;
	int                     quality_set;
	uint8_t                *out;
	size_t                  out_cap;
	int                     no_fmt_logged;
};

#ifdef N2_HOST_FAKE
/* Host tests replace the device with their cjpeg-backed fake.  Never compiled into the iOS object. */
static int (*s_test_open)(rwjpeg **out);

void
n2jpeg_hw_vt_test_set_opener(int (*fn)(rwjpeg **out))
{
	s_test_open = fn;
}
#endif

/* ------------------------------------------------------------------ helpers */

static struct n2jpeg_hw_vt *
storage(CMBaseObjectRef object)
{
	return (struct n2jpeg_hw_vt *)CMBaseObjectGetDerivedStorage(object);
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
	if (accept & N2_PROP_ARR) {
		/* No CFArrayGetTypeID in n2_apple.h: an empty probe array gives the type id. */
		CFMutableArrayRef probe = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
		CFTypeID array_type;

		if (probe == NULL)
			return 0;
		array_type = CFGetTypeID(probe);
		CFRelease(probe);
		if (t == array_type)
			return 1;
	}
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
jpeg_quality_from_vt(const struct n2jpeg_hw_vt *e)
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
vt_status(int status)
{
	switch (status) {
	case RWJPEG_E_INVAL:
		return N2_kVTParameterErr;
	case RWJPEG_E_NOMEM:
		return N2_kVTAllocationFailedErr;
	default:
		return N2_kVTVideoEncoderMalfunctionErr;
	}
}

static OSStatus
emit_failure(struct n2jpeg_hw_vt *e, VTVideoEncoderFrame frame, OSStatus status)
{
	if (e->session != NULL)
		(void)VTEncoderSessionEmitEncodedFrame(e->session, frame, status, 0, NULL);
	return status;
}

static int
ensure_device(struct n2jpeg_hw_vt *e)
{
	int status;

	if (e->dev != NULL)
		return RWJPEG_OK;
#ifdef N2_HOST_FAKE
	if (s_test_open != NULL)
		status = s_test_open(&e->dev);
	else
		status = rwjpeg_open(&e->dev, NULL);
#else
	status = rwjpeg_open(&e->dev, NULL);
#endif
	if (status != RWJPEG_OK) {
		e->dev = NULL;
		n2vt_say("n2jpeg-hw: no device (%s)", rwjpeg_status_name(status));
	}
	return status;
}

static int
ensure_buffer(struct n2jpeg_hw_vt *e)
{
	if (e->out != NULL)
		return 0;
	{
		size_t cap = (size_t)e->width * (size_t)e->height / 2u + 65536u;

		if (cap < N2_JPEG_MIN_CAP)
			cap = N2_JPEG_MIN_CAP;
		e->out = (uint8_t *)malloc(cap);
		if (e->out == NULL)
			return N2_kVTAllocationFailedErr;
		e->out_cap = cap;
	}
	return 0;
}

static int
grow_buffer(struct n2jpeg_hw_vt *e, size_t need)
{
	uint8_t *grown;

	if (need > SIZE_MAX - N2_JPEG_GROW_SLACK)
		return -1;
	grown = (uint8_t *)realloc(e->out, need + N2_JPEG_GROW_SLACK);
	if (grown == NULL)
		return -1;
	e->out = grown;
	e->out_cap = need + N2_JPEG_GROW_SLACK;
	return 0;
}

static int
ensure_format(struct n2jpeg_hw_vt *e)
{
	if (e->fmt != NULL)
		return 0;
	if (CMVideoFormatDescriptionCreate == NULL) {
		if (!e->no_fmt_logged) {
			n2vt_say("n2jpeg-hw: CMVideoFormatDescriptionCreate is absent");
			e->no_fmt_logged = 1;
		}
		return 0;
	}
	if (CMVideoFormatDescriptionCreate(kCFAllocatorDefault, N2_MEDIA_JPEG, e->width, e->height, NULL, &e->fmt) != 0 ||
	    e->fmt == NULL) {
		e->fmt = NULL;
		n2vt_say("n2jpeg-hw: CMVideoFormatDescriptionCreate('jpeg', %dx%d) failed", (int)e->width, (int)e->height);
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	return 0;
}

/* ------------------------------------------------------------------ properties */

static OSStatus
base_copy_property(CMBaseObjectRef object, CFStringRef key, CFAllocatorRef allocator, void *valueOut)
{
	struct n2jpeg_hw_vt *e = storage(object);
	CFTypeRef value;

	(void)allocator;
	if (key == NULL || valueOut == NULL)
		return N2_kVTParameterErr;
	if (CFGetTypeID((CFTypeRef)key) != CFStringGetTypeID())
		return N2_kVTParameterErr;

	if (key_is(key, s_props[P_USING_HW].name)) {
		*(CFTypeRef *)valueOut = CFRetain(kCFBooleanTrue);
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
	struct n2jpeg_hw_vt *e = storage(object);
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
			n2vt_say("n2jpeg-hw: unsupported property %s", keybuf);
		else
			n2vt_say("n2jpeg-hw: unsupported property (unprintable)");
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
			e->quality = q;
			e->quality_set = 1;
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
	return CFStringCreateWithCString(kCFAllocatorDefault, "n2jpeg MT6893 hardware JPEG encoder", kCFStringEncodingUTF8);
}

static void
base_finalize(CMBaseObjectRef object)
{
	struct n2jpeg_hw_vt *e = storage(object);

	if (e->dev != NULL) {
		rwjpeg_close(e->dev);
		e->dev = NULL;
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
	struct n2jpeg_hw_vt *e = storage((CMBaseObjectRef)encoder);
	CFMutableArrayRef formats = NULL;
	CFMutableDictionaryRef attributes = NULL;
	CFMutableDictionaryRef surface = NULL;
	CFNumberRef width_number = NULL, height_number = NULL, format_number = NULL;
	OSStatus status = N2_kVTAllocationFailedErr;

	if (session == NULL)
		return N2_kVTParameterErr;
	if (dims.width < 2 || dims.height < 2 || dims.width > N2_JPEG_MAX_DIM || dims.height > N2_JPEG_MAX_DIM ||
	    (dims.width & 1) != 0 || (dims.height & 1) != 0) {
		n2vt_say("n2jpeg-hw: rejected %dx%d (even, 2..%d)", (int)dims.width, (int)dims.height, N2_JPEG_MAX_DIM);
		return N2_kVTParameterErr;
	}

	formats = CFArrayCreateMutable(kCFAllocatorDefault, 3, &kCFTypeArrayCallBacks);
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
	format_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ (int32_t)N2_PIXFMT_BGRA });
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
	if (e->dev != NULL) {
		rwjpeg_close(e->dev);
		e->dev = NULL;
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

/* Encode the locked pixel buffer; *r receives an rwjpeg status.  The callers below do the VT plumbing. */
static int
encode_pixels(struct n2jpeg_hw_vt *e, CVPixelBufferRef pb, unsigned quality, size_t *jpeg_size)
{
	uint32_t pixel_format = CVPixelBufferGetPixelFormatType(pb);
	size_t width = CVPixelBufferGetWidth(pb);
	size_t height = CVPixelBufferGetHeight(pb);
	size_t plane_count = CVPixelBufferGetPlaneCount(pb);
	int r;

	if (width != (size_t)e->width || height != (size_t)e->height)
		return RWJPEG_E_INVAL;

	if (pixel_format == N2_PIXFMT_420V || pixel_format == N2_PIXFMT_420F) {
		const uint8_t *y, *c;
		size_t y_stride, c_stride;

		if (plane_count < 2)
			return RWJPEG_E_INVAL;
		y = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
		c = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
		y_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
		c_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
		if (y == NULL || c == NULL || y_stride < width || c_stride < width || y_stride > 0xffffffffu ||
		    c_stride > 0xffffffffu)
			return RWJPEG_E_INVAL;
		r = rwjpeg_encode_nv12(e->dev, y, c, (uint32_t)y_stride, (uint32_t)c_stride,
		                       (uint32_t)width, (uint32_t)height, quality, e->out, e->out_cap, jpeg_size, NULL);
	} else if (pixel_format == N2_PIXFMT_BGRA) {
		const uint8_t *base;
		size_t stride;

		if (plane_count < 1)
			return RWJPEG_E_INVAL;
		base = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
		stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
		if (base == NULL || stride < width * 4u || stride > 0xffffffffu)
			return RWJPEG_E_INVAL;
		r = rwjpeg_encode_bgra(e->dev, base, (uint32_t)stride, (uint32_t)width, (uint32_t)height, quality,
		                       e->out, e->out_cap, jpeg_size, NULL);
	} else {
		return RWJPEG_E_INVAL;
	}
	if (r == RWJPEG_E_NOSPC) {
		/* *jpeg_size is the size the call needs. */
		if (grow_buffer(e, *jpeg_size) != 0)
			return RWJPEG_E_NOMEM;
		if (pixel_format == N2_PIXFMT_BGRA) {
			const uint8_t *base = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
			size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);

			r = rwjpeg_encode_bgra(e->dev, base, (uint32_t)stride, (uint32_t)width, (uint32_t)height, quality,
			                       e->out, e->out_cap, jpeg_size, NULL);
		} else {
			const uint8_t *y = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
			const uint8_t *c = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
			size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
			size_t c_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);

			r = rwjpeg_encode_nv12(e->dev, y, c, (uint32_t)y_stride, (uint32_t)c_stride,
			                       (uint32_t)width, (uint32_t)height, quality, e->out, e->out_cap, jpeg_size, NULL);
		}
	}
	return r;
}

static OSStatus
enc_encode_frame(VTVideoEncoderRef encoder, VTVideoEncoderFrame frame, CVImageBufferRef image,
                 const CMTime *pts, const CMTime *duration, CFDictionaryRef frameProperties,
                 uint32_t *infoFlagsOut)
{
	struct n2jpeg_hw_vt *e = storage((CMBaseObjectRef)encoder);
	CVPixelBufferRef pb = (CVPixelBufferRef)image;
	CMTime zero = { 0, 1, N2_CMTIME_VALID, 0 };
	CMSampleTimingInfo timing;
	CMBlockBufferRef block = NULL;
	CMSampleBufferRef sample = NULL;
	CFArrayRef attachments;
	size_t jpeg_size = 0;
	int32_t lock_status;
	int status;
	OSStatus vstatus;

	(void)frameProperties;
	if (infoFlagsOut != NULL)
		*infoFlagsOut = 0;

	pthread_mutex_lock(&e->lock);

	if (e->session == NULL) {
		pthread_mutex_unlock(&e->lock);
		return N2_kVTInvalidSessionErr;
	}
	if (pb == NULL) {
		vstatus = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}
	status = ensure_device(e);
	if (status != RWJPEG_OK) {
		vstatus = emit_failure(e, frame, vt_status(status));
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}
	status = ensure_buffer(e);
	if (status != 0) {
		vstatus = emit_failure(e, frame, status);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}

	lock_status = CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
	if (lock_status != 0) {
		vstatus = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}
	status = encode_pixels(e, pb, (unsigned)jpeg_quality_from_vt(e), &jpeg_size);
	(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
	if (status != RWJPEG_OK) {
		n2vt_say("n2jpeg-hw: encode failed (%s)", rwjpeg_status_name(status));
		vstatus = emit_failure(e, frame, vt_status(status));
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}
	if (jpeg_size == 0 || jpeg_size > 0x7fffffffu) {
		vstatus = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}

	vstatus = ensure_format(e);
	if (vstatus != 0) {
		vstatus = emit_failure(e, frame, vstatus);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}

	if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, NULL, jpeg_size, kCFAllocatorDefault, NULL,
	                                       0, jpeg_size, N2_kCMBlockBufferAssureMemoryNowFlag, &block) != 0 ||
	    block == NULL) {
		n2vt_say("n2jpeg-hw: CMBlockBufferCreateWithMemoryBlock failed");
		if (block != NULL)
			CFRelease(block);
		vstatus = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}
	if (CMBlockBufferReplaceDataBytes(e->out, block, 0, jpeg_size) != 0) {
		n2vt_say("n2jpeg-hw: CMBlockBufferReplaceDataBytes failed");
		CFRelease(block);
		vstatus = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}

	timing.duration = (duration != NULL) ? *duration : zero;
	timing.presentationTimeStamp = (pts != NULL) ? *pts : zero;
	timing.decodeTimeStamp = timing.presentationTimeStamp;

	if (CMSampleBufferCreateReady(kCFAllocatorDefault, block, e->fmt, 1, 1, &timing, 1, &jpeg_size,
	                              &sample) != 0 || sample == NULL) {
		n2vt_say("n2jpeg-hw: CMSampleBufferCreateReady failed");
		if (sample != NULL)
			CFRelease(sample);
		CFRelease(block);
		vstatus = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return vstatus;
	}

	attachments = CMSampleBufferGetSampleAttachmentsArray(sample, 1);
	if (attachments != NULL && CFArrayGetCount(attachments) > 0) {
		CFMutableDictionaryRef dict = (CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);

		if (dict != NULL) {
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_NotSync, kCFBooleanFalse);
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_DependsOnOthers, kCFBooleanFalse);
		}
	}

	vstatus = VTEncoderSessionEmitEncodedFrame(e->session, frame, 0, 0, sample);
	CFRelease(sample);
	CFRelease(block);
	pthread_mutex_unlock(&e->lock);
	return vstatus;
}

static OSStatus
enc_complete_frames(VTVideoEncoderRef encoder, const CMTime *completeUntilPTS)
{
	struct n2jpeg_hw_vt *e = storage((CMBaseObjectRef)encoder);

	(void)completeUntilPTS;
	pthread_mutex_lock(&e->lock);
	/* The kernel's ioctl is synchronous: nothing is ever pending. */
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
		key = CFStringCreateWithCString(kCFAllocatorDefault, s_props[i].name, kCFStringEncodingUTF8);
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
	.derivedStorageSize = sizeof(struct n2jpeg_hw_vt),
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

/* ------------------------------------------------------------------ entry points */

OSStatus
n2jpeg_hw_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	CMBaseObjectRef object = NULL;
	struct n2jpeg_hw_vt *e;
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

/* 1 when /dev/redwood-jpeg answers with the frozen ABI and the encoder capability.  The plug-in uses this to choose between
 * this object and the software n2jpeg object at registration time. */
int
n2jpeg_hw_vt_available(void)
{
	return rwjpeg_present();
}

/* The info dictionary to pass to VTRegisterVideoEncoderWithInfo('jpeg', info, n2jpeg_hw_vt_create); the caller releases it.
 * NULL when CoreFoundation is not fully there.  kVTVideoEncoder_IsHardwareAccelerated is weak: when the running VideoToolbox
 * does not export it, the key is left out and the encoder stays selectable by everyone. */
CFDictionaryRef
n2jpeg_hw_vt_copy_registration_info(void)
{
	CFMutableDictionaryRef info;
	CFStringRef implementation_id;

	info = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                 &kCFTypeDictionaryValueCallBacks);
	if (info == NULL)
		return NULL;
	implementation_id = CFStringCreateWithCString(kCFAllocatorDefault, N2JPEG_HW_IMPLEMENTATION_ID,
	                                              kCFStringEncodingUTF8);
	if (implementation_id != NULL) {
		CFDictionarySetValue(info, N2VT_INFO_IMPLEMENTATION_ID, implementation_id);
		CFRelease(implementation_id);
	}
	if (kVTVideoEncoder_IsHardwareAccelerated != NULL && kCFBooleanTrue != NULL)
		CFDictionarySetValue(info, kVTVideoEncoder_IsHardwareAccelerated, kCFBooleanTrue);
	return info;
}
