#include <arm64/sweet_early.h>
#if defined(NORD2)
/*
 * 0720 (bt-20261115): the MT6893 Bluetooth controller (CONNAC2 soc3_0 BGFSYS, BT firmware soc3_0_ram_{mcu,bt}_1a_1_hdr.bin)
 * brought up from XNU and reached over BTIF in PIO mode. See nord2_bt.h for the three pieces and the credits.
 *
 * Nothing happens at boot. The user daemon /usr/local/libexec/n2btd (payload bt1, it also replaces BlueTool's job)
 * writes kern.n2bt_addr (its per-device address, generated and stored on its first run) and then kern.n2bt_up=1; one
 * kernel thread then does, logging every step BEFORE it touches the hardware ("NORD2 BT0: ..."):
 *   S1  AP-side always-on checks (the six Wi-Fi checks: SPM 0x10006304[4:0]=0xd, 0x1000616c/170[1]=1, GALS 0x1020e504[0]=1,
 *       infra protection 0x10001228[13]=0 and 0x10001724[2]=0), retried for kern.n2bt_wait_s (120) while Wi-Fi comes up;
 *   S2  BT wake vote CONN_INFRA_WAKEUP_BT 0x180601a8[0], on2off sleep-protect ack 0x18060184[5]==0, CONN_INFRA_CFG id
 *       0x18001000==0x20010101 (MT6893), EMI remap 0x180601c4==0x8fb0 / BT peri 0x180601d8==0x01000 (set by our Wi-Fi);
 *   S3  BGF must be cold (CPU 0x18000014[0]==0, BT_FUNC_EN 0x18001874[0]==0) unless this boot powered it;
 *   S4  firmware: /usr/share/firmware/nord2-bt/soc3_0_ram_{mcu,bt}_1a_1_hdr.bin (the user's own vendor files, installed by
 *       the payload), header checked (ALPS, hw 0x8a, EMI target 0xF0xxxxxx inside MCU [0,1M) / BT [1M,1M+0x129400)),
 *       copied to CONN EMI 0x8fb00000 + target offset and read back;
 *   S5  radio status bit (SYSRAM 0x18052834[0] under semaphore 19), BGF top power 0x18000008 (key 0x4254|bit7) if GNSS
 *       has not set it, then bgfsys_power_on as btmtk_mt66xx.c: CPU reset, BPLL/WPLL 0x18003008[21:20] (semaphore 20,
 *       only bits we set are cleared), BT_FUNC_EN, BGF on/off power acks, conn2bt/bt2conn GALS sleep protection off,
 *       BGF ids 0x18812000/04/08/10 = 0x8a00/0x8a00/0/0x20010000, AHB, semaphore reset, CPU release, MCU ROM 0x1D1E;
 *       (deviation: the wake vote is kept until 0x1D1E is seen, then dropped; Linux drops it just before that poll)
 *   S6  BTIF 0x1100c000: clock infracfg_ao CG0 bit31 (clr 0x10001084, sta 0x10001090, values read from the stock
 *       kernel's ifrao_btif gate), hal_btif_hw_init PIO setup; driver own (BGF_LPCTL 0x18060030 bit1, poll bit2==0);
 *   S7  WMT over HCI: 01 6f fc 06 01 06 02 00 00 01 (FUNC_CTRL BT on), answer event e4 02 06 01 00 <status>;
 *   S8  HCI probe (Reset, Read_Local_Version, Read_BD_ADDR, Features, Buffer_Size, LE_Read_Buffer_Size);
 *   S9  address: MediaTek vendor 0xfc1a (6 bytes LSB first) with kern.n2bt_addr, read back with Read_BD_ADDR; what the
 *       controller then reports is what is published (a mismatch would break pairing keys);
 *   S10 n2bt_transport_run() (0721): registry (0722), nodes, kernel control, Skywalk, bridge -- never returns.
 * Any failure before S10 powers BGF off again (bgfsys_power_off order), drops our votes, sets kern.n2bt_state to
 * "failed:<step>" and the thread ends; another kern.n2bt_up=1 retries (at most kern.n2bt_tries, 3).
 *
 * bt2 changes (after the bt18b phone reset): all mappings are ml_io_map_unmappable (kernel_map) and lazy, never ml_io_map
 * (io_submap exhaustion = silent panic, GNSS 0355); a persistent breadcrumb in ramoops dump record 0x4811E000 BEFORE every risky
 * step; kernel guard: a previous boot that reset during the bring-up (INFLIGHT without END) makes this boot refuse; S1 needs 3
 * good reads; S2 reads the host CSR before its first write; kern.n2bt_maxstep N stops after step N (1 = AP-side reads only).
 * Switches (all run time; the command line is full):
 *   kern.n2bt          1 (default) bring-up allowed; 0 = kill switch, n2bt_up refused, nothing touched
 *   kern.n2bt_publish  1 (default) expose to iOS after the probe; 0 = controller up + probe only ("probe" stage)
 *   kern.n2bt_quirks   answer rewrites (nord2_bt.h N2BT_Q_*), default 0x1f; 0 = raw controller answers
 *   kern.n2bt_trace    HCI packets still to log (each direction, first 32 bytes), default 160
 *   kern.n2bt_adie     1 (default): every 2 s with the wake vote check the A-die top clock 0x18001900[1] that our GNSS
 *                      driver's power-off clears without the Linux refcount, and set it again while BT runs; 0 = off
 *   kern.n2bt_wait_s   S1 wait, default 120;  kern.n2bt_force 1 = skip the S1 predicates (diagnosis only)
 *   kern.n2bt_state    (read) idle | waiting-conninfra | starting:<step> | probe-ok | up | failed:<step>[:detail]
 *   kern.n2bt_stats    (read) counters;  kern.n2bt_note (write) a line from the daemon -> "NORD2 BT U: <line>"
 */
#include "nord2_bt.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <kern/kalloc.h>
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <sys/random.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <security/mac_policy.h>
#include <net/kpi_interface.h>
extern boolean_t IOCurrentTaskHasEntitlement(const char *entitlement);

extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, uint32_t);
extern void ml_io_unmap(vm_offset_t, vm_size_t);
#ifndef VM_WIMG_IO
#define VM_WIMG_IO 0x7u        /* osfmk/arm/memory_types.h: COHERENT|NOT_CACHEABLE|GUARDED = Device */
#endif
extern void IOSleep(unsigned milliseconds);
extern void IODelay(unsigned microseconds);
extern kern_return_t thread_terminate(thread_t);

#define BT0(...) printf("NORD2 BT0: " __VA_ARGS__)

/* ---------------- switches and state ---------------- */
static uint32_t n2bt_enable = 1, n2bt_publish = 1, n2bt_quirk = N2BT_Q_DEFAULT, n2bt_trace = 160, n2bt_adie = 1;
static uint32_t n2bt_tx_gap_us = 0;       /* bt19: unused (no pause inside a packet any more); kept for the sysctl */      /* bt6: pause after every 16-byte BTIF chunk of packets > 64 bytes */
static uint32_t n2bt_wait_s = 120, n2bt_force = 0, n2bt_tries = 3, n2bt_maxstep = 10;
static uint32_t n2bt_stop_mode = 0;      /* bt3: what a stop after S6+ does: 0 keep BT powered (ownership handed back), 1 graceful off */
static uint32_t n2bt_monitor_s = 600;     /* bt3: post-stop/up conn_infra monitor duration; END is written after it */
static bool n2bt_owned, n2bt_func_on, n2bt_btif_on;
static uint64_t n2bt_up_since;            /* when the bridge (or the probe loop) started, for the END mark */
static uint8_t n2bt_req_addr[6];          /* from kern.n2bt_addr, MSB first ("aa:bb:..") */
static bool n2bt_have_req_addr;
static uint8_t n2bt_ctl_addr[6];          /* controller's address after S9, LSB first */
static char n2bt_state_text[96] = "idle";
static uint32_t n2bt_attempts, n2bt_running, n2bt_bgf_ours;
static struct {
	uint32_t tx_pkts, rx_pkts, tx_bytes, rx_bytes, rx_resync, rx_overrun, tx_timeouts, adie_fixes, own_clr, tx_wait_max, rx_in_tx, rx_at_tx;
} n2bt_st;

static LCK_GRP_DECLARE(n2bt_lck_grp, "nord2-bt");
static LCK_MTX_DECLARE(n2bt_lock, &n2bt_lck_grp);

void
n2bt_set_state(const char *s)
{
	lck_mtx_lock(&n2bt_lock);
	strlcpy(n2bt_state_text, s, sizeof(n2bt_state_text));
	lck_mtx_unlock(&n2bt_lock);
	n2bt_now(s, 0, 0);                          /* bt11: the NOW slot names the bring-up step an AP reset interrupted */
}

uint32_t n2bt_quirks(void) { return n2bt_quirk; }

uint32_t
n2bt_trace_left(void)
{
	uint32_t v = n2bt_trace;
	if (v == 0) {
		return 0;
	}
	n2bt_trace = v - 1;
	return v;
}

void
n2bt_bdaddr(uint8_t out[6])
{
	memcpy(out, n2bt_ctl_addr, 6);
}

/* ---------------- MMIO: a fixed page list, mapped lazily from kernel_map ----------------
 * bt2 (after the q10jfinal18 + bt1 phone reset, 2026-11-15): NEVER ml_io_map(). ml_io_map takes from XNU's 8 MiB io_submap with
 * KMA_NOFAIL (osfmk/arm/io_map.c): when it is exhausted the kernel panics and on this port the panic never reaches the console
 * (GNSS 0355: q10jr4 reset the phone inside the ml_io_map of 6 pages; the Wi-Fi ROM window alone holds 0x450000 of it).
 * bt1's 0720 mapped 13 pages + ~0.85 MB of EMI that way as the first action of its thread = prime suspect of the reset.
 * Now every page is ml_io_map_unmappable(pa, 0x1000, VM_WIMG_IO) (kernel_map, Device, like GNSS/camera/pool), only when a step
 * first needs it, with a persistent breadcrumb BEFORE the mapping. */
static struct { uint32_t pa; vm_offset_t va; } n2bt_pages[] = {
	{ 0x10001000u, 0 },   /* infracfg_ao: protection status, CG0 (BTIF clock) */
	{ 0x10003000u, 0 },   /* bt9: MD coex CCIF control 0x10003318 (CONSYS_BGF_PWR_ON / BGF_READY), as Linux bgfsys_ccif_on */
	{ 0x18050000u, 0 },   /* bt9: conn_infra SYSRAM base page (BT calibration data backup/restore, Linux bgfsys_cal_data_*) */
	{ 0x10006000u, 0 },   /* SPM power status (read only) */
	{ 0x1020e000u, 0 },   /* AP GALS (read only) */
	{ 0x1100c000u, 0 },   /* BTIF */
	{ 0x18000000u, 0 },   /* conn_infra RGU */
	{ 0x18001000u, 0 },   /* conn_infra CFG */
	{ 0x18003000u, 0 },   /* conn_infra AFE (BPLL/WPLL) */
	{ 0x18005000u, 0 },   /* conn_infra WT_SLP (A-die clock busy) */
	{ 0x18052000u, 0 },   /* conn_infra SYSRAM (radio status word) */
	{ 0x18060000u, 0 },   /* conn_host_csr_top (always on) */
	{ 0x18072000u, 0 },   /* conn semaphore, M2 own/release */
	{ 0x18800000u, 0 },   /* BGFSYS */
	{ 0x18812000u, 0 },   /* BGFSYS info (ids) */
	{ 0x18c10000u, 0 },   /* bt3 quiesce check only: BGF_GPS_DMA (the GNSS trace polls 0x18c1021c/224), read when GPS is up */
	{ 0x18c73000u, 0 },   /* bt3 quiesce check only: GPS AON (the GNSS trace polls 0x18c7300c/024), read when GPS is up */
};
#define N2BT_NPAGES (sizeof(n2bt_pages) / sizeof(n2bt_pages[0]))
static bool n2bt_map_failed;
static volatile uint32_t n2bt_dummy;

/* ---------------- persistent breadcrumbs (survive a hard hang / WDT reset) ----------------
 * The LAST ramoops dump record: phys 0x48090000 + 142 x 4 KiB = 0x4811E000 (4 KiB), right below the console zone 0x4811F000 that the
 * GNSS trace (0583) uses. Linux layout from the LK bootargs (ramoops mem 0x48090000 size 0xe0000, console 0x40000, pmsg 0x10000,
 * record = ftrace = 4096 -> 143 dump records, then console, ftrace, pmsg 0x48160000). struct persistent_ram_buffer {'DBGC', start,
 * size, data}; data begins with a kmsg header "====0.0-D" so that the rescue's pstore keeps it: /sys/fs/pstore/dmesg-ramoops-142
 * (any dmesg-ramoops-* file containing "N2BT"). Linear (no wrap: the early lines are the interesting ones). Device mapping:
 * every line is in DRAM before the next hardware step. Also the kernel guard: a previous boot whose zone says "N2BT INFLIGHT"
 * without "N2BT END" reset during the bring-up -> this boot refuses (kern.n2bt_rearm=1 or a fresh payload install re-arms).
 * bt8 layout (the bt26a crash: the linear 4 KiB were full at 73 s, nothing of the pairing survived). Data area (after the 12-byte
 * header), all text, blank-padded, '\n' at the end of every slot so that the pstore file reads as lines:
 *     [0, 900)      the first lines of the boot, linear (zone header, INFLIGHT, the bring-up)
 *     [900, 940)    NOW:   "N2BT NOW <ms> <step> <a> <b>", overwritten at every step of the bridge (what it did last)
 *     [940, 964)    GUARD: "N2BT END <what>" once the bring-up is over (survives the ring)
 *     [964, 4080)   ring of 41 slots x 76 bytes: the LAST 41 lines (sequence number first: sort by it; bt-20261115/crumbs.py)
 * The record size (header word 2) is the whole 4080 bytes from the start, so the rescue's pstore always shows all of it. */
#define N2BT_CRUMB_PA   0x4811e000u
#define N2BT_CRUMB_SIZE 0x1000u
#define N2BT_LIN_CAP    900u
#define N2BT_NOW_AT     900u
#define N2BT_NOW_LEN    40u
#define N2BT_GUARD_AT   940u
#define N2BT_GUARD_LEN  24u
#define N2BT_RING_AT    964u
#define N2BT_SLOT       76u
#define N2BT_SLOTS      41u
#define N2BT_DATA_END   (N2BT_RING_AT + N2BT_SLOT * N2BT_SLOTS)   /* 4080 <= 4096 - 12 */
static volatile uint8_t *n2bt_crumb_va;
static uint32_t n2bt_crumb_seq, n2bt_rearm, n2bt_prev_reset, n2bt_end_written;
static uint32_t n2bt_lin_at, n2bt_ring_next;

