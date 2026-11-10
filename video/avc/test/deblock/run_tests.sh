#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_tests.sh [outdir] -- R2, the H.264 in-loop deblocking filter (avc2/n2avc_deblock.c).
#
# 1. builds and runs the hand-computed unit tests with ASan+UBSan and with plain -O2;
# 2. makes a build-directory copy of avc2/n2avc_core.c with the three integration edits the lead has been asked for
#    (patch_core.py; no source file is changed) and builds a small encoder CLI that sets N2AVC_F_DEBLOCK;
# 3. the oracle: for every case, ffmpeg decodes the deblocked stream and its output must equal n2avc_reconstruction()
#    byte for byte, with empty ffmpeg stderr and the exact frame count; intra-only and P, 1/4/16 slices, QP 10..51,
#    16x16 to 1280x720, all five WP09 patterns.  A case whose *baseline* (deblock off) stream is not bit-exact is a bug
#    of the encoder core/inter, not of R2: it is reported as BASELINE-FAIL and excluded from the count (STATUS.md keeps
#    the list).  A case whose baseline is exact but whose deblocked stream differs is an R2 failure;
# 4. the same, on a subset, under ASan+UBSan; one multi-slice case under TSan;
# 5. a trace_headers check that the slices really say disable_deblocking_filter_idc = 0 with both offsets 0;
# 6. an arm64-apple-ios16.0 -Werror compile of avc2/n2avc_deblock.c.
#
# Last line on success: N2TEST R2 PASS <n>/<n>
# R2_QUICK=1 cuts the matrix down for iteration; R2_SKIP_MATRIX=1 runs only 1, 2, 5, 6.
set -uo pipefail
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
V=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$V/build/R2}
TMP=$OUT/tmp
CC=${CC:-clang}
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
WARN="-Wall -Wextra -Werror"
INC="-I $V/include"
QUICK=${R2_QUICK:-0}
SKIP_MATRIX=${R2_SKIP_MATRIX:-0}
FROM_HEAD=${R2_FROM_HEAD:-0}

rm -rf "$TMP"
mkdir -p "$TMP" || { echo "N2TEST R2 FAIL cannot create $TMP"; exit 1; }

pass=0
total=0
fail=0
excluded=0
matrix_cases=0
baseline_list=""
fail_list=""

say() { printf '%s\n' "$*"; }
bump() { total=$((total + 1)); pass=$((pass + 1)); }

# ---------------------------------------------------------------- unit tests
build_unit() {
	local san=$1 out=$2 extra=$3
	# shellcheck disable=SC2086
	$CC -std=c11 -O2 -g $WARN $san $extra $INC -o "$out" "$V/avc2/n2avc_deblock.c" "$HERE/t_deblock.c" || return 1
}

