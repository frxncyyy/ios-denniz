#!/bin/bash
# ccperf-20261114/merge_check.sh: runs integ-20261114/merge.sh on a COPY (temp dir; integ-20261114/chain and p/ are not touched) with 0491 (wifi-fix-20261114,
# if present), 0492 and 0493 appended to its SRC list, and checks that merge.sh regenerates 0492/0493 byte-identical (clean, no 3-way merge needed) and that
# the resulting chain holds this piece's b/ files.  Note: merge.sh's MORE_SRC variable cannot carry "NNNN dir" pairs (it is expanded unquoted inside the
# array, so each pair is split at the space); the entries are therefore inserted into the copy's SRC list by sed.
set -euo pipefail
B=$NORD2_WORK/gpu/build
P=$(cd "$(dirname "$0")" && pwd); I=$B/integ-20261114
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for x in src-0470 src-0483 src-0484 resolve; do ln -s $I/$x $T/$x; done
more="\"0492 $P\" \"0493 $P\""; [ -f $B/wifi-fix-20261114/0491-*.patch ] && more="\"0491 $B/wifi-fix-20261114\" $more"
sed -e "s#^H=\$B/integ-20261114; cd \$H#H=$T; cd \$H#" -e "s#\${MORE_SRC:-} )#$more )#" $I/merge.sh > $T/merge.sh
grep -q "0493 $P" $T/merge.sh && grep -q "^H=$T; cd" $T/merge.sh || { echo "merge.sh layout changed: edit failed"; exit 1; }
(cd $T && bash ./merge.sh) | grep -E '^049|MERGE_DONE|CONFLICT'
for n in 0492 0493; do cmp $T/p/$n/$n-*.patch $P/$n-*.patch; diff -r $T/p/$n/patched-sources-$n $P/patched-sources-$n; done
for f in bsd/dev/nord2_gov.h bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dram_rate.h iokit/Kernel/arm/Nord2GPU.cpp; do cmp $T/chain/s/$f $P/b/$f; done
echo MERGE_CHECK_OK
