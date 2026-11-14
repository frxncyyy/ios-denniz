/* host test of the 0560 strobe additions to nord2_torch.h + the MT6360 STROBE register sequence of sweet_touch.c n2t_hw_strobe.
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nord2_torch.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- MT6360 PMU register model (write = write + read-back compare, as sweet_touch.c n2t_pmu_write) ---- */
static uint8_t reg[256];
static unsigned writes, write_fail_at;
static uint8_t wlog_r[64], wlog_v[64];
static uint64_t fake_time = 1000;
static uint64_t mach_absolute_time(void) { return ++fake_time; }
static bool n2t_pmu_read(uint8_t r, uint8_t *v) { *v = reg[r]; return true; }
static bool
n2t_pmu_write(uint8_t r, uint8_t v)
{
	writes++;
	if (writes == write_fail_at) { return false; }
	if (writes <= 64) { wlog_r[writes - 1] = r; wlog_v[writes - 1] = v; }
	reg[r] = v;
	return true;
}
static void delay(unsigned us) { (void)us; }
/* copied from sweet_touch.c (0496 / 0560; test/run.sh checks the copies are byte-identical) */
static bool
n2t_hw_off(void)
{
	uint8_t en = 0;
	if (!n2t_pmu_read(N2T_REG_FLED_EN, &en)) { return false; }
	return (en & (N2T_EN_ON | N2T_EN_STROBE)) == 0 || n2t_pmu_write(N2T_REG_FLED_EN, (uint8_t)(en & ~(N2T_EN_ON | N2T_EN_STROBE)));
}
static bool
n2t_hw_strobe(uint32_t sel, uint32_t ms, uint64_t *on_t)
{
	uint8_t s1 = 0, s2 = 0, to = 0, en = 0;
	if (sel > N2T_STRB_HW_MAX) { sel = N2T_STRB_HW_MAX; }
	if (!n2t_pmu_read(N2T_REG_FLED1_STRB2, &s1) || !n2t_pmu_read(N2T_REG_FLED2_STRB2, &s2) || !n2t_pmu_read(N2T_REG_FLED_STRB_CTRL, &to) ||
	    !n2t_pmu_read(N2T_REG_FLED_EN, &en)) { return false; }
	if (!n2t_pmu_write(N2T_REG_FLED_STRB_CTRL, (uint8_t)((to & ~N2T_STRB_TO_MASK) | n2t_strobe_to_sel(ms))) ||
	    !n2t_pmu_write(N2T_REG_FLED1_STRB2, (uint8_t)((s1 & ~(N2T_UTRAL_ISTRB | N2T_ISTRB_MASK)) | N2T_UTRAL_ISTRB | sel)) ||
	    !n2t_pmu_write(N2T_REG_FLED2_STRB2, (uint8_t)((s2 & ~(N2T_UTRAL_ISTRB | N2T_ISTRB_MASK)) | N2T_UTRAL_ISTRB | sel))) { return false; }
	if ((en & (N2T_EN_STROBE | N2T_EN_TORCH)) && !n2t_pmu_write(N2T_REG_FLED_EN, (uint8_t)(en & ~(N2T_EN_STROBE | N2T_EN_TORCH)))) { return false; }
	delay(400);
	en = (uint8_t)((en & ~N2T_EN_TORCH) | N2T_EN_CS1 | N2T_EN_CS2 | N2T_EN_STROBE);
	bool ok = n2t_pmu_write(N2T_REG_FLED_EN, en);
	*on_t = mach_absolute_time();
	return ok;
}

static void
le(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }

static void
test_parse(void)
{
	uint8_t b[12];
	uint32_t l, m, s;
	le(b, 1000); le(b + 4, 150); le(b + 8, 1);
	CHECK(n2t_parse12(b, 12, &l, &m, &s) && l == 1000 && m == 150 && s == 1);
	le(b + 4, N2T_STROBE_MS_MAX); CHECK(n2t_parse12(b, 12, &l, &m, &s));
	le(b + 4, N2T_STROBE_MS_MAX + 1); CHECK(!n2t_parse12(b, 12, &l, &m, &s));
	le(b + 4, 0); CHECK(!n2t_parse12(b, 12, &l, &m, &s));
	le(b + 4, 800); le(b + 8, 0); CHECK(n2t_parse12(b, 12, &l, &m, &s) && s == 0);       /* a 12-byte pulse: the 0496 limit */
	le(b + 4, 1001); CHECK(!n2t_parse12(b, 12, &l, &m, &s));
	le(b + 4, 100); le(b + 8, 2); CHECK(!n2t_parse12(b, 12, &l, &m, &s));               /* unknown flag */
	le(b, 1001); le(b + 8, 1); CHECK(!n2t_parse12(b, 12, &l, &m, &s));
	CHECK(!n2t_parse12(b, 8, &l, &m, &s));
}

