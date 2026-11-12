#!/bin/bash
# SPDX-License-Identifier: MIT
# Host suite of the JPEG phone test (opus-jpeg): n2jpeg_phone against a fake /dev/redwood-jpeg (t_fake.c: K01's validation
# rules + J02's cjpeg-backed encoder + an ffmpeg-backed decoder with J04's proposed RWJ_DECODE semantics), and phone_check.py
# (the PC verdict) on its outputs.  Build products in /tmp/n2hw-opus-jpeg/test (deleted first).  Last line: N2TEST JPGP PASS n/n.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); PH=$(cd "$HERE/.." && pwd); HW=$(cd "$PH/../.." && pwd)
O=/tmp/n2hw-opus-jpeg/test; rm -rf "$O"; mkdir -p "$O/s"
NICE="nice -n 10"; pass=0; total=0
ok() { total=$((total+1)); if [ "$1" = 0 ]; then pass=$((pass+1)); echo "  ok   $2"; else echo "  FAIL $2"; fi; }
has() { grep -q -- "$2" "$1"; }
CF=(-std=c11 -Wall -Wextra -Werror -I "$HW/include" -I "$HW/jpeg" -I "$HW/jpeg/test_j02")
SAN=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all)
SRC=("$PH/n2jpeg_phone.c" "$HW/jpeg/rwjpeg.c" "$HW/jdec/n2jdec_parse.c" "$HW/jdec/n2jdec_words.c")
export N2J02_SCRATCH=$O/s

echo "== builds"
for v in asan o2; do
	if [ $v = asan ]; then F=("${SAN[@]}"); else F=(-O2); fi
	$NICE clang "${CF[@]}" "${F[@]}" -c -Dn2rwj_test_open=j02_open_unused "$HW/jpeg/test_j02/fake_dev.c" -o "$O/fake_$v.o" &&
	$NICE clang "${CF[@]}" "${F[@]}" -DN2JPGP_TEST -DN2RWJ_TOOL_TEST "${SRC[@]}" "$HERE/t_fake.c" "$O/fake_$v.o" -o "$O/phone_$v"
	ok $? "test build $v"
done
$NICE clang "${CF[@]}" -O2 "${SRC[@]}" -o "$O/phone_prod"; ok $? "production host build"
I=$O/inputs; bash "$PH/gen_dec_inputs.sh" "$I" > "$O/gen.log" 2>&1; ok $? "decode inputs generated"

echo "== A: full default run, kernel without decoder (ENOTSUP), ASan"
mkdir -p "$O/A/out"; touch "$O/A/gate"
"$O/phone_asan" run --gate "$O/A/gate" --out "$O/A/out" --log "$O/A/jpeg.log" --bench 2 --idle-ms 3200 --dec "$I/dec640.jpg" > "$O/A/stdout" 2>&1
ok $? "A exit 0"
L=$O/A/jpeg.log
[ ! -e "$O/A/gate" ]; ok $? "A gate removed (one-shot)"
has "$L" "N2JPGP DONE pass=22 fail=0 skip=2"; ok $? "A DONE pass=22 fail=0 skip=2 ($(grep -o 'DONE.*' "$L"))"
has "$L" "N2JPG selftest 640x480 PASS 12/12"; ok $? "A library selftest 12/12"
for s in 640x480 1920x1080 4032x3024; do for q in 95 90 60; do
	[ -s "$O/A/out/jpeg-$s-q$q.jpg" ] && djpeg -outfile /dev/null "$O/A/out/jpeg-$s-q$q.jpg"; ok $? "A jpeg-$s-q$q.jpg written + djpeg"
done; done
has "$L" "PASS limit 4096x3072 q=90"; ok $? "A 12 MP passed -> ABI maximum tried"
has "$L" "N2JPG bench 4032x3024 n=3"; ok $? "A 12 MP bench"
has "$L" "PASS negatives started no job"; ok $? "A negatives counted no job"
has "$L" "PASS idle off: RWJ_INFO.powered 0"; ok $? "A idle off"
[ "$(grep -c 'answers ENOTSUP' "$L")" = 2 ]; ok $? "A both decodes SKIP on ENOTSUP"
has "$L" "words(K01 claim PAs"; ok $? "A hybrid words logged"
[ "$(grep -c '^N2JPGP ' "$O/A/stdout")" = 0 ]; ok $? "A log is the dup2'd stdout (nothing left on launchd stdout)"