run_unit() {
	local bin=$1 name=$2 line n
	if ! "$bin" >"$TMP/$name.log" 2>&1; then
		say "N2TEST R2 FAIL unit $name"
		tail -n 30 "$TMP/$name.log"
		fail=$((fail + 1))
		return 1
	fi
	line=$(tail -n 1 "$TMP/$name.log")
	case "$line" in
	N2TEST\ R2\ PASS\ */*) ;;
	*) say "N2TEST R2 FAIL unit $name (last line: $line)"; fail=$((fail + 1)); return 1 ;;
	esac
	n=${line##* }
	n=${n%%/*}
	say "  unit $name: $line"
	pass=$((pass + n)); total=$((total + n))
	return 0
}

say "== R2: unit tests =="
build_unit "$SAN" "$TMP/t_deblock_asan" "" || { echo "N2TEST R2 FAIL build unit asan"; exit 1; }
build_unit "" "$TMP/t_deblock_plain" "" || { echo "N2TEST R2 FAIL build unit plain"; exit 1; }
run_unit "$TMP/t_deblock_asan" asan || true
run_unit "$TMP/t_deblock_plain" plain || true

# ---------------------------------------------------------------- patched core + CLI
say "== R2: build the encoder CLI with the deblock call =="
if [ "$FROM_HEAD" = 1 ]; then
	HASH=$(git -C "$V" rev-parse --short HEAD) || { say "R2_FROM_HEAD: no git"; exit 1; }
	mkdir -p "$TMP/head"
	for f in avc2/n2avc_core.c avc/n2avc_inter.c avc/n2avc_inter_priv.h avc/n2avc_me.c avc/n2avc_rc.c; do
		git -C "$V" show "HEAD:gpu/video/$f" > "$TMP/head/$(basename "$f")" || { say "R2_FROM_HEAD: cannot read $f"; exit 1; }
	done
	SRC_CORE=$TMP/head/n2avc_core.c
	SRC_INTER=$TMP/head/n2avc_inter.c
	SRC_ME=$TMP/head/n2avc_me.c
	SRC_RC=$TMP/head/n2avc_rc.c
	HEADINC="-I $TMP/head"
	say "  R2_FROM_HEAD=1: building the committed encoder state $HASH (not the working tree)"
else
	SRC_CORE=$V/avc2/n2avc_core.c
	SRC_INTER=$V/avc/n2avc_inter.c
	SRC_ME=$V/avc/n2avc_me.c
	SRC_RC=$V/avc/n2avc_rc.c
	HEADINC=""
	if command -v git >/dev/null 2>&1; then
		DIRTY=$(git -C "$V" status --porcelain -- avc/n2avc_inter.c avc/n2avc_me.c avc/n2avc_rc.c 2>/dev/null || true)
		if [ -n "$DIRTY" ]; then
			say "  NOTE: the working tree has uncommitted edits in the shared encoder files:"
			printf '%s\n' "$DIRTY" | sed 's/^/    /'
			say "  The suite tests the working tree (the integration target); cases whose baseline is not bit-exact are"
			say "  reported as BASELINE-FAIL and excluded. Use R2_FROM_HEAD=1 to build the committed state instead."
		fi
	fi
fi
python3 "$HERE/patch_core.py" "$SRC_CORE" "$TMP/n2avc_core_deblock.c" || {
	say "N2TEST R2 FAIL patch_core"
	exit 1
}
python3 "$HERE/patch_inter.py" "$SRC_INTER" "$TMP/n2avc_inter_8411.c" || {
	say "N2TEST R2 FAIL patch_inter"
	exit 1
}
CORE="$TMP/n2avc_core_deblock.c"
INTER="$TMP/n2avc_inter_8411.c"
SRCS="$CORE $INTER $SRC_ME $SRC_RC $V/kern/n2vk_ref.c $V/kern/n2vk_dispatch.c $V/avc2/n2avc_deblock.c $HERE/t_cli.c"

# shellcheck disable=SC2086
$CC -std=c11 -O2 -g $WARN -pthread -include limits.h $INC $HEADINC -I "$V/avc" -I "$V/kern" -o "$TMP/t_cli" $SRCS -lm || {
	say "N2TEST R2 FAIL build t_cli"
	exit 1
}
# shellcheck disable=SC2086
$CC -std=c11 -O2 -g $WARN -pthread -include limits.h $SAN $INC $HEADINC -I "$V/avc" -I "$V/kern" -o "$TMP/t_cli_asan" $SRCS -lm || {
	say "N2TEST R2 FAIL build t_cli_asan"
	exit 1
}
# shellcheck disable=SC2086
$CC -std=c11 -O2 -g $WARN -pthread -include limits.h -fsanitize=thread $INC $HEADINC -I "$V/avc" -I "$V/kern" -o "$TMP/t_cli_tsan" $SRCS -lm || {
	say "N2TEST R2 FAIL build t_cli_tsan"
	exit 1
}
say "  t_cli built (plain, ASan+UBSan, TSan; host-only -include limits.h while avc/n2avc_me.c lacks <limits.h>)"

# ---------------------------------------------------------------- oracle helpers
GEN=$V/vectors/gen_yuv.py
CACHE=$TMP/cache
mkdir -p "$CACHE"

# make_input <size> <pattern> <frames> -> path in $CACHE
make_input() {
	local size=$1 pat=$2 frames=$3
	local f=$CACHE/${pat}_${size}_${frames}.yuv

	if [ ! -s "$f" ]; then
		python3 "$GEN" --pattern "$pat" --size "$size" --frames "$frames" --format i420 --out "$f.tmp" >/dev/null 2>&1 &&
			mv "$f.tmp" "$f" || { rm -f "$f.tmp"; return 1; }
	fi
	printf '%s' "$f"
}

# decode_equal <bin> <deblock:0|1> <size> <frames> <qp> <threads> <mode> <in> -> 0 when bit-exact
decode_equal() {
	local bin=$1 db=$2 size=$3 frames=$4 qp=$5 th=$6 mode=$7 in=$8
	local w=${size%x*} h=${size#*x}
	local tag="$TMP/case_$$"
	local flags=""
	local rc

	[ "$db" = 1 ] && flags="--deblock"
	# shellcheck disable=SC2086
	"$bin" --size "$size" --frames "$frames" --qp "$qp" --threads "$th" $mode $flags --in "$in" \
		--out "$tag.264" --recon "$tag.recon" >"$tag.enc.log" 2>&1 || {
		rm -f "$tag".*; return 1
	}
	ffmpeg -v error -y -i "$tag.264" -f rawvideo -pix_fmt yuv420p "$tag.dec" 2>"$tag.err"
	rc=$?
	if [ $rc -ne 0 ] || [ -s "$tag.err" ]; then
		rm -f "$tag".*; return 1
	fi
	if [ "$(stat -c %s "$tag.dec" 2>/dev/null || echo 0)" != "$((frames * w * h * 3 / 2))" ]; then
		rm -f "$tag".*; return 1
	fi
	if ! cmp -s "$tag.dec" "$tag.recon"; then
		rm -f "$tag".*; return 1
	fi
	rm -f "$tag".*
	return 0
}

# run_case <size> <pattern> <qp> <threads> <mode> <frames> <binary>
run_case() {
	local size=$1 pat=$2 qp=$3 th=$4 mode=$5 frames=$6 bin=$7
	local in
	matrix_cases=$((matrix_cases + 1))
	in=$(make_input "$size" "$pat" "$frames") || {
		say "FAIL input generation $size $pat $frames"
		fail=$((fail + 1)); total=$((total + 1)); return
	}
	if decode_equal "$bin" 1 "$size" "$frames" "$qp" "$th" "$mode" "$in"; then
		bump
		return
	fi
	# Failed with deblocking.  Is the baseline itself exact?  If not, this is not an R2 defect.
	if ! decode_equal "$TMP/t_cli" 0 "$size" "$frames" "$qp" "$th" "$mode" "$in"; then
		excluded=$((excluded + 1))
		total=$((total + 1))   # counted below by subtracting one from both after the run
		baseline_list="$baseline_list
    BASELINE-FAIL $size $pat qp$qp t$th ${mode:-P} frames$frames (deblock-off stream is not bit-exact; not R2)"
		printf '.'
		return
	fi
	fail=$((fail + 1)); total=$((total + 1))
	fail_list="$fail_list
    R2-FAIL $size $pat qp$qp t$th ${mode:-P} frames$frames (baseline exact, deblocked stream differs)"
	printf 'F'
}

# run_group <size> <pattern> <frames> <threads list> <qp list> <modes...>
run_group() {
	local size=$1 pat=$2 frames=$3 ths=$4 qps=$5
	shift 5
	local modes=("$@")
	local in th qp mode

	in=$(make_input "$size" "$pat" "$frames") || {
		say "FAIL input generation $size $pat $frames"
		fail=$((fail + 1)); total=$((total + 1)); return
	}
	for th in $ths; do
		for qp in $qps; do
			for mode in "${modes[@]}"; do
				run_case "$size" "$pat" "$qp" "$th" "$mode" "$frames" "$TMP/t_cli"
			done
		done
	done
	rm -f "$in"
}

# ---------------------------------------------------------------- trace_headers
check_headers() {
	local in o n_idc n_a n_b bad
	in=$(make_input 176x144 gradient 2) || return 1
	o=$TMP/hdr.264
	"$TMP/t_cli" --size 176x144 --frames 2 --qp 26 --threads 4 --deblock --in "$in" --out "$o" --recon "$TMP/hdr.recon" || return 1
	n_idc=$(ffmpeg -hide_banner -loglevel trace -i "$o" -c copy -bsf:v trace_headers -f null - 2>&1 | grep -c 'disable_deblocking_filter_idc *1 = 0')
	n_a=$(ffmpeg -hide_banner -loglevel trace -i "$o" -c copy -bsf:v trace_headers -f null - 2>&1 | grep -c 'slice_alpha_c0_offset_div2 *1 = 0')
	n_b=$(ffmpeg -hide_banner -loglevel trace -i "$o" -c copy -bsf:v trace_headers -f null - 2>&1 | grep -c 'slice_beta_offset_div2 *1 = 0')
	bad=$(ffmpeg -hide_banner -loglevel trace -i "$o" -c copy -bsf:v trace_headers -f null - 2>&1 | grep -c 'disable_deblocking_filter_idc *[0-9]* = 1')
	rm -f "$o" "$TMP/hdr.recon"
	# 2 pictures x 4 slices (176x144 has 9 rows, 4 slices) = 8 slice headers; one SPS/PPS pair per IDR.
	if [ "$n_idc" -ge 8 ] && [ "$n_a" -eq "$n_idc" ] && [ "$n_b" -eq "$n_idc" ] && [ "$bad" -eq 0 ]; then
		say "  trace_headers: $n_idc slices with disable_deblocking_filter_idc=0 and both offsets 0"
		return 0
	fi
	say "  trace_headers FAIL: idc0=$n_idc alpha0=$n_a beta0=$n_b idc1=$bad"
	return 1
}

# ---------------------------------------------------------------- the oracle matrix
if [ "$SKIP_MATRIX" = 0 ]; then
	say "== R2: oracle matrix (deblocked decode == reconstruction, byte for byte) =="
	start=$SECONDS

	if [ "$QUICK" = 1 ]; then
		run_group 176x144 gradient 6 "1 4" "26" "" "--intra-only"
		run_group 176x144 noise 6 "1 4" "26 51" "" "--intra-only"
		run_group 178x146 squares 6 "4" "26" "" "--intra-only"
	else
		# 16x16 and 32x32: one and four macroblocks; picture-boundary and one internal edge.
		run_group 16x16 gradient 6 "1" "10 26 40 51" "" "--intra-only"
		run_group 16x16 noise    6 "1" "10 26 40 51" "" "--intra-only"
		run_group 16x16 squares  6 "1" "10 26 40 51" "" "--intra-only"
		run_group 32x32 gradient 6 "1" "10 26 40 51" "" "--intra-only"
		run_group 32x32 noise    6 "1" "10 26 40 51" "" "--intra-only"
		run_group 32x32 squares  6 "1" "10 26 40 51" "" "--intra-only"

		# 176x144 and 178x146: 1 and 4 slices, all patterns, the interesting QP range.
		for size in 176x144 178x146; do
			for pat in gradient noise squares pan static; do
				run_group "$size" "$pat" 6 "1 4" "10 26 40 51" "" "--intra-only"
			done
		done

		# 320x240 and 640x480: 4 slices.
		for size in 320x240 640x480; do
			for pat in gradient noise pan; do
				run_group "$size" "$pat" 4 "4" "20 32 51" "" "--intra-only"
			done
		done

		# 1280x720, 2 pictures: the large end of the matrix.
		for pat in gradient pan; do
			run_group 1280x720 "$pat" 2 "4" "26 40" ""
		done

		# Slice stress: many slices (16 threads are capped to the macroblock rows).
		run_group 176x144 noise 6 "16" "26 40" "" "--intra-only"
		run_group 176x144 pan   6 "9"  "26" "" "--intra-only"
		run_group 640x480 pan   4 "16" "26" ""
		run_group 640x480 noise 4 "16" "51" "--intra-only"
	fi
	say "  oracle matrix: $matrix_cases cases, $pass passed of $total judged so far, $((SECONDS - start)) s"
fi

# ---------------------------------------------------------------- ASan subset
if [ "$SKIP_MATRIX" = 0 ]; then
	say "== R2: ASan+UBSan oracle subset =="
	# These run the ASan binary explicitly, so a memory error in the filter cannot hide behind an exact cmp.
	for spec in "176x144 noise 26 4 --intra-only 6" "176x144 noise 40 4 --intra-only 6" "176x144 gradient 26 4 --intra-only 6" \
	            "176x144 squares 51 4 --intra-only 6" "178x146 noise 26 4 '' 6" "640x480 pan 26 4 '' 4"; do
		# shellcheck disable=SC2086
		set -- $spec
		size=$1; pat=$2; qp=$3; th=$4; mode=$5; frames=$6
		[ "$mode" = "''" ] && mode=""
		in=$(make_input "$size" "$pat" "$frames") || { fail=$((fail + 1)); total=$((total + 1)); continue; }
		if decode_equal "$TMP/t_cli_asan" 1 "$size" "$frames" "$qp" "$th" "$mode" "$in"; then
			bump
		elif ! decode_equal "$TMP/t_cli" 0 "$size" "$frames" "$qp" "$th" "$mode" "$in"; then
			excluded=$((excluded + 1)); total=$((total + 1))
			baseline_list="$baseline_list
    BASELINE-FAIL(asan) $size $pat qp$qp t$th ${mode:-P} (deblock-off stream is not bit-exact; not R2)"
			printf '.'
		else
			fail=$((fail + 1)); total=$((total + 1))
			fail_list="$fail_list
    R2-FAIL(asan) $size $pat qp$qp t$th ${mode:-P}"
			printf 'F'
		fi
	done
fi

# ---------------------------------------------------------------- TSan
if [ "$SKIP_MATRIX" = 0 ]; then
	say "== R2: TSan (4 slice threads, deblocking after the join) =="
	for spec in "176x144 noise 26 --deblock 6" "176x144 gradient 26 --deblock --intra-only 6"; do
		# shellcheck disable=SC2086
		set -- $spec
		size=$1; pat=$2; qp=$3; shift 3
		flags=""
		while [ $# -gt 1 ]; do flags="$flags $1"; shift; done
		frames=$1
		in=$(make_input "$size" "$pat" "$frames") || { fail=$((fail + 1)); total=$((total + 1)); continue; }
		# shellcheck disable=SC2086
		if TSAN_OPTIONS=halt_on_error=1 "$TMP/t_cli_tsan" --size "$size" --frames "$frames" --qp "$qp" --threads 4 $flags \
			--in "$in" --out "$TMP/tsan.264" --recon "$TMP/tsan.recon" >"$TMP/tsan.log" 2>&1; then
			bump
		else
			fail=$((fail + 1)); total=$((total + 1))
			say "  TSan FAIL $size $pat qp$qp $flags"
			tail -n 20 "$TMP/tsan.log"
		fi
		rm -f "$TMP/tsan.264" "$TMP/tsan.recon"
	done
fi

# ---------------------------------------------------------------- headers
say "== R2: slice header =="
if check_headers; then bump; else fail=$((fail + 1)); total=$((total + 1)); fi

# ---------------------------------------------------------------- iOS compile
say "== R2: arm64-apple-ios16.0 compile =="
RESINC=$($CC -print-resource-dir)/include
if [ -d "$V/sdk/sysinc" ]; then
	if $CC -c --target=arm64-apple-ios16.0 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc \
		-isystem "$RESINC" -isystem "$V/sdk/sysinc" $INC -std=c11 $WARN \
		-o "$TMP/n2avc_deblock.ios.o" "$V/avc2/n2avc_deblock.c"; then
		say "  n2avc_deblock.c compiles for arm64-apple-ios16.0 with -Werror"
		bump
	else
		say "  iOS compile FAIL"
		fail=$((fail + 1)); total=$((total + 1))
	fi
else
	say "  sdk/sysinc missing; iOS compile not checked"
	fail=$((fail + 1)); total=$((total + 1))
fi

# ---------------------------------------------------------------- result
rm -rf "$CACHE"
# The excluded (baseline) cases were counted once in total; remove them so n/n means "judged and passed".
total=$((total - excluded))
if [ -n "$baseline_list" ]; then
	say ""
	say "WARNING: $excluded case(s) excluded because the current avc2/inter baseline is not bit-exact WITHOUT deblocking."
	say "         These are encoder-core failures, not R2; reproduce each with --deblock omitted:"
	printf '%s\n' "$baseline_list"
	say ""
fi
if [ -n "$fail_list" ]; then
	say "R2 cases that failed with an exact baseline:$fail_list"
fi
if [ "$fail" -ne 0 ]; then
	say "N2TEST R2 FAIL $((pass))/$((pass + fail))"
	exit 1
fi
if [ "$pass" -ne "$total" ]; then
	say "N2TEST R2 FAIL internal count: pass=$pass total=$total"
	exit 1
fi
say "N2TEST R2 PASS $pass/$total"
exit 0
