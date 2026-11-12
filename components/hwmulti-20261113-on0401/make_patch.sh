#!/bin/bash
# 0402 (hwmulti-20261113): CPU-free composition -- more overlay layers (2L + cascaded 4L OVL, kern.n2fbhwlayers 2..6), arbitrary
# layer rects + plane alpha in the planner, H6 GPU fences (SwapEnd write_seq snapshot, bounded hold, never a CPU fallback),
# IOMFBScalingLimits = 1:1, CPU frames counted by reason + "NORD2 FB CPUFREE: FB HW: CPU-FREE PASS|FAIL" every 60 s, test 7.
# Files: iokit/Kernel/arm/SweetFramebuffer.cpp, iokit/Kernel/arm/sweet_fb_hw.h.
# VARIANT on0401: a/ = the chain + 0394..0401 (= iommu-20261112/patched-sources-0401, 0401 sha 38d17cee...), b/ = patched.
# Differences to hwmulti-20261113 (on 0400): hwWriteRegs gates only 2L lists through 0401 (4L lists are physical-only), no HOOK comment.
#
#   ./make_patch.sh                 regenerate the patch from a/ -> b/, self-check (applies to a/ with --fuzz=0, no offset, == b/)
#   BASE=<stage dir> ./make_patch.sh   REBASE: take a/ from that stage (e.g. a PREFLIGHT_ONLY tree of chain + 0401, when 0401 touches
#                                   SweetFramebuffer.cpp in its marked hunks), carry the 0402 change over with patch(1) (fuzz allowed
#                                   only here, reported), refresh b/, then regenerate and self-check as above.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0402-nord2-fb-hw-multi-layer.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h"
if [ -n "${BASE:-}" ]; then
  T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
  for f in $FILES; do mkdir -p "$T/new/$(dirname $f)"; cp "$BASE/$f" "$T/new/$f"; done
  (cd "$T/new" && patch -p1 --no-backup-if-mismatch -i "$OUT") | tee "$T/rebase.log"
  if grep -qE 'FAILED|rej' "$T/rebase.log"; then echo "REBASE FAILED: resolve by hand (b/ unchanged)"; exit 1; fi
  for f in $FILES; do cp "$BASE/$f" "a/$f"; cp "$T/new/$f" "b/$f"; done
  echo "rebased a/ on $BASE"
fi
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T2=$(mktemp -d); trap 'rm -rf "$T2" ${T:-}' EXIT
cp -a a/. "$T2/"
(cd "$T2" && patch -p1 --fuzz=0 -i "$OUT") > "$T2.log" 2>&1 || { cat "$T2.log"; echo "SELF-CHECK FAILED: does not apply"; exit 1; }
if grep -qE 'offset|fuzz|FAILED|Reversed' "$T2.log"; then cat "$T2.log"; echo "SELF-CHECK FAILED: offset/fuzz"; exit 1; fi
rm -f "$T2.log"
for f in $FILES; do cmp "$T2/$f" "b/$f"; done
rm -rf patched-sources-0402; for f in $FILES; do mkdir -p patched-sources-0402/$(dirname $f); cp b/$f patched-sources-0402/$f; done
echo "SELF-CHECK OK: applies to a/ with --fuzz=0, no offset, result == b/"
sha256sum "$OUT"
