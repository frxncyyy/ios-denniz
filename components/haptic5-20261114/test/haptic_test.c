/* haptic_test.c -- host tests of b/bsd/dev/nord2_haptic.h (0610) + nord2_haptic_ram.h (0611) against a simulated AW8697. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "nord2_haptic.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

/* ---- simulated chip ---- */
struct chip {
	uint8_t r[256];
	int nack_after;           /* >= 0: transactions until a NACK */
	int brake_reads;          /* GLB busy reads left after GO=0 */
	int stuck;                /* never leaves busy */
	unsigned writes, boost_seen, bad_reg, drv_over;
	bool playing;
	/* 0611: SRAM */
	uint8_t ram[8192];
	unsigned ramaddr, ram_bad, ram_busy_reads, ram_plays, gain_over;
	uint8_t last_wave, last_gain;
	uint64_t on_ticks;
};
static void chip_reset(struct chip *c)
{
	memset(c->r, 0, sizeof(c->r));
	c->r[N2H_REG_ID] = N2H_CHIPID;
	c->r[N2H_REG_SYSCTRL] = 0x01 | 0x40;      /* standby, WAVDAT 1X bits as some default */
	c->r[N2H_REG_SYSINTM] = 0xff;
	c->r[N2H_REG_DETCTRL] = 0x00;
	c->r[N2H_REG_PWMDBG] = 0x01;
	c->r[N2H_REG_DATCTRL] = 0x03;
	c->r[N2H_REG_BEMF_NUM] = 0x50;
	c->r[N2H_REG_RLDET] = 0x23;               /* 35 x 298 = 10.43 ohm */
	c->r[N2H_REG_VBATDET] = 0xa8;             /* 168 x 6100 / 256 = 4003 mV */
	c->r[N2H_REG_TRIM_LRA] = 0x0d;
	c->playing = false;
	c->r[N2H_REG_DATDBG] = 0x80;
	c->ram_busy_reads = 0;
}
static bool tx(struct chip *c)
{
	if (c->nack_after == 0) { return false; }
	if (c->nack_after > 0) { c->nack_after--; }
	return true;
}
static bool sim_rd(void *ctx, uint8_t reg, uint8_t *v)
{
	struct chip *c = ctx;
	if (!tx(c)) { return false; }
	if (reg == N2H_REG_GLB_STATE) {
		if (c->playing) { c->r[reg] = 0x08; }
		else if (c->ram_busy_reads > 0) { c->ram_busy_reads--; c->r[reg] = 0x06; }
		else if (c->brake_reads > 0 || c->stuck) { c->brake_reads--; c->r[reg] = 0x09; }
		else { c->r[reg] = 0; }
	}
	if (reg == N2H_REG_DETCTRL) { c->r[reg] &= (uint8_t)~(N2H_DETCTRL_DIAG_GO | N2H_DETCTRL_VBAT_GO); *v = c->r[reg]; return true; }
	*v = c->r[reg];
	return true;
}
static bool sim_wr(void *ctx, uint8_t reg, uint8_t v)
{
	struct chip *c = ctx;
	if (!tx(c)) { return false; }
	c->writes++;
	if (!n2h_reg_writable(reg)) { c->bad_reg++; }
	if (reg == N2H_REG_BSTDBG4) { c->bad_reg++; }
	if (reg == N2H_REG_SYSCTRL && (v & N2H_SYSCTRL_BOOST)) { c->boost_seen++; }
	if (reg == N2H_REG_DRV_LVL && v > N2H_DRV_HW_MAX) { c->drv_over++; }
	if (reg == N2H_REG_DRV_LVL_OV && v > N2H_DRV_OV_HW_MAX) { c->drv_over++; }
	if (reg == N2H_REG_ID && v == N2H_SOFTRESET) { chip_reset(c); return true; }
	if (reg == N2H_REG_DATDBG && v > N2H_GAIN_HW_MAX) { c->gain_over++; }
	if (reg == N2H_REG_RAMDATA) { c->bad_reg++; }
	c->r[reg] = v;
	if (reg == N2H_REG_RAMADDRH || reg == N2H_REG_RAMADDRL) { c->ramaddr = (unsigned)c->r[N2H_REG_RAMADDRH] << 8 | c->r[N2H_REG_RAMADDRL]; }
	if (reg == N2H_REG_GO) {
		bool cont = (c->r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_PLAY_MASK) == N2H_SYSCTRL_PLAY_CONT && !(c->r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_STANDBY);
		if ((v & 1) && cont) { c->playing = true; }
		bool ram = (c->r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_PLAY_MASK) == N2H_SYSCTRL_PLAY_RAM && !(c->r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_STANDBY);
		if ((v & 1) && ram) { c->ram_plays++; c->ram_busy_reads = 2; c->last_wave = c->r[N2H_REG_WAVSEQ1]; c->last_gain = c->r[N2H_REG_DATDBG]; }
		if (!(v & 1) && c->playing) { c->playing = false; c->brake_reads = 3; }
	}
	return true;
}
static void sim_sleep(void *ctx, unsigned ms) { (void)ctx; (void)ms; }
static bool sim_wrn(void *ctx, uint8_t reg, const uint8_t *v, unsigned n)
{
	struct chip *c = ctx;
	if (!tx(c)) { return false; }
	if (reg != N2H_REG_RAMDATA || !(c->r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_RAMINIT) || n > N2H_BURST_MAX) { c->ram_bad++; return true; }
	for (unsigned i = 0; i < n; i++) { if (c->ramaddr >= sizeof(c->ram)) { c->ram_bad++; return true; } c->ram[c->ramaddr++] = v[i]; }
	return true;
}
static bool sim_rdn(void *ctx, uint8_t reg, uint8_t *v, unsigned n)
{
	struct chip *c = ctx;
	if (!tx(c)) { return false; }
	if (reg != N2H_REG_RAMDATA || !(c->r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_RAMINIT) || n > N2H_BURST_MAX) { c->ram_bad++; return true; }
	for (unsigned i = 0; i < n; i++) { v[i] = c->ramaddr < sizeof(c->ram) ? c->ram[c->ramaddr++] : 0xee; }
	return true;
}

