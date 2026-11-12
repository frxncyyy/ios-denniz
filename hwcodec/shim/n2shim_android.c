/* SPDX-License-Identifier: MIT */
/*
 * n2shim_android.c -- Android runtime imports of the five vendor codec objects (package V34).
 *
 * What this file is: the host implementations behind 35 of the 149 imports the vendor set makes, the ones that
 * belong to the Android user-space runtime rather than to libc proper (D01 census: facts/imports.py --table
 * android). They fall into six groups:
 *
 *   logging        __android_log_print, __android_log_vprint -> the n2shim_log callback. No printing from here.
 *   properties     property_get from the table in n2shim_props.c; an unknown key returns the caller's default and
 *                  is reported once through n2shim_log.
 *   tracing/clock  atrace_get_enabled_tags (0: no tags enabled), atrace_begin_body / atrace_end_body /
 *                  atrace_int_body (no-ops), perf_lock_acq / perf_lock_rel (0: no performance service).
 *   ion            mt_ion_open/mt_ion_close and ion_alloc / ion_alloc_camera / ion_free / ion_share /
 *                  ion_share_close / ion_import / ion_mmap / ion_munmap / ion_custom_ioctl over the `n2shim_hw`
 *                  DMA back end (include/n2vc_hw.h). An ion handle IS an n2vc_hw dma handle; ion_mmap is
 *                  n2vc_dma_guest_map(); the two custom ioctls the vendor uses to turn a handle into an address
 *                  (ION_MM_GET_IOVA, ION_SYS_GET_PHYS) write the dma `pa` into the caller's struct. The command
 *                  numbers and struct offsets were read off the call sites in libvcodec_utility.so and
 *                  libvpud_vcodec.so; STATUS.md has the table with addresses.
 *   dynamic load   dlopen / dlsym / dlclose / dlerror / dladdr restricted to the loaded n2elf set (`n2shim_set`).
 *                  The vendor asks for three libraries that are NOT in the set -- "AVCSecureVdecCA.so",
 *                  "HEVCSecureVdecCA.so" and "/vendor/lib/libmtcloader.so" -- and correctly handles a NULL dlopen,
 *                  so the secure-video and thermal-policy paths stay off (the first is out of scope, the second is
 *                  a perf hint we do not want).
 *   C++/CRT        __cxa_atexit (recorded and run at exit), __cxa_finalize, __register_atfork (ignored),
 *                  __libc_init (calls the guest main), __gnu_Unwind_Find_exidx (0), __aeabi_unwind_cpp_pr0 (no-op).
 *   math           log and exp2, double precision, in the armeabi-v7a soft-float ABI the guest uses (double in
 *                  r0:r1, never d0). The H.264 slice-header POC calculation is the only user.
 *
 * ABI notes that are easy to get wrong and are tested:
 *   - guest pointers (tag, property key/value/default, dl names, Dl_info, ion structs) are 32-bit GUEST
 *     addresses; every string this file returns to the guest (dlerror, dladdr's dli_fname/dli_sname) is placed in
 *     guest memory first, never a host pointer;
 *   - property_get copies at most 91 bytes + NUL (bionic PROP_VALUE_MAX = 92 with a zeroed buffer) and returns
 *     the strlen, which is what the vendor parses with atoi();
 *   - atrace_get_enabled_tags returns a 64-bit 0 in r0:r1;
 *   - __libc_init's 4th argument is the old-bionic `structors` array (vpud passes a 3-pointer array built on its
 *     stack: preinit_array, init_array, fini_array addresses). The loader already ran those arrays through
 *     n2elf_run_init(), so the array is deliberately not traversed a second time (documented, tested).
 *
 * Error convention: a shim that fails either returns the value the vendor would see from Android (0 / -1 / NULL)
 * or sets the guest's errno where V33's stdio shims own errno. Nothing here aborts, exits or prints.
 *
 * State the runner (IV) must set before guest code runs:
 *     n2shim_set          the linked n2elf set (dlsym/dladdr)
 *     n2shim_hw           the DMA back end (ion), NULL = every ion call returns -ENODEV
 *     n2shim_log          the log sink (V33's stdio table defines the same global; this file's definition is weak
 *                         so V33's strong one wins when both are linked)
 *     n2shim_libc_argc/argv/envp   what __libc_init passes to main; argv == 0 makes it parse raw_args instead
 *
 * Threading: guest threads (V32) call these shims on several host threads at once, so every mutable table here is
 * protected by one host mutex. The lock is never held across a call into the guest or into the DMA back end.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2vc_hw.h"
#include "n2fmt.h"

/* V41 (ap/n2vc_dma.c); weak so the shim links without the AP side.  (opus-venc / board 11 November) */
extern int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes)
	__attribute__((weak));
extern int n2vc_dma_sync(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr, uint32_t offset,
	uint32_t bytes, int to_device) __attribute__((weak));

/* ------------------------------------------------------------------------------------------------------------------
 * State the runner sets
 * ---------------------------------------------------------------------------------------------------------------- */

struct n2elf_set *n2shim_set;                    /* dlsym/dladdr search this set only */
const struct n2vc_hw *n2shim_hw;                 /* ion DMA back end, NULL = -ENODEV */
__attribute__((weak)) void (*n2shim_log)(int prio, const char *tag, const char *text);
int n2shim_libc_argc;                            /* __libc_init: guest main's argc (runner-set) */
uint32_t n2shim_libc_argv;                       /* __libc_init: guest address of char **argv, 0 = parse raw_args */
uint32_t n2shim_libc_envp;                       /* __libc_init: guest address of char **envp */
int n2shim_libc_status = -1;                     /* __libc_init: main's return value, -1 until it returns */

/* A shim's return value that says "the guest must stop, main returned" is signalled with c->stop_request; the
 * runner reads n2shim_libc_status afterwards. Documented and tested. */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------------------------------------------------------
 * Property table interface (defined in n2shim_props.c; no shared header, see the note in that file)
 * ---------------------------------------------------------------------------------------------------------------- */

struct n2prop_entry { const char *key; const char *value; const char *why; };
extern const struct n2prop_entry n2shim_props[];
extern const struct n2prop_entry *n2shim_prop_find(const char *key);

/* ------------------------------------------------------------------------------------------------------------------
 * Small helpers
 * ---------------------------------------------------------------------------------------------------------------- */

#define N2AND_LOG_TEXT   1024u   /* bytes of formatted log text kept, like bionic's LOG_BUF_SIZE - 1 */
#define N2AND_TAG        64u
#define N2AND_KEY        128u
#define N2AND_PROP_MAX   92u     /* bionic PROP_VALUE_MAX; value buffers are 92 bytes incl. NUL */
#define N2AND_DL_NAME    128u

static void log_emit(int prio, const char *tag, const char *text)
{
	if (n2shim_log != NULL)
		n2shim_log(prio, tag, text);   /* nothing prints when the runner has not set a sink */
}

/* Copy a guest NUL-terminated string into a host buffer. Returns the number of bytes copied without the NUL.
 * A NULL guest pointer yields an empty string. A guest fault inside n2a32_ld8 becomes a FAULT stop in the core,
 * exactly as for V30's string shims, so no separate bound check is needed here. */
static size_t guest_str(struct n2a32_cpu *c, uint32_t ga, char *out, size_t cap)
{
	size_t i = 0;

	if (cap == 0)
		return 0;
	if (ga == 0) {
		out[0] = '\0';
		return 0;
	}
	while (i + 1 < cap) {
		uint8_t ch = n2a32_ld8(c, ga + (uint32_t)i);

		out[i++] = (char)ch;
		if (ch == 0)
			return i - 1;
	}
	out[cap - 1] = '\0';
	return cap - 1;
}

/* Write a host string to guest memory (bounded), NUL included. Returns the number of bytes written without NUL. */
static size_t guest_put_str(struct n2a32_cpu *c, uint32_t ga, const char *s, size_t max)
{
	size_t i;

	if (ga == 0)
		return 0;
	for (i = 0; i < max && s[i] != '\0'; i++)
		n2a32_st8(c, ga + (uint32_t)i, (uint8_t)s[i]);
	n2a32_st8(c, ga + (uint32_t)i, 0);
	return i;
}

