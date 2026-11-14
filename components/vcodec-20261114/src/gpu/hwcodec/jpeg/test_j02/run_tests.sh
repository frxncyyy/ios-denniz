#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# J02 host suite: rwjpeg.c (client library), rwjpeg_tool.c (the on-phone exerciser), n2jpeg_hw_vt.c (the VideoToolbox
# hardware-JPEG object) against a cjpeg-backed fake /dev/redwood-jpeg and gpu/video's fake Apple runtime.
#
# Builds and runs:
#   - t_rwjpeg  under ASan+UBSan and -O2 (all pixel arithmetic, marker surgery, every error path);
#   - rwjpeg_tool with -DN2RWJ_TOOL_TEST: selftest (all 11 quality codes), pattern vs gpu/hwcodec/tools/hwc.py, enc + djpeg,
#     bench, info, and the production build's no-device exit code;
#   - t_vt_hw    under ASan+UBSan, TSan and -O2 (vtable, 420v/420f frames, BGRA formats, properties, error emits, leaks);
#   - the three sources compiled for arm64-apple-ios16.0 with gpu/video's freestanding SDK headers.
#
# Everything is built under /tmp/n2hw-J02 (the sandbox's private tmp).  No phone, no network.  Last line:
#   N2TEST J02 PASS <n>/<n>     or     N2TEST J02 FAIL <what>
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)              # gpu/hwcodec
VIDEO=$(cd "$ROOT/../video" && pwd)
OUT=${N2J02_OUT:-/tmp/n2hw-J02}
CC=${CC:-clang}
NICE=${NICE:-nice -n 10}      # WORKER_RULES 2: builds are niced; one compiler process at a time
CJPEG=${N2J02_CJPEG:-$(command -v cjpeg || true)}
DJPEG=${N2J02_DJPEG:-$(command -v djpeg || true)}
TOTAL=0
LOGS=()

mkdir -p "$OUT/scratch"
export N2J02_SCRATCH="$OUT/scratch"
export N2J02_CJPEG="$CJPEG"

fail() {
	echo "N2TEST J02 FAIL $*"
	exit 1
}

need_tool() {
	[ -n "$CJPEG" ] && [ -x "$CJPEG" ] || fail "cjpeg is required (the test oracle) but was not found"
	[ -n "$DJPEG" ] && [ -x "$DJPEG" ] || fail "djpeg is required (the test oracle) but was not found"
}

# add_n <log> <extended-regex> <field> -- take "<passed>/<total>" from the last matching line and add its numerator
add_n() {
	local log=$1 re=$2 field=$3 n
	n=$(awk -v re="$re" -v field="$field" '$0 ~ re {print $field}' "$log" | tail -n 1)
	case "$n" in
		*/*) n=${n%%/*} ;;
	esac
	[ -n "${n:-}" ] || fail "no line matching '$re' in $log"
	TOTAL=$((TOTAL + n))
}

run_logged() {   # run_logged <log> <binary> [args...]
	local log=$1
	shift
	if ! "$@" >"$log" 2>&1; then
		cat "$log"
		fail "exit status of ${1##*/}"
	fi
	cat "$log"
	LOGS+=("$log")
}

need_tool

COMMON=(-std=c11 -g -Wall -Wextra -Werror -I "$ROOT/include" -I "$ROOT/jpeg" -I "$HERE")
SAN=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
OPT=(-O2)
TOOL_TEST=(-DN2RWJ_TOOL_TEST)

echo "== t_rwjpeg: ASan+UBSan =="
$NICE "$CC" "${COMMON[@]}" "${SAN[@]}" "$HERE/t_rwjpeg.c" "$ROOT/jpeg/rwjpeg.c" "$HERE/fake_dev.c" \
	-o "$OUT/t_rwjpeg_asan" || fail "t_rwjpeg ASan build"
run_logged "$OUT/t_rwjpeg_asan.log" "$OUT/t_rwjpeg_asan"
add_n "$OUT/t_rwjpeg_asan.log" '^T_RWJPEG PASS ' 3
"$DJPEG" -outfile /dev/null "$OUT/scratch/enc640.jpg" || fail "djpeg on the library's 640x480 output"
"$DJPEG" -outfile /dev/null "$OUT/scratch/enc16_bgra.jpg" || fail "djpeg on the BGRA-converted output"
"$DJPEG" -outfile /dev/null "$OUT/scratch/enc640_exif.jpg" || fail "djpeg on the EXIF post-surgery output"
TOTAL=$((TOTAL + 3))

