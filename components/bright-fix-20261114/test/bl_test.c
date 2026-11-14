/* host test of sweet_fb_backlight.h (0495).  cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_backlight.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define Q(n) ((int32_t)((n) * 65536.0))

int
main(void)
{
	struct sweet_bl_cfg c; struct sweet_bl b;
	sweet_bl_cfg_default(&c); sweet_bl_cfg_fix(&c); sweet_bl_init(&b);
	/* mapping: 400 nits = 2047, 200 = 1024 (rounded), 5 nits = 26, 0 / negative = floor, above full = max */
	CHECK(sweet_bl_q16_to_dbv(Q(400), &c, NULL) == 2047);
	CHECK(sweet_bl_q16_to_dbv(Q(200), &c, NULL) == 1024);
	CHECK(sweet_bl_q16_to_dbv(Q(5), &c, NULL) == 26);
	bool fl = false;
	CHECK(sweet_bl_q16_to_dbv(0, &c, &fl) == SWEET_BL_FLOOR_DEFAULT && fl);
	CHECK(sweet_bl_q16_to_dbv(-5, &c, NULL) == SWEET_BL_FLOOR_DEFAULT);
	CHECK(sweet_bl_q16_to_dbv(Q(1000), &c, NULL) == 2047);
	CHECK(sweet_bl_q16_to_dbv(INT32_MAX, &c, NULL) == 2047);
	/* monotonic over the whole 16.16 range used by CoreBrightness (0 .. 2000 nits) */
	uint32_t prev = 0;
	for (int32_t q = 0; q <= Q(2000); q += 97) { uint32_t d = sweet_bl_q16_to_dbv(q, &c, NULL); CHECK(d >= prev && d >= c.floor_dbv && d <= 2047); prev = d; if (fails > 20) { break; } }
	/* nothing before the first request; mode 0 sends nothing */
	CHECK(sweet_bl_target(&b, &c) == 0 && sweet_bl_due(&b, &c, 1000) == 0);
	sweet_bl_request(&b, &c, Q(200));
	CHECK(sweet_bl_due(&b, &c, 1000) == 1024);
	c.mode = 0; CHECK(sweet_bl_due(&b, &c, 1000) == 0); c.mode = 1;
	/* sent -> nothing more; a failure retries after the gap; the gap limits a fade */
	sweet_bl_sent(&b, 1024, true, 1000);
	CHECK(sweet_bl_due(&b, &c, 2000) == 0 && b.sends == 1);
	sweet_bl_request(&b, &c, Q(100));
	CHECK(sweet_bl_due(&b, &c, 1000 + SWEET_BL_MIN_GAP_US - 1) == 0);
	CHECK(sweet_bl_due(&b, &c, 1000 + SWEET_BL_MIN_GAP_US) == 512);
	sweet_bl_sent(&b, 512, false, 20000);
	CHECK(b.fails == 1 && b.sent_dbv == 1024 && sweet_bl_due(&b, &c, 20000 + SWEET_BL_MIN_GAP_US) == 512);
	/* the latest request wins: 60 Hz fade of 120 requests, sends at most every 8 ms, ends at the last value */
	uint64_t t = 100000; unsigned sends = 0;
	for (int i = 0; i < 120; i++) {
		sweet_bl_request(&b, &c, Q(400 - i * 3));
		for (int k = 0; k < 4; k++) { t += 4166; uint32_t d = sweet_bl_due(&b, &c, t); if (d) { sweet_bl_sent(&b, d, true, t); sends++; } }
	}
	for (int k = 0; k < 4; k++) { t += 4166; uint32_t d = sweet_bl_due(&b, &c, t); if (d) { sweet_bl_sent(&b, d, true, t); sends++; } }
	CHECK(b.sent_dbv == sweet_bl_q16_to_dbv(Q(400 - 119 * 3), &c, NULL));
	CHECK(sends <= 120 * 4166 * 4 / SWEET_BL_MIN_GAP_US + 4);
	/* thermal: warm caps at 70 %, hot 50 %, hysteresis, invalid readings keep the level, cap never under the floor */
	sweet_bl_request(&b, &c, Q(400));
	CHECK(sweet_bl_therm_update(&b, true, 4250, 1) && b.therm == 1 && sweet_bl_target(&b, &c) == SWEET_BL_WARM_CAP);
	CHECK(!sweet_bl_therm_update(&b, true, 4150, 2) && b.therm == 1);      /* within 1 C */
	CHECK(sweet_bl_therm_update(&b, true, 4600, 3) && b.therm == 2 && sweet_bl_target(&b, &c) == SWEET_BL_HOT_CAP);
	CHECK(!sweet_bl_therm_update(&b, false, 2000, 4) && b.therm == 2);
	CHECK(!sweet_bl_therm_update(&b, true, 4450, 5) && b.therm == 2);
	CHECK(sweet_bl_therm_update(&b, true, 4300, 6) && b.therm == 1);
	CHECK(sweet_bl_therm_update(&b, true, 4000, 7) && b.therm == 0 && sweet_bl_target(&b, &c) == 2047);
	sweet_bl_therm_update(&b, true, 5000, 8);
	c.floor_dbv = 1500; sweet_bl_cfg_fix(&c); sweet_bl_remap(&b, &c);
	CHECK(sweet_bl_target(&b, &c) == 1500);
	/* config sanitising */
	struct sweet_bl_cfg x = { 7, 0, 9999, 5 }; sweet_bl_cfg_fix(&x);
	CHECK(x.mode == 1 && x.floor_dbv == 4 && x.max_dbv == 2047 && x.nits_full == 100);
	struct sweet_bl_cfg y = { 0, 3000, 100, 5000 }; sweet_bl_cfg_fix(&y);
	CHECK(y.mode == 0 && y.max_dbv == 100 && y.floor_dbv == 100 && y.nits_full == 1000);
	/* packet */
	uint8_t p[3];
	sweet_bl_packet(0x7ff, p); CHECK(p[0] == 0x51 && p[1] == 0x07 && p[2] == 0xff);
	sweet_bl_packet(5000, p); CHECK(p[1] == 0x07 && p[2] == 0xff);      /* never HBM */
	sweet_bl_packet(26, p); CHECK(p[1] == 0 && p[2] == 26);
	/* random: target always in [floor, min(max, 2047)] and never above the thermal cap unless the floor is higher */
	srand(1);
	for (int i = 0; i < 200000; i++) {
		struct sweet_bl_cfg r = { (uint32_t)(rand() & 1), (uint32_t)(rand() % 5000), (uint32_t)(rand() % 5000), (uint32_t)(rand() % 2000) };
		sweet_bl_cfg_fix(&r);
		struct sweet_bl s; sweet_bl_init(&s);
		sweet_bl_request(&s, &r, (int32_t)((uint32_t)rand() * 2u + (uint32_t)(rand() & 1)));
		sweet_bl_therm_update(&s, true, rand() % 6000, 1);
		uint32_t tg = sweet_bl_target(&s, &r), cap = sweet_bl_cap(&s);
		if (!(tg >= r.floor_dbv && tg <= r.max_dbv && tg <= 2047 && (tg <= cap || tg == r.floor_dbv))) {
			printf("FAIL random cfg %u %u %u %u req %d therm %u -> %u\n", r.mode, r.floor_dbv, r.max_dbv, r.nits_full, s.req_q16, s.therm, tg); fails++; break;
		}
		uint32_t d = sweet_bl_due(&s, &r, 5);
		if (d != 0 && (d != tg || !r.mode)) { printf("FAIL random due\n"); fails++; break; }
	}
	printf("%s (%d failures)\n", fails ? "BL_TEST_FAIL" : "BL_TEST_OK", fails);
	return fails != 0;
}
