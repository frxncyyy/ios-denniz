#!/bin/bash
cd $NORD2_WORK/gpu
while ps -eo comm | grep -qx "cc1\|rustc\|cargo\|ninja\|clang"; do sleep 4; done
nice -n 10 python3 tools/build_pipeline_pack.py --bundle build/hardware-20261025/c6k-test/offline-update/inputs/payload/Nord2Metal.bundle/Nord2Metal --corpus build/pipeline-pack-20261026/mini-corpus2.txt --work build/pipeline-pack-20261026/mini2 --replay build/pipeline-pack-20261026/mini/replay/pipeline-replay > build/pipeline-pack-20261026/mini2.log 2>&1
echo done rc=$?
