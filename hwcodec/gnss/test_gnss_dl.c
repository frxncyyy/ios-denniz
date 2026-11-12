/* SPDX-License-Identifier: MIT
 * Host test of gnss_dl.c against a model of the BGF_GPS_DMA engine + USRT + a fake GNSS DSP:
 *  - A2D DMA (DMA1/3) copies COUNT words from PGMADDR (wrapping to WPTO after WPPT words) into the DSP inbox, then
 *    INTSTA.INT = 1, STATE = 1;
 *  - the DSP answers every complete frame "<len:u32><payload>" with "<len><payload xor 0x5a>" in its outbox;
 *  - USRT STA.TX_IND = outbox not empty; D2A DMA (DMA2/4) copies min(outbox, COUNT words) to memory (with wrap); if the
 *    outbox ran dry first: STA.NODAINTB = 1 and RLCT = words left, else INTSTA/STATE done and RLCT = 0.
 * Checks: USRT bring-up sequence, register allow-list, wrap-around in both rings, partial words, backpressure, NODATA
 * clearing, 200 random frames end to end.   cc -O1 -Wall -Wextra test_gnss_dl.c gnss_dl.c gnss_pwr.c && ./a.out
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gnss_dl.h"

#define REMAP 0xdfe00000u
static uint8_t mem[65536];                    /* the EMI window: phys REMAP .. REMAP+64K */
static uint32_t regs[512][2]; static int nregs, bad, rom_ok = 1;
static uint8_t inbox[1 << 20], outbox[1 << 20]; static size_t nin, nout, oout;
static uint32_t *R(uint32_t pa) { for (int i = 0; i < nregs; i++) if (regs[i][0] == pa) return &regs[i][1]; regs[nregs][0] = pa; regs[nregs][1] = 0; return &regs[nregs++][1]; }
static uint8_t *busp(uint32_t bus) { uint32_t off = bus - N2G_EMI_BUS_BASE; if (off >= sizeof mem) { bad++; return mem; } return mem + off; }
static void dsp_consume(void)
{
	size_t p = 0;
	while (nin - p >= 4) {
		uint32_t n; memcpy(&n, inbox + p, 4);
		uint32_t padded = (n + 3u) & ~3u;
		if (nin - p < 4 + padded) break;
		memcpy(outbox + nout, &n, 4);
		for (uint32_t i = 0; i < padded; i++) outbox[nout + 4 + i] = (uint8_t)(i < n ? inbox[p + 4 + i] ^ 0x5a : 0);
		nout += 4 + padded; p += 4 + padded;
	}
	memmove(inbox, inbox + p, nin - p); nin -= p;
}
static int ch_of(uint32_t pa) { return (int)(((pa - 0x18c10000u) >> 8) - 1u); }
static void dma_run(int ch)
{
	uint32_t pg = *R(N2G_DMA_PGMADDR(ch)), to = *R(N2G_DMA_WPTO(ch)), wp = *R(N2G_DMA_WPPT(ch)) & 0xffff, cnt = *R(N2G_DMA_COUNT(ch)) & 0xffff;
	uint32_t a = pg, words_to_wrap = wp, left = cnt;
	int a2d = ch == 0 || ch == 2;
	while (left) {
		if (a2d) { memcpy(inbox + nin, busp(a), 4); nin += 4; }
		else { if (oout >= nout) break; memcpy(busp(a), outbox + oout, 4); oout += 4; }
		left--; a += 4;
		if (--words_to_wrap == 0) { a = to; words_to_wrap = 0x10000; }
	}
	*R(N2G_DMA_RLCT(ch)) = left;
	if (a2d) dsp_consume();
	if (!a2d && left) { *R(N2G_USRT_STA(ch == 3)) |= 2u; }        /* ran dry: NODAINTB, no DMA done */
	else { *R(N2G_DMA_INTSTA(ch)) = N2G_DMA_BIT; *R(N2G_DMA_STATE(ch)) = 1; }
	if (oout == nout) { oout = nout = 0; }
}
static uint32_t rd(void *c, uint32_t pa)
{
	(void)c;
	if (!n2g_reg_allowed(pa)) { bad++; printf("bad read %#x\n", pa); return 0; }
	if (pa == N2G_USRT_D2AF(0) || pa == N2G_USRT_D2AF(1)) return rom_ok ? 8u : 0;
	if (pa == N2G_USRT_STA(0) || pa == N2G_USRT_STA(1)) {
		uint32_t v = *R(pa) & ~8u;
		return v | (oout < nout ? 8u : 0);
	}
	return *R(pa);
}
static void wr(void *c, uint32_t pa, uint32_t v)
{
	(void)c;
	if (!n2g_reg_allowed(pa)) { bad++; printf("bad write %#x\n", pa); return; }
	if (pa == N2G_USRT_STA(0) || pa == N2G_USRT_STA(1)) { *R(pa) &= ~(v & 2u); return; }      /* w1c NODAINTB */
	if (pa >= 0x18c10100u && pa < 0x18c10500u && ((pa & 0xff) == 0x20) && (v & N2G_DMA_BIT)) {  /* ACKINT */
		int ch = ch_of(pa); *R(N2G_DMA_INTSTA(ch)) = 0; *R(N2G_DMA_STATE(ch)) = 0; return;
	}
	*R(pa) = v;
	if (pa >= 0x18c10100u && pa < 0x18c10500u && ((pa & 0xff) == 0x18) && (v & N2G_DMA_BIT)) dma_run(ch_of(pa));
}
static void dly(void *c, unsigned us) { (void)c; (void)us; }
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void pump(struct n2g_link *l, int n) { for (int i = 0; i < n; i++) n2g_dl_poll(l); }

