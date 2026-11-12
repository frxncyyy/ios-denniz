/* SPDX-License-Identifier: MIT */
/*
 * t_fake_dec07.c -- host test of the RWVC_WIN_VDEC_ALL support in tests/hw/n2vc_hw_fake.c (package DEC07's patch to T02).
 *
 * The fake under test is the staged copy: current tests/hw/n2vc_hw_fake.c + patches/n2vc_hw_fake-vdec-all.patch, with
 * include/redwood_vcodec.h + patches/redwood_vcodec-rev3.patch.  Or T02's own generic suite (t_fake.c) covers "every
 * window of the table"; this file covers the parts that only exist for the new window: the 0x30000 size and per-page
 * power rule, the two read-only larb pages, the recorder name, load-time `set` versus a runtime set rule into a larb
 * page, and the value round trip through the separate backing array.
 *
 * Expected values: the page -> block table of facts/VDEC_PROTOCOL.md section 4 (identical to the kernel's
 * n2vc_vdec_page_blk and to ABI rev 3); the larb pages are the kernel's (n2vc_larb_on).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "n2vc_hw.h"
#include "n2vc_hw_fake.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define CORE RWVC_BLK_VDEC_CORE
#define SOC  RWVC_BLK_VDEC_SOC
#define LARB5_PAGE 0x0du
#define LARB4_PAGE 0x2eu

static const uint8_t PAGE_BLK[48] = {
	CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE,
	SOC, CORE,
	SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC, SOC,
	CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE, CORE,
};

static int rd(const struct n2vc_hw *h, unsigned win, uint32_t off, uint32_t *v)
{
	struct rwvc_reg_op o = { RWVC_OP_READ, (uint8_t)win, 0, off, 0, 0 };
	unsigned d = 0;
	int e = h->regs(h->ctx, &o, 1, &d);
	if (v) { *v = o.value; }
	return e;
}

static int wr(const struct n2vc_hw *h, unsigned win, uint32_t off, uint32_t v)
{
	return n2vc_hw_wr(h, win, off, v);
}

static void test_size_and_pages(void)
{
	for (uint32_t p = 0; p < 48; p++) {
		struct n2vc_hw h;
		uint32_t v = 0;
		unsigned viol;

		CHECK(n2vc_hw_open_fake(&h, NULL) == 0);
		/* nothing powered: every page is EACCES and counted */
		viol = n2vc_hw_fake_violations(&h);
		CHECK(rd(&h, RWVC_WIN_VDEC_ALL, p * 4096u, &v) == -EACCES);
		CHECK(n2vc_hw_fake_violations(&h) == viol + 1);
		/* power exactly the page's block: readable and writable (except the larb pages) */
		CHECK(h.power(h.ctx, PAGE_BLK[p], 1) == 0);
		CHECK(rd(&h, RWVC_WIN_VDEC_ALL, p * 4096u, &v) == 0);
		int e = wr(&h, RWVC_WIN_VDEC_ALL, p * 4096u, 0xa5a50000u + p);
		if (p == LARB5_PAGE || p == LARB4_PAGE) { CHECK(e == -EACCES); }
		else { CHECK(e == 0); }
		CHECK(h.power(h.ctx, PAGE_BLK[p], 0) == 0);
		CHECK(rd(&h, RWVC_WIN_VDEC_ALL, p * 4096u, &v) == -EACCES);
		h.close(h.ctx);
	}
}

