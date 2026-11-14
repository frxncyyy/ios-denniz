/* SPDX-License-Identifier: MIT */
/*
 * n2hwenc_vt.c -- the VideoToolbox hardware H.264/HEVC encoder objects backed by vcodecd (package P04).
 *
 * One CMBaseObject subclass with the VTVideoEncoder class record of gpu/video/include/n2vt_abi.h, instantiated for
 * 'avc1' and for 'hvc1'/'hev1'.  Every frame goes through struct n2hwenc_client (n2hwenc_client.h): the object copies
 * the NV12 planes' host pointers and strides, asks the client for one Annex B access unit, strips the parameter sets the
 * daemon may prepend, converts the start codes to four-byte lengths (vt/annexb.c) and emits a CMSampleBuffer with the
 * key-frame attachments n2avc_vt.c uses.  The format description is built from the first key frame's SPS/PPS(/VPS) with
 * the CoreMedia parameter-set constructors; the avcC/hvcC records our annexb.c writes are built alongside as the
 * validation of those parameter sets and the trace's byte count.
 *
 * Pipeline (opus-cam, 12 Nov 2026, Camera integration).  vcodecd serialises one picture at a time and a 1080p picture
 * costs ~29 ms of daemon time at 273 MHz plus the copy into the relay buffer, so a synchronous encodeFrame would hold
 * the capture graph's compressor thread for a whole frame period.  With max_in_flight > 1 (default 2) a worker thread
 * owns the client calls: encodeFrame validates and retains the CVPixelBuffer, queues it and returns
 * (kVTEncodeInfo_Asynchronous); the worker copies it into the relay buffer, encodes and releases it.  Emission is
 * deferred to VideoToolbox's own calling thread: every encodeFrame and completeFrames first emits, in submission order,
 * the frames the worker has finished.  So VTEncoderSessionEmitEncodedFrame is only ever called from a thread inside a
 * VideoToolbox callout (exactly like the synchronous software encoder that Camera already records through), never
 * from the worker, and the worker never waits for VideoToolbox.  Every wait is bounded (N2HWENC_WAIT_MS); a stuck
 * client turns into one malfunction emit per outstanding frame, never into a hang.  max_in_flight 1 is the old
 * synchronous behaviour (also forced by the switch file /private/var/db/nord2-video-hwenc-sync, or when the client
 * sets MaxFrameDelayCount 0).
 *
 * Original code; no Apple SDK header and no third-party codec source is involved.  Error policy: if the daemon cannot be
 * reached at StartSession the object returns kVTVideoEncoderNotAvailableNowErr so VideoToolbox skips it and keeps the
 * software encoder for the codec type (VideoToolbox retries the next candidate on any StartSession error, [V]
 * facts/VT_DECODER_ABI.md section 5); StartSession is bounded to about 2 s.  A failure later in the session is a
 * malfunction (one emit with the error and a NULL sample buffer, exactly one emit per submitted frame).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1               /* glibc host build: usleep, clock_gettime, recursive mutex attr */
#endif
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2_apple.h"
#include "n2vt_abi.h"
#include "n2hwenc_annexb.h"
#include "n2hwenc_client.h"
#include "n2hwenc_vt.h"

#define N2_CODEC_AVC1 0x61766331u   /* 'avc1' */
#define N2_CODEC_HVC1 0x68766331u   /* 'hvc1' */
#define N2_CODEC_HEV1 0x68657631u   /* 'hev1' */

#define N2HWENC_MAX_PS 8

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2_WEAK_LOG __attribute__((weak_import))
#else
#define N2_WEAK_LOG __attribute__((weak))
#endif
/* gpu/video/vt/n2vt_log.c's console channel when it is in the same dylib (the product link), absent in the host suite.
 * Same lines as n2avc_vt.c so a phone run shows what Camera asks for and why the hardware path was skipped. */
extern void n2vt_say(const char *format, ...) __attribute__((format(printf, 1, 2))) N2_WEAK_LOG;
#define N2HW_SAY(...) do { if (n2vt_say != NULL) n2vt_say(__VA_ARGS__); } while (0)
#define N2HWENC_MAX_OUT (64u * 1024u * 1024u)

/* Pipeline limits (opus-cam). */
#define N2HWENC_SLOTS            3          /* ring size: the most frames that can be in flight */
#define N2HWENC_DEFAULT_IN_FLIGHT 2         /* one encoding in the daemon + one queued */
#define N2HWENC_WAIT_MS          2500u      /* longest wait for one frame: client frame timeout (1 s) + close + slack */
#define N2HWENC_START_RETRY_MS   600u       /* BUSY / daemon restart: keep retrying OPEN this long inside StartSession */
#define N2HWENC_REOPEN_RETRY_MS  1500u      /* first-picture reopen: an HEVC reopen restarts vcodecd (vc24 rule) */
#define N2HWENC_RETRY_SLEEP_US   100000u
#define N2HWENC_SWITCH_SYNC      "/private/var/db/nord2-video-hwenc-sync"
#define N2HWENC_IDLE_REOPEN_MS   8000u      /* vcodecd drops a session idle for 10 s (--session-idle-ms default) */
#define N2_kVTEncodeInfo_Asynchronous 1u

/* ------------------------------------------------------------------ trace */

static pthread_mutex_t s_trace_lock = PTHREAD_MUTEX_INITIALIZER;
static n2hwenc_trace_fn s_trace_fn;
static void *s_trace_ctx;
static int s_max_in_flight_override;        /* n2hwenc_set_max_in_flight(); 0 = switch file / default */

void n2hwenc_set_trace(n2hwenc_trace_fn fn, void *ctx)
{
	pthread_mutex_lock(&s_trace_lock);
	s_trace_fn = fn;
	s_trace_ctx = ctx;
	pthread_mutex_unlock(&s_trace_lock);
}

void n2hwenc_set_max_in_flight(int frames)
{
	pthread_mutex_lock(&s_trace_lock);
	s_max_in_flight_override = (frames < 0 || frames > N2HWENC_SLOTS) ? 0 : frames;
	pthread_mutex_unlock(&s_trace_lock);
}

static int configured_in_flight(void)
{
	int v;

	pthread_mutex_lock(&s_trace_lock);
	v = s_max_in_flight_override;
	pthread_mutex_unlock(&s_trace_lock);
	if (v > 0)
		return v;
	if (access(N2HWENC_SWITCH_SYNC, F_OK) == 0)
		return 1;
	return N2HWENC_DEFAULT_IN_FLIGHT;
}

static void trace(int event, uint32_t a, uint32_t b)
{
	n2hwenc_trace_fn fn;
	void *ctx;

	pthread_mutex_lock(&s_trace_lock);
	fn = s_trace_fn;
	ctx = s_trace_ctx;
	pthread_mutex_unlock(&s_trace_lock);
	if (fn != NULL)
		fn(ctx, event, a, b);
}

