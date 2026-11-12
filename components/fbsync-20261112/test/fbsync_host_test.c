/* 0393 host test: SwapWait retire state with the 0156 double buffer (prepare slot).
 * cc -O1 -Wall -I../b/iokit/Kernel/arm fbsync_host_test.c -o t && ./t */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sweet_fb_swapwait.h"

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); fails++; } else { printf("ok   %s\n", msg); } } while (0)

/* CoreAnimation's question before it redraws the surface of swap id: SwapWait(id, mode 0, no-block) */
static int can_update(struct sweet_fb_retire *r, uint32_t id) { return sweet_fb_swap_ready(r, id, 0, false, false); }

int main(void)
{
	struct sweet_fb_retire r; unsigned slot;

	/* --- the pre-0393 bookkeeping (prepNext took the swap without telling fRetire) --- */
	memset(&r, 0, sizeof(r));
	sweet_fb_retire_on_swap(&r, 1, false, 0, &slot); sweet_fb_retire_take(&r);      /* swap 1 presenting (transfer running) */
	sweet_fb_retire_on_swap(&r, 2, false, 0, &slot);                                 /* swap 2 queued */
	/* old prepNext: lifecycle take only, r.pending stays 2; copy of 2 not done yet (waiting for the GPU) */
	sweet_fb_retire_on_swap(&r, 3, false, 0, &slot);                                 /* swap 3 arrives */
	CHECK(can_update(&r, 2), "old: swap 2 answered retired while its copy has not run (the bug 0393 fixes)");

	/* --- 0393 --- */
	memset(&r, 0, sizeof(r));
	sweet_fb_retire_on_swap(&r, 1, false, 0, &slot); sweet_fb_retire_take(&r);
	CHECK(!can_update(&r, 1), "swap 1 not retired while it is being copied");
	sweet_fb_retire_copied(&r);
	CHECK(can_update(&r, 1), "swap 1 retired as soon as its copy ended (transfer still running)");
	sweet_fb_retire_on_swap(&r, 2, false, 0, &slot);
	sweet_fb_retire_prep(&r);                                                        /* prepNext takes 2 */
	CHECK(!can_update(&r, 2), "swap 2 taken by the prepare slot: not retired");
	sweet_fb_retire_on_swap(&r, 3, false, 0, &slot);
	CHECK(r.dropped == 0, "swap 3 does not count the prepared swap 2 as replaced");
	CHECK(!can_update(&r, 2), "swap 2 still not retired after swap 3 arrived (copy pending)");
	CHECK(!can_update(&r, 3), "swap 3 pending: not retired");
	sweet_fb_retire_prep_end(&r, false);                                             /* prepNext copied 2 */
	CHECK(can_update(&r, 2), "swap 2 retired after its copy");
	/* handOver of the prepared frame (state 2) must not touch the pending swap 3 */
	sweet_fb_retire_prep_end(&r, false);
	CHECK(!can_update(&r, 3) && r.pending == 3 && r.presenting == 0, "handOver(prepared) leaves swap 3 pending (old code marked it presenting)");

	/* handOver of an uncopied prepare slot (state 1): the presenter owns it */
	memset(&r, 0, sizeof(r));
	sweet_fb_retire_on_swap(&r, 7, false, 0, &slot); sweet_fb_retire_prep(&r);
	sweet_fb_retire_prep_end(&r, true);
	CHECK(r.presenting == 7 && !can_update(&r, 7), "state-1 hand-over: swap 7 presenting, not retired");
	sweet_fb_retire_copied(&r);
	CHECK(can_update(&r, 7), "swap 7 retired after the presenter's copy");

	/* prepare slot dropped (power off) without a newer swap: back to pending; with a newer one: retired */
	memset(&r, 0, sizeof(r));
	sweet_fb_retire_on_swap(&r, 9, false, 0, &slot); sweet_fb_retire_prep(&r);
	sweet_fb_retire_prep_drop(&r, true, 9);
	CHECK(r.pending == 9 && !can_update(&r, 9), "dropped+requeued swap 9 is pending again");
	memset(&r, 0, sizeof(r));
	sweet_fb_retire_on_swap(&r, 9, false, 0, &slot); sweet_fb_retire_prep(&r);
	sweet_fb_retire_on_swap(&r, 10, false, 0, &slot);
	sweet_fb_retire_prep_drop(&r, false, 9);
	CHECK(can_update(&r, 9) && r.pending == 10, "dropped swap 9 superseded by 10: 9 retired, 10 pending");

	/* display off / stopped: always ready */
	memset(&r, 0, sizeof(r));
	sweet_fb_retire_on_swap(&r, 4, false, 0, &slot); sweet_fb_retire_prep(&r);
	CHECK(sweet_fb_swap_ready(&r, 4, 0, true, false) && sweet_fb_swap_ready(&r, 4, 0, false, true), "display off / stopped: ready");
	/* mode 1: no longer waiting once taken */
	CHECK(sweet_fb_swap_ready(&r, 4, 1, false, false), "mode 1: the prepared swap is no longer waiting");

	/* Simulation: CA with two surfaces (A/B alternate) at its frame clock; the presenter takes and copies the pending swap only on some
	 * ticks (it lags behind sometimes).  A race = CA redraws a surface whose last swap was neither copied nor replaced unseen (the copy
	 * would read the new drawing).  Level 0 = always ready (q10jfe); new = level 1 with the 0393 bookkeeping. */
	{
		enum { N = 20000 };
		static unsigned char copied[N + 2], dropped[N + 2];
		unsigned long races[2] = { 0, 0 }, skips = 0;
		srand(1);
		for (int mode = 0; mode < 2; mode++) {
			memset(&r, 0, sizeof(r)); memset(copied, 0, sizeof(copied)); memset(dropped, 0, sizeof(dropped));
			uint32_t last[2] = { 0, 0 }, id = 0;
			for (int f = 0; f < N; f++) {
				uint32_t prev = last[f & 1];
				int ready = mode == 0 ? 1 : (prev == 0 || can_update(&r, prev));
				if (!ready) { skips++; }
				else {
					if (prev != 0 && !copied[prev] && !dropped[prev]) { races[mode]++; }
					id++; last[f & 1] = id;
					uint32_t replaced = r.pending;
					if (sweet_fb_retire_on_swap(&r, id, false, 0, &slot) == SWEET_FB_RETIRE_RETIRED) { dropped[replaced] = 1; }
				}
				int k = rand() % 100;
				if (r.preparing) {                         /* the prepare slot holds one swap (fNextState != 0): it finishes first */
					if (k < 60) { copied[r.preparing] = 1; sweet_fb_retire_prep_end(&r, false); }
				} else if (k < 50 && r.pending) {          /* presenter idle: take + copy now */
					sweet_fb_retire_take(&r); copied[r.presenting] = 1; sweet_fb_retire_copied(&r);
				} else if (k < 70 && r.pending) {          /* prepare slot takes it (GPU wait), copy on a later tick */
					sweet_fb_retire_prep(&r);
				}
			}
		}
		printf("sim: redraw-before-copy level0=%lu new=%lu (new: %lu frames skipped by CA instead)\n", races[0], races[1], skips);
		CHECK(races[0] > 0 && races[1] == 0, "simulation: level 1 + 0393 never lets CA redraw an uncopied surface");
	}
	printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
	return fails != 0;
}
