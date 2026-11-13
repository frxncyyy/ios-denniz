#!/bin/bash
# 0480 (memwod-20261113): WIRE-ON-DEMAND IOSurfaces, default OFF (kern.n2surf_wod 0/1).  Design, rules, invariants: b/iokit/Kernel/arm/sweet_surface_wod.h.
#  new: sweet_surface_wod.h (pure state machine, host-tested), sweet_surface_wod_kern.inc (SweetSurface.cpp: holds, creation, tick, sysctls),
#       sweet_fb_iommu_wod.inc (display IOMMU: entry holds, idle unmap, page-wise clean), nord2_gpu_wod.inc (GPU import under a hold)
#  hooks (one or two lines each): SweetSurface.h (the wod field + 4 prototypes), SweetSurface.cpp (include, free, purge scan skip, create branch,
#       SetPurgeable), SweetFramebuffer.cpp (swap retain/release = hold/unhold, H0 log guard), sweet_fb_iommu_hw.h (unmap hook, include,
#       map + clean calls), Nord2GPU.cpp (free_owner unhold, include, importSurface hand-over)
#  a/ = the q10jgs stage (device-mmu-q10jgs-candidate/k/kernel/stage = chain 0394..0418).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
N=0480; NAME=$N-nord2-surface-wire-on-demand.patch
FILES="iokit/Kernel/arm/Nord2GPU.cpp iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/SweetSurface.h iokit/Kernel/arm/sweet_fb_iommu_hw.h iokit/Kernel/arm/nord2_gpu_wod.inc iokit/Kernel/arm/sweet_fb_iommu_wod.inc iokit/Kernel/arm/sweet_surface_wod.h iokit/Kernel/arm/sweet_surface_wod_kern.inc"
for f in $FILES; do
  if [ -f a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f || true; fi
done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $NAME
T=$(mktemp -d "$H/.apply.XXXX"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-$N; for f in $FILES; do mkdir -p patched-sources-$N/$(dirname $f); cp b/$f patched-sources-$N/$f; done
cc -std=c11 -O2 -Wall -Wextra -Werror -o test/wod_host_test test/wod_host_test.c && ./test/wod_host_test > test/wod_host_test.out
tail -1 test/wod_host_test.out; grep -c '^@@' $NAME; sha256sum $NAME
