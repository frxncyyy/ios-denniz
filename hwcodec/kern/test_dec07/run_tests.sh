#!/bin/bash
# SPDX-License-Identifier: MIT
#
# DEC07 host test: RWVC_WIN_VDEC_ALL (ABI rev 3) in the kernel driver and in T02's fake hardware.
#
# What it does, in order:
#   1. applies the four DEC07 patches to a fresh copy of the tree files (fails with "stale" if the tree moved):
#        patches/redwood_vcodec-rev3.patch       include/redwood_vcodec.h        (for hw-lead)
#        patches/nord2_vcodec-vdec-all.patch     kern/bsd/dev/nord2_vcodec.inc   (for hw-k02/opus-kern)
#        patches/n2vc_hw_fake-vdec-all.patch     tests/hw/n2vc_hw_fake.{c,h}     (for hw-t02)
#        patches/t_fake-vdec-all.patch           tests/hw/t_fake.c               (for hw-t02, keeps its suite green)
#   2. K02's own register-model suite (kern/test_k02/t_k02.c) against the patched driver, with the rev 3 header and
#      with the current rev 2 header (the patch carries a scaffold so it can land before hw-lead);
#   3. this package's driver test (t_dec07.c, its own MT6893 model) against the patched driver + rev 3 header;
#   4. T02's fake suite (tests/hw/t_fake.c) against the patched fake + rev 3 header, then this package's fake test;
#   5. the staged kernel compile of bsd/dev/sweet_touch.c with the patched driver and header (0 unknown symbols).
# Builds only under /tmp/n2hw-DEC07/run.  Last line: N2TEST DEC07 PASS n/n.
set -euo pipefail
ID=DEC07
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)                  # gpu/hwcodec
W=$(cd "$ROOT/../.." && pwd)                     # work root
SB=/tmp/n2hw-DEC07/run
PATCH=$HERE/patches
STAGE_DEV=$W/gpu/build/autotest/q10at3/kernel/stage/bsd/dev
BASE_OBJ=$W/gpu/build/autotest/q10at3/kernel/sweet_touch.o
KERNEL=$W/gpu/build/autotest/q10at3/kernel/kernel.development.nord2-metal-q10at3
COMPILE=$W/gpu/tools/xnu_staged_compile.py
PASSED=0
TOTAL=0
fail() { echo "N2TEST $ID FAIL $*"; exit 1; }

rm -rf "$SB"
mkdir -p "$SB"

# ---- 1. patches on a fresh copy of the tree --------------------------------------------------------------
A=$SB/apply
mkdir -p "$A/include" "$A/kern/bsd/dev" "$A/tests/hw"
cp "$ROOT/include/redwood_vcodec.h"       "$A/include/"
cp "$ROOT/kern/bsd/dev/nord2_vcodec.inc"  "$A/kern/bsd/dev/"
cp "$ROOT/tests/hw/n2vc_hw_fake.c" "$ROOT/tests/hw/n2vc_hw_fake.h" "$ROOT/tests/hw/t_fake.c" "$A/tests/hw/"
apply_one() {   # $1 patch name; fails if the tree file already carries the change or moved
	[ -f "$PATCH/$1.patch" ] || fail "missing $PATCH/$1.patch"
	( cd "$A" && patch -p1 -s --dry-run -i "$PATCH/$1.patch" ) \
		|| fail "patch $1 is stale against the tree (regenerate it against the current file)"
	( cd "$A" && patch -p1 -s --no-backup-if-mismatch -i "$PATCH/$1.patch" )
}
# the header: if hw-lead has landed rev 3, the tree is the rev 3 copy and rev 2 is the reverse-applied one; before that,
# the tree is rev 2 and the patch makes rev 3.  Both variants are built by the K02 step below.
if grep -q "RWVC_WIN_VDEC_ALL" "$A/include/redwood_vcodec.h"; then
	echo "== header: the tree already carries rev 3 ($(sha256sum "$A/include/redwood_vcodec.h" | cut -c1-16)...)"
	mkdir -p "$SB/inc3" "$SB/inc2"
	cp "$A/include/redwood_vcodec.h" "$SB/inc3/redwood_vcodec.h"
	cp "$A/include/redwood_vcodec.h" "$SB/inc2/redwood_vcodec.h"
	( cd "$SB/inc2" && patch -p2 -R -s -i "$PATCH/redwood_vcodec-rev3.patch" ) \
		|| fail "cannot reconstruct the rev 2 header from the tree (revert the rev 3 patch cleanly)"
