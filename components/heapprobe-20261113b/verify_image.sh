#!/bin/bash
# hand verification after the known 0399/0404 IMAGECHECK glitch: package, 0392/0393, every other marker, cmdline, sha
B=$NORD2_WORK/gpu/build
N=$1; O=$B/device-mmu-$N-candidate; K=$O/k/kernel/kernel.development.nord2-metal-$N; cd $B
grep -q "FAILURES: none" $O/ui-sched/check.log && echo check.log ok
grep -q "audio package replaced: .* sha256 94fb2f9b6ba09fd921d42f15f034c5618dbcd85de16af8e29ae03f99fb7584b4" $O/ui-sched.stdout && echo pkg ok
grep -a -q 'NORD2 FB SYNC: pushes=' $K && grep -a -q "layers outside a swap's mask kept %s (0392)" $K && echo 0392/0393 ok
X="$(cat heapprobe-20261113b/extra_dirs.txt)"
for n in 0399 0400 0401 0403 0404 0405 0406 0407 0408 0409 0410 0411 0413 0414 0415 0416 0417 0418 0419; do f=$(find hwcomp-20261112 $X -maxdepth 3 -name "$n-*.patch" -not -path '*/patched-sources*' | head -1)
  m=$(grep '^+[^+]' "$f" | grep -o '"NORD2[^"%\\]\{6,\}' | head -1 | cut -c2-); [ -z "$m" ] && { echo -n "$n - "; continue; }
  grep -a -q -F "$m" $K && echo -n "$n ok " || echo -n "$n MISSING "; done; echo
grep -a -q "0418 policy for" $K && echo 0418 string ok
python3 -c "
import json;a=json.load(open('$O/ui-sched/image-receipt.json'));b=json.load(open('$B/device-mmu-q10jfh-candidate/ui-sched/image-receipt.json'))
print('cmdline same as reference:', a['commandline'].split()==[x for x in b['commandline'].split() if x!='keepsyms=1'], len(a['commandline']), 'boot_spare', a['boot_spare'])"
sha256sum $O/ui-sched/*.img
