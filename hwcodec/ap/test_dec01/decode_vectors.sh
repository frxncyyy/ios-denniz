#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/ap/test_dec01/decode_vectors.sh -- the DEC01 runner hook: decode real H.264/H.265
# access units with the phone's own codec stack (vpud + libvcodecdrv under the A32 interpreter on
# T02's fake) through ap/n2ap_vdec.c, and print how far each vector gets.
#
# The vectors are generated here with ffmpeg (testsrc, 320x240 and 1920x1080, yuv420p, libx264 main
# with and without B-frames, libx265) so nothing binary is stored in the tree.  A vector stops at the
# first unmet hardware need:
#   VD-a QUERY_CAP answered           VD-d the first VDEC register access (LOCK)
#   VD-b INIT + SET_PARAM answered    VD-e the first WAITISR
#   VD-c DEC_START accepted
# H.264 goes through a frame decode and reaches VD-e.  H.265 stops after the header START because
# T02's fake does not model the HEVC fetch-ok poll (LAT_VLD +0xe8, DEC03's n2vld model is not wired
# into the fake yet), which is named in the output; the vendor client side is the same code.
#
# This script is called by run_tests.sh (set N2DEC01_VECTORS=0 to skip it) and can be run alone.
# It builds into /tmp/n2hw-DEC01 and writes nothing into the tree.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
top=$(cd "$root/../.." && pwd)
work=/tmp/n2hw-DEC01
cc=${CC:-clang}
vendor=${N2HW_VENDOR_DIR:-$top/gpu/build/cam-vendor-20261109/vfs}
vec=$work/vec
probe=$work/n2vdec_probe

if ! command -v ffmpeg >/dev/null 2>&1; then
	echo "DEC01 vectors SKIP (ffmpeg not installed)"
	exit 0
fi
if [ ! -f "$vendor/lib/libvpud_vcodec.so" ]; then
	echo "DEC01 vectors SKIP (no extracted /vendor at $vendor)"
	exit 0
fi

mkdir -p "$vec"
if ! make -C "$root/host" o2 OUT="$work" >"$work/vectors_host_build.log" 2>&1; then
	echo "DEC01 vectors FAIL (gpu/hwcodec/host did not build; see $work/vectors_host_build.log)"
	exit 1
fi
if ! "$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	-I"$root/include" -I"$root/ap" -I"$root" -I"$root/host" -I"$root/vcu" -I"$here" -pthread \
	"$root/ap/n2ap_vdec.c" "$here/probe_dec01.c" -o "$probe" \
	-Wl,--whole-archive "$work/o2/libn2vhost.a" -Wl,--no-whole-archive -lpthread -lm \
	>"$work/vectors_probe_build.err" 2>&1; then
	cat "$work/vectors_probe_build.err" >&2
	echo "DEC01 vectors FAIL (probe build)"
	exit 1
fi

# name | size | frames | codec | extra ffmpeg args | probe args
vectors=(
	"t320bf0|320x240|12|libx264|-profile:v main -bf 0 -g 12 -crf 30|"
	"t320bf2|320x240|12|libx264|-profile:v main -bf 2 -g 12 -crf 30|"
	"t1080bf0|1920x1080|4|libx264|-profile:v main -bf 0 -g 4 -crf 32|"
	"t1080bf2|1920x1080|4|libx264|-profile:v main -bf 2 -g 4 -crf 32|"
	"t320x265|320x240|6|libx265|-x265-params log-level=error:bframes=2:keyint=6|--no-frame"
	"t1080x265|1920x1080|4|libx265|-x265-params log-level=error:bframes=2:keyint=4|--no-frame"
)

pass=0
total=0
failed=0
stopped=0
for v in "${vectors[@]}"; do
	IFS='|' read -r name size frames enc extra pargs <<<"$v"
	total=$((total + 1))
	ext=h264
	mux=h264
	case $enc in libx265) ext=h265; mux=hevc ;; esac
	file=$vec/$name.$ext
	if ! ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=$size:rate=30" \
		-frames:v "$frames" -pix_fmt yuv420p -c:v "$enc" $extra -f $mux "$file" -y \
		>"$work/vec_$name.ffmpeg.log" 2>&1; then
		echo "N2VDEC vector $name: ffmpeg failed (see $work/vec_$name.ffmpeg.log)"
		failed=$((failed + 1))
		continue
	fi
	w=${size%x*}
	h=${size#*x}
	set +e
	N2HW_VENDOR_DIR="$vendor" N2HW_RUN_DIR="$work/run" \
		timeout 60 "$probe" --codec "$ext" --size "${w}x${h}" --bitstream "$file" \
		--seconds 1 --fake-irq $pargs >"$work/vec_$name.log" 2>&1
	prc=$?
	set -e
	milestone=$(grep -E "^N2VDEC (reached|first-unmet) VD-" "$work/vec_$name.log" | tail -1 || true)
	frames=$(grep -E "^N2VDEC frames:" "$work/vec_$name.log" | tail -1 || true)
	detail=""
	case $milestone in
	*"VD-c"*)
		detail="; first unmet: the fake's HEVC VLD fetch-ok model (LAT_VLD +0xe8, DEC03 not wired into T02's fake); vendor frame path not reached"
		;;
	esac
	echo "N2VDEC vector $name ($size, $(wc -c <"$file") B, exit $prc): ${milestone:-no milestone}$detail"
	[ -n "$frames" ] && echo "  $frames"
	grep -E '^N2VDEC vsi after header' "$work/vec_$name.log" | sed 's/^/  /' || true
	if grep -q 'get_ringbuf input handle NULL' "$work/vec_$name.log"; then
		echo "N2VDEC vector $name: vendor logged the get_ringbuf input handle NULL error"
		failed=$((failed + 1))
		continue
	fi
	if [ -n "$pargs" ]; then
		# header-only by design: the named first unmet need is the fake's HEVC VLD model
		case $milestone in
		*"reached VD-c"*) stopped=$((stopped + 1)) ;;
		*) failed=$((failed + 1)) ;;
		esac
	else
		case $milestone in
		*"reached VD-d"*|*"reached VD-e"*) pass=$((pass + 1)) ;;
		*) failed=$((failed + 1)) ;;
		esac
	fi
done

echo "N2VDEC vectors: $pass/$total reached VD-d or VD-e, $stopped stopped at VD-c (named need), $failed failed"
[ "$failed" = 0 ]
