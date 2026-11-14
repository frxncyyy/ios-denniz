/* SPDX-License-Identifier: MIT */
/*
 * test_v33/t_main.c -- host tests for package V33 (n2vfs.c + n2shim_stdio.c).
 *
 * Every shim is driven the way the interpreter drives it: a struct n2a32_cpu with r0-r3 and a guest stack,
 * reading the result from r0.  Guest memory is a real n2a32_mem (V02's 4 GiB reservation), so guest pointers,
 * guest strings and the per-thread errno word are ordinary guest addresses.  A fake n2vfs_dev records every
 * callback.  Expected values are literals taken from the deliverable, the Linux ABI, the vendor objects'
 * call sites (see the comments in the two source files) and the phone's own mt6893.dts.
 *
 * Run through run_tests.sh; the last line on success is "N2TEST V33 PASS <passed>/<total>".
 */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"

/* V33 extras exported from n2shim_stdio.c.  No frozen header declares them (n2elf.h knows only the two
 * tables), so the test and the daemon repeat these declarations; the STATUS.md documents them. */
extern void (*n2shim_log)(int prio, const char *tag, const char *text);
int n2shim_stdio_exit_reason(void);
int n2shim_stdio_exit_status(void);
uint32_t n2shim_stdio_exit_pc(void);
void n2shim_stdio_exit_clear(void);
void n2shim_stdio_set_stderr_symbol(uint32_t guest_addr);
int n2vfs_set_maps(const char *text, uint32_t len);

/* ---- harness -------------------------------------------------------------------------------------------------- */
static int g_pass, g_fail;

#define CHECK(cond, ...) do { \
	if (cond) { g_pass++; } else { \
		g_fail++; \
		fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} \
} while (0)

static struct n2a32_mem *g_mem;
static struct n2a32_cpu g_cpu;          /* the main guest cpu */
static uint32_t g_stack;               /* 64 KiB guest stack, grows down; r13 points at its top */

static void guest_init(void)
{
	g_mem = n2a32_mem_create();
	if (!g_mem) {
		fprintf(stderr, "n2a32_mem_create failed\n");
		exit(2);
	}
	g_stack = n2a32_mem_alloc(g_mem, 64 * 1024, 16);
	if (!g_stack) {
		fprintf(stderr, "guest stack alloc failed\n");
		exit(2);
	}
	memset(&g_cpu, 0, sizeof g_cpu);
	g_cpu.mem = g_mem;
	g_cpu.r[13] = g_stack + 64 * 1024;
	g_cpu.r[15] = 0x00400000u;      /* not executed; only recorded by exit()/abort() */
}

static n2a32_hostcall find_shim(const char *name)
{
	const struct n2shim_entry *e;

	for (e = n2shim_stdio; e->name; e++)
		if (!strcmp(e->name, name))
			return e->fn;
	fprintf(stderr, "no shim named %s\n", name);
	exit(2);
}

/* Call a shim with up to seven words: r0-r3 then the guest stack (AAPCS).  Returns r0. */
static uint32_t calln(struct n2a32_cpu *c, const char *name, uint32_t n, const uint32_t *a)
{
	n2a32_hostcall fn = find_shim(name);
	unsigned i;

	for (i = 0; i < n && i < 4; i++)
		c->r[i] = a[i];
	for (i = 4; i < n; i++)
		n2a32_st32(c, c->r[13] + 4 * (i - 4), a[i]);
	fn(c);
	return c->r[0];
}

