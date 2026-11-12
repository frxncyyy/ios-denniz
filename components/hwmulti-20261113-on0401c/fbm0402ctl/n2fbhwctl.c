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
 * 0401 fork (gpu/build/iommu-20261112/fbiommuctl; same binary name/path/guest, backward compatible), optional tokens:
 *   iommu=a,b,..  kern.n2fbiommu per phase (one per mode, 0..3), set BEFORE the phase's kern.n2fbhw; kern.n2fbiommu=0 at the end
 *   probe=1       kern.n2fbiommu_probe=1 (read-only register dump + counters) at start, before anything is armed, and at every phase_end
 *   iotest=1      kern.n2fbiommutest=1 at start: kern.n2fbhwtest reads a scattered (non-pool) copy of the pattern through the IOMMU
 *   hwtest=N      kern.n2fbhwtest=N 10 s into the first phase with iommu >= 2 (self-test photo window)
 * Extra lines: N2FBHWCTL iommu start|probe|phase I/N iommu=K rc=E readback=R|phase_end I/N iommu=K readback=R [AUTO-DISABLE]|hwtest|done
 * 0402b fork (gpu/build/hwmulti-20261113-on0401b/fbm0402ctl; same binary name/path/guest family, backward compatible), optional
 * per-phase vectors (one value per mode, -1 = leave the sysctl alone), set at the start of each phase BEFORE kern.n2fbhw:
 *   scal=a,b,..    kern.n2fbscaling (0/1)       h6=a,b,..   kern.n2fbh6 (0/1)       layers=a,b,..  kern.n2fbhwlayers (2..6)
 *   bbd=a,b,..     1 = after those sysctls, SIGKILL backboardd (launchd restarts it; CoreAnimation reads IOMFBScalingLimits at start)
 *                  and wait bbdwait=S seconds (default 20, counted in the phase) before kern.n2fbhw
 *   reset=1        (default 1 when a vector is given) at the end: kern.n2fbscaling=0, kern.n2fbh6=0, kern.n2fbhwlayers=2 (no restart)
 * The phase line then carries the features: N2FBHWCTL phase I/N mode=M set rc=E readback=R t=S scal=A h6=B layers=C bbd=D
 * (readbacks in N2FBHWCTL feat ...), so the last phase line before a lockup names the feature set that was on.
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
extern int proc_listallpids(void *, int);
extern int proc_name(int, void *, unsigned int);
extern int kill(int, int);
/* SIGKILL every process named nm (launchd restarts a KeepAlive daemon); returns how many, *pid = the last one */
static int restart_proc(const char *nm, int *pid)
{
	static int pids[4096]; char b[64]; int k = 0;
	int n = proc_listallpids(pids, (int)sizeof pids);
	for (int i = 0; i < n && i < 4096; i++) {
		if (pids[i] <= 1) continue;
		b[0] = 0;
		if (proc_name(pids[i], b, sizeof b) > 0 && !strcmp(b, nm) && kill(pids[i], 9) == 0) { k++; *pid = pids[i]; }
	}
	return k;
}
#endif