static void test_sequences(void)
{
	struct chip c = { .nack_after = -1 };
	chip_reset(&c);
	struct n2h_bus b = { &c, sim_rd, sim_wr, sim_sleep, sim_wrn, sim_rdn };
	CHECK(N2H_RUN(&b, n2h_seq_init, 0, 0) == 0);
	CHECK((c.r[N2H_REG_SYSCTRL] & 0x03) == N2H_SYSCTRL_STANDBY);
	CHECK((c.r[N2H_REG_SYSCTRL] & 0xC0) == 0x40);                 /* bits the driver does not own kept */
	CHECK((c.r[N2H_REG_PWMDBG] & 0x60) == 0x40 && (c.r[N2H_REG_PWMDBG] & 0x01));
	CHECK(c.r[N2H_REG_BSTDBG1] == 0x30 && c.r[N2H_REG_BSTDBG2] == 0xeb && c.r[N2H_REG_BSTDBG3] == 0xd4);
	CHECK(c.r[N2H_REG_TSET] == 0x12 && c.r[N2H_REG_R_SPARE] == 0x68);
	CHECK((c.r[N2H_REG_ANADBG] & 0x0c) == 0x0c && (c.r[N2H_REG_BSTCFG] & 7) == 5);
	CHECK((c.r[N2H_REG_DETCTRL] & N2H_DETCTRL_NOPROTECT) && (c.r[N2H_REG_TRG_CFG2] & 7) == 0);
	CHECK((c.r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_RAMINIT) == 0 && (c.r[N2H_REG_ADCTEST] & 0x40));
	CHECK(N2H_RUN(&b, n2h_seq_cont, 0, 0) == 0);
	CHECK(c.r[N2H_REG_CONT_CTRL] == 0x89);
	unsigned fpre = 1000000000u / (1700u * 260u);
	CHECK(fpre == 2262 && c.r[N2H_REG_F_PRE_H] == (fpre >> 8) && c.r[N2H_REG_F_PRE_L] == (fpre & 0xff));
	CHECK(n2h_cont_f0((uint8_t)(fpre >> 8), (uint8_t)fpre) == 1700);
	CHECK(c.r[N2H_REG_TD_H] == 0x00 && c.r[N2H_REG_TD_L] == 0x9a && c.r[N2H_REG_ZC_THRSH_H] == 0x0f && c.r[N2H_REG_ZC_THRSH_L] == 0xf1);
	CHECK(c.r[N2H_REG_BEMF_NUM] == 0x53 && c.r[N2H_REG_TIME_NZC] == 0x23);
	CHECK((c.r[N2H_REG_DATCTRL] & 0xe0) == 0xe0 && (c.r[N2H_REG_DATCTRL] & 0x03) == 0x03);
	CHECK(N2H_RUN(&b, n2h_seq_start, 60, 125) == 0);
	CHECK(c.playing && c.r[N2H_REG_DRV_LVL] == 60 && c.r[N2H_REG_DRV_LVL_OV] == 125);
	CHECK((c.r[N2H_REG_SYSCTRL] & 0x0f) == N2H_SYSCTRL_PLAY_CONT && (c.r[N2H_REG_SYSINTM] & N2H_SYSINTM_UVLO) == 0);
	/* a drive above the ceiling is refused before the bus */
	unsigned w0 = c.writes;
	CHECK(N2H_RUN(&b, n2h_seq_start, 61, 125) == 1 && c.writes == w0);
	CHECK(N2H_RUN(&b, n2h_seq_start, 60, 126) == 2);
	CHECK(!n2h_write(&b, N2H_REG_SYSCTRL, 0x0a) && !n2h_write(&b, N2H_REG_BSTDBG4, 0x10) && !n2h_write(&b, N2H_REG_ID, 0x00));
	CHECK(!n2h_write(&b, 0x42, 0x00) && !n2h_write(&b, N2H_REG_TRIM_LRA, 0x00));
	CHECK(!n2h_write(&b, N2H_REG_DATDBG, N2H_GAIN_HW_MAX + 1) && !n2h_write(&b, N2H_REG_WAVSEQ1, N2H_WSEQ_MAX + 1));
	CHECK(!n2h_write(&b, N2H_REG_WAVSEQ2, 1) && !n2h_write(&b, N2H_REG_MAIN_LOOP, 1) && !n2h_write(&b, N2H_REG_WAVLOOP1, 0x10));
	CHECK(N2H_RUN(&b, n2h_seq_stop, 0, 0) == 0 && !c.playing);
	uint8_t g = 0; int n = 0;
	do { CHECK(sim_rd(&c, N2H_REG_GLB_STATE, &g)); n++; } while (g & 0x0f);
	CHECK(n == 4);
	CHECK(N2H_RUN(&b, n2h_seq_standby, 0, 0) == 0 && (c.r[N2H_REG_SYSCTRL] & 1) && (c.r[N2H_REG_SYSINTM] & N2H_SYSINTM_UVLO));
	uint32_t mohm = 0, mv = 0;
	CHECK(n2h_measure_lra(&b, &mohm) && mohm == 35 * 298);
	CHECK((c.r[N2H_REG_SYSCTRL] & (N2H_SYSCTRL_RAMINIT | N2H_SYSCTRL_BOOST)) == 0 && (c.r[N2H_REG_ANACTRL] & N2H_ANACTRL_HD_PD));
	CHECK((c.r[N2H_REG_D2SCFG] & 0xe0) == N2H_D2SCFG_CLK_6M);
	CHECK(n2h_measure_vbat(&b, &mv) && mv == 168u * 6100u / 256u);
	CHECK(c.boost_seen == 0 && c.bad_reg == 0 && c.drv_over == 0);
	/* a NACK mid-sequence is reported with its step */
	c.nack_after = 5;
	unsigned bad = N2H_RUN(&b, n2h_seq_init, 0, 0);
	CHECK(bad > 0 && bad <= 5);
	printf("sequences: init %zu ops, cont %zu, start %zu; register end states, ceilings, allow-list, LRA/VBAT, NACK OK\n",
	    sizeof(n2h_seq_init) / sizeof(n2h_seq_init[0]), sizeof(n2h_seq_cont) / sizeof(n2h_seq_cont[0]),
	    sizeof(n2h_seq_start) / sizeof(n2h_seq_start[0]));
}

