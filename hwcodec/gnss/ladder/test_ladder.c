#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "gnss_pwr.h"
#include "gnss_pwr.c"
#define printf(...) (verbose ? printf(__VA_ARGS__) : 0)
static int verbose, noack; static uint8_t sentbuf[8192]; static unsigned sent;
static void delay(unsigned us) { (void)us; }
typedef uintptr_t vm_offset_t;
#define __builtin_arm_dsb(x) __sync_synchronize()
#define VM_WIMG_IO 0
static vm_offset_t ml_io_map_unmappable(uint32_t pa, uint32_t sz, int f) { (void)pa; (void)sz; (void)f; static uint32_t pg[1024]; return (vm_offset_t)pg; }
static uint8_t ringwin[0x10000];
static int n2gd_ringwin(uint8_t **va) { *va = ringwin; return 0; }
struct { bool powered, hw_valid; int last_err; uint32_t conn_hw_ver; } n2gd;
/* register model */
#define NR 512
static uint32_t ra[NR], rv[NR]; static int nr; static int guard_fail_at = -1, accesses;
static uint32_t *R(uint32_t pa) { for (int i = 0; i < nr; i++) if (ra[i] == pa) return &rv[i]; ra[nr] = pa; rv[nr] = 0; return &rv[nr++]; }
static uint32_t model(uint32_t pa) {
	uint32_t v = *R(pa);
	if (pa == N2G_CSR_SLP_PROT_CTRL) return (*R(N2G_CSR_WAKEPU_GPS) & 1) || (*R(0x180601a0) & 1) ? (v & ~0x20u) : (v | 0x20u);
	if (pa == N2G_RGU_BGFSYS_PWR_ACK) return (*R(N2G_RGU_BGFSYS_PWR_CTL) & 0x80) && !noack ? (v | 3u << 24) : (v & ~(3u << 24));
	if (pa == N2G_CFG_GALS_CONN2GPS || pa == N2G_CFG_GALS_GPS2CONN) { v &= ~0x88u; v |= (v & 1) << 3; v |= (v & 0x10) << 3; return v; }
	if (pa == N2G_CSR_GPS_CFG2HOST_DBG) return (*R(N2G_CSR_HOST2GPS_DBG_SEL) & 0xff) == 0x80 && (*R(N2G_RGU_BGFSYS_PWR_CTL) & 0x80) ? 2 : 0x55;
	if (pa == N2G_WTSLP_WB_STA) return v & ~(1u << 26);
	bool l1 = (*R(0x18c00014) & 0x21) == 0x21;
	if (pa == 0x18c1011c || pa == 0x18c1021c) return *R(pa);
	if (pa == 0x18c10148 || pa == 0x18c10248) return *R(pa);
	if (pa == 0x18c01020) return l1 ? 3 : 0;
	if (pa >= 0x18c73000 && pa < 0x18c73060) { if (!l1) return 0xffffffffu; if (pa == 0x18c73024) return (*R(0x18c73020) & 8) ? 8 : 0; if (pa == 0x18c73004 && !*R(pa)) return 0x24010138; }
	return v;
}
static uint32_t rd(void *c, uint32_t pa) { (void)c; accesses++; if (!n2g_reg_allowed(pa)) { fprintf(stderr, "NOT ALLOWED rd %#x\n", pa); } return model(pa); }
static void wr(void *c, uint32_t pa, uint32_t v) { (void)c; accesses++; if (!n2g_reg_allowed(pa)) fprintf(stderr, "NOT ALLOWED wr %#x\n", pa);
	if (pa == N2G_RGU_BGFSYS_PWR_CTL) { if ((v >> 16) != 0x4254) { fprintf(stderr, "BGF write without key %#x\n", v); return; } v &= 0xffff; }
	if (pa == N2G_CFG_GALS_CONN2GPS || pa == N2G_CFG_GALS_GPS2CONN) v &= ~0x88u;
	if (pa >= 0x18c73000 && pa < 0x18c73060 && (*R(0x18c00014) & 0x21) != 0x21) return;
	if (pa == 0x18c7300c) { *R(pa) &= ~(v & 2u); return; }   /* STA: NODAINTB w1c, rest RO */   /* USRT in reset: writes lost */
	if (pa == 0x18c00014 && (v & 0x21) != 0x21) for (uint32_t u = 0x18c73000; u < 0x18c73060; u += 4) *R(u) = 0;   /* L1 reset clears USRT */
	*R(pa) = v;
	/* DMA model: START.STR on DMA1 (A2D) consumes COUNT words from PGMADDR; the DSP then has a 24-byte reply (TX_IND);
	 * START on DMA2 (D2A) writes the reply at PGMADDR and leaves RLCT = COUNT - 6 */
	if (pa == 0x18c10118 && (v & 0x8000)) { uint32_t off = *R(0x18c1012c) - 0x78000000u; sent = 0; for (uint32_t i = 0; i < (*R(0x18c10110) & 0xffff) * 4; i++) sentbuf[sent++] = ringwin[off + i];
		*R(0x18c1011c) = 0x8000; *R(0x18c10148) = 1; *R(0x18c7300c) |= 8; }
	if (pa == 0x18c10218 && (v & 0x8000)) { uint32_t off = *R(0x18c1022c) - 0x78000000u; for (int i = 0; i < 24; i++) ringwin[off + i] = (uint8_t)(0xa0 + i);
		*R(0x18c10224) = (*R(0x18c10210) & 0xffff) - 6; *R(0x18c1021c) = 0x8000; *R(0x18c10248) = 1; *R(0x18c7300c) &= ~8u; }
	if ((pa == 0x18c10118 || pa == 0x18c10218) && !(v & 0x8000)) { *R(pa + 4) = 0; *R(pa + 0x30) = 0; }
}
static bool n2gd_wifi_guard(uint32_t g[9]) { g[7] = 1u << 16 | 1; g[8] = 6; return guard_fail_at < 0 || accesses < guard_fail_at; }
static int n2gd_hw(struct n2g_hw *h) { h->ctx = 0; h->read32 = rd; h->write32 = wr; h->emi_base_pa = 0x8a000000u; return 0; }
#include "ladder_part.c"
static void reset(void) { nr = 0; accesses = 0; *R(0x180601a0) = 1; *R(N2G_CSR_WAKEPU_GPS) = 0; *R(N2G_CFG_GALS_CONN2GPS) = 0x11; *R(N2G_CFG_GALS_GPS2CONN) = 0x11;
	*R(N2G_CSR_CONN2AP_GPS_EMI) = 0x12345; *R(N2G_CSR_HOST2GPS_DBG_SEL) = 0x3; *R(N2G_GPS_AON_DSLEEP_CTL) = 0x0b; *R(N2G_CONN_HW_VER) = 0x20010101; *R(N2G_RGU_BGFSYS_PWR_CTL) = 0x0; n2gd.powered = 0; }
