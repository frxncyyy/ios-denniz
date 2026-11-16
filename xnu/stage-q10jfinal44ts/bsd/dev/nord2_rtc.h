/*
 * 0540 (rtc-20261114): NORD2 RTC -- the MT6359P real-time clock as the iOS time of day.  Register model, access sequences and
 * the sync policy, host-testable (rtc-20261114/test/rtc_test.c runs these exact functions against a simulated MT6359 RTC).
 * The XNU glue is nord2_rtc.inc (included by sweet_touch.c: every access on the PWRAP owner thread, ni_thread) and the
 * first-exec hook of 0530 in kern_exec.c (nord2_clockfloor.h: n2ck_pick_rtc).
 *
 * Sources (read, not guessed):
 *   nord2-current-kernel (the Nord 2 Android 11 tree) drivers/rtc/rtc-mt6358.c, the driver mt6893_defconfig builds
 *   (CONFIG_RTC_DRV_MT6358=y; DT arch/arm64/boot/dts/mediatek/mt6359p.dtsi: "mediatek,mt6359-rtc", base = <0x580>):
 *     register offsets from base, RTC_TC_SEC..YEA field masks, RTC_MIN_YEAR 1968 (hardware year 0 = 1968, 7 bits -> 2095),
 *     mtk_rtc_read_time() (read SEC..YEA, re-read SEC, repeat while SEC went backwards = a carry during the read),
 *     rtc_ops_set_time() (write SEC..YEA, RTC_WRTGR = 1, wait while RTC_BBPU bit6 CBUSY, 500 ms bound).
 *   drivers/mfd/mt6358-misc.c: RTC_PDN1 bit13 = Fast Boot (the bit sweet_arm_fastboot() and the boot stub set), PDN1/PDN2/SPAR0/
 *     SPAR1 = Android/recovery/power-on-alarm bits, RTC_AL_MTH/RTC_AL_HOU high bytes = fuel-gauge spare data.
 *   drivers/misc/mediatek/rtc/mtk_rtc_hal_common.c + mt6357/mtk_rtc_hw.h: rtc_writeif_unlock() = RTC_PROT 0x586a, trigger,
 *     RTC_PROT 0x9136, trigger; rtc_set_writeif(false) = RTC_PROT 0, trigger.
 *   Hardware (hf1 boot log, 2026-11-13): "NORD2 AUTOFB: RTC_PDN1 0000 -> 2000 (rc 0 0 0 0)": a PDN1 write committed by WRTGR without
 *     any unlock, i.e. the bootloader leaves the RTC write interface open.  So a plain write is tried first; the unlock is only a
 *     fallback when the readback proves the write was ignored, and then the interface is locked again (left as found).
 *
 * What this code may touch (enforced below, n2rtc_may_read / n2rtc_may_write; a refused access is an error, never forwarded):
 *   reads:  RTC_BBPU (CBUSY), RTC_TC_SEC..RTC_TC_YEA, RTC_POWERKEY1/2, RTC_PDN1, RTC_PDN2, RTC_PROT (the last five for the log only)
 *   writes: RTC_TC_SEC/MIN/HOU/DOM/MTH/YEA (read-modify-write: bits outside each field and RTC_TC_DOW are kept as read),
 *           RTC_WRTGR = 1, RTC_PROT = 0x586a / 0x9136 / 0 (fallback only).
 *   Never: RTC_BBPU (bit0 PWREN: writing it without PWREN powers the phone off), RTC_IRQ_STA (read-clear), RTC_IRQ_EN / RTC_AL_*
 *   (alarm: a power-on alarm could wake/boot the phone), RTC_OSC32CON (32k oscillator), RTC_POWERKEY1/2 (a mismatch makes the
 *   preloader re-initialise the RTC), RTC_PDN1/PDN2/SPAR0/SPAR1 (fast-boot, recovery, power-on-alarm bits), RTC_CON.
 *
 * The RTC keeps UTC, like Android on this phone.
 */
#ifndef NORD2_RTC_H
#define NORD2_RTC_H

#include <stdint.h>
#include <stdbool.h>

