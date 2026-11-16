#!/usr/bin/env bash
W=$NORD2_WORK
O=$W/gpu/build/c6f-lowering-20261026/c6n
cd "$W"
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan|fsjobs|probe_host)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 10; done
nice -n 10 python3 gpu/build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py > $O/render-harness.log 2>&1; echo "render-harness rc=$?" > $O/harness.status
