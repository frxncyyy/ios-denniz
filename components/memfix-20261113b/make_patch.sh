#!/bin/bash
# 0418 (memfix-20261113b): on 0417 -- limits scale with the RAM window (max_mem) and nothing is refused while memory is available.
#  r5b (q10jgr, 5088 MiB): Photos crashed on 0417's static refusals (combined 2240, per process 896 MiB) with 156,673 pages available.
#  Purge marks: available < 37 % of RAM pages, surfaces > 32.5 % RAM, surfaces + GPU > 59.3 % RAM.  Refusals only under the floor
#  (7.7 % of RAM pages, >= 8192) after purging, or over hard caps: surfaces 55 %, GPU private max(Nord2GPU limit, 40 %), one process 60 %.
#  a/ = memfix-20261113/jgn/patched-sources-0417 (= the q10jgr and q10jgp stages).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
NAME=0418-nord2-mem-ram-scaled-budgets.patch
FILES="bsd/dev/nord2_gpu_transport.h iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/sweet_surface_purge.h"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $NAME
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0418; for f in $FILES; do mkdir -p patched-sources-0418/$(dirname $f); cp b/$f patched-sources-0418/$f; done
sha256sum $NAME