#define N2RTC_BASE              0x0580u
#define N2RTC_BBPU              0x0588u          /* bit6 CBUSY; read only here */
#define N2RTC_IRQ_STA           0x058au          /* read-clear: never accessed */
#define N2RTC_TC_SEC            0x0592u          /* + 2 * N2RTC_I_* */
#define N2RTC_TC_YEA            0x059eu
#define N2RTC_AL_SEC            0x05a0u          /* alarm block 0x5a0..0x5ac: never accessed */
#define N2RTC_POWERKEY1         0x05b0u
#define N2RTC_POWERKEY2         0x05b2u
#define N2RTC_PDN1              0x05b4u          /* bit13 RTC_FAST_BOOT (sweet_arm_fastboot, boot stub) */
#define N2RTC_PDN2              0x05b6u
#define N2RTC_PROT              0x05bcu
#define N2RTC_WRTGR             0x05c2u

#define N2RTC_BBPU_CBUSY        (1u << 6)
#define N2RTC_PROT_KEY1         0x586au
#define N2RTC_PROT_KEY2         0x9136u
#define N2RTC_POWERKEY1_KEY     0xa357u          /* mtk_rtc_hw.h: what the preloader writes once the RTC is initialised */
#define N2RTC_POWERKEY2_KEY     0x67d2u

enum { N2RTC_I_SEC, N2RTC_I_MIN, N2RTC_I_HOU, N2RTC_I_DOM, N2RTC_I_DOW, N2RTC_I_MTH, N2RTC_I_YEA, N2RTC_NREG };
static const uint16_t n2rtc_field_mask[N2RTC_NREG] = { 0x3f, 0x3f, 0x1f, 0x1f, 0x07, 0x0f, 0x7f };

#define N2RTC_MIN_YEAR          1968             /* hardware year 0 (rtc-mt6358.c RTC_MIN_YEAR) */
#define N2RTC_VALID_YEAR        2025             /* an RTC before 2025 is "never set / lost power": invalid */
#define N2RTC_MAX_YEAR          2095             /* 1968 + 127 */
#define N2RTC_VALID_SECS        1735689600ULL    /* 2025-01-01 00:00:00 UTC */
#define N2RTC_MAX_SECS          3976214399ULL    /* 2095-12-31 23:59:59 UTC */
#define N2RTC_TRUST_MAX_SECS    2110752000ULL    /* 0530 N2CK_BUILD_EPOCH + N2CK_MAX_AHEAD: a boot RTC beyond it is not applied */

#define N2RTC_BUSY_US           500000u          /* rtc_write_trigger: 500 ms */
#define N2RTC_POLL_US           20u

enum n2rtc_rc {
	N2RTC_OK = 0,
	N2RTC_E_IO,             /* PWRAP transaction failed, or an access outside the allow-list was refused */
	N2RTC_E_UNSTABLE,       /* SEC kept going backwards between the reads */
	N2RTC_E_RANGE,          /* a field out of range (garbage / zero registers) */
	N2RTC_E_OLD,            /* a well-formed date before 2025 */
	N2RTC_E_BUSY,           /* CBUSY did not clear */
	N2RTC_E_VERIFY,         /* written, but the readback is not the time written */
	N2RTC_E_ARGS,
};

struct n2rtc_tm {
	int year;
	unsigned mon, mday, hour, min, sec;     /* mon 1..12, mday 1..31 */
};

/* ---------------------------------------------------------------- calendar arithmetic (proleptic Gregorian, UTC) */
static inline int64_t
n2rtc_days_from_civil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

static inline void
n2rtc_civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t yy = (int64_t)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y = (int)(yy + (*m <= 2));
}

static inline unsigned
n2rtc_mdays(int y, unsigned m)
{
	static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m == 2) {
		return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
	}
	return md[(m - 1) % 12];
}

static inline uint64_t
n2rtc_tm_secs(const struct n2rtc_tm *t)
{
	return (uint64_t)(n2rtc_days_from_civil(t->year, t->mon, t->mday) * 86400 + t->hour * 3600 + t->min * 60 + t->sec);
}

/* false when s is outside 2025-01-01 .. 2095-12-31 (what the RTC may hold and what this code ever writes) */
static inline bool
n2rtc_secs_tm(uint64_t s, struct n2rtc_tm *t)
{
	if (s < N2RTC_VALID_SECS || s > N2RTC_MAX_SECS) {
		return false;
	}
	uint64_t day = s / 86400, rem = s % 86400;
	n2rtc_civil_from_days((int64_t)day, &t->year, &t->mon, &t->mday);
	t->hour = (unsigned)(rem / 3600);
	t->min = (unsigned)(rem / 60 % 60);
	t->sec = (unsigned)(rem % 60);
	return true;
}

