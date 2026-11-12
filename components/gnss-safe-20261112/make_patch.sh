#!/bin/bash
# 0353 (gnss-safe-20261112), on top of 0350/0351 (gnss-20261112, frozen): after the q10jr reset, open() no longer powers the
# GNSS block; the power path runs as single ioctl steps 101..104 guarded by the Wi-Fi driver's always-on predicates, with
# console markers and 300 ms pauses before each risky access. a/ = files after 0350/0351, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
P=$H/0353-nord2-gnss-safe-steps.patch; FILES="bsd/dev/nord2_gpsdl.inc bsd/dev/sweet_touch.c"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
