#include <arm64/sweet_early.h>
#if defined(NORD2)
#include "../../../../../../shim/nord2-xnu/wifi_common.c"
#define guard scope_private_guard
#include "scope.c"
#undef guard
#define guard nai_private_guard
#include "../../adie-id/id.c"
#undef guard
#include "strap.c"
#include "../../../../../../shim/nord2-xnu/wifi_cold_snapshot.c"
/*
 * Nord2 FT3518 and physical keys. Pin assignments/report format come from
 * the live Nord2 DT and its Oplus FT3518/MT6885 pinctrl/keypad drivers.
 * One polling thread owns GPIO I2C0; open drain is direction-switching,
 * never driving a high level. No DMA, clock votes or firmware upload.
 * I2C5 accesses only the MT6360 ID and dedicated LDO2 VTP supply, with CRC,
 * readback, voltage/enable allowlist and restoration before rescue.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern void sweet_hid_key(int, int);
extern void sweet_hid_touch(int, int, int, int);

enum { NI_SCL = 204, NI_SDA = 205, NI_RESET = 20, NI_IRQ = 21, NI_VOLDN = 155, NI_PSCL = 202, NI_PSDA = 203 };
static unsigned ni_scl = NI_SCL, ni_sda = NI_SDA;
static vm_offset_t ni_gpio, ni_lt, ni_tm, ni_rt, ni_pwrap;
static bool ni_started, ni_saved, ni_bus_owned, ni_pwrap_owned;
static int ni_chan;
static uint32_t ni_active;
static int ni_x[10], ni_y[10];
static unsigned ni_events;
static struct {
	unsigned pin, mux, dir, dout;
} ni_pins[] = {{NI_SCL,0,0,0}, {NI_SDA,0,0,0}, {NI_RESET,0,0,0},
              {NI_IRQ,0,0,0}, {NI_VOLDN,0,0,0}, {NI_PSCL,0,0,0}, {NI_PSDA,0,0,0}};
static uint32_t ni_ies_lt, ni_ies_tm, ni_ies_rt, ni_ies_pwr;
static bool ni_ldo_changed;
static uint8_t ni_ldo_en, ni_ldo_volt;
static unsigned ni_stage;

static uint32_t
ni_rd(vm_offset_t base, unsigned off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
ni_wr(vm_offset_t base, unsigned off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = v;
	__asm__ volatile("dsb osh" ::: "memory");
}

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

static void
ni_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&ni_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static void
ni_line(unsigned pin, bool high)
{
	ni_gpio_bit(pin, 0, !high); /* DOUT fixed at zero: release or pull low. */
	delay(4);
}

static bool
ni_clock_high(void)
{
	ni_line(ni_scl, true);
	for (unsigned i = 0; i < 100; i++) {
		if (ni_level(ni_scl)) { return true; }
		delay(1);
	}
	return false;
}

static bool
ni_stop(void)
{
	ni_line(ni_scl, false);
	ni_line(ni_sda, false);
	bool ok = ni_clock_high();
	ni_line(ni_sda, true);
	return ok && ni_level(ni_sda);
}

static bool
ni_start(void)
{
	ni_line(ni_sda, true);
	if (!ni_clock_high() || !ni_level(ni_sda)) { return false; }
	ni_line(ni_sda, false);
	ni_line(ni_scl, false);
	return true;
}

static bool
ni_send(uint8_t b)
{
	for (unsigned bit = 0; bit < 8; bit++) {
		ni_line(ni_sda, !!(b & 0x80));
		if (!ni_clock_high()) { return false; }
		ni_line(ni_scl, false);
		b <<= 1;
	}
	ni_line(ni_sda, true);
	if (!ni_clock_high()) { return false; }
	bool ack = !ni_level(ni_sda);
	ni_line(ni_scl, false);
	return ack;
}

static bool
ni_recv(uint8_t *b, bool last)
{
	uint8_t v = 0;
	ni_line(ni_sda, true);
	for (unsigned bit = 0; bit < 8; bit++) {
		if (!ni_clock_high()) { return false; }
		v = (uint8_t)((v << 1) | ni_level(ni_sda));
		ni_line(ni_scl, false);
	}
	ni_line(ni_sda, last); /* master NACK on final byte */
	if (!ni_clock_high()) { return false; }
	ni_line(ni_scl, false);
	ni_line(ni_sda, true);
	*b = v;
	return true;
}

static void
ni_bus(bool power)
{
	ni_scl = power ? NI_PSCL : NI_SCL;
	ni_sda = power ? NI_PSDA : NI_SDA;
}

static bool
ni_bus_read(uint8_t addr, uint8_t reg, uint8_t *data, unsigned len)
{
	if (!len || len > 62) { return false; }
	ni_stage = 1;
	bool ok = ni_start();
	if (ok) { ni_stage = 2; ok = ni_send(addr << 1); }
	if (ok) { ni_stage = 3; ok = ni_send(reg); }
	if (ok) { ni_stage = 4; ok = ni_start(); }
	if (ok) { ni_stage = 5; ok = ni_send((addr << 1) | 1u); }
	for (unsigned i = 0; ok && i < len; i++) {
		ni_stage = 6;
		ok = ni_recv(&data[i], i + 1 == len);
	}
	bool stopped = ni_stop();
	if (ok) { ni_stage = stopped ? 0 : 7; }
	return ok && stopped;
}

static bool
ni_read(uint8_t reg, uint8_t *data, unsigned len)
{
	ni_bus(false);
	return ni_bus_read(0x38, reg, data, len);
}

static uint8_t
ni_crc(uint8_t addr, uint8_t reg, uint8_t value)
{
	uint8_t bytes[3] = {addr, reg, value}, crc = 0;
	for (unsigned n = 0; n < 3; n++) {
		crc ^= bytes[n];
		for (unsigned b = 0; b < 8; b++) {
			crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 7 : 0));
		}
	}
	return crc;
}

static bool
ni_ldo_read(uint8_t reg, uint8_t *value)
{
	uint8_t b[2];
	if (reg != 0x11 && reg != 0x15) { return false; }
	ni_bus(true);
	if (!ni_bus_read(0x64, reg, b, 2) || b[1] != ni_crc(0xc9, reg, b[0])) {
		return false;
	}
	*value = b[0];
	return true;
}

static bool
ni_ldo_write(uint8_t reg, uint8_t value)
{
	/* Only LDO2's saved/requested selector or enable state may be written. */
	if (!ni_ldo_changed ||
	    (reg == 0x15 ? (value != 0xaa && value != ni_ldo_volt) :
	    (reg != 0x11 || ((value ^ ni_ldo_en) & ~0x40u)))) {
		return false;
	}
	ni_bus(true);
	/* Vendor CRC-protected SMBus I2C-block write: value, CRC, dummy byte. */
	bool ok = ni_start() && ni_send(0xc8) && ni_send(reg) &&
	    ni_send(value) && ni_send(ni_crc(0xc8, reg, value)) && ni_send(0);
	bool stopped = ni_stop();
	if (!ok || !stopped) { return false; }
	uint8_t check;
	return ni_ldo_read(reg, &check) &&
	    (reg == 0x15 ? check == value : (check & 0x40) == (value & 0x40));
}

static bool
ni_power_restore(void)
{
	if (!ni_ldo_changed) { return true; }
	/* Reset low while changing its supply; pin restore follows this call. */
	ni_gpio_bit(NI_RESET, 0x100, false);
	ni_gpio_bit(NI_RESET, 0, true);
	bool ok = ni_ldo_write(0x11, ni_ldo_en & ~0x40u) &&
	    ni_ldo_write(0x15, ni_ldo_volt) &&
	    ni_ldo_write(0x11, ni_ldo_en);
	if (ok) { ni_ldo_changed = false; }
	return ok;
}