/* A guest buffer for a string this file has to return to the guest (dlerror, dladdr). Allocated once per process
 * from the guest heap; `*slot` holds its guest address. Returns 0 when the guest heap is full. */
static uint32_t guest_string_slot(struct n2a32_cpu *c, uint32_t *slot, const char *s, size_t cap)
{
	if (*slot == 0) {
		uint32_t a = n2a32_mem_alloc(c->mem, (uint32_t)cap, 8);

		if (a == 0)
			return 0;
		*slot = a;
	}
	(void)guest_put_str(c, *slot, s, cap - 1);
	return *slot;
}

static double ld_double(struct n2a32_cpu *c)
{
	uint64_t u = (uint64_t)c->r[0] | ((uint64_t)c->r[1] << 32);
	double d;

	memcpy(&d, &u, sizeof d);
	return d;
}

static void st_double(struct n2a32_cpu *c, double d)
{
	uint64_t u;

	memcpy(&u, &d, sizeof u);
	n2shim_ret64(c, u);
}

/* ------------------------------------------------------------------------------------------------------------------
 * Logging: __android_log_print / __android_log_vprint
 * ---------------------------------------------------------------------------------------------------------------- */

/* prio: 2 verbose, 3 debug, 4 info, 5 warn, 6 error, 7 fatal (ANDROID_LOG_*). Returns the number of characters
 * actually kept (<= N2AND_LOG_TEXT - 1), like liblog's write path. A NULL format returns -1, like n2fmt. */
static int android_log_common(struct n2a32_cpu *c, unsigned first_vararg, int use_va, uint32_t guest_va)
{
	char text[N2AND_LOG_TEXT];
	char tag[N2AND_TAG];
	struct n2fmt_args a;
	int prio = (int)n2shim_arg(c, 0);
	uint32_t gtag = n2shim_arg(c, 1);
	uint32_t gfmt = n2shim_arg(c, 2);
	int n;

	(void)guest_str(c, gtag, tag, sizeof tag);
	if (use_va) {
		if (n2fmt_args_va(&a, c, guest_va) != 0) {
			log_emit(prio, tag, "(null va_list)");
			return -1;
		}
	} else {
		n2fmt_args_regs(&a, c, first_vararg);
	}
	n = n2fmt(c, text, sizeof text, gfmt, &a);
	if (n < 0) {
		log_emit(prio, tag, "(null format)");
		return -1;
	}
	if ((size_t)n > sizeof text - 1)
		n = (int)sizeof text - 1;
	log_emit(prio, tag, text);
	return n;
}

static void sh_android_log_print(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)android_log_common(c, 3, 0, 0));
}

static void sh_android_log_vprint(struct n2a32_cpu *c)
{
	/* int prio, const char *tag, const char *fmt, va_list ap: ap is a guest pointer to the va_list object */
	n2shim_ret(c, (uint32_t)android_log_common(c, 0, 1, n2shim_arg(c, 3)));
}

/* ------------------------------------------------------------------------------------------------------------------
 * property_get
 * ---------------------------------------------------------------------------------------------------------------- */

#define N2AND_UNKNOWN_MAX 32

static char g_unknown[N2AND_UNKNOWN_MAX][N2AND_PROP_MAX + 4];
static unsigned g_nunknown;

/* Report an unknown key once (bounded: the first 32 distinct keys; after that nothing is reported, so a hostile or
 * broken guest cannot grow this table). `def` is what the caller will receive. */
static void note_unknown_key(const char *key, const char *def)
{
	unsigned i;

	pthread_mutex_lock(&g_lock);
	for (i = 0; i < g_nunknown; i++) {
		if (strcmp(g_unknown[i], key) == 0) {
			pthread_mutex_unlock(&g_lock);
			return;
		}
	}
	if (g_nunknown == N2AND_UNKNOWN_MAX) {
		pthread_mutex_unlock(&g_lock);
		return;
	}
	strncpy(g_unknown[g_nunknown], key, sizeof g_unknown[0] - 1);
	g_unknown[g_nunknown][sizeof g_unknown[0] - 1] = '\0';
	g_nunknown++;
	pthread_mutex_unlock(&g_lock);

	{
		char msg[N2AND_PROP_MAX + 96];

		/* build the message without snprintf so that this file has no formatter dependency of its own */
		size_t n = 0;
		const char *p = "unknown property '";
		const char *q = "' -> caller's default '";

		while (*p && n + 1 < sizeof msg)
			msg[n++] = *p++;
		while (*key && n + 1 < sizeof msg)
			msg[n++] = *key++;
		while (*q && n + 1 < sizeof msg)
			msg[n++] = *q++;
		while (*def && n + 1 < sizeof msg)
			msg[n++] = *def++;
		if (n + 1 < sizeof msg)
			msg[n++] = '\'';
		msg[n] = '\0';
		log_emit(5 /* ANDROID_LOG_WARN */, "n2shim-props", msg);
	}
}

/* int property_get(const char *key, char *value, const char *default_value)
 *
 * Android semantics, exactly: a property that exists copies its value; a property that does not exist copies the
 * caller's default. The return value is strlen(value), which is what the vendor feeds to atoi(). The table has
 * three states (see n2shim_props.c): a set value, a known-but-absent key (NULL value -> caller's default, not
 * reported), and an unknown key (caller's default, reported once). */
static void sh_property_get(struct n2a32_cpu *c)
{
	char key[N2AND_KEY];
	char def[N2AND_PROP_MAX];
	uint32_t gkey = n2shim_arg(c, 0);
	uint32_t gval = n2shim_arg(c, 1);
	uint32_t gdef = n2shim_arg(c, 2);
	const struct n2prop_entry *e;
	const char *src;
	size_t n;

	(void)guest_str(c, gkey, key, sizeof key);
	(void)guest_str(c, gdef, def, sizeof def);
	e = n2shim_prop_find(key);
	if (e != NULL && e->value != NULL) {
		src = e->value;
	} else if (e != NULL) {
		src = def;               /* known key, not present on this phone: caller's default */
	} else {
		src = def;
		note_unknown_key(key, def);
	}
	n = strlen(src);
	if (n >= N2AND_PROP_MAX)
		n = N2AND_PROP_MAX - 1;
	if (gval != 0) {
		(void)guest_put_str(c, gval, src, n);
		n2shim_ret(c, (uint32_t)n);
	} else {
		n2shim_ret(c, 0);
	}
}

/* Test hooks (not used by the vendor code). */
unsigned n2shim_props_unknown_count(void)
{
	unsigned n;

	pthread_mutex_lock(&g_lock);
	n = g_nunknown;
	pthread_mutex_unlock(&g_lock);
	return n;
}

