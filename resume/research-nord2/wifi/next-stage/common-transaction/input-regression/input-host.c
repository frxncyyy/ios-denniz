
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
static uint32_t regs[5][1024];
static bool bits[1024], stuck_clock, wrap_complete;
static unsigned bit_count, bit_pos, delays, events, last_down, last_finger;
static uint32_t model_read(vm_offset_t base,unsigned off) {
    assert(base<5 && off<4096 && !(off%4));
    return regs[base][off/4];
}
static void model_write(vm_offset_t base,unsigned off,uint32_t v) {
    assert(base<5 && off<4096 && !(off%4));
    if(base==0 && ((off&15)==4 || (off&15)==8)) {
        unsigned idx=(off&~15)/4;
        if((off&15)==4) regs[base][idx]|=v; else regs[base][idx]&=~v;
        if((off&~15)==0x60) /* I2C pins must NEVER drive high */
            assert(!(regs[0][0x60/4]&regs[0][0x160/4]&(15<<10)));
    } else regs[base][off/4]=v;
    if(base==4 && off==0xc80) {
        assert(v==0x2a); /* only TOPSTATUS READ */
        regs[4][0xca8/4]=(1<<15)|(wrap_complete ? 6<<1 : 2<<1);
        regs[4][0xc94/4]=0xa;
    }
    if(base==4 && off==0xca4) {
        assert(v==1);
        regs[4][0xca8/4]=1<<15;
    }
}
static bool model_pin(unsigned pin) {
    if((pin==204 || pin==202)) return !stuck_clock;
    assert((pin==205 || pin==203) && bit_pos<bit_count);
    return bits[bit_pos++];
}
static void delay(unsigned n) { delays+=n; assert(delays<10000000); }
static void sweet_hid_touch(int finger,int down,int x,int y) {
    assert(finger>=0 && finger<10 && x>=0 && x<1080 && y>=0 && y<2400);
    events++; last_down=down; last_finger=finger;
}
static void reset_bits(void) { bit_count=bit_pos=delays=0; stuck_clock=false; }
static void bit(bool b) { bits[bit_count++]=b; }
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
	return model_read(base, off);
}

static void
ni_wr(vm_offset_t base, unsigned off, uint32_t v)
{
	model_write(base, off, v);
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
	return model_pin(pin);
}

