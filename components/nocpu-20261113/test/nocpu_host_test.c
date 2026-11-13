/* 0413 host test: sweet_fb_hw_cpu_allowed (exhaustive) and sweet_fb_blank_action (exhaustive) vs the stated rules; the 0404 helpers still compile.
 * cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../b/iokit/Kernel/arm nocpu_host_test.c  (also -x c++) */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sweet_fb_hw.h"
static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %d %s\n", __LINE__, m); } } while (0)
int main(void)
{
	unsigned n = 0;
	for (uint32_t mode = 0; mode <= 3; mode++) for (int bo = 0; bo < 2; bo++) for (int dead = 0; dead < 2; dead++) for (int em = 0; em < 2; em++) {
		bool got = sweet_fb_hw_cpu_allowed(mode, bo, dead, em), want = dead ? em : (!bo && mode < 2);
		CHECK(got == want, "cpu_allowed rule"); n++;
		if (mode >= 2 && !dead) CHECK(!got, "HW modes never copy unless the overlay is dead");
		if (bo && !dead) CHECK(!got, "backoff holds, never copies");
	}
	for (int po = 0; po < 2; po++) for (int bl = 0; bl < 2; bl++) for (int src = 0; src < 2; src++) for (unsigned f = 0; f < 40; f++) {
		int a = sweet_fb_blank_action(po, bl, src, f), w;
		if (po) w = bl ? SWEET_FB_BLANK_NONE : ((!src || f >= 30) ? SWEET_FB_BLANK_CPU_FADE : SWEET_FB_BLANK_OFF);
		else w = bl ? SWEET_FB_BLANK_ON : SWEET_FB_BLANK_NONE;
		CHECK(a == w, "blank rule"); n++;
	}
	CHECK(sweet_fb_blank_action(false, true, false, 0) == SWEET_FB_BLANK_ON, "display on after a blank even on the CPU path");
	printf("%s: %u cases, %d failures\n", fails ? "FAILED" : "ALL OK", n, fails); return fails != 0;
}