static uint64_t now_us(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* Absolute CLOCK_REALTIME deadline for pthread_cond_timedwait (Darwin has no pthread_condattr_setclock). */
static void deadline_in(struct timespec *ts, uint32_t ms)
{
	(void)clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_sec += (time_t)(ms / 1000u);
	ts->tv_nsec += (long)(ms % 1000u) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

/* ------------------------------------------------------------------ properties */

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
	const char *type_name;
	uint32_t    accept;
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


/* ------------------------------------------------------------------ object */

enum slot_state { SLOT_FREE = 0, SLOT_QUEUED, SLOT_RUNNING, SLOT_DONE, SLOT_EMITTING };

/* One submitted picture, from encodeFrame until its emit. */
struct n2hwenc_slot {
	int                 state;
	VTVideoEncoderFrame frame;
	CVPixelBufferRef    pb;                 /* retained from encodeFrame until the worker has copied it */
	CMTime              pts, duration;
	int                 have_pts, have_duration;
	int                 force_key;
	int                 abandoned;          /* already answered with a failure emit (bounded wait expired) */
	int                 result;             /* client status (0 or negative) */
	uint32_t            bytes;              /* Annex B access unit in buf */
	int                 key;
	uint8_t            *buf;                /* Annex B in, length-prefixed sample out, in place */
	uint32_t            cap;
	uint64_t            t_queued, t_done;   /* us, for the session statistics */
};

struct n2hwenc_vt {
	pthread_mutex_t        lock;                /* every field below except where noted */
	int                    mutex_ok;
	pthread_mutex_t        emit_lock;           /* recursive: serialises emission (fmt, the ring head) */
	int                    emit_lock_ok;
	pthread_cond_t         work_cond;           /* worker: a slot became QUEUED, or stop */
	pthread_cond_t         done_cond;           /* emitters: a slot became DONE */
	int                    conds_ok;
	int                    codec;               /* N2HWENC_AVC / N2HWENC_HEVC */
	uint32_t               codec_type;
	VTVideoEncoderSession  session;             /* not retained: VideoToolbox owns it */
	int32_t                width, height;
	CMVideoFormatDescriptionRef fmt;            /* emit_lock */
	struct n2hwenc_client  client;
	struct n2vcd_client   *conn;
	struct n2vcd_session  *cs;
	uint32_t               in_capacity;         /* what enc_frame may write (out_buf_bytes from OPEN) */
	uint32_t               buf_capacity;        /* in_capacity plus the worst-case start-code expansion */
	CFMutableDictionaryRef props;

	/* requested stream parameters (the OPEN message, and ENC_SET for the first two after the first picture) */
	uint32_t               bitrate;
	uint32_t               fps_num, fps_den;
	uint32_t               gop;
	double                 keyint_duration;
	uint32_t               profile, level;
	uint32_t               flags;
	int32_t                max_frame_delay;     /* MaxFrameDelayCount, -1 = not set */
	struct n2hwenc_params  opened;              /* what the open daemon session was opened with */
	uint64_t               opened_at_us;

	/* session state */
	int                    opened_ok;
	int                    fatal;
	int                    degraded;        /* first-picture reopen: 0 as asked, 1 no profile/level, 2 StartSession's set */
	int                    invalidated;
	int                    full_range;           /* 420f vs 420v, fixed by the first picture */
	int                    range_fixed;
	uint64_t               submitted;            /* pictures handed to the pipeline in this stream */
	uint64_t               frames, key_frames, bytes;
	uint32_t               failures;

	/* pipeline */
	struct n2hwenc_slot    slots[N2HWENC_SLOTS];
	uint32_t               head, count;          /* ring: oldest slot, slots in flight */
	int                    max_in_flight;        /* fixed at the first picture */
	int                    pipeline_fixed;
	pthread_t              worker;
	int                    worker_started;
	int                    stop;
	uint8_t               *stage;                /* NV12 repack at the session stride (pitch != align16(width)) */
	uint32_t               stage_bytes;

	/* statistics (lock) */
	uint64_t               client_us_total, client_us_max, wait_us_max, latency_us_max, first_us, last_us;
	uint32_t               staged_frames, enc_set_failures, open_attempts;
	int                    said_first;
	uint32_t               first_stride_y, first_stride_c;
};

static struct n2hwenc_vt *storage(CMBaseObjectRef object)
{
	return (struct n2hwenc_vt *)CMBaseObjectGetDerivedStorage(object);
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

static int number_i64(CFNumberRef number, int64_t *out)
{
	double  vd;
	int64_t v64;
	int32_t v32;

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

/* "H264_High_4_1" / "H264_Baseline_AutoLevel" / "HEVC_Main10_AutoLevel": profile_idc and level_idc, as the protocol's
 * struct n2vcd_open carries them.  A name we do not know leaves profile/level at 0 ("daemon default"), which is what a
 * client that sets no ProfileLevel gets. */
static void parse_profile_level(int codec, const char *name, uint32_t *profile, uint32_t *level)
{
	uint32_t major = 0, minor = 0, p = 0;
	size_t i, len = strlen(name);
	int have_level = 0;

	if (codec == N2HWENC_AVC) {
		if (strstr(name, "Baseline") != NULL)
			p = 66;
		else if (strstr(name, "High10") != NULL)
			p = 110;
		else if (strstr(name, "High422") != NULL)
			p = 122;
		else if (strstr(name, "High") != NULL)
			p = 100;
		else if (strstr(name, "Main") != NULL)
			p = 77;
		else if (strstr(name, "Extended") != NULL)
			p = 88;
	} else {
		if (strstr(name, "MainStill") != NULL)
			p = 3;
		else if (strstr(name, "Main10") != NULL)
			p = 2;
		else if (strstr(name, "Main422") != NULL)
			p = 4;
		else if (strstr(name, "Main") != NULL)
			p = 1;
	}

	/* The last "_<digit>[_<digit>]" group is the level ("AutoLevel" has none). */
	for (i = 0; i + 2 < len; i++) {
		if (name[i] == '_' && name[i + 1] >= '0' && name[i + 1] <= '9') {
			uint32_t a = 0, b = 0;
			size_t j = i + 1;

			while (j < len && name[j] >= '0' && name[j] <= '9')
				a = a * 10 + (uint32_t)(name[j++] - '0');
			if (j < len && name[j] == '_' && j + 1 < len && name[j + 1] >= '0' && name[j + 1] <= '9') {
				j++;
				while (j < len && name[j] >= '0' && name[j] <= '9')
					b = b * 10 + (uint32_t)(name[j++] - '0');
			}
			major = a;
			minor = b;
			have_level = 1;
		}
	}
	if (have_level && major >= 1 && major <= 6 && minor <= 9) {
		if (codec == N2HWENC_AVC)
			*level = major * 10u + minor;
		else
			*level = 30u * major + 3u * minor;
	} else {
		*level = 0;
	}
	*profile = p;
}

static uint32_t effective_bitrate(const struct n2hwenc_vt *e)
{
	return e->bitrate;
}

static uint32_t effective_gop(const struct n2hwenc_vt *e)
{
	uint32_t gop = e->gop;

	if (e->keyint_duration > 0.0) {
		double pictures = e->keyint_duration * (double)e->fps_num / (double)e->fps_den;
		uint32_t by_duration;
		if (pictures < 1.0)
			pictures = 1.0;
		if (pictures > 4294967295.0)
			pictures = 4294967295.0;
		by_duration = (uint32_t)(pictures + 0.5);
		if (gop == 0 || by_duration < gop)
			gop = by_duration;
	}
	return gop;
}


/* ------------------------------------------------------------------ client plumbing */

static uint32_t nvcd_codec(int codec)
{
	return codec == N2HWENC_HEVC ? 0x31637668u /* 'hvc1' */ : 0x31637661u /* 'avc1' */;
}

static uint32_t align16(uint32_t v)
{
	return (v + 15u) & ~15u;
}

static void fill_params(const struct n2hwenc_vt *e, struct n2hwenc_params *p)
{
	memset(p, 0, sizeof(*p));
	p->codec = nvcd_codec(e->codec);
	p->width = (uint32_t)e->width;
	p->height = (uint32_t)e->height;
	p->fps_num = e->fps_num;
	p->fps_den = e->fps_den;
	p->bitrate = effective_bitrate(e);
	p->gop = effective_gop(e);
	p->profile = e->profile;
	p->level = e->level;
	/* cam1-prod: Camera sets RealTime=true, and vcodecd's handle_open refuses any non-zero OPEN.flags with
	 * N2VCD_E_UNSUPPORTED (daemon/vcodecd.c "OPEN.flags bit 0 has no VENC mapping in v1"), which killed the first-picture
	 * reopen (-102 = -EOPNOTSUPP).  RealTime has no daemon effect, so it stays in e->flags (reported in the first-picture
	 * line) and is never sent. */
	p->flags = 0;
}

static void close_session_only(struct n2hwenc_vt *e)
{
	if (e->cs != NULL) {
		(void)e->client.close_session(e->client.ctx, e->cs);
		e->cs = NULL;
	}
	e->opened_ok = 0;
}

static void close_client(struct n2hwenc_vt *e)
{
	close_session_only(e);
	if (e->conn != NULL) {
		(void)e->client.disconnect(e->client.ctx, e->conn);
		e->conn = NULL;
	}
}

static void free_buffers(struct n2hwenc_vt *e)
{
	int i;

	for (i = 0; i < N2HWENC_SLOTS; i++) {
		free(e->slots[i].buf);
		e->slots[i].buf = NULL;
		e->slots[i].cap = 0;
	}
	free(e->stage);
	e->stage = NULL;
	e->stage_bytes = 0;
	e->in_capacity = e->buf_capacity = 0;
}

/* One OPEN on the existing connection.  Returns the client status (0 or negative), or N2HWENC_E_BUFFERS. */
#define N2HWENC_E_BUFFERS (-8192)
static int client_open(struct n2hwenc_vt *e, const struct n2hwenc_params *use)
{
	struct n2hwenc_params p;
	uint32_t in_bytes = 0, out_bytes = 0;
	uint32_t capacity;
	int r, i;

	if (use != NULL)
		p = *use;                           /* degraded reopen (reopen_if_changed) */
	else
		fill_params(e, &p);
	e->open_attempts++;
	r = e->client.enc_open(e->client.ctx, e->conn, &p, &e->cs, &in_bytes, &out_bytes);
	if (r != 0 || e->cs == NULL) {
		if (e->cs != NULL) {
			(void)e->client.close_session(e->client.ctx, e->cs);
			e->cs = NULL;
		}
		trace(N2HWENC_TRACE_OPEN_FAILED, (uint32_t)r, 0);
		return r != 0 ? r : -1;
	}
	if (out_bytes == 0 || out_bytes > N2HWENC_MAX_OUT) {
		(void)e->client.close_session(e->client.ctx, e->cs);
		e->cs = NULL;
		trace(N2HWENC_TRACE_OPEN_FAILED, out_bytes, 0);
		return N2HWENC_E_BUFFERS;
	}
	capacity = n2hwenc_annexb_expanded(out_bytes);
	if (capacity < out_bytes) {
		(void)e->client.close_session(e->client.ctx, e->cs);
		e->cs = NULL;
		return N2HWENC_E_BUFFERS;
	}
	/* One output buffer per slot (malloc'd lazily by the system: only the bytes of an access unit get touched). */
	for (i = 0; i < N2HWENC_SLOTS; i++) {
		if (e->slots[i].cap != capacity) {
			uint8_t *b = (uint8_t *)malloc(capacity);
			if (b == NULL) {
				(void)e->client.close_session(e->client.ctx, e->cs);
				e->cs = NULL;
				return N2HWENC_E_BUFFERS;
			}
			free(e->slots[i].buf);
			e->slots[i].buf = b;
			e->slots[i].cap = capacity;
		}
	}
	e->in_capacity = out_bytes;
	e->buf_capacity = capacity;
	e->opened = p;
	e->opened_at_us = now_us();
	e->opened_ok = 1;
	trace(N2HWENC_TRACE_OPENED, in_bytes, out_bytes);
	return 0;
}

/* Connect (if needed) + OPEN, bounded.  A daemon that is down answers ENXIO at once: no retry, unless this call already
 * saw the daemon's restart answer.  BUSY (another session is closing) and the restart-on-switch answer (vcodecd exits 75
 * and launchd starts a fresh one in ~0.3 s; since vc24 it also does this for ANY open after an HEVC session, i.e. for the
 * first-picture reopen of every HEVC stream) are retried until budget_ms has passed.  The client's own timeouts bound
 * each attempt (connect 400 ms, OPEN 1 s).  e->lock held. */
static int open_with_retry(struct n2hwenc_vt *e, uint32_t budget_ms, const struct n2hwenc_params *use)
{
	uint64_t t0 = now_us();
	int r, attempt, restarting = 0;

	for (attempt = 0;; attempt++) {
		if (e->conn == NULL) {
			r = e->client.connect(e->client.ctx, &e->conn, NULL, NULL);
			if (r != 0) {
				e->conn = NULL;
				trace(N2HWENC_TRACE_CONNECT_FAILED, (uint32_t)r, (uint32_t)attempt);
				if (!restarting)
					return r;
				goto retry;
			}
			trace(N2HWENC_TRACE_CONNECTED, 0, (uint32_t)attempt);
		}
		r = client_open(e, use);
		if (r == 0)
			return 0;
		if (r != N2HWENC_CLIENT_BUSY && r != N2HWENC_CLIENT_RESTART)
			return r;
		if (r == N2HWENC_CLIENT_RESTART) {
			/* The daemon is exiting: this connection is dead. */
			restarting = 1;
			(void)e->client.disconnect(e->client.ctx, e->conn);
			e->conn = NULL;
		}
retry:
		if (now_us() - t0 >= (uint64_t)budget_ms * 1000u)
			return r;
		(void)usleep(N2HWENC_RETRY_SLEEP_US);
	}
}

int n2hwenc_vt_available(void)
{
	const struct n2hwenc_client *client = n2hwenc_client_current();
	struct n2vcd_client *conn = NULL;
	int r;

	if (client->connect == NULL)
		return 0;
	r = client->connect(client->ctx, &conn, NULL, NULL);
	if (r != 0)
		return 0;
	if (conn != NULL && client->disconnect != NULL)
		(void)client->disconnect(client->ctx, conn);
	return 1;
}

/* ------------------------------------------------------------------ format description */

static OSStatus make_format(struct n2hwenc_vt *e, const uint8_t *buf, uint32_t bytes)
{
	struct n2hwenc_nal nals[N2HWENC_NAL_MAX];
	struct n2hwenc_ps sets[N2HWENC_MAX_PS];
	const uint8_t *ps[N2HWENC_MAX_PS];
	size_t ps_size[N2HWENC_MAX_PS];
	uint32_t n = 0, i, nps = 0, pass;
	uint8_t extradata[8192];
	int extralen = -1;
	OSStatus status;
	int want[3];
	uint32_t want_count;

	if (n2hwenc_annexb_scan(buf, bytes, e->codec, nals, N2HWENC_NAL_MAX, &n) != 0)
		return N2_kVTVideoEncoderMalfunctionErr;

	if (e->codec == N2HWENC_AVC) {
		want[0] = 7;
		want[1] = 8;
		want_count = 2;
	} else {
		want[0] = 32;
		want[1] = 33;
		want[2] = 34;
		want_count = 3;
	}
	for (pass = 0; pass < want_count; pass++) {
		for (i = 0; i < n && nps < N2HWENC_MAX_PS; i++) {
			if (!nals[i].param || nals[i].type != want[pass])
				continue;
			ps[nps] = buf + nals[i].offset;
			ps_size[nps] = nals[i].size;
			sets[nps].type = (uint8_t)want[pass];
			sets[nps].data = ps[nps];
			sets[nps].bytes = nals[i].size;
			nps++;
		}
	}
	/* Required sets: AVC SPS+PPS, HEVC VPS+SPS+PPS. */
	for (pass = 0; pass < want_count; pass++) {
		uint32_t have = 0;
		for (i = 0; i < nps; i++)
			if (sets[i].type == (uint8_t)want[pass])
				have++;
		if (have == 0)
			return N2_kVTVideoEncoderMalfunctionErr;
	}

	if (e->codec == N2HWENC_AVC)
		extralen = n2hwenc_avcc_build(sets, nps, extradata, sizeof(extradata));
	else
		extralen = n2hwenc_hvcc_build(sets, nps, extradata, sizeof(extradata));
	if (extralen <= 0) {
		trace(N2HWENC_TRACE_FORMAT_FAILED, (uint32_t)extralen, nps);
		return N2_kVTVideoEncoderMalfunctionErr;
	}

	if (e->codec == N2HWENC_AVC)
		status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, nps, ps, ps_size, 4,
		                                                             &e->fmt);
	else
		status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault, nps, ps, ps_size, 4,
		                                                             NULL, &e->fmt);
	if (status != 0 || e->fmt == NULL) {
		e->fmt = NULL;
		trace(N2HWENC_TRACE_FORMAT_FAILED, (uint32_t)status, nps);
		return N2_kVTVideoEncoderMalfunctionErr;
	}
	trace(N2HWENC_TRACE_FORMAT, nps, (uint32_t)extralen);
	return 0;
}

/* ------------------------------------------------------------------ emit (VideoToolbox's calling thread only) */

static OSStatus emit_failure(struct n2hwenc_vt *e, VTVideoEncoderSession session, VTVideoEncoderFrame frame,
                             OSStatus status)
{
	pthread_mutex_lock(&e->lock);
	e->failures++;
	pthread_mutex_unlock(&e->lock);
	if (session != NULL)
		(void)VTEncoderSessionEmitEncodedFrame(session, frame, status, 0, NULL);
	return status;
}

static OSStatus emit_sample(struct n2hwenc_vt *e, VTVideoEncoderSession session, VTVideoEncoderFrame frame,
                            const uint8_t *data, uint32_t bytes, int key, const CMTime *pts, const CMTime *duration)
{
	CMTime zero = { 0, 1, N2_CMTIME_VALID, 0 };
	CMSampleTimingInfo timing;
	CMBlockBufferRef block = NULL;
	CMSampleBufferRef sample = NULL;
	CFArrayRef attachments;
	size_t sample_size = bytes;
	OSStatus status;

	if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, NULL, bytes, kCFAllocatorDefault, NULL,
	                                       0, bytes, N2_kCMBlockBufferAssureMemoryNowFlag, &block) != 0 ||
	    block == NULL) {
		if (block != NULL)
			CFRelease(block);
		return emit_failure(e, session, frame, N2_kVTVideoEncoderMalfunctionErr);
	}
	if (CMBlockBufferReplaceDataBytes(data, block, 0, bytes) != 0) {
		CFRelease(block);
		return emit_failure(e, session, frame, N2_kVTVideoEncoderMalfunctionErr);
	}

	timing.duration = (duration != NULL) ? *duration : zero;
	timing.presentationTimeStamp = (pts != NULL) ? *pts : zero;
	timing.decodeTimeStamp = timing.presentationTimeStamp;      /* no reordering: DTS = PTS */

	status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, e->fmt, 1, 1, &timing, 1, &sample_size, &sample);
	if (status != 0 || sample == NULL) {
		if (sample != NULL)
			CFRelease(sample);
		CFRelease(block);
		return emit_failure(e, session, frame, N2_kVTVideoEncoderMalfunctionErr);
	}

	attachments = CMSampleBufferGetSampleAttachmentsArray(sample, 1);
	if (attachments != NULL && CFArrayGetCount(attachments) > 0) {
		CFMutableDictionaryRef dict = (CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
		if (dict != NULL) {
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_NotSync,
			                     key ? kCFBooleanFalse : kCFBooleanTrue);
			CFDictionarySetValue(dict, kCMSampleAttachmentKey_DependsOnOthers,
			                     key ? kCFBooleanFalse : kCFBooleanTrue);
		}
	}

	status = VTEncoderSessionEmitEncodedFrame(session, frame, 0, 0, sample);
	CFRelease(sample);
	CFRelease(block);
	return status;
}

