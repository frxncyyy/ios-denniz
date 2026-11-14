/* SPDX-License-Identifier: MIT */
/*
 * t_unit.c -- V34 host tests: every shim in n2shim_android[] driven through a fake cpu with literal expectations,
 * the real V02 memory + n2a32_call for the guest-code paths, a fake n2vc_hw for the ion path and a fake ELF symbol
 * provider for the dl* handle/error logic. The real loader is exercised by t_core.c.
 *
 * Fakes owned by this file (each is the piece a different package owns):
 *   n2elf_sym / n2elf_describe   stand-in for V03's loader: two known symbols and three known descriptions, so the
 *                                dlopen/dlsym/dlerror/dladdr logic (which is this package's) can be tested alone.
 *   n2vc_dma_guest_map/unmap     stand-in for V41's DMA mirror: a plain n2a32_mem_alloc() of the requested bytes.
 *   struct n2vc_hw               a 60-line recording fake: dma_alloc hands out handle 1,2,... and pa
 *                                0xda800000,0xda900000,...; the counts are asserted literally.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2vc_hw.h"
#include "t_v34.h"

/* ------------------------------------------------------------------------------------------------------------------
 * Fakes
 * ---------------------------------------------------------------------------------------------------------------- */

static const uint32_t FAKE_PA0 = 0xda800000u;
static const uint32_t FAKE_TEXT_SYM = 0x9100u;

uint32_t n2elf_sym(struct n2elf_set *s, const char *name)
{
	(void)s;
	if (name == NULL)
		return 0;
	if (strcmp(name, "found_symbol") == 0 || strcmp(name, "dlsym_target") == 0)
		return FAKE_TEXT_SYM;
	if (strcmp(name, "VCodecConfigMCIPort") == 0)   /* the probe dlopen("libvcodec_utility.so") uses */
		return 0x9200u;
	return 0;
}

const char *n2elf_describe(struct n2elf_set *s, uint32_t addr, char *buf, size_t len)
{
	/* mimics the real describe: the address is compared with bit 0 clear, an address inside the object but outside
	 * any symbol gives "soname+0xN" (V34 passes addr + 1 to work around V03's unmasked Thumb st_value compare) */
	uint32_t a = addr & ~1u;

	(void)s;
	if (a >= 0x9100u && a < 0x9120u)
		snprintf(buf, len, "libtest.so:found_symbol+0x%x", a - 0x9100u);
	else if (a >= 0x1230u && a < 0x1240u)
		snprintf(buf, len, "libtest.so+0x%x", a - 0x1230u);
	else if (a >= 0x5670u && a < 0x5680u)
		snprintf(buf, len, "hostcall:malloc");
	else
		snprintf(buf, len, "0x%08x", a);
	return buf;
}

/* V41's DMA mirror stand-in: one fresh guest allocation per map, nothing to copy (the fake hw has no backing
 * store; dma_rw is only counted). */
int n2vc_dma_guest_map(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle, uint32_t bytes,
		       uint32_t *guest_addr)
{
	uint32_t ga;

	(void)h;
	(void)handle;
	if (guest_addr == NULL || bytes == 0)
		return -EINVAL;
	ga = n2a32_mem_alloc(m, bytes, 0x1000);
	if (ga == 0)
		return -ENOMEM;
	*guest_addr = ga;
	return 0;
}

int n2vc_dma_guest_unmap(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr)
{
	(void)h;
	(void)m;
	(void)guest_addr;
	return 0;
}

struct fakehw {
	int nalloc, nfree, nrw;
	uint32_t next_handle;
	uint32_t next_pa;
	uint32_t last_bytes, last_flags;
	uint32_t last_handle;
};

static int f_power(void *ctx, unsigned block, int on)
{
	(void)ctx; (void)block; (void)on;
	return 0;
}

static int f_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	(void)ctx; (void)ops; (void)count;
	if (done != NULL)
		*done = 0;
	return -EINVAL;
}

static int f_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	(void)ctx; (void)irq; (void)timeout_ms;
	if (status != NULL)
		*status = 0;
	return -ETIMEDOUT;
}

static int f_irq_clear(void *ctx, unsigned irq)
{
	(void)ctx; (void)irq;
	return 0;
}

static int f_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct fakehw *f = ctx;

	if (bytes == 0 || handle == NULL || pa == NULL)
		return -EINVAL;
	f->nalloc++;
	f->last_bytes = bytes;
	f->last_flags = flags;
	f->last_handle = f->next_handle;
	*handle = f->next_handle++;
	*pa = f->next_pa;
	f->next_pa += 0x00100000u;
	return 0;
}

static int f_dma_free(void *ctx, uint32_t handle)
{
	struct fakehw *f = ctx;

	(void)handle;
	f->nfree++;
	return 0;
}

static int f_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct fakehw *f = ctx;

	(void)handle; (void)offset; (void)host; (void)bytes; (void)write;
	f->nrw++;
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------
 * Guest helpers
 * ---------------------------------------------------------------------------------------------------------------- */

static struct n2a32_mem *M;
static struct n2a32_cpu C;
static uint32_t g_bump = T_DATA_ADDR + 0x8000u;

static void reset_cpu(void)
{
	t_cpu_setup(&C, M);
	g_bump = T_DATA_ADDR + 0x8000u;
}

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

static void w32(uint32_t a, uint32_t v) { n2a32_st32(&C, a, v); }
static uint32_t r32(uint32_t a) { return n2a32_ld32(&C, a); }

/* ------------------------------------------------------------------------------------------------------------------
 * Data table
 * ---------------------------------------------------------------------------------------------------------------- */

static void test_data_table(void)
{
	const struct n2shim_data *d = n2shim_data_all;
	int n = 0;

	CHECK(d != NULL);
	if (d == NULL)
		return;
	while (d[n].name != NULL)
		n++;
	CHECK_EQ(n, 2);
	CHECK_STR(d[0].name, "__stack_chk_guard");
	CHECK_EQ(d[0].size, 4);
	CHECK(d[0].init != NULL);
	if (d[0].init != NULL)
		CHECK_EQ(*(const uint32_t *)d[0].init, 0x4e325634u);   /* fixed canary "N2V4" */
	CHECK_STR(d[1].name, "stderr");
	CHECK_EQ(d[1].size, 4);
	CHECK(d[1].init != NULL);
	if (d[1].init != NULL)
		CHECK_EQ(*(const uint32_t *)d[1].init, 2u);           /* the FILE* value V33's stdio table knows */
	CHECK(d[2].name == NULL);
	CHECK_EQ(d[2].size, 0);
	CHECK(d[2].init == NULL);
}

