#!/bin/bash
# 0294, on top of the q10hn series (.., 0293), bsd/dev/nord2_camdev.inc: the two instances no longer starve each other
# (fair hand-over of the hardware lock, one I2C transaction per hold, exposure written one changed register per turn of the
# session loop, no "stall" called by a thread that was itself kept waiting), and the frames' time is the hardware's 13 MHz
# stamp of the frame's start. a/ = the file of the q10hn stage, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0294-nord2-camera-fair-lock-hw-time.patch
{ diff -u a/bsd/dev/nord2_camdev.inc b/bsd/dev/nord2_camdev.inc || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
cmp "$T/bsd/dev/nord2_camdev.inc" b/bsd/dev/nord2_camdev.inc
sha256sum "$OUT"
