#!/bin/bash
# 0405 (gov-20261113): CPU governor cluster-L floor while the UI is active, N2D_GOV_FLOOR_L_MHZ 1740u -> 2200u (an exact L row of
#  nord2_cpuhvfs_tables.h). Every other tunable unchanged.
# a/ = device-mmu-q10jfh-candidate/k/kernel/stage/bsd/dev/nord2_dvfs.h (= kernel-p1-20261026/patched-sources-0183, untouched by 0394..0404); b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0405-nord2-dvfs-floor-l-2200.patch
FILES="bsd/dev/nord2_dvfs.h"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0405; for f in $FILES; do mkdir -p patched-sources-0405/$(dirname $f); cp b/$f patched-sources-0405/$f; done
sha256sum "$OUT"
