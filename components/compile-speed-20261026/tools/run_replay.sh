#!/bin/bash
# run_replay.sh <libdir> <records> <outname> [extra env...]  -- profiled replay (host only)
G=$NORD2_WORK/gpu
W=$G/build/compile-speed-20261026
LIB=$1; REC=$2; OUT=$W/runs/$3; shift 3
rm -rf $OUT; mkdir -p $OUT/cache
env "$@" NIR_DEBUG=${NIR_DEBUG-novalidate} SPEED_OUT=$OUT LD_PRELOAD=$W/bin/speedshim.so LD_LIBRARY_PATH=$LIB \
  TEGU_PIPELINE_CACHE_BUILD_OVERRIDE=nord2-arch9-b661b954568b7bcb6c93349158b91b143dd3359c6160b26ef0c1f030a94526fa TEGU_PIPELINE_CACHE=1 \
  nice -n 10 ${REPLAY:-$G/build/pipeline-pack-20261026/c6k/replay/pipeline-replay} $G/build/ui-corpus-20261025/libraries $REC $OUT/cache > $OUT/replay.log 2>&1
echo "rc=$? $(grep 'REPLAY SUMMARY' $OUT/replay.log | tail -1)"
