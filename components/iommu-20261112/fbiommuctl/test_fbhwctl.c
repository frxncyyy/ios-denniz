/* host test of n2fbhwctl.c: config parser + schedule against a fake kernel (sysctl), including a 0396 auto-fallback.
 * cc -O1 -Wall -Wextra -fsanitize=address,undefined test_fbhwctl.c -o t && ./t */
#include <stdio.h>
#include <string.h>
#define N2FBHWCTL_HOST 1
static char logbuf[16384]; static int now, mode_k, pool_k = 1, has_mode = 1, fallback_at = -1, slept;
static int stage_k, stage_n;
static int iommu_k, has_iommu = 1, iommu_fault_at = -1, probes, iotest_k, hwtest_k, hwtest_t = -1, mode_set_t = -1, iommu_set_order;
static void out(const char *s) { strcat(logbuf, s); strcat(logbuf, "\n"); }
static int sc_get(const char *n, int *v)
{
	if (!strcmp(n, "kern.n2fbhw")) { if (!has_mode) return 2; if (fallback_at >= 0 && now >= fallback_at) mode_k = 0; *v = mode_k; return 0; }
	if (!strcmp(n, "kern.n2fbhw_pool")) { *v = pool_k; return 0; }
	if (!strcmp(n, "kern.n2fbiommu")) { if (!has_iommu) return 2; if (iommu_fault_at >= 0 && now >= iommu_fault_at) iommu_k = 0; *v = iommu_k; return 0; }
	return 2;
}
static int sc_set(const char *n, int v)
{
	if (!strcmp(n, "kern.n2fbhw")) { if (fallback_at >= 0 && now >= fallback_at && v) return 1; mode_k = v; mode_set_t = now; return 0; }   /* EPERM after fallback */
	if (!strcmp(n, "kern.n2fbhw_pool")) { pool_k = v; return 0; }
	if (!strcmp(n, "kern.n2fbiommu")) { if (!has_iommu) return 2; iommu_k = v; if (mode_set_t != now) iommu_set_order++; return 0; }
	if (!strcmp(n, "kern.n2fbiommu_probe")) { if (!has_iommu) return 2; probes++; return 0; }
	if (!strcmp(n, "kern.n2fbiommutest")) { if (!has_iommu) return 2; iotest_k = v; return 0; }
	if (!strcmp(n, "kern.n2fbhwtest")) { hwtest_k = v; hwtest_t = now; return 0; }
	if (!strcmp(n, "kern.n2fbiommu_stage")) { if (!has_iommu) return 2; stage_k = v; stage_n++; return 0; }
	return 2;
}
static void nap(unsigned s) { now += s; slept += s; }
#include "n2fbhwctl.c"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m); fails++; } else printf("ok   %s\n", m); } while (0)
static void reset(void) { logbuf[0] = 0; now = 0; mode_k = 0; pool_k = 1; has_mode = 1; fallback_at = -1; slept = 0;
	iommu_k = 0; has_iommu = 1; iommu_fault_at = -1; probes = 0; iotest_k = 0; hwtest_k = 0; hwtest_t = -1; mode_set_t = -1; iommu_set_order = 0; stage_k = -1; stage_n = 0; }

