/* SPDX-License-Identifier: MIT */
/*
 * n2vhost.c -- track V host runner: bring the phone's own codec stack up under the AArch32 interpreter.
 *
 * This file is both the library behind gpu/hwcodec/host/n2vhost.h (used by the daemon, package IP) and,
 * when compiled without -DN2VHOST_NO_MAIN, the command-line runner that gpu/hwcodec/host/run_vhost.sh
 * builds and runs.  It owns no hardware and no phone access: the hardware is struct n2vc_hw.
 *
 * Sequence (each step logged as a "N2VHOST ..." line, milestones M-a..M-f as in n2vhost.h):
 *   mem + /dev/vcu model -> shim tables -> load 5 objects -> link -> unresolved list ->
 *   init arrays (M-b) -> vpud guest main on a guest thread -> AP drive in this thread -> report.
 *
 * The set is read from the extracted copy of the phone's own /vendor at run time; nothing is copied
 * into the tree and no vendor byte is printed.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n2vhost.h"
#include "n2a32_trace.h"        /* T01: n2a32_trace_attach/set_elf/dump (tests/emu, read only) */

/* V40 and V41 are opt-in: the Makefile defines these only when the real header *and* source are present,
 * so this file never guesses another package's prototypes. */
#if defined(N2VHOST_HAVE_VCU)
#include "n2vcu.h"
#endif
#if defined(N2VHOST_HAVE_AP)
#include "n2ap_venc.h"
/* Optional V41 extensions proposed on the board (IV -> V41, 11 November): the daemon's ring buffer
 * imports the frame/bitstream dma-buf fds, and the result arrives asynchronously through PUT_BUFFER.
 * The runner uses them when V41 provides them and stays honest about the gap when it does not. */
extern int n2ap_venc_encode_fd(struct n2ap_venc *e, uint32_t frame_handle, int frame_fd,
	uint32_t bs_handle, int bs_fd, unsigned flags, struct n2ap_enc_result *res)
	__attribute__((weak));
extern int n2ap_venc_wait_done(struct n2ap_venc *e, unsigned timeout_ms,
	struct n2ap_enc_result *res) __attribute__((weak));
extern int n2ap_venc_power(struct n2ap_venc *e, int on) __attribute__((weak));
#endif
/* V34's integrator hook for a buffer the AP allocated itself (shim/n2shim_android.c). */
extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa, uint32_t bytes)
	__attribute__((weak));

/* The shim tables are referenced strongly, not through weak stand-ins (R2 finding F15): an archive
 * consumer that links libn2vhost.a without pulling the table objects would otherwise get empty
 * thread/stdio/android/data tables and a product vcodecd that cannot serve a single guest call.  A
 * tree without one of V32/V33/V34 now fails at link time with the missing table's name, which is the
 * honest outcome.  The runner itself always links every package's source (host/Makefile), so the
 * tables it reports below are the real ones (test_iv checks the counts). */

/* V34's two globals (shim/n2shim_android.c).  They are referenced through pointers to weak symbols so the
 * file still links when V34 is not in the build; the loader tells dlopen what to search and the ion shim
 * what hardware to allocate from.  Both are process-global, like the tables. */
extern struct n2elf_set *n2shim_set __attribute__((weak));
extern const struct n2vc_hw *n2shim_hw __attribute__((weak));
static struct n2elf_set **const n2v_shim_set_slot = &n2shim_set;
static const struct n2vc_hw **const n2v_shim_hw_slot = &n2shim_hw;

/* V33's guest /proc/self/maps (shim/n2vfs.c).  Weak: a tree without it simply does not get the file. */
extern int n2vfs_set_maps(const char *text, uint32_t len) __attribute__((weak));

/* The two data imports of the set (FACTS 3.3).  Registered after n2shim_data_all and only for names
 * that table does not provide, so V34 stays the owner when it is in the build. */
static const unsigned char n2v_guard_init[4] = { 0x00, 0x00, 0x00, 0x00 };
static const struct n2shim_data n2v_data_local[] = {
	{ "__stack_chk_guard", 4, n2v_guard_init },
	{ "stderr", 4, NULL },
	{ NULL, 0, NULL }
};

#define N2V_MAX_OBJ 8
#define N2V_MAX_TABLES 8
#define N2V_VENDOR_MAX 512
#define N2V_NAME_MAX 96

struct n2v_table {
	const char *name;               /* "string", "heap", ... */
	const void *tab;                /* the array */
	int is_data;
	unsigned count;
	const char *owner;              /* package id, for the report */
};

struct n2vhost {
	struct n2a32_mem *mem;
	struct n2elf_set *set;
	struct n2elf_image *img[N2V_MAX_OBJ];
	unsigned nimg;
	char vendor_dir[N2V_VENDOR_MAX];
	char err[192];

	const struct n2vc_hw *hw_real;
	struct n2vc_hw hw;              /* callbacks wrapped, ctx = this */
	struct n2vcu *vcu;

	struct n2a32_cpu init_cpu;
	struct n2a32_cpu guest_cpu;
	uint32_t stack_base, stack_top, raw_args, entry;
	uint64_t ilimit;

	unsigned unresolved;
	struct n2v_table tab[N2V_MAX_TABLES];
	unsigned ntab;
	int init_ran;
	int maps_bytes;                 /* guest /proc/self/maps text handed to V33, 0 = not set */
	char maps_note[96];
	int trace_on;                   /* N2HW_TRACE was set when the guest started */

	/* guest thread */
	pthread_t th;
	int th_started;
	int done;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	int stopped;
	enum n2a32_stop stop;
	uint32_t stop_addr, stop_insn;
	uint64_t stop_icount;

	/* hardware traffic seen through the wrapper */
	unsigned long regs_calls, venc_ops, venc_first_op;
	uint32_t venc_first_off, venc_first_val;
	int venc_first_seen;
	unsigned long irq_waits, venc_irq_waits;
	unsigned long power_on_venc;
	unsigned unresolved_calls;

	int dump_shims;
	/* milestones */
	unsigned reached;               /* highest n2vhost_ms proven */
	char why[N2VHOST_MS_F + 1][200];
	char first_unmet[200];
	char next_unmet[200];           /* first failure after the reached milestone (recorded, printed) */
};

/* =====================================================================================================
 * small utilities
 * ===================================================================================================== */

static void seterr(struct n2vhost *h, const char *fmt, ...)
{
	va_list ap;

	if (!h)
		return;
	va_start(ap, fmt);
	vsnprintf(h->err, sizeof h->err, fmt, ap);
	va_end(ap);
}

static const char *stop_name(enum n2a32_stop s)
{
	switch (s) {
	case N2A32_STOP_NONE: return "NONE";
	case N2A32_STOP_RETURN: return "RETURN";
	case N2A32_STOP_UNDEF: return "UNDEF";
	case N2A32_STOP_FAULT: return "FAULT";
	case N2A32_STOP_SVC: return "SVC";
	case N2A32_STOP_BKPT: return "BKPT";
	case N2A32_STOP_LIMIT: return "LIMIT";
	case N2A32_STOP_REQUEST: return "REQUEST";
	case N2A32_STOP_NOIMPORT: return "NOIMPORT";
	}
	return "?";
}

static void milestone(struct n2vhost *h, unsigned ms, const char *fmt, ...)
{
	va_list ap;

	if (ms > N2VHOST_MS_F)
		return;
	if (h->why[ms][0] == '\0') {
		va_start(ap, fmt);
		vsnprintf(h->why[ms], sizeof h->why[ms], fmt, ap);
		va_end(ap);
	}
	if (ms > h->reached)
		h->reached = ms;
}

/* =====================================================================================================
 * hardware back end wrapper: it forwards every call unchanged and records the evidence for M-e and M-f
 * ===================================================================================================== */

static int w_power(void *ctx, unsigned block, int on)
{
	struct n2vhost *h = ctx;

	if (block == RWVC_BLK_VENC && on > 0)
		h->power_on_venc++;
	return h->hw_real->power(h->hw_real->ctx, block, on);
}

static int w_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct n2vhost *h = ctx;
	int rc = h->hw_real->regs(h->hw_real->ctx, ops, count, done);
	unsigned n, i;

	h->regs_calls++;
	n = done ? *done : (rc ? 0u : count);
	/* A back end that stops at a failing op reports it in *done; that op *was* attempted (the fake
	 * records "W VENC 0x00a8 EACCES" and leaves *done = 0).  Count it, so M-e sees the first access
	 * even when the block is not powered yet. */
	if (rc != 0 && done && *done < count)
		n = *done + 1;
	if (n > count)
		n = count;
	for (i = 0; i < n; i++) {
		if (ops[i].window != (uint8_t)RWVC_WIN_VENC || ops[i].op == RWVC_OP_DELAY_US)
			continue;
		h->venc_ops++;
		if (!h->venc_first_seen) {
			h->venc_first_seen = 1;
			h->venc_first_op = ops[i].op;
			h->venc_first_off = ops[i].offset;
			h->venc_first_val = ops[i].op == RWVC_OP_READ ? ops[i].value : ops[i].value;
			milestone(h, N2VHOST_MS_E, "first VENC window access: %s VENC 0x%04x %s0x%08x",
				ops[i].op == RWVC_OP_READ ? "read" : ops[i].op == RWVC_OP_WRITE ? "write" :
				ops[i].op == RWVC_OP_POLL ? "poll" : "write-mask",
				ops[i].offset, ops[i].op == RWVC_OP_WRITE_MASK ? "mask " : "", ops[i].value);
		}
	}
	return rc;
}

static int w_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct n2vhost *h = ctx;

	h->irq_waits++;
	if (irq == (unsigned)RWVC_IRQ_VENC) {
		h->venc_irq_waits++;
		milestone(h, N2VHOST_MS_F, "wait_irq(RWVC_IRQ_VENC, timeout_ms=%u) reached", timeout_ms);
	}
	return h->hw_real->wait_irq(h->hw_real->ctx, irq, timeout_ms, status);
}

static int w_irq_clear(void *ctx, unsigned irq)
{
	struct n2vhost *h = ctx;

	return h->hw_real->irq_clear(h->hw_real->ctx, irq);
}

static int w_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct n2vhost *h = ctx;

	return h->hw_real->dma_alloc(h->hw_real->ctx, bytes, flags, handle, pa);
}

static int w_dma_free(void *ctx, uint32_t handle)
{
	struct n2vhost *h = ctx;

	return h->hw_real->dma_free(h->hw_real->ctx, handle);
}

