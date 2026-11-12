/* SPDX-License-Identifier: MIT */
/*
 * n2memlimit.h -- process memory footprint and jetsam limit helpers for vcodecd / n2vcd_selftest (opus-guestmem).
 *
 * Second phone run: vcodecd and n2vcd_selftest were SIGKILLed with "reason ns 1 code 0x7" = OS_REASON_JETSAM /
 * JETSAM_REASON_MEMORY_PERPROCESSLIMIT (xnu bsd/sys/reason.h, bsd/sys/kern_memorystatus.h) while loading the first
 * vendor library, at a footprint the host measures at ~21 MB for a whole run: the launchd JetsamProperties limit
 * (1024 MB) was evidently not what the kernel applied.  memorystatus_control() needs root or the memorystatus
 * entitlement (kern_memorystatus.c), and both daemons run as root, so each raises its own limit at startup.
 *
 * Header only; static functions; no-ops (except the RSS read) off Darwin.
 */
#ifndef N2MEMLIMIT_H
#define N2MEMLIMIT_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task_info.h>

/* libsystem_kernel export; not in the public SDK headers (xnu bsd/sys/kern_memorystatus.h) */
extern int memorystatus_control(uint32_t command, int32_t pid, uint32_t flags, void *buffer, size_t buffersize);
#define N2ML_CMD_SET_MEMLIMIT_PROPERTIES 7
#define N2ML_CMD_GET_MEMLIMIT_PROPERTIES 8
#define N2ML_CMD_CONVERT_MEMLIMIT_MB     28
#define N2ML_ATTR_FATAL                  0x1
struct n2ml_props {
	int32_t active;
	uint32_t active_attr;
	int32_t inactive;
	uint32_t inactive_attr;
};
#endif

/* Current physical footprint (Darwin: phys_footprint, what jetsam enforces) and resident size, in KiB; -1 if unknown. */
static inline void n2ml_usage(long *footprint_kb, long *resident_kb)
{
	*footprint_kb = -1;
	*resident_kb = -1;
#ifdef __APPLE__
	task_vm_info_data_t vi;
	mach_msg_type_number_t cnt = TASK_VM_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &cnt) == KERN_SUCCESS) {
		*footprint_kb = (long)(vi.phys_footprint >> 10);
		*resident_kb = (long)(vi.resident_size >> 10);
	}
#else
	FILE *f = fopen("/proc/self/statm", "r");
	if (f) {
		long size = 0, res = 0;
		if (fscanf(f, "%ld %ld", &size, &res) == 2) {
			*resident_kb = res * (sysconf(_SC_PAGESIZE) / 1024);
			*footprint_kb = *resident_kb;
		}
		fclose(f);
	}
#endif
}

/* One line "fp=..KiB rss=..KiB" into buf. */
static inline const char *n2ml_usage_str(char *buf, size_t len)
{
	long fp, rss;
	n2ml_usage(&fp, &rss);
	snprintf(buf, len, "footprint=%ldKiB rss=%ldKiB", fp, rss);
	return buf;
}

/* Read the current limits, raise both active and inactive to `mb` (fatal), read back; describe everything in buf.
 * Returns 0 when the read-back limit is >= mb, -1 otherwise (or off Darwin). */
static inline int n2ml_raise(int32_t mb, char *buf, size_t len)
{
#ifdef __APPLE__
	struct n2ml_props before, want, after;
	int32_t conv = 0;
	int rg, rs, ra, eg = 0, es = 0, ea = 0;
	pid_t pid = getpid();

	memset(&before, 0, sizeof before);
	memset(&after, 0, sizeof after);
	rg = memorystatus_control(N2ML_CMD_GET_MEMLIMIT_PROPERTIES, pid, 0, &before, sizeof before);
	if (rg != 0) eg = errno;
	conv = before.active;
	if (rg == 0 && memorystatus_control(N2ML_CMD_CONVERT_MEMLIMIT_MB, pid, (uint32_t)before.active, &conv, sizeof conv) != 0)
		conv = before.active;
	want.active = mb;
	want.active_attr = N2ML_ATTR_FATAL;
	want.inactive = mb;
	want.inactive_attr = N2ML_ATTR_FATAL;
	rs = memorystatus_control(N2ML_CMD_SET_MEMLIMIT_PROPERTIES, pid, 0, &want, sizeof want);
	if (rs != 0) es = errno;
	ra = memorystatus_control(N2ML_CMD_GET_MEMLIMIT_PROPERTIES, pid, 0, &after, sizeof after);
	if (ra != 0) ea = errno;
	snprintf(buf, len,
	         "memlimit before active=%d(attr %u, effective %d MB) inactive=%d(attr %u) get=%d/%d; set %d MB fatal -> %d errno=%d (%s); "
	         "after active=%d inactive=%d get=%d/%d",
	         before.active, before.active_attr, conv, before.inactive, before.inactive_attr, rg, eg, mb, rs, es,
	         es ? strerror(es) : "ok", after.active, after.inactive, ra, ea);
	return (ra == 0 && after.active >= mb) ? 0 : -1;
#else
	snprintf(buf, len, "memlimit: not Darwin, nothing to raise (wanted %d MB)", mb);
	return -1;
#endif
}

#endif
