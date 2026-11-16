/* Panfrost/RedwoodMali request surface of y2sios (MIT, see build/y2s-stage0/NOTICE.md) answered over the
 * Nord2GPU transport (nord2_gpu_transport.h).  One device and one connection per process, shared by the
 * Mesa libdrm stand-in (libredwoodair) and the Metal driver, because GPU addresses are per connection here.
 * SPDX-License-Identifier: MIT */
#ifndef NORD2_PAN_H
#define NORD2_PAN_H
#include <stddef.h>
#include <stdint.h>

/* An IOSurface as the process sees it (its base address is the buffer's CPU view). */
struct np_surface_info {
	uint64_t size, cpu;
	uint32_t offset, rowBytes, width, height, pixelFormat, planes;
	uint32_t planeOffset[2], planeRowBytes[2];
};

/* Same shape as struct n2mb_ops: a test kernel or the IOKit connection. */
struct np_ops {
	void *opaque;
	int (*call)(void *, unsigned sel, const uint64_t *in, unsigned ni, const void *bytes, size_t size,
		    uint64_t *out, unsigned *no, void *ob, size_t *os);
	int (*map)(void *, unsigned handle, uint64_t *address, uint64_t *size);
	int (*unmap)(void *, unsigned handle, uint64_t address);
	int (*surface)(void *, uint32_t surface_id, struct np_surface_info *out);   /* optional */
};

#define NP_BOS          8192u           /* Panfrost handles (RedwoodMali.cpp MALI_BUFFERS) */
#define NP_SYNCOBJS     1024u
#define NP_KERNEL_BOS   32u             /* N2C_JOB_BOS */

/* Extension bits in CREATE_BO flags (Linux has NOEXEC 1 and HEAP 2). */
#define NP_BO_NOEXEC    1u
#define NP_BO_HEAP      2u

int np_init(const struct np_ops *ops);          /* 0 or -1 (errno) */
int np_ready(void);
int np_open_iokit(void);        /* the process's Nord2GPU connection (iOS), opened once: 0 or -1 */
void np_shutdown(void);
/* One request: `number` is the low byte of the Linux ioctl code (0x40 SUBMIT ...), `data` goes in and comes
 * back as it is (IOConnectCallStructMethod semantics).  Returns 0, or -1 with errno set. */
int np_call(unsigned number, void *data, size_t size, size_t *returned);
/* CPU mapping of a buffer (IOConnectMapMemory64 semantics). */
int np_map(uint32_t handle, uint64_t *address, uint64_t *size);
int np_unmap(uint32_t handle, uint64_t address);
/* Newest job known finished (read-only; the libdrm fast path of WAIT_BO). */
const volatile uint64_t *np_completed_ptr(void);
/* Statistics for diagnostics. */
struct np_stats {
	uint64_t submits, submit_bos_dropped, waits, wait_calls, creates, closes, full_maint, faults;
	uint64_t no_head_bo, surf_fail, surf_fail_enoent;
};
void np_stats(struct np_stats *out);
void np_log(const char *text, size_t length);
void np_pool_enable(int on);
int np_pressure(int drain, unsigned holdoff_ms, uint64_t out[8]);   /* y2s95: see nord2_pan.c */
void np_start_sampler(void);                    /* diagnostic: where the processes wait (iOS only) */
void np_start_prewarm(void);                    /* read the files of the first interactions into the file cache while idle (the renderer does this) */
unsigned np_prewarm_dirs(const char *const *dirs, unsigned n, uint64_t budget, uint64_t *used);   /* the same walk, for tests */
void np_start_prime(void);                      /* fill the pool in the background (the renderer does this at init) */   /* a line into the kernel log */
#endif
