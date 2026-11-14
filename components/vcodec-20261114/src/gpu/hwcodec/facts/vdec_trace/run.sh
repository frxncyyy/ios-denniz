#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/facts/vdec_trace/run.sh -- DEC02's register-access trace of H.264 decode and the
# PAGES.md / COST.md documents.
#
# What runs (host only; nothing touches the phone, nothing is published):
#   1. a /tmp copy of the tree (emu/loader/shim/tests/vcu/ap/fakevenc/host/include) with the
#      overlays DEC02 needs and does not own -- exact text and owners in tools/stage.py:
#        V34 property layout.mode=0 (MM21, not UFO MT21)          [opus-dec1, board 1794415185]
#        V40 window 12 = 0x16000000+0x30000 (RWVC_WIN_VDEC_ALL)   [DEC04/DEC07]
#        T02 fake: VDEC_BASE/RACING are VDEC_SOC pages            [reported to hw-t02/hw-k02]
#   2. DEC01's AP client (ap/n2ap_vdec.c) with DEC02's n2tr recorder between it and T02's fake
#      hardware, driving the phone's own vpud + vendor libraries under the AArch32 interpreter:
#        320x240  main, 12 AUs, -bf 2 (B-frames)      1920x1080 high, 6 AUs, -bf 0
#      twice per resolution, because the acceptance oracle is that a deterministic re-run gives the
#      same access multiset and a byte-identical PAGES.md.
#   3. opus-dec1's reference driver (dec1/vdec_ref.c) on the same two streams, once each: an
#      independent implementation of the same kernel protocol, used only to cross-check the page
#      set and the per-AU register counts.
#   4. DEC02's own AP probe (probe/vdec_probe.c) on a one-AU stream: header START -> frame START,
#      no mmio_bad.  A third, independent implementation.
#
# Last line: N2TEST DEC02 PASS <n>/<n> (or FAIL <what>, exit 1).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
HWD="$(cd "$HERE/../.." && pwd)"                # gpu/hwcodec
ROOT="$(cd "$HWD/../.." && pwd)"                # the work tree root
VFS="$ROOT/gpu/build/cam-vendor-20261109/vfs"
OUT="${N2DEC02_OUT:-/tmp/n2hw-dec02}"
TREE="$OUT/tree"
VEC="$OUT/vec"
CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L"
JOBS=2
NICE="nice -n 10"

pass=0
total=0
ck() {   # ck <ok:0|1> <name>
	total=$((total + 1))
	if [ "$1" = 0 ]; then
		pass=$((pass + 1))
	else
		echo "DEC02 FAIL: $2"
	fi
}

fail() {
	echo "N2TEST DEC02 FAIL $1"
	exit 1
}

[ -d "$VFS/lib" ] || fail "vendor directory missing: $VFS"
command -v ffmpeg >/dev/null || fail "ffmpeg missing (the test vectors are generated with it)"
mkdir -p "$OUT" "$VEC"

echo "DEC02: stage tree + drivers"
python3 "$HERE/tools/stage.py" "$TREE" "$OUT/vdec_ref.c" || fail "staging failed"

echo "DEC02: build libn2vhost.a (-j$JOBS, nice 10)"
$NICE make -s -C "$TREE/host" "$OUT/o2/libn2vhost.a" OUT="$OUT" JOBS="$JOBS" -j"$JOBS" \
	>"$OUT/build.log" 2>&1 || { tail -20 "$OUT/build.log"; fail "host library build failed"; }
grep -E '^N2VHOST build:' "$OUT/build.log" | tail -1

INC="-I$TREE -I$TREE/include -I$TREE/tests/hw -I$TREE/tests/emu -I$TREE/vcu -I$TREE/ap -I$TREE/host -I$TREE/shim"
$NICE clang $CFLAGS $INC -I"$HERE/probe" "$HERE/probe/dec01_client.c" "$HERE/probe/n2tr.c" \
	"$TREE/ap/n2ap_vdec.c" -o "$OUT/dec01_client" \
	-Wl,--whole-archive "$OUT/o2/libn2vhost.a" -Wl,--no-whole-archive \
	-lpthread -lm >"$OUT/build-client.log" 2>&1 || { tail -20 "$OUT/build-client.log"; fail "DEC01-client driver build failed"; }
