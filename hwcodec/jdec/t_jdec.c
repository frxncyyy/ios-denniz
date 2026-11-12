/* SPDX-License-Identifier: MIT */
/*
 * t_jdec.c -- host test for the J04 parser and word generator.
 *
 * Two modes:
 *   t_jdec corpus <corpus.txt> <dir-with-hand-*.jpg>
 *   t_jdec selftest
 *
 * The corpus file is produced by gen_jpegs.py, whose expectations come from djpeg -verbose -verbose
 * and ffprobe (independent oracles) plus the T.81 formulas written out again in Python. The hand
 * literals below were computed by hand from mtk_jpeg_dec_hw.c and are cited line by line in
 * REGISTERS.md; they do not come from this implementation.
 */
#include "n2jdec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_checks;
static unsigned g_fails;

#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { \
		g_fails++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

static uint8_t *
read_file(const char *path, size_t *len)
{
	FILE *f;
	long n;
	uint8_t *b;

	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	n = ftell(f);
	if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	b = (uint8_t *)malloc((size_t)n + 1u);
	if (b == NULL) {
		fclose(f);
		return NULL;
	}
	if (fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return b;
}

static uint8_t *
find_marker(uint8_t *b, size_t len, uint8_t m)
{
	size_t i;

	for (i = 0; i + 1 < len; i++) {
		if (b[i] == 0xff && b[i + 1] == m)
			return b + i;
	}
	return NULL;
}

static uint32_t
align128(uint32_t v)
{
	return (v + 127u) & ~127u;
}

static uint32_t
align16(uint32_t v)
{
	return (v + 15u) & ~15u;
}

static int
find_reg(const n2jdec_reg *r, unsigned n, uint16_t off, uint32_t *value)
{
	unsigned i;
	int found = 0;

	for (i = 0; i < n; i++) {
		if (r[i].off == off) {
			if (found == 0 && value != NULL)
				*value = r[i].value;
			found++;
		}
	}
	return found;
}

/* ---- hand-computed word tests ------------------------------------------------------------ */
static void
hand_words(const char *dir, const char *name, uint32_t exp_du, uint32_t exp_mcu,
	   uint32_t exp_unit, uint32_t du0_w, uint32_t du0_h, uint32_t du1_w, uint32_t du1_h,
	   uint32_t exp_comp_id, uint32_t exp_qt, uint32_t exp_stride_y,
	   uint32_t exp_stride_uv, uint32_t exp_du_ctrl, uint32_t exp_brz, uint32_t exp_wdma)
{
	char path[1024];
	uint8_t *b;
	size_t len;
	n2jdec_jpeg j;
	n2jdec_addr a;
	n2jdec_words w;
	n2jdec_reg sup[32];
	unsigned nsup = 0;
	uint32_t v = 0;
	int rc;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	b = read_file(path, &len);
	CHECK(b != NULL, "read %s", path);
	if (b == NULL)
		return;
	rc = n2jdec_parse(b, len, &j);
	CHECK(rc == N2JDEC_OK, "%s parse rc=%d (%s)", name, rc, n2jdec_strerror(rc));
	if (rc != N2JDEC_OK) {
		free(b);
		return;
	}

	a.src_pa = 0xd8000000u;
	a.out_y = 0xd8800000u;
	a.out_u = 0xd8830000u;
	a.out_v = 0xd8848000u;
	a.index_pa = 0xd8f00000u;
	a.index_size = 0x4000u;
	rc = n2jdec_fill_words(&j, &a, &w);
	CHECK(rc == N2JDEC_OK, "%s fill_words rc=%d", name, rc);
	if (rc != N2JDEC_OK) {
		free(b);
		return;
	}

	CHECK(w.data[0] == 0x00000000u, "%s data[0]=%08x", name, w.data[0]);
	CHECK(w.data[1] == 0x00000010u, "%s data[1]=%08x", name, w.data[1]);
	CHECK(w.data[2] == exp_du, "%s data[2]=%03x want %03x", name, w.data[2], exp_du);
	CHECK(w.data[3] == 0xd8800000u, "%s data[3]=%08x", name, w.data[3]);
	CHECK(w.data[4] == 0xd8830000u, "%s data[4]=%08x", name, w.data[4]);
	CHECK(w.data[5] == 0xd8848000u, "%s data[5]=%08x", name, w.data[5]);
	CHECK(w.data[6] == 0x00000000u, "%s data[6]=%08x", name, w.data[6]);
	CHECK(w.data[7] == 0xd8000000u, "%s data[7]=%08x", name, w.data[7]);
	CHECK(w.data[8] == exp_comp_id, "%s data[8]=%08x want %08x", name, w.data[8], exp_comp_id);
	CHECK(w.data[9] == exp_mcu - 1u, "%s data[9]=%u want %u", name, w.data[9], exp_mcu - 1u);
	CHECK(w.data[10] == exp_unit - 1u, "%s data[10]=%u want %u", name, w.data[10], exp_unit - 1u);
	CHECK(w.data[11] == exp_du_ctrl, "%s data[11]=%08x want %08x", name, w.data[11], exp_du_ctrl);
	CHECK(w.data[12] == align128((uint32_t)len), "%s data[12]=%u want %u",
	      name, w.data[12], align128((uint32_t)len));
	CHECK(w.data[13] == exp_qt, "%s data[13]=%04x want %04x", name, w.data[13], exp_qt);
	CHECK(w.data[14] == 0u, "%s data[14]=%08x", name, w.data[14]);
	CHECK(w.data[15] == exp_stride_y, "%s data[15]=%u want %u", name, w.data[15], exp_stride_y);
	CHECK(w.data[16] == exp_stride_uv, "%s data[16]=%u want %u", name, w.data[16], exp_stride_uv);
	CHECK(w.data[17] == 0u, "%s data[17]=%08x", name, w.data[17]);
	CHECK(w.data[18] == exp_mcu - 1u, "%s data[18]=%u want %u", name, w.data[18], exp_mcu - 1u);
	CHECK(w.data[19] == 0u, "%s data[19]=%08x", name, w.data[19]);
	CHECK(w.data[20] == 0x4000u, "%s data[20]=%u", name, w.data[20]);
	CHECK(w.index_pa[0] == 0xd8f00000u && w.index_pa[1] == 0xd8f00000u &&
	      w.index_pa[2] == 0xd8f04000u && w.index_pa[3] == 0xd8f08000u,
	      "%s index pointers %08x %08x %08x %08x", name,
	      w.index_pa[0], w.index_pa[1], w.index_pa[2], w.index_pa[3]);
	CHECK(w.total_mcu == exp_mcu && w.unit_num == exp_unit, "%s total/unit %u/%u",
	      name, w.total_mcu, w.unit_num);
	CHECK(j.comp[0].du_w == du0_w && j.comp[0].du_h == du0_h &&
	      j.comp[0].du_count == du0_w * du0_h, "%s comp0 du %ux%u", name,
	      j.comp[0].du_w, j.comp[0].du_h);
	if (j.ncomp >= 2)
		CHECK(j.comp[1].du_w == du1_w && j.comp[1].du_h == du1_h,
		      "%s comp1 du %ux%u", name, j.comp[1].du_w, j.comp[1].du_h);

	CHECK(w.known_mask == 0x00103fbcu, "%s known_mask=%08x", name, w.known_mask);
	CHECK(w.hypothesis_mask == 0x000fc043u, "%s hypothesis_mask=%08x", name,
	      w.hypothesis_mask);
	CHECK((w.known_mask & w.hypothesis_mask) == 0u &&
	      (w.known_mask | w.hypothesis_mask) == 0x001fffffu, "%s masks", name);
	rc = n2jdec_supplementary(&j, &a, sup, 32, &nsup);
	CHECK(rc == N2JDEC_OK && nsup <= 32, "%s supplementary rc=%d n=%u", name, rc, nsup);
	CHECK(find_reg(sup, nsup, 0x0f8, &v) == 1 && v == (exp_brz << 8), "%s 0x0f8=%03x", name, v);
	CHECK(find_reg(sup, nsup, 0x16c, &v) == 1 && v == exp_wdma, "%s 0x16c=%08x", name, v);
	CHECK(find_reg(sup, nsup, 0x170, &v) == 1 && v == exp_mcu, "%s 0x170=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x158, &v) == 1 && v == exp_stride_y, "%s 0x158=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x15c, &v) == 1 && v == exp_stride_uv, "%s 0x15c=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x160, &v) == 1 && v == exp_stride_y, "%s 0x160=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x164, &v) == 1 && v == exp_stride_uv, "%s 0x164=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x210, &v) == 1 && v == exp_mcu - 1u, "%s 0x210=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x224, &v) == 1 && v == exp_unit - 1u, "%s 0x224=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x23c, &v) == 1 && v == exp_du_ctrl, "%s 0x23c=%08x", name, v);
	CHECK(find_reg(sup, nsup, 0x270, &v) == 1 && v == exp_qt, "%s 0x270=%04x", name, v);
	CHECK(find_reg(sup, nsup, 0x320, &v) == 1 && v == 0u, "%s 0x320=%u", name, v);
	CHECK(find_reg(sup, nsup, 0x248, &v) == 1 && v == 0xd8000000u + align16((uint32_t)len),
	      "%s 0x248=%08x", name, v);
	CHECK(find_reg(sup, nsup, 0x240, &v) == 1 && v == 0u, "%s 0x240=%u", name, v);
	free(b);
}

