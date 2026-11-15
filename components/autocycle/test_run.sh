#!/bin/bash
# usage: test_run.sh TAG  -- build device-mmu-TAG-candidate/build_TAG.sh, then flash it as a TEST image and capture (run_cycle)
W=$NORD2_WORK
T=$1; O=$W/gpu/build/device-mmu-$T-candidate; D=$W/gpu/build/autocycle
cd $W && rm -f $O/pack.done && bash $O/build_$T.sh > $O/build.log 2>&1
grep -q done $O/pack.done || { echo "BUILD FAILED $(cat $O/pack.done)"; exit 1; }
cd $D; rm -rf $T-TEST-image runs/$T; mkdir -p $T-TEST-image; cp $O/ui-sched/*.img $T-TEST-image/
FB_WAIT=14400 bash ./run_cycle.sh $D/$T-TEST-image/$(ls $D/$T-TEST-image) $T
echo CYCLE-DONE
