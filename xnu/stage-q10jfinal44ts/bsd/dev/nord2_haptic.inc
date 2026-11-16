/*
 * 0610 (haptic-20261114): the AW8697 haptic motor (included by sweet_touch.c after the torch, 0496).  Chip, sequences and
 * policy: nord2_haptic.h.  The chip sits on I2C9 (GPIO120/121), the software bus the input owner thread already drives for the
 * alert-slider halls (nord2_slider.inc): every chip access here runs on that thread (n2h_owner_start, n2h_service, called from
 * ni_thread), so no other bus user can collide with it.  /dev/redwood-haptic (0666) only records iOS's waveform envelope.
 * No ml_io_map: GPIO110 (reset) and GPIO135 (irq, read only) are in the 0x10005000 page already mapped as ni_gpio.
 * Kill switches: boot-arg n2hap=0 or n2gdv +1024 (no device node, the chip and its pins untouched); sysctl kern.n2hap=0 at run
 * time (motor stopped and kept off).  kern.n2hap_drvmax (drive at full scale, 18..60, default 60 = Android), kern.n2hap_max_ms
 * (one continuous run, default 1000), kern.n2hap_test (play effect N once: diagnostics), kern.n2hapstat (read only).
 * 0611 (haptic2-20261114): after the boot pulse the owner thread synthesises three click waveforms at the tracked f0, uploads them
 * to the chip's SRAM in 128-byte steps (one per tick, then read back and compared) and from then on plays each iOS onset as one
 * RAM click (tick / click / heavy by the onset's peak, gain by level and VBAT); CONT drive is kept only for waveforms still
 * sounding 50 ms after their onset.  A failed upload or verify keeps the 0610 CONT behaviour.  nord2_haptic_ram.h.
 * 0613 (haptic3-20261114): CONT only after 300 ms of loud sound (was 50 ms: UIKit waveforms of 50..120 ms got click + buzz); a
 * waveform still loud after a click gets more clicks every 70 ms (tick train) instead; a RAM play still busy at its end is stopped
 * at once (GLB_STATE stayed 7 after the boot click: 87 ms of motor phase, onsets dropped); stale window 80 ms; one log line per iOS
 * waveform with its length, peak and the path taken (RAM / RAM train / CONT / skip + why), and per-path sums.
 * 0614 (haptic4-20261114): the waveform is chosen on the peak after a 9 ms lookahead (the first write often held only the attack:
 * the same 10347-peak iOS waveform played a weak tick or a click at random), gain floor 0.85, tick 1.5 cycles + brake, and a RAM
 * play gets 15 ms past its end to reach idle by itself before GO 0.
 */
#include "nord2_haptic.h"

static struct n2h_cfg n2h_cfg_g = { 1, N2H_DRV_HW_MAX, N2H_MAX_MS_DEFAULT };
static struct n2h_state n2h_st;                 /* owner thread */
static struct n2h_stream n2h_strm;              /* owner thread */
static struct n2h_env n2h_env_g;                /* written by the HAL I/O thread (write handler), read by the owner thread */
static volatile uint64_t n2h_req_word;          /* internal patterns (kern.n2hap_test), latest wins */
static uint32_t n2h_seq, n2h_opens, n2h_test_last;
static bool n2h_made, n2h_owner_ready, n2h_chip_ok, n2h_pins_saved, n2h_announced;
static lck_grp_t *n2h_grp;
static lck_mtx_t *n2h_lock;
static struct { unsigned mux, dir, dout; } n2h_pin_rst, n2h_pin_irq;
enum { N2H_HW_OFF = 0, N2H_HW_ON, N2H_HW_STOPPING, N2H_HW_RAM };
static struct {
	unsigned phase, faults, said;
	uint8_t drv, glb_max;
	uint64_t on_us, until_us, stop_us, therm_next_us, sum_next_us, sum_sig;
	uint64_t starts, stops, drv_changes, watchdog, stop_timeouts, resets, io_fails, chip_faults;
	uint32_t f0_last, lra_mohm, vbat_mv;
} n2h_hw;

/* 0611: the RAM clicks (owner thread) */
enum { N2H_RAM_NONE = 0, N2H_RAM_WRITING, N2H_RAM_VERIFYING, N2H_RAM_READY, N2H_RAM_FAILED };
static uint8_t n2h_ram_img[N2H_RAM_IMG_MAX];
static struct n2h_ram_layout n2h_lay;
static struct {
	unsigned state, off, said, ovsaid, wsaid;
	uint8_t datdbg_boot, wave, gain;
	bool datdbg_read;
	uint64_t t0_us, play_end_us, vbat_next_us, logged_onset;
	uint64_t uploads, plays, overruns, verify_bad;
} n2h_ram;

/* ---- the I2C9 transactions (owner thread only) ---- */
static bool
n2h_bus_rd(__unused void *ctx, uint8_t reg, uint8_t *v)
{
	ns_bus();
	bool ok = ni_bus_read(N2H_ADDR, reg, v, 1);
	ni_bus(false);
	return ok;
}

static bool
n2h_bus_wr(__unused void *ctx, uint8_t reg, uint8_t v)
{
	ns_bus();
	bool ok = ni_start() && ni_send((uint8_t)(N2H_ADDR << 1)) && ni_send(reg) && ni_send(v);
	bool stopped = ni_stop();
	ni_bus(false);
	return ok && stopped;
}

static void
n2h_bus_sleep(__unused void *ctx, unsigned ms)
{
	ni_wait(ms);
}

/* 0611: one start, the register, n bytes, one stop (the RAMDATA port auto-increments the SRAM address) */
static bool
n2h_bus_wrn(__unused void *ctx, uint8_t reg, const uint8_t *v, unsigned n)
{
	ns_bus();
	bool ok = n > 0 && n <= N2H_BURST_MAX && ni_start() && ni_send((uint8_t)(N2H_ADDR << 1)) && ni_send(reg);
	for (unsigned i = 0; ok && i < n; i++) { ok = ni_send(v[i]); }
	bool stopped = ni_stop();
	ni_bus(false);
	return ok && stopped;
}

