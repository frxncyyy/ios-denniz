/* Revision3 snapshot/PIO tests; simulated registers only. */
#include <assert.h>
#include <string.h>
#include "audio_id.h"
#include "audio_i2c8.h"
#include "nord2-audio-i2c8-model.h"

struct ctx {
	struct i2c8_model m;
	unsigned calls, writes, fail, failed, after, fail_delay;
	uint32_t snap[NAU_SNAP_COUNT];
};

static int
tick(struct ctx *c)
{
	if (c->failed) { c->after++; }
	if (++c->calls == c->fail) { c->failed = 1; return -1; }
	return 0;
}

static int
rd(void *p, uint32_t a, unsigned w, uint32_t *v)
{
	struct ctx *c = p;
	if (tick(c)) { return -1; }
	*v = m_read(&c->m, a, w);
	return 0;
}

static int
wr(void *p, uint32_t a, unsigned w, uint32_t v)
{
	struct ctx *c = p;
	c->writes++;
	m_write(&c->m, a, w, v);
	return tick(c);
}

static int
us(void *p, unsigned n)
{
	struct ctx *c = p;
	assert(n == 10);
	if (tick(c)) { return -1; }
	if (c->fail_delay) { c->failed = 1; return -1; }
	return 0;
}

static const struct na8_io io = {rd, wr, us};

static int
snap(void *p, uint32_t a, unsigned w, uint32_t *v)
{
	struct ctx *c = p;
	if (tick(c)) { return -1; }
	assert(nau_read_allowed(a, w));
	if (w == 4) {
		for (unsigned i = 0; i < NAU_SNAP_COUNT; i++) {
			if (a == nau_snapshot_pa[i]) { *v = c->snap[i]; return 0; }
		}
	}
	if (w == 4 && (a & ~0xfffu) == 0x11210000) {
		assert(!nau_afe_block(c->snap, NAU_SNAP_COUNT));
		for (unsigned i = 0; i < NAU_AFE_COUNT; i++) {
			if (a == nau_afe_pa[i]) { *v = 0x12340000u + i; return 0; }
		}
	}
	if (w == 4 && (a & ~0xfffu) == 0x11d04000) {
		assert(!nau_i2c8_block(c->snap, NAU_SNAP_COUNT));
	}
	if (w == 4 && (a & ~0xfffu) == 0x11000000) {
		assert(!nau_dma_block(c->snap, NAU_SNAP_COUNT));
	}
	*v = m_read(&c->m, a, w);
	return 0;
}

static const struct nau_ops so = {snap};

static void
setup(struct ctx *c)
{
	memset(c, 0, sizeof(*c));
	m_init(&c->m);
	c->snap[NAU_S_MODE_120] = c->m.pinmux;
	c->snap[NAU_S_IFR1_CG] = c->m.ifr1;
	c->snap[NAU_S_IFR2_CG] = c->m.ifr2;
	c->snap[NAU_S_BM_IES122] = c->m.ies;
	c->snap[NAU_S_DIN_96] = c->m.pads;
}

