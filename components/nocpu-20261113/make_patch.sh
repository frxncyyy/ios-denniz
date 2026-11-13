#!/bin/bash
# 0413 (nocpu-20261113): overlay composition is the only frame source.  kern.n2fbhw default 3; a frame the overlays cannot compose is HELD
# (last overlay frame stays in the panel GRAM, swap retired, held[reason] counted, "NORD2 FB HELD:"), never copied by the CPU; before the
# first push the boot (LK) buffer is pushed as is to latch the link ("NORD2 FB HOLD:"); display ownership taken from the early console
# before any frame (takeDisplay); no prepNext copy-ahead while frames are held; display off = DCS 0x28 / on = DCS 0x29 before the next push
# ("NORD2 FB BLANK:"), CPU fade only if DCS fails 30 times or the CPU path is in use.  The CPU copy path stays only for the debug modes 0/1
# (set by hand) and as the EMERGENCY path when the overlay is unusable for the boot (init refused, restore mismatch, 4 faults): kern.n2fbcpuemerg
# (default 1, 0 = hold even then), "NORD2 FB CPU EMERGENCY:".  presentCopy / 0390 blend code is unreachable otherwise (not deleted yet).
# Pure rules sweet_fb_hw_cpu_allowed / sweet_fb_blank_action in sweet_fb_hw.h, test/nocpu_host_test.c.  a/ = q10jgk + 0411 stage.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0413-nord2-fb-no-cpu-copy.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0413; for f in $FILES; do mkdir -p patched-sources-0413/$(dirname $f); cp b/$f patched-sources-0413/$f; done
sha256sum "$OUT"
