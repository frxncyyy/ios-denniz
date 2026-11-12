/* SPDX-License-Identifier: MIT
 * Host tests of gnss_pwr.c against a fake CONNAC2 conn_infra/GPS register file that behaves like the vendor driver
 * expects (acks follow requests after a few reads). Checks the exact write sequence, every failure path's rollback and
 * that no register outside the allow-list is touched.  cc -O1 -Wall -Wextra test_gnss_pwr.c gnss_pwr.c && ./a.out
 */
#include <stdio.h>
#include <string.h>
#include "gnss_pwr.h"

#define NREG 32
struct fake {
	uint32_t pa[NREG], val[NREG]; int n;
	uint32_t hwver; int ack_delay, ack1_never, ack2_never, slp_stuck, top_never, wb_stuck, wake_stuck;
	int bgf_reads; int dbg_reads;
	char log[4096]; int nw; int bad_access; unsigned delay_total;
};
static uint32_t *reg(struct fake *f, uint32_t pa)
{
	for (int i = 0; i < f->n; i++) if (f->pa[i] == pa) return &f->val[i];
	f->pa[f->n] = pa; f->val[f->n] = 0; return &f->val[f->n++];
}
static uint32_t r32(void *c, uint32_t pa)
{
	struct fake *f = c;
	if (!n2g_reg_allowed(pa)) { f->bad_access++; return 0xdeadbeef; }
	if (pa == N2G_CONN_HW_VER) return f->hwver;
	if (pa == N2G_RGU_BGFSYS_PWR_ACK) {
		uint32_t on = (*reg(f, N2G_RGU_BGFSYS_PWR_CTL) & 0x80) && (*reg(f, N2G_CFG_GPS_PWRCTRL0) & 1);
		if (!on) return 0;
		if (++f->bgf_reads < f->ack_delay) return 0;
		return (f->ack1_never ? 0 : 1u << 24) | (f->ack2_never ? 0 : 1u << 25);
	}
	if (pa == N2G_CFG_GALS_CONN2GPS || pa == N2G_CFG_GALS_GPS2CONN) {       /* RDY bits mirror EN bits */
		uint32_t v = *reg(f, pa), rdy = 0;
		if (v & 1) rdy |= 1u << 3;
		if (v & 0x10) rdy |= 1u << 7;
		if (f->slp_stuck && pa == N2G_CFG_GALS_GPS2CONN) rdy |= 1u << 7;
		return (v & 0x11) | rdy;
	}
	if (pa == N2G_CSR_GPS_CFG2HOST_DBG) {
		if (f->top_never || (*reg(f, N2G_CSR_HOST2GPS_DBG_SEL) & 0xff) != 0x80) return 0;
		return ++f->dbg_reads > 2 ? 2 : 1;
	}
	if (pa == N2G_CSR_SLP_PROT_CTRL) return f->wake_stuck ? (1u << 5) : 0;
	if (pa == N2G_WTSLP_WB_STA) return (f->wb_stuck || !(*reg(f, N2G_CFG_ADIE_CTL) & 2)) ? (1u << 26) : 0;
	return *reg(f, pa);
}
static void w32(void *c, uint32_t pa, uint32_t v)
{
	struct fake *f = c;
	if (!n2g_reg_allowed(pa)) { f->bad_access++; return; }
	*reg(f, pa) = v;
	f->nw++;
	char b[64]; snprintf(b, sizeof b, "%08x=%08x ", pa, v); strncat(f->log, b, sizeof f->log - strlen(f->log) - 1);
}
static void dly(void *c, unsigned us) { ((struct fake *)c)->delay_total += us; }
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static struct n2g_hw mk(struct fake *f) { struct n2g_hw h = { f, r32, w32, dly, 0, 0x7f000000u }; return h; }

