/*
 * rtc-20261114/test/rtc_test.c: host test of 0540 (b/bsd/dev/nord2_rtc.h + the RTC policy of b/bsd/kern/nord2_clockfloor.h).
 * The functions under test are the ones the kernel runs; the PMIC side is a simulated MT6359 RTC block (0x580..0x5c4) with
 * rtc-mt6358.c semantics: TC registers tick with carries, writes are latched until RTC_WRTGR = 1, CBUSY for a few BBPU polls,
 * RTC_PROT key state machine (0x586a, trigger, 0x9136, trigger = open; any other value + trigger = closed), IRQ_STA read-clear.
 * Every access outside the 0540 allow-list, and any change of PDN1/PDN2/SPAR/alarm/POWERKEY/BBPU, fails the test.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nord2_rtc.h"
#include "nord2_clockfloor.h"

#define NR 0x30                                  /* 0x580 .. 0x5de, 16-bit registers */
#define IX(a) (((a) - N2RTC_BASE) / 2)
#define N2RTC_TC_MTH (N2RTC_TC_SEC + 2 * N2RTC_I_MTH)

struct sim {
	uint16_t reg[NR];
	uint16_t pend[NR];
	bool pend_set[NR];
	int prot_stage;                          /* 0 closed, 1 first key, 2 open */
	unsigned busy, busy_load;                /* BBPU polls with CBUSY still set after a trigger */
	unsigned sec_reads, carry_at;            /* tick once when the carry_at-th TC_SEC read happens (0 = never) */
	unsigned reads, writes, triggers, bad, irq_reads;
	unsigned long udelay_us;
	bool fail_io;
	uint16_t snap[NR];
};

static bool is_tc(uint16_t a) { return a >= N2RTC_TC_SEC && a <= N2RTC_TC_YEA && !(a & 1); }

static void
sim_tick(struct sim *s)
{
	uint16_t r[N2RTC_NREG];
	struct n2rtc_tm t;
	for (unsigned i = 0; i < N2RTC_NREG; i++) r[i] = s->reg[IX(N2RTC_TC_SEC) + i];
	if (n2rtc_regs_decode(r, &t) == N2RTC_E_RANGE) return;      /* a garbage RTC does not count meaningfully */
	uint64_t x = n2rtc_tm_secs(&t) + 1;
	int64_t day = (int64_t)(x / 86400);
	n2rtc_civil_from_days(day, &t.year, &t.mon, &t.mday);
	t.hour = (unsigned)(x % 86400 / 3600); t.min = (unsigned)(x % 3600 / 60); t.sec = (unsigned)(x % 60);
	uint16_t n[N2RTC_NREG];
	n2rtc_regs_encode(&t, r, n);
	if (x % 86400 == 0) n[N2RTC_I_DOW] = (uint16_t)((r[N2RTC_I_DOW] & ~7u) | ((r[N2RTC_I_DOW] & 7u) % 7u + 1u));   /* DOW 1..7 */
	for (unsigned i = 0; i < N2RTC_NREG; i++) s->reg[IX(N2RTC_TC_SEC) + i] = n[i];
}

static bool
sim_rd(void *ctx, uint16_t a, uint16_t *v)
{
	struct sim *s = ctx;
	s->reads++;
	if (s->fail_io) return false;
	if (a == N2RTC_IRQ_STA) { s->irq_reads++; s->reg[IX(a)] = 0; }
	if (!n2rtc_may_read(a)) s->bad++;
	if (a == N2RTC_BBPU) {
		*v = (uint16_t)(s->reg[IX(a)] | (s->busy ? N2RTC_BBPU_CBUSY : 0));
		if (s->busy && s->busy != 0xffffffffu) s->busy--;
		return true;
	}
	if (a == N2RTC_TC_SEC && s->carry_at && ++s->sec_reads == s->carry_at) sim_tick(s);
	*v = s->reg[IX(a)];
	return true;
}

