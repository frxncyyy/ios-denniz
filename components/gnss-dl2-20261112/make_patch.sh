#!/bin/bash
# 0362 (gnss-dl2-20261112), on top of 0359: vendor-order GNSS data link "V9" (ladder K=23 capture = ioctl 163, K=24 HOLD = ioctl 164):
# MCUB D2AF RESET_DONE acked (W1C), EMI_REQ_GPS 0x18001c1c[0] claimed, USRT INTEN TXIEN/NODAIEN, RX DMA2 armed on a 0xA5-filled
# ring BEFORE the TX of frame 1, 2 s capture with every STA/D2AF/DMA2 change logged + hex dump; RX-round NODAINTB fix (REGE ignored).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0362-nord2-gnss-datalink-v9.patch; FILES="bsd/dev/nord2_gpsdl.inc"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
