#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/daemon/dec/run_tests.sh -- DEC05: decoder service + client library.
#
# Host only (nothing touches the phone, nothing is published, no download):
#   1. stage include/n2vcd_proto.h rev 2 in /tmp until hw-lead lands it;
#   2. unit suite (service + client + fake AP + DEC03's n2vld) in ASan+UBSan -O1, -O2 and TSan;
#   3. end-to-end suite over SOCK_SEQPACKET with a real dec_testd process, in the same 3 configs;
#   4. ffmpeg/ffprobe stream oracle: the generated streams are what the geometry literals assume;
#   5. arm64-apple-ios16.0 compile of the product sources.
#
# Last line on success: N2TEST DEC05 PASS <passed>/<total>
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)          # gpu/hwcodec
top=$(cd "$root/../.." && pwd)           # work tree root
work=/tmp/n2hw-DEC05
cc=${CC:-clang}

mkdir -p "$work"
rm -rf "$work/inc" "$work/obj" "$work/e2e" "$work/run" 2>/dev/null || true
mkdir -p "$work/inc" "$work/obj" "$work/e2e" "$work/run"

fail() {
	echo "N2TEST DEC05 FAIL $*"
	exit 1
}

# ---- 1. protocol rev 2 staging ---------------------------------------------------------------
if grep -q 'N2VCD_DEC_ADD_BUFFER' "$root/include/n2vcd_proto.h"; then
	echo "DEC05 proto using the tree header (rev 2 already landed)"
	stage_check=1
else
	python3 "$here/test/stage_proto.py" "$root/include/n2vcd_proto.h" \
		"$here/n2vcd_proto_dec_rev2.h.txt" "$work/inc/n2vcd_proto.h" >"$work/run/stage.log" \
		|| fail "stage_proto"
	cat "$work/run/stage.log"
	grep -q 'N2VCD_DEC_ADD_BUFFER = 20' "$work/inc/n2vcd_proto.h" || fail "staged header has no decoder ids"
	grep -q '#define N2VCD_VERSION 2u' "$work/inc/n2vcd_proto.h" || fail "staged header did not bump the version"
	stage_check=1
fi

inc=(-I"$work/inc" -I"$root/include" -I"$root/daemon/dec" -I"$root/daemon/dec/test"
     -I"$root/daemon" -I"$root/client/dec" -I"$root/tests/hw/vdec")
base=(-std=c11 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L "${inc[@]}" -pthread)
libs=("$root/daemon/dec/n2vcd_dec_service.c" "$root/client/dec/n2vcd_dec.c"
      "$root/daemon/dec/test/fake_ap.c" "$root/tests/hw/vdec/n2vld.c")

total=0
add_checks() { total=$((total + $1)); }

# ---- 2. unit suite ---------------------------------------------------------------------------
unit_first=""
for cfg in asan o2 tsan; do
	case $cfg in
	asan) flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all) ;;
	o2)   flags=(-O2) ;;
	tsan) flags=(-O1 -fsanitize=thread) ;;
	esac
	bin="$work/obj/t_dec_unit_$cfg"
	if ! "$cc" "${base[@]}" "${flags[@]}" "${libs[@]}" "$here/test/t_dec_unit.c" -o "$bin" \
	     2>"$work/run/unit_$cfg.err"; then
		cat "$work/run/unit_$cfg.err" >&2
		fail "unit compile $cfg"
	fi
	if ! "$bin" >"$work/run/unit_$cfg.log" 2>&1; then
		cat "$work/run/unit_$cfg.log" >&2
		fail "unit run $cfg"
	fi
	line=$(grep -E '^DEC05 unit (PASS|FAIL) [0-9]+/[0-9]+$' "$work/run/unit_$cfg.log" | tail -1)
	[ -n "$line" ] || { cat "$work/run/unit_$cfg.log" >&2; fail "unit $cfg: no result line"; }
	case $line in *FAIL*) cat "$work/run/unit_$cfg.log" >&2; fail "unit $cfg: $line" ;; esac
	n=$(printf '%s\n' "$line" | sed -E 's#.*PASS ([0-9]+)/[0-9]+$#\1#')
	[ -n "$unit_first" ] || unit_first=$n
	[ "$unit_first" = "$n" ] || fail "unit configs differ: $cfg has $n, first had $unit_first"
	add_checks "$n"
	echo "DEC05 unit $cfg $line"
done

# ---- 3. end-to-end suite ---------------------------------------------------------------------
oracle=$(command -v ffprobe || true)
ffmpeg=$(command -v ffmpeg || true)
[ -n "$oracle" ] || fail "ffprobe not installed"
[ -n "$ffmpeg" ] || fail "ffmpeg not installed"

s320="$work/e2e/stream-320x240.h264"
s1080="$work/e2e/stream-1920x1080.h264"
"$ffmpeg" -hide_banner -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 \
	-frames:v 6 -c:v libx264 -profile:v main -pix_fmt yuv420p -bf 0 -g 30 -f h264 "$s320" \
	|| fail "ffmpeg 320x240"
