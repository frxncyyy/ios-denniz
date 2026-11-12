/* 0407 host test: content clock (sweet_fb_fpsmeter.h) and touch rate (nord2_input_rate.h).
 * cc -std=c11 -Wall -Wextra -Werror -I../b/iokit/Kernel/arm -I../b/bsd/dev fpsmeter_host_test.c -o fpsmeter_host_test && ./fpsmeter_host_test */
#include <assert.h>
#include <string.h>
#include "sweet_fb_fpsmeter.h"
#include "nord2_input_rate.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int note2(struct sweet_fb_fps *f, uint32_t i0, uint64_t s0, uint32_t i1, uint64_t s1)
{
	uint32_t id[2] = { i0, i1 }; uint64_t sq[2] = { s0, s1 }; int32_t pid[2] = { 368, 59 };      /* layer 0 game, layer 1 backboardd */
	return sweet_fb_fps_note(f, 2, id, sq, pid);
}

static void test_clock(void)
{
	struct sweet_fb_fps f; memset(&f, 0, sizeof(f));
	CHECK(note2(&f, 309, 100, 2, 101) == SWEET_FB_FPS_NEW);      /* first frame: layout change */
	CHECK(note2(&f, 309, 100, 2, 101) == SWEET_FB_FPS_SAME);     /* identical: the same pixels scanned out again */
	CHECK(note2(&f, 323, 110, 3, 111) == SWEET_FB_FPS_NEW);      /* both re-rendered */
	CHECK(note2(&f, 323, 110, 51, 115) == SWEET_FB_FPS_NEW);     /* only backboardd re-rendered: game frame repeated */
	CHECK(note2(&f, 309, 100, 2, 120) == SWEET_FB_FPS_BACK);     /* game layer goes back to an older drawable: time travel */
	CHECK(note2(&f, 309, 130, 2, 120) == SWEET_FB_FPS_NEW);      /* re-rendered drawable 309: newer again */
	CHECK(note2(&f, 77, 0, 2, 120) == SWEET_FB_FPS_UNK);         /* CPU-drawn surface (no GPU write known) swapped in */
	CHECK(note2(&f, 77, 0, 2, 120) == SWEET_FB_FPS_SAME);
	CHECK(note2(&f, 78, 0, 3, 140) == SWEET_FB_FPS_NEW);         /* a GPU-proven change wins over unknown */
	CHECK(f.n_new == 5 && f.n_same == 2 && f.n_back == 1 && f.n_unk == 1);
	/* per client: 9 frames each; game fresh on frames 1,3,5,6,7,9 = 6; backboardd fresh on 1,3,4,5,9 = 5 */
	CHECK(f.cl[0].pid == 368 && f.cl[0].shown == 9 && f.cl[0].fresh == 6);
	CHECK(f.cl[1].pid == 59 && f.cl[1].shown == 9 && f.cl[1].fresh == 5);
	/* a CPU frame in between: the next HW frame counts as new even with an older seq */
	sweet_fb_fps_break(&f);
	CHECK(note2(&f, 309, 100, 2, 101) == SWEET_FB_FPS_NEW);
	/* single layer after two: layout change -> new, never back */
	{ uint32_t id = 2; uint64_t sq = 5; CHECK(sweet_fb_fps_note(&f, 1, &id, &sq, NULL) == SWEET_FB_FPS_NEW); }
	char b[200];
	sweet_fb_fps_format_seen(&f, b, sizeof(b));
	CHECK(strcmp(b, "hw_new=7 hw_same=2 hw_back=1 hw_unk=1") == 0);
	sweet_fb_fps_window_reset(&f);
	CHECK(f.n_new == 0 && f.cl[0].shown == 0 && f.last_n == 1 && f.last_id[0] == 2);      /* the previous frame survives the window edge */
	{ uint32_t id = 2; uint64_t sq = 5; CHECK(sweet_fb_fps_note(&f, 1, &id, &sq, NULL) == SWEET_FB_FPS_SAME); }
	/* client overflow -> other; rate formatting */
	memset(&f, 0, sizeof(f));
	for (int32_t p = 1; p <= 6; p++) { uint32_t id = (uint32_t)p; uint64_t sq = (uint64_t)p; sweet_fb_fps_note(&f, 1, &id, &sq, &p); }
	CHECK(f.cl[3].pid == 4 && f.other_shown == 2 && f.other_fresh == 2);
	struct sweet_fb_fps_client c = { 368, 446, 223 };
	sweet_fb_fps_format_client(&c, "SubwaySurf", 5000000, b, sizeof(b));
	CHECK(strcmp(b, "SubwaySurf[368] shown=446 fresh=223 (44.6/s)") == 0);
}

static void test_input(void)
{
	struct n2ir r; memset(&r, 0, sizeof(r));
	n2ir_reset(&r, 1000);
	CHECK(n2ir_due(&r, 2000) == 0);
	uint64_t t = 10000;
	n2ir_report(&r, t, 1, 1, 3000);                         /* touch down: first report, no gap */
	t += 9000;  n2ir_report(&r, t, 1, 1, 3000);             /* 9 ms  -> bin <=10 */
	t += 22000; n2ir_report(&r, t, 1, 1, 11000);            /* 22 ms -> bin <=25 */
	t += 60000; n2ir_report(&r, t, 1, 0, 3000);             /* 60 ms -> more (stationary finger: no move) */
	t += 15000; n2ir_report(&r, t, 0, 1, 3000);             /* lift: not a report, ends the chain */
	t += 500000; n2ir_report(&r, t, 1, 1, 3000);            /* new touch: no gap from the old chain */
	n2ir_error(&r);
	CHECK(r.reports == 5 && r.moves == 5 && r.gaps == 3 && r.errors == 1);
	CHECK(r.gap[0] == 1 && r.gap[4] == 1 && r.gap[7] == 1 && r.gap_max_us == 60000);
	CHECK(r.read_max_us == 11000 && r.read_sum_us == 23000);
	CHECK(n2ir_bin(10000) == 0 && n2ir_bin(10001) == 1 && n2ir_bin(33000) == 5 && n2ir_bin(50001) == 7);
	CHECK(n2ir_due(&r, 1000 + N2IR_WINDOW_US) == 1);
	char b[300];
	n2ir_format(&r, 1000 + N2IR_WINDOW_US, b, sizeof(b));
	CHECK(strcmp(b, "win_ms=5000 reports=5 (1.0/s) moves=5 errors=1 gap_ms<=10/12/16/20/25/33/50/more=1/0/0/0/1/0/0/1 "
	                "gap_avg_us=30333 gap_max_us=60000 read_avg_us=4600 read_max_us=11000") == 0);
	if (fails) { printf("%s\n", b); }
	n2ir_reset(&r, 1000 + N2IR_WINDOW_US);
	CHECK(r.reports == 0 && r.last_us == t);                 /* finger still down across the edge keeps its chain */
	CHECK(n2ir_due(&r, 1000 + 2 * N2IR_WINDOW_US) == -1);    /* idle window: silent */
}

int main(void)
{
	test_clock();
	test_input();
	printf(fails ? "FAILED %d\n" : "fpsmeter host test: all checks passed%.0d\n", fails);
	return fails != 0;
}
