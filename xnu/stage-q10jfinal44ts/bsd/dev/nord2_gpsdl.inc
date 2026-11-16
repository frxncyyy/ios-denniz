/* SPDX-License-Identifier: MIT */
/*
 * nord2_gpsdl.inc -- patch 0350 SKELETON: /dev/gpsdl0 (GNSS L1) and /dev/gpsdl1 (L5) for the Nord2 XNU port, the
 * character devices MediaTek's mnld/libmnl.so talk to on Android (drivers/misc/mediatek/connectivity/gps/data_link,
 * linux/gps_each_device.c). Same style as nord2_jpegdev.inc: included by bsd/dev/sweet_touch.c, host builds define
 * N2GD_HOST and supply the kernel shims.
 *
 * Milestone 1 (this file + ../../../gnss_pwr.c, host-tested by test_gnss_pwr.c):
 *   - nodes, open/close bookkeeping, ioctl numbers of the Linux driver (GPSDL_IOC_*), GPSDL_IOC_QUERY_STATUS;
 *   - power path = n2g_common_on() + n2g_dsp_on() on the first open of a link, gated by boot-arg nord2gnss=1 AND the
 *     (or n2gps=1) AND the Wi-Fi bring-up's conninfra owner being powered (n2gd_conninfra_ready(), to be wired to the persistent/wake state);
 *   - read/write/select return ENODEV until milestone 2 (the DMA rings) exists: no EMI/DMA is touched here.
 * Milestone 2 (not written): the per-link TX/RX DMA rings in a reserved EMI window (GPS_DL_TX/RX_BUF_SIZE 8 KiB),
 *   BGF_GPS_DMA (bus 0x80010000 -> AP 0x18C10000) channel setup, the GNSS IRQs (GIC SPI 383..387 in the Android DT),
 *   USRT APB mailbox for DSP wake/sleep (gps_usrt_apb.h), the DSP state machine (gps_dsp_fsm.c: RESET_DONE -> MVCD ->
 *   WORKING; the DSP code itself is sent by libmnl over this data link, "_BootDSP_UART_V2"/"Send_DspCode").
 *
 * Linux ioctls are plain numbers (cmd 6..21); on XNU the userland shim (n2mnld's ioctl wrapper) maps them to
 * N2GD_IOC(n) below, which encode the argument size, so the kernel copyin/copyout stays bounded.
 */
#ifndef NORD2_GPSDL_INC
#define NORD2_GPSDL_INC 1

#ifdef N2GD_HOST
#include "../../../gnss_pwr.h"
#else
#include "nord2_gnss_pwr.inc"   /* gnss_pwr.h + gnss_pwr.c with N2G_API static (patch 0350) */
#endif

#ifdef N2GD_HOST
/* kernel types and functions are supplied by the host test */
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/ioccom.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
#endif

#define N2GD_LINKS        2                                  /* gpsdl0 = L1, gpsdl1 = L5 */
#define N2GD_IOC(n)       _IOWR('G', (n), uint32_t)          /* Linux GPSDL_IOC_* n, 4-byte in/out argument */
enum {                                                         /* gps_dl_linux.c numbers */
	GPSDL_IOC_GPS_HWVER = 6, GPSDL_IOC_GPS_IC_HW_VERSION = 7, GPSDL_IOC_GPS_IC_FW_VERSION = 8,
	GPSDL_IOC_D1_EFUSE_GET = 9, GPSDL_IOC_RTC_FLAG = 10, GPSDL_IOC_CO_CLOCK_FLAG = 11, GPSDL_IOC_TRIGGER_ASSERT = 12,
	GPSDL_IOC_QUERY_STATUS = 13, GPSDL_IOC_TAKE_GPS_WAKELOCK = 14, GPSDL_IOC_GIVE_GPS_WAKELOCK = 15,
	GPSDL_IOC_GET_GPS_LNA_PIN = 16, GPSDL_IOC_GPS_FWCTL = 17, GPSDL_IOC_GPS_HW_SUSPEND = 18,
	GPSDL_IOC_GPS_HW_RESUME = 19, GPSDL_IOC_GPS_LISTEN_RST_EVT = 20, GPSDL_IOC_GPS_GET_MD_STATUS = 21,
};

struct n2gd_state {
	bool made, enabled, powered, hw_valid;
	int major;
	unsigned opens[N2GD_LINKS];
	int last_err;
	uint32_t conn_hw_ver;
};
static struct n2gd_state n2gd;
/* 0580: kern.n2gnss control word (default 0). bit 0 = GNSS off: ladder ioctls and the pipe answer ENODEV; bit 1 (0581) = the
 * pipe's gps_top_off gate only observes (counts/logs, still accesses). Replaces the n2gdv boot-arg bits (all taken). */
static int n2gd_ctl;
#ifndef N2GD_HOST
SYSCTL_INT(_kern, OID_AUTO, n2gnss, CTLFLAG_RW | CTLFLAG_LOCKED, &n2gd_ctl, 0, "Nord2 GNSS: bit0 off, bit1 pipe gate observe-only");
#endif
static lck_grp_t *n2gd_grp;
/*
 * 0583 (gnss-20261114): GNSS register trace that SURVIVES a hard hang. Every GNSS register access (n2gd_rd/n2gd_wr), the Wi-Fi
 * guard, every ioctl/read/write entry and the pipe events are written, BEFORE the access, as short text lines into the Linux
 * ramoops CONSOLE zone that the stock LK preserves across a WDT/warm reset: phys 0x48090000 + 0x8F000 = 0x4811F000, 0x40000 bytes
 * (live.dts mblock-16-pstore + LK bootargs ramoops.mem_address=0x48090000 mem_size=0xe0000 console_size=0x40000 pmsg_size=0x10000,
 * fs/pstore/ram.c defaults record_size = ftrace_size = 4096 -> dump zones 0x8F000 first, then the console zone). Format = struct
 * persistent_ram_buffer {u32 sig 0x43474244 'DBGC'; u32 start; u32 size; data[]} circular (ram_core.c, ecc 0), so after the hang the
 * rescue (stock Android kernel + DT) shows the trace in /sys/fs/pstore/console-ramoops-0 (gnss-20261114/sweetlog_tail.sh).
 * Mapped Device (uncached) with ml_io_map_unmappable: every line is in DRAM before the next register access, no flush needed.
 * Outside XNU's RAM (0x1_0000_0000..). sysctl kern.n2gnss bit 3 = trace off. Lines: "GT <seq> <t_ms> <event>".
 */
#ifndef N2GD_HOST
#include <kern/cpu_number.h>                                   /* 0591: cpu id in every trace line */
#endif
#define N2GT_PA    0x4811f000u
#define N2GT_SIZE  0x40000u
static volatile uint8_t *n2gt_va;
static uint32_t n2gt_seq;
static void
n2gt_put(const char *s, uint32_t n)
{
	volatile uint32_t *h = (volatile uint32_t *)(void *)n2gt_va;
	uint32_t cap = N2GT_SIZE - 12u, st = h[1], sz = h[2];
	if (st >= cap) { st = 0; sz = 0; }
	for (uint32_t i = 0; i < n; i++) { n2gt_va[12u + st] = (uint8_t)s[i]; st = (st + 1u) % cap; }
	sz = sz + n > cap ? cap : sz + n;
	h[1] = st; h[2] = sz;
	__builtin_arm_dsb(0xf);
}
static void n2gt(const char *fmt, ...) __printflike(1, 2);
static void
n2gt(const char *fmt, ...)
{
	if (!n2gt_va || (n2gd_ctl & 8)) { return; }
	char line[160];
	uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	int k = snprintf(line, sizeof line, "GT %u %llu ", ++n2gt_seq, (unsigned long long)(ns / 1000000ull));
	va_list ap; va_start(ap, fmt); k += vsnprintf(line + k, sizeof line - (size_t)k - 1, fmt, ap); va_end(ap);
	if (k > (int)sizeof line - 8) { k = (int)sizeof line - 8; }
	k += snprintf(line + k, 7, " c%d", cpu_number());           /* 0591: CPU of the access (0-3 little, 4-6 big, 7 prime) */
	line[k++] = '\n';
	n2gt_put(line, (uint32_t)k);
}
static int n2gd_ioctl(dev_t, unsigned long, caddr_t, int, struct proc *);   /* 0591: forward, for the kaddr line */
static int n2gd_read(dev_t, struct uio *, int);
static int n2gd_write(dev_t, struct uio *, int);
static int n2gl_tx(const uint8_t *, uint32_t, bool);
static void
n2gt_init(void)
{
	n2gt_va = (volatile uint8_t *)ml_io_map_unmappable(N2GT_PA, N2GT_SIZE, VM_WIMG_IO);
	if (!n2gt_va) { printf("NORD2 GNSS TRACE: map of %#x failed, no persistent trace\n", N2GT_PA); return; }
	volatile uint32_t *h = (volatile uint32_t *)(void *)n2gt_va;
	uint32_t osig = h[0], ost = h[1], osz = h[2];
	h[0] = 0x43474244u; h[1] = 0; h[2] = 0;                   /* fresh zone for this boot (the rescue already kept the old one) */
	__builtin_arm_dsb(0xf);
	printf("NORD2 GNSS TRACE: persistent register trace in the ramoops console zone %#x (+%#x), old header sig %#x start %u size %u\n",
	    N2GT_PA, N2GT_SIZE, osig, ost, osz);
	n2gt("boot: GNSS trace on (0583)");
	/* 0591: link-time addresses of a few GNSS functions, so the per-CPU last PCs that the stock kernel prints after a WDT reset
	 * (/proc/aed/reboot-reason in the rescue) can be symbolised against kernel.development (slide = runtime - nm address) */
	n2gt("kaddr n2gt_init %p n2gd_ioctl %p n2gd_read %p n2gd_write %p n2gl_tx %p", (void *)n2gt_init, (void *)n2gd_ioctl, (void *)n2gd_read,
	    (void *)n2gd_write, (void *)n2gl_tx);
}
static lck_mtx_t n2gd_lock;

/* integration points (milestone 1 stubs; the integrator binds them to the existing owners) */
static bool n2gd_conninfra_ready(void);                        /* Wi-Fi persistent init done + HOST1a0 wake vote held */
static int n2gd_hw(struct n2g_hw *hw);                         /* fills callbacks over the mapped allow-listed pages */

static int
n2gd_power_on_locked(void)
{
	if (n2gd.powered) { return 0; }
	if (!n2gd.enabled || !n2gd_conninfra_ready()) { return ENODEV; }
	struct n2g_hw hw;
	int e = n2gd_hw(&hw);
	if (e) { return e; }
	int rc = n2g_link_on(&hw);                               /* wake vote, common_on, L1 DSP on, vote released */
	n2gd.last_err = rc;
	if (rc != N2G_OK) {
		printf("NORD2 GNSS: power on failed, step %d\n", rc);
		return EIO;
	}
	n2gd.conn_hw_ver = hw.read32(hw.ctx, N2G_CONN_HW_VER);
	n2gd.powered = true;
	printf("NORD2 GNSS: powered (conn_infra %#x)\n", n2gd.conn_hw_ver);
	return 0;
}

/*
 * 0353 (q10jr3): after the q10jr reset (phone back to fastboot when the probe opened /dev/gpsdl0), the power path runs
 * only as explicit single steps, each guarded like the Wi-Fi driver guards its own conn_infra accesses (nwfn_guard: SPM
 * 0x10006304[4:0]=0xd, 0x1000616c/0x10006170 bit1, GALS 0x1020e504 bit0 = 1, infracfg bus protection 0x10001228[13] = 0
 * and 0x10001724[2] = 0, all always-on AP registers), with a console marker and a 300 ms pause before every risky access:
 *   step 1: read and print the six always-on predicates only (no conn_infra access at all)
 *   step 2: + read CONN_HOST_CSR_TOP 0x18060184 (on2off sleep-protection ack) and 0x180601ac (GPS wake vote)
 *   step 3: + GPS wake vote, poll the ack, read the conn_infra version 0x18001000, release the vote
 *   step 4: the full n2g_link_on (common_on + L1 DSP on)
 * Return value: bit 0 = step completed, bits 8..15 = n2g error / refusal code, bits 16..23 = failing predicate index.
 */
static bool n2gd_ao_predicates(uint32_t out[6]);                 /* sweet_touch.c binding: true when all six match */
/* step 4: every register access is announced on the console (and given 20 ms to drain) before it is made */
static struct n2g_hw n2gd_inner;
static unsigned n2gd_acc;
static uint32_t
n2gd_trace_rd(__unused void *c, uint32_t pa)
{
	printf("NORD2 GNSS ACC %u: read %#x\n", ++n2gd_acc, pa); delay(20000);
	uint32_t v = n2gd_inner.read32(n2gd_inner.ctx, pa);
	printf("NORD2 GNSS ACC %u: = %#x\n", n2gd_acc, v);
	return v;
}
static void
n2gd_trace_wr(__unused void *c, uint32_t pa, uint32_t v)
{
	printf("NORD2 GNSS ACC %u: write %#x = %#x\n", ++n2gd_acc, pa, v); delay(20000);
	n2gd_inner.write32(n2gd_inner.ctx, pa, v);
}
/*
 * 0354 (q10jr4): after q10jr3 step 2 reset the phone (first host-CSR read), every single conn_infra access is its own
 * ioctl, done the way the Wi-Fi driver does it: the six always-on predicates AND its GIC check (GICD+0x130 bits 23/26
 * clear), Wi-Fi's own mapping of the host CSR page (npa_native.host, not a new ml_io_map), the Wi-Fi software state
 * printed first (ap/wf ownership, asleep, stage: no register read), and a 2 s pause after the marker so the console
 * drains before the access:
 *   111: no access: predicates + GIC + Wi-Fi software state only
 *   112: read 0x180601a0 (Wi-Fi's own HOST wake vote register; Wi-Fi reads it in nwfn wake/release)
 *   113: read 0x18060184 (on2off sleep-protection ack; Wi-Fi polls it after its vote)
 *   114: read 0x180601ac (GPS wake vote register from the vendor v010 headers; NOT one Wi-Fi ever touches)
 *   115: ml_io_map of the 6 GNSS pages only, no access (tests the mapping step of q10jr3 step 2); since 0355 it maps
 *        lazily from kernel_map (n2gd_hw maps nothing; ONE 5 now only prints "mapped, rc=0")
 */
static bool n2gd_wifi_guard(uint32_t out[9]);                    /* sweet_touch.c: predicates + GIC + Wi-Fi state */
static vm_offset_t n2gd_wifi_host(void);                         /* sweet_touch.c: npa_native.host (0x18060000 page) */
/* 0355 (q10jr5): q10jr4 one5 showed the ml_io_map of the 6 pages itself resets the phone (io_submap is 8 MiB and
 * KMA_NOFAIL: exhaustion = a panic the console never shows). GNSS pages now come from kernel_map, lazily, one action per
 * boot in ioctls 121..130 (sweet_touch.c n2gd_mapone has the table). */
static uint32_t n2gd_mapone(int k);
static uint32_t
n2gd_single(int k)
{
	uint32_t g[9] = {0};
	bool ok = n2gd_wifi_guard(g);
	printf("NORD2 GNSS ONE %d: spm304=%#x gals504=%#x infra228=%#x infra724=%#x gic130=%#x wifi ap=%u wf=%u asleep=%u stage=%u -> %s\n",
	    k, g[0], g[3], g[4], g[5], g[6], g[7] >> 16, g[7] & 0xffff, g[8] >> 16, g[8] & 0xffff, ok ? "guard ok" : "GUARD REFUSES");
	if (k == 1) { return 1; }
	if (!ok) { return 0x0100u; }
	if (k == 5) {
		struct n2g_hw hw;
		printf("NORD2 GNSS ONE 5: about to ml_io_map the 6 GNSS pages (no access)\n"); delay(2000000);
		int e = n2gd_hw(&hw);
		printf("NORD2 GNSS ONE 5: mapped, rc=%d\n", e);
		return e ? 0x0300u : 1u;
	}
	vm_offset_t host = n2gd_wifi_host();
	if (!host) { printf("NORD2 GNSS ONE %d: no Wi-Fi host mapping\n", k); return 0x0400u; }
	uint32_t off = k == 2 ? 0x1a0u : k == 3 ? 0x184u : 0x1acu;
	printf("NORD2 GNSS ONE %d: about to read %#x through the Wi-Fi host mapping\n", k, 0x18060000u + off); delay(2000000);
	uint32_t val = *(volatile uint32_t *)(host + off);
	printf("NORD2 GNSS ONE %d: %#x = %#x\n", k, 0x18060000u + off, val);
	return 1;
}

/*
 * 0356 (q10jr6): the GNSS power-on ladder. q10jr5 proved the host CSR block readable through Wi-Fi's mapping (0x180601a0 = 1,
 * 0x18060184 = 0xe00, 0x180601ac = 0) and the GNSS pages mappable from kernel_map. Ioctl 140+K runs the proven steps 1..K-1
 * and then the NEW step K (2 s console pause before it), prints every write as old -> new (readback), checks the Wi-Fi
 * guard (six always-on predicates, GIC, Wi-Fi ap owned and awake) before EVERY access, and finally rolls back every step it
 * did, restoring the saved original bits in the vendor off order (gps_dl_hw_gps_common_off: sleep protection on first).
 * If the guard refuses, nothing more is accessed (not even the rollback) and the result carries bit 24.
 *   1 GPS wake vote: 0x180601ac[0] = 1, poll 0x18060184[5] == 0 (10 ms)           rollback: [0] restored
 *   2 read only: conn_infra cfg page (version 0x18001000, 878, c1c, 618, 61c, 900)
 *   3 read only: conn_infra RGU page (0x18000008, 0x18000114)
 *   4 read only: 0x18005008, 0x180601d0, 0x180600b0, 0x18060240
 *   5 GPS EMI remap 0x180601d0[19:0] = 0x8a000000 >> 16                           rollback: restored
 *   6 BGFSYS_ON_TOP power: 0x18000008 = key 0x4254 | [7]; ack 0x18000114 read     rollback: key | original [7]
 *   7 GP_FUNCTION_EN 0x18001878[0] = 1; poll ack 0x18000114 [24] then [25]        rollback: [0] restored
 *   8 GALS sleep protection off (conn2gps rx/tx, gps2conn rx/tx: EN = 0, RDY == 0)  rollback: EN restored, RDY polled
 *   9 0x180600b0[7:0] = 0x80, poll 0x18060240[15:0] == 2 (gps_top_off ACTIVE)    rollback: sel restored
 *  10 A-die top clock 0x18001900[1] = 1, poll 0x18005008[26] == 0                  rollback: [1] restored
 *  11 read only, FIRST GPS-bus access: 0x18c0103c, 0x18c73108, USRT L1 0x18c73004/08/0c/20/24/34
 *  12 GPS CLKGEN1 0x18c0103c[4] = 1                                                 rollback: [4] restored
 *  13 L1 DSP on: 0x18c73108 [1:0] = 0, [3] = 0, [4] pulse; poll USRT D2AF 0x18c73024[3] (DSP ROM okay, 100 ms, info)
 *                                                                                    rollback: [4:0] restored
 * 0357 (q10jr7): q10jr6 lad13 passed (CLKGEN1 0x18c0103c read 0x10840, but every USRT word read 0xffffffff): the ladder
 * never did plat/v010 gps_dl_hw_dep_set_dsp_on_and_poll_ack, the GPS L1 power/reset release in GPS RGU_ON. Now:
 *  13 L1 DSP pwr_stat ON exactly as the vendor: [1:0] = 0, [3] = 0, [4] DIS_HW_RST_CNT = 1 (no pulse, no poll)
 *  14 read only: GPS RGU_ON 0x18c00014/160/164 and CFG_ON 0x18c01020 (first access to the RGU_ON page)
 *  15 L1_ON + SOFT_RST_B (0x18c00014 [0], [5]), poll 0x18c01020[3:0] == 3, MEM_ADJ_DLY_EN, DUMMY_NUM = 5, USRT readback
 *  16 USRT on (DMA mode, 4-byte, interrupts left off), A2D1 = 0xe, A2DF3 = 1, poll D2AF3 (DSP ROM okay, 100 ms)
 *  17 steps 1..16, 5 s powered with status reads, full rollback;  18 HOLD after 1..16 (not rolled back)
 * The q10jr6 meaning of 14/15 below is replaced by 17/18.
 *  14 HOLD: steps 1..13, then only the wake vote is released (as n2g_link_on does). NOT ROLLED BACK: GNSS stays powered
 *     until the next reboot (needed for the data link; no power-off path exists in this image).
 *  15 steps 1..13, 5 s powered with a status read every second, then the full rollback.
 * Result: bit 0 = completed, bits 8..15 = failing step, bits 16..23 = n2g error code, bit 24 = guard refused (stopped,
 * rollback incomplete), bit 25 = a rollback poll timed out.
 */