static uint32_t call0(const char *n) { return calln(&g_cpu, n, 0, NULL); }
static uint32_t call1(const char *n, uint32_t a) { uint32_t v[1] = { a }; return calln(&g_cpu, n, 1, v); }
static uint32_t call2(const char *n, uint32_t a, uint32_t b) { uint32_t v[2] = { a, b }; return calln(&g_cpu, n, 2, v); }
static uint32_t call3(const char *n, uint32_t a, uint32_t b, uint32_t c) { uint32_t v[3] = { a, b, c }; return calln(&g_cpu, n, 3, v); }
static uint32_t call4(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { uint32_t v[4] = { a, b, c, d }; return calln(&g_cpu, n, 4, v); }
static uint32_t call5(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { uint32_t v[5] = { a, b, c, d, e }; return calln(&g_cpu, n, 5, v); }
static uint32_t call7(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { uint32_t v[7] = { a, b, c, d, e, f, g }; return calln(&g_cpu, n, 7, v); }

static uint32_t guest_alloc(size_t n)
{
	uint32_t a = n2a32_mem_alloc(g_mem, (uint32_t)(n ? n : 16), 16);
	if (!a) {
		fprintf(stderr, "guest_alloc(%zu) failed\n", n);
		exit(2);
	}
	return a;
}

/* Copy a host string (and its NUL) into fresh guest memory. */
static uint32_t guest_str(const char *s)
{
	size_t n = strlen(s) + 1;
	uint32_t a = guest_alloc(n);

	memcpy(n2a32_ptr(g_mem, a), s, n);
	return a;
}

static int guest_errno(void)
{
	uint32_t loc = call0("__errno");

	return loc ? (int)n2a32_ld32(&g_cpu, loc) : -1;
}

/* ---- the fake device ------------------------------------------------------------------------------------------ */
struct fake_dev {
	const char *path;
	int opens, closes, ioctls, mmaps;
	int last_open_flags;
	uint32_t last_req, last_arg;
	int open_ret, close_ret, ioctl_ret;
	uint32_t mmap_ret;
	int mmap_err;
	uint32_t mmap_len, mmap_prot;
	uint64_t mmap_off;
	uint32_t mmap_ctx_tag;
};

static int fd_open(void *ctx, int flags)
{
	struct fake_dev *d = ctx;

	d->opens++;
	d->last_open_flags = flags;
	return d->open_ret;
}

static int fd_ioctl(void *ctx, struct n2a32_cpu *c, uint32_t req, uint32_t arg)
{
	struct fake_dev *d = ctx;

	(void)c;
	d->ioctls++;
	d->last_req = req;
	d->last_arg = arg;
	return d->ioctl_ret;
}

static uint32_t fd_mmap(void *ctx, struct n2a32_cpu *c, uint32_t len, int prot, uint64_t off, int *err)
{
	struct fake_dev *d = ctx;

	(void)c;
	d->mmaps++;
	d->mmap_len = len;
	d->mmap_prot = (uint32_t)prot;
	d->mmap_off = off;
	if (!d->mmap_ret) {
		*err = d->mmap_err;
		return 0;
	}
	return d->mmap_ret;
}

static int fd_close(void *ctx)
{
	struct fake_dev *d = ctx;

	d->closes++;
	return d->close_ret;
}

static struct fake_dev g_dev_a = { "/dev/testa", .open_ret = 0, .ioctl_ret = 0, .close_ret = 0 };
static struct fake_dev g_dev_b = { "/dev/testb", .open_ret = 0, .ioctl_ret = 0, .close_ret = 0 };

static const struct n2vfs_dev g_vdev_a = {
	"/dev/testa", fd_open, fd_ioctl, fd_mmap, fd_close, &g_dev_a
};
static const struct n2vfs_dev g_vdev_b = {
	"/dev/testb", fd_open, fd_ioctl, fd_mmap, fd_close, &g_dev_b
};

/* The registration table keeps the pointer, so the filler devices must outlive the test function. */
static struct n2vfs_dev g_fill[16];
static char g_fill_names[16][16];

/* ---- registration and fd allocation --------------------------------------------------------------------------- */
static void test_registration(void)
{
	struct n2vfs_dev bad = { NULL, fd_open, fd_ioctl, fd_mmap, fd_close, &g_dev_a };
	struct n2vfs_dev empty = { "", fd_open, fd_ioctl, fd_mmap, fd_close, &g_dev_a };
	struct n2vfs_dev dup = { "/dev/testa", fd_open, fd_ioctl, fd_mmap, fd_close, &g_dev_b };
	int i, rc;

	CHECK(n2vfs_register(NULL) == -22, "n2vfs_register(NULL) must be -EINVAL, got %d", n2vfs_register(NULL));
	CHECK(n2vfs_register(&bad) == -22, "a device without a path must be -EINVAL");
	CHECK(n2vfs_register(&empty) == -22, "an empty path must be -EINVAL");
	CHECK(n2vfs_register(&g_vdev_a) == 0, "registering /dev/testa must succeed");
	CHECK(n2vfs_register(&g_vdev_a) == -17, "registering the same device twice must be -EEXIST");
	CHECK(n2vfs_register(&dup) == -17, "a second device with the same path must be -EEXIST");
	CHECK(n2vfs_register(&g_vdev_b) == 0, "registering /dev/testb must succeed");

	/* fill the table: 2 used, 14 free, the 15th extra is refused */
	for (i = 0; i < 14; i++) {
		snprintf(g_fill_names[i], sizeof g_fill_names[i], "/dev/fill%d", i);
		g_fill[i].path = g_fill_names[i];
		g_fill[i].open = fd_open;
		g_fill[i].ioctl = fd_ioctl;
		g_fill[i].mmap = fd_mmap;
		g_fill[i].close = fd_close;
		g_fill[i].ctx = &g_dev_a;
		rc = n2vfs_register(&g_fill[i]);
		CHECK(rc == 0, "filling slot %d must succeed, got %d", i, rc);
	}
	memset(&g_fill[15], 0, sizeof g_fill[15]);
	g_fill[15].path = "/dev/overflow";
	g_fill[15].open = fd_open;
	g_fill[15].ctx = &g_dev_a;
	CHECK(n2vfs_register(&g_fill[15]) == -28, "a 17th device must be -ENOSPC, got %d",
	      n2vfs_register(&g_fill[15]));
}

static void test_device_fd(void)
{
	uint32_t p = guest_str("/dev/testa");
	uint32_t fd1, fd2, fd3, buf;
	int before = g_dev_a.opens;

	g_dev_a.open_ret = 0;
	fd1 = call2("open", p, 2 /* O_RDWR */);
	CHECK(fd1 == 100, "first guest fd must be 100, got %u", fd1);
	CHECK(g_dev_a.opens == before + 1, "dev->open must be called once");
	CHECK(g_dev_a.last_open_flags == 2, "O_RDWR must reach dev->open, got %d", g_dev_a.last_open_flags);

	fd2 = call2("__open_2", p, 0);
	CHECK(fd2 == 101, "second guest fd must be 101, got %u", fd2);

	/* ioctl pass-through, both conventions */
	g_dev_a.ioctl_ret = 0x1234;
	CHECK(call3("ioctl", fd1, 0xdeadbeef, 0x11223344) == 0x1234, "a positive ioctl result is passed through");
	CHECK(g_dev_a.last_req == 0xdeadbeef && g_dev_a.last_arg == 0x11223344, "ioctl request/arg must arrive");
	g_dev_a.ioctl_ret = -22;
	CHECK(call3("ioctl", fd1, 1, 2) == 0xffffffffu, "a negative ioctl result must give the guest -1");
	CHECK(guest_errno() == 22, "and errno = EINVAL, got %d", guest_errno());

	/* open failure is -errno, never a fd */
	g_dev_a.open_ret = -16;
	CHECK(call2("open", p, 0) == 0xffffffffu, "a failing dev->open must give the guest -1");
	CHECK(guest_errno() == 16, "and errno = EBUSY, got %d", guest_errno());
	g_dev_a.open_ret = 0;

	/* read/write on a device: the frozen struct has no callbacks for them */
	buf = guest_alloc(16);
	CHECK(call3("read", fd1, buf, 16) == 0xffffffffu, "read on a device fd must fail");
	CHECK(guest_errno() == 22, "read on a device fd sets EINVAL, got %d", guest_errno());
	CHECK(call3("write", fd1, buf, 4) == 0xffffffffu, "write on a device fd must fail");
	CHECK(guest_errno() == 22, "write on a device fd sets EINVAL, got %d", guest_errno());

	/* close */
	CHECK(call1("close", fd1) == 0, "closing fd 100 must succeed");
	CHECK(g_dev_a.closes == 1, "dev->close must be called once");
	CHECK(call1("close", fd1) == 0xffffffffu, "closing it twice must fail");
	CHECK(guest_errno() == 9, "double close sets EBADF, got %d", guest_errno());
	CHECK(call1("close", 999) == 0xffffffffu, "closing an unknown fd must fail");
	CHECK(guest_errno() == 9, "unknown close sets EBADF, got %d", guest_errno());

	/* a device is matched by exact path only */
	CHECK(call2("open", guest_str("/dev/testa/"), 0) == 0xffffffffu, "a longer path must not match /dev/testa");
	CHECK(guest_errno() == 2, "unknown path is ENOENT, got %d", guest_errno());

	CHECK(call1("close", fd2) == 0, "close fd 101");
	fd3 = call2("open", guest_str("/dev/testb"), 0);
	CHECK(fd3 == 100, "the freed lowest slot is reused, got %u", fd3);
	CHECK(call1("close", fd3) == 0, "close testb");
}

/* ---- virtual files -------------------------------------------------------------------------------------------- */
static int read_file(const char *path, uint32_t buf, uint32_t n)
{
	uint32_t fd = call2("open", guest_str(path), 0);
	int r;

	if (fd == 0xffffffffu)
		return -1;
	r = (int)call3("read", fd, buf, n);
	call1("close", fd);
	return r;
}

static int count_substr(const char *hay, const char *needle)
{
	int n = 0;
	const char *p = hay;

	while ((p = strstr(p, needle)) != NULL) {
		n++;
		p += strlen(needle);
	}
	return n;
}

static void test_virtual_files(void)
{
	uint32_t buf = guest_alloc(4096);
	int n;
	uint32_t fd;
	char *text;

	/* /proc/cpuinfo: the MT6893 layout from mt6893.dts */
	n = read_file("/proc/cpuinfo", buf, 4096);
	CHECK(n > 1000, "/proc/cpuinfo must be longer than 1000 bytes, got %d", n);
	if (n > 0) {
		text = (char *)n2a32_ptr(g_mem, buf);
		CHECK(count_substr(text, "processor\t: ") == 8, "cpuinfo must describe 8 processors");
		CHECK(count_substr(text, "CPU part\t: 0xd05") == 4, "cpus 0-3 are Cortex-A55 (0xd05)");
		CHECK(count_substr(text, "CPU part\t: 0xd41") == 4, "cpus 4-7 are Cortex-A78 (0xd41)");
		CHECK(strstr(text, "CPU implementer\t: 0x41") != NULL, "implementer 0x41 (ARM)");
	}

	/* partial reads and EOF */
	fd = call2("open", guest_str("/proc/cpuinfo"), 0);
	CHECK(fd >= 100, "cpuinfo opens");
	CHECK(call3("read", fd, buf, 10) == 10, "a 10-byte read returns 10");
	CHECK(!memcmp(n2a32_ptr(g_mem, buf), "processor\t", 10), "and the content starts with processor");
	{
		int total = 10;

		for (;;) {
			int r = (int)call3("read", fd, buf, 4096);

			if (r <= 0)
				break;
			total += r;
		}
		CHECK(total == n, "reading in chunks gives the same length (%d vs %d)", total, n);
	}
	CHECK(call3("read", fd, buf, 16) == 0, "a read at EOF returns 0");
	CHECK(call1("close", fd) == 0, "close cpuinfo");

	/* write access is refused: the table is read-only */
	fd = call2("open", guest_str("/proc/cpuinfo"), 1 /* O_WRONLY */);
	CHECK(fd == 0xffffffffu, "opening a virtual file O_WRONLY must fail");
	CHECK(guest_errno() == 13, "with EACCES, got %d", guest_errno());
	fd = call2("open", guest_str("/proc/cpuinfo"), 2 /* O_RDWR */);
	CHECK(fd == 0xffffffffu, "opening a virtual file O_RDWR must fail");
	CHECK(guest_errno() == 13, "with EACCES, got %d", guest_errno());

	/* ioctl on a virtual file */
	fd = call2("open", guest_str("/proc/cpuinfo"), 0);
	CHECK(call3("ioctl", fd, 1, 0) == 0xffffffffu, "ioctl on a virtual file must fail");
	CHECK(guest_errno() == 25, "with ENOTTY, got %d", guest_errno());
	CHECK(call3("write", fd, buf, 4) == 0xffffffffu, "write on a read-only virtual file must fail");
	CHECK(guest_errno() == 9, "with EBADF, got %d", guest_errno());
	CHECK(call1("close", fd) == 0, "close");

	/* /proc/device-tree/chosen/atag,devinfo: struct devinfo_tag */
	{
		uint32_t raw = guest_alloc(1024);
		int m = read_file("/proc/device-tree/chosen/atag,devinfo", raw, 1024);

		CHECK(m == 884, "the devinfo property is 4 + 220*4 = 884 bytes, got %d", m);
		CHECK(n2a32_ld32(&g_cpu, raw) == 220, "data_size is 220 words, got %u", n2a32_ld32(&g_cpu, raw));
		CHECK(n2a32_ld32(&g_cpu, raw + 0x10) == 0x80000000u,
		      "data[3] (the eFuse capability word at offset 0x10) is 0x80000000, got 0x%08x",
		      n2a32_ld32(&g_cpu, raw + 0x10));
		CHECK(n2a32_ld32(&g_cpu, raw + 4) == 0, "data[0] is zero");
	}

	/* the exact QueryCapability() sequence of libvcodecdrv.so: open, one read of 0x194 bytes, use buf+0x10 */
	{
		uint32_t raw = guest_alloc(0x200);
		uint32_t fd = call2("open", guest_str("/proc/device-tree/chosen/atag,devinfo"), 0);
		int m = (int)call3("read", fd, raw, 0x194);

		CHECK(m == 0x194, "a 0x194-byte read returns 404 bytes (the whole property is 884), got %d", m);
		CHECK(n2a32_ld32(&g_cpu, raw + 0x10) == 0x80000000u,
		      "the eFuse word at buf+0x10 is 0x80000000, got 0x%08x", n2a32_ld32(&g_cpu, raw + 0x10));
		CHECK(call1("close", fd) == 0, "close devinfo");
	}

	/* the CPU topology files the OAL reads before it will report CPU information */
	n = read_file("/sys/devices/system/cpu/cpu0/topology/physical_package_id", buf, 16);
	CHECK(n == 2 && !memcmp(n2a32_ptr(g_mem, buf), "0\n", 2), "cpu0 package id 0");
	n = read_file("/sys/devices/system/cpu/cpu4/topology/physical_package_id", buf, 16);
	CHECK(n == 2 && !memcmp(n2a32_ptr(g_mem, buf), "1\n", 2), "cpu4 package id 1");
	n = read_file("/sys/devices/system/cpu/cpu7/topology/physical_package_id", buf, 16);
	CHECK(n == 2 && !memcmp(n2a32_ptr(g_mem, buf), "2\n", 2), "cpu7 package id 2");
	n = read_file("/sys/devices/system/cpu/cpu3/of_node/compatible", buf, 64);
	CHECK(n == 15 && !memcmp(n2a32_ptr(g_mem, buf), "arm,cortex-a55\n", 15), "cpu3 is cortex-a55");
	n = read_file("/sys/devices/system/cpu/cpu4/of_node/compatible", buf, 64);
	CHECK(n == 15 && !memcmp(n2a32_ptr(g_mem, buf), "arm,cortex-a78\n", 15), "cpu4 is cortex-a78");

	/* paths that are deliberately absent */
	CHECK(read_file("/sys/bus/platform/drivers/dev_info/dev_info", buf, 16) == -1, "dev_info is not supplied");
	CHECK(read_file("/sys/power/cpufreq_state", buf, 16) == -1, "cpufreq_state is not supplied");
	CHECK(read_file("/proc/cpuinfo2", buf, 16) == -1, "an unknown path is not a prefix match");
	CHECK(read_file("/dev/vcu", buf, 16) == -1, "/dev/vcu is a device path, not a virtual file");

	/* EMFILE: 64 descriptors, then one more */
	{
		uint32_t fds[64];
		int i, ok = 1;
		uint32_t p = guest_str("/proc/cpuinfo");

		for (i = 0; i < 64; i++) {
			fds[i] = call2("open", p, 0);
			if (fds[i] == 0xffffffffu || fds[i] != (uint32_t)(100 + i))
				ok = 0;
		}
		CHECK(ok, "64 consecutive opens give fds 100..163");
		CHECK(call2("open", p, 0) == 0xffffffffu, "the 65th open must fail");
		CHECK(guest_errno() == 24, "with EMFILE(24), got %d", guest_errno());
		for (i = 0; i < 64; i++)
			CHECK(call1("close", fds[i]) == 0, "close %d", 100 + i);
	}

	/* bad guest pointers */
	{
		uint32_t p = guest_str("/proc/cpuinfo");

		fd = call2("open", p, 0);
		CHECK(call3("read", fd, 0, 16) == 0xffffffffu, "read into NULL must fail");
		CHECK(guest_errno() == 14, "with EFAULT, got %d", guest_errno());
		CHECK(call3("read", fd, 0xffffff00u, 0x1000) == 0xffffffffu, "a read that wraps past 4 GiB must fail");
		CHECK(guest_errno() == 14, "with EFAULT, got %d", guest_errno());
		CHECK(call3("read", fd, buf, 0) == 0, "a zero-length read returns 0");
		CHECK(call2("open", 0, 0) == 0xffffffffu, "open(NULL) must fail");
		CHECK(guest_errno() == 14, "open(NULL) sets EFAULT, got %d", guest_errno());
		CHECK(call1("close", fd) == 0, "close");
	}
}


/* ---- MMIO buffers and over-long paths ------------------------------------------------------------------------ */
static uint32_t mmio_rd(void *ctx, uint32_t a, unsigned bytes)
{
	(void)ctx; (void)a; (void)bytes;
	return 0;
}

static void mmio_wr(void *ctx, uint32_t a, unsigned bytes, uint32_t v)
{
	(void)ctx; (void)a; (void)bytes; (void)v;
}

static void test_mmio_and_longpath(void)
{
	struct n2a32_mmio hook;
	uint32_t fd, p;
	char *longpath;

	memset(&hook, 0, sizeof hook);
	hook.rd = mmio_rd;
	hook.wr = mmio_wr;
	CHECK(n2a32_mem_mmio(g_mem, 0x20000000u, 1u << 20, &hook) == 0, "map one MMIO megabyte at 0x20000000");
	fd = call2("open", guest_str("/proc/cpuinfo"), 0);
	CHECK(call3("read", fd, 0x20000010u, 8) == 0xffffffffu, "read into MMIO must fail");
	CHECK(guest_errno() == 14, "with EFAULT, got %d", guest_errno());
	CHECK(call1("close", fd) == 0, "close");

	longpath = malloc(2048);
	memset(longpath, 'x', 2047);
	longpath[0] = '/';
	longpath[2047] = 0;
	p = guest_alloc(2048);
	memcpy(n2a32_ptr(g_mem, p), longpath, 2048);
	free(longpath);
	CHECK(call2("open", p, 0) == 0xffffffffu, "a 2047-byte path must fail");
	CHECK(guest_errno() == 36, "with ENAMETOOLONG(36), got %d", guest_errno());
	CHECK(call4("fopen", p, guest_str("r"), 0, 0) == 0, "fopen of a 2047-byte path returns NULL");
	CHECK(guest_errno() == 36, "fopen with the same error is ENAMETOOLONG(36) too, got %d",
	      guest_errno());
}

/* ---- mmap forwarding ------------------------------------------------------------------------------------------ */
static void test_mmap(void)
{
	uint32_t fd = call2("open", guest_str("/dev/testa"), 2);
	uint32_t p;

	g_dev_a.mmap_ret = 0x50000000u;
	g_dev_a.mmap_err = 0;
	p = n2vfs_mmap(&g_cpu, 0, 0x40000, 3, 1, (int)fd, 0x16000000ull);
	CHECK(p == 0x50000000u, "the device's guest address is passed through, got 0x%x", p);
	CHECK(g_dev_a.mmaps == 1, "dev->mmap called once");
	CHECK(g_dev_a.mmap_len == 0x40000 && g_dev_a.mmap_prot == 3 && g_dev_a.mmap_off == 0x16000000ull,
	      "len/prot/offset forwarded (%u/%u/%llu)", g_dev_a.mmap_len, g_dev_a.mmap_prot,
	      (unsigned long long)g_dev_a.mmap_off);

	g_dev_a.mmap_ret = 0;
	g_dev_a.mmap_err = 19;
	p = n2vfs_mmap(&g_cpu, 0, 4096, 3, 1, (int)fd, 0);
	CHECK(p == 0, "a failing device mmap returns 0");
	CHECK(guest_errno() == 19, "with the device's errno, got %d", guest_errno());

	p = n2vfs_mmap(&g_cpu, 0, 4096, 3, 1, 999, 0);
	CHECK(p == 0, "an unknown fd fails");
	CHECK(guest_errno() == 9, "with EBADF, got %d", guest_errno());

	/* a virtual file has no mmap */
	{
		uint32_t vfd = call2("open", guest_str("/proc/cpuinfo"), 0);

		p = n2vfs_mmap(&g_cpu, 0, 4096, 3, 1, (int)vfd, 0);
		CHECK(p == 0, "mmap of a virtual file fails");
		CHECK(guest_errno() == 19, "with ENODEV, got %d", guest_errno());
		CHECK(call1("close", vfd) == 0, "close");
	}
	CHECK(call1("close", fd) == 0, "close testa");
}

/* ---- per-thread errno ----------------------------------------------------------------------------------------- */
struct th_arg {
	struct n2a32_cpu cpu;
	uint32_t addr;
	int err_after_open;
};

static void *th_main(void *p)
{
	struct th_arg *t = p;

	memset(&t->cpu, 0, sizeof t->cpu);
	t->cpu.mem = g_mem;
	t->cpu.r[13] = n2a32_mem_alloc(g_mem, 64 * 1024, 16) + 64 * 1024;
	t->addr = calln(&t->cpu, "__errno", 0, NULL);
	(void)calln(&t->cpu, "open", 2, (uint32_t[]){ guest_str("/no/such/file"), 0 });
	t->err_after_open = (int)n2a32_ld32(&t->cpu, t->addr);
	return NULL;
}

static void test_errno_threads(void)
{
	struct th_arg t1, t2;
	pthread_t a, b;
	uint32_t loc1, loc2;

	/* the same cpu gives the same word every time */
	loc1 = call0("__errno");
	loc2 = call0("__errno");
	CHECK(loc1 != 0 && loc1 == loc2, "one cpu -> one errno word (0x%x/0x%x)", loc1, loc2);
	CHECK((loc1 & 3) == 0, "the errno word is 4-byte aligned, got 0x%x", loc1);
	n2a32_st32(&g_cpu, loc1, 12345);
	CHECK(n2a32_ld32(&g_cpu, call0("__errno")) == 12345, "the word is ordinary guest memory");
	n2a32_st32(&g_cpu, loc1, 2);

	(void)call2("open", guest_str("/no/such/main"), 0);   /* main cpu errno = ENOENT (2) */
	CHECK(guest_errno() == 2, "the main cpu starts with ENOENT");
	CHECK(pthread_create(&a, NULL, th_main, &t1) == 0, "thread 1");
	CHECK(pthread_create(&b, NULL, th_main, &t2) == 0, "thread 2");
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	CHECK(t1.addr != 0 && t2.addr != 0, "both threads have an errno word");
	CHECK(t1.addr != t2.addr, "different guest threads get different errno words (0x%x/0x%x)", t1.addr, t2.addr);
	CHECK(t1.addr != loc1 && t2.addr != loc1, "and neither shares the main cpu's word");
	CHECK(t1.err_after_open == 2 && t2.err_after_open == 2, "each thread sees its own ENOENT (2): %d/%d",
	      t1.err_after_open, t2.err_after_open);
	CHECK(guest_errno() == 2, "the main cpu's errno is untouched by the threads, got %d", guest_errno());
}

/* ---- stdio on real files --------------------------------------------------------------------------------------- */
static uint32_t g_fbuf;         /* 64 KiB guest buffer for file I/O */

static uint32_t fp_open(const char *path, const char *mode)
{
	return call4("fopen", guest_str(path), guest_str(mode), 0, 0);
}

static int fp_write_str(uint32_t fp, const char *s)
{
	uint32_t b = guest_str(s);

	return (int)call4("fwrite", b, 1, (uint32_t)strlen(s), fp);
}

/* The OAL parses every /proc/cpuinfo line with sscanf(); mirror that with V30's scanner over one host line. */
struct scan_in {
	const char *s;
	size_t i;
};

static int scan_get(void *ctx)
{
	struct scan_in *p = ctx;

	return p->s[p->i] ? (unsigned char)p->s[p->i++] : -1;
}

static void scan_unget(void *ctx, int byte)
{
	struct scan_in *p = ctx;

	(void)byte;
	if (p->i)
		p->i--;
}

static int scan_line(const char *fmt, const char *line)
{
	struct scan_in in;
	struct n2fmt_input input;
	struct n2fmt_args a;
	uint32_t out = guest_alloc(16);
	int n;

	in.s = line;
	in.i = 0;
	input.get = scan_get;
	input.unget = scan_unget;
	input.ctx = &in;
	n2a32_st32(&g_cpu, out, 0xdeadbeefu);
	n2fmt_args_regs(&a, &g_cpu, 0);
	g_cpu.r[0] = out;
	n = n2fmt_vscan(&g_cpu, &input, guest_str(fmt), &a);
	if (n != 1)
		return -1;
	return (int)n2a32_ld32(&g_cpu, out);
}

/* The OAL's getProcessor() must find 4 A55 (0xd05) and 4 A78 (0xd41) in our /proc/cpuinfo. */
static void test_cpuinfo_parse(void)
{
	uint32_t fp = fp_open("/proc/cpuinfo", "r");
	int cur = -1, a55 = 0, a78 = 0, lines = 0;

	CHECK(fp >= 3, "open /proc/cpuinfo as a stream");
	while (call3("fgets", g_fbuf, 128, fp) == g_fbuf) {
		char *line = (char *)n2a32_ptr(g_mem, g_fbuf);
		int v = scan_line("processor : %d", line);

		lines++;
		if (v >= 0) {
			cur = v;
			continue;
		}
		v = scan_line("CPU part : %x", line);
		if (v >= 0 && cur >= 0) {
			if (v == 0xd05)
				a55++;
			if (v == 0xd41)
				a78++;
		}
	}
	CHECK(lines > 40, "cpuinfo has the usual per-cpu lines, got %d", lines);
	CHECK(a55 == 4 && a78 == 4, "the OAL parser sees 4x 0xd05 and 4x 0xd41 (got %d/%d)", a55, a78);
	CHECK(call1("fclose", fp) == 0, "close");

	/* the A78 fallback path: fscanf("arm,cortex-a%d") on of_node/compatible */
	{
		uint32_t raw = guest_alloc(64);
		int n = read_file("/sys/devices/system/cpu/cpu7/of_node/compatible", raw, 64);

		CHECK(n > 0, "read cpu7 compatible");
		CHECK(scan_line("arm,cortex-a%d", (char *)n2a32_ptr(g_mem, raw)) == 78,
		      "the compatibility string names cortex-a78");
	}
}

/* Guest files that really exist live below $N2HW_DUMP_DIR: the guest path /data/local/tmp/X is the host
 * path /tmp/n2hw-v33/dump/data/local/tmp/X.  No other guest path may reach a host file (R1 F18). */
#define V33_DUMP "/tmp/n2hw-v33/dump"
#define V33_GUEST_DIR "/data/local/tmp"

/* vpud remap_shmem(): fgets each line, keep the first with BOTH "rw-p" and "libvpud_vcodec.so", sscanf
 * "%x-%x" it.  This is the exact parse, through V30's scanner, over the content we set with n2vfs_set_maps(). */
static int scan_range(const char *line, uint32_t *start, uint32_t *end)
{
	struct scan_in in;
	struct n2fmt_input input;
	struct n2fmt_args a;
	uint32_t outs = guest_alloc(16);
	int n;

	in.s = line;
	in.i = 0;
	input.get = scan_get;
	input.unget = scan_unget;
	input.ctx = &in;
	n2a32_st32(&g_cpu, outs, 0);
	n2a32_st32(&g_cpu, outs + 4, 0);
	n2fmt_args_regs(&a, &g_cpu, 0);
	g_cpu.r[0] = outs;
	g_cpu.r[1] = outs + 4;
	n = n2fmt_vscan(&g_cpu, &input, guest_str("%x-%x"), &a);
	if (n != 2)
		return 0;
	*start = n2a32_ld32(&g_cpu, outs);
	*end = n2a32_ld32(&g_cpu, outs + 4);
	return 1;
}

static void test_maps(void)
{
	static const char maps[] =
		"40080000-40087230 r-xp 00000000 00:00 0 /vendor/bin/vpud\n"
		"40087230-4008732c rw-p 00000000 00:00 0 /vendor/bin/vpud\n"
		"4009d980-4009dd70 rw-p 00000000 00:00 0 /vendor/lib/libvpud_vcodec.so\n"
		"4008f230-4009c980 r-xp 00000000 00:00 0 /vendor/lib/libvpud_vcodec.so\n";
	uint32_t fp, start = 0, end = 0;
	char *line;
	int found = 0;

	/* not set yet: the file does not exist */
	CHECK(read_file("/proc/self/maps", g_fbuf, 256) == -1, "/proc/self/maps is ENOENT before n2vfs_set_maps");
	CHECK(fp_open("/proc/self/maps", "r") == 0, "and fopen agrees");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());

	CHECK(n2vfs_set_maps(maps, (uint32_t)strlen(maps)) == 0, "n2vfs_set_maps accepts the map list");
	fp = fp_open("/proc/self/maps", "r");
	CHECK(fp >= 3, "fopen of /proc/self/maps now works");
	while (call3("fgets", g_fbuf, 128, fp) == g_fbuf) {
		line = (char *)n2a32_ptr(g_mem, g_fbuf);
		if (strstr(line, "rw-p") && strstr(line, "libvpud_vcodec.so") && !found) {
			CHECK(scan_range(line, &start, &end) == 1, "the vpud line scans as two hex words");
			found = 1;
		}
	}
	CHECK(found == 1, "vpud finds its rw-p libvpud_vcodec.so line");
	CHECK(start == 0x4009d980u && end == 0x4009dd70u,
	      "the range is 4009d980-4009dd70 as set (got %x-%x)", start, end);
	CHECK(call1("fclose", fp) == 0, "close");

	/* a second open starts at the beginning again */
	CHECK(read_file("/proc/self/maps", g_fbuf, 256) == (int)strlen(maps), "a full read gives the whole text");

	/* set_maps(NULL,0) removes the file again; an oversized text is refused */
	CHECK(n2vfs_set_maps(NULL, 0) == 0, "n2vfs_set_maps(NULL,0) clears it");
	CHECK(read_file("/proc/self/maps", g_fbuf, 256) == -1, "and the file is ENOENT again");
	{
		char *big = malloc(20000);

		memset(big, 'x', 20000);
		CHECK(n2vfs_set_maps(big, 20000) == -22, "a text above the 16 KiB limit is -EINVAL");
		free(big);
	}

	/* the platform model file that drv_getchipname() requires (it exit(1)s without it) */
	{
		int n = read_file("/proc/device-tree/model", g_fbuf, 64);

		CHECK(n == 7 && !memcmp(n2a32_ptr(g_mem, g_fbuf), "MT6893\n", 7),
		      "/proc/device-tree/model is \"MT6893\\n\", got %d", n);
	}
}

static void test_stdio_files(void)
{
	char path[256];
	uint32_t fp;
	char *g;
	int n;

	unsetenv("N2HW_DUMP_DIR");
	/* R1 F18: a guest fopen() must never open a host file */
	CHECK(fp_open("/proc/self/maps", "r") == 0, "fopen of /proc/self/maps returns NULL (no host maps)");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(fp_open("/etc/passwd", "r") == 0, "fopen of /etc/passwd returns NULL");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(call2("open", guest_str("/proc/self/maps"), 0) == 0xffffffffu, "open of /proc/self/maps fails too");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(fp_open("/tmp/n2hw-v33/t_denied.txt", "w") == 0, "fopen(\"w\") of a host path is refused");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(access("/tmp/n2hw-v33/t_denied.txt", F_OK) != 0, "and no host file was created");
	setenv("N2HW_DUMP_DIR", V33_DUMP, 1);
	CHECK(fp_open("/proc/self/maps", "r") == 0, "even with the dump dir set, /proc/self/maps stays ENOENT");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(fp_open("/data/../../etc/passwd", "r") == 0, "a .. component cannot walk out of the dump tree");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(fp_open("/data/./local/tmp/v33.txt", "r") == 0, "a . component is refused too");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());

	/* write with fprintf/fwrite, read back with fgets */
	snprintf(path, sizeof path, V33_GUEST_DIR "/t_basic.txt");
	fp = fp_open(path, "w");
	CHECK(fp >= 3 && fp < 64, "fopen(\"w\") gives a FILE* in 3..63, got %u", fp);
	n = (int)call7("fprintf", fp, guest_str("%d %s %d %s %d"), 42, guest_str("hello"), 7,
		       guest_str("world"), 9);
	CHECK(n == 18, "fprintf with five arguments (two on the guest stack) returns 18, got %d", n);
	CHECK(fp_write_str(fp, "\nsecond line\n") == 13, "fwrite writes 13 bytes");
	CHECK(call1("fflush", fp) == 0, "fflush returns 0");
	CHECK(call1("fileno", fp) >= 100, "fileno gives the guest fd (>= 100)");
	CHECK(call1("fsync", call1("fileno", fp)) == 0, "fsync of that fd returns 0");
	CHECK(call1("fclose", fp) == 0, "fclose returns 0");

	fp = fp_open(path, "r");
	CHECK(fp >= 3, "fopen(\"r\")");
	CHECK(call3("fgets", g_fbuf, 64, fp) == g_fbuf, "fgets returns the buffer");
	g = (char *)n2a32_ptr(g_mem, g_fbuf);
	CHECK(!strcmp(g, "42 hello 7 world 9\n"), "the first line is exact: %.40s", g);
	CHECK(call1("feof", fp) == 0, "not at EOF after the first line");
	CHECK(call3("fgets", g_fbuf, 64, fp) == g_fbuf, "second line");
	CHECK(!strcmp((char *)n2a32_ptr(g_mem, g_fbuf), "second line\n"), "second line text");
	CHECK(call1("feof", fp) == 0, "not at EOF yet");
	CHECK(call3("fgets", g_fbuf, 64, fp) == 0, "fgets at EOF returns NULL");
	CHECK(call1("feof", fp) == 1, "feof is now true");
	CHECK(call1("fclose", fp) == 0, "fclose");

	/* fread / fseek / __fread_chk on binary data */
	snprintf(path, sizeof path, V33_GUEST_DIR "/t_bin.txt");
	fp = fp_open(path, "w");
	CHECK(fp_write_str(fp, "0123456789") == 10, "write 10 bytes");
	CHECK(call1("fclose", fp) == 0, "close");

	fp = fp_open(path, "r");
	CHECK(call4("fread", g_fbuf, 1, 4, fp) == 4, "fread 4 elements of 1");
	CHECK(!memcmp(n2a32_ptr(g_mem, g_fbuf), "0123", 4), "fread content");
	CHECK(call3("fseek", fp, 5, 0) == 0, "fseek(5, SEEK_SET)");
	CHECK(call4("fread", g_fbuf, 2, 2, fp) == 2, "fread 2 elements of 2");
	CHECK(!memcmp(n2a32_ptr(g_mem, g_fbuf), "56", 2), "after the seek we read from offset 5");
	CHECK(call3("fseek", fp, 0xfffffffeu /* -2 */, 2 /* SEEK_END */) == 0, "fseek(-2, SEEK_END)");
	CHECK(call4("fread", g_fbuf, 1, 8, fp) == 2, "two bytes left");
	CHECK(!memcmp(n2a32_ptr(g_mem, g_fbuf), "89", 2), "the last two bytes");
	CHECK(call1("fclose", fp) == 0, "close");

	/* __fread_chk(dst, size, nmemb, fp, dst_len): the fifth word is on the guest stack */
	fp = fp_open(path, "r");
	CHECK(call5("__fread_chk", g_fbuf, 1, 10, fp, 16) == 10, "__fread_chk with dst_len 16 accepts 10 bytes");
	CHECK(call3("fseek", fp, 0, 0) == 0, "rewind");
	n2shim_stdio_exit_clear();
	g_cpu.stop_request = 0;
	CHECK(call5("__fread_chk", g_fbuf, 2, 100, fp, 8) == 0, "__fread_chk refuses to overflow dst_len");
	CHECK(n2shim_stdio_exit_reason() == 4, "and records reason 4 (fread_chk), got %d",
	      n2shim_stdio_exit_reason());
	CHECK(g_cpu.stop_request == 1, "and asks the cpu to stop");
	g_cpu.stop_request = 0;
	n2shim_stdio_exit_clear();
	CHECK(call1("fclose", fp) == 0, "close");

	/* a conversion longer than the 512-byte stack buffer takes the heap path.  A 600-character %s
	 * (not %0600d: V30's zero-flag padding has its own overflow, reported to hw-v30 on the board). */
	snprintf(path, sizeof path, V33_GUEST_DIR "/t_long.txt");
	fp = fp_open(path, "w");
	{
		uint32_t longstr = guest_alloc(601);
		uint32_t base = longstr;

		memset(n2a32_ptr(g_mem, base), 'A', 600);
		n2a32_st8(&g_cpu, base + 600, 0);
		n = (int)call7("fprintf", fp, guest_str("%s"), base, 0, 0, 0, 0);
	}
	CHECK(n == 600, "a 600-byte conversion goes through the heap buffer, got %d", n);
	CHECK(call1("fclose", fp) == 0, "close");
	{
		struct stat st;

		CHECK(stat(V33_DUMP V33_GUEST_DIR "/t_long.txt", &st) == 0 && st.st_size == 600,
		      "the file holds 600 bytes");
	}

	/* fscanf through the same FILE table */
	snprintf(path, sizeof path, V33_GUEST_DIR "/t_scan.txt");
	fp = fp_open(path, "w");
	fp_write_str(fp, "17 0x2a hello\n");
	call1("fclose", fp);
	fp = fp_open(path, "r");
	{
		uint32_t out = guest_alloc(64);
		uint32_t pint = guest_alloc(16), px = guest_alloc(16);

		n = (int)call7("fscanf", fp, guest_str("%d %x %s"), pint, px, out, 0, 0);
		CHECK(n == 3, "fscanf returns 3 assignments, got %d", n);
		CHECK(n2a32_ld32(&g_cpu, pint) == 17, "%%d read 17, got %d", (int)n2a32_ld32(&g_cpu, pint));
		CHECK(n2a32_ld32(&g_cpu, px) == 0x2a, "%%x read 0x2a, got 0x%x", n2a32_ld32(&g_cpu, px));
		CHECK(!strcmp((char *)n2a32_ptr(g_mem, out), "hello"), "%%s read hello");
	}
	CHECK(call1("fclose", fp) == 0, "close");

	/* fopen() dispatches to a registered device by exact path before anything else */
	{
		int before = g_dev_a.opens;
		uint32_t dfp = fp_open("/dev/testa", "r");

		CHECK(dfp >= 3, "fopen of a registered device path gives a stream");
		CHECK(g_dev_a.opens == before + 1, "and went to dev->open");
		CHECK(call1("fclose", dfp) == 0, "close it");
	}

	/* a bad mode and a NULL path */
	CHECK(fp_open(path, "q") == 0, "fopen with an invalid mode returns NULL");
	CHECK(guest_errno() == 22, "with EINVAL, got %d", guest_errno());
	CHECK(call4("fopen", 0, guest_str("r"), 0, 0) == 0, "fopen(NULL) returns NULL");
	CHECK(guest_errno() == 14, "with EFAULT, got %d", guest_errno());

	/* fclose of the std streams is a no-op */
	CHECK(call1("fclose", 1) == 0, "fclose(stdout) returns 0");
	CHECK(call1("fclose", 2) == 0, "fclose(stderr) returns 0");
	CHECK(call1("feof", 1) == 0, "feof(stdout) is false");
	CHECK(call1("fileno", 1) == 1, "fileno(stdout) = 1");
	CHECK(call1("fileno", 2) == 2, "fileno(stderr) = 2");
	CHECK(call1("fflush", 0) == 0, "fflush(NULL) returns 0");
	CHECK(call1("feof", 999) == 0, "feof of an unknown FILE* is false");

	/* an unknown FILE* fails cleanly */
	CHECK(call3("fgets", g_fbuf, 16, 999) == 0, "fgets on an unknown FILE* returns NULL");
	CHECK(guest_errno() == 9, "with EBADF, got %d", guest_errno());
	CHECK(call4("fread", g_fbuf, 1, 4, 999) == 0xffffffffu, "fread on an unknown FILE* fails");
	CHECK(guest_errno() == 9, "with EBADF, got %d", guest_errno());

	/* many streams: slot 63 is the last */
	{
		uint32_t fds[64];
		int i, ok = 1;
		const char *pth = V33_GUEST_DIR "/t_basic.txt";

		for (i = 0; i < 61; i++) {
			fds[i] = fp_open(pth, "r");
			if (fds[i] != (uint32_t)(3 + i))
				ok = 0;
		}
		CHECK(ok, "streams are allocated 3..63");
		CHECK(fp_open(pth, "r") == 0, "the 62nd stream fails");
		CHECK(guest_errno() == 24, "with EMFILE, got %d", guest_errno());
		for (i = 0; i < 61; i++)
			call1("fclose", fds[i]);
	}
}

/* ---- /data and /sdcard ----------------------------------------------------------------------------------------- */
static void test_dump_dir(void)
{
	char path[512];
	FILE *h;
	char line[64];

	unsetenv("N2HW_DUMP_DIR");
	CHECK(fp_open("/data/local/tmp/v33.txt", "w") == 0, "fopen of /data without N2HW_DUMP_DIR fails");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(fp_open("/sdcard/v33.txt", "w") == 0, "fopen of /sdcard without N2HW_DUMP_DIR fails");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	CHECK(fp_open("/database", "r") == 0, "the prefix test is component-wise: /database is not /data");

	setenv("N2HW_DUMP_DIR", "/tmp/n2hw-v33/dump", 1);
	{
		uint32_t fp = fp_open("/data/local/tmp/v33.txt", "w");

		CHECK(fp >= 3, "with N2HW_DUMP_DIR the path is mapped below it");
		if (fp >= 3) {
			CHECK(fp_write_str(fp, "mapped\n") == 7, "write through the mapped file");
			CHECK(call1("fclose", fp) == 0, "close");
		}
	}
	snprintf(path, sizeof path, "/tmp/n2hw-v33/dump/data/local/tmp/v33.txt");
	h = fopen(path, "r");
	CHECK(h != NULL, "the file exists at $N2HW_DUMP_DIR/data/local/tmp/v33.txt");
	if (h) {
		CHECK(fgets(line, sizeof line, h) != NULL && !strcmp(line, "mapped\n"), "with the written content");
		fclose(h);
	}
	{
		uint32_t fp = fp_open("/sdcard/v33.txt", "w");

		CHECK(fp >= 3, "/sdcard is mapped too");
		if (fp >= 3)
			call1("fclose", fp);
	}
	CHECK(access("/tmp/n2hw-v33/dump/sdcard/v33.txt", F_OK) == 0, "and the file is under the dump directory");
	CHECK(fp_open("/data/local/tmp/missing/sub/x", "r") == 0, "a missing subdirectory is ENOENT");
	CHECK(guest_errno() == 2, "with ENOENT, got %d", guest_errno());
	unsetenv("N2HW_DUMP_DIR");
}

/* ---- the log callback ------------------------------------------------------------------------------------------ */
static char g_log_text[4096];
static char g_log_tag[32];
static int g_log_prio, g_log_calls;

static void log_rec(int prio, const char *tag, const char *text)
{
	g_log_calls++;
	g_log_prio = prio;
	snprintf(g_log_tag, sizeof g_log_tag, "%s", tag ? tag : "(null)");
	snprintf(g_log_text, sizeof g_log_text, "%s", text ? text : "(null)");
}

static void test_log(void)
{
	uint32_t b;

	g_log_calls = 0;
	n2shim_log = log_rec;

	CHECK(call2("printf", guest_str("x=%d\n"), 7) == 4, "printf returns the length");
	CHECK(g_log_calls == 1 && !strcmp(g_log_text, "x=7\n"), "printf reaches the log, got \"%s\"", g_log_text);
	CHECK(g_log_prio == 4 && !strcmp(g_log_tag, "stdout"), "stdout priority 4, tag stdout (%d/%s)",
	      g_log_prio, g_log_tag);

	CHECK(call7("fprintf", 2, guest_str("e:%s:%d\n"), guest_str("tag"), 9, 0, 0, 0) == 8,
	      "fprintf(stderr) returns the length");
	CHECK(g_log_calls == 2 && !strcmp(g_log_text, "e:tag:9\n"), "fprintf(stderr) reaches the log, got \"%s\"",
	      g_log_text);
	CHECK(g_log_prio == 6 && !strcmp(g_log_tag, "stderr"), "stderr priority 6, tag stderr (%d/%s)",
	      g_log_prio, g_log_tag);

	CHECK(call1("puts", guest_str("hi")) == 0, "puts returns 0");
	CHECK(!strcmp(g_log_text, "hi\n"), "puts appends a newline, got \"%s\"", g_log_text);

	b = guest_str("abc");
	CHECK(call3("write", 1, b, 3) == 3, "write(1, ...) returns 3");
	CHECK(!strcmp(g_log_text, "abc"), "and reaches the log, got \"%s\"", g_log_text);
	CHECK(call3("write", 2, b, 3) == 3, "write(2, ...) returns 3");
	CHECK(g_log_prio == 6 && !strcmp(g_log_tag, "stderr"), "with stderr priority/tag");

	/* the stderr data symbol from V34: value 2 works, and the address can be registered too */
	{
		uint32_t cell = guest_alloc(8);

		n2a32_st32(&g_cpu, cell, 2);
		CHECK(call7("fprintf", n2a32_ld32(&g_cpu, cell), guest_str("s\n"), 0, 0, 0, 0, 0) == 2,
		      "the value of the stderr cell is a FILE*");
		n2shim_stdio_set_stderr_symbol(cell);
		CHECK(call7("fprintf", cell, guest_str("a\n"), 0, 0, 0, 0, 0) == 2,
		      "and so is the address of the cell");
		CHECK(g_log_prio == 6 && !strcmp(g_log_tag, "stderr"), "both reach stderr");
		n2shim_stdio_set_stderr_symbol(0);
	}

	/* no callback: the call still succeeds and nothing is printed */
	n2shim_log = NULL;
	g_log_calls = 0;
	CHECK(call2("printf", guest_str("quiet %d\n"), 1) == 8, "printf works without a log callback");
	CHECK(g_log_calls == 0, "and does not call anything");
}

/* ---- clock_gettime / gettimeofday ------------------------------------------------------------------------------ */
static void test_time(void)
{
	uint32_t ts1 = guest_alloc(16), ts2 = guest_alloc(16), tv = guest_alloc(16);
	uint32_t sec1, nsec1, sec2, nsec2;
	struct timespec host;

	CHECK(call2("clock_gettime", 1 /* CLOCK_MONOTONIC */, ts1) == 0, "clock_gettime(MONOTONIC)");
	CHECK(call2("clock_gettime", 1, ts2) == 0, "clock_gettime(MONOTONIC) again");
	sec1 = n2a32_ld32(&g_cpu, ts1);
	nsec1 = n2a32_ld32(&g_cpu, ts1 + 4);
	sec2 = n2a32_ld32(&g_cpu, ts2);
	nsec2 = n2a32_ld32(&g_cpu, ts2 + 4);
	CHECK(nsec1 < 1000000000u, "tv_nsec < 1e9, got %u", nsec1);
	CHECK((int64_t)sec2 * 1000000000ll + nsec2 >= (int64_t)sec1 * 1000000000ll + nsec1,
	      "monotonic time does not go backwards");

	CHECK(call2("clock_gettime", 0 /* CLOCK_REALTIME */, ts1) == 0, "clock_gettime(REALTIME)");
	clock_gettime(CLOCK_REALTIME, &host);
	sec1 = n2a32_ld32(&g_cpu, ts1);
	CHECK((int64_t)sec1 - (int64_t)host.tv_sec <= 5 && (int64_t)host.tv_sec - (int64_t)sec1 <= 5,
	      "the guest's realtime is within 5 s of the host (%u vs %lld)", sec1, (long long)host.tv_sec);

	CHECK(call2("clock_gettime", 99, ts1) == 0xffffffffu, "an unknown clock id fails");
	CHECK(guest_errno() == 22, "with EINVAL, got %d", guest_errno());
	CHECK(call2("clock_gettime", 1, 0) == 0xffffffffu, "a NULL timespec fails");
	CHECK(guest_errno() == 14, "with EFAULT, got %d", guest_errno());

	CHECK(call2("gettimeofday", tv, 0) == 0, "gettimeofday");
	sec1 = n2a32_ld32(&g_cpu, tv);
	nsec1 = n2a32_ld32(&g_cpu, tv + 4);
	clock_gettime(CLOCK_REALTIME, &host);
	CHECK(nsec1 < 1000000u, "tv_usec < 1e6, got %u", nsec1);
	CHECK((int64_t)sec1 - (int64_t)host.tv_sec <= 5 && (int64_t)host.tv_sec - (int64_t)sec1 <= 5,
	      "gettimeofday is within 5 s of the host");
	CHECK(call2("gettimeofday", 0, 0) == 0xffffffffu, "gettimeofday(NULL) fails");
	CHECK(guest_errno() == 14, "with EFAULT, got %d", guest_errno());
}

/* ---- exit / abort / __stack_chk_fail --------------------------------------------------------------------------- */
static void test_exit(void)
{
	n2shim_stdio_exit_clear();
	g_cpu.stop_request = 0;
	g_cpu.r[15] = 0x00401234;
	CHECK(call1("exit", 3) == 0, "exit returns nothing meaningful but must not leave the host");
	CHECK(g_cpu.stop_request == 1, "exit sets stop_request on the calling cpu");
	CHECK(n2shim_stdio_exit_reason() == 1, "reason EXIT, got %d", n2shim_stdio_exit_reason());
	CHECK(n2shim_stdio_exit_status() == 3, "status 3, got %d", n2shim_stdio_exit_status());
	CHECK(n2shim_stdio_exit_pc() == 0x00401234u, "the pc of the call is recorded, got 0x%x", n2shim_stdio_exit_pc());

	g_cpu.stop_request = 0;
	n2shim_stdio_exit_clear();
	CHECK(call0("abort") == 0, "abort returns to the caller");
	CHECK(g_cpu.stop_request == 1 && n2shim_stdio_exit_reason() == 2, "abort stops the cpu with reason 2");

	g_cpu.stop_request = 0;
	n2shim_stdio_exit_clear();
	CHECK(call0("__stack_chk_fail") == 0, "__stack_chk_fail returns to the caller");
	CHECK(g_cpu.stop_request == 1 && n2shim_stdio_exit_reason() == 3,
	      "stack check failure stops the cpu with reason 3");

	g_cpu.stop_request = 0;
	n2shim_stdio_exit_clear();
}

/* ---- main ------------------------------------------------------------------------------------------------------ */
int main(void)
{
	guest_init();
	g_fbuf = guest_alloc(64 * 1024);

	test_registration();
	test_device_fd();
	test_virtual_files();
	test_cpuinfo_parse();
	test_maps();
	test_mmio_and_longpath();
	test_mmap();
	test_errno_threads();
	test_stdio_files();
	test_dump_dir();
	test_log();
	test_time();
	test_exit();

	if (g_fail) {
		printf("N2TEST V33 FAIL %d checks failed (%d passed)\n", g_fail, g_pass);
		return 1;
	}
	printf("N2TEST V33 PASS %d/%d\n", g_pass, g_pass + g_fail);
	return 0;
}
