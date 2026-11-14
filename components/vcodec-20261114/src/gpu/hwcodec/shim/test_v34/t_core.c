/* SPDX-License-Identifier: MIT */
/*
 * t_core.c -- V34 against the real loader and the real vendor object.
 *
 * t_unit tests the shim logic with fakes. This test links V03's real loader (loader/n2elf.c + n2elf_reloc.c) and
 * loads the phone's own libvcodec_utility.so, then checks the parts that only a real ELF set can prove:
 *
 *   - all 35 android-table names are resolvable through the linked set (a missing/misspelt table entry would show
 *     up as an unresolved import instead);
 *   - dlopen of a loaded soname returns a handle, dlopen of a name outside the set returns NULL with a guest
 *     dlerror, and dlsym through the handle equals n2elf_sym();
 *   - dladdr of a real exported function fills dli_fname / dli_sname / dli_saddr in guest memory;
 *   - __libc_init runs the Thumb main from guest_thumb.s with the runner's argc/argv/envp and records the exit;
 *   - __cxa_atexit + __cxa_finalize really call guest code.
 *
 * The vendor object is the user's own phone file; it is read from gpu/build/cam-vendor-20261109/vfs (or
 * N2HW_VENDOR_DIR) and never copied. If it is absent the test fails with a clear message (the tree ships it).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2vc_hw.h"
#include "t_v34.h"

static struct n2a32_mem *M;
static struct n2a32_cpu C;
static struct n2elf_set *S;
static uint32_t g_bump;
static uint32_t g_stack;

static uint32_t gstr(const char *s)
{
	uint32_t a = g_bump;

	while (*s != '\0')
		n2a32_st8(&C, g_bump++, (uint8_t)*s++);
	n2a32_st8(&C, g_bump++, 0);
	return a;
}

static uint32_t gbuf(size_t n)
{
	uint32_t a = (g_bump + 7u) & ~7u;

	g_bump = a + (uint32_t)n;
	memset(M->base + a, 0xcc, n);
	return a;
}

static const char *gread(uint32_t a, char *out, size_t cap)
{
	size_t i = 0;

	if (a == 0 || cap == 0) {
		if (cap > 0)
			out[0] = '\0';
		return out;
	}
	while (i + 1 < cap) {
		char ch = (char)n2a32_ld8(&C, a + (uint32_t)i);

		out[i++] = ch;
		if (ch == '\0')
			return out;
	}
	out[cap - 1] = '\0';
	return out;
}

static void call_shim(const char *name, const uint32_t args[4], const uint32_t *stack, unsigned nstack)
{
	(void)stack;
	(void)nstack;
	t_call(&C, n2shim_android, name, args, NULL, 0);
}

int main(void)
{
	const char *vendor = getenv("N2HW_VENDOR_DIR");
	char path[512], err[160];
	unsigned unresolved = 0;
	char buf[200];

	if (vendor == NULL || vendor[0] == '\0')
		vendor = "gpu/build/cam-vendor-20261109/vfs";
	M = t_mem_new();
	t_cpu_setup(&C, M);
	g_bump = T_DATA_ADDR + 0x8000u;
	g_stack = n2a32_mem_alloc(M, 64 * 1024, 16);
	CHECK(g_stack != 0);
	C.r[13] = g_stack + 64 * 1024 - 8;

	S = n2elf_set_create(M);
	CHECK(S != NULL);
	if (S == NULL)
		return 1;
	n2elf_set_add_shims(S, n2shim_android);
	n2elf_set_add_data(S, n2shim_data_all);
	n2shim_set = S;

	snprintf(path, sizeof path, "%s/lib/libvcodec_utility.so", vendor);
	if (n2elf_load(S, path, "libvcodec_utility.so", err) == NULL) {
		printf("FATAL: cannot load %s: %s\n", path, err);
		printf("N2TEST V34 FAIL t_core vendor-object-missing\n");
		return 1;
	}
	if (n2elf_link(S, &unresolved, err) != 0) {
		printf("FATAL: link failed: %s\n", err);
		printf("N2TEST V34 FAIL t_core link\n");
		return 1;
	}
	printf("linked libvcodec_utility.so: %u unresolved function imports\n", unresolved);
	CHECK(unresolved > 0);           /* the five objects import 146 names in total; one object a subset */

	/* every name of the android table is in the linked namespace (functions from the shim table) */
	{
		static const char *const imports[] = {
			"__aeabi_unwind_cpp_pr0", "__android_log_print", "__android_log_vprint", "__cxa_atexit",
			"__cxa_finalize", "__gnu_Unwind_Find_exidx", "__libc_init", "__register_atfork",
			"atrace_begin_body", "atrace_end_body", "atrace_get_enabled_tags", "atrace_int_body",
			"dladdr", "dlclose", "dlerror", "dlopen", "dlsym", "exp2",
			"ion_alloc", "ion_alloc_camera", "ion_custom_ioctl", "ion_free", "ion_import", "ion_mmap",
			"ion_munmap", "ion_share", "ion_share_close", "log", "mt_ion_close", "mt_ion_open",
			"perf_lock_acq", "perf_lock_rel", "property_get", "sec_drv_base_check", "sec_drv_info_update",
			NULL,
		};
		int i;

		for (i = 0; imports[i] != NULL; i++) {
			uint32_t a = n2elf_sym(S, imports[i]);

			CHECK(a != 0);
			if (a == 0)
				printf("FAIL: %s does not resolve\n", imports[i]);
			/* a function shim is a host-call slot in [BASE, BASE + 4*MAX) */
			CHECK(a >= N2A32_HOSTCALL_BASE && a < N2A32_HOSTCALL_BASE + 4u * N2A32_HOSTCALL_MAX);
		}
	}

	/* dlopen of a loaded soname, by soname and by path, plus the misses */
	{
		uint32_t h1, h2;
		uint32_t args[4];

		args[0] = 0;
		call_shim("dlopen", args, NULL, 0);
		CHECK_EQ(C.r[0], 1);

		args[0] = gstr("libvcodec_utility.so");
		call_shim("dlopen", args, NULL, 0);
		h1 = C.r[0];
		CHECK(h1 >= 0x100);

		args[0] = gstr("/vendor/lib/libvcodec_utility.so");
		call_shim("dlopen", args, NULL, 0);
		h2 = C.r[0];
		CHECK_EQ(h1, h2);

		args[0] = gstr("libvcodecdrv.so");
		call_shim("dlopen", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		args[0] = gstr("AVCSecureVdecCA.so");
		call_shim("dlopen", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		call_shim("dlerror", args, NULL, 0);
		CHECK(C.r[0] != 0);
		CHECK(strstr(gread(C.r[0], buf, sizeof buf), "AVCSecureVdecCA.so") != NULL);

		/* dlsym through the handle is the loader's answer */
		args[0] = h1;
		args[1] = gstr("eVideoMemAlloc");
		call_shim("dlsym", args, NULL, 0);
		CHECK_EQ(C.r[0], n2elf_sym(S, "eVideoMemAlloc"));
		CHECK(C.r[0] != 0);
		args[0] = 1;
		args[1] = gstr("eVideoMemAlloc");
		call_shim("dlsym", args, NULL, 0);
		CHECK_EQ(C.r[0], n2elf_sym(S, "eVideoMemAlloc"));
		args[0] = h1;
		args[1] = gstr("no_such_vendor_symbol");
		call_shim("dlsym", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		call_shim("dlerror", args, NULL, 0);
		CHECK(C.r[0] != 0);
		/* a shim symbol resolves through the same namespace */
		args[0] = 1;
		args[1] = gstr("property_get");
		call_shim("dlsym", args, NULL, 0);
		CHECK_EQ(C.r[0], n2elf_sym(S, "property_get"));
	}

	/* dladdr of a real exported function */
	{
		uint32_t fn = n2elf_sym(S, "eVideoMemAlloc");
		uint32_t info = gbuf(16);
		uint32_t args[4];

		CHECK(fn != 0);
		args[0] = fn;
		args[1] = info;
		call_shim("dladdr", args, NULL, 0);
		CHECK_EQ(C.r[0], 1);
		CHECK_STR(gread(n2a32_ld32(&C, info + 0), buf, sizeof buf), "libvcodec_utility.so");
		CHECK_EQ(n2a32_ld32(&C, info + 4), 0);
		CHECK_STR(gread(n2a32_ld32(&C, info + 8), buf, sizeof buf), "eVideoMemAlloc");
		CHECK_EQ(n2a32_ld32(&C, info + 12), fn & ~1u);

		/* an address inside the function: same symbol, same symbol start */
		args[0] = (fn & ~1u) + 4;
		args[1] = info;
		call_shim("dladdr", args, NULL, 0);
		CHECK_EQ(C.r[0], 1);
		CHECK_STR(gread(n2a32_ld32(&C, info + 8), buf, sizeof buf), "eVideoMemAlloc");
		CHECK_EQ(n2a32_ld32(&C, info + 12), fn & ~1u);
	}

	/* property_get through the real linked namespace (same shim, still the same table) */
	{
		uint32_t args[4];
		uint32_t gval = gbuf(128);

		args[0] = gstr("ro.build.type");
		args[1] = gval;
		args[2] = gstr("x");
		args[3] = 0;
		call_shim("property_get", args, NULL, 0);
		CHECK_EQ(C.r[0], 4);
		CHECK_STR(gread(gval, buf, sizeof buf), "user");
	}

	/* __libc_init runs the Thumb main with the runner's arguments and records the exit */
	{
		uint32_t args[4];

		t_cpu_setup(&C, M);
		C.r[13] = g_stack + 64 * 1024 - 8;
		n2shim_libc_argc = 5;
		n2shim_libc_argv = 0x40001000u;   /* main_args only tests for NULL */
		n2shim_libc_envp = 0x40002000u;
		n2shim_libc_status = -1;
		args[0] = 0;
		args[1] = 0;
		args[2] = t_guest_fn(M, T_OFF_main_args);
		args[3] = 0;
		call_shim("__libc_init", args, NULL, 0);
		CHECK_EQ(C.r[0], 5u + 0x100u + 0x10000u);
		CHECK_EQ(n2shim_libc_status, 5 + 0x100 + 0x10000);
		CHECK_EQ(C.stop_request, 1u);
	}

	/* __cxa_atexit / __cxa_finalize really run guest code */
	{
		uint32_t args[4];
		uint32_t counter = gbuf(4);

		n2a32_st32(&C, counter, 0);
		C.stop_request = 0;   /* __libc_init above ended the guest process; start a fresh call */
		args[0] = t_guest_fn(M, T_OFF_atexit_inc);
		args[1] = counter;
		args[2] = 0;
		call_shim("__cxa_atexit", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		args[0] = 0;
		call_shim("__cxa_finalize", args, NULL, 0);
		CHECK_EQ(n2a32_ld32(&C, counter), 1);
		call_shim("__cxa_finalize", args, NULL, 0);
		CHECK_EQ(n2a32_ld32(&C, counter), 1);
	}

	n2elf_set_destroy(S);
	n2shim_set = NULL;
	printf("T34CORE %d/%d\n", t_checks - t_fails, t_checks);
	if (t_fails != 0)
		return 1;
	printf("N2TEST V34CORE PASS %d/%d\n", t_checks, t_checks);
	return 0;
}