/* blank-padded copy of t into [at, at + len) of the data area, '\n' last */
static void
n2bt_crumb_slot(uint32_t at, uint32_t len, const char *t, uint32_t n)
{
	if (n2bt_crumb_va == NULL || len == 0 || at + len > N2BT_DATA_END) return;
	for (uint32_t i = 0; i + 1 < len; i++) {
		char ch = i < n ? t[i] : ' ';
		n2bt_crumb_va[12u + at + i] = (uint8_t)(ch == '\n' ? ' ' : ch);
	}
	n2bt_crumb_va[12u + at + len - 1] = '\n';
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
n2bt_crumb_raw(const char *t, uint32_t n)
{
	if (n2bt_crumb_va == NULL) return;
	if (n2bt_lin_at + n <= N2BT_LIN_CAP) {
		for (uint32_t i = 0; i < n; i++) n2bt_crumb_va[12u + n2bt_lin_at + i] = (uint8_t)t[i];
		n2bt_lin_at += n;
		__asm__ volatile ("dsb sy" ::: "memory");
		return;
	}
	if (n2bt_lin_at < N2BT_LIN_CAP) {              /* close the linear part with blanks: no half line in it */
		char pad[1] = { ' ' };
		while (n2bt_lin_at < N2BT_LIN_CAP - 1) { n2bt_crumb_va[12u + n2bt_lin_at++] = (uint8_t)pad[0]; }
		n2bt_crumb_va[12u + n2bt_lin_at++] = '\n';
	}
	n2bt_crumb_slot(N2BT_RING_AT + N2BT_SLOT * (n2bt_ring_next % N2BT_SLOTS), N2BT_SLOT, t, n);
	n2bt_ring_next++;
}

void
n2bt_now(const char *step, uint32_t a, uint32_t b)
{
	char t[N2BT_NOW_LEN];
	uint64_t ns = 0;
	int k;
	if (n2bt_crumb_va == NULL) return;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	k = snprintf(t, sizeof(t), "N2BT NOW %llu %s %x %x", (unsigned long long)(ns / 1000000ull), step, a, b);
	if (k < 0) return;
	if (k > (int)sizeof(t) - 1) k = (int)sizeof(t) - 1;
	n2bt_crumb_slot(N2BT_NOW_AT, N2BT_NOW_LEN, t, (uint32_t)k);
}

static void n2bt_crumb(const char *fmt, ...) __printflike(1, 2);
static void
n2bt_crumb(const char *fmt, ...)
{
	char line[232];
	uint64_t ns = 0;
	va_list ap;
	int k;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	k = snprintf(line, sizeof(line), "N2BT %u %llu ", ++n2bt_crumb_seq, (unsigned long long)(ns / 1000000ull));
	va_start(ap, fmt);
	k += vsnprintf(line + k, sizeof(line) - (size_t)k - 2, fmt, ap);
	va_end(ap);
	if (k > (int)sizeof(line) - 2) k = (int)sizeof(line) - 2;
	printf("NORD2 BT0: %s\n", line + 5);
	line[k++] = '\n';
	n2bt_crumb_raw(line, (uint32_t)k);
}

/* bt4: breadcrumbs from 0721 (same zone, same format) */
void
n2bt_trail(const char *fmt, ...)
{
	char t[200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	n2bt_crumb("%s", t);
}

/* bt11: strstr (not in the kernel's string API here) */
static const char *
n2bt_find(const char *h, const char *n)
{
	size_t nl = strlen(n);
	for (; *h; h++) if (strncmp(h, n, nl) == 0) return h;
	return NULL;
}

/* map the zone, look at what the previous boot left, start this boot's record */
static void
n2bt_crumb_init(void)
{
	volatile uint32_t *h;
	char old[96];
	uint32_t osig, osz, prev_inflight = 0, prev_end = 0;

	if (n2bt_crumb_va != NULL) return;
	printf("NORD2 BT0: mapping the breadcrumb zone %#x (kernel_map, Device)\n", N2BT_CRUMB_PA);
	n2bt_crumb_va = (volatile uint8_t *)ml_io_map_unmappable(N2BT_CRUMB_PA, N2BT_CRUMB_SIZE, VM_WIMG_IO);
	if (n2bt_crumb_va == NULL) {
		printf("NORD2 BT0: breadcrumb zone not mapped: no persistent trace, guard off\n");
		return;
	}
	h = (volatile uint32_t *)(void *)n2bt_crumb_va;
	osig = h[0];
	osz = h[2];                                     /* bt8 zones: 4080 (the whole layout); older boots: what they wrote */
	if (osig == 0x43474244u && osz == N2BT_DATA_END) {
		/* bt11 rule: refuse ONLY when the AP died inside a hardware step. GUARD slot + NOW slot of that boot:
		 *   "N2BT END ..." (bridge started / clean stop / restart returned / alive every 15 s)  -> clean
		 *   "N2BT INFLIGHT"          and NOW = "starting:S2".."starting:S9"                     -> reset in the bring-up: refuse
		 *   "N2BT INFLIGHT restart"  and NOW = rs-bgf / rs-own / rs-func (restart power-on)     -> reset in a restart: refuse
		 *   anything else (NOW = idle/tx/rx/hk-*, S1 waiting, an older zone's leftovers)         -> clean */
		char g[N2BT_GUARD_LEN + 1], nw[N2BT_NOW_LEN + 1];
		for (uint32_t i = 0; i < N2BT_GUARD_LEN; i++) g[i] = (char)n2bt_crumb_va[12u + N2BT_GUARD_AT + i];
		for (uint32_t i = 0; i < N2BT_NOW_LEN; i++) nw[i] = (char)n2bt_crumb_va[12u + N2BT_NOW_AT + i];
		g[N2BT_GUARD_LEN] = 0;
		nw[N2BT_NOW_LEN] = 0;
		prev_end = strncmp(g, "N2BT END", 8) == 0;
		if (strncmp(g, "N2BT INFLIGHT restart", 21) == 0) {
			prev_inflight = n2bt_find(nw, " rs-bgf ") || n2bt_find(nw, " rs-own ") || n2bt_find(nw, " rs-func ");
		} else if (strncmp(g, "N2BT INFLIGHT", 13) == 0) {
			const char *p = n2bt_find(nw, " starting:S");
			prev_inflight = p != NULL && p[11] >= '2' && p[11] <= '9';
		}
		snprintf(old, sizeof(old), "guard \"%.21s\" now \"%.22s\"", g, nw + 9);
	} else if (0 && osig == 0x43474244u && osz <= N2BT_CRUMB_SIZE - 12u) {
		/* bt10: zones of older kernels (linear layout) are no longer judged: they could not tell a run-time hang from a
		 * reset during the bring-up (bt29a was refused after a q10jfinal27 boot). Only the bt8+ GUARD slot decides. */
		/* scan the old text for our two tokens (byte reads from Device memory, aligned by construction) */
		static const char t1[] = "N2BT INFLIGHT", t2[] = "N2BT END";
		uint32_t m1 = 0, m2 = 0;
		for (uint32_t i = 0; i < osz; i++) {
			char c = (char)n2bt_crumb_va[12u + i];
			m1 = (c == t1[m1]) ? m1 + 1 : (c == t1[0] ? 1 : 0);
			m2 = (c == t2[m2]) ? m2 + 1 : (c == t2[0] ? 1 : 0);
			if (m1 == sizeof(t1) - 1) { prev_inflight = 1; m1 = 0; }
			if (m2 == sizeof(t2) - 1) { prev_end = 1; m2 = 0; }
		}
	}
	if (!(osig == 0x43474244u && osz == N2BT_DATA_END)) snprintf(old, sizeof(old), "sig %#x size %u (older layout: not judged)", osig, osz);
	n2bt_prev_reset = prev_inflight && !prev_end;
	h[0] = 0x43474244u; h[1] = 0; h[2] = 0;
	__asm__ volatile ("dsb sy" ::: "memory");
	for (uint32_t i = 0; i < N2BT_DATA_END; i++) n2bt_crumb_va[12u + i] = (uint8_t)(((i + 1) % 76u) == 0 ? '\n' : ' ');
	n2bt_lin_at = 0;
	n2bt_ring_next = 0;
	h[1] = 0;
	h[2] = N2BT_DATA_END;
	__asm__ volatile ("dsb sy" ::: "memory");
	n2bt_crumb_raw("====0.0-D\n", 10);
	n2bt_crumb("zone %#x previous boot: %s%s", N2BT_CRUMB_PA, old, n2bt_prev_reset ? " -> THAT BOOT RESET DURING THE BT BRING-UP" : "");
}

static volatile uint32_t *
n2bt_reg(uint32_t pa)
{
	for (unsigned i = 0; i < N2BT_NPAGES; i++) {
		if ((pa & ~0xfffu) == n2bt_pages[i].pa) {
			if (n2bt_pages[i].va == 0 && !n2bt_map_failed) {
				n2bt_crumb("map page %#x (kernel_map, Device)", n2bt_pages[i].pa);
				n2bt_pages[i].va = ml_io_map_unmappable(n2bt_pages[i].pa, 0x1000, VM_WIMG_IO);
				if (n2bt_pages[i].va == 0) {
					n2bt_map_failed = true;
					n2bt_crumb("map of %#x FAILED", n2bt_pages[i].pa);
				}
			}
			if (n2bt_pages[i].va == 0) return &n2bt_dummy;
			return (volatile uint32_t *)(n2bt_pages[i].va + (pa & 0xfffu));
		}
	}
	n2bt_map_failed = true;
	printf("NORD2 BT0: register %#x is not in the page list (bug): access dropped\n", pa);
	return &n2bt_dummy;
}

static inline uint32_t rd(uint32_t pa) { return *n2bt_reg(pa); }
static inline void wr(uint32_t pa, uint32_t v) { *n2bt_reg(pa) = v; __asm__ volatile ("dsb sy" ::: "memory"); }
static inline void setb(uint32_t pa, uint32_t m) { wr(pa, rd(pa) | m); }
static inline void clrb(uint32_t pa, uint32_t m) { wr(pa, rd(pa) & ~m); }

/* poll until (reg & mask) == want; tries x us */
static bool
n2bt_poll(uint32_t pa, uint32_t mask, uint32_t want, unsigned tries, unsigned us)
{
	for (unsigned i = 0; i < tries; i++) {
		if ((rd(pa) & mask) == want) {
			return true;
		}
		IODelay(us);
	}
	return (rd(pa) & mask) == want;
}

/* ---------------- registers (MT6893) ---------------- */
#define SPM_CONN_PWR_CON      0x10006304u
#define SPM_PWR_STATUS        0x1000616cu
#define SPM_PWR_STATUS_2ND    0x10006170u
#define AP_GALS_CONN          0x1020e504u
#define INFRA_PROT_STA1       0x10001228u
#define INFRA_PROT_STA_VDNR   0x10001724u
#define INFRA_CG0_CLR         0x10001084u
#define INFRA_CG0_STA         0x10001090u
#define CONN_RGU_BGF_TOP_PWR  0x18000008u   /* [31:16] key 0x4254, [7] on */
#define CONN_RGU_BGF_CPU_RST  0x18000014u   /* [0] 1 = released */
#define CONN_RGU_BGF_SW_RST   0x1800001cu   /* [0] semaphore reset_b */
#define CONN_RGU_BGF_ON_ACK   0x18000114u   /* [25:24] */
#define CONN_RGU_BGF_OFF_ACK  0x18000124u   /* [24], [1:0] */
#define CONN_CFG_ID           0x18001000u
#define CONN_CFG_CONN2BT_GALS 0x18001610u
#define CONN_CFG_BT2CONN_GALS 0x18001614u
#define CONN_CFG_BT_PWRCTLCR0 0x18001874u   /* [0] BT_FUNC_EN */
#define CONN_CFG_ADIE_CTL     0x18001900u   /* [1] A-die top clock */
#define CONN_CFG_EMI_BT       0x18001c18u
#define CONN_AFE_DIG_EN_03    0x18003008u   /* [21] BPLL [20] WPLL */
#define CONN_WTSLP_WB_STA     0x18005008u   /* [26] busy */
#define CONN_SYSRAM_RADIO     0x18052834u   /* [0] BT */
#define HOST_BGF_LPCTL        0x18060030u   /* [0] set fw own [1] clr fw own [2] owner state */
#define HOST_BGF_IRQ_STAT     0x18060034u
#define HOST_SLP_PROT_CTRL    0x18060184u   /* [5] on2off slp prot ack */
#define HOST_WAKEUP_BT        0x180601a8u   /* [0] BT vote */
#define HOST_REMAP_MCU_EMI    0x180601c4u
#define HOST_REMAP_BT_PERI    0x180601d8u
#define HOST_MCU_PC           0x1806022cu
#define HOST_BUS_TIMEOUT_IRQ  0x180602d4u
#define SEMA_OWN(i)           (0x18072000u + 4u * (i))
#define SEMA_REL(i)           (0x18072200u + 4u * (i))
#define BGF_MCCR              0x18800100u
#define BGF_MCCR_SET          0x18800104u
#define BGF_SW_DBG_CTL        0x1880016cu
#define BGF_HW_VER            0x18812000u
#define BGF_FW_VER            0x18812004u
#define BGF_HW_CODE           0x18812008u
#define BGF_IP_VER            0x18812010u
#define BTIF_RBR_THR          0x1100c000u
#define BTIF_IER              0x1100c004u
#define BTIF_FIFOCTRL         0x1100c008u
#define BTIF_FAKELCR          0x1100c00cu
#define BTIF_LSR              0x1100c014u
#define BTIF_SLEEP_EN         0x1100c048u
#define BTIF_DMA_EN           0x1100c04cu
#define BTIF_TRI_LVL          0x1100c060u
#define BTIF_WAK              0x1100c064u
#define BTIF_HANDSHAKE        0x1100c06cu

#define CONN_EMI_PA           0x8fb00000u
#define EMI_MCU_START         0u
#define EMI_MCU_SIZE          0x100000u
#define EMI_BT_START          0x100000u
#define EMI_BT_SIZE           0x129400u
#define N2BT_FW_DIR           "/usr/share/firmware/nord2-bt/"

/* ---------------- semaphores (M2 = the AP) ---------------- */
static bool
n2bt_sema_get(unsigned idx)
{
	for (unsigned i = 0; i < 2000; i++) {          /* consys_sema_acquire_timeout: one side-effect read per try */
		if (rd(SEMA_OWN(idx)) & 1u) {
			return true;
		}
		IODelay(1);
	}
	BT0("semaphore %u not acquired in 2 ms\n", idx);
	return false;
}

static void n2bt_sema_put(unsigned idx) { wr(SEMA_REL(idx), 1u); }

/* ---------------- votes ---------------- */
static bool
n2bt_wake(void)
{
	setb(HOST_WAKEUP_BT, 1u);
	for (unsigned i = 0; i < 100; i++) {
		if ((rd(HOST_SLP_PROT_CTRL) & (1u << 5)) == 0) {
			return true;
		}
		IOSleep(1);
	}
	BT0("conn_infra did not wake for the BT vote (0x18060184 = %#x)\n", rd(HOST_SLP_PROT_CTRL));
	return false;
}

/* bt9: kern.n2bt_keepvote (default 1): once the bridge runs, the BT wake vote stays set (re-asserted every 100 ms by the housekeeping)
 * and n2bt_unwake leaves it alone. Linux releases it after power-on (bgfsys_power_on "release conn_infra force on") because its
 * conninfra driver keeps conn_infra up; here nothing guarantees that while BT runs [I]. */
static uint32_t n2bt_keepvote = 1, n2bt_vote_hold, n2bt_vote_fixes;
static void n2bt_unwake(void) { if (n2bt_keepvote && n2bt_vote_hold) return; clrb(HOST_WAKEUP_BT, 1u); }
static void n2bt_ccif_on(void);
static void n2bt_cal_backup(void);
static void n2bt_blank_on(void);

static bool
n2bt_ap_predicates(char *why, size_t n)
{
	uint32_t pwrcon = rd(SPM_CONN_PWR_CON), s1 = rd(SPM_PWR_STATUS), s2 = rd(SPM_PWR_STATUS_2ND);
	uint32_t gals = rd(AP_GALS_CONN), p1 = rd(INFRA_PROT_STA1), p2 = rd(INFRA_PROT_STA_VDNR);
	bool ok = (pwrcon & 0x1fu) == 0xdu && (s1 & 2u) && (s2 & 2u) && (gals & 1u) && !(p1 & (1u << 13)) && !(p2 & (1u << 2));

	snprintf(why, n, "pwrcon %#x sta %#x/%#x gals %#x prot %#x/%#x", pwrcon, s1, s2, gals, p1, p2);
	return ok;
}

/* BPLL/WPLL votes: set only what is clear; remember what we set to clear exactly that */
static uint32_t n2bt_pll_ours;

static void
n2bt_bus_clock(bool on)
{
	if (!n2bt_sema_get(20)) {
		return;
	}
	if (on) {
		uint32_t v = rd(CONN_AFE_DIG_EN_03);
		n2bt_pll_ours = ~v & ((1u << 21) | (1u << 20));
		if (n2bt_pll_ours & (1u << 21)) { setb(CONN_AFE_DIG_EN_03, 1u << 21); IODelay(30); }
		if (n2bt_pll_ours & (1u << 20)) { setb(CONN_AFE_DIG_EN_03, 1u << 20); IODelay(50); }
	} else if (n2bt_pll_ours) {
		if (n2bt_pll_ours & (1u << 20)) { clrb(CONN_AFE_DIG_EN_03, 1u << 20); }
		if (n2bt_pll_ours & (1u << 21)) { clrb(CONN_AFE_DIG_EN_03, 1u << 21); }
		n2bt_pll_ours = 0;
	}
	n2bt_sema_put(20);
}

static void
n2bt_radio_status(bool on)
{
	if (!n2bt_sema_get(19)) {
		return;
	}
	if (on) { setb(CONN_SYSRAM_RADIO, 1u); } else { clrb(CONN_SYSRAM_RADIO, 1u); }
	n2bt_sema_put(19);
}

/* ---------------- firmware ---------------- */
struct n2bt_fw {
	const char *name;
	uint32_t emi_start, emi_size;
	uint8_t *buf;
	uint32_t len;
};

static int
n2bt_read_file_sz(const char *path, off_t minsz, off_t maxsz, uint8_t **out, uint32_t *outlen)
{
	vfs_context_t ctx = vfs_context_kernel();
	vnode_t vp = NULLVP;
	off_t size = 0;
	int err = vnode_open(path, FREAD, 0, 0, &vp, ctx);

	*out = NULL;
	*outlen = 0;
	if (err != 0) {
		return err;
	}
	{
		struct vnode_attr va;
		VATTR_INIT(&va);
		VATTR_WANTED(&va, va_data_size);
		err = vnode_getattr(vp, &va, ctx);
		size = (off_t)va.va_data_size;
	}
	if (err == 0 && (size < minsz || size > maxsz)) {
		err = EFBIG;
	}
	if (err == 0) {
		uint8_t *buf = kalloc_data((vm_size_t)size, Z_WAITOK | Z_ZERO);
		uio_t uio = buf ? uio_create(1, 0, UIO_SYSSPACE, UIO_READ) : NULL;

		if (buf == NULL || uio == NULL) {
			err = ENOMEM;
		} else {
			uio_addiov(uio, CAST_USER_ADDR_T(buf), (user_size_t)size);
			err = VNOP_READ(vp, uio, 0, ctx);
			if (err == 0 && uio_resid(uio) != 0) {
				err = EIO;
			}
		}
		if (uio) {
			uio_free(uio);
		}
		if (err == 0) {
			*out = buf;
			*outlen = (uint32_t)size;
		} else if (buf) {
			kfree_data(buf, (vm_size_t)size);
		}
	}
	vnode_close(vp, FREAD, ctx);
	return err;
}

static int
n2bt_read_file(const char *path, uint8_t **out, uint32_t *outlen)
{
	return n2bt_read_file_sz(path, 96, 2 * 1024 * 1024, out, outlen);
}

/* header: date[16] plat[4] hw_ver u16 sw_ver u16 emi_addr[4] subsys u32 rsv[14] crc u16 = 48 bytes, then the body */
static int
n2bt_fw_check(const struct n2bt_fw *f, uint32_t *offset)
{
	const uint8_t *h = f->buf;
	uint32_t off, body;

	if (f->len < 48 + 64 || memcmp(h + 16, "ALPS", 4) != 0 || h[20] != 0x8a || h[27] != 0xf0) {
		return EINVAL;
	}
	off = ((uint32_t)h[26] << 16) | ((uint32_t)h[25] << 8);
	body = f->len - 48;
	if ((body & 3u) || off < f->emi_start || off + body > f->emi_start + f->emi_size) {
		return ERANGE;
	}
	*offset = off;
	return 0;
}

static int
n2bt_fw_place(struct n2bt_fw *f)
{
	uint32_t off = 0, body;
	vm_offset_t va;
	int err = n2bt_fw_check(f, &off);

	if (err) {
		BT0("S4 %s: header refused (%d): plat %.4s hw %02x emi %02x%02x%02x%02x len %u\n", f->name, err, f->buf + 16,
		    f->buf[20], f->buf[27], f->buf[26], f->buf[25], f->buf[24], f->len);
		return err;
	}
	body = f->len - 48;
	BT0("S4 %s: built %.14s, %u bytes -> EMI %#x (pa %#x)\n", f->name, f->buf, body, off, CONN_EMI_PA + off);
	n2bt_crumb("S4 map EMI %#x +%#x (kernel_map, Device)", CONN_EMI_PA + off, (unsigned)round_page(body));
	va = ml_io_map_unmappable(CONN_EMI_PA + off, round_page(body), VM_WIMG_IO);
	if (va == 0) {
		n2bt_crumb("S4 EMI map failed");
		return ENOMEM;
	}
	n2bt_crumb("S4 copy %u bytes into EMI", body);
	{
		const uint8_t *src = f->buf + 48;
		volatile uint32_t *dst = (volatile uint32_t *)va;
		uint32_t mism = 0;

		for (uint32_t i = 0; i < body / 4; i++) {
			uint32_t w;
			memcpy(&w, src + 4 * i, 4);
			dst[i] = w;
		}
		__asm__ volatile ("dsb sy" ::: "memory");
		for (uint32_t i = 0; i < body / 4; i++) {
			uint32_t w;
			memcpy(&w, src + 4 * i, 4);
			mism += dst[i] != w;
		}
		ml_io_unmap(va, round_page(body));
		if (mism) {
			BT0("S4 %s: read-back of the EMI copy differs in %u words\n", f->name, mism);
			return EIO;
		}
	}
	return 0;
}

/* ---------------- BGFSYS power ---------------- */
static const char *n2bt_step = "none";
#define STEP(s) do { n2bt_step = (s); n2bt_crumb("%s", (s)); } while (0)

static void
n2bt_bgf_off(void)
{
	/* bgfsys_power_off order; best effort, each poll bounded */
	BT0("BGF off: sleep protection on, function off, CPU reset\n");
	if (!n2bt_wake()) {
		return;
	}
	setb(CONN_CFG_BT2CONN_GALS, 1u << 0); (void)n2bt_poll(CONN_CFG_BT2CONN_GALS, 1u << 3, 1u << 3, 100, 500);
	setb(CONN_CFG_BT2CONN_GALS, 1u << 4); (void)n2bt_poll(CONN_CFG_BT2CONN_GALS, 1u << 7, 1u << 7, 100, 500);
	setb(CONN_CFG_CONN2BT_GALS, 1u << 0); (void)n2bt_poll(CONN_CFG_CONN2BT_GALS, 1u << 3, 1u << 3, 100, 500);
	setb(CONN_CFG_CONN2BT_GALS, 1u << 4); (void)n2bt_poll(CONN_CFG_CONN2BT_GALS, 1u << 7, 1u << 7, 100, 500);
	n2bt_bus_clock(false);
	clrb(CONN_CFG_BT_PWRCTLCR0, 1u);
	clrb(CONN_RGU_BGF_CPU_RST, 1u);
	clrb(CONN_RGU_BGF_SW_RST, 1u);
	setb(CONN_CFG_EMI_BT, 1u); clrb(CONN_CFG_EMI_BT, 1u); clrb(CONN_CFG_EMI_BT, 2u);
	n2bt_radio_status(false);
	n2bt_bgf_ours = 0;
	n2bt_unwake();
}

static int
n2bt_bgf_on(void)
{
	uint32_t v;

	if ((rd(CONN_RGU_BGF_TOP_PWR) & 0x80u) == 0) {
		STEP("S5 BGF top power on (key 0x4254, not yet set by GNSS)");
		wr(CONN_RGU_BGF_TOP_PWR, (rd(CONN_RGU_BGF_TOP_PWR) & ~0xffff0080u) | 0x42540000u | 0x80u);
	}
	STEP("S5 CPU reset, bus clocks, BT_FUNC_EN");
	clrb(CONN_RGU_BGF_CPU_RST, 1u);
	n2bt_bus_clock(true);
	setb(CONN_CFG_BT_PWRCTLCR0, 1u);
	n2bt_bgf_ours = 1;
	if (!n2bt_poll(CONN_RGU_BGF_ON_ACK, 3u << 24, 3u << 24, 100, 500)) { n2bt_step = "S5 on-ack"; return EIO; }
	if (!n2bt_poll(CONN_RGU_BGF_OFF_ACK, 1u << 24, 1u << 24, 100, 500)) { n2bt_step = "S5 off-ack-b"; return EIO; }
	if (!n2bt_poll(CONN_RGU_BGF_OFF_ACK, 3u, 3u, 100, 500)) { n2bt_step = "S5 off-ack-s"; return EIO; }
	STEP("S5 GALS sleep protection off");
	clrb(CONN_CFG_CONN2BT_GALS, 1u << 4); if (!n2bt_poll(CONN_CFG_CONN2BT_GALS, 1u << 7, 0, 100, 500)) { n2bt_step = "S5 conn2bt rx"; return EIO; }
	clrb(CONN_CFG_CONN2BT_GALS, 1u << 0); if (!n2bt_poll(CONN_CFG_CONN2BT_GALS, 1u << 3, 0, 100, 500)) { n2bt_step = "S5 conn2bt tx"; return EIO; }
	clrb(CONN_CFG_BT2CONN_GALS, 1u << 4); if (!n2bt_poll(CONN_CFG_BT2CONN_GALS, 1u << 7, 0, 100, 500)) { n2bt_step = "S5 bt2conn rx"; return EIO; }
	clrb(CONN_CFG_BT2CONN_GALS, 1u << 0); if (!n2bt_poll(CONN_CFG_BT2CONN_GALS, 1u << 3, 0, 100, 500)) { n2bt_step = "S5 bt2conn tx"; return EIO; }
	IODelay(440);
	if (!n2bt_poll(BGF_HW_VER, 0xffffffffu, 0x8a00u, 10, 550)) { n2bt_step = "S5 bgf hw version"; return EIO; }
	BT0("S5 BGF ids hw %#x fw %#x code %#x ip %#x\n", rd(BGF_HW_VER), rd(BGF_FW_VER), rd(BGF_HW_CODE), rd(BGF_IP_VER));
	if (rd(BGF_FW_VER) != 0x8a00u || rd(BGF_HW_CODE) != 0 || rd(BGF_IP_VER) != 0x20010000u) { n2bt_step = "S5 bgf ids"; return EIO; }
	clrb(BGF_MCCR, 1u << 31);
	wr(BGF_MCCR_SET, 1u << 2);
	clrb(CONN_RGU_BGF_SW_RST, 1u);
	STEP("S5 BGF MCU released");
	setb(CONN_RGU_BGF_CPU_RST, 1u);
	n2bt_bus_clock(false);
	for (unsigned i = 0; i < 100; i++) {
		uint32_t pc;
		v = rd(BGF_SW_DBG_CTL);
		pc = rd(HOST_MCU_PC);
		/* bt11: 0x1D1E alone can be stale (bt30a restart 2: pc 0xf0006f30); the ROM idle loop runs at 0x1005xx */
		if (v == 0x1d1eu && pc >= 0x100000u && pc < 0x200000u) {
			BT0("S5 MCU ROM idle (0x1D1E) after %u x 5 ms, pc %#x\n", i, pc);
			return 0;
		}
		if (v == 0xdeadfeedu && i >= 20) {
			BT0("S5 BGF bus answers 0xdeadfeed after %u x 5 ms (pc %#x): not ready\n", i, pc);
			break;
		}
		IOSleep(5);
	}
	BT0("S5 MCU ROM never idle: sw_dbg %#x pc %#x lpctl %#x\n", v, rd(HOST_MCU_PC), rd(HOST_BGF_LPCTL));
	n2bt_step = "S5 mcu rom";
	return ETIMEDOUT;
}

/* ---------------- BTIF (PIO) ---------------- */
static int
n2bt_btif_init(void)
{
	wr(INFRA_CG0_CLR, 1u << 31);
	IODelay(10);
	if (rd(INFRA_CG0_STA) & (1u << 31)) {
		BT0("S6 BTIF clock still gated (CG0 sta %#x)\n", rd(INFRA_CG0_STA));
		return EIO;
	}
	wr(BTIF_FAKELCR, 0);
	wr(BTIF_HANDSHAKE, rd(BTIF_HANDSHAKE) | 1u);  /* new handshake */
	wr(BTIF_TRI_LVL, 0x18);                       /* TX 8, RX 1 (hal_btif_hw_init) */
	wr(BTIF_FIFOCTRL, 1u << 1); wr(BTIF_FIFOCTRL, 0);
	wr(BTIF_FIFOCTRL, 1u << 2); wr(BTIF_FIFOCTRL, 0);
	wr(BTIF_DMA_EN, 1u << 2);                     /* no DMA, auto reset */
	wr(BTIF_SLEEP_EN, 0);
	wr(BTIF_IER, 0);                              /* polled */
	BT0("S6 BTIF on (PIO): CG0 sta %#x LSR %#x\n", rd(INFRA_CG0_STA), rd(BTIF_LSR));
	/* bt20: the BTIF register set after init (BTIF is the AP<->BGF FIFO bridge: no baud or sample-count registers; Linux btif_priv.h) */
	n2bt_crumb("S6b BTIF IER %#x FAKELCR %#x LSR %#x SLEEP_EN %#x DMA_EN %#x TRI_LVL %#x WAK %#x HANDSHAKE %#x; mode %u", rd(BTIF_IER),
	    rd(BTIF_FAKELCR), rd(BTIF_LSR), rd(BTIF_SLEEP_EN), rd(BTIF_DMA_EN), rd(BTIF_TRI_LVL), rd(BTIF_WAK), rd(BTIF_HANDSHAKE), n2bt_tx_mode);
	return 0;
}

/* driver own: btmtk_cif_fw_own_clr */
static int
n2bt_own(void)
{
	for (unsigned i = 0; i < 4096; i++) {
		if ((i & 0xf) == 0) {
			wr(HOST_BGF_LPCTL, 1u << 1);
		}
		if ((rd(HOST_BGF_LPCTL) & (1u << 2)) == 0) {
			wr(HOST_BGF_IRQ_STAT, 1u);
			n2bt_st.own_clr++;
			return 0;
		}
		IODelay(500);
	}
	BT0("driver own not granted: lpctl %#x pc %#x\n", rd(HOST_BGF_LPCTL), rd(HOST_MCU_PC));
	return ETIMEDOUT;
}

static void n2bt_rx_drain(void);
static unsigned n2bt_rx_have;
static uint32_t n2bt_wak = 0;
static uint32_t n2bt_tx_timeout_ms = 200, n2bt_tx_fail_at, n2bt_tx_fail_n, n2bt_tx_fail_lsr, n2bt_tx_fail_us;   /* bt19 */
static uint32_t n2bt_reset_req;
/* bt20: transport mode auto-bisect */
/* bt21 [V phone bt39a]: mode 0 lived 353 s, mode 1 8.9 s, mode 2 streamed AAC without an assert from 438 s on -> mode 2 by default */
static int n2bt_mode = 2;                        /* kern.n2bt_mode: 2 default (fixed), -1 auto (order 2, 4, 5, 0, 1, 3), 0..5 fixed */
uint32_t n2bt_tx_mode = 2;                       /* in effect now */
uint32_t n2bt_poll_idle_us = 100;                /* bt21: poll period with a link up but no traffic for 500 ms */
static uint32_t n2bt_mode_tried, n2bt_mode_ms[6], n2bt_mode_settled;
uint32_t n2bt_poll_us = 100;
uint32_t n2bt_tx_bps = 1000000;
uint32_t n2bt_a2dp_filter = 0;                     /* bt18: DIAGNOSTIC only (1 hide AAC, 2 hide all but SBC); default off */                    /* bt17: ACL bit rate cap to the controller (bluetoothd -> BTIF); 0 = none */                       /* bt11: bridge RX poll period while a link is up or traffic is recent */
static int
n2bt_btif_write(const uint8_t *b, unsigned n)
{
	unsigned at = 0;

	if ((rd(HOST_BGF_LPCTL) & (1u << 2)) && n2bt_own() != 0) {
		return EIO;
	}
	/* bt11 [V phone bt30a]: the firmware asserted (hcit_mtk_uart.c #500, its BTIF ISR) right when its events (link key request,
	 * completed packets) met our Link Key Reply. The AP-side RX FIFO holds only 8 bytes (Linux btif_priv.h BTIF_RX_FIFO_SIZE) and
	 * Linux empties it by interrupt/DMA; we poll. Before writing, everything the controller has queued is pulled out. */
	if (rd(BTIF_LSR) & 1u) {
		unsigned before = n2bt_rx_have;
		n2bt_rx_drain();
		n2bt_st.rx_at_tx++;
		n2bt_st.rx_in_tx += n2bt_rx_have - before;
	}
	/* bt10 [V phone bt29a]: the firmware asserted in its BTIF transport ISR ("<ASSERT> system/transport/hcit_mtk_uart.c #500 ...
	 * id=0x4 idle, isr=") on the first command after ~7 s without traffic. We pulsed BTIF_WAK low for 2 us before EVERY write
	 * (HakiOS/connac1 habit); hal_btif_raise_wak_sig wants > 1/32k s (128-160 us), and the connac2 driver never calls it at all
	 * [V Linux mtk_btif.c: btif_raise_wak_signal has no caller; BT wake-up is the LPCTL own handshake]. A 2 us glitch on the
	 * "AP wakes consys" line while the firmware idles on the 32 kHz clock is the prime suspect [I].
	 * kern.n2bt_wak: 0 (default) never touch BTIF_WAK, as Linux; 1 the old 2 us pulse; 2 a Linux-length 150 us pulse. */
	if (n2bt_wak == 1) {
		wr(BTIF_WAK, rd(BTIF_WAK) & ~1u);
		IODelay(2);
		wr(BTIF_WAK, rd(BTIF_WAK) | 1u);
	} else if (n2bt_wak == 2) {
		wr(BTIF_WAK, rd(BTIF_WAK) & ~1u);
		IODelay(150);
		wr(BTIF_WAK, rd(BTIF_WAK) | 1u);
	}
	/* bt20: transport mode (kern.n2bt_mode, -1 = auto-bisect, see n2bt_mode_next):
	 *  0 = bt36a: per 16-byte chunk wait for TEMT (IODelay(1) + RX drain), 30 us gap between chunks of long packets
	 *  1 = as 0 (the bridge adds the ACL credit gate)
	 *  2 = 8-byte chunks on THRE (spin), RX drain after every chunk, no gap
	 *  3 = bt19 tight burst: 16/8 bytes on TEMT/THRE, byte drain of the RX FIFO only when LSR says data
	 *  4 = byte by byte, RX drain before every byte
	 *  5 = 4-byte chunks on THRE, RX drain after every chunk */
	{
		volatile uint32_t *lsr = n2bt_reg(BTIF_LSR);
		volatile uint8_t *thr = (volatile uint8_t *)n2bt_reg(BTIF_RBR_THR);
		uint64_t t0 = mach_absolute_time(), lim = 0;
		if (n2bt_mode >= 0 && n2bt_mode <= 5) n2bt_tx_mode = (uint32_t)n2bt_mode;   /* bt21: a fixed mode set by sysctl applies at once */
		unsigned mode = n2bt_tx_mode;
		nanoseconds_to_absolutetime((uint64_t)n2bt_tx_timeout_ms * 1000000ull, &lim);
		while (at < n) {
			uint32_t l = *lsr;
			unsigned room;
			if (mode <= 1) room = (l & (1u << 6)) ? 16u : 0u;
			else if (mode == 2) room = (l & (1u << 5)) ? 8u : 0u;
			else if (mode == 3) room = (l & (1u << 6)) ? 16u : (l & (1u << 5)) ? 8u : 0u;
			else if (mode == 4) room = (l & (1u << 5)) ? 1u : 0u;
			else room = (l & (1u << 5)) ? 4u : 0u;
			if ((l & 1u) || mode == 4) {
				unsigned before = n2bt_rx_have;
				n2bt_rx_drain();
				n2bt_st.rx_in_tx += n2bt_rx_have - before;
			}
			if (room == 0) {
				uint64_t now = mach_absolute_time();
				if (now - t0 > lim) {
					uint64_t ns = 0;
					absolutetime_to_nanoseconds(now - t0, &ns);
					n2bt_st.tx_timeouts++;
					n2bt_tx_fail_at = at; n2bt_tx_fail_n = n; n2bt_tx_fail_lsr = l; n2bt_tx_fail_us = (uint32_t)(ns / 1000ull);
					wr(BTIF_FIFOCTRL, 1u << 2); wr(BTIF_FIFOCTRL, 0);
					if (at > 0) n2bt_reset_req = 1;
					return ETIMEDOUT;
				}
				if (mode <= 1) IODelay(1);
				continue;
			}
			for (unsigned k = 0; k < room && at < n; k++) *thr = b[at++];
			if (mode == 2 || mode == 5) { unsigned before = n2bt_rx_have; n2bt_rx_drain(); n2bt_st.rx_in_tx += n2bt_rx_have - before; }
			if (mode <= 1) {
				if (*lsr & 1u) n2bt_rx_drain();
				if (n > 64 && at < n) IODelay(30);
			}
		}
		uint64_t ns = 0;
		absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
		if (ns / 1000ull > n2bt_st.tx_wait_max) n2bt_st.tx_wait_max = (uint32_t)(ns / 1000ull);
	}
	return 0;
}

/* RX reassembly: bytes from the BTIF FIFO into whole H4 packets */
#define N2BT_RXBUF 4096u
static uint8_t n2bt_rx[N2BT_RXBUF];

static unsigned
n2bt_h4_len(const uint8_t *b, unsigned have)
{
	if (have < 1) return 0;
	switch (b[0]) {
	case 1: return have >= 4 ? 4u + b[3] : 0;
	case 2: return have >= 5 ? 5u + (b[3] | ((unsigned)b[4] << 8)) : 0;
	case 3: return have >= 4 ? 4u + b[3] : 0;
	case 4: return have >= 3 ? 3u + b[2] : 0;
	case 5: return have >= 5 ? 5u + ((b[3] | ((unsigned)b[4] << 8)) & 0x3fff) : 0;
	default: return 0;
	}
}

static void
n2bt_rx_drain(void)
{
	while ((rd(BTIF_LSR) & 1u) && n2bt_rx_have < N2BT_RXBUF) {
		n2bt_rx[n2bt_rx_have++] = (uint8_t)(rd(BTIF_RBR_THR) & 0xff);
	}
	if (n2bt_rx_have == N2BT_RXBUF && (rd(BTIF_LSR) & 1u)) {
		n2bt_st.rx_overrun++;
	}
}

int
n2bt_hci_receive(uint8_t *h4, unsigned room, unsigned wait_ms)
{
	uint64_t deadline = 0, now;

	clock_interval_to_deadline(wait_ms, NSEC_PER_MSEC, &deadline);
	for (;;) {
		unsigned n;

		n2bt_rx_drain();
		while (n2bt_rx_have > 0 && (n2bt_rx[0] < 2 || n2bt_rx[0] > 5 || n2bt_rx[0] == 1)) {
			/* not a controller packet type: resync one byte at a time */
			memmove(n2bt_rx, n2bt_rx + 1, --n2bt_rx_have);
			n2bt_st.rx_resync++;
		}
		n = n2bt_h4_len(n2bt_rx, n2bt_rx_have);
		if (n != 0 && n > N2BT_RXBUF) {
			n2bt_rx_have = 0;
			n2bt_st.rx_resync++;
			continue;
		}
		if (n != 0 && n <= n2bt_rx_have) {
			if (n > room) {
				memmove(n2bt_rx, n2bt_rx + n, n2bt_rx_have - n);
				n2bt_rx_have -= n;
				return -EMSGSIZE;
			}
			memcpy(h4, n2bt_rx, n);
			memmove(n2bt_rx, n2bt_rx + n, n2bt_rx_have - n);
			n2bt_rx_have -= n;
			n2bt_st.rx_pkts++;
			n2bt_st.rx_bytes += n;
			return (int)n;
		}
		now = mach_absolute_time();
		if (now >= deadline) {
			return 0;
		}
		if (rd(BTIF_LSR) & 1u) {
			continue;
		}
		IODelay(100);
	}
}

/* bt20: called by the bridge at every controller restart with how long the current mode lived. Auto: 0, 1, ... 5, then the
 * longest survivor for good. Fixed (kern.n2bt_mode 0..5): that mode. Returns the mode now in effect. */
uint32_t
n2bt_mode_next(uint32_t survived_ms, uint32_t acl_tx, uint32_t acl_rx)
{
	uint32_t cur = n2bt_tx_mode;
	if (n2bt_mode >= 0 && n2bt_mode <= 5 && (uint32_t)n2bt_mode != cur) cur = n2bt_tx_mode = (uint32_t)n2bt_mode;   /* sysctl changed */
	n2bt_crumb("S12m mode %u survived %u ms (acl tx %u rx %u) before the restart", cur, survived_ms, acl_tx, acl_rx);
	if (cur < 6 && survived_ms > n2bt_mode_ms[cur]) n2bt_mode_ms[cur] = survived_ms;
	n2bt_mode_tried |= 1u << cur;
	if (n2bt_mode >= 0 && n2bt_mode <= 5) { n2bt_tx_mode = (uint32_t)n2bt_mode; return n2bt_tx_mode; }
	if (!n2bt_mode_settled && n2bt_mode_tried != 0x3fu) {
		static const uint32_t order[6] = { 2, 4, 5, 0, 1, 3 };   /* bt21: mode 2's neighbours first */
		uint32_t nx = cur;
		for (unsigned o = 0; o < 6; o++) if (!(n2bt_mode_tried & (1u << order[o]))) { nx = order[o]; break; }
		n2bt_tx_mode = nx;
	} else {
		uint32_t best = 0;
		for (uint32_t m = 1; m < 6; m++) if (n2bt_mode_ms[m] > n2bt_mode_ms[best]) best = m;
		if (!n2bt_mode_settled) n2bt_crumb("S12m all modes tried: keeping mode %u (best survival %u ms)", best, n2bt_mode_ms[best]);
		n2bt_mode_settled = 1;
		n2bt_tx_mode = best;
	}
	n2bt_crumb("S12m next transport mode %u", n2bt_tx_mode);
	return n2bt_tx_mode;
}

void
n2bt_tx_fail(uint32_t out[4])
{
	out[0] = n2bt_tx_fail_at; out[1] = n2bt_tx_fail_n; out[2] = n2bt_tx_fail_lsr; out[3] = n2bt_tx_fail_us;
}

void
n2bt_counters(uint32_t out[6])
{
	out[0] = n2bt_st.rx_resync; out[1] = n2bt_st.rx_overrun; out[2] = n2bt_st.tx_timeouts;
	out[3] = n2bt_st.tx_wait_max; out[4] = n2bt_st.rx_in_tx; out[5] = n2bt_st.rx_at_tx;
}

void
n2bt_rx_debug(uint32_t *have, uint32_t *lsr, uint8_t first[4])
{
	*have = n2bt_rx_have;
	*lsr = rd(BTIF_LSR);
	for (unsigned i = 0; i < 4; i++) first[i] = i < n2bt_rx_have ? n2bt_rx[i] : 0;
}

int
n2bt_hci_send(const uint8_t *h4, unsigned n)
{
	int err = n2bt_btif_write(h4, n);
	if (err == 0) {
		n2bt_st.tx_pkts++;
		n2bt_st.tx_bytes += n;
	}
	return err;
}

static void
n2bt_dump(const char *dir, const uint8_t *b, unsigned n)
{
	char t[3 * 24 + 1];
	unsigned k = n < 24 ? n : 24;
	for (unsigned i = 0; i < k; i++) {
		snprintf(t + 3 * i, 4, " %02x", b[i]);
	}
	t[3 * k] = 0;
	BT0("%s %u:%s%s\n", dir, n, t, n > k ? " ..." : "");
}

/* one command, wait for its Command Complete (or for a WMT event when wmt_op != 0); returns status or -1 */
static uint8_t n2bt_pkt[1100];

static int
n2bt_cmd(const uint8_t *cmd, unsigned n, uint16_t opcode, uint8_t wmt_op, unsigned wait_ms, uint8_t *ret, unsigned retroom)
{
	uint64_t deadline;
	n2bt_dump("tx", cmd, n);
	if (n2bt_hci_send(cmd, n) != 0) {
		return -1;
	}
	clock_interval_to_deadline(wait_ms, NSEC_PER_MSEC, &deadline);
	while (mach_absolute_time() < deadline) {
		int len = n2bt_hci_receive(n2bt_pkt, sizeof(n2bt_pkt), 20);
		if (len <= 0) {
			continue;
		}
		n2bt_dump("rx", n2bt_pkt, (unsigned)len);
		if (wmt_op && len >= 8 && n2bt_pkt[0] == 4 && n2bt_pkt[1] == 0xe4 && n2bt_pkt[3] == 0x02 && n2bt_pkt[4] == wmt_op) {
			unsigned plen = n2bt_pkt[5] | ((unsigned)n2bt_pkt[6] << 8);
			return plen >= 1 && (unsigned)len >= 7 + plen ? n2bt_pkt[7] : -1;
		}
		if (!wmt_op && len >= 7 && n2bt_pkt[0] == 4 && n2bt_pkt[1] == 0x0e && n2bt_pkt[4] == (opcode & 0xff) && n2bt_pkt[5] == (opcode >> 8)) {
			if (ret) {
				unsigned k = (unsigned)len - 7;
				memcpy(ret, n2bt_pkt + 7, k < retroom ? k : retroom);
			}
			return n2bt_pkt[6];
		}
		if (!wmt_op && len >= 7 && n2bt_pkt[0] == 4 && n2bt_pkt[1] == 0x0f && n2bt_pkt[5] == (opcode & 0xff) && n2bt_pkt[6] == (opcode >> 8) && n2bt_pkt[3] != 0) {
			return n2bt_pkt[3];   /* Command Status with an error */
		}
	}
	BT0("opcode %#06x: no answer in %u ms\n", wmt_op ? 0xfc6f : opcode, wait_ms);
	return -1;
}

static int
n2bt_hci0(uint16_t op, uint8_t *ret, unsigned room)
{
	const uint8_t c[4] = { 1, (uint8_t)op, (uint8_t)(op >> 8), 0 };
	return n2bt_cmd(c, 4, op, 0, 2000, ret, room);
}

static bool
n2bt_addr_ok(const uint8_t a[6])
{
	uint8_t o = 0, x = 0xff;
	for (int i = 0; i < 6; i++) { o |= a[i]; x &= a[i]; }
	return o != 0 && x != 0xff;
}

/* bt4: an address that must never be published: MediaTek's generic default 00:00:46:xx:xx:xx (every Nord 2 reports
 * 00:00:46:67:61:01 without NVRAM) and the reserved inquiry LAPs 9e:8b:00..3f. a[] MSB first. */
static bool
n2bt_addr_publishable(const uint8_t a[6])
{
	return n2bt_addr_ok(a) && !(a[0] == 0x00 && a[1] == 0x00 && a[2] == 0x46) && !(a[3] == 0x9e && a[4] == 0x8b && a[5] < 0x40);
}

/* bt4: "aa:bb:cc:dd:ee:ff" (hex either case), optionally followed by \n / \r / NUL / spaces; MSB first. 0 or EINVAL. */
static int
n2bt_parse_addr(const char *t, size_t n, uint8_t a[6])
{
	size_t len = 0;
	while (len < n && t[len] != 0) len++;
	while (len > 0 && (t[len - 1] == '\n' || t[len - 1] == '\r' || t[len - 1] == ' ')) len--;
	if (len != 17) return EINVAL;
	for (int i = 0; i < 6; i++) {
		unsigned v = 0;
		for (int k = 0; k < 2; k++) {
			char c = t[3 * i + k];
			unsigned d = (c >= '0' && c <= '9') ? (unsigned)(c - '0') : (c >= 'a' && c <= 'f') ? (unsigned)(c - 'a' + 10) :
			    (c >= 'A' && c <= 'F') ? (unsigned)(c - 'A' + 10) : 99u;
			if (d > 15) return EINVAL;
			v = v << 4 | d;
		}
		if (i < 5 && t[3 * i + 2] != ':') return EINVAL;
		a[i] = (uint8_t)v;
	}
	return n2bt_addr_publishable(a) ? 0 : EINVAL;
}

/* bt4: an address made in the kernel when the daemon gave none (n2btd stores it afterwards from kern.n2bt_addr) */
static void
n2bt_make_addr(uint8_t a[6])
{
	do {
		read_random(a, 6);
		a[0] &= 0xfc;
	} while (!n2bt_addr_publishable(a));
}

/* bt4 (0721 uses it after every HCI_Reset bluetoothd sends): the MediaTek set-address command for the address in use */
int
n2bt_addr_cmd(uint8_t c[10])
{
	if (!n2bt_have_req_addr) return 0;
	c[0] = 0x01; c[1] = 0x1a; c[2] = 0xfc; c[3] = 0x06;
	for (int i = 0; i < 6; i++) c[4 + i] = n2bt_req_addr[5 - i];
	return 10;
}

/* ---------------- bt3: end of BT, quiesce check, monitor ----------------
 * bt18bt2 phone runs: stopping after S5 (BGF on, MCU idle in ROM, no driver own) was fine; stopping after S6/S7 (driver own taken,
 * firmware running) with bt2's abrupt power-off (no WMT off, ownership not handed back, CPU reset) was followed 3-6 min later by a
 * hard reset outside any BT step (the GNSS trace's last line is a COMPLETED read: the hang was not in a GNSS register access).
 * [I] the dead BGF left with a host-own request pending blocks a later conn_infra sleep/wake handshake. bt3: never kill a running
 * firmware abruptly. Stop after S6+ = keep BT powered and hand ownership back (fw own set, as Linux's PSM idles); failure after S6 =
 * Linux order (WMT FUNC_CTRL off, fw own, bgfsys_power_off, our FW_BT A-die vote bit only -- the A-die clock itself and the BGF top
 * power are never switched off: GNSS uses them without the Linux refcount). Then a quiesce check (BGF/GPS power and sleep-protection
 * state; the GNSS-polled registers only when GPS is up and unprotected) and a monitor of the always-on host CSR for
 * kern.n2bt_monitor_s (600 s); the persistent "N2BT END" (kernel guard) is written only after that, so a delayed reset in that window
 * makes the next boot refuse BT. */
static bool n2bt_end_done;
static void
n2bt_end(const char *what)
{
	char t[48];
	int k;
	if (n2bt_end_done) return;
	n2bt_end_done = true;
	k = snprintf(t, sizeof(t), "N2BT END %s\n", what);
	n2bt_crumb_raw(t, (uint32_t)k);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, t, (uint32_t)k);   /* bt8: the ring may overwrite the line above */
}

/* btmtk_cif_fw_own_set: give BGF ownership back to the firmware (its ack: BGF_IRQ_STAT2 bit0, write-1-clear) */
#define HOST_BGF_IRQ_STAT2    0x1806003cu
static bool
n2bt_fw_own_set(void)
{
	for (unsigned i = 0; i < 4000; i++) {
		if ((i & 0xf) == 0) wr(HOST_BGF_LPCTL, 1u << 0);
		if (rd(HOST_BGF_IRQ_STAT2) & 1u) {
			wr(HOST_BGF_IRQ_STAT2, 1u);
			n2bt_owned = false;
			return true;
		}
		IODelay(500);
	}
	return false;
}

static void
n2bt_hand_back(void)
{
	n2bt_crumb("end: fw own set (lpctl %#x)", rd(HOST_BGF_LPCTL));
	bool ok = n2bt_fw_own_set();
	n2bt_crumb("end: fw own %s, lpctl %#x", ok ? "acknowledged" : "NOT acknowledged", rd(HOST_BGF_LPCTL));
}

#define CONN_SYSRAM_ADIE_VOTE 0x18052830u   /* SYSRAM A_DIE_TOP_CK_EN_CTRL, bit 5 = CONNSYS_ADIE_CTL_FW_BT */
static void
n2bt_graceful_off(void)
{
	if (n2bt_func_on) {
		static const uint8_t off[] = { 0x01, 0x6f, 0xfc, 0x06, 0x01, 0x06, 0x02, 0x00, 0x00, 0x00 };
		n2bt_crumb("end: WMT FUNC_CTRL BT off");
		int st = n2bt_cmd(off, sizeof(off), 0xfc6f, 0x06, 3000, NULL, 0);
		n2bt_crumb("end: WMT func off status %d", st);
		n2bt_func_on = false;
	}
	if (n2bt_owned) {
		n2bt_hand_back();
	}
	n2bt_crumb("end: bgfsys power off (Linux order; BGF top power and A-die clock left as they are)");
	n2bt_bgf_off();
	if (n2bt_wake()) {
		if (n2bt_sema_get(19)) {
			uint32_t v = rd(CONN_SYSRAM_ADIE_VOTE);
			if (v & (1u << 5)) clrb(CONN_SYSRAM_ADIE_VOTE, 1u << 5);
			n2bt_sema_put(19);
			n2bt_crumb("end: A-die vote word %#x -> %#x (FW_BT bit only; clock untouched)", v, rd(CONN_SYSRAM_ADIE_VOTE));
		}
		n2bt_unwake();
	}
}

/* what the GNSS side depends on, read right after the end of BT (reads only, under our wake vote) */
static void
n2bt_quiesce(const char *tag)
{
	uint32_t on, off, c2g, g2c, dbg, sel;
	n2bt_crumb("quiesce %s: host lpctl %#x 0x184 %#x votes ap %#x wf %#x bt %#x gps %#x busto %#x pc %#x", tag, rd(HOST_BGF_LPCTL),
	    rd(HOST_SLP_PROT_CTRL), rd(0x180601a0u), rd(0x180601a4u), rd(HOST_WAKEUP_BT), rd(0x180601acu), rd(HOST_BUS_TIMEOUT_IRQ), rd(HOST_MCU_PC));
	if (!n2bt_wake()) {
		n2bt_crumb("quiesce %s: no wake (conn_infra asleep?): conn_infra side not read", tag);
		return;
	}
	on = rd(CONN_RGU_BGF_ON_ACK); off = rd(CONN_RGU_BGF_OFF_ACK);
	c2g = rd(0x18001618u); g2c = rd(0x1800161cu);
	sel = rd(0x180600b0u);
	wr(0x180600b0u, (sel & ~0xffu) | 0x80u);               /* the GNSS driver's own debug selector value */
	dbg = rd(0x18060240u);
	wr(0x180600b0u, sel);
	n2bt_crumb("quiesce %s: bgf top %#x cpu %#x btfunc %#x on_ack %#x off_ack %#x gals c2bt %#x bt2c %#x c2gps %#x gps2c %#x gpsfunc %#x adie %#x vote %#x radio %#x gpsdbg %#x",
	    tag, rd(CONN_RGU_BGF_TOP_PWR), rd(CONN_RGU_BGF_CPU_RST), rd(CONN_CFG_BT_PWRCTLCR0), on, off, rd(CONN_CFG_CONN2BT_GALS),
	    rd(CONN_CFG_BT2CONN_GALS), c2g, g2c, rd(0x18001878u), rd(CONN_CFG_ADIE_CTL), rd(CONN_SYSRAM_ADIE_VOTE), rd(CONN_SYSRAM_RADIO), dbg);
	if ((on & (3u << 24)) == (3u << 24) && !(c2g & 0x88u) && !(g2c & 0x88u) && (dbg & 0xfu) == 2u) {
		n2bt_crumb("quiesce %s: GPS up and unprotected: reading the GNSS-polled registers", tag);
		n2bt_crumb("quiesce %s: 0x18c7300c %#x 0x18c73024 %#x 0x18c1021c %#x 0x18c10224 %#x", tag, rd(0x18c7300cu), rd(0x18c73024u),
		    rd(0x18c1021cu), rd(0x18c10224u));
	} else {
		n2bt_crumb("quiesce %s: GPS not up / protected: GNSS registers not read (that is the GNSS driver's state, not an error)", tag);
	}
	n2bt_unwake();
}

/* always-on host CSR only (no vote): a line on change, a heartbeat every 60 s; drains BTIF if its clock is on */
static void
n2bt_monitor(const char *tag)
{
	uint32_t last[7] = { 0 }, said = 0, drained = 0;
	for (uint32_t t = 0; t <= n2bt_monitor_s; t += 10) {
		uint32_t v[7] = { rd(HOST_BGF_LPCTL), rd(HOST_SLP_PROT_CTRL), rd(0x180601a0u), rd(0x180601a4u), rd(HOST_WAKEUP_BT), rd(0x180601acu),
			rd(HOST_BUS_TIMEOUT_IRQ) };
		if (n2bt_btif_on) {
			while ((rd(BTIF_LSR) & 1u) && drained < 1000000u) { (void)rd(BTIF_RBR_THR); drained++; }
		}
		if ((memcmp(v, last, sizeof(v)) != 0 && said < 30) || t % 60 == 0) {
			n2bt_crumb("mon %s t=%us lpctl %#x 0x184 %#x ap %#x wf %#x bt %#x gps %#x busto %#x pc %#x btif-drained %u", tag, t, v[0], v[1], v[2],
			    v[3], v[4], v[5], v[6], rd(HOST_MCU_PC), drained);
			said++;
			memcpy(last, v, sizeof(v));
		}
		IOSleep(10000);
	}
}

static void
n2bt_settle(const char *what, bool check)
{
	char t[96];
	if (check) n2bt_quiesce(what);
	n2bt_monitor(what);
	if (check) n2bt_quiesce("after-monitor");
	lck_mtx_lock(&n2bt_lock);
	snprintf(t, sizeof(t), "%s:settled", n2bt_state_text);
	lck_mtx_unlock(&n2bt_lock);
	n2bt_set_state(t);
	n2bt_crumb("%s: %u s without reset", t, n2bt_monitor_s);
	n2bt_end(what);
}

/* ---------------- the bring-up thread ---------------- */
static void
n2bt_fail(const char *detail)
{
	char s[96];
	snprintf(s, sizeof(s), "failed:%s%s%s", n2bt_step, detail ? ":" : "", detail ? detail : "");
	n2bt_set_state(s);
	n2bt_crumb("FAILED at %s%s%s", n2bt_step, detail ? ": " : "", detail ? detail : "");
}

static void
n2bt_thread(__unused void *arg, __unused wait_result_t wr_)
{
	struct n2bt_fw fw[2] = {
		{ "soc3_0_ram_mcu_1a_1_hdr.bin", EMI_MCU_START, EMI_MCU_SIZE, NULL, 0 },
		{ "soc3_0_ram_bt_1a_1_hdr.bin", EMI_BT_START, EMI_BT_SIZE, NULL, 0 },
	};
	char why[128];
	uint8_t r[64];
	int st;
	bool woke = false;

	n2bt_step = "S0 guard";
	n2bt_set_state("starting:S0");
	n2bt_crumb_init();
	if (n2bt_prev_reset && !n2bt_rearm) {
		n2bt_crumb("S0 the previous boot reset during the bring-up: REFUSED (kern.n2bt_rearm=1 or reinstall payload bt to retry)");
		n2bt_set_state("refused:previous-boot-reset");
		n2bt_end("refused");
		goto out;
	}
	{
		char t[64];
		int k = snprintf(t, sizeof(t), "N2BT INFLIGHT attempt %u maxstep %u stop_mode %u\n", n2bt_attempts, n2bt_maxstep, n2bt_stop_mode);
		n2bt_end_done = false;
		n2bt_crumb_raw(t, (uint32_t)k);
		n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	}
	/* S1: AP-side reads only (SPM/infra/GALS pages), three consecutive good reads one second apart */
	STEP("S1 AP-side conninfra checks (reads of 0x10006000/0x10001000/0x1020e000 pages)");
	n2bt_set_state("waiting-conninfra");
	for (uint32_t t = 0, good = 0;; t++) {
		bool ok = n2bt_ap_predicates(why, sizeof(why)) && !n2bt_map_failed;
		good = ok ? good + 1 : 0;
		if (good >= 3 || (n2bt_force && !n2bt_map_failed)) {
			n2bt_crumb("S1 %s (%s)", good >= 3 ? "ok x3" : "FORCED", why);
			break;
		}
		if (t == 0 || t % 20 == 0) {
			n2bt_crumb("S1 conninfra not (yet) on: %s", why);
		}
		if (t >= n2bt_wait_s || !n2bt_enable || n2bt_map_failed) {
			n2bt_fail(why);
			goto out;
		}
		IOSleep(1000);
	}
	if (n2bt_maxstep <= 1) goto stop;
	/* S4 first part: read the files before any vote (no hardware involved) */
	n2bt_step = "S4 read firmware";
	for (int i = 0; i < 2; i++) {
		char path[128];
		snprintf(path, sizeof(path), N2BT_FW_DIR "%s", fw[i].name);
		st = n2bt_read_file(path, &fw[i].buf, &fw[i].len);
		BT0("S4 %s: %d (%u bytes)\n", path, st, fw[i].len);
		if (st) {
			n2bt_fail(fw[i].name);
			goto out;
		}
	}
	/* S2 */
	n2bt_set_state("starting:S2");
	STEP("S2 read host CSR 0x180601a0 (Wi-Fi AP vote) and 0x18060184 (slp prot) before any write");
	n2bt_crumb("S2 host 0x1a0 %#x 0x184 %#x 0x1a8 %#x", rd(0x180601a0u), rd(HOST_SLP_PROT_CTRL), rd(HOST_WAKEUP_BT));
	STEP("S2 BT wake vote (write 0x180601a8 bit0)");
	if (!(woke = n2bt_wake())) {
		n2bt_fail(NULL);
		goto out;
	}
	BT0("S2 cfg id %#x remap mcu_emi %#x bt_peri %#x bus_to %#x\n", rd(CONN_CFG_ID), rd(HOST_REMAP_MCU_EMI),
	    rd(HOST_REMAP_BT_PERI), rd(HOST_BUS_TIMEOUT_IRQ));
	if (rd(CONN_CFG_ID) != 0x20010101u) { n2bt_step = "S2 cfg id"; n2bt_fail("not MT6893"); goto out; }
	if ((rd(HOST_REMAP_MCU_EMI) & 0xfffffu) != (CONN_EMI_PA >> 16) || (rd(HOST_REMAP_BT_PERI) & 0xfffffu) != 0x01000u) {
		n2bt_step = "S2 remap";
		n2bt_fail("EMI remap not set (Wi-Fi not up?)");
		goto out;
	}
	if (n2bt_maxstep <= 2) goto stop;
	/* S3 */
	STEP("S3 BGF cold check (conn_infra RGU/CFG reads)");
	BT0("S3 bgf top %#x cpu_rst %#x bt_func %#x adie %#x\n", rd(CONN_RGU_BGF_TOP_PWR), rd(CONN_RGU_BGF_CPU_RST),
	    rd(CONN_CFG_BT_PWRCTLCR0), rd(CONN_CFG_ADIE_CTL));
	if (((rd(CONN_RGU_BGF_CPU_RST) & 1u) || (rd(CONN_CFG_BT_PWRCTLCR0) & 1u)) && !n2bt_bgf_ours) {
		n2bt_fail("BGF MCU already running (not ours)");
		goto out;
	}
	if (n2bt_bgf_ours) {
		n2bt_unwake(); woke = false;
		n2bt_bgf_off();
		if (!(woke = n2bt_wake())) { n2bt_fail(NULL); goto out; }
	}
	if (n2bt_maxstep <= 3) goto stop;
	/* S4 */
	n2bt_set_state("starting:S4");
	for (int i = 0; i < 2; i++) {
		n2bt_step = i ? "S4 place bt" : "S4 place mcu";
		if (n2bt_fw_place(&fw[i]) != 0) { n2bt_fail(NULL); goto out; }
	}
	if (n2bt_maxstep <= 4) goto stop;
	/* S5 */
	n2bt_set_state("starting:S5");
	n2bt_radio_status(true);
	n2bt_ccif_on();
	if ((st = n2bt_bgf_on()) != 0) {
		BT0("S5 failed (%d) at %s: lpctl %#x pc %#x\n", st, n2bt_step, rd(HOST_BGF_LPCTL), rd(HOST_MCU_PC));
		goto fail_off;
	}
	n2bt_unwake(); woke = false;
	if (n2bt_maxstep <= 5) goto stop_off;
	/* S6 */
	n2bt_set_state("starting:S6");
	n2bt_step = "S6 btif";
	if (n2bt_btif_init() != 0) goto fail_off;
	n2bt_btif_on = true;
	n2bt_step = "S6 driver own";
	STEP("S6 driver own");
	n2bt_owned = true;                       /* set before: a timeout may still have left the request pending */
	if (n2bt_own() != 0) goto fail_off;
	n2bt_rx_have = 0;
	if (n2bt_maxstep <= 6) goto stop_off;
	/* S7 */
	n2bt_set_state("starting:S7");
	STEP("S7 WMT FUNC_CTRL BT on");
	{
		static const uint8_t on[] = { 0x01, 0x6f, 0xfc, 0x06, 0x01, 0x06, 0x02, 0x00, 0x00, 0x01 };
		st = n2bt_cmd(on, sizeof(on), 0xfc6f, 0x06, 6000, NULL, 0);
		BT0("S7 WMT func on status %d\n", st);
		n2bt_func_on = st == 0;
		if (st != 0) goto fail_off;
		n2bt_blank_on();
		n2bt_cal_backup();
	}
	if (n2bt_maxstep <= 7) goto stop_off;
	/* S8 */
	n2bt_set_state("starting:S8");
	STEP("S8 HCI probe");
	{
		static const struct { const char *what; uint16_t op; } probe[] = {
			{ "Reset", 0x0c03 }, { "Read_Local_Version", 0x1001 }, { "Read_BD_ADDR", 0x1009 },
			{ "Read_Local_Features", 0x1003 }, { "Read_Buffer_Size", 0x1005 }, { "LE_Read_Buffer_Size", 0x2002 },
		};
		unsigned good = 0;
		for (unsigned i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
			memset(r, 0, sizeof(r));
			st = n2bt_hci0(probe[i].op, r, sizeof(r));
			good += st == 0;
			if (probe[i].op == 0x1001 && st == 0) {
				BT0("S8 HCI ver %u rev %#x LMP ver %u manufacturer %#x subver %#x\n", r[0], r[1] | (r[2] << 8), r[3],
				    r[4] | (r[5] << 8), r[6] | (r[7] << 8));
			}
			if (probe[i].op == 0x1009 && st == 0) {
				memcpy(n2bt_ctl_addr, r, 6);
			}
			if (st != 0) {
				BT0("S8 %s: status %d\n", probe[i].what, st);
			}
		}
		BT0("S8 %u of 6 HCI commands answered with status 0\n", good);
		if (good != 6) goto fail_off;
	}
	if (n2bt_maxstep <= 8) goto stop_off;
	/* S9 */
	n2bt_step = "S9 address";
	n2bt_set_state("starting:S9");
	BT0("S9 controller address %02x:%02x:%02x:%02x:%02x:%02x\n", n2bt_ctl_addr[5], n2bt_ctl_addr[4], n2bt_ctl_addr[3],
	    n2bt_ctl_addr[2], n2bt_ctl_addr[1], n2bt_ctl_addr[0]);
	if (!n2bt_have_req_addr) {
		n2bt_make_addr(n2bt_req_addr);
		n2bt_have_req_addr = true;
		n2bt_chosen_address(n2bt_req_addr, "generated at S9");
		n2bt_crumb("S9 no address from the daemon: generated %02x:%02x:%02x:%02x:%02x:%02x in the kernel (n2btd stores it from kern.n2bt_addr)",
		    n2bt_req_addr[0], n2bt_req_addr[1], n2bt_req_addr[2], n2bt_req_addr[3], n2bt_req_addr[4], n2bt_req_addr[5]);
	}
	{
		uint8_t c[10];
		(void)n2bt_addr_cmd(c);
		n2bt_crumb("S9 MediaTek set address 0xfc1a");
		st = n2bt_cmd(c, sizeof(c), 0xfc1a, 0, 2000, NULL, 0);
		memset(r, 0, sizeof(r));
		if (n2bt_hci0(0x1009, r, sizeof(r)) == 0) memcpy(n2bt_ctl_addr, r, 6);
		BT0("S9 set %02x:%02x:%02x:%02x:%02x:%02x: status %d, controller now %02x:%02x:%02x:%02x:%02x:%02x\n",
		    n2bt_req_addr[0], n2bt_req_addr[1], n2bt_req_addr[2], n2bt_req_addr[3], n2bt_req_addr[4], n2bt_req_addr[5], st,
		    n2bt_ctl_addr[5], n2bt_ctl_addr[4], n2bt_ctl_addr[3], n2bt_ctl_addr[2], n2bt_ctl_addr[1], n2bt_ctl_addr[0]);
	}
	{
		uint8_t now[6];
		bool match;
		for (int i = 0; i < 6; i++) now[i] = n2bt_ctl_addr[5 - i];
		match = memcmp(now, n2bt_req_addr, 6) == 0;
		n2bt_crumb("S9 controller reports %02x:%02x:%02x:%02x:%02x:%02x: %s", now[0], now[1], now[2], now[3], now[4], now[5],
		    match ? "the per-phone address" : "NOT the requested address");
		if (!match || !n2bt_addr_publishable(now)) {
			if (n2bt_publish && n2bt_maxstep > 9) {
				n2bt_fail("controller address not set: never published with the generic MediaTek address");
				goto fail_off;
			}
			n2bt_crumb("S9 (probe stage: continuing, but this controller would NOT be published)");
		}
	}
	for (int i = 0; i < 2; i++) {
		if (fw[i].buf) { kfree_data(fw[i].buf, fw[i].len); fw[i].buf = NULL; }
	}
	if (!n2bt_publish || n2bt_maxstep <= 9) {
		n2bt_set_state("probe-ok");
		n2bt_up_since = mach_absolute_time();
		BT0("controller up, kern.n2bt_publish=0: not shown to iOS (probe stage)\n");
		for (;;) {               /* keep the controller fed: drain and log what it says */
			int len = n2bt_hci_receive(n2bt_pkt, sizeof(n2bt_pkt), 0);
			if (len > 0 && n2bt_trace_left()) n2bt_dump("rx", n2bt_pkt, (unsigned)len);
			if (len <= 0) IOSleep(10);
			n2bt_housekeeping();
			if (n2bt_publish && n2bt_maxstep > 9) break;
		}
	}
	/* S10 */
	n2bt_up_since = mach_absolute_time();
	n2bt_end("bridge");                         /* bt9: the hardware bring-up is over; a run-time hang must not refuse the next boot */
	if (n2bt_keepvote) {
		n2bt_vote_hold = 1;
		(void)n2bt_wake();
		n2bt_crumb("S10 BT wake vote held while BT runs (kern.n2bt_keepvote=1): votes %#x", rd(HOST_WAKEUP_BT));
	}
	n2bt_step = "S10 transport";
	STEP("S10 transport");
	st = n2bt_transport_run();      /* returns only on failure */
	BT0("S10 transport ended (%d)\n", st);
	n2bt_fail(NULL);
	goto out;

stop:
	{
		char t[48];
		snprintf(t, sizeof(t), "stopped-at-S%u", n2bt_maxstep);
		n2bt_set_state(t);
		n2bt_crumb("%s (kern.n2bt_maxstep): nothing powered", t);
	}
	if (woke) { n2bt_unwake(); woke = false; }
	n2bt_settle("stopped", false);
	goto out;
stop_off:
	if (woke) { n2bt_unwake(); woke = false; }
	{
		char t[48];
		bool keep = n2bt_owned && n2bt_stop_mode == 0;
		snprintf(t, sizeof(t), "stopped-at-S%u%s", n2bt_maxstep, keep ? "-kept" : "");
		n2bt_set_state(t);
		n2bt_crumb("%s (kern.n2bt_maxstep): %s", t, keep ? "BT stays powered, ownership handed back to the firmware (kern.n2bt_stop_mode=0)" :
		    "graceful power-off (Linux order)");
		if (keep) n2bt_hand_back(); else n2bt_graceful_off();
	}
	n2bt_settle("stopped", true);
	goto out;
fail_off:
	n2bt_fail(NULL);
	if (woke) { n2bt_unwake(); woke = false; }
	n2bt_graceful_off();
	n2bt_settle("failed", true);
out:
	if (woke) n2bt_unwake();
	n2bt_end("exit");                           /* every exit path is a clean end for the kernel guard (no-op if already written) */
	for (int i = 0; i < 2; i++) {
		if (fw[i].buf) kfree_data(fw[i].buf, fw[i].len);
	}
	__atomic_store_n(&n2bt_running, 0, __ATOMIC_RELEASE);
	thread_terminate(current_thread());
}

/* ---------------- bt8: controller diagnostics and recovery ----------------
 * [V phone, live capture after bt26a]: with the FreeBuds connected (AVDTP signalling on ACL handle 0x32) a 5-byte MediaTek vendor
 * event 0xff arrived, then the controller answered nothing more (not LE Scan Enable, not HCI_Reset, not Read_Local_Version), BTIF
 * LSR 0x60 (TX empty), no RX byte: the BT firmware stopped. n2bt_diag reads only the always-on host CSR page (0x18060000, the page
 * the wake vote lives in; read without a vote since S2) three times, so the breadcrumb says whether the BGF MCU still runs (PC moving)
 * and who owns it. n2bt_restart power-cycles the BT subsystem the way the failure path does (WMT off, fw own, bgfsys_power_off in
 * the Linux order, A-die clock and BGF top power untouched for GNSS) and repeats S4..S9 with the firmware read again from disk.
 * kern.n2bt_recover=0 switches the automatic use off (bridge: at most 3 per 5 minutes). */
static uint32_t n2bt_recover = 1, n2bt_diag_bgf = 1, n2bt_restarting;
uint32_t n2bt_recover_on(void) { return n2bt_recover; }

/* bt9 [V Linux connac2 btmtk_mt66xx.c btmtk_set_power_on step 2]: before the BGF power-on Linux tells the modem that BT is on
 * (MD coex CCIF: 0x10003318 bit0 CONSYS_BGF_PWR_ON = 1, bit1 CONSYS_BGF_READY = 0); we never did. [I] the BT firmware coordinates
 * with the LTE modem when links start; without this the modem side may never answer it. kern.n2bt_ccif=0 skips it. */
static uint32_t n2bt_ccif = 1;
static void
n2bt_ccif_on(void)
{
	if (!n2bt_ccif) return;
	uint32_t before = rd(0x10003318u);
	setb(0x10003318u, 1u << 0);
	clrb(0x10003318u, 1u << 1);
	n2bt_crumb("S5c MD coex CCIF: 0x10003318 %#x -> %#x (BGF_PWR_ON)", before, rd(0x10003318u));
}

/* bt9 [V Linux btmtk_get_cal_data_ref / bgfsys_cal_data_backup / _restore]: after the first func-on the firmware's RF calibration
 * data lies in conn_infra SYSRAM (WMT RF_CAL 0x14 subop 3 answers start/length/ready addresses 0x7C05xxxx); Linux keeps a copy and,
 * before every later func-on, writes it back plus 0x5AD02EA5 at the ready address so that the firmware does not calibrate again.
 * Our restart did not -> [I] the func-on after a restart calibrated again (and failed). */
#define N2BT_CAL_READY 0x5AD02EA5u
static uint8_t n2bt_cal[0x1000];
static uint32_t n2bt_cal_off, n2bt_cal_ready, n2bt_cal_len;

static void
n2bt_cal_backup(void)
{
	static const uint8_t q[] = { 0x01, 0x6f, 0xfc, 0x05, 0x01, 0x14, 0x01, 0x00, 0x03 };
	int st = n2bt_cmd(q, sizeof(q), 0xfc6f, 0x14, 2000, NULL, 0);
	uint32_t plen = n2bt_pkt[5] | ((uint32_t)n2bt_pkt[6] << 8);
	if (st != 0 || plen < 12) { n2bt_crumb("S7c calibration reference not available (status %d, %u bytes)", st, plen); return; }
	uint32_t start = n2bt_pkt[9] | (n2bt_pkt[10] << 8) | (n2bt_pkt[11] << 16) | ((uint32_t)n2bt_pkt[12] << 24);
	uint32_t len = n2bt_pkt[13] | (n2bt_pkt[14] << 8);
	uint32_t ready = n2bt_pkt[15] | (n2bt_pkt[16] << 8) | (n2bt_pkt[17] << 16) | ((uint32_t)n2bt_pkt[18] << 24);
	uint32_t off = start & 0xffffu, roff = ready & 0xffffu;
	if (len == 0 || off + len > 0x1000u || roff + 4u > 0x1000u || (off & 3u) || (roff & 3u)) {
		n2bt_crumb("S7c calibration reference unusable: start %#x len %u ready %#x", start, len, ready);
		return;
	}
	if (!n2bt_wake()) return;
	for (uint32_t i = 0; i < len; i += 4) {
		uint32_t w = rd(0x18050000u + off + i);
		for (uint32_t k = 0; k < 4 && i + k < len; k++) n2bt_cal[i + k] = (uint8_t)(w >> (8 * k));
	}
	n2bt_unwake();
	n2bt_cal_off = off; n2bt_cal_ready = roff; n2bt_cal_len = len;
	n2bt_crumb("S7c calibration data saved: %u bytes at sysram %#x (ready word %#x)", len, start, ready);
}

static void
n2bt_cal_restore(void)
{
	if (n2bt_cal_len == 0) { n2bt_crumb("S10r no calibration copy: the firmware calibrates again"); return; }
	if (!n2bt_wake()) return;
	for (uint32_t i = 0; i < n2bt_cal_len; i += 4) {
		uint32_t w = 0;
		for (uint32_t k = 0; k < 4; k++) w |= (uint32_t)(i + k < n2bt_cal_len ? n2bt_cal[i + k] : 0) << (8 * k);
		wr(0x18050000u + n2bt_cal_off + i, w);
	}
	wr(0x18050000u + n2bt_cal_ready, N2BT_CAL_READY);
	n2bt_crumb("S10r calibration data restored (%u bytes), ready word %#x", n2bt_cal_len, rd(0x18050000u + n2bt_cal_ready));
	n2bt_unwake();
}

/* bt9 [V Linux btmtk_send_blank_status_cmd, sent right after func-on]: WMT 0xF0 "screen on" */
static void
n2bt_blank_on(void)
{
	static const uint8_t b[] = { 0x01, 0x6f, 0xfc, 0x06, 0x01, 0xf0, 0x02, 0x00, 0x03, 0x01 };
	int st = n2bt_cmd(b, sizeof(b), 0xfc6f, 0xf0, 1000, NULL, 0);
	n2bt_crumb("S7b WMT blank status (screen on): %d", st);
}

/* bt10 [V Linux btmtk_irq.c bt_bgf2ap_irq_handler]: the firmware's software interrupts to the host. Status BGF_SW_IRQ_STATUS
 * 0x18800150: bit24 FW log ready, bit25 subsys reset request (it asserted), bit26 whole-chip reset request, 0xDEADFEED; the host
 * acknowledges by setting the same bit in BGF_SW_IRQ_RESET 0x1880014C. We have no interrupt line: polled every 100 ms by the
 * housekeeping while we hold the wake vote and the driver own (BGF awake). kern.n2bt_swirq=0 = off. Unknown bits are logged. */
static uint32_t n2bt_swirq = 1, n2bt_swirq_last, n2bt_fwlog_acks;
uint32_t n2bt_reset_requested(void) { uint32_t r = n2bt_reset_req; n2bt_reset_req = 0; return r; }

static void
n2bt_swirq_poll(void)
{
	uint32_t st;
	if (!n2bt_swirq || !n2bt_vote_hold || !n2bt_func_on || (rd(HOST_BGF_LPCTL) & (1u << 2))) return;
	st = rd(0x18800150u);
	if (st == n2bt_swirq_last && !(st & ((1u << 24) | (1u << 25) | (1u << 26)))) return;
	if (st != n2bt_swirq_last) {
		static uint32_t said;
		if (said++ < 16) n2bt_crumb("S11q BGF sw irq status %#x (was %#x)", st, n2bt_swirq_last);
		n2bt_swirq_last = st;
	}
	if (st == 0xdeadfeedu) return;
	if (st & (1u << 24)) {
		setb(0x1880014cu, 1u << 24);
		if (n2bt_fwlog_acks++ < 4) n2bt_crumb("S11q firmware log notification acknowledged (%u)", n2bt_fwlog_acks);
	}
	if (st & (1u << 25)) {
		setb(0x1880014cu, 1u << 25);
		n2bt_reset_req = 1;
		n2bt_crumb("S11r the firmware requests a subsys reset (it asserted): acknowledged, restart follows");
	}
	if (st & (1u << 26)) {
		setb(0x1880014cu, 1u << 26);
		n2bt_crumb("S11r the firmware requests a WHOLE-CHIP reset (not done: it would take Wi-Fi down); BT restart instead");
		n2bt_reset_req = 1;
	}
}

void
n2bt_snap(const char *tag)
{
	n2bt_crumb("S11v %s: votes %#x lpctl %#x pc %#x slp %#x irq %#x", tag, rd(HOST_WAKEUP_BT), rd(HOST_BGF_LPCTL), rd(HOST_MCU_PC),
	    rd(HOST_SLP_PROT_CTRL), rd(HOST_BGF_IRQ_STAT));
}

void
n2bt_diag(const char *why)
{
	uint32_t pc0 = rd(HOST_MCU_PC), lp = rd(HOST_BGF_LPCTL), irq = rd(HOST_BGF_IRQ_STAT), irq2 = rd(HOST_BGF_IRQ_STAT2), slp = rd(HOST_SLP_PROT_CTRL);
	IODelay(50);
	uint32_t pc1 = rd(HOST_MCU_PC);
	IODelay(50);
	uint32_t pc2 = rd(HOST_MCU_PC);
	n2bt_crumb("S10q %s: mcu pc %#x %#x %#x%s lpctl %#x irq %#x/%#x slp %#x votes %#x", why, pc0, pc1, pc2,
	    (pc0 == pc1 && pc1 == pc2) ? " (STUCK)" : "", lp, irq, irq2, slp, rd(HOST_WAKEUP_BT));
	if (pc0 == 0x1005f2u) n2bt_crumb("S10q pc 0x1005f2 = the ROM idle loop of S5: the BGF MCU was reset (firmware assert -> watchdog?)");
	/* [V Linux bt_bgf2ap_irq_handler / bt_chip_reset_flow]: BGF_SW_IRQ_STATUS (0x18800150) bit24 fw log, bit25 subsys reset request,
	 * bit26 whole-chip reset request, 0xDEADFEED; the firmware's assert text at conninfra EMI + 0x3B000 */
	if (n2bt_diag_bgf && n2bt_wake()) {
		n2bt_now("diag-bgf", 0, 0);
		uint32_t sw = rd(0x18800150u), dbg = rd(BGF_SW_DBG_CTL);
		n2bt_unwake();
		n2bt_crumb("S10q BGF sw irq status %#x%s%s%s sw_dbg %#x", sw, (sw & (1u << 25)) ? " SUBSYS-RESET-REQUEST" : "",
		    (sw & (1u << 26)) ? " WHOLE-CHIP-RESET-REQUEST" : "", sw == 0xdeadfeedu ? " DEADFEED" : "", dbg);
	}
	if (n2bt_diag_bgf) {
		static vm_offset_t msg_va;
		char m[100];
		unsigned k = 0;
		if (msg_va == 0) msg_va = ml_io_map_unmappable(CONN_EMI_PA + 0x3b000u, 0x1000, VM_WIMG_IO);
		if (msg_va != 0) {
			for (unsigned i = 0; i < 96; i++) {
				uint8_t ch = ((volatile uint8_t *)msg_va)[i];
				if (ch == 0) break;
				m[k++] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.';
			}
			m[k] = 0;
			n2bt_crumb("S10q firmware assert text (EMI+0x3b000): \"%s\"", m);
		}
	}
}

/* bt11 [V phone bt30a]: of three restarts the first got sw_dbg 0xdeadfeed / pc 0 (BGF bus not back yet, 5 s after the assert),
 * the second "ROM idle" with a stale 0x1D1E but pc 0xf0006f30 (not the ROM idle loop) and no answer to func-on, the third (10 s
 * later) worked. Now: no WMT func off / fw own handshake with a dead firmware (Linux reset flow skips both: "do not send CMD to
 * connsys while resetting"), ROM idle only with pc in the ROM (n2bt_bgf_on), and up to 3 power-on attempts with 0.2 / 2 / 5 s of
 * settling after the power-off, each one acknowledging the firmware's software interrupts first. */
static int
n2bt_restart_once(struct n2bt_fw fw[2], unsigned attempt)
{
	uint8_t r[16];
	int st;

	if (n2bt_wake()) {
		uint32_t sw = rd(0x18800150u);
		if (sw != 0xdeadfeedu && (sw & (7u << 24))) setb(0x1880014cu, sw & (7u << 24));
		n2bt_unwake();
	}
	n2bt_now("rs-fw", attempt, 0);
	if (!n2bt_wake()) { n2bt_crumb("S10r #%u no conn_infra wake", attempt); return EIO; }
	for (int i = 0; i < 2; i++) {
		if (n2bt_fw_place(&fw[i]) != 0) { n2bt_unwake(); n2bt_crumb("S10r #%u firmware %s not placed", attempt, fw[i].name); return EIO; }
	}
	n2bt_radio_status(true);
	n2bt_ccif_on();
	n2bt_now("rs-bgf", attempt, 0);
	if ((st = n2bt_bgf_on()) != 0) {
		n2bt_crumb("S10r #%u BGF power-on failed at %s (%d)", attempt, n2bt_step, st);
		n2bt_unwake();
		return st;
	}
	n2bt_unwake();
	n2bt_now("rs-own", attempt, 0);
	if (n2bt_btif_init() != 0) { n2bt_crumb("S10r #%u BTIF init failed", attempt); return EIO; }
	n2bt_btif_on = true;
	n2bt_owned = true;
	if (n2bt_own() != 0) { n2bt_crumb("S10r #%u driver own not granted", attempt); return EIO; }
	n2bt_rx_have = 0;
	n2bt_cal_restore();
	n2bt_now("rs-func", attempt, 0);
	{
		static const uint8_t on[] = { 0x01, 0x6f, 0xfc, 0x06, 0x01, 0x06, 0x02, 0x00, 0x00, 0x01 };
		int w = n2bt_cmd(on, sizeof(on), 0xfc6f, 0x06, 6000, NULL, 0);
		n2bt_func_on = w == 0;
		if (w != 0) { n2bt_crumb("S10r #%u WMT func on: %d", attempt, w); return EIO; }
	}
	if (n2bt_hci0(0x0c03, r, sizeof(r)) != 0) { n2bt_crumb("S10r #%u HCI_Reset not answered", attempt); return EIO; }
	{
		uint8_t c[10];
		if (n2bt_addr_cmd(c) == 10) (void)n2bt_cmd(c, sizeof(c), 0xfc1a, 0, 2000, NULL, 0);
	}
	return 0;
}

int
n2bt_restart(const char *why)
{
	struct n2bt_fw fw[2] = {
		{ "soc3_0_ram_mcu_1a_1_hdr.bin", EMI_MCU_START, EMI_MCU_SIZE, NULL, 0 },
		{ "soc3_0_ram_bt_1a_1_hdr.bin", EMI_BT_START, EMI_BT_SIZE, NULL, 0 },
	};
	static const unsigned settle_ms[3] = { 200, 2000, 5000 };
	int st = EIO;

	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT restart", 21);
	n2bt_crumb("S10r controller restart (%s)", why);
	n2bt_set_state("restarting");
	n2bt_restarting = 1;
	for (int i = 0; i < 2; i++) {
		char path[128];
		snprintf(path, sizeof(path), N2BT_FW_DIR "%s", fw[i].name);
		if (n2bt_read_file(path, &fw[i].buf, &fw[i].len) != 0) { n2bt_crumb("S10r firmware %s not read", fw[i].name); goto done; }
	}
	for (unsigned attempt = 1; attempt <= 3; attempt++) {
		n2bt_now("rs-off", attempt, 0);
		n2bt_func_on = false;                   /* no WMT func off and no fw-own handshake with a firmware that is gone */
		n2bt_owned = false;
		n2bt_graceful_off();
		n2bt_btif_on = false;
		n2bt_rx_have = 0;
		IOSleep(settle_ms[attempt - 1]);
		st = n2bt_restart_once(fw, attempt);
		if (st == 0) break;
	}
done:
	for (int i = 0; i < 2; i++) {
		if (fw[i].buf) kfree_data(fw[i].buf, fw[i].len);
	}
	if (st == 0) n2bt_blank_on();
	n2bt_crumb("S10r restart %s", st == 0 ? "done: controller answers again" : "FAILED after 3 attempts");
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT END restart", 16);   /* the AP survived it (success or failure) */
	n2bt_restarting = 0;
	n2bt_now(st == 0 ? "rs-done" : "rs-failed", 0, 0);
	n2bt_set_state(st == 0 ? "up" : "restart-failed");
	return st;
}

/* every ~2 s from the bridge: the A-die top clock keeper (see the header) */
void
n2bt_housekeeping(void)
{
	static uint64_t next;
	uint64_t now = mach_absolute_time();

	if (!n2bt_end_written && n2bt_up_since != 0) {
		uint64_t ns = 0;
		absolutetime_to_nanoseconds(now - n2bt_up_since, &ns);
		if (ns > 120ull * NSEC_PER_SEC) {
			n2bt_end_written = 1;
			n2bt_crumb("running 120 s without reset");
			n2bt_end("up");
		}
	}
	{
		static uint64_t anext;
		if (now >= anext && n2bt_end_done && !n2bt_restarting) {   /* bt11: "AP alive" stamp for post-mortems (never refuses) */
			char t[N2BT_GUARD_LEN];
			uint64_t ns = 0;
			clock_interval_to_deadline(15000, NSEC_PER_MSEC, &anext);
			absolutetime_to_nanoseconds(now, &ns);
			int k = snprintf(t, sizeof(t), "N2BT END alive %llus", (unsigned long long)(ns / 1000000000ull));
			if (k > 0) n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, t, (uint32_t)(k < (int)sizeof(t) ? k : (int)sizeof(t) - 1));
		}
	}
	{
		static uint64_t snext;
		if (now >= snext) {
			clock_interval_to_deadline(100, NSEC_PER_MSEC, &snext);
			n2bt_swirq_poll();
		}
	}
	{
		static uint64_t vnext;
		if (n2bt_keepvote && n2bt_vote_hold && now >= vnext) {
			clock_interval_to_deadline(100, NSEC_PER_MSEC, &vnext);
			if ((rd(HOST_WAKEUP_BT) & 1u) == 0) {
				setb(HOST_WAKEUP_BT, 1u);
				if (n2bt_vote_fixes++ < 8) n2bt_crumb("S11w the BT wake vote was cleared by someone else: set again");
			}
		}
	}
	if (!n2bt_adie || now < next) {
		return;
	}
	clock_interval_to_deadline(2000, NSEC_PER_MSEC, &next);
	n2bt_now("hk-wake", 0, 0);
	if (!n2bt_wake()) {
		n2bt_now("hk-nowake", 0, 0);
		return;
	}
	n2bt_now("hk-adie", 0, 0);
	if ((rd(CONN_CFG_ADIE_CTL) & 2u) == 0) {
		setb(CONN_CFG_ADIE_CTL, 2u);
		(void)n2bt_poll(CONN_WTSLP_WB_STA, 1u << 26, 0, 100, 5);
		if (n2bt_st.adie_fixes++ < 8) {
			BT0("A-die top clock was off (GNSS power-off?): set again\n");
		}
	}
	n2bt_unwake();
	n2bt_now("hk-done", 0, 0);
}

