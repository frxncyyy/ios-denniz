#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail
# K02: host test of kern/bsd/dev/nord2_vcodec.inc against the MT6893 register model in t_k02.c.
# Builds only under /tmp/n2hw-K02/test.  Two builds: ASan+UBSan -O1 and plain -O2.

ID=K02
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"              # gpu/hwcodec
SB="/tmp/n2hw-K02/test"
INC="$ROOT/include"
SRC="$HERE/t_k02.c"
fail() { echo "N2TEST K02 FAIL $*"; exit 1; }

rm -rf "$SB"
mkdir -p "$SB"

CFLAGS=(-std=c11 -g -Wall -Wextra -Werror -I "$INC")

echo "== clang -O1 ASan+UBSan"
nice -n 10 clang "${CFLAGS[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	-o "$SB/t_k02-san" "$SRC" || fail "sanitizer build"
echo "== clang -O2"
nice -n 10 clang "${CFLAGS[@]}" -O2 -o "$SB/t_k02-o2" "$SRC" || fail "-O2 build"
echo "== clang -O2 N2VC_VENC_SLOW=0 (the 624 MHz knob)"
nice -n 10 clang "${CFLAGS[@]}" -O2 -DN2VC_VENC_SLOW=0 -o "$SB/t_k02-fast" "$SRC" || fail "fast-knob build"

echo "== run ASan+UBSan"
ASAN_OPTIONS=detect_leaks=1 "$SB/t_k02-san" > "$SB/out-san.txt" 2>&1 || true
cat "$SB/out-san.txt"
line=$(tail -n 1 "$SB/out-san.txt")
case "$line" in
	N2TEST\ K02\ PASS*) ;;
	*) fail "sanitizer run: $line" ;;
esac

echo "== run -O2"
"$SB/t_k02-o2" > "$SB/out-o2.txt" 2>&1 || true
cat "$SB/out-o2.txt"
line2=$(tail -n 1 "$SB/out-o2.txt")
case "$line2" in
	N2TEST\ K02\ PASS*) ;;
	*) fail "-O2 run: $line2" ;;
esac

echo "== run -O2 N2VC_VENC_SLOW=0"
"$SB/t_k02-fast" > "$SB/out-fast.txt" 2>&1 || true
cat "$SB/out-fast.txt"
line3=$(tail -n 1 "$SB/out-fast.txt")
case "$line3" in
	N2TEST\ K02\ PASS*) ;;
	*) fail "fast-knob run: $line3" ;;
esac

rm -f "$SB/t_k02-san" "$SB/t_k02-o2" "$SB/t_k02-fast"
echo "$line"
