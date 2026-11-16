/* host test of the bt2 breadcrumb zone + previous-boot guard (functions extracted verbatim from nord2_bt.c) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#define __printflike(a,b)
typedef uintptr_t vm_offset_t; typedef size_t vm_size_t;
static uint8_t zone[0x1000] __attribute__((aligned(8)));
static int mapped;
static vm_offset_t ml_io_map_unmappable(vm_offset_t pa, vm_size_t sz, uint32_t f) { (void)pa; (void)sz; (void)f; mapped++; return (vm_offset_t)zone; }
static uint64_t mach_absolute_time(void) { return 5; }
static void absolutetime_to_nanoseconds(uint64_t a, uint64_t *ns) { *ns = a * 1000000; }
#define VM_WIMG_IO 7
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#include "crumb.inc"
static void reboot(void) { n2bt_crumb_va = NULL; n2bt_prev_reset = 0; n2bt_crumb_seq = 0; n2bt_lin_at = 0; n2bt_ring_next = 0; }
static int zone_has(const char *t) { uint32_t sz = ((uint32_t *)zone)[2]; return memmem(zone + 12, sz, t, strlen(t)) != NULL; }
int main(void)
{
	memset(zone, 0xa5, sizeof zone);                       /* garbage from LK/Android */
	n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "garbage zone is not a reset");
	CHECK(((uint32_t *)zone)[0] == 0x43474244u && memcmp(zone + 12, "====0.0-D\n", 10) == 0, "header + kmsg header");
	n2bt_crumb_raw("N2BT INFLIGHT attempt 1 maxstep 10\n", 35);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);   /* as n2bt_thread does */
	n2bt_now("starting:S2", 0, 0);                        /* as n2bt_set_state does */
	n2bt_crumb("S2 BT wake vote");
	reboot(); n2bt_crumb_init();                           /* boot 2 after a reset in S2 */
	CHECK(n2bt_prev_reset, "INFLIGHT without END = reset");
	CHECK(zone_has("THAT BOOT RESET"), "logged");
	n2bt_crumb_raw("N2BT INFLIGHT attempt 1 maxstep 10\n", 35);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_crumb_raw("N2BT END failed\n", 16);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT END failed\n", 16);   /* as n2bt_end does */
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "INFLIGHT + END = clean");
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "empty previous record = clean");
	for (int i = 0; i < 400; i++) n2bt_crumb("filler line %d with some text to fill the zone", i);
	CHECK(((uint32_t *)zone)[2] <= 0x1000 - 12, "linear zone never overflows (%u)", ((uint32_t *)zone)[2]);
	/* bt8: the ring keeps the LAST lines; the linear part keeps the first ones; END survives in the guard slot */
	CHECK(zone_has("filler line 399 ") && zone_has("filler line 359 ") && !zone_has("filler line 358 "), "ring holds exactly the last 41 lines");
	CHECK(zone_has("====0.0-D\n") && zone_has("previous boot"), "first lines kept");
	{
		int bad = 0;
		for (uint32_t i = 0; i < N2BT_DATA_END; i++) { uint8_t ch = zone[12 + i]; if (ch != '\n' && (ch < 0x20 || ch > 0x7e)) bad++; }
		CHECK(bad == 0, "zone is plain text (%d bad bytes)", bad);
		CHECK(zone[12 + N2BT_DATA_END - 1] == '\n' && zone[12 + N2BT_LIN_CAP - 1] == '\n', "slots end in newlines");
	}
	n2bt_now("tx", 0x200b, 11);
	CHECK(memmem(zone + 12 + N2BT_NOW_AT, N2BT_NOW_LEN, "N2BT NOW 5 tx 200b b", 20) != NULL, "NOW slot");
	for (int i = 0; i < 5; i++) n2bt_now("a-very-long-step-name-that-does-not-fit", 0xffffffffu, 0xffffffffu);
	CHECK(zone[12 + N2BT_NOW_AT + N2BT_NOW_LEN - 1] == '\n' && zone[12 + N2BT_GUARD_AT] != 'a' && zone[12 + N2BT_NOW_AT + N2BT_NOW_LEN] != 'a', "NOW slot bounded");
	n2bt_crumb_raw("N2BT INFLIGHT attempt 1 maxstep 10\n", 35);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_now("starting:S7", 0, 0);
	reboot(); n2bt_crumb_init();
	CHECK(n2bt_prev_reset, "INFLIGHT (ring + guard slot) without END = reset");
	n2bt_crumb_raw("N2BT INFLIGHT attempt 1 maxstep 10\n", 35);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_crumb_raw("N2BT END up\n", 12);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT END up\n", 12);
	for (int i = 0; i < 100; i++) n2bt_crumb("pairing line %d", i);   /* pushes the END line out of the ring */
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "END kept by the guard slot after the ring wrapped");
	((uint32_t *)zone)[2] = 0xffffff;                     /* corrupt size */
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "corrupt header ignored");
	/* bt9: guard slot decides for a bt8+ zone */
	n2bt_crumb_raw("N2BT INFLIGHT attempt 1 maxstep 10\n", 35);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_crumb_raw("N2BT END bridge\n", 16);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT END bridge\n", 16);
	for (int i = 0; i < 80; i++) n2bt_crumb("S10w no answer / S10q stuck %d", i);   /* run-time hang, then a hard reset */
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "run-time hang after the bridge started: next boot NOT refused");
	n2bt_crumb_raw("N2BT INFLIGHT attempt 1 maxstep 10\n", 35);
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_crumb_raw("N2BT END bridge\n", 16);   /* END line in the linear part, but the slot says a restart was in flight */
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT restart", 21);
	n2bt_now("rs-bgf", 0, 0);
	reboot(); n2bt_crumb_init();
	CHECK(n2bt_prev_reset, "AP reset inside a restart's power-on: refused");
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT restart", 21);
	n2bt_now("restart", 1, 0);                             /* the q10jfinal29bt case: restart begun, AP alive afterwards */
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "INFLIGHT restart but NOW not in a power-on step (29bt 'restart FAILED' boot): not refused");
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT restart", 21);
	n2bt_now("idle", 0, 0);
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "INFLIGHT restart, bridge idle afterwards: not refused");
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_now("starting:S5", 0, 0);
	reboot(); n2bt_crumb_init();
	CHECK(n2bt_prev_reset, "AP reset during the bring-up: refused");
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_now("waiting-conninfra", 0, 0);
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "manual reboot while S1 waits for conninfra: not refused");
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT END alive 1135s", 20);
	n2bt_now("tx", 0x20032, 21);
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "AP died while BT ran (alive stamp): not refused");
	n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
	n2bt_crumb("S10e bridge runs; waiting for bluetoothd");          /* a q10jfinal28bt zone after a run-time hang */
	for (int i = 0; i < 10; i++) n2bt_crumb("S10w hang %d", i);
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "28bt zone (INFLIGHT slot, bridge had run): not refused");
	/* bt10: an old-format (linear, pre-bt8) zone with INFLIGHT and no END is accepted */
	memset(zone + 12, ' ', 4000);
	memcpy(zone + 12, "====0.0-D\nN2BT INFLIGHT attempt 1 maxstep 10\nN2BT 5 73080 S10j vendor command 0xfd47\n", 82);
	((uint32_t *)zone)[0] = 0x43474244u; ((uint32_t *)zone)[1] = 4074; ((uint32_t *)zone)[2] = 4074;
	reboot(); n2bt_crumb_init();
	CHECK(!n2bt_prev_reset, "old-format zone (q10jfinal27, inflight 1 end 0) not refused");
	if (getenv("DUMP")) {                                 /* the pstore view of the last zone, for crumbs.py */
		n2bt_crumb_raw("N2BT INFLIGHT attempt 1 maxstep 10\n", 35);
		n2bt_crumb_slot(N2BT_GUARD_AT, N2BT_GUARD_LEN, "N2BT INFLIGHT", 13);
		for (int i = 0; i < 60; i++) n2bt_crumb("S11e line %d", i);
		n2bt_now("tx", 0x20032, 21);
		FILE *f = fopen(getenv("DUMP"), "wb"); fwrite(zone + 12, 1, N2BT_DATA_END, f); fclose(f);
	}
	printf("t_crumb: %d checks, %d failures%s\n", checks, fails, fails ? "" : " -- ALL OK");
	return fails != 0;
}