void n2shim_props_forget_unknown(void)
{
	pthread_mutex_lock(&g_lock);
	g_nunknown = 0;
	memset(g_unknown, 0, sizeof g_unknown);
	pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------------------------------------------------------
 * atrace and performance service
 * ---------------------------------------------------------------------------------------------------------------- */

static void sh_atrace_get_enabled_tags(struct n2a32_cpu *c)
{
	n2shim_ret64(c, 0);   /* ATRACE_TAG_NEVER: the vendor then skips every atrace_body call */
}

static void sh_atrace_body(struct n2a32_cpu *c)
{
	(void)c;              /* no tracing on this host; the argument is a guest string we never read */
}

/* int perf_lock_acq(int handle, int duration, int list[], int num_args): 0 = no performance service, no lock held.
 * The vendor only treats the result as an opaque handle and passes it to perf_lock_rel, which also returns 0. */
static void sh_perf_lock_acq(struct n2a32_cpu *c)
{
	n2shim_ret(c, 0);
}

static void sh_perf_lock_rel(struct n2a32_cpu *c)
{
	n2shim_ret(c, 0);
}

/* ------------------------------------------------------------------------------------------------------------------
 * sec_drv_base_check / sec_drv_info_update
 *
 * Callers (read off the call sites in libvcodec_utility.so and the callee in the phone's libmtk_drvb.so):
 *   drvb_fc  0x345e8: drvb_fc_prepare(out); sec_drv_base_check(ctx, out); drvb_fc_respond(out, digest)
 *   drvb_f2  0x34638: drvb_fc_prepare(out); sec_drv_info_update(ctx, a, b, out); drvb_fc_respond(out, digest)
 * Both wrappers are `void`: the return value of the sec_drv_* call is not used by anything. The real function in
 * libmtk_drvb.so talks to the secure world (mtk_drvb_basechk / platform_init / platform_advchk) and re-signs the
 * response buffer, after which drvb_fc_respond() verifies it. With no secure world, returning 0 leaves the buffer
 * untouched, so drvb_fc_respond() takes its documented error path (it calls drvb_error(0xdeaddead) and returns 1,
 * which its own caller discards). Nothing in the codec path depends on the result; the secure decoder needs its
 * own TEE and is out of scope (PLAN 1.4, FACTS 8.5).
 *
 * Return 0 with the buffer untouched. The same structural note is in STATUS.md.
 * ---------------------------------------------------------------------------------------------------------------- */

static void sh_sec_drv_base_check(struct n2a32_cpu *c)
{
	n2shim_ret(c, 0);
}

static void sh_sec_drv_info_update(struct n2a32_cpu *c)
{
	n2shim_ret(c, 0);
}

/* ------------------------------------------------------------------------------------------------------------------
 * ion over the n2vc_hw DMA back end
 *
 * Signatures and argument positions come from the call sites (STATUS.md has the addresses):
 *   int  mt_ion_open(const char *client)                       utility 0x1793a "vpud"; vpud_vcodec 0x140c2 "vpud"
 *   int  mt_ion_close(int fd)                                  utility 0x179be
 *   int  ion_alloc(int fd, size_t len, size_t align,
 *                  unsigned heap_mask, unsigned flags,
 *                  ion_user_handle_t *handle)                   utility 0x17a5a (6 args: r0-r3 + flags, handle)
 *   int  ion_alloc_camera(int fd, size_t len, size_t align,
 *                         unsigned flags, ion_user_handle_t *h) utility 0x17eb6 (5 args: r0-r3 + handle)
 *   int  ion_free(int fd, ion_user_handle_t handle)            utility 0x1838c
 *   int  ion_share(int fd, ion_user_handle_t handle, int *fd_out)  utility 0x17ef4
 *   int  ion_share_close(int fd, int share_fd)                 utility 0x18374
 *   int  ion_import(int fd, int share_fd, ion_user_handle_t *handle) vpud_vcodec 0xaebe
 *   int  ion_mmap(int fd, void *addr, size_t len, int prot, int flags,
 *                 int share_fd, int offset)                    utility 0x1803c, vpud_vcodec 0xb12a
 *   int  ion_munmap(int fd, void *addr, size_t len)            utility 0x1835c
 *   int  ion_custom_ioctl(int fd, unsigned cmd, void *data)    utility 0x1809a/0x17acc/0x17cb2/0x17dec, ...
 *
 * ion_custom_ioctl's two levels (kernel mtk_ion drivers/staging/android/mtk_ion/mtk/ion_drv.h and uapi/ion.h):
 *   cmd 0 = ION_CMD_SYSTEM,          data->sys_cmd 0 = ION_SYS_CACHE_SYNC (no result)
 *                                    data->sys_cmd 1 = ION_SYS_GET_PHYS:  {handle @8, phy_addr @12, len @16}
 *   cmd 1 = ION_CMD_MULTIMEDIA,      data->mm_cmd 0 = ION_MM_CONFIG_BUFFER (no result)
 *                                    data->mm_cmd 8 = ION_MM_GET_IOVA:    {handle @8, module @12, ..., phy_addr @32}
 *   cmd 2 = ION_CMD_MULTIMEDIA_SEC,  same mm_cmd values, used by the secure allocation path
 * struct ion_mm_data / ion_sys_data put the enum at offset 0 and the union at offset 8 (the union contains an
 * unsigned long long, so it is 8-aligned on a 32-bit ARM). The offsets above were checked at both call sites.
 *
 * An ion handle is an n2vc_hw dma handle; an ion share fd is a 32-bit token this file hands out and resolves; an ion
 * mmap is n2vc_dma_guest_map() over the guest memory of the VM. Cross-process dmabuf fds cannot exist in a single
 * process, so a share fd this file did not create is rejected with -EINVAL unless the integrator registered it with
 * n2shim_ion_register_share() (the AP side does that when it allocates a buffer the daemon will import). A buffer
 * published that way is `foreign`: the AP owns it, so the importer's ion_free() and ion_share_close() are no-ops
 * that succeed and leave the handle and the share id usable for the next job (board: IV -> V34, 11 November -- the
 * real vpud closes the imported share after every encode and then imports it again).
 * ---------------------------------------------------------------------------------------------------------------- */

#define N2AND_ION_FDS   4
#define N2AND_ION_BUFS  256   /* opus-fmt: vpud holds 34-53 ion buffers; VP9/AV1 add 2 x (dpb+2) client shares */
#define N2AND_ION_FD0   0x700     /* fake fd numbers, outside V33's guest fd table (100+) */
#define N2AND_ION_SHARE0 0x400    /* fake share fd numbers */

struct n2and_ion_buf {
	uint8_t used;
	uint8_t shared;
	uint8_t foreign;          /* registered by the AP through n2shim_ion_register_share() */
	uint32_t handle;          /* n2vc_hw dma handle; 0 is not a valid handle here */
	uint32_t pa;
	uint32_t bytes;
	int32_t share_id;
};

static struct n2and_ion_buf g_ion_buf[N2AND_ION_BUFS];
static uint8_t g_ion_fd[N2AND_ION_FDS];
static int32_t g_ion_next_share = N2AND_ION_SHARE0;

static struct n2and_ion_buf *ion_find_handle(uint32_t handle)
{
	unsigned i;

	for (i = 0; i < N2AND_ION_BUFS; i++)
		if (g_ion_buf[i].used && g_ion_buf[i].handle == handle)
			return &g_ion_buf[i];
	return NULL;
}

static struct n2and_ion_buf *ion_find_share(uint32_t share_fd)
{
	unsigned i;

	for (i = 0; i < N2AND_ION_BUFS; i++)
		if (g_ion_buf[i].used && g_ion_buf[i].shared && (uint32_t)g_ion_buf[i].share_id == share_fd)
			return &g_ion_buf[i];
	return NULL;
}

static struct n2and_ion_buf *ion_alloc_slot(void)
{
	unsigned i;

	for (i = 0; i < N2AND_ION_BUFS; i++)
		if (!g_ion_buf[i].used)
			return &g_ion_buf[i];
	return NULL;
}

static void sh_mt_ion_open(struct n2a32_cpu *c)
{
	char client[64];
	unsigned i;
	int fd = -1;

	(void)guest_str(c, n2shim_arg(c, 0), client, sizeof client);   /* "vpud" on this phone; only for the trace */
	pthread_mutex_lock(&g_lock);
	for (i = 0; i < N2AND_ION_FDS; i++) {
		if (!g_ion_fd[i]) {
			g_ion_fd[i] = 1;
			fd = N2AND_ION_FD0 + (int)i;
			break;
		}
	}
	pthread_mutex_unlock(&g_lock);
	if (fd < 0) {
		n2shim_ret(c, (uint32_t)-EMFILE);
		return;
	}
	n2shim_ret(c, (uint32_t)fd);
}

static void sh_mt_ion_close(struct n2a32_cpu *c)
{
	uint32_t fd = n2shim_arg(c, 0);
	unsigned i;

	if (fd >= N2AND_ION_FD0 && fd < N2AND_ION_FD0 + N2AND_ION_FDS) {
		i = fd - N2AND_ION_FD0;
		pthread_mutex_lock(&g_lock);
		g_ion_fd[i] = 0;
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	n2shim_ret(c, (uint32_t)-EBADF);
}

/* Common tail of ion_alloc / ion_alloc_camera. Returns 0 or -errno and writes the guest handle on success. */
static int ion_alloc_common(struct n2a32_cpu *c, uint32_t len, uint32_t align, uint32_t flags, uint32_t ghandle)
{
	struct n2and_ion_buf *b;
	uint32_t handle = 0, pa = 0;
	int rc;

	if (ghandle == 0 || len == 0)
		return -EINVAL;
	if (n2shim_hw == NULL || n2shim_hw->dma_alloc == NULL)
		return -ENODEV;
	/* `flags` are ION flags (ION_FLAG_CACHED = 1, ION_FLAG_CACHED_NEEDS_SYNC = 2, MTK bits above), not RWVC_DMA_*
	 * flags: the kernel's RWVC_DMA_ALLOC rejects anything but RWVC_DMA_SHARED with EINVAL (phone run vc10: vpud's
	 * ENC_INIT allocation failed, the INIT reply carried FAIL and n2ap_venc_open returned -EINVAL), and ION_FLAG_CACHED
	 * would alias RWVC_DMA_SHARED.  A guest ION buffer is private to the daemon; cache maintenance is the explicit
	 * ION_SYS_CACHE_SYNC path.  So the back end always gets 0. */
	(void)flags;
	rc = n2shim_hw->dma_alloc(n2shim_hw->ctx, len, 0u, &handle, &pa);
	if (rc != 0) {
		log_emit(6, "n2shim-ion", "ion_alloc: dma_alloc failed");
		return rc;
	}
	if (handle == 0)
		return -EIO;   /* a dma_alloc that returns 0 without a handle is a back-end bug */
	if (align > 1 && (align & (align - 1)) == 0 && (pa & (align - 1)) != 0) {
		/* n2vc_hw has no alignment parameter; a backend that cannot honour a request above its page size is
		 * reported once instead of failing the allocation (the codec still works, just not at that alignment) */
		static int warned;
		int do_log = 0;

		pthread_mutex_lock(&g_lock);
		if (!warned) {
			warned = 1;
			do_log = 1;
		}
		pthread_mutex_unlock(&g_lock);
		if (do_log)
			log_emit(5, "n2shim-ion", "ion_alloc: the DMA back end returned an address below the "
			         "requested alignment; the codec still works, the buffer is just not aligned");
	}

	pthread_mutex_lock(&g_lock);
	b = ion_alloc_slot();
	if (b == NULL) {
		pthread_mutex_unlock(&g_lock);
		if (n2shim_hw->dma_free != NULL)
			n2shim_hw->dma_free(n2shim_hw->ctx, handle);
		return -ENOMEM;
	}
	memset(b, 0, sizeof *b);
	b->used = 1;
	b->handle = handle;
	b->pa = pa;
	b->bytes = len;
	b->share_id = -1;
	pthread_mutex_unlock(&g_lock);

	n2a32_st32(c, ghandle, handle);
	return 0;
}

static void sh_ion_alloc(struct n2a32_cpu *c)
{
	/* fd, len, align, heap_mask, flags, *handle */
	uint32_t len = n2shim_arg(c, 1);
	uint32_t align = n2shim_arg(c, 2);
	uint32_t flags = n2shim_arg(c, 4);
	uint32_t ghandle = n2shim_arg(c, 5);

	n2shim_ret(c, (uint32_t)ion_alloc_common(c, len, align, flags, ghandle));
}

static void sh_ion_alloc_camera(struct n2a32_cpu *c)
{
	/* fd, len, align, flags, *handle */
	uint32_t len = n2shim_arg(c, 1);
	uint32_t align = n2shim_arg(c, 2);
	uint32_t flags = n2shim_arg(c, 3);
	uint32_t ghandle = n2shim_arg(c, 4);

	n2shim_ret(c, (uint32_t)ion_alloc_common(c, len, align, flags, ghandle));
}

static void sh_ion_free(struct n2a32_cpu *c)
{
	uint32_t handle = n2shim_arg(c, 1);
	struct n2and_ion_buf *b;

	pthread_mutex_lock(&g_lock);
	b = ion_find_handle(handle);
	if (b != NULL && b->foreign) {
		/* Board: IV -> V34 (11 November).  The exporter (the AP) owns this buffer; an importer's
		 * ion_free() only drops its own handle, it does not destroy the buffer or the share. */
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	if (b != NULL) {
		uint32_t h = b->handle;

		memset(b, 0, sizeof *b);
		pthread_mutex_unlock(&g_lock);
		if (n2shim_hw != NULL && n2shim_hw->dma_free != NULL)
			(void)n2shim_hw->dma_free(n2shim_hw->ctx, h);
		n2shim_ret(c, 0);
		return;
	}
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, (uint32_t)-EINVAL);
}

static void sh_ion_share(struct n2a32_cpu *c)
{
	uint32_t handle = n2shim_arg(c, 1);
	uint32_t gout = n2shim_arg(c, 2);
	struct n2and_ion_buf *b;

	if (gout == 0) {
		n2shim_ret(c, (uint32_t)-EINVAL);
		return;
	}
	pthread_mutex_lock(&g_lock);
	b = ion_find_handle(handle);
	if (b == NULL) {
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, (uint32_t)-EINVAL);
		return;
	}
	if (!b->shared) {
		b->shared = 1;
		b->share_id = g_ion_next_share++;
		if (g_ion_next_share <= N2AND_ION_SHARE0)
			g_ion_next_share = N2AND_ION_SHARE0 + 1;   /* wrap: keep share fds positive */
	}
	{
		uint32_t id = (uint32_t)b->share_id;

		pthread_mutex_unlock(&g_lock);
		n2a32_st32(c, gout, id);
		n2shim_ret(c, 0);
	}
}

static void sh_ion_share_close(struct n2a32_cpu *c)
{
	uint32_t share_fd = n2shim_arg(c, 1);
	struct n2and_ion_buf *b;

	pthread_mutex_lock(&g_lock);
	b = ion_find_share(share_fd);
	if (b != NULL && b->foreign) {
		/* Board: IV -> V34 (11 November).  Closing the importer's share fd does not invalidate
		 * the exporter's share; the AP may hand the same fd to the next encode job. */
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	if (b != NULL) {
		b->shared = 0;
		b->share_id = -1;
	}
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, b != NULL ? 0u : (uint32_t)-EINVAL);
}

static void sh_ion_import(struct n2a32_cpu *c)
{
	uint32_t share_fd = n2shim_arg(c, 1);
	uint32_t ghandle = n2shim_arg(c, 2);
	struct n2and_ion_buf *b;

	if (ghandle == 0) {
		n2shim_ret(c, (uint32_t)-EINVAL);
		return;
	}
	pthread_mutex_lock(&g_lock);
	b = ion_find_share(share_fd);
	if (b != NULL) {
		uint32_t handle = b->handle;

		pthread_mutex_unlock(&g_lock);
		n2a32_st32(c, ghandle, handle);
		n2shim_ret(c, 0);
		return;
	}
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, (uint32_t)-EINVAL);
}

