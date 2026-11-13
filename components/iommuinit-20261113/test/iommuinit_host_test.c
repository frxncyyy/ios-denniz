/* 0408 host test: sweet_fb_iommu_init_ports_ok() exhaustive over MMU_EN bits x owned masks + extension bits, vs a reference written from
 * the rule (physical OK; OVL*_2L MMU_EN only when owned by the armed IOMMU; p5 MMU_EN, ext bits [15:8] or foreign MMU_EN refused),
 * plus the old 0396 rule (any MMU_EN refused) still holding when nothing is owned, and a mutant check.
 * cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../b/iokit/Kernel/arm iommuinit_host_test.c   (also -x c++) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_iommu.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static bool ref(const uint32_t v[4], unsigned owned)
{
	for (int i = 0; i < 4; i++) { if ((v[i] >> 8) & 0xffu) return false; }
	if (v[0] & 1u) return false;                 /* larb0 p5 = OVL1 (4L) */
	if (v[2] & 1u) return false;                 /* larb1 p5 = OVL0 (4L) */
	if ((v[1] & 1u) && !(owned & 1u)) return false;
	if ((v[3] & 1u) && !(owned & 2u)) return false;
	return true;
}
static bool mutant(const uint32_t v[4], unsigned owned) { (void)owned; return !((v[1] | v[3]) & 0xff00u) && !((v[0] | v[2]) & 1u); }   /* forgets ownership */

int main(void)
{
	unsigned n = 0, mut_diff = 0;
	static const uint32_t ext[] = { 0, 0x100, 0x8000, 0xff00 }, other[] = { 0, 0x2, 0x10, 0xfe, 0xffff0000u };
	for (unsigned bits = 0; bits < 16; bits++)
		for (unsigned owned = 0; owned < 4; owned++)
			for (unsigned e = 0; e < 4; e++)
				for (unsigned ep = 0; ep < 4; ep++)
					for (unsigned o = 0; o < 5; o++) {
						uint32_t v[4];
						for (unsigned p = 0; p < 4; p++) { v[p] = ((bits >> p) & 1u) | other[o]; }
						v[ep] |= ext[e];
						bool got = sweet_fb_iommu_init_ports_ok(v, owned), want = ref(v, owned);
						CHECK(got == want, "v %08x %08x %08x %08x owned %u: got %d want %d", v[0], v[1], v[2], v[3], owned, got, want);
						if (owned == 0) { CHECK(got == (!((v[0] | v[1] | v[2] | v[3]) & 1u) && !((v[0] | v[1] | v[2] | v[3]) & 0xff00u)), "old rule with nothing owned"); }
						if (mutant(v, owned) != want) { mut_diff++; }
						n++;
					}
	/* the cases the bring-up meets */
	uint32_t phys[4] = { 0, 0, 0, 0 }, st2[4] = { 0, 1, 0, 0 }, st3[4] = { 0, 1, 0, 1 }, lk[4] = { 1, 0, 0, 0 };
	CHECK(sweet_fb_iommu_init_ports_ok(phys, 0), "all physical, not armed");
	CHECK(sweet_fb_iommu_init_ports_ok(st2, 1), "stage 2 port (larb0 p6) owned");
	CHECK(!sweet_fb_iommu_init_ports_ok(st2, 2), "larb0 p6 on but only larb1 owned");
	CHECK(sweet_fb_iommu_init_ports_ok(st3, 3), "both OVL*_2L ports owned (kern.n2fbiommu>=2 or stage 3)");
	CHECK(!sweet_fb_iommu_init_ports_ok(st3, 0), "both on, IOMMU not armed");
	CHECK(!sweet_fb_iommu_init_ports_ok(lk, 3), "4L port translating");
	CHECK(mut_diff > 0, "mutant (ownership ignored) not caught");
	printf("%s: %u cases, mutant differs in %u, %d failures\n", fails ? "FAILED" : "ALL OK", n, mut_diff, fails);
	return fails != 0;
}
