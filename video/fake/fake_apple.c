/* SPDX-License-Identifier: MIT */
/*
 * fake_apple.c -- host-test implementation of the subset of CoreFoundation, CoreMedia and CoreVideo declared in
 * include/n2_apple.h.  Compiled with -DN2_HOST_FAKE; never part of the iOS dylib.
 *
 * Design: every object starts with struct n2fake_base (magic, type tag, atomic refcount, flags).  Allocation through
 * n2fake_object_alloc() bumps a process-wide live counter; CFRelease() at zero calls the type's destructor (and the
 * optional destroy_extra hook used by the CMBaseObject in fake_vt.c).  Interned strings and the four static singletons
 * are process-lifetime and exempt from the counter, so a test can assert that a session left nothing behind.
 */
#define N2FAKE_BUILD 1
#include "fake_apple.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------------------------ */
/* Object header and global state                                                                                     */
/* ------------------------------------------------------------------------------------------------------------------ */

#define N2FAKE_ROUND64(v) (((v) + (size_t)63) & ~(size_t)63)

static _Atomic long g_live_objects;

struct n2fake_allocator { struct n2fake_base base; };

struct n2fake_string {
	struct n2fake_base base;
	char *cstr;
};

struct n2fake_number {
	struct n2fake_base base;
	int      is_double;
	int64_t  i64;
	double   dbl;
};

struct n2fake_boolean {
	struct n2fake_base base;
	int value;
};

struct n2fake_dict_entry {
	const void *key;
	const void *value;
};

struct n2fake_dictionary {
	struct n2fake_base base;
	size_t count;
	size_t capacity;
	struct n2fake_dict_entry *entries;
};

struct n2fake_array {
	struct n2fake_base base;
	size_t count;
	size_t capacity;
	const void **values;
};

struct n2fake_blockbuffer {
	struct n2fake_base base;
	uint8_t *memory;
	size_t blockLength;
	size_t offset;
	size_t dataLength;
	int owned;
};

struct n2fake_param {
	uint8_t *bytes;
	size_t size;
};

struct n2fake_formatdesc {
	struct n2fake_base base;
	uint32_t codec;
	int nalLength;
	size_t count;
	struct n2fake_param *params;
};

struct n2fake_samplebuffer {
	struct n2fake_base base;
	CMBlockBufferRef data;
	CMFormatDescriptionRef fmt;
	size_t numSamples;
	CMSampleTimingInfo timing;
	size_t *sampleSizes;
	size_t numSampleSizes;
	CFMutableArrayRef attachments;
};

struct n2fake_plane {
	uint8_t *base;
	size_t stride;
	size_t rows;
	size_t bytesUsed;
	size_t size;
};

struct n2fake_pixelbuffer {
	struct n2fake_base base;
	size_t width;
	size_t height;
	uint32_t format;
	size_t planeCount;
	struct n2fake_plane planes[3];
	int lockCount;
};

void n2fake_object_fatal(const char *func, const char *what)
{
	fprintf(stderr, "n2fake: %s: %s\n", func, what);
	fflush(stderr);
	abort();
}

void *n2fake_object_alloc(size_t size, uint32_t type, int countable)
{
	struct n2fake_base *b;

	if (size < sizeof(*b))
		n2fake_object_fatal("n2fake_object_alloc", "requested size smaller than the object header");
	b = (struct n2fake_base *)calloc(1, size);
	if (!b)
		n2fake_object_fatal("n2fake_object_alloc", "out of memory");
	b->magic = N2FAKE_MAGIC;
	b->type = type;
	atomic_init(&b->refcount, 1);
	b->flags = 0;
	if (countable)
		(void)atomic_fetch_add(&g_live_objects, 1);
	return b;
}

struct n2fake_base *n2fake_object_check(CFTypeRef cf, uint32_t expectedType, const char *func)
{
	struct n2fake_base *b;

	if (!cf)
		n2fake_object_fatal(func, "NULL object");
	b = (struct n2fake_base *)(uintptr_t)cf;
	if (b->magic != N2FAKE_MAGIC)
		n2fake_object_fatal(func, "not a fake object (or an already-freed object)");
	if (expectedType != 0 && b->type != expectedType)
		n2fake_object_fatal(func, "wrong object type");
	return b;
}

