#!/bin/bash
# 0340 (cam60-20261112), on top of the q10jk series (.., 0331): the main camera's sensor modes. REDWOOD_CAMERA_MODE (5) a = 0 the 4096x3072
# 30/s mode (default, as before), 1 hs_video 2048x1152 at 60/s (the vendor's 120/s mode with frames twice as long), 2 custom2 4096x2304 60/s;
# tables from the OnePlus GPL driver the existing IMX766 tables came from (make_tables.py checks that). a/ = the q10jk stage files, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0340-nord2-camera-sensor-modes-60.patch
FILES="bsd/dev/nord2_cam_tables.h bsd/dev/nord2_camdev.inc"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum "$OUT"
