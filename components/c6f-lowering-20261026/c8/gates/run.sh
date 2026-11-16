#!/usr/bin/env bash
W=$NORD2_WORK
D=$W/gpu/build/c6f-lowering-20261026
O=$D/c8/gates
rm -f $O/extra.status
bash $D/c6n/unit.sh > $O/unit.log 2>&1; echo "unit rc=$? $(grep -c 'rc=0 pass=2' $O/unit.log)/3" >> $O/extra.status
cp $D/c6n/bridge.log $O/bridge.log
bash $D/c6n/gates.sh
cp $D/c6n/gates.status $D/c6n/*.log $O/ 2>/dev/null
cd $W
NORD2_METAL_CHAIN=1 NORD2_METAL_PREFRAME=1 nice -n 10 python3 gpu/build/c6f-lowering-20261026/ca_backboardd_host_test.py > $O/ca-pf1-chain1.log 2>&1; echo "ca pf1 chain1 rc=$?" >> $O/extra.status
NORD2_METAL_CHAIN=2 nice -n 10 python3 gpu/build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py > $O/harness-chain2.log 2>&1; echo "harness chain2 rc=$?" >> $O/extra.status
nice -n 10 python3 gpu/build/encode-cpu-20261026/ab_schedule_test.py > $O/ab.log 2>&1; echo "ab rc=$?" >> $O/extra.status
echo "done $(date -Is)" >> $O/extra.status
