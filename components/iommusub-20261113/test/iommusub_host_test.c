/* 0409 host test: n2io_sub_plan (extracted from ../b/iokit/Kernel/arm/sweet_fb_iommu_hw.h between the 0409-PURE markers into sub_pure.h)
 * exhaustive over all 256 values x armed x prepped vs a reference written from the rule; plus the T1 equivalence 0x0e == stage-1 order.
 * sed -n '/0409-PURE-BEGIN/,/0409-PURE-END/p' ../b/iokit/Kernel/arm/sweet_fb_iommu_hw.h > sub_pure.h
 * cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined iommusub_host_test.c   (also -x c++) */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "sub_pure.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static int ref(uint32_t v, bool armed, bool prepped, uint8_t *a)
{
	int n = 0;
	if (v > 0xff || (v & 0x60)) return -22;
	bool w = v & 0x0e;
	if ((v & 0x80) && ((v & 1) || w)) return -22;
	if (armed && v != 0 && v != 0x10) return -16;
	if (v & 0x10) a[n++] = 0x10;
	if ((v & 1) || (w && !prepped)) a[n++] = 1;
	static const uint8_t ord[] = { 2, 4, 8, 0x80 };
	for (int i = 0; i < 4; i++) if (v & ord[i]) a[n++] = ord[i];
	return n;
}
int main(void)
{
	unsigned cases = 0;
	for (uint32_t v = 0; v < 0x200; v++)
		for (int armed = 0; armed < 2; armed++)
			for (int prepped = 0; prepped < 2; prepped++) {
				uint8_t a[6] = { 0 }, b[8] = { 0 };
				int n = n2io_sub_plan(v, armed, prepped, a), m = ref(v, armed, prepped, b);
				CHECK(n == m, "v %#x armed %d prepped %d: n %d ref %d", v, armed, prepped, n, m);
				for (int i = 0; n == m && i < n; i++) CHECK(a[i] == b[i], "v %#x order %d: %#x vs %#x", v, i, a[i], b[i]);
				if (n > 0) { for (int i = 0; i < n; i++) CHECK(a[i] != 2 || i > 0 || prepped, "PT_BASE before prep v %#x", v); }
				cases++;
			}
	uint8_t a[6]; int n = n2io_sub_plan(0x0e, false, false, a);
	CHECK(n == 4 && a[0] == 1 && a[1] == 2 && a[2] == 4 && a[3] == 8, "0x0e = prep, PT_BASE, TFRP, TLB (stage-1 order)");
	CHECK(n2io_sub_plan(0x10, true, true, a) == 1 && a[0] == 0x10, "dump allowed when armed");
	CHECK(n2io_sub_plan(0x82, false, true, a) == -22, "undo + write refused");
	CHECK(n2io_sub_plan(0x80, false, false, a) == 1 && a[0] == 0x80, "undo alone");
	CHECK(n2io_sub_plan(0, false, false, a) == 0, "0 = nothing");
	printf("%s: %u cases, %d failures\n", fails ? "FAILED" : "ALL OK", cases, fails);
	return fails != 0;
}
