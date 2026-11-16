#!/usr/bin/env bash
# Pipelined CA loop on the frame_bench binary: kernel calls per frame. Usage: caloop.sh <scn> <frames> [extra env...]
G=$NORD2_WORK/gpu
cd $G
ARGS=$(python3 -c "import sys;sys.path.insert(0,'tools');sys.argv=['x'];import frame_bench as fb;print(' '.join(map(str,fb.corpus_args())))")
scn=$1;n=$2;shift 2
env LD_LIBRARY_PATH=$G/build/deepseek-parallel-20261025/workers/compute/cargo-target/release:$G/build/mesa-build/src/panfrost/clc \
 ASAN_OPTIONS=detect_leaks=0 TEGU_PLAN_MEMO_VERIFY=0 FB_ASYNC=1 FB_CA_LOOP=$n FB_CA_SCN=$scn "$@" \
 nice -n 10 build/frame-bench-20261026/frame_bench_bin $ARGS 1 tick 2>&1 | grep -E "FB_CALOOP|TEGU_PERF_ASYNC|TEGU_PERF_KCALL"
