#!/bin/bash
# 0490 (yuvovl-20261113/0490-yuv422-ovl): packed 4:2:2 YCbCr IOSurfaces ('2vuy' = UYVY, 'yuvs' / 'yuvf' = YUYV) as overlay layers through the
#  OVL's own YUV input (L_CON CFMT 4 / 5 + MTX 4 / 6 / 7, Ln_CLIP for odd columns), behind sysctl kern.n2fbyuv (default 0 = OFF: such surfaces
#  are refused by the planner with "format" exactly as before; nothing has run on the phone).  kern.n2fbyuvstat = counters.  4:2:0 two-plane
#  surfaces (420v / 420f) are no OVL input format and stay refused (counted as seen_biplanar).  "NORD2 FB YUV:" lines.
#  Files: SweetFramebuffer.cpp (3 inserted lines: include, hwInputs, hwFrameTry), sweet_fb_hw.h (planner: 2 bytes per pixel + CLIP for a layer
#  whose format_ok > 1), sweet_fb_yuv.h (new, pure, host test test/yuv_host_test.c), sweet_fb_yuv_kern.inc (new: sysctls, counters, log).
# a/ = q10jgs stage (device-mmu-q10jgs-candidate/k/kernel/stage).  Checks: host test (+ASan/UBSan) on b/, BGRA-only digest equal on a/ and b/,
#  clean apply (fuzz 0) on a/, result == b/.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
NAME=0490-nord2-fb-yuv422-overlay.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h iokit/Kernel/arm/sweet_fb_yuv.h iokit/Kernel/arm/sweet_fb_yuv_kern.inc"
CF="-std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror"
cc $CF -I b/iokit/Kernel/arm -o test/t490.bin test/yuv_host_test.c && ./test/t490.bin > test/yuv_host_test.out
cc $CF -I a/iokit/Kernel/arm -DYUV_BASE -o test/t490.bin test/yuv_host_test.c && ./test/t490.bin > test/yuv_host_test.base.out && rm test/t490.bin
grep -q '^PASS: ' test/yuv_host_test.out
[ "$(grep '^BGRA_DIGEST' test/yuv_host_test.out)" = "$(grep '^BGRA_DIGEST' test/yuv_host_test.base.out)" ] || { echo "BGRA plans changed"; exit 1; }
grep -E '^(PASS|BGRA_DIGEST)' test/yuv_host_test.out
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$NAME"
T=$(mktemp -d -p "$H"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"
(cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log || { echo "not clean"; exit 1; }
rm -rf patched-sources-0490
for f in $FILES; do cmp "$T/$f" "b/$f"; mkdir -p patched-sources-0490/$(dirname $f); cp "$T/$f" patched-sources-0490/$f; done
sha256sum "$NAME"
