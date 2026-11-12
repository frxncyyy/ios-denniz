#!/bin/bash
# 0350 + 0351 (gnss-20261112), on top of the q10jm series (.., 0340; q10jp runs q10jm's kernel): real GNSS milestone 1.
# 0350 adds bsd/dev/nord2_gnss_pwr.inc (= gpu/hwcodec/gnss/gnss_pwr.{h,c}, host-tested) and bsd/dev/nord2_gpsdl.inc
# (= gpu/hwcodec/gnss/kern/bsd/dev/nord2_gpsdl.inc); 0351 binds them in sweet_touch.c (include, Wi-Fi owner predicate,
# page mapping, nord2_gpsdl_start()). a/ = the q10jm stage files, b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"; G=$H/../../hwcodec/gnss
# b/ copies of the host-tested sources must be current
{ echo '/* SPDX-License-Identifier: MIT */'; echo '/* nord2_gnss_pwr.inc -- patch 0350: gpu/hwcodec/gnss/gnss_pwr.h + gnss_pwr.c (host-tested by run_tests.sh), functions static. */'
  echo '#ifndef NORD2_GNSS_PWR_INC'; echo '#define NORD2_GNSS_PWR_INC 1'; echo '#define N2G_KERNEL 1'; echo '#define N2G_API static __attribute__((unused))'
  cat $G/gnss_pwr.h $G/gnss_pwr.c; echo '#endif'; } | cmp - b/bsd/dev/nord2_gnss_pwr.inc
cmp $G/kern/bsd/dev/nord2_gpsdl.inc b/bsd/dev/nord2_gpsdl.inc
P0=$H/0350-nord2-gnss-gpsdl-core.patch; P1=$H/0351-nord2-gnss-gpsdl-bind.patch
for f in bsd/dev/nord2_gnss_pwr.inc bsd/dev/nord2_gpsdl.inc; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P0
diff -u a/bsd/dev/sweet_touch.c b/bsd/dev/sweet_touch.c | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $P1 || true
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i $P0 && patch -s -p1 -i $P1)
for f in bsd/dev/nord2_gnss_pwr.inc bsd/dev/nord2_gpsdl.inc bsd/dev/sweet_touch.c; do cmp "$T/$f" "b/$f"; done
sha256sum $P0 $P1
