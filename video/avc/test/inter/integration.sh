#!/bin/bash
# SPDX-License-Identifier: MIT
#
# integration.sh [--quick|--full] [outdir] -- WP04's real-encoder checks.
#
# This is the part of the package's test suite that needs WP03's files: it
# builds the WP03 oracle CLI twice (with the real WP04 files, and with WP03's
# all-intra stub_inter.c), drives it with WP09's deterministic YUV generator,
# and compares ffmpeg's decoded pictures with n2avc_reconstruction() byte for
# byte.  It also measures the two effectiveness bounds and the real rate
# controller, and reports the 1280x720 one-thread encode speed.
#
# It is a separate script because the harness suite in run_tests.sh must stay
# runnable (and fast) before WP03 lands; run_tests.sh calls this one only when
# the WP03 sources and the CLI are present.  WP04 does not edit any WP03 file.
#
# Last line on success: N2TEST WP04 INTEGRATION PASS <passed>/<total>
set -euo pipefail
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
VID=$(cd "$HERE/../../.." && pwd)
MODE=${1:---quick}
OUT=${2:-$VID/build/WP04/integration}
if [ -n "${CORE_DIR:-}" ]; then
	:
elif [ -f "$VID/avc2/n2avc_core.c" ]; then
	CORE_DIR=$VID/avc2
else
	CORE_DIR=$VID/avc
