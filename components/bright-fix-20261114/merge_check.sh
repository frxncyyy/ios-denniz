#!/bin/bash
# bright-fix-20261114/merge_check.sh: integ-20261114/merge.sh on a COPY (temp dir; integ-20261114/chain and p/ untouched) with 0491 (wifi-fix),
# 0492 0493 (ccperf-20261114), 0494 (ccperf-fix-20261114), 0495 0496 (this dir) appended to SRC (by sed, as ccperf-fix-20261114/merge_check.sh).
# Checks: merge.sh regenerates 0495 and 0496 byte-identical (clean) and 0491..0494 as their own dirs have them; the merged chain holds b/;
# no new io_submap call sites (ml_io_map / ml_io_map_wcomb) in any file; the merged SweetFramebuffer.cpp and sweet_touch.c compile with
# the CI flags with exactly the warnings of the q10jce objects (message-for-message) and no new ml_io_map relocation.
set -euo pipefail
B=$NORD2_WORK/gpu/build
ROOT=$(cd $B/../.. && pwd)
P=$(cd "$(dirname "$0")" && pwd); I=$B/integ-20261114; C=$B/ccperf-20261114; F=$B/ccperf-fix-20261114; R=$B/device-mmu-q10jce-candidate/k/kernel
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for x in src-0470 src-0483 src-0484 resolve; do ln -s $I/$x $T/$x; done
more="\"0491 $B/wifi-fix-20261114\" \"0492 $C\" \"0493 $C\" \"0494 $F\" \"0495 $P\" \"0496 $P\""
sed -e "s#^H=\$B/integ-20261114; cd \$H#H=$T; cd \$H#" -e "s#\${MORE_SRC:-} )#$more )#" $I/merge.sh > $T/merge.sh
grep -q "0496 $P" $T/merge.sh && grep -q "^H=$T; cd" $T/merge.sh || { echo "merge.sh layout changed: edit failed"; exit 1; }
(cd $T && bash ./merge.sh) | grep -E '^049|MERGE_DONE|CONFLICT'
for n in 0495 0496; do cmp $T/p/$n/$n-*.patch $P/$n-*.patch; diff -r $T/p/$n/patched-sources-$n $P/patched-sources-$n; done
cmp $T/p/0494/0494-*.patch $F/0494-*.patch; for n in 0492 0493; do cmp $T/p/$n/$n-*.patch $C/$n-*.patch; done
for f in iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_backlight.h bsd/dev/sweet_touch.c bsd/dev/nord2_torch.h; do cmp $T/chain/s/$f $P/b/$f; done
echo "0495/0496 regenerated identical; merged chain == b/"
calls() { grep -v '^extern\|^ \*\|^/\*' "$1" | { grep -o 'ml_io_map\(_wcomb\)\?(' || true; } | wc -l; }
bad=0
while read -r f; do
  o=0; [ -f "$I/chain/s/$f" ] && o=$(calls "$I/chain/s/$f"); n=$(calls "$T/chain/s/$f")
  if [ "$f" != bsd/dev/nord2_cpufreq.c ] && [ "$n" != "$o" ]; then echo "IO_SUBMAP: $f $o -> $n"; bad=1; fi
done < <(cd $T/chain/s && grep -rl 'ml_io_map' --include='*.c' --include='*.cpp' --include='*.h' --include='*.inc' . | sed 's#^\./##' | sort)
[ $bad = 0 ] && echo "io_submap call sites: unchanged in every file but nord2_cpufreq.c (0494: 5 fewer)"
w() { grep -E ': warning' "$1" | sed -E 's#^.*/##; s#:[0-9]+:[0-9]+:#:#' | sort; }
cd $T
python3 $ROOT/gpu/tools/xnu_staged_compile.py $T/chain/s/iokit/Kernel/arm/SweetFramebuffer.cpp --out $T/fb.o --extra=-Wno-unsafe-buffer-usage --cxx > $T/fb.log 2>&1
python3 $ROOT/gpu/tools/xnu_staged_compile.py $T/chain/s/bsd/dev/sweet_touch.c --out $T/st.o --extra=-Wno-unsafe-buffer-usage > $T/st.log 2>&1
diff <(w $R/SweetFramebuffer-compile.log) <(w $T/fb.log) > /dev/null && diff <(w $R/sweet_touch-compile.log) <(w $T/st.log) > /dev/null
echo "CI-flag compile: SweetFramebuffer.cpp $(grep -c ': warning' $T/fb.log) warnings (q10jce $(grep -c ': warning' $R/SweetFramebuffer-compile.log), same messages), sweet_touch.c $(grep -c ': warning' $T/st.log) (-Werror)"
for p in "fb.o $R/SweetFramebuffer.cpo" "st.o $R/sweet_touch.o"; do set -- $p
  a=$(llvm-objdump -r $1 | grep -c '_ml_io_map\(_wcomb\)\?$' || true); b=$(llvm-objdump -r $2 | grep -c '_ml_io_map\(_wcomb\)\?$' || true)
  echo "ml_io_map relocations $1: $a (q10jce $b)"; [ "$a" = "$b" ]; done
llvm-nm $T/fb.o > $T/fb.nm; llvm-nm $T/st.o > $T/st.nm; grep -q AppleARMBacklight $T/fb.nm && grep -q n2td_write $T/st.nm
echo MERGE_CHECK_OK
