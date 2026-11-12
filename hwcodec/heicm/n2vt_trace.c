/* SPDX-License-Identifier: MIT */
/*
 * n2vt_trace.c -- a logging proxy around a VideoToolbox video-encoder object (opus-jpeg, 12 Nov 2026).  Measurement only.
 *
 * Question it answers on the phone: when CMPhoto encodes a HEIC still (Camera, ImageIO/Photos export), what does it ask the
 * 'hvc1' encoder (today Apple's VCPHEVC software encoder, registered by gpu/video's n2vt_shim.c)?  Frame session or tile
 * session, which sizes, how many frames/tiles, which properties with which values, which frame properties.
 *
 * How: n2vt_shim.c calls the weak n2vt_trace_wrap(codec, factory) before registering the HEVC factory.  While the switch file
 * /private/var/db/nord2-video-trace exists, it returns n2vt_trace_create instead: that creates the real encoder with the real
 * factory and wraps it in a CMDerivedObject of the same class whose vtable is a COPY of the real one with logging forwarders
 * in every non-NULL slot.  Everything is forwarded unchanged with the real object as `self`; the session token the real
 * encoder gets in StartSession is VideoToolbox's own, so its Emit* calls go straight to VideoToolbox.  Without the switch
 * file nothing changes (the factory is returned as is).
 *
 * Log: one line per call to /private/var/db/nord2-video/trace/<prog>-<pid>.log (directory 1777, made by the guest; opened
 * O_APPEND per line, so a crash loses nothing and no descriptor is kept in mediaserverd), at most N2T_MAX_LINES lines per
 * process, plus the first N2T_CONSOLE lines also on the N2VIDEO console channel (n2vt_say).
 * Slots whose C signature is not known (tile slots, the reserved ones) are forwarded with x1..x7 unchanged and logged as hex.
 */
#include "n2vt_abi.h"
void n2vt_say(const char *format, ...) __attribute__((format(printf, 1, 2)));   /* gpu/video/vt/n2vt_log.h */

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2T_WEAK __attribute__((weak_import))
#else
#define N2T_WEAK __attribute__((weak))
#endif

extern const n2_VTVideoEncoderVTable *CMBaseObjectGetVTable(CMBaseObjectRef o) N2T_WEAK;
extern CFStringRef CFCopyDescription(CFTypeRef cf) N2T_WEAK;
extern CFTypeID CVPixelBufferGetTypeID(void) N2T_WEAK;
extern const char *getprogname(void);

#ifdef N2_HOST_FAKE                                  /* host test: paths from the environment */
#include <stdlib.h>
#define N2T_SWITCH    (getenv("N2T_SWITCH") ? getenv("N2T_SWITCH") : "/nonexistent")
#define N2T_DIR       (getenv("N2T_DIR") ? getenv("N2T_DIR") : "/nonexistent")
#else
#define N2T_SWITCH    "/private/var/db/nord2-video-trace"
#define N2T_DIR       "/private/var/db/nord2-video/trace"
#endif
#define N2T_MAX_LINES 3000
#define N2T_CONSOLE   30
#define N2T_LINE      1024

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_lines;
static char g_path[256];
static n2_VTVideoEncoderCreateInstance g_inner_create;
static const n2_VTVideoEncoderVTable *g_inner_vt;     /* the real encoder's vtable (same for every instance of the factory) */
static n2_CMBaseClass g_base;
static uint64_t g_class[64];                         /* copy of the real class record (0xa8 bytes used, rest zero) */
static n2_VTVideoEncoderVTable g_vt;
static int g_ready;
static unsigned g_serial;

struct n2t_proxy {
	CMBaseObjectRef inner;
	unsigned id;
	unsigned frames, tiles;
};

/* ------------------------------------------------------------------ log */

static void
tlog(const char *fmt, ...)
{
	char line[N2T_LINE];
	va_list ap;
	int n, fd;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n > sizeof(line) - 2)
		n = (int)sizeof(line) - 2;
	line[n++] = '\n';
	pthread_mutex_lock(&g_lock);
	if (g_lines < N2T_MAX_LINES) {
		if (!g_path[0])
			snprintf(g_path, sizeof(g_path), "%s/%s-%d.log", N2T_DIR, getprogname() ? getprogname() : "x", (int)getpid());
		fd = open(g_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0) {
			(void)write(fd, line, (size_t)n);
			(void)close(fd);
		}
		if (g_lines < N2T_CONSOLE) {
			line[n - 1] = 0;
			n2vt_say("trace %s", line);
		}
		g_lines++;
	}
	pthread_mutex_unlock(&g_lock);
}