/* ---------------- bt14: Skywalk user-pipe privilege for bluetoothd ----------------
 * [V phone bt33a: "SWEET Sandbox: bluetoothd privilege 12000 denied"; crash reports + disassembly of bluetoothd 20H392]: when an
 * audio device connects, bluetoothd builds its AudioSkywalkPipe: os_nexus_controller_register_provider("com.apple.bluetooth.AudioPipe",
 * NEXUS_TYPE_USER_PIPE) (+0x3efc2c, result unchecked), alloc/bind a provider instance, os_channel_create_extended -> stored at +0xa8.
 * The registration needs PRIV_SKYWALK_REGISTER_USER_PIPE (12000). On iOS AMFI grants it to holders of the entitlement
 * com.apple.private.skywalk.register-user-pipe (bluetoothd has it); this port's sweet_amfi stand-in denies everything except
 * PRIV_WORK_INTERVAL -> no provider, no instance, NULL channel -> SIGSEGV in os_channel_get_fd(NULL) from the read-loop setup
 * (+0x3effb4). Fix without touching the other pieces' policies: a tiny extra MAC policy that grants exactly this privilege, only to a
 * process named bluetoothd that holds the entitlement. kern.n2bt_skywalk_grant=0 switches it off. */
static uint32_t n2bt_skywalk_grant = 1, n2bt_grants;
static int
n2bt_priv_grant(__unused kauth_cred_t cred, int priv)
{
	char name[32];
	if (priv != PRIV_SKYWALK_REGISTER_USER_PIPE || !n2bt_skywalk_grant) return EPERM;
	proc_selfname(name, sizeof(name));
	if (strcmp(name, "bluetoothd") != 0) return EPERM;
	if (!IOCurrentTaskHasEntitlement("com.apple.private.skywalk.register-user-pipe")) return EPERM;
	if (n2bt_grants++ < 8) printf("NORD2 BT0: S12p Skywalk user-pipe registration granted to bluetoothd (pid %d, entitled)\n", proc_selfpid());
	return 0;
}
static struct mac_policy_ops n2bt_mac_ops = {
	.mpo_priv_grant = n2bt_priv_grant,
};
static struct mac_policy_conf n2bt_mac_conf = {
	.mpc_name = "Nord2BT",
	.mpc_fullname = "Nord 2 Bluetooth: Skywalk user-pipe grant for bluetoothd",
	.mpc_ops = &n2bt_mac_ops,
};
static mac_policy_handle_t n2bt_mac_handle;

