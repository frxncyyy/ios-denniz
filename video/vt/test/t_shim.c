/* SPDX-License-Identifier: MIT */
/*
 * t_shim.c -- host tests of vt/n2vt_shim.c and vt/n2vt_log.c.
 *
 * The shim is compiled with -DN2_HOST_FAKE and with its libSystem/VideoToolbox calls renamed to the doubles below
 * (-Daccess=t_access -Ddlopen=t_dlopen -Ddlsym=t_dlsym -Dopen=t_open -Dwrite=t_write -Dclose=t_close -Dgetpid=t_getpid
 * -Dgetprogname=t_getprogname -DVTRegisterVideoEncoderWithInfo=t_vt_register). Everything the doubles see is checked against
 * the literal values the phone must see: Darwin's RTLD_NOW|RTLD_LOCAL = 2|4, O_WRONLY|O_NONBLOCK|O_NOCTTY = 1|4|0x20000,
 * the VCPHEVC image path, the factory's name, the four codec types, the two implementation IDs and the IsHardwareAccelerated
 * key. The CoreFoundation subset the shim uses (one dictionary of strings/booleans, one interned-string constructor) is
 * implemented here rather than linked from fake/: this suite stays meaningful whether or not WP02 has delivered, and it
 * counts dictionary allocations so that the shim's "no allocation kept" can be checked.
 *
 * Each scenario runs in a forked child, because SLIMEncoderRegister is once-per-process by design. The last line is
 * "N2RESULT t_shim <passed> <total>"; the exit status is 0 only if every scenario passed.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "n2vt_abi.h"
#include "n2vt_log.h"

/* the plug-in's two exports (n2vt_abi.h gives only the entry's name as a string) */
extern void     SLIMEncoderRegister(void);
extern unsigned n2vt_version(void);

#if defined(T_NO_AVC_FACTORY)
#define HAVE_AVC_FACTORY 0
#else
#define HAVE_AVC_FACTORY 1
#endif

/* ------------------------------------------------------------------------------------------------------------------------
 * The CoreFoundation subset of the shim's world.
 * --------------------------------------------------------------------------------------------------------------------- */
enum { K_STRING = 1, K_DICT = 2, K_BOOL = 3 };

struct t_object { int kind; };
struct t_string { struct t_object header; const char *text; };
struct t_bool   { struct t_object header; int value; };
struct t_dict {
	struct t_object header;
	int count;
	struct { const void *key, *value; } item[8];
};

static struct t_string t_strings[24];
static int             t_string_count;
static struct t_bool   t_bool_true  = { { K_BOOL }, 1 };
static struct t_bool   t_bool_false = { { K_BOOL }, 0 };
static struct t_dict   t_dicts[24];
static int             t_dict_count;
static int             t_live_dicts;

const CFAllocatorRef kCFAllocatorDefault = NULL;
const CFBooleanRef   kCFBooleanTrue  = (CFBooleanRef)&t_bool_true;
const CFBooleanRef   kCFBooleanFalse = (CFBooleanRef)&t_bool_false;
static struct t_string t_hw_key = { { K_STRING }, "IsHardwareAccelerated" };
const CFStringRef kVTVideoEncoder_IsHardwareAccelerated = (CFStringRef)&t_hw_key;
const CFDictionaryKeyCallBacks   kCFTypeDictionaryKeyCallBacks   = { 0, NULL, NULL, NULL, NULL, NULL };
const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks = { 0, NULL, NULL, NULL, NULL };

/* Interned strings: N2STR() under -DN2_HOST_FAKE is n2fake_cfstr(). */
CFStringRef
n2fake_cfstr(const char *text)
{
	for (int i = 0; i < t_string_count; i++) {
		if (strcmp(t_strings[i].text, text) == 0) {
			return (CFStringRef)&t_strings[i];
		}
	}
	if (t_string_count >= (int)(sizeof t_strings / sizeof t_strings[0])) {
		abort();
	}
	t_strings[t_string_count].header.kind = K_STRING;
	t_strings[t_string_count].text = text;
	return (CFStringRef)&t_strings[t_string_count++];
}