#ifndef N2G_USRT_BASE                                            /* gnss_dl.h (M2) is not in this image */
#define N2G_USRT_BASE(l)        N2G_GPS_BUS((l) ? 0x80083000u : 0x80073000u)
#define N2G_USRT_CTRL(l)        (N2G_USRT_BASE(l) + 0x04u)
#define N2G_USRT_INTEN(l)       (N2G_USRT_BASE(l) + 0x08u)
#define N2G_USRT_STA(l)         (N2G_USRT_BASE(l) + 0x0cu)
#define N2G_USRT_A2DF(l)        (N2G_USRT_BASE(l) + 0x20u)
#define N2G_USRT_D2AF(l)        (N2G_USRT_BASE(l) + 0x24u)
#define N2G_USRT_A2D1(l)        (N2G_USRT_BASE(l) + 0x34u)
#endif
static struct n2g_hw n2gl_hw;
static bool n2gl_stop;
static int n2gl_k;
static struct { uint32_t vote, emi, bgf, func, c2g, g2c, sel, adie, clk1, aon, l1cr, dly, mem, emictl; } n2gl_o;
static bool n2gl_v9;                                            /* 0362: K >= 23, vendor-order data link (INTEN, D2AF clear, EMI req, RX armed) */
static bool n2gl_v9hold;                                        /* 0362: K = 24 HOLD: read() uses the pre-armed RX DMA */
static bool n2gd_dl_up;                                         /* 0359: HOLD with the data link up (read/write work) */
static bool
n2gl_guard(void)
{
	uint32_t g[9] = {0};
	if (n2gl_stop) { return false; }
	bool ok = n2gd_wifi_guard(g) && (g[7] >> 16) == 1 && (g[8] >> 16) == 0;
	if (!ok) {
		n2gl_stop = true;
		printf("NORD2 GNSS LAD %d: GUARD REFUSES, nothing more is accessed: spm304=%#x gals504=%#x infra228=%#x infra724=%#x gic130=%#x ap=%u wf=%u asleep=%u stage=%u\n",
		    n2gl_k, g[0], g[3], g[4], g[5], g[6], g[7] >> 16, g[7] & 0xffff, g[8] >> 16, g[8] & 0xffff);
	}
	return ok;
}
static uint32_t
n2gl_rd(uint32_t pa)
{
	return n2gl_guard() ? n2gl_hw.read32(n2gl_hw.ctx, pa) : 0xffffffffu;
}
static void
n2gl_show(const char *what, uint32_t pa)
{
	uint32_t v = n2gl_rd(pa);
	if (!n2gl_stop) { printf("NORD2 GNSS LAD %d: %s %#x = %#x\n", n2gl_k, what, pa, v); }
}
/* read-modify-write of the masked bits, then readback */
static bool
n2gl_field(uint32_t pa, uint32_t mask, uint32_t val)
{
	uint32_t old = n2gl_rd(pa);
	if (n2gl_stop || !n2gl_guard()) { return false; }
	n2gl_hw.write32(n2gl_hw.ctx, pa, (old & ~mask) | (val & mask));
	uint32_t rb = n2gl_rd(pa);
	if (n2gl_stop) { return false; }
	printf("NORD2 GNSS LAD %d: write %#x mask %#x: %#x -> %#x\n", n2gl_k, pa, mask, old, rb);
	return true;
}
static bool
n2gl_poll(uint32_t pa, uint32_t mask, uint32_t want, int max_us, const char *what)
{
	uint32_t v = 0;
	for (int i = 0; i < max_us; i++) {
		v = n2gl_rd(pa);
		if (n2gl_stop) { return false; }
		if ((v & mask) == want) { printf("NORD2 GNSS LAD %d: %s ok after %d polls (%#x = %#x)\n", n2gl_k, what, i, pa, v); return true; }
		delay(1);
	}
	printf("NORD2 GNSS LAD %d: %s TIMEOUT (%#x = %#x)\n", n2gl_k, what, pa, v);
	return false;
}
static const struct { uint32_t pa, en, rdy; } n2gl_slp_off[4] = {
	{ N2G_CFG_GALS_CONN2GPS, 1u << 4, 1u << 7 }, { N2G_CFG_GALS_CONN2GPS, 1u << 0, 1u << 3 },
	{ N2G_CFG_GALS_GPS2CONN, 1u << 4, 1u << 7 }, { N2G_CFG_GALS_GPS2CONN, 1u << 0, 1u << 3 },
};
static const char *const n2gl_name[44] = { "", "GPS wake vote", "read conn_infra cfg page", "read conn_infra RGU page",
	"read WB_STA + GPS CSRs", "GPS EMI remap", "BGFSYS_ON_TOP power", "GP_FUNCTION_EN + BGF acks", "GALS sleep protection off",
	"gps_top_off ACTIVE check", "A-die top clock", "read GPS bus (first)", "GPS CLKGEN1 DIGCK", "L1 DSP pwr_stat ON",
	"read GPS RGU_ON page", "L1 power on + soft reset release", "USRT on + DSP ROM okay", "data link up (rings, DMA idle check)", "send libmnl frame 1 (A2D DMA)", "capture the DSP reply (D2A DMA)",
	"5 s powered", "L1 read-only probe", "HOLD + /dev/gpsdl0 read/write",
	"V9 vendor-order capture (D2AF clear, EMI req, RX armed before TX, frame 1, 2 s)", "V9 HOLD (vendor order, RX armed, /dev/gpsdl0 pipe)",
	"S10a RX DMA2 armed small (16 words) 3 s, no TX, disarm", "S10b RX DMA2 armed small + TX boot header, 1 s, disarm",
	"S10c TX boot header, NO RX DMA (reply stays in the USRT FIFO), INTEN off", "S10d as S10c with USRT INTEN on (vendor)",
	"S10e RX DMA2 armed small, GPS wake vote RELEASED 3 s idle, vote back, disarm",
	"S11 DSP code download (uploaded libmnl frames), post-boot command, rollback", "S11 DSP code download + HOLD (vote held, INTEN off, RX 256 B per arming)",
	"S12 HOLD with the DSP in ROM (NO kernel download): libmnl boots the DSP itself over the pipe, vendor flow (0586)",
	"S13 pacing probe: S11 download with a 200 ms idle gap after fragment 1 (1 ms state samples), then HOLD as K=31 (0587)",
	"S12b = K=32 + NO LNA + ladder-style RX (fresh 64 B arming before each pipe TX) + 2 s settle (0588)",
	"S12c = K=32 + NO LNA + 2 s settle (0588)", "S12d = K=32 + ladder-style RX + 2 s settle, LNA on (0588)",
	"S12e = K=34 + QUIET pipe (no gate register reads, no console lines) + RX round closed/re-armed on data (S11-exact) + GPS GIC SPIs masked/logged at HOLD (0589)",
	"S12f = S12e + GIC enable/pending/active words of the GPS SPIs traced before/after every pipe TX (0589)",
	"S12g = S12e with the pipe TX done by the ladder's own n2gl_tx (readbacks + printf before the done poll) (0590)",
	"S12h = S12e with a no-poll pipe TX: 200 us wait after START, then single done-flag checks every 200 us (vendor waits the IRQ) (0590)",
	"S12i = K=35 + quiet gate; the boot is driven frame by frame by ioctl 190 = the ladder's own n2gb_xchg (RX close/arm, n2gl_tx, ack poll) (0591)",
	"S12j = S12i (ioctl-190 boot) + RX round closed on data, for libmnl after the boot (0592)",
	"S12k = S12i (ioctl-190 boot); after it the pipe TX uses the ladder n2gl_tx, RX via read() with byte-wise (volatile) ring copies (0592)" };
/* 0357: GPS RGU_ON (bus 0x80000000 -> AP 0x18c00000) and CFG_ON (0x80001000 -> 0x18c01000), plat/v010 gps_rgu_on.h / gps_cfg_on.h */
#define N2G_GPS_RGU_L1_CR          0x18c00014u   /* [0] RGU_GPS_L1_ON [5] RGU_GPS_L1_SOFT_RST_B */
#define N2G_GPS_RGU_L1_DLY_CHAIN   0x18c00160u   /* [19:16] MEM_PDN_DELAY_DUMMY_NUM (vendor 5) */
#define N2G_GPS_RGU_L1_MEM_DLY     0x18c00164u   /* [31] MEM_ADJ_DLY_EN */
#define N2G_GPS_CFG_L1_SLP_PWR     0x18c01020u   /* [3:0] GPS_L1_SLP_PWR_CTL_CS: poll == 3 after L1 on */
/* one ladder step; 0 or an n2g error code. done[] is set before the first write so a partial step is rolled back. */
/*
 * 0359 (q10jr8): data link. The q10jr7 "USRT 0xffffffff" were refusals of the 0350 allow-list (13 "refused read 0x18c730xx"
 * in the gl7b log), not a dead block: the l7e probe, which bypasses the allow-list, read CTRL 0x24010138, STA 0x22000030 and
 * D2AF 0x8. The allow-list now has the L1 USRT words and the BGF_GPS_DMA channel 1/2 registers (nord2_gnss_pwr.inc).
 * Rings: 64 KiB at phys 0xDFE00000 (unused gap 0xDA800000..0xDFE70000, redwood_vcodec.h), L1 TX at +0, RX at +0x2000,
 * kernel_map Device mapping; the GPS EMI remap (step 5) points at 0xDFE00000, so bus 0x78000000 = phys 0xDFE00000.
 * DMA in 4-byte units (vendor default), polling only. Vendor sources: gps_dl_hw_set_dma_start/stop, gps_dl_hal RX flow.
 */
#ifndef N2G_DMA_BIT
#define N2G_DMA_WPPT(ch)        N2G_GPS_BUS(0x80010108u + 0x100u * (ch))
#define N2G_DMA_WPTO(ch)        N2G_GPS_BUS(0x8001010cu + 0x100u * (ch))
#define N2G_DMA_COUNT(ch)       N2G_GPS_BUS(0x80010110u + 0x100u * (ch))
#define N2G_DMA_CON(ch)         N2G_GPS_BUS(0x80010114u + 0x100u * (ch))
#define N2G_DMA_START(ch)       N2G_GPS_BUS(0x80010118u + 0x100u * (ch))
#define N2G_DMA_INTSTA(ch)      N2G_GPS_BUS(0x8001011cu + 0x100u * (ch))
#define N2G_DMA_ACKINT(ch)      N2G_GPS_BUS(0x80010120u + 0x100u * (ch))
#define N2G_DMA_RLCT(ch)        N2G_GPS_BUS(0x80010124u + 0x100u * (ch))
#define N2G_DMA_PGMADDR(ch)     N2G_GPS_BUS(0x8001012cu + 0x100u * (ch))
#define N2G_DMA_STATE(ch)       N2G_GPS_BUS(0x80010148u + 0x100u * (ch))
#define N2G_DMA_BIT             0x8000u
#define N2G_CON_L1_A2D          0x00128016u
#define N2G_CON_L1_D2A          0x0007801Au
#endif
#define N2GL_RING      8192u
#define N2GL_TXCH      0                     /* DMA1: L1 A2D */
#define N2GL_RXCH      1                     /* DMA2: L1 D2A */
static uint8_t *n2gl_ring_va;
/*
 * 0592 (gnss-20261114): the ring window is a Device mapping (ml_io_map_unmappable VM_WIMG_IO). The compiler vectorised n2gd_read's
 * ring -> tmp copy into 16-byte LDR Q loads at ring + (rd & 0x1fff) (verified in the q10jgde disassembly: 16 'ldr q, [x, #0x2000]'),
 * i.e. at any 4-byte-aligned rd: an unaligned access to Device memory is an Alignment fault (ARMv8: always, whatever SCTLR.A), a kernel
 * data abort, and a panic on this port never reaches the console => silent freeze + WDT 20 s later, with no GNSS register access
 * pending, exactly the signature of every HOLD-read death. The working paths (S11 loop, n2gp_feed, ioctl-190 boot) read the ring
 * byte by byte. From 0592 every ring access goes through this volatile byte view: plain LDRB/STRB, never vector/pair/DC ZVA.
 */
#define N2R ((volatile uint8_t *)n2gl_ring_va)
static struct { uint32_t rd, wr; } n2gl_rx;
static bool n2gl_tx_started, n2gl_rx_started;
static int n2gd_ringwin(uint8_t **va);
static uint32_t n2gd_gicd_rd(uint32_t off);                    /* 0363: sweet_touch.c, read-only GICD word */          /* sweet_touch.c: maps (once) and zeroes the 64 KiB window */
/*
 * 0584 (gnss-20261114): GPS EMI window moved to the vendor layout. gnsstr1 (0583 trace, 06:07) died ~10 ms after an ordinary RX DMA2
 * re-arm with no AP access pending, i.e. not on an AP register access. The vendor reserves 1 MiB for GPS and lays it out as
 * (linux/inc/gps_dl_linux_reserved_mem.h): icap_buf 0x50000 (DSP-owned, I/Q capture) | 4 KiB pad | tx_dma_buf[2][8K+4K] |
 * rx_dma_buf[2][8K+4K] | met_buf 32 KiB = 0x65000, remap base = start of the reserve (hal/gps_dl_conn_infra.c, bus 0x78000000,
 * 64 MiB window). Our 0359 window at 0xDFE00000 put the TX/RX rings at remap offsets 0/0x2000 = inside the DSP's icap area, and
 * the 1 MiB reserve the DSP may use reached DPM firmware memory at 0xDFE70000 (offset 0x70000; live.dts mblock-27-DPM-reserved)
 * and the modem at 0xE0000000. Now: remap base 0xDFC00000 (1 MiB aligned; 0xDFC00000..0xDFE70000 is free: jpegdec ends 0xDFC00000),
 * rings at the vendor offsets (L1 TX 0x51000, L1 RX 0x51000 + 0x2000 as before relative to the ring window), the whole first 1 MiB
 * is free RAM, DPM is at offset 0x270000. */
#define N2GL_RING_OFF  0x51000u
static uint32_t n2gl_bus(uint32_t off) { return 0x78000000u + N2GL_RING_OFF + off; }   /* off from the ring window base */
static uint32_t n2gl_rx_used(void) { return (n2gl_rx.wr + N2GL_RING - n2gl_rx.rd) % N2GL_RING; }
static bool
n2gl_wr(uint32_t pa, uint32_t v)
{
	if (!n2gl_guard()) { return false; }
	n2gl_hw.write32(n2gl_hw.ctx, pa, v);
	return true;
}
static void
n2gl_dma_regs(const char *what, int ch)
{
	uint32_t st = n2gl_rd(N2G_DMA_START(ch)), is = n2gl_rd(N2G_DMA_INTSTA(ch)), rl = n2gl_rd(N2G_DMA_RLCT(ch)), sa = n2gl_rd(N2G_DMA_STATE(ch));
	if (!n2gl_stop) { printf("NORD2 GNSS LAD %d: %s DMA%d start=%#x intsta=%#x rlct=%#x state=%#x\n", n2gl_k, what, ch + 1, st, is, rl, sa); }
}
static bool
n2gl_dma_start(int ch, uint32_t con, uint32_t ring_off, uint32_t at, uint32_t bytes)
{
	return n2gl_wr(N2G_DMA_CON(ch), con) && n2gl_wr(N2G_DMA_PGMADDR(ch), n2gl_bus(ring_off + at)) &&
	    n2gl_wr(N2G_DMA_WPTO(ch), n2gl_bus(ring_off)) && n2gl_field(N2G_DMA_WPPT(ch), 0xffffu, (N2GL_RING - at) / 4u) &&
	    n2gl_field(N2G_DMA_COUNT(ch), 0xffffu, bytes / 4u) && n2gl_field(N2G_DMA_START(ch), N2G_DMA_BIT, N2G_DMA_BIT);
}
static void
n2gl_dma_stop(int ch)
{
	n2gl_field(N2G_DMA_START(ch), N2G_DMA_BIT, 0);
	n2gl_field(N2G_DMA_ACKINT(ch), N2G_DMA_BIT, N2G_DMA_BIT);
}
static bool
n2gl_dma_done(int ch)
{
	return (n2gl_rd(N2G_DMA_INTSTA(ch)) & N2G_DMA_BIT) && (n2gl_rd(N2G_DMA_STATE(ch)) & 0x7fu) == 1u;
}
/* synchronous TX of len bytes (zero padded to words) from offset 0 of the TX ring; 0 or an error */
static int
n2gl_tx(const uint8_t *buf, uint32_t len, bool log)
{
	if (!n2gl_ring_va || !len || len > N2GL_RING - 4u) { return N2G_E_ARG; }
	uint32_t padded = (len + 3u) & ~3u;
	for (uint32_t i = 0; i < padded; i++) { N2R[i] = i < len ? buf[i] : 0; }
	__builtin_arm_dsb(0xf);
	n2gl_tx_started = true;
	if (!n2gl_dma_start(N2GL_TXCH, N2G_CON_L1_A2D, 0, 0, padded)) { return N2G_E_ARG; }
	int i;
	for (i = 0; i < 100000 && !n2gl_stop && !n2gl_dma_done(N2GL_TXCH); i++) { delay(1); }
	if (log) { n2gl_dma_regs(i < 100000 ? "TX done" : "TX TIMEOUT", N2GL_TXCH); }
	n2gl_dma_stop(N2GL_TXCH);
	if (n2gl_stop) { return N2G_E_ARG; }
	return i < 100000 ? 0 : 9;
}
/* one RX round: if the DSP has data (USRT STA TX_IND), D2A DMA into the RX ring until done / no-data; bytes received */
static uint32_t
n2gl_rx_round(int wait_us, bool log)
{
	uint32_t sta = 0;
	int t;
	for (t = 0; t < wait_us; t++) {
		sta = n2gl_rd(N2G_USRT_STA(0));
		if (n2gl_stop) { return 0; }
		if (sta & (1u << 3)) { break; }
		delay(1);
	}
	if (!(sta & (1u << 3))) { return 0; }
	uint32_t fr = (N2GL_RING - 4u - n2gl_rx_used()) & ~3u;
	if (fr < 4u) { return 0; }
	n2gl_rx_started = true;
	if (!n2gl_dma_start(N2GL_RXCH, N2G_CON_L1_D2A, N2GL_RING, n2gl_rx.wr, fr)) { return 0; }
	int i;
	bool nodata = false, done = false;
	for (i = 0; i < 100000 && !n2gl_stop; i++) {
		done = n2gl_dma_done(N2GL_RXCH);
		uint32_t s2 = n2gl_rd(N2G_USRT_STA(0));
		nodata = (s2 & (1u << 1)) != 0;                        /* 0362: NODAINTB only (vendor); REGE [5] is set at idle (STA 0x22100030) */
		if (done || nodata) { break; }
		delay(1);
	}
	n2gl_dma_stop(N2GL_RXCH);
	uint32_t left = (n2gl_rd(N2G_DMA_RLCT(N2GL_RXCH)) & 0xffffu) * 4u;
	uint32_t got = left <= fr ? fr - left : 0;
	if (nodata) { n2gl_field(N2G_USRT_STA(0), 1u << 1, 1u << 1); }      /* NODAINTB is write-1-to-clear */
	if (log) { printf("NORD2 GNSS LAD %d: RX round: sta=%#x after %d us, %s after %d us, got %u bytes\n", n2gl_k, sta, t, done ? "DMA done" : nodata ? "no-data" : "TIMEOUT", i, got); }
	n2gl_rx.wr = (n2gl_rx.wr + got) % N2GL_RING;
	return got;
}
static void
n2gl_hexdump(const char *what, uint32_t from, uint32_t n)
{
	char line[3 * 32 + 1];
	for (uint32_t o = 0; o < n; o += 32) {
		uint32_t m = n - o < 32 ? n - o : 32, p = 0;
		for (uint32_t i = 0; i < m; i++) {
			static const char hx[] = "0123456789abcdef";
			uint8_t b = N2R[N2GL_RING + (from + o + i) % N2GL_RING];
			line[p++] = hx[b >> 4]; line[p++] = hx[b & 15]; line[p++] = ' ';
		}
		line[p] = 0;
		printf("NORD2 GNSS LAD %d: %s +%u: %s\n", n2gl_k, what, o, line);
	}
}
/* libmnl's first frame to the DSP ROM (captured in the M3 emulator: _BootDSP_UART_V2) */
static const uint8_t n2gl_frame1[15] = { 0xaa, 0xf0, 0x09, 0x00, 0x05, 0xfe, 0x32, 0x00, 0x00, 0x01, 0x00, 0x3f, 0x01, 0xaa, 0x0f };

