/*
 * Which Metal calls block, and for how long.  The first cold interactions wait on MTLCompilerService (Apple's compiler process), but
 * -newLibraryWithSource: is not what asks it.  This wraps every object-creating method ("new...") of the device and the library classes,
 * plus a few queue/buffer calls, with a timer: a call that takes 4 ms or more is logged with its class, selector, thread and wall time, and
 * every call is counted per selector.  The original method runs unchanged; only methods whose arguments and result are plain integers,
 * pointers or objects (registers x2..x6) are wrapped.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <time.h>

#define SLOW_NS 4000000ull
#define MAX_SAID 600
#define MAX_SEL 160

static unsigned said;
static struct { char name[72]; unsigned calls; uint64_t ns, max; } tab[MAX_SEL];
static unsigned ntab, wrapped_methods;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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
note(unsigned idx, uint64_t ns)
{
	BOOL say = NO;

	pthread_mutex_lock(&lock);
	tab[idx].calls++;
	tab[idx].ns += ns;
	if (ns > tab[idx].max) {
		tab[idx].max = ns;
	}
	if (ns >= SLOW_NS && said < MAX_SAID) {
		said++;
		say = YES;
	}
	pthread_mutex_unlock(&lock);
	if (say) {
		char thr[48] = "", line[240];

		pthread_getname_np(pthread_self(), thr, sizeof(thr));
		snprintf(line, sizeof(line), "I SLOW %s took %llu.%02llu ms on '%s'", tab[idx].name, (unsigned long long)(ns / 1000000ull),
			 (unsigned long long)((ns / 10000ull) % 100ull), thr[0] ? thr : "-");
		nrw_log_line(line, strlen(line));
	}
}

/* a summary line per selector, called now and then from whichever wrapped call finds 10 s have passed */
static void
summary(void)
{
	static uint64_t last;
	uint64_t t = now_ns();
	char line[240];
	int o;

	if (t - last < 10000000000ull) {
		return;
	}
	last = t;
	pthread_mutex_lock(&lock);
	o = snprintf(line, sizeof(line), "I SLOWSUM ");
	for (unsigned k = 0; k < 4; k++) {
		unsigned best = MAX_SEL;
		uint64_t top = 0;

		for (unsigned i = 0; i < ntab; i++) {
			if (tab[i].ns > top) {
				top = tab[i].ns;
				best = i;
			}
		}
		if (best == MAX_SEL || o >= (int)sizeof(line) - 90) {
			break;
		}
		o += snprintf(line + o, sizeof(line) - (size_t)o, "[%s: %u calls, %llu ms, max %llu ms] ", tab[best].name, tab[best].calls,
			      (unsigned long long)(tab[best].ns / 1000000ull), (unsigned long long)(tab[best].max / 1000000ull));
		tab[best].ns = 0;
	}
	pthread_mutex_unlock(&lock);
	if (o > 12) {
		nrw_log_line(line, (size_t)o);
	}
}

/* plain integers, pointers and objects only; at most five arguments after self and _cmd; result void/integer/pointer/object */
static BOOL
plain(Method m)
{
	char type[64];
	unsigned n = method_getNumberOfArguments(m);

	if (n > 7) {
		return NO;
	}
	method_getReturnType(m, type, sizeof(type));
	if (strchr("vcCsSiIlLqQB@#:*^", type[0]) == NULL) {
		return NO;
	}
	for (unsigned i = 2; i < n; i++) {
		method_getArgumentType(m, i, type, sizeof(type));
		if (strchr("cCsSiIlLqQB@#:*^", type[0]) == NULL) {
			return NO;
		}
	}
	return YES;
}

static void
wrap_class(const char *cname, BOOL only_new)
{
	Class cls = objc_getClass(cname);
	unsigned count = 0;
	Method *list;

	if (cls == Nil) {
		return;
	}
	list = class_copyMethodList(cls, &count);
	for (unsigned i = 0; i < count; i++) {
		SEL sel = method_getName(list[i]);
		const char *name = sel_getName(sel);
		IMP orig = method_getImplementation(list[i]);
		unsigned idx;
		IMP wrapper;

		if (only_new ? strncmp(name, "new", 3) != 0 :
		    (strcmp(name, "commit") && strcmp(name, "waitUntilCompleted") && strcmp(name, "waitUntilScheduled") && strncmp(name, "present", 7))) {
			continue;
		}
		if (!plain(list[i])) {
			continue;
		}
		if (ntab >= MAX_SEL) {
			break;
		}
		idx = ntab++;
		snprintf(tab[idx].name, sizeof(tab[idx].name), "%s %s", cname, name);
		wrapper = imp_implementationWithBlock(^uintptr_t(id self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
			uint64_t t0 = now_ns();
			uintptr_t r = ((uintptr_t (*)(id, SEL, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))orig)(self, sel, a, b, c, d, e);

			note(idx, now_ns() - t0);
			summary();
			return r;
		});
		method_setImplementation(list[i], wrapper);
		wrapped_methods++;
	}
	free(list);
}

__attribute__((constructor)) static void
nord2_srcprobe_install(void)
{
	char line[96];

	if (access("/System/Library/Extensions/Nord2Metal.bundle/diag", F_OK) != 0) {
		return;                      /* measurement only: the marker file `diag` in the bundle turns it on */
	}
	{                              /* memory pressure events of this process, with the uptime and the free pages: texture purges and flicker follow them */
		static dispatch_source_t src;
		const char *proc = getprogname();

		if (proc != NULL && (!strcmp(proc, "backboardd") || !strcmp(proc, "SpringBoard"))) {
			src = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
						     DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL,
						     dispatch_queue_create("n2.memorypressure", DISPATCH_QUEUE_SERIAL));
			dispatch_source_set_event_handler(src, ^{
				unsigned long m = dispatch_source_get_data(src);
				vm_statistics64_data_t vm;
				mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
				unsigned free_mib = 0;
				char l[200];

				if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &cnt) == KERN_SUCCESS) {
					free_mib = (unsigned)((uint64_t)vm.free_count * vm_page_size >> 20);
				}
				snprintf(l, sizeof(l), "I MEMP %s: memory pressure %s%s%s at uptime %llu ms, %u MiB free",
					 getprogname(), (m & DISPATCH_MEMORYPRESSURE_NORMAL) ? "NORMAL " : "", (m & DISPATCH_MEMORYPRESSURE_WARN) ? "WARN " : "",
					 (m & DISPATCH_MEMORYPRESSURE_CRITICAL) ? "CRITICAL" : "", (unsigned long long)(now_ns() / 1000000ull), free_mib);
				nrw_log_line(l, strlen(l));
			});
			dispatch_resume(src);
		}
	}
	wrap_class("_MTLDevice", YES);
	wrap_class("TeguComputeDevice", YES);
	wrap_class("_MTLLibrary", YES);
	wrap_class("_MTLCommandQueue", YES);
	wrap_class("_MTLCommandBuffer", NO);
	snprintf(line, sizeof(line), "I SLOW probe installed: %u methods wrapped", wrapped_methods);
	nrw_log_line(line, strlen(line));
}
