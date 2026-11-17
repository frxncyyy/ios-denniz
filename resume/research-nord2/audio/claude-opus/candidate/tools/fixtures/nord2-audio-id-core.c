/* Host test for shim/nord2-xnu/audio_id.c against the open-drain I2C8 model.
 * No physical MMIO, GPIO or I2C access; everything is simulated. */
#include "audio_id.h"
#include "nord2-audio-i2c-sim.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d CHECK %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

static struct sim bus;

struct ctx {
	uint32_t snap[NAU_SNAP_COUNT];
	int afe_expected;
	unsigned reads, afe_reads, fail_read_at, callbacks;
	int take_rc, restore_rc;
	unsigned takes, restores, line_ops;
	int pins_on, m_scl_low, m_sda_low;
};

static int
c_read32(void *p, uint32_t pa, uint32_t *v)
{
	struct ctx *c = p;
	c->callbacks++;
	CHECK(nau_read_allowed(pa));
	CHECK(!c->pins_on); /* all MMIO precedes the bus phase */
	if (c->reads++ == c->fail_read_at) { return -1; }
	for (unsigned i = 0; i < NAU_SNAP_COUNT; i++) {
		if (nau_snapshot_pa[i] == pa) { *v = c->snap[i]; return 0; }
	}
	CHECK(c->afe_expected);
	for (unsigned i = 0; i < NAU_AFE_COUNT; i++) {
		if (nau_afe_pa[i] == pa) { *v = 0xa000u + i; c->afe_reads++; return 0; }
	}
	CHECK(0);
	return -1;
}

static int
c_take(void *p)
{
	struct ctx *c = p;
	c->callbacks++;
	c->takes++;
	if (c->take_rc == 0) { c->pins_on = 1; }
	return c->take_rc;
}

static int
c_restore(void *p)
{
	struct ctx *c = p;
	c->callbacks++;
	c->restores++;
	CHECK(c->pins_on);
	c->pins_on = 0;
	return c->restore_rc;
}

static void
c_scl(void *p, int release)
{
	struct ctx *c = p;
	CHECK(c->pins_on);
	c->line_ops++;
	c->m_scl_low = !release;
	sim_master(&bus, c->m_scl_low, c->m_sda_low);
}

static void
c_sda(void *p, int release)
{
	struct ctx *c = p;
	CHECK(c->pins_on);
	c->line_ops++;
	c->m_sda_low = !release;
	sim_master(&bus, c->m_scl_low, c->m_sda_low);
}

static int c_scl_level(void *p) { CHECK(((struct ctx *)p)->pins_on); return bus.scl; }
static int c_sda_level(void *p) { CHECK(((struct ctx *)p)->pins_on); return bus.sda; }
static void c_delay(void *p, unsigned us) { (void)p; sim_advance(&bus, us); }

static const struct nau_line_ops c_line = {c_scl, c_sda, c_scl_level, c_sda_level, c_delay};
static const struct nau_ops c_ops = {c_read32, c_take, c_restore, &c_line};

static void
cold_snapshot(uint32_t s[NAU_SNAP_COUNT])
{
	memset(s, 0, sizeof(uint32_t) * NAU_SNAP_COUNT);
	s[NAU_S_AUDIO_PWR_CON] = 0x112; /* ISO|CLK_DIS|SRAM_PDN: off */
	s[NAU_S_IFR1_CG] = 1u << 25;
	s[NAU_S_IFR3_CG] = 1u << 4;
	s[NAU_S_PROT2_STA1] = 1u << 4;
	s[NAU_S_CLK_CFG_7] = 1u << 31;
	s[NAU_S_CLK_CFG_8] = 1u << 7;
	s[NAU_S_MODE_120] = 0x1100;     /* GPIO122/123 mode1 = SCL8/SDA8 */
}

static void
powered_snapshot(uint32_t s[NAU_SNAP_COUNT])
{
	cold_snapshot(s);
	s[NAU_S_PWR_STATUS] = 1u << 21;
	s[NAU_S_PWR_STATUS_2ND] = 1u << 21;
	s[NAU_S_AUDIO_PWR_CON] = 0x0d;
	s[NAU_S_IFR1_CG] = 0;
	s[NAU_S_IFR3_CG] = 0;
	s[NAU_S_PROT2_STA1] = 0;
	s[NAU_S_CLK_CFG_7] = 0;
	s[NAU_S_CLK_CFG_8] = 0;
}