/* description of a CF value, truncated; never fails */
static const char *
desc(CFTypeRef v, char *buf, size_t cap)
{
	CFStringRef s;

	if (v == NULL) { snprintf(buf, cap, "NULL"); return buf; }
	if (CFCopyDescription == NULL) { snprintf(buf, cap, "(no CFCopyDescription)"); return buf; }
	s = CFCopyDescription(v);
	if (s == NULL) { snprintf(buf, cap, "(no description)"); return buf; }
	if (!CFStringGetCString(s, buf, (CFIndex)cap, kCFStringEncodingUTF8))
		snprintf(buf, cap, "(description longer than %zu bytes)", cap);
	CFRelease(s);
	for (char *p = buf; *p; p++)
		if (*p == '\n' || *p == '\r') *p = ' ';
	return buf;
}

static const char *
cstr(CFStringRef s, char *buf, size_t cap)
{
	if (s == NULL || !CFStringGetCString(s, buf, (CFIndex)cap, kCFStringEncodingUTF8))
		snprintf(buf, cap, "?");
	return buf;
}

static struct n2t_proxy *
P(void *enc)
{
	return (struct n2t_proxy *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)enc);
}

static const n2_VTVideoEncoderClass *
IC(void)
{
	return g_inner_vt->encoderClass;
}

/* ------------------------------------------------------------------ base class forwarders */

static OSStatus
t_copyProperty(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef a, void *valueOut)
{
	struct n2t_proxy *p = P(o);
	char k[128], v[700];
	OSStatus st = g_inner_vt->baseClass->copyProperty ? g_inner_vt->baseClass->copyProperty(p->inner, key, a, valueOut) : -12782;
	tlog("#%u copyProperty %s -> %d %s", p->id, cstr(key, k, sizeof k), (int)st,
	     st == 0 && valueOut ? desc(*(CFTypeRef *)valueOut, v, sizeof v) : "");
	return st;
}

static OSStatus
t_setProperty(CMBaseObjectRef o, CFStringRef key, CFTypeRef value)
{
	struct n2t_proxy *p = P(o);
	char k[128], v[700];
	OSStatus st = g_inner_vt->baseClass->setProperty ? g_inner_vt->baseClass->setProperty(p->inner, key, value) : -12782;
	tlog("#%u setProperty %s = %s -> %d", p->id, cstr(key, k, sizeof k), desc(value, v, sizeof v), (int)st);
	return st;
}

static OSStatus
t_invalidate(CMBaseObjectRef o)
{
	struct n2t_proxy *p = P(o);
	OSStatus st = g_inner_vt->baseClass->invalidate ? g_inner_vt->baseClass->invalidate(p->inner) : 0;
	tlog("#%u invalidate -> %d (frames %u, tiles %u)", p->id, (int)st, p->frames, p->tiles);
	return st;
}

static void
t_finalize(CMBaseObjectRef o)
{
	struct n2t_proxy *p = P(o);
	tlog("#%u finalize (frames %u, tiles %u)", p->id, p->frames, p->tiles);
	if (p->inner)
		CFRelease(p->inner);
	p->inner = NULL;
}

static CFStringRef
t_copyDebugDescription(CMBaseObjectRef o)
{
	struct n2t_proxy *p = P(o);
	return g_inner_vt->baseClass->copyDebugDescription ? g_inner_vt->baseClass->copyDebugDescription(p->inner) : NULL;
}

/* ------------------------------------------------------------------ encoder class forwarders (known signatures) */

static OSStatus
t_startSession(VTVideoEncoderRef enc, VTVideoEncoderSession session, CMVideoDimensions dims)
{
	struct n2t_proxy *p = P(enc);
	OSStatus st = IC()->startSession((VTVideoEncoderRef)p->inner, session, dims);
	tlog("#%u startSession %dx%d -> %d", p->id, (int)dims.width, (int)dims.height, (int)st);
	return st;
}

