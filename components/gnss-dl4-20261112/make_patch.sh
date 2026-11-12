#!/bin/bash
# 0364 (gnss-dl4-20261112), on top of 0363: S11 = the libmnl DSP code download inside one ladder ioctl: K=30 (ioctl 170) boot + rollback,
# K=31 (ioctl 171) boot + HOLD (GPS wake vote held, INTEN off, RX 256 B per arming). Frames uploaded by user space with
# N2GD_IOC_CHUNK (_IOW 'G' 180, 4 KiB chunks into BSS), validated (sum, markers, ids, fragment seq) before any access.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0364-nord2-gnss-dsp-boot.patch; FILES="bsd/dev/nord2_gpsdl.inc"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
