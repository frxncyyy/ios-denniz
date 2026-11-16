#!/usr/bin/env bash
cd $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan|fsjobs|probe_host)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 10; done
export SRC_METAL=$NORD2_WORK/gpu/metal
NORD2_METAL_PASS_MRT=1 nice -n 10 python3 $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/fsjobs_pm.py --rebuild --tag pm --preframe 1 --tile > $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-on2.log 2>&1; echo "on2 rc=$?" >> $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-on2.log
cp $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-pf1.json $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/census-pm-on2.json; cp $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/fsjobs-pm-pf1.log $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/replay-pm-on2.log
bash $NORD2_WORK/gpu/build/c6f-lowering-20261026/c7/gates/run.sh
