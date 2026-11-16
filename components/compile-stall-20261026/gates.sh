#!/bin/bash
cd $NORD2_WORK/gpu
B=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/enc/busy.sh
run(){ name=$1; shift; until ! $B; do sleep 20; done; echo "== $name $(date +%T)"; nice -n 10 "$@" > $NORD2_WORK/gpu/build/compile-stall-20261026/gate-$name.log 2>&1; echo "$name rc=$?"; }
run host_tests env NORD2_AIR_LLVM_BIN=/usr/bin bash tools/run_host_tests.sh
grep -c "^PASS\|PASS " $NORD2_WORK/gpu/build/compile-stall-20261026/gate-host_tests.log; tail -3 $NORD2_WORK/gpu/build/compile-stall-20261026/gate-host_tests.log
run ca python3 build/c6f-lowering-20261026/ca_backboardd_host_test.py; tail -2 $NORD2_WORK/gpu/build/compile-stall-20261026/gate-ca.log
run render python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py; grep -c TEGU_COMMAND_FAILED $NORD2_WORK/gpu/build/compile-stall-20261026/gate-render.log; tail -1 $NORD2_WORK/gpu/build/compile-stall-20261026/gate-render.log
run async python3 build/encode-cpu-20261026/async_frontend_test.py; grep "^PASS\|^FAIL" $NORD2_WORK/gpu/build/compile-stall-20261026/gate-async.log
run ab python3 build/encode-cpu-20261026/ab_schedule_test.py; tail -1 $NORD2_WORK/gpu/build/compile-stall-20261026/gate-ab.log
run integ python3 tests/metal_resource_integration_test.py --candidate; tail -3 $NORD2_WORK/gpu/build/compile-stall-20261026/gate-integ.log
echo GATES-DONE
