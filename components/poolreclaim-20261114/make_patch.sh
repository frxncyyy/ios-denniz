#!/bin/bash
# 0406 (poolreclaim-20261114): display pool (0394) reclaim of idle VOLATILE pool surfaces when the pool is full (night18: 23 surfaces /
#  247 of 256 MiB held for an hour by backboardd and MobileSlideShow client mappings, ~4% CPU frames with reason 'memory').
#  - sweet_fb_pool_reclaim.h (new, pure): eviction verdict (pool, not kernel-made, n2surfuse 2, no display IOMMU, older than
#    kern.n2fbhw_pool_age, not swapped within it, purgeable VOLATILE, use counts 0/0, no display / GPU job hold, no GPU import, GPU not
#    busy), LRU victim pick against a copy of the 0394 bitmap (pruned to a minimal set, none if no fit is possible), grace rule.
#  - SweetSurface.cpp: kern.n2fbhw_pool_reclaim 0 off / 1 log only (DEFAULT) / 2 evict; kern.n2fbhw_pool_reclaim_grace (2 s);
#    eviction = new zeroed IOBMD behind the same user addresses (createMappingInTask kIOMapOverwrite per client mapping), purgeable
#    state Empty, old descriptor + pool range returned only after the grace when no hold/import/swap/mapping of it is left;
#    addRef maps a retained snapshot of s->memory (retry if swapped); SetPurgeable takes the eviction lock; leak report rows show
#    purgeable state and the verdict.  Host test test/reclaim_host_test.c.
# a/ = iommu-20261112/patched-sources-0401 SweetSurface.cpp (= q10jgb stage: untouched by 0402, 0404, 0405); b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0406-nord2-fb-pool-reclaim.patch
FILES="iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/sweet_fb_pool_reclaim.h"
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0406; for f in $FILES; do mkdir -p patched-sources-0406/$(dirname $f); cp b/$f patched-sources-0406/$f; done
sha256sum "$OUT"