/*
 * 0362 (q10jr9): vendor-order data link ("V9", ladder K = 23 capture, K = 24 HOLD). q10jr8 gl8b: frame 1 left by A2D DMA
 * (DMA1 done), USRT STA stayed 0x22100030, D2AF stayed 0x8 and no byte came back. Differences from the vendor gps_dl
 * (hal/gps_dl_hal.c, hal/gps_dl_isr.c, hw/gps_dl_hw_usrt_apb.c, hw/gps_dl_hw_power_ctrl.c) closed here:
 *  a) MCUB D2AF RESET_DONE [3] was never acknowledged. gps_dl_hal_mcub_flag_handler reads D2AF/D2A0/D2A1, writes the flag
 *     back (W1C, gps_dl_hw_clear_mcub_d2a_flag) and only then gps_dsp_fsm(RESET_DONE) marks the link ready to write;
 *  b) CONN_INFRA_CFG EMI_CTL_GPS 0x18001c1c [0] EMI_REQ_GPS was never set; the vendor claims it around every TX/RX DMA
 *     (gps_dl_hal_emi_usage_claim -> gps_dl_hw_gps_sw_request_emi_usage, GPS_DL_USE_TIA = 0 on plat v010);
 *  c) USRT INTEN TXIEN/NODAIEN stayed 0 (step 16 sets them when V9);
 *  d) RX started only after a polled TX_IND. Now D2A DMA2 is armed on the whole free RX ring BEFORE the TX (the vendor
 *     starts it from the has-data IRQ; arming first cannot miss a fast reply), the live write index comes from RLCT as in
 *     gps_dl_real_dma_get_rx_write_index, the round is closed on DMA done or NODAINTB and re-armed. The RX ring is
 *     pre-filled with 0xA5 so a DMA write shows up even when RLCT does not move.
 */
#ifndef N2G_USRT_MONF
#define N2G_USRT_MONF(l)        (N2G_USRT_BASE(l) + 0x10u)
#define N2G_USRT_D2A0(l)        (N2G_USRT_BASE(l) + 0x50u)
#define N2G_USRT_D2A1(l)        (N2G_USRT_BASE(l) + 0x54u)
#endif
#define N2GL_FILL      0xA5u
static bool n2gl_rx_armed, n2gl_rx_rlct_ok, n2gl_rx_norearm;   /* 0363: norearm = S10 single-shot RX */
static uint32_t n2gl_rx_cap;                                    /* 0363: max bytes per RX arming (0 = whole free ring) */
static uint32_t n2gl_rx_at, n2gl_rx_len, n2gl_rx_cur, n2gl_rx_total;
static int n2gl_lines;
static bool
n2gl_rmw_q(uint32_t pa, uint32_t mask, uint32_t val)               /* quiet read-modify-write (no console line) */
{
	uint32_t old = n2gl_rd(pa);
	return !n2gl_stop && n2gl_wr(pa, (old & ~mask) | (val & mask));
}
static bool
n2gl_dma_start_q(int ch, uint32_t con, uint32_t ring_off, uint32_t at, uint32_t bytes)
{	/* gps_dl_hw_set_dma_start order: CON, PGMADDR, WPTO, WPPT, COUNT, START.STR */
	return n2gl_wr(N2G_DMA_CON(ch), con) && n2gl_wr(N2G_DMA_PGMADDR(ch), n2gl_bus(ring_off + at)) &&
	    n2gl_wr(N2G_DMA_WPTO(ch), n2gl_bus(ring_off)) && n2gl_rmw_q(N2G_DMA_WPPT(ch), 0xffffu, (N2GL_RING - at) / 4u) &&
	    n2gl_rmw_q(N2G_DMA_COUNT(ch), 0xffffu, bytes / 4u) && n2gl_rmw_q(N2G_DMA_START(ch), N2G_DMA_BIT, N2G_DMA_BIT);
}
static void
n2gl_dma_stop_q(int ch)
{	/* gps_dl_hw_set_dma_stop: STR = 0, ACKINT = 1 */
	n2gl_rmw_q(N2G_DMA_START(ch), N2G_DMA_BIT, 0);
	n2gl_rmw_q(N2G_DMA_ACKINT(ch), N2G_DMA_BIT, N2G_DMA_BIT);
	(void)n2gl_rd(N2G_DMA_STATE(ch));                         /* 0584: channel state after the stop, in the 0583 trace */
}
/* arm DMA2 on all free RX ring bytes from the write index (it wraps at the ring end through WPPT/WPTO) */
static bool
n2gl_rx_arm(bool log)
{
	if (n2gl_rx_armed) { return true; }
	uint32_t fr = (N2GL_RING - 4u - n2gl_rx_used()) & ~3u;
	if (n2gl_rx_cap && fr > n2gl_rx_cap) { fr = n2gl_rx_cap; }
	if (fr < 4u) { return false; }                          /* ring full: data waits in the USRT FIFO until read() */
	n2gl_rx_started = true;
	if (!n2gl_dma_start_q(N2GL_RXCH, N2G_CON_L1_D2A, N2GL_RING, n2gl_rx.wr, fr)) { return false; }
	n2gl_rx_armed = true; n2gl_rx_at = n2gl_rx.wr; n2gl_rx_len = fr; n2gl_rx_cur = 0;
	uint32_t rl = n2gl_rd(N2G_DMA_RLCT(N2GL_RXCH)) & 0xffffu;
	n2gl_rx_rlct_ok = rl == fr / 4u;                        /* RLCT loaded with COUNT at start: live index usable */
	if (log) {
		printf("NORD2 GNSS LAD %d: RX DMA2 ARMED at ring +%u for %u bytes (COUNT %u words), RLCT now %u (%s)\n", n2gl_k,
		    n2gl_rx_at, fr, fr / 4u, rl, n2gl_rx_rlct_ok ? "live index from RLCT" : "RLCT not loaded: ring pattern decides");
	}
	return !n2gl_stop;
}
/* bytes the armed round has written: RLCT if usable, else the last word that is no longer the 0xA5 fill */
static uint32_t
n2gl_rx_written(void)
{
	if (n2gl_rx_rlct_ok) {
		uint32_t left = (n2gl_rd(N2G_DMA_RLCT(N2GL_RXCH)) & 0xffffu) * 4u;
		return left <= n2gl_rx_len ? n2gl_rx_len - left : 0;
	}
	uint32_t last = 0;
	for (uint32_t o = 0; o < n2gl_rx_len; o += 4u) {
		const volatile uint8_t *w = &N2R[N2GL_RING + (n2gl_rx_at + o) % N2GL_RING];
		if (w[0] != N2GL_FILL || w[1] != N2GL_FILL || w[2] != N2GL_FILL || w[3] != N2GL_FILL) { last = o + 4u; }
	}
	return last;
}
/* 0581: pipe heartbeat state (defined with the pipe below) */
static void n2gq_mcub_note(uint32_t f, uint32_t d0, uint32_t d1);
static void n2gq_hb(const char *why);
static void n2gd_lna_l1_dsp_ctrl(void);                         /* 0585: nord2_gnss_bind.inc, GPIO162 -> GPS_L1_ELNA_EN */
/* ack MCUB D2A flags like gps_dl_hal_mcub_flag_handler (read flag/d0/d1, write the flag back) */
static void
n2gl_mcub_ack(bool log)
{
	uint32_t f = n2gl_rd(N2G_USRT_D2AF(0));
	if (n2gl_stop || f == 0 || f == 0xffffffffu) { return; }
	uint32_t d0 = n2gl_rd(N2G_USRT_D2A0(0)), d1 = n2gl_rd(N2G_USRT_D2A1(0));
	n2gl_wr(N2G_USRT_D2AF(0), f);
	n2gq_mcub_note(f, d0, d1);                                /* 0581: heartbeat / pipe log */
	uint32_t after = n2gl_rd(N2G_USRT_D2AF(0));
	if (log && n2gl_lines < 200) {
		n2gl_lines++;
		printf("NORD2 GNSS LAD %d: MCUB d2a flag=%#x d0=%#x d1=%#x%s%s%s -> acked (W1C), D2AF now %#x\n", n2gl_k, f, d0, d1,
		    (f & 8u) ? " RESET_DONE" : "", (f & 1u) ? " RAMCODE_READY" : "", (f & 4u) ? " MCU_ACTION_REQ" : "", after);
	}
}
/* one service pass: MCUB ack, live RX index, close the round on DMA done / NODAINTB and re-arm. Returns new bytes. */
static uint32_t
n2gl_rx_service(bool log)
{
	n2gl_mcub_ack(log);
	if (n2gl_stop) { return 0; }
	if (!n2gl_rx_armed) { if (!n2gl_rx_norearm) { n2gl_rx_arm(log); } return 0; }
	uint32_t got = 0;
	bool done = (n2gl_rd(N2G_DMA_INTSTA(N2GL_RXCH)) & N2G_DMA_BIT) != 0;
	bool nod = (n2gl_rd(N2G_USRT_STA(0)) & (1u << 1)) != 0;     /* NODAINTB: the DSP finished a burst */
	if (n2gl_stop) { return 0; }
	if (n2gl_rx_rlct_ok || done || nod) {
		uint32_t w = done ? n2gl_rx_len : n2gl_rx_written();
		if (w > n2gl_rx_cur) { got = w - n2gl_rx_cur; n2gl_rx_cur = w; n2gl_rx.wr = (n2gl_rx_at + w) % N2GL_RING; }
	}
	if (done || nod) {
		n2gl_dma_stop_q(N2GL_RXCH);
		if (nod) { n2gl_rmw_q(N2G_USRT_STA(0), 1u << 1, 1u << 1); }   /* gps_dl_hw_usrt_clear_nodata_irq (W1C) */
		if (log && n2gl_lines < 200) {
			n2gl_lines++;
			printf("NORD2 GNSS LAD %d: RX round closed by %s: %u bytes in this round (ring +%u..+%u)\n", n2gl_k,
			    done ? "DMA2 done" : "NODAINTB", n2gl_rx_cur, n2gl_rx_at, n2gl_rx.wr);
		}
		n2gl_rx_armed = false;
		if (!n2gl_rx_norearm) { n2gl_rx_arm(log); }
	}
	n2gl_rx_total += got;
	return got;
}
/* data link prep in vendor order: show the mailbox, ack RESET_DONE, claim EMI, fill + arm RX. done[25] = EMI claim. */
static int
n2gl_v9_prep(bool done[32])
{
	n2gl_show("USRT MONF", N2G_USRT_MONF(0)); n2gl_show("USRT INTEN", N2G_USRT_INTEN(0)); n2gl_show("USRT STA", N2G_USRT_STA(0));
	n2gl_show("USRT A2DF", N2G_USRT_A2DF(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0));
	n2gl_show("USRT D2A0", N2G_USRT_D2A0(0)); n2gl_show("USRT D2A1", N2G_USRT_D2A1(0));
	n2gl_lines = 0;
	n2gl_mcub_ack(true);                                     /* (a) RESET_DONE acknowledged before any write */
	n2gl_o.emictl = n2gl_rd(N2G_CFG_EMI_CTL); done[25] = true;
	if (!n2gl_field(N2G_CFG_EMI_CTL, 1u, 1u)) { return N2G_E_ARG; }   /* (b) EMI_REQ_GPS = 1 */
	for (uint32_t i = 0; i < N2GL_RING; i++) { N2R[N2GL_RING + i] = N2GL_FILL; }
	__builtin_arm_dsb(0xf);
	n2gl_rx.rd = n2gl_rx.wr = 0; n2gl_rx_total = 0; n2gl_rx_armed = false;
	if (!n2gl_rx_arm(true)) { return N2G_E_ARG; }            /* (d) RX armed before the first TX */
	n2gl_dma_regs("RX armed", N2GL_RXCH);
	n2gl_show("USRT STA", N2G_USRT_STA(0));
	return n2gl_stop ? N2G_E_ARG : 0;
}
/* watch for ms milliseconds: every change of STA / D2AF / DMA2 INTSTA / RLCT / MONF is printed, RX is serviced */
static void
n2gl_v9_watch(int ms, const char *phase)
{
	uint32_t ls = ~0u, lf = ~0u, li = ~0u, lr = ~0u, lm = ~0u;
	int lines = 0;
	for (int t = 0; t < ms && !n2gl_stop; t++) {
		uint32_t sta = n2gl_rd(N2G_USRT_STA(0)), f = n2gl_rd(N2G_USRT_D2AF(0)), is = n2gl_rd(N2G_DMA_INTSTA(N2GL_RXCH));
		uint32_t rl = n2gl_rd(N2G_DMA_RLCT(N2GL_RXCH)), m = n2gl_rd(N2G_USRT_MONF(0));
		if (n2gl_stop) { break; }
		if ((sta != ls || f != lf || is != li || rl != lr || m != lm) && lines < 60) {
			lines++;
			printf("NORD2 GNSS LAD %d: %s t=%dms sta=%#x%s%s d2af=%#x monf=%#x dma2 intsta=%#x rlct=%#x\n", n2gl_k, phase, t, sta,
			    (sta & 8u) ? " TX_IND(has data)" : "", (sta & 2u) ? " NODAINTB" : "", f, m, is, rl);
			ls = sta; lf = f; li = is; lr = rl; lm = m;
		}
		uint32_t g = n2gl_rx_service(true);
		if (g && n2gl_lines < 200) { n2gl_lines++; printf("NORD2 GNSS LAD %d: %s t=%dms +%u bytes (total %u)\n", n2gl_k, phase, t, g, n2gl_rx_total); }
		delay(1000);
	}
}
/* what the RX ring holds: bytes that differ from the fill, and a hex dump */
static uint32_t
n2gl_v9_report(const char *phase)
{
	uint32_t first = N2GL_RING, last = 0, cnt = 0;
	for (uint32_t i = 0; i < N2GL_RING; i++) {
		if (N2R[N2GL_RING + i] != N2GL_FILL) { cnt++; if (first == N2GL_RING) { first = i; } last = i; }
	}
	printf("NORD2 GNSS LAD %d: %s: RX total %u bytes by index (wr=%u armed=%u rlct_ok=%u), ring bytes != 0xa5: %u (first +%u last +%u)\n",
	    n2gl_k, phase, n2gl_rx_total, n2gl_rx.wr, n2gl_rx_armed, n2gl_rx_rlct_ok, cnt, cnt ? first : 0, cnt ? last : 0);
	uint32_t end = cnt ? last + 1u : 0;
	if (n2gl_rx.wr > end) { end = n2gl_rx.wr; }
	if (end) { n2gl_hexdump("RX", 0, end < 512u ? end : 512u); }
	n2gl_show("USRT STA", N2G_USRT_STA(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0)); n2gl_show("USRT MONF", N2G_USRT_MONF(0));
	n2gl_dma_regs(phase, N2GL_RXCH);
	return cnt > n2gl_rx_total ? cnt : n2gl_rx_total;
}
/* K = 23: frame 1 with RX already armed, 2 s capture; if silent, frame 1 once more and 1 s capture */
static int
n2gl_v9_capture(void)
{
	printf("NORD2 GNSS LAD %d: V9 TX libmnl frame 1 (15 bytes, RX armed before TX): aa f0 09 00 05 fe 32 00 00 01 00 3f 01 aa 0f\n", n2gl_k);
	int e = n2gl_tx(n2gl_frame1, sizeof n2gl_frame1, true);
	if (e) { return e; }
	n2gl_show("USRT STA after TX", N2G_USRT_STA(0));
	n2gl_v9_watch(2000, "after frame 1");
	uint32_t n = n2gl_v9_report("after frame 1 (2 s)");
	if (!n && !n2gl_stop) {
		printf("NORD2 GNSS LAD %d: V9 silent; frame 1 again, 1 s capture\n", n2gl_k);
		e = n2gl_tx(n2gl_frame1, sizeof n2gl_frame1, true);
		if (e) { return e; }
		n2gl_v9_watch(1000, "after frame 1 retry");
		n = n2gl_v9_report("after frame 1 retry (1 s)");
	}
	printf("NORD2 GNSS LAD %d: V9 RESULT: DSP reply %u bytes\n", n2gl_k, n);
	return n2gl_stop ? N2G_E_ARG : 0;
}

/*
 * 0363 (q10jr10): split ladders after gl9b (q10jr9, K=24 HOLD + boot replay) reset the phone 2..4 s after the HOLD, before any
 * N2GRPL frame line: the first header TX / first reply was the new element. Candidates: (B) the first D2A DMA write into
 * DRAM 0xDFE02000 (never done before; EMI MPU / bus), (C) the DSP's first "has data" with INTEN on -> a GPS IRQ storm,
 * (A) GPS-bus accesses after the HOLD released the GPS wake vote. Every S10 ladder keeps the vote (except S10e on purpose),
 * pauses 2 s with an "about to ..." line before each risky action, arms RX for 64 bytes at most, never re-arms, and prints the
 * GIC enable/pending words of the GPS INTIDs (415 = ISENABLER12/ISPENDR12 bit 31; 416..421 = ISENABLER13/ISPENDR13 bits 0..5).
 *   K=25 S10a: arm DMA2 (16 words), 3 s watch, disarm               -> arming alone
 *   K=26 S10b: arm DMA2 (16 words), TX boot header, 1 s, disarm     -> first DMA write of a reply
 *   K=27 S10c: TX boot header with DMA2 idle, INTEN off, 1 s watch; COMD4 x3, header again, 1 s -> does the ROM answer at all
 *   K=28 S10d: as S10c with INTEN = TXIEN|NODAIEN                   -> IRQ hypothesis
 *   K=29 S10e: arm DMA2 (16 words), release the GPS wake vote, 3 s without any access, vote back, disarm -> HOLD hypothesis
 */
