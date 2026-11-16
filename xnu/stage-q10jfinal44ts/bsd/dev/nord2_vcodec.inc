/* SPDX-License-Identifier: MIT */
/*
 * nord2_vcodec.inc -- /dev/redwood-vcodec, the hardware half: the MT6893 VENC (0x17020000, core 1 0x17820000) and VDEC
 * (0x16000000, LAT 0x16010000) as raw blocks for the user-space codec host, plus the register-window DMA pool.
 *
 * Included once by bsd/dev/sweet_touch.c, after nord2_hwcodec.inc (this file uses its helpers nothing else: it is
 * self-contained apart from ni_rd/ni_wr/ni_mask and the kernel facilities listed below).  Everything runs on demand: no
 * register is touched until an ioctl asks for it.  No phone has run this file yet; the source of every number is
 * kern/VCODEC_HW.md (path:line for each).
 *
 * Kernel facilities used: ml_io_map_unmappable(), copyin/copyout, lck_mtx, delay(), msleep(), mach_absolute_time(),
 * cdevsw_add/devfs_make_node, printf.  Waits longer than 100 us sleep with msleep() and drop n2vc_mtx; every wake
 * re-validates the open file and the block's power before any register access.
 * Host test: gpu/hwcodec/kern/test_k02 (compiled with -DN2VC_HOST against a register model; the model fails the test on
 * any access to a block's registers, gates or LARB while its domain is off/isolated/protected, on any write to another
 * user's LARB port or protection bit, and on every wait that does not end).
 */
#ifndef N2VC_ABI_HDR
#define N2VC_ABI_HDR "redwood_vcodec.h"
#endif
#include N2VC_ABI_HDR

#ifdef N2VC_HOST
/* the host model supplies these (test_k02/t_k02.c) */
typedef unsigned long u_long;
typedef char *caddr_t;
struct proc;
extern void *n2vc_host_map(uint32_t pa, uint32_t bytes);
extern void n2vc_host_us(uint32_t us);
extern uint64_t n2vc_host_now_us(void);
extern void n2vc_host_sleep(uint32_t us);
extern int n2vc_host_copyin(uint64_t uaddr, void *kaddr, uint32_t bytes);
extern int n2vc_host_copyout(const void *kaddr, uint64_t uaddr, uint32_t bytes);
#define n2vc_pa_map(pa, bytes, io) ((vm_offset_t)(uintptr_t)n2vc_host_map((pa), (bytes)))
#define n2vc_us(us) n2vc_host_us((uint32_t)(us))
#define n2vc_now_us() n2vc_host_now_us()
#define n2vc_sleep_us(us) n2vc_host_sleep((uint32_t)(us))
#define n2vc_copyin(u, k, n) n2vc_host_copyin((uint64_t)(u), (k), (uint32_t)(n))
#define n2vc_copyout(k, u, n) n2vc_host_copyout((k), (uint64_t)(u), (uint32_t)(n))
#define n2vc_lock() ((void)0)
#define n2vc_unlock() ((void)0)
#define N2VC_KERNEL 0
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/types.h>
#include <miscfs/devfs/devfs.h>
#include <libkern/copyio.h>
#include <kern/locks.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, uint32_t);
#define n2vc_pa_map(pa, bytes, io) ml_io_map_unmappable((pa), (bytes), (io) ? VM_WIMG_IO : VM_WIMG_DEFAULT)
#define n2vc_us(us) delay((int)(us))
#define n2vc_copyin(u, k, n) copyin((user_addr_t)(u), (k), (size_t)(n))
#define n2vc_copyout(k, u, n) copyout((k), (user_addr_t)(u), (size_t)(n))
static LCK_GRP_DECLARE(n2vc_grp, "nord2-vcodec");
static LCK_MTX_DECLARE(n2vc_mtx, &n2vc_grp);
#define n2vc_lock() lck_mtx_lock(&n2vc_mtx)
#define n2vc_unlock() lck_mtx_unlock(&n2vc_mtx)
#define N2VC_KERNEL 1
#endif

/* ---- facts (kern/VCODEC_HW.md §0-§6) --------------------------------------------------------------------------- */
#define N2VC_SPM_PA      0x10006000u
#define N2VC_INFRA_PA    0x10001000u
#define N2VC_TOP_PA      0x10000000u
#define N2VC_MM_OFF      0x2d0u
#define N2VC_MM_SET      0x2d4u
#define N2VC_MM_CLR      0x2d8u
#define N2VC_MM_STA      0x2ecu
#define N2VC_MM2_OFF     0xdc8u
#define N2VC_MM2_SET     0xdccu
#define N2VC_MM2_CLR     0xdd0u
#define N2VC_MM2_STA     0xdd8u
#define N2VC_PWR_STA     0x16cu
#define N2VC_PWR_STA2    0x170u
#define N2VC_DIS_BIT     (1u << 20)
#define N2VC_MDP_BIT     (1u << 19)       /* MDP domain: SMI common of larb8 (VENC_C1) and larb4 (VDEC_CORE) */
#define N2VC_KEY         ((0xb16u << 16) | 1u)
#define N2VC_CFG1        0x020u           /* CLK_CFG_1: disp_sel bits 3:0 gate 7, mdp_sel bits 11:8 gate 15 (clk-mt6885.c:1693-1702) */
#define N2VC_DISP_GATE   (1u << 7)
#define N2VC_MDP_GATE    (1u << 15)
#define N2VC_GICD_PA     0x0c000000u      /* GIC distributor (dts gic: 0x0c000000 + 0x40000); read-only ISPENDR probe */
#define N2VC_CFG13       0x0e0u
#define N2VC_CFG14       0x0f0u
#define N2VC_CFG_UPDATE1 0x008u
/* venc_sel: CLK_CFG_13 bits 27:24 parent, bit 31 gate, update CLK_CFG_UPDATE1 bit 24 (clk-mt6885.c:1958-1963).
 * venc_parents[] (clk-mt6885.c:1221-1237): 4 = mainpll_d4_d2 = 273 MHz (mt6893.dts mmdvfs venc_step3, lowest VENC OPP),
 * 12 = univpll_d4 = 624 MHz (venc_step0, what the bootloader leaves: CLK_CFG_13 0x0c030601 on the phone, q10hw1). */
#ifndef N2VC_VENC_SLOW
#define N2VC_VENC_SLOW 1u                 /* 1 = a VENC power-on run by vcodecd (RWVC_POWER) selects the 273 MHz parent
                                           * (root's decision for the first phone encode, 11 Nov); 0 = 624 MHz. The
                                           * JPEG driver's shared n2vc_ven_get() always asks for 624 MHz and, like every
                                           * path here, never rewrites a running mux. */
#endif
#define N2VC_VENC_PARENT_SLOW 4u          /* mainpll_d4_d2, 273 MHz */
#define N2VC_VENC_PARENT_FAST 12u         /* univpll_d4, 624 MHz */
#define N2VC_VDEC_PARENT 14u              /* univpll_d4 in vdec_parents/vdec_lat_parents */

/* 0312 (opus-enc4k, 12 Nov): kern.n2vc.venc_fast selects vcodecd's venc_sel parent at run time.  0 (default) = what
 * N2VC_VENC_SLOW says (273 MHz in this series), 1 = 624 MHz (univpll_d4, venc_step0, the bootloader's value).  Nothing is
 * re-muxed by the sysctl itself: RWVC_POWER passes the parent to n2vc_hw_on(), which programs venc_sel only while VEN and
 * VENC_C1 are both off (the existing rule).  The vendor library powers the VENC on and off around every frame (vc16..vc21
 * logs: "hw power block=0/1 on=1 ... on=0" per frame, cores alternating), so a write takes effect at the next frame.
 * Voltage: the Android kernel raises vcore through mmdvfs before 624 MHz (venc_step0 needs the high vcore OPP); this
 * kernel has no vcore/DVFSRC control, the phone runs at whatever OPP the bootloader/SPM left (see the 0312 notes). */
static unsigned int n2vc_venc_fast;
static unsigned int n2vc_spin_us = 2000u;     /* sysctl kern.n2vc.spin_us, 0 = sleep from the first poll */
static unsigned int n2vc_sleep_step = 200u;   /* sysctl kern.n2vc.sleep_us */
static inline unsigned
n2vc_vcodecd_venc_parent(void)
{
	if (n2vc_venc_fast) { return N2VC_VENC_PARENT_FAST; }
	return N2VC_VENC_SLOW ? N2VC_VENC_PARENT_SLOW : N2VC_VENC_PARENT_FAST;
}
#if N2VC_KERNEL
#include <sys/sysctl.h>
static int
n2vc_sysctl_venc_fast SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1, arg2)
	unsigned int v = n2vc_venc_fast;
	int err = sysctl_handle_int(oidp, &v, 0, req);
	if (err || req->newptr == USER_ADDR_NULL) { return err; }
	if (v > 1u) { return EINVAL; }
	n2vc_lock();
	unsigned int was = n2vc_venc_fast;
	n2vc_venc_fast = v;
	n2vc_unlock();
	printf("NORD2 VCODEC: kern.n2vc.venc_fast %u -> %u (%s at the next VENC power-on with VEN off)\n", was, v,
	    v ? "624 MHz" : (N2VC_VENC_SLOW ? "273 MHz" : "624 MHz"));
	return 0;
}
SYSCTL_NODE(_kern, OID_AUTO, n2vc, CTLFLAG_RW | CTLFLAG_LOCKED, 0, "nord2 vcodec");
SYSCTL_UINT(_kern_n2vc, OID_AUTO, spin_us, CTLFLAG_RW | CTLFLAG_LOCKED, &n2vc_spin_us, 0, "0314: spin-poll span of a wait (us)");
SYSCTL_UINT(_kern_n2vc, OID_AUTO, sleep_us, CTLFLAG_RW | CTLFLAG_LOCKED, &n2vc_sleep_step, 0, "0314: sleep step after it (50..1000 us)");
SYSCTL_PROC(_kern_n2vc, OID_AUTO, venc_fast, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2vc_sysctl_venc_fast, "I",
    "vcodecd VENC clock: 0 = 273 MHz (default), 1 = 624 MHz; applied at the next power-on while VEN is off");
#endif

#define N2VC_POOL_PAGES  (RW_CLAIM_VCODEC_BYTES / 4096u)
#define N2VC_POOL_WORDS  ((N2VC_POOL_PAGES + 31u) / 32u)
/* 0313: the pool must stay below 4 GB (32-bit DMA addresses, IOMMU bypassed) inside the free gap SCP-reserved end ..
 * vpu_binary (live FDT mblocks), and a whole number of bitmap words (n2vc_pool_free_pages counts words). */
_Static_assert(RW_CLAIM_VCODEC_PA >= 0x9ff00000u && (uint64_t)RW_CLAIM_VCODEC_PA + RW_CLAIM_VCODEC_BYTES <= 0xbd6c0000ull &&
    (RW_CLAIM_VCODEC_PA & 0xfffu) == 0 && N2VC_POOL_PAGES % 32u == 0, "vcodec pool outside the free low-DRAM gap");