static void sh_ion_mmap(struct n2a32_cpu *c)
{
	/* fd, addr, len, prot, flags, share_fd, offset(32-bit at slot 6; a 64-bit offset would put its low word
	 * at the same slot, and the value is not used) */
	uint32_t len = n2shim_arg(c, 2);
	uint32_t share_fd = n2shim_arg(c, 5);
	struct n2and_ion_buf *b;
	uint32_t handle, bytes;
	uint32_t ga = 0;
	int rc;

	pthread_mutex_lock(&g_lock);
	b = ion_find_share(share_fd);
	if (b == NULL) {
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);   /* 0 is MAP_FAILED for the vendor's checks */
		return;
	}
	handle = b->handle;
	bytes = b->bytes;
	pthread_mutex_unlock(&g_lock);

	if (len == 0 || len > bytes) {
		n2shim_ret(c, 0);
		return;
	}
	if (n2shim_hw == NULL) {
		n2shim_ret(c, 0);
		return;
	}
	rc = n2vc_dma_guest_map(n2shim_hw, c->mem, handle, len, &ga);
	n2shim_ret(c, rc == 0 ? ga : 0);
}

static void sh_ion_munmap(struct n2a32_cpu *c)
{
	uint32_t addr = n2shim_arg(c, 1);
	int rc = 0;

	if (n2shim_hw != NULL)
		rc = n2vc_dma_guest_unmap(n2shim_hw, c->mem, addr);
	n2shim_ret(c, rc == 0 ? 0u : (uint32_t)rc);
}

