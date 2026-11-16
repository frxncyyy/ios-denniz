#!/usr/bin/env bash
# Host-only qualification for the tiler differential. No device access.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=build/tiler-review
mkdir -p "$OUT"
CC=${CC:-cc}
SAN=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
INC=(-Imetal -Ibuild/tegu-frontend)
./tools/check_preserved_baseline.sh
./tools/check_sync.sh
python3 tests/metal_probe_analysis_test.py
"$CC" "${SAN[@]}" -std=c11 "${INC[@]}" -c metal/nord2_metal_render.c -o "$OUT/render.o"
"$CC" "${SAN[@]}" -std=c11 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 "${INC[@]}" \
 -Ibuild/mesa-build/src/panfrost -Ibuild/mesa-src/src/panfrost -Ibuild/mesa-build/src -Ibuild/mesa-src/src \
 -Ibuild/mesa-build/include -Ibuild/mesa-src/include -Ibuild/mesa-build/src/compiler -Ibuild/mesa-src/src/compiler \
 tests/nord2_metal_render_test.c "$OUT/render.o" metal/tegu/blend_state.c -lm -o "$OUT/render_semantics"
"$OUT/render_semantics"
"$CC" "${SAN[@]}" -std=c11 -pthread -Wall -Wextra -Werror -Wno-misleading-indentation \
 -Iinclude "${INC[@]}" tests/nord2_metal_bridge_test.c metal/nord2_metal_bridge.c \
 src/nord2_gpu_desc.c src/nord2_gpu_jm.c "$OUT/render.o" metal/tegu/blend_state.c -o "$OUT/bridge_test"
for trace in 0 1; do
 NORD2_METAL_TRACE=$trace "$OUT/bridge_test" build/callable-compiler-test/callable.tgp
done
python3 tests/frontend_jm_test.py
python3 tests/metal_update_test.py
echo "PASS tiler host validation; hardware rasterisation remains unverified"