#define N2VC_DMA_MAX     1024u
#define N2VC_DMA_PER_FILE 128u
#define N2VC_MAX_FILES   16u
#define N2VC_MAX_SLOTS   16u
#define N2VC_MM_MAX      24u
#define N2VC_CHUNK       64u
#define N2VC_RW_CHUNK    0x00100000u     /* RWVC_DMA_RW copies at most 1 MiB per copyin/copyout */
#define N2VC_BATCH_US    2000000u         /* one RWVC_REGS batch may run this long at most */
#define N2VC_POLL_MAX_US 1000000u
/* 0314: adaptive wait for WAIT_IRQ and RWVC_OP_POLL.  For the first kern.n2vc.spin_us of a wait the status is re-read
 * every N2VC_SPIN_STEP_US (delay(), n2vc_mtx dropped around it); after that msleep() in kern.n2vc.sleep_us steps
 * (clamped 50..1000).  Was a flat 500 us msleep (plus timer-coalescing leeway) from the first poll. */
#define N2VC_SPIN_STEP_US 20u
#define N2VC_IRQ_POLL_US N2VC_SPIN_STEP_US   /* the first step of a wait (spin_us > 0); test_k02 checks it */
/* (n2vc_spin_us / n2vc_sleep_step: defined with the 0312 sysctl node above) */
#define N2VC_WAIT_MS_MAX 5000u

#define N2VC_NONE 0xffu

/* ABI rev 3 (2026-11-11, DEC07): RWVC_WIN_VDEC_ALL = 12, 0x16000000+0x30000, block per 4 KiB page.  The tree's
 * include/redwood_vcodec.h is still rev 2 while this patch waits for hw-lead, so the constant and the window count are
 * supplied here when it is absent; with rev 3 in place the two definitions collapse to the header's and only N2VC_WIN_N
 * remains (then delete the #ifndef block and put RWVC_WIN_COUNT back). */
#ifndef RWVC_WIN_VDEC_ALL
#define RWVC_WIN_VDEC_ALL 12u
#define N2VC_WIN_N 13u
#else
#define N2VC_WIN_N RWVC_WIN_COUNT
#endif
#define N2VC_BLK_PER_PAGE 0xfeu       /* n2vc_wins[].blk: the block comes from the per-page table, not the window */

/* ---- per-block tables ------------------------------------------------------------------------------------------ */
struct n2vc_win { uint32_t pa, bytes; uint8_t blk; };
struct n2vc_gate { uint16_t set_off, clr_off, sta_off; uint8_t bit; };
struct n2vc_larb { uint32_t pa; uint8_t nports; uint8_t int_conf; const uint8_t *ostdl; uint32_t ports; uint32_t nsmask; };
struct n2vc_blk {
	const char *name;
	uint16_t con;                 /* SPM power control register */
	uint8_t sta;                  /* status bit number 15..18 */
	uint8_t dep;                  /* block that must be powered first, N2VC_NONE if none */
	uint32_t step1_mm, step2_mm;  /* bus-protect bits in INFRA MM (0x2d0..) */
	uint32_t step1_mm2, step2_mm2;/* bus-protect bits in INFRA MM_2 (0xdc8..) */
	const struct n2vc_gate *gates; uint8_t ngates;
	const struct n2vc_larb *larb;
	uint32_t gate_pa;             /* gate controller block */
	uint16_t post_write;          /* pg.c fidelity value written to the first SET register, 0 = none */
	uint8_t mdp_smi;              /* its larb hangs off the MDP SMI common (needs the MDP domain), else the DISP one */
};

static const struct n2vc_win n2vc_wins[N2VC_WIN_N] = {
	[RWVC_WIN_VENC]          = { 0x17020000u, 0x3000u, RWVC_BLK_VENC },   /* vpud maps 0x3000; JPGENC starts at 0x17030000 */
	[RWVC_WIN_VENC_C1]       = { 0x17820000u, 0x3000u, RWVC_BLK_VENC_C1 },
	[RWVC_WIN_VDEC_SYS]      = { 0x1602f000u, 0x1000u, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_BASE]     = { 0x16000000u, 0x1000u, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_VLD]      = { 0x16020000u, 0x1000u, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_MC]       = { 0x16021000u, 0x1000u, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_MV]       = { 0x16023000u, 0x1000u, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_MISC]     = { 0x16025000u, 0x4000u, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_LAT_MISC] = { 0x16010000u, 0x1000u, RWVC_BLK_VDEC_SOC },
	[RWVC_WIN_VDEC_LAT_VLD]  = { 0x16011000u, 0x0400u, RWVC_BLK_VDEC_SOC },
	[RWVC_WIN_VDEC_RACING]   = { 0x16004000u, 0x1000u, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_SOC_GCON] = { 0x1600f000u, 0x1000u, RWVC_BLK_VDEC_SOC },
	[RWVC_WIN_VDEC_ALL]      = { 0x16000000u, 0x30000u, N2VC_BLK_PER_PAGE },
};

/* smi_conf.h mt6885: smi_larb7_init_pair :247-276, smi_larb8_init_pair :277-305, smi_larb4 :220-232, smi_larb5 :234-243 */
static const uint8_t n2vc_ostdl7[27] = { 1, 4, 1, 1, 1, 1, 1, 1, 1, 4, 4, 1, 4, 1, 0xa, 6, 1, 0xa, 6, 1, 1, 1, 1, 5, 3, 3, 4 };
static const uint8_t n2vc_ostdl8[27] = { 1, 4, 1, 1, 1, 1, 1, 1, 1, 4, 4, 1, 4, 1, 0xa, 6, 1, 0xa, 6, 1, 1, 1, 1, 5, 3, 3, 4 };
static const uint8_t n2vc_ostdl4[11] = { 0x28, 0x19, 0xb, 1, 1, 1, 1, 1, 1, 4, 1 };
static const uint8_t n2vc_ostdl5[8] = { 1, 1, 4, 1, 1, 1, 1, 0x16 };
/* VENC/VENC_C1 own larb7/larb8 ports 0-6 and 13-22 (dt-bindings/memory/mt6885-larb-port.h:91-113/119-141). 7-12 and
 * 23-26 are the JPEG encoder's (K01) and are never written here. */
#define N2VC_PORTS_VENC ((uint32_t)((1u << 7) - 1u) | (uint32_t)(0x3ffu << 13))

/* NON_SEC_CON(p) bits cleared on our ports: bit 0 MMU_EN and bits 8-15 address bits 32+ (pseudo_m4u_v2.c:563-575); on the
 * VENC larbs additionally bits 16-19, the MMSRAM route Linux sets on ports 5,6,13,14,21,22 (smi_drv.c:498-524 called from
 * mt6885/mtk_vcodec_enc_pm.c:308-317).  This kernel does not power MMSRAM, so every VENC port goes to DRAM. */
#define N2VC_NS_IOMMU    0x0000ff01u
#define N2VC_NS_VENC     0x000fff01u
static const struct n2vc_larb n2vc_larb7 = { 0x17010000u, 27, 1, n2vc_ostdl7, N2VC_PORTS_VENC, N2VC_NS_VENC };
static const struct n2vc_larb n2vc_larb8 = { 0x17810000u, 27, 1, n2vc_ostdl8, N2VC_PORTS_VENC, N2VC_NS_VENC };
static const struct n2vc_larb n2vc_larb4 = { 0x1602e000u, 11, 0, n2vc_ostdl4, 0x7ffu, N2VC_NS_IOMMU };
static const struct n2vc_larb n2vc_larb5 = { 0x1600d000u,  8, 0, n2vc_ostdl5, 0x0ffu, N2VC_NS_IOMMU };

/* gate layout: VENC/VENC_C1 set 0x4, clear 0x8, status 0x0 (clk-mt6885-ven.c:27-31,103-116);
 * VDE/VDE2 three word pairs 0x0/0x4, 0x200/0x204, 0x8/0xc with status = set offset (clk-mt6885-vde.c:28-39,142-151). */
static const struct n2vc_gate n2vc_g_venc[3]  = { { 0x4, 0x8, 0x0, 0 }, { 0x4, 0x8, 0x0, 4 }, { 0x4, 0x8, 0x0, 28 } };
static const struct n2vc_gate n2vc_g_venc1[3] = { { 0x4, 0x8, 0x0, 0 }, { 0x4, 0x8, 0x0, 4 }, { 0x4, 0x8, 0x0, 28 } };
static const struct n2vc_gate n2vc_g_vde[3]   = { { 0x0, 0x4, 0x0, 0 }, { 0x200, 0x204, 0x200, 0 }, { 0x8, 0xc, 0x8, 0 } };
static const struct n2vc_gate n2vc_g_vde2[3]  = { { 0x0, 0x4, 0x0, 0 }, { 0x200, 0x204, 0x200, 0 }, { 0x8, 0xc, 0x8, 0 } };

/* mdp_smi: smi_bus_prepare_enable() (smi_drv.c:198-232) puts larb7 and larb5 on the DISP common (smi 21/24/25) and larb8
 * and larb4 on the MDP common (smi 22/26/27: mdp_smi_common@1f002000, clocks SCP_SYS_MDP + CLK_MDP_SMI1/2; iommu1
 * m4u@1f027000 serves larb4/larb8).  The power-domain parent of all four is PG_DIS (pg.c:4994-4999). */
static const struct n2vc_blk n2vc_blocks[RWVC_BLK_COUNT] = {
	[RWVC_BLK_VENC]       = { "VENC",       0x344, 17, N2VC_NONE,           1u << 26, 1u << 27, 1u << 0, 1u << 1,
	                          n2vc_g_venc, 3, &n2vc_larb7, 0x17000000u, 0x4, 0 },
	[RWVC_BLK_VENC_C1]    = { "VENC_C1",    0x348, 18, RWVC_BLK_VENC,      (1u << 28) | (1u << 30), (1u << 29) | (1u << 31), 0, 0,
	                          n2vc_g_venc1, 3, &n2vc_larb8, 0x17800000u, 0x4, 1 },
	[RWVC_BLK_VDEC_SOC]   = { "VDEC_SOC",   0x33c, 15, N2VC_NONE,           1u << 24, 1u << 25, 0, 0,
	                          n2vc_g_vde, 3, &n2vc_larb5, 0x1600f000u, 0, 0 },
	[RWVC_BLK_VDEC_CORE]  = { "VDEC_CORE",  0x340, 16, RWVC_BLK_VDEC_SOC,  0, 0, 1u << 6, 1u << 7,
	                          n2vc_g_vde2, 3, &n2vc_larb4, 0x1602f000u, 0, 1 },
};

