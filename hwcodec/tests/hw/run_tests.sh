#!/bin/bash
set -euo pipefail
# T02-hw: host test of the fake n2vc_hw back end (tests/hw/n2vc_hw_fake.c).
# Builds only under /tmp/n2hw-T02/hw; three configurations: ASan+UBSan, -O2, TSan.
ID=T02-hw
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SB="/tmp/n2hw-T02/hw"
INC="$ROOT/include"
SRC="$HERE/t_fake.c"
FAKE="$HERE/n2vc_hw_fake.c"
fail() { echo "N2TEST $ID FAIL $*"; exit 1; }

rm -rf "$SB"
mkdir -p "$SB"

CFLAGS=(-std=c11 -g -Wall -Wextra -Werror -I "$INC" -I "$HERE" -pthread)

build_run() {
	local name="$1"; shift
	local out="$SB/$name"
	echo "== $name"
	# shellcheck disable=SC2086
	nice -n 10 clang "${CFLAGS[@]}" "$@" -o "$out" "$SRC" "$FAKE" || fail "$name build"
	local log="$SB/$name.log"
	set +e
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	TSAN_OPTIONS=halt_on_error=1 \
	"$out" "$HERE/scripts" "$SB" >"$log" 2>&1
	local rc=$?
	set -e
	cat "$log"
	if [ "$rc" -ne 0 ]; then
		fail "$name run (exit $rc)"
	fi
	local line
	line=$(tail -n 1 "$log")
	case "$line" in
	"N2TEST $ID PASS "*) ;;
	*) fail "$name last line: $line" ;;
	esac
}

build_run asan-ubsan -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
build_run o2 -O2
build_run tsan -O1 -fsanitize=thread

rm -f "$SB/asan-ubsan" "$SB/o2" "$SB/tsan"
tail -n 1 "$SB/o2.log"