static OSStatus
t_encodeFrame(VTVideoEncoderRef enc, VTVideoEncoderFrame frame, CVImageBufferRef image, const CMTime *pts,
              const CMTime *duration, CFDictionaryRef props, uint32_t *flags)
{
	struct n2t_proxy *p = P(enc);
	char v[700];
	unsigned w = 0, h = 0, fmt = 0, planes = 0;
	OSStatus st;

	if (image && CVPixelBufferGetTypeID && CFGetTypeID(image) == CVPixelBufferGetTypeID()) {
		w = (unsigned)CVPixelBufferGetWidth((CVPixelBufferRef)image);
		h = (unsigned)CVPixelBufferGetHeight((CVPixelBufferRef)image);
		fmt = CVPixelBufferGetPixelFormatType((CVPixelBufferRef)image);
		planes = (unsigned)CVPixelBufferGetPlaneCount((CVPixelBufferRef)image);
	}
	st = IC()->encodeFrame((VTVideoEncoderRef)p->inner, frame, image, pts, duration, props, flags);
	p->frames++;
	tlog("#%u encodeFrame %u: %ux%u fmt=%c%c%c%c planes=%u pts=%lld/%d dur=%lld/%d props=%s -> %d flags=0x%x", p->id,
	     p->frames, w, h, (fmt >> 24) & 0xff, (fmt >> 16) & 0xff, (fmt >> 8) & 0xff, fmt & 0xff, planes,
	     pts ? (long long)pts->value : -1, pts ? (int)pts->timescale : 0, duration ? (long long)duration->value : -1,
	     duration ? (int)duration->timescale : 0, desc(props, v, sizeof v), (int)st, flags ? *flags : 0);
	return st;
}

static OSStatus
t_completeFrames(VTVideoEncoderRef enc, const CMTime *until)
{
	struct n2t_proxy *p = P(enc);
	OSStatus st = IC()->completeFrames((VTVideoEncoderRef)p->inner, until);
	tlog("#%u completeFrames until=%lld/%d -> %d", p->id, until ? (long long)until->value : -1, until ? (int)until->timescale : 0,
	     (int)st);
	return st;
}

static OSStatus
t_copySupported(VTVideoEncoderRef enc, CFDictionaryRef *out)
{
	struct n2t_proxy *p = P(enc);
	char v[N2T_LINE - 64];
	OSStatus st = IC()->copySupportedPropertyDictionary((VTVideoEncoderRef)p->inner, out);
	tlog("#%u copySupportedPropertyDictionary -> %d %s", p->id, (int)st, st == 0 && out ? desc(*out, v, sizeof v) : "");
	return st;
}

/* ------------------------------------------------------------------ generic forwarders (unknown signatures) */

typedef uint64_t (*n2t_gen_fn)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static const char *const g_slot_names[21] = {
	"version", "startSession", "encodeFrame", "completeFrames", "copySupportedPropertyDictionary", "setProperties",
	"copySerializableProperties", "prepareToEncodeFrames", "slot8", "slot9", "slot10", "slot11", "slot12", "slot13",
	"slot14", "slot15", "slot16", "startTileSession", "prepareToEncodeTiles", "encodeTile", "completeTiles" };

static uint64_t
gen_call(unsigned slot, void *enc, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
	struct n2t_proxy *p = P(enc);
	n2t_gen_fn f = (n2t_gen_fn)(uintptr_t)((const uint64_t *)IC())[slot];
	uint64_t r;

	if (slot == 17 || slot == 19)      /* startTileSession / encodeTile */
		p->tiles += slot == 19;
	r = f(p->inner, a1, a2, a3, a4, a5, a6, a7);
	tlog("#%u %s(x1=%#llx x2=%#llx [lo %u hi %u] x3=%#llx x4=%#llx x5=%#llx x6=%#llx x7=%#llx) -> %d%s", p->id, g_slot_names[slot],
	     (unsigned long long)a1, (unsigned long long)a2, (unsigned)(a2 & 0xffffffffu), (unsigned)(a2 >> 32),
	     (unsigned long long)a3, (unsigned long long)a4, (unsigned long long)a5, (unsigned long long)a6, (unsigned long long)a7,
	     (int)(int32_t)r, "");
	return r;
}

#define GEN(n) static uint64_t gen_##n(void *e, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, \
	uint64_t a7) { return gen_call(n, e, a1, a2, a3, a4, a5, a6, a7); }
GEN(5) GEN(6) GEN(7) GEN(8) GEN(9) GEN(10) GEN(11) GEN(12) GEN(13) GEN(14) GEN(15) GEN(16) GEN(17) GEN(18) GEN(19) GEN(20)
static n2t_gen_fn const g_gen[21] = { 0, 0, 0, 0, 0, gen_5, gen_6, gen_7, gen_8, gen_9, gen_10, gen_11, gen_12, gen_13, gen_14,
	gen_15, gen_16, gen_17, gen_18, gen_19, gen_20 };

/* ------------------------------------------------------------------ vtable copy + factory */

