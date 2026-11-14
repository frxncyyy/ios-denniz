#!/bin/bash
# SPDX-License-Identifier: MIT
# R1 independent spot tests for track V instruction classes and the V02 core.
# Builds only under /tmp/n2hw-r1.  Run from the work tree root:
#   bash gpu/hwcodec/review/r1/run_spot.sh
set -euo pipefail

out=/tmp/n2hw-r1/spot
rm -rf "$out"
mkdir -p "$out"

src="gpu/hwcodec/emu/n2a32_arm_dp.c gpu/hwcodec/emu/n2a32_arm_media.c \
     gpu/hwcodec/emu/n2a32_arm_mem.c gpu/hwcodec/emu/n2a32_arm_misc.c \
     gpu/hwcodec/emu/n2a32_t16.c gpu/hwcodec/emu/n2a32_t32_dp.c gpu/hwcodec/emu/n2a32_neon_shift.c gpu/hwcodec/emu/n2a32_vfp.c gpu/hwcodec/emu/n2a32_neon_3same.c gpu/hwcodec/emu/n2a32_t32_mem.c gpu/hwcodec/emu/n2a32_t32_misc.c gpu/hwcodec/emu/n2a32_neon_misc.c gpu/hwcodec/emu/n2a32_neon_ldst.c \
     gpu/hwcodec/emu/n2a32_excl.c gpu/hwcodec/emu/n2a32_core.c \
     gpu/hwcodec/emu/n2a32_mem.c gpu/hwcodec/emu/n2a32_stub_classes.c \
     gpu/hwcodec/review/r1/spot/spot.c"

common="-std=c11 -D_GNU_SOURCE -g -Wall -Wextra -Werror -I gpu/hwcodec/include"

nice -n 10 clang $common -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	$src -o "$out/spot-san" -pthread -lm
nice -n 10 clang $common -O2 $src -o "$out/spot-o2" -pthread -lm

echo "== ASan+UBSan -O1"
"$out/spot-san" | tee "$out/san.txt"
echo "== -O2"
"$out/spot-o2" | tee "$out/o2.txt"

san=$(tail -1 "$out/san.txt")
o2=$(tail -1 "$out/o2.txt")
if [ "$san" != "$o2" ]; then
	echo "R1-SPOT FAIL: sanitizer and -O2 runs disagree: '$san' vs '$o2'"
	exit 1
fi
if ! grep -q '^R1-SPOT PASS ' <<<"$san"; then
	echo "R1-SPOT FAIL: $san"
	exit 1
fi
echo "N2TEST R1 PASS ${san#R1-SPOT PASS }"
