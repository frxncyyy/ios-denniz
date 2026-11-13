#!/bin/bash
# wifi-fix-20261114/test/run-host-tests.sh: 0491 host tests on the MERGED integration chain (integ-20261114/chain/s = q10jia sources)
# + patched-sources-0491.  Same method as integ-20261114/test/run-all.sh (Wi-Fi part): each Wi-Fi set's own runner works on a copy of its
# test dir under test/wtree whose patched-sources-* hold the merged+0491 files.  The copy of the 0436 fixture is adapted by adapt.py
# (deactivate without address, 0491 fallback transitions).  Nothing outside wifi-fix-20261114 is written.
set -uo pipefail
B=$NORD2_WORK/gpu/build
F=$B/wifi-fix-20261114; I=$B/integ-20261114; L=$F/test/logs; S=$F/test/stage; W=$F/test/wtree; fail=0
rm -rf $L $S $W $F/test/tmp; mkdir -p $L $F/test/tmp
cp -a $I/chain/s $S; cp -a $F/patched-sources-0491/. $S/
for f in $(cd $F/patched-sources-0491 && find . -type f); do cmp -s $F/patched-sources-0491/$f $S/$f || { echo "stage overlay failed: $f"; exit 1; }; done
r() { local n=$1; shift; if "$@" > $L/$n.log 2>&1; then echo "PASS $n: $(grep -v '^\s*$' $L/$n.log | tail -1 | cut -c1-110)"; else echo "FAIL $n (log $L/$n.log): $(tail -2 $L/$n.log | tr '\n' ' ' | cut -c1-220)"; fail=1; fi; }
mkdir -p $W/device-mmu-q10jgw2-candidate/k/kernel; ln -s $S $W/device-mmu-q10jgw2-candidate/k/kernel/stage
WF=$(for n in 0430 0432 0433 0434 0436 0450 0452; do grep '^+++ ' $I/p/$n/$n-*.patch | awk '{print $2}' | sed 's#^b/##'; done | sort -u)
mkps() { for f in $WF; do mkdir -p $1/$(dirname $f); cp $S/$f $1/$f; done; }
for set in wifi-20261113 wifi-sae-20261113 wifi8021x-20261113; do mkdir -p $W/$set; cp -a $B/$set/test $W/$set/test; done
cp $B/wifi-20261113/wifi_binpatch.py $W/wifi-20261113/
mkps $W/wifi-20261113/patched-sources-0430; mkps $W/wifi-20261113/patched-sources-0436
mkps $W/wifi-sae-20261113/patched-sources-0434
mkps $W/wifi8021x-20261113/patched-sources-0450; mkps $W/wifi8021x-20261113/patched-sources-0452
python3 $F/test/adapt.py $W/wifi-20261113/test || exit 1
# same glue as integ run-all.sh (the 0436 and SAE suites assert pre-0450 / pre-0434 refusals)
sed -i 's/unsigned niwj_8021x=0x11,niwj_ap_mfpr;/unsigned niwj_8021x=0,niwj_ap_mfpr;/' $W/wifi-sae-20261113/patched-sources-0434/bsd/dev/wifi_ios_join.c
grep -q 'unsigned niwj_8021x=0,niwj_ap_mfpr;' $W/wifi-sae-20261113/patched-sources-0434/bsd/dev/wifi_ios_join.c || { echo "SAE 8021x-off edit failed"; exit 1; }
for x in "wifi-20261113:--pack-sae --8021x-off --sae0" "wifi8021x-20261113:--pack-sae"; do m=$W/${x%%:*}/test/mk-candidate.py
  printf '\nimport subprocess as _sp; _sp.run(["python3", "%s", str(out)] + "%s".split(), check=True)\n' $I/test/cand_post.py "${x#*:}" >> $m; done
export TMPDIR=$F/test/tmp
grep -q 'unsigned niwj_privmac_mode=2;' $S/bsd/dev/wifi_ios_join.c || { echo "default mode is not 2"; exit 1; }
r wifi-0430-0436-0491-suite sh -c "python3 $W/wifi-20261113/test/mk-candidate.py $L/wifi0491-cand > /dev/null && bash $W/wifi-20261113/test/run-host-tests.sh $L/wifi0491-cand $L/wifi0491"
r wifi-sae-0432-0434-suite bash $W/wifi-sae-20261113/test/run-host-tests.sh $L/wifisae
r wifi-8021x-0450-0453-suite bash $W/wifi8021x-20261113/test/run-host-tests.sh $L/wifi8021x
rm -rf $F/test/tmp
[ $fail = 0 ] && echo WIFI_FIX_HOST_TESTS_OK || echo WIFI_FIX_HOST_TESTS_FAILED
exit $fail
