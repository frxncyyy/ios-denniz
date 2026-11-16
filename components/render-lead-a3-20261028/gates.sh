#!/usr/bin/env bash
# render-lead A3 + chain-fake gate set (host only). Usage: gates.sh <tag> [subset]
W=$NORD2_WORK
G=$W/gpu;L=$G/build/render-lead-a3-20261028/gates-$1;mkdir -p $L;cd $G
export NORD2_AIR_LLVM_BIN=/usr/bin NORD2_PIPELINE_CACHE_SCOPE=compilers
run(){ name=$1;shift; ( "$@" ) > $L/$name.log 2>&1; echo "$name rc=$?"; }
want(){ [ -z "$SUBSET" ] || [[ " $SUBSET " == *" $1 "* ]]; }
SUBSET="$2"
want vk && run convert_vk nice -n 10 python3 tests/convert_shader_vk_test.py
want tf && run texture_format nice -n 10 python3 tests/texture_format_frontend_test.py
want ca && run ca_pf0 env NORD2_METAL_PREFRAME=0 nice -n 10 python3 build/c6f-lowering-20261026/ca_backboardd_host_test.py
want ca && run ca_pf1 env NORD2_METAL_PREFRAME=1 nice -n 10 python3 build/c6f-lowering-20261026/ca_backboardd_host_test.py
want cachain && run ca_chain1 env NORD2_METAL_CHAIN=1 nice -n 10 python3 build/c6f-lowering-20261026/ca_backboardd_host_test.py
want cachain && run ca_chain2 env NORD2_METAL_CHAIN=2 NORD2_METAL_PREFRAME=1 nice -n 10 python3 build/c6f-lowering-20261026/ca_backboardd_host_test.py
want render && run render nice -n 10 python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py
want renderchain && run render_chain1 env NORD2_METAL_CHAIN=1 nice -n 10 python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py
want renderchain && run render_chain2 env NORD2_METAL_CHAIN=2 nice -n 10 python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py
want async && run async nice -n 10 python3 build/encode-cpu-20261026/async_frontend_test.py
want integ && run integration bash -c "cd $W && NORD2_AIR_LLVM_BIN=/usr/bin nice -n 10 python3 gpu/tests/metal_resource_integration_test.py --candidate"
echo ALLDONE