/* Turn one finished slot into its emit.  Called with emit_lock held and e->lock NOT held.  Returns the frame's status. */
static OSStatus finish_slot(struct n2hwenc_vt *e, VTVideoEncoderSession session, struct n2hwenc_slot *s)
{
	OSStatus status;
	uint32_t sample_bytes = 0, sample_nals = 0;
	int r;

	if (s->result != 0) {
		trace(N2HWENC_TRACE_FRAME_FAILED, (uint32_t)s->result, 0);
		pthread_mutex_lock(&e->lock);
		if (!e->fatal)
			N2HW_SAY("n2hwenc: ENC_FRAME failed (%d) after %llu pictures: session fatal", s->result,
			         (unsigned long long)e->frames);
		e->fatal = 1;                   /* a failed ENC_FRAME leaves the daemon session unusable */
		pthread_mutex_unlock(&e->lock);
		return emit_failure(e, session, s->frame, N2_kVTVideoEncoderMalfunctionErr);
	}
	trace(N2HWENC_TRACE_FRAME, s->bytes, (uint32_t)s->key);
	if (s->bytes == 0 || s->bytes > e->in_capacity)
		goto fatal;
	if (e->fmt == NULL && make_format(e, s->buf, s->bytes) != 0)
		goto fatal;
	r = n2hwenc_annexb_to_avcc(s->buf, s->bytes, s->cap, e->codec, 1, &sample_bytes, &sample_nals);
	if (r != 0 || sample_bytes == 0)
		goto fatal;
	status = emit_sample(e, session, s->frame, s->buf, sample_bytes, s->key, s->have_pts ? &s->pts : NULL,
	                     s->have_duration ? &s->duration : NULL);
	if (status == 0) {
		pthread_mutex_lock(&e->lock);
		e->frames++;
		e->bytes += sample_bytes;
		e->key_frames += s->key ? 1 : 0;
		pthread_mutex_unlock(&e->lock);
	}
	return status;
fatal:
	pthread_mutex_lock(&e->lock);
	e->fatal = 1;
	pthread_mutex_unlock(&e->lock);
	return emit_failure(e, session, s->frame, N2_kVTVideoEncoderMalfunctionErr);
}

