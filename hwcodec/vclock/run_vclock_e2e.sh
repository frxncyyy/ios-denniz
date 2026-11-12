#!/bin/bash
# vcodec-20261114: host end-to-end of the automatic VENC clock: test-build vcodecd (--fake --fake-venc, the phone's vendor
# objects under the interpreter) + host n2vcd_probe over the socket transport; kern.n2vc.venc_fast is redirected to a file
# (N2VCLK_TEST_FILE), the override file to N2VCLK_FILE_PATH.  usage: run_vclock_e2e.sh VCODECD_TESTBUILD HWCODEC_TREE RUNDIR(short path)
set -uo pipefail
VC=$1; Wt=$2; RUN=$3; rm -rf "$RUN"; mkdir -p "$RUN/out" "$RUN/s.buf"; cd "$RUN"   # relative socket path: sun_path is 108 bytes
VENDOR=$(cd "$Wt/../build/cam-vendor-20261109/vfs" && pwd)
clang -std=c11 -O2 -Wall -Wextra -Werror -I "$Wt/include" -o "$RUN/probe" "$Wt/ios/n2vcd_probe.c" || exit 1
export N2A32_MEM_GRANULE=16384 N2VC_FAKE_HANDLE_BASE=0x10000 N2VHOST_AP_TIMEOUT_MS=20000
echo 0 > "$RUN/sysctl"; : > "$RUN/vclk-writes"
N2VCLK_TEST_FILE=$RUN/sysctl N2VCLK_FILE_PATH=$RUN/venc-clock "$VC" --fake --fake-venc --socket s --dma-dir s.buf \
	--vendor "$VENDOR" --no-console --hw-timeout-ms 20000 > "$RUN/vcodecd.log" 2>&1 &
VP=$!; trap 'kill $VP 2>/dev/null' EXIT
P() { "$RUN/probe" --socket s --dma-dir s.buf --delay 0 --wait-server 60 --open-timeout 120 --frame-timeout 120 --out "$RUN/out" --no-console "$@" >> "$RUN/probe.log" 2>&1; }
fail=0
chk() { # $1 label, $2 expected sysctl after open (read from the log line), $3 expected value after close
	local line; line=$(grep 'venc clock:' "$RUN/vcodecd.log" | grep -v 'session closed' | tail -1)
	local now; now=$(cat "$RUN/sysctl")
	echo "  $1: $line | sysctl after close: $now"
	case "$line" in *"-> $2 ("*) ;; *) echo "  FAIL $1: wanted $2"; fail=1 ;; esac
	[ "$now" = "$3" ] || { echo "  FAIL $1: after close $now, wanted $3"; fail=1; }
}
P --codec avc1 --size 640x480 --frames 2 --fps 30;   chk "vga30 auto" 0 0
P --codec avc1 --size 1280x720 --frames 2 --fps 60;  chk "720p60 auto" 1 0
grep -q 'session closed, kern.n2vc.venc_fast 1 -> 0' "$RUN/vcodecd.log" || { echo "  FAIL no restore line"; fail=1; }
P --codec avc1 --size 1920x1080 --frames 2 --fps 30; chk "1080p30 auto" 0 0
echo slow > "$RUN/venc-clock"
P --codec avc1 --size 1280x720 --frames 2 --fps 60;  chk "720p60 file slow" 0 0
echo fast > "$RUN/venc-clock"
P --codec avc1 --size 640x480 --frames 2 --fps 30;   chk "vga30 file fast" 1 0
echo keep > "$RUN/venc-clock"; echo 1 > "$RUN/sysctl"
P --codec avc1 --size 640x480 --frames 2 --fps 30
grep 'venc clock:' "$RUN/vcodecd.log" | tail -1 | grep -q 'mode keep (file)' && [ "$(cat $RUN/sysctl)" = 1 ] && echo "  keep: sysctl untouched (1)" || { echo "  FAIL keep"; fail=1; }
echo garbage > "$RUN/venc-clock"; echo 0 > "$RUN/sysctl"
P --codec hvc1 --size 1280x720 --frames 2 --fps 60;  chk "hevc 720p60 invalid file -> auto" 1 0
grep 'venc clock:' "$RUN/vcodecd.log" | grep -q 'file invalid, ignored' || { echo "  FAIL invalid-file note"; fail=1; }
# The fake VENC model encodes only 640x480 H.264 (fakevenc/STATUS.md "Not done / limits"): the 720p/1080p/HEVC sessions time out
# in the model (hardware timeout 20 s) after their clock decision, which is what they check (and the restore after a failed session).
n=$(grep -c 'DONE ok=1' "$RUN/probe.log"); echo "  probe sessions encoded: $n/7 (expected 3: the three 640x480 H.264 sessions)"; [ "$n" = 3 ] || fail=1
[ $fail = 0 ] && echo "N2TEST VCLOCK-E2E PASS" || echo "N2TEST VCLOCK-E2E FAIL"