static const uint8_t n2gl_bhdr[116] = { 0xaa, 0xf0, 0x6e, 0x00, 0x08, 0xfe, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x88, 0x00, 0x74, 0x04, 0x74, 0x7b, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x93, 0x68, 0x00, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x4e, 0x4f, 0xbf, 0x16, 0xaa, 0x0f };    /* libmnl _BootDSP_UART_V2 header 0xFE08 (n2mnld/runs/bootdsp_replay.json seq 1) */
static const uint8_t n2gl_comd4[12] = { 0xaa, 0xf0, 0x06, 0x00, 0x05, 0xfe, 0x04, 0x00, 0x0d, 0x01, 0xaa, 0x0f };
static void
n2gl_pause(const char *what)
{
	printf("NORD2 GNSS LAD %d: about to %s\n", n2gl_k, what);
	delay(2000000);
}
static void
n2gl_gic(const char *phase)
{
	printf("NORD2 GNSS LAD %d: %s GIC isenabler12=%#x isenabler13=%#x ispendr12=%#x ispendr13=%#x (GPS: 415 = 12[31], 416..421 = 13[0..5])\n",
	    n2gl_k, phase, n2gd_gicd_rd(0x130), n2gd_gicd_rd(0x134), n2gd_gicd_rd(0x230), n2gd_gicd_rd(0x234));
}
/* watch ms milliseconds, print every change (USRT STA/D2AF/MONF, DMA2 START/INTSTA/RLCT, GPS IRQ pending bits) */
static void
n2gl_w10(int ms, const char *phase)
{
	uint32_t l[7] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };
	int lines = 0;
	for (int t = 0; t < ms && !n2gl_stop; t++) {
		uint32_t v[7] = { n2gl_rd(N2G_USRT_STA(0)), n2gl_rd(N2G_USRT_D2AF(0)), n2gl_rd(N2G_USRT_MONF(0)), n2gl_rd(N2G_DMA_START(N2GL_RXCH)),
		    n2gl_rd(N2G_DMA_INTSTA(N2GL_RXCH)), n2gl_rd(N2G_DMA_RLCT(N2GL_RXCH)), (n2gd_gicd_rd(0x230) >> 31) | ((n2gd_gicd_rd(0x234) & 0x3fu) << 1) };
		if (n2gl_stop) { break; }
		bool ch = false;
		for (int i = 0; i < 7; i++) { if (v[i] != l[i]) { ch = true; } l[i] = v[i]; }
		if (ch && lines < 40) {
			lines++;
			printf("NORD2 GNSS LAD %d: %s t=%dms sta=%#x%s%s d2af=%#x monf=%#x dma2 start=%#x intsta=%#x rlct=%#x gps_irq_pend(415..421)=%#x\n", n2gl_k, phase, t,
			    v[0], (v[0] & 8u) ? " TX_IND(has data)" : "", (v[0] & 2u) ? " NODAINTB" : "", v[1], v[2], v[3], v[4], v[5], v[6]);
		}
		if (n2gl_rx_armed) { uint32_t g = n2gl_rx_service(true); if (g) { printf("NORD2 GNSS LAD %d: %s t=%dms +%u bytes\n", n2gl_k, phase, t, g); } }
		else { n2gl_mcub_ack(true); }
		delay(1000);
	}
}
static void
n2gl_s10_disarm(void)
{
	if (!n2gl_rx_armed && !n2gl_rx_started) { return; }
	n2gl_pause("DISARM RX DMA2 (STR = 0, ACKINT)");
	uint32_t w = n2gl_rx_armed ? n2gl_rx_written() : 0;
	n2gl_dma_stop(N2GL_RXCH);
	if (n2gl_rx_armed && w > n2gl_rx_cur) { n2gl_rx_total += w - n2gl_rx_cur; n2gl_rx.wr = (n2gl_rx_at + w) % N2GL_RING; }
	n2gl_rx_armed = false;
	printf("NORD2 GNSS LAD %d: RX DMA2 disarmed\n", n2gl_k);
}
static int
n2gl_s10_tx(const uint8_t *b, uint32_t n, const char *what)
{
	n2gl_pause(what);
	int e = n2gl_tx(b, n, true);
	printf("NORD2 GNSS LAD %d: TX %s (%u bytes) %s\n", n2gl_k, what, n, e ? "FAILED" : "done");
	return e;
}
/* S10 prep: MCUB RESET_DONE ack + EMI_REQ_GPS (as V9), RX ring filled with 0xA5; RX arming is per ladder */
static int
n2gl_s10(int k, bool done[32])
{
	n2gl_rx_cap = 64; n2gl_rx_norearm = true; n2gl_rx_armed = false; n2gl_rx_total = 0; n2gl_lines = 0;
	n2gl_gic("start");
	n2gl_show("USRT MONF", N2G_USRT_MONF(0)); n2gl_show("USRT INTEN", N2G_USRT_INTEN(0)); n2gl_show("USRT STA", N2G_USRT_STA(0));
	n2gl_show("USRT D2AF", N2G_USRT_D2AF(0));
	n2gl_pause("ack MCUB D2AF (W1C) and set EMI_REQ_GPS 0x18001c1c[0]");
	n2gl_mcub_ack(true);
	n2gl_o.emictl = n2gl_rd(N2G_CFG_EMI_CTL); done[25] = true;
	if (!n2gl_field(N2G_CFG_EMI_CTL, 1u, 1u)) { return N2G_E_ARG; }
	for (uint32_t i = 0; i < N2GL_RING; i++) { N2R[N2GL_RING + i] = N2GL_FILL; }
	__builtin_arm_dsb(0xf);
	n2gl_rx.rd = n2gl_rx.wr = 0;
	int e = 0;
	if (k == 25 || k == 26 || k == 29) {
		n2gl_pause("ARM RX DMA2 for 16 words at bus 0x78002000 (phys 0xDFE02000)");
		if (!n2gl_rx_arm(true)) { return N2G_E_ARG; }
		n2gl_dma_regs("armed", N2GL_RXCH); n2gl_gic("armed");
	}
	if (k == 25) { n2gl_w10(3000, "S10a armed, no TX"); }
	if (k == 26) {
		e = n2gl_s10_tx(n2gl_bhdr, sizeof n2gl_bhdr, "TX boot header 0xFE08 with RX DMA2 armed (16 words)");
		if (!e) { n2gl_w10(1000, "S10b after header"); }
	}
	if (k == 27 || k == 28) {
		e = n2gl_s10_tx(n2gl_bhdr, sizeof n2gl_bhdr, "TX boot header 0xFE08, RX DMA2 idle");
		if (!e) { n2gl_w10(1000, "S10c/d after header"); n2gl_gic("after header"); }
		for (int i = 0; i < 3 && !e && !n2gl_stop; i++) { e = n2gl_s10_tx(n2gl_comd4, sizeof n2gl_comd4, "TX COMD4 0xFE05"); }
		if (!e) { n2gl_w10(500, "S10c/d after COMD4 x3"); }
		if (!e) { e = n2gl_s10_tx(n2gl_bhdr, sizeof n2gl_bhdr, "TX boot header 0xFE08 again"); }
		if (!e) { n2gl_w10(1000, "S10c/d after header 2"); n2gl_gic("after header 2"); }
	}
	if (k == 29) {
		n2gl_pause("RELEASE the GPS wake vote 0x180601ac[0] and wait 3 s WITHOUT any register access");
		n2gl_field(N2G_CSR_WAKEPU_GPS, 1u, 0);
		delay(3000000);
		printf("NORD2 GNSS LAD %d: 3 s idle done, taking the vote back\n", k);
		n2gl_field(N2G_CSR_WAKEPU_GPS, 1u, 1u);
		n2gl_poll(N2G_CSR_SLP_PROT_CTRL, 1u << 5, 0, 10000, "on2off slp_prot ack clear");
		n2gl_w10(200, "S10e after vote back");
	}
	n2gl_s10_disarm();
	n2gl_v9_report("S10 end");
	printf("NORD2 GNSS LAD %d: S10 RESULT: %s, RX %u bytes, e=%d\n", k, n2gl_name[k], n2gl_rx_total, e);
	n2gl_rx_cap = 0; n2gl_rx_norearm = false;
	return n2gl_stop ? N2G_E_ARG : e;
}

/*
 * 0364 (q10jr11): the whole libmnl DSP code download inside one ladder ioctl ("S11", K = 30 boot + rollback, K = 31 boot +
 * HOLD). q10jr10 gl10b proved RX DMA into 0xDFE02000 and the ROM's header ack (aa f0 0e 00 31 fe 00 00 13 19 ... 69 01 aa 0f).
 * The frames (libmnl _BootDSP_UART_V2 replay: [0] header 0xFE08, [1] COMD4, [2..51] fragments 0xFE0C seq 1..50, [52] the
 * post-boot command 0xFE05) are NOT in the image (70.7 KB > boot spare): user space pushes them with N2GD_IOC_CHUNK into BSS
 * and the kernel checks markers, ids, sequence numbers and a sum before any hardware access. Polled, INTEN off, RX DMA2
 * armed per exchange (64 bytes, never re-armed), GPS wake vote held for the whole run (also in the K = 31 HOLD).
 */
struct n2gb_chunk { uint32_t which, off, len, sum; uint8_t data[4096]; };   /* which 0 = data at off, 1 = table */
#define N2GD_IOC_CHUNK    _IOW('G', 180, struct n2gb_chunk)
#define N2GB_MAX          (80u * 1024u)
#define N2GB_NF           53
static uint8_t n2gb_data[N2GB_MAX];
static uint32_t n2gb_tab[N2GB_NF][2], n2gb_total, n2gb_sum, n2gb_have;
static int
n2gb_chunk_in(const struct n2gb_chunk *c)
{
	if (c->len > sizeof c->data) { return EINVAL; }
	if (c->which == 0) {
		if (c->off > N2GB_MAX || c->len > N2GB_MAX - c->off) { return EINVAL; }
		memcpy(n2gb_data + c->off, c->data, c->len);
		if (c->off + c->len > n2gb_have) { n2gb_have = c->off + c->len; }
		return 0;
	}
	if (c->which == 1) {                                     /* data = u32 nframes, total, sum, then nframes x (off, len) */
		uint32_t h[3 + 2 * N2GB_NF];
		if (c->len != sizeof h) { return EINVAL; }
		memcpy(h, c->data, sizeof h);
		if (h[0] != N2GB_NF || h[1] > N2GB_MAX) { return EINVAL; }
		n2gb_total = h[1]; n2gb_sum = h[2];
		for (int i = 0; i < N2GB_NF; i++) { n2gb_tab[i][0] = h[3 + 2 * i]; n2gb_tab[i][1] = h[4 + 2 * i]; }
		return 0;
	}
	return EINVAL;
}
static uint16_t n2gb_id(int i) { const uint8_t *f = n2gb_data + n2gb_tab[i][0]; return (uint16_t)(f[4] | (f[5] << 8)); }
static uint16_t n2gb_seq(int i) { const uint8_t *f = n2gb_data + n2gb_tab[i][0]; return (uint16_t)(f[6] | (f[7] << 8)); }
/* the uploaded frames are what the replay says: sum, AA F0 .. AA 0F, ids, fragment seq 1..50, sizes that fit the TX ring */
static bool
n2gb_valid(void)
{
	uint32_t s = 0;
	if (!n2gb_total || n2gb_have < n2gb_total) { printf("NORD2 GNSS S11: blob incomplete (%u of %u bytes)\n", n2gb_have, n2gb_total); return false; }
	for (uint32_t i = 0; i < n2gb_total; i++) { s = s * 31u + n2gb_data[i]; }
	if (s != n2gb_sum) { printf("NORD2 GNSS S11: blob sum %#x != %#x\n", s, n2gb_sum); return false; }
	for (int i = 0; i < N2GB_NF; i++) {
		uint32_t o = n2gb_tab[i][0], n = n2gb_tab[i][1];
		const uint8_t *f = n2gb_data + o;
		if (o > n2gb_total || n < 10u || n > n2gb_total - o || n > N2GL_RING - 8u || f[0] != 0xaa || f[1] != 0xf0 || f[n - 2] != 0xaa || f[n - 1] != 0x0f) {
			printf("NORD2 GNSS S11: frame %d bad (off %u len %u)\n", i, o, n); return false;
		}
		uint16_t want = i == 0 ? 0xfe08 : (i == 1 || i == 52) ? 0xfe05 : 0xfe0c;
		if (n2gb_id(i) != want || (i >= 2 && i <= 51 && n2gb_seq(i) != (uint16_t)(i - 1))) {
			printf("NORD2 GNSS S11: frame %d id %#x seq %u unexpected\n", i, n2gb_id(i), n2gb_seq(i)); return false;
		}
	}
	return true;
}
/* RX frame parser over the ring (consumes n2gl_rx.rd .. wr): AA F0 <stuffed len id payload cks> AA 0F, AA = DE DF, DE = DE E0 */
static uint8_t n2gp_buf[512];
static int n2gp_n = -1, n2gp_prev = -1;
static uint32_t n2gp_frames, n2gp_id, n2gp_seq, n2gp_bad;
static void
n2gp_done(void)
{
	uint8_t u[512]; int n = 0;
	for (int i = 0; i < n2gp_n; i++) {
		if (n2gp_buf[i] == 0xde && i + 1 < n2gp_n) { u[n++] = n2gp_buf[i + 1] == 0xdf ? 0xaa : 0xde; i++; } else { u[n++] = n2gp_buf[i]; }
	}
	if (n < 6) { return; }
	uint32_t sum = 0;
	for (int i = 0; i < n - 2; i++) { sum += u[i]; }
	bool ok = (sum & 0xffffu) == (uint32_t)(u[n - 2] | (u[n - 1] << 8));
	n2gp_frames++;
	if (!ok) { n2gp_bad++; }
	n2gp_id = (uint32_t)(u[2] | (u[3] << 8)); n2gp_seq = n >= 8 ? (uint32_t)(u[4] | (u[5] << 8)) : 0xffffu;
	if (n2gl_lines < 200) {
		char line[3 * 16 + 1]; int p = 0;
		for (int i = 4; i < n - 2 && i < 4 + 16; i++) { static const char hx[] = "0123456789abcdef"; line[p++] = hx[u[i] >> 4]; line[p++] = hx[u[i] & 15]; line[p++] = ' '; }
		line[p] = 0;
		n2gl_lines++;
		printf("NORD2 GNSS LAD %d: DSP frame #%u id=%#x len=%#x cks=%s payload: %s\n", n2gl_k, n2gp_frames, n2gp_id, (uint32_t)(u[0] | (u[1] << 8)), ok ? "ok" : "BAD", line);
	}
	if (!ok) { n2gp_id = 0; }                                /* a bad checksum never counts as an ack */
}
static void
n2gp_feed(void)
{
	while (n2gl_rx.rd != n2gl_rx.wr) {
		volatile uint8_t *pb = &N2R[N2GL_RING + n2gl_rx.rd];
		int c = *pb;
		*pb = N2GL_FILL;
		n2gl_rx.rd = (n2gl_rx.rd + 1u) % N2GL_RING;
		if (n2gp_prev == 0xaa && c == 0xf0) { n2gp_n = 0; }
		else if (n2gp_prev == 0xaa && c == 0x0f && n2gp_n >= 0) { n2gp_n--; n2gp_done(); n2gp_n = -1; }
		else if (n2gp_n >= 0) { if (n2gp_n < (int)sizeof n2gp_buf) { n2gp_buf[n2gp_n++] = (uint8_t)c; } else { n2gp_n = -1; } }
		n2gp_prev = c;
	}
}
/* close the current RX arming (if any), keep what it wrote */
static void
n2gb_rx_close(void)
{
	if (!n2gl_rx_armed) { return; }
	uint32_t w = n2gl_rx_written();
	if (w > n2gl_rx_cur) { n2gl_rx_total += w - n2gl_rx_cur; n2gl_rx_cur = w; n2gl_rx.wr = (n2gl_rx_at + w) % N2GL_RING; }
	n2gl_dma_stop_q(N2GL_RXCH);
	n2gl_rx_armed = false;
	n2gp_feed();
}
/* send frame i with RX armed first; wait ms for frame id (seq if >= 0); true on the ack */
static bool
n2gb_xchg(int i, uint32_t id, int seq, int ms, int *tx_err)
{
	n2gb_rx_close();
	n2gl_rx_cap = 64; n2gl_rx_norearm = true;
	if (!n2gl_rx_arm(false)) { *tx_err = N2G_E_ARG; return false; }
	uint32_t before = n2gp_frames;
	*tx_err = n2gl_tx(n2gb_data + n2gb_tab[i][0], n2gb_tab[i][1], false);
	if (*tx_err) { printf("NORD2 GNSS LAD %d: TX frame %d FAILED (%d)\n", n2gl_k, i, *tx_err); return false; }
	for (int t = 0; t < ms && !n2gl_stop; t++) {
		n2gl_rx_service(false);
		n2gp_feed();
		if (n2gp_frames != before && n2gp_id == id && (seq < 0 || n2gp_seq == (uint32_t)seq)) { n2gb_rx_close(); return true; }
		if (!n2gl_rx_armed) { n2gl_rx_arm(false); }          /* the round closed (NODAINTB) without our ack: listen again */
		delay(1000);
	}
	n2gb_rx_close();
	return false;
}
/*
 * 0587 (gnss-20261114): K = 33 "S13 pacing probe". Every hang (hang3/4/5) came 10..20 ms after a frame reached the DSP from the
 * HOLD pipe, where libmnl answers through n2mnld's 10 ms read poll; the same frames inside the kernel S11 loop (next frame ~1 ms
 * after the ack) never hung (53 TX). hang5: header, 5 ms, fragment 1 (acked, 12 B), then death ~10 ms into the wait for
 * fragment 2. Hypothesis H1: the DSP ROM loader misbehaves when the host is slow between fragments. K = 33 = K = 31 with ONE
 * change: after fragment 1 is acked the kernel stays idle for 200 ms (RX armed for 64 B, no TX) and samples the GPS/conn_infra
 * state every 1 ms into the 0583 trace (console line on every change, max 60), then finishes the download and HOLDs like K = 31.
 * Hang inside the gap = H1 proven with the pre-death state in the trace; no hang = idle after fragment 1 is harmless and the
 * killer is specific to the userland path.
 */
