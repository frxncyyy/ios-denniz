#include <arm64/sweet_early.h>
#if defined(NORD2)
#include "wifi_common.c"
#include "wifi_common_scope.c"
#include "wifi_adie_id.c"
#include "wifi_adie_strap.c"
#include "wifi_cold_snapshot.c"
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
#include "audio_native.h"
#include "audio_call.h"
#include "nord2_input_rate.h"   /* 0407: touch sampling rate (measurement only) */

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern void sweet_hid_key(int, int);
extern void sweet_hid_touch(int, int, int, int);

enum { NI_SCL = 204, NI_SDA = 205, NI_RESET = 20, NI_IRQ = 21, NI_VOLDN = 155, NI_PSCL = 202, NI_PSDA = 203 };
enum { NS_SCL = 120, NS_SDA = 121 }; /* I2C9: alert-slider hall sensors (nord2_slider.inc) */
static unsigned ni_scl = NI_SCL, ni_sda = NI_SDA;
static vm_offset_t ni_gpio, ni_lt, ni_tm, ni_rt, ni_br, ni_pwrap;
static bool ni_started, ni_saved, ni_bus_owned, ni_pwrap_owned;
static int ni_chan;
static uint32_t ni_active;
static int ni_x[10], ni_y[10];
static unsigned ni_events;
static struct n2ir ni_rate;      /* 0407: input thread only */
static uint32_t ni_moves;        /* 0407: HID events of the last ni_report */
/*
 * 0620 (perf-20261114): touch sampling.  The FT3518 is read over bit-banged I2C0 by this thread: 62 bytes with 3 x delay(4) per bit
 * = ~7.4 ms of spinning per report, then ni_wait(10) -> one report every ~18.7 ms = ~53 Hz while the panel shows 89 Hz (capture
 * perf1a: INPUT RATE gap_avg 18.5-18.7 ms, read_avg 7.3-7.4 ms, gaps to 36 ms): a finger-driven scroll moves the content on some
 * frames and not on others (judder) and every report is up to ~19 ms old.  With 0620, while a finger is down and only for the touch
 * reads: half-bit delay n2tp_half_us (default 2 us = ~2x faster; the FT3518 is a 400 kHz part and SCL stretching is still honoured),
 * and the loop period is n2tp_period_us measured from the start of the iteration (default 8000 us = 125 Hz) instead of 10 ms after
 * the work.  Every other bus (MT6360 torch, AW8697 haptics, slider) keeps delay(4).  Safety: 8 touch read errors in fast mode put the
 * half-bit delay back to 4 us for the boot ('NORD2 INPUT PACE: ... back to 4 us').
 * Kill switch: boot-arg n2gdv +2048 (n2gdv=2057) or n2tp=0 = the old timing; sysctl kern.n2tpace 0/1, kern.n2tp_half_us 1..4,
 * kern.n2tp_period_us 4000..10000 (run time).  Line 'NORD2 INPUT PACE:' at start and next to every 'NORD2 INPUT RATE:'.
 */
static uint32_t n2tp_on = 1, n2tp_half_us = 2, n2tp_period_us = 8000, n2tp_fast_errors, n2tp_fallbacks;
static volatile uint32_t ni_half_us = 4;      /* half-bit delay of ni_line (input thread only) */

static uint64_t
ni_now_us(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000;
}
static struct {
	unsigned pin, mux, dir, dout;
} ni_pins[] = {{NI_SCL,0,0,0}, {NI_SDA,0,0,0}, {NI_RESET,0,0,0},
              {NI_IRQ,0,0,0}, {NI_VOLDN,0,0,0}, {NI_PSCL,0,0,0}, {NI_PSDA,0,0,0},
              {NS_SCL,0,0,0}, {NS_SDA,0,0,0}};
static uint32_t ni_ies_lt, ni_ies_tm, ni_ies_rt, ni_ies_pwr, ni_ies_br;
#define NI_BR_IES_I2C9 ((1u << 4) | (1u << 14)) /* iocfg_br+0x70: GPIO120/121 IES (pinctrl-mt6885.c) */
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
	delay(ni_half_us);      /* 0620: 4 us, or n2tp_half_us during a touch read */
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
			ni_moves++;      /* 0407 */
		}
	}
	ni_active = mask;
}