static void
build_vtable(const n2_VTVideoEncoderVTable *ivt)
{
	const uint64_t *ic = (const uint64_t *)ivt->encoderClass;
	unsigned i, n = 0;
	char slots[400];

	g_inner_vt = ivt;
	memcpy(&g_base, ivt->baseClass, sizeof(g_base));
	g_base.derivedStorageSize = sizeof(struct n2t_proxy);
	g_base.copyProperty = t_copyProperty;
	g_base.setProperty = t_setProperty;
	g_base.invalidate = t_invalidate;
	g_base.finalize = t_finalize;
	g_base.copyDebugDescription = ivt->baseClass->copyDebugDescription ? t_copyDebugDescription : NULL;
	g_base.equal = NULL;
	memset(g_class, 0, sizeof(g_class));
	memcpy(g_class, ic, sizeof(n2_VTVideoEncoderClass));
	slots[0] = 0;
	for (i = 1; i < 21; i++) {
		if (!ic[i])
			continue;
		n += (unsigned)snprintf(slots + n, sizeof(slots) - n, " %s", g_slot_names[i]);
		if (n >= sizeof(slots)) n = sizeof(slots) - 1;
		switch (i) {
		case 1: g_class[i] = (uint64_t)(uintptr_t)t_startSession; break;
		case 2: g_class[i] = (uint64_t)(uintptr_t)t_encodeFrame; break;
		case 3: g_class[i] = (uint64_t)(uintptr_t)t_completeFrames; break;
		case 4: g_class[i] = (uint64_t)(uintptr_t)t_copySupported; break;
		default: g_class[i] = (uint64_t)(uintptr_t)g_gen[i]; break;
		}
	}
	g_vt.reserved = NULL;
	g_vt.baseClass = &g_base;
	g_vt.encoderClass = (const n2_VTVideoEncoderClass *)g_class;
	tlog("vtable: base v%llu storage %zu, class v%llu, non-NULL:%s", (unsigned long long)ivt->baseClass->version,
	     ivt->baseClass->derivedStorageSize, (unsigned long long)ic[0], slots);
	g_ready = 1;
}

static OSStatus
n2vt_trace_create(uint32_t codec, CFAllocatorRef alloc, VTVideoEncoderRef *out)
{
	VTVideoEncoderRef inner = NULL;
	CMBaseObjectRef proxy = NULL;
	OSStatus st;

	*out = NULL;
	st = g_inner_create(codec, alloc, &inner);
	if (st != 0 || inner == NULL) {
		tlog("create %c%c%c%c: real factory -> %d", codec >> 24, (codec >> 16) & 0xff, (codec >> 8) & 0xff, codec & 0xff, (int)st);
		return st ? st : N2_kVTAllocationFailedErr;
	}
	pthread_mutex_lock(&g_lock);
	if (!g_ready) {
		pthread_mutex_unlock(&g_lock);
		const n2_VTVideoEncoderVTable *ivt = CMBaseObjectGetVTable((CMBaseObjectRef)inner);
		pthread_mutex_lock(&g_lock);
		if (!g_ready && ivt && ivt->baseClass && ivt->encoderClass) {
			pthread_mutex_unlock(&g_lock);
			build_vtable(ivt);
			pthread_mutex_lock(&g_lock);
		}
	}
	unsigned id = ++g_serial;
	pthread_mutex_unlock(&g_lock);
	if (!g_ready || CMBaseObjectGetVTable((CMBaseObjectRef)inner) != g_inner_vt) {
		tlog("create: unexpected vtable, returning the real encoder untraced");
		*out = inner;
		return 0;
	}
	st = CMDerivedObjectCreate(alloc, &g_vt, VTVideoEncoderGetClassID(), &proxy);
	if (st != 0 || proxy == NULL) {
		tlog("create: CMDerivedObjectCreate -> %d, returning the real encoder untraced", (int)st);
		*out = inner;
		return 0;
	}
	struct n2t_proxy *p = P(proxy);
	p->inner = (CMBaseObjectRef)inner;
	p->id = id;
	tlog("#%u create %c%c%c%c (prog %s pid %d)", id, codec >> 24, (codec >> 16) & 0xff, (codec >> 8) & 0xff, codec & 0xff,
	     getprogname() ? getprogname() : "?", (int)getpid());
	*out = (VTVideoEncoderRef)proxy;
	return 0;
}

__attribute__((visibility("hidden"))) n2_VTVideoEncoderCreateInstance
n2vt_trace_wrap(uint32_t codec, n2_VTVideoEncoderCreateInstance inner)
{
	(void)codec;
	if (inner == NULL || access(N2T_SWITCH, F_OK) != 0 || CMBaseObjectGetVTable == NULL)
		return inner;
	if (g_inner_create != NULL && g_inner_create != inner)
		return inner;                 /* one wrapped factory per process (the HEVC one) */
	g_inner_create = inner;
	n2vt_say("trace: HEVC encoder calls are logged to %s/ (%s exists)", N2T_DIR, N2T_SWITCH);
	return n2vt_trace_create;
}
