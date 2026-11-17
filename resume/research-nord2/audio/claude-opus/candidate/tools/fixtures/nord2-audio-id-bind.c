/* Host test for the native binding shim/nord2-xnu/audio_id_bind.inc.
 * Supplies the sweet_touch.c environment (ni_* helpers copied verbatim from
 * xnu/patches/0082 bsd/dev/sweet_touch.c) over a simulated register file:
 * GPIO DIR/DOUT SET/CLR aliases, DIN from the I2C8 wire model, MODE, IOCFG.
 * Every MMIO write is logged and checked against the binding's contract. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include "nord2-audio-i2c-sim.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d CHECK %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
#define VOFF ((vm_offset_t)0x7f0000000000ull) /* catches PA-as-VA misuse */

static struct sim bus;

/* ---- simulated register file ---- */
#define PAGE_WORDS 1024
struct page {
	uint32_t pa;
	uint32_t w[PAGE_WORDS];
	int writable;
};
static struct page pages[] = {
	{0x10006000, {0}, 0}, {0x10001000, {0}, 0}, {0x10000000, {0}, 0}, {0x1000c000, {0}, 0},
	{0x10005000, {0}, 1}, {0x11d10000, {0}, 1}, {0x11c20000, {0}, 0}, {0x11f20000, {0}, 0},
	{0x11210000, {0}, 0},
};
#define NPAGES (sizeof(pages) / sizeof(pages[0]))

static struct { uint32_t pa, v, before; } wlog[4096];
static unsigned nwlog, nreads, afe_reads, mapped, mode_writes, freeze_mode_after = ~0u;
static uint32_t rlog[65536];
static int afe_powered;

static struct page *
page_of(uint32_t pa)
{
	for (unsigned i = 0; i < NPAGES; i++) {
		if ((pa & ~0xfffu) == pages[i].pa) { return &pages[i]; }
	}
	fprintf(stderr, "unmapped pa %#x\n", pa);
	abort();
}

static uint32_t *
word(uint32_t pa)
{
	return &page_of(pa)->w[(pa & 0xfff) / 4];
}

static unsigned
pin_mode(unsigned pin)
{
	return (*word(0x10005300 + pin / 8 * 16) >> (pin % 8 * 4)) & 15;
}

static unsigned
pin_bit(uint32_t base, unsigned pin)
{
	return (*word(0x10005000 + base + pin / 32 * 16) >> (pin % 32)) & 1;
}

/* Pad model for GPIO122 (SCL8) / GPIO123 (SDA8). Function mode 1: the idle
 * I2C8 controller releases the line. */
static void
pads_update(void)
{
	int low[2] = {0, 0};
	for (unsigned i = 0; i < 2; i++) {
		unsigned pin = 122 + i;
		if (pin_mode(pin) == 0 && pin_bit(0, pin)) {
			if (pin_bit(0x100, pin)) { bus.master_drive_high++; }
			else { low[i] = 1; }
		}
	}
	sim_master(&bus, low[0], low[1]);
}

static uint32_t
mmio_read(uint32_t pa)
{
	if (nreads < sizeof(rlog) / sizeof(rlog[0])) { rlog[nreads] = pa; }
	nreads++;
	if ((pa & ~0xfffu) == 0x11210000) {
		CHECK(afe_powered);
		afe_reads++;
	}
	if (pa == 0x10005230) {
		/* DIN 96..127: bus levels for 122/123, stored bits elsewhere. */
		uint32_t v = *word(pa) & ~((1u << 26) | (1u << 27));
		return v | ((uint32_t)bus.scl << 26) | ((uint32_t)bus.sda << 27);
	}
	return *word(pa);
}

static void
mmio_write(uint32_t pa, uint32_t v)
{
	struct page *p = page_of(pa);
	CHECK(p->writable);
	CHECK(nwlog < sizeof(wlog) / sizeof(wlog[0]));
	uint32_t off = pa & 0xfff;
	if (p->pa == 0x10005000 && off < 0x300 && (off & 0xf)) {
		/* DIR/DOUT SET (+4) / CLR (+8) aliases of the word at off & ~0xf. */
		uint32_t *w = word(pa & ~0xfu);
		wlog[nwlog++] = (typeof(wlog[0])){pa, v, *w};
		if ((off & 0xf) == 4) { *w |= v; }
		else if ((off & 0xf) == 8) { *w &= ~v; }
		else { CHECK(0); }
	} else {
		uint32_t *w = word(pa);
		wlog[nwlog++] = (typeof(wlog[0])){pa, v, *w};
		if (pa == 0x100053f0 && mode_writes++ >= freeze_mode_after) { return; }
		*w = v;
	}
	pads_update();
}

