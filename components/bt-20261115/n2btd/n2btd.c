/* SPDX-License-Identifier: MIT
 * n2btd -- piece 0723 (bt-20261115): the user half of Bluetooth on the Nord 2 port. Runs as the launchd job
 * com.apple.BlueTool (payload bt1 replaces that job's program; the original /usr/sbin/BlueTool stays on disk).
 *
 *  1. gate: /private/var/db/nord2-bt/enable must exist, else exit 0 (kill switch: delete the file, reboot).
 *  2. address: /private/var/db/nord2-bt/bdaddr ("aa:bb:cc:dd:ee:ff"); generated with arc4random on the first run on a
 *     phone (not a valid unicast-able address or an inquiry LAP 0x9e8b00..3f is never produced) and kept -> each Nord 2
 *     gets its own stable address without any per-phone calibration. Written to kern.n2bt_addr.
 *  3. kern.n2bt_up = 1 once per boot (the kernel thread of piece 0720 brings the controller up and, with 0721/0722,
 *     publishes it); the state is followed for 180 s and logged ("NORD2 BT U:" lines in the kernel log via kern.n2bt_note).
 *     /private/var/db/nord2-bt/stage = "probe" sets kern.n2bt_publish=0 first (controller + HCI probe only).
 *  4. serves com.apple.BlueTool for bluetoothd (verified on the host in bluetoothd 16.7: message {kMsgId, kMsgArgs},
 *     "RunBuiltinScript" {script, btaddress?} -> reply {result int64 0, firmware string, manufacturer int64};
 *     result 0 = success; "RunCommand" {command} -> {result 0, output ""}). Apple's BlueTool would try to load Broadcom
 *     .hcd firmware over a UART and time out; here the controller is already up (kernel).
 */
#include "n2xpc.h"
#include <dispatch/dispatch.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <dlfcn.h>
#include <ctype.h>
#include <CoreFoundation/CoreFoundation.h>

#define DIR_DB    "/private/var/db/nord2-bt"
#define GATE      DIR_DB "/enable"
#define ADDRF     DIR_DB "/bdaddr"
#define STAGEF    DIR_DB "/stage"
#define LOGF      DIR_DB "/n2btd.log"
#define FWDIR     "/usr/share/firmware/nord2-bt/"

