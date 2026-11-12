#!/bin/bash
# SPDX-License-Identifier: MIT
# Host tests for package V20 (NEON two-register misc, VEXT/VTBL/VDUP/immediates, structure load/store).
# Builds only under /tmp/n2hw-V20.  No arguments.  Last line on success: N2TEST V20 PASS n/n
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
INC="$(cd "$HERE/../.." && pwd)/include"
OUT=/tmp/n2hw-V20
CC="${CC:-clang}"
mkdir -p "$OUT"

fail() { echo "N2TEST V20 FAIL $*"; exit 1; }

python3 "$HERE/mkenc.py" "$OUT/enc_v20.h" "$HERE/misc.s" "$HERE/ldst.s" || fail "llvm-mc encoding generation"
python3 "$HERE/gen_space.py" "$OUT/space_v20.h" || fail "llvm-mc space sweep"

COMMON="-std=c11 -g -Wall -Wextra -Werror -I $INC -I $HERE -I $OUT"
IMPL="$HERE/../n2a32_neon_misc.c $HERE/../n2a32_neon_ldst.c"

total=0
run_suite() {
	local tag="$1"; shift
	local flags="$1"; shift
	local prog
	for t in t_misc t_ldst t_cover t_sweep; do
		prog="$OUT/${t}_${tag}"
		# shellcheck disable=SC2086
		$CC $COMMON $flags "$HERE/$t.c" $IMPL -o "$prog" || fail "build $t ($tag)"
		"$prog" || fail "run $t ($tag)"
	done
}

run_suite asan "-O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
run_suite o2 "-O2"

# count the checks of every run (the suites print "t_xxx: N checks, 0 failures")
for f in "$OUT"/t_*_asan "$OUT"/t_*_o2; do
	n=$("$f" 2>/dev/null | sed -n 's/^t_[a-z]*: \([0-9]*\) checks, 0 failures$/\1/p' | head -1)
	[ -n "$n" ] || fail "could not read the check count of $f"
	total=$((total + n))
done

du -sh "$OUT" >/dev/null 2>&1 || true
echo "N2TEST V20 PASS $total/$total"
