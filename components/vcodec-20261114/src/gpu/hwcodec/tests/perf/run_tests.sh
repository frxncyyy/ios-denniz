#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# opus-perf: differential test of the core's decode cache and fast paths (t_fast.c), host only.
# Builds into /tmp/n2hw-perf/test.  Last line: N2TEST PERF PASS <passed>/<total>.
set -euo pipefail
trap 'echo "N2TEST PERF FAIL at line $LINENO"; exit 1' ERR
here=$(cd "$(dirname "$0")" && pwd)
hw=$(cd "$here/../.." && pwd)
out=${N2HW_TMP:-/tmp/n2hw-perf}/test
mkdir -p "$out"
cflags=(-std=c11 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
	-Wno-tautological-constant-out-of-range-compare -I "$hw/include")
srcs=("$hw"/emu/n2a32_*.c "$here/t_fast.c")

# ASan+UBSan -O1 (fewer cases) and -O2 (full counts), one compile job at a time
nice -n 10 clang "${cflags[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-DSTATES16=2 -DT32_TARGET=250000 -DT32_RANDOM=100000 -DA32_RANDOM=30000 "${srcs[@]}" -pthread -lm -o "$out/t_fast_san"
nice -n 10 clang "${cflags[@]}" -O2 "${srcs[@]}" -pthread -lm -o "$out/t_fast_o2"

p=0; t=0
for b in t_fast_san t_fast_o2; do
	echo "== $b"
	if ! r=$("$out/$b"); then
		echo "$r" | tail -30
		echo "N2TEST PERF FAIL $b"
		exit 1
	fi
	echo "$r" | tail -5 | sed 's/^/  /'
	line=$(echo "$r" | grep '^TESTS ')
	p=$((p + $(echo "$line" | sed 's#TESTS \([0-9]*\)/.*#\1#')))
	t=$((t + $(echo "$line" | sed 's#TESTS [0-9]*/\([0-9]*\)#\1#')))
done
echo "N2TEST PERF PASS $p/$t"