CFMutableDictionaryRef
CFDictionaryCreateMutable(CFAllocatorRef allocator, CFIndex capacity, const CFDictionaryKeyCallBacks *keys,
                          const CFDictionaryValueCallBacks *values)
{
	(void)allocator;
	(void)capacity;
	(void)keys;
	(void)values;
	if (t_dict_count >= (int)(sizeof t_dicts / sizeof t_dicts[0])) {
		abort();
	}
	memset(&t_dicts[t_dict_count], 0, sizeof t_dicts[0]);
	t_dicts[t_dict_count].header.kind = K_DICT;
	t_live_dicts++;
	return (CFMutableDictionaryRef)&t_dicts[t_dict_count++];
}

static int
t_same_key(const void *a, const void *b)
{
	const struct t_object *x = (const struct t_object *)a;
	const struct t_object *y = (const struct t_object *)b;

	if (a == b) {
		return 1;
	}
	if (x == NULL || y == NULL || x->kind != y->kind) {
		return 0;
	}
	if (x->kind == K_STRING) {
		return strcmp(((const struct t_string *)a)->text, ((const struct t_string *)b)->text) == 0;
	}
	return 0;
}

void
CFDictionarySetValue(CFMutableDictionaryRef dict, const void *key, const void *value)
{
	struct t_dict *d = (struct t_dict *)dict;

	for (int i = 0; i < d->count; i++) {
		if (t_same_key(d->item[i].key, key)) {
			d->item[i].value = value;
			return;
		}
	}
	if (d->count >= (int)(sizeof d->item / sizeof d->item[0])) {
		abort();
	}
	d->item[d->count].key = key;
	d->item[d->count].value = value;
	d->count++;
}

const void *
CFDictionaryGetValue(CFDictionaryRef dict, const void *key)
{
	const struct t_dict *d = (const struct t_dict *)dict;

	for (int i = 0; i < d->count; i++) {
		if (t_same_key(d->item[i].key, key)) {
			return d->item[i].value;
		}
	}
	return NULL;
}

void
CFRelease(CFTypeRef object)
{
	const struct t_object *o = (const struct t_object *)object;

	if (o != NULL && o->kind == K_DICT) {
		t_live_dicts--;
	}
}

/* ------------------------------------------------------------------------------------------------------------------------
 * The renamed libSystem / VideoToolbox calls.
 * --------------------------------------------------------------------------------------------------------------------- */
struct t_registration {
	uint32_t                        codec;
	CFDictionaryRef                 info;
	n2_VTVideoEncoderCreateInstance create;
};

static struct t_registration t_regs[8];
static int                   t_reg_count;
static int                   t_reg_overflow;

static int   t_off, t_no_hevc, t_no_avc, t_no_jpeg, t_claim_hw, t_quiet;
static int   t_access_bad_path;
static int   t_hevc_image_present = 1;
static int   t_dlopen_calls;
static int   t_dlopen_bad_path;
static int   t_dlopen_flags;
static int   t_dlsym_calls;
static int   t_dlsym_bad_name;
static int t_hevc_token;
static void *const t_hevc_handle = &t_hevc_token;

static char  t_log[8192];
static size_t t_log_length;
static int   t_log_opens;
static int   t_log_closes;
static int   t_log_bad_fd;
static int   t_log_bad_path;
static int   t_open_bad_flags;

int
t_access(const char *path, int mode)
{
	if (mode != F_OK) {
		t_access_bad_path++;
	}
	if (strcmp(path, N2VT_SWITCH_OFF) == 0) {
		return t_off ? 0 : -1;
	}
	if (strcmp(path, N2VT_SWITCH_NO_HEVC) == 0) {
		return t_no_hevc ? 0 : -1;
	}
	if (strcmp(path, N2VT_SWITCH_NO_AVC) == 0) {
		return t_no_avc ? 0 : -1;
	}
	if (strcmp(path, N2VT_SWITCH_NO_JPEG) == 0) {
		return t_no_jpeg ? 0 : -1;
	}
	if (strcmp(path, N2VT_SWITCH_CLAIM_HW) == 0) {
		return t_claim_hw ? 0 : -1;
	}
	if (strcmp(path, N2VT_SWITCH_QUIET) == 0) {
		return t_quiet ? 0 : -1;
	}
	t_access_bad_path++;
	return -1;
}

void *
t_dlopen(const char *path, int flags)
{
	t_dlopen_calls++;
	t_dlopen_flags = flags;
	if (strcmp(path, "/System/Library/VideoCodecs/VCPHEVC.videocodec") != 0) {
		t_dlopen_bad_path++;
		return NULL;
	}
	return t_hevc_image_present ? t_hevc_handle : NULL;
}

