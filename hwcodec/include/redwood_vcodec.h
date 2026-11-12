/* SPDX-License-Identifier: MIT */
/*
 * redwood_vcodec.h -- FROZEN user/kernel ABI of /dev/redwood-vcodec (XNU character device), the MT6893 VENC/VDEC blocks as
 * raw hardware for the user-space codec host (vcodecd), and the message relay between VideoToolbox plug-ins and vcodecd.
 * Owner: hwcodec-lead. Fixed-width fields; structs are multiples of 8 bytes.
 *
 * Two minors:  0 "/dev/redwood-vcodec"  (0600 root: vcodecd; hardware ioctls + the server side of the relay)
 *              1 "/dev/redwood-vcodec-client" (0666: plug-ins; ONLY RWVC_INFO, RWVC_MSG_*, RWVC_DMA_ALLOC/FREE/RW on own buffers)
 * The kernel is policy-free: it does not know codecs. It gives (a) power/clock control per block, (b) batched register
 * access restricted to the windows below, (c) interrupt wait, (d) DMA memory below 4 GB from a fixed pool with physical
 * addresses (larb ports of the block run with the IOMMU bypassed, as the cameras and the JPEG encoder do), (e) the relay.
 *
 * Register windows (mt6893.dts; index = RWVC_WIN_*):
 *   VENC 0x17020000+0x2000, VENC_C1 0x17820000+0x2000, VDEC_SYS 0x1602f000, VDEC_BASE 0x16000000, VDEC_VLD 0x16020000,
 *   VDEC_MC 0x16021000, VDEC_MV 0x16023000, VDEC_MISC 0x16025000+0x4000, VDEC_LAT_MISC 0x16010000, VDEC_LAT_VLD 0x16011000+0x400,
 *   VDEC_RACING 0x16004000, VDEC_SOC_GCON 0x1600f000 (0x1000 each unless given)
 *   VDEC_ALL 0x16000000+0x30000 (rev 3, 2026-11-11, DEC07; RWVC_WIN_COUNT becomes 13, RWVC_ABI_VERSION stays 1 because
 *   no existing ioctl or struct changed): the vendor decoder maps 0x16000000+0x40000 and addresses
 *   sub-blocks that the ten windows above do not cover (facts/VDEC_PROTOCOL.md section 4). This one window spans the
 *   part it uses up to VDEC_SYS; the power block is decided per 4 KiB page, not per window:
 *     0x00000-0x0cfff + 0x0e000-0x0efff + 0x20000-0x2ffff -> VDEC_CORE (VDE2): VDEC_BASE 0x16000000 (+0x800 UFO),
 *       the vendor's 0x16001000/0x16002000/0x16003800, VDEC_RACING 0x16004000, VDEC_VLD 0x16020000, VDEC_MC 0x16021000,
 *       AVC_VLD 0x16022000, VDEC_MV 0x16023000, PP 0x16024000, VDEC_MISC 0x16025000, 0x16029000, VDEC_SYS 0x1602f000,
 *       larb4 0x1602e000.
 *     0x0d000 + 0x0f000-0x1ffff -> VDEC_SOC (VDE): larb5 0x1600d000 (page 0x0d), VDEC_SOC_GCON 0x1600f000,
 *       VDEC_LAT_MISC 0x16010000, VDEC_LAT_VLD 0x16011000, the vendor's LAT sub-blocks 0x16011800-0x16018800.
 *   VDEC_CORE takes a hidden reference on VDEC_SOC, so a core page is usable only while both domains are on (the core
 *   clock path needs MT_CG_SOC and MT_CG_VDEC0). The larb pages 0x0d000 (larb5) and 0x2e000 (larb4) are READ-ONLY
 *   through this window except the words in the kernel's allow-list: offset 0x00c bit 0 of both, the sleep-protect bit
 *   the vendor's vdec_hal_wait_larb_sleep_protect sets (opus-dec1's LAT trace, opus-dec2's CORE trace; DEC04 measured
 *   both handshakes). The kernel owns the rest of the larb configuration; DEC02's facts/vdec_trace/PAGES.md can add a
 *   word if a further one is measured.
 * Power blocks (clk-mt6885-pg.c): VEN 0x344 bit 17 (larb7 0x17010000, vencsys 0x17000000), VEN_CORE1 0x348 bit 18 (larb8
 * 0x17810000, 0x17800000), VDE 0x33c bit 15 (larb5 0x1600d000, vdec_soc_gcon), VDE2 0x340 bit 16 (larb4 0x1602e000, vdec_gcon).
 * Interrupts: VENC SPI 308, VENC_C1 SPI 313?, VDEC core SPI 424, VDEC LAT SPI 425 (K02 verifies; polled mode first).
 */