static bool
ni_power_on(void)
{
	uint8_t id = 0, en = 0, volt = 0;
	ni_bus(true);
	if (!ni_level(NI_PSCL) || !ni_level(NI_PSDA) ||
	    !ni_bus_read(0x34, 0, &id, 1) || (id & 0xf0) != 0x50 ||
	    !ni_ldo_read(0x11, &en) || !ni_ldo_read(0x15, &volt)) {
		printf("NORD2 INPUT: MT6360 identification/CRC failed id=%#x stage=%u scl=%d sda=%d\n",
		    id, ni_stage, ni_level(NI_PSCL), ni_level(NI_PSDA));
		return false;
	}
	printf("NORD2 INPUT: MT6360 id=%#x VTP enable=%#x selector=%#x\n", id, en, volt);
	ni_ldo_en = en;
	ni_ldo_volt = volt;
	/* Select exactly the board DT's 3.0 V, not the misleading 2v8 name. */
	if (volt < 0xaa || volt > 0xb0 || !(en & 0x40)) {
		ni_ldo_changed = true; /* saved state precedes every possible write */
		bool ok = ni_ldo_write(0x11, en & ~0x40u) &&
		    ni_ldo_write(0x15, 0xaa) && ni_ldo_write(0x11, en | 0x40u);
		if (!ok) {
			printf("NORD2 INPUT: VTP write/readback failed; restore=%d\n", ni_power_restore());
			return false;
		}
	}
	for (unsigned i = 0; i < 20; i++) {
		delay(1000);
		if (!ni_ldo_read(0x11, &en)) { break; }
		if (en & 4) {
			printf("NORD2 INPUT: VTP enabled at 3.0V, status=%#x changed=%d\n", en, ni_ldo_changed);
			return true;
		}
	}
	printf("NORD2 INPUT: VTP failed to become ready; restore=%d\n", ni_power_restore());
	return false;
}

/* Pure decoder: reject a whole corrupt report before publishing any contact.
 * FT3518's high_resolution_support is x4, not the generic 12-bit protocol. */
static bool
ni_decode(const uint8_t b[62], uint32_t *active, int x[10], int y[10])
{
	uint32_t seen = 0, mask = 0;
	if (b[1] > 10) { return false; }
	for (unsigned i = 0; i < 10; i++) {
		unsigned off = 6 * i, id = b[off + 4] >> 4;
		if (id == 15) { break; }
		if (id >= 10 || (seen & (1u << id))) { return false; }
		seen |= 1u << id;
		unsigned event = b[off + 2] >> 6;
		if (event == 3) { return false; }
		if (event == 1) { continue; }
		unsigned px = ((b[off + 2] & 15) << 10) |
		    (b[off + 3] << 2) | ((b[off + 6] >> 6) & 3);
		unsigned py = ((b[off + 4] & 15) << 10) |
		    (b[off + 5] << 2) | ((b[off + 6] >> 4) & 3);
		if (!b[1] || px >= 4320 || py >= 9600) { return false; }
		x[id] = (int)(px / 4);
		y[id] = (int)(py / 4);
		mask |= 1u << id;
	}
	if ((unsigned)__builtin_popcount(mask) != b[1]) { return false; }
	*active = mask;
	return true;
}

static void
ni_report(uint32_t mask, const int x[10], const int y[10])
{
	for (unsigned i = 0; i < 10; i++) {
		bool was = !!(ni_active & (1u << i)), down = !!(mask & (1u << i));
		if (was != down || (down && (ni_x[i] != x[i] || ni_y[i] != y[i]))) {
			if (down) { ni_x[i] = x[i]; ni_y[i] = y[i]; }
			if (was != down && ni_events++ < 80) {
				printf("NORD2 INPUT: finger=%u down=%d x=%d y=%d\n",
				    i, down, ni_x[i], ni_y[i]);
			}
			sweet_hid_touch((int)i, down, ni_x[i], ni_y[i]);
		}
	}
	ni_active = mask;
}

static bool
ni_touch(void)
{
	uint8_t b[62], ack;
	memset(b, 0xff, sizeof(b));
	if (!ni_read(0x01, b, 15)) { return false; }
	if (b[0] == 0xff && b[1] == 0 && b[2] == 0xff) { return true; }
	if (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff) {
		(void)ni_read(0x3e, &ack, 1);
		return false;
	}
	if (b[14] == 0xff) {
		if (!ni_read(0x3e, &ack, 1)) { return false; }
	} else if (!ni_read(0x10, b + 15, 47)) { return false; }
	uint32_t active;
	int x[10] = {0}, y[10] = {0};
	if (!ni_decode(b, &active, x, y)) { return false; }
	ni_report(active, x, y);
	return true;
}

/* MT6885 PMIF SWINF2 sends a read only. The legacy WACS2 layout is wrong
 * for this SoC. Source: drivers/soc/mediatek/mtk-pmic-wrap_v1.c.
 * VLD_CLR acknowledges the wrapper, not the PMIC. This input thread is the
 * sole owner: the optional radio snapshot runs before the key polling loop. */
static bool
ni_pmic_read(uint16_t addr, uint16_t *out)
{
	/* TOPSTATUS plus MT6359 radio rail/clock readback only. No write opcode,
	 * arbitrary PMIC address, or clear-on-read interrupt status is admitted. */
	switch (addr) {
	case 0x002a: /* TOPSTATUS: power bit1, HOMEKEY/volume-up bit3, active-low */
	case 0x0788: case 0x079e: /* DCXO_CW00, CW09 */
	case 0x1be2: case 0x1be6: case 0x1be8: case 0x1bee: case 0x1bf4:
	case 0x1c08: case 0x1c0c: case 0x1c0e: case 0x1c14: case 0x1c1a:
	case 0x1c1c: case 0x1c20: case 0x1c22: case 0x1c28:
	case 0x1c2e: case 0x1c32: case 0x1c34: case 0x1c3a:
	case 0x1f98: case 0x1f9c: case 0x2010: case 0x202e:
		break;
	default:
		return false;
	}
	uint32_t v = ni_rd(ni_pwrap, 0xca8);
	if (!(v & (1u << 15))) { return false; }
	if (((v >> 1) & 7u) == 6 && ni_pwrap_owned) {
		ni_wr(ni_pwrap, 0xca4, 1);
		v = ni_rd(ni_pwrap, 0xca8);
	}
	if (((v >> 1) & 7u) != 0) { return false; }
	ni_pwrap_owned = true;
	ni_wr(ni_pwrap, 0xc80, addr);
	for (unsigned i = 0; i < 1000; i++) {
		v = ni_rd(ni_pwrap, 0xca8);
		if (((v >> 1) & 7u) == 6) {
			*out = (uint16_t)ni_rd(ni_pwrap, 0xc94);
			ni_wr(ni_pwrap, 0xca4, 1);
			return true;
		}
		delay(1);
	}
	return false;
}

static bool
ni_keys_read(uint16_t *out)
{
	return ni_pmic_read(0x2a, out);
}

static void
ni_wifi_snapshot(void)
{
	int enabled = 0;
	PE_parse_boot_argn("nord2wifi", &enabled, sizeof(enabled));
	if (enabled != 1 && enabled != 2) { return; }
	/* AP-side registers only. The CONN region (0x18000000..) is deliberately
	 * not mapped: an access while power/bus protection is wrong can hang
	 * the bus regardless of any software timeout. No radio state is changed.
	 * Register definitions: Android MT6893 conninfra POS, MT6359 upmu_hw.h. */
	vm_offset_t spm = ml_io_map(0x10006000, 0x1000);
	vm_offset_t infra = ml_io_map(0x10001000, 0x1000);
	vm_offset_t gals = ml_io_map(0x1020e000, 0x1000);
	vm_offset_t rgu = ml_io_map(0x10007000, 0x1000);
	if (!spm || !infra || !gals || !rgu) {
		printf("NORD2 WIFI: snapshot mapping failed; radio untouched\n");
		return;
	}
	printf("NORD2 WIFI: AP snapshot begin; no radio/PMIC-value writes\n");
	printf("NORD2 WIFI: SPM cfg=%#x pwr=%#x ack=%#x/%#x rc=%#x\n",
	    ni_rd(spm, 0), ni_rd(spm, 0x304), ni_rd(spm, 0x16c),
	    ni_rd(spm, 0x170), ni_rd(spm, 0x504));
	printf("NORD2 WIFI: AXI protect=%#x/%#x GALS=%#x reset=%#x\n",
	    ni_rd(infra, 0x228), ni_rd(infra, 0x724),
	    ni_rd(gals, 0x504), ni_rd(rgu, 0x18));
	printf("NORD2 WIFI: pins mode21=%#x mode22=%#x drive=%#x pd=%#x pu=%#x\n",
	    ni_rd(ni_gpio, 0x450), ni_rd(ni_gpio, 0x460),
	    ni_rd(ni_rt, 0), ni_rd(ni_rt, 0x50), ni_rd(ni_rt, 0x70));
	static const uint16_t registers[] = {
	    0x0788, 0x079e,
	    0x1be2, 0x1be6, 0x1be8, 0x1bee, 0x1bf4,
	    0x1c08, 0x1c0c, 0x1c0e, 0x1c14, 0x1c1a,
	    0x1c1c, 0x1c20, 0x1c22, 0x1c28,
	    0x1c2e, 0x1c32, 0x1c34, 0x1c3a,
	    0x1f98, 0x1f9c, 0x2010, 0x202e,
	};
	unsigned done = 0;
	for (; done < sizeof(registers) / sizeof(registers[0]); done++) {
		uint16_t value = 0;
		if (!ni_pmic_read(registers[done], &value)) {
			printf("NORD2 WIFI: PMIC read failed addr=%#x status=%#x\n",
			    registers[done], ni_rd(ni_pwrap, 0xca8));
			break;
		}
		printf("NORD2 WIFI: PMIC[%#x]=%#x\n", registers[done], value);
	}
	printf("NORD2 WIFI: snapshot complete pmic=%u/24; continuing iOS\n", done);
}