static void
selftest_tables(void)
{
	const n2jdec_word_info *t;
	const n2jdec_word_info *ix;
	unsigned n = 0, nx = 0, i;
	static const uint16_t offs[N2JDEC_WORDS] = {
		0x090, 0x090, 0x0fc, 0x14c, 0x150, 0x154, 0x17c, 0x200, 0x20c, 0x210, 0x224,
		0x23c, 0x24c, 0x270, 0x31c, 0x330, 0x334, 0x33c, 0x344, 0x240, 0x000
	};
	unsigned hyp = 0;
	unsigned mask = 0;

	t = n2jdec_word_table(&n);
	CHECK(n == N2JDEC_WORDS, "word table n=%u", n);
	for (i = 0; i < n; i++) {
		CHECK(t[i].slot == i, "word %u slot=%u", i, t[i].slot);
		CHECK(t[i].off == offs[i], "word %u off=%03x want %03x", i, t[i].off, offs[i]);
		CHECK(t[i].name != NULL && t[i].name[0] != 0, "word %u name", i);
		CHECK(t[i].note != NULL && t[i].note[0] != 0, "word %u note", i);
		CHECK(t[i].verdict == N2JDEC_KNOWN || t[i].verdict == N2JDEC_HYPOTHESIS,
		      "word %u verdict", i);
		if (t[i].verdict == N2JDEC_HYPOTHESIS) {
			hyp++;
			CHECK(i == 0 || i == 1 || i == 6 || (i >= 14 && i <= 19),
			      "unexpected hypothesis word %u", i);
		}
	}
	CHECK(hyp == 9, "hypothesis words=%u", hyp);

	ix = n2jdec_index_table(&nx);
	CHECK(nx == N2JDEC_INDEX_REGS, "index table n=%u", nx);
	{
		static const uint16_t ioffs[N2JDEC_INDEX_REGS] = { 0x338, 0x36c, 0x370, 0x374 };
		for (i = 0; i < nx; i++) {
			CHECK(ix[i].slot == 21 + i, "index %u slot", i);
			CHECK(ix[i].off == ioffs[i], "index %u off=%03x", i, ix[i].off);
			CHECK(ix[i].verdict == N2JDEC_HYPOTHESIS, "index %u verdict", i);
			CHECK(ix[i].name != NULL && ix[i].note != NULL, "index %u text", i);
		}
	}

	/* every data[] slot 0..20 must be produced by the table or by the unknown function */
	for (i = 0; i < n; i++)
		mask |= 1u << t[i].slot;
	CHECK(mask == 0x1fffffu, "slot mask=%05x", mask);
}

