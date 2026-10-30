/* Host test of nord2_cpuhvfs_init.h (step 3, README-0107-design.md 12.7).  The golden hashes come from gen_cpuhvfs_tables.py, which reimplements the vendor
 * driver's CSRAM initialisation independently in Python from the vendor table header. */
#include "nord2_cpuhvfs_init.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct model {
	uint8_t mem[N2DV_CSRAM_SIZE];
	unsigned stores;
	unsigned first_off[4];                 /* order of the first stores, for the sequence checks */
	uint8_t touched[N2DV_CSRAM_SIZE / 4];
};
static void w32(void *c, uint32_t off, uint32_t v)
{
	struct model *m = c;
	assert((off & 3u) == 0 && off + 4 <= N2DV_CSRAM_SIZE);      /* aligned, inside the 5 KiB CSRAM */
	for (unsigned i = 0; i < 4; i++) m->mem[off + i] = (uint8_t)(v >> (8 * i));
	if (m->stores < 4) m->first_off[m->stores] = off;
	m->stores++;
	m->touched[off / 4]++;
}
static const struct n2dv_ops ops = { w32 };
static void noise(struct model *m)
{
	memset(m, 0, sizeof(*m));
	for (unsigned i = 0; i < N2DV_CSRAM_SIZE / 4; i++) {
		uint32_t v = (uint32_t)(i * 2654435761u + 12345u);
		for (unsigned k = 0; k < 4; k++) m->mem[4 * i + k] = (uint8_t)(v >> (8 * k));
	}
}
static uint64_t fnv64(const uint8_t *b, unsigned n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (unsigned i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
	return h;
}
static uint32_t rd(const struct model *m, uint32_t off) { return (uint32_t)m->mem[off] | (uint32_t)m->mem[off + 1] << 8 | (uint32_t)m->mem[off + 2] << 16 | (uint32_t)m->mem[off + 3] << 24; }

int main(void)
{
	static struct model m;
	/* ---- the vendor data, spot-checked against README-0107-design.md section 3 (rows: MHz / mV = raw * 6.25) */
	assert(n2dv_l1_pvt[1][0] == 1800 && n2dv_l1_pvt[1][1] * 625u == 95000u);                      /* LL row 1: 1800 MHz, 950 mV */
	assert(n2dv_l1_pvt[0][0] == 2000 && n2dv_l1_pvt[0][1] * 625u == 100000u);                     /* LL top */
	assert(n2dv_l1_pvt[16][0] == 2600 && n2dv_l1_pvt[16][1] * 625u == 105000u);                   /* L top */
	assert(n2dv_l1_pvt[32][0] == 3000 && n2dv_l1_pvt[32][1] * 625u == 105000u);                   /* B top */
	assert(n2dv_l1_pvt[48][0] == 1700 && n2dv_l1_pvt[48][1] * 625u == 100000u);                   /* CCI top */
	assert(n2dv_l1_pvt[15][0] == 500 && n2dv_l1_pvt[15][1] * 625u == 65000u);
	for (unsigned c = 0; c < 4; c++) {
		for (unsigned i = 0; i < 16; i++) {
			const uint16_t *r = n2dv_l1_pvt[16 * c + i];
			assert(r[1] * 625u >= N2DV_MIN_VPROC && r[1] * 625u <= N2DV_MAX_VPROC);             /* every table voltage inside the platform clamp */
			if (i) assert(r[0] <= n2dv_l1_pvt[16 * c + i - 1][0] && r[1] <= n2dv_l1_pvt[16 * c + i - 1][1]);   /* rows get slower and lower */
		}
	}
	for (unsigned i = 0; i < 32; i++) for (unsigned j = 0; j < 16; j++) assert(n2dv_l1_cci[i][j] < 16);   /* a CCI row index */

	/* ---- level decode (stock Image: this phone's devinfo[7] low byte is 0x40) */
	assert(n2dv_level_from_devinfo(0x40, 0) == 1 && n2dv_level_from_devinfo(0x10, 0xffffffffu) == 0);
	assert(n2dv_level_from_devinfo(0x00, 1u << 13) == 1 && n2dv_level_from_devinfo(0x00, 0) == 0 && n2dv_level_from_devinfo(0xabcd1240u, 0) == 1);

	/* ---- the init stores: golden image, default and pinned */
	struct n2dv_plan p = { .level = 1, .pin = false, .row = { 0, 0, 0 } };
	noise(&m);
	assert(n2dv_csram_init(&ops, &m, &p) == 0);
	assert(fnv64(m.mem, N2DV_CSRAM_SIZE) == N2DV_GOLDEN_FNV64_DEFAULT);
	/* the words the vendor loop writes, independent of the hash */
	assert(rd(&m, 0) == N2DV_GUARD0 && rd(&m, 4) == N2DV_GUARD1 && rd(&m, 8) == N2DV_GUARD0 && rd(&m, 12) == N2DV_GUARD1);
	assert(rd(&m, N2DV_OFF_DATA) == N2DV_GUARD0 && rd(&m, N2DV_OFF_DATA + 4) == 0 && rd(&m, N2DV_CSRAM_SIZE - 4) == 0);
	assert(rd(&m, N2DV_OFF_PPM_LIMIT) == 15 && rd(&m, N2DV_OFF_PPM_LIMIT + 4) == 15 && rd(&m, N2DV_OFF_PPM_LIMIT + 8) == 15 && rd(&m, N2DV_OFF_PPM_LIMIT + 12) == 0);
	assert(rd(&m, 0x10) == (160u << 16 | 2000u) && rd(&m, 0x14) == (152u << 16 | 1800u));                 /* LL rows 0, 1 */
	assert(rd(&m, 0x10 + 4 * 16) == (1u << 16 | 1u) && rd(&m, 0x10 + 4 * 32) == 0xffffffffu && rd(&m, 0x10 + 4 * 33) == 0);
	assert(rd(&m, 0x10 + 4 * 36) == (168u << 16 | 2600u) && rd(&m, 0x10 + 4 * 72) == (168u << 16 | 3000u) && rd(&m, 0x10 + 4 * 108) == (160u << 16 | 1700u));
	assert(rd(&m, 0x10 + 4 * 68) == 0xffffffffu && rd(&m, 0x10 + 4 * 104) == 0xffffffffu && rd(&m, 0x10 + 4 * 140) == 0xffffffffu);
	assert(m.mem[N2DV_OFF_CCI_MAP] == 1 && m.mem[N2DV_OFF_CCI_MAP + 1] == 2 && m.mem[N2DV_OFF_CCI_MAP + 8] == 8);   /* first CCI row of the vendor map */
	/* what init must NOT touch: the request and current-row windows are only zeroed in the default plan (the vendor zero loop), the words between the table end and
	 * the data area stay as they were, and no store is outside the CSRAM */
	assert(rd(&m, 0x250) == (uint32_t)(0x250 / 4 * 2654435761u + 12345u) && rd(&m, 0x29c) == (uint32_t)(0x29c / 4 * 2654435761u + 12345u));
	assert(rd(&m, N2DV_OFF_REQ) == 0 && rd(&m, N2DV_OFF_REQ + 12) == 0);                                  /* default plan: request 0 = the FASTEST row */
	/* every touched word was stored exactly once except the data-area guard (zero then guard) */
	for (unsigned i = 0; i < N2DV_CSRAM_SIZE / 4; i++) {
		unsigned want = (i * 4 == N2DV_OFF_DATA) ? 2 : 1;
		if (i * 4 < N2DV_OFF_TBL) want = i < 4 ? 1 : 0;
		if (i * 4 >= N2DV_OFF_TBL_END && i * 4 < N2DV_OFF_DATA) want = 0;
		if (i * 4 >= N2DV_OFF_TBL && i * 4 < N2DV_OFF_TBL_END) want = 1;
		if (i * 4 >= N2DV_OFF_PPM_LIMIT && i * 4 < N2DV_OFF_PPM_LIMIT + 12) want = 1;                  /* the default limits are stored once, inside the zero loop */
		if (i * 4 >= N2DV_OFF_CCI_MAP && i * 4 < N2DV_OFF_CCI_MAP + N2DV_CCI_MAP_BYTES) want = 2;     /* zeroed, then written (the vendor memset + fill) */
		if (i * 4 >= N2DV_OFF_DATA && i * 4 != N2DV_OFF_DATA && !(i * 4 >= N2DV_OFF_CCI_MAP && i * 4 < N2DV_OFF_CCI_MAP + N2DV_CCI_MAP_BYTES)) want = 1;
		assert(m.touched[i] == want);
	}

	/* pinned plan: min = max = row, request words written, nothing else different from the default plan */
	struct model d = m;
	struct n2dv_plan q = { .level = 1, .pin = true, .row = { 1, 14, 15 } };
	noise(&m);
	assert(n2dv_csram_init(&ops, &m, &q) == 0);
	assert(fnv64(m.mem, N2DV_CSRAM_SIZE) == N2DV_GOLDEN_FNV64_PINNED_1_14_15);
	assert(rd(&m, N2DV_OFF_PPM_LIMIT) == (1u << 16 | 1u) && rd(&m, N2DV_OFF_PPM_LIMIT + 4) == (14u << 16 | 14u) && rd(&m, N2DV_OFF_PPM_LIMIT + 8) == (15u << 16 | 15u));
	assert(rd(&m, N2DV_OFF_REQ) == 1 && rd(&m, N2DV_OFF_REQ + 4) == 14 && rd(&m, N2DV_OFF_REQ + 8) == 15 && rd(&m, N2DV_OFF_REQ + 12) == n2dv_l1_cci[1][15]);
	assert(n2dv_l1_cci[1][15] == 10 && n2dv_l1_pvt[48 + 10][0] == 895);                                    /* CCI row 10 = 895 MHz (vendor map for LL row 1, B row 15) */
	unsigned diff_words = 0;
	for (unsigned off = 0; off < N2DV_CSRAM_SIZE; off += 4) diff_words += rd(&m, off) != rd(&d, off);
	assert(diff_words == 3 + 4);                                                                           /* 3 PPM words + 4 request words, nothing else */
	/* pinned plan: the limit and request windows get their pinned value in the single store of the zero loop; no store of 0 into them, ever */
	{
		struct model z; noise(&z);
		struct { unsigned seen; unsigned zero_to_req; } cnt = { 0, 0 };
		(void)cnt;
		memset(z.touched, 0, sizeof(z.touched));
		assert(n2dv_csram_init(&ops, &z, &q) == 0);
		for (unsigned i = 0; i < 4; i++) assert(z.touched[N2DV_OFF_REQ / 4 + i] == 1 && z.touched[N2DV_OFF_PPM_LIMIT / 4 + (i < 3 ? i : 0)] == 1);
		assert(rd(&z, N2DV_OFF_REQ) == 1 && rd(&z, N2DV_OFF_REQ + 4) == 14 && rd(&z, N2DV_OFF_REQ + 8) == 15 && rd(&z, N2DV_OFF_REQ + 12) == 10);
	}
	/* bad plans write nothing */
	struct n2dv_plan bad[] = { { .level = 0 }, { .level = 2 }, { .level = 1, .pin = true, .row = { 16, 0, 0 } }, { .level = 1, .pin = true, .row = { 0, 0, 200 } } };
	for (unsigned i = 0; i < 4; i++) { noise(&m); assert(n2dv_csram_init(&ops, &m, &bad[i]) == -1 && m.stores == 0); }
	noise(&m); assert(n2dv_csram_init(0, &m, &p) == -1 && n2dv_csram_init(&ops, &m, 0) == -1 && m.stores == 0);

	/* ---- rows for the bootloader's clocks (README section 3: LL ~1791, L and B ~730 MHz) */
	assert(n2dv_row_for_mhz(N2DV_LL, 1791) == 1 && n2dv_row_for_mhz(N2DV_LL, 1793) == 1 && n2dv_row_for_mhz(N2DV_LL, 2000) == 0 && n2dv_row_for_mhz(N2DV_LL, 505) == 15);
	assert(n2dv_row_for_mhz(N2DV_L, 730) == 14);                       /* 700 MHz (rows 14 and 15 are both 700: the first is taken) */
	assert(n2dv_row_for_mhz(N2DV_B, 730) == 15);                       /* 659 MHz: the only row not above 730 */
	assert(n2dv_row_for_mhz(N2DV_B, 652) == -1 && n2dv_row_for_mhz(N2DV_B, 653) == 15 && n2dv_row_for_mhz(N2DV_LL, 100) == -1 && n2dv_row_for_mhz(N2DV_CCI, 1700) == -1 && n2dv_row_for_mhz(7, 1) == -1);
	assert(n2dv_row_for_mhz(N2DV_LL, 1800) == 1 && n2dv_row_for_mhz(N2DV_LL, 1783) == 1 && n2dv_row_for_mhz(N2DV_LL, 1782) == 2);   /* 1 % tolerance: 1800 <= 1783 * 1.01, not 1782 * 1.01 */

	/* ---- messages */
	uint32_t msg[4];
	n2dv_msg_init_ptbl(msg); assert(msg[0] == 0 && msg[1] == 0 && msg[2] == 0 && msg[3] == 0);
	n2dv_msg_init_sta(msg, 1); assert(msg[0] == 1 && msg[1] == 1 && msg[2] == 0);
	/* the rails the P18 probe read on the device (c6t): LL Vproc 1000.00 / Vsram 1000.00 mV; L at its 700 MHz row Vproc 650.00 / Vsram 750.00 mV */
	assert(n2dv_msg_init_volt(msg, 0, 100000, 100000) && msg[0] == 10 && msg[1] == 0 && msg[2] == 100000 && msg[3] == 100000);
	assert(n2dv_msg_init_volt(msg, 1, 65000, 75000) && msg[1] == 1 && msg[2] == 65000 && msg[3] == 75000);
	assert(n2dv_msg_init_volt(msg, 2, 65000, 75000) && n2dv_msg_init_volt(msg, 3, 100000, 100000));
	assert(!n2dv_msg_init_volt(msg, 4, 75000, 85000));                       /* no cluster 4 */
	/* the stock rule: Vsram >= Vproc and the difference at most 250 mV; the floors only reject garbage */
	assert(n2dv_rails_ok(65000, 75000) && n2dv_rails_ok(100000, 100000) && n2dv_rails_ok(75000, 100000) && n2dv_rails_ok(75000, 100000 - 0) && n2dv_rails_ok(80000, 105000));
	assert(!n2dv_rails_ok(75000, 74999) && !n2dv_rails_ok(75000, 100001) && n2dv_rails_ok(75000, 100000) && !n2dv_rails_ok(80000, 105001));
	assert(!n2dv_rails_ok(49999, 60000) && !n2dv_rails_ok(50000 - 1, 50000) && n2dv_rails_ok(50000, 50000) && !n2dv_rails_ok(112001, 112001) && n2dv_rails_ok(112000, 112000));
	assert(!n2dv_rails_ok(65000, 49999) && !n2dv_rails_ok(0, 0) && !n2dv_rails_ok(65000, 112001) && !n2dv_rails_ok(100000, 112001 + 25000));
	assert(n2dv_msg_init_volt(msg, 3, 87000, 112000) && !n2dv_msg_init_volt(msg, 3, 86999, 112000));

	/* ---- PMIC decode */
	assert(n2dv_vproc_10uv(0xa0) == 100000u && n2dv_vproc_10uv(0x30) == 30000u && n2dv_vproc_10uv(0xbf) == 119375u && n2dv_vproc_10uv(0x2f) == 0 && n2dv_vproc_10uv(0xc0) == 0);
	assert(n2dv_vsram_10uv(0x00) == 50000u && n2dv_vsram_10uv(0x6f) == 119375u && n2dv_vsram_10uv(0x70) == 0 && n2dv_vsram_10uv(0x38) == 85000u);
	assert(n2dv_vproc_10uv(160) == (uint32_t)n2dv_l1_pvt[0][1] * 625u);       /* the table's raw volt is the PMIC selector */
	assert(!n2dv_msg_init_volt(msg, 0, n2dv_vproc_10uv(0xbf), 112000));       /* 1.19375 V is above the clamp: refused */
	struct n2dv_rail_regs a = n2dv_cluster_rails(N2DV_LL), b = n2dv_cluster_rails(N2DV_L), c = n2dv_cluster_rails(N2DV_B), e = n2dv_cluster_rails(N2DV_CCI);
	assert(a.vproc_sid == 7 && a.vproc_reg == 0x1599 && a.vsram_reg == 0x1eb0 && memcmp(&a, &e, sizeof(a)) == 0);
	assert(b.vproc_sid == 6 && b.vproc_reg == 0x1499 && b.vsram_reg == 0x1e90 && memcmp(&b, &c, sizeof(b)) == 0);

	/* ---- the later stores */
	noise(&m); n2dv_sched_disable(&ops, &m); assert(m.stores == 1 && rd(&m, N2DV_OFF_SCHED_DIS) == 1);
	noise(&m); for (unsigned i = 0; i < 5; i++) n2dv_cluster_on(&ops, &m, i);
	assert(m.stores == 4 && rd(&m, N2DV_OFF_CLUSTER_ONOFF) == 0x43434f4eu && rd(&m, N2DV_OFF_CLUSTER_ONOFF + 12) == 0x43434f4eu);
	const uint8_t rows[3] = { 1, 14, 15 };
	noise(&m); n2dv_fallback_pin(&ops, &m, rows);
	assert(m.stores == 6 && rd(&m, N2DV_OFF_PPM_LIMIT + 4) == (14u << 16 | 14u) && rd(&m, N2DV_OFF_REQ + 8) == 15);
	puts("nord2_cpuhvfs_init_test OK");
	return 0;
}