static bool
ni_touch(void)
{
	uint8_t b[62], ack;
	uint64_t t0 = ni_now_us();      /* 0407 */
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
	ni_moves = 0;
	ni_report(active, x, y);
	{ uint64_t t1 = ni_now_us(); n2ir_report(&ni_rate, t1, active, ni_moves, t1 - t0); }      /* 0407 */
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
	case 0x18b4: /* shared VS2 voltage metadata, read only */
	case 0x0788: case 0x079e: /* DCXO_CW00, CW09 */
	case 0x1be2: case 0x1be6: case 0x1be8: case 0x1bee: case 0x1bf4:
	case 0x1c08: case 0x1c0c: case 0x1c0e: case 0x1c14: case 0x1c1a:
	case 0x1c1c: case 0x1c20: case 0x1c22: case 0x1c28:
	case 0x1c2e: case 0x1c32: case 0x1c34: case 0x1c3a:
	case 0x1f98: case 0x1f9c: case 0x2010: case 0x202e:
	/* 0284: VCORE (BUCK_VGPU11) and its SCP ("SSHUB") controls, read only: CON0, OP_EN, DBG0 (output VOSEL),
	 * SSHUB_CON0 (SCP enable/VOSEL), ELR0 (AP VOSEL), PMRC_CON1 (VR_SSHUB_MODE bit 2), VSRAM_OTHERS_SSHUB */
	case 0x1588: case 0x1594: case 0x15a6: case 0x15aa: case 0x15b4: case 0x01ac: case 0x1f28:
	/* 0284: LDO VIBR (the TCS3701 proximity IR LED supply on the Nord 2): CON0 (EN bit 0), MON, ANA_CON0 (VOSEL bits 8-11) */
	case 0x1d52: case 0x1d56: case 0x1fae: case 0x1c52: case 0x1c56: case 0x2014:
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

static void n2scp_pmic_snapshot(void);   /* 0284, nord2_scp.inc */
static void n2c_pmic_service(void);      /* 0284, nord2_cam.inc */
static void n2scp_pmic_service(void);

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
static bool npa_persistent_lifetime; /* PRIVATE persistent owner, never restore */
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

/* PRIVATE persistent-mode refinement: selected4.19 source sets SW_LP
 * before regulator_enable, rather than combining bits1|0. Fixed slots only;
 * same ordered PMIF and full-word readback as original helper. */
static bool
npa_pmic_source_enable(unsigned slot)
{
 if(!nw.saved||(slot!=2&&slot!=5))return false;
 for(unsigned phase=0;phase<2;phase++){
  uint16_t current=0,got=0,addr=nw_addr[slot];
  uint16_t expected=phase?2:0, next=phase?3:2;
  if(!ni_pmic_read(addr,&current))return false;
  if((current&3)!=expected || (current&~3u)!=(nw.pmic[slot]&~3u))return false;
  uint16_t want=(current&~3u)|next;
  if(!nw_wait(ni_pwrap,0xca8,0x800e,0x8000))return false;
  ni_pwrap_owned=true;
  ni_wr(ni_pwrap,0xc84,want);
  ni_wr(ni_pwrap,0xc80,(1u<<29)|addr);
  if(!nw_wait(ni_pwrap,0xca8,0x800e,0x8000))return false;
  if(!ni_pmic_read(addr,&got)||got!=want)return false;
 }
 return true;
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
	if (npa_persistent_lifetime) { return false; } /* no MMIO while retained */
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

#include "wifi_common_adapter.inc"
#include "wifi_persistent_adapter.h"
#include "wifi_persistent_bind.inc"
#include "wifi_wake_adapter.h"
#include "wifi_wake_bind.inc"
#include "wifi_wfsys_adapter.h"
#include "wifi_wfsys_bind.inc"
#include "wifi_rom_bind.inc"
#include "pool_bind.inc"
#include "wifi_main_wfdma_native.h"
#include "wifi_normal_backend_forward.inc"
static int ncd_normal_owner_window(const struct nwfn_native *);
#define NWFN_NORMAL_LEASE(n) ncd_normal_owner_window(n)
#include "wifi_main_wfdma_bind.inc"
#undef NWFN_NORMAL_LEASE
#include "wifi_vcn33_bind.inc"
#include "wifi_main_bind.inc"
#include "wifi_capability_bind.inc"
#include "wifi_normal_backend_bind.inc"
/* Same input-thread caller owns PMIF and this retained radio lifetime.
 * Fresh powered reset/master predicates are checked in adapter.c; these
 * software baselines were captured before original outer PMIC/power changes.
 * Parent-approved policy is historical CONN/CCCI remap only, no DRAMaccess.
 * Selected source clock function is26MHz; initial outer RC/voltage guards
 * and unmodified6msXOsettle remain mandatory. No static bool grants a cold
 * predicate: adapter repeatedly reads live AP/WFSYS/BGFSYS idle state. */
static int npa_source_owner_proof(void *cookie)
{
 (void)cookie;
 if(!ni_saved||!ncc_fresh_owned||!nw.saved||!nw.domain_touched||ncc_latched_dirty)return -1;
 if((nw.pwr&0x1f)!=0x12||(nw.clock&1)||(nw.mux21&0xffff0000)||(nw.mux22&0x0fffffff))return -1;
 for(unsigned i=0;i<6;i++)if(nw.pmic[i]&nw_mask[i])return -1;
 return 0;
}


static void
ni_wifi_power_probe(void)
{
	if (npa_persistent_lifetime) { panic("NORD2 WIFI PERSIST: refuse reentry; retain lifetime"); }
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
	unsigned persistent_power = 0;
	PE_parse_boot_argn("nord2wifipersist", &persistent_power, sizeof(persistent_power));
	for (unsigned i = 0; i < 6; i++) {
		if (persistent_power == 1 && (i == 2 || i == 5)) {
			if (!npa_pmic_source_enable(i)) { goto out; }
		} else if (!nw_pmic_write(i, false)) { goto out; }
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
	unsigned persistent = 0;
	PE_parse_boot_argn("nord2wifipersist", &persistent, sizeof(persistent));
	if (persistent == 1) {
		int retained = npa_persistent_window(host, conn, id,
		    npa_source_owner_proof, 0, 0x8fb00000, 0x8a000000, 3);
		if (retained != 1) { goto out; } /* no legacy diagnostic fallback */
		nvc_after_persistent();
		nawa_after_persistent(); /* explicit separate wake diagnostic only */
		nwsa_after_persistent(); /* separate CPU-held WFSYS diagnostic */
		nrpa_after_persistent();
    nppa_after_rom(); /* exact nord2wifirom=1, prepared before probe */
    nwfn_after_pool(); /* separate nord2wifimain=1; real firmware/calibration */
    ncd_schedule(); /* explicit nord2wificap=1; one deferred query after real FW_READY */
	} else {
		ncc_power_window(host, conn, id);
	}
	success = true;
out:
	printf("NORD2 WIFI: power probe stage=%u id=%#x cfg=%#x success=%d\n",
	    stage, id, cfg_id, success);
	if (npa_persistent_lifetime) {
		if (npa_native.state.fatal || npa_native.state.result.phase != 10) {
			panic("NORD2 WIFI PERSIST: incomplete retained state; manual reset");
		}
		printf("NORD2 WIFI: retained=1 restored=0 persistent source stage10\n");
		return; /* skip outer teardown ONLY explicit retained mode */
	}
	bool restored = nw_restore();
	printf("NORD2 WIFI: power probe restored=%d\n", restored);
	if (!restored) { panic("NORD2 WIFI: restoration failed; refuse Linux return"); }
}

#include "nord2_slider.inc"

static void n2t_service(void);      /* 0496, below */
static void n2t_force_off(void);
static void n2h_force_off(void);  /* 0610, nord2_haptic.inc */
static void n2h_owner_start(void);
static void n2h_service(void);

int
nord2_input_prepare_return(void)
{
	if (npa_persistent_lifetime) { return -1; } /* no reset/power/PMIF cleanup */
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
		n2t_force_off();      /* 0496: the flash LED off before the return */
		n2h_force_off();      /* 0610: the haptic motor stopped, its reset pin as found */
		ni_bus(false); (void)ni_stop();
		ni_bus(true); (void)ni_stop();
		if (ns_enabled) { ns_bus(); (void)ni_stop(); }
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
	ni_mask(ni_br, 0x70, NI_BR_IES_I2C9, ni_ies_br);
	ni_saved = false;
	return idle && power_ok ? 1 : -1;
}

#include "nord2_rtc.inc"   /* 0540: MT6359P RTC as the time of day (boot read + calendar -> RTC sync), on this owner thread */

static void
ni_thread(void *arg, wait_result_t result)
{
	(void)arg; (void)result;
	ni_pwrap = ml_io_map(0x10026000, 0x1000);   /* 0540: the same mapping, made before the 2 s wait: the RTC is read first */
	n2rtc_boot_read();                          /* 0540: before launchd needs the time (nord2_rtc_boot_time) */
	ni_wait(2000);
	ni_gpio = ml_io_map(0x10005000, 0x1000);
	ni_lt = ml_io_map(0x11f20000, 0x1000);
	ni_tm = ml_io_map(0x11f30000, 0x1000);
	ni_rt = ml_io_map(0x11ea0000, 0x1000);
	ni_br = ml_io_map(0x11d10000, 0x1000);
	PE_parse_boot_argn("nord2slider", &ns_enabled, sizeof(ns_enabled));
	if (!ni_gpio || !ni_lt || !ni_tm || !ni_rt || !ni_br || !ni_pwrap) {
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
	ni_ies_br = ni_rd(ni_br, 0x70);
	ni_saved = true; /* snapshots complete before first mutation */
	ni_wifi_snapshot(); /* PMIF cleanup is now armed, before any GPIO change. */
	nrpa_prepare_before_power(); /* sleepable UFS before radio ownership */
	ni_wifi_power_probe();
	for (unsigned i = 0; i < sizeof(ni_pins) / sizeof(ni_pins[0]); i++) {
		unsigned pin = ni_pins[i].pin;
		if ((pin == NS_SCL || pin == NS_SDA) && !ns_enabled) { continue; }
		ni_gpio_bit(pin, 0, false);
		if (pin == NI_SDA || pin == NI_SCL || pin == NI_PSCL || pin == NI_PSDA ||
		    pin == NS_SCL || pin == NS_SDA) {
			ni_gpio_bit(pin, 0x100, false);
		}
		ni_mask(ni_gpio, 0x300 + pin / 8 * 16, 15u << (pin % 8 * 4), 0);
	}
	ni_mask(ni_lt, 0x40, 3u << 15, 3u << 15);
	ni_mask(ni_tm, 0x50, 3u << 12, 3u << 12);
	ni_mask(ni_tm, 0x60, (1u << 3) | (1u << 6), (1u << 3) | (1u << 6));
	ni_mask(ni_rt, 0x30, 1u << 24, 1u << 24);
	if (ns_enabled) { ni_mask(ni_br, 0x70, NI_BR_IES_I2C9, NI_BR_IES_I2C9); }
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
	{   /* 0620 */
		uint32_t tp = 1; int gdv = 0;
		if (!PE_parse_boot_argn("n2tp", &tp, sizeof(tp)) && PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & 2048)) { tp = 0; }
		n2tp_on = tp ? 1 : 0;
		printf("NORD2 INPUT PACE: on=%u half_us=%u period_us=%u (0620; finger down only; off: n2gdv +2048 / n2tp=0 / kern.n2tpace=0)\n",
		    n2tp_on, n2tp_half_us, n2tp_period_us);
	}
	/* Publish retained maps only after this owner finishes initial GPIO writes. */
	nah_native_publish_ready();
	ns_probe(); /* alert slider: publishes the boot ring/silent state */
	n2h_owner_start(); /* 0610: the AW8697 haptic chip on the same I2C9 bus (reset, id, init, one boot pulse) */
	n2scp_pmic_snapshot(); /* 0284: VCORE/SSHUB state for the SCP thread, on the PMIF's owner thread */
	ni_bus(false);
	unsigned errors = 0, ticks = 0, key_errors = 0, slider_ticks = 0;
	int state[3] = {0}, previous[3] = {-1, -1, -1};
	for (;;) {
        uint64_t n2tp_it0 = ni_now_us();      /* 0620: start of this iteration */
        extern void nord2_gpu_owner_poll(void);
        nord2_gpu_owner_poll(); /* serialized GPU PMIC work */
        nord2_audio_owner_poll(); /* 0280: serialized MT6359 call-audio work */
        nvc_poll(); /* owner-only periodic phase sampling; no exact scan-boundary claim */
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
				if (state[k]) { nac_volume_key(k); }   /* 0280: volume keys step the in-call level (no-op outside calls) */
			}
			previous[k] = now[k];
		}
		if (touch_ok && (ni_active || !ni_level(NI_IRQ) || (++ticks % 10) == 0)) {
			uint32_t half = n2tp_on && n2tp_half_us >= 1 && n2tp_half_us < 4 ? n2tp_half_us : 4;      /* 0620 */
			ni_half_us = half;
			bool touch_read = ni_touch();
			ni_half_us = 4;
			if (!touch_read && half < 4 && ++n2tp_fast_errors >= 8) {      /* 0620: the bus does not take the fast timing */
				n2tp_half_us = 4; n2tp_fallbacks++; n2tp_fast_errors = 0;
				printf("NORD2 INPUT PACE: 8 touch read errors at half_us=%u: back to 4 us for this boot (fallbacks %u)\n", half, n2tp_fallbacks);
			}
			if (!touch_read) {
				n2ir_error(&ni_rate);      /* 0407 */
				if ((errors++ % 100) == 0) {
					printf("NORD2 INPUT: touch report error irq=%d scl=%d sda=%d\n",
					    ni_level(NI_IRQ), ni_level(NI_SCL), ni_level(NI_SDA));
				}
				if (errors >= 3) { ni_report(0, ni_x, ni_y); }
			} else { errors = 0; }
		}
		if (touch_ok) {      /* 0407: one line per 5 s window with finger activity */
			uint64_t nu = ni_now_us();
			if (ni_rate.win_start_us == 0) { n2ir_reset(&ni_rate, nu); }
			int due = n2ir_due(&ni_rate, nu);
			if (due > 0) {
				char rb[256]; (void)n2ir_format(&ni_rate, nu, rb, sizeof(rb)); printf("NORD2 INPUT RATE: %s\n", rb);
				printf("NORD2 INPUT PACE: on=%u half_us=%u period_us=%u fast_errors=%u fallbacks=%u\n", n2tp_on, n2tp_half_us, n2tp_period_us,
				    n2tp_fast_errors, n2tp_fallbacks);      /* 0620 */
			}
			if (due != 0) { n2ir_reset(&ni_rate, nu); }
		}
		if ((++slider_ticks % 10) == 0) { ns_poll(); }
		n2scp_pmic_service(); n2c_pmic_service();   /* 0284: VIBR on/off requests from the SCP thread */
		if (powered) { n2t_service(); }   /* 0496: torch requests from /dev/redwood-torch (MT6360 FLED, same I2C5 bus) */
		n2h_service();     /* 0610: iOS Actuator envelope -> AW8697 CONT drive, limits, watchdog (I2C9) */
		n2rtc_service();   /* 0540: calendar -> MT6359P RTC when the calendar moved (one deadline compare per tick) */
		if (n2tp_on && touch_ok && ni_active) {      /* 0620: a finger is down: fixed period from the start of the iteration */
			uint32_t per = n2tp_period_us < 4000 ? 4000 : n2tp_period_us > 10000 ? 10000 : n2tp_period_us;
			uint64_t el = ni_now_us() - n2tp_it0, wait_us = el + 1000 < per ? per - el : 1000;
			uint64_t deadline;
			clock_interval_to_deadline((uint32_t)wait_us, NSEC_PER_USEC, &deadline);
			(void)assert_wait_deadline(&ni_chan, THREAD_UNINT, deadline);
			thread_block(THREAD_CONTINUE_NULL);
		} else {
			ni_wait(10);
		}
	}
}

#include "nord2_scp.inc"   /* 0284: SCP sensor hub */
#include "nord2_hang.inc"  /* 0284: SpringBoard main-thread stack probe */
#include "nord2_cam.inc"   /* 0284: cameras (after HakiOS mtkcam) */
#include "nord2_hwcodec.inc"   /* 0298: hardware JPEG encoder, driven by autotest command 7 */
#include "nord2_vcodec_relay.inc"   /* 0310: /dev/redwood-vcodec-client and the datagram relay (IK) */
#include "nord2_vcodec.inc"   /* 0310: /dev/redwood-vcodec hardware part (K02) */
#include "nord2_jpegdev.inc"   /* 0310: /dev/redwood-jpeg (K01) */
#include "nord2_gnss_bind.inc"   /* 0580: GNSS /dev/gpsdl0,1 (0350..0364 line) + its Wi-Fi/conninfra binding */
#include "nord2_vcodec_at.inc"   /* 0311: VENC bring-up steps, autotest command 8 */
#include "nord2_mdp.inc"   /* 0670: MDP (mdpsys 0x1f000000) probe, stage 0 of the MDP offload plan */
#include "nord2_autotest.inc"   /* 0296: synthetic touches/keys and panel pictures over the USB console */
/*
 * 0496 (bright-fix-20261114): the torch.  /dev/redwood-torch did not exist, so the camera's TorchLevel writes ("the flashlight at N
 * of 1000 NOT TAKEN", camera-userland rwcam_device.c torch_set) went nowhere and the flash LED never lit.  The device only records
 * the request; the MT6360 FLED registers are written by the input owner thread (n2t_service, every 10 ms), the only thread that
 * drives the GPIO202/203 software I2C.  Policy and limits: nord2_torch.h.
 * Kill switches: boot-arg n2torch=0 (no device node; also sysctl kern.n2torch=0 at run time: LED off and kept off).
 * kern.n2torch_maxsel (ITOR 0..14, default 6 = 100 mA per channel), kern.n2torch_flashsel (default 14 = 200 mA per channel, pulses of
 * at most 1 s, duty <= 20 %), kern.n2torch_max_s (default 300 s), kern.n2torchstat (read only).
 */
#include <sys/sysctl.h>
#include "nord2_torch.h"
extern bool nord2_battery_temperature_cC(int *centi);     /* iokit/Kernel/arm/Nord2GPU.cpp */

static struct n2t_cfg n2t_cfg_g = { 1, N2T_MAXSEL_DEFAULT, N2T_FLASHSEL_DEFAULT, N2T_ON_S_DEFAULT, 1, N2T_STROBESEL_DEFAULT };   /* 0560: + strobe */
static struct n2t_status n2t_pub;               /* 0560: what read() gives (owner thread writes it under n2t_lock) */
static struct n2t_state n2t_st;                 /* owner thread (stat copy under n2t_lock) */
static volatile uint64_t n2t_req_word;          /* seq:32 | level:16 | ms:16, written by userland, read by the owner thread */
static uint32_t n2t_seq, n2t_opens;
static bool n2t_made, n2t_owner_ready, n2t_announced;
static lck_grp_t *n2t_grp;
static lck_mtx_t *n2t_lock;                     /* n2t_seq / n2t_opens / n2t_req_word writers, stat copies */
static uint64_t n2t_therm_next_us;

static void
n2t_post(uint32_t level, uint32_t ms, uint32_t strobe)
{
	lck_mtx_lock(n2t_lock);
	n2t_seq = n2t_seq + 1 == 0 ? 1 : n2t_seq + 1;
	/* 0560: bit 15 of the level field = strobe (levels are <= 1000) */
	__atomic_store_n(&n2t_req_word, (uint64_t)n2t_seq << 32 | (uint64_t)((level & 0x7fffu) | (strobe ? 0x8000u : 0u)) << 16 | (ms & 0xffffu),
	    __ATOMIC_RELEASE);
	lck_mtx_unlock(n2t_lock);
}

/* MT6360 PMU (0x34), plain SMBus byte write / read, on the power bus (GPIO202/203).  Owner thread only. */
static bool
n2t_pmu_write(uint8_t reg, uint8_t value)
{
	ni_bus(true);
	bool ok = ni_start() && ni_send(N2T_PMU_ADDR << 1) && ni_send(reg) && ni_send(value);
	bool stopped = ni_stop();
	uint8_t check = 0;
	bool rb = ok && stopped && ni_bus_read(N2T_PMU_ADDR, reg, &check, 1);
	ni_bus(false);
	return rb && check == value;
}

static bool
n2t_pmu_read(uint8_t reg, uint8_t *value)
{
	ni_bus(true);
	bool ok = ni_bus_read(N2T_PMU_ADDR, reg, value, 1);
	ni_bus(false);
	return ok;
}

/* LED off: CS1/CS2/TORCH cleared (STROBE is never set here) */
static bool
n2t_hw_off(void)
{
	uint8_t en = 0;
	if (!n2t_pmu_read(N2T_REG_FLED_EN, &en)) { return false; }
	return (en & (N2T_EN_ON | N2T_EN_STROBE)) == 0 || n2t_pmu_write(N2T_REG_FLED_EN, (uint8_t)(en & ~(N2T_EN_ON | N2T_EN_STROBE)));
}

/* LED on in torch mode at ITOR sel on both channels (Android DUAL_TORCH: strobe off, 500 us, CS1|CS2|TORCH) */
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

/*
 * 0560: LED on in STROBE mode at ISTRB sel on both channels (Android DUAL_FLASH: strobe off, 400 us, CS1|CS2|STROBE), with the hardware
 * timeout STRB_TO set first (the strobe ends by itself even if this thread stalls).  *on_t = mach_absolute_time() after the enabling write.
 */
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

/* 0560: the read() status, refreshed by the owner thread */
static void
n2t_publish(uint32_t state, uint32_t sel, uint64_t on_t, uint64_t off_t, const struct n2t_cfg *c)
{
	lck_mtx_lock(n2t_lock);
	n2t_pub.magic = N2T_STATUS_MAGIC; n2t_pub.version = 1;
	n2t_pub.seq = n2t_st.seen_seq;
	n2t_pub.state = state;
	if (on_t != 0) { n2t_pub.on_time = on_t; n2t_pub.off_time = 0; n2t_pub.sel = sel; n2t_pub.lit_seq = n2t_st.seen_seq; }
	if (off_t != 0) { n2t_pub.off_time = off_t; }
	n2t_pub.flags = (c->strobe ? 1u : 0u) | (n2t_st.therm == 1 ? 2u : 0u) | (n2t_st.therm >= 2 ? 4u : 0u) | (n2t_st.latched ? 8u : 0u) |
	    (c->mode ? 0u : 16u);
	n2t_pub.strobesel = c->strobesel; n2t_pub.strobe_ms_max = N2T_STROBE_MS_MAX;
	lck_mtx_unlock(n2t_lock);
}

static void
n2t_io_result(bool ok, const char *what)
{
	if (ok) { n2t_st.faults = 0; return; }
	n2t_st.io_fails++;
	if (++n2t_st.faults >= N2T_FAULT_LATCH && !n2t_st.latched) {
		n2t_st.latched = true;
		(void)n2t_hw_off();
		printf("NORD2 TORCH: %u I2C failures in a row (%s, stage %u): torch disabled for this boot\n", n2t_st.faults, what, ni_stage);
	} else {
		printf("NORD2 TORCH: %s FAILED (stage %u, fails %u)\n", what, ni_stage, n2t_st.faults);
	}
}

/* input owner thread, every loop (10 ms) */
static void
n2t_service(void)
{
	if (!n2t_made) { return; }
	uint64_t now = ni_now_us();
	if (!n2t_owner_ready) {
		n2t_owner_ready = true;
		uint8_t en = 0, st2 = 0;
		bool ok = n2t_pmu_read(N2T_REG_FLED_EN, &en) && n2t_pmu_read(N2T_REG_CHG_STAT2, &st2);
		bool off = ok && n2t_hw_off();
		printf("NORD2 TORCH: MT6360 FLED_EN=%#x CHG_STAT2=%#x (VINOVP %u) read %s, LED off at start %s\n", en, st2, (st2 & N2T_VINOVP) ? 1u : 0u,
		    ok ? "ok" : "FAILED", off ? "ok" : "FAILED");
		n2t_io_result(ok && off, "start");
	}
	uint64_t w = __atomic_load_n(&n2t_req_word, __ATOMIC_ACQUIRE);
	struct n2t_req r = { (uint32_t)(w >> 32), (uint32_t)(w >> 16) & 0x7fffu, (uint32_t)w & 0xffffu, (uint32_t)(w >> 31) & 1u };   /* 0560: + strobe bit */
	lck_mtx_lock(n2t_lock);
	struct n2t_cfg c = n2t_cfg_g;
	lck_mtx_unlock(n2t_lock);
	uint32_t prev_seq = n2t_st.seen_seq;
	n2t_take(&n2t_st, &r, now);
	bool fresh = n2t_st.seen_seq != prev_seq;
	if ((n2t_st.hw_on || n2t_st.level != 0) && now >= n2t_therm_next_us) {      /* the gauge only while the LED is (to be) lit */
		int cc = 0;
		bool ok = nord2_battery_temperature_cC(&cc);
		n2t_therm_next_us = now + N2T_THERM_PERIOD_US;
		if (n2t_therm_update(&n2t_st, ok, cc, now)) {
			printf("NORD2 TORCH: thermal level %u at battery %d.%02d C (%s)\n", n2t_st.therm, cc / 100, (cc < 0 ? -cc : cc) % 100,
			    n2t_st.therm >= 2 ? "LED off" : n2t_st.therm == 1 ? "lowest current" : "normal");
		}
	}
	bool was_expired = n2t_st.expired;
	uint32_t sel = 0;
	uint32_t kind = n2t_decide_kind(&n2t_st, &c, now, &sel);      /* 0560: 0 dark, 1 torch mode, 2 strobe */
	bool want = kind != 0;
	if (fresh) { n2t_publish(n2t_st.hw_on ? (n2t_st.hw_strobe ? 2u : 1u) : 0u, 0, 0, 0, &c); }
	if (want && kind == 2 && !(n2t_st.hw_on && n2t_st.hw_strobe && sel == n2t_st.hw_sel)) {
		/* 0560: a strobe (from dark or from torch mode); its end is the pulse's (n2t_decide) or the hardware timeout */
		uint64_t on_t = 0;
		bool ok = n2t_hw_strobe(sel, n2t_st.ms, &on_t);
		n2t_io_result(ok, "strobe");
		if (ok) {
			if (!n2t_st.hw_on) { n2t_st.ons++; }
			n2t_st.hw_on = true; n2t_st.hw_strobe = true; n2t_st.hw_sel = sel; n2t_st.strobes++;
			n2t_publish(2, sel, on_t, 0, &c);
			printf("NORD2 TORCH: strobe %u ms, level %u/1000 -> ISTRB %u (%u mA x 2), hardware timeout %u ms\n", n2t_st.ms, n2t_st.level, sel,
			    n2t_strobe_ma(sel), 64u + 32u * n2t_strobe_to_sel(n2t_st.ms));
		}
		return;
	}
	if (want && kind == 1 && n2t_st.hw_strobe) { n2t_st.hw_on = false; n2t_st.hw_strobe = false; }   /* 0560: strobe -> torch mode: relight */
	if (!was_expired && n2t_st.expired && n2t_st.ms == 0) {
		printf("NORD2 TORCH: on for %u s: turned off (kern.n2torch_max_s; a new request relights it)\n", c.max_on_s);
	}
	if (want && (!n2t_st.hw_on || sel != n2t_st.hw_sel)) {
		bool ok = n2t_hw_on(sel, n2t_st.hw_on);
		n2t_io_result(ok, "on");
		if (ok) {
			if (!n2t_st.hw_on) { n2t_st.ons++; n2t_publish(1, sel, mach_absolute_time(), 0, &c); }      /* 0560 */
			n2t_st.hw_on = true; n2t_st.hw_sel = sel;
			printf("NORD2 TORCH: on, level %u/1000%s -> ITOR %u (%u mA x 2)%s\n", n2t_st.level, n2t_st.ms ? " (flash pulse)" : "", sel,
			    25u + sel * 25u / 2u, n2t_st.ms ? "" : "");
		}
	} else if (!want && n2t_st.hw_on) {
		bool ok = n2t_hw_off();
		n2t_io_result(ok, "off");
		if (ok) {
			n2t_publish(0, 0, 0, mach_absolute_time(), &c);      /* 0560 */
			n2t_st.hw_on = false; n2t_st.hw_strobe = false; n2t_st.offs++;
			uint8_t s1 = 0, s2 = 0;
			(void)n2t_pmu_read(N2T_REG_FLED_STAT1, &s1); (void)n2t_pmu_read(N2T_REG_FLED_STAT2, &s2);
			printf("NORD2 TORCH: off (%s) FLED_STAT %#x %#x\n", !c.mode ? "kern.n2torch=0" : n2t_st.latched ? "fault latch" :
			    n2t_st.therm >= 2 ? "thermal" : n2t_st.expired ? (n2t_st.ms ? "end of pulse" : "time limit") : "request", s1, s2);
		}
	} else if (fresh && !want && n2t_st.level != 0) {
		printf("NORD2 TORCH: request %u/1000 not lit (%s)\n", n2t_st.level, !c.mode ? "kern.n2torch=0" : n2t_st.latched ? "fault latch" :
		    n2t_st.therm >= 2 ? "thermal" : "expired");
	}
}

/* nord2_input_prepare_return: other cores are off, no locks */
static void
n2t_force_off(void)
{
	if (n2t_made && n2t_owner_ready) { (void)n2t_hw_off(); }
}

static int
n2td_open(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	if (!n2t_made || minor(dev) != 0) { return ENXIO; }
	lck_mtx_lock(n2t_lock);
	n2t_opens++;
	lck_mtx_unlock(n2t_lock);
	return 0;
}

static int
n2td_close(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	if (!n2t_made || minor(dev) != 0) { return ENXIO; }
	n2t_post(0, 0, 0);      /* the last close (devfs calls d_close once): the owner is gone, the LED goes off */
	lck_mtx_lock(n2t_lock);
	n2t_opens = 0;
	lck_mtx_unlock(n2t_lock);
	return 0;
}

static int
n2td_write(dev_t dev, struct uio *uio, __unused int ioflag)
{
	uint8_t b[12];
	uint32_t level = 0, ms = 0, strobe = 0;
	if (!n2t_made || minor(dev) != 0) { return ENXIO; }
	user_ssize_t n = uio_resid(uio);
	if (n != 4 && n != 8 && n != 12) { return EINVAL; }      /* 0560: + 12-byte strobe request */
	int err = uiomove((char *)b, (int)n, uio);
	if (err != 0) { return err; }
	if (n == 12 ? !n2t_parse12(b, 12, &level, &ms, &strobe) : !n2t_parse(b, (unsigned)n, &level, &ms)) { return EINVAL; }
	n2t_post(level, ms, strobe);
	if (!n2t_announced) { n2t_announced = true; printf("NORD2 TORCH: first request %u/1000%s from %s\n", level, ms ? " (pulse)" : "", "userland"); }
	return 0;
}

/* 0560: read() = struct n2t_status (the strobe's on / off times for the camera); a shorter buffer gets the first bytes */
static int
n2td_read(dev_t dev, struct uio *uio, __unused int ioflag)
{
	if (!n2t_made || minor(dev) != 0) { return ENXIO; }
	struct n2t_status st;
	lck_mtx_lock(n2t_lock);
	st = n2t_pub;
	st.magic = N2T_STATUS_MAGIC; st.version = 1;
	lck_mtx_unlock(n2t_lock);
	user_ssize_t n = uio_resid(uio);
	if (n <= 0) { return 0; }
	return uiomove((char *)&st, (int)(n < (user_ssize_t)sizeof(st) ? n : (user_ssize_t)sizeof(st)), uio);
}

static const struct cdevsw n2td_cdevsw = {
	.d_open = n2td_open, .d_close = n2td_close,
	.d_read = n2td_read, .d_write = n2td_write, .d_ioctl = eno_ioctl,
	.d_stop = eno_stop, .d_reset = eno_reset, .d_ttys = NULL,
	.d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0,
};

static int
n2t_sysctl_cfg SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1)
	if (n2t_lock == NULL) { return ENODEV; }
	lck_mtx_lock(n2t_lock);
	struct n2t_cfg c = n2t_cfg_g;
	lck_mtx_unlock(n2t_lock);
	uint32_t *field = arg2 == 0 ? &c.mode : arg2 == 1 ? &c.maxsel : arg2 == 2 ? &c.flashsel : arg2 == 3 ? &c.max_on_s : arg2 == 4 ? &c.strobe : &c.strobesel;
	int v = (int)*field, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 100000) { return EINVAL; }
	*field = (uint32_t)v;
	n2t_cfg_fix(&c);
	lck_mtx_lock(n2t_lock);
	n2t_cfg_g = c;
	lck_mtx_unlock(n2t_lock);
	printf("NORD2 TORCH: sysctl %s: mode %u maxsel %u flashsel %u max_s %u strobe %u strobesel %u\n", oidp->oid_name, c.mode, c.maxsel, c.flashsel,
	    c.max_on_s, c.strobe, c.strobesel);
	return 0;
}
static int
n2t_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[480];
	struct n2t_state s = n2t_st;            /* owner-thread fields, read without a lock: a diagnostic snapshot */
	snprintf(buf, sizeof(buf), "made=%u owner=%u lit=%u sel=%u level=%u ms=%u expired=%u therm=%u bat_cC=%d latched=%u opens=%u reqs=%llu "
	    "ons=%llu offs=%llu timeouts=%llu flashes=%llu cooled=%llu derates=%llu therm_offs=%llu io_fails=%llu strobe_now=%u strobes=%llu "
	    "strobe_cooled=%llu strobe_as_pulse=%llu", n2t_made, n2t_owner_ready,
	    s.hw_on, s.hw_sel, s.level, s.ms, s.expired, s.therm, s.therm_cc, s.latched, n2t_opens, s.reqs, s.ons, s.offs, s.timeouts, s.flashes,
	    s.flash_cooled, s.therm_derates, s.therm_offs, s.io_fails, s.hw_strobe, s.strobes, s.strobe_cooled, s.strobe_as_pulse);
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
SYSCTL_PROC(_kern, OID_AUTO, n2torch, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2t_sysctl_cfg, "I",
    "0496 torch: 1 allow, 0 off and kept off");
