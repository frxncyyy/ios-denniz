#!/usr/bin/env bash
# Candidate build: frontend, bundle link, pipeline pack, addon (iOS compilers: build_native_compilers.py air|mali first).
# usage: gpu/tools/build_candidate.sh TAG   (e.g. c6w). Host only, no phone.
# NORD2_FRONTEND_DEFINES below is the production recipe since C6u: without it the compositor gate
# is off (backboardd gets no Metal) and the batch/tile-fetch/async defaults are not set.
set -uo pipefail
TAG=${1:?tag}
W=$NORD2_WORK
D=gpu/build/l4-$TAG
N=$(nproc)
cd "$W"; mkdir -p $D
export NORD2_FRONTEND_DEFINES="-DTEGU_INERT_PIPELINES -DTEGU_COMPOSITOR_DEFAULT_ON -DTEGU_JM_BATCH_DEFAULT -DTEGU_TILE_FETCH_DEFAULT=1 -DNORD2_PREFRAME_DEFAULT=1 -DTEGU_AB_BUILTIN_DEFAULT -DTEGU_ASYNC_PIPELINE_DEFAULT=1 -DTEGU_SHADOW_GPU_DEFAULT=2 -DN2MB_CHAIN_DEFAULT=1"; export NORD2_PIPELINE_CACHE_SCOPE=compilers NORD2_AIR_LLVM_BIN=/usr/bin CARGO_BUILD_JOBS=$N NORD2_NINJA_JOBS=$N
step() { n=$1; shift; echo "[$(date +%T)] $n start"; "$@" > $D/$n.log 2>&1; rc=$?; echo "[$(date +%T)] $n rc=$rc"; [ $rc -eq 0 ] || exit $rc; }


step frontend    bash -c 'cd gpu && bash tools/build_tegu_frontend_objects.sh'
step link        bash -c 'cd gpu && python3 tools/link_metal_bundle.py'
step expand      python3 gpu/tools/expand_mali_variants.py gpu/build/pipeline-corpus/corpus.txt gpu/build/pipeline-corpus/corpus-expanded.txt
step pack        python3 gpu/tools/build_pipeline_pack.py --bundle gpu/build/native/Nord2Metal.bundle \
                   --corpus gpu/build/pipeline-corpus/corpus-expanded.txt --work gpu/build/pipeline-pack-$TAG \
                   --pack gpu/build/pipeline-pack-$TAG/PipelinePack.tpcpack
step package     python3 gpu/tools/package_metal_addon.py --output gpu/build/springboard-candidate-$TAG \
                   --ca-prefs --pipeline-pack gpu/build/pipeline-pack-$TAG/PipelinePack.tpcpack
echo ALLDONE