static void
setup(struct ctx *c, int powered)
{
	memset(c, 0, sizeof(*c));
	c->fail_read_at = ~0u;
	if (powered) { powered_snapshot(c->snap); } else { cold_snapshot(c->snap); }
	c->afe_expected = powered;
	sim_init(&bus);
	struct sim_dev *a = &bus.dev[0], *b = &bus.dev[1];
	a->reg[0x03] = 0x0b74; a->reg[0x00] = 0x0001; a->reg[0x13] = 0x0300;
	a->reg[0x14] = 0x0000; a->reg[0x15] = 0x0123; a->reg[0x16] = 0x0019; a->reg[0x17] = 0x0200;
	b->reg[0x03] = 0x0a74; b->reg[0x00] = 0x0019; b->reg[0x13] = 0x0000;
	b->reg[0x14] = 0x0048; b->reg[0x15] = 0x03ff; b->reg[0x16] = 0x01ff; b->reg[0x17] = 0x0001;
	/* Bystanders hold distinctive values that must never be read. */
	for (unsigned i = 2; i < SIM_DEVS; i++) {
		for (unsigned r = 0; r < 256; r++) { bus.dev[i].reg[r] = 0xdead; }
	}
}

static int
run(struct ctx *c, struct nau_state *s)
{
	memset(s, 0, sizeof(*s));
	return nau_run(&c_ops, c, 1, s);
}

static void
check_amp_ok(const struct nau_state *s, unsigned i)
{
	const struct nau_amp *m = &s->amp[i];
	CHECK(m->result == NAU_AMP_OK);
	CHECK(m->regs_read == NAU_AMP_REGS);
	for (unsigned j = 0; j < NAU_AMP_REGS; j++) {
		CHECK(m->reg[j] == bus.dev[i].reg[nau_amp_reg[j]]);
	}
}

static void
test_disabled(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 0);
	memset(&s, 0, sizeof(s));
	CHECK(nau_run(&c_ops, &c, 0, &s) == -1);
	CHECK(nau_run(&c_ops, &c, 2, &s) == -1);
	CHECK(nau_run(&c_ops, &c, 1, NULL) == -1);
	CHECK(nau_run(NULL, &c, 1, &s) == -1);
	CHECK(c.callbacks == 0 && s.entered == 0 && bus.rises == 0);
	struct nau_line_ops bad = c_line;
	bad.sda_level = NULL;
	struct nau_ops o = {c_read32, c_take, c_restore, &bad};
	CHECK(nau_run(&o, &c, 1, &s) == -3 && c.callbacks == 0 && s.entered == 1);
}

static void
test_cold_success(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 0);
	CHECK(run(&c, &s) == 0);
	CHECK(s.error == 0 && !s.halt && !s.busy && s.stage == NAU_DONE);
	CHECK(s.snap_read == NAU_SNAP_COUNT && c.afe_reads == 0 && s.afe_read == 0);
	CHECK(s.afe_block == (NAU_AFE_BLOCK_STATUS | NAU_AFE_BLOCK_PWR_CON | NAU_AFE_BLOCK_INFRA |
	    NAU_AFE_BLOCK_PROTECT | NAU_AFE_BLOCK_MUX));
	CHECK(c.takes == 1 && c.restores == 1 && s.pins_taken && s.pins_restored);
	CHECK(s.idle_before && s.idle_after && bus.scl && bus.sda);
	check_amp_ok(&s, 0);
	check_amp_ok(&s, 1);
	CHECK(s.transactions == 2 * NAU_AMP_REGS && s.bytes_sent == 3 * 2 * NAU_AMP_REGS);
	CHECK(s.bus_faults == 0 && s.recover_pulses == 0);
	CHECK(s.amp[0].rev == 0x0b74 && s.amp[0].family == 0x74 && s.amp[0].pwdn == 1 &&
	    s.amp[0].ampe == 0 && s.amp[0].manstate == 0 && s.amp[0].ovds == 1 &&
	    s.amp[0].clips == 1 && s.amp[0].bats == 0x123 && s.amp[0].temps == 0x19 &&
	    s.amp[0].vddps == 0x200);
	CHECK(s.amp[1].rev == 0x0a74 && s.amp[1].pwdn == 1 && s.amp[1].ampe == 1 &&
	    s.amp[1].dca == 1 && s.amp[1].manstate == 9 && s.amp[1].bats == 0x3ff &&
	    s.amp[1].temps == 0x1ff && s.amp[1].vddps == 1);
	CHECK(bus.dev[0].ptr_writes == NAU_AMP_REGS && bus.dev[1].ptr_writes == NAU_AMP_REGS);
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
	/* Reentry is refused without any callback. */
	unsigned before = c.callbacks, rises = bus.rises;
	CHECK(nau_run(&c_ops, &c, 1, &s) == -2 && c.callbacks == before && bus.rises == rises);
}

