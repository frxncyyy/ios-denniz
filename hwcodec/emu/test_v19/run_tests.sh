#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# V19 host tests: NEON shifts by immediate, widening/narrowing, two regs + scalar.
#
#   bash gpu/hwcodec/emu/test_v19/run_tests.sh
#
# Builds only under /tmp/n2hw-V19.  Encodings come from llvm-mc (gen_vectors.py), expected
# values from the ARM ARM pseudocode model in the same script; no hand-typed encoding hex.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
hw=$(cd "$here/../.." && pwd)
inc="$hw/include"
src="$here/../n2a32_neon_shift.c"
build=/tmp/n2hw-V19

rm -rf "$build"
mkdir -p "$build"

python3 "$here/gen_vectors.py" "$build"

cc=${CC:-clang}
flags=(-std=c11 -Wall -Wextra -Werror -I "$inc" -I "$build")
san=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
opt=(-O2)

nice -n 10 "$cc" "${flags[@]}" "${san[@]}" "$src" "$here/t_v19.c" -lm -o "$build/t_v19_asan"
nice -n 10 "$cc" "${flags[@]}" "${opt[@]}" "$src" "$here/t_v19.c" -lm -o "$build/t_v19_o2"

asan_out=$("$build/t_v19_asan") || {
	printf '%s\n' "$asan_out"
	echo "N2TEST V19 FAIL asan-run"
	exit 1
}
o2_out=$("$build/t_v19_o2") || {
	printf '%s\n' "$o2_out"
	echo "N2TEST V19 FAIL o2-run"
	exit 1
}

asan_last=$(printf '%s\n' "$asan_out" | tail -1)
o2_last=$(printf '%s\n' "$o2_out" | tail -1)
if [ "$asan_last" != "$o2_last" ]; then
	printf '%s\n' "$asan_out"
	printf '%s\n' "$o2_out"
	echo "N2TEST V19 FAIL asan/o2 disagree"
	exit 1
fi
case "$asan_last" in
N2TEST\ V19\ PASS*) ;;
*)
	printf '%s\n' "$asan_out"
	echo "N2TEST V19 FAIL summary"
	exit 1
	;;
esac

# Show the sweep line from the sanitizer run for the STATUS file, then the summary.
printf '%s\n' "$asan_out" | grep '^sweep:'
printf '%s\n' "$asan_out" | tail -1
