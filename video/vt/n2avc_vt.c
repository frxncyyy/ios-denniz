/* SPDX-License-Identifier: MIT */
/*
 * n2avc_vt.c -- the VideoToolbox video-encoder object around the n2avc H.264 encoder (WP05).
 *
 * One CMBaseObject subclass with a VTVideoEncoder class record, exactly as include/n2vt_abi.h freezes it. The object owns
 * an n2avc encoder that is opened lazily at the first encodeFrame(); the packets n2avc writes are AVCC (4-byte big-endian
 * NAL lengths, no in-band SPS/PPS) and are wrapped in a CMBlockBuffer/CMSampleBuffer per submitted frame.
 *
 * Original code written from ITU-T H.264 and the plug-in ABI observed in the iOS 16.7 cache; no Apple SDK header and no
 * third-party codec source is involved.  SPDX-License-Identifier: MIT
 */
#if defined(N2_HOST_FAKE) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L        /* clock_gettime in the host test build */
#endif
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2_apple.h"
#include "n2avc.h"
#include "n2avc_vt.h"
#include "n2vt_abi.h"

/* WP01 delivers vt/n2vt_log.h. Until it exists (e.g. while this package is built alone) the log is a no-op. */
#if defined(__has_include)
#if __has_include("n2vt_log.h")
#include "n2vt_log.h"
#define N2AVC_VT_HAVE_LOG 1
#endif
#endif
#ifndef N2AVC_VT_HAVE_LOG
static inline void n2vt_say(const char *fmt, ...)
{
	(void)fmt;
}
#endif

#define N2_CODEC_AVC1 0x61766331u

