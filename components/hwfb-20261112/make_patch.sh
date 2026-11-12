#!/bin/bash
# 0396 (hwfb-20261112, Track C of the HW composition work): the display controller's overlay layers compose the screen.
#  - sweet_fb_hw.h (new, pure): planner (dual pipe OVL0_2L/OVL1_2L, 540 columns each, 32-bit physical addresses, pool/carve-out
#    windows), register lists (apply / restore-to-base with read-back masks), H2 test pattern + premultiplied/straight reference
#    strips, fault / race / fence policy.
#  - SweetFramebuffer.cpp: sysctl kern.n2fbhw (0 off, 1 dry run, 2 H3 single full-screen layer, 3 H4 two layers), kern.n2fbhwpm,
#    kern.n2fbhwtest (1..5 H2 patterns), kern.n2fbhwhold, kern.n2fbhwstat; per-frame plan with per-reason CPU fallback; GPU fence
#    (never shown with a GPU write in flight, never forced); auto-disable to 0 on OVL underflow / DSI underrun / frame-done
#    timeout / read-back mismatch / 16 consecutive GPU races; H0 surface property log; NORD2 FB HW lines.
#  Needs 0394 (sweet_fb_hw_pool_contains, sweet_surface_pool_pa) and 0395 (nord2_gpu_surface_write_seq / _write_pending) at link time.
# a/ = q10jfh stage (q10jfe + 0393), b/ = patched; patched-sources-0396 = b/.  Host test: test/hwfb_host_test.c.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0396-nord2-fb-hw-overlay-compose.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0396; for f in $FILES; do mkdir -p patched-sources-0396/$(dirname $f); cp b/$f patched-sources-0396/$f; done
sha256sum "$OUT"
