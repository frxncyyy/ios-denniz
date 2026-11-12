#!/bin/bash
# 0355 (gnss-safe3-20261112), on top of 0354 (gnss-safe2-20261112): GNSS pages mapped from kernel_map (ml_io_map_unmappable,
# Device attributes) instead of the 8 MiB io_submap, lazily; ioctls 121..130 = one mapping action per boot.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0355-nord2-gnss-map-kernel-map.patch; FILES="bsd/dev/nord2_gpsdl.inc bsd/dev/sweet_touch.c"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