static void
test_sel(void)
{
	CHECK(n2t_strobe_sel(1000, N2T_STROBESEL_DEFAULT) == N2T_STROBESEL_DEFAULT);
	CHECK(n2t_strobe_ma(N2T_STROBESEL_DEFAULT) == 500);
	CHECK(n2t_strobe_ma(N2T_STRB_HW_MAX) == 750);
	CHECK(n2t_strobe_sel(1, N2T_STROBESEL_DEFAULT) == 1);
	CHECK(n2t_strobe_sel(0, N2T_STROBESEL_DEFAULT) == 0);
	CHECK(n2t_strobe_sel(1000, 0xFF) == N2T_STRB_HW_MAX);
	for (uint32_t c = 0; c <= 0xFF; c++) {
		for (uint32_t l = 0; l <= 1000; l++) { CHECK(n2t_strobe_ma(n2t_strobe_sel(l, c)) <= 750); }
	}
	for (uint32_t ms = 1; ms <= N2T_STROBE_MS_MAX; ms++) {
		uint32_t n = n2t_strobe_to_sel(ms), hw = 64u + 32u * n;
		CHECK(hw >= ms + N2T_STRB_TO_MARGIN_MS);
		CHECK(n == 0 || hw - 32u < ms + N2T_STRB_TO_MARGIN_MS);      /* the smallest that covers it */
		CHECK(n <= 74u);
	}
	struct n2t_cfg c = { 1, 99, 99, 5, 7, 0xFF };
	n2t_cfg_fix(&c);
	CHECK(c.strobe == 1 && c.strobesel == N2T_STRB_HW_MAX && c.maxsel == N2T_SEL_HW_MAX);
	n2t_cfg_default(&c);
	CHECK(c.strobe == 1 && c.strobesel == N2T_STROBESEL_DEFAULT);
}

static void
test_decide(void)
{
	struct n2t_cfg c; n2t_cfg_default(&c);
	struct n2t_state s; n2t_init(&s);
	uint32_t sel = 99;
	uint64_t t = 1000000;
	struct n2t_req r = { 1, 1000, 150, 1 };
	n2t_take(&s, &r, t);
	CHECK(s.strobe && s.flashes == 1);
	CHECK(n2t_decide_kind(&s, &c, t, &sel) == 2 && sel == N2T_STROBESEL_DEFAULT);
	CHECK(n2t_decide_kind(&s, &c, t + 149999, &sel) == 2);
	CHECK(n2t_decide_kind(&s, &c, t + 150000, &sel) == 0 && s.expired && s.cool_until_us == t + 150000 + 600000);
	/* a strobe inside the cool-down: torch current pulse */
	struct n2t_req r2 = { 2, 1000, 150, 1 };
	n2t_take(&s, &r2, t + 300000);
	CHECK(!s.strobe && s.strobe_cooled == 1);
	CHECK(n2t_decide_kind(&s, &c, t + 300000, &sel) == 1 && sel == c.maxsel);
	/* after the cool-down: strobe again */
	CHECK(n2t_decide_kind(&s, &c, t + 450000, &sel) == 0 && s.cool_until_us == t + 450000 + 600000);   /* the pulse's own cool-down */
	struct n2t_req r3 = { 3, 500, 100, 1 };
	n2t_take(&s, &r3, t + 1050000);
	CHECK(n2t_decide_kind(&s, &c, t + 1050000, &sel) == 2 && sel == n2t_strobe_sel(500, c.strobesel));
	/* strobe switched off by configuration: a 0496 pulse at flashsel */
	c.strobe = 0;
	CHECK(n2t_decide_kind(&s, &c, t + 1050001, &sel) == 1 && sel == n2t_level_sel(500, c.flashsel) && s.strobe_as_pulse == 1);
	c.strobe = 1;
	/* warm: lowest-current torch pulse; hot: dark */
	n2t_init(&s);
	struct n2t_req r4 = { 4, 1000, 150, 1 };
	n2t_take(&s, &r4, t);
	s.therm = 1;
	CHECK(n2t_decide_kind(&s, &c, t, &sel) == 1 && sel == 0);
	n2t_init(&s); n2t_take(&s, &r4, t); s.therm = 2;
	CHECK(n2t_decide_kind(&s, &c, t, &sel) == 0);
	/* kern.n2torch=0: dark */
	n2t_init(&s); n2t_take(&s, &r4, t); c.mode = 0;
	CHECK(n2t_decide_kind(&s, &c, t, &sel) == 0);
	c.mode = 1;
	/* a torch request is never a strobe */
	n2t_init(&s);
	struct n2t_req r5 = { 5, 1000, 0, 1 };
	n2t_take(&s, &r5, t);
	CHECK(!s.strobe && n2t_decide_kind(&s, &c, t, &sel) == 1 && sel == c.maxsel);
}