static void
test_powered_afe(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 1);
	CHECK(run(&c, &s) == 0);
	CHECK(s.afe_block == 0 && s.afe_read == NAU_AFE_COUNT && c.afe_reads == NAU_AFE_COUNT);
	for (unsigned i = 0; i < NAU_AFE_COUNT; i++) { CHECK(s.afe[i] == 0xa000u + i); }
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
}

static void
test_afe_block(void)
{
	uint32_t p[NAU_SNAP_COUNT], t[NAU_SNAP_COUNT];
	powered_snapshot(p);
	CHECK(nau_afe_block(p, NAU_SNAP_COUNT) == 0);
	CHECK(nau_afe_block(p, NAU_SNAP_COUNT - 1) == NAU_AFE_BLOCK_SNAPSHOT);
	static const struct { unsigned idx; uint32_t xor_mask; unsigned want; } m[] = {
		{NAU_S_PWR_STATUS, 1u << 21, NAU_AFE_BLOCK_STATUS},
		{NAU_S_PWR_STATUS_2ND, 1u << 21, NAU_AFE_BLOCK_STATUS},
		{NAU_S_AUDIO_PWR_CON, 1u << 0, NAU_AFE_BLOCK_PWR_CON},  /* RST_B clear */
		{NAU_S_AUDIO_PWR_CON, 1u << 1, NAU_AFE_BLOCK_PWR_CON},  /* ISO set */
		{NAU_S_AUDIO_PWR_CON, 1u << 2, NAU_AFE_BLOCK_PWR_CON},  /* ON clear */
		{NAU_S_AUDIO_PWR_CON, 1u << 3, NAU_AFE_BLOCK_PWR_CON},  /* ON_2ND clear */
		{NAU_S_AUDIO_PWR_CON, 1u << 4, NAU_AFE_BLOCK_PWR_CON},  /* CLK_DIS set */
		{NAU_S_AUDIO_PWR_CON, 1u << 8, NAU_AFE_BLOCK_PWR_CON},  /* SRAM_PDN set */
		{NAU_S_AUDIO_PWR_CON, 1u << 12, NAU_AFE_BLOCK_PWR_CON}, /* SRAM ack set */
		{NAU_S_IFR1_CG, 1u << 25, NAU_AFE_BLOCK_INFRA},
		{NAU_S_IFR3_CG, 1u << 4, NAU_AFE_BLOCK_INFRA},
		{NAU_S_PROT2_STA1, 1u << 4, NAU_AFE_BLOCK_PROTECT},
		{NAU_S_CLK_CFG_7, 1u << 31, NAU_AFE_BLOCK_MUX},
		{NAU_S_CLK_CFG_8, 1u << 7, NAU_AFE_BLOCK_MUX},
	};
	for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
		memcpy(t, p, sizeof(t));
		t[m[i].idx] ^= m[i].xor_mask;
		CHECK(nau_afe_block(t, NAU_SNAP_COUNT) == m[i].want);
	}
	/* Unrelated bits do not block. */
	memcpy(t, p, sizeof(t));
	t[NAU_S_PWR_STATUS] |= ~(1u << 21);
	t[NAU_S_AUDIO_PWR_CON] |= 0xffff0000u | (1u << 9);
	t[NAU_S_IFR1_CG] |= ~(1u << 25);
	t[NAU_S_CLK_CFG_8] |= 0x7f;
	CHECK(nau_afe_block(t, NAU_SNAP_COUNT) == 0);
}

