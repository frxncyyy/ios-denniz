/* SPDX-License-Identifier: MIT */
/*
 * n2vfs.c -- the guest's files: file descriptors, the device table, /proc and /sys as read-only data.
 * Package V33 of gpu/hwcodec (owner hw-v33).  Host-only so far: this file has never run on the phone.
 *
 * What the vendor set does (facts: gpu/hwcodec/FACTS.md 5.6a, facts/vcu_usage.md 1):
 *   open("/dev/vcu", O_RDWR)                          libvpud_vcodec.so, the daemon's device
 *   open("/dev/vpu", O_RDWR)                          libvpud_vcodec.so decoder path (no driver on Android)
 *   open("/proc/device-tree/chosen/atag,devinfo")     libvcodecdrv.so QueryCapability (read, 0x194 bytes)
 *   open("/proc/mcdi/sodi_en", O_WRONLY) + write      libvcodec_utility.so eHalMCDICtrl
 *   open("/sys/power/cpufreq_state", O_WRONLY)+write  libvcodec_utility.so eHalDVFSDCMCtrl
 *   open("/sys/bus/platform/drivers/mem_bw_ctrl/concurrency_scenario")  eVideoBWLimit
 *   /proc/cpuinfo, /sys/devices/system/cpu/...        libvcodec_oal.so getProcessor / CPU-perf query
 *
 * The design (work_packages.json V33):
 *   - guest fd numbers start at 100 (VF_FD_FIRST).  A descriptor is one of
 *       device    a struct n2vfs_dev registered by n2vfs_register(), matched by exact path;
 *       virtual   one of the static read-only files in vf_table[] below;
 *       host      a real file below $N2HW_DUMP_DIR, reachable only through fopen() and only for a guest path
 *                 under /data or /sdcard (the guest path is appended below the dump directory).  A guest
 *                 path outside those two roots never becomes a host path: a guest fopen("/proc/self/maps")
 *                 must not return the daemon's own maps (hw-iv found vpud reading host addresses from that
 *                 and faulting; R1 F18), and no guest code may read host files.
 *     open()/__open_2() dispatch device -> virtual -> guest errno ENOENT; there is no host fallback in
 *     open() (the frozen task text: "else -1 with guest errno ENOENT").  fopen() adds only the
 *     /data,/sdcard -> $N2HW_DUMP_DIR mapping.
 *   - read/write/ioctl/close on a device go through the callbacks of struct n2vfs_dev (include/n2elf.h).
 *     The struct has no read/write callback, so those return -EINVAL on a device; the /dev/vcu protocol
 *     is ioctl-only (FACTS 5.2-5.7).
 *   - ioctl: the device returns >= 0 (passed to the guest) or -errno (the guest gets -1 and errno = -ret).
 *   - mmap: V30's heap shim forwards descriptor-backed mmap to n2vfs_mmap() here, which calls the owning
 *     device's mmap hook.  Everything else fails with ENODEV.
 *   - __errno returns a guest address of a 4-byte word, one per guest cpu (i.e. per guest thread); the
 *     word is allocated from the guest's own address space on first use so guest code can read and write it.
 *
 * Virtual files: only data that is derivable from the phone's own kernel sources or device tree is
 * supplied.  A path that is not in the table returns ENOENT, which is what the vendor code sees on a
 * stock phone for a file this kernel does not export; the callers all check the open result.  Deliberately
 * absent (ENOENT): /proc/ppm/dump_cluster_N_dvfs_table, cpufreq_oppidx, scaling_available_frequencies,
 * /sys/bus/platform/drivers/dev_info/dev_info (format not known; see STATUS.md), /proc/mcdi/sodi_en and
 * /sys/power/cpufreq_state (write-only control files, and this table is read-only).
 *
 * Guest errno values are the Linux/ARM ones (the guest is Android), not the host's: on Darwin
 * ENAMETOOLONG is 63 and Linux wants 36, so every host errno is translated (hg_errno).
 */
#define _POSIX_C_SOURCE 200809L

#include "n2elf.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Defined in n2shim_stdio.c: the one output sink of the guest (see the comment there). */
extern void (*n2shim_log)(int prio, const char *tag, const char *text);

