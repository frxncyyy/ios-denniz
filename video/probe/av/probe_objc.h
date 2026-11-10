/* SPDX-License-Identifier: MIT */
/*
 * probe_objc.h -- the Objective-C runtime calls, hand-built blocks and the CoreMedia/CoreVideo audio declarations
 * the AVFoundation probe (probe_av.c, WP06) needs. No Apple SDK header is involved; the declarations are the
 * documented public shape of the API, the same method as gpu/build/camera-userland-20261109/src/rwcam_abi.h.
 *
 * The classes of AVFoundation, AVFCapture and Foundation exist in the probe's process only after the frameworks
 * are loaded: n2o_load_frameworks() dlopens them (the dyld shared cache pages them in) before objc_getClass() is
 * asked for anything. Every AVFileType* / AVMediaType* / AVVideo*Key name is an exported NSString constant; its
 * VALUE is taken at run time with dlsym(RTLD_DEFAULT, name) (WP06's TASK requires this: the literal strings in
 * probe_av.c are only a reported fallback for the case dlsym() gives NULL).
 *
 * Blocks: a completion handler is a hand-built global block literal -- the struct the block runtime passes as the
 * `^` argument: { isa = &_NSConcreteGlobalBlock, flags = 1 << 28 (BLOCK_IS_GLOBAL), reserved = 0,
 * invoke, descriptor } with descriptor { 0, sizeof(struct n2o_block) }. N2O_GLOBAL_BLOCK() makes one; it captures
 * nothing, so the handler reads static variables and signals a dispatch semaphore (the task's rule).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2_PROBE_OBJC_H
#define N2_PROBE_OBJC_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <math.h>

#include "n2_apple.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Objective-C runtime (libobjc.A.dylib) ---------------------------------------------------------------- */
typedef signed char BOOL;
typedef struct objc_object *id;
typedef struct objc_class *Class;
typedef struct objc_selector *SEL;
typedef void (*IMP)(void);

Class   objc_getClass(const char *name);
SEL     sel_registerName(const char *name);
void    objc_msgSend(void);
#if defined(__x86_64__)
void    objc_msgSend_stret(void);       /* a >16-byte struct result: hidden pointer first (Darwin x86_64 ABI) */
#endif
void   *objc_autoreleasePoolPush(void);
void    objc_autoreleasePoolPop(void *pool);
Class   objc_allocateClassPair(Class superclass, const char *name, size_t extraBytes);
void    objc_registerClassPair(Class cls);
BOOL    class_addMethod(Class cls, SEL name, IMP imp, const char *types);

/* ---- the block ABI (what a block literal is) --------------------------------------------------------------- */
struct n2o_block_descriptor {
	unsigned long reserved;
	unsigned long size;
};
struct n2o_block {
	void                                    *isa;
	int32_t                                  flags;
	int32_t                                  reserved;
	void                                    *invoke;
	const struct n2o_block_descriptor       *descriptor;
};
extern void *_NSConcreteGlobalBlock;    /* the global block class object of libSystem's block runtime */
#define N2O_BLOCK_IS_GLOBAL (1 << 28)

/* N2O_GLOBAL_BLOCK(name, (void *block, ...), body...): a static global block with no captured state. */
#define N2O_GLOBAL_BLOCK(NAME, PARAMS, ...)                                                                    \
	static void NAME##_invoke PARAMS { __VA_ARGS__ }                                                       \
	static const struct n2o_block_descriptor NAME##_descriptor = { 0, sizeof(struct n2o_block) };           \
	static struct n2o_block NAME = {                                                                        \
		(void *)&_NSConcreteGlobalBlock, N2O_BLOCK_IS_GLOBAL, 0, (void *)&NAME##_invoke, &NAME##_descriptor \
	}

/* ---- libdispatch semaphores (in libSystem, as VideoToolbox code uses them) ---------------------------------- */
typedef struct dispatch_semaphore_s *dispatch_semaphore_t;
dispatch_semaphore_t dispatch_semaphore_create(long value);
long                 dispatch_semaphore_wait(dispatch_semaphore_t semaphore, uint64_t timeout);
long                 dispatch_semaphore_signal(dispatch_semaphore_t semaphore);
uint64_t             dispatch_time(uint64_t when, int64_t delta);
#define N2O_TIME_NOW 0ull
#define N2O_TIME_SECONDS(n) ((int64_t)(n) * 1000000000ll)

