#!/bin/sh
# SPDX-License-Identifier: MIT
# T02 phone-side test runner.  Runs on the phone as root; the main session
# installs it (for example as /usr/local/bin/n2hw_phone_tests) and runs it.
# It never flashes, resets or powers anything off outside the ioctls it makes.
#
#   STAGE=preflight|jpeg|vcodec|daemon|all  (default all)
#   OUT=/var/tmp/n2hw                       result directory
#   RJ=/usr/local/bin/rwjpeg_tool           J02 tool
#   PROBE=/usr/local/bin/rwvc_probe         this package's probe (rwvc_probe.c)
#   VCODEC=/usr/local/bin/vcodecd           hw-ip daemon
#   CLI=/usr/local/bin/n2vcd_cli            hw-p03 client
#   ENABLE_DAEMON=1                         run the daemon section
#
# Every test appends one line to $OUT/results.txt:
#   PASS name | FAIL name reason | SKIP name reason
# The last line is N2HW PHONE PASS n/n or N2HW PHONE FAIL n/n.
set -u

OUT=${OUT:-/var/tmp/n2hw}
STAGE=${STAGE:-all}
RJ=${RJ:-/usr/local/bin/rwjpeg_tool}
PROBE=${PROBE:-/usr/local/bin/rwvc_probe}
VCODEC=${VCODEC:-/usr/local/bin/vcodecd}
CLI=${CLI:-/usr/local/bin/n2vcd_cli}
ENABLE_DAEMON=${ENABLE_DAEMON:-0}
VCODEC_ARGS=${VCODEC_ARGS:-}
CLI_ARGS=${CLI_ARGS:-}

mkdir -p "$OUT" || exit 1
: >"$OUT/results.txt"
PASS=0
FAIL=0
SKIP=0

pass() { PASS=$((PASS + 1)); echo "PASS $1" >>"$OUT/results.txt"; echo "PASS $1"; }
fail() { FAIL=$((FAIL + 1)); echo "FAIL $1: $2" >>"$OUT/results.txt"; echo "FAIL $1: $2"; }
skip() { SKIP=$((SKIP + 1)); echo "SKIP $1: $2" >>"$OUT/results.txt"; echo "SKIP $1: $2"; }

# run_check NAME LOG PATTERN COMMAND...
run_check() {
	name=$1
	log=$2
	pattern=$3
	shift 3
	if "$@" >"$log" 2>&1; then
		if [ -z "$pattern" ] || grep -q "$pattern" "$log"; then
			pass "$name"
		else
			fail "$name" "missing '$pattern' in $log"
		fi
	else
		rc=$?
		if grep -q "no device" "$log" 2>/dev/null; then
			skip "$name" "no device (exit $rc)"
		else
			fail "$name" "exit $rc (see $log)"
		fi
	fi
}

nodes() {
	ls -l /dev/redwood-* >"$OUT/nodes.log" 2>&1
	ok=1
	for n in /dev/redwood-jpeg /dev/redwood-vcodec /dev/redwood-vcodec-client; do
		if [ ! -c "$n" ]; then
			fail "node $n" "missing or not a character device"
			ok=0
		fi
	done
	[ "$ok" = 1 ] && pass "device nodes"
}

preflight() {
	nodes
	if [ "$(id -u)" != 0 ]; then
		skip "rwvc_probe" "not root (minor 0 is 0600)"
	else
		run_check rwvc-info "$OUT/rwvc-info.log" "N2VC info abi=1" "$PROBE" info
		run_check rwvc-client "$OUT/rwvc-client.log" "N2VC client verdict PASS" "$PROBE" client
	fi
}