static bool
n2h_bus_rdn(__unused void *ctx, uint8_t reg, uint8_t *v, unsigned n)
{
	ns_bus();
	bool ok = ni_bus_read(N2H_ADDR, reg, v, n);
	ni_bus(false);
	return ok;
}

static const struct n2h_bus n2h_bus_g = { NULL, n2h_bus_rd, n2h_bus_wr, n2h_bus_sleep, n2h_bus_wrn, n2h_bus_rdn };

static void
n2h_pin_save(unsigned pin, unsigned *mux, unsigned *dir, unsigned *dout)
{
	*mux = (ni_rd(ni_gpio, 0x300 + pin / 8 * 16) >> (pin % 8 * 4)) & 15;
	*dir = (ni_rd(ni_gpio, pin / 32 * 16) >> (pin % 32)) & 1;
	*dout = (ni_rd(ni_gpio, 0x100 + pin / 32 * 16) >> (pin % 32)) & 1;
}

static void
n2h_pin_restore(unsigned pin, unsigned mux, unsigned dir, unsigned dout)
{
	ni_gpio_bit(pin, 0, false);
	ni_gpio_bit(pin, 0x100, dout != 0);
	ni_gpio_bit(pin, 0, dir != 0);
	ni_mask(ni_gpio, 0x300 + pin / 8 * 16, 15u << (pin % 8 * 4), mux << (pin % 8 * 4));
}

/* the reset pin as a GPIO output at `high' (low = the chip in reset: the motor cannot be driven) */
static void
n2h_reset_pin(bool high)
{
	ni_gpio_bit(N2H_GPIO_RESET, 0x100, high);
	ni_gpio_bit(N2H_GPIO_RESET, 0, true);
	ni_mask(ni_gpio, 0x300 + N2H_GPIO_RESET / 8 * 16, 15u << (N2H_GPIO_RESET % 8 * 4), 0);
}

static void
n2h_latch(const char *why)
{
	n2h_st.latched = true;
	(void)n2h_bus_wr(NULL, N2H_REG_GO, 0);
	n2h_reset_pin(false);
	n2h_hw.phase = N2H_HW_OFF;
	printf("NORD2 HAPTIC: %s: the chip is held in reset (GPIO110 low), haptics off for this boot\n", why);
}

static void
n2h_io_result(bool ok, const char *what)
{
	if (ok) { n2h_hw.faults = 0; return; }
	n2h_hw.io_fails++;
	if (++n2h_hw.faults >= N2H_FAULT_LATCH && !n2h_st.latched) {
		char b[96];
		snprintf(b, sizeof(b), "%u I2C failures in a row (%s, stage %u)", n2h_hw.faults, what, ni_stage);
		n2h_latch(b);
	} else if (n2h_hw.io_fails <= 16) {
		printf("NORD2 HAPTIC: %s FAILED (stage %u, fails %u)\n", what, ni_stage, n2h_hw.faults);
	}
}

/* reset pulse + id + init; true when the chip is configured and in standby */
static bool
n2h_chip_init(const char *why)
{
	uint8_t id = 0;
	bool idok = false;
	n2h_reset_pin(false);
	ni_wait(2);
	n2h_reset_pin(true);
	ni_wait(5);
	for (unsigned i = 0; i < 5 && !idok; i++) {
		idok = n2h_bus_rd(NULL, N2H_REG_ID, &id) && id == N2H_CHIPID;
		if (!idok) { ni_wait(2); }
	}
	if (!idok) {
		printf("NORD2 HAPTIC: %s: no AW8697 at I2C9 0x5a (id %#x, stage %u, scl=%d sda=%d)\n", why, id, ni_stage,
		    ni_level(NS_SCL), ni_level(NS_SDA));
		return false;
	}
	unsigned bad = N2H_RUN(&n2h_bus_g, n2h_seq_init, 0, 0);
	if (bad) {
		printf("NORD2 HAPTIC: %s: init step %u failed (reg %#x, stage %u)\n", why, bad, n2h_seq_init[bad - 1].reg, ni_stage);
		return false;
	}
	return true;
}