enum drain_mode { DRAIN_READY, DRAIN_ONE_FREE, DRAIN_ALL };

/*
 * Emit finished frames in submission order.  DRAIN_READY emits what is done and returns; DRAIN_ONE_FREE additionally
 * waits until fewer than max_in_flight frames are outstanding; DRAIN_ALL waits until none is.  Each wait for one frame
 * is bounded by N2HWENC_WAIT_MS: when it expires the frame and every later outstanding frame get a malfunction emit,
 * their slots are marked abandoned (the worker frees them when its client call finally returns) and the session is
 * fatal.  Called with emit_lock held and e->lock NOT held.  *last_status gets the status of the last frame emitted.
 */
static void drain(struct n2hwenc_vt *e, enum drain_mode mode, OSStatus *last_status)
{
	for (;;) {
		struct n2hwenc_slot *s;
		VTVideoEncoderSession session;
		OSStatus status;
		uint32_t idx;
		uint64_t w0;

		pthread_mutex_lock(&e->lock);
		if (e->invalidated || e->count == 0) {
			pthread_mutex_unlock(&e->lock);
			return;
		}
		idx = e->head;
		s = &e->slots[idx];
		if (s->abandoned) {
			if (s->state == SLOT_DONE) {         /* the worker came back after we gave up on it */
				s->state = SLOT_FREE;
				s->abandoned = 0;
				e->head = (e->head + 1u) % N2HWENC_SLOTS;
				e->count--;
				pthread_cond_broadcast(&e->done_cond);
				pthread_mutex_unlock(&e->lock);
				continue;
			}
			/* Still inside the client and already answered with a failure: never wait for it again (the session is
			 * fatal; waiting would stall every later encodeFrame by N2HWENC_WAIT_MS). */
			pthread_mutex_unlock(&e->lock);
			return;
		}
		if (s->state != SLOT_DONE) {
			struct timespec ts;
			int timed_out = 0;

			if (mode == DRAIN_READY || (mode == DRAIN_ONE_FREE && e->count < (uint32_t)e->max_in_flight)) {
				pthread_mutex_unlock(&e->lock);
				return;
			}
			w0 = now_us();
			deadline_in(&ts, N2HWENC_WAIT_MS);
			while (s->state != SLOT_DONE && !e->invalidated && !timed_out)
				timed_out = pthread_cond_timedwait(&e->done_cond, &e->lock, &ts) != 0 && s->state != SLOT_DONE;
			if (now_us() - w0 > e->wait_us_max)
				e->wait_us_max = now_us() - w0;
			if (e->invalidated) {
				pthread_mutex_unlock(&e->lock);
				return;
			}
			if (timed_out) {
				/* Give up on every outstanding frame: one malfunction emit each, in order. */
				VTVideoEncoderFrame frames[N2HWENC_SLOTS];
				uint32_t k, n = 0;

				session = e->session;
				for (k = 0; k < e->count; k++) {
					struct n2hwenc_slot *o = &e->slots[(e->head + k) % N2HWENC_SLOTS];
					if (!o->abandoned) {
						o->abandoned = 1;
						frames[n++] = o->frame;
					}
				}
				if (!e->fatal)
					N2HW_SAY("n2hwenc: no answer from vcodecd within %u ms: %u outstanding pictures failed, session "
					         "fatal", N2HWENC_WAIT_MS, n);
				e->fatal = 1;
				pthread_mutex_unlock(&e->lock);
				for (k = 0; k < n; k++) {
					OSStatus st = emit_failure(e, session, frames[k], N2_kVTVideoEncoderMalfunctionErr);
					if (last_status != NULL)
						*last_status = st;
				}
				return;
			}
		}
		/* Head is DONE: emit it outside e->lock (the client's output callback may call back into this object). */
		s->state = SLOT_EMITTING;
		session = e->session;
		pthread_mutex_unlock(&e->lock);

		status = finish_slot(e, session, s);
		if (last_status != NULL)
			*last_status = status;

		pthread_mutex_lock(&e->lock);
		if (e->invalidated) {                    /* invalidated from inside the output callback */
			pthread_mutex_unlock(&e->lock);
			return;
		}
		s->state = SLOT_FREE;
		e->head = (e->head + 1u) % N2HWENC_SLOTS;
		e->count--;
		pthread_cond_broadcast(&e->done_cond);
		pthread_mutex_unlock(&e->lock);
		if (mode == DRAIN_ONE_FREE) {
			pthread_mutex_lock(&e->lock);
			idx = e->count;
			pthread_mutex_unlock(&e->lock);
			if (idx < (uint32_t)e->max_in_flight)
				return;
		}
	}
}

/* ------------------------------------------------------------------ the encode step (worker, or inline when synchronous) */

static void stage_plane(uint8_t *dst, uint32_t dst_pitch, const uint8_t *src, uint32_t src_pitch, uint32_t row_bytes,
                        uint32_t rows)
{
	uint32_t y;

	for (y = 0; y < rows; y++)
		memcpy(dst + (size_t)y * dst_pitch, src + (size_t)y * src_pitch, row_bytes);
}

/* Copy one picture into the daemon and run the encode.  e->lock must NOT be held.  The client (and so e->cs) is stable
 * while frames are in flight: reopen only happens with an empty pipeline, and invalidate joins the worker first. */
