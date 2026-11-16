/*
 * Specialised shader functions ready before CoreAnimation asks for them.
 *
 * CoreAnimation's display thread creates its draw variants with -[MTLLibrary newFunctionWithDescriptor:error:] (the generic uber-shader plus
 * function constants).  Variants that QuartzCore's pipeline library does not hold are specialised by MTLCompilerService while the display thread
 * waits: 19 ms each in the first interactions, up to 440 ms at boot (measured by nord2_srcprobe.m).  Three things here:
 *
 *   1. an in-process cache of the functions that took 6 ms or more, keyed by library, name, options and constants: a repeated request is answered
 *      at once (a function is immutable);
 *   2. a LIST of those requests (no binaries: names and constant values, a few KB) kept in
 *      /var/mobile/Library/Caches/com.tegu.metal.pipelines/n2fn-<process>.txt, appended when a slow request is seen the first time;
 *   3. a low-priority thread that, once a library is created, replays the list entries that were requested late in an earlier run (after 20 s
 *      of the process's life: the early ones are asked for at once anyway), so the service has specialised them before the user's first
 *      interaction and the display thread finds them in the cache.
 *
 * Switch off: create the file n2fn-disable in that directory.  Nothing is replayed without a list; the list is written by the process itself.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <objc/runtime.h>
#include <objc/message.h>

#define DIR_PATH "/var/mobile/Library/Caches/com.tegu.metal.pipelines"
#define SLOW_NS 6000000ull
#define MAX_ENTRIES 600
#define LATE_AGE_S 20

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static NSMutableDictionary *cache;      /* key -> function */
static NSMutableDictionary *cost_ms;    /* key -> NSNumber */
static NSMutableSet *inflight;          /* keys being specialised right now */
static NSMutableSet *known;             /* "lib\tname\topts\tconsts" already in the list (file or this run) */
static NSMutableArray *entries;         /* loaded list: NSArray of 5 strings (age, lib, name, opts, consts) */
static NSMutableSet *replay_started;    /* library keys */
static char libkey_assoc;
static unsigned hits, slow_misses, replayed, appended, unkeyed_slow;
static uint64_t saved_ms, t_start, last_report;
static char list_path[200];
static BOOL disabled, ready;
static __thread BOOL replaying;

static uint64_t
now_ns(void)
{
	static mach_timebase_info_data_t tb;

	if (tb.denom == 0) {
		mach_timebase_info(&tb);
	}
	return mach_absolute_time() * tb.numer / tb.denom;
}

static void
say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
say(const char *fmt, ...)
{
	char line[300];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	nrw_log_line(line, strlen(line));
}

static size_t
type_size(NSUInteger type)
{
	switch (type) {
	case 3: return 4;   case 4: return 8;   case 5: return 12;  case 6: return 16;
	case 16: return 2;  case 17: return 4;  case 18: return 6;  case 19: return 8;
	case 29: case 33: return 4;  case 30: case 34: return 8;
	case 31: case 35: return 12; case 32: case 36: return 16;
	case 37: case 41: return 2;  case 38: case 42: return 4;
	case 39: case 43: return 6;  case 40: case 44: return 8;
	case 45: case 49: case 53: return 1;
	case 46: case 50: case 54: return 2;
	case 47: case 51: case 55: return 3;
	case 48: case 52: case 56: return 4;
	case 81: case 85: return 8;
	default: return 4;
	}
}

/* the constants of a constant-values object as one canonical string ("N=name,type,hex;I=index,type,hex"), sorted; nil when something cannot be
 * written down (then the request is not cached at all) */