/* NORD2 Wi-Fi power probe.
 * Register/sequence reference: MediaTek's GPL-2.0 platform code,
 * Copyright (c) 2020 MediaTek Inc., mt6893_pos.c/mt6893_pmic.c, and
 * clk-mt6885-pg.c selected by CONFIG_COMMON_CLK_MT6893. This bounded probe
 * only starts the conninfra domain, reads its identity and restores state.
 * No WFSYS CPU release, firmware/EMI writes, DMA, calibration or RF.
 * MT6359P addresses below match mt6885 upmu_hw.h, NOT mt6359/registers.h.
 * The existing input thread is the sole PMIF/GPIO owner during this probe. */
static bool ncc_fresh_owned, ncc_latched_dirty;
static struct {
	vm_offset_t spm, infra, gals;
	uint32_t cfg, pwr, prot, prot2, clock, mux21, mux22, drive, pd, pu;
	uint16_t pmic[6];
	bool saved, domain_touched;
} nw;
static const uint16_t nw_addr[6] = {0x1c34, 0x1c3a, 0x1c2e, 0x1c22, 0x1c28, 0x1c1c};
static const uint16_t nw_mask[6] = {0xf0, 0xf0, 3, 0xf0, 0xf0, 3};
static const uint16_t nw_value[6] = {0xf0, 0, 3, 0xf0, 0, 3};

static bool
nw_wait(vm_offset_t base, unsigned off, uint32_t mask, uint32_t value)
{
	for (unsigned i = 0; i < 2000; i++) {
		if ((ni_rd(base, off) & mask) == value) { return true; }
		delay(10);
	}
	printf("NORD2 WIFI: power timeout off=%#x mask=%#x want=%#x got=%#x\n",
	    off, mask, value, ni_rd(base, off));
	return false;
}

static bool
nw_pmic_write(unsigned slot, bool restore)
{
	/* Allow only each saved value or its RC-mode request. Preserve all other
	 * fields; neither voltage selectors nor unrelated PMIC rails are writable. */
	if (!nw.saved || slot >= 6) { return false; }
	uint16_t addr = nw_addr[slot], current = 0;
	uint16_t want = restore ? nw.pmic[slot] :
	    (nw.pmic[slot] & ~nw_mask[slot]) | nw_value[slot];
	if (!ni_pmic_read(addr, &current)) { return false; }
	if (current == want) { return true; }
	if ((current & ~nw_mask[slot]) != (want & ~nw_mask[slot])) { return false; }
	if (!nw_wait(ni_pwrap, 0xca8, 0x800e, 0x8000)) { return false; }
	ni_pwrap_owned = true;
	ni_wr(ni_pwrap, 0xc84, want);
	ni_wr(ni_pwrap, 0xc80, (1u << 29) | addr);
	/* Write completion is idle, not read-valid. Serialize a readback only
	 * after it completes; all PMIF commands remain on the same thread. */
	if (!nw_wait(ni_pwrap, 0xca8, 0x800e, 0x8000)) { return false; }
	bool ok = ni_pmic_read(addr, &current) && current == want;
	if (!ok) {
		printf("NORD2 WIFI: PMIC verify addr=%#x want=%#x got=%#x restore=%d\n",
		    addr, want, current, restore);
	}
	return ok;
}

static bool
nw_rail_wait(uint16_t addr, uint16_t mask, uint16_t want)
{
	uint16_t v = 0;
	for (unsigned i = 0; i < 100; i++) {
		if (!ni_pmic_read(addr, &v)) { return false; }
		/* MT6359P monitor B_EN and B_STB. Do not proceed on SW_EN alone. */
		if ((v & mask) == want) { return true; }
		delay(100);
	}
	printf("NORD2 WIFI: rail timeout addr=%#x mask=%#x want=%#x got=%#x\n",
	    addr, mask, want, v);
	return false;
}

static bool
nw_restore(void)
{
	if (ncc_latched_dirty) { return false; } /* preserve dirty common ownership */
	if (!nw.saved) { return true; }
	if (nw.domain_touched) {
		/* Isolate the bus before removing power, including partial-on failures.
		 * Refuse to drop rails or return to Linux if protection cannot settle. */
		ni_wr(nw.infra, 0x2a0, 1u << 13);
		if (!nw_wait(nw.infra, 0x228, 1u << 13, 1u << 13)) { return false; }
		ni_wr(nw.infra, 0x2a0, 1u << 14);
		if (!nw_wait(nw.infra, 0x228, 1u << 14, 1u << 14)) { return false; }
		ni_wr(nw.infra, 0x714, 4);
		if (!nw_wait(nw.infra, 0x724, 4, 4)) { return false; }
		ni_mask(nw.spm, 0x304, 2, 2);
		ni_mask(nw.spm, 0x304, 0x10, 0x10);
		ni_mask(nw.spm, 0x304, 1, 0);
		ni_mask(nw.spm, 0x304, 4, 0);
		ni_mask(nw.spm, 0x304, 8, 0);
		if (!nw_wait(nw.spm, 0x16c, 2, 0) ||
		    !nw_wait(nw.spm, 0x170, 2, 0)) { return false; }
		ni_mask(nw.spm, 0x304, 0x1f, nw.pwr);
		/* Restore only the protection bits this probe owns. Bootloader leaves
		 * bit13 clear even while CONN is off; preserve that measured state. */
		ni_wr(nw.infra, 0x2a4, (~nw.prot) & 0x6000);
		ni_wr(nw.infra, 0x718, (~nw.prot2) & 4);
		if (!nw_wait(nw.infra, 0x228, 0x6000, nw.prot & 0x6000) ||
		    !nw_wait(nw.infra, 0x724, 4, nw.prot2 & 4)) { return false; }
		nw.domain_touched = false;
	}
	ni_mask(nw.gals, 0x504, 1, nw.clock);
	ni_wr(nw.spm, 0, 0x0b160000 | (nw.cfg & 0xffff));
	ni_mask(ni_gpio, 0x450, 0xffff0000, nw.mux21);
	ni_mask(ni_gpio, 0x460, 0x0fffffff, nw.mux22);
	ni_mask(ni_rt, 0, 0x3fff8, nw.drive);
	ni_mask(ni_rt, 0x50, 0x4800, nw.pd);
	ni_mask(ni_rt, 0x70, 0x4800, nw.pu);
	/* Reverse order disables VCN13 before VCN18; retain context on failure. */
	for (unsigned i = 6; i > 0; i--) {
		if (!nw_pmic_write(i - 1, true)) { return false; }
	}
	if (!nw_rail_wait(0x1c20, 1, 0) || !nw_rail_wait(0x1c32, 1, 0)) { return false; }
	if ((ni_rd(nw.gals, 0x504) & 1) != (nw.clock & 1) ||
	    (ni_rd(nw.spm, 0) & 0xffff) != (nw.cfg & 0xffff) ||
	    (ni_rd(nw.spm, 0x304) & 0x1f) != (nw.pwr & 0x1f) ||
	    (ni_rd(ni_gpio, 0x450) & 0xffff0000) != (nw.mux21 & 0xffff0000) ||
	    (ni_rd(ni_gpio, 0x460) & 0x0fffffff) != (nw.mux22 & 0x0fffffff) ||
	    (ni_rd(ni_rt, 0) & 0x3fff8) != (nw.drive & 0x3fff8) ||
	    (ni_rd(ni_rt, 0x50) & 0x4800) != (nw.pd & 0x4800) ||
	    (ni_rd(ni_rt, 0x70) & 0x4800) != (nw.pu & 0x4800)) { return false; }
	nw.saved = false;
	ncc_fresh_owned = false;
	return true;
}

#include "common_adapter.candidate.inc"

