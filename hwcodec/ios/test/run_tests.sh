#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_tests.sh -- host test of gpu/hwcodec/ios (opus-ios).  No arguments; everything goes to /tmp/n2hw-opus-ios/test.
#   1. n2vcd_selftest on the PC, linked against IV's host libn2vhost.a with --whole-archive (see README: the plain archive
#      link loses the thread/stdio shim tables), on the phone's extracted vendor set: T1..T5 must PASS.
#   2. n2vcd_probe end to end against the REAL vcodecd source in its test build (IP's: T02 fake hardware, fake_vhost canned
#      H.264 from ffmpeg, SOCK_SEQPACKET + file-backed DMA handles): the probe's output file must be exactly the canned access
#      units, decode with ffmpeg, and its console lines must have the per-frame, hex and DONE ok=1 lines.
#   3. the probe's refusals: gate absent -> exit 0 and nothing else; no server -> bounded wait, DONE ok=0, exit 1.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)            # gpu/hwcodec/ios/test
IOS=$(cd "$HERE/.." && pwd)                    # gpu/hwcodec/ios
W=$(cd "$IOS/.." && pwd)                       # gpu/hwcodec
ROOT=$(cd "$W/../.." && pwd)
VENDOR=${N2HW_VENDOR_DIR:-$ROOT/gpu/build/cam-vendor-20261109/vfs}
OUT=/tmp/n2hw-opus-ios/test
rm -rf "$OUT"; mkdir -p "$OUT"
PASS=0; TOTAL=0
check() { TOTAL=$((TOTAL + 1)); if eval "$2"; then PASS=$((PASS + 1)); echo "ok   $1"; else echo "FAIL $1"; fi; }
CC=${CC:-clang}
FL=(-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)

echo "== 1. selftest on the host =="
nice -n 10 make -C "$W/host" lib OUT="$OUT/iv" JOBS=2 > "$OUT/iv.log" 2>&1 || { tail -5 "$OUT/iv.log"; echo "N2TEST opus-ios FAIL IV lib build"; exit 1; }
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -I "$W/include" -I "$W/host" -I "$W/tests/hw" -o "$OUT/selftest" "$IOS/n2vcd_selftest.c" \
	-Wl,--whole-archive "$OUT/iv/o2/libn2vhost.a" -Wl,--no-whole-archive -pthread -lm
mkdir -p "$OUT/gate"; : > "$OUT/gate/st"
"$OUT/selftest" --gate "$OUT/gate/st" --no-console --out "$OUT/st" --vendor "$VENDOR" --iters 500000 > "$OUT/selftest.log" 2>&1 || true
grep -E "PASS|FAIL|SKIP|DONE" "$OUT/selftest.log" | sed 's/^/   /'
for t in "T1 vm" "T2 tls" "T3 interp" "T4 fault" "T5 vendor"; do check "selftest $t" "grep -q '$t PASS' '$OUT/selftest.log'"; done
check "selftest gate consumed" "[ ! -e '$OUT/gate/st' ]"
check "selftest summary file" "ls '$OUT'/st/selftest-*.txt >/dev/null 2>&1"
"$OUT/selftest" --gate "$OUT/gate/st" --no-console --out "$OUT/st2" > "$OUT/selftest-nogate.log" 2>&1; rc=$?
check "selftest without gate: exit 0, one line, no summary" "[ $rc = 0 ] && [ \$(wc -l < '$OUT/selftest-nogate.log') = 1 ] && [ ! -d '$OUT/st2' ]"

echo "== 2. probe against vcodecd (IP's test build) =="
command -v ffmpeg >/dev/null || { echo "N2TEST opus-ios FAIL ffmpeg missing"; exit 1; }
D=$OUT/e2e; mkdir -p "$D/run" "$D/dma" "$D/out" "$D/fake"
ffmpeg -v error -y -f lavfi -i "testsrc2=size=64x48:rate=30" -frames:v 12 -c:v libx264 -profile:v baseline -g 6 -keyint_min 6 \
	-sc_threshold 0 -pix_fmt yuv420p -f h264 "$D/stream.h264"