/* ---- Linux guest errno values (do not use the host's) -------------------------------------------------------- */
enum {
	VF_EPERM = 1, VF_ENOENT = 2, VF_EIO = 5, VF_EBADF = 9, VF_EAGAIN = 11, VF_ENOMEM = 12,
	VF_EACCES = 13, VF_EFAULT = 14, VF_EBUSY = 16, VF_EEXIST = 17, VF_ENODEV = 19, VF_EISDIR = 21,
	VF_EINVAL = 22, VF_EMFILE = 24, VF_ENOTTY = 25, VF_ENOSPC = 28, VF_ESPIPE = 29, VF_EROFS = 30,
	VF_ENAMETOOLONG = 36, VF_ENOSYS = 38, VF_ELOOP = 40, VF_EOVERFLOW = 75, VF_ENOTSUP = 95
};

/* Linux/ARM open flags (the guest's ABI). */
enum {
	VF_O_ACCMODE = 3, VF_O_RDONLY = 0, VF_O_WRONLY = 1, VF_O_RDWR = 2,
	VF_O_CREAT = 0100, VF_O_EXCL = 0200, VF_O_TRUNC = 01000, VF_O_APPEND = 02000, VF_O_CLOEXEC = 02000000
};

#define VF_FD_FIRST 100
#define VF_FD_COUNT 64
#define VF_MAX_DEVS 16
#define VF_MAX_PATH 1024

enum vf_kind { VF_FREE = 0, VF_DEV, VF_VFILE, VF_HOST };

struct vf_file {
	const char *path;
	const void *data;
	uint32_t len;
};

struct vf_dev_slot {
	const struct n2vfs_dev *dev;
};

struct vf_fd {
	int kind;
	const struct n2vfs_dev *dev;
	int hostfd;
	const void *data;          /* VF_VFILE: content (the maps buffer is shared; set it before opening) */
	uint32_t len;
	uint32_t pos;
	int flags;
};

static pthread_mutex_t vf_lock = PTHREAD_MUTEX_INITIALIZER;
static struct vf_dev_slot vf_devs[VF_MAX_DEVS];
static unsigned vf_ndevs;
static struct vf_fd vf_fds[VF_FD_COUNT];

/* ---- virtual read-only files ---------------------------------------------------------------------------------
 * /proc/cpuinfo: MT6893 as the phone's own device tree describes it (nord2-current-kernel
 * arch/arm64/boot/dts/mediatek/mt6893.dts, cpus{}): cpu0-3 Cortex-A55 (part 0xd05), cpu4-7 Cortex-A78
 * (part 0xd41), one 3.0 GHz core (cpu7, cluster2).  libvcodec_oal.so's getProcessor() parses exactly
 * "processor : %d" and "CPU part : %x" with sscanf and knows 0xd05 = 55; 0xd41 is unknown to it, so it
 * falls back to /sys/devices/system/cpu/cpuN/of_node/compatible, which we also supply.
 *
 * physical_package_id: arch/arm64/kernel/topology.c assigns one package id per cpu-map cluster, and
 * mt6893.dts has cluster0 = cpu0-3, cluster1 = cpu4-6, cluster2 = cpu7.
 *
 * /proc/device-tree/chosen/atag,devinfo: struct devinfo_tag { u32 data_size; u32 data[]; } in the
 * little-endian form the bootloader writes.  libvcodecdrv.so reads it once (0x194 bytes) and uses the
 * u32 at byte offset 0x10 = data[3] as the eFuse capability word (bits 6,7,8,10,11,12).  Our synthetic
 * blob has data_size = 220 and data[3] = 0x80000000, which is the word the phone's live device tree
 * (ios-sweet/dtb/nord2/live.dtb) carries; the remaining 219 words are zero here because on the phone
 * they hold serial numbers and hardware ids that must not be committed to this tree.  Only data[3] is
 * read by the codec library, so zeroing the rest changes nothing for it.
 */
static const char vf_cpuinfo[] =
	"processor\t: 0\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x0\n"
	"CPU part\t: 0xd05\n"
	"CPU revision\t: 0\n"
	"\n"
	"processor\t: 1\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x0\n"
	"CPU part\t: 0xd05\n"
	"CPU revision\t: 0\n"
	"\n"
	"processor\t: 2\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x0\n"
	"CPU part\t: 0xd05\n"
	"CPU revision\t: 0\n"
	"\n"
	"processor\t: 3\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x0\n"
	"CPU part\t: 0xd05\n"
	"CPU revision\t: 0\n"
	"\n"
	"processor\t: 4\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x1\n"
	"CPU part\t: 0xd41\n"
	"CPU revision\t: 0\n"
	"\n"
	"processor\t: 5\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x1\n"
	"CPU part\t: 0xd41\n"
	"CPU revision\t: 0\n"
	"\n"
	"processor\t: 6\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x1\n"
	"CPU part\t: 0xd41\n"
	"CPU revision\t: 0\n"
	"\n"
	"processor\t: 7\n"
	"BogoMIPS\t: 26.00\n"
	"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\n"
	"CPU implementer\t: 0x41\n"
	"CPU architecture: 8\n"
	"CPU variant\t: 0x1\n"
	"CPU part\t: 0xd41\n"
	"CPU revision\t: 0\n";