SYSCTL_PROC(_kern, OID_AUTO, n2torch_maxsel, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 1, n2t_sysctl_cfg, "I",
    "0496 torch current ceiling, ITOR 0..14 (25 mA + 12.5 mA x n per channel), default 6");
SYSCTL_PROC(_kern, OID_AUTO, n2torch_flashsel, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 2, n2t_sysctl_cfg, "I",
    "0496 flash pulse current ceiling, ITOR 0..14, default 14");
SYSCTL_PROC(_kern, OID_AUTO, n2torch_max_s, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 3, n2t_sysctl_cfg, "I",
    "0496 torch on-time limit, s (10..1800, default 300)");
SYSCTL_PROC(_kern, OID_AUTO, n2torch_strobe, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 4, n2t_sysctl_cfg, "I",
    "0560 camera flash: 1 strobe requests use the MT6360 STROBE mode, 0 they become torch pulses");
SYSCTL_PROC(_kern, OID_AUTO, n2torch_strobesel, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 5, n2t_sysctl_cfg, "I",
    "0560 strobe current ceiling, ISTRB 0..116 (25 mA + 6.25 mA x n per channel), default 76 = 500 mA");
SYSCTL_UINT(_kern, OID_AUTO, n2tpace, CTLFLAG_RW | CTLFLAG_LOCKED, &n2tp_on, 0, "0620 touch pacing: 0 old timing, 1 fast reads + fixed period");
SYSCTL_UINT(_kern, OID_AUTO, n2tp_half_us, CTLFLAG_RW | CTLFLAG_LOCKED, &n2tp_half_us, 0, "0620: half-bit delay of the touch reads, 1..3 us (4 = old)");
SYSCTL_UINT(_kern, OID_AUTO, n2tp_period_us, CTLFLAG_RW | CTLFLAG_LOCKED, &n2tp_period_us, 0, "0620: loop period while a finger is down, 4000..10000 us");
SYSCTL_PROC(_kern, OID_AUTO, n2torchstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, n2t_sysctl_stat, "A",
    "0496 torch state and counters");

