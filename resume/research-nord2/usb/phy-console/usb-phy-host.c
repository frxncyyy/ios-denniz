
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
#define NUSB_SOFTCON (1u<<6)
static struct {
    bool owned;
    uint32_t power, lv1ie, epie, commonie, linkie, devconf;
} nusb;
static const uint16_t nusb_phy_offsets[] = {0, 0x14, 0x18, 0x1c, 0x20, 0x68, 0x6c};
static struct {
	vm_offset_t com;
	bool owned;
	uint32_t saved[7];
} nusb_phy;


static uint32_t phy_regs[0x100/4], mac_regs[0x2e00/4], original[7];
static uint32_t writes, mac_writes, delays, elapsed, restores;
static uint32_t written_off[32], written_val[32];
static bool map_fail, restoring;
static vm_offset_t ml_io_map(vm_offset_t pa, unsigned size) {
    assert(pa==0x11e40000 && size==0x700);
    return map_fail ? 0 : 0x100000;
}
static unsigned phy_index(uint32_t off) {
    for (unsigned i=0;i<7;i++) if (nusb_phy_offsets[i]==off) return i;
    assert(0); return 0;
}
static uint32_t nusb_phy_rd(uint32_t off) {
    (void)phy_index(off);
    assert(nusb_phy.com==0x100300);
    return phy_regs[off/4];
}
static void nusb_phy_wr(uint32_t off, uint32_t value) {
    unsigned i=phy_index(off);
    assert(nusb_phy.owned);
    if (restoring) {
        /* Exact rollback, after MAC disconnect and before interrupt restore. */
        assert(mac_writes==8 && !(mac_regs[0x2404/4] & NUSB_SOFTCON));
        assert(value==original[i]);
        if (off==0x68) assert(restores==6); /* suspend overrides last */
        restores++;
    } else {
        static const uint32_t change_mask[7]={
            1u<<5, 1u<<11, (1u<<23)|(1u<<20), (1u<<12)|(1u<<9),
            (1u<<9)|(1u<<8),
            0xc4000000u|(1u<<23)|(0x1fu<<17)|(0xfu<<10)|0xfcu,
            (1u<<16)|(1u<<9)|0x36u
        };
        assert(((value ^ original[i]) & ~change_mask[i])==0);
        assert(writes<32);
        written_off[writes]=off; written_val[writes]=value; writes++;
    }
    phy_regs[off/4]=value;
}
static void delay(unsigned us) {
    assert(us==(delays ? 800u:30u));
    if (!delays) {
        assert(writes==3);
        assert(written_off[0]==0x68 && written_off[1]==0x68 && written_off[2]==0x68);
        assert(written_val[0] & (1u<<18));
        assert(!(written_val[1] & (1u<<3)));
        assert((written_val[2]&((1u<<18)|(1u<<3)))==((1u<<18)|(1u<<3)));
    }
    delays++; elapsed+=us;
}
static uint32_t nusb_rd(uint32_t off) {
    assert(off==0x2404); return mac_regs[off/4];
}
static void nusb_wr(uint32_t off, uint32_t val) {
    if (mac_writes++==0) assert(off==0x2404 && !(val&NUSB_SOFTCON));
    switch(off) {
    case 0xc: mac_regs[4/4]&=~val; break;
    case 0x8c: mac_regs[0x84/4]&=~val; break;
    case 8: mac_regs[4/4]|=val; break;
    case 0x88: mac_regs[0x84/4]|=val; break;
    case 0x80: case 0x241c: case 0x854: mac_regs[off/4]&=~val; break;
    case 0x100: case 0x800: case 0x2418: case 0x850: case 0x2404:
        mac_regs[off/4]=val; break;
    default: assert(0);
    }
}
#define printf(...) ((void)0)
static bool
nusb_phy_snapshot(void)
{
	/* Called only after the existing clock and active-device MAC guards. */
	vm_offset_t base = ml_io_map(0x11e40000, 0x700);
	if (!base) { return false; }
	nusb_phy.com = base + 0x300;  /* generic-tphy-v2, not the v1 layout */
	for (unsigned i = 0; i < 7; i++) {
		nusb_phy.saved[i] = nusb_phy_rd(nusb_phy_offsets[i]);
		printf("NORD2 USB: PHY[%03x]=%08x\n",
		    0x300 + nusb_phy_offsets[i], nusb_phy.saved[i]);
		if (nusb_phy.saved[i] == ~0u) { return false; }
	}
	return true;
}

/*
 * Restore MAC control of the USB2 pins, following MT6893's TPHY power-on
 * and device-mode path. Keep analog calibration, clocks, supplies, the
 * charger IC and Type-C routing unchanged. All seven modified registers
 * are saved before the first write and restored after MAC disconnect.
 */
