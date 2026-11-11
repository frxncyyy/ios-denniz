/* Host test for the worker side of /dev/redwood-mic (audio_mic.inc, patch 0321): the open/read/close device and the
 * NM_* state machine against fakes of what audio_device.c and audio_call.c give it (nau_hw start/stop/feed, the async
 * MT6359 tables, the capture-only AFE, VUL12 samples).  Checks: the speaker path started silent for a recording and
 * stopped after it, a playing path reused and left running, sound while silent -> restart with the amplifiers, a call
 * takes the MT6359 only after the recording gave it back, repeated failures park the device until every reader closes,
 * nonblocking reads never wait. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

/* ---- what the kernel gives (host versions) ---- */
#define __unused __attribute__((unused))
#define LCK_MTX_DECLARE(n, g) int n __attribute__((unused))
static int lock_depth;
#define lck_mtx_lock(x) (assert(lock_depth == 0), lock_depth++)
#define lck_mtx_unlock(x) (assert(lock_depth == 1), lock_depth--)
#define SYSCTL_NODE(...)
#define SYSCTL_UINT(...)
#define SYSCTL_INT(...)
#define SYSCTL_QUAD(...)
#define IO_NDELAY 0x4
#define FREAD 1
#define FWRITE 2
#define PZERO 22
#define PCATCH 0x100
#define DEVFS_CHAR 1
#define UID_ROOT 0
#define GID_WHEEL 0
typedef int64_t user_ssize_t;
typedef char *caddr_t;
struct proc;
struct uio { char *p; user_ssize_t resid; };
static user_ssize_t uio_resid(struct uio *u) { return u->resid; }
static int uiomove(caddr_t src, int n, struct uio *u) { assert(lock_depth == 0); memcpy(u->p, src, (size_t)n); u->p += n; u->resid -= n; return 0; }
static int sleeps, wakeups;
static int msleep(void *chan, int *mtx, int pri, const char *w, struct timespec *ts)
{ (void)chan; (void)mtx; (void)pri; (void)w; (void)ts; assert(lock_depth == 1); sleeps++; return EWOULDBLOCK; }
static void wakeup(void *chan) { (void)chan; wakeups++; }
typedef int (*d_fn)();
struct cdevsw { void *d_open, *d_close, *d_read, *d_write, *d_ioctl, *d_stop, *d_reset, *d_select, *d_mmap, *d_strategy; int d_type; };
static int eno_rdwrt(void) { return ENODEV; }
static int eno_ioctl(void) { return ENODEV; }
static int eno_stop(void) { return ENODEV; }
static int eno_reset(void) { return ENODEV; }
static int eno_select(void) { return ENODEV; }
static int eno_mmap(void) { return ENODEV; }
static void eno_strat(void) {}
static int cdevsw_add(int i, const struct cdevsw *c) { (void)i; (void)c; return 42; }
static int cdevsw_remove(int i, const struct cdevsw *c) { (void)i; (void)c; return 0; }
static void *devfs_make_node(int dev, int t, int u, int g, int m, const char *n) { (void)dev; (void)t; (void)u; (void)g; (void)m; assert(!strcmp(n, "redwood-mic")); return (void *)1; }
static int makedev(int a, int b) { return a << 24 | b; }

