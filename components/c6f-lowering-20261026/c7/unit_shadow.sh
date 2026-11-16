#!/usr/bin/env bash
# Units against the private shadow tree (pass-MRT WIP), jobs1 nice10.
G=$NORD2_WORK/gpu
C=$G/build/c6f-lowering-20261026/c7; S=$C/shadow
cd $G
SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
set -e
nice -n 10 cc $SAN -std=c11 -I$S/metal -Ibuild/tegu-frontend -c $S/metal/nord2_metal_render.c -o $C/render.o
nice -n 10 cc $SAN -std=c11 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -I$S/metal -Ibuild/tegu-frontend \
 -Ibuild/mesa-build/src/panfrost -Ibuild/mesa-src/src/panfrost -Ibuild/mesa-build/src -Ibuild/mesa-src/src \
 -Ibuild/mesa-build/include -Ibuild/mesa-src/include -Ibuild/mesa-build/src/compiler -Ibuild/mesa-src/src/compiler \
 $S/tests/nord2_metal_render_test.c $C/render.o metal/tegu/blend_state.c -lm -o $C/render_semantics 2>/dev/null
$C/render_semantics >/dev/null && echo RENDER_OK
nice -n 10 cc $SAN -std=c11 -pthread -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude -I$S/metal -Ibuild/tegu-frontend -o $C/bridge_test $S/tests/nord2_metal_bridge_test.c $S/metal/nord2_metal_bridge.c src/nord2_gpu_desc.c src/nord2_gpu_jm.c $C/render.o metal/tegu/blend_state.c
for e in NORD2_METAL_TRACE=0 NORD2_METAL_TRACE=1; do env $e $C/bridge_test build/callable-compiler-test/callable.tgp > $C/bridge.log 2>&1; echo "$e rc=$? pass=$(grep -c ^PASS $C/bridge.log)"; done
