#ifndef NORD2_AUDIO_I2C8_H
#define NORD2_AUDIO_I2C8_H
#include <stdint.h>

/*
 * Nord2 I2C8 hardware-controller (I2Cv2 PIO) read transport for the two
 * TFA98xx amplifiers, milestone 1b. Derived from the selected Android
 * i2c-mtk.c/h and from the reviewed battery transport
 * shim/nord2-xnu/mtk_i2c_read.c (read-only reference, not modified), then
 * re-derived for bus 8, which is a MULTI-CHANNEL controller:
 *   shadow/common registers  0x11d04000 (i2c_*_shadow, i2c-mtk.c:87-89, 427-462)
 *   AP channel               0x11d04100 (DT ch_offset_default = 0x100)
 *   CCU channel              0x11d04200 (DT ch_offset_ccu = 0x200, a second master)
 *   APDMA channel            0x11000780 (DT reg[1])
 *   clock gate               0x11d05000 +e00 sta/+e04 clr/+e08 set, bit4
 *                            (clk-mt6885-i2c.c:110-135, CLK_IMPS_AP_I2C8_RO)
 * I2C8 also carries the always-on DDR (0x51) and UFS (0x56) rail bucks.
 *
 * Peripheral side: only WRRD "register pointer, then read two bytes" to
 * slaves 0x34/0x37 with pointers 0x03/0x00/0x14. No amplifier data write.
 * Controller side (these ARE writes): AP-channel configuration registers
 * (saved and restored, including the source HW timeout at 0x4c), INTR_STAT
 * W1C, FIFO_ADDR_CLR, DATA_PORT (one pointer byte), START. Never written:
 * clock gate, mux/PLL, pinmux, shadow or CCU registers, APDMA, SOFTRESET,
 * RESUME_ARBIT.
 *
 * Preconditions are refused, never repaired: a gated controller is not
 * ungated (that would enable an unknown controller to inspect it); CAM power
 * on (the CCU master could run) refuses; any START/RESUME_ARBIT bit on any
 * channel, DMA mode, active APDMA, FIFO content or unknown inherited status
 * refuses. An AP-channel I3C enable (bit15 of V2 DMA_FSM_DEBUG at 0xec, the
 * register the selected driver reads at i2c-mtk.c:1040; NOT DEBUGSTAT 0xe4,
 * i2c-mtk.h:275,295) also refuses: this transport implements only the I2C
 * WRRD path, not the source HFIFO/I3C setup (i2c-mtk.c:1039-1046). Every non-clean outcome latches offline (sticky):
 * later calls make zero callbacks. After any write, a failure is "uncertain":
 * no cleanup, replay, reset, recovery clocks or next-slave access.
 *
 * A completed transfer whose only error is ACKERR (INTR_STAT exactly
 * I2C_TRANSAC_COMP|I2C_ACKERR, START already clear) is a clean NACK. The
 * selected source returns -EREMOTEIO for that exact multi-channel case
 * without mt_i2c_init_hw()/RESUME_ARBIT (i2c-mtk.c:1326-1330), so this
 * transport clears only its own two status bits, clears the FIFO and
 * restores the saved configuration. That is a bounded, non-uncertain result:
 * na8_read16() returns 1, no data byte is read, and Linux return stays
 * allowed.
 */
struct na8_io {
	int (*read)(void *, uint32_t, unsigned, uint32_t *);   /* width 1/2/4 */
	int (*write)(void *, uint32_t, unsigned, uint32_t);
	int (*delay_us)(void *, unsigned);
};

/* failure: 1 mux, 2 PLL, 3 timing, 4 pinmux, 5 controller gated, 6 AP START,
 * 7 DMA, 8 inherited IRQ, 9 FIFO, 10 read callback, 11 write callback,
 * 12 delay, 13 timeout, 14 completion error, 15 byte bounds,
 * 16 CAM power (CCU may run), 17 CCU channel busy, 18 shadow START,
 * 19 argument not allowed, 20 existing GPIO input-enable/pads not high,
 * 21 AP-channel I3C mode enabled. */
struct na8_state {
	unsigned entered, inflight, offline, mutated, uncertain, polls, calls;
	unsigned nack, nacks, i3c, restoring;
	int error;
	unsigned stage, failure, last_width, last_write, last_valid;
	uint32_t last_addr, last_value, mux, parent_hz, pinmux, gate, pwr, pwr2;
	uint32_t ifr1, ifr2, dma_en, sh_start, ccu_start, ccu_ctrl, prior_irq, prior_ack;
	uint32_t completion_irq;
	unsigned dma_checked;
};

/* One read of a 16-bit big-endian TFA register (tfa98xx.c:4705-4709).
 *  0 success (value valid)
 *  1 clean NACK: transaction completed, status/FIFO cleared, config restored,
 *    no data byte was read, nothing is uncertain, Linux return may proceed
 * -1 refused before any write (offline, hardware untouched)
 * -2 uncertain after a write (offline, Linux return must be refused) */
int na8_read16(const struct na8_io *, void *, struct na8_state *,
    unsigned slave, unsigned reg, uint16_t *value);

/* 1 if Linux return may proceed: nothing in flight and no write was
 * followed by a non-clean outcome. Pure: performs no callback. A clean NACK
 * does not refuse return. */
int na8_return_ok(const struct na8_state *);

enum na8_class {
	NA8_UNPROBED = 0,
	NA8_FAILED = 1,        /* transport refused/uncertain; sequence stopped */
	NA8_UNKNOWN = 2,       /* REV low byte not handled by tfa98xx.c probe */
	NA8_OTHER_FAMILY = 3,  /* handled by the driver but not a 9873/9874 */
	NA8_UNLISTED_REV = 4,  /* 0x73/0x74 family, revision not in tfa_init.c */
	NA8_SUPPORTED = 5,     /* exact revision with a source init table */
	NA8_NACK = 6,          /* clean address/data NACK; no REV value read */
	NA8_PARTIAL = 7,       /* revision5: supported REV read, then a clean NACK on
	                        * 0x00/0x14; nothing further on that amp */
};

struct na8_amp {
	uint8_t addr;
	unsigned cls, reads;
	uint16_t rev, sys0, status14;
	unsigned pwdn, ampe, manstate;
	int rc;
};

/* REV (0x03) first. Only an exact supported revision may be followed by
 * 0x00 and 0x14 on that slave. A clean NACK (on REV, or revision5: on 0x00/
 * 0x14 after a supported REV -> NA8_PARTIAL) stops only that amplifier; no
 * register pointer is invented or retried; the other amplifier is still
 * probed because the controller is verified idle and restored again. Any
 * uncertain/refused transport outcome stops the whole sequence; the second
 * amplifier is probed only after clean completions. */
int na8_probe(const struct na8_io *, void *, struct na8_state *, struct na8_amp amp[2]);
unsigned na8_classify(uint16_t rev);
#endif
