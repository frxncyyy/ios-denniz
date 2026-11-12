#!/bin/bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/host/test_iv/run_tests.sh -- package IV acceptance test.
#
# Builds the track V/T source set + the runner + libn2vhost.a in the sanitizer and -O2 configurations,
# then checks mechanically, host only (T02's fake n2vc_hw, the extracted /vendor read at run time):
#   1. the build produces both runners and both static libraries, and the library has no main;
#   2. M-a: five objects loaded and linked, and the unresolved list is exactly D01's 149-import
#      oracle minus the two data imports, __aeabi_unwind_cpp_pr0 and every registered shim name
#      (host/test_iv/check_imports.py);
#   3. M-b: the init arrays ran, the guest main started and the report ends with "N2VHOST reached M-x";
#   4. the same result in the sanitizer build and in the -O2 build (ASan+UBSan clean, no leak report);
#   5. the AP frame/buffer path ran on the fake hardware (320x240 NV12, two DMA buffers, no violation);
#   6. the diagnostic entry (apps_init) also runs and reports a named first unmet need;
#   7. t_api: the library API, the M-e/M-f hardware wrapper, n2vhost_note/report, the maps text and the
#      error paths.
#
# Last line: "N2TEST IV PASS n/n" or "N2TEST IV FAIL <what>", exit 0/1. Everything is built under
# /tmp/n2hw-iv; nothing is written to the work tree.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
hwd=$(cd "$here/../.." && pwd)
root=$(cd "$hwd/../.." && pwd)
out=/tmp/n2hw-iv/test
vendor=${N2HW_VENDOR_DIR:-$root/gpu/build/cam-vendor-20261109/vfs}
CC=${CC:-clang}
flags="-std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -I$hwd/host -I$hwd/include -I$hwd/tests/hw"

rm -rf "$out"
mkdir -p "$out/report"

pass=0
total=0
fail() {
	trap - ERR
	echo "N2TEST IV FAIL $1"
	exit 1
}
ck() {  # ck <what> <shell condition>
	total=$((total + 1))
	if eval "$2"; then
		pass=$((pass + 1))
	else
		echo "IV FAIL: $1"
	fi
}
sanitizer_clean() {  # sanitizer_clean <file>
	if [ ! -s "$1" ]; then
		return 0
	fi
	! grep -Eq 'ERROR: (AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer)' "$1"
}

[ -d "$vendor" ] || fail "vendor directory missing: $vendor"
[ -f "$vendor/bin/vpud" ] || fail "vendor set incomplete: $vendor/bin/vpud"

# ------------------------------------------------------------------ 1. build both configurations + libs
echo "IV build: sanitizer and -O2 concurrently, one compile job each (2 total, nice -n 10)"
make -C "$hwd/host" -j1 o2 OUT=/tmp/n2hw-iv JOBS=1 >>"$out/report/build.log" 2>&1 &
b1=$!
make -C "$hwd/host" -j1 san OUT=/tmp/n2hw-iv JOBS=1 >>"$out/report/build.log" 2>&1 &
b2=$!
rc1=0; rc2=0
wait $b1 || rc1=$?
wait $b2 || rc2=$?
if [ $rc1 -ne 0 ] || [ $rc2 -ne 0 ]; then
	tail -20 "$out/report/build.log" >&2
	fail "build (o2 rc=$rc1 san rc=$rc2, see $out/report/build.log)"
fi
if ! make -C "$hwd/host" -j1 lib OUT=/tmp/n2hw-iv JOBS=1 >>"$out/report/build.log" 2>&1; then
	tail -20 "$out/report/build.log" >&2
	fail "library build (see $out/report/build.log)"
fi
ck "sanitizer runner built" "[ -x /tmp/n2hw-iv/san/n2vhost ]"
ck "-O2 runner built" "[ -x /tmp/n2hw-iv/o2/n2vhost ]"
ck "sanitizer library built" "[ -f /tmp/n2hw-iv/san/libn2vhost.a ]"
ck "-O2 library built" "[ -f /tmp/n2hw-iv/o2/libn2vhost.a ]"
ck "library exports n2vhost_start" "llvm-nm -g /tmp/n2hw-iv/o2/libn2vhost.a 2>/dev/null | grep -q ' T n2vhost_start$'"
ck "library has no main (the daemon owns main)" "! llvm-nm -g /tmp/n2hw-iv/o2/libn2vhost.a 2>/dev/null | grep -q ' T main$'"
ck "library exports n2vhost_vcu and n2vhost_stop" \
	"llvm-nm -g /tmp/n2hw-iv/o2/libn2vhost.a 2>/dev/null | grep -q ' T n2vhost_vcu$' && llvm-nm -g /tmp/n2hw-iv/o2/libn2vhost.a | grep -q ' T n2vhost_stop$'"
