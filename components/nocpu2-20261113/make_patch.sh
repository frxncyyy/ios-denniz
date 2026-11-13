#!/bin/bash
# 0416 (nocpu2-20261113): overlay + IOMMU only.  DELETED: presentCopy and its helpers, the prepNext copy-ahead, dropNext/ovlSelect/dblEnable
# (CPU double buffer), noteDistinct (CPU hash of the panel), poolAlias, snap (CPU screenshot; sweet_fb_snap now returns NULL), the CPU
# power-off fade, the n2scan probe and the H2 test patterns (CPU-drawn), kern.n2fbhwtest/n2fbhwselftest/n2fbhwhold/n2fbcpuemerg/
# n2fbpoolalias, modes 0/1 (kern.n2fbhw accepts 2..3), hwRestore (CPU-path layout), hwFault's mode-0 backoff, the 0390 blend code in
# sweet_fb_blend.h (geometry kept), the IOMMU test-slot CPU copy and its auto-disable.  NEW behaviour: non-composable frames HELD (bug
# counter), GPU fences waited for (never skipped, never forced; only a newer swap replaces a frame), overlay init refused -> retried with a
# logged backoff, IOMMU boot arm refused -> retried 5/10/20/40/60 s, M4U fault -> logged, cleared, TLB invalidated, PT_BASE re-written if
# lost (never disabled), display off/on = DCS 0x28/0x29 only (retried), panel memory never mapped for the CPU, kern.n2fbhw_pool default 0
# (every surface through the IOMMU), map cache 96.  b/ = mk0416.py applied to a/ (q10jgl stage + 0414 + 0415).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0416-nord2-fb-overlay-only.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h iokit/Kernel/arm/sweet_fb_blend.h iokit/Kernel/arm/sweet_fb_iommu.h iokit/Kernel/arm/sweet_fb_iommu_hw.h iokit/Kernel/arm/SweetSurface.cpp"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0416; for f in $FILES; do mkdir -p patched-sources-0416/$(dirname $f); cp b/$f patched-sources-0416/$f; done
sha256sum "$OUT"