static NSString *
constants_string(id values)
{
	NSMutableArray *items = [NSMutableArray array];

	for (unsigned s = 0; values != nil && s < 2; s++) {
		SEL list = sel_getUid(s == 0 ? "newNamedConstantArray" : "newIndexedConstantArray");
		NSArray *a;

		if (![values respondsToSelector:list]) {
			return nil;
		}
		a = ((id (*)(id, SEL))objc_msgSend)(values, list);   /* +1 */
		for (id v in a) {
			NSUInteger type = ((NSUInteger (*)(id, SEL))objc_msgSend)(v, sel_getUid("dataType"));
			const unsigned char *bytes = ((const unsigned char *(*)(id, SEL))objc_msgSend)(v, sel_getUid("data"));
			NSMutableString *hex = [NSMutableString string];
			size_t n = type_size(type);

			if (bytes == NULL || n > 16) {
				[a release];
				return nil;
			}
			for (size_t i = 0; i < n; i++) {
				[hex appendFormat:@"%02x", bytes[i]];
			}
			if (s == 0) {
				NSString *name = ((id (*)(id, SEL))objc_msgSend)(v, sel_getUid("name"));

				if (![name isKindOfClass:[NSString class]] || [name rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@",;\t\n="]].location != NSNotFound) {
					[a release];
					return nil;
				}
				[items addObject:[NSString stringWithFormat:@"N=%@,%lu,%@", name, (unsigned long)type, hex]];
			} else {
				NSUInteger index = ((NSUInteger (*)(id, SEL))objc_msgSend)(v, sel_getUid("index"));

				[items addObject:[NSString stringWithFormat:@"I=%lu,%lu,%@", (unsigned long)index, (unsigned long)type, hex]];
			}
		}
		[a release];
	}
	[items sortUsingSelector:@selector(compare:)];
	return [items componentsJoinedByString:@";"];
}

static NSString *
library_key(id lib)
{
	return objc_getAssociatedObject(lib, &libkey_assoc);
}

/* the key components of a request; nil when the request cannot be keyed */
static BOOL
request_parts(id lib, id name, NSUInteger opts, id values, NSString **libkey, NSString **namekey, NSString **consts)
{
	*libkey = library_key(lib);
	if (*libkey == nil || ![name isKindOfClass:[NSString class]] || [name rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"\t\n"]].location != NSNotFound) {
		return NO;
	}
	*namekey = name;
	*consts = values != nil ? constants_string(values) : @"";
	return *consts != nil;
}

static NSString *
full_key(id lib, NSString *name, NSUInteger opts, NSString *consts)
{
	return [NSString stringWithFormat:@"%p\t%@\t%lu\t%@", lib, name, (unsigned long)opts, consts];
}

static NSString *
list_key(NSString *libkey, NSString *name, NSUInteger opts, NSString *consts)
{
	return [NSString stringWithFormat:@"%@\t%@\t%lu\t%@", libkey, name, (unsigned long)opts, consts];
}

/* ---- the list on disk */

static void
load_list(void)
{
	NSString *text = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:list_path] encoding:NSUTF8StringEncoding error:nil];

	for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
		NSArray *f = [line componentsSeparatedByString:@"\t"];

		if ([f count] != 6 || ![f[0] isEqualToString:@"v1"] || [entries count] >= MAX_ENTRIES) {
			continue;
		}
		[entries addObject:[f subarrayWithRange:NSMakeRange(1, 5)]];
		[known addObject:list_key(f[2], f[3], (NSUInteger)[f[4] longLongValue], f[5])];
	}
}

static void
append_list(NSString *libkey, NSString *name, NSUInteger opts, NSString *consts)
{
	NSString *line = [NSString stringWithFormat:@"v1\t%llu\t%@\t%@\t%lu\t%@\n", (unsigned long long)((now_ns() - t_start) / 1000000000ull), libkey,
			  name, (unsigned long)opts, consts];
	FILE *f;

	if (appended + [entries count] >= MAX_ENTRIES) {
		return;
	}
	f = fopen(list_path, "a");
	if (f == NULL) {
		return;
	}
	fputs([line UTF8String], f);
	fclose(f);
	appended++;
}

/* ---- the cache */

