#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for stage in vert frag; do
 glslangValidator -V "tests/shaders/nord2_preload.$stage" -o "build/tegu-frontend/preload.$stage.spv"
done
glslangValidator -V tests/shaders/nord2_frame_preload.frag -o build/tegu-frontend/frame_preload.frag.spv
glslangValidator -V tests/shaders/nord2_preload_uint.frag -o build/tegu-frontend/preload_uint.frag.spv
glslangValidator -V tests/shaders/nord2_frame_preload_uint.frag -o build/tegu-frontend/frame_preload_uint.frag.spv
clang -O2 -Wall -Wextra -Werror -Imetal tools/generate_preload.c -Lbuild/mesa-build/src/panfrost/clc \
 -lnord2_mali_runtime -Wl,-rpath,"$PWD/build/mesa-build/src/panfrost/clc" -o build/tegu-frontend/generate_preload
build/tegu-frontend/generate_preload build/tegu-frontend/preload.vert.spv build/tegu-frontend/preload.frag.spv build/tegu-frontend/frame_preload.frag.spv \
 build/tegu-frontend/preload_uint.frag.spv build/tegu-frontend/frame_preload_uint.frag.spv > build/tegu-frontend/nord2_preload.h
