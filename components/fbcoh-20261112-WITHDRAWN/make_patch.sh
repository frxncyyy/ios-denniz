#!/bin/bash
# 0394 (fbcoh-20261112), on top of 0393 (fbsync-20261112): coherent CPU copy of the swap layers (sweet_fb_coherent.h).
# a/ = 0393 b/, b/ = patched; patched-sources-0394 = b/.  Host test: test/fbcoh_host_test.c.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0394-nord2-fb-coherent-copy.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_coherent.h"
for f in $FILES; do [ -e a/$f ] || { mkdir -p a/$(dirname $f); : ; }; diff -u --new-file a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d -p "$H"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0394; for f in $FILES; do mkdir -p patched-sources-0394/$(dirname $f); cp b/$f patched-sources-0394/$f; done
sha256sum "$OUT"
