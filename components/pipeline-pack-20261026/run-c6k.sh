#!/bin/bash
cd $NORD2_WORK/gpu
while ps -eo comm | grep -qx "cc1\|rustc\|cargo\|ninja"; do sleep 5; done
nice -n 10 python3 tools/build_pipeline_pack.py --bundle build/hardware-20261025/c6k-test/offline-update/inputs/payload/Nord2Metal.bundle/Nord2Metal --corpus build/pipeline-corpus/corpus.txt --work build/pipeline-pack-20261026/c6k --tree build/pipeline-pack-20261026/src-c6k/gpu --defines "-DTEGU_INERT_PIPELINES -DTEGU_COMPOSITOR_DEFAULT_ON -DTEGU_JM_BATCH_DEFAULT -DTEGU_TILE_FETCH_DEFAULT=0 -DTEGU_AB_BUILTIN_DEFAULT" --logs build/hardware-20261025/c6k-test/ui350-usb/xnu-live.log build/hardware-20261025/c6k-test/ui654-usb/xnu-live.log > build/pipeline-pack-20261026/c6k-pack.log 2>&1
echo done rc=$?