#ifndef REDWOOD_VCODEC_H
#define REDWOOD_VCODEC_H
#include <stdint.h>

#define RWVC_ABI_VERSION 1u
enum { RWVC_WIN_VENC = 0, RWVC_WIN_VENC_C1, RWVC_WIN_VDEC_SYS, RWVC_WIN_VDEC_BASE, RWVC_WIN_VDEC_VLD, RWVC_WIN_VDEC_MC,
	RWVC_WIN_VDEC_MV, RWVC_WIN_VDEC_MISC, RWVC_WIN_VDEC_LAT_MISC, RWVC_WIN_VDEC_LAT_VLD, RWVC_WIN_VDEC_RACING,
	RWVC_WIN_VDEC_SOC_GCON, RWVC_WIN_VDEC_ALL, RWVC_WIN_COUNT };
enum { RWVC_BLK_VENC = 0, RWVC_BLK_VENC_C1 = 1, RWVC_BLK_VDEC_SOC = 2, RWVC_BLK_VDEC_CORE = 3, RWVC_BLK_COUNT };
enum { RWVC_IRQ_VENC = 0, RWVC_IRQ_VENC_C1 = 1, RWVC_IRQ_VDEC = 2, RWVC_IRQ_VDEC_LAT = 3, RWVC_IRQ_COUNT };

struct rwvc_info {                            /* RWVC_INFO */
	uint32_t abi_version, powered_mask;       /* bit per RWVC_BLK_* */
	uint64_t pool_pa; uint32_t pool_bytes, pool_free;
	uint32_t irq_mode;                        /* 0 polled, 1 interrupts */
	uint32_t server_present;                  /* vcodecd has minor 0 open */
	uint32_t reserved[8];
};
struct rwvc_power { uint32_t block, on; };    /* RWVC_POWER (reference counted per open file; all dropped on close) */

/* Batched register access. The kernel runs the ops in order and stops at the first failure (out_done = ops completed).
 * POLL: repeat reading until (value & mask) == (value field & mask) or timeout_us (bounded at 1 s) -> ETIMEDOUT.
 * Offsets must be 4-aligned and inside the window; the block of the window must be powered, else EACCES. */
enum { RWVC_OP_READ = 0, RWVC_OP_WRITE = 1, RWVC_OP_WRITE_MASK = 2, RWVC_OP_POLL = 3, RWVC_OP_DELAY_US = 4 };
struct rwvc_reg_op { uint8_t op, window; uint16_t reserved; uint32_t offset, value, mask; };   /* READ: value = result; DELAY: value = microseconds <= 1000 */
#define RWVC_REG_MAX_OPS 2048u
struct rwvc_reg_batch { uint64_t ops; uint32_t count, out_done; uint32_t poll_timeout_us, reserved; };   /* RWVC_REGS; ops = user address of rwvc_reg_op[count] */

struct rwvc_wait_irq {                        /* RWVC_WAIT_IRQ */
	uint32_t irq, timeout_ms;                 /* RWVC_IRQ_*; timeout <= 5000 */
	uint32_t out_count;                       /* interrupts since the last wait on this line (0 with ETIMEDOUT) */
	uint32_t out_status;                      /* the block's interrupt status as the handler read it before clearing (K02 documents which register in kern/VCODEC_HW.md) */
};
struct rwvc_irq_clear { uint32_t irq, reserved; };   /* RWVC_IRQ_CLEAR: forget pending counts before starting a job */

/* DMA buffers: page-aligned pieces of the pool, zero-filled, owned by the open file that allocated them (freed on close).
 * The hardware address is `pa` (32 bits). Access from user space is by copy (RWVC_DMA_RW). Handles are global; a file may
 * use a handle it allocated, or one allocated with RWVC_DMA_SHARED by a file of the OTHER minor (client buffers are visible to
 * the server and server buffers to clients; clients never see each other's). The server learns the hardware address of a
 * client's shared buffer with RWVC_DMA_INFO (rev 2, 2026-11-11, proposed by hw-ip).
 * Client identity (hw-ik): XNU gives a character device no per-open pointer, so a relay client is a PROCESS: all its opens
 * of minor 1 share one client id and one buffer set; the close notification fires at its last close. */
