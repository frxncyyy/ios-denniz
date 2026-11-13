#!/bin/bash
# 0411 (iommuboot-20261113): display IOMMU default on: kern.n2fbiommu boot default 3 (boot-arg n2fbiommu=0..3), armed by a thread_call n2fbiommu_delay (20) s after the framebuffer start, stepwise-logged (each M4U0/larb step + 200 ms), fault status bits enabled with the GIC line masked (polled per HW frame, auto-disable), no lazy arming from the present thread while the boot arm is pending; kern.n2fbhw_pool default 1 (physical fallback when the IOMMU is refused).  a/ = q10jgk stage (0394..0401,0403..0410).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0411-nord2-fb-iommu-default-on.patch
FILES="iokit/Kernel/arm/sweet_fb_iommu.h iokit/Kernel/arm/sweet_fb_iommu_hw.h iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/SweetSurface.cpp"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0411; for f in $FILES; do mkdir -p patched-sources-0411/$(dirname $f); cp b/$f patched-sources-0411/$f; done
sha256sum "$OUT"
