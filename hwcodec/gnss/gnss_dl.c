/* SPDX-License-Identifier: MIT
 * gnss_dl.c -- see gnss_dl.h. Vendor source order kept for every register sequence; all accesses through n2g_hw.
 */
#ifndef N2G_KERNEL
#include "gnss_dl.h"
#endif

static uint32_t dl_rd(const struct n2g_link *l, uint32_t pa) { return l->hw->read32(l->hw->ctx, pa); }
static void dl_wr(const struct n2g_link *l, uint32_t pa, uint32_t v) { l->hw->write32(l->hw->ctx, pa, v); }
static void dl_set(const struct n2g_link *l, uint32_t pa, uint32_t m, uint32_t v) { dl_wr(l, pa, (dl_rd(l, pa) & ~m) | (v & m)); }
static uint32_t dl_bus(const struct n2g_link *l, uint32_t pa) { return N2G_EMI_BUS_BASE + (pa - l->remap_pa); }

static uint32_t ring_used(const struct n2g_ring *r) { return (r->wr + N2G_RING_BYTES - r->rd) % N2G_RING_BYTES; }
static uint32_t ring_free(const struct n2g_ring *r) { return N2G_RING_BYTES - 4u - ring_used(r); }

N2G_API int n2g_dl_init(struct n2g_link *l, int id, const struct n2g_hw *hw, uint8_t *va, uint32_t pa)
{
	if (!l || !hw || !va || (id != 0 && id != 1) || (pa & 3u) || pa < hw->emi_base_pa ||
	    pa + 2u * N2G_RING_BYTES - hw->emi_base_pa > (64u << 20)) return N2G_E_ARG;
	for (size_t i = 0; i < sizeof *l; i++) ((uint8_t *)l)[i] = 0;
	l->id = id; l->hw = hw; l->remap_pa = hw->emi_base_pa;
	l->tx.va = va; l->tx.pa = pa;
	l->rx.va = va + N2G_RING_BYTES; l->rx.pa = pa + N2G_RING_BYTES;
	return N2G_OK;
}

/* gps_dl_hw_usrt_ctrl(link, is_on = true, is_dma_mode = true, is_1byte_mode = false) */
N2G_API int n2g_dl_usrt_on(struct n2g_link *l)
{
	int i = l->id;
	dl_set(l, N2G_USRT_CTRL(i), 1u << 3, 0);            /* BYTEN = 0 (4-byte mode) */
	dl_set(l, N2G_USRT_CTRL(i), 1u << 1, 1u << 1);      /* TX_EN */
	dl_set(l, N2G_USRT_CTRL(i), 1u << 0, 1u << 0);      /* RX_EN */
	dl_set(l, N2G_USRT_INTEN(i), 1u << 0, 1u << 0);     /* TXIEN */
	dl_set(l, N2G_USRT_INTEN(i), 1u << 1, 1u << 1);     /* NODAIEN */
	dl_set(l, N2G_USRT_A2D1(i), 0xffffu, N2G_DSP_CFG_4BYTE);
	dl_set(l, N2G_USRT_A2DF(i), 1u << 3, 1u << 3);      /* A2DF3 */
	for (int n = 0; n < N2G_POLL_MAX; n++) {            /* wait ROM okay flag */
		if (dl_rd(l, N2G_USRT_D2AF(i)) & (1u << 3)) return N2G_OK;
		l->hw->delay_us(l->hw->ctx, 1);
	}
	l->stat_err++;
	return N2G_E_TIMEOUT;
}

N2G_API void n2g_dl_usrt_off(struct n2g_link *l)
{
	int i = l->id;
	dl_set(l, N2G_USRT_CTRL(i), 1u << 3, 0);
	dl_set(l, N2G_USRT_CTRL(i), 1u << 1, 0);
	dl_set(l, N2G_USRT_CTRL(i), 1u << 0, 0);
	dl_set(l, N2G_USRT_INTEN(i), 1u << 0, 0);
	dl_set(l, N2G_USRT_INTEN(i), 1u << 1, 0);
}

static int ch_a2d(const struct n2g_link *l) { return l->id ? 2 : 0; }
static int ch_d2a(const struct n2g_link *l) { return l->id ? 3 : 1; }

/* gps_dl_hw_set_dma_start: CON, PGMADDR, WPTO, WPPT, COUNT, START.STR (units of 4 bytes in 4-byte mode) */
static void dma_start(const struct n2g_link *l, int ch, uint32_t con, const struct n2g_ring *r, uint32_t at, uint32_t bytes)
{
	dl_wr(l, N2G_DMA_CON(ch), con);
	dl_wr(l, N2G_DMA_PGMADDR(ch), dl_bus(l, r->pa + at));
	dl_wr(l, N2G_DMA_WPTO(ch), dl_bus(l, r->pa));
	dl_set(l, N2G_DMA_WPPT(ch), 0xffffu, (N2G_RING_BYTES - at) / 4u);
	dl_set(l, N2G_DMA_COUNT(ch), 0xffffu, bytes / 4u);
	dl_set(l, N2G_DMA_START(ch), N2G_DMA_BIT, N2G_DMA_BIT);
}
/* gps_dl_hw_set_dma_stop: START.STR = 0, ACKINT = 1 */
static void dma_stop(const struct n2g_link *l, int ch)
{
	dl_set(l, N2G_DMA_START(ch), N2G_DMA_BIT, 0);
	dl_set(l, N2G_DMA_ACKINT(ch), N2G_DMA_BIT, N2G_DMA_BIT);
}
static int dma_done(const struct n2g_link *l, int ch)
{
	return (dl_rd(l, N2G_DMA_INTSTA(ch)) & N2G_DMA_BIT) && (dl_rd(l, N2G_DMA_STATE(ch)) & 0x7fu) == 1u;
}

