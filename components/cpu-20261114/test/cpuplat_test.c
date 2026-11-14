/* 0641 host test: nord2_dvfs.h CCI choice and store order.  cc -I<piece>/bsd/dev -I<stage>/bsd/dev cpuplat_test.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nord2_dvfs.h"

#define MAXW 4096
static struct { uint32_t off, v; } tr[MAXW];
static unsigned ntr;
static void w32(void *c, uint32_t off, uint32_t v) { (void)c; if (ntr < MAXW) { tr[ntr].off = off; tr[ntr].v = v; } ntr++; }
static uint32_t r32(void *c, uint32_t off) { (void)c; (void)off; return 0; }
static void sl(void *c, unsigned ms) { (void)c; (void)ms; }
static struct n2d_ops ops;
static int fails;
#define CHECK(x, ...) do { if (!(x)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* the pre-0641 n2d_set_row_fast store sequence (copied from the a/ copy of nord2_dvfs.h) */
static void
old_set_row_fast(uint8_t cur[3], unsigned c, unsigned row)
{
	while (cur[c] != row) {
		unsigned step = cur[c] > row ? (unsigned)cur[c] - 1u : (unsigned)cur[c] + 1u;
		uint8_t next[3] = { cur[0], cur[1], cur[2] };
		next[c] = (uint8_t)step;
		uint32_t cci = n2dv_cci_row(next[N2DV_LL], next[N2DV_B]);
		if (step < cur[c]) w32(0, N2DV_OFF_REQ + 4 * N2DV_CCI, cci);
		w32(0, N2DV_OFF_PPM_LIMIT + 4 * c, (uint32_t)step << 16 | step);
		w32(0, N2DV_OFF_REQ + 4 * c, step);
		if (step >= cur[c]) w32(0, N2DV_OFF_REQ + 4 * N2DV_CCI, cci);
		cur[c] = (uint8_t)step;
	}
}

static unsigned mhz_b(unsigned r) { return n2dv_l1_pvt[16 * N2DV_B + r][0]; }
static unsigned mhz_l(unsigned r) { return n2dv_l1_pvt[16 * N2DV_L + r][0]; }