static void sh_ion_custom_ioctl(struct n2a32_cpu *c)
{
	uint32_t cmd = n2shim_arg(c, 1);
	uint32_t ga = n2shim_arg(c, 2);
	uint32_t sub, handle;
	struct n2and_ion_buf *b;
	uint32_t pa = 0, bytes = 0;
	int have = 0;

	if (ga == 0) {
		n2shim_ret(c, (uint32_t)-EINVAL);
		return;
	}
	sub = n2a32_ld32(c, ga);
	switch (cmd) {
	case 0:   /* ION_CMD_SYSTEM */
		if (sub == 1) {          /* ION_SYS_GET_PHYS */
			handle = n2a32_ld32(c, ga + 8);
			pthread_mutex_lock(&g_lock);
			b = ion_find_handle(handle);
			if (b != NULL) {
				pa = b->pa;
				bytes = b->bytes;
				have = 1;
			}
			pthread_mutex_unlock(&g_lock);
			if (!have) {
				n2shim_ret(c, (uint32_t)-EINVAL);
				return;
			}
			n2a32_st32(c, ga + 12, pa);
			n2a32_st32(c, ga + 16, bytes);
			n2shim_ret(c, 0);
			return;
		}
		if (sub == 0) {          /* ION_SYS_CACHE_SYNC: {handle @8, va @12, size @16, sync_type @20} */
			/* The guest mirror and the device copy are two memories here, so a cache operation is a
			 * copy: clean/flush pushes the guest bytes to the device, invalidate pulls them back.  The
			 * daemon writes SPS/PPS with the CPU and flushes (h264_enc_encode_Header ->
			 * VENC_FlushRangeForIon) and invalidates the bitstream after the hardware
			 * (VENC_gceEncodeEnd); without this the AP's from_device sync wiped the header.
			 * Types: mtk ion_drv.h ION_CACHE_SYNC_TYPE.  (opus-venc / board 11 November) */
			uint32_t va = n2a32_ld32(c, ga + 12), size = n2a32_ld32(c, ga + 16);
			uint32_t type = n2a32_ld32(c, ga + 20);
			uint32_t gbase = 0, gbytes = 0;

			handle = n2a32_ld32(c, ga + 8);
			pthread_mutex_lock(&g_lock);
			b = ion_find_handle(handle);
			if (b != NULL)
				bytes = b->bytes;
			pthread_mutex_unlock(&g_lock);
			if (b != NULL && n2shim_hw != NULL && n2vc_dma_lookup_handle && n2vc_dma_sync &&
			    n2vc_dma_lookup_handle(handle, NULL, &gbase, &gbytes) == 0 && gbase != 0) {
				uint32_t off = 0, len = bytes;

				if (type < 6 && va >= gbase && va - gbase < bytes) {   /* by range */
					off = va - gbase;
					len = size < bytes - off ? size : bytes - off;
				}
				if (len)
					(void)n2vc_dma_sync(n2shim_hw, c->mem, gbase, off, len,
							    (type == 1 || type == 4 || type == 7) ? 0 : 1);
			}
			n2shim_ret(c, 0);
			return;
		}
		/* everything else needs no work here */
		n2shim_ret(c, 0);
		return;
	case 1:   /* ION_CMD_MULTIMEDIA */
	case 2:   /* ION_CMD_MULTIMEDIA_SEC */
		if (sub == 8 || sub == 9) {   /* ION_MM_GET_IOVA(_EXT) */
			handle = n2a32_ld32(c, ga + 8);
			pthread_mutex_lock(&g_lock);
			b = ion_find_handle(handle);
			if (b != NULL) {
				pa = b->pa;
				have = 1;
			}
			pthread_mutex_unlock(&g_lock);
			if (!have) {
				n2shim_ret(c, (uint32_t)-EINVAL);
				return;
			}
			n2a32_st32(c, ga + 32, pa);      /* get_phys_param.phy_addr, 64-bit: low word */
			n2a32_st32(c, ga + 36, 0);       /*                                   high word */
			n2shim_ret(c, 0);
			return;
		}
		/* ION_MM_CONFIG_BUFFER (0) / _EXT (5): the address is not cached and no secure world exists, so the
		 * request has no work; the subsequent GET_IOVA supplies the address the vendor needs */
		n2shim_ret(c, 0);
		return;
	default:
		n2shim_ret(c, (uint32_t)-EINVAL);
		return;
	}
}

/* Integrator hook: make a share fd resolvable that this file did not create (the AP side may allocate a buffer with
 * n2vc_hw directly and hand the daemon an fd). Returns 0, or -ENOMEM when the table is full / -EINVAL. */
int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa, uint32_t bytes)
{
	struct n2and_ion_buf *b;
	int rc = 0;

	if (share_id <= 0 || dma_handle == 0 || bytes == 0)
		return -EINVAL;
	pthread_mutex_lock(&g_lock);
	if (ion_find_share((uint32_t)share_id) != NULL) {
		pthread_mutex_unlock(&g_lock);
		return -EINVAL;
	}
	b = ion_find_handle(dma_handle);
	if (b == NULL) {
		b = ion_alloc_slot();
		if (b == NULL) {
			pthread_mutex_unlock(&g_lock);
			return -ENOMEM;
		}
		memset(b, 0, sizeof *b);
		b->used = 1;
		b->handle = dma_handle;
		b->pa = pa;
		b->bytes = bytes;
	}
	b->shared = 1;
	b->foreign = 1;
	b->share_id = share_id;
	if (share_id >= g_ion_next_share)
		g_ion_next_share = share_id + 1;
	pthread_mutex_unlock(&g_lock);
	return rc;
}

/* opus-decint: the inverse of n2shim_ion_register_share() for a FOREIGN share (the AP's buffer): frees its slot so the
 * 64-entry table does not fill up across decoder sessions (each session publishes dpb+2 client buffers; vpud keeps ~34
 * slots of its own).  Call only after the vendor session that imported it is gone (DEINIT).  0, or -ENOENT when the id
 * is not a foreign share.  Additive: nothing else in this file changes behaviour. */
int n2shim_ion_unregister_share(int share_id)
{
	struct n2and_ion_buf *b;

	if (share_id <= 0)
		return -ENOENT;
	pthread_mutex_lock(&g_lock);
	b = ion_find_share((uint32_t)share_id);
	if (b == NULL || !b->foreign) {
		pthread_mutex_unlock(&g_lock);
		return -ENOENT;
	}
	memset(b, 0, sizeof *b);
	pthread_mutex_unlock(&g_lock);
	return 0;
}