int main(void)
{
	struct n2g_hw hw = { 0, rd, wr, dly, 0, REMAP };
	struct n2g_link L;
	CHECK(n2g_dl_init(&L, 0, &hw, mem, REMAP + 0x100) == N2G_OK);
	CHECK(n2g_dl_init(&L, 0, &hw, mem, REMAP + 0x102) == N2G_E_ARG);
	CHECK(n2g_dl_init(&L, 0, &hw, mem, REMAP) == N2G_OK);
	CHECK(n2g_dl_usrt_on(&L) == N2G_OK);
	CHECK((*R(N2G_USRT_CTRL(0)) & 0xb) == 3 && (*R(N2G_USRT_INTEN(0)) & 3) == 3 && (*R(N2G_USRT_A2D1(0)) & 0xffff) == 0xe && (*R(N2G_USRT_A2DF(0)) & 8));
	rom_ok = 0; CHECK(n2g_dl_usrt_on(&L) == N2G_E_TIMEOUT); rom_ok = 1;

	/* one frame end to end */
	uint8_t f[64], back[64]; uint32_t n = 5; memcpy(f, &n, 4); memcpy(f + 4, "hello", 5);
	CHECK(n2g_dl_write(&L, f, 9) == 9);
	CHECK(*R(N2G_DMA_CON(0)) == N2G_CON_L1_A2D && *R(N2G_DMA_PGMADDR(0)) == N2G_EMI_BUS_BASE && (*R(N2G_DMA_COUNT(0)) & 0xffff) == 3);
	pump(&L, 4);
	CHECK(n2g_dl_rx_avail(&L) == 12);
	CHECK(n2g_dl_read(&L, back, sizeof back) == 12);
	CHECK(memcmp(back, f, 4) == 0 && back[4] == ('h' ^ 0x5a) && back[8] == ('o' ^ 0x5a) && back[9] == 0);
	CHECK(*R(N2G_DMA_CON(1)) == N2G_CON_L1_D2A && L.stat_rx_nodata >= 1 && (*R(N2G_USRT_STA(0)) & 2) == 0);

	/* 200 random frames, ring wrap in both directions, reader drains slowly (backpressure) */
	srand(7);
	static uint8_t sent[1 << 19], got[1 << 19]; size_t ns = 0, ng = 0;
	for (int k = 0; k < 200; k++) {
		uint32_t len = 1 + (uint32_t)(rand() % 1500);
		uint8_t fr[1600]; memcpy(fr, &len, 4);
		for (uint32_t i = 0; i < len; i++) fr[4 + i] = (uint8_t)rand();
		size_t off = 0;
		while (off < 4 + len) {
			off += n2g_dl_write(&L, fr + off, 4 + len - off);
			pump(&L, 2);
			ng += n2g_dl_read(&L, got + ng, 700);
		}
		memcpy(sent + ns, fr, 4 + len); ns += 4 + len;
		uint32_t pad = ((len + 3) & ~3u) - len; memset(sent + ns, 0, pad); ns += pad;   /* written zero padded */
	}
	for (int i = 0; i < 200; i++) { pump(&L, 2); ng += n2g_dl_read(&L, got + ng, 4096); }
	CHECK(ng == ns);
	int ok = ng == ns;
	for (size_t p = 0; ok && p < ns; ) {                    /* reply = same frames, payload xor 0x5a */
		uint32_t len; memcpy(&len, sent + p, 4);
		uint32_t padded = (len + 3) & ~3u;
		if (memcmp(got + p, sent + p, 4)) ok = 0;
		for (uint32_t i = 0; ok && i < len; i++) if (got[p + 4 + i] != (sent[p + 4 + i] ^ 0x5a)) ok = 0;
		p += 4 + padded;
	}
	CHECK(ok);
	CHECK(L.tx.wr != 0 && L.stat_tx >= ns && bad == 0);      /* wrapped many times, nothing outside the allow-list */
	printf("frames 200, bytes %zu, tx %u rx %u nodata %u\n", ns, L.stat_tx, L.stat_rx, L.stat_rx_nodata);

	/* L5 link uses DMA3/4 and the second USRT block */
	struct n2g_link L5; CHECK(n2g_dl_init(&L5, 1, &hw, mem + 0x8000, REMAP + 0x8000) == N2G_OK);
	CHECK(n2g_dl_usrt_on(&L5) == N2G_OK && (*R(N2G_USRT_CTRL(1)) & 3) == 3);
	n = 3; memcpy(f, &n, 4); memcpy(f + 4, "abc", 3); CHECK(n2g_dl_write(&L5, f, 7) == 7);
	CHECK(*R(N2G_DMA_CON(2)) == N2G_CON_L5_A2D);
	pump(&L5, 4); CHECK(n2g_dl_read(&L5, back, 64) == 8 && back[4] == ('a' ^ 0x5a));
	n2g_dl_usrt_off(&L); CHECK((*R(N2G_USRT_CTRL(0)) & 0xb) == 0 && (*R(N2G_USRT_INTEN(0)) & 3) == 0);
	printf(fails ? "GNSS DL TEST FAILED (%d)\n" : "GNSS DL TEST OK\n", fails);
	return fails != 0;
}
