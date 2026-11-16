#ifndef NORD2_DRAM_RATE_H
#define NORD2_DRAM_RATE_H
/*
 * 0462: which data rate the DRAM runs at, read-only.  The stock kernel's fmeter_v1 (drivers/memory/mediatek/dramc.c) with the register list of mt6893.dts
 * (dramc@10230000: shu_lv / pll_id in DDRPHY NAO channel A + 0x50c, the PLL fields in DDRPHY AO channel A + 0x700 * shu_lv), crystal 52 MHz.  Nothing is written.
 * The steps the bootloader calibrated (live FDT freq_step): 4266 3733 3068 2366 1866 1534 1200 800 Mbps.
 */
#include <stdint.h>

#define N2DR_NAO_PA 0x10236000u       /* DDRPHY NAO CHA, 0x1000 */
#define N2DR_AO_PA  0x10238000u       /* DDRPHY AO CHA, 0x2000 */

struct n2dr { unsigned shu_lv, pll_id, sdmpcw, prediv, posdiv, ckdiv4, pll_md, cldiv2, fbksel, dqopen, raw, mbps; };

static inline unsigned
n2dr_decode(unsigned v)
{
	switch (v) {
	case 4264: return 4266;
	case 3718: return 3733;
	case 3094: case 3068: return 3200;
	case 2392: case 2366: return 2400;
	case 1859: return 1866;
	case 1534: return 1600;
	case 1196: case 1144: return 1200;
	case 819: case 754: return 800;
	default: return v;
	}
}

/* rd(ctx, block, offset): block 0 = DDRPHY NAO CHA, 1 = DDRPHY AO CHA */
static inline void
n2dr_read(uint32_t (*rd)(void *ctx, unsigned block, uint32_t off), void *ctx, struct n2dr *o)
{
	uint32_t s = rd(ctx, 0, 0x50c);
	o->shu_lv = (s & 0x00030000u) >> 16;
	o->pll_id = (s & 0x00000100u) >> 8;
	uint32_t sh = 0x700u * o->shu_lv, p = o->pll_id ? 0x20u : 0u;
	o->sdmpcw = (rd(ctx, 1, 0x704 + p + sh) & 0xffff0000u) >> 16;
	uint32_t d = rd(ctx, 1, 0x708 + p + sh);
	o->prediv = (d & 0x000c0000u) >> 18;
	o->posdiv = d & 7u;
	o->ckdiv4 = (rd(ctx, 1, 0x874 + sh) & 4u) >> 2;
	o->pll_md = (rd(ctx, 1, 0x744 + sh) & 0x100u) >> 8;
	o->cldiv2 = (rd(ctx, 1, 0x8b4 + sh) & 2u) >> 1;
	o->fbksel = (rd(ctx, 1, 0x70c + sh) & 0x40u) >> 6;
	o->dqopen = (rd(ctx, 1, 0x870 + sh) & 0x00200000u) >> 21;
	o->raw = ((52u >> o->prediv) * (o->sdmpcw >> 8)) >> o->posdiv >> o->ckdiv4 >> o->pll_md >> o->cldiv2 << o->fbksel >> (o->dqopen << 1);
	o->mbps = n2dr_decode(o->raw);
}
#endif