static inline int64_t
n2rtc_sdiff(uint64_t a, uint64_t b)
{
	return a >= b ? (int64_t)(a - b) : -(int64_t)(b - a);
}

/* ---------------------------------------------------------------- register <-> time */
static inline int
n2rtc_regs_decode(const uint16_t r[N2RTC_NREG], struct n2rtc_tm *t)
{
	t->sec = r[N2RTC_I_SEC] & n2rtc_field_mask[N2RTC_I_SEC];
	t->min = r[N2RTC_I_MIN] & n2rtc_field_mask[N2RTC_I_MIN];
	t->hour = r[N2RTC_I_HOU] & n2rtc_field_mask[N2RTC_I_HOU];
	t->mday = r[N2RTC_I_DOM] & n2rtc_field_mask[N2RTC_I_DOM];
	t->mon = r[N2RTC_I_MTH] & n2rtc_field_mask[N2RTC_I_MTH];
	t->year = N2RTC_MIN_YEAR + (int)(r[N2RTC_I_YEA] & n2rtc_field_mask[N2RTC_I_YEA]);
	if (t->sec > 59 || t->min > 59 || t->hour > 23 || t->mon < 1 || t->mon > 12 || t->mday < 1 ||
	    t->mday > n2rtc_mdays(t->year, t->mon)) {
		return N2RTC_E_RANGE;
	}
	return t->year < N2RTC_VALID_YEAR ? N2RTC_E_OLD : N2RTC_OK;
}

/* new = old with only the six time fields replaced; RTC_TC_DOW and every bit outside a field are kept */
static inline void
n2rtc_regs_encode(const struct n2rtc_tm *t, const uint16_t old[N2RTC_NREG], uint16_t out[N2RTC_NREG])
{
	unsigned v[N2RTC_NREG] = { t->sec, t->min, t->hour, t->mday, 0, t->mon, (unsigned)(t->year - N2RTC_MIN_YEAR) };
	for (unsigned i = 0; i < N2RTC_NREG; i++) {
		out[i] = i == N2RTC_I_DOW ? old[i] : (uint16_t)((old[i] & ~n2rtc_field_mask[i]) | (v[i] & n2rtc_field_mask[i]));
	}
}

/* ---------------------------------------------------------------- guarded access */
struct n2rtc_io {
	bool (*rd)(void *ctx, uint16_t addr, uint16_t *value);   /* one PWRAP read (owner thread) */
	bool (*wr)(void *ctx, uint16_t addr, uint16_t value);    /* one PWRAP write, completion waited */
	void (*udelay)(void *ctx, unsigned us);
	void *ctx;
	unsigned refused;                                       /* accesses refused by the allow-list (must stay 0) */
};

static inline bool
n2rtc_may_read(uint16_t a)
{
	return a == N2RTC_BBPU || (a >= N2RTC_TC_SEC && a <= N2RTC_TC_YEA && !(a & 1)) || a == N2RTC_POWERKEY1 ||
	       a == N2RTC_POWERKEY2 || a == N2RTC_PDN1 || a == N2RTC_PDN2 || a == N2RTC_PROT;
}

static inline bool
n2rtc_may_write(uint16_t a, uint16_t v)
{
	if (a >= N2RTC_TC_SEC && a <= N2RTC_TC_YEA && !(a & 1)) {
		return a != N2RTC_TC_SEC + 2 * N2RTC_I_DOW;     /* DOW is never written */
	}
	if (a == N2RTC_WRTGR) {
		return v == 1;
	}
	if (a == N2RTC_PROT) {
		return v == N2RTC_PROT_KEY1 || v == N2RTC_PROT_KEY2 || v == 0;
	}
	return false;
}

static inline bool
n2rtc_rd(struct n2rtc_io *io, uint16_t a, uint16_t *v)
{
	if (!n2rtc_may_read(a)) {
		io->refused++;
		return false;
	}
	return io->rd(io->ctx, a, v);
}

static inline bool
n2rtc_wr(struct n2rtc_io *io, uint16_t a, uint16_t v)
{
	if (!n2rtc_may_write(a, v)) {
		io->refused++;
		return false;
	}
	return io->wr(io->ctx, a, v);
}

