#!/bin/bash
# 0293, on top of the q10hj series (.., 0292), bsd/dev/nord2_camdev.inc: each sensor's own analog gain code (IMX766 16384
# scale, IMX615 4096 - 1024/g, IMX355 1024 scale; all three had the IMX355's), IMX615 up to 64x, the first exposures of a
# session said with their code. a/ = the file of the q10hj stage, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0293-nord2-camera-gain-code-per-sensor.patch
{ diff -u a/bsd/dev/nord2_camdev.inc b/bsd/dev/nord2_camdev.inc || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
cmp "$T/bsd/dev/nord2_camdev.inc" b/bsd/dev/nord2_camdev.inc
sha256sum "$OUT"