int main(void)
{
	struct fbcfg c;
	CHECK(fbcfg_parse("modes=0,1,2,3 period=120 delay=60", &c) == 0 && c.nmodes == 4 && c.modes[3] == 3 && c.period == 120 && c.pool == -1 && c.final == 0, "cycle config");
	CHECK(fbcfg_parse("modes=2 period=0 delay=30 pool=1\n", &c) == 0 && c.nmodes == 1 && c.modes[0] == 2 && c.pool == 1, "fixed config with newline");
	CHECK(fbcfg_parse("modes=4", &c) == 1, "mode 4 refused");
	CHECK(fbcfg_parse("period=10", &c) == 2, "no modes refused");
	CHECK(fbcfg_parse("modes=1 bogus=2", &c) == 2, "unknown token refused");
	CHECK(fbcfg_parse("modes=1 period=12x", &c) == 2, "trailing junk refused");
	CHECK(fbcfg_parse("modes=0,1,2,3,0,1,2,3,0", &c) == 1, "more than 8 modes refused");
	CHECK(fbcfg_parse("modes=1 pool=2", &c) == 2, "pool 2 refused");
	CHECK(fbcfg_parse("modes=1 final=-1 rounds=2", &c) == 0 && c.final == -1 && c.rounds == 2, "final -1, rounds 2");

	reset(); fbcfg_parse("modes=0,1,2,3 period=120 delay=60", &c);
	CHECK(fbctl_run(&c) == 0, "cycle runs");
	CHECK(slept == 60 + 4 * 120, "cycle sleeps delay + 4 periods");
	CHECK(strstr(logbuf, "N2FBHWCTL phase 3/4 mode=2 set rc=0 readback=2 t=300") != 0, "phase 3 line");
	CHECK(strstr(logbuf, "FALLBACK") == 0, "no fallback without one");
	CHECK(strstr(logbuf, "N2FBHWCTL done final=0 rc=0 readback=0 t=540") != 0 && mode_k == 0, "final mode 0 set");
	CHECK(strstr(logbuf, "N2FBHWCTL pool") == 0, "pool untouched at -1");

	reset(); fallback_at = 250; fbcfg_parse("modes=0,1,2,3 period=120 delay=60", &c); fbctl_run(&c);
	CHECK(strstr(logbuf, "phase_end 2/4 mode=1 readback=1 t=300") == 0 && strstr(logbuf, "phase_end 2/4 mode=1 readback=0 t=300 FALLBACK") != 0, "fallback seen at the end of mode 1");
	CHECK(strstr(logbuf, "phase 3/4 mode=2 set rc=1 readback=0") != 0, "after fallback the set is refused and logged");

	reset(); has_mode = 0; fbcfg_parse("modes=1 period=0", &c);
	CHECK(fbctl_run(&c) == 2 && strstr(logbuf, "unreadable") != 0, "kernel without kern.n2fbhw: error, nothing set");

	reset(); fbcfg_parse("modes=3 period=0 delay=20 pool=0", &c);
	CHECK(fbctl_run(&c) == 0 && mode_k == 3 && pool_k == 0 && slept == 20 && strstr(logbuf, "(fixed)") != 0, "fixed mode 3 with pool 0");
	CHECK(strstr(logbuf, "N2FBHWCTL pool set=0 rc=0 readback=0 t=0") != 0, "pool set at t=0, before the delay");
	/* 0401 fork */
	CHECK(fbcfg_parse("modes=3,3,3,3 iommu=0,1,2,3 probe=1 iotest=1 hwtest=6 period=120 delay=90 pool=1 final=0", &c) == 0 && c.niommu == 4 && c.iommu[2] == 2 && c.probe == 1 && c.iotest == 1 && c.hwtest == 6, "iommu cycle config");
	CHECK(fbcfg_parse("modes=3,3 iommu=1", &c) != 0, "iommu count must match modes");
	CHECK(fbcfg_parse("modes=3 iommu=4", &c) == 2, "iommu 4 refused");
	CHECK(fbcfg_parse("modes=3 hwtest=7", &c) == 2, "hwtest 7 refused");
	CHECK(fbcfg_parse("modes=3 probe=2", &c) == 2, "probe 2 refused");
	reset(); fbcfg_parse("modes=3,3,3,3 iommu=0,1,2,3 probe=1 iotest=1 hwtest=6 period=120 delay=90 pool=1 final=0", &c);
	CHECK(fbctl_run(&c) == 0, "iommu cycle runs");
	CHECK(slept == 90 + 4 * 120, "iommu cycle total time unchanged by hwtest");
	CHECK(strstr(logbuf, "N2FBHWCTL iommu probe rc=0 t=0") != 0 && probes == 5, "probe at start (before the delay) and at every phase_end");
	CHECK(iotest_k == 1 && strstr(logbuf, "N2FBHWCTL iommu iotest rc=0 t=0") != 0, "iotest set at start");
	CHECK(strstr(logbuf, "N2FBHWCTL iommu phase 3/4 iommu=2 rc=0 readback=2 t=330") != 0, "iommu phase line");
	CHECK(strstr(logbuf, "N2FBHWCTL phase 3/4 mode=3 set rc=0 readback=3 t=330") != 0, "unchanged phase line for fbhw_verdict.py");
	CHECK(strstr(logbuf, "N2FBHWCTL iommu phase 3/4") < strstr(logbuf, "N2FBHWCTL phase 3/4"), "iommu set before the mode");
	CHECK(hwtest_k == 6 && hwtest_t == 340 && strstr(logbuf, "N2FBHWCTL iommu hwtest 6 rc=0 t=340") != 0, "hwtest once, 10 s into the first iommu>=2 phase");
	CHECK(strstr(logbuf, "AUTO-DISABLE") == 0 && iommu_k == 0 && strstr(logbuf, "N2FBHWCTL iommu done iommu=0 rc=0 t=570") != 0, "iommu 0 at the end");
	reset(); iommu_fault_at = 400; fbcfg_parse("modes=3,3,3,3 iommu=0,1,2,3 period=120 delay=90", &c); fbctl_run(&c);
	CHECK(strstr(logbuf, "N2FBHWCTL iommu phase_end 3/4 iommu=2 readback=0 t=450 AUTO-DISABLE") != 0, "kernel auto-disable seen at phase_end");
	CHECK(strstr(logbuf, "N2FBHWCTL iommu phase_end 1/4 iommu=0 readback=0 t=210\n") != 0 && strstr(logbuf, "phase_end 1/4 iommu=0 readback=0 t=210 AUTO") == 0, "iommu 0 never flagged");
	reset(); has_iommu = 0; fbcfg_parse("modes=3,3 iommu=0,2 period=60 delay=10", &c);
	CHECK(fbctl_run(&c) == 0 && strstr(logbuf, "N2FBHWCTL iommu phase 2/2 iommu=2 rc=2 readback=-1") != 0 && mode_k == 0, "kernel without 0401: logged, fbhw cycle still runs");
	reset(); fbcfg_parse("modes=0,1,2,3 period=120 delay=60", &c); fbctl_run(&c);
	CHECK(strstr(logbuf, "iommu") == 0 && probes == 0, "old configs: no iommu line, no iommu sysctl touched");
	reset(); CHECK(fbcfg_parse("modes=0 period=0 delay=5 probe=1 stages=1,2,3 stagegap=20", &c) == 0 && c.nstages == 3 && c.stagegap == 20, "staged config");
	CHECK(fbctl_run(&c) == 0 && stage_n == 3 && stage_k == 3 && slept == 3 * 20 + 5 && probes == 4, "stages run 20 s apart, probe after each, then the delay");
	CHECK(strstr(logbuf, "N2FBHWCTL iommu stage 2/3 value=2 begin t=20") != 0 && strstr(logbuf, "N2FBHWCTL iommu stage 3/3 value=3 alive t=60") != 0, "stage lines");
	CHECK(strstr(logbuf, "N2FBHWCTL iommu stage 3/3 value=3 alive t=60") < strstr(logbuf, "N2FBHWCTL boot mode="), "stages before any mode");
	CHECK(fbcfg_parse("modes=0 stages=4", &c) == 2 && fbcfg_parse("modes=0 stagegap=0", &c) == 2, "bad stage values refused");
	fputs(logbuf, stdout);
	printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
	return fails != 0;
}
