/* Host test for nord2_slider.inc: byte-level fake I2C9 with IST8801 / MXM1120 register models. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

enum { NS_SCL = 120, NS_SDA = 121 };
static unsigned ni_scl, ni_sda, ni_stage;
static int bus_idle = 1;

/* fake devices */
struct dev { uint8_t addr, did; uint8_t reg[256]; int16_t field; int present; int writes; };
static struct dev devs[4];
static int chip_kind; /* 1 IST, 2 MXM */
static struct dev *find(uint8_t a) { for (int i = 0; i < 4; i++) if (devs[i].present && devs[i].addr == a) return &devs[i]; return NULL; }
static void refresh(struct dev *d) {
	if (chip_kind == 1) { d->reg[0x10] = (d->reg[0x08] & 0xf0) ? 0x01 : 0x00; d->reg[0x11] = (uint8_t)d->field; d->reg[0x12] = (uint8_t)((uint16_t)d->field >> 8); }
	else { int x = d->field; d->reg[0x10] = (d->reg[0x08] & 1) ? 1 : 0; uint16_t u = (uint16_t)(x & 0x3ff); d->reg[0x11] = (uint8_t)u; d->reg[0x12] = (uint8_t)(((u >> 8) & 1) << 6 | (x < 0 ? 0x80 : 0)); }
}
/* write transaction state */
static struct dev *cur; static int wstate; static uint8_t wreg;
static bool ni_start(void) { if (!bus_idle) return false; wstate = 0; cur = NULL; return true; }
static bool ni_send(uint8_t b) {
	if (wstate == 0) { cur = find(b >> 1); wstate = 1; return cur != NULL; }
	if (wstate == 1) { wreg = b; wstate = 2; return true; }
	cur->reg[wreg] = b; cur->writes++; wreg++; return true;
}
static bool ni_stop(void) { return true; }
static bool ni_bus_read(uint8_t addr, uint8_t reg, uint8_t *data, unsigned len) {
	if (!bus_idle) return false;
	struct dev *d = find(addr); if (!d) { ni_stage = 2; return false; }
	refresh(d);
	for (unsigned i = 0; i < len; i++) data[i] = d->reg[(uint8_t)(reg + i)];
	return true;
}
static void ni_wait(unsigned ms) { (void)ms; }
static int ni_level(unsigned pin) { (void)pin; return bus_idle; }
static int ringer_calls, ringer_last = -1;
static void sweet_hid_ringer(int on) { ringer_calls++; ringer_last = on; }
#define extern static
#include "../b/bsd/dev/nord2_slider.inc"
#undef extern

static void reset_world(int kind, int present) {
	memset(devs, 0, sizeof(devs)); chip_kind = kind;
	ns_chip = 0; ns_valid = 0; ns_pos = -1; ns_cand = -1; ns_ring = -1; ns_errors = 0; ns_enabled = 1;
	ringer_calls = 0; ringer_last = -1;
	uint8_t up = kind == 1 ? 0x18 : 0x0d, dn = kind == 1 ? 0x19 : 0x0c, did = kind == 1 ? 0x81 : 0x9c;
	devs[0] = (struct dev){.addr = up, .present = present}; devs[1] = (struct dev){.addr = dn, .present = present};
	devs[0].reg[0x09] = devs[1].reg[0x09] = did;
}
static void set_fields(int up, int dn) { devs[0].field = (int16_t)up; devs[1].field = (int16_t)dn; }

int main(void) {
	for (int kind = 1; kind <= 2; kind++) {
		/* boot at bottom (ring): up hall weak, down hall strong */
		reset_world(kind, 1); set_fields(10, 120);
		ns_probe();
		assert(ns_chip == (unsigned)kind);
		assert(ringer_calls == 1 && ringer_last == 1 && ns_pos == NS_BOTTOM);
		if (kind == 1) { assert(devs[0].reg[0x08] == 0x70 && devs[0].reg[0x0d] == 0x2c && devs[0].reg[0x01] == 0 && (devs[0].reg[0x40] & 4)); }
		else { assert(devs[0].reg[0x08] == 0x41 && devs[0].reg[0x01] == 0 && devs[0].reg[0x00] == 0xf0); }
		/* move to middle: one sample is not enough, two publish silent */
		set_fields(40, 45); ns_poll(); assert(ringer_calls == 1); ns_poll(); assert(ringer_calls == 2 && ringer_last == 0 && ns_pos == NS_MIDDLE);
		/* glitch: one top sample then back to middle -> no event */
		set_fields(150, 5); ns_poll(); set_fields(40, 45); ns_poll(); ns_poll(); assert(ringer_calls == 2);
		/* top: still silent -> position tracked, no new event */
		set_fields(150, 5); ns_poll(); ns_poll(); assert(ringer_calls == 2 && ringer_last == 0 && ns_pos == NS_TOP);
		/* negative fields (magnet polarity): Android negates both */
		set_fields(-150, -5); ns_poll(); ns_poll(); assert(ns_pos == NS_TOP);
		set_fields(-5, -150); ns_poll(); ns_poll(); assert(ns_pos == NS_BOTTOM && ringer_last == 1 && ringer_calls == 3);
		/* bus error keeps state */
		bus_idle = 0; ns_poll(); bus_idle = 1; assert(ns_pos == NS_BOTTOM && ns_errors == 1);
		/* boot at top: initial silent published */
		reset_world(kind, 1); set_fields(150, 5); ns_probe(); assert(ringer_calls == 1 && ringer_last == 0);
		/* no chips: off, no events */
		reset_world(kind, 0); ns_probe(); assert(ns_chip == 0 && ringer_calls == 0); ns_poll(); assert(ringer_calls == 0);
		printf("kind %d ok\n", kind);
	}
	/* disabled */
	reset_world(1, 1); ns_enabled = 0; ns_probe(); assert(ns_chip == 0 && devs[0].writes == 0);
	/* bus stuck low */
	reset_world(1, 1); bus_idle = 0; ns_probe(); assert(ns_chip == 0 && devs[0].writes == 0); bus_idle = 1;
	/* MXM 10-bit decode */
	reset_world(2, 1); ns_chip = 2; ns_addr[0] = 0x0d; devs[0].reg[0x08] = 0x41;
	int vals[] = {0, 1, 255, 256, 511, -1, -256, -512};
	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) { devs[0].field = (int16_t)vals[i]; ns_valid = 0; assert(ns_read_hall(0)); assert(ns_value[0] == vals[i]); }
	printf("all ok\n");
	return 0;
}