/* IRQ lines: status/clear register and the block that must be powered (kern/VCODEC_HW.md §5). */
struct n2vc_irq { uint32_t status_off, ack_off; uint32_t match, clear_mask; uint8_t win, blk; };
static const struct n2vc_irq n2vc_irqs[RWVC_IRQ_COUNT] = {
	[RWVC_IRQ_VENC]     = { 0x05c, 0x060, 0x0bfu, 0x0bfu, RWVC_WIN_VENC,      RWVC_BLK_VENC },
	[RWVC_IRQ_VENC_C1]  = { 0x05c, 0x060, 0x0bfu, 0x0bfu, RWVC_WIN_VENC_C1,   RWVC_BLK_VENC_C1 },
	[RWVC_IRQ_VDEC]     = { 0x0a4, 0x0a4, 0x10000u, 0, RWVC_WIN_VDEC_MISC,    RWVC_BLK_VDEC_CORE },
	[RWVC_IRQ_VDEC_LAT] = { 0x0a4, 0x0a4, 0x10000u, 0, RWVC_WIN_VDEC_LAT_MISC, RWVC_BLK_VDEC_SOC },
};

/* ---- state ----------------------------------------------------------------------------------------------------- */
struct n2vc_dma {
	uint8_t used, flags;
	uint16_t gen;
	uint32_t page, npages, bytes;
	struct n2vc_open *owner;
};

struct n2vc_open {
	uint8_t used;
	uint8_t minor;
	uint16_t refs[RWVC_BLK_COUNT];
	uint32_t irq_seen[RWVC_IRQ_COUNT];
	uint32_t ndma;
	uint32_t id;                        /* unique per open: a destroyed slot may be reused while a waiter sleeps */
	struct proc *proc;                  /* kernel: identity of a minor-1 client (IK's relay); NULL on host */
};

static struct {
	uint8_t inited;
	uint8_t on[RWVC_BLK_COUNT];
	uint32_t refs[RWVC_BLK_COUNT];      /* every reference: open files, child blocks, n2vc_ven_get() */
	uint32_t hidden[RWVC_BLK_COUNT];    /* how many of refs[] are not owned by an open file */
	uint8_t dep_ref[RWVC_BLK_COUNT];    /* this block currently holds its hidden reference on its parent */
	uint32_t pool_bits[N2VC_POOL_WORDS];
	struct n2vc_dma dma[N2VC_DMA_MAX];
	struct n2vc_open files[N2VC_MAX_FILES];
	struct { uint32_t pa, bytes; vm_offset_t va; } mm[N2VC_MM_MAX];
	uint8_t nmm;
	uint32_t next_file_id;
	vm_offset_t win_va[N2VC_WIN_N];     /* 0314: n2vc_wins[] mappings, looked up once (mm[] entries are never unmapped) */
} n2vc;

/* Kernel time and sleeping.  A wait longer than 100 us sleeps with msleep(), which drops n2vc_mtx: another thread may
 * close the file or power the block off while we sleep, so every caller re-validates f->used and n2vc.on[blk] after a
 * wake before touching a register.  The host shim keeps a deterministic fake clock and runs an optional callback
 * "during" the sleep (test_k02 uses it to power a block off / close a file under a sleeping waiter). */
#ifdef N2VC_HOST
/* n2vc_now_us() and n2vc_sleep() are the macros above (n2vc_host_now_us / n2vc_host_sleep). */
#else
static uint64_t
n2vc_now_us(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000u;
}

static void
n2vc_sleep_us(uint32_t us)
{
	if (us < 100u) { lck_mtx_unlock(&n2vc_mtx); delay((int)us); lck_mtx_lock(&n2vc_mtx); return; }
	struct timespec ts = { 0, (long)us * 1000 };
	(void)msleep(&n2vc, &n2vc_mtx, PZERO, "n2vc-wait", &ts);
}
#endif

/* one step of a wait that started `waited` us ago; drops n2vc_mtx in both branches (callers revalidate after it) */
static void
n2vc_wait_step(uint64_t waited)
{
	unsigned int sl = n2vc_sleep_step;
	if (sl < 50u) { sl = 50u; } else if (sl > 1000u) { sl = 1000u; }
	n2vc_sleep_us(waited < n2vc_spin_us ? N2VC_SPIN_STEP_US : sl);
}

/* ---- small helpers --------------------------------------------------------------------------------------------- */
static vm_offset_t
n2vc_pa(uint32_t pa, uint32_t bytes, int io)
{
#if !N2VC_KERNEL
	(void)io;
#endif
	for (unsigned i = 0; i < n2vc.nmm; i++) {
		if (n2vc.mm[i].pa == pa && n2vc.mm[i].bytes >= bytes) { return n2vc.mm[i].va; }
	}
	vm_offset_t va = n2vc_pa_map(pa, bytes, io);
	if (!va) { return 0; }
	if (n2vc.nmm < N2VC_MM_MAX) { n2vc.mm[n2vc.nmm].pa = pa; n2vc.mm[n2vc.nmm].bytes = bytes; n2vc.mm[n2vc.nmm].va = va; n2vc.nmm++; }
	return va;
}

static vm_offset_t
n2vc_win(unsigned w)
{
	if (!n2vc.win_va[w]) { n2vc.win_va[w] = n2vc_pa(n2vc_wins[w].pa, n2vc_wins[w].bytes, 1); }
	return n2vc.win_va[w];
}

static bool
n2vc_wait(vm_offset_t base, uint32_t off, uint32_t mask, uint32_t want)
{
	if (!base) { return false; }
	for (unsigned i = 0; i < 2000; i++) {
		if ((ni_rd(base, off) & mask) == want) { return true; }
		n2vc_us(10);
	}
	return false;
}

static void
n2vc_cache(vm_offset_t va, uint32_t len)
{
#if N2VC_KERNEL
	for (vm_offset_t a = va & ~63ul; a < va + len; a += 64) { __asm__ volatile("dc civac, %0" :: "r"(a) : "memory"); }
	__asm__ volatile("dsb sy" ::: "memory");
#else
	(void)va; (void)len;
#endif
}

/* ---- power ----------------------------------------------------------------------------------------------------- */
static bool
n2vc_status_on(const struct n2vc_blk *b)
{
	vm_offset_t spm = n2vc_pa(N2VC_SPM_PA, 0x1000, 1);
	uint32_t sta = 1u << b->sta;
	return spm && (ni_rd(spm, N2VC_PWR_STA) & sta) && (ni_rd(spm, N2VC_PWR_STA2) & sta);
}

/* domain powered, out of reset/isolation, SRAM on and bus protection released */
static bool
n2vc_usable(unsigned blk)
{
	const struct n2vc_blk *b = &n2vc_blocks[blk];
	vm_offset_t spm = n2vc_pa(N2VC_SPM_PA, 0x1000, 1), infra = n2vc_pa(N2VC_INFRA_PA, 0x1000, 1);
	if (!spm || !infra || !n2vc_status_on(b)) { return false; }
	uint32_t con = ni_rd(spm, b->con);
	if ((con & 0x1fu) != 0x0du) { return false; }
	if (con & ((1u << 8) | (1u << 12))) { return false; }
	if ((b->step1_mm | b->step2_mm) && (ni_rd(infra, N2VC_MM_OFF) & (b->step1_mm | b->step2_mm))) { return false; }
	if ((b->step1_mm2 | b->step2_mm2) && (ni_rd(infra, N2VC_MM2_OFF) & (b->step1_mm2 | b->step2_mm2))) { return false; }
	return true;
}

/* one top mux: leave a running one alone; otherwise CLR the field+gate, SET the parent, pulse update, verify. */
static int
n2vc_mux(uint32_t cfg, unsigned shift, unsigned gate_bit, unsigned update_bit, unsigned parent, const char *what)
{
	vm_offset_t top = n2vc_pa(N2VC_TOP_PA, 0x1000, 1);
	if (!top) { return ENXIO; }
	uint32_t v = ni_rd(top, cfg);
	if (!(v & (1u << gate_bit)) && (((v >> shift) & 0xfu) != 0)) { return 0; }
	uint32_t clr = (0x8fu << shift);
	ni_wr(top, cfg + 8, clr);
	ni_wr(top, cfg + 4, (uint32_t)parent << shift);
	ni_wr(top, N2VC_CFG_UPDATE1, 1u << update_bit);
	n2vc_us(100);
	v = ni_rd(top, cfg);
	if ((v & (1u << gate_bit)) || (((v >> shift) & 0xfu) != parent)) {
		printf("NORD2 VCODEC: %s mux %#x did not take parent %u (reads %#x)\n", what, cfg, parent, v);
		return EIO;
	}
	return 0;
}

/*
 * venc_sel. Unlike n2vc_mux() this rewrites even a running mux when the wanted parent differs, so its only caller
 * (n2vc_hw_on) has already established that VEN and VENC_C1 are off. vcodecd's RWVC_POWER asks for N2VC_VENC_SLOW's
 * parent (root's 273 MHz decision); the JPEG driver's shared n2vc_ven_get() asks for 624 MHz, the phone's boot value.
 */
static int
n2vc_venc_mux(unsigned want)
{
	vm_offset_t top = n2vc_pa(N2VC_TOP_PA, 0x1000, 1);
	if (!top) { return ENXIO; }
	uint32_t v = ni_rd(top, N2VC_CFG13), was = v;
	if (!(v & (1u << 31)) && (((v >> 24) & 0xfu) == want)) { return 0; }
	ni_wr(top, N2VC_CFG13 + 8, 0x8fu << 24);
	ni_wr(top, N2VC_CFG13 + 4, (uint32_t)want << 24);
	ni_wr(top, N2VC_CFG_UPDATE1, 1u << 24);
	n2vc_us(100);
	v = ni_rd(top, N2VC_CFG13);
	if ((v & (1u << 31)) || (((v >> 24) & 0xfu) != want)) {
		printf("NORD2 VCODEC: venc_sel mux 0xe0 did not take parent %u (reads %#x)\n", want, v);
		return EIO;
	}
	/* the phone log proves the clock without a second ioctl: 4 = 273 MHz (venc_step3), 12 = 624 MHz (venc_step0) */
	printf("NORD2 VCODEC: venc_sel %#x -> %#x (%s)\n", was, v,
	    want == N2VC_VENC_PARENT_SLOW ? "273 MHz" : want == N2VC_VENC_PARENT_FAST ? "624 MHz" : "parent");
	return 0;
}

static int
n2vc_gates(const struct n2vc_blk *b, bool on)
{
	vm_offset_t g = n2vc_pa(b->gate_pa, 0x1000, 1);
	if (!g) { return ENXIO; }
	if (on && b->post_write) { ni_wr(g, b->gates[0].set_off, b->post_write); }   /* clk-mt6885-pg.c:1178/1181 */
	for (unsigned i = 0; i < b->ngates; i++) {
		ni_wr(g, on ? b->gates[i].set_off : b->gates[i].clr_off, 1u << b->gates[i].bit);
	}
	for (unsigned i = 0; i < b->ngates; i++) {
		uint32_t want = on ? (1u << b->gates[i].bit) : 0;
		for (unsigned t = 0; t < 100; t++) {
			if ((ni_rd(g, b->gates[i].sta_off) & (1u << b->gates[i].bit)) == want) { break; }
			if (t == 99) {
				printf("NORD2 VCODEC: %s gate %#x bit %u did not switch %s\n", b->name, b->gate_pa, b->gates[i].bit, on ? "on" : "off");
				return EIO;
			}
			n2vc_us(100);
		}
	}
	return 0;
}