static void
nord2_torch_start(void)
{
	uint32_t on = 1;
	(void)PE_parse_boot_argn("n2torch", &on, sizeof(on));
	n2t_init(&n2t_st);
	n2t_grp = lck_grp_alloc_init("nord2-torch", LCK_GRP_ATTR_NULL);
	n2t_lock = n2t_grp != NULL ? lck_mtx_alloc_init(n2t_grp, LCK_ATTR_NULL) : NULL;
	if (on == 0 || n2t_lock == NULL) {
		printf("NORD2 TORCH: /dev/redwood-torch not made (boot-arg n2torch=%u)\n", on);
		return;
	}
	int major = cdevsw_add(-1, &n2td_cdevsw);
	if (major < 0) { printf("NORD2 TORCH: cdevsw_add failed (%d)\n", major); return; }
	if (devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "redwood-torch") == NULL) {
		printf("NORD2 TORCH: devfs_make_node failed\n");
		(void)cdevsw_remove(major, &n2td_cdevsw);
		return;
	}
	__atomic_store_n(&n2t_made, true, __ATOMIC_RELEASE);
	printf("NORD2 TORCH: /dev/redwood-torch made (major %d, 0666): MT6360 FLED1+FLED2 torch mode, ceiling ITOR %u (%u mA x 2), "
	    "pulses ITOR %u <= %u ms duty 1/%u, off after %u s, thermal derate %d C / off %d C\n", major, n2t_cfg_g.maxsel,
	    25u + n2t_cfg_g.maxsel * 25u / 2u, n2t_cfg_g.flashsel, N2T_FLASH_MS_MAX, N2T_FLASH_DUTY_DIV + 1, n2t_cfg_g.max_on_s,
	    N2T_WARM_CC / 100, N2T_HOT_CC / 100);
	printf("NORD2 TORCH STROBE: 0560 camera flash: strobe %s, ISTRB %u (%u mA x 2) <= %u ms, hardware timeout pulse + %u ms, read() status v1\n",
	    n2t_cfg_g.strobe ? "on" : "off", n2t_cfg_g.strobesel, n2t_strobe_ma(n2t_cfg_g.strobesel), N2T_STROBE_MS_MAX, N2T_STRB_TO_MARGIN_MS);
}

