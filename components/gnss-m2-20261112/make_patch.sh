#!/bin/bash
# 0352 (gnss-m2-20261112), on top of 0350/0351 (gnss-20261112): GNSS milestone 2, the data link (read/write byte pipe).
# Adds bsd/dev/nord2_gnss_dl.inc (= gpu/hwcodec/gnss/gnss_dl.{h,c}), updates nord2_gnss_pwr.inc (allow-list + DMA/USRT,
# N2G_E_TIMEOUT), nord2_gpsdl.inc (rings, USRT on, worker thread, read/write) and sweet_touch.c (ring window 0xDFE00000,
# 2 more pages). a/ = the files after 0350/0351, b/ = patched. Refuses if b/ differs from the host-tested sources.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"; G=$H/../../hwcodec/gnss
mkinc() { { echo '/* SPDX-License-Identifier: MIT */'; echo "/* $1 -- $2 (host-tested by gpu/hwcodec/gnss/run_tests.sh), functions static. */"; echo "#ifndef $3"; echo "#define $3 1"; echo '#define N2G_KERNEL 1'; echo '#define N2G_API static __attribute__((unused))'; shift 3; cat "$@"; echo '#endif'; }; }
mkinc nord2_gnss_pwr.inc "patch 0350: gpu/hwcodec/gnss/gnss_pwr.h + gnss_pwr.c" NORD2_GNSS_PWR_INC $G/gnss_pwr.h $G/gnss_pwr.c | cmp - b/bsd/dev/nord2_gnss_pwr.inc
mkinc nord2_gnss_dl.inc "patch 0352: gpu/hwcodec/gnss/gnss_dl.h + gnss_dl.c" NORD2_GNSS_DL_INC $G/gnss_dl.h $G/gnss_dl.c | sed 's|#include "gnss_pwr.h"|/* gnss_pwr.h: in nord2_gnss_pwr.inc */|' | cmp - b/bsd/dev/nord2_gnss_dl.inc
cmp $G/kern/bsd/dev/nord2_gpsdl.inc b/bsd/dev/nord2_gpsdl.inc
P=$H/0352-nord2-gnss-datalink.patch; FILES="bsd/dev/nord2_gnss_dl.inc bsd/dev/nord2_gnss_pwr.inc bsd/dev/nord2_gpsdl.inc bsd/dev/sweet_touch.c"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P)
for f in $FILES; do cmp "$T/$f" "b/$f"; done
sha256sum $P
