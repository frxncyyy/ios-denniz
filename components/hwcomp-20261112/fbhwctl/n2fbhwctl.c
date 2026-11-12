/* SPDX-License-Identifier: MIT
 * n2fbhwctl: sets the display HW composition sysctls (patch 0396 kern.n2fbhw 0..3, patch 0394 kern.n2fbhw_pool) from a launchd job
 * (com.nord2.fbhwctl, RunAtLoad, root), so a mode can be chosen or compared without a new kernel. There is no shell/ssh on the phone:
 * the whole schedule is in a one-line config file written by the payload (guest-fbhwctl.sh), which is also the gate (absent = exit).
 *   n2fbhwctl --config /private/var/db/nord2-fbhw-mode
 * Config tokens (any order, unknown tokens refused):  modes=0,1,2,3  period=120  delay=60  pool=-1  rounds=1  final=0
 *   modes   up to 8 modes (0..3), applied in order; period = seconds per mode (0 = set the first mode once and exit);
 *   delay   seconds after start before the first mode (boot settle); pool -1 = do not touch kern.n2fbhw_pool, 0/1 = set it at once
 *           (before the delay: 0394 only places surfaces created after the switch, so the earlier the better);
 *   rounds  repetitions of the mode list (1..20); final = mode left set at the end (-1 = leave the last one).
 * Console lines (the orchestrator's capture parses them, hwcomp-20261112/phone/fbhw_verdict.py):
 *   N2FBHWCTL start ... | N2FBHWCTL pool set=V rc=E readback=R | N2FBHWCTL phase I/N mode=M set rc=E readback=R t=S
 *   N2FBHWCTL phase_end I/N mode=M readback=R t=S [FALLBACK]  | N2FBHWCTL done final=M readback=R t=S | N2FBHWCTL error ...
 * readback != mode at phase_end = the kernel left the mode (0396 auto-fallback): logged as FALLBACK, the next phases are still tried.
 */
#ifndef N2FBHWCTL_HOST
typedef unsigned long size_t; typedef long ssize_t;
extern int open(const char *, int, ...);
extern int close(int);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
extern int *__error(void);
extern unsigned int sleep(unsigned int);
extern int snprintf(char *, size_t, const char *, ...);
extern int strcmp(const char *, const char *);
extern size_t strlen(const char *);
extern int sysctlbyname(const char *, void *, size_t *, void *, size_t);
static void out(const char *s)
{
	int fd = open("/dev/console", 1 | 4 | 0x20000);
	if (fd >= 0) { write(fd, s, strlen(s)); write(fd, "\n", 1); close(fd); }
}
static int sc_get(const char *n, int *v) { size_t l = sizeof(*v); return sysctlbyname(n, v, &l, 0, 0) ? *__error() : 0; }
static int sc_set(const char *n, int v) { return sysctlbyname(n, 0, 0, &v, sizeof(v)) ? *__error() : 0; }
static void nap(unsigned s) { while (s) s = sleep(s); }
#endif

#define FB_SYSCTL_MODE "kern.n2fbhw"
#define FB_SYSCTL_POOL "kern.n2fbhw_pool"
struct fbcfg { int modes[8]; int nmodes, period, delay, pool, rounds, final; };