static int n2vc_hw_off(unsigned blk);

static int
n2vc_larb_on(const struct n2vc_larb *l, const char *name)
{
	vm_offset_t base = n2vc_pa(l->pa, 0x1000, 1);
	if (!base) { return ENXIO; }
	ni_wr(base, 0x24, 0x300256);                 /* smi_conf.h :87-90/:95-99 */
	ni_wr(base, 0x40, 1);
	if (l->int_conf) { ni_wr(base, 0x524, 0x300256); }
	for (unsigned p = 0; p < l->nports; p++) {
		if (!(l->ports & (1u << p))) { continue; }
		ni_wr(base, 0x200 + 4 * p, l->ostdl[p]);
		uint32_t was = ni_rd(base, 0x380 + 4 * p);
		if (was & 0x000f0000u & l->nsmask) {
			printf("NORD2 VCODEC: %s larb port %u was routed to MMSRAM (%#x): back to DRAM\n", name, p, was);
		}
		ni_mask(base, 0x380 + 4 * p, l->nsmask, 0); /* IOMMU off, address bits 32+ zero (pseudo_m4u_v2.c:563-575), no MMSRAM */
	}
	for (unsigned p = 0; p < l->nports; p++) {
		if (!(l->ports & (1u << p))) { continue; }
		if (ni_rd(base, 0x380 + 4 * p) & l->nsmask) {
			printf("NORD2 VCODEC: %s larb port %u still translates (%#x)\n", name, p, ni_rd(base, 0x380 + 4 * p));
			return EIO;
		}
	}
	return 0;
}

static int
n2vc_prot_set(vm_offset_t infra, uint32_t set, uint32_t ack, uint32_t bits, const char *name)
{
	if (!bits) { return 0; }
	if (!infra) { return ENXIO; }
	ni_wr(infra, set, bits);
	if (!n2vc_wait(infra, ack, bits, bits)) {
		printf("NORD2 VCODEC: %s bus protection %#x not acknowledged (%#x)\n", name, bits, ni_rd(infra, ack));
		return ETIMEDOUT;
	}
	return 0;
}

static int
n2vc_hw_on(unsigned blk, unsigned venc_parent)
{
	const struct n2vc_blk *b = &n2vc_blocks[blk];
	vm_offset_t spm = n2vc_pa(N2VC_SPM_PA, 0x1000, 1), infra = n2vc_pa(N2VC_INFRA_PA, 0x1000, 1);
	if (!spm || !infra) { return ENXIO; }
	/* the display domain (the SMI common's power) must be on before any video bus protection is released */
	if (!(ni_rd(spm, N2VC_PWR_STA) & N2VC_DIS_BIT) || !(ni_rd(spm, N2VC_PWR_STA2) & N2VC_DIS_BIT)) {
		printf("NORD2 VCODEC: %s not powered: the display domain is off\n", b->name);
		return EACCES;
	}
	/* larb8 (VENC core 1) and larb4 (VDEC core) hang off the MDP SMI common, not the display's: releasing their bus
	 * protection with MDP off is the camera bring-up's hang pattern (smi_drv.c:198-232, dts mdp_smi_common@1f002000). */
	if (b->mdp_smi && (!(ni_rd(spm, N2VC_PWR_STA) & N2VC_MDP_BIT) || !(ni_rd(spm, N2VC_PWR_STA2) & N2VC_MDP_BIT))) {
		printf("NORD2 VCODEC: %s not powered: the MDP domain (its SMI common) is off\n", b->name);
		return EACCES;
	}
	/* and the common's clock root must run: CLK_CFG_1 disp_sel (bit 7) / mdp_sel (bit 15) are gate bits, set = gated */
	vm_offset_t topc = n2vc_pa(N2VC_TOP_PA, 0x1000, 1);
	if (!topc) { return ENXIO; }
	uint32_t cfg1 = ni_rd(topc, N2VC_CFG1);
	if ((cfg1 & N2VC_DISP_GATE) || (b->mdp_smi && (cfg1 & N2VC_MDP_GATE))) {
		printf("NORD2 VCODEC: %s not powered: the %s clock is gated (CLK_CFG_1 %#x)\n", b->name,
		    (cfg1 & N2VC_DISP_GATE) ? "disp_sel" : "mdp_sel", cfg1);
		return EACCES;
	}
	int err;
	if (blk == RWVC_BLK_VENC || blk == RWVC_BLK_VENC_C1) {
		/* venc_sel is shared by both VENC cores and by the JPEG encoder, and its owner's wanted parent is passed in:
		 * vcodecd asks for N2VC_VENC_SLOW (root's 273 MHz first-encode decision), the JPEG driver's shared reference
		 * asks for 624 MHz (the value the bootloader leaves, M1). Program it only while both VENC domains are off:
		 * n2vc_hw_on runs at reference zero for the requesting block, but core 1's hidden VEN parent may be on. */
		bool ven_on = n2vc.refs[RWVC_BLK_VENC] || n2vc.refs[RWVC_BLK_VENC_C1] ||
		    n2vc_status_on(&n2vc_blocks[RWVC_BLK_VENC]) || n2vc_status_on(&n2vc_blocks[RWVC_BLK_VENC_C1]);
		uint32_t v = ni_rd(topc, N2VC_CFG13);
		err = 0;
		if (ven_on) {
			if ((v & (1u << 31)) || ((v >> 24) & 0xfu) == 0) {   /* a running block without a clock: driver conflict */
				printf("NORD2 VCODEC: %s: venc_sel has no clock while VEN is on (%#x)\n", b->name, v);
				err = EIO;
			}
		} else if (venc_parent != 0 || (v & (1u << 31)) || ((v >> 24) & 0xfu) == 0) {
			/* a wanted parent, or no clock at all (gated / 26 MHz): program it; otherwise keep what is running */
			err = n2vc_venc_mux(venc_parent != 0 ? venc_parent : N2VC_VENC_PARENT_FAST);
		}
	} else {
		err = n2vc_mux(N2VC_CFG14, 0, 7, 25, N2VC_VDEC_PARENT, "vdec_sel");
		if (!err) { err = n2vc_mux(N2VC_CFG14, 8, 15, 26, N2VC_VDEC_PARENT, "vdec_lat_sel"); }
	}
	if (err) { return err; }

	ni_wr(spm, 0x000, N2VC_KEY);
	uint32_t sta = 1u << b->sta;
	if (!n2vc_status_on(b)) {
		ni_mask(spm, b->con, (1u << 2) | (1u << 3), (1u << 2) | (1u << 3));
		if (!n2vc_wait(spm, N2VC_PWR_STA, sta, sta) || !n2vc_wait(spm, N2VC_PWR_STA2, sta, sta)) {
			ni_mask(spm, b->con, (1u << 2) | (1u << 3), 0);
			printf("NORD2 VCODEC: %s power status timeout (con %#x): request withdrawn\n", b->name, ni_rd(spm, b->con));
			return ETIMEDOUT;
		}
	}
	ni_mask(spm, b->con, 1u << 4, 0);            /* PWR_CLK_DIS off */
	ni_mask(spm, b->con, 1u << 1, 0);            /* PWR_ISO off */
	ni_mask(spm, b->con, 1u << 0, 1u << 0);      /* PWR_RST_B on */
	ni_mask(spm, b->con, 1u << 8, 0);            /* SRAM_PDN off */
	if (!n2vc_wait(spm, b->con, 1u << 12, 0)) {
		printf("NORD2 VCODEC: %s SRAM ack timeout (con %#x): bus protection left on\n", b->name, ni_rd(spm, b->con));
		return ETIMEDOUT;
	}
	ni_wr(infra, N2VC_MM_CLR, b->step2_mm);      /* release step 2, then step 1 (spm_mtcmos_ctrl_*, see VCODEC_HW.md §1.1) */
	ni_wr(infra, N2VC_MM2_CLR, b->step2_mm2);
	ni_wr(infra, N2VC_MM_CLR, b->step1_mm);
	ni_wr(infra, N2VC_MM2_CLR, b->step1_mm2);
	/* let the protection release settle; every delay() is <= 100 us */
	for (unsigned i = 0; i < 4 && !n2vc_usable(blk); i++) { n2vc_us(50); }
	if (!n2vc_usable(blk)) {
		printf("NORD2 VCODEC: %s still isolated/protected after power-on (spm %#x/%#x con %#x mm %#x mm2 %#x)\n", b->name,
		    ni_rd(spm, N2VC_PWR_STA), ni_rd(spm, N2VC_PWR_STA2), ni_rd(spm, b->con), ni_rd(infra, N2VC_MM_OFF), ni_rd(infra, N2VC_MM2_OFF));
		(void)n2vc_hw_off(blk);              /* do not leave a half-powered block behind */
		return EIO;
	}
	if ((err = n2vc_gates(b, true)) != 0) { (void)n2vc_hw_off(blk); return err; }
	if ((err = n2vc_larb_on(b->larb, b->name)) != 0) { (void)n2vc_hw_off(blk); return err; }
	n2vc.on[blk] = 1;
	return 0;
}

static int
n2vc_hw_off(unsigned blk)
{
	const struct n2vc_blk *b = &n2vc_blocks[blk];
	vm_offset_t spm = n2vc_pa(N2VC_SPM_PA, 0x1000, 1), infra = n2vc_pa(N2VC_INFRA_PA, 0x1000, 1);
	if (!spm || !infra) { return ENXIO; }
	n2vc.on[blk] = 0;
	if (!n2vc_status_on(b)) { return 0; }          /* already off */
	/* Gate the clocks first, but only if the domain is really usable: a protected/isolated block refuses register
	 * access (and its clocks are already off in the paths that leave it that way). */
	int err = 0;
	/* opus-decint (phone run vc19, q10je): VDE/VDE2's VDEC_CKEN/LAT_CKEN/LARB1_CKEN are never cleared by Linux: their
	 * clk ops are mtk_clk_gate_ops_setclr_inv_dummy (clk-mt6885-vde.c:76-123/184-231, clk-gate.c:310-314: disable is a
	 * no-op), so spm_mtcmos_ctrl_vde2() sets the bus protection with the core/larb4 clocks RUNNING.  Gating them first
	 * left MM_2 bit 6 unacknowledged ("VDEC_CORE bus protection 0x40 not acknowledged (0x103)", power-off ETIMEDOUT
	 * after every decoded frame).  The VENC gates are real gates (VENC_CG) and stay gated first, as before. */
	bool vdec = blk == RWVC_BLK_VDEC_SOC || blk == RWVC_BLK_VDEC_CORE;
	if (!vdec && n2vc_usable(blk)) { err = n2vc_gates(b, false); }
	if (err) { return err; }
	ni_wr(spm, 0x000, N2VC_KEY);
	if ((err = n2vc_prot_set(infra, N2VC_MM_SET, N2VC_MM_STA, b->step1_mm, b->name)) != 0) { return err; }
	if ((err = n2vc_prot_set(infra, N2VC_MM2_SET, N2VC_MM2_STA, b->step1_mm2, b->name)) != 0) { return err; }
	if ((err = n2vc_prot_set(infra, N2VC_MM_SET, N2VC_MM_STA, b->step2_mm, b->name)) != 0) { return err; }
	if ((err = n2vc_prot_set(infra, N2VC_MM2_SET, N2VC_MM2_STA, b->step2_mm2, b->name)) != 0) { return err; }
	ni_mask(spm, b->con, 1u << 8, 1u << 8);
	if (!n2vc_wait(spm, b->con, 1u << 12, 1u << 12)) {
		printf("NORD2 VCODEC: %s off: SRAM ack timeout (con %#x): domain left powered\n", b->name, ni_rd(spm, b->con));
		return EIO;
	}
	ni_mask(spm, b->con, 1u << 1, 1u << 1);
	ni_mask(spm, b->con, 1u << 4, 1u << 4);
	ni_mask(spm, b->con, 1u << 0, 0);
	ni_mask(spm, b->con, 1u << 2, 0);
	ni_mask(spm, b->con, 1u << 3, 0);
	if (!n2vc_wait(spm, N2VC_PWR_STA, 1u << b->sta, 0) || !n2vc_wait(spm, N2VC_PWR_STA2, 1u << b->sta, 0)) {
		printf("NORD2 VCODEC: %s off: power status did not drop (con %#x)\n", b->name, ni_rd(spm, b->con));
		return ETIMEDOUT;
	}
	return 0;
}

