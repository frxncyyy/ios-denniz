/* SPDX-License-Identifier: MIT */
/*
 * t_fallback.c -- opus-robust (12 Nov 2026): the decoder object's software fallback and its BUSY/RESTART handling.
 *
 * VideoToolbox calls a decoder's StartSession once; a failure there fails the whole decompression session (ABI.md
 * section 5).  These checks drive the object the way VTDecompressionSessionCreateWithOptions does (factory, two base
 * SetProperty calls, StartSession) with a fake software decoder registered BEHIND the hardware one (as Apple's H264SW /
 * VCPHEVC are: rating 0 vs our 100), and a scripted daemon wrapped around t_fakeclient_dec:
 *
 *   hardware path untouched when the daemon works (no selector call, UsingHardwareAcceleratedVideoDecoder = true)
 *   OPEN refused / DEC_HEADER refused / daemon gone after the factory's probe -> the session runs on the software
 *     decoder: same VTVideoDecoderSession, every token emitted exactly once by it, FinishDelayedFrames /
 *     CanAcceptFormatDescription / copyProperty / SetProperty forwarded, the two pre-StartSession properties replayed,
 *     our own factory refuses while the selector runs (no recursion), finalize releases it (no leaked objects)
 *   BUSY twice -> retried, hardware session; BUSY forever -> software after the retry budget (timed)
 *   RESTART, then two failed connects (daemon between processes), then OK -> hardware session
 *   a second decode session while one holds the (exclusive) daemon -> software; the first keeps decoding; after both
 *     end a new session gets the hardware again (CLOSE was sent)
 *   DEC_HEADER refused after OPEN_OK -> CLOSE is sent (regression: it used to disconnect without CLOSE, leaving vcodecd
 *     BUSY until its 10 s idle drop)
 *   10-bit HEVC -> software without touching the daemon
 *   software decoder that fails too -> StartSession returns the hardware error, nothing leaks
 *   two threads: one hardware session decoding while another thread's session falls back (TSan build)
 *   dp4 (HEVC lazy configure, vendor rules: a DEC_HEADER without a slice fails): HEVC stays on the hardware with the
 *     DEC_HEADER sent at the first IRAP (parameter sets + that AU); non-IRAP samples before it are dropped; a refused
 *     header at the first IRAP, or no IRAP within 30 samples, hands the session to the software decoder INSIDE
 *     decodeFrame (CLOSE once, current frame and the properties set so far forwarded, every token emitted once); if no
 *     software decoder starts, the frame fails and nothing leaks
 */
#define _POSIX_C_SOURCE 200809L
#include "n2vtd_vt.h"
#include "n2vtd_nal.h"
#include "n2vtd_pixels.h"
#include "t_apple_dec.h"
#include "t_fakeclient_dec.h"
#include "t_support.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CODEC_AVC1 0x61766331u
#define CODEC_HVC1 0x68766331u

static uint64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Fake software decoder (stands in for H264SW / VCPHEVC: class version 3, IsHardwareAccelerated false)                */
/* ------------------------------------------------------------------------------------------------------------------ */

struct tsw {
	VTVideoDecoderSession session;
	uint32_t codec_type;
	int64_t thread_count;
};

static _Atomic int g_sw_created, g_sw_started, g_sw_decoded, g_sw_finished, g_sw_accepts, g_sw_invalidated,
    g_sw_finalized, g_sw_props, g_sw_fail_start;
static _Atomic long g_sw_thread_count;   /* value of the replayed "ThreadCount" property */
static _Atomic int g_sw_saw_realtime;

static OSStatus tsw_start(VTVideoDecoderRef dec, VTVideoDecoderSession session, CMVideoFormatDescriptionRef fmt)
{
	struct tsw *t = (struct tsw *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec);
	CFMutableDictionaryRef attrs;
	int32_t v;
	CFNumberRef n;

	(void)fmt;
	if (atomic_load(&g_sw_fail_start))
		return N2_kVTVideoDecoderMalfunctionErr;
	t->session = session;
	attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                  &kCFTypeDictionaryValueCallBacks);
	v = (int32_t)N2_PIXFMT_420V;
	n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
	CFDictionarySetValue(attrs, kCVPixelBufferPixelFormatTypeKey, n);
	CFRelease(n);
	v = 64;
	n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
	CFDictionarySetValue(attrs, kCVPixelBufferWidthKey, n);
	CFRelease(n);
	v = 48;
	n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
	CFDictionarySetValue(attrs, kCVPixelBufferHeightKey, n);
	CFRelease(n);
	(void)VTDecoderSessionSetPixelBufferAttributes(session, attrs);
	CFRelease(attrs);
	atomic_fetch_add(&g_sw_started, 1);
	return 0;
}

/* Decodes synchronously: one 64x48 picture per sample, Y[0] = pts & 0xff. */
static OSStatus tsw_decode(VTVideoDecoderRef dec, VTVideoDecoderFrame frame, CMSampleBufferRef sample,
                           uint32_t flags, uint32_t *info)
{
	struct tsw *t = (struct tsw *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec);
	CVPixelBufferRef pb = NULL;
	CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
	OSStatus st;

	(void)flags;
	if (info)
		*info = 0;
	st = VTDecoderSessionCreatePixelBuffer(t->session, NULL, &pb);
	if (st != 0 || !pb)
		return VTDecoderSessionEmitDecodedFrame(t->session, frame, st ? st : -12904, N2_kVTDecodeInfo_FrameDropped,
		                                        NULL);
	CVPixelBufferLockBaseAddress(pb, 0);
	((uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0))[0] = (uint8_t)(pts.value & 0xff);
	CVPixelBufferUnlockBaseAddress(pb, 0);
	atomic_fetch_add(&g_sw_decoded, 1);
	st = VTDecoderSessionEmitDecodedFrame(t->session, frame, 0, 0, pb);
	CFRelease(pb);
	return st;
}

