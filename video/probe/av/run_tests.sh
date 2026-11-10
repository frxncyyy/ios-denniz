#!/bin/bash
# Host tests of probe/av (WP06): probe_av.c against the made-up Objective-C runtime in test/fake_objc.c, under
# ASan+UBSan, plus the zero-warning iOS arm64 compile of probe_av.c with the PLAN.md 4.3 flags. HOST TESTS ONLY:
# the fake proves ordering, nil handling, dictionary contents and the block/delegate plumbing; it says nothing about
# whether AVFoundation on the phone accepts these selectors or settings (STATUS.json says so).
#
# usage: run_tests.sh [outdir]        (default gpu/video/build/WP06)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
VIDEO=$(cd "$HERE/../.." && pwd)
OUT=${1:-$VIDEO/build/WP06}
CC=${CC:-clang}
TMP=$OUT/tmp
mkdir -p "$OUT" "$TMP"

HOST_FLAGS=(
	-std=c11 -O2 -g -Wall -Wextra -Werror
	-fsanitize=address,undefined -fno-sanitize-recover=undefined
	-D_DEFAULT_SOURCE
	-I "$VIDEO/include" -I "$VIDEO/probe" -I "$HERE" -I "$HERE/test"
	-Ddlopen=n2fake_dlopen -Ddlsym=n2fake_dlsym
)

echo "== building the host oracle =="
"$CC" "${HOST_FLAGS[@]}" -o "$OUT/t_av" \
	"$HERE/probe_av.c" "$HERE/test/fake_objc.c" "$HERE/test/t_av.c" -lm

echo "== running the host oracle (ASan+UBSan) =="
LOG=$OUT/t_av.log
set +e
N2_PROBE_DB="$TMP" ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	"$OUT/t_av" "$TMP" >"$LOG" 2>&1
STATUS=$?
set -e
cat "$LOG"
if [ $STATUS -ne 0 ]; then
	echo "N2TEST WP06 FAIL host oracle exit $STATUS" >&2
	exit 1
fi
if ! grep -q '^N2TEST WP06 PASS ' "$LOG"; then
	echo "N2TEST WP06 FAIL no PASS line in the host oracle output" >&2
	exit 1
fi

echo "== iOS arm64 compile check (PLAN.md 4.3 flags, zero warnings) =="
RES=$(clang -print-resource-dir)
IOS_LOG=$OUT/probe_av_ios.log
set +e
clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc \
	-isystem "$RES/include" -isystem "$VIDEO/sdk/sysinc" \
	-I "$VIDEO/include" -I "$VIDEO/probe" -I "$HERE" \
	-Wall -Wextra \
	-o "$OUT/probe_av_ios.o" "$HERE/probe_av.c" >"$IOS_LOG" 2>&1
IOS_STATUS=$?
set -e
if [ -s "$IOS_LOG" ]; then
	cat "$IOS_LOG" >&2
	echo "N2TEST WP06 FAIL iOS compile produced warnings or errors" >&2
	exit 1
fi
if [ $IOS_STATUS -ne 0 ]; then
	echo "N2TEST WP06 FAIL iOS compile exit $IOS_STATUS" >&2
	exit 1
fi

tail -n 1 "$LOG"
