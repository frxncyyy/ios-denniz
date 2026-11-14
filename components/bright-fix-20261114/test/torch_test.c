/* host test of nord2_torch.h (0496) plus a model of the MT6360 FLED register sequence of sweet_touch.c n2t_hw_on/off.
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nord2_torch.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- MT6360 PMU register model, same code shape as sweet_touch.c (write = write + read-back compare) ---- */
static uint8_t reg[256];
static unsigned writes, write_fail_at;      /* write_fail_at: 1-based index of a write that fails (0 = none) */
static uint8_t wlog_r[64], wlog_v[64];
static bool n2t_pmu_read(uint8_t r, uint8_t *v) { *v = reg[r]; return true; }
static bool
n2t_pmu_write(uint8_t r, uint8_t v)
{
	writes++;
	if (writes == write_fail_at) { return false; }
	if (writes <= 64) { wlog_r[writes - 1] = r; wlog_v[writes - 1] = v; }
	/* bits 7..5 of the TOR registers and bits 7..4 of FLED_EN are other fields: preserved by the caller (checked below) */
	reg[r] = v;
	return true;
}
static void delay(unsigned us) { (void)us; }
/* copied from sweet_touch.c (0496) */
static bool
n2t_hw_off(void)
{
	uint8_t en = 0;
	if (!n2t_pmu_read(N2T_REG_FLED_EN, &en)) { return false; }
	return (en & (N2T_EN_ON | N2T_EN_STROBE)) == 0 || n2t_pmu_write(N2T_REG_FLED_EN, (uint8_t)(en & ~(N2T_EN_ON | N2T_EN_STROBE)));
}
static bool
n2t_hw_on(uint32_t sel, bool was_on)
{
	uint8_t t1 = 0, t2 = 0, en = 0;
	if (sel > N2T_SEL_HW_MAX) { sel = N2T_SEL_HW_MAX; }
	if (!n2t_pmu_read(N2T_REG_FLED1_TOR, &t1) || !n2t_pmu_read(N2T_REG_FLED2_TOR, &t2) || !n2t_pmu_read(N2T_REG_FLED_EN, &en)) { return false; }
	if (!n2t_pmu_write(N2T_REG_FLED1_TOR, (uint8_t)((t1 & ~N2T_ITOR_MASK) | sel)) ||
	    !n2t_pmu_write(N2T_REG_FLED2_TOR, (uint8_t)((t2 & ~N2T_ITOR_MASK) | sel))) { return false; }
	if (was_on && (en & N2T_EN_ON) == N2T_EN_ON && !(en & N2T_EN_STROBE)) { return true; }      /* current change only */
	if ((en & N2T_EN_STROBE) && !n2t_pmu_write(N2T_REG_FLED_EN, (uint8_t)(en & ~N2T_EN_STROBE))) { return false; }
	delay(500);
	return n2t_pmu_write(N2T_REG_FLED_EN, (uint8_t)((en & ~N2T_EN_STROBE) | N2T_EN_ON));
}

static void
req(struct n2t_req *r, uint32_t level, uint32_t ms)
{
	r->seq++; r->level = level; r->ms = ms;
}