/* (lead) wall-clock nanoseconds, for the per-session timing said on the console */
static uint64_t now_ns(void)
{
#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

/* n2_apple.h does not declare CFArrayGetTypeID; ask CoreFoundation for an array's type id without importing it by
 * making a throw-away empty array. Only the rare DataRateLimits property pays for it. */
static int is_cf_array(CFTypeRef value)
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

enum n2_prop_id {
	P_AVERAGE_BITRATE,
	P_EXPECTED_FRAME_RATE,
	P_MAX_KEYFRAME_INTERVAL,
	P_MAX_KEYFRAME_INTERVAL_DURATION,
	P_REAL_TIME,
	P_QUALITY,
	P_NUMBER_OF_SLICES,
	P_N2_THREADS,
	P_DATA_RATE_LIMITS,
	P_PROFILE_LEVEL,
	P_ALLOW_FRAME_REORDERING,
	P_ALLOW_TEMPORAL_COMPRESSION,
	P_H264_ENTROPY_MODE,
	P_COLOR_PRIMARIES,
	P_TRANSFER_FUNCTION,
	P_YCBCR_MATRIX,
	P_PIXEL_ASPECT_RATIO,
	P_CLEAN_APERTURE,
	P_FIELD_COUNT,
	P_MAX_FRAME_DELAY_COUNT,
	P_PRIORITY,
	P_SOURCE_FRAME_COUNT,
	P_EXPECTED_DURATION,
	P_BASE_LAYER_FRAME_RATE,
	P_ENCODER_ID,
	P_USING_HW,
	P_PENDING_FRAMES,
	P_ALLOW_OPEN_GOP,
	P_MAXIMIZE_POWER_EFFICIENCY,
	P_COUNT
};

struct n2_prop {
	const char *name;
	const char *type_name;      /* "Number" / "Boolean" / "Enumeration" */
	uint32_t    accept;         /* N2_PROP_* type bits, N2_PROP_RO for read-only */
};

static const struct n2_prop s_props[P_COUNT] = {
	[P_AVERAGE_BITRATE]                = { "AverageBitRate", "Number", N2_PROP_NUM },
	[P_EXPECTED_FRAME_RATE]            = { "ExpectedFrameRate", "Number", N2_PROP_NUM },
	[P_MAX_KEYFRAME_INTERVAL]          = { "MaxKeyFrameInterval", "Number", N2_PROP_NUM },
	[P_MAX_KEYFRAME_INTERVAL_DURATION] = { "MaxKeyFrameIntervalDuration", "Number", N2_PROP_NUM },
	[P_REAL_TIME]                      = { "RealTime", "Boolean", N2_PROP_BOOL },
	[P_QUALITY]                        = { "Quality", "Number", N2_PROP_NUM },
	[P_NUMBER_OF_SLICES]               = { "NumberOfSlices", "Number", N2_PROP_NUM },
	[P_N2_THREADS]                     = { "N2Threads", "Number", N2_PROP_NUM },
	[P_DATA_RATE_LIMITS]               = { "DataRateLimits", "Enumeration", N2_PROP_ARR },
	[P_PROFILE_LEVEL]                  = { "ProfileLevel", "Enumeration", N2_PROP_STR },
	[P_ALLOW_FRAME_REORDERING]         = { "AllowFrameReordering", "Boolean", N2_PROP_BOOL },
	[P_ALLOW_TEMPORAL_COMPRESSION]     = { "AllowTemporalCompression", "Boolean", N2_PROP_BOOL },
	[P_H264_ENTROPY_MODE]              = { "H264EntropyMode", "Enumeration", N2_PROP_STR | N2_PROP_NUM },
	[P_COLOR_PRIMARIES]                = { "ColorPrimaries", "Enumeration", N2_PROP_STR },
	[P_TRANSFER_FUNCTION]              = { "TransferFunction", "Enumeration", N2_PROP_STR },
	[P_YCBCR_MATRIX]                   = { "YCbCrMatrix", "Enumeration", N2_PROP_STR },
	[P_PIXEL_ASPECT_RATIO]             = { "PixelAspectRatio", "Enumeration", N2_PROP_DICT },
	[P_CLEAN_APERTURE]                 = { "CleanAperture", "Enumeration", N2_PROP_DICT },
	[P_FIELD_COUNT]                    = { "FieldCount", "Number", N2_PROP_NUM },
	[P_MAX_FRAME_DELAY_COUNT]          = { "MaxFrameDelayCount", "Number", N2_PROP_NUM },
	[P_PRIORITY]                       = { "Priority", "Number", N2_PROP_NUM },
	[P_SOURCE_FRAME_COUNT]             = { "SourceFrameCount", "Number", N2_PROP_NUM },
	[P_EXPECTED_DURATION]              = { "ExpectedDuration", "Number", N2_PROP_NUM },
	[P_BASE_LAYER_FRAME_RATE]          = { "BaseLayerFrameRate", "Number", N2_PROP_NUM },
	[P_ENCODER_ID]                     = { "EncoderID", "Enumeration", N2_PROP_STR },
	[P_USING_HW]                       = { "UsingHardwareAcceleratedVideoEncoder", "Boolean", N2_PROP_BOOL | N2_PROP_RO },
	[P_PENDING_FRAMES]                 = { "NumberOfPendingFrames", "Number", N2_PROP_NUM | N2_PROP_RO },
	[P_ALLOW_OPEN_GOP]                 = { "AllowOpenGOP", "Boolean", N2_PROP_BOOL },
	[P_MAXIMIZE_POWER_EFFICIENCY]      = { "MaximizePowerEfficiency", "Boolean", N2_PROP_BOOL },
};

struct n2avc_vt {
	pthread_mutex_t         lock;
	int                     mutex_ok;
	VTVideoEncoderSession   session;        /* not retained: VideoToolbox owns it */
	int32_t                 width, height;
	CMVideoFormatDescriptionRef fmt;        /* created from n2avc's own SPS/PPS, kept for the session */
	n2avc_enc              *enc;            /* opened lazily at the first encodeFrame */
	CFMutableDictionaryRef  props;
	uint64_t                said_frames, said_bytes, said_keys;     /* (lead) for the console: was this encoder used, how much */
	uint32_t                said_failures;
	uint64_t                said_ns, said_ns_max, said_first_ns, said_last_ns;

	/* Used properties (decoded into the n2avc_config at open). */
	uint32_t bitrate;                       /* bits/s, 0 = constant QP */
	uint32_t fps_num, fps_den;
	uint32_t keyint;                        /* pictures, 0 = n2avc default */
	double   keyint_duration;               /* seconds, 0 = unset */
	uint32_t threads;
	int      quality_set;
	double   quality;
	uint8_t  colour_primaries, transfer, matrix;
	uint64_t drl_bytes;
	uint32_t drl_seconds;
	int      drl_set;

	/* The input pixel format is fixed by the first picture (it is a property of the whole stream). */
	int      full_range;
	int      range_fixed;
};

/* ------------------------------------------------------------------ helpers */

static struct n2avc_vt *storage(CMBaseObjectRef object)
{
	return (struct n2avc_vt *)CMBaseObjectGetDerivedStorage(object);
}

static int key_is(CFStringRef key, const char *name)
{
	CFStringRef probe = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
	Boolean equal;

	if (probe == NULL)
		return 0;
	equal = CFEqual(key, probe);
	CFRelease(probe);
	return equal != 0;
}

static int prop_find(CFStringRef key)
{
	int i;

	for (i = 0; i < (int)P_COUNT; i++)
		if (key_is(key, s_props[i].name))
			return i;
	return -1;
}

static CFStringRef prop_name_ref(int id)
{
	return CFStringCreateWithCString(kCFAllocatorDefault, s_props[id].name, kCFStringEncodingUTF8);
}

static int accepts_value(uint32_t accept, CFTypeRef value)
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

/* The fake and the real CFNumbers may store different internal types; accept any numeric flavour. */
static int number_i64(CFNumberRef number, int64_t *out)
{
	double  vd;
	int64_t v64;
	int32_t v32;

	/* Float64 first: it is a defined conversion for every CFNumber, and the range check avoids undefined integer casts
	 * for NaN/Infinity values a client could hand us. */
	if (CFNumberGetValue(number, kCFNumberFloat64Type, &vd)) {
		if (vd != vd || vd < -9223372036854775808.0 || vd >= 9223372036854775808.0)
			return -1;
		*out = (int64_t)(vd < 0.0 ? vd - 0.5 : vd + 0.5);
		return 0;
	}
	if (CFNumberGetValue(number, kCFNumberSInt64Type, &v64)) {
		*out = v64;
		return 0;
	}
	if (CFNumberGetValue(number, kCFNumberSInt32Type, &v32)) {
		*out = v32;
		return 0;
	}
	return -1;
}

static int number_double(CFNumberRef number, double *out)
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

static uint32_t gcd_u32(uint32_t a, uint32_t b)
{
	while (b != 0) {
		uint32_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

static uint32_t effective_bitrate(const struct n2avc_vt *e)
{
	uint64_t rate = e->bitrate;

	if (e->drl_set && e->drl_seconds != 0) {
		uint64_t bits = (e->drl_bytes > UINT64_MAX / 8u) ? UINT64_MAX : e->drl_bytes * 8u;
		uint64_t cap = bits / e->drl_seconds;
		if (rate == 0 || cap < rate)
			rate = cap;
	}
	if (rate > 0xffffffffu)
		rate = 0xffffffffu;
	return (uint32_t)rate;
}

static uint32_t quality_qp(double quality)
{
	double q = 51.0 - 41.0 * quality;
	int qp = (int)(q + 0.5);

	if (qp < 10)
		qp = 10;
	if (qp > 51)
		qp = 51;
	return (uint32_t)qp;
}

static uint32_t effective_keyint(const struct n2avc_vt *e)
{
	uint32_t keyint = e->keyint;

	if (e->keyint_duration > 0.0) {
		double pictures = e->keyint_duration * (double)e->fps_num / (double)e->fps_den;
		uint32_t by_duration;
		if (pictures < 1.0)
			pictures = 1.0;
		if (pictures > 4294967295.0)
			pictures = 4294967295.0;
		by_duration = (uint32_t)(pictures + 0.5);
		if (keyint == 0 || by_duration < keyint)
			keyint = by_duration;
	}
	if (keyint == 0)
		keyint = 60;
	return keyint;
}

static uint8_t colour_number(CFStringRef value)
{
	if (CFEqual(value, N2STR("ITU_R_709_2")))
		return 1;
	if (CFEqual(value, N2STR("SMPTE_C")) || CFEqual(value, N2STR("ITU_R_601_4")))
		return 6;
	return 2;
}

static int build_config(const struct n2avc_vt *e, n2avc_config *cfg, int full_range)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->size = (uint32_t)sizeof(*cfg);
	cfg->width = (uint32_t)e->width;
	cfg->height = (uint32_t)e->height;
	cfg->fps_num = e->fps_num;
	cfg->fps_den = e->fps_den;
	cfg->bitrate = effective_bitrate(e);
	cfg->qp = (cfg->bitrate == 0) ? (e->quality_set ? quality_qp(e->quality) : 26u) : 0u;
	cfg->keyint = effective_keyint(e);
	cfg->threads = e->threads;
	cfg->flags = full_range ? N2AVC_F_FULLRANGE : 0u;
	/* (lead) the in-loop deblocking filter (R2, bit-exact against ffmpeg): on up to 1280x720, where there is time for it;
	 * it runs after the slices on one thread (about a quarter of the encode time). Files in /private/var/db decide otherwise. */
	if (access("/private/var/db/nord2-video-no-deblock", 0) != 0 &&
	    ((int64_t)e->width * e->height <= 1280 * 720 || access("/private/var/db/nord2-video-deblock-all", 0) == 0))
		cfg->flags |= N2AVC_F_DEBLOCK;
	cfg->search_range = 0;
	cfg->colour_primaries = e->colour_primaries;
	cfg->transfer = e->transfer;
	cfg->matrix = e->matrix;
	return 0;
}

static OSStatus emit_failure(struct n2avc_vt *e, VTVideoEncoderFrame frame, OSStatus status)
{
	e->said_failures++;             /* the count is said when the session ends */
	if (e->session != NULL)
		(void)VTEncoderSessionEmitEncodedFrame(e->session, frame, status, 0, NULL);
	return status;
}

static OSStatus reconfigure_now(struct n2avc_vt *e)
{
	int r;

	if (e->enc == NULL)
		return 0;
	r = n2avc_reconfigure(e->enc, effective_bitrate(e), e->fps_num, e->fps_den);
	if (r != N2AVC_OK) {
		n2vt_say("n2avc: reconfigure failed (%d)", r);
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	return 0;
}

/* ------------------------------------------------------------------ properties */

static OSStatus base_copy_property(CMBaseObjectRef object, CFStringRef key, CFAllocatorRef allocator, void *valueOut)
{
	struct n2avc_vt *e = storage(object);
	CFTypeRef value;
	CFAllocatorRef alloc = (allocator != NULL) ? allocator : kCFAllocatorDefault;

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
		CFNumberRef number = CFNumberCreate(alloc, kCFNumberSInt32Type, &zero);
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

static OSStatus base_set_property(CMBaseObjectRef object, CFStringRef key, CFTypeRef value)
{
	struct n2avc_vt *e = storage(object);
	char keybuf[128];
	int id;
	int reconfigure = 0;
	int failed = 0;
	OSStatus status = 0;

	if (key == NULL || value == NULL)
		return N2_kVTParameterErr;
	if (CFGetTypeID((CFTypeRef)key) != CFStringGetTypeID())
		return N2_kVTParameterErr;

	id = prop_find(key);
	if (id < 0) {
		/* (lead, after phone run 4: mediaserverd sets EncoderUsage; CMCapture's movie sink sets a dozen more -- FigThreadPriority,
		 * InputPixelFormat, PrioritizeEncodingSpeedOverQuality, RelaxAverageBitRateTarget, MinimizeMemoryUsage, ...). A key we
		 * do not know is KEPT and answered with success, and said on the console so that a run shows what clients ask for:
		 * refusing it could stop a recording, taking it cannot hurt (it has no effect). */
		if (CFStringGetCString(key, keybuf, (CFIndex)sizeof(keybuf), kCFStringEncodingUTF8))
			n2vt_say("n2avc: property %s kept without effect", keybuf);
		else
			n2vt_say("n2avc: property (unprintable) kept without effect");
		pthread_mutex_lock(&e->lock);
		CFDictionarySetValue(e->props, key, value);
		pthread_mutex_unlock(&e->lock);
		return 0;
	}
	if ((s_props[id].accept & N2_PROP_RO) != 0)
		return N2_kVTPropertyNotSupportedErr;
	if (!accepts_value(s_props[id].accept, value))
		return N2_kVTParameterErr;

	pthread_mutex_lock(&e->lock);

	switch (id) {
	case P_AVERAGE_BITRATE: {
		int64_t v;
		if (number_i64((CFNumberRef)value, &v) != 0 || v < 0 || v > 0xffffffffll) {
			failed = 1;
			break;
		}
		e->bitrate = (uint32_t)v;
		reconfigure = 1;
		break;
	}
	case P_EXPECTED_FRAME_RATE: {
		double fps;
		uint32_t num, den, g;
		if (number_double((CFNumberRef)value, &fps) != 0 || !(fps > 0.0) || fps > 1000.0) {
			failed = 1;
			break;
		}
		num = (uint32_t)(fps * 1000.0 + 0.5);
		den = 1000;
		if (num == 0) {
			failed = 1;
			break;
		}
		g = gcd_u32(num, den);
		e->fps_num = num / g;
		e->fps_den = den / g;
		reconfigure = 1;
		break;
	}
	case P_MAX_KEYFRAME_INTERVAL: {
		int64_t v;
		if (number_i64((CFNumberRef)value, &v) != 0 || v < 0 || v > 0xffffffffll) {
			failed = 1;
			break;
		}
		e->keyint = (uint32_t)v;
		break;
	}
	case P_MAX_KEYFRAME_INTERVAL_DURATION: {
		double seconds;
		if (number_double((CFNumberRef)value, &seconds) != 0 || !(seconds >= 0.0)) {
			failed = 1;
			break;
		}
		e->keyint_duration = seconds;
		break;
	}
	case P_REAL_TIME:
		break;                          /* stored; no effect on this encoder */
	case P_QUALITY: {
		double q;
		if (number_double((CFNumberRef)value, &q) != 0 || !(q >= 0.0 && q <= 1.0)) {
			failed = 1;
			break;
		}
		e->quality = q;
		e->quality_set = 1;
		break;
	}
	case P_NUMBER_OF_SLICES:
	case P_N2_THREADS: {
		int64_t v;
		if (number_i64((CFNumberRef)value, &v) != 0 || v < 1 || v > 16) {
			failed = 1;
			break;
		}
		e->threads = (uint32_t)v;
		break;
	}
	case P_DATA_RATE_LIMITS: {
		CFArrayRef arr = (CFArrayRef)value;
		CFTypeRef bytes_value, seconds_value;
		int64_t bytes, seconds;
		if (CFArrayGetCount(arr) < 2) {
			failed = 1;
			break;
		}
		bytes_value = CFArrayGetValueAtIndex(arr, 0);
		seconds_value = CFArrayGetValueAtIndex(arr, 1);
		if (bytes_value == NULL || seconds_value == NULL ||
		    CFGetTypeID(bytes_value) != CFNumberGetTypeID() ||
		    CFGetTypeID(seconds_value) != CFNumberGetTypeID()) {
			failed = 1;
			break;
		}
		if (number_i64((CFNumberRef)bytes_value, &bytes) != 0 || bytes <= 0 ||
		    bytes > 0x7fffffffffffffffll) {
			failed = 1;
			break;
		}
		if (number_i64((CFNumberRef)seconds_value, &seconds) != 0 ||
		    seconds <= 0 || seconds > 0xffffffffll) {
			failed = 1;
			break;
		}
		e->drl_bytes = (uint64_t)bytes;
		e->drl_seconds = (uint32_t)seconds;
		e->drl_set = 1;
		reconfigure = 1;
		break;
	}
	case P_COLOR_PRIMARIES:
		e->colour_primaries = colour_number((CFStringRef)value);
		break;
	case P_TRANSFER_FUNCTION:
		e->transfer = colour_number((CFStringRef)value);
		break;
	case P_YCBCR_MATRIX:
		e->matrix = colour_number((CFStringRef)value);
		break;
	default:
		break;                          /* accepted and stored, no effect */
	}

	if (failed) {
		status = N2_kVTParameterErr;
	} else {
		CFDictionarySetValue(e->props, key, value);
		if (reconfigure)
			status = reconfigure_now(e);
	}
	pthread_mutex_unlock(&e->lock);
	return status;
}

static CFStringRef base_copy_debug_description(CMBaseObjectRef object)
{
	(void)object;
	return CFStringCreateWithCString(kCFAllocatorDefault, "n2avc H.264 encoder", kCFStringEncodingUTF8);
}

static void base_finalize(CMBaseObjectRef object)
{
	struct n2avc_vt *e = storage(object);

	if (e->width != 0)
		n2vt_say("n2avc: session %dx%d ended: %llu pictures (%llu key), %llu bytes, %u failures; encode %llu us/picture "
		         "(longest %llu us), %llu ms from first to last", (int)e->width, (int)e->height,
		         (unsigned long long)e->said_frames, (unsigned long long)e->said_keys, (unsigned long long)e->said_bytes,
		         (unsigned)e->said_failures,
		         (unsigned long long)(e->said_frames ? e->said_ns / e->said_frames / 1000u : 0u),
		         (unsigned long long)(e->said_ns_max / 1000u),
		         (unsigned long long)((e->said_last_ns - e->said_first_ns) / 1000000u));
	if (e->enc != NULL) {
		n2avc_close(e->enc);
		e->enc = NULL;
	}
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

static OSStatus enc_start_session(VTVideoEncoderRef encoder, VTVideoEncoderSession session, CMVideoDimensions dims)
{
	struct n2avc_vt *e = storage((CMBaseObjectRef)encoder);
	CFMutableArrayRef formats;
	CFMutableDictionaryRef attributes;
	CFMutableDictionaryRef surface;
	CFNumberRef width_number, height_number, format_number;
	OSStatus status;

	if (session == NULL)
		return N2_kVTParameterErr;
	if (dims.width < 16 || dims.height < 16 || dims.width > 4096 || dims.height > 2304 ||
	    (dims.width & 1) != 0 || (dims.height & 1) != 0)
		return N2_kVTParameterErr;

	formats = CFArrayCreateMutable(kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
	attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                       &kCFTypeDictionaryValueCallBacks);
	surface = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                    &kCFTypeDictionaryValueCallBacks);
	format_number = width_number = height_number = NULL;
	status = N2_kVTAllocationFailedErr;
	if (formats == NULL || attributes == NULL || surface == NULL)
		goto done;

	format_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ (int32_t)N2_PIXFMT_420V });
	if (format_number != NULL) {
		CFArrayAppendValue(formats, format_number);
		CFRelease(format_number);
		format_number = NULL;
	}
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
	/* A repeated StartSession is a fresh stream: drop the old encoder and format description. */
	if (e->enc != NULL) {
		n2avc_close(e->enc);
		e->enc = NULL;
	}
	if (e->fmt != NULL) {
		CFRelease(e->fmt);
		e->fmt = NULL;
	}
	e->range_fixed = 0;
	e->full_range = 0;
	e->session = session;
	e->width = dims.width;
	e->height = dims.height;
	pthread_mutex_unlock(&e->lock);

	status = VTEncoderSessionSetPixelBufferAttributes(session, attributes);
	n2vt_say("n2avc: session %dx%d started (pixel buffer attributes %d)", (int)dims.width, (int)dims.height, (int)status);
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

static int ensure_format(struct n2avc_vt *e)
{
	const uint8_t *sps = NULL, *pps = NULL;
	size_t sps_size = 0, pps_size = 0;
	const uint8_t *pointers[2];
	size_t sizes[2];
	int r;

	if (e->fmt != NULL)
		return 0;
	r = n2avc_headers(e->enc, &sps, &sps_size, &pps, &pps_size);
	if (r != N2AVC_OK) {
		n2vt_say("n2avc: headers failed (%d)", r);
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	if (sps == NULL || pps == NULL || sps_size == 0 || pps_size == 0) {
		n2vt_say("n2avc: empty SPS/PPS");
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	pointers[0] = sps;
	pointers[1] = pps;
	sizes[0] = sps_size;
	sizes[1] = pps_size;
	if (CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, pointers, sizes, 4, &e->fmt) != 0) {
		n2vt_say("n2avc: CMVideoFormatDescriptionCreateFromH264ParameterSets failed");
		e->fmt = NULL;
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	return 0;
}

static OSStatus enc_encode_frame(VTVideoEncoderRef encoder, VTVideoEncoderFrame frame, CVImageBufferRef image,
                                 const CMTime *pts, const CMTime *duration, CFDictionaryRef frameProperties,
                                 uint32_t *infoFlagsOut)
{
	struct n2avc_vt *e = storage((CMBaseObjectRef)encoder);
	CVPixelBufferRef pb = (CVPixelBufferRef)image;
	n2avc_picture picture;
	n2avc_packet packet;
	CMTime zero = { 0, 1, N2_CMTIME_VALID, 0 };
	CMSampleTimingInfo timing;
	CMBlockBufferRef block = NULL;
	CMSampleBufferRef sample = NULL;
	CFArrayRef attachments;
	uint32_t pixel_format;
	size_t width, height, plane_count, i;
	uint32_t required_planes;
	int32_t lock_status;
	int full_range;
	int force_idr = 0;
	int r, status = 0;

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
	if (pixel_format != N2_PIXFMT_420V && pixel_format != N2_PIXFMT_420F && pixel_format != N2_PIXFMT_I420) {
		status = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	width = CVPixelBufferGetWidth(pb);
	height = CVPixelBufferGetHeight(pb);
	if (width != (size_t)e->width || height != (size_t)e->height) {
		status = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	required_planes = (pixel_format == N2_PIXFMT_I420) ? 3u : 2u;
	plane_count = CVPixelBufferGetPlaneCount(pb);
	if (plane_count < required_planes) {
		status = emit_failure(e, frame, N2_kVTParameterErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	full_range = (pixel_format == N2_PIXFMT_420F);
	if (e->enc != NULL && e->range_fixed && full_range != e->full_range) {
		n2vt_say("n2avc: pixel range changed mid-stream");
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
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
	picture.format = (pixel_format == N2_PIXFMT_I420) ? N2AVC_FMT_I420 : N2AVC_FMT_NV12;
	picture.pts = (pts != NULL) ? pts->value : 0;
	for (i = 0; i < required_planes; i++) {
		void *base = CVPixelBufferGetBaseAddressOfPlane(pb, i);
		size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pb, i);
		if (base == NULL || stride == 0 || stride > 0xffffffffu) {
			(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
			status = emit_failure(e, frame, N2_kVTParameterErr);
			pthread_mutex_unlock(&e->lock);
			return status;
		}
		picture.plane[i] = (const uint8_t *)base;
		picture.stride[i] = (uint32_t)stride;
	}

	if (e->enc == NULL) {
		n2avc_config config;
		build_config(e, &config, full_range);
		r = n2avc_open(&config, &e->enc);
		if (r != N2AVC_OK || e->enc == NULL) {
			e->enc = NULL;
			(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
			n2vt_say("n2avc: n2avc_open failed (%d)", r);
			status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
			pthread_mutex_unlock(&e->lock);
			return status;
		}
		e->full_range = full_range;
		e->range_fixed = 1;
	}

	if (frameProperties != NULL && CFGetTypeID((CFTypeRef)frameProperties) == CFDictionaryGetTypeID()) {
		CFTypeRef force = CFDictionaryGetValue(frameProperties, N2STR("ForceKeyFrame"));
		if (force != NULL && CFGetTypeID(force) == CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)force))
			force_idr = 1;
	}

	{
		uint64_t t0 = now_ns(), dt;
		r = n2avc_encode(e->enc, &picture, force_idr, &packet);
		dt = now_ns() - t0;
		e->said_ns += dt;
		if (dt > e->said_ns_max)
			e->said_ns_max = dt;
		if (e->said_first_ns == 0)
			e->said_first_ns = t0;
		e->said_last_ns = t0 + dt;
	}
	if (r != N2AVC_OK) {
		(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
		n2vt_say("n2avc: n2avc_encode failed (%d)", r);
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

	status = ensure_format(e);
	if (status != 0) {
		status = emit_failure(e, frame, status);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	if (packet.data == NULL || packet.size == 0 || packet.size > 0x7fffffffu) {
		n2vt_say("n2avc: empty or oversized packet");
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, NULL, packet.size, kCFAllocatorDefault, NULL,
	                                       0, packet.size, N2_kCMBlockBufferAssureMemoryNowFlag, &block) != 0 ||
	    block == NULL) {
		n2vt_say("n2avc: CMBlockBufferCreateWithMemoryBlock failed");
		if (block != NULL)
			CFRelease(block);
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}
	if (CMBlockBufferReplaceDataBytes(packet.data, block, 0, packet.size) != 0) {
		n2vt_say("n2avc: CMBlockBufferReplaceDataBytes failed");
		CFRelease(block);
		status = emit_failure(e, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->lock);
		return status;
	}

	timing.duration = (duration != NULL) ? *duration : zero;
	timing.presentationTimeStamp = (pts != NULL) ? *pts : zero;
	timing.decodeTimeStamp = timing.presentationTimeStamp;

	if (CMSampleBufferCreateReady(kCFAllocatorDefault, block, e->fmt, 1, 1, &timing, 1, &packet.size,
	                              &sample) != 0 || sample == NULL) {
		n2vt_say("n2avc: CMSampleBufferCreateReady failed");
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
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_NotSync,
			                     packet.keyframe ? kCFBooleanFalse : kCFBooleanTrue);
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_DependsOnOthers,
			                     packet.keyframe ? kCFBooleanFalse : kCFBooleanTrue);
		}
	}

	if (e->said_frames == 0) {
		n2avc_config said;
		build_config(e, &said, e->full_range);
		n2vt_say("n2avc: first picture %dx%d %s: %u bytes, bitrate %u, fps %u/%u, keyint %u, slices %u, qp %u, deblock %u", (int)e->width,
		         (int)e->height, pixel_format == N2_PIXFMT_420F ? "420f" : pixel_format == N2_PIXFMT_420V ? "420v" : "y420",
		         (unsigned)packet.size, said.bitrate, said.fps_num, said.fps_den, said.keyint, said.threads, (unsigned)packet.qp,
		         (unsigned)((said.flags & N2AVC_F_DEBLOCK) != 0));
	}
	e->said_frames++;
	e->said_bytes += packet.size;
	e->said_keys += packet.keyframe ? 1 : 0;
	status = VTEncoderSessionEmitEncodedFrame(e->session, frame, 0, 0, sample);
	CFRelease(sample);
	CFRelease(block);
	pthread_mutex_unlock(&e->lock);
	return status;
}

static OSStatus enc_complete_frames(VTVideoEncoderRef encoder, const CMTime *completeUntilPTS)
{
	struct n2avc_vt *e = storage((CMBaseObjectRef)encoder);

	(void)completeUntilPTS;
	pthread_mutex_lock(&e->lock);
	/* Encoding is synchronous: nothing is ever pending. */
	pthread_mutex_unlock(&e->lock);
	return 0;
}

static OSStatus enc_copy_supported_properties(VTVideoEncoderRef encoder, CFDictionaryRef *dictOut)
{
	CFMutableDictionaryRef dict;
	CFMutableDictionaryRef entry;
	CFStringRef key;
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
		CFStringRef type_string;
		entry = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
		                                  &kCFTypeDictionaryValueCallBacks);
		if (entry == NULL)
			continue;
		type_string = CFStringCreateWithCString(kCFAllocatorDefault, s_props[i].type_name,
		                                       kCFStringEncodingUTF8);
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
	.derivedStorageSize = sizeof(struct n2avc_vt),
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

OSStatus n2avc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	CMBaseObjectRef object = NULL;
	struct n2avc_vt *e;
	OSStatus status;

	if (instanceOut == NULL)
		return N2_kVTParameterErr;
	if (codecType != N2_CODEC_AVC1)
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

	e->fps_num = 30;
	e->fps_den = 1;
	e->threads = 4;   /* (lead) four slices coded in parallel: real time matters more than the few bytes */

	*instanceOut = (VTVideoEncoderRef)object;
	return 0;
}
