#!/bin/bash
# 0354 (gnss-safe2-20261112), on top of 0353 (gnss-safe-20261112): single conn_infra accesses as ioctls 111..115, through the
# Wi-Fi driver's own host-CSR mapping and its full read-only guard, with the Wi-Fi software state and a 2 s console pause.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0354-nord2-gnss-single-access.patch; FILES="bsd/dev/nord2_gpsdl.inc bsd/dev/sweet_touch.c"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
