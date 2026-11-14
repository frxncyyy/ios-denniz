#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# P04 host suite: vt/annexb.c against ffmpeg's own avcC/hvcC and length-prefixed samples, and the hardware H.264/HEVC
# encoder objects against gpu/video's fake CoreFoundation/VideoToolbox runtime with a canned vcodecd client.
#
# Everything is built and generated under /tmp/n2hw-P04 (override with N2HW_P04_OUT, still required to be under /tmp).
# Needs ffmpeg with libx264 and libx265; they are the independent oracle, there is no substitute for them here.
# The iOS compile check uses gpu/video/sdk/sysinc and the SDK's resource headers; it is skipped (with a note) if the
# stubs are absent, exactly the way hw-ip's build_ios.sh reports it.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)                 # gpu/hwcodec
VIDEO=$(cd "$ROOT/../video" && pwd)             # gpu/video (read only)
OUT=${N2HW_P04_OUT:-/tmp/n2hw-P04/test}
case $OUT in
	/tmp/*) ;;
	*) echo "run_tests.sh: output directory must be under /tmp (got $OUT)" >&2; exit 2 ;;
esac

CC=${CC:-clang}
NICE=(nice -n 10)
mkdir -p "$OUT"
rm -f "$OUT"/*.o "$OUT"/*.bin "$OUT"/*.log "$OUT"/t_* 2>/dev/null || true

COMMON=(-std=c11 -Wall -Wextra -Werror -DN2_HOST_FAKE
        -I "$VIDEO/include" -I "$VIDEO/fake" -I "$ROOT/include" -I "$ROOT/client" -I "$ROOT/vt" -I "$HERE")
LIB=("$ROOT/vt/annexb.c" "$ROOT/vt/n2hwenc_vt.c" "$ROOT/vt/n2hwenc_client.c" "$ROOT/vt/n2hwenc_register.c")
FAKE=("$VIDEO/fake/fake_apple.c" "$VIDEO/fake/fake_vt.c")
T_ANNEX=("$HERE/t_annexb.c" "$HERE/t_support.c")
T_VT=("$HERE/t_vt.c" "$HERE/t_fakeclient.c" "$HERE/t_support.c")
VECTORS=("$OUT/avc-high.bin" "$OUT/avc-base.bin" "$OUT/hevc-main.bin")

TOTAL=0
VECTORS_OK=0

need_tool() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "run_tests.sh: $1 is required (the independent oracle)" >&2
		exit 1
	fi
}

build_run() {   # build_run <name> <sanitizer flags...> -- <binary args...>
	local name=$1
	shift
	local san=()
	while [ "$1" != "--" ]; do san+=("$1"); shift; done
	shift
	local bin="$OUT/$name"
	if ! "${NICE[@]}" "$CC" "${COMMON[@]}" "${san[@]}" -o "$bin" "${LIB[@]}" "${FAKE[@]}" "$@" \
	     >"$OUT/$name.build.log" 2>&1; then
		cat "$OUT/$name.build.log"
		echo "N2TEST P04 FAIL build $name"
		exit 1
	fi
}

run_and_count() {   # run_and_count <binary> <log> <args...>
	local bin=$1 log=$2
	shift 2
	if ! "$bin" "$@" >"$log" 2>&1; then
		cat "$log"
		echo "N2TEST P04 FAIL run ${bin##*/}"
		exit 1
	fi
	local n
	n=$(awk '/^T_(ANNEXB|VT) PASS [0-9]+$/{print $3}' "$log" | tail -n 1)
	if [ -z "${n:-}" ]; then
		cat "$log"
		echo "N2TEST P04 FAIL ${bin##*/} has no PASS line"
		exit 1
	fi
	cat "$log"
	TOTAL=$((TOTAL + n))
}

need_tool ffmpeg
need_tool ffprobe
need_tool python3
need_tool "$CC"

echo "== P04: ffmpeg vectors (the independent oracle) =="
make_vector() {   # make_vector <name> <raw argument...>
	local name=$1
	shift
	local ext=h264
	case $name in
		hevc*) ext=h265 ;;
	esac
	local raw="$OUT/$name.$ext"
	local mp4="$OUT/$name.mp4"
	local frames
	ffmpeg -hide_banner -loglevel error "$@" "$raw" -y
	frames=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "$raw")
	if [ "$frames" != "25" ]; then
		echo "N2TEST P04 FAIL $name: ffprobe read $frames frames, want 25"
		exit 1
	fi
	# Decode the source once: the vector must be a valid stream before it is used as an expectation.
	if ! ffmpeg -hide_banner -v error -i "$raw" -f null - >/dev/null 2>&1; then
		echo "N2TEST P04 FAIL $name: ffmpeg cannot decode the generated stream"
		exit 1
	fi
	case $name in
		hevc*) ffmpeg -hide_banner -loglevel error -i "$raw" -c copy -bsf:v "filter_units=remove_types=32|33|34" "$mp4" -y ;;
		*)     ffmpeg -hide_banner -loglevel error -i "$raw" -c copy -bsf:v "filter_units=remove_types=7|8"     "$mp4" -y ;;
	esac
	case $name in
		hevc*) python3 "$HERE/mp4_extract.py" "$mp4" hevc "$OUT/$name.bin" ;;
		*)     python3 "$HERE/mp4_extract.py" "$mp4" avc  "$OUT/$name.bin" ;;
	esac
}

make_vector avc-high -f lavfi -i testsrc2=size=176x144:rate=30 -frames:v 25 \
	-c:v libx264 -profile:v high -level 4.1 -x264-params "keyint=10:min-keyint=10:scenecut=0" \
	-pix_fmt yuv420p -f h264