static void test_pack(void)
{
	for (uint32_t fx = 0; fx < 256; fx += 17) for (uint32_t lv = 0; lv <= 1000; lv += 125) for (uint32_t ms = 0; ms < 0x4000; ms += 999) {
		struct n2h_req r = n2h_req_unpack(n2h_req_pack(0xdeadbeef, fx, lv, ms));
		CHECK(r.seq == 0xdeadbeef && r.effect == fx && r.level == lv && r.ms == ms);
	}
	struct n2h_cfg c = { 1, 60, 1000 };
	CHECK(n2h_drv_for(1000, &c, 0) == 60 && n2h_drv_for(0, &c, 0) == N2H_DRV_MIN && n2h_drv_for(5000, &c, 0) == 60);
	CHECK(n2h_drv_for(1000, &c, 1) == 30 && n2h_drv_for(0, &c, 1) == N2H_DRV_MIN);
	c.drv_max = 500; n2h_cfg_fix(&c); CHECK(c.drv_max == 60);
	c.drv_max = 2; n2h_cfg_fix(&c); CHECK(c.drv_max == N2H_DRV_MIN);
	c.max_ms = 99999; n2h_cfg_fix(&c); CHECK(c.max_ms == N2H_MAX_MS_HW);
	c.mode = 7; n2h_cfg_fix(&c); CHECK(c.mode == 1);
	for (unsigned d = 0; d <= 60; d++) { CHECK(n2h_drv_ov_for((uint8_t)d) <= 125); }
	CHECK(n2h_drv_ov_for(60) == 125);
	c = (struct n2h_cfg){ 1, 60, 1000 };
	CHECK(n2h_drv_for_peak(32767, &c, 0) == 60 && n2h_drv_for_peak(N2H_PEAK_FULL, &c, 0) == 60);
	CHECK(n2h_drv_for_peak(300, &c, 0) >= N2H_DRV_MIN + (60 - N2H_DRV_MIN) * 625 / 1000);
	int16_t s[4] = { 3, -32768, 100, 32767 };
	CHECK(n2h_peak_of(s, 4) == 32768);
	printf("pack/drive maps OK\n");
}

/* stream scenarios: ticks of 10 ms, HAL buffers of ~10.7 ms */
struct sim { struct n2h_state s; struct n2h_stream t; struct n2h_env e; struct n2h_cfg c; uint64_t now; bool on; uint64_t on_us, run_us, max_run_us; };
static void sim_init(struct sim *m)
{
	memset(m, 0, sizeof(*m));
	m->c = (struct n2h_cfg){ 1, 60, 1000 };
	m->now = 1000000;
	n2h_init(&m->s, m->now);
}
static void tick(struct sim *m)
{
	uint8_t drv = 0; uint64_t until = 0;
	bool w = n2h_decide_all(&m->s, &m->t, &m->e, &m->c, m->now, m->on, &drv, &until);
	if (w) {
		CHECK(drv <= m->c.drv_max && drv >= N2H_DRV_MIN && until > m->now);
		CHECK(until <= m->now + (uint64_t)m->c.max_ms * 1000);
	}
	if (w && m->on) { m->on_us += 10000; m->run_us += 10000; if (m->run_us > m->max_run_us) { m->max_run_us = m->run_us; } }
	if (!w) { m->run_us = 0; }
	m->on = w;
	m->now += 10000;
}
static void feed(struct sim *m, uint32_t peak) { n2h_env_feed(&m->e, peak, 512, m->now); }