# R2 F15: the library must not ship empty shim tables.  n2shim_thread/stdio/android/
# n2shim_data_all are strong undefined references of n2vhost.o, so a plain archive link pulls the
# members and a product that forgot them fails at link time instead of silently resolving nothing.
ck "library references the real shim tables (R2 F15)" \
	"llvm-nm -u /tmp/n2hw-iv/o2/libn2vhost.a 2>/dev/null | grep -q ' n2shim_thread$' && \
	 llvm-nm -u /tmp/n2hw-iv/o2/libn2vhost.a | grep -q ' n2shim_stdio$' && \
	 llvm-nm -u /tmp/n2hw-iv/o2/libn2vhost.a | grep -q ' n2shim_android$' && \
	 llvm-nm -u /tmp/n2hw-iv/o2/libn2vhost.a | grep -q ' n2shim_data_all$'"
ck "library has no weak empty table definitions" \
	"! llvm-nm /tmp/n2hw-iv/o2/libn2vhost.a 2>/dev/null | grep -q ' W n2shim_thread$'"
ck "library excludes the test-only fake VENC model" \
	"! llvm-nm /tmp/n2hw-iv/o2/libn2vhost.a 2>/dev/null | grep -q ' T n2venc_fake_wrap$'"
ck "the iOS archive excludes the fake VENC too" \
	"make -C '$hwd/host' ios-lib OUT=/tmp/n2hw-iv-testios >'$out/report/ioslib.log' 2>&1 && \
	 ! llvm-nm /tmp/n2hw-iv-testios/ios/libn2vhost.a 2>/dev/null | grep -q ' T n2venc_fake_wrap$'"

# ------------------------------------------------------------------------ 2/3. the runner, both configs
run_cfg() {  # run_cfg <cfg> <tag> [args...]
	local cfg=$1 tag=$2
	shift 2
	mkdir -p "$out/run-$tag"
	set +e
	N2HW_RUN_DIR="$out/run-$tag" N2HW_TRACE="$out/run-$tag/trace.txt" \
		"/tmp/n2hw-iv/$cfg/n2vhost" --vendor "$vendor" --seconds 2 "$@" \
		>"$out/report/$tag.log" 2>"$out/report/$tag.err"
	echo $? >"$out/report/$tag.rc"
	set -e
}