static OSStatus
fake_hevc_factory(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	(void)codecType;
	(void)allocator;
	(void)instanceOut;
	return 0;
}

void *
t_dlsym(void *handle, const char *name)
{
	t_dlsym_calls++;
	if (handle != t_hevc_handle || strcmp(name, "HEVCVideoEncoder_CreateInstance") != 0) {
		t_dlsym_bad_name++;
		return NULL;
	}
	return (void *)&fake_hevc_factory;
}

#if !defined(T_NO_VT_REGISTER)
OSStatus
t_vt_register(uint32_t codecType, CFDictionaryRef info, n2_VTVideoEncoderCreateInstance create)
{
	if (t_reg_count >= (int)(sizeof t_regs / sizeof t_regs[0])) {
		t_reg_overflow++;
		return N2_kVTAllocationFailedErr;
	}
	t_regs[t_reg_count].codec = codecType;
	t_regs[t_reg_count].info = info;
	t_regs[t_reg_count].create = create;
	t_reg_count++;
	return 0;
}
#endif /* !T_NO_VT_REGISTER */

int
t_open(const char *path, int flags, ...)
{
	t_log_opens++;
	if (strcmp(path, "/dev/console") != 0) {
		t_log_bad_path++;
	}
	if (flags != 0x20005) {         /* O_WRONLY | O_NONBLOCK | O_NOCTTY, Darwin's numbers */
		t_open_bad_flags++;
	}
	return 42;
}

ssize_t
t_write(int fd, const void *buffer, size_t length)
{
	if (fd != 42) {
		t_log_bad_fd++;
		return -1;
	}
	if (t_log_length + length < sizeof t_log) {
		memcpy(t_log + t_log_length, buffer, length);
		t_log_length += length;
		t_log[t_log_length] = '\0';
	}
	return (ssize_t)length;
}

int
t_close(int fd)
{
	if (fd == 42) {
		t_log_closes++;
	}
	return 0;
}

pid_t
t_getpid(void)
{
	return 4242;
}

const char *
t_getprogname(void)
{
	return "t_shim";
}

/* ------------------------------------------------------------------------------------------------------------------------
 * The factories of the other packages. n2avc_vt_create is defined in this test unless T_NO_AVC_FACTORY says otherwise; the
 * JPEG factory is deliberately absent (the reserve package), so 'jpeg' must never be registered here.
 * --------------------------------------------------------------------------------------------------------------------- */
#if HAVE_AVC_FACTORY
/* the definition the shim's weak reference binds to; it must have external linkage, exactly like vt/n2avc_vt.c's */
OSStatus
n2avc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
{
	(void)codecType;
	(void)allocator;
	(void)instanceOut;
	return 0;
}
#else
extern OSStatus n2avc_vt_create(uint32_t, CFAllocatorRef, VTVideoEncoderRef *) __attribute__((weak));
#endif

/* ------------------------------------------------------------------------------------------------------------------------
 * State reset between scenarios (each scenario is its own process, so this only clears what the child inherited).
 * --------------------------------------------------------------------------------------------------------------------- */
static void
t_reset(void)
{
	t_reg_count = 0;
	t_reg_overflow = 0;
	t_off = t_no_hevc = t_no_avc = t_no_jpeg = t_claim_hw = t_quiet = 0;
	t_access_bad_path = 0;
	t_hevc_image_present = 1;
	t_dlopen_calls = 0;
	t_dlopen_bad_path = 0;
	t_dlopen_flags = 0;
	t_dlsym_calls = 0;
	t_dlsym_bad_name = 0;
	t_log_length = 0;
	t_log[0] = '\0';
	t_log_opens = t_log_closes = t_log_bad_fd = t_log_bad_path = 0;
	t_open_bad_flags = 0;
}

#define CHK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "    check failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 0; \
	} \
} while (0)

#if !defined(T_ONLY_THREADS)

/* The dictionary a registration carried, by index. */
static const struct t_dict *
t_info(int index)
{
	return (const struct t_dict *)(const void *)t_regs[index].info;
}