fi
mkdir -p "$OUT/data"
# The raw pictures and streams are regenerable and large (the 1280x720 set is
# ~50 MB); the disk on this host is nearly full, so drop them on exit and keep
# only the logs and the stats files.
cleanup_raw() {
	rm -f "$OUT"/*.yuv "$OUT"/*.264 "$OUT"/*.rec "$OUT"/data/*.yuv "$OUT"/*.dec 2>/dev/null || true
	rm -f "$OUT"/*.log.trace 2>/dev/null || true
}
trap cleanup_raw EXIT
CC=${CC:-clang}

passed=0
total=0
failures=0

ok() {
	total=$((total + 1))
	passed=$((passed + 1))
	echo "  ok   $*"
}

bad() {
	total=$((total + 1))
	failures=$((failures + 1))
	echo "  FAIL $*"
}

# ---------------------------------------------------------------- build
if [ -f "$CORE_DIR/n2avc_core.c" ]; then
	CORE_SRCS="$CORE_DIR/n2avc_core.c"
	# The core may call the R2 in-loop deblocker (lead-owned, same directory).
	[ -f "$CORE_DIR/n2avc_deblock.c" ] && CORE_SRCS="$CORE_SRCS $CORE_DIR/n2avc_deblock.c"
else
	CORE_SRCS="$CORE_DIR/n2avc_bits.c $CORE_DIR/n2avc_ps.c $CORE_DIR/n2avc_xform.c $CORE_DIR/n2avc_cavlc.c $CORE_DIR/n2avc_intra.c $CORE_DIR/n2avc_mb.c $CORE_DIR/n2avc_enc.c"
fi
WP04_SRCS="$VID/avc/n2avc_me.c $VID/avc/n2avc_inter.c $VID/avc/n2avc_rc.c"
KERN="$VID/kern/n2vk_ref.c $VID/kern/n2vk_dispatch.c"
CLI="$VID/avc/test/core/n2avc_cli.c"
STUB="$VID/avc/test/core/stub_inter.c"
for f in $CORE_SRCS $WP04_SRCS $KERN $CLI; do
	[ -f "$f" ] || { echo "N2TEST WP04 INTEGRATION FAIL missing $f"; exit 1; }
done
[ -f "$STUB" ] || { echo "N2TEST WP04 INTEGRATION FAIL missing $STUB"; exit 1; }

echo "WP04 integration: building the $CORE_DIR CLI with the real inter files ..."
# shellcheck disable=SC2086
$CC -std=c11 -O2 -g -Wall -Wextra -pthread -I "$VID/include" -I "$CORE_DIR" -I "$VID/avc" \
	-o "$OUT/n2avc_cli" $CORE_SRCS $WP04_SRCS $CLI $KERN -lm
echo "WP04 integration: building the all-intra stub CLI ..."
# shellcheck disable=SC2086
$CC -std=c11 -O2 -g -Wall -Wextra -pthread -I "$VID/include" -I "$CORE_DIR" -I "$VID/avc" \
	-o "$OUT/n2avc_cli_stub" $CORE_SRCS "$STUB" $CLI $KERN -lm

# ---------------------------------------------------------------- vectors
gen() {
	local pattern=$1 size=$2 frames=$3 file=$OUT/data/$4

	if [ -s "$file" ]; then
		return 0
	fi
	python3 "$VID/vectors/gen_yuv.py" --pattern "$pattern" --size "$size" \
		--frames "$frames" --format i420 --out "$file"
}

yuv_of() { echo "$OUT/data/$1_$2_$3.yuv"; }

# ---------------------------------------------------------------- one case
# encode_case <name> <pattern> <WxH> <frames> <qp|-> <bitrate|-> <threads>
encode_case() {
	local name=$1 pattern=$2 size=$3 frames=$4 qp=$5 bitrate=$6 threads=$7
	local yuv r264 recon dec stats trace rate

	yuv=$(yuv_of "$pattern" "$size" "$frames")
	gen "$pattern" "$size" "$frames" "$(basename "$yuv")"
	r264=$OUT/$name.264
	recon=$OUT/$name.recon.yuv
	dec=$OUT/$name.dec.yuv
	stats=$OUT/$name.stats.json
	trace=$OUT/$name.trace.log

	if [ "$qp" != "-" ]; then
		rate="--qp $qp"
	else
		rate="--bitrate $bitrate"
	fi
	if ! "$OUT/n2avc_cli" --size "$size" --frames "$frames" $rate --threads "$threads" \
		--annexb --in "$yuv" --out "$r264" --recon "$recon" --stats "$stats" \
		>"$OUT/$name.encode.log" 2>&1; then
		bad "$name: encoder failed (see $OUT/$name.encode.log)"
		return 1
	fi
	if ! ffmpeg -v error -y -i "$r264" -f rawvideo -pix_fmt yuv420p "$dec" \
		>"$OUT/$name.ffmpeg.log" 2>&1; then
		bad "$name: ffmpeg could not decode"
		return 1
	fi
	if ! cmp -s "$dec" "$recon"; then
		bad "$name: decoder output != n2avc_reconstruction (sizes $(stat -c%s "$dec") / $(stat -c%s "$recon"))"
		return 1
	fi
	if ! ffmpeg -v error -y -i "$r264" -bsf:v trace_headers -c:v copy -f null - \
		>"$OUT/$name.trace.log" 2>&1; then
		bad "$name: trace_headers rejected the stream"
		return 1
	fi
	if [ -s "$OUT/$name.trace.log" ]; then
		bad "$name: trace_headers printed an error"
		return 1
	fi
	ok "$name"
	return 0
}

# ---------------------------------------------------------------- gate
# WP03's own intra CAVLC must be bit-exact before a P-slice result means
# anything.  This is a 1-frame noise picture at QP 20, the case that exposed
# the run_before/total_zeros bugs; it is intentionally not a WP04 test.
gen noise 176x144 1 "noise_176x144_1.yuv"
gate_yuv=$OUT/data/noise_176x144_1.yuv
if ! "$OUT/n2avc_cli" --size 176x144 --frames 1 --qp 20 --intra-only --annexb \
	--in "$gate_yuv" --out "$OUT/gate.264" --recon "$OUT/gate.recon.yuv" \
	>"$OUT/gate.encode.log" 2>&1; then
	echo "N2TEST WP04 INTEGRATION FAIL WP03 intra encoder failed on the 176x144 QP20 noise gate"
	tail -n 10 "$OUT/gate.encode.log"
	exit 1
fi
if ! ffmpeg -v error -y -i "$OUT/gate.264" -f rawvideo -pix_fmt yuv420p "$OUT/gate.dec.yuv" \
	>"$OUT/gate.ffmpeg.log" 2>&1 || ! cmp -s "$OUT/gate.dec.yuv" "$OUT/gate.recon.yuv"; then
	echo "N2TEST WP04 INTEGRATION FAIL WP03 intra baseline is not bit-exact (noise 176x144 QP20); P matrix not run"
	cat "$OUT/gate.ffmpeg.log" 2>/dev/null | head -5
	exit 1
fi
ok "gate: WP03 noise 176x144 QP20 intra round trip"

# ---------------------------------------------------------------- matrix
CASE_QPS="20 32 44"
SIZES="176x144 178x146 640x480"
PATTERNS="squares pan static"

for size in $SIZES; do
	for pattern in $PATTERNS; do
		for qp in $CASE_QPS; do
			for threads in 1 4; do
				encode_case "q_${pattern}_${size}_qp${qp}_t${threads}" \
					"$pattern" "$size" 10 "$qp" - "$threads" || true
			done
		done
	done
done

# 1280x720 is the expensive size; the quick mode runs one QP and both thread
# counts, the full mode adds the rest.
if [ "$MODE" = "--full" ]; then
	for pattern in $PATTERNS; do
		for qp in $CASE_QPS; do
			for threads in 1 4; do
				encode_case "q_${pattern}_1280x720_qp${qp}_t${threads}" \
					"$pattern" 1280x720 10 "$qp" - "$threads" || true
			done
		done
	done
else
	for threads in 1 4; do
		encode_case "q_pan_1280x720_qp32_t${threads}" \
			pan 1280x720 10 32 - "$threads" || true
	done
fi

for br in 300000 2000000; do
	for threads in 1 4; do
		encode_case "br_${br}_pan_640x480_t${threads}" \
			pan 640x480 10 - "$br" "$threads" || true
	done
done
if [ "$MODE" = "--full" ]; then
	for size in 176x144 178x146 1280x720; do
		for br in 300000 2000000; do
			encode_case "br_${br}_pan_${size}_t1" pan "$size" 10 - "$br" 1 || true
		done
	done
fi

# ---------------------------------------------------------------- group 3
# static: every P picture must be < 2 % of the IDR picture.
static_stats=$OUT/q_static_640x480_qp32_t1.stats.json
if [ -s "$static_stats" ]; then
	if python3 - "$static_stats" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
idr = [x for x in p if x["keyframe"]][0]["bytes"]
ps = [x["bytes"] for x in p if not x["keyframe"]]
assert ps, "no P pictures"
assert max(ps) * 100 < idr * 2, "max P %d is not < 2%% of IDR %d" % (max(ps), idr)
print("static: IDR %d bytes, P max %d bytes (%.2f%%)" % (idr, max(ps), 100.0 * max(ps) / idr))
PY
	then ok "effectiveness: static P < 2% of IDR"; else bad "effectiveness: static P >= 2% of IDR"; fi
else
	bad "effectiveness: static stats missing"
fi

# pan: the real P pictures must be < 35 % of the all-intra stub's P pictures.
pan_real=$OUT/q_pan_640x480_qp32_t1.stats.json
pan_stub=$OUT/stub_pan_640x480_qp32.stats.json
if [ ! -s "$pan_stub" ]; then
	"$OUT/n2avc_cli_stub" --size 640x480 --frames 10 --qp 32 --threads 1 --annexb \
		--in "$(yuv_of pan 640x480 10)" --out "$OUT/stub_pan_640x480_qp32.264" \
		--recon "$OUT/stub_pan_640x480_qp32.recon.yuv" --stats "$pan_stub" \
		>"$OUT/stub_pan_640x480_qp32.log" 2>&1 || true
fi
if [ -s "$pan_real" ] && [ -s "$pan_stub" ]; then
	if python3 - "$pan_real" "$pan_stub" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
s = json.load(open(sys.argv[2]))
rp = sum(x["bytes"] for x in r if not x["keyframe"])
sp = sum(x["bytes"] for x in s if not x["keyframe"])
assert sp > 0, "stub produced no P bytes"
assert rp * 100 < sp * 35, "real P %d is not < 35%% of stub P %d" % (rp, sp)
print("pan QP32: real P %d bytes, stub P %d bytes (%.1f%%)" % (rp, sp, 100.0 * rp / sp))
PY
	then ok "effectiveness: pan P < 35% of all-intra stub"; else bad "effectiveness: pan P >= 35% of stub"; fi
else
	bad "effectiveness: pan stats missing"
fi

# ---------------------------------------------------------------- group 4
# 60 pictures, 640x480, 1 Mbit/s, 30 fps: total within +-20 % of 250000 bytes
# and |QP step| <= 4 between consecutive P pictures.
gen pan 640x480 60 "pan_640x480_60.yuv"
rc_stats=$OUT/rc_60.stats.json
rc_264=$OUT/rc_60.264
if "$OUT/n2avc_cli" --size 640x480 --frames 60 --bitrate 1000000 --fps 30/1 \
	--threads 1 --annexb --in "$OUT/data/pan_640x480_60.yuv" --out "$rc_264" \
	--recon "$OUT/rc_60.recon.yuv" --stats "$rc_stats" >"$OUT/rc_60.log" 2>&1; then
	size=$(stat -c%s "$rc_264")
	if python3 - "$rc_stats" "$size" <<'PY'
import json, sys
p = json.load(open(sys.argv[1])); size = int(sys.argv[2])
assert len(p) == 60, "expected 60 pictures, got %d" % len(p)
assert 200000 <= size <= 300000, "total %d bytes outside 250000 +-20%%" % size
ps = [x for x in p if not x["keyframe"]]
for a, b in zip(ps, ps[1:]):
    assert abs(a["qp"] - b["qp"]) <= 4, "QP step %d -> %d at picture %d" % (a["qp"], b["qp"], b["picture"])
qps = [x["qp"] for x in p]
print("rate control: %d bytes (target 250000), QP %d..%d, max P step %d" %
      (size, min(qps), max(qps), max(abs(a["qp"]-b["qp"]) for a,b in zip(ps, ps[1:]))))
PY
	then ok "rate control: 60 pictures within +-20% and QP step <= 4"
	else bad "rate control: size or QP step outside the bounds"; fi
else
	bad "rate control: encoder failed (see $OUT/rc_60.log)"
fi

# ---------------------------------------------------------------- speed
# 1280x720, one thread, -O2: pictures per second on this host.
speed_yuv=$(yuv_of pan 1280x720 10)
if [ -s "$speed_yuv" ]; then
	t0=$(date +%s%N)
	"$OUT/n2avc_cli" --size 1280x720 --frames 10 --qp 32 --threads 1 --annexb \
		--in "$speed_yuv" --out "$OUT/speed.264" --recon "$OUT/speed.recon.yuv" \
		--stats "$OUT/speed.stats.json" >"$OUT/speed.log" 2>&1
	t1=$(date +%s%N)
	ms=$(( (t1 - t0) / 1000000 ))
	printf '  speed: 10 pictures 1280x720 one thread -O2 in %d ms = %.2f pictures/s\n' \
		"$ms" "$(python3 -c "print(10000.0/$ms)")"
	if [ "$ms" -lt 120000 ]; then ok "speed: 1280x720 one-thread run completed"; else bad "speed: 1280x720 run took ${ms} ms"; fi
else
	bad "speed: no 1280x720 vector"
fi

# ---------------------------------------------------------------- cleanup
rm -f "$OUT/data"/pan_640x480_60.yuv
rm -f "$OUT"/*.recon.yuv "$OUT"/*.dec.yuv

if [ "$failures" -ne 0 ]; then
	echo "N2TEST WP04 INTEGRATION FAIL $failures/$total"
	exit 1
fi
echo "N2TEST WP04 INTEGRATION PASS $passed/$total"