static void
selftest_unknown_and_addr(const char *dir)
{
	char path[1024];
	uint8_t *b;
	size_t len;
	n2jdec_jpeg j;
	n2jdec_addr a;
	n2jdec_words w;
	n2jdec_unknown u[16];
	unsigned n = 0, i;
	unsigned seen = 0;
	int rc;

	snprintf(path, sizeof(path), "%s/hand420.jpg", dir);
	b = read_file(path, &len);
	CHECK(b != NULL, "read %s", path);
	if (b == NULL)
		return;
	CHECK(n2jdec_parse(b, len, &j) == N2JDEC_OK, "hand420 parse");

	a.src_pa = 0xd8000000u;
	a.out_y = 0xd8800000u;
	a.out_u = 0xd8830000u;
	a.out_v = 0xd8848000u;
	a.index_pa = 0xd8f00000u;
	a.index_size = 0x4000u;

	rc = n2jdec_unknown_words(&j, &a, u, 16, &n);
	CHECK(rc == N2JDEC_OK, "unknown rc=%d", rc);
	CHECK(n == 13, "unknown count=%u", n);
	for (i = 0; i < n; i++) {
		CHECK(u[i].hypothesis != NULL && u[i].hypothesis[0] != 0, "unknown %u text", i);
		seen |= (u[i].slot < 21) ? (1u << u[i].slot) : (1u << 21);
	}
	CHECK((seen & 0x1fffffu) == ((1u << 0) | (1u << 1) | (1u << 6) | (0x1fu << 14) |
				    (1u << 19)), "unknown slot set %05x", seen & 0x1fffffu);
	CHECK((seen >> 21) == 1u, "index unknown entries %x", seen >> 21);
	CHECK(u[0].off == 0x090 && u[0].value == 0 && u[1].off == 0x090 && u[1].value == 0x10,
	      "reset entries");
	CHECK(u[6].off == 0x33c && u[6].value == 0, "0x33c entry");
	CHECK(u[7].off == 0x344 && u[7].value == j.total_mcu - 1u, "0x344 entry");
	CHECK(u[9].off == 0x338 && u[9].value == 0xd8f00000u, "0x338 entry");
	CHECK(u[12].off == 0x374 && u[12].value == 0xd8f08000u, "0x374 entry");

	CHECK(n2jdec_fill_words(&j, &a, &w) == N2JDEC_OK, "fill hand420");
	CHECK(w.data[15] == j.comp[0].stride, "data[15] stride");
	CHECK(w.data[16] == j.comp[1].stride, "data[16] stride");

	/* address validation */
	{
		n2jdec_addr bad = a;
		bad.src_pa = 0xd8000001u;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "unaligned src");
		bad = a;
		bad.out_y = 0xd8800008u;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "unaligned dst");
		bad = a;
		bad.out_u = 0;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "zero out_u");
		bad = a;
		bad.index_pa = 0xd8f00040u;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "unaligned index");
		bad = a;
		bad.index_size = 0x4001u;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "odd index size");
		bad = a;
		bad.index_size = 0;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "zero index size");
		bad = a;
		bad.src_pa = 0xffffff00u;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "src wrap");
		bad = a;
		bad.index_pa = 0xffffff00u;
		CHECK(n2jdec_fill_words(&j, &bad, &w) == N2JDEC_E_ADDR, "index wrap");
	}
	CHECK(n2jdec_fill_words(NULL, &a, &w) == N2JDEC_E_ARG, "null jpeg");
	CHECK(n2jdec_fill_words(&j, NULL, &w) == N2JDEC_E_ARG, "null addr");
	CHECK(n2jdec_fill_words(&j, &a, NULL) == N2JDEC_E_ARG, "null words");
	{
		n2jdec_jpeg j2 = j;
		j2.comp[0].h = 1;
		j2.comp[0].v = 1;
		j2.comp[2].h = 2;
		j2.comp[2].v = 2;
		CHECK(n2jdec_words_supported(&j2) == 0, "comp0 not the maximum");
		CHECK(n2jdec_fill_words(&j2, &a, &w) == N2JDEC_E_UNSUPPORTED,
		      "comp0 not the maximum fill");
	}
	free(b);
}

