/* SPDX-License-Identifier: MIT */
/*
 * t_unit.c -- unit tests of the WP03 H.264 encoder core, with expectations taken literally from ITU-T H.264
 * (03/2010), hand-computed from its tables, or checked against the structures they describe.  Nothing here
 * mirrors the implementation: the bit strings, the coefficient tables and the transform expectations were written
 * out by hand from the Recommendation.
 */
#include "n2avc_priv.h"
#include "n2avc_tables.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                                   \
	checks++;                                                               \
	if (!(cond)) {                                                          \
		failures++;                                                     \
		fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
		fprintf(stderr, __VA_ARGS__);                                   \
		fprintf(stderr, "\n");                                          \
	}                                                                       \
} while (0)

static void bits_of(const uint8_t *buf, size_t n, char *out)
{
	size_t i;
	int k;
	for (i = 0; i < n; i++)
		for (k = 7; k >= 0; k--)
			*out++ = (char)('0' + ((buf[i] >> k) & 1));
	*out = 0;
}

/* ---------------------------------------------------------------- clause 9.1 */
static void test_exp_golomb(void)
{
	static const struct { uint32_t v; const char *bits; } ue[] = {
		{ 0, "1" }, { 1, "010" }, { 2, "011" }, { 3, "00100" }, { 4, "00101" },
		{ 5, "00110" }, { 6, "00111" }, { 7, "0001000" }, { 8, "0001001" },
		{ 30, "000011111" }, { 31, "00000100000" }
	};
	static const struct { int32_t v; const char *bits; } se[] = {
		{ 1, "010" }, { -1, "011" }, { 2, "00100" }, { -2, "00101" },
		{ 3, "00110" }, { -3, "00111" }, { 4, "0001000" }, { -4, "0001001" }
	};
	uint8_t buf[16];
	char bits[128];
	n2bw w;
	size_t i;

	for (i = 0; i < sizeof ue / sizeof ue[0]; i++) {
		n2bw_init(&w, buf, sizeof buf);
		n2bw_ue(&w, ue[i].v);
		n2bw_trailing(&w);
		(void)n2bw_flush(&w);
		bits_of(buf, w.pos, bits);
		CHECK(strncmp(bits, ue[i].bits, strlen(ue[i].bits)) == 0,
		      "ue(%u): got %s want %s", ue[i].v, bits, ue[i].bits);
	}
	for (i = 0; i < sizeof se / sizeof se[0]; i++) {
		n2bw_init(&w, buf, sizeof buf);
		n2bw_se(&w, se[i].v);
		n2bw_trailing(&w);
		(void)n2bw_flush(&w);
		bits_of(buf, w.pos, bits);
		CHECK(strncmp(bits, se[i].bits, strlen(se[i].bits)) == 0,
		      "se(%d): got %s want %s", se[i].v, bits, se[i].bits);
	}

	/* rbsp_trailing_bits after three bits: the stop bit, then zeros to the byte boundary (7.3.2.11). */
	n2bw_init(&w, buf, sizeof buf);
	n2bw_u(&w, 3, 5);                       /* 101 */
	n2bw_trailing(&w);
	CHECK(n2bw_flush(&w) == 1, "trailing: one byte");
	bits_of(buf, 1, bits);
	CHECK(strcmp(bits, "10110000") == 0, "rbsp_trailing_bits: %s", bits);

	/* A write that does not fit sets overflow, and later writes are ignored. */
	n2bw_init(&w, buf, 1);
	n2bw_u(&w, 8, 0xab);
	n2bw_u(&w, 8, 0xcd);
	CHECK(w.overflow == 1, "overflow flag");
	CHECK(n2bw_flush(&w) == 1, "overflow: only the byte that fitted");

	/* n2bw_bits counts bits. */
	n2bw_init(&w, buf, sizeof buf);
	n2bw_u(&w, 5, 3);
	CHECK(n2bw_bits(&w) == 5, "5 bits");
	n2bw_u(&w, 11, 0x7ff);
	CHECK(n2bw_bits(&w) == 16 && w.pos == 2, "16 bits in 2 bytes");
}