else
	mkdir -p "$SB/inc2" "$SB/inc3"
	cp "$A/include/redwood_vcodec.h" "$SB/inc2/redwood_vcodec.h"
	cp "$A/include/redwood_vcodec.h" "$SB/inc3/redwood_vcodec.h"
	( cd "$SB/inc3" && patch -p2 -s --dry-run -i "$PATCH/redwood_vcodec-rev3.patch" ) \
		|| fail "header rev 3 patch is stale against the tree"
	( cd "$SB/inc3" && patch -p2 -s --no-backup-if-mismatch -i "$PATCH/redwood_vcodec-rev3.patch" )
fi
# the fake: T02 may have landed the patch; use the tree then, apply it otherwise
if grep -q "VDEC_ALL" "$A/tests/hw/n2vc_hw_fake.c"; then
	echo "== fake: the tree already carries VDEC_ALL ($(sha256sum "$A/tests/hw/n2vc_hw_fake.c" | cut -c1-16)...)"
	grep -q "0x30000" "$A/tests/hw/t_fake.c" || fail "fake has VDEC_ALL but t_fake.c has no 0x30000 entry"
else
	apply_one n2vc_hw_fake-vdec-all
	apply_one t_fake-vdec-all
fi
# hw-k02 applies the driver patch; once it is in the tree, test the tree as it is (it may have been extended there)
if grep -q "n2vc_vdec_page_blk" "$A/kern/bsd/dev/nord2_vcodec.inc"; then
	echo "== driver: the tree already carries RWVC_WIN_VDEC_ALL ($(sha256sum "$A/kern/bsd/dev/nord2_vcodec.inc" | cut -c1-16)...); testing it as is"
else
	apply_one nord2_vcodec-vdec-all
fi
echo "== staged: header + fake + driver ready for the current tree"

# ---- helpers ---------------------------------------------------------------------------------------------
build() {   # $1 out, $2 config (san|o2), rest = sources
	local out=$1 cfg=$2; shift 2
	local flags=(-O2)
	if [ "$cfg" = san ]; then
		flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
	fi
	nice -n 10 clang -std=c11 -g -Wall -Wextra -Werror "${flags[@]}" -o "$out" "$@" \
		|| fail "build $out"
}
run_case() {   # $1 log, $2 expected id, $3.. = command
	local log=$1 want=$2; shift 2
	set +e
	"$@" > "$log" 2>&1
	local rc=$?
	set -e
	if [ "$rc" -ne 0 ]; then
		grep -E '^FAIL|^VIOLATION|violations' "$log" | head -20 || true
		fail "$want exited $rc (log $log)"
	fi
	local line
	line=$(tail -n 1 "$log")
	case "$line" in
		"N2TEST $want PASS "*) ;;
		*) grep -E '^FAIL|^VIOLATION' "$log" | head -20 || true; fail "$want: $line" ;;
	esac
	local a b
	a=$(printf '%s\n' "$line" | sed -E 's/^N2TEST [A-Za-z0-9_-]+ PASS ([0-9]+)\/([0-9]+).*/\1/')
	b=$(printf '%s\n' "$line" | sed -E 's/^N2TEST [A-Za-z0-9_-]+ PASS ([0-9]+)\/([0-9]+).*/\2/')
	PASSED=$((PASSED + a))
	TOTAL=$((TOTAL + b))
	echo "$line"
}

# ---- 2. K02's suite against the patched driver, rev 2 and rev 3 headers -----------------------------------
# The rev 2 variant only exists while the driver carries its scaffold (the rev 3 window constant); once hw-lead lands the
# header and hw-k02 drops the scaffold, a rev 2 build cannot compile a test that names RWVC_WIN_VDEC_ALL.
RUN_REV2=1
if grep -q "RWVC_WIN_VDEC_ALL" "$A/include/redwood_vcodec.h" && ! grep -q "#ifndef RWVC_WIN_VDEC_ALL" "$A/kern/bsd/dev/nord2_vcodec.inc"; then
	RUN_REV2=0
	echo "== K02: rev 3 header landed and no scaffold left, skipping the rev 2 variant"
fi
for v in rev2 rev3; do
	if [ "$v" = rev2 ] && [ "$RUN_REV2" = 0 ]; then continue; fi
	B=$SB/k02-$v
	mkdir -p "$B/bsd/dev" "$B/test_k02" "$B/include"
	cp "$A/kern/bsd/dev/nord2_vcodec.inc" "$B/bsd/dev/"
	cp "$ROOT/kern/test_k02/t_k02.c" "$B/test_k02/"
	if [ "$v" = rev2 ]; then cp "$SB/inc2/redwood_vcodec.h" "$B/include/"; else cp "$SB/inc3/redwood_vcodec.h" "$B/include/"; fi
	for cfg in san o2; do
		build "$B/t-$cfg" "$cfg" -I "$B/include" "$B/test_k02/t_k02.c"
		run_case "$B/$cfg.log" K02 "$B/t-$cfg"
	done
	rm -f "$B/t-san" "$B/t-o2"
