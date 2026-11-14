/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_vclock.h -- vcodecd's automatic VENC clock (vcodec-20261114, opus).
 *
 * Kernel 0312 has the sysctl kern.n2vc.venc_fast: 0 = venc_sel on mainpll_d4_d2 (273 MHz, the default), 1 = univpll_d4
 * (624 MHz, the bootloader's own value).  The kernel applies it at the next VENC power-on with VEN and VENC_C1 off; the
 * vendor library powers the VENC on and off around every frame, so a value written before a session's first frame
 * holds for the whole session.  Before this file nothing wrote it on the phone (only the n2vcd_probe test tool), so
 * Camera recordings ran at 273 MHz: [PHONE] vcodecd.log in the 14 Nov rootfs mirror, boot 159 of the log: an HD 60
 * recording "avc1 1920x1080@60/1" closed with "frames=103 ... avg_hw_us=25191" (at most ~38 fps of hardware time, the
 * camera asked for 60), and 1080p30 recordings show avg_hw_us 29186..29394 (88 % of the 33.3 ms frame period).
 * [PHONE] vc25: at 624 MHz H.264 High 4K30 averages 30.2 ms and 1080p60 12.8 ms per frame, HEVC 4K30 25.6 ms.
 *
 * Policy (mode "auto", the default): 624 MHz when the session needs it, 273 MHz otherwise:
 *   fast  iff  width*height*fps > 1920*1088*30          (1080p60, 1440p30+, 4K, ...)
 *         or   fps > 31 and width*height > 640*480       (720p60: 16.45 ms of hardware time at 273 MHz, vc21, > 16.7 ms
 *                                                         with the power cycle)
 *   1080p30 / 720p30 / VGA stay at the proven 273 MHz.
 * Other modes: "slow" (always 0), "fast" (always 1), "keep" (never write the sysctl: the behaviour before this file).
 * "auto:N" sets the pixel-rate threshold N (pixels per second).  The mode comes from --venc-clock and can be overridden
 * at every OPEN by the first word of the file N2VCLK_FILE (payload-controlled; there is no shell on the phone).
 * When a session closes the daemon writes 0 again if it was the one that wrote 1 (the JPEG driver ignores the sysctl).
 *
 * Risk [I]: this kernel has no vcore/DVFSRC control; 624 MHz runs at whatever vcore OPP the bootloader/SPM left.  It
 * worked in vc25 (30..60-frame sessions) and is the JPEG driver's fixed clock; long recordings at 624 MHz are untested.
 * "slow" in N2VCLK_FILE restores the old behaviour without a new binary.
 */
#ifndef N2VCD_VCLOCK_H
#define N2VCD_VCLOCK_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define N2VCLK_FILE      "/private/var/db/nord2-vcodec/venc-clock"
#define N2VCLK_SYSCTL    "kern.n2vc.venc_fast"
#define N2VCLK_AUTO_PPS  (1920ull * 1088ull * 30ull)

enum { N2VCLK_KEEP = 0, N2VCLK_AUTO = 1, N2VCLK_SLOW = 2, N2VCLK_FAST = 3 };

static inline const char *
n2vclk_mode_name(int m)
{
	return m == N2VCLK_AUTO ? "auto" : m == N2VCLK_SLOW ? "slow" : m == N2VCLK_FAST ? "fast" : "keep";
}

/* Parses "auto", "auto:N", "slow", "fast", "keep" (leading blanks and a trailing newline/blank are ignored).
 * Returns 0 and fills mode/thr, -1 on anything else (the caller keeps its previous values). */
static inline int
n2vclk_parse(const char *s, int *mode, uint64_t *thr)
{
	if (!s) { return -1; }
	while (*s == ' ' || *s == '\t') { s++; }
	size_t n = 0;
	while (s[n] && s[n] != '\n' && s[n] != '\r' && s[n] != ' ' && s[n] != '\t') { n++; }
	char w[32];
	if (n == 0 || n >= sizeof(w)) { return -1; }
	memcpy(w, s, n);
	w[n] = 0;
	if (!strcmp(w, "slow")) { *mode = N2VCLK_SLOW; return 0; }
	if (!strcmp(w, "fast")) { *mode = N2VCLK_FAST; return 0; }
	if (!strcmp(w, "keep")) { *mode = N2VCLK_KEEP; return 0; }
	if (!strcmp(w, "auto")) { *mode = N2VCLK_AUTO; *thr = N2VCLK_AUTO_PPS; return 0; }
	if (!strncmp(w, "auto:", 5)) {
		char *end = NULL;
		unsigned long long v = strtoull(w + 5, &end, 10);
		if (!end || *end || end == w + 5 || v < 1000000ull || v > 2000000000ull) { return -1; }
		*mode = N2VCLK_AUTO;
		*thr = v;
		return 0;
	}
	return -1;
}

/* The wanted sysctl value for a session, or -1 for "do not write" (mode keep). */
static inline int
n2vclk_want(int mode, uint64_t thr, uint32_t w, uint32_t h, uint32_t fps_num, uint32_t fps_den)
{
	if (mode == N2VCLK_KEEP) { return -1; }
	if (mode == N2VCLK_SLOW) { return 0; }
	if (mode == N2VCLK_FAST) { return 1; }
	if (!fps_num) { fps_num = 30u; }
	if (!fps_den) { fps_den = 1u; }
	uint64_t px = (uint64_t)w * h;
	uint64_t pps = px * fps_num / fps_den;
	if (pps > thr) { return 1; }
	if ((uint64_t)fps_num > 31ull * fps_den && px > 640ull * 480ull) { return 1; }
	return 0;
}

/* Reads the override file: 1 = valid word found (mode/thr updated), 0 = no file, -1 = unreadable/invalid. */
static inline int
n2vclk_read_file(const char *path, int *mode, uint64_t *thr)
{
	FILE *f = fopen(path, "r");
	if (!f) { return 0; }
	char buf[64];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	return n2vclk_parse(buf, mode, thr) == 0 ? 1 : -1;
}

#endif