#ifndef N2G_CSR_BUS_TIMEOUT_IRQ
#define N2G_CSR_BUS_TIMEOUT_IRQ 0x180602d4u                     /* 0587: conn_infra host CSR, [0] == 1 = no off-domain bus hang */
#endif
static int n2gb_gap_ms;
static void n2gq_gic_mask(void);                                /* 0589: defined with the pipe */
static uint32_t n2gq_mode;                                      /* 0588: K=34..36 HOLD variant bits (1 = no LNA, 2 = S11-style RX) */
static void
n2gb_gap_watch(int ms)
{
	static const uint32_t pa[9] = { N2G_CSR_GPS_CFG2HOST_DBG, N2G_CSR_SLP_PROT_CTRL, N2G_CSR_BUS_TIMEOUT_IRQ, 0x18c01020u /* L1 SLP_PWR_CTL */,
	    N2G_GPS_AON_DSLEEP_CTL, N2G_USRT_STA(0), N2G_USRT_D2AF(0), N2G_USRT_MONF(0), N2G_DMA_RLCT(N2GL_RXCH) };
	uint32_t last[9]; int lines = 0;
	for (int j = 0; j < 9; j++) { last[j] = 0xdeadbeefu; }
	n2gl_rx_cap = 64; n2gl_rx_norearm = true;
	if (!n2gl_rx_arm(false)) { return; }
	printf("NORD2 GNSS LAD %d: S13 GAP START: fragment 1 acked, %d ms idle (no TX), state sampled every 1 ms\n", n2gl_k, ms);
	n2gt("S13 gap start %d ms", ms);
	int t;
	for (t = 0; t < ms && !n2gl_stop; t++) {
		uint32_t v[9];
		for (int j = 0; j < 9; j++) { v[j] = n2gl_rd(pa[j]); }
		if (n2gl_stop) { break; }
		bool ch = false;
		for (int j = 0; j < 9; j++) { if (v[j] != last[j]) { ch = true; } }
		if (ch && lines < 60) {
			lines++;
			printf("NORD2 GNSS LAD %d: S13 t=%dms top=%#x slp=%#x bto=%#x l1slp=%#x aon=%#x sta=%#x d2af=%#x monf=%#x rlct=%#x\n", n2gl_k, t,
			    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
		}
		for (int j = 0; j < 9; j++) { last[j] = v[j]; }
		n2gl_rx_service(true);
		n2gp_feed();
		if (!n2gl_rx_armed) { n2gl_rx_arm(false); }
		delay(1000);
	}
	n2gb_rx_close();
	printf("NORD2 GNSS LAD %d: S13 GAP END after %d ms: phone alive, %u DSP frames parsed so far; continuing the download\n", n2gl_k, t, n2gp_frames);
	n2gt("S13 gap end %d ms", t);
}
/* K = 30 / 31: header (ack 0xFE31 seq 0; COMD4 x3 + header up to 3 rounds), 50 fragments (ack 0xFE32 seq k, 2 resends),
 * the post-boot command, 3 s capture with RX re-armed (256 bytes per arming). Returns 0 on a complete boot. */
static int
n2gb_boot(bool done[32])
{
	int e = 0;
	if (!n2gb_valid()) { return N2G_E_ARG; }
	printf("NORD2 GNSS LAD %d: S11 blob ok: %u bytes, %d frames (header %u B, fragments %u..%u B)\n", n2gl_k, n2gb_total, N2GB_NF,
	    n2gb_tab[0][1], n2gb_tab[51][1], n2gb_tab[2][1]);
	n2gl_rx_cap = 64; n2gl_rx_norearm = true; n2gl_rx_armed = false; n2gl_rx_total = 0; n2gl_lines = 0;
	n2gp_n = -1; n2gp_prev = -1; n2gp_frames = n2gp_bad = 0;
	n2gl_gic("start");
	n2gl_show("USRT INTEN", N2G_USRT_INTEN(0)); n2gl_show("USRT STA", N2G_USRT_STA(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0));
	n2gl_mcub_ack(true);
	n2gl_o.emictl = n2gl_rd(N2G_CFG_EMI_CTL); done[25] = true;
	if (!n2gl_field(N2G_CFG_EMI_CTL, 1u, 1u)) { return N2G_E_ARG; }
	for (uint32_t i = 0; i < N2GL_RING; i++) { N2R[N2GL_RING + i] = N2GL_FILL; }
	__builtin_arm_dsb(0xf);
	n2gl_rx.rd = n2gl_rx.wr = 0;
	n2gl_pause("run the DSP code download (header, 50 fragments, post-boot command), continuous from here");
	bool hdr = n2gb_xchg(0, 0xfe31, 0, 300, &e);
	for (int r = 0; r < 3 && !hdr && !e && !n2gl_stop; r++) {
		int x = 0;
		for (int j = 0; j < 3 && !x; j++) { x = n2gl_tx(n2gb_data + n2gb_tab[1][0], n2gb_tab[1][1], false); }
		delay(500000);
		hdr = n2gb_xchg(0, 0xfe31, 0, 1000, &e);
		printf("NORD2 GNSS LAD %d: S11 header round %d (after COMD4 x3): %s\n", n2gl_k, r + 1, hdr ? "ACK" : "no ack");
	}
	if (n2gl_stop) { return N2G_E_ARG; }
	if (!hdr) { printf("NORD2 GNSS LAD %d: S11 RESULT: no header ack 0xFE31 (e=%d)\n", n2gl_k, e); return e ? e : 9; }
	printf("NORD2 GNSS LAD %d: S11 header ACK 0xFE31 seq 0 (DSP ver/info above); sending 50 fragments\n", n2gl_k);
	int acked = 0;
	for (int k = 1; k <= 50 && !e && !n2gl_stop; k++) {
		bool ok = false; int t;
		for (t = 0; t < 3 && !ok && !e && !n2gl_stop; t++) { ok = n2gb_xchg(1 + k, 0xfe32, k, 300, &e); }
		if (ok) { acked++; }
		if (n2gl_lines < 200 && (!ok || t > 1 || k == 1 || k == 50 || k % 10 == 0)) {
			n2gl_lines++;
			printf("NORD2 GNSS LAD %d: S11 fragment %d/50 %s after %d send(s)\n", n2gl_k, k, ok ? "ACK 0xFE32" : "NO ACK", t);
		}
		if (!ok) { break; }
		if (k == 1 && n2gb_gap_ms > 0) { n2gb_gap_watch(n2gb_gap_ms); }      /* 0587: K = 33 pacing probe */
	}
	if (n2gl_stop) { return N2G_E_ARG; }
	printf("NORD2 GNSS LAD %d: S11 fragments acked %d/50 (frames parsed %u, bad checksum %u)\n", n2gl_k, acked, n2gp_frames, n2gp_bad);
	if (acked != 50) { printf("NORD2 GNSS LAD %d: S11 RESULT: download stopped at fragment %d\n", n2gl_k, acked + 1); return e ? e : 9; }
	/* post-boot: MCUB RAMCODE_READY [0] is the vendor's "DSP RAM code running" (gps_dsp_fsm -> WORKING) */
	n2gl_rx_cap = 256; n2gl_rx_norearm = false;
	n2gb_rx_close();
	n2gl_rx_arm(true);
	uint32_t f0 = n2gp_frames, r0 = n2gl_rx_total;
	e = n2gl_tx(n2gb_data + n2gb_tab[52][0], n2gb_tab[52][1], false);
	printf("NORD2 GNSS LAD %d: S11 post-boot command 0xFE05 (old frame 1) sent (%d); 3 s capture\n", n2gl_k, e);
	uint32_t rd0 = n2gl_rx.rd;
	for (int t = 0; t < 3000 && !e && !n2gl_stop; t++) {
		n2gl_rx_service(true);
		if (t == 2999 || n2gl_rx_used() >= 256u) {
			uint32_t u = n2gl_rx_used();
			if (u && rd0 == n2gl_rx.rd) { n2gl_hexdump("post-boot RX", n2gl_rx.rd, u < 256u ? u : 256u); rd0 = ~0u; }
			n2gp_feed();
		}
		delay(1000);
	}
	n2gb_rx_close();
	printf("NORD2 GNSS LAD %d: S11 RESULT: DSP booted (50/50 fragments acked); after the post-boot command %u bytes, %u frames; D2AF %#x\n",
	    n2gl_k, n2gl_rx_total - r0, n2gp_frames - f0, n2gl_rd(N2G_USRT_D2AF(0)));
	return n2gl_stop ? N2G_E_ARG : e;
}

static int
n2gl_do(int s, bool done[32])
{
	switch (s) {
	case 1:
		n2gl_o.vote = n2gl_rd(N2G_CSR_WAKEPU_GPS); done[1] = true;
		if (!n2gl_field(N2G_CSR_WAKEPU_GPS, 1u, 1u)) { return N2G_E_ARG; }
		if (!n2gl_poll(N2G_CSR_SLP_PROT_CTRL, 1u << 5, 0, 10000, "on2off slp_prot ack clear")) { return N2G_E_WAKE; }
		n2gl_show("vote readback", N2G_CSR_WAKEPU_GPS);
		return 0;
	case 2:
		n2gl_show("conn_infra version", N2G_CONN_HW_VER); n2gl_show("GPS_PWRCTRL0", N2G_CFG_GPS_PWRCTRL0);
		n2gl_show("EMI_CTL", N2G_CFG_EMI_CTL); n2gl_show("GALS_CONN2GPS", N2G_CFG_GALS_CONN2GPS);
		n2gl_show("GALS_GPS2CONN", N2G_CFG_GALS_GPS2CONN); n2gl_show("ADIE_CTL", N2G_CFG_ADIE_CTL);
		return 0;
	case 3:
		n2gl_show("BGFSYS_PWR_CTL", N2G_RGU_BGFSYS_PWR_CTL); n2gl_show("BGFSYS_PWR_ACK", N2G_RGU_BGFSYS_PWR_ACK);
		return 0;
	case 4:
		n2gl_show("WTSLP_WB_STA", N2G_WTSLP_WB_STA); n2gl_show("CONN2AP_GPS_EMI", N2G_CSR_CONN2AP_GPS_EMI);
		n2gl_show("HOST2GPS_DBG_SEL", N2G_CSR_HOST2GPS_DBG_SEL); n2gl_show("GPS_CFG2HOST_DBG", N2G_CSR_GPS_CFG2HOST_DBG);
		return 0;
	case 5:
		n2gl_o.emi = n2gl_rd(N2G_CSR_CONN2AP_GPS_EMI); done[5] = true;
		return n2gl_field(N2G_CSR_CONN2AP_GPS_EMI, 0x000fffffu, n2gl_hw.emi_base_pa >> 16) ? 0 : N2G_E_ARG;
	case 6:
		n2gl_o.bgf = n2gl_rd(N2G_RGU_BGFSYS_PWR_CTL); done[6] = true;
		if (!n2gl_field(N2G_RGU_BGFSYS_PWR_CTL, 0xffff0080u, N2G_RGU_BGFSYS_PWR_KEY | (1u << 7))) { return N2G_E_ARG; }
		n2gl_show("BGFSYS_PWR_ACK", N2G_RGU_BGFSYS_PWR_ACK);
		return 0;
	case 7:
		n2gl_o.func = n2gl_rd(N2G_CFG_GPS_PWRCTRL0); done[7] = true;
		if (!n2gl_field(N2G_CFG_GPS_PWRCTRL0, 1u, 1u)) { return N2G_E_ARG; }
		if (!n2gl_poll(N2G_RGU_BGFSYS_PWR_ACK, 1u << 24, 1u << 24, N2G_POLL_MAX, "BGF primary ack")) { return N2G_E_BGF_ACK1; }
		if (!n2gl_poll(N2G_RGU_BGFSYS_PWR_ACK, 1u << 25, 1u << 25, N2G_POLL_MAX, "BGF secondary ack")) { return N2G_E_BGF_ACK2; }
		return 0;
	case 8:
		delay(200);
		n2gl_o.c2g = n2gl_rd(N2G_CFG_GALS_CONN2GPS); n2gl_o.g2c = n2gl_rd(N2G_CFG_GALS_GPS2CONN); done[8] = true;
		for (int i = 0; i < 4; i++) {
			if (!n2gl_field(n2gl_slp_off[i].pa, n2gl_slp_off[i].en, 0)) { return N2G_E_ARG; }
			if (!n2gl_poll(n2gl_slp_off[i].pa, n2gl_slp_off[i].rdy, 0, N2G_POLL_MAX, "sleep protection RDY clear")) { return N2G_E_SLP_PROT; }
		}
		return 0;
	case 9: {
		n2gl_o.sel = n2gl_rd(N2G_CSR_HOST2GPS_DBG_SEL); done[9] = true;
		if (!n2gl_field(N2G_CSR_HOST2GPS_DBG_SEL, 0xffu, 0x80u)) { return N2G_E_ARG; }
		bool top = false;
		for (int i = 0; i < 3 && !top && !n2gl_stop; i++) { top = n2gl_poll(N2G_CSR_GPS_CFG2HOST_DBG, 0xffffu, 2u, N2G_POLL_MAX, "gps_top_off ACTIVE"); }
		return top ? 0 : N2G_E_GPS_TOP;
	}
	case 10:
		n2gl_o.adie = n2gl_rd(N2G_CFG_ADIE_CTL); done[10] = true;
		if (!n2gl_field(N2G_CFG_ADIE_CTL, 1u << 1, 1u << 1)) { return N2G_E_ARG; }
		return n2gl_poll(N2G_WTSLP_WB_STA, 1u << 26, 0, N2G_POLL_MAX, "A-die WB_STA idle") ? 0 : N2G_E_ADIE_CLK;
	case 11:
		n2gl_show("GPS CLKGEN1", N2G_GPS_CLKGEN1_CTL); n2gl_show("GPS AON DSLEEP", N2G_GPS_AON_DSLEEP_CTL);
		n2gl_show("USRT CTRL", N2G_USRT_CTRL(0)); n2gl_show("USRT INTEN", N2G_USRT_INTEN(0)); n2gl_show("USRT STA", N2G_USRT_STA(0));
		n2gl_show("USRT A2DF", N2G_USRT_A2DF(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0)); n2gl_show("USRT A2D1", N2G_USRT_A2D1(0));
		return 0;
	case 12:
		n2gl_o.clk1 = n2gl_rd(N2G_GPS_CLKGEN1_CTL); done[12] = true;
		return n2gl_field(N2G_GPS_CLKGEN1_CTL, 1u << 4, 1u << 4) ? 0 : N2G_E_ARG;
	case 13:
		/* gps_dl_hw_gps_pwr_stat_ctrl(GPS_L1_DSP_ON): PWR_STAT = 0, FORCE_OSC_EN_ON = 0, DIS_HW_RST_CNT = 1 (and it stays 1;
		 * q10jr6 pulsed it back to 0, which is the OFF value) */
		n2gl_o.aon = n2gl_rd(N2G_GPS_AON_DSLEEP_CTL); done[13] = true;
		if (!n2gl_field(N2G_GPS_AON_DSLEEP_CTL, 0x3u, 0) || !n2gl_field(N2G_GPS_AON_DSLEEP_CTL, 1u << 3, 0) ||
		    !n2gl_field(N2G_GPS_AON_DSLEEP_CTL, 1u << 4, 1u << 4)) { return N2G_E_ARG; }
		return 0;
	case 14:
		n2gl_show("GPS RGU_ON L1_CR", N2G_GPS_RGU_L1_CR); n2gl_show("GPS RGU_ON L1_DLY_CHAIN", N2G_GPS_RGU_L1_DLY_CHAIN);
		n2gl_show("GPS RGU_ON L1_MEM_DLY", N2G_GPS_RGU_L1_MEM_DLY); n2gl_show("GPS CFG_ON L1_SLP_PWR_CTL", N2G_GPS_CFG_L1_SLP_PWR);
		return n2gl_stop ? N2G_E_ARG : 0;
	case 15:
		/* plat/v010 gps_dl_hw_dep_set_dsp_on_and_poll_ack(L1): L1_ON = 1, SOFT_RST_B = 1, poll SLP_PWR_CTL_CS == 3,
		 * MEM_ADJ_DLY_EN = 1, MEM_PDN_DELAY_DUMMY_NUM = 5; then 100 us (3 x 32k) before the USRT is touched */
		n2gl_o.l1cr = n2gl_rd(N2G_GPS_RGU_L1_CR); n2gl_o.dly = n2gl_rd(N2G_GPS_RGU_L1_DLY_CHAIN); n2gl_o.mem = n2gl_rd(N2G_GPS_RGU_L1_MEM_DLY);
		done[15] = true;
		if (!n2gl_field(N2G_GPS_RGU_L1_CR, 1u, 1u) || !n2gl_field(N2G_GPS_RGU_L1_CR, 1u << 5, 1u << 5)) { return N2G_E_ARG; }
		if (!n2gl_poll(N2G_GPS_CFG_L1_SLP_PWR, 0xfu, 3u, 10000, "L1 SLP_PWR_CTL_CS == 3")) { return N2G_E_GPS_TOP; }
		if (!n2gl_field(N2G_GPS_RGU_L1_MEM_DLY, 1u << 31, 1u << 31) || !n2gl_field(N2G_GPS_RGU_L1_DLY_CHAIN, 0xfu << 16, 5u << 16)) { return N2G_E_ARG; }
		delay(100);
		{	/* 0359: poll USRT CTRL until it answers (50 us steps, 20 ms) and print the time */
			int us; uint32_t c = 0xffffffffu;
			for (us = 0; us <= 20000 && !n2gl_stop; us += 50) { c = n2gl_rd(N2G_USRT_CTRL(0)); if (c != 0xffffffffu) { break; } delay(50); }
			if (n2gl_stop) { return N2G_E_ARG; }
			printf("NORD2 GNSS LAD %d: USRT CTRL %s after %d us (%#x)\n", n2gl_k, c != 0xffffffffu ? "answered" : "STILL 0xffffffff", us, c);
			if (c == 0xffffffffu) { return N2G_E_GPS_TOP; }
		}
		n2gl_show("USRT CTRL", N2G_USRT_CTRL(0)); n2gl_show("USRT INTEN", N2G_USRT_INTEN(0)); n2gl_show("USRT STA", N2G_USRT_STA(0));
		n2gl_show("USRT A2DF", N2G_USRT_A2DF(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0)); n2gl_show("USRT A2D1", N2G_USRT_A2D1(0));
		return n2gl_stop ? N2G_E_ARG : 0;
	case 16: {
		/* gps_dl_hw_usrt_ctrl(L1, on, DMA mode, 4-byte): BYTEN = 0, TX_EN = 1, RX_EN = 1, A2D1 = 0x000e (MVCD speed-up |
		 * A-die MT6635 E2+ | 4-byte), A2DF3 = 1, then poll D2AF3 = DSP ROM okay. DEVIATION: the vendor also sets
		 * INTEN TXIEN/NODAIEN; they stay 0 here (no GNSS interrupt handler exists; polling only). */
		uint32_t ctl = n2gl_rd(N2G_USRT_CTRL(0));
		if (n2gl_stop) { return N2G_E_ARG; }
		if (ctl == 0xffffffffu) { printf("NORD2 GNSS LAD %d: USRT still reads 0xffffffff, not touching it\n", n2gl_k); return N2G_E_GPS_TOP; }
		done[16] = true;
		if (!n2gl_field(N2G_USRT_CTRL(0), 0xbu, 0x3u) || !n2gl_field(N2G_USRT_A2D1(0), 0xffffu, 0x000eu) ||
		    !n2gl_field(N2G_USRT_A2DF(0), 1u << 3, 1u << 3)) { return N2G_E_ARG; }
		/* 0362 (V9, K >= 23): vendor gps_dl_hw_usrt_ctrl also sets INTEN TXIEN [0] | NODAIEN [1]. The GIC lines (SPI 383..389 =
		 * INTID 415..421) are not enabled by XNU (no handler), so this only raises the USRT-level status, as on Android. */
		/* 0363: only K = 23, 24 (as built in q10jr9) and 28 (S10d) set INTEN; the other S10 ladders leave it 0 */
		if (n2gl_v9 && (n2gl_k == 23 || n2gl_k == 24 || n2gl_k == 28) && !n2gl_field(N2G_USRT_INTEN(0), 0x3u, 0x3u)) { return N2G_E_ARG; }
		bool rom = n2gl_poll(N2G_USRT_D2AF(0), 1u << 3, 1u << 3, 100000, "DSP ROM okay (USRT D2AF3)");
		n2gl_show("USRT STA", N2G_USRT_STA(0));
		if (n2gl_stop) { return N2G_E_ARG; }
		printf("NORD2 GNSS LAD %d: L1 DSP %s\n", n2gl_k, rom ? "ROM OKAY: ready for the MVCD download over the data link" : "ROM did not answer within 100 ms");
		return rom ? 0 : 9;                                     /* N2G_E_TIMEOUT (not in the 0350 header) */
	}
	case 17: {
		/* data link up: ring window mapped and zeroed, remap check, both L1 DMA channels idle */
		if (n2gd_ringwin(&n2gl_ring_va)) { return N2G_E_ARG; }
		n2gl_rx.rd = n2gl_rx.wr = 0;
		done[17] = true;
		n2gl_show("GPS EMI remap (want 0xdfc0, 0584)", N2G_CSR_CONN2AP_GPS_EMI);
		n2gl_dma_regs("idle?", N2GL_TXCH); n2gl_dma_regs("idle?", N2GL_RXCH);
		n2gl_show("USRT STA", N2G_USRT_STA(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0));
		return n2gl_stop ? N2G_E_ARG : 0;
	}
	case 18: {
		printf("NORD2 GNSS LAD %d: TX libmnl frame 1 (15 bytes): aa f0 09 00 05 fe 32 00 00 01 00 3f 01 aa 0f\n", n2gl_k);
		done[18] = true;
		int e = n2gl_tx(n2gl_frame1, sizeof n2gl_frame1, true);
		n2gl_show("USRT STA after TX", N2G_USRT_STA(0));
		return e;
	}
	case 19: {
		/* up to 8 RX rounds over ~1 s: each waits up to 125 ms for TX_IND (DSP has data) */
		done[19] = true;
		uint32_t total = 0;
		for (int r = 0; r < 8 && !n2gl_stop; r++) { total += n2gl_rx_round(125000, true); }
		if (n2gl_stop) { return N2G_E_ARG; }
		printf("NORD2 GNSS LAD %d: DSP reply: %u bytes\n", n2gl_k, total);
		if (total) { n2gl_hexdump("RX", 0, total < 256 ? total : 256); }
		n2gl_show("USRT STA", N2G_USRT_STA(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0));
		return 0;                                           /* no reply is a result, not a failure */
	}
	}
	return 0;
}
/* rollback of the done steps, restoring the saved original bits; vendor off order (sleep protection first) */
static bool
n2gl_undo(bool done[32])
{
	bool clean = true;
	/* 0357: vendor GPS_L1_DSP_OFF order: USRT off (TX/RX/IEN = 0), pwr_stat, then set_dsp_off (SOFT_RST_B = 0, L1_ON = 0).
	 * The USRT read 0xffffffff before L1 was on, so its rollback target is the vendor off value, not the saved word. */
	if (n2gl_rx_started) { n2gl_dma_stop(N2GL_RXCH); n2gl_rx_started = false; }      /* 0359: DMA channels stopped first */
	if (n2gl_tx_started) { n2gl_dma_stop(N2GL_TXCH); n2gl_tx_started = false; }
	n2gl_rx_armed = false;
	if (done[25]) { n2gl_field(N2G_CFG_EMI_CTL, 1u, n2gl_o.emictl); }   /* 0362: EMI_REQ_GPS released after the DMAs stop */
	if (done[16]) { n2gl_field(N2G_USRT_CTRL(0), 0x3u, 0); n2gl_field(N2G_USRT_INTEN(0), 0x3u, 0); }
	if (done[13]) { n2gl_field(N2G_GPS_AON_DSLEEP_CTL, 0x1fu, n2gl_o.aon); }
	if (done[15]) {
		n2gl_field(N2G_GPS_RGU_L1_CR, 1u << 5, n2gl_o.l1cr); n2gl_field(N2G_GPS_RGU_L1_CR, 1u, n2gl_o.l1cr);
		n2gl_field(N2G_GPS_RGU_L1_MEM_DLY, 1u << 31, n2gl_o.mem); n2gl_field(N2G_GPS_RGU_L1_DLY_CHAIN, 0xfu << 16, n2gl_o.dly);
		n2gl_show("L1 SLP_PWR_CTL after", N2G_GPS_CFG_L1_SLP_PWR);
	}
	if (done[8]) {
		static const int order[4] = { 1, 0, 3, 2 };              /* conn2gps tx, conn2gps rx, gps2conn tx, gps2conn rx */
		for (int j = 0; j < 4; j++) {
			int i = order[j];
			uint32_t orig = i < 2 ? n2gl_o.c2g : n2gl_o.g2c;
			n2gl_field(n2gl_slp_off[i].pa, n2gl_slp_off[i].en, orig);
			if ((orig & n2gl_slp_off[i].en) && !n2gl_poll(n2gl_slp_off[i].pa, n2gl_slp_off[i].rdy, n2gl_slp_off[i].rdy, N2G_POLL_MAX, "rollback sleep protection RDY set")) { clean = false; }
		}
	}
	if (done[12]) { n2gl_field(N2G_GPS_CLKGEN1_CTL, 1u << 4, n2gl_o.clk1); }
	if (done[10]) { n2gl_field(N2G_CFG_ADIE_CTL, 1u << 1, n2gl_o.adie); }
	if (done[9]) { n2gl_field(N2G_CSR_HOST2GPS_DBG_SEL, 0xffu, n2gl_o.sel); }
	if (done[7]) { n2gl_field(N2G_CFG_GPS_PWRCTRL0, 1u, n2gl_o.func); }
	if (done[6]) { n2gl_field(N2G_RGU_BGFSYS_PWR_CTL, 0xffff0080u, N2G_RGU_BGFSYS_PWR_KEY | (n2gl_o.bgf & 0x80u)); n2gl_show("BGFSYS_PWR_ACK after", N2G_RGU_BGFSYS_PWR_ACK); }
	if (done[5]) { n2gl_field(N2G_CSR_CONN2AP_GPS_EMI, 0x000fffffu, n2gl_o.emi); }
	if (done[1]) { n2gl_field(N2G_CSR_WAKEPU_GPS, 1u, n2gl_o.vote); n2gl_show("ack after vote release", N2G_CSR_SLP_PROT_CTRL); }
	return clean;
}
/*
 * 0358 (q10jr7b): q10jr7 l7b: after L1_ON + SOFT_RST_B the L1 sleep-power state read 0x33 at once, but every USRT word stayed
 * 0xffffffff. Ladder K=19 runs steps 1..15 and then this READ-ONLY probe (no write at all), safest groups first, each value
 * printed as it is read and 20 ms given to the console between groups (30 ms before each unknown page of group 8), so a reset
 * localises to the group/address. Pages outside the GNSS list are mapped read-only-in-practice from kernel_map
 * (ml_io_map_unmappable, Device), bypassing the write allow-list: this function never writes.
 *   P1 USRT 0x18c73004 timeline: 20 reads 100 us apart, then 20 reads 1 ms apart (first non-0xffffffff time)
 *   P2 conn_infra GPS config: 0x18001878 PWRCTRL0, 0x18001834/38/3c RC_CTL (GPSSYS_OSC_RC_EN = 0x18001834[4]), 0x180010f0
 *      light security, 0x18001860 PWRCTRL0, 0x18001a00 CKGEN_BUS, 0x18005088/94/98 WB_GPS, 0x180601dc GPS peri remap, 0x18000090
 *   P3 GPS RGU_ON 0x18c00000..1c, 0x18c00048..5c (L1 DSP P/X/Y RAM PDN/SLP), 0x18c00214 (L5 CR)
 *   P4 GPS CFG_ON 0x18c01000..1c, 0x18c01024, 0x18c0103c
 *   P5 GPS AON top 0x18c73100..11c, TCXO ms counter 0x18c73114/118 again after 2 ms (moves only if clocked)
 *   P6 BGF_GPS_CFG 0x18c21010 (IP version) / 14, BGF_GPS_DMA 0x18c10108/0x18c10148
 *   P7 USRT L1 0x18c73000/0c/10/20/24/30/50/54, L5 USRT 0x18c83004, GPS sys adrdec 0x18c70400/404
 *   P8 first word of 0x18c70000, 71000, 72000, 74000 .. 7f000 (unknown blocks, last)
 */
#define N2GP_MAXPG 32
static uint32_t n2gp_pg[N2GP_MAXPG];
static vm_offset_t n2gp_va[N2GP_MAXPG];
static uint32_t
n2gp_rd(uint32_t pa, bool *ok)
{
	*ok = false;
	if (!n2gl_guard()) { return 0xffffffffu; }
	uint32_t pg = pa & ~0xfffu;
	int i;
	for (i = 0; i < N2GP_MAXPG && n2gp_pg[i] && n2gp_pg[i] != pg; i++) { }
	if (i == N2GP_MAXPG) { printf("NORD2 GNSS PROBE: page table full at %#x\n", pa); return 0xffffffffu; }
	if (!n2gp_pg[i]) {
		vm_offset_t va = ml_io_map_unmappable(pg, 0x1000, VM_WIMG_IO);
		if (!va) { printf("NORD2 GNSS PROBE: map %#x failed\n", pg); return 0xffffffffu; }
		n2gp_pg[i] = pg; n2gp_va[i] = va;
	}
	*ok = true;
	return *(volatile uint32_t *)(n2gp_va[i] + (pa & 0xfffu));
}
static bool
n2gp_show(int g, uint32_t pa)
{
	bool ok;
	uint32_t v = n2gp_rd(pa, &ok);
	if (ok) { printf("NORD2 GNSS PROBE P%d: %#x = %#x\n", g, pa, v); }
	return ok;
}
static bool
n2gp_range(int g, uint32_t pa, uint32_t end)
{
	for (; pa <= end; pa += 4) { if (!n2gp_show(g, pa)) { return false; } }
	return true;
}
static int
n2gl_probe(void)
{
	bool ok;
	/* P1 */
	printf("NORD2 GNSS PROBE P1: USRT CTRL 0x18c73004 timeline\n"); delay(20000);
	int first = -1; uint32_t v = 0, last = 0;
	for (int i = 0; i < 40; i++) {
		v = n2gp_rd(0x18c73004u, &ok);
		if (!ok) { return N2G_E_ARG; }
		if (v != 0xffffffffu && first < 0) { first = i; }
		if (i == 0 || v != last) { printf("NORD2 GNSS PROBE P1: read %d (%s) = %#x\n", i, i < 20 ? "100us" : "1ms", v); }
		last = v;
		delay(i < 20 ? 100 : 1000);
	}
	printf("NORD2 GNSS PROBE P1: %s\n", first < 0 ? "USRT never answered within ~22 ms" : "USRT answered");
	/* P2 */
	delay(20000);
	static const uint32_t p2[] = { 0x18001878u, 0x18001834u, 0x18001838u, 0x1800183cu, 0x180010f0u, 0x18001860u, 0x18001a00u,
		0x18005088u, 0x18005094u, 0x18005098u, 0x180601dcu, 0x18000090u };
	for (unsigned i = 0; i < sizeof p2 / sizeof p2[0]; i++) { if (!n2gp_show(2, p2[i])) { return N2G_E_ARG; } }
	/* P3 */
	delay(20000);
	if (!n2gp_range(3, 0x18c00000u, 0x18c0001cu) || !n2gp_range(3, 0x18c00048u, 0x18c0005cu) || !n2gp_show(3, 0x18c00214u)) { return N2G_E_ARG; }
	/* P4 */
	delay(20000);
	if (!n2gp_range(4, 0x18c01000u, 0x18c0101cu) || !n2gp_show(4, 0x18c01024u) || !n2gp_show(4, 0x18c0103cu)) { return N2G_E_ARG; }
	/* P5 */
	delay(20000);
	if (!n2gp_range(5, 0x18c73100u, 0x18c7311cu)) { return N2G_E_ARG; }
	delay(2000);
	if (!n2gp_show(5, 0x18c73114u) || !n2gp_show(5, 0x18c73118u)) { return N2G_E_ARG; }
	/* P6 */
	delay(20000);
	printf("NORD2 GNSS PROBE P6: BGF_GPS_CFG / BGF_GPS_DMA (new pages)\n"); delay(20000);
	if (!n2gp_show(6, 0x18c21010u) || !n2gp_show(6, 0x18c21014u) || !n2gp_show(6, 0x18c10108u) || !n2gp_show(6, 0x18c10148u)) { return N2G_E_ARG; }
	/* P7 */
	delay(20000);
	static const uint32_t p7[] = { 0x18c73000u, 0x18c7300cu, 0x18c73010u, 0x18c73020u, 0x18c73024u, 0x18c73030u, 0x18c73050u, 0x18c73054u,
		0x18c83004u, 0x18c70400u, 0x18c70404u };
	for (unsigned i = 0; i < sizeof p7 / sizeof p7[0]; i++) {
		if (p7[i] == 0x18c83004u || p7[i] == 0x18c70400u) { printf("NORD2 GNSS PROBE P7: next %#x (new page)\n", p7[i]); delay(30000); }
		if (!n2gp_show(7, p7[i])) { return N2G_E_ARG; }
	}
	/* P8 */
	delay(20000);
	for (uint32_t pg = 0x18c70000u; pg <= 0x18c7f000u; pg += 0x1000u) {
		if (pg == 0x18c73000u) { continue; }
		printf("NORD2 GNSS PROBE P8: next %#x\n", pg); delay(30000);
		if (!n2gp_show(8, pg)) { return N2G_E_ARG; }
	}
	printf("NORD2 GNSS PROBE: done\n");
	return 0;
}

static uint32_t
n2gd_ladder(int k)
{
	uint32_t g[9] = {0};
	bool done[32] = {false};
	n2gl_k = k; n2gl_stop = false;
	n2gt("ladder K=%d", k);
	bool ok = n2gd_wifi_guard(g) && (g[7] >> 16) == 1 && (g[8] >> 16) == 0;
	printf("NORD2 GNSS LAD %d (%s): spm304=%#x gals504=%#x infra228=%#x infra724=%#x gic130=%#x wifi ap=%u wf=%u asleep=%u stage=%u -> %s\n",
	    k, n2gl_name[k], g[0], g[3], g[4], g[5], g[6], g[7] >> 16, g[7] & 0xffff, g[8] >> 16, g[8] & 0xffff, ok ? "guard ok" : "GUARD REFUSES");
	if (!ok) { return 0x01000000u; }
	if (n2gd_hw(&n2gl_hw)) { return 0x0300u; }
	if ((k == 22 || k == 24 || (k >= 31 && k <= 43)) && n2gd.powered) { printf("NORD2 GNSS LAD %d: already held\n", k); return 1; }
	n2gl_tx_started = n2gl_rx_started = false;
	n2gl_v9 = k >= 23; n2gl_rx_armed = false;                /* 0362 */
	int last = k <= 19 ? k : (k == 21 ? 15 : (k == 22 || k >= 23) ? 17 : 16), rc = 0, s;
	for (s = 1; s <= last && !rc; s++) {
		if (s == k || (k == 20 && s == 16) || (k == 21 && s == 15) || (k == 22 && s == 17) || (k >= 23 && s == 16)) { printf("NORD2 GNSS LAD %d: about to run NEW step %d (%s)\n", k, s, n2gl_name[s]); delay(2000000); }
		else { printf("NORD2 GNSS LAD %d: step %d (%s)\n", k, s, n2gl_name[s]); }
		rc = n2gl_do(s, done);
		if (n2gl_stop) { return ((uint32_t)s << 8) | 0x01000000u; }
		printf("NORD2 GNSS LAD %d: step %d %s\n", k, s, rc ? "FAILED" : "done");
	}
	int failed = rc ? s - 1 : 0;
	if (!rc && (k == 23 || k == 24)) {
		/* 0362: vendor-order data link prep (MCUB ack, EMI_REQ_GPS, RX ring fill, DMA2 armed) */
		printf("NORD2 GNSS LAD %d: about to run NEW V9 phase (%s)\n", k, n2gl_name[k]); delay(2000000);
		rc = n2gl_v9_prep(done);
		if (n2gl_stop) { return 0x01000000u | ((uint32_t)k << 8); }
		if (rc) { failed = k; printf("NORD2 GNSS LAD %d: V9 prep FAILED (%d)\n", k, rc); }
	}
	if (!rc && k == 24) {
		/* HOLD as K = 22, but read() serves the pre-armed RX DMA (n2gl_rx_service); EMI_REQ_GPS stays claimed */
		n2gl_field(N2G_CSR_WAKEPU_GPS, 1u, n2gl_o.vote);
		if (n2gl_stop) { return 0x01000000u | 0x1800u; }
		n2gd.powered = true; n2gd.hw_valid = true; n2gd.last_err = 0;
		n2gd.conn_hw_ver = n2gl_rd(N2G_CONN_HW_VER);
		n2gl_v9hold = true; n2gd_dl_up = true;
		printf("NORD2 GNSS LAD 24: V9 HOLD, GNSS powered, RX DMA2 armed, /dev/gpsdl0 read/write enabled (conn_infra %#x), NOT rolled back until reboot\n", n2gd.conn_hw_ver);
		return 1;
	}
	if (!rc && (k == 32 || (k >= 34 && k <= 43))) {
		/*
		 * 0588 (gnss-20261114): K = 34..36 = K = 32 with the pipe made to look like the kernel S11 loop. gnss8a (q10jgd + gnssfix8,
		 * poll 1 ms) died ~3 ms after libmnl's fragment 1 like hang5 (poll 10 ms): pacing excluded. libmnl's frames are byte-identical
		 * to the kernel blob (boot proxy: "identical to the kernel blob", 0 frames unlike), and the TX register sequence is the same,
		 * so what differs from the working S11 loop is the HOLD environment: (a) 0585 put GPIO162 in mode 2 (DSP-driven L1 eLNA)
		 * BEFORE the download (K = 31 sets it only after the boot; never tested during a boot), (b) RX DMA2 runs one long 256 B
		 * arming across TXs (S11 stops it and arms a fresh 64 B round right before every TX), (c) the header comes ~46 ms after the
		 * EMI request / RESET_DONE ack (S11: 2 s pause). K = 34 removes all three, K = 35 only (a)+(c), K = 36 only (b)+(c).
		 */
		n2gq_mode = k == 34 ? 7u : k == 35 ? 5u : k == 36 ? 6u : k == 37 ? 47u : k == 38 ? 63u : k == 39 ? 111u : k == 40 ? 175u : k == 41 ? 13u : k == 42 ? 45u : k == 43 ? 77u : 0u;   /* 0592: 42 = 41 + close-on-data, 43 = 41 + ladder TX in the pipe */   /* 0590: bit 6 = pipe TX via n2gl_tx (ladder), bit 7 = no-poll TX */
		/* (0589 bits: */   /* bit 0 = no LNA, bit 1 = ladder-style RX per TX, bit 2 = no 0587 gate sampling, bit 3 = quiet pipe (0589), bit 4 = GIC trace per TX (0589), bit 5 = close + re-arm the RX round as soon as bytes arrived (S11 n2gb_rx_close, 0589) */
		/*
		 * 0586 (gnss-20261114): K = 32 = steps 1..17 (DSP ROM okay, data link up) + the S11 data-link prep WITHOUT the kernel DSP code
		 * download, then the same HOLD as K = 31 (GPS wake vote held, INTEN off, RX 256 B per arming). libmnl then runs its own
		 * _BootDSP_UART_V2 over /dev/gpsdl0 (header, 50 fragments, post-boot 0xFE05) exactly as on Android, where the kernel never
		 * boots the DSP. Reason (gnss6a/6b, 0583 traces hang3/hang4, V): with the kernel boot (K = 31) libmnl's own post-boot 0xFE05
		 * reached a DSP that was ALREADY running the RAM code (the kernel had sent the same 0xFE05 ~0.6 s before); the TX DMA
		 * completed (INTSTA 0x8000, STATE 1), two normal RX rounds followed and the phone died ~10 ms later with no AP access pending.
		 * I: a second execute/jump command restarts the running DSP code and it then misbehaves on the bus. Without the kernel boot
		 * the DSP receives each boot frame exactly once, from libmnl, whose state machine sees the genuine 0xFE31/0xFE32 acks and the
		 * DSP's real post-boot answer (gnssd5..7 had to fake them, and libmnl stayed idle).
		 */
		printf("NORD2 GNSS LAD %d: about to run NEW S12 phase (%s)\n", k, n2gl_name[k]); delay(2000000);
		n2gl_rx_cap = (n2gq_mode & 2u) ? 64u : 256u; n2gl_rx_norearm = false; n2gl_rx_armed = false; n2gl_rx_total = 0; n2gl_lines = 0;
		n2gl_show("USRT INTEN", N2G_USRT_INTEN(0)); n2gl_show("USRT STA", N2G_USRT_STA(0)); n2gl_show("USRT D2AF", N2G_USRT_D2AF(0));
		n2gl_mcub_ack(true);
		n2gl_o.emictl = n2gl_rd(N2G_CFG_EMI_CTL); done[25] = true;
		if (!n2gl_field(N2G_CFG_EMI_CTL, 1u, 1u)) { rc = N2G_E_ARG; failed = 32; }
		if (!rc && !n2gl_stop) {
			for (uint32_t i = 0; i < N2GL_RING; i++) { N2R[N2GL_RING + i] = N2GL_FILL; }
			__builtin_arm_dsb(0xf);
			n2gl_rx.rd = n2gl_rx.wr = 0;
			n2gd.powered = true; n2gd.hw_valid = true; n2gd.last_err = 0;
			n2gd.conn_hw_ver = n2gl_rd(N2G_CONN_HW_VER);
			n2gl_v9hold = true; n2gd_dl_up = true;
			if (k >= 34) { printf("NORD2 GNSS LAD %d: 0588 settle 2 s after the EMI request (as S11) before the pipe opens\n", k); delay(2000000); }
			printf("NORD2 GNSS LAD %d: S12 HOLD, DSP in ROM (no kernel download), GPS wake vote held, /dev/gpsdl0 read/write enabled for libmnl's own DSP boot, NOT rolled back until reboot (0588 mode %u: LNA %s, RX %s)\n",
			    k, n2gq_mode, (n2gq_mode & 1u) ? "NOT set" : "GPIO162 mode 2", (n2gq_mode & 2u) ? "fresh 64 B arming before each TX" : "256 B continuous");
			if (n2gq_mode & 8u) { n2gq_gic_mask(); } else { n2gq_hb("HOLD start (0586 S12: libmnl boots the DSP)"); }
			n2gt("S12 HOLD K=%d mode %u: DSP in ROM, libmnl boots it (0586/0588)", k, n2gq_mode);
			if (!(n2gq_mode & 1u)) { n2gt("LNA: GPIO162 -> mode 2 (0585)"); n2gd_lna_l1_dsp_ctrl(); }
			else { printf("NORD2 GNSS LNA: GPIO162 left in its boot mode (0588 K=%d)\n", k); }
			return 1;
		}
		if (n2gl_stop) { return 0x01000000u | ((uint32_t)k << 8); }
	}
	if (!rc && k >= 30 && k <= 33 && k != 32) {
		printf("NORD2 GNSS LAD %d: about to run NEW S11 phase (%s)\n", k, n2gl_name[k]); delay(2000000);
		n2gb_gap_ms = k == 33 ? 200 : 0;                     /* 0587: K = 33 pacing probe */
		rc = n2gb_boot(done);
		n2gb_gap_ms = 0;
		if (n2gl_stop) { return 0x01000000u | ((uint32_t)k << 8); }
		if (rc) { failed = k; }
		if (!rc && (k == 31 || k == 33)) {
			/* HOLD: the GPS wake vote stays HELD (q10jr9 released it; gl9b reset), INTEN off, RX 256 B per arming */
			n2gl_rx_cap = 256; n2gl_rx_norearm = false;
			n2gd.powered = true; n2gd.hw_valid = true; n2gd.last_err = 0;
			n2gd.conn_hw_ver = n2gl_rd(N2G_CONN_HW_VER);
			n2gl_v9hold = true; n2gd_dl_up = true;
			printf("NORD2 GNSS LAD %d: S11 HOLD, DSP booted, GPS wake vote held, /dev/gpsdl0 read/write enabled, NOT rolled back until reboot\n", k);
			n2gq_hb("HOLD start (0581 pipe: gate = Wi-Fi guard + gps_top_off ACTIVE)");
			n2gt("LNA: GPIO162 -> mode 2 (0585)");
			n2gd_lna_l1_dsp_ctrl();                              /* 0585: vendor gps_dl_lna_pin_ctrl(L1, dsp on) */
			return 1;
		}
	}
	if (!rc && k >= 25 && k < 30) {
		printf("NORD2 GNSS LAD %d: about to run NEW S10 phase (%s)\n", k, n2gl_name[k]); delay(2000000);
		rc = n2gl_s10(k, done);
		if (n2gl_stop) { return 0x01000000u | ((uint32_t)k << 8); }
		if (rc) { failed = k; }
	}
	if (!rc && k == 23) {
		rc = n2gl_v9_capture();
		if (n2gl_stop) { return 0x01000000u | 0x1700u; }
		if (rc) { failed = 23; }
	}
	if (!rc && k == 22) {
		/* HOLD: power stays on, only the GPS wake vote is released (n2g_link_on order). Not undoable in this image. */
		n2gl_field(N2G_CSR_WAKEPU_GPS, 1u, n2gl_o.vote);
		if (n2gl_stop) { return 0x01000000u | 0x1600u; }
		n2gd.powered = true; n2gd.hw_valid = true; n2gd.last_err = 0;
		n2gd.conn_hw_ver = n2gl_rd(N2G_CONN_HW_VER);
		n2gd_dl_up = true;
		printf("NORD2 GNSS LAD 22: HOLD, GNSS left powered with the data link up, /dev/gpsdl0 read/write enabled (conn_infra %#x), NOT rolled back until reboot\n", n2gd.conn_hw_ver);
		return 1;
	}
	if (!rc && k == 21) {
		printf("NORD2 GNSS LAD 21: about to run the READ-ONLY L1 probe (P1..P8)\n"); delay(2000000);
		rc = n2gl_probe();
		if (n2gl_stop) { return 0x01000000u | 0x1500u; }
		if (rc) { failed = 21; }
	}
	if (!rc && k == 20) {
		for (int t = 1; t <= 5 && !n2gl_stop; t++) {
			delay(1000000);
			uint32_t sta = n2gl_rd(N2G_USRT_STA(0)), d2a = n2gl_rd(N2G_USRT_D2AF(0)), top = n2gl_rd(N2G_CSR_GPS_CFG2HOST_DBG), ack = n2gl_rd(N2G_RGU_BGFSYS_PWR_ACK);
			if (!n2gl_stop) { printf("NORD2 GNSS LAD 20: t=%ds usrt_sta=%#x d2af=%#x top=%#x bgf_ack=%#x\n", t, sta, d2a, top, ack); }
		}
		if (n2gl_stop) { return 0x01000000u | 0x1400u; }
	}
	printf("NORD2 GNSS LAD %d: rolling back\n", k);
	bool clean = n2gl_undo(done);
	printf("NORD2 GNSS LAD %d: rollback %s%s\n", k, n2gl_stop ? "STOPPED BY GUARD" : "done", clean ? "" : " (a sleep-protection RDY did not come back)");
	return (rc ? ((uint32_t)failed << 8) | ((uint32_t)rc << 16) : 1u) | (n2gl_stop ? 0x01000000u : 0) | (clean ? 0 : 0x02000000u);
}

static uint32_t
n2gd_step(int step)
{
	uint32_t ao[6] = {0};
	printf("NORD2 GNSS STEP %d: begin, reading the always-on predicates\n", step);
	bool ok = n2gd_ao_predicates(ao);
	printf("NORD2 GNSS STEP %d: spm304=%#x spm16c=%#x spm170=%#x gals504=%#x infra228=%#x infra724=%#x -> %s\n", step,
	    ao[0], ao[1], ao[2], ao[3], ao[4], ao[5], ok ? "conn_infra reachable (Wi-Fi guard ok)" : "REFUSED (Wi-Fi guard fails)");
	if (step == 1) { return 1; }
	if (!ok) { return 0x0100u; }
	if (!n2gd.enabled || !n2gd_conninfra_ready()) { printf("NORD2 GNSS STEP %d: refused, Wi-Fi owner not retained\n", step); return 0x0200u; }
	struct n2g_hw hw;
	if (n2gd_hw(&hw)) { return 0x0300u; }
	printf("NORD2 GNSS STEP %d: about to read 0x18060184 and 0x180601ac\n", step); delay(300000);
	uint32_t ack = hw.read32(hw.ctx, N2G_CSR_SLP_PROT_CTRL), vote = hw.read32(hw.ctx, N2G_CSR_WAKEPU_GPS);
	printf("NORD2 GNSS STEP %d: slp_prot_ctrl=%#x wakepu_gps=%#x\n", step, ack, vote);
	if (step == 2) { return 1; }
	if (step == 3) {
		printf("NORD2 GNSS STEP 3: about to set the GPS wake vote\n"); delay(300000);
		hw.write32(hw.ctx, N2G_CSR_WAKEPU_GPS, vote | 1u);
		int i; for (i = 0; i < 10000 && (hw.read32(hw.ctx, N2G_CSR_SLP_PROT_CTRL) & 0x20u); i++) { delay(1); }
		printf("NORD2 GNSS STEP 3: wake ack after %d polls (%s); about to read 0x18001000\n", i, i < 10000 ? "ok" : "TIMEOUT"); delay(300000);
		uint32_t ver = i < 10000 ? hw.read32(hw.ctx, N2G_CONN_HW_VER) : 0;
		hw.write32(hw.ctx, N2G_CSR_WAKEPU_GPS, vote & ~1u);
		printf("NORD2 GNSS STEP 3: conn_infra version %#x, vote released\n", ver);
		return i < 10000 ? 1u : 0x0800u;
	}
	printf("NORD2 GNSS STEP 4: about to run n2g_link_on (common_on + L1 DSP on), every access announced\n"); delay(300000);
	n2gd_inner = hw; n2gd_acc = 0;
	struct n2g_hw traced = hw; traced.read32 = n2gd_trace_rd; traced.write32 = n2gd_trace_wr;
	int rc = n2g_link_on(&traced);
	n2gd.last_err = rc;
	if (rc == N2G_OK) { n2gd.powered = true; n2gd.hw_valid = true; n2gd.conn_hw_ver = hw.read32(hw.ctx, N2G_CONN_HW_VER); }
	printf("NORD2 GNSS STEP 4: n2g_link_on -> %d\n", rc);
	return rc == N2G_OK ? 1u : ((uint32_t)rc << 8);
}

static int
n2gd_open(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	int l = minor(dev);
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS) { return ENXIO; }
	lck_mtx_lock(&n2gd_lock);
	int e = 0;                                               /* 0353: open never touches the hardware */
	(void)n2gd_power_on_locked;
	n2gd.opens[l]++;
	lck_mtx_unlock(&n2gd_lock);
	return e;
}

