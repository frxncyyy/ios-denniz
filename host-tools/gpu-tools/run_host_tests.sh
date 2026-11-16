#!/usr/bin/env bash
# Offline Nord 2 GPU validation: production packers/MMU/scheduler/board under
# ASan+UBSan, the real pinned arch9 Mesa compiler, and ARM64 XNU compile of
# every new kernel unit.  No device, no USB, no rootfs.
set -euo pipefail
cd "$(dirname "$0")/.."
WORK=$PWD
OUT=$WORK/build/host-tests
mkdir -p "$OUT"
CC=${CC:-cc}
SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
INC="-Iinclude"

echo "== preserved audio/Wi-Fi/display baseline"
./tools/check_preserved_baseline.sh

echo "== ADT Mali node"
python3 ../tools/test-nord2-adt-gpu.py
python3 ../tools/mkadt-nord2.py "$OUT/interrupt-binding.adt"
python3 tests/adt_interrupt_import_test.py "$OUT/interrupt-binding.adt"

echo "== source sync"
./tools/check_sync.sh

echo "== pipeline pack tool (C cache -> pack -> C attach)"
python3 tests/pipeline_pack_tool_test.py
python3 tools/test_analyze_ab_run.py

echo "== arch9 contract"
./tools/check_arch9_contract.sh

echo "== descriptor byte parity (Mesa v9 oracle)"
$CC $SAN -std=c11 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 \
  $INC -Ibuild/mesa-build/src/panfrost -Ibuild/mesa-src/src/panfrost \
  -Ibuild/mesa-build/src -Ibuild/mesa-src/src -Ibuild/mesa-build/include \
  -Ibuild/mesa-src/include -Ibuild/mesa-build/src/compiler \
  -Ibuild/mesa-src/src/compiler -I/usr/include/libdrm \
  -o "$OUT/desc_parity" tests/nord2_desc_parity.c src/nord2_gpu_desc.c src/nord2_gpu_jm.c -lm
"$OUT/desc_parity"

echo "== randomized descriptor parity"
$CC $SAN -std=c11 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 \
  $INC -Ibuild/mesa-build/src/panfrost -Ibuild/mesa-src/src/panfrost \
  -Ibuild/mesa-build/src -Ibuild/mesa-src/src -Ibuild/mesa-build/include \
  -Ibuild/mesa-src/include -Ibuild/mesa-build/src/compiler \
  -Ibuild/mesa-src/src/compiler -I/usr/include/libdrm \
  -o "$OUT/desc_fuzz" tests/nord2_desc_fuzz.c src/nord2_gpu_desc.c -lm
"$OUT/desc_fuzz"

echo "== transport ABI and allocation bounds"
$CC $SAN -std=c11 $INC -o "$OUT/transport_test" tests/nord2_transport_test.c
"$OUT/transport_test"

echo "== MMU"
$CC $SAN -std=c11 $INC -o "$OUT/mmu_test" tests/nord2_mmu_test.c \
  src/nord2_gpu_mmu.c
"$OUT/mmu_test"

echo "== randomized MMU model"
$CC $SAN -std=c11 $INC -o "$OUT/mmu_fuzz" tests/nord2_mmu_fuzz.c \
  src/nord2_gpu_mmu.c
"$OUT/mmu_fuzz"

echo "== buffer/quarantine ownership"
$CC $SAN -std=c11 $INC -o "$OUT/mem_test" tests/nord2_mem_test.c \
  src/nord2_gpu_mmu.c src/nord2_gpu_mem.c
"$OUT/mem_test"

echo "== scheduler/quarantine"
$CC $SAN -std=c11 $INC -o "$OUT/sched_test" tests/nord2_sched_test.c \
  src/nord2_gpu_mmu.c src/nord2_gpu_mem.c src/nord2_gpu_sched.c
"$OUT/sched_test"

echo "== randomized scheduler"
$CC $SAN -std=c11 $INC -o "$OUT/sched_fuzz" tests/nord2_sched_fuzz.c \
  src/nord2_gpu_mmu.c src/nord2_gpu_mem.c src/nord2_gpu_sched.c
"$OUT/sched_fuzz"

echo "== SCPsys MTCMOS"
$CC $SAN -std=c11 $INC -o "$OUT/scpsys_test" tests/nord2_scpsys_test.c \
  src/nord2_gpu_scpsys.c
"$OUT/scpsys_test"

echo "== board/power"
$CC $SAN -std=c11 $INC -o "$OUT/board_test" tests/nord2_board_test.c \
  src/nord2_gpu_board.c src/nord2_gpu_rail.c src/nord2_gpu_clk.c \
  src/nord2_gpu_scpsys.c src/nord2_gpu_hw.c
"$OUT/board_test"

