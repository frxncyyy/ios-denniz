#!/bin/bash
# 0407 on the 0403 stage (0394..0401+0403+0404-on0403+0405+0406, no 0402): fpsmeter-20261114/0407-nord2-fps-meter.patch rebased.
#  Same content except the two hunks whose context was 0402's: hunk 4 (fFps reset after the bzero(&fHwSt..) line) and hunk 8 (content
#  clock block in hwFrameTry after the plan log, before return 1; reads at most SWEET_FB_HW_SLOTS entries of fHwSeq, which has 2 entries
#  without 0402).  a/ = that stage, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0407-nord2-fps-meter-on0403.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_fpsmeter.h bsd/dev/sweet_touch.c bsd/dev/nord2_input_rate.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0407; for f in $FILES; do mkdir -p patched-sources-0407/$(dirname $f); cp b/$f patched-sources-0407/$f; done
sha256sum "$OUT"
