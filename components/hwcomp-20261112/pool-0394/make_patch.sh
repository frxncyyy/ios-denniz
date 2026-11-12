#!/bin/bash
# 0394 (hwcomp-20261112/pool-0394, TRACK A of the HW display composition): display surface pool PA 0xC0000000..0xD0000000.
#  - sweet_fb_hw_pool.h (interface contract, master copy ../sweet_fb_hw_pool.h): pool base/size, sweet_fb_hw_pool_contains(),
#    sweet_surface_pool_pa/stride/size/cache(_obj)(), sweet_surface_create_pool().
#  - sweet_fb_hw_pool_alloc.h: pure allocator (64 KiB granule bitmap, first fit, eligibility, canaries); host test ../test/pool_host_test.c.
#  - SweetSurface.cpp: kern.n2fbhw_pool (boot-arg n2fbhw_pool, default 0) puts BGRA surfaces of >= 1000x2000 pixels (pitch % 64 == 0,
#    offset 0, single plane) into the pool as withPhysicalAddress descriptors; one kernel mapping of the pool (ml_io_map_unmappable);
#    all CPU mappings write-combined (n2fbpoolc=1: write-back); full pool -> normal IOBMD memory; range returned in SweetSurface::free().
# a/ = q10jfh stage (q10jfe + 0393), b/ = patched; patched-sources-0394 = b/.  No other track's file is touched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0394-nord2-fb-hw-pool.patch
FILES="iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/sweet_fb_hw_pool.h iokit/Kernel/arm/sweet_fb_hw_pool_alloc.h"
cmp ../sweet_fb_hw_pool.h b/iokit/Kernel/arm/sweet_fb_hw_pool.h || { echo "b/ header differs from the contract master ../sweet_fb_hw_pool.h"; exit 1; }
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d -p "$H" .chk.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0394; for f in $FILES; do mkdir -p patched-sources-0394/$(dirname $f); cp b/$f patched-sources-0394/$f; done
sha256sum "$OUT"