static int
n2gd_close(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	int l = minor(dev);
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS) { return ENXIO; }
	lck_mtx_lock(&n2gd_lock);
	if (n2gd.opens[l]) { n2gd.opens[l]--; }
	/* power-off policy comes with milestone 2 (DSP sleep/stop states); the block stays on, as after mnld exits */
	lck_mtx_unlock(&n2gd_lock);
	return 0;
}

static int
n2gd_ioctl(dev_t dev, unsigned long cmd, caddr_t data, __unused int fflag, __unused struct proc *p)
{
	int l = minor(dev);
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS) { return ENXIO; }
	if (data == NULL) { return EFAULT; }
	n2gt("ioctl %#lx", cmd & 0xffffffffu);
	uint32_t v = 0;
	if ((uint32_t)cmd == (uint32_t)N2GD_IOC_CHUNK) {            /* 0364: S11 blob upload (kernel copy of the _IOW buffer) */
		lck_mtx_lock(&n2gd_lock);
		int ce = n2gb_chunk_in((const struct n2gb_chunk *)(const void *)data);
		lck_mtx_unlock(&n2gd_lock);
		return ce;
	}
	switch ((uint32_t)cmd) {
	case (uint32_t)N2GD_IOC(GPSDL_IOC_QUERY_STATUS):
		v = (n2gd.powered ? 1u : 0u) | ((uint32_t)(n2gd.last_err & 0xff) << 8);
		break;
	case (uint32_t)N2GD_IOC(GPSDL_IOC_GPS_HWVER):
		v = n2gd.conn_hw_ver;
		break;
	case (uint32_t)N2GD_IOC(GPSDL_IOC_TAKE_GPS_WAKELOCK):
	case (uint32_t)N2GD_IOC(GPSDL_IOC_GIVE_GPS_WAKELOCK):
		v = 0;                                           /* no wakelocks on XNU */
		break;
	case (uint32_t)N2GD_IOC(111):                            /* 0354 single accesses, one per boot */
	case (uint32_t)N2GD_IOC(112):
	case (uint32_t)N2GD_IOC(113):
	case (uint32_t)N2GD_IOC(114):
	case (uint32_t)N2GD_IOC(115):
		lck_mtx_lock(&n2gd_lock);
		v = n2gd_single((int)((uint32_t)cmd & 0xff) - 110);
		lck_mtx_unlock(&n2gd_lock);
		break;
	case (uint32_t)N2GD_IOC(141):                            /* 0356 power-on ladder, one per boot */
	case (uint32_t)N2GD_IOC(142):
	case (uint32_t)N2GD_IOC(143):
	case (uint32_t)N2GD_IOC(144):
	case (uint32_t)N2GD_IOC(145):
	case (uint32_t)N2GD_IOC(146):
	case (uint32_t)N2GD_IOC(147):
	case (uint32_t)N2GD_IOC(148):
	case (uint32_t)N2GD_IOC(149):
	case (uint32_t)N2GD_IOC(150):
	case (uint32_t)N2GD_IOC(151):
	case (uint32_t)N2GD_IOC(152):
	case (uint32_t)N2GD_IOC(153):
	case (uint32_t)N2GD_IOC(154):
	case (uint32_t)N2GD_IOC(155):
	case (uint32_t)N2GD_IOC(156):                            /* 0357: ladder steps 14..16, 17 = 5 s, 18 = hold */
	case (uint32_t)N2GD_IOC(157):
	case (uint32_t)N2GD_IOC(158):
	case (uint32_t)N2GD_IOC(160):                            /* 0359: 17..19 data link, 20 = 5 s, 21 = probe, 22 = HOLD */
	case (uint32_t)N2GD_IOC(161):
	case (uint32_t)N2GD_IOC(162):
	case (uint32_t)N2GD_IOC(159):                            /* 0358: K=19 = steps 1..15 + read-only L1 probe */
	case (uint32_t)N2GD_IOC(163):                            /* 0362: K=23 V9 vendor-order capture, K=24 V9 HOLD */
	case (uint32_t)N2GD_IOC(164):
	case (uint32_t)N2GD_IOC(165):                            /* 0363: S10a..e = K 25..29 */
	case (uint32_t)N2GD_IOC(166):
	case (uint32_t)N2GD_IOC(167):
	case (uint32_t)N2GD_IOC(168):
	case (uint32_t)N2GD_IOC(169):
	case (uint32_t)N2GD_IOC(170):                            /* 0364: S11 boot (K=30) / boot + HOLD (K=31) */
	case (uint32_t)N2GD_IOC(171):
	case (uint32_t)N2GD_IOC(172):                            /* 0586: S12 HOLD without the kernel DSP boot */
	case (uint32_t)N2GD_IOC(173):                            /* 0587: S13 pacing probe (K=31 + 200 ms gap after fragment 1) */
	case (uint32_t)N2GD_IOC(174):                            /* 0588: S12b/c/d (K=32 variants) */
	case (uint32_t)N2GD_IOC(175):
	case (uint32_t)N2GD_IOC(176):
	case (uint32_t)N2GD_IOC(177):                            /* 0589: S12e/f quiet pipe */
	case (uint32_t)N2GD_IOC(178):
	case (uint32_t)N2GD_IOC(179):                            /* 0590: S12g/h TX variants */
	case (uint32_t)N2GD_IOC(180):
	case (uint32_t)N2GD_IOC(181):                            /* 0591: S12i (HOLD for the ioctl-190 frame-by-frame boot) */
	case (uint32_t)N2GD_IOC(182):                            /* 0592: S12j/k */
	case (uint32_t)N2GD_IOC(183):
		if (n2gd_ctl & 1) { printf("NORD2 GNSS: ladder refused (sysctl kern.n2gnss bit 0)\n"); return ENODEV; }
		lck_mtx_lock(&n2gd_lock);
		v = n2gd_ladder((int)((uint32_t)cmd & 0xff) - 140);
		lck_mtx_unlock(&n2gd_lock);
		break;
	case (uint32_t)N2GD_IOC(121):                            /* 0355 mapping bisection, one per boot */
	case (uint32_t)N2GD_IOC(122):
	case (uint32_t)N2GD_IOC(123):
	case (uint32_t)N2GD_IOC(124):
	case (uint32_t)N2GD_IOC(125):
	case (uint32_t)N2GD_IOC(126):
	case (uint32_t)N2GD_IOC(127):
	case (uint32_t)N2GD_IOC(128):
	case (uint32_t)N2GD_IOC(129):
	case (uint32_t)N2GD_IOC(130):
		lck_mtx_lock(&n2gd_lock);
		v = n2gd_mapone((int)((uint32_t)cmd & 0xff) - 120);
		lck_mtx_unlock(&n2gd_lock);
		break;
	case (uint32_t)N2GD_IOC(101):                            /* 0353 single steps, one per call, markers first */
	case (uint32_t)N2GD_IOC(102):
	case (uint32_t)N2GD_IOC(103):
	case (uint32_t)N2GD_IOC(104):
		lck_mtx_lock(&n2gd_lock);
		v = n2gd_step((int)((uint32_t)cmd & 0xff) - 100);
		lck_mtx_unlock(&n2gd_lock);
		break;
	case (uint32_t)N2GD_IOC(192):                            /* 0593: L1 eLNA to the DSP (GPIO162 mode 2) on demand, AFTER the DSP boot */
		lck_mtx_lock(&n2gd_lock);
		if (!n2gd_dl_up) { v = 0x100u; }
		else { n2gt("LNA: GPIO162 -> mode 2 on request (0593 ioctl 192)"); n2gd_lna_l1_dsp_ctrl(); v = 1u; }
		lck_mtx_unlock(&n2gd_lock);
		break;
	case (uint32_t)N2GD_IOC(190): {                          /* 0591: one boot frame through the ladder's n2gb_xchg (K=41 HOLD) */
		int i = (int)*(uint32_t *)(void *)data, e = 0;
		lck_mtx_lock(&n2gd_lock);
		n2gl_k = 41; n2gl_stop = false;
		if (!n2gd_dl_up || !n2gb_valid() || i < 0 || i >= N2GB_NF || i == 1) { v = 0x100u; }
		else {
			if (i == 0) {                                       /* same init as n2gb_boot */
				n2gl_rx_cap = 64; n2gl_rx_norearm = true; n2gb_rx_close(); n2gl_rx_armed = false; n2gl_rx_total = 0; n2gl_lines = 0;
				n2gp_n = -1; n2gp_prev = -1; n2gp_frames = n2gp_bad = 0;
			}
			n2gt("xchg190 frame %d", i);
			if (i == N2GB_NF - 1) {                             /* post-boot 0xFE05: TX, then RX as the K=31 HOLD (256 B, re-arm) */
				n2gl_rx_cap = 256; n2gl_rx_norearm = false; n2gb_rx_close(); n2gl_rx_arm(false);
				e = n2gl_tx(n2gb_data + n2gb_tab[i][0], n2gb_tab[i][1], false);
				v = e ? 0u : 1u;
			} else {
				bool ok = false;
				for (int t = 0; t < 3 && !ok && !e && !n2gl_stop; t++) { ok = n2gb_xchg(i, i == 0 ? 0xfe31u : 0xfe32u, i == 0 ? 0 : i - 1, 300, &e); }
				v = ok ? 1u : 0u;
			}
			if (n2gl_stop) { v |= 0x01000000u; n2gl_stop = false; }
		}
		lck_mtx_unlock(&n2gd_lock);
		break;
	}
	default:
		return ENOTSUP;                                  /* milestone 2+ */
	}
	*(uint32_t *)(void *)data = v;                           /* _IOWR: the kernel copies the 4 bytes in/out */
	return 0;
}