static void test_stream(void)
{
	struct sim m;
	/* nothing written: never on */
	sim_init(&m);
	for (int i = 0; i < 100; i++) { tick(&m); CHECK(!m.on); }
	/* a tap: one loud buffer then silence -> on for >= 12 ms, off within 3 ticks */
	sim_init(&m);
	feed(&m, 20000); tick(&m); CHECK(m.on);
	feed(&m, 0); tick(&m);
	int on_ticks = 1; while (m.on && on_ticks < 10) { tick(&m); on_ticks++; }
	CHECK(!m.on && on_ticks <= 3 && m.t.taps == 1);
	/* a tap with no following silent write (HAL stops writing): off after N2H_ACTIVE_US */
	sim_init(&m);
	feed(&m, 20000);
	int n = 0; tick(&m); while (m.on && n < 20) { tick(&m); n++; }
	CHECK(n <= 3);
	/* a long vibration: continuous loud writes for 3 s -> capped at max_ms then rest as long */
	sim_init(&m);
	for (int i = 0; i < 300; i++) { feed(&m, 17000); tick(&m); }
	CHECK(m.max_run_us <= 1000000 && m.t.capped >= 1);
	CHECK(m.on_us >= 1400000 && m.on_us <= 2100000);
	/* drive follows the peak: soft waveform below full drive */
	sim_init(&m);
	{
		uint8_t drv = 0; uint64_t until = 0;
		feed(&m, 1000);
		CHECK(n2h_decide_all(&m.s, &m.t, &m.e, &m.c, m.now, false, &drv, &until) && drv < 60 && drv >= 40);
	}
	/* kern.n2hap=0, thermal hot, latched: never on */
	sim_init(&m); m.c.mode = 0;
	for (int i = 0; i < 50; i++) { feed(&m, 30000); tick(&m); CHECK(!m.on); }
	sim_init(&m); CHECK(n2h_therm_update(&m.s, true, 4650)); CHECK(m.s.therm == 2);
	for (int i = 0; i < 50; i++) { feed(&m, 30000); tick(&m); CHECK(!m.on); }
	CHECK(!n2h_therm_update(&m.s, true, 4450) && m.s.therm == 2);      /* hysteresis */
	CHECK(n2h_therm_update(&m.s, true, 4350) && m.s.therm == 1);
	CHECK(n2h_therm_update(&m.s, true, 4000) && m.s.therm == 0);
	CHECK(!n2h_therm_update(&m.s, false, 9000) && m.s.therm == 0);
	sim_init(&m); m.s.latched = true;
	for (int i = 0; i < 50; i++) { feed(&m, 30000); tick(&m); CHECK(!m.on); }
	/* warm: half drive */
	sim_init(&m); n2h_therm_update(&m.s, true, 4300);
	{ uint8_t drv = 0; uint64_t u = 0; feed(&m, 30000); CHECK(n2h_decide_all(&m.s, &m.t, &m.e, &m.c, m.now, false, &drv, &u) && drv == 30); }
	/* budget: 60 s of continuous loud writes with max_ms 3000 -> long-run on-time near 50 % plus the bucket */
	sim_init(&m); m.c.max_ms = 3000;
	for (int i = 0; i < 6000; i++) { feed(&m, 30000); tick(&m); }
	CHECK(m.on_us <= 60000000ull / 2 + (uint64_t)N2H_BUDGET_MS * 1000 + 1000000);
	CHECK(m.s.throttled > 0 || m.t.capped > 0);
	/* close: quiet now -> off within a tick */
	sim_init(&m);
	for (int i = 0; i < 20; i++) { feed(&m, 30000); tick(&m); }
	CHECK(m.on);
	m.e.quiet_us = m.now; tick(&m); CHECK(!m.on);
	/* a pattern (boot pulse 60 ms) runs ~60 ms, then the stream decides again */
	sim_init(&m);
	{ struct n2h_req r = { 1, N2H_FX_RAW, 800, 60 }; n2h_take(&m.s, &r, &m.c, m.now); }
	int pt = 0; for (int i = 0; i < 20; i++) { tick(&m); if (m.on) { pt++; } }
	CHECK(pt >= 5 && pt <= 7 && !m.on);
	/* notification error: 3 segments with gaps */
	sim_init(&m);
	{ struct n2h_req r = { 2, N2H_FX_NOTIFY_ERROR, 0, 0 }; n2h_take(&m.s, &r, &m.c, m.now); }
	int edges = 0; bool prev = false;
	for (int i = 0; i < 60; i++) { tick(&m); if (m.on && !prev) { edges++; } prev = m.on; }
	CHECK(edges == 3);
	/* same seq is ignored, bad effect counted, RAW clamp */
	{ struct n2h_req r = { 2, N2H_FX_NOTIFY_ERROR, 0, 0 }; uint64_t q = m.s.reqs; n2h_take(&m.s, &r, &m.c, m.now); CHECK(m.s.reqs == q); }
	{ struct n2h_req r = { 3, 200, 0, 0 }; n2h_take(&m.s, &r, &m.c, m.now); CHECK(m.s.bad_effect == 1); }
	{ struct n2h_req r = { 4, N2H_FX_RAW, 1000, 9999 }; n2h_take(&m.s, &r, &m.c, m.now); CHECK(m.s.segs[0].on_ms == 1000 && m.s.clamped == 1); }
	printf("stream scenarios OK (tap, lost writer, cap+rest, peak, off switches, thermal, budget, close, patterns)\n");
}

