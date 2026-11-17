/*
 * nord2_cam.inc - the OnePlus Nord 2's cameras, after HakiOS' MediaTek camera work (oosexgang/hakios-mt6768
 * kernel/bsd/dev/redwood_mtkcam.c, hakios-xnu-kernel mtk xnu/files/bsd/dev/redwood_mtkcam_skel.c and
 * docs/camera-mtk-porting.md): power, sensor, receiver + DMA, frame layout, exposure -- in that order.
 *
 * Step T1 (this file now): the ultra-wide IMX355 (D-PHY 4 lanes, plain Bayer, 3264x2448@30: the closest to the
 * X6826B's S5K JN1) powered as the stock kernel does on the production (EVT) board, its chip ID read, the
 * OnePlus GPL kernel's init + preview tables written, streaming on, the frame counter read, everything off.
 *
 * Where each fact comes from (OnePlus kernel android_kernel_oneplus_mt6893, stock dtbo entry 2 = project 20827):
 *   power sequence  OplusImgsensor/imgsensor_hwcfg_20131.c sensor_power_sequence_20817_EVT (IMX355):
 *                   RST low 1 ms, AVDD 2.8 V 2 ms, DVDD 1.2 V 3 ms, DOVDD 1.8 V 1 ms, MCLK 1 ms, RST high 3 ms
 *   AVDD, DVDD      FAN53870 (i2c8 @0x35, GPIO122/123): LDO4 2.8 V (0xB3), LDO2 1.2 V (0x95)
 *                   (imgsensor_hwcfg_custom.c ..._20817_EVB2 table, used for every pcb but EVB; pmic_ldo_set/fan53870.c)
 *   chip enable     dtbo fan53870_gpio_enable = GPIO29 high; "GPIO133 to enable power" for every sensor (not EVB)
 *   DOVDD           MT6359P VCAMIO: LDO_VCAMIO_CON0 0x1c52 bit 0, VCAMIO_ANA_CON0 0x2014 bits 8-11 = 12 (1.8 V)
 *   MCLK            GPIO151 function 1 = CMMCLK2 = topckgen camtg3_sel: CLK_CFG_6 (0x070) bits 0-2 = 1
 *                   (univpll_192m_d8, 24 MHz), gate bit 7, update CLK_CFG_UPDATE (0x004) bit 24 (clk-mt6885.c)
 *   reset           dtbo cam2_rst = GPIO145
 *   sensor I2C      i2c4 (camera_main_two, GPIO139 SCL / GPIO140 SDA), IMX355 write id 0x34 (7-bit 0x1a);
 *                   ID 0x0016/0x0017 = 0x0355; EEPROM 0xA2 (7-bit 0x51)
 *   pins            iocfg_br 0x11D10000: IES +0x70, PU +0xb0 (pinctrl-mt6885.c)
 */
#include "nord2_cam_tables.h"

