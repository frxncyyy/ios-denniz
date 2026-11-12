/* SPDX-License-Identifier: MIT
 * gnss_dl.h -- GNSS milestone 2: the MT6893 GNSS data link (the byte pipe /dev/gpsdl0,1 give libmnl), host-testable.
 * Transcribed from the vendor gps_dl (plat/v010 = MT6885/MT6893): USRT APB control (hw/gps_dl_hw_usrt_apb.c
 * gps_dl_hw_usrt_ctrl, DMA mode, 4-byte mode which is the vendor default dma_is_1byte_mode = false), BGF_GPS_DMA channel
 * programming (hw/gps_dl_hw_gps.c gps_dl_hw_set_dma_start/stop, wait_until_dma_complete), RX start on USRT TX_IND
 * ("has data"), RX end on DMA done or USRT NODAINTB/REGE ("no data"), ring arithmetic (lib/gps_dl_dma_buf.c
 * gdl_dma_buf_entry_to_transfer). Addresses resolved from plat/v010/hw/inc on the host; AP view = 0x18C00000 + (bus - 0x80000000).
 * Polling only (the vendor also has an IRQ path, SPI 383..389; the worker polls like gps_dl_hal_poll_event).
 */
#ifndef N2G_GNSS_DL_H
#define N2G_GNSS_DL_H
#ifndef N2G_KERNEL
#include <stdint.h>
#include <stddef.h>
#endif
#include "gnss_pwr.h"

#define N2G_DMA_WPPT(ch)        N2G_GPS_BUS(0x80010108u + 0x100u * (ch))   /* [15:0] units to wrap */
#define N2G_DMA_WPTO(ch)        N2G_GPS_BUS(0x8001010cu + 0x100u * (ch))   /* buffer start (bus) */
#define N2G_DMA_COUNT(ch)       N2G_GPS_BUS(0x80010110u + 0x100u * (ch))   /* [15:0] units */
#define N2G_DMA_CON(ch)         N2G_GPS_BUS(0x80010114u + 0x100u * (ch))
#define N2G_DMA_START(ch)       N2G_GPS_BUS(0x80010118u + 0x100u * (ch))   /* [15] STR */
#define N2G_DMA_INTSTA(ch)      N2G_GPS_BUS(0x8001011cu + 0x100u * (ch))   /* [15] INT */
#define N2G_DMA_ACKINT(ch)      N2G_GPS_BUS(0x80010120u + 0x100u * (ch))   /* [15] ACK */
#define N2G_DMA_RLCT(ch)        N2G_GPS_BUS(0x80010124u + 0x100u * (ch))   /* [15:0] units left */
#define N2G_DMA_PGMADDR(ch)     N2G_GPS_BUS(0x8001012cu + 0x100u * (ch))   /* transfer start (bus) */
#define N2G_DMA_STATE(ch)       N2G_GPS_BUS(0x80010148u + 0x100u * (ch))   /* [6:0], 1 = done/idle after a transfer */
#define N2G_DMA_BIT             0x8000u
/* 4-byte mode CON values per channel: DMA1 L1 A2D, DMA2 L1 D2A, DMA3 L5 A2D, DMA4 L5 D2A */
#define N2G_CON_L1_A2D 0x00128016u
#define N2G_CON_L1_D2A 0x0007801Au
#define N2G_CON_L5_A2D 0x00328016u
#define N2G_CON_L5_D2A 0x0027801Au

#define N2G_USRT_BASE(l)        N2G_GPS_BUS((l) ? 0x80083000u : 0x80073000u)
#define N2G_USRT_CTRL(l)        (N2G_USRT_BASE(l) + 0x04u)   /* [3] BYTEN [1] TX_EN [0] RX_EN */
#define N2G_USRT_INTEN(l)       (N2G_USRT_BASE(l) + 0x08u)   /* [0] TXIEN [1] NODAIEN */
#define N2G_USRT_STA(l)         (N2G_USRT_BASE(l) + 0x0cu)   /* [3] TX_IND (DSP has data) [1] NODAINTB (w1c) [5] REGE [29] RX_EMP */
#define N2G_USRT_A2DF(l)        (N2G_USRT_BASE(l) + 0x20u)   /* [3] A2DF3 */
#define N2G_USRT_D2AF(l)        (N2G_USRT_BASE(l) + 0x24u)   /* [3] D2AF3: DSP ROM okay */
#define N2G_USRT_A2D1(l)        (N2G_USRT_BASE(l) + 0x34u)   /* [15:0] DSP config */
#define N2G_DSP_CFG_4BYTE       0x000eu   /* MVCD_SPEED_UP | ADIE_IS_MT6635_E2_OR_AFTER | USRT_4BYTE_MODE */

#define N2G_EMI_BUS_BASE        0x78000000u   /* GPS_EMI_BUS_BASE: bus = 0x78000000 + (phys - remap base) */
#define N2G_RING_BYTES          8192u         /* GPS_DL_TX/RX_BUF_SIZE */

struct n2g_ring { uint8_t *va; uint32_t pa, rd, wr; };     /* bytes, 4-byte aligned indexes, one word kept free */
struct n2g_link {
	int id;                          /* 0 = L1, 1 = L5 */
	const struct n2g_hw *hw;
	uint32_t remap_pa;               /* the GPS EMI remap base (64 KiB aligned), same as hw->emi_base_pa */
	struct n2g_ring tx, rx;
	int tx_busy, rx_busy;
	uint32_t tx_len, rx_max;         /* bytes programmed into the running transfer */
	uint32_t stat_tx, stat_rx, stat_rx_nodata, stat_err;
};

/* rings: va/pa of 2 x N2G_RING_BYTES (TX then RX), inside the remap window */
N2G_API int n2g_dl_init(struct n2g_link *l, int id, const struct n2g_hw *hw, uint8_t *va, uint32_t pa);
/* USRT on in DMA mode (gps_dl_hw_usrt_ctrl(link, on, dma, 4-byte)), then wait for the DSP ROM okay flag */
N2G_API int n2g_dl_usrt_on(struct n2g_link *l);
N2G_API void n2g_dl_usrt_off(struct n2g_link *l);
/* user write: copy into the TX ring (whole 4-byte words; a trailing partial word is zero padded), kick TX DMA */
N2G_API size_t n2g_dl_write(struct n2g_link *l, const uint8_t *buf, size_t len);
/* user read: copy out of the RX ring */
N2G_API size_t n2g_dl_read(struct n2g_link *l, uint8_t *buf, size_t len);
N2G_API size_t n2g_dl_rx_avail(const struct n2g_link *l);
N2G_API size_t n2g_dl_tx_free(const struct n2g_link *l);
/* worker step: finish/start DMA transfers; returns a bitmask: 1 tx progressed, 2 rx data arrived */
N2G_API int n2g_dl_poll(struct n2g_link *l);

#endif