for module in hw va mmu_reclaim core rail owner; do
  case "$module" in
    hw) sources="src/nord2_gpu_hw.c" ;;
    va) sources="src/nord2_gpu_va.c" ;;
    mmu_reclaim) sources="src/nord2_gpu_mmu.c" ;;
    core) sources="src/nord2_gpu_core.c src/nord2_gpu_mmu.c src/nord2_gpu_va.c" ;;
    rail) sources="src/nord2_gpu_rail.c src/nord2_gpu_pmif.c" ;;
    owner) sources="src/nord2_gpu_owner.c -pthread" ;;
  esac
  echo "== $module"
  $CC $SAN -std=c11 -Wall -Wextra -Werror $INC -o "$OUT/${module}_test" "tests/nord2_${module}_test.c" $sources
  "$OUT/${module}_test"
done

echo "== Android clock/register/workaround parity"
ANDROID=${NORD2_ANDROID_SOURCE:-../../../../../../../nord2-current-kernel}
python3 tools/prepare_android_wa.py "$ANDROID/firmware/valhall-1691526.wa" build/generated/valhall_wa_firmware.h
python3 tests/android_register_oracle.py "$ANDROID"
python3 tests/android_clock_oracle.py "$ANDROID"
python3 tests/android_wa_oracle.py "$ANDROID"
python3 tests/android_mmu_oracle.py "$ANDROID"
$CC $SAN -std=c11 -Wall -Wextra -Werror $INC -Ibuild/generated -o "$OUT/wa_test" \
  tests/nord2_wa_test.c src/nord2_gpu_hw.c src/nord2_gpu_core.c \
  src/nord2_gpu_mmu.c src/nord2_gpu_va.c
"$OUT/wa_test"

echo "== JM chain builders"
$CC $SAN -std=c11 $INC -o "$OUT/jm_test" tests/nord2_jm_test.c \
  src/nord2_gpu_desc.c src/nord2_gpu_jm.c
"$OUT/jm_test"

echo "== Metal compute frontend core"
$CC $SAN -std=c11 -Imetal -o "$OUT/n2m_test" \
  metal/nord2_metal_host_test.c metal/nord2_metal.c
"$OUT/n2m_test"

echo "== native diagnostic syntax"
$CC -fsyntax-only -std=c11 -Itests/stubs metal/nord2_gpu_client.c

echo "== Metal compute frontend syntax"
clang -fsyntax-only -x objective-c -fblocks -fobjc-runtime=gnustep-2.0 \
  -Wno-objc-root-class metal/Nord2Metal.m

echo "== legacy minimal frontend syntax only (not installed)"

echo "== render plan composition"
$CC $SAN -std=c11 $INC -o "$OUT/render_test" \
  tests/nord2_render_plan_test.c src/nord2_gpu_desc.c src/nord2_gpu_jm.c
"$OUT/render_test"

echo "== production render-plan builder"
$CC $SAN -std=c11 $INC -o "$OUT/render_plan_module_test" \
  tests/render_plan_module_test.c src/nord2_gpu_desc.c src/nord2_gpu_jm.c \
  src/nord2_gpu_render_plan.c
"$OUT/render_plan_module_test"

echo "== arch9 Mesa compiler"
python3 tests/compiler_test.py | tee "$OUT/compiler.txt"
python3 tests/compiler_negative_test.py

echo "== real reused callable compiler (requires build_callable_compilers.py)"
python3 tests/callable_compiler_test.py | tee "$OUT/callable-compiler.txt"

echo "== real linked arch9 graphics compiler"
$CC $SAN -std=c11 -Imetal -o "$OUT/linked_shader_test" tests/nord2_linked_shader_test.c \
 -Lbuild/mesa-build/src/panfrost/clc -lnord2_mali_runtime \
 -Wl,-rpath,"$WORK/build/mesa-build/src/panfrost/clc"
"$OUT/linked_shader_test" build/compiler-test/fullscreen.vert.spv build/compiler-test/fullscreen.frag.spv
for stage in vert frag; do
 glslangValidator -V "tests/shaders/linked_sparse.$stage" -o "build/callable-compiler-test/linked_sparse.$stage.spv"
done
"$OUT/linked_shader_test" build/callable-compiler-test/linked_sparse.vert.spv build/callable-compiler-test/linked_sparse.frag.spv

echo "== arch9 graphics descriptors and real LOAD shader"
python3 tools/generate_mesa_pack_subset.py
bash tools/generate_preload.sh
$CC $SAN -std=c11 -Imetal -Ibuild/tegu-frontend -c metal/nord2_metal_render.c -o "$OUT/render.o"
$CC $SAN -std=c11 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -Imetal -Ibuild/tegu-frontend \
 -Ibuild/mesa-build/src/panfrost -Ibuild/mesa-src/src/panfrost -Ibuild/mesa-build/src -Ibuild/mesa-src/src \
 -Ibuild/mesa-build/include -Ibuild/mesa-src/include -Ibuild/mesa-build/src/compiler -Ibuild/mesa-src/src/compiler \
 tests/nord2_metal_render_test.c "$OUT/render.o" metal/tegu/blend_state.c -lm -o "$OUT/render_semantics"