static void test_random(void)
{
	srand(12345);
	for (int run = 0; run < 3; run++) {
		struct sim m; sim_init(&m);
		m.c.max_ms = 50 + (unsigned)(rand() % 2950); m.c.drv_max = 18 + (unsigned)(rand() % 43);
		uint64_t win_on = 0; uint64_t win_start = m.now;
		for (int i = 0; i < 200000; i++) {
			int k = rand() % 100;
			if (k < 40) { feed(&m, (uint32_t)(rand() % 33000)); }
			else if (k < 45) { feed(&m, 0); }
			else if (k < 46) { struct n2h_req r = { (uint32_t)i + 10, (uint32_t)(rand() % N2H_FX_COUNT), (uint32_t)(rand() % 1001), (uint32_t)(rand() % 4000) }; n2h_take(&m.s, &r, &m.c, m.now); }
			else if (k < 47) { n2h_therm_update(&m.s, true, 3500 + rand() % 1500); }
			else if (k == 47) { m.c.mode = (uint32_t)(rand() % 2); }
			bool was = m.on; uint64_t before = m.on_us;
			tick(&m);
			if ((m.s.therm >= 2 || !m.c.mode) && m.s.nseg == 0) { CHECK(!m.on || !was || m.on_us == before); }
			CHECK(m.max_run_us <= (uint64_t)(m.c.max_ms > N2H_MAX_MS_HW ? N2H_MAX_MS_HW : m.c.max_ms) * 1000 + 3000000);
			win_on = m.on_us;
		}
		uint64_t span = m.now - win_start;
		CHECK(win_on <= span / 2 + (uint64_t)N2H_BUDGET_MS * 1000 + 2000000);
		printf("random run %d: %llu s simulated, motor on %llu s, taps %llu runs %llu capped %llu throttled %llu\n", run,
		    (unsigned long long)(span / 1000000), (unsigned long long)(win_on / 1000000), (unsigned long long)m.t.taps,
		    (unsigned long long)m.t.runs, (unsigned long long)m.t.capped, (unsigned long long)m.s.throttled);
	}
}

