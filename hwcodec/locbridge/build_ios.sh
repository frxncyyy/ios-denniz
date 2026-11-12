#!/bin/bash
# SPDX-License-Identifier: MIT
# builds n2locbridge for iOS 16 arm64 (freestanding, like gpu/hwcodec/locdiag/build_ios.sh) and signs it ad-hoc WITH
# entitlements.plist (com.apple.locationd.simulation) via sign_ent.py.  usage: build_ios.sh [OUTDIR] -> LOCBRIDGE BUILD OK
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); OUT=${1:-$HERE/build}; mkdir -p "$OUT"
RES=$(clang -print-resource-dir); VSDK=$(cd "$HERE/../../video/sdk/lib" && pwd)
F=(--target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -ffreestanding -nostdinc -isystem "$RES/include" -fno-builtin -Wall -Wextra)
clang -c "${F[@]}" -o "$OUT/nmea.o" "$HERE/nmea.c"
clang -c "${F[@]}" -o "$OUT/n2locbridge.o" "$HERE/n2locbridge.c"
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs -o "$OUT/n2locbridge" "$OUT/n2locbridge.o" "$OUT/nmea.o" \
	"$HERE/libSystem-locbridge.tbd" "$VSDK/libobjc.A.tbd"
for u in $(llvm-nm -u "$OUT/n2locbridge" | awk '{print $NF}'); do
	grep -q -- "- $u\$" "$HERE/libSystem-locbridge.tbd" "$VSDK/libobjc.A.tbd" || { echo "uncovered $u" >&2; exit 1; }; done
python3 "$HERE/sign_ent.py" "$OUT/n2locbridge" "$HERE/entitlements.plist" n2locbridge
file "$OUT/n2locbridge" | grep -q 'Mach-O 64-bit arm64 executable'
llvm-objdump --macho --dylibs-used "$OUT/n2locbridge" | sed 1d
sha256sum "$OUT/n2locbridge"; echo "LOCBRIDGE BUILD OK $OUT/n2locbridge"