static void
n2bt_mac_start(void)
{
	static bool done;
	if (done) return;
	done = true;
	int e = mac_policy_register(&n2bt_mac_conf, &n2bt_mac_handle, NULL);
	printf("NORD2 BT0: S12p MAC policy Nord2BT (bluetoothd Skywalk user-pipe grant) registered: %d\n", e);
}

/* ---------------- bt6: boot-time /chosen address ----------------
 * MobileGestalt computes (and caches) "bluetooth" early in userland, long before n2btd and the bring-up. This thread starts from
 * 0722's C++ static initialisation, touches no hardware, waits for the root volume, reads n2btd's stored address file
 * /private/var/db/nord2-bt/bdaddr (written on the first run, "aa:bb:cc:dd:ee:ff") and sets IODeviceTree:/chosen
 * mac-address-bluetooth0 (n2bt_chosen_address). Without the file (BT never installed, or the very first boot) it does nothing.
 * Kill switch: delete /private/var/db/nord2-bt/enable (it is checked too). */
static void n2bt_early_bringup(void);
static void
n2bt_early_thread(__unused void *arg, __unused wait_result_t w)
{
	uint8_t *buf = NULL, a[6];
	uint32_t len = 0, waited = 0;

	for (;;) {
		vnode_t root = vfs_rootvnode();
		if (root != NULLVP) {
			vnode_put(root);
			break;
		}
		if (waited++ > 12000) {                                 /* 120 s */
			printf("NORD2 BT2: early: no root volume in 120 s\n");
			goto out;
		}
		IOSleep(10);
	}
	n2bt_mac_start();                           /* bt14: after bsd_init (root mounted): MAC is fully up, late registration */
	/* bt6: the data volume (and the address file) appears some seconds after the root volume; MobileGestalt may answer early, so
	 * poll fast (25 ms) and long enough (60 s) to win that race in the normal case. */
	int tries;
	bool enabled = false;
	for (tries = 0; tries < 2400; tries++) {
		uint8_t *g = NULL;
		uint32_t gl = 0;
		if (n2bt_read_file_sz("/private/var/db/nord2-bt/enable", 0, 64, &g, &gl) == 0) {
			if (g) kfree_data(g, gl);
			enabled = true;
			if (n2bt_read_file_sz("/private/var/db/nord2-bt/bdaddr", 17, 64, &buf, &len) == 0) break;
			buf = NULL;
			if (tries >= 400) break;                /* bt22: enabled but no address yet (first boot): 10 s is enough to know */
		}
		IOSleep(25);
	}
	if (buf == NULL) {
		printf("NORD2 BT2: early: no /private/var/db/nord2-bt/bdaddr%s: /chosen left alone\n", enabled ? "" : " (or BT disabled)");
	} else {
		if (n2bt_parse_addr((const char *)buf, len, a) == 0) {
			printf("NORD2 BT2: early: address file read %u ms after the root volume (root after %u ms)\n", (unsigned)tries * 25u, (unsigned)waited * 10u);
			n2bt_chosen_address(a, "boot, n2btd's stored address");
			memcpy(n2bt_req_addr, a, 6);         /* bt22: the bring-up below uses it like kern.n2bt_addr */
			n2bt_have_req_addr = true;
		} else {
			printf("NORD2 BT2: early: bdaddr file not usable\n");
		}
		kfree_data(buf, len);
	}
	if (enabled) n2bt_early_bringup();
out:
	thread_terminate(current_thread());
}