python3 "$W/daemon/test/split_au.py" "$D/stream.h264" "$D/stream.au"
"$CC" "${FL[@]}" -I "$W/include" -I "$W/daemon" -I "$W/host" -I "$W/ap" -I "$W/tests/hw" -DN2IP_TEST_HOOKS -o "$D/vcodecd" \
	"$W/daemon/vcodecd.c" "$W/daemon/n2vcd_srv.c" "$W/tests/hw/n2vc_hw_fake.c" "$W/daemon/test/fake_vhost.c" -pthread
"$CC" "${FL[@]}" -I "$W/include" -o "$D/probe" "$IOS/n2vcd_probe.c"
export N2IP_FAKE_DIR=$D/fake N2IP_FAKE_STREAM=$D/stream.au N2IP_FAKE_INPUT_SEEN=$D/fake/seen N2IP_FAKE_PARAMS=$D/fake/params
"$D/vcodecd" --fake --socket "$D/run/s" --vendor "$D/fake" --dma-dir "$D/dma" --no-console --hw-timeout-ms 2000 > "$D/vcodecd.log" 2>&1 &
VPID=$!
trap 'kill $VPID 2>/dev/null || true' EXIT
: > "$D/gate"
set +e
"$D/probe" --gate "$D/gate" --socket "$D/run/s" --dma-dir "$D/dma" --out "$D/out" --size 64x48 --frames 12 --no-console --wait-server 20 \
	> "$D/probe.log" 2>&1
prc=$?
set -e
sed 's/^/   /' "$D/probe.log" | head -30
F=$(ls "$D"/out/probe-*.h264 2>/dev/null | head -1)
check "probe exit 0 and DONE ok=1" "[ $prc = 0 ] && grep -q 'DONE ok=1 frames=12/12' '$D/probe.log'"
check "probe gate removed before the run" "[ ! -e '$D/gate' ] && grep -q 'gate .* removed' '$D/probe.log'"
check "probe HELLO_OK and OPEN_OK lines" "grep -q HELLO_OK '$D/probe.log' && grep -q 'OPEN_OK session=' '$D/probe.log'"
check "probe frame 0 is key with SPS/PPS (nal=7,8,..5)" "grep -Eq 'frame 0 bytes=[0-9]+ key=1 .*nal=7,8.*5' '$D/probe.log'"
check "probe hex lines start with an Annex B start code" "grep -Eq 'hex 0000 0000000167|hex 00000000000167|hex 0000 00000001' '$D/probe.log'"
check "probe file == the canned stream byte for byte" "[ -n '$F' ] && cmp -s '$F' '$D/stream.h264'"
check "probe file decodes (ffmpeg, 12 frames)" "[ -n '$F' ] && [ \$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 '$F') = 12 ]"

echo "== 3. refusals =="
set +e
"$D/probe" --gate "$D/nogate" --socket "$D/run/s" --dma-dir "$D/dma" --no-console > "$D/nogate.log" 2>&1; rc1=$?
kill $VPID 2>/dev/null; wait $VPID 2>/dev/null
"$D/probe" --socket "$D/run/none" --dma-dir "$D/dma" --out "$D/out2" --no-console --wait-server 1 > "$D/noserver.log" 2>&1; rc2=$?
"$D/probe" --relay "$D/no-such-node" --out "$D/out3" --no-console --wait-server 1 > "$D/norelay.log" 2>&1; rc3=$?
set -e
check "no gate: exit 0, one line" "[ $rc1 = 0 ] && [ \$(wc -l < '$D/nogate.log') = 1 ]"
check "no server (socket): bounded, DONE ok=0, exit 1" "[ $rc2 = 1 ] && grep -q 'DONE ok=0' '$D/noserver.log'"
check "no relay node: bounded, DONE ok=0, exit 1, no output file" "[ $rc3 = 1 ] && grep -q 'not serving' '$D/norelay.log' && [ ! -e '$D/out3' ] || [ -z \"\$(ls '$D/out3' 2>/dev/null)\" ]"

if [ "$PASS" = "$TOTAL" ]; then echo "N2TEST opus-ios PASS $PASS/$TOTAL"; else echo "N2TEST opus-ios FAIL $PASS/$TOTAL"; exit 1; fi
