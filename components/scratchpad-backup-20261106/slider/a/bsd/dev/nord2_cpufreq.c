/*
 * Nord2 patch 0104: gated CPU DVFS for the big clusters (MT6893), kernel glue of bsd/dev/nord2_cpufreq.h.
 *
 *   n2cpu=0 (default)  nothing: no thread, no mapping, no output
 *   n2cpu=1..499       discovery only: dump the cpuhvfs CSRAM (0x0011bc00, 0x1400 bytes, three words per line), report every block that
 *                      looks like a cpufreq-hw domain and how it was judged; NOTHING is written
 *   n2cpu=<MHz> >= 500 discovery, then every VERIFIED big-cluster domain (cpu4-6 "l", cpu7 "b") is ramped row by row to the fastest row at or
 *                      below <MHz> (clamped to the cluster rating 2600/3000); n2cpu=2000 is the recommended value
 * Log lines start with "NORD2 CPUFREQ".  The CPU clock measurement is nord2_cpu_mhz() of patch 0099 (nord2_sched.c), the battery gauge is
 * nord2_battery_temperature_cC() of Nord2GPU.cpp (false = unreadable).
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)

#include <sys/param.h>
#include <sys/systm.h>
#include <mach/mach_types.h>
#include <kern/kern_types.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>

#include <mach/thread_act.h>

#include "nord2_cpufreq.h"
#include "nord2_mcupm_scan.h"
#include "nord2_dvfs.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

TUNABLE(int, nord2_cpufreq_arg, "n2cpu", 0);
TUNABLE(int, nord2_mcu_arg, "n2mcu", 0);
TUNABLE(int, nord2_mcuping_arg, "n2mcuping", 0);
TUNABLE(int, nord2_dvfs_arg, "n2dvfs", 0);
TUNABLE(int, nord2_membw_arg, "n2membw", 0);   /* patch 0120: CPU memory bandwidth measurement (read/write/copy of 64 MiB); touches only DRAM */
TUNABLE(int, nord2_dram_arg, "n2dram", 0);   /* patch 0121: read-only dump of the DVFSRC page (1), plus the DRAMC ch0 AO page (2); nothing is written */
TUNABLE(int, nord2_dvfs_stage_arg, "n2dvst", 3);
TUNABLE(int, nord2_gov_arg, "n2gov", 1);          /* 0172: 1 = load-based scaling (default), 0 = fixed cap with the old 2 s guard */   /* 0 preflight only, 1 + share buffer, 2 + platform init, 3 = everything (default) */

extern unsigned nord2_cpu_mhz(int cpu);                                                /* nord2_sched.c (0099) */
extern int nord2_sched_mode;                                                            /* nord2_sched.c (0099): placement mode bits */
extern bool nord2_battery_temperature_cC(int *centi);                                  /* Nord2GPU.cpp */
extern int nord2_cpu_util_permille(int cpu);                                            /* Nord2GPU.cpp (0172): busy permille over the last ~100 ms */
extern int nord2_gpu_pmic_ready(void);  /* Nord2GPU.cpp (0142): the GPU driver is ready and its PMIC owner thread answers */
extern int nord2_pmic_read(unsigned bus, unsigned sid, unsigned reg, unsigned *val);  /* Nord2GPU.cpp (0116): read-only PMIC access on the owner thread */

#define N2CF_FIRST_S    60u     /* let the boot settle and the 0099 thread print its baseline first */
#define N2CF_TICK_S     2u

static volatile uint32_t *n2cf_csram;

static uint32_t
n2cf_rd(__unused void *ctx, uint32_t off)
{
	return n2cf_csram[off / 4];
}

