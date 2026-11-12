/*
 * 0404 host test: the restore / power-off state machine around an H2 test pattern, for both panel buffer indices.
 * The model mirrors the driver's decisions (vsyncFired power-off branch, test end = hwTestPoll phase 2 + hwTestFinish, power
 * on/off, a CPU present) and uses the same pure helpers as SweetFramebuffer.cpp (sweet_fb_hw.h): sweet_fb_hw_fade_may_run,
 * sweet_fb_hw_test_end_action, sweet_fb_hw_front_base.  "old" = the 0399..0402 rules, to show the device symptom is reproduced.
 * Build: cc -O2 -I../b/iokit/Kernel/arm hwfix_host_test.c
 */
#include <stdio.h>
#include <string.h>
#include "sweet_fb_hw.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

enum src { S_A, S_B, S_TEST, S_HW };
struct m {
	int old;                    /* 1 = the 0399..0402 rules */
	bool dbl, power_off, dirty, hw_on;
	uint32_t front, pstate, fade_step, frame;
	enum src ovl;
	char buf[2][16], panel[16];
	int fade_pushes_in_test, lit_while_off, presents;
};

static void push(struct m *m)
{
	const char *c = m->ovl == S_A ? m->buf[0] : m->ovl == S_B ? m->buf[1] : m->ovl == S_TEST ? "pattern" : "hwframe";
	snprintf(m->panel, sizeof(m->panel), "%s", c);
	if (m->power_off && (strncmp(m->panel, "ui", 2) == 0 || strcmp(m->panel, "hwframe") == 0)) { m->lit_while_off++; }
}

static void present(struct m *m)        /* CPU frame: copy into the back buffer, flip, push */
{
	uint32_t back = m->dbl ? (m->front ? 0 : 1) : 0;
	snprintf(m->buf[back], sizeof(m->buf[back]), "ui%u", ++m->frame);
	m->front = back; m->ovl = back ? S_B : S_A;
	m->dirty = false; m->presents++;
	push(m);
}

static void vsync(struct m *m)
{
	if (m->power_off && !m->old && !sweet_fb_hw_fade_may_run(m->pstate)) { return; }
	if (m->power_off) {
		uint32_t step = m->fade_step;
		if (step <= 6) { m->fade_step = step + 1; }
		if (m->hw_on && m->pstate == 0) { m->hw_on = false; m->ovl = sweet_fb_hw_front_base(m->dbl, m->front, S_A, S_B) == S_A ? S_A : S_B; if (step < 6) { step = 6; m->fade_step = 7; } }
		if (m->dbl && m->front != 0 && m->pstate == 0) { m->ovl = S_A; m->front = 0; }
		if (step <= 6) {
			snprintf(m->buf[0], sizeof(m->buf[0]), step < 6 ? "fade%u" : "black", step);
			if (m->pstate == SWEET_FB_HW_PSTATE_TEST) { m->fade_pushes_in_test++; }
			push(m);
		}
		return;
	}
	if (m->pstate == 0 && m->dirty) { present(m); }
}

static void power(struct m *m, bool off) { m->power_off = off; m->fade_step = 0; m->dirty = true; }

static void test_begin(struct m *m) { m->pstate = SWEET_FB_HW_PSTATE_TEST; m->ovl = S_TEST; m->hw_on = true; push(m); }

static void test_end(struct m *m, bool reapply_wanted)
{
	struct sweet_fb_hw_test_end te = m->old ? (struct sweet_fb_hw_test_end){ false, true, reapply_wanted }
	                                        : sweet_fb_hw_test_end_action(m->power_off, m->dbl, m->front, reapply_wanted);
	if (te.reapply) { m->ovl = S_HW; m->hw_on = true; }
	else { m->ovl = sweet_fb_hw_front_base(m->dbl, m->front, S_A, S_B) == S_A ? S_A : S_B; m->hw_on = false; }
	/* hwTestFinish */
	struct sweet_fb_hw_test_end f = m->old ? (struct sweet_fb_hw_test_end){ false, true, false } : sweet_fb_hw_test_end_action(m->power_off, m->dbl, m->front, false);
	if (!m->old && m->power_off && m->hw_on) { m->hw_on = false; m->ovl = m->front ? S_B : S_A; }
	if (f.select_buffer0) { m->ovl = S_A; m->front = 0; }
	if (f.push) { push(m); }
	m->pstate = 0;
	if (m->dirty && !m->power_off) { present(m); }
}