int main(int argc, char **argv) {
	verbose = argc > 1; int bad = 0;
	for (int k = 1; k <= 22; k++) { if (k == 21) continue;
		reset(); uint32_t sa[NR], sv[NR]; int sn;
		for (int i = 0; i < nr; i++) { sa[i] = ra[i]; sv[i] = rv[i]; } sn = nr;
		uint32_t r = n2gd_ladder(k);
		int diff = 0;
		for (int i = 0; i < nr; i++) { uint32_t o = 0; for (int j = 0; j < sn; j++) if (sa[j] == ra[i]) o = sv[j]; if (rv[i] != o && !(ra[i] >= 0x18c10100 && ra[i] < 0x18c10300 && ra[i] != 0x18c10118 && ra[i] != 0x18c10218)) { diff++; if (k != 22) fprintf(stderr, "k=%d reg %#x %#x -> %#x\n", k, ra[i], o, rv[i]); } }
		int expect_ok = (r & 0xff) == 1 && !(r >> 24);
		fprintf(stderr, "k=%2d result=%#x regs changed=%d accesses=%d %s\n", k, r, diff, accesses, (expect_ok && (k == 22 ? diff > 0 && n2gd.powered && !(*R(N2G_CSR_WAKEPU_GPS) & 1) : diff == 0)) ? "OK" : "BAD");
		if (!(expect_ok && (k == 22 ? diff > 0 : diff == 0))) bad++;
	}
	reset(); verbose = 0; n2gd_ladder(19);
	{ int good = sent == 16 && !memcmp(sentbuf, n2gl_frame1, 15) && sentbuf[15] == 0 && n2gl_rx.wr == 24 && ringwin[0x2000] == 0xa0 && ringwin[0x2000 + 23] == 0xa0 + 23;
	  fprintf(stderr, "frame/reply: sent=%u rx=%u %s\n", sent, n2gl_rx.wr, good ? "OK" : "BAD"); if (!good) bad++; }
	/* guard refusal mid-run: stops, no access after */
	reset(); guard_fail_at = 30; uint32_t r = n2gd_ladder(13); int a = accesses;
	fprintf(stderr, "guard-stop result=%#x accesses=%d %s\n", r, a, (r >> 24) == 1 && a <= 31 ? "OK" : "BAD"); if (!((r >> 24) == 1 && a <= 31)) bad++;
	guard_fail_at = -1;
	for (int k = 7; k <= 17; k += 10) { reset(); noack = 1; uint32_t sa[NR], sv[NR]; int sn = nr; for (int i = 0; i < nr; i++) { sa[i] = ra[i]; sv[i] = rv[i]; }
		uint32_t r2 = n2gd_ladder(k); int diff = 0;
		for (int i = 0; i < nr; i++) { uint32_t o = 0; for (int j = 0; j < sn; j++) if (sa[j] == ra[i]) o = sv[j]; if (rv[i] != o) diff++; }
		int good = r2 == ((7u << 8) | (2u << 16)) && diff == 0 && !n2gd.powered;
		fprintf(stderr, "noack k=%d result=%#x changed=%d %s\n", k, r2, diff, good ? "OK" : "BAD"); if (!good) bad++; noack = 0; }
	fprintf(stderr, bad ? "LADDER SIM FAILED\n" : "LADDER SIM OK\n"); return bad;
}
