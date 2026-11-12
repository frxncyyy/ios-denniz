/* host test of n2fbhwctl.c: config parser + schedule against a fake kernel (sysctl), including a 0396 auto-fallback.
 * cc -O1 -Wall -Wextra -fsanitize=address,undefined test_fbhwctl.c -o t && ./t */
#include <stdio.h>
#include <string.h>
#define N2FBHWCTL_HOST 1
static char logbuf[8192]; static int now, mode_k, pool_k = 1, has_mode = 1, fallback_at = -1, slept;
static void out(const char *s) { strcat(logbuf, s); strcat(logbuf, "\n"); }
static int sc_get(const char *n, int *v)
{
	if (!strcmp(n, "kern.n2fbhw")) { if (!has_mode) return 2; if (fallback_at >= 0 && now >= fallback_at) mode_k = 0; *v = mode_k; return 0; }
	if (!strcmp(n, "kern.n2fbhw_pool")) { *v = pool_k; return 0; }
	return 2;
}
static int sc_set(const char *n, int v)
{
	if (!strcmp(n, "kern.n2fbhw")) { if (fallback_at >= 0 && now >= fallback_at && v) return 1; mode_k = v; return 0; }   /* EPERM after fallback */
	if (!strcmp(n, "kern.n2fbhw_pool")) { pool_k = v; return 0; }
	return 2;
}
static void nap(unsigned s) { now += s; slept += s; }
#include "n2fbhwctl.c"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m); fails++; } else printf("ok   %s\n", m); } while (0)
static void reset(void) { logbuf[0] = 0; now = 0; mode_k = 0; pool_k = 1; has_mode = 1; fallback_at = -1; slept = 0; }

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
	fputs(logbuf, stdout);
	printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
	return fails != 0;
}