static void run_slot(struct n2hwenc_vt *e, struct n2hwenc_slot *s)
{
	CVPixelBufferRef pb = s->pb;
	const uint8_t *plane_y, *plane_c;
	size_t sy, sc;
	uint32_t w = (uint32_t)e->width, h = (uint32_t)e->height, stride = align16((uint32_t)e->width);
	uint32_t stride_y, stride_c, bytes = 0;
	int key = 0, staged = 0, r;
	uint64_t t0;

	s->result = -22;
	if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != 0)
		goto out;
	plane_y = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
	plane_c = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
	sy = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
	sc = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
	if (plane_y == NULL || plane_c == NULL || sy < w || sc < w || sy > 0xffffffffu || sc > 0xffffffffu) {
		(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
		goto out;
	}
	stride_y = (uint32_t)sy;
	stride_c = (uint32_t)sc;
	/* The client copies a plane in ONE RWVC_DMA_RW when its pitch equals the session stride (align16(width)) and with one
	 * ioctl per row (each with a cache clean) otherwise: 1620 ioctls per 1080p picture.  Repack such pictures here. */
	if ((stride_y != stride || stride_c != stride) && e->stage != NULL) {
		stage_plane(e->stage, stride, plane_y, stride_y, w, h);
		stage_plane(e->stage + (size_t)stride * h, stride, plane_c, stride_c, w, h / 2u);
		(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
		plane_y = e->stage;
		plane_c = e->stage + (size_t)stride * h;
		stride_y = stride_c = stride;
		staged = 1;
	}
	t0 = now_us();
	r = e->client.enc_frame(e->client.ctx, e->cs, plane_y, plane_c, stride_y, stride_c,
	                        s->have_pts ? (uint64_t)s->pts.value : 0u, s->force_key, s->buf, e->in_capacity, &bytes,
	                        &key);
	if (!staged)
		(void)CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
	s->result = r;
	s->bytes = bytes;
	s->key = key;
	pthread_mutex_lock(&e->lock);
	{
		uint64_t dt = now_us() - t0;
		e->client_us_total += dt;
		if (dt > e->client_us_max)
			e->client_us_max = dt;
		e->staged_frames += staged ? 1u : 0u;
	}
	pthread_mutex_unlock(&e->lock);
out:
	s->pb = NULL;
	CFRelease((CFTypeRef)pb);
}

static void *worker_main(void *arg)
{
	struct n2hwenc_vt *e = (struct n2hwenc_vt *)arg;

	pthread_mutex_lock(&e->lock);
	for (;;) {
		struct n2hwenc_slot *s = NULL;
		uint32_t k;

		while (!e->stop) {
			for (k = 0; k < e->count; k++) {
				struct n2hwenc_slot *c = &e->slots[(e->head + k) % N2HWENC_SLOTS];
				if (c->state == SLOT_QUEUED) {
					s = c;
					break;
				}
			}
			if (s != NULL)
				break;
			pthread_cond_wait(&e->work_cond, &e->lock);
		}
		if (e->stop)
			break;
		s->state = SLOT_RUNNING;
		if (e->fatal) {
			/* Do not feed a dead session: release the picture, the emit side reports the failure. */
			CVPixelBufferRef pb = s->pb;
			s->pb = NULL;
			s->result = -32;            /* -EPIPE */
			s->bytes = 0;
			pthread_mutex_unlock(&e->lock);
			if (pb != NULL)
				CFRelease((CFTypeRef)pb);
			pthread_mutex_lock(&e->lock);
		} else {
			pthread_mutex_unlock(&e->lock);
			run_slot(e, s);
			pthread_mutex_lock(&e->lock);
		}
		s->t_done = now_us();
		if (s->t_done - s->t_queued > e->latency_us_max)
			e->latency_us_max = s->t_done - s->t_queued;
		s->state = SLOT_DONE;
		pthread_cond_broadcast(&e->done_cond);
	}
	pthread_mutex_unlock(&e->lock);
	return NULL;
}

/* Stop and join the worker, then release every picture still queued.  e->lock NOT held. */
static void stop_pipeline(struct n2hwenc_vt *e)
{
	int i, join = 0;
	pthread_t w;

	pthread_mutex_lock(&e->lock);
	e->stop = 1;
	pthread_cond_broadcast(&e->work_cond);
	pthread_cond_broadcast(&e->done_cond);
	if (e->worker_started) {
		w = e->worker;
		e->worker_started = 0;
		join = !pthread_equal(w, pthread_self());
	}
	pthread_mutex_unlock(&e->lock);
	if (join)
		(void)pthread_join(w, NULL);
	pthread_mutex_lock(&e->lock);
	for (i = 0; i < N2HWENC_SLOTS; i++) {
		struct n2hwenc_slot *s = &e->slots[i];
		if (s->pb != NULL) {
			CFRelease((CFTypeRef)s->pb);
			s->pb = NULL;
		}
		s->state = SLOT_FREE;
		s->abandoned = 0;
	}
	e->head = e->count = 0;
	pthread_mutex_unlock(&e->lock);
}

/* ------------------------------------------------------------------ encoder class */

static OSStatus enc_start_session(VTVideoEncoderRef encoder, VTVideoEncoderSession session, CMVideoDimensions dims)
{
	struct n2hwenc_vt *e = storage((CMBaseObjectRef)encoder);
	CFMutableArrayRef formats = NULL;
	CFMutableDictionaryRef attributes = NULL;
	CFMutableDictionaryRef surface = NULL;
	CFNumberRef number = NULL;
	OSStatus status = N2_kVTAllocationFailedErr;
	uint64_t t0 = now_us();
	int r;

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
	if (formats == NULL || attributes == NULL || surface == NULL)
		goto done;

	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ (int32_t)N2_PIXFMT_420V });
	if (number == NULL)
		goto done;
	CFArrayAppendValue(formats, number);
	CFRelease(number);
	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ (int32_t)N2_PIXFMT_420F });
	if (number == NULL)
		goto done;
	CFArrayAppendValue(formats, number);
	CFRelease(number);
	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ dims.width });
	if (number == NULL)
		goto done;
	CFDictionarySetValue(attributes, kCVPixelBufferWidthKey, number);
	CFRelease(number);
	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(int32_t){ dims.height });
	if (number == NULL)
		goto done;
	CFDictionarySetValue(attributes, kCVPixelBufferHeightKey, number);
	CFRelease(number);
	number = NULL;

	CFDictionarySetValue(attributes, kCVPixelBufferPixelFormatTypeKey, formats);
	CFDictionarySetValue(attributes, kCVPixelBufferIOSurfacePropertiesKey, surface);

	/* A repeated StartSession is a fresh stream: drop the old pipeline and connection first. */
	stop_pipeline(e);
	pthread_mutex_lock(&e->lock);
	if (e->invalidated) {
		pthread_mutex_unlock(&e->lock);
		status = N2_kVTInvalidSessionErr;
		goto done;
	}
	close_client(e);
	pthread_mutex_unlock(&e->lock);
	pthread_mutex_lock(&e->emit_lock);
	if (e->fmt != NULL) {
		CFRelease(e->fmt);
		e->fmt = NULL;
	}
	pthread_mutex_unlock(&e->emit_lock);
	pthread_mutex_lock(&e->lock);
	e->stop = 0;
	e->range_fixed = 0;
	e->full_range = 0;
	e->fatal = 0;
	e->degraded = 0;
	e->frames = e->key_frames = e->bytes = 0;
	e->submitted = 0;
	e->failures = 0;
	e->pipeline_fixed = 0;
	e->said_first = 0;
	e->client_us_total = e->client_us_max = e->wait_us_max = e->latency_us_max = e->first_us = e->last_us = 0;
	e->staged_frames = e->enc_set_failures = e->open_attempts = 0;
	e->session = session;
	e->width = dims.width;
	e->height = dims.height;

	r = open_with_retry(e, N2HWENC_START_RETRY_MS, NULL);
	if (r != 0) {
		if (e->conn != NULL) {
			(void)e->client.disconnect(e->client.ctx, e->conn);
			e->conn = NULL;
		}
		e->session = NULL;
		pthread_mutex_unlock(&e->lock);
		N2HW_SAY("n2hwenc: %s %dx%d: vcodecd %s (%d) after %llu ms, VideoToolbox falls back to the next encoder",
		         e->codec == N2HWENC_HEVC ? "hevc" : "avc", (int)dims.width, (int)dims.height,
		         r == N2HWENC_CLIENT_BUSY || r == N2HWENC_CLIENT_RESTART ? "busy" : "unavailable or OPEN refused", r,
		         (unsigned long long)((now_us() - t0) / 1000u));
		status = N2_kVTVideoEncoderNotAvailableNowErr;
		goto done;
	}
	/* The repack buffer exists from the start (3 MB at 1080p): allocating it in the worker would be a late failure. */
	{
		uint32_t stride = align16((uint32_t)dims.width);
		uint64_t need = (uint64_t)stride * (uint32_t)dims.height * 3u / 2u + 64u;
		if (e->stage_bytes < need) {
			free(e->stage);
			e->stage = (uint8_t *)malloc((size_t)need);
			e->stage_bytes = e->stage != NULL ? (uint32_t)need : 0;
		}
	}
	pthread_mutex_unlock(&e->lock);
	N2HW_SAY("n2hwenc: %s %dx%d: hardware session open in %llu ms (attempts %u, bitrate %u, gop %u, profile %u level %u)",
	         e->codec == N2HWENC_HEVC ? "hevc" : "avc", (int)dims.width, (int)dims.height,
	         (unsigned long long)((now_us() - t0) / 1000u), e->open_attempts, effective_bitrate(e), effective_gop(e),
	         e->profile, e->level);

	status = VTEncoderSessionSetPixelBufferAttributes(session, attributes);
