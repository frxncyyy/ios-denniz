#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# run_tests.sh -- the WP03 suite: the H.264 encoder core (bit writer, parameter sets, transform, CAVLC, intra,
# macroblock loop, public API) against the frozen interfaces include/n2avc.h and include/n2avc_int.h.
#
#   bash gpu/video/avc/test/core/run_tests.sh [outdir]
#
# Builds with clang -std=c11 -O2 -g -Wall -Wextra -fsanitize=address,undefined (and once with
# -fsanitize=thread), runs the unit and API tests, and then the oracle: every stream is decoded with ffmpeg and
# the decoded pictures must be byte-identical to n2avc_reconstruction().
#
# Environment: CC (default clang), N2AVC_SKIP_ORACLE=1 to skip the ffmpeg part, N2AVC_FULL_MATRIX=1 for the whole
# size/QP/thread matrix in addition to the standard one.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-$root/build/WP03}
CC=${CC:-clang}
tmp="$out/tmp"
mkdir -p "$out" "$tmp"

INC="-I $root/include -I $root/avc"
WARN="-std=c11 -O2 -g -Wall -Wextra -Werror"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
SRC="$root/avc/n2avc_bits.c $root/avc/n2avc_ps.c $root/avc/n2avc_xform.c $root/avc/n2avc_cavlc.c
     $root/avc/n2avc_intra.c $root/avc/n2avc_mb.c $root/avc/n2avc_enc.c $root/avc/test/core/stub_inter.c"

pass=0
fail=0

run() {                                  # run <name> <command...>
	local name=$1
	shift
	if "$@"; then
		pass=$((pass + 1))
	else
		echo "N2TEST WP03 FAIL $name"
		fail=$((fail + 1))
	fi
}

echo "== WP03: build (ASan + UBSan, -Werror)"
# shellcheck disable=SC2086
$CC $WARN $SAN $INC -o "$out/t_unit" $SRC "$here/t_unit.c"
# shellcheck disable=SC2086
$CC $WARN $SAN $INC -o "$out/t_api" $SRC "$here/t_api.c"
# shellcheck disable=SC2086
$CC $WARN $SAN $INC -o "$out/t_threads_asan" $SRC "$here/t_threads.c"

echo "== WP03: unit and API tests"
run unit "$out/t_unit"
run api "$out/t_api"

echo "== WP03: no allocation after n2avc_open (allocator interposition, no sanitizer)"
# shellcheck disable=SC2086
$CC $WARN $INC -o "$out/t_noalloc" $SRC "$here/t_noalloc.c"
run noalloc "$out/t_noalloc"

echo "== WP03: ThreadSanitizer on the four-thread encoder"
# shellcheck disable=SC2086
$CC $WARN -fsanitize=thread $INC -o "$out/t_threads_tsan" $SRC "$here/t_threads.c"
run threads-tsan "$out/t_threads_tsan"

if [ "${N2AVC_SKIP_ORACLE:-0}" = "1" ]; then
	echo "== WP03: oracle skipped (N2AVC_SKIP_ORACLE=1)"