/* ---------------- bt22: kernel-side bring-up at boot ----------------
 * [V phone]: n2btd started the bring-up only at uptime >= 45 s with en0 RUNNING (link up), else at 120 s: offline phones waited 2 min
 * and bluetoothd respawned a dozen times meanwhile. What the bring-up needs: conn_infra powered (our Wi-Fi bring-up does that in the
 * kernel, early, whether or not a network is joined; iOS "Wi-Fi off"/airplane is only a logical power state there
 * [V wifi_station_service_bind.inc nord2_wifi_power_set: no hardware power-down]) and the Wi-Fi bring-up finished with conn_infra so
 * that BT's S2..S5 do not overlap its writes. Gate: en0 ATTACHED (the Wi-Fi backend published its interface after its whole bring-up;
 * not "running") or 90 s, then S1's own three conn_infra power checks. Started from the early thread (root volume + enable file),
 * stage / rearm files honoured like n2btd does; n2btd's later kern.n2bt_up finds the bring-up running. kern.n2bt_early=0 = old way. */
static uint32_t n2bt_early = 1;

static void
n2bt_early_bringup(void)
{
	uint8_t *f = NULL;
	uint32_t fl = 0;
	uint64_t t0 = mach_absolute_time(), ns = 0;
	unsigned ms = 0;
	bool attached = false;

	if (!n2bt_early || !n2bt_enable) { printf("NORD2 BT0: S0e kernel start off (kern.n2bt_early=%u kern.n2bt=%u): n2btd starts BT\n", n2bt_early, n2bt_enable); return; }
	if (n2bt_read_file_sz("/private/var/db/nord2-bt/stage", 1, 32, &f, &fl) == 0) {
		char b[33] = "";
		memcpy(b, f, fl < 32 ? fl : 32);
		int m = strncmp(b, "probe", 5) == 0 ? 9 : (int)strtol(b, NULL, 10);
		n2bt_maxstep = (m < 1 || m > 10) ? 1 : (uint32_t)m;
		if (strncmp(b, "probe", 5) == 0) n2bt_publish = 0;
		kfree_data(f, fl);
		f = NULL;
	}
	if (n2bt_read_file_sz("/private/var/db/nord2-bt/rearm", 0, 64, &f, &fl) == 0) {
		if (f) kfree_data(f, fl);
		n2bt_rearm = 1;
		printf("NORD2 BT0: S0e rearm file: kernel guard ignored once (n2btd removes the file)\n");
	}
	for (unsigned i = 0; i < 360; i++) {             /* 90 s */
		ifnet_t ifp = NULL;
		if (ifnet_find_by_name("en0", &ifp) == 0 && ifp != NULL) { ifnet_release(ifp); attached = true; break; }
		IOSleep(250);
	}
	absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
	ms = (unsigned)(ns / 1000000ull);
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	if (n2bt_attempts >= n2bt_tries || __atomic_exchange_n(&n2bt_running, 1, __ATOMIC_ACQ_REL)) {
		printf("NORD2 BT0: S0e kernel start skipped: a bring-up is already running or the attempts are used up\n");
		return;
	}
	n2bt_attempts++;
	printf("NORD2 BT0: S0e BT start at uptime %llu s: en0 %s after %u ms (stop after S%u)\n", (unsigned long long)(ns / 1000000000ull),
	    attached ? "attached (Wi-Fi bring-up done)" : "NOT attached in 90 s: S1 checks conn_infra itself", ms, n2bt_maxstep);
	thread_t th;
	if (kernel_thread_start(n2bt_thread, NULL, &th) != KERN_SUCCESS) {
		__atomic_store_n(&n2bt_running, 0, __ATOMIC_RELEASE);
		return;
	}
	thread_deallocate(th);
}

