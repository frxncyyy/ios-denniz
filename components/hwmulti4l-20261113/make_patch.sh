#!/bin/bash
# 0415 (hwmulti4l-20261113): the 4-layer cascade = 0402c (hwmulti-20261113-on0401c) rebased on q10jgl + 0414, defaults ON:
#  kern.n2fbhwlayers 6 (OVL*_2L 2 + cascaded 4L OVL* 4 layers per pipe), kern.n2fbh6 1 (lock-free 0402c H6 fence only: no Nord2GPU lock
#  in SwapEnd / present), kern.n2fbscaling 0 (the OVL has no scaler: layers are shown 1:1).  4L register blocks mapped once with
#  ml_io_map_unmappable (kernel_map, the 0410 rule; "NORD2 FB IOMAP: 4L OVL.." + 200 ms).  IOMMU on the 4L ports: larb0/1 p5 switch with
#  p6 (sweet_fb_iommu_hw.h n2io_ports), 4L register lists gated too (sweet_fb_iommu_gate4 / gate_list4, prog = 6 slots per pipe),
#  owned ports + hwInit R11 accept p5 set by the armed IOMMU, the planner's "4L slots are physical-only" W_MEMORY refusal removed.
#  0402c hunks 3/7/26 re-done by hand (context changed by 0404/0407/0413/0414); the rest applied as is.
# a/ = q10jgl stage + 0414 (colorfix-20261113), b/ = patched.  Tests: test/ (hwmulti, iommuinit4l, hwcolor4l).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0415-nord2-fb-hw-cascade-4l.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h iokit/Kernel/arm/Nord2GPU.cpp iokit/Kernel/arm/sweet_fb_iommu.h iokit/Kernel/arm/sweet_fb_iommu_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0415; for f in $FILES; do mkdir -p patched-sources-0415/$(dirname $f); cp b/$f patched-sources-0415/$f; done
sha256sum "$OUT"