#define RWVC_DMA_SHARED 1u
struct rwvc_dma_alloc { uint32_t bytes, flags; uint32_t out_handle, out_pa; };     /* RWVC_DMA_ALLOC; flags: RWVC_DMA_SHARED */
struct rwvc_dma_free { uint32_t handle, reserved; };                               /* RWVC_DMA_FREE */
struct rwvc_dma_rw { uint32_t handle, offset; uint64_t user; uint32_t bytes, write; };   /* RWVC_DMA_RW: write = 1 user -> buffer (then cache-flushed), 0 buffer -> user */
struct rwvc_dma_fill { uint32_t handle, offset, bytes, value; };                   /* RWVC_DMA_FILL: memset */
struct rwvc_dma_info { uint32_t handle, bytes, out_pa, reserved; };                /* RWVC_DMA_INFO: bytes and out_pa are results. EINVAL handle 0,
                                                                                    * ENOENT unknown, EPERM not usable by the caller (rule above) */

/* Relay: datagrams of at most RWVC_MSG_MAX bytes. A client's messages carry its kernel-assigned client id; the server
 * addresses replies to a client id. RECV blocks up to timeout_ms (EAGAIN at 0 / ETIMEDOUT). A client that closes makes the
 * kernel queue a synthetic message {client, bytes = 0} for the server. */
#define RWVC_MSG_MAX 1024u
struct rwvc_msg { uint64_t user; uint32_t bytes, client; uint32_t timeout_ms, reserved; };   /* RWVC_MSG_SEND / RWVC_MSG_RECV */

#define RWVC_IOC(dir, nr, type) ((dir) | ((uint32_t)(sizeof(type) & 0x1fff) << 16) | ((uint32_t)'V' << 8) | (nr))
#define RWVC_INFO       RWVC_IOC(0x40000000u, 1, struct rwvc_info)
#define RWVC_POWER      RWVC_IOC(0x80000000u, 2, struct rwvc_power)
#define RWVC_REGS       RWVC_IOC(0xc0000000u, 3, struct rwvc_reg_batch)
#define RWVC_WAIT_IRQ   RWVC_IOC(0xc0000000u, 4, struct rwvc_wait_irq)
#define RWVC_IRQ_CLEAR  RWVC_IOC(0x80000000u, 5, struct rwvc_irq_clear)
#define RWVC_DMA_ALLOC  RWVC_IOC(0xc0000000u, 6, struct rwvc_dma_alloc)
#define RWVC_DMA_FREE   RWVC_IOC(0x80000000u, 7, struct rwvc_dma_free)
#define RWVC_DMA_RW     RWVC_IOC(0x80000000u, 8, struct rwvc_dma_rw)
#define RWVC_DMA_FILL   RWVC_IOC(0x80000000u, 9, struct rwvc_dma_fill)
#define RWVC_MSG_SEND   RWVC_IOC(0x80000000u, 10, struct rwvc_msg)
#define RWVC_MSG_RECV   RWVC_IOC(0xc0000000u, 11, struct rwvc_msg)
#define RWVC_DMA_INFO   RWVC_IOC(0xc0000000u, 12, struct rwvc_dma_info)

/* Fixed physical claims below 4 GB (nothing is carved out in 0xC0000000..0xDFE70000; cameras use 0xD0000000..0xD8000000):
 *   JPEG   0xD8000000 .. 0xDA800000  (source 24 MiB at +0, output 16 MiB at +0x1800000)   bsd/dev/nord2_jpegdev.inc
 *   vcodec 0xDA800000 .. 0xDFE00000  (the RWVC pool, 86 MiB)                              bsd/dev/nord2_vcodec.inc */
#define RW_CLAIM_JPEG_PA    0xd8000000u
#define RW_CLAIM_JPEG_BYTES 0x02800000u
#define RW_CLAIM_VCODEC_PA    0xda800000u
#define RW_CLAIM_VCODEC_BYTES 0x05600000u
#endif