static void
selftest_negative(const char *dir)
{
	char path[1024];
	uint8_t *orig;
	size_t len, i;
	n2jdec_jpeg j;
	int rc;

	snprintf(path, sizeof(path), "%s/hand420.jpg", dir);
	orig = read_file(path, &len);
	CHECK(orig != NULL, "read %s", path);
	if (orig == NULL)
		return;

	/* progressive / extended frames */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		uint8_t *m;
		CHECK(b != NULL, "malloc");
		if (b != NULL) {
			memcpy(b, orig, len);
			m = find_marker(b, len, 0xc0);
			CHECK(m != NULL, "find SOF0");
			if (m != NULL) {
				m[1] = 0xc2;
				rc = n2jdec_parse(b, len, &j);
				CHECK(rc == N2JDEC_E_NOT_BASELINE && j.sof_marker == 0xc2,
				      "SOF2 rc=%d marker=%02x", rc, j.sof_marker);
				m[1] = 0xc1;
				rc = n2jdec_parse(b, len, &j);
				CHECK(rc == N2JDEC_E_NOT_BASELINE && j.sof_marker == 0xc1,
				      "SOF1 rc=%d marker=%02x", rc, j.sof_marker);
				m[1] = 0xc0;
			}
			free(b);
		}
	}

	/* precision 12 */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		uint8_t *m;
		memcpy(b, orig, len);
		m = find_marker(b, len, 0xc0);
		CHECK(m != NULL, "find SOF0 (precision)");
		if (m == NULL) { free(b); return; }
		m[4] = 12;
		CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_PRECISION, "precision 12");
		free(b);
	}

	/* two components (length adjusted so the component check is reached) */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		uint8_t *m;
		memcpy(b, orig, len);
		m = find_marker(b, len, 0xc0);
		CHECK(m != NULL, "find SOF0 (components)");
		if (m == NULL) { free(b); return; }
		m[9] = 2;
		m[2] = 0;
		m[3] = 14;
		CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_COMPONENTS, "2 components");
		free(b);
	}

	/* H=4,V=4 => 16 blocks > 10 */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		uint8_t *m;
		memcpy(b, orig, len);
		m = find_marker(b, len, 0xc0);
		CHECK(m != NULL, "find SOF0 (sampling)");
		if (m == NULL) { free(b); return; }
		m[11] = 0x44;
		CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_SAMPLING, "sampling 4x4");
		free(b);
	}

	/* duplicate component id */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		uint8_t *m;
		memcpy(b, orig, len);
		m = find_marker(b, len, 0xc0);
		CHECK(m != NULL, "find SOF0 (duplicate)");
		if (m == NULL) { free(b); return; }
		m[13] = m[10];
		CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_SOF, "duplicate id");
		free(b);
	}

	/* baseline scan parameters */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		uint8_t *m;
		memcpy(b, orig, len);
		m = find_marker(b, len, 0xda);
		CHECK(m != NULL, "find SOS");
		if (m != NULL) {
			unsigned ns = m[4];
			m[5 + 2 * ns] = 1;   /* Ss */
			CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_NOT_BASELINE, "Ss=1");
			m[5 + 2 * ns] = 0;
			m[4] = 4;            /* Ns */
			CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_SOS, "Ns=4");
		}
		free(b);
	}

	/* no EOI */
	CHECK(n2jdec_parse(orig, len - 2, &j) == N2JDEC_E_NO_EOI, "no EOI");

	/* segment length past the end */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		uint8_t *m;
		memcpy(b, orig, len);
		m = find_marker(b, len, 0xe0);
		if (m != NULL) {
			m[2] = 0xff;
			m[3] = 0xff;
			CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_TRUNC, "APP0 length");
		} else {
			CHECK(0, "no APP0");
		}
		free(b);
	}

	/* no DHT: change every FF C4 into FF FE (COM) */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		size_t k;
		memcpy(b, orig, len);
		for (k = 0; k + 1 < len; k++)
			if (b[k] == 0xff && b[k + 1] == 0xc4)
				b[k + 1] = 0xfe;
		CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_DHT, "missing DHT");
		free(b);
	}

	/* no DQT */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		size_t k;
		memcpy(b, orig, len);
		for (k = 0; k + 1 < len; k++)
			if (b[k] == 0xff && b[k + 1] == 0xdb)
				b[k + 1] = 0xfe;
		CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_DQT, "missing DQT");
		free(b);
	}

	/* bad SOI */
	{
		uint8_t *b = (uint8_t *)malloc(len);
		memcpy(b, orig, len);
		b[1] = 0xd9;
		CHECK(n2jdec_parse(b, len, &j) == N2JDEC_E_MARKER, "bad SOI");
		free(b);
	}

	/* all prefixes of a valid file must fail cleanly (no crash, no false success) */
	for (i = 0; i < len; i++) {
		rc = n2jdec_parse(orig, i, &j);
		CHECK(rc != N2JDEC_OK, "prefix %zu parsed as OK", i);
		if (rc == N2JDEC_OK)
			break;
	}

	/* deterministic fuzz: random bytes must never crash and essentially never parse */
	{
		uint32_t s = 0x12345678u;
		unsigned k;
		for (k = 0; k < 300; k++) {
			size_t n = (s % 512u) + 1u;
			uint8_t *b = (uint8_t *)malloc(n);
			size_t q;
			s = s * 1664525u + 1013904223u;
			for (q = 0; q < n; q++) {
				s = s * 1664525u + 1013904223u;
				b[q] = (uint8_t)(s >> 24);
			}
			rc = n2jdec_parse(b, n, &j);
			CHECK(rc != N2JDEC_OK, "random buffer %u parsed", k);
			free(b);
		}
	}

	free(orig);
}