static int num(const char **p, int *v)
{
	int neg = 0, n = 0, any = 0;
	if (**p == '-') { neg = 1; (*p)++; }
	while (**p >= '0' && **p <= '9') { n = n * 10 + (**p - '0'); (*p)++; any = 1; if (n > 100000) return -1; }
	*v = neg ? -n : n; return any ? 0 : -1;
}
static int key(const char **p, const char *k)
{
	const char *s = *p; while (*k && *s == *k) { s++; k++; }
	if (*k || *s != '=') return 0;
	*p = s + 1; return 1;
}
/* 0 = ok, else the 1-based position of the first bad token */
static int fbcfg_parse(const char *t, struct fbcfg *c)
{
	c->nmodes = 0; c->period = 120; c->delay = 60; c->pool = -1; c->rounds = 1; c->final = 0;
	int tok = 0, seen_modes = 0;
	for (const char *p = t; ; ) {
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
		if (!*p) break;
		tok++;
		if (key(&p, "modes")) {
			seen_modes = 1;
			for (;;) { int m; if (c->nmodes == 8 || num(&p, &m) || m < 0 || m > 3) return tok; c->modes[c->nmodes++] = m; if (*p != ',') break; p++; }
		} else if (key(&p, "period")) { if (num(&p, &c->period) || c->period < 0 || c->period > 3600) return tok; }
		else if (key(&p, "delay")) { if (num(&p, &c->delay) || c->delay < 0 || c->delay > 3600) return tok; }
		else if (key(&p, "pool")) { if (num(&p, &c->pool) || c->pool < -1 || c->pool > 1) return tok; }
		else if (key(&p, "rounds")) { if (num(&p, &c->rounds) || c->rounds < 1 || c->rounds > 20) return tok; }
		else if (key(&p, "final")) { if (num(&p, &c->final) || c->final < -1 || c->final > 3) return tok; }
		else return tok;
		if (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return tok;
	}
	return seen_modes && c->nmodes ? 0 : tok + 1;
}

static int fbctl_run(const struct fbcfg *c)
{
	char l[200]; int t = 0, v = -1, e;
	snprintf(l, sizeof l, "N2FBHWCTL start modes=%d period=%d delay=%d pool=%d rounds=%d final=%d", c->nmodes, c->period, c->delay, c->pool, c->rounds, c->final);
	out(l);
	if (c->pool >= 0) {             /* first, before the delay: only surfaces created after this come from the pool (0394) */
		int r = -1; e = sc_set(FB_SYSCTL_POOL, c->pool); sc_get(FB_SYSCTL_POOL, &r);
		snprintf(l, sizeof l, "N2FBHWCTL pool set=%d rc=%d readback=%d t=%d", c->pool, e, r, t); out(l);
	}
	nap((unsigned)c->delay); t += c->delay;
	if ((e = sc_get(FB_SYSCTL_MODE, &v))) {
		snprintf(l, sizeof l, "N2FBHWCTL error %s unreadable errno=%d (kernel without 0396?) t=%d", FB_SYSCTL_MODE, e, t); out(l); return 2;
	}
	snprintf(l, sizeof l, "N2FBHWCTL boot mode=%d t=%d", v, t); out(l);
	int n = c->nmodes * c->rounds, last = -1;
	for (int i = 0; i < n; i++) {
		int m = c->modes[i % c->nmodes]; v = -1;
		e = sc_set(FB_SYSCTL_MODE, m); sc_get(FB_SYSCTL_MODE, &v); last = m;
		snprintf(l, sizeof l, "N2FBHWCTL phase %d/%d mode=%d set rc=%d readback=%d t=%d", i + 1, n, m, e, v, t); out(l);
		if (c->period == 0) { snprintf(l, sizeof l, "N2FBHWCTL done final=%d readback=%d t=%d (fixed)", m, v, t); out(l); return e ? 1 : 0; }
		nap((unsigned)c->period); t += c->period; v = -1; sc_get(FB_SYSCTL_MODE, &v);
		snprintf(l, sizeof l, "N2FBHWCTL phase_end %d/%d mode=%d readback=%d t=%d%s", i + 1, n, m, v, t, v != m ? " FALLBACK" : ""); out(l);
	}
	int f = c->final >= 0 ? c->final : last; v = -1;
	e = sc_set(FB_SYSCTL_MODE, f); sc_get(FB_SYSCTL_MODE, &v);
	snprintf(l, sizeof l, "N2FBHWCTL done final=%d rc=%d readback=%d t=%d", f, e, v, t); out(l);
	return 0;
}

#ifndef N2FBHWCTL_HOST
int main(int argc, char **argv)
{
	const char *cfgp = 0; char buf[256], l[300]; struct fbcfg c;
	for (int i = 1; i + 1 < argc; i += 2) if (!strcmp(argv[i], "--config")) cfgp = argv[i + 1];
	if (!cfgp) { out("N2FBHWCTL error usage: --config FILE"); return 2; }
	int fd = open(cfgp, 0);
	if (fd < 0) return 0;                                   /* gate: no config, nothing to do */
	ssize_t r = read(fd, buf, sizeof buf - 1); close(fd);
	if (r <= 0) { out("N2FBHWCTL error empty config"); return 2; }
	buf[r] = 0;
	int bad = fbcfg_parse(buf, &c);
	if (bad) { snprintf(l, sizeof l, "N2FBHWCTL error config token %d: %s", bad, buf); out(l); return 2; }
	return fbctl_run(&c);
}
#endif