static id
cache_get(NSString *key, uint64_t *saved)
{
	id fn;

	pthread_mutex_lock(&mu);
	for (;;) {
		fn = cache[key];
		if (fn != nil || ![inflight containsObject:key]) {
			break;
		}
		/* somebody (the replay thread) is specialising exactly this: wait for it instead of doing it twice */
		struct timespec until;
		clock_gettime(CLOCK_REALTIME, &until);
		until.tv_nsec += 150 * 1000000L;
		until.tv_sec += until.tv_nsec / 1000000000L;
		until.tv_nsec %= 1000000000L;
		if (pthread_cond_timedwait(&cv, &mu, &until) != 0) {
			fn = cache[key];
			break;
		}
	}
	if (fn != nil) {
		hits++;
		*saved = (uint64_t)[cost_ms[key] unsignedLongLongValue];
		saved_ms += *saved;
		[fn retain];
	}
	pthread_mutex_unlock(&mu);
	return fn;
}

static void
cache_put(NSString *key, id fn, uint64_t ns)
{
	pthread_mutex_lock(&mu);
	if (cache[key] == nil && [cache count] < 2000) {
		cache[key] = fn;
		cost_ms[key] = @(ns / 1000000ull);
	}
	[inflight removeObject:key];
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mu);
}

static void
report(BOOL force)
{
	uint64_t t = now_ns();

	if (!force && t - last_report < 20000000000ull) {
		return;
	}
	last_report = t;
	say("I FNW %s: %u answered from the cache (%llu ms of specialisation saved), %u slow requests seen (+%u that could not be keyed), %u replayed, %u added to the list (%lu on it)",
	    getprogname(), hits, (unsigned long long)saved_ms, slow_misses, unkeyed_slow, replayed, appended, (unsigned long)[entries count]);
}

/* ---- the replay */

typedef id (*fn_desc_t)(id, SEL, id, NSError **);
static fn_desc_t orig_desc;

static void *
replay_thread(void *arg)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	NSArray *job = (NSArray *)arg;       /* [library, libkey] */
	id lib = job[0];
	NSString *libkey = job[1];
	uint64_t t0 = now_ns();
	unsigned done = 0, wanted = 0;

	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
	pthread_setname_np("n2fn-replay");
	replaying = YES;
	for (NSArray *e in entries) {
		NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
		NSError *err = nil;
		MTLFunctionDescriptor *d;
		MTLFunctionConstantValues *cv;
		NSString *key;
		id fn;
		BOOL bad = NO;

		if (![e[1] isEqualToString:libkey] || [e[0] integerValue] < LATE_AGE_S) {
			[inner release];
			continue;
		}
		wanted++;
		cv = [[MTLFunctionConstantValues alloc] init];
		for (NSString *item in [e[4] length] ? [e[4] componentsSeparatedByString:@";"] : @[]) {
			NSArray *kv = [item componentsSeparatedByString:@"="];
			NSArray *p = [kv count] == 2 ? [kv[1] componentsSeparatedByString:@","] : nil;
			unsigned char bytes[16] = { 0 };
			NSUInteger type;
			NSString *hex;

			if ([p count] != 3) {
				bad = YES;
				break;
			}
			type = (NSUInteger)[p[1] longLongValue];
			hex = p[2];
			for (size_t i = 0; i < type_size(type) && 2 * i + 1 < [hex length]; i++) {
				unsigned v = 0;

				sscanf([[hex substringWithRange:NSMakeRange(2 * i, 2)] UTF8String], "%2x", &v);
				bytes[i] = (unsigned char)v;
			}
			if ([kv[0] isEqualToString:@"N"]) {
				[cv setConstantValue:bytes type:(MTLDataType)type withName:p[0]];
			} else {
				[cv setConstantValue:bytes type:(MTLDataType)type atIndex:(NSUInteger)[p[0] longLongValue]];
			}
		}
		if (!bad) {
			d = [MTLFunctionDescriptor functionDescriptor];
			d.name = e[2];
			d.options = (MTLFunctionOptions)[e[3] longLongValue];
			d.constantValues = cv;
			key = nil;
			{
				NSString *libkey2, *name2, *consts2;

				if (request_parts(lib, e[2], (NSUInteger)[e[3] longLongValue], cv, &libkey2, &name2, &consts2)) {
					key = full_key(lib, name2, (NSUInteger)[e[3] longLongValue], consts2);
				}
			}
			if (key != nil) {
				pthread_mutex_lock(&mu);
				BOOL have = cache[key] != nil;
				if (!have) {
					[inflight addObject:key];
				}
				pthread_mutex_unlock(&mu);
				if (!have) {
					uint64_t t1 = now_ns();

					fn = orig_desc(lib, @selector(newFunctionWithDescriptor:error:), d, &err);
					if (fn != nil) {
						cache_put(key, fn, now_ns() - t1);     /* the cache keeps its own reference */
						[fn release];
						done++;
					} else {
						cache_put(key, nil, 0);                /* only drops the in-flight mark */
					}
				}
			}
		}
		[cv release];
		[inner release];
		usleep(20000);                   /* the service and the big cores are shared with the user */
	}
	replaying = NO;
	pthread_mutex_lock(&mu);
	replayed += done;
	pthread_mutex_unlock(&mu);
	say("I FNW replay for %s: %u of %u list entries specialised in %llu ms", [libkey UTF8String], done, wanted, (unsigned long long)((now_ns() - t0) / 1000000ull));
	[job release];
	[pool release];
	return NULL;
}