echo "== B: block limit at 6 MP (fake), ladder brackets it"
mkdir -p "$O/B/out"
N2JPGP_FAKE_MAXPIX=6000000 "$O/phone_o2" run --out "$O/B/out" --log "$O/B/jpeg.log" --sizes 4032x3024 --q 90 --bench 2 --idle-ms 0 --no-dec-self > /dev/null 2>&1
[ $? = 1 ]; ok $? "B exit 1 (failure reported)"
L=$O/B/jpeg.log
has "$L" "enc 4032x3024 q=90 code=0x7 rc=-1 errno=5 .* out_status=1 (TIMEOUT)"; ok $? "B errno + out_status logged"
has "$L" "info after failure"; ok $? "B INFO after failure"
has "$L" "FAIL limit 3840x2160" && has "$L" "FAIL limit 3264x2448" && has "$L" "PASS limit 2592x1944"; ok $? "B ladder 8.3 MP fail, 8.0 MP fail, 5.0 MP pass"
! has "$L" "bench 4032x3024"; ok $? "B no 12 MP bench after a 12 MP failure"

echo "== C: decoder present (fake: J04 semantics, ffmpeg pixels) + phone_check decode oracle (djpeg/libjpeg)"
mkdir -p "$O/C/out"
N2JPGP_FAKE_DEC=ffmpeg "$O/phone_asan" run --out "$O/C/out" --log "$O/C/jpeg.log" --sizes 640x480 --q 90 --bench 0 --idle-ms 0 \
	--dec "$I/dec640.jpg" --dec "$I/dec4032.jpg" --dec "$I/dec1920r.jpg" > /dev/null 2>&1
ok $? "C exit 0"
has "$O/C/jpeg.log" "DONE pass=13 fail=0 skip=0"; ok $? "C DONE pass=13 fail=0 skip=0"
for f in jdec-dec640-640x480.420p.yuv jdec-dec4032-4032x3024.420p.yuv jdec-dec1920r-1920x1080.422p.yuv jdec-jpeg-640x480-q90-640x480.420p.yuv; do
	[ -s "$O/C/out/$f" ]; ok $? "C $f"
done
# the fake encoder goes through RGB (J02 design), which clips the test picture: ~35 dB vs libjpeg's ~43; slack 12 here only
$NICE python3 "$PH/phone_check.py" "$O/C" --inputs "$I" --slack 12 > "$O/C/check.log" 2>&1; ok $? "C phone_check PASS"
[ "$(grep -c '^DEC PASS' "$O/C/check.log")" = 4 ]; ok $? "C 4 decodes match libjpeg (Y >= 45 dB, RGB >= 35 dB)"

echo "== D: phone_check must FAIL on bad results"
mkdir -p "$O/D/out"; cp "$O/C/jpeg.log" "$O/D/"
head -c 20000 "$O/C/out/jpeg-640x480-q90.jpg" > "$O/D/out/jpeg-640x480-q90.jpg"
$NICE python3 "$PH/phone_check.py" "$O/D" --slack 12 > "$O/D/check.log" 2>&1; [ $? = 1 ]; ok $? "D truncated JPEG -> FAIL"
mkdir -p "$O/D2/out"; cp "$O/C/jpeg.log" "$O/D2/"; cp "$O/C/out/jpeg-640x480-q90.jpg" "$O/D2/out/"
$NICE python3 "$PH/phone_check.py" "$O/D2" > "$O/D2/check.log" 2>&1; [ $? = 1 ]; ok $? "D2 RGB-clipped encoder vs libjpeg reference at default slack -> FAIL"
mkdir -p "$O/D3/out"; cp "$O/C/jpeg.log" "$O/D3/"; cp "$I/dec640.jpg" "$O/D3/out/"
python3 -c "import sys;d=bytearray(open(sys.argv[1],'rb').read());d[1000:60000]=bytes(59000);open(sys.argv[2],'wb').write(d)" \
	"$O/C/out/jdec-dec640-640x480.420p.yuv" "$O/D3/out/jdec-dec640-640x480.420p.yuv"
$NICE python3 "$PH/phone_check.py" "$O/D3" > "$O/D3/check.log" 2>&1; [ $? = 1 ] && has "$O/D3/check.log" "DEC FAIL dec640"; ok $? "D3 corrupted decode -> FAIL"
sed 's/fail=0/fail=1/' "$O/C/jpeg.log" > "$O/D2/jpeg.log"
$NICE python3 "$PH/phone_check.py" "$O/D2" --slack 12 > "$O/D2/check2.log" 2>&1; [ $? = 1 ]; ok $? "D4 DONE fail=1 -> FAIL"

