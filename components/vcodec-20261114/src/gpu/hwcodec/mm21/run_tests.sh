#!/bin/bash
# SPDX-License-Identifier: MIT
# run_tests.sh -- n2_mm21 host tests + arm64-ios compile check (opus-mm21).  Builds into /tmp/n2hw-opus-mm21.
#  1-4  t_mm21: {portable C, NEON code path on the intrinsic emulation} x {ASan+UBSan -O1, plain -O2}; with the host
#       libyuv (black-box oracle, linked) when /usr/include/libyuv/convert.h declares MM21ToNV12
#  5    arm64-apple-ios16.0 compile of n2_mm21.c with the real SDK: Mach-O arm64, NEON path selected (q-register
#       loads/stores present), the six exported functions present
#  6    benchmark line (informational, 1080p, single core) -- not a pass criterion
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$HERE/../../.." && pwd)                       # tree root that holds gpu/
O=/tmp/n2hw-opus-mm21/rt
rm -rf "$O"; mkdir -p "$O"
fail() { echo "N2TEST MM21 FAIL $*"; exit 1; }
pass=0; total=0

YUV=(); if [ -f /usr/include/libyuv/convert.h ] && grep -q MM21ToNV12 /usr/include/libyuv/convert.h; then YUV=(-DHAVE_LIBYUV -lyuv); fi
[ ${#YUV[@]} -gt 0 ] && echo "oracle: host libyuv MM21ToNV12" || echo "oracle: reference tiler only (no libyuv with MM21ToNV12)"
SAN=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all)
for impl in c emu; do
	for opt in san o2; do
		total=$((total + 1))
		F=(-std=c11 -Wall -Wextra -Werror -I "$HERE")
		[ "$impl" = emu ] && F+=(-DN2_MM21_NEON_EMU)
		if [ "$opt" = san ]; then F+=("${SAN[@]}"); else F+=(-O2); fi
		nice -n 10 clang "${F[@]}" -o "$O/t_${impl}_$opt" "$HERE/t_mm21.c" "$HERE/n2_mm21.c" "${YUV[@]}" \
			> "$O/build_${impl}_$opt.log" 2>&1 || { cat "$O/build_${impl}_$opt.log"; fail "build $impl $opt"; }
		"$O/t_${impl}_$opt" > "$O/t_${impl}_$opt.log" 2>&1 || { tail -20 "$O/t_${impl}_$opt.log"; fail "t_mm21 $impl $opt"; }
		echo "  $impl $opt: $(head -1 "$O/t_${impl}_$opt.log") $(tail -1 "$O/t_${impl}_$opt.log")"
		pass=$((pass + 1))
	done
done

total=$((total + 1))
SDK=${N2_IOS_SDK:-$W/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
if [ -d "$SDK/usr/include" ]; then
	nice -n 10 clang -c --target=arm64-apple-ios16.0 -isysroot "$SDK" -std=c11 -O2 -Wall -Wextra -Werror -I "$HERE" \
		-o "$O/n2_mm21_ios.o" "$HERE/n2_mm21.c" || fail "ios compile"
	file "$O/n2_mm21_ios.o" | grep -q 'Mach-O 64-bit arm64 object' || fail "ios object is not arm64 Mach-O"
	nq=$(llvm-objdump -d "$O/n2_mm21_ios.o" | grep -cE '(ldr|str|stp|ldp)[[:space:]]+q' || true)
	[ "$nq" -ge 4 ] || fail "ios object has no q-register NEON copies ($nq)"
	strings "$O/n2_mm21_ios.o" | grep -qx neon || fail "ios build did not select the NEON path"
	for s in n2_mm21_to_nv12 n2_mm21_detile_plane n2_mm21_to_nv12_staged n2_mm21_staging_bytes n2_nv12_to_mm21 n2_mm21_impl; do
		llvm-nm "$O/n2_mm21_ios.o" | grep -q " T _$s\$" || fail "ios object lacks $s"
	done
	echo "  ios: arm64 Mach-O, impl=neon, $nq q-register loads/stores"
	pass=$((pass + 1))
else
	fail "no iOS SDK at $SDK"
fi

"$O/t_emu_o2" --bench 50 | sed 's/^/  /'
"$O/t_c_o2" --bench 50 | sed 's/^/  /'
echo "N2TEST MM21 PASS $pass/$total"