static OSStatus tsw_finish(VTVideoDecoderRef dec)
{
	(void)dec;
	atomic_fetch_add(&g_sw_finished, 1);
	return 0;
}

static Boolean tsw_accept(VTVideoDecoderRef dec, CMVideoFormatDescriptionRef fmt)
{
	(void)dec;
	(void)fmt;
	atomic_fetch_add(&g_sw_accepts, 1);
	return 1;
}

static OSStatus tsw_invalidate(CMBaseObjectRef o)
{
	(void)o;
	atomic_fetch_add(&g_sw_invalidated, 1);
	return 0;
}

static void tsw_finalize(CMBaseObjectRef o)
{
	(void)o;
	atomic_fetch_add(&g_sw_finalized, 1);
}

static OSStatus tsw_copy_property(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef a, void *out)
{
	(void)o;
	(void)a;
	*(CFTypeRef *)out = NULL;
	if (key && CFEqual(key, n2vtdt_cfstr("UsingHardwareAcceleratedVideoDecoder"))) {
		*(CFTypeRef *)out = CFRetain((CFTypeRef)kCFBooleanFalse);
		return 0;
	}
	return N2_kCMBaseObjectError_ValueNotAvailable;
}

static OSStatus tsw_set_property(CMBaseObjectRef o, CFStringRef key, CFTypeRef value)
{
	int32_t v = 0;

	(void)o;
	atomic_fetch_add(&g_sw_props, 1);
	if (key && CFEqual(key, n2vtdt_cfstr("ThreadCount")) && n2vtdt_number_int32(value, &v) == 0)
		atomic_store(&g_sw_thread_count, (long)v);
	if (key && CFEqual(key, n2vtdt_cfstr("RealTime")) && value == (CFTypeRef)kCFBooleanTrue)
		atomic_store(&g_sw_saw_realtime, 1);
	return 0;
}

static const n2_CMBaseClass g_tsw_base = {
	.version = 1,
	.derivedStorageSize = sizeof(struct tsw),
	.invalidate = tsw_invalidate,
	.finalize = tsw_finalize,
	.copyProperty = tsw_copy_property,
	.setProperty = tsw_set_property,
};

static const n2_VTVideoDecoderClass g_tsw_class = {
	.version = 3,
	.startSession = tsw_start,
	.decodeFrame = tsw_decode,
	.canAcceptFormatDescription = tsw_accept,
	.finishDelayedFrames = tsw_finish,
};

static const n2_VTVideoDecoderVTable g_tsw_vtable = { NULL, &g_tsw_base, &g_tsw_class };

static OSStatus tsw_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoDecoderRef *out)
{
	CMBaseObjectRef obj = NULL;
	OSStatus st = CMDerivedObjectCreate(allocator, &g_tsw_vtable, VTVideoDecoderGetClassID(), &obj);

	if (st != 0)
		return st;
	((struct tsw *)CMBaseObjectGetDerivedStorage(obj))->codec_type = codecType;
	atomic_fetch_add(&g_sw_created, 1);
	*out = (VTVideoDecoderRef)obj;
	return 0;
}