echo "== t_rwjpeg: -O2 =="
$NICE "$CC" "${COMMON[@]}" "${OPT[@]}" "$HERE/t_rwjpeg.c" "$ROOT/jpeg/rwjpeg.c" "$HERE/fake_dev.c" \
	-o "$OUT/t_rwjpeg_o2" || fail "t_rwjpeg -O2 build"
run_logged "$OUT/t_rwjpeg_o2.log" "$OUT/t_rwjpeg_o2"
add_n "$OUT/t_rwjpeg_o2.log" '^T_RWJPEG PASS ' 3

echo "== rwjpeg_tool (test build, same main() as on the phone) =="
$NICE "$CC" "${COMMON[@]}" "${TOOL_TEST[@]}" "${SAN[@]}" "$ROOT/jpeg/rwjpeg_tool.c" "$ROOT/jpeg/rwjpeg.c" \
	"$HERE/fake_dev.c" -o "$OUT/rwjpeg_tool_asan" || fail "rwjpeg_tool ASan build"
$NICE "$CC" "${COMMON[@]}" "${TOOL_TEST[@]}" "${OPT[@]}" "$ROOT/jpeg/rwjpeg_tool.c" "$ROOT/jpeg/rwjpeg.c" \
	"$HERE/fake_dev.c" -o "$OUT/rwjpeg_tool_o2" || fail "rwjpeg_tool -O2 build"

run_logged "$OUT/tool_selftest_asan.log" "$OUT/rwjpeg_tool_asan" selftest
add_n "$OUT/tool_selftest_asan.log" '^N2JPG selftest 640x480 PASS ' 5
run_logged "$OUT/tool_selftest_o2.log" "$OUT/rwjpeg_tool_o2" selftest
add_n "$OUT/tool_selftest_o2.log" '^N2JPG selftest 640x480 PASS ' 5

# The tool's picture must be the kernel's n2j_pattern, byte for byte (hwc.py is the independent copy of the formula).
python3 "$ROOT/tools/hwc.py" pattern 640x480 "$OUT/scratch/hwc640.nv12" || fail "hwc.py pattern"
"$OUT/rwjpeg_tool_o2" pattern 640x480 "$OUT/scratch/tool640.nv12" >"$OUT/tool_pattern.log" 2>&1 \
	|| { cat "$OUT/tool_pattern.log"; fail "rwjpeg_tool pattern"; }
cat "$OUT/tool_pattern.log"
cmp "$OUT/scratch/hwc640.nv12" "$OUT/scratch/tool640.nv12" || fail "tool pattern differs from the kernel's n2j_pattern"
TOTAL=$((TOTAL + 1))

# enc: one file from the kernel's picture; djpeg must decode it, and the SOF must say 640x480.
run_logged "$OUT/tool_enc.log" "$OUT/rwjpeg_tool_o2" enc "$OUT/scratch/hwc640.nv12" 640x480 90 "$OUT/scratch/tool_enc.jpg"
"$DJPEG" -outfile /dev/null "$OUT/scratch/tool_enc.jpg" || fail "djpeg on rwjpeg_tool enc output"
TOTAL=$((TOTAL + 1))

# bench prints the two rates the phone test needs.
run_logged "$OUT/tool_bench.log" "$OUT/rwjpeg_tool_o2" bench 640x480 3
grep -q '^N2JPG bench 640x480 n=3 .*hw_ms_per_mp=' "$OUT/tool_bench.log" || fail "bench line format"
TOTAL=$((TOTAL + 1))

# info goes through RWJ_INFO.
run_logged "$OUT/tool_info.log" "$OUT/rwjpeg_tool_o2" info
grep -q '^N2JPG info abi=1 .*caps=0x1' "$OUT/tool_info.log" || fail "info line"
TOTAL=$((TOTAL + 1))

