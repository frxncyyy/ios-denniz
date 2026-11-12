#!/bin/bash
# 0391 (fbdiag-20261112), on top of 0390 (fbblend-20261112): multi-layer presents clean+invalidate each layer's source rows before the
# CPU copy (n2fbinval=0 = as 0390) + pixel diagnostics (NORD2 FB PX: pre-acquire / acquired / civac / panel; NORD2 FB SWAPLOG).
# a/ = 0390's b/, b/ = patched; patched-sources-0391 = b/.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0391-nord2-fb-layer-inval-diag.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0391; for f in $FILES iokit/Kernel/arm/sweet_fb_blend.h; do mkdir -p patched-sources-0391/$(dirname $f); cp b/$f patched-sources-0391/$f; done
sha256sum "$OUT"
