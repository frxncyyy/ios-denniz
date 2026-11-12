#!/bin/bash
# 0392 (fbretain-20261112), on top of 0391 (fbdiag-20261112): a SwapEnd record updates only the layers in its mask; the others keep
# their surface (sweet_fb_scene.h). Run fbd1: after the detach, backboardd's UI-only swaps (mask 0x2, ids 0,3) made the framebuffer
# show its transparent frame alone. n2fbretain=0 = as 0391. a/ = 0391's b/, b/ = patched; patched-sources-0392 = b/.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0392-nord2-fb-swap-mask-retain.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_scene.h"
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0392; for f in $FILES iokit/Kernel/arm/sweet_fb_blend.h; do mkdir -p patched-sources-0392/$(dirname $f); cp b/$f patched-sources-0392/$f; done
sha256sum "$OUT"
