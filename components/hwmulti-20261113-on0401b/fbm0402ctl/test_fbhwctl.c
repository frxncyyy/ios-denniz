/* host test of n2fbhwctl.c: config parser + schedule against a fake kernel (sysctl), including a 0396 auto-fallback.
 * cc -O1 -Wall -Wextra -fsanitize=address,undefined test_fbhwctl.c -o t && ./t */
#include <stdio.h>
#include <string.h>
#define N2FBHWCTL_HOST 1
static char logbuf[16384]; static int now, mode_k, pool_k = 1, has_mode = 1, fallback_at = -1, slept;
static int scal_k, h6_k, layers_k = 2, kills, kill_t[8], bbd_live = 1;
static int iommu_k, has_iommu = 1, iommu_fault_at = -1, probes, iotest_k, hwtest_k, hwtest_t = -1, mode_set_t = -1, iommu_set_order;
static void out(const char *s) { strcat(logbuf, s); strcat(logbuf, "\n"); }
static int sc_get(const char *n, int *v)
{
	if (!strcmp(n, "kern.n2fbhw")) { if (!has_mode) return 2; if (fallback_at >= 0 && now >= fallback_at) mode_k = 0; *v = mode_k; return 0; }
	if (!strcmp(n, "kern.n2fbhw_pool")) { *v = pool_k; return 0; }
	if (!strcmp(n, "kern.n2fbscaling")) { *v = scal_k; return 0; }
	if (!strcmp(n, "kern.n2fbh6")) { *v = h6_k; return 0; }
	if (!strcmp(n, "kern.n2fbhwlayers")) { *v = layers_k; return 0; }
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
	if (!strcmp(n, "kern.n2fbscaling")) { scal_k = v; return 0; }
	if (!strcmp(n, "kern.n2fbh6")) { h6_k = v; return 0; }
	if (!strcmp(n, "kern.n2fbhwlayers")) { if (v < 2 || v > 6) return 22; layers_k = v; return 0; }
	return 2;
}
static void nap(unsigned s) { now += s; slept += s; }
static int restart_proc(const char *n, int *p) { if (strcmp(n, "backboardd")) return 0; if (kills < 8) kill_t[kills] = now; kills++; *p = 77; return bbd_live; }
#include "n2fbhwctl.c"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m); fails++; } else printf("ok   %s\n", m); } while (0)
static void reset(void) { logbuf[0] = 0; now = 0; mode_k = 0; pool_k = 1; has_mode = 1; fallback_at = -1; slept = 0;
	iommu_k = 0; has_iommu = 1; iommu_fault_at = -1; probes = 0; iotest_k = 0; hwtest_k = 0; hwtest_t = -1; mode_set_t = -1; iommu_set_order = 0; scal_k = 0; h6_k = 0; layers_k = 2; kills = 0; bbd_live = 1; }

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
	/* 0402b fork: the payload-fbm0402cycle config */
	const char *cy = "modes=3,3,3,3,3 scal=0,1,0,0,1 h6=0,0,1,0,1 layers=2,2,2,4,4 bbd=0,1,1,0,1 period=180 delay=90 pool=1 final=0";
	CHECK(fbcfg_parse(cy, &c) == 0 && c.nfeat[0] == 5 && c.feat[2][3] == 4 && c.feat[3][2] == 1 && c.reset == 1 && c.bbdwait == 20, "0402b cycle config");
	CHECK(fbcfg_parse("modes=3,3 scal=1", &c) != 0, "feature vector count must match modes");
	CHECK(fbcfg_parse("modes=3 layers=7", &c) == 2 && fbcfg_parse("modes=3 layers=1", &c) == 2 && fbcfg_parse("modes=3 h6=2", &c) == 2, "feature ranges");
	CHECK(fbcfg_parse("modes=3 bbd=-1", &c) == 2, "bbd must be 0/1");
	CHECK(fbcfg_parse("modes=3,3 layers=-1,4 bbdwait=5", &c) == 0 && c.feat[2][0] == -1 && c.bbdwait == 5, "-1 = leave, bbdwait");
	reset(); fbcfg_parse(cy, &c);
	CHECK(fbctl_run(&c) == 0, "0402b cycle runs");
	CHECK(slept == 90 + 5 * 180, "0402b cycle: backboardd waits count inside the phase (total = delay + 5 periods)");
	CHECK(kills == 3 && kill_t[0] == 270 && kill_t[1] == 450 && kill_t[2] == 810, "backboardd restarted at the start of phases B, C, E");
	CHECK(strstr(logbuf, "N2FBHWCTL phase 1/5 mode=3 set rc=0 readback=3 t=90 scal=0 h6=0 layers=2 bbd=0") != 0, "phase A line = all defaults");
	CHECK(strstr(logbuf, "N2FBHWCTL feat 2/5 scal=1/0/1 h6=0/0/0 layers=2/0/2") != 0 && strstr(logbuf, "N2FBHWCTL bbd restart killed=1 pid=77 (launchd restarts it) wait=20 t=270") != 0, "phase B: scaling + restart");
	CHECK(strstr(logbuf, "N2FBHWCTL phase 2/5 mode=3 set rc=0 readback=3 t=290 scal=1 h6=0 layers=2 bbd=1") != 0, "phase B line after the restart wait");
	CHECK(strstr(logbuf, "N2FBHWCTL phase 3/5 mode=3 set rc=0 readback=3 t=470 scal=0 h6=1 layers=2 bbd=1") != 0, "phase C: H6 only (scaling removed + restart)");
	CHECK(strstr(logbuf, "N2FBHWCTL phase 4/5 mode=3 set rc=0 readback=3 t=630 scal=0 h6=0 layers=4 bbd=0") != 0, "phase D: 4L only");
	CHECK(strstr(logbuf, "N2FBHWCTL phase 5/5 mode=3 set rc=0 readback=3 t=830 scal=1 h6=1 layers=4 bbd=1") != 0, "phase E: all");
	CHECK(strstr(logbuf, "N2FBHWCTL feat 2/5") < strstr(logbuf, "N2FBHWCTL bbd restart") && strstr(logbuf, "N2FBHWCTL bbd restart") < strstr(logbuf, "N2FBHWCTL phase 2/5"), "order: sysctls, restart, mode");
	CHECK(strstr(logbuf, "N2FBHWCTL feat reset scal=0 h6=0 layers=2 rc=0/0/0 t=990") != 0 && scal_k == 0 && h6_k == 0 && layers_k == 2 && mode_k == 0, "defaults back at the end");
	CHECK(strstr(logbuf, "N2FBHWCTL done final=0 rc=0 readback=0 t=990") != 0, "done line for fbhw_scenario.py --cycle");
	reset(); bbd_live = 0; fbcfg_parse("modes=3 bbd=1 period=60 delay=0", &c); fbctl_run(&c);
	CHECK(strstr(logbuf, "N2FBHWCTL bbd restart killed=0") != 0, "no backboardd found: logged, schedule goes on");
	reset(); fbcfg_parse("modes=0,1,2,3 period=120 delay=60", &c); fbctl_run(&c);
	CHECK(strstr(logbuf, "feat") == 0 && kills == 0 && layers_k == 2, "old configs: no feature sysctl touched, no restart, no reset");
	fputs(logbuf, stdout);
	printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
	return fails != 0;
}
