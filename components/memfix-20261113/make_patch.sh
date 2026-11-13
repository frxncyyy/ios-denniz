#!/bin/bash
# 0417 (memfix-20261113): purge idle VOLATILE IOSurfaces under memory pressure (shared sink view, 0406 in-place remap, Empty answer,
#  wired again on NonVolatile), one budget for surfaces + GPU private (kern.n2mem_budget_mib 2240), per-process surface budget
#  (kern.n2surf_proc_mib 896, backboardd/SpringBoard exempt), "NORD2 MEM SURF:" every 10 s.  Files: SweetSurface.cpp/.h,
#  sweet_surface_purge.h (new, pure, host test test/purge_host_test.c), bsd/dev/nord2_gpu_transport.h (GPU side of the budget).
# a/ = q10jgn stage; the same patch applies with no offset/fuzz to q10jgm's stage (a-jgm/ SweetSurface.cpp differs only at 0416's
#  sweet_pool_enabled line, far from every hunk).  Outputs jgn/ and jgm/ (same patch, patched-sources-0417 per stage).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
NAME=0417-nord2-mem-surface-purge.patch; OUT=$H/$NAME
FILES="bsd/dev/nord2_gpu_transport.h iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/SweetSurface.h iokit/Kernel/arm/sweet_surface_purge.h"
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
for v in jgn jgm; do
  T=$(mktemp -d); cp -a a/. "$T/"; [ $v = jgm ] && cp a-jgm/iokit/Kernel/arm/SweetSurface.cpp "$T/iokit/Kernel/arm/SweetSurface.cpp"
  (cd "$T" && patch -p1 --fuzz=0 --batch -i "$OUT") > apply-$v.log
  ! grep -E 'offset|fuzz|FAILED|Reversed' apply-$v.log || { echo "$v: not clean"; exit 1; }
  [ $v = jgn ] && for f in $FILES; do cmp "$T/$f" "b/$f"; done
  rm -rf $v; mkdir -p $v; cp "$OUT" $v/
  for f in $FILES; do mkdir -p $v/patched-sources-0417/$(dirname $f); cp "$T/$f" $v/patched-sources-0417/$f; done
  rm -rf "$T"
done
rm "$OUT"
sha256sum jgn/$NAME jgm/$NAME