static vm_offset_t n2c_top;                 /* topckgen 0x10000000 */
static int n2c_chan;
static void
n2c_sleep(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&n2c_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

#include "nord2_seninf.inc"
#include "nord2_camsv.inc"
static bool n2c_seninf_ok;

/* ---- GPIO (0x10005000: DIR 0x000, DOUT 0x100, DIN 0x200, MODE 0x300 4 bits a pin; +4 SET, +8 CLR) ---- */
static void
n2c_mode(unsigned pin, unsigned func)
{
	unsigned reg = 0x300 + pin / 8 * 16, sh = pin % 8 * 4;
	ni_wr(ni_gpio, reg + 8, 0xfu << sh);
	if (func) { ni_wr(ni_gpio, reg + 4, (uint32_t)func << sh); }
}
static void
n2c_out(unsigned pin, bool high)
{
	n2c_mode(pin, 0);
	ni_gpio_bit(pin, 0x100, high);      /* DOUT first */
	ni_gpio_bit(pin, 0x000, true);      /* then DIR out */
}

/* ---- software I2C on two pins (open drain by direction, DOUT 0), as the input thread's buses ---- */
struct n2c_bus { unsigned scl, sda, ies_bits; const char *name; };
static const struct n2c_bus n2c_i2c8 = { 122, 123, (1u << 3) | (1u << 13), "i2c8" };
static const struct n2c_bus n2c_i2c4 = { 139, 140, (1u << 1) | (1u << 11), "i2c4" };
static const struct n2c_bus *n2c_b;

static void n2c_line(unsigned pin, bool high) { ni_gpio_bit(pin, 0x000, !high); delay(3); }
static bool n2c_lvl(unsigned pin) { return !!(ni_rd(ni_gpio, 0x200 + pin / 32 * 16) & (1u << (pin % 32))); }
static bool
n2c_sclh(void)
{
	n2c_line(n2c_b->scl, true);
	for (unsigned i = 0; i < 200; i++) { if (n2c_lvl(n2c_b->scl)) { return true; } delay(1); }
	return false;
}
static void
n2c_bus_init(const struct n2c_bus *b)
{
	n2c_b = b;
	ni_mask(ni_br, 0x70, b->ies_bits, b->ies_bits);   /* input buffers on */
	for (unsigned k = 0; k < 2; k++) {
		unsigned pin = k ? b->sda : b->scl;
		n2c_mode(pin, 0);
		ni_gpio_bit(pin, 0x100, false);
		ni_gpio_bit(pin, 0x000, false);              /* released */
	}
	delay(10);
}
static bool
n2c_start(void)
{
	n2c_line(n2c_b->sda, true);
	if (!n2c_sclh() || !n2c_lvl(n2c_b->sda)) { return false; }
	n2c_line(n2c_b->sda, false);
	n2c_line(n2c_b->scl, false);
	return true;
}
static bool
n2c_stop(void)
{
	n2c_line(n2c_b->scl, false);
	n2c_line(n2c_b->sda, false);
	bool ok = n2c_sclh();
	n2c_line(n2c_b->sda, true);
	return ok && n2c_lvl(n2c_b->sda);
}
static bool
n2c_send(uint8_t v)
{
	for (unsigned i = 0; i < 8; i++, v <<= 1) {
		n2c_line(n2c_b->sda, !!(v & 0x80));
		if (!n2c_sclh()) { return false; }
		n2c_line(n2c_b->scl, false);
	}
	n2c_line(n2c_b->sda, true);
	if (!n2c_sclh()) { return false; }
	bool ack = !n2c_lvl(n2c_b->sda);
	n2c_line(n2c_b->scl, false);
	return ack;
}
static bool
n2c_recv(uint8_t *out, bool last)
{
	uint8_t v = 0;
	n2c_line(n2c_b->sda, true);
	for (unsigned i = 0; i < 8; i++) {
		if (!n2c_sclh()) { return false; }
		v = (uint8_t)(v << 1 | n2c_lvl(n2c_b->sda));
		n2c_line(n2c_b->scl, false);
	}
	n2c_line(n2c_b->sda, last);
	if (!n2c_sclh()) { return false; }
	n2c_line(n2c_b->scl, false);
	n2c_line(n2c_b->sda, true);
	*out = v;
	return true;
}
/* register address of 1 or 2 bytes, data bytes */
static bool
n2c_rd(uint8_t dev, uint16_t reg, unsigned rlen, uint8_t *data, unsigned n)
{
	bool ok = n2c_start() && n2c_send((uint8_t)(dev << 1));
	if (ok && rlen == 2) { ok = n2c_send((uint8_t)(reg >> 8)); }
	ok = ok && n2c_send((uint8_t)reg) && n2c_start() && n2c_send((uint8_t)(dev << 1 | 1));
	for (unsigned i = 0; ok && i < n; i++) { ok = n2c_recv(&data[i], i + 1 == n); }
	bool st = n2c_stop();
	return ok && st;
}
static bool
n2c_wr(uint8_t dev, uint16_t reg, unsigned rlen, uint8_t v)
{
	bool ok = n2c_start() && n2c_send((uint8_t)(dev << 1));
	if (ok && rlen == 2) { ok = n2c_send((uint8_t)(reg >> 8)); }
	ok = ok && n2c_send((uint8_t)reg) && n2c_send(v);
	bool st = n2c_stop();
	return ok && st;
}

/* ---- FAN53870 camera LDOs (i2c8 @0x35) ---- */
#define N2C_FAN 0x35
static bool
n2c_fan_upd(uint8_t reg, uint8_t mask, uint8_t val)
{
	uint8_t cur = 0;
	n2c_bus_init(&n2c_i2c8);
	if (!n2c_rd(N2C_FAN, reg, 1, &cur, 1)) { return false; }
	return n2c_wr(N2C_FAN, reg, 1, (uint8_t)((cur & ~mask) | (val & mask)));
}

/*
 * I2C8 is the audio amplifiers' bus too (audio_i2c8.c drives its hardware controller and refuses to run unless GPIO122/123
 * are SCL8/SDA8; every refusal leaves the speaker offline). So the FAN53870 is set up ONCE, under the audio driver's own
 * lease flag (audio_hw_native.inc nah_native_bus_owned, compare-and-swap 0 -> 2), all the camera LDOs at their voltages
 * and on, the pins given back to function 1; the camera never touches I2C8 again (sensors are held in reset instead).
 */
static volatile unsigned nah_native_bus_owned;      /* tentative definition: audio_hw_native.inc defines it */
static void
n2c_i2c_pins_back(const struct n2c_bus *b)
{
	n2c_mode(b->scl, 1);
	n2c_mode(b->sda, 1);
}
static bool n2c_fan_ready;
static void n2c_fan_dump(void);

/* ---- PMIC (MT6359P) through the input thread, the PMIF's only user: VCAMIO ---- */
static volatile int n2c_vcamio_want = -1, n2c_vcamio_done = -1;
static bool
n2c_pmic_write(uint16_t addr, uint16_t mask, uint16_t value)
{
	uint16_t cur = 0, want;
	if ((addr != 0x1c52 || mask != 0x0001) && (addr != 0x2014 || mask != 0x0f00)) { return false; }
	if (!ni_pmic_read(addr, &cur)) { return false; }
	want = (uint16_t)((cur & ~mask) | (value & mask));
	if (want == cur) { return true; }
	if (!nw_wait(ni_pwrap, 0xca8, 0x800e, 0x8000)) { return false; }
	ni_pwrap_owned = true;
	ni_wr(ni_pwrap, 0xc84, want);
	ni_wr(ni_pwrap, 0xc80, (1u << 29) | addr);
	if (!nw_wait(ni_pwrap, 0xca8, 0x800e, 0x8000)) { return false; }
	return ni_pmic_read(addr, &cur) && cur == want;
}
static void
n2c_pmic_service(void)       /* called from the input loop */
{
	int w = n2c_vcamio_want;
	uint16_t con = 0, ana = 0;
	if (w < 0) { return; }
	n2c_vcamio_want = -1;
	bool ok = w ? (n2c_pmic_write(0x2014, 0x0f00, 12u << 8) && n2c_pmic_write(0x1c52, 0x0001, 1)) :
	    n2c_pmic_write(0x1c52, 0x0001, 0);
	(void)ni_pmic_read(0x1c52, &con); (void)ni_pmic_read(0x2014, &ana);
	printf("NORD2 CAM: VCAMIO %s: %s - CON0 %#x ANA_CON0 %#x\n", w ? "on 1.8 V" : "off", ok ? "ok" : "FAILED", con, ana);
	n2c_vcamio_done = ok ? w : -2;
}
static bool
n2c_vcamio(int on)
{
	n2c_vcamio_done = -1;
	n2c_vcamio_want = on;
	for (unsigned i = 0; i < 200 && n2c_vcamio_done == -1; i++) { n2c_sleep(5); }
	return n2c_vcamio_done == on;
}

/* ---- the sensors ---- */
enum { N2C_IMX766, N2C_IMX615, N2C_IMX355, N2C_GC02M1B, N2C_NSENSORS };
struct n2c_mclk { unsigned pin, cfg, shift, upd; };     /* CMMCLKn pin, CLK_CFG_n offset, sel field shift (gate = shift+7), update bit */
struct n2c_sensor {
	const char *name;
	const struct n2c_bus *bus;
	uint8_t addr, reg8;              /* 7-bit address; reg8: 8-bit register addresses (GC) */
	uint16_t idreg_hi, idreg_lo, id;
	unsigned rst;
	struct n2c_mclk mclk;
	const uint16_t *init;
	unsigned init_n;
	const uint16_t *mode;
	unsigned mode_n;
	uint8_t mirror;                  /* 0x0101 */
	bool cphy; int lanes; int64_t pixel_rate;   /* the receiver: C-PHY trios or D-PHY lanes, the preview mode's mipi_pixel_rate */
	int port; uint32_t w, h;                     /* its CSI port (q10g3: found by the SENINF scan; -1 unknown), the preview size */
};
#define N2C_TAB(a) a, (unsigned)(sizeof(a) / sizeof(a[0]))
static const struct n2c_bus n2c_i2c2 = { 141, 142, (1u << 0) | (1u << 10), "i2c2" };
static const struct n2c_sensor n2c_sensors[N2C_NSENSORS] = {
	/* main: camera_main i2c2 0x20, MCLK GPIO150 = CMMCLK1 = camtg2 (CLK_CFG_5 bits 24-26, update bit 23), RST GPIO144 */
	[N2C_IMX766] = { "IMX766 (main)", &n2c_i2c2, 0x10, 0, 0x0016, 0x0017, 0x0766, 144, { 150, 0x60, 24, 23 },
	    N2C_TAB(n2c_imx766_init), N2C_TAB(n2c_imx766_preview), 0x03, true, 3, 787200000, CSI_PORT_2, 4096, 3072 },
	/* front: camera_sub i2c4 0x20, MCLK GPIO149 = CMMCLK0 = camtg (CLK_CFG_5 bits 16-18, update bit 22), RST GPIO143 */
	[N2C_IMX615] = { "IMX615 (front)", &n2c_i2c4, 0x10, 0, 0x0016, 0x0017, 0x0615, 143, { 149, 0x60, 16, 22 },
	    N2C_TAB(n2c_imx615_init), N2C_TAB(n2c_imx615_preview), 0x00, false, 4, 567410000, 0, 3264, 2464 },   /* 3264 of its 3280 columns, from 8 */
	/* ultra-wide: camera_main_two i2c4 0x34, MCLK GPIO151 = CMMCLK2 = camtg3 (CLK_CFG_6 bits 0-2, update bit 24), RST GPIO145 */
	[N2C_IMX355] = { "IMX355 (ultra-wide)", &n2c_i2c4, 0x1a, 0, 0x0016, 0x0017, 0x0355, 145, { 151, 0x70, 0, 24 },
	    N2C_TAB(n2c_imx355_init), N2C_TAB(n2c_imx355_preview), 0x03, false, 4, 280800000, 1, 3264, 2448 },
	/* mono depth: camera_main_three i2c4 0x6e, MCLK GPIO136 = CMMCLK3 = camtg4 (CLK_CFG_6 bits 8-10, update bit 25), RST GPIO130 */
	[N2C_GC02M1B] = { "GC02M1B (mono)", &n2c_i2c4, 0x37, 1, 0xf0, 0xf1, 0x02e0, 130, { 136, 0x70, 8, 25 },
	    NULL, 0, NULL, 0, 0, false, 1, 67200000, -1, 1600, 1200 },
};
static const struct n2c_sensor *n2c_s;

static bool
n2c_s_wr(uint16_t reg, uint8_t v)
{
	n2c_bus_init(n2c_s->bus);
	return n2c_wr(n2c_s->addr, reg, n2c_s->reg8 ? 1 : 2, v);
}
static int
n2c_s_rd(uint16_t reg)
{
	uint8_t v = 0;
	n2c_bus_init(n2c_s->bus);
	return n2c_rd(n2c_s->addr, reg, n2c_s->reg8 ? 1 : 2, &v, 1) ? v : -1;
}
static unsigned
n2c_s_table(const uint16_t *t, unsigned words)
{
	unsigned nak = 0;
	n2c_bus_init(n2c_s->bus);
	for (unsigned i = 0; i + 1 < words; i += 2) {
		if (words > 600 && (i / 2) % 32 == 0) { printf("NORD2 CAM: %s table write %u: [%#06x] <- %#04x\n", n2c_s->name, i / 2, t[i], t[i + 1]); }
		if (!n2c_wr(n2c_s->addr, t[i], 2, (uint8_t)t[i + 1])) { nak++; }
	}
	return nak;
}
static void
n2c_mclk(const struct n2c_mclk *m, bool on)
{
	if (on) {
		ni_wr(n2c_top, m->cfg + 8, 0x87u << m->shift);      /* CLR: sel and gate */
		ni_wr(n2c_top, m->cfg + 4, 0x01u << m->shift);      /* SET: univpll_192m_d8 = 24 MHz */
		ni_wr(n2c_top, 0x04, 1u << m->upd);
		n2c_mode(m->pin, 1);
	} else {
		n2c_out(m->pin, false);
		ni_wr(n2c_top, m->cfg + 4, 0x80u << m->shift);      /* gate */
		ni_wr(n2c_top, 0x04, 1u << m->upd);
	}
}
/* a FAN53870 LDO: vout register, value, sequence register and mask, enable bit (pmic_ldo_set/fan53870.c) */
struct n2c_ldo { uint8_t vreg, val, seq, seqmask, en; const char *what; };
static const struct n2c_ldo n2c_ldo1_1v1 = { 0x04, 0x89, 0x0b, 0x07, 0x01, "LDO1 1.1 V" };
static const struct n2c_ldo n2c_ldo2_1v2 = { 0x05, 0x95, 0x0b, 0x38, 0x02, "LDO2 1.2 V" };
static const struct n2c_ldo n2c_ldo3_2v8 = { 0x06, 0xb3, 0x0c, 0x07, 0x04, "LDO3 2.8 V" };
static const struct n2c_ldo n2c_ldo4_2v8 = { 0x07, 0xb3, 0x0c, 0x38, 0x08, "LDO4 2.8 V" };
static const struct n2c_ldo n2c_ldo6_2v8 = { 0x09, 0xb3, 0x0d, 0x38, 0x20, "LDO6 2.8 V" };
static const struct n2c_ldo n2c_ldo7_2v9 = { 0x0a, 0xbf, 0x0e, 0x07, 0x40, "LDO7 2.9 V" };
static bool
n2c_ldo_raw(const struct n2c_ldo *l)
{
	bool ok = n2c_wr(N2C_FAN, l->vreg, 1, l->val) && n2c_fan_upd(l->seq, l->seqmask, 0) && n2c_fan_upd(0x03, l->en, l->en);
	printf("NORD2 CAM:   FAN %s %s\n", l->what, ok ? "on" : "FAILED");
	return ok;
}
static bool
n2c_fan_setup(void)
{
	bool ok = true;
	for (unsigned tries = 0; tries < 200 && !__sync_bool_compare_and_swap(&nah_native_bus_owned, 0, 2); tries++) { n2c_sleep(10); }
	if (nah_native_bus_owned != 2) { printf("NORD2 CAM: I2C8 is the audio driver's now: FAN53870 not set up\n"); return false; }
	n2c_bus_init(&n2c_i2c8);
	printf("NORD2 CAM: FAN53870 (I2C8 leased from audio): all camera LDOs on\n");
	n2c_fan_dump();
	ok = n2c_ldo_raw(&n2c_ldo1_1v1) && ok; ok = n2c_ldo_raw(&n2c_ldo2_1v2) && ok; ok = n2c_ldo_raw(&n2c_ldo3_2v8) && ok;
	ok = n2c_ldo_raw(&n2c_ldo4_2v8) && ok; ok = n2c_ldo_raw(&n2c_ldo6_2v8) && ok; ok = n2c_ldo_raw(&n2c_ldo7_2v9) && ok;
	n2c_fan_dump();
	n2c_i2c_pins_back(&n2c_i2c8);
	__sync_bool_compare_and_swap(&nah_native_bus_owned, 2, 0);
	n2c_fan_ready = ok;
	return ok;
}
static bool __attribute__((unused))
n2c_ldo(const struct n2c_ldo *l, bool on)
{
	n2c_bus_init(&n2c_i2c8);
	if (!on) { return n2c_fan_upd(0x03, l->en, 0); }
	bool ok = n2c_wr(N2C_FAN, l->vreg, 1, l->val) && n2c_fan_upd(l->seq, l->seqmask, 0) && n2c_fan_upd(0x03, l->en, l->en);
	printf("NORD2 CAM:   FAN %s %s\n", l->what, ok ? "on" : "FAILED");
	return ok;
}
static void
n2c_fan_dump(void)
{
	uint8_t r[0x0f] = {0};
	n2c_bus_init(&n2c_i2c8);
	bool ok = n2c_rd(N2C_FAN, 0x00, 1, r, sizeof(r));
	printf("NORD2 CAM:   FAN %s: id %02x %02x %02x en %02x vout %02x %02x %02x %02x %02x %02x %02x seq %02x %02x %02x %02x\n", ok ? "ok" : "NO ACK",
	    r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14]);
}