static void
nusb_phy_enable(void)
{
	nusb_phy.owned = true;
	/* Leave UART mode; power-on suspend pulse used by MT6893 Linux. */
	uint32_t dtm0 = nusb_phy_rd(0x68) & ~0xc4000000u;
	nusb_phy_wr(0x68, dtm0 | (1u << 18));
	nusb_phy_wr(0x68, (dtm0 | (1u << 18)) & ~(1u << 3));
	nusb_phy_wr(0x68, dtm0 | (1u << 18) | (1u << 3));
	delay(30);
	/* Release forced data, pulldowns, transceiver, suspend and termination. */
	uint32_t release = (1u << 23) | (1u << 21) | (1u << 20) |
	    (1u << 19) | (1u << 18) | (1u << 17) | (0xfu << 10) |
	    (1u << 7) | (1u << 6) | (3u << 4) | (1u << 3) | (1u << 2);
	nusb_phy_wr(0x68, (dtm0 | (1u << 3)) & ~(1u << 18));
	nusb_phy_wr(0x68, dtm0 & ~release);
	/* USB PLL interrupt enable; disable 100uA SSUSB current and GPIO mode. */
	nusb_phy_wr(0, nusb_phy_rd(0) | (1u << 5));
	nusb_phy_wr(0x14, nusb_phy_rd(0x14) & ~(1u << 11));
	nusb_phy_wr(0x20, nusb_phy_rd(0x20) & ~((1u << 9) | (1u << 8)));
	/* Clear the explicit PHY pullup test override, as DPPULLUP_CLR does. */
	nusb_phy_wr(0x1c, nusb_phy_rd(0x1c) & ~((1u << 12) | (1u << 9)));
	/* Hand DP/DM back from BC1.1 detection; enable VBUS comparator. */
	nusb_phy_wr(0x18, (nusb_phy_rd(0x18) & ~(1u << 23)) | (1u << 20));
	uint32_t dtm1 = nusb_phy_rd(0x6c) & ~((1u << 16) | (1u << 4));
	/* Device ID, VBUS/AVALID values, as Linux set_mode/device + power_on. */
	nusb_phy_wr(0x6c, dtm1 | (1u << 9) | (1u << 1) | (1u << 5) | (1u << 2));
	delay(800);
}

static void
nusb_phy_restore(void)
{
	if (!nusb_phy.owned) { return; }
	/* Restore suspend/termination overrides last, after the analog registers. */
	for (unsigned i = 0; i < 7; i++) {
		if (nusb_phy_offsets[i] != 0x68) {
			nusb_phy_wr(nusb_phy_offsets[i], nusb_phy.saved[i]);
		}
	}
	nusb_phy_wr(0x68, nusb_phy.saved[5]);
	nusb_phy.owned = false;
}

/* No DMA was started. Disconnect before restoring owned PHY and MAC state. */
int
nord2_usb_prepare_return(void)
{
	if (!nusb.owned) { return 0; }
	nusb_wr(0x2404, nusb_rd(0x2404) & ~NUSB_SOFTCON);
	nusb_wr(0xc, ~0u);
	nusb_wr(0x8c, ~0u);
	nusb_wr(0x80, ~0u);
	nusb_wr(0x241c, ~0u);
	nusb_wr(0x854, ~0u);
	nusb_wr(0x100, 64);
	nusb_wr(0x800, nusb.devconf);
	nusb_phy_restore();
	nusb_wr(0x2418, nusb.commonie);
	nusb_wr(0x850, nusb.linkie);
	nusb_wr(0x2404, nusb.power);
	nusb_wr(0x88, nusb.epie);
	nusb_wr(8, nusb.lv1ie);
	nusb.owned = false;
	return 1;
}


#undef printf
static void reset(unsigned seed) {
    memset(&nusb,0,sizeof(nusb)); memset(&nusb_phy,0,sizeof(nusb_phy));
    writes=mac_writes=delays=elapsed=restores=0;
    map_fail=restoring=false;
    for(unsigned i=0;i<7;i++) {
        seed=seed*1664525u+1013904223u;
        phy_regs[nusb_phy_offsets[i]/4]=original[i]=seed;
    }
}
int main(void) {
    reset(1); map_fail=true;
    assert(!nusb_phy_snapshot() && !writes && !nusb_phy.owned);
    for (unsigned i=0;i<7;i++) {
        reset(i); phy_regs[nusb_phy_offsets[i]/4]=0xffffffffu;
        assert(!nusb_phy_snapshot() && !writes && !nusb_phy.owned);
    }
    for (unsigned seed=1;seed<=1000;seed++) {
        reset(seed);
        assert(nusb_phy_snapshot() && !nusb_phy.owned && !writes);
        for (unsigned i=0;i<7;i++) assert(nusb_phy.saved[i]==original[i]);
        nusb_phy_enable();
        assert(nusb_phy.owned && delays==2 && elapsed==830);
        assert((phy_regs[0x68/4]&0xc4be3cfcu)==0);
        assert(!(phy_regs[0x18/4]&(1u<<23)) && (phy_regs[0x18/4]&(1u<<20)));
        assert(!(phy_regs[0x20/4]&0x300) && !(phy_regs[0x1c/4]&0x1200));
        assert((phy_regs[0x6c/4]&0x10036)==0x26 && (phy_regs[0x6c/4]&0x200));
        nusb.owned=true; nusb.power=0x60a1; nusb.lv1ie=0xff; nusb.epie=0x3000b;
        nusb.commonie=0x3b7; nusb.linkie=1; nusb.devconf=0;
        mac_regs[0x2404/4]=nusb.power|NUSB_SOFTCON;
        restoring=true;
        assert(nord2_usb_prepare_return()==1);
        assert(!nusb.owned && !nusb_phy.owned && restores==7 && mac_writes==13);
        for (unsigned i=0;i<7;i++) assert(phy_regs[nusb_phy_offsets[i]/4]==original[i]);
        assert(nord2_usb_prepare_return()==0 && mac_writes==13);
        nusb_phy_restore(); assert(restores==7);
    }
    puts("PASS: exact TPHY code; failed mapping/register reads cause no writes;"
         " pulse and delays, seven-register scope, calibration preserved;"
         " MAC disconnect precedes exact PHY rollback; 1000 inherited states");
}