static void
maybe_replay(id lib, NSString *libkey)
{
	BOOL start = NO;
	pthread_t t;

	if (disabled || libkey == nil) {
		return;
	}
	pthread_mutex_lock(&mu);
	if (![replay_started containsObject:libkey]) {
		for (NSArray *e in entries) {
			if ([e[1] isEqualToString:libkey] && [e[0] integerValue] >= LATE_AGE_S) {
				start = YES;
				break;
			}
		}
		[replay_started addObject:libkey];
	}
	pthread_mutex_unlock(&mu);
	if (start) {
		pthread_attr_t a;
		NSArray *job = [[NSArray alloc] initWithObjects:lib, libkey, nil];     /* the thread owns a reference to the library */

		pthread_attr_init(&a);
		pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&t, &a, replay_thread, job) != 0) {
			[job release];
		}
		pthread_attr_destroy(&a);
	}
}

/* ---- the hooks */

static id
hook_desc(id self, SEL sel, id descriptor, NSError **err)
{
	NSString *libkey, *name, *consts, *key = nil;
	NSUInteger opts = 0;
	uint64_t t0, ns;
	id fn, values = nil;
	uint64_t saved = 0;

	if (disabled || !ready || replaying || descriptor == nil) {
		return orig_desc(self, sel, descriptor, err);
	}
	if ([descriptor respondsToSelector:@selector(options)]) {
		opts = (NSUInteger)[(MTLFunctionDescriptor *)descriptor options];
	}
	if ([descriptor respondsToSelector:@selector(constantValues)]) {
		values = [(MTLFunctionDescriptor *)descriptor constantValues];
	}
	@try {
		if (request_parts(self, [descriptor name], opts, values, &libkey, &name, &consts)) {
			key = full_key(self, name, opts, consts);
			maybe_replay(self, libkey);
			fn = cache_get(key, &saved);
			if (fn != nil) {
				report(NO);
				return fn;
			}
		}
	} @catch (id ex) {
		key = nil;
	}
	t0 = now_ns();
	fn = orig_desc(self, sel, descriptor, err);
	ns = now_ns() - t0;
	if (key != nil && fn != nil && ns >= SLOW_NS) {
		NSString *lk = list_key(libkey, name, opts, consts);

		cache_put(key, fn, ns);
		pthread_mutex_lock(&mu);
		slow_misses++;
		if (![known containsObject:lk]) {
			[known addObject:lk];
			pthread_mutex_unlock(&mu);
			append_list(libkey, name, opts, consts);
		} else {
			pthread_mutex_unlock(&mu);
		}
	}
	if (key == nil && fn != nil && ns >= SLOW_NS) {
		pthread_mutex_lock(&mu);
		unkeyed_slow++;
		pthread_mutex_unlock(&mu);
	}
	report(NO);
	return fn;
}

