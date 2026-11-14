/* SPDX-License-Identifier: MIT */
/*
 * t_apple_dec.c -- host-test Apple runtime for DEC06 (see t_apple_dec.h).  Small on purpose: only the calls the decoder
 * object and its tests make, with the semantics n2_apple.h and n2vt_dec_abi.h describe.  Every object is refcounted;
 * n2vtdt_live_objects() is checked at the end of the suite so a leak in the object shows up as a test failure.
 */
#define _POSIX_C_SOURCE 200809L
#include "t_apple_dec.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	T_STR = 1, T_NUM, T_BOOL, T_DICT, T_ARRAY, T_DATA, T_BLOCK, T_SAMPLE, T_FMT, T_PB, T_POOL, T_CMOBJ,
};

struct tobj {
	int type;
	int refs;
};

struct tstr {
	struct tobj o;
	char *s;
};

struct tnum {
	struct tobj o;
	int32_t v;
};

struct tbool {
	struct tobj o;
	int v;
};

struct tdict {
	struct tobj o;
	size_t n, cap;
	const void **keys, **vals;
};

struct tarray {
	struct tobj o;
	size_t n, cap;
	const void **vals;
};

struct tdata {
	struct tobj o;
	size_t n;
	uint8_t *bytes;
};

struct tblock {
	struct tobj o;
	size_t n;
	uint8_t *bytes;
};

struct tsample {
	struct tobj o;
	CMBlockBufferRef block;
	CMFormatDescriptionRef fmt;
	CMTime pts;
};

struct tfmt {
	struct tobj o;
	uint32_t codec;
	CFMutableDictionaryRef extensions;       /* may be NULL */
	struct { uint8_t *data; size_t size; } ps[16];
	size_t nps;
	size_t nal_length_size;
};

struct tpb {
	struct tobj o;
	size_t w, h;
	uint32_t format;
	size_t planes;
	struct {
		uint8_t *base;
		size_t stride, rows, bytes_used;
	} p[3];
	int lock;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct tstr *g_interned[256];
static size_t g_ninterned;
static _Atomic long g_live;
static struct tbool g_true = { { T_BOOL, 1 }, 1 };
static struct tbool g_false = { { T_BOOL, 1 }, 0 };
static struct tobj g_alloc = { 0, 1 };
static struct tobj g_pool = { T_POOL, 1 };

static void t_destroy(struct tobj *o);
static void t_retain(const void *p);
static void t_cmobj_release_hook(struct tobj *o);
static void t_release(const void *p);

/* ---- console capture (the test's strong n2vt_say) -------------------------------------------------------------- */

#define T_LOG_LINES 32
#define T_LOG_BYTES 512
static char g_log[T_LOG_LINES][T_LOG_BYTES];
static int g_log_count;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

void n2vt_say(const char *format, ...)
{
	va_list ap;

	pthread_mutex_lock(&g_log_lock);
	if (g_log_count < T_LOG_LINES) {
		va_start(ap, format);
		(void)vsnprintf(g_log[g_log_count], T_LOG_BYTES, format, ap);
		va_end(ap);
		g_log_count++;
	}
	pthread_mutex_unlock(&g_log_lock);
}

void n2vtdt_log_reset(void)
{
	pthread_mutex_lock(&g_log_lock);
	g_log_count = 0;
	pthread_mutex_unlock(&g_log_lock);
}

int n2vtdt_log_count(void)
{
	return g_log_count;
}

const char *n2vtdt_log_line(int index)
{
	return (index >= 0 && index < g_log_count) ? g_log[index] : NULL;
}

int n2vtdt_log_contains(const char *needle)
{
	int i;

	for (i = 0; i < g_log_count; i++)
		if (strstr(g_log[i], needle) != NULL)
			return 1;
	return 0;
}

/* ---- CF plumbing ----------------------------------------------------------------------------------------------- */

static struct tobj *t_check(const void *p, int type, const char *who)
{
	struct tobj *o = (struct tobj *)p;