static int n2gd_rw(dev_t, struct uio *, int) __attribute__((unused));
static int
n2gd_rw(__unused dev_t dev, __unused struct uio *uio, __unused int flag)
{
	return ENODEV;
}
/* 0359: byte pipe for n2mnld after ladder 22 (HOLD with the data link up). write: synchronous A2D DMA of up to 4 KiB per call.
 * read: RX rounds (D2A DMA) until data, up to 1 s, or EAGAIN at once with IO_NDELAY. Link 0 (L1) only.
 *
 * 0581 (gnss-20261114) pipe hardening, after gnssd3/gnssd3j (q10jfg/q10jfj, 12 Nov): both runs lost the phone (USB console
 * cut mid-line) 1..3 s after libmnl's engine threads started, while the same K=31 HOLD with no engine process stayed up for
 * 6 minutes (gnssd1b/gnssd2). The cause was never seen (the engine log was on the lost rootfs state). Changes:
 *  a) every pipe call first checks, from the conn_infra HOST CSR page only (never the GPS bus): the Wi-Fi guard (quiet, rate-
 *     limited log) and gps_top_off == ACTIVE through the host debug mux (HOST2GPS_DBG_SEL 0x80, set by ladder step 9 and kept in
 *     HOLD; CFG2HOST_DBG [3:0] == 2). If the GPS top is not active, no GPS-bus register (USRT, BGF DMA) is accessed: an AXI
 *     access to a powered-down GPS domain hangs the SoC, the most likely way to lose the phone without a panic line.
 *     sysctl kern.n2gnss bit 1 = observe only (count + log, still access).
 *  b) refusals are transient (EAGAIN) instead of disabling the pipe at the first one; the pipe goes down (EIO) only after
 *     3000 consecutive refusals (~30 s of n2mnld's 10 ms polls).
 *  c) write uses the quiet DMA helpers (0580 printed 5 console lines per write) and logs the first 24 writes BEFORE the DMA
 *     ("about to TX"), so a hang shows the last frame on the console.
 *  d) heartbeat every 2 s from the pipe calls: "NORD2 GNSS PIPE: ..." counters, last MCUB flag, gps_top_off value, refusals.
 */
