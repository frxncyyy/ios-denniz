#!/bin/bash
cd $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/gpu
B=$NORD2_WORK/gpu/build/compile-stall-20261026/tools/busy.sh
run(){ name=$1; shift; until ! $B; do sleep 20; done; echo "== $name $(date +%T)"; nice -n 10 "$@" > $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/pg-$name.log 2>&1; echo "$name rc=$?"; }
run host_tests env NORD2_ANDROID_SOURCE=$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel NORD2_AIR_LLVM_BIN=/usr/bin bash tools/run_host_tests.sh; tail -1 $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/pg-host_tests.log
run ca python3 build/c6f-lowering-20261026/ca_backboardd_host_test.py; tail -1 $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/pg-ca.log; grep -c TEGU_PLAN_MEMO_MISMATCH $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/pg-ca.log
run render python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py; grep -c TEGU_COMMAND_FAILED $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/pg-render.log; grep -m2 "Assert\|error:" $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/pg-render.log
run async python3 build/encode-cpu-20261026/async_frontend_test.py; grep "^PASS\|^FAIL" $NORD2_WORK/gpu/build/compile-stall-20261026/c6p-private/pg-async.log
echo PGATES-DONE
