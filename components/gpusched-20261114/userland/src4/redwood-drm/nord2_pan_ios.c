/* iOS side of the Panfrost request layer: the one Nord2GPU connection of the process (IOKit) and IOSurface
 * lookup.  Included into the libdrm stand-in, so libredwoodair owns the connection and the Metal driver reaches
 * it through the exported np_* functions.  SPDX-License-Identifier: MIT */
#include <dlfcn.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

#include "nord2_pan.h"

static io_connect_t np_connection;
static pthread_once_t np_once = PTHREAD_ONCE_INIT;
static int np_once_result = -1;

static int ios_call(void *o, unsigned sel, const uint64_t *in, unsigned ni, const void *bytes, size_t size,
		    uint64_t *out, unsigned *no, void *ob, size_t *os)
{
	uint32_t n = no ? *no : 0;
	int kr;

	(void)o;
	kr = IOConnectCallMethod(np_connection, sel, in, ni, bytes, bytes ? size : 0, out, no ? &n : NULL, ob, os);
	if (no)
		*no = n;
	return kr;
}

static int ios_map(void *o, unsigned handle, uint64_t *address, uint64_t *size)
{
	mach_vm_address_t a = 0;
	mach_vm_size_t s = 0;

	(void)o;
	if (IOConnectMapMemory64(np_connection, handle, mach_task_self(), &a, &s, kIOMapAnywhere))
		return -1;
	*address = a;
	*size = s;
	return 0;
}

static int ios_unmap(void *o, unsigned handle, uint64_t address)
{
	(void)o;
	return IOConnectUnmapMemory64(np_connection, handle, mach_task_self(), address);
}

typedef void *np_surf_t;
static np_surf_t (*s_lookup)(uint32_t);
static void *(*s_base)(np_surf_t);
static size_t (*s_alloc)(np_surf_t), (*s_bpr)(np_surf_t), (*s_w)(np_surf_t), (*s_h)(np_surf_t), (*s_planes)(np_surf_t);
static uint32_t (*s_fmt)(np_surf_t);
static void *(*s_pbase)(np_surf_t, size_t);
static size_t (*s_pbpr)(np_surf_t, size_t);

/* A surface looked up stays retained for the life of its buffer (its pages are the buffer's CPU view). */
static int ios_surface(void *o, uint32_t id, struct np_surface_info *out)
{
	np_surf_t s;

	(void)o;
	if (!s_lookup || !(s = s_lookup(id)))
		return -1;
	char *base = s_base(s);

	out->cpu = (uint64_t)(uintptr_t)base;
	out->size = s_alloc(s);
	out->rowBytes = (uint32_t)s_bpr(s);
	out->width = (uint32_t)s_w(s);
	out->height = (uint32_t)s_h(s);
	out->pixelFormat = s_fmt ? s_fmt(s) : 0;
	out->planes = s_planes ? (uint32_t)s_planes(s) : 1;
	for (unsigned i = 0; i < 2; i++) {
		if (i < out->planes && out->planes > 1 && s_pbase && s_pbpr) {
			out->planeOffset[i] = (uint32_t)((char *)s_pbase(s, i) - base);
			out->planeRowBytes[i] = (uint32_t)s_pbpr(s, i);
		} else {
			out->planeOffset[i] = 0;
			out->planeRowBytes[i] = i == 0 ? out->rowBytes : 0;
		}
	}
	return 0;
}

static void np_open_once(void)
{
	io_service_t service = IOServiceGetMatchingService(0, IOServiceMatching("Nord2GPU"));
	void *iosurface = dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface", RTLD_NOW);
	struct np_ops ops = { .call = ios_call, .map = ios_map, .unmap = ios_unmap, .surface = ios_surface };

	if (iosurface) {
		s_lookup = dlsym(iosurface, "IOSurfaceLookup");
		s_base = dlsym(iosurface, "IOSurfaceGetBaseAddress");
		s_alloc = dlsym(iosurface, "IOSurfaceGetAllocSize");
		s_bpr = dlsym(iosurface, "IOSurfaceGetBytesPerRow");
		s_w = dlsym(iosurface, "IOSurfaceGetWidth");
		s_h = dlsym(iosurface, "IOSurfaceGetHeight");
		s_planes = dlsym(iosurface, "IOSurfaceGetPlaneCount");
		s_fmt = dlsym(iosurface, "IOSurfaceGetPixelFormat");
		s_pbase = dlsym(iosurface, "IOSurfaceGetBaseAddressOfPlane");
		s_pbpr = dlsym(iosurface, "IOSurfaceGetBytesPerRowOfPlane");
		if (!s_base || !s_alloc || !s_bpr || !s_w || !s_h)
			s_lookup = NULL;
	}
	if (!service)
		return;
	if (IOServiceOpen(service, mach_task_self(), 0, &np_connection) == 0 && np_init(&ops) == 0)
		np_once_result = 0;
	IOObjectRelease(service);
}

/* The process's connection, opened once; 0 or -1. */
__attribute__((visibility("default"))) int np_open_iokit(void)
{
	pthread_once(&np_once, np_open_once);
	return np_once_result;
}