"$OUT/render_semantics"
python3 tests/mesa_viewport_oracle.py
python3 tests/probe_surface_test.py
python3 tests/metal_probe_analysis_test.py
python3 tests/probe_log_reconcile_test.py
python3 tests/delta_final_verification_test.py
echo "== Metal/JM transport lifetime and concurrency"
$CC $SAN -std=c11 -pthread -Wall -Wextra -Werror -Wno-misleading-indentation $INC -Imetal -Ibuild/tegu-frontend \
 -o "$OUT/metal_bridge_test" tests/nord2_metal_bridge_test.c metal/nord2_metal_bridge.c \
 src/nord2_gpu_desc.c src/nord2_gpu_jm.c "$OUT/render.o" metal/tegu/blend_state.c
NORD2_METAL_TRACE=1 "$OUT/metal_bridge_test" build/callable-compiler-test/callable.tgp
echo "== actual IOSurface CPU methods"
python3 tests/iosurface_cpu_test.py
echo "== tessellator (drawPatches CPU tessellation, independent oracle)"
python3 tests/tessellator_test.py
echo "== Metal API surface (events, fences, queues, encoders, parallel, indirect, MSL source)"
python3 tests/api_surface_frontend_test.py

echo "== full reused Metal frontend ARM64 objects"
./tools/build_tegu_frontend_objects.sh | tee "$OUT/tegu-frontend.txt"

echo "== optional AIR render-pipeline oracle"
if [ -x "$WORK/build/cargo-target/release/metal2vulkan" ] && \
   [ -n "${NORD2_AIR_LLVM_BIN:-}" ]; then
  python3 tests/air_render_test.py \
    --translator "$WORK/build/cargo-target/release/metal2vulkan" \
    --llvm-bin "$NORD2_AIR_LLVM_BIN" | tee "$OUT/air-render.txt"
else
  echo "skipped: set NORD2_AIR_LLVM_BIN and build metal2vulkan (see docs/AIR_BRIDGE.md)"
fi

echo "== ARM64 XNU compile of new units"
if [ -x build/xnu-obj/xnu.obj/DEVELOPMENT_ARM64_SWEET ]; then :; fi
for source in src/nord2_gpu_*.c; do
  f=$(basename "$source" .c)
  python3 tools/xnu_staged_compile.py "build/xnu-stage/bsd/dev/$f.c" \
    --out "build/objs/$f.o" \
    --extra="-I$WORK/build/xnu-stage/bsd/dev" --extra=-Wno-error \
    > "$OUT/$f.compile.log" 2>&1
done
python3 tools/xnu_staged_compile.py build/xnu-stage/iokit/Kernel/arm/Nord2GPU.cpp \
  --cxx --out build/objs/Nord2GPU.o --extra=-Wno-error \
  --extra=-Wno-deprecated-declarations --extra=-Wno-unused-parameter \
  --extra=-Wno-missing-prototypes > "$OUT/Nord2GPU.compile.log" 2>&1
ar rcs build/libnord2gpu.a build/objs/nord2_gpu_*.o build/objs/Nord2GPU.o
ar t build/libnord2gpu.a | grep -qx nord2_gpu_scpsys.o
ar t build/libnord2gpu.a | grep -qx nord2_gpu_render_plan.o
echo "kernel units compiled; archive build/libnord2gpu.a"

echo "== candidate patch reverse-apply check"
(cd build/xnu-stage && patch -p1 -R --dry-run \
  < "$WORK/../xnu/patches/0084-nord2-gpu.patch" > "$OUT/patch-check.log" 2>&1)

echo "== synthetic kernel link-size delta (optional)"
CI_LINK=${NORD2_CI_LINK_DIR:-}
if [ -z "$CI_LINK" ]; then
  CI_LINK=$NORD2_REPO/research/ci-40034353275/xnu-kernel/kernel.development.sweet.link
fi
if [ -f "$CI_LINK/kernel.development.sweet.a" ]; then
  ./tools/measure_kernel_delta.sh "$CI_LINK" | tee "$OUT/kernel-delta.txt"
else
  echo "skipped: set NORD2_CI_LINK_DIR (see docs/BOOT_BUDGET.md)"
fi
echo "== offline addon preservation and rollback"
python3 tests/metal_install_test.py
python3 tests/metal_update_test.py
echo "== actual iOS bundle strict link and public-API probe"
python3 tools/link_metal_bundle.py > "$OUT/native-bundle.txt"
bash tools/build_metal_probe.sh > "$OUT/native-probe.txt" 2>&1
echo "all offline GPU tests passed"