#define VF_CPUINFO_PART 28   /* offset of "processor\t: 0" in each 358-byte block, checked by the test */

static const unsigned char vf_devinfo[884] = {
	[0] = 0xdc,          /* data_size = 220 u32 words, little-endian */
	[0x13] = 0x80,       /* data[3] = 0x80000000 at byte offset 0x10 (read by libvcodecdrv.so) */
};

/* /proc/self/maps and /proc/device-tree/model.  The map list cannot be static: vpud remap_shmem() reads the
 * first line that is both "rw-p" and "libvpud_vcodec.so" and copies that range into the VCU DMEM, so the text
 * must describe the loader's real bases.  The runner sets it with n2vfs_set_maps() after n2elf_link() and
 * before it starts the guest; the file is ENOENT until then (a made-up map would make vpud copy the wrong
 * bytes).  A later call rewrites the shared buffer, so do not call it while a maps stream is open. */
#define VF_MAPS_MAX 16384
static pthread_mutex_t vf_maps_lock = PTHREAD_MUTEX_INITIALIZER;
static char vf_maps[VF_MAPS_MAX];
static uint32_t vf_maps_len;

int n2vfs_set_maps(const char *text, uint32_t len)
{
	if (!text || !len) {
		pthread_mutex_lock(&vf_maps_lock);
		vf_maps_len = 0;
		pthread_mutex_unlock(&vf_maps_lock);
		return 0;
	}
	if (len >= VF_MAPS_MAX)
		return -VF_EINVAL;
	pthread_mutex_lock(&vf_maps_lock);
	memcpy(vf_maps, text, len);
	vf_maps[len] = 0;
	vf_maps_len = len;
	pthread_mutex_unlock(&vf_maps_lock);
	return 0;
}

/* The phone's platform name (nord2-current-kernel mt6893.dts:23 model = "MT6893").  drv_getchipname() in
 * libvcodec_utility.so opens this file and exit(1)s when it is missing; its needle table has no 6893 entry,
 * so the chip id stays -1 with a "get unknown chip name" log, exactly as on the phone with this library. */
static const char vf_model[] = "MT6893\n";

static const char vf_pkg_a[] = "0\n";      /* cpu0-3, cluster0 */
static const char vf_pkg_b[] = "1\n";      /* cpu4-6, cluster1 */
static const char vf_pkg_c[] = "2\n";      /* cpu7,   cluster2 */
static const char vf_a55[] = "arm,cortex-a55\n";
static const char vf_a78[] = "arm,cortex-a78\n";

#define VF_TEXT(p, s) { (p), (s), (uint32_t)(sizeof(s) - 1u) }

static const struct vf_file vf_table[] = {
	VF_TEXT("/proc/cpuinfo", vf_cpuinfo),
	{ "/proc/self/maps", vf_maps, 0 },          /* len filled in by n2vfs_set_maps() at open time */
	VF_TEXT("/proc/device-tree/model", vf_model),
	{ "/proc/device-tree/chosen/atag,devinfo", vf_devinfo, (uint32_t)sizeof vf_devinfo },
	VF_TEXT("/sys/devices/system/cpu/cpu0/topology/physical_package_id", vf_pkg_a),
	VF_TEXT("/sys/devices/system/cpu/cpu1/topology/physical_package_id", vf_pkg_a),
	VF_TEXT("/sys/devices/system/cpu/cpu2/topology/physical_package_id", vf_pkg_a),
	VF_TEXT("/sys/devices/system/cpu/cpu3/topology/physical_package_id", vf_pkg_a),
	VF_TEXT("/sys/devices/system/cpu/cpu4/topology/physical_package_id", vf_pkg_b),
	VF_TEXT("/sys/devices/system/cpu/cpu5/topology/physical_package_id", vf_pkg_b),
	VF_TEXT("/sys/devices/system/cpu/cpu6/topology/physical_package_id", vf_pkg_b),
	VF_TEXT("/sys/devices/system/cpu/cpu7/topology/physical_package_id", vf_pkg_c),
	VF_TEXT("/sys/devices/system/cpu/cpu0/of_node/compatible", vf_a55),
	VF_TEXT("/sys/devices/system/cpu/cpu1/of_node/compatible", vf_a55),
	VF_TEXT("/sys/devices/system/cpu/cpu2/of_node/compatible", vf_a55),
	VF_TEXT("/sys/devices/system/cpu/cpu3/of_node/compatible", vf_a55),
	VF_TEXT("/sys/devices/system/cpu/cpu4/of_node/compatible", vf_a78),
	VF_TEXT("/sys/devices/system/cpu/cpu5/of_node/compatible", vf_a78),
	VF_TEXT("/sys/devices/system/cpu/cpu6/of_node/compatible", vf_a78),
	VF_TEXT("/sys/devices/system/cpu/cpu7/of_node/compatible", vf_a78),
};
#define VF_NFILES (sizeof vf_table / sizeof vf_table[0])

