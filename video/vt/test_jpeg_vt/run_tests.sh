#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# R1 VideoToolbox glue suite: the JPEG encoder object against WP02's fake Apple runtime, under ASan+UBSan and TSan, plus
# an arm64-apple-ios16.0 compile of the glue with the real sdk/sysinc headers.
#
# usage: bash gpu/video/vt/test_jpeg_vt/run_tests.sh [outdir]      (default gpu/video/build/R1/vt)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-"$ROOT/build/R1/vt"}
CC=${CC:-clang}

mkdir -p "$OUT"
TOTAL=0

COMMON=(-std=c11 -O2 -g -Wall -Wextra -Werror -DN2_HOST_FAKE
        -I "$ROOT/include" -I "$ROOT/fake" -I "$ROOT/vt" -I "$ROOT/jpeg" -I "$ROOT")
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
SOURCES=("$HERE/t_jpeg_vt.c" "$ROOT/vt/n2jpeg_vt.c" "$ROOT/jpeg/n2jpeg.c"
         "$ROOT/kern/n2vk_ref.c" "$ROOT/kern/n2vk_dispatch.c"
         "$ROOT/fake/fake_apple.c" "$ROOT/fake/fake_vt.c")

run_suite() {   # run_suite <binary> <log>
	local log=$2
	local n

	if ! "$1" >"$log" 2>&1; then
		cat "$log"
		echo "N2TEST R1-vt FAIL ${1##*/}"
		exit 1
	fi
	cat "$log"
	n=$(awk '/^T_JPEG_VT PASS [0-9]+$/{print $3}' "$log" | tail -n 1)
	if [ -z "${n:-}" ]; then
		echo "N2TEST R1-vt FAIL no T_JPEG_VT PASS line in $log"
		exit 1
	fi
	TOTAL=$((TOTAL + n))
}

echo "== R1 vt: fake runtime, ASan+UBSan =="
if ! "$CC" "${COMMON[@]}" "${SAN[@]}" -o "$OUT/t_jpeg_vt_asan" "${SOURCES[@]}" -lm >"$OUT/build_asan.log" 2>&1; then
	cat "$OUT/build_asan.log"
	echo "N2TEST R1-vt FAIL asan build"
	exit 1
fi
run_suite "$OUT/t_jpeg_vt_asan" "$OUT/asan.log"

echo "== R1 vt: fake runtime, TSan (two encoders, two threads) =="
if ! "$CC" "${COMMON[@]}" -fsanitize=thread -o "$OUT/t_jpeg_vt_tsan" "${SOURCES[@]}" -lm >"$OUT/build_tsan.log" 2>&1; then
	cat "$OUT/build_tsan.log"
	echo "N2TEST R1-vt FAIL tsan build"
	exit 1
fi
run_suite "$OUT/t_jpeg_vt_tsan" "$OUT/tsan.log"

echo "== R1 vt: arm64-apple-ios16.0 compile of the glue (sdk/sysinc) =="
SYSINC=$ROOT/sdk/sysinc
RES=$(clang -print-resource-dir)
if [ -d "$SYSINC" ]; then
	set +e
	clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc \
		-isystem "$RES/include" -isystem "$SYSINC" -I "$ROOT/include" -I "$ROOT" -I "$ROOT/vt" -I "$ROOT/jpeg" \
		-Wall -Wextra -Werror "$ROOT/vt/n2jpeg_vt.c" -o "$OUT/n2jpeg_vt.ios.o" >"$OUT/build_ios.log" 2>&1
	ios_status=$?
	set -e
	if [ $ios_status -ne 0 ]; then
		cat "$OUT/build_ios.log"
		echo "N2TEST R1-vt FAIL ios compile (status $ios_status)"
		exit 1
	fi
	echo "   vt/n2jpeg_vt.c compiles for arm64-apple-ios16.0 with zero warnings"
	TOTAL=$((TOTAL + 1))
else
	echo "   sdk/sysinc is missing (WP01); the iOS compile is skipped"
fi

echo "N2TEST R1-vt PASS $TOTAL/$TOTAL"
