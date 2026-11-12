#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# V11 -- host tests for n2a32_arm_media (A32 media / saturating arithmetic / divide).
# Builds only under /tmp/n2hw-V11/ (RAM-backed), never in the work tree.
#
#   bash gpu/hwcodec/emu/test_v11/run_tests.sh
#
# Step 1: encodings are produced from v11_enc.s by llvm-mc (no hand-typed hex).
# Step 2: the test is built twice -- -O1 with ASan+UBSan, and -O2 without -- and
#         both runs must print the same summary line.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
hw=$(cd "$here/../.." && pwd)
inc="$hw/include"
src="$here/../n2a32_arm_media.c"
build=/tmp/n2hw-V11

mkdir -p "$build"
rm -f "$build/v11_enc.h" "$build/t_v11_asan" "$build/t_v11_o2"

python3 "$here/gen_enc.py" "$here/v11_enc.s" "$build/v11_enc.h"

cc=${CC:-clang}
flags=(-std=c11 -Wall -Wextra -Werror -I "$inc" -I "$here" -I "$build")

nice -n 10 "$cc" "${flags[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	"$src" "$here/t_v11.c" -o "$build/t_v11_asan"
nice -n 10 "$cc" "${flags[@]}" -O2 "$src" "$here/t_v11.c" -o "$build/t_v11_o2"

asan_out=$("$build/t_v11_asan") || {
	echo "$asan_out"
	echo "N2TEST V11 FAIL asan-run"
	exit 1
}
o2_out=$("$build/t_v11_o2") || {
	echo "$o2_out"
	echo "N2TEST V11 FAIL o2-run"
	exit 1
}

asan_last=$(printf '%s\n' "$asan_out" | tail -1)
o2_last=$(printf '%s\n' "$o2_out" | tail -1)
if [ "$asan_last" != "$o2_last" ]; then
	printf '%s\n' "$asan_out"
	printf '%s\n' "$o2_out"
	echo "N2TEST V11 FAIL asan/o2 disagree"
	exit 1
fi
case "$asan_last" in
N2TEST\ V11\ PASS*) ;;
*)
	printf '%s\n' "$asan_out"
	echo "N2TEST V11 FAIL summary"
	exit 1
	;;
esac
printf '%s\n' "$asan_out" | tail -1
printf '%s\n' "$o2_out" | tail -1