done:
	if (number != NULL)
		CFRelease(number);
	if (formats != NULL)
		CFRelease(formats);
	if (attributes != NULL)
		CFRelease(attributes);
	if (surface != NULL)
		CFRelease(surface);
	return status;
}

/* A daemon answer that says "not these parameters" (E_UNSUPPORTED, E_ARGS) or "the hardware did not take them" (E_HW),
 * as opposed to busy/restarting/absent/timeouts, where asking again with other values cannot help. */
static int reopen_refused(int r)
{
	return r == -EOPNOTSUPP || r == -EINVAL || r == -EIO;
}

/* Before the first picture: reopen if anything the OPEN carries changed since StartSession's OPEN (Camera sets its
 * properties after VTCompressionSessionCreate, i.e. after StartSession).  e->lock held, pipeline empty. */
static int reopen_if_changed(struct n2hwenc_vt *e)
{
	struct n2hwenc_params want, start;
	int r, have_start;

	fill_params(e, &want);
	/* Same parameters and opened recently: keep it.  A session opened at StartSession and left without pictures for
	 * longer than the daemon's idle limit has been dropped by vcodecd, so it is reopened as well. */
	if (e->opened_ok && memcmp(&want, &e->opened, sizeof(want)) == 0 &&
	    now_us() - e->opened_at_us < (uint64_t)N2HWENC_IDLE_REOPEN_MS * 1000u)
		return 0;
	start = e->opened;                      /* StartSession's OPEN: the one set the daemon is known to accept */
	have_start = e->opened_ok;
	close_session_only(e);
	r = open_with_retry(e, N2HWENC_REOPEN_RETRY_MS, NULL);
	if (r != 0 && reopen_refused(r)) {
		/* cam1-prod: the daemon refused the client's set (there: OPEN.flags) and the recording produced nothing, because
		 * VideoToolbox cannot change encoders after StartSession.  A refusal (not busy, not a dead daemon) is retried
		 * with less: first the client's bitrate/fps/gop with profile and level left to the daemon, then StartSession's
		 * parameters.  A recording with another profile or the default rate is better than none. */
		struct n2hwenc_params less = want;
		int first = r, step = 0;

		less.profile = less.level = 0;
		less.flags = 0;
		if (memcmp(&less, &want, sizeof(less)) != 0) {
			step = 1;
			r = open_with_retry(e, N2HWENC_REOPEN_RETRY_MS, &less);
		}
		if (r != 0 && reopen_refused(r) && have_start && memcmp(&start, &want, sizeof(start)) != 0 &&
		    (step == 0 || memcmp(&start, &less, sizeof(start)) != 0)) {
			step = 2;
			r = open_with_retry(e, N2HWENC_REOPEN_RETRY_MS, &start);
		}
		if (r == 0) {
			e->degraded = step;
			N2HW_SAY("n2hwenc: OPEN with the client's properties refused (%d; bitrate %u, fps %u/%u, gop %u, profile %u "
			         "level %u): hardware session reopened %s (bitrate %u, gop %u, profile %u level %u)", first,
			         want.bitrate, want.fps_num, want.fps_den, want.gop, want.profile, want.level,
			         step == 1 ? "without profile/level" : "with the StartSession parameters", e->opened.bitrate,
			         e->opened.gop, e->opened.profile, e->opened.level);
		}
	}
	if (r != 0)
		return r;
	trace(N2HWENC_TRACE_REOPENED, effective_bitrate(e), effective_gop(e));
	return 0;
}

static OSStatus enc_encode_frame(VTVideoEncoderRef encoder, VTVideoEncoderFrame frame, CVImageBufferRef image,
                                 const CMTime *pts, const CMTime *duration, CFDictionaryRef frameProperties,
                                 uint32_t *infoFlagsOut)
{
	struct n2hwenc_vt *e = storage((CMBaseObjectRef)encoder);
	CVPixelBufferRef pb = (CVPixelBufferRef)image;
	VTVideoEncoderSession session;
	struct n2hwenc_slot *s;
	uint32_t pixel_format;
	size_t width, height, plane_count;
	int full_range, force_key = 0, sync;
	OSStatus status = 0, bad = 0;

	if (infoFlagsOut != NULL)
		*infoFlagsOut = 0;

	pthread_mutex_lock(&e->emit_lock);
	pthread_mutex_lock(&e->lock);
	if (e->invalidated || e->session == NULL) {
		pthread_mutex_unlock(&e->lock);
		pthread_mutex_unlock(&e->emit_lock);
		return N2_kVTInvalidSessionErr;
	}
	session = e->session;

	/* Validation (no pixel access: format, size, plane count). */
	if (e->fatal) {
		bad = N2_kVTVideoEncoderMalfunctionErr;
	} else if (pb == NULL) {
		bad = N2_kVTParameterErr;
	} else {
		pixel_format = CVPixelBufferGetPixelFormatType(pb);
		width = CVPixelBufferGetWidth(pb);
		height = CVPixelBufferGetHeight(pb);
		plane_count = CVPixelBufferGetPlaneCount(pb);
		full_range = (pixel_format == N2_PIXFMT_420F);
		if (pixel_format != N2_PIXFMT_420V && pixel_format != N2_PIXFMT_420F)
			bad = N2_kVTParameterErr;   /* the VENC takes NV12 only; 'y420' would need a conversion this object does not do */
		else if (width != (size_t)e->width || height != (size_t)e->height || plane_count < 2)
			bad = N2_kVTParameterErr;
		else if (e->range_fixed && full_range != e->full_range)
			bad = N2_kVTVideoEncoderMalfunctionErr;
	}
	if (bad != 0) {
		/* Keep the emits in order: everything submitted earlier goes out first. */
		pthread_mutex_unlock(&e->lock);
		drain(e, DRAIN_ALL, &status);
		status = emit_failure(e, session, frame, bad);
		pthread_mutex_unlock(&e->emit_lock);
		return status;
	}

	if (e->submitted == 0) {
		int r;

		if (!e->pipeline_fixed) {
			e->max_in_flight = configured_in_flight();
			if (e->max_frame_delay == 0)
				e->max_in_flight = 1;            /* the client wants every frame back before encodeFrame returns */
			if (e->max_in_flight > N2HWENC_SLOTS)
				e->max_in_flight = N2HWENC_SLOTS;
			e->pipeline_fixed = 1;
		}
		r = reopen_if_changed(e);
		if (r != 0) {
			e->fatal = 1;
			pthread_mutex_unlock(&e->lock);
			N2HW_SAY("n2hwenc: reopen with the client's properties failed (%d): session fatal; bitrate %u, fps %u/%u, "
			         "gop %u, profile %u level %u, realtime %u, attempts %u", r, effective_bitrate(e), e->fps_num,
			         e->fps_den, effective_gop(e), e->profile, e->level, e->flags, e->open_attempts);
			status = emit_failure(e, session, frame, N2_kVTVideoEncoderMalfunctionErr);
			pthread_mutex_unlock(&e->emit_lock);
			return status;
		}
		if (e->max_in_flight > 1 && !e->worker_started) {
			e->stop = 0;
			if (pthread_create(&e->worker, NULL, worker_main, e) == 0)
				e->worker_started = 1;
			else
				e->max_in_flight = 1;           /* no thread: synchronous, still correct */
		}
		e->full_range = full_range;
		e->range_fixed = 1;
		e->first_us = now_us();
		force_key = 1;                          /* the probe does the same on its frame 0 (verified on the phone) */
	}
	sync = (e->max_in_flight <= 1 || !e->worker_started);
	if (frameProperties != NULL && CFGetTypeID((CFTypeRef)frameProperties) == CFDictionaryGetTypeID()) {
		CFTypeRef force = CFDictionaryGetValue(frameProperties, N2STR("ForceKeyFrame"));
		if (force != NULL && CFGetTypeID(force) == CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)force))
			force_key = 1;
	}
	pthread_mutex_unlock(&e->lock);

	/* Emit what is finished; wait (bounded) for a free slot. */
	drain(e, sync ? DRAIN_ALL : DRAIN_ONE_FREE, &status);

	pthread_mutex_lock(&e->lock);
	if (e->invalidated || e->fatal || e->count >= (uint32_t)(sync ? 1 : e->max_in_flight)) {
		int invalid = e->invalidated;
		pthread_mutex_unlock(&e->lock);
		if (invalid) {
			pthread_mutex_unlock(&e->emit_lock);
			return N2_kVTInvalidSessionErr;
		}
		status = emit_failure(e, session, frame, N2_kVTVideoEncoderMalfunctionErr);
		pthread_mutex_unlock(&e->emit_lock);
		return status;
	}
	s = &e->slots[(e->head + e->count) % N2HWENC_SLOTS];
	s->frame = frame;
	s->pb = (CVPixelBufferRef)CFRetain((CFTypeRef)pb);
	s->have_pts = pts != NULL && (pts->flags & N2_CMTIME_VALID) != 0;
	s->have_duration = duration != NULL;
	if (pts != NULL)
		s->pts = *pts;
	if (duration != NULL)
		s->duration = *duration;
	s->force_key = force_key;
	s->abandoned = 0;
	s->result = 0;
	s->bytes = 0;
	s->key = 0;
	s->t_queued = now_us();
	e->count++;
	e->submitted++;
	e->last_us = s->t_queued;
	if (!e->said_first) {
		e->first_stride_y = (uint32_t)CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
		e->first_stride_c = (uint32_t)CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
	}
	if (sync) {
		s->state = SLOT_RUNNING;
		pthread_mutex_unlock(&e->lock);
		run_slot(e, s);
		pthread_mutex_lock(&e->lock);
		s->state = SLOT_DONE;
		pthread_mutex_unlock(&e->lock);
		drain(e, DRAIN_ALL, &status);           /* emits this frame */
	} else {
		s->state = SLOT_QUEUED;
		pthread_cond_signal(&e->work_cond);
		pthread_mutex_unlock(&e->lock);
		status = 0;
		if (infoFlagsOut != NULL)
			*infoFlagsOut |= N2_kVTEncodeInfo_Asynchronous;
		drain(e, DRAIN_READY, NULL);
	}

	pthread_mutex_lock(&e->lock);
	if (!e->said_first) {
		CFTypeRef prof = CFDictionaryGetValue(e->props, N2STR("ProfileLevel"));
		char pl[64] = "-";

		if (prof != NULL && CFGetTypeID(prof) == CFStringGetTypeID())
			(void)CFStringGetCString((CFStringRef)prof, pl, (CFIndex)sizeof(pl), kCFStringEncodingUTF8);
		e->said_first = 1;
		pthread_mutex_unlock(&e->lock);
		N2HW_SAY("n2hwenc: first picture %dx%d %s pitch %u/%u (session stride %u%s): %s, bitrate %u, fps %u/%u, gop %u, "
		         "ProfileLevel %s -> profile %u level %u, realtime %u, MaxFrameDelayCount %d, in flight %d",
		         (int)e->width, (int)e->height, e->full_range ? "420f" : "420v", e->first_stride_y, e->first_stride_c,
		         align16((uint32_t)e->width),
		         (e->first_stride_y == align16((uint32_t)e->width) && e->first_stride_c == align16((uint32_t)e->width))
		             ? ", direct copy" : ", repacked",
		         e->codec == N2HWENC_HEVC ? "hevc" : "avc", effective_bitrate(e), e->fps_num, e->fps_den,
		         effective_gop(e), pl, e->profile, e->level, e->flags, (int)e->max_frame_delay, e->max_in_flight);
	} else {
		pthread_mutex_unlock(&e->lock);
	}
	pthread_mutex_unlock(&e->emit_lock);
	return status;
}

