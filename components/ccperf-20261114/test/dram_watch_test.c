/* ccperf-20261114: host test of 0493's watch logic (nord2_dram_rate.h: n2dr_watch_update) and of the decode it relies on */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "../b/bsd/dev/nord2_dram_rate.h"
static uint32_t regs[2][0x2000 / 4];
static unsigned loads, bad;
static uint32_t rd(void *ctx, unsigned block, uint32_t off) { (void)ctx; loads++; if (block > 1 || off >= (block ? 0x2000u : 0x1000u) || (off & 3)) { bad++; return 0; } return regs[block][off / 4]; }
/* shuffle level lv with the PLL fields of a rate: sdmpcw (16.8 fixed), posdiv; prediv 0, the rest 0 -> raw = 52 * (sdmpcw >> 8) >> posdiv */
static void set(unsigned lv, unsigned sdmpcw, unsigned posdiv)
{
	uint32_t sh = 0x700u * lv;
	regs[0][0x50c / 4] = lv << 16;
	regs[1][(0x704 + sh) / 4] = sdmpcw << 16;
	regs[1][(0x708 + sh) / 4] = posdiv;
}
int main(void)
{
	struct n2dr r; struct n2dr_watch w; memset(&w, 0, sizeof(w));
	set(0, 82 << 8, 0); n2dr_read(rd, NULL, &r); assert(r.raw == 4264 && r.mbps == 4266 && r.shu_lv == 0);
	assert(n2dr_watch_update(&w, &r) && w.reads == 1 && w.changes == 0);          /* the first read is printed, not a change */
	for (unsigned i = 0; i < 100; i++) { n2dr_read(rd, NULL, &r); assert(!n2dr_watch_update(&w, &r)); }
	assert(w.changes == 0 && w.reads == 101);
	set(1, 59 << 8, 1); n2dr_read(rd, NULL, &r); assert(r.shu_lv == 1 && r.raw == 1534 && r.mbps == 1600);
	assert(n2dr_watch_update(&w, &r) && w.changes == 1 && w.mbps == 1600 && w.lv == 1);
	n2dr_read(rd, NULL, &r); assert(!n2dr_watch_update(&w, &r));
	unsigned printed = 0;
	for (unsigned i = 0; i < 1000; i++) {      /* a rate that flips with every read: 32 lines, then one in 32 */
		if (i & 1) set(1, 59 << 8, 1); else set(0, 82 << 8, 0);
		n2dr_read(rd, NULL, &r);
		if (n2dr_watch_update(&w, &r)) printed++;
	}
	assert(w.changes == 1001 && printed == 61);      /* changes 2..32 (31 lines), then 64, 96, ... 992 (30 lines) */
	assert(bad == 0 && loads == 8u * (1 + 100 + 2 + 1000));      /* eight loads per read, all inside the two mappings (0x1000 NAO, 0x2000 AO) */
	for (unsigned lv = 0; lv < 4; lv++) { loads = 0; set(lv, 46 << 8, 0); n2dr_read(rd, NULL, &r); assert(r.shu_lv == lv && r.raw == 2392 && r.mbps == 2400 && bad == 0); }
	printf("DRAM watch: first read printed, steady = silent, change printed, flip-flop rate limited (%u lines for 1000 changes), all loads in range\nDRAM_WATCH_OK\n", printed);
	return 0;
}
