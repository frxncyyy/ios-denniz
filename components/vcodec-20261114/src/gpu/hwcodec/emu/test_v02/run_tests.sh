#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V02 host tests: builds into /tmp/n2hw-v02 only.  Last line on success: N2TEST V02 PASS <passed>/<total>
set -euo pipefail
trap 'echo "N2TEST V02 FAIL at line $LINENO" >&2; exit 1' ERR

HERE=$(cd "$(dirname "$0")" && pwd)
EMU=$(cd "$HERE/.." && pwd)
HW=$(cd "$EMU/.." && pwd)
OUT=/tmp/n2hw-v02
ID=V02

rm -rf "$OUT"
mkdir -p "$OUT"

# llvm-mc is the only source of encodings (never hand-typed hex).  gen.py also emits the guest programs.
python3 "$HERE/gen.py" "$OUT" >/dev/null

CORE="$EMU/n2a32_core.c $EMU/n2a32_mem.c $EMU/n2a32_excl.c $EMU/n2a32_stub_classes.c"
CFLAGS="-std=c11 -g -Wall -Wextra -Werror -I$HW/include -I$OUT"
SAN="-O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
OPT="-O2"
TSANF="-O1 -fsanitize=thread -fno-omit-frame-pointer"

build() { # build <out> <flags> <sources...>
	local out=$1 flags=$2
	shift 2
	if ! nice -n 10 clang $CFLAGS $flags "$@" -pthread -o "$out"; then
		echo "N2TEST $ID FAIL build $out" >&2
		exit 1
	fi
}

run_tests() { # run_tests <binary> <label> <counters-file>
	local bin=$1 label=$2 cf=$3 out
	if ! out=$("$bin"); then
		echo "$out" | sed 's/^/  /'
		echo "N2TEST $ID FAIL $label (non-zero exit)" >&2
		exit 1
	fi
	echo "$out" | sed 's/^/  /'
	echo "$out" | grep -q '^TESTS ' || { echo "N2TEST $ID FAIL $label produced no TESTS line"; exit 1; }
	local line
	line=$(echo "$out" | grep '^TESTS ')
	local p=${line#TESTS }; p=${p%/*}
	local t=${line#TESTS }; t=${t#*/}
	echo "$p $t" >> "$cf"
}

counters="$OUT/counts.txt"
: > "$counters"

echo "== V02 core: ASan+UBSan -O1 =="
build "$OUT/t_core_san" "$SAN" $CORE "$HERE/t_core.c"
run_tests "$OUT/t_core_san" asan "$counters"

echo "== V02 core: -O2 (no sanitizers) =="
build "$OUT/t_core_opt" "$OPT" $CORE "$HERE/t_core.c"
run_tests "$OUT/t_core_opt" opt "$counters"

echo "== V02 threads: TSan -O1 =="
build "$OUT/t_threads_tsan" "$TSANF" $CORE "$HERE/t_threads.c"
run_tests "$OUT/t_threads_tsan" tsan "$counters"

awk '{ p += $1; t += $2 } END { printf "N2TEST V02 PASS %d/%d\n", p, t }' "$counters"