static void
ni_wifi_power_probe(void)
{
	if (ncc_latched_dirty) { panic("NORD2 WIFI COMMON: dirty; refuse another probe"); }
	ncc_fresh_owned = false;
	int enabled = 0;
	PE_parse_boot_argn("nord2wifi", &enabled, sizeof(enabled));
	if (enabled != 2) { return; }
	nw.spm = ml_io_map(0x10006000, 0x1000);
	nw.infra = ml_io_map(0x10001000, 0x1000);
	nw.gals = ml_io_map(0x1020e000, 0x1000);
	if (!nw.spm || !nw.infra || !nw.gals) { return; }
	nw.cfg = ni_rd(nw.spm, 0);
	nw.pwr = ni_rd(nw.spm, 0x304);
	nw.prot = ni_rd(nw.infra, 0x228);
	nw.prot2 = ni_rd(nw.infra, 0x724);
	nw.clock = ni_rd(nw.gals, 0x504);
	nw.mux21 = ni_rd(ni_gpio, 0x450); nw.mux22 = ni_rd(ni_gpio, 0x460);
	nw.drive = ni_rd(ni_rt, 0); nw.pd = ni_rd(ni_rt, 0x50); nw.pu = ni_rd(ni_rt, 0x70);
	uint16_t volts = 0, mon13 = 0, mon18 = 0;
	/* Cold RC-mode only, matching the measured Nord2 state. Never seize an
	 * active/shared radio domain or adjust voltage to make a guard pass. */
	if ((nw.pwr & 0x1f) != 0x12 || (ni_rd(nw.spm, 0x16c) & 2) ||
	    (ni_rd(nw.spm, 0x170) & 2) || !(ni_rd(nw.spm, 0x504) & 1) ||
	    !(nw.prot & 0x4000) || !(nw.prot2 & 4) ||
	    (nw.clock & 1) || (nw.mux21 & 0xffff0000) || (nw.mux22 & 0x0fffffff) ||
	    !ni_pmic_read(0x202e, &volts) || (volts & 0xf0f) != 0x400 ||
	    !ni_pmic_read(0x1c20, &mon13) || mon13 ||
	    !ni_pmic_read(0x1c32, &mon18) || mon18) {
		printf("NORD2 WIFI: power probe refused non-cold/RC/1.3V state\n");
		return;
	}
	for (unsigned i = 0; i < 6; i++) {
		if (!ni_pmic_read(nw_addr[i], &nw.pmic[i]) ||
		    (nw.pmic[i] & nw_mask[i])) {
			printf("NORD2 WIFI: power probe refused PMIC slot=%u\n", i);
			return;
		}
	}
	ncc_fresh_owned = true; /* only after ALL original cold+PMIC checks */
	nw.saved = true; /* Arm restoration before the first PMIC/state write. */
	unsigned stage = 1;
	bool success = false;
	uint32_t id = 0, cfg_id = 0;
	printf("NORD2 WIFI: cold power probe begin; no firmware/DMA/RF\n");
	for (unsigned i = 0; i < 6; i++) {
		if (!nw_pmic_write(i, false)) { goto out; }
	}
	if (!nw_rail_wait(0x1c32, 3, 3) || !nw_rail_wait(0x1c20, 3, 3)) { goto out; }
	stage = 2;
	ni_mask(ni_gpio, 0x450, 0xffff0000, 0x11110000);
	ni_mask(ni_gpio, 0x460, 0x0fffffff, 0x01111111);
	ni_mask(ni_rt, 0, 0x3fff8, 0x8248);
	ni_mask(ni_rt, 0x50, 0x4800, 0);
	ni_mask(ni_rt, 0x70, 0x4800, 0x4800);
	ni_wr(nw.spm, 0, 0x0b160001 | (nw.cfg & 0xfffe));
	ni_mask(nw.gals, 0x504, 1, 1);
	nw.domain_touched = true;
	ni_mask(nw.spm, 0x304, 4, 4);
	ni_mask(nw.spm, 0x304, 8, 8);
	if (!nw_wait(nw.spm, 0x16c, 2, 2) || !nw_wait(nw.spm, 0x170, 2, 2)) { goto out; }
	ni_mask(nw.spm, 0x304, 0x10, 0);
	ni_mask(nw.spm, 0x304, 2, 0);
	ni_mask(nw.spm, 0x304, 1, 1);
	ni_wr(nw.infra, 0x2a4, 0x4000);
	ni_wr(nw.infra, 0x718, 4); /* Selected clk-mt6885-pg, not POS's 0x2. */
	ni_wr(nw.infra, 0x2a4, 0x2000);
	if (!nw_wait(nw.infra, 0x228, 0x6000, 0) ||
	    !nw_wait(nw.infra, 0x724, 4, 0)) { goto out; }
	delay(6000); /* Platform XO settling delay. */
	stage = 3;
	/* Only map/access CONN after AP power, clock and bus acknowledgements. */
	if ((ni_rd(nw.spm, 0x304) & 0x1f) != 0xd ||
	    !(ni_rd(nw.gals, 0x504) & 1)) { goto out; }
	vm_offset_t host = ml_io_map(0x18060000, 0x1000);
	vm_offset_t conn = ml_io_map(0x18001000, 0x1000);
	if (!host || !conn || !nw_wait(host, 0x184, 0x20, 0)) { goto out; }
	id = ni_rd(conn, 0);
	if (id != 0x20010101) { goto out; }
	cfg_id = ni_rd(conn, 4);
	ncc_power_window(host, conn, id);
	success = true;
out:
	printf("NORD2 WIFI: power probe stage=%u id=%#x cfg=%#x success=%d\n",
	    stage, id, cfg_id, success);
	bool restored = nw_restore();
	printf("NORD2 WIFI: power probe restored=%d\n", restored);
	if (!restored) { panic("NORD2 WIFI: restoration failed; refuse Linux return"); }
}

int
nord2_input_prepare_return(void)
{
	if (ncc_latched_dirty) { return -1; } /* refuse Linux-return preparation */
	/* Called only after all other cores are OFF. Never take a thread lock. */
	if (!nw_restore()) { return -1; }
	if (!ni_saved) { return 0; }
	bool idle = true;
	if (ni_pwrap_owned) {
		idle = false;
		for (unsigned i = 0; i < 1000; i++) {
			unsigned fsm = (ni_rd(ni_pwrap, 0xca8) >> 1) & 7;
			if (fsm == 6) { ni_wr(ni_pwrap, 0xca4, 1); }
			if (fsm == 0) { idle = true; break; }
			delay(1);
		}
	}
	bool power_ok = true;
	if (ni_bus_owned) {
		ni_bus(false); (void)ni_stop();
		ni_bus(true); (void)ni_stop();
		power_ok = ni_power_restore();
		ni_bus(false);
	}
	for (unsigned i = 0; i < sizeof(ni_pins) / sizeof(ni_pins[0]); i++) {
		unsigned pin = ni_pins[i].pin, shift = pin % 8 * 4;
		ni_gpio_bit(pin, 0, false);
		ni_gpio_bit(pin, 0x100, ni_pins[i].dout);
		ni_gpio_bit(pin, 0, ni_pins[i].dir);
		ni_mask(ni_gpio, 0x300 + pin / 8 * 16, 15u << shift, ni_pins[i].mux << shift);
	}
	ni_mask(ni_lt, 0x40, 3u << 15, ni_ies_lt);
	ni_mask(ni_tm, 0x50, 3u << 12, ni_ies_tm);
	ni_mask(ni_tm, 0x60, (1u << 3) | (1u << 6), ni_ies_pwr);
	ni_mask(ni_rt, 0x30, 1u << 24, ni_ies_rt);
	ni_saved = false;
	return idle && power_ok ? 1 : -1;
}