/* ---- what audio_device.c gives ---- */
#define NSA_CHUNK 256u
#define NAH_QUEUE_FRAMES 8192u
enum { NC_IDLE = 0, NC_ACTIVE = 5, NC_FAILED = 9 };
static unsigned call_phase, call_wanted;
static struct { unsigned ready, starts, stop; } device = { .ready = 1 };
static struct { struct { unsigned initialized, running, silent_only, uncertain, queued_frames; int error, stop_error; } status; } hardware;
static unsigned output_gain[2] = { 256, 5793 };
static int nsa_group __attribute__((unused)), nsa_lock __attribute__((unused));
static struct { int16_t q[2 * 4096]; unsigned n; } stream;
static uint64_t now;
static unsigned hw_starts_silent, hw_starts_loud, hw_stops, hw_fed, notified;
static uint64_t now_us(void) { return now; }
static void notify_worker(void) { notified++; }
static unsigned nac_wanted(void) { return call_wanted; }
static int nau_hw_start_channels(void *h, unsigned r, unsigned c, unsigned b, unsigned silent, unsigned t, unsigned bo, unsigned d, uint64_t n)
{ (void)h; (void)r; (void)c; (void)b; (void)t; (void)bo; (void)d; (void)n; assert(!hardware.status.running); hardware.status.running = 1; hardware.status.silent_only = silent; silent ? hw_starts_silent++ : hw_starts_loud++; return 0; }
static int nau_hw_stop(void *h) { (void)h; hardware.status.running = 0; hw_stops++; return 0; }
static int nau_hw_feed(void *h, const int16_t *p, unsigned n) { (void)h; (void)p; assert(hardware.status.running); hw_fed += n; return (int)n; }
static int start_fail;
static int start(unsigned silent, unsigned dur) { (void)dur; if (start_fail) { hardware.status.error = start_fail; return start_fail; } return nau_hw_start_channels(0, 48000, 2, 16, silent, 0, 0, 0, now); }
static unsigned nsa_dequeue(void *s, int16_t *out, unsigned cap) { (void)s; unsigned n = stream.n < cap ? stream.n : cap; memcpy(out, stream.q, n * 4); memmove(stream.q, stream.q + 2 * n, (stream.n - n) * 4); stream.n -= n; return n; }
#define nsa_dequeue(s, o, c) nsa_dequeue((void *)(s), o, c)
#define nau_hw_start_channels(h, ...) nau_hw_start_channels((void *)(h), __VA_ARGS__)
#define nau_hw_stop(h) nau_hw_stop((void *)(h))
#define nau_hw_feed(h, p, n) nau_hw_feed((void *)(h), p, n)

/* ---- what audio_call.c (0320) gives ---- */
static int pm_pending = -1, pm_up, pm_fail, pm_busy, afe_live, afe_ups, ticks, last_phase;
static int nac_mic_pmic_submit(unsigned up) { if (pm_busy) return -1; assert(pm_pending < 0); pm_pending = (int)up; return 0; }
static int nac_mic_pmic_poll(int *e) { if (pm_pending < 0) return 0; *e = pm_pending && pm_fail ? -104 : 0; if (!*e) pm_up = pm_pending; pm_pending = -1; return 1; }
static int nac_mic_afe_up(void *h) { (void)h; assert(pm_up && hardware.status.running && !afe_live); afe_live = 1; afe_ups++; return 0; }
static int nac_afe_down(void *h) { (void)h; afe_live = 0; return 0; }
static unsigned vul_level = 1000;
static unsigned nac_mic_take(void *h, int16_t *m, unsigned max) { (void)h; assert(afe_live && hardware.status.running); unsigned n = max < 96 ? max : 96; for (unsigned i = 0; i < n; i++) m[i] = (int16_t)(i & 1 ? vul_level : -vul_level); return n; }
static void nac_tick(void *h) { (void)h; ticks++; }
static void nac_note(int phase, int rc) { (void)rc; last_phase = phase; }
#define nac_mic_afe_up(h) nac_mic_afe_up((void *)(h))
#define nac_afe_down(h) nac_afe_down((void *)(h))
#define nac_mic_take(h, m, n) nac_mic_take((void *)(h), m, n)
#define nac_tick(h) nac_tick((void *)(h))

#include "../src/bsd/dev/audio_mic.inc"

/* one worker pass as audio_device.c's loop does it (call_poll, mic_poll, the feed) */
static void
pass(void)
{
	mic_poll();
	if (hardware.status.running && mic_holds_hw()) mic_feed();
	now += 2000;
}
static int
rd(int16_t *buf, unsigned samples, int flags, unsigned *got)
{
	struct uio u = { (char *)buf, (user_ssize_t)samples * 2 };
	int e = mic_read(0, &u, flags);
	*got = (unsigned)(samples - u.resid / 2);
	return e;
}