/*
 * Power, as sensor_power_sequence_20817_EVT (imgsensor_hwcfg_20131.c) with the regulators of the non-EVB boards
 * (Oplusimgsensor_power_fan53870_20817_EVB2) and the GPIO rails of the dtbo (cam0_ldo_vcama GPIO146, cam0_ldo_vcamd
 * GPIO133, cam0_ldo_vcamd1 GPIO186, cam0_ldo_vcamiso1 GPIO6). VCAMIO and GPIO133 are shared: on once, off at the end.
 */
static bool
n2c_power(unsigned which, bool on)
{
	const struct n2c_sensor *s = &n2c_sensors[which];
	bool ok = true;
	if (!on) {
		n2c_out(s->rst, false);
		n2c_mclk(&s->mclk, false);
		switch (which) {
		case N2C_IMX766: n2c_out(186, false); n2c_out(146, false); n2c_out(6, false); break;
		case N2C_IMX615: break;
		case N2C_IMX355: break;
		case N2C_GC02M1B: break;
		}
		printf("NORD2 CAM: %s off\n", s->name);
		return true;
	}
	printf("NORD2 CAM: %s power on\n", s->name);
	n2c_out(s->rst, false);
	n2c_sleep(1);
	switch (which) {
	case N2C_IMX766:
		n2c_out(6, true); n2c_sleep(1);                         /* VOIS_1 */
		ok = n2c_fan_ready; n2c_sleep(1);                       /* AFVDD (FAN LDO6, on since setup) */
		n2c_sleep(4);                                           /* AVDD_1 (FAN LDO3) */
		n2c_out(146, true); n2c_sleep(2);                       /* AVDD (GPIO rail) */
		n2c_out(133, true); n2c_sleep(2);                       /* DVDD */
		n2c_out(186, true); n2c_sleep(4);                       /* DVDD_1 */
		break;
	case N2C_IMX615:
		ok = n2c_fan_ready; n2c_sleep(6);                       /* AVDD LDO7, DVDD LDO1 (on since setup) */
		break;
	case N2C_IMX355:
		ok = n2c_fan_ready; n2c_sleep(5);                       /* AVDD LDO4, DVDD LDO2 (on since setup) */
		break;
	case N2C_GC02M1B:
		ok = n2c_fan_ready; n2c_sleep(8);                       /* AVDD LDO4 (on since setup) */
		break;
	}
	n2c_mclk(&s->mclk, true);
	n2c_sleep(5);
	n2c_out(s->rst, true);
	n2c_sleep(8);
	return ok;
}

