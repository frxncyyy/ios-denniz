#!/bin/bash
# 0290, on top of the q10hc series (.., 0289), bsd/dev/nord2_camdev.inc only:
#  F1 the main camera's lens: AK7377AF at i2c2 0x0c (registers 00-0f printed at power-on, active at stream start, position by
#     REDWOOD_CAMERA_FOCUS, standby at stop), frames' focus / focus_max;
#  F2 front camera: enqueues to the CAMSV queue spaced (never two at once while frames flow: the cause of "passed over" and the
#     fall from 60 to 30 frames a second), IMX615's shortest frame 5284 lines = 30 a second;
#  F3 analysis aid only: the DMA's 64-byte frame header printed for the first three frames of a session.
# a/ = the file of the q10hc stage, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0290-nord2-camera-focus-enqueue-spacing-front30.patch
{ diff -u a/bsd/dev/nord2_camdev.inc b/bsd/dev/nord2_camdev.inc || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
cmp "$T/bsd/dev/nord2_camdev.inc" b/bsd/dev/nord2_camdev.inc
sha256sum "$OUT"