static void
test_absent(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 0);
	bus.dev[1].present = 0;
	CHECK(run(&c, &s) == 0);
	check_amp_ok(&s, 0);
	CHECK(s.amp[1].result == NAU_AMP_NACK && s.amp[1].attempts == 3 && s.amp[1].fail_stage == 2);
	CHECK(s.bus_faults == 0 && s.idle_after);
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);

	setup(&c, 0);
	bus.dev[0].present = 0;
	bus.dev[1].present = 0;
	CHECK(run(&c, &s) == 0);
	CHECK(s.amp[0].result == NAU_AMP_NACK && s.amp[1].result == NAU_AMP_NACK);
	CHECK(s.transactions == 6 && s.bytes_sent == 6);
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
}

static void
test_stuck_sda_before(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 0);
	bus.dev[3].stuck_sda = 1; /* e.g. the DDR buck holds SDA: not ours to touch */
	sim_update(&bus);
	CHECK(run(&c, &s) == -10);
	CHECK(!s.idle_before && !s.idle_after && s.transactions == 0 && bus.starts == 0 && bus.rises == 0);
	CHECK(c.line_ops == 0);
	CHECK(s.amp[0].result == NAU_AMP_UNPROBED && s.amp[1].result == NAU_AMP_UNPROBED);
	CHECK(!s.busy && c.restores == 1 && s.pins_restored);
}

static void
test_stuck_scl(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 0);
	bus.dev[0].stuck_scl_after_addr = 1;
	CHECK(run(&c, &s) == -9);
	CHECK(s.amp[0].result == NAU_AMP_FAULT && s.amp[1].result == NAU_AMP_UNPROBED);
	CHECK(s.busy && !s.idle_after && !s.halt && s.pins_restored && c.restores == 1);
	CHECK(s.bus_faults == 1);
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
}

static void
test_stretch_recovery(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 0);
	bus.dev[0].stretch_us = 200;
	bus.dev[0].stretch_armed = 1;
	CHECK(run(&c, &s) == 0);
	CHECK(s.amp[0].result == NAU_AMP_FAULT && s.amp[0].fail_stage == 6);
	CHECK(s.recover_pulses > 0 && s.recover_pulses <= 9);
	check_amp_ok(&s, 1);
	CHECK(s.idle_after && !s.busy && s.bus_faults == 1);
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
}

static void
test_partial_and_unknown(void)
{
	struct ctx c;
	struct nau_state s;
	setup(&c, 0);
	bus.dev[0].nack_reg[0x14] = 1;
	bus.dev[1].reg[0x03] = 0x1234;
	CHECK(run(&c, &s) == 0);
	CHECK(s.amp[0].result == NAU_AMP_PARTIAL && s.amp[0].regs_read == 3 && s.amp[0].fail_stage == 3);
	CHECK(s.amp[0].family == 0 && s.amp[0].pwdn == 0);
	CHECK(s.amp[1].result == NAU_AMP_OK && s.amp[1].family == 0 && s.amp[1].rev == 0x1234);
	CHECK(s.amp[1].pwdn == 0 && s.amp[1].manstate == 0 && s.bus_faults == 0);
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
}

static void
test_callback_failures(void)
{
	struct ctx c;
	struct nau_state s;
	for (unsigned k = 0; k < NAU_SNAP_COUNT; k++) {
		setup(&c, 0);
		c.fail_read_at = k;
		CHECK(run(&c, &s) == -4 && s.snap_read == k && c.takes == 0 && c.line_ops == 0);
	}
	setup(&c, 1);
	c.fail_read_at = NAU_SNAP_COUNT + 4;
	CHECK(run(&c, &s) == -5 && s.afe_read == 4 && c.takes == 0 && c.line_ops == 0);

	setup(&c, 0);
	c.take_rc = -1;
	CHECK(run(&c, &s) == -7 && !s.halt && c.restores == 0 && c.line_ops == 0);
	setup(&c, 0);
	c.take_rc = -2;
	CHECK(run(&c, &s) == -6 && s.halt && c.restores == 0 && c.line_ops == 0);
	setup(&c, 0);
	c.restore_rc = -1;
	CHECK(run(&c, &s) == -8 && s.halt && !s.pins_restored);
}

