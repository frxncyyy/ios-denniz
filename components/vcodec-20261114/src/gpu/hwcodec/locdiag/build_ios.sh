#!/bin/bash
# SPDX-License-Identifier: MIT
# builds n2sample (thread sampler, n2sample.c) for iOS 16 arm64, freestanding like gpu/hwcodec/vtdec_run/build_ios.sh,
# against libSystem-sample.tbd (the symbols it needs). Ad-hoc signed: on this port every signed binary on the root
# filesystem is a platform binary (sweet_amfi.c), which task_for_pid on platform targets needs (inferred).
# usage: build_ios.sh [OUTDIR]  -> prints SAMPLE BUILD OK
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); OUT=${1:-$HERE/build}; mkdir -p "$OUT"
RES=$(clang -print-resource-dir)
clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -ffreestanding -nostdinc -isystem "$RES/include" -fno-builtin \
	-Wall -Wextra -Wno-unused-parameter -o "$OUT/n2sample.o" "$HERE/n2sample.c"
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs -o "$OUT/n2sample" "$OUT/n2sample.o" "$HERE/libSystem-sample.tbd"
und=$(llvm-nm -u "$OUT/n2sample" | awk '{print $NF}')
for u in $und; do grep -q -- "- $u\$" "$HERE/libSystem-sample.tbd" || { echo "uncovered $u" >&2; exit 1; }; done
file "$OUT/n2sample" | grep -q 'Mach-O 64-bit arm64 executable'
llvm-objdump --macho --dylibs-used "$OUT/n2sample" | sed 1d
sha256sum "$OUT/n2sample"; echo "SAMPLE BUILD OK $OUT/n2sample"
