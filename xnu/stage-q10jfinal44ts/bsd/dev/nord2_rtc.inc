/*
 * 0540 (rtc-20261114): NORD2 RTC, the XNU side.  Included by sweet_touch.c before ni_thread: every MT6359P access runs on the input
 * thread, the sole owner of PWRAP SWINF2 (the same owner the GPU / audio / Wi-Fi rail / key code uses), through the existing ni_pwrap
 * mapping (no new ml_io_map).  Model, allow-list and sequences: nord2_rtc.h.
 *
 *   boot:  nord2_input_init() arms the read (n2rtc_arm), ni_thread reads the RTC as its first PWRAP work (n2rtc_boot_read), the first
 *          execve (0530 hook in kern_exec.c) takes the time through nord2_rtc_boot_time() (waits at most N2CK_RTC_WAIT_MS) and sets the
 *          calendar to the newest of RTC and 0530 floor.  If the hook gave up before the read finished, the owner thread applies a
 *          valid read itself (never backwards).  An invalid RTC (zero, garbage, before 2025) leaves the floor in charge.
 *   run:   n2rtc_service() every 15 s compares the calendar with the RTC predicted from the last read (no PMIC access while they agree);
 *          when the calendar moved (timed's settimeofday after NTP, a user change), when the RTC was invalid, or when the hourly re-read
 *          finds >= 3 s of drift, the calendar is written into the RTC (rounded second, read back twice).  That is the PESetUTCTimeOfDay
 *          path of a real platform expert: SweetPlatformExpert is a prebuilt CI object here and its setGMTTimeOfDay is a no-op.
 *   kill:  boot-arg n2rtc=0 or n2gdv +256 = the old behaviour (no RTC access at all, the 0530 floor alone); n2rtc=2 = read only.
 *          sysctl kern.n2rtc 0 / 1 / 2 at run time (0 stops reads and writes, 2 stops writes); kern.n2rtcstat = state and counters.
 * Log: "NORD2 RTC: boot read ...", "NORD2 RTC: write ...", "NORD2 RTC: late apply ...", and kern_exec.c's "NORD2 CLOCK: rtc ...".
 */
#include <sys/sysctl.h>
#include "nord2_rtc.h"

enum { N2RTC_BOOT_OFF = 0, N2RTC_BOOT_PENDING, N2RTC_BOOT_VALID, N2RTC_BOOT_INVALID, N2RTC_BOOT_LATE, N2RTC_BOOT_APPLIED };

static uint32_t n2rtc_boot_state;               /* N2RTC_BOOT_*, atomic (owner thread <-> first exec) */
static uint32_t n2rtc_exec_seen;                /* the first exec has seeded the calendar (RTC or floor) */
static int n2rtc_mode;                          /* enum n2rtc_mode, kern.n2rtc */
static uint64_t n2rtc_boot_secs, n2rtc_boot_abs;
static struct n2rtc_sync n2rtc_s;               /* owner thread */
static uint64_t n2rtc_next_ms;
static unsigned n2rtc_logged_fail;
static struct {
	uint16_t raw[N2RTC_NREG], bbpu, pdn1, pdn2, prot, pk1, pk2;
	int boot_rc, last_wrc, last_first_rc;
	uint64_t last_target;
	unsigned unlocks, refused;
} n2rtc_d;

/* One SWINF2 read / write, the sequence of ni_pmic_read() and nw_pmic_write() (MT6885 PMIF: STA bit15 ready, FSM bits 1..3, 6 =
 * WFVLDCLR, read data at 0xc94, VLD_CLR at 0xca4, write = (1 << 29) | addr with the data at 0xc84).  Bounded; owner thread only. */