/* ---- 0611: RAM clicks ---- */
#ifdef N2H_VENDOR_RAM
static void test_ram_image(void)
{
	static uint8_t img[N2H_RAM_IMG_MAX];
	struct n2h_ram_layout lay;
	unsigned len = n2h_ram_build(img, sizeof(img), 1700, N2H_PROFILE, &lay);
	CHECK(len == N2H_VENDOR_LEN && memcmp(img, n2h_vendor_img, len) == 0 && lay.vendor);
	for (unsigned k = 1; k <= N2H_W_COUNT; k++) {
		const uint8_t *e = img + 1 + 4 * (n2h_vendor_idx[k] - 1);
		CHECK(lay.idx[k] == n2h_vendor_idx[k] && lay.start[k] == (e[0] << 8 | e[1]) && lay.end[k] == (e[2] << 8 | e[3]));
		CHECK(lay.ms_x10[k] == (lay.end[k] - lay.start[k] + 1u) * 10000u / 24000u);
	}
	printf("vendor image: %u bytes at %#x, tick %u click %u heavy %u (%u.%u / %u.%u / %u.%u ms) OK\n", len, N2H_VENDOR_BASE,
	    lay.idx[1], lay.idx[2], lay.idx[3], lay.ms_x10[1] / 10, lay.ms_x10[1] % 10, lay.ms_x10[2] / 10, lay.ms_x10[2] % 10,
	    lay.ms_x10[3] / 10, lay.ms_x10[3] % 10);
	CHECK(n2h_ram_build(img, N2H_VENDOR_LEN - 1, 1700, N2H_PROFILE, &lay) == 0);
}
#else
static void test_ram_image(void)
{
	static uint8_t img[N2H_RAM_IMG_MAX];
	struct n2h_ram_layout lay;
	for (unsigned pi = 0; pi < 6; pi++) {
		const struct n2h_profile *p = &n2h_profiles[pi];
		for (uint32_t f0 = N2H_F0_MIN_OK; f0 <= N2H_F0_MAX_OK; f0 += 25) {
			unsigned len = n2h_ram_build(img, sizeof(img), f0, p, &lay);
			CHECK(len > 13 && len == lay.len && len <= N2H_RAM_IMG_MAX && N2H_RAM_BASE + len <= N2H_RAM_END);
			CHECK(img[0] == N2H_RAM_VERSION);
			uint16_t first = (uint16_t)(img[1] << 8 | img[2]);
			CHECK((first - N2H_RAM_BASE - 1) / 4 == N2H_W_COUNT);               /* haptic_hv.c get_ram_num */
			for (unsigned k = 1; k <= N2H_W_COUNT; k++) {
				uint8_t *e = img + 1 + 4 * (k - 1);
				CHECK((e[0] << 8 | e[1]) == lay.start[k] && (e[2] << 8 | e[3]) == lay.end[k]);
				CHECK(lay.end[k] > lay.start[k] && (k == 1 || lay.start[k] == lay.end[k - 1] + 1));
				const int8_t *w = (const int8_t *)img + (lay.start[k] - N2H_RAM_BASE);
				unsigned n = lay.end[k] - lay.start[k] + 1u;
				CHECK(w[n - 1] == 0 && w[n - 2] == 0);
				/* expected length: (drive + brake) half-cycles at f0, +2 rest samples */
				double halves = p->w[k].drive_halves + p->w[k].nbrk, hl = 24000.0 * 10.0 / (2.0 * f0);
				CHECK(n >= (unsigned)(halves * hl) && n <= (unsigned)(halves * hl) + 4);
				/* polarity per half-cycle: drive follows the sine (+,-,+..), the brake is its inverse; peak near full scale */
				for (unsigned h = 0; h < halves; h++) {
					unsigned a = (unsigned)(h * hl + hl * 0.25), z = (unsigned)(h * hl + hl * 0.75);
					int maxabs = 0; long sum = 0;
					for (unsigned i = a; i < z; i++) { sum += w[i]; maxabs = abs(w[i]) > maxabs ? abs(w[i]) : maxabs; }
					int sine_sign = (h & 1) ? -1 : 1;
					int want = h < p->w[k].drive_halves ? sine_sign : -sine_sign;
					CHECK((sum > 0 ? 1 : -1) == want);
					unsigned pct = h < p->w[k].drive_halves ? 100 : p->w[k].brk_pct[h - p->w[k].drive_halves];
					CHECK(maxabs <= 127 && maxabs >= (int)(127 * pct / 100) - 3);
				}
				CHECK(lay.ms_x10[k] == n * 10000u / 24000u);
			}
			if (f0 == 1700 && pi == 0) {
				printf("ram image (default, 170 Hz): %u bytes; tick %u.%u ms, click %u.%u ms, heavy %u.%u ms\n", len,
				    lay.ms_x10[1] / 10, lay.ms_x10[1] % 10, lay.ms_x10[2] / 10, lay.ms_x10[2] % 10, lay.ms_x10[3] / 10, lay.ms_x10[3] % 10);
				FILE *f = fopen(getenv("N2H_DUMP") ? getenv("N2H_DUMP") : "/dev/null", "wb");
				if (f) { fwrite(img, 1, len, f); fclose(f); }
			}
		}
	}
	CHECK(n2h_ram_build(img, sizeof(img), 1500, &n2h_profiles[0], &lay) == 0);
	CHECK(n2h_ram_build(img, 100, 1700, &n2h_profiles[0], &lay) == 0);
	CHECK(n2h_f0_for_ram(0) == 1700 && n2h_f0_for_ram(1695) == 1695 && n2h_f0_for_ram(2350) == 1700);
	printf("ram image: 3 profiles x f0 160..180 Hz: table, lengths, polarity (drive / antiphase brake), amplitudes OK\n");
}
#endif