static void
note(const char *fmt, ...)
{
	char b[200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	(void)sysctlbyname("kern.n2bt_note", NULL, NULL, b, strlen(b) + 1);
	FILE *f = fopen(LOGF, "a");
	if (f) {
		time_t t = time(NULL);
		struct stat st;
		if (fstat(fileno(f), &st) == 0 && st.st_size > 256 * 1024) {
			fclose(f);
			rename(LOGF, LOGF ".1");
			f = fopen(LOGF, "a");
		}
		if (f) { fprintf(f, "%ld %s\n", (long)t, b); fclose(f); }
	}
}

static int
parse_addr(const char *s, unsigned char a[6])
{
	unsigned v[6];
	if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return -1;
	for (int i = 0; i < 6; i++) a[i] = (unsigned char)v[i];
	return 0;
}

static int
addr_ok(const unsigned char a[6])
{
	unsigned char o = 0, x = 0xff;
	for (int i = 0; i < 6; i++) { o |= a[i]; x &= a[i]; }
	if (o == 0 || x == 0xff) return 0;
	if (a[3] == 0x9e && a[4] == 0x8b && a[5] < 0x40) return 0;   /* reserved LAPs (inquiry access codes) */
	return 1;
}

static int
load_or_make_addr(char out[18])
{
	unsigned char a[6];
	char buf[64] = "";
	FILE *f = fopen(ADDRF, "r");
	if (f) {
		if (fgets(buf, sizeof(buf), f) && parse_addr(buf, a) == 0 && addr_ok(a)) {
			fclose(f);
			snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
			return 0;
		}
		fclose(f);
		note("bdaddr file unreadable or invalid: a new address is made");
	}
	do {
		arc4random_buf(a, sizeof(a));
		a[0] &= 0xfc;            /* no multicast / "locally administered" look-alike bits in the first octet */
	} while (!addr_ok(a));
	snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
	char tmp[] = DIR_DB "/bdaddr.tmp";
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return -1;
	dprintf(fd, "%s\n", out);
	fsync(fd);
	close(fd);
	if (rename(tmp, ADDRF) != 0) return -1;
	note("first run on this phone: Bluetooth address %s generated and stored in " ADDRF, out);
	return 0;
}

static void
read_state(char *s, size_t n)
{
	size_t len = n;
	s[0] = 0;
	if (sysctlbyname("kern.n2bt_state", s, &len, NULL, 0) != 0) snprintf(s, n, "no-kernel-support(%d)", errno);
}

/* ---- bt2 per-boot guard (after the bt18b reset): /private/var/db/nord2-bt/guard = "inflight <boottime> <last state>" is written
 * (fsync + F_FULLFSYNC) before kern.n2bt_up and removed only after a clean outcome (bt3: "<state>:settled" = stopped/failed and
 * then 600 s of kernel monitoring without reset; refused at once; up/probe-ok after 120 s of running). A guard file from ANOTHER boot = that boot reset during or soon after the bring-up -> Bluetooth disables itself
 * (enable -> enable.off-reset) and this daemon exits: no reset loop. Re-arm = reinstall payload bt1 (or rename the file back). ---- */
static void log_gestalt(void);
#define GUARDF  DIR_DB "/guard"
#define REARMF  DIR_DB "/rearm"

static long
boottime_sec(void)
{
	struct timeval tv = { 0, 0 };
	size_t len = sizeof(tv);
	if (sysctlbyname("kern.boottime", &tv, &len, NULL, 0) != 0) return -1;
	return (long)tv.tv_sec;
}

static long
uptime_sec(void)
{
	long b = boottime_sec();
	return b > 0 ? (long)time(NULL) - b : 0;
}

static void
sync_dir(void)
{
	int d = open(DIR_DB, O_RDONLY);
	if (d >= 0) { fsync(d); close(d); }
}

static void
guard_write(const char *state)
{
	char tmp[] = DIR_DB "/guard.tmp";
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { note("guard file not writable (errno %d)", errno); return; }
	dprintf(fd, "inflight %ld %s\n", boottime_sec(), state);
	fsync(fd);
	fcntl(fd, F_FULLFSYNC);
	close(fd);
	rename(tmp, GUARDF);
	sync_dir();
}

static void
guard_clear(const char *why)
{
	if (unlink(GUARDF) == 0) { sync_dir(); note("guard cleared (%s)", why); }
}

/* true = a previous boot died with the guard set: disable and stop */
static int
guard_tripped(void)
{
	FILE *f = fopen(GUARDF, "r");
	char st[64] = "";
	long bt = 0;
	if (f == NULL) return 0;
	int n = fscanf(f, "inflight %ld %63s", &bt, st);
	fclose(f);
	if (n >= 1 && bt == boottime_sec()) return 0;     /* this boot's own file (daemon restarted): not a reset */
	note("GUARD: the previous boot (boottime %ld) reset with Bluetooth in state '%s': Bluetooth disabled (enable -> enable.off-reset); reinstall payload bt1 to retry", bt, st);
	rename(GATE, GATE ".off-reset");
	unlink(GUARDF);
	sync_dir();
	return 1;
}

/* bt4: if the kernel had to generate the address (no file / refused), keep that one for the next boots */
static void
adopt_kernel_addr(void)
{
	char k[64] = "", f[64] = "";
	size_t len = sizeof(k) - 1;
	unsigned char a[6];
	FILE *fp;
	if (sysctlbyname("kern.n2bt_addr", k, &len, NULL, 0) != 0 || parse_addr(k, a) != 0 || !addr_ok(a)) return;
	if ((fp = fopen(ADDRF, "r")) != NULL) { if (!fgets(f, sizeof(f), fp)) f[0] = 0; fclose(fp); }
	if (strncmp(f, k, 17) == 0) return;
	char tmp[] = DIR_DB "/bdaddr.tmp";
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	dprintf(fd, "%.17s\n", k);
	fsync(fd);
	close(fd);
	if (rename(tmp, ADDRF) == 0) { sync_dir(); note("kernel-generated address %.17s stored in " ADDRF, k); }
}

static int
wifi_running(void)
{
	struct ifaddrs *ifa = NULL, *i;
	int up = 0;
	if (getifaddrs(&ifa) != 0) return 0;
	for (i = ifa; i; i = i->ifa_next)
		if (i->ifa_name && strcmp(i->ifa_name, "en0") == 0 && (i->ifa_flags & IFF_UP) && (i->ifa_flags & IFF_RUNNING)) up = 1;
	freeifaddrs(ifa);
	return up;
}

static void
bring_up(void)
{
	char addr[18], st[100], last[100] = "";
	int one = 1, maxstep = 10;
	FILE *f;

	mkdir(DIR_DB, 0755);
	if (load_or_make_addr(addr) == 0) {
		note("address %s -> kern.n2bt_addr", addr);
		/* bt4: 17 bytes, no NUL (the bt3 kernel's 18-byte buffer refused 18 bytes with EINVAL; 17 fits old and new kernels) */
		if (sysctlbyname("kern.n2bt_addr", NULL, NULL, addr, strlen(addr)) != 0) note("kern.n2bt_addr: errno %d", errno);
		else note("kern.n2bt_addr accepted");
	} else {
		note("no address could be stored (errno %d): the controller's own is used", errno);
	}
	if ((f = fopen(STAGEF, "r")) != NULL) {
		char b[32] = "";
		if (fgets(b, sizeof(b), f)) maxstep = strncmp(b, "probe", 5) == 0 ? 9 : atoi(b);
		fclose(f);
		if (maxstep < 1 || maxstep > 10) maxstep = 1;      /* unreadable stage file: the safest stage */
	}
	note("stage: stop after S%d%s", maxstep, maxstep == 1 ? " (AP-side reads only)" : maxstep == 9 ? " (controller up, not shown to iOS)" : maxstep == 10 ? " (full)" : "");
	if (sysctlbyname("kern.n2bt_maxstep", NULL, NULL, &maxstep, sizeof(maxstep)) != 0) {
		note("kern.n2bt_maxstep: errno %d (kernel without bt2 pieces?): no bring-up", errno);
		return;
	}
	if (access(REARMF, F_OK) == 0) {
		sysctlbyname("kern.n2bt_rearm", NULL, NULL, &one, sizeof(one));
		unlink(REARMF);
		note("rearm file: kernel guard ignored once");
	}
	/* bt22: the kernel (q10jfinal40bt+) starts the bring-up itself at boot: no waiting when it already did */
	read_state(st, sizeof(st));
	if (strcmp(st, "idle") != 0 && strncmp(st, "failed", 6) != 0) {
		note("kernel already started the bring-up (state '%s'): following it", st);
		goto follow;
	}
	/* conninfra is powered by our Wi-Fi bring-up: never before 45 s of uptime, then until en0 runs (Wi-Fi up) or 120 s */
	for (int said = 0;; said++) {
		long up = uptime_sec();
		int w = wifi_running();
		if ((up >= 45 && w) || up >= 120) { note("starting at uptime %ld s (en0 %s)", up, w ? "running" : "not running"); break; }
		if (said == 0) note("waiting: uptime %ld s, en0 %s", up, w ? "running" : "not running");
		sleep(2);
	}
	read_state(st, sizeof(st));
	if (strcmp(st, "idle") != 0 && strncmp(st, "failed", 6) != 0) {
		note("kernel state already '%s': no new bring-up", st);
		if (strcmp(st, "up") == 0 || strcmp(st, "probe-ok") == 0) { sleep(120); guard_clear("already running in this boot"); }
		return;
	}
	if (maxstep >= 4 && (access(FWDIR "soc3_0_ram_mcu_1a_1_hdr.bin", R_OK) != 0 || access(FWDIR "soc3_0_ram_bt_1a_1_hdr.bin", R_OK) != 0)) {
		note("firmware missing in " FWDIR ": no bring-up");
		return;
	}
	guard_write("requested");
	note("guard written; requesting the bring-up (kern.n2bt_up=1)");
	if (sysctlbyname("kern.n2bt_up", NULL, NULL, &one, sizeof(one)) != 0) {
		note("kern.n2bt_up refused: errno %d", errno);
		guard_clear("refused");
		return;
	}
follow:;
	int running_since = -1;
	for (int i = 0;; i++) {
		read_state(st, sizeof(st));
		if (strcmp(st, last) != 0) {
			note("kernel state: %s", st);
			strlcpy(last, st, sizeof(last));
			guard_write(st);
		}
		/* bt3: a stop or a failure is clean only after the kernel's post-stop monitor ("...:settled", kern.n2bt_monitor_s):
		 * the bt2 phone runs reset 3-6 min AFTER a stop */
		if (strstr(st, ":settled") != NULL || strncmp(st, "refused", 7) == 0) { guard_clear(st); break; }
		if (strcmp(st, "up") == 0 || strcmp(st, "probe-ok") == 0) {
			if (running_since < 0) { running_since = i; adopt_kernel_addr(); log_gestalt(); }
			if (i - running_since >= 120) { guard_clear("120 s running"); break; }
		}
		if (i > 1500) { note("no settled outcome in 1500 s (state %s): guard kept", st); break; }
		sleep(1);
	}
}

/* ---- bt5 diagnostics ----
 * (1) bluetoothd's own os_log: payload bt1 gives bluetoothd's launchd job OS_ACTIVITY_DT_MODE=YES (os_log mirrored to stderr) and
 *     StandardErrorPath/StandardOutPath = /private/var/db/nord2-bt/bluetoothd.log; this thread forwards the lines that matter
 *     (errors, failures, restarts, init steps, power/state) to the kernel log as "NORD2 BT U: btd: ..." (at most 350 per boot)
 *     and keeps the file under 4 MiB.
 * (2) MobileGestalt: the answers Settings/BluetoothManager use for the Bluetooth row, logged once after the bring-up. */
#define BTDLOG DIR_DB "/bluetoothd.log"
static int
interesting(const char *l)
{
	static const char *const k[] = { "fail", "error", "restart", "fatal", "terminat", "vsc", "initcmd", "couldn", "unsupported",
		"not supported", "timeout", "timed out", "power", "state", "chipset", "transport", "hci", "stall", "abort", "exit", "crash",
		/* bt15: the profile / audio path */
		"a2dp", "avdtp", "avrcp", "hfp", "audio", "route", "skywalk", "profile", "connect", "sdp", "sbc", "aac", "codec", "stream",
		/* bt18: codec negotiation / encoder */
		"selected", "configur", "encoder", "converter", "bitpool", "bitrate", NULL };
	char low[256];
	size_t i;
	for (i = 0; l[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)l[i]);
	low[i] = 0;
	for (int j = 0; k[j]; j++) if (strstr(low, k[j])) return 1;
	return 0;
}

static void
btd_log_thread(void)
{
	off_t pos = 0;
	unsigned sent = 0;
	char buf[4096], line[512];
	size_t ll = 0;
	for (;;) {
		int fd = open(BTDLOG, O_RDONLY);
		if (fd >= 0) {
			struct stat st;
			if (fstat(fd, &st) == 0) {
				if (st.st_size < pos) pos = 0;                     /* truncated */
				if (st.st_size > 4 * 1024 * 1024 && pos >= st.st_size) {
					close(fd);
					truncate(BTDLOG, 0);                           /* launchd opens it O_APPEND */
					pos = 0;
					continue;
				}
				lseek(fd, pos, SEEK_SET);
				ssize_t n;
				while ((n = read(fd, buf, sizeof(buf))) > 0) {
					pos += n;
					for (ssize_t i = 0; i < n; i++) {
						if (buf[i] == '\n' || ll == sizeof(line) - 1) {
							line[ll] = 0;
							if (ll > 0 && sent < 3000 && interesting(line)) { note("btd: %.180s", line); sent++; }
							ll = 0;
						} else {
							line[ll++] = buf[i];
						}
					}
				}
			}
			close(fd);
		}
		sleep(1);
	}
}

/* bt6: MobileGestalt caches "bluetooth" (false since the first boot, before Bluetooth existed). The kernel now sets
 * IODeviceTree:/chosen mac-address-bluetooth0 at boot from our address file; if the answer is still false after the bring-up the
 * cache is moved aside so that MobileGestalt rebuilds it on the next boot (the port's established method, apps-20261104/guest-mg.sh).
 * At most twice (counter file). */
#define MGCACHE "/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist"
#define MGCOUNT DIR_DB "/mg-rebuilds"
static void
mg_rebuild_if_false(int bt_true)
{
	int count = 0;
	FILE *f = fopen(MGCOUNT, "r");
	if (f) { if (fscanf(f, "%d", &count) != 1) count = 0; fclose(f); }
	if (bt_true) { note("gestalt: bluetooth is true: Settings shows the Bluetooth row"); return; }
	if (count >= 2) { note("gestalt: bluetooth still false after %d cache rebuilds: not touching the cache again", count); return; }
	char bak[] = DIR_DB "/MobileGestalt.plist.n2btd-rebuild";
	if (rename(MGCACHE, bak) == 0) {
		f = fopen(MGCOUNT, "w");
		if (f) { fprintf(f, "%d\n", count + 1); fclose(f); }
		sync_dir();
		note("gestalt: bluetooth false although /chosen has the address: MobileGestalt cache moved aside (rebuild %d of 2 on the next boot)", count + 1);
	} else {
		note("gestalt: bluetooth false; cache not moved (errno %d)", errno);
	}
}

static void
log_gestalt(void)
{
	void *h = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_NOW);
	if (h == NULL) { note("gestalt: libMobileGestalt not loaded"); return; }
	Boolean (*get_bool)(CFStringRef) = (Boolean (*)(CFStringRef))dlsym(h, "MGGetBoolAnswer");
	CFTypeRef (*copy)(CFStringRef) = (CFTypeRef (*)(CFStringRef))dlsym(h, "MGCopyAnswer");
	int bt_answer = 0;
	static const char *const keys[] = { "bluetooth", "bluetooth-le", "BluetoothCapability", "BluetoothLowEnergyCapability", "DeviceSupportsBluetooth", NULL };
	for (int i = 0; keys[i] && get_bool && copy; i++) {
		CFStringRef k = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
		CFTypeRef v = copy(k);
		char d[64] = "null";
		if (v != NULL) {
			if (CFGetTypeID(v) == CFBooleanGetTypeID()) snprintf(d, sizeof(d), "%s", CFBooleanGetValue((CFBooleanRef)v) ? "true" : "false");
			else if (CFGetTypeID(v) == CFNumberGetTypeID()) { long long x = 0; CFNumberGetValue((CFNumberRef)v, kCFNumberLongLongType, &x); snprintf(d, sizeof(d), "%lld", x); }
			else snprintf(d, sizeof(d), "type %lu", (unsigned long)CFGetTypeID(v));
			CFRelease(v);
		}
		int b = get_bool(k) ? 1 : 0;
		note("gestalt: %s = %s (MGGetBoolAnswer %d)", keys[i], d, b);
		if (i == 0) bt_answer = b;
		CFRelease(k);
	}
	mg_rebuild_if_false(bt_answer);
}

