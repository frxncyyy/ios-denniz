#!/bin/bash
# 0401 (iommu-20261112): display IOMMU (MTK M4U0, v7s tables) so the OVL layers read ANY IOSurface (scattered pages anywhere in DRAM)
# through 32-bit IOVAs; the 0394 pool stays as mode A (identity-mapped, so it shows the same with translation on or off).
#  New: iokit/Kernel/arm/sweet_fb_iommu.h (pure: v7s builder/walk, IOVA allocator, map cache policy, gate decision; host test
#       test/iommu_host_test.c) and sweet_fb_iommu_hw.h (kernel: probe, arm, map/unmap/TLB, OVL-port MMU_EN gate, fault poll,
#       sysctls kern.n2fbiommu 0..3, kern.n2fbiommu_probe, kern.n2fbiommutest), included once by SweetSurface.cpp.
#  SweetSurface.cpp: include + unmap in SweetSurface::free.  SweetFramebuffer.cpp: 5 one-line hunks marked "0401 IOMMU" (include,
#  hwInputs IOVA layer + mode-2 rule, hwWriteRegs gate, hwFrameEnd fault poll, hwTestPoll scattered slot).  sweet_fb_hw.h: the
#  planner accepts the IOVA window 0x80000000..0xA0000000 (1 line).
# a/ = the chain q10jfh stage + 0394..0400 (== device-mmu-q10jfn-candidate stage for these 3 files), b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0401-nord2-fb-display-iommu.patch
FILES="iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h iokit/Kernel/arm/sweet_fb_iommu.h iokit/Kernel/arm/sweet_fb_iommu_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0401; for f in $FILES; do mkdir -p patched-sources-0401/$(dirname $f); cp b/$f patched-sources-0401/$f; done
sha256sum "$OUT"
