/* Nord2 speaker-path diagnostics, native binding (revision 2).
 * Included by sweet_touch.c after the ni_* helpers; runs once on the input
 * thread. It never writes GPIO, IOCFG, clocks, power or PMIF.
 *  nord2audioid=1  milestone 1a: snapshot, MMIO READS ONLY (zero writes).
 *  nord2audioid=2  milestone 1b: the same snapshot, then, only when
 *                  nau_i2c8_ready()==0, the I2C8 hardware-controller REV probe
 *                  (audio_i2c8.c). 1b WRITES I2C8 AP-channel controller
 *                  registers; amplifier access is register-pointer + read only.
 *                  Not authorized for hardware; review the level-1 capture first.
 * Any uncertain 1b outcome is sticky: no further I2C8 callback in this boot
 * and nab_prepare_return() refuses Linux return without touching hardware. */
#include "audio_id.h"
#include "audio_i2c8.h"

static struct {
	struct nau_state state;
	struct na8_state bus;
	struct na8_amp amp[2];
	vm_offset_t spm, infra, top, apmixed, imps, bm, rm, afe, i2c8, apdma;
	unsigned level, ready, probed;
	int status, probe_rc;
	bool armed;
} nab;

#ifndef NAB_HOST_MMIO
static uint32_t
nab_mmio_read(vm_offset_t va, unsigned width)
{
	uint32_t v;
	if (width == 1) { v = *(volatile uint8_t *)va; }
	else if (width == 2) { v = *(volatile uint16_t *)va; }
	else { v = *(volatile uint32_t *)va; }
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
nab_mmio_write(vm_offset_t va, unsigned width, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	if (width == 1) { *(volatile uint8_t *)va = (uint8_t)v; }
	else if (width == 2) { *(volatile uint16_t *)va = (uint16_t)v; }
	else { *(volatile uint32_t *)va = v; }
	__asm__ volatile("dsb osh" ::: "memory");
}
#endif

static vm_offset_t
nab_va(uint32_t pa, unsigned width)
{
	vm_offset_t base;
	switch (pa & ~0xfffu) {
	case 0x10006000: base = nab.spm; break;
	case 0x10001000: base = nab.infra; break;
	case 0x10000000: base = nab.top; break;
	case 0x1000c000: base = nab.apmixed; break;
	case 0x10005000: base = ni_gpio; break;
	case 0x11d05000: base = nab.imps; break;
	case 0x11d10000: base = nab.bm; break;
	case 0x11c20000: base = nab.rm; break;
	case 0x11f20000: base = ni_lt; break;
	case 0x11210000: base = nab.afe; break;
	case 0x11d04000: base = nab.i2c8; break;
	case 0x11000000: base = nab.apdma; break;
	default: return 0;
	}
	if (!base || (pa & (width - 1))) { return 0; }
	return base + (pa & 0xfff);
}

/* Milestone 1a callback: the core's tables plus its clock predicates. */
static int
nab_snap_read(void *cookie, uint32_t pa, unsigned width, uint32_t *v)
{
	(void)cookie;
	const struct nau_state *s = &nab.state;
	if (!v || !nau_read_allowed(pa, width)) { return -1; }
	uint32_t page = pa & ~0xfffu;
	if (page == 0x11210000 && nau_afe_block(s->snap, s->snap_read)) { return -1; }
	if (page == 0x11d04000 && nau_i2c8_block(s->snap, s->snap_read)) { return -1; }
	if (page == 0x11000000 && nau_dma_block(s->snap, s->snap_read)) { return -1; }
	vm_offset_t va = nab_va(pa, width);
	if (!va) { return -1; }
	*v = nab_mmio_read(va, width);
	return 0;
}
static const struct nau_ops nab_snap_ops = {nab_snap_read};

/* Milestone 1b: exactly the accesses audio_i2c8.c makes. */
#define NAB_CH 0x11d04100u
static int
nab_ch_config(uint32_t off)
{
	switch (off) {
	case 0x04: case 0x08: case 0x10: case 0x14: case 0x18: case 0x1c: case 0x20:
	case 0x28: case 0x2c: case 0x30: case 0x34: case 0x40: case 0x44: case 0x48:
		return 1;
	default:
		return 0;
	}
}

static int
nab_bus_read_allowed(uint32_t pa, unsigned width)
{
	if (width == 4) {
		switch (pa) {
		case 0x100000b0: case 0x1000c340: case 0x1000c344: case 0x1000c34c:
		case 0x1000c308: case 0x1000c30c: case 0x1000c314: case 0x100053f0:
		case 0x1000616c: case 0x10006170: case 0x11d05e00: case 0x10001094:
		case 0x100010d8: case 0x11000788: case 0x11d10070: case 0x10005230:
			return 1;
		default:
			return 0;
		}
	}
	/* DATA_PORT pops the FIFO: only in the post-completion byte stage. */
	if (width == 1) { return pa == NAB_CH && nab.bus.inflight && nab.bus.stage == 13; }
	if (width != 2) { return 0; }
	if (pa == 0x11d04024 || pa == 0x11d04224 || pa == 0x11d04210 || pa == 0x11d04010 || pa == 0x11d040f4 || pa == 0x11d042f4) { return 1; }
	if ((pa & ~0xffu) != NAB_CH) { return 0; }
	uint32_t off = pa & 0xff;
	return nab_ch_config(off) || off == 0x0c || off == 0x24 || off == 0xf4;
}

static int
nab_bus_read(void *cookie, uint32_t pa, unsigned width, uint32_t *v)
{
	(void)cookie;
	if (!v || !nab_bus_read_allowed(pa, width)) { return -1; }
	/* Defence in depth: the APDMA channel only with both its clocks on. */
	if (pa == 0x11000788 && nau_dma_block(nab.state.snap, nab.state.snap_read)) { return -1; }
	vm_offset_t va = nab_va(pa, width);
	if (!va) { return -1; }
	*v = nab_mmio_read(va, width);
	return 0;
}

static int
nab_bus_write(void *cookie, uint32_t pa, unsigned width, uint32_t v)
{
	(void)cookie;
	uint32_t off = pa - NAB_CH;
	int ok = 0;
	if ((pa & ~0xffu) == NAB_CH) {
		if (width == 2 && nab_ch_config(off)) { ok = v <= 0xffff; }
		else if (width == 2 && off == 0x0c) { ok = v == 1; }      /* INTR_STAT W1C comp */
		else if (width == 2 && off == 0x38) { ok = v == 5; }      /* FIFO_ADDR_CLR(_MCH) */
		else if (width == 2 && off == 0x24) { ok = v == 1; }      /* TRANSAC_START only */
		else if (width == 1 && off == 0x00) { ok = v == 0x03 || v == 0x00 || v == 0x14; }
	}
	if (!ok) { return -1; }
	vm_offset_t va = nab_va(pa, width);
	if (!va) { return -1; }
	nab_mmio_write(va, width, v);
	return 0;
}

static int
nab_bus_delay(void *cookie, unsigned us)
{
	(void)cookie;
	delay(us);
	return 0;
}
static const struct na8_io nab_bus_io = {nab_bus_read, nab_bus_write, nab_bus_delay};

static void
nab_report(void)
{
	const struct nau_state *s = &nab.state;
	const uint32_t *v = s->snap;
	printf("NORD2 AUDIO ID: level=%u status=%d stage=%u failed=%u error=%d snap=%u/%u afe=%#x:%u i2c8=%#x:%u dma=%#x:%u ready=%#x reads=%u\n",
	    nab.level, nab.status, s->stage, s->failed, s->error, s->snap_read, NAU_SNAP_COUNT,
	    s->afe_block, s->afe_read, s->i2c8_block, s->i2c8_read, s->dma_block, s->dma_read,
	    nab.ready, s->callbacks);
	if (s->snap_read != NAU_SNAP_COUNT) { return; }
	printf("NORD2 AUDIO POWER: pwr=%#x pwr2=%#x audio_con=%#x adsp_con=%#x ifr1_cg=%#x ifr2_cg=%#x ifr3_cg=%#x prot2=%#x\n",
	    v[NAU_S_PWR_STATUS], v[NAU_S_PWR_STATUS_2ND], v[NAU_S_AUDIO_PWR_CON],
	    v[NAU_S_ADSP_PWR_CON], v[NAU_S_IFR1_CG], v[NAU_S_IFR2_CG], v[NAU_S_IFR3_CG],
	    v[NAU_S_PROT2_STA1]);
	printf("NORD2 AUDIO CLK: cfg7=%#x cfg8=%#x cfg10=%#x cfg11=%#x cfg12=%#x cfg13=%#x cfg15=%#x auddiv=%#x/%#x/%#x/%#x/%#x imps_cg=%#x\n",
	    v[NAU_S_CLK_CFG_7], v[NAU_S_CLK_CFG_8], v[NAU_S_CLK_CFG_10], v[NAU_S_CLK_CFG_11],
	    v[NAU_S_CLK_CFG_12], v[NAU_S_CLK_CFG_13], v[NAU_S_CLK_CFG_15], v[NAU_S_AUDDIV_0],
	    v[NAU_S_AUDDIV_1], v[NAU_S_AUDDIV_2], v[NAU_S_AUDDIV_3], v[NAU_S_AUDDIV_4],
	    v[NAU_S_IMPS_CG]);
 printf("NORD2 AUDIO PLL1: con3=%#x tuner=%#x apll1=%#x/%#x/%#x/%#x/%#x\n",v[NAU_S_AP_PLL_CON3],v[NAU_S_APLL1_TUNER],v[NAU_S_APLL1_CON0],v[NAU_S_APLL1_CON1],v[NAU_S_APLL1_CON2],v[NAU_S_APLL1_CON3],v[NAU_S_APLL1_CON4]);
 printf("NORD2 AUDIO PLL2: tuner=%#x apll2=%#x/%#x/%#x/%#x/%#x\n",v[NAU_S_APLL2_TUNER],v[NAU_S_APLL2_CON0],v[NAU_S_APLL2_CON1],v[NAU_S_APLL2_CON2],v[NAU_S_APLL2_CON3],v[NAU_S_APLL2_CON4]);
 printf("NORD2 AUDIO PARENT: main=%#x/%#x/%#x univ=%#x/%#x/%#x\n",v[NAU_S_MAINPLL_CON0],v[NAU_S_MAINPLL_CON1],v[NAU_S_MAINPLL_CON3],v[NAU_S_UNIVPLL_CON0],v[NAU_S_UNIVPLL_CON1],v[NAU_S_UNIVPLL_CON3]);
	if (s->afe_read == NAU_AFE_COUNT) {
		const uint32_t *a = s->afe;
		printf("NORD2 AUDIO AFE: top=%#x/%#x/%#x/%#x dac=%#x i2s3=%#x dl1=%#x conn28=%#x conn29=%#x\n",
		    a[NAU_A_TOP_CON0], a[NAU_A_TOP_CON1], a[NAU_A_TOP_CON2], a[NAU_A_TOP_CON3],
		    a[NAU_A_DAC_CON0], a[NAU_A_I2S_CON3], a[NAU_A_DL1_CON0], a[NAU_A_CONN28],
		    a[NAU_A_CONN29]);
	}
	if (s->i2c8_read == NAU_I2C8_COUNT) {
		static const char *const name[NAU_CH_COUNT] = {"shadow", "ap", "ccu"};
		for (unsigned ch = 0; ch < NAU_CH_COUNT; ch++) {
			const uint32_t *c = &s->i2c8[NAU_I2C8_INDEX(ch, 0)];
			printf("NORD2 AUDIO I2C8 %s: mask=%#x stat=%#x ctrl=%#x start=%#x fifo=%#x\n",
			    name[ch], c[NAU_C_MASK], c[NAU_C_STAT], c[NAU_C_CTRL], c[NAU_C_START],
			    c[NAU_C_FIFO]);
		}
	}
	if (s->dma_read == NAU_DMA_COUNT) {
		printf("NORD2 AUDIO APDMA: flag=%#x int_en=%#x en=%#x con=%#x\n",
		    s->dma[NAU_D_INT_FLAG], s->dma[NAU_D_INT_EN], s->dma[NAU_D_EN], s->dma[NAU_D_CON]);
	}
	printf("NORD2 AUDIO IOCFG: bm_eh=%#x/%#x bm_rsel=%#x\n",
	    v[NAU_S_BM_EH122], v[NAU_S_BM_EH123], v[NAU_S_BM_RSEL]);
	struct nau_pin pins[NAU_PIN_COUNT];
	nau_pin_decode(v, pins);
	for (unsigned i = 0; i < NAU_PIN_COUNT; i++) {
		printf("NORD2 AUDIO PIN: pin=%u mode=%u dir=%u out=%u in=%u ies=%u pu=%u pd=%u\n",
		    pins[i].pin, pins[i].mode, pins[i].dir, pins[i].dout, pins[i].din,
		    pins[i].ies, pins[i].pu, pins[i].pd);
	}
	if (!nab.probed) { return; }
	const struct na8_state *b = &nab.bus;
	printf("NORD2 AUDIO BUS: rc=%d stage=%u failure=%u offline=%u uncertain=%u mutated=%u inflight=%u polls=%u calls=%u completion=%#x prior=%#x/%u last=%#x/%u/%u/%u/%#x\n",
	    nab.probe_rc, b->stage, b->failure, b->offline, b->uncertain, b->mutated, b->inflight,
	    b->polls, b->calls, b->completion_irq, b->prior_irq, b->prior_ack, b->last_addr,
	    b->last_width, b->last_write, b->last_valid, b->last_value);
	printf("NORD2 AUDIO BUS PRE: mux=%#x parent=%u pinmux=%#x gate=%#x pwr=%#x/%#x ifr=%#x/%#x dma=%u:%#x sh=%#x ccu=%#x/%#x\n",
	    b->mux, b->parent_hz, b->pinmux, b->gate, b->pwr, b->pwr2, b->ifr1, b->ifr2,
	    b->dma_checked, b->dma_en, b->sh_start, b->ccu_start, b->ccu_ctrl);
	for (unsigned i = 0; i < 2; i++) {
		const struct na8_amp *m = &nab.amp[i];
		printf("NORD2 AUDIO AMP: addr=%#x reset=%d class=%u reads=%u rc=%d rev=%#x sys0=%#x st14=%#x pwdn=%u ampe=%u manstate=%u\n",
		    m->addr, nau_amp_reset(pins, i), m->cls, m->reads, m->rc, m->rev, m->sys0,
		    m->status14, m->pwdn, m->ampe, m->manstate);
	}
}

/* Called once from ni_thread after the Wi-Fi probes, before touch setup. */
static void
nab_audio_id(void)
{
	unsigned level = 0;
	PE_parse_boot_argn("nord2audioid", &level, sizeof(level));
	if (level != 1 && level != 2) { return; }
	if (nab.armed) { panic("NORD2 AUDIO ID: refuse reentry"); }
	nab.armed = true;
	nab.level = level;
	/* Mapping alone performs no access to any of these apertures. */
	nab.spm = ml_io_map(0x10006000, 0x1000);
	nab.infra = ml_io_map(0x10001000, 0x1000);
	nab.top = ml_io_map(0x10000000, 0x1000);
	nab.apmixed = ml_io_map(0x1000c000, 0x1000);
	nab.imps = ml_io_map(0x11d05000, 0x1000);
	nab.bm = ml_io_map(0x11d10000, 0x1000);
	nab.rm = ml_io_map(0x11c20000, 0x1000);
	nab.afe = ml_io_map(0x11210000, 0x1000);
	nab.i2c8 = ml_io_map(0x11d04000, 0x1000);
	nab.apdma = ml_io_map(0x11000000, 0x1000);
	if (!nab.spm || !nab.infra || !nab.top || !nab.apmixed || !nab.imps || !nab.bm ||
	    !nab.rm || !nab.afe || !nab.i2c8 || !nab.apdma || !ni_gpio || !ni_lt) {
		printf("NORD2 AUDIO ID: skipped=1 reason=map\n");
		return;
	}
	nab.status = nau_run(&nab_snap_ops, NULL, 1, &nab.state);
	nab.ready = nau_i2c8_ready(&nab.state);
	if (level == 2 && nab.status == 0 && nab.ready == 0) {
		nab.probed = 1;
		nab.probe_rc = na8_probe(&nab_bus_io, NULL, &nab.bus, nab.amp);
	}
	nab_report();
	if (!na8_return_ok(&nab.bus)) {
		printf("NORD2 AUDIO ID: I2C8 outcome uncertain (stage=%u failure=%u); bus offline, Linux return refused\n",
		    nab.bus.stage, nab.bus.failure);
	}
}

/* For nord2_input_prepare_return(): false refuses Linux return. Pure state
 * check; never touches hardware, never retries. */
static bool
nab_prepare_return(void)
{
	return na8_return_ok(&nab.bus) != 0;
}
