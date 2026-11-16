/* Force-included (-include) when y2sios' redwoodmetal.m / redwooddrawable.m are built for the Nord2: their calls
 * to the "RedwoodMali" IOKit service become np_call()/np_map() of the process's one Nord2GPU connection, which lives
 * in libredwoodair (nord2_pan.c).  One connection matters: GPU addresses are per connection on this kernel.
 * SPDX-License-Identifier: MIT */
#ifndef NORD2_RW_COMPAT_H
#define NORD2_RW_COMPAT_H
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <IOKit/IOKitLib.h>

#define NRW_SERVICE     0x4e325331u     /* the token "service" and "connection" both are */

static struct {
	int tried;
	int (*open_iokit)(void);
	int (*call)(unsigned, void *, size_t, size_t *);
	int (*map)(uint32_t, uint64_t *, uint64_t *);
	int (*unmap)(uint32_t, uint64_t);
} nrw;

static int
nrw_load(void)
{
	if (!nrw.tried) {
		void *h = dlopen("/System/Library/Extensions/Nord2Metal.bundle/libredwoodair.dylib", RTLD_NOW);

		nrw.tried = 1;
		if (h) {
			nrw.open_iokit = (int (*)(void))dlsym(h, "np_open_iokit");
			nrw.call = (int (*)(unsigned, void *, size_t, size_t *))dlsym(h, "np_call");
			nrw.map = (int (*)(uint32_t, uint64_t *, uint64_t *))dlsym(h, "np_map");
			nrw.unmap = (int (*)(uint32_t, uint64_t))dlsym(h, "np_unmap");
		}
	}
	return nrw.open_iokit && nrw.call && nrw.map && nrw.unmap && nrw.open_iokit() == 0;
}

/* m2m.h's records (its device does not exist here) go to the kernel log. */
static void
nrw_log_line(const char *text, size_t length)
{
	static void (*logger)(const char *, size_t);

	if (!logger) {                  /* the library may not be loaded yet: ask again at the next record */
		void *h = dlopen("/System/Library/Extensions/Nord2Metal.bundle/libredwoodair.dylib", RTLD_NOW | RTLD_NOLOAD);

		if (h)
			logger = (void (*)(const char *, size_t))dlsym(h, "np_log");
	}
	if (logger)
		logger(text, length);
}

static CFMutableDictionaryRef
nrw_matching(const char *name)
{
	return (CFMutableDictionaryRef)(uintptr_t)name;         /* only compared by nrw_service */
}

static io_service_t
nrw_service(mach_port_t port, CFDictionaryRef matching)
{
	const char *name = (const char *)(uintptr_t)matching;

	(void)port;
	return name && strcmp(name, "RedwoodMali") == 0 && nrw_load() ? NRW_SERVICE : 0;
}

static kern_return_t
nrw_open(io_service_t service, task_port_t task, uint32_t type, io_connect_t *connection)
{
	(void)task;
	(void)type;
	if (service != NRW_SERVICE)
		return kIOReturnNotFound;
	*connection = NRW_SERVICE;
	return KERN_SUCCESS;
}

static kern_return_t
nrw_call(io_connect_t c, uint32_t selector, const void *in, size_t in_size, void *out, size_t *out_size)
{
	size_t n = in_size;

	if (c != NRW_SERVICE || !nrw.call)
		return kIOReturnNotOpen;
	if (out && out != in)
		memcpy(out, in, in_size < (out_size ? *out_size : in_size) ? in_size : (out_size ? *out_size : in_size));
	if (nrw.call(selector, out ? out : (void *)(uintptr_t)in, out && out_size && *out_size < in_size ? *out_size : in_size, &n) != 0)
		return kIOReturnError;
	if (out_size)
		*out_size = n < *out_size ? n : *out_size;
	return KERN_SUCCESS;
}

static kern_return_t
nrw_map(io_connect_t c, uint32_t handle, task_port_t task, mach_vm_address_t *address, mach_vm_size_t *size, IOOptionBits options)
{
	uint64_t a = 0, s = 0;

	(void)task;
	(void)options;
	if (c != NRW_SERVICE || !nrw.map || nrw.map(handle, &a, &s) != 0)
		return kIOReturnError;
	*address = a;
	*size = s;
	return KERN_SUCCESS;
}

static kern_return_t
nrw_unmap(io_connect_t c, uint32_t handle, task_port_t task, mach_vm_address_t address)
{
	(void)task;
	return c == NRW_SERVICE && nrw.unmap && nrw.unmap(handle, address) == 0 ? KERN_SUCCESS : kIOReturnError;
}

static kern_return_t
nrw_release(io_object_t o)
{
	return o == NRW_SERVICE ? KERN_SUCCESS : IOObjectRelease(o);
}

static kern_return_t
nrw_close(io_connect_t c)
{
	return c == NRW_SERVICE ? KERN_SUCCESS : IOServiceClose(c);     /* the connection lives with the process */
}

#define IOServiceMatching               nrw_matching
#define IOServiceGetMatchingService     nrw_service
#define IOServiceOpen                   nrw_open
#define IOConnectCallStructMethod       nrw_call
#define IOConnectMapMemory64            nrw_map
#define IOConnectUnmapMemory64          nrw_unmap
#define IOObjectRelease                 nrw_release
#define IOServiceClose                  nrw_close
#endif
