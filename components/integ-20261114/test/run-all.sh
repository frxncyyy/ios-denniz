#!/bin/bash
# integ-20261114/test/run-all.sh: every piece's host test against the MERGED chain (integ-20261114/chain/s = q10jgs stage + all patches).
# Tests that include "../b/..." get a temp dir whose b/ is the merged chain.  Wi-Fi: each set's own runner (SAE, 0436, 802.1X incl. the
# 0430 suite) runs from a copy of its test dir in test/wtree/<set>/ whose patched-sources-* hold the MERGED Wi-Fi files (the runners'
# mk-candidate.py overlay those), and the SAE runner's q10jgw2 stage is the merged chain.
set -uo pipefail
B=$NORD2_WORK/gpu/build
H=$B/integ-20261114; S=$H/chain/s; L=$H/test/logs; rm -rf $L; mkdir -p $L; T=$(mktemp -d); trap 'rm -rf $T' EXIT; ln -s $S $T/b; fail=0
CC="cc -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all"
r() { local n=$1; shift; if "$@" > $L/$n.log 2>&1; then echo "PASS $n: $(grep -v '^\s*$' $L/$n.log | tail -1 | cut -c1-110)"; else echo "FAIL $n (log $L/$n.log): $(tail -2 $L/$n.log | tr '\n' ' ' | cut -c1-220)"; fail=1; fi; }
inb() { mkdir -p $T/$1/test; cp $2 $T/$1/test/; ln -sfn $S $T/$1/b; }      # test file in <tmp>/<n>/test with ../b = merged chain
inb mg $B/memgov-20261113/test/memgov_host_test.c;       r 0420-memgov sh -c "$CC -o $T/mg/t $T/mg/test/memgov_host_test.c && $T/mg/t"
r 0484-memgov-class-budget sh -c "$CC -Werror -I$S/bsd/kern -o $T/m84 $H/test/memgov_0484_test.c && $T/m84"
mkdir $T/hp; cp $B/gfxheap-20261113/heap-0440/test/*.c $T/hp/; cp $S/iokit/Kernel/arm/sweet_fb_hw_pool.h $S/iokit/Kernel/arm/sweet_fb_iommu.h $T/hp/; ln -s $S $T/b2
sed -i 's#"\.\./b/#"'$S'/#' $T/hp/gfx_heap_host_test.c;  r 0440-heap sh -c "$CC -o $T/hp/t $T/hp/gfx_heap_host_test.c && $T/hp/t"
r 0411-iommuboot sh -c "$CC -Werror -I$S/iokit/Kernel/arm -o $T/ib $B/iommuboot-20261113/test/iommuboot_host_test.c && $T/ib"
mkdir -p $T/dv/test; cp $B/dvfs-20261113/test/run.sh $B/dvfs-20261113/test/dvfs_sim_test.c $T/dv/test/; ln -s $S $T/dv/b
r 0460-0462-dvfs env STAGE=$S bash $T/dv/test/run.sh
inb wod $B/memwod-20261113/wod-0480/test/wod_host_test.c;      r 0480-wod sh -c "cc -std=c11 -O2 -Wall -Wextra -Werror -o $T/wod/t $T/wod/test/wod_host_test.c && $T/wod/t"
inb madv $B/memwod-20261113/gpumadv-0481/test/madv_host_test.c; r 0481-madv sh -c "cc -std=c11 -O2 -Wall -Wextra -Werror -o $T/madv/t $T/madv/test/madv_host_test.c && $T/madv/t"
inb vmc $B/memwod-20261113/vmcomp-0482/test/vmcomp_host_test.c; r 0482-vmcomp sh -c "cc -std=c11 -Wall -Wextra -Werror -o $T/vmc/t $T/vmc/test/vmcomp_host_test.c && $T/vmc/t"
r 0490-yuv sh -c "cc -std=c11 -O1 -Wall -Wextra -I$S/iokit/Kernel/arm -o $T/yuv $B/yuvovl-20261113/0490-yuv422-ovl/test/yuv_host_test.c && $T/yuv | tail -3"
# ---- Wi-Fi sets on the merged sources
W=$H/test/wtree; rm -rf $W; mkdir -p $W/device-mmu-q10jgw2-candidate/k/kernel; ln -s $S $W/device-mmu-q10jgw2-candidate/k/kernel/stage
WF=$(for n in 0430 0432 0433 0434 0436 0450 0452; do grep '^+++ ' $H/p/$n/$n-*.patch | awk '{print $2}' | sed 's#^b/##'; done | sort -u)
mkps() { for f in $WF; do mkdir -p $1/$(dirname $f); cp $S/$f $1/$f; done; }
for set in wifi-20261113 wifi-sae-20261113 wifi8021x-20261113; do mkdir -p $W/$set; cp -a $B/$set/test $W/$set/test; done
cp $B/wifi-20261113/wifi_binpatch.py $W/wifi-20261113/
mkps $W/wifi-20261113/patched-sources-0430; mkps $W/wifi-20261113/patched-sources-0436
mkps $W/wifi-sae-20261113/patched-sources-0434
mkps $W/wifi8021x-20261113/patched-sources-0450; mkps $W/wifi8021x-20261113/patched-sources-0452
# glue (cand_post.py): the 0436 and SAE suites run with kern.n2wifi_8021x=0 (they assert the pre-0450 refusal of 802.1X requests; the 802.1X
# suite runs the switch on), the 0436 suite also with kern.n2wifi_sae=0 (it asserts the 0430 MFP refusal that 0434 lifts; the SAE suite
# covers sae on/off itself); 0436 and 802.1X candidates get the SAE host packaging.
sed -i 's/unsigned niwj_8021x=0x11,niwj_ap_mfpr;/unsigned niwj_8021x=0,niwj_ap_mfpr;/' $W/wifi-sae-20261113/patched-sources-0434/bsd/dev/wifi_ios_join.c   # SAE runner compiles these directly
grep -q 'unsigned niwj_8021x=0,niwj_ap_mfpr;' $W/wifi-sae-20261113/patched-sources-0434/bsd/dev/wifi_ios_join.c || { echo "SAE 8021x-off edit failed"; exit 1; }
for x in "wifi-20261113:--pack-sae --8021x-off --sae0" "wifi8021x-20261113:--pack-sae"; do m=$W/${x%%:*}/test/mk-candidate.py
  printf '\nimport subprocess as _sp; _sp.run(["python3", "%s", str(out)] + "%s".split(), check=True)\n' $H/test/cand_post.py "${x#*:}" >> $m; done
export TMPDIR=$H/test/tmp; mkdir -p $TMPDIR
r wifi-0430-0436-suite sh -c "python3 $W/wifi-20261113/test/mk-candidate.py $L/wifi0436-cand > /dev/null && bash $W/wifi-20261113/test/run-host-tests.sh $L/wifi0436-cand $L/wifi0436"
r wifi-sae-0432-0434-suite bash $W/wifi-sae-20261113/test/run-host-tests.sh $L/wifisae
r wifi-8021x-0450-0453-suite bash $W/wifi8021x-20261113/test/run-host-tests.sh $L/wifi8021x
[ $fail = 0 ] && echo INTEG_HOST_TESTS_OK || echo INTEG_HOST_TESTS_FAILED
