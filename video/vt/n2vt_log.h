/* SPDX-License-Identifier: MIT */
/*
 * n2vt_log.h -- the one console channel of the video work.
 *
 * n2vt_say() writes one line "N2VIDEO <progname>[<pid>]: <text>" to /dev/console. /dev/console is opened and closed for every
 * line: the plug-in is loaded into processes (mediaserverd) whose descriptors are not ours to keep, and a descriptor kept from
 * the first line is somebody else's file later (the camera bundle learned this on run q10gm).
 *
 * At most N2VT_LOG_MAX_LINES lines are written per process; the next call writes "(further lines suppressed)" once, and
 * every call after that writes nothing. The channel is silent while the file N2VT_SWITCH_QUIET exists. The call is
 * thread-safe, never fails loudly and never returns a status.
 */
#ifndef N2VT_LOG_H
#define N2VT_LOG_H

#define N2VT_LOG_MAX_LINES 40

void n2vt_say(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