"$ffmpeg" -hide_banner -loglevel error -y -f lavfi -i testsrc=size=1920x1080:rate=30 \
	-frames:v 1 -c:v libx264 -profile:v main -pix_fmt yuv420p -bf 0 -g 30 -f h264 "$s1080" \
	|| fail "ffmpeg 1920x1080"
probe320=$("$oracle" -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$s320" | head -1)
frames320=$("$oracle" -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "$s320" | head -1)
probe1080=$("$oracle" -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$s1080" 2>/dev/null | head -1 || true)
case "$probe320/$frames320" in 320,240/6) echo "DEC05 oracle 320x240x6 PASS ($probe320 frames $frames320)" ;; *) fail "ffprobe 320 stream: $probe320 frames $frames320" ;; esac
add_checks 1
case "$probe1080" in 1920,1080) echo "DEC05 oracle 1920x1080 PASS ($probe1080)" ;; *) fail "ffprobe 1080 stream: $probe1080" ;; esac
add_checks 1

daemon_src=("$root/daemon/dec/n2vcd_dec_service.c" "$root/daemon/dec/test/fake_ap.c"
            "$root/tests/hw/vdec/n2vld.c" "$root/daemon/dec/test/dec_testd.c" "$root/daemon/n2vcd_srv.c")
e2e_first=""
for cfg in asan o2 tsan; do
	case $cfg in
	asan) flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all) ;;
	o2)   flags=(-O2) ;;
	tsan) flags=(-O1 -fsanitize=thread) ;;
	esac
	dd="$work/obj/dec_testd_$cfg"
	et="$work/obj/t_dec_e2e_$cfg"
	"$cc" "${base[@]}" "${flags[@]}" "${daemon_src[@]}" -o "$dd" 2>"$work/run/testd_$cfg.err" \
		|| { cat "$work/run/testd_$cfg.err" >&2; fail "dec_testd compile $cfg"; }
	"$cc" "${base[@]}" "${flags[@]}" "$root/daemon/dec/n2vcd_dec_service.c" \
		"$root/client/dec/n2vcd_dec.c" "$root/daemon/dec/test/fake_ap.c" \
		"$root/tests/hw/vdec/n2vld.c" "$here/test/t_dec_e2e.c" -o "$et" \
		2>"$work/run/e2e_$cfg.err" || { cat "$work/run/e2e_$cfg.err" >&2; fail "e2e compile $cfg"; }
	dir="$work/e2e/$cfg"
	mkdir -p "$dir"
	if ! "$et" "$dd" "$s320" "$s1080" "$dir" >"$work/run/e2e_$cfg.log" 2>&1; then
		cat "$work/run/e2e_$cfg.log" >&2
		fail "e2e run $cfg"
	fi
	line=$(grep -E '^DEC05 e2e (PASS|FAIL) [0-9]+/[0-9]+$' "$work/run/e2e_$cfg.log" | tail -1)
	[ -n "$line" ] || { cat "$work/run/e2e_$cfg.log" >&2; fail "e2e $cfg: no result line"; }
	case $line in *FAIL*) cat "$work/run/e2e_$cfg.log" >&2; fail "e2e $cfg: $line" ;; esac
	n=$(printf '%s\n' "$line" | sed -E 's#.*PASS ([0-9]+)/[0-9]+$#\1#')
	[ -n "$e2e_first" ] || e2e_first=$n
	[ "$e2e_first" = "$n" ] || fail "e2e configs differ: $cfg has $n, first had $e2e_first"
	add_checks "$n"
	echo "DEC05 e2e $cfg $line"
done

# ---- 4. arm64-apple-ios16.0 compile ----------------------------------------------------------
sdk="$top/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk"
[ -d "$sdk" ] || fail "iPhoneOS SDK not found at $sdk"
ios_src=(daemon/dec/n2vcd_dec_service.c client/dec/n2vcd_dec.c client/dec/n2vcd_dec_path.c
         daemon/dec/n2vcd_dec_ap_n2ap.c)
ios_ok=0
for src in "${ios_src[@]}"; do
	obj="$work/obj/$(basename "$src" .c).ios.o"
	"$cc" -c --target=arm64-apple-ios16.0 -isysroot "$sdk" -std=c11 -O2 -fno-common \
		-Wall -Wextra -Werror -I"$work/inc" -I"$root/include" -I"$root/daemon/dec" \
		-I"$root/client/dec" -I"$root/ap" -I"$root/vcu" -I"$root" -o "$obj" "$root/$src" \
		|| fail "arm64-apple-ios16.0 compile of $src"
	ios_ok=$((ios_ok + 1))
done
echo "DEC05 ios compile PASS ($ios_ok objects)"
add_checks "$ios_ok"
add_checks "$stage_check"

echo "N2TEST DEC05 PASS $total/$total"
