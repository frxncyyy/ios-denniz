#!/bin/bash
# mk_test.sh TAG : clone q10cx test build script to TAG, build and run cycle (writes runs/TAG/DONE)
B=$NORD2_WORK/gpu/build
T=$1; N=$B/device-mmu-$T-candidate; mkdir -p $N
cp $B/device-mmu-q10cx-candidate/{build_p1_candidate.py,pack_with_args.py} $N/
sed "s/q10cx/$T/g" $B/device-mmu-q10cx-candidate/build_q10cx.sh > $N/build_$T.sh; chmod +x $N/build_$T.sh
cd $B/autocycle; rm -rf runs/$T
$N/build_$T.sh > $N/build_$T.log 2>&1
./build_and_run.sh $T > runs/$T.out 2>&1