/* ---------------------------------------------------------------- sequences */
/* rtc-mt6358.c mtk_rtc_read_time(): SEC..YEA, then SEC again; a smaller second means a carry happened in between: read again */
static inline int
n2rtc_read_regs(struct n2rtc_io *io, uint16_t r[N2RTC_NREG])
{
	for (unsigned tries = 0; tries < 3; tries++) {
		uint16_t again = 0;
		for (unsigned i = 0; i < N2RTC_NREG; i++) {
			if (!n2rtc_rd(io, (uint16_t)(N2RTC_TC_SEC + 2 * i), &r[i])) {
				return N2RTC_E_IO;
			}
		}
		if (!n2rtc_rd(io, N2RTC_TC_SEC, &again)) {
			return N2RTC_E_IO;
		}
		if ((again & n2rtc_field_mask[N2RTC_I_SEC]) >= (r[N2RTC_I_SEC] & n2rtc_field_mask[N2RTC_I_SEC])) {
			return N2RTC_OK;
		}
	}
	return N2RTC_E_UNSTABLE;
}

/* *secs is set only with N2RTC_OK; r (may be NULL) gets the raw registers whatever the result */
static inline int
n2rtc_read_time(struct n2rtc_io *io, uint64_t *secs, uint16_t *r)
{
	uint16_t buf[N2RTC_NREG] = { 0 };
	struct n2rtc_tm t;
	int rc = n2rtc_read_regs(io, buf);
	if (r != (uint16_t *)0) {
		for (unsigned i = 0; i < N2RTC_NREG; i++) {
			r[i] = buf[i];
		}
	}
	if (rc != N2RTC_OK) {
		return rc;
	}
	rc = n2rtc_regs_decode(buf, &t);
	if (rc == N2RTC_OK) {
		*secs = n2rtc_tm_secs(&t);
	}
	return rc;
}

static inline bool
n2rtc_wait_idle(struct n2rtc_io *io)
{
	for (unsigned waited = 0; waited <= N2RTC_BUSY_US; waited += N2RTC_POLL_US) {
		uint16_t b = 0;
		if (!n2rtc_rd(io, N2RTC_BBPU, &b)) {
			return false;
		}
		if (!(b & N2RTC_BBPU_CBUSY)) {
			return true;
		}
		io->udelay(io->ctx, N2RTC_POLL_US);
	}
	return false;
}

/* rtc_write_trigger(): RTC_WRTGR = 1 moves the written values into the RTC domain; CBUSY is set until that is done */
static inline bool
n2rtc_trigger(struct n2rtc_io *io)
{
	return n2rtc_wr(io, N2RTC_WRTGR, 1) && n2rtc_wait_idle(io);
}

struct n2rtc_wres {
	uint16_t old[N2RTC_NREG], neu[N2RTC_NREG], rb[N2RTC_NREG];
	uint64_t readback;
	unsigned attempts;      /* time writes issued (1, or 2 with the unlock fallback) */
	bool unlocked, relocked;
	bool ignored;           /* the plain write left the registers as they were: the signature of a locked interface */
	int first_rc;           /* result of the plain attempt */
};

/* the registers after a write still show the old time (seconds may have ticked): the write was not committed */
static inline bool
n2rtc_write_ignored(const uint16_t old[N2RTC_NREG], const uint16_t neu[N2RTC_NREG], const uint16_t rb[N2RTC_NREG])
{
	struct n2rtc_tm to, tr;
	int ro = n2rtc_regs_decode(old, &to), rr = n2rtc_regs_decode(rb, &tr);
	if (ro != N2RTC_E_RANGE && rr != N2RTC_E_RANGE) {
		int64_t d = n2rtc_sdiff(n2rtc_tm_secs(&tr), n2rtc_tm_secs(&to));
		return d >= 0 && d <= 2;
	}
	bool same_old = true, same_new = true;
	for (unsigned i = N2RTC_I_MIN; i < N2RTC_NREG; i++) {
		if (i == N2RTC_I_DOW) {
			continue;
		}
		same_old &= (rb[i] & n2rtc_field_mask[i]) == (old[i] & n2rtc_field_mask[i]);
		same_new &= (rb[i] & n2rtc_field_mask[i]) == (neu[i] & n2rtc_field_mask[i]);
	}
	return same_old && !same_new;
}