/* one run: boot frames, test (hold of 180 vsyncs), optional power-off in the hold, test end, 20 vsyncs, power on, 5 vsyncs */
static void run(int old, bool dbl, uint32_t front, bool off_in_hold, bool reapply, struct m *m)
{
	memset(m, 0, sizeof(*m));
	m->old = old; m->dbl = dbl;
	snprintf(m->buf[0], 16, "boot"); snprintf(m->buf[1], 16, "boot");
	m->dirty = true;
	vsync(m);                                   /* frame 1 into buffer (dbl ? 1 : 0) */
	if (dbl && front == 0) { m->dirty = true; vsync(m); }      /* frame 2 into buffer 0 */
	CHECK(m->front == (dbl ? front : 0), "setup front %u want %u", m->front, front);
	test_begin(m);
	for (int t = 0; t < 180; t++) {
		if (off_in_hold && t == 40) { power(m, true); }
		vsync(m);
	}
	test_end(m, reapply);
	for (int t = 0; t < 20; t++) { vsync(m); }
	if (off_in_hold) {
		power(m, false);
		for (int t = 0; t < 5; t++) { vsync(m); }
	}
}

int
main(void)
{
	int cases = 0;
	for (int dbl = 0; dbl <= 1; dbl++) {
		for (uint32_t front = 0; front <= (uint32_t)dbl; front++) {
			for (int off = 0; off <= 1; off++) {
				for (int re = 0; re <= 1; re++) {
					struct m m;
					run(0, dbl, front, off, re, &m);
					cases++;
					CHECK(m.fade_pushes_in_test == 0, "dbl %d front %u off %d re %d: fade pushed under the test %d times", dbl, front, off, re, m.fade_pushes_in_test);
					CHECK(m.lit_while_off == 0, "dbl %d front %u off %d re %d: a UI/HW frame was pushed while the display is off (%d)", dbl, front, off, re, m.lit_while_off);
					if (off) {
						/* after the test end + 20 vsyncs (still off): the fade ran from its first step and the panel is black on buffer 0 */
						/* (checked through the final state after power on: presents resumed, see below) */
						CHECK(m.presents >= (dbl && front == 0 ? 3 : 2), "dbl %d front %u re %d: presents did not resume after power on (%d)", dbl, front, re, m.presents);
						CHECK(strncmp(m.panel, "ui", 2) == 0, "dbl %d front %u re %d: panel after power on = %s", dbl, front, re, m.panel);
					} else {
						CHECK(strcmp(m.panel, re ? "hwframe" : (front ? m.buf[1] : m.buf[0])) == 0 || (!re && strncmp(m.panel, "ui", 2) == 0),
						    "dbl %d front %u re %d: panel after the test %s", dbl, front, re, m.panel);
					}
				}
			}
		}
	}
	/* black while off: stop right after the test end + 20 vsyncs, both buffer indices */
	for (uint32_t front = 0; front <= 1; front++) {
		struct m m;
		memset(&m, 0, sizeof(m)); m.dbl = true;
		snprintf(m.buf[0], 16, "boot"); snprintf(m.buf[1], 16, "boot");
		m.dirty = true; vsync(&m);
		if (front == 0) { m.dirty = true; vsync(&m); }
		test_begin(&m);
		for (int t = 0; t < 180; t++) { if (t == 40) { power(&m, true); } vsync(&m); }
		CHECK(m.fade_step == 0, "front %u: the fade advanced under the test (%u)", front, m.fade_step);
		test_end(&m, true);
		CHECK(m.front == 0 && m.ovl == S_A, "front %u: test end with the display off must restore onto buffer 0 (front %u ovl %d)", front, m.front, (int)m.ovl);
		CHECK(strcmp(m.panel, "pattern") == 0, "front %u: nothing pushed at the test end (panel %s)", front, m.panel);
		for (int t = 0; t < 20; t++) { vsync(&m); }
		CHECK(strcmp(m.panel, "black") == 0 && m.lit_while_off == 0, "front %u: panel %s after the fade (lit %d)", front, m.panel, m.lit_while_off);
		CHECK(sweet_fb_hw_front_base(true, front, 0x76e10000u, 0x77806000u) == (front ? 0x77806000u : 0x76e10000u), "front base");
		CHECK(sweet_fb_hw_front_base(false, front, 0x76e10000u, 0x77806000u) == 0x76e10000u, "front base single");
	}
	/* the old rules reproduce the device: fade pushes under the test, and with front = 1 a stale UI frame lit on an off display */
	{
		struct m m0, m1;
		run(1, true, 0, true, false, &m0);
		run(1, true, 1, true, false, &m1);
		CHECK(m0.fade_pushes_in_test > 0 && m1.fade_pushes_in_test > 0, "old: fade under the test not reproduced");
		CHECK(m1.lit_while_off > 0, "old, front 1 (night8 case): stale UI frame on an off display not reproduced");
		printf("old rules: fade pushes under the test %d/%d, stale frame lit while off: front0 %d front1 %d\n", m0.fade_pushes_in_test,
		    m1.fade_pushes_in_test, m0.lit_while_off, m1.lit_while_off);
	}
	CHECK(sweet_fb_hw_fade_may_run(0) && sweet_fb_hw_fade_may_run(2) && !sweet_fb_hw_fade_may_run(SWEET_FB_HW_PSTATE_TEST), "fade gate");
	printf("%d state-machine cases (single/double buffer, front 0/1, display off in the hold or not, re-apply or not)\n", cases);
	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
	return fails ? 1 : 0;
}
