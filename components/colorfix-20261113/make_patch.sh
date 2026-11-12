#!/bin/bash
# 0414 (colorfix-20261113): default layer-1 CON variant pm 0 (CFMT 2 + AEN = 0x000021ff, the LK-verified colour layout): with 0399's
# 0x028031ff the user still saw iOS blue as orange in overlay-composed Photos frames (both premultiplied values swap R/B on layer 1).
# kern.n2fbhwpm / kern.n2fbhwcsw unchanged; boot-args n2fbhwpm=0..3 / n2fbhwcsw=0..3 read once (hwInit / first hwFrameTry,
# "NORD2 FB HW COLOR:"); "CON written" line + layer count, surface ids, producer pid/name.  sweet_fb_hw_con_verified() in sweet_fb_hw.h.
# Host test test/hwcolor_host_test.c.  a/ = q10jgl stage (= patched-sources-0413).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0414-nord2-fb-hw-colour-pm0.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 --fuzz=0 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0414; for f in $FILES; do mkdir -p patched-sources-0414/$(dirname $f); cp b/$f patched-sources-0414/$f; done
sha256sum "$OUT"