echo "== E: gate and device absent"
"$O/phone_prod" run --gate "$O/nonexistent-gate" > "$O/E1.log" 2>&1; [ $? = 0 ] && has "$O/E1.log" "absent: not running"; ok $? "E gate absent -> exit 0, nothing done"
mkdir -p "$O/E/out"
N2RWJ_DEV=/nonexistent-n2jpgp "$O/phone_prod" run --out "$O/E/out" --log "$O/E/jpeg.log" > /dev/null 2>&1; [ $? = 2 ] && has "$O/E/jpeg.log" "NODEV"; ok $? "E no device -> exit 2 NODEV"
"$O/phone_prod" pattern 640x480 "$O/E/p.nv12" > /dev/null && python3 "$HW/tools/hwc.py" pattern 640x480 "$O/E/hwc.nv12" > /dev/null 2>&1 && cmp -s "$O/E/p.nv12" "$O/E/hwc.nv12"
ok $? "E pattern == hwc.py pattern (the kernel's n2j_pattern)"

echo "== G: kernel decoder RWJD (0315 fake): decode-only, E2 works"
mkdir -p "$O/G/out"
N2JPGP_FAKE_JD=e2 "$O/phone_asan" run --decode-only --out "$O/G/out" --log "$O/G/jpeg.log" --dec "$I/dec640.jpg" --dec "$I/dec1920r.jpg" --dec "$I/dec4032.jpg" > /dev/null 2>&1
ok $? "G exit 0"
L=$O/G/jpeg.log
has "$L" "decode only: S2-S6 (encode) skipped" && ! has "$L" "N2JPGP enc "; ok $? "G no encoder job"
has "$L" "PASS hwdec E0/E1 probe"; ok $? "G E0/E1 probe"
has "$L" "hwdec search on dec640.jpg: E2"; ok $? "G search finds E2 on the small input"
has "$L" "PASS hwdec dec1920r.jpg E2" && has "$L" "PASS hwdec dec4032.jpg E2"; ok $? "G medium + 12 MP decoded with E2"
has "$L" "DONE pass=5 fail=0 skip=7"; ok $? "G DONE pass=5 fail=0 skip=7 = G0, F5 and the five F misses ($(grep -o 'DONE.*' "$L"))"
has "$L" "hwdec after F0 (post-reset probe): rc=0" && has "$L" "hwdec window 300-3fc: rc=0"; ok $? "G post-step probe + window dump logged"
cp "$L" "$O/G/jpeg-selftest.log"
$NICE python3 "$PH/phone_check.py" "$O/G" --inputs "$I" > "$O/G/check.log" 2>&1; ok $? "G phone_check PASS ($(tail -1 "$O/G/check.log"))"
[ "$(grep -c '^DEC PASS' "$O/G/check.log")" = 3 ]; ok $? "G 3 hardware-step decodes match libjpeg"

echo "== H: only E3 works (E2 must be a clean miss: its list passes the kernel's checks)"
mkdir -p "$O/H/out"
N2JPGP_FAKE_JD=e3 "$O/phone_o2" run --decode-only --out "$O/H/out" --log "$O/H/jpeg.log" --dec "$I/dec640.jpg" --dec "$I/dec4032.jpg" > /dev/null 2>&1
ok $? "H exit 0"
L=$O/H/jpeg.log
has "$L" "dec640.jpg E2: rc=-1 errno=5 .*TIMEOUT" && has "$L" "SKIP hwdec dec640.jpg E2 (search miss"; ok $? "H E2 = EIO/TIMEOUT miss (not EPERM/EFAULT)"
has "$L" "hwdec search on dec640.jpg: E3" && has "$L" "PASS hwdec dec4032.jpg E3"; ok $? "H E3 found and used for 12 MP"

echo "== I: E4b works: every E4 list before it passes the kernel's checks"
mkdir -p "$O/I/out"
N2JPGP_FAKE_JD=e4b "$O/phone_o2" run --decode-only --out "$O/I/out" --log "$O/I/jpeg.log" --dec "$I/dec640.jpg" --dec "$I/dec1920r.jpg" > /dev/null 2>&1
ok $? "I exit 0"
L=$O/I/jpeg.log
[ "$(grep -c 'errno=5 .*TIMEOUT' "$L")" = 12 ]; ok $? "I G0,F5,F0-F4,E2,E3,E4a1-3 = 12 clean misses"
! grep -Eq 'errno=(1|14|22) ' "$L"; ok $? "I no EPERM/EFAULT/EINVAL from the register checks"
has "$L" "PASS hwdec dec1920r.jpg E4b"; ok $? "I E4b used for the 4:2:2 medium input"