#define FB_SYSCTL_MODE "kern.n2fbhw"
#define FB_SYSCTL_POOL "kern.n2fbhw_pool"
#define FB_SYSCTL_IOMMU "kern.n2fbiommu"
#define FB_NFEAT 4
static const char *const fb_feat_key[FB_NFEAT] = { "scal", "h6", "layers", "bbd" };
static const char *const fb_feat_sysctl[FB_NFEAT - 1] = { "kern.n2fbscaling", "kern.n2fbh6", "kern.n2fbhwlayers" };
static const int fb_feat_reset[FB_NFEAT - 1] = { 0, 0, 2 };
struct fbcfg { int modes[8]; int nmodes, period, delay, pool, rounds, final; int iommu[8]; int niommu, probe, iotest, hwtest;
               int feat[FB_NFEAT][8]; int nfeat[FB_NFEAT], bbdwait, reset; };

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
	c->niommu = 0; c->probe = 0; c->iotest = 0; c->hwtest = 0; c->bbdwait = 20; c->reset = -1;
	for (int f = 0; f < FB_NFEAT; f++) c->nfeat[f] = 0;
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
		else if (key(&p, "iommu")) {
			for (;;) { int m; if (c->niommu == 8 || num(&p, &m) || m < 0 || m > 3) return tok; c->iommu[c->niommu++] = m; if (*p != ',') break; p++; }
		}
		else if (key(&p, "probe")) { if (num(&p, &c->probe) || c->probe < 0 || c->probe > 1) return tok; }
		else if (key(&p, "iotest")) { if (num(&p, &c->iotest) || c->iotest < 0 || c->iotest > 1) return tok; }
		else if (key(&p, "hwtest")) { if (num(&p, &c->hwtest) || c->hwtest < 0 || c->hwtest > 6) return tok; }
		else if (key(&p, "bbdwait")) { if (num(&p, &c->bbdwait) || c->bbdwait < 0 || c->bbdwait > 600) return tok; }
		else if (key(&p, "reset")) { if (num(&p, &c->reset) || c->reset < 0 || c->reset > 1) return tok; }
		else {
			int f, hit = 0;
			for (f = 0; f < FB_NFEAT && !hit; f++) {
				if (!key(&p, fb_feat_key[f])) continue;
				hit = 1;
				int lo = f == 2 ? 2 : 0, hi = f == 2 ? 6 : 1;
				for (;;) { int m; if (c->nfeat[f] == 8 || num(&p, &m) || (m != -1 && (m < lo || m > hi)) || (f == 3 && m < 0)) return tok; c->feat[f][c->nfeat[f]++] = m; if (*p != ',') break; p++; }
			}
			if (!hit) return tok;
		}
		if (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return tok;
	}
	if (c->niommu && c->niommu != c->nmodes) return tok + 1;      /* one iommu value per mode */
	int anyf = 0;
	for (int f = 0; f < FB_NFEAT; f++) { if (c->nfeat[f] && c->nfeat[f] != c->nmodes) return tok + 1; anyf |= c->nfeat[f] != 0; }
	if (c->reset < 0) c->reset = anyf;
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
	if (c->niommu || c->probe || c->iotest) {
		snprintf(l, sizeof l, "N2FBHWCTL iommu start phases=%d probe=%d iotest=%d hwtest=%d", c->niommu, c->probe, c->iotest, c->hwtest); out(l);
		if (c->probe) { e = sc_set("kern.n2fbiommu_probe", 1); snprintf(l, sizeof l, "N2FBHWCTL iommu probe rc=%d t=%d", e, t); out(l); }
		if (c->iotest) { e = sc_set("kern.n2fbiommutest", 1); snprintf(l, sizeof l, "N2FBHWCTL iommu iotest rc=%d t=%d", e, t); out(l); }
	}
	nap((unsigned)c->delay); t += c->delay;
	if ((e = sc_get(FB_SYSCTL_MODE, &v))) {
		snprintf(l, sizeof l, "N2FBHWCTL error %s unreadable errno=%d (kernel without 0396?) t=%d", FB_SYSCTL_MODE, e, t); out(l); return 2;
	}
	snprintf(l, sizeof l, "N2FBHWCTL boot mode=%d t=%d", v, t); out(l);
	int n = c->nmodes * c->rounds, last = -1, tested = 0;
	for (int i = 0; i < n; i++) {
		int m = c->modes[i % c->nmodes]; v = -1;
		int k = c->niommu ? c->iommu[i % c->nmodes] : -1, kv = -1;
		if (k >= 0) {
			int ke = sc_set(FB_SYSCTL_IOMMU, k); sc_get(FB_SYSCTL_IOMMU, &kv);
			snprintf(l, sizeof l, "N2FBHWCTL iommu phase %d/%d iommu=%d rc=%d readback=%d t=%d", i + 1, n, k, ke, kv, t); out(l);
		}
		int fv[FB_NFEAT], fe[FB_NFEAT - 1], fr[FB_NFEAT - 1], used = 0;
		for (int f = 0; f < FB_NFEAT; f++) { fv[f] = c->nfeat[f] ? c->feat[f][i % c->nmodes] : -1; used |= c->nfeat[f] != 0; }
		for (int f = 0; f < FB_NFEAT - 1; f++) { fe[f] = 0; fr[f] = -1; if (fv[f] >= 0) { fe[f] = sc_set(fb_feat_sysctl[f], fv[f]); } sc_get(fb_feat_sysctl[f], &fr[f]); }
		if (used) {
			snprintf(l, sizeof l, "N2FBHWCTL feat %d/%d scal=%d/%d/%d h6=%d/%d/%d layers=%d/%d/%d (set/rc/readback) t=%d", i + 1, n, fv[0], fe[0], fr[0], fv[1], fe[1], fr[1], fv[2], fe[2], fr[2], t); out(l);
		}
		unsigned spent = 0;
		if (fv[3] == 1) {
			int pid = -1, kn = restart_proc("backboardd", &pid);
			snprintf(l, sizeof l, "N2FBHWCTL bbd restart killed=%d pid=%d (launchd restarts it) wait=%d t=%d", kn, pid, c->bbdwait, t); out(l);
			nap((unsigned)c->bbdwait); t += c->bbdwait; spent = (unsigned)c->bbdwait;
		}
		e = sc_set(FB_SYSCTL_MODE, m); sc_get(FB_SYSCTL_MODE, &v); last = m;
		if (used) { snprintf(l, sizeof l, "N2FBHWCTL phase %d/%d mode=%d set rc=%d readback=%d t=%d scal=%d h6=%d layers=%d bbd=%d", i + 1, n, m, e, v, t, fr[0], fr[1], fr[2], fv[3] == 1); }
		else { snprintf(l, sizeof l, "N2FBHWCTL phase %d/%d mode=%d set rc=%d readback=%d t=%d", i + 1, n, m, e, v, t); }
		out(l);
		if (c->period == 0) { snprintf(l, sizeof l, "N2FBHWCTL done final=%d readback=%d t=%d (fixed)", m, v, t); out(l); return e ? 1 : 0; }
		unsigned rest = (unsigned)c->period > spent ? (unsigned)c->period - spent : 1u;
		if (k >= 2 && c->hwtest && !tested && rest > 10) {
			nap(10); t += 10; rest -= 10; tested = 1;
			int he = sc_set("kern.n2fbhwtest", c->hwtest);
			snprintf(l, sizeof l, "N2FBHWCTL iommu hwtest %d rc=%d t=%d", c->hwtest, he, t); out(l);
		}
		nap(rest); t += (int)rest; v = -1; sc_get(FB_SYSCTL_MODE, &v);
		snprintf(l, sizeof l, "N2FBHWCTL phase_end %d/%d mode=%d readback=%d t=%d%s", i + 1, n, m, v, t, v != m ? " FALLBACK" : ""); out(l);
		if (k >= 0 || c->probe) {
			kv = -1; sc_get(FB_SYSCTL_IOMMU, &kv);
			if (c->probe) { (void)sc_set("kern.n2fbiommu_probe", 1); }
			snprintf(l, sizeof l, "N2FBHWCTL iommu phase_end %d/%d iommu=%d readback=%d t=%d%s", i + 1, n, k, kv, t, (k > 0 && kv != k) ? " AUTO-DISABLE" : ""); out(l);
		}
	}
	if (c->niommu) { int ke = sc_set(FB_SYSCTL_IOMMU, 0); snprintf(l, sizeof l, "N2FBHWCTL iommu done iommu=0 rc=%d t=%d", ke, t); out(l); }
	if (c->reset) {
		int re[FB_NFEAT - 1];
		for (int g = 0; g < FB_NFEAT - 1; g++) { re[g] = sc_set(fb_feat_sysctl[g], fb_feat_reset[g]); }
		snprintf(l, sizeof l, "N2FBHWCTL feat reset scal=0 h6=0 layers=2 rc=%d/%d/%d t=%d", re[0], re[1], re[2], t); out(l);
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
