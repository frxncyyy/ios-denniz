#!/bin/bash
# pack_identity.sh <mali lib dir> <name>: replay the 142 device-seen pipeline records (pipeline-corpus/corpus.txt)
# through the existing pack replay binary (frontend of pipeline-pack-aa4073ac) with the given host Mali
# runtime, then hash every produced cache entry. Two runs with different Mali libs compare entry by entry.
G=$NORD2_WORK/gpu
W=$G/build/shader-coverage-20261027/pack-gate
LIB=$(realpath $1); OUT=$W/$2
rm -rf $OUT; mkdir -p $OUT/cache
LD_LIBRARY_PATH=$LIB TEGU_PIPELINE_CACHE_BUILD_OVERRIDE=nord2-arch9-aa4073aca7b29199c63b9e1fec525386a21526518880874cdc93a54eb505b4a6 TEGU_PIPELINE_CACHE=1 \
 nice -n 10 $G/build/pipeline-pack-aa4073ac/replay/pipeline-replay $G/build/ui-corpus-20261025/libraries $G/build/pipeline-corpus/corpus.txt $OUT/cache > $OUT/replay.log 2>&1
echo "rc=$? $(grep 'REPLAY SUMMARY' $OUT/replay.log | tail -1)"
(cd $OUT/cache && find . -type f | sort | xargs sha256sum) > $OUT/entries.sha256
sha256sum $LIB/libnord2_mali_runtime.so > $OUT/mali.sha256
wc -l < $OUT/entries.sha256