static void
n2h_owner_start(void)
{
	if (!__atomic_load_n(&n2h_made, __ATOMIC_ACQUIRE)) { return; }      /* n2hap=0: the chip and its pins are not touched */
	n2h_pin_save(N2H_GPIO_RESET, &n2h_pin_rst.mux, &n2h_pin_rst.dir, &n2h_pin_rst.dout);
	n2h_pin_save(N2H_GPIO_IRQ, &n2h_pin_irq.mux, &n2h_pin_irq.dir, &n2h_pin_irq.dout);
	n2h_pins_saved = true;
	printf("NORD2 HAPTIC: pins as found: reset GPIO110 mux=%u dir=%u out=%u level=%d, irq GPIO135 mux=%u dir=%u level=%d\n",
	    n2h_pin_rst.mux, n2h_pin_rst.dir, n2h_pin_rst.dout, ni_level(N2H_GPIO_RESET), n2h_pin_irq.mux, n2h_pin_irq.dir,
	    ni_level(N2H_GPIO_IRQ));
	if (!ns_enabled || !ni_level(NS_SCL) || !ni_level(NS_SDA)) {
		printf("NORD2 HAPTIC: I2C9 not usable (nord2slider=%d scl=%d sda=%d): haptics off, chip untouched\n", ns_enabled,
		    ni_level(NS_SCL), ni_level(NS_SDA));
		n2h_pins_saved = false;
		return;
	}
	n2h_init(&n2h_st, ni_now_us());
	if (!n2h_chip_init("start")) {
		n2h_pin_restore(N2H_GPIO_RESET, n2h_pin_rst.mux, n2h_pin_rst.dir, n2h_pin_rst.dout);
		printf("NORD2 HAPTIC: off for this boot (reset pin restored)\n");
		return;
	}
	uint8_t trim = 0, sysst = 0, glb = 0;
	(void)n2h_bus_rd(NULL, N2H_REG_TRIM_LRA, &trim);
	(void)n2h_bus_rd(NULL, N2H_REG_SYSST, &sysst);
	(void)n2h_bus_rd(NULL, N2H_REG_GLB_STATE, &glb);
	n2h_ram.datdbg_read = n2h_bus_rd(NULL, N2H_REG_DATDBG, &n2h_ram.datdbg_boot);     /* 0611: restored before CONT */
	bool lok = n2h_measure_lra(&n2h_bus_g, &n2h_hw.lra_mohm);
	bool vok = n2h_measure_vbat(&n2h_bus_g, &n2h_hw.vbat_mv);
	(void)N2H_RUN(&n2h_bus_g, n2h_seq_standby, 0, 0);
	n2h_chip_ok = true;
	n2h_hw.phase = N2H_HW_OFF;
	printf("NORD2 HAPTIC: AW8697 id 0x97 configured (CONT mode, f0 preset %u.%u Hz, drive <= %u, boost never used): TRIM_LRA %#x "
	    "SYSST %#x GLB %#x, LRA %u.%03u ohm%s, VBAT %u mV%s, irq level %d\n", N2H_F0_PRE / 10, N2H_F0_PRE % 10, n2h_cfg_g.drv_max,
	    trim, sysst, glb, n2h_hw.lra_mohm / 1000, n2h_hw.lra_mohm % 1000, lok ? "" : " (read FAILED)", n2h_hw.vbat_mv,
	    vok ? "" : " (read FAILED)", ni_level(N2H_GPIO_IRQ));
#ifdef N2H_FIXED_GAIN
	printf("NORD2 HAPTIC RAM: 0618 fixed gain %#x for every tap (no peak / VBAT scaling)\n", N2H_FIXED_GAIN);
#endif
	printf("NORD2 HAPTIC RAM: 0611 RAM clicks, profile %s: DATDBG at boot %#x%s; upload after the boot pulse\n", N2H_PROFILE->name,
	    n2h_ram.datdbg_boot, n2h_ram.datdbg_read ? "" : " (read FAILED)");
	if (n2h_hw.lra_mohm < 4000 || n2h_hw.lra_mohm > 60000) {
		printf("NORD2 HAPTIC: LRA resistance out of the 4..60 ohm range: the motor may be open or shorted (still allowed)\n");
	}
	/* one boot pulse: the user feels the motor once at boot; the log shows the chip's state machine and the tracked f0 */
	struct n2h_req r = { 1, N2H_FX_RAW, 800, 60 };
	n2h_take(&n2h_st, &r, &n2h_cfg_g, ni_now_us());
	n2h_owner_ready = true;
}

static void
n2h_hw_start(uint8_t drv, uint64_t now, uint64_t until, const char *src)
{
	if (n2h_ram.plays && n2h_ram.datdbg_read && n2h_ram.datdbg_boot <= N2H_GAIN_HW_MAX) {
		(void)n2h_write(&n2h_bus_g, N2H_REG_DATDBG, n2h_ram.datdbg_boot);     /* 0611: CONT as in 0610 after RAM clicks */
	}
	unsigned bad = N2H_RUN(&n2h_bus_g, n2h_seq_cont, 0, 0);
	if (!bad) { bad = N2H_RUN(&n2h_bus_g, n2h_seq_start, drv, n2h_drv_ov_for(drv)); }
	n2h_io_result(bad == 0, "start");
	if (bad) { return; }
	n2h_hw.phase = N2H_HW_ON; n2h_hw.drv = drv; n2h_hw.on_us = now; n2h_hw.until_us = until; n2h_hw.glb_max = 0;
	n2h_hw.starts++;
	uint8_t glb = 0;
	if (n2h_bus_rd(NULL, N2H_REG_GLB_STATE, &glb)) { n2h_hw.glb_max = glb & N2H_GLB_BUSY_MASK; }
	if (n2h_hw.said < 24) {
		printf("NORD2 HAPTIC: on (%s), drive %u ov %u, GLB_STATE %#x\n", src, drv, n2h_drv_ov_for(drv), glb);
	}
}

static void
n2h_hw_stop(uint64_t now, const char *why)
{
	unsigned bad = N2H_RUN(&n2h_bus_g, n2h_seq_stop, 0, 0);
	n2h_io_result(bad == 0, "stop");
	if (bad) { return; }
	n2h_hw.phase = N2H_HW_STOPPING; n2h_hw.stop_us = now; n2h_hw.stops++;
	if (n2h_hw.said < 24 && why != NULL) { printf("NORD2 HAPTIC: stop (%s) after %llu ms\n", why, (now - n2h_hw.on_us) / 1000); }
}

