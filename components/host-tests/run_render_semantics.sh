#!/bin/bash
set -eu
cd "$(dirname "$0")/../.."
CC=clang
SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
OUT=$PWD/build/host-tests
$CC $SAN -std=c11 -Imetal -Ibuild/tegu-frontend -c metal/nord2_metal_render.c -o "$OUT/render.o"
$CC $SAN -std=c11 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -Imetal -Ibuild/tegu-frontend \
 -Ibuild/mesa-build/src/panfrost -Ibuild/mesa-src/src/panfrost -Ibuild/mesa-build/src -Ibuild/mesa-src/src \
 -Ibuild/mesa-build/include -Ibuild/mesa-src/include -Ibuild/mesa-build/src/compiler -Ibuild/mesa-src/src/compiler \
 tests/nord2_metal_render_test.c "$OUT/render.o" metal/tegu/blend_state.c -lm -o "$OUT/render_semantics"
"$OUT/render_semantics"