/* ---- guest errno: one 4-byte word per guest cpu (guest thread) ------------------------------------------------ */
#define VF_ERRNO_SLOTS 64

struct vf_errno_slot {
	struct n2a32_cpu *cpu;
	uint32_t addr;
};

static pthread_mutex_t vf_errno_lock = PTHREAD_MUTEX_INITIALIZER;
static struct vf_errno_slot vf_errno_slots[VF_ERRNO_SLOTS];

uint32_t n2vfs_errno_location(struct n2a32_cpu *c)
{
	unsigned i;
	uint32_t a = 0;

	if (!c || !c->mem)
		return 0;
	pthread_mutex_lock(&vf_errno_lock);
	for (i = 0; i < VF_ERRNO_SLOTS; i++) {
		if (vf_errno_slots[i].cpu == c) {
			a = vf_errno_slots[i].addr;
			break;
		}
	}
	if (!a) {
		for (i = 0; i < VF_ERRNO_SLOTS; i++) {
			if (!vf_errno_slots[i].cpu) {
				a = n2a32_mem_alloc(c->mem, 16, 8);
				if (a) {
					vf_errno_slots[i].cpu = c;
					vf_errno_slots[i].addr = a;
					n2a32_st32(c, a, 0);
				}
				break;
			}
		}
	}
	pthread_mutex_unlock(&vf_errno_lock);
	return a;
}

static void vf_set_errno(struct n2a32_cpu *c, int e)
{
	uint32_t loc = n2vfs_errno_location(c);

	if (loc)
		n2a32_st32(c, loc, (uint32_t)e);
}

/* Host errno -> Linux guest errno.  The two agree for the common values; the ones that differ on Darwin
 * (ENAMETOOLONG, ELOOP, EOVERFLOW, ENOTSUP, ENOSYS) are translated here. */
static int hg_errno(int e)
{
	switch (e) {
	case EPERM: return VF_EPERM;
	case ENOENT: return VF_ENOENT;
	case EIO: return VF_EIO;
	case EBADF: return VF_EBADF;
	case EAGAIN: return VF_EAGAIN;
	case ENOMEM: return VF_ENOMEM;
	case EACCES: return VF_EACCES;
	case EFAULT: return VF_EFAULT;
	case EBUSY: return VF_EBUSY;
	case EEXIST: return VF_EEXIST;
	case ENODEV: return VF_ENODEV;
	case EISDIR: return VF_EISDIR;
	case EINVAL: return VF_EINVAL;
	case EMFILE: return VF_EMFILE;
	case ENOTTY: return VF_ENOTTY;
	case ENOSPC: return VF_ENOSPC;
	case ESPIPE: return VF_ESPIPE;
	case EROFS: return VF_EROFS;
#ifdef ENAMETOOLONG
	case ENAMETOOLONG: return VF_ENAMETOOLONG;
#endif
	case ENOSYS: return VF_ENOSYS;
#ifdef ELOOP
	case ELOOP: return VF_ELOOP;
#endif
	default: return VF_EIO;
	}
}

/* ---- registration --------------------------------------------------------------------------------------------- */
int n2vfs_register(const struct n2vfs_dev *dev)
{
	unsigned i;
	int rc = 0;

	if (!dev || !dev->path || !dev->path[0])
		return -VF_EINVAL;
	pthread_mutex_lock(&vf_lock);
	for (i = 0; i < vf_ndevs; i++) {
		if (vf_devs[i].dev == dev || !strcmp(vf_devs[i].dev->path, dev->path)) {
			rc = -VF_EEXIST;
			goto out;
		}
	}
	if (vf_ndevs >= VF_MAX_DEVS) {
		rc = -VF_ENOSPC;
		goto out;
	}
	vf_devs[vf_ndevs++].dev = dev;
out:
	pthread_mutex_unlock(&vf_lock);
	return rc;
}