/* stopping: wait for GLB_STATE idle (the brake), then standby; a chip that does not stop is reset */
static void
n2h_hw_stopping(uint64_t now)
{
	uint8_t glb = 0xff, sysst = 0, fh = 0, fl = 0;
	bool ok = n2h_bus_rd(NULL, N2H_REG_GLB_STATE, &glb);
	n2h_io_result(ok, "state");
	if (ok && (glb & N2H_GLB_BUSY_MASK) == 0) {
		bool sok = N2H_RUN(&n2h_bus_g, n2h_seq_standby, 0, 0) == 0;
		bool rok = n2h_bus_rd(NULL, N2H_REG_SYSST, &sysst) && n2h_bus_rd(NULL, N2H_REG_F_LRA_CONT_H, &fh) &&
		    n2h_bus_rd(NULL, N2H_REG_F_LRA_CONT_L, &fl);
		n2h_io_result(sok && rok, "standby");
		if (!sok) { return; }
		n2h_hw.phase = N2H_HW_OFF;
		n2h_hw.f0_last = n2h_cont_f0(fh, fl);
		if (n2h_hw.said < 24) {
			n2h_hw.said++;
			printf("NORD2 HAPTIC: standby %llu ms after the stop; tracked f0 %u.%u Hz; SYSST %#x\n", (now - n2h_hw.stop_us) / 1000,
			    n2h_hw.f0_last / 10, n2h_hw.f0_last % 10, sysst);
		}
		if (sysst & 0xC6) {             /* boost error, over-voltage, over-current, over-temperature */
			n2h_hw.chip_faults++;
			char b[64];
			snprintf(b, sizeof(b), "chip fault SYSST %#x", sysst);
			n2h_latch(b);
		}
		return;
	}
	if (now - n2h_hw.stop_us >= N2H_STOP_TIMEOUT_US) {
		n2h_hw.stop_timeouts++;
		printf("NORD2 HAPTIC: still busy (GLB_STATE %#x) %llu ms after the stop: soft reset\n", glb, (now - n2h_hw.stop_us) / 1000);
		(void)n2h_bus_wr(NULL, N2H_REG_ID, N2H_SOFTRESET);
		ni_wait(4);
		n2h_hw.resets++;
		if (!n2h_chip_init("after a stop timeout")) { n2h_latch("re-init failed"); return; }
		n2h_ram.state = N2H_RAM_NONE;        /* 0611: the SRAM is uploaded again */
		n2h_strm.ram_ok = false;
		(void)N2H_RUN(&n2h_bus_g, n2h_seq_standby, 0, 0);
		n2h_hw.phase = N2H_HW_OFF;
	}
}

/* 0611: one RAM waveform (the motor is idle) */
static void
n2h_ram_start(uint8_t wave, uint8_t gain, uint64_t now, const char *src, uint32_t peak, uint64_t lat_us)
{
	unsigned bad = N2H_RUN(&n2h_bus_g, n2h_seq_ram_play, gain, n2h_lay.idx[wave]);
	n2h_io_result(bad == 0, "RAM play");
	if (bad) { return; }
	n2h_hw.phase = N2H_HW_RAM; n2h_hw.on_us = now; n2h_hw.starts++;
	n2h_ram.play_end_us = now + (uint64_t)n2h_lay.ms_x10[wave] * 100u + 2000u;
	n2h_hw.until_us = n2h_ram.play_end_us + (uint64_t)N2H_RAM_OVERRUN_MS * 1000u;
	n2h_ram.plays++; n2h_ram.wave = wave; n2h_ram.gain = gain;
	if (n2h_ram.said < 40) {
		n2h_ram.said++;
		printf("NORD2 HAPTIC: click (%s) %s (RAM wave %u) gain %#x peak %u, %u.%u ms waveform, %llu ms after the onset\n", src,
		    n2h_wave_name[wave], n2h_lay.idx[wave], gain, peak, n2h_lay.ms_x10[wave] / 10, n2h_lay.ms_x10[wave] % 10, lat_us / 1000);
	}
}

/* 0611: the RAM upload, one step per tick while the motor is idle (WRITING -> VERIFYING -> READY or FAILED) */
static void
n2h_ram_fail(const char *what, unsigned at)
{
	(void)N2H_RUN(&n2h_bus_g, n2h_seq_ram_close, 0, 0);
	(void)N2H_RUN(&n2h_bus_g, n2h_seq_standby, 0, 0);
	n2h_ram.state = N2H_RAM_FAILED;
	n2h_strm.ram_ok = false;
	printf("NORD2 HAPTIC RAM: %s at byte %u (stage %u): RAM clicks off, CONT taps as in 0610\n", what, at, ni_stage);
}

static void
n2h_ram_step(uint64_t now, const struct n2h_cfg *c)
{
	if (n2h_ram.state == N2H_RAM_NONE) {
		uint32_t f0 = n2h_f0_for_ram(n2h_hw.f0_last);
		if (n2h_ram_build(n2h_ram_img, sizeof(n2h_ram_img), f0, N2H_PROFILE, &n2h_lay) == 0) { n2h_ram_fail("image build failed", 0); return; }
		unsigned bad = N2H_RUN(&n2h_bus_g, n2h_seq_ram_open, 0, 0);
		n2h_io_result(bad == 0, "RAM open");
		if (bad) { n2h_ram_fail("RAM open failed", 0); return; }
		n2h_ram.state = N2H_RAM_WRITING; n2h_ram.off = 0; n2h_ram.t0_us = now; n2h_ram.uploads++;
		return;
	}
	unsigned n = n2h_lay.len - n2h_ram.off < N2H_RAM_CHUNK ? n2h_lay.len - n2h_ram.off : N2H_RAM_CHUNK;
	uint16_t addr = (uint16_t)(N2H_RAM_BASE + n2h_ram.off);
	if (n2h_ram.state == N2H_RAM_WRITING) {
		bool ok = n2h_ram_write(&n2h_bus_g, addr, n2h_ram_img + n2h_ram.off, n);
		n2h_io_result(ok, "RAM write");
		if (!ok) { n2h_ram_fail("RAM write failed", n2h_ram.off); return; }
		n2h_ram.off += n;
		if (n2h_ram.off >= n2h_lay.len) { n2h_ram.state = N2H_RAM_VERIFYING; n2h_ram.off = 0; }
		return;
	}
	if (n2h_ram.state == N2H_RAM_VERIFYING) {
		unsigned bad = n; uint8_t got = 0;
		bool ok = n2h_ram_verify(&n2h_bus_g, addr, n2h_ram_img + n2h_ram.off, n, &bad, &got);
		n2h_io_result(ok, "RAM read");
		if (!ok) { n2h_ram_fail("RAM read-back failed", n2h_ram.off); return; }
		if (bad < n) {
			n2h_ram.verify_bad++;
			printf("NORD2 HAPTIC RAM: read-back %#x != %#x at %#x\n", got, n2h_ram_img[n2h_ram.off + bad], N2H_RAM_BASE + n2h_ram.off + bad);
			n2h_ram_fail("RAM verify mismatch", n2h_ram.off + bad);
			return;
		}
		n2h_ram.off += n;
		if (n2h_ram.off < n2h_lay.len) { return; }
		bool cok = N2H_RUN(&n2h_bus_g, n2h_seq_ram_close, 0, 0) == 0 && N2H_RUN(&n2h_bus_g, n2h_seq_standby, 0, 0) == 0;
		n2h_io_result(cok, "RAM close");
		if (!cok) { n2h_ram_fail("RAM close failed", n2h_lay.len); return; }
		n2h_ram.state = N2H_RAM_READY;
		n2h_strm.ram_ok = true;
		n2h_strm.click_onset = n2h_env_g.onset_us;          /* what iOS played before is not replayed */
		n2h_ram.vbat_next_us = now + 60000000ull;
		printf("NORD2 HAPTIC RAM: %u bytes at %#x written and read back OK in %llu ms: f0 %u.%u Hz (tracked %u.%u), profile %s, "
		    "tick %u.%u ms, click %u.%u ms, heavy %u.%u ms; iOS onsets now play RAM clicks (boost bypassed, gain <= %#x)\n",
		    n2h_lay.len, N2H_RAM_BASE, (now - n2h_ram.t0_us) / 1000, n2h_lay.f0_dhz / 10, n2h_lay.f0_dhz % 10, n2h_hw.f0_last / 10,
		    n2h_hw.f0_last % 10, n2h_lay.vendor ? "vendor aw8697_haptic_170.bin" : N2H_PROFILE->name, n2h_lay.ms_x10[1] / 10, n2h_lay.ms_x10[1] % 10, n2h_lay.ms_x10[2] / 10,
		    n2h_lay.ms_x10[2] % 10, n2h_lay.ms_x10[3] / 10, n2h_lay.ms_x10[3] % 10, N2H_GAIN_HW_MAX);
		if (c->mode && n2h_st.therm < 2 && !n2h_st.latched) {   /* one boot click: the user feels the new waveform once */
			n2h_ram_start(N2H_W_CLICK, n2h_gain_for(1000, n2h_hw.vbat_mv, n2h_st.therm, N2H_PROFILE), now, "boot", 0, 0);
		}
	}
}

