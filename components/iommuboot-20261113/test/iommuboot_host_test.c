/* 0411 host test: sweet_fb_iommu_boot_mode (boot default 3, boot-arg clamps to 0..3) + the 0408 init check still accepting owned ports.
 * cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../b/iokit/Kernel/arm iommuboot_host_test.c  (also -x c++) */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "sweet_fb_iommu.h"
static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %d %s\n", __LINE__, m); } } while (0)
int main(void)
{
	CHECK(sweet_fb_iommu_boot_mode(false, 0) == 3, "no boot-arg: on (3)");
	for (int v = -5; v < 10; v++) { int m = sweet_fb_iommu_boot_mode(true, v); CHECK(m == (v < 0 ? 0 : v > 3 ? 3 : v), "boot-arg clamps"); }
	uint32_t v[4] = { 0, 1, 0, 1 }; CHECK(sweet_fb_iommu_init_ports_ok(v, 3) && !sweet_fb_iommu_init_ports_ok(v, 0), "0408 rule unchanged");
	printf("%s: %d failures\n", fails ? "FAILED" : "ALL OK", fails); return fails != 0;
}
