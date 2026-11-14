#!/bin/bash
# ccperf-fix-20261114/merge_check.sh: runs integ-20261114/merge.sh on a COPY (temp dir; integ-20261114/chain and p/ are not touched) with 0491 (wifi-fix-20261114),
# 0492, 0493 (ccperf-20261114) and 0494 (this dir) appended to its SRC list (by sed, as ccperf-20261114/merge_check.sh: MORE_SRC cannot carry "NNNN dir" pairs),
# and checks that merge.sh regenerates 0494 byte-identical (clean, no 3-way merge) and that the resulting chain holds this piece's b/ file.  Then:
#  - io_submap call sites (ml_io_map / ml_io_map_wcomb) per file, merged chain against the q10jia chain (integ-20261114/chain/s = what q10jia/q10jib were built
#    from): no file may have more, nord2_cpufreq.c has 5 fewer;
#  - the merged nord2_cpufreq.c compiles with the CI flags (integ-20261114/compile_0104_integ.py, output in the temp dir): 0 errors, no more warnings than the
#    q10jcc object, and the object calls _ml_io_map_unmappable.
set -euo pipefail
B=$NORD2_WORK/gpu/build
P=$(cd "$(dirname "$0")" && pwd); I=$B/integ-20261114; C=$B/ccperf-20261114
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for x in src-0470 src-0483 src-0484 resolve; do ln -s $I/$x $T/$x; done
more="\"0491 $B/wifi-fix-20261114\" \"0492 $C\" \"0493 $C\" \"0494 $P\""
sed -e "s#^H=\$B/integ-20261114; cd \$H#H=$T; cd \$H#" -e "s#\${MORE_SRC:-} )#$more )#" $I/merge.sh > $T/merge.sh
grep -q "0494 $P" $T/merge.sh && grep -q "^H=$T; cd" $T/merge.sh || { echo "merge.sh layout changed: edit failed"; exit 1; }
(cd $T && bash ./merge.sh) | grep -E '^049|MERGE_DONE|CONFLICT'
cmp $T/p/0494/0494-*.patch $P/0494-*.patch; diff -r $T/p/0494/patched-sources-0494 $P/patched-sources-0494
for n in 0492 0493; do cmp $T/p/$n/$n-*.patch $C/$n-*.patch; done
cmp $T/chain/s/bsd/dev/nord2_cpufreq.c $P/b/bsd/dev/nord2_cpufreq.c
for f in bsd/dev/nord2_gov.h bsd/dev/nord2_dram_rate.h iokit/Kernel/arm/Nord2GPU.cpp; do cmp $T/chain/s/$f $C/b/$f; done
calls() { grep -v '^extern\|^ \*\|^/\*' "$1" | { grep -o 'ml_io_map\(_wcomb\)\?(' || true; } | wc -l; }
more_sites=0; tot_old=0; tot_new=0
while read -r f; do
  o=0; [ -f "$I/chain/s/$f" ] && o=$(calls "$I/chain/s/$f"); n=$(calls "$T/chain/s/$f"); tot_old=$((tot_old + o)); tot_new=$((tot_new + n))
  [ "$n" -gt "$o" ] && { echo "IO_SUBMAP: $f has $n call sites, q10jia $o"; more_sites=1; }
  [ "$n" != "$o" ] && echo "io_submap call sites $f: q10jia $o -> $n"
done < <(cd $T/chain/s && grep -rl 'ml_io_map' --include='*.c' --include='*.cpp' --include='*.h' --include='*.inc' . | sed 's#^\./##' | sort)
echo "io_submap call sites, whole chain: q10jia $tot_old -> merged $tot_new"
[ $more_sites = 0 ] && [ $((tot_old - tot_new)) = 5 ]
python3 $I/compile_0104_integ.py $T/chain/s/bsd/dev/nord2_cpufreq.c $T/nord2_cpufreq.o > $T/cc.json
python3 - $T/cc.json $B/device-mmu-q10jcc-candidate/k/kernel/objs/nord2_cpufreq.json <<'PY'
import json, sys
n, o = (json.load(open(p)) for p in sys.argv[1:3])
print('CI-flag compile of the merged nord2_cpufreq.c: rc %d errors %d warnings %d (q10jcc object: %d)' % (n['rc'], n['errors'], n['warnings'], o['warnings']))
sys.exit(0 if n['rc'] == 0 and n['errors'] == 0 and n['warnings'] <= o['warnings'] else 1)
PY
r_new=$(llvm-objdump -r $T/nord2_cpufreq.o | grep -c '_ml_io_map$' || true); r_un=$(llvm-objdump -r $T/nord2_cpufreq.o | grep -c '_ml_io_map_unmappable$' || true)
r_old=$(llvm-objdump -r $B/device-mmu-q10jcc-candidate/k/kernel/objs/nord2_cpufreq.o | grep -c '_ml_io_map$' || true)
echo "object relocations: _ml_io_map q10jcc $r_old -> $r_new, _ml_io_map_unmappable $r_un"
[ "$r_un" -ge 1 ] && [ "$r_new" -lt "$r_old" ]
echo MERGE_CHECK_OK