static const char *
t_id_of(int index)
{
	const struct t_dict *d = t_info(index);
	const struct t_string *id;

	if (d == NULL) {
		return NULL;
	}
	id = (const struct t_string *)CFDictionaryGetValue((CFDictionaryRef)d, N2VT_INFO_IMPLEMENTATION_ID);
	if (id == NULL || ((const struct t_object *)id)->kind != K_STRING) {
		return NULL;
	}
	return id->text;
}

static int
t_claims_hw(int index)
{
	const struct t_dict *d = t_info(index);
	const void *value;

	if (d == NULL) {
		return 0;
	}
	value = CFDictionaryGetValue((CFDictionaryRef)d, kVTVideoEncoder_IsHardwareAccelerated);
	return value == kCFBooleanTrue;
}

static int
t_has_hw_key(int index)
{
	const struct t_dict *d = t_info(index);

	return d != NULL && CFDictionaryGetValue((CFDictionaryRef)d, kVTVideoEncoder_IsHardwareAccelerated) != NULL;
}

/* ------------------------------------------------------------------------------------------------------------------------
 * Scenarios. Each returns 1 on success.
 * --------------------------------------------------------------------------------------------------------------------- */
static int
sc_all_factories(void)
{
	SLIMEncoderRegister();
	CHK(t_reg_count == 3);
	CHK(t_reg_overflow == 0);
	CHK(t_regs[0].codec == 0x68766331u);            /* 'hvc1' */
	CHK(t_regs[1].codec == 0x68657631u);            /* 'hev1' */
	CHK(t_regs[2].codec == 0x61766331u);            /* 'avc1' */
	CHK(t_regs[0].create == &fake_hevc_factory);
	CHK(t_regs[1].create == &fake_hevc_factory);
	CHK(t_regs[2].create == &n2avc_vt_create);
	CHK(strcmp(t_id_of(0), N2VT_ID_HEVC) == 0);
	CHK(strcmp(t_id_of(1), N2VT_ID_HEVC) == 0);
	CHK(strcmp(t_id_of(2), N2VT_ID_AVC) == 0);
	CHK(!t_has_hw_key(0) && !t_has_hw_key(2));
	CHK(t_dlopen_calls == 1);
	CHK(t_dlopen_flags == 6);                       /* Darwin RTLD_NOW | RTLD_LOCAL = 2 | 4 */
	CHK(t_dlopen_bad_path == 0);
	CHK(t_dlsym_calls == 1);                        /* only the HEVC image is ever inspected */
	CHK(t_dlsym_bad_name == 0);
	CHK(t_access_bad_path == 0);
	CHK(t_live_dicts == 0);                         /* both info dictionaries were released */
	CHK(strstr(t_log, "registered: hvc1=ok hev1=ok avc1=ok jpeg=absent") != NULL);
	CHK(n2vt_version() == 1);
	return 1;
}

static int
sc_switch_off(void)
{
	t_off = 1;
	SLIMEncoderRegister();
	CHK(t_reg_count == 0);
	CHK(t_dlopen_calls == 0);
	CHK(t_live_dicts == 0);
	CHK(strstr(t_log, "off: ") != NULL);
	CHK(strstr(t_log, "registered:") == NULL);      /* the early return writes no summary */
	CHK(t_log_opens == 1);                          /* one console line, not more */
	return 1;
}

static int
sc_no_hevc(void)
{
	t_no_hevc = 1;
	SLIMEncoderRegister();
	CHK(t_reg_count == 1);
	CHK(t_regs[0].codec == 0x61766331u);
	CHK(t_dlopen_calls == 0);
	CHK(t_live_dicts == 0);
	CHK(strstr(t_log, "registered: hvc1=off hev1=off avc1=ok jpeg=absent") != NULL);
	return 1;
}

static int
sc_dlopen_fails(void)
{
	t_hevc_image_present = 0;
	SLIMEncoderRegister();
	CHK(t_reg_count == 1);
	CHK(t_regs[0].codec == 0x61766331u);
	CHK(t_dlopen_calls == 1);
	CHK(t_dlsym_calls == 0);
	CHK(t_live_dicts == 0);
	CHK(strstr(t_log, "registered: hvc1=failed hev1=failed avc1=ok jpeg=absent") != NULL);
	return 1;
}

static int
sc_no_avc_factory(void)
{
	SLIMEncoderRegister();
	CHK(t_reg_count == 2);
	CHK(t_regs[0].codec == 0x68766331u);
	CHK(t_regs[1].codec == 0x68657631u);
	CHK(t_live_dicts == 0);
	return 1;
}

