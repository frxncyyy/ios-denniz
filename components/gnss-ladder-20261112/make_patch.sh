#!/bin/bash
# 0356 (gnss-ladder-20261112), on top of 0355: the GNSS power-on ladder (ioctls 141..155), step K = proven steps 1..K-1 + new
# step K, readback after every write, Wi-Fi guard before every access, automatic rollback (except 14 = hold).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0356-nord2-gnss-ladder.patch; FILES="bsd/dev/nord2_gpsdl.inc"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
