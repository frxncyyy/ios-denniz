#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Host tests for WP02: the fake Apple runtime.  Usage: bash gpu/video/fake/run_tests.sh [outdir]
set -euo pipefail

DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$DIR/.." && pwd)
OUT=${1:-"$ROOT/build/WP02"}
CC=${CC:-clang}

CFLAGS=(
	-std=c11 -O2 -g -Wall -Wextra -Werror
	-fsanitize=address,undefined -fno-sanitize-recover=undefined
	-DN2_HOST_FAKE
	-I "$ROOT/include" -I "$DIR"
)

mkdir -p "$OUT"

fail() {
	echo "N2TEST WP02 FAIL $*"
	exit 1
}

compile() {
	local src=$1 out=$2
	if ! "$CC" "${CFLAGS[@]}" -pthread -c "$src" -o "$out"; then
		fail "compile $(basename "$src")"
	fi
}

compile "$DIR/fake_apple.c" "$OUT/fake_apple.o"
compile "$DIR/fake_vt.c" "$OUT/fake_vt.o"
compile "$DIR/test/t_fake.c" "$OUT/t_fake.o"

if ! "$CC" "${CFLAGS[@]}" -pthread "$OUT/fake_apple.o" "$OUT/fake_vt.o" "$OUT/t_fake.o" -o "$OUT/t_fake"; then
	fail "link t_fake"
fi

set +e
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "$OUT/t_fake"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
	fail "host test exited $rc"
fi

# The fake's refcounting and intern table are shared between threads; run the same suite once under TSan.
TSAN_OUT="$OUT/tsan"
mkdir -p "$TSAN_OUT"
TSAN_FLAGS=(
	-std=c11 -O1 -g -Wall -Wextra -Werror
	-fsanitize=thread
	-DN2_HOST_FAKE
	-I "$ROOT/include" -I "$DIR"
)
for src in "$DIR/fake_apple.c" "$DIR/fake_vt.c" "$DIR/test/t_fake.c"; do
	if ! "$CC" "${TSAN_FLAGS[@]}" -pthread -c "$src" -o "$TSAN_OUT/$(basename "${src%.c}").o"; then
		fail "tsan compile $(basename "$src")"
	fi
done
if ! "$CC" "${TSAN_FLAGS[@]}" -pthread "$TSAN_OUT/fake_apple.o" "$TSAN_OUT/fake_vt.o" \
     "$TSAN_OUT/t_fake.o" -o "$TSAN_OUT/t_fake"; then
	fail "tsan link t_fake"
fi
set +e
TSAN_OPTIONS=halt_on_error=1 "$TSAN_OUT/t_fake"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
	fail "tsan host test exited $rc"
fi