int
main(void)
{
	int16_t buf[4096];
	unsigned got;
	hardware.status.initialized = 1;
	mic_initialize();

	/* 1. no reader: nothing happens */
	for (int i = 0; i < 10; i++) pass();
	assert(nmic_state == NM_IDLE && pm_pending < 0 && !hardware.status.running);
	assert(mic_open(0, FWRITE, 0, 0) == EPERM);

	/* 2. a recording without playback: MT6359 up (async), the speaker path silent, AFE up, samples flow */
	assert(mic_open(0, FREAD, 0, 0) == 0 && nmic_users == 1);
	pass(); assert(nmic_state == NM_UP_WAIT && pm_pending == 1 && last_phase == 8 && mic_blocks_call());
	assert(rd(buf, 1024, IO_NDELAY, &got) == EWOULDBLOCK && got == 0 && sleeps == 0);   /* never waits */
	pass(); assert(nmic_state == NM_ACTIVE && hw_starts_silent == 1 && afe_live && last_phase == 7 && nmic_sessions == 1);
	assert(rd(buf, 1024, IO_NDELAY, &got) == EWOULDBLOCK);                 /* priming (2048) */
	for (int i = 0; i < 22; i++) pass();                                    /* 22 x 96 = 2112 */
	assert(rd(buf, 1024, IO_NDELAY, &got) == 0 && got == 1024 && buf[0] == -1000 && buf[1] == 1000);
	assert(ticks >= 22);
	/* zeros keep the silent path's 750 ms watchdog fed */
	for (int i = 0; i < 500; i++) { pass(); (void)rd(buf, 96, IO_NDELAY, &got); }
	assert(hw_fed >= 9 * NSA_CHUNK && hardware.status.running && hw_stops == 0);
	/* digital gain */
	nmic_gain_q8 = 512; for (int i = 0; i < 40; i++) pass();
	while (rd(buf, 96, IO_NDELAY, &got) == 0 && got) { if (buf[0] == -2000) break; }
	assert(buf[0] == -2000); nmic_gain_q8 = 256;

	/* 3. zeros from the HAL keep it silent; sound restarts the path with the amplifiers, AFE up again */
	memset(stream.q, 0, sizeof stream.q); stream.n = 256; pass();
	assert(hardware.status.silent_only && hw_starts_loud == 0);
	stream.q[100] = 500; stream.n = 256; pass();                          /* default: not played, the recording safe */
	assert(hardware.status.silent_only && hw_starts_loud == 0 && hw_stops == 0 && stream.n == 0);
	nmic_upgrade = 1;
	stream.q[100] = 500; stream.n = 256; pass();
	assert(!hardware.status.silent_only && hw_starts_loud == 1 && hw_stops == 1 && afe_live && afe_ups == 2 && nmic_upgrades == 1);
	for (int i = 0; i < 50; i++) pass();
	assert(nmic_state == NM_ACTIVE);

	/* 4. a call: the recording gives the MT6359 back first; the playing path stays (it was not ours) */
	call_wanted = 1;
	assert(mic_blocks_call());
	pass(); assert(nmic_state == NM_DOWN_WAIT && !afe_live && hardware.status.running && hw_stops == 1 && mic_blocks_call());
	pass(); assert(nmic_state == NM_IDLE && !pm_up && !mic_blocks_call() && last_phase == 0);
	call_phase = NC_ACTIVE;
	for (int i = 0; i < 10; i++) pass();
	assert(nmic_state == NM_IDLE);                                          /* not during the call */
	call_wanted = 0; call_phase = NC_IDLE; hardware.status.running = 0;     /* the call ended and stopped the path */

	/* 5. the recording resumes by itself (the reader is still open), the path silent again */
	pass(); pass(); assert(nmic_state == NM_ACTIVE && hw_starts_silent == 2);
	/* the reader closes: AFE down, the silent path stopped, MT6359 down */
	assert(mic_close(0, 0, 0, 0) == 0 && nmic_users == 0);
	pass(); assert(nmic_state == NM_DOWN_WAIT && !hardware.status.running && hw_stops == 2);
	pass(); assert(nmic_state == NM_IDLE && !pm_up && nmic_last.in > 0 && nmic_last.silent == 1);

	/* 6. a recording while music plays: the playing path is used as it is and left running */
	hardware.status.running = 1; hardware.status.silent_only = 0;
	assert(mic_open(0, FREAD, 0, 0) == 0);
	pass(); pass(); assert(nmic_state == NM_ACTIVE && hw_starts_silent == 2 && hw_starts_loud == 1);
	for (int i = 0; i < 30; i++) pass();
	assert(mic_close(0, 0, 0, 0) == 0);
	pass(); pass(); assert(nmic_state == NM_IDLE && hardware.status.running && hw_stops == 2);
	hardware.status.running = 0;

	/* 7. the hardware stops under the recording (an error): torn down, not restarted on a broken path */
	assert(mic_open(0, FREAD, 0, 0) == 0);
	pass(); pass(); assert(nmic_state == NM_ACTIVE);
	hardware.status.running = 0; hardware.status.error = -10;
	pass(); assert(nmic_state == NM_DOWN_WAIT); pass(); assert(nmic_state == NM_IDLE);
	pass(); assert(nmic_state == NM_FAILED && pm_pending < 0);
	assert(mic_close(0, 0, 0, 0) == 0); pass(); assert(nmic_state == NM_IDLE);
	hardware.status.error = 0;

	/* 8. MT6359 refuses three times: parked until every reader closes; a busy slot is retried */
	pm_fail = 1; nmic_rc = 0;
	assert(mic_open(0, FREAD, 0, 0) == 0);
	for (int k = 0; k < 3; k++) {
		pass(); assert(nmic_state == NM_UP_WAIT);
		pm_busy = 1; pass(); assert(nmic_state == NM_DOWN_SUBMIT);
		pass(); assert(nmic_state == NM_DOWN_SUBMIT); pm_busy = 0;
		pass(); assert(nmic_state == NM_DOWN_WAIT);
		pass();
	}
	assert(nmic_state == NM_FAILED && !hardware.status.running && nmic_rc == -104);
	for (int i = 0; i < 5; i++) pass();
	assert(nmic_state == NM_FAILED);
	assert(mic_close(0, 0, 0, 0) == 0); pass(); assert(nmic_state == NM_IDLE);
	pm_fail = 0;

	/* 9. a blocking reader (dd): waits for the first samples, at most ~1 s, then returns what came */
	assert(mic_open(0, FREAD, 0, 0) == 0);
	pass(); pass();
	sleeps = 0; assert(rd(buf, 512, 0, &got) == 0 && got == 0 && sleeps == 20);   /* nothing came (the fake does not run the worker) */
	for (int i = 0; i < 30; i++) pass();
	assert(rd(buf, 4096, 0, &got) == 0 && got == 31 * 96 && nmic.count == 0);
	assert(mic_close(0, 0, 0, 0) == 0); pass(); pass();
	assert(nmic_state == NM_IDLE && !hardware.status.running && lock_depth == 0);
	/* 10. an amplifier start that fails during the recording (the I2C8 failure): the recording ends cleanly, MT6359 down */
	assert(mic_open(0, FREAD, 0, 0) == 0);
	pass(); pass(); assert(nmic_state == NM_ACTIVE && hardware.status.silent_only);
	start_fail = -3; stream.q[0] = 900; stream.n = 64; pass();
	assert(!hardware.status.running && !afe_live && nmic_rc == -3);
	pass(); assert(nmic_state == NM_DOWN_WAIT); pass(); assert(!pm_up && (nmic_state == NM_IDLE || nmic_state == NM_FAILED));
	pass(); assert(nmic_state == NM_FAILED);                                 /* the path's error is sticky: no retry loop */
	assert(mic_close(0, 0, 0, 0) == 0); pass(); assert(nmic_state == NM_IDLE);
	printf("all /dev/redwood-mic worker host tests passed (sessions %u, upgrades %u, wakeups %d)\n", nmic_sessions, nmic_upgrades, wakeups);
	return 0;
}
