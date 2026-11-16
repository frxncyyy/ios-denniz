#!/bin/bash
# bt-20261115/fin28 = private copy of bt-20261115/fin3 for q10jfinal19 (final18bt3 recipe + 0715); usbchg-20261115 private copy (paths -> bt-20261115/fin28); q10jfinal14usb = final13 recipe + EXTRA_PIECES 0700..0702
# final3-20261114/rebase.sh: rebase the post-q10jcx pieces onto the real q10jcx chain (merge.sh logic, own chain/ and p/ here so the
# shared integ-20261114/{chain,p} are untouched).  Start = q10jcx stage with 0640 reversed (= chain through 0551).  Each piece N:
# per-file 3-way merge (ours = chain, base = patched-sources-N with N reversed, theirs = patched-sources-N); hand resolutions in
# final3-20261114/resolve/resolved-NNNN-<file> valid only for the exact conflict sha; output p/NNNN/NNNN-*.patch fuzz-0 + patched-sources-NNNN.
#   usage: rebase.sh            PIECES env extends the list ('"NNNN dir" ...', any order, sorted here; later codec/shader pieces go here)
set -euo pipefail
B=$NORD2_WORK/gpu/build
H=$B/bt-20261115/fin28/integ; cd $H
eval "SRC0=( \"0560 $B/camflash-20261114\" \"0580 $B/gnss-20261114\" \"0581 $B/gnss-20261114\" \"0610 $B/haptic-20261114\" \"0640 $B/snap-20261114\" ${PIECES:-} )"
mapfile -t SRC < <(printf '%s\n' "${SRC0[@]}" | sort -n -k1,1)   # pieces in ascending number (codec 0590-0609 land before 0610)
rm -rf chain p; mkdir -p chain p; cp -a $B/device-mmu-q10jcx-candidate/k/kernel/stage chain/s
(cd chain/s && patch -p1 -R --fuzz=0 --batch -i $B/snap-20261114/0640-nord2-overlay-snapshot.patch >/dev/null) || { echo "0640 reverse on q10jcx failed"; exit 1; }
for f in $(grep '^+++ ' $B/snap-20261114/0640-*.patch | awk '{print $2}' | sed 's#^b/##'); do
  [ -f $B/snap-20261114/a/$f ] && { cmp $B/snap-20261114/a/$f chain/s/$f || { echo "start != snap a/ for $f"; exit 1; }; }; done
echo "start = q10jcx - 0640 (== snap-20261114/a)"
T=$(mktemp -d -p $H .rb.XXXXXX); trap 'rm -rf "$T"' EXIT
prevn=0000
for e in "${SRC[@]}"; do
  n=${e%% *}; d=${e#* }; p=$(ls $d/$n-*.patch); name=$(basename $p)
  [ $((10#$n)) -gt $((10#$prevn)) ] || { echo "PIECES not ascending at $n"; exit 1; }; prevn=$n
  ps=$d/patched-sources-$n; files=$(grep '^+++ ' $p | awk '{print $2}' | sed 's#^b/##')
  rm -rf $T/*; mkdir -p $T/base $T/prev
  for f in $files; do
    mkdir -p $T/base/$(dirname $f) $T/prev/$(dirname $f)
    [ -f chain/s/$f ] && cp chain/s/$f $T/prev/$f
    cp $ps/$f $T/base/$f
  done
  (cd $T/base && patch -p1 -R --fuzz=0 --batch -i $p >/dev/null) || { echo "$n: reverse failed"; exit 1; }
  st=clean
  for f in $files; do
    if [ ! -s $T/base/$f ] && [ ! -f chain/s/$f ]; then mkdir -p chain/s/$(dirname $f); cp $ps/$f chain/s/$f; continue; fi
    if cmp -s $T/base/$f chain/s/$f; then cp $ps/$f chain/s/$f; continue; fi
    cp chain/s/$f $T/ours
    if ! git merge-file -p $T/ours $T/base/$f $ps/$f > $T/merged; then
      rf=$H/resolve/resolved-$n-$(basename $f); cs=$(sed "s#^\(<<<<<<<\|>>>>>>>\) .*#\1#" $T/merged | sha256sum | cut -c1-64)
      if [ -f $rf ] && [ "$(cat $rf.conflict-sha)" = "$cs" ]; then cp $rf $T/merged; st=resolved; echo "$n: CONFLICT in $f resolved by hand (resolve/$(basename $rf))"
      else echo "$n: CONFLICT in $f (conflict sha $cs)"; cp $T/merged $H/conflict-$n-$(basename $f); echo $cs > $H/conflict-$n-$(basename $f).sha
        git merge-file -p --diff3 -L chain -L base$n -L piece$n $T/ours $T/base/$f $ps/$f > $H/conflict-$n-$(basename $f).diff3 || true; exit 2; fi
    else st=merged; echo "$n: 3-way merged $f"; fi
    ! grep -q '^\(<<<<<<<\|=======\|>>>>>>>\)' $T/merged || { echo "$n: markers left in $f"; exit 2; }
    cp $T/merged chain/s/$f
  done
  mkdir -p p/$n; out=p/$n/$name; : > $out
  for f in $files; do
    if [ -f $T/prev/$f ]; then diff -u $T/prev/$f chain/s/$f | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> $out || true
    else diff -u /dev/null chain/s/$f | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> $out || true; fi
  done
  (cd $T/prev && patch -p1 --fuzz=0 --batch -i $H/$out) > p/$n/apply.log; ! grep -E "offset|fuzz|FAILED|Reversed" p/$n/apply.log
  for f in $files; do cmp $T/prev/$f chain/s/$f; mkdir -p p/$n/patched-sources-$n/$(dirname $f); cp chain/s/$f p/$n/patched-sources-$n/$f; done
  cmp -s $p $out && echo "$n: identical to $p ($st)" || echo "$n: regenerated ($st) $(sha256sum $out | cut -c1-16)"
done
echo REBASE_DONE
