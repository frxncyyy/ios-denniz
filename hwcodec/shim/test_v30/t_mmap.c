/* SPDX-License-Identifier: MIT */
/* t_mmap.c -- anonymous and descriptor-backed mmap/munmap/getpagesize shims. */
#include <stdint.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"
#include "v30test.h"

#define MAP_FAILED_ 0xffffffffu

extern uint64_t t_vfs_last_off;
extern int t_vfs_calls;

static uint32_t mmap_call(const char *name, uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags,
			  uint32_t fd, uint64_t off)
{
	uint32_t args[6];

	args[0] = addr;
	args[1] = len;
	args[2] = prot;
	args[3] = flags;
	args[4] = fd;
	args[5] = (uint32_t)off;
	if (strcmp(name, "mmap64") == 0) {
		uint32_t a8[8];
		a8[0] = addr;
		a8[1] = len;
		a8[2] = prot;
		a8[3] = flags;
		a8[4] = fd;
		a8[5] = 0xdeadbeefu;              /* padding slot: must be ignored */
		a8[6] = (uint32_t)off;
		a8[7] = (uint32_t)(off >> 32);
		return t_call(n2shim_heap, name, a8, 8, NULL);
	}
	return t_call(n2shim_heap, name, args, 6, NULL);
}

void test_mmap(void)
{
	struct n2a32_cpu *c = t_cpu();
	uint32_t p;

	T_CHECK(t_call(n2shim_heap, "getpagesize", NULL, 0, NULL) == 4096);

	/* anonymous, anywhere */
	p = mmap_call("mmap", 0, 100, 3, 0x22, 0xffffffffu, 0);
	t_checkf(p != 0 && p != MAP_FAILED_ && (p % 4096) == 0, "anonymous mmap = 0x%x", p);
	n2a32_st8(c, p, 0x5a);
	T_CHECK(n2a32_ld8(c, p) == 0x5a);
	T_CHECK(n2a32_ld8(c, p + 99) == 0);      /* the length is rounded up to pages, and zero filled */

	/* MAP_FIXED maps at the requested address */
	p = mmap_call("mmap", 0x30000000u, 8192, 3, 0x32, 0xffffffffu, 0);
	T_CHECK(p == 0x30000000u);

	/* a zero length fails */
	T_CHECK(mmap_call("mmap", 0, 0, 3, 0x22, 0xffffffffu, 0) == MAP_FAILED_);

	/* descriptor-backed: forwarded to V33's n2vfs_mmap() when it is linked, refused otherwise */
	t_vfs_calls = 0;
	t_vfs_last_off = 0;
	p = mmap_call("mmap", 0, 4096, 3, 0x02, 7, 0x1234);
#if defined(V30_NO_VFS)
	T_CHECK(p == MAP_FAILED_);
	t_checkf(t_vfs_calls == 0, "weak default was not called: %d", t_vfs_calls);
#else
	t_checkf(p == 0x20001234u, "forwarded mmap = 0x%x", p);
	t_checkf(t_vfs_calls == 1 && t_vfs_last_off == 0x1234u, "forwarded off = 0x%llx",
		 (unsigned long long)t_vfs_last_off);
#endif

	/* an unknown descriptor is refused even with the hook */
	T_CHECK(mmap_call("mmap", 0, 4096, 3, 0x02, 9, 0) == MAP_FAILED_);

	/* mmap64: anonymous and forwarded, with the 64-bit offset in slots 6/7 */
	p = mmap_call("mmap64", 0, 16384, 3, 0x22, 0xffffffffu, 0);
	t_checkf(p != 0 && p != MAP_FAILED_ && (p % 4096) == 0, "mmap64 anonymous = 0x%x", p);
	t_vfs_calls = 0;
	t_vfs_last_off = 0;
	p = mmap_call("mmap64", 0, 4096, 3, 0x02, 7, 0x123456789ULL);
#if defined(V30_NO_VFS)
	T_CHECK(p == MAP_FAILED_);
	t_checkf(t_vfs_calls == 0, "weak default was not called: %d", t_vfs_calls);
#else
	t_checkf(p == 0x20006789u, "forwarded mmap64 = 0x%x", p);
	t_checkf(t_vfs_calls == 1 && t_vfs_last_off == 0x123456789ULL, "forwarded off64 = 0x%llx",
		 (unsigned long long)t_vfs_last_off);
#endif

	/* munmap succeeds (the pages stay allocated; documented) */
	T_CHECK(t_call(n2shim_heap, "munmap", (uint32_t[]){ 0x30000000u, 8192 }, 2, NULL) == 0);
}