/* ------------------------------------------------------------------------------------------------------------------
 * property_get
 * ---------------------------------------------------------------------------------------------------------------- */

static int prop_get(const char *key, const char *def, char *out, size_t cap)
{
	uint32_t gkey = gstr(key);
	uint32_t gval = gbuf(128);
	uint32_t gdef = def != NULL ? gstr(def) : 0;
	uint32_t args[4] = { gkey, gval, gdef, 0 };

	t_call(&C, n2shim_android, "property_get", args, NULL, 0);
	if (out != NULL)
		gread(gval, out, cap);
	return (int)C.r[0];
}

static void test_properties(void)
{
	char v[160];

	reset_cpu();
	n2shim_props_forget_unknown();

	/* a value the phone really has (evidence: the phone's own build.prop) */
	CHECK_EQ(prop_get("ro.build.type", "x", v, sizeof v), 4);
	CHECK_STR(v, "user");
	CHECK_EQ(prop_get("ro.vendor.mtk_config_max_dram_size", "x", v, sizeof v), 11);
	CHECK_STR(v, "0x800000000");
	CHECK_EQ(prop_get("ro.vendor.mtk_sec_video_path_support", "x", v, sizeof v), 1);
	CHECK_STR(v, "1");

	/* a key that exists in the table but is not present on the phone: the caller's default comes back, and it
	 * must not be reported as unknown */
	CHECK_EQ(prop_get("ro.config.low_ram", "false", v, sizeof v), 5);
	CHECK_STR(v, "false");
	CHECK_EQ(prop_get("vendor.mtk.vdec.h264.dump", "0", v, sizeof v), 1);
	CHECK_STR(v, "0");
	CHECK_EQ(prop_get("persist.vendor.vt.bitrate_ratio", "-1", v, sizeof v), 2);
	CHECK_STR(v, "-1");
	CHECK_EQ(n2shim_props_unknown_count(), 0);

	/* a key not in the table: default + reported once */
	CHECK_EQ(prop_get("vendor.n2.does.not.exist", "7", v, sizeof v), 1);
	CHECK_STR(v, "7");
	CHECK_EQ(n2shim_props_unknown_count(), 1);
	CHECK(t_log.calls >= 1);
	CHECK_STR(t_log.tag, "n2shim-props");
	CHECK(strstr(t_log.text, "vendor.n2.does.not.exist") != NULL);
	CHECK(strstr(t_log.text, "7") != NULL);
	/* the same unknown key again: default again, but no second report */
	{
		int before = t_log.calls;

		CHECK_EQ(prop_get("vendor.n2.does.not.exist", "8", v, sizeof v), 1);
		CHECK_STR(v, "8");
		CHECK_EQ(t_log.calls, before);
		CHECK_EQ(n2shim_props_unknown_count(), 1);
	}

	/* a second distinct key is reported too */
	CHECK_EQ(prop_get("vendor.n2.other.missing", "0", v, sizeof v), 1);
	CHECK_EQ(n2shim_props_unknown_count(), 2);

	/* no default: the value is the empty string, length 0 */
	CHECK_EQ(prop_get("ro.config.low_ram", NULL, v, sizeof v), 0);
	CHECK_STR(v, "");

	/* a default longer than PROP_VALUE_MAX-1 is truncated to 91 characters */
	{
		char big[200];
		int i;

		for (i = 0; i < 199; i++)
			big[i] = 'A';
		big[199] = '\0';
		CHECK_EQ(prop_get("ro.config.low_ram", big, v, sizeof v), 91);
		CHECK_EQ(strlen(v), 91u);
	}

	/* a NULL value pointer is tolerated (returns 0 without writing) */
	{
		uint32_t args[4];

		args[0] = gstr("ro.build.type");
		args[1] = 0;
		args[2] = gstr("x");
		args[3] = 0;
		t_call(&C, n2shim_android, "property_get", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
	}

	/* a NULL key is unknown -> default, reported (as the empty key) */
	{
		int before = t_log.calls;

		CHECK_EQ(prop_get("", "d", v, sizeof v), 1);
		CHECK_STR(v, "d");
		CHECK_EQ(t_log.calls, before + 1);
	}

	/* lookup is exact and case sensitive */
	CHECK(n2shim_prop_find("ro.build.type") != NULL);
	CHECK(n2shim_prop_find("ro.build.Type") == NULL);
	CHECK(n2shim_prop_find("vendor.mtk.venc.h264.DynamicI") != NULL);
	CHECK(n2shim_prop_find("vendor.mtk.venc.h264.dynamici") == NULL);

	/* table shape: sorted, unique, every entry has a reason, every non-NULL value is a non-empty string */
	{
		const struct n2prop_entry *e = n2shim_props;
		int n = 0;

		while (e[n].key != NULL) {
			CHECK(e[n].why != NULL && e[n].why[0] != '\0');
			if (e[n].value != NULL)
				CHECK(e[n].value[0] != '\0');
			if (n > 0)
				CHECK(strcmp(e[n - 1].key, e[n].key) < 0);
			n++;
		}
		CHECK(n >= 200);   /* the five objects name 203 keys today */
		CHECK(e[n].value == NULL && e[n].why == NULL);
	}
}

/* The coverage test: every key the five vendor objects contain (extracted by run_tests.sh with the real `strings`)
 * must be in the table. The explicit allow list below is the documented escape hatch for a string that matches the
 * property pattern but is not a property at all; it is empty today. */
static const char *const default_is_fine[] = {
	NULL,
};

static void test_property_keys(const char *path)
{
	FILE *f;
	char line[256];
	int keys = 0, missing = 0;

	f = fopen(path, "r");
	if (f == NULL) {
		printf("FATAL: cannot open the key list %s\n", path);
		t_fails++;
		t_checks++;
		return;
	}
	while (fgets(line, sizeof line, f) != NULL) {
		size_t n = strlen(line);
		size_t i;
		int allowed = 0;

		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (n == 0)
			continue;
		keys++;
		CHECK(n <= 92);
		for (i = 0; default_is_fine[i] != NULL; i++)
			if (strcmp(default_is_fine[i], line) == 0)
				allowed = 1;
		if (!allowed && n2shim_prop_find(line) == NULL) {
			printf("FAIL: vendor key not in the table: %s\n", line);
			missing++;
		}
	}
	fclose(f);
	CHECK_EQ(missing, 0);
	CHECK(keys >= 200);   /* 203 with the current vendor objects; keeps a truncated scan from passing */
	printf("property keys covered: %d of %d\n", keys - missing, keys);
}

/* ------------------------------------------------------------------------------------------------------------------
 * __android_log_print / __android_log_vprint
 * ---------------------------------------------------------------------------------------------------------------- */

static void test_log(void)
{
	uint32_t args[4];
	uint32_t fmt;

	reset_cpu();
	t_log_reset();

	fmt = gstr("%d %s %c");
	args[0] = 4;                     /* ANDROID_LOG_INFO */
	args[1] = gstr("TAGX");
	args[2] = fmt;
	args[3] = 7;
	{
		uint32_t stack[2];

		stack[0] = gstr("str");
		stack[1] = 'Z';
		t_call(&C, n2shim_android, "__android_log_print", args, stack, 2);
	}
	CHECK_EQ(C.r[0], 7);             /* "7 str Z" */
	CHECK_EQ(t_log.calls, 1);
	CHECK_EQ(t_log.prio, 4);
	CHECK_STR(t_log.tag, "TAGX");
	CHECK_STR(t_log.text, "7 str Z");

	/* a NULL tag is passed through as an empty tag; the format is still used */
	t_log_reset();
	args[0] = 2;
	args[1] = 0;
	args[2] = gstr("plain");
	args[3] = 0;
	t_call(&C, n2shim_android, "__android_log_print", args, NULL, 0);
	CHECK_EQ(C.r[0], 5);
	CHECK_STR(t_log.tag, "");
	CHECK_STR(t_log.text, "plain");

	/* a NULL format: -1 and a diagnostic line, like n2fmt's contract */
	t_log_reset();
	args[2] = 0;
	t_call(&C, n2shim_android, "__android_log_print", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -1);
	CHECK_EQ(t_log.calls, 1);
	CHECK_STR(t_log.text, "(null format)");

	/* the 1024-byte cut: a 2000-character format produces 1023 bytes and returns 1023 */
	{
		char big[2100];
		int i;

		for (i = 0; i < 2000; i++)
			big[i] = 'q';
		big[2000] = '\0';
		t_log_reset();
		args[0] = 3;
		args[1] = gstr("T");
		args[2] = gstr(big);
		args[3] = 0;
		t_call(&C, n2shim_android, "__android_log_print", args, NULL, 0);
		CHECK_EQ(C.r[0], 1023);
		CHECK_EQ(strlen(t_log.text), 1023u);
	}

	/* __android_log_vprint: prio, tag, fmt, va_list.  ARM EABI passes the va_list struct by
	 * value, so the register is the argument area itself (opus-dec1 HIGH finding; the old test
	 * passed a pointer to the area and only matched the old double-dereference in n2fmt_args_va). */
	{
		uint32_t area = gbuf(16);      /* two 32-bit argument slots */

		w32(area, 42);
		w32(area + 4, gstr("vs"));
		t_log_reset();
		args[0] = 5;
		args[1] = gstr("VP");
		args[2] = gstr("v=%d/%s");
		args[3] = area;
		t_call(&C, n2shim_android, "__android_log_vprint", args, NULL, 0);
		CHECK_EQ(C.r[0], 7);           /* "v=42/vs" */
		CHECK_EQ(t_log.prio, 5);
		CHECK_STR(t_log.tag, "VP");
		CHECK_STR(t_log.text, "v=42/vs");
	}

	/* a NULL va_list object must not read guest address 0 */
	t_log_reset();
	args[0] = 5;
	args[1] = gstr("VP");
	args[2] = gstr("%d");
	args[3] = 0;
	t_call(&C, n2shim_android, "__android_log_vprint", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -1);
	CHECK_STR(t_log.text, "(null va_list)");
}

/* ------------------------------------------------------------------------------------------------------------------
 * atrace / perf / sec_drv
 * ---------------------------------------------------------------------------------------------------------------- */

static void test_simple_returns(void)
{
	uint32_t args[4] = { 0, 0, 0, 0 };

	reset_cpu();
	t_log_reset();
	t_call(&C, n2shim_android, "atrace_get_enabled_tags", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(C.r[1], 0);             /* the 64-bit result is 0 in both halves */

	/* the three body calls are no-ops; the argument is never dereferenced */
	args[0] = gstr("a-name");
	args[1] = 1234;
	t_call(&C, n2shim_android, "atrace_begin_body", args, NULL, 0);
	t_call(&C, n2shim_android, "atrace_end_body", args, NULL, 0);
	t_call(&C, n2shim_android, "atrace_int_body", args, NULL, 0);
	CHECK_EQ(t_log.calls, 0);        /* and they do not log */

	args[0] = 0x10;
	args[1] = 500;
	t_call(&C, n2shim_android, "perf_lock_acq", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	args[0] = 0;
	t_call(&C, n2shim_android, "perf_lock_rel", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);

	args[0] = 1;
	t_call(&C, n2shim_android, "sec_drv_base_check", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	args[0] = 1;
	t_call(&C, n2shim_android, "sec_drv_info_update", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);

	args[0] = 0x7000;
	args[1] = 0;
	t_call(&C, n2shim_android, "__gnu_Unwind_Find_exidx", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	t_call(&C, n2shim_android, "__aeabi_unwind_cpp_pr0", args, NULL, 0);
}

/* ------------------------------------------------------------------------------------------------------------------
 * log / exp2, soft-float ABI
 * ---------------------------------------------------------------------------------------------------------------- */

static uint64_t bits_of(double d)
{
	uint64_t u;

	memcpy(&u, &d, sizeof u);
	return u;
}

static double d_of_bits(uint64_t u)
{
	double d;

	memcpy(&d, &u, sizeof d);
	return d;
}

static double call_math(const char *name, double x)
{
	uint64_t u = bits_of(x);
	uint32_t args[4];

	args[0] = (uint32_t)u;
	args[1] = (uint32_t)(u >> 32);
	args[2] = 0xdeadbeef;            /* must not be touched: a double argument is r0:r1 only */
	args[3] = 0xdeadbeef;
	t_call(&C, n2shim_android, name, args, NULL, 0);
	return d_of_bits((uint64_t)C.r[0] | ((uint64_t)C.r[1] << 32));
}

static void test_math(void)
{
	reset_cpu();
	/* exact values, from the IEEE-754 definitions */
	CHECK_EQ(bits_of(call_math("log", 1.0)), bits_of(0.0));
	CHECK_EQ(bits_of(call_math("exp2", 10.0)), bits_of(1024.0));
	CHECK_EQ(bits_of(call_math("exp2", 0.0)), bits_of(1.0));
	CHECK_EQ(bits_of(call_math("exp2", -1.0)), bits_of(0.5));
	CHECK_EQ(bits_of(call_math("exp2", -1074.0)), bits_of(0x1p-1074));   /* smallest subnormal */
	/* log(2) = 0.693147180559945309417232121458176568 -- the host's correctly-rounded double */
	CHECK_EQ(bits_of(call_math("log", 2.0)), bits_of(0.69314718055994530942));
	CHECK_EQ(bits_of(call_math("log", 8.0)), bits_of(3.0 * 0.69314718055994530942));
	/* special values */
	CHECK(isinf(call_math("log", 0.0)) && call_math("log", 0.0) < 0.0);
	CHECK(isnan(call_math("log", -1.0)));
	CHECK(isinf(call_math("exp2", 2000.0)) && call_math("exp2", 2000.0) > 0.0);
	CHECK_EQ(bits_of(call_math("exp2", -2000.0)), bits_of(0.0));
	/* the argument is not read from d0: put garbage there and require the r0:r1 value to win */
	{
		double r;

		C.d[0].u64 = bits_of(12345.0);
		r = call_math("log", 1.0);
		CHECK_EQ(bits_of(r), bits_of(0.0));
	}
}

/* ------------------------------------------------------------------------------------------------------------------
 * dlopen / dlsym / dlclose / dlerror / dladdr (handle and error logic; t_core tests the real loader)
 * ---------------------------------------------------------------------------------------------------------------- */

static struct n2elf_set *fake_set;   /* never dereferenced by the fakes; non-NULL says "a set exists" */

static void test_dl(void)
{
	uint32_t args[4];
	char buf[200];
	uint32_t gerr;

	reset_cpu();
	n2shim_set = fake_set = (struct n2elf_set *)(uintptr_t)0x1;   /* opaque to the fakes */
	(void)fake_set;

	/* dlopen(NULL) -> the global handle */
	args[0] = 0;
	t_call(&C, n2shim_android, "dlopen", args, NULL, 0);
	CHECK_EQ(C.r[0], 1);

	/* an object of the set whose probe symbol resolves -> a per-object handle */
	{
		uint32_t h;

		args[0] = gstr("libvcodec_utility.so");
		t_call(&C, n2shim_android, "dlopen", args, NULL, 0);
		h = C.r[0];
		CHECK_EQ(h, 0x180);          /* the representative-symbol probe range */
		/* dlsym through it finds the symbol the fake knows */
		args[0] = h;
		args[1] = gstr("found_symbol");
		t_call(&C, n2shim_android, "dlsym", args, NULL, 0);
		CHECK_EQ(C.r[0], FAKE_TEXT_SYM);
		/* dlclose of that handle is a success and unloads nothing */
		args[0] = h;
		t_call(&C, n2shim_android, "dlclose", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
	}

	/* the three names the vendor really asks for and that are NOT in the set -> NULL + a guest dlerror */
	{
		static const char *const absent[] = {
			"AVCSecureVdecCA.so", "HEVCSecureVdecCA.so", "/vendor/lib/libmtcloader.so", NULL,
		};
		int i;

		for (i = 0; absent[i] != NULL; i++) {
			args[0] = gstr(absent[i]);
			t_call(&C, n2shim_android, "dlopen", args, NULL, 0);
			CHECK_EQ(C.r[0], 0);
			t_call(&C, n2shim_android, "dlerror", args, NULL, 0);
			gerr = C.r[0];
			CHECK(gerr != 0);
			CHECK(strstr(gread(gerr, buf, sizeof buf), absent[i]) != NULL);
			/* dlerror clears itself */
			t_call(&C, n2shim_android, "dlerror", args, NULL, 0);
			CHECK_EQ(C.r[0], 0);
		}
	}

	/* the integrator registry wins over the probe table, and a full path is reduced to its basename */
	CHECK_EQ(n2shim_dl_note_loaded("/vendor/lib/libother.so"), 0);
	args[0] = gstr("/vendor/lib/libother.so");
	t_call(&C, n2shim_android, "dlopen", args, NULL, 0);
	CHECK_EQ(C.r[0], 0x100);         /* the registry range, first entry */
	args[0] = gstr("libother.so");
	t_call(&C, n2shim_android, "dlopen", args, NULL, 0);
	CHECK_EQ(C.r[0], 0x100);         /* basename comparison */

	/* dlsym failures set the error and return 0 */
	args[0] = 1;
	args[1] = gstr("no_such_symbol");
	t_call(&C, n2shim_android, "dlsym", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	t_call(&C, n2shim_android, "dlerror", args, NULL, 0);
	CHECK(C.r[0] != 0);
	CHECK(strstr(gread(C.r[0], buf, sizeof buf), "no_such_symbol") != NULL);

	/* an invalid handle is rejected before the lookup: 0, a registry slot past the last registered object */
	args[0] = 0;
	args[1] = gstr("found_symbol");
	t_call(&C, n2shim_android, "dlsym", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	t_call(&C, n2shim_android, "dlerror", args, NULL, 0);
	CHECK(C.r[0] != 0);
	args[0] = 0x105;
	args[1] = gstr("found_symbol");
	t_call(&C, n2shim_android, "dlsym", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	t_call(&C, n2shim_android, "dlerror", args, NULL, 0);
	CHECK(C.r[0] != 0);
	/* the global handle and a probed handle are both accepted */
	{
		uint32_t ok[4];

		ok[0] = 1;
		ok[1] = gstr("found_symbol");
		t_call(&C, n2shim_android, "dlsym", ok, NULL, 0);
		CHECK_EQ(C.r[0], FAKE_TEXT_SYM);
		ok[0] = 0x180;
		t_call(&C, n2shim_android, "dlsym", ok, NULL, 0);
		CHECK_EQ(C.r[0], FAKE_TEXT_SYM);
	}

	/* dladdr of a symbol: fname/sname are GUEST pointers with the parsed text, saddr = addr - offset */
	{
		uint32_t info = gbuf(16);

		args[0] = 0x9104;
		args[1] = info;
		t_call(&C, n2shim_android, "dladdr", args, NULL, 0);
		CHECK_EQ(C.r[0], 1);
		CHECK_STR(gread(r32(info + 0), buf, sizeof buf), "libtest.so");
		CHECK_EQ(r32(info + 4), 0);
		CHECK_STR(gread(r32(info + 8), buf, sizeof buf), "found_symbol");
		CHECK_EQ(r32(info + 12), 0x9100);
	}

	/* dladdr of an address inside an object but outside any symbol: fname only, sname NULL */
	{
		uint32_t info = gbuf(16);

		args[0] = 0x1234;
		args[1] = info;
		t_call(&C, n2shim_android, "dladdr", args, NULL, 0);
		CHECK_EQ(C.r[0], 1);
		CHECK_STR(gread(r32(info + 0), buf, sizeof buf), "libtest.so");
		CHECK_EQ(r32(info + 8), 0);
	}

	/* an address in no object (describe gives "0x...") -> 0 and no writes */
	{
		uint32_t info = gbuf(16);

		w32(info + 0, 0xaaaaaaaa);
		args[0] = 0xfeed0000;
		args[1] = info;
		t_call(&C, n2shim_android, "dladdr", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		CHECK_EQ(r32(info + 0), 0xaaaaaaaau);
	}

	/* a NULL Dl_info is rejected */
	args[0] = 0x9104;
	args[1] = 0;
	t_call(&C, n2shim_android, "dladdr", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);

	/* without a set every dl* answer is the failure answer */
	n2shim_set = NULL;
	args[0] = gstr("libvcodec_utility.so");
	t_call(&C, n2shim_android, "dlopen", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	args[0] = 1;
	args[1] = gstr("found_symbol");
	t_call(&C, n2shim_android, "dlsym", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	args[0] = 0x9104;
	args[1] = gbuf(16);
	t_call(&C, n2shim_android, "dladdr", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
}

/* ------------------------------------------------------------------------------------------------------------------
 * ion
 * ---------------------------------------------------------------------------------------------------------------- */

static void test_ion(void)
{
	struct fakehw fh;
	struct n2vc_hw hw;
	uint32_t args[4];
	uint32_t stack[3];
	uint32_t fd, handle, share, ga, h2, info;

	memset(&fh, 0, sizeof fh);
	fh.next_handle = 1;
	fh.next_pa = FAKE_PA0;
	hw.ctx = &fh;
	hw.power = f_power;
	hw.regs = f_regs;
	hw.wait_irq = f_wait_irq;
	hw.irq_clear = f_irq_clear;
	hw.dma_alloc = f_dma_alloc;
	hw.dma_free = f_dma_free;
	hw.dma_rw = f_dma_rw;
	hw.close = NULL;

	reset_cpu();
	n2shim_ion_reset();
	n2shim_hw = &hw;

	/* mt_ion_open("vpud") -> a fake fd, and only four of them */
	args[0] = gstr("vpud");
	t_call(&C, n2shim_android, "mt_ion_open", args, NULL, 0);
	fd = C.r[0];
	CHECK_EQ(fd, 0x700);
	args[0] = gstr("vpud");
	t_call(&C, n2shim_android, "mt_ion_open", args, NULL, 0);
	CHECK_EQ(C.r[0], 0x701);
	args[0] = 0x701;
	t_call(&C, n2shim_android, "mt_ion_close", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	args[0] = 0x999;
	t_call(&C, n2shim_android, "mt_ion_close", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -EBADF);

	/* ion_alloc_camera(fd, 4096, 4096, flags=1, &handle): five arguments, the last on the stack */
	{
		uint32_t gh = gbuf(4);

		args[0] = fd;
		args[1] = 4096;
		args[2] = 4096;
		args[3] = 1;
		stack[0] = gh;
		t_call(&C, n2shim_android, "ion_alloc_camera", args, stack, 1);
		CHECK_EQ(C.r[0], 0);
		handle = r32(gh);
		CHECK_EQ(handle, 1);
		CHECK_EQ(fh.nalloc, 1);
		CHECK_EQ(fh.last_bytes, 4096);
		/* ION flags (here ION_FLAG_CACHED = 1) are never forwarded: RWVC_DMA_ALLOC accepts only RWVC_DMA_SHARED and
		 * 1 would alias it (phone run vc10: forwarded ION flags made the kernel return EINVAL). */
		CHECK_EQ(fh.last_flags, 0);
	}

	/* ion_alloc(fd, len, align, heap_mask, flags, &handle): six arguments, two on the stack */
	{
		uint32_t gh = gbuf(4);

		args[0] = fd;
		args[1] = 8192;
		args[2] = 0;
		args[3] = 0x40000;   /* heap mask, as the secure path passes */
		stack[0] = 0;        /* flags */
		stack[1] = gh;
		t_call(&C, n2shim_android, "ion_alloc", args, stack, 2);
		CHECK_EQ(C.r[0], 0);
		CHECK_EQ(r32(gh), 2);
		CHECK_EQ(fh.nalloc, 2);
		CHECK_EQ(fh.last_bytes, 8192);
		CHECK_EQ(fh.last_flags, 0);
	}

	/* ion_share(fd, handle, &share) */
	{
		uint32_t gs = gbuf(4);

		args[0] = fd;
		args[1] = handle;
		args[2] = gs;
		t_call(&C, n2shim_android, "ion_share", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		share = r32(gs);
		CHECK_EQ(share, 0x400);
	}

	/* ion_import(fd, share, &handle) resolves the same dma handle */
	{
		uint32_t gh = gbuf(4);

		args[0] = fd;
		args[1] = share;
		args[2] = gh;
		t_call(&C, n2shim_android, "ion_import", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		CHECK_EQ(r32(gh), handle);
	}

	/* ion_mmap(fd, 0, len, prot=3, flags=1, share, offset=0): seven arguments, three on the stack */
	args[0] = fd;
	args[1] = 0;
	args[2] = 4096;
	args[3] = 3;
	stack[0] = 1;
	stack[1] = share;
	stack[2] = 0;
	t_call(&C, n2shim_android, "ion_mmap", args, stack, 3);
	ga = C.r[0];
	CHECK(ga != 0);
	if (ga != 0) {
		n2a32_st32(&C, ga, 0xcafef00du);
		CHECK_EQ(r32(ga), 0xcafef00du);
		args[0] = fd;
		args[1] = ga;
		args[2] = 4096;
		t_call(&C, n2shim_android, "ion_munmap", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
	}

	/* a length above the allocation is refused (0 = MAP_FAILED for the vendor's checks) */
	args[0] = fd;
	args[1] = 0;
	args[2] = 8192;
	args[3] = 3;
	stack[0] = 1;
	stack[1] = share;
	stack[2] = 0;
	t_call(&C, n2shim_android, "ion_mmap", args, stack, 3);
	CHECK_EQ(C.r[0], 0);

	/* ion_custom_ioctl, MULTIMEDIA / ION_MM_GET_IOVA: the dma pa lands at struct offset 32 */
	info = gbuf(0x60);
	w32(info, 8);                 /* mm_cmd = ION_MM_GET_IOVA */
	w32(info + 8, handle);        /* get_phys_param.handle */
	w32(info + 12, 7);            /* module_id, echoed by the vendor */
	args[0] = fd;
	args[1] = 1;                  /* ION_CMD_MULTIMEDIA */
	args[2] = info;
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(r32(info + 32), FAKE_PA0);
	CHECK_EQ(r32(info + 36), 0);

	/* ION_MM_CONFIG_BUFFER: accepted, nothing written */
	w32(info, 0);
	w32(info + 32, 0x12345678);
	args[1] = 1;
	args[2] = info;
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(r32(info + 32), 0x12345678u);

	/* SYSTEM / ION_SYS_GET_PHYS: pa at offset 12, len at 16 */
	w32(info, 1);
	w32(info + 8, handle);
	args[1] = 0;
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(r32(info + 12), FAKE_PA0);
	CHECK_EQ(r32(info + 16), 4096);

	/* SYSTEM / ION_SYS_CACHE_SYNC and a secure CONFIG_BUFFER are accepted without output */
	w32(info, 0);
	args[1] = 0;
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	w32(info, 0);
	w32(info + 8, handle);
	w32(info + 12, 1);            /* module */
	w32(info + 16, 1);            /* security */
	w32(info + 20, 1);            /* coherent */
	args[1] = 2;                  /* ION_CMD_MULTIMEDIA_SEC */
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);

	/* an unknown command family and a NULL data pointer fail with -EINVAL */
	args[1] = 9;
	args[2] = info;
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -EINVAL);
	args[1] = 0;
	args[2] = 0;
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -EINVAL);

	/* an unknown handle in a custom ioctl fails instead of writing a bogus address */
	w32(info, 8);
	w32(info + 8, 0x1234);
	args[1] = 1;
	args[2] = info;
	t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -EINVAL);

	/* ion_share_close ends the mapping path */
	args[0] = fd;
	args[1] = share;
	t_call(&C, n2shim_android, "ion_share_close", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	args[0] = fd;
	args[1] = share;
	args[2] = gbuf(4);
	t_call(&C, n2shim_android, "ion_import", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -EINVAL);

	/* ion_free frees the dma handle once */
	args[0] = fd;
	args[1] = handle;
	t_call(&C, n2shim_android, "ion_free", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(fh.nfree, 1);
	args[0] = fd;
	args[1] = handle;
	t_call(&C, n2shim_android, "ion_free", args, NULL, 0);
	CHECK_EQ((int32_t)C.r[0], -EINVAL);
	CHECK_EQ(fh.nfree, 1);

	/* h2: the second allocation is still there and can be freed */
	args[0] = fd;
	args[1] = 2;
	t_call(&C, n2shim_android, "ion_free", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(fh.nfree, 2);

	/* no hardware back end: every allocation fails with -ENODEV and nothing is freed */
	n2shim_hw = NULL;
	{
		uint32_t gh = gbuf(4);

		args[0] = fd;
		args[1] = 4096;
		args[2] = 4096;
		args[3] = 0;
		stack[0] = gh;
		t_call(&C, n2shim_android, "ion_alloc_camera", args, stack, 1);
		CHECK_EQ((int32_t)C.r[0], -ENODEV);
		CHECK_EQ(fh.nalloc, 2);
	}
	{
		uint32_t gh = gbuf(4);

		args[0] = fd;
		args[1] = 16;
		args[2] = 0;
		args[3] = 0;
		stack[0] = 0;
		stack[1] = gh;
		t_call(&C, n2shim_android, "ion_alloc", args, stack, 2);
		CHECK_EQ((int32_t)C.r[0], -ENODEV);
	}

	/* the integrator hook makes a foreign share fd importable */
	n2shim_hw = &hw;
	n2shim_ion_reset();
	CHECK_EQ(n2shim_ion_register_share(0x555, 0x77, FAKE_PA0 + 0x200000, 0x1000), 0);
	args[0] = fd;
	args[1] = 0x555;
	args[2] = gbuf(4);
	t_call(&C, n2shim_android, "ion_import", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	h2 = r32(args[2]);
	CHECK_EQ(h2, 0x77);
	CHECK_EQ(n2shim_ion_register_share(0x555, 0x78, 0, 0x1000), -EINVAL);   /* duplicate id */
	CHECK_EQ(n2shim_ion_register_share(0, 0x78, 0, 0x1000), -EINVAL);

	/* An imported (foreign) buffer belongs to the AP: the importer's ion_share_close() and ion_free() must
	 * neither destroy the share nor free the DMA handle, so the same share id imports again for the next job.
	 * Board: IV -> V34, 11 November (the second encode failed with "[ERROR] ion_import failed" without this). */
	{
		uint32_t nfree_before = (uint32_t)fh.nfree;
		uint32_t gh = gbuf(4);
		uint32_t info2 = gbuf(0x60);

		args[0] = fd;
		args[1] = 0x555;
		t_call(&C, n2shim_android, "ion_share_close", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);

		args[0] = fd;
		args[1] = 0x555;
		args[2] = gh;
		t_call(&C, n2shim_android, "ion_import", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);                      /* still importable after the share close */
		CHECK_EQ(r32(gh), 0x77);

		args[0] = fd;
		args[1] = 0x77;
		t_call(&C, n2shim_android, "ion_free", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);                      /* a no-op, not -EINVAL */
		CHECK_EQ(fh.nfree, nfree_before);         /* the AP's dma handle is not freed */

		/* the address query still answers after both calls */
		w32(info2, 8);                            /* mm_cmd = ION_MM_GET_IOVA */
		w32(info2 + 8, 0x77);
		args[0] = fd;
		args[1] = 1;
		args[2] = info2;
		t_call(&C, n2shim_android, "ion_custom_ioctl", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		CHECK_EQ(r32(info2 + 32), FAKE_PA0 + 0x200000);

		/* and the next job imports the same share id one more time */
		args[0] = fd;
		args[1] = 0x555;
		args[2] = gh;
		t_call(&C, n2shim_android, "ion_import", args, NULL, 0);
		CHECK_EQ(C.r[0], 0);
		CHECK_EQ(r32(gh), 0x77);
	}
}

/* ------------------------------------------------------------------------------------------------------------------
 * __libc_init, __cxa_atexit / __cxa_finalize
 * ---------------------------------------------------------------------------------------------------------------- */

static void test_libc_init(void)
{
	uint32_t args[4];

	/* A: the runner's globals win; main sees argc/argv/envp and its return value is recorded */
	reset_cpu();
	n2shim_libc_argc = 3;
	n2shim_libc_argv = 0x40000000u;   /* main_args only tests for NULL, it never dereferences argv */
	n2shim_libc_envp = 0x40000040u;
	n2shim_libc_status = -1;
	t_log_reset();
	args[0] = 0;
	args[1] = 0;
	args[2] = t_guest_fn(M, T_OFF_main_args);
	args[3] = 0;
	t_call(&C, n2shim_android, "__libc_init", args, NULL, 0);
	CHECK_EQ(C.r[0], 3u + 0x100u + 0x10000u);
	CHECK_EQ(n2shim_libc_status, 3 + 0x100 + 0x10000);
	CHECK_EQ(C.stop_request, 1u);

	/* B: no runner globals -> argv/envp are parsed from raw_args exactly like bionic */
	{
		uint32_t raw = T_DATA_ADDR + 0x7000u;

		reset_cpu();
		n2shim_libc_argc = 0;
		n2shim_libc_argv = 0;
		n2shim_libc_envp = 0;
		n2shim_libc_status = -1;
		w32(raw + 0, 2);              /* argc */
		w32(raw + 4, 0x11110000u);    /* argv[0] */
		w32(raw + 8, 0x22220000u);    /* argv[1] */
		w32(raw + 12, 0);             /* argv[2] = NULL */
		w32(raw + 16, 0x33330000u);   /* envp[0] */
		w32(raw + 20, 0);             /* envp[1] = NULL */
		args[0] = raw;
		args[1] = 0;
		args[2] = t_guest_fn(M, T_OFF_main_args);
		args[3] = 0;
		t_call(&C, n2shim_android, "__libc_init", args, NULL, 0);
		CHECK_EQ(C.r[0], 2u + 0x100u + 0x10000u);
		CHECK_EQ(n2shim_libc_status, 2 + 0x100 + 0x10000);
	}

	/* C: onexit runs before main and reaches guest code */
	reset_cpu();
	n2a32_st8(&C, T_HEAP_ADDR + 0x100, 0);
	n2shim_libc_argc = 0;
	n2shim_libc_argv = 0;
	n2shim_libc_envp = 0;
	n2shim_libc_status = -1;
	args[0] = 0;
	args[1] = t_guest_fn(M, T_OFF_onexit_marker);
	args[2] = t_guest_fn(M, T_OFF_main_const);
	args[3] = 0;
	t_call(&C, n2shim_android, "__libc_init", args, NULL, 0);
	CHECK_EQ(C.r[0], 42);
	CHECK_EQ(n2shim_libc_status, 42);
	CHECK_EQ(n2a32_ld8(&C, T_HEAP_ADDR + 0x100), 0xed);

#ifndef T_V34_NO_FAULT
	/* D: a main that faults leaves the fault visible and does not claim a clean exit.
	 * Skipped in the TSan build: V02's SIGSEGV-to-stop recovery uses siglongjmp out of a signal handler, which TSan
	 * cannot follow ("can't find longjmp buf"); that is a property of the sanitizer, not of this package. */
	reset_cpu();
	n2shim_libc_argc = 0;
	n2shim_libc_argv = 0;
	n2shim_libc_envp = 0;
	n2shim_libc_status = -1;
	args[0] = 0;
	args[1] = 0;
	args[2] = 0xdead0001u;      /* inside the 4 GiB reservation, unmapped */
	args[3] = 0;
	t_call(&C, n2shim_android, "__libc_init", args, NULL, 0);
	CHECK_EQ((int32_t)n2shim_libc_status, -1);
	CHECK_EQ(C.stop_request, 0u);
	CHECK_EQ(C.stop, N2A32_STOP_FAULT);
#endif

	/* E: a NULL main is refused without touching the CPU */
	reset_cpu();
	args[0] = 0;
	args[1] = 0;
	args[2] = 0;
	args[3] = 0;
	t_call(&C, n2shim_android, "__libc_init", args, NULL, 0);
	CHECK_EQ((int32_t)n2shim_libc_status, -1);
	CHECK_EQ(C.stop_request, 0u);
}

static void test_atexit(void)
{
	uint32_t args[4];
	uint32_t gcounter;
	int i;

	reset_cpu();
	gcounter = gbuf(4);
	n2shim_libc_argc = 0;
	n2shim_libc_argv = 0;
	n2shim_libc_envp = 0;
	w32(gcounter, 0);

	/* two handlers, one with a dso handle; a 33-entry overflow is refused */
	args[0] = t_guest_fn(M, T_OFF_atexit_inc);
	args[1] = gcounter;
	args[2] = 0;
	t_call(&C, n2shim_android, "__cxa_atexit", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	args[2] = 0x1234;
	t_call(&C, n2shim_android, "__cxa_atexit", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);

	/* a finalize for a different dso runs nothing */
	args[0] = 0x9999;
	t_call(&C, n2shim_android, "__cxa_finalize", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(r32(gcounter), 0);

	/* __cxa_finalize(NULL) runs every remaining handler, newest first */
	args[0] = 0;
	t_call(&C, n2shim_android, "__cxa_finalize", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
	CHECK_EQ(r32(gcounter), 2);
	/* and only once */
	args[0] = 0;
	t_call(&C, n2shim_android, "__cxa_finalize", args, NULL, 0);
	CHECK_EQ(r32(gcounter), 2);

	/* the table holds 32 handlers; the 33rd registration is refused */
	for (i = 0; i < 33; i++) {
		args[0] = t_guest_fn(M, T_OFF_atexit_inc);
		args[1] = gcounter;
		args[2] = 0;
		t_call(&C, n2shim_android, "__cxa_atexit", args, NULL, 0);
		if (i < 32)
			CHECK_EQ(C.r[0], 0);
		else
			CHECK_EQ((int32_t)C.r[0], -ENOMEM);
	}
	args[0] = 0;
	t_call(&C, n2shim_android, "__cxa_finalize", args, NULL, 0);
	CHECK_EQ(r32(gcounter), 2 + 32);

	/* __register_atfork is accepted and dropped */
	args[0] = t_guest_fn(M, T_OFF_main_const);
	args[1] = t_guest_fn(M, T_OFF_main_const);
	args[2] = t_guest_fn(M, T_OFF_main_const);
	args[3] = 0;
	t_call(&C, n2shim_android, "__register_atfork", args, NULL, 0);
	CHECK_EQ(C.r[0], 0);
}

/* ------------------------------------------------------------------------------------------------------------------
 * Guest threads: four host threads with one n2a32_cpu each (the real V32 model) hammering the locked tables
 * ---------------------------------------------------------------------------------------------------------------- */

struct thr_work {
	struct n2a32_cpu c;
	uint32_t key, val, def;
	int iters;
	int fails;
};

static void *thr_property(void *p)
{
	struct thr_work *w = p;
	int i;

	for (i = 0; i < w->iters; i++) {
		uint32_t args[4];

		args[0] = w->key;
		args[1] = w->val;
		args[2] = w->def;
		args[3] = 0;
		t_call(&w->c, n2shim_android, "property_get", args, NULL, 0);
		if (w->c.r[0] != 1)
			w->fails++;
	}
	return NULL;
}

static void *thr_ion(void *p)
{
	struct thr_work *w = p;
	int i;

	for (i = 0; i < w->iters; i++) {
		uint32_t gh = w->val;
		uint32_t args[4];

		args[0] = w->key;          /* the fake fd */
		args[1] = 64;
		args[2] = 64;
		args[3] = 0;
		{
			uint32_t stack[1];

			stack[0] = gh;
			t_call(&w->c, n2shim_android, "ion_alloc_camera", args, stack, 1);
		}
		if (w->c.r[0] != 0) {
			w->fails++;
			continue;
		}
		{
			uint32_t a2[4];

			a2[0] = w->key;
			a2[1] = n2a32_ld32(&w->c, gh);
			a2[2] = 0;
			a2[3] = 0;
			t_call(&w->c, n2shim_android, "ion_free", a2, NULL, 0);
			if (w->c.r[0] != 0)
				w->fails++;
		}
	}
	return NULL;
}

/* The fake hardware counters are shared by all four threads and are the only lock-free state here. */
static int f_dma_alloc_atomic(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct fakehw *f = ctx;
	uint32_t n;

	if (bytes == 0 || handle == NULL || pa == NULL)
		return -EINVAL;
	(void)__atomic_fetch_add(&f->nalloc, 1, __ATOMIC_RELAXED);
	n = __atomic_fetch_add(&f->next_handle, 1, __ATOMIC_RELAXED) + 1;
	(void)flags;
	*handle = n;
	*pa = FAKE_PA0 + n * 0x100000u;
	return 0;
}

static int f_dma_free_atomic(void *ctx, uint32_t handle)
{
	struct fakehw *f = ctx;

	(void)handle;
	(void)__atomic_fetch_add(&f->nfree, 1, __ATOMIC_RELAXED);
	return 0;
}

static void test_threads(void)
{
	enum { NTHR = 4, ITERS = 40 };
	struct thr_work w[NTHR];
	pthread_t t[NTHR];
	struct fakehw fh;
	struct n2vc_hw hw;
	int i;

	reset_cpu();
	n2shim_props_forget_unknown();
	/* no log sink while four threads run: the recorder is not thread-safe and a log line is not what is tested */
	n2shim_log = NULL;

	/* four distinct unknown keys: each is reported exactly once even though four threads hit their own */
	for (i = 0; i < NTHR; i++) {
		char key[64];

		memset(&w[i], 0, sizeof w[i]);
		n2a32_cpu_init(&w[i].c, M);
		w[i].c.r[13] = T_STACK_ADDR + T_STACK_LEN - 0x100 - (uint32_t)i * 0x400;
		snprintf(key, sizeof key, "vendor.n2.thread.%d", i);
		w[i].key = gstr(key);
		w[i].val = gbuf(128);
		w[i].def = gstr("z");
		w[i].iters = ITERS;
	}
	for (i = 0; i < NTHR; i++)
		CHECK_EQ(pthread_create(&t[i], NULL, thr_property, &w[i]), 0);
	for (i = 0; i < NTHR; i++)
		CHECK_EQ(pthread_join(t[i], NULL), 0);
	for (i = 0; i < NTHR; i++) {
		CHECK_EQ(w[i].fails, 0);
		CHECK_EQ(n2a32_ld8(&w[i].c, w[i].val), 'z');
	}
	CHECK_EQ(n2shim_props_unknown_count(), (unsigned)NTHR);

	/* concurrent ion allocate/free through the one locked table */
	memset(&fh, 0, sizeof fh);
	hw.ctx = &fh;
	hw.power = f_power;
	hw.regs = f_regs;
	hw.wait_irq = f_wait_irq;
	hw.irq_clear = f_irq_clear;
	hw.dma_alloc = f_dma_alloc_atomic;
	hw.dma_free = f_dma_free_atomic;
	hw.dma_rw = f_dma_rw;
	hw.close = NULL;
	n2shim_hw = &hw;
	n2shim_ion_reset();
	for (i = 0; i < NTHR; i++) {
		uint32_t fdargs[4];

		fdargs[0] = gstr("vpud");
		t_call(&w[i].c, n2shim_android, "mt_ion_open", fdargs, NULL, 0);
		w[i].key = w[i].c.r[0];
		w[i].val = gbuf(4);
		w[i].iters = ITERS;
	}
	for (i = 0; i < NTHR; i++)
		CHECK_EQ(pthread_create(&t[i], NULL, thr_ion, &w[i]), 0);
	for (i = 0; i < NTHR; i++)
		CHECK_EQ(pthread_join(t[i], NULL), 0);
	for (i = 0; i < NTHR; i++)
		CHECK_EQ(w[i].fails, 0);
	CHECK_EQ(fh.nalloc, NTHR * ITERS);
	CHECK_EQ(fh.nfree, NTHR * ITERS);
	n2shim_hw = NULL;
	n2shim_log = t_log_sink;
}

/* ------------------------------------------------------------------------------------------------------------------
 * Table completeness
 * ---------------------------------------------------------------------------------------------------------------- */

static void test_tables(void)
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
	unsigned n = 0, i, j;

	/* exactly the 35 imports of D01's android table, no duplicates, terminator present */
	while (n2shim_android[n].name != NULL) {
		CHECK(n2shim_android[n].fn != NULL);
		for (i = 0; i < n; i++)
			CHECK(strcmp(n2shim_android[i].name, n2shim_android[n].name) != 0);
		n++;
	}
	CHECK_EQ(n, 35);
	for (i = 0; imports[i] != NULL; i++)
		CHECK(t_find(n2shim_android, imports[i]) != NULL);
	CHECK_EQ(i, n);
	CHECK(n2shim_android[n].fn == NULL);

	/* every name is a plausible C symbol and no longer than the loader's tables accept */
	for (i = 0; i < n; i++) {
		const char *s = n2shim_android[i].name;

		CHECK(s[0] != '\0' && strlen(s) < 64);
		for (j = 0; s[j] != '\0'; j++)
			CHECK(s[j] == '_' || (s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') ||
			      (s[j] >= '0' && s[j] <= '9'));
	}
}

/* ------------------------------------------------------------------------------------------------------------------ */

int main(int argc, char **argv)
{
	n2shim_log = t_log_sink;
	M = t_mem_new();
	reset_cpu();

	test_tables();
	test_data_table();
	test_properties();
	if (argc > 1)
		test_property_keys(argv[1]);
	test_log();
	test_simple_returns();
	test_math();
	test_dl();
	test_ion();
	test_libc_init();
	test_atexit();
	test_threads();

	printf("T34RESULT %d/%d\n", t_checks - t_fails, t_checks);
	if (t_fails != 0)
		return 1;
	printf("N2TEST V34 PASS %d/%d\n", t_checks, t_checks);
	return 0;
}
