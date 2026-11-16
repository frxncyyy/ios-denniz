/*
 * Nord2 alert slider (OPLUS tri-state key, project 20827) as the iOS
 * ring/silent switch. Included by sweet_touch.c; runs only on the NORD2
 * INPUT thread, which owns every software-I2C pin (ni_scl/ni_sda).
 *
 * Hardware, from the Android denniz kernel:
 *  - cust_mt6893_oplus_tri_state_key_20827.dtsi: two hall sensors on I2C9
 *    (GPIO120 = SCL9, GPIO121 = SDA9), either IST8801 (up 0x18, down 0x19,
 *    DID 0x81) or MagnaChip MXM1120 (up 0x0D, down 0x0C, DID 0x9C). The
 *    driver probes both pairs; only the fitted pair answers. INT outputs
 *    on GPIO12/GPIO13 are not used: we poll with interrupts disabled.
 *    Supplies VIO28 (vdd) and VIO18 (vio) are not switched here; a NACK
 *    from every address means absent or unpowered, and the slider stays off.
 *  - hall_ist8801_up.c reset_device/set_operation_mode/set_sensitivity("40mT")
 *    and hall_mxm1120_up.c reset_device: the init sequences below, with
 *    INTSRS interrupt-off (polling mode, same sensitivity bits).
 *  - oplus_tri_key.c reboot_get_position(): delta = up - down (both negated
 *    when either is negative); delta > 30 top, delta < -30 bottom, else middle.
 *    OnePlus: top silent, middle vibrate, bottom ring (UP_STATE reports 1).
 * iOS: top and middle -> silent, bottom -> ringer on (sweet_hid_ringer).
 * Boot-arg nord2slider=0 leaves GPIO120/121 and the sensors untouched.
 */
extern void sweet_hid_ringer(int);

enum { NS_TOP = 0, NS_BOTTOM = 1, NS_MIDDLE = 2 };	/* oplus enum tri_key_position */
static const char *const ns_names[] = {"top/silent", "bottom/ring", "middle/vibrate"};
static int ns_enabled = 1;
static unsigned ns_chip;		/* 0 none, 1 IST8801, 2 MXM1120 */
static uint8_t ns_addr[2];		/* [0] up hall (DHALL_1), [1] down hall (DHALL_0) */
static int16_t ns_value[2];
static unsigned ns_valid;
static int ns_pos = -1, ns_cand = -1, ns_ring = -1;
static unsigned ns_errors;

static void
ns_bus(void)
{
	ni_scl = NS_SCL;
	ni_sda = NS_SDA;
}

static bool
ns_rd(uint8_t addr, uint8_t reg, uint8_t *data, unsigned len)
{
	ns_bus();
	return ni_bus_read(addr, reg, data, len);
}

static bool
ns_wr(uint8_t addr, uint8_t reg, uint8_t value)
{
	ns_bus();
	bool ok = ni_start() && ni_send((uint8_t)(addr << 1)) && ni_send(reg) && ni_send(value);
	bool stopped = ni_stop();
	return ok && stopped;
}

static bool
ns_ist_init(uint8_t a)
{
	uint8_t id = 0, ifc = 0, info = 0;
	if (!ns_rd(a, 0x09, &id, 1) || id != 0x81) { return false; }
	if (!ns_wr(a, 0x07, 0x01)) { return false; }	/* SRST */
	ni_wait(20);
	if (!ns_rd(a, 0x09, &id, 1) || id != 0x81) { return false; }
	bool ok = ns_wr(a, 0x76, 0x04) &&		/* TSTCNTL */
	    ns_wr(a, 0x6c, 0x05) &&			/* OSRCNTL */
	    ns_rd(a, 0x87, &info, 1) &&			/* INFO, log only (40mT row is the same) */
	    ns_wr(a, 0x00, 0x20) &&			/* PERSINT count 2 */
	    ns_wr(a, 0x01, 0x00) &&			/* INTSRS: interrupt off, range 0 */
	    ns_wr(a, 0x20, 0x00);			/* ACTION */
	if (!ok) { return false; }
	ni_wait(5);
	ok = ns_wr(a, 0x08, 0x00) &&			/* OPF standby */
	    ns_rd(a, 0x40, &ifc, 1) && ns_wr(a, 0x40, (uint8_t)(ifc | 0x04)) &&	/* IFCNTL */
	    ns_wr(a, 0x0d, 0x2c) &&			/* CNTL2: 40mT = GAIN x2, 10-bit ADC */
	    ns_wr(a, 0x08, 0x70);			/* OPF: 20 Hz continuous */
	printf("NORD2 SLIDER: IST8801 %#x info=%#x ifcntl=%#x ok=%d\n", a, info, ifc, ok);
	return ok;
}

