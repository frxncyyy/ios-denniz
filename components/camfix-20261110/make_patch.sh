#!/bin/bash
# 0289, on top of the q10h8 series (.., 0288):
#  K1 camera capture: 0x2c = 0x11 at enqueue as the stock HAL, two buffers kept in the hardware's queue (four buffers), a stall is
#     reported (CAMSV + INT_STATUS + receiver) and answered by starting CAMSV again with the sensor left streaming, no permanent
#     give-up after five failed sessions, the session's own frame count;
#  K2 read: format 4 (the DMA buffer as it is), copied out 1 MiB at a time;
#  K3 SweetFramebuffer: DisplayPipePlaneBaseAlignment / DisplayPipeStrideRequirements as dictionaries.
# a/ = the files of the q10h8 stage, b/ = patched; patched-sources-0289/ = what build_p1_candidate.py checks (SweetFramebuffer.cpp).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0289-nord2-camera-stall-format4-fb-alignment.patch
FILES="bsd/dev/nord2_camsv.inc bsd/dev/nord2_camdev.inc iokit/Kernel/arm/SweetFramebuffer.cpp"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
mkdir -p patched-sources-0289/iokit/Kernel/arm; cp b/iokit/Kernel/arm/SweetFramebuffer.cpp patched-sources-0289/iokit/Kernel/arm/
sha256sum "$OUT"