	if (!p) {
		fprintf(stderr, "t_apple_dec: %s: NULL object\n", who);
		abort();
	}
	if (o->type != type) {
		fprintf(stderr, "t_apple_dec: %s: object type %d, want %d\n", who, o->type, type);
		abort();
	}
	return o;
}

static void t_retain(const void *p)
{
	struct tobj *o = (struct tobj *)p;

	if (!o || o->refs < 0)
		return;                                   /* interned/static objects have refs < 0 */
	o->refs++;
}

static void t_release(const void *p)
{
	struct tobj *o = (struct tobj *)p;

	if (!o || o->refs < 0)
		return;
	if (--o->refs == 0)
		t_destroy(o);
}

static void t_destroy(struct tobj *o)
{
	if (o->type == T_CMOBJ) {
		t_cmobj_release_hook(o);
		g_live--;
		free(o);
		return;
	}
	switch (o->type) {
	case T_DICT: {
		struct tdict *d = (struct tdict *)o;
		size_t i;
		for (i = 0; i < d->n; i++) {
			t_release(d->keys[i]);
			t_release(d->vals[i]);
		}
		free((void *)d->keys);
		free((void *)d->vals);
		break;
	}
	case T_ARRAY: {
		struct tarray *a = (struct tarray *)o;
		size_t i;
		for (i = 0; i < a->n; i++)
			t_release(a->vals[i]);
		free((void *)a->vals);
		break;
	}
	case T_DATA:
		free(((struct tdata *)o)->bytes);
		break;
	case T_BLOCK:
		free(((struct tblock *)o)->bytes);
		break;
	case T_SAMPLE: {
		struct tsample *s = (struct tsample *)o;
		t_release(s->block);
		t_release(s->fmt);
		break;
	}
	case T_FMT: {
		struct tfmt *f = (struct tfmt *)o;
		size_t i;
		for (i = 0; i < f->nps; i++)
			free(f->ps[i].data);
		t_release(f->extensions);
		break;
	}
	case T_PB: {
		struct tpb *p = (struct tpb *)o;
		size_t i;
		for (i = 0; i < p->planes; i++)
			free(p->p[i].base);
		break;
	}
	default:
		break;
	}
	g_live--;
	free(o);
}

long n2vtdt_live_objects(void)
{
	return g_live;
}

CFStringRef n2vtdt_cfstr(const char *s)
{
	size_t i;

	pthread_mutex_lock(&g_lock);
	for (i = 0; i < g_ninterned; i++) {
		if (strcmp(g_interned[i]->s, s) == 0) {
			pthread_mutex_unlock(&g_lock);
			return (CFStringRef)g_interned[i];
		}
	}
	if (g_ninterned < sizeof(g_interned) / sizeof(g_interned[0])) {
		struct tstr *st = (struct tstr *)calloc(1, sizeof(*st));
		st->o.type = T_STR;
		st->o.refs = -1;
		st->s = strdup(s);
		g_interned[g_ninterned++] = st;
		pthread_mutex_unlock(&g_lock);
		return (CFStringRef)st;
	}
	pthread_mutex_unlock(&g_lock);
	return NULL;
}

CFStringRef n2fake_cfstr(const char *s)
{
	return n2vtdt_cfstr(s);
}

const CFAllocatorRef kCFAllocatorDefault = (CFAllocatorRef)&g_alloc;
const CFAllocatorRef kCFAllocatorNull = (CFAllocatorRef)&g_alloc;
const CFBooleanRef kCFBooleanTrue = (CFBooleanRef)&g_true;
const CFBooleanRef kCFBooleanFalse = (CFBooleanRef)&g_false;
const CFDictionaryKeyCallBacks kCFTypeDictionaryKeyCallBacks = { 0, NULL, NULL, NULL, NULL, NULL };
const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks = { 0, NULL, NULL, NULL, NULL };
const CFArrayCallBacks kCFTypeArrayCallBacks = { 0, NULL, NULL, NULL, NULL };

CFTypeRef CFRetain(CFTypeRef cf)
{
	t_retain(cf);
	return cf;
}

void CFRelease(CFTypeRef cf)
{
	t_release(cf);
}

CFTypeID CFGetTypeID(CFTypeRef cf)
{
	struct tobj *o = (struct tobj *)cf;

	if (!o)
		return 0;
	switch (o->type) {
	case T_STR:
		return 1;
	case T_NUM:
		return 2;
	case T_BOOL:
		return 3;
	case T_DICT:
		return 4;
	case T_ARRAY:
		return 5;
	case T_DATA:
		return 6;
	default:
		return 100 + (CFTypeID)o->type;
	}
}

CFTypeID CFNumberGetTypeID(void)
{
	return 2;
}
CFTypeID CFBooleanGetTypeID(void)
{
	return 3;
}
CFTypeID CFStringGetTypeID(void)
{
	return 1;
}
CFTypeID CFDictionaryGetTypeID(void)
{
	return 4;
}
CFTypeID CFArrayGetTypeID(void)
{
	return 5;
}
CFTypeID CFDataGetTypeID(void)
{
	return 6;
}

Boolean CFEqual(CFTypeRef a, CFTypeRef b)
{
	struct tobj *x = (struct tobj *)a, *y = (struct tobj *)b;

	if (a == b)
		return 1;
	if (!x || !y || x->type != y->type)
		return 0;
	switch (x->type) {
	case T_STR:
		return strcmp(((struct tstr *)x)->s, ((struct tstr *)y)->s) == 0;
	case T_NUM:
		return ((struct tnum *)x)->v == ((struct tnum *)y)->v;
	case T_BOOL:
		return ((struct tbool *)x)->v == ((struct tbool *)y)->v;
	default:
		return 0;
	}
}

CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef a, CFIndex capacity, const CFDictionaryKeyCallBacks *k,
                                                 const CFDictionaryValueCallBacks *v)
{
	struct tdict *d;

	(void)a;
	(void)capacity;
	(void)k;
	(void)v;
	d = (struct tdict *)calloc(1, sizeof(*d));
	d->o.type = T_DICT;
	d->o.refs = 1;
	g_live++;
	return (CFMutableDictionaryRef)d;
}

static int t_dict_find(struct tdict *d, const void *key)
{
	size_t i;

	for (i = 0; i < d->n; i++)
		if (CFEqual(d->keys[i], key))
			return (int)i;
	return -1;
}

void CFDictionarySetValue(CFMutableDictionaryRef dict, const void *key, const void *value)
{
	struct tdict *d = (struct tdict *)t_check(dict, T_DICT, "CFDictionarySetValue");
	int i = t_dict_find(d, key);

	if (i >= 0) {
		t_retain(value);
		t_release(d->vals[i]);
		d->vals[i] = value;
		return;
	}
	if (d->n == d->cap) {
		size_t cap = d->cap ? d->cap * 2 : 8;
		d->keys = (const void **)realloc((void *)d->keys, cap * sizeof(*d->keys));
		d->vals = (const void **)realloc((void *)d->vals, cap * sizeof(*d->vals));
		d->cap = cap;
	}
	t_retain(key);
	t_retain(value);
	d->keys[d->n] = key;
	d->vals[d->n] = value;
	d->n++;
}

const void *CFDictionaryGetValue(CFDictionaryRef dict, const void *key)
{
	struct tdict *d = (struct tdict *)t_check(dict, T_DICT, "CFDictionaryGetValue");
	int i = t_dict_find(d, key);

	return i >= 0 ? d->vals[i] : NULL;
}

CFIndex n2vtdt_dictionary_count(CFDictionaryRef dict)
{
	return (CFIndex)((struct tdict *)t_check(dict, T_DICT, "dictionary_count"))->n;
}

const void *n2vtdt_dictionary_key_at(CFDictionaryRef dict, CFIndex i)
{
	struct tdict *d = (struct tdict *)t_check(dict, T_DICT, "dictionary_key_at");

	return (i >= 0 && (size_t)i < d->n) ? d->keys[i] : NULL;
}

const void *n2vtdt_dictionary_value_at(CFDictionaryRef dict, CFIndex i)
{
	struct tdict *d = (struct tdict *)t_check(dict, T_DICT, "dictionary_value_at");

	return (i >= 0 && (size_t)i < d->n) ? d->vals[i] : NULL;
}

const void *n2vtdt_dictionary_get(CFDictionaryRef dict, const char *key)
{
	return CFDictionaryGetValue(dict, n2vtdt_cfstr(key));
}