#ifndef IO_NDELAY
#define IO_NDELAY 0x0010                                        /* sys/vnode.h */
#endif
#define N2GQ_OBSERVE ((n2gd_ctl & 2) != 0)                     /* sysctl kern.n2gnss bit 1 */
static struct {
	uint64_t rd, rd_data, rd_eagain, wr, wr_bytes, rx_bytes, guard_ref, top_bad, tx_to, mcub, last_hb_ns, t0_ns;
	uint32_t last_d2af, last_d0, last_d1, last_top, last_sel, consec_ref, wlog, toplog, mlog;
	uint32_t last_slp, last_bto, cslog;                       /* 0587 */
} n2gq;
#ifndef N2G_CSR_BUS_TIMEOUT_IRQ
#define N2G_CSR_BUS_TIMEOUT_IRQ 0x180602d4u                     /* 0587: conn_infra host CSR, [0] == 1 = no off-domain bus hang */
#endif
static uint64_t
n2gq_ns(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}
static void
n2gq_mcub_note(uint32_t f, uint32_t d0, uint32_t d1)
{
	n2gq.mcub++;
	n2gt("mcub flag %#x d0 %#x d1 %#x", f, d0, d1);
	if (n2gd_dl_up && !(n2gq_mode & 8u) && (f != n2gq.last_d2af || d0 != n2gq.last_d0) && n2gq.mlog < 40) {
		n2gq.mlog++;
		printf("NORD2 GNSS PIPE: MCUB d2a flag %#x d0 %#x d1 %#x (was %#x)\n", f, d0, d1, n2gq.last_d2af);
	}
	n2gq.last_d2af = f; n2gq.last_d0 = d0; n2gq.last_d1 = d1;
}
static void
n2gq_hb(const char *why)
{
	uint64_t ns = n2gq_ns();
	if (!n2gq.t0_ns) { n2gq.t0_ns = ns; }
	if (ns - n2gq.last_hb_ns < 2000000000ull && why == NULL) { return; }
	n2gq.last_hb_ns = ns;
	printf("NORD2 GNSS PIPE: t=%llus rd=%llu (data %llu, eagain %llu) rx=%llu B | wr=%llu (%llu B, tx timeouts %llu) | mcub=%llu last d2af %#x d0 %#x | "
	    "top=%#x sel=%#x top_bad=%llu guard_ref=%llu consec=%u%s%s%s\n",
	    (unsigned long long)((ns - n2gq.t0_ns) / 1000000000ull), (unsigned long long)n2gq.rd, (unsigned long long)n2gq.rd_data,
	    (unsigned long long)n2gq.rd_eagain, (unsigned long long)n2gq.rx_bytes, (unsigned long long)n2gq.wr, (unsigned long long)n2gq.wr_bytes,
	    (unsigned long long)n2gq.tx_to, (unsigned long long)n2gq.mcub, n2gq.last_d2af, n2gq.last_d0, n2gq.last_top, n2gq.last_sel,
	    (unsigned long long)n2gq.top_bad, (unsigned long long)n2gq.guard_ref, n2gq.consec_ref, N2GQ_OBSERVE ? " OBSERVE-ONLY" : "",
	    why ? " | " : "", why ? why : "");
}
/* 0 = go; EAGAIN = transient refusal (nothing on the GPS bus was touched); EIO = pipe taken down */
static int
n2gq_gate(void)
{
	if (n2gq_mode & 8u) { return n2gd_dl_up ? 0 : EIO; }    /* 0589 quiet pipe: NO register access in the gate (GPS wake vote held by the HOLD) */
	uint32_t g[9] = {0};
	bool ok = n2gd_wifi_guard(g) && (g[7] >> 16) == 1 && (g[8] >> 16) == 0;
	if (!ok) {
		n2gq.guard_ref++;
		if (n2gq.guard_ref <= 5 || n2gq.guard_ref % 500 == 0) {
			printf("NORD2 GNSS PIPE: Wi-Fi/conninfra guard refuses (#%llu): spm304=%#x gals504=%#x infra228=%#x infra724=%#x gic130=%#x ap=%u asleep=%u stage=%u\n",
			    (unsigned long long)n2gq.guard_ref, g[0], g[3], g[4], g[5], g[6], g[7] >> 16, g[8] >> 16, g[8] & 0xffff);
		}
	} else {
		/* conn_infra host CSR page (Wi-Fi's own mapping), safe whenever the guard passes */
		uint32_t sel = n2gl_hw.read32(n2gl_hw.ctx, N2G_CSR_HOST2GPS_DBG_SEL);
		uint32_t top = n2gl_hw.read32(n2gl_hw.ctx, N2G_CSR_GPS_CFG2HOST_DBG);
		if ((top != n2gq.last_top || sel != n2gq.last_sel) && n2gq.toplog < 30) {
			n2gq.toplog++;
			printf("NORD2 GNSS PIPE: gps_top_off %#x (sel %#x) -> %s\n", top, sel,
			    ((sel & 0xffu) == 0x80u && (top & 0xfu) == 2u) ? "ACTIVE" : N2GQ_OBSERVE ? "NOT ACTIVE (observe only: still accessed)" : "NOT ACTIVE: GPS bus not touched");
		}
		n2gq.last_top = top; n2gq.last_sel = sel;
		/* 0587: two more host CSR words per pipe call (same Wi-Fi host page, safe whenever the guard passes), so the 0583 trace
		 * shows the conn_infra state at the poll rate up to a hang: CONN_SLP_PROT_CTRL (0x18060184: [5] on2off / [3] off2on
		 * sleep-protection ack, [11:9] the AP2CONN enables) and BUS_TIMEOUT_IRQ (0x180602d4 [0] == 1 = no off-domain bus hang). */
		if (!(n2gd_ctl & 16) && !(n2gq_mode & 4u)) {               /* 0588: K=34..36 skip it (same pipe as q10jgo/hang5) */
			uint32_t slp = n2gl_hw.read32(n2gl_hw.ctx, N2G_CSR_SLP_PROT_CTRL), bto = n2gl_hw.read32(n2gl_hw.ctx, N2G_CSR_BUS_TIMEOUT_IRQ);
			if ((slp != n2gq.last_slp || bto != n2gq.last_bto) && n2gq.cslog < 30) {
				n2gq.cslog++;
				printf("NORD2 GNSS PIPE: conn_infra slp_prot %#x (on2off ack %u off2on ack %u) bus_timeout_irq %#x%s\n", slp, (slp >> 5) & 1u,
				    (slp >> 3) & 1u, bto, (bto & 1u) ? "" : " (bit0 = 0: conn_infra off bus MAY HANG)");
			}
			n2gq.last_slp = slp; n2gq.last_bto = bto;
		}
		if ((sel & 0xffu) != 0x80u || (top & 0xfu) != 2u) {
			n2gq.top_bad++;
			ok = N2GQ_OBSERVE != 0;
		}
	}
	if (ok) { n2gq.consec_ref = 0; return 0; }
	if (++n2gq.consec_ref >= 3000) {
		n2gd_dl_up = false;
		n2gq_hb("PIPE DOWN after 3000 consecutive refusals (EIO from now on)");
		return EIO;
	}
	return EAGAIN;
}
/* 0589: GPS SPIs 383..389 = INTID 415..421 (live.dts gps@18C00000): GICD ISENABLER12 [31] / ISENABLER13 [5:0], ISPENDR 0x230/0x234,
 * ISACTIVER 0x330/0x334, ICENABLER 0x1b0/0x1b4. Logged at the K=37/38 HOLD and disabled if XNU had enabled any (it has no handler). */
static void n2gd_gicd_wr(uint32_t off, uint32_t v);          /* nord2_gnss_bind.inc */
static void
n2gq_gic_trace(const char *what)
{
	n2gt("gic %s en %#x/%#x pend %#x/%#x act %#x/%#x", what, n2gd_gicd_rd(0x130), n2gd_gicd_rd(0x134), n2gd_gicd_rd(0x230), n2gd_gicd_rd(0x234),
	    n2gd_gicd_rd(0x330), n2gd_gicd_rd(0x334));
}
static void
n2gq_gic_mask(void)
{
	uint32_t e12 = n2gd_gicd_rd(0x130), e13 = n2gd_gicd_rd(0x134);
	printf("NORD2 GNSS PIPE: 0589 quiet pipe; GPS GIC SPIs: isenabler12=%#x isenabler13=%#x ispendr12=%#x ispendr13=%#x isactiver12=%#x isactiver13=%#x\n",
	    e12, e13, n2gd_gicd_rd(0x230), n2gd_gicd_rd(0x234), n2gd_gicd_rd(0x330), n2gd_gicd_rd(0x334));
	n2gq_gic_trace("hold");
	if ((e12 != 0xffffffffu && (e12 & (1u << 31))) || (e13 != 0xffffffffu && (e13 & 0x3fu))) {
		if (e12 & (1u << 31)) { n2gd_gicd_wr(0x1b0, 1u << 31); }
		if (e13 & 0x3fu) { n2gd_gicd_wr(0x1b4, e13 & 0x3fu); }
		printf("NORD2 GNSS PIPE: 0589 GPS SPIs were ENABLED in the GIC: disabled (now isenabler12=%#x isenabler13=%#x)\n", n2gd_gicd_rd(0x130), n2gd_gicd_rd(0x134));
		n2gq_gic_trace("masked");
	}
}
/* 0588: K = 34/36 pipe RX like the S11 loop: before every TX close the running D2A round (bytes it wrote stay in the ring for
 * read(); nothing is consumed) and arm a fresh one (n2gl_rx_cap = 64) at the write index */
static void
n2gq_rx_fresh(void)
{
	if (n2gl_rx_armed) {
		uint32_t w = n2gl_rx_written();
		if (w > n2gl_rx_cur) { n2gl_rx_total += w - n2gl_rx_cur; n2gl_rx_cur = w; n2gl_rx.wr = (n2gl_rx_at + w) % N2GL_RING; }
		n2gl_dma_stop_q(N2GL_RXCH);
		n2gl_rx_armed = false;
	}
	if (!n2gl_stop) { (void)n2gl_rx_arm(false); }
}
/*
 * 0590 (gnss-20261114): every pipe boot died 1-3 ms after the fragment-1 ack, the ladder never. The only AP-side difference left (see
 * NOTES 12:30): the pipe TX polls the BGF DMA INTSTA 21-24 times WHILE the 355-word DMA runs; the ladder's n2gl_tx does readback +
 * console printf after START (hundreds of us), so its first poll normally comes after the ~25 us transfer; the vendor never polls,
 * it waits for the DMA-done IRQ. K = 39: the pipe calls n2gl_tx itself. K = 40: no-poll TX below (no readback, no printf).
 */
static int
n2gq_tx_nopoll(const uint8_t *buf, uint32_t len)
{
	if (!n2gl_ring_va || !len || len > N2GL_RING - 4u) { return N2G_E_ARG; }
	uint32_t padded = (len + 3u) & ~3u;
	for (uint32_t i = 0; i < padded; i++) { N2R[i] = i < len ? buf[i] : 0; }
	__builtin_arm_dsb(0xf);
	n2gl_tx_started = true;
	if (!n2gl_dma_start_q(N2GL_TXCH, N2G_CON_L1_A2D, 0, 0, padded)) { return N2G_E_ARG; }
	int i;
	for (i = 0; i < 250 && !n2gl_stop; i++) {               /* up to 50 ms: one done check every 200 us, the first one 200 us after START */
		delay(200);
		if (n2gl_dma_done(N2GL_TXCH)) { break; }
	}
	n2gl_dma_stop_q(N2GL_TXCH);
	if (n2gl_stop) { return N2G_E_ARG; }
	if (i >= 250) { n2gq.tx_to++; return 9; }
	return 0;
}
/* quiet synchronous TX (n2gl_tx without the per-register console lines) */
static int
n2gq_tx(const uint8_t *buf, uint32_t len)
{
	if (!n2gl_ring_va || !len || len > N2GL_RING - 4u) { return N2G_E_ARG; }
	uint32_t padded = (len + 3u) & ~3u;
	for (uint32_t i = 0; i < padded; i++) { N2R[i] = i < len ? buf[i] : 0; }
	__builtin_arm_dsb(0xf);
	n2gl_tx_started = true;
	if (!n2gl_dma_start_q(N2GL_TXCH, N2G_CON_L1_A2D, 0, 0, padded)) { return N2G_E_ARG; }
	int i;
	for (i = 0; i < 100000 && !n2gl_stop && !n2gl_dma_done(N2GL_TXCH); i++) { delay(1); }
	n2gl_dma_stop_q(N2GL_TXCH);
	if (n2gl_stop) { return N2G_E_ARG; }
	if (i >= 100000) { n2gq.tx_to++; return 9; }
	return 0;
}
static int
n2gd_read(dev_t dev, struct uio *uio, int flag)
{
	if (n2gd_ctl & 1) { return ENODEV; }                      /* 0580: kern.n2gnss bit 0 */
	if (minor(dev) != 0) { return ENODEV; }
	lck_mtx_lock(&n2gd_lock);
	int e = 0;
	if (!n2gd_dl_up) { e = ENODEV; goto out; }
	n2gl_k = 30; n2gl_stop = false;
	n2gq.rd++;
	n2gt("read #%llu flag %#x", (unsigned long long)n2gq.rd, flag);
	if ((e = n2gq_gate()) != 0) { if (e == EAGAIN) { n2gq.rd_eagain++; } goto out; }
	for (int t = 0; !n2gl_rx_used() && t < 1000 && !n2gl_stop; t++) {
		if (n2gl_v9hold) {                                   /* 0362: pre-armed RX DMA, MCUB flags acked and logged */
			n2gl_rx_service(false);
			if ((n2gq_mode & 32u) && n2gl_rx_armed && n2gl_rx_cur > 0) { n2gq_rx_fresh(); }   /* 0589: S11 closes the round on data */
			if (n2gl_rx_used() || (flag & IO_NDELAY)) { break; }
			delay(1000);
			if (t % 50 == 49 && n2gq_gate()) { break; }      /* 0581: blocking reads re-check the gate every 50 ms */
			continue;
		}
		n2gl_rx_round(1000, false);
		if (flag & IO_NDELAY) { break; }
	}
	if (n2gl_stop) {                                          /* 0581: a guard refusal inside the round is transient */
		n2gl_stop = false;
		n2gq.guard_ref++;
		e = (++n2gq.consec_ref >= 3000) ? (n2gd_dl_up = false, EIO) : EAGAIN;
		goto out;
	}
	uint32_t n = n2gl_rx_used();
	if (!n) { e = (flag & IO_NDELAY) ? EAGAIN : 0; if (e) { n2gq.rd_eagain++; } goto out; }
	if ((user_ssize_t)n > uio_resid(uio)) { n = (uint32_t)uio_resid(uio); }
	if (n2gq.rd_data < 3 && !(n2gq_mode & 8u)) { printf("NORD2 GNSS PIPE: read #%llu returns %u bytes (first data reads)\n", (unsigned long long)n2gq.rd, n); }
	n2gq.rd_data++;
	uint8_t tmp[256];
	while (n && !e) {
		uint32_t m = n < sizeof tmp ? n : (uint32_t)sizeof tmp;
		for (uint32_t i = 0; i < m; i++) { tmp[i] = N2R[N2GL_RING + (n2gl_rx.rd + i) % N2GL_RING]; }
		e = uiomove((caddr_t)tmp, (int)m, uio);
		if (!e) {
			if (n2gl_v9hold) { for (uint32_t i = 0; i < m; i++) { N2R[N2GL_RING + (n2gl_rx.rd + i) % N2GL_RING] = N2GL_FILL; } }
			n2gl_rx.rd = (n2gl_rx.rd + m) % N2GL_RING; n -= m; n2gq.rx_bytes += m;
		}
	}
out:
	if (n2gq.rd && !(n2gq_mode & 8u)) { n2gq_hb(NULL); }
	lck_mtx_unlock(&n2gd_lock);
	return e;
}
static int
n2gd_write(dev_t dev, struct uio *uio, __unused int flag)
{
	if (n2gd_ctl & 1) { return ENODEV; }                      /* 0580: kern.n2gnss bit 0 */
	if (minor(dev) != 0) { return ENODEV; }
	static uint8_t buf[4096];
	lck_mtx_lock(&n2gd_lock);
	int e = 0;
	if (!n2gd_dl_up) { e = ENODEV; goto out; }
	n2gl_k = 31; n2gl_stop = false;
	n2gt("write %lld bytes", (long long)uio_resid(uio));
	if ((e = n2gq_gate()) != 0) { goto out; }                /* nothing consumed: n2mnld retries EAGAIN */
	while (uio_resid(uio) > 0 && !e) {
		uint32_t m = uio_resid(uio) < (user_ssize_t)sizeof buf ? (uint32_t)uio_resid(uio) : (uint32_t)sizeof buf;
		e = uiomove((caddr_t)buf, (int)m, uio);
		if (e) { break; }
		n2gq.wr++;
		if (n2gq.wlog < 24 && !(n2gq_mode & 8u)) {
			n2gq.wlog++;
			printf("NORD2 GNSS PIPE: about to TX write #%llu: %u bytes, id %#x (%02x %02x %02x %02x %02x %02x)\n", (unsigned long long)n2gq.wr, m,
			    m >= 6 ? (uint32_t)(buf[4] | (buf[5] << 8)) : 0u, buf[0], m > 1 ? buf[1] : 0, m > 2 ? buf[2] : 0, m > 3 ? buf[3] : 0, m > 4 ? buf[4] : 0, m > 5 ? buf[5] : 0);
		}
		if (n2gq_mode & 2u) { n2gq_rx_fresh(); }               /* 0588 */
		if (n2gq_mode & 16u) { n2gq_gic_trace("pre"); }        /* 0589 */
		int te = (n2gq_mode & 64u) ? n2gl_tx(buf, m, false) : (n2gq_mode & 128u) ? n2gq_tx_nopoll(buf, m) : n2gq_tx(buf, m);   /* 0590 */
		if (n2gq_mode & 16u) { n2gq_gic_trace("post"); }
		if (te) { e = n2gl_stop ? EIO : ETIMEDOUT; break; }
		n2gq.wr_bytes += m;
	}
	if (n2gl_stop) { n2gl_stop = false; n2gq.guard_ref++; }  /* 0581: transient; the pipe stays up */
out:
	if (n2gq.wr && !(n2gq_mode & 8u)) { n2gq_hb(NULL); }
	lck_mtx_unlock(&n2gd_lock);
	return e;
}

#ifdef N2GD_HOST
static const struct cdevsw n2gd_cdevsw = { .d_open = n2gd_open, .d_close = n2gd_close, .d_ioctl = n2gd_ioctl, .d_read = n2gd_read, .d_write = n2gd_write };
#else
static const struct cdevsw n2gd_cdevsw = {
	.d_open = n2gd_open, .d_close = n2gd_close,
	.d_read = n2gd_read, .d_write = n2gd_write, .d_ioctl = n2gd_ioctl,
	.d_stop = eno_stop, .d_reset = eno_reset, .d_ttys = NULL,
	.d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0,
};
#endif

/* Called from nord2_input_init() after the Wi-Fi owner exists (the integrator adds the call). Touches no register. */
static int nord2_gpsdl_start(void) __attribute__((unused));
static int
nord2_gpsdl_start(void)
{
	if (n2gd.made) { return 0; }
	int on = 0;
	n2gt_init();                                               /* 0583 */
	/* 0580: on by default (the boot command line has ~4 free characters, n2gps=1 no longer fits; the n2gdv bits are all taken).
	 * Creating the nodes touches no register; the hardware is only reached by the ladder ioctls (Wi-Fi guard first) and the HOLD
	 * pipe, i.e. only when the gnssfix payload's n2mnld runs. Kill switches: the payload gate /private/var/db/nord2-gnssd (or its
	 * undo), and sysctl kern.n2gnss bit 0 (ladder + pipe refuse with ENODEV from then on). */
	n2gd.enabled = !(PE_parse_boot_argn("nord2gnss", &on, sizeof(on)) && on == 0);
	n2gd_grp = lck_grp_alloc_init("nord2-gpsdl", LCK_GRP_ATTR_NULL);
	lck_mtx_init(&n2gd_lock, n2gd_grp, LCK_ATTR_NULL);
	int major = cdevsw_add(-1, &n2gd_cdevsw);
	if (major < 0) { printf("NORD2 GNSS: cdevsw_add failed (%d)\n", major); return EIO; }
	for (int l = 0; l < N2GD_LINKS; l++) {
		if (devfs_make_node(makedev(major, l), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0660, "gpsdl%d", l) == NULL) {
			printf("NORD2 GNSS: devfs_make_node gpsdl%d failed\n", l);
			(void)cdevsw_remove(major, &n2gd_cdevsw);
			return EIO;
		}
	}
	n2gd.major = major;
	n2gd.made = true;
	printf("NORD2 GNSS: /dev/gpsdl0,1 made (major %d), power path %s\n", major, n2gd.enabled ? "enabled (0580 default; kill switch sysctl kern.n2gnss=1)" : "disabled");
	return 0;
}

#endif /* NORD2_GPSDL_INC */