/* Test hooks: forget every ion buffer and fd (each test starts from a clean table). */
void n2shim_ion_reset(void)
{
	pthread_mutex_lock(&g_lock);
	memset(g_ion_buf, 0, sizeof g_ion_buf);
	memset(g_ion_fd, 0, sizeof g_ion_fd);
	g_ion_next_share = N2AND_ION_SHARE0;
	pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------------------------------------------------------
 * dlopen / dlsym / dlclose / dlerror / dladdr, restricted to the loaded n2elf set
 *
 * What the vendor asks for (call sites in STATUS.md): "AVCSecureVdecCA.so", "HEVCSecureVdecCA.so" (the secure
 * decoder CA libraries, which do not exist for us -- libvpud_vcodec.so handles a NULL dlopen) and
 * "/vendor/lib/libmtcloader.so" (thermal policy, libvcodec_utility.so 0x19c98; also handled as NULL). No vendor
 * call site dlopens an object of the loaded set, but the general contract is implemented and tested:
 *   - dlopen(NULL) returns the global handle (1), whose dlsym is the whole set;
 *   - dlopen(name) returns a per-object handle when the object is loaded. "Loaded" means the integrator called
 *     n2shim_dl_note_loaded(soname), or, absent that, a representative defined symbol of that object resolves in
 *     the set (a small table below). The five sonames of PLAN 1.2 are the only objects that exist here.
 *   - dlsym(handle, name) is n2elf_sym(n2shim_set, name) for any valid handle; the set includes the shim tables,
 *     so "malloc" resolves to the heap shim as it would through the Android linker's global scope.
 *   - dlclose() returns 0 and unloads nothing: the loader cannot unload (V03) and the vendor never relies on it.
 *   - dlerror() returns the last error text in GUEST memory and clears it (standard semantics).
 *   - dladdr(addr, Dl_info) fills dli_fname / dli_sname from n2elf_describe() (guest copies), dli_saddr from
 *     n2elf_sym() by symbol name, and leaves dli_fbase 0 (the frozen loader API has no address -> image query).
 *     Returns 0 when the address is in no loaded object.
 * ---------------------------------------------------------------------------------------------------------------- */

#define N2AND_DL_GLOBAL 1u
#define N2AND_DL_HANDLE0 0x100u   /* handles of objects registered by the integrator: 0x100 .. 0x107 */
#define N2AND_DL_KNOWN0  0x180u   /* handles of objects found by the representative-symbol probe: 0x180 .. 0x184 */
#define N2AND_DL_MAX 8

struct n2and_dl_obj {
	char soname[64];
	char probe[48];      /* representative defined symbol, "" = registry only */
};

/* One entry per object of the vendor set. The probe symbols are defined (not imported) in the named object; a
 * symbol lookup that resolves proves the object is in the set. bin/vpud is stripped, so it has no probe. */
static const struct n2and_dl_obj g_dl_known[] = {
	{ "libvcodec_utility.so", "VCodecConfigMCIPort" },
	{ "libvcodecdrv.so",      "H264_HAL_SetPRefPicListReg" },
	{ "libvpud_vcodec.so",    "venc_codec_service_init" },
	{ "libvcodec_oal.so",     "VCodecInitHWLock" },
	{ "vpud",                 "" },
};

static char g_dl_reg[N2AND_DL_MAX][64];
static unsigned g_dl_nreg;

static char g_dl_error[160];
static uint32_t g_dl_error_guest;
static uint32_t g_dl_fname_guest, g_dl_sname_guest;

static void dl_set_error(const char *what, const char *name)
{
	size_t n = 0;

	pthread_mutex_lock(&g_lock);
	while (*what && n + 1 < sizeof g_dl_error)
		g_dl_error[n++] = *what++;
	if (name != NULL) {
		while (*name && n + 1 < sizeof g_dl_error)
			g_dl_error[n++] = *name++;
	}
	g_dl_error[n] = '\0';
	pthread_mutex_unlock(&g_lock);
}

int n2shim_dl_note_loaded(const char *soname)
{
	size_t i;
	const char *base;

	if (soname == NULL || soname[0] == '\0')
		return -EINVAL;
	base = strrchr(soname, '/');
	soname = base != NULL ? base + 1 : soname;
	pthread_mutex_lock(&g_lock);
	for (i = 0; i < g_dl_nreg; i++) {
		if (strcmp(g_dl_reg[i], soname) == 0) {
			pthread_mutex_unlock(&g_lock);
			return 0;
		}
	}
	if (g_dl_nreg == N2AND_DL_MAX) {
		pthread_mutex_unlock(&g_lock);
		return -ENOMEM;
	}
	strncpy(g_dl_reg[g_dl_nreg], soname, sizeof g_dl_reg[0] - 1);
	g_dl_reg[g_dl_nreg][sizeof g_dl_reg[0] - 1] = '\0';
	g_dl_nreg++;
	pthread_mutex_unlock(&g_lock);
	return 0;
}

/* Returns the handle for `name` (basename compared, so "/vendor/lib/libx.so" and "libx.so" agree) or 0. */
static uint32_t dl_handle_for(const char *name)
{
	const char *base = strrchr(name, '/');
	unsigned i;

	base = base ? base + 1 : name;
	pthread_mutex_lock(&g_lock);
	for (i = 0; i < g_dl_nreg; i++) {
		if (strcmp(g_dl_reg[i], base) == 0) {
			pthread_mutex_unlock(&g_lock);
			return N2AND_DL_HANDLE0 + i;
		}
	}
	pthread_mutex_unlock(&g_lock);
	for (i = 0; i < sizeof g_dl_known / sizeof g_dl_known[0]; i++) {
		if (strcmp(g_dl_known[i].soname, base) != 0)
			continue;
		if (g_dl_known[i].probe[0] == '\0')
			return 0;
		if (n2shim_set != NULL && n2elf_sym(n2shim_set, g_dl_known[i].probe) != 0)
			return N2AND_DL_KNOWN0 + i;
		return 0;
	}
	return 0;
}

static void sh_dlopen(struct n2a32_cpu *c)
{
	char name[N2AND_DL_NAME];
	uint32_t gname = n2shim_arg(c, 0);

	if (gname == 0) {
		n2shim_ret(c, N2AND_DL_GLOBAL);
		return;
	}
	(void)guest_str(c, gname, name, sizeof name);
	{
		uint32_t h = dl_handle_for(name);

		if (h != 0) {
			n2shim_ret(c, h);
			return;
		}
	}
	dl_set_error("dlopen: not part of the loaded vendor set: ", name);
	n2shim_ret(c, 0);
}

static void sh_dlsym(struct n2a32_cpu *c)
{
	uint32_t handle = n2shim_arg(c, 0);
	char name[N2AND_DL_NAME];
	uint32_t addr;

	if (n2shim_arg(c, 1) == 0) {
		dl_set_error("dlsym: NULL symbol name", NULL);
		n2shim_ret(c, 0);
		return;
	}
	(void)guest_str(c, n2shim_arg(c, 1), name, sizeof name);
	if (handle == 0) {
		dl_set_error("dlsym: invalid handle", NULL);
		n2shim_ret(c, 0);
		return;
	}
	{
		/* only a handle this file can have returned is accepted: the global one, a registered object, or a
		 * probed one. The search is the same for all of them, but a garbage handle must not look valid. */
		unsigned nreg, nknown = (unsigned)(sizeof g_dl_known / sizeof g_dl_known[0]);
		int valid;

		pthread_mutex_lock(&g_lock);
		nreg = g_dl_nreg;
		pthread_mutex_unlock(&g_lock);
		if (handle == N2AND_DL_GLOBAL)
			valid = 1;
		else if (handle >= N2AND_DL_HANDLE0 && handle < N2AND_DL_HANDLE0 + N2AND_DL_MAX)
			valid = handle < N2AND_DL_HANDLE0 + nreg;
		else if (handle >= N2AND_DL_KNOWN0 && handle < N2AND_DL_KNOWN0 + nknown)
			valid = 1;
		else
			valid = 0;
		if (!valid) {
			dl_set_error("dlsym: invalid handle", NULL);
			n2shim_ret(c, 0);
			return;
		}
	}
	addr = n2shim_set != NULL ? n2elf_sym(n2shim_set, name) : 0;
	if (addr == 0) {
		dl_set_error("dlsym: symbol not in the loaded vendor set: ", name);
		n2shim_ret(c, 0);
		return;
	}
	n2shim_ret(c, addr);
}

static void sh_dlclose(struct n2a32_cpu *c)
{
	(void)c;   /* nothing can be unloaded; returning 0 is what a successful dlclose looks like */
	n2shim_ret(c, 0);
}

static void sh_dlerror(struct n2a32_cpu *c)
{
	uint32_t ga;

	pthread_mutex_lock(&g_lock);
	if (g_dl_error[0] == '\0') {
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	ga = guest_string_slot(c, &g_dl_error_guest, g_dl_error, sizeof g_dl_error);
	g_dl_error[0] = '\0';
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, ga);
}

/* Fill the guest Dl_info { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; }.
 * The text comes from n2elf_describe(): "soname:symbol+0xoff", "soname+0xoff", "hostcall:name" or "0x...". */
static void sh_dladdr(struct n2a32_cpu *c)
{
	uint32_t addr = n2shim_arg(c, 0);
	uint32_t ginfo = n2shim_arg(c, 1);
	char desc[160];
	const char *d;
	uint32_t gname = 0, gsym = 0, saddr = addr;
	char fname[96], sname[96];

	if (ginfo == 0) {
		n2shim_ret(c, 0);
		return;
	}
	if (n2shim_set == NULL) {
		n2shim_ret(c, 0);
		return;
	}
	/* n2elf_describe() compares the dynsym st_value (which carries the Thumb bit on every function of this set)
	 * against an even link-time offset, so asking it about the exact address of a Thumb function returns the
	 * PREVIOUS symbol. Asking one byte later makes its comparison succeed for the containing symbol; the symbol
	 * start below is then taken from n2elf_sym() by name, so the +1 never leaks into dli_saddr. Reported to
	 * hw-v03/hw-lead as a loader finding. */
	d = n2elf_describe(n2shim_set, addr + 1, desc, sizeof desc);
	if (d == NULL || d[0] == '\0') {
		n2shim_ret(c, 0);
		return;
	}
	/* split "name:sym+0xN" / "name+0xN" */
	{
		const char *colon = strchr(desc, ':');
		const char *plus = strrchr(desc, '+');
		size_t flen;

		if (colon != NULL && (plus == NULL || colon < plus)) {
			flen = (size_t)(colon - desc);
			if (flen >= sizeof fname)
				flen = sizeof fname - 1;
			memcpy(fname, desc, flen);
			fname[flen] = '\0';
			if (plus != NULL && plus > colon + 1) {
				size_t slen = (size_t)(plus - (colon + 1));

				if (slen >= sizeof sname)
					slen = sizeof sname - 1;
				memcpy(sname, colon + 1, slen);
				sname[slen] = '\0';
			} else {
				sname[0] = '\0';
			}
			if (plus != NULL) {
				uint32_t start = n2elf_sym(n2shim_set, sname);

				/* exact start from the symbol table, never addr - offset */
				saddr = start != 0 ? (start & ~1u) : (addr & ~1u);
			}
		} else if (plus != NULL && desc[0] != '0') {
			/* "soname+0xN": an object but no symbol */
			size_t flen = (size_t)(plus - desc);

			if (flen >= sizeof fname)
				flen = sizeof fname - 1;
			memcpy(fname, desc, flen);
			fname[flen] = '\0';
			sname[0] = '\0';
			saddr = addr & ~1u;
		} else {
			/* "0x..." (no object) or "hostcall:name": the text is the file name, no symbol */
			size_t flen = strlen(desc);

			if (flen >= sizeof fname)
				flen = sizeof fname - 1;
			memcpy(fname, desc, flen);
			fname[flen] = '\0';
			sname[0] = '\0';
			if (desc[0] == '0') {
				n2shim_ret(c, 0);   /* not in any loaded object */
				return;
			}
		}
	}
	gname = guest_string_slot(c, &g_dl_fname_guest, fname, sizeof fname);
	if (sname[0] != '\0')
		gsym = guest_string_slot(c, &g_dl_sname_guest, sname, sizeof sname);
	n2a32_st32(c, ginfo + 0, gname);
	n2a32_st32(c, ginfo + 4, 0);              /* dli_fbase: not available through the frozen loader API */
	n2a32_st32(c, ginfo + 8, gsym);
	n2a32_st32(c, ginfo + 12, gsym != 0 ? saddr : addr);
	n2shim_ret(c, gname != 0 ? 1 : 0);
}

/* ------------------------------------------------------------------------------------------------------------------
 * C++ / CRT: __cxa_atexit, __cxa_finalize, __register_atfork, __libc_init, unwinder helpers
 * ---------------------------------------------------------------------------------------------------------------- */

#define N2AND_ATEXIT_MAX 32

struct n2and_atexit {
	uint32_t fn;
	uint32_t arg;
	uint32_t dso;
	uint32_t seq;          /* registration order: __cxa_finalize runs the newest first */
};

static struct n2and_atexit g_atexit[N2AND_ATEXIT_MAX];
static uint32_t g_atexit_seq;

/* int __cxa_atexit(void (*fn)(void *), void *arg, void *dso_handle) */
static void sh_cxa_atexit(struct n2a32_cpu *c)
{
	uint32_t fn = n2shim_arg(c, 0);
	uint32_t arg = n2shim_arg(c, 1);
	uint32_t dso = n2shim_arg(c, 2);
	int rc = 0;

	if (fn == 0) {
		n2shim_ret(c, (uint32_t)-EINVAL);
		return;
	}
	pthread_mutex_lock(&g_lock);
	{
		unsigned i, slot = N2AND_ATEXIT_MAX;

		for (i = 0; i < N2AND_ATEXIT_MAX; i++) {
			if (g_atexit[i].fn == 0) {
				slot = i;
				break;
			}
		}
		if (slot == N2AND_ATEXIT_MAX) {
			rc = -ENOMEM;   /* the vendor ignores this; the handler simply does not run */
		} else {
			g_atexit[slot].fn = fn;
			g_atexit[slot].arg = arg;
			g_atexit[slot].dso = dso;
			g_atexit[slot].seq = ++g_atexit_seq;
		}
	}
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, (uint32_t)rc);
}

