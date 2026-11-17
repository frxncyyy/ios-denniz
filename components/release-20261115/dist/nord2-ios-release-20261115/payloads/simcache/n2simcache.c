// SPDX-License-Identifier: MIT
// n2simcache (sim-20261115, payload simcache1): with NO SIM in the phone, iOS Settings > General > About still shows the last SIM's
// carrier ("Very 54.0"). Cause [V, unified log + rootfs mirror]: CommCenter keeps the last SIM's carrier bundle links
// /var/mobile/Library/Carrier Bundles/BundleLinks/Carrier1Bundle.bundle (-> .../22288_GID1-0000000099) and Operator1Bundle.bundle
// (-> .../22288) plus the matching key com.apple.carrier_1 in com.apple.commcenter (user _wireless), and reloads them at every launch;
// with no SIM nothing ever replaces them. The kernel cannot fix it: CommCenter reads them ~1 s after launch, long before the modem
// knows whether a card is there.
// This one-shot root job (launchd com.nord2.simcache, RunAtLoad, LaunchOnlyOnce) waits for kern.n2sim_state (kernel piece 0710) to
// say "no SIM, settled" (3 reads in a row), then: removes those two links (only links into /System/Library/Carrier Bundles/),
// clears com.apple.carrier_1 through CFPreferences, and restarts CommCenter (SIGTERM; launchd relaunches it) so the running
// CommCenter forgets the bundle; then checks the baseband link came back (kern.n2bb_link, piece 0713: SAHARA done + QMUX clients).
// A failed link check writes the 'norestart' switch so later boots never restart CommCenter again.
// Idempotent: nothing to remove -> nothing done (no restart). With a SIM: nothing done.
// Switches in /private/var/db/nord2-simcache: disabled (do nothing), norestart (remove/clear only; the About row is clean from the next
// boot), keepkey (links only), dryrun (log what would be done). Log: /private/var/db/nord2-simcache/log and kern.n2sim_helper
// step codes (kernel prints "NORD2 SIM: helper step 0x5cSSDDDD").
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DIR_ "/private/var/db/nord2-simcache"
#define LINKS "/private/var/mobile/Library/Carrier Bundles/BundleLinks/"
static const char *const kLinks[] = { "Carrier1Bundle.bundle", "Operator1Bundle.bundle" };
static FILE *logf_;

static void lg(const char *fmt, ...)
{
	char ts[32];
	time_t t = time(NULL);
	strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", localtime(&t));
	va_list ap;
	if (logf_) {
		fprintf(logf_, "%s ", ts);
		va_start(ap, fmt); vfprintf(logf_, fmt, ap); va_end(ap);
		fputc('\n', logf_); fflush(logf_);
	}
	fprintf(stderr, "n2simcache: ");
	va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr);
}
static void step(unsigned s, unsigned d)
{
	unsigned v = 0x5c000000u | (s & 0xffu) << 16 | (d & 0xffffu);
	(void)sysctlbyname("kern.n2sim_helper", NULL, NULL, &v, sizeof v);
}
static int rd(const char *name, unsigned *v)
{
	size_t n = sizeof *v;
	return sysctlbyname(name, v, &n, NULL, 0);
}
static int sw(const char *name)
{
	char p[128];
	snprintf(p, sizeof p, DIR_ "/%s", name);
	return access(p, F_OK) == 0;
}
static pid_t find_pid(const char *comm)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t n = 0;
	if (sysctl(mib, 4, NULL, &n, NULL, 0) != 0 || !n) return -1;
	n += 64 * sizeof(struct kinfo_proc);
	struct kinfo_proc *kp = malloc(n);
	if (!kp) return -1;
	pid_t pid = -1;
	if (sysctl(mib, 4, kp, &n, NULL, 0) == 0) {
		for (size_t i = 0; i < n / sizeof *kp; i++)
			if (!strncmp(kp[i].kp_proc.p_comm, comm, MAXCOMLEN) && kp[i].kp_proc.p_stat != SZOMB) { pid = kp[i].kp_proc.p_pid; break; }
	}
	free(kp);
	return pid;
}
// 1: a link of ours points into the system carrier bundles at something else than the Default bundle
static int stale_links(char tgt[2][256])
{
	int any = 0;
	for (int i = 0; i < 2; i++) {
		char p[256];
		snprintf(p, sizeof p, LINKS "%s", kLinks[i]);
		ssize_t k = readlink(p, tgt[i], 255);
		tgt[i][k > 0 ? k : 0] = 0;
		if (k > 0 && !strncmp(tgt[i], "/System/Library/Carrier Bundles/", 32) && !strstr(tgt[i], "Default.bundle")) any = 1;
		else tgt[i][0] = 0;
	}
	return any;
}
static int clear_key(int dry)
{
	CFStringRef key = CFSTR("com.apple.carrier_1"), app = CFSTR("com.apple.commcenter"), user = CFSTR("_wireless");
	CFPropertyListRef v = CFPreferencesCopyValue(key, app, user, kCFPreferencesAnyHost);
	if (!v) { lg("key com.apple.carrier_1: absent"); return 0; }
	CFRelease(v);
	if (dry) { lg("dryrun: would clear com.apple.carrier_1"); return 0; }
	CFPreferencesSetValue(key, NULL, app, user, kCFPreferencesAnyHost);
	Boolean ok = CFPreferencesSynchronize(app, user, kCFPreferencesAnyHost);
	v = CFPreferencesCopyValue(key, app, user, kCFPreferencesAnyHost);
	if (v) CFRelease(v);
	lg("key com.apple.carrier_1: cleared (synchronize %s, verify %s)", ok ? "ok" : "FAILED", v ? "STILL THERE" : "gone");
	return v ? -1 : 1;
}