static const struct n2vfs_dev *vf_find_dev(const char *path)
{
	const struct n2vfs_dev *d = NULL;
	unsigned i;

	pthread_mutex_lock(&vf_lock);
	for (i = 0; i < vf_ndevs; i++) {
		if (!strcmp(vf_devs[i].dev->path, path)) {
			d = vf_devs[i].dev;
			break;
		}
	}
	pthread_mutex_unlock(&vf_lock);
	return d;
}

/* Snapshot the virtual file entry (the maps entry has a run-time length).  0 = no such file. */
static int vf_find_file(const char *path, struct vf_file *out)
{
	unsigned i;

	for (i = 0; i < VF_NFILES; i++) {
		if (!strcmp(vf_table[i].path, path)) {
			*out = vf_table[i];
			if (out->data == vf_maps) {
				pthread_mutex_lock(&vf_maps_lock);
				out->len = vf_maps_len;
				pthread_mutex_unlock(&vf_maps_lock);
				if (!out->len)
					return 0;       /* not set yet -> ENOENT */
			}
			return 1;
		}
	}
	return 0;
}

/* ---- fd table ------------------------------------------------------------------------------------------------- */
static int vf_fd_alloc(int kind, const struct n2vfs_dev *dev, int hostfd, const struct vf_file *f, int flags)
{
	int i, fd = -VF_EMFILE;

	pthread_mutex_lock(&vf_lock);
	for (i = 0; i < VF_FD_COUNT; i++) {
		if (vf_fds[i].kind == VF_FREE) {
			vf_fds[i].kind = kind;
			vf_fds[i].dev = dev;
			vf_fds[i].hostfd = hostfd;
			vf_fds[i].data = f ? f->data : NULL;
			vf_fds[i].len = f ? f->len : 0;
			vf_fds[i].pos = 0;
			vf_fds[i].flags = flags;
			fd = VF_FD_FIRST + i;
			break;
		}
	}
	pthread_mutex_unlock(&vf_lock);
	return fd;
}

/* Snapshot one descriptor.  0 on success, -EBADF when the number is not ours. */
static int vf_fd_get(int fd, struct vf_fd *out)
{
	int i = fd - VF_FD_FIRST;

	if (i < 0 || i >= VF_FD_COUNT)
		return -VF_EBADF;
	pthread_mutex_lock(&vf_lock);
	if (vf_fds[i].kind == VF_FREE) {
		pthread_mutex_unlock(&vf_lock);
		return -VF_EBADF;
	}
	*out = vf_fds[i];
	pthread_mutex_unlock(&vf_lock);
	return 0;
}

static void vf_fd_store_pos(int fd, uint32_t pos)
{
	int i = fd - VF_FD_FIRST;

	if (i < 0 || i >= VF_FD_COUNT)
		return;
	pthread_mutex_lock(&vf_lock);
	if (vf_fds[i].kind == VF_VFILE)
		vf_fds[i].pos = pos;
	pthread_mutex_unlock(&vf_lock);
}

/* Is the guest buffer [a, a+len) usable as plain memory?  We cannot ask the core which pages are mapped
 * (n2a32.h has no accessor); this rejects the cases that are decidable: NULL, wrapping past 4 GiB, MMIO. */
static int vf_guest_buf_ok(struct n2a32_cpu *c, uint32_t a, uint32_t len)
{
	if (!c || !c->mem)
		return 0;
	if (!a)
		return len == 0;
	if ((uint64_t)a + len > 0x100000000ull)
		return 0;
	if (len && (n2a32_is_mmio(c->mem, a) || n2a32_is_mmio(c->mem, (uint32_t)((uint64_t)a + len - 1))))
		return 0;
	return 1;
}

/* Copy a NUL-terminated guest string.  0 on success, -EFAULT/-ENAMETOOLONG. */
static int vf_guest_str(struct n2a32_cpu *c, uint32_t ga, char *out, size_t cap)
{
	size_t i;

	if (!ga || !cap)
		return -VF_EFAULT;
	for (i = 0; i + 1 < cap; i++) {
		uint8_t ch;

		if ((uint64_t)ga + i > 0xffffffffull)
			return -VF_EFAULT;
		ch = n2a32_ld8(c, ga + (uint32_t)i);
		out[i] = (char)ch;
		if (!ch)
			return 0;
	}
	out[cap - 1] = 0;
	return -VF_ENAMETOOLONG;
}

/* ---- open ----------------------------------------------------------------------------------------------------- */
static int vf_starts_with(const char *s, const char *prefix, int component)
{
	size_t n = strlen(prefix);

	if (strncmp(s, prefix, n))
		return 0;
	return !component || s[n] == 0 || s[n] == '/';
}