static void sw_counters_reset(void)
{
	atomic_store(&g_sw_created, 0);
	atomic_store(&g_sw_started, 0);
	atomic_store(&g_sw_decoded, 0);
	atomic_store(&g_sw_finished, 0);
	atomic_store(&g_sw_accepts, 0);
	atomic_store(&g_sw_invalidated, 0);
	atomic_store(&g_sw_finalized, 0);
	atomic_store(&g_sw_props, 0);
	atomic_store(&g_sw_thread_count, 0);
	atomic_store(&g_sw_saw_realtime, 0);
	atomic_store(&g_sw_fail_start, 0);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Scripted daemon behaviour around t_fakeclient_dec                                                                  */
/* ------------------------------------------------------------------------------------------------------------------ */

static struct n2vtd_client g_inner;                 /* the tfd table */
static struct n2vtd_client g_wrap;
static _Atomic int w_connect_fail_all, w_connect_fail_next, w_busy_next, w_restart_next, w_restart_connect_fails,
    w_exclusive, w_connects, w_opens, w_opens_ok, w_closes;
static pthread_mutex_t w_lock = PTHREAD_MUTEX_INITIALIZER;
static void *w_open_conn[16];                       /* connections that hold an open daemon session */

static int w_open_count(void)
{
	int i, n = 0;

	pthread_mutex_lock(&w_lock);
	for (i = 0; i < 16; i++)
		if (w_open_conn[i])
			n++;
	pthread_mutex_unlock(&w_lock);
	return n;
}

static void w_mark(void *conn, int open)
{
	int i;

	pthread_mutex_lock(&w_lock);
	for (i = 0; i < 16; i++) {
		if (open && !w_open_conn[i]) {
			w_open_conn[i] = conn;
			break;
		}
		if (!open && w_open_conn[i] == conn) {
			w_open_conn[i] = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&w_lock);
}

static int w_connect(void *ctx, void **conn, const char *sp, const char *dp, uint32_t timeout_ms)
{
	atomic_fetch_add(&w_connects, 1);
	if (atomic_load(&w_connect_fail_all))
		return N2VTD_CLIENT_ABSENT;
	if (atomic_load(&w_connect_fail_next) > 0) {
		atomic_fetch_sub(&w_connect_fail_next, 1);
		return N2VTD_CLIENT_ABSENT;
	}
	return g_inner.connect(ctx, conn, sp, dp, timeout_ms);
}

static int w_open(void *ctx, void *conn, uint32_t codec, uint32_t mw, uint32_t mh, uint32_t fmt, uint32_t *in_bytes)
{
	int rc;

	atomic_fetch_add(&w_opens, 1);
	if (atomic_load(&w_restart_next) > 0) {
		atomic_fetch_sub(&w_restart_next, 1);
		/* vcodecd answers BUSY/RESTART and exits 75: the next connects fail until launchd has a new one */
		atomic_store(&w_connect_fail_next, atomic_load(&w_restart_connect_fails));
		return N2VTD_CLIENT_E_RESTART;
	}
	if (atomic_load(&w_busy_next) > 0) {
		atomic_fetch_sub(&w_busy_next, 1);
		return N2VTD_CLIENT_E_BUSY;
	}
	if (atomic_load(&w_exclusive) && w_open_count() > 0)
		return N2VTD_CLIENT_E_BUSY;
	rc = g_inner.dec_open(ctx, conn, codec, mw, mh, fmt, in_bytes);
	if (rc == 0) {
		atomic_fetch_add(&w_opens_ok, 1);
		w_mark(conn, 1);
	}
	return rc;
}

static int w_close(void *ctx, void *conn)
{
	atomic_fetch_add(&w_closes, 1);
	w_mark(conn, 0);
	return g_inner.dec_close(ctx, conn);
}

/* Install tfd with `cfg`, then the wrapper over it; reset the script. */
static void install(const struct tfd_config *cfg)
{
	tfd_install(cfg);
	g_inner = *n2vtd_client_current();
	g_wrap = g_inner;
	g_wrap.connect = w_connect;
	g_wrap.dec_open = w_open;
	g_wrap.dec_close = w_close;
	n2vtd_client_install(&g_wrap);
	atomic_store(&w_connect_fail_all, 0);
	atomic_store(&w_connect_fail_next, 0);
	atomic_store(&w_busy_next, 0);
	atomic_store(&w_restart_next, 0);
	atomic_store(&w_restart_connect_fails, 0);
	atomic_store(&w_exclusive, 0);
	atomic_store(&w_connects, 0);
	atomic_store(&w_opens, 0);
	atomic_store(&w_opens_ok, 0);
	atomic_store(&w_closes, 0);
	pthread_mutex_lock(&w_lock);
	memset(w_open_conn, 0, sizeof(w_open_conn));
	pthread_mutex_unlock(&w_lock);
	n2vtd_vt_availability_reset();
	n2vtd_vt_stats_reset();
	n2vtd_vt_set_start_retry_ms(1000);
	sw_counters_reset();
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Helpers                                                                                                            */
/* ------------------------------------------------------------------------------------------------------------------ */

static struct t_vector g_avc, g_hevc10, g_hevc;
static struct tfd_config g_cfg;

static void base_config(struct tfd_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->width = 176;
	cfg->height = 144;
	cfg->stride = 176;
	cfg->buf_h = 160;
	cfg->dpb = 4;
	cfg->format = N2VTD_FMT_MM21;
	cfg->in_bytes = 65536;
	cfg->auto_picture = 1;
	cfg->out_bytes = cfg->stride * cfg->buf_h + ((cfg->stride + 31u) & ~31u) * (cfg->buf_h / 2u);
}

static int is_hardware(struct n2vtdt_session *s)
{
	CFTypeRef v = NULL;
	int hw = -1;

	if (n2vtdt_session_copy_property(s, n2vtdt_cfstr("UsingHardwareAcceleratedVideoDecoder"), &v) == 0 && v) {
		hw = (v == (CFTypeRef)kCFBooleanTrue) ? 1 : (v == (CFTypeRef)kCFBooleanFalse) ? 0 : -1;
		CFRelease(v);
	}
	return hw;
}

/* Decode n samples with pts base+i*100; returns how many emitted with an image, every token exactly once. */
static int decode_n(struct n2vtdt_session *s, CMFormatDescriptionRef fmt, const struct t_vector *v, int n,
                    int64_t base)
{
	int i, first = n2vtdt_session_emit_count(s), good = 0;

	for (i = 0; i < n; i++) {
		CMSampleBufferRef sb = NULL;
		uint32_t info = 0;

		if (n2vtdt_sample_create(fmt, v->sample_data[i % v->samples], v->sample_bytes[i % v->samples],
		                         base + i * 100, &sb) != 0)
			return -1;
		if (n2vtdt_session_decode(s, sb, 0, &info) != 0) {
			CFRelease((CFTypeRef)sb);
			return -1;
		}
		CFRelease((CFTypeRef)sb);
	}
	for (i = first; i < n2vtdt_session_emit_count(s); i++) {
		OSStatus st = -1;
		CVImageBufferRef img = NULL;
		int tok = -1;

		if (n2vtdt_session_emitted(s, i, &st, NULL, &img, &tok) && st == 0 && img &&
		    n2vtdt_session_token_emitted(s, tok))
			good++;
	}
	return good;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Tests                                                                                                              */
/* ------------------------------------------------------------------------------------------------------------------ */

static void test_hardware_untouched(CMFormatDescriptionRef fmt)
{
	struct n2vtdt_session *s;
	OSStatus st = -1;
	int selects = n2vtdt_select_calls();

	install(&g_cfg);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(s != NULL);
	if (!s)
		return;
	CHECK_EQ(is_hardware(s), 1);
	CHECK_EQ(n2vtdt_select_calls(), selects);          /* no fallback when the daemon works */
	CHECK_EQ(atomic_load(&g_sw_created), 0);
	CHECK_EQ(decode_n(s, fmt, &g_avc, 3, 1000), 3);
	CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
	CHECK_EQ(atomic_load(&w_opens_ok), 1);
	CHECK_EQ(n2vtd_vt_fallback_in_progress(), 0);
	n2vtdt_session_destroy(s);
	CHECK_EQ(w_open_count(), 0);                        /* CLOSE at finalize */
}

static void test_open_refused(CMFormatDescriptionRef fmt)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	OSStatus st = -1;
	int selects;
	long live0 = n2vtdt_live_objects();
	static const char *const keys[] = { "ThreadCount", "RealTime" };
	int32_t four = 4;
	CFNumberRef n4 = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &four);
	CFTypeRef values[2] = { n4, (CFTypeRef)kCFBooleanTrue };

	cfg.open_fail = 1;
	install(&cfg);
	selects = n2vtdt_select_calls();
	s = n2vtdt_session_create_props(CODEC_AVC1, fmt, keys, values, 2, &st);
	CHECK(s != NULL);
	CHECK_EQ(st, 0);
	if (!s) {
		CFRelease(n4);
		return;
	}
	CHECK_EQ(n2vtdt_select_calls(), selects + 1);
	CHECK_EQ(atomic_load(&g_sw_created), 1);            /* our factory refused inside the selector: no recursion */
	CHECK_EQ(atomic_load(&g_sw_started), 1);
	CHECK_EQ(atomic_load(&g_sw_props), 2);              /* VT's two pre-StartSession properties replayed */
	CHECK_EQ(atomic_load(&g_sw_thread_count), 4);
	CHECK_EQ(atomic_load(&g_sw_saw_realtime), 1);
	CHECK_EQ(is_hardware(s), 0);
	CHECK_EQ(n2vtd_vt_stats()->sw_fallbacks, 1u);
	CHECK_EQ(atomic_load(&w_opens), 1);                 /* a refusal (not BUSY) is not retried */
	CHECK_EQ(atomic_load(&w_opens_ok), 0);
	CHECK_EQ(decode_n(s, fmt, &g_avc, 5, 2000), 5);
	CHECK_EQ(atomic_load(&g_sw_decoded), 5);
	CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
	{
		CVImageBufferRef img = NULL;
		CHECK(n2vtdt_session_emitted(s, 0, NULL, NULL, &img, NULL));
		CHECK(img && n2vtdt_pixel_plane(img, 0)[0] == (uint8_t)(2000 & 0xff));   /* the software decoder's picture */
	}
	CHECK_EQ(n2vtdt_session_finish(s), 0);
	CHECK_EQ(atomic_load(&g_sw_finished), 1);
	CHECK_EQ(n2vtdt_session_can_accept(s, fmt), 0);
	CHECK_EQ(atomic_load(&g_sw_accepts), 1);
	CHECK_EQ(n2vtdt_session_set_properties(s, NULL), N2_kVTUnimplementedErr);   /* no slot -> base per key */
	CHECK_EQ(n2vtdt_session_base(s)->setProperty(n2vtdt_session_object(s), n2vtdt_cfstr("ThreadCount"), n4), 0);
	CHECK_EQ(atomic_load(&g_sw_props), 3);              /* after StartSession: forwarded */
	CHECK(n2vtdt_log_contains("session runs on the software decoder"));
	CHECK_EQ(n2vtdt_session_base(s)->invalidate(n2vtdt_session_object(s)), 0);
	CHECK_EQ(atomic_load(&g_sw_invalidated), 1);
	n2vtdt_session_destroy(s);
	CHECK_EQ(atomic_load(&g_sw_invalidated), 1);        /* not twice */
	CHECK_EQ(atomic_load(&g_sw_finalized), 1);
	CFRelease(n4);
	CHECK_EQ(n2vtdt_live_objects(), live0);
}

static void test_daemon_gone_after_probe(CMFormatDescriptionRef fmt)
{
	struct n2vtdt_session *s;
	OSStatus st = -1;
	uint64_t t0;

	install(&g_cfg);
	CHECK_EQ(n2vtd_vt_available(), 1);                  /* the factory's probe succeeds and is cached for 2 s */
	atomic_store(&w_connect_fail_all, 1);               /* ... then vcodecd dies */
	atomic_store(&w_connects, 0);
	t0 = now_ms();
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(s != NULL);
	CHECK(now_ms() - t0 < 300u);                        /* an absent daemon is not retried */
	CHECK_EQ(atomic_load(&w_connects), 1);
	if (s) {
		CHECK_EQ(is_hardware(s), 0);
		CHECK_EQ(decode_n(s, fmt, &g_avc, 2, 3000), 2);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&g_sw_finalized), 1);
}

static void test_busy(CMFormatDescriptionRef fmt)
{
	struct n2vtdt_session *s;
	OSStatus st = -1;
	uint64_t t0, dt;

	/* BUSY twice (another session closing): retried, then the hardware. */
	install(&g_cfg);
	atomic_store(&w_busy_next, 2);
	t0 = now_ms();
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	dt = now_ms() - t0;
	CHECK(s != NULL);
	if (s) {
		CHECK_EQ(is_hardware(s), 1);
		CHECK_EQ(decode_n(s, fmt, &g_avc, 2, 4000), 2);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&w_opens), 3);
	CHECK_EQ(n2vtd_vt_stats()->open_retries, 2u);
	CHECK(dt >= 150u && dt < 1000u);
	CHECK_EQ(atomic_load(&g_sw_created), 0);
	CHECK(n2vtdt_log_contains("hardware session open after 3 attempts"));

	/* BUSY for longer than the budget: software after ~budget. */
	install(&g_cfg);
	n2vtd_vt_set_start_retry_ms(300);
	atomic_store(&w_busy_next, 1000);
	t0 = now_ms();
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	dt = now_ms() - t0;
	CHECK(s != NULL);
	CHECK(dt >= 300u && dt < 900u);
	CHECK(atomic_load(&w_opens) >= 3);
	if (s) {
		CHECK_EQ(is_hardware(s), 0);
		CHECK_EQ(decode_n(s, fmt, &g_avc, 2, 5000), 2);
		n2vtdt_session_destroy(s);
	}
	CHECK(n2vtdt_log_contains("daemon busy"));

	/* Budget 0: BUSY falls back at once. */
	install(&g_cfg);
	n2vtd_vt_set_start_retry_ms(0);
	atomic_store(&w_busy_next, 1000);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(s != NULL);
	CHECK_EQ(atomic_load(&w_opens), 1);
	if (s) {
		CHECK_EQ(is_hardware(s), 0);
		n2vtdt_session_destroy(s);
	}
}

