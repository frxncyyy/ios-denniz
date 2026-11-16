#!/bin/bash
# build_air_so.sh <src dir with mali_*.c and air/> <outdir> [extra cflags...]
# Private host libnord2_mali_runtime.so with the AIR->NIR frontend (air/), linked against the shared
# host Mesa build's static libraries (read-only use of gpu/build/mesa-build). nice 10, one cc at a time.
set -e
G=$NORD2_WORK/gpu
W=$G/build/compile-speed-20261026
B=${MESA_B:-$G/build/mesa-build}
SRC=$(realpath $1); OUT=$(realpath -m $2); shift 2
mkdir -p $OUT/obj
cd $B
FLAGS=$(sed "s|\.\./mesa-src|$G/build/mesa-src|g" $W/tools/mali_cflags.txt)
objs=""
for f in $SRC/air/air_bitcode.c $SRC/air/air_layout.c $SRC/air/air_metallib.c $SRC/air/air_module.c $SRC/air/air_print.c $SRC/air/air_to_nir.c $SRC/air/air_intrinsics.c $SRC/mali_compute_compiler.c $SRC/mali_graphics_compiler.c; do
  o=$OUT/obj/$(basename ${f%.c}).o
  if [ ! -f $o ] || [ $f -nt $o ] || [ -n "$FORCE" ] || { [ $(basename $f) = mali_graphics_compiler.c ] && [ -n "$(find $SRC -maxdepth 1 \( -name '*.inc' -o -name '*.h' \) -newer $o)" ]; }; then
    eval nice -n 10 cc -fdiagnostics-color=never -I$SRC -I$SRC/air $FLAGS -DTEGU_MALI_LIBRARY_ONLY -DTEGU_MALI_AIR "$@" -c $f -o $o
  fi
  objs="$objs $o"
done
nice -n 10 c++ -o $OUT/libnord2_mali_runtime.so $objs \
 -Wl,--as-needed -Wl,--no-undefined -shared -fPIC -Wl,-soname,libnord2_mali_runtime.so -Wl,--start-group src/panfrost/compiler/libpanfrost_compiler.a src/panfrost/compiler/midgard/libpanfrost_midgard.a src/panfrost/compiler/midgard/libpanfrost_midgard_disasm.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_clflush.a src/util/libmesa_util_clflushopt.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/panfrost/compiler/bifrost/libpanfrost_bifrost.a src/panfrost/compiler/bifrost/libpanfrost_bifrost_disasm.a src/panfrost/compiler/bifrost/valhall/libpanfrost_valhall_disasm.a src/panfrost/model/libpanfrost_model_lib.a src/compiler/spirv/libvtn.a /usr/lib/libSPIRV-Tools-opt.so /usr/lib/libSPIRV-Tools.so /usr/lib/libSPIRV-Tools-link.so -pthread -lm /usr/lib/libunwind.so /usr/lib/libdrm.so -Wl,--end-group
sha256sum $OUT/libnord2_mali_runtime.so