CFMutableArrayRef CFArrayCreateMutable(CFAllocatorRef a, CFIndex capacity, const CFArrayCallBacks *cb)
{
	struct tarray *arr;

	(void)a;
	(void)capacity;
	(void)cb;
	arr = (struct tarray *)calloc(1, sizeof(*arr));
	arr->o.type = T_ARRAY;
	arr->o.refs = 1;
	g_live++;
	return (CFMutableArrayRef)arr;
}

void CFArrayAppendValue(CFMutableArrayRef arr, const void *value)
{
	struct tarray *a = (struct tarray *)t_check(arr, T_ARRAY, "CFArrayAppendValue");

	if (a->n == a->cap) {
		a->cap = a->cap ? a->cap * 2 : 8;
		a->vals = (const void **)realloc((void *)a->vals, a->cap * sizeof(*a->vals));
	}
	t_retain(value);
	a->vals[a->n++] = value;
}

CFIndex CFArrayGetCount(CFArrayRef arr)
{
	return (CFIndex)((struct tarray *)t_check(arr, T_ARRAY, "CFArrayGetCount"))->n;
}

const void *CFArrayGetValueAtIndex(CFArrayRef arr, CFIndex i)
{
	struct tarray *a = (struct tarray *)t_check(arr, T_ARRAY, "CFArrayGetValueAtIndex");

	return (i >= 0 && (size_t)i < a->n) ? a->vals[i] : NULL;
}

CFNumberRef CFNumberCreate(CFAllocatorRef a, CFIndex type, const void *valuePtr)
{
	struct tnum *n;

	(void)a;
	if (type != kCFNumberSInt32Type && type != kCFNumberSInt64Type)
		return NULL;
	n = (struct tnum *)calloc(1, sizeof(*n));
	n->o.type = T_NUM;
	n->o.refs = 1;
	n->v = (type == kCFNumberSInt32Type) ? *(const int32_t *)valuePtr : (int32_t)*(const int64_t *)valuePtr;
	g_live++;
	return (CFNumberRef)n;
}

Boolean CFNumberGetValue(CFNumberRef n, CFIndex type, void *valuePtr)
{
	struct tnum *x = (struct tnum *)t_check(n, T_NUM, "CFNumberGetValue");

	if (type == kCFNumberSInt32Type)
		*(int32_t *)valuePtr = x->v;
	else if (type == kCFNumberSInt64Type)
		*(int64_t *)valuePtr = x->v;
	else
		return 0;
	return 1;
}

Boolean CFBooleanGetValue(CFBooleanRef b)
{
	return (Boolean)((struct tbool *)t_check(b, T_BOOL, "CFBooleanGetValue"))->v;
}

CFStringRef CFStringCreateWithCString(CFAllocatorRef a, const char *s, uint32_t encoding)
{
	(void)a;
	(void)encoding;
	return n2vtdt_cfstr(s);
}

Boolean CFStringGetCString(CFStringRef s, char *buf, CFIndex size, uint32_t encoding)
{
	struct tstr *x = (struct tstr *)t_check(s, T_STR, "CFStringGetCString");

	(void)encoding;
	if ((CFIndex)strlen(x->s) >= size)
		return 0;
	strcpy(buf, x->s);
	return 1;
}

CFDataRef n2vtdt_data_create(const void *bytes, size_t n)
{
	struct tdata *d = (struct tdata *)calloc(1, sizeof(*d));

	d->o.type = T_DATA;
	d->o.refs = 1;
	d->n = n;
	d->bytes = (uint8_t *)malloc(n ? n : 1);
	memcpy(d->bytes, bytes, n);
	g_live++;
	return (CFDataRef)d;
}

const uint8_t *CFDataGetBytePtr(CFDataRef data)
{
	return ((struct tdata *)t_check(data, T_DATA, "CFDataGetBytePtr"))->bytes;
}

CFIndex CFDataGetLength(CFDataRef data)
{
	return (CFIndex)((struct tdata *)t_check(data, T_DATA, "CFDataGetLength"))->n;
}

int32_t n2vtdt_number_int32(CFTypeRef n, int32_t *out)
{
	struct tnum *x = (struct tnum *)t_check(n, T_NUM, "number_int32");

	*out = x->v;
	return 0;
}

/* ---- CoreMedia ------------------------------------------------------------------------------------------------- */

OSStatus CMBlockBufferCreateWithMemoryBlock(CFAllocatorRef structureAllocator, void *memoryBlock, size_t blockLength,
                                            CFAllocatorRef blockAllocator, const void *customBlockSource,
                                            size_t offsetToData, size_t dataLength, uint32_t flags,
                                            CMBlockBufferRef *blockBufferOut)
{
	struct tblock *b;

	(void)structureAllocator;
	(void)blockAllocator;
	(void)customBlockSource;
	(void)offsetToData;
	(void)flags;
	if (!blockBufferOut)
		return -12712;
	b = (struct tblock *)calloc(1, sizeof(*b));
	b->o.type = T_BLOCK;
	b->o.refs = 1;
	b->n = dataLength ? dataLength : blockLength;
	b->bytes = (uint8_t *)malloc(b->n ? b->n : 1);
	if (memoryBlock)
		memcpy(b->bytes, memoryBlock, b->n);
	g_live++;
	*blockBufferOut = (CMBlockBufferRef)b;
	return 0;
}

OSStatus CMBlockBufferReplaceDataBytes(const void *sourceBytes, CMBlockBufferRef dst, size_t offsetIntoDestination,
                                       size_t dataLength)
{
	struct tblock *b = (struct tblock *)t_check(dst, T_BLOCK, "CMBlockBufferReplaceDataBytes");

	if (offsetIntoDestination + dataLength > b->n)
		return -12712;
	memcpy(b->bytes + offsetIntoDestination, sourceBytes, dataLength);
	return 0;
}

size_t CMBlockBufferGetDataLength(CMBlockBufferRef block)
{
	return ((struct tblock *)t_check(block, T_BLOCK, "CMBlockBufferGetDataLength"))->n;
}

OSStatus CMBlockBufferCopyDataBytes(CMBlockBufferRef block, size_t offset, size_t length, void *destination)
{
	struct tblock *b = (struct tblock *)t_check(block, T_BLOCK, "CMBlockBufferCopyDataBytes");

	if (offset + length > b->n)
		return -12712;
	memcpy(destination, b->bytes + offset, length);
	return 0;
}