static void test_restart(CMFormatDescriptionRef fmt)
{
	struct n2vtdt_session *s;
	OSStatus st = -1;

	/* RESTART (vcodecd exits 75), two connects refused while launchd respawns it, then a fresh daemon. */
	install(&g_cfg);
	atomic_store(&w_restart_next, 1);
	atomic_store(&w_restart_connect_fails, 2);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(s != NULL);
	if (s) {
		CHECK_EQ(is_hardware(s), 1);
		CHECK_EQ(decode_n(s, fmt, &g_avc, 2, 6000), 2);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&w_connects), 5);              /* probe 1 + open 1 + 2 refused while restarting + 1 good */
	CHECK_EQ(atomic_load(&w_opens), 2);
	CHECK_EQ(atomic_load(&g_sw_created), 0);
}

static void test_second_session(CMFormatDescriptionRef fmt)
{
	struct n2vtdt_session *a, *b, *c;
	OSStatus st = -1;

	install(&g_cfg);
	atomic_store(&w_exclusive, 1);                      /* vcodecd: one codec session at a time */
	n2vtd_vt_set_start_retry_ms(300);
	a = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(a != NULL);
	if (!a)
		return;
	CHECK_EQ(is_hardware(a), 1);
	b = n2vtdt_session_create(CODEC_AVC1, fmt, &st);   /* e.g. a thumbnail while a movie plays */
	CHECK(b != NULL);
	if (b) {
		CHECK_EQ(is_hardware(b), 0);
		CHECK_EQ(decode_n(b, fmt, &g_avc, 3, 7000), 3);
		CHECK_EQ(decode_n(a, fmt, &g_avc, 3, 8000), 3);   /* the first session is unaffected */
		CHECK_EQ(n2vtdt_session_protocol_errors(a), 0);
		CHECK_EQ(n2vtdt_session_protocol_errors(b), 0);
		n2vtdt_session_destroy(b);
	}
	n2vtdt_session_destroy(a);
	CHECK_EQ(w_open_count(), 0);
	c = n2vtdt_session_create(CODEC_AVC1, fmt, &st);   /* the daemon is free again */
	CHECK(c != NULL);
	if (c) {
		CHECK_EQ(is_hardware(c), 1);
		n2vtdt_session_destroy(c);
	}
}