static int
sc_claim_hw(void)
{
	t_claim_hw = 1;
	SLIMEncoderRegister();
	CHK(t_reg_count == 3);
	CHK(t_claims_hw(0));
	CHK(t_claims_hw(1));
	CHK(t_claims_hw(2));
	CHK(t_info(0)->count == 2);                     /* implementation ID + IsHardwareAccelerated */
	CHK(t_live_dicts == 0);
	return 1;
}

static int
sc_no_vt_register(void)
{
	SLIMEncoderRegister();
	CHK(t_reg_count == 0);
	CHK(t_live_dicts == 0);
	CHK(strstr(t_log, "VTRegisterVideoEncoderWithInfo") != NULL);
	return 1;
}

static int
sc_log_budget(void)
{
	char line[64];

	for (int i = 0; i < 45; i++) {
		snprintf(line, sizeof line, "line %d", i);
		n2vt_say("%s", line);
	}
	CHK(t_log_opens == 41);
	CHK(t_log_closes == 41);
	CHK(t_log_bad_fd == 0);
	CHK(t_log_bad_path == 0);
	CHK(t_open_bad_flags == 0);
	{
		int lines = 0;
		const char *p = t_log;

		while (*p != '\0') {
			const char *newline = strchr(p, '\n');

			if (newline == NULL) {
				break;
			}
			lines++;
			p = newline + 1;
		}
		CHK(lines == 41);                       /* 40 messages + one suppression line */
	}
	CHK(strstr(t_log, "line 0\n") != NULL);
	CHK(strstr(t_log, "line 39\n") != NULL);
	CHK(strstr(t_log, "line 40") == NULL);
	CHK(strstr(t_log, "(further lines suppressed)\n") != NULL);
	{
		const char *last = strrchr(t_log, '\n');
		const char *suppression = strstr(t_log, "(further lines suppressed)");

		CHK(last != NULL && suppression != NULL);
		CHK(suppression < last);                /* the suppression line is the last one written */
	}
	CHK(strcmp(t_log, "N2VIDEO t_shim[4242]: line 0\nN2VIDEO t_shim[4242]: line 1\n"
	                  "N2VIDEO t_shim[4242]: line 2\nN2VIDEO t_shim[4242]: line 3\n"
	                  "N2VIDEO t_shim[4242]: line 4\n") != 0);   /* the whole prefix, not just the first line */
	CHK(strncmp(t_log, "N2VIDEO t_shim[4242]: line 0\n", strlen("N2VIDEO t_shim[4242]: line 0\n")) == 0);
	return 1;
}

static int
sc_log_quiet(void)
{
	t_quiet = 1;
	n2vt_say("you must not see this");
	n2vt_say("nor this");
	CHK(t_log_length == 0);
	CHK(t_log_opens == 0);
	return 1;
}

static int
sc_log_prefix_format(void)
{
	n2vt_say("value=%d", 7);
	CHK(t_log_opens == 1);
	CHK(strcmp(t_log, "N2VIDEO t_shim[4242]: value=7\n") == 0);
	return 1;
}

/* ------------------------------------------------------------------------------------------------------------------------
 * The runner: a scenario per forked child (SLIMEncoderRegister is once per process), then the two in-process log checks
 * (also forked, the log's budget is per process).
 * --------------------------------------------------------------------------------------------------------------------- */
enum { M_AVC_PRESENT = 0, M_AVC_ABSENT, M_VT_ABSENT, M_LOG };

struct scenario { const char *name; int (*fn)(void); int mode; };

static int
t_run(const char *name, int (*fn)(void))
{
	pid_t pid;
	int status;

	fflush(stdout);
	fflush(stderr);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		printf("not ok %s (fork failed)\n", name);
		return 0;
	}
	if (pid == 0) {
		t_reset();
		_exit(fn() ? 0 : 1);
	}
	if (waitpid(pid, &status, 0) != pid) {
		printf("not ok %s (waitpid failed)\n", name);
		return 0;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		printf("ok %s\n", name);
		return 1;
	}
	printf("not ok %s\n", name);
	return 0;
}