int
main(void)
{
	struct n2t_cfg c; struct n2t_state s; struct n2t_req r = { 0, 0, 0 }; uint32_t sel = 0;
	n2t_cfg_default(&c); n2t_cfg_fix(&c); n2t_init(&s);
	/* parse */
	uint8_t b[8] = { 0xe8, 0x03, 0, 0, 0x2c, 0x01, 0, 0 };      /* 1000, 300 ms */
	uint32_t l = 0, m = 0;
	CHECK(n2t_parse(b, 4, &l, &m) && l == 1000 && m == 0);
	CHECK(n2t_parse(b, 8, &l, &m) && l == 1000 && m == 300);
	CHECK(!n2t_parse(b, 3, &l, &m) && !n2t_parse(b, 5, &l, &m));
	b[0] = 0xe9; CHECK(!n2t_parse(b, 4, &l, &m));                   /* 1001 */
	b[0] = 0xe8; b[4] = 0; b[5] = 0; CHECK(!n2t_parse(b, 8, &l, &m));  /* ms 0 */
	b[4] = 0xe9; b[5] = 0x03; CHECK(!n2t_parse(b, 8, &l, &m));       /* 1001 ms */
	/* level -> selector: never above the ceiling, never above the hard maximum */
	CHECK(n2t_level_sel(1000, 6) == 6 && n2t_level_sel(500, 6) == 3 && n2t_level_sel(1, 6) == 0 && n2t_level_sel(1000, 99) == N2T_SEL_HW_MAX);
	for (uint32_t lv = 0; lv <= 1000; lv++) { for (uint32_t ce = 0; ce <= 40; ce++) { uint32_t x = n2t_level_sel(lv, ce); CHECK(x <= ce && x <= N2T_SEL_HW_MAX); if (fails) { goto out; } } }
	/* nothing requested: dark */
	CHECK(!n2t_decide(&s, &c, 0, &sel));
	/* torch at 1.0 -> maxsel (6 = 100 mA per channel) */
	req(&r, 1000, 0); n2t_take(&s, &r, 1000);
	CHECK(n2t_decide(&s, &c, 1000, &sel) && sel == N2T_MAXSEL_DEFAULT);
	/* the same request is not taken twice; it times out after max_on_s and stays dark until a new write */
	n2t_take(&s, &r, 5000000); CHECK(s.reqs == 1);
	CHECK(n2t_decide(&s, &c, 1000 + (uint64_t)c.max_on_s * 1000000u - 1, &sel));
	CHECK(!n2t_decide(&s, &c, 1000 + (uint64_t)c.max_on_s * 1000000u, &sel) && s.expired && s.timeouts == 1);
	CHECK(!n2t_decide(&s, &c, 2000000000ull, &sel));
	req(&r, 250, 0); n2t_take(&s, &r, 2000000000ull);
	CHECK(n2t_decide(&s, &c, 2000000000ull, &sel) && sel == 2);
	/* off request, kill switch */
	req(&r, 0, 0); n2t_take(&s, &r, 2000000100ull); CHECK(!n2t_decide(&s, &c, 2000000100ull, &sel));
	req(&r, 1000, 0); n2t_take(&s, &r, 2000000200ull); c.mode = 0; CHECK(!n2t_decide(&s, &c, 2000000200ull, &sel)); c.mode = 1;
	CHECK(n2t_decide(&s, &c, 2000000300ull, &sel));
	/* thermal: warm -> sel 0, hot -> off, hot clears only under 44 C, invalid readings keep the level */
	CHECK(n2t_therm_update(&s, true, 4350, 1) && s.therm == 1 && n2t_decide(&s, &c, 2000000400ull, &sel) && sel == 0);
	CHECK(n2t_therm_update(&s, true, 4650, 2) && s.therm == 2 && !n2t_decide(&s, &c, 2000000500ull, &sel));
	CHECK(!n2t_therm_update(&s, false, 2000, 3) && s.therm == 2);
	CHECK(!n2t_therm_update(&s, true, 4450, 4) && s.therm == 2);
	CHECK(n2t_therm_update(&s, true, 4350, 5) && s.therm == 1);
	CHECK(!n2t_therm_update(&s, true, 4250, 6) && s.therm == 1);      /* 1 C hysteresis under 43 C */
	CHECK(n2t_therm_update(&s, true, 4100, 7) && s.therm == 0);
	/* flash pulse: flashsel for ms, then off and a 4 x ms cool-down in which a pulse only gets the torch ceiling */
	n2t_init(&s); r.seq = 100;
	req(&r, 1000, 200); n2t_take(&s, &r, 10000000);
	CHECK(n2t_decide(&s, &c, 10000000, &sel) && sel == N2T_FLASHSEL_DEFAULT && s.flashes == 1);
	CHECK(n2t_decide(&s, &c, 10000000 + 199999, &sel));
	CHECK(!n2t_decide(&s, &c, 10000000 + 200000, &sel) && s.expired && s.cool_until_us == 10200000 + 800000);
	req(&r, 1000, 200); n2t_take(&s, &r, 10500000);
	CHECK(s.flash_cooled == 1 && n2t_decide(&s, &c, 10500000, &sel) && sel == N2T_MAXSEL_DEFAULT);
	CHECK(!n2t_decide(&s, &c, 10700000, &sel));
	req(&r, 1000, 100); n2t_take(&s, &r, 11600000);
	CHECK(s.flashes == 2 && n2t_decide(&s, &c, 11600000, &sel) && sel == N2T_FLASHSEL_DEFAULT);
	/* duty over a long run of back-to-back pulse requests stays <= 1/(1+4) at flash current */
	{
		struct n2t_state f; n2t_init(&f); struct n2t_req q = { 500, 0, 0 };
		uint64_t t = 1000000, flash_us = 0;
		for (int i = 0; i < 20000; i++) {
			if (i % 10 == 0) { req(&q, 1000, 50); n2t_take(&f, &q, t); }
			uint32_t x = 0;
			if (n2t_decide(&f, &c, t, &x) && x == N2T_FLASHSEL_DEFAULT) { flash_us += 10000; }
			t += 10000;
		}
		CHECK(flash_us * 5 <= (t - 1000000) + 100000);
	}
	/* fault latch keeps it dark */
	s.latched = true; req(&r, 1000, 0); n2t_take(&s, &r, 20000000); CHECK(!n2t_decide(&s, &c, 20000000, &sel)); s.latched = false;
	/* config sanitising */
	struct n2t_cfg x = { 5, 99, 99, 1 }; n2t_cfg_fix(&x); CHECK(x.mode == 1 && x.maxsel == N2T_SEL_HW_MAX && x.flashsel == N2T_SEL_HW_MAX && x.max_on_s == N2T_ON_S_MIN);
	struct n2t_cfg y = { 0, 3, 3, 99999 }; n2t_cfg_fix(&y); CHECK(y.mode == 0 && y.max_on_s == N2T_ON_S_MAX);
	/* register sequence: other bits preserved, strobe never set, off clears CS1/CS2/TORCH/STROBE only */
	memset(reg, 0, sizeof(reg)); reg[N2T_REG_FLED1_TOR] = 0xa1; reg[N2T_REG_FLED2_TOR] = 0x60; reg[N2T_REG_FLED_EN] = 0x84;     /* bit 7 other, strobe left set */
	writes = 0;
	CHECK(n2t_hw_on(6, false));
	CHECK(reg[N2T_REG_FLED1_TOR] == 0xa6 && reg[N2T_REG_FLED2_TOR] == 0x66 && reg[N2T_REG_FLED_EN] == (0x80 | N2T_EN_ON));
	CHECK(writes == 4 && wlog_r[2] == N2T_REG_FLED_EN && wlog_v[2] == 0x80 && wlog_r[3] == N2T_REG_FLED_EN);     /* strobe off first */
	writes = 0; CHECK(n2t_hw_on(3, true) && writes == 2 && (reg[N2T_REG_FLED1_TOR] & 0x1f) == 3);              /* current change only */
	writes = 0; CHECK(n2t_hw_on(40, true) && (reg[N2T_REG_FLED2_TOR] & 0x1f) == N2T_SEL_HW_MAX);
	writes = 0; CHECK(n2t_hw_off() && reg[N2T_REG_FLED_EN] == 0x80 && writes == 1);
	writes = 0; CHECK(n2t_hw_off() && writes == 0);                                                               /* already off */
	for (unsigned i = 0; i < 256; i++) { if (i != N2T_REG_FLED1_TOR && i != N2T_REG_FLED2_TOR && i != N2T_REG_FLED_EN) { CHECK(reg[i] == 0); } }
	/* a failing write reports failure (the caller counts it and latches after 3) */
	reg[N2T_REG_FLED_EN] = 0; writes = 0; write_fail_at = 1; CHECK(!n2t_hw_on(6, false)); write_fail_at = 0;
	CHECK((reg[N2T_REG_FLED_EN] & N2T_EN_ON) == 0);
out:
	printf("%s (%d failures)\n", fails ? "TORCH_TEST_FAIL" : "TORCH_TEST_OK", fails);
	return fails != 0;
}