void
n2bt_early_start(void)
{
	thread_t th;
	if (kernel_thread_start(n2bt_early_thread, NULL, &th) == KERN_SUCCESS) {
		thread_deallocate(th);
	}
}

/* ---------------- sysctls ---------------- */
static int
n2bt_sysctl_up(__unused struct sysctl_oid *oidp, __unused void *arg1, __unused int arg2, struct sysctl_req *req)
{
	int v = __atomic_load_n(&n2bt_running, __ATOMIC_ACQUIRE), err = sysctl_handle_int(oidp, &v, 0, req);
	thread_t th;

	if (err || req->newptr == USER_ADDR_NULL) {
		return err;
	}
	if (v != 1) {
		return EINVAL;
	}
	if (!n2bt_enable) {
		BT0("kern.n2bt_up refused: kern.n2bt=0 (kill switch)\n");
		return EPERM;
	}
	if (n2bt_attempts >= n2bt_tries) {
		BT0("kern.n2bt_up refused: %u attempts this boot\n", n2bt_attempts);
		return EAGAIN;
	}
	if (__atomic_exchange_n(&n2bt_running, 1, __ATOMIC_ACQ_REL)) {
		return EBUSY;
	}
	n2bt_attempts++;
	BT0("bring-up %u requested by pid %d\n", n2bt_attempts, proc_selfpid());
	if (kernel_thread_start(n2bt_thread, NULL, &th) != KERN_SUCCESS) {
		__atomic_store_n(&n2bt_running, 0, __ATOMIC_RELEASE);
		return ENOMEM;
	}
	thread_deallocate(th);
	return 0;
}

