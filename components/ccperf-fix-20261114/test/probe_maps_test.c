/* ccperf-fix-20261114: host test of 0494.  The DRAM section of nord2_cpufreq.c (n2dram_page .. n2dram_rate_watch, cut out of the real source by run.sh into
 * EXTRACT) runs here against a model of the two kernel allocators it can map from:
 *   ml_io_map            -> io_submap: 512 pages of 16 KiB, permanent, KMA_NOFAIL (osfmk/arm/io_map.c); the model counts pages and "panics" when it is full
 *   ml_io_map_unmappable -> kernel_map: counted, never full
 * Scenario = the q10jcc boots: 501 pages taken (every q10jia/q10jib log), governor start runs the probe (n2drp=3), 30 watch reads, then the SCP thread's maps
 * (TCM, CFG, CORE0, CORE1, TOPCK+INFRA, MBOX0-4 = 2 pages, FIFO: 8 pages).  The register contents are the ones the phone printed (shu_lv 0, 3733 Mbps).
 * Output: the lines the code prints (compared a/ against b/ by run.sh, map lines apart) and a RESULT line. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uintptr_t vm_offset_t; typedef size_t vm_size_t;
#define __unused __attribute__((unused))
#define TUNABLE_WRITEABLE(t, n, s, d) static t n = d
#define SYSCTL_INT(...) struct n2_sysctl_unused
#define PG 0x4000u
static int nord2_dram_arg;
static unsigned iosub_used = 501, iosub_taken_here, kmap_pages, slept_ms, bad_flags;
static bool iosub_panic;
struct win { uint32_t pa; uint32_t *mem; };
static struct win wins[64]; static unsigned nwins;
static unsigned pages_of(vm_offset_t pa, vm_size_t sz) { return (unsigned)(((pa & (PG - 1)) + sz + PG - 1) / PG); }
static vm_offset_t backing(vm_offset_t pa, vm_size_t sz)
{
	unsigned n = pages_of(pa, sz); uint32_t base = (uint32_t)pa & ~(PG - 1);
	uint32_t *m = calloc(n, PG); assert(m && nwins < 64);
	for (unsigned p = 0; p < n; p++) {                                  /* what the phone printed (q10jcc, both boots) */
		uint32_t pg = base + p * PG;
		if (pg == 0x10234000u) m[(p * PG + 0x2000 + 0x50c) / 4] = 0x100;                          /* DDRPHY NAO CHA 0x10236000 + 0x50c: shu_lv 0, pll 1 */
		if (pg == 0x10238000u) { m[(p * PG + 0x724) / 4] = 0x8f000000u; m[(p * PG + 0x728) / 4] = 1u << 18; }   /* AO: sdmpcw 0x8f00, prediv 1 */
		if (pg == 0x10010000u) m[(p * PG + 0x2000) / 4] = 8;                                      /* DVFSRC basic_control */
		if (pg == 0x10004000u) { m[(p * PG + 0x2000 + 0x4a4) / 4] = 4; m[(p * PG + 0x2000 + 0x4f8) / 4] = 0x20002; m[(p * PG + 0x2000 + 0x600) / 4] = 0x418; }
	}
	wins[nwins++] = (struct win){ base, m };
	return (vm_offset_t)m + (pa & (PG - 1));
}
vm_offset_t ml_io_map(vm_offset_t pa, vm_size_t sz);
vm_offset_t ml_io_map(vm_offset_t pa, vm_size_t sz)
{
	unsigned n = pages_of(pa, sz);
	if (iosub_used + n > 512) { iosub_panic = true; return backing(pa, sz); }      /* the real one panics here; the model goes on to report */
	iosub_used += n; iosub_taken_here += n;
	return backing(pa, sz);
}
vm_offset_t ml_io_map_unmappable(vm_offset_t pa, vm_size_t sz, uint32_t flags);
vm_offset_t ml_io_map_unmappable(vm_offset_t pa, vm_size_t sz, uint32_t flags)
{
	if (flags != 0x7u) bad_flags++;                                     /* VM_WIMG_IO: device memory, as ml_io_map maps it */
	kmap_pages += pages_of(pa, sz);
	return backing(pa, sz);
}
static void n2cf_sleep_ms(__unused void *ctx, unsigned ms) { slept_ms += ms; }
#include "nord2_dram_rate.h"
#include EXTRACT
int main(int argc, char **argv)
{
	int dram = argc > 1 ? atoi(argv[1]) : 0;                            /* n2dram boot-arg (0121): 0, 1, 2 */
	nord2_dram_arg = dram;
	n2dram_run("before DVFS"); n2dram_run("after DVFS");                /* n2dvfs_main */
	nord2_dram_probe = 3;                                               /* n2gdv +16 */
	n2dram_rate_probe(); nord2_dram_probe++;
	for (int i = 0; i < 30; i++) n2dram_rate_watch();                   /* 60 s of DECISION lines */
	unsigned by_dram = iosub_taken_here;
	static const struct { const char *what; uint32_t pa, sz; } scp[] = { { "TCM", 0x10500000, 0x1000 }, { "CFG", 0x10724000, 0x1000 }, { "CORE0", 0x10730000, 0x1000 },
		{ "CORE1", 0x10740000, 0x1000 }, { "TOPCK+INFRA", 0x10000000, 0x2000 }, { "MBOX0-4", 0x107fb000, 0x5000 }, { "FIFO", 0x8904c000, 0x4000 } };
	const char *full_at = "-";
	for (unsigned i = 0; i < 7 && !iosub_panic; i++) { (void)ml_io_map(scp[i].pa, scp[i].sz); if (iosub_panic) full_at = scp[i].what; }
	printf("RESULT n2dram=%d: io_submap pages taken by the DRAM reads %u, kernel_map pages %u, bad flags %u, io_submap after the SCP maps %u/512 free %d, %s%s\n", dram, by_dram,
	    kmap_pages, bad_flags, iosub_used, 512 - (int)iosub_used, iosub_panic ? "PANIC (io_submap full) at SCP map " : "no panic", iosub_panic ? full_at : "");
	return 0;
}