static bool
n2rtc_pw_read(void *ctx, uint16_t addr, uint16_t *out)
{
	(void)ctx;
	if (!ni_pwrap) { return false; }
	uint32_t v = ni_rd(ni_pwrap, 0xca8);
	if (!(v & (1u << 15))) { return false; }
	if (((v >> 1) & 7u) == 6 && ni_pwrap_owned) {
		ni_wr(ni_pwrap, 0xca4, 1);
		v = ni_rd(ni_pwrap, 0xca8);
	}
	if (((v >> 1) & 7u) != 0) { return false; }
	ni_pwrap_owned = true;
	ni_wr(ni_pwrap, 0xc80, addr);
	for (unsigned i = 0; i < 1000; i++) {
		v = ni_rd(ni_pwrap, 0xca8);
		if (((v >> 1) & 7u) == 6) {
			*out = (uint16_t)ni_rd(ni_pwrap, 0xc94);
			ni_wr(ni_pwrap, 0xca4, 1);
			return true;
		}
		delay(1);
	}
	return false;
}

static bool
n2rtc_pw_idle(void)
{
	for (unsigned i = 0; i < 2000; i++) {
		if ((ni_rd(ni_pwrap, 0xca8) & 0x800e) == 0x8000) { return true; }
		delay(10);
	}
	return false;
}

static bool
n2rtc_pw_write(void *ctx, uint16_t addr, uint16_t value)
{
	(void)ctx;
	if (!ni_pwrap || !n2rtc_pw_idle()) { return false; }
	ni_pwrap_owned = true;
	ni_wr(ni_pwrap, 0xc84, value);
	ni_wr(ni_pwrap, 0xc80, (1u << 29) | addr);
	return n2rtc_pw_idle();         /* write completion is idle, not read-valid */
}

static void
n2rtc_udelay(void *ctx, unsigned us)
{
	(void)ctx;
	delay(us);
}

static struct n2rtc_io n2rtc_io_g = { n2rtc_pw_read, n2rtc_pw_write, n2rtc_udelay, NULL, 0 };

static void
n2rtc_fmt(uint64_t s, char *buf, size_t n)
{
	struct n2rtc_tm t;
	if (!n2rtc_secs_tm(s, &t)) {
		snprintf(buf, n, "(out of range)");
		return;
	}
	snprintf(buf, n, "%04d-%02u-%02u %02u:%02u:%02u UTC", t.year, t.mon, t.mday, t.hour, t.min, t.sec);
}

/* nord2_input_init(), before the owner thread exists */
static void
n2rtc_arm(void)
{
	int m = N2RTC_MODE_RW, gdv = 0;
	if (!PE_parse_boot_argn("n2rtc", &m, sizeof(m)) && PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & 256)) {
		m = N2RTC_MODE_OFF;
	}
	if (m < N2RTC_MODE_OFF || m > N2RTC_MODE_RO) {
		m = N2RTC_MODE_RO;
	}
	n2rtc_mode = m;
	__atomic_store_n(&n2rtc_boot_state, m == N2RTC_MODE_OFF ? N2RTC_BOOT_OFF : N2RTC_BOOT_PENDING, __ATOMIC_RELEASE);
	if (m == N2RTC_MODE_OFF) {
		printf("NORD2 RTC: off (n2rtc=0 / n2gdv +256): no RTC access, the 0530 floor alone\n");
	}
}

static void
n2rtc_disarm(void)
{
	uint32_t want = N2RTC_BOOT_PENDING;
	(void)__atomic_compare_exchange_n(&n2rtc_boot_state, &want, N2RTC_BOOT_OFF, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* RTC seconds + the time since the read (+ 0.5 s: the read happened somewhere inside an RTC second) */
static void
n2rtc_boot_now(uint64_t *secs, uint32_t *usecs)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time() - n2rtc_boot_abs, &ns);
	ns += 500000000ULL;
	*secs = n2rtc_boot_secs + ns / 1000000000ULL;
	*usecs = (uint32_t)(ns % 1000000000ULL / 1000);
}