static int w_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct n2vhost *h = ctx;

	return h->hw_real->dma_rw(h->hw_real->ctx, handle, offset, host, bytes, write);
}

static void w_close(void *ctx)
{
	struct n2vhost *h = ctx;

	if (h->hw_real->close)
		h->hw_real->close(h->hw_real->ctx);
}

/* =====================================================================================================
 * shim tables
 * ===================================================================================================== */

static unsigned entry_count(const struct n2shim_entry *t)
{
	unsigned n = 0;

	while (t && t[n].name)
		n++;
	return n;
}

static unsigned data_count(const struct n2shim_data *t)
{
	unsigned n = 0;

	while (t && t[n].name)
		n++;
	return n;
}

static void add_table(struct n2vhost *h, const char *name, const void *tab, int is_data, unsigned count,
	const char *owner)
{
	if (h->ntab >= N2V_MAX_TABLES)
		return;
	h->tab[h->ntab].name = name;
	h->tab[h->ntab].tab = tab;
	h->tab[h->ntab].is_data = is_data;
	h->tab[h->ntab].count = count;
	h->tab[h->ntab].owner = owner;
	h->ntab++;
}

static int table_has(const struct n2vhost *h, const char *name)
{
	unsigned i, k;

	for (i = 0; i < h->ntab; i++) {
		if (h->tab[i].is_data) {
			const struct n2shim_data *d = h->tab[i].tab;

			for (k = 0; d && d[k].name; k++)
				if (strcmp(d[k].name, name) == 0)
					return 1;
		} else {
			const struct n2shim_entry *e = h->tab[i].tab;

			for (k = 0; e && e[k].name; k++)
				if (strcmp(e[k].name, name) == 0)
					return 1;
		}
	}
	return 0;
}

/* Register the shim tables in the order the package asks for: string, heap, thread, stdio, android, data.
 * Empty tables are registered too (they cost nothing) so that the report can say which package is missing. */
static void register_shims(struct n2vhost *h)
{
	unsigned n;

	n = entry_count(n2shim_string);
	add_table(h, "string", n2shim_string, 0, n, "V30");
	n2elf_set_add_shims(h->set, n2shim_string);

	n = entry_count(n2shim_heap);
	add_table(h, "heap", n2shim_heap, 0, n, "V30");
	n2elf_set_add_shims(h->set, n2shim_heap);

	n = entry_count(n2shim_thread);
	add_table(h, "thread", n2shim_thread, 0, n, "V32");
	n2elf_set_add_shims(h->set, n2shim_thread);

	n = entry_count(n2shim_stdio);
	add_table(h, "stdio", n2shim_stdio, 0, n, "V33");
	n2elf_set_add_shims(h->set, n2shim_stdio);

	n = entry_count(n2shim_android);
	add_table(h, "android", n2shim_android, 0, n, "V34");
	n2elf_set_add_shims(h->set, n2shim_android);

	n = data_count(n2shim_data_all);
	add_table(h, "data", n2shim_data_all, 1, n, "V34");
	n2elf_set_add_data(h->set, n2shim_data_all);
	/* The two data imports of the set (FACTS 3.3).  n2elf_set_add_data() keeps the pointer, so the table
	 * must live as long as the set: n2v_data_local is static.  It is registered after V34's table, which
	 * therefore wins for any name it provides; the loader searches in registration order. */
	add_table(h, "data-local", n2v_data_local, 1, data_count(n2v_data_local), "IV");
	n2elf_set_add_data(h->set, n2v_data_local);
}

/* =====================================================================================================
 * guest stack and vpud start-up frame
 * ===================================================================================================== */

#define N2V_STACK_BYTES (1024u * 1024u)

static int map_stack(struct n2vhost *h)
{
	uint32_t s = n2a32_mem_alloc(h->mem, N2V_STACK_BYTES, 16);

	if (s == 0)
		return -1;
	h->stack_base = s;
	h->stack_top = s + N2V_STACK_BYTES;
	return 0;
}

/* Build the initial stack of a bionic process start: argc, argv[], NULL, envp[], NULL, then the strings.
 * vpud is started by init as "/vendor/bin/vpud -f" (gpu/hwcodec/STATUS.md), and its _start passes
 * r0 = sp to __libc_init as raw_args.  Returns the raw_args pointer or 0. */
static uint32_t build_raw_args(struct n2vhost *h, struct n2a32_cpu *c, const char *const *argv, unsigned argc)
{
	static const char *const def_argv[] = { "/vendor/bin/vpud", "-f" };
	uint32_t str, sp, i;
	size_t off = 0;

	if (!argv) {
		argv = def_argv;
		argc = 2;
	}
	if (argc > 16)
		return 0;
	/* strings at the top of the stack, arguments below them */
	str = (h->stack_top - 0x400) & ~15u;
	sp = (str - 16u - 4u * (argc + 4u)) & ~7u;
	for (i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]) + 1;

		if (off + len > 0x300)
			return 0;
		memcpy(n2a32_ptr(h->mem, str + (uint32_t)off), argv[i], len);
		n2a32_st32(c, sp + 4u + 4u * i, str + (uint32_t)off);
		off += len;
	}
	n2a32_st32(c, sp, argc);
	n2a32_st32(c, sp + 4u + 4u * argc, 0);          /* argv terminator */
	n2a32_st32(c, sp + 4u + 4u * (argc + 1u), 0);   /* envp terminator */
	h->raw_args = sp;
	c->r[13] = sp;
	return sp;
}

/* =====================================================================================================
 * guest thread
 * ===================================================================================================== */

static void *guest_main(void *arg)
{
	struct n2vhost *h = arg;
	struct n2a32_cpu *c = &h->guest_cpu;
	uint32_t a = h->raw_args;

	n2a32_cpu_init(c, h->mem);
	c->ilimit = h->ilimit;
	c->r[13] = h->raw_args;
	/* Attaching T01's trace makes c->trace non-NULL, which disables the interpreter's fast
	 * loop (emu/n2a32_core.c:626) and formats a record per instruction.  The product daemon
	 * (vcodecd -> n2vhost_start) must not pay that: attach only when the runner asked for a
	 * trace file (N2HW_TRACE).  M-c evidence comes from n2vcu_stats() regardless (board:
	 * opus-perf -> IV). */
	if (h->trace_on) {
		n2a32_trace_attach(c);
		if (h->set)
			n2a32_trace_set_elf(h->set);
	}
	(void)n2a32_call(c, h->entry, &a, 1, NULL);

	pthread_mutex_lock(&h->mu);
	h->stopped = 1;
	h->stop = c->stop;
	h->stop_addr = c->stop_addr;
	h->stop_insn = c->stop_insn;
	h->stop_icount = c->icount;
	if (c->stop == N2A32_STOP_NOIMPORT)
		h->unresolved_calls++;
	h->done = 1;
	pthread_cond_broadcast(&h->cv);
	pthread_mutex_unlock(&h->mu);
	return NULL;
}

void n2vhost_note_stop(struct n2vhost *h, enum n2a32_stop stop, uint32_t addr, uint32_t insn,
	uint64_t icount)
{
	if (!h)
		return;
	pthread_mutex_lock(&h->mu);
	h->stopped = 1;
	h->stop = stop;
	h->stop_addr = addr;
	h->stop_insn = insn;
	h->stop_icount = icount;
	if (stop == N2A32_STOP_NOIMPORT)
		h->unresolved_calls++;
	h->done = 1;
	pthread_cond_broadcast(&h->cv);
	pthread_mutex_unlock(&h->mu);
}

/* =====================================================================================================
 * start / stop
 * ===================================================================================================== */

static const char *const object_paths[5][2] = {
	{ "bin/vpud", "vpud" },
	{ "lib/libvpud_vcodec.so", "libvpud_vcodec.so" },
	{ "lib/libvcodecdrv.so", "libvcodecdrv.so" },
	{ "lib/libvcodec_utility.so", "libvcodec_utility.so" },
	{ "lib/libvcodec_oal.so", "libvcodec_oal.so" },
};

/* Check everything that can be checked without touching global state *before* the guest address space,
 * the /dev/vcu model and V33's device table are created: a failed start must leave no registration
 * behind (n2vfs has no unregister, see n2vhost_stop). */
static int vendor_ok(const char *dir, char *err, size_t cap)
{
	unsigned i;

	for (i = 0; i < 5; i++) {
		char path[N2V_VENDOR_MAX + 64];
		FILE *f;

		if (snprintf(path, sizeof path, "%s/%s", dir, object_paths[i][0]) >= (int)sizeof path) {
			snprintf(err, cap, "vendor path too long");
			return -1;
		}
		f = fopen(path, "rb");
		if (!f) {
			snprintf(err, cap, "%s is missing or unreadable", object_paths[i][0]);
			return -1;
		}
		fclose(f);
	}
	return 0;
}

static int load_objects(struct n2vhost *h)
{
	unsigned i;

	for (i = 0; i < 5; i++) {
		char path[N2V_VENDOR_MAX + 64];
		char err[160] = "";
		struct n2elf_image *im;

		if (snprintf(path, sizeof path, "%s/%s", h->vendor_dir, object_paths[i][0]) >= (int)sizeof path) {
			seterr(h, "vendor path too long");
			return -1;
		}
		im = n2elf_load(h->set, path, object_paths[i][1], err);
		if (!im) {
			seterr(h, "load %s: %s", object_paths[i][0], err);
			return -1;
		}
		h->img[h->nimg++] = im;
	}
	return 0;
}

/* ---- the guest /proc/self/maps text ---------------------------------------------------------------
 * bionic reports one page-aligned line per mapping, not one line per PT_LOAD: the linker maps each
 * PT_LOAD at a page-aligned base and, after relocation, mprotects the GNU_RELRO part of a writable
 * segment read-only.  This matters to vpud's remap_shmem(): it takes the FIRST line that is both
 * "rw-p" and "libvpud_vcodec.so", copies that range into the VCU DMEM and then MAP_FIXED-maps the
 * DMEM over the same range, because its codec structures (venc_drv_inst, venc_formats,
 * venc_framesizes) are static tables in that segment.  If the RELRO line is reported "rw-p" (as it
 * was before this change), vpud picks the 0x3f0-byte .data.rel.ro page instead of the real data+bss
 * segment and reports an address outside the DMEM window (M-d stop of 11 November: 0x4010ceb0).
 * Only ELF facts are read here (PT_LOAD, PT_GNU_RELRO); the lines use the loader's real bases. */
