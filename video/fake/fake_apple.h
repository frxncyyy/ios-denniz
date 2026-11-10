/* SPDX-License-Identifier: MIT */
/*
 * fake_apple.h -- the test-only replacement for the Apple runtime used on the host build (-DN2_HOST_FAKE).
 *
 * This header declares the extra inspection/driving calls of the fake.  The CoreFoundation, CoreMedia and CoreVideo
 * functions themselves are declared in include/n2_apple.h, the VideoToolbox ABI in include/n2vt_abi.h; this file adds
 * only n2fake_* names.  Everything here is host-only and must never be compiled into the iOS dylib.
 */
#ifndef N2FAKE_APPLE_H
#define N2FAKE_APPLE_H

#ifndef N2_HOST_FAKE
#error "fake_apple.h is a host-test header: compile with -DN2_HOST_FAKE"
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "n2_apple.h"
#include "n2vt_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------------------------------ */
/* Process-wide state                                                                                                 */
/* ------------------------------------------------------------------------------------------------------------------ */

/* Number of live, releasable fake objects.  Interned strings and the static singletons are exempt (process lifetime). */
long n2fake_live_objects(void);

/* Intern a UTF-8 string: the same text always gives the same pointer; the string lives until the process exits. */
CFStringRef n2fake_cfstr(const char *s);

/* Number of entries in a CFDictionary (insertion order). */
CFIndex      n2fake_dictionary_count(CFDictionaryRef d);
/* Borrowed key at an insertion index, or NULL when out of range. */
const void  *n2fake_dictionary_key_at(CFDictionaryRef d, CFIndex index);

/* ------------------------------------------------------------------------------------------------------------------ */
/* CoreMedia inspection                                                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

/* 1 and the stored timing[0] / first sample size of a CMSampleBuffer (sampleSize 0 when no size array); 0 if empty. */
int    n2fake_sample_timing(CMSampleBufferRef sb, CMSampleTimingInfo *timing, size_t *sampleSize);
/* Number of samples the CMSampleBuffer was created with. */
size_t n2fake_sample_count(CMSampleBufferRef sb);
/* Sample size at an index, 0 when the index has no entry. */
size_t n2fake_sample_size_at(CMSampleBufferRef sb, size_t index);

/* ------------------------------------------------------------------------------------------------------------------ */
/* CoreVideo inspection                                                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

/* Height in rows of a plane (chroma planes are half the picture height, rounded up). */
size_t n2fake_pixel_buffer_plane_height(CVPixelBufferRef pb, size_t plane);
/* Bytes actually used in each row of a plane (luma: width; 420v/420f chroma: width; i420 chroma: (width+1)/2). */
size_t n2fake_pixel_buffer_plane_bytes_used(CVPixelBufferRef pb, size_t plane);
/* Bytes allocated for a plane (stride * plane height). */
size_t n2fake_pixel_buffer_plane_data_size(CVPixelBufferRef pb, size_t plane);

/* ------------------------------------------------------------------------------------------------------------------ */
/* VideoToolbox registry and session driver                                                                           */
/* ------------------------------------------------------------------------------------------------------------------ */

/* 1 and the registration at index (borrowed info, may be NULL); 0 when index is past the end. */
int  n2fake_vt_registered(int index, uint32_t *codec, CFDictionaryRef *info, n2_VTVideoEncoderCreateInstance *fn);
/* Forget every registration (releases the retained info dictionaries). */
void n2fake_vt_reset(void);

struct n2fake_session;

/* Create an encoder through fn, then call its startSession with (width,height); NULL if either step fails. */
struct n2fake_session *n2fake_session_create(n2_VTVideoEncoderCreateInstance fn, uint32_t codec, int width, int height);
/* Submit one frame: calls encodeFrame with a fresh frame token and local CMTime copies. */
OSStatus n2fake_session_encode(struct n2fake_session *s, CVPixelBufferRef pb, CMTime pts, CMTime duration,
                               CFDictionaryRef frameProps);
