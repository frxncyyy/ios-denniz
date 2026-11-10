/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_TIME_H
#define N2_MIN_TIME_H
#include <stddef.h>
typedef long time_t;
struct timespec { time_t tv_sec; long tv_nsec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; const char *tm_zone; };
#define CLOCK_MONOTONIC 6
int clock_gettime(int, struct timespec *);
time_t time(time_t *);
int nanosleep(const struct timespec *, struct timespec *);
struct tm *gmtime_r(const time_t *, struct tm *);
#endif
