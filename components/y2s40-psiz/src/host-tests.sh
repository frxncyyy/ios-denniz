#!/bin/bash
# How the host checks of y2s39 (and y2s38) were built and run (10 November 2026; no phone, nothing installed).  A native build of the SAME Mesa tree
# (softpipe + the Panfrost compiler libraries, asserts and NIR validation on) in a scratch directory, then this directory's air_metal.c against it.
#   T=<empty scratch dir> bash host-tests.sh
# glibc >= 2.41 declares once_flag/call_once in <stdlib.h>, which Mesa 25.0.7's c11/threads.h also defines: hostfix.h makes the two agree.
# air_metal.c only includes winsys_handle.h / drm_fourcc.h when a GPU driver is configured: hostfix2.h includes them for the softpipe-only build.
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$(dirname "$0")" && pwd); T=${T:?set T to an empty scratch directory}; H=$T/hostmesa
export PATH=$G/build/venv/bin:$PATH
printf '#ifndef __ASSEMBLER__\n#include <pthread.h>\n#define __once_flag_defined 1\n#define once_flag pthread_once_t\n#endif\n' > $T/hostfix.h
printf '#include <stdint.h>\n#include <stdbool.h>\n#include "frontend/winsys_handle.h"\n#include "drm-uapi/drm_fourcc.h"\n' > $T/hostfix2.h
meson setup $H $M --wrap-mode=nofallback --buildtype=debugoptimized -Db_ndebug=false -Dc_args="-g0 -include $T/hostfix.h" -Dcpp_args="-g0 -include $T/hostfix.h" \
  -Dplatforms= -Dgallium-drivers=softpipe,panfrost -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=disabled -Dshared-glapi=enabled \
  -Dgles1=disabled -Dgles2=enabled -Dopengl=true -Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled \
  -Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled -Dgallium-xa=disabled -Dlmsensors=disabled \
  -Dbuild-tests=false > $T/setup.log
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/compiler/air/libair.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
PAN="src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/lib/libpanfrost_lib.a src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a"
ninja -C $H $LIBS $PAN > $T/build.log
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
DEF="-D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -DHAVE_SOFTPIPE -DGALLIUM_SOFTPIPE -DHAVE_SWRAST -DHAVE_PTHREAD -DHAVE_STRUCT_TIMESPEC -DHAVE_ENDIAN_H -DHAVE_DLFCN_H -DHAVE_SECURE_GETENV -DHAVE___BUILTIN_BSWAP32 -DHAVE___BUILTIN_BSWAP64 -DHAVE___BUILTIN_CLZ -DHAVE___BUILTIN_CLZLL -DHAVE___BUILTIN_CTZ -DHAVE___BUILTIN_EXPECT -DHAVE___BUILTIN_FFS -DHAVE___BUILTIN_FFSLL -DHAVE___BUILTIN_POPCOUNT -DHAVE___BUILTIN_POPCOUNTLL -DHAVE___BUILTIN_UNREACHABLE -DHAVE___BUILTIN_TYPES_COMPATIBLE_P -DHAVE_FUNC_ATTRIBUTE_CONST -DHAVE_FUNC_ATTRIBUTE_FLATTEN -DHAVE_FUNC_ATTRIBUTE_MALLOC -DHAVE_FUNC_ATTRIBUTE_PURE -DHAVE_FUNC_ATTRIBUTE_UNUSED -DHAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT -DHAVE_FUNC_ATTRIBUTE_WEAK -DHAVE_FUNC_ATTRIBUTE_FORMAT -DHAVE_FUNC_ATTRIBUTE_PACKED -DHAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL -DHAVE_FUNC_ATTRIBUTE_NORETURN -DHAVE_FUNC_ATTRIBUTE_VISIBILITY -DHAVE_UINT128 -DUSE_GCC_ATOMIC_BUILTINS -DHAVE_TIMESPEC_GET -DMESA_DEBUG=0 -DLLVM_AVAILABLE=0 -DDRAW_LLVM_AVAILABLE=0"
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
A=$(for l in $LIBS; do printf "$H/$l "; done); P=$(for l in $PAN; do printf "$H/$l "; done)
cd $T
$CC -c $HERE/air_metal.c -o air_metal_host.o
cc -std=gnu11 -O1 -g -I$HERE -c $HERE/ycbcr_gpu_host_test.c -o test.o
c++ -o ycbcr_gpu_host_test test.o air_metal_host.o -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
./ycbcr_gpu_host_test                 # y2s38: the YCbCr pass on softpipe, against the floating-point reference
C=$G/build/camera-userland-20261109
cc -std=gnu11 -O2 -g -I$C/src -c $C/src/camera_isp.c -o camera_isp.o
cc -std=gnu11 -O1 -g -Wall -I$HERE -I$C/src -c $HERE/isp_gpu_host_test.c -o isp_test.o
c++ -o isp_gpu_host_test isp_test.o camera_isp.o air_metal_host.o -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
./isp_gpu_host_test                   # y2s39: the camera ISP on softpipe, against the bundle's camera_isp.c, byte for byte (QUICK=1 skips the 4096x3072 frames)
$CC -I$HERE -c $HERE/pan_compile_host_test.c -o pan.o
c++ -o pan_compile_host_test pan.o -Wl,--start-group $A $P -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
./pan_compile_host_test               # every shader of both through Panfrost's Valhall compiler (Mali-G77)
# y2s40: a .metallib's functions through the translator and Panfrost's compiler, the pipeline through rwa_render_create over a device that
# compiles as Panfrost does, and the fault guard.  METALLIB: e.g. CameraUI.framework/default.metallib taken out of the rootfs mirror
# (7z e gpu/build/mirror/rootfs-mirror.img iOSRoot/System/Library/PrivateFrameworks/CameraUI.framework/default.metallib).
# The fault this version fixes shows only in a build WITHOUT assertions, as the phone's is (meson configure -Db_ndebug=true, -DNDEBUG here):
# with the library of y2s39 the vertex function dies in bi_compile_variant; with -DRWA_TEST_WITHOUT_POINT_SIZE_MARK the guard refuses it.
if [ -n "${METALLIB:-}" ]; then
  mkdir -p t && cp $HERE/metallib_compile_host_test.c t/test.c     # away from any air_metal.c: the include comes from -I
  cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF -I$HERE $INC -c t/test.c -o metallib.o
  c++ -o metallib_compile_host_test metallib.o -Wl,--start-group $A $P -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
  ./metallib_compile_host_test "$METALLIB"
fi
