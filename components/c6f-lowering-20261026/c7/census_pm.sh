#!/usr/bin/env bash
cd $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7
export SRC_METAL=$NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/shadow/metal
NORD2_METAL_PASS_MRT=0 python3 $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/fsjobs_pm.py --rebuild --tag pm --preframe 1 --tile > $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-off.log 2>&1; echo "off rc=$?" >> $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-off.log
cp $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-pf1.json $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-off.json 2>/dev/null; cp $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/fsjobs-pm-pf1.log $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/replay-pm-off.log 2>/dev/null
NORD2_METAL_PASS_MRT=1 python3 $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/fsjobs_pm.py --tag pm --preframe 1 --tile > $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-on.log 2>&1; echo "on rc=$?" >> $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-on.log
cp $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-pf1.json $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-on.json 2>/dev/null; cp $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/fsjobs-pm-pf1.log $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/replay-pm-on.log 2>/dev/null
