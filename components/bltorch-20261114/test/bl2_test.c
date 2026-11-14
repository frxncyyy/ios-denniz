/* host test of sweet_fb_backlight.h after 0501 (legacy "brightness" requests, safe floor, architecture).
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_backlight.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define Q(n) ((int32_t)((n) * 65536.0))

static unsigned
pump(struct sweet_bl *b, const struct sweet_bl_cfg *c, uint64_t *t)
{
	unsigned sends = 0;
	for (int k = 0; k < 4; k++) { *t += 4166; uint32_t d = sweet_bl_due(b, c, *t); if (d) { sweet_bl_sent(b, d, true, *t); sends++; } }
	return sends;
}

int
main(void)
{
	struct sweet_bl_cfg c; struct sweet_bl b; uint64_t t = 1000;
	sweet_bl_cfg_default(&c); sweet_bl_cfg_fix(&c); sweet_bl_init(&b);
	CHECK(c.arch == 0 && c.safe_dbv == SWEET_BL_SAFE_DEFAULT && c.floor_dbv == SWEET_BL_FLOOR_DEFAULT);

	/* ---- the q10jch boot, legacy architecture: one "brightness-nits" 0 (+ IOMFBBrightnessLevel 0): not used, nothing sent, the panel
	 * keeps its boot level */
	CHECK(sweet_bl_request(&b, &c, 0) == 0 && sweet_bl_request(&b, &c, 0) == 0);
	CHECK(b.nits_ignored == 2 && b.nits_reqs == 2 && b.reqs == 2 && b.req_kind == SWEET_BL_REQ_NONE);
	CHECK(sweet_bl_target(&b, &c) == 0 && pump(&b, &c, &t) == 0 && b.sends == 0);
	/* a thermal change with no request sends nothing either */
	CHECK(sweet_bl_therm_update(&b, true, 4250, t) && sweet_bl_due(&b, &c, t + 100000) == 0);
	CHECK(sweet_bl_therm_update(&b, true, 3900, t) && b.therm == 0);

	/* ---- legacy requests before any level above the safe floor: display off (0), a dim saved level (120): the safe floor is shown */
	CHECK(sweet_bl_request_raw(&b, &c, 0) == SWEET_BL_SAFE_DEFAULT && !b.trusted && b.floored == 1);
	CHECK(pump(&b, &c, &t) == 1 && b.sent_dbv == SWEET_BL_SAFE_DEFAULT);
	CHECK(sweet_bl_request_raw(&b, &c, 120) == SWEET_BL_SAFE_DEFAULT && !b.trusted);
	CHECK(sweet_bl_request_raw(&b, &c, SWEET_BL_SAFE_DEFAULT) == SWEET_BL_SAFE_DEFAULT && !b.trusted);      /* at the level, not above */
	CHECK(pump(&b, &c, &t) == 0);
	/* the slider goes to the middle: trusted from now on, the ordinary floor applies */
	CHECK(sweet_bl_request_raw(&b, &c, 1024) == 1024 && b.trusted);
	CHECK(pump(&b, &c, &t) == 1 && b.sent_dbv == 1024);
	CHECK(sweet_bl_request_raw(&b, &c, 120) == 120);
	CHECK(sweet_bl_request_raw(&b, &c, 0) == SWEET_BL_FLOOR_DEFAULT);
	CHECK(pump(&b, &c, &t) == 1 && b.sent_dbv == SWEET_BL_FLOOR_DEFAULT);
	/* range: negative, above the scale, the whole scale monotonic */
	CHECK(sweet_bl_request_raw(&b, &c, -7) == SWEET_BL_FLOOR_DEFAULT && b.req_raw == 0);
	CHECK(sweet_bl_request_raw(&b, &c, 5000) == 2047 && b.req_raw == 2047);
	CHECK(sweet_bl_request_raw(&b, &c, INT32_MAX) == 2047);
	uint32_t prev = 0;
	for (int32_t r = 0; r <= 2047; r++) { uint32_t d = sweet_bl_request_raw(&b, &c, r); CHECK(d >= prev && d >= c.floor_dbv && d <= 2047 && (r < 16 || d == (uint32_t)r)); prev = d; if (fails > 20) { break; } }
	/* a slider sweep at 60 requests a second: at most one DCS per 8 ms, ends on the last value */
	unsigned sends = 0;
	for (int i = 0; i < 120; i++) { sweet_bl_request_raw(&b, &c, 2047 - i * 15); sends += pump(&b, &c, &t); }
	sends += pump(&b, &c, &t);
	CHECK(b.sent_dbv == 2047 - 119 * 15 && sends <= 120 * 4166 * 4 / SWEET_BL_MIN_GAP_US + 4);
	/* legacy architecture: a nits key in between changes nothing */
	CHECK(sweet_bl_request(&b, &c, Q(400)) == 0 && b.want_dbv == 2047 - 119 * 15 && b.req_kind == SWEET_BL_REQ_RAW);
	/* thermal cap on a legacy request; the cap never goes under the floor in force */
	sweet_bl_request_raw(&b, &c, 2047);
	CHECK(sweet_bl_therm_update(&b, true, 4250, t) && sweet_bl_target(&b, &c) == SWEET_BL_WARM_CAP);
	CHECK(sweet_bl_therm_update(&b, true, 4600, t) && sweet_bl_target(&b, &c) == SWEET_BL_HOT_CAP);
	c.floor_dbv = 1500; sweet_bl_cfg_fix(&c); sweet_bl_remap(&b, &c);
	CHECK(sweet_bl_target(&b, &c) == 1500);
	c.floor_dbv = SWEET_BL_FLOOR_DEFAULT; sweet_bl_cfg_fix(&c); sweet_bl_remap(&b, &c);
	CHECK(sweet_bl_therm_update(&b, true, 3000, t) && sweet_bl_target(&b, &c) == 2047);
	/* kern.n2bl_max and kern.n2bl=0 */
	c.max_dbv = 1000; sweet_bl_cfg_fix(&c); sweet_bl_remap(&b, &c); CHECK(sweet_bl_target(&b, &c) == 1000);
	c.max_dbv = 2047; sweet_bl_cfg_fix(&c); sweet_bl_remap(&b, &c); CHECK(sweet_bl_target(&b, &c) == 2047);
	c.mode = 0; CHECK(sweet_bl_due(&b, &c, t + 1000000) == 0); c.mode = 1;
	/* display on again (sent_dbv forgotten): the level is sent again */
	pump(&b, &c, &t); b.sent_dbv = 0; CHECK(pump(&b, &c, &t) == 1 && b.sent_dbv == 2047);

	/* ---- safe floor settings: off (0), raised over the request, never over max, floor above safe wins */
	struct sweet_bl b2; sweet_bl_init(&b2);
	c.safe_dbv = 0; sweet_bl_cfg_fix(&c);
	CHECK(sweet_bl_request_raw(&b2, &c, 0) == SWEET_BL_FLOOR_DEFAULT && !b2.trusted && sweet_bl_floor(&b2, &c) == SWEET_BL_FLOOR_DEFAULT);
	c.safe_dbv = 600; sweet_bl_cfg_fix(&c); sweet_bl_remap(&b2, &c);
	CHECK(b2.want_dbv == 600 && sweet_bl_request_raw(&b2, &c, 500) == 600 && sweet_bl_request_raw(&b2, &c, 601) == 601 && b2.trusted);
	c.safe_dbv = 4000; sweet_bl_cfg_fix(&c); CHECK(c.safe_dbv == c.max_dbv);
	sweet_bl_init(&b2); c.safe_dbv = 100; c.floor_dbv = 200; sweet_bl_cfg_fix(&c);
	CHECK(sweet_bl_request_raw(&b2, &c, 0) == 200);
	sweet_bl_cfg_default(&c); sweet_bl_cfg_fix(&c);

	/* ---- the new architecture (n2bl=2): nits are the level; 0 nits at start gives the safe floor, not a dark panel */
	struct sweet_bl n; sweet_bl_init(&n); c.arch = 1; sweet_bl_cfg_fix(&c); CHECK(c.arch == 1);
	CHECK(sweet_bl_request(&n, &c, 0) == SWEET_BL_SAFE_DEFAULT && n.nits_ignored == 0 && n.req_kind == SWEET_BL_REQ_NITS);
	CHECK(pump(&n, &c, &t) == 1 && n.sent_dbv == SWEET_BL_SAFE_DEFAULT);
	CHECK(sweet_bl_request(&n, &c, Q(5)) == SWEET_BL_SAFE_DEFAULT && !n.trusted);        /* 5 nits = DBV 26 */
	CHECK(sweet_bl_request(&n, &c, Q(200)) == 1024 && n.trusted);
	CHECK(sweet_bl_request(&n, &c, Q(5)) == 26 && sweet_bl_request(&n, &c, 0) == SWEET_BL_FLOOR_DEFAULT && sweet_bl_request(&n, &c, -5) == SWEET_BL_FLOOR_DEFAULT);
	CHECK(sweet_bl_request(&n, &c, Q(400)) == 2047 && sweet_bl_request(&n, &c, Q(1000)) == 2047 && sweet_bl_request(&n, &c, INT32_MAX) == 2047);
	prev = 0;
	for (int32_t q = 0; q <= Q(2000); q += 97) { uint32_t d = sweet_bl_request(&n, &c, q); CHECK(d >= prev && d >= c.floor_dbv && d <= 2047); prev = d; if (fails > 20) { break; } }
	/* kern.n2bl_nits re-maps the last nits request; a legacy key is still honoured in this architecture */
	sweet_bl_request(&n, &c, Q(200)); c.nits_full = 800; sweet_bl_cfg_fix(&c); sweet_bl_remap(&n, &c); CHECK(n.want_dbv == 512);
	CHECK(sweet_bl_request_raw(&n, &c, 700) == 700 && n.req_kind == SWEET_BL_REQ_RAW);
	/* the packet */
	uint8_t p[3]; sweet_bl_packet(1024, p); CHECK(p[0] == 0x51 && p[1] == 0x04 && p[2] == 0x00);
	sweet_bl_packet(300, p); CHECK(p[1] == 0x01 && p[2] == 0x2c);
	sweet_bl_packet(9999, p); CHECK(p[1] == 0x07 && p[2] == 0xff);
	printf(fails ? "BL2 TEST FAILED (%d)\n" : "bl2_test: all checks passed%.0d\n", fails);
	return fails != 0;
}