static void
ni_thread(void *arg, wait_result_t result)
{
	(void)arg; (void)result;
	ni_wait(2000);
	ni_gpio = ml_io_map(0x10005000, 0x1000);
	ni_lt = ml_io_map(0x11f20000, 0x1000);
	ni_tm = ml_io_map(0x11f30000, 0x1000);
	ni_rt = ml_io_map(0x11ea0000, 0x1000);
	ni_pwrap = ml_io_map(0x10026000, 0x1000);
	if (!ni_gpio || !ni_lt || !ni_tm || !ni_rt || !ni_pwrap) {
		panic("NORD2 INPUT: mapping failed");
	}
	for (unsigned i = 0; i < sizeof(ni_pins) / sizeof(ni_pins[0]); i++) {
		unsigned pin = ni_pins[i].pin;
		ni_pins[i].mux = (ni_rd(ni_gpio, 0x300 + pin / 8 * 16) >> (pin % 8 * 4)) & 15;
		ni_pins[i].dir = (ni_rd(ni_gpio, pin / 32 * 16) >> (pin % 32)) & 1;
		ni_pins[i].dout = (ni_rd(ni_gpio, 0x100 + pin / 32 * 16) >> (pin % 32)) & 1;
		printf("NORD2 INPUT: pin=%u mux=%u dir=%u out=%u level=%d\n",
		    pin, ni_pins[i].mux, ni_pins[i].dir, ni_pins[i].dout, ni_level(pin));
	}
	ni_ies_lt = ni_rd(ni_lt, 0x40);
	ni_ies_tm = ni_rd(ni_tm, 0x50);
	ni_ies_pwr = ni_rd(ni_tm, 0x60);
	ni_ies_rt = ni_rd(ni_rt, 0x30);
	ni_saved = true; /* snapshots complete before first mutation */
	ni_wifi_snapshot(); /* PMIF cleanup is now armed, before any GPIO change. */
	ni_wifi_power_probe();
	for (unsigned i = 0; i < sizeof(ni_pins) / sizeof(ni_pins[0]); i++) {
		unsigned pin = ni_pins[i].pin;
		ni_gpio_bit(pin, 0, false);
		if (pin == NI_SDA || pin == NI_SCL || pin == NI_PSCL || pin == NI_PSDA) {
			ni_gpio_bit(pin, 0x100, false);
		}
		ni_mask(ni_gpio, 0x300 + pin / 8 * 16, 15u << (pin % 8 * 4), 0);
	}
	ni_mask(ni_lt, 0x40, 3u << 15, 3u << 15);
	ni_mask(ni_tm, 0x50, 3u << 12, 3u << 12);
	ni_mask(ni_tm, 0x60, (1u << 3) | (1u << 6), (1u << 3) | (1u << 6));
	ni_mask(ni_rt, 0x30, 1u << 24, 1u << 24);
	ni_bus_owned = true;
	/* Keep reset asserted while checking/enabling only its dedicated rail. */
	ni_gpio_bit(NI_RESET, 0x100, false);
	ni_gpio_bit(NI_RESET, 0, true);
	bool powered = ni_power_on();
	ni_bus(false);
	ni_wait(10);
	ni_gpio_bit(NI_RESET, 0x100, true);
	ni_wait(200);
	uint8_t a = 0, b = 0;
	bool bus_idle = ni_level(NI_SCL) && ni_level(NI_SDA);
	bool touch_ok = bus_idle && ni_read(0xa3, &a, 1) && ni_read(0x9f, &b, 1) &&
	    a == 0x54 && b == 0x52;
	uint16_t top = 0;
	bool key_ok = ni_keys_read(&top);
	printf("NORD2 INPUT: FT3518 id=%02x:%02x ready=%d powered=%d idle=%d stage=%u irq=%d; TOPSTATUS=%#x ok=%d\n",
	    a, b, touch_ok, powered, bus_idle, ni_stage, ni_level(NI_IRQ), top, key_ok);
	printf("NORD2 INPUT: software I2C0, x4 coordinates, PMIC power/up and GPIO155 down; poll=10ms\n");
	unsigned errors = 0, ticks = 0, key_errors = 0;
	int state[3] = {0}, previous[3] = {-1, -1, -1};
	for (;;) {
		key_ok = ni_keys_read(&top);
		int now[3] = {!(top & 2), !(top & 8), !ni_level(NI_VOLDN)};
		if (!key_ok && (key_errors++ % 500) == 0) {
			printf("NORD2 INPUT: PWRAP read failed status=%#x\n", ni_rd(ni_pwrap, 0xca8));
		}
		if (key_ok) { key_errors = 0; }
		for (unsigned k = 0; k < 3; k++) {
			if (k < 2 && !key_ok) {
				previous[k] = -1;
				if (key_errors >= 3 && state[k]) {
					state[k] = 0;
					sweet_hid_key((int)k, 0);
				}
				continue;
			}
			if (previous[k] == now[k] && state[k] != now[k]) {
				state[k] = now[k];
				printf("NORD2 INPUT: key=%u down=%d top=%#x\n", k, state[k], top);
				sweet_hid_key((int)k, state[k]);
			}
			previous[k] = now[k];
		}
		if (touch_ok && (ni_active || !ni_level(NI_IRQ) || (++ticks % 10) == 0)) {
			if (!ni_touch()) {
				if ((errors++ % 100) == 0) {
					printf("NORD2 INPUT: touch report error irq=%d scl=%d sda=%d\n",
					    ni_level(NI_IRQ), ni_level(NI_SCL), ni_level(NI_SDA));
				}
				if (errors >= 3) { ni_report(0, ni_x, ni_y); }
			} else { errors = 0; }
		}
		ni_wait(10);
	}
}

void
nord2_input_init(void)
{
	int enabled = 0, stop = 0;
	thread_t thread;
	PE_parse_boot_argn("nord2input", &enabled, sizeof(enabled));
	PE_parse_boot_argn("nord2stopat", &stop, sizeof(stop));
	if (ni_started || enabled != 1 || stop != 23 || !nord2_smp_userland_ready()) { return; }
	ni_started = true;
	kern_return_t kr = kernel_thread_start(ni_thread, NULL, &thread);
	if (kr == KERN_SUCCESS) { thread_deallocate(thread); }
	else { printf("NORD2 INPUT: thread start failed %d\n", kr); }
}
#else /* original Sweet hardware */
/*
 * SWEET SE7 FIFO I2C / GT9896 Yellowstone / Focaltech 54:52 bring-up.
 * Call once after the scheduler is running; all work runs on one thread.
 * Requires ABL-loaded GENI I2C firmware; votes L11C through sweet_rpmh.c.
 * No firmware/config upload or direct PMIC writes. Only IC data write:
 * zero to GT9896's event status after reading a report. See touch2/report.md.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
/* Mach MIG declaration is not included by BSD's exported thread.h. */
extern kern_return_t thread_terminate(thread_t);
extern int sweet_rpmh_vreg_enable(const char *res, unsigned mv);
extern void sweet_hid_touch(int finger, int down, int x, int y);
void sweet_touch_init(void);

#define TOUCH_SE_PA      0x00a84000u
#define TOUCH_GCC_PA     0x00100000u
#define TOUCH_NORTH_PA   0x03900000u /* DT TLMM + NORTH (0x500000) */
#define TOUCH_GCC_VOTE   0x5200cu
#define TOUCH_RCG       0x18148u    /* wrapper1 LOCAL S1 = global SE7 */
#define TOUCH_GPIO_CFG(n) ((uint32_t)(n) * 0x1000u)
#define TOUCH_GPIO_IO(n)  (TOUCH_GPIO_CFG(n) + 4u)
#define TOUCH_MAX        10u
#define TOUCH_GOODIX     1
#define TOUCH_FTS        2
/* GT9896 is Yellowstone in Xiaomi sweet-r-oss, not the Berlin protocol. */
#define GOODIX_PID_REG   0x4022u
#define GOODIX_EVENT_REG 0x4180u
#define GOODIX_HEAD_LEN  8u
#define GOODIX_POINT_LEN 8u

#define SE_STATUS       0x040u
#define SE_MCLK         0x048u
#define SE_FW           0x068u
#define SE_CLK_SEL      0x07cu
#define SE_DMA          0x258u
#define SE_TX_LEN       0x26cu
#define SE_RX_LEN       0x270u
#define SE_SCL          0x278u
#define SE_CMD          0x600u
#define SE_CTRL         0x604u
#define SE_IRQ          0x610u
#define SE_IRQ_EN       0x614u
#define SE_IRQ_CLR      0x618u
#define SE_TX           0x700u
#define SE_RX           0x780u
#define SE_TX_COUNT     0x800u
#define SE_RX_COUNT     0x804u
#define SE_HW_TX        0xe24u
#define SE_HW_RX        0xe28u
#define SE_TX_WM        0x80cu
#define SE_RX_WM_IRQ    (1u << 26)
#define SE_RX_LAST      (1u << 27)
#define SE_TX_WM_IRQ    (1u << 30)
#define SE_DONE         (1u << 0)
#define SE_NACK         (1u << 10)
#define SE_ERRORS       (0x3eu | (1u << 9) | (1u << 10) | (1u << 12) | \
                         (1u << 13) | (3u << 24) | (3u << 28))
