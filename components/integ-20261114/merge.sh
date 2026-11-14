#!/bin/bash
# integ-20261114/merge.sh: rebase the parallel pieces onto the real chain (q10jgs stage + ascending patches) by per-file 3-way merge,
# regenerate each patch fuzz-0 against the state before it, and write patched-sources-NNNN.  base(N,file) = patched-sources-N/file
# with patch N reversed; ours = current chain state; theirs = patched-sources-N/file.  Conflicts stop the script.
# Usage: merge.sh [stopafter]     MORE_SRC='"NNNN dir" ...' appends later pieces (ascending).  0470's authored source: src-0470/.
set -euo pipefail
B=$NORD2_WORK/gpu/build
H=$B/integ-20261114; cd $H
SRC=(  "0420 $B/memgov-20261113"
       "0430 $B/wifi-20261113"
       "0432 $B/wifi-sae-20261113"
       "0433 $B/wifi-sae-20261113"
       "0434 $B/wifi-sae-20261113"
       "0436 $B/wifi-20261113"
       "0440 $B/gfxheap-20261113/heap-0440"
       "0441 $B/gfxheap-20261113/gpuheap-0441"
       "0450 $B/wifi8021x-20261113"
       "0451 $B/wifi8021x-20261113"
       "0452 $B/wifi8021x-20261113"
       "0453 $B/wifi8021x-20261113"
       "0460 $B/dvfs-20261113"
       "0461 $B/dvfs-20261113"
       "0462 $B/dvfs-20261113"
       "0470 $H/src-0470"
       "0480 $B/memwod-20261113/wod-0480"
       "0481 $B/memwod-20261113/gpumadv-0481"
       "0482 $B/memwod-20261113/vmcomp-0482"
       "0483 $H/src-0483"
       "0484 $H/src-0484"
       "0490 $B/yuvovl-20261113/0490-yuv422-ovl" ${MORE_SRC:-} )
rm -rf chain; mkdir chain; cp -a $B/device-mmu-q10jgs-candidate/k/kernel/stage chain/s
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for e in "${SRC[@]}"; do
  n=${e%% *}; d=${e#* }; p=$(ls $d/$n-*.patch); name=$(basename $p)
  ps=$d/patched-sources-$n; files=$(grep '^+++ ' $p | awk '{print $2}' | sed 's#^b/##')
  rm -rf $T/*; mkdir -p $T/base $T/prev
  for f in $files; do
    mkdir -p $T/base/$(dirname $f) $T/prev/$(dirname $f)
    [ -f chain/s/$f ] && cp chain/s/$f $T/prev/$f
    cp $ps/$f $T/base/$f
  done
  # reverse patch N on its own result -> its base
  (cd $T/base && patch -p1 -R --fuzz=0 --batch -i $p >/dev/null) || { echo "$n: reverse failed"; exit 1; }
  st=clean
  for f in $files; do
    if [ ! -s $T/base/$f ] && [ ! -f chain/s/$f ]; then mkdir -p chain/s/$(dirname $f); cp $ps/$f chain/s/$f; continue; fi
    if cmp -s $T/base/$f chain/s/$f; then cp $ps/$f chain/s/$f; continue; fi
    cp chain/s/$f $T/ours
    if ! git merge-file -p $T/ours $T/base/$f $ps/$f > $T/merged; then
      # hand resolution: resolve/resolved-NNNN-<file>, valid only for the exact conflict it was written for (sha of the conflict output)
      rf=$H/resolve/resolved-$n-$(basename $f); cs=$(sed "s#^\(<<<<<<<\|>>>>>>>\) .*#\1#" $T/merged | sha256sum | cut -c1-64)
      if [ -f $rf ] && [ "$(cat $rf.conflict-sha)" = "$cs" ]; then cp $rf $T/merged; st=resolved; echo "$n: CONFLICT in $f resolved by hand (resolve/$(basename $rf))"
      else echo "$n: CONFLICT in $f (conflict sha $cs)"; cp $T/merged $H/conflict-$n-$(basename $f); git merge-file -p --diff3 -L chain -L base$n -L piece$n $T/ours $T/base/$f $ps/$f > $H/conflict-$n-$(basename $f).diff3; exit 2; fi
    else st=merged; echo "$n: 3-way merged $f"; fi
    ! grep -q '^\(<<<<<<<\|=======\|>>>>>>>\)' $T/merged || { echo "$n: markers left in $f"; exit 2; }
    cp $T/merged chain/s/$f
  done
  mkdir -p p/$n; out=p/$n/$name; : > $out
  for f in $files; do
    if [ -f $T/prev/$f ]; then diff -u $T/prev/$f chain/s/$f | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> $out || true
    else diff -u /dev/null chain/s/$f | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> $out || true; fi
  done
  # verify fuzz-0 apply on prev
  (cd $T/prev && patch -p1 --fuzz=0 --batch -i $H/$out) > p/$n/apply.log; ! grep -E "offset|fuzz|FAILED|Reversed" p/$n/apply.log
  rm -rf p/$n/patched-sources-$n
  for f in $files; do cmp $T/prev/$f chain/s/$f; mkdir -p p/$n/patched-sources-$n/$(dirname $f); cp chain/s/$f p/$n/patched-sources-$n/$f; done
  cmp -s $p $out && echo "$n: identical to $p ($st)" || echo "$n: regenerated ($st) $(sha256sum $out | cut -c1-16)"
  [ "${1:-}" = "$n" ] && break
done
echo MERGE_DONE