static uint16_t le16(const unsigned char *p)
{
	return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#define N2V_PAGE 0x1000u

static unsigned maps_line(char *buf, size_t cap, size_t *off, uint32_t start, uint32_t end,
	const char *perms, const char *path)
{
	int n;

	if (end <= start)
		return 0;
	n = snprintf(buf + *off, cap - *off, "%08x-%08x %s 00000000 00:00 0 /vendor/%s\n",
		start, end, perms, path);
	if (n < 0 || (size_t)n >= cap - *off)
		return -1;
	*off += (size_t)n;
	return 1;
}

static int build_maps(struct n2vhost *h, char *buf, size_t cap)
{
	size_t off = 0;
	unsigned i;

	if (cap < 2)
		return -1;
	buf[0] = 0;
	for (i = 0; i < h->nimg; i++) {
		char path[N2V_VENDOR_MAX + 64];
		unsigned char eh[52], ph[32];
		uint32_t phoff, relro_start = 0, relro_end = 0;
		uint32_t base = n2elf_base(h->img[i]);
		unsigned phentsize, phnum, k;
		FILE *f;

		if (snprintf(path, sizeof path, "%s/%s", h->vendor_dir, object_paths[i][0]) >= (int)sizeof path)
			return -1;
		f = fopen(path, "rb");
		if (!f)
			return -1;
		if (fread(eh, 1, sizeof eh, f) != sizeof eh || memcmp(eh, "\177ELF", 4) != 0) {
			fclose(f);
			return -1;
		}
		phoff = le32(eh + 28);
		phentsize = le16(eh + 42);
		phnum = le16(eh + 44);
		if (phentsize < sizeof ph || phnum > 32) {
			fclose(f);
			return -1;
		}
		/* GNU_RELRO (0x6474e552) first: its page range is read-only at run time. */
		for (k = 0; k < phnum; k++) {
			if (fseek(f, (long)(phoff + k * phentsize), SEEK_SET) != 0 ||
			    fread(ph, 1, sizeof ph, f) != sizeof ph)
				break;
			if (le32(ph + 0) == 0x6474e552u) {
				relro_start = le32(ph + 8) & ~(N2V_PAGE - 1u);
				relro_end = (le32(ph + 8) + le32(ph + 16) + N2V_PAGE - 1u) &
					~(N2V_PAGE - 1u);
				break;
			}
		}
		for (k = 0; k < phnum; k++) {
			uint32_t type, vaddr, memsz, flags, start, end, rs, re;
			char perms[5];

			if (fseek(f, (long)(phoff + k * phentsize), SEEK_SET) != 0 ||
			    fread(ph, 1, sizeof ph, f) != sizeof ph)
				break;
			type = le32(ph + 0);
			if (type != 1u)                       /* PT_LOAD */
				continue;
			vaddr = le32(ph + 8);
			memsz = le32(ph + 20);
			flags = le32(ph + 24);
			if (memsz == 0)
				continue;
			start = base + (vaddr & ~(N2V_PAGE - 1u));
			end = base + (vaddr + memsz + N2V_PAGE - 1u & ~(N2V_PAGE - 1u));
			/* A writable LOAD whose page range overlaps GNU_RELRO is reported as up to three
			 * mappings: writable before, read-only in the RELRO pages, writable after. */
			rs = (flags & 2u) ? (start > base + relro_start ? start : base + relro_start) : end;
			re = (flags & 2u) ? (end < base + relro_end ? end : base + relro_end) : start;
			if (rs >= re) {
				perms[0] = (flags & 4u) ? 'r' : '-';
				perms[1] = (flags & 2u) ? 'w' : '-';
				perms[2] = (flags & 1u) ? 'x' : '-';
				perms[3] = 'p';
				perms[4] = 0;
				if (maps_line(buf, cap, &off, start, end, perms, object_paths[i][0]) < 0)
					return -1;
			} else {
				if (maps_line(buf, cap, &off, start, rs, "rw-p", object_paths[i][0]) < 0 ||
				    maps_line(buf, cap, &off, rs, re, "r--p", object_paths[i][0]) < 0 ||
				    maps_line(buf, cap, &off, re, end, "rw-p", object_paths[i][0]) < 0)
					return -1;
			}
		}
		fclose(f);
	}
	return (int)off;
}

int n2vhost_start(struct n2vhost **out, const struct n2vc_hw *hw, const char *vendor_dir)
{
	return n2vhost_start_entry(out, hw, vendor_dir, getenv("N2VHOST_ENTRY"));
}

int n2vhost_start_entry(struct n2vhost **out, const struct n2vc_hw *hw, const char *vendor_dir,
	const char *entry)
{
	struct n2vhost *h;
	const char *dir;

	if (!out)
		return -1;
	*out = NULL;
	if (!hw || !hw->regs || !hw->power || !hw->wait_irq) {
		fprintf(stderr, "N2VHOST error: hardware back end is NULL or incomplete\n");
		return -1;
	}
	h = calloc(1, sizeof *h);
	if (!h)
		return -1;
	pthread_mutex_init(&h->mu, NULL);
	pthread_cond_init(&h->cv, NULL);

	dir = vendor_dir;
	if (!dir)
		dir = getenv("N2HW_VENDOR_DIR");
	if (!dir)
		dir = "gpu/build/cam-vendor-20261109/vfs";
	if (strlen(dir) >= sizeof h->vendor_dir) {
		seterr(h, "vendor directory path too long");
		goto fail;
	}
	snprintf(h->vendor_dir, sizeof h->vendor_dir, "%s", dir);
	if (vendor_ok(h->vendor_dir, h->err, sizeof h->err) != 0)
		goto fail;

	h->hw_real = hw;
	h->hw = *hw;
	h->hw.ctx = h;
	h->hw.power = w_power;
	h->hw.regs = w_regs;
	h->hw.wait_irq = w_wait_irq;
	h->hw.irq_clear = hw->irq_clear ? w_irq_clear : NULL;
	h->hw.dma_alloc = hw->dma_alloc ? w_dma_alloc : NULL;
	h->hw.dma_free = hw->dma_free ? w_dma_free : NULL;
	h->hw.dma_rw = hw->dma_rw ? w_dma_rw : NULL;
	h->hw.close = hw->close ? w_close : NULL;
	h->ilimit = 200000000u;         /* 200 M instructions: a bound on every guest run */

	h->mem = n2a32_mem_create();
	if (!h->mem) {
		seterr(h, "n2a32_mem_create failed");
		goto fail;
	}
	h->set = n2elf_set_create(h->mem);
	if (!h->set) {
		seterr(h, "n2elf_set_create failed");
		goto fail;
	}
	/* V34's globals: dlopen/dlsym search only this linked set; the ion shim allocates from this back end.
	 * Both are process-global (V34 has no per-set instance), so a second n2vhost_start in one process is
	 * refused below when the /dev/vcu registration already exists. */
	if (n2v_shim_set_slot)
		*n2v_shim_set_slot = h->set;
	if (n2v_shim_hw_slot)
		*n2v_shim_hw_slot = &h->hw;

#if defined(N2VHOST_HAVE_VCU)
	h->vcu = n2vcu_create(h->mem, &h->hw);
	if (!h->vcu) {
		seterr(h, "n2vcu_create failed (the /dev/vcu model is not available)");
		goto fail;
	}
	/* The model owns the device nodes; V33's n2vfs dispatches open/ioctl/mmap by exact path.
	 * libvpud_vcodec opens both: /dev/vcu in the venc handler, /dev/vpu in the vdec one (FACTS 5.1).
	 * n2vfs has no unregister (V33), so a previous vhost in this process that already registered the
	 * two nodes cannot be replaced; say so instead of letting the new model sit behind a dead one. */
	{
		int r1 = n2vfs_register(n2vcu_dev(h->vcu));
		int r2 = n2vfs_register(n2vcu_dev_vpu(h->vcu));

		if (r1 != 0 || r2 != 0) {
			seterr(h, "n2vfs_register(/dev/vcu,/dev/vpu) = %d/%d: one guest set per process (n2vfs has "
				"no unregister; V33)", r1, r2);
			goto fail;
		}
	}
#endif

	register_shims(h);

	if (load_objects(h) != 0)
		goto fail;

	h->unresolved = 0;
	{
		char err[160] = "";

		if (n2elf_link(h->set, &h->unresolved, err) != 0) {
			seterr(h, "link: %s", err);
			goto fail;
		}
	}
	milestone(h, N2VHOST_MS_A, "loaded %u objects and linked: %u unresolved imports, 0 called so far",
		h->nimg, h->unresolved);

	/* Guest /proc/self/maps: vpud reads it before it opens /dev/vcu for the encoder (remap_shmem), and the
	 * text must describe the loader's real bases.  V33 owns the file (n2vfs_set_maps); without that symbol
	 * the file stays ENOENT and vpud exits with "Can not open device model!". */
	if (n2vfs_set_maps) {
		char *maps = calloc(1, 8192);

		if (maps) {
			int n = build_maps(h, maps, 8192);

			if (n > 0 && n2vfs_set_maps(maps, (uint32_t)n) == 0) {
				h->maps_bytes = n;
				snprintf(h->maps_note, sizeof h->maps_note,
					"%d bytes of segment lines from the loader bases", n);
			} else {
				snprintf(h->maps_note, sizeof h->maps_note,
					"could not build or set the text (%d)", n);
			}
			free(maps);
		} else {
			snprintf(h->maps_note, sizeof h->maps_note, "out of memory");
		}
	} else {
		snprintf(h->maps_note, sizeof h->maps_note, "n2vfs_set_maps not in the build (V33)");
	}

	h->trace_on = getenv("N2HW_TRACE") != NULL && getenv("N2HW_TRACE")[0] != '\0';
	if (map_stack(h) != 0) {
		seterr(h, "guest stack allocation failed");
		goto fail;
	}
	n2a32_cpu_init(&h->init_cpu, h->mem);
	h->init_cpu.r[13] = h->stack_top - 16;
	{
		char err[160] = "";

		if (n2elf_run_init(h->set, &h->init_cpu) != 0) {
			seterr(h, "init arrays: %s", err);
			h->stop = h->init_cpu.stop;
			h->stop_addr = h->init_cpu.stop_addr;
			h->stop_insn = h->init_cpu.stop_insn;
			h->stopped = 1;
			h->init_ran = 0;
			*out = h;
			return 1;
		}
	}
	h->init_ran = 1;
	milestone(h, N2VHOST_MS_B, "init arrays ran (DT_PREINIT_ARRAY/DT_INIT/DT_INIT_ARRAY over %u objects)", h->nimg);

	h->entry = n2elf_entry(h->img[0]);
	if (!h->entry) {
		seterr(h, "vpud has no entry point");
		goto fail;
	}
	/* Diagnostic only (runner): apps_init or service-venc starts the vendor service directly instead of
	 * through vpud's _start -> __libc_init.  It answers "what would the guest need next" while a bionic
	 * shim is still missing; the daemon never passes it. */
	{
		const char *e = entry;

		if (e && strcmp(e, "start") == 0)
			e = NULL;
		if (e && strcmp(e, "apps_init") == 0) {
			h->entry = n2elf_sym(h->set, "apps_init");
			if (!h->entry) {
				seterr(h, "N2VHOST_ENTRY=apps_init but libvpud_vcodec.so exports no apps_init");
				goto fail;
			}
		} else if (e && strcmp(e, "service-venc") == 0) {
			uint32_t svc = n2elf_sym(h->set, "_app_venc_service");

			if (!svc) {
				seterr(h, "N2VHOST_ENTRY=service-venc but libvpud_vcodec.so exports no _app_venc_service");
				goto fail;
			}
			h->entry = n2a32_ld32(&h->init_cpu, svc + 8);
			if (!h->entry) {
				seterr(h, "_app_venc_service has no init function at +8");
				goto fail;
			}
		} else if (e && *e) {
			seterr(h, "unknown entry point \"%s\" (start, apps_init, service-venc)", e);
			goto fail;
		}
	}
	if (!build_raw_args(h, &h->init_cpu, NULL, 0)) {
		seterr(h, "could not build the vpud raw_args frame");
		goto fail;
	}
	if (pthread_create(&h->th, NULL, guest_main, h) != 0) {
		seterr(h, "pthread_create for the guest main failed");
		goto fail;
	}
	h->th_started = 1;
	*out = h;
	return 0;

fail:
	*out = h;                       /* the caller stops it and reads the error */
	return -1;
}

const char *n2vhost_error(struct n2vhost *h) { return h ? h->err : "no host"; }

int n2vhost_join(struct n2vhost *h, unsigned timeout_ms)
{
	struct timespec ts;
	int rc = 0;

	if (!h)
		return -1;
	pthread_mutex_lock(&h->mu);
	while (!h->done && rc == 0) {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += timeout_ms / 1000u;
		ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		rc = pthread_cond_timedwait(&h->cv, &h->mu, &ts);
	}
	pthread_mutex_unlock(&h->mu);
	if (rc != 0)
		return -1;
	if (h->th_started) {
		pthread_join(h->th, NULL);
		h->th_started = 0;
	}
	return 0;
}

/* n2vfs (V33) keeps the struct n2vfs_dev pointers it is given for the lifetime of the process and has no
 * unregister, and V34's n2shim_set/n2shim_hw are process globals.  So the /dev/vcu model, the guest
 * address space and the linked set cannot be freed while the process lives: a later guest open() of
 * "/dev/vcu" would follow a dangling pointer.  n2vhost_stop() therefore parks them here (a global root, so
 * LeakSanitizer sees them as still referenced, which is the truth) and the OS reclaims them at exit.  The
 * daemon (package IP) starts once and stops at exit; a second start in the same process is refused by
 * n2vhost_start_entry() with a message naming n2vfs, instead of silently leaving a dead model in the table. */
static struct n2v_parked {
	struct n2a32_mem *mem;
	struct n2elf_set *set;
	struct n2vcu *vcu;
} n2v_parked[N2V_MAX_OBJ];
static unsigned n2v_nparked;
/* A vhost whose guest thread could not be collected (stuck inside a host shim) is kept alive here: the
 * thread is still using its guest_cpu, its cpu pointer and its mutex, so freeing the struct would be a
 * use-after-free in a live thread.  The process is expected to exit; this only makes that safe. */
static struct n2vhost *n2v_parked_host[N2V_MAX_OBJ];
static unsigned n2v_nparked_host;

static void park(struct n2vhost *h)
{
	if (!h->mem && !h->set && !h->vcu)
		return;
	if (n2v_nparked < sizeof n2v_parked / sizeof n2v_parked[0]) {
		n2v_parked[n2v_nparked].mem = h->mem;
		n2v_parked[n2v_nparked].set = h->set;
		n2v_parked[n2v_nparked].vcu = h->vcu;
		n2v_nparked++;
	}
	h->mem = NULL;
	h->set = NULL;
#if defined(N2VHOST_HAVE_VCU)
	h->vcu = NULL;
#endif
}

void n2vhost_stop(struct n2vhost *h)
{
	int detached = 0;

	if (!h)
		return;
	if (h->th_started) {
		__atomic_store_n(&h->guest_cpu.stop_request, 1u, __ATOMIC_RELEASE);
		if (n2vhost_join(h, 2000) != 0) {
			/* The guest is stuck inside a host shim; it cannot be collected. Detach it and let the
			 * process exit without it.  The thread still reads h->guest_cpu, h->mu and h->mem, so the
			 * struct itself must stay allocated (parked below) -- freeing it here would be a
			 * use-after-free in a thread we just detached. */
			pthread_detach(h->th);
			h->th_started = 0;
			detached = 1;
		}
	}
	if (n2v_shim_set_slot && *n2v_shim_set_slot == h->set)
		*n2v_shim_set_slot = NULL;              /* never point dlopen at a stopped set */
	if (n2v_shim_hw_slot && *n2v_shim_hw_slot == &h->hw)
		*n2v_shim_hw_slot = NULL;
	if (detached) {
		if (n2v_nparked_host < sizeof n2v_parked_host / sizeof n2v_parked_host[0])
			n2v_parked_host[n2v_nparked_host++] = h;
		return;                                 /* no park(h): the thread still needs h->mem */
	}
	park(h);
	pthread_mutex_destroy(&h->mu);
	pthread_cond_destroy(&h->cv);
	free(h);
}

/* =====================================================================================================
 * accessors
 * ===================================================================================================== */

struct n2vcu *n2vhost_vcu(struct n2vhost *h) { return h ? h->vcu : NULL; }
const struct n2vc_hw *n2vhost_hw(struct n2vhost *h) { return h ? &h->hw : NULL; }
struct n2a32_mem *n2vhost_mem(struct n2vhost *h) { return h ? h->mem : NULL; }
struct n2elf_set *n2vhost_elf(struct n2vhost *h) { return h ? h->set : NULL; }
unsigned n2vhost_unresolved(const struct n2vhost *h) { return h ? h->unresolved : 0; }

const char *n2vhost_milestone_name(unsigned ms)
{
	static const char *const n[] = { "none", "M-a", "M-b", "M-c", "M-d", "M-e", "M-f" };

	return ms <= N2VHOST_MS_F ? n[ms] : "?";
}

unsigned n2vhost_milestone(const struct n2vhost *h) { return h ? h->reached : 0; }
const char *n2vhost_milestone_evidence(const struct n2vhost *h, unsigned ms)
{
	return (h && ms <= N2VHOST_MS_F) ? h->why[ms] : "";
}

void n2vhost_dump_shims(struct n2vhost *h, int on) { if (h) h->dump_shims = on; }

void n2vhost_note(struct n2vhost *h, unsigned ms, const char *evidence)
{
	if (!h || ms > N2VHOST_MS_F)
		return;
	milestone(h, ms, "%s", evidence ? evidence : "");
}

void n2vhost_blocked(struct n2vhost *h, unsigned ms, const char *why)
{
	if (!h || ms > N2VHOST_MS_F)
		return;
	/* The first failure after the start is always the next thing to report (M-d is proven by
	 * INIT_DONE, then SET_PARAM or ENCODE may fail).  The milestone's own evidence (why[ms]) is
	 * only filled while that milestone is still unreached. */
	if (h->next_unmet[0] == '\0' && why && why[0])
		snprintf(h->next_unmet, sizeof h->next_unmet, "%s", why);
	if (h->reached >= ms)
		return;
	snprintf(h->why[ms], sizeof h->why[ms], "%s", why ? why : "");
}

enum n2a32_stop n2vhost_stop_reason(const struct n2vhost *h)
{
	enum n2a32_stop s;

	if (!h)
		return N2A32_STOP_NONE;
	/* The stop fields are written once, before h->done, under h->mu; the caller only reads them. */
	pthread_mutex_lock((pthread_mutex_t *)&h->mu);
	s = h->stopped ? h->stop : N2A32_STOP_NONE;
	pthread_mutex_unlock((pthread_mutex_t *)&h->mu);
	return s;
}

const char *n2vhost_stop_text(struct n2vhost *h)
{
	static char buf[160];
	const char *imp = NULL;

	if (!h)
		return "no host";
	pthread_mutex_lock(&h->mu);
	if (!h->stopped) {
		snprintf(buf, sizeof buf, "running or not started");
	} else if (h->stop == N2A32_STOP_NOIMPORT && (imp = n2a32_hostcall_name(h->stop_addr)) != NULL) {
		snprintf(buf, sizeof buf, "NOIMPORT %s at 0x%08x", imp, h->stop_addr);
	} else if (h->stop == N2A32_STOP_UNDEF) {
		/* The interpreter keeps the first failing instruction's word but the last stop's address when a
		 * nested stop was recovered (reported to V02), so the two are printed separately and labelled. */
		snprintf(buf, sizeof buf, "UNDEF insn 0x%08x, last stop 0x%08x", h->stop_insn, h->stop_addr);
	} else {
		snprintf(buf, sizeof buf, "%s at 0x%08x", stop_name(h->stop), h->stop_addr);
	}
	pthread_mutex_unlock(&h->mu);
	return buf;
}

/* =====================================================================================================
 * report
 * ===================================================================================================== */

struct n2v_names {
	char (*v)[N2V_NAME_MAX];
	unsigned n, cap;
};

static void names_add(struct n2v_names *l, const char *s)
{
	unsigned i;

	for (i = 0; i < l->n; i++)
		if (strcmp(l->v[i], s) == 0)
			return;
	if (l->n >= l->cap)
		return;
	snprintf(l->v[l->n], N2V_NAME_MAX, "%s", s);
	l->n++;
}

static int cmp_names(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/* Names bound to the unresolved slot: every registered host-call slot whose name no shim table provides.
 * This is the honest list of "unresolved imports" of the set, computed through the frozen hostcall API. */
static unsigned collect_unresolved(struct n2vhost *h, struct n2v_names *l)
{
	unsigned i, total = 0;

	for (i = 1; i < N2A32_HOSTCALL_MAX; i++) {
		const char *n = n2a32_hostcall_name(N2A32_HOSTCALL_BASE + 4u * i);

		if (!n)
			continue;
		total++;
		if (!table_has(h, n))
			names_add(l, n);
	}
	return total;
}

/* Evidence for M-c: the trace file (N2HW_TRACE) holds one line per host call while the guest runs.
 * vpud's only device is /dev/vcu (FACTS 5.1), so the first "call ioctl" after "call open" is its first
 * ioctl on /dev/vcu.  A front end that has better evidence uses n2vhost_note(). */
static int trace_says_ioctl(struct n2vhost *h)
{
	const char *path = getenv("N2HW_TRACE");
	char line[256];
	FILE *f;
	int opened = 0, ioctls = 0;

	(void)h;
	if (!path || !*path)
		return 0;
	f = fopen(path, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		/* T01's trace prints "h ret X" when the guest *enters* import X and "h call X" when the
		 * shim returns (n2a32_trace_hostcall(..., leaving) is written with leaving=1 on entry). */
		char *call = strstr(line, "h ret ");

		if (!call)
			continue;
		call += 6;
		if (strncmp(call, "open", 4) == 0)
			opened++;
		else if (strncmp(call, "ioctl", 5) == 0)
			ioctls++;
	}
	fclose(f);
	if (ioctls > 0)
		milestone(h, N2VHOST_MS_C, "%d ioctl host call(s) after %d open call(s) in %s", ioctls, opened, path);
	return ioctls > 0;
}

/* M-c from the model's own counters (V40's advice: n2vcu_stats(), never parse a log).  The guest has
 * issued an ioctl on /dev/vcu once a SET_OBJECT/GET_OBJECT or an allocation request arrived. */
static void vcu_says_ioctl(struct n2vhost *h, FILE *out)
{
#if defined(N2VHOST_HAVE_VCU)
	struct n2vcu_stats s;

	if (!h->vcu)
		return;
	memset(&s, 0, sizeof s);
	n2vcu_stats(h->vcu, &s);
	fprintf(out, "N2VHOST vcu set_object=%llu get_object=%llu allocations=%llu gce_flushes=%llu "
		"unknown=%llu mmaps=%llu mmio_r=%llu mmio_w=%llu ipi_queued=%llu ipi_acks=%llu "
		"log_gets=%llu log_waits=%llu\n",
		(unsigned long long)s.set_object, (unsigned long long)s.get_object,
		(unsigned long long)s.allocations, (unsigned long long)s.gce_flushes,
		(unsigned long long)s.unknown_requests, (unsigned long long)s.mmaps,
		(unsigned long long)s.mmio_reads, (unsigned long long)s.mmio_writes,
		(unsigned long long)s.ipi_queued, (unsigned long long)s.ipi_acks,
		(unsigned long long)s.log_gets, (unsigned long long)s.log_waits);
	if (s.set_object + s.get_object + s.allocations > 0)
		/* milestone() keeps the first evidence and never lowers `reached`; calling it here even
		 * after M-d was noted fills the M-c line that reaching M-d would otherwise leave empty. */
		milestone(h, N2VHOST_MS_C, "first ioctl on /dev/vcu: set_object=%llu get_object=%llu "
			"allocations=%llu gce_flushes=%llu (n2vcu_stats)",
			(unsigned long long)s.set_object, (unsigned long long)s.get_object,
			(unsigned long long)s.allocations, (unsigned long long)s.gce_flushes);
#else
	(void)h;
	(void)out;
#endif
}

int n2vhost_report(struct n2vhost *h, FILE *out)
{
	struct n2v_names unres = { NULL, 0, 0 };
	char (*storage)[N2V_NAME_MAX] = NULL;
	char stopbuf[192];
	unsigned i, slots;

	if (!h)
		return 0;
	if (!out)
		out = stdout;

	storage = calloc(256, sizeof *storage);
	if (storage) {
		unres.v = storage;
		unres.cap = 256;
	}
	slots = collect_unresolved(h, &unres);
	if (unres.n > 1)
		qsort(storage, unres.n, sizeof *storage, cmp_names);

	trace_says_ioctl(h);

	fprintf(out, "N2VHOST vendor %s\n", h->vendor_dir);
	vcu_says_ioctl(h, out);
	if (h->nimg == 0) {
		fprintf(out, "N2VHOST M-a not reached: %s\n", h->err[0] ? h->err : "no object was loaded");
		fprintf(out, "N2VHOST reached none: %s\n", h->err[0] ? h->err : "nothing loaded");
		free(storage);
		return 0;
	}
	fprintf(out, "N2VHOST tables");
	for (i = 0; i < h->ntab; i++)
		fprintf(out, " %s=%u(%s)", h->tab[i].name, h->tab[i].count, h->tab[i].owner);
	fprintf(out, "\n");
	fprintf(out, "N2VHOST trace %s\n", h->trace_on ? "on (N2HW_TRACE)" :
		"off (fast interpreter path; no per-instruction trace)");
	fprintf(out, "N2VHOST maps %s\n", h->maps_note[0] ? h->maps_note : "not set");
	for (i = 0; i < h->nimg; i++) {
		const char *soname = object_paths[i][1];

		fprintf(out, "N2VHOST object %s base=0x%08x entry=0x%08x\n", soname, n2elf_base(h->img[i]),
			n2elf_entry(h->img[i]));
	}
	(void)slots;
	if (h->reached >= N2VHOST_MS_A) {
		fprintf(out, "N2VHOST M-a loaded %u objects, linked, %u unresolved imports, %u called so far\n",
			h->nimg, h->unresolved, h->unresolved_calls);
		for (i = 0; i < unres.n; i++)
			fprintf(out, "N2VHOST unresolved %s\n", storage[i]);
		if (h->dump_shims) {
			unsigned t, k;

			for (t = 0; t < h->ntab; t++) {
				if (h->tab[t].is_data) {
					const struct n2shim_data *d = h->tab[t].tab;

					for (k = 0; d && d[k].name; k++)
						fprintf(out, "N2VHOST shim %s\n", d[k].name);
				} else {
					const struct n2shim_entry *e = h->tab[t].tab;

					for (k = 0; e && e[k].name; k++)
						fprintf(out, "N2VHOST shim %s\n", e[k].name);
				}
			}
		}
	} else {
		fprintf(out, "N2VHOST M-a not reached: %s\n", h->err[0] ? h->err : "link failed");
	}

	if (h->init_ran)
		fprintf(out, "N2VHOST M-b init arrays ran over %u objects (0 init functions: the vendor set has only "
			"the 0xffffffff sentinel in vpud's preinit/init arrays and FINI arrays elsewhere)\n", h->nimg);
	else
		fprintf(out, "N2VHOST M-b not reached: init stopped (%s)\n", n2vhost_stop_text(h));

	if (h->reached >= N2VHOST_MS_C)
		fprintf(out, "N2VHOST M-c %s\n", h->why[N2VHOST_MS_C]);
	else
		fprintf(out, "N2VHOST M-c not reached: no SET_OBJECT/GET_OBJECT/allocation on /dev/vcu yet%s\n",
			h->stopped ? " (guest stopped first)" : "");

	if (h->reached >= N2VHOST_MS_D)
		fprintf(out, "N2VHOST M-d %s\n", h->why[N2VHOST_MS_D]);
	else
		fprintf(out, "N2VHOST M-d not reached: %s\n", h->why[N2VHOST_MS_D][0] ? h->why[N2VHOST_MS_D] :
			"the AP side (ap/n2ap_venc.h, V41) is not in the build");

	if (h->reached >= N2VHOST_MS_E)
		fprintf(out, "N2VHOST M-e %s\n", h->why[N2VHOST_MS_E]);
	else
		fprintf(out, "N2VHOST M-e not reached: %s (%lu hw register batches)\n",
			h->why[N2VHOST_MS_E][0] ? h->why[N2VHOST_MS_E] : "no VENC window access seen",
			h->regs_calls);

	if (h->reached >= N2VHOST_MS_F)
		fprintf(out, "N2VHOST M-f %s\n", h->why[N2VHOST_MS_F]);
	else
		fprintf(out, "N2VHOST M-f not reached: %s (%lu waits)\n",
			h->why[N2VHOST_MS_F][0] ? h->why[N2VHOST_MS_F] : "no wait_irq for RWVC_IRQ_VENC",
			h->irq_waits);

	fprintf(out, "N2VHOST hw power_on_venc=%lu venc_ops=%lu irq_waits=%lu venc_waits=%lu regs_calls=%lu\n",
		h->power_on_venc, h->venc_ops, h->irq_waits, h->venc_irq_waits, h->regs_calls);

	/* the first unmet need, in the order the pipeline hits them */
	stopbuf[0] = '\0';
	if (h->stopped) {
		const char *imp = NULL;
		char impbuf[96];

		pthread_mutex_lock(&h->mu);
		if (h->stop == N2A32_STOP_NOIMPORT && (imp = n2a32_hostcall_name(h->stop_addr)) != NULL)
			snprintf(impbuf, sizeof impbuf, "%s", imp);
		pthread_mutex_unlock(&h->mu);
		if (imp)
			snprintf(stopbuf, sizeof stopbuf,
				"guest stopped at 0x%08x: NOIMPORT %s (no registered shim provides it)",
				h->stop_addr, impbuf);
		else if (h->stop == N2A32_STOP_UNDEF)
			snprintf(stopbuf, sizeof stopbuf,
				"guest stopped: UNDEF insn 0x%08x (first failure), last stop at 0x%08x",
				h->stop_insn, h->stop_addr);
		else
			snprintf(stopbuf, sizeof stopbuf, "guest stopped: %s at 0x%08x", stop_name(h->stop),
				h->stop_addr);
		fprintf(out, "N2VHOST first-unmet %s\n", stopbuf);
	} else if (!h->th_started) {
		fprintf(out, "N2VHOST first-unmet none: the guest main was never started (%s)\n",
			h->err[0] ? h->err : "init did not finish");
	} else {
		fprintf(out, "N2VHOST first-unmet none: the guest is still running\n");
	}
	if (h->next_unmet[0])
		fprintf(out, "N2VHOST next-unmet %s\n", h->next_unmet);
	fprintf(out, "N2VHOST stop %s icount=%llu\n", n2vhost_stop_text(h),
		(unsigned long long)(h->stopped ? h->stop_icount : h->guest_cpu.icount));

	fprintf(out, "N2VHOST reached %s: %s\n", n2vhost_milestone_name(h->reached),
		h->why[h->reached][0] ? h->why[h->reached] : "nothing proven");
	free(storage);
	return (int)h->reached;
}

/* =====================================================================================================
 * runner (not part of the library: built with -DN2VHOST_NO_MAIN for libn2vhost.a)
 * ===================================================================================================== */
#ifndef N2VHOST_NO_MAIN

#include "n2vc_hw_fake.h"       /* T02: script/violations/stats of the fake back end */
#if defined(N2VHOST_HAVE_FAKEVENC)
#include "n2venc_fake.h"        /* opus-venc: VENC behaviour on top of T02's fake */
#endif

struct n2v_frame {
	uint32_t handle, pa, bytes;
	uint32_t guest;                 /* guest address of the DMA mirror (V41 n2vc_dma_guest_alloc), 0 = none */
};

/* V33 owns this sink (n2shim_stdio.c); the front end installs it.  Weak, so the library still links
 * when V33's stdio shim is not in the build. */
__attribute__((weak)) void (*n2shim_log)(int prio, const char *tag, const char *text) = NULL;

static void guest_log(int prio, const char *tag, const char *text)
{
	/* N2VHOST_LOG_INDEX=1 numbers the guest's log lines in call order, so that one line of the run
	 * log can be matched to the h call __android_log_print entries of the N2HW_TRACE. Diagnosing the
	 * vendor's own messages (e.g. the SET_PARAM path) needs that correspondence.
	 *
	 * V40's GET_LOG_OBJECT returns immediately (its documented deviation), so the vendor log reader
	 * spins and a run can print the same line 100k times; 600 MB of run log is not useful and the
	 * shared disk has ~1 GB.  Consecutive identical lines are collapsed, with a count when the text
	 * changes (the first 5 are kept so the start of a burst is visible). */
	static unsigned long seq, rep;
	static char prev[192];
	static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&mu);
	if (text && strcmp(prev, text) == 0) {
		rep++;
		if (rep <= 5 || (rep % 10000u) == 0) {
			const char *idx = getenv("N2VHOST_LOG_INDEX");

			if (idx && *idx && *idx != '0')
				fprintf(stderr, "N2GUEST#%lu %s/%d %s (repeat %lu)\n", ++seq,
					tag ? tag : "-", prio, text, rep);
			else
				fprintf(stderr, "N2GUEST %s/%d %s (repeat %lu)\n", tag ? tag : "-", prio,
					text, rep);
		}
		pthread_mutex_unlock(&mu);
		return;
	}
	if (rep > 5 && prev[0])
		fprintf(stderr, "N2GUEST (previous line repeated %lu times)\n", rep);
	rep = 0;
	strncpy(prev, text ? text : "", sizeof prev - 1);
	prev[sizeof prev - 1] = '\0';
	if (text && strlen(text) >= sizeof prev - 1)
		prev[sizeof prev - 2] = '~';   /* mark a truncated comparison key */
	{
		const char *idx = getenv("N2VHOST_LOG_INDEX");

		if (idx && *idx && *idx != '0')
			fprintf(stderr, "N2GUEST#%lu %s/%d %s\n", ++seq, tag ? tag : "-", prio,
				text ? text : "");
		else
			fprintf(stderr, "N2GUEST %s/%d %s\n", tag ? tag : "-", prio, text ? text : "");
	}
	pthread_mutex_unlock(&mu);
}

static const char *run_dir(void)
{
	const char *d = getenv("N2HW_RUN_DIR");

	return d && *d ? d : "/tmp/n2hw-iv/run";
}

/* V41's event trace, printed only when N2VHOST_AP_TRACE is set: the AP-side state machine around
 * open/start/SET_PARAM/encode is what turns an M-d success into the next finding. */
static void ap_trace(void *arg, const char *event, uint32_t a, uint32_t b, uint32_t c)
{
	(void)arg;
	if (getenv("N2VHOST_AP_TRACE"))
		fprintf(stderr, "N2VHOST ap %s a=%u b=%u c=%u\n", event ? event : "-", a, b, c);
}

/* The 320x240 NV12 test picture, generated deterministically so that the PC can regenerate it and compare
 * PSNR of a decoded frame against it (tests/check/gen_yuv.py uses its own formula; this one is only for the
 * runner's smoke test and is written next to the bitstream as frame-<W>x<H>.nv12). */
static void fill_nv12(uint8_t *p, unsigned w, unsigned h)
{
	unsigned x, y;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			p[y * w + x] = (uint8_t)(16 + ((x * 5u + y * 3u) & 0xdfu));
	p += w * h;
	for (y = 0; y < h / 2; y++) {
		for (x = 0; x < w / 2; x++) {
			p[y * (w / 2) + x] = (uint8_t)(128 + ((x * 3u + y * 7u) & 0x3fu));
			p[(h / 2) * (w / 2) + y * (w / 2) + x] = (uint8_t)(128 - ((x * 5u + y * 2u) & 0x3fu));
		}
	}
}

/* Allocate a DMA buffer.  With V41 in the build, prefer n2vc_dma_guest_alloc(): it allocates the DMA
 * handle *and* a guest mirror, records the pa, so n2ap_venc_* can sync the buffer around each job and the
 * runner can fill/read it through the guest address.  Without V41 (or if the model refuses) a plain
 * hw->dma_alloc handle is used: the AP side then only needs the pa (V41's documented fallback). */
static int prep_buffer(const struct n2vc_hw *hw, struct n2a32_mem *mem, uint32_t bytes,
	struct n2v_frame *fr)
{
	memset(fr, 0, sizeof *fr);
	fr->bytes = bytes;
#if defined(N2VHOST_HAVE_AP)
	if (mem && n2vc_dma_guest_alloc(hw, mem, bytes, 0, &fr->handle, &fr->pa, &fr->guest) == 0)
		return 0;
#endif
	(void)mem;
	if (!hw->dma_alloc)
		return -1;
	return hw->dma_alloc(hw->ctx, bytes, 0, &fr->handle, &fr->pa) == 0 ? 0 : -1;
}

static void drop_buffer(const struct n2vc_hw *hw, struct n2v_frame *fr)
{
	if (!fr->handle)
		return;
#if defined(N2VHOST_HAVE_AP)
	if (fr->guest) {
		(void)n2vc_dma_guest_forget(hw, fr->handle);   /* owns the DMA handle it allocated */
		fr->handle = 0;
		return;
	}
#endif
	if (hw->dma_free)
		(void)hw->dma_free(hw->ctx, fr->handle);
	fr->handle = 0;
}

static int buffer_write(const struct n2vc_hw *hw, struct n2a32_mem *mem, const struct n2v_frame *fr,
	uint32_t off, const void *p, uint32_t bytes)
{
	if (fr->guest) {
		void *g = n2a32_ptr(mem, fr->guest + off);

		if (!g || off + bytes > fr->bytes)
			return -1;
		memcpy(g, p, bytes);
		return 0;
	}
	return hw->dma_rw ? hw->dma_rw(hw->ctx, fr->handle, off, (void *)p, bytes, 1) : -1;
}

static int buffer_read(const struct n2vc_hw *hw, struct n2a32_mem *mem, const struct n2v_frame *fr,
	uint32_t off, void *p, uint32_t bytes)
{
	if (fr->guest) {
		const void *g = n2a32_ptr(mem, fr->guest + off);

		if (!g || off + bytes > fr->bytes)
			return -1;
		memcpy(p, g, bytes);
		return 0;
	}
	return hw->dma_rw ? hw->dma_rw(hw->ctx, fr->handle, off, p, bytes, 0) : -1;
}

static int guest_running(struct n2vhost *h)
{
	int run;

	pthread_mutex_lock(&h->mu);
	run = !h->done;
	pthread_mutex_unlock(&h->mu);
	return run;
}

/* Every AP wait is bounded; the daemon's default is 6 s, but a runner that reports the first unmet need
 * should not sit on a dead guest.  N2VHOST_AP_TIMEOUT_MS overrides. */
static unsigned ap_timeout_ms(void)
{
	const char *s = getenv("N2VHOST_AP_TIMEOUT_MS");
	unsigned v = s && *s ? (unsigned)strtoul(s, NULL, 10) : 2500u;

	return v ? v : 1u;
}

/* The AP side of a real phone opens the encoder long after boot; here vpud has just started and its IPI
 * handlers are registered by the service threads it creates on the way.  A message that arrives before
 * that is delivered with "Get ipi_id: ... obj handler is NULL" and dropped, so give the daemon a moment
 * (N2VHOST_AP_DELAY_MS, default 1000 ms) before the first AP_IPIMSG_ENC_INIT. */
static void ap_settle(void)
{
	const char *s = getenv("N2VHOST_AP_DELAY_MS");
	unsigned ms = s && *s ? (unsigned)strtoul(s, NULL, 10) : 1000u;
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

#if defined(N2VHOST_HAVE_AP)
/* The AP side of the kernel powers the encoder clock/domain around every job
 * (venc_encode_prepare() -> mtk_vcodec_enc_clock_on(), venc_drv_if.c:109-124, called from the
 * VCU_IPIMSG_ENC_POWER_ON handler, venc_vcu_if.c:135-148).  The phone's daemon does not send
 * POWER_ON before it writes the VENC registers, so this drive does what the V4L2 driver does; the
 * model's equivalent of the two clocks is RWVC_POWER(VENC) + RWVC_POWER(VENC_C1).  When V41 gains
 * n2ap_venc_power(), this becomes a call to it (the back end itself stays refcount-free). */
static void venc_power(struct n2vhost *h, struct n2ap_venc *e, int on)
{
	const struct n2vc_hw *hw = n2vhost_hw(h);

	/* V41's public reference counter (rev 5): it shares the count with any POWER_ON/POWER_OFF the
	 * daemon sends, so mixing both cannot unbalance the blocks.  V41 takes only RWVC_BLK_VENC:
	 * VENC_C1 is the whole core-1 power domain, not a gate this path should force (K02).  The
	 * fallback (an older V41 without the symbol) still takes both, which is what the model needs. */
	if (n2ap_venc_power && e) {
		(void)n2ap_venc_power(e, on ? 1 : 0);
		return;
	}
	if (!hw || !hw->power)
		return;
	if (on) {
		(void)hw->power(hw->ctx, RWVC_BLK_VENC, 1);
		(void)hw->power(hw->ctx, RWVC_BLK_VENC_C1, 1);
	} else {
		(void)hw->power(hw->ctx, RWVC_BLK_VENC_C1, 0);
		(void)hw->power(hw->ctx, RWVC_BLK_VENC, 0);
	}
}

/* One encode job.  Uses V41's fd/async extensions when they are linked (proposal IV -> V41); the
 * share ids are the ones registered with V34 below.  A V41 without them still runs the sequence
 * header, then fails on the frame because the vendor ring buffer calls ion_import(fd = 0). */
static int ap_encode(struct n2vhost *h, struct n2ap_venc *e, uint32_t frame_handle,
	uint32_t bs_handle, unsigned flags, struct n2ap_enc_result *r, int *fds_used)
{
	int rc;

	(void)h;
	if (n2ap_venc_encode_fd) {
		*fds_used = 1;
		rc = n2ap_venc_encode_fd(e, frame_handle, 0x500, bs_handle, 0x501, flags, r);
	} else {
		*fds_used = 0;
		rc = n2ap_venc_encode(e, frame_handle, bs_handle, flags, r);
	}
	if (rc == 0 && n2ap_venc_wait_done)
		rc = n2ap_venc_wait_done(e, 3000, r);
	return rc;
}
#endif

static int drive_ap(struct n2vhost *h, unsigned w, unsigned hh, const char *out_dir)
{
	const struct n2vc_hw *hw = n2vhost_hw(h);
	struct n2a32_mem *mem = n2vhost_mem(h);
	struct n2v_frame frame, bs;
	uint8_t *pix = NULL;
	char path[600];
	FILE *f;
	uint32_t nv12 = w * hh * 3u / 2u;
	int rc = 0;

	if (!hw->dma_alloc) {
		printf("N2VHOST ap not driven: this hardware back end has no DMA\n");
		return -1;
	}
	if (prep_buffer(hw, mem, nv12, &frame) != 0) {
		printf("N2VHOST ap not driven: dma_alloc(%u) failed\n", nv12);
		return -1;
	}
	if (prep_buffer(hw, mem, 1u << 20, &bs) != 0) {
		printf("N2VHOST ap not driven: dma_alloc(bitstream) failed\n");
		drop_buffer(hw, &frame);
		return -1;
	}
	pix = malloc(nv12);
	if (!pix) {
		drop_buffer(hw, &bs);
		drop_buffer(hw, &frame);
		return -1;
	}
	fill_nv12(pix, w, hh);
	if (buffer_write(hw, mem, &frame, 0, pix, nv12) != 0) {
		printf("N2VHOST ap not driven: writing the frame failed\n");
		free(pix);
		drop_buffer(hw, &bs);
		drop_buffer(hw, &frame);
		return -1;
	}
	snprintf(path, sizeof path, "%s/frame-%ux%u.nv12", out_dir, w, hh);
	f = fopen(path, "wb");
	if (f) {
		(void)fwrite(pix, 1, nv12, f);
		fclose(f);
	}
	free(pix);
	printf("N2VHOST frame %ux%u NV12 %u bytes handle=%u pa=0x%08x%s -> %s\n", w, hh, nv12,
		frame.handle, frame.pa, frame.guest ? " guest-mapped" : "", path);
	printf("N2VHOST bitstream buffer 1 MiB handle=%u pa=0x%08x\n", bs.handle, bs.pa);

#if defined(N2VHOST_HAVE_AP)
	{
		struct n2ap_venc *e = NULL;
		struct n2ap_enc_param p;
		struct n2ap_enc_result r;
		uint8_t *hdr = NULL;
		uint32_t hdr_bytes = 0;

		if (!n2vhost_vcu(h)) {
			n2vhost_blocked(h, N2VHOST_MS_D,
				"AP side not driven: the /dev/vcu model (V40) is not in the build");
			rc = -1;
			goto out;
		}
		if (!guest_running(h)) {
			n2vhost_blocked(h, N2VHOST_MS_D,
				"AP side not driven: the guest had already stopped (see first-unmet)");
			rc = -1;
			goto out;
		}
		ap_settle();
		if (!guest_running(h)) {
			n2vhost_blocked(h, N2VHOST_MS_D,
				"AP side not driven: the guest stopped before the first AP message (see first-unmet)");
			rc = -1;
			goto out;
		}
		/* venc_if_init(): AP_IPIMSG_ENC_INIT, answered by VCU_IPIMSG_ENC_INIT_DONE. */
		rc = n2ap_venc_open(&e, n2vhost_vcu(h), hw, N2AP_FOURCC_H264, ap_timeout_ms());
		if (rc != 0) {
			char why[200];

			snprintf(why, sizeof why, "n2ap_venc_open returned %d; the AP_IPIMSG_ENC_INIT exchange with "
				"the daemon did not complete (guest %s)", rc,
				guest_running(h) ? "running but not serving /dev/vcu" : "stopped - see first-unmet");
			n2vhost_blocked(h, N2VHOST_MS_D, why);
			goto out;
		}
		{
			char ev[176];

			snprintf(ev, sizeof ev, "AP_IPIMSG_ENC_INIT (0xc000) answered with "
				"VCU_IPIMSG_ENC_INIT_DONE (0xd000): n2ap_venc_open returned 0, last_status=%d",
				(int)n2ap_venc_last_status(e));
			n2vhost_note(h, N2VHOST_MS_D, ev);
		}
		n2ap_venc_set_trace(e, ap_trace, NULL);
		/* The daemon's ring buffer imports the two buffers through ion; V34's hook makes the AP's
		 * own DMA handles resolvable as share fds.  Only meaningful with V41's fd extension, but
		 * registering them is harmless without it (and says so in the report). */
		if (n2shim_ion_register_share) {
			int rf = n2shim_ion_register_share(0x500, frame.handle, frame.pa, frame.bytes);
			int rb = n2shim_ion_register_share(0x501, bs.handle, bs.pa, bs.bytes);

			printf("N2VHOST ion shares frame(fd=0x500)=%d bs(fd=0x501)=%d%s\n", rf, rb,
				n2ap_venc_encode_fd ? "" :
				" (V41 has no fd encode API: the daemon will see fd 0)");
		}
		/* vb2ops_venc_start_streaming() on a 320x240 session with the V4L2 defaults (SEPARATE header
		 * mode: the driver encodes the sequence header as its own job before the first frame). */
		n2ap_enc_param_1080p30_h264(&p);
		p.width = w;
		p.height = hh;
		p.buf_width = w;
		p.buf_height = hh;
		p.frm_rate = 30;
		p.bitrate = w * hh * 2u;          /* ~2 bit/pixel per frame at 30 fps is plenty for a smoke test */
		p.gop_size = 30;
		p.intra_period = 30;
		rc = n2ap_venc_start(e, &p);
		printf("N2VHOST ap start rc=%d\n", rc);
		if (rc != 0) {
			char why[200];

			snprintf(why, sizeof why, "n2ap_venc_start failed (%d): VENC_SET_PARAM_ENC was not "
				"answered by the daemon", rc);
			n2vhost_blocked(h, N2VHOST_MS_D, why);
			(void)n2ap_venc_close(e);
			goto out;
		}
		{
			int fds = 0;

			venc_power(h, e, 1);
			rc = ap_encode(h, e, 0, bs.handle, N2AP_ENC_SEQ_HDR, &r, &fds);
			venc_power(h, e, 0);
		}
		if (rc != 0) {
			n2vhost_blocked(h, N2VHOST_MS_F, "the sequence-header encode failed");
			(void)n2ap_venc_close(e);
			goto out;
		}
		if (r.bytes && r.bytes <= bs.bytes) {
			hdr = malloc(r.bytes);
			if (hdr) {
				if (buffer_read(hw, mem, &bs, 0, hdr, r.bytes) == 0)
					hdr_bytes = r.bytes;
			}
		}
		printf("N2VHOST encode seq-hdr bytes=%u key=%u\n", r.bytes, r.is_key_frm);
		{
			int fds = 0;

			venc_power(h, e, 1);
			rc = ap_encode(h, e, frame.handle, bs.handle, N2AP_ENC_FRAME, &r, &fds);
			venc_power(h, e, 0);
			printf("N2VHOST encode frame fds=%d bytes=%u key=%u\n", fds, r.bytes, r.is_key_frm);
		}
		if (rc != 0) {
			char why[220];
			uint32_t pa = 0, ga = 0, by = 0;
			int lost = 0;

			/* Name the next blocker precisely: V41's n2vc_dma_guest_unmap() clears an
			 * AP-owned entry when the daemon unmaps its ion view after the previous job. */
			if (n2vc_dma_lookup_handle(frame.handle, &pa, &ga, &by) != 0 ||
			    n2vc_dma_lookup_handle(bs.handle, &pa, &ga, &by) != 0)
				lost = 1;
			snprintf(why, sizeof why, "the frame encode failed (%d)%s", rc,
				lost ? ": V41's DMA table no longer knows the AP's buffer - the daemon's "
				       "ion_munmap after the previous job cleared the owns=0 entry in "
				       "n2vc_dma_guest_unmap()" : "");
			n2vhost_blocked(h, N2VHOST_MS_F, why);
			free(hdr);
			(void)n2ap_venc_close(e);
			goto out;
		}
		printf("N2VHOST encode frame bytes=%u key=%u sync=%u\n", r.bytes, r.is_key_frm, p.sync_mode);
		{   /* EOS: VENC_START_OPT_ENCODE_FRAME_FINAL without buffers (mtk_vcodec_enc.c:1928) */
			struct n2ap_enc_result fr;
			int frc = n2ap_venc_encode(e, 0, bs.handle, N2AP_ENC_FRAME_FINAL, &fr);

			printf("N2VHOST encode final rc=%d bytes=%u\n", frc, frc == 0 ? fr.bytes : 0u);
		}
		{
			uint32_t total = 0;
			uint8_t *stream = NULL;

			if (r.bytes && r.bytes <= bs.bytes) {
				stream = malloc((size_t)hdr_bytes + r.bytes);
				if (stream) {
					memcpy(stream, hdr, hdr_bytes);
					if (buffer_read(hw, mem, &bs, 0, stream + hdr_bytes, r.bytes) == 0)
						total = hdr_bytes + r.bytes;
				}
			}
			if (total) {
				snprintf(path, sizeof path, "%s/bitstream-%ux%u.h264", out_dir, w, hh);
				f = fopen(path, "wb");
				if (f) {
					(void)fwrite(stream, 1, total, f);
					fclose(f);
					printf("N2VHOST bitstream %s %u bytes\n", path, total);
				}
			}
			free(stream);
		}
		free(hdr);
		(void)n2ap_venc_close(e);
	}
#else
	n2vhost_blocked(h, N2VHOST_MS_D,
		"ap/n2ap_venc.h is not in the build, so open/set_param/encode cannot be called (V41)");
	rc = -1;
	goto out;
#endif
out:
	drop_buffer(hw, &bs);
	drop_buffer(hw, &frame);
	return rc;
}

/* Read a NUL-terminated guest string for the trace explanation; bounded and safe for any address. */
static int guest_str(struct n2vhost *h, uint32_t a, char *buf, size_t len)
{
	struct n2a32_mem *m = n2vhost_mem(h);
	size_t i;

	if (!m || !a || len < 2)
		return -1;
	/* plain guest memory: the trace is only explained while the host is alive */
	for (i = 0; i + 1 < len; i++) {
		uint8_t *p = (uint8_t *)n2a32_ptr(m, a + (uint32_t)i);

		if (!p)
			break;
		buf[i] = (char)*p;
		if (*p == 0)
			return (int)i;
	}
	buf[i] = 0;
	return (int)i;
}

/* Explain the interesting host calls of the last run from the trace file: for property_get, open,
 * __open_2, fopen and dlopen print the guest string argument.  Diagnostic only (needs --trace-regs). */
static void explain_trace(struct n2vhost *h, FILE *out)
{
	const char *path = getenv("N2HW_TRACE");
	char line[512], last_regs[512] = "";
	FILE *f;
	unsigned shown = 0;

	if (!path || !*path)
		return;
	f = fopen(path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof line, f) && shown < 12) {
		char name[64];
		unsigned r0 = 0;
		const char *keys[] = { "property_get", "open", "__open_2", "fopen", "dlopen", "ioctl" };
		unsigned k;
		int interesting = 0;

		if (line[0] == 'r' && line[1] == ' ' && line[2] && isxdigit((unsigned char)line[2])) {
			snprintf(last_regs, sizeof last_regs, "%s", line);
			continue;
		}
		if (sscanf(line, "h ret %63s", name) != 1)
			continue;
		for (k = 0; k < sizeof keys / sizeof keys[0]; k++)
			if (strcmp(name, keys[k]) == 0)
				interesting = 1;
		if (!interesting || last_regs[0] == 0)
			continue;
		if (sscanf(last_regs, "r %x", &r0) != 1)
			continue;
		{
			char str[128];

			if (guest_str(h, r0, str, sizeof str) >= 0 && str[0]) {
				fprintf(out, "N2VHOST guest %s(\"%s\")\n", name, str);
				shown++;
			} else if (strcmp(name, "ioctl") == 0) {
				fprintf(out, "N2VHOST guest ioctl(fd=%u, request=0x%08x)\n", r0, 0u);
				shown++;
			}
		}
	}
	fclose(f);
}

static void usage(FILE *out)
{
	fprintf(out,
		"usage: n2vhost [options]\n"
		"  --vendor DIR     extracted /vendor (default $N2HW_VENDOR_DIR or gpu/build/cam-vendor-20261109/vfs)\n"
		"  --record FILE    fake hardware record file (default $N2HW_RUN_DIR/hw-record.txt)\n"
		"  --script FILE    fake hardware script (also $N2VC_FAKE_SCRIPT)\n"
		"  --size WxH       frame size for the AP drive (default 320x240)\n"
		"  --seconds N      how long to let the guest run before stopping it (default 5)\n"
		"  --quiet          only the report's milestone lines\n"
		"  --help           this text\n"
		"Environment: N2VHOST_ENTRY=start|apps_init|service-venc (diagnostic entry point),\n"
		"             N2HW_TRACE=FILE (full interpreter trace; needed for M-c evidence),\n"
		"             N2HW_RUN_DIR=DIR (frame, bitstream and record files)\n");
}

int main(int argc, char **argv)
{
	const char *vendor = NULL, *record = NULL, *script = NULL, *entry = NULL;
	unsigned w = 320, hh = 240, seconds = 5;
	int quiet = 0, dump_shims = 0, trace_regs = 0, explain = 0, i, rc;
	struct n2vc_hw hw;
	const struct n2vc_hw *hw_used = &hw;
	struct n2vhost *host = NULL;
	char recpath[600], tracepath[600];
#if defined(N2VHOST_HAVE_FAKEVENC)
	struct n2vc_hw vhw;
	struct n2venc_fake *venc = NULL;
#endif

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--vendor") == 0 && i + 1 < argc)
			vendor = argv[++i];
		else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
			record = argv[++i];
		else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc)
			script = argv[++i];
		else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			if (sscanf(argv[++i], "%ux%u", &w, &hh) != 2 || w < 16 || hh < 16 || w > 4096 || hh > 4096) {
				fprintf(stderr, "n2vhost: bad --size\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
			seconds = (unsigned)strtoul(argv[++i], NULL, 10);
		else if (strcmp(argv[i], "--quiet") == 0)
			quiet = 1;
		else if (strcmp(argv[i], "--dump-shims") == 0)
			dump_shims = 1;
		else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc)
			entry = argv[++i];
		else if (strcmp(argv[i], "--trace-regs") == 0)
			trace_regs = 1;
		else if (strcmp(argv[i], "--explain") == 0)
			explain = 1;
		else if (strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			return 0;
		} else {
			fprintf(stderr, "n2vhost: unknown argument %s\n", argv[i]);
			usage(stderr);
			return 2;
		}
	}
	if (w & 1u)
		w++;
	if (hh & 1u)
		hh++;

	if (!record) {
		snprintf(recpath, sizeof recpath, "%s/hw-record.txt", run_dir());
		record = recpath;
	}
	if (!script)
		script = getenv("N2VC_FAKE_SCRIPT");
	/* N2HW_TRACE is set by run_vhost.sh: the library does not touch the environment, and M-c evidence is
	 * read from the trace file only when the caller asked for one. */
	(void)tracepath;

	if (!quiet && n2shim_log == NULL)
		n2shim_log = guest_log;      /* show the guest's own log lines (vpud logs through this) */
	if (n2vc_hw_open_fake(&hw, record) != 0) {
		printf("N2VHOST reached M-none: cannot open the fake hardware (%s)\n", record);
		return 1;
	}
	if (script && n2vc_hw_fake_script(&hw, script) != 0) {
		printf("N2VHOST script %s rejected: %s\n", script, n2vc_hw_fake_last_error(&hw));
		script = NULL;
	}
	if (!quiet)
		printf("N2VHOST fake hardware record=%s script=%s\n", record, script ? script : "none");
#if defined(N2VHOST_HAVE_FAKEVENC)
	/* opus-venc's behavioural VENC model on top of T02's register/recorder fake: reset 0x0a8,
	 * trigger 0x058 -> a synthetic IDR bitstream, BYTE_CNT, IRQ status and one interrupt.  `hw`
	 * stays T02's for its controls and violation count; the runner and the VCU model use hw_used. */
	{
		char venclog[600];

		snprintf(venclog, sizeof venclog, "%s/venc-fake.txt", run_dir());
		if (n2venc_fake_wrap(&hw, &vhw, &venc, venclog) == 0) {
			hw_used = &vhw;
			if (!quiet)
				printf("N2VHOST fake VENC model on (log %s)\n", venclog);
		} else if (!quiet) {
			printf("N2VHOST fake VENC model not available; the raw fake is used\n");
		}
	}
#endif

	if (trace_regs)
		n2a32_trace_regs(1);
	rc = n2vhost_start_entry(&host, hw_used, vendor, entry);
	if (host)
		n2vhost_dump_shims(host, dump_shims);
	if (rc < 0) {
		printf("N2VHOST %s\n", host ? n2vhost_error(host) : "n2vhost_start failed");
		if (host)
			n2vhost_report(host, stdout);
		n2vhost_stop(host);
		if (hw_used->close)
			hw_used->close(hw_used->ctx);
		return 1;
	}
	if (rc == 1) {
		printf("N2VHOST start partial: %s\n", n2vhost_error(host));
	} else {
		/* The AP side runs on this thread while vpud runs on the guest thread. */
		(void)drive_ap(host, w, hh, run_dir());
	}

	/* Let vpud run for a bounded time, then stop it.  A stop (NOIMPORT/UNDEF/...) ends the guest thread
	 * earlier and join returns immediately. */
	if (n2vhost_join(host, seconds * 1000u) != 0 && !quiet)
		printf("N2VHOST guest still running after %u s; stopping it\n", seconds);

	if (explain)
		explain_trace(host, stdout);
	if (!quiet)
		printf("N2VHOST fake violations=%u\n", n2vc_hw_fake_violations(&hw));
#if defined(N2VHOST_HAVE_FAKEVENC)
	if (venc && !quiet) {
		struct n2venc_fake_stats vs;

		n2venc_fake_get_stats(venc, &vs);
		printf("N2VHOST fake-venc resets=%u triggers=%u frames=%u irqs=%u/%u acks=%u polls=%u/%u "
		       "overflow=%u dma_errors=%u unsupported=%u headers=%u last_bytes=%u mb=%ux%u\n",
		       vs.resets, vs.triggers, vs.frames, vs.irqs_taken, vs.irqs_raised, vs.acks, vs.polls,
		       vs.poll_fail, vs.overflow, vs.dma_errors, vs.unsupported, vs.headers_learned,
		       vs.last_bytes, vs.last_mb_w, vs.last_mb_h);
	}
#endif
	rc = n2vhost_report(host, stdout);
	n2vhost_stop(host);
	if (hw_used->close)
		hw_used->close(hw_used->ctx);   /* the caller owns the back end; the fake frees its pool */
	return rc >= (int)N2VHOST_MS_A ? 0 : 1;
}
#endif /* N2VHOST_NO_MAIN */