static void test_ram_chip(void)
{
	static uint8_t img[N2H_RAM_IMG_MAX];
	struct n2h_ram_layout lay;
	struct chip c = { .nack_after = -1 };
	chip_reset(&c);
	struct n2h_bus b = { &c, sim_rd, sim_wr, sim_sleep, sim_wrn, sim_rdn };
	CHECK(N2H_RUN(&b, n2h_seq_init, 0, 0) == 0);
	unsigned len = n2h_ram_build(img, sizeof(img), 1700, N2H_PROFILE, &lay);
	CHECK(len);
	/* RAM writes without RAMINIT are refused by the address check only when outside the area; the chip ignores them */
	CHECK(!n2h_ram_write(&b, 0x0100, img, 4));                   /* below the base */
	CHECK(!n2h_ram_write(&b, 0x1ff0, img, 64));                  /* past the end */
	CHECK(N2H_RUN(&b, n2h_seq_ram_open, 0, 0) == 0);
	CHECK((c.r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_RAMINIT) && (c.r[N2H_REG_BASE_ADDRH] << 8 | c.r[N2H_REG_BASE_ADDRL]) == N2H_RAM_BASE);
	CHECK((c.r[N2H_REG_FIFO_AEH] << 8 | c.r[N2H_REG_FIFO_AEL]) == N2H_RAM_BASE / 2);
	CHECK((c.r[N2H_REG_FIFO_AFH] << 8 | c.r[N2H_REG_FIFO_AFL]) == N2H_RAM_BASE - N2H_RAM_BASE / 4);
	for (unsigned off = 0; off < len; off += N2H_RAM_CHUNK) {
		unsigned n = len - off < N2H_RAM_CHUNK ? len - off : N2H_RAM_CHUNK;
		CHECK(n2h_ram_write(&b, (uint16_t)(N2H_RAM_BASE + off), img + off, n));
	}
	CHECK(memcmp(c.ram + N2H_RAM_BASE, img, len) == 0 && c.ram_bad == 0);
	for (unsigned off = 0; off < len; off += N2H_RAM_CHUNK) {
		unsigned n = len - off < N2H_RAM_CHUNK ? len - off : N2H_RAM_CHUNK, bad = 0; uint8_t got = 0;
		CHECK(n2h_ram_verify(&b, (uint16_t)(N2H_RAM_BASE + off), img + off, n, &bad, &got) && bad == n);
	}
	/* a corrupted byte is found with its offset */
	c.ram[N2H_RAM_BASE + 300] ^= 0x5a;
	{ unsigned bad = 0; uint8_t got = 0; CHECK(n2h_ram_verify(&b, N2H_RAM_BASE + 256, img + 256, 128, &bad, &got) && bad == 44 && got == (img[300] ^ 0x5a)); }
	c.ram[N2H_RAM_BASE + 300] ^= 0x5a;
	CHECK(N2H_RUN(&b, n2h_seq_ram_close, 0, 0) == 0 && !(c.r[N2H_REG_SYSCTRL] & N2H_SYSCTRL_RAMINIT));
	CHECK(N2H_RUN(&b, n2h_seq_standby, 0, 0) == 0);
	/* play: RAM mode, boost bypassed, active, our waveform and gain, once */
	CHECK(N2H_RUN(&b, n2h_seq_ram_play, 0x80, lay.idx[N2H_W_CLICK]) == 0);
	CHECK(c.ram_plays == 1 && c.last_wave == lay.idx[N2H_W_CLICK] && c.last_gain == 0x80);
	CHECK((c.r[N2H_REG_SYSCTRL] & 0x2f) == 0 && (c.r[N2H_REG_SYSCTRL] & 0xc0) == 0x40);   /* RAM, bypass, active, RAMINIT off */
	CHECK(c.r[N2H_REG_WAVSEQ2] == 0 && c.r[N2H_REG_WAVLOOP1] == 0 && c.r[N2H_REG_MAIN_LOOP] == 0 && (c.r[N2H_REG_GO] & 1));
	uint8_t g = 0; int n = 0;
	do { CHECK(sim_rd(&c, N2H_REG_GLB_STATE, &g)); n++; } while ((g & 0x0f) && n < 10);
	CHECK(n == 3);
	/* refused before the bus: gain above the ceiling, waveform 0 or > 3 */
	unsigned w0 = c.writes;
	CHECK(N2H_RUN(&b, n2h_seq_ram_play, N2H_GAIN_HW_MAX + 1, 1) == 1 && c.writes == w0);
	CHECK(N2H_RUN(&b, n2h_seq_ram_play, 0x80, 0) == 2 && N2H_RUN(&b, n2h_seq_ram_play, 0x80, N2H_WSEQ_MAX + 1) == 2);
	CHECK(c.boost_seen == 0 && c.bad_reg == 0 && c.drv_over == 0 && c.gain_over == 0 && c.ram_bad == 0);
	/* a NACK during the upload is reported */
	CHECK(N2H_RUN(&b, n2h_seq_ram_open, 0, 0) == 0);
	c.nack_after = 3;
	CHECK(!n2h_ram_write(&b, N2H_RAM_BASE, img, 128));
	c.nack_after = -1;
	/* the gain map: always within the ceiling, monotonic in level, halved when warm */
	for (unsigned pi = 0; pi < 6; pi++) for (uint32_t mv = 2000; mv <= 5000; mv += 100) {
		uint8_t prev = 0;
		for (uint32_t lv = 0; lv <= 1200; lv += 50) {
			uint8_t gg = n2h_gain_for(lv, mv, 0, &n2h_profiles[pi]);
			CHECK(gg <= N2H_GAIN_HW_MAX && gg >= 8 && gg >= prev);
			CHECK(n2h_gain_for(lv, mv, 1, &n2h_profiles[pi]) <= gg / 2 + 8);
			prev = gg;
		}
	}
	CHECK(n2h_gain_for(1000, 4400, 0, &n2h_profiles[0]) == 0x80);
	printf("ram chip: open/upload/verify (+ corrupted byte found), close, play registers, ceilings, NACK, gain map OK\n");
}

