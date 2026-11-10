/* SPDX-License-Identifier: MIT */
/*
 * fake_vt.c -- host-test side of include/n2vt_abi.h: the CMBaseObject/VTVideoEncoder object, the registration registry,
 * and the session driver that calls an encoder the way VideoToolbox does (startSession, encodeFrame, emit, complete).
 *
 * The encoder object is a fake CF object owned by fake_apple.c: one allocation holds the object header, the vtable
 * pointer and the zero-filled derived storage the encoder asked for.  Releasing the last reference calls the base
 * class's finalize exactly once.
 */
#define N2FAKE_BUILD 1
#include "fake_apple.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N2FAKE_VT_CLASS_ID 0x6e327674ul         /* 'n2vt': the fixed class id of the fake encoder base object */

struct n2fake_cmobject {
	struct n2fake_base base;
	const n2_VTVideoEncoderVTable *vt;
	CMTypeID classID;
	unsigned char storage[];
};

static void n2fake_cmobject_destroy(struct n2fake_base *b)
{
	struct n2fake_cmobject *o = (struct n2fake_cmobject *)b;
	const n2_CMBaseClass *bc = o->vt ? o->vt->baseClass : NULL;

	if (bc && bc->finalize)
		bc->finalize((CMBaseObjectRef)o);
}

CMTypeID VTVideoEncoderGetClassID(void)
{
	return (CMTypeID)N2FAKE_VT_CLASS_ID;
}

OSStatus CMDerivedObjectCreate(CFAllocatorRef allocator, const void *vtable, CMTypeID classID,
                               CMBaseObjectRef *objectOut)
{
	const n2_VTVideoEncoderVTable *vt = (const n2_VTVideoEncoderVTable *)vtable;
	struct n2fake_cmobject *o;
	size_t extra;

	(void)allocator;
	if (!vt || !objectOut)
		return -12780;
	if (classID != VTVideoEncoderGetClassID())
		return -12780;
	if (!vt->baseClass)
		return -12780;
	extra = vt->baseClass->derivedStorageSize;
	if (extra > SIZE_MAX - sizeof(*o))
		return -12780;
	o = (struct n2fake_cmobject *)n2fake_object_alloc(sizeof(*o) + extra, N2FAKE_TYPE_CMOBJECT, 1);
	o->vt = vt;
	o->classID = classID;
	o->base.destroy_extra = n2fake_cmobject_destroy;
	*objectOut = (CMBaseObjectRef)o;
	return 0;
}

void *CMBaseObjectGetDerivedStorage(CMBaseObjectRef o)
{
	struct n2fake_cmobject *obj = (struct n2fake_cmobject *)n2fake_object_check(
		(CFTypeRef)o, N2FAKE_TYPE_CMOBJECT, "CMBaseObjectGetDerivedStorage");
	return obj->storage;
}

