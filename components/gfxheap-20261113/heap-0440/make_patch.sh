#!/bin/bash
# 0440 (gfxheap-20261113/heap-0440): the low-bank GRAPHICS HEAP (Step B of the memory study): displayed full-screen BGRA IOSurfaces (pool eligibility +
#  admission) out of 9 low-bank regions (6 SAFE on by default, 3 CHECK behind kern.n2heap_check=1) instead of XNU pages.  One 64 KiB-granule bitmap,
#  first fit restarting at each region, each region mapped once write-combined (ml_io_map_unmappable, never io_submap), canary at setup / alloc /
#  30 s scrub, per-region switch-off; not in the 0417/0418 surface budgets; sysctls kern.n2heap* (no boot-arg).  "NORD2 HEAP:" lines.
#  Files: SweetSurface.cpp (include, free/kernelAddress/create/start/report), sweet_fb_iommu_hw.h (n2io_ram accepts heap pages; M4U invalid-PA fault
#  switches the region off), sweet_gfx_heap.h (new, pure, host test test/gfx_heap_host_test.c), sweet_gfx_heap_kern.inc (new).
# a/ = q10jgs stage (device-mmu-q10jgs-candidate/k/kernel/stage).  Checks: host test (+ASan/UBSan), clean apply (fuzz 0) on a/, result == b/.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
NAME=0440-nord2-gfx-heap.patch
FILES="iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/sweet_fb_iommu_hw.h iokit/Kernel/arm/sweet_gfx_heap.h iokit/Kernel/arm/sweet_gfx_heap_kern.inc"
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror -o test/t.bin test/gfx_heap_host_test.c && ./test/t.bin > test/gfx_heap_host_test.out && rm test/t.bin && tail -1 test/gfx_heap_host_test.out
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$NAME"
T=$(mktemp -d -p "$H"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"
(cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log || { echo "not clean"; exit 1; }
rm -rf patched-sources-0440
for f in $FILES; do cmp "$T/$f" "b/$f"; mkdir -p patched-sources-0440/$(dirname $f); cp "$T/$f" patched-sources-0440/$f; done
sha256sum "$NAME"