/* ni_thread, first PWRAP work (ni_pwrap mapped just before) */
static void
n2rtc_boot_read(void)
{
	char when[48];
	uint64_t s = 0;
	if (__atomic_load_n(&n2rtc_boot_state, __ATOMIC_ACQUIRE) != N2RTC_BOOT_PENDING) {
		return;
	}
	int rc = ni_pwrap ? n2rtc_read_time(&n2rtc_io_g, &s, n2rtc_d.raw) : N2RTC_E_IO;
	n2rtc_boot_abs = mach_absolute_time();
	n2rtc_d.boot_rc = rc;
	(void)n2rtc_rd(&n2rtc_io_g, N2RTC_BBPU, &n2rtc_d.bbpu);       /* the rest: read only, for the log */
	(void)n2rtc_rd(&n2rtc_io_g, N2RTC_PDN1, &n2rtc_d.pdn1);
	(void)n2rtc_rd(&n2rtc_io_g, N2RTC_PDN2, &n2rtc_d.pdn2);
	(void)n2rtc_rd(&n2rtc_io_g, N2RTC_PROT, &n2rtc_d.prot);
	(void)n2rtc_rd(&n2rtc_io_g, N2RTC_POWERKEY1, &n2rtc_d.pk1);
	(void)n2rtc_rd(&n2rtc_io_g, N2RTC_POWERKEY2, &n2rtc_d.pk2);
	n2rtc_boot_secs = s;
	n2rtc_fmt(s, when, sizeof(when));
	printf("NORD2 RTC: boot read %s (rc %d): %s = %llu s; raw sec %#x min %#x hou %#x dom %#x dow %#x mth %#x yea %#x (year %d); "
	    "BBPU %#x PDN1 %#x PDN2 %#x PROT %#x POWERKEY %#x/%#x%s; mode %d (n2rtc=0 or n2gdv +256 = off, 2 = read only)\n",
	    rc == N2RTC_OK ? "VALID" : "INVALID", rc, rc == N2RTC_OK ? when : "-", (unsigned long long)s,
	    n2rtc_d.raw[0], n2rtc_d.raw[1], n2rtc_d.raw[2], n2rtc_d.raw[3], n2rtc_d.raw[4], n2rtc_d.raw[5], n2rtc_d.raw[6],
	    N2RTC_MIN_YEAR + (n2rtc_d.raw[6] & 0x7f), n2rtc_d.bbpu, n2rtc_d.pdn1, n2rtc_d.pdn2, n2rtc_d.prot, n2rtc_d.pk1, n2rtc_d.pk2,
	    (n2rtc_d.pk1 == N2RTC_POWERKEY1_KEY && n2rtc_d.pk2 == N2RTC_POWERKEY2_KEY) ? " (keys ok)" : " (keys NOT the preloader's)",
	    n2rtc_mode);
	uint32_t want = N2RTC_BOOT_PENDING, next = rc == N2RTC_OK ? N2RTC_BOOT_VALID : N2RTC_BOOT_INVALID;
	if (__atomic_compare_exchange_n(&n2rtc_boot_state, &want, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		return;                 /* the first exec takes it (or already waits for it) */
	}
	if (want != N2RTC_BOOT_LATE) {
		return;
	}
	/* the first exec stopped waiting: apply a valid RTC here, never backwards (the floor may already be ahead) */
	if (rc == N2RTC_OK) {
		clock_sec_t cs = 0;
		clock_usec_t cu = 0;
		uint64_t ns_s = 0;
		uint32_t ns_u = 0;
		clock_get_calendar_microtime(&cs, &cu);
		n2rtc_boot_now(&ns_s, &ns_u);
		bool set = ns_s > (uint64_t)cs && ns_s <= N2RTC_TRUST_MAX_SECS;
		if (set) {
			clock_set_calendar_microtime((clock_sec_t)ns_s, (clock_usec_t)ns_u);
		}
		printf("NORD2 RTC: late apply: calendar %llu s %s %llu s\n", (unsigned long long)cs, set ? "set to" : "kept (RTC not newer):",
		    (unsigned long long)ns_s);
	}
	__atomic_store_n(&n2rtc_boot_state, rc == N2RTC_OK ? N2RTC_BOOT_APPLIED : N2RTC_BOOT_INVALID, __ATOMIC_RELEASE);
}

/* kern_exec.c, the first execve (0530 hook) */
int
nord2_rtc_boot_time(unsigned wait_ms, uint64_t *secs, uint32_t *usecs, unsigned *state)
{
	int ok = 0;
	for (unsigned waited = 0;; waited += 10) {
		uint32_t st = __atomic_load_n(&n2rtc_boot_state, __ATOMIC_ACQUIRE);
		if (st == N2RTC_BOOT_VALID) {
			uint32_t want = N2RTC_BOOT_VALID;
			if (__atomic_compare_exchange_n(&n2rtc_boot_state, &want, N2RTC_BOOT_APPLIED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
				n2rtc_boot_now(secs, usecs);
				ok = 1;
				st = N2RTC_BOOT_APPLIED;
				break;
			}
			continue;
		}
		if (st != N2RTC_BOOT_PENDING) {
			break;
		}
		if (waited >= wait_ms) {
			uint32_t want = N2RTC_BOOT_PENDING;
			if (__atomic_compare_exchange_n(&n2rtc_boot_state, &want, N2RTC_BOOT_LATE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
				st = N2RTC_BOOT_LATE;
				break;
			}
			continue;
		}
		delay_for_interval(10, NSEC_PER_MSEC);
	}
	*state = __atomic_load_n(&n2rtc_boot_state, __ATOMIC_ACQUIRE);
	__atomic_store_n(&n2rtc_exec_seen, 1, __ATOMIC_RELEASE);
	return ok;
}

static const char *
n2rtc_why_name(unsigned w)
{
	return w == N2RTC_WHY_INVALID ? "RTC invalid" : w == N2RTC_WHY_STEP ? "calendar stepped (settimeofday/NTP)" :
	       w == N2RTC_WHY_DRIFT ? "drift" : "-";
}

/* ni_thread loop, every 10 ms: a deadline compare unless a 15 s check is due */
static void
n2rtc_service(void)
{
	int mode = __atomic_load_n(&n2rtc_mode, __ATOMIC_RELAXED);
	if (mode == N2RTC_MODE_OFF || !ni_pwrap) {
		return;
	}
	uint64_t now_ms = ni_now_us() / 1000;
	if (now_ms < n2rtc_next_ms) {
		return;
	}
	n2rtc_next_ms = now_ms + N2RTC_CHECK_MS;
	if (!__atomic_load_n(&n2rtc_exec_seen, __ATOMIC_ACQUIRE)) {
		return;                 /* the calendar is not seeded yet: nothing to compare */
	}
	uint32_t bs = __atomic_load_n(&n2rtc_boot_state, __ATOMIC_ACQUIRE);
	if (bs == N2RTC_BOOT_PENDING || bs == N2RTC_BOOT_LATE || bs == N2RTC_BOOT_VALID) {
		return;                 /* boot handshake still running */
	}
	clock_sec_t cs = 0;
	clock_usec_t cu = 0;
	clock_get_calendar_microtime(&cs, &cu);
	if (n2rtc_should_read(&n2rtc_s, (uint64_t)cs, now_ms) != N2RTC_ACT_READ) {
		return;
	}
	uint64_t rtc = 0;
	int rc = n2rtc_read_time(&n2rtc_io_g, &rtc, NULL);
	clock_get_calendar_microtime(&cs, &cu);
	int act = n2rtc_after_read(&n2rtc_s, mode, (uint64_t)cs, rc, rtc, now_ms);
	if ((rc == N2RTC_E_IO || rc == N2RTC_E_UNSTABLE) && n2rtc_logged_fail++ < 5) {
		printf("NORD2 RTC: read failed rc %d (PWRAP status %#x); retry in %llu s\n", rc, ni_rd(ni_pwrap, 0xca8), N2RTC_CHECK_MS / 1000);
	}
	if (act != N2RTC_ACT_WRITE) {
		return;
	}
	struct n2rtc_wres w;
	char when[48];
	clock_get_calendar_microtime(&cs, &cu);
	uint64_t target = n2rtc_target((uint64_t)cs, (uint32_t)cu);
	int wrc = n2rtc_write_time(&n2rtc_io_g, target, mode == N2RTC_MODE_RW, &w);
	n2rtc_after_write(&n2rtc_s, wrc, target, now_ms);
	n2rtc_d.last_wrc = wrc;
	n2rtc_d.last_first_rc = w.first_rc;
	n2rtc_d.last_target = target;
	n2rtc_d.unlocks += w.unlocked;
	n2rtc_d.refused = n2rtc_io_g.refused;
	n2rtc_fmt(target, when, sizeof(when));
	printf("NORD2 RTC: write %s (%s): calendar %llu.%06u -> RTC %s = %llu s; RTC was %s %llu (calendar - RTC %lld s); readback %llu; "
	    "first rc %d ignored %d unlock %d relock %d attempts %u refused %u; writes %u fails %u%s\n", wrc == N2RTC_OK ? "ok" : "FAILED",
	    n2rtc_why_name(n2rtc_s.why), (unsigned long long)cs, (unsigned)cu, when, (unsigned long long)target,
	    rc == N2RTC_OK ? "valid" : "invalid", (unsigned long long)rtc, (long long)n2rtc_s.last_diff, (unsigned long long)w.readback,
	    w.first_rc, w.ignored, w.unlocked, w.relocked, w.attempts, n2rtc_io_g.refused, n2rtc_s.writes, n2rtc_s.write_fails,
	    n2rtc_s.writes_off ? "; writes OFF until the next boot" : "");
}

static int
n2rtc_sysctl_mode SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = __atomic_load_n(&n2rtc_mode, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < N2RTC_MODE_OFF || v > N2RTC_MODE_RO) { return EINVAL; }
	__atomic_store_n(&n2rtc_mode, v, __ATOMIC_RELAXED);
	printf("NORD2 RTC: sysctl kern.n2rtc=%d (0 off, 1 read+write, 2 read only)\n", v);
	return 0;
}

static int
n2rtc_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[400];
	struct n2rtc_sync s = n2rtc_s;          /* owner-thread fields, read without a lock: a diagnostic snapshot */
	snprintf(buf, sizeof(buf), "mode=%d boot_state=%u boot_rc=%d boot_secs=%llu raw=%x,%x,%x,%x,%x,%x,%x bbpu=%#x pdn1=%#x pdn2=%#x prot=%#x "
	    "pk=%#x/%#x exec_seen=%u reads=%u read_fails=%u writes=%u write_fails=%u writes_off=%d last_diff=%lld last_target=%llu last_wrc=%d "
	    "last_first_rc=%d unlocks=%u refused=%u", n2rtc_mode, n2rtc_boot_state, n2rtc_d.boot_rc, (unsigned long long)n2rtc_boot_secs,
	    n2rtc_d.raw[0], n2rtc_d.raw[1], n2rtc_d.raw[2], n2rtc_d.raw[3], n2rtc_d.raw[4], n2rtc_d.raw[5], n2rtc_d.raw[6], n2rtc_d.bbpu,
	    n2rtc_d.pdn1, n2rtc_d.pdn2, n2rtc_d.prot, n2rtc_d.pk1, n2rtc_d.pk2, n2rtc_exec_seen, s.reads, s.read_fails, s.writes, s.write_fails,
	    s.writes_off, (long long)s.last_diff, (unsigned long long)n2rtc_d.last_target, n2rtc_d.last_wrc, n2rtc_d.last_first_rc,
	    n2rtc_d.unlocks, n2rtc_d.refused);
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
SYSCTL_PROC(_kern, OID_AUTO, n2rtc, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2rtc_sysctl_mode, "I",
    "0540 MT6359P RTC: 0 off, 1 read + write the calendar into it (default), 2 read only");
SYSCTL_PROC(_kern, OID_AUTO, n2rtcstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, n2rtc_sysctl_stat, "A",
    "0540 MT6359P RTC state and counters");