#define SE_EVENTS       (SE_DONE | SE_ERRORS | SE_RX_WM_IRQ | SE_RX_LAST | SE_TX_WM_IRQ)
/* geni_se_config_packing(8, 4, true, true, true): start bits 7/15/23/31,
 * MSB-to-LSB, length 8, STOP only on vector 3. FIFO bytes remain LSB first. */
#define SE_PACK0        0x0007f8feu
#define SE_PACK1        0x000ffefeu

static struct {
	vm_offset_t se, gcc, gpio;
	bool started, dead, probing;
	int kind, irq_hint;
	uint32_t irq_status, irq_last, active;
	unsigned sent, got;
	int x[TOUCH_MAX], y[TOUCH_MAX];
} st;
static int touch_sleep_chan;

static uint32_t
touch_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
touch_wr(vm_offset_t base, uint32_t off, uint32_t val)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = val;
	__asm__ volatile("dsb osh" ::: "memory");
}

static void
touch_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&touch_sleep_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static bool
touch_wait_clear(vm_offset_t base, uint32_t off, uint32_t mask)
{
	for (unsigned i = 0; i < 5000; ++i) {
		if (!(touch_rd(base, off) & mask)) {
			return true;
		}
		delay(1);
	}
	return false;
}

static void
touch_gpio(unsigned pin, unsigned mux, unsigned pull, unsigned drive, bool out, bool high)
{
	uint32_t off = TOUCH_GPIO_CFG(pin);
	uint32_t cfg = touch_rd(st.gpio, off);
	if (out) {
		/* Set the desired latch before enabling output (reset is active low). */
		touch_wr(st.gpio, TOUCH_GPIO_IO(pin),
		    (touch_rd(st.gpio, TOUCH_GPIO_IO(pin)) & ~2u) | (high ? 2u : 0u));
	}
	cfg &= ~0x3ffu;
	cfg |= (mux << 2) | pull | (drive << 6) | (out ? 0x200u : 0u);
	touch_wr(st.gpio, off, cfg);
}

static bool
touch_clocks(void)
{
	/* Vote on only wrapper1 and SE7, preserving other clock users. */
	uint32_t mask = (0x0fu << 18) | (1u << 23);
	touch_wr(st.gcc, TOUCH_GCC_VOTE, touch_rd(st.gcc, TOUCH_GCC_VOTE) | mask);
	uint32_t dfs = touch_rd(st.gcc, TOUCH_RCG + 0x14u);
	uint32_t cfg = touch_rd(st.gcc, TOUCH_RCG + 4u);
	printf("SWEET TOUCH: gcc vote=%#x rcg=%#x cfg=%#x dfs=%#x\n",
	    touch_rd(st.gcc, TOUCH_GCC_VOTE), touch_rd(st.gcc, TOUCH_RCG), cfg, dfs);
	if (dfs & 1u) {
		/* SE CLK_SEL=0 selects DFS performance level 0. Do not alter tables. */
		cfg = touch_rd(st.gcc, TOUCH_RCG + 0x1cu);
		if ((cfg & 0x371fu) != 1u) {
			printf("SWEET TOUCH: DFS level0=%#x is not XO/1; need clock integration\n", cfg);
			return false;
		}
	} else {
		/* XO parent 0, HID divider 1 => 19.2 MHz. No PLL or voltage change. */
		cfg = (cfg & ~(0x371fu | (1u << 20))) | 1u;
		touch_wr(st.gcc, TOUCH_RCG + 4u, cfg);
		touch_wr(st.gcc, TOUCH_RCG, touch_rd(st.gcc, TOUCH_RCG) | 1u);
		if (!touch_wait_clear(st.gcc, TOUCH_RCG, 1u)) {
			printf("SWEET TOUCH: RCG update timeout\n");
			return false;
		}
	}
	const uint32_t branches[] = {0x18004, 0x18008, 0x1800c, 0x18010, 0x18144};
	for (unsigned i = 0; i < sizeof(branches) / sizeof(branches[0]); ++i) {
		if (!touch_wait_clear(st.gcc, branches[i], 1u << 31)) {
			printf("SWEET TOUCH: GCC branch %#x halted (%#x)\n", branches[i],
			    touch_rd(st.gcc, branches[i]));
			return false;
		}
	}
	return true;
}

/* Called only for a transfer started by this driver, including a stretched TX. */
static void
touch_abort(void)
{
	touch_wr(st.se, SE_CTRL, 2); /* abort M command, release held bus */
	bool done = false;
	for (unsigned i = 0; i < 5000; ++i) {
		if (touch_rd(st.se, SE_IRQ) & (1u << 5)) {
			done = true;
			break;
		}
		delay(1);
	}
	if (!done || !touch_wait_clear(st.se, SE_STATUS, 1u)) {
		st.dead = true;
		printf("SWEET TOUCH: abort failed status=%#x irq=%#x; stopping\n",
		    touch_rd(st.se, SE_STATUS), touch_rd(st.se, SE_IRQ));
	}
	/* Bound drain even if the hardware reports an impossible FIFO count. */
	for (unsigned i = 0; i < 64 && (touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu); ++i) {
		(void)touch_rd(st.se, SE_RX);
	}
	touch_wr(st.se, SE_IRQ_CLR, 0xffffffffu);
	if (touch_rd(st.se, SE_TX_COUNT) & 0xfffffffu) {
		st.dead = true;
		printf("SWEET TOUCH: TX FIFO not empty after abort; stopping\n");
	}
}

/* Keep all observed bits for diagnostics, even if a watermark was cleared. */
static uint32_t
touch_irq(void)
{
	st.irq_last = touch_rd(st.se, SE_IRQ);
	st.irq_status |= st.irq_last;
	return st.irq_last;
}

/* One Linux-style FIFO message. TX is at most 3 bytes; RX at most 90. */
static int
touch_msg_run(uint8_t addr, const uint8_t *tx, unsigned nt, uint8_t *rx,
    unsigned nr, bool stretch)
{
	if (st.dead || nt > 3 || nr > 90 || (nt && nr) || (!nt && !nr)) {
		return EIO;
	}
	if (!touch_wait_clear(st.se, SE_STATUS, 1u)) {
		st.dead = true; /* no other SE7 owner is supported */
		return EBUSY;
	}
	touch_wr(st.se, SE_IRQ_CLR, 0xffffffffu);
	touch_wr(st.se, SE_TX_WM, nt ? 1u : 0u);
	touch_wr(st.se, nr ? SE_RX_LEN : SE_TX_LEN, nr ? nr : nt);
	touch_wr(st.se, SE_CMD, ((nr ? 2u : 1u) << 27) |
	    ((uint32_t)addr << 9) | (stretch ? 4u : 0u));
	int err = ETIMEDOUT;
	for (unsigned i = 0; i < 20000; ++i) {
		uint32_t irq = touch_irq();
		if (irq & SE_ERRORS) {
			err = (irq & SE_NACK) ? ENXIO : EIO;
			/* A completed NACK is normal during address probing. Aborting an
			 * idle sequencer may never produce ABORT_DONE. Wait for completion
			 * and leave a clean idle engine available for the other IC. */
			for (unsigned j = 0; j < 5000 && !(irq & SE_DONE); ++j) {
				delay(1);
				irq = touch_irq();
			}
			if ((irq & SE_DONE) && !(touch_rd(st.se, SE_STATUS) & 1u) &&
			    !(touch_rd(st.se, SE_TX_COUNT) & 0xfffffffu) &&
			    !(touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu)) {
				touch_wr(st.se, SE_IRQ_CLR, irq);
				return err;
			}
			break;
		}
		if (nt && !st.sent && (irq & SE_TX_WM_IRQ)) {
			uint32_t word = 0;
			for (unsigned j = 0; j < nt; ++j) {
				word |= (uint32_t)tx[j] << (8u * j);
			}
			touch_wr(st.se, SE_TX, word);
			st.sent = nt;
			touch_wr(st.se, SE_TX_WM, 0);
		}
		uint32_t count = touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu;
		if (count > 64 || count > (nr - st.got + 3u) / 4u) {
			err = EIO;
			break;
		}
		while (count--) {
			uint32_t word = touch_rd(st.se, SE_RX);
			for (unsigned j = 0; j < 4 && st.got < nr; ++j) {
				rx[st.got++] = (uint8_t)(word >> (8u * j));
			}
		}
		/* DONE is checked after draining RX, so final short words aren't lost. */
		if (irq & SE_DONE) {
			/* Re-sample after the final drain; a late error must beat DONE. */
			irq = touch_irq();
			if (irq & SE_ERRORS) {
				continue;
			}
			touch_wr(st.se, SE_IRQ_CLR, irq);
			if (st.got == nr && st.sent == nt) {
				return 0;
			}
			err = EIO;
			break;
		}
		/* Preserve DONE/errors if they arrive during this iteration. */
		touch_wr(st.se, SE_IRQ_CLR, irq & (SE_RX_WM_IRQ | SE_RX_LAST | SE_TX_WM_IRQ));
		delay(1);
	}
	touch_abort();
	return err;
}