/* Guest (Linux/ARM) open flags -> host flags.  They are the same on Linux but not on Darwin, where the
 * daemon runs: guest O_CREAT 0100 is 64, Darwin's is 0x200.  Only the flags the vendor set uses are mapped. */
static int lh_flags(int g)
{
	int h;

	switch (g & VF_O_ACCMODE) {
	case VF_O_WRONLY: h = O_WRONLY; break;
	case VF_O_RDWR: h = O_RDWR; break;
	default: h = O_RDONLY; break;
	}
	if (g & VF_O_CREAT)
		h |= O_CREAT;
	if (g & VF_O_EXCL)
		h |= O_EXCL;
	if (g & VF_O_TRUNC)
		h |= O_TRUNC;
	if (g & VF_O_APPEND)
		h |= O_APPEND;
	h |= O_CLOEXEC;
	return h;
}

/* No "." or ".." component: the dump mapping must not be able to walk out of its tree with /data/../../etc. */
static int vf_path_plain(const char *p)
{
	const char *s = p;

	while (*s) {
		const char *e;

		while (*s == '/')
			s++;
		e = s;
		while (*e && *e != '/')
			e++;
		if (e - s == 1 && s[0] == '.')
			return 0;
		if (e - s == 2 && s[0] == '.' && s[1] == '.')
			return 0;
		s = e;
	}
	return 1;
}

/* The host path a guest fopen() of /data or /sdcard maps to, or NULL when there is none.  Anything that is
 * not under those two roots returns NULL: the guest file system is {devices} + {virtual files} + {the dump
 * tree}, nothing else.  *mapped is set to 1 when the path was rewritten. */
static const char *vf_dump_path(char *buf, size_t cap, const char *path, int *mapped)
{
	const char *dir;

	*mapped = 0;
	if (!vf_starts_with(path, "/data", 1) && !vf_starts_with(path, "/sdcard", 1))
		return NULL;
	*mapped = 1;
	if (!vf_path_plain(path))
		return NULL;
	dir = getenv("N2HW_DUMP_DIR");
	if (!dir || !dir[0])
		return NULL;
	if (snprintf(buf, cap, "%s%s", dir, path) >= (int)cap)
		return NULL;
	return buf;
}

int n2vfs_open_path(struct n2a32_cpu *c, const char *path, int flags, int mode, int allow_host)
{
	const struct n2vfs_dev *dev;
	struct vf_file vfile;
	char mapped[VF_MAX_PATH * 2];
	const char *hostpath;
	int fd, rc, acc, mapped_flag = 0;

	(void)c;
	if (!path || !path[0])
		return -VF_ENOENT;

	dev = vf_find_dev(path);
	if (dev) {
		if (dev->open) {
			rc = dev->open(dev->ctx, flags);
			if (rc < 0)
				return rc;
		}
		fd = vf_fd_alloc(VF_DEV, dev, -1, NULL, flags);
		if (fd < 0) {
			if (dev->close)
				(void)dev->close(dev->ctx);
			return fd;
		}
		return fd;
	}

	if (vf_find_file(path, &vfile)) {
		acc = flags & VF_O_ACCMODE;
		if (acc != VF_O_RDONLY)
			return -VF_EACCES;
		return vf_fd_alloc(VF_VFILE, NULL, -1, &vfile, flags);
	}

	if (!allow_host)
		return -VF_ENOENT;

	hostpath = vf_dump_path(mapped, sizeof mapped, path, &mapped_flag);
	(void)mapped_flag;
	if (!hostpath)
		return -VF_ENOENT;
	{
		int hf = open(hostpath, lh_flags(flags), mode);

		if (hf < 0)
			return -hg_errno(errno);
		fd = vf_fd_alloc(VF_HOST, NULL, hf, NULL, flags);
		if (fd < 0) {
			close(hf);
			return fd;
		}
	}
	return fd;
}

/* ---- read / write / seek / fsync / close (shared with V33's stdio) -------------------------------------------- */
int n2vfs_gfd_read(struct n2a32_cpu *c, int fd, void *dst, size_t len)
{
	struct vf_fd e;
	int rc;

	(void)c;
	if (!len)
		return 0;
	rc = vf_fd_get(fd, &e);
	if (rc < 0)
		return rc;
	switch (e.kind) {
	case VF_VFILE: {
		uint32_t avail = 0, n;

		if (e.pos < e.len)
			avail = e.len - e.pos;
		n = len > avail ? avail : (uint32_t)len;
		if (n)
			memcpy(dst, (const unsigned char *)e.data + e.pos, n);
		vf_fd_store_pos(fd, e.pos + n);
		return (int)n;
	}
	case VF_HOST: {
		ssize_t r = read(e.hostfd, dst, len);

		if (r < 0)
			return -hg_errno(errno);
		return (int)r;
	}
	default:
		return -VF_EINVAL;
	}
}

