#!/bin/bash
# hand verification after the known 0399/0404 IMAGECHECK glitch (their first "NORD2..." literal was reworded by later patches of the chain):
# check.log, package, 0392/0393, every other marker of the list, the 0480..0482 strings, cmdline, boot_spare, sha.   usage: verify_image.sh q10jhk
B=$NORD2_WORK/gpu/build
N=${1:-q10jhk}; O=$B/device-mmu-$N-candidate; K=$O/k/kernel/kernel.development.nord2-metal-$N; cd $B
grep -q "FAILURES: none" $O/ui-sched/check.log && echo check.log ok
grep -q "audio package replaced: .* sha256 94fb2f9b6ba09fd921d42f15f034c5618dbcd85de16af8e29ae03f99fb7584b4" $O/ui-sched.stdout && echo pkg ok
grep -a -q 'NORD2 FB SYNC: pushes=' $K && grep -a -q "layers outside a swap's mask kept %s (0392)" $K && echo 0392/0393 ok
X="$(cat memgov-20261113/extra_dirs.txt) $B/memwod-20261113/wod-0480 $B/memwod-20261113/gpumadv-0481 $B/memwod-20261113/vmcomp-0482"
for n in 0394 0395 0396 0397 0398 0399 0400 0401 0403 0404 0405 0406 0407 0408 0409 0410 0411 0413 0414 0415 0416 0417 0418 0480 0481 0482; do
  f=$(find hwcomp-20261112 $X -maxdepth 3 -name "$n-*.patch" -not -path '*/patched-sources*' | head -1)
  m=$(grep '^+[^+]' "$f" | grep -o '"NORD2[^"%\\]\{6,\}' | head -1 | cut -c2-); [ -z "$m" ] && { echo -n "$n - "; continue; }
  grep -a -q -F "$m" $K && echo -n "$n ok " || echo -n "$n MISSING "; done; echo
for s in 'NORD2 SURF WOD: wire-on-demand surfaces on' 'NORD2 SURF WOD: live=' 'NORD2 GPU WOD: import ctx=' 'NORD2 GPU MADV: purged ' 'NORD2 VMCOMP: compressor mode default ' 'n2surf_wod' 'n2gpu_madv' 'n2vc'; do
  echo "'$s' x$(grep -a -c -F "$s" $K)"; done
grep -q '^5088 MiB window patched' $O/k/kernel.stdout && echo "5088 layout patch ok"
python3 -c "
import json;a=json.load(open('$O/ui-sched/image-receipt.json'));b=json.load(open('$B/device-mmu-q10jfh-candidate/ui-sched/image-receipt.json'));g=json.load(open('$B/device-mmu-q10jgs-candidate/ui-sched/image-receipt.json'))
print('cmdline same as reference:', a['commandline'].split()==[x for x in b['commandline'].split() if x!='keepsyms=1'], len(a['commandline']), 'chars; boot_spare', a['boot_spare'], '(q10jgs', g['boot_spare'], '-> cost', g['boot_spare']-a['boot_spare'], 'B); window', a.get('phys_base'), a.get('mem_size'))"
sha256sum $O/ui-sched/*.img
