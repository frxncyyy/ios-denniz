#!/bin/bash
# bltorch-20261114/merge_check.sh: integ-20261114/merge.sh on a COPY (temp dir under this dir; integ-20261114/chain and p/ untouched) with the
# q10jci list appended to SRC: 0491 (wifi-fix), 0492 (ccperf), 0494 (gpuas r0494), 0495 0496 (bright-fix), 0500 (gpuas), 0501 0502 (this dir).
# Checks: merge.sh regenerates 0501 and 0502 byte-identical with the same patched-sources; the merged chain holds c/; no new io_submap call
# sites (ml_io_map / ml_io_map_wcomb) in SweetFramebuffer.cpp against the q10jch stage (a/).
set -euo pipefail
B=$NORD2_WORK/gpu/build
P=$(cd "$(dirname "$0")" && pwd); I=$B/integ-20261114; G=$B/gpuas-20261114
T=$(mktemp -d -p "$P" .mc.XXXXXX); trap 'rm -rf "$T"' EXIT
for x in src-0470 src-0483 src-0484 resolve; do ln -s $I/$x $T/$x; done
more="\"0491 $B/wifi-fix-20261114\" \"0492 $B/ccperf-20261114\" \"0494 $G/r0494\" \"0495 $B/bright-fix-20261114\" \"0496 $B/bright-fix-20261114\" \"0500 $G\" \"0501 $P\" \"0502 $P\""
sed -e "s#^H=\$B/integ-20261114; cd \$H#H=$T; cd \$H#" -e "s#\${MORE_SRC:-} )#$more )#" $I/merge.sh > $T/merge.sh
grep -q "0502 $P" $T/merge.sh && grep -q "^H=$T; cd" $T/merge.sh || { echo "merge.sh layout changed: edit failed"; exit 1; }
(cd $T && bash ./merge.sh) | grep -E '^049|^050|MERGE_DONE|CONFLICT'
for n in 0501 0502; do cmp $T/p/$n/$n-*.patch $P/$n-*.patch; diff -r $T/p/$n/patched-sources-$n $P/patched-sources-$n; done
for f in iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_backlight.h; do cmp $T/chain/s/$f $P/c/$f; done
echo "0501/0502 regenerated identical; merged chain == c/"
calls() { grep -v '^extern\|^ \*\|^/\*' "$1" | { grep -o 'ml_io_map\(_wcomb\)\?(' || true; } | wc -l; }
f=iokit/Kernel/arm/SweetFramebuffer.cpp; [ "$(calls $P/a/$f)" = "$(calls $T/chain/s/$f)" ] && echo "io_submap call sites in SweetFramebuffer.cpp: unchanged ($(calls $P/a/$f))"
echo MERGE_CHECK_OK