typedef id (*lib_file_t)(id, SEL, id, NSError **);
static lib_file_t orig_file, orig_url, orig_bundle;
typedef id (*lib_default_t)(id, SEL);
static lib_default_t orig_default;

static void
tag(id lib, NSString *key)
{
	if (lib != nil && key != nil) {
		objc_setAssociatedObject(lib, &libkey_assoc, key, OBJC_ASSOCIATION_RETAIN);
	}
}

static id
hook_file(id self, SEL sel, id path, NSError **err)
{
	id lib = orig_file(self, sel, path, err);

	tag(lib, [NSString stringWithFormat:@"file:%@", path]);
	return lib;
}

static id
hook_url(id self, SEL sel, id url, NSError **err)
{
	id lib = orig_url(self, sel, url, err);

	tag(lib, [NSString stringWithFormat:@"file:%@", [url respondsToSelector:@selector(path)] ? [url path] : url]);
	return lib;
}

static id
hook_bundle(id self, SEL sel, id bundle, NSError **err)
{
	id lib = orig_bundle(self, sel, bundle, err);

	tag(lib, [NSString stringWithFormat:@"bundle:%@", [bundle respondsToSelector:@selector(bundleIdentifier)] ? [bundle bundleIdentifier] : bundle]);
	return lib;
}

static id
hook_default(id self, SEL sel)
{
	id lib = orig_default(self, sel);

	tag(lib, @"default");
	return lib;
}

/* the method a class itself answers with, replaced; nil when it does not have one of its own */
static IMP
replace_own(Class cls, SEL sel, IMP hook)
{
	unsigned count = 0;
	Method *list;
	IMP old = NULL;

	if (cls == Nil) {
		return NULL;
	}
	list = class_copyMethodList(cls, &count);
	for (unsigned i = 0; i < count; i++) {
		if (method_getName(list[i]) == sel) {
			old = method_setImplementation(list[i], hook);
			break;
		}
	}
	free(list);
	return old;
}

__attribute__((constructor)) static void
nord2_fnwarm_install(void)
{
	Class dev = objc_getClass("_MTLDevice"), lib = objc_getClass("_MTLLibrary");
	const char *proc = getprogname();
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

	if (dev == Nil || lib == Nil || proc == NULL || (strcmp(proc, "backboardd") && strcmp(proc, "SpringBoard"))) {
		[pool release];
		return;
	}
	t_start = now_ns();
	cache = [[NSMutableDictionary alloc] init];
	cost_ms = [[NSMutableDictionary alloc] init];
	inflight = [[NSMutableSet alloc] init];
	known = [[NSMutableSet alloc] init];
	entries = [[NSMutableArray alloc] init];
	replay_started = [[NSMutableSet alloc] init];
	snprintf(list_path, sizeof(list_path), DIR_PATH "/n2fn-%s.txt", proc);
	disabled = access(DIR_PATH "/n2fn-disable", F_OK) == 0;
	if (!disabled) {
		load_list();
	}
	orig_file = (lib_file_t)replace_own(dev, @selector(newLibraryWithFile:error:), (IMP)hook_file);
	orig_url = (lib_file_t)replace_own(dev, @selector(newLibraryWithURL:error:), (IMP)hook_url);
	orig_bundle = (lib_file_t)replace_own(dev, @selector(newDefaultLibraryWithBundle:error:), (IMP)hook_bundle);
	orig_default = (lib_default_t)replace_own(dev, @selector(newDefaultLibrary), (IMP)hook_default);
	orig_desc = (fn_desc_t)replace_own(lib, @selector(newFunctionWithDescriptor:error:), (IMP)hook_desc);
	ready = orig_desc != NULL;
	say("I FNW %s: %s, %lu list entries loaded from %s, hooks file=%d url=%d bundle=%d default=%d desc=%d", proc, disabled ? "DISABLED" : "active",
	    (unsigned long)[entries count], list_path, orig_file != NULL, orig_url != NULL, orig_bundle != NULL, orig_default != NULL, orig_desc != NULL);
	[pool release];
}