static void
n2cf_wr(__unused void *ctx, uint32_t off, uint32_t v)
{
	n2cf_csram[off / 4] = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static unsigned
n2cf_mhz(__unused void *ctx, int cpu)
{
	return nord2_cpu_mhz(cpu);
}

static bool
n2cf_gauge(__unused void *ctx, int *cC)
{
	return nord2_battery_temperature_cC(cC);
}

static void
n2cf_sleep_ms(__unused void *ctx, unsigned ms)
{
	assert_wait_timeout((event_t)&n2cf_sleep_ms, THREAD_UNINT, ms, NSEC_PER_MSEC);
	thread_block(THREAD_CONTINUE_NULL);
}

static void
n2cf_log(__unused void *ctx, int code, unsigned a, unsigned b, unsigned c, unsigned d)
{
	switch (code) {
	case N2CF_L_DUMP:
		printf("NORD2 CPUFREQ csram +0x%04x: %08x %08x %08x\n", a, b, c, d);
		break;
	case N2CF_L_CANDIDATE:
		printf("NORD2 CPUFREQ candidate LUT at +0x%04x: %u rows, top %u MHz\n", a, b, c);
		break;
	case N2CF_L_VERDICT: {
		static const char *const why[] = { "ok", "not a LUT", "engine off (ENABLE/HW_STATE)", "bad PERF_STATE/HW_STATE",
			                           "top row matches no cluster", "measured clock does not match LUT[PERF_STATE]", "cannot measure" };
		printf("NORD2 CPUFREQ block +0x%04x cluster %s: %s\n", c, a < N2CF_CLUSTERS ? n2cf_clusters[a].name : "?",
		    b < sizeof(why) / sizeof(why[0]) ? why[b] : "?");
		break;
	}
	case N2CF_L_VERIFIED:
		printf("NORD2 CPUFREQ VERIFIED cluster %s at +0x%04x: PERF_STATE %u, CPU measures %u MHz\n", n2cf_clusters[a].name, b, c, d);
		break;
	case N2CF_L_STEP:
		printf("NORD2 CPUFREQ cluster %s row %u (%u MHz): CPU measures %u MHz\n", n2cf_clusters[a].name, b, c, d);
		break;
	case N2CF_L_REVERT:
		printf("NORD2 CPUFREQ cluster %s: row for %u MHz not reached (CPU measures %u MHz): PERF_STATE %u written back, cluster frozen\n",
		    n2cf_clusters[a].name, c, d, b);
		break;
	case N2CF_L_DONE:
		printf("NORD2 CPUFREQ cluster %s: row %u -> %u, CPU measures %u MHz\n", n2cf_clusters[a].name, b, c, d);
		break;
	case N2CF_L_REFUSED:
		if (a == 1) {
			printf("NORD2 CPUFREQ no ramp: battery %d.%02d C is at or above the start limit\n", (int)b / 100, (int)b % 100);
		} else if (a == 2) {
			printf("NORD2 CPUFREQ no ramp for cluster %s: no verified domain\n", n2cf_clusters[b].name);
		} else {
			printf("NORD2 CPUFREQ cluster %s: already at or beyond the requested row (cur %u, target %u)\n", n2cf_clusters[b].name, c, d);
		}
		break;
	case N2CF_L_DOMAIN:
		printf("NORD2 CPUFREQ canonical domain +0x%04x: ENABLE %08x PERF_STATE %08x HW_STATE %08x\n", a, b, c, d);
		break;
	case N2CF_L_SRAM: {
		static const char *const st[] = { "empty (zeros/ones)", "power-up noise: nothing ever wrote it, so no DVFS engine runs", "structured" };
		printf("NORD2 CPUFREQ csram is %s (ones %u.%u%%, %u zero/ones words, %u equal neighbours)\n", a < 3 ? st[a] : "?", b / 10, b % 10, c, d);
		break;
	}
	case N2CF_L_PLACE:
		printf("NORD2 CPUFREQ placement mode %#x (big cores measure %u MHz, little %u MHz)\n", a, b, c);
		break;
	case N2CF_L_GUARD:
		printf("NORD2 CPUFREQ guard cluster %s: battery %d.%02d C, row %u (%s)\n", n2cf_clusters[a].name, (int)c / 100, (int)c % 100, b,
		    d ? "faster" : "slower");
		break;
	}
}

static const struct n2cf_ops n2cf_ops = { n2cf_rd, n2cf_wr, n2cf_mhz, n2cf_gauge, n2cf_sleep_ms, n2cf_log };

/* Enable core-aware placement (0099 mode 0xf) when the big cluster measures well above the little one, back off when it no longer does. */
static void
n2cf_place(bool force_log)
{
	unsigned big = nord2_cpu_mhz(n2cf_clusters[1].first_cpu), little = nord2_cpu_mhz(n2cf_clusters[0].first_cpu);
	int next = n2cf_place_decide(big, little, nord2_sched_mode);
	if (next != nord2_sched_mode || force_log) {
		nord2_sched_mode = next;
		n2cf_log(NULL, N2CF_L_PLACE, (unsigned)next, big, little, 0);
	}
}

/*
 * n2mcu=1: READ-ONLY look at the MCUPM (MCU power manager, the co-processor that owns CPU DVFS on this SoC; README-0107-design.md).
 * Addresses are the live DT's `mediatek,mcupm` node (reg-names): control registers (shared by all five mailboxes) 0x10300074 clr, 0x10300078 recv,
 * 0x10300150 send, 0x10300154 set; mailbox slot memory mbox0..4 = 0x1031fce0 + 0xa0*n; firmware SRAM base 0x10301000.  Only 32-bit loads, no
 * store of any kind, nothing is sent.  Printed raw; interpretation is in the design note.  Risk: a block whose clock or power is off may stall
 * the bus (no abort recovery in this kernel), which is why this has its own boot-arg and needs its own approval to run.
 */
static void
n2mcu_probe(void)
{
	static const struct { uint64_t pa; uint32_t words; const char *what; } r[] = {
		{ 0x10300074, 1, "mbox clr" }, { 0x10300078, 1, "mbox recv" }, { 0x10300150, 1, "mbox send" }, { 0x10300154, 1, "mbox set" },
	};
	volatile uint32_t *ctl = (volatile uint32_t *)ml_io_map(0x10300000, 0x1000);
	volatile uint32_t *box = (volatile uint32_t *)ml_io_map(0x1031f000, 0x1000);
	volatile uint32_t *sram = (volatile uint32_t *)ml_io_map(0x10301000, 0x1000);
	if (ctl == NULL || box == NULL || sram == NULL) {
		printf("NORD2 MCUPM probe: mapping failed\n");
		return;
	}
	printf("NORD2 MCUPM probe begin (read-only)\n");
	for (unsigned i = 0; i < sizeof(r) / sizeof(r[0]); i++) {
		printf("NORD2 MCUPM reg %#llx %s = %08x\n", r[i].pa, r[i].what, ctl[(r[i].pa & 0xfff) / 4]);
	}
	for (unsigned n = 0; n < 5; n++) {
		uint32_t base = 0xce0 + 0xa0 * n;                 /* offset in the page 0x1031f000: 0x1031fce0 + 0xa0*n */
		for (unsigned w = 0; w < 0xa0 / 4; w += 4) {
			printf("NORD2 MCUPM mbox%u +0x%02x: %08x %08x %08x %08x\n", n, w * 4, box[base / 4 + w], box[base / 4 + w + 1],
			    box[base / 4 + w + 2], box[base / 4 + w + 3]);
		}
	}
	for (unsigned w = 0; w < 64; w += 4) {                    /* first 256 bytes of the firmware SRAM */
		printf("NORD2 MCUPM sram +0x%03x: %08x %08x %08x %08x\n", w * 4, sram[w], sram[w + 1], sram[w + 2], sram[w + 3]);
	}
	printf("NORD2 MCUPM probe end\n");
}

/*
 * n2mcu=2 / n2mcu=3: the second READ-ONLY look at the MCUPM (patch 0114, README-0107-design.md section 11.6).  Same rules as n2mcu=1: only 32-bit loads,
 * no store to any mapped address, no interrupt, nothing sent.  At boot+60 s it prints the control registers and the five mailboxes, then reads the
 * firmware SRAM window the DT documents (0x10301000, 0xb800 bytes) into a snapshot and prints a 256-byte-block map (ZERO / NOISE / STRUCT runs, a
 * hexdump and printable strings of every STRUCT block, candidate DRAM pointers), then ten samples 100 ms apart of the control registers, mailbox 3
 * slot 8 (TASK_STA) and mailbox 4 slots 9..17.  n2mcu=3 additionally reads the gap between the window and the mailboxes (0x1030c800..0x1031fce0,
 * NOT mapped by the Android kernel: the only range here with an unknown bus answer; it is read last, after everything else has been printed).
 * At boot+180 s everything is read again and compared with the first reads (liveness).  The pure helpers are nord2_mcupm_scan.h.
 */
#define N2MCU_WIN_PA      0x10301000ull
#define N2MCU_WIN_WORDS   (0xb800u / 4u)
#define N2MCU_GAP_PA      0x1030c800ull
#define N2MCU_GAP_WORDS   ((0x1031fce0u - 0x1030c800u) / 4u)
#define N2MCU_MAP_PA      0x10301000ull
#define N2MCU_MAP_SIZE    0x1f000u
#define N2MCU_MB_PA       0x1031fce0u
#define N2MCU_MB_WORDS    (5u * 0xa0u / 4u)
#define N2MCU_SECOND_S    120u   /* second read at boot + 60 + 120 = 180 s */
#define N2MCU_MAXLINES    64u

static uint32_t n2mcu_win[N2MCU_WIN_WORDS];
static uint32_t n2mcu_gap[N2MCU_GAP_WORDS];
static uint32_t n2mcu_mb[N2MCU_MB_WORDS];
static uint32_t n2mcu_reg[4];
static unsigned n2mcu_idx[N2MCU_MAXLINES];

static void
n2mcu_copy(volatile uint32_t *src, uint32_t *dst, unsigned words)
{
	for (unsigned i = 0; i < words; i++) {
		dst[i] = src[i];
	}
}

static void
n2mcu_wait_ms(unsigned ms)
{
	assert_wait_timeout((event_t)&n2mcu_idx, THREAD_UNINT, ms, NSEC_PER_MSEC);
	thread_block(THREAD_CONTINUE_NULL);
}

static const char *
n2mcu_cname(enum n2ms_class c)
{
	return c == N2MS_ZERO ? "ZERO" : (c == N2MS_NOISE ? "NOISE" : "STRUCT");
}

/* Print the block map of `w` (a region of `words` words at physical address `pa`): runs of ZERO/NOISE blocks collapse into one line, every STRUCT block gets
 * its numbers, the first eight words and up to two printable strings. */
static void
n2mcu_map(const char *name, uint64_t pa, const uint32_t *w, unsigned words)
{
	int open = -1;                                         /* class of the ZERO/NOISE run that is open, or -1 */
	unsigned run_start = 0, counts[3] = { 0, 0, 0 };
	unsigned blocks = (words + N2MS_BLOCK_WORDS - 1) / N2MS_BLOCK_WORDS;
	for (unsigned b = 0; b < blocks; b++) {
		unsigned n = words - b * N2MS_BLOCK_WORDS;
		if (n > N2MS_BLOCK_WORDS) {
			n = N2MS_BLOCK_WORDS;
		}
		struct n2ms_block m;
		n2ms_measure(w + b * N2MS_BLOCK_WORDS, n, &m);
		enum n2ms_class c = n < N2MS_BLOCK_WORDS ? N2MS_STRUCT : n2ms_class_of(&m, n);
		counts[c]++;
		if (c != N2MS_STRUCT && open == (int)c) {
			continue;                                      /* extends the open run */
		}
		if (open >= 0) {
			printf("NORD2 MCUPM map %s %s run +0x%05x..+0x%05x (%u blocks)\n", name, n2mcu_cname((enum n2ms_class)open),
			    run_start * N2MS_BLOCK_WORDS * 4, b * N2MS_BLOCK_WORDS * 4 - 1, b - run_start);
			open = -1;
		}
		if (c != N2MS_STRUCT) {
			open = (int)c;
			run_start = b;
			continue;
		}
		const uint32_t *p = w + b * N2MS_BLOCK_WORDS;
		printf("NORD2 MCUPM map %s +0x%05x STRUCT zero=%u ones=%u rep=%u dup=%u : %08x %08x %08x %08x %08x %08x %08x %08x\n", name,
		    b * N2MS_BLOCK_WORDS * 4, m.zero, m.ones, m.rep, m.dup, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		unsigned off = 0, len = 0, from = 0, shown = 0;
		while (shown < 2 && n2ms_ascii_next((const uint8_t *)p, n * 4, from, 6, &off, &len)) {
			printf("NORD2 MCUPM map %s +0x%05x ascii \"%.*s\"%s\n", name, b * N2MS_BLOCK_WORDS * 4 + off, len > 40 ? 40 : (int)len,
			    (const char *)p + off, len > 40 ? "..." : "");
			from = off + len;
			shown++;
		}
	}
	if (open >= 0) {
		printf("NORD2 MCUPM map %s %s run +0x%05x..+0x%05x (%u blocks)\n", name, n2mcu_cname((enum n2ms_class)open),
		    run_start * N2MS_BLOCK_WORDS * 4, words * 4 - 1, blocks - run_start);
	}
	printf("NORD2 MCUPM map %s %#llx +0x%05x bytes: zero blocks %u, noise blocks %u, structured blocks %u\n", name, pa, words * 4, counts[0], counts[1],
	    counts[2]);
}

/* Words that could point at the bootloader's MCUPM-reserved mblock (strict) or at a page in DRAM bank 0 (loose, expect noise false positives). */
static void
n2mcu_ptrs(const char *name, uint64_t pa, const uint32_t *w, unsigned words)
{
	unsigned strict = 0, loose = 0;
	for (unsigned i = 0; i < words; i++) {
		if (n2ms_ptr_mblock(w[i])) {
			if (strict++ < N2MCU_MAXLINES) {
				printf("NORD2 MCUPM ptr %s %#llx = %08x (inside mblock-13-MCUPM-reserved 0x7e900000+1MiB)\n", name, pa + 4ull * i, w[i]);
			}
		} else if (n2ms_ptr_dram_page(w[i])) {
			if (loose++ < N2MCU_MAXLINES) {
				printf("NORD2 MCUPM ptr %s %#llx = %08x (page-aligned, DRAM bank 0; may be noise)\n", name, pa + 4ull * i, w[i]);
			}
		}
	}
	printf("NORD2 MCUPM ptr %s: %u mblock-range words, %u page-aligned DRAM words\n", name, strict, loose);
}

static void
n2mcu_diff_report(const char *name, uint64_t pa, const uint32_t *old, volatile uint32_t *src, unsigned words, uint32_t *tmp)
{
	n2mcu_copy(src, tmp, words);
	unsigned changed = n2ms_diff(old, tmp, words, n2mcu_idx, N2MCU_MAXLINES);
	printf("NORD2 MCUPM live %s: %u of %u words changed between the reads\n", name, changed, words);
	for (unsigned i = 0; i < changed && i < N2MCU_MAXLINES; i++) {
		printf("NORD2 MCUPM live %s %#llx: %08x -> %08x\n", name, pa + 4ull * n2mcu_idx[i], old[n2mcu_idx[i]], tmp[n2mcu_idx[i]]);
	}
}

/*
 * n2mcuping=1 (with n2mcu=2): the ONE write test of the MCUPM work (patch 0115, README-0107-design.md section 12.6).  It sends the stock alive check
 * (mcupm_alive_show: IPI id 0, message {0xdead, 0, 0}) through mailbox 0: three 32-bit stores to 0x1031fce0..e8 and one store of 1 to the doorbell register
 * 0x10300154, once, then polls the incoming status 0x10300078 and the outgoing status 0x10300150 for at most 20 ms and reads the reply word at 0x1031fd30.
 * No retry, no clearing of a pending doorbell, nothing in mailbox 3/4, no CSRAM write, no interrupt enabled.  It only runs from the state both probes
 * saw (all control registers 0, TASK_STA == 1); after it the mailboxes and the window tail are sampled for 5 s and every change is printed.
 */
#define N2MCU_TAIL_WORD0  (0x8000u / 4u)
#define N2MCU_TAIL_WORDS  ((0xb800u - 0x8000u) / 4u)
static uint32_t n2mcu_pre_mb[N2MCU_MB_WORDS];
static uint32_t n2mcu_pre_tail[N2MCU_TAIL_WORDS];

static void
n2mcu_ping(volatile uint32_t *ctl, volatile uint32_t *win, volatile uint32_t *mb)
{
	uint32_t regs[4] = { ctl[N2MS_PING_CLR_OFF / 4], ctl[N2MS_PING_RECV_OFF / 4], ctl[N2MS_PING_SEND_OFF / 4], ctl[N2MS_PING_SET_OFF / 4] };
	uint32_t sta = mb[3 * 0xa0 / 4 + 8];
	if (!n2ms_ping_pre_ok(regs, sta)) {
		printf("NORD2 MCUPM ping SKIPPED: preconditions failed (regs %08x %08x %08x %08x, TASK_STA %08x)\n", regs[0], regs[1], regs[2], regs[3], sta);
		return;
	}
	printf("NORD2 MCUPM ping begin: preconditions ok (regs 0, TASK_STA 1); sending {0xdead,0,0} to mailbox 0, doorbell bit 0\n");
	mb[0] = N2MS_PING_CMD;
	mb[1] = 0;
	mb[2] = 0;
	__asm__ volatile ("dsb st" ::: "memory");
	n2mcu_copy(mb, n2mcu_pre_mb, N2MCU_MB_WORDS);                         /* baseline AFTER our own stores, BEFORE the doorbell */
	n2mcu_copy(win + N2MCU_TAIL_WORD0, n2mcu_pre_tail, N2MCU_TAIL_WORDS);
	ctl[N2MS_PING_SET_OFF / 4] = 1;
	__asm__ volatile ("dsb st" ::: "memory");
	uint32_t set_rb = ctl[N2MS_PING_SET_OFF / 4], send_rb = ctl[N2MS_PING_SEND_OFF / 4];
	printf("NORD2 MCUPM ping doorbell rung: readback set %08x send-status %08x\n", set_rb, send_rb);
	bool recv_bit = false, send_seen = (send_rb & 1u) != 0, send_cleared = false;
	unsigned first_us = 0;
	uint32_t reply[4] = { 0, 0, 0, 0 };
	for (unsigned i = 0; i < N2MS_PING_POLL_MAX; i++) {
		uint32_t rv = ctl[N2MS_PING_RECV_OFF / 4], sv = ctl[N2MS_PING_SEND_OFF / 4];
		if ((sv & 1u) != 0) {
			send_seen = true;
		} else if (send_seen && !send_cleared) {
			send_cleared = true;
			if (first_us == 0) {
				first_us = i * N2MS_PING_POLL_US;
			}
			printf("NORD2 MCUPM ping: outgoing status bit 0 cleared after %u us\n", i * N2MS_PING_POLL_US);
		}
		if ((rv & 1u) != 0) {
			recv_bit = true;
			first_us = i * N2MS_PING_POLL_US;
			for (unsigned k = 0; k < 4; k++) {
				reply[k] = mb[N2MS_PING_REPLY_OFF / 4 + k];
			}
			ctl[N2MS_PING_CLR_OFF / 4] = 1;                                /* clear the incoming bit, as mtk_mbox_polling does */
			__asm__ volatile ("dsb st" ::: "memory");
			break;
		}
		delay(N2MS_PING_POLL_US);
	}
	printf("NORD2 MCUPM ping poll done: reply bit %u after %u us, reply words %08x %08x %08x %08x, regs now %08x %08x %08x %08x\n", recv_bit, first_us,
	    reply[0], reply[1], reply[2], reply[3], ctl[N2MS_PING_CLR_OFF / 4], ctl[N2MS_PING_RECV_OFF / 4], ctl[N2MS_PING_SEND_OFF / 4], ctl[N2MS_PING_SET_OFF / 4]);
	/* 5 s of observation: print the number of changed words whenever it changes, then the first changed words */
	unsigned last_mb = ~0u, last_tail = ~0u, mb_changed = 0, tail_changed = 0;
	static uint32_t cur[N2MCU_TAIL_WORDS];
	for (unsigned t = 0; t < 50; t++) {
		n2mcu_copy(mb, cur, N2MCU_MB_WORDS);
		mb_changed = n2ms_diff(n2mcu_pre_mb, cur, N2MCU_MB_WORDS, n2mcu_idx, N2MCU_MAXLINES);
		static uint32_t cur_tail[N2MCU_TAIL_WORDS];
		n2mcu_copy(win + N2MCU_TAIL_WORD0, cur_tail, N2MCU_TAIL_WORDS);
		tail_changed = n2ms_diff(n2mcu_pre_tail, cur_tail, N2MCU_TAIL_WORDS, n2mcu_idx, 0);
		if (mb_changed != last_mb || tail_changed != last_tail) {
			printf("NORD2 MCUPM ping +%u ms: %u mailbox words, %u window-tail words differ from the pre-doorbell read; regs %08x %08x %08x %08x\n",
			    t * 100, mb_changed, tail_changed, ctl[N2MS_PING_CLR_OFF / 4], ctl[N2MS_PING_RECV_OFF / 4], ctl[N2MS_PING_SEND_OFF / 4], ctl[N2MS_PING_SET_OFF / 4]);
			last_mb = mb_changed;
			last_tail = tail_changed;
		}
		if (t == 49) {
			n2mcu_copy(mb, cur, N2MCU_MB_WORDS);
			unsigned n = n2ms_diff(n2mcu_pre_mb, cur, N2MCU_MB_WORDS, n2mcu_idx, N2MCU_MAXLINES);
			for (unsigned i = 0; i < n && i < N2MCU_MAXLINES; i++) {
				printf("NORD2 MCUPM ping mbox %#x: %08x -> %08x\n", N2MS_PING_MBOX0_PA + 4 * n2mcu_idx[i], n2mcu_pre_mb[n2mcu_idx[i]], cur[n2mcu_idx[i]]);
			}
			unsigned m = n2ms_diff(n2mcu_pre_tail, cur_tail, N2MCU_TAIL_WORDS, n2mcu_idx, N2MCU_MAXLINES);
			for (unsigned i = 0; i < m && i < N2MCU_MAXLINES; i++) {
				printf("NORD2 MCUPM ping win %#x: %08x -> %08x\n", 0x10301000u + 4 * (N2MCU_TAIL_WORD0 + n2mcu_idx[i]), n2mcu_pre_tail[n2mcu_idx[i]], cur_tail[n2mcu_idx[i]]);
			}
		}
		n2mcu_wait_ms(100);
	}
	enum n2ms_ping_out out = n2ms_ping_classify(recv_bit, reply[0], send_seen && send_cleared, mb_changed + tail_changed);
	static const char *const what[] = {
		"A: reply bit set, reply word non-zero: the firmware serviced the platform channel (stock 'Alive')",
		"B: reply bit set, reply word 0: the firmware answered but reports 'dead' in the stock sense",
		"C: no reply, but the doorbell was consumed or other words changed: stop and think",
		"D: nothing changed: the firmware does not service the AP before AP_READY, or is halted",
	};
	printf("NORD2 MCUPM ping outcome %s\n", what[out]);
}

static void
n2mcu_probe2(int mode)
{
	volatile uint32_t *ctl = (volatile uint32_t *)ml_io_map(0x10300000, 0x1000);
	volatile uint32_t *map = (volatile uint32_t *)ml_io_map(N2MCU_MAP_PA, N2MCU_MAP_SIZE);
	if (ctl == NULL || map == NULL) {
		printf("NORD2 MCUPM probe2: mapping failed\n");
		return;
	}
	volatile uint32_t *win = map;                                                 /* 0x10301000 */
	volatile uint32_t *gap = map + (N2MCU_GAP_PA - N2MCU_MAP_PA) / 4;             /* 0x1030c800 */
	volatile uint32_t *mb = map + (N2MCU_MB_PA - N2MCU_MAP_PA) / 4;               /* 0x1031fce0 */
	static const unsigned regoff[4] = { 0x74 / 4, 0x78 / 4, 0x150 / 4, 0x154 / 4 };

	printf("NORD2 MCUPM probe2 begin (read-only, n2mcu=%d)\n", mode);
	n2mcu_probe();                                                                   /* control registers, five mailboxes, SRAM head (as n2mcu=1) */
	for (unsigned i = 0; i < 4; i++) {
		n2mcu_reg[i] = ctl[regoff[i]];
	}
	n2mcu_copy(mb, n2mcu_mb, N2MCU_MB_WORDS);
	n2mcu_copy(win, n2mcu_win, N2MCU_WIN_WORDS);
	printf("NORD2 MCUPM window read: %u words at 0x10301000\n", N2MCU_WIN_WORDS);
	n2mcu_map("win", N2MCU_WIN_PA, n2mcu_win, N2MCU_WIN_WORDS);
	n2mcu_ptrs("win", N2MCU_WIN_PA, n2mcu_win, N2MCU_WIN_WORDS);
	n2mcu_ptrs("mbox", N2MCU_MB_PA, n2mcu_mb, N2MCU_MB_WORDS);
	for (unsigned t = 0; t < 10; t++) {
		/* time series: control registers, mbox3 slot 8 (TASK_STA, mailbox 3 = 0x1031fec0, +0x20), mbox4 slots 9..17 (0x1031ff60 + 0x24..0x44) */
		const volatile uint32_t *m3 = mb + 3 * 0xa0 / 4, *m4 = mb + 4 * 0xa0 / 4;
		printf("NORD2 MCUPM ts %u: regs %08x %08x %08x %08x mbox3.s8 %08x mbox4.s9-17 %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", t,
		    ctl[regoff[0]], ctl[regoff[1]], ctl[regoff[2]], ctl[regoff[3]], m3[8], m4[9], m4[10], m4[11], m4[12], m4[13], m4[14], m4[15], m4[16], m4[17]);
		n2mcu_wait_ms(100);
	}
	if (nord2_mcuping_arg == 1) {
		if (mode == 2) {
			n2mcu_ping(ctl, win, mb);
		} else {
			printf("NORD2 MCUPM ping SKIPPED: only together with n2mcu=2\n");
		}
	}
	if (mode >= 3) {
		printf("NORD2 MCUPM gap read begins: 0x1030c800..0x1031fce0 (not mapped by the Android kernel)\n");
		n2mcu_copy(gap, n2mcu_gap, N2MCU_GAP_WORDS);
		printf("NORD2 MCUPM gap read done: %u words\n", N2MCU_GAP_WORDS);
		n2mcu_map("gap", N2MCU_GAP_PA, n2mcu_gap, N2MCU_GAP_WORDS);
		n2mcu_ptrs("gap", N2MCU_GAP_PA, n2mcu_gap, N2MCU_GAP_WORDS);
	}

	assert_wait_timeout((event_t)&n2mcu_idx, THREAD_UNINT, N2MCU_SECOND_S, NSEC_PER_SEC);
	thread_block(THREAD_CONTINUE_NULL);
	printf("NORD2 MCUPM liveness pass begin (second read, %us after the first)\n", N2MCU_SECOND_S);
	static uint32_t tmp[N2MCU_GAP_WORDS];                                           /* large enough for every range */
	for (unsigned i = 0; i < 4; i++) {
		uint32_t v = ctl[regoff[i]];
		if (v != n2mcu_reg[i]) {
			printf("NORD2 MCUPM live reg %u: %08x -> %08x\n", i, n2mcu_reg[i], v);
		}
	}
	n2mcu_diff_report("mbox", N2MCU_MB_PA, n2mcu_mb, mb, N2MCU_MB_WORDS, tmp);
	n2mcu_diff_report("win", N2MCU_WIN_PA, n2mcu_win, win, N2MCU_WIN_WORDS, tmp);
	if (mode >= 3) {
		n2mcu_diff_report("gap", N2MCU_GAP_PA, n2mcu_gap, gap, N2MCU_GAP_WORDS, tmp);
	}
	printf("NORD2 MCUPM probe2 end\n");
}

/*
 * n2dvfs=<n> (patch 0116): the CPU DVFS bring-up of nord2_dvfs.h on the device.  n2dvfs=1: platform init and the DVFS hand-over with every cluster pinned at
 * the bootloader row, no frequency change.  n2dvfs=<MHz >= 500>: the same, then the measured ramp of the A78 clusters (L, B) up to <MHz> with the battery guard,
 * and the 0099 placement once the big cores measure faster than the A55s (unless n2sch= was given).  Every step logs a `NORD2 DVFS` line; any unexpected reply or
 * measurement runs the fallback (limits and requests back to the bootloader rows) and stops.  Ignores n2cpu, n2mcu and n2mcuping.
 */
#define N2DV_CTL_PA        0x10300000ull
#define N2DV_MBOX_PAGE_PA  0x1031f000ull
#define N2DV_MBOX_BASE     0xce0u          /* offset of mailbox 0 in the mailbox page; mailbox n at + 0xa0 * n */

static volatile uint32_t *n2dv_ctl, *n2dv_mbpage, *n2dv_share, *n2dv_share_end;

static int
n2dv_mbox(__unused void *ctx, unsigned ch, const uint32_t *w, unsigned n, uint32_t *reply, unsigned nreply, unsigned timeout_ms)
{
	if (ch > 1 || n == 0 || n > 9 || nreply > 4 || n2dv_ctl == NULL || n2dv_mbpage == NULL) {
		return -1;
	}
	volatile uint32_t *box = n2dv_mbpage + (N2DV_MBOX_BASE + 0xa0u * ch) / 4;
	uint32_t bit = 1u << ch;
	if ((n2dv_ctl[N2MS_PING_SEND_OFF / 4] & bit) != 0 || (n2dv_ctl[N2MS_PING_RECV_OFF / 4] & bit) != 0) {
		return -3;                                                          /* a previous doorbell is still pending, or an unread reply: do not stack */
	}
	for (unsigned i = 0; i < n; i++) {
		box[i] = w[i];
	}
	__asm__ volatile ("dsb st" ::: "memory");
	n2dv_ctl[N2MS_PING_SET_OFF / 4] = bit;
	__asm__ volatile ("dsb st" ::: "memory");
	unsigned waited_us = 0;
	for (;;) {
		if ((n2dv_ctl[N2MS_PING_RECV_OFF / 4] & bit) != 0) {
			for (unsigned k = 0; k < nreply; k++) {
				reply[k] = box[N2MS_PING_REPLY_OFF / 4 + k];
			}
			n2dv_ctl[N2MS_PING_CLR_OFF / 4] = bit;                          /* clear the incoming bit, as mtk_mbox_polling does */
			__asm__ volatile ("dsb st" ::: "memory");
			return 0;
		}
		if (waited_us >= timeout_ms * 1000u) {
			return -2;                                                      /* the doorbell stays pending: no retry, no clearing */
		}
		if (waited_us < 20000u) {
			delay(50);
			waited_us += 50;
		} else {
			n2cf_sleep_ms(NULL, 1);
			waited_us += 1000;
		}
	}
}

/* Stock mcupm_plt_init: the header (magic, sizeof(struct plt_ctrl_s) = 12, region size) at the start and the magic mark in the last word.  Only the first and the last
 * 16 KiB of the region are mapped (device memory, 32-bit stores): the firmware has no DRAM access in this configuration and the stock driver zeroes only the header. */
static bool
n2dv_share_init(__unused void *ctx, uint32_t pa, uint32_t size)
{
	if (n2dv_share == NULL) {
		n2dv_share = (volatile uint32_t *)ml_io_map(pa, 0x4000);
	}
	if (n2dv_share_end == NULL) {
		n2dv_share_end = (volatile uint32_t *)ml_io_map(pa + size - 0x4000, 0x4000);
	}
	if (n2dv_share == NULL || n2dv_share_end == NULL) {
		return false;
	}
	n2dv_share_end[0x4000 / 4 - 1] = N2D_PLT_INIT;
	n2dv_share[0] = N2D_PLT_INIT;                                       /* the start mark, then plt_ctl->magic */
	n2dv_share[1] = 12;                                                 /* sizeof(struct plt_ctrl_s) */
	n2dv_share[2] = size;                                               /* mem_sz */
	__asm__ volatile ("dsb sy" ::: "memory");
	return n2dv_share[0] == N2D_PLT_INIT && n2dv_share[1] == 12 && n2dv_share[2] == size && n2dv_share_end[0x4000 / 4 - 1] == N2D_PLT_INIT;
}

static bool
n2dv_rail(__unused void *ctx, unsigned cluster, uint32_t *vproc, uint32_t *vsram)
{
	struct n2dv_rail_regs r = n2dv_cluster_rails(cluster);
	unsigned a = 0, b = 0;
	if (nord2_pmic_read(0, r.vproc_sid, r.vproc_reg, &a) != 0 || nord2_pmic_read(1, 0, r.vsram_reg, &b) != 0) {
		return false;
	}
	*vproc = n2dv_vproc_10uv(a & 0xffu);
	*vsram = n2dv_vsram_10uv((b >> 8) & 0x7fu);
	return *vproc != 0 && *vsram != 0;
}

/* The console log is pulled by the host (control requests about every 250 ms): wait long enough for it to fetch the line that announces the next hardware access. */
static void
n2dv_flush(__unused void *ctx)
{
	n2cf_sleep_ms(NULL, 400);
}

static void
n2dv_set_place(__unused void *ctx, int mode)
{
	nord2_sched_mode = mode;
}

static void
n2dv_log(__unused void *ctx, int code, unsigned a, unsigned b, unsigned c, unsigned d)
{
	static const char *const cl[] = { "LL", "L", "B", "CCI" };
	switch (code) {
	case N2D_L_PREFLIGHT:
		if (c == 99) {
			printf("NORD2 DVFS step 0 preflight: cluster %s measures %u MHz: NOT the bootloader state of the earlier boots, nothing will be sent\n", cl[a], b);
		} else {
			printf("NORD2 DVFS step 0 preflight: cluster %s measures %u MHz -> bootloader row %u (%u MHz)\n", cl[a], b, c, d);
		}
		break;
	case N2D_L_RAIL:
		printf("NORD2 DVFS step 0 rails cluster %s%s: Vproc %u.%02u mV, Vsram %u.%02u mV: %s\n", cl[a], a >= 2 ? " (shared rails with LL/L, not read)" : " (read)", b / 100, b % 100, c / 100, c % 100,
		    d ? "ok" : "REFUSED (unreadable, below 500 mV or above 1120 mV, Vsram below Vproc, or Vsram more than 250 mV above Vproc)");
		break;
	case N2D_L_SHARE:
		printf("NORD2 DVFS step 0 share buffer %#x +%#x: %s\n", a, b, c ? "mapped, zeroed, stock header written and read back" : "REFUSED");
		break;
	case N2D_L_PLT:
		if (a == 3) {
			printf("NORD2 DVFS step 1 platform init: skipped on purpose (the stock kernel sends no platform init), no share buffer\n");
		} else if (a == 0) {
			printf("NORD2 DVFS step 1 platform init: sending {%#x, %#x, %#x} to mailbox 0\n", N2D_PLT_INIT, b, c);
		} else if (a == 1) {
			printf("NORD2 DVFS step 1 platform init: reply %#x (%s)\n", b, b == 1 ? "accepted" : "NOT 1: refused");
		} else {
			printf("NORD2 DVFS step 1 platform init: no reply (%d)\n", (int)b);
		}
		break;
	case N2D_L_CSRAM:
		if (a == 0) {
			printf("NORD2 DVFS step 2 CSRAM: pinned init plan stored (table, CCI map, limits, requests)\n");
		} else {
			printf("NORD2 DVFS step 2 CSRAM readback: %s (%u mismatching words)\n", a == 1 ? "verified" : "MISMATCH", b);
		}
		break;
	case N2D_L_IPI:
		printf("NORD2 DVFS step 2 DVFS command %u arg %u: %s (status %d, first reply word %#x)\n", a, b, (int)c == 0 ? "reply seen" : "FAILED", (int)c, d);
		break;
	case N2D_L_INITOK:
		printf("NORD2 DVFS step 2 cluster %s: firmware reports row %u, CPU measures %u MHz (pinned row %u MHz)\n", cl[a], b, c, d);
		break;
	case N2D_L_RAMP:
		if (d) {
			printf("NORD2 DVFS step 3 cluster %s row %u (%u MHz): CPU measures %u MHz\n", cl[a], b, c, d);
		} else {
			printf("NORD2 DVFS step 3 cluster %s row %u (%u MHz): NOT reached\n", cl[a], b, c);
		}
		break;
	case N2D_L_RAMPDONE:
		printf("NORD2 DVFS step 3 cluster %s done: row %u -> %u, CPU measures %u MHz\n", cl[a], b, c, d);
		break;
	case N2D_L_THERMAL: {
		static const char *const why[] = { "?", "no ramp: battery at or above 45.0 C", "no ramp: battery gauge unreadable", "ramp stopped: battery at or above 45.0 C", "battery at or above 46.0 C: fastest allowed row one slower",
			                           "battery below 44.0 C: fastest allowed row one faster" };
		printf("NORD2 DVFS thermal: %s (battery %d.%02d C, cluster %s, row %u)\n", a < 6 ? why[a] : "?", (int)b / 100, (int)b % 100, c < 4 ? cl[c] : "-", d);
		break;
	}
	case N2D_L_PLACE:
		printf("NORD2 DVFS step 4 placement mode %#x (big cores measure %u MHz, A55s %u MHz)\n", a, b, c);
		break;
	case N2D_L_FALLBACK:
		printf("NORD2 DVFS FALLBACK (%u): limits and requests of LL/L/B pinned back to the bootloader rows %u/%u/%u, nothing more is sent\n", a, b, c, d);
		break;
	case N2D_L_ACCESS: {
		static const char *const what[] = { "?", "PMIC read: MT6315 SID 7 VBUCK3 and MT6359P VSRAM_PROC2 (LL rails)", "PMIC read: MT6315 SID 6 VBUCK1 and MT6359P VSRAM_PROC1 (L rails)",
			"share buffer: map the first and last 16 KiB and store the header", "mailbox 0 doorbell: platform init", "CSRAM stores: pinned plan (guards, zero area, limits, requests, table, CCI map)",
			"mailbox 1 doorbell: DVFS command", "CSRAM stores: sched-disable and cluster-on", "CSRAM stores: ramp step (limit, request, CCI request)" };
		printf("NORD2 DVFS access %u: next is %s%s", a, a < sizeof(what) / sizeof(what[0]) ? what[a] : "?", a == N2D_A_IPI || a == N2D_A_RAMP ? " " : "\n");
		if (a == N2D_A_IPI) {
			printf("(command %u arg %u)\n", b, c);
		} else if (a == N2D_A_RAMP) {
			printf("(cluster %s row %u)\n", cl[b & 3], c);
		}
		break;
	}
	case N2D_L_GOV:
		printf("NORD2 DVFS gov: L row %u (%u MHz) util %d permille, B row %u (%u MHz) util %d permille, %u row changes, boost %u permille of the ticks\n", a & 0xff,
		    n2d_row_mhz(N2DV_L, a & 0xff), (b & 0xffff) == 0xffff ? -1 : (int)(b & 0xffff), (a >> 8) & 0xff, n2d_row_mhz(N2DV_B, (a >> 8) & 0xff),
		    (b >> 16) == 0xffff ? -1 : (int)(b >> 16), c, d);
		break;
	case N2D_L_GOVCLK:
		printf("NORD2 DVFS gov clock: cluster %s asked %u MHz, measures %u MHz\n", cl[a & 3], b, c);
		break;
	case N2D_L_STAGE:
		printf("NORD2 DVFS stage limit %u reached: stopping on purpose before the next stage (0 preflight, 1 share buffer, 2 platform init, 3 hand-over)\n", a);
		break;
	case N2D_L_RESULT: {
		static const char *const res[] = { "?", "hand-over done, clusters pinned at the bootloader rows", "ramp reached the cap rows", "STOPPED in preflight: clocks (nothing sent)",
			                           "STOPPED in preflight: rails (nothing sent)", "STOPPED in preflight: share buffer (nothing sent)", "STOPPED: platform init refused",
			                           "STOPPED: CSRAM readback mismatch", "STOPPED: DVFS command failed", "STOPPED: a clock disagreed", "STOPPED: a ramp row was not reached", "STOPPED: bad arguments", "stage limit reached (on purpose)" };
		printf("NORD2 DVFS result %u: %s\n", a, a < sizeof(res) / sizeof(res[0]) ? res[a] : "?");
		break;
	}
	}
}

static volatile uint64_t n2dv_ui_kick;

static uint64_t
n2dv_now_ms(__unused void *ctx)
{
	uint64_t abs = 0, ns = 0;
	clock_get_uptime(&abs);
	absolutetime_to_nanoseconds(abs, &ns);
	return ns / 1000000ull;
}

/* 0172: called by the framebuffer driver for every CoreAnimation swap: the UI is active, the governor keeps the boost floor for N2D_GOV_BOOST_MS */
void
nord2_dvfs_ui_kick(void)
{
	n2dv_ui_kick = n2dv_now_ms(NULL);
}

static int
n2dv_util(__unused void *ctx, int cpu)
{
	return nord2_cpu_util_permille(cpu);
}

static uint64_t
n2dv_ui_kick_ms(__unused void *ctx)
{
	return n2dv_ui_kick;
}

static const struct n2d_ops n2dv_ops = { n2cf_wr, n2cf_rd, n2dv_mbox, n2dv_share_init, n2dv_rail, n2cf_mhz, n2cf_gauge, n2cf_sleep_ms, n2dv_set_place, n2dv_log, n2dv_flush,
	n2dv_util, n2dv_now_ms, n2dv_ui_kick_ms };

/* ---- patch 0120: CPU memory bandwidth ------------------------------------------------------------------------------------------
 * The GPU renders ~1 Gpixel/s whatever its clock, shader length or job count.  If the DRAM clock was left at a low bootloader value
 * (the CPU clocks were), the same limit shows on the CPU.  Plain cacheable kernel memory, 64 MiB per buffer (far above every cache),
 * best of 4 passes per test; nothing but DRAM is touched.  MB/s = 10^6 bytes/s; copy counts the bytes moved once. */
#define N2MB_BYTES   (64u << 20)
#define N2MB_PASSES  4u

static uint64_t
n2mb_ns(uint64_t t0, uint64_t t1)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(t1 - t0, &ns);
	return ns ? ns : 1;
}

static void
n2membw_run(const char *tag)
{
	vm_offset_t a = 0, b = 0;
	uint64_t best_w = 0, best_r = 0, best_c = 0;
	volatile uint64_t sink = 0;

	if (kmem_alloc(kernel_map, &a, N2MB_BYTES, KMA_KOBJECT | KMA_ZERO, VM_KERN_MEMORY_DIAG) != KERN_SUCCESS ||
	    kmem_alloc(kernel_map, &b, N2MB_BYTES, KMA_KOBJECT | KMA_ZERO, VM_KERN_MEMORY_DIAG) != KERN_SUCCESS) {
		printf("NORD2 MEMBW %s: allocation failed\n", tag);
		goto out;
	}
	for (unsigned pass = 0; pass < N2MB_PASSES; pass++) {
		uint64_t t0, t1, mbs;
		t0 = mach_absolute_time();
		memset((void *)a, 0x5a + (int)pass, N2MB_BYTES);
		t1 = mach_absolute_time();
		mbs = ((uint64_t)N2MB_BYTES * 1000ull) / n2mb_ns(t0, t1);
		if (mbs > best_w)
			best_w = mbs;

		t0 = mach_absolute_time();
		{
			const uint64_t *p = (const uint64_t *)a;
			uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
			for (size_t i = 0; i < N2MB_BYTES / 8; i += 8) {
				s0 += p[i] + p[i + 4];
				s1 += p[i + 1] + p[i + 5];
				s2 += p[i + 2] + p[i + 6];
				s3 += p[i + 3] + p[i + 7];
			}
			sink += s0 + s1 + s2 + s3;
		}
		t1 = mach_absolute_time();
		mbs = ((uint64_t)N2MB_BYTES * 1000ull) / n2mb_ns(t0, t1);
		if (mbs > best_r)
			best_r = mbs;

		t0 = mach_absolute_time();
		memcpy((void *)b, (const void *)a, N2MB_BYTES);
		t1 = mach_absolute_time();
		mbs = ((uint64_t)N2MB_BYTES * 1000ull) / n2mb_ns(t0, t1);
		if (mbs > best_c)
			best_c = mbs;
	}
	printf("NORD2 MEMBW %s: cpu %d (%u MHz), LL %u / L %u / B %u MHz: write %llu MB/s, read %llu MB/s, copy %llu MB/s (best of %u, %u MiB buffers)\n",
	    tag, cpu_number(), nord2_cpu_mhz(cpu_number()), nord2_cpu_mhz(0), nord2_cpu_mhz(4), nord2_cpu_mhz(7),
	    (unsigned long long)best_w, (unsigned long long)best_r, (unsigned long long)best_c, N2MB_PASSES, N2MB_BYTES >> 20);
	(void)sink;
out:
	if (a)
		kmem_free(kernel_map, a, N2MB_BYTES);
	if (b)
		kmem_free(kernel_map, b, N2MB_BYTES);
}

/* ---- patch 0121: DRAM clock state, read-only ------------------------------------------------------------------------------------
 * The stock kernel runs DVFSRC (DRAM 800..4266 MHz, 24 OPPs, 2400 MHz idle in the rescue).  Ours never touches it, so the DRAM may sit at
 * the bootloader level.  Only 32-bit reads of the DVFSRC page (0x10012000, a platform device of the stock kernel) are done; level 2 then
 * reads the DRAMC ch0 AO page (0x10230000) after the DVFSRC lines have been flushed.  All-zero lines are folded. */
static void
n2dram_page(const char *tag, uint32_t pa)
{
	volatile uint32_t *m = (volatile uint32_t *)ml_io_map(pa, 0x1000);
	unsigned zero = 0;

	if (m == NULL) {
		printf("NORD2 DRAM %s %08x: mapping failed\n", tag, pa);
		return;
	}
	for (unsigned o = 0; o < 0x400; o += 8) {
		uint32_t w[8];
		unsigned any = 0;
		for (unsigned i = 0; i < 8; i++) {
			w[i] = m[o + i];
			any |= w[i];
		}
		if (!any) {
			zero++;
			continue;
		}
		printf("NORD2 DRAM %s %08x+%03x: %08x %08x %08x %08x %08x %08x %08x %08x\n", tag, pa, o * 4, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
	}
	printf("NORD2 DRAM %s %08x: done, %u all-zero lines folded\n", tag, pa, zero);
}

static void
n2dram_run(const char *tag)
{
	if (nord2_dram_arg <= 0)
		return;
	n2dram_page(tag, 0x10012000);
	if (nord2_dram_arg >= 2)
		n2dram_page(tag, 0x10230000);
}

static void
n2dvfs_main(void)
{
	struct n2d_cfg cfg;
	struct n2d_state st;
	int user_sched = 0;
	cfg.cap_mhz = nord2_dvfs_arg >= 500 ? (unsigned)nord2_dvfs_arg : 0;
	cfg.place = cfg.cap_mhz != 0 && !PE_parse_boot_argn("n2sch", &user_sched, sizeof(user_sched));
	cfg.max_step = nord2_dvfs_stage_arg < 0 ? 0u : (nord2_dvfs_stage_arg > 3 ? 3u : (unsigned)nord2_dvfs_stage_arg);
	{
		int plt = 0;
		cfg.skip_plt = !PE_parse_boot_argn("n2dvplt", &plt, sizeof(plt)) || plt == 0;      /* n2dvplt=1 sends the P20 platform init again (the stock kernel does not) */
	}
	n2cf_csram = (volatile uint32_t *)ml_io_map(N2CF_CSRAM_PA, N2CF_CSRAM_SIZE);
	n2dv_ctl = (volatile uint32_t *)ml_io_map(N2DV_CTL_PA, 0x1000);
	n2dv_mbpage = (volatile uint32_t *)ml_io_map(N2DV_MBOX_PAGE_PA, 0x1000);
	if (n2cf_csram == NULL || n2dv_ctl == NULL || n2dv_mbpage == NULL) {
		printf("NORD2 DVFS: mapping failed, nothing done\n");
		return;
	}
	printf("NORD2 DVFS begin: n2dvfs=%d n2dvst=%u (%s), placement %s\n", nord2_dvfs_arg, cfg.max_step, cfg.cap_mhz ? "hand-over then ramp of L and B to the cap" : "hand-over only, no frequency change",
	    cfg.place ? "managed" : "not managed");
	n2dram_run("before DVFS");
	if (nord2_membw_arg)
		n2membw_run("before DVFS");
	enum n2d_result r = n2d_bringup(&n2dv_ops, NULL, &cfg, &st);
	printf("NORD2 DVFS bring-up finished with result %u\n", (unsigned)r);
	if (nord2_membw_arg)
		n2membw_run("after DVFS");
	n2dram_run("after DVFS");
	if (nord2_gov_arg != 0 && !st.stopped && st.inited && cfg.cap_mhz != 0) {
		struct n2d_gov gov;
		n2d_gov_init(&n2dv_ops, NULL, &st, &gov);
		printf("NORD2 DVFS gov: load-based scaling on (target %u permille, down hold %u ms, boost %u ms, UI floor L %u / B %u MHz, idle L %u / B %u MHz, limit rows L %u B %u)\n",
		    N2D_GOV_TARGET_PERMILLE, N2D_GOV_DOWN_HOLD_MS, N2D_GOV_BOOST_MS, N2D_GOV_FLOOR_L_MHZ, N2D_GOV_FLOOR_B_MHZ, N2D_GOV_IDLE_L_MHZ, N2D_GOV_IDLE_B_MHZ,
		    gov.limit[N2DV_L], gov.limit[N2DV_B]);
		while (!st.stopped && st.inited) {
			n2cf_sleep_ms(NULL, N2D_GOV_TICK_MS);
			n2d_gov_tick(&n2dv_ops, NULL, &cfg, &st, &gov);
		}
	}
	while (!st.stopped && st.inited && cfg.cap_mhz != 0) {
		n2cf_sleep_ms(NULL, 1000u * N2CF_TICK_S);
		n2d_tick(&n2dv_ops, NULL, &cfg, &st);
	}
	printf("NORD2 DVFS thread ends (stopped %u)\n", (unsigned)st.stopped);
}

__attribute__((noreturn)) static void
nord2_cpufreq_exit(void)
{
	(void)thread_terminate(current_thread());
	for (;;) {
		assert_wait_timeout((event_t)&nord2_cpufreq_exit, THREAD_UNINT, 1, NSEC_PER_SEC);
		thread_block(THREAD_CONTINUE_NULL);
	}
}

__attribute__((noreturn)) static void
nord2_cpufreq_thread(__unused void *arg, __unused wait_result_t wr)
{
	struct n2cf_domain dom[N2CF_CLUSTERS];
	unsigned ramped = 0;
	unsigned cap = nord2_cpufreq_arg >= 500 ? (unsigned)nord2_cpufreq_arg : 0;

	bzero(dom, sizeof(dom));
	if (nord2_dvfs_arg > 0) {
		/* 0142: the CPUs run at the bootloader clocks (the big clusters at 730 MHz) until this bring-up, and the first minute is when the
		 * user unlocks and opens the first apps.  It needs the GPU driver's PMIC owner (the rail reads), which is ready some 23 s into the
		 * boot: start as soon as it is (at least 10 s in, then 4 s of settle), and give up waiting after two minutes. */
		unsigned waited_ms = 0;
		/* 0147: the first minute is when the user unlocks and opens the first apps and the CPUs sit at the bootloader clocks until this
		 * bring-up, so it starts the moment the owner answers: poll every 100 ms (was a fixed 10 s first and then 500 ms), settle 1 s (was 4 s). */
		while (waited_ms < 110000 && !nord2_gpu_pmic_ready()) {
			assert_wait_timeout((event_t)&nord2_cpufreq_thread, THREAD_UNINT, 100, NSEC_PER_MSEC);
			thread_block(THREAD_CONTINUE_NULL);
			waited_ms += 100;
		}
		assert_wait_timeout((event_t)&nord2_cpufreq_thread, THREAD_UNINT, 1, NSEC_PER_SEC);
		thread_block(THREAD_CONTINUE_NULL);
		printf("NORD2 DVFS: GPU/PMIC owner %s after %u ms of waiting; bring-up starts now (was a fixed boot+60 s)\n", nord2_gpu_pmic_ready() ? "ready" : "NOT ready", waited_ms);
	} else {
		assert_wait_timeout((event_t)&nord2_cpufreq_thread, THREAD_UNINT, N2CF_FIRST_S, NSEC_PER_SEC);
		thread_block(THREAD_CONTINUE_NULL);
	}

	if (nord2_dvfs_arg > 0) {
		n2dvfs_main();
		nord2_cpufreq_exit();
	}
	if (nord2_membw_arg) {
		n2membw_run("boot clocks");
		nord2_cpufreq_exit();
	}
	if (nord2_mcu_arg == 1) {
		n2mcu_probe();
	} else if (nord2_mcu_arg >= 2) {
		n2mcu_probe2(nord2_mcu_arg);
	}
	if (nord2_cpufreq_arg <= 0) {
		nord2_cpufreq_exit();
	}
	n2cf_csram = (volatile uint32_t *)ml_io_map(N2CF_CSRAM_PA, N2CF_CSRAM_SIZE);
	if (n2cf_csram == NULL) {
		printf("NORD2 CPUFREQ: CSRAM unmapped\n");
		nord2_cpufreq_exit();
	}
	n2cf_probe_canonical(&n2cf_ops, NULL);
	(void)n2cf_csram_diagnose(&n2cf_ops, NULL, true);
	unsigned mask = n2cf_discover(&n2cf_ops, NULL, dom, true);
	printf("NORD2 CPUFREQ discovery done: verified clusters 0x%x (bit0 ll, bit1 l, bit2 b)%s\n", mask,
	    mask == 0 ? ": no cpufreq-hw domain verified, nothing will be written" : "");
	if (cap != 0 && mask != 0) {
		ramped = n2cf_start(&n2cf_ops, NULL, dom, cap);
	}
	/* Placement follows the measured advantage of the big cores, unless n2sch= was given on the command line. */
	int user_sched = 0;
	bool manage_placement = cap != 0 && !PE_parse_boot_argn("n2sch", &user_sched, sizeof(user_sched));
	unsigned place_ticks = 0;
	if (manage_placement) {
		n2cf_place(false);
	}
	while (ramped != 0 || manage_placement) {
		assert_wait_timeout((event_t)&nord2_cpufreq_thread, THREAD_UNINT, N2CF_TICK_S, NSEC_PER_SEC);
		thread_block(THREAD_CONTINUE_NULL);
		for (unsigned c = 0; c < N2CF_CLUSTERS; c++) {
			if (ramped & (1u << c)) {
				n2cf_guard_tick(&n2cf_ops, NULL, c, &dom[c], n2cf_target_row(&dom[c].lut, n2cf_cap_for(cap, c)));
			}
		}
		if (manage_placement && ++place_ticks >= 5) {                    /* every 10 s */
			place_ticks = 0;
			n2cf_place(false);
		}
	}
	nord2_cpufreq_exit();
}

static void
nord2_cpufreq_init(void)
{
	if (nord2_cpufreq_arg <= 0 && nord2_mcu_arg <= 0 && nord2_dvfs_arg <= 0) {
		return;
	}
	if (nord2_dvfs_arg > 0) {
		printf("NORD2 DVFS: n2dvfs=%d, bring-up at boot+%us (n2cpu, n2mcu and n2mcuping are ignored)\n", nord2_dvfs_arg, N2CF_FIRST_S);
	}
	if (nord2_mcuping_arg == 1 && nord2_mcu_arg == 2) {
		printf("NORD2 MCUPM: n2mcuping=1: ONE write test at boot+60s (alive check to mailbox 0, doorbell bit 0)\n");
	}
	if (nord2_mcu_arg > 0) {
		printf("NORD2 MCUPM: n2mcu=%d (read-only probe at boot+%us%s)\n", nord2_mcu_arg, N2CF_FIRST_S,
		    nord2_mcu_arg >= 3 ? ", plus the unmapped gap" : (nord2_mcu_arg == 2 ? ", plus window map and liveness diff at +180 s" : ""));
	}
	if (nord2_cpufreq_arg > 0) printf("NORD2 CPUFREQ: n2cpu=%d (%s)\n", nord2_cpufreq_arg,
	    nord2_cpufreq_arg >= 500 ? "discover, then ramp verified big clusters" : "discovery only, nothing is written");
	thread_t thread;
	if (kernel_thread_start(nord2_cpufreq_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_set_thread_name(thread, "nord2_cpufreq");
		thread_deallocate(thread);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, nord2_cpufreq_init);

#endif /* ARM64_BOARD_CONFIG_SWEET */