static inline int
n2rtc_write_once(struct n2rtc_io *io, const struct n2rtc_tm *t, uint64_t target, struct n2rtc_wres *w)
{
	static const uint8_t order[] = { N2RTC_I_SEC, N2RTC_I_MIN, N2RTC_I_HOU, N2RTC_I_DOM, N2RTC_I_MTH, N2RTC_I_YEA };   /* Linux order, no DOW */
	int rc = n2rtc_read_regs(io, w->old);
	if (rc != N2RTC_OK) {
		return rc;
	}
	n2rtc_regs_encode(t, w->old, w->neu);
	w->attempts++;
	for (unsigned k = 0; k < sizeof(order); k++) {
		if (!n2rtc_wr(io, (uint16_t)(N2RTC_TC_SEC + 2 * order[k]), w->neu[order[k]])) {
			return N2RTC_E_IO;
		}
	}
	if (!n2rtc_trigger(io)) {
		return N2RTC_E_BUSY;
	}
	w->readback = 0;
	rc = n2rtc_read_time(io, &w->readback, w->rb);
	if (rc == N2RTC_E_IO || rc == N2RTC_E_UNSTABLE) {
		return N2RTC_E_IO;
	}
	if (rc == N2RTC_OK && w->readback + 1 >= target && w->readback <= target + 2) {
		return N2RTC_OK;
	}
	w->ignored = n2rtc_write_ignored(w->old, w->neu, w->rb);
	return N2RTC_E_VERIFY;
}

/*
 * Write target (UTC seconds) into the RTC.  The interface is expected open (the bootloader leaves it so).  Only if the plain write
 * was demonstrably ignored (registers unchanged) and allow_unlock is set: unlock with the two RTC_PROT keys, write once more and
 * lock again, so RTC_PROT is left as it was found (a lock this code opened is always closed, also after an error).  A readback that
 * is neither the old nor the new time is reported and nothing else is tried.
 */
static inline int
n2rtc_write_time(struct n2rtc_io *io, uint64_t target, bool allow_unlock, struct n2rtc_wres *w)
{
	struct n2rtc_tm t;
	*w = (struct n2rtc_wres){ .first_rc = N2RTC_E_ARGS };
	if (!n2rtc_secs_tm(target, &t)) {
		return N2RTC_E_ARGS;
	}
	if (!n2rtc_wait_idle(io)) {
		w->first_rc = N2RTC_E_BUSY;
		return N2RTC_E_BUSY;
	}
	int rc = n2rtc_write_once(io, &t, target, w);
	w->first_rc = rc;
	if (rc != N2RTC_E_VERIFY || !w->ignored || !allow_unlock) {
		return rc;
	}
	if (n2rtc_wr(io, N2RTC_PROT, N2RTC_PROT_KEY1)) {
		w->unlocked = true;     /* from the first key on: the lock is closed again below whatever happens */
		if (n2rtc_trigger(io) && n2rtc_wr(io, N2RTC_PROT, N2RTC_PROT_KEY2) && n2rtc_trigger(io)) {
			rc = n2rtc_write_once(io, &t, target, w);
		} else {
			rc = N2RTC_E_IO;
		}
	} else {
		rc = N2RTC_E_IO;
	}
	if (w->unlocked) {
		w->relocked = n2rtc_wr(io, N2RTC_PROT, 0) && n2rtc_trigger(io);
		if (!w->relocked && rc == N2RTC_OK) {
			rc = N2RTC_E_IO;
		}
	}
	return rc;
}

/* ---------------------------------------------------------------- sync policy (owner thread, every N2RTC_CHECK_MS) */
#define N2RTC_CHECK_MS          15000ULL         /* calendar vs RTC prediction: no PMIC access while they agree */
#define N2RTC_REFRESH_MS        3600000ULL       /* re-read the RTC at least hourly (32k drift vs the AP timer) */
#define N2RTC_THRESH_S          3                /* both clocks have 1 s granularity here: 3 s never flaps */
#define N2RTC_WRITE_GAP_MS      60000ULL         /* at most one write a minute */
#define N2RTC_MAX_WRITES        64u              /* per boot */
#define N2RTC_MAX_FAILS         3u               /* failed writes in a row -> writes off until the next boot */