$NICE clang $CFLAGS $INC -I"$HERE/probe" "$OUT/vdec_ref.c" "$HERE/probe/n2tr.c" \
	-o "$OUT/vdec_ref" -Wl,--whole-archive "$OUT/o2/libn2vhost.a" -Wl,--no-whole-archive \
	-lpthread -lm >"$OUT/build-ref.log" 2>&1 || { tail -20 "$OUT/build-ref.log"; fail "reference driver build failed"; }
$NICE clang $CFLAGS $INC -I"$HERE/probe" "$HERE/probe/n2tr.c" "$HERE/probe/vdec_probe.c" \
	-o "$OUT/vdec_probe" -Wl,--whole-archive "$OUT/o2/libn2vhost.a" -Wl,--no-whole-archive \
	-lpthread -lm >"$OUT/build-probe.log" 2>&1 || { tail -20 "$OUT/build-probe.log"; fail "probe build failed"; }
ck 0 "host library, DEC01-client driver, reference driver and probe build"

# Test vectors: fixed ffmpeg parameters, generated once per OUT.  The same files are used for every
# run, so a libx264 difference cannot make two runs of this script disagree.
[ -s "$VEC/t320b.h264" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 \
	-frames:v 12 -pix_fmt yuv420p -c:v libx264 -profile:v main -bf 2 -g 12 "$VEC/t320b.h264" \
	|| fail "ffmpeg 320x240 vector failed"
[ -s "$VEC/t1080.h264" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=1920x1080:rate=30 \
	-frames:v 6 -pix_fmt yuv420p -c:v libx264 -profile:v high -bf 0 "$VEC/t1080.h264" \
	|| fail "ffmpeg 1920x1080 vector failed"
[ -s "$VEC/first.h264" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 \
	-frames:v 1 -pix_fmt yuv420p -c:v libx264 -profile:v main -bf 0 -x264-params repeat-headers=1 \
	-f h264 "$VEC/first.h264" || fail "ffmpeg one-AU vector failed"
# Multi-slice, B-frames, 1080p: the stream class most likely to reach a page the two main vectors do
# not (more slice headers -> more AVC_VLD/LAT use, more DPB churn).  One run, page-set check only.
[ -s "$VEC/t1080s.h264" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=1920x1080:rate=30 \
	-frames:v 6 -pix_fmt yuv420p -c:v libx264 -profile:v high -bf 2 -slices 4 -g 6 \
	"$VEC/t1080s.h264" || fail "ffmpeg multi-slice vector failed"

run_client() {   # run_client <run> <stream> <WxH>  -> $OUT/<stream>-<run>.{log,trace}
	local run=$1 stream=$2 size=$3
	# VR_REC is T02's own recorder (n2vc_hw_fake.h format): an independent witness of the same
	# traffic.  n2tr sees every op at the n2vc_hw boundary, T02's recorder only the ones the fake
	# was asked to perform, so their frozen-window counts must agree.
	N2DEC02_TRACE="$OUT/$stream-$run.trace" VR_REC="$OUT/$stream-$run.t02.txt" \
		timeout 110 "$OUT/dec01_client" --vendor "$VFS" --stream "$VEC/$stream.h264" \
		--maxframes 1000 --size "$size" >"$OUT/$stream-$run.log" 2>&1 \
		|| fail "dec01_client run $run/$stream exited non-zero"
	grep -q "DC reached VD-g" "$OUT/$stream-$run.log" || fail "dec01_client run $run/$stream did not reach VD-g"
	grep -q "DC fake: register violations=0" "$OUT/$stream-$run.log" || fail "dec01_client run $run/$stream: fake violations"
}

run_ref() {      # run_ref <stream> -> $OUT/<stream>-ref.{log,trace}
	local stream=$1
	VR_FAKEIRQ=1 N2DEC02_TRACE="$OUT/$stream-ref.trace" timeout 110 "$OUT/vdec_ref" \
		"$VFS" "$VEC/$stream.h264" 1000 >"$OUT/$stream-ref.log" 2>&1 \
		|| fail "reference driver on $stream exited non-zero"
	grep -q "VR reached VD-g" "$OUT/$stream-ref.log" || fail "reference driver on $stream did not reach VD-g"
}

echo "DEC02: DEC01 client, two deterministic runs per resolution"
run_client 1 t320b 320x240
run_client 2 t320b 320x240
run_client 1 t1080 1920x1080
run_client 2 t1080 1920x1080
ck 0 "DEC01 client reaches VD-g on both streams, both runs (fake violations 0)"

echo "DEC02: reference driver (cross-check)"
run_ref t320b
run_ref t1080
ck 0 "reference driver reaches VD-g on both streams"

echo "DEC02: multi-slice B-frame 1080p (page-set robustness)"
N2DEC02_TRACE="$OUT/t1080s-1.trace" timeout 110 "$OUT/dec01_client" --vendor "$VFS" \
	--stream "$VEC/t1080s.h264" --maxframes 1000 --size 1920x1080 >"$OUT/t1080s-1.log" 2>&1 \
	|| fail "dec01_client multi-slice run exited non-zero"
grep -q "DC reached VD-g" "$OUT/t1080s-1.log" || fail "multi-slice run did not reach VD-g"
grep -q "DC fake: register violations=0" "$OUT/t1080s-1.log" || fail "multi-slice run: fake violations"
ck 0 "multi-slice 1080p session reaches VD-g with 0 fake violations"

# The vendor decodes on two guest threads, so the *interleaving* of their register accesses is
# scheduling-dependent; each thread's own sequence is not.  The determinism check is therefore on
# the normalised multiset (phase + every field, thread tag and per-thread sequence removed).
norm() { awk '{ if ($1 == "MARK") ph = $2; else print ph, $0 }' "$1" | sed -E 's/ t[0-9]+ [0-9]+$/ t/' | sort; }
for pair in "t320b-1 t320b-2" "t1080-1 t1080-2"; do
	set -- $pair
	norm "$OUT/$1.trace" > "$OUT/$1.norm"
	norm "$OUT/$2.trace" > "$OUT/$2.norm"
	cmp -s "$OUT/$1.norm" "$OUT/$2.norm" && ck 0 "$1 access multiset identical twice" \
		|| ck 1 "$1 access multiset differs between runs"
done

echo "DEC02: classify"
python3 "$HERE/tools/classify.py" --trace320 "$OUT/t320b-1.trace" --trace1080 "$OUT/t1080-1.trace" \
	--log320 "$OUT/t320b-1.log" --log1080 "$OUT/t1080-1.log" \
	--pages "$HERE/PAGES.md" --cost "$HERE/COST.md" --check 2>"$OUT/classify1.err" \
	|| { cat "$OUT/classify1.err"; fail "classify checks failed (run 1)"; }
ck 0 "classifier self-checks"
python3 "$HERE/tools/classify.py" --trace320 "$OUT/t320b-2.trace" --trace1080 "$OUT/t1080-2.trace" \
	--log320 "$OUT/t320b-2.log" --log1080 "$OUT/t1080-2.log" \
	--pages "$OUT/PAGES-2.md" --cost "$OUT/COST-2.md" --check 2>"$OUT/classify2.err" \
	|| { cat "$OUT/classify2.err"; fail "classify checks failed (run 2)"; }
cmp -s "$HERE/PAGES.md" "$OUT/PAGES-2.md" && ck 0 "PAGES.md identical twice" || ck 1 "PAGES.md differs between runs"
# COST.md's timing rows are a measurement and vary; its counter rows must not.
sed -n '/## 1. Per access unit/,/## 5. Guest time/p' "$HERE/COST.md" > "$OUT/cost-counter-1.txt"
sed -n '/## 1. Per access unit/,/## 5. Guest time/p' "$OUT/COST-2.md" > "$OUT/cost-counter-2.txt"
cmp -s "$OUT/cost-counter-1.txt" "$OUT/cost-counter-2.txt" \
	&& ck 0 "COST.md counter rows identical twice" || ck 1 "COST.md counter rows differ between runs"

# The page list must not depend on the AP driver: compare the 4 KiB page list of the DEC01-client
# run with the reference driver's run.  (Individual offsets differ by construction: the client's
# LOCK role restores all 68 racing words, the reference harness does not.)
python3 "$HERE/tools/classify.py" --pageset "$OUT/t320b-1.trace" > "$OUT/pages-client.txt"
python3 "$HERE/tools/classify.py" --pageset "$OUT/t320b-ref.trace" > "$OUT/pages-ref.txt"
python3 "$HERE/tools/classify.py" --pageset "$OUT/t1080s-1.trace" > "$OUT/pages-ms.txt"
cmp -s "$OUT/pages-client.txt" "$OUT/pages-ms.txt" \
	&& ck 0 "multi-slice B-frame 1080p touches exactly the same 18 pages" \
	|| { ck 1 "multi-slice stream touches a different page set"; diff "$OUT/pages-client.txt" "$OUT/pages-ms.txt" | head -10; }
cmp -s "$OUT/pages-client.txt" "$OUT/pages-ref.txt" \
	&& ck 0 "DEC01 client and reference driver touch the same 4 KiB pages at 320x240" \
	|| { ck 1 "the two drivers touch different pages at 320x240"; diff "$OUT/pages-client.txt" "$OUT/pages-ref.txt" | head -10; }

# T02's recorder (--record file, n2vc_hw_fake.h) and n2tr must agree on the frozen-window ops.
t02_ops=$(awk '$1 == "R" || $1 == "W" || $1 == "WM" || $1 == "POLL" { n++ } END { print n + 0 }' "$OUT/t320b-1.t02.txt")
n2_ops=$(awk '$1 == "R" || $1 == "W" || $1 == "WM" { n++ } END { print n + 0 }' "$OUT/t320b-1.trace")
n2_win12=$(awk '($1 == "R" || $1 == "W" || $1 == "WM") && $2 == 12 { n++ } END { print n + 0 }' "$OUT/t320b-1.trace")
[ $((n2_ops - n2_win12)) = "$t02_ops" ] \
	&& ck 0 "T02 record and n2tr agree on the frozen-window ops ($t02_ops)" \
	|| ck 1 "T02 record ($t02_ops) and n2tr non-VDEC_ALL ops ($((n2_ops - n2_win12))) disagree"

echo "DEC02: independent probe (own AP client, one header + one frame)"
timeout 60 "$OUT/vdec_probe" --vendor "$VFS" --stream "$VEC/first.h264" --width 320 --height 240 \
	--trace "$OUT/probe.trace" --json "$OUT/probe.json" >"$OUT/probe.log" 2>&1 \
	|| fail "probe exited non-zero"
grep -q "N2DEC02 start header rc=0 status=0 .*pic=320x240" "$OUT/probe.log" \
	&& ck 0 "probe header START gives pic=320x240" || ck 1 "probe header START failed"
grep -Eq "N2DEC02 start frame rc=0 status=0 .*mmio=[1-9][0-9]*r/[1-9][0-9]*w/0bad" "$OUT/probe.log" \
	&& ck 0 "probe frame START touches registers with no mmio_bad" || ck 1 "probe frame START has no clean register traffic"

echo "DEC02: page-list table"
grep -E '^\| 0x[0-9a-f]{5} \|' "$HERE/PAGES.md" | sed 's/^/  /' | head -20

if [ "$pass" = "$total" ]; then
	echo "N2TEST DEC02 PASS $pass/$total"
else
	echo "N2TEST DEC02 FAIL $((total - pass)) of $total checks failed"
	exit 1
fi
