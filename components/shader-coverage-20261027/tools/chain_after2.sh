#!/bin/bash
# graphics + captured re-evaluated before/after (compute compiler source unchanged: compute rows come from runs/base)
cd $NORD2_WORK/gpu/build/shader-coverage-20261027
B=runs/probe0-oldharness/libs
nice -n 10 python3 tools/replay.py --run base-g --kind graphics --jobs 3 --bridge $B/libtegu_air_bridge.so --compiler $B/libnord2_mali_runtime.so --profile $B/tegu-air-profile.h > runs-base-g.log 2>&1
nice -n 10 python3 tools/replay.py --run base-g --kind captured --jobs 3 --bridge $B/libtegu_air_bridge.so --compiler $B/libnord2_mali_runtime.so --profile $B/tegu-air-profile.h >> runs-base-g.log 2>&1
nice -n 10 python3 tools/replay.py --run after-f3 --kind graphics --jobs 3 --bridge $B/libtegu_air_bridge.so --compiler libs/mali-f3/libnord2_mali_runtime.so --profile $B/tegu-air-profile.h > runs-after-f3.log 2>&1
nice -n 10 python3 tools/replay.py --run after-f3 --kind captured --jobs 3 --bridge $B/libtegu_air_bridge.so --compiler libs/mali-f3/libnord2_mali_runtime.so --profile $B/tegu-air-profile.h >> runs-after-f3.log 2>&1
echo CHAIN-DONE
