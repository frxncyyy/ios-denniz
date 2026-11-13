#!/bin/bash
# 0409 (iommusub-20261113): sysctl kern.n2fbiommu_sub = the 0403 stage 1 split into sub-steps to bisect the T1 lockup (iot1):
#  0x01 prep (pool map + tables, no M4U write), 0x02 PT_BASE, 0x04 TFRP, 0x08 TLB invalidate all, 0x10 read-only dump of the other M4U0
#  larbs' ports (only powered domains), 0x80 undo.  sweet_fb_iommu_hw.h only; unset = nothing runs (0401/0403 unchanged).
#  Payload token sub=... in the n2fbhwctl fork iommusub-20261113/fbiommuctl.  Host test: test/iommusub_host_test.c.
# a/ = q10jgi stage (0394..0401,0403,0404-on0403,0405,0406,0407-on0403,0408), b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0409-nord2-fb-iommu-substeps.patch
FILES="iokit/Kernel/arm/sweet_fb_iommu_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0409; for f in $FILES; do mkdir -p patched-sources-0409/$(dirname $f); cp b/$f patched-sources-0409/$f; done
sha256sum "$OUT"