long n2fake_live_objects(void)
{
	return atomic_load(&g_live_objects);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Static singletons                                                                                                  */
/* ------------------------------------------------------------------------------------------------------------------ */

#define N2FAKE_STATIC_BASE(t) { N2FAKE_MAGIC, (uint32_t)(t), 1, N2FAKE_FLAG_STATIC, 0, NULL }
#define N2FAKE_STATIC_STRING(var, text) \
	static struct n2fake_string var = { { N2FAKE_MAGIC, N2FAKE_TYPE_STRING, 1, \
		N2FAKE_FLAG_STATIC | N2FAKE_FLAG_INTERNED, 0, NULL }, (char *)(text) }

static struct n2fake_allocator n2fake_alloc_default = N2FAKE_STATIC_BASE(N2FAKE_TYPE_ALLOCATOR);
static struct n2fake_allocator n2fake_alloc_null = N2FAKE_STATIC_BASE(N2FAKE_TYPE_ALLOCATOR);
static struct n2fake_boolean n2fake_boolean_true = { N2FAKE_STATIC_BASE(N2FAKE_TYPE_BOOLEAN), 1 };
static struct n2fake_boolean n2fake_boolean_false = { N2FAKE_STATIC_BASE(N2FAKE_TYPE_BOOLEAN), 0 };

N2FAKE_STATIC_STRING(n2fake_key_notsync, "kCMSampleAttachmentKey_NotSync");
N2FAKE_STATIC_STRING(n2fake_key_depends, "kCMSampleAttachmentKey_DependsOnOthers");
N2FAKE_STATIC_STRING(n2fake_key_pixfmt, "kCVPixelBufferPixelFormatTypeKey");
N2FAKE_STATIC_STRING(n2fake_key_width, "kCVPixelBufferWidthKey");
N2FAKE_STATIC_STRING(n2fake_key_height, "kCVPixelBufferHeightKey");
N2FAKE_STATIC_STRING(n2fake_key_iosurface, "kCVPixelBufferIOSurfacePropertiesKey");
N2FAKE_STATIC_STRING(n2fake_key_ishw, "kVTVideoEncoder_IsHardwareAccelerated");
N2FAKE_STATIC_STRING(n2fake_key_rating, "kVTVideoEncoder_Rating");

const struct __CFAllocator *const kCFAllocatorDefault =
	(const struct __CFAllocator *)&n2fake_alloc_default;
const struct __CFAllocator *const kCFAllocatorNull =
	(const struct __CFAllocator *)&n2fake_alloc_null;
const struct __CFBoolean *const kCFBooleanTrue =
	(const struct __CFBoolean *)&n2fake_boolean_true;
const struct __CFBoolean *const kCFBooleanFalse =
	(const struct __CFBoolean *)&n2fake_boolean_false;

const struct __CFString *const kCMSampleAttachmentKey_NotSync =
	(const struct __CFString *)&n2fake_key_notsync;
const struct __CFString *const kCMSampleAttachmentKey_DependsOnOthers =
	(const struct __CFString *)&n2fake_key_depends;
const struct __CFString *const kCVPixelBufferPixelFormatTypeKey =
	(const struct __CFString *)&n2fake_key_pixfmt;
const struct __CFString *const kCVPixelBufferWidthKey =
	(const struct __CFString *)&n2fake_key_width;
const struct __CFString *const kCVPixelBufferHeightKey =
	(const struct __CFString *)&n2fake_key_height;
const struct __CFString *const kCVPixelBufferIOSurfacePropertiesKey =
	(const struct __CFString *)&n2fake_key_iosurface;
const struct __CFString *const kVTVideoEncoder_IsHardwareAccelerated =
	(const struct __CFString *)&n2fake_key_ishw;
const struct __CFString *const kVTVideoEncoder_Rating =
	(const struct __CFString *)&n2fake_key_rating;

/* The fake always uses CFType retain/release semantics; custom callbacks are not emulated. */
const CFDictionaryKeyCallBacks kCFTypeDictionaryKeyCallBacks =
	{ 0, NULL, NULL, NULL, NULL, NULL };
const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks =
	{ 0, NULL, NULL, NULL, NULL };
const CFArrayCallBacks kCFTypeArrayCallBacks =
	{ 0, NULL, NULL, NULL, NULL };

/* ------------------------------------------------------------------------------------------------------------------ */
/* Interning                                                                                                          */
/* ------------------------------------------------------------------------------------------------------------------ */

static pthread_mutex_t g_intern_lock = PTHREAD_MUTEX_INITIALIZER;
static struct n2fake_string **g_interned;
static size_t g_interned_count;
static size_t g_interned_capacity;
static int g_static_strings_registered;

static void n2fake_intern_add(struct n2fake_string *s)
{
	if (g_interned_count == g_interned_capacity) {
		size_t cap = g_interned_capacity ? g_interned_capacity * 2 : 32;
		struct n2fake_string **p = (struct n2fake_string **)realloc(g_interned, cap * sizeof(*p));
		if (!p)
			n2fake_object_fatal("n2fake_cfstr", "out of memory growing the intern table");
		g_interned = p;
		g_interned_capacity = cap;
	}
	g_interned[g_interned_count++] = s;
}

static void n2fake_register_static_strings(void)
{
	n2fake_intern_add(&n2fake_key_notsync);
	n2fake_intern_add(&n2fake_key_depends);
	n2fake_intern_add(&n2fake_key_pixfmt);
	n2fake_intern_add(&n2fake_key_width);
	n2fake_intern_add(&n2fake_key_height);
	n2fake_intern_add(&n2fake_key_iosurface);
	n2fake_intern_add(&n2fake_key_ishw);
	n2fake_intern_add(&n2fake_key_rating);
}

__attribute__((constructor)) static void n2fake_bootstrap(void)
{
	/* The first n2fake_cfstr() call also registers these, so constructor order cannot create duplicates. */
	pthread_mutex_lock(&g_intern_lock);
	if (!g_static_strings_registered) {
		n2fake_register_static_strings();
		g_static_strings_registered = 1;
	}
	pthread_mutex_unlock(&g_intern_lock);
}

CFStringRef n2fake_cfstr(const char *s)
{
	struct n2fake_string *str;
	size_t i, len;

	if (!s)
		n2fake_object_fatal("n2fake_cfstr", "NULL C string");
	pthread_mutex_lock(&g_intern_lock);
	if (!g_static_strings_registered) {
		n2fake_register_static_strings();
		g_static_strings_registered = 1;
	}
	for (i = 0; i < g_interned_count; i++) {
		if (strcmp(g_interned[i]->cstr, s) == 0) {
			str = g_interned[i];
			pthread_mutex_unlock(&g_intern_lock);
			return (CFStringRef)str;
		}
	}
	len = strlen(s);
	str = (struct n2fake_string *)n2fake_object_alloc(sizeof(*str), N2FAKE_TYPE_STRING, 0);
	str->base.flags = N2FAKE_FLAG_INTERNED;
	str->cstr = (char *)malloc(len + 1);
	if (!str->cstr)
		n2fake_object_fatal("n2fake_cfstr", "out of memory");
	memcpy(str->cstr, s, len + 1);
	n2fake_intern_add(str);
	pthread_mutex_unlock(&g_intern_lock);
	return (CFStringRef)str;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Reference counting and type identity                                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

static void n2fake_destroy_object(struct n2fake_base *b)
{
	if (b->flags & N2FAKE_FLAG_STATIC)
		return;
	if (b->destroy_extra)
		b->destroy_extra(b);
	switch (b->type) {
	case N2FAKE_TYPE_STRING: {
		struct n2fake_string *s = (struct n2fake_string *)b;
		if (!(b->flags & N2FAKE_FLAG_INTERNED))
			free(s->cstr);
		break;
	}
	case N2FAKE_TYPE_DICTIONARY: {
		struct n2fake_dictionary *d = (struct n2fake_dictionary *)b;
		size_t i;
		for (i = 0; i < d->count; i++) {
			CFRelease(d->entries[i].key);
			CFRelease(d->entries[i].value);
		}
		free(d->entries);
		break;
	}
	case N2FAKE_TYPE_ARRAY: {
		struct n2fake_array *a = (struct n2fake_array *)b;
		size_t i;
		for (i = 0; i < a->count; i++)
			CFRelease(a->values[i]);
		free(a->values);
		break;
	}
	case N2FAKE_TYPE_BLOCKBUFFER: {
		struct n2fake_blockbuffer *bb = (struct n2fake_blockbuffer *)b;
		if (bb->owned)
			free(bb->memory);
		break;
	}
	case N2FAKE_TYPE_SAMPLEBUFFER: {
		struct n2fake_samplebuffer *sb = (struct n2fake_samplebuffer *)b;
		if (sb->data)
			CFRelease(sb->data);
		if (sb->fmt)
			CFRelease(sb->fmt);
		if (sb->attachments)
			CFRelease(sb->attachments);
		free(sb->sampleSizes);
		break;
	}
	case N2FAKE_TYPE_FORMATDESC: {
		struct n2fake_formatdesc *fd = (struct n2fake_formatdesc *)b;
		size_t i;
		for (i = 0; i < fd->count; i++)
			free(fd->params[i].bytes);
		free(fd->params);
		break;
	}
	case N2FAKE_TYPE_PIXELBUFFER: {
		struct n2fake_pixelbuffer *pb = (struct n2fake_pixelbuffer *)b;
		size_t i;
		for (i = 0; i < pb->planeCount; i++)
			free(pb->planes[i].base);
		break;
	}
	default:
		break;
	}
	if (!(b->flags & N2FAKE_FLAG_INTERNED)) {
		(void)atomic_fetch_sub(&g_live_objects, 1);
		free(b);
	}
}

CFTypeRef CFRetain(CFTypeRef cf)
{
	struct n2fake_base *b = n2fake_object_check(cf, 0, "CFRetain");
	long old;

	if (b->flags & (N2FAKE_FLAG_STATIC | N2FAKE_FLAG_INTERNED))
		return cf;
	old = atomic_fetch_add(&b->refcount, 1);
	if (old <= 0)
		n2fake_object_fatal("CFRetain", "object has already been freed");
	return cf;
}

void CFRelease(CFTypeRef cf)
{
	struct n2fake_base *b;
	long old;

	if (!cf)                        /* the one NULL-safe entry point, as in CoreFoundation */
		return;
	b = n2fake_object_check(cf, 0, "CFRelease");
	if (b->flags & (N2FAKE_FLAG_STATIC | N2FAKE_FLAG_INTERNED))
		return;
	old = atomic_fetch_sub(&b->refcount, 1);
	if (old == 1)
		n2fake_destroy_object(b);
	else if (old <= 0)
		n2fake_object_fatal("CFRelease", "release of an object with refcount 0");
}

CFTypeID CFGetTypeID(CFTypeRef cf)
{
	return n2fake_object_check(cf, 0, "CFGetTypeID")->type;
}

CFTypeID CFNumberGetTypeID(void)     { return N2FAKE_TYPE_NUMBER; }
CFTypeID CFBooleanGetTypeID(void)    { return N2FAKE_TYPE_BOOLEAN; }
CFTypeID CFStringGetTypeID(void)     { return N2FAKE_TYPE_STRING; }
CFTypeID CFDictionaryGetTypeID(void) { return N2FAKE_TYPE_DICTIONARY; }

Boolean CFEqual(CFTypeRef a, CFTypeRef b)
{
	struct n2fake_base *ba = n2fake_object_check(a, 0, "CFEqual");
	struct n2fake_base *bb = n2fake_object_check(b, 0, "CFEqual");

	if (ba == bb)
		return 1;
	if (ba->type != bb->type)
		return 0;
	switch (ba->type) {
	case N2FAKE_TYPE_STRING:
		return strcmp(((struct n2fake_string *)ba)->cstr,
		              ((struct n2fake_string *)bb)->cstr) == 0;
	case N2FAKE_TYPE_NUMBER: {
		struct n2fake_number *na = (struct n2fake_number *)ba;
		struct n2fake_number *nb = (struct n2fake_number *)bb;
		if (na->is_double || nb->is_double) {
			double da = na->is_double ? na->dbl : (double)na->i64;
			double db = nb->is_double ? nb->dbl : (double)nb->i64;
			return da == db;
		}
		return na->i64 == nb->i64;
	}
	case N2FAKE_TYPE_BOOLEAN:
		return ((struct n2fake_boolean *)ba)->value == ((struct n2fake_boolean *)bb)->value;
	default:
		return 0;
	}
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CFString                                                                                                           */
/* ------------------------------------------------------------------------------------------------------------------ */

static int n2fake_string_encoding_ok(uint32_t encoding)
{
	return encoding == kCFStringEncodingUTF8 || encoding == 0;
}

CFStringRef CFStringCreateWithCString(CFAllocatorRef a, const char *s, uint32_t encoding)
{
	struct n2fake_string *str;
	size_t len;

	(void)a;
	if (!s)
		n2fake_object_fatal("CFStringCreateWithCString", "NULL C string");
	if (!n2fake_string_encoding_ok(encoding))
		return NULL;
	len = strlen(s);
	str = (struct n2fake_string *)n2fake_object_alloc(sizeof(*str), N2FAKE_TYPE_STRING, 1);
	str->cstr = (char *)malloc(len + 1);
	if (!str->cstr)
		n2fake_object_fatal("CFStringCreateWithCString", "out of memory");
	memcpy(str->cstr, s, len + 1);
	return (CFStringRef)str;
}

Boolean CFStringGetCString(CFStringRef s, char *buf, CFIndex size, uint32_t encoding)
{
	struct n2fake_string *str = (struct n2fake_string *)n2fake_object_check(s, N2FAKE_TYPE_STRING,
	                                                                          "CFStringGetCString");
	size_t len;

	if (!n2fake_string_encoding_ok(encoding))
		return 0;
	len = strlen(str->cstr);
	if (!buf || size <= 0 || (size_t)size < len + 1)
		return 0;
	memcpy(buf, str->cstr, len + 1);
	return 1;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CFNumber / CFBoolean                                                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

CFNumberRef CFNumberCreate(CFAllocatorRef a, CFIndex type, const void *valuePtr)
{
	struct n2fake_number *n;

	(void)a;
	if (!valuePtr)
		n2fake_object_fatal("CFNumberCreate", "NULL value pointer");
	n = (struct n2fake_number *)n2fake_object_alloc(sizeof(*n), N2FAKE_TYPE_NUMBER, 1);
	switch (type) {
	case kCFNumberSInt32Type:
		n->is_double = 0;
		n->i64 = *(const int32_t *)valuePtr;
		break;
	case kCFNumberSInt64Type:
		n->is_double = 0;
		n->i64 = *(const int64_t *)valuePtr;
		break;
	case kCFNumberFloat64Type:
		n->is_double = 1;
		n->dbl = *(const double *)valuePtr;
		break;
	default:
		CFRelease((CFTypeRef)n);
		return NULL;
	}
	return (CFNumberRef)n;
}

Boolean CFNumberGetValue(CFNumberRef n, CFIndex type, void *valuePtr)
{
	struct n2fake_number *num = (struct n2fake_number *)n2fake_object_check(n, N2FAKE_TYPE_NUMBER,
	                                                                        "CFNumberGetValue");
	double d;

	if (!valuePtr)
		n2fake_object_fatal("CFNumberGetValue", "NULL output pointer");
	d = num->is_double ? num->dbl : (double)num->i64;
	switch (type) {
	case kCFNumberSInt32Type:
		if (num->is_double) {
			if (!(d >= (double)INT32_MIN && d <= (double)INT32_MAX))
				return 0;
			*(int32_t *)valuePtr = (int32_t)d;
		} else {
			if (num->i64 < INT32_MIN || num->i64 > INT32_MAX)
				return 0;
			*(int32_t *)valuePtr = (int32_t)num->i64;
		}
		return 1;
	case kCFNumberSInt64Type:
		if (num->is_double) {
			if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
				return 0;
			*(int64_t *)valuePtr = (int64_t)d;
		} else {
			*(int64_t *)valuePtr = num->i64;
		}
		return 1;
	case kCFNumberFloat64Type:
		*(double *)valuePtr = d;
		return 1;
	default:
		return 0;
	}
}

Boolean CFBooleanGetValue(CFBooleanRef b)
{
	struct n2fake_boolean *bo = (struct n2fake_boolean *)n2fake_object_check(b, N2FAKE_TYPE_BOOLEAN,
	                                                                        "CFBooleanGetValue");
	return bo->value ? 1 : 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CFDictionary                                                                                                       */
/* ------------------------------------------------------------------------------------------------------------------ */

static void n2fake_dictionary_grow(struct n2fake_dictionary *d)
{
	size_t cap = d->capacity ? d->capacity * 2 : 8;
	struct n2fake_dict_entry *p = (struct n2fake_dict_entry *)realloc(d->entries, cap * sizeof(*p));
	if (!p)
		n2fake_object_fatal("CFDictionarySetValue", "out of memory growing the dictionary");
	d->entries = p;
	d->capacity = cap;
}

CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef a, CFIndex capacity,
                                                 const CFDictionaryKeyCallBacks *k,
                                                 const CFDictionaryValueCallBacks *v)
{
	struct n2fake_dictionary *d;

	(void)a;
	(void)k;
	(void)v;
	d = (struct n2fake_dictionary *)n2fake_object_alloc(sizeof(*d), N2FAKE_TYPE_DICTIONARY, 1);
	d->capacity = capacity > 0 ? (size_t)capacity : 8;
	d->entries = (struct n2fake_dict_entry *)calloc(d->capacity, sizeof(*d->entries));
	if (!d->entries)
		n2fake_object_fatal("CFDictionaryCreateMutable", "out of memory");
	return (CFMutableDictionaryRef)d;
}

void CFDictionarySetValue(CFMutableDictionaryRef d, const void *key, const void *value)
{
	struct n2fake_dictionary *dict = (struct n2fake_dictionary *)n2fake_object_check(
		(CFTypeRef)d, N2FAKE_TYPE_DICTIONARY, "CFDictionarySetValue");
	size_t i;

	if (!key)
		n2fake_object_fatal("CFDictionarySetValue", "NULL key");
	if (!value)
		n2fake_object_fatal("CFDictionarySetValue", "NULL value");
	for (i = 0; i < dict->count; i++) {
		if (CFEqual(dict->entries[i].key, key)) {
			CFRetain(value);
			CFRelease(dict->entries[i].value);
			dict->entries[i].value = value;
			return;
		}
	}
	if (dict->count == dict->capacity)
		n2fake_dictionary_grow(dict);
	dict->entries[dict->count].key = CFRetain(key);
	dict->entries[dict->count].value = CFRetain(value);
	dict->count++;
}

const void *CFDictionaryGetValue(CFDictionaryRef d, const void *key)
{
	struct n2fake_dictionary *dict = (struct n2fake_dictionary *)n2fake_object_check(
		(CFTypeRef)d, N2FAKE_TYPE_DICTIONARY, "CFDictionaryGetValue");
	size_t i;

	if (!key)
		n2fake_object_fatal("CFDictionaryGetValue", "NULL key");
	for (i = 0; i < dict->count; i++)
		if (CFEqual(dict->entries[i].key, key))
			return dict->entries[i].value;
	return NULL;
}

CFIndex n2fake_dictionary_count(CFDictionaryRef d)
{
	struct n2fake_dictionary *dict = (struct n2fake_dictionary *)n2fake_object_check(
		(CFTypeRef)d, N2FAKE_TYPE_DICTIONARY, "n2fake_dictionary_count");
	return (CFIndex)dict->count;
}

const void *n2fake_dictionary_key_at(CFDictionaryRef d, CFIndex index)
{
	struct n2fake_dictionary *dict = (struct n2fake_dictionary *)n2fake_object_check(
		(CFTypeRef)d, N2FAKE_TYPE_DICTIONARY, "n2fake_dictionary_key_at");
	if (index < 0 || (size_t)index >= dict->count)
		return NULL;
	return dict->entries[index].key;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CFArray                                                                                                            */
/* ------------------------------------------------------------------------------------------------------------------ */

CFMutableArrayRef CFArrayCreateMutable(CFAllocatorRef a, CFIndex capacity, const CFArrayCallBacks *cb)
{
	struct n2fake_array *arr;

	(void)a;
	(void)cb;
	arr = (struct n2fake_array *)n2fake_object_alloc(sizeof(*arr), N2FAKE_TYPE_ARRAY, 1);
	arr->capacity = capacity > 0 ? (size_t)capacity : 8;
	arr->values = (const void **)calloc(arr->capacity, sizeof(*arr->values));
	if (!arr->values)
		n2fake_object_fatal("CFArrayCreateMutable", "out of memory");
	return (CFMutableArrayRef)arr;
}

void CFArrayAppendValue(CFMutableArrayRef arr, const void *value)
{
	struct n2fake_array *a = (struct n2fake_array *)n2fake_object_check(
		(CFTypeRef)arr, N2FAKE_TYPE_ARRAY, "CFArrayAppendValue");

	if (!value)
		n2fake_object_fatal("CFArrayAppendValue", "NULL value");
	if (a->count == a->capacity) {
		size_t cap = a->capacity ? a->capacity * 2 : 8;
		const void **p = (const void **)realloc(a->values, cap * sizeof(*p));
		if (!p)
			n2fake_object_fatal("CFArrayAppendValue", "out of memory growing the array");
		a->values = p;
		a->capacity = cap;
	}
	a->values[a->count++] = CFRetain(value);
}

CFIndex CFArrayGetCount(CFArrayRef arr)
{
	struct n2fake_array *a = (struct n2fake_array *)n2fake_object_check(
		(CFTypeRef)arr, N2FAKE_TYPE_ARRAY, "CFArrayGetCount");
	return (CFIndex)a->count;
}

const void *CFArrayGetValueAtIndex(CFArrayRef arr, CFIndex i)
{
	struct n2fake_array *a = (struct n2fake_array *)n2fake_object_check(
		(CFTypeRef)arr, N2FAKE_TYPE_ARRAY, "CFArrayGetValueAtIndex");
	if (i < 0 || (size_t)i >= a->count)
		n2fake_object_fatal("CFArrayGetValueAtIndex", "index out of range");
	return a->values[i];
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CMBlockBuffer                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */

OSStatus CMBlockBufferCreateWithMemoryBlock(CFAllocatorRef structureAllocator, void *memoryBlock,
                                            size_t blockLength, CFAllocatorRef blockAllocator,
                                            const void *customBlockSource, size_t offsetToData,
                                            size_t dataLength, uint32_t flags,
                                            CMBlockBufferRef *blockBufferOut)
{
	struct n2fake_blockbuffer *bb;

	(void)structureAllocator;
	(void)blockAllocator;
	(void)customBlockSource;
	(void)flags;                    /* the fake allocates eagerly, which is AssureMemoryNow */
	if (!blockBufferOut)
		n2fake_object_fatal("CMBlockBufferCreateWithMemoryBlock", "NULL out pointer");
	if (offsetToData > blockLength || dataLength > blockLength - offsetToData)
		return -12704;
	bb = (struct n2fake_blockbuffer *)n2fake_object_alloc(sizeof(*bb), N2FAKE_TYPE_BLOCKBUFFER, 1);
	bb->blockLength = blockLength;
	bb->offset = offsetToData;
	bb->dataLength = dataLength;
	if (memoryBlock) {
		bb->memory = (uint8_t *)memoryBlock;
		bb->owned = 0;
	} else {
		bb->memory = (uint8_t *)calloc(1, blockLength ? blockLength : 1);
		if (!bb->memory) {
			CFRelease((CFTypeRef)bb);
			return -12701;
		}
		bb->owned = 1;
	}
	*blockBufferOut = (CMBlockBufferRef)bb;
	return 0;
}

OSStatus CMBlockBufferReplaceDataBytes(const void *sourceBytes, CMBlockBufferRef dst,
                                       size_t offsetIntoDestination, size_t dataLength)
{
	struct n2fake_blockbuffer *bb = (struct n2fake_blockbuffer *)n2fake_object_check(
		(CFTypeRef)dst, N2FAKE_TYPE_BLOCKBUFFER, "CMBlockBufferReplaceDataBytes");

	if (offsetIntoDestination > bb->dataLength ||
	    dataLength > bb->dataLength - offsetIntoDestination)
		return -12704;
	if (dataLength && !sourceBytes)
		return -12705;
	if (dataLength)
		memcpy(bb->memory + bb->offset + offsetIntoDestination, sourceBytes, dataLength);
	return 0;
}

size_t CMBlockBufferGetDataLength(CMBlockBufferRef b)
{
	struct n2fake_blockbuffer *bb = (struct n2fake_blockbuffer *)n2fake_object_check(
		(CFTypeRef)b, N2FAKE_TYPE_BLOCKBUFFER, "CMBlockBufferGetDataLength");
	return bb->dataLength;
}

OSStatus CMBlockBufferCopyDataBytes(CMBlockBufferRef b, size_t offsetToData, size_t dataLength,
                                    void *destination)
{
	struct n2fake_blockbuffer *bb = (struct n2fake_blockbuffer *)n2fake_object_check(
		(CFTypeRef)b, N2FAKE_TYPE_BLOCKBUFFER, "CMBlockBufferCopyDataBytes");

	if (offsetToData > bb->dataLength || dataLength > bb->dataLength - offsetToData)
		return -12704;
	if (dataLength && !destination)
		return -12705;
	if (dataLength)
		memcpy(destination, bb->memory + bb->offset + offsetToData, dataLength);
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CMSampleBuffer                                                                                                     */
/* ------------------------------------------------------------------------------------------------------------------ */

OSStatus CMSampleBufferCreateReady(CFAllocatorRef a, CMBlockBufferRef dataBuffer, CMFormatDescriptionRef fmt,
                                   CFIndex numSamples, CFIndex numSampleTimingEntries,
                                   const CMSampleTimingInfo *timing, CFIndex numSampleSizeEntries,
                                   const size_t *sampleSizeArray, CMSampleBufferRef *out)
{
	struct n2fake_samplebuffer *sb;

	(void)a;
	if (!out)
		n2fake_object_fatal("CMSampleBufferCreateReady", "NULL out pointer");
	if (numSamples < 0)
		return -12712;
	if (numSampleTimingEntries > 0 && !timing)
		return -12712;
	if (numSampleSizeEntries > 0 && !sampleSizeArray)
		return -12712;
	sb = (struct n2fake_samplebuffer *)n2fake_object_alloc(sizeof(*sb), N2FAKE_TYPE_SAMPLEBUFFER, 1);
	sb->data = dataBuffer ? (CMBlockBufferRef)CFRetain(dataBuffer) : NULL;
	sb->fmt = fmt ? (CMFormatDescriptionRef)CFRetain(fmt) : NULL;
	sb->numSamples = (size_t)numSamples;
	if (numSampleTimingEntries > 0)
		sb->timing = timing[0];
	if (numSampleSizeEntries > 0) {
		size_t n = (size_t)numSampleSizeEntries;
		sb->sampleSizes = (size_t *)calloc(n, sizeof(*sb->sampleSizes));
		if (!sb->sampleSizes) {
			CFRelease((CFTypeRef)sb);
			return -12712;
		}
		memcpy(sb->sampleSizes, sampleSizeArray, n * sizeof(*sb->sampleSizes));
		sb->numSampleSizes = n;
	}
	*out = (CMSampleBufferRef)sb;
	return 0;
}

CFArrayRef CMSampleBufferGetSampleAttachmentsArray(CMSampleBufferRef s, Boolean createIfNecessary)
{
	struct n2fake_samplebuffer *sb = (struct n2fake_samplebuffer *)n2fake_object_check(
		(CFTypeRef)s, N2FAKE_TYPE_SAMPLEBUFFER, "CMSampleBufferGetSampleAttachmentsArray");
	size_t i;

	if (sb->attachments)
		return (CFArrayRef)sb->attachments;
	if (!createIfNecessary)
		return NULL;
	sb->attachments = CFArrayCreateMutable(NULL, (CFIndex)sb->numSamples, &kCFTypeArrayCallBacks);
	for (i = 0; i < sb->numSamples; i++) {
		CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
		                                                    &kCFTypeDictionaryValueCallBacks);
		CFArrayAppendValue(sb->attachments, d);
		CFRelease((CFTypeRef)d);
	}
	return (CFArrayRef)sb->attachments;
}

CMBlockBufferRef CMSampleBufferGetDataBuffer(CMSampleBufferRef s)
{
	struct n2fake_samplebuffer *sb = (struct n2fake_samplebuffer *)n2fake_object_check(
		(CFTypeRef)s, N2FAKE_TYPE_SAMPLEBUFFER, "CMSampleBufferGetDataBuffer");
	return sb->data;
}

CMFormatDescriptionRef CMSampleBufferGetFormatDescription(CMSampleBufferRef s)
{
	struct n2fake_samplebuffer *sb = (struct n2fake_samplebuffer *)n2fake_object_check(
		(CFTypeRef)s, N2FAKE_TYPE_SAMPLEBUFFER, "CMSampleBufferGetFormatDescription");
	return sb->fmt;
}

int n2fake_sample_timing(CMSampleBufferRef s, CMSampleTimingInfo *timing, size_t *sampleSize)
{
	struct n2fake_samplebuffer *sb = (struct n2fake_samplebuffer *)n2fake_object_check(
		(CFTypeRef)s, N2FAKE_TYPE_SAMPLEBUFFER, "n2fake_sample_timing");
	if (sb->numSamples == 0)
		return 0;
	if (timing)
		*timing = sb->timing;
	if (sampleSize)
		*sampleSize = sb->numSampleSizes ? sb->sampleSizes[0] : 0;
	return 1;
}

size_t n2fake_sample_count(CMSampleBufferRef s)
{
	struct n2fake_samplebuffer *sb = (struct n2fake_samplebuffer *)n2fake_object_check(
		(CFTypeRef)s, N2FAKE_TYPE_SAMPLEBUFFER, "n2fake_sample_count");
	return sb->numSamples;
}

size_t n2fake_sample_size_at(CMSampleBufferRef s, size_t index)
{
	struct n2fake_samplebuffer *sb = (struct n2fake_samplebuffer *)n2fake_object_check(
		(CFTypeRef)s, N2FAKE_TYPE_SAMPLEBUFFER, "n2fake_sample_size_at");
	if (index >= sb->numSampleSizes)
		return 0;
	return sb->sampleSizes[index];
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CMVideoFormatDescription (H.264 parameter sets)                                                                    */
/* ------------------------------------------------------------------------------------------------------------------ */

OSStatus CMVideoFormatDescriptionCreateFromH264ParameterSets(CFAllocatorRef a, size_t parameterSetCount,
                                                             const uint8_t *const *parameterSetPointers,
                                                             const size_t *parameterSetSizes,
                                                             int NALUnitHeaderLength,
                                                             CMFormatDescriptionRef *formatDescriptionOut)
{
	struct n2fake_formatdesc *fd;
	size_t i;

	(void)a;
	if (!formatDescriptionOut)
		n2fake_object_fatal("CMVideoFormatDescriptionCreateFromH264ParameterSets", "NULL out pointer");
	if (parameterSetCount < 2 || !parameterSetPointers || !parameterSetSizes)
		return -12712;
	if (NALUnitHeaderLength != 1 && NALUnitHeaderLength != 2 && NALUnitHeaderLength != 4)
		return -12712;
	if (!parameterSetPointers[0] || parameterSetSizes[0] < 1)
		return -12712;
	if ((parameterSetPointers[0][0] & 0x1f) != 7)          /* first parameter set must be an SPS */
		return -12712;
	fd = (struct n2fake_formatdesc *)n2fake_object_alloc(sizeof(*fd), N2FAKE_TYPE_FORMATDESC, 1);
	fd->codec = 0x61766331u;                                /* 'avc1' */
	fd->nalLength = NALUnitHeaderLength;
	fd->count = parameterSetCount;
	fd->params = (struct n2fake_param *)calloc(parameterSetCount, sizeof(*fd->params));
	if (!fd->params) {
		CFRelease((CFTypeRef)fd);
		return -12712;
	}
	for (i = 0; i < parameterSetCount; i++) {
		if (!parameterSetPointers[i] || parameterSetSizes[i] == 0) {
			CFRelease((CFTypeRef)fd);
			return -12712;
		}
		fd->params[i].bytes = (uint8_t *)malloc(parameterSetSizes[i]);
		if (!fd->params[i].bytes) {
			CFRelease((CFTypeRef)fd);
			return -12712;
		}
		memcpy(fd->params[i].bytes, parameterSetPointers[i], parameterSetSizes[i]);
		fd->params[i].size = parameterSetSizes[i];
	}
	*formatDescriptionOut = (CMFormatDescriptionRef)fd;
	return 0;
}

OSStatus CMVideoFormatDescriptionGetH264ParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index,
                                                            const uint8_t **ps, size_t *size, size_t *count,
                                                            int *nalUnitHeaderLength)
{
	struct n2fake_formatdesc *fd = (struct n2fake_formatdesc *)n2fake_object_check(
		(CFTypeRef)fmt, N2FAKE_TYPE_FORMATDESC, "CMVideoFormatDescriptionGetH264ParameterSetAtIndex");

	if (fd->codec != 0x61766331u || index >= fd->count)
		return -12712;
	if (ps)
		*ps = fd->params[index].bytes;
	if (size)
		*size = fd->params[index].size;
	if (count)
		*count = fd->count;
	if (nalUnitHeaderLength)
		*nalUnitHeaderLength = fd->nalLength;
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* CVPixelBuffer                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */

static OSStatus n2fake_plane_alloc(struct n2fake_plane *p, size_t stride, size_t rows, size_t bytesUsed)
{
	if (stride != 0 && rows > SIZE_MAX / stride)
		return -6662;
	p->stride = stride;
	p->rows = rows;
	p->bytesUsed = bytesUsed;
	p->size = stride * rows;
	p->base = (uint8_t *)calloc(1, p->size ? p->size : 1);
	if (!p->base)
		return -6662;
	return 0;
}

int32_t CVPixelBufferCreate(CFAllocatorRef a, size_t width, size_t height, uint32_t pixelFormat,
                            CFDictionaryRef attrs, CVPixelBufferRef *out)
{
	struct n2fake_pixelbuffer *pb;
	OSStatus st = 0;

	(void)a;
	(void)attrs;
	if (!out)
		n2fake_object_fatal("CVPixelBufferCreate", "NULL out pointer");
	if (width == 0 || height == 0)
		return -6661;
	if (pixelFormat != N2_PIXFMT_420V && pixelFormat != N2_PIXFMT_420F &&
	    pixelFormat != N2_PIXFMT_I420)
		return -6661;
	if (width > SIZE_MAX - 63)
		return -6661;
	pb = (struct n2fake_pixelbuffer *)n2fake_object_alloc(sizeof(*pb), N2FAKE_TYPE_PIXELBUFFER, 1);
	pb->width = width;
	pb->height = height;
	pb->format = pixelFormat;
	if (pixelFormat == N2_PIXFMT_I420) {
		size_t cw = (width + 1) / 2;
		size_t ch = (height + 1) / 2;
		size_t cstride = N2FAKE_ROUND64(cw);
		pb->planeCount = 3;
		st = n2fake_plane_alloc(&pb->planes[0], N2FAKE_ROUND64(width), height, width);
		if (!st)
			st = n2fake_plane_alloc(&pb->planes[1], cstride, ch, cw);
		if (!st)
			st = n2fake_plane_alloc(&pb->planes[2], cstride, ch, cw);
	} else {
		size_t cstride = N2FAKE_ROUND64(width);
		size_t ch = (height + 1) / 2;
		size_t cuv = 2 * ((width + 1) / 2);
		pb->planeCount = 2;
		st = n2fake_plane_alloc(&pb->planes[0], N2FAKE_ROUND64(width), height, width);
		if (!st)
			st = n2fake_plane_alloc(&pb->planes[1], cstride, ch, cuv);
	}
	if (st != 0) {
		CFRelease((CFTypeRef)pb);
		return -6662;
	}
	*out = (CVPixelBufferRef)pb;
	return 0;
}

int32_t CVPixelBufferLockBaseAddress(CVPixelBufferRef pb, uint64_t lockFlags)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, "CVPixelBufferLockBaseAddress");
	(void)lockFlags;
	buf->lockCount++;
	return 0;
}

int32_t CVPixelBufferUnlockBaseAddress(CVPixelBufferRef pb, uint64_t lockFlags)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, "CVPixelBufferUnlockBaseAddress");
	(void)lockFlags;
	if (buf->lockCount <= 0)
		return -6661;
	buf->lockCount--;
	return 0;
}

static struct n2fake_plane *n2fake_plane_of(CVPixelBufferRef pb, size_t plane, const char *func)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, func);
	if (plane >= buf->planeCount)
		n2fake_object_fatal(func, "plane index out of range");
	return &buf->planes[plane];
}

void *CVPixelBufferGetBaseAddressOfPlane(CVPixelBufferRef pb, size_t plane)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, "CVPixelBufferGetBaseAddressOfPlane");
	struct n2fake_plane *p;

	if (plane >= buf->planeCount)
		n2fake_object_fatal("CVPixelBufferGetBaseAddressOfPlane", "plane index out of range");
	if (buf->lockCount <= 0)
		return NULL;            /* deliberately strict: catches a missing lock in the caller */
	p = &buf->planes[plane];
	return p->base;
}