int main(void)
{
	/* 1. success: exact write sequence */
	struct fake f; memset(&f, 0, sizeof f); f.hwver = N2G_CONN_HW_VER_MT6893; f.ack_delay = 5;
	struct n2g_hw h = mk(&f);
	CHECK(n2g_common_on(&h) == N2G_OK);
	const char *want =
		"180601d0=00007f00 "                    /* EMI remap: 0x7f000000 >> 16 */
		"18000008=42540080 "                    /* BGFSYS_ON_TOP power on, key 0x4254 */
		"18001878=00000001 "                    /* GP_FUNCTION_EN */
		"18001618=00000000 18001618=00000000 "  /* conn2gps rx, tx sleep protection off */
		"1800161c=00000000 1800161c=00000000 "  /* gps2conn rx, tx */
		"180600b0=00000080 "                    /* debug select gps_top_off */
		"18001900=00000002 "                    /* A-die top clock (host GPS vote) */
		"18c0103c=00000010 ";                   /* GPS_CLKGEN1 DIGCK_DIV_EN (bus 0x8000103c) */
	CHECK(strcmp(f.log, want) == 0);
	if (strcmp(f.log, want)) printf("got  %s\nwant %s\n", f.log, want);
	CHECK(f.bad_access == 0 && f.delay_total >= 400);
	f.log[0] = 0; CHECK(n2g_dsp_on(&h) == N2G_OK);
	CHECK(strcmp(f.log, "18c73108=00000000 18c73108=00000000 18c73108=00000010 18c73108=00000000 ") == 0);
	f.log[0] = 0; CHECK(n2g_common_off(&h) == N2G_OK);
	CHECK(strstr(f.log, "18001618=00000001 18001618=00000019 1800161c=00000001 1800161c=00000019 ") == f.log); /* RMW carries the read-only TX_RDY bit, as the vendor RMW does */
	CHECK(*reg(&f, N2G_CFG_GPS_PWRCTRL0) == 0 && (*reg(&f, N2G_CFG_ADIE_CTL) & 2) == 0);

	/* 2. unknown conn_infra version: nothing written */
	memset(&f, 0, sizeof f); f.hwver = 0x20010102; h = mk(&f);
	CHECK(n2g_common_on(&h) == N2G_E_HWVER && f.nw == 0);
	/* MT6885 accepted (same plat) */
	memset(&f, 0, sizeof f); f.hwver = N2G_CONN_HW_VER_MT6885; f.ack_delay = 1; h = mk(&f);
	CHECK(n2g_common_on(&h) == N2G_OK);

	/* 3. failure paths roll back GP_FUNCTION_EN and EMI request */
	struct { int field; int err; } cases[] = { {1, N2G_E_BGF_ACK1}, {2, N2G_E_BGF_ACK2}, {3, N2G_E_SLP_PROT}, {4, N2G_E_GPS_TOP}, {5, N2G_E_ADIE_CLK} };
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		memset(&f, 0, sizeof f); f.hwver = N2G_CONN_HW_VER_MT6893; f.ack_delay = 1;
		f.ack1_never = cases[i].field == 1; f.ack2_never = cases[i].field == 2; f.slp_stuck = cases[i].field == 3;
		f.top_never = cases[i].field == 4; f.wb_stuck = cases[i].field == 5;
		h = mk(&f);
		int rc = n2g_common_on(&h);
		CHECK(rc == cases[i].err);
		CHECK(*reg(&f, N2G_CFG_GPS_PWRCTRL0) == 0 && *reg(&f, N2G_CFG_EMI_CTL) == 0);
		CHECK(strstr(f.log, "18c0103c") == 0);           /* PLL never enabled after a failure */
		CHECK(f.bad_access == 0);
	}
	/* 3b. link_on: wake vote around common_on + dsp_on */
	memset(&f, 0, sizeof f); f.hwver = N2G_CONN_HW_VER_MT6893; f.ack_delay = 1; h = mk(&f);
	CHECK(n2g_link_on(&h) == N2G_OK);
	CHECK(strncmp(f.log, "180601ac=00000001 ", 18) == 0);
	CHECK(strstr(f.log, "18c73108=00000000 180601ac=00000000 ") != 0);
	memset(&f, 0, sizeof f); f.hwver = N2G_CONN_HW_VER_MT6893; f.ack_delay = 1; f.wake_stuck = 1; h = mk(&f);
	CHECK(n2g_link_on(&h) == N2G_E_WAKE && strcmp(f.log, "180601ac=00000001 180601ac=00000000 ") == 0);
	/* 4. argument checks */
	memset(&f, 0, sizeof f); h = mk(&f); h.emi_base_pa = 0x7f001000u;
	CHECK(n2g_common_on(&h) == N2G_E_ARG && f.nw == 0);
	CHECK(n2g_reg_allowed(0x18001000) && !n2g_reg_allowed(0x18001004) && n2g_reg_allowed(0x18c73108));
	printf(fails ? "GNSS PWR TEST FAILED (%d)\n" : "GNSS PWR TEST OK\n", fails);
	return fails != 0;
}