static void
test_tables_and_pins(void)
{
	for (unsigned i = 0; i < NAU_SNAP_COUNT; i++) {
		uint32_t pa = nau_snapshot_pa[i];
		CHECK(pa && !(pa & 3) && nau_read_allowed(pa));
		uint32_t page = pa & ~0xfffu;
		CHECK(page == 0x10006000 || page == 0x10001000 || page == 0x10000000 ||
		    page == 0x1000c000 || page == 0x10005000 || page == 0x11d10000 ||
		    page == 0x11c20000 || page == 0x11f20000);
		for (unsigned j = 0; j < i; j++) { CHECK(nau_snapshot_pa[j] != pa); }
	}
	for (unsigned i = 0; i < NAU_AFE_COUNT; i++) {
		CHECK((nau_afe_pa[i] & ~0xfffu) == 0x11210000 && nau_read_allowed(nau_afe_pa[i]));
	}
	static const uint32_t denied[] = {0, 0x10006000, 0x10006304, 0x10001090, 0x10000004,
	    0x1000c000, 0x10005000, 0x10005034, 0x11210044, 0x11d04000, 0x10026000, 0x11212000};
	for (unsigned i = 0; i < sizeof(denied) / sizeof(denied[0]); i++) {
		CHECK(!nau_read_allowed(denied[i]));
	}
	for (unsigned i = 0; i < NAU_AMP_REGS; i++) {
		CHECK(nau_amp_reg[i] != 0x10 && nau_amp_reg[i] != 0x11 && nau_amp_reg[i] < 0x20);
	}

	uint32_t s[NAU_SNAP_COUNT];
	struct nau_pin p[NAU_PIN_COUNT];
	memset(s, 0, sizeof(s));
	s[NAU_S_MODE_32] = 0x1011;          /* GPIO32/33/35 mode1, GPIO34 mode0 */
	s[NAU_S_MODE_104] = 0;              /* GPIO111 mode0 */
	s[NAU_S_MODE_120] = 0x1100;         /* GPIO122/123 mode1 */
	s[NAU_S_MODE_144] = 0x30000;        /* GPIO148 mode3 */
	s[NAU_S_DIR_96] = 1u << 15;         /* GPIO111 output */
	s[NAU_S_DOUT_96] = 1u << 15;        /* ... driving high = reset asserted */
	s[NAU_S_DIN_96] = (1u << 26) | (1u << 27);
	s[NAU_S_LT_IES32] = 1u << 2;
	s[NAU_S_BM_PU122] = (1u << 3) | (1u << 13);
	s[NAU_S_RM_PD148] = 1u << 5;
	nau_pin_decode(s, p);
	CHECK(p[0].pin == 32 && p[0].mode == 1 && p[1].mode == 1 && p[2].mode == 0 && p[3].mode == 1);
	CHECK(p[2].ies == 1 && p[0].ies == 0);
	CHECK(p[4].pin == 111 && p[4].dir == 1 && p[4].dout == 1);
	CHECK(p[5].mode == 1 && p[6].mode == 1 && p[5].din && p[6].din && p[5].pu && p[6].pu);
	CHECK(p[7].pin == 148 && p[7].mode == 3 && p[7].pd == 1);
	CHECK(nau_amp_reset(p, 0) == 1 && nau_amp_reset(p, 1) == -1 && nau_amp_reset(p, 2) == -1);
	s[NAU_S_MODE_144] = 0;
	s[NAU_S_RM_IES148] = 1u << 5;
	s[NAU_S_DOUT_96] = 0;
	nau_pin_decode(s, p);
	CHECK(nau_amp_reset(p, 0) == 0 && nau_amp_reset(p, 1) == 0);
	s[NAU_S_DIN_128] = 1u << 20;
	nau_pin_decode(s, p);
	CHECK(nau_amp_reset(p, 1) == 1);
	s[NAU_S_RM_IES148] = 0;
	nau_pin_decode(s, p);
	CHECK(nau_amp_reset(p, 1) == -1);
}

int
main(void)
{
	test_disabled();
	test_cold_success();
	test_powered_afe();
	test_afe_block();
	test_absent();
	test_stuck_sda_before();
	test_stuck_scl();
	test_stretch_recovery();
	test_partial_and_unknown();
	test_callback_failures();
	test_tables_and_pins();
	puts("PASS nord2 audio-id core");
	return 0;
}
