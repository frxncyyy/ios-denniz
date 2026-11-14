/* 0633 host test of n2rail_set (nord2_gpu_rail.c) against a PMIC register model: order (VSRAM up before VGPU, VGPU down before VSRAM),
 * BUCK_DIFF at every intermediate state, read-back and DA verification, vendor settle delays, limits, failure -> uncertain, power-off undo
 * restores the boot selectors. */
#include "nord2_gpu_rail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d %s\n", __func__, __LINE__, #x); exit(1); } } while (0)
static uint8_t r15[0x2000]; static uint16_t r59[0x2000];
static char order[64]; static unsigned no, delay_us, writes, da_lag, fail_write_at;
static uint32_t vgpu(void) { uint32_t v = 0; n2rail_vgpu_uv(r15[N2RAIL_VGPU_DA_VOSEL], &v); return v; }
static uint32_t vsram(void) { uint32_t v = 0; n2rail_vsram_uv((uint8_t)((r59[N2RAIL_VSRAM_VOSEL1] >> 8) & 0x7f), &v); return v; }
static void diffcheck(void) { int32_t d = (int32_t)vsram() - (int32_t)vgpu(); CHECK(d <= 300000 && d >= -100000); }
static int rd15(void *c, uint16_t a, uint8_t *v) { (void)c; *v = r15[a]; return 0; }
static int wr15(void *c, uint16_t a, uint8_t v) { (void)c; if (fail_write_at && ++writes == fail_write_at) return -1; r15[a] = v;
	if (a == N2RAIL_VGPU_VOSEL) { order[no++] = 'G'; if (!da_lag) r15[N2RAIL_VGPU_DA_VOSEL] = v; diffcheck(); } return 0; }
static int rd59(void *c, uint16_t a, uint16_t *v) { (void)c; *v = r59[a]; return 0; }
static int wr59(void *c, uint16_t a, uint16_t v) { (void)c; if (fail_write_at && ++writes == fail_write_at) return -1; r59[a] = v;
	if (a == N2RAIL_VSRAM_ELR) { order[no++] = 'S'; r59[N2RAIL_VSRAM_VOSEL1] = (uint16_t)((r59[N2RAIL_VSRAM_VOSEL1] & 0x80ff) | ((v & 0x7f) << 8)); diffcheck(); } return 0; }
static void ud(void *c, unsigned us) { (void)c; delay_us += us; }
static struct n2rail r;
static void boot(void) {   /* the phone at boot: VGPU 0x78 = 750 mV on, VSRAM 0x28 = 750 mV on (NORD2 GPU RAIL line) */
	memset(r15, 0, sizeof(r15)); memset(r59, 0, sizeof(r59));
	r15[N2RAIL_VGPU_EN] = 0x0d; r15[N2RAIL_VGPU_VOSEL] = 0x78; r15[N2RAIL_VGPU_DA_EN] = 0x83; r15[N2RAIL_VGPU_DA_VOSEL] = 0x78;
	r59[N2RAIL_VSRAM_CON0] = 1; r59[N2RAIL_VSRAM_MON] = 3; r59[N2RAIL_VSRAM_VOSEL1] = 0x283c; r59[N2RAIL_VSRAM_ELR] = 0x28;
	struct n2rail_ops o = { rd15, wr15, rd59, wr59, ud, NULL };
	CHECK(n2rail_init(&r, &o, NULL));
	struct n2rail_point pt; CHECK(n2rail_point_for_khz(654000, &pt)); CHECK(n2rail_on(&r, &pt) == N2RAIL_OK && r.writes == 0 && r.on);
	no = 0; memset(order, 0, sizeof(order)); delay_us = 0; writes = 0; da_lag = 0; fail_write_at = 0;
}
int main(void) {
	boot(); CHECK(vgpu() == 750000 && vsram() == 750000);
	/* raise to the 886 MHz OPP: 800/800 mV, VSRAM first */
	CHECK(n2rail_set(&r, 800000, 800000) == N2RAIL_OK); CHECK(!strcmp(order, "SG") && vgpu() == 800000 && vsram() == 800000);
	CHECK(delay_us == (50000 / 12500 + 8) + (50000 / 12500 + 9));                    /* vendor rising settle */
	CHECK(r.vgpu_raised && r.vsram_raised && r.vgpu_orig_sel == 0x78 && r.vsram_orig_elr == 0x28);
	/* back down: VGPU first */
	no = 0; memset(order, 0, sizeof(order)); delay_us = 0;
	CHECK(n2rail_set(&r, 750000, 750000) == N2RAIL_OK); CHECK(!strcmp(order, "GS") && vgpu() == 750000 && vsram() == 750000);
	CHECK(delay_us == (50000 / 5000 + 9) + (50000 / 5000 + 8));                        /* vendor falling settle */
	CHECK(!r.vgpu_raised && !r.vsram_raised);
	/* no change: no write */
	no = 0; CHECK(n2rail_set(&r, 750000, 750000) == N2RAIL_OK && no == 0);
	/* limits */
	CHECK(n2rail_set(&r, 806250, 800000) == N2RAIL_E_OVERVOLT); CHECK(n2rail_set(&r, 800000, 806250) == N2RAIL_E_OVERVOLT);
	CHECK(n2rail_set(&r, 643750, 750000) == N2RAIL_E_OVERVOLT); CHECK(n2rail_set(&r, 750000, 743750) == N2RAIL_E_OVERVOLT);
	CHECK(!r.uncertain && no == 0);
	/* power-off after a raise restores the boot selectors */
	CHECK(n2rail_set(&r, 800000, 800000) == N2RAIL_OK); CHECK(n2rail_off(&r) == N2RAIL_OK && r15[N2RAIL_VGPU_VOSEL] == 0x78 && (r59[N2RAIL_VSRAM_ELR] & 0x7f) == 0x28);
	/* DA does not follow (regulator not responding): verify fails, uncertain, no further change */
	boot(); da_lag = 1; CHECK(n2rail_set(&r, 800000, 800000) == N2RAIL_E_VERIFY && r.uncertain); CHECK(n2rail_set(&r, 750000, 750000) == N2RAIL_E_STATE);
	/* transport failure on the VGPU write: uncertain */
	boot(); fail_write_at = 2; CHECK(n2rail_set(&r, 800000, 800000) != N2RAIL_OK && r.uncertain);
	/* rails not on */
	boot(); r.on = false; CHECK(n2rail_set(&r, 800000, 800000) == N2RAIL_E_STATE && no == 0);
	boot(); r15[N2RAIL_VGPU_DA_EN] = 0; CHECK(n2rail_set(&r, 800000, 800000) == N2RAIL_E_STATE && no == 0);
	/* odd values round up to the next selector and stay in range */
	boot(); CHECK(n2rail_set(&r, 776000, 790000) == N2RAIL_OK && vgpu() == 781250 && vsram() == 793750);
	printf("RAIL_SET_HOST_TEST_OK\n"); return 0;
}