/* ---- XNU environment expected by audio_id_bind.inc ---- */
static vm_offset_t ni_gpio, ni_lt;
static unsigned bootarg;
static jmp_buf panic_jmp;
static char out[1 << 16];
static size_t outlen;

static vm_offset_t
ml_io_map(vm_offset_t pa, vm_size_t size)
{
	CHECK(size == 0x1000);
	mapped++;
	return pa + VOFF;
}

static int
PE_parse_boot_argn(const char *name, void *arg, int size)
{
	CHECK(!strcmp(name, "nord2audioid") && size == sizeof(unsigned));
	*(unsigned *)arg = bootarg;
	return 1;
}

static void
delay(unsigned us)
{
	sim_advance(&bus, us);
}

static int
sim_printf(const char *fmt, ...)
{
	va_list ap;
	size_t start = outlen;
	va_start(ap, fmt);
	int n = vsnprintf(out + outlen, sizeof(out) - outlen, fmt, ap);
	va_end(ap);
	CHECK(n >= 0 && (size_t)n < sizeof(out) - outlen);
	outlen += (size_t)n;
	if (getenv("NORD2_AUDIO_VERBOSE")) { fputs(out + start, stdout); }
	return n;
}
#define printf sim_printf

static void __attribute__((noreturn))
sim_panic(const char *msg)
{
	sim_printf("PANIC %s\n", msg);
	longjmp(panic_jmp, 1);
}
#define panic(...) sim_panic(__VA_ARGS__)

static uint32_t
ni_rd(vm_offset_t base, unsigned off)
{
	CHECK(base >= VOFF);
	return mmio_read((uint32_t)(base - VOFF) + off);
}

static void
ni_wr(vm_offset_t base, unsigned off, uint32_t v)
{
	CHECK(base >= VOFF);
	mmio_write((uint32_t)(base - VOFF) + off, v);
}

/* ---- verbatim from bsd/dev/sweet_touch.c (patch 0082) ---- */
static void
ni_mask(vm_offset_t base, unsigned off, uint32_t mask, uint32_t v)
{
	ni_wr(base, off, (ni_rd(base, off) & ~mask) | (v & mask));
}

static void
ni_gpio_bit(unsigned pin, unsigned reg, bool set)
{
	/* MTK GPIO SET/CLR aliases: affect only this pin. */
	ni_wr(ni_gpio, reg + pin / 32 * 16 + (set ? 4 : 8), 1u << (pin % 32));
}

static bool
ni_level(unsigned pin)
{
	return !!(ni_rd(ni_gpio, 0x200 + pin / 32 * 16) & (1u << (pin % 32)));
}
/* ---- end verbatim ---- */

#include "audio_id_bind.inc"

/* ---- test harness ---- */
static uint32_t initial[NPAGES][PAGE_WORDS];

static void
reset_world(int powered)
{
	for (unsigned i = 0; i < NPAGES; i++) { memset(pages[i].w, 0, sizeof(pages[i].w)); }
	memset(&nab, 0, sizeof(nab));
	nwlog = nreads = afe_reads = mapped = mode_writes = 0;
	freeze_mode_after = ~0u;
	outlen = 0;
	out[0] = 0;
	afe_powered = powered;
	sim_init(&bus);
	struct sim_dev *a = &bus.dev[0], *b = &bus.dev[1];
	a->reg[0x03] = 0x0b74; a->reg[0x00] = 0x0001; a->reg[0x14] = 0;
	a->reg[0x15] = 0x0123; a->reg[0x16] = 0x0019; a->reg[0x17] = 0x0200;
	b->reg[0x03] = 0x0b74; b->reg[0x00] = 0x0001;
	ni_gpio = 0x10005000 + VOFF;
	ni_lt = 0x11f20000 + VOFF;
	/* Cold audio domain (the expected live state), GPIO120..127 functions. */
	*word(0x10006354) = 0x112;
	*word(0x10001094) = 1u << 25;
	*word(0x100010ac) = 1u << 4;
	*word(0x10001724) = 1u << 4;
	*word(0x10000080) = 1u << 31;
	*word(0x10000090) = 1u << 7;
	if (powered) {
		*word(0x1000616c) = 1u << 21;
		*word(0x10006170) = 1u << 21;
		*word(0x10006354) = 0x0d;
		*word(0x10001094) = 0;
		*word(0x100010ac) = 0;
		*word(0x10001724) = 0;
		*word(0x10000080) = 0;
		*word(0x10000090) = 0;
	}
	*word(0x100053f0) = 0x21111111;     /* 120..127: 122/123 mode1, neighbours kept */
	*word(0x11d10070) = 0xffffffff;     /* IES enabled everywhere */
	*word(0x11d100b0) = (1u << 3) | (1u << 13);
	*word(0x10005030) = 0x00004001;     /* unrelated outputs in the same DIR word */
	*word(0x10005130) = 0x00000001;
	*word(0x10005230) = 0x00008000;     /* GPIO111 level high */
	for (unsigned i = 0; i < NPAGES; i++) { memcpy(initial[i], pages[i].w, sizeof(initial[i])); }
	pads_update();
}