/* 0611: RAM waveform playing: wait for GLB_STATE idle after its length, then standby; still busy N2H_RAM_OVERRUN_MS later: GO 0 */
static void
n2h_ram_playing(uint64_t now)
{
	if (now < n2h_ram.play_end_us) { return; }
	uint8_t glb = 0xff;
	bool ok = n2h_bus_rd(NULL, N2H_REG_GLB_STATE, &glb);
	n2h_io_result(ok, "state");
	if (ok && (glb & N2H_GLB_BUSY_MASK) == 0) {
		bool sok = N2H_RUN(&n2h_bus_g, n2h_seq_standby, 0, 0) == 0;
		n2h_io_result(sok, "standby");
		if (!sok) { return; }
		n2h_hw.phase = N2H_HW_OFF; n2h_hw.stops++;
		if (n2h_ram.wsaid < 6) { n2h_ram.wsaid++; printf("NORD2 HAPTIC: click done, idle %llu ms after the start\n", (now - n2h_hw.on_us) / 1000); }
		return;
	}
	if (!ok) { return; }
	/* 0614: GLB_STATE 7 / 0xa right after a click's end are the chip's own end-of-play states (ticks reached idle by ~30 ms by
	 * themselves): give it N2H_RAM_SETTLE_MS before GO 0, so no play is cut */
	if (now < n2h_ram.play_end_us + (uint64_t)N2H_RAM_SETTLE_MS * 1000u) { return; }
	/* 0613: the waveform is over by time; the chip stays in GLB_STATE 7 (seen after the boot click) until GO 0: stop it now */
	n2h_ram.overruns++;
	if (n2h_ram.ovsaid < 8) { n2h_ram.ovsaid++; printf("NORD2 HAPTIC: RAM play end: GLB_STATE %#x %llu ms after the start: GO 0\n", glb, (now - n2h_hw.on_us) / 1000); }
	n2h_hw_stop(now, NULL);
}

/* 0611: one line per iOS waveform once it ended (first 40): its length, peak and frequency, for the tuning */
static void
n2h_ios_wave_log(uint64_t now)
{
	uint64_t onset = n2h_env_g.onset_us, heard = n2h_env_g.heard_us, quiet = n2h_env_g.quiet_us;
	if (onset == 0 || onset == n2h_ram.logged_onset || heard < onset) { return; }
	if (!(quiet > heard || now - heard >= N2H_ACTIVE_US)) { return; }
	n2h_ram.logged_onset = onset;
	uint64_t fr = n2h_env_g.act_frames, zc = n2h_env_g.zc, dur = (heard - onset) / 1000 + 5;   /* + the last 256-frame write */
	const char *path = n2h_path_account(&n2h_strm, onset, dur);
	static unsigned said;
	if (said >= 200) { return; }
	said++;
	printf("NORD2 HAPTIC: iOS waveform %llu ms (%llu loud frames), peak %llu (first %llu), ~%llu Hz -> %s, clicks %llu%s\n", dur, fr,
	    n2h_env_g.peak, n2h_strm.ons_onset == onset ? n2h_strm.ons_peak_first : 0, fr ? zc * 48000u / (2u * fr) : 0, path,
	    n2h_strm.ons_onset == onset ? n2h_strm.ons_clicks : 0, n2h_strm.ons_onset == onset && n2h_strm.ons_cont ? ", CONT" : "");
}