static void test_header_refused(CMFormatDescriptionRef fmt)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	OSStatus st = -1;

	cfg.header_fail = 1;                                /* e.g. a size/profile the vendor refuses at DEC_HEADER */
	install(&cfg);
	atomic_store(&w_exclusive, 1);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(s != NULL);
	CHECK_EQ(atomic_load(&w_opens_ok), 1);
	CHECK_EQ(atomic_load(&w_closes), 1);                /* CLOSE was sent although StartSession failed after OPEN_OK */
	CHECK_EQ(w_open_count(), 0);
	if (s) {
		CHECK_EQ(is_hardware(s), 0);
		CHECK_EQ(decode_n(s, fmt, &g_avc, 2, 9000), 2);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&w_closes), 1);                /* and not again at finalize */
	CHECK(n2vtdt_log_contains("DEC_HEADER/pool refused"));
}

/* dp1-fix (phone dp1, reproduced with the vendor objects under decint): the phone's vendor decoder displays nothing
 * unless the SPS/PPS travel in-band with the first frame, and refuses an HEVC DEC_HEADER that holds no slice.  With
 * the fake behaving like that: H.264 stays on the hardware and every frame is displayed (the parameter sets are
 * prepended once).  dp4: HEVC stays on the hardware too (before dp4 it went to the software decoder: phone dp3
 * "DEC_HEADER/pool refused (-12911)"): the DEC_HEADER waits for the first IRAP and carries it. */
static void test_vendor_rules(CMFormatDescriptionRef fmt)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	CMFormatDescriptionRef hfmt = NULL;
	OSStatus st = -1;

	cfg.vendor_rules = 1;
	install(&cfg);
	n2vtd_vt_stats_reset();
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(s != NULL);
	if (s) {
		CHECK_EQ(is_hardware(s), 1);
		CHECK_EQ(decode_n(s, fmt, &g_avc, 5, 11000), 5);   /* before the fix: 0 (all FrameDropped, status 0) */
		CHECK(tfd_last_conn() != NULL && tfd_inputs_dropped(tfd_last_conn()) == 0);
		CHECK(n2vtd_vt_stats()->params_inband >= 1);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(w_open_count(), 0);

	install(&cfg);
	atomic_store(&w_exclusive, 1);
	CHECK_EQ(n2vtdt_format_with_atoms(g_hevc.codec, g_hevc.record, g_hevc.record_bytes, &hfmt), 0);
	n2vtdt_log_reset();
	s = n2vtdt_session_create(CODEC_HVC1, hfmt, &st);
	CHECK(s != NULL);
	CHECK_EQ(atomic_load(&w_opens_ok), 1);
	CHECK_EQ(atomic_load(&w_closes), 0);
	CHECK_EQ(w_open_count(), 1);
	if (s) {
		struct tfd_conn *c = tfd_last_conn();

		CHECK(c != NULL);
		CHECK_EQ(tfd_header_bytes(c), 0u);                 /* no DEC_HEADER at StartSession */
		CHECK_EQ(is_hardware(s), 1);
		CHECK_EQ(decode_n(s, hfmt, &g_hevc, 5, 12000), 5);  /* before dp4: software (prod5, phone dp3) */
		CHECK_EQ(is_hardware(s), 1);
		CHECK(c && tfd_header_bytes(c) > g_hevc.sample_bytes[0]);   /* parameter sets + the IRAP */
		CHECK(c && tfd_inputs(c) == 5 && tfd_inputs_dropped(c) == 0);
		CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
		CHECK_EQ(n2vtd_vt_stats()->headers_deferred, 1u);
		CHECK_EQ(n2vtd_vt_stats()->headers_with_slice, 1u);
		CHECK_EQ(n2vtd_vt_stats()->sw_handovers, 0u);
		CHECK_EQ(atomic_load(&g_sw_created), 0);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&w_closes), 1);
	CHECK_EQ(w_open_count(), 0);
	CHECK(!n2vtdt_log_contains("DEC_HEADER/pool refused"));
	CHECK(n2vtdt_log_contains("DEC_HEADER waits for the first IRAP"));
	CHECK(n2vtdt_log_contains("session configured at the first IRAP"));
	if (hfmt)
		CFRelease((CFTypeRef)hfmt);
}

