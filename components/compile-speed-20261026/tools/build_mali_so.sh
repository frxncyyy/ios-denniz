#!/bin/bash
# build_mali_so.sh <dir with mali_*.c/.h> <outdir> [extra cflags...] -- private host libnord2_mali_runtime.so
# against the shared host Mesa build's static libraries (read-only use of gpu/build/mesa-build).
set -e
W=$NORD2_WORK/gpu/build/compile-speed-20261026
B=${MESA_B:-$W/../mesa-build}
SRC=$(realpath $1); OUT=$(realpath -m $2); shift 2
mkdir -p $OUT/obj
cd $B
for f in mali_compute_compiler mali_graphics_compiler; do
  eval nice -n 10 cc -I$SRC $(sed "s|\.\./mesa-src|$NORD2_WORK/gpu/build/mesa-src|g" $W/tools/mali_cflags.txt) -DTEGU_MALI_LIBRARY_ONLY "$@" -c $SRC/$f.c -o $OUT/obj/$f.o
done
nice -n 10 c++ -o $OUT/libnord2_mali_runtime.so $OUT/obj/mali_compute_compiler.o $OUT/obj/mali_graphics_compiler.o \
 -Wl,--as-needed -Wl,--no-undefined -shared -fPIC -Wl,-soname,libnord2_mali_runtime.so -Wl,--start-group src/panfrost/compiler/libpanfrost_compiler.a src/panfrost/compiler/midgard/libpanfrost_midgard.a src/panfrost/compiler/midgard/libpanfrost_midgard_disasm.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_clflush.a src/util/libmesa_util_clflushopt.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/panfrost/compiler/bifrost/libpanfrost_bifrost.a src/panfrost/compiler/bifrost/libpanfrost_bifrost_disasm.a src/panfrost/compiler/bifrost/valhall/libpanfrost_valhall_disasm.a src/panfrost/model/libpanfrost_model_lib.a src/compiler/spirv/libvtn.a /usr/lib/libSPIRV-Tools-opt.so /usr/lib/libSPIRV-Tools.so /usr/lib/libSPIRV-Tools-link.so -pthread -lm /usr/lib/libunwind.so /usr/lib/libdrm.so -Wl,--end-group
sha256sum $OUT/libnord2_mali_runtime.so