static bool
ns_mxm_init(uint8_t a)
{
	uint8_t id = 0;
	if (!ns_rd(a, 0x09, &id, 1) || id != 0x9c) { return false; }
	if (!ns_wr(a, 0x07, 0x01)) { return false; }	/* SRST */
	ni_wait(20);
	if (!ns_rd(a, 0x09, &id, 1) || id != 0x9c) { return false; }
	bool ok = ns_wr(a, 0x00, 0xf0) &&		/* PERSINT 15 << 4 */
	    ns_wr(a, 0x01, 0x00) &&			/* INTSRS: interrupt off, 10-bit 0.068 mT */
	    ns_wr(a, 0x08, 0x40) &&			/* OPF: 80 Hz, 10-bit */
	    ns_wr(a, 0x08, 0x41);			/* measurement (HSSON) */
	printf("NORD2 SLIDER: MXM1120 %#x ok=%d\n", a, ok);
	return ok;
}

static bool
ns_read_hall(unsigned i)
{
	uint8_t b[3] = {0};
	if (!ns_rd(ns_addr[i], 0x10, b, 3)) { return false; }
	if (ns_chip == 1) {
		if (b[0] & 0x05) {			/* ST1 DRDY or overrun: hall_ist8801_up.c get_data */
			ns_value[i] = (int16_t)((b[2] << 8) | b[1]);
			ns_valid |= 1u << i;
		}
	} else if (b[0] & 0x01) {			/* m1120_2byte_to_short, 10-bit */
		int x = ((b[2] & 0x40) ? 0x100 : 0) | b[1];
		if (b[2] & 0x80) { x -= 0x200; }
		ns_value[i] = (int16_t)x;
		ns_valid |= 1u << i;
	}
	return true;
}

static int
ns_decode(int16_t up, int16_t down)
{
	int16_t delta = (up < 0 || down < 0) ? (int16_t)(-up - -down) : (int16_t)(up - down);
	return delta > 30 ? NS_TOP : (-delta > 30 ? NS_BOTTOM : NS_MIDDLE);
}

/* One sample of both halls. A position counts after two equal decodes in a
 * row; iOS gets an event for the first one and whenever ring/silent changes
 * (top <-> middle is silent -> silent: logged, no event). */
static void
ns_poll(void)
{
	if (!ns_chip) { return; }
	if (!ns_read_hall(0) || !ns_read_hall(1)) {
		if ((ns_errors++ % 100) == 0) {
			printf("NORD2 SLIDER: read error stage=%u scl=%d sda=%d\n",
			    ni_stage, ni_level(NS_SCL), ni_level(NS_SDA));
		}
		return;
	}
	ns_errors = 0;
	if (ns_valid != 3) { return; }
	int pos = ns_decode(ns_value[0], ns_value[1]);
	if (pos != ns_cand) { ns_cand = pos; return; }
	if (pos == ns_pos) { return; }
	bool initial = ns_pos < 0;
	int ring = pos == NS_BOTTOM;
	ns_pos = pos;
	printf("NORD2 SLIDER: %s%s up=%d down=%d -> iOS ringer %s\n", initial ? "initial " : "",
	    ns_names[pos], ns_value[0], ns_value[1], ring ? "on" : "silent");
	if (ring != ns_ring) {
		ns_ring = ring;
		sweet_hid_ringer(ring);
	}
}

/* After the pins are GPIO inputs with DOUT 0 and IES set. */
static void
ns_probe(void)
{
	if (!ns_enabled) {
		printf("NORD2 SLIDER: disabled (nord2slider=0)\n");
		return;
	}
	if (!ni_level(NS_SCL) || !ni_level(NS_SDA)) {
		printf("NORD2 SLIDER: I2C9 not idle scl=%d sda=%d; slider off\n",
		    ni_level(NS_SCL), ni_level(NS_SDA));
		return;
	}
	if (ns_ist_init(0x18) && ns_ist_init(0x19)) {
		ns_chip = 1; ns_addr[0] = 0x18; ns_addr[1] = 0x19;
	} else if (ns_mxm_init(0x0d) && ns_mxm_init(0x0c)) {
		ns_chip = 2; ns_addr[0] = 0x0d; ns_addr[1] = 0x0c;
	} else {
		printf("NORD2 SLIDER: no IST8801 (0x18/0x19) or MXM1120 (0x0d/0x0c) pair; stage=%u slider off\n",
		    ni_stage);
		return;
	}
	ni_wait(120);			/* first conversions (20 Hz / 80 Hz) */
	ns_poll();
	ni_wait(60);
	ns_poll();			/* second equal decode publishes the boot state */
	printf("NORD2 SLIDER: %s up=%#x down=%#x, polled every 100 ms, state=%d\n",
	    ns_chip == 1 ? "IST8801" : "MXM1120", ns_addr[0], ns_addr[1], ns_pos);
}