/* Run the registered handlers newest-first. dso == 0 runs every remaining handler (the itanium ABI's
 * __cxa_finalize(NULL)); otherwise only the handlers whose dso_handle matches. Stops at the first guest stop
 * (fault/undef/noimport) so a broken handler cannot corrupt the rest of the shutdown. */
void n2shim_run_atexit(struct n2a32_cpu *c, uint32_t dso)
{
	for (;;) {
		uint32_t fn = 0, arg = 0, best = 0;
		unsigned i, slot = N2AND_ATEXIT_MAX;

		/* newest matching handler first (LIFO, like __cxa_finalize) */
		pthread_mutex_lock(&g_lock);
		for (i = 0; i < N2AND_ATEXIT_MAX; i++) {
			if (g_atexit[i].fn == 0)
				continue;
			if (dso != 0 && g_atexit[i].dso != dso)
				continue;
			if (slot == N2AND_ATEXIT_MAX || g_atexit[i].seq > best) {
				best = g_atexit[i].seq;
				slot = i;
			}
		}
		if (slot != N2AND_ATEXIT_MAX) {
			fn = g_atexit[slot].fn;
			arg = g_atexit[slot].arg;
			g_atexit[slot].fn = 0;   /* one-shot, like __cxa_finalize */
			g_atexit[slot].arg = 0;
			g_atexit[slot].dso = 0;
		}
		pthread_mutex_unlock(&g_lock);
		if (slot == N2AND_ATEXIT_MAX)
			return;
		{
			uint32_t a[1];

			a[0] = arg;
			(void)n2a32_call(c, fn, a, 1, NULL);
		}
		if (c->stop != N2A32_STOP_NONE && c->stop != N2A32_STOP_RETURN)
			return;   /* a broken handler stops the shutdown; c->stop says why */
	}
}

/* void __cxa_finalize(void *dso_handle) */
static void sh_cxa_finalize(struct n2a32_cpu *c)
{
	uint32_t dso = n2shim_arg(c, 0);

	n2shim_run_atexit(c, dso);
	n2shim_ret(c, 0);
}

/* int __register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void), void *dso_handle)
 * The host never forks the guest (a guest fork would need a second address space and is not used by the codec), so
 * the handlers are accepted and dropped. */
static void sh_register_atfork(struct n2a32_cpu *c)
{
	n2shim_ret(c, 0);
}

/* void __libc_init(void *raw_args, void (*onexit)(void), int (*main)(int, char **, char **), void *structors)
 *
 * Old-bionic signature: vpud's entry point (0x278c) calls it with r0 = its original stack pointer, r1 = NULL,
 * r2 = main (loaded from a relocation slot), r3 = an array of three pointers to .preinit_array/.init_array/
 * .fini_array built on the stack. We
 *   - take argc/argv/envp from the runner's globals when n2shim_libc_argv is set (the normal case: IV builds a
 *     guest argv/envp), otherwise parse raw_args exactly as bionic does (argc at [raw_args], argv = raw_args + 4,
 *     envp = argv + 4 * (argc + 1));
 *   - call onexit() if one was given;
 *   - call guest main(argc, argv, envp) with n2a32_call (AAPCS, Thumb bit honoured);
 *   - run the __cxa_atexit handlers (bionic's exit() does that);
 *   - record main's return value in n2shim_libc_status and set c->stop_request, because bionic's _start would not
 *     return either. The `structors` array is NOT traversed: the loader already ran preinit/init arrays through
 *     n2elf_run_init() and running constructors twice would be wrong.
 * A main that faults leaves c->stop describing the fault and does not set stop_request; the runner reports it. */
