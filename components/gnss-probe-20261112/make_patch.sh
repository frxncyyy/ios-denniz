#!/bin/bash
# 0358 (gnss-probe-20261112), on top of 0357: ladder K=19 (ioctl 159): steps 1..15, then a READ-ONLY probe of conn_infra GPS
# config and the GPS subsystem pages (USRT poll timeline, RGU_ON/CFG_ON/AON/BGF/DMA/adrdec, 0x18c70000..0x18c7f000), then rollback.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0358-nord2-gnss-l1-probe.patch; FILES="bsd/dev/nord2_gpsdl.inc"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