static int
n2bt_sysctl_addr(__unused struct sysctl_oid *oidp, __unused void *arg1, __unused int arg2, struct sysctl_req *req)
{
	/* bt4: the buffer was exactly 18 bytes and sysctl_io_string refuses newlen >= bufsize (kern_newsysctl.c: "if (req->newlen >=
	 * valueSize) return EINVAL"), so n2btd's "aa:..:ff" + NUL (18 bytes) always got EINVAL (phone, stage 9). 64 bytes now. */
	char text[64];
	uint8_t a[6];
	int err;

	snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", n2bt_req_addr[0], n2bt_req_addr[1], n2bt_req_addr[2],
	    n2bt_req_addr[3], n2bt_req_addr[4], n2bt_req_addr[5]);
	err = sysctl_handle_string(oidp, text, sizeof(text), req);
	if (err || req->newptr == USER_ADDR_NULL) {
		return err;
	}
	if ((err = n2bt_parse_addr(text, sizeof(text), a)) != 0) {
		BT0("address from the daemon refused (not aa:bb:cc:dd:ee:ff, or not publishable): '%.20s'\n", text);
		return err;
	}
	memcpy(n2bt_req_addr, a, 6);
	n2bt_have_req_addr = true;
	BT0("address from the daemon: %02x:%02x:%02x:%02x:%02x:%02x\n", a[0], a[1], a[2], a[3], a[4], a[5]);
	n2bt_chosen_address(a, "kern.n2bt_addr");
	return 0;
}

