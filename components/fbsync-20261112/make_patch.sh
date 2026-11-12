#!/bin/bash
# 0393 (fbsync-20261112), on top of 0392 (fbretain-20261112): display-sync fixes for the interaction artifacts.
#  - SwapWait default level 1 (n2swait=0 = old): a swap retires when its copy ended; CoreAnimation (two surfaces, can_update_) no longer
#    redraws a surface the copy has not read.  Valve: not-ready > 250 ms is answered ready.
#  - retire state knows the 0156 prepare slot (prepNext took swaps without telling it; handOver marked the newer pending swap presenting).
#  - OVL flip before every push path (TE-less / blocking paths pushed the old front buffer); no flip when nothing was composited.
#  - black outside the layers (two panel buffers: unwritten areas showed the frame of two presents ago, alternating).
#  - diagnostics: NORD2 FB SYNC (tear detector, source rewritten during copy, GPU busy after copy, copy verify, long presents), FB TEAR,
#    FB RACE, FB LONG.
# a/ = q10jfe stage (0392's b/ + swapwait.h), b/ = patched; patched-sources-0393 = b/.  Host test: test/fbsync_host_test.c.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0393-nord2-fb-sync.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_swapwait.h"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0393; for f in $FILES; do mkdir -p patched-sources-0393/$(dirname $f); cp b/$f patched-sources-0393/$f; done
sha256sum "$OUT"