for cfg in o2 san; do
	tag=$cfg
	run_cfg "$cfg" "$tag" --dump-shims
	log=$out/report/$tag.log
	ck "$cfg: runner exit status 0" "[ \"\$(cat '$out/report/$tag.rc')\" = 0 ]"
	ck "$cfg: M-a line with five objects" "grep -q '^N2VHOST M-a loaded 5 objects, linked,' '$log'"
	ck "$cfg: M-b line (init arrays ran)" "grep -q '^N2VHOST M-b init arrays ran over 5 objects' '$log'"
	ck "$cfg: real shim tables reported (V32/V33/V34 counts)" \
		"grep -q 'thread=66(V32) stdio=27(V33) android=35(V34) data=2(V34)' '$log'"
	ck "$cfg: trace is attached only because N2HW_TRACE is set" \
		"grep -q '^N2VHOST trace on (N2HW_TRACE)' '$log'"
	ck "$cfg: no milestone is claimed without M-a" "! grep -q '^N2VHOST reached none' '$log'"
	ck "$cfg: report ends at M-f (the whole AP encode reached the VENC)" "tail -1 '$log' | grep -q '^N2VHOST reached M-f:'"
	ck "$cfg: unresolved list matches D01's oracle" \
		"python3 '$here/check_imports.py' '$log' --vendor '$vendor' >'$out/report/$tag-imports.txt' && grep -q ' ok$' '$out/report/$tag-imports.txt'"
	ck "$cfg: ASan/UBSan/LeakSanitizer silent" "sanitizer_clean '$out/report/$tag.err'"
	ck "$cfg: AP frame prepared (320x240 NV12)" "grep -q '^N2VHOST frame 320x240 NV12 115200 bytes' '$log'"
	ck "$cfg: the frame has a guest-mapped DMA mirror (V41)" "grep -q '^N2VHOST frame 320x240 NV12 115200 bytes .* guest-mapped' '$log'"
	ck "$cfg: bitstream buffer allocated" "grep -q '^N2VHOST bitstream buffer 1 MiB handle=' '$log'"
	ck "$cfg: the AP buffers are registered as ion shares (V34/V41 fd path)" \
		"grep -q '^N2VHOST ion shares frame(fd=0x500)=0 bs(fd=0x501)=0' '$log'"
	ck "$cfg: the /dev/vcu counters are reported (M-c evidence)" "grep -q '^N2VHOST vcu set_object=[0-9]* get_object=' '$log'"
	ck "$cfg: the guest /proc/self/maps text came from the loader bases" \
		"grep -qE '^N2VHOST maps ([0-9]+ bytes of segment lines from the loader bases|n2vfs_set_maps not in the build)' '$log'"
	ck "$cfg: M-d INIT_DONE accepted (V40 commit VMA fix)" \
		"grep -q '^N2VHOST M-d AP_IPIMSG_ENC_INIT (0xc000) answered with VCU_IPIMSG_ENC_INIT_DONE (0xd000): n2ap_venc_open returned 0' '$log'"
	ck "$cfg: M-e first VENC register access" "grep -q '^N2VHOST M-e first VENC window access: write VENC 0x00a8 ' '$log'"
	ck "$cfg: M-f wait for the VENC done interrupt" "grep -q '^N2VHOST M-f wait_irq(RWVC_IRQ_VENC' '$log'"
	ck "$cfg: the encode carried the ion fds (V41 encode_fd)" "grep -q '^N2VHOST encode frame fds=1' '$log'"
	ck "$cfg: the fake VENC produced an IDR frame" \
		"grep -qE '^N2VHOST encode frame fds=1 bytes=[1-9][0-9]* key=1' '$log'"
	ck "$cfg: fake-venc frames=1 headers=1" \
		"grep -qE '^N2VHOST fake-venc .*frames=1 .*headers=1' '$log'"
	ck "$cfg: the EOS flush was sent" "grep -q '^N2VHOST encode final rc=0 bytes=0' '$log'"
	ck "$cfg: VENC traffic is not zero" "grep -qE '^N2VHOST hw .*venc_ops=[1-9][0-9]* .*venc_waits=[1-9]' '$log'"
	ck "$cfg: no hardware violation" "grep -q '^N2VHOST fake violations=0' '$log'"
done

ck "o2 and san reach the same milestone" \
	"[ \"\$(tail -1 '$out/report/o2.log' | cut -d: -f1)\" = \"\$(tail -1 '$out/report/san.log' | cut -d: -f1)\" ]"
ck "the fake hardware record shows two DMA buffers" \
	"[ \$(grep -c 'DMA alloc' '$out/run-o2/hw-record.txt') -ge 2 ]"
ck "the NV12 frame file has the expected size" \
	"[ \$(stat -c%s '$out/run-o2/frame-320x240.nv12') -eq 115200 ]"
ck "the trace file recorded the guest's host calls" \
	"grep -q 'h call ' '$out/run-o2/trace.txt'"
ck "the o2 bitstream is the expected 115841 bytes" \
	"[ \"\$(stat -c%s '$out/run-o2/bitstream-320x240.h264' 2>/dev/null)\" = 115841 ]"
ck "the o2 bitstream is a valid H.264 IDR (T02 checker)" \
	"python3 '$root/gpu/hwcodec/tests/check/h264_check.py' '$out/run-o2/bitstream-320x240.h264' 320 240 1 \
	 --source '$out/run-o2/frame-320x240.nv12' --quiet >'$out/report/h264-check.txt' 2>&1"