/* input owner thread, every loop (10 ms) */
static void
n2h_service(void)
{
	if (!n2h_owner_ready || !n2h_chip_ok) { return; }
	uint64_t now = ni_now_us();
	lck_mtx_lock(n2h_lock);
	struct n2h_cfg c = n2h_cfg_g;
	lck_mtx_unlock(n2h_lock);
	uint64_t w = __atomic_load_n(&n2h_req_word, __ATOMIC_ACQUIRE);
	if (w) {
		struct n2h_req r = n2h_req_unpack(w);
		n2h_take(&n2h_st, &r, &c, now);
	}
	bool hw_on = n2h_hw.phase == N2H_HW_ON;
	if ((hw_on || n2h_env_g.heard_us + N2H_ACTIVE_US > now) && now >= n2h_hw.therm_next_us) {
		int cc = 0;
		bool ok = nord2_battery_temperature_cC(&cc);
		n2h_hw.therm_next_us = now + N2H_THERM_PERIOD_US;
		if (n2h_therm_update(&n2h_st, ok, cc)) {
			printf("NORD2 HAPTIC: thermal level %u at battery %d.%02d C (%s)\n", n2h_st.therm, cc / 100, (cc < 0 ? -cc : cc) % 100,
			    n2h_st.therm >= 2 ? "motor off" : n2h_st.therm == 1 ? "half drive" : "normal");
		}
	}
	/* 0611: the RAM upload runs right after the boot pulse, while the motor is idle; nothing else drives the chip meanwhile */
	if ((n2h_ram.state == N2H_RAM_NONE || n2h_ram.state == N2H_RAM_WRITING || n2h_ram.state == N2H_RAM_VERIFYING) &&
	    n2h_hw.phase == N2H_HW_OFF && n2h_st.nseg == 0 && !n2h_st.latched) {
		n2h_ram_step(now, &c);
		if (n2h_ram.state != N2H_RAM_READY && n2h_ram.state != N2H_RAM_FAILED) { return; }
	}
	n2h_ios_wave_log(now);
	if (n2h_strm.ram_ok && n2h_hw.phase == N2H_HW_OFF && !n2h_st.latched) {
		uint8_t wave = 0, gain = 0;
		uint32_t peak = 0;
		if (n2h_ram.vbat_next_us && now >= n2h_ram.vbat_next_us) {     /* VBAT for the gain compensation, once a minute */
			n2h_ram.vbat_next_us = now + 60000000ull;
			uint32_t mv = 0;
			if (n2h_measure_vbat(&n2h_bus_g, &mv) && mv >= 2500 && mv <= 4700) { n2h_hw.vbat_mv = mv; }
		}
		n2h_budget(&n2h_st, now, false);
		uint64_t tr0 = n2h_strm.trains;
		if (n2h_click_take(&n2h_st, &n2h_strm, &n2h_env_g, &c, now, n2h_hw.vbat_mv, N2H_PROFILE, &wave, &gain, &peak)) {
			n2h_ram_start(wave, gain, now, n2h_strm.trains != tr0 ? "iOS train" : "iOS", peak, now - n2h_env_g.onset_us);
		}
	}
	hw_on = n2h_hw.phase == N2H_HW_ON || n2h_hw.phase == N2H_HW_RAM;
	bool pattern = n2h_st.nseg != 0;
	uint8_t drv = 0;
	uint64_t until = 0;
	bool want = n2h_decide_all(&n2h_st, &n2h_strm, &n2h_env_g, &c, now, hw_on, &drv, &until);
	if (n2h_st.latched) { return; }
	switch (n2h_hw.phase) {
	case N2H_HW_OFF:
		if (want && !pattern && n2h_strm.ram_ok) {
			/* 0613: CONT for a long iOS waveform only while it is still loud (not a quiet tail) */
			if (n2h_env_g.peak_recent < N2H_TRAIN_MIN_PEAK) {
				if (n2h_strm.ons_onset == n2h_env_g.onset_us && !n2h_strm.ons_clicks) { n2h_strm.ons_skip = N2H_SKIP_TAIL; }
				break;
			}
			n2h_env_g.peak_recent = 0;
			if (n2h_strm.ons_onset == n2h_env_g.onset_us) { n2h_strm.ons_cont = 1; }
		}
		if (want) { n2h_hw_start(drv, now, until, pattern ? n2h_fx_table[n2h_st.effect].name : "iOS"); }
		break;
	case N2H_HW_ON:
		if (now > n2h_hw.until_us + N2H_OVERRUN_US) {           /* this thread came back late: stop first, ask later */
			n2h_hw.watchdog++;
			printf("NORD2 HAPTIC: watchdog: on %llu ms past its limit\n", (now - n2h_hw.until_us) / 1000);
			n2h_hw_stop(now, NULL);
		} else if (!want) {
			n2h_hw_stop(now, pattern ? "end of pattern" : !c.mode ? "kern.n2hap=0" : n2h_st.therm >= 2 ? "thermal" : "iOS silent");
		} else {
			n2h_hw.until_us = until;
			if (drv + 4 < n2h_hw.drv || drv > n2h_hw.drv + 4) {
				bool ok = n2h_write(&n2h_bus_g, N2H_REG_DRV_LVL, drv) && n2h_write(&n2h_bus_g, N2H_REG_DRV_LVL_OV, n2h_drv_ov_for(drv));
				n2h_io_result(ok, "drive");
				if (ok) { n2h_hw.drv = drv; n2h_hw.drv_changes++; }
			}
			uint8_t glb = 0;
			if (n2h_hw.starts <= 4 && n2h_bus_rd(NULL, N2H_REG_GLB_STATE, &glb) && (glb & N2H_GLB_BUSY_MASK) > n2h_hw.glb_max) {
				n2h_hw.glb_max = glb & N2H_GLB_BUSY_MASK;
			}
		}
		break;
	case N2H_HW_STOPPING:
		n2h_hw_stopping(now);
		break;
	case N2H_HW_RAM:
		n2h_ram_playing(now);
		break;
	}
	if (now >= n2h_hw.sum_next_us) {
		n2h_hw.sum_next_us = now + 60000000ull;
		uint64_t sig = n2h_env_g.writes + n2h_hw.starts * 3 + n2h_st.throttled * 5 + n2h_hw.io_fails * 7 + n2h_ram.state * 11;
		if (sig != n2h_hw.sum_sig) {
			n2h_hw.sum_sig = sig;
			printf("NORD2 HAPTIC SUM: opens %u writes %llu frames %llu onsets %llu taps %llu runs %llu capped %llu starts %llu stops %llu "
			    "drvchg %llu throttled %llu therm %u refused %llu watchdog %llu stop_timeouts %llu resets %llu io_fails %llu faults %llu "
			    "f0 %u.%u Hz budget %lld ms\n", n2h_opens, n2h_env_g.writes, n2h_env_g.frames, n2h_env_g.onsets, n2h_strm.taps,
			    n2h_strm.runs, n2h_strm.capped, n2h_hw.starts, n2h_hw.stops, n2h_hw.drv_changes, n2h_st.throttled, n2h_st.therm,
			    n2h_st.therm_refused, n2h_hw.watchdog, n2h_hw.stop_timeouts, n2h_hw.resets, n2h_hw.io_fails, n2h_hw.chip_faults,
			    n2h_hw.f0_last / 10, n2h_hw.f0_last % 10, n2h_st.budget_us / 1000);
			printf("NORD2 HAPTIC RAM SUM: state %u clicks %llu (tick %llu click %llu heavy %llu) plays %llu stale %llu overruns %llu "
			    "verify_bad %llu uploads %llu last gain %#x vbat %u mV\n", n2h_ram.state, n2h_strm.clicks, n2h_strm.wave_n[1],
			    n2h_strm.wave_n[2], n2h_strm.wave_n[3], n2h_ram.plays, n2h_strm.stale, n2h_ram.overruns, n2h_ram.verify_bad,
			    n2h_ram.uploads, n2h_ram.gain, n2h_hw.vbat_mv);
			printf("NORD2 HAPTIC LOOKAHEAD: %llu ticks waited for a waveform's peak\n", n2h_strm.lookahead);
			printf("NORD2 HAPTIC PATHS: RAM %llu train %llu (extra clicks %llu) CONT %llu skip %llu none %llu | iOS waveform ms <30 %llu "
			    "<60 %llu <120 %llu <300 %llu >=300 %llu\n", n2h_strm.path_ram, n2h_strm.path_train, n2h_strm.trains, n2h_strm.path_cont,
			    n2h_strm.path_skip, n2h_strm.path_none, n2h_strm.dur_hist[0], n2h_strm.dur_hist[1], n2h_strm.dur_hist[2],
			    n2h_strm.dur_hist[3], n2h_strm.dur_hist[4]);
		}
	}
}

