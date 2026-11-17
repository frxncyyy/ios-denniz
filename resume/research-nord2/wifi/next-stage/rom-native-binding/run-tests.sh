#!/bin/sh
set -eu
# Run from ios-sweet root. Outputs remain private here.
p=research/nord2/wifi/next-stage/rom-native-binding
base='-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -DNRB_HOST_TEST -DNWF_ROM_HOST_TEST -Ishim/nord2-xnu'
cc $base "$p/test-engine.c" "$p/rom_place.c" "$p/../rom-native-candidate/rom_contract.c" shim/nord2-xnu/wifi_rom_plan.c -lcrypto -o "$p/test-engine"
"$p/test-engine" research/nord2/wifi/firmware/soc3_0_ram_wmmcu_1a_1_hdr.bin research/nord2/wifi/firmware/soc3_0_ram_wifi_1a_1_hdr.bin > "$p/test-engine.log"
cc $base -DNRB_NATIVE_HOST_TEST -DNWF_HOST_TEST "$p/test-native.c" "$p/rom_xnu.c" "$p/rom_place.c" "$p/../rom-native-candidate/rom_contract.c" shim/nord2-xnu/wifi_rom_plan.c shim/nord2-xnu/wifi_firmware.c -lcrypto -o "$p/test-native"
"$p/test-native" research/nord2/wifi/vendor-read/vendor.img > "$p/test-native.log"