# ------------------------------------------------------------------- 3b. product mode: no trace, fast loop
# The daemon (vcodecd -> n2vhost_start) does not set N2HW_TRACE.  Attaching T01's trace anyway makes
# the interpreter format a record per instruction and takes the slow loop (opus-perf finding).
mkdir -p "$out/run-notrace"
set +e
env -u N2HW_TRACE N2HW_RUN_DIR="$out/run-notrace" /tmp/n2hw-iv/o2/n2vhost --vendor "$vendor" \
	--seconds 1 --record "$out/run-notrace/hw-record.txt" \
	>"$out/report/notrace.log" 2>"$out/report/notrace.err"
echo $? >"$out/report/notrace.rc"
set -e
ck "no-trace: runner exit status 0" "[ \"\$(cat '$out/report/notrace.rc')\" = 0 ]"
ck "no-trace: trace is off (fast interpreter path)" \
	"grep -q '^N2VHOST trace off (fast interpreter path' '$out/report/notrace.log'"
ck "no-trace: no trace file was created" "[ ! -e '$out/run-notrace/trace.txt' ]"
ck "no-trace: the same AP session still reaches M-f" \
	"tail -1 '$out/report/notrace.log' | grep -q '^N2VHOST reached M-f:'"

# ------------------------------------------------------------------------------- 4. the diagnostic entry
run_cfg o2 o2-apps_init --dump-shims --entry apps_init
dlog=$out/report/o2-apps_init.log
ck "--entry apps_init reports a milestone" "tail -1 '$dlog' | grep -q '^N2VHOST reached M-'"
ck "--entry apps_init reaches M-b too" "grep -q '^N2VHOST M-b init arrays ran' '$dlog'"
ck "--entry apps_init names its first unmet need or keeps running" \
	"grep -qE '^N2VHOST (first-unmet guest stopped|first-unmet none: the guest is still running)' '$dlog'"

# --------------------------------------------------------------------------------- 5. the library API test
for cfg in o2 san; do
	extra=""
	[ "$cfg" = san ] && extra="-fsanitize=address,undefined -fno-sanitize-recover=all"
	if ! nice -n 10 "$CC" $flags $extra "$here/t_api.c" "/tmp/n2hw-iv/$cfg/libn2vhost.a" \
		-pthread -lm -o "$out/t_api-$cfg" >"$out/report/t_api-$cfg.build" 2>&1; then
		cat "$out/report/t_api-$cfg.build" >&2
		fail "t_api link ($cfg)"
	fi
	ck "t_api $cfg: library API and M-e/M-f wrapper" \
		"'$out/t_api-$cfg' '$vendor' '$out/run-$cfg/t_api-record.txt' >'$out/report/t_api-$cfg.log' 2>&1"
	ck "t_api $cfg: no sanitizer report" "sanitizer_clean '$out/report/t_api-$cfg.log'"
done
ck "t_api says PASS" "grep -q '^IV-API PASS' '$out/report/t_api-o2.log'"

# ------------------------------------------------------------------- 6. threaded code under TSan
# Guest threads (V32) + the AP drive share V40/V41 state; every lock is package-owned.  The known V02
# non-atomic stop_request read (V32 F8) is V02's; on this tree the run is clean.
echo "IV tsan: build and one short run"
if ! make -C "$hwd/host" -j1 tsan OUT=/tmp/n2hw-iv JOBS=1 >>"$out/report/build.log" 2>&1; then
	tail -20 "$out/report/build.log" >&2
	fail "tsan build (see $out/report/build.log)"
fi
# The one suppressed pair is the guest-shared VENC VSI ring (tsan.supp explains why it cannot be
# locked in the model); anything else TSan reports is a real finding.
export TSAN_OPTIONS="suppressions=$here/tsan.supp"
run_cfg tsan tsan
unset TSAN_OPTIONS
ck "tsan: runner reached M-f" "tail -1 '$out/report/tsan.log' | grep -q '^N2VHOST reached M-f:'"
ck "tsan: no ThreadSanitizer report outside the documented shared-ring pair" \
	"! grep -q 'WARNING: ThreadSanitizer' '$out/report/tsan.err'"
ck "tsan: the VENC traffic is there too" "grep -qE '^N2VHOST hw .*venc_ops=[1-9]' '$out/report/tsan.log'"

# --------------------------------------------------------------------------------------------- conclusion
if [ "$pass" -ne "$total" ]; then
	echo "N2TEST IV FAIL $pass/$total"
	exit 1
fi
echo "N2TEST IV PASS $pass/$total"
exit 0
