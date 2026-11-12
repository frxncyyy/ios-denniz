#!/bin/bash
# 0399 (hwcolor-20261112-on0398): the same change as hwcolor-20261112/0399, rebased to apply exactly (no offset/fuzz) after 0398-on0397.
#  - Layer 1 (premultiplied) CON 0x038031ff (the MTK DRM table's ARGB8888-premultiplied: BYTE_SWAP set) showed R and B swapped on the
#    phone (Photos, mode 3); layer 0 (0x20ff) was right.  kern.n2fbhwpm 2 is now 0x028031ff (BYTE_SWAP clear); the old value is pm 3.
#  - kern.n2fbhwcsw (0..3): flip BYTE_SWAP on layer 1 (bit 0) / layer 0 (bit 1) at run time; every CON written is logged
#    ("NORD2 FB HW: CON written ..."), and is in kern.n2fbhwstat (csw, con_l0, con_l1).
#  - kern.n2fbhwtest=6 colour test: R|G|B strip by layer 0 (rows 620-699) and the same by layer 1 (rows 710-789), x 390-689 (crosses
#    the seam); 5 = 1,2,3,4,6.  kern.n2fbhwselftest (default 1): the first switch to mode 2/3 of a boot runs test 6 first.
#  - sweet_fb_hw.h: CON bit names, sweet_fb_hw_con_decode() = the colour-order model; host test decodes through it.
#  Overlap with 0398 (usecount-20261112-on0397): none (0398 changes sweet_fb_retain/release_surface only); both orders apply.
# a/ = 0398-on0397 patched SweetFramebuffer.cpp + 0396 sweet_fb_hw.h, b/ = patched.  Host test: test/hwcolor_host_test.c.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0399-nord2-fb-hw-colour-order.patch
FILES="iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_hw.h"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0399; for f in $FILES; do mkdir -p patched-sources-0399/$(dirname $f); cp b/$f patched-sources-0399/$f; done
sha256sum "$OUT"