OSStatus CMSampleBufferCreateReady(CFAllocatorRef a, CMBlockBufferRef dataBuffer, CMFormatDescriptionRef fmt,
                                   CFIndex numSamples, CFIndex numSampleTimingEntries, const CMSampleTimingInfo *timing,
                                   CFIndex numSampleSizeEntries, const size_t *sampleSizeArray,
                                   CMSampleBufferRef *out)
{
	struct tsample *s;

	(void)a;
	(void)numSamples;
	(void)numSampleSizeEntries;
	(void)sampleSizeArray;
	if (!out)
		return -12712;
	s = (struct tsample *)calloc(1, sizeof(*s));
	s->o.type = T_SAMPLE;
	s->o.refs = 1;
	s->block = dataBuffer;
	t_retain(dataBuffer);
	s->fmt = fmt;
	t_retain(fmt);
	if (numSampleTimingEntries > 0)
		s->pts = timing[0].presentationTimeStamp;
	g_live++;
	*out = (CMSampleBufferRef)s;
	return 0;
}

CMBlockBufferRef CMSampleBufferGetDataBuffer(CMSampleBufferRef sb)
{
	struct tsample *s = (struct tsample *)t_check(sb, T_SAMPLE, "CMSampleBufferGetDataBuffer");

	return s->block;
}

CMFormatDescriptionRef CMSampleBufferGetFormatDescription(CMSampleBufferRef sb)
{
	struct tsample *s = (struct tsample *)t_check(sb, T_SAMPLE, "CMSampleBufferGetFormatDescription");

	return s->fmt;
}

CMTime CMSampleBufferGetPresentationTimeStamp(CMSampleBufferRef sb)
{
	return ((struct tsample *)t_check(sb, T_SAMPLE, "CMSampleBufferGetPresentationTimeStamp"))->pts;
}

CFArrayRef CMSampleBufferGetSampleAttachmentsArray(CMSampleBufferRef sb, Boolean createIfNecessary)
{
	(void)sb;
	(void)createIfNecessary;
	return NULL;
}

const void *n2vtdt_sample_data(CMSampleBufferRef sb, size_t *bytes)
{
	struct tsample *s = (struct tsample *)t_check(sb, T_SAMPLE, "sample_data");
	struct tblock *b = (struct tblock *)t_check(s->block, T_BLOCK, "sample_data block");

	if (bytes)
		*bytes = b->n;
	return b->bytes;
}

CMTime n2vtdt_sample_pts(CMSampleBufferRef sb)
{
	return ((struct tsample *)t_check(sb, T_SAMPLE, "sample_pts"))->pts;
}

int n2vtdt_sample_create(CMFormatDescriptionRef fmt, const void *bytes, size_t n, int64_t pts, CMSampleBufferRef *out)
{
	CMBlockBufferRef block = NULL;
	CMSampleTimingInfo timing;
	OSStatus st;

	if (!out || (!bytes && n))
		return -1;
	st = CMBlockBufferCreateWithMemoryBlock(NULL, NULL, n, NULL, NULL, 0, n, 1, &block);
	if (st != 0)
		return -1;
	if (n)
		CMBlockBufferReplaceDataBytes(bytes, block, 0, n);
	memset(&timing, 0, sizeof(timing));
	timing.presentationTimeStamp.value = pts;
	timing.presentationTimeStamp.timescale = 1000;
	timing.presentationTimeStamp.flags = N2_CMTIME_VALID;
	timing.decodeTimeStamp = timing.presentationTimeStamp;
	st = CMSampleBufferCreateReady(NULL, block, fmt, 1, 1, &timing, 1, &n, out);
	CFRelease((CFTypeRef)block);
	return st == 0 ? 0 : -1;
}

static CMFormatDescriptionRef t_fmt_alloc(uint32_t codec)
{
	struct tfmt *f = (struct tfmt *)calloc(1, sizeof(*f));

	f->o.type = T_FMT;
	f->o.refs = 1;
	f->codec = codec;
	f->nal_length_size = 4;
	g_live++;
	return (CMFormatDescriptionRef)f;
}

static int t_fmt_add_ps(struct tfmt *f, const uint8_t *data, size_t n)
{
	if (f->nps >= sizeof(f->ps) / sizeof(f->ps[0]) || n == 0)
		return -1;
	f->ps[f->nps].data = (uint8_t *)malloc(n);
	memcpy(f->ps[f->nps].data, data, n);
	f->ps[f->nps].size = n;
	f->nps++;
	return 0;
}

/* Split the parameter sets out of an avcC/hvcC record for the accessor fallback; the atom route is still the primary. */
static void t_fmt_fill_from_record(struct tfmt *f, const uint8_t *rec, size_t bytes)
{
	size_t i = 0;

	if (f->codec == 0x31637661u) {            /* 'avc1' */
		unsigned num_sps, num_pps, k;
		if (bytes < 7 || rec[0] != 1)
			return;
		f->nal_length_size = (size_t)(rec[4] & 3) + 1;
		i = 5;
		num_sps = rec[i++] & 0x1f;
		for (k = 0; k < num_sps && i + 2 <= bytes; k++) {
			unsigned len = ((unsigned)rec[i] << 8) | rec[i + 1];
			i += 2;
			if (i + len > bytes)
				return;
			t_fmt_add_ps(f, rec + i, len);
			i += len;
		}
		if (i >= bytes)
			return;
		num_pps = rec[i++];
		for (k = 0; k < num_pps && i + 2 <= bytes; k++) {
			unsigned len = ((unsigned)rec[i] << 8) | rec[i + 1];
			i += 2;
			if (i + len > bytes)
				return;
			t_fmt_add_ps(f, rec + i, len);
			i += len;
		}
	} else {                                  /* 'hvc1' */
		size_t p;
		if (bytes < 23 || rec[0] != 1)
			return;
		f->nal_length_size = (size_t)(rec[21] & 3) + 1;
		p = 23;
		{
			unsigned arrays = rec[22], a;
			for (a = 0; a < arrays && p + 3 <= bytes; a++) {
				unsigned type = rec[p] & 0x3f, nals, k;
				nals = ((unsigned)rec[p + 1] << 8) | rec[p + 2];
				p += 3;
				for (k = 0; k < nals && p + 2 <= bytes; k++) {
					unsigned len = ((unsigned)rec[p] << 8) | rec[p + 1];
					p += 2;
					if (p + len > bytes)
						return;
					if (type == 32 || type == 33 || type == 34)
						t_fmt_add_ps(f, rec + p, len);
					p += len;
				}
			}
		}
	}
}

