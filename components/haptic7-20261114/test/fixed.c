#include <stdio.h>
#include <string.h>
#include "nord2_haptic.h"
int main(void) {
	static uint8_t img[N2H_RAM_IMG_MAX]; struct n2h_ram_layout lay;
	if (!n2h_ram_build(img, sizeof img, 1700, N2H_PROFILE, &lay)) return 1;
	for (uint32_t lv = 0; lv <= 1000; lv += 100) for (uint32_t mv = 3000; mv <= 4500; mv += 500)
		if (n2h_gain_for(lv, mv, 0, N2H_PROFILE) != N2H_FIXED_GAIN || n2h_gain_for(lv, mv, 1, N2H_PROFILE) != N2H_FIXED_GAIN / 2) return 2;
	printf("profile %s fixed %#x: tick %u click %u heavy %u x0.1 ms, all waves same length: %d\n", N2H_PROFILE->name, N2H_FIXED_GAIN,
	    lay.ms_x10[1], lay.ms_x10[2], lay.ms_x10[3], lay.ms_x10[1] == lay.ms_x10[2] && lay.ms_x10[2] == lay.ms_x10[3]);
	return 0;
}