# The production build (no test hook) must say "no device" with exit code 2 on a machine without /dev/redwood-jpeg.
$NICE "$CC" "${COMMON[@]}" "${OPT[@]}" "$ROOT/jpeg/rwjpeg_tool.c" "$ROOT/jpeg/rwjpeg.c" -o "$OUT/rwjpeg_tool_prod" \
	|| fail "production rwjpeg_tool build"
set +e
N2RWJ_DEV=/nonexistent-n2hw-j02 "$OUT/rwjpeg_tool_prod" info >"$OUT/tool_nodev.log" 2>&1
nodev_status=$?
set -e
cat "$OUT/tool_nodev.log"
[ "$nodev_status" -eq 2 ] || fail "production tool: no device should exit 2, got $nodev_status"
grep -q 'no device (fallback)' "$OUT/tool_nodev.log" || fail "production tool: no-device message"
TOTAL=$((TOTAL + 1))

echo "== t_vt_hw: fake Apple runtime, ASan+UBSan =="
VT_SRC=("$HERE/t_vt_hw.c" "$ROOT/jpeg/n2jpeg_hw_vt.c" "$ROOT/jpeg/rwjpeg.c" "$HERE/fake_dev.c"
        "$VIDEO/fake/fake_apple.c" "$VIDEO/fake/fake_vt.c")
VT_INC=(-DN2_HOST_FAKE -I "$VIDEO/include" -I "$VIDEO/vt" -I "$VIDEO/fake")
[ -f "$VIDEO/fake/fake_apple.c" ] || fail "gpu/video/fake/fake_apple.c is missing (read-only dependency)"
$NICE "$CC" "${COMMON[@]}" "${VT_INC[@]}" "${SAN[@]}" "${VT_SRC[@]}" -o "$OUT/t_vt_hw_asan" || fail "t_vt_hw ASan build"
run_logged "$OUT/t_vt_hw_asan.log" "$OUT/t_vt_hw_asan"
add_n "$OUT/t_vt_hw_asan.log" '^T_VT_HW PASS ' 3

echo "== t_vt_hw: fake Apple runtime, TSan =="
$NICE "$CC" "${COMMON[@]}" "${VT_INC[@]}" -O1 -fsanitize=thread "${VT_SRC[@]}" -o "$OUT/t_vt_hw_tsan" || fail "t_vt_hw TSan build"
run_logged "$OUT/t_vt_hw_tsan.log" "$OUT/t_vt_hw_tsan"
add_n "$OUT/t_vt_hw_tsan.log" '^T_VT_HW PASS ' 3

echo "== t_vt_hw: fake Apple runtime, -O2 =="
$NICE "$CC" "${COMMON[@]}" "${VT_INC[@]}" "${OPT[@]}" "${VT_SRC[@]}" -o "$OUT/t_vt_hw_o2" || fail "t_vt_hw -O2 build"
run_logged "$OUT/t_vt_hw_o2.log" "$OUT/t_vt_hw_o2"
add_n "$OUT/t_vt_hw_o2.log" '^T_VT_HW PASS ' 3

echo "== arm64-apple-ios16.0 compile checks (gpu/video/sdk headers, -nostdinc) =="
SYSINC=$VIDEO/sdk/sysinc
RES=$("$CC" -print-resource-dir)
[ -d "$SYSINC" ] || fail "gpu/video/sdk/sysinc is missing (iOS compile check)"
for src in rwjpeg.c rwjpeg_tool.c n2jpeg_hw_vt.c; do
	if ! $NICE "$CC" -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc \
		-isystem "$RES/include" -isystem "$SYSINC" -I "$ROOT/include" -I "$ROOT/jpeg" -I "$VIDEO/include" -I "$VIDEO/vt" \
		-Wall -Wextra -Werror "$ROOT/jpeg/$src" -o "$OUT/$src.ios.o" >"$OUT/ios_$src.log" 2>&1; then
		cat "$OUT/ios_$src.log"
		fail "iOS compile of $src"
	fi
	echo "   jpeg/$src compiles for arm64-apple-ios16.0 with zero warnings"
	TOTAL=$((TOTAL + 1))
done

echo "N2TEST J02 PASS $TOTAL/$TOTAL"