int n2vtdt_format_with_atoms(uint32_t codec, const uint8_t *record, size_t record_bytes, CMFormatDescriptionRef *fmtOut)
{
	struct tfmt *f;
	CFMutableDictionaryRef atoms;

	if (!fmtOut || !record)
		return -1;
	f = (struct tfmt *)t_fmt_alloc(codec);
	t_fmt_fill_from_record(f, record, record_bytes);
	atoms = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	{
		CFDataRef data = n2vtdt_data_create(record, record_bytes);
		CFDictionarySetValue(atoms, n2vtdt_cfstr(codec == 0x31637661u ? "avcC" : "hvcC"), data);
		CFRelease((CFTypeRef)data);
	}
	f->extensions = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	                                          &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(f->extensions, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms, atoms);
	CFRelease(atoms);
	*fmtOut = (CMFormatDescriptionRef)f;
	return 0;
}

int n2vtdt_format_with_params(uint32_t codec, const uint8_t *const *ps, const size_t *sizes, size_t count,
                              size_t nal_length_size, CMFormatDescriptionRef *fmtOut)
{
	struct tfmt *f;
	size_t i;

	if (!fmtOut || !ps || !sizes || count == 0)
		return -1;
	f = (struct tfmt *)t_fmt_alloc(codec);
	f->nal_length_size = nal_length_size;
	for (i = 0; i < count; i++)
		if (t_fmt_add_ps(f, ps[i], sizes[i]) != 0)
			return -1;
	*fmtOut = (CMFormatDescriptionRef)f;
	return 0;
}

OSStatus CMVideoFormatDescriptionCreateFromH264ParameterSets(CFAllocatorRef a, size_t count,
                                                             const uint8_t *const *ps, const size_t *sizes,
                                                             int nal_header_length, CMFormatDescriptionRef *out)
{
	(void)a;
	(void)nal_header_length;
	return n2vtdt_format_with_params(0x31637661u, ps, sizes, count, 4, out) == 0 ? 0 : -12712;
}

OSStatus CMVideoFormatDescriptionGetH264ParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index,
                                                           const uint8_t **psOut, size_t *sizeOut, size_t *countOut,
                                                           int *nalLengthOut)
{
	struct tfmt *f = (struct tfmt *)t_check(fmt, T_FMT, "GetH264ParameterSetAtIndex");

	if (f->codec != 0x31637661u || index >= f->nps)
		return -12712;
	if (psOut)
		*psOut = f->ps[index].data;
	if (sizeOut)
		*sizeOut = f->ps[index].size;
	if (countOut)
		*countOut = f->nps;
	if (nalLengthOut)
		*nalLengthOut = (int)f->nal_length_size;
	return 0;
}

OSStatus CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index,
                                                           const uint8_t **psOut, size_t *sizeOut, size_t *countOut,
                                                           int *nalLengthOut)
{
	struct tfmt *f = (struct tfmt *)t_check(fmt, T_FMT, "GetHEVCParameterSetAtIndex");

	if (f->codec != 0x31637668u || index >= f->nps)
		return -12712;
	if (psOut)
		*psOut = f->ps[index].data;
	if (sizeOut)
		*sizeOut = f->ps[index].size;
	if (countOut)
		*countOut = f->nps;
	if (nalLengthOut)
		*nalLengthOut = (int)f->nal_length_size;
	return 0;
}

static struct tstr s_atoms_key = { { T_STR, -1 }, (char *)"SampleDescriptionExtensionAtoms" };
static struct tstr s_pixfmt_key = { { T_STR, -1 }, (char *)"PixelFormatType" };
static struct tstr s_width_key = { { T_STR, -1 }, (char *)"Width" };
static struct tstr s_height_key = { { T_STR, -1 }, (char *)"Height" };
static struct tstr s_iosurface_key = { { T_STR, -1 }, (char *)"IOSurfaceProperties" };
static struct tstr s_vt_hw_key = { { T_STR, -1 }, (char *)"VTIsHardwareAccelerated" };
static struct tstr s_vt_rating_key = { { T_STR, -1 }, (char *)"VTRating" };
const CFStringRef kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms = (CFStringRef)&s_atoms_key;

CFTypeRef CMFormatDescriptionGetExtension(CMFormatDescriptionRef fmt, CFStringRef key)
{
	struct tfmt *f = (struct tfmt *)t_check(fmt, T_FMT, "CMFormatDescriptionGetExtension");

	if (!f->extensions)
		return NULL;
	return (CFTypeRef)CFDictionaryGetValue(f->extensions, key);
}

/* ---- CoreVideo ------------------------------------------------------------------------------------------------- */

const CFStringRef kCVPixelBufferPixelFormatTypeKey = (CFStringRef)&s_pixfmt_key;
const CFStringRef kCVPixelBufferWidthKey = (CFStringRef)&s_width_key;
const CFStringRef kCVPixelBufferHeightKey = (CFStringRef)&s_height_key;
const CFStringRef kCVPixelBufferIOSurfacePropertiesKey = (CFStringRef)&s_iosurface_key;
const CFStringRef kVTVideoDecoder_IsHardwareAccelerated = (CFStringRef)&s_vt_hw_key;
const CFStringRef kVTVideoDecoderInfo_Rating = (CFStringRef)&s_vt_rating_key;

static size_t round64(size_t v)
{
	return (v + 63u) & ~(size_t)63u;
}

