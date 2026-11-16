#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=build/native
SDK="$PWD/build/apple-toolchain/sdks/iPhoneOS16.5.sdk"
case "${1:-}" in
 --binary-only) test -s "$OUT/varying.bc" ;;
 "")
"${NORD2_AIR_LLVM_BIN:?}/llvm-as" metal/tegu/varying_probe.ll -o "$OUT/probe-render.bc"
"${NORD2_AIR_LLVM_BIN}/llvm-as" tests/shaders/metal_probe_compute.ll -o "$OUT/probe-compute.bc"
"${NORD2_AIR_LLVM_BIN}/llvm-as" tests/shaders/metal_probe_geometry.ll -o "$OUT/probe-geometry.bc"
"${NORD2_AIR_LLVM_BIN}/llvm-as" tests/shaders/metal_probe_fetch.ll -o "$OUT/probe-fetch.bc"
"${NORD2_AIR_LLVM_BIN}/llvm-link" "$OUT/probe-render.bc" "$OUT/probe-compute.bc" "$OUT/probe-geometry.bc" "$OUT/probe-fetch.bc" -o "$OUT/varying.bc"
 ;;
 *) echo 'usage: build_metal_probe.sh [--binary-only]' >&2; exit 2 ;;
esac
clang --target=arm64-apple-ios16.0 -isysroot "$SDK" -fuse-ld=lld -fblocks -fno-objc-arc -O2 -Wall -Wextra -Werror \
 metal/nord2_metal_probe.m -framework Foundation -framework Metal -framework IOSurface -framework CoreFoundation \
 -lobjc -lSystem -Wl,-adhoc_codesign -o "$OUT/nord2-metal-probe"
echo 'PASS strict ARM64 Metal public-API probe build (not executed)'
