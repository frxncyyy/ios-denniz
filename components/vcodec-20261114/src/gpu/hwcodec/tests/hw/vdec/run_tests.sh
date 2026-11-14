#!/bin/bash
# SPDX-License-Identifier: MIT
# DEC03 host test: the VLD/AVC_VLD model of tests/hw/vdec/n2vld.c.
#
# Builds only under /tmp/n2hw-DEC03.  Two configurations of the unit test and, when the extracted
# vendor partition is present, of the differential test against the phone's own libvcodecdrv.so
# under the AArch32 interpreter.  No phone, no network, no tree writes.
set -euo pipefail
ID=DEC03
HERE="$(cd "$(dirname "$0")" && pwd)"
HWD="$(cd "$HERE/../../.." && pwd)"
SB=/tmp/n2hw-DEC03
VFS=${N2HW_VENDOR_DIR:-$HWD/../build/cam-vendor-20261109/vfs}
LIB=$VFS/lib/libvcodecdrv.so
fail() { echo "N2TEST $ID FAIL $*"; exit 1; }
command -v ffmpeg >/dev/null || fail "ffmpeg not found"
command -v ffprobe >/dev/null || fail "ffprobe not found"

INC=(-I "$HWD/include" -I "$HWD/tests/hw" -I "$HERE")
CFLAGS=(-std=c11 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE)
SAN=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
OPT=(-O2)
EMU=("$HWD"/emu/n2a32_*.c)
LOADER=("$HWD"/loader/n2elf.c "$HWD"/loader/n2elf_reloc.c)
SHIMS=("$HWD"/shim/n2shim_string.c "$HWD"/shim/n2shim_heap.c "$HWD"/shim/n2shim_thread.c \
       "$HWD"/shim/n2shim_stdio.c "$HWD"/shim/n2shim_android.c "$HWD"/shim/n2shim_props.c \
       "$HWD"/shim/n2shim_format.c "$HWD"/shim/n2vfs.c)
FAKE=("$HWD"/tests/hw/n2vc_hw_fake.c)

rm -rf "$SB"
mkdir -p "$SB/vec" "$SB/bin" "$SB/log"

# ---- three H.264 streams: 320x240, 1920x1080 (coded 1088, cropped), 1920x808 (coded 816) ----
ENC=(-c:v libx264 -preset ultrafast -profile:v main
     -x264-params keyint=25:min-keyint=25:bframes=0:ref=1:scenecut=0 -pix_fmt yuv420p -f h264)
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=320x240:rate=25 -frames:v 2 "${ENC[@]}" "$SB/vec/v320.h264" -y || fail "ffmpeg 320x240"
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=25 -frames:v 1 "${ENC[@]}" "$SB/vec/v1080.h264" -y || fail "ffmpeg 1920x1080"
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x808:rate=25 -frames:v 1 "${ENC[@]}" "$SB/vec/vcrop.h264" -y || fail "ffmpeg 1920x808"
: > "$SB/expect.txt"
for v in v320 v1080 vcrop; do
	wh=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$SB/vec/$v.h264")
	[ -n "$wh" ] || fail "ffprobe $v"
	echo "$SB/vec/$v.h264 ${wh/,/ }" >> "$SB/expect.txt"
done

total=0
passed=0

# parse "N2VLD checks 768 pass 768" / "N2VLDV checks 298 pass 298"
account() {
	local log=$1 line n p
	line=$(tail -n 1 "$log")
	case "$line" in
	N2VLD\ checks\ *\ pass\ *|N2VLDV\ checks\ *\ pass\ *) ;;
	*) fail "unexpected last line in $log: $line" ;;
	esac
	n=$(echo "$line" | awk '{print $3}')
	p=$(echo "$line" | awk '{print $5}')
	[ "$n" = "$p" ] || fail "$log: $p of $n checks"
	total=$((total + n))
	passed=$((passed + p))
}

build() {
	local name=$1 cfg=$2; shift 2
	echo "== build $name ($cfg)"
	nice -n 10 clang "${CFLAGS[@]}" "$@" -o "$SB/bin/$name-$cfg" "${INC[@]}" || fail "$name ($cfg) build"
}

run() {
	local name=$1 cfg=$2; shift 2
	local log="$SB/log/$name-$cfg.log"
	echo "== run $name ($cfg)"
	set +e
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$SB/bin/$name-$cfg" "$@" >"$log" 2>&1
	local rc=$?
	set -e
	cat "$log"
	[ "$rc" -eq 0 ] || fail "$name ($cfg) exit $rc"
	account "$log"
}

for cfg in san o2; do
	if [ "$cfg" = san ]; then EXTRA=("${SAN[@]}"); else EXTRA=("${OPT[@]}"); fi
	build t_vld "$cfg" "${EXTRA[@]}" "$HERE/t_vld.c" "$HERE/n2vld.c" "${FAKE[@]}" -pthread
	run t_vld "$cfg"
	if [ -f "$LIB" ]; then
		build t_vendor "$cfg" "${EXTRA[@]}" "$HERE/t_vendor.c" "$HERE/n2vld.c" \
			"${EMU[@]}" "${LOADER[@]}" "${SHIMS[@]}" -pthread -lm
		run t_vendor "$cfg" "$LIB" "$SB/expect.txt"
	fi
done
if [ ! -f "$LIB" ]; then
	echo "DEC03 vendor differential: SKIP (no $LIB)"
fi

# ---- arm64 iOS compile check (compile only) ----
IOS_SDK=${N2_IOS_SDK:-$HWD/../build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
if [ -d "$IOS_SDK/usr/include" ]; then
	nice -n 10 clang -target arm64-apple-ios16.5 -isysroot "$IOS_SDK" -std=c11 -O2 \
		-Wall -Wextra -Werror "${INC[@]}" -c "$HERE/n2vld.c" -o "$SB/bin/n2vld-ios.o" \
		|| fail "arm64 iOS compile"
	echo "DEC03 arm64 iOS compile: n2vld.c ok"
	total=$((total + 1))
	passed=$((passed + 1))
else
	echo "DEC03 arm64 iOS compile: SKIP (no SDK at $IOS_SDK)"
fi

[ "$passed" -eq "$total" ] || fail "$passed of $total checks"
echo "N2TEST $ID PASS $passed/$total"
