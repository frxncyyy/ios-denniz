#!/bin/bash
# 0407 (fpsmeter-20261114): measurement only, no change to composition or input behaviour.
#  - FB SEEN gets " hw_new= hw_same= hw_back= hw_unk=": overlay (mode 2/3) frames classified by a content clock (per planner slot:
#    surface id + nord2_gpu_surface_write_seq already read by hwFrameTry; GPU job ids are device-wide monotonic). noteHwGen feeds
#    the FB SEEN / FB CADENCE gap histograms from HW frames too (different/same stay CPU-copy hash only).
#  - NORD2 FB CLIENT: per creator pid of the shown surfaces, frames shown / fresh (its layer changed id or seq) and fresh/s.
#  - NORD2 INPUT RATE (sweet_touch.c): touch reports/s while a finger is down, report-gap histogram, I2C read time; 5 s windows
#    with activity only; no extra I2C transfer.
#  - sampled pixel hash for HW frames: NOT included (kernelAddress() maps lazily and the cached alias needs per-line maintenance:
#    neither cheap nor safe on the present thread).
#  - pure helpers sweet_fb_fpsmeter.h / nord2_input_rate.h, host test test/fpsmeter_host_test.c.
# a/ = device-mmu-q10jfh-candidate stage + 0394..0402(b),0404,0405 applied --fuzz=0 (= device-mmu-q10jgb-candidate stage; equal to
#      hwfix-20261113/patched-sources-0404 for SweetFramebuffer.cpp; sweet_touch.c untouched by 0394..0405); b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0407-nord2-fps-meter.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_fpsmeter.h bsd/dev/sweet_touch.c bsd/dev/nord2_input_rate.h"
for f in $FILES; do
  if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f || true; fi
done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*|/dev/null)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0407; for f in $FILES; do mkdir -p patched-sources-0407/$(dirname $f); cp b/$f patched-sources-0407/$f; done
(cd test && cc -std=c11 -Wall -Wextra -Werror -I../b/iokit/Kernel/arm -I../b/bsd/dev fpsmeter_host_test.c -o fpsmeter_host_test && ./fpsmeter_host_test > fpsmeter_host_test.out && cat fpsmeter_host_test.out)
sha256sum "$OUT"