enum n2rtc_mode { N2RTC_MODE_OFF = 0, N2RTC_MODE_RW = 1, N2RTC_MODE_RO = 2 };
enum n2rtc_act { N2RTC_ACT_NONE = 0, N2RTC_ACT_READ, N2RTC_ACT_WRITE };
enum n2rtc_why { N2RTC_WHY_NONE = 0, N2RTC_WHY_INVALID, N2RTC_WHY_STEP, N2RTC_WHY_DRIFT };

struct n2rtc_sync {
	uint64_t ref_rtc, ref_ms;       /* the RTC held ref_rtc at monotonic ref_ms (valid when ref_valid) */
	uint64_t last_write_ms;
	int64_t last_diff;              /* calendar - RTC at the last read (s) */
	bool ref_valid, writes_off;
	bool verify_pending;            /* re-read at the next check after a write (a second, independent readback) */
	unsigned why;                   /* enum n2rtc_why of the pending write */
	unsigned reads, read_fails, writes, write_fails, fails_in_row;
};

static inline bool
n2rtc_cal_ok(uint64_t cal)
{
	return cal >= N2RTC_VALID_SECS && cal <= N2RTC_MAX_SECS;
}

/* READ when the RTC must be looked at: no reference, the calendar left the prediction (settimeofday), or the hourly refresh */
static inline int
n2rtc_should_read(const struct n2rtc_sync *s, uint64_t cal, uint64_t now_ms)
{
	if (!n2rtc_cal_ok(cal)) {
		return N2RTC_ACT_NONE;                  /* a 1970 calendar is never compared, never written */
	}
	if (!s->ref_valid || s->verify_pending || now_ms - s->ref_ms >= N2RTC_REFRESH_MS) {
		return N2RTC_ACT_READ;
	}
	int64_t d = n2rtc_sdiff(cal, s->ref_rtc + (now_ms - s->ref_ms) / 1000);
	return (d >= N2RTC_THRESH_S || d <= -N2RTC_THRESH_S) ? N2RTC_ACT_READ : N2RTC_ACT_NONE;
}

static inline int
n2rtc_after_read(struct n2rtc_sync *s, int mode, uint64_t cal, int rc, uint64_t rtc, uint64_t now_ms)
{
	s->reads++;
	s->why = N2RTC_WHY_NONE;
	s->verify_pending = false;
	if (rc == N2RTC_OK) {
		int64_t d = n2rtc_sdiff(cal, rtc);
		s->ref_rtc = rtc;
		s->ref_ms = now_ms;
		s->ref_valid = true;
		s->last_diff = d;
		if (d < N2RTC_THRESH_S && d > -N2RTC_THRESH_S) {
			return N2RTC_ACT_NONE;
		}
		s->why = (d >= 600 || d <= -600) ? N2RTC_WHY_STEP : N2RTC_WHY_DRIFT;
	} else {
		s->ref_valid = false;
		if (rc == N2RTC_E_IO || rc == N2RTC_E_UNSTABLE) {
			s->read_fails++;
			return N2RTC_ACT_NONE;          /* bus trouble: never write on a failed read; try again next check */
		}
		s->why = N2RTC_WHY_INVALID;            /* zero / garbage / before 2025: put the calendar in */
	}
	if (mode != N2RTC_MODE_RW || s->writes_off || !n2rtc_cal_ok(cal) || s->writes >= N2RTC_MAX_WRITES ||
	    (s->last_write_ms != 0 && now_ms - s->last_write_ms < N2RTC_WRITE_GAP_MS)) {
		return N2RTC_ACT_NONE;
	}
	return N2RTC_ACT_WRITE;
}

static inline void
n2rtc_after_write(struct n2rtc_sync *s, int rc, uint64_t target, uint64_t now_ms)
{
	s->last_write_ms = now_ms ? now_ms : 1;
	if (rc == N2RTC_OK) {
		s->writes++;
		s->fails_in_row = 0;
		s->ref_rtc = target;
		s->ref_ms = now_ms;
		s->ref_valid = true;
		s->verify_pending = true;
		return;
	}
	s->write_fails++;
	s->ref_valid = false;
	if (++s->fails_in_row >= N2RTC_MAX_FAILS) {
		s->writes_off = true;
	}
}

/* the second to write for a calendar time: rounded to the nearest second (the RTC has no sub-second register) */
static inline uint64_t
n2rtc_target(uint64_t cal_s, uint32_t cal_us)
{
	return cal_s + (cal_us >= 500000u ? 1 : 0);
}

#endif /* NORD2_RTC_H */