static int
touch_msg(uint8_t addr, const uint8_t *tx, unsigned nt, uint8_t *rx,
    unsigned nr, bool stretch)
{
	st.irq_status = st.irq_last = 0;
	st.sent = st.got = 0;
	int err = touch_msg_run(addr, tx, nt, rx, nr, stretch);
	if (st.probing) {
		printf("SWEET TOUCH: probe xfer addr=%#x %s len=%u stretch=%u err=%d irq=%#x seen=%#x tx=%u rx=%u status=%#x\n",
		    addr, nr ? "read" : "write", nr ? nr : nt, stretch ? 1u : 0u,
		    err, st.irq_last, st.irq_status, st.sent, st.got,
		    touch_rd(st.se, SE_STATUS));
	}
	return err;
}

static int
touch_read(uint8_t addr, uint16_t reg, bool wide, uint8_t *buf, unsigned len)
{
	uint8_t ptr[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
	int err = touch_msg(addr, wide ? ptr : ptr + 1, wide ? 2u : 1u,
	    NULL, 0, true);
	if (err) {
		return err;
	}
	/* STOP_STRETCH on the pointer write retains the bus for a repeated START. */
	return touch_msg(addr, NULL, 0, buf, len, false);
}

static int
touch_goodix_ack(void)
{
	const uint8_t ack[3] = {GOODIX_EVENT_REG >> 8, GOODIX_EVENT_REG & 0xffu, 0};
	return touch_msg(0x5d, ack, sizeof(ack), NULL, 0, false);
}

/* Raw read only: 0x8240 is the vendor's generic/older VERSION candidate,
 * not a second accepted GT9896 identity. Never try a four-byte Berlin pointer:
 * on this 16-bit IC the extra bytes would be register data writes. */
static int
touch_probe_info(uint16_t reg, uint8_t *buf)
{
	memset(buf, 0xa5, 32);
	int err = touch_read(0x5d, reg, true, buf, 32);
	printf("SWEET TOUCH: probe info reg=%#x len=32 err=%d irq=%#x seen=%#x\n",
	    reg, err, st.irq_last, st.irq_status);
	if (!err) {
		for (unsigned off = 0; off < 32; off += 16) {
			char line[49];
			for (unsigned j = 0; j < 16; ++j) {
				(void)snprintf(line + j * 3u, 4, "%02x ", buf[off + j]);
			}
			printf("SWEET TOUCH: probe raw %#x: %s\n", reg + off, line);
		}
	}
	return err;
}

static unsigned
touch_be16(const uint8_t *p)
{
	return ((unsigned)p[0] << 8) | p[1];
}

static bool
touch_checksum(const uint8_t *buf, unsigned len)
{
	if (len < 2) {
		return false;
	}
	unsigned sum = 0;
	for (unsigned i = 0; i + 2 < len; ++i) {
		sum += buf[i];
	}
	return (sum & 0xffffu) == touch_be16(buf + len - 2);
}

static void
touch_report(uint32_t mask, const int *x, const int *y)
{
	for (unsigned id = 0; id < TOUCH_MAX; ++id) {
		uint32_t bit = 1u << id;
		if (mask & bit) {
			if (!(st.active & bit) || x[id] != st.x[id] || y[id] != st.y[id]) {
				sweet_hid_touch((int)id, 1, x[id], y[id]);
			}
			st.x[id] = x[id];
			st.y[id] = y[id];
		} else if (st.active & bit) {
			sweet_hid_touch((int)id, 0, st.x[id], st.y[id]);
		}
	}
	st.active = mask;
}

static int
touch_goodix(void)
{
	uint8_t buf[90];
	int x[TOUCH_MAX] = {0}, y[TOUCH_MAX] = {0};
	uint32_t mask = 0;
	int err = touch_read(0x5d, GOODIX_EVENT_REG, true, buf, GOODIX_HEAD_LEN);
	if (err) {
		return err;
	}
	if (!(buf[0] & 0x80u)) {
		/* Do not acknowledge firmware/config requests that we cannot service. */
		if (buf[0] & 0x40u) {
			return ENOTSUP;
		}
		return 0; /* no new touch frame: retain current contacts */
	}
	if (!touch_checksum(buf, GOODIX_HEAD_LEN)) {
		return EIO; /* header untrusted: no write to the IC */
	}
	unsigned n = buf[2] & 15u;
	if (n > TOUCH_MAX) {
		(void)touch_goodix_ack();
		return EIO;
	}
	if (n) {
		err = touch_read(0x5d, GOODIX_EVENT_REG + GOODIX_HEAD_LEN, true,
		    buf + GOODIX_HEAD_LEN, n * GOODIX_POINT_LEN + 2u);
		if (err) {
			return err;
		}
		if (!touch_checksum(buf + GOODIX_HEAD_LEN, n * GOODIX_POINT_LEN + 2u)) {
			(void)touch_goodix_ack();
			return EIO;
		}
	}
	for (unsigned i = 0; i < n; ++i) {
		const uint8_t *p = buf + GOODIX_HEAD_LEN + i * GOODIX_POINT_LEN;
		unsigned id = p[0] >> 4;
		unsigned px = touch_be16(p + 2), py = touch_be16(p + 4);
		/* Low nibble 1/3 denotes pen data in the vendor parser. */
		if ((p[0] & 15u) == 1u || (p[0] & 15u) == 3u || id >= TOUCH_MAX ||
		    (mask & (1u << id)) || px > 1079 || py > 2399) {
			(void)touch_goodix_ack();
			return EIO;
		}
		mask |= 1u << id;
		x[id] = (int)px;
		y[id] = (int)py;
	}
	err = touch_goodix_ack();
	if (!err) {
		touch_report(mask, x, y);
	}
	return err;
}

static int
touch_focaltech(void)
{
	uint8_t buf[63];
	int x[TOUCH_MAX] = {0}, y[TOUCH_MAX] = {0};
	uint32_t mask = 0, seen = 0;
	int err = touch_read(0x38, 0, false, buf, sizeof(buf));
	if (err) {
		return err;
	}
	unsigned n = buf[2] & 15u, active = 0;
	if ((buf[0] & 0x70u) || n > TOUCH_MAX) {
		return EIO;
	}
	for (unsigned i = 0; i < TOUCH_MAX; ++i) {
		const uint8_t *p = buf + 3u + 6u * i;
		unsigned id = p[2] >> 4, event = p[0] >> 6;
		if (id >= TOUCH_MAX) {
			break; /* vendor's unused-record sentinel */
		}
		if (seen & (1u << id)) {
			return EIO;
		}
		seen |= 1u << id;
		if (event == 1u) {
			continue; /* UP record */
		}
		if (event == 3u) {
			return EIO;
		}
		unsigned px = ((unsigned)(p[0] & 15u) << 8) | p[1];
		unsigned py = ((unsigned)(p[2] & 15u) << 8) | p[3];
		if (px > 1079 || py > 2399) {
			return EIO;
		}
		mask |= 1u << id;
		x[id] = (int)px;
		y[id] = (int)py;
		++active;
	}
	if (active != n) {
		return EIO;
	}
	touch_report(mask, x, y);
	return 0;
}

static bool
touch_se_init(void)
{
	uint32_t fw = touch_rd(st.se, SE_FW);
	uint32_t tx = touch_rd(st.se, SE_HW_TX), rx = touch_rd(st.se, SE_HW_RX);
	printf("SWEET TOUCH: se7 fw=%#x proto=%u status=%#x tx=%#x rx=%#x ios=%#x\n",
	    fw, (fw >> 8) & 0xffu, touch_rd(st.se, SE_STATUS), tx, rx,
	    touch_rd(st.se, 0x908));
	if (((fw >> 8) & 0xffu) != 3u || (touch_rd(st.se, 0x64) & 1u) ||
	    ((tx >> 24) & 0x3fu) != 32u || ((rx >> 24) & 0x3fu) != 32u ||
	    ((tx >> 16) & 0x3fu) < 2u || ((rx >> 16) & 0x3fu) < 2u) {
		printf("SWEET TOUCH: need ABL-loaded I2C firmware and 32-bit FIFOs; probes skipped\n");
		return false;
	}
	if (touch_rd(st.se, SE_STATUS) & 0x1001u) {
		printf("SWEET TOUCH: SE7 already busy; probes skipped\n");
		return false;
	}
	touch_wr(st.se, 0xe18, 0); /* no GSI events */
	touch_wr(st.se, 0xe1c, 0); /* mask external IRQ routing; poll local status */
	touch_wr(st.se, 0x644, 0);
	touch_wr(st.se, SE_IRQ_CLR, 0xffffffffu);
	touch_wr(st.se, 0x648, 0xffffffffu);
	touch_wr(st.se, SE_DMA, 0);
	touch_wr(st.se, 0x28, touch_rd(st.se, 0x28) | 0x7fu);
	touch_wr(st.se, 0xe30, touch_rd(st.se, 0xe30) | 0x0fu);
	touch_wr(st.se, 0x24, 0x7f);
	touch_wr(st.se, 0x20, 1);
	touch_wr(st.se, SE_CLK_SEL, 0);
	touch_wr(st.se, SE_MCLK, (7u << 4) | 1u);
	touch_wr(st.se, SE_SCL, (10u << 20) | (11u << 10) | 26u); /* 100 kHz */
	/* Linux 8-bit I2C packing: MSB on the wire, LSB byte first in FIFO. */
	touch_wr(st.se, 0x254, 0);
	touch_wr(st.se, 0x260, SE_PACK0);
	touch_wr(st.se, 0x264, SE_PACK1);
	touch_wr(st.se, 0x284, SE_PACK0);
	touch_wr(st.se, 0x288, SE_PACK1);
	touch_wr(st.se, SE_TX_WM, 0);
	touch_wr(st.se, 0x810, 0);
	touch_wr(st.se, 0x814, ((rx >> 16) & 0x3fu) - 1u);
	touch_wr(st.se, SE_IRQ_EN, SE_EVENTS);
	printf("SWEET TOUCH: packing tx=%#x/%#x rx=%#x/%#x gran=%#x\n",
	    touch_rd(st.se, 0x260), touch_rd(st.se, 0x264),
	    touch_rd(st.se, 0x284), touch_rd(st.se, 0x288), touch_rd(st.se, 0x254));
	if (touch_rd(st.se, 0x260) != SE_PACK0 || touch_rd(st.se, 0x264) != SE_PACK1 ||
	    touch_rd(st.se, 0x284) != SE_PACK0 || touch_rd(st.se, 0x288) != SE_PACK1 ||
	    touch_rd(st.se, 0x254) != 0) {
		printf("SWEET TOUCH: packing readback mismatch; probes skipped\n");
		return false;
	}
	if ((touch_rd(st.se, SE_TX_COUNT) & 0xfffffffu) ||
	    (touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu)) {
		printf("SWEET TOUCH: inherited FIFO data; probes skipped\n");
		return false;
	}
	return true;
}

static void
touch_thread(void *arg, wait_result_t result)
{
	unsigned delay_s = 45;
	(void)arg;
	(void)result;
	/*
	 * Start late (boot-arg sweettouchdelay, seconds): if the bus access
	 * below hangs the SoC, the persistent log and the GUI got there first.
	 */
	(void)PE_parse_boot_argn("sweettouchdelay", &delay_s, sizeof(delay_s));
	printf("SWEET TOUCH: starting in %u s\n", delay_s);
	touch_wait(delay_s * 1000u);
	printf("SWEET TOUCH: mapping GCC/TLMM/SE7\n");
	st.gcc = ml_io_map(TOUCH_GCC_PA, 0x60000);
	st.gpio = ml_io_map(TOUCH_NORTH_PA, 0x5b000);
	st.se = ml_io_map(TOUCH_SE_PA, 0x4000);
	if (!st.gcc || !st.gpio || !st.se) {
		printf("SWEET TOUCH: MMIO mapping failed\n");
		goto out;
	}
	if (!touch_clocks() || !touch_se_init()) {
		goto out;
	}
	printf("SWEET TOUCH: TLMM reset=%#x irq=%#x vdd90=%#x; voting L11C through RPMh\n",
	    touch_rd(st.gpio, TOUCH_GPIO_IO(8)), touch_rd(st.gpio, TOUCH_GPIO_IO(9)),
	    touch_rd(st.gpio, TOUCH_GPIO_IO(90)));
	/* Keep the IC in reset while its rails settle; IRQ stays an input. */
	touch_gpio(9, 0, 3, 3, false, false); /* IRQ input, 8 mA, pull-up */
	touch_gpio(8, 0, 3, 3, true, false);
	int power_error = sweet_rpmh_vreg_enable("ldoc11", 3200);
	if (power_error) {
		printf("SWEET TOUCH: L11C RPMh enable failed %d; probes skipped\n", power_error);
		goto out;
	}
	touch_gpio(90, 0, 0, 0, true, true); /* board digital power enable */
	touch_wait(10); /* conservative rail settling; not a DT timing value */
	touch_gpio(6, 1, 0, 0, false, false); /* qup11 SDA/SCL, 2 mA */
	touch_gpio(7, 1, 0, 0, false, false);
	/* Vendor GT9896 dev_confirm: reset low 2 ms, then high. */
	touch_gpio(8, 0, 3, 3, true, false);
	touch_wait(2);
	touch_gpio(8, 0, 3, 3, true, true);
	touch_wait(300); /* exceeds vendor 5 ms test + 95 ms post-test wait */
	uint8_t gid[32], legacy[32], fid1 = 0, fid2 = 0;
	st.probing = true;
	int ge = touch_probe_info(GOODIX_PID_REG, gid);
	printf("SWEET TOUCH: probe 0x5d err=%d pid=%02x%02x%02x%02x fw=%02x.%02x.%02x.%02x irq=%#x\n",
	    ge, gid[0], gid[1], gid[2], gid[3], gid[8], gid[9], gid[10], gid[11], st.irq_status);
	(void)touch_probe_info(0x8240, legacy);
	int fe1 = touch_read(0x38, 0xa3, false, &fid1, 1);
	int fe2 = touch_read(0x38, 0x9f, false, &fid2, 1);
	printf("SWEET TOUCH: probe 0x38 err=%d/%d chip-id=%02x:%02x irq=%#x\n",
	    fe1, fe2, fid1, fid2, st.irq_status);
	st.probing = false;
	bool goodix = !ge && gid[0] == '9' && gid[1] == '8' && gid[2] == '9' && gid[3] == '6';
	bool fts = !fe1 && !fe2 && fid1 == 0x54 && fid2 == 0x52;
	if (st.dead || goodix == fts) {
		printf("SWEET TOUCH: no unique supported IC (goodix=%d fts=%d dead=%d); stopping\n",
		    goodix, fts, st.dead);
		goto out;
	}
	st.kind = goodix ? TOUCH_GOODIX : TOUCH_FTS;
	printf("SWEET TOUCH: selected %s, 1080x2400, poll=10ms irq-hint=%d\n",
	    goodix ? "GT9896 Yellowstone" : "Focaltech 54:52", st.irq_hint);
	unsigned errors = 0, ticks = 0;
	while (!st.dead) {
		/* Optional IRQ hint still reads every 100ms, and always while down. */
		if (!st.irq_hint || st.active || !(touch_rd(st.gpio, TOUCH_GPIO_IO(9)) & 1u) ||
		    (++ticks % 10u) == 0) {
			int err = st.kind == TOUCH_GOODIX ? touch_goodix() : touch_focaltech();
			if (err) {
				if ((errors++ % 100u) == 0) {
					printf("SWEET TOUCH: report err=%d irq=%#x\n", err, st.irq_status);
				}
				if (errors == 10) {
					touch_report(0, st.x, st.y); /* release after persistent bad reads */
				}
			} else {
				errors = 0;
			}
		}
		touch_wait(10);
	}
	touch_report(0, st.x, st.y);
out:
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

void
sweet_touch_init(void)
{
	int enabled = 1;
	thread_t thread;
	if (st.started) {
		return;
	}
	st.started = true;
	(void)PE_parse_boot_argn("sweettouch", &enabled, sizeof(enabled));
	(void)PE_parse_boot_argn("sweettouchirq", &st.irq_hint, sizeof(st.irq_hint));
	if (!enabled) {
		printf("SWEET TOUCH: disabled (sweettouch=0)\n");
		return;
	}
	kern_return_t kr = kernel_thread_start(touch_thread, NULL, &thread);
	if (kr == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("SWEET TOUCH: kernel_thread_start failed %d\n", kr);
	}
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
#endif /* NORD2 */