/* Ask the encoder to complete all frames (completeFrames(enc, NULL)). */
OSStatus n2fake_session_complete(struct n2fake_session *s);
/* Call the encoder's baseClass setProperty. */
OSStatus n2fake_session_set_property(struct n2fake_session *s, CFStringRef key, CFTypeRef value);
/* Call the encoder's baseClass copyProperty; returns a +1 reference in *valueOut. */
OSStatus n2fake_session_copy_property(struct n2fake_session *s, CFStringRef key, CFTypeRef *valueOut);
/* 1 and the index-th recorded emit (status, flags, BORROWED sample buffer, frame token); 0 when out of range. */
int  n2fake_session_emitted(struct n2fake_session *s, int index, OSStatus *status, uint32_t *flags,
                            CMSampleBufferRef *sbuf, void **frameToken);
/* Last dictionary passed to VTEncoderSessionSetPixelBufferAttributes (borrowed, NULL if never set). */
CFDictionaryRef n2fake_session_pixel_attributes(struct n2fake_session *s);
/* The vtable CMDerivedObjectCreate stored for this session's encoder (borrowed, never NULL). */
const n2_VTVideoEncoderVTable *n2fake_session_vtable(struct n2fake_session *s);
/* The encoder object VTVideoEncoderGetCMBaseObject() also returns (borrowed, owned by the session). */
VTVideoEncoderRef n2fake_session_encoder(struct n2fake_session *s);
/* Sticky count of protocol violations: emit for an unknown token or a second emit for one token. */
int  n2fake_session_protocol_errors(struct n2fake_session *s);
/* Release the encoder (calling finalize once), the recorded sample buffers and the session itself; NULL is a no-op. */
void n2fake_session_destroy(struct n2fake_session *s);

/* ------------------------------------------------------------------------------------------------------------------ */
/* Internal to fake_apple.c / fake_vt.c.  Not part of the test-facing API.                                            */
/* ------------------------------------------------------------------------------------------------------------------ */
#ifdef N2FAKE_BUILD

enum {
	N2FAKE_TYPE_ALLOCATOR    = 1,
	N2FAKE_TYPE_STRING       = 2,
	N2FAKE_TYPE_NUMBER       = 3,
	N2FAKE_TYPE_BOOLEAN      = 4,
	N2FAKE_TYPE_DICTIONARY   = 5,
	N2FAKE_TYPE_ARRAY        = 6,
	N2FAKE_TYPE_BLOCKBUFFER  = 7,
	N2FAKE_TYPE_SAMPLEBUFFER = 8,
	N2FAKE_TYPE_FORMATDESC   = 9,
	N2FAKE_TYPE_PIXELBUFFER  = 10,
	N2FAKE_TYPE_CMOBJECT     = 11
};

#define N2FAKE_MAGIC            0x6e32664bu             /* "n2fK" */
#define N2FAKE_FLAG_STATIC      1u
#define N2FAKE_FLAG_INTERNED    2u

struct n2fake_base {
	uint32_t magic;
	uint32_t type;
	_Atomic long refcount;
	uint32_t flags;
	uint32_t reserved;
	void (*destroy_extra)(struct n2fake_base *);
};

/* Allocate a zeroed object with refcount 1; countable objects appear in n2fake_live_objects(). */
void *n2fake_object_alloc(size_t size, uint32_t type, int countable);
/* Validate the magic and (when expectedType != 0) the type; abort() naming func on NULL/wrong/freed objects. */
struct n2fake_base *n2fake_object_check(CFTypeRef cf, uint32_t expectedType, const char *func);
/* abort() with "n2fake: <func>: <what>" -- the fake's replacement for the crash a real NULL would cause. */
void n2fake_object_fatal(const char *func, const char *what) __attribute__((noreturn));

#endif /* N2FAKE_BUILD */

#ifdef __cplusplus
}
#endif
#endif /* N2FAKE_APPLE_H */