static void sh_libc_init(struct n2a32_cpu *c)
{
	uint32_t raw_args = n2shim_arg(c, 0);
	uint32_t onexit = n2shim_arg(c, 1);
	uint32_t main_fn = n2shim_arg(c, 2);
	uint32_t argc, argv, envp;

	(void)n2shim_arg(c, 3);   /* structors: see above */
	if (n2shim_libc_argv != 0) {
		argc = (uint32_t)(n2shim_libc_argc < 0 ? 0 : n2shim_libc_argc);
		argv = n2shim_libc_argv;
		envp = n2shim_libc_envp;
	} else if (raw_args != 0) {
		argc = n2a32_ld32(c, raw_args);
		argv = raw_args + 4;
		envp = argv + 4 * (argc + 1);
	} else {
		argc = 0;
		argv = 0;
		envp = 0;
	}
	if (onexit != 0)
		(void)n2a32_call(c, onexit, NULL, 0, NULL);
	if (main_fn == 0) {
		n2shim_libc_status = -1;
		n2shim_ret(c, 0);
		return;
	}
	{
		uint32_t a[3];
		uint32_t r;

		a[0] = argc;
		a[1] = argv;
		a[2] = envp;
		r = n2a32_call(c, main_fn, a, 3, NULL);
		if (c->stop != N2A32_STOP_NONE && c->stop != N2A32_STOP_RETURN) {
			n2shim_libc_status = -1;   /* fault/undef/noimport: c->stop says which */
			n2shim_ret(c, 0);
			return;
		}
		n2shim_libc_status = (int)r;
		n2shim_run_atexit(c, 0);
		c->stop_request = 1;             /* main returned: the guest process would exit here */
		n2shim_ret(c, r);
	}
}

/* void *__gnu_Unwind_Find_exidx(void *pc, int *count): no exception tables for a hosted object (the vendor's
 * .ARM.exidx is only used by the unwinder, and __aeabi_unwind_cpp_pr0 below is the personality that never runs). */
static void sh_gnu_unwind_find_exidx(struct n2a32_cpu *c)
{
	uint32_t gpcount = n2shim_arg(c, 1);

	if (gpcount != 0)
		n2a32_st32(c, gpcount, 0);
	n2shim_ret(c, 0);
}

/* void __aeabi_unwind_cpp_pr0(void): a no-op personality routine. It has no dynamic relocation (FACTS 3.4), so
 * this entry exists for completeness and for a future object that does reference it. */
static void sh_aeabi_unwind_cpp_pr0(struct n2a32_cpu *c)
{
	(void)c;
}

/* ------------------------------------------------------------------------------------------------------------------
 * math: log, exp2 (double, soft-float ABI: argument in r0:r1, result in r0:r1)
 *
 * Guest evidence: libvcodecdrv.so 0x11029a (exp2) and 0x110caa (log), both inside the H.264 slice-header POC
 * computation: vcvt.f64.u32 d16,s0; vmov r0,r1,d16; blx exp2; vmov d16,r0,r1; vcvt.s32.f64 s0,d16.
 * The host's IEEE-754 double log()/exp2() are used (both hosts have libm). Denormals, +-inf and NaN follow IEEE
 * 754. The host libm rounds in the HOST's current mode (round-to-nearest); the guest's FPSCR is neither read nor
 * modified, so a guest that changed FPSCR.RMode before the call could in principle disagree in the last bit.
 * No vendor path changes FPSCR.RMode (the only users are the two H.264 POC computations above).
 * ---------------------------------------------------------------------------------------------------------------- */

static void sh_math_log(struct n2a32_cpu *c)
{
	st_double(c, log(ld_double(c)));
}

static void sh_math_exp2(struct n2a32_cpu *c)
{
	st_double(c, exp2(ld_double(c)));
}

/* ------------------------------------------------------------------------------------------------------------------
 * The tables
 * ---------------------------------------------------------------------------------------------------------------- */

#define E(name, fn) { name, fn }

const struct n2shim_entry n2shim_android[] = {
	/* D01 census: facts/imports.py --table android (35 imports) */
	E("__aeabi_unwind_cpp_pr0", sh_aeabi_unwind_cpp_pr0),
	E("__android_log_print", sh_android_log_print),
	E("__android_log_vprint", sh_android_log_vprint),
	E("__cxa_atexit", sh_cxa_atexit),
	E("__cxa_finalize", sh_cxa_finalize),
	E("__gnu_Unwind_Find_exidx", sh_gnu_unwind_find_exidx),
	E("__libc_init", sh_libc_init),
	E("__register_atfork", sh_register_atfork),
	E("atrace_begin_body", sh_atrace_body),
	E("atrace_end_body", sh_atrace_body),
	E("atrace_get_enabled_tags", sh_atrace_get_enabled_tags),
	E("atrace_int_body", sh_atrace_body),
	E("dladdr", sh_dladdr),
	E("dlclose", sh_dlclose),
	E("dlerror", sh_dlerror),
	E("dlopen", sh_dlopen),
	E("dlsym", sh_dlsym),
	E("exp2", sh_math_exp2),
	E("ion_alloc", sh_ion_alloc),
	E("ion_alloc_camera", sh_ion_alloc_camera),
	E("ion_custom_ioctl", sh_ion_custom_ioctl),
	E("ion_free", sh_ion_free),
	E("ion_import", sh_ion_import),
	E("ion_mmap", sh_ion_mmap),
	E("ion_munmap", sh_ion_munmap),
	E("ion_share", sh_ion_share),
	E("ion_share_close", sh_ion_share_close),
	E("log", sh_math_log),
	E("mt_ion_close", sh_mt_ion_close),
	E("mt_ion_open", sh_mt_ion_open),
	E("perf_lock_acq", sh_perf_lock_acq),
	E("perf_lock_rel", sh_perf_lock_rel),
	E("property_get", sh_property_get),
	E("sec_drv_base_check", sh_sec_drv_base_check),
	E("sec_drv_info_update", sh_sec_drv_info_update),
	{ NULL, NULL },
};

/* Data imports. __stack_chk_guard is a fixed 32-bit canary ("N2V4"); stderr is the 4-byte cell holding the fake
 * FILE* value 2 that V33's stdio table recognises (guest FILE* objects are small integers there). */
static const uint32_t g_stack_chk_guard = 0x4e325634u;   /* "N2V4" little-endian */
static const uint32_t g_stderr_cell = 2u;

const struct n2shim_data n2shim_data_all[] = {
	{ "__stack_chk_guard", 4, &g_stack_chk_guard },
	{ "stderr", 4, &g_stderr_cell },
	{ NULL, 0, NULL },
};

/* ------------------------------------------------------------------------------------------------------------------
 * Weak fallbacks for the DMA-mirror functions of include/n2vc_hw.h (V41 owns the real ones in ap/n2vc_dma.c).
 *
 * V34 must be linkable on its own so its host tests do not need V41; the shims above only call these two. When V41
 * is linked, its strong definitions replace these and the ion path works for real. Without V41 the ion mmap fails
 * with -ENOSYS (and the runner sees the same "no memory" path the vendor handles), instead of a link error that
 * would take down the whole host runner. Documented in STATUS.md and posted to hw-v41/hw-iv.
 * ---------------------------------------------------------------------------------------------------------------- */

__attribute__((weak)) int n2vc_dma_guest_map(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle,
                                             uint32_t bytes, uint32_t *guest_addr)
{
	(void)h; (void)m; (void)handle; (void)bytes; (void)guest_addr;
	return -ENOSYS;
}

__attribute__((weak)) int n2vc_dma_guest_unmap(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr)
{
	(void)h; (void)m; (void)guest_addr;
	return -ENOSYS;
}