int32_t CVPixelBufferCreate(CFAllocatorRef a, size_t width, size_t height, uint32_t pixelFormat,
                            CFDictionaryRef attrs, CVPixelBufferRef *out)
{
	struct tpb *p;

	(void)a;
	(void)attrs;
	if (!out || width == 0 || height == 0)
		return -6661;
	if (pixelFormat != N2_PIXFMT_420V && pixelFormat != N2_PIXFMT_420F && pixelFormat != N2_PIXFMT_I420)
		return -6661;
	p = (struct tpb *)calloc(1, sizeof(*p));
	p->o.type = T_PB;
	p->o.refs = 1;
	p->w = width;
	p->h = height;
	p->format = pixelFormat;
	p->planes = (pixelFormat == N2_PIXFMT_I420) ? 3 : 2;
	p->p[0].stride = round64(width);
	p->p[0].rows = height;
	p->p[0].bytes_used = width;
	if (p->planes == 2) {
		p->p[1].stride = round64(width);
		p->p[1].rows = (height + 1) / 2;
		p->p[1].bytes_used = 2 * ((width + 1) / 2);
	} else {
		p->p[1].stride = round64((width + 1) / 2);
		p->p[1].rows = (height + 1) / 2;
		p->p[1].bytes_used = (width + 1) / 2;
		p->p[2].stride = p->p[1].stride;
		p->p[2].rows = p->p[1].rows;
		p->p[2].bytes_used = p->p[1].bytes_used;
	}
	{
		size_t i;
		for (i = 0; i < p->planes; i++) {
			p->p[i].base = (uint8_t *)calloc(p->p[i].stride, p->p[i].rows);
			if (!p->p[i].base) {
				CFRelease((CFTypeRef)p);
				return -6662;
			}
		}
	}
	g_live++;
	*out = (CVPixelBufferRef)p;
	return 0;
}

int32_t CVPixelBufferLockBaseAddress(CVPixelBufferRef pb, uint64_t flags)
{
	struct tpb *p = (struct tpb *)t_check(pb, T_PB, "CVPixelBufferLockBaseAddress");

	(void)flags;
	p->lock++;
	return 0;
}

int32_t CVPixelBufferUnlockBaseAddress(CVPixelBufferRef pb, uint64_t flags)
{
	struct tpb *p = (struct tpb *)t_check(pb, T_PB, "CVPixelBufferUnlockBaseAddress");

	(void)flags;
	if (p->lock <= 0)
		return -6661;
	p->lock--;
	return 0;
}

void *CVPixelBufferGetBaseAddressOfPlane(CVPixelBufferRef pb, size_t plane)
{
	struct tpb *p = (struct tpb *)t_check(pb, T_PB, "CVPixelBufferGetBaseAddressOfPlane");

	if (plane >= p->planes)
		abort();
	if (p->lock <= 0)
		return NULL;
	return p->p[plane].base;
}

size_t CVPixelBufferGetBytesPerRowOfPlane(CVPixelBufferRef pb, size_t plane)
{
	struct tpb *p = (struct tpb *)t_check(pb, T_PB, "CVPixelBufferGetBytesPerRowOfPlane");

	return plane < p->planes ? p->p[plane].stride : 0;
}

size_t CVPixelBufferGetWidth(CVPixelBufferRef pb)
{
	return ((struct tpb *)t_check(pb, T_PB, "CVPixelBufferGetWidth"))->w;
}

size_t CVPixelBufferGetHeight(CVPixelBufferRef pb)
{
	return ((struct tpb *)t_check(pb, T_PB, "CVPixelBufferGetHeight"))->h;
}

size_t CVPixelBufferGetPlaneCount(CVPixelBufferRef pb)
{
	return ((struct tpb *)t_check(pb, T_PB, "CVPixelBufferGetPlaneCount"))->planes;
}

uint32_t CVPixelBufferGetPixelFormatType(CVPixelBufferRef pb)
{
	return ((struct tpb *)t_check(pb, T_PB, "CVPixelBufferGetPixelFormatType"))->format;
}

size_t n2vtdt_pixel_plane_height(CVPixelBufferRef pb, size_t plane)
{
	struct tpb *p = (struct tpb *)t_check(pb, T_PB, "pixel_plane_height");

	return plane < p->planes ? p->p[plane].rows : 0;
}

size_t n2vtdt_pixel_plane_stride(CVPixelBufferRef pb, size_t plane)
{
	struct tpb *p = (struct tpb *)t_check(pb, T_PB, "pixel_plane_stride");

	return plane < p->planes ? p->p[plane].stride : 0;
}

const uint8_t *n2vtdt_pixel_plane(CVPixelBufferRef pb, size_t plane)
{
	struct tpb *p = (struct tpb *)t_check(pb, T_PB, "pixel_plane");

	return plane < p->planes ? p->p[plane].base : NULL;
}

/* ---- VideoToolbox ---------------------------------------------------------------------------------------------- */

struct tcmobj {
	struct tobj o;
	const n2_VTVideoDecoderVTable *vt;
	CMTypeID classID;
	unsigned char storage[];
};

static void t_cmobj_release_hook(struct tobj *o)
{
	struct tcmobj *c = (struct tcmobj *)o;

	if (c->vt && c->vt->baseClass && c->vt->baseClass->finalize)
		c->vt->baseClass->finalize((CMBaseObjectRef)c);
}

CMTypeID VTVideoDecoderGetClassID(void)
{
	return (CMTypeID)0x6e327664ul;            /* 'n2vd' */
}

CMBaseObjectRef VTVideoDecoderGetCMBaseObject(VTVideoDecoderRef dec)
{
	return (CMBaseObjectRef)dec;
}

OSStatus CMDerivedObjectCreate(CFAllocatorRef allocator, const void *vtable, CMTypeID classID,
                               CMBaseObjectRef *objectOut)
{
	const n2_VTVideoDecoderVTable *vt = (const n2_VTVideoDecoderVTable *)vtable;
	struct tcmobj *c;

	(void)allocator;
	if (!vt || !vt->baseClass || !objectOut)
		return -12780;
	if (classID != VTVideoDecoderGetClassID())
		return -12780;
	c = (struct tcmobj *)calloc(1, sizeof(*c) + vt->baseClass->derivedStorageSize);
	c->o.type = T_CMOBJ;
	c->o.refs = 1;
	c->vt = vt;
	c->classID = classID;
	g_live++;
	*objectOut = (CMBaseObjectRef)c;
	return 0;
}

void *CMBaseObjectGetDerivedStorage(CMBaseObjectRef obj)
{
	struct tcmobj *c = (struct tcmobj *)t_check(obj, T_CMOBJ, "CMBaseObjectGetDerivedStorage");

	return c->storage;
}

