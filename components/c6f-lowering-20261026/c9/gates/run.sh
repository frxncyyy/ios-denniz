#!/usr/bin/env bash
W=$NORD2_WORK
D=$W/gpu/build/c6f-lowering-20261026
O=$D/c9/gates
rm -f $O/extra.status
bash $D/c6n/unit.sh > $O/unit.log 2>&1; echo "unit rc=$? $(grep -c 'rc=0 pass=2' $O/unit.log)/3" >> $O/extra.status
cp $D/c6n/bridge.log $O/bridge.log
bash $D/c6n/gates.sh
cp $D/c6n/gates.status $D/c6n/*.log $O/ 2>/dev/null
cd $W
nice -n 10 python3 gpu/build/encode-cpu-20261026/ab_schedule_test.py > $O/ab.log 2>&1; echo "ab rc=$?" >> $O/extra.status
cd $W/gpu && nice -n 10 python3 build/encode-cpu-20261026/async_frontend_test.py > $O/async-frontend.log 2>&1; echo "async frontend rc=$?" >> $O/extra.status
echo "done $(date -Is)" >> $O/extra.status