static void
test_regs(void)
{
	uint64_t on = 0;
	memset(reg, 0, sizeof(reg));
	reg[N2T_REG_FLED_EN] = 0xF0 | N2T_EN_ON;            /* torch on (preflash), other bits set */
	reg[N2T_REG_FLED_STRB_CTRL] = 0x80 | 0x26;
	reg[N2T_REG_FLED1_STRB2] = 0x10; reg[N2T_REG_FLED2_STRB2] = 0x91;
	writes = 0; write_fail_at = 0;
	CHECK(n2t_hw_strobe(N2T_STROBESEL_DEFAULT, 150, &on) && on != 0);
	CHECK(reg[N2T_REG_FLED_STRB_CTRL] == (0x80 | n2t_strobe_to_sel(150)));
	CHECK(reg[N2T_REG_FLED1_STRB2] == (N2T_UTRAL_ISTRB | N2T_STROBESEL_DEFAULT));
	CHECK(reg[N2T_REG_FLED2_STRB2] == (N2T_UTRAL_ISTRB | N2T_STROBESEL_DEFAULT));
	CHECK(reg[N2T_REG_FLED_EN] == (0xF0 | N2T_EN_CS1 | N2T_EN_CS2 | N2T_EN_STROBE));
	CHECK(writes == 5);
	/* order: timeout first (the hardware ends it whatever happens next), currents, TORCH/STROBE off, then STROBE on */
	CHECK(wlog_r[0] == N2T_REG_FLED_STRB_CTRL && wlog_r[1] == N2T_REG_FLED1_STRB2 && wlog_r[2] == N2T_REG_FLED2_STRB2);
	CHECK(wlog_r[3] == N2T_REG_FLED_EN && (wlog_v[3] & (N2T_EN_TORCH | N2T_EN_STROBE)) == 0);
	CHECK(wlog_r[4] == N2T_REG_FLED_EN && (wlog_v[4] & N2T_EN_STROBE) && !(wlog_v[4] & N2T_EN_TORCH));
	CHECK(n2t_hw_off() && reg[N2T_REG_FLED_EN] == 0xF0);
	/* from dark: no extra FLED_EN write */
	writes = 0;
	CHECK(n2t_hw_strobe(0xFF, 250, &on) && writes == 4 && (reg[N2T_REG_FLED1_STRB2] & N2T_ISTRB_MASK) == N2T_STRB_HW_MAX);
	CHECK(n2t_hw_off());
	/* a failing write at each step: false, and never STROBE on after a failed earlier write */
	for (unsigned k = 1; k <= 5; k++) {
		memset(reg, 0, sizeof(reg));
		reg[N2T_REG_FLED_EN] = N2T_EN_ON;
		writes = 0; write_fail_at = k;
		bool ok = n2t_hw_strobe(10, 100, &on);
		CHECK(!ok);
		if (k < 5) { CHECK(!(reg[N2T_REG_FLED_EN] & N2T_EN_STROBE)); }
	}
	write_fail_at = 0;
}

/* the owner loop (10 ms ticks) with random requests: a strobe never lasts beyond its ms + one tick, strobes keep the 1/5 duty,
 * never a strobe when warm / hot / switched off */
static void
test_sim(void)
{
	srand(5);
	for (int run = 0; run < 200; run++) {
		struct n2t_cfg c; n2t_cfg_default(&c);
		struct n2t_state s; n2t_init(&s);
		uint32_t seq = 0;
		uint64_t strobe_on = 0, last_end = 0, last_ms = 0, req_ms = 0;
		bool lit = false;
		for (uint64_t t = 1000000; t < 1000000 + 60000000ull; t += 10000) {
			if (rand() % 40 == 0) {
				struct n2t_req r = { ++seq, (uint32_t)(rand() % 1001), (uint32_t)(rand() % 3 ? 1 + rand() % N2T_STROBE_MS_MAX : 0),
				    (uint32_t)(rand() % 2) };
				n2t_take(&s, &r, t);
			}
			if (rand() % 500 == 0) { s.therm = (uint32_t)(rand() % 3); }
			if (rand() % 700 == 0) { c.strobe = (uint32_t)(rand() % 2); }
			uint32_t sel = 0;
			uint32_t k = n2t_decide_kind(&s, &c, t, &sel);
			if (k == 2) {
				CHECK(c.strobe && s.therm == 0 && sel <= c.strobesel && sel >= 1);
				if (lit && s.req_us != strobe_on) { lit = false; last_end = t; last_ms = (t - strobe_on) / 1000u; if (last_ms > req_ms) { last_ms = req_ms; } }   /* replaced (the duty counts the requested length: the owner tick may light it a little longer) */
				if (!lit) {
					if (last_end != 0) { uint64_t e = strobe_on + last_ms * 1000u; CHECK(t - e >= last_ms * 1000u * N2T_FLASH_DUTY_DIV); }   /* from the end of its requested (or cut) length */
					strobe_on = s.req_us; lit = true; req_ms = s.ms;
				}
				CHECK(t - strobe_on < (uint64_t)s.ms * 1000u);
			} else if (lit) {
				/* dark: ended by itself (the owner tick after its ms: the cool-down counts from the requested length) or replaced */
				lit = false; last_end = t; last_ms = req_ms; if ((t - strobe_on) / 1000u < req_ms) { last_ms = (t - strobe_on) / 1000u; }
			}
		}
	}
}

int
main(void)
{
	test_parse();
	test_sel();
	test_decide();
	test_regs();
	test_sim();
	CHECK(sizeof(struct n2t_status) == 64);
	if (fails) { printf("STROBE TESTS: %d FAILED\n", fails); return 1; }
	printf("STROBE TESTS OK\n");
	return 0;
}
