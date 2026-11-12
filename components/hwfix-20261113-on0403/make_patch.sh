#!/bin/bash
# 0404 on the 0403 stage (0394..0401+0403, no 0402): same content as hwfix-20261113/0404-nord2-fb-hw-test-poweroff.patch (sha 33db0c0d...),
# regenerated with zero fuzz/offset: kern.n2fbhwselftest default 0, fade waits while the test owns the panel, test end with display off ->
# CPU path on buffer 0, nothing pushed.  a/ = base+0394..0401+0403, b/ = a/ + the original 0404 hunks.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0404-nord2-fb-hw-test-poweroff-on0403.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0404; for f in $FILES; do mkdir -p patched-sources-0404/$(dirname $f); cp b/$f patched-sources-0404/$f; done
sha256sum "$OUT"