static void
check_state_restored(void)
{
	for (unsigned i = 0; i < NPAGES; i++) {
		CHECK(!memcmp(initial[i], pages[i].w, sizeof(initial[i])));
	}
}

/* The binding may write only GPIO122/123 DIR/DOUT/MODE and their IES bits. */
static void
check_write_contract(void)
{
	for (unsigned i = 0; i < nwlog; i++) {
		uint32_t pa = wlog[i].pa, v = wlog[i].v, before = wlog[i].before;
		switch (pa) {
		case 0x10005034: case 0x10005038: case 0x10005134: case 0x10005138:
			CHECK(v == (1u << 26) || v == (1u << 27));
			break;
		case 0x100053f0:
			CHECK(((v ^ before) & ~0x0000ff00u) == 0);
			break;
		case 0x11d10070:
			CHECK(((v ^ before) & ~((1u << 3) | (1u << 13))) == 0);
			break;
		default:
			fprintf(stderr, "unexpected write %#x=%#x\n", pa, v);
			CHECK(0);
		}
	}
}

static int
allowed_read(uint32_t pa)
{
	return nau_read_allowed(pa) || pa == 0x10005030 || pa == 0x10005130 ||
	    pa == 0x10005230 || pa == 0x100053f0 || pa == 0x11d10070;
}

static void
check_reads(void)
{
	unsigned n = nreads < sizeof(rlog) / sizeof(rlog[0]) ? nreads : sizeof(rlog) / sizeof(rlog[0]);
	for (unsigned i = 0; i < n; i++) { CHECK(allowed_read(rlog[i])); }
}

static int
run_once(void)
{
	if (setjmp(panic_jmp)) { return 1; }
	nab_audio_id();
	return 0;
}

static void
test_disabled(void)
{
	reset_world(0);
	bootarg = 0;
	CHECK(run_once() == 0);
	CHECK(mapped == 0 && nreads == 0 && nwlog == 0 && outlen == 0);
	bootarg = 2;
	CHECK(run_once() == 0 && mapped == 0 && nreads == 0 && nwlog == 0);
	CHECK(nab_prepare_return());
}

static void
test_success(void)
{
	reset_world(0);
	bootarg = 1;
	CHECK(run_once() == 0);
	CHECK(strstr(out, "NORD2 AUDIO ID: status=0 "));
	CHECK(strstr(out, "NORD2 AUDIO AFE: skipped block=0x3e"));
	CHECK(strstr(out, "NORD2 AUDIO AMP: addr=0x34 reset=-1 result=3"));
	CHECK(strstr(out, "rev=0xb74 family=0x74 pwdn=1"));
	CHECK(nab.state.amp[0].result == NAU_AMP_OK && nab.state.amp[1].result == NAU_AMP_OK);
	CHECK(afe_reads == 0 && nwlog > 0 && !nab.changed);
	check_state_restored();
	check_write_contract();
	check_reads();
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
	CHECK(bus.scl && bus.sda);
	CHECK(nab_prepare_return());
	/* A second call panics before any further access. */
	unsigned w = nwlog, r = nreads;
	CHECK(run_once() == 1 && strstr(out, "PANIC NORD2 AUDIO ID: refuse reentry"));
	CHECK(nwlog == w && nreads == r);
}

