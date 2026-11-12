/* SPDX-License-Identifier: MIT
 * NMEA 0183 RMC/GGA parser, see nmea.h. Freestanding (no libc calls) so the same file builds for iOS and the host.
 */
#include "nmea.h"

#define MAXF 24

void n2_fix_init(struct n2_fix *f)
{
	f->valid = 0; f->lat = f->lon = f->alt = 0; f->hdop = -1; f->sats = -1; f->quality = -1;
	f->speed_mps = -1; f->course_deg = -1; f->utc_ms = -1; f->h_acc = f->v_acc = -1; f->seen = 0;
}

static int hexv(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

/* decimal number "[-]ddd[.ddd]" of length n; returns 0 on success */
static int num(const char *s, int n, double *out)
{
	int i = 0, neg = 0, any = 0; double v = 0, scale = 1;
	if (n <= 0) return -1;
	if (s[0] == '-') { neg = 1; i = 1; }
	for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) { v = v * 10 + (s[i] - '0'); any = 1; }
	if (i < n && s[i] == '.') {
		for (i++; i < n && s[i] >= '0' && s[i] <= '9'; i++) { scale /= 10; v += (s[i] - '0') * scale; any = 1; }
	}
	if (i != n || !any) return -1;
	*out = neg ? -v : v;
	return 0;
}

static int inum(const char *s, int n, int *out)
{
	double d;
	if (num(s, n, &d) || d != (double)(int)d) return -1;
	*out = (int)d;
	return 0;
}

/* ddmm.mmmm / dddmm.mmmm + hemisphere */
static int coord(const char *s, int n, char hemi, int degdigits, double *out)
{
	double d, m;
	if (n < degdigits + 2 || num(s, degdigits, &d) || num(s + degdigits, n - degdigits, &m) || m >= 60) return -1;
	d += m / 60.0;
	if (hemi == 'S' || hemi == 'W') d = -d;
	else if (hemi != 'N' && hemi != 'E') return -1;
	if ((degdigits == 2 && d > 90.0) || d < -180.0 || d > 180.0) return -1;
	*out = d;
	return 0;
}

static long long days_from_civil(int y, int m, int d)   /* H. Hinnant's algorithm */
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long long)doe - 719468;
}

int n2_nmea_feed(struct n2_fix *f, const char *line)
{
	const char *p = line, *fs[MAXF]; int fl[MAXF], nf = 0, n = 0;
	while (p[n] && p[n] != '\r' && p[n] != '\n') n++;
	if (n < 7 || p[0] != '$') return N2_NMEA_BADFMT;
	int end = n;
	for (int i = 1; i < n; i++) if (p[i] == '*') { end = i; break; }
	if (end < n) {                                   /* checksum present: must match */
		if (n - end < 3) return N2_NMEA_BADFMT;
		int h = hexv(p[end + 1]), l = hexv(p[end + 2]);
		if (h < 0 || l < 0) return N2_NMEA_BADFMT;
		unsigned char x = 0;
		for (int i = 1; i < end; i++) x ^= (unsigned char)p[i];
		if (x != (unsigned char)(h * 16 + l)) return N2_NMEA_BADSUM;
	}
	int start = 1;
	for (int i = 1; i <= end && nf < MAXF; i++) {
		if (i == end || p[i] == ',') { fs[nf] = p + start; fl[nf] = i - start; nf++; start = i + 1; }
	}
	if (nf < 1 || fl[0] != 5) return N2_NMEA_BADFMT;
	const char *t = fs[0] + 2;                       /* skip the talker id */
	if (t[0] == 'R' && t[1] == 'M' && t[2] == 'C') {
		/* 0 id,1 time,2 status,3 lat,4 N/S,5 lon,6 E/W,7 speed kn,8 course,9 date ddmmyy */
		if (nf < 10) return N2_NMEA_BADFMT;
		int ok = fl[2] == 1 && fs[2][0] == 'A';
		double lat, lon;
		if (ok) {
			if (fl[4] != 1 || fl[6] != 1 || coord(fs[3], fl[3], fs[4][0], 2, &lat) || coord(fs[5], fl[5], fs[6][0], 3, &lon)) return N2_NMEA_BADFMT;
			f->lat = lat; f->lon = lon;
		}
		double v;
		f->speed_mps = (fl[7] && !num(fs[7], fl[7], &v)) ? v * 0.514444 : -1;
		f->course_deg = (fl[8] && !num(fs[8], fl[8], &v)) ? v : -1;
		int dd, mo, yy; double hms;
		f->utc_ms = -1;
		if (fl[9] == 6 && fl[1] >= 6 && !inum(fs[9], 2, &dd) && !inum(fs[9] + 2, 2, &mo) && !inum(fs[9] + 4, 2, &yy) &&
		    !num(fs[1], fl[1], &hms) && mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31) {
			int hh = (int)(hms / 10000), mi = (int)(hms / 100) % 100; double ss = hms - hh * 10000 - mi * 100;
			long long days = days_from_civil(yy < 80 ? 2000 + yy : 1900 + yy, mo, dd);   /* two-digit year: 1980..2079 */
			f->utc_ms = days * 86400000LL + (long long)hh * 3600000LL + (long long)mi * 60000LL + (long long)(ss * 1000 + 0.5);
		}
		f->valid = ok;
		f->seen |= 1;
		return N2_NMEA_OK;
	}
	if (t[0] == 'G' && t[1] == 'G' && t[2] == 'A') {
		/* 0 id,1 time,2 lat,3 N/S,4 lon,5 E/W,6 quality,7 sats,8 hdop,9 alt,10 M */
		if (nf < 11) return N2_NMEA_BADFMT;
		int q;
		if (inum(fs[6], fl[6], &q)) return N2_NMEA_BADFMT;
		f->quality = q;
		if (q > 0) {
			double lat, lon;
			if (fl[3] != 1 || fl[5] != 1 || coord(fs[2], fl[2], fs[3][0], 2, &lat) || coord(fs[4], fl[4], fs[5][0], 3, &lon)) return N2_NMEA_BADFMT;
			f->lat = lat; f->lon = lon;
		}
		int s; double v;
		f->sats = (fl[7] && !inum(fs[7], fl[7], &s)) ? s : -1;
		f->hdop = (fl[8] && !num(fs[8], fl[8], &v)) ? v : -1;
		if (fl[9] && !num(fs[9], fl[9], &v)) f->alt = v;
		if (!(f->seen & 1)) f->valid = q > 0;            /* RMC (if seen) decides validity */
		f->seen |= 2;
		return N2_NMEA_OK;
	}
	return N2_NMEA_IGNORED;
}

void n2_fix_accuracy(struct n2_fix *f)
{
	f->h_acc = f->hdop > 0 ? f->hdop * 5.0 : 50.0;
	f->v_acc = f->hdop > 0 ? f->hdop * 8.0 : 80.0;
}
