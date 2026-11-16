#!/bin/bash
# caloop.sh TAG ASYNC CA_US SUBMIT_US GPU_US [frames]
A=$(sed -n 1p $NORD2_WORK/gpu/build/compile-stall-20261026/tools/fbargs.txt);L=$(sed -n 2p $NORD2_WORK/gpu/build/compile-stall-20261026/tools/fbargs.txt)
cd $NORD2_WORK/gpu/build/frame-bench-20261026
env LD_LIBRARY_PATH=$L ASAN_OPTIONS=detect_leaks=0 TEGU_PLAN_MEMO_VERIFY=0 TEGU_ASYNC_PIPELINE=1 FB_ASYNC=$2 FB_CA_US=$3 FB_SUBMIT_US=$4 FB_GPU_US=$5 FB_CA_LOOP=${6:-40} FB_CA_SCN=large  nice -n 10 ./frame_bench_bin $A 0 tick > $NORD2_WORK/gpu/build/compile-stall-20261026/caloop-$1.out 2> $NORD2_WORK/gpu/build/compile-stall-20261026/caloop-$1.err
echo "$1 rc=$?"; grep -h "FB_CALOOP" $NORD2_WORK/gpu/build/compile-stall-20261026/caloop-$1.out; grep -h "TEGU_PERF_ASYNC" $NORD2_WORK/gpu/build/compile-stall-20261026/caloop-$1.err | tail -2