int
main(void)
{
	ops.w32 = w32; ops.r32 = r32; ops.sleep_ms = sl;
	/* 1: mode 0 == n2dv_cci_row for every row triple (perf flag ignored without the bit) */
	for (unsigned ll = 0; ll < 16; ll++) for (unsigned l = 0; l < 16; l++) for (unsigned b = 0; b < 16; b++) {
		uint8_t r[3] = { (uint8_t)ll, (uint8_t)l, (uint8_t)b };
		n2p_mode = 0; n2p_perf_now = true;
		CHECK(n2p_cci_for(r) == n2dv_cci_row(ll, b), "mode0 %u %u %u", ll, l, b);
		/* 2: COL never slower than the old rule, and only rows the old rule can pick for this LL row */
		n2p_mode = N2P_COL; n2p_perf_now = false;
		uint32_t c = n2p_cci_for(r);
		CHECK(c <= n2dv_cci_row(ll, b), "col slower %u %u %u: %u > %u", ll, l, b, c, n2dv_cci_row(ll, b));
		bool seen = false;
		for (unsigned bb = 0; bb < 16; bb++) seen |= n2dv_cci_row(ll, bb) == c;
		CHECK(seen, "col picked a new row %u", c);
		/* the column clock covers both big clusters */
		unsigned col = n2p_b_row_for(mhz_l(l) > mhz_b(b) ? mhz_l(l) : mhz_b(b));
		CHECK(mhz_b(col) >= mhz_l(l) && mhz_b(col) >= mhz_b(b), "column clock %u", col);
		/* 3: perf half never slower than normal, rows within 0..15 */
		n2p_mode = N2P_COL | N2P_PERF; n2p_perf_now = true;
		uint32_t p = n2p_cci_for(r);
		CHECK(p <= c && p < 16, "perf %u vs %u", p, c);
		CHECK(p == n2dv_l1_cci[16 + ll][n2p_b_row_for(mhz_l(l) > mhz_b(b) ? mhz_l(l) : mhz_b(b))], "perf row is not the vendor perf map entry");
		n2p_perf_now = false;
		CHECK(n2p_cci_for(r) == c, "perf off != col");
	}
	/* known points: UI floors (LL 1800 row 1, L 2200 row 3, B 1998 row 7) */
	{
		uint8_t r[3] = { 1, 3, 7 };
		n2p_mode = 0; printf("UI floors: old CCI row %u (%u MHz)", n2p_cci_for(r), n2dv_l1_pvt[48 + n2p_cci_for(r)][0]);
		n2p_mode = N2P_COL; printf(", col %u (%u MHz)", n2p_cci_for(r), n2dv_l1_pvt[48 + n2p_cci_for(r)][0]);
		r[0] = 0; n2p_mode = N2P_COL | N2P_LL; printf(", +LL %u (%u MHz)", n2p_cci_for(r), n2dv_l1_pvt[48 + n2p_cci_for(r)][0]);
		n2p_mode = 7; n2p_perf_now = true; printf(", +perf %u (%u MHz)\n", n2p_cci_for(r), n2dv_l1_pvt[48 + n2p_cci_for(r)][0]);
		uint8_t j[3] = { 1, 0, 0 };
		n2p_mode = 0; printf("cap rows: old %u (%u MHz)", n2p_cci_for(j), n2dv_l1_pvt[48 + n2p_cci_for(j)][0]);
		n2p_mode = 7; n2p_perf_now = true; j[0] = 0; printf(", all bits %u (%u MHz)\n", n2p_cci_for(j), n2dv_l1_pvt[48 + n2p_cci_for(j)][0]);
	}
	/* 4: mode 0 store sequence of n2d_set_row_fast == the pre-0641 one, random walks of L, B and LL */
	srand(12345);
	for (unsigned it = 0; it < 200000; it++) {
		struct n2d_state st; memset(&st, 0, sizeof(st)); st.inited = true;
		uint8_t old[3];
		for (unsigned k = 0; k < 3; k++) old[k] = st.cur[k] = (uint8_t)(rand() % 16);
		n2p_mode = 0; n2p_perf_now = rand() & 1; n2p_cci_last = (rand() & 1) ? -1 : (int)n2dv_cci_row(st.cur[0], st.cur[2]);
		unsigned c = (unsigned)(rand() % 3), row = (unsigned)(rand() % 16);
		static uint32_t a[MAXW][2]; unsigned na;
		ntr = 0; old_set_row_fast(old, c, row); na = ntr; for (unsigned i = 0; i < na && i < MAXW; i++) { a[i][0] = tr[i].off; a[i][1] = tr[i].v; }
		ntr = 0; n2d_set_row_fast(&ops, 0, &st, c, row);
		CHECK(ntr == na, "len %u vs %u", ntr, na);
		for (unsigned i = 0; i < na && i < ntr; i++) CHECK(tr[i].off == a[i][0] && tr[i].v == a[i][1], "store %u differs", i);
		CHECK(st.cur[c] == row && old[c] == row, "row");
	}
	/* 5: any mode: the CCI is stored on every step, a faster CCI before the cluster store, a slower one after it; n2p_cci_last tracks it */
	for (unsigned it = 0; it < 200000; it++) {
		struct n2d_state st; memset(&st, 0, sizeof(st)); st.inited = true;
		for (unsigned k = 0; k < 3; k++) st.cur[k] = (uint8_t)(rand() % 16);
		n2p_mode = (unsigned)(rand() % 8); n2p_perf_now = rand() & 1;
		n2p_cci_last = (int)n2p_cci_for(st.cur);
		unsigned c = (unsigned)(rand() % 3), row = (unsigned)(rand() % 16);
		int prev = n2p_cci_last;
		ntr = 0; n2d_set_row_fast(&ops, 0, &st, c, row);
		unsigned i = 0;
		while (i < ntr) {
			/* one step = 3 stores; find the cluster REQ store */
			unsigned j = i; int cci_at = -1, req_at = -1; uint32_t cci = 0;
			for (; j < ntr && j < i + 3; j++) {
				if (tr[j].off == N2DV_OFF_REQ + 4 * N2DV_CCI) { cci_at = (int)j; cci = tr[j].v; }
				if (tr[j].off == N2DV_OFF_REQ + 4 * c) req_at = (int)j;
			}
			CHECK(cci_at >= 0 && req_at >= 0, "step without CCI or REQ");
			if ((int)cci < prev) CHECK(cci_at < req_at, "faster CCI after the cluster");
			if ((int)cci > prev) CHECK(cci_at > req_at, "slower CCI before the cluster");
			prev = (int)cci; i = j;
		}
		CHECK(n2p_cci_last == prev, "cci_last");
		CHECK(n2p_cci_last == (int)n2p_cci_for(st.cur), "cci_last != rule for the final rows");
		/* refresh: no store when nothing changed; one store after a perf flip that changes the row */
		ntr = 0; CHECK(!n2p_cci_refresh(&ops, 0, &st) && ntr == 0, "refresh stored without a change");
		n2p_perf_now = !n2p_perf_now;
		uint32_t want = n2p_cci_for(st.cur);
		ntr = 0; bool did = n2p_cci_refresh(&ops, 0, &st);
		CHECK(did == (want != (uint32_t)prev) && ntr == (did ? 1u : 0u), "refresh");
		if (did) CHECK(tr[0].off == N2DV_OFF_REQ + 4 * N2DV_CCI && tr[0].v == want, "refresh store");
	}
	/* 6: stopped state: refresh never stores */
	{ struct n2d_state st; memset(&st, 0, sizeof(st)); st.inited = true; st.stopped = true; n2p_mode = 7; n2p_cci_last = -1; ntr = 0;
	  CHECK(!n2p_cci_refresh(&ops, 0, &st) && ntr == 0, "stopped"); }
	printf(fails ? "CPUPLAT_HOST_TEST FAILED (%d)\n" : "CPUPLAT_HOST_TEST_OK\n", fails);
	return fails != 0;
}
