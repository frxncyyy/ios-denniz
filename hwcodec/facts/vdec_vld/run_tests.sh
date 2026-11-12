#!/bin/bash
# SPDX-License-Identifier: MIT
# gpu/hwcodec/facts/vdec_vld/run_tests.sh -- host test of the VDEC VLD reference model (opus-dec2). No vendor code, no phone.
set -euo pipefail
ID=VDECVLD
HERE="$(cd "$(dirname "$0")" && pwd)"
SB=/tmp/n2hw-opus-dec2/vld
fail() { echo "N2TEST $ID FAIL $*"; exit 1; }
rm -rf "$SB"; mkdir -p "$SB"
CF=(-std=c11 -g -Wall -Wextra -Werror -I "$HERE")
passed=0; total=0
run() {
	local name=$1; shift
	total=$((total + 1))
	nice -n 10 clang "${CF[@]}" "$@" -o "$SB/t_$name" "$HERE/t_vdec_vld.c" "$HERE/n2_vdec_vld.c" || fail "$name build"
	"$SB/t_$name" > "$SB/$name.log" 2>&1 || { cat "$SB/$name.log"; fail "$name run"; }
	tail -1 "$SB/$name.log"
	passed=$((passed + 1))
}
run asan -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
run o2 -O2
echo "N2TEST $ID PASS $passed/$total"