/* ---- corpus driven by djpeg/ffprobe ------------------------------------------------------ */
static void
corpus(const char *corpus_path)
{
	FILE *f;
	char path[1024];
	unsigned w, h, ncomp, h0, v0, h1, v1, h2, v2;
	unsigned mcu_w, mcu_h, total, unit, restart, dqtmask, dhtmask, fmt, size, sampling;
	unsigned du_num, comp_id, qt_id, stride_y, stride_uv, words_ok;
	unsigned lines = 0;

	f = fopen(corpus_path, "r");
	CHECK(f != NULL, "open %s", corpus_path);
	if (f == NULL)
		return;

	while (fscanf(f, "%1023s %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u "
		      "%u %u %u %u %u %u",
		      path, &w, &h, &ncomp, &h0, &v0, &h1, &v1, &h2, &v2,
		      &mcu_w, &mcu_h, &total, &unit, &restart, &dqtmask, &dhtmask,
		      &fmt, &size, &sampling, &du_num, &comp_id, &qt_id,
		      &stride_y, &stride_uv, &words_ok) == 26) {
		uint8_t *b;
		size_t len;
		n2jdec_jpeg j;
		int rc;
		unsigned i;

		b = read_file(path, &len);
		CHECK(b != NULL, "read %s", path);
		if (b == NULL)
			continue;
		rc = n2jdec_parse(b, len, &j);
		CHECK(rc == N2JDEC_OK, "%s parse rc=%d (%s)", path, rc, n2jdec_strerror(rc));
		if (rc != N2JDEC_OK) {
			free(b);
			continue;
		}
		CHECK(j.width == w && j.height == h, "%s dims %ux%u want %ux%u",
		      path, j.width, j.height, w, h);
		CHECK(j.ncomp == ncomp, "%s ncomp %u want %u", path, j.ncomp, ncomp);
		CHECK(j.comp[0].h == h0 && j.comp[0].v == v0, "%s comp0 %ux%u",
		      path, j.comp[0].h, j.comp[0].v);
		CHECK((ncomp < 2) || (j.comp[1].h == h1 && j.comp[1].v == v1),
		      "%s comp1 %ux%u", path, j.comp[1].h, j.comp[1].v);
		CHECK((ncomp < 3) || (j.comp[2].h == h2 && j.comp[2].v == v2),
		      "%s comp2 %ux%u", path, j.comp[2].h, j.comp[2].v);
		CHECK(j.mcu_w == mcu_w && j.mcu_h == mcu_h, "%s mcu %ux%u want %ux%u",
		      path, j.mcu_w, j.mcu_h, mcu_w, mcu_h);
		CHECK(j.total_mcu == total, "%s total %u want %u", path, j.total_mcu, total);
		CHECK(j.unit_num == unit, "%s unit %u want %u", path, j.unit_num, unit);
		CHECK(j.restart_interval == restart, "%s restart %u want %u",
		      path, j.restart_interval, restart);
		{
			unsigned dm = 0, hm = 0;
			for (i = 0; i < 4; i++) {
				if (j.dqt_present[i])
					dm |= 1u << i;
				if (j.dc_present[i])
					hm |= 1u << i;
				if (j.ac_present[i])
					hm |= 1u << (4 + i);
			}
			CHECK(dm == dqtmask, "%s dqt %x want %x", path, dm, dqtmask);
			CHECK(hm == dhtmask, "%s dht %x want %x", path, hm, dhtmask);
		}
		CHECK(j.out_fmt == fmt, "%s fmt %u want %u", path, j.out_fmt, fmt);
		CHECK(j.sampling == sampling, "%s sampling %u want %u",
		      path, j.sampling, sampling);
		CHECK(j.file_bytes == size, "%s file_bytes %u want %u", path, j.file_bytes, size);
		CHECK(n2jdec_words_supported(&j) == (int)words_ok,
		      "%s words_supported %d want %u", path, n2jdec_words_supported(&j), words_ok);
		if (words_ok) {
			n2jdec_addr a;
			n2jdec_words wj;
			a.src_pa = 0xd8000000u;
			a.out_y = 0xd8800000u;
			a.out_u = 0xd8830000u;
			a.out_v = 0xd8848000u;
			a.index_pa = 0xd8f00000u;
			a.index_size = 0x4000u;
			rc = n2jdec_fill_words(&j, &a, &wj);
			CHECK(rc == N2JDEC_OK, "%s fill rc=%d", path, rc);
			if (rc == N2JDEC_OK) {
				CHECK(wj.data[2] == du_num, "%s data[2]=%03x want %03x",
				      path, wj.data[2], du_num);
				CHECK(wj.data[8] == comp_id, "%s comp_id=%08x want %08x",
				      path, wj.data[8], comp_id);
				CHECK(wj.data[13] == qt_id, "%s qt_id=%04x want %04x",
				      path, wj.data[13], qt_id);
				CHECK(wj.data[15] == stride_y, "%s stride_y=%u want %u",
				      path, wj.data[15], stride_y);
				CHECK(wj.data[16] == stride_uv, "%s stride_uv=%u want %u",
				      path, wj.data[16], stride_uv);
				CHECK(wj.data[12] == align128((uint32_t)len), "%s file size word",
				      path);
			}
		} else {
			n2jdec_addr a;
			n2jdec_words wj;
			a.src_pa = 0xd8000000u;
			a.out_y = 0xd8800000u;
			a.out_u = 0xd8830000u;
			a.out_v = 0xd8848000u;
			a.index_pa = 0xd8f00000u;
			a.index_size = 0x4000u;
			CHECK(n2jdec_fill_words(&j, &a, &wj) == N2JDEC_E_UNSUPPORTED,
			      "%s unsupported fill", path);
		}
		free(b);
		lines++;
		(void)w;
	}
	fclose(f);
	CHECK(lines >= 30, "corpus has %u lines", lines);
}

