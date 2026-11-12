#!/bin/bash
# SPDX-License-Identifier: MIT -- builds n2gpsdl_probe (freestanding, ad-hoc signed like gpu/hwcodec/locdiag). -> PROBE BUILD OK
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); OUT=${1:-$H/build}; mkdir -p "$OUT"
clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 -ffreestanding -nostdinc -isystem "$(clang -print-resource-dir)/include" -fno-builtin -Wall -Wextra -o "$OUT/p.o" "$H/n2gpsdl_probe.c"
clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 -ffreestanding -nostdinc -isystem "$(clang -print-resource-dir)/include" -fno-builtin -Wall -Wextra -o "$OUT/q.o" "$H/n2gpsdl_pipe.c"
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -o "$OUT/n2gpsdl_pipe" "$OUT/q.o" "$H/libSystem-probe.tbd"
clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 -ffreestanding -nostdinc -isystem "$(clang -print-resource-dir)/include" -fno-builtin -Wall -Wextra -o "$OUT/s.o" "$H/n2gpsdl_step.c"
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -o "$OUT/n2gpsdl_step" "$OUT/s.o" "$H/libSystem-probe.tbd"
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -o "$OUT/n2gpsdl_probe" "$OUT/p.o" "$H/libSystem-probe.tbd"
for u in $(llvm-nm -u -j "$OUT/n2gpsdl_probe" "$OUT/n2gpsdl_pipe" | grep -v ":$" | grep -v "^$"); do grep -q -- "- $u\$" "$H/libSystem-probe.tbd" || { echo "uncovered $u"; exit 1; }; done
sha256sum "$OUT/n2gpsdl_probe" "$OUT/n2gpsdl_pipe"; echo "PROBE BUILD OK"
