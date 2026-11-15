#!/bin/bash
# usage: chain_next.sh PREV NEXT : build NEXT once PREV's image exists, run NEXT once PREV's cycle is DONE
B=$NORD2_WORK/gpu/build
P=$1; N=$2
until [ -n "$(ls $B/device-mmu-$P-candidate/ui-sched/*.img 2>/dev/null)" ] || grep -q FAILED $B/device-mmu-$P-candidate/pack.done 2>/dev/null; do sleep 10; done
sleep 5; rm -rf $B/device-mmu-$P-candidate/kernelb
cd $B/device-mmu-$N-candidate; rm -rf kernelb ui-sched pack.done; ./build_$N.sh > build_$N.log 2>&1
until [ -f $B/autocycle/runs/$P/DONE ] || grep -q "BUILD FAILED" $B/autocycle/runs-$P.out 2>/dev/null; do sleep 10; done
$B/autocycle/build_and_run.sh $N > $B/autocycle/runs-$N.out 2>&1