/* dp4: decode one HEVC sample (index `k` of g_hevc) with pts; returns decodeFrame's status. */
static OSStatus hevc_one(struct n2vtdt_session *s, CMFormatDescriptionRef fmt, int k, int64_t pts)
{
	CMSampleBufferRef sb = NULL;
	uint32_t info = 0;
	OSStatus st;

	if (n2vtdt_sample_create(fmt, g_hevc.sample_data[k], g_hevc.sample_bytes[k], pts, &sb) != 0)
		return -1;
	st = n2vtdt_session_decode(s, sb, 0, &info);
	CFRelease((CFTypeRef)sb);
	return st;
}

/* dp4: count emitted entries [from, end) that carry FrameDropped and no image, each token once. */
static int dropped_since(struct n2vtdt_session *s, int from)
{
	int i, n = 0;

	for (i = from; i < n2vtdt_session_emit_count(s); i++) {
		OSStatus st = -1;
		uint32_t fl = 0;
		CVImageBufferRef img = NULL;
		int tok = -1;

		if (n2vtdt_session_emitted(s, i, &st, &fl, &img, &tok) && st == 0 && !img &&
		    (fl & N2_kVTDecodeInfo_FrameDropped) && n2vtdt_session_token_emitted(s, tok))
			n++;
	}
	return n;
}

/* dp4: the first samples are not IRAPs (a seek that lands after the sync sample, an open-GOP cut): they are dropped
 * (emitted with FrameDropped, no DEC_HEADER, no INPUT) until the IRAP, which then configures the hardware. */
static void test_hevc_skip_to_irap(void)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	CMFormatDescriptionRef hfmt = NULL;
	OSStatus st = -1;

	cfg.vendor_rules = 1;
	install(&cfg);
	CHECK_EQ(n2vtdt_format_with_atoms(g_hevc.codec, g_hevc.record, g_hevc.record_bytes, &hfmt), 0);
	s = n2vtdt_session_create(CODEC_HVC1, hfmt, &st);
	CHECK(s != NULL);
	if (s) {
		struct tfd_conn *c = tfd_last_conn();

		CHECK_EQ(hevc_one(s, hfmt, 1, 100), 0);
		CHECK_EQ(hevc_one(s, hfmt, 2, 200), 0);
		CHECK_EQ(dropped_since(s, 0), 2);
		CHECK(c && tfd_header_bytes(c) == 0 && tfd_inputs(c) == 0);
		CHECK_EQ(n2vtd_vt_stats()->pre_irap_dropped, 2u);
		CHECK_EQ(decode_n(s, hfmt, &g_hevc, 5, 300), 5);   /* sample 0 = IDR first */
		CHECK(c && tfd_header_bytes(c) > 0 && tfd_inputs(c) == 5);
		CHECK_EQ(is_hardware(s), 1);
		CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
		CHECK_EQ(n2vtdt_session_finish(s), 0);
		CHECK(n2vtdt_log_contains("first sample is not an IRAP"));
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&g_sw_created), 0);
	CHECK_EQ(w_open_count(), 0);
	if (hfmt)
		CFRelease((CFTypeRef)hfmt);
}

/* dp4: DEC_HEADER refused at the first IRAP (a profile/size the vendor refuses even with the slice): the session is
 * handed to the software decoder inside decodeFrame.  CLOSE once, that frame and the following ones decoded by the
 * software decoder (every token once), the two pre-StartSession properties AND one set after StartSession replayed. */
static void test_hevc_header_refused_at_first_frame(void)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	CMFormatDescriptionRef hfmt = NULL;
	OSStatus st = -1;
	long live0 = n2vtdt_live_objects();
	static const char *const keys[] = { "ThreadCount", "Other" };
	int32_t four = 4;
	CFNumberRef n4 = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &four);
	CFTypeRef values[2] = { n4, (CFTypeRef)kCFBooleanFalse };

	cfg.vendor_rules = 1;
	cfg.header_fail = 1;
	install(&cfg);
	CHECK_EQ(n2vtdt_format_with_atoms(g_hevc.codec, g_hevc.record, g_hevc.record_bytes, &hfmt), 0);
	n2vtdt_log_reset();
	s = n2vtdt_session_create_props(CODEC_HVC1, hfmt, keys, values, 2, &st);
	CHECK(s != NULL);
	CHECK_EQ(st, 0);
	if (s) {
		CHECK_EQ(is_hardware(s), 1);
		CHECK_EQ(atomic_load(&w_closes), 0);
		/* after StartSession, before the first frame (VT does this for RealTime) */
		CHECK_EQ(n2vtdt_session_base(s)->setProperty(n2vtdt_session_object(s), n2vtdt_cfstr("RealTime"),
		                                             (CFTypeRef)kCFBooleanTrue), 0);
		CHECK_EQ(atomic_load(&g_sw_props), 0);
		CHECK_EQ(decode_n(s, hfmt, &g_hevc, 4, 500), 4);
		CHECK_EQ(is_hardware(s), 0);
		CHECK_EQ(atomic_load(&w_closes), 1);
		CHECK_EQ(w_open_count(), 0);
		CHECK_EQ(atomic_load(&g_sw_created), 1);
		CHECK_EQ(atomic_load(&g_sw_started), 1);
		CHECK_EQ(atomic_load(&g_sw_decoded), 4);        /* including the frame whose header failed */
		CHECK_EQ(atomic_load(&g_sw_props), 3);
		CHECK_EQ(atomic_load(&g_sw_thread_count), 4);
		CHECK_EQ(atomic_load(&g_sw_saw_realtime), 1);
		CHECK_EQ(n2vtdt_session_emit_count(s), 4);
		CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
		CHECK_EQ(n2vtd_vt_stats()->sw_handovers, 1u);
		{
			CVImageBufferRef img = NULL;
			CHECK(n2vtdt_session_emitted(s, 0, NULL, NULL, &img, NULL));
			CHECK(img && n2vtdt_pixel_plane(img, 0)[0] == (uint8_t)(500 & 0xff));
		}
		CHECK_EQ(n2vtdt_session_finish(s), 0);
		CHECK_EQ(atomic_load(&g_sw_finished), 1);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&w_closes), 1);
	CHECK_EQ(atomic_load(&g_sw_finalized), 1);
	CHECK(n2vtdt_log_contains("DEC_HEADER/pool refused at the first IRAP"));
	CHECK(n2vtdt_log_contains("session runs on the software decoder"));
	if (hfmt)
		CFRelease((CFTypeRef)hfmt);
	CFRelease(n4);
	CHECK_EQ(n2vtdt_live_objects(), live0);
}