make_vector avc-base -f lavfi -i testsrc2=size=176x144:rate=30 -frames:v 25 \
	-c:v libx264 -profile:v baseline -level 3.0 -x264-params "keyint=10:min-keyint=10:scenecut=0" \
	-pix_fmt yuv420p -f h264
make_vector hevc-main -f lavfi -i testsrc2=size=176x144:rate=30 -frames:v 25 \
	-c:v libx265 -profile:v main -level 4.1 \
	-x265-params "keyint=10:min-keyint=10:scenecut=0:log-level=error" -pix_fmt yuv420p -f hevc
VECTORS_OK=1

echo "== P04: annexb.c, ASan+UBSan =="
build_run t_annexb_asan -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -- "${T_ANNEX[@]}"
run_and_count "$OUT/t_annexb_asan" "$OUT/annexb-asan.log" "${VECTORS[@]}"

echo "== P04: annexb.c, -O2 =="
build_run t_annexb_o2 -O2 -g -- "${T_ANNEX[@]}"
run_and_count "$OUT/t_annexb_o2" "$OUT/annexb-o2.log" "${VECTORS[@]}"

echo "== P04: encoder object, ASan+UBSan =="
build_run t_vt_asan -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -- "${T_VT[@]}"
run_and_count "$OUT/t_vt_asan" "$OUT/vt-asan.log" "$OUT/avc-high.bin" "$OUT/hevc-main.bin"

echo "== P04: encoder object, -O2 =="
build_run t_vt_o2 -O2 -g -- "${T_VT[@]}"
run_and_count "$OUT/t_vt_o2" "$OUT/vt-o2.log" "$OUT/avc-high.bin" "$OUT/hevc-main.bin"

echo "== P04: encoder object, TSan (two sessions on two threads) =="
build_run t_vt_tsan -O1 -g -fsanitize=thread -- "${T_VT[@]}"
run_and_count "$OUT/t_vt_tsan" "$OUT/vt-tsan.log" "$OUT/avc-high.bin" "$OUT/hevc-main.bin"

echo "== P04: link and run against P03's real client library (default seam, no fake client) =="
LINK_OK=0
if [ -f "$ROOT/client/n2vcd_client.c" ] && [ -f "$ROOT/client/n2vcd_transport.c" ] && [ -f "$ROOT/client/n2vc_hw_dev.c" ]; then
	for src in n2vcd_client.c n2vcd_transport.c n2vc_hw_dev.c; do
		if ! "${NICE[@]}" "$CC" -c -std=c11 -O1 -g -Wall -Wextra -Werror -I "$ROOT/include" -I "$ROOT/client" \
			"$ROOT/client/$src" -o "$OUT/link_${src%.c}.o" >"$OUT/link-${src%.c}.log" 2>&1; then
			cat "$OUT/link-${src%.c}.log"
			echo "N2TEST P04 FAIL client compile $src"
			exit 1
		fi
	done
	if ! "${NICE[@]}" "$CC" -std=c11 -O1 -g -Wall -Wextra -Werror "${COMMON[@]}" -o "$OUT/t_link" \
		"$HERE/t_link.c" "${LIB[@]}" "$HERE/t_support.c" "${FAKE[@]}" \
		"$OUT/link_n2vcd_client.o" "$OUT/link_n2vcd_transport.o" "$OUT/link_n2vc_hw_dev.o" \
		-pthread >"$OUT/link.log" 2>&1; then
		cat "$OUT/link.log"
		echo "N2TEST P04 FAIL link against the P03 client"
		exit 1
	fi
	LINK_OUT=$("$OUT/t_link")
	echo "$LINK_OUT"
	case $LINK_OUT in
		"T_LINK registered=3 available=0") TOTAL=$((TOTAL + 2)); LINK_OK=1 ;;
		*) echo "N2TEST P04 FAIL unexpected t_link output"; exit 1 ;;
	esac
else
	echo "P04 link check: skipped (client/n2vcd_client.c is absent)"
fi

echo "== P04: arm64-apple-ios16.0 compile check (build_ios.sh flags) =="
if [ -d "$VIDEO/sdk/sysinc" ] && [ -f "$VIDEO/sdk/sysinc/string.h" ]; then
	RES=$(clang -print-resource-dir)
	IOS_OK=0
	for src in annexb.c n2hwenc_vt.c n2hwenc_client.c n2hwenc_register.c; do
		if ! "${NICE[@]}" "$CC" -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding \
			-nostdinc -isystem "$RES/include" -isystem "$VIDEO/sdk/sysinc" -I "$VIDEO/include" \
			-I "$ROOT/include" -I "$ROOT/client" -I "$ROOT/vt" -Wall -Wextra -Werror \
			-o "$OUT/ios_${src%.c}.o" "$ROOT/vt/$src" >"$OUT/ios-${src%.c}.log" 2>&1; then
			cat "$OUT/ios-${src%.c}.log"
			echo "N2TEST P04 FAIL ios compile $src"
			exit 1
		fi
		IOS_OK=$((IOS_OK + 1))
	done
	file "$OUT"/ios_*.o | sed -n '1,4p'
	echo "P04 ios compile: $IOS_OK/4 objects for arm64-apple-ios16.0"
else
	echo "P04 ios compile: skipped ($VIDEO/sdk/sysinc is absent)"
fi

if [ "$VECTORS_OK" != "1" ]; then
	echo "N2TEST P04 FAIL vectors"
	exit 1
fi

echo "P04 checks: $TOTAL (annexb x2 builds, encoder object x3 builds, P03 link $((LINK_OK * 2)))"
echo "N2TEST P04 PASS $TOTAL/$TOTAL"
