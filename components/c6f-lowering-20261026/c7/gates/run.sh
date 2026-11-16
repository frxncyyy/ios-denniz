#!/usr/bin/env bash
W=$NORD2_WORK
D=$W/gpu/build/c6f-lowering-20261026
O=$D/c7/gates
bash $D/c6n/gates.sh
cp $D/c6n/gates.status $D/c6n/*.log $O/ 2>/dev/null
bash $D/c6n/unit.sh > $O/unit.log 2>&1; echo "unit rc=$?" >> $O/extra.status
cd $W
NORD2_METAL_PASS_MRT=1 NORD2_METAL_PREFRAME=1 nice -n 10 python3 gpu/build/c6f-lowering-20261026/ca_backboardd_host_test.py > $O/ca-pf1-passmrt.log 2>&1; echo "ca pf1 passmrt rc=$?" >> $O/extra.status
nice -n 10 python3 gpu/build/encode-cpu-20261026/ab_schedule_test.py > $O/ab.log 2>&1; echo "ab rc=$?" >> $O/extra.status
echo "done $(date -Is)" >> $O/extra.status
