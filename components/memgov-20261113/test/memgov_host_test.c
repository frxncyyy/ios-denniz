/* host test of b/bsd/kern/nord2_memgov.h (0420): thresholds, level, eligibility, ordering, cadence */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../b/bsd/kern/nord2_memgov.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
	struct n2mg_limits l;
	uint64_t ram = 0x13e000000ull;          /* 5088 MiB window */
	n2mg_limits_for(ram, 16384, 200, 120, &l);
	CHECK(l.pages == 325632);
	CHECK(l.low == 65126 && l.crit == 39075);
	printf("5088 MiB: pages %u low %u (%.0f MiB) crit %u (%.0f MiB)\n", l.pages, l.low, l.low / 64.0, l.crit, l.crit / 64.0);
	n2mg_limits_for(0xec000000ull, 16384, 200, 120, &l);   /* 3776 MiB */
	CHECK(l.pages == 241664 && l.low == 48332 && l.crit == 28999);
	n2mg_limits_for(ram, 16384, 100, 150, &l);           /* crit > low: clamped to low */
	CHECK(l.crit == l.low);
	n2mg_limits_for(ram, 16384, 0, 5000, &l);            /* out of range: defaults */
	CHECK(l.low == 65126 && l.crit == 39075);
	n2mg_limits_for(ram, 0, 200, 120, &l);
	CHECK(l.pages == 0 && l.low == 0 && l.crit == 0);

	n2mg_limits_for(ram, 16384, 200, 120, &l);
	CHECK(n2mg_level(70000, &l) == N2MG_LEVEL_OK);
	CHECK(n2mg_level(65125, &l) == N2MG_LEVEL_LOW);
	CHECK(n2mg_level(39074, &l) == N2MG_LEVEL_CRIT);
	CHECK(n2mg_avail(0xffffffffu, 5) == 0xffffffffu);
	CHECK(n2mg_avail(10, 5) == 15);

	struct n2mg_cand c = { .pid = 500, .band = 0, .memstat_state = 0, .system_or_exiting = false, .footprint = 100, .name = "MobileSafari" };
	CHECK(n2mg_verdict(&c, 30) == N2MG_OK);
	c.band = 30; CHECK(n2mg_verdict(&c, 30) == N2MG_OK);
	c.band = 40; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_BAND);
	c.band = 100; CHECK(n2mg_verdict(&c, 200) == N2MG_NO_BAND);        /* foreground never */
	c.band = 99; CHECK(n2mg_verdict(&c, 200) == N2MG_OK);
	c.band = -2; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_BAND);
	c.band = 0;
	c.pid = 1; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_PID);
	c.pid = 0; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_PID);
	c.pid = 500;
	c.memstat_state = N2MG_ST_INTERNAL; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_STATE);
	c.memstat_state = N2MG_ST_TERMINATED; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_STATE);
	c.memstat_state = 0x800; CHECK(n2mg_verdict(&c, 30) == N2MG_OK);  /* MANAGED alone is fine */
	c.memstat_state = 0;
	c.system_or_exiting = true; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_SYSTEM);
	c.system_or_exiting = false;
	const char *prot[] = { "SpringBoard", "backboardd", "launchd", "mediaserverd", "kernel_task", "runningboardd", "", NULL };
	for (int i = 0; i < 8; i++) { c.name = prot[i]; CHECK(n2mg_verdict(&c, 30) == N2MG_NO_NAME); }
	c.name = "SpringBoardX"; CHECK(n2mg_verdict(&c, 30) == N2MG_OK);
	c.name = "Springboard"; CHECK(n2mg_verdict(&c, 30) == N2MG_OK);    /* exact match only */

	struct n2mg_cand a = { .pid = 10, .band = 0, .footprint = 50 }, b = { .pid = 11, .band = 10, .footprint = 900 };
	CHECK(n2mg_better(&a, &b) && !n2mg_better(&b, &a));                  /* lower band wins over size */
	b.band = 0; CHECK(n2mg_better(&b, &a));                              /* same band: bigger */
	b.footprint = 50; CHECK(n2mg_better(&b, &a));                        /* tie: higher pid */

	uint32_t run = 0;
	CHECK(!n2mg_should_pick(N2MG_LEVEL_CRIT, &run, 200000, 0, 200));    /* first sample */
	CHECK(n2mg_should_pick(N2MG_LEVEL_CRIT, &run, 200250, 0, 200));     /* second */
	CHECK(!n2mg_should_pick(N2MG_LEVEL_CRIT, &run, 200500, 200250, 200)); /* 250 ms after a kill */
	CHECK(!n2mg_should_pick(N2MG_LEVEL_CRIT, &run, 202249, 200250, 202));
	CHECK(n2mg_should_pick(N2MG_LEVEL_CRIT, &run, 202250, 200250, 202)); /* 2 s */
	CHECK(!n2mg_should_pick(N2MG_LEVEL_LOW, &run, 205000, 0, 205) && run == 0);
	run = 5; CHECK(!n2mg_should_pick(N2MG_LEVEL_CRIT, &run, 60000, 0, 60)); /* boot grace */
	/* 10 s of continuous crit: at most 5 picks */
	run = 0; int picks = 0; uint64_t last = 0;
	for (uint64_t t = 300000; t < 310000; t += 250) if (n2mg_should_pick(N2MG_LEVEL_CRIT, &run, t, last, (uint32_t)(t / 1000))) { picks++; last = t; }
	CHECK(picks == 5);
	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
	return fails != 0;
}