size_t CVPixelBufferGetBytesPerRowOfPlane(CVPixelBufferRef pb, size_t plane)
{
	return n2fake_plane_of(pb, plane, "CVPixelBufferGetBytesPerRowOfPlane")->stride;
}

size_t CVPixelBufferGetWidth(CVPixelBufferRef pb)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, "CVPixelBufferGetWidth");
	return buf->width;
}

size_t CVPixelBufferGetHeight(CVPixelBufferRef pb)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, "CVPixelBufferGetHeight");
	return buf->height;
}

size_t CVPixelBufferGetPlaneCount(CVPixelBufferRef pb)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, "CVPixelBufferGetPlaneCount");
	return buf->planeCount;
}

uint32_t CVPixelBufferGetPixelFormatType(CVPixelBufferRef pb)
{
	struct n2fake_pixelbuffer *buf = (struct n2fake_pixelbuffer *)n2fake_object_check(
		(CFTypeRef)pb, N2FAKE_TYPE_PIXELBUFFER, "CVPixelBufferGetPixelFormatType");
	return buf->format;
}

size_t n2fake_pixel_buffer_plane_height(CVPixelBufferRef pb, size_t plane)
{
	return n2fake_plane_of(pb, plane, "n2fake_pixel_buffer_plane_height")->rows;
}

size_t n2fake_pixel_buffer_plane_bytes_used(CVPixelBufferRef pb, size_t plane)
{
	return n2fake_plane_of(pb, plane, "n2fake_pixel_buffer_plane_bytes_used")->bytesUsed;
}

size_t n2fake_pixel_buffer_plane_data_size(CVPixelBufferRef pb, size_t plane)
{
	return n2fake_plane_of(pb, plane, "n2fake_pixel_buffer_plane_data_size")->size;
}