/* ---- references (per open file, per child block, and n2vc_ven_get) ---------------------------------------------- */
static int n2vc_acquire_ex(unsigned blk, struct n2vc_open *f, bool hidden, unsigned venc_parent);
static int n2vc_release_locked(unsigned blk, struct n2vc_open *f, bool hidden);

/* venc_parent: the parent to select for venc_sel when VEN is off, 0 = keep the running parent (never re-mux).
 * The 3-argument name is what hw-kern's autotest (kern/p031x/.../nord2_vcodec_at.inc) calls; its step 1 sets the mux
 * itself for the --slow/--fast knob, so the driver must not undo that -> parent 0. Kept as a macro so that file needs no
 * edit and no unused-function warning appears in a build without the autotest (IK's 0310-only compile check). */
#define n2vc_acquire_locked(blk, f, hidden) n2vc_acquire_ex((blk), (f), (hidden), 0u)

static int
n2vc_acquire_ex(unsigned blk, struct n2vc_open *f, bool hidden, unsigned venc_parent)
{
	const struct n2vc_blk *b = &n2vc_blocks[blk];
	int err = 0;
	if (!hidden && f && f->refs[blk] == 0xffffu) { return EOVERFLOW; }
	if (n2vc.refs[blk] == 0) {
		if (b->dep != N2VC_NONE && !n2vc.dep_ref[blk]) {
			if ((err = n2vc_acquire_ex(b->dep, NULL, true, venc_parent)) != 0) { return err; }
			n2vc.dep_ref[blk] = 1;
		}
		if ((err = n2vc_hw_on(blk, venc_parent)) != 0) {
			if (b->dep != N2VC_NONE && n2vc.dep_ref[blk]) { (void)n2vc_release_locked(b->dep, NULL, true); n2vc.dep_ref[blk] = 0; }
			return err;
		}
	}
	n2vc.refs[blk]++;
	if (hidden) { n2vc.hidden[blk]++; }
	else if (f) { f->refs[blk]++; }
	return 0;
}

static int
n2vc_release_locked(unsigned blk, struct n2vc_open *f, bool hidden)
{
	const struct n2vc_blk *b = &n2vc_blocks[blk];
	if (n2vc.refs[blk] == 0) {
		printf("NORD2 VCODEC: %s reference underflow\n", b->name);
		return EINVAL;
	}
	n2vc.refs[blk]--;
	if (hidden) { if (n2vc.hidden[blk]) { n2vc.hidden[blk]--; } }
	else if (f && f->refs[blk]) { f->refs[blk]--; }
	if (n2vc.refs[blk] != 0) { return 0; }
	int err = n2vc_hw_off(blk);
	if (err) {
		/* the block is marked unusable; keep the parent powered rather than pull a live bus parent out from under it */
		printf("NORD2 VCODEC: %s power-off failed (%d): its parent reference is kept\n", b->name, err);
		return err;
	}
	if (b->dep != N2VC_NONE && n2vc.dep_ref[blk]) { n2vc.dep_ref[blk] = 0; (void)n2vc_release_locked(b->dep, NULL, true); }
	return 0;
}

int
n2vc_ven_get(void)
{
	n2vc_lock();
	/* K01's JPEG driver: its own clock, 624 MHz, the parent the phone boots with (M1). Only programmed while VEN is
	 * off (see n2vc_hw_on); a running domain is never re-muxed. */
	int err = n2vc_acquire_ex(RWVC_BLK_VENC, NULL, true, N2VC_VENC_PARENT_FAST);
	n2vc_unlock();
	return err;
}

void
n2vc_ven_put(void)
{
	n2vc_lock();
	(void)n2vc_release_locked(RWVC_BLK_VENC, NULL, true);
	n2vc_unlock();
}

/* ---- DMA pool -------------------------------------------------------------------------------------------------- */
static bool
n2vc_pg_free(uint32_t p)
{
	return !(n2vc.pool_bits[p >> 5] & (1u << (p & 31u)));
}

static void
n2vc_pg_mark(uint32_t p, bool used)
{
	if (used) { n2vc.pool_bits[p >> 5] |= 1u << (p & 31u); }
	else { n2vc.pool_bits[p >> 5] &= ~(1u << (p & 31u)); }
}

static uint32_t
n2vc_pool_free_pages(void)
{
	uint32_t n = 0;
	for (uint32_t w = 0; w < N2VC_POOL_WORDS; w++) { n += 32u - (uint32_t)__builtin_popcount(n2vc.pool_bits[w]); }
	return n;
}

static uint32_t
n2vc_pool_take(uint32_t pages)
{
	uint32_t run = 0, first = 0;
	if (pages == 0 || pages > N2VC_POOL_PAGES) { return 0xffffffffu; }
	for (uint32_t p = 0; p < N2VC_POOL_PAGES; p++) {
		if (n2vc_pg_free(p)) {
			if (!run) { first = p; }
			if (++run == pages) {
				for (uint32_t k = 0; k < pages; k++) { n2vc_pg_mark(first + k, true); }
				return first;
			}
		} else { run = 0; }
	}
	return 0xffffffffu;
}

/* The pool access rule (frozen header): the allocator may always use its handle; another file only when the handle was
 * allocated RWVC_DMA_SHARED and the caller is on the OTHER minor.  Two clients on minor 1 never see each other.  This is
 * IK's n2vr_dma_allowed() in the kernel build (per-process identity) and the same rule in the host model. */
static bool
n2vc_dma_usable(struct n2vc_open *f, const struct n2vc_dma *d)
{
	if (d->owner == f) { return true; }
	if (!(d->flags & RWVC_DMA_SHARED)) { return false; }
#if N2VC_KERNEL
	return n2vr_dma_allowed(d->owner->minor, d->owner->proc, d->flags, f->minor, f->proc);
#else
	return d->owner->minor != f->minor;
#endif
}

static struct n2vc_dma *
n2vc_dma_lookup(struct n2vc_open *f, uint32_t handle, int *err)
{
	uint32_t idx = (handle & 0xfffu);
	uint32_t gen = handle >> 12;
	if (idx == 0 || idx > N2VC_DMA_MAX) { *err = EINVAL; return NULL; }
	struct n2vc_dma *d = &n2vc.dma[idx - 1];
	if (!d->used || d->gen != (uint16_t)gen) { *err = EINVAL; return NULL; }
	if (!f) { *err = EINVAL; return NULL; }
	if (!n2vc_dma_usable(f, d)) { *err = EACCES; return NULL; }
	*err = 0;
	return d;
}

/* RWVC_DMA_INFO: the same rule, with the header's own errors: EINVAL handle 0, ENOENT unknown/stale, EPERM not usable. */
static int
n2vc_dma_info_locked(struct n2vc_open *f, struct rwvc_dma_info *di)
{
	if (di->handle == 0) { return EINVAL; }
	uint32_t idx = di->handle & 0xfffu;
	uint32_t gen = di->handle >> 12;
	if (idx == 0 || idx > N2VC_DMA_MAX) { return ENOENT; }
	struct n2vc_dma *d = &n2vc.dma[idx - 1];
	if (!d->used || d->gen != (uint16_t)gen) { return ENOENT; }
	if (!n2vc_dma_usable(f, d)) { return EPERM; }
	di->bytes = d->bytes;
	di->out_pa = RW_CLAIM_VCODEC_PA + d->page * 4096u;
	return 0;
}

static void
n2vc_dma_free_all(struct n2vc_open *f)
{
	for (uint32_t i = 0; i < N2VC_DMA_MAX; i++) {
		struct n2vc_dma *d = &n2vc.dma[i];
		if (!d->used || d->owner != f) { continue; }
		for (uint32_t k = 0; k < d->npages; k++) { n2vc_pg_mark(d->page + k, false); }
		d->used = 0; d->owner = NULL; d->gen = (uint16_t)(d->gen + 1u); d->flags = 0; d->page = d->npages = d->bytes = 0;
	}
	f->ndma = 0;
}

/* ---- open/close ------------------------------------------------------------------------------------------------ */
struct n2vc_open *
n2vc_open_create(unsigned minor)
{
	struct n2vc_open *f = NULL;
	if (minor > 1) { return NULL; }
	n2vc_lock();
	for (unsigned i = 0; i < N2VC_MAX_FILES; i++) {
		if (!n2vc.files[i].used) {
			f = &n2vc.files[i];
			memset(f, 0, sizeof(*f));
			f->used = 1;
			f->minor = (uint8_t)minor;
			if (++n2vc.next_file_id == 0) { n2vc.next_file_id = 1; }
			f->id = n2vc.next_file_id;
			break;
		}
	}
	n2vc_unlock();
	return f;
}

void
n2vc_open_destroy(struct n2vc_open *f)
{
	if (!f) { return; }
	n2vc_lock();
	n2vc_dma_free_all(f);
	for (unsigned b = 0; b < RWVC_BLK_COUNT; b++) {
		while (f->refs[b] > 0) { (void)n2vc_release_locked(b, f, false); }
	}
	for (unsigned i = 0; i < RWVC_IRQ_COUNT; i++) { f->irq_seen[i] = 0; }
	f->used = 0;
	n2vc_unlock();
}

unsigned
n2vc_minor_opens(unsigned minor)
{
	unsigned n = 0;
	n2vc_lock();
	for (unsigned i = 0; i < N2VC_MAX_FILES; i++) { if (n2vc.files[i].used && n2vc.files[i].minor == minor) { n++; } }
	n2vc_unlock();
	return n;
}

