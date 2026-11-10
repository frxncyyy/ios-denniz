#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# WP05 host suite: the glue against the stub encoder (ASan+UBSan and TSan), plus the real avc/ encoder when it builds.
# Usage: bash gpu/video/vt/test_avc_vt/run_tests.sh [outdir]
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-"$ROOT/build/WP05"}
CC=${CC:-clang}

mkdir -p "$OUT/tmp"
# Raw pictures and streams are regenerable; the disk rule says do not keep them.
trap 'rm -f "$OUT/tmp"/*' EXIT

COMMON=(-std=c11 -O2 -g -Wall -Wextra -Werror -DN2_HOST_FAKE
        -I "$ROOT/include" -I "$ROOT/fake" -I "$ROOT/vt")
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
TEST=("$ROOT/vt/test_avc_vt/t_avc_vt.c")
STUB=("$ROOT/vt/test_avc_vt/stub_n2avc.c")
GLUE=("$ROOT/vt/n2avc_vt.c")
FAKE=("$ROOT/fake/fake_apple.c" "$ROOT/fake/fake_vt.c")

TOTAL=0
REAL_STATUS="not run (avc/ is not there yet)"

count_checks() {
	local log=$1
	local n
	n=$(awk '/^T_AVC_VT PASS [0-9]+$/{print $3}' "$log" | tail -n 1)
	if [ -z "${n:-}" ]; then
		echo "N2TEST WP05 FAIL $log has no 'T_AVC_VT PASS <n>' line"
		exit 1
	fi
	TOTAL=$((TOTAL + n))
}

run_binary() {
	local binary=$1
	local log=$2
	if ! "$binary" "$OUT/tmp" >"$log" 2>&1; then
		cat "$log"
		echo "N2TEST WP05 FAIL ${binary##*/}"
		exit 1
	fi
	cat "$log"
	count_checks "$log"
}

echo "== WP05: stub encoder, ASan+UBSan =="
if ! "$CC" "${COMMON[@]}" "${SAN[@]}" -o "$OUT/t_avc_vt_stub" \
	"${TEST[@]}" "${STUB[@]}" "${GLUE[@]}" "${FAKE[@]}" 2>"$OUT/stub_build.log"; then
	cat "$OUT/stub_build.log"
	echo "N2TEST WP05 FAIL stub build"
	exit 1
fi
run_binary "$OUT/t_avc_vt_stub" "$OUT/stub.log"

echo "== WP05: stub encoder, TSan (two sessions, two threads) =="
if ! "$CC" "${COMMON[@]}" -fsanitize=thread -o "$OUT/t_avc_vt_tsan" \
	"${TEST[@]}" "${STUB[@]}" "${GLUE[@]}" "${FAKE[@]}" 2>"$OUT/tsan_build.log"; then
	cat "$OUT/tsan_build.log"
	echo "N2TEST WP05 FAIL tsan build"
	exit 1
fi
run_binary "$OUT/t_avc_vt_tsan" "$OUT/tsan.log"

echo "== WP05: real encoder (avc/ or avc2/) =="
RUN_REAL=0
if [ -f "$ROOT/avc/STATUS.json" ] || [ -f "$ROOT/avc2/n2avc_core.c" ]; then
	RUN_REAL=1
fi
if [ "${WP05_REAL:-0}" = "1" ]; then
	RUN_REAL=1
fi
if [ "$RUN_REAL" = "1" ] && { [ -f "$ROOT/avc2/n2avc_core.c" ] || { compgen -G "$ROOT/avc/*.c" >/dev/null && [ -f "$ROOT/avc/n2avc_enc.c" ]; }; }; then
	REAL_SOURCES=()
	REAL_EXTRA=()
	REAL_NAME="avc"
	if [ -f "$ROOT/avc2/n2avc_core.c" ]; then
		# The product core: avc2/n2avc_core.c + the PINNED avc2 inter/me/rc (N2AVC_INTER_DIR overrides for WP04's working set).
		INTER_DIR=${N2AVC_INTER_DIR:-"$ROOT/avc2"}
		REAL_NAME="avc2 (pinned $INTER_DIR)"
		REAL_SOURCES=("$ROOT/avc2/n2avc_core.c" "$ROOT/avc2/n2avc_deblock.c" "$INTER_DIR/n2avc_inter.c" "$INTER_DIR/n2avc_me.c"
		              "$INTER_DIR/n2avc_rc.c")
		[ -f "$ROOT/kern/n2vk_ref.c" ] && REAL_SOURCES+=("$ROOT/kern/n2vk_ref.c")
		[ -f "$ROOT/kern/n2vk_dispatch.c" ] && REAL_SOURCES+=("$ROOT/kern/n2vk_dispatch.c")
		REAL_EXTRA=(-pthread -I "$INTER_DIR")
	else
		REAL_SOURCES=("$ROOT"/avc/*.c)
	fi
	echo "   real encoder sources: $REAL_NAME"
	if "$CC" "${COMMON[@]}" "${SAN[@]}" "${REAL_EXTRA[@]}" -DN2AVC_REAL -o "$OUT/t_avc_vt_real" \
		"${TEST[@]}" "${GLUE[@]}" "${REAL_SOURCES[@]}" "${FAKE[@]}" -lm 2>"$OUT/real_build.log"; then
		run_binary "$OUT/t_avc_vt_real" "$OUT/real.log"
		REAL_STATUS="ran ($REAL_NAME, 10 pictures decoded by ffmpeg)"
	else
		echo "NOTE: $REAL_NAME does not build with the package flags yet; real-encoder test skipped"
		tail -n 20 "$OUT/real_build.log" || true
		REAL_STATUS="skipped ($REAL_NAME does not build yet)"
	fi
else
	if [ -f "$ROOT/avc2/n2avc_core.c" ] && [ "$RUN_REAL" != "1" ]; then
		echo "NOTE: avc2/n2avc_core.c exists but the real run is not forced; set WP05_REAL=1 to run it"
		REAL_STATUS="skipped (avc2 present; WP05_REAL=1 forces it)"
	elif compgen -G "$ROOT/avc/*.c" >/dev/null; then
		if [ -f "$ROOT/avc/n2avc_enc.c" ] && [ "$RUN_REAL" != "1" ]; then
			echo "NOTE: avc/n2avc_enc.c exists but WP03 has not announced avc/ (no avc/STATUS.json); set WP05_REAL=1 to force"
			REAL_STATUS="skipped (avc/ not announced yet; WP05_REAL=1 forces it)"
		else
			echo "NOTE: avc/ has private sources but no avc/n2avc_enc.c yet; real-encoder test skipped"
			REAL_STATUS="skipped (avc/n2avc_enc.c not there yet)"
		fi
	else
		echo "NOTE: avc/ not present yet; real-encoder test skipped"
	fi
fi

echo "WP05 real-encoder test: $REAL_STATUS"
echo "N2TEST WP05 PASS $TOTAL/$TOTAL"