int main(void)
{
	mkdir(DIR_, 0755);
	logf_ = fopen(DIR_ "/log", "a");
	struct stat st;
	if (logf_ && fstat(fileno(logf_), &st) == 0 && st.st_size > 256 * 1024) { fclose(logf_); logf_ = fopen(DIR_ "/log", "w"); }
	lg("start (simcache1)");
	if (sw("disabled")) { lg("switch 'disabled': nothing done"); step(0x0c, 0); return 0; }
	unsigned s = 0, link0 = 0;
	if (rd("kern.n2sim_state", &s) != 0) { lg("no kern.n2sim_state (kernel without 0710): nothing done"); step(0x0d, 0); return 0; }
	step(0x01, s & 0xffff);
	int absent_in_row = 0;
	for (int i = 0; i < 150; i++) {                         // up to 300 s after launch
		if (rd("kern.n2sim_state", &s) != 0) break;
		const unsigned raw = s & 0xff, settled = (s >> 8) & 0xff;
		if (settled == 1 && raw != 4) { lg("SIM present (state 0x%08x): nothing to do", s); step(0x03, s & 0xffff); return 0; }
		absent_in_row = settled == 1 && raw == 4 ? absent_in_row + 1 : 0;
		if (absent_in_row >= 3) break;
		sleep(2);
	}
	if (absent_in_row < 3) { lg("SIM state never settled to 'absent' (last 0x%08x): nothing done", s); step(0x0e, s & 0xffff); return 0; }
	lg("no SIM, settled (state 0x%08x)", s); step(0x02, s & 0xffff);
	char tgt[2][256];
	if (!stale_links(tgt)) { lg("carrier bundle links already clean: nothing to do"); step(0x04, 0); return 0; }
	const int dry = sw("dryrun");
	int removed = 0;
	for (int i = 0; i < 2; i++) {
		if (!tgt[i][0]) continue;
		char p[256];
		snprintf(p, sizeof p, LINKS "%s", kLinks[i]);
		if (dry) { lg("dryrun: would remove %s -> %s", kLinks[i], tgt[i]); continue; }
		if (unlink(p) == 0) { removed++; lg("removed %s (was -> %s)", kLinks[i], tgt[i]); }
		else lg("could not remove %s: %s", kLinks[i], strerror(errno));
	}
	step(0x05, (unsigned)removed);
	int key = sw("keepkey") ? 0 : clear_key(dry);
	step(0x06, key < 0 ? 0xffff : (unsigned)key);
	if (dry || !removed) { lg("done (%s)", dry ? "dryrun" : "nothing removed"); step(0x0f, 0); return 0; }
	if (sw("norestart")) { lg("switch 'norestart': CommCenter keeps the old carrier until the next boot"); step(0x0f, 1); return 0; }
	pid_t old = find_pid("CommCenter");
	if (old <= 0) { lg("CommCenter not running: nothing to restart"); step(0x0f, 2); return 0; }
	(void)rd("kern.n2bb_link", &link0);
	lg("restarting CommCenter (pid %d), baseband link before 0x%08x", (int)old, link0);
	if (kill(old, SIGTERM) != 0) { lg("kill failed: %s", strerror(errno)); step(0x07, 0xffff); return 0; }
	step(0x07, (unsigned)old & 0xffff);
	pid_t nw = -1;
	for (int i = 0; i < 60 && (nw = find_pid("CommCenter")) <= 0; i++) sleep(1);
	for (int i = 0; i < 30 && nw == old; i++) { sleep(1); nw = find_pid("CommCenter"); }   // SIGTERM still pending
	if (nw <= 0 || nw == old) { lg("CommCenter did not come back within 90 s (pid %d)", (int)nw); step(0x08, 0xffff); }
	else { lg("CommCenter relaunched (pid %d)", (int)nw); step(0x08, (unsigned)nw & 0xffff); }
	unsigned link = 0;
	int ok = 0;
	for (int i = 0; i < 60 && !ok; i++) {                   // 120 s: SAHARA done (6) and QMUX clients back
		sleep(2);
		if (rd("kern.n2bb_link", &link) == 0 && (link & 1) && ((link >> 16) & 0xff) == 6 && ((link >> 8) & 0xff) >= 1 &&
		    (i >= 5 || ((link >> 24) & 0xff) != ((link0 >> 24) & 0xff))) ok = 1;
	}
	if (ok) { lg("baseband link back (0x%08x)", link); step(0x09, (link >> 8) & 0xffff); }
	else {
		lg("baseband link NOT back after 120 s (0x%08x): writing 'norestart' so later boots never restart CommCenter", link);
		int fd = open(DIR_ "/norestart", O_CREAT | O_WRONLY, 0644);
		if (fd >= 0) { (void)write(fd, "auto\n", 5); close(fd); }
		step(0x0a, (link >> 8) & 0xffff);
	}
	if (stale_links(tgt)) { lg("WARNING: CommCenter recreated a carrier link (%s / %s)", tgt[0], tgt[1]); step(0x0b, 0); }
	lg("done"); step(0x0f, 3);
	return 0;
}