/* opus-robust: the vtable pointer CMDerivedObjectCreate was given (CoreMedia's CMBaseObjectGetVTable). */
const void *CMBaseObjectGetVTable(CMBaseObjectRef obj)
{
	struct tcmobj *c = (struct tcmobj *)t_check(obj, T_CMOBJ, "CMBaseObjectGetVTable");

	return c->vt;
}

/* ---- registry -------------------------------------------------------------------------------------------------- */

struct treg {
	uint32_t codec;
	CFDictionaryRef info;
	n2_VTVideoDecoderCreateInstance fn;
};

static struct treg g_reg[32];
static int g_nreg;

static void t_reset_registry(void)
{
	int i;

	for (i = 0; i < g_nreg; i++)
		t_release(g_reg[i].info);
	g_nreg = 0;
}

void n2vtdt_vt_reset(void)
{
	t_reset_registry();
}

OSStatus VTRegisterVideoDecoder(uint32_t codecType, n2_VTVideoDecoderCreateInstance create)
{
	return VTRegisterVideoDecoderWithInfo(codecType, NULL, create);
}

OSStatus VTRegisterVideoDecoderWithInfo(uint32_t codecType, CFDictionaryRef info, n2_VTVideoDecoderCreateInstance create)
{
	if (!create || g_nreg >= (int)(sizeof(g_reg) / sizeof(g_reg[0])))
		return -12780;
	g_reg[g_nreg].codec = codecType;
	g_reg[g_nreg].info = info;
	t_retain(info);
	g_reg[g_nreg].fn = create;
	g_nreg++;
	return 0;
}

int n2vtdt_vt_registered(int index, uint32_t *codec, CFDictionaryRef *info, n2_VTVideoDecoderCreateInstance *fn)
{
	if (index < 0 || index >= g_nreg)
		return 0;
	if (codec)
		*codec = g_reg[index].codec;
	if (info)
		*info = g_reg[index].info;
	if (fn)
		*fn = g_reg[index].fn;
	return 1;
}

/* opus-robust: VideoToolbox's selector as disassembled (0x194cc73d0 -> 0x194cc7408): walk the candidates for the codec
 * type in registration order (the tests register by rating), call each factory as (codecType, allocator, out), move on
 * after a non-zero status or a NULL instance, return the last error (or -12906 for an empty list, -12911 for NULL). */
static _Atomic int g_select_calls;

int n2vtdt_select_calls(void)
{
	return atomic_load(&g_select_calls);
}

OSStatus VTSelectAndCreateVideoDecoderInstance(uint32_t codecType, CFAllocatorRef allocator, CFDictionaryRef spec,
                                               VTVideoDecoderRef *out)
{
	OSStatus st = -12906;
	int i;

	(void)spec;
	atomic_fetch_add(&g_select_calls, 1);
	if (!out)
		return -12903;
	*out = NULL;
	for (i = 0; i < g_nreg; i++) {
		if (g_reg[i].codec != codecType)
			continue;
		st = g_reg[i].fn(codecType, allocator, out);
		if (st == 0 && *out)
			return 0;
		if (st == 0)
			st = -12911;
		*out = NULL;
	}
	return st;
}

/* ---- decoder session driver ------------------------------------------------------------------------------------ */

struct temit {
	OSStatus status;
	uint32_t flags;
	CVImageBufferRef image;                   /* retained */
	int token;
};

struct tframe {
	int index;
	int emitted;
};

struct n2vtdt_session {
	CMBaseObjectRef dec;
	struct tcmobj *obj;
	const n2_VTVideoDecoderVTable *vt;
	CFDictionaryRef attrs;                    /* retained copy of the last SetPixelBufferAttributes */
	struct temit emits[128];
	int nemits;
	int protocol_errors;
	int ntokens;
	struct tframe tokens[64];
};

CFDictionaryRef VTDecoderSessionGetDestinationPixelBufferAttributes(VTVideoDecoderSession session)
{
	(void)session;
	return NULL;
}

CVPixelBufferPoolRef VTDecoderSessionGetPixelBufferPool(VTVideoDecoderSession session)
{
	(void)session;
	return (CVPixelBufferPoolRef)&g_pool;
}

OSStatus VTDecoderSessionSetPixelBufferAttributes(VTVideoDecoderSession session, CFDictionaryRef attributes)
{
	struct n2vtdt_session *s = (struct n2vtdt_session *)session;

	if (!s || !attributes)
		return -12903;
	t_release(s->attrs);
	s->attrs = attributes;
	t_retain(attributes);
	return 0;
}

OSStatus VTDecoderSessionCreatePixelBuffer(VTVideoDecoderSession session, CFDictionaryRef unused,
                                           CVPixelBufferRef *out)
{
	struct n2vtdt_session *s = (struct n2vtdt_session *)session;
	uint32_t fmt = N2_PIXFMT_420V;
	int32_t w = 0, h = 0;
	const void *v;

	(void)unused;
	if (!s || !out)
		return -12903;
	if (s->attrs) {
		v = n2vtdt_dictionary_get(s->attrs, "PixelFormatType");
		if (v)
			n2vtdt_number_int32((CFTypeRef)v, (int32_t *)&fmt);
		v = n2vtdt_dictionary_get(s->attrs, "Width");
		if (v)
			n2vtdt_number_int32((CFTypeRef)v, &w);
		v = n2vtdt_dictionary_get(s->attrs, "Height");
		if (v)
			n2vtdt_number_int32((CFTypeRef)v, &h);
	}
	if (w <= 0 || h <= 0)
		return -12903;
	return CVPixelBufferCreate(NULL, (size_t)w, (size_t)h, fmt, NULL, out);
}

OSStatus VTDecoderSessionEmitDecodedFrame(VTVideoDecoderSession session, VTVideoDecoderFrame frame, OSStatus status,
                                          uint32_t infoFlags, CVImageBufferRef imageBuffer)
{
	struct n2vtdt_session *s = (struct n2vtdt_session *)session;
	struct tframe *t = (struct tframe *)frame;
	struct temit *e;

	if (!s || !t)
		return -12903;
	if (t->index < 0 || t->index >= s->ntokens || t->emitted) {
		s->protocol_errors++;
		return -12902;
	}
	t->emitted = 1;
	if (s->nemits >= (int)(sizeof(s->emits) / sizeof(s->emits[0]))) {
		s->protocol_errors++;
		return -12902;
	}
	e = &s->emits[s->nemits++];
	e->status = status;
	e->flags = infoFlags;
	e->token = t->index;
	e->image = NULL;
	if (status == 0 && !(infoFlags & N2_kVTDecodeInfo_FrameDropped) && imageBuffer) {
		e->image = imageBuffer;
		t_retain(imageBuffer);
	}
	return 0;
}