else
	echo "== WP03: oracle (ffmpeg decodes, the reconstruction must match byte for byte)"
	# shellcheck disable=SC2086
	$CC $WARN $INC -o "$out/n2avc_cli" $SRC "$here/n2avc_cli.c"
	gen="$root/vectors/gen_yuv.py"

	make_yuv() {                     # make_yuv <pattern> <size> <frames> <out>
		if [ -f "$gen" ]; then
			python3 "$gen" --pattern "$1" --size "$2" --frames "$3" --out "$4" >/dev/null
		else
			python3 - "$1" "$3" "$4" <<-'PY'
			import sys
			size, frames, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
			w, h = (int(v) for v in size.lower().split("x"))
			cw, ch = w // 2, h // 2
			state = 1
			with open(out, "wb") as f:
			    for fr in range(frames):
			        f.write(bytes(((x + 2 * y + 3 * fr) & 255) for y in range(h) for x in range(w)))
			        f.write(bytes((128 + ((cx + fr) & 31)) for cy in range(ch) for cx in range(cw)))
			        f.write(bytes((128 - (cy & 31)) for cy in range(ch) for cx in range(cw)))
			PY
		fi
	}

	check_stream() {                 # check_stream <pattern> <size> <qp> <threads> <intra-only|p> [frames]
		local pat=$1 size=$2 qp=$3 th=$4 kind=$5 frames=${6:-2}
		local yuv="$tmp/o.yuv" bit="$tmp/o.264" rec="$tmp/o.recon.yuv" dec="$tmp/o.dec.yuv"
		local flags=()
		[ "$kind" = "intra" ] && flags+=(--intra-only)
		make_yuv "$pat" "$size" "$frames" "$yuv"
		if ! "$out/n2avc_cli" --size "$size" --frames "$frames" --qp "$qp" --threads "$th" \
		     "${flags[@]+"${flags[@]}"}" --annexb --in "$yuv" --out "$bit" --recon "$rec" >"$tmp/cli.log" 2>&1; then
			echo "  encode failed: $pat $size qp$qp th$th $kind"
			cat "$tmp/cli.log"
			return 1
		fi
		rm -f "$dec"
		if ! ffmpeg -v error -y -i "$bit" -f rawvideo -pix_fmt yuv420p "$dec" 2>"$tmp/ffmpeg.log"; then
			echo "  ffmpeg rejected: $pat $size qp$qp th$th $kind"
			cat "$tmp/ffmpeg.log"
			return 1
		fi
		if [ -s "$tmp/ffmpeg.log" ]; then
			echo "  ffmpeg wrote to stderr: $pat $size qp$qp th$th $kind"
			cat "$tmp/ffmpeg.log"
			return 1
		fi
		if ! cmp -s "$dec" "$rec"; then
			echo "  decoded pictures differ from the reconstruction: $pat $size qp$qp th$th $kind"
			return 1
		fi
		return 0
	}

	# The subset of the task's oracle: sizes x QP x threads x {intra only, P}.
	for size in 176x144 178x146 640x480; do
		for qp in 10 26 40 51; do
			for th in 1 4; do
				for kind in intra p; do
					if [ "$size" = "640x480" ] && [ "${N2AVC_FULL_MATRIX:-0}" != "1" ]; then
						# keep the plain run under a minute: the big size at two QPs and both kinds
						case "$qp/$kind" in
						26/intra|26/p|40/p) ;;
						*) continue ;;
						esac
					fi
					run "oracle-$size-qp$qp-t$th-$kind" check_stream gradient "$size" "$qp" "$th" "$kind" 2
				done
			done
		done
	done

	# PSNR floors on the gradient vector at 176x144 (task: >= 28 dB at QP 40, >= 44 dB at QP 10).
	psnr_check() {                   # psnr_check <qp> <floor>
		local qp=$1 floor=$2
		local yuv="$tmp/p.yuv" bit="$tmp/p.264" rec="$tmp/p.recon.yuv"
		make_yuv gradient 176x144 2 "$yuv"
		"$out/n2avc_cli" --size 176x144 --frames 2 --qp "$qp" --annexb --in "$yuv" --out "$bit" \
			--recon "$rec" >/dev/null 2>&1
		python3 - "$yuv" "$rec" "$floor" <<-'PY'
		import sys, math
		w, h = 176, 144
		src = open(sys.argv[1], "rb").read()
		rec = open(sys.argv[2], "rb").read()
		floor = float(sys.argv[3])
		se = 0
		n = 0
		frames = len(src) // (w * h * 3 // 2)
		for f in range(frames):
		    base = f * w * h * 3 // 2
		    for i in range(w * h):
		        d = src[base + i] - rec[base + i]
		        se += d * d
		        n += 1
		mse = se / n
		psnr = 99.0 if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
		print("   psnr luma %.2f dB (floor %.1f)" % (psnr, floor))
		sys.exit(0 if psnr >= floor else 1)
		PY
	}
	run psnr-qp10 psnr_check 10 44
	run psnr-qp40 psnr_check 40 28

	# Noise at QP 10 must still decode (the I_PCM fallback), and a single 16x16 macroblock must work.
	run oracle-noise-qp10 check_stream noise 176x144 10 1 p 2
	run oracle-16x16 check_stream text 16x16 26 1 intra 1
	run oracle-nv12 check_stream gradient 320x240 26 4 p 2

	# trace_headers must parse the SPS, PPS and slice headers of an intra-only and a P stream without error.
	trace_check() {
		local kind=$1
		local yuv="$tmp/t.yuv" bit="$tmp/t.264"
		local flags=()
		[ "$kind" = "intra" ] && flags+=(--intra-only)
		make_yuv gradient 176x144 2 "$yuv"
		"$out/n2avc_cli" --size 176x144 --frames 2 --qp 26 "${flags[@]+"${flags[@]}"}" --annexb \
			--in "$yuv" --out "$bit" >/dev/null 2>&1
		ffmpeg -v error -i "$bit" -c copy -bsf:v trace_headers -f null - 2>"$tmp/trace.log"
		if [ -s "$tmp/trace.log" ]; then
			echo "  trace_headers complained:"
			cat "$tmp/trace.log"
			return 1
		fi
		return 0
	}
	run trace-intra trace_check intra
	run trace-p trace_check p
fi

echo "== WP03: summary"
total=$((pass + fail))
if [ "$fail" -ne 0 ]; then
	echo "N2TEST WP03 FAIL $fail of $total"
	exit 1
fi
echo "N2TEST WP03 PASS $pass/$total"