static void
ni_wait(unsigned ms)
{
	(void)ms;
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

static bool ncc_latched_dirty;
static bool nw_restore(void) { return true; }

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


static void point(uint8_t b[62],unsigned slot,unsigned id,unsigned event,unsigned x,unsigned y) {
    unsigned o=6*slot;
    b[o+2]=(event<<6)|(x>>10); b[o+3]=x>>2;
    b[o+4]=(id<<4)|(y>>10); b[o+5]=y>>2;
    b[o+6]=((x&3)<<6)|((y&3)<<4); b[o+7]=8;
}
static void queue_read(const uint8_t *data,unsigned len) {
    bit(1); bit(0); bit(0); bit(1); bit(0);
    for(unsigned j=0;j<len;j++) for(unsigned i=0;i<8;i++) bit(data[j]&(0x80>>i));
    bit(1);
}
static void queue_ldo(uint8_t reg,uint8_t value) {
    uint8_t b[2]={value,ni_crc(0xc9,reg,value)};
    queue_read(b,2);
}
static void queue_write(uint8_t reg,uint8_t value) {
    bit(1); for(unsigned i=0;i<5;i++) bit(0); bit(1);
    queue_ldo(reg,value);
}
int main(void) {
    uint8_t b[62]; uint32_t active=0; int x[10]={0},y[10]={0};
    memset(b,255,sizeof b); b[1]=0;
    assert(ni_decode(b,&active,x,y) && !active);
    b[1]=2; point(b,0,9,0,4319,9599); point(b,1,0,2,0,0);
    assert(ni_decode(b,&active,x,y) && active==513 && x[9]==1079 && y[9]==2399);
    ni_report(active,x,y); assert(events==2);
    ni_report(active,x,y); assert(events==2);
    ni_report(1,x,y); assert(events==3 && last_finger==9 && !last_down);
    ni_report(0,x,y); assert(events==4 && !ni_active);
    point(b,1,9,0,0,0); assert(!ni_decode(b,&active,x,y)); /* duplicate */
    point(b,1,10,0,0,0); assert(!ni_decode(b,&active,x,y)); /* invalid ID */
    point(b,1,0,3,0,0); assert(!ni_decode(b,&active,x,y)); /* reserved event */
    point(b,1,0,0,4320,0); assert(!ni_decode(b,&active,x,y));
    point(b,1,0,0,0,9600); assert(!ni_decode(b,&active,x,y));
    memset(b,255,sizeof b); b[1]=10;
    for(unsigned i=0;i<10;i++) point(b,i,i,2,i*400,i*900);
    assert(ni_decode(b,&active,x,y) && active==1023);
    b[1]=11; assert(!ni_decode(b,&active,x,y));
    b[1]=0; assert(!ni_decode(b,&active,x,y));
    for(unsigned i=0;i<10;i++) point(b,i,i,1,0,0);
    assert(ni_decode(b,&active,x,y) && !active); /* all fingers UP */
    uint32_t random=17;
    for(unsigned n=0;n<100000;n++) {
        for(unsigned i=0;i<62;i++) { random=random*1664525+1013904223; b[i]=random>>24; }
        (void)ni_decode(b,&active,x,y); /* ASan/UBSan bounds */
    }

    ni_gpio=0; ni_lt=1; ni_tm=2; ni_rt=3; ni_pwrap=4;
    reset_bits(); bit(1); bit(0); bit(0); bit(1); bit(0);
    const uint8_t expected[3]={0x54,0x52,0xa5};
    for(unsigned j=0;j<3;j++) for(unsigned i=0;i<8;i++) bit(expected[j]&(0x80>>i));
    bit(1);
    assert(ni_read(0xa3,b,3) && !memcmp(b,expected,3) && bit_pos==bit_count);
    assert(!(regs[0][0x60/4]&(15<<10))); /* bus released after STOP */
    reset_bits(); bit(1); bit(1); bit(1); /* address NACK, STOP */
    assert(!ni_read(0xa3,b,1) && bit_pos==bit_count && delays<1000);
    reset_bits(); stuck_clock=true;
    assert(!ni_read(0xa3,b,1) && delays<1000 && !bit_pos);
    assert(!ni_read(0,b,0) && !ni_read(0,b,63));

    uint16_t top=0;
    assert(!ni_keys_read(&top)); /* wrapper not initialized */
    regs[4][0xca8/4]=(1<<15)|(6<<1);
    assert(!ni_keys_read(&top)); /* do not consume somebody else's result */
    regs[4][0xca8/4]=1<<15; wrap_complete=true;
    assert(ni_keys_read(&top) && top==0xa && regs[4][0xca8/4]==1<<15);
    wrap_complete=false; delays=0;
    assert(!ni_keys_read(&top) && delays==1000);
    ncc_latched_dirty=true; ni_saved=true;
    assert(nord2_input_prepare_return()==-1 && ni_saved);
    ncc_latched_dirty=false;
    ni_saved=true; ni_bus_owned=false;
    assert(nord2_input_prepare_return()==-1); /* cannot hand active wrapper to Linux */
    ni_saved=true; regs[4][0xca8/4]=(1<<15)|(6<<1);
    assert(nord2_input_prepare_return()==1 && regs[4][0xca8/4]==1<<15);
    assert(nord2_input_prepare_return()==0);
    /* A partial setup must restore saved fields while retaining neighbours. */
    ni_saved=true; ni_bus_owned=false;
    for(unsigned i=0;i<7;i++) {
        unsigned pin=ni_pins[i].pin;
        ni_pins[i].mux=i+1; ni_pins[i].dir=(pin==20); ni_pins[i].dout=1;
        regs[0][(0x300+pin/8*16)/4]=0xabababab;
    }
    ni_ies_lt=1<<15; ni_ies_tm=1<<12; ni_ies_rt=0; ni_ies_pwr=1<<3;
    regs[1][0x40/4]=regs[2][0x50/4]=regs[3][0x30/4]=regs[2][0x60/4]=0xffffffff;
    assert(nord2_input_prepare_return()==1);
    uint32_t masks[1024]={0};
    for(unsigned i=0;i<7;i++) {
        unsigned pin=ni_pins[i].pin, mo=(0x300+pin/8*16)/4, shift=pin%8*4;
        masks[mo]|=15u<<shift;
        assert(((regs[0][mo]>>shift)&15)==ni_pins[i].mux);
        assert(((regs[0][(pin/32*16)/4]>>(pin%32))&1)==ni_pins[i].dir);
        assert((regs[0][(0x100+pin/32*16)/4]>>(pin%32))&1);
    }
    for(unsigned i=0;i<1024;i++) if(masks[i])
        assert((regs[0][i]&~masks[i])==(0xabababab&~masks[i]));
    assert(regs[1][0x40/4]==(0xffffffffu&~(1u<<16)));
    assert(regs[2][0x50/4]==(0xffffffffu&~(1u<<13)));
    assert(regs[3][0x30/4]==(0xffffffffu&~(1u<<24)));
    assert(regs[2][0x60/4]==(0xffffffffu&~(1u<<6)));

    /* CRC vectors independently computed by polynomial division (0x107). */
    assert(ni_crc(0xc8,0x15,0xaa)==0x95);
    assert(ni_crc(0xc9,0x11,0x44)==0x2e);
    assert(ni_crc(0xc9,0x15,0xaa)==0xfe);
    regs[0][0x160/4]=0; reset_bits(); ni_ldo_changed=false;
    assert(!ni_ldo_write(0x15,0xaa) && !bit_pos);
    ni_ldo_changed=true; ni_ldo_volt=0xa9; ni_ldo_en=0;
    assert(!ni_ldo_write(0x12,0) && !ni_ldo_write(0x15,0xff));
    assert(!ni_ldo_write(0x11,0x10) && !bit_pos); /* mode changes forbidden */
    queue_write(0x15,0xaa);
    assert(ni_ldo_write(0x15,0xaa) && bit_pos==bit_count);
    reset_bits(); uint8_t wrong_crc[2]={0xaa,0xff}, value=0x77;
    queue_read(wrong_crc,2);
    assert(!ni_ldo_read(0x15,&value) && value==0x77 && bit_pos==bit_count);

    reset_bits(); ni_ldo_changed=false; bit(1); /* PMIC bus initially idle */
    const uint8_t id=0x53;
    queue_read(&id,1); queue_ldo(0x11,0); queue_ldo(0x15,0x8a);
    queue_write(0x11,0); queue_write(0x15,0xaa); queue_write(0x11,0x40);
    queue_ldo(0x11,0x44);
    assert(ni_power_on() && ni_ldo_changed && ni_ldo_volt==0x8a && !ni_ldo_en);
    assert(bit_pos==bit_count);
    reset_bits();
    queue_write(0x11,0); queue_write(0x15,0x8a); queue_write(0x11,0);
    assert(ni_power_restore() && !ni_ldo_changed && bit_pos==bit_count);

    /* An inherited enabled 3.0V rail needs no writes. */
    reset_bits(); bit(1);
    queue_read(&id,1); queue_ldo(0x11,0x44); queue_ldo(0x15,0xad);
    queue_ldo(0x11,0x44);
    assert(ni_power_on() && !ni_ldo_changed && bit_pos==bit_count);

    /* Write transport failure and wrong readback retain pending restoration. */
    ni_ldo_changed=true; ni_ldo_en=0; ni_ldo_volt=0x8a;
    reset_bits(); bit(1); bit(1); bit(1); /* address NACK then STOP */
    assert(!ni_power_restore() && ni_ldo_changed && bit_pos==bit_count);
    reset_bits();
    bit(1); for(unsigned i=0;i<5;i++) bit(0); bit(1);
    queue_ldo(0x11,0x40); /* disable did not stick: no voltage write allowed */
    assert(!ni_power_restore() && ni_ldo_changed && bit_pos==bit_count);
    reset_bits();
    queue_write(0x11,0);
    bit(1); for(unsigned i=0;i<5;i++) bit(0); bit(1);
    queue_ldo(0x15,0xaa); /* saved selector was not restored: do not enable */
    assert(!ni_power_restore() && ni_ldo_changed && bit_pos==bit_count);
    reset_bits();
    queue_write(0x11,0); queue_write(0x15,0x8a); queue_write(0x11,0);
    assert(ni_power_restore() && !ni_ldo_changed && bit_pos==bit_count);

    reset_bits(); bit(1); /* Bad identity cannot cause any power writes. */
    const uint8_t wrong_id=0x20; queue_read(&wrong_id,1); bit(1);
    assert(!ni_power_on() && !ni_ldo_changed && bit_pos==bit_count);
    puts("PASS: x4/multitouch/up/invalid reports, 100k fuzz, HID changes, I2C read/NACK/"
         "stuck-clock/STOP/bounds, PMIF timeout/ownership, masked pin/IES restore,"
         " CRC/identity/write allowlist, 3.0V supply enable/readback/restoration,"
         " inherited-rail preservation and failed-write restoration refusal");
}