static void tx_kick(struct n2g_link *l)
{
	uint32_t used = ring_used(&l->tx);
	if (l->tx_busy || !used) return;
	l->tx_len = used;
	dma_start(l, ch_a2d(l), l->id ? N2G_CON_L5_A2D : N2G_CON_L1_A2D, &l->tx, l->tx.rd, used);
	l->tx_busy = 1;
}

N2G_API size_t n2g_dl_write(struct n2g_link *l, const uint8_t *buf, size_t len)
{
	size_t done = 0;
	while (done < len) {
		uint32_t fr = ring_free(&l->tx);
		if (fr < 4u) break;
		uint32_t chunk = (uint32_t)(len - done);
		if (chunk > fr) chunk = fr & ~3u;
		uint32_t padded = (chunk + 3u) & ~3u;
		if (padded > fr) { chunk = fr & ~3u; padded = chunk; }
		if (!chunk) break;
		for (uint32_t i = 0; i < padded; i++) {
			l->tx.va[(l->tx.wr + i) % N2G_RING_BYTES] = i < chunk ? buf[done + i] : 0;
		}
		l->tx.wr = (l->tx.wr + padded) % N2G_RING_BYTES;
		done += chunk;
	}
	tx_kick(l);
	return done;
}

N2G_API size_t n2g_dl_read(struct n2g_link *l, uint8_t *buf, size_t len)
{
	size_t n = ring_used(&l->rx);
	if (n > len) n = len;
	for (size_t i = 0; i < n; i++) buf[i] = l->rx.va[(l->rx.rd + i) % N2G_RING_BYTES];
	/* keep rd word aligned for the DMA: a partial word read leaves its rest for the next read */
	l->rx.rd = (uint32_t)((l->rx.rd + n) % N2G_RING_BYTES);
	return n;
}

N2G_API size_t n2g_dl_rx_avail(const struct n2g_link *l) { return ring_used(&l->rx); }
N2G_API size_t n2g_dl_tx_free(const struct n2g_link *l) { return ring_free(&l->tx); }

N2G_API int n2g_dl_poll(struct n2g_link *l)
{
	int r = 0, i = l->id;
	/* TX: gps_dl_hw_wait_until_dma_complete_and_stop_it */
	if (l->tx_busy && dma_done(l, ch_a2d(l))) {
		dma_stop(l, ch_a2d(l));
		l->tx.rd = (l->tx.rd + l->tx_len) % N2G_RING_BYTES;
		l->stat_tx += l->tx_len;
		l->tx_busy = 0;
		r |= 1;
		tx_kick(l);
	}
	/* RX end: DMA done, or USRT "no data" / REGE (gps_dl_hal_wait_and_handle_until_usrt_has_nodata_or_rx_dma_done) */
	if (l->rx_busy) {
		uint32_t sta = dl_rd(l, N2G_USRT_STA(i));
		int done = dma_done(l, ch_d2a(l)), nodata = (sta & ((1u << 1) | (1u << 5))) != 0;
		if (done || nodata) {
			dma_stop(l, ch_d2a(l));
			uint32_t left = (dl_rd(l, N2G_DMA_RLCT(ch_d2a(l))) & 0xffffu) * 4u;
			uint32_t got = left <= l->rx_max ? l->rx_max - left : 0;
			l->rx.wr = (l->rx.wr + got) % N2G_RING_BYTES;
			l->stat_rx += got;
			if (nodata) { l->stat_rx_nodata++; dl_set(l, N2G_USRT_STA(i), 1u << 1, 1u << 1); }   /* clear NODAINTB (w1c) */
			l->rx_busy = 0;
			if (got) r |= 2;
		}
	}
	/* RX start: DSP has data (USRT TX_IND) and the RX ring has room */
	if (!l->rx_busy && (dl_rd(l, N2G_USRT_STA(i)) & (1u << 3))) {
		uint32_t fr = ring_free(&l->rx) & ~3u;
		if (fr >= 4u) {
			l->rx_max = fr;
			dma_start(l, ch_d2a(l), l->id ? N2G_CON_L5_D2A : N2G_CON_L1_D2A, &l->rx, l->rx.wr, fr);
			l->rx_busy = 1;
		}
	}
	return r;
}