int
main(int argc, char **argv)
{
	if (argc == 4 && strcmp(argv[1], "corpus") == 0) {
		selftest_tables();
		hand_words(argv[3], "hand420.jpg", 0x500u, 4u, 9u, 4u, 4u, 2u, 2u,
			   0x01020300u, 0x0011u, 32u, 16u, 0x7fff5924u, 0u, 0x00070001u);
		hand_words(argv[3], "hand444.jpg", 0x000u, 4800u, 4800u, 80u, 60u, 80u, 60u,
			   0x01020300u, 0x0011u, 640u, 320u, 0x7fffffacu, 1u, 0x000f040fu);
		hand_words(argv[3], "handgrey.jpg", 0x000u, 10u, 10u, 5u, 2u, 0u, 0u,
			   0x01000000u, 0x0000u, 64u, 0u, 0xfffffffcu, 0u, 0x000f0004u);
		selftest_unknown_and_addr(argv[3]);
		selftest_negative(argv[3]);
		corpus(argv[2]);
	} else if (argc == 2 && strcmp(argv[1], "selftest") == 0) {
		selftest_tables();
	} else {
		fprintf(stderr, "usage: %s corpus <corpus.txt> <dir>\n", argv[0]);
		return 2;
	}
	printf("T_JDEC checks=%u failures=%u\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