int
main(void)
{
	static const struct scenario scenarios[] = {
		{ "register.all-factories: hvc1, hev1, avc1 in that order", sc_all_factories,   M_AVC_PRESENT },
		{ "register.off-switch: nothing",                           sc_switch_off,       M_AVC_PRESENT },
		{ "register.no-hevc: avc1 only",                            sc_no_hevc,          M_AVC_PRESENT },
		{ "register.dlopen-fails: avc1 only, no crash",             sc_dlopen_fails,     M_AVC_PRESENT },
		{ "register.claim-hw: IsHardwareAccelerated true",          sc_claim_hw,         M_AVC_PRESENT },
		{ "register.no-avc-factory: hvc1 and hev1 only",            sc_no_avc_factory,   M_AVC_ABSENT },
		{ "register.vt-register-absent: nothing, said so",          sc_no_vt_register,   M_VT_ABSENT },
		{ "log.budget: 40 lines then one suppression line",         sc_log_budget,       M_LOG },
		{ "log.quiet: silent",                                      sc_log_quiet,        M_LOG },
		{ "log.prefix: N2VIDEO prog[pid]: text",                    sc_log_prefix_format, M_LOG },
	};
	int passed = 0;
	int total = 0;

#if defined(T_NO_VT_REGISTER)
	const int run_mode = M_VT_ABSENT;
#elif HAVE_AVC_FACTORY
	const int run_mode = -1;                        /* the whole matrix */
#else
	const int run_mode = M_AVC_ABSENT;
#endif

	printf("t_shim: shim and log, one scenario per process\n");
	for (unsigned i = 0; i < sizeof scenarios / sizeof scenarios[0]; i++) {
		int run;

		if (run_mode == -1) {
			/* the normal matrix: everything except the scenario of a binary that lacks a symbol */
			run = scenarios[i].mode != M_VT_ABSENT &&
			      !(scenarios[i].mode == M_AVC_ABSENT && HAVE_AVC_FACTORY) &&
			      !(scenarios[i].mode == M_AVC_PRESENT && !HAVE_AVC_FACTORY);
		} else {
			run = scenarios[i].mode == run_mode;
		}
		if (!run) {
			continue;
		}
		total++;
		passed += t_run(scenarios[i].name, scenarios[i].fn);
	}
	printf("N2RESULT t_shim %d %d\n", passed, total);
	return passed == total && total > 0 ? 0 : 1;
}

#endif /* !T_ONLY_THREADS */

#if defined(T_ONLY_THREADS)
/*
 * The once-per-process register called 3 times from each of 4 threads. The whole point is TSan: the scenario passes only
 * if exactly one registration round happened (three codec types, in order) and no thread saw a torn once state.
 */
static void *
t_thread_worker(void *argument)
{
	(void)argument;
	for (int i = 0; i < 3; i++) {
		SLIMEncoderRegister();
	}
	return NULL;
}

int
main(void)
{
	pthread_t threads[4];
	int ok;

	/* The forked-scenario counters are still written by the doubles in this binary (which runs only the once check); one
	 * read keeps them from being "set but not used" and documents that they are shared on purpose. */
	(void)(t_reg_overflow + t_access_bad_path + t_dlopen_bad_path + t_dlopen_flags + t_dlsym_bad_name +
	       t_log_opens + t_open_bad_flags);
	t_reset();
	for (unsigned i = 0; i < sizeof threads / sizeof threads[0]; i++) {
		if (pthread_create(&threads[i], NULL, t_thread_worker, NULL) != 0) {
			printf("not ok register.once: pthread_create failed\n");
			printf("N2RESULT t_shim_threads 0 1\n");
			return 1;
		}
	}
	for (unsigned i = 0; i < sizeof threads / sizeof threads[0]; i++) {
		(void)pthread_join(threads[i], NULL);
	}
	ok = t_reg_count == 3 &&
	     t_regs[0].codec == 0x68766331u && t_regs[1].codec == 0x68657631u && t_regs[2].codec == 0x61766331u &&
	     t_regs[0].create == &fake_hevc_factory && t_regs[2].create == &n2avc_vt_create &&
	     t_dlopen_calls == 1 && t_dlsym_calls == 1 && t_live_dicts == 0;
	printf("%s register.once: 3 calls x 4 threads -> one registration\n", ok ? "ok" : "not ok");
	printf("N2RESULT t_shim_threads %d 1\n", ok ? 1 : 0);
	return ok ? 0 : 1;
}
#endif /* T_ONLY_THREADS */
