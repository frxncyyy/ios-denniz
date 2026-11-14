#!/bin/bash
# rtc-20261114/merge_check.sh: integ-20261114/merge.sh on a COPY (temp dir under this dir; integ-20261114/chain and p/ untouched) with the
# q10jcn list appended to SRC: 0491 (wifi-fix), 0492 (ccperf), 0494 (gpuas r0494), 0495 0496 (bright-fix), 0500 (gpuas), 0501 0502 (bltorch),
# 0530 0531 (heat), 0540 (this dir).  Checks: merge.sh regenerates 0540 byte-identical with the same patched-sources; the merged chain
# holds b/ for every file 0540 touches; no new io_submap call sites (ml_io_map*) in sweet_touch.c against the q10jcm stage (a/).
set -euo pipefail
B=$NORD2_WORK/gpu/build
P=$(cd "$(dirname "$0")" && pwd); I=$B/integ-20261114; G=$B/gpuas-20261114
T=$(mktemp -d -p "$P" .mc.XXXXXX); trap 'rm -rf "$T"' EXIT
for x in src-0470 src-0483 src-0484 resolve; do ln -s $I/$x $T/$x; done
more="\"0491 $B/wifi-fix-20261114\" \"0492 $B/ccperf-20261114\" \"0494 $G/r0494\" \"0495 $B/bright-fix-20261114\" \"0496 $B/bright-fix-20261114\" \"0500 $G\" \"0501 $B/bltorch-20261114\" \"0502 $B/bltorch-20261114\" \"0530 $B/heat-20261114\" \"0531 $B/heat-20261114\" \"0540 $P\""
sed -e "s#^H=\$B/integ-20261114; cd \$H#H=$T; cd \$H#" -e "s#\${MORE_SRC:-} )#$more )#" $I/merge.sh > $T/merge.sh
grep -q "0540 $P" $T/merge.sh && grep -q "^H=$T; cd" $T/merge.sh || { echo "merge.sh layout changed: edit failed"; exit 1; }
(cd $T && bash ./merge.sh) | grep -E '^049|^05|MERGE_DONE|CONFLICT'
cmp $T/p/0540/0540-*.patch $P/0540-*.patch; diff -r $T/p/0540/patched-sources-0540 $P/patched-sources-0540
for f in bsd/dev/nord2_rtc.h bsd/dev/nord2_rtc.inc bsd/dev/sweet_touch.c bsd/kern/nord2_clockfloor.h bsd/kern/kern_exec.c; do cmp $T/chain/s/$f $P/b/$f; done
echo "0540 regenerated identical; merged chain == b/"
calls() { grep -v '^extern\|^ \*\|^/\*' "$1" | { grep -o 'ml_io_map[_a-z]*(' || true; } | wc -l; }
f=bsd/dev/sweet_touch.c; [ "$(calls $P/a/$f)" = "$(calls $T/chain/s/$f)" ] && echo "io_submap call sites in sweet_touch.c: unchanged ($(calls $P/a/$f))"
[ "$(calls $T/chain/s/bsd/dev/nord2_rtc.inc)" = 0 ] && echo "nord2_rtc.inc: no ml_io_map call"
echo MERGE_CHECK_OK