/* clause 7.4.1: emulation_prevention_three_byte. */
static void test_emulation(void)
{
	static const uint8_t rbsp[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
	static const uint8_t want[] = { 0x65, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x01 };
	static const uint8_t rbsp2[] = { 0x00, 0x00, 0x02, 0x00, 0x00, 0x03, 0xff, 0x80 };
	static const uint8_t want2[] = { 0x65, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x03, 0x03, 0xff, 0x80 };
	uint8_t out[64];
	size_t n;

	n = n2avc_nal(out, sizeof out, rbsp, sizeof rbsp, 3, 5);
	CHECK(n == sizeof want && memcmp(out, want, n) == 0, "00 00 00 00 00 01 -> 00 00 03 00 00 03 00 01");

	n = n2avc_nal(out, sizeof out, rbsp2, sizeof rbsp2, 3, 5);
	CHECK(n == sizeof want2 && memcmp(out, want2, n) == 0, "00 00 02 / 00 00 03 example");

	n = n2avc_nal(out, sizeof out, rbsp, 1, 3, 7);
	CHECK(n == 2 && out[0] == 0x67 && out[1] == 0, "header 0x67 (nal_ref_idc 3, SPS)");

	CHECK(n2avc_nal(out, 2, rbsp, sizeof rbsp, 3, 5) == 0, "no room: 0, not a truncated NAL");
}

/* ---------------------------------------------------------------- tables 9-5 .. 9-10 */
static int prefix_free(const n2vlc *cw, int n, const char *what, int col)
{
	int i, j, k;
	for (i = 0; i < n; i++) {
		char a[32];
		if (!cw[i].len)
			continue;
		for (k = 0; k < cw[i].len; k++)
			a[k] = (char)('0' + ((cw[i].code >> (cw[i].len - 1 - k)) & 1));
		a[cw[i].len] = 0;
		for (j = 0; j < n; j++) {
			char b[32];
			if (i == j || !cw[j].len || cw[j].len <= cw[i].len)
				continue;
			for (k = 0; k < cw[j].len; k++)
				b[k] = (char)('0' + ((cw[j].code >> (cw[j].len - 1 - k)) & 1));
			b[cw[j].len] = 0;
			if (strncmp(a, b, (size_t)cw[i].len) == 0) {
				fprintf(stderr, "prefix clash in %s column %d: %s is a prefix of %s\n",
					what, col, a, b);
				return 0;
			}
		}
	}
	return 1;
}

static void test_tables(void)
{
	int cls, tc, to, k, z, ok, count;

	/* Table 9-5: each column (fixed nC class and TrailingOnes) is one prefix-free code. */
	for (cls = 0; cls < 6; cls++)
		for (to = 0; to < 4; to++) {
			n2vlc col[17];
			for (tc = 0; tc < 17; tc++)
				col[tc] = n2avc_cw_coeff_token[cls][tc][to];
			CHECK(prefix_free(col, 17, "table 9-5", cls * 4 + to),
			      "table 9-5 class %d TrailingOnes %d is not prefix free", cls, to);
		}

	/* Coverage: for nC >= 0, exactly the 62 valid (TotalCoeff, TrailingOnes) pairs have a code. */
	for (cls = 0; cls < 4; cls++) {
		count = 0;
		for (tc = 0; tc <= 16; tc++)
			for (to = 0; to < 4; to++) {
				int valid = (tc == 0) ? (to == 0) : (to <= (tc < 3 ? tc : 3));
				if (valid) {
					CHECK(n2avc_cw_coeff_token[cls][tc][to].len > 0,
					      "table 9-5 class %d TC %d TO %d missing", cls, tc, to);
					count++;
				} else {
					CHECK(n2avc_cw_coeff_token[cls][tc][to].len == 0,
					      "table 9-5 class %d TC %d TO %d should not exist", cls, tc, to);
				}
			}
		CHECK(count == 62, "table 9-5 class %d covers 62 pairs, got %d", cls, count);
	}
	/* nC == -1: chroma DC, TotalCoeff 0..4 only. */
	for (tc = 0; tc <= 16; tc++)
		for (to = 0; to < 4; to++) {
			int valid = (tc <= 4) && ((tc == 0) ? (to == 0) : (to <= (tc < 3 ? tc : 3)));
			if (valid)
				CHECK(n2avc_cw_coeff_token[4][tc][to].len > 0,
				      "table 9-5 chromaDC TC %d TO %d missing", tc, to);
			else
				CHECK(n2avc_cw_coeff_token[4][tc][to].len == 0,
				      "table 9-5 chromaDC TC %d TO %d should not exist", tc, to);
		}

	ok = 1;
	for (tc = 1; tc <= 15; tc++)
		if (!prefix_free(n2avc_cw_total_zeros[tc], 16, "tables 9-7/9-8", tc))
			ok = 0;
	CHECK(ok, "tables 9-7/9-8 prefix free per tzVlcIndex");
	/* Complete: total_zeros 0 .. 16 - tzVlcIndex have codes, nothing beyond. */
	for (tc = 1; tc <= 15; tc++)
		for (k = 0; k < 16; k++) {
			int valid = k <= 16 - tc;
			CHECK((n2avc_cw_total_zeros[tc][k].len > 0) == valid,
			      "table 9-7/9-8 tz %d zeros %d: %s", tc, k,
			      valid ? "missing" : "should not exist");
		}

	ok = 1;
	for (tc = 1; tc <= 3; tc++)
		if (!prefix_free(n2avc_cw_total_zeros_dc[tc], 4, "table 9-9(a)", tc))
			ok = 0;
	CHECK(ok, "table 9-9(a) prefix free");
	for (tc = 1; tc <= 3; tc++)
		for (k = 0; k < 4; k++)
			CHECK((n2avc_cw_total_zeros_dc[tc][k].len > 0) == (k <= 4 - tc),
			      "table 9-9(a) tz %d zeros %d", tc, k);

	ok = 1;
	for (z = 0; z < 8; z++) {
		n2vlc col[15];
		for (k = 0; k < 15; k++)
			col[k] = n2avc_cw_run_before[k][z];
		if (!prefix_free(col, 15, "table 9-10", z))
			ok = 0;
	}
	CHECK(ok, "table 9-10 prefix free per zerosLeft class");

	/* Literal entries, quoted from the tables. */
	CHECK(n2avc_cw_coeff_token[0][5][3].len == 7 && n2avc_cw_coeff_token[0][5][3].code == 0x04,
	      "table 9-5: TC 5 / TO 3, nC < 2 = 0000100");
	CHECK(n2avc_cw_coeff_token[0][0][0].len == 1 && n2avc_cw_coeff_token[0][0][0].code == 1,
	      "table 9-5: TC 0 / TO 0, nC < 2 = 1");
	CHECK(n2avc_cw_coeff_token[4][1][1].len == 1 && n2avc_cw_coeff_token[4][1][1].code == 1,
	      "table 9-5: TC 1 / TO 1, nC == -1 = 1");
	CHECK(n2avc_cw_total_zeros[5][3].len == 3 && n2avc_cw_total_zeros[5][3].code == 7,
	      "table 9-7: tzVlcIndex 5 / total_zeros 3 = 111");
	CHECK(n2avc_cw_total_zeros[1][0].len == 1 && n2avc_cw_total_zeros[1][0].code == 1,
	      "table 9-7: tzVlcIndex 1 / total_zeros 0 = 1");
	CHECK(n2avc_cw_total_zeros[11][4].len == 1 && n2avc_cw_total_zeros[11][4].code == 1,
	      "table 9-8: tzVlcIndex 11 / total_zeros 4 = 1");
	CHECK(n2avc_cw_total_zeros_dc[1][3].len == 3 && n2avc_cw_total_zeros_dc[1][3].code == 0,
	      "table 9-9(a): tzVlcIndex 1 / total_zeros 3 = 000");
	CHECK(n2avc_cw_run_before[1][3].len == 2 && n2avc_cw_run_before[1][3].code == 2,
	      "table 9-10: run 1 / zerosLeft 3 = 10");
	CHECK(n2avc_cw_run_before[0][2].len == 1 && n2avc_cw_run_before[0][2].code == 1,
	      "table 9-10: run 0 / zerosLeft 2 = 1");
	CHECK(n2avc_cw_run_before[1][2].len == 2 && n2avc_cw_run_before[1][2].code == 1,
	      "table 9-10: run 1 / zerosLeft 2 = 01");
	CHECK(n2avc_cw_run_before[7][7].len == 4 && n2avc_cw_run_before[7][7].code == 1,
	      "table 9-10: run 7 / zerosLeft > 6 = 0001");
	CHECK(n2avc_cw_run_before[14][7].len == 11 && n2avc_cw_run_before[14][7].code == 1,
	      "table 9-10: run 14 / zerosLeft > 6 = 00000000001");
}

/* ---------------------------------------------------------------- CAVLC coding */
static void test_cavlc_blocks(void)
{
	/* The textbook block: coefficients at scan positions 1, 3, 4, 5, 7 with the values 3, 1, -1, -1, 1,
	 * so TotalCoeff 5, TrailingOnes 3 and total_zeros 3 (the zeros up to the highest coefficient, position 7).
	 * Bit by bit: coeff_token 0000100 (table 9-5, nC < 2); signs 0 1 1; level +1 -> 1; level +3 -> 0010;
	 * total_zeros 3 at tzVlcIndex 5 -> 111 (table 9-7); run_before 1/zerosLeft 3 -> 10, 0/2 -> 1, 0/2 -> 1,
	 * 1/2 -> 01 (table 9-10).  These are exactly the 24 bits of the worked example. */
	static const int16_t coef[16] = { 0, 3, 0, 1, -1, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
	static const char want[] = "000010001110010111101101";
	uint8_t buf[16];
	char bits[256];
	n2bw w;

	n2bw_init(&w, buf, sizeof buf);
	n2avc_write_residual(&w, coef, 16, 0, 0);
	n2bw_trailing(&w);
	(void)n2bw_flush(&w);
	bits_of(buf, w.pos, bits);
	CHECK(strncmp(bits, want, strlen(want)) == 0, "worked CAVLC block: got %s want %s", bits, want);
	CHECK(w.pos * 8 == 32, "24 bits plus rbsp_trailing_bits: %zu", w.pos * 8);

	/* The same block with nC >= 8: coeff_token(5,3) = 4 * (5 - 1) + 3 = 19 = 010011 (table 9-5, nC >= 8 is a
	 * 6-bit fixed length code).  The rest of the block is unchanged. */
	{
		static const char want8[] = "01001101110010111101101";
		n2bw_init(&w, buf, sizeof buf);
		n2avc_write_residual(&w, coef, 16, 0, 12);
		n2bw_trailing(&w);
		(void)n2bw_flush(&w);
		bits_of(buf, w.pos, bits);
		CHECK(strncmp(bits, want8, strlen(want8)) == 0, "nC >= 8 coeff_token: got %s want %s", bits, want8);
	}

	/* Chroma DC (nC == -1, maxNumCoeff 4), one coefficient of value 1 at position 0: coeff_token(1,1) = 1,
	 * sign 0, total_zeros 0 at tzVlcIndex 1 = 1 (table 9-9(a)). */
	{
		static const int16_t dc[4] = { 1, 0, 0, 0 };
		n2bw_init(&w, buf, sizeof buf);
		n2avc_write_residual(&w, dc, 4, 0, -1);
		n2bw_trailing(&w);
		(void)n2bw_flush(&w);
		bits_of(buf, w.pos, bits);
		CHECK(strncmp(bits, "101", 3) == 0, "chroma DC block: got %s want 101", bits);
	}

	/* All-zero block: coeff_token(0,0) = 1, nothing else. */
	{
		int16_t zero[16];
		memset(zero, 0, sizeof zero);
		n2bw_init(&w, buf, sizeof buf);
		n2avc_write_residual(&w, zero, 16, 0, 0);
		n2bw_trailing(&w);
		(void)n2bw_flush(&w);
		CHECK(w.pos == 1 && buf[0] == 0xc0, "all-zero block: got %02x, want 1100 0000", buf[0]);
	}

	/* Intra16x16 AC block (start index 1): the coefficient at the block's scan position 1, i.e. the first
	 * of the 15 AC positions.  coeff_token(1,0) = 000101 (nC < 2); the level +2 is the first non-trailing
	 * level with fewer than three trailing ones, so clause 9.2.2 step 7 applies (code 0 -> 1); total_zeros 0
	 * at tzVlcIndex 1 = 1 (table 9-7). */
	{
		int16_t ac[16];
		memset(ac, 0, sizeof ac);
		ac[1] = 2;
		n2bw_init(&w, buf, sizeof buf);
		n2avc_write_residual(&w, ac, 16, 1, 0);
		n2bw_trailing(&w);
		(void)n2bw_flush(&w);
		bits_of(buf, w.pos, bits);
		CHECK(strncmp(bits, "00010111", 8) == 0, "AC block with the bias: %s", bits);
	}
}

/* ---------------------------------------------------------------- 8.5 transforms */
static void test_transform(void)
{
	int v, x, y;

	/* Forward + quantisation + inverse of a flat block of value v at QP 0 returns v exactly. */
	for (v = 0; v <= 255; v++) {
		uint8_t src[16], pred[16], rec[16];
		int16_t W[16], zz[16];
		memset(pred, 0, sizeof pred);
		for (x = 0; x < 16; x++)
			src[x] = (uint8_t)v;
		n2avc_fwd4x4(src, 4, pred, 4, W);
		CHECK(W[0] == 16 * v, "flat block DC: W00 = 16 * v (v=%d, got %d)", v, W[0]);
		for (x = 1; x < 16; x++)
			CHECK(W[x] == 0, "flat block: W[%d] = %d", x, W[x]);
		n2avc_quant4x4(W, 0, 1, zz);
		n2avc_inv4x4(zz, 0, pred, 4, rec, 4);
		for (y = 0; y < 4; y++)
			for (x = 0; x < 4; x++)
				CHECK(rec[y * 4 + x] == v, "flat block round trip v=%d at (%d,%d): %d",
				      v, x, y, rec[y * 4 + x]);
	}

	/* The forward transform of an impulse is the outer product of the basis vector:
	 * X = 200 * e(1,1) gives W[i][j] = 200 * c[i] * c[j] with c = (1, 1, -1, -2). */
	{
		uint8_t src[16], pred[16];
		int16_t W[16];
		static const int c[4] = { 1, 1, -1, -2 };
		memset(src, 0, sizeof src);
		memset(pred, 0, sizeof pred);
		src[1 * 4 + 1] = 200;
		n2avc_fwd4x4(src, 4, pred, 4, W);
		for (y = 0; y < 4; y++)
			for (x = 0; x < 4; x++)
				CHECK(W[y * 4 + x] == 200 * c[y] * c[x], "impulse transform (%d,%d): %d != %d",
				      x, y, W[y * 4 + x], 200 * c[y] * c[x]);
	}

	/* Intra_16x16 DC of sixteen blocks whose DC is 16 (a flat block of 1): the Hadamard gives 256 at [0][0];
	 * the quantiser at QP 0 gives level 25 ((256 * 13107 + 2^17/3) >> 17, the Hadamard's gain of 16 being
	 * taken out of MF); the inverse gives (25 * 160 + 32) >> 6 = 63, i.e. 64 * the block residual of 1. */
	{
		int16_t dc[16], lvl[16];
		int back[16];
		int32_t Wd[16];
		for (x = 0; x < 16; x++)
			dc[x] = 16;
		n2avc_fwd_dc16(dc, Wd);
		CHECK(Wd[0] == 256, "DC Hadamard: %d", Wd[0]);
		for (x = 1; x < 16; x++)
			CHECK(Wd[x] == 0, "DC Hadamard: %d at %d", Wd[x], x);
		n2avc_quant_dc16(Wd, 0, 1, lvl);
		CHECK(lvl[0] == 25, "DC level at QP 0: %d", lvl[0]);
		n2avc_inv_dc16(lvl, 0, back);
		for (x = 0; x < 16; x++)
			CHECK(back[x] == 63, "DC inverse: %d at %d", back[x], x);
	}

	/* Chroma DC: a constant array of 20 gives 80 at [0][0] and zero elsewhere (2x2 Hadamard). */
	{
		int16_t dc[4] = { 20, 20, 20, 20 }, W[4], lvl[4];
		int back[4];
		n2avc_fwd_dc4(dc, W);
		CHECK(W[0] == 80 && W[1] == 0 && W[2] == 0 && W[3] == 0, "2x2 Hadamard: %d %d %d %d",
		      W[0], W[1], W[2], W[3]);
		n2avc_quant_dc4(W, 0, 1, lvl);
		n2avc_inv_dc4(lvl, 0, back);
		CHECK(back[0] == back[1] && back[1] == back[2] && back[2] == back[3],
		      "2x2 inverse is constant: %d %d %d %d", back[0], back[1], back[2], back[3]);
		/* (80 * 13107 + 2^16/3) >> 16 = 16 with the 2x2 gain of 4 taken out; the inverse gives
		 * (16 * 160) >> 5 = 80, the DC of the four chroma blocks again. */
		CHECK(lvl[0] == 16, "chroma DC level: %d", lvl[0]);
		CHECK(back[0] == 80, "chroma DC round trip: %d", back[0]);
	}

	/* Table 8-15 with chroma_qp_index_offset 0. */
	{
		static const int want[52] = {
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
			25, 26, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38,
			39, 39, 39, 39
		};
		for (v = 0; v <= 51; v++)
			CHECK(n2avc_qpc(v) == want[v], "QPC(%d) = %d, want %d", v, n2avc_qpc(v), want[v]);
	}

	/* SAD. */
	{
		uint8_t a[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
		uint8_t b[16];
		for (x = 0; x < 16; x++)
			b[x] = (uint8_t)(a[x] + (x & 1));
		CHECK(n2avc_sad(a, 4, b, 4, 4, 4) == 8, "SAD of eight unit differences: %d",
		      n2avc_sad(a, 4, b, 4, 4, 4));
	}
}

/* ---------------------------------------------------------------- table 9-4 */
static void test_cbp(void)
{
	/* Table 9-4 (a), column "Inter", written out from the Recommendation as codeNum -> coded_block_pattern
	 * (ChromaArrayType 1 or 2).  n2avc_cbp_inter_code() is the inverse mapping. */
	static const uint8_t inter[48] = {
		0, 16, 1, 2, 4, 8, 32, 3, 5, 10, 12, 15, 47, 7, 11, 13, 14, 6, 9, 31, 35, 37, 42, 44,
		33, 34, 36, 40, 39, 43, 45, 46, 17, 18, 20, 24, 19, 21, 26, 28, 23, 27, 29, 30, 22, 25, 38, 41
	};
	int code;

	for (code = 0; code < 48; code++)
		CHECK(n2avc_cbp_inter_code(inter[code]) == (uint32_t)code,
		      "cbp %d must be code %d, got %u", inter[code], code,
		      n2avc_cbp_inter_code(inter[code]));
	CHECK(n2avc_cbp_inter_code(47) == 12, "cbp 47 -> code 12");
	CHECK(n2avc_cbp_inter_code(15) == 11, "cbp 15 -> code 11");
	CHECK(n2avc_cbp_inter_code(16) == 1, "cbp 16 -> code 1");
}

/* ---------------------------------------------------------------- nC, clause 9.2.1 */
static void test_nc(void)
{
	n2avc_mbinfo mb[4];
	n2avc_slice s;

	memset(&s, 0, sizeof s);
	memset(mb, 0, sizeof mb);
	s.mb = mb;
	s.mb_w = 2;
	s.mb_h = 2;
	s.first_row = 0;
	s.end_row = 2;

	/* MBs: 0 = (0,0), 1 = (1,0), 2 = (0,1), 3 = (1,1). */
	mb[0].nnz[3] = 4;                       /* (0,0), block (3,0) */
	mb[0].nnz[12] = 7;                      /* (0,0), block (0,3) */
	mb[1].nnz[0] = 2;
	mb[1].nnz[12] = 2;
	mb[2].nnz[0] = 6;
	mb[2].nnz[3] = 6;

	/* (1,1) block 0: left (0,1) block (3,0) = 6, above (1,0) block (0,3) = 2 -> (6 + 2 + 1) >> 1 = 4. */
	CHECK(n2avc_block_nc(&s, 1, 1, 0) == 4, "nC(1,1,0) = %d", n2avc_block_nc(&s, 1, 1, 0));
	/* (1,0) block 0: left (0,0) block (3,0) = 4, above is the picture edge, so nC = nA = 4. */
	CHECK(n2avc_block_nc(&s, 1, 0, 0) == 4, "nC(1,0,0) = %d", n2avc_block_nc(&s, 1, 0, 0));
	/* (0,0) block 0: nothing available. */
	CHECK(n2avc_block_nc(&s, 0, 0, 0) == 0, "nC(0,0,0) = %d", n2avc_block_nc(&s, 0, 0, 0));
	/* (0,0) block 4 is below block 0 (whose nnz we set to 3) and on the left edge. */
	mb[0].nnz[0] = 3;
	CHECK(n2avc_block_nc(&s, 0, 0, 4) == 3, "nC(0,0,4) = %d", n2avc_block_nc(&s, 0, 0, 4));
	/* Chroma: (1,1) block 16 (Cb (0,0)): left = (0,1) Cb (1,0) = block 17 = 5,
	 * above = (1,0) Cb (0,1) = block 18 = 1 -> (5 + 1 + 1) >> 1 = 3. */
	mb[2].nnz[17] = 5;
	mb[1].nnz[18] = 1;
	CHECK(n2avc_block_nc(&s, 1, 1, 16) == 3, "nC chroma (1,1,16) = %d", n2avc_block_nc(&s, 1, 1, 16));
	/* A macroblock of another slice is not available (clause 6.4.9): with the slice starting at row 1 the
	 * macroblock above (0,1) is outside it, so nC of (0,1) block 0 has no neighbour at all. */
	s.first_row = 1;
	s.end_row = 2;
	CHECK(n2avc_block_nc(&s, 0, 1, 0) == 0, "nC with the above macroblock in another slice = %d",
	      n2avc_block_nc(&s, 0, 1, 0));
	CHECK(n2avc_block_nc(&s, 1, 1, 0) == 6, "nC inside the slice (left only) = %d",
	      n2avc_block_nc(&s, 1, 1, 0));
}

int main(void)
{
	test_exp_golomb();
	test_emulation();
	test_tables();
	test_cavlc_blocks();
	test_transform();
	test_cbp();
	test_nc();
	if (failures) {
		printf("N2TEST WP03-unit FAIL %d of %d checks\n", failures, checks);
		return 1;
	}
	printf("N2TEST WP03-unit PASS %d/%d\n", checks, checks);
	return 0;
}
