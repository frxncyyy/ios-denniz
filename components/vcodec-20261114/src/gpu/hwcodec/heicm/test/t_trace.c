/* SPDX-License-Identifier: MIT */
/*
 * t_trace.c -- host test of heicm/n2vt_trace.c against gpu/video's fake Apple runtime (opus-jpeg).
 *  1. switch absent: n2vt_trace_wrap returns the factory unchanged.
 *  2. gpu/video's JPEG encoder (n2jpeg_vt_create) wrapped: a session of 2 frames + properties through the proxy emits exactly
 *     what the real encoder emits, no protocol error, no leak, and the log names every call.
 *  3. a made-up inner encoder with tile slots: the generic forwarders pass x1..x7 unchanged with the REAL object as self.
 */
#define _DEFAULT_SOURCE 1
#define N2FAKE_BUILD 1
#include "fake_apple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern n2_VTVideoEncoderCreateInstance n2vt_trace_wrap(uint32_t codec, n2_VTVideoEncoderCreateInstance inner);
extern OSStatus n2jpeg_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut);

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* --- the runtime pieces gpu/video's fake does not have --- */
struct fake_cmobject { struct n2fake_base base; const n2_VTVideoEncoderVTable *vt; CMTypeID classID; unsigned char storage[]; };
const n2_VTVideoEncoderVTable *CMBaseObjectGetVTable(CMBaseObjectRef o) { return ((struct fake_cmobject *)o)->vt; }
CFStringRef CFCopyDescription(CFTypeRef cf) { (void)cf; return CFStringCreateWithCString(kCFAllocatorDefault, "<desc>", kCFStringEncodingUTF8); }
static CFTypeID pb_type;
CFTypeID CVPixelBufferGetTypeID(void) { return pb_type; }
void n2vt_say(const char *fmt, ...) { (void)fmt; }
const char *getprogname(void) { return "t_trace"; }

/* --- a made-up encoder with tile slots --- */
static uint64_t seen[8];
static void *seen_self;
static OSStatus m_start(VTVideoEncoderRef e, VTVideoEncoderSession s, CMVideoDimensions d) { (void)e; (void)s; (void)d; return 0; }
static OSStatus m_frame(VTVideoEncoderRef e, VTVideoEncoderFrame f, CVImageBufferRef i, const CMTime *p, const CMTime *d,
                        CFDictionaryRef pr, uint32_t *fl) { (void)e; (void)f; (void)i; (void)p; (void)d; (void)pr; (void)fl; return 0; }
static OSStatus m_complete(VTVideoEncoderRef e, const CMTime *u) { (void)e; (void)u; return 0; }
static OSStatus m_supported(VTVideoEncoderRef e, CFDictionaryRef *o) { (void)e; *o = NULL; return -12900; }
static uint64_t m_tile(void *self, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
	seen_self = self; seen[1] = a1; seen[2] = a2; seen[3] = a3; seen[4] = a4; seen[5] = a5; seen[6] = a6; seen[7] = a7;
	return 7;
}
static void m_finalize(CMBaseObjectRef o) { (void)o; }
static const n2_CMBaseClass m_base = { 1, 16, NULL, NULL, m_finalize, NULL, NULL, NULL, NULL, NULL, NULL };
static n2_VTVideoEncoderClass m_class;
static n2_VTVideoEncoderVTable m_vt = { NULL, &m_base, &m_class };
static OSStatus
m_create(uint32_t codec, CFAllocatorRef a, VTVideoEncoderRef *out)
{
	(void)codec;
	return CMDerivedObjectCreate(a, &m_vt, VTVideoEncoderGetClassID(), (CMBaseObjectRef *)out);
}

static int
log_has(const char *dir, const char *needle)
{
	char path[512], line[2048];
	int hit = 0;
	snprintf(path, sizeof path, "%s/t_trace-%d.log", dir, (int)getpid());
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	while (fgets(line, sizeof line, f)) if (strstr(line, needle)) hit = 1;
	fclose(f);
	return hit;
}

