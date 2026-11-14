#!/bin/bash
# SPDX-License-Identifier: MIT
# Host tests for package V10 (A32 data processing, moves, multiplies).
# Builds only under /tmp/n2hw-V10.  No arguments.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
INC="$(cd "$HERE/../.." && pwd)/include"
OUT=/tmp/n2hw-V10
CC="${CC:-clang}"
mkdir -p "$OUT"

fail() { echo "N2TEST V10 FAIL $*"; exit 1; }

python3 "$HERE/mkenc.py" "$HERE/encodings.txt" "$OUT/enc_v10.h" || fail "llvm-mc encoding generation"

COMMON="-std=c11 -g -Wall -Wextra -Werror -I $INC -I $HERE -I $OUT"
SRC="$HERE/t_dp.c $HERE/../n2a32_arm_dp.c"

# shellcheck disable=SC2086
$CC $COMMON -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	$SRC -o "$OUT/t_v10_asan" || fail "clang -O1 ASan+UBSan build"
ASAN_OPTIONS=detect_leaks=0 "$OUT/t_v10_asan" || fail "ASan+UBSan run"

# shellcheck disable=SC2086
$CC $COMMON -O2 $SRC -o "$OUT/t_v10_o2" || fail "clang -O2 build"
"$OUT/t_v10_o2" || fail "O2 run"
