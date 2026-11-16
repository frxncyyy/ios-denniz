#!/usr/bin/env bash
# Light unit step: render oracle + bridge test (ASan), jobs1 nice 10, current tree.
G=$NORD2_WORK/gpu
O=$G/build/c6f-lowering-20261026/c6n
cd "$G"
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan|fsjobs|probe_host)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 10; done
SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
set -e
nice -n 10 cc $SAN -std=c11 -Wall -Wextra -Imetal -Ibuild/tegu-frontend -c metal/nord2_metal_render.c -o $O/render.o
nice -n 10 cc $SAN -std=c11 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -Imetal -Ibuild/tegu-frontend \
 -Ibuild/mesa-build/src/panfrost -Ibuild/mesa-src/src/panfrost -Ibuild/mesa-build/src -Ibuild/mesa-src/src \
 -Ibuild/mesa-build/include -Ibuild/mesa-src/include -Ibuild/mesa-build/src/compiler -Ibuild/mesa-src/src/compiler \
 tests/nord2_metal_render_test.c $O/render.o metal/tegu/blend_state.c -lm -o $O/render_semantics 2>&1 | grep -v "variable tracking\|int main\|^ *[0-9]* |\|^ *|" || true
$O/render_semantics && echo RENDER_OK
nice -n 10 cc $SAN -std=c11 -pthread -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude -Imetal -Ibuild/tegu-frontend -o $O/metal_bridge_test tests/nord2_metal_bridge_test.c metal/nord2_metal_bridge.c src/nord2_gpu_desc.c src/nord2_gpu_jm.c $O/render.o metal/tegu/blend_state.c
for e in NORD2_METAL_TRACE=0 NORD2_METAL_TRACE=1 NORD2_METAL_EXTENT=0; do env $e $O/metal_bridge_test build/callable-compiler-test/callable.tgp > $O/bridge.log 2>&1; echo "$e rc=$? pass=$(grep -c ^PASS $O/bridge.log)"; done
echo UNIT_DONE
