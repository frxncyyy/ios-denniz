#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# run_avc2_tests.sh -- WP03's contract tests of the encoder core the plug-in links (avc2/n2avc_core.c, owned by
# the lead), through include/n2avc.h only.  Nothing here edits avc2/.
#
#   bash gpu/video/avc/test/core/run_avc2_tests.sh [outdir]
#
# Groups:
#   1. API contract under ASan+UBSan: argument errors, NV12 == I420, force_idr, keyint, N2AVC_F_HEADERS_IN_KEYFRAMES
#      with AVCC lengths, SPS values from include/n2avc.h, 4096x2304.
#   2. ThreadSanitizer with 4 and 16 slices, two encoders in parallel.
#   3. No allocation after n2avc_open (allocator interposition, no sanitizer).
#   4. The 1000-picture frame_num wrap (frame_num is 8 bits) through the CLI, every picture decoded by ffmpeg and
#      compared with the encoder's reconstruction.
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-$root/build/WP03/avc2}
CC=${CC:-clang}
tmp="$out/tmp"
mkdir -p "$out" "$tmp"

# The same composition as avc2/build_cli.sh: avc2's own (pinned) inter/me/rc plus the kernels.  Set
# N2AVC_INTER_DIR=$root/avc to judge WP04's working files instead of the pinned ones, as build_cli.sh does.
INTER_DIR=${N2AVC_INTER_DIR:-$root/avc2}
INC="-I $root/include -I $INTER_DIR -I $root/kern"
CORE="$root/avc2/n2avc_core.c $root/avc2/n2avc_deblock.c $INTER_DIR/n2avc_inter.c $INTER_DIR/n2avc_me.c \
      $INTER_DIR/n2avc_rc.c $root/kern/n2vk_ref.c $root/kern/n2vk_dispatch.c"
WARN="-std=c11 -O2 -g -Wall -Wextra -pthread"

pass=0
fail=0
fails=""

run() {                                  # run <name> <command...>
	local name=$1
	shift
	if "$@"; then
		pass=$((pass + 1))
	else
		fails="$fails $name"
		fail=$((fail + 1))
		echo "N2TEST WP03-avc2 FAIL $name"
	fi
}

echo "== WP03/avc2: API contract (ASan + UBSan)"
# shellcheck disable=SC2086
$CC $WARN -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC \
	$CORE "$here/t_avc2_api.c" -o "$out/t_avc2_api" -lm || { echo "N2TEST WP03-avc2 FAIL build"; exit 1; }
run api "$out/t_avc2_api"

echo "== WP03/avc2: ThreadSanitizer, 4 and 16 slices"
# shellcheck disable=SC2086
$CC $WARN -fsanitize=thread $INC $CORE "$here/t_threads.c" -o "$out/t_avc2_threads_tsan" -lm \
	|| { echo "N2TEST WP03-avc2 FAIL build-tsan"; exit 1; }
run threads-tsan "$out/t_avc2_threads_tsan"

echo "== WP03/avc2: no allocation after n2avc_open"
# shellcheck disable=SC2086
$CC $WARN $INC $CORE "$here/t_noalloc.c" -o "$out/t_avc2_noalloc" -lm \
	|| { echo "N2TEST WP03-avc2 FAIL build-noalloc"; exit 1; }
run noalloc "$out/t_avc2_noalloc"

echo "== WP03/avc2: 1000 pictures, frame_num wrap (8 bits), every picture decoded by ffmpeg"
wrap_test() {
	local yuv="$tmp/wrap.yuv" bit="$tmp/wrap.264" rec="$tmp/wrap.recon.yuv" dec="$tmp/wrap.dec.yuv"
	local frames=1000 size=64x64
	bash "$root/avc2/build_cli.sh" "$out/n2avc_cli" >/dev/null 2>&1 || {
		echo "  avc2/build_cli.sh failed"
		return 1
	}
	python3 - "$size" "$frames" "$yuv" <<-'PY'
	import sys
	size, frames, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
	w, h = (int(v) for v in size.split("x"))
	cw, ch = w // 2, h // 2
	with open(out, "wb") as f:
	    for fr in range(frames):
	        f.write(bytes(((x + 2 * y + 3 * fr) & 255) for y in range(h) for x in range(w)))
	        f.write(bytes((128 + ((cx + fr) & 31)) for cy in range(ch) for cx in range(cw)))
	        f.write(bytes((128 - (cy & 31)) for cy in range(ch) for cx in range(cw)))
	PY
	# keyint larger than 256 so that frame_num (8 bits) wraps at least twice in the P pictures.
	if ! "$out/n2avc_cli" --size "$size" --frames "$frames" --qp 30 --keyint 400 --threads 4 \
	     --annexb --in "$yuv" --out "$bit" --recon "$rec" --stats "$tmp/wrap.json" >"$tmp/wrap.log" 2>&1; then
		echo "  encode of $frames pictures failed:"
		cat "$tmp/wrap.log"
		return 1
	fi
	rm -f "$dec"
	if ! ffmpeg -v error -y -i "$bit" -f rawvideo -pix_fmt yuv420p "$dec" 2>"$tmp/wrap.err"; then
		echo "  ffmpeg rejected the 1000-picture stream:"
		cat "$tmp/wrap.err"
		return 1
	fi
	if [ -s "$tmp/wrap.err" ]; then
		echo "  ffmpeg wrote to stderr on the 1000-picture stream:"
		cat "$tmp/wrap.err"
		return 1
	fi
	if ! cmp -s "$dec" "$rec"; then
		echo "  decoded pictures differ from the reconstruction"
		return 1
	fi
	python3 - "$tmp/wrap.json" <<-'PY'
	import json, sys
	stats = json.load(open(sys.argv[1]))
	if len(stats) != 1000:
	    print("  expected 1000 pictures, the stats file has %d" % len(stats))
	    sys.exit(1)
	nums = [p["frame_num"] for p in stats]
	# frame_num restarts at 0 at every IDR and wraps modulo 256 between them; with keyint 400 the P pictures
	# reach 256 and therefore wrap at least once.
	wrap = any(nums[i + 1] < nums[i] and not stats[i + 1]["keyframe"] for i in range(len(nums) - 1))
	if not wrap:
	    print("  no frame_num wrap observed: fewer than 256 P pictures between two IDRs")
	    sys.exit(1)
	print("   frame_num wrapped as expected (8 bits, %d pictures)" % len(stats))
	PY
	rm -f "$yuv" "$dec" "$rec"
	return 0
}
run wrap-1000 wrap_test

echo "== WP03/avc2: summary"
total=$((pass + fail))
if [ "$fail" -ne 0 ]; then
	echo "N2TEST WP03-avc2 FAIL $pass/$total ($fails)"
	exit 1
fi
echo "N2TEST WP03-avc2 PASS $pass/$total"
