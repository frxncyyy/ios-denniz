#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V14 host tests.  Builds everything under /tmp/n2hw-V14 (RAM-backed tmp).
set -euo pipefail

ID=V14
SB=/tmp/n2hw-V14
SELF="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SELF/../.." && pwd)"	# gpu/hwcodec
INC="$ROOT/include"
CC="${CC:-clang}"
LLVM_MC="${LLVM_MC:-llvm-mc}"
LLVM_OBJDUMP="${LLVM_OBJDUMP:-llvm-objdump}"
TRIPLE=thumbv7a-linux-gnueabi
MATTR=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8

rm -rf "$SB"
mkdir -p "$SB"

# 1. Encodings come from llvm-mc, never from hand-written hex.
"$LLVM_MC" -triple="$TRIPLE" -mattr="$MATTR" -filetype=obj \
	"$SELF/t16_enc.s" -o "$SB/t16_enc.o"
"$LLVM_OBJDUMP" -d --triple="$TRIPLE" "$SB/t16_enc.o" >"$SB/t16_enc.dump"
"$LLVM_MC" -triple="$TRIPLE" -mattr="$MATTR" --show-encoding \
	"$SELF/t16_enc.s" >"$SB/t16_enc.show"
python3 "$SELF/gen_enc.py" "$SELF/t16_enc.s" "$SB/t16_enc.dump" \
	"$SB/t16_enc.show" "$SB/encodings.h"

# 2. ASan + UBSan build (the required host flags).
# shellcheck disable=SC2086
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-I "$INC" -I "$SB" \
	"$SELF/../n2a32_t16.c" "$SELF/t16_tests.c" -o "$SB/t16_asan"

# 3. Second build: -O2, no sanitizers.
# shellcheck disable=SC2086
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
	-I "$INC" -I "$SB" \
	"$SELF/../n2a32_t16.c" "$SELF/t16_tests.c" -o "$SB/t16_o2"

if ! "$SB/t16_asan" >"$SB/asan.log" 2>&1; then
	cat "$SB/asan.log"
	echo "N2TEST $ID FAIL asan-run"
	exit 1
fi
if ! "$SB/t16_o2" >"$SB/o2.log" 2>&1; then
	cat "$SB/o2.log"
	echo "N2TEST $ID FAIL o2-run"
	exit 1
fi

cat "$SB/asan.log"
grep -E '^N2TEST V14 PASS ' "$SB/asan.log" | tail -1
