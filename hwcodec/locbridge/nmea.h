/* SPDX-License-Identifier: MIT
 * Minimal NMEA 0183 parser for the Nord2 location bridge (n2locbridge). No libc beyond what the caller provides:
 * pure functions, host-tested by test_nmea.c. Sentences: RMC (position, speed, course, validity, UTC date/time) and
 * GGA (fix quality, satellites, HDOP, altitude), any talker (GP, GN, GL, GA, BD...). Checksum required when present.
 */
#ifndef N2_NMEA_H
#define N2_NMEA_H

struct n2_fix {
	int valid;              /* 1 when the merged fix is usable (RMC status A, or GGA quality > 0) */
	double lat, lon;        /* degrees, south/west negative */
	double alt;             /* metres above mean sea level (GGA), 0 when unknown */
	double hdop;            /* GGA, -1 when unknown */
	int sats;               /* GGA, -1 when unknown */
	int quality;            /* GGA fix quality, -1 when unknown */
	double speed_mps;       /* RMC, -1 when unknown */
	double course_deg;      /* RMC, -1 when unknown */
	long long utc_ms;       /* RMC date + time as Unix ms, -1 when unknown; GGA alone gives time of day only (ignored) */
	double h_acc, v_acc;    /* derived accuracy (m): HDOP * UERE, see n2_fix_accuracy */
	unsigned seen;          /* bit 0 RMC, bit 1 GGA since the last n2_fix_take */
};

enum { N2_NMEA_OK = 0, N2_NMEA_IGNORED = 1, N2_NMEA_BADSUM = -1, N2_NMEA_BADFMT = -2 };

void n2_fix_init(struct n2_fix *f);
/* Feed one sentence (with or without trailing CR/LF). Returns N2_NMEA_OK when RMC/GGA updated the fix. */
int n2_nmea_feed(struct n2_fix *f, const char *line);
/* Fills h_acc/v_acc from hdop (UERE 5 m; 50 m when no HDOP). */
void n2_fix_accuracy(struct n2_fix *f);

#endif