static void test_bounds_and_roundtrip(void)
{
	struct n2vc_hw h;
	uint32_t v = 0;
	char rec[512];
	snprintf(rec, sizeof rec, "%s/dec07_fake.rec", getenv("DEC07_SCRATCH") ? getenv("DEC07_SCRATCH") : "/tmp");
	CHECK(n2vc_hw_open_fake(&h, rec) == 0);
	CHECK(h.power(h.ctx, RWVC_BLK_VDEC_CORE, 1) == 0);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, 0x2fffc, 0x12345678u) == 0);
	CHECK(rd(&h, RWVC_WIN_VDEC_ALL, 0x2fffc, &v) == 0 && v == 0x12345678u);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, 0x30000, 1) == -EINVAL);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, 0x2ffe, 1) == -EINVAL);
	CHECK(rd(&h, RWVC_WIN_COUNT, 0, &v) == -EINVAL);
	/* the two larb pages are readable and carry the kernel's LARB configuration words when written through their own
	 * windows; a write through VDEC_ALL is refused and counted */
	unsigned viol = n2vc_hw_fake_violations(&h);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x24, 1) == -EACCES);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB4_PAGE * 4096u + 0x24, 1) == -EACCES);
	CHECK(n2vc_hw_fake_violations(&h) == viol + 2);
	/* The two allowed larb words (larb5 0x1600d00c, larb4 0x1602e00c), bit 0 only.  Since opus-decint's vc19
	 * finding the fake also models the SMI_LARB_SLP_CON silicon: bit 16 (SLP_PROT_RDY, read-only) follows bit 0,
	 * so the vendor's poll on bit 16 ends at once. */
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, 1) == 0);
	CHECK(rd(&h, RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, &v) == 0 && v == 0x00010001u);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB4_PAGE * 4096u + 0x00c, 1) == 0);
	CHECK(rd(&h, RWVC_WIN_VDEC_ALL, LARB4_PAGE * 4096u + 0x00c, &v) == 0 && v == 0x00010001u);
	/* The vendor's literal release write is 0x00010000 (bit 16).  It is refused: only bit 0 is in the allow-list
	 * mask, and vcu/n2vcu.c rewrites the release as a bit-0 WRITE_MASK with value 0 (opus-decint's fix). */
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, 0x00010000u) == -EACCES);
	{
		struct rwvc_reg_op o = { RWVC_OP_WRITE_MASK, RWVC_WIN_VDEC_ALL, 0, LARB5_PAGE * 4096u + 0x00c, 0, 1u };
		unsigned d = 0;
		CHECK(h.regs(h.ctx, &o, 1, &d) == 0 && d == 1);
	}
	CHECK(rd(&h, RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, &v) == 0 && v == 0u);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x00c, 3) == -EACCES);   /* bit 1 outside the mask */
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB5_PAGE * 4096u + 0x010, 1) == -EACCES);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, LARB4_PAGE * 4096u + 0x010, 1) == -EACCES);
	CHECK(n2vc_hw_fake_violations(&h) == viol + 6);
	/* the recorder names the window as the header does */
	h.close(h.ctx);
	{
		FILE *fp = fopen(rec, "r");
		char line[512];
		int seen_alloc = 0, seen_read = 0;
		CHECK(fp != NULL);
		while (fp && fgets(line, sizeof line, fp)) {
			if (strstr(line, "W VDEC_ALL 0x2fffc 0x12345678")) { seen_alloc++; }
			if (strstr(line, "R VDEC_ALL 0x2fffc 0x12345678")) { seen_read++; }
		}
		if (fp) { fclose(fp); }
		CHECK(seen_alloc == 1);
		CHECK(seen_read == 1);
		remove(rec);
	}
}

static void test_script_rules(void)
{
	const char *scratch = getenv("DEC07_SCRATCH") ? getenv("DEC07_SCRATCH") : "/tmp";
	char path[512], line[256];
	struct n2vc_hw h;
	uint32_t v = 0;
	FILE *fp;

	/* a load-time default on a larb page is not an access and is accepted; a runtime set into it is refused */
	snprintf(path, sizeof path, "%s/dec07_script.txt", scratch);
	fp = fopen(path, "w");
	CHECK(fp != NULL);
	if (fp) {
		fprintf(fp, "set VDEC_ALL 0xd000 0x11223344\n");
		fprintf(fp, "after write VDEC_ALL 0x0010 0x00000001 -> set VDEC_ALL 0x0014 0x00000002\n");
		fprintf(fp, "after write VDEC_ALL 0x0020 0x00000001 -> set VDEC_ALL 0xd000 0x00000001\n");
		fclose(fp);
	}
	CHECK(n2vc_hw_open_fake(&h, NULL) == 0);
	CHECK(n2vc_hw_fake_script(&h, path) == 0);
	CHECK(h.power(h.ctx, RWVC_BLK_VDEC_CORE, 1) == 0);
	CHECK(rd(&h, RWVC_WIN_VDEC_ALL, 0xd000, &v) == 0 && v == 0x11223344u);   /* the load-time default landed */
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, 0x0010, 1) == 0);
	CHECK(rd(&h, RWVC_WIN_VDEC_ALL, 0x0014, &v) == 0 && v == 2);              /* an ordinary set rule still works */
	unsigned viol = n2vc_hw_fake_violations(&h);
	CHECK(wr(&h, RWVC_WIN_VDEC_ALL, 0x0020, 1) == -EACCES);                   /* its target is a read-only larb page */
	CHECK(n2vc_hw_fake_violations(&h) == viol + 1);
	CHECK(strstr(n2vc_hw_fake_last_error(&h), "read-only larb") != NULL);
	h.close(h.ctx);
	remove(path);
	(void)line;
}

int main(void)
{
	for (int i = 0; i < 48; i++) { CHECK(PAGE_BLK[i] == CORE || PAGE_BLK[i] == SOC); }
	test_size_and_pages();
	test_bounds_and_roundtrip();
	test_script_rules();
	printf("N2TEST DEC07FAKE %s %d/%d\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	return fails ? 1 : 0;
}