static void
test_powered(void)
{
	reset_world(1);
	bootarg = 1;
	CHECK(run_once() == 0);
	CHECK(nab.state.afe_block == 0 && afe_reads == NAU_AFE_COUNT);
	CHECK(strstr(out, "NORD2 AUDIO AFE: top="));
	check_state_restored();
	check_write_contract();
	check_reads();
}

static void
test_odd_saved_state(void)
{
	/* Function mode with DIR=1/DOUT=1 left behind: restore must reselect the
	 * function before DIR/DOUT so the pad never drives high or low. */
	reset_world(0);
	*word(0x10005030) |= (1u << 26) | (1u << 27);
	*word(0x10005130) |= (1u << 26) | (1u << 27);
	for (unsigned i = 0; i < NPAGES; i++) { memcpy(initial[i], pages[i].w, sizeof(initial[i])); }
	bootarg = 1;
	CHECK(run_once() == 0);
	CHECK(nab.state.amp[0].result == NAU_AMP_OK);
	CHECK(bus.master_drive_high == 0);
	check_state_restored();
	check_write_contract();
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);

	/* Saved GPIO input mode is accepted and kept. */
	reset_world(0);
	*word(0x100053f0) = 0x21110011;
	for (unsigned i = 0; i < NPAGES; i++) { memcpy(initial[i], pages[i].w, sizeof(initial[i])); }
	pads_update();
	CHECK(run_once() == 0 && nab.state.amp[1].result == NAU_AMP_OK);
	check_state_restored();
	check_write_contract();
}

static void
test_foreign_owner(void)
{
	/* GPIO122 in another function, or a GPIO output: no write at all. */
	static const uint32_t modes[] = {0x21111211, 0x21112111};
	for (unsigned i = 0; i < 2; i++) {
		reset_world(0);
		*word(0x100053f0) = modes[i];
		for (unsigned j = 0; j < NPAGES; j++) { memcpy(initial[j], pages[j].w, sizeof(initial[j])); }
		bootarg = 1;
		CHECK(run_once() == 0);
		CHECK(nab.state.error == -7 && nwlog == 0 && !nab.changed && bus.rises == 0);
		CHECK(nab_prepare_return());
	}
	reset_world(0);
	*word(0x100053f0) = 0x21110011;
	*word(0x10005030) |= 1u << 27;
	bootarg = 1;
	CHECK(run_once() == 0 && nab.state.error == -7 && nwlog == 0);
}

static void
test_restore_failures(void)
{
	/* MODE writes stop taking effect after the two take writes: the restore
	 * cannot be verified -> halt, Linux return refused. */
	reset_world(0);
	freeze_mode_after = 2;
	bootarg = 1;
	CHECK(run_once() == 0);
	CHECK(nab.state.error == -8 && nab.state.halt && nab.changed);
	CHECK(strstr(out, "Linux return refused"));
	CHECK(!nab_prepare_return());
	check_write_contract();

	/* MODE writes never take effect: take detects it, self-restores, -7. */
	reset_world(0);
	freeze_mode_after = 0;
	CHECK(run_once() == 0);
	CHECK(nab.state.error == -7 && !nab.state.halt && !nab.changed && bus.rises == 0);
	check_state_restored();
	CHECK(nab_prepare_return());
}

static void
test_bus_held(void)
{
	reset_world(0);
	bus.dev[4].stuck_sda = 1; /* UFS buck holding SDA: never clock it */
	sim_update(&bus);
	bootarg = 1;
	CHECK(run_once() == 0);
	CHECK(nab.state.error == -10 && bus.rises == 0 && bus.starts == 0);
	check_state_restored();
	check_write_contract();
	CHECK(nab_prepare_return());

	reset_world(0);
	bus.dev[0].stuck_scl_after_addr = 1;
	CHECK(run_once() == 0);
	CHECK(nab.state.error == -9 && nab.state.busy && !nab.state.halt);
	CHECK(!nab_prepare_return());
	check_state_restored();
	check_write_contract();
	sim_check_wire(&bus, nau_amp_reg, NAU_AMP_REGS);
}

int
main(void)
{
	test_disabled();
	test_success();
	test_powered();
	test_odd_saved_state();
	test_foreign_owner();
	test_restore_failures();
	test_bus_held();
	puts("PASS nord2 audio-id native binding");
	return 0;
}