OSStatus VTDecoderSessionTrace(VTVideoDecoderSession session, const char *fmt, ...)
{
	(void)session;
	(void)fmt;
	return 0;
}

struct n2vtdt_session *n2vtdt_session_create(uint32_t codec, CMFormatDescriptionRef fmt, OSStatus *statusOut)
{
	return n2vtdt_session_create_props(codec, fmt, NULL, NULL, 0, statusOut);
}

/* VTDecompressionSessionCreateWithOptions: select, base SetProperty for each pair (VT makes two such calls between the
 * factory and StartSession, n2vt_dec_abi.h), then StartSession. */
struct n2vtdt_session *n2vtdt_session_create_props(uint32_t codec, CMFormatDescriptionRef fmt, const char *const *keys,
                                                   const CFTypeRef *values, int nprops, OSStatus *statusOut)
{
	struct n2vtdt_session *s;
	struct tcmobj *obj = NULL;
	OSStatus st = -12906;
	int i;

	if (statusOut)
		*statusOut = st;
	for (i = 0; i < g_nreg; i++) {
		if (g_reg[i].codec != codec)
			continue;
		st = g_reg[i].fn(codec, NULL, (VTVideoDecoderRef *)&obj);
		if (st == 0 && obj)
			break;
		obj = NULL;
	}
	if (!obj) {
		if (statusOut)
			*statusOut = st != 0 ? st : -12911;
		return NULL;
	}
	s = (struct n2vtdt_session *)calloc(1, sizeof(*s));
	s->dec = (CMBaseObjectRef)obj;
	s->obj = obj;
	s->vt = obj->vt;
	s->ntokens = 0;
	for (i = 0; i < nprops; i++)
		if (s->vt->baseClass->setProperty)
			(void)s->vt->baseClass->setProperty(s->dec, n2vtdt_cfstr(keys[i]), values[i]);
	st = s->vt->decoderClass->startSession((VTVideoDecoderRef)obj, (VTVideoDecoderSession)s, fmt);
	if (st != 0) {
		if (statusOut)
			*statusOut = st;
		CFRelease((CFTypeRef)obj);
		free(s);
		return NULL;
	}
	if (statusOut)
		*statusOut = 0;
	return s;
}

OSStatus n2vtdt_session_decode(struct n2vtdt_session *s, CMSampleBufferRef sb, uint32_t flags, uint32_t *infoOut)
{
	struct tframe *t;

	if (!s)
		return -12903;
	if (s->ntokens >= (int)(sizeof(s->tokens) / sizeof(s->tokens[0])))
		return -12903;
	t = &s->tokens[s->ntokens++];
	t->index = s->ntokens - 1;
	t->emitted = 0;
	return s->vt->decoderClass->decodeFrame((VTVideoDecoderRef)s->dec, (VTVideoDecoderFrame)t, sb, flags, infoOut);
}

OSStatus n2vtdt_session_finish(struct n2vtdt_session *s)
{
	if (!s)
		return -12903;
	return s->vt->decoderClass->finishDelayedFrames((VTVideoDecoderRef)s->dec);
}

OSStatus n2vtdt_session_copy_property(struct n2vtdt_session *s, CFStringRef key, CFTypeRef *valueOut)
{
	if (!s)
		return -12903;
	return s->vt->baseClass->copyProperty((CMBaseObjectRef)s->dec, key, NULL, valueOut);
}

OSStatus n2vtdt_session_can_accept(struct n2vtdt_session *s, CMFormatDescriptionRef fmt)
{
	Boolean b = s->vt->decoderClass->canAcceptFormatDescription((VTVideoDecoderRef)s->dec, fmt);

	return b ? 0 : -12916;
}

OSStatus n2vtdt_session_set_properties(struct n2vtdt_session *s, CFDictionaryRef props)
{
	return s->vt->decoderClass->setProperties((VTVideoDecoderRef)s->dec, props);
}

OSStatus n2vtdt_session_copy_supported(struct n2vtdt_session *s, CFDictionaryRef *out)
{
	return s->vt->decoderClass->copySupportedPropertyDictionary((VTVideoDecoderRef)s->dec, out);
}

const n2_VTVideoDecoderClass *n2vtdt_session_class(struct n2vtdt_session *s)
{
	return s->vt->decoderClass;
}

CMBaseObjectRef n2vtdt_session_object(struct n2vtdt_session *s)
{
	return s->dec;
}

const n2_CMBaseClass *n2vtdt_session_base(struct n2vtdt_session *s)
{
	return s->vt->baseClass;
}

CFDictionaryRef n2vtdt_session_pixel_attributes(struct n2vtdt_session *s)
{
	return s->attrs;
}

int n2vtdt_session_emitted(struct n2vtdt_session *s, int index, OSStatus *status, uint32_t *flags,
                           CVImageBufferRef *image, int *tokenIndex)
{
	if (!s || index < 0 || index >= s->nemits)
		return 0;
	if (status)
		*status = s->emits[index].status;
	if (flags)
		*flags = s->emits[index].flags;
	if (image)
		*image = s->emits[index].image;
	if (tokenIndex)
		*tokenIndex = s->emits[index].token;
	return 1;
}

int n2vtdt_session_protocol_errors(struct n2vtdt_session *s)
{
	return s ? s->protocol_errors : -1;
}

int n2vtdt_session_emit_count(struct n2vtdt_session *s)
{
	return s ? s->nemits : 0;
}

int n2vtdt_session_token_emitted(struct n2vtdt_session *s, int tokenIndex)
{
	int i, n = 0;

	for (i = 0; i < s->nemits; i++)
		if (s->emits[i].token == tokenIndex)
			n++;
	return n == 1;
}

void n2vtdt_session_destroy(struct n2vtdt_session *s)
{
	int i;

	if (!s)
		return;
	for (i = 0; i < s->nemits; i++)
		t_release(s->emits[i].image);
	t_release(s->attrs);
	CFRelease((CFTypeRef)s->obj);
	free(s);
}