static void __attribute__((unused))
n2c_t1_sensor(unsigned which)
{
	n2c_s = &n2c_sensors[which];
	if (which == N2C_IMX766 && n2c_seninf_ok) {      /* q10gg: its receiver before its power, then 8 s of nothing */
		printf("NORD2 CAM: IMX766 STEP receiver first, sensor still off\n"); n2c_sleep(300);
		n2s_rx(n2c_s->port, n2c_s->cphy, n2c_s->lanes, n2c_s->pixel_rate);
		for (unsigned k = 0; k < 6; k++) { printf("NORD2 CAM: IMX766 receiver set, sensor off: alive %u ms\n", k * 500); n2c_sleep(500); }
	}
	if (!n2c_power(which, true)) { printf("NORD2 CAM: %s rails FAILED\n", n2c_s->name); }
	printf("NORD2 CAM: %s STEP powered\n", n2c_s->name); n2c_sleep(300);
	if (which == N2C_IMX766) { for (unsigned k = 0; k < 16; k++) { printf("NORD2 CAM: IMX766 powered, untouched: alive %u ms\n", 300 + k * 500); n2c_sleep(500); } }
	n2c_bus_init(n2c_s->bus);
	printf("NORD2 CAM: %s bus %s idle SCL %d SDA %d\n", n2c_s->name, n2c_s->bus->name, n2c_lvl(n2c_s->bus->scl), n2c_lvl(n2c_s->bus->sda));
	int hi = n2c_s_rd(n2c_s->idreg_hi), lo = n2c_s_rd(n2c_s->idreg_lo);
	bool idok = hi == (n2c_s->id >> 8) && lo == (n2c_s->id & 0xff);
	printf("NORD2 CAM: %s chip id %#x %#x (want %#x): %s\n", n2c_s->name, hi, lo, n2c_s->id, idok ? "OK" : "WRONG");
	if (idok && n2c_s->init) {
		unsigned nak = n2c_s_table(n2c_s->init, n2c_s->init_n);
		printf("NORD2 CAM: %s STEP init table done\n", n2c_s->name); n2c_sleep(300);
		(void)n2c_s_wr(0x0101, n2c_s->mirror);
		n2c_sleep(5);
		unsigned nak2 = n2c_s_table(n2c_s->mode, n2c_s->mode_n);
		printf("NORD2 CAM: %s tables: init %u writes (%u NAK), preview %u writes (%u NAK); frame_len %d/%d line_len %d/%d out %dx%d\n",
		    n2c_s->name, n2c_s->init_n / 2, nak, n2c_s->mode_n / 2, nak2, n2c_s_rd(0x0340), n2c_s_rd(0x0341), n2c_s_rd(0x0342),
		    n2c_s_rd(0x0343), n2c_s_rd(0x034c) << 8 | n2c_s_rd(0x034d), n2c_s_rd(0x034e) << 8 | n2c_s_rd(0x034f));
		printf("NORD2 CAM: %s STEP tables done\n", n2c_s->name); n2c_sleep(300);
		n2v_xoff = which == N2C_IMX615 ? 8 : 0;
		bool armed = n2c_seninf_ok && n2c_s->port >= 0 &&     /* the receiver and CAMSV first, then the stream */
		    n2v_start(n2c_s->name, n2c_s->port, n2c_s->cphy, n2c_s->lanes, n2c_s->pixel_rate, n2c_s->w, n2c_s->h);
		printf("NORD2 CAM: %s STEP armed %d, stream on next\n", n2c_s->name, armed); n2c_sleep(300);
		(void)n2c_s_wr(0x0100, 0x01);
		printf("NORD2 CAM: %s STEP stream on written\n", n2c_s->name);
		n2c_sleep(60);
		printf("NORD2 CAM: %s streaming: frame count %d\n", n2c_s->name, n2c_s_rd(0x0005));
		if (armed) { n2v_finish(n2c_s->name, n2c_s->w, n2c_s->h); }
		printf("NORD2 CAM: %s frame count %d\n", n2c_s->name, n2c_s_rd(0x0005));
		(void)n2c_s_wr(0x0100, 0x00);
		n2c_sleep(50);
		printf("NORD2 CAM: %s stream off: mode %d frame count %d\n", n2c_s->name, n2c_s_rd(0x0100), n2c_s_rd(0x0005));
	}
	if (which == N2C_IMX766) {                       /* its lens actuator and EEPROM on the same bus */
		uint8_t ee[8] = {0}, af = 0;
		bool e = n2c_rd(0x50, 0x0000, 2, ee, 8), a = n2c_rd(0x0c, 0x00, 1, &af, 1);
		printf("NORD2 CAM: i2c2 EEPROM 0x50 %s %02x %02x %02x %02x %02x %02x %02x %02x; AF 0x0c %s %02x\n", e ? "ok" : "NAK",
		    ee[0], ee[1], ee[2], ee[3], ee[4], ee[5], ee[6], ee[7], a ? "ok" : "NAK", af);
	}
	(void)n2c_power(which, false);
}