uint32_t
n2vc_powered_mask(void)
{
	uint32_t m = 0;
	n2vc_lock();
	for (unsigned b = 0; b < RWVC_BLK_COUNT; b++) { if (n2vc.on[b]) { m |= 1u << b; } }
	n2vc_unlock();
	return m;
}

/* ---- ioctls ---------------------------------------------------------------------------------------------------- */
/*
 * RWVC_WIN_VDEC_ALL (ABI rev 3, DEC07): one 0x30000 window over the vendor decoder's mapping of 0x16000000, with the
 * power block decided per 4 KiB page.  48 pages, from facts/VDEC_PROTOCOL.md section 4 and the sources it cites
 * (mt6893.dts:6281-6292, mtk_vcodec_dec_pm.c:154-288, clk-mt6885-vde.c):
 *
 *   0x00-0x0c  VDEC_CORE  VDEC_BASE 0x16000000 (+0x800 UFO), the vendor's 0x16001000/0x16002000/0x16003800,
 *                         VDEC_RACING_CTRL 0x16004000.  K02's separate VDEC_BASE/VDEC_RACING windows already require
 *                         VDEC_CORE for these registers.
 *   0x0d       VDEC_SOC   larb5 0x1600d000: kernel-owned, read-only (allow-list below).
 *   0x0e       VDEC_CORE  larb6 0x1600e000 (mt6893.dts:6051, zero ports, never programmed here, so writable).
 *   0x0f-0x1f  VDEC_SOC   VDEC_SOC_GCON 0x1600f000, VDEC_LAT_MISC 0x16010000, VDEC_LAT_VLD 0x16011000 and the vendor's
 *                         LAT sub-blocks 0x16011800-0x16018800 (mt6893.dts:6283-6292; clk-mt6885-vde.c:214-232).
 *   0x20-0x2d  VDEC_CORE  VDEC_VLD 0x16020000, VDEC_MC 0x16021000, AVC_VLD 0x16022000, VDEC_MV 0x16023000, PP
 *                         0x16024000, VDEC_MISC 0x16025000-0x16028fff, 0x16029000 (mt6893.dts:6283-6292).
 *   0x2e       VDEC_CORE  larb4 0x1602e000: kernel-owned, read-only (allow-list below).
 *   0x2f       VDEC_CORE  VDEC_SYS 0x1602f000.
 *
 * The core's clock-on path enables MT_CG_SOC (the VDE gate block) and MT_CG_VDEC0 (mtk_vcodec_dec_pm.c:160-170), which
 * n2vc_blocks[] expresses as VDEC_CORE's hidden reference on VDEC_SOC: a core page needs both domains, so mapping the
 * base/racing/unknown low pages to the core cannot widen anything the separate windows allow today.
 */
#define N2VC_VDEC_PAGES (0x30000u / 4096u)
static uint8_t
n2vc_vdec_page_blk(uint32_t page)
{
	if (page < 0x0du) { return RWVC_BLK_VDEC_CORE; }
	if (page == 0x0du) { return RWVC_BLK_VDEC_SOC; }     /* larb5 */
	if (page == 0x0eu) { return RWVC_BLK_VDEC_CORE; }    /* larb6 */
	if (page < 0x20u) { return RWVC_BLK_VDEC_SOC; }      /* SOC_GCON + LAT_* */
	return RWVC_BLK_VDEC_CORE;                           /* VLD .. VDEC_SYS, larb4 */
}

/*
 * Writes to the two kernel-owned larb pages are refused unless the exact word is listed here.  n2vc_larb_on() programs
 * larb4/larb5 itself, so a client may only change the bits the vendor's sleep-protect handshake needs:
 *   - larb5 0x1600d00c (page 0x0d, offset 0x00c) is SMI_LARB_SLP_CON; opus-dec1's host frame trace has the vendor
 *     writing bit 0 there and polling bit 16 (vdec_hal_wait_larb_sleep_protect / release_larb_sleep_protect), so a flat
 *     read-only rule would break the first frame.  Bit 0 only; `wmask` refuses a write that touches any other bit.
 *   - larb4 0x1602e00c (page 0x2e, offset 0x00c) is the same SMI_LARB_SLP_CON word on the core's larb; DEC04's
 *     reading of opus-dec2's trace has the core path writing it too (board 1794414983), so it gets the same bit 0.
 * An unknown offset is refused, never silently allowed.
 */
struct n2vc_vdec_allow { uint8_t page; uint32_t off, wmask; };
static const struct n2vc_vdec_allow n2vc_vdec_allow[] = {
	{ 0x0du, 0x00cu, 0x00000001u },     /* larb5 SMI_LARB_SLP_CON bit 0 (sleep protect, LAT path) */
	{ 0x2eu, 0x00cu, 0x00000001u },     /* larb4 SMI_LARB_SLP_CON bit 0 (core path, DEC04/opus-dec2) */
};
static const unsigned n2vc_vdec_allow_n = sizeof(n2vc_vdec_allow) / sizeof(n2vc_vdec_allow[0]);

/* may this write change exactly these bits?  `off` is the offset inside the 4 KiB page, `bits` is what the caller asks
 * to modify (value, or value&mask for a masked write); an offset that is not in the table is refused. */
static bool
n2vc_vdec_larb_write_allowed(uint32_t page, uint32_t off, uint32_t bits)
{
	for (unsigned i = 0; i < n2vc_vdec_allow_n; i++) {
		const struct n2vc_vdec_allow *a = &n2vc_vdec_allow[i];
		if (a->page == page && a->off == off) { return (bits & ~a->wmask) == 0; }
	}
	return false;
}

/* the block that must be powered for this op, and the read-only refusal of RWVC_WIN_VDEC_ALL's larb pages */
static int
n2vc_op_block(const struct rwvc_reg_op *o, unsigned *blk)
{
	const struct n2vc_win *w = &n2vc_wins[o->window];
	if (w->blk != N2VC_BLK_PER_PAGE) { *blk = w->blk; return 0; }
	uint32_t page = o->offset >> 12;
	if (page >= N2VC_VDEC_PAGES) { return EINVAL; }      /* not reached: the window is 0x30000 bytes and 4-aligned */
	*blk = n2vc_vdec_page_blk(page);
	if ((o->op == RWVC_OP_WRITE || o->op == RWVC_OP_WRITE_MASK) && (page == 0x0du || page == 0x2eu)) {
		uint32_t bits = o->op == RWVC_OP_WRITE_MASK ? (o->value & o->mask) : o->value;
		if (!n2vc_vdec_larb_write_allowed(page, o->offset & 0xfffu, bits)) { return EACCES; }
	}
	return 0;
}

static int
n2vc_op_window(const struct rwvc_reg_op *o, unsigned *win, vm_offset_t *base, uint32_t *off)
{
	if (o->window >= N2VC_WIN_N) { return EINVAL; }
	if (o->offset & 3u) { return EINVAL; }
	if (o->offset > n2vc_wins[o->window].bytes - 4u) { return EINVAL; }
	unsigned blk = 0;
	int err = n2vc_op_block(o, &blk);
	if (err) { return err; }
	if (!n2vc.on[blk]) { return EACCES; }
	vm_offset_t va = n2vc_win(o->window);
	if (!va) { return ENXIO; }
	*win = o->window; *base = va; *off = o->offset;
	return 0;
}

static int
n2vc_regs_locked(struct n2vc_open *f, struct rwvc_reg_batch *b)
{
	struct rwvc_reg_op ops[N2VC_CHUNK];
	uint64_t batch0 = n2vc_now_us();
	uint32_t fid = f->id;
	if (b->count > RWVC_REG_MAX_OPS) { return EINVAL; }
	if (b->count && !b->ops) { return EINVAL; }
	uint32_t timeout = b->poll_timeout_us;
	if (timeout > N2VC_POLL_MAX_US) { timeout = N2VC_POLL_MAX_US; }
	uint32_t done = 0;
	for (uint32_t start = 0; start < b->count; start += N2VC_CHUNK) {
		uint32_t n = b->count - start;
		if (n > N2VC_CHUNK) { n = N2VC_CHUNK; }
		uint64_t uaddr = b->ops + (uint64_t)start * sizeof(struct rwvc_reg_op);
		int err = n2vc_copyin(uaddr, ops, n * (uint32_t)sizeof(struct rwvc_reg_op));
		if (err) { b->out_done = done; return err; }
		for (uint32_t i = 0; i < n; i++) {
			struct rwvc_reg_op *o = &ops[i];
			vm_offset_t base = 0;
			uint32_t off = 0;
			unsigned win = 0;
			switch (o->op) {
			case RWVC_OP_READ:
			case RWVC_OP_WRITE:
			case RWVC_OP_WRITE_MASK:
			case RWVC_OP_POLL: {
				err = n2vc_op_window(o, &win, &base, &off);
				if (err) { (void)n2vc_copyout(ops, uaddr, n * (uint32_t)sizeof(struct rwvc_reg_op)); b->out_done = done; return err; }
				(void)win;
				if (o->op == RWVC_OP_READ) { o->value = ni_rd(base, off); }
				else if (o->op == RWVC_OP_WRITE) { ni_wr(base, off, o->value); }
				else if (o->op == RWVC_OP_WRITE_MASK) { ni_mask(base, off, o->mask, o->value); }
				else {
					uint64_t start_us = n2vc_now_us();
					for (;;) {
						uint32_t v = ni_rd(base, off);
						if ((v & o->mask) == (o->value & o->mask)) { break; }
						uint64_t now = n2vc_now_us();
						if (now - start_us >= timeout || now - batch0 >= N2VC_BATCH_US) {
							(void)n2vc_copyout(ops, uaddr, n * (uint32_t)sizeof(struct rwvc_reg_op));
							b->out_done = done;
							return ETIMEDOUT;
						}
						n2vc_wait_step(now - start_us);   /* drops n2vc_mtx; revalidate before the next register access */
						if (!f->used || f->id != fid) {
							(void)n2vc_copyout(ops, uaddr, n * (uint32_t)sizeof(struct rwvc_reg_op));
							b->out_done = done;
							return ENXIO;
						}
						unsigned rblk = 0;
						if (n2vc_op_block(o, &rblk) != 0 || !n2vc.on[rblk]) {
							(void)n2vc_copyout(ops, uaddr, n * (uint32_t)sizeof(struct rwvc_reg_op));
							b->out_done = done;
							return EACCES;
						}
					}
				}
				break;
			}
			case RWVC_OP_DELAY_US:
				if (o->value > 1000u) { b->out_done = done; return EINVAL; }
				if (n2vc_now_us() - batch0 + o->value > N2VC_BATCH_US) { b->out_done = done; return ETIMEDOUT; }
				for (uint32_t left = o->value; left != 0; ) {
					uint32_t d = left > 100u ? 100u : left;   /* delay() only for waits <= 100 us */
					n2vc_us(d);
					left -= d;
				}
				break;
			default:
				b->out_done = done;
				return EINVAL;
			}
			done++;
		}
		err = n2vc_copyout(ops, uaddr, n * (uint32_t)sizeof(struct rwvc_reg_op));
		if (err) { b->out_done = done; return err; }
	}
	b->out_done = done;
	return 0;
}