/* nord2_input_prepare_return: other cores are off, no locks */
static void
n2h_force_off(void)
{
	if (n2h_owner_ready && n2h_chip_ok && !n2h_st.latched) {
		(void)n2h_bus_wr(NULL, N2H_REG_GO, 0);
		delay(20000);
		(void)N2H_RUN(&n2h_bus_g, n2h_seq_standby, 0, 0);
	}
	if (n2h_pins_saved) { n2h_pin_restore(N2H_GPIO_RESET, n2h_pin_rst.mux, n2h_pin_rst.dir, n2h_pin_rst.dout); }
}

static void
n2h_post(uint32_t effect, uint32_t level, uint32_t ms)
{
	lck_mtx_lock(n2h_lock);
	n2h_seq = n2h_seq + 1 == 0 ? 2 : n2h_seq + 1;
	if (n2h_seq == 1) { n2h_seq = 2; }                       /* 1 is the boot pulse's */
	__atomic_store_n(&n2h_req_word, n2h_req_pack(n2h_seq, effect, level, ms), __ATOMIC_RELEASE);
	lck_mtx_unlock(n2h_lock);
}

static int
n2hd_open(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	if (!n2h_made || minor(dev) != 0) { return ENXIO; }
	lck_mtx_lock(n2h_lock);
	n2h_opens++;
	lck_mtx_unlock(n2h_lock);
	return 0;
}

static int
n2hd_close(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	if (!n2h_made || minor(dev) != 0) { return ENXIO; }
	n2h_env_g.quiet_us = ni_now_us();      /* the writer is gone (mediaserverd exited): silence now, the motor stops */
	n2h_post(N2H_FX_STOP, 0, 0);
	printf("NORD2 HAPTIC: /dev/redwood-haptic closed (last close): motor stopped\n");
	return 0;
}

/* the HAL's I/O thread: never waits, never fails a well-formed write */
static int
n2hd_write(dev_t dev, struct uio *uio, __unused int ioflag)
{
	int16_t samples[256];
	int err = 0;
	if (!n2h_made || minor(dev) != 0) { return ENXIO; }
	uint64_t now = ni_now_us();
	while (uio_resid(uio) >= 2) {
		user_ssize_t n = uio_resid(uio);
		if (n > (user_ssize_t)sizeof(samples)) { n = sizeof(samples); }
		n &= ~(user_ssize_t)1;
		if ((err = uiomove((char *)samples, (int)n, uio)) != 0) { break; }
		uint32_t pk = n2h_peak_of(samples, (unsigned)(n / 2));
		n2h_env_feed(&n2h_env_g, pk, (unsigned)(n / 2), now);
		if (pk > N2H_SILENT_BELOW) { n2h_env_g.zc += n2h_zc_count(samples, (unsigned)(n / 2), &n2h_env_g.last_sign); }   /* 0611 */
	}
	if (uio_resid(uio) == 1) { (void)uiomove((char *)samples, 1, uio); }
	if (!n2h_announced && n2h_env_g.onsets) {
		n2h_announced = true;
		printf("NORD2 HAPTIC: first waveform from iOS's Actuator (peak %llu of 32768)\n", n2h_env_g.peak);
	}
	return err;
}

