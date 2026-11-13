#!/bin/bash
# 0408 (iommuinit-20261113): overlay init (hwInit R11) accepts MMU_EN on an OVL*_2L port when the ARMED display IOMMU set it itself
# (kern.n2fbiommu >= 2 ports or a 0403 stage port): its table maps the panel buffers and the pool 1:1.  MMU_EN on a 4L port (p5), IOVA
# extension bits, or an MMU_EN the IOMMU code did not set are still refused (unchanged message).  Before 0408 a first hwInit inside a
# stage window, or a boot that arms the IOMMU before the first HW frame (final design: IOMMU always on), latched "init REFUSED" for the
# whole boot.  Pure check sweet_fb_iommu_init_ports_ok() in sweet_fb_iommu.h, test/iommuinit_host_test.c.
# a/ = base + 0394..0401, 0403, 0404-on0403, 0405, 0406, 0407-on0403 (no 0402), b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0408-nord2-fb-iommu-init-order.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_iommu.h iokit/Kernel/arm/sweet_fb_iommu_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0408; for f in $FILES; do mkdir -p patched-sources-0408/$(dirname $f); cp b/$f patched-sources-0408/$f; done
sha256sum "$OUT"
