#!/bin/bash
# 0403 (iommu-20261113b): staged display-IOMMU arming for a one-boot bring-up, on the 0401 chain (0394..0401, no 0402).
#  sweet_fb_iommu_hw.h only: sysctl kern.n2fbiommu_stage (1 arm without interrupt enable, logged step by step with a 200 ms console drain
#  before every M4U0 write; 2 MMU_EN on larb0 p6 = OVL1_2L only, identity-mapped panel buffers; 3 both OVL*_2L ports; 0 off) and
#  kern.n2fbiommu_wdms (default 2000): a thread_call watchdog armed BEFORE the port enable turns stage ports off again, independent of the
#  display thread.  Refused (EBUSY) while kern.n2fbiommu >= 2.  Payload: apps-20261104/payload-fbiommustage (stages=1,2,0,3 stagegap=20).
# a/ = patched-sources-0401 header, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0403-nord2-fb-iommu-staged-arming.patch
FILES="iokit/Kernel/arm/sweet_fb_iommu_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0403; for f in $FILES; do mkdir -p patched-sources-0403/$(dirname $f); cp b/$f patched-sources-0403/$f; done
sha256sum "$OUT"
