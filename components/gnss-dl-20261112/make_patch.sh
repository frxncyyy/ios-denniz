#!/bin/bash
# 0359 (gnss-dl-20261112), on top of 0358: allow-list + USRT/DMA (the q10jr7 0xffffffff were allow-list refusals), step 15 USRT
# poll, 17 data link up (rings at 0xDFE00000), 18 send the first libmnl frame, 19 capture the DSP reply; 22 = HOLD + /dev/gpsdl0 read/write.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0359-nord2-gnss-datalink-ladder.patch; FILES="bsd/dev/nord2_gpsdl.inc bsd/dev/nord2_gnss_pwr.inc bsd/dev/sweet_touch.c"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
