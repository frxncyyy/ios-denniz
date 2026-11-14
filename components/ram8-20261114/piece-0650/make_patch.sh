#!/bin/bash
# 0650 (ram8-20261114): graphics heap +4 regions (dph = display pool upper 240 MiB lent while the pool is off, top = 32 MiB above XNU's
#  window, lb4/lb0 = LK/loader boot buffers dead after boot), cap 2048 MiB, one new region per heap tick with a log line + 1 s pause before
#  the first touch, loader-address check (/chosen nord2-stock-entry/-dtb), "NORD2 RAM8:" plan lines.  Files: SweetSurface.cpp (pool lending,
#  pool canary/bitmap limited to the kept 16 MiB, sweet_pool_surface_pa excludes the lent part), sweet_gfx_heap.h, sweet_gfx_heap_kern.inc.
# a/ = q10jfinal1 stage.  Checks: host tests (test/run.sh), clean apply (fuzz 0) on a/, result == b/.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"; N=${N:-0650}; NAME=${NAME:-0650-nord2-ram8-heap-regions.patch}
FILES="iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/sweet_gfx_heap.h iokit/Kernel/arm/sweet_gfx_heap_kern.inc"
bash ../test/run.sh
: > "$NAME"
for f in $FILES; do cmp -s a/$f b/$f && continue; diff -u a/$f b/$f | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' >> "$NAME" || true; done
T=$(mktemp -d -p "$H"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"
(cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log || { echo "not clean"; exit 1; }
rm -rf patched-sources-$N
for f in $(grep '^+++ ' "$NAME" | awk '{print $2}' | sed 's#^b/##'); do cmp "$T/$f" "b/$f"; mkdir -p patched-sources-$N/$(dirname $f); cp "$T/$f" patched-sources-$N/$f; done
sha256sum "$NAME"
