#!/bin/bash
# 0363 (gnss-dl3-20261112), on top of 0362: split GNSS ladders after the gl9b reset: K=25..29 (ioctl 165..169) S10a..e, 2 s "about to"
# pauses before each risky action, RX DMA2 capped at 16 words and never re-armed, GIC enable/pending of the GPS INTIDs printed (sweet_touch.c
# n2gd_gicd_rd); INTEN only for K=23,24,28.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0363-nord2-gnss-split-ladders.patch; FILES="bsd/dev/nord2_gpsdl.inc bsd/dev/sweet_touch.c"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
