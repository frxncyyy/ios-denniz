/* SPDX-License-Identifier: MIT */
/* n2vtd_log.c -- see n2vtd_log.h.  One bounded line per call; no allocation after the first use; thread-safe enough for
 * a console channel (the formatting uses a per-call stack buffer). */
#include "n2vtd_log.h"

#include <stdarg.h>
#include <stdio.h>

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2VTD_WEAK __attribute__((weak_import))
#else
#define N2VTD_WEAK __attribute__((weak))
#endif

extern void n2vt_say(const char *format, ...) N2VTD_WEAK;

void n2vtd_log(const char *format, ...)
{
	char line[512];
	va_list ap;

	if (n2vt_say == NULL || format == NULL)
		return;
	va_start(ap, format);
	(void)vsnprintf(line, sizeof(line), format, ap);
	va_end(ap);
	n2vt_say("N2VDEC %s", line);
}
