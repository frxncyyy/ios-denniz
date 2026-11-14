#!/bin/bash
# SPDX-License-Identifier: MIT
# Host tests for package V18 (NEON three registers of the same length).
# Builds only under /tmp/n2hw-V18.  No arguments.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
HW="$(cd "$HERE/../.." && pwd)"
INC="$HW/include"
OUT=/tmp/n2hw-V18
CC="${CC:-clang}"
mkdir -p "$OUT"

fail() { echo "N2TEST V18 FAIL $*"; exit 1; }

# Encodings and expected values (llvm-mc + ref_model.py), and the llvm-mc decode oracle.
python3 "$HERE/gen_cases.py" "$OUT" || fail "case generation"
python3 "$HERE/gen_decode_oracle.py" "$OUT" || fail "decode oracle generation"

COMMON="-std=c11 -g -Wall -Wextra -Werror -I $INC -I $OUT -lm"
SRC="$HERE/t_3same.c $HW/emu/n2a32_neon_3same.c"

# shellcheck disable=SC2086
$CC $COMMON -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	$SRC -o "$OUT/t_v18_asan" || fail "clang -O1 ASan+UBSan build"
ASAN_OPTIONS=detect_leaks=0 "$OUT/t_v18_asan" || fail "ASan+UBSan run"

# shellcheck disable=SC2086
$CC $COMMON -O2 $SRC -o "$OUT/t_v18_o2" || fail "clang -O2 build"
"$OUT/t_v18_o2" || fail "-O2 run"

# arm64-apple-ios16.0 compile check with gpu/video's freestanding SDK headers (same recipe as J02).
VIDEO="$HW/../video"
SYSINC="$VIDEO/sdk/sysinc"
RES=$($CC -print-resource-dir)
if [ -d "$SYSINC" ]; then
	if ! $CC -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden \
		-ffreestanding -nostdinc -isystem "$RES/include" -isystem "$SYSINC" -I "$INC" \
		-Wall -Wextra -Werror "$HW/emu/n2a32_neon_3same.c" \
		-o "$OUT/n2a32_neon_3same.ios.o" > "$OUT/ios_compile.log" 2>&1; then
		cat "$OUT/ios_compile.log"
		fail "arm64-apple-ios16.0 compile"
	fi
	echo "V18-IOS n2a32_neon_3same.c compiles for arm64-apple-ios16.0 with zero warnings"
else
	echo "V18-IOS SKIP (gpu/video/sdk/sysinc missing)"
fi

# Acceptance check against the phone's own vendor libraries (skipped when they are not there):
# every mnemonic of this class that they use must be implemented.
python3 "$HERE/check_vendor_words.py" || fail "vendor-library class mnemonics"
