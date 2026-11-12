/* SPDX-License-Identifier: MIT -- host unit test of nmea.c (test_host.sh) */
#include <stdio.h>
#include <math.h>
#include "nmea.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b, e) CHECK(fabs((a) - (b)) < (e))
int main(void)
{
	struct n2_fix f; n2_fix_init(&f);
	/* classic NMEA reference sentences (checksums computed for these exact strings) */
	CHECK(n2_nmea_feed(&f, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47") == N2_NMEA_OK);
	NEAR(f.lat, 48.1173, 1e-4); NEAR(f.lon, 11.516667, 1e-5); NEAR(f.alt, 545.4, 1e-9); CHECK(f.sats == 8); NEAR(f.hdop, 0.9, 1e-9);
	CHECK(f.quality == 1 && f.valid == 1);
	CHECK(n2_nmea_feed(&f, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A") == N2_NMEA_OK);
	NEAR(f.speed_mps, 22.4 * 0.514444, 1e-6); NEAR(f.course_deg, 84.4, 1e-9);
	CHECK(f.utc_ms == 764426119000LL);                 /* 1994-03-23 12:35:19 UTC */
	n2_fix_accuracy(&f); NEAR(f.h_acc, 4.5, 1e-9);
	/* bad checksum, south/west, void status, unknown sentence, GN talker, no checksum */
	CHECK(n2_nmea_feed(&f, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B") == N2_NMEA_BADSUM);
	CHECK(n2_nmea_feed(&f, "$GNRMC,101500.00,A,3352.128,S,15112.558,E,0.0,,121126,,,A") == N2_NMEA_OK);
	NEAR(f.lat, -33.8688, 1e-4); NEAR(f.lon, 151.209300, 1e-5); CHECK(f.course_deg == -1);
	CHECK(n2_nmea_feed(&f, "$GPRMC,101501,A,4045.000,N,07400.000,W,0.0,0.0,121126,,") == N2_NMEA_OK);
	NEAR(f.lon, -74.0, 1e-9); CHECK(f.utc_ms == 1794478501000LL);   /* 2026-11-12 10:15:01 UTC */
	CHECK(n2_nmea_feed(&f, "$GPRMC,101502,V,,,,,,,121126,,") == N2_NMEA_OK && f.valid == 0);
	CHECK(n2_nmea_feed(&f, "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74") == N2_NMEA_IGNORED);
	CHECK(n2_nmea_feed(&f, "$GPRMC,1,A,9907.038,N,01131.000,E,,,230394,,") == N2_NMEA_BADFMT);   /* latitude 99 */
	CHECK(n2_nmea_feed(&f, "garbage") == N2_NMEA_BADFMT);
	CHECK(n2_nmea_feed(&f, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n") == N2_NMEA_OK);
	printf(fails ? "NMEA TEST FAILED (%d)\n" : "NMEA TEST OK\n", fails);
	return fails != 0;
}