static OSStatus enc_complete_frames(VTVideoEncoderRef encoder, const CMTime *completeUntilPTS)
{
	struct n2hwenc_vt *e = storage((CMBaseObjectRef)encoder);
	OSStatus last = 0;

	(void)completeUntilPTS;                     /* no reordering: completing everything is exact */
	pthread_mutex_lock(&e->emit_lock);
	drain(e, DRAIN_ALL, &last);
	pthread_mutex_unlock(&e->emit_lock);
	return 0;
}

static OSStatus enc_copy_supported_properties(VTVideoEncoderRef encoder, CFDictionaryRef *dictOut)
{
	CFMutableDictionaryRef dict;
	CFMutableDictionaryRef entry;
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
		type_string = CFStringCreateWithCString(kCFAllocatorDefault, s_props[i].type_name, kCFStringEncodingUTF8);
		if (type_string != NULL) {
			CFDictionarySetValue(entry, N2STR("PropertyType"), type_string);
			CFRelease(type_string);
		}
		CFDictionarySetValue(entry, N2STR("ReadWriteStatus"),
		                     (s_props[i].accept & N2_PROP_RO) ? N2STR("ReadOnly") : N2STR("ReadWrite"));
		{
			CFStringRef key = prop_name_ref(i);
			if (key != NULL) {
				CFDictionarySetValue(dict, key, entry);
				CFRelease(key);
			}
		}
		CFRelease(entry);
	}

	*dictOut = dict;
	return 0;
}

/* ------------------------------------------------------------------ base class */

static OSStatus base_copy_property(CMBaseObjectRef object, CFStringRef key, CFAllocatorRef allocator, void *valueOut)
{
	struct n2hwenc_vt *e = storage(object);
	CFTypeRef value;
	CFAllocatorRef alloc = (allocator != NULL) ? allocator : kCFAllocatorDefault;

	if (key == NULL || valueOut == NULL)
		return N2_kVTParameterErr;
	if (CFGetTypeID((CFTypeRef)key) != CFStringGetTypeID())
		return N2_kVTParameterErr;

	if (key_is(key, s_props[P_USING_HW].name)) {
		*(CFTypeRef *)valueOut = CFRetain(kCFBooleanTrue);
		return 0;
	}
	if (key_is(key, s_props[P_PENDING_FRAMES].name)) {
		int32_t pending;
		CFNumberRef number;

		pthread_mutex_lock(&e->lock);
		pending = (int32_t)e->count;
		pthread_mutex_unlock(&e->lock);
		number = CFNumberCreate(alloc, kCFNumberSInt32Type, &pending);
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

/* Run-time bitrate / frame-rate change.  Before the first picture nothing is sent: the change is folded into the
 * first-picture reopen (OPEN carries both and is the path verified on the phone; ENC_SET has never run there).  After
 * it, ENC_SET is sent; a refusal is logged and NOT fatal (the stream keeps its old rate), because failing a recording
 * over a rate hint would be worse.  e->lock held. */
static void apply_bitrate_fps(struct n2hwenc_vt *e)
{
	int r;

	if (!e->opened_ok || e->cs == NULL || e->submitted == 0)
		return;
	r = e->client.enc_set(e->client.ctx, e->cs, effective_bitrate(e), e->fps_num, e->fps_den, 0);
	if (r != 0) {
		trace(N2HWENC_TRACE_ENC_SET_FAILED, (uint32_t)r, 0);
		if (e->enc_set_failures++ == 0)
			N2HW_SAY("n2hwenc: ENC_SET (bitrate %u, fps %u/%u) refused (%d): kept the previous rate",
			         effective_bitrate(e), e->fps_num, e->fps_den, r);
		return;
	}
	e->opened.bitrate = effective_bitrate(e);
	e->opened.fps_num = e->fps_num;
	e->opened.fps_den = e->fps_den;
	trace(N2HWENC_TRACE_ENC_SET, effective_bitrate(e), (e->fps_num & 0xffffu) | ((e->fps_den & 0xffffu) << 16));
}

static OSStatus base_set_property(CMBaseObjectRef object, CFStringRef key, CFTypeRef value)
{
	struct n2hwenc_vt *e = storage(object);
	int id;
	OSStatus status = 0;

	if (key == NULL || value == NULL)
		return N2_kVTParameterErr;
	if (CFGetTypeID((CFTypeRef)key) != CFStringGetTypeID())
		return N2_kVTParameterErr;

	id = prop_find(key);
	if (id < 0) {
		/* A key we do not know is kept and accepted (as n2avc_vt.c does): refusing one could stop a recording. */
		char keybuf[96];
		if (CFStringGetCString(key, keybuf, (CFIndex)sizeof(keybuf), kCFStringEncodingUTF8))
			N2HW_SAY("n2hwenc: property %s kept without effect", keybuf);
		else
			N2HW_SAY("n2hwenc: property (unprintable) kept without effect");
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
			status = N2_kVTParameterErr;
			break;
		}
		e->bitrate = (uint32_t)v;
		apply_bitrate_fps(e);
		break;
	}
	case P_EXPECTED_FRAME_RATE: {
		double fps;
		uint32_t num, den, g;
		if (number_double((CFNumberRef)value, &fps) != 0 || !(fps > 0.0) || fps > 1000.0) {
			status = N2_kVTParameterErr;
			break;
		}
		num = (uint32_t)(fps * 1000.0 + 0.5);
		den = 1000;
		if (num == 0) {
			status = N2_kVTParameterErr;
			break;
		}
		g = gcd_u32(num, den);
		e->fps_num = num / g;
		e->fps_den = den / g;
		apply_bitrate_fps(e);
		break;
	}
	case P_MAX_KEYFRAME_INTERVAL: {
		int64_t v;
		if (number_i64((CFNumberRef)value, &v) != 0 || v < 0 || v > 0xffffffffll) {
			status = N2_kVTParameterErr;
			break;
		}
		e->gop = (uint32_t)v;
		break;
	}
	case P_MAX_KEYFRAME_INTERVAL_DURATION: {
		double seconds;
		if (number_double((CFNumberRef)value, &seconds) != 0 || !(seconds >= 0.0)) {
			status = N2_kVTParameterErr;
			break;
		}
		e->keyint_duration = seconds;
		break;
	}
	case P_REAL_TIME:
		e->flags = CFBooleanGetValue((CFBooleanRef)value) != 0 ? 1u : 0u;
		break;
	case P_PROFILE_LEVEL: {
		char name[128];
		uint32_t profile = 0, level = 0;
		if (!CFStringGetCString((CFStringRef)value, name, (CFIndex)sizeof(name), kCFStringEncodingUTF8)) {
			status = N2_kVTParameterErr;
			break;
		}
		parse_profile_level(e->codec, name, &profile, &level);
		e->profile = profile;
		e->level = level;
		break;
	}
	case P_MAX_FRAME_DELAY_COUNT: {
		int64_t v;
		if (number_i64((CFNumberRef)value, &v) != 0) {
			status = N2_kVTParameterErr;
			break;
		}
		/* kVTUnlimitedFrameDelayCount is -1; any value >= our pipeline depth changes nothing. */
		e->max_frame_delay = (v < 0 || v > 1000) ? -1 : (int32_t)v;
		break;
	}
	case P_QUALITY:
	case P_NUMBER_OF_SLICES:
	case P_N2_THREADS:
	case P_DATA_RATE_LIMITS:
	case P_COLOR_PRIMARIES:
	case P_TRANSFER_FUNCTION:
	case P_YCBCR_MATRIX:
	default:
		/* Accepted and stored; the VENC's rate control has no per-slice or constant-quality controls in this protocol.
		 * Structural values (gop, profile/level, real time) take effect at the first-picture reopen; after the first
		 * picture they are stored without a restart. */
		break;
	}

	if (status == 0)
		CFDictionarySetValue(e->props, key, value);
	pthread_mutex_unlock(&e->lock);
	return status;
}