int
main(void)
{
	struct ctx c;
	struct nau_state ss = {0};
	struct na8_state s = {0};
	struct na8_amp amp[2];
	uint16_t v;
	unsigned calls;

	/* Default-off, reentry and every snapshot callback failure. */
	setup(&c);
	assert(nau_run(&so, &c, 0, &ss) == -1 && !c.calls);
	assert(nau_run(&so, &c, 1, &ss) == 0 && !c.writes &&
	    ss.i2c8_read == NAU_I2C8_COUNT && !ss.afe_read && !ss.dma_read);
	assert(nau_i2c8_ready(&ss) == 0);
	calls = c.calls;
	assert(nau_run(&so, &c, 1, &ss) == -2 && c.calls == calls);
	for (unsigned f = 1; f <= calls; f++) {
		setup(&c);
		c.fail = f;
		ss = (struct nau_state){0};
		assert(nau_run(&so, &c, 1, &ss) == -4 && ss.failed && !c.after && !c.writes);
	}

	/* I2C8 aperture gates: gated leaf, mux power-down, invalid PLL parent. */
	setup(&c);
	c.snap[NAU_S_IMPS_CG] = 1u << 4;
	ss = (struct nau_state){0};
	assert(nau_run(&so, &c, 1, &ss) == 0 && !ss.i2c8_read && nau_i2c8_ready(&ss));
	setup(&c);
	c.snap[NAU_S_CLK_CFG_10] = 1u << 15;
	ss = (struct nau_state){0};
	assert(nau_run(&so, &c, 1, &ss) == 0 && !ss.i2c8_read && nau_i2c8_ready(&ss));
	setup(&c);
	c.snap[NAU_S_CLK_CFG_10] = 3u << 8;
	ss = (struct nau_state){0};
	assert(nau_run(&so, &c, 1, &ss) == 0 && !ss.i2c8_read && nau_i2c8_ready(&ss));
	setup(&c);
	c.snap[NAU_S_CLK_CFG_10] = 1u << 8;
	c.snap[NAU_S_MAINPLL_CON0] = 0;
	ss = (struct nau_state){0};
	assert(nau_run(&so, &c, 1, &ss) == 0 && !ss.i2c8_read && nau_i2c8_ready(&ss));

	/* AFE block bits, one at a time. */
	setup(&c);
	c.snap[NAU_S_PWR_STATUS] = c.snap[NAU_S_PWR_STATUS_2ND] = 1u << 21;
	c.snap[NAU_S_AUDIO_PWR_CON] = 0xd;
	ss = (struct nau_state){0};
	assert(nau_run(&so, &c, 1, &ss) == 0 && ss.afe_read == NAU_AFE_COUNT);

	struct { unsigned field, bit, block; } afe[] = {
		{NAU_S_PWR_STATUS, 21, NAU_AFE_BLOCK_STATUS},
		{NAU_S_PWR_STATUS_2ND, 21, NAU_AFE_BLOCK_STATUS},
		{NAU_S_AUDIO_PWR_CON, 3, NAU_AFE_BLOCK_PWR_CON},
		{NAU_S_AUDIO_PWR_CON, 4, NAU_AFE_BLOCK_PWR_CON},
		{NAU_S_IFR1_CG, 25, NAU_AFE_BLOCK_INFRA},
		{NAU_S_IFR3_CG, 4, NAU_AFE_BLOCK_INFRA},
		{NAU_S_PROT2_STA1, 4, NAU_AFE_BLOCK_PROTECT},
		{NAU_S_CLK_CFG_7, 31, NAU_AFE_BLOCK_MUX},
		{NAU_S_CLK_CFG_8, 7, NAU_AFE_BLOCK_MUX},
	};
	for (unsigned i = 0; i < sizeof(afe) / sizeof(afe[0]); i++) {
		setup(&c);
		c.snap[NAU_S_PWR_STATUS] = c.snap[NAU_S_PWR_STATUS_2ND] = 1u << 21;
		c.snap[NAU_S_AUDIO_PWR_CON] = 0xd;
		c.snap[afe[i].field] ^= 1u << afe[i].bit;
		ss = (struct nau_state){0};
		assert(nau_run(&so, &c, 1, &ss) == 0 && ss.afe_read == 0);
		assert(ss.afe_block == afe[i].block);
	}

	/* APDMA gate bits and the ungated idle case. */
	setup(&c);
	c.m.ifr1 = 0;
	c.snap[NAU_S_IFR1_CG] = 0;
	c.snap[NAU_S_IFR2_CG] = 1u << 31;
	ss = (struct nau_state){0};
	assert(nau_run(&so, &c, 1, &ss) == 0 && !ss.dma_read && ss.dma_block == NAU_DMA_BLOCK_APDMA);
	setup(&c);
	c.m.ifr1 = 0;
	c.snap[NAU_S_IFR1_CG] = 0;
	ss = (struct nau_state){0};
	assert(nau_run(&so, &c, 1, &ss) == 0 && ss.dma_read == NAU_DMA_COUNT &&
	    !ss.dma_block && nau_i2c8_ready(&ss) == 0);

	/* Full real controller-model REV/pointer read. */
	setup(&c);
	s = (struct na8_state){0};
	assert(na8_probe(&io, &c, &s, amp) == 0);
	assert(amp[0].rev == 0xb74 && amp[0].reads == 3 && amp[1].rev == 0xa74 &&
	    amp[1].reads == 3 && c.m.ntrans == 6 && na8_return_ok(&s));
	assert(c.m.setup_hw_timeout == 386); /* 10400/27, i2c-mtk.c:963-977 */
	m_check_trans(&c.m);
	m_check_restored(&c.m);

	/* Every callback failing along a clean read: sticky, no replay. */
	setup(&c);
	s = (struct na8_state){0};
	assert(na8_read16(&io, &c, &s, 0x34, 3, &v) == 0 && v == 0xb74);
	m_check_restored(&c.m);
	calls = c.calls;
	for (unsigned f = 1; f <= calls; f++) {
		setup(&c);
		c.fail = f;
		s = (struct na8_state){0};
		assert(na8_read16(&io, &c, &s, 0x34, 3, &v) != 0 && s.offline && !c.after);
		unsigned old = c.calls;
		assert(na8_read16(&io, &c, &s, 0x34, 3, &v) != 0 && c.calls == old);
		assert(na8_return_ok(&s) == !s.uncertain);
	}

	/* Clean NACK: source returns -EREMOTEIO without init_hw (i2c-mtk.c:1326-1330). */
	setup(&c);
	c.m.slave[0].nack = 1;
	s = (struct na8_state){0};
	assert(na8_read16(&io, &c, &s, 0x34, 3, &v) == 1);
	assert(s.nack && !s.uncertain && !s.inflight && na8_return_ok(&s));
	assert(c.m.ntrans == 1 && c.m.starts == 1 && c.writes > 0);
	m_check_trans(&c.m);
	m_check_restored(&c.m);
	setup(&c);
	c.m.slave[0].nack = 1;
	s = (struct na8_state){0};
	assert(na8_probe(&io, &c, &s, amp) == 0);
	assert(amp[0].cls == NA8_NACK && amp[0].reads == 0 && amp[1].cls == NA8_SUPPORTED &&
	    amp[1].reads == 3 && c.m.ntrans == 4 && na8_return_ok(&s));
	m_check_trans(&c.m);
	setup(&c);
	c.m.slave[1].nack = 1;
	s = (struct na8_state){0};
	assert(na8_probe(&io, &c, &s, amp) == 0);
	assert(amp[0].cls == NA8_SUPPORTED && amp[1].cls == NA8_NACK && c.m.ntrans == 4 &&
	    na8_return_ok(&s));
	setup(&c);
	c.m.slave[0].nack = c.m.slave[1].nack = 1;
	s = (struct na8_state){0};
	assert(na8_probe(&io, &c, &s, amp) == 0);
	assert(amp[0].cls == NA8_NACK && amp[1].cls == NA8_NACK && c.m.ntrans == 2 &&
	    na8_return_ok(&s));
	/* A NACK whose status clear is refused is uncertain, not benign. */
	setup(&c);
	c.m.slave[0].nack = 1;
	c.m.irq_sticky = 1;
	s = (struct na8_state){0};
	assert(na8_read16(&io, &c, &s, 0x34, 3, &v) == -2 && s.uncertain && s.nack &&
	    !na8_return_ok(&s));
	/* A NACK with START still set is not a completed clean STOP. */
	setup(&c);
	c.m.slave[0].nack = 1;
	c.m.start_stuck = 1;
	s = (struct na8_state){0};
	assert(na8_read16(&io, &c, &s, 0x34, 3, &v) == -2 && s.uncertain && !s.nack &&
	    !na8_return_ok(&s));

	/* Hard completion errors remain uncertain. */
	setup(&c);
	c.m.behaviour = M_BUSERR;
	s = (struct na8_state){0};
	assert(na8_probe(&io, &c, &s, amp) == -2 && s.uncertain && !s.nack &&
	    c.m.starts == 1 && !amp[1].reads && !na8_return_ok(&s));

	/* Unknown/unlisted/other-family revisions: only REV is read on that amp. */
	for (unsigned i = 0; i < 3; i++) {
		setup(&c);
		c.m.slave[0].reg[3] = i == 0 ? 0x1234 : i == 1 ? 0x0174 : 0x0194;
		s = (struct na8_state){0};
		assert(na8_probe(&io, &c, &s, amp) == 0 && amp[0].reads == 1 &&
		    amp[0].cls != NA8_SUPPORTED && amp[1].reads == 3 && c.m.ntrans == 4);
		m_check_trans(&c.m);
	}
	assert(na8_classify(0x0a74) == NA8_SUPPORTED && na8_classify(0x0c74) == NA8_SUPPORTED &&
	    na8_classify(0x0a73) == NA8_SUPPORTED && na8_classify(0x1a73) == NA8_SUPPORTED);
	assert(na8_classify(0x0d74) == NA8_UNLISTED_REV && na8_classify(0x0194) == NA8_OTHER_FAMILY &&
	    na8_classify(0x1234) == NA8_UNKNOWN);

	/* Timeout and delay failure. */
	setup(&c);
	c.m.behaviour = M_HANG;
	s = (struct na8_state){0};
	assert(na8_probe(&io, &c, &s, amp) == -2 && s.polls == 2000 && s.inflight &&
	    s.uncertain && c.m.starts == 1 && !amp[1].reads && !na8_return_ok(&s));
	setup(&c);
	c.m.behaviour = M_HANG;
	c.fail_delay = 1;
	s = (struct na8_state){0};
	assert(na8_probe(&io, &c, &s, amp) == -2 && s.failure == 12 && s.uncertain &&
	    !c.after && c.m.starts == 1);

	/* Inherited exact completion and sticky IRQ. */
	setup(&c);
	c.m.ch[1][6] = 1;
	s = (struct na8_state){0};
	assert(na8_read16(&io, &c, &s, 0x34, 3, &v) == 0 && s.prior_ack);
	setup(&c);
	c.m.ch[1][6] = 1;
	c.m.irq_sticky = 1;
	s = (struct na8_state){0};
	assert(na8_read16(&io, &c, &s, 0x34, 3, &v) == -2 && s.uncertain && !c.m.starts);

	/* Preflight refusals: no writes, no START, Linux return allowed. */
	for (unsigned k = 0; k < 14; k++) {
		setup(&c);
		s = (struct na8_state){0};
		if (k == 0) { c.m.gate = 16; }
		if (k == 1) { c.m.pwr = 1u << 23; }
		if (k == 2) { c.m.ch[0][0x24 / 2] = 1; }
		if (k == 3) { c.m.ch[2][0x24 / 2] = 1; }
		if (k == 4) { c.m.ch[1][0x10 / 2] |= 4; }
		if (k == 5) { c.m.ch[1][0xf4 / 2] = 1; }
		if (k == 6) { c.m.ch[1][6] = 3; }
		if (k == 7) { c.m.pads = 0; }
		if (k == 8) { c.m.ies = 0; }
		if (k == 9) { c.m.ch[0][0x10 / 2] = 4; }
		if (k == 10) { c.m.ch[2][0xf4 / 2] = 1; }
		if (k == 11) { c.m.ifr1 = 0; c.m.dma[2] = 1; }
		if (k == 12) { c.m.ch[2][0x10 / 2] = 4; }
		if (k == 13) { c.m.ch[1][0xe4 / 2] = 0x8000; }
		assert(na8_read16(&io, &c, &s, 0x34, 3, &v) == -1);
		assert(!c.writes && !c.m.starts && na8_return_ok(&s));
	}
	setup(&c);
	s = (struct na8_state){0};
	assert(na8_read16(&io, &c, &s, 0x51, 3, &v) == -1 && !c.calls && !c.writes);

	puts("PASS revision3 snapshot zero-write/gates/faults; PIO source args/BE REV/"
	    "variant-first/NACK/HW-timeout/I3C/each callback failure/no replay/timeout/"
	    "shared-bus refusal");
	return 0;
}
