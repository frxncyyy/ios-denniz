#!/bin/bash
# 0292, on top of the q10hi series (.., 0291): two camera instances at once. /dev/redwood-camera (SENINF mux1 -> cam mux 3 ->
# CAMSV0, as before) and /dev/redwood-camera2 (mux2 -> cam mux 5 -> CAMSV2), each with its session thread, receiver context,
# buffers (4 x 16 MiB: 0xD0000000 and 0xD4000000), exposure and stall handling; I2C, power and SENINF's shared registers
# under one lock; sparse colour means in the frame header. ABI: gpu/build/camera-userland-20261109/KERNEL_ABI_DUAL.md.
# a/ = the files of the q10hi stage, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0292-nord2-camera-two-instances.patch
FILES="bsd/dev/nord2_seninf.inc bsd/dev/nord2_camsv.inc bsd/dev/nord2_camdev.inc"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum "$OUT"
