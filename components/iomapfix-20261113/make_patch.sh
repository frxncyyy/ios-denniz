#!/bin/bash
# 0410 (iomapfix-20261113): io_submap exhaustion fix (Fable RCA: every T1/sub hang at ~108 s uptime = the SCP thread's delayed ml_io_map
# calls, only in boots where 0401's n2io_map_regs had taken 5 permanent io_submap maps).  sweet_fb_iommu_hw.h: all IOMMU-side maps once
# via ml_io_map_unmappable (kernel_map) + "NORD2 FB IOMAP:" line + 200 ms; SPM/larb maps of the 0x10 dump cached; the 0x10 check runs
# automatically before every PT_BASE write (arm and sub 0x02) and refuses if a powered M4U0 larb (5/7/11/14/17/19/20) has an MMU_EN port.
# SweetFramebuffer.cpp: hwInit larb0/1 maps once via ml_io_map_unmappable.  nord2_scp.inc: each SCP map logged before/after (+200 ms).
# a/ = q10jgj stage (0394..0401,0403..0409), b/ = patched.  Host test: test/iomapfix_host_test.c (pure helpers).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0410-nord2-iomap-kernel-map.patch
FILES="iokit/Kernel/arm/sweet_fb_iommu_hw.h iokit/Kernel/arm/SweetFramebuffer.cpp bsd/dev/nord2_scp.inc"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0410; for f in $FILES; do mkdir -p patched-sources-0410/$(dirname $f); cp b/$f patched-sources-0410/$f; done
sha256sum "$OUT"