/* ---- CoreVideo additions (the calls n2_apple.h does not declare) ------------------------------------------- */
typedef struct __CVPixelBufferPool *CVPixelBufferPoolRef;
typedef int32_t                      CVReturn;
CVReturn CVPixelBufferPoolCreatePixelBuffer(CFAllocatorRef allocator, CVPixelBufferPoolRef pool,
                                            CVPixelBufferRef *pixelBufferOut);
void     CVPixelBufferRelease(CVPixelBufferRef pixelBuffer);

/* ---- CoreMedia additions: audio sample buffers (a 440 Hz LPCM sine for the writer) ------------------------- */
typedef CMFormatDescriptionRef CMAudioFormatDescriptionRef;
typedef long                   CMItemCount;

struct n2o_asbd {                       /* AudioStreamBasicDescription, 40 bytes, the documented layout */
	double   mSampleRate;
	uint32_t mFormatID;
	uint32_t mFormatFlags;
	uint32_t mBytesPerPacket;
	uint32_t mFramesPerPacket;
	uint32_t mBytesPerFrame;
	uint32_t mChannelsPerFrame;
	uint32_t mBitsPerChannel;
	uint32_t mReserved;
};

OSStatus    CMAudioFormatDescriptionCreate(CFAllocatorRef allocator, const struct n2o_asbd *asbd, size_t layoutSize,
                                           const void *layout, size_t magicCookieSize, const void *magicCookie,
                                           CFDictionaryRef extensions, CMAudioFormatDescriptionRef *formatDescriptionOut);
CMItemCount CMSampleBufferGetNumSamples(CMSampleBufferRef sampleBuffer);
CVImageBufferRef CMSampleBufferGetImageBuffer(CMSampleBufferRef sampleBuffer);

#define N2O_FORMAT_LPCM 0x6c70636dU     /* 'lpcm' */
#define N2O_FLAG_SIGNED_INTEGER 4U      /* kLinearPCMFormatFlagIsSignedInteger */
#define N2O_FLAG_PACKED 8U              /* kLinearPCMFormatFlagIsPacked */

/* ---- small conveniences shared by the AV tests ------------------------------------------------------------- */
#define N2O_SEL(name) sel_registerName(name)

typedef struct { double width, height; } N2O_SIZE;   /* CGSize on 64-bit */

/* a message with a >16-byte struct result: objc_msgSend_stret on the x86_64 ABI, objc_msgSend on arm64 */
static inline CMTime n2o_cmtime(id object, const char *name)
{
	CMTime t = { 0, 1, 0, 0 };

	if (object == NULL) {
		return t;
	}
#if defined(__x86_64__)
	((void (*)(CMTime *, id, SEL))objc_msgSend_stret)(&t, object, sel_registerName(name));
#else
	t = ((CMTime (*)(id, SEL))objc_msgSend)(object, sel_registerName(name));
#endif
	return t;
}

static inline N2O_SIZE n2o_size(id object, const char *name)
{
	N2O_SIZE s = { 0, 0 };

	if (object == NULL) {
		return s;
	}
	return ((N2O_SIZE (*)(id, SEL))objc_msgSend)(object, sel_registerName(name));
}

static inline float n2o_float(id object, const char *name)
{
	if (object == NULL) {
		return 0.0f;
	}
	return ((float (*)(id, SEL))objc_msgSend)(object, sel_registerName(name));
}

static inline const char *n2o_utf8(id string_object)
{
	if (string_object == NULL) {
		return NULL;
	}
	return ((const char *(*)(id, SEL))objc_msgSend)(string_object, sel_registerName("UTF8String"));
}

static inline CMTime n2o_time(int64_t value, int32_t timescale)
{
	CMTime t = { value, timescale, N2_CMTIME_VALID, 0 };

	return t;
}

#ifdef __cplusplus
}
#endif
#endif
