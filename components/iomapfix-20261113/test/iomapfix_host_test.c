/* 0410 host test: pure helpers extracted from ../b/iokit/Kernel/arm/sweet_fb_iommu_hw.h (0409-PURE block): n2io_larb_powered (both SPM
 * status words must show the domain), n2io_foreign_count (powered larbs with an MMU_EN port), and n2io_sub_plan unchanged from 0409.
 * sed -n '/0409-PURE-BEGIN/,/0409-PURE-END/p' ../b/iokit/Kernel/arm/sweet_fb_iommu_hw.h > pure.h
 * cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined iomapfix_host_test.c   (also -x c++) */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pure.h"
static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %d %s\n", __LINE__, m); } } while (0)
int main(void)
{
	unsigned n = 0;
	for (unsigned bit = 0; bit < 32; bit++) for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
		uint32_t s1 = (a ? 1u << bit : 0) | 0x5a5a0000u * (bit != 17), s2 = (b ? 1u << bit : 0);
		CHECK(n2io_larb_powered(s1 & ~(a ? 0 : 1u << bit), s2, bit) == (a && b), "powered only if both status words say on"); n++;
	}
	for (unsigned m = 0; m < 128; m++) for (unsigned e = 0; e < 128; e++) {
		uint32_t en[7]; bool pw[7]; int want = 0;
		for (int i = 0; i < 7; i++) { pw[i] = (m >> i) & 1; en[i] = ((e >> i) & 1) ? (1u << (i * 4)) : 0; if (pw[i] && en[i]) want++; }
		CHECK(n2io_foreign_count(en, pw, 7) == want, "foreign count"); n++;
	}
	uint32_t en0[7] = { 0, 0, 0, 0, 0, 0, 0x80000000u }; bool pw0[7] = { 0, 0, 0, 0, 0, 0, 1 };
	CHECK(n2io_foreign_count(en0, pw0, 7) == 1, "port 31 of larb20 counts");
	bool pwoff[7] = { 0 }; CHECK(n2io_foreign_count(en0, pwoff, 7) == 0, "unpowered larbs never count (never read)");
	uint8_t act[6]; CHECK(n2io_sub_plan(0x0e, false, false, act) == 4 && act[1] == 2, "0409 plan unchanged");
	printf("%s: %u cases, %d failures\n", fails ? "FAILED" : "ALL OK", n, fails);
	return fails != 0;
}