done

# ---- 3. DEC07's driver test ------------------------------------------------------------------------------
B=$SB/dec07
mkdir -p "$B"
cp "$A/kern/bsd/dev/nord2_vcodec.inc" "$B/nord2_vcodec_inc.h"
cp "$SB/inc3/redwood_vcodec.h" "$B/"
for cfg in san o2; do
	build "$B/t-$cfg" "$cfg" -I "$B" "$HERE/t_dec07.c"
	run_case "$B/$cfg.log" DEC07CORE "$B/t-$cfg"
done
rm -f "$B/t-san" "$B/t-o2"

# ---- 4. T02's fake suite and DEC07's fake test -----------------------------------------------------------
B=$SB/fake
mkdir -p "$B"
cp "$SB/inc3/redwood_vcodec.h" "$B/"
cp "$ROOT/include/n2vc_hw.h" "$B/"
cp "$A/tests/hw/n2vc_hw_fake.c" "$A/tests/hw/n2vc_hw_fake.h" "$A/tests/hw/t_fake.c" "$B/"
for cfg in san o2; do
	build "$B/t02-$cfg" "$cfg" -I "$B" -pthread "$B/t_fake.c" "$B/n2vc_hw_fake.c"
	run_case "$B/t02-$cfg.log" T02-hw "$B/t02-$cfg" "$ROOT/tests/hw/scripts" "$B"
	build "$B/dec07-$cfg" "$cfg" -I "$B" -pthread "$HERE/t_fake_dec07.c" "$B/n2vc_hw_fake.c"
	run_case "$B/dec07fake-$cfg.log" DEC07FAKE env DEC07_SCRATCH="$B" "$B/dec07-$cfg"
done
rm -f "$B/t02-san" "$B/t02-o2" "$B/dec07-san" "$B/dec07-o2"

# ---- 5. staged kernel compile with the patched driver and header -----------------------------------------
B=$SB/compile
mkdir -p "$B/bsd"
cp -a "$STAGE_DEV" "$B/bsd/dev"
( cd "$B" && patch -p1 -s --no-backup-if-mismatch -i "$ROOT/kernel/0298-nord2-hwcodec-jpeg.patch" ) \
	|| fail "0298 does not apply to the q10at3 stage"
( cd "$B" && patch -p1 -s --no-backup-if-mismatch -i "$ROOT/kern/0299-nord2-jpegdev-vcodec.patch" ) \
	|| fail "0299 does not apply after 0298 (regenerate it, then re-run)"
cp "$ROOT/kern/bsd/dev/nord2_vcodec_relay.inc" "$ROOT/kern/bsd/dev/nord2_jpegdev.inc" "$B/bsd/dev/"
cp "$A/kern/bsd/dev/nord2_vcodec.inc" "$B/bsd/dev/nord2_vcodec.inc"
cp "$A/include/redwood_vcodec.h" "$B/bsd/dev/redwood_vcodec.h"
cp "$ROOT/include/redwood_jpeg.h" "$B/bsd/dev/redwood_jpeg.h"
if ! nice -n 10 python3 "$COMPILE" "$B/bsd/dev/sweet_touch.c" --out "$B/sweet_touch.o" \
	--extra=-Wno-unsafe-buffer-usage > "$B/compile.log" 2>&1; then
	grep -E "error:|warning:" "$B/compile.log" | head -30 || tail -20 "$B/compile.log"
	fail "staged sweet_touch.c does not compile"
fi
llvm-nm -u "$B/sweet_touch.o"    | awk 'NF {print $NF}' | sort -u > "$B/new.und"
llvm-nm -u "$BASE_OBJ"           | awk 'NF {print $NF}' | sort -u > "$B/base.und"
comm -13 "$B/base.und" "$B/new.und" > "$B/added.und"
llvm-nm --defined-only "$KERNEL" | awk 'NF {print $NF}' | sort -u > "$B/kernel.def"
comm -23 "$B/added.und" "$B/kernel.def" > "$B/unknown.und"
added=$(wc -l < "$B/added.und")
unknown=$(wc -l < "$B/unknown.und")
echo "== staged compile: sweet_touch.o $(wc -c < "$B/sweet_touch.o") bytes, $added new undefined symbols, $unknown unknown"
if [ "$unknown" -ne 0 ]; then sed 's/^/    /' "$B/unknown.und" >&2; fail "staged compile: $unknown unknown symbols"; fi
PASSED=$((PASSED + 1))
TOTAL=$((TOTAL + 1))

echo "N2TEST $ID PASS $PASSED/$TOTAL"