static int
n2bt_sysctl_state(__unused struct sysctl_oid *oidp, __unused void *arg1, __unused int arg2, struct sysctl_req *req)
{
	char s[sizeof(n2bt_state_text)];
	lck_mtx_lock(&n2bt_lock);
	strlcpy(s, n2bt_state_text, sizeof(s));
	lck_mtx_unlock(&n2bt_lock);
	return sysctl_handle_string(oidp, s, sizeof(s), req);
}

static int
n2bt_sysctl_stats(__unused struct sysctl_oid *oidp, __unused void *arg1, __unused int arg2, struct sysctl_req *req)
{
	char s[256];
	snprintf(s, sizeof(s), "attempts %u running %u tx %u/%u rx %u/%u resync %u overrun %u tx_to %u own %u adie_fix %u addr %02x:%02x:%02x:%02x:%02x:%02x",
	    n2bt_attempts, n2bt_running, n2bt_st.tx_pkts, n2bt_st.tx_bytes, n2bt_st.rx_pkts, n2bt_st.rx_bytes, n2bt_st.rx_resync,
	    n2bt_st.rx_overrun, n2bt_st.tx_timeouts, n2bt_st.own_clr, n2bt_st.adie_fixes, n2bt_ctl_addr[5], n2bt_ctl_addr[4],
	    n2bt_ctl_addr[3], n2bt_ctl_addr[2], n2bt_ctl_addr[1], n2bt_ctl_addr[0]);
	return sysctl_handle_string(oidp, s, sizeof(s), req);
}

static int
n2bt_sysctl_note(__unused struct sysctl_oid *oidp, __unused void *arg1, __unused int arg2, struct sysctl_req *req)
{
	static uint32_t notes;
	char s[200] = "";
	int err = sysctl_handle_string(oidp, s, sizeof(s), req);

	if (err || req->newptr == USER_ADDR_NULL) {
		return err;
	}
	s[sizeof(s) - 1] = 0;
	if (notes++ < 400) {
		printf("NORD2 BT U: %s\n", s);
	}
	return 0;
}

SYSCTL_UINT(_kern, OID_AUTO, n2bt, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_enable, 0, "0720 Bluetooth: 1 bring-up allowed, 0 kill switch");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_publish, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_publish, 0, "0720: 1 expose the controller to iOS, 0 probe only");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_quirks, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_quirk, 0, "0721 answer rewrites (N2BT_Q_*)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_trace, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_trace, 0, "0721 HCI packets still to log");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_adie, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_adie, 0, "0720 A-die top clock keeper");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_recover, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_recover, 0, "0720 bt8: automatic controller restart when it stops answering");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_skywalk_grant, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_skywalk_grant, 0, "0720 bt14: grant PRIV_SKYWALK_REGISTER_USER_PIPE to entitled bluetoothd");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_a2dp_filter, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_a2dp_filter, 0, "0720 bt18: DIAGNOSTIC AVDTP codec filter: 0 off (default), 1 hide AAC, 2 only SBC");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_early, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_early, 0, "0720 bt22: start the bring-up from the kernel at boot (en0 attached), not n2btd");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_poll_idle_us, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_poll_idle_us, 0, "0720 bt21: poll period (us) with a link up but quiet");
SYSCTL_INT(_kern, OID_AUTO, n2bt_mode, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_mode, 0, "0720 bt21: transport mode (2 default, -1 auto-bisect at each restart, 0..5 fixed)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_tx_mode, CTLFLAG_RD | CTLFLAG_LOCKED, &n2bt_tx_mode, 0, "0720 bt20: transport mode in effect");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_tx_timeout_ms, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_tx_timeout_ms, 0, "0720 bt19: per-packet BTIF TX deadline (ms)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_tx_bps, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_tx_bps, 0, "0720 bt17: ACL bit/s cap to the controller (0 = none)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_poll_us, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_poll_us, 0, "0720 bt11: RX poll period (us) with a link up / recent traffic; 0 = old 1 ms");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_wak, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_wak, 0, "0720 bt10: BTIF_WAK before TX: 0 never (Linux), 1 old 2 us pulse, 2 150 us pulse");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_swirq, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_swirq, 0, "0720 bt10: poll/acknowledge the firmware's software interrupts");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_keepvote, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_keepvote, 0, "0720 bt9: keep the conn_infra BT wake vote while BT runs");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_ccif, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_ccif, 0, "0720 bt9: tell the modem BT is on (MD coex CCIF) before BGF power-on");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_diag_bgf, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_diag_bgf, 0, "0720 bt9: on a hang also read BGF sw irq status and the firmware assert text");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_wait_s, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_wait_s, 0, "0720 conninfra wait (s)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_force, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_force, 0, "0720 skip the AP-side predicates (diagnosis)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_maxstep, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_maxstep, 0, "0720 bt2: stop after step N (1 = AP-side reads only, 9 = controller up not published, 10 = full)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_rearm, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_rearm, 0, "0720 bt2: 1 = ignore the previous-boot-reset guard once");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_stop_mode, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_stop_mode, 0, "0720 bt3: stop after S6+: 0 keep BT powered (fw own handed back), 1 graceful off");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_monitor_s, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_monitor_s, 0, "0720 bt3: post-stop monitor seconds (END written after it)");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_tx_gap_us, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_tx_gap_us, 0, "0720 bt6: us pause per 16-byte chunk of long packets");
SYSCTL_UINT(_kern, OID_AUTO, n2bt_tries, CTLFLAG_RW | CTLFLAG_LOCKED, &n2bt_tries, 0, "0720 bring-up attempts allowed per boot");
SYSCTL_PROC(_kern, OID_AUTO, n2bt_up, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2bt_sysctl_up, "I", "0720 write 1: bring the controller up");
SYSCTL_PROC(_kern, OID_AUTO, n2bt_addr, CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2bt_sysctl_addr, "A", "0720 BD address aa:bb:cc:dd:ee:ff");
SYSCTL_PROC(_kern, OID_AUTO, n2bt_state, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, n2bt_sysctl_state, "A", "0720 state");
SYSCTL_PROC(_kern, OID_AUTO, n2bt_stats, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, n2bt_sysctl_stats, "A", "0720 counters");
SYSCTL_PROC(_kern, OID_AUTO, n2bt_note, CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2bt_sysctl_note, "A", "0720 daemon log line");
#endif /* NORD2 */
