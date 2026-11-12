#!/bin/bash
# SPDX-License-Identifier: MIT -- builds n2fbhwctl (freestanding, ad-hoc signed, like gpu/hwcodec/gnss/probe) -> FBHWCTL BUILD OK
# and runs its host test first. Output: build/n2fbhwctl (copy it into apps-20261104/payload-fbhw*/ with make_payloads.sh).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); OUT=${1:-$H/build}; mkdir -p "$OUT"
cc -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -o "$OUT/test_fbhwctl" "$H/test_fbhwctl.c" && "$OUT/test_fbhwctl" | tail -1
F="--target=arm64-apple-ios16.0 -std=c11 -O2 -ffreestanding -nostdinc -isystem $(clang -print-resource-dir)/include -fno-builtin -Wall -Wextra -Werror"
clang -c $F -o "$OUT/n2fbhwctl.o" "$H/n2fbhwctl.c"
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -o "$OUT/n2fbhwctl" "$OUT/n2fbhwctl.o" "$H/libSystem-fbhwctl.tbd"
for u in $(llvm-nm -u -j "$OUT/n2fbhwctl" | grep -v ":$" | grep -v "^$"); do grep -q -- "- $u\$" "$H/libSystem-fbhwctl.tbd" || { echo "uncovered $u"; exit 1; }; done
head -c4 "$OUT/n2fbhwctl" | od -An -tx1 | grep -q 'cf fa ed fe' || { echo "not a Mach-O"; exit 1; }
grep -a -q 'N2FBHWCTL phase %d/%d' "$OUT/n2fbhwctl" && grep -a -q 'N2FBHWCTL iommu phase %d/%d' "$OUT/n2fbhwctl" || { echo "strings missing"; exit 1; }
rm -f "$OUT/n2fbhwctl.o"; sha256sum "$OUT/n2fbhwctl"; echo "FBHWCTL BUILD OK (0401 fbiommuctl fork)"