int n2vfs_gfd_write(struct n2a32_cpu *c, int fd, const void *src, size_t len)
{
	struct vf_fd e;
	int rc;

	(void)c;
	if (!len)
		return 0;
	rc = vf_fd_get(fd, &e);
	if (rc < 0)
		return rc;
	switch (e.kind) {
	case VF_HOST: {
		const unsigned char *p = src;
		size_t done = 0;

		while (done < len) {
			ssize_t w = write(e.hostfd, p + done, len - done);

			if (w < 0) {
				if (errno == EINTR)
					continue;
				return -hg_errno(errno);
			}
			if (!w)
				break;
			done += (size_t)w;
		}
		return (int)done;
	}
	case VF_VFILE:
		return -VF_EBADF;
	default:
		return -VF_EINVAL;
	}
}

int n2vfs_gfd_seek(struct n2a32_cpu *c, int fd, int64_t off, int whence)
{
	struct vf_fd e;
	int rc;

	(void)c;
	rc = vf_fd_get(fd, &e);
	if (rc < 0)
		return rc;
	switch (e.kind) {
	case VF_HOST: {
		off_t r = lseek(e.hostfd, (off_t)off, whence);

		if (r < 0)
			return -hg_errno(errno);
		return (int)r;
	}
	case VF_VFILE: {
		int64_t base;

		switch (whence) {
		case 0: base = 0; break;
		case 1: base = e.pos; break;
		case 2: base = e.len; break;
		default: return -VF_EINVAL;
		}
		if ((off > 0 && base > INT64_MAX - off) || (off < 0 && base < -off))
			return -VF_EINVAL;
		base += off;
		if (base < 0 || base > 0xffffffffll)
			return -VF_EINVAL;
		vf_fd_store_pos(fd, (uint32_t)base);
		return (int)base;
	}
	default:
		return -VF_ESPIPE;
	}
}

int n2vfs_gfd_fsync(struct n2a32_cpu *c, int fd)
{
	struct vf_fd e;
	int rc;

	(void)c;
	rc = vf_fd_get(fd, &e);
	if (rc < 0)
		return rc;
	if (e.kind != VF_HOST)
		return 0;               /* a virtual file or a device keeps no host buffer to flush */
	if (fsync(e.hostfd) != 0)
		return -hg_errno(errno);
	return 0;
}

int n2vfs_gfd_close(int fd)
{
	struct vf_fd e;
	int i, rc = 0;

	i = fd - VF_FD_FIRST;
	if (i < 0 || i >= VF_FD_COUNT)
		return -VF_EBADF;
	pthread_mutex_lock(&vf_lock);
	if (vf_fds[i].kind == VF_FREE) {
		pthread_mutex_unlock(&vf_lock);
		return -VF_EBADF;
	}
	e = vf_fds[i];
	vf_fds[i].kind = VF_FREE;
	pthread_mutex_unlock(&vf_lock);
	switch (e.kind) {
	case VF_HOST:
		if (close(e.hostfd) != 0)
			rc = -hg_errno(errno);
		break;
	case VF_DEV:
		if (e.dev->close) {
			int d = e.dev->close(e.dev->ctx);

			if (d < 0)
				rc = d;
		}
		break;
	default:
		break;
	}
	return rc;
}

/* ---- mmap hook (called by V30's heap shim) -------------------------------------------------------------------- */
uint32_t n2vfs_mmap(struct n2a32_cpu *c, uint32_t addr, uint32_t len, int prot, int flags, int fd, uint64_t off)
{
	struct vf_fd e;
	int err = VF_ENODEV;

	(void)addr;
	(void)flags;
	if (vf_fd_get(fd, &e) < 0) {
		vf_set_errno(c, VF_EBADF);
		return 0;
	}
	if (e.kind != VF_DEV || !e.dev->mmap) {
		vf_set_errno(c, VF_ENODEV);
		return 0;
	}
	{
		uint32_t p = e.dev->mmap(e.dev->ctx, c, len, prot, off, &err);

		if (!p) {
			vf_set_errno(c, err > 0 ? err : VF_ENODEV);
			return 0;
		}
		return p;
	}
}

