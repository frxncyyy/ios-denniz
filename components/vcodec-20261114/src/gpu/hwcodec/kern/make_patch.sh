#!/bin/bash
# Assemble gpu/hwcodec/kern/0299-nord2-jpegdev-vcodec.patch from the current sources and verify it round-trips.
#
# 0299 applies ON TOP OF gpu/hwcodec/kernel/b (the 0298 state) and adds, in one patch:
#   bsd/dev/sweet_touch.c      the three includes (relay, vcodec, jpegdev) and the two start calls
#   bsd/dev/nord2_vcodec_relay.inc   (IK, this package)
#   bsd/dev/nord2_vcodec.inc         (K02)
#   bsd/dev/nord2_jpegdev.inc        (K01)
#   bsd/dev/redwood_vcodec.h         byte-identical copy of the frozen gpu/hwcodec/include/redwood_vcodec.h
#   bsd/dev/redwood_jpeg.h           byte-identical copy of the frozen gpu/hwcodec/include/redwood_jpeg.h
# The header copies exist because the kernel build's include path (gpu/tools/xnu_staged_compile.py) has no
# gpu/hwcodec/include entry and build_p1_candidate.py compiles the staged sweet_touch.c with fixed flags; a quoted
# include in bsd/dev is the only place both the CI-style compile and the host tests agree on.  compile_check.sh checks
# that the copies are byte-identical to the frozen originals, so they cannot drift silently.
#
# Verification (all failures exit 1):
#   1. a fresh copy of kernel/b + 0299 equals the constructed tree byte for byte (round trip);
#   2. 0298 then 0299 apply cleanly to a fresh copy of the q10at3 stage (the q10ik source state) and give the same files.
#
# Environment overrides (used by test_ik/run_tests.sh with stand-ins while K01/K02 are unwritten):
#   N2IK_RELAY_INC, N2IK_VCODEC_INC, N2IK_JPEGDEV_INC, N2IK_HEADER_DIR, N2IK_OUT, N2IK_BASE
set -euo pipefail

H=$(cd "$(dirname "$0")" && pwd)                 # gpu/hwcodec/kern
W=$(cd "$H/../../.." && pwd)                     # repo work root
BASE=${N2IK_BASE:-$W/gpu/hwcodec/kernel/b}
RELAY=${N2IK_RELAY_INC:-$H/bsd/dev/nord2_vcodec_relay.inc}
VCODEC=${N2IK_VCODEC_INC:-$H/bsd/dev/nord2_vcodec.inc}
JPEGDEV=${N2IK_JPEGDEV_INC:-$H/bsd/dev/nord2_jpegdev.inc}
INCLUDE=${N2IK_HEADER_DIR:-$W/gpu/hwcodec/include}
OUT=${N2IK_OUT:-$H/0299-nord2-jpegdev-vcodec.patch}
STAGE_DEV=${N2IK_STAGE_DEV:-$W/gpu/build/autotest/q10at3/kernel/stage/bsd/dev}
P298=$W/gpu/hwcodec/kernel/0298-nord2-hwcodec-jpeg.patch
T=/tmp/n2hw-ik/patch

fail() { echo "MAKE_PATCH FAILED: $*" >&2; exit 1; }
for f in "$BASE" "$RELAY" "$VCODEC" "$JPEGDEV" "$INCLUDE/redwood_vcodec.h" "$INCLUDE/redwood_jpeg.h" \
         "$H/bsd/dev/sweet_touch.c.patch" "$P298" "$STAGE_DEV"; do
	[ -e "$f" ] || fail "missing $f (K01 owns nord2_jpegdev.inc, K02 owns nord2_vcodec.inc; set N2IK_* to a stand-in)"
done
case "$OUT" in /*) ;; *) fail "N2IK_OUT must be an absolute path" ;; esac

rm -rf "$T"
mkdir -p "$T/a" "$T/b"
cp -a "$BASE/." "$T/a/"
cp -a "$T/a/." "$T/b/"

# 1. the sweet_touch.c change
(cd "$T/b" && patch -p1 -s -i "$H/bsd/dev/sweet_touch.c.patch") || fail "sweet_touch.c.patch does not apply to $BASE"

# 2. the new sources and the frozen header copies
cp "$RELAY"   "$T/b/bsd/dev/nord2_vcodec_relay.inc"
cp "$VCODEC"  "$T/b/bsd/dev/nord2_vcodec.inc"
cp "$JPEGDEV" "$T/b/bsd/dev/nord2_jpegdev.inc"
cp "$INCLUDE/redwood_vcodec.h" "$T/b/bsd/dev/redwood_vcodec.h"
cp "$INCLUDE/redwood_jpeg.h"   "$T/b/bsd/dev/redwood_jpeg.h"

# 3. the patch
set +e
(cd "$T" && diff -ruN a b > "$OUT")
rc=$?
set -e
[ "$rc" -ne 0 ] || fail "no differences: $BASE already contains 0299?"
[ "$rc" -eq 1 ] || fail "diff failed ($rc)"
# deterministic output: the file mtimes of the two staging trees are not part of the patch
sed -i -E 's/^(--- a\/[^\t]*)\t.*$/\1/; s/^(\+\+\+ b\/[^\t]*)\t.*$/\1/' "$OUT"

# 4. round trip: fresh kernel/b + 0299 == the constructed tree
cp -a "$T/a" "$T/c"
(cd "$T/c" && patch -p1 -s -i "$OUT") || fail "0299 does not apply to a fresh copy of kernel/b"
if ! diff -rq "$T/b" "$T/c" > "$T/roundtrip.log"; then
	sed -n 1,20p "$T/roundtrip.log" >&2
	fail "0299 round trip differs from the constructed tree"
fi

# 5. the series on the real q10at3 stage: 0298 then 0299 must apply and produce the same new files
mkdir -p "$T/series/bsd"
cp -a "$STAGE_DEV" "$T/series/bsd/dev"
(cd "$T/series" && patch -p1 -s --no-backup-if-mismatch -i "$P298") || fail "0298 does not apply to the q10at3 stage"
(cd "$T/series" && patch -p1 -s --no-backup-if-mismatch -i "$OUT") || fail "0299 does not apply after 0298 on the q10at3 stage"
for f in sweet_touch.c nord2_vcodec_relay.inc nord2_vcodec.inc nord2_jpegdev.inc redwood_vcodec.h redwood_jpeg.h nord2_hwcodec.inc; do
	cmp -s "$T/b/bsd/dev/$f" "$T/series/bsd/dev/$f" || fail "series result differs: bsd/dev/$f"
done

files=$(grep -c '^diff -ruN' "$OUT" || true)
bytes=$(wc -c < "$OUT")
lines=$(wc -l < "$OUT")
echo "0299 written: $OUT"
echo "  files: $files  lines: $lines  bytes: $bytes  (base: $BASE)"
echo "  round trip OK; 0298+0299 OK on the q10at3 stage"
echo "MAKE_PATCH OK"