jpeg() {
	if [ ! -x "$RJ" ]; then
		skip "rwjpeg_tool" "not installed at $RJ"
		return
	fi
	run_check rwjpeg-info "$OUT/rwjpeg-info.log" "N2JPG info abi=1" "$RJ" info
	run_check rwjpeg-selftest "$OUT/rwjpeg-selftest.log" "N2JPG selftest 640x480 PASS 12/12" "$RJ" selftest
	run_check rwjpeg-bench "$OUT/rwjpeg-bench.log" "N2JPG bench 1920x1080" "$RJ" bench 1920x1080 30
	if "$RJ" pattern 640x480 "$OUT/src640.nv12" >"$OUT/rwjpeg-pattern.log" 2>&1 &&
	   "$RJ" enc "$OUT/src640.nv12" 640x480 90 "$OUT/hw640.jpg" >"$OUT/rwjpeg-enc.log" 2>&1 &&
	   [ -s "$OUT/hw640.jpg" ]; then
		# SOI FF D8 (od is in the base system; xxd is not)
		soi=$(od -An -tx1 -N2 "$OUT/hw640.jpg" 2>/dev/null | tr -d ' \n')
		if [ "$soi" = "ffd8" ]; then
			pass "rwjpeg-enc 640x480"
		else
			fail "rwjpeg-enc 640x480" "file does not start with FF D8 (got '$soi')"
		fi
	else
		fail "rwjpeg-enc 640x480" "pattern/enc failed (see $OUT/rwjpeg-enc.log)"
	fi
}

vcodec() {
	if [ "$(id -u)" != 0 ]; then
		skip "rwvc_probe all" "not root (minor 0 is 0600)"
		return
	fi
	if [ ! -x "$PROBE" ]; then
		skip "rwvc_probe" "not installed at $PROBE"
		return
	fi
	run_check rwvc-info "$OUT/rwvc-info.log" "N2VC info abi=1" "$PROBE" info
	run_check rwvc-power "$OUT/rwvc-all.log" "N2VC all PASS" "$PROBE" all
	run_check rwvc-venc-read "$OUT/rwvc-read.log" "N2VC verdict PASS" "$PROBE" read
	grep -E "N2VC (read|power) VENC" "$OUT/rwvc-read.log" >"$OUT/rwvc-read.values" 2>/dev/null || true
}

daemon() {
	if [ "$ENABLE_DAEMON" != 1 ]; then
		skip "daemon encode" "ENABLE_DAEMON != 1 (hw-ip/hw-p03 tools not confirmed)"
		return
	fi
	if [ ! -x "$VCODEC" ] || [ ! -x "$CLI" ]; then
		skip "daemon encode" "missing $VCODEC or $CLI"
		return
	fi
	# shellcheck disable=SC2086
	"$VCODEC" $VCODEC_ARGS >"$OUT/vcodecd.log" 2>&1 &
	pid=$!
	sleep 2
	# shellcheck disable=SC2086
	run_check n2vcd-cli-info "$OUT/n2vcd-cli-info.log" "N2VCD" "$CLI" info $CLI_ARGS
	if [ -x "$RJ" ]; then
		"$RJ" pattern 640x480 "$OUT/src640.nv12" >"$OUT/daemon-pattern.log" 2>&1
	fi
	# shellcheck disable=SC2086
	run_check n2vcd-encode "$OUT/n2vcd-encode.log" "N2VCD" \
		"$CLI" enc "$OUT/src640.nv12" 640x480 30 2000000 "$OUT/out.h264" $CLI_ARGS
	if [ -s "$OUT/out.h264" ]; then
		pass "n2vcd output file"
	else
		fail "n2vcd output file" "$OUT/out.h264 missing or empty"
	fi
	kill "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true
}

case "$STAGE" in
	preflight) preflight ;;
	jpeg) jpeg ;;
	vcodec) vcodec ;;
	daemon) daemon ;;
	all) preflight; jpeg; vcodec; daemon ;;
	*) echo "unknown STAGE=$STAGE" >&2; exit 2 ;;
esac

total=$((PASS + FAIL))
if [ "$FAIL" -eq 0 ]; then
	echo "N2HW PHONE PASS $PASS/$total (skipped $SKIP)" | tee -a "$OUT/results.txt"
	exit 0
else
	echo "N2HW PHONE FAIL $PASS/$total (skipped $SKIP)" | tee -a "$OUT/results.txt"
	exit 1
fi