#include "nord2_haptic.inc"   /* 0610: /dev/redwood-haptic and the AW8697 on I2C9 */

void
nord2_input_init(void)
{
	int enabled = 0, stop = 0;
	thread_t thread;
	PE_parse_boot_argn("nord2input", &enabled, sizeof(enabled));
	PE_parse_boot_argn("nord2stopat", &stop, sizeof(stop));
	if (ni_started || enabled != 1 || stop != 23 || !nord2_smp_userland_ready()) { return; }
	ni_started = true;
	n2rtc_arm();         /* 0540: before the owner thread exists; the first exec waits for its RTC read */
	kern_return_t kr = kernel_thread_start(ni_thread, NULL, &thread);
	if (kr == KERN_SUCCESS) { thread_deallocate(thread); }
	else { printf("NORD2 INPUT: thread start failed %d\n", kr); n2rtc_disarm(); }
	nord2_scp_start();   /* 0284 */
	nord2_hang_start();  /* 0284 */
	nord2_cam_start();   /* 0284 */
	nord2_autotest_start();   /* 0296 */
	nord2_jpegdev_start();   /* 0310: /dev/redwood-jpeg */
	nord2_vcodec_start();   /* 0310: /dev/redwood-vcodec + /dev/redwood-vcodec-client */
	nord2_gpsdl_start();   /* 0580: /dev/gpsdl0,1 (no register access here; kill switch sysctl kern.n2gnss bit 0) */
	nord2_torch_start();    /* 0496: /dev/redwood-torch */
	nord2_haptic_start();   /* 0610: /dev/redwood-haptic */
	nord2_mdp_start();   /* 0670: MDP probe thread (read-only at +45 s by default; kern.n2mdp_probe) */
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

#if defined(NORD2)
#define NAH_NATIVE_DEFINE_PREPARE 1
#include "audio_hw_native.inc"
#endif