static uint32_t
n2vc_irq_status(unsigned irq)
{
	const struct n2vc_irq *q = &n2vc_irqs[irq];
	vm_offset_t va = n2vc_win(q->win);
	return va ? ni_rd(va, q->status_off) : 0;
}

static bool
n2vc_irq_match(unsigned irq, uint32_t st)
{
	return (st & n2vc_irqs[irq].match) != 0;
}

static void
n2vc_irq_ack(unsigned irq, uint32_t st)
{
	const struct n2vc_irq *q = &n2vc_irqs[irq];
	vm_offset_t va = n2vc_pa(n2vc_wins[q->win].pa, n2vc_wins[q->win].bytes, 1);
	if (!va) { return; }
	if (irq == RWVC_IRQ_VDEC || irq == RWVC_IRQ_VDEC_LAT) {
		uint32_t v = ni_rd(va, q->status_off);
		ni_wr(va, q->status_off, v | 0x11u);
		ni_wr(va, q->status_off, (v | 0x11u) & ~0x10u);
	} else {
		ni_wr(va, q->ack_off, st & q->clear_mask);
	}
}

/*
 * GIC evidence for the future interrupt path, read only.  SPI 308 (VENC) and 313 (core 1) are INTID 340 / 345.  This
 * driver never enables them (ml_install_interrupt_handler keeps ONE handler per CPU and Nord2GPU.cpp owns it), but a level
 * line that is asserted shows in GICD_ISPENDR even while disabled.  -1 = GICD not mapped (host build or map failure).
 */
#define N2VC_INTID_VENC    (308u + 32u)
#define N2VC_INTID_VENC_C1 (313u + 32u)
static int
n2vc_gic_pending(uint32_t intid)
{
#if N2VC_KERNEL
	vm_offset_t gicd = n2vc_pa(N2VC_GICD_PA, 0x1000, 1);
	if (!gicd) { return -1; }
	return (int)((ni_rd(gicd, 0x200u + 4u * (intid / 32u)) >> (intid % 32u)) & 1u);
#else
	(void)intid;
	return -1;
#endif
}
static bool n2vc_gic_logged[2];

static int
n2vc_wait_irq_locked(struct n2vc_open *f, struct rwvc_wait_irq *w)
{
	if (w->irq >= RWVC_IRQ_COUNT) { return EINVAL; }
	if (w->timeout_ms > N2VC_WAIT_MS_MAX) { return EINVAL; }
	unsigned blk = n2vc_irqs[w->irq].blk;
	uint32_t fid = f->id;
	if (!f->used || f->id != fid) { return ENXIO; }
	if (!n2vc.on[blk]) { return EACCES; }
	uint64_t start = n2vc_now_us();
	uint64_t limit = (uint64_t)w->timeout_ms * 1000u;
	uint32_t st = 0;
	for (;;) {
		st = n2vc_irq_status(w->irq);
		if (n2vc_irq_match(w->irq, st)) {
			if (w->irq <= RWVC_IRQ_VENC_C1 && !n2vc_gic_logged[w->irq]) {
				n2vc_gic_logged[w->irq] = true;          /* once per boot and line: is the dts SPI the one that asserts? */
				printf("NORD2 VCODEC: first %s done status %#x; GIC INTID %u pending %d (before the ack)\n",
				    w->irq == RWVC_IRQ_VENC ? "VENC" : "VENC_C1", st,
				    w->irq == RWVC_IRQ_VENC ? N2VC_INTID_VENC : N2VC_INTID_VENC_C1,
				    n2vc_gic_pending(w->irq == RWVC_IRQ_VENC ? N2VC_INTID_VENC : N2VC_INTID_VENC_C1));
			}
			n2vc_irq_ack(w->irq, st);
			f->irq_seen[w->irq]++;
			w->out_count = f->irq_seen[w->irq];
			f->irq_seen[w->irq] = 0;
			w->out_status = st;
			return 0;
		}
		uint64_t waited = n2vc_now_us() - start;
		if (waited >= limit) { w->out_count = 0; w->out_status = st; return ETIMEDOUT; }
		n2vc_wait_step(waited);                   /* drops n2vc_mtx: the file may close or the block may power off meanwhile */
		if (!f->used || f->id != fid) { w->out_count = 0; w->out_status = st; return ENXIO; }
		if (!n2vc.on[blk]) { w->out_count = 0; w->out_status = st; return EACCES; }
	}
}

/* the hardware half of struct rwvc_info; the relay's minor-1 INFO fills the same fields */
static void
n2vc_info_fill_locked(struct rwvc_info *i)
{
	if (!i) { return; }
	memset(i, 0, sizeof(*i));
	i->abi_version = RWVC_ABI_VERSION;
	for (unsigned b = 0; b < RWVC_BLK_COUNT; b++) { if (n2vc.on[b]) { i->powered_mask |= 1u << b; } }
	i->pool_pa = RW_CLAIM_VCODEC_PA;
	i->pool_bytes = RW_CLAIM_VCODEC_BYTES;
	i->pool_free = n2vc_pool_free_pages() * 4096u;
	i->irq_mode = 0;
#if !N2VC_KERNEL
	for (unsigned k = 0; k < N2VC_MAX_FILES; k++) { if (n2vc.files[k].used && n2vc.files[k].minor == 0) { i->server_present = 1; } }
#endif
	/* kernel: the caller sets server_present from n2vr_server_present() outside this lock (no nested locks) */
}

void
n2vc_info_fill(struct rwvc_info *i)
{
	if (!i) { return; }
	n2vc_lock();
	n2vc_info_fill_locked(i);
	n2vc_unlock();
}

static int
n2vc_ioctl_locked(struct n2vc_open *f, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	(void)flag; (void)p;
	if (!data) { return EINVAL; }
	if (cmd == RWVC_INFO) {
		n2vc_info_fill_locked((struct rwvc_info *)data);
		return 0;
	}
	if (f->minor != 0) {
		if (cmd != RWVC_DMA_ALLOC && cmd != RWVC_DMA_FREE && cmd != RWVC_DMA_RW &&
		    cmd != RWVC_DMA_INFO && cmd != RWVC_MSG_SEND && cmd != RWVC_MSG_RECV) { return EACCES; }
	}
	switch (cmd) {
	case RWVC_POWER: {
		struct rwvc_power *p = (struct rwvc_power *)data;
		if (p->block >= RWVC_BLK_COUNT) { return EINVAL; }
		/* vcodecd: N2VC_VENC_SLOW's 273 MHz parent for the first phone encode (root, 11 Nov), 624 MHz while
		 * kern.n2vc.venc_fast = 1 (0312) */
		if (p->on) { return n2vc_acquire_ex(p->block, f, false, n2vc_vcodecd_venc_parent()); }
		if (!f->refs[p->block]) { return EINVAL; }
		return n2vc_release_locked(p->block, f, false);
	}
	case RWVC_REGS:
		return n2vc_regs_locked(f, (struct rwvc_reg_batch *)data);
	case RWVC_WAIT_IRQ:
		return n2vc_wait_irq_locked(f, (struct rwvc_wait_irq *)data);
	case RWVC_IRQ_CLEAR: {
		struct rwvc_irq_clear *c = (struct rwvc_irq_clear *)data;
		if (c->irq >= RWVC_IRQ_COUNT) { return EINVAL; }
		if (n2vc.on[n2vc_irqs[c->irq].blk]) { n2vc_irq_ack(c->irq, n2vc_irq_status(c->irq)); }
		f->irq_seen[c->irq] = 0;
		return 0;
	}
	case RWVC_DMA_ALLOC: {
		struct rwvc_dma_alloc *a = (struct rwvc_dma_alloc *)data;
		if (!a->bytes || (a->flags & ~RWVC_DMA_SHARED)) { return EINVAL; }
		if (f->ndma >= N2VC_DMA_PER_FILE) { return ENOMEM; }
		uint32_t pages = (a->bytes + 4095u) / 4096u;
		unsigned slot = N2VC_DMA_MAX;
		for (unsigned i = 0; i < N2VC_DMA_MAX; i++) { if (!n2vc.dma[i].used) { slot = i; break; } }
		if (slot == N2VC_DMA_MAX) { return ENOMEM; }
		uint32_t page = n2vc_pool_take(pages);
		if (page == 0xffffffffu) { return ENOMEM; }
		vm_offset_t va = n2vc_pa(RW_CLAIM_VCODEC_PA, RW_CLAIM_VCODEC_BYTES, 0);
		if (!va) {
			for (uint32_t k = 0; k < pages; k++) { n2vc_pg_mark(page + k, false); }
			return ENXIO;
		}
		struct n2vc_dma *d = &n2vc.dma[slot];
		d->used = 1; d->flags = (uint8_t)a->flags; d->gen = (uint16_t)(d->gen + 1u);
		d->page = page; d->npages = pages; d->bytes = pages * 4096u; d->owner = f;
		memset((void *)(va + page * 4096u), 0, pages * 4096u);
		n2vc_cache(va + page * 4096u, pages * 4096u);
		a->out_handle = (uint32_t)((uint32_t)d->gen << 12) | (slot + 1u);
		a->out_pa = RW_CLAIM_VCODEC_PA + page * 4096u;
		f->ndma++;
		return 0;
	}
	case RWVC_DMA_FREE: {
		struct rwvc_dma_free *fr = (struct rwvc_dma_free *)data;
		int err = 0;
		struct n2vc_dma *d = n2vc_dma_lookup(f, fr->handle, &err);
		if (!d) { return err; }
		if (d->owner != f) { return EACCES; }
		for (uint32_t k = 0; k < d->npages; k++) { n2vc_pg_mark(d->page + k, false); }
		d->used = 0; d->owner = NULL; d->gen = (uint16_t)(d->gen + 1u); d->flags = 0; d->page = d->npages = d->bytes = 0;
		if (f->ndma) { f->ndma--; }
		return 0;
	}
	case RWVC_DMA_RW: {
		struct rwvc_dma_rw *rw = (struct rwvc_dma_rw *)data;
		int err = 0;
		struct n2vc_dma *d = n2vc_dma_lookup(f, rw->handle, &err);
		if (!d) { return err; }
		if (rw->offset > d->bytes || rw->bytes > d->bytes - rw->offset) { return EINVAL; }
		if (!rw->bytes) { return 0; }
		if (!rw->user) { return EFAULT; }
		vm_offset_t va = n2vc_pa(RW_CLAIM_VCODEC_PA, RW_CLAIM_VCODEC_BYTES, 0);
		if (!va) { return ENXIO; }
		va += d->page * 4096u + rw->offset;
		/* the mutex is held throughout, so the handle cannot go away: chunk only to bound how long a copy keeps the
		 * relay/JPEG callers out.  Cache maintenance is per chunk, after writes and before reads. */
		uint64_t user = rw->user;
		uint32_t left = rw->bytes;
		while (left) {
			uint32_t n = left > N2VC_RW_CHUNK ? N2VC_RW_CHUNK : left;
			if (rw->write) {
				err = n2vc_copyin(user, (void *)va, n);
				if (!err) { n2vc_cache(va, n); }
			} else {
				n2vc_cache(va, n);
				err = n2vc_copyout((void *)va, user, n);
			}
			if (err) { return err; }
			va += n; user += n; left -= n;
		}
		return 0;
	}
	case RWVC_DMA_FILL: {
		struct rwvc_dma_fill *fl = (struct rwvc_dma_fill *)data;
		int err = 0;
		struct n2vc_dma *d = n2vc_dma_lookup(f, fl->handle, &err);
		if (!d) { return err; }
		if (fl->offset > d->bytes || fl->bytes > d->bytes - fl->offset) { return EINVAL; }
		vm_offset_t va = n2vc_pa(RW_CLAIM_VCODEC_PA, RW_CLAIM_VCODEC_BYTES, 0);
		if (!va) { return ENXIO; }
		va += d->page * 4096u + fl->offset;
		memset((void *)va, (int)(fl->value & 0xffu), fl->bytes);
		n2vc_cache(va, fl->bytes);
		return 0;
	}
	case RWVC_DMA_INFO:
		return n2vc_dma_info_locked(f, (struct rwvc_dma_info *)data);
	default:
		return ENOTTY;
	}
}