/* dp4: no IRAP within the first 30 samples: the 31st goes to the software decoder (dropping forever would show
 * nothing; a software decoder may recover, e.g. intra refresh). */
static void test_hevc_no_irap(void)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	CMFormatDescriptionRef hfmt = NULL;
	OSStatus st = -1;
	int i;

	cfg.vendor_rules = 1;
	install(&cfg);
	CHECK_EQ(n2vtdt_format_with_atoms(g_hevc.codec, g_hevc.record, g_hevc.record_bytes, &hfmt), 0);
	s = n2vtdt_session_create(CODEC_HVC1, hfmt, &st);
	CHECK(s != NULL);
	if (s) {
		struct tfd_conn *c = tfd_last_conn();

		for (i = 0; i < 30; i++)
			CHECK_EQ(hevc_one(s, hfmt, 1 + (i % 4), 1000 + i), 0);
		CHECK_EQ(dropped_since(s, 0), 30);
		CHECK(c && tfd_header_bytes(c) == 0 && tfd_inputs(c) == 0);
		CHECK_EQ(is_hardware(s), 1);
		CHECK_EQ(hevc_one(s, hfmt, 1, 1030), 0);
		CHECK_EQ(is_hardware(s), 0);
		CHECK_EQ(atomic_load(&g_sw_decoded), 1);
		CHECK_EQ(atomic_load(&w_closes), 1);
		CHECK_EQ(n2vtdt_session_emit_count(s), 31);
		CHECK_EQ(decode_n(s, hfmt, &g_hevc, 3, 2000), 3);
		CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
		CHECK_EQ(n2vtd_vt_stats()->pre_irap_dropped, 31u);
		CHECK_EQ(n2vtd_vt_stats()->sw_handovers, 1u);
		n2vtdt_session_destroy(s);
	}
	CHECK(n2vtdt_log_contains("no IRAP in the first samples"));
	if (hfmt)
		CFRelease((CFTypeRef)hfmt);
}

/* dp4: header refused at the first IRAP and the software decoder fails to start too: that frame fails with the
 * hardware error, later frames with NotAvailableNow; CLOSE once; nothing leaks. */
static void test_hevc_handover_fails(void)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	CMFormatDescriptionRef hfmt = NULL;
	OSStatus st = -1;
	long live0 = n2vtdt_live_objects();

	cfg.vendor_rules = 1;
	cfg.header_fail = 1;
	install(&cfg);
	CHECK_EQ(n2vtdt_format_with_atoms(g_hevc.codec, g_hevc.record, g_hevc.record_bytes, &hfmt), 0);
	s = n2vtdt_session_create(CODEC_HVC1, hfmt, &st);
	CHECK(s != NULL);
	if (s) {
		atomic_store(&g_sw_fail_start, 1);
		CHECK(hevc_one(s, hfmt, 0, 100) != 0);
		CHECK_EQ(hevc_one(s, hfmt, 1, 200), N2_kVTVideoDecoderNotAvailableNowErr);
		CHECK_EQ(n2vtdt_session_emit_count(s), 1);         /* the failed frame, once, with the error */
		{
			OSStatus est = 0;
			CVImageBufferRef img = (CVImageBufferRef)1;
			CHECK(n2vtdt_session_emitted(s, 0, &est, NULL, &img, NULL));
			CHECK(est != 0 && img == NULL);
		}
		CHECK_EQ(n2vtdt_session_protocol_errors(s), 0);
		CHECK_EQ(atomic_load(&w_closes), 1);
		CHECK_EQ(atomic_load(&g_sw_created), 1);
		CHECK_EQ(atomic_load(&g_sw_finalized), 1);
		CHECK_EQ(n2vtd_vt_stats()->sw_fallback_failed, 1u);
		CHECK_EQ(n2vtdt_session_finish(s), 0);
		n2vtdt_session_destroy(s);
	}
	CHECK_EQ(atomic_load(&w_closes), 1);
	if (hfmt)
		CFRelease((CFTypeRef)hfmt);
	CHECK_EQ(n2vtdt_live_objects(), live0);
}

static void test_unsupported_stream(void)
{
	CMFormatDescriptionRef fmt = NULL;
	struct n2vtdt_session *s;
	OSStatus st = -1;

	install(&g_cfg);
	CHECK_EQ(n2vtdt_format_with_atoms(g_hevc10.codec, g_hevc10.record, g_hevc10.record_bytes, &fmt), 0);
	s = n2vtdt_session_create(CODEC_HVC1, fmt, &st);
	CHECK(s != NULL);
	CHECK_EQ(atomic_load(&w_opens), 0);                 /* the daemon is not even asked */
	if (s) {
		CHECK_EQ(is_hardware(s), 0);
		CHECK_EQ(decode_n(s, fmt, &g_hevc10, 2, 100), 2);   /* previously: -12910 for every frame */
		n2vtdt_session_destroy(s);
	}
	CHECK(n2vtdt_log_contains("stream format"));
	CFRelease((CFTypeRef)fmt);
}

