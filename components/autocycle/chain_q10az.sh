#!/bin/bash
B=$NORD2_WORK/gpu/build
until [ -n "$(ls $B/device-mmu-q10ay-candidate/ui-sched/*.img 2>/dev/null)" ] || grep -q FAILED $B/device-mmu-q10ay-candidate/pack.done 2>/dev/null; do sleep 10; done
rm -rf $B/device-mmu-q10ay-candidate/kernelb
cd $B/device-mmu-q10az-candidate; rm -rf kernelb ui-sched pack.done; nohup ./build_q10az.sh > build_q10az.log 2>&1 &
until [ -f $B/autocycle/runs/q10ay/DONE ] || grep -q "BUILD FAILED" $B/autocycle/runs-q10ay.out 2>/dev/null; do sleep 10; done
$B/autocycle/install_nvdisk.sh > $B/autocycle/install_nvdisk.out 2>&1; echo "install rc=$?" >> $B/autocycle/install_nvdisk.out
$B/autocycle/build_and_run.sh q10az > $B/autocycle/runs-q10az.out 2>&1
