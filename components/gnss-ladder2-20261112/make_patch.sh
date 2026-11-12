#!/bin/bash
# 0357 (gnss-ladder2-20261112), on top of 0356: ladder steps 13 (vendor DSP pwr_stat ON) .. 16: GPS RGU_ON L1 power + reset release,
# USRT on + DSP ROM okay; 17 = 5 s powered + rollback, 18 = hold. Adds the GPS RGU_ON page 0x18c00000 (kernel_map, lazy).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0357-nord2-gnss-ladder-l1.patch; FILES="bsd/dev/nord2_gpsdl.inc bsd/dev/nord2_gnss_pwr.inc bsd/dev/sweet_touch.c"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