echo "== J: nothing works -> one FAIL, the others SKIP, every E4 step tried"
mkdir -p "$O/J/out"
N2JPGP_FAKE_JD=fail "$O/phone_o2" run --decode-only --out "$O/J/out" --log "$O/J/jpeg.log" --dec "$I/dec640.jpg" --dec "$I/dec4032.jpg" > /dev/null 2>&1
[ $? = 1 ]; ok $? "J exit 1"
L=$O/J/jpeg.log
has "$L" "FAIL hwdec search: no step" && has "$L" "SKIP hwdec dec4032.jpg (no working step"; ok $? "J search FAIL + 12 MP SKIP"
has "$L" "hwdec E4d skipped: uv_brz_w 0" && has "$L" "dec640.jpg E4i:"; ok $? "J E4a..E4i tried (E4d skipped for 4:2:0)"

echo "== K: full run (encode + decode, E3) decodes the phone's own JPEG last"
mkdir -p "$O/K/out"
N2JPGP_FAKE_JD=e3 "$O/phone_o2" run --out "$O/K/out" --log "$O/K/jpeg.log" --sizes 640x480 --q 90 --bench 0 --idle-ms 0 --no-ladder --dec "$I/dec640.jpg" > /dev/null 2>&1
ok $? "K exit 0"
has "$O/K/jpeg.log" "PASS hwdec jpeg-640x480-q90.jpg E3"; ok $? "K self JPEG decoded with E3"

echo "== L: jp6 F1 (2015 set + DU_NUM + 0x344 = file bytes) works: F0 a clean miss, F1 used for every input"
mkdir -p "$O/L/out"
N2JPGP_FAKE_JD=f1 "$O/phone_asan" run --decode-only --out "$O/L/out" --log "$O/L/jpeg.log" --dec "$I/dec640.jpg" --dec "$I/dec1920r.jpg" --dec "$I/dec4032.jpg" > /dev/null 2>&1
ok $? "L exit 0"
L=$O/L/jpeg.log
has "$L" "SKIP hwdec dec640.jpg F0 (search miss" && has "$L" "hwdec search on dec640.jpg: F1"; ok $? "L F0 miss, search finds F1"
grep -q 'dec640.jpg F1 regs.* 0fc=500 .*344=[0-9a-f]* 274=37 240=0$' "$L" && ! grep -q 'F1 regs.* 330=' "$L"; ok $? "L F1 list: DU_NUM 0x500, 0x344 before the trigger, no 0x330"
has "$L" "PASS hwdec dec1920r.jpg F1" && has "$L" "PASS hwdec dec4032.jpg F1"; ok $? "L medium + 12 MP decoded with F1"
! grep -Eq 'errno=(1|14|22) ' "$L"; ok $? "L no EPERM/EFAULT/EINVAL from the register checks"
cp "$L" "$O/L/jpeg-selftest.log"
$NICE python3 "$PH/phone_check.py" "$O/L" --inputs "$I" > "$O/L/check.log" 2>&1; ok $? "L phone_check PASS ($(tail -1 "$O/L/check.log"))"

