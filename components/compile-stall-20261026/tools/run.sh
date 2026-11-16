#!/bin/bash
# run.sh TAG [env...]: cold replay of $CORPUS (default c6jb) into $NORD2_WORK/gpu/build/compile-stall-20261026/runs/TAG
T=$1; shift
R=${REPLAY:-$NORD2_WORK/gpu/build/pipeline-pack-20261026/c6jb/replay/pipeline-replay}
CORPUS=${CORPUS:-$NORD2_WORK/gpu/build/pipeline-pack-20261026/corpus-c6jb.txt}
mkdir -p $NORD2_WORK/gpu/build/compile-stall-20261026/runs; rm -rf $NORD2_WORK/gpu/build/compile-stall-20261026/runs/$T; mkdir -p $NORD2_WORK/gpu/build/compile-stall-20261026/runs/$T
B=$(python3 -c "import json;print(json.load(open('$NORD2_WORK/gpu/build/pipeline-pack-20261026/c6jb/PipelinePack.tpcpack.json'))['build'])")
cd $NORD2_WORK/gpu
t0=$(date +%s.%N)
env TEGU_PIPELINE_CACHE_BUILD_OVERRIDE=$B TEGU_PIPELINE_CACHE=1 "$@" nice -n 10 $R build/ui-corpus-20261025/libraries $CORPUS $NORD2_WORK/gpu/build/compile-stall-20261026/runs/$T > $NORD2_WORK/gpu/build/compile-stall-20261026/runs/$T.log 2>&1
rc=$?; t1=$(date +%s.%N)
echo "rc=$rc wall=$(python3 -c "print(round($t1-$t0,3))")s $(grep 'REPLAY SUMMARY' $NORD2_WORK/gpu/build/compile-stall-20261026/runs/$T.log | tail -1)"
