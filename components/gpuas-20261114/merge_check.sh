#!/bin/bash
# gpuas-20261114/merge_check.sh: runs integ-20261114/merge.sh on a COPY (temp dir; integ-20261114/chain and p/ are not touched) with 0491 (wifi-fix-20261114),
# 0492 (ccperf-20261114), 0494 (r0494/ here: rebased for a chain without 0493) and 0500 (this dir) appended to its SRC list (by sed, as
# ccperf-fix-20261114/merge_check.sh), and checks that merge.sh regenerates 0494 and 0500 byte-identical (clean, no 3-way merge), that the merged chain
# holds b/'s files, and that 0500 adds no ml_io_map call (io_submap has 3 free pages; 0500 maps no registers at all).
set -euo pipefail
B=$NORD2_WORK/gpu/build
P=$(cd "$(dirname "$0")" && pwd); I=$B/integ-20261114; C=$B/ccperf-20261114
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for x in src-0470 src-0483 src-0484 resolve; do ln -s $I/$x $T/$x; done
more="\"0491 $B/wifi-fix-20261114\" \"0492 $C\" \"0494 $P/r0494\" \"0500 $P\""
sed -e "s#^H=\$B/integ-20261114; cd \$H#H=$T; cd \$H#" -e "s#\${MORE_SRC:-} )#$more )#" $I/merge.sh > $T/merge.sh
grep -q "0500 $P" $T/merge.sh && grep -q "^H=$T; cd" $T/merge.sh || { echo "merge.sh layout changed: edit failed"; exit 1; }
(cd $T && bash ./merge.sh) | grep -E '^049|^050|MERGE_DONE|CONFLICT'
cmp $T/p/0500/0500-*.patch $P/0500-*.patch; diff -r $T/p/0500/patched-sources-0500 $P/patched-sources-0500
cmp $T/p/0494/0494-*.patch $P/r0494/0494-*.patch; diff -r $T/p/0494/patched-sources-0494 $P/r0494/patched-sources-0494
cmp $T/p/0492/0492-*.patch $C/0492-*.patch
for f in bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c iokit/Kernel/arm/Nord2GPU.cpp; do cmp $T/chain/s/$f $P/b/$f; done
cmp $T/chain/s/bsd/dev/nord2_cpufreq.c $P/r0494/b/bsd/dev/nord2_cpufreq.c
! grep '^+' $P/0500-*.patch | grep -q 'ml_io_map' || { echo "0500 adds an ml_io_map call"; exit 1; }
echo MERGE_CHECK_OK