static CFStringRef base_copy_debug_description(CMBaseObjectRef object)
{
	struct n2hwenc_vt *e = storage(object);

	return CFStringCreateWithCString(kCFAllocatorDefault,
	                                 e->codec == N2HWENC_HEVC ? "n2hwenc H.265 hardware encoder"
	                                                          : "n2hwenc H.264 hardware encoder",
	                                 kCFStringEncodingUTF8);
}

static void object_teardown(struct n2hwenc_vt *e)
{
	stop_pipeline(e);
	pthread_mutex_lock(&e->emit_lock);
	if (e->fmt != NULL) {
		CFRelease(e->fmt);
		e->fmt = NULL;
	}
	pthread_mutex_unlock(&e->emit_lock);
	pthread_mutex_lock(&e->lock);
	close_client(e);
	free_buffers(e);
	e->session = NULL;
	pthread_mutex_unlock(&e->lock);
}

static OSStatus base_invalidate(CMBaseObjectRef object)
{
	struct n2hwenc_vt *e = storage(object);
	int first;

	if (!e->mutex_ok)
		return 0;
	pthread_mutex_lock(&e->lock);
	first = !e->invalidated;
	if (first) {
		uint64_t span = e->last_us > e->first_us ? e->last_us - e->first_us : 0;
		trace(N2HWENC_TRACE_INVALIDATE, (uint32_t)e->frames, (uint32_t)e->bytes);
		if (e->session != NULL || e->frames != 0)
			N2HW_SAY("n2hwenc: session %dx%d ended: %llu pictures (%llu key), %llu bytes, %u failures; client %llu "
			         "us/picture (longest %llu us), longest wait %llu us, longest latency %llu us, repacked %u, "
			         "ENC_SET refused %u, %llu ms first to last (%.1f fps submitted)",
			         (int)e->width, (int)e->height, (unsigned long long)e->frames,
			         (unsigned long long)e->key_frames, (unsigned long long)e->bytes, e->failures,
			         (unsigned long long)(e->submitted ? e->client_us_total / e->submitted : 0),
			         (unsigned long long)e->client_us_max, (unsigned long long)e->wait_us_max,
			         (unsigned long long)e->latency_us_max, e->staged_frames, e->enc_set_failures,
			         (unsigned long long)(span / 1000u),
			         span > 0 && e->submitted > 1 ? (double)(e->submitted - 1) * 1e6 / (double)span : 0.0);
		e->invalidated = 1;
	}
	pthread_mutex_unlock(&e->lock);
	if (first)
		object_teardown(e);
	return 0;
}

static void base_finalize(CMBaseObjectRef object)
{
	struct n2hwenc_vt *e = storage(object);

	if (!e->mutex_ok)
		return;
	object_teardown(e);
	pthread_mutex_lock(&e->lock);
	if (e->props != NULL) {
		CFRelease(e->props);
		e->props = NULL;
	}
	pthread_mutex_unlock(&e->lock);
	if (e->conds_ok) {
		pthread_cond_destroy(&e->work_cond);
		pthread_cond_destroy(&e->done_cond);
		e->conds_ok = 0;
	}
	if (e->emit_lock_ok) {
		pthread_mutex_destroy(&e->emit_lock);
		e->emit_lock_ok = 0;
	}
	pthread_mutex_destroy(&e->lock);
	e->mutex_ok = 0;
}

/* ------------------------------------------------------------------ vtable and factories */

static const n2_CMBaseClass s_base_class = {
	.version = 1,
	.derivedStorageSize = sizeof(struct n2hwenc_vt),
	.equal = NULL,
	.invalidate = base_invalidate,
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

static OSStatus create_common(uint32_t codecType, int codec, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	CMBaseObjectRef object = NULL;
	struct n2hwenc_vt *e;
	pthread_mutexattr_t attr;
	OSStatus status;

	if (instanceOut == NULL)
		return N2_kVTParameterErr;
	if (codec == N2HWENC_AVC) {
		if (codecType != N2_CODEC_AVC1)
			return N2_kVTParameterErr;
	} else {
		if (codecType != N2_CODEC_HVC1 && codecType != N2_CODEC_HEV1)
			return N2_kVTParameterErr;
	}

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
	if (pthread_mutexattr_init(&attr) != 0 || pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0 ||
	    pthread_mutex_init(&e->emit_lock, &attr) != 0) {
		CFRelease(object);
		return N2_kVTAllocationFailedErr;
	}
	(void)pthread_mutexattr_destroy(&attr);
	e->emit_lock_ok = 1;
	if (pthread_cond_init(&e->work_cond, NULL) != 0) {
		CFRelease(object);
		return N2_kVTAllocationFailedErr;
	}
	if (pthread_cond_init(&e->done_cond, NULL) != 0) {
		pthread_cond_destroy(&e->work_cond);
		CFRelease(object);
		return N2_kVTAllocationFailedErr;
	}
	e->conds_ok = 1;
	e->props = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                     &kCFTypeDictionaryValueCallBacks);
	if (e->props == NULL) {
		CFRelease(object);
		return N2_kVTAllocationFailedErr;
	}
	e->codec = codec;
	e->codec_type = codecType;
	e->client = *n2hwenc_client_current();
	e->fps_num = 30;
	e->fps_den = 1;
	e->max_frame_delay = -1;
	e->max_in_flight = 1;
	*instanceOut = (VTVideoEncoderRef)object;
	return 0;
}

OSStatus n2hwavc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	return create_common(codecType, N2HWENC_AVC, allocator, instanceOut);
}

OSStatus n2hwhevc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	return create_common(codecType, N2HWENC_HEVC, allocator, instanceOut);
}