int
n2vc_ioctl(struct n2vc_open *f, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	(void)flag; (void)p;
	if (!f) { return ENXIO; }
	n2vc_lock();
	int err = f->used ? n2vc_ioctl_locked(f, cmd, data, flag, p) : ENXIO;
	n2vc_unlock();
#if N2VC_KERNEL
	if (!err && cmd == RWVC_INFO && data) { ((struct rwvc_info *)data)->server_present = n2vr_server_present(); }
#endif
	return err;
}

/* ---- open slots (character device; also compiled for the host test with N2VC_HOST_SLOTS) ----------------------- */
#if N2VC_KERNEL || defined(N2VC_HOST_SLOTS)
static struct { struct n2vc_open *f; uint8_t minor, dead; uint32_t busy; } n2vc_slot[N2VC_MAX_SLOTS];

/* allocate a slot for an open file; caller holds n2vc_lock */
static int
n2vc_slot_add(struct n2vc_open *f, unsigned mn, struct proc *p)
{
	for (unsigned i = 0; i < N2VC_MAX_SLOTS; i++) {
		if (n2vc_slot[i].f) { continue; }
		n2vc_slot[i].f = f; n2vc_slot[i].minor = (uint8_t)mn; n2vc_slot[i].dead = 0; n2vc_slot[i].busy = 0;
		f->proc = p;
		return 0;
	}
	return EBUSY;
}

/* Remove every slot of this minor (XNU calls d_close once for the last close but d_open per open): non-busy files are
 * returned in out[] for the caller to destroy, a busy one is marked dead and n2vc_dev_ioctl destroys it when its ioctl
 * returns.  *found counts the slots seen (one n2vr_server_close() is owed per minor-0 slot).  Caller holds n2vc_lock. */
static unsigned
n2vc_slot_close_all(unsigned mn, struct n2vc_open **out, unsigned cap, unsigned *found)
{
	unsigned n = 0;
	*found = 0;
	for (unsigned i = 0; i < N2VC_MAX_SLOTS; i++) {
		if (!n2vc_slot[i].f || n2vc_slot[i].minor != mn) { continue; }
		(*found)++;
		if (n2vc_slot[i].busy) { n2vc_slot[i].dead = 1; }
		else {
			if (n < cap) { out[n++] = n2vc_slot[i].f; }
			n2vc_slot[i].f = NULL;
		}
	}
	return n;
}
#endif

/* ---- character device ------------------------------------------------------------------------------------------ */
#if N2VC_KERNEL

/* K01's JPEG driver (nord2_jpegdev.inc, included after this file) registers n2vc_ven_get/put here. */
extern int n2jd_share_ven(int (*get)(void), void (*put)(void));

/* IK's relay (nord2_vcodec_relay.inc, included before this file) calls this for a minor-1 client's DMA ioctls. */
static int
n2vc_relay_hw_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	(void)dev;
	struct n2vc_open *f = NULL;
	n2vc_lock();
	for (unsigned i = 0; i < N2VC_MAX_FILES; i++) {
		if (n2vc.files[i].used && n2vc.files[i].minor == 1 && n2vc.files[i].proc == p) { f = &n2vc.files[i]; break; }
	}
	for (unsigned i = 0; f == NULL && i < N2VC_MAX_FILES; i++) {
		if (!n2vc.files[i].used) { f = &n2vc.files[i]; memset(f, 0, sizeof(*f)); f->used = 1; f->minor = 1; f->proc = p; break; }
	}
	n2vc_unlock();
	if (!f) { return ENOMEM; }
	return n2vc_ioctl(f, cmd, data, flag, p);
}

/* IK's relay client close should call this (one line, posted to hw-ik): free that client's DMA handles and references. */
void
n2vc_relay_closed(struct proc *p)
{
	struct n2vc_open *dying = NULL;
	n2vc_lock();
	for (unsigned i = 0; i < N2VC_MAX_FILES; i++) {
		if (n2vc.files[i].used && n2vc.files[i].minor == 1 && n2vc.files[i].proc == p) { dying = &n2vc.files[i]; break; }
	}
	n2vc_unlock();
	if (dying) { n2vc_open_destroy(dying); }
}

static int n2vc_dev_open_body(dev_t dev, struct proc *p);
static int n2vc_dev_close_body(dev_t dev);
static int n2vc_dev_ioctl_body(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p);
/* q10jz breadcrumbs: IK's n2vr_trace() lines (kern.n2vctrace) around every entry into minor 0 */
static int
n2vc_dev_open(dev_t dev, __unused int flags, __unused int type, struct proc *p)
{
	bool t = n2vr_trace('o', (unsigned)minor(dev), 0, p);
	return n2vr_trace_end(t, n2vc_dev_open_body(dev, p));
}
static int
n2vc_dev_close(dev_t dev, __unused int flags, __unused int type, struct proc *p)
{
	bool t = n2vr_trace('c', (unsigned)minor(dev), 0, p);
	return n2vr_trace_end(t, n2vc_dev_close_body(dev));
}
static int
n2vc_dev_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	bool t = n2vr_trace('i', (unsigned)minor(dev), cmd, p);
	return n2vr_trace_end(t, n2vc_dev_ioctl_body(dev, cmd, data, flag, p));
}

static int
n2vc_dev_open_body(dev_t dev, struct proc *p)
{
	unsigned minor = (unsigned)minor(dev);
	if (minor > 1) { return ENXIO; }
	struct n2vc_open *f = n2vc_open_create(minor);
	if (!f) { return ENOMEM; }
	n2vc_lock();
	int err = n2vc_slot_add(f, minor, p);
	n2vc_unlock();
	if (err) { n2vc_open_destroy(f); return err; }
	if (minor == 0) { n2vr_server_open(); }
	return 0;
}

static int
n2vc_dev_close_body(dev_t dev)
{
	/* XNU calls d_close once at the last close of the vnode but d_open for every open (spec_vnops.c:2760 has no
	 * D_TRACKCLOSE), so there can be several slots of this minor: destroy every one of them here, and count one
	 * n2vr_server_close() per slot because each open counted one server open.  A slot with an ioctl in flight is
	 * marked dead and destroyed by n2vc_dev_ioctl when that ioctl returns. */
	struct n2vc_open *dying[N2VC_MAX_SLOTS];
	unsigned found = 0;
	unsigned mn = (unsigned)minor(dev);
	n2vc_lock();
	unsigned ndying = n2vc_slot_close_all(mn, dying, N2VC_MAX_SLOTS, &found);
	n2vc_unlock();
	for (unsigned i = 0; i < ndying; i++) { n2vc_open_destroy(dying[i]); }
	if (mn == 0) { for (unsigned i = 0; i < found; i++) { n2vr_server_close(); } }
	if (found > 1) { printf("NORD2 VCODEC: last close of minor %u ended %u opens\n", mn, found); }
	return 0;
}

static int
n2vc_dev_ioctl_body(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	if (n2vr_relay_cmd(cmd)) { return n2vr_ioctl(dev, cmd, data, flag, p); }
	struct n2vc_open *f = NULL;
	n2vc_lock();
	for (unsigned i = 0; i < N2VC_MAX_SLOTS; i++) {
		if (n2vc_slot[i].f && n2vc_slot[i].minor == (unsigned)minor(dev) && !n2vc_slot[i].dead) {
			n2vc_slot[i].busy++;
			f = n2vc_slot[i].f;
			break;
		}
	}
	n2vc_unlock();
	if (!f) { return ENXIO; }
	int err = n2vc_ioctl(f, cmd, data, flag, p);
	n2vc_lock();
	for (unsigned i = 0; i < N2VC_MAX_SLOTS; i++) {
		if (n2vc_slot[i].f == f) {
			if (n2vc_slot[i].busy) { n2vc_slot[i].busy--; }
			if (n2vc_slot[i].dead && !n2vc_slot[i].busy) {
				struct n2vc_open *dying = n2vc_slot[i].f;
				n2vc_slot[i].f = NULL; n2vc_slot[i].dead = 0;
				n2vc_unlock();
				n2vc_open_destroy(dying);
				return err;
			}
			break;
		}
	}
	n2vc_unlock();
	return err;
}

static const struct cdevsw n2vc_cdevsw = {
	.d_open = n2vc_dev_open, .d_close = n2vc_dev_close, .d_read = eno_rdwrt, .d_write = eno_rdwrt,
	.d_ioctl = n2vc_dev_ioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

static bool n2vc_started;

void
nord2_vcodec_start(void)
{
	if (n2vc_started) { return; }
	int major = cdevsw_add(-1, &n2vc_cdevsw);
	bool a = major >= 0 && devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0600, "redwood-vcodec") != NULL;
	n2vc_started = a;
	struct n2vr_hw hw = { n2vc_relay_hw_ioctl, n2vc_info_fill };
	bool r = n2vr_start(&hw);
	int jd = n2jd_share_ven(n2vc_ven_get, n2vc_ven_put);
	printf("NORD2 VCODEC: /dev/redwood-vcodec %s, /dev/redwood-vcodec-client %s, JPEG VEN sharing %s (VENC 0x17020000, "
	    "VDEC 0x16000000; pool %#x+%#x; polled, irq_mode 0)\n",
	    a ? "made" : "CANNOT BE MADE", r ? "made" : "CANNOT BE MADE", jd == 0 ? "on" : "NOT shared",
	    RW_CLAIM_VCODEC_PA, RW_CLAIM_VCODEC_BYTES);
}
#endif /* N2VC_KERNEL */