echo "== M: jp8 model (reset keeps 0x160..0x170; decode needs this picture's values): G0 miss, F5 for every input, stale-value diagnostics miss"
mkdir -p "$O/M/out"
N2JPGP_FAKE_JD=f5 "$O/phone_asan" run --decode-only --out "$O/M/out" --log "$O/M/jpeg.log" --dec "$I/dec640.jpg" --dec "$I/dec1920r.jpg" --dec "$I/dec4032.jpg" --dec "$I/dec640n.jpg" --dec "$I/dec1920f.jpg" > /dev/null 2>&1
ok $? "M exit 0"
L=$O/M/jpeg.log
has "$L" "SKIP hwdec dec640.jpg G0 (search miss" && has "$L" "hwdec search on dec640.jpg: F5"; ok $? "M G0 miss in the power-on state, search finds F5"
grep -q 'dec640.jpg F5 regs(35):.* 344=75f3 .* 0f8=0 160=280 164=140 16c=70407 170=4b0 274=37 240=0$' "$L"; ok $? "M F5 list = F3 + 0x0f8/0x160/0x164/0x16c/0x170, trigger last"
grep -q 'dec4032.jpg F5 regs(35):.* 160=fc0 164=7e0 16c=[0-9a-f]* 170=ba0c 274=37 240=0$' "$L"; ok $? "M 12 MP F5 list carries its own strides and PAUSE_MCU_NUM = total_mcu"
has "$L" "PASS hwdec dec1920r.jpg F5" && has "$L" "PASS hwdec dec4032.jpg F5"; ok $? "M medium + 12 MP decoded with F5"
has "$L" "SKIP hwdec dec4032.jpg F5p (diagnostic: no decode)" && has "$L" "SKIP hwdec dec1920r.jpg F5s (diagnostic: no decode)"; ok $? "M stale 640x480 values reproduce the jp6 failure as diagnostics (SKIP, not FAIL)"
grep -q 'dec4032.jpg F5p regs(35):.* 170=4b0 274=37 240=0$' "$L"; ok $? "M F5p = F5 with the first input's PAUSE_MCU_NUM"
[ -z "$(ls "$O/M/out" | grep -E 'F5[psw]')" ]; ok $? "M diagnostics write no plane file"
! grep -Eq 'errno=(1|14|22) ' "$L"; ok $? "M no EPERM/EFAULT/EINVAL from the register checks"
has "$L" "DONE pass=7 fail=0 skip=10"; ok $? "M DONE pass=7 fail=0 skip=10 = info, probe, 5x F5; G0 + 3 diagnostics on dec1920r/dec4032/dec1920f ($(grep -o 'DONE.*' "$L"))"
has "$L" "PASS hwdec dec640n.jpg F5" && has "$L" "PASS hwdec dec1920f.jpg F5"; ok $? "M hypothesis-2 inputs (640x480 > 64 KiB, 1920x1088 < 64 KiB) run with F5"
[ "$(grep -c 'hwdec dec640n.jpg F5[psw] skipped: same' "$L")" = 3 ] && ! grep -q 'dec640n.jpg F5[psw] regs' "$L"; ok $? "M dec640n (= dec640 geometry): the three stale-value diagnostics are skipped, not run"
! grep -q 'F5i\|F5n\|F6' "$L"; ok $? "M F5 decoded everything: no F6/F5n/F5i fallback job"
cp "$L" "$O/M/jpeg-selftest.log"
$NICE python3 "$PH/phone_check.py" "$O/M" --inputs "$I" > "$O/M/check.log" 2>&1; ok $? "M phone_check PASS ($(tail -1 "$O/M/check.log"))"
[ "$(grep -c '^DEC PASS' "$O/M/check.log")" = 5 ]; ok $? "M 5 F5 decodes match libjpeg"

echo "== N: jp8 hypothesis 3 model (larger pictures also need index tables 0x5000 apart): F5, F6, F5n miss, F5i decodes"
mkdir -p "$O/N/out"
N2JPGP_FAKE_JD=f5i "$O/phone_asan" run --decode-only --out "$O/N/out" --log "$O/N/jpeg.log" --dec "$I/dec640.jpg" --dec "$I/dec4032.jpg" > /dev/null 2>&1
[ $? = 1 ]; ok $? "N exit 1 (the 12 MP F5 result is a FAIL even though F5i decodes)"
L=$O/N/jpeg.log
has "$L" "DONE pass=4 fail=1 skip=3"; ok $? "N DONE pass=4 fail=1 skip=3 ($(grep -o 'DONE.*' "$L"))"
has "$L" "hwdec search on dec640.jpg: F5" && has "$L" "FAIL hwdec dec4032.jpg F5"; ok $? "N 640 found with F5, 12 MP F5 fails"
has "$L" "SKIP hwdec dec4032.jpg F6 (search miss" && has "$L" "SKIP hwdec dec4032.jpg F5n (search miss"; ok $? "N F6 and F5n are search misses"
grep -q 'dec4032.jpg F5i regs(35):.* 36c=db800000 370=db805000 374=db80a000 ' "$L" && has "$L" "PASS hwdec dec4032.jpg F5i"; ok $? "N F5i: tables 0x5000 apart, decodes"
! grep -Eq 'errno=(1|14|22) ' "$L"; ok $? "N no EPERM/EFAULT/EINVAL (F5i pointers pass the address checks)"
cp "$L" "$O/N/jpeg-selftest.log"
$NICE python3 "$PH/phone_check.py" "$O/N" --inputs "$I" > "$O/N/check.log" 2>&1
grep -q '^DEC PASS dec4032-F5i' "$O/N/check.log"; ok $? "N phone_check finds the source of jdec-dec4032-F5i ($(grep -c '^DEC PASS' "$O/N/check.log") DEC PASS)"

echo "== F: arm64-apple-ios16.0 build (compiled + linked, not run)"
bash "$PH/build_ios.sh" "$O/ios" > "$O/ios.log" 2>&1; ok $? "F $(tail -1 "$O/ios.log")"

echo "N2TEST JPGP $([ $pass = $total ] && echo PASS || echo FAIL) $pass/$total"
[ $pass = $total ]
