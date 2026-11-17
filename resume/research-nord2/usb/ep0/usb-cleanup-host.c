
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define NUSB_SOFTCON (1u<<6)
static struct {
    bool owned;
    uint32_t power, lv1ie, epie, commonie, linkie, devconf;
} nusb;
static uint32_t regs[0x2e00/4], writes;
static uint32_t nusb_rd(uint32_t off) { assert(off==0x2404); return regs[off/4]; }
static void nusb_wr(uint32_t off, uint32_t val) {
    if (writes++==0) assert(off==0x2404 && !(val & NUSB_SOFTCON));
    switch(off) {
    case 0xc: regs[4/4]&=~val; break;
    case 0x8c: regs[0x84/4]&=~val; break;
    case 8: regs[4/4]|=val; break;
    case 0x88: regs[0x84/4]|=val; break;
    case 0x80: case 0x241c: case 0x854: regs[off/4]&=~val; break;
    case 0x100: case 0x800: case 0x2418: case 0x850: case 0x2404:
        regs[off/4]=val; break;
    default: assert(0); /* No FIFO, DMA descriptor or unrelated MMIO write. */
    }
}
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
	nusb_wr(0x2418, nusb.commonie);
	nusb_wr(0x850, nusb.linkie);
	nusb_wr(0x2404, nusb.power);
	nusb_wr(0x88, nusb.epie);
	nusb_wr(8, nusb.lv1ie);
	nusb.owned = false;
	return 1;
}


int main(void) {
    assert(nord2_usb_prepare_return()==0 && writes==0);
    nusb.owned=true; nusb.power=0x60a1; nusb.lv1ie=0xff; nusb.epie=0x3000b;
    nusb.commonie=0x3b7; nusb.linkie=1; nusb.devconf=0;
    regs[0x2404/4]=nusb.power|NUSB_SOFTCON;
    regs[4/4]=0; regs[0x84/4]=0x10001;
    regs[0x800/4]=5u<<24;
    regs[0x80/4]=regs[0x241c/4]=regs[0x854/4]=0xffffffffu;
    assert(nord2_usb_prepare_return()==1 && !nusb.owned && writes==13);
    assert(regs[0x2404/4]==nusb.power && regs[4/4]==nusb.lv1ie);
    assert(regs[0x84/4]==nusb.epie && regs[0x800/4]==nusb.devconf);
    assert(regs[0x2418/4]==nusb.commonie && regs[0x850/4]==nusb.linkie);
    assert(!regs[0x80/4] && !regs[0x241c/4] && !regs[0x854/4]);
    assert(nord2_usb_prepare_return()==0 && writes==13);
    puts("PASS: exact USB cleanup; inactive no-op, disconnect before restore,"
         " interrupt masks/address restored, no DMA/FIFO writes, idempotence");
}
