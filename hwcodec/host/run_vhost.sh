#!/bin/bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/host/run_vhost.sh -- build and run the track V host runner.
#
# Loads bin/vpud + the four libraries of the phone's own /vendor into the AArch32 interpreter, links them
# against the shim tables, runs the init arrays, starts vpud's guest main and drives one H.264 encode from
# the AP side; prints the milestone reached and the first unmet need.  Host only: the hardware is T02's fake
# n2vc_hw, the vendor files are read at run time from the extracted /vendor, nothing is written to the tree
# and nothing is published.
#
# Usage:
#   bash gpu/hwcodec/host/run_vhost.sh [--san] [--size WxH] [--seconds N] [--entry start|apps_init|service-venc]
#
# Environment: N2HW_VENDOR_DIR (the extracted /vendor), N2HW_RUN_DIR (default /tmp/n2hw-iv/run),
#              N2HW_TRACE (default $N2HW_RUN_DIR/trace.txt), N2VC_FAKE_SCRIPT (fake hardware script).
# Last line: "N2VHOST reached M-x: ..." (x as far as the tree allows), exit 0 when at least M-a is reached.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${N2HW_OUT:-/tmp/n2hw-iv}
run=$out/run
cfg=o2
size=320x240
seconds=5
entry=

while [ $# -gt 0 ]; do
	case $1 in
	--san) cfg=san ;;
	--size) shift; size=${1:?--size needs WxH} ;;
	--seconds) shift; seconds=${1:?--seconds needs a number} ;;
	--entry) shift; entry=${1:?--entry needs start|apps_init|service-venc} ;;
	--help|-h)
		sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
		exit 0 ;;
	*)
		echo "run_vhost.sh: unknown argument $1" >&2
		exit 2 ;;
	esac
	shift
done

vendor=${N2HW_VENDOR_DIR:-$root/gpu/build/cam-vendor-20261109/vfs}
mkdir -p "$run" "$out/$cfg"
log=$run/run-$cfg.log

fail_last() {
	echo "$1" | tee -a "$log"
	exit 1
}

if [ ! -d "$vendor" ]; then
	fail_last "N2VHOST reached M-none: vendor directory not found: $vendor"
fi
for f in bin/vpud lib/libvpud_vcodec.so lib/libvcodecdrv.so lib/libvcodec_utility.so lib/libvcodec_oal.so; do
	if [ ! -f "$vendor/$f" ]; then
		fail_last "N2VHOST reached M-none: $vendor/$f is missing"
	fi
done

echo "run_vhost.sh: vendor=$vendor out=$out cfg=$cfg" | tee "$log"
echo "run_vhost.sh: build (nice -n 10, -j2)" | tee -a "$log"

if ! make -C "$here" -j2 "$cfg" OUT="$out" >>"$log" 2>&1; then
	echo "---- build log (tail) ----" >&2
	tail -40 "$log" >&2
	# Any package file that does not compile is a finding for its owner; the runner is not built.
	fail_last "N2VHOST reached M-none: build failed, see $log"
fi

# Build log lines that matter (a package object that was missing) go to the console.
grep -E '^N2VHOST build:' "$log" || true

args=(--vendor "$vendor" --size "$size" --seconds "$seconds" --record "$run/hw-record.txt")
if [ -n "${N2VC_FAKE_SCRIPT:-}" ]; then
	args+=(--script "$N2VC_FAKE_SCRIPT")
fi
if [ -n "$entry" ]; then
	export N2VHOST_ENTRY=$entry
fi
export N2HW_RUN_DIR=$run
export N2HW_TRACE=${N2HW_TRACE:-$run/trace.txt}
: >"$N2HW_TRACE"
# Never validate a bitstream left by an earlier run: only a file this run writes may be checked below.
rm -f "$run"/bitstream-*.h264

set +e
"$out/$cfg/n2vhost" "${args[@]}" >>"$log" 2>&1
rc=$?
set -e

# The runner's report is in the log; print it and make its last line the script's last line.
sed -n '/^N2VHOST /p' "$log" | tail -60 >"$run/report-$cfg.txt"
cat "$run/report-$cfg.txt"

# If the AP drive got far enough to write a bitstream, check it with T02's H.264 checker (ffprobe/ffmpeg on
# the PC, no phone).  Reported on stderr so the milestone line above stays this script's last stdout line.
if ls "$run"/bitstream-*.h264 >/dev/null 2>&1; then
	bss=$(ls -t "$run"/bitstream-*.h264 | head -1)
	frame=$run/frame-$size.nv12
	if python3 "$root/gpu/hwcodec/tests/check/h264_check.py" "$bss" "${size%x*}" "${size#*x}" 1 \
		--source "$frame" --quiet >"$run/h264-check.log" 2>&1; then
		echo "run_vhost.sh: $bss validated (H.264 SPS/geometry/decode/PSNR, $(wc -c <"$bss") bytes)" >&2
	else
		rc2=$?
		if [ "$rc2" = 2 ]; then
			echo "run_vhost.sh: $bss not validated (checker/tool error, see $run/h264-check.log)" >&2
		else
			echo "run_vhost.sh: $bss FAILED the H.264 check (see $run/h264-check.log)" >&2
		fi
	fi
fi

last=$(tail -1 "$run/report-$cfg.txt")
if [ "$rc" -gt 1 ]; then
	echo "N2VHOST reached M-none: runner exited $rc (see $log)"
	exit 1
fi
case $last in
"N2VHOST reached M-none"*)
	echo "$last"
	exit 1 ;;
"N2VHOST reached M-"*)
	exit 0 ;;
*)
	echo "N2VHOST reached M-none: no milestone line in $log"
	exit 1 ;;
esac
exit 0
