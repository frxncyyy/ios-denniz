#!/bin/bash
# 0470: display IOMMU boot arming at 3 s (kern.n2fbiommu_delay), readiness wait, refusal retry keeps the boot mode.
# a/ = chain after 0462 (0470/a), b/ = chain/s (edited in place after merge.sh).
set -euo pipefail; cd "$(dirname "$0")"
f=iokit/Kernel/arm/sweet_fb_iommu_hw.h; out=0470/0470-nord2-fb-iommu-early-arm.patch
diff -u 0470/a/$f chain/s/$f | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" > $out || true
T=$(mktemp -d); mkdir -p $T/$(dirname $f); cp 0470/a/$f $T/$f
(cd $T && patch -p1 --fuzz=0 --batch -i $OLDPWD/$out) > 0470/apply.log; ! grep -E 'offset|fuzz|FAILED|Reversed' 0470/apply.log
cmp $T/$f chain/s/$f; rm -rf 0470/patched-sources-0470 $T; mkdir -p 0470/patched-sources-0470/$(dirname $f); cp chain/s/$f 0470/patched-sources-0470/$f
sha256sum $out