static void
handle(xpc_connection_t peer, xpc_object_t msg)
{
	const char *id = xpc_dictionary_get_string(msg, "kMsgId");
	xpc_object_t args = xpc_dictionary_get_value(msg, "kMsgArgs");
	xpc_object_t reply = xpc_dictionary_create_reply(msg);
	static unsigned said;

	if (reply == NULL) {
		if (said++ < 20) note("BlueTool message '%s' without reply port", id ? id : "?");
		return;
	}
	xpc_dictionary_set_int64(reply, "result", 0);
	if (id && strcmp(id, "RunBuiltinScript") == 0) {
		const char *script = args && xpc_get_type(args) == XPC_TYPE_DICTIONARY ? xpc_dictionary_get_string(args, "script") : NULL;
		const char *bta = args && xpc_get_type(args) == XPC_TYPE_DICTIONARY ? xpc_dictionary_get_string(args, "btaddress") : NULL;
		xpc_dictionary_set_string(reply, "firmware", "soc3_0_ram_bt_1a_1 (MT6893 BGFSYS, kernel 0720)");
		xpc_dictionary_set_int64(reply, "manufacturer", 70);    /* MediaTek */
		if (said++ < 40) note("BlueTool RunBuiltinScript '%s' btaddress %s -> result 0", script ? script : "?", bta ? bta : "-");
	} else if (id && strcmp(id, "RunCommand") == 0) {
		const char *cmd = args && xpc_get_type(args) == XPC_TYPE_DICTIONARY ? xpc_dictionary_get_string(args, "command") : NULL;
		xpc_dictionary_set_string(reply, "output", "");
		if (said++ < 40) note("BlueTool RunCommand '%s' -> result 0, no output", cmd ? cmd : "?");
	} else {
		if (said++ < 40) note("BlueTool message '%s' -> result 0", id ? id : "?");
	}
	xpc_connection_send_message(peer, reply);
	xpc_release(reply);
}

int
main(void)
{
	if (access(GATE, F_OK) != 0) {
		return 0;                          /* kill switch: no Bluetooth (bluetoothd behaves as before) */
	}
	note("n2btd starts (pid %d, uptime %ld s)", getpid(), uptime_sec());
	if (guard_tripped()) return 0;
	xpc_connection_t listener = xpc_connection_create_mach_service("com.apple.BlueTool", NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
	if (listener == NULL) {
		note("com.apple.BlueTool listener not created");
	} else {
		xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
			if (xpc_get_type(peer) != XPC_TYPE_CONNECTION) return;
			xpc_connection_set_event_handler(peer, ^(xpc_object_t m) {
				if (xpc_get_type(m) == XPC_TYPE_DICTIONARY) handle(peer, m);
			});
			xpc_connection_resume(peer);
		});
		xpc_connection_resume(listener);
	}
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ bring_up(); });
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), ^{ btd_log_thread(); });
	dispatch_main();
}
