#!/bin/bash
# 0390 (fbblend-20261112), on top of the q10jm series (.., 0340): SweetFramebuffer composites the SwapEnd layers (premultiplied
# source-over in index order, sweet_fb_blend.h) instead of memcpy'ing them over each other. Run thumbcomp5: backboardd detached
# Photos' opaque drawable to its own layer and the transparent UI layer copied over it -> black grid. n2fbblend=0 = old copy.
# a/ = the q10jm stage file, b/ = patched; patched-sources-0390 = b/ (the build's source check).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0390-nord2-fb-layer-blend.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_blend.h"
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0390; for f in $FILES; do mkdir -p patched-sources-0390/$(dirname $f); cp b/$f patched-sources-0390/$f; done
sha256sum "$OUT"