static void __attribute__((unused))
n2c_t1(void)
{
	n2c_top = ml_io_map_unmappable(0x10000000, 0x1000, VM_WIMG_IO);
	printf("NORD2 CAM: GPIO29 (FAN53870 enable, also the panel bias: only ever high) and GPIO133 (camera power enable) high\n");
	if (!n2c_lvl(29)) { n2c_out(29, true); }
	n2c_out(133, true);
	n2c_sleep(5);
	(void)n2c_fan_setup();
	printf("NORD2 CAM: DOVDD: VCAMIO 1.8 V\n");
	if (!n2c_vcamio(1)) { printf("NORD2 CAM: VCAMIO FAILED\n"); return; }
	n2c_sleep(2);
	printf("NORD2 CAM: T2a: CAM power domain, clocks, SENINF\n");
	n2c_seninf_ok = n2s_init();
	static const unsigned order[] = { N2C_IMX766, N2C_IMX355, N2C_IMX615 };
	for (unsigned k = 0; k < 3; k++) { n2c_t1_sensor(order[k]); }
	(void)n2c_vcamio(0);
	printf("NORD2 CAM: all off\n");
}

#ifndef N2C_TEST
#define N2C_TEST 0      /* daily image: /dev/redwood-camera */
#endif
static void __attribute__((unused))
n2c_test_reset(void)
{
	vm_offset_t wdt = ml_io_map_unmappable(0x10007000, 0x1000, VM_WIMG_IO);
	printf("NORD2 CAM: test over: reset into fastboot in 3 s\n");
	n2c_sleep(3000);                              /* the USB console drains */
	if (!wdt) { return; }
	ni_wr(wdt, 0x00, (ni_rd(wdt, 0x00) & ~(1u << 3)) | 0x22000000u);
	for (;;) { ni_wr(wdt, 0x14, 0x1209); delay(5000); }   /* TOPRGU SWRST, as sweet_reset_now() */
}

static void __attribute__((unused))
n2c_thread(void *arg __unused, wait_result_t wr __unused)
{
	n2c_sleep(30000);          /* after the boot's own work */
	printf("NORD2 CAM: T1 bring-up of the four sensors\n");
	n2c_t1();
	for (unsigned s = 0; s < 60; s += 5) { printf("NORD2 CAM: alive %u s after the captures\n", s); n2c_sleep(5000); }
	printf("NORD2 CAM: T1 done\n");
	if (N2C_TEST) { n2c_test_reset(); }
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

#include "nord2_camdev.inc"

static void
nord2_cam_start(void)
{
	thread_t thread;
	/* TEST image: the bring-up test, then reset; daily image: /dev/redwood-camera */
	if (kernel_thread_start(N2C_TEST ? n2c_thread : n2d_thread, NULL, &thread) == KERN_SUCCESS) { thread_deallocate(thread); }
}