static int
tiles_main(void)
{
	m_class.version = 4; m_class.startSession = m_start; m_class.encodeFrame = m_frame; m_class.completeFrames = m_complete;
	m_class.copySupportedPropertyDictionary = m_supported;
	m_class.startTileSession = (void *)(uintptr_t)m_tile; m_class.encodeTile = (void *)(uintptr_t)m_tile;
	n2_VTVideoEncoderCreateInstance w = n2vt_trace_wrap(0x68766331u, m_create);
	VTVideoEncoderRef proxy = NULL;
	CHECK(w != m_create);
	CHECK(w(0x68766331u, kCFAllocatorDefault, &proxy) == 0 && proxy != NULL);
	const n2_VTVideoEncoderVTable *pvt = CMBaseObjectGetVTable((CMBaseObjectRef)proxy);
	CHECK(pvt != &m_vt && pvt->encoderClass->version == 4 && pvt->encoderClass->encodeTile != NULL);
	CHECK(pvt->encoderClass->prepareToEncodeTiles == NULL && pvt->encoderClass->completeTiles == NULL);
	typedef uint64_t (*G)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
	uint64_t r = ((G)(uintptr_t)pvt->encoderClass->startTileSession)(proxy, 11, ((uint64_t)384 << 32) | 512, 33, 44, 55, 66, 77);
	CHECK(r == 7 && seen[1] == 11 && seen[2] == (((uint64_t)384 << 32) | 512) && seen[7] == 77);
	CHECK(seen_self != (void *)proxy && CMBaseObjectGetVTable((CMBaseObjectRef)seen_self) == &m_vt);
	r = ((G)(uintptr_t)pvt->encoderClass->encodeTile)(proxy, 1, 2, 3, 4, 5, 6, 7);
	CHECK(r == 7 && seen[3] == 3);
	CFRelease(proxy);
	const char *dir = getenv("N2T_DIR");
	CHECK(log_has(dir, "class v4, non-NULL: startSession encodeFrame completeFrames copySupportedPropertyDictionary startTileSession encodeTile"));
	CHECK(log_has(dir, "startTileSession(x1=0xb x2=0x18000000200 [lo 512 hi 384]"));
	CHECK(log_has(dir, "#1 finalize (frames 0, tiles 1)"));
	CHECK(n2fake_live_objects() == 0);
	printf("T_TRACE tiles %s %d/%d\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	return fails != 0;
}

int
main(void)
{
	if (getenv("T_MODE") && !strcmp(getenv("T_MODE"), "tiles"))
		return tiles_main();
	const char *dir = getenv("N2T_DIR");
	CVPixelBufferRef pb = NULL;
	CHECK(dir != NULL);
	CHECK(CVPixelBufferCreate(kCFAllocatorDefault, 64, 48, N2_PIXFMT_420V, NULL, &pb) == 0);
	pb_type = CFGetTypeID(pb);

	/* 1 */
	const char *sw = getenv("N2T_SWITCH");
	unsetenv("N2T_SWITCH");
	CHECK(n2vt_trace_wrap(0x6a706567u, n2jpeg_vt_create) == n2jpeg_vt_create);
	setenv("N2T_SWITCH", sw, 1);

	/* 2 */
	n2_VTVideoEncoderCreateInstance w = n2vt_trace_wrap(0x6a706567u, n2jpeg_vt_create);
	CHECK(w != n2jpeg_vt_create && w != NULL);
	struct n2fake_session *s = n2fake_session_create(w, 0x6a706567u, 64, 48);
	CHECK(s != NULL);
	if (s) {
		CMTime t0 = { 0, 600, 1, 0 }, t1 = { 20, 600, 1, 0 }, d = { 20, 600, 1, 0 };
		CFTypeRef v = NULL;
		double q = 0.75;
		CFNumberRef qn = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &q);
		CHECK(n2fake_session_set_property(s, n2fake_cfstr("Quality"), qn) == 0);
		CFRelease(qn);
		CHECK(n2fake_session_copy_property(s, n2fake_cfstr("Quality"), &v) == 0);
		if (v) CFRelease(v);
		CHECK(n2fake_session_encode(s, pb, t0, d, NULL) == 0);
		CHECK(n2fake_session_encode(s, pb, t1, d, NULL) == 0);
		CHECK(n2fake_session_complete(s) == 0);
		OSStatus st = -1; uint32_t fl = 0; CMSampleBufferRef sb = NULL; void *tok = NULL;
		CHECK(n2fake_session_emitted(s, 1, &st, &fl, &sb, &tok) == 1 && st == 0 && sb != NULL);
		CHECK(n2fake_session_emitted(s, 2, &st, &fl, &sb, &tok) == 0);
		CHECK(n2fake_session_protocol_errors(s) == 0);
		n2fake_session_destroy(s);
	}
	CHECK(log_has(dir, "vtable: base v1"));
	CHECK(log_has(dir, "#1 create jpeg"));
	CHECK(log_has(dir, "#1 startSession 64x48 -> 0"));
	CHECK(log_has(dir, "#1 setProperty Quality = <desc> -> 0"));
	CHECK(log_has(dir, "#1 encodeFrame 2: 64x48 fmt=420v planes=2 pts=20/600"));
	CHECK(log_has(dir, "#1 completeFrames"));
	CHECK(log_has(dir, "#1 finalize (frames 2, tiles 0)"));

	/* a second factory in the same process is not wrapped (one per process) */
	CHECK(n2vt_trace_wrap(0x68766331u, m_create) == m_create);
	CHECK(n2fake_live_objects() == 1);   /* the pixel buffer */
	CFRelease(pb);
	CHECK(n2fake_live_objects() == 0);
	printf("T_TRACE %s %d/%d\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	return fails != 0;
}