static void test_ram_stream(void)
{
	struct sim m;
	uint8_t wave = 0, gain = 0; uint32_t peak = 0;
	/* one onset -> one click; the same onset never twice; waveform by peak */
	sim_init(&m); m.t.ram_ok = true;
	feed(&m, 10466);
	CHECK(n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now + 5000, 4000, N2H_PROFILE, &wave, &gain, &peak));
	CHECK(wave == N2H_W_CLICK && peak == 10466 && gain > N2H_PROFILE->gain_max * 9 / 10 && gain <= N2H_GAIN_HW_MAX);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now + 15000, 4000, N2H_PROFILE, &wave, &gain, &peak));
	/* 0614 lookahead: a soft first write waits; a louder second write within 9 ms makes it a click; a soft waveform stays a tick */
	sim_init(&m); m.t.ram_ok = true; feed(&m, 1040);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now + 2000, 4000, N2H_PROFILE, &wave, &gain, &peak) && m.t.lookahead == 1);
	m.now += 5333; feed(&m, 10347);
	CHECK(n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now + 5000, 4000, N2H_PROFILE, &wave, &gain, &peak) && wave == N2H_W_CLICK && peak == 10347);
	sim_init(&m); m.t.ram_ok = true; feed(&m, 599);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak));
	CHECK(n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now + 10000, 4000, N2H_PROFILE, &wave, &gain, &peak) && wave == N2H_W_TICK);
	CHECK(gain >= N2H_PROFILE->gain_max * 85 / 100 * 4400 / 4000 - 1 && gain <= N2H_PROFILE->gain_max * 4400 / 4000);
	sim_init(&m); m.t.ram_ok = true; feed(&m, 30000);
	CHECK(n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak) && wave == N2H_W_HEAVY);
	/* stale onset (motor busy for > 40 ms) is dropped and counted */
	sim_init(&m); m.t.ram_ok = true; feed(&m, 10000);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now + 90000, 4000, N2H_PROFILE, &wave, &gain, &peak) && m.t.stale == 1);
	CHECK(!strcmp(n2h_path_account(&m.t, m.e.onset_us, 20), "skip (motor busy > 80 ms)") && m.t.path_skip == 1 && m.t.dur_hist[0] == 1);
	/* off switches, thermal, latch, budget, not loaded */
	sim_init(&m); m.t.ram_ok = true; m.c.mode = 0; feed(&m, 10000);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak));
	sim_init(&m); m.t.ram_ok = true; m.s.therm = 2; feed(&m, 10000);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak));
	sim_init(&m); m.t.ram_ok = true; m.s.latched = true; feed(&m, 10000);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak));
	sim_init(&m); m.t.ram_ok = true; m.s.budget_us = 0; feed(&m, 10000);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak) && m.s.throttled == 1);
	sim_init(&m); feed(&m, 10000);
	CHECK(!n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak));
	sim_init(&m); m.t.ram_ok = true; m.s.therm = 1; feed(&m, 10466);
	CHECK(n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak) && gain <= 0x80 * 1100 / 2000 + 1);
	/* 0613 tick train: a loud waveform still sounding gets a click every N2H_TRAIN_GAP_MS until N2H_SUSTAIN_MS; a quiet tail none */
	{
		sim_init(&m); m.t.ram_ok = true;
		unsigned n = 0; uint64_t t0 = m.now;
		for (int i = 0; i < 40; i++) {
			feed(&m, 10000);
			if (n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak)) { n++; CHECK(m.now - t0 < 300000); }
			m.now += 10000;
		}
		CHECK(n >= 4 && n <= 5 && m.t.trains == n - 1);
		CHECK(!strcmp(n2h_path_account(&m.t, m.e.onset_us, 400), "RAM train") && m.t.dur_hist[4] == 1);
		sim_init(&m); m.t.ram_ok = true; n = 0;
		feed(&m, 10000);
		CHECK(n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak));
		for (int i = 0; i < 25; i++) { m.now += 10000; feed(&m, 3000); if (n2h_click_take(&m.s, &m.t, &m.e, &m.c, m.now, 4000, N2H_PROFILE, &wave, &gain, &peak)) { n++; } }
		CHECK(n == 0 && m.t.trains == 0);
		CHECK(!strcmp(n2h_path_account(&m.t, m.e.onset_us, 250), "RAM") && m.t.path_ram == 1);
	}
	/* with RAM: a short waveform (40 ms) never starts CONT; a long one gets CONT from N2H_SUSTAIN_MS after its onset */
	sim_init(&m); m.t.ram_ok = true;
	for (int i = 0; i < 4; i++) { feed(&m, 10466); tick(&m); CHECK(!m.on); }
	feed(&m, 0);
	for (int i = 0; i < 20; i++) { tick(&m); CHECK(!m.on); }
	sim_init(&m); m.t.ram_ok = true;
	int first_on = -1;
	for (int i = 0; i < 40; i++) { feed(&m, 10466); tick(&m); if (m.on && first_on < 0) { first_on = i; } }
	CHECK(first_on >= 30 && first_on <= 31 && m.on);
	sim_init(&m); m.t.ram_ok = true;
	for (int i = 0; i < 12; i++) { feed(&m, 10466); tick(&m); CHECK(!m.on); }     /* 120 ms UIKit-length waveform: no CONT */
	feed(&m, 0); tick(&m); CHECK(!m.on);
	/* zero crossings: 170 Hz sine at 48 kHz, 10 writes of 256 frames (53.3 ms = 9.07 periods) -> ~18 crossings */
	{
		int16_t sm[256]; volatile int64_t ls = 0; unsigned z = 0;
		for (int k = 0; k < 10; k++) {
			for (int i = 0; i < 256; i++) { sm[i] = (int16_t)(10000.0 * __builtin_sin(2 * 3.14159265 * 170.0 * (k * 256 + i) / 48000.0 + 0.3)); }
			z += n2h_zc_count(sm, 256, &ls);
		}
		CHECK(z >= 17 && z <= 19 && z * 48000u / (2u * 2560u) >= 159 && z * 48000u / (2u * 2560u) <= 179);
	}
	printf("ram stream: click per onset, waveform by peak, stale/switches/thermal/latch/budget, CONT only for sustained, zc OK\n");
}

int main(void)
{
	test_sequences();
	test_pack();
	test_stream();
	test_random();
	test_ram_image();
	test_ram_chip();
	test_ram_stream();
	printf("HAPTIC_TESTS_OK\n");
	return 0;
}