CMBaseObjectRef VTVideoEncoderGetCMBaseObject(VTVideoEncoderRef enc)
{
	(void)n2fake_object_check((CFTypeRef)enc, N2FAKE_TYPE_CMOBJECT, "VTVideoEncoderGetCMBaseObject");
	return (CMBaseObjectRef)enc;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Registry                                                                                                           */
/* ------------------------------------------------------------------------------------------------------------------ */

struct n2fake_registration {
	uint32_t codec;
	CFDictionaryRef info;
	n2_VTVideoEncoderCreateInstance fn;
};

static pthread_mutex_t g_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static struct n2fake_registration *g_registry;
static int g_registry_count;
static int g_registry_capacity;

OSStatus VTRegisterVideoEncoderWithInfo(uint32_t codecType, CFDictionaryRef info,
                                        n2_VTVideoEncoderCreateInstance create)
{
	struct n2fake_registration *r;

	if (!create)
		return N2_kVTParameterErr;
	if (codecType == 0)
		return N2_kVTParameterErr;
	pthread_mutex_lock(&g_registry_lock);
	if (g_registry_count == g_registry_capacity) {
		int cap = g_registry_capacity ? g_registry_capacity * 2 : 8;
		struct n2fake_registration *p = (struct n2fake_registration *)realloc(
			g_registry, (size_t)cap * sizeof(*p));
		if (!p) {
			pthread_mutex_unlock(&g_registry_lock);
			return N2_kVTAllocationFailedErr;
		}
		g_registry = p;
		g_registry_capacity = cap;
	}
	r = &g_registry[g_registry_count++];
	r->codec = codecType;
	r->info = info ? (CFDictionaryRef)CFRetain(info) : NULL;
	r->fn = create;
	pthread_mutex_unlock(&g_registry_lock);
	return 0;
}

OSStatus VTRegisterVideoEncoder(uint32_t codecType, n2_VTVideoEncoderCreateInstance create)
{
	return VTRegisterVideoEncoderWithInfo(codecType, NULL, create);
}

int n2fake_vt_registered(int index, uint32_t *codec, CFDictionaryRef *info,
                         n2_VTVideoEncoderCreateInstance *fn)
{
	int found = 0;

	pthread_mutex_lock(&g_registry_lock);
	if (index >= 0 && index < g_registry_count) {
		if (codec)
			*codec = g_registry[index].codec;
		if (info)
			*info = g_registry[index].info;
		if (fn)
			*fn = g_registry[index].fn;
		found = 1;
	}
	pthread_mutex_unlock(&g_registry_lock);
	return found;
}

void n2fake_vt_reset(void)
{
	int i;

	pthread_mutex_lock(&g_registry_lock);
	for (i = 0; i < g_registry_count; i++)
		if (g_registry[i].info)
			CFRelease((CFTypeRef)g_registry[i].info);
	free(g_registry);
	g_registry = NULL;
	g_registry_count = 0;
	g_registry_capacity = 0;
	pthread_mutex_unlock(&g_registry_lock);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Session driver                                                                                                     */
/* ------------------------------------------------------------------------------------------------------------------ */

struct n2fake_frame_token {
	uint64_t id;
	int emitted;
};

struct n2fake_emit {
	OSStatus status;
	uint32_t flags;
	CMSampleBufferRef sbuf;
	struct n2fake_frame_token *token;
};

struct n2fake_session {
	VTVideoEncoderRef enc;
	CFDictionaryRef pixelAttributes;
	struct n2fake_frame_token **frames;
	size_t frameCount;
	size_t frameCapacity;
	struct n2fake_emit *emits;
	size_t emitCount;
	size_t emitCapacity;
	int protocolErrors;
	uint64_t nextToken;
};

static const n2_VTVideoEncoderClass *n2fake_session_class(struct n2fake_session *s, const char *func)
{
	struct n2fake_cmobject *o;

	if (!s)
		n2fake_object_fatal(func, "NULL session");
	o = (struct n2fake_cmobject *)n2fake_object_check((CFTypeRef)s->enc, N2FAKE_TYPE_CMOBJECT, func);
	if (!o->vt || !o->vt->encoderClass)
		n2fake_object_fatal(func, "encoder has no encoderClass");
	return o->vt->encoderClass;
}

struct n2fake_session *n2fake_session_create(n2_VTVideoEncoderCreateInstance fn, uint32_t codec,
                                             int width, int height)
{
	struct n2fake_session *s;
	struct n2fake_cmobject *o;
	const n2_VTVideoEncoderClass *ec;
	VTVideoEncoderRef enc = NULL;
	CMVideoDimensions dims;
	OSStatus st;

	if (!fn)
		return NULL;
	s = (struct n2fake_session *)calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	st = fn(codec, kCFAllocatorDefault, &enc);
	if (st != 0 || !enc) {
		free(s);
		return NULL;
	}
	o = (struct n2fake_cmobject *)(uintptr_t)enc;
	if (o->base.magic != N2FAKE_MAGIC || o->base.type != N2FAKE_TYPE_CMOBJECT ||
	    !o->vt || !o->vt->encoderClass || !o->vt->encoderClass->startSession) {
		CFRelease((CFTypeRef)enc);
		free(s);
		return NULL;
	}
	ec = o->vt->encoderClass;
	dims.width = width;
	dims.height = height;
	st = ec->startSession(enc, (VTVideoEncoderSession)s, dims);
	if (st != 0) {
		CFRelease((CFTypeRef)enc);
		free(s);
		return NULL;
	}
	s->enc = enc;
	s->nextToken = 1;
	return s;
}

static struct n2fake_frame_token *n2fake_session_new_token(struct n2fake_session *s)
{
	struct n2fake_frame_token *t;

	if (s->frameCount == s->frameCapacity) {
		size_t cap = s->frameCapacity ? s->frameCapacity * 2 : 8;
		struct n2fake_frame_token **p = (struct n2fake_frame_token **)realloc(
			s->frames, cap * sizeof(*p));
		if (!p)
			n2fake_object_fatal("n2fake_session_encode", "out of memory growing the frame list");
		s->frames = p;
		s->frameCapacity = cap;
	}
	t = (struct n2fake_frame_token *)calloc(1, sizeof(*t));
	if (!t)
		n2fake_object_fatal("n2fake_session_encode", "out of memory");
	t->id = s->nextToken++;
	s->frames[s->frameCount++] = t;
	return t;
}

OSStatus n2fake_session_encode(struct n2fake_session *s, CVPixelBufferRef pb, CMTime pts,
                               CMTime duration, CFDictionaryRef frameProps)
{
	const n2_VTVideoEncoderClass *ec = n2fake_session_class(s, "n2fake_session_encode");
	struct n2fake_frame_token *token;
	CMTime localPts, localDuration;
	uint32_t infoFlags = 0;
	OSStatus st;

	if (!ec->encodeFrame)
		return N2_kVTVideoEncoderMalfunctionErr;
	token = n2fake_session_new_token(s);
	localPts = pts;
	localDuration = duration;
	st = ec->encodeFrame(s->enc, (VTVideoEncoderFrame)token, pb, &localPts, &localDuration,
	                     frameProps, &infoFlags);
	return st;
}

OSStatus n2fake_session_complete(struct n2fake_session *s)
{
	const n2_VTVideoEncoderClass *ec = n2fake_session_class(s, "n2fake_session_complete");

	if (!ec->completeFrames)
		return N2_kVTVideoEncoderMalfunctionErr;
	return ec->completeFrames(s->enc, NULL);
}

OSStatus n2fake_session_set_property(struct n2fake_session *s, CFStringRef key, CFTypeRef value)
{
	struct n2fake_cmobject *o;
	const n2_CMBaseClass *bc;

	(void)n2fake_session_class(s, "n2fake_session_set_property");
	o = (struct n2fake_cmobject *)(uintptr_t)s->enc;
	bc = o->vt->baseClass;
	if (!bc || !bc->setProperty)
		return N2_kVTPropertyNotSupportedErr;
	return bc->setProperty((CMBaseObjectRef)o, key, value);
}

OSStatus n2fake_session_copy_property(struct n2fake_session *s, CFStringRef key, CFTypeRef *valueOut)
{
	struct n2fake_cmobject *o;
	const n2_CMBaseClass *bc;

	(void)n2fake_session_class(s, "n2fake_session_copy_property");
	if (!valueOut)
		n2fake_object_fatal("n2fake_session_copy_property", "NULL out pointer");
	o = (struct n2fake_cmobject *)(uintptr_t)s->enc;
	bc = o->vt->baseClass;
	if (!bc || !bc->copyProperty)
		return N2_kVTPropertyNotSupportedErr;
	return bc->copyProperty((CMBaseObjectRef)o, key, kCFAllocatorDefault, (void *)valueOut);
}

int n2fake_session_emitted(struct n2fake_session *s, int index, OSStatus *status, uint32_t *flags,
                           CMSampleBufferRef *sbuf, void **frameToken)
{
	if (!s)
		n2fake_object_fatal("n2fake_session_emitted", "NULL session");
	if (index < 0 || (size_t)index >= s->emitCount)
		return 0;
	if (status)
		*status = s->emits[index].status;
	if (flags)
		*flags = s->emits[index].flags;
	if (sbuf)
		*sbuf = s->emits[index].sbuf;
	if (frameToken)
		*frameToken = (void *)s->emits[index].token;
	return 1;
}

CFDictionaryRef n2fake_session_pixel_attributes(struct n2fake_session *s)
{
	if (!s)
		n2fake_object_fatal("n2fake_session_pixel_attributes", "NULL session");
	return s->pixelAttributes;
}

const n2_VTVideoEncoderVTable *n2fake_session_vtable(struct n2fake_session *s)
{
	struct n2fake_cmobject *o;

	if (!s)
		n2fake_object_fatal("n2fake_session_vtable", "NULL session");
	o = (struct n2fake_cmobject *)n2fake_object_check((CFTypeRef)s->enc, N2FAKE_TYPE_CMOBJECT,
	                                                  "n2fake_session_vtable");
	return o->vt;
}

VTVideoEncoderRef n2fake_session_encoder(struct n2fake_session *s)
{
	if (!s)
		n2fake_object_fatal("n2fake_session_encoder", "NULL session");
	return s->enc;
}

int n2fake_session_protocol_errors(struct n2fake_session *s)
{
	if (!s)
		n2fake_object_fatal("n2fake_session_protocol_errors", "NULL session");
	return s->protocolErrors;
}

void n2fake_session_destroy(struct n2fake_session *s)
{
	size_t i;

	if (!s)
		return;
	for (i = 0; i < s->emitCount; i++)
		if (s->emits[i].sbuf)
			CFRelease((CFTypeRef)s->emits[i].sbuf);
	free(s->emits);
	for (i = 0; i < s->frameCount; i++)
		free(s->frames[i]);
	free(s->frames);
	if (s->pixelAttributes)
		CFRelease((CFTypeRef)s->pixelAttributes);
	if (s->enc)
		CFRelease((CFTypeRef)s->enc);
	free(s);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* VideoToolbox session -> encoder calls                                                                              */
/* ------------------------------------------------------------------------------------------------------------------ */

OSStatus VTEncoderSessionSetPixelBufferAttributes(VTVideoEncoderSession session, CFDictionaryRef attributes)
{
	struct n2fake_session *s = (struct n2fake_session *)session;

	if (!s)
		n2fake_object_fatal("VTEncoderSessionSetPixelBufferAttributes", "NULL session");
	CFDictionaryRef old = s->pixelAttributes;
	s->pixelAttributes = attributes ? (CFDictionaryRef)CFRetain(attributes) : NULL;
	if (old)
		CFRelease((CFTypeRef)old);
	return 0;
}

OSStatus VTEncoderSessionEmitEncodedFrame(VTVideoEncoderSession session, VTVideoEncoderFrame frame,
                                          OSStatus status, uint32_t infoFlags, CMSampleBufferRef sampleBuffer)
{
	struct n2fake_session *s = (struct n2fake_session *)session;
	struct n2fake_frame_token *token = (struct n2fake_frame_token *)(uintptr_t)frame;
	struct n2fake_frame_token *known = NULL;
	struct n2fake_emit *e;
	size_t i;

	if (!s)
		n2fake_object_fatal("VTEncoderSessionEmitEncodedFrame", "NULL session");
	for (i = 0; i < s->frameCount; i++) {
		if (s->frames[i] == token) {
			known = token;
			break;
		}
	}
	if (!known || known->emitted)
		s->protocolErrors++;
	else
		known->emitted = 1;
	if (s->emitCount == s->emitCapacity) {
		size_t cap = s->emitCapacity ? s->emitCapacity * 2 : 8;
		struct n2fake_emit *p = (struct n2fake_emit *)realloc(s->emits, cap * sizeof(*p));
		if (!p)
			n2fake_object_fatal("VTEncoderSessionEmitEncodedFrame", "out of memory growing emits");
		s->emits = p;
		s->emitCapacity = cap;
	}
	e = &s->emits[s->emitCount++];
	e->status = status;
	e->flags = infoFlags;
	e->sbuf = sampleBuffer ? (CMSampleBufferRef)CFRetain(sampleBuffer) : NULL;
	e->token = token;
	return 0;
}