/* ---- the shims ------------------------------------------------------------------------------------------------ */
static void n2vfs_sh_open_common(struct n2a32_cpu *c, unsigned first)
{
	char path[VF_MAX_PATH];
	uint32_t gp = n2shim_arg(c, first);
	int flags = (int)n2shim_arg(c, first + 1);
	int mode = 0666;
	int r = vf_guest_str(c, gp, path, sizeof path);

	if (r < 0) {
		vf_set_errno(c, -r);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	if (flags & VF_O_CREAT)          /* a mode word exists only when O_CREAT is set */
		mode = (int)n2shim_arg(c, first + 2);
	r = n2vfs_open_path(c, path, flags, mode, 0);
	if (r < 0) {
		vf_set_errno(c, -r);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2shim_ret(c, (uint32_t)r);
}

void n2vfs_sh_open(struct n2a32_cpu *c)
{
	n2vfs_sh_open_common(c, 0);
}

void n2vfs_sh___open_2(struct n2a32_cpu *c)
{
	n2vfs_sh_open_common(c, 0);
}

void n2vfs_sh_close(struct n2a32_cpu *c)
{
	uint32_t fd = n2shim_arg(c, 0);
	int r;

	if (fd <= 2) {                  /* the three std descriptors are not ours; closing them is a no-op */
		n2shim_ret(c, 0);
		return;
	}
	r = n2vfs_gfd_close((int)fd);
	if (r < 0) {
		vf_set_errno(c, -r);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2shim_ret(c, 0);
}

void n2vfs_sh_read(struct n2a32_cpu *c)
{
	uint32_t fd = n2shim_arg(c, 0), buf = n2shim_arg(c, 1), count = n2shim_arg(c, 2);
	int r;

	if (fd <= 2) {
		vf_set_errno(c, VF_EBADF);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	if (!vf_guest_buf_ok(c, buf, count)) {
		vf_set_errno(c, VF_EFAULT);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	r = n2vfs_gfd_read(c, (int)fd, n2a32_ptr(c->mem, buf), count);
	if (r < 0) {
		vf_set_errno(c, -r);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2shim_ret(c, (uint32_t)r);
}

void n2vfs_sh_write(struct n2a32_cpu *c)
{
	uint32_t fd = n2shim_arg(c, 0), buf = n2shim_arg(c, 1), count = n2shim_arg(c, 2);
	int r;

	if (!vf_guest_buf_ok(c, buf, count)) {
		vf_set_errno(c, VF_EFAULT);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	if ((fd == 1 || fd == 2) && count == 0) {
		n2shim_ret(c, 0);
		return;
	}
	if (fd == 1 || fd == 2) {        /* stdout/stderr: hand the bytes to the log callback, see n2shim_stdio.c */
		uint32_t n = count;
		char stackbuf[256];
		char *p = stackbuf;

		if (n > 65535u)
			n = 65535u;
		if (n + 1u > sizeof stackbuf) {
			p = malloc((size_t)n + 1u);
			if (!p) {
				vf_set_errno(c, VF_ENOMEM);
				n2shim_ret(c, 0xffffffffu);
				return;
			}
		}
		if (n)
			memcpy(p, n2a32_ptr(c->mem, buf), n);
		p[n] = 0;
		if (n2shim_log)
			n2shim_log(fd == 1 ? 4 : 6, fd == 1 ? "stdout" : "stderr", p);
		if (p != stackbuf)
			free(p);
		n2shim_ret(c, count);
		return;
	}
	if (fd == 0) {
		vf_set_errno(c, VF_EBADF);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	r = n2vfs_gfd_write(c, (int)fd, n2a32_ptr(c->mem, buf), count);
	if (r < 0) {
		vf_set_errno(c, -r);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2shim_ret(c, (uint32_t)r);
}

void n2vfs_sh_ioctl(struct n2a32_cpu *c)
{
	uint32_t fd = n2shim_arg(c, 0), req = n2shim_arg(c, 1), arg = n2shim_arg(c, 2);
	struct vf_fd e;

	if (vf_fd_get((int)fd, &e) < 0 || e.kind != VF_DEV || !e.dev->ioctl) {
		vf_set_errno(c, VF_ENOTTY);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	{
		int r = e.dev->ioctl(e.dev->ctx, c, req, arg);

		if (r < 0) {
			vf_set_errno(c, -r);
			n2shim_ret(c, 0xffffffffu);
			return;
		}
		n2shim_ret(c, (uint32_t)r);
	}
}

void n2vfs_sh___errno(struct n2a32_cpu *c)
{
	n2shim_ret(c, n2vfs_errno_location(c));
}