static void test_software_fails_too(CMFormatDescriptionRef fmt)
{
	struct tfd_config cfg = g_cfg;
	struct n2vtdt_session *s;
	OSStatus st = 0;
	long live0 = n2vtdt_live_objects();

	cfg.open_fail = 1;
	install(&cfg);
	atomic_store(&g_sw_fail_start, 1);
	s = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(s == NULL);
	CHECK_EQ(st, N2_kVTVideoDecoderMalfunctionErr);     /* the hardware error, as before the fallback existed */
	CHECK_EQ(atomic_load(&g_sw_created), 1);
	CHECK_EQ(atomic_load(&g_sw_invalidated), 1);
	CHECK_EQ(atomic_load(&g_sw_finalized), 1);
	CHECK_EQ(n2vtd_vt_stats()->sw_fallback_failed, 1u);
	CHECK_EQ(n2vtdt_live_objects(), live0);
	if (s)
		n2vtdt_session_destroy(s);
}

/* Two threads: thread 1 holds the hardware session and decodes while thread 2's session falls back. */
static struct {
	CMFormatDescriptionRef fmt;
	struct n2vtdt_session *hw;
	int ok_hw, ok_sw;
} g_thr;

static void *thr_hw(void *arg)
{
	(void)arg;
	g_thr.ok_hw = decode_n(g_thr.hw, g_thr.fmt, &g_avc, 20, 10000) == 20;
	return NULL;
}

static void *thr_sw(void *arg)
{
	struct n2vtdt_session *s;
	OSStatus st = -1;

	(void)arg;
	s = n2vtdt_session_create(CODEC_AVC1, g_thr.fmt, &st);
	if (!s)
		return NULL;
	g_thr.ok_sw = is_hardware(s) == 0 && decode_n(s, g_thr.fmt, &g_avc, 20, 20000) == 20 &&
	              n2vtdt_session_protocol_errors(s) == 0;
	n2vtdt_session_destroy(s);
	return NULL;
}

static void test_threads(CMFormatDescriptionRef fmt)
{
	pthread_t t1, t2;
	OSStatus st = -1;

	install(&g_cfg);
	atomic_store(&w_exclusive, 1);
	n2vtd_vt_set_start_retry_ms(200);
	memset(&g_thr, 0, sizeof(g_thr));
	g_thr.fmt = fmt;
	g_thr.hw = n2vtdt_session_create(CODEC_AVC1, fmt, &st);
	CHECK(g_thr.hw != NULL);
	if (!g_thr.hw)
		return;
	pthread_create(&t1, NULL, thr_hw, NULL);
	pthread_create(&t2, NULL, thr_sw, NULL);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	CHECK(g_thr.ok_hw);
	CHECK(g_thr.ok_sw);
	CHECK_EQ(n2vtdt_session_protocol_errors(g_thr.hw), 0);
	n2vtdt_session_destroy(g_thr.hw);
}

static char g_swdir[128];

int main(void)
{
	CMFormatDescriptionRef fmt = NULL;
	char err[128], p[192];

	printf("t_fallback: software fallback at StartSession, BUSY/RESTART retry, second session, CLOSE after refusal\n");
	if (t_vector_load("/tmp/n2hw-DEC06/vectors/avc-high.vec", &g_avc, err, sizeof(err)) != 0 ||
	    t_vector_load("/tmp/n2hw-DEC06/vectors/hevc-main10.vec", &g_hevc10, err, sizeof(err)) != 0 ||
	    t_vector_load("/tmp/n2hw-DEC06/vectors/hevc-main.vec", &g_hevc, err, sizeof(err)) != 0) {
		fprintf(stderr, "t_fallback: %s\n", err);
		return 1;
	}
	snprintf(g_swdir, sizeof(g_swdir), "/tmp/n2hw-DEC06/sw-fallback");
	mkdir("/tmp/n2hw-DEC06", 0777);
	mkdir(g_swdir, 0777);
	snprintf(p, sizeof(p), "%s/nord2-video-hwdec", g_swdir);
	fclose(fopen(p, "w"));
	n2vtd_register_set_switch_dir(g_swdir);
	n2vtdt_log_reset();
	n2vtdt_vt_reset();
	/* Registration order = VT's sort: ours (rating 100, hardware) before the software decoders (rating 0). */
	CHECK_EQ(n2vtd_vt_register_all(), 2);
	CHECK_EQ(VTRegisterVideoDecoderWithInfo(CODEC_AVC1, NULL, tsw_create), 0);
	CHECK_EQ(VTRegisterVideoDecoderWithInfo(CODEC_HVC1, NULL, tsw_create), 0);
	base_config(&g_cfg);
	CHECK_EQ(n2vtdt_format_with_atoms(g_avc.codec, g_avc.record, g_avc.record_bytes, &fmt), 0);

	test_hardware_untouched(fmt);
	test_open_refused(fmt);
	test_daemon_gone_after_probe(fmt);
	test_busy(fmt);
	test_restart(fmt);
	test_second_session(fmt);
	test_header_refused(fmt);
	test_vendor_rules(fmt);
	test_hevc_skip_to_irap();
	test_hevc_header_refused_at_first_frame();
	test_hevc_no_irap();
	test_hevc_handover_fails();
	test_unsupported_stream();
	test_software_fails_too(fmt);
	test_threads(fmt);

	CFRelease((CFTypeRef)fmt);
	t_vector_free(&g_avc);
	t_vector_free(&g_hevc10);
	t_vector_free(&g_hevc);
	return t_summary("FALLBACK");
}
