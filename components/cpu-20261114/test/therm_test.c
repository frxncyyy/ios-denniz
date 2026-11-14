/* 0643 host test: n2g_therm_update2 (battery + SoC ladder). */
#include <stdio.h>
#include <stdlib.h>
#include "nord2_gov.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static unsigned
fresh(bool bok, int b, bool sok, int s, bool ap)
{
	struct n2g_therm t; n2g_therm_init(&t);
	return n2g_therm_update2(&t, bok, b, sok, s, ap);
}

int
main(void)
{
	/* no SoC: old battery steps, level 4 at 46.0 */
	CHECK(fresh(true, 4099, false, 0, false) == 0, "4099");
	CHECK(fresh(true, 4100, false, 0, false) == 1, "41");
	CHECK(fresh(true, 4300, false, 0, false) == 2, "43");
	CHECK(fresh(true, 4500, false, 0, false) == 3, "45");
	CHECK(fresh(true, 4600, false, 0, false) == 4, "46");
	/* SoC usable (60 C, exact cal): battery steps 43 / 44.5 / 45.5 / 46 */
	CHECK(fresh(true, 4200, true, 6000, false) == 0, "42 soc");
	CHECK(fresh(true, 4300, true, 6000, false) == 1, "43 soc");
	CHECK(fresh(true, 4450, true, 6000, false) == 2, "44.5 soc");
	CHECK(fresh(true, 4550, true, 6000, false) == 3, "45.5 soc");
	CHECK(fresh(true, 4600, true, 6000, false) == 4, "46 soc");
	/* SoC steps 85 / 90 / 95 / 100, +10 C with the default calibration */
	CHECK(fresh(true, 3500, true, 8500, false) == 1, "85");
	CHECK(fresh(true, 3500, true, 9500, false) == 3, "95");
	CHECK(fresh(true, 3500, true, 8500, true) == 3, "85 approx = 95");
	CHECK(fresh(true, 3500, true, 10000, false) == 4, "100");
	/* implausible SoC: ignored (old battery steps) */
	CHECK(fresh(true, 4200, true, 2000, false) == 1, "soc colder than battery - 15 C ignored");
	CHECK(fresh(true, 4200, true, 13000, false) == 1, "soc > 125 C ignored");
	n2g_therm_soc_off = true;
	CHECK(fresh(true, 4200, true, 6000, false) == 1, "soc off");
	n2g_therm_soc_off = false;
	/* nothing readable: keep (initial level 4) */
	CHECK(fresh(false, 0, false, 0, false) == 4, "unreadable");
	CHECK(fresh(false, 0, true, 6000, false) == 0, "soc only");
	/* hysteresis: down only when every source allows */
	{
		struct n2g_therm t; n2g_therm_init(&t);
		CHECK(n2g_therm_update2(&t, true, 4600, true, 6000, false) == 4, "up 4");
		CHECK(n2g_therm_update2(&t, true, 4550, true, 6000, false) == 4, "hyst bat 45.5 holds 4");
		CHECK(n2g_therm_update2(&t, true, 4480, true, 6000, false) == 3, "44.8 holds 3");
		CHECK(n2g_therm_update2(&t, true, 4380, true, 6000, false) == 2, "down to 2");
		CHECK(n2g_therm_update2(&t, true, 4000, true, 9300, false) == 2, "soc holds 2 (93 >= 90-3)");
		CHECK(n2g_therm_update2(&t, true, 4000, true, 8600, false) == 1, "soc 86: 1");
		CHECK(n2g_therm_update2(&t, true, 4000, true, 8100, false) == 0, "soc 81: 0");
	}
	/* random: never below the battery-only level - (soc thresholds only raise), level 4 whenever battery >= 46, monotone in each input */
	srand(3);
	for (int i = 0; i < 2000000; i++) {
		struct n2g_therm t; n2g_therm_init(&t);
		int n = rand() % 6;
		bool bok = true, sok = rand() % 3 != 0, ap = rand() & 1;
		int b = 3000, s = 5000;
		for (int k = 0; k <= n; k++) {
			b = 3000 + rand() % 2000; s = 2000 + rand() % 12000;
			bok = rand() % 10 != 0;
			n2g_therm_update2(&t, bok, b, sok, s, ap);
		}
		if (bok && b >= 4600) CHECK(t.level == 4, "46 C must be level 4 (b=%d)", b);
		bool usable = sok && s <= 12500 && s >= b - 1500;
		if (bok && ((usable && b < 4300 - 100 && s + (ap ? 1000 : 0) < 8500 - 300) || (!usable && b < 4100 - 100))) CHECK(t.level == 0, "cool must be 0 (b=%d s=%d lv=%u)", b, s, t.level);
		if (bok && !usable && b >= 4100) CHECK(t.level >= 1, "battery-only 41 C must be >= 1");
		CHECK(t.level <= 4, "range");
	}
	printf(fails ? "THERM_TEST FAILED %d\n" : "THERM_TEST_OK (%d checks)\n", fails ? fails : checks);
	return fails != 0;
}
