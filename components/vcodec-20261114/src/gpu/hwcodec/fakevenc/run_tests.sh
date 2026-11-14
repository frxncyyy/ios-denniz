#!/bin/bash
# SPDX-License-Identifier: MIT
# gpu/hwcodec/fakevenc/run_tests.sh -- host test of the fake VENC model (no vendor code, no phone).
# Builds under /tmp/n2hw-opus-venc/test: ASan+UBSan -O1, -O2, TSan.  Oracles: ffmpeg/libx264 make the
# source picture and an independent SPS/PPS; ffmpeg decodes the model's IDR frame, which must equal the
# source exactly (I_PCM is lossless).
set -euo pipefail
ID=fakevenc
HERE="$(cd "$(dirname "$0")" && pwd)"
HW="$(cd "$HERE/.." && pwd)"
SB=/tmp/n2hw-opus-venc/test
fail() { echo "N2TEST $ID FAIL $*"; exit 1; }
command -v ffmpeg >/dev/null || fail "ffmpeg (oracle) not installed"
rm -rf "$SB"
mkdir -p "$SB"

# Oracle inputs: a 320x240 NV12 test picture; SPS/PPS of a Baseline (CAVLC) libx264 stream of it.
ffmpeg -v error -f lavfi -i testsrc2=size=320x240:rate=1 -frames:v 1 -pix_fmt nv12 -f rawvideo "$SB/src.nv12"
ffmpeg -v error -f rawvideo -pix_fmt nv12 -s 320x240 -i "$SB/src.nv12" -c:v libx264 -profile:v baseline \
	-f h264 "$SB/x264.h264"
ffmpeg -v error -f rawvideo -pix_fmt nv12 -s 320x240 -i "$SB/src.nv12" -pix_fmt yuv420p -f rawvideo "$SB/src.yuv"

CF=(-std=c11 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -I "$HW/include" -I "$HW/tests/hw" -I "$HERE" -pthread)
passed=0
total=0
run() {
	local name=$1; shift
	total=$((total + 1))
	nice -n 10 clang "${CF[@]}" "$@" -o "$SB/t_$name" "$HERE/t_fakevenc.c" "$HERE/n2venc_fake.c" \
		"$HW/tests/hw/n2vc_hw_fake.c" || fail "$name build"
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	TSAN_OPTIONS=halt_on_error=1 "$SB/t_$name" "$SB/src.nv12" "$SB/x264.h264" "$SB/out-$name.h264" \
		>"$SB/$name.log" 2>&1 || { cat "$SB/$name.log"; fail "$name run"; }
	tail -1 "$SB/$name.log"
	grep -q "^N2TEST fakevenc-unit PASS" "$SB/$name.log" || fail "$name unit"
	# Oracle decode: the model's frame (with libx264's SPS/PPS) must decode to the source, bit-exact.
	ffmpeg -v error -i "$SB/out-$name.h264" -f rawvideo -pix_fmt yuv420p "$SB/dec-$name.yuv" || fail "$name decode"
	cmp -s "$SB/dec-$name.yuv" "$SB/src.yuv" || fail "$name decoded picture differs from the source"
	echo "$name: decoded frame equals the source (ffmpeg)"
	passed=$((passed + 1))
}
run asan -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
run o2 -O2
run tsan -O1 -fsanitize=thread
rm -f "$SB"/t_*
echo "N2TEST $ID PASS $passed/$total"
