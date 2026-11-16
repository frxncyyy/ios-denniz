#!/usr/bin/env bash
# Host-only gates for C6m (preframe / tile batch / indexed / async), jobs1, nice 10.
W=$NORD2_WORK
O=$W/gpu/build/c6f-lowering-20261026/c6m
cd "$W"
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 5; done
echo "lease $(date -Is)" > $O/gates.status
nice -n 10 python3 gpu/tests/metal_resource_integration_test.py --candidate > $O/integration.log 2>&1; echo "integration rc=$?" >> $O/gates.status
nice -n 10 python3 gpu/build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py > $O/render-harness.log 2>&1; echo "render-harness rc=$?" >> $O/gates.status
nice -n 10 python3 gpu/build/c6f-lowering-20261026/ca_backboardd_host_test.py > $O/ca-preframe0.log 2>&1; echo "ca preframe0 rc=$?" >> $O/gates.status
NORD2_METAL_PREFRAME=1 nice -n 10 python3 gpu/build/c6f-lowering-20261026/ca_backboardd_host_test.py > $O/ca-preframe1.log 2>&1; echo "ca preframe1 rc=$?" >> $O/gates.status
(cd gpu && nice -n 10 bash tools/run_host_tests.sh > $O/host-tests.log 2>&1); echo "host-tests rc=$? (final probe step needs NORD2_AIR_LLVM_BIN)" >> $O/gates.status
echo "done $(date -Is)" >> $O/gates.status