static bool
sim_wr(void *ctx, uint16_t a, uint16_t v)
{
	struct sim *s = ctx;
	s->writes++;
	if (s->fail_io) return false;
	if (!n2rtc_may_write(a, v)) { s->bad++; return true; }
	if (is_tc(a) || a == N2RTC_PROT) { s->pend[IX(a)] = v; s->pend_set[IX(a)] = true; return true; }
	if (a == N2RTC_WRTGR) {
		s->triggers++;
		if (s->pend_set[IX(N2RTC_PROT)]) {
			uint16_t p = s->pend[IX(N2RTC_PROT)];
			s->prot_stage = p == N2RTC_PROT_KEY1 ? 1 : (p == N2RTC_PROT_KEY2 && s->prot_stage == 1) ? 2 : 0;
			s->reg[IX(N2RTC_PROT)] = p;
		}
		for (uint16_t r = N2RTC_TC_SEC; r <= N2RTC_TC_YEA; r += 2)
			if (s->pend_set[IX(r)] && s->prot_stage == 2) s->reg[IX(r)] = s->pend[IX(r)];
		memset(s->pend_set, 0, sizeof(s->pend_set));
		s->busy = s->busy_load;
		return true;
	}
	s->bad++;
	return true;
}

static void sim_ud(void *ctx, unsigned us) { ((struct sim *)ctx)->udelay_us += us; }

static void
sim_init(struct sim *s, bool open, uint64_t t)
{
	memset(s, 0, sizeof(*s));
	s->prot_stage = open ? 2 : 0;
	s->busy_load = 3;
	s->reg[IX(N2RTC_BBPU)] = 0x0001;                         /* PWREN */
	s->reg[IX(N2RTC_PDN1)] = 0x2000;                         /* fast boot (sweet_arm_fastboot) */
	s->reg[IX(N2RTC_PDN2)] = 0x8a13;                         /* power-on time / logo bits */
	s->reg[IX(0x05b8)] = 0x00c5; s->reg[IX(0x05ba)] = 0x7a3b; /* SPAR0 / SPAR1 */
	s->reg[IX(0x05aa)] = 0x5c00; s->reg[IX(0x05a4)] = 0x0100; /* AL_MTH / AL_HOU high bytes: fuel-gauge spare data */
	s->reg[IX(N2RTC_POWERKEY1)] = N2RTC_POWERKEY1_KEY; s->reg[IX(N2RTC_POWERKEY2)] = N2RTC_POWERKEY2_KEY;
	s->reg[IX(0x05ae)] = 0x0f1f;                             /* OSC32CON */
	s->reg[IX(N2RTC_TC_SEC + 2 * N2RTC_I_DOW)] = 5;
	s->reg[IX(N2RTC_TC_MTH)] = 0xa000;                       /* "unknown" upper bits must survive a write */
	if (t) {
		struct n2rtc_tm tm; uint16_t r[N2RTC_NREG], n[N2RTC_NREG];
		assert(n2rtc_secs_tm(t, &tm));
		for (unsigned i = 0; i < N2RTC_NREG; i++) r[i] = s->reg[IX(N2RTC_TC_SEC) + i];
		n2rtc_regs_encode(&tm, r, n);
		for (unsigned i = 0; i < N2RTC_NREG; i++) s->reg[IX(N2RTC_TC_SEC) + i] = n[i];
	}
	memcpy(s->snap, s->reg, sizeof(s->snap));
}

/* everything outside TC (and the RTC_PROT value) is exactly as at sim_init */
static void
sim_check_untouched(const struct sim *s, bool prot_may_change)
{
	for (unsigned i = 0; i < NR; i++) {
		uint16_t a = (uint16_t)(N2RTC_BASE + 2 * i);
		if (is_tc(a) && a != N2RTC_TC_SEC + 2 * N2RTC_I_DOW) continue;
		if (a == N2RTC_PROT && prot_may_change) continue;
		if (a == N2RTC_TC_SEC + 2 * N2RTC_I_DOW) continue;   /* DOW ticks in the model; never written (checked via may_write) */
		if (s->reg[i] != s->snap[i]) { fprintf(stderr, "register %#x changed %#x -> %#x\n", a, s->snap[i], s->reg[i]); abort(); }
	}
	assert(s->bad == 0 && s->irq_reads == 0);
}

