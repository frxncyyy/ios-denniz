#!/bin/bash
# 0404 (hwfix-20261113): H2 test pattern vs display power-off (the "frozen display" after the 0399 colour self-test, night12/night10).
#  - kern.n2fbhwselftest default 0 (was 1).
#  - the power-off fade (vsync thread) waits while a test owns the panel (fPState 7): it no longer draws into buffer 0 and pushes
#    under the test; it starts from its first step after the test end.
#  - a test that ends with the display off: CPU path restored onto buffer 0 (ovlSelect(0), fFront = 0), no HW re-apply, nothing
#    pushed (before: the front buffer was pushed: buffer 1 = a stale UI frame lit on an off display, buffer 0 = the fade's black).
#  - pure helpers in sweet_fb_hw.h (sweet_fb_hw_fade_may_run, sweet_fb_hw_test_end_action, sweet_fb_hw_front_base), host test
#    test/hwfix_host_test.c (both buffer indices; the old rules reproduce the device symptoms).
# a/ = hwmulti-20261113-on0401b/patched-sources-0402 (0402b) = the q10jfw stage; b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0404-nord2-fb-hw-test-poweroff.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0404; for f in $FILES; do mkdir -p patched-sources-0404/$(dirname $f); cp b/$f patched-sources-0404/$f; done
sha256sum "$OUT"