static const struct cdevsw n2hd_cdevsw = {
	.d_open = n2hd_open, .d_close = n2hd_close,
	.d_read = eno_rdwrt, .d_write = n2hd_write, .d_ioctl = eno_ioctl,
	.d_stop = eno_stop, .d_reset = eno_reset, .d_ttys = NULL,
	.d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0,
};

static int
n2h_sysctl_cfg SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1)
	if (n2h_lock == NULL) { return ENODEV; }
	lck_mtx_lock(n2h_lock);
	struct n2h_cfg c = n2h_cfg_g;
	lck_mtx_unlock(n2h_lock);
	uint32_t *field = arg2 == 0 ? &c.mode : arg2 == 1 ? &c.drv_max : &c.max_ms;
	int v = (int)*field, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 100000) { return EINVAL; }
	*field = (uint32_t)v;
	n2h_cfg_fix(&c);
	lck_mtx_lock(n2h_lock);
	n2h_cfg_g = c;
	lck_mtx_unlock(n2h_lock);
	printf("NORD2 HAPTIC: sysctl %s: mode %u drvmax %u max_ms %u\n", oidp->oid_name, c.mode, c.drv_max, c.max_ms);
	return 0;
}

static int
n2h_sysctl_test SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1, arg2, oidp)
	int v = (int)n2h_test_last, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v >= N2H_FX_COUNT || v == N2H_FX_RAW || n2h_lock == NULL) { return EINVAL; }
	n2h_test_last = (uint32_t)v;
	n2h_post((uint32_t)v, 0, 0);
	return 0;
}

static int
n2h_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[704];
	snprintf(buf, sizeof(buf), "ram=%u clicks=%llu tick=%llu click=%llu heavy=%llu stale=%llu ram_overruns=%llu f0ram_dHz=%u made=%u owner=%u chip=%u latched=%u phase=%u drv=%u opens=%u writes=%llu frames=%llu onsets=%llu taps=%llu "
	    "runs=%llu capped=%llu starts=%llu stops=%llu throttled=%llu therm=%u bat_cC=%d refused=%llu watchdog=%llu stop_timeouts=%llu "
	    "resets=%llu io_fails=%llu faults=%llu f0_dHz=%u lra_mohm=%u vbat_mv=%u budget_ms=%lld", n2h_ram.state, n2h_strm.clicks,
	    n2h_strm.wave_n[1], n2h_strm.wave_n[2], n2h_strm.wave_n[3], n2h_strm.stale, n2h_ram.overruns, n2h_lay.f0_dhz, n2h_made, n2h_owner_ready, n2h_chip_ok,
	    n2h_st.latched, n2h_hw.phase, n2h_hw.drv, n2h_opens, n2h_env_g.writes, n2h_env_g.frames, n2h_env_g.onsets, n2h_strm.taps,
	    n2h_strm.runs, n2h_strm.capped, n2h_hw.starts, n2h_hw.stops, n2h_st.throttled, n2h_st.therm, n2h_st.therm_cc,
	    n2h_st.therm_refused, n2h_hw.watchdog, n2h_hw.stop_timeouts, n2h_hw.resets, n2h_hw.io_fails, n2h_hw.chip_faults,
	    n2h_hw.f0_last, n2h_hw.lra_mohm, n2h_hw.vbat_mv, n2h_st.budget_us / 1000);
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
SYSCTL_PROC(_kern, OID_AUTO, n2hap, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2h_sysctl_cfg, "I",
    "0610 haptics: 1 allow, 0 motor off and kept off");
SYSCTL_PROC(_kern, OID_AUTO, n2hap_drvmax, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 1, n2h_sysctl_cfg, "I",
    "0610 haptics: AW8697 DRV_LVL at full scale, 18..60 (60 = Android)");
SYSCTL_PROC(_kern, OID_AUTO, n2hap_max_ms, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 2, n2h_sysctl_cfg, "I",
    "0610 haptics: longest continuous run, ms (50..3000, default 1000; then a rest as long)");
SYSCTL_PROC(_kern, OID_AUTO, n2hap_test, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2h_sysctl_test, "I",
    "0610 haptics: play effect N once (1..15, nord2_haptic.h N2H_FX_*)");
SYSCTL_PROC(_kern, OID_AUTO, n2hapstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, n2h_sysctl_stat, "A",
    "0610 haptics state and counters");

static void
nord2_haptic_start(void)
{
	uint32_t on = 1;
	int gdv = 0;
	if (!PE_parse_boot_argn("n2hap", &on, sizeof(on)) && PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & 1024)) { on = 0; }
	n2h_grp = lck_grp_alloc_init("nord2-haptic", LCK_GRP_ATTR_NULL);
	n2h_lock = n2h_grp != NULL ? lck_mtx_alloc_init(n2h_grp, LCK_ATTR_NULL) : NULL;
	if (on == 0 || n2h_lock == NULL) {
		printf("NORD2 HAPTIC: /dev/redwood-haptic not made (boot-arg n2hap=%u / n2gdv +1024): chip untouched\n", on);
		return;
	}
	int major = cdevsw_add(-1, &n2hd_cdevsw);
	if (major < 0) { printf("NORD2 HAPTIC: cdevsw_add failed (%d)\n", major); return; }
	if (devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "redwood-haptic") == NULL) {
		printf("NORD2 HAPTIC: devfs_make_node failed\n");
		(void)cdevsw_remove(major, &n2hd_cdevsw);
		return;
	}
	__atomic_store_n(&n2h_made, true, __ATOMIC_RELEASE);
	printf("NORD2 HAPTIC: /dev/redwood-haptic made (major %d, 0666): iOS Actuator samples -> AW8697 CONT drive <= %u, run <= %u ms, "
	    "budget %u ms at 50 %% refill, thermal half %d C / off %d C\n", major, n2h_cfg_g.drv_max, n2h_cfg_g.max_ms, N2H_BUDGET_MS,
	    N2H_WARM_CC / 100, N2H_HOT_CC / 100);
}