static uint64_t
sim_time(struct sim *s)
{
	uint16_t r[N2RTC_NREG]; struct n2rtc_tm t;
	for (unsigned i = 0; i < N2RTC_NREG; i++) r[i] = s->reg[IX(N2RTC_TC_SEC) + i];
	assert(n2rtc_regs_decode(r, &t) == N2RTC_OK);
	return n2rtc_tm_secs(&t);
}

#define IO(s) { sim_rd, sim_wr, sim_ud, (s), 0 }

static uint64_t
ts(int y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned se)
{
	struct n2rtc_tm t = { y, mo, d, h, mi, se };
	return n2rtc_tm_secs(&t);
}

int
main(void)
{
	/* ---- 1. calendar arithmetic against libc (timegm / gmtime_r), the whole valid range */
	assert(N2RTC_VALID_SECS == (uint64_t)ts(2025, 1, 1, 0, 0, 0));
	assert(N2RTC_MAX_SECS == (uint64_t)ts(2096, 1, 1, 0, 0, 0) - 1);
	assert(N2RTC_TRUST_MAX_SECS == N2CK_BUILD_EPOCH + N2CK_MAX_AHEAD);
	assert(ts(1970, 1, 1, 0, 0, 0) == 0 && ts(2026, 11, 13, 0, 0, 0) == N2CK_BUILD_EPOCH);
	srand(540);
	for (unsigned k = 0; k < 2000000; k++) {
		uint64_t x = N2RTC_VALID_SECS + ((uint64_t)rand() * 65536u + (uint64_t)rand()) % (N2RTC_MAX_SECS - N2RTC_VALID_SECS + 1);
		if (k < 1000) x = k & 1 ? N2RTC_MAX_SECS - k : N2RTC_VALID_SECS + k;
		struct n2rtc_tm t; struct tm g; time_t tt = (time_t)x;
		assert(n2rtc_secs_tm(x, &t));
		gmtime_r(&tt, &g);
		assert(t.year == g.tm_year + 1900 && t.mon == (unsigned)g.tm_mon + 1 && t.mday == (unsigned)g.tm_mday &&
		       t.hour == (unsigned)g.tm_hour && t.min == (unsigned)g.tm_min && t.sec == (unsigned)g.tm_sec);
		assert(n2rtc_tm_secs(&t) == x && (uint64_t)timegm(&g) == x);
		/* registers round trip, year offset 1968 */
		uint16_t old[N2RTC_NREG] = { 0xffc0, 0xffc0, 0xffe0, 0xffe0, 0x0003, 0xfff0, 0xff80 }, r[N2RTC_NREG];
		struct n2rtc_tm back;
		n2rtc_regs_encode(&t, old, r);
		assert((r[N2RTC_I_YEA] & 0x7f) == (unsigned)(t.year - 1968) && (r[N2RTC_I_MTH] & 0xf) == t.mon);
		assert(r[N2RTC_I_DOW] == 0x0003);
		for (unsigned i = 0; i < N2RTC_NREG; i++) assert((r[i] & ~n2rtc_field_mask[i]) == (old[i] & ~n2rtc_field_mask[i]));
		assert(n2rtc_regs_decode(r, &back) == N2RTC_OK && n2rtc_tm_secs(&back) == x);
	}
	struct n2rtc_tm t0;
	assert(!n2rtc_secs_tm(N2RTC_VALID_SECS - 1, &t0) && !n2rtc_secs_tm(N2RTC_MAX_SECS + 1, &t0) && !n2rtc_secs_tm(0, &t0));
	/* a known vector: 2026-11-13 12:34:56 UTC -> hw year 58, month 11 */
	{
		uint16_t z[N2RTC_NREG] = { 0 }, r[N2RTC_NREG];
		assert(n2rtc_secs_tm(N2CK_BUILD_EPOCH + 12 * 3600 + 34 * 60 + 56, &t0));
		n2rtc_regs_encode(&t0, z, r);
		assert(r[0] == 56 && r[1] == 34 && r[2] == 12 && r[3] == 13 && r[4] == 0 && r[5] == 11 && r[6] == 58);
	}
	/* ---- 2. decode validation: zero registers, preloader defaults, garbage, leap days */
	{
		uint16_t z[N2RTC_NREG] = { 0 };
		struct n2rtc_tm t;
		assert(n2rtc_regs_decode(z, &t) == N2RTC_E_RANGE);                         /* month 0, day 0 */
		uint16_t y1968[N2RTC_NREG] = { 0, 0, 0, 1, 1, 1, 0 };                       /* 1968-01-01: never set */
		assert(n2rtc_regs_decode(y1968, &t) == N2RTC_E_OLD);
		uint16_t y2010[N2RTC_NREG] = { 0, 0, 0, 1, 1, 1, 42 };                      /* 2010-01-01 (a typical MTK default) */
		assert(n2rtc_regs_decode(y2010, &t) == N2RTC_E_OLD && t.year == 2010);
		uint16_t y2024[N2RTC_NREG] = { 59, 59, 23, 31, 1, 12, 56 };
		assert(n2rtc_regs_decode(y2024, &t) == N2RTC_E_OLD);
		uint16_t y2025[N2RTC_NREG] = { 0, 0, 0, 1, 1, 1, 57 };
		assert(n2rtc_regs_decode(y2025, &t) == N2RTC_OK && n2rtc_tm_secs(&t) == N2RTC_VALID_SECS);
		uint16_t feb29[N2RTC_NREG] = { 0, 0, 0, 29, 1, 2, 60 };                     /* 2028-02-29 */
		assert(n2rtc_regs_decode(feb29, &t) == N2RTC_OK);
		feb29[6] = 59;                                                              /* 2027-02-29 */
		assert(n2rtc_regs_decode(feb29, &t) == N2RTC_E_RANGE);
		uint16_t junk[][N2RTC_NREG] = { { 60, 0, 0, 1, 1, 1, 58 }, { 0, 60, 0, 1, 1, 1, 58 }, { 0, 0, 24, 1, 1, 1, 58 }, { 0, 0, 0, 31, 1, 4, 58 },
		                                { 0, 0, 0, 1, 1, 13, 58 }, { 0x3f, 0x3f, 0x1f, 0x1f, 7, 0xf, 0x7f }, { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff } };
		for (unsigned i = 0; i < sizeof(junk) / sizeof(junk[0]); i++) assert(n2rtc_regs_decode(junk[i], &t) == N2RTC_E_RANGE);
		uint16_t y2095[N2RTC_NREG] = { 59, 59, 23, 31, 1, 12, 127 };
		assert(n2rtc_regs_decode(y2095, &t) == N2RTC_OK && n2rtc_tm_secs(&t) == N2RTC_MAX_SECS);
	}
	/* ---- 3. allow-list */
	for (unsigned a = 0x500; a < 0x700; a++) {
		bool tc = is_tc((uint16_t)a);
		bool r_ok = n2rtc_may_read((uint16_t)a);
		assert(r_ok == (tc || a == N2RTC_BBPU || a == N2RTC_POWERKEY1 || a == N2RTC_POWERKEY2 || a == N2RTC_PDN1 || a == N2RTC_PDN2 ||
		                a == N2RTC_PROT));
		for (unsigned v = 0; v < 0x10000; v += 0x1111) {
			bool w_ok = n2rtc_may_write((uint16_t)a, (uint16_t)v);
			assert(!w_ok || (tc && a != 0x059a) || (a == N2RTC_WRTGR && v == 1) || a == N2RTC_PROT);
		}
	}
	assert(!n2rtc_may_read(N2RTC_IRQ_STA) && !n2rtc_may_write(N2RTC_BBPU, 0x4301) && !n2rtc_may_write(N2RTC_PDN1, 0x2000) &&
	       !n2rtc_may_write(N2RTC_PROT, 0x1234) && !n2rtc_may_write(N2RTC_WRTGR, 0) && n2rtc_may_write(N2RTC_PROT, 0) &&
	       !n2rtc_may_write(0x059a, 3) && !n2rtc_may_write(N2RTC_AL_SEC, 0) && !n2rtc_may_write(0x05ae, 0));
	{
		struct sim s; struct n2rtc_io io = IO(&s); uint16_t v;
		sim_init(&s, true, 0);
		assert(!n2rtc_rd(&io, N2RTC_IRQ_STA, &v) && !n2rtc_wr(&io, N2RTC_BBPU, 0x4300) && io.refused == 2 && s.reads == 0 && s.writes == 0);
	}
	/* ---- 4. reads: plain, with a carry in the middle, failing bus */
	{
		struct sim s; struct n2rtc_io io = IO(&s); uint64_t x = 0; uint16_t raw[N2RTC_NREG];
		uint64_t T = ts(2026, 12, 31, 23, 59, 59);
		sim_init(&s, true, T);
		assert(n2rtc_read_time(&io, &x, raw) == N2RTC_OK && x == T && s.writes == 0);
		s.carry_at = 2;                         /* tick (to 2027-01-01 00:00:00) after the SEC read: YEA etc. read after the carry, the
		                                           re-read SEC (0) < 59 -> the driver reads again */
		s.sec_reads = 0;
		assert(n2rtc_read_time(&io, &x, raw) == N2RTC_OK && x == T + 1 && s.writes == 0);
		sim_check_untouched(&s, false);
		s.fail_io = true;
		assert(n2rtc_read_time(&io, &x, raw) == N2RTC_E_IO);
		/* invalid RTC: zeros */
		sim_init(&s, true, 0);
		assert(n2rtc_read_time(&io, &x, raw) == N2RTC_E_RANGE);
	}
	/* ---- 5. write, interface open (the hardware case: AUTOFB writes PDN1 without unlock): one write, no RTC_PROT access */
	{
		struct sim s; struct n2rtc_io io = IO(&s); struct n2rtc_wres w;
		sim_init(&s, true, 0);
		s.reg[IX(N2RTC_TC_SEC) + 6] = 42; s.reg[IX(N2RTC_TC_SEC) + 5] |= 1; s.reg[IX(N2RTC_TC_SEC) + 3] = 1;   /* 2010-01-01 */
		memcpy(s.snap, s.reg, sizeof(s.snap));
		uint64_t T = ts(2026, 11, 14, 8, 30, 15);
		assert(n2rtc_write_time(&io, T, true, &w) == N2RTC_OK);
		assert(sim_time(&s) == T && w.attempts == 1 && !w.unlocked && !w.relocked && w.readback == T);
		assert((s.reg[IX(N2RTC_TC_MTH)] & 0xfff0) == 0xa000);        /* bits outside the month field kept */
		assert(s.reg[IX(N2RTC_PROT)] == s.snap[IX(N2RTC_PROT)] && s.prot_stage == 2 && s.triggers == 1);
		sim_check_untouched(&s, false);
		/* invalid targets are refused before any access */
		unsigned wr0 = s.writes;
		assert(n2rtc_write_time(&io, 25, true, &w) == N2RTC_E_ARGS && n2rtc_write_time(&io, N2RTC_MAX_SECS + 1, true, &w) == N2RTC_E_ARGS &&
		       s.writes == wr0);
	}
	/* ---- 6. write, interface closed: the plain write is ignored -> unlock, write, lock again (left as found) */
	{
		struct sim s; struct n2rtc_io io = IO(&s); struct n2rtc_wres w;
		uint64_t old = ts(2026, 11, 1, 10, 0, 0), T = ts(2026, 11, 14, 9, 0, 0);
		sim_init(&s, false, old);
		assert(n2rtc_write_time(&io, T, true, &w) == N2RTC_OK);
		assert(w.first_rc == N2RTC_E_VERIFY && w.ignored && w.unlocked && w.relocked && w.attempts == 2 && sim_time(&s) == T);
		assert(s.prot_stage == 0 && s.reg[IX(N2RTC_PROT)] == 0);    /* closed again */
		sim_check_untouched(&s, true);
		/* same, unlock not allowed (read-only policy never calls write; RW with allow_unlock false): nothing opened */
		sim_init(&s, false, old);
		assert(n2rtc_write_time(&io, T, false, &w) == N2RTC_E_VERIFY && w.ignored && !w.unlocked && sim_time(&s) == old);
		assert(s.prot_stage == 0);
		sim_check_untouched(&s, false);
		/* closed + garbage registers: ignored is recognised from the unchanged fields */
		sim_init(&s, false, 0);
		assert(n2rtc_write_time(&io, T, true, &w) == N2RTC_OK && w.unlocked && w.relocked && s.prot_stage == 0 && sim_time(&s) == T);
		sim_check_untouched(&s, true);
	}
	/* ---- 7. CBUSY stuck: nothing beyond the bounded wait; an opened lock is closed even then */
	{
		struct sim s; struct n2rtc_io io = IO(&s); struct n2rtc_wres w;
		uint64_t T = ts(2026, 11, 14, 9, 0, 0);
		sim_init(&s, true, ts(2026, 1, 1, 0, 0, 0));
		s.busy = 0xffffffffu;
		assert(n2rtc_write_time(&io, T, true, &w) == N2RTC_E_BUSY && s.writes == 0 && s.udelay_us >= N2RTC_BUSY_US);
		/* closed interface; CBUSY never clears after the first RTC_PROT key: error, and the relock is still issued */
		sim_init(&s, false, ts(2026, 1, 1, 0, 0, 0));
		s.busy_load = 0;
		struct n2rtc_wres w2;
		assert(n2rtc_write_time(&io, T, false, &w2) == N2RTC_E_VERIFY);        /* plain write ignored, no unlock allowed */
		unsigned wr_before = s.writes;
		s.busy_load = 0xffffffffu;                                              /* from now on every trigger hangs */
		s.busy = 0;
		assert(n2rtc_write_time(&io, T, true, &w) == N2RTC_E_BUSY);              /* the plain write's own trigger hangs: nothing more */
		assert(!w.unlocked && s.writes == wr_before + 7);
		sim_check_untouched(&s, false);
	}
	/* ---- 8. readback neither old nor new (an unknown fault): no unlock attempt */
	{
		struct sim s; struct n2rtc_io io = IO(&s); struct n2rtc_wres w;
		uint16_t old[N2RTC_NREG] = { 10, 10, 10, 10, 1, 10, 58 }, neu[N2RTC_NREG] = { 20, 20, 20, 20, 1, 11, 58 }, rb[N2RTC_NREG] = { 5, 5, 5, 5, 1, 5, 40 };
		assert(!n2rtc_write_ignored(old, neu, rb));
		assert(n2rtc_write_ignored(old, neu, old));
		rb[0] = 12; memcpy(rb + 1, old + 1, 6 * sizeof(uint16_t));
		assert(n2rtc_write_ignored(old, neu, rb));                  /* two seconds ticked: still the old time */
		(void)s; (void)io; (void)w;
	}
	/* ---- 9. sync policy */
	{
		struct n2rtc_sync s = { 0 };
		uint64_t T = ts(2026, 11, 14, 10, 0, 0), ms = 100000;
		/* a 1970 / floorless calendar is never compared or written */
		assert(n2rtc_should_read(&s, 34, ms) == N2RTC_ACT_NONE);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, 34, N2RTC_E_RANGE, 0, ms) == N2RTC_ACT_NONE);
		/* no reference yet -> read; invalid RTC -> write the calendar (the floor at the first boot of this kernel) */
		assert(n2rtc_should_read(&s, T, ms) == N2RTC_ACT_READ);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, T, N2RTC_E_RANGE, 0, ms) == N2RTC_ACT_WRITE && s.why == N2RTC_WHY_INVALID);
		n2rtc_after_write(&s, N2RTC_OK, T, ms);
		/* the second, independent readback at the next check */
		assert(n2rtc_should_read(&s, T + 15, ms + 15000) == N2RTC_ACT_READ);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, T + 15, N2RTC_OK, T + 15, ms + 15000) == N2RTC_ACT_NONE);
		/* agreeing clocks: no PMIC access for an hour */
		for (uint64_t k = 1; k < 240; k++) assert(n2rtc_should_read(&s, T + 15 + 15 * k, ms + 15000 + 15000 * k) == N2RTC_ACT_NONE);
		assert(n2rtc_should_read(&s, T + 15 + 3600, ms + 15000 + 3600000) == N2RTC_ACT_READ);
		/* 2 s of drift found by the hourly read: no write; 3 s: write (gap respected) */
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, T + 3617, N2RTC_OK, T + 3615, ms + 3615000) == N2RTC_ACT_NONE);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, T + 3618, N2RTC_OK, T + 3615, ms + 3615000) == N2RTC_ACT_WRITE && s.why == N2RTC_WHY_DRIFT);
		n2rtc_after_write(&s, N2RTC_OK, T + 3618, ms + 3615000);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, T + 3618 + 30, N2RTC_OK, T + 3618 + 30, ms + 3645000) == N2RTC_ACT_NONE);
		/* NTP: timed steps the calendar by 3 days -> the prediction breaks at the next check -> read -> write, why = step */
		uint64_t now = ms + 3700000, cal = T + 3618 + 85 + 3 * 86400, rtc = T + 3618 + 85;
		assert(n2rtc_should_read(&s, cal, now) == N2RTC_ACT_READ);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, cal, N2RTC_OK, rtc, now) == N2RTC_ACT_WRITE && s.why == N2RTC_WHY_STEP);
		/* read only: never a write */
		struct n2rtc_sync r = s;
		assert(n2rtc_after_read(&r, N2RTC_MODE_RO, cal, N2RTC_OK, rtc, now) == N2RTC_ACT_NONE);
		assert(n2rtc_after_read(&r, N2RTC_MODE_RO, cal, N2RTC_E_RANGE, 0, now) == N2RTC_ACT_NONE);
		/* a failed read is never followed by a write */
		assert(n2rtc_after_read(&r, N2RTC_MODE_RW, cal, N2RTC_E_IO, 0, now + 120000) == N2RTC_ACT_NONE && r.read_fails == 1);
		assert(n2rtc_after_read(&r, N2RTC_MODE_RW, cal, N2RTC_E_UNSTABLE, 0, now + 120000) == N2RTC_ACT_NONE);
		/* write gap: a second write within 60 s waits; three failures in a row switch writes off for the boot */
		n2rtc_after_write(&s, N2RTC_E_VERIFY, cal, now);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, cal + 15, N2RTC_OK, rtc + 15, now + 15000) == N2RTC_ACT_NONE);
		assert(n2rtc_after_read(&s, N2RTC_MODE_RW, cal + 61, N2RTC_OK, rtc + 61, now + 61000) == N2RTC_ACT_WRITE);
		n2rtc_after_write(&s, N2RTC_E_VERIFY, cal, now + 61000);
		n2rtc_after_write(&s, N2RTC_E_BUSY, cal, now + 122000);
		assert(s.writes_off && n2rtc_after_read(&s, N2RTC_MODE_RW, cal + 400, N2RTC_OK, rtc + 400, now + 400000) == N2RTC_ACT_NONE);
		/* per-boot cap */
		struct n2rtc_sync c = { 0 };
		c.writes = N2RTC_MAX_WRITES;
		assert(n2rtc_after_read(&c, N2RTC_MODE_RW, T, N2RTC_E_RANGE, 0, ms) == N2RTC_ACT_NONE);
		assert(n2rtc_target(T, 499999) == T && n2rtc_target(T, 500000) == T + 1);
	}
	/* ---- 10. boot choice (0530 floor + 0540 RTC): newest wins, never backwards, corrupt future ignored */
	{
		unsigned src = 99;
		uint64_t B = N2CK_BUILD_EPOCH, now = 40;
		assert(n2ck_pick_rtc(now, B, N2CK_SRC_BUILD, 1, B + 86400 * 5, &src) == B + 86400 * 5 && src == N2CK_SRC_RTC);
		assert(n2ck_pick_rtc(now, B + 86400 * 7, N2CK_SRC_CAND, 1, B + 86400 * 5, &src) == B + 86400 * 7 && src == N2CK_SRC_CAND);
		assert(n2ck_pick_rtc(now, B, N2CK_SRC_BUILD, 0, 0, &src) == B && src == N2CK_SRC_BUILD);           /* invalid RTC: the floor */
		assert(n2ck_pick_rtc(now, 0, N2CK_SRC_NONE, 1, B + 99, &src) == B + 99 && src == N2CK_SRC_RTC);    /* n2ck=0: RTC alone */
		assert(n2ck_pick_rtc(now, 0, N2CK_SRC_NONE, 0, 0, &src) == now && src == N2CK_SRC_NONE);           /* both off: 1970 as before */
		assert(n2ck_pick_rtc(now, B, N2CK_SRC_BUILD, 1, B + N2CK_MAX_AHEAD + 1, &src) == B && src == N2CK_SRC_BUILD);
		assert(n2ck_pick_rtc(B + 500, B, N2CK_SRC_BUILD, 1, B + 100, &src) == B + 500 && src == N2CK_SRC_NONE);
	}
	/* ---- 11. end to end over three boots on one simulated PMIC (interface open, as on the phone) */
	{
		struct sim s; struct n2rtc_io io = IO(&s); struct n2rtc_wres w; struct n2rtc_sync y = { 0 }; unsigned src;
		uint64_t x = 0, truth = ts(2026, 11, 14, 7, 0, 0);
		sim_init(&s, true, 0);                                       /* boot 1: RTC never set (zeros) */
		int rc = n2rtc_read_time(&io, &x, NULL);
		assert(rc == N2RTC_E_RANGE);
		uint64_t floor = N2CK_BUILD_EPOCH + 3600, cal = n2ck_pick_rtc(40, floor, N2CK_SRC_CAND, rc == N2RTC_OK, x, &src);
		assert(cal == floor && src == N2CK_SRC_CAND);
		assert(n2rtc_should_read(&y, cal + 15, 55000) == N2RTC_ACT_READ);
		assert(n2rtc_after_read(&y, N2RTC_MODE_RW, cal + 15, n2rtc_read_time(&io, &x, NULL), x, 55000) == N2RTC_ACT_WRITE);
		assert(n2rtc_write_time(&io, cal + 15, true, &w) == N2RTC_OK && sim_time(&s) == cal + 15);  /* the floor goes into the RTC */
		n2rtc_after_write(&y, N2RTC_OK, cal + 15, 55000);
		/* NTP at uptime 250 s: calendar -> truth */
		assert(n2rtc_should_read(&y, truth, 250000) == N2RTC_ACT_READ);
		assert(n2rtc_after_read(&y, N2RTC_MODE_RW, truth, n2rtc_read_time(&io, &x, NULL), x, 250000) == N2RTC_ACT_WRITE);
		assert(n2rtc_write_time(&io, truth, true, &w) == N2RTC_OK);
		n2rtc_after_write(&y, N2RTC_OK, truth, 250000);
		for (unsigned k = 0; k < 600; k++) sim_tick(&s);            /* 10 minutes, then power off / reboot (RTC keeps counting) */
		/* boot 2: the RTC is the time of day, no NTP needed, the floor loses */
		assert(n2rtc_read_time(&io, &x, NULL) == N2RTC_OK && x == truth + 600);
		assert(n2ck_pick_rtc(40, floor, N2CK_SRC_CAND, 1, x, &src) == truth + 600 && src == N2CK_SRC_RTC);
		struct n2rtc_sync y2 = { 0 };
		assert(n2rtc_after_read(&y2, N2RTC_MODE_RW, truth + 615, N2RTC_OK, truth + 615, 55000) == N2RTC_ACT_NONE);   /* no write */
		sim_check_untouched(&s, false);
		assert(s.prot_stage == 2);
	}
	/* ---- 12. dasd (heat-20261114): with a correct boot clock, a stored 1970 lastModulation still means ~2 M slots once -> heat1 */
	{
		long long last = 34, now = (long long)ts(2026, 11, 14, 0, 0, 0);
		long long n = (now - last) / 900;
		assert(n > 1990000 && n < 2000000);
		printf("dasd slots from a stored 1970-01-01 00:00:34 to 2026-11-14 with a correct clock: %lld (heat1 removes that date)\n", n);
	}
	printf("RTC_TESTS_OK\n");
	return 0;
}
